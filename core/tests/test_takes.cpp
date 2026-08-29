#include "TestFramework.h"
#include "vsm/sequencer/Track.h"
#include <string>
#include <vector>

using namespace vsm::midi;
using namespace vsm::sequencer;

// D3.5 de docs/ROADMAP-daw.md — LES PRISES EMPILÉES.
//
// Le critère de l'étape est « les prises se conservent et se choisissent », et
// ce qui suit vérifie exactement ces deux mots. Le modèle retenu est celui du
// RANGEMENT : la piste garde un seul matériau courant, les prises inactives
// attendent à côté (voir `Take` pour le modèle écarté).

namespace {
Track pisteAvecNotes(std::initializer_list<int> hauteurs) {
    Track piste;
    uint64_t compteur = 1;
    Tick t = 0;
    for (int h : hauteurs) {
        piste.addNote(t, t + 240, static_cast<uint8_t>(h), 100, 0, compteur);
        t += 480;
    }
    return piste;
}

Take priseAvecNotes(const std::string& nom, std::initializer_list<int> hauteurs) {
    Take prise;
    prise.name = nom;
    uint64_t compteur = 1000;
    Tick t = 0;
    for (int h : hauteurs) {
        Note note;
        note.startTick = t;
        note.endTick = t + 240;
        note.number = static_cast<uint8_t>(h);
        note.id = compteur++;
        prise.notes.push_back(note);
        t += 480;
    }
    return prise;
}

/// Les hauteurs de la piste, en clair : le cadre de test compare par
/// `operator<<`, et une chaîne dit tout de suite CE QUI diffère.
std::string hauteursDe(const Track& piste) {
    std::string sortie;
    for (const auto& n : piste.notes) {
        if (!sortie.empty()) sortie += " ";
        sortie += std::to_string(n.number);
    }
    return sortie;
}
}

VSM_TEST(a_track_without_takes_behaves_exactly_as_before) {
    // « Vide » veut dire « aucune prise empilée », pas « prise vide » : c'est la
    // même règle que pour les clips, et c'est ce qui rend la migration des
    // projets existants littéralement vide.
    Track piste = pisteAvecNotes({60, 62, 64});
    VSM_ASSERT(piste.takes.empty());
    VSM_ASSERT_EQ(piste.activeTake, -1);
    VSM_ASSERT_EQ(piste.notes.size(), size_t(3));
}

VSM_TEST(the_material_that_was_there_becomes_take_zero) {
    // Sans cela, le premier enregistrement empilé effacerait ce qui était là --
    // typiquement une partie reconstruite, c'est-à-dire le plus précieux.
    Track piste = pisteAvecNotes({60, 62});
    pushTake(piste, priseAvecNotes("Prise 1", {70, 71, 72}));

    VSM_ASSERT_EQ(piste.takes.size(), size_t(2));
    VSM_ASSERT_EQ(piste.takes[0].name, std::string("Origine"));
    VSM_ASSERT_EQ(piste.takes[0].notes.size(), size_t(2));
    VSM_ASSERT_EQ(piste.activeTake, 1);
    // La piste joue la prise qu'on vient de faire.
    VSM_ASSERT_EQ(hauteursDe(piste), std::string("70 71 72"));
}

VSM_TEST(an_empty_track_does_not_gain_an_empty_origin_take) {
    // Empiler une prise sur une piste vide ne doit pas fabriquer une prise vide
    // qu'il faudrait ensuite expliquer à l'utilisateur.
    Track piste;
    pushTake(piste, priseAvecNotes("Prise 1", {60}));
    VSM_ASSERT_EQ(piste.takes.size(), size_t(1));
    VSM_ASSERT_EQ(piste.activeTake, 0);
    VSM_ASSERT_EQ(hauteursDe(piste), std::string("60"));
}

