#include "TestFramework.h"
#include "vsm/sequencer/EventList.h"

using namespace vsm::midi;
using namespace vsm::sequencer;

// D32.2 de docs/ROADMAP-daw.md — LA LISTE DES ÉVÉNEMENTS.
//
// L'attendu, écrit avant la mesure : « sur une piste portant les cinq natures,
// la liste compte EXACTEMENT autant de lignes que le modèle porte
// d'événements. Un écart dirait qu'une nature est oubliée par la vue, ce qui
// est le défaut qu'on répare. »

namespace {

/// Une piste qui porte les SIX familles -- les cinq oubliées plus les notes.
Track pisteComplete() {
    Track t; t.name = "Tout"; t.channel = 0;
    uint64_t id = 1;
    t.addNote(0, 480, 60, 100, 0, id);
    t.addNote(480, 960, 64, 90, 0, id);
    t.controlChanges.push_back({240, 0, 74, 64});
    t.controlChanges.push_back({720, 0, 1, 127});
    t.pitchBends.push_back({120, 0, -4096});
    t.polyAftertouch.push_back({300, 0, 60, 90});
    t.channelPressure.push_back({360, 0, 55});
    t.programChanges.push_back({0, 0, 12});
    return t;
}

size_t combienDansLeModele(const Track& t) {
    return t.notes.size() + t.controlChanges.size() + t.pitchBends.size()
           + t.polyAftertouch.size() + t.channelPressure.size() + t.programChanges.size();
}

} // namespace

VSM_TEST(the_list_shows_every_event_the_model_carries) {
    const Track piste = pisteComplete();
    const auto lignes = listTrackEvents(piste);
    // LE CHIFFRE DE L'ÉTAPE : autant de lignes que d'événements, pas une de
    // moins. Une nature oubliée par la vue se verrait ici et nulle part ailleurs.
    VSM_ASSERT_EQ(lignes.size(), combienDansLeModele(piste));
    VSM_ASSERT_EQ(lignes.size(), size_t{8});

    // Et les six familles sont toutes représentées.
    bool vu[6] = {false, false, false, false, false, false};
    for (const auto& l : lignes) vu[static_cast<int>(l.kind)] = true;
    for (bool b : vu) VSM_ASSERT(b);
}

VSM_TEST(the_list_is_sorted_by_tick_and_stable_within_a_tick) {
    const auto lignes = listTrackEvents(pisteComplete());
    for (size_t i = 1; i < lignes.size(); ++i)
        VSM_ASSERT(lignes[i - 1].tick <= lignes[i].tick);
    // Au tick 0 : le programme et la note. L'ordre doit être le MÊME d'une
    // ouverture à l'autre, sans quoi la ligne qu'on s'apprête à supprimer
    // aurait bougé entre le regard et le clic.
    const auto secondes = listTrackEvents(pisteComplete());
    for (size_t i = 0; i < lignes.size(); ++i) {
        VSM_ASSERT_EQ(static_cast<int>(lignes[i].kind), static_cast<int>(secondes[i].kind));
        VSM_ASSERT_EQ(lignes[i].tick, secondes[i].tick);
    }
}

VSM_TEST(the_numbers_are_raw) {
    // C'est tout l'intérêt d'une liste : voir -4096 et non « un peu en dessous ».
    const auto lignes = listTrackEvents(pisteComplete());
    bool trouve = false;
    for (const auto& l : lignes)
        if (l.kind == EventKind::PitchBend) { VSM_ASSERT_EQ(l.second, -4096); trouve = true; }
    VSM_ASSERT(trouve);

    for (const auto& l : lignes)
        if (l.kind == EventKind::Note && l.first == 60 && l.tick == 0) {
            VSM_ASSERT_EQ(l.second, 100);          // vélocité
            VSM_ASSERT_EQ(l.length, Tick{480});    // durée, en ticks
        }
}

