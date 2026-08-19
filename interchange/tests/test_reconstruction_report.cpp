#include "TestFramework.h"
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/interchange/ReconstructionReport.h"
#include <filesystem>
#include <string>

using namespace vsm::interchange;
namespace fs = std::filesystem;

namespace {

fs::path writeReport(const std::string& name, const std::string& content) {
    const fs::path folder = fs::temp_directory_path() / ("vsm-rapport-" + name);
    fs::remove_all(folder);
    fs::create_directories(folder);
    const fs::path path = folder / "rapport.json";
    std::string error;
    writeTextFile(path.string(), content, error);
    return path;
}

/// Projet de deux notes sur une piste nommée « Basse », à 120 BPM :
/// une noire vaut 0,5 s, donc le tick 480 tombe à 0,5 s.
vsm::sequencer::Project makeProject() {
    vsm::sequencer::Project project;
    vsm::sequencer::Track track;
    track.name = "Basse";
    for (int i = 0; i < 2; ++i) {
        vsm::sequencer::Note note;
        note.startTick = i * 480;
        note.endTick = i * 480 + 240;
        note.number = static_cast<uint8_t>(45 + i);
        track.notes.push_back(note);
    }
    project.tracks.push_back(track);
    return project;
}

} // namespace

VSM_TEST(a_missing_report_fails_explicitly) {
    // Un projet s'ouvre très bien sans rapport -- mais on ne fait pas semblant
    // d'en avoir un.
    const auto result = loadReconstructionReport("/ce/fichier/n/existe/pas.json");
    VSM_ASSERT(!result.success);
    VSM_ASSERT(!result.error.empty());
}

VSM_TEST(a_report_of_another_format_is_refused) {
    // Lire « au mieux » produirait des confiances inventées, donc des notes
    // signalées au hasard : pire que pas de signalement du tout.
    const auto path = writeReport("mauvais", R"({"format":"autre-chose","stems":[]})");
    const auto result = loadReconstructionReport(path.string());
    VSM_ASSERT(!result.success);
    VSM_ASSERT(result.error.find("format") != std::string::npos);
    fs::remove_all(path.parent_path());
}

VSM_TEST(a_report_carries_its_metric_and_per_note_confidence) {
    const auto path = writeReport("lecture", R"({
        "format": "vsm-reconstruction-report",
        "version": 1,
        "metric": "v2",
        "globalDistance": 0.42,
        "stems": [{
            "name": "Basse",
            "machine": "vsm.sh101",
            "distance": 0.13,
            "noteConfidence": [
                {"note": 45, "start": 0.0, "confidence": 0.95},
                {"note": 46, "start": 0.5, "confidence": 0.21}
            ]
        }]
    })");
    const auto result = loadReconstructionReport(path.string());
    VSM_ASSERT(result.success);
    VSM_ASSERT_EQ(result.report.metric, std::string("v2"));
    VSM_ASSERT_NEAR(result.report.globalDistance, 0.42, 1e-6);
    VSM_ASSERT_EQ(result.report.stems.size(), size_t(1));
    VSM_ASSERT_EQ(result.report.stems[0].notes.size(), size_t(2));
    VSM_ASSERT(result.report.findStem("Basse") != nullptr);
    VSM_ASSERT(result.report.findStem("Inconnue") == nullptr);
    fs::remove_all(path.parent_path());
}

VSM_TEST(an_old_report_without_a_metric_field_is_read_as_v1) {
    // Les rapports écrits avant l'étape 10.3 n'ont pas de champ « metric ».
    // Les lire comme v2 laisserait comparer des distances qui ne se comparent
    // pas.
    const auto path = writeReport("ancien",
        R"({"format":"vsm-reconstruction-report","version":1,"stems":[]})");
    const auto result = loadReconstructionReport(path.string());
    VSM_ASSERT(result.success);
    VSM_ASSERT_EQ(result.report.metric, std::string("v1"));
    fs::remove_all(path.parent_path());
}

