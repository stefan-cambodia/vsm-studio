#include "TestFramework.h"
#include "vsm/interchange/RecoverySession.h"
#include <string>

using namespace vsm::interchange;

// D10.4 de docs/ROADMAP-daw.md — SAUVEGARDE AUTOMATIQUE ET RÉCUPÉRATION.
//
// La sauvegarde automatique n'écrit PAS les médias : recopier une prise de deux
// cents mégaoctets toutes les trente secondes ferait d'elle la panne dont elle
// devait protéger. Les chemins de médias restent donc relatifs au dossier
// d'ORIGINE du projet, et c'est cet enregistrement-ci qui s'en souvient. Sans
// lui, un projet récupéré rouvrirait avec toutes ses pistes audio muettes, et
// rien n'expliquerait pourquoi.

VSM_TEST(a_recovery_record_remembers_where_the_media_live) {
    RecoveryRecord ecrit;
    ecrit.originalFolder = "/musique/projets/Sky and Sand";
    ecrit.title = "Sky and Sand";
    ecrit.savedAtEpochSeconds = 1756512000;
    ecrit.trackCount = 12;
    ecrit.noteCount = 4821;

    RecoveryRecord relu;
    VSM_ASSERT(recoveryRecordFromJson(recoveryRecordToJson(ecrit), relu));
    VSM_ASSERT_EQ(relu.originalFolder, ecrit.originalFolder);
    VSM_ASSERT_EQ(relu.title, ecrit.title);
    VSM_ASSERT_EQ(relu.savedAtEpochSeconds, ecrit.savedAtEpochSeconds);
    VSM_ASSERT_EQ(relu.trackCount, 12);
    VSM_ASSERT_EQ(relu.noteCount, 4821);
}

VSM_TEST(a_project_never_saved_has_no_original_folder_and_that_is_valid) {
    // C'EST LE CAS QUI COMPTE LE PLUS. Un projet déjà enregistré, on en perd au
    // pire une minute ; un projet jamais enregistré, on le perd en entier. Un
    // dossier d'origine vide doit donc être une valeur normale, pas une
    // anomalie qui ferait écarter l'enregistrement.
    RecoveryRecord ecrit;
    ecrit.title = "";
    ecrit.trackCount = 3;

    RecoveryRecord relu;
    VSM_ASSERT(recoveryRecordFromJson(recoveryRecordToJson(ecrit), relu));
    VSM_ASSERT(relu.originalFolder.empty());
    VSM_ASSERT_EQ(relu.trackCount, 3);
}

VSM_TEST(an_unreadable_record_is_ignored_rather_than_half_understood) {
    // Proposer de récupérer une session puis échouer à l'ouvrir serait la
    // deuxième mauvaise nouvelle d'affilée. Un enregistrement qu'on ne comprend
    // pas ne se propose pas.
    RecoveryRecord relu;
    VSM_ASSERT(!recoveryRecordFromJson("", relu));
    VSM_ASSERT(!recoveryRecordFromJson("{pas du JSON", relu));
    // Bon JSON, mauvais format : écarté aussi.
    VSM_ASSERT(!recoveryRecordFromJson(R"({"format":"autre.chose","title":"x"})", relu));
}