VSM_TEST(removing_a_row_removes_exactly_that_event) {
    Track piste = pisteComplete();
    const auto lignes = listTrackEvents(piste);
    const size_t avant = combienDansLeModele(piste);

    // Un pli, qui n'a pas d'identifiant et se désigne par son rang.
    const EventRow* pli = nullptr;
    for (const auto& l : lignes) if (l.kind == EventKind::PitchBend) pli = &l;
    VSM_ASSERT(pli != nullptr);
    VSM_ASSERT(removeTrackEvent(piste, *pli));
    VSM_ASSERT_EQ(combienDansLeModele(piste), avant - 1);
    VSM_ASSERT(piste.pitchBends.empty());
    // Rien d'autre n'a bougé.
    VSM_ASSERT_EQ(piste.notes.size(), size_t{2});
    VSM_ASSERT_EQ(piste.controlChanges.size(), size_t{2});
}

VSM_TEST(removing_a_stale_row_refuses_instead_of_taking_the_neighbour) {
    // Si la piste a changé entre l'affichage et le clic, la ligne ne désigne
    // plus rien : on refuse, et on le dit. Supprimer le voisin serait la pire
    // des issues -- l'utilisateur croirait avoir retiré ce qu'il visait.
    Track piste = pisteComplete();
    const auto lignes = listTrackEvents(piste);
    const EventRow* cc = nullptr;
    for (const auto& l : lignes) if (l.kind == EventKind::ControlChange) cc = &l;
    VSM_ASSERT(cc != nullptr);
    EventRow perimee = *cc;
    perimee.tick += 7;                       // ce tick n'existe plus à ce rang
    VSM_ASSERT(!removeTrackEvent(piste, perimee));
    VSM_ASSERT_EQ(piste.controlChanges.size(), size_t{2});

    // Une note dont l'identifiant a disparu : même refus.
    EventRow noteFantome;
    noteFantome.kind = EventKind::Note;
    noteFantome.noteId = 99999;
    VSM_ASSERT(!removeTrackEvent(piste, noteFantome));
    VSM_ASSERT_EQ(piste.notes.size(), size_t{2});
}

VSM_TEST(an_empty_track_lists_nothing_rather_than_failing) {
    Track vide;
    VSM_ASSERT(listTrackEvents(vide).empty());
}

// ---------------------------------------------------------------------------
// D348 — MODIFIER UNE VALEUR DEPUIS LA LISTE.
//
// L'attendu, écrit avant la mesure : « chaque champ modifiable change CE QU'IL
// NOMME et rien d'autre ; une valeur hors bornes est REFUSÉE et ne borne pas en
// silence ; un champ qui n'a pas de sens pour la famille est refusé ; une ligne
// périmée est refusée comme pour la suppression. »
// ---------------------------------------------------------------------------

namespace {

const EventRow* ligneDe(const std::vector<EventRow>& lignes, EventKind nature) {
    for (const auto& l : lignes) if (l.kind == nature) return &l;
    return nullptr;
}

} // namespace

VSM_TEST(editing_a_note_changes_only_the_field_named) {
    Track piste = pisteComplete();
    const auto lignes = listTrackEvents(piste);
    const EventRow* note = ligneDe(lignes, EventKind::Note);
    VSM_ASSERT(note != nullptr);
    const auto avant = piste.notes;

    VSM_ASSERT(setTrackEventField(piste, *note, EventField::Value, 42));
    VSM_ASSERT_EQ(int{piste.notes[0].velocity}, 42);
    VSM_ASSERT_EQ(int{piste.notes[0].number}, int{avant[0].number});
    VSM_ASSERT_EQ(piste.notes[0].startTick, avant[0].startTick);
    VSM_ASSERT_EQ(piste.notes[0].endTick, avant[0].endTick);

    VSM_ASSERT(setTrackEventField(piste, *note, EventField::Number, 72));
    VSM_ASSERT_EQ(int{piste.notes[0].number}, 72);
    VSM_ASSERT_EQ(int{piste.notes[0].velocity}, 42);

    VSM_ASSERT(setTrackEventField(piste, *note, EventField::Length, 240));
    VSM_ASSERT_EQ(piste.notes[0].endTick - piste.notes[0].startTick, Tick{240});
}

