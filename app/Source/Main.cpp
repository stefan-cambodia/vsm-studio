#include <JuceHeader.h>
#include <algorithm>
#include <cxxabi.h>
#include <cstdlib>
#include <vector>
#include "MainComponent.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "ui/UiScale.h"
#include "ui/Langue.h"
#if VSM_WITH_CLAP || VSM_WITH_VST3
#include "plugins/PluginScanner.h"
#include <cstdio>
#endif

// ---------------------------------------------------------------------------
// Application JUCE (voir ARCHITECTURE.md pour l'architecture complète).
//
// Compilée, liée et lancée avec succès sur machine réelle (Linux/ALSA) --
// voir ARCHITECTURE.md section 6 pour l'historique de vérification. Pour
// (re)construire :
//
//   cmake -B build -DVSM_BUILD_APP=ON
//   cmake --build build --target VintageSynthMidiStudio
//
// (nécessite une connexion réseau pour récupérer JUCE via FetchContent, et
// les bibliothèques système habituelles : ALSA/X11 sous Linux, Xcode sous
// macOS, Visual Studio sous Windows — voir app/CMakeLists.txt)
// ---------------------------------------------------------------------------

/// D58 : LE PLANCHER DE LA FENÊTRE, en pixels LOGIQUES (donc à l'échelle de
/// l'interface). Mesuré : à 900x660 les légendes de la façade de machine se
/// lisent encore, abrégées ; à 800x600 elles ont TOUTES disparu (« OSCILL... »
/// et des rangées de points) et le bandeau du master déborde. La lisibilité
/// prime sur « ça tient dans la case ». Un écran plus petit se règle par
/// « Affichage ▸ Taille de l'interface ».
inline constexpr int kLargeurMinimale = 900;
inline constexpr int kHauteurMinimale = 660;

namespace {
/// D163 : CHRONOMÉTRER UN DESSIN, par le chemin que le système emprunte.
///
/// POURQUOI IL A FALLU L'ÉCRIRE. `VSM_CAPTURE` dessine UNE fois et écrit un PNG :
/// cette unique passe paye l'allocation de l'image, le premier remplissage des
/// caches de police et de glyphes, et le dessin lui-même, sans les distinguer.
/// Un chiffre de réactivité demande le contraire -- la même surface repeinte N
/// fois dans une image allouée une seule fois, et la MÉDIANE publiée avec son
/// minimum et son maximum.
///
/// `paintEntireComponent` est exactement ce que JUCE appelle quand le système
/// demande une image : rien n'est simulé ici, et c'est la seule raison pour
/// laquelle le chiffre veut dire quelque chose.
void chronometrerPeinture(juce::Component* cible, const juce::String& nom, int passes) {
    if (cible == nullptr || cible->getWidth() <= 0 || cible->getHeight() <= 0) return;
    juce::Image image(juce::Image::ARGB, cible->getWidth(), cible->getHeight(), true);
    std::vector<double> millisecondes;
    millisecondes.reserve(static_cast<size_t>(passes));
    for (int i = 0; i < passes; ++i) {
        const double depart = juce::Time::getMillisecondCounterHiRes();
        {
            juce::Graphics g(image);
            cible->paintEntireComponent(g, true);
        }
        millisecondes.push_back(juce::Time::getMillisecondCounterHiRes() - depart);
    }
    std::vector<double> triees = millisecondes;
    std::sort(triees.begin(), triees.end());
    const double mediane = triees[triees.size() / 2];
    std::fputs(("VSM_PEINTURE : " + nom.toStdString() + " " + std::to_string(cible->getWidth()) + "x"
                + std::to_string(cible->getHeight()) + " : m\u00e9diane "
                + juce::String(mediane, 2).toStdString() + " ms (min "
                + juce::String(triees.front(), 2).toStdString() + ", max "
                + juce::String(triees.back(), 2).toStdString() + ", 1re passe "
                + juce::String(millisecondes.front(), 2).toStdString() + ") sur "
                + std::to_string(passes) + " passes\n").c_str(), stderr);
}

/// `VSM_PEINTURE_ENFANTS=N` : la profondeur à laquelle descendre dans les
/// enfants (0 par défaut -- les fenêtres seules, comme au premier relevé).
int profondeurDesEnfants() {
    const char* p = std::getenv("VSM_PEINTURE_ENFANTS");
    if (p == nullptr || *p == 0) return 0;
    return juce::jlimit(0, 6, juce::String(p).getIntValue());
}

/// D163 (suite) : LE NOM DE CLASSE D'UN COMPOSANT, pour pouvoir nommer celui qui
/// coûte. Beaucoup de composants n'ont pas de `getName()` -- un panneau interne
/// n'a aucune raison d'en porter un --, et un chiffre sans nom ne désigne rien.
juce::String nomDeClasse(juce::Component* c) {
    const char* brut = typeid(*c).name();
    int statut = 0;
    char* lisible = abi::__cxa_demangle(brut, nullptr, nullptr, &statut);
    const juce::String nom = (statut == 0 && lisible != nullptr) ? juce::String(lisible) : juce::String(brut);
    std::free(lisible);
    return nom.fromLastOccurrenceOf("::", false, false).isNotEmpty()
             ? nom.fromLastOccurrenceOf("::", false, false)
             : nom;
}

/// Les enfants visibles d'un composant, chronométrés un par un jusqu'à une
/// profondeur donnée. C'est ce qui a manqué au premier relevé de D163 : le
/// panneau « Piano Roll » coûtait 30 ms quand la somme des étapes de
/// `PianoRollComponent::paint` n'en faisait que 1,6 -- le coût était chez un
/// VOISIN, et aucun chiffre ne pouvait le dire.
void chronometrerEnfants(juce::Component* parent, const juce::String& prefixe, int passes, int profondeur) {
    if (profondeur <= 0) return;
    for (int i = 0; i < parent->getNumChildComponents(); ++i) {
        auto* enfant = parent->getChildComponent(i);
        if (enfant == nullptr || !enfant->isVisible()) continue;
        if (enfant->getWidth() <= 0 || enfant->getHeight() <= 0) continue;
        const juce::String nom = prefixe + "/" + nomDeClasse(enfant);
        chronometrerPeinture(enfant, nom, passes);
        chronometrerEnfants(enfant, nom, passes, profondeur - 1);
    }
}

/// La fenêtre socle PUIS chaque fenêtre flottante visible -- même parcours que
/// `VSM_CAPTURE_PANNEAUX` (D55.2), pour que ce qui est chronométré soit ce que
/// la photo montre.
void chronometrerToutesLesPeintures(juce::Component* socle, int passes) {
    // LE NOM EST « socle », SANS ACCENT, ET C'EST UNE DÉCISION (D193). Le premier
    // essai écrivait « fenêtre » par `fromUTF8` — un accent impose ce détour,
    // `juce::String(const char*)` lisant ses octets en Latin-1 (ui/Langue.h), et
    // le tout premier jet avait d'ailleurs produit « fenÃªtre » au journal. Mais
    // `tools/inventaire_langue.py`, la garde de la langue (D150), compte tout
    // littéral ainsi construit comme du TEXTE D'ÉCRAN : ces deux lignes ont fait
    // passer son compte de 7 à 9 alors qu'elles ne s'affichent nulle part. « socle »
    // est le mot du dépôt pour cette fenêtre, il n'a pas d'accent, et la garde
    // retrouve son chiffre.
    chronometrerPeinture(socle, "socle", passes);
    chronometrerEnfants(socle, "socle", passes, profondeurDesEnfants());
    for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i) {
        auto* fenetre = juce::TopLevelWindow::getTopLevelWindow(i);
        if (fenetre == nullptr || !fenetre->isVisible()) continue;
        juce::Component* contenu = fenetre;
        if (auto* doc = dynamic_cast<juce::DocumentWindow*>(fenetre))
            if (auto* c = doc->getContentComponent()) contenu = c;
        if (contenu == socle) continue;
        chronometrerPeinture(contenu, fenetre->getName(), passes);
        chronometrerEnfants(contenu, fenetre->getName(), passes, profondeurDesEnfants());
    }
}

