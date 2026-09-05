#include <set>
#include "TestFramework.h"
#include "vsm/sequencer/Project.h"
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

// --- D4.2 : suppression d'une piste et routages de groupe ------------------

VSM_TEST(removing_a_track_renumbers_the_group_routings_that_follow_it) {
    // LE PIÈGE : supprimer une piste décale toutes les suivantes, et un routage
    // qui visait la piste 5 viserait la 4. Le mixage partirait dans un autre
    // groupe sans qu'aucun réglage n'ait bougé -- le genre de défaut qu'on
    // n'attribue jamais à la suppression qui l'a causé.
    Project projet;
    for (int i = 0; i < 5; ++i) projet.tracks.emplace_back();
    projet.tracks[3].kind = Track::Kind::Group;
    projet.tracks[3].name = "Batterie";
    projet.tracks[4].kind = Track::Kind::Group;
    projet.tracks[4].name = "Claviers";
    projet.tracks[0].outputGroup = 3;   // vers Batterie
    projet.tracks[1].outputGroup = 4;   // vers Claviers
    projet.tracks[2].outputGroup = -1;  // vers le master

    removeTrack(projet, 0);             // on retire la première piste

    VSM_ASSERT_EQ(projet.tracks.size(), size_t(4));
    // Batterie est passée de 3 à 2, Claviers de 4 à 3 : les routages suivent.
    VSM_ASSERT_EQ(projet.tracks[0].outputGroup, 3);   // ex-piste 1 -> Claviers
    VSM_ASSERT_EQ(projet.tracks[1].outputGroup, -1);  // ex-piste 2 -> master
    VSM_ASSERT_EQ(projet.tracks[2].name, std::string("Batterie"));
}

VSM_TEST(removing_a_group_sends_its_members_back_to_the_master) {
    // Le seul choix qui ne fasse pas disparaître leur son.
    Project projet;
    for (int i = 0; i < 3; ++i) projet.tracks.emplace_back();
    projet.tracks[2].kind = Track::Kind::Group;
    projet.tracks[0].outputGroup = 2;
    projet.tracks[1].outputGroup = 2;

    removeTrack(projet, 2);

    VSM_ASSERT_EQ(projet.tracks.size(), size_t(2));
    VSM_ASSERT_EQ(projet.tracks[0].outputGroup, -1);
    VSM_ASSERT_EQ(projet.tracks[1].outputGroup, -1);
}

VSM_TEST(removing_a_track_out_of_range_changes_nothing) {
    Project projet;
    projet.tracks.emplace_back();
    projet.tracks[0].outputGroup = 7;
    removeTrack(projet, 42);
    VSM_ASSERT_EQ(projet.tracks.size(), size_t(1));
    VSM_ASSERT_EQ(projet.tracks[0].outputGroup, 7);
}

// --- D5.3 : réordonner les pistes ------------------------------------------

VSM_TEST(moving_a_track_carries_the_group_routings_with_the_tracks) {
    // LA RÈGLE, simple à énoncer et impossible à deviner : ce ne sont pas les
    // index qui suivent, ce sont les PISTES. Une piste qui allait dans un
    // groupe continue d'y aller, où que ce groupe se retrouve -- sinon le
    // mixage partirait ailleurs sans qu'aucun réglage n'ait bougé.
    Project projet;
    for (int i = 0; i < 4; ++i) projet.tracks.emplace_back();
    projet.tracks[0].name = "Voix";
    projet.tracks[1].name = "Basse";
    projet.tracks[2].name = "Groupe A";
    projet.tracks[2].kind = Track::Kind::Group;
    projet.tracks[3].name = "Groupe B";
    projet.tracks[3].kind = Track::Kind::Group;
    projet.tracks[0].outputGroup = 3;   // Voix -> Groupe B
    projet.tracks[1].outputGroup = 2;   // Basse -> Groupe A

    // On remonte le Groupe B tout en haut : tous les index changent.
    moveTrack(projet, 3, 0);

    VSM_ASSERT_EQ(projet.tracks[0].name, std::string("Groupe B"));
    VSM_ASSERT_EQ(projet.tracks[1].name, std::string("Voix"));
    VSM_ASSERT_EQ(projet.tracks[2].name, std::string("Basse"));
    VSM_ASSERT_EQ(projet.tracks[3].name, std::string("Groupe A"));
    // Et les routages désignent toujours LES MÊMES pistes.
    VSM_ASSERT_EQ(projet.tracks[1].outputGroup, 0);   // Voix -> Groupe B
    VSM_ASSERT_EQ(projet.tracks[2].outputGroup, 3);   // Basse -> Groupe A
}