VSM_TEST(moving_a_note_carries_its_length) {
    // LA DURÉE SUIT LA NOTE : déplacer n'est pas raccourcir. Sans cette règle,
    // corriger une position depuis la liste mangerait la fin de la note.
    Track piste = pisteComplete();
    const auto lignes = listTrackEvents(piste);
    const EventRow* note = ligneDe(lignes, EventKind::Note);
    VSM_ASSERT(note != nullptr);
    const Tick duree = piste.notes[0].endTick - piste.notes[0].startTick;
    VSM_ASSERT(setTrackEventField(piste, *note, EventField::Position, 1920));
    VSM_ASSERT_EQ(piste.notes[0].startTick, Tick{1920});
    VSM_ASSERT_EQ(piste.notes[0].endTick - piste.notes[0].startTick, duree);
}

VSM_TEST(an_out_of_range_value_is_refused_and_never_clamped) {
    // BORNER EN SILENCE SERAIT PIRE QUE REFUSER : l'utilisateur tape 300, voit
    // 127, et croit que le logiciel a compris autre chose que ce qu'il a écrit.
    Track piste = pisteComplete();
    const auto lignes = listTrackEvents(piste);
    const EventRow* note = ligneDe(lignes, EventKind::Note);
    VSM_ASSERT(note != nullptr);
    const int velocite = int{piste.notes[0].velocity};
    VSM_ASSERT(!setTrackEventField(piste, *note, EventField::Value, 300));
    VSM_ASSERT_EQ(int{piste.notes[0].velocity}, velocite);
    VSM_ASSERT(!setTrackEventField(piste, *note, EventField::Value, -1));
    VSM_ASSERT_EQ(int{piste.notes[0].velocity}, velocite);
    // Une durée nulle ou négative : refusée aussi (la note disparaîtrait).
    VSM_ASSERT(!setTrackEventField(piste, *note, EventField::Length, 0));
    VSM_ASSERT(piste.notes[0].endTick > piste.notes[0].startTick);
    // Une position négative, elle, se RAMÈNE à zéro : un tick négatif n'existe
    // pas et le geste est sans ambiguïté. C'est la seule exception, et elle est
    // écrite dans l'en-tête.
    VSM_ASSERT(setTrackEventField(piste, *note, EventField::Position, -50));
    VSM_ASSERT_EQ(piste.notes[0].startTick, Tick{0});
}

VSM_TEST(a_field_without_meaning_for_its_kind_is_refused) {
    Track piste = pisteComplete();
    const auto lignes = listTrackEvents(piste);
    const EventRow* cc = ligneDe(lignes, EventKind::ControlChange);
    const EventRow* pli = ligneDe(lignes, EventKind::PitchBend);
    const EventRow* prog = ligneDe(lignes, EventKind::ProgramChange);
    VSM_ASSERT(cc != nullptr && pli != nullptr && prog != nullptr);
    VSM_ASSERT(!setTrackEventField(piste, *cc, EventField::Length, 100));    // un CC n'a pas de durée
    VSM_ASSERT(!setTrackEventField(piste, *pli, EventField::Number, 12));    // un pli n'a pas de numéro
    VSM_ASSERT(!setTrackEventField(piste, *prog, EventField::Value, 12));    // un programme n'a que son numéro
    // Et ce qui a un sens passe.
    VSM_ASSERT(setTrackEventField(piste, *cc, EventField::Number, 7));
    VSM_ASSERT(setTrackEventField(piste, *pli, EventField::Value, -8192));
    VSM_ASSERT(setTrackEventField(piste, *prog, EventField::Number, 40));
}

VSM_TEST(editing_a_stale_row_refuses_instead_of_touching_the_neighbour) {
    Track piste = pisteComplete();
    const auto lignes = listTrackEvents(piste);
    const EventRow* cc = ligneDe(lignes, EventKind::ControlChange);
    VSM_ASSERT(cc != nullptr);
    EventRow perimee = *cc;
    perimee.tick += 7;
    const auto avant = piste.controlChanges;
    VSM_ASSERT(!setTrackEventField(piste, perimee, EventField::Value, 1));
    VSM_ASSERT_EQ(int{piste.controlChanges[0].value}, int{avant[0].value});

    EventRow noteFantome;
    noteFantome.kind = EventKind::Note;
    noteFantome.noteId = 99999;
    VSM_ASSERT(!setTrackEventField(piste, noteFantome, EventField::Value, 1));
}

