#include "TestFramework.h"
#include "vsm/sequencer/DrumKit.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cstdio>
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

// D610.1 — ADAPTER UNE PARTIE GENERAL MIDI À LA MACHINE. L'attendu écrit avant le code (ROADMAP-daw, D610, (a)) : sur
// les 35 pièces de la table General MIDI, celles qui sonnent DANS LEUR CATÉGORIE sans puis avec l'adaptation, et celles
// qui restent sans équivalent — 808 : 6 → 9 (26) ; 909 : 9 → 18 (17) ; batterie : 10 → 20 (15) ; FM Drums : 6 → 14
// (21) ; percussions : 13 → 13 (22). Et sur la 909 : 41 → 45, 43 → 45, 48 → 47, 44 → 42, 57 → 49, 36 inchangée.
VSM_TEST(adapting_general_midi_to_each_machine_by_category_and_nearest_pitch_d610) {
    std::vector<uint8_t> pieces;
    for (int n = 0; n < 128; ++n)
        if (generalMidiCategory(static_cast<uint8_t>(n)) != DrumCategory::None) pieces.push_back(static_cast<uint8_t>(n));
    VSM_ASSERT_EQ(pieces.size(), size_t(35));
    struct Attendu { const char* machine; size_t avant, apres, sans; };
    for (const Attendu& a : {Attendu{"vsm.tr808", 6, 9, 26}, Attendu{"vsm.tr909", 9, 18, 17},
                             Attendu{"vsm.drums", 10, 20, 15}, Attendu{"vsm.fmdrums", 6, 14, 21},
                             Attendu{"vsm.perc", 13, 13, 22}}) {
        const auto adaptation = adaptGeneralMidi(a.machine, pieces);
        size_t avant = 0, apres = 0;
        for (uint8_t g : pieces) {
            avant += soundsInCategory(a.machine, g, g) ? 1 : 0;
            const auto ici = adaptation.entries.find(g);
            apres += soundsInCategory(a.machine, g, ici == adaptation.entries.end() ? g : ici->second) ? 1 : 0;
        }
        std::printf("      [D610.1] %-12s %2zu → %2zu, %2zu sans équivalent (%zu entrée(s))\n", a.machine, avant, apres,
                    adaptation.sansEquivalent.size(), adaptation.entries.size());
        VSM_ASSERT_EQ(avant, a.avant);
        VSM_ASSERT_EQ(apres, a.apres);
        VSM_ASSERT_EQ(adaptation.sansEquivalent.size(), a.sans);
    }
    const auto la909 = adaptGeneralMidi("vsm.tr909", {36, 41, 43, 44, 48, 51, 57});
    VSM_ASSERT_EQ(la909.entries.count(36), size_t(0));
    VSM_ASSERT_EQ(la909.entries.at(41), uint8_t(45));
    VSM_ASSERT_EQ(la909.entries.at(43), uint8_t(45));
    VSM_ASSERT_EQ(la909.entries.at(48), uint8_t(47));
    VSM_ASSERT_EQ(la909.entries.at(44), uint8_t(42));
    VSM_ASSERT_EQ(la909.entries.at(57), uint8_t(49));
    VSM_ASSERT_EQ(la909.sansEquivalent.size(), size_t(1));   // le ride 51 : la 909 n'en a pas
    VSM_ASSERT_EQ(la909.sansEquivalent[0], uint8_t(51));
}

// D610 — « ÉCLATER PAR HAUTEUR » GARDE CE QUE LA LECTURE FAIT DE LA NOTE. L'attendu écrit avant le code (ROADMAP-daw,
// D610, (e)) : une piste à carte {41 → 45} transposée de +2, éclatée — la piste de la pièce 41 joue 47 (41 → 45 → +2) ;
// le code d'avant jouait 41.
VSM_TEST(exploding_by_pitch_keeps_each_piece_s_drum_map_entry_and_transposition_d610) {
    Project project;
    project.ticksPerQuarterNote = 480;
    Track piste;
    uint64_t ids = 1;
    piste.addNote(0, 240, 36, 100, 9, ids);
    piste.addNote(480, 720, 41, 100, 9, ids);
    piste.drumMap = {{41, 45}};
    piste.transposeSemitones = 2;
    project.tracks.push_back(piste);
    VSM_ASSERT_EQ(explodeTrackByPitch(project, 0), size_t(1));
    VSM_ASSERT_EQ(project.tracks.size(), size_t(2));
    int joue41 = -1;
    for (const auto& e : PlaybackScheduler::build(project, 0, 1000000))
        if (const auto* on = std::get_if<NoteOnEvent>(&e.data); on != nullptr && e.trackIndex == 1) joue41 = on->note;
    std::printf("      [D610] la pièce 41 éclatée joue %d\n", joue41);
    VSM_ASSERT_EQ(joue41, 47);
    VSM_ASSERT_EQ(project.tracks[1].drumMap.size(), size_t(1));   // l'entrée qui la concerne, pas les autres
}
