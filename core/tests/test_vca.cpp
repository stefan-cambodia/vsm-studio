#include <set>
#include "TestFramework.h"
#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/Track.h"
#include <string>
#include <vector>

using namespace vsm::midi;
using namespace vsm::sequencer;

// D532.1 de docs/ROADMAP-daw.md — LES FADERS VCA, le modèle.
//
// Un VCA ne joue rien : son fader MULTIPLIE celui des pistes qui le désignent. Ce
// fichier garde ce que le modèle en dit — qui désigne quoi, ce qui s'entend, et que la
// référence, un INDEX, suit les pistes partout où elles bougent.

namespace {
Project projetVca() {
    Project p;
    p.tracks.resize(3);
    p.tracks[0].name = "A";
    p.tracks[0].vcaTrack = 2;
    p.tracks[1].name = "B";            // pas membre
    p.tracks[2].name = "V";
    p.tracks[2].kind = Track::Kind::Vca;
    return p;
}
}

VSM_TEST(vca_of_designates_only_a_vca_and_never_from_a_vca) {
    Project p = projetVca();
    VSM_ASSERT(vcaOf(p.tracks, 0) == &p.tracks[2]);
    VSM_ASSERT(vcaOf(p.tracks, 1) == nullptr);              // pas membre
    p.tracks[1].vcaTrack = 0;                               // vers une piste qui n'est pas un VCA
    VSM_ASSERT(vcaOf(p.tracks, 1) == nullptr);
    p.tracks[1].vcaTrack = 9;                               // hors bornes
    VSM_ASSERT(vcaOf(p.tracks, 1) == nullptr);
    p.tracks.push_back(Track{});
    p.tracks[3].kind = Track::Kind::Vca;
    p.tracks[3].vcaTrack = 2;                               // un VCA dans un VCA : refusé
    VSM_ASSERT(vcaOf(p.tracks, 3) == nullptr);
    p.tracks[2].vcaTrack = 2;                               // et lui-même
    VSM_ASSERT(vcaOf(p.tracks, 2) == nullptr);
}

VSM_TEST(a_muted_or_soloed_vca_reaches_its_members_and_only_them) {
    Project p = projetVca();
    VSM_ASSERT(trackAudible(p.tracks, 0, false));
    p.tracks[2].muted = true;
    VSM_ASSERT(!trackAudible(p.tracks, 0, false));          // le membre se tait
    VSM_ASSERT(trackAudible(p.tracks, 1, false));           // l'autre non
    p.tracks[2].muted = false;
    p.tracks[2].solo = true;
    const bool solo = anySoloActive(p.tracks);
    VSM_ASSERT(solo);
    VSM_ASSERT(trackAudible(p.tracks, 0, solo));            // le membre est sous le solo
    VSM_ASSERT(!trackAudible(p.tracks, 1, solo));           // l'autre se tait
    p.tracks[2].solo = false;
    p.tracks[2].disabled = true;
    VSM_ASSERT(!trackAudible(p.tracks, 0, false));
}

VSM_TEST(the_vca_reference_follows_moves_duplicates_and_removals) {
    Project p = projetVca();
    moveTrack(p, 2, 0);                                     // V en tête : [V, A, B]
    VSM_ASSERT(p.tracks[1].name == "A");
    VSM_ASSERT_EQ(p.tracks[1].vcaTrack, 0);
    duplicateTrack(p, 0);                                   // [V, V (copie), A, B]
    VSM_ASSERT(p.tracks[2].name == "A");
    VSM_ASSERT_EQ(p.tracks[2].vcaTrack, 0);                 // le VCA d'origine n'a pas bougé
    removeTrack(p, 0);                                      // le VCA supprimé
    VSM_ASSERT(p.tracks[1].name == "A");
    VSM_ASSERT_EQ(p.tracks[1].vcaTrack, -1);                // ne commande plus personne
}

VSM_TEST(exploded_pieces_keep_the_group_and_the_vca_placed_after_them) {
    // UNE BATTERIE ÉCLATÉE DONT LE BUS ET LE VCA SONT PLACÉS APRÈS ELLE — l'ordre des
    // reconstructions. Les pièces copiaient l'index du groupe AVANT que l'insertion ne
    // le décale : elles visaient la piste d'avant.
    Project p;
    p.tracks.resize(3);
    p.tracks[0].name = "Batterie";
    p.tracks[0].instrumentId = "vsm.tr808";
    p.tracks[0].outputGroup = 1;
    p.tracks[0].vcaTrack = 2;
    p.tracks[1].name = "Bus";
    p.tracks[1].kind = Track::Kind::Group;
    p.tracks[2].name = "V";
    p.tracks[2].kind = Track::Kind::Vca;
    uint64_t compteur = 1;
    const int hauteurs[3] = {36, 38, 42};
    for (int i = 0; i < 3; ++i)
        p.tracks[0].addNote(static_cast<Tick>(i * 120), static_cast<Tick>(i * 120 + 60),
                             static_cast<uint8_t>(hauteurs[i]), 100, 0, compteur);
    VSM_ASSERT_EQ(explodeTrackByPitch(p, 0), size_t(2));   // [Batterie, 38, 42, Bus, V]
    VSM_ASSERT(p.tracks[3].name == "Bus");
    VSM_ASSERT(p.tracks[4].name == "V");
    for (size_t t = 0; t < 3; ++t) {
        VSM_ASSERT_EQ(p.tracks[t].outputGroup, 3);
        VSM_ASSERT_EQ(p.tracks[t].vcaTrack, 4);
    }
}