VSM_TEST(a_pitch_bend_keeps_its_fourteen_bits) {
    Track piste = pisteComplete();
    const auto lignes = listTrackEvents(piste);
    const EventRow* pli = ligneDe(lignes, EventKind::PitchBend);
    VSM_ASSERT(pli != nullptr);
    VSM_ASSERT(setTrackEventField(piste, *pli, EventField::Value, 8191));
    VSM_ASSERT_EQ(int{piste.pitchBends[0].value}, 8191);
    VSM_ASSERT(!setTrackEventField(piste, *pli, EventField::Value, 8192));
    VSM_ASSERT_EQ(int{piste.pitchBends[0].value}, 8191);
    VSM_ASSERT(!setTrackEventField(piste, *pli, EventField::Value, -8193));
    VSM_ASSERT_EQ(int{piste.pitchBends[0].value}, 8191);
}


// ---------------------------------------------------------------------------
// D352 — CRÉER UN ÉVÉNEMENT DEPUIS LA LISTE.
//
// L'attendu, écrit avant la mesure : « chaque famille se crée et se retrouve
// ensuite dans la liste ; les lanes restent triées par tick ; une demande qui
// n'a pas de sens (durée nulle, valeur hors domaine, canal hors bornes) est
// REFUSÉE sans rien toucher ; et les deux familles qui ne se créent nulle part
// ailleurs — changement de programme et pression polyphonique — se créent ici. »
// ---------------------------------------------------------------------------

VSM_TEST(creating_an_event_of_each_kind_adds_exactly_one_row) {
    Track piste;
    uint64_t id = 1;
    const EventKind familles[] = { EventKind::Note, EventKind::ControlChange,
                                    EventKind::PitchBend, EventKind::PolyPressure,
                                    EventKind::ChannelPressure, EventKind::ProgramChange };
    size_t attendu = 0;
    for (const auto famille : familles) {
        const int premier = famille == EventKind::PitchBend ? 0 : 60;
        const int valeur = famille == EventKind::PitchBend ? -2048 : 90;
        VSM_ASSERT(addTrackEvent(piste, famille, 480, 0, premier, valeur, 240, id));
        ++attendu;
        VSM_ASSERT_EQ(listTrackEvents(piste).size(), attendu);
    }
    // Et chaque famille est bien celle qu'on a demandée.
    const auto lignes = listTrackEvents(piste);
    for (const auto famille : familles) {
        bool vue = false;
        for (const auto& l : lignes) if (l.kind == famille) vue = true;
        VSM_ASSERT(vue);
    }
}

VSM_TEST(a_created_program_change_is_reachable_nowhere_else) {
    // LE MANQUE QUE CETTE FONCTION COMBLE : le piano roll fait des notes, la
    // lane MIDI CC fait des contrôleurs, des plis et des pressions de canal.
    // Un changement de programme et une pression POLYPHONIQUE ne se posaient
    // nulle part.
    Track piste;
    uint64_t id = 1;
    VSM_ASSERT(addTrackEvent(piste, EventKind::ProgramChange, 960, 3, 41, 0, 0, id));
    VSM_ASSERT_EQ(piste.programChanges.size(), size_t{1});
    VSM_ASSERT_EQ(int{piste.programChanges[0].program}, 41);
    VSM_ASSERT_EQ(int{piste.programChanges[0].channel}, 3);
    VSM_ASSERT(addTrackEvent(piste, EventKind::PolyPressure, 960, 3, 64, 100, 0, id));
    VSM_ASSERT_EQ(piste.polyAftertouch.size(), size_t{1});
    VSM_ASSERT_EQ(int{piste.polyAftertouch[0].note}, 64);
    VSM_ASSERT_EQ(int{piste.polyAftertouch[0].pressure}, 100);
}

VSM_TEST(a_created_event_keeps_its_lane_sorted) {
    // `listTrackEvents` et le séquenceur supposent des lanes triées : un
    // événement posé AVANT ceux qui existent ne doit pas casser l'ordre.
    Track piste;
    uint64_t id = 1;
    VSM_ASSERT(addTrackEvent(piste, EventKind::ControlChange, 960, 0, 7, 100, 0, id));
    VSM_ASSERT(addTrackEvent(piste, EventKind::ControlChange, 240, 0, 7, 20, 0, id));
    VSM_ASSERT(addTrackEvent(piste, EventKind::ControlChange, 480, 0, 7, 60, 0, id));
    VSM_ASSERT_EQ(piste.controlChanges.size(), size_t{3});
    VSM_ASSERT_EQ(piste.controlChanges[0].tick, Tick{240});
    VSM_ASSERT_EQ(piste.controlChanges[1].tick, Tick{480});
    VSM_ASSERT_EQ(piste.controlChanges[2].tick, Tick{960});
}

