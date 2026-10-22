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

VSM_TEST(the_arpeggio_mode_names_its_three_positions) {
    // D477 : le mode s'écrivait « 0 », « 1 », « 2 ». Une position par valeur
    // entière, dans l'ordre où `arpeger` les lit : 0 montant, 1 descendant,
    // 2 aller-retour.
    for (const auto& p : midiEffectParameters("arpeggio")) {
        if (std::string(p.name) != "Mode") { VSM_ASSERT(p.choices.empty()); continue; }
        VSM_ASSERT_EQ(p.choices.size(), size_t{3});
        VSM_ASSERT_EQ(p.choices.size(), static_cast<size_t>(p.maxValue - p.minValue) + 1);
        VSM_ASSERT_EQ(p.choices[0], std::string("Montant"));
        VSM_ASSERT_EQ(p.choices[2], std::string("Aller-retour"));
    }
}

// D573.3 — LA DURÉE EN EFFET MIDI (*Length Compression*). Quatre notes d'une croche (240 ticks) : un do à 0, un ré à 480,
// et deux mi répétés à 960 et 1 200 — le premier mi finit juste où le second commence. À 0,5 : toutes à 120, les départs
// intacts. À 2 : le do et le ré à 480 ; le PREMIER MI s'arrête au départ du second (240, le legato), le second à 480. À 4
// sur un mi qui chevauchait DÉJÀ son suivant, sa durée d'origine reste. Le matériau ne bouge pas ; le type est proposé.
VSM_TEST(the_length_effect_scales_durations_and_stops_at_the_next_same_note) {
    std::vector<Note> materiau = {{0, 240, 0, 60, 100, 64, 1}, {480, 720, 0, 62, 100, 64, 2},
                                  {960, 1200, 0, 64, 100, 64, 3}, {1200, 1440, 0, 64, 100, 64, 4}};
    auto durees = [](const std::vector<Note>& notes) {
        std::vector<Tick> d;
        for (const auto& n : notes) d.push_back(n.endTick - n.startTick);
        return d;
    };
    MidiEffectReport rapport;
    const auto moitie = applyMidiEffects({fait("length", {{"Ratio", 0.5f}})}, materiau, 480, &rapport);
    VSM_ASSERT(durees(moitie) == (std::vector<Tick>{120, 120, 120, 120}));
    for (size_t i = 0; i < materiau.size(); ++i) VSM_ASSERT_EQ(moitie[i].startTick, materiau[i].startTick);
    VSM_ASSERT_EQ(rapport.unknownEffects, size_t(0));
    const auto double_ = applyMidiEffects({fait("length", {{"Ratio", 2.0f}})}, materiau, 480);
    VSM_ASSERT(durees(double_) == (std::vector<Tick>{480, 480, 240, 480}));
    VSM_ASSERT(durees(materiau) == (std::vector<Tick>{240, 240, 240, 240}));   // le matériau intact
    // Le témoin à 1 : rien ne bouge. Hors bornes : bornée (0,25 et 4).
    VSM_ASSERT(durees(applyMidiEffects({fait("length", {{"Ratio", 1.0f}})}, materiau, 480)) == durees(materiau));
    VSM_ASSERT(durees(applyMidiEffects({fait("length", {{"Ratio", 0.01f}})}, materiau, 480))
               == (std::vector<Tick>{60, 60, 60, 60}));
    // Un mi qui chevauchait déjà le suivant (960-1 300 contre 1 200) garde ses 340 ticks à 4.
    materiau[2].endTick = 1300;
    VSM_ASSERT_EQ(durees(applyMidiEffects({fait("length", {{"Ratio", 4.0f}})}, materiau, 480))[2], Tick(340));
    // Un tick au moins.
    const std::vector<Note> breve = {{0, 1, 0, 60, 100, 64, 1}};
    VSM_ASSERT_EQ(applyMidiEffects({fait("length", {{"Ratio", 0.25f}})}, breve, 480)[0].endTick, Tick(1));
    const auto types = midiEffectTypes();
    VSM_ASSERT(std::find(types.begin(), types.end(), "length") != types.end());
    VSM_ASSERT_EQ(midiEffectDisplayName("length"), std::string("Durée"));
    VSM_ASSERT_EQ(midiEffectParameters("length").size(), size_t(1));
}

