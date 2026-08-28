// Rend le piano roll en PNG, SANS écran.
//
// Pourquoi cet outil existe : le marquage des notes douteuses (étape 11.3) est
// une affaire de LISIBILITÉ. Un test peut vérifier qu'une confiance a bien été
// reportée sur la bonne note ; il ne dira jamais si le marqueur se voit, s'il
// se distingue de la sélection, ni s'il reste visible sur une note très
// courte. C'est le même raisonnement que pour les façades de machines, et il a
// déjà attrapé de vrais défauts là-bas.
//
//   vsm-pianoroll-preview <fichier-de-sortie.png> [largeur] [hauteur] [douteuse]
//
// Avec « douteuse », la première note douteuse est sélectionnée avant le rendu
// (la touche D dans l'application) : c'est le cas où liseré de sélection et
// marqueur de doute se superposent, et il faut VOIR que l'un ne masque pas
// l'autre.

#include <JuceHeader.h>
#include "../ui/PianoRollComponent.h"
#include "vsm/sequencer/Project.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2) {
        std::fprintf(stderr, "Usage : vsm-pianoroll-preview <sortie.png> [largeur] [hauteur] [douteuse]\n");
        return 1;
    }
    const juce::File output =
        juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[1]));
    const int width = argc >= 3 ? juce::jmax(400, std::atoi(argv[2])) : 1200;
    const int height = argc >= 4 ? juce::jmax(300, std::atoi(argv[3])) : 520;

    // Motif de démonstration : des notes de confiances variées, dont deux très
    // courtes, pour vérifier que le marqueur tient aussi sur une croche.
    vsm::sequencer::Project project;
    vsm::sequencer::Track track;
    track.name = "Reconstruction";
    track.colorRgba = 0xff6B9BFFu;
    const float confiances[] = {1.0f, 0.31f, 0.92f, 0.12f, 0.78f, 0.44f, 1.0f, 0.05f};
    uint64_t identifiant = 1;
    for (int i = 0; i < 8; ++i) {
        vsm::sequencer::Note note;
        note.startTick = i * 480;
        // Deux notes volontairement très brèves (indices 3 et 7) : c'est là
        // qu'un liseré seul se confondrait avec la sélection.
        note.endTick = note.startTick + ((i == 3 || i == 7) ? 60 : 400);
        note.number = static_cast<uint8_t>(64 + (i * 3) % 13);
        note.velocity = static_cast<uint8_t>(60 + i * 8);
        note.id = identifiant++;
        note.confidence = confiances[i];
        track.notes.push_back(note);
    }
    // Une note muette, pour vérifier que les deux marquages ne se confondent
    // pas : hachures pour muette, liseré ambre et coin replié pour douteuse.
    vsm::sequencer::Note muette;
    muette.startTick = 8 * 480;
    muette.endTick = muette.startTick + 400;
    muette.number = 71;
    muette.velocity = 100;
    muette.id = identifiant++;
    muette.muted = true;
    track.notes.push_back(muette);

    project.tracks.push_back(track);

    PianoRollComponent pianoRoll;
    pianoRoll.setProject(&project);
    pianoRoll.setActiveTrackIndex(0);
    pianoRoll.setSize(width, height);
    if (argc >= 5 && juce::String(argv[4]) == "douteuse") pianoRoll.selectNextDoubtfulNote(true);

    juce::Image image(juce::Image::ARGB, width, height, true);
    { juce::Graphics g(image); pianoRoll.paintEntireComponent(g, true); }

    juce::PNGImageFormat png;
    if (auto stream = std::unique_ptr<juce::FileOutputStream>(output.createOutputStream())) {
        stream->setPosition(0);
        stream->truncate();
        png.writeImageToStream(image, *stream);
    }
    std::printf("%s (%d x %d)\n", output.getFullPathName().toRawUTF8(), width, height);
    return 0;
}