VSM_TEST(a_creation_that_makes_no_sense_is_refused_without_touching_anything) {
    Track piste;
    uint64_t id = 1;
    const uint64_t idAvant = id;
    VSM_ASSERT(!addTrackEvent(piste, EventKind::Note, 0, 0, 60, 100, 0, id));      // durée nulle
    VSM_ASSERT(!addTrackEvent(piste, EventKind::Note, -1, 0, 60, 100, 240, id));   // tick négatif
    VSM_ASSERT(!addTrackEvent(piste, EventKind::Note, 0, 0, 300, 100, 240, id));   // hauteur hors bornes
    VSM_ASSERT(!addTrackEvent(piste, EventKind::Note, 0, 16, 60, 100, 240, id));   // canal hors bornes
    VSM_ASSERT(!addTrackEvent(piste, EventKind::PitchBend, 0, 0, 0, 9000, 0, id)); // pli hors 14 bits
    VSM_ASSERT(!addTrackEvent(piste, EventKind::ProgramChange, 0, 0, 128, 0, 0, id));
    VSM_ASSERT(listTrackEvents(piste).empty());
    VSM_ASSERT_EQ(id, idAvant);   // aucun identifiant de note consommé pour rien
}


// ---------------------------------------------------------------------------
// D353 — LE CANAL SE MODIFIE, ET IL NE DÉPLACE RIEN.
//
// L'attendu, écrit avant la mesure : « le canal se change dans les six
// familles ; hors de 0-15 il est REFUSÉ et non borné ; et changer le canal
// d'une note ne la retire pas de sa piste — dans ce logiciel le canal décide de
// ce qui SORT, pas de qui joue. »
// ---------------------------------------------------------------------------

VSM_TEST(the_channel_can_be_changed_in_every_kind) {
    // UN SEUL ÉVÉNEMENT PAR FAMILLE : `pisteComplete()` en porte deux de
    // certaines, et `ligneDe` rend la première — la seconde serait restée sur
    // son canal et aurait fait échouer un attendu mal écrit (elle l'a fait).
    Track piste;
    uint64_t id = 1;
    const EventKind familles[] = { EventKind::Note, EventKind::ControlChange,
                                    EventKind::PitchBend, EventKind::PolyPressure,
                                    EventKind::ChannelPressure, EventKind::ProgramChange };
    for (const auto famille : familles) {
        const int premier = famille == EventKind::PitchBend ? 0 : 60;
        const int valeur = famille == EventKind::PitchBend ? -2048 : 90;
        VSM_ASSERT(addTrackEvent(piste, famille, 480, 0, premier, valeur, 240, id));
    }
    for (const auto& ligne : listTrackEvents(piste))
        VSM_ASSERT(setTrackEventField(piste, ligne, EventField::Channel, 9));
    // Relue, chaque ligne porte le canal 9 — les six familles comprises.
    const auto apres = listTrackEvents(piste);
    VSM_ASSERT_EQ(apres.size(), size_t{6});
    for (const auto& l : apres)
        VSM_ASSERT_EQ(int{l.channel}, 9);
}

VSM_TEST(a_channel_out_of_range_is_refused_and_the_note_stays_on_its_track) {
    Track piste = pisteComplete();
    const auto lignes = listTrackEvents(piste);
    const EventRow* note = ligneDe(lignes, EventKind::Note);
    VSM_ASSERT(note != nullptr);
    const size_t combien = piste.notes.size();
    const int canal = int{piste.notes[0].channel};
    VSM_ASSERT(!setTrackEventField(piste, *note, EventField::Channel, 16));
    VSM_ASSERT(!setTrackEventField(piste, *note, EventField::Channel, -1));
    VSM_ASSERT_EQ(int{piste.notes[0].channel}, canal);
    // CHANGER LE CANAL NE DÉPLACE RIEN : la note reste sur sa piste, et sa
    // hauteur comme sa durée sont intactes.
    VSM_ASSERT(setTrackEventField(piste, *note, EventField::Channel, 15));
    VSM_ASSERT_EQ(piste.notes.size(), combien);
    VSM_ASSERT_EQ(int{piste.notes[0].channel}, 15);
    VSM_ASSERT_EQ(int{piste.notes[0].number}, 60);
    VSM_ASSERT_EQ(piste.notes[0].endTick - piste.notes[0].startTick, Tick{480});
}