// D574.3 — LA PLAGE EN EFFET MIDI (*Range*). Des notes à 30, 60 et 90, de vélocités 20, 64 et 120. L'attendu écrit avant
// (ROADMAP-daw, D574) : limiter les hauteurs à 48-72 → 54, 60, 66 ; les filtrer → 60 seule, 2 écartées comptées ; limiter
// les vélocités à 40-100 → 40, 64, 100 ; la plage entière → les notes d'origine. Et une plage plus étroite qu'une octave
// qui n'a pas de place pour une hauteur l'écarte, comptée ; un Min au-dessus du Max ne fait rien, compté.
VSM_TEST(the_range_effect_limits_or_filters_and_counts_what_it_drops) {
    const std::vector<Note> materiau = {{0, 240, 0, 30, 20, 64, 1}, {240, 480, 0, 60, 64, 64, 2},
                                        {480, 720, 0, 90, 120, 64, 3}};
    auto hauteurs = [](const std::vector<Note>& n) {
        std::vector<int> h;
        for (const auto& x : n) h.push_back(x.number);
        return h;
    };
    auto velocites = [](const std::vector<Note>& n) {
        std::vector<int> v;
        for (const auto& x : n) v.push_back(x.velocity);
        return v;
    };
    auto plage = [&](float mode, float mini, float maxi, MidiEffectReport* r = nullptr) {
        return applyMidiEffects({fait("range", {{"Mode", mode}, {"Min", mini}, {"Max", maxi}})}, materiau, 480, r);
    };
    VSM_ASSERT(hauteurs(plage(0, 48, 72)) == (std::vector<int>{54, 60, 66}));
    MidiEffectReport filtre;
    VSM_ASSERT(hauteurs(plage(1, 48, 72, &filtre)) == (std::vector<int>{60}));
    VSM_ASSERT_EQ(filtre.outsideRange, size_t(2));
    VSM_ASSERT(velocites(plage(2, 40, 100)) == (std::vector<int>{40, 64, 100}));
    MidiEffectReport vel;
    VSM_ASSERT(velocites(plage(3, 40, 100, &vel)) == (std::vector<int>{64}));
    VSM_ASSERT_EQ(vel.outsideRange, size_t(2));
    // Le témoin : la plage entière ne change rien.
    VSM_ASSERT(hauteurs(plage(0, 0, 127)) == hauteurs(materiau));
    VSM_ASSERT(velocites(plage(2, 0, 127)) == velocites(materiau));
    // 61-65 : 60 → 72 ? non, 72 > 65 ; aucune octave ne loge le do — écarté, compté ; le fa dièse (30 → 66) non plus.
    MidiEffectReport etroit;
    VSM_ASSERT(hauteurs(plage(0, 61, 65, &etroit)).empty());
    VSM_ASSERT_EQ(etroit.outsideRange, size_t(3));
    MidiEffectReport inverse;
    VSM_ASSERT(hauteurs(plage(1, 72, 48, &inverse)) == hauteurs(materiau));
    VSM_ASSERT_EQ(inverse.invertedRanges, size_t(1));
    // Une vélocité limitée ne tombe jamais à zéro.
    VSM_ASSERT(velocites(plage(2, 0, 0)) == (std::vector<int>{1, 1, 1}));
    VSM_ASSERT_EQ(midiEffectDisplayName("range"), std::string("Plage"));
}

// D575.3 — L'ALÉATOIRE EN EFFET MIDI (*Random*). L'attendu écrit avant (ROADMAP-daw, D575) : *Position* −30..+30 sur
// seize notes — tous les départs dans ±30 de l'origine, au moins un qui bouge, le MÊME tirage à chaque passe ; 0..0 rend
// les notes d'origine ; une autre graine, un autre tirage. ET LE TIRAGE NE DÉPEND PAS DES IDENTIFIANTS, que le chargement
// redonne : les mêmes notes renumérotées rendent les mêmes positions. Une hauteur poussée hors du clavier est écartée et
// comptée ; Min > Max s'échangent.
VSM_TEST(the_random_effect_is_bounded_reproducible_and_blind_to_note_ids) {
    std::vector<Note> seize;
    for (int i = 0; i < 16; ++i)
        seize.push_back({static_cast<Tick>(480 * i + 240), static_cast<Tick>(480 * i + 480), 0,
                         static_cast<uint8_t>(60 + i % 5), 100, 64, static_cast<uint64_t>(i + 1)});
    auto debuts = [](const std::vector<Note>& n) {
        std::vector<Tick> d;
        for (const auto& x : n) d.push_back(x.startTick);
        return d;
    };
    auto tirer = [&](const std::vector<Note>& n, float mini, float maxi, float graine, float cible = 0.0f,
                     MidiEffectReport* r = nullptr) {
        return applyMidiEffects({fait("random", {{"Cible", cible}, {"Min", mini}, {"Max", maxi}, {"Graine", graine}})},
                                n, 480, r);
    };
    const auto a = tirer(seize, -30, 30, 7);
    size_t bougees = 0;
    for (size_t i = 0; i < seize.size(); ++i) {
        const Tick e = a[i].startTick - seize[i].startTick;
        VSM_ASSERT(e >= -30 && e <= 30);
        VSM_ASSERT_EQ(a[i].endTick - a[i].startTick, Tick(240));   // la durée gardée
        if (e != 0) ++bougees;
    }
    VSM_ASSERT(bougees > 0);
    VSM_ASSERT(debuts(tirer(seize, -30, 30, 7)) == debuts(a));          // la même passe, le même tirage
    VSM_ASSERT(debuts(tirer(seize, 30, -30, 7)) == debuts(a));          // Min > Max s'échangent
    VSM_ASSERT(debuts(tirer(seize, 0, 0, 7)) == debuts(seize));         // le témoin
    VSM_ASSERT(debuts(tirer(seize, -30, 30, 8)) != debuts(a));          // une autre graine
    auto renumerotees = seize;
    for (auto& n : renumerotees) n.id += 1000;
    VSM_ASSERT(debuts(tirer(renumerotees, -30, 30, 7)) == debuts(a));   // aveugle aux identifiants
    // La hauteur : +48 sur un do 60 sort pour certaines ; écartées, comptées.
    MidiEffectReport r;
    const auto hautes = tirer(seize, 48, 48, 1, 1.0f, &r);
    VSM_ASSERT_EQ(hautes.size(), seize.size());                         // 60..64 + 48 = 108..112 : dans le clavier
    const auto tropHautes = tirer({Note{0, 240, 0, 100, 100, 64, 1}}, 48, 48, 1, 1.0f, &r);
    VSM_ASSERT(tropHautes.empty());
    VSM_ASSERT_EQ(r.droppedOutOfRange, size_t(1));
    VSM_ASSERT_EQ(midiEffectDisplayName("random"), std::string("Aléatoire"));
}
