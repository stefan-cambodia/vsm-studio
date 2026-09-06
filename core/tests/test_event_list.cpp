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