/// Le nombre de passes demandé, et le délai d'attente, lus une seule fois.
int passesDePeinture() {
    const char* peinture = std::getenv("VSM_PEINTURE");
    if (peinture == nullptr || *peinture == 0 || *peinture == '0') return 0;
    return juce::jmax(3, juce::String(peinture).getIntValue());
}
} // namespace

class VintageSynthMidiStudioApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Vintage Synth MIDI Studio"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        // AVANT toute construction de composant : TrackListComponent
        // interroge PluginRegistry dès la construction de sa première
        // ligne (voir TrackListComponent.cpp) pour peupler le combo
        // "instrument" avec les plugins RÉELLEMENT disponibles.
        vsm::audio::plugin::registerBuiltInPlugins();

        // AVANT la première fenêtre : le facteur d'échelle détermine leur
        // taille physique, et JUCE ne redimensionne pas rétroactivement ce
        // qui existe déjà. Réglage conservé d'une exécution à l'autre --
        // voir ui/UiScale.h.
        vsm::app::ui::UiScale::applySavedAtStartup();
        // D73 : LA LANGUE AVANT LA PREMIÈRE FENÊTRE, comme l'échelle. Les
        // libellés sont lus à la construction des composants ; une langue posée
        // après aurait laissé la moitié de l'interface dans l'autre.
        vsm::app::ui::Langue::appliquerAuDemarrage();

        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override { mainWindow = nullptr; }

    // D175 (A30) : ON DEMANDE AVANT DE QUITTER, quand il y a quelque chose à
    // perdre. `MainComponent::demanderAvantDeQuitter` rend `true` quand la
    // question est posée : c'est alors sa réponse qui appellera `quit()`.
    //
    // CE CHEMIN EST CELUI DE L'UTILISATEUR -- le bouton de fermeture, « Quitter »
    // du menu, le gestionnaire de fenêtres. La fin d'une course de BANC, elle,
    // n'est pas un geste d'utilisateur : elle appelle `quit()` directement (voir
    // la fin du bloc `VSM_CAPTURE`), sans quoi la boîte modale ferait expirer
    // tous les bancs du dépôt.
    void systemRequestedQuit() override {
        if (mainWindow != nullptr)
            if (auto* principal = dynamic_cast<MainComponent*>(mainWindow->getContentComponent()))
                if (principal->demanderAvantDeQuitter([this] { quit(); }))
                    return;
        quit();
    }
    void anotherInstanceStarted(const juce::String&) override {}

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xff1a1a1e),
                              DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            auto* content = new MainComponent();
            std::pair<int, int> tailleDemandee { 0, 0 };   // D58
            setContentOwned(content, true); // la fenêtre s'ajuste à la taille du contenu (menu + transport)
            centreWithSize(content->getWidth(), content->getHeight());
            setResizable(true, true);
            // D58 : UN PLANCHER, MESURÉ. Rien n'en fixait, et l'on pouvait
            // réduire la fenêtre jusqu'à ce que la façade de machine perde
            // TOUTES ses légendes (à 800x600 logiques : « OSCILL... » et des
            // rangées de points) et que le bandeau du master déborde. Le
            // plancher est pris là où la mesure le place : à 900x660 les
            // légendes se lisent encore, abrégées ; en dessous elles
            // disparaissent. La lisibilité prime sur « ça tient dans la
            // case », comme partout dans cette application.
            //
            // EN PIXELS LOGIQUES, donc à l'échelle de l'interface : à 150 %
            // ce plancher vaut 1350x990 pixels réels, et un écran plus petit
            // se règle par « Affichage ▸ Taille de l'interface » plutôt qu'en
            // rendant l'application illisible.
            setResizeLimits(kLargeurMinimale, kHauteurMinimale, 32000, 32000);
            // VSM_TAILLE=LARGEURxHAUTEUR (pixels logiques) : la taille de la
            // fenêtre pour un autoportrait. Sans elle, l'autoportrait prend
            // la taille mémorisée, et une disposition qui ne tient qu'à une
            // certaine largeur -- la barre de transport à 1 280 px -- ne se
            // vérifie pas.
            if (const char* taille = std::getenv("VSM_TAILLE"); taille != nullptr && *taille) {
                const juce::String t(taille);
                int l = t.upToFirstOccurrenceOf("x", false, true).getIntValue();
                int h = t.fromFirstOccurrenceOf("x", false, true).getIntValue();
                if (l > 200 && h > 100) {
                    // D58 : LE PLANCHER VAUT AUSSI ICI. Une image prise sous
                    // le plancher montrerait une disposition que l'application
                    // n'accepte pas à la souris -- c'est-à-dire un défaut que
                    // personne ne peut atteindre, et l'on passerait du temps à
                    // le corriger.
                    const int lBorne = juce::jmax(l, kLargeurMinimale);
                    const int hBorne = juce::jmax(h, kHauteurMinimale);
                    if (lBorne != l || hBorne != h)
                        std::fputs(("VSM_TAILLE : " + std::to_string(l) + "x" + std::to_string(h)
                                    + " est sous le plancher de "
                                    + std::to_string(kLargeurMinimale) + "x"
                                    + std::to_string(kHauteurMinimale)
                                    + " \u2014 remont\u00e9\n").c_str(), stderr);
                    l = lBorne; h = hBorne;
                    centreWithSize(l, h);
                    // D58 : ET ON LE DIT À LA FENÊTRE, sans quoi la
                    // disposition en fenêtre unique recouvre la taille
                    // demandée trois lignes plus bas.
                    content->forceWindowSize();
                    tailleDemandee = { l, h };
                } else {
                    std::fputs(("VSM_TAILLE : \"" + std::string(taille)
                                + "\" illisible ou hors bornes (largeur > 200, hauteur > 100) "
                                  "\u2014 la taille m\u00e9moris\u00e9e est gard\u00e9e\n").c_str(),
                               stderr);
                }
            }
            setVisible(true);

            // Seulement maintenant : la fenêtre socle a une position
            // d'écran réelle, les fenêtres flottantes peuvent se
            // positionner par rapport à elle (voir MainComponent.h).
            content->showFloatingPanels();



            // AUTOPORTRAIT (VSM_CAPTURE=sortie.png) : la fenêtre se rend
            // elle-même en PNG deux secondes après l'ouverture, puis quitte.
            // Même raison d'être que les outils vsm-*-preview : sous Wayland,
            // aucun outil externe ne sait ni viser cette fenêtre ni la faire
            // passer devant un terminal -- une interface qu'on ne peut pas
            // regarder est une interface qu'on ne peut pas juger.
            // VSM_PROJET=dossier : ouvrir un projet AVANT la capture. Sans
            // cela, l'autoportrait ne montrait que le projet vide -- or ce
            // qu'on a besoin de regarder, c'est presque toujours une machine
            // ou un arrangement précis, et le sélecteur de machine ne
            // s'atteint qu'à la souris. Même raison d'être que VSM_VUE : sous
            // Wayland, une interface qu'on ne peut pas piloter sans souris
            // est une interface qu'on ne peut pas juger.
            if (const char* projet = std::getenv("VSM_PROJET"); projet != nullptr && *projet) {
                const juce::File dossier =
                    juce::File::getCurrentWorkingDirectory().getChildFile(projet);
                // PANNE MUETTE INTERDITE, Y COMPRIS DANS L'OUTIL QUI SERT À
                // VÉRIFIER. Un projet illisible n'ouvrait qu'une alerte
                // graphique -- que la capture, prise deux secondes plus tard
                // sur le composant principal, ne montre même pas. On croyait
                // donc regarder son projet en regardant le projet vide, et
                // rien ne le disait. Ce mode est piloté depuis un terminal :
                // c'est au terminal qu'il doit se plaindre.
                if (!dossier.isDirectory())
                    std::fputs(("VSM_PROJET : dossier introuvable — "
                                + dossier.getFullPathName().toStdString() + "\n").c_str(), stderr);
                else if (!content->openProjectFolderForCapture(dossier))
                    std::fputs(("VSM_PROJET : projet illisible dans "
                                + dossier.getFullPathName().toStdString()
                                + " — la capture montrera le projet par défaut\n").c_str(), stderr);
            }
            // VSM_IMPORT=fichier : importer un projet d'un autre DAW au
            // démarrage. Même raison d'être que VSM_PROJET — sans cela, cet
            // écran ne serait vérifiable qu'à la souris, et le dépôt refuse
            // de déclarer une interface invérifiable.
            if (const char* aImporter = std::getenv("VSM_IMPORT");
                aImporter != nullptr && *aImporter) {
                const juce::File fichier =
                    juce::File::getCurrentWorkingDirectory().getChildFile(aImporter);
                if (!fichier.existsAsFile())
                    std::fputs(("VSM_IMPORT : fichier introuvable — "
                                + fichier.getFullPathName().toStdString() + "\n").c_str(), stderr);
                else
                    content->importDawProjectForCapture(fichier);
            }
            if (const char* vues = std::getenv("VSM_VUE"); vues != nullptr && *vues) {
                juce::StringArray liste;
                liste.addTokens(juce::String::fromUTF8(vues), ",", "");
                for (const auto& v : liste) content->applyViewCommand(v.trim());
            }
            // VSM_RAPPORT=1 : montrer le rapport de reconstruction du projet
            // ouvert (VSM_PROJET) avant la capture. Même raison d'être que
            // VSM_IMPORT : cet écran ne s'atteint autrement qu'à la souris,
            // et une interface qu'on ne peut pas photographier ne se juge pas.
            // VSM_FILTRE=texte : poser le filtre de la liste des pistes
            // (D19.2). Même raison d'être que VSM_VUE — un champ de saisie ne
            // se remplit qu'au clavier, et une capture d'un champ VIDE ne
            // prouve pas que le filtre filtre.
            if (const char* filtre = std::getenv("VSM_FILTRE"); filtre != nullptr && *filtre)
                content->setTrackFilterForCapture(juce::String::fromUTF8(filtre));
            // VSM_PLUGIN=chemin (D102) : le fichier que le PROCHAIN sélecteur de
            // plugin rendra -- AVANT VSM_MENU, dont le geste l'ouvre. Seul le
            // sélecteur est sauté ; le menu, et ce qu'on fait du fichier, restent
            // ceux de l'utilisateur.
            if (const char* plugin = std::getenv("VSM_PLUGIN"); plugin != nullptr && *plugin)
                content->setPluginFileForCapture(juce::File::getCurrentWorkingDirectory().getChildFile(
                    juce::String::fromUTF8(plugin)));
            // VSM_FICHIER=chemin (D108) : le même crochet, sous un nom qui ne dit plus
            // « plugin » -- tout sélecteur qui passe par `prendreLeFichierDeBanc`
            // (l'original de l'écoute A/B, par exemple).
            // D213 : PLUSIEURS FICHIERS, séparés par « ; », pour les sélecteurs
            // qui acceptent une sélection multiple (l'import audio). Le premier
            // sert aussi aux sélecteurs à un seul fichier : un banc qui n'en
            // nomme qu'un se comporte comme avant.
            if (const char* fichier = std::getenv("VSM_FICHIER"); fichier != nullptr && *fichier) {
                juce::StringArray noms;
                noms.addTokens(juce::String::fromUTF8(fichier), ";", "");
                juce::Array<juce::File> fichiers;
                for (const auto& nom : noms)
                    if (nom.trim().isNotEmpty())
                        fichiers.add(juce::File::getCurrentWorkingDirectory().getChildFile(nom.trim()));
                if (!fichiers.isEmpty()) {
                    content->setPluginFileForCapture(fichiers.getFirst());
                    content->poserLesFichiersDeBanc(fichiers);
                }
            }
            // VSM_MENU=libellé[;libellé…] : exécuter des entrées de menu par
            // leur LIBELLÉ avant la capture (D20). Trois gestes de cet audit
            // ne vivent que dans le menu contextuel d'un clip ; leurs jumeaux
            // du menu Édition s'atteignent ainsi sans souris, dans l'ordre
            // écrit (« Tout sélectionner;Répéter la sélection ... »).
            // VSM_PRESET_PISTE=nom : écrire la piste choisie comme preset de
            // piste (D22.5) avant VSM_MENU, pour que « Appliquer un preset de
            // piste ▸ nom » ait quelque chose à lister. La boîte qui demande
            // le nom ne se photographie pas.
            if (const char* nom = std::getenv("VSM_PRESET_PISTE"); nom != nullptr && *nom)
                if (!content->saveTrackPresetForCapture(juce::String::fromUTF8(nom)))
                    std::fputs("VSM_PRESET_PISTE : preset non \u00e9crit (aucune piste choisie, ou dossier illisible)\n", stderr);
            if (const char* entrees = std::getenv("VSM_MENU"); entrees != nullptr && *entrees) {
                juce::StringArray liste;
                liste.addTokens(juce::String::fromUTF8(entrees), ";", "");
                for (const auto& e : liste) content->runMenuEntryForCapture(e.trim());
            }
            // VSM_GESTE_PISTE=muet|renommer:…|volume:…|couleur:…|choisir:…
            // Les gestes qui n'existent qu'au bouton ou au clic d'une ligne :
            // aucun menu ne les porte, et sans eux l'effet de D36.1, D37 et
            // D38 ne se photographierait pas. Ils passent par les MÊMES
            // méthodes que la souris, faute de quoi ils vérifieraient un
            // chemin que personne n'emprunte.
            //
            // APRÈS VSM_MENU, ET C'EST UNE CORRECTION (D38) : placé avant, un
            // « choisir:0,1,2 » s'appliquait à une liste d'une seule piste,
            // puis les pistes ajoutées par le menu reconstruisaient la liste et
            // emportaient la sélection. La capture ne montrait alors AUCUNE
            // différence -- et c'est la mesure, non la lecture, qui l'a dit.
            // PLUSIEURS GESTES, SÉPARÉS PAR « ; », comme VSM_MENU : montrer un
            // lot demande d'abord de le choisir, puis d'agir dessus, et deux
            // variables pour un enchaînement en cacheraient l'ordre.
            if (const char* gestes = std::getenv("VSM_GESTE_PISTE"); gestes != nullptr && *gestes) {
                juce::StringArray suite;
                suite.addTokens(juce::String::fromUTF8(gestes), ";", "");
                for (const auto& g : suite)
                    if (g.trim().isNotEmpty() && !content->runTrackGestureForCapture(g.trim()))
                        // D214 : LE GESTE EST NOMMÉ, et « inconnu » ne couvre plus un
                        // geste CONNU qui a échoué. « cliquer:Exporter... » a rendu faux
                        // parce qu'aucun bouton ne portait ce texte -- la ligne disait
                        // « geste inconnu », et c'est le verbe qu'on allait soupçonner
                        // au lieu du libellé. La leçon de D147, à l'envers.
                        std::fputs((juce::String("VSM_GESTE_PISTE : ") + g.trim()
                                    + juce::String::fromUTF8(u8" \u2014 refusé (geste inconnu, ou sans effet : "
                                                             u8"voir la ligne au-dessus)\n")).toRawUTF8(), stderr);
            }
            // D341 : VSM_GESTE_APRES=<ms>:<geste>[;<ms>:<geste>…] -- LE MÊME
            // GESTE, JOUÉ PLUS TARD, sur le thread de message.
            //
            // POURQUOI IL A FALLU L'ÉCRIRE. Tous les verbes du banc agissent au
            // DÉMARRAGE, d'un bloc : rien ne pouvait toucher une commande qui
            // n'a de sens qu'une fois un travail de fond engagé. Le cas payé est
            // celui de D339 : le bouton « Annuler » de la fenêtre de
            // reconstruction existe dès la première seconde, mais presser
            // « Annuler » avant que demucs ne démarre ne mesure rien de ce qu'on
            // veut mesurer -- c'est l'arrêt du GROUPE de processus qui est en
            // cause, et il n'y a de groupe qu'une fois la séparation partie.
            //
            // LE GESTE QUI NE SERA JAMAIS JOUÉ EST DIT. La course ferme à
            // `VSM_DELAI` : un geste demandé au-delà ne se produirait pas, et
            // son absence se lirait comme un bouton sans effet.
            if (const char* differes = std::getenv("VSM_GESTE_APRES");
                differes != nullptr && *differes) {
                int delaiDeFermeture = 2000;
                if (const char* d = std::getenv("VSM_DELAI"); d != nullptr && *d)
                    delaiDeFermeture = juce::jmax(500, juce::String(d).getIntValue());
                const bool laCourseFerme = (std::getenv("VSM_CAPTURE") != nullptr)
                                           || passesDePeinture() > 0;
                // D354 : UN GESTE DIFFÉRÉ N'EST PAS VU PAR UN EXPORT DU DÉMARRAGE.
                // Les verbes d'export et d'enregistrement agissent tout de suite ;
                // ce qui est demandé « dans 1 500 ms » arrive après eux, et le
                // fichier écrit montre alors l'état d'AVANT le geste. Payé ici
                // même : une quantification demandée à 1 500 ms, un .mid exporté
                // identique, et c'est la QUANTIFICATION qu'on a soupçonnée — elle
                // était juste. C'est la leçon de D222, une seconde fois.
                for (const char* verbe : { "VSM_EXPORT", "VSM_EXPORT_MIDI",
                                            "VSM_EXPORT_MIDI_PISTE", "VSM_EXPORT_STEMS",
                                            "VSM_ENREGISTRER" })
                    if (std::getenv(verbe) != nullptr)
                        std::fputs((juce::String::fromUTF8("VSM_GESTE_APRES : ATTENTION \xe2\x80\x94 ") + verbe
                                    + juce::String::fromUTF8(" \xc3\xa9" "crit AU D\xc3\x89MARRAGE, "
                                        "donc AVANT ce geste : le fichier montrera l'\xc3\xa9tat d'avant. "
                                        "Passer par VSM_MENU_CONTEXTE ou VSM_GESTE_PISTE pour agir avant "
                                        "l'export, ou DIFF\xc3\x89RER l'export lui-m\xc3\xaame "
                                        "(VSM_GESTE_APRES=<ms>:exporter-midi:<fichier>, D356).\n")).toRawUTF8(), stderr);
                juce::StringArray suite;
                suite.addTokens(juce::String::fromUTF8(differes), ";", "");
                for (const auto& entree : suite) {
                    const juce::String texte = entree.trim();
                    if (texte.isEmpty()) continue;
                    const int ms = texte.upToFirstOccurrenceOf(":", false, false).getIntValue();
                    const juce::String geste = texte.fromFirstOccurrenceOf(":", false, false).trim();
                    if (ms <= 0 || geste.isEmpty()) {
                        std::fputs(("VSM_GESTE_APRES : \"" + texte.toStdString()
                                    + "\" illisible (<ms>:<geste>)\n").c_str(), stderr);
                        continue;
                    }
                    if (laCourseFerme && ms >= delaiDeFermeture) {
                        std::fputs(("VSM_GESTE_APRES : " + geste.toStdString() + " \u00e0 "
                                    + std::to_string(ms) + " ms \u2014 JAMAIS JOU\u00c9, la course "
                                      "ferme \u00e0 " + std::to_string(delaiDeFermeture)
                                    + " ms (VSM_DELAI)\n").c_str(), stderr);
                        continue;
                    }
                    auto* cible = content;
                    juce::Timer::callAfterDelay(ms, [cible, geste, ms] {
                        const bool fait = cible->runTrackGestureForCapture(geste);
                        std::fputs(("VSM_GESTE_APRES : " + geste.toStdString() + " \u00e0 "
                                    + std::to_string(ms) + " ms \u2014 "
                                    + (fait ? "jou\u00e9"
                                            : "refus\u00e9 (geste inconnu, ou sans effet : voir la "
                                              "ligne au-dessus)")
                                    + "\n").c_str(), stderr);
                    });
                }
            }
            // VSM_TOUCHE=« shift + M »[;…] : enfoncer des touches (D39.1).
            // Distinct de VSM_GESTE_PISTE, et c'est tout l'intérêt : elle
            // traverse `keyPressed` et la table des raccourcis, c'est-à-dire un
            // AUTRE chemin que le bouton. Deux instruments braqués au même
            // endroit ne valent pas mieux qu'un seul.
            //
            // APRÈS VSM_GESTE_PISTE, ET C'EST LA MÊME CORRECTION QU'À D38 :
            // placée avant, la touche agissait sur la piste active D'ALORS --
            // la dernière ajoutée par le menu -- et la sélection posée ensuite
            // effaçait la trace du désordre. La capture montrait une quatrième
            // tranche muette hors de la sélection, ce qui ressemblait trait
            // pour trait au défaut cherché. **L'ordre des variables d'un banc
            // fait partie du banc.**
            // D193 : VSM_FERMER=1 -- la VRAIE fermeture, celle de l'utilisateur,
            // AVEC LES AUTRES VERBES et non dans le rappel de la photo.
            //
            // Posée dans le rappel, la boîte était demandée et l'autoportrait
            // pris dans le MÊME message : la fenêtre modale n'était pas encore
            // posée, et `VSM_CAPTURE_PANNEAUX` ne la photographiait pas — la
            // course de D72, revue une fois de plus. Déclenchée ici, elle a tout
            // le délai de la capture pour exister, et la photo la montre. La
            // course se termine quand même : personne n'est là pour cliquer, et
            // le quit de la capture ne passe pas par ce chemin (D175).
            if (const char* fermer = std::getenv("VSM_FERMER");
                fermer != nullptr && *fermer && *fermer != '0')
                content->demanderAvantDeQuitter([] {});
            // D336 : VSM_NOTES=piste:tick:durée:hauteur[;…] -- écrire des notes par le
            // chemin du piano roll (le modèle, puis `onNotesEdited`), pour que le clip
            // implicite d'une piste où l'on ÉCRIT se vérifie sans souris : aucun autre
            // verbe n'écrivait une note.
            if (const char* notes = std::getenv("VSM_NOTES"); notes != nullptr && *notes) {
                juce::StringArray suite;
                suite.addTokens(juce::String::fromUTF8(notes), ";", "");
                for (const auto& n : suite)
                    if (n.trim().isNotEmpty() && !content->ecrireNotesPourCapture(n.trim()))
                        std::fputs("VSM_NOTES : note illisible (piste:tick:dur\u00e9e:hauteur)\n", stderr);
            }
            // VSM_POSITION=17.3 : la tête à une position saisie (D22.2), pour
            // que « Aller à la mesure » se vérifie sans souris : la barre de
            // transport doit dire la nouvelle position.
            //
            // D356 : REMONTÉE AVANT `VSM_TOUCHE` ET `VSM_MENU_CONTEXTE`. C'est un
            // verbe de SCÈNE, pas d'action : il dit OÙ est la tête, et plusieurs
            // gestes n'ont de sens que par rapport à elle (« Couper à la tête de
            // lecture », « Coller à la tête »). Posée entre les deux, elle servait
            // le menu et pas le clavier — deux portes du même geste rendaient alors
            // des fichiers différents pour une raison qui ne tenait pas au geste
            // mais à l'ordre des variables. C'est la correction de D38, une fois de
            // plus : l'ordre des variables d'un banc fait partie du banc.
            if (const char* position = std::getenv("VSM_POSITION"); position != nullptr && *position)
                if (!content->goToPositionForCapture(juce::String::fromUTF8(position)))
                    std::fputs(("VSM_POSITION : \u00ab " + std::string(position)
                                + " \u00bb refus\u00e9e, la t\u00eate n'a pas boug\u00e9\n").c_str(), stderr);
            if (const char* touches = std::getenv("VSM_TOUCHE"); touches != nullptr && *touches) {
                juce::StringArray suite;
                suite.addTokens(juce::String::fromUTF8(touches), ";", "");
                for (const auto& t : suite)
                    if (t.trim().isNotEmpty() && !content->runKeyForCapture(t.trim()))
                        std::fputs("VSM_TOUCHE : touche inconnue ou sans commande\n", stderr);
            }
            // VSM_BOITE_ESSAI=perdues;impossible;rien;latence;disque (D112) : les
            // boîtes de l'enregistrement, par les fonctions du chemin réel, avec des
            // chiffres fixes -- ni son émis, ni latence retenue, ni préférence écrite.
            // D114 : « indisponible », la boîte d'une reconstruction impossible, avec la
            // raison que la chaîne a VRAIMENT rendue d'après les préférences.
            if (const char* boites = std::getenv("VSM_BOITE_ESSAI"); boites != nullptr && *boites) {
                juce::StringArray suite;
                suite.addTokens(juce::String::fromUTF8(boites), ";", "");
                for (const auto& b : suite)
                    if (b.trim().isNotEmpty() && !content->showBoxForCapture(b.trim()))
                        std::fputs("VSM_BOITE_ESSAI : bo\u00eete inconnue\n", stderr);
            }
            // VSM_EXPORT=fichier.flac : exporter le projet ouvert sans fenêtre
            // (D20.5). Un export passe par un sélecteur de fichier et une
            // boîte de dialogue, qu'aucune capture ne traverse ; le fichier
            // écrit, lui, se relit -- c'est ainsi qu'on vérifie un format.
            if (const char* sortie = std::getenv("VSM_EXPORT"); sortie != nullptr && *sortie) {
                // VSM_EXPORT_NIVEAU=crete|lufs14|lufs23 (D21.5) : le niveau demandé.
                auto niveau = MainComponent::ExportLevel::AsIs;
                if (const char* n = std::getenv("VSM_EXPORT_NIVEAU"); n != nullptr && *n) {
                    const juce::String demande(n);
                    niveau = demande == "crete" ? MainComponent::ExportLevel::PeakMinus1
                           : demande == "lufs14" ? MainComponent::ExportLevel::Lufs14
                           : demande == "lufs23" ? MainComponent::ExportLevel::Lufs23
                                                 : MainComponent::ExportLevel::AsIs;
                }
                content->exportForCapture(juce::File::getCurrentWorkingDirectory().getChildFile(sortie), niveau);
            }
            // VSM_EXPORT_STEMS=dossier : l'export par stems sans fenêtre (D50).
            // Il vit derrière DEUX modales -- options puis sélecteur de
            // dossier --, donc son compte rendu était jusqu'ici invérifiable ;
            // c'est pourtant lui qui dit désormais la crête de chaque stem et
            // ce que le format demandé rabote.
            // VSM_EXPORT_STEMS_FORMAT=float32|int24|int16 (défaut int24, comme
            // le menu) ; VSM_EXPORT_STEMS_PAR=piste|groupe (défaut piste).
            if (const char* sortie = std::getenv("VSM_EXPORT_STEMS"); sortie != nullptr && *sortie) {
                auto format = vsm::audio::io::SampleFormat::Int24;
                if (const char* f = std::getenv("VSM_EXPORT_STEMS_FORMAT"); f != nullptr && *f) {
                    const juce::String demande(f);
                    format = demande == "float32" ? vsm::audio::io::SampleFormat::Float32
                           : demande == "int16"   ? vsm::audio::io::SampleFormat::Int16
                                                  : vsm::audio::io::SampleFormat::Int24;
                }
                auto granularite = vsm::interchange::StemGranularity::Tracks;
                if (const char* g = std::getenv("VSM_EXPORT_STEMS_PAR"); g != nullptr && *g)
                    if (juce::String(g) == "groupe")
                        granularite = vsm::interchange::StemGranularity::Groups;
                content->exportStemsForCapture(
                    juce::File::getCurrentWorkingDirectory().getChildFile(sortie), format, granularite);
            }
            // VSM_LECTURE=1 : lancer la lecture avant la capture (D22.4) --
            // le voyant OUT ne s'allume que si des notes partent.
            // VSM_LECTURE=1 lance tout de suite ; VSM_LECTURE=4000 lance après
            // 4 s -- le temps qu'un outil extérieur (aseqdump, D27.5) se branche.
            if (const char* lecture = std::getenv("VSM_LECTURE"); lecture != nullptr && *lecture && *lecture != '0') {
                const int delai = juce::String(lecture).getIntValue();
                if (delai > 1) juce::Timer::callAfterDelay(delai, [content] { content->startPlaybackForCapture(); });
                else content->startPlaybackForCapture();
            }
            // VSM_IMPORT_AUDIO=fichier.wav : sur une piste neuve (D24.5), par la
            // même fonction que le menu ; refusé et dit si le projet n'a pas de
            // dossier.
            if (const char* audio = std::getenv("VSM_IMPORT_AUDIO"); audio != nullptr && *audio) {
                juce::StringArray noms;
                noms.addTokens(juce::String::fromUTF8(audio), ";", "");
                juce::Array<juce::File> fichiers;
                for (const auto& nom : noms)
                    if (nom.trim().isNotEmpty())
                        fichiers.add(juce::File::getCurrentWorkingDirectory().getChildFile(nom.trim()));
                content->importAudioForCapture(fichiers);   // D213 : par la fonction du menu
            }
            // VSM_DEPOSER=fichier (D91) : un dépôt de fichier, par `filesDropped` --
            // la boîte « Que faire de ce fichier ? » n'avait aucun autre chemin.
            if (const char* depot = std::getenv("VSM_DEPOSER"); depot != nullptr && *depot)
                content->dropFileForCapture(juce::File::getCurrentWorkingDirectory().getChildFile(depot));
            // VSM_NAVIGATEUR=geste:référence;… (D99) : le double-clic et le dépôt du
            // navigateur, par les mêmes fonctions que la souris -- APRÈS VSM_MENU,
            // qui ajoute la piste visée.
            if (const char* nav = std::getenv("VSM_NAVIGATEUR"); nav != nullptr && *nav) {
                juce::StringArray liste;
                liste.addTokens(juce::String::fromUTF8(nav), ";", "");
                for (const auto& g : liste)
                    if (g.trim().isNotEmpty()) content->runBrowserGestureForCapture(g.trim());
            }
            // VSM_MENU_CONTEXTE=quel:libellé;… (D91) : les menus du clic droit, par la
            // même fonction que le clic -- APRÈS l'import audio, dont le clip est
            // celui que vise « clip-audio ».
            if (const char* ctx = std::getenv("VSM_MENU_CONTEXTE"); ctx != nullptr && *ctx) {
                juce::StringArray liste;
                liste.addTokens(juce::String::fromUTF8(ctx), ";", "");
                for (const auto& e : liste) content->runContextMenuForCapture(e.trim());
            }
            // VSM_CLIPS=1 (D262) : chaque clip avec son IDENTIFIANT -- APRÈS les
            // gestes, parce que c'est l'état qu'ils laissent qui se mesure. Un
            // identifiant 0 veut dire « jamais numéroté », et c'est ce que tout
            // clip né en séance portait avant D262.
            if (const char* clips = std::getenv("VSM_CLIPS"); clips != nullptr && *clips && *clips != '0')
                content->listClipsForCapture();
            // D338 : le rang du piano roll APRÈS les gestes (un « Zoom : tout voir »
            // de VSM_MENU_CONTEXTE le change), là où le relevé de géométrie du
            // panneau, pris à la disposition, ne le voit pas.
            if (const char* zones = std::getenv("VSM_PIANOROLL_ZONES"); zones != nullptr && *zones && *zones != '0')
                content->releverRangPianoRoll();
            // D362 : VSM_ARRANGEMENT=1 -- ce que la vue d'arrangement montre du
            // morceau, APRÈS les gestes (un « Zoom : tout voir » le change).
            if (const char* arr = std::getenv("VSM_ARRANGEMENT"); arr != nullptr && *arr && *arr != '0')
                content->releverFenetreArrangement();
            // D368 : VSM_AUTOSAUVEGARDE=1 -- forcer une sauvegarde automatique et
            // dire où elle a écrit. APRÈS les gestes de vue (le zoom doit être
            // celui qu'on veut voir emporté) et AVANT VSM_ENREGISTRER, qui est le
            // dernier à agir : c'est la leçon de D222, un banc qui écrit avant
            // d'agir publie l'état d'AVANT son geste.
            // Une valeur autre que « 1 » est un CHEMIN où recopier le fichier
            // écrit : le dossier de session disparaît à la fermeture normale, et
            // un banc qui le lirait ensuite ne trouverait rien.
            if (const char* aut = std::getenv("VSM_AUTOSAUVEGARDE"); aut != nullptr && *aut && *aut != '0') {
                const juce::String valeur = juce::String::fromUTF8(aut);
                content->forcerSauvegardeAutomatiquePourCapture(
                    valeur == "1" ? juce::File()
                                  : juce::File::getCurrentWorkingDirectory().getChildFile(valeur));
            }
            // D364 : VSM_TRANSPORT_ZONES=1 -- combien de rangées prend la barre.
            if (const char* tz = std::getenv("VSM_TRANSPORT_ZONES"); tz != nullptr && *tz && *tz != '0')
                content->releverRangeesTransport();
            // VSM_MENU_LISTE=1 (D80 ; déplacée par D83 après l'import audio, dont le clip doit y figurer) : la barre de menus entière, telle qu'elle
            // s'affiche, sur la sortie d'erreur -- APRÈS les gestes du banc, pour
            // que les libellés qui en dépendent (« Annuler : … ») soient ceux
            // de l'état photographié.
            if (const char* liste = std::getenv("VSM_MENU_LISTE"); liste != nullptr && *liste && *liste != '0')
                content->listMenusForCapture();
            // D352 : VSM_LISTE_AJOUTER=nature[:tick][;…] -- créer un événement depuis
            // la liste. AVANT `VSM_LISTE_EDITER` : on crée, puis on règle ce qu'on
            // vient de créer, et c'est l'ordre dans lequel un musicien le fait.
            if (const char* ajouts = std::getenv("VSM_LISTE_AJOUTER");
                ajouts != nullptr && *ajouts) {
                juce::StringArray suite;
                suite.addTokens(juce::String::fromUTF8(ajouts), ";", "");
                for (const auto& e : suite)
                    if (e.trim().isNotEmpty() && !content->addListEventForCapture(e.trim()))
                        std::fputs((juce::String("VSM_LISTE_AJOUTER : ") + e.trim()
                                    + juce::String::fromUTF8(u8" \u2014 refusé (voir la ligne au-dessus)\n"))
                                       .toRawUTF8(), stderr);
            }
            // D348 : VSM_LISTE_EDITER=ligne:colonne:valeur[;…] -- une case de la liste
            // d'événements modifiée par le chemin de la saisie. APRÈS VSM_VUE (la liste doit
            // être montée et remplie) et AVANT TOUT VERBE D'EXPORT — la leçon de
            // D222 : un banc qui exporte avant d'agir écrit l'état d'AVANT son geste,
            // et l'on croit que le geste n'a rien fait. Payé une fois ici même : les
            // trois saisies passaient au journal et le .mid relu ne bougeait pas.
            if (const char* saisies = std::getenv("VSM_LISTE_EDITER");
                saisies != nullptr && *saisies) {
                juce::StringArray suite;
                suite.addTokens(juce::String::fromUTF8(saisies), ";", "");
                for (const auto& e : suite)
                    if (e.trim().isNotEmpty() && !content->editListForCapture(e.trim()))
                        std::fputs((juce::String("VSM_LISTE_EDITER : ") + e.trim()
                                    + juce::String::fromUTF8(u8" \u2014 refusé (voir la ligne au-dessus)\n"))
                                       .toRawUTF8(), stderr);
            }
            // VSM_EXPORT_MIDI_PISTE=fichier.mid : la piste choisie seule, en
            // MIDI, sans fenêtre (D23.3) -- le fichier relu doit compter UNE piste.
            if (const char* sortie = std::getenv("VSM_EXPORT_MIDI_PISTE"); sortie != nullptr && *sortie)
                content->exportTrackMidiForCapture(juce::File::getCurrentWorkingDirectory().getChildFile(sortie));
            // VSM_EXPORT_MIDI=fichier.mid : LE PROJET ENTIER, par la même
            // fonction que « Fichier ▸ Exporter MIDI… » (D56.1). Sans lui, ce
            // que l'export ÉCRIT ne se relisait pas -- seulement ce qu'il
            // annonce, et c'est justement l'écart entre les deux que D56
            // corrige.
            if (const char* sortie = std::getenv("VSM_EXPORT_MIDI"); sortie != nullptr && *sortie)
                content->exportProjectMidiForCapture(
                    juce::File::getCurrentWorkingDirectory().getChildFile(sortie));
            // D222 : VSM_ENREGISTRER=dossier -- le projet écrit APRÈS tous les
            // gestes, par la fonction de « Enregistrer sous… ». `VSM_MENU` passe
            // avant `VSM_MENU_CONTEXTE` et avant `VSM_TOUCHE` : une course qui
            // enregistre par le menu écrit l'état d'AVANT ses gestes de clic
            // droit, et l'on croit que le geste n'a rien fait (payé une fois sur
            // le renommage d'un clip). Ce verbe-ci est le dernier à agir.
            if (const char* sortie = std::getenv("VSM_ENREGISTRER"); sortie != nullptr && *sortie)
                content->enregistrerSousPourCapture(
                    juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(sortie)));
            if (const char* rapport = std::getenv("VSM_RAPPORT");
                rapport != nullptr && *rapport)
                content->showReconstructionReport();
            // VSM_RAPPORT_LISTE=1 (D89) : le texte du volet de rapport, tel qu'affiché.
            // D92 : APRÈS VSM_RAPPORT. Placée avant, elle listait le rapport
            // d'ouverture que le rapport de reconstruction allait remplacer.
            if (const char* liste = std::getenv("VSM_RAPPORT_LISTE"); liste != nullptr && *liste && *liste != '0')
                content->listReportForCapture();
            // D344 : VSM_MIXEUR_NIVEAU=piste:dBFS[;…] -- des crêtes posées sur les
            // vumètres de la console, APRÈS les gestes (un geste peut changer les
            // tranches) et avant la photo.
            if (const char* niveaux = std::getenv("VSM_MIXEUR_NIVEAU");
                niveaux != nullptr && *niveaux)
                content->setMixerLevelsForCapture(juce::String::fromUTF8(niveaux));
            // D343 : VSM_CLIPS_MINI=1 -- la fenêtre de hauteurs des miniatures de
            // clip. Au démarrage (elle ne dépend que du projet, pas de la mise en
            // page) et APRÈS les gestes, comme VSM_CLIPS.
            if (const char* minis = std::getenv("VSM_CLIPS_MINI");
                minis != nullptr && *minis && *minis != '0')
                content->listClipMiniaturesForCapture();
            // VSM_TEXTES_LISTE=1 (D94) : les textes que la fenêtre MONTRE -- libellés,
            // boutons, listes, infobulles des composants visibles --, dans la langue
            // courante. Une infobulle ne se photographie pas : elle se liste.
            if (const char* textes = std::getenv("VSM_TEXTES_LISTE"); textes != nullptr && *textes && *textes != '0')
                content->listTextsForCapture();
            if (const char* sortie = std::getenv("VSM_CAPTURE"); sortie != nullptr && *sortie) {
                const juce::File fichier =
                    juce::File::getCurrentWorkingDirectory().getChildFile(sortie);
                // VSM_DELAI=ms : le délai avant l'autoportrait (2 s par
                // défaut). Une transcription (D20.4) met dix secondes à
                // charger Basic Pitch, et la capture doit l'attendre.
                int delai = 2000;
                if (const char* d = std::getenv("VSM_DELAI"); d != nullptr && *d)
                    delai = std::max(500, juce::String(d).getIntValue());
                juce::Timer::callAfterDelay(delai, [this, fichier, tailleDemandee] {
                    // D174 : LE TITRE DE LA FENÊTRE, AU MOMENT DE LA PHOTO. Il
                    // n'est le texte d'aucun composant -- c'est le gestionnaire
                    // de fenêtres qui le dessine --, donc ni l'autoportrait ni
                    // `VSM_TEXTES_LISTE` ne le voient. Or c'est lui qui porte,
                    // depuis D174, la marque « non enregistré » : sans cette
                    // ligne, cette marque serait invérifiable.
                    std::fputs(("VSM_TITRE : " + getName().toStdString() + "\n").c_str(), stderr);
                    if (auto* c = getContentComponent()) {
                        auto image = c->createComponentSnapshot(c->getLocalBounds());
                        // D58 : CE QU'ON A DEMANDÉ ET CE QU'ON A OBTENU, tous
                        // deux dits, et l'obtenu lu sur L'IMAGE -- la leçon de
                        // D49. La taille est bornée par l'écran DIVISÉ par
                        // l'échelle d'interface : à 150 % sur un écran de 1920,
                        // le plafond logique est 1280. Demander 1600 et
                        // recevoir 1280 sans un mot ferait écrire « vérifié à
                        // 1 600 px » sous une image de 1 280. Et le composant,
                        // lui, rend encore la taille DEMANDÉE à cet instant :
                        // seule l'image dit la vérité.
                        if (tailleDemandee.first > 0)
                            std::fputs(("VSM_TAILLE : " + std::to_string(tailleDemandee.first) + "x"
                                        + std::to_string(tailleDemandee.second) + " demand\u00e9, "
                                        + std::to_string(image.getWidth()) + "x"
                                        + std::to_string(image.getHeight()) + " obtenu"
                                        + ((image.getWidth() != tailleDemandee.first
                                            || image.getHeight() != tailleDemandee.second)
                                               ? " \u2014 BORN\u00c9 par l'\u00e9cran divis\u00e9 "
                                                 "par l'\u00e9chelle d'interface"
                                               : "")
                                        + "\n").c_str(), stderr);
                        fichier.deleteFile();
                        juce::FileOutputStream flux(fichier);
                        if (flux.openedOk())
                            juce::PNGImageFormat().writeImageToStream(image, flux);
                    }
                    // VSM_TEXTES_LISTE (D95) : les AUTRES fenêtres -- boîtes, panneaux
                    // flottants --, lues AU MOMENT DE LA PHOTO : une boîte ouverte
                    // par un geste du banc n'existe qu'après lui, et la liste du
                    // démarrage passe avant.
                    if (const char* textes = std::getenv("VSM_TEXTES_LISTE");
                        textes != nullptr && *textes && *textes != '0')
                        if (auto* principal = dynamic_cast<MainComponent*>(getContentComponent()))
                            principal->listWindowTextsForCapture();
                    // D344 : VSM_VUMETRES=1 -- ce que chaque vumètre montre, au moment
                    // de la photo : c'est là que la barre a sa hauteur définitive.
                    if (const char* vus = std::getenv("VSM_VUMETRES");
                        vus != nullptr && *vus && *vus != '0')
                        if (auto* principal = dynamic_cast<MainComponent*>(getContentComponent()))
                            principal->listMixerMetersForCapture();
                    // D345 : VSM_AUTOMATION=1 -- l'échelle de la lane d'automation, au
                    // moment de la photo : le paramètre affiché n'est choisi qu'après
                    // le chargement du projet et la mise en page des onglets.
                    if (const char* autom = std::getenv("VSM_AUTOMATION");
                        autom != nullptr && *autom && *autom != '0')
                        if (auto* principal = dynamic_cast<MainComponent*>(getContentComponent()))
                            principal->listAutomationScaleForCapture();
                    // D342 : VSM_MIXEUR=1 -- la géométrie des tranches de la console.
                    // ICI, dans le rappel de la photo, et non au démarrage : le dock du
                    // bas n'est disposé qu'après, et la course lue avant vaudrait zéro.
                    if (const char* console = std::getenv("VSM_MIXEUR");
                        console != nullptr && *console && *console != '0')
                        if (auto* principal = dynamic_cast<MainComponent*>(getContentComponent()))
                            principal->listMixerForCapture();
                    // D138 : VSM_SANS_SURFACE=1 -- les commandes « visibles » sans surface,
                    // au moment de la photo : la mise en page est faite, les fenêtres ouvertes.
                    if (const char* surface = std::getenv("VSM_SANS_SURFACE");
                        surface != nullptr && *surface && *surface != '0')
                        if (auto* principal = dynamic_cast<MainComponent*>(getContentComponent()))
                            principal->listerSansSurfacePourCapture();
                    // D55.2 : VSM_CAPTURE_PANNEAUX=1 photographie AUSSI chaque
                    // fenêtre flottante visible, une image par panneau.
                    //
                    // POURQUOI IL A FALLU L'AJOUTER. L'autoportrait ne prend que
                    // la fenêtre socle : un panneau flottant -- l'assemblage des
                    // prises, l'ordre de jeu, l'historique -- n'y figure pas, et
                    // la capture d'écran du système, elle, rend une fenêtre
                    // BLANCHE sous XWayland (le contenu JUCE n'est pas dans le
                    // pixmap que le compositeur donne). Sans cela, tout panneau
                    // flottant serait « invérifiable faute d'écran », ce que ce
                    // projet s'interdit de dire.
                    if (const char* pans = std::getenv("VSM_CAPTURE_PANNEAUX");
                        pans != nullptr && *pans && *pans != '0') {
                        for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i) {
                            auto* fenetre = juce::TopLevelWindow::getTopLevelWindow(i);
                            if (fenetre == nullptr || fenetre == this || !fenetre->isVisible())
                                continue;
                            juce::Component* contenu = fenetre;
                            if (auto* doc = dynamic_cast<juce::DocumentWindow*>(fenetre))
                                if (auto* c = doc->getContentComponent()) contenu = c;
                            if (contenu->getWidth() <= 0 || contenu->getHeight() <= 0) continue;
                            const juce::String titre =
                                fenetre->getName().retainCharacters(
                                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
                            // LE RANG DANS LE NOM : deux fenêtres peuvent porter le
                            // MÊME titre -- une boîte d'alerte reprend celui du
                            // panneau qui l'a ouverte --, et la seconde écrasait
                            // alors la première sans un mot.
                            const juce::File cible = fichier.getParentDirectory().getChildFile(
                                fichier.getFileNameWithoutExtension() + "-" + juce::String(i)
                                + "-" + titre + ".png");
                            cible.deleteFile();
                            juce::FileOutputStream f2(cible);
                            if (f2.openedOk())
                                juce::PNGImageFormat().writeImageToStream(
                                    contenu->createComponentSnapshot(contenu->getLocalBounds()), f2);
                            std::fputs(("VSM_CAPTURE_PANNEAUX : " + cible.getFullPathName().toStdString()
                                        + "\n").c_str(), stderr);
                        }
                    }
                    // D163 : VSM_PEINTURE=N -- le chronométrage des dessins, ICI
                    // et pas dans un bloc à part : le quit de l'autoportrait
                    // tombe à la même échéance, et un second minuteur posé
                    // après lui ne se déclencherait jamais.
                    if (const int passes = passesDePeinture(); passes > 0)
                        if (auto* c = getContentComponent())
                            chronometrerToutesLesPeintures(c, passes);
                    juce::JUCEApplication::getInstance()->quit();   // D175 : fin de course de BANC, pas un geste d'utilisateur
                });
            }
            // D163 : VSM_PEINTURE SANS autoportrait -- son propre minuteur, le
            // même délai, et c'est lui qui ferme l'application.
            else if (const int passes = passesDePeinture(); passes > 0) {
                int delai = 2000;
                if (const char* d = std::getenv("VSM_DELAI"); d != nullptr && *d)
                    delai = juce::jmax(500, juce::String(d).getIntValue());
                juce::Timer::callAfterDelay(delai, [this, passes] {
                    if (auto* c = getContentComponent())
                        chronometrerToutesLesPeintures(c, passes);
                    juce::JUCEApplication::getInstance()->quit();   // D175 : fin de course de BANC, pas un geste d'utilisateur
                });
            }
        }

        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->quit();   // D175 : fin de course de BANC, pas un geste d'utilisateur
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

