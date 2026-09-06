#include "TestFramework.h"
#include "vsm/sequencer/MidiEffects.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>

using namespace vsm::midi;
using namespace vsm::sequencer;

// D31 de docs/ROADMAP-daw.md — LES EFFETS MIDI DE PISTE.
//
// L'attendu a été écrit AVANT ces mesures, dans la feuille de route : un
// accord de trois notes tenu une RONDE, arpégé au pas de la DOUBLE-CROCHE,
// doit donner SEIZE attaques et non trois ; le matériau doit rester intact ;
// et une piste sans chaîne doit donner un planning identique événement pour
// événement à celui d'avant la phase.

namespace {

/// Un accord de trois notes, tenu une ronde, à 480 ticks la noire.
std::vector<Note> unAccord(Tick ppq = 480) {
    std::vector<Note> notes;
    uint64_t id = 1;
    for (uint8_t n : {60, 64, 67}) notes.push_back({0, ppq * 4, 0, n, 100, 64, id++});
    return notes;
}

MidiEffect fait(const std::string& type, std::map<std::string, float> p) {
    MidiEffect e; e.type = type; e.parameters = std::move(p); return e;
}

} // namespace

VSM_TEST(an_arpeggio_turns_a_three_note_chord_into_sixteen_attacks) {
    // LE CHIFFRE DE LA PHASE. Une ronde vaut quatre noires, donc seize
    // doubles-croches : c'est ce compte qui dit que le découpage de l'accord
    // ET le pas sont justes. Trois attaques diraient que rien n'a eu lieu ;
    // quatre, que le pas est la noire.
    const std::vector<Note> materiau = unAccord();
    const std::vector<MidiEffect> chaine { fait("arpeggio", {{"Division", 4.0f}, {"Mode", 0.0f}}) };
    const std::vector<Note> joue = applyMidiEffects(chaine, materiau, 480);

    VSM_ASSERT_EQ(joue.size(), size_t{16});
    // L'ARPÈGE REMPLIT EXACTEMENT LA PLACE DE L'ACCORD : ni plus (il mordrait
    // sur ce qui suit), ni moins (il laisserait un trou).
    VSM_ASSERT_EQ(joue.front().startTick, Tick{0});
    VSM_ASSERT_EQ(joue.back().endTick, Tick{480 * 4});
    // Montant : do, mi, sol, do, mi, sol...
    VSM_ASSERT_EQ(joue[0].number, uint8_t{60});
    VSM_ASSERT_EQ(joue[1].number, uint8_t{64});
    VSM_ASSERT_EQ(joue[2].number, uint8_t{67});
    VSM_ASSERT_EQ(joue[3].number, uint8_t{60});
    // ET LE MATÉRIAU N'A PAS BOUGÉ : c'est ce qui sépare un effet d'une édition.
    VSM_ASSERT_EQ(materiau.size(), size_t{3});
    VSM_ASSERT_EQ(materiau[0].endTick, Tick{480 * 4});
}

VSM_TEST(the_arpeggio_directions_are_what_they_say) {
    const std::vector<Note> materiau = unAccord();
    auto hauteurs = [&](int mode) {
        const std::vector<Note> j = applyMidiEffects(
            {fait("arpeggio", {{"Division", 4.0f}, {"Mode", static_cast<float>(mode)}})},
            materiau, 480);
        std::vector<int> h;
        for (size_t i = 0; i < 4 && i < j.size(); ++i) h.push_back(j[i].number);
        return h;
    };
    VSM_ASSERT(hauteurs(0) == (std::vector<int>{60, 64, 67, 60}));   // montant
    VSM_ASSERT(hauteurs(1) == (std::vector<int>{67, 64, 60, 67}));   // descendant
    // ALLER-RETOUR SANS REDOUBLER LES EXTRÊMES : do, mi, sol, mi, puis do.
    // Redoubler le sol marquerait un temps, ce qui n'est pas ce qu'on entend
    // d'un arpégiateur.
    VSM_ASSERT(hauteurs(2) == (std::vector<int>{60, 64, 67, 64}));
}