// D555.2 — SUPPRIMER LES CONTRÔLEURS : CC (la banque comptée à part), plis et pressions partent ; le changement de
// programme et les notes restent ; rejoué, plus rien.
VSM_TEST(removing_track_controllers_keeps_notes_and_program_changes) {
    Track t;
    uint64_t ids = 1;
    t.addNote(0, 480, 60, 100, 0, ids);
    t.controlChanges = {{0, 0, 0, 1}, {0, 0, 32, 0}, {0, 0, 7, 100}, {480, 0, 64, 127}};
    t.pitchBends = {{240, 0, 4096}};
    t.channelPressure = {{240, 0, 80}};
    t.polyAftertouch = {{240, 0, 60, 70}};
    t.programChanges = {{0, 0, 5}};
    const auto avant = countControllers(t);
    VSM_ASSERT_EQ(avant.total(), size_t(7));
    VSM_ASSERT_EQ(t.controlChanges.size(), size_t(4));   // compter ne touche à rien
    const auto r = removeAllControllers(t);
    VSM_ASSERT_EQ(r.controlChanges, size_t(4));
    VSM_ASSERT_EQ(r.bankSelects, size_t(2));
    VSM_ASSERT_EQ(r.pitchBends, size_t(1));
    VSM_ASSERT_EQ(r.channelPressure, size_t(1));
    VSM_ASSERT_EQ(r.polyAftertouch, size_t(1));
    VSM_ASSERT(t.controlChanges.empty() && t.pitchBends.empty() && t.channelPressure.empty() && t.polyAftertouch.empty());
    VSM_ASSERT_EQ(t.programChanges.size(), size_t(1));
    VSM_ASSERT_EQ(t.notes.size(), size_t(1));
    VSM_ASSERT_EQ(removeAllControllers(t).total(), size_t(0));
}


