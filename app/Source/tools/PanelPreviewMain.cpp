// Rend les façades de machines en PNG, SANS écran.
//
// Pourquoi cet outil existe : une façade qu'on ne regarde jamais est une
// façade qu'on ne peut pas juger. Les tests vérifient qu'aucune commande ne
// pointe dans le vide et que rien ne se chevauche -- ils ne diront jamais si
// le résultat est lisible et évoque bien l'instrument. Ce rendu hors écran
// permet de le vérifier à chaque changement, y compris sur une machine sans
// serveur graphique.
//
//   vsm-panel-preview <dossier-de-sortie> [largeur] [échelle]
//
// L'ÉCHELLE reproduit hors écran ce que fait le réglage « Taille de
// l'interface » du menu Affichage (voir ui/UiScale.h) : la façade garde
// exactement la même mise en page, rendue plus grand. Ce n'est PAS la même
// chose qu'augmenter la largeur -- une façade plus large recalcule ses cases
// et rebute ses légendes sur leur plafond de 11 points, alors que l'échelle
// grossit ces 11 points eux-mêmes. C'est précisément ce qu'il faut pouvoir
// comparer côte à côte quand on juge de la lisibilité.

#include <JuceHeader.h>
#include "../ui/machines/MachinePanelComponent.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/panels/MachinePanel.h"
#include "vsm/sequencer/StepPattern.h"
#include "vsm/sequencer/Track.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "vsm/interchange/NumberText.h"

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2) {
        std::fprintf(stderr,
                     "Usage : vsm-panel-preview <dossier-de-sortie> [largeur] [échelle]\n");
        return 1;
    }
    const juce::File outputFolder =
        juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[1]));
    outputFolder.createDirectory();
    const int width = argc >= 3 ? juce::jmax(320, std::atoi(argv[2])) : 1100;
    const double scale =
        argc >= 4 ? juce::jlimit(0.5, 4.0, vsm::interchange::numberFromTextOr(argv[3], 1.0)) : 1.0;

    vsm::audio::plugin::registerBuiltInPlugins();
    int rendered = 0;

    for (const std::string& pluginId : vsm::panels::machinePanelIds()) {
        const auto* panel = vsm::panels::findMachinePanel(pluginId);
        auto synth = vsm::audio::plugin::PluginRegistry::instance().create(pluginId);
        if (!panel || !synth) continue;
        synth->initialize(48000.0, 512);

        // Motif de démonstration : une façade à séquenceur vide ne montrerait
        // pas ce qu'on cherche justement à vérifier.
        vsm::sequencer::Track track;
        if (panel->sequencer.kind != vsm::panels::SequencerKind::None) {
            auto pattern = panel->sequencer.kind == vsm::panels::SequencerKind::DrumGrid
                ? vsm::sequencer::makeDrumPattern([&] {
                      std::vector<std::pair<std::string, uint8_t>> pieces;
                      for (const auto& [name, note] : panel->sequencer.lanes)
                          pieces.emplace_back(name, static_cast<uint8_t>(note));
                      return pieces;
                  }())
                : vsm::sequencer::makeMonoPattern(panel->sequencer.defaultNote);

            for (size_t lane = 0; lane < pattern.lanes.size(); ++lane) {
                for (int step = 0; step < pattern.stepCount; ++step) {
                    const bool on = (lane == 0 && step % 4 == 0) || (lane == 1 && step % 8 == 4) ||
                                    (lane == 3 && step % 2 == 0) || (lane == 0 && step == 14);
                    if (!on) continue;
                    auto& cell = pattern.lanes[lane].steps[static_cast<size_t>(step)];
                    cell.active = true;
                    cell.accent = (step % 8 == 0);
                    if (pattern.lanes.size() == 1) {
                        cell.noteNumber = static_cast<uint8_t>(36 + (step % 4) * 3);
                        cell.slide = (step % 8 == 4);
                    }
                }
            }
            uint64_t ids = 0;
            vsm::sequencer::writePatternToTrack(track, pattern, ids);
        }

        MachinePanelComponent component;
        component.setPanel(panel, synth.get());
        component.setTrack(&track);
        const int height = static_cast<int>(std::lround(static_cast<double>(width) / component.aspectRatio()));
        component.setBounds(0, 0, width, height);

        // L'image est plus grande d'un facteur `scale`, et le dessin y est
        // transformé d'autant : la façade n'apprend rien du changement, ses
        // cases et ses polices gardent leurs valeurs -- seul le rendu final
        // grossit. C'est exactement ce que fait l'échelle de l'application.
        const int imageWidth  = static_cast<int>(std::lround(width * scale));
        const int imageHeight = static_cast<int>(std::lround(height * scale));
        juce::Image image(juce::Image::ARGB, imageWidth, imageHeight, true);
        {
            juce::Graphics g(image);
            if (scale != 1.0)
                g.addTransform(juce::AffineTransform::scale(static_cast<float>(scale)));
            component.paintEntireComponent(g, true);
        }

        const juce::File file = outputFolder.getChildFile(juce::String(pluginId) + ".png");
        file.deleteFile();
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat png;
        if (stream.openedOk() && png.writeImageToStream(image, stream)) {
            std::printf("%s (%d x %d, échelle %.2f)\n",
                        file.getFullPathName().toRawUTF8(), imageWidth, imageHeight, scale);
            ++rendered;
        }
    }
    std::printf("%d façade(s) rendue(s)\n", rendered);
    return rendered > 0 ? 0 : 2;
}