VSM_TEST(moving_a_track_downwards_keeps_the_routings_too) {
    Project projet;
    for (int i = 0; i < 3; ++i) projet.tracks.emplace_back();
    projet.tracks[0].name = "A";
    projet.tracks[1].name = "B";
    projet.tracks[2].name = "G";
    projet.tracks[2].kind = Track::Kind::Group;
    projet.tracks[0].outputGroup = 2;
    projet.tracks[1].outputGroup = 2;

    moveTrack(projet, 0, 2);   // A descend en dernier
    VSM_ASSERT_EQ(projet.tracks[0].name, std::string("B"));
    VSM_ASSERT_EQ(projet.tracks[1].name, std::string("G"));
    VSM_ASSERT_EQ(projet.tracks[2].name, std::string("A"));
    VSM_ASSERT_EQ(projet.tracks[0].outputGroup, 1);
    VSM_ASSERT_EQ(projet.tracks[2].outputGroup, 1);
}

VSM_TEST(moving_a_track_carries_its_own_contents) {
    // Notes, effets, prises et automation vivent DANS la piste : les déplacer
    // ne demande rien de plus, et c'est précisément pourquoi ils y ont été
    // rangés (voir `TrackEffect`).
    Project projet;
    for (int i = 0; i < 3; ++i) projet.tracks.emplace_back();
    uint64_t ids = 1;
    projet.tracks[2].addNote(0, 480, 60, 100, 0, ids);
    projet.tracks[2].effects.push_back({"reverb", {}});
    projet.tracks[2].arrangementHeight = 120;
    projet.tracks[2].folded = true;

    moveTrack(projet, 2, 0);
    VSM_ASSERT_EQ(projet.tracks[0].notes.size(), size_t(1));
    VSM_ASSERT_EQ(projet.tracks[0].effects.size(), size_t(1));
    VSM_ASSERT_EQ(projet.tracks[0].arrangementHeight, 120);
    VSM_ASSERT(projet.tracks[0].folded);
}

VSM_TEST(moving_a_track_nowhere_or_out_of_range_changes_nothing) {
    Project projet;
    for (int i = 0; i < 2; ++i) projet.tracks.emplace_back();
    projet.tracks[0].name = "A";
    projet.tracks[1].name = "B";
    moveTrack(projet, 0, 0);
    moveTrack(projet, 5, 0);
    moveTrack(projet, 0, 9);
    VSM_ASSERT_EQ(projet.tracks[0].name, std::string("A"));
    VSM_ASSERT_EQ(projet.tracks[1].name, std::string("B"));
}

// D11.5 — DUPLIQUER UNE PISTE : tout est copié, les identifiants sont neufs,
// les routages sont réparés.

VSM_TEST(duplicating_a_track_copies_everything_with_fresh_ids) {
    Project projet;
    Track piste;
    piste.name = "Basse";
    piste.channel = 3;
    piste.colorRgba = 0xFF112233u;
    piste.instrumentId = "vsm.minimoog";
    piste.volume = 0.5f;
    Note n; n.startTick = 0; n.endTick = 480; n.number = 40; n.id = projet.nextNoteId();
    piste.notes.push_back(n);
    Clip c; c.startTick = 0; c.length = 1920; c.id = projet.nextClipId();
    piste.clips.push_back(c);
    projet.tracks.push_back(piste);

    const size_t copie = duplicateTrack(projet, 0);
    VSM_ASSERT_EQ(copie, size_t{1});
    VSM_ASSERT_EQ(projet.tracks.size(), size_t{2});
    const auto& d = projet.tracks[1];
    VSM_ASSERT_EQ(d.name, std::string("Basse (copie)"));
    VSM_ASSERT_EQ(static_cast<int>(d.channel), 3);
    VSM_ASSERT_EQ(d.colorRgba, 0xFF112233u);
    VSM_ASSERT_EQ(d.instrumentId, std::string("vsm.minimoog"));
    VSM_ASSERT_EQ(d.notes.size(), size_t{1});
    VSM_ASSERT_EQ(d.clips.size(), size_t{1});
    VSM_ASSERT(d.notes[0].id != projet.tracks[0].notes[0].id);
    VSM_ASSERT(d.clips[0].id != projet.tracks[0].clips[0].id);
    VSM_ASSERT_EQ(d.notes[0].number, uint8_t{40});
}