VSM_TEST(choosing_a_take_swaps_the_material_and_loses_nothing) {
    Track piste = pisteAvecNotes({60, 62});
    pushTake(piste, priseAvecNotes("Prise 1", {70}));
    pushTake(piste, priseAvecNotes("Prise 2", {80, 81}));
    VSM_ASSERT_EQ(piste.takes.size(), size_t(3));

    selectTake(piste, 0);
    VSM_ASSERT_EQ(piste.activeTake, 0);
    VSM_ASSERT_EQ(hauteursDe(piste), std::string("60 62"));

    selectTake(piste, 1);
    VSM_ASSERT_EQ(hauteursDe(piste), std::string("70"));

    selectTake(piste, 2);
    VSM_ASSERT_EQ(hauteursDe(piste), std::string("80 81"));
}

VSM_TEST(an_edit_belongs_to_the_take_it_was_made_on) {
    // C'est le coût assumé du modèle, et il doit être VRAI : on corrige la prise
    // qu'on écoute, et la correction reste sur elle quand on revient.
    Track piste;
    pushTake(piste, priseAvecNotes("Prise 1", {70}));
    pushTake(piste, priseAvecNotes("Prise 2", {80}));

    selectTake(piste, 0);
    uint64_t compteur = 5000;
    piste.addNote(2000, 2200, 99, 100, 0, compteur);   // correction sur la prise 1
    VSM_ASSERT_EQ(piste.notes.size(), size_t(2));

    selectTake(piste, 1);
    VSM_ASSERT_EQ(hauteursDe(piste), std::string("80"));   // la prise 2 est intacte

    selectTake(piste, 0);
    VSM_ASSERT_EQ(piste.notes.size(), size_t(2));               // la correction a tenu
}

VSM_TEST(choosing_the_active_take_or_a_missing_one_changes_nothing) {
    Track piste;
    pushTake(piste, priseAvecNotes("Prise 1", {70}));
    selectTake(piste, 0);
    VSM_ASSERT_EQ(hauteursDe(piste), std::string("70"));
    selectTake(piste, 7);
    VSM_ASSERT_EQ(piste.activeTake, 0);
    selectTake(piste, -3);
    VSM_ASSERT_EQ(piste.activeTake, 0);
    VSM_ASSERT_EQ(hauteursDe(piste), std::string("70"));
}

VSM_TEST(audio_takes_share_one_file_and_differ_by_their_window) {
    // L'ENREGISTREMENT EN BOUCLE ÉCRIT UN SEUL FICHIER, et chaque passe est une
    // FENÊTRE dessus -- le modèle de la région, comme pour les clips. Le
    // contraire obligerait à fermer et rouvrir un fichier au passage exact de
    // la boucle, c'est-à-dire à faire une pause au seul endroit où il ne faut
    // pas.
    Track piste;
    piste.kind = Track::Kind::Audio;

    for (int passe = 0; passe < 3; ++passe) {
        Take prise;
        prise.name = "Prise " + std::to_string(passe + 1);
        prise.audio.path = "audio/boucle-1.wav";
        prise.audio.sampleRate = 48000.0;
        prise.audio.frames = 48000 * 6;
        prise.audio.channels = 2;
        Clip clip;
        clip.startTick = 1920;                       // le début de la boucle
        clip.length = 3840;                          // deux mesures
        clip.sourceStartSeconds = 2.0 * passe;       // la passe dans le fichier
        prise.clips.push_back(clip);
        prise.startTick = 1920;
        prise.endTick = 1920 + 3840;
        pushTake(piste, std::move(prise));
    }

    VSM_ASSERT_EQ(piste.takes.size(), size_t(3));
    for (const auto& prise : piste.takes)
        VSM_ASSERT_EQ(prise.audio.path, std::string("audio/boucle-1.wav"));

    selectTake(piste, 0);
    VSM_ASSERT_EQ(piste.clips.size(), size_t(1));
    VSM_ASSERT_NEAR(piste.clips[0].sourceStartSeconds, 0.0, 1e-12);
    VSM_ASSERT_EQ(piste.clips[0].startTick, Tick(1920));

    selectTake(piste, 2);
    VSM_ASSERT_NEAR(piste.clips[0].sourceStartSeconds, 4.0, 1e-12);
    VSM_ASSERT_EQ(piste.clips[0].startTick, Tick(1920));   // toutes au même endroit
}