VSM_TEST(confidences_are_matched_by_pitch_and_time_not_by_position) {
    // L'appariement par INDICE serait plus simple et faux : il suffirait
    // qu'une note soit ajoutée ou déplacée dans l'éditeur pour que toutes les
    // confiances suivantes désignent la mauvaise note, sans rien signaler.
    // Ici, le rapport donne les notes DANS L'ORDRE INVERSE de la piste.
    const auto path = writeReport("appariement", R"({
        "format": "vsm-reconstruction-report",
        "version": 1,
        "stems": [{
            "name": "Basse",
            "noteConfidence": [
                {"note": 46, "start": 0.5, "confidence": 0.20},
                {"note": 45, "start": 0.0, "confidence": 0.90}
            ]
        }]
    })");
    const auto result = loadReconstructionReport(path.string());
    VSM_ASSERT(result.success);

    auto project = makeProject();
    VSM_ASSERT_EQ(applyNoteConfidences(result.report, project), size_t(2));
    VSM_ASSERT_NEAR(project.tracks[0].notes[0].confidence, 0.90f, 1e-5); // note 45 à 0,0 s
    VSM_ASSERT_NEAR(project.tracks[0].notes[1].confidence, 0.20f, 1e-5); // note 46 à 0,5 s
    fs::remove_all(path.parent_path());
}

VSM_TEST(a_note_the_report_does_not_mention_keeps_full_confidence) {
    // Une note saisie à la main n'est pas douteuse : ne rien dire d'elle doit
    // la laisser franche, jamais la rendre suspecte par défaut.
    const auto path = writeReport("partiel", R"({
        "format": "vsm-reconstruction-report",
        "version": 1,
        "stems": [{"name":"Basse","noteConfidence":[{"note":45,"start":0.0,"confidence":0.3}]}]
    })");
    const auto result = loadReconstructionReport(path.string());
    auto project = makeProject();
    VSM_ASSERT_EQ(applyNoteConfidences(result.report, project), size_t(1));
    VSM_ASSERT_NEAR(project.tracks[0].notes[0].confidence, 0.3f, 1e-5);
    VSM_ASSERT_NEAR(project.tracks[0].notes[1].confidence, 1.0f, 1e-5); // intacte
    fs::remove_all(path.parent_path());
}

VSM_TEST(a_report_for_another_track_name_changes_nothing) {
    const auto path = writeReport("autrepiste", R"({
        "format": "vsm-reconstruction-report",
        "version": 1,
        "stems": [{"name":"Nappe","noteConfidence":[{"note":45,"start":0.0,"confidence":0.1}]}]
    })");
    const auto result = loadReconstructionReport(path.string());
    auto project = makeProject();
    VSM_ASSERT_EQ(applyNoteConfidences(result.report, project), size_t(0));
    for (const auto& note : project.tracks[0].notes)
        VSM_ASSERT_NEAR(note.confidence, 1.0f, 1e-5);
    fs::remove_all(path.parent_path());
}

VSM_TEST(the_closest_candidate_wins_on_a_repeated_note) {
    // Sur une note répétée rapidement, le premier candidat dans la tolérance
    // n'est pas forcément le bon : on prend le PLUS PROCHE.
    const auto path = writeReport("repetee", R"({
        "format": "vsm-reconstruction-report",
        "version": 1,
        "stems": [{"name":"Basse","noteConfidence":[
            {"note":45,"start":0.02,"confidence":0.4},
            {"note":45,"start":0.001,"confidence":0.8}
        ]}]
    })");
    const auto result = loadReconstructionReport(path.string());
    auto project = makeProject();
    applyNoteConfidences(result.report, project);
    VSM_ASSERT_NEAR(project.tracks[0].notes[0].confidence, 0.8f, 1e-5);
    fs::remove_all(path.parent_path());
}