VSM_TEST(duplicating_a_track_keeps_routings_to_groups_that_moved_down) {
    Project projet;
    projet.tracks.resize(3);
    projet.tracks[2].kind = Track::Kind::Group;
    projet.tracks[0].outputGroup = 2;   // la piste 0 va dans le groupe en 2
    projet.tracks[1].outputGroup = 2;
    duplicateTrack(projet, 0);          // le groupe recule en 3
    VSM_ASSERT_EQ(projet.tracks.size(), size_t{4});
    VSM_ASSERT(projet.tracks[3].kind == Track::Kind::Group);
    VSM_ASSERT_EQ(projet.tracks[0].outputGroup, 3);
    VSM_ASSERT_EQ(projet.tracks[1].outputGroup, 3);   // la copie
    VSM_ASSERT_EQ(projet.tracks[2].outputGroup, 3);
}

// --------------------------------------------------------------------------
// D18.2 — ASSEMBLER LES PRISES.
//
// `Track::takes` conserve chaque passe depuis D3.5 et l'on ne pouvait que
// CHOISIR la meilleure : impossible de prendre le couplet de la deuxième et le
// refrain de la quatrième. Or c'est le geste pour lequel on enregistre
// plusieurs passes.
// --------------------------------------------------------------------------

namespace {

/// Trois prises d'une même mesure, chacune sur une hauteur reconnaissable :
/// la prise n joue la note 60+n à chaque temps.
Track pisteATroisPrises() {
    Track piste;
    uint64_t ids = 1;
    for (int p = 0; p < 3; ++p) {
        Take prise;
        prise.name = "Prise " + std::to_string(p + 1);
        prise.startTick = 0;
        prise.endTick = 1920;
        for (int t = 0; t < 4; ++t)
            prise.notes.push_back(Note{480 * t, 480 * t + 240, 0,
                                        static_cast<uint8_t>(60 + p), 100, 64, ids++});
        piste.takes.push_back(std::move(prise));
    }
    return piste;
}

} // namespace

VSM_TEST(three_segments_taken_from_three_takes_give_each_takes_notes_on_its_range) {
    // LE CRITÈRE DE L'ÉTAPE.
    Track piste = pisteATroisPrises();
    uint64_t ids = 1000;
    const std::vector<CompSegment> troncons = {
        {0, 0, 480},        // le premier temps vient de la prise 1
        {1, 480, 1440},     // les deux suivants de la prise 2
        {2, 1440, 1920},    // le dernier de la prise 3
    };
    const auto composite = buildCompositeTake(piste, troncons, ids);

    VSM_ASSERT_EQ(composite.size(), size_t(4));
    VSM_ASSERT_EQ(int(composite[0].number), 60);   // prise 1
    VSM_ASSERT_EQ(int(composite[1].number), 61);   // prise 2
    VSM_ASSERT_EQ(int(composite[2].number), 61);
    VSM_ASSERT_EQ(int(composite[3].number), 62);   // prise 3
    for (size_t i = 0; i < composite.size(); ++i)
        VSM_ASSERT_EQ(composite[i].startTick, vsm::midi::Tick(480 * static_cast<int>(i)));
    // Des identifiants NEUFS, et tous distincts : deux tronçons peuvent venir
    // de la même prise, donc porter deux fois la même note d'origine.
    VSM_ASSERT(composite[0].id != composite[1].id);
}

VSM_TEST(the_active_takes_material_is_read_from_the_track_and_not_from_the_stale_copy) {
    // LE PIÈGE DU MODÈLE : quand `activeTake` désigne une prise, le contenu de
    // `takes[activeTake]` est PÉRIMÉ. Lire la copie rangée rendrait l'état
    // d'AVANT pour la passe qu'on est en train d'écouter — c'est-à-dire
    // exactement celle qu'on vient de juger bonne.
    Track piste = pisteATroisPrises();
    selectTake(piste, 1);                       // la prise 2 devient le matériau courant
    VSM_ASSERT_EQ(piste.activeTake, 1);
    // On l'ÉDITE : la vérité est maintenant dans `piste.notes`, et la copie
    // rangée dans `takes[1]` ne la connaît pas.
    for (auto& note : piste.notes) note.number = 99;

    uint64_t ids = 1000;
    const auto composite = buildCompositeTake(piste, {{1, 0, 1920}}, ids);
    VSM_ASSERT_EQ(composite.size(), size_t(4));
    for (const auto& note : composite) VSM_ASSERT_EQ(int(note.number), 99);
}