// D557.2 — ÉCLAIRCIR LES CONTRÔLEURS (*Thin Out Data*) : dans chaque flux, un événement part quand il reste à l'écart
// choisi ou moins de la dernière valeur GARDÉE ; le premier et le dernier restent ; les commandes (banque, RPN, entrée
// de données) et les poids faibles restent tous ; notes et programmes intacts ; l'écart rejoué tient la tolérance.
VSM_TEST(thinning_controllers_keeps_ends_commands_and_the_tolerance) {
    Track t;
    uint64_t ids = 1;
    t.addNote(0, 480, 60, 100, 0, ids);
    t.programChanges = {{0, 0, 5}};
    // Une molette (CC 1) qui monte de 0 à 10 un par un, puis un doublon ; le volume (CC 7) répété ; deux RPN égaux et
    // une banque répétée (des COMMANDES, laissées) ; un poids faible (CC 33) répété.
    for (int v = 0; v <= 10; ++v) t.controlChanges.push_back({static_cast<Tick>(v * 10), 0, 1, static_cast<uint8_t>(v)});
    t.controlChanges.push_back({110, 0, 1, 10});
    t.controlChanges.push_back({0, 0, 7, 100});
    t.controlChanges.push_back({50, 0, 7, 100});
    t.controlChanges.push_back({100, 0, 7, 100});
    t.controlChanges.push_back({0, 0, 101, 0});
    t.controlChanges.push_back({10, 0, 101, 0});
    t.controlChanges.push_back({0, 0, 0, 1});
    t.controlChanges.push_back({10, 0, 0, 1});
    t.controlChanges.push_back({0, 0, 33, 3});
    t.controlChanges.push_back({10, 0, 33, 3});
    // Un pli qui revient au centre par petits pas (14 bits : l'écart 1 vaut 128).
    t.pitchBends = {{0, 0, 0}, {10, 0, 1000}, {20, 0, 1100}, {30, 0, 1200}, {40, 0, 60}, {50, 0, 0}};
    t.channelPressure = {{0, 0, 10}, {10, 0, 11}, {20, 0, 12}};
    t.polyAftertouch = {{0, 0, 60, 50}, {10, 0, 60, 50}, {10, 0, 62, 50}};
    t.sortEvents();
    const Track avant = t;

    // L'écart 0 ne retire que les DOUBLONS : le 10 répété de la molette (le dernier du flux, mais il ne change pas la
    // valeur), deux volumes, la pression polyphonique répétée de la note 60 (pas celle de la 62, un autre flux) — et ni
    // la banque, ni le RPN, ni le poids faible.
    {
        Track u = avant;
        const auto r = thinControllers(u, 0);
        VSM_ASSERT_EQ(r.removed, size_t(4));
        VSM_ASSERT_EQ(r.untouched, size_t(6));
        VSM_ASSERT_EQ(maxControllerDeviation(avant, u), 0.0);
        VSM_ASSERT_EQ(countThinnableControllers(avant, 0).removed, size_t(4));   // l'aperçu dit la même chose
    }
    // L'écart 2 : la molette garde 0, 3, 6, 9, et son dernier 10 ; le pli (écart 256 en 14 bits) garde 0, 1000, 60, et
    // son dernier 0 ; la pression garde 10 et son dernier 12.
    {
        Track u = avant;
        const auto r = thinControllers(u, 2);
        size_t molette = 0;
        for (const auto& c : u.controlChanges) if (c.controller == 1) ++molette;
        VSM_ASSERT_EQ(molette, size_t(5));
        for (size_t i = 1; i < u.controlChanges.size(); ++i)   // toujours trié par tick
            VSM_ASSERT(u.controlChanges[i - 1].tick <= u.controlChanges[i].tick);
        VSM_ASSERT_EQ(u.pitchBends.size(), size_t(4));
        VSM_ASSERT_EQ(u.pitchBends.back().value, int16_t(0));   // le dernier du flux, le centre
        VSM_ASSERT_EQ(u.channelPressure.size(), size_t(2));
        VSM_ASSERT_EQ(u.channelPressure.back().pressure, uint8_t(12));
        VSM_ASSERT(maxControllerDeviation(avant, u) <= 2.0);
        VSM_ASSERT(maxControllerDeviation(avant, u) > 0.0);   // la mesure voit bien quelque chose
        VSM_ASSERT_EQ(r.removed, avant.controlChanges.size() + avant.pitchBends.size() + avant.channelPressure.size()
                                     + avant.polyAftertouch.size() - u.controlChanges.size() - u.pitchBends.size()
                                     - u.channelPressure.size() - u.polyAftertouch.size());
        VSM_ASSERT_EQ(u.notes.size(), size_t(1));
        VSM_ASSERT_EQ(u.programChanges.size(), size_t(1));
        size_t commandes = 0;
        for (const auto& c : u.controlChanges) if (c.controller == 0 || c.controller == 101 || c.controller == 33) ++commandes;
        VSM_ASSERT_EQ(commandes, size_t(6));
        // Rejoué : ce qui reste est déjà éclairci.
        VSM_ASSERT_EQ(thinControllers(u, 2).removed, size_t(0));
    }
    // L'écart négatif ne retire rien.
    {
        Track u = avant;
        VSM_ASSERT_EQ(thinControllers(u, -1).removed, size_t(0));
    }
}

// L'écart rejoué se mesure sur l'UNION des ticks : sur la seule piste éclaircie, il rendrait zéro à tous les coups.
VSM_TEST(controller_deviation_is_measured_where_events_were_removed) {
    Track a;
    a.controlChanges = {{0, 0, 1, 0}, {10, 0, 1, 5}, {20, 0, 1, 9}};
    Track b;
    b.controlChanges = {{0, 0, 1, 0}, {20, 0, 1, 9}};
    VSM_ASSERT_EQ(maxControllerDeviation(a, b), 5.0);
    Track c;
    c.pitchBends = {{0, 0, 0}, {10, 0, 256}};
    Track d;
    d.pitchBends = {{0, 0, 0}};
    VSM_ASSERT_EQ(maxControllerDeviation(c, d), 2.0);   // 256 en 14 bits = 2 en 7 bits
}
