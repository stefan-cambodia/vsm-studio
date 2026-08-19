#include <JuceHeader.h>
#include "MainComponent.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"

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

START_JUCE_APPLICATION(VintageSynthMidiStudioApplication)