VSM_TEST(applying_a_composite_files_the_take_that_was_playing_and_belongs_to_none) {
    Track piste = pisteATroisPrises();
    selectTake(piste, 2);
    for (auto& note : piste.notes) note.velocity = 42;   // une édition qu'on ne veut pas perdre

    uint64_t ids = 1000;
    VSM_ASSERT(applyCompositeTake(piste, {{0, 0, 960}, {2, 960, 1920}}, ids));

    // LA COMPOSITE N'APPARTIENT À AUCUNE PRISE : la dire active écraserait
    // cette prise-là au prochain changement.
    VSM_ASSERT_EQ(piste.activeTake, -1);
    VSM_ASSERT_EQ(piste.notes.size(), size_t(4));
    VSM_ASSERT_EQ(int(piste.notes[0].number), 60);
    VSM_ASSERT_EQ(int(piste.notes[3].number), 62);
    // LA PASSE QU'ON ÉCOUTAIT A ÉTÉ RANGÉE : son édition est dans sa prise.
    VSM_ASSERT_EQ(piste.takes[2].notes.size(), size_t(4));
    for (const auto& note : piste.takes[2].notes) VSM_ASSERT_EQ(int(note.velocity), 42);
}

VSM_TEST(a_note_that_would_overrun_its_segment_is_cut_and_empty_segments_do_nothing) {
    Track piste;
    uint64_t ids = 1;
    Take longue;
    longue.notes.push_back(Note{0, 1920, 0, 60, 100, 64, ids++});   // une ronde
    piste.takes.push_back(std::move(longue));

    uint64_t neufs = 100;
    const auto composite = buildCompositeTake(piste, {{0, 0, 480}}, neufs);
    VSM_ASSERT_EQ(composite.size(), size_t(1));
    // Coupée au bord : laissée entière, elle sonnerait par-dessus le tronçon
    // suivant, qui vient d'une AUTRE passe.
    VSM_ASSERT_EQ(composite[0].endTick, vsm::midi::Tick(480));

    // Un tronçon vide, à l'envers, ou qui désigne une prise inexistante ne
    // fabrique rien -- et `applyCompositeTake` ne touche alors pas la piste.
    Track temoin = piste;
    VSM_ASSERT(buildCompositeTake(piste, {{0, 480, 480}}, neufs).empty());
    VSM_ASSERT(buildCompositeTake(piste, {{0, 960, 480}}, neufs).empty());
    VSM_ASSERT(buildCompositeTake(piste, {{7, 0, 1920}}, neufs).empty());
    VSM_ASSERT(!applyCompositeTake(piste, {{7, 0, 1920}}, neufs));
    VSM_ASSERT_EQ(piste.notes.size(), temoin.notes.size());
}

