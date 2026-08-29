// Rend la vue d'arrangement en PNG, SANS écran (étape D5.1 de
// docs/ROADMAP-daw.md).
//
// Pourquoi cet outil existe : la même raison que `vsm-pianoroll-preview` et
// `vsm-panel-preview`. Ce qu'on ne peut pas regarder, on ne peut pas le juger --
// et une vue d'arrangement se juge d'abord à l'œil : est-ce qu'un clip se
// distingue de son voisin, est-ce qu'on voit lequel est sélectionné, est-ce
// qu'un clip muet se remarque sans disparaître, est-ce que les mesures tombent
// où elles doivent.
//
//     ./vsm-arrangement-preview sortie.png [largeur] [hauteur]

#include "ui/ArrangementComponent.h"
#include "ui/LookAndFeel/VsmLookAndFeel.h"

#include <cstdio>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2) {
        std::fprintf(stderr, "Usage : vsm-arrangement-preview <sortie.png> [largeur] [hauteur]\n");
        return 1;
    }
    const juce::File sortie =
        juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[1]));
    const int largeur = argc >= 3 ? juce::jmax(600, std::atoi(argv[2])) : 1280;
    const int hauteur = argc >= 4 ? juce::jmax(200, std::atoi(argv[3])) : 340;

    // Un arrangement de démonstration qui porte les cas qu'on veut voir : des
    // clips de longueurs différentes, un clip muet, un clip audio, un groupe,
    // et deux clips qui se suivent bord à bord (le résultat d'une coupe).
    vsm::sequencer::Project projet;
    projet.ticksPerQuarterNote = 480;
    const vsm::midi::Tick mesure = 1920;

    struct Modele { const char* nom; uint32_t couleur; vsm::sequencer::Track::Kind genre; };
    const Modele modeles[] = {
        {"Basse", 0xffE3A24Du, vsm::sequencer::Track::Kind::Midi},
        {"Nappe", 0xff6B9BFFu, vsm::sequencer::Track::Kind::Midi},
        {"Voix", 0xff8ED081u, vsm::sequencer::Track::Kind::Audio},
        {"Batterie", 0xffD08BC8u, vsm::sequencer::Track::Kind::Group},
    };

    uint64_t idNote = 1;
    for (int p = 0; p < 4; ++p) {
        vsm::sequencer::Track piste;
        piste.name = modeles[p].nom;
        piste.colorRgba = modeles[p].couleur;
        piste.kind = modeles[p].genre;
        // Du matériau, pour que « jusqu'au bout du matériau » ait un sens.
        for (int n = 0; n < 32; ++n)
            piste.addNote(n * 240, n * 240 + 200, static_cast<uint8_t>(48 + n % 12), 100, 0, idNote);

        if (p == 0) {
            piste.clips.push_back({0, mesure * 2, 0, mesure * 2, false, "Intro", modeles[p].couleur});
            piste.clips.push_back({0, mesure, mesure * 2, mesure, false, "Couplet", modeles[p].couleur});
            // Bord à bord : le résultat d'une coupe, qui doit se lire comme deux
            // clips et non comme un seul.
            piste.clips.push_back({mesure, mesure, mesure * 3, mesure, false, "Couplet (2)",
                                    modeles[p].couleur});
        } else if (p == 1) {
            piste.clips.push_back({0, mesure * 3, mesure, mesure * 3, false, "Tenue",
                                    modeles[p].couleur});
            piste.clips.push_back({0, mesure, mesure * 5, mesure, true, "Muet", modeles[p].couleur});
        } else if (p == 3) {
            // Un clip ÉTIRÉ au-delà de son matériau : il répète sa fenêtre, et
            // l'aperçu sert à vérifier que cela SE VOIT -- dessiné comme un
            // simple rectangle plus long, il mentirait sur ce qu'il joue.
            vsm::sequencer::Clip boucle;
            boucle.startTick = 0;
            boucle.sourceLength = mesure;      // une mesure de fenêtre...
            boucle.length = mesure * 4;        // ...jouée quatre fois
            boucle.name = "Boucle x4";
            boucle.colorRgba = modeles[p].couleur;
            piste.clips.push_back(boucle);
        } else if (p == 2) {
            piste.audio.path = "audio/voix.wav";
            piste.audio.sampleRate = 48000.0;
            piste.audio.frames = 48000 * 12;
            piste.audio.channels = 2;
            piste.clips.push_back({0, 0, mesure * 2, mesure * 3, false, "Prise 3",
                                    modeles[p].couleur});
        }
        projet.tracks.push_back(std::move(piste));
    }
    projet.assignClipIds();

    // SEIZE PISTES, dont douze pliées : c'est le critère de l'étape (« l'écran
    // tient 16 pistes »), et l'aperçu sert à le VÉRIFIER plutôt qu'à
    // l'affirmer. Les quatre premières restent dépliées -- on travaille sur
    // quelques-unes à la fois et on replie le reste.
    for (int p = 4; p < 16; ++p) {
        vsm::sequencer::Track piste;
        piste.name = "Piste " + std::to_string(p + 1);
        piste.colorRgba = modeles[p % 4].couleur;
        piste.folded = true;
        piste.clips.push_back({0, mesure, static_cast<vsm::midi::Tick>(mesure * (p % 5)),
                                mesure, false, "", piste.colorRgba});
        projet.tracks.push_back(std::move(piste));
    }
    projet.assignClipIds();

    ArrangementComponent vue;
    vue.setProject(&projet);
    vue.setPlayheadTick(mesure * 2 + 480);
    vue.setBounds(0, 0, largeur, hauteur);

    juce::Image image(juce::Image::ARGB, largeur, hauteur, true);
    { juce::Graphics g(image); vue.paintEntireComponent(g, true); }

    juce::PNGImageFormat png;
    if (auto flux = std::unique_ptr<juce::FileOutputStream>(sortie.createOutputStream())) {
        flux->setPosition(0);
        flux->truncate();
        png.writeImageToStream(image, *flux);
    }
    std::printf("%s (%d x %d)\n", sortie.getFullPathName().toRawUTF8(), largeur, hauteur);
    return 0;
}