VSM_TEST(only_notes_that_start_together_are_a_chord) {
    // Deux notes dont l'une commence au milieu de l'autre ne forment pas un
    // accord mais une tenue : les arpéger déplacerait la seconde.
    std::vector<Note> notes;
    uint64_t id = 1;
    notes.push_back({0, 480, 0, 60, 100, 64, id++});
    notes.push_back({240, 720, 0, 64, 100, 64, id++});
    const std::vector<Note> joue = applyMidiEffects(
        {fait("arpeggio", {{"Division", 4.0f}})}, notes, 480);
    // Aucune des deux n'a de partenaire au même tick : rien n'est arpégé.
    VSM_ASSERT_EQ(joue.size(), size_t{2});
    VSM_ASSERT_EQ(joue[0].startTick, Tick{0});
    VSM_ASSERT_EQ(joue[1].startTick, Tick{240});
}

VSM_TEST(transposing_drops_what_leaves_the_keyboard_and_says_so) {
    std::vector<Note> notes;
    uint64_t id = 1;
    notes.push_back({0, 480, 0, 10, 100, 64, id++});    // sortira par le bas
    notes.push_back({0, 480, 0, 60, 100, 64, id++});
    MidiEffectReport rapport;
    const std::vector<Note> joue = applyMidiEffects(
        {fait("transpose", {{"Semitones", -24.0f}})}, notes, 480, &rapport);
    VSM_ASSERT_EQ(joue.size(), size_t{1});
    VSM_ASSERT_EQ(joue[0].number, uint8_t{36});
    // ÉCARTÉE ET COMPTÉE, jamais repliée à l'octave.
    VSM_ASSERT_EQ(rapport.droppedOutOfRange, size_t{1});
}

VSM_TEST(velocity_never_reaches_zero) {
    // Une vélocité nulle est un NoteOff déguisé : une piste qu'on voulait
    // seulement adoucir cesserait de sonner sans qu'aucune note ait disparu.
    std::vector<Note> notes;
    uint64_t id = 1;
    notes.push_back({0, 480, 0, 60, 100, 64, id++});
    notes.push_back({0, 480, 0, 62, 10, 64, id++});
    const std::vector<Note> joue = applyMidiEffects(
        {fait("velocity", {{"Scale", 0.0f}, {"Offset", -100.0f}})}, notes, 480);
    VSM_ASSERT_EQ(joue.size(), size_t{2});
    for (const auto& n : joue) VSM_ASSERT(n.velocity >= 1);

    // Et l'échelle fait ce qu'elle dit, décalage compris.
    const std::vector<Note> moitie = applyMidiEffects(
        {fait("velocity", {{"Scale", 0.5f}, {"Offset", 10.0f}})}, notes, 480);
    VSM_ASSERT_EQ(moitie[0].velocity, uint8_t{60});     // 100 * 0,5 + 10
    VSM_ASSERT_EQ(moitie[1].velocity, uint8_t{15});     // 10 * 0,5 + 10
}

VSM_TEST(a_bypassed_or_unknown_effect_does_nothing_and_the_unknown_one_is_counted) {
    const std::vector<Note> materiau = unAccord();
    MidiEffect eteint = fait("arpeggio", {{"Division", 4.0f}});
    eteint.enabled = false;
    VSM_ASSERT_EQ(applyMidiEffects({eteint}, materiau, 480).size(), size_t{3});

    MidiEffectReport rapport;
    const std::vector<Note> joue = applyMidiEffects({fait("chorus-midi", {})}, materiau, 480,
                                                     &rapport);
    VSM_ASSERT_EQ(joue.size(), size_t{3});
    VSM_ASSERT_EQ(rapport.unknownEffects, size_t{1});   // panne muette interdite
}