// D18.7b — LA SOURCE D'UNE SORTIE PUBLIÉE EST UN INDEX, et un index survit mal
// aux remaniements si personne ne s'en occupe. Les trois gestes qui déplacent
// les pistes doivent le faire suivre, exactement comme `outputGroup`.
VSM_TEST(a_published_output_follows_its_source_through_move_duplicate_and_remove) {
    auto projetDeBase = [] {
        Project p;
        for (int i = 0; i < 4; ++i) p.tracks.emplace_back();
        p.tracks[0].name = "Boite a rythmes";
        p.tracks[2].name = "Caisse claire";
        p.tracks[2].outputSourceTrack = 0;
        p.tracks[2].outputIndex = 1;
        return p;
    };

    // DÉPLACER la piste porteuse : la publication la suit.
    {
        Project p = projetDeBase();
        moveTrack(p, 0, 3);                       // la porteuse passe en dernier
        int publieuse = -1;
        for (size_t i = 0; i < p.tracks.size(); ++i)
            if (p.tracks[i].name == "Caisse claire") publieuse = static_cast<int>(i);
        VSM_ASSERT(publieuse >= 0);
        const int source = p.tracks[static_cast<size_t>(publieuse)].outputSourceTrack;
        VSM_ASSERT(source >= 0);
        VSM_ASSERT(p.tracks[static_cast<size_t>(source)].name == "Boite a rythmes");
        VSM_ASSERT_EQ(p.tracks[static_cast<size_t>(publieuse)].outputIndex, 1);
    }

    // DUPLIQUER une piste AVANT la source : les index reculent d'un rang.
    {
        Project p = projetDeBase();
        duplicateTrack(p, 0);                     // insère en 1, la source reste 0
        VSM_ASSERT_EQ(p.tracks[3].outputSourceTrack, 0);
        VSM_ASSERT(p.tracks[3].name == "Caisse claire");
        // La COPIE de la porteuse ne publie rien de nouveau : elle porte la
        // même machine, mais personne ne réclame ses sorties.
        VSM_ASSERT_EQ(p.tracks[1].outputSourceTrack, -1);
    }

    // DUPLIQUER la piste qui publie : les deux copies lisent la même sortie,
    // ce qui est le partage, pas un doublement.
    {
        Project p = projetDeBase();
        duplicateTrack(p, 2);
        VSM_ASSERT_EQ(p.tracks[2].outputSourceTrack, 0);
        VSM_ASSERT_EQ(p.tracks[3].outputSourceTrack, 0);
        VSM_ASSERT_EQ(p.tracks[3].outputIndex, 1);
    }

    // SUPPRIMER la source : la piste cesse de publier plutôt que de pointer
    // dans le vide, et son index part avec.
    {
        Project p = projetDeBase();
        removeTrack(p, 0);
        VSM_ASSERT_EQ(p.tracks[1].outputSourceTrack, -1);
        VSM_ASSERT_EQ(p.tracks[1].outputIndex, 0);
    }

    // SUPPRIMER une piste AVANT la source : l'index recule d'un rang.
    {
        Project p = projetDeBase();
        p.tracks[3].outputSourceTrack = 0;
        p.tracks[3].outputIndex = 2;
        removeTrack(p, 1);
        VSM_ASSERT_EQ(p.tracks[1].outputSourceTrack, 0);
        VSM_ASSERT_EQ(p.tracks[2].outputSourceTrack, 0);
        VSM_ASSERT_EQ(p.tracks[2].outputIndex, 2);
    }
}

// D18.7b — LA COMMANDE QUI PUBLIE, et son idempotence.
VSM_TEST(publishing_outputs_creates_one_track_each_and_never_twice) {
    const std::vector<std::string> noms = {"Grosse caisse", "Caisse claire", "Charley"};
    Project p;
    p.tracks.emplace_back();
    p.tracks[0].name = "TR-808";
    p.tracks.emplace_back();
    p.tracks[1].name = "Basse";
    p.tracks[1].outputGroup = 1;   // se route sur elle-même, peu importe : c'est l'index qui compte

    VSM_ASSERT_EQ(publishInstrumentOutputs(p, 0, noms), size_t(2));
    VSM_ASSERT_EQ(p.tracks.size(), size_t(4));
    // JUSTE APRÈS LA SOURCE, pour que la machine et ses pièces se suivent.
    VSM_ASSERT(p.tracks[1].name == "Caisse claire");
    VSM_ASSERT(p.tracks[2].name == "Charley");
    VSM_ASSERT_EQ(p.tracks[1].outputSourceTrack, 0);
    VSM_ASSERT_EQ(p.tracks[1].outputIndex, 1);
    VSM_ASSERT_EQ(p.tracks[2].outputIndex, 2);
    // LA SORTIE 0 RESTE SUR LA PISTE SOURCE : elle n'est pas republiée.
    VSM_ASSERT_EQ(p.tracks[0].outputSourceTrack, -1);
    // L'index de routage de la piste repoussée a suivi.
    VSM_ASSERT(p.tracks[3].name == "Basse");
    VSM_ASSERT_EQ(p.tracks[3].outputGroup, 3);

    // IDEMPOTENTE : rejouer la commande ne crée rien et ne double aucune pièce.
    VSM_ASSERT_EQ(publishInstrumentOutputs(p, 0, noms), size_t(0));
    VSM_ASSERT_EQ(p.tracks.size(), size_t(4));

    // Une sortie retirée à la main se republie, et elle seule.
    removeTrack(p, 1);
    VSM_ASSERT_EQ(publishInstrumentOutputs(p, 0, noms), size_t(1));
    VSM_ASSERT_EQ(p.tracks.size(), size_t(4));
    VSM_ASSERT_EQ(p.tracks[1].outputIndex, 1);

    // Une machine à UNE seule sortie n'a rien à publier.
    Project mono;
    mono.tracks.emplace_back();
    VSM_ASSERT_EQ(publishInstrumentOutputs(mono, 0, {"Sortie"}), size_t(0));
    VSM_ASSERT_EQ(mono.tracks.size(), size_t(1));
}

