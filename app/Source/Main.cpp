#include <JuceHeader.h>
#include <cstdlib>
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

    void systemRequestedQuit() override { quit(); }
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
            if (const char* fichier = std::getenv("VSM_FICHIER"); fichier != nullptr && *fichier)
                content->setPluginFileForCapture(juce::File::getCurrentWorkingDirectory().getChildFile(
                    juce::String::fromUTF8(fichier)));
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
                        std::fputs("VSM_GESTE_PISTE : geste inconnu\n", stderr);
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
            // VSM_POSITION=17.3 : la tête à une position saisie (D22.2), pour
            // que « Aller à la mesure » se vérifie sans souris : la barre de
            // transport doit dire la nouvelle position.
            if (const char* position = std::getenv("VSM_POSITION"); position != nullptr && *position)
                if (!content->goToPositionForCapture(juce::String::fromUTF8(position)))
                    std::fputs(("VSM_POSITION : \u00ab " + std::string(position)
                                + " \u00bb refus\u00e9e, la t\u00eate n'a pas boug\u00e9\n").c_str(), stderr);
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
            if (const char* audio = std::getenv("VSM_IMPORT_AUDIO"); audio != nullptr && *audio)
                content->importAudioForCapture(juce::File::getCurrentWorkingDirectory().getChildFile(audio));
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
            // VSM_MENU_LISTE=1 (D80 ; déplacée par D83 après l'import audio, dont le clip doit y figurer) : la barre de menus entière, telle qu'elle
            // s'affiche, sur la sortie d'erreur -- APRÈS les gestes du banc, pour
            // que les libellés qui en dépendent (« Annuler : … ») soient ceux
            // de l'état photographié.
            if (const char* liste = std::getenv("VSM_MENU_LISTE"); liste != nullptr && *liste && *liste != '0')
                content->listMenusForCapture();
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
            if (const char* rapport = std::getenv("VSM_RAPPORT");
                rapport != nullptr && *rapport)
                content->showReconstructionReport();
            // VSM_RAPPORT_LISTE=1 (D89) : le texte du volet de rapport, tel qu'affiché.
            // D92 : APRÈS VSM_RAPPORT. Placée avant, elle listait le rapport
            // d'ouverture que le rapport de reconstruction allait remplacer.
            if (const char* liste = std::getenv("VSM_RAPPORT_LISTE"); liste != nullptr && *liste && *liste != '0')
                content->listReportForCapture();
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
                    juce::JUCEApplication::getInstance()->systemRequestedQuit();
                });
            }
        }

        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
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
