#include <JuceHeader.h>
#include <cstdlib>
#include "MainComponent.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "ui/UiScale.h"
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
            setContentOwned(content, true); // la fenêtre s'ajuste à la taille du contenu (menu + transport)
            centreWithSize(content->getWidth(), content->getHeight());
            setResizable(true, true);
            // VSM_TAILLE=LARGEURxHAUTEUR (pixels logiques) : la taille de la
            // fenêtre pour un autoportrait. Sans elle, l'autoportrait prend
            // la taille mémorisée, et une disposition qui ne tient qu'à une
            // certaine largeur -- la barre de transport à 1 280 px -- ne se
            // vérifie pas.
            if (const char* taille = std::getenv("VSM_TAILLE"); taille != nullptr && *taille) {
                const juce::String t(taille);
                const int l = t.upToFirstOccurrenceOf("x", false, true).getIntValue();
                const int h = t.fromFirstOccurrenceOf("x", false, true).getIntValue();
                if (l > 200 && h > 100) centreWithSize(l, h);
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
            // VSM_MENU=libellé[;libellé…] : exécuter des entrées de menu par
            // leur LIBELLÉ avant la capture (D20). Trois gestes de cet audit
            // ne vivent que dans le menu contextuel d'un clip ; leurs jumeaux
            // du menu Édition s'atteignent ainsi sans souris, dans l'ordre
            // écrit (« Tout sélectionner;Répéter la sélection ... »).
            if (const char* entrees = std::getenv("VSM_MENU"); entrees != nullptr && *entrees) {
                juce::StringArray liste;
                liste.addTokens(juce::String::fromUTF8(entrees), ";", "");
                for (const auto& e : liste) content->runMenuEntryForCapture(e.trim());
            }
            // VSM_EXPORT=fichier.flac : exporter le projet ouvert sans fenêtre
            // (D20.5). Un export passe par un sélecteur de fichier et une
            // boîte de dialogue, qu'aucune capture ne traverse ; le fichier
            // écrit, lui, se relit -- c'est ainsi qu'on vérifie un format.
            if (const char* sortie = std::getenv("VSM_EXPORT"); sortie != nullptr && *sortie)
                content->exportForCapture(juce::File::getCurrentWorkingDirectory().getChildFile(sortie));
            if (const char* rapport = std::getenv("VSM_RAPPORT");
                rapport != nullptr && *rapport)
                content->showReconstructionReport();
            if (const char* sortie = std::getenv("VSM_CAPTURE"); sortie != nullptr && *sortie) {
                const juce::File fichier =
                    juce::File::getCurrentWorkingDirectory().getChildFile(sortie);
                // VSM_DELAI=ms : le délai avant l'autoportrait (2 s par
                // défaut). Une transcription (D20.4) met dix secondes à
                // charger Basic Pitch, et la capture doit l'attendre.
                int delai = 2000;
                if (const char* d = std::getenv("VSM_DELAI"); d != nullptr && *d)
                    delai = std::max(500, juce::String(d).getIntValue());
                juce::Timer::callAfterDelay(delai, [this, fichier] {
                    if (auto* c = getContentComponent()) {
                        auto image = c->createComponentSnapshot(c->getLocalBounds());
                        fichier.deleteFile();
                        juce::FileOutputStream flux(fichier);
                        if (flux.openedOk())
                            juce::PNGImageFormat().writeImageToStream(image, flux);
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
        // UNE SEULE LIGNE PAR PLUGIN, SUR LA SORTIE STANDARD. Le parent ignore
        // tout ce qui n'a pas la bonne forme : un plugin qui écrit un message
        // de licence pendant son chargement ne doit pas entrer au catalogue.
        for (const auto& plugin : vsm::app::plugins::scanOneFileInThisProcess(fichier))
            std::printf("%s\n", vsm::interchange::encodeScanLine(plugin).c_str());
        std::fflush(stdout);
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