// D19.3 — ÉCLATER UNE PISTE PAR HAUTEUR : le pendant manuel de la parité.
VSM_TEST(exploding_a_track_by_pitch_splits_the_material_without_duplicating_a_note) {
    Project p;
    p.tracks.emplace_back();
    p.tracks[0].name = "Batterie";
    p.tracks[0].instrumentId = "vsm.tr808";
    uint64_t compteur = 1;
    // Trois hauteurs, entrelacées dans le temps : 36, 38, 42.
    const int hauteurs[6] = {36, 42, 38, 36, 42, 38};
    for (int i = 0; i < 6; ++i)
        p.tracks[0].addNote(static_cast<Tick>(i * 120), static_cast<Tick>(i * 120 + 60),
                             static_cast<uint8_t>(hauteurs[i]), 100, 0, compteur);
    // Une piste APRÈS, qui se route sur un groupe situé plus loin : son index
    // doit suivre l'insertion.
    p.tracks.emplace_back();
    p.tracks[1].name = "Basse";
    p.tracks[1].outputGroup = 1;

    const size_t avant = p.tracks[0].notes.size();
    VSM_ASSERT_EQ(explodeTrackByPitch(p, 0), size_t(2));   // 3 hauteurs -> 2 pistes neuves
    VSM_ASSERT_EQ(p.tracks.size(), size_t(4));

    // LA RÉUNION EST EXACTEMENT LE MATÉRIAU DE DÉPART : aucune note perdue,
    // aucune dupliquée. C'est le critère de l'étape.
    std::set<uint64_t> reunion;
    size_t total = 0;
    for (size_t t = 0; t < 3; ++t) {
        total += p.tracks[t].notes.size();
        for (const auto& n : p.tracks[t].notes) reunion.insert(n.id);
    }
    VSM_ASSERT_EQ(total, avant);
    VSM_ASSERT_EQ(reunion.size(), avant);

    // CHAQUE PISTE NE PORTE QU'UNE HAUTEUR.
    for (size_t t = 0; t < 3; ++t) {
        VSM_ASSERT(!p.tracks[t].notes.empty());
        const uint8_t h = p.tracks[t].notes.front().number;
        for (const auto& n : p.tracks[t].notes) VSM_ASSERT_EQ(static_cast<int>(n.number), static_cast<int>(h));
    }
    // LA PLUS GRAVE RESTE SUR LA PISTE D'ORIGINE, les autres suivent en ordre.
    VSM_ASSERT_EQ(static_cast<int>(p.tracks[0].notes.front().number), 36);
    VSM_ASSERT_EQ(static_cast<int>(p.tracks[1].notes.front().number), 38);
    VSM_ASSERT_EQ(static_cast<int>(p.tracks[2].notes.front().number), 42);
    // L'instrument est recopié : chaque pièce se joue avec la même machine.
    VSM_ASSERT(p.tracks[1].instrumentId == "vsm.tr808");
    // L'index de routage de la piste repoussée a suivi.
    VSM_ASSERT(p.tracks[3].name == "Basse");
    VSM_ASSERT_EQ(p.tracks[3].outputGroup, 3);
}

VSM_TEST(exploding_a_track_that_has_nothing_to_split_creates_no_empty_track) {
    Project p;
    p.tracks.emplace_back();
    uint64_t compteur = 1;
    p.tracks[0].addNote(0, 60, 36, 100, 0, compteur);
    p.tracks[0].addNote(120, 180, 36, 100, 0, compteur);
    // UNE SEULE HAUTEUR : la commande n'a pas échoué, elle n'avait rien à
    // faire — et elle le dit en rendant zéro plutôt qu'en posant une piste
    // vide dont on ne saurait plus si elle a servi.
    VSM_ASSERT_EQ(explodeTrackByPitch(p, 0), size_t(0));
    VSM_ASSERT_EQ(p.tracks.size(), size_t(1));
    VSM_ASSERT_EQ(p.tracks[0].notes.size(), size_t(2));

    Project vide;
    vide.tracks.emplace_back();
    VSM_ASSERT_EQ(explodeTrackByPitch(vide, 0), size_t(0));
    VSM_ASSERT_EQ(vide.tracks.size(), size_t(1));
}