VSM_TEST(the_chain_order_is_respected) {
    // Transposer puis arpéger n'est pas la même chaîne qu'arpéger puis
    // transposer -- ici les deux donnent le même son, mais l'ordre est
    // appliqué, et un effet qui lirait les hauteurs le verrait.
    const std::vector<Note> materiau = unAccord();
    const std::vector<Note> joue = applyMidiEffects(
        {fait("transpose", {{"Semitones", 12.0f}}), fait("arpeggio", {{"Division", 4.0f}})},
        materiau, 480);
    VSM_ASSERT_EQ(joue.size(), size_t{16});
    VSM_ASSERT_EQ(joue[0].number, uint8_t{72});
}

// ---------------------------------------------------------------------------
// D31.3 — À LA LECTURE, ET SANS TOUCHER AU MATÉRIAU.
// ---------------------------------------------------------------------------

VSM_TEST(the_scheduler_plays_the_arpeggio_and_leaves_the_material_alone) {
    Project projet;
    projet.ticksPerQuarterNote = 480;
    Track piste; piste.name = "Accords"; piste.channel = 0;
    piste.notes = unAccord();
    projet.tracks.push_back(piste);

    const size_t sansChaine = PlaybackScheduler::build(projet, 0, 480 * 8).size();
    VSM_ASSERT_EQ(sansChaine, size_t{6});          // 3 NoteOn + 3 NoteOff

    projet.tracks[0].midiEffects.push_back(fait("arpeggio", {{"Division", 4.0f}}));
    const auto avec = PlaybackScheduler::build(projet, 0, 480 * 8);
    VSM_ASSERT_EQ(avec.size(), size_t{32});        // 16 NoteOn + 16 NoteOff

    // LE MATÉRIAU EST INTACT, et c'est l'attendu n° 2 de la phase.
    VSM_ASSERT_EQ(projet.tracks[0].notes.size(), size_t{3});
    VSM_ASSERT_EQ(projet.tracks[0].notes[0].endTick, Tick{480 * 4});
}

VSM_TEST(a_track_without_a_midi_chain_schedules_exactly_as_before) {
    // L'ATTENDU N° 3 : « rien ne change pour ce qui existe ». Le planning
    // d'une piste sans chaîne doit être identique ÉVÉNEMENT POUR ÉVÉNEMENT.
    Project projet;
    projet.ticksPerQuarterNote = 480;
    Track piste; piste.name = "T"; piste.channel = 0;
    uint64_t id = 1;
    piste.addNote(0, 480, 60, 100, 0, id);
    piste.addNote(480, 960, 64, 90, 0, id);
    piste.transposeSemitones = 3;      // et la transposition de D17.5 tient toujours
    projet.tracks.push_back(piste);

    const auto planning = PlaybackScheduler::build(projet, 0, 480 * 8);
    VSM_ASSERT_EQ(planning.size(), size_t{4});
    // La transposition de piste s'applique toujours, et elle est toujours la
    // seule à s'appliquer : la chaîne vide n'a rien ajouté ni retiré.
    const auto* premier = std::get_if<NoteOnEvent>(&planning[0].data);
    VSM_ASSERT(premier != nullptr);
    VSM_ASSERT_EQ(premier->note, uint8_t{63});
}

VSM_TEST(the_two_transpositions_compose_instead_of_fighting) {
    // La transposition de PISTE (D17.5) et l'effet MIDI de transposition
    // (D31.2) sont deux réglages distincts, et ils s'ajoutent. Il fallait le
    // trancher : la seconde ne remplace pas la première.
    Project projet;
    projet.ticksPerQuarterNote = 480;
    Track piste; piste.name = "T"; piste.channel = 0;
    uint64_t id = 1;
    piste.addNote(0, 480, 60, 100, 0, id);
    piste.transposeSemitones = 2;
    piste.midiEffects.push_back(fait("transpose", {{"Semitones", 5.0f}}));
    projet.tracks.push_back(piste);

    const auto planning = PlaybackScheduler::build(projet, 0, 480 * 4);
    const auto* on = std::get_if<NoteOnEvent>(&planning[0].data);
    VSM_ASSERT(on != nullptr);
    VSM_ASSERT_EQ(on->note, uint8_t{67});   // 60 + 5 (effet) + 2 (piste)
}
