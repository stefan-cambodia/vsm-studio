#include <JuceHeader.h>
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
            setVisible(true);

            // Seulement maintenant : la fenêtre socle a une position
            // d'écran réelle, les fenêtres flottantes peuvent se
            // positionner par rapport à elle (voir MainComponent.h).
            content->showFloatingPanels();
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
    return juce::JUCEApplicationBase::main(argc, const_cast<const char**>(argv));
}

// `START_JUCE_APPLICATION` définirait un second `main`. On garde donc seulement
// ce que la macro fait d'autre : désigner la classe d'application.
juce::JUCEApplicationBase* juce_CreateApplication() {
    return new VintageSynthMidiStudioApplication();
}

#else
START_JUCE_APPLICATION(VintageSynthMidiStudioApplication)
#endif