VSM_TEST(exploding_names_each_track_from_the_machine_when_it_can) {
    Project p;
    p.tracks.emplace_back();
    p.tracks[0].name = "Batterie";
    uint64_t compteur = 1;
    p.tracks[0].addNote(0, 60, 36, 100, 0, compteur);
    p.tracks[0].addNote(120, 180, 38, 100, 0, compteur);
    // Le nom vient de L'APPELANT : core/ ne connaît pas les machines.
    auto nommer = [](uint8_t note) -> std::string {
        return note == 36 ? "Grosse caisse" : (note == 38 ? "Caisse claire" : std::string());
    };
    VSM_ASSERT_EQ(explodeTrackByPitch(p, 0, nommer), size_t(1));
    VSM_ASSERT(p.tracks[0].name == "Grosse caisse");
    VSM_ASSERT(p.tracks[1].name == "Caisse claire");

    // Sans nommeur, la piste prend le nom de la note plutôt qu'un numéro nu.
    Project q;
    q.tracks.emplace_back();
    q.tracks[0].name = "Perc";
    uint64_t c2 = 1;
    q.tracks[0].addNote(0, 60, 36, 100, 0, c2);
    q.tracks[0].addNote(120, 180, 38, 100, 0, c2);
    VSM_ASSERT_EQ(explodeTrackByPitch(q, 0), size_t(1));
    VSM_ASSERT(q.tracks[1].name.find("Perc - ") == 0);
}

VSM_TEST(exploding_gives_every_piece_the_same_clips_with_fresh_ids) {
    // UN CLIP EST UNE FENÊTRE, pas un conteneur : la découpe de la piste vaut
    // pour chacune de ses pièces. L'oublier ferait sonner les pièces éclatées
    // là où l'originale se taisait.
    Project p;
    p.tracks.emplace_back();
    uint64_t compteur = 1;
    p.tracks[0].addNote(0, 60, 36, 100, 0, compteur);
    p.tracks[0].addNote(120, 180, 38, 100, 0, compteur);
    Clip c;
    c.startTick = 0;
    c.length = 480;
    c.id = p.nextClipId();
    p.tracks[0].clips.push_back(c);

    VSM_ASSERT_EQ(explodeTrackByPitch(p, 0), size_t(1));
    VSM_ASSERT_EQ(p.tracks[1].clips.size(), size_t(1));
    VSM_ASSERT_EQ(p.tracks[1].clips[0].startTick, Tick(0));
    VSM_ASSERT_EQ(p.tracks[1].clips[0].length, Tick(480));
    // DES IDENTIFIANTS NEUFS : deux clips qui partagent un id feraient agir
    // toute sélection sur les deux.
    VSM_ASSERT(p.tracks[1].clips[0].id != p.tracks[0].clips[0].id);
}

// D19.4 — LES PISTES DOSSIER : un rangement, pas un bus.
namespace {
/// Un projet : Dossier(0) [ Kick(1), Snare(2), SousDossier(3) [ HatA(4) ] ], Basse(5)
Project projetAvecDossiers() {
    Project p;
    for (int i = 0; i < 6; ++i) p.tracks.emplace_back();
    p.tracks[0].kind = Track::Kind::Folder; p.tracks[0].name = "Batterie"; p.tracks[0].folderDepth = 0;
    p.tracks[1].name = "Kick";        p.tracks[1].folderDepth = 1;
    p.tracks[2].name = "Snare";       p.tracks[2].folderDepth = 1;
    p.tracks[3].kind = Track::Kind::Folder; p.tracks[3].name = "Charleys"; p.tracks[3].folderDepth = 1;
    p.tracks[4].name = "HatA";        p.tracks[4].folderDepth = 2;
    p.tracks[5].name = "Basse";       p.tracks[5].folderDepth = 0;
    return p;
}
}

