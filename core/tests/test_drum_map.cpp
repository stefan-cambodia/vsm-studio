#include "TestFramework.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <utility>
#include <vector>

using namespace vsm::midi;
using namespace vsm::sequencer;

// D609.1 — LA CARTE DE BATTERIE, sa note de sortie. L'attendu écrit avant le code (ROADMAP-daw, D609, (a)) : une piste
// qui écrit 41 et 43 avec la carte 41 → 45, 43 → 47 planifie 45 et 47 aux mêmes instants, NoteOff compris ; la chasse
// relance 45 ; sans carte, 41 et 43 ; une transposition de +2 s'ajoute APRÈS la carte.

namespace {
Project desToms() {
    Project project;
    project.ticksPerQuarterNote = 480;
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 240, 41, 100, 9, ids);
    piste.addNote(480, 720, 43, 100, 9, ids);
    project.tracks.push_back(piste);
    return project;
}

/// (instant en millisecondes, hauteur, NoteOn ?) de chaque note planifiée, dans l'ordre du temps.
std::vector<std::tuple<int, int, bool>> planifie(const Project& project) {
    std::vector<std::tuple<int, int, bool>> r;
    for (const auto& e : PlaybackScheduler::build(project, 0, 1000000)) {
        const int ms = static_cast<int>(std::lround(e.timeSeconds * 1000.0));
        if (const auto* on = std::get_if<NoteOnEvent>(&e.data)) r.emplace_back(ms, on->note, true);
        else if (const auto* off = std::get_if<NoteOffEvent>(&e.data)) r.emplace_back(ms, off->note, false);
    }
    std::sort(r.begin(), r.end());
    return r;
}
} // namespace

VSM_TEST(the_drum_map_plays_the_output_note_of_each_written_note_d609) {
    Project temoin = desToms();
    Project carte = desToms();
    carte.tracks[0].drumMap = {{41, 45}, {43, 47}};
    const auto t = planifie(temoin);
    const auto c = planifie(carte);
    VSM_ASSERT_EQ(t.size(), size_t(4));
    VSM_ASSERT_EQ(c.size(), size_t(4));
    // LES MÊMES INSTANTS, d'autres hauteurs : 0 ms 41→45, 125 ms (fin) 41→45, 250 ms 43→47, 375 ms 43→47.
    for (size_t i = 0; i < t.size(); ++i) {
        VSM_ASSERT_EQ(std::get<0>(c[i]), std::get<0>(t[i]));
        VSM_ASSERT_EQ(std::get<2>(c[i]), std::get<2>(t[i]));
        const int ecrite = std::get<1>(t[i]);
        VSM_ASSERT(ecrite == 41 || ecrite == 43);
        VSM_ASSERT_EQ(std::get<1>(c[i]), ecrite == 41 ? 45 : 47);
    }
    // LA CHASSE : lancée au milieu du premier tom, elle relance 45, pas 41.
    bool relance45 = false, relance41 = false;
    for (const auto& e : PlaybackScheduler::chaseNotesAt(carte, 120))
        if (const auto* on = std::get_if<NoteOnEvent>(&e.data)) {
            relance45 = relance45 || on->note == 45;
            relance41 = relance41 || on->note == 41;
        }
    VSM_ASSERT(relance45);
    VSM_ASSERT(!relance41);
    // LA TRANSPOSITION APRÈS LA CARTE : 41 → 45 → 47.
    carte.tracks[0].transposeSemitones = 2;
    VSM_ASSERT_EQ(std::get<1>(planifie(carte)[0]), 47);
    // UNE NOTE QUE LA CARTE NE NOMME PAS se joue elle-même.
    Track& piste = carte.tracks[0];
    piste.transposeSemitones = 0;
    uint64_t ids = 10;
    piste.addNote(960, 1200, 36, 100, 9, ids);
    bool kick = false;
    for (const auto& n : planifie(carte)) kick = kick || std::get<1>(n) == 36;
    VSM_ASSERT(kick);
}