#if VSM_WITH_CLAP || VSM_WITH_VST3

// --- D7.5 : le processus enfant du balayage ---------------------------------
//
// L'APPLICATION SE RELANCE ELLE-MÊME avec `--scan-plugin <fichier>`. L'enfant
// ouvre ce fichier, écrit une ligne par plugin trouvé, et sort. S'il tombe --
// et c'est ce qui arrive avec un plugin mal écrit -- il tombe SEUL.
//
// SE RELANCER SOI-MÊME plutôt que de livrer un exécutable de balayage à part :
// l'enfant doit charger EXACTEMENT le même code d'hôte que le parent, sinon le
// balayage validerait un chemin et la lecture en emprunterait un autre. Un
// second binaire aurait aussi à être trouvé, installé et tenu à jour.
//
// AVANT TOUT LE RESTE DE JUCE, et c'est essentiel : ouvrir une fenêtre pour
// balayer un fichier serait absurde, et sur une machine sans affichage cela
// échouerait avant même d'avoir commencé.
/// Annoncée avant `main`, définie après : c'est `main` qui l'inscrit dans
/// `JUCEApplicationBase::createInstance`.
juce::JUCEApplicationBase* juce_CreateApplication();

int main(int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (juce::String(argv[i]) != "--scan-plugin") continue;

        const juce::File fichier(juce::String::fromUTF8(argv[i + 1]));
        // UNE SEULE LIGNE PAR PLUGIN. Le parent ignore tout ce qui n'a pas la
        // bonne forme : un plugin qui écrit un message de licence pendant son
        // chargement ne doit pas entrer au catalogue.
        //
        // D113 : DANS LE FICHIER QUE LE PARENT DONNE (troisième argument), et
        // plus sur la sortie standard -- le parent attend avec son délai, puis
        // lit. Un fichier qui ne rend rien le DIT (la raison de l'hôte), et la
        // sentinelle vient en dernier : absente, l'enfant est mort en route.
        std::string erreur;
        const auto trouves = vsm::app::plugins::scanOneFileInThisProcess(fichier, erreur);
        std::string texte;
        for (const auto& plugin : trouves) texte += vsm::interchange::encodeScanLine(plugin) + "\n";
        if (trouves.empty() && !erreur.empty())
            texte += std::string(vsm::app::plugins::kLigneEchecBalayage) + erreur + "\n";
        texte += std::string(vsm::app::plugins::kLigneFinBalayage) + "\n";
        std::FILE* flux = (i + 2 < argc) ? std::fopen(argv[i + 2], "w") : stdout;
        if (flux == nullptr) return 2;
        std::fputs(texte.c_str(), flux);
        if (flux != stdout) std::fclose(flux);
        else std::fflush(stdout);
        return 0;
    }

    // CETTE LIGNE EST LA MOITIÉ OUBLIÉE DE `START_JUCE_APPLICATION`, ET SANS
    // ELLE L'APPLICATION NE DÉMARRE PAS DU TOUT.
    //
    // La macro fait DEUX choses : elle définit `juce_CreateApplication()`, et
    // elle inscrit ce pointeur dans `JUCEApplicationBase::createInstance` --
    // c'est par là, et uniquement par là, que JUCE sait quelle classe
    // instancier. Réécrire `main()` à la main (voir ci-dessus, D7.5) en n'ayant
    // gardé que la définition de la fonction laissait le pointeur NUL :
    // `JUCEApplicationBase::main()` appelait l'adresse zéro et le processus
    // mourait sur une faute de segmentation avant d'avoir ouvert une fenêtre.
    //
    // POURQUOI PERSONNE NE L'A VU : l'assertion de JUCE qui l'aurait dit
    // (`jassert (createInstance != nullptr)`) est compilée hors des builds
    // optimisés, et le reste du dépôt -- 1 206 tests, six suites -- ne passe
    // jamais par ce fichier. Une interface ne se teste pas sans écran, mais son
    // POINT D'ENTRÉE, si : la vérification tient dans un lancement.
    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    return juce::JUCEApplicationBase::main(argc, const_cast<const char**>(argv));
}

// `START_JUCE_APPLICATION` définirait un second `main`. On reprend donc à la
// main ce que la macro fait par ailleurs : définir la fabrique (ici) et
// l'inscrire dans `createInstance` (dans `main`, ci-dessus).
juce::JUCEApplicationBase* juce_CreateApplication() {
    return new VintageSynthMidiStudioApplication();
}

#else
START_JUCE_APPLICATION(VintageSynthMidiStudioApplication)
#endif