VSM_TEST(a_folder_contains_what_follows_it_until_the_depth_comes_back) {
    Project p = projetAvecDossiers();
    const auto batterie = folderContents(p, 0);
    // Kick, Snare, Charleys ET HatA : le contenu inclut les sous-dossiers.
    VSM_ASSERT_EQ(batterie.size(), size_t(4));
    VSM_ASSERT_EQ(batterie.front(), size_t(1));
    VSM_ASSERT_EQ(batterie.back(), size_t(4));
    // La Basse est DEHORS : sa profondeur ramène à la racine.
    for (size_t t : batterie) VSM_ASSERT(t != 5);

    const auto charleys = folderContents(p, 3);
    VSM_ASSERT_EQ(charleys.size(), size_t(1));
    VSM_ASSERT_EQ(charleys.front(), size_t(4));

    // Une piste qui n'est pas un dossier ne contient rien.
    VSM_ASSERT(folderContents(p, 1).empty());
    VSM_ASSERT(folderContents(p, 99).empty());
}

VSM_TEST(collapsing_a_folder_hides_everything_it_holds_including_nested_ones) {
    Project p = projetAvecDossiers();
    // Déplié : rien n'est caché.
    for (size_t t = 0; t < p.tracks.size(); ++t)
        VSM_ASSERT(!hiddenByCollapsedFolder(p, t));

    p.tracks[0].folded = true;
    VSM_ASSERT(!hiddenByCollapsedFolder(p, 0));   // le dossier lui-même reste visible
    VSM_ASSERT(hiddenByCollapsedFolder(p, 1));
    VSM_ASSERT(hiddenByCollapsedFolder(p, 2));
    VSM_ASSERT(hiddenByCollapsedFolder(p, 3));
    // UN SOUS-DOSSIER DÉPLIÉ DANS UN DOSSIER REPLIÉ RESTE CACHÉ : un tiroir
    // fermé ne laisse pas dépasser ce qu'il contient.
    VSM_ASSERT(!p.tracks[3].folded);
    VSM_ASSERT(hiddenByCollapsedFolder(p, 4));
    // La Basse est dehors, elle ne bouge pas.
    VSM_ASSERT(!hiddenByCollapsedFolder(p, 5));
}

VSM_TEST(collapsing_only_the_inner_folder_leaves_its_siblings_alone) {
    Project p = projetAvecDossiers();
    p.tracks[3].folded = true;
    VSM_ASSERT(!hiddenByCollapsedFolder(p, 1));   // Kick
    VSM_ASSERT(!hiddenByCollapsedFolder(p, 2));   // Snare
    VSM_ASSERT(!hiddenByCollapsedFolder(p, 3));   // le sous-dossier lui-même
    VSM_ASSERT(hiddenByCollapsedFolder(p, 4));    // HatA, dedans
    VSM_ASSERT(!hiddenByCollapsedFolder(p, 5));   // Basse
}

VSM_TEST(folder_depths_are_put_back_in_order_rather_than_left_incoherent) {
    Project p;
    for (int i = 0; i < 4; ++i) p.tracks.emplace_back();
    // Une première piste prétendument profonde : impossible, il n'y a rien
    // au-dessus d'elle.
    p.tracks[0].folderDepth = 3;
    // Une piste rangée dans une piste ORDINAIRE : elle prétendrait être
    // contenue par quelque chose qui ne contient rien.
    p.tracks[1].folderDepth = 1;
    p.tracks[2].kind = Track::Kind::Folder;
    p.tracks[3].folderDepth = 5;   // un seul cran est permis après un dossier

    VSM_ASSERT(normalizeFolderDepths(p) > 0);
    VSM_ASSERT_EQ(p.tracks[0].folderDepth, 0);
    VSM_ASSERT_EQ(p.tracks[1].folderDepth, 0);
    VSM_ASSERT_EQ(p.tracks[2].folderDepth, 0);
    VSM_ASSERT_EQ(p.tracks[3].folderDepth, 1);   // dans le dossier, un cran

    // IDEMPOTENTE : un arbre déjà d'aplomb ne bouge plus.
    VSM_ASSERT_EQ(normalizeFolderDepths(p), size_t(0));

    // Et un arbre légitime traverse sans une correction.
    Project q = projetAvecDossiers();
    VSM_ASSERT_EQ(normalizeFolderDepths(q), size_t(0));
}
