#include "TestFramework.h"
#include "vsm/interchange/ProjectDocument.h"
#include <cmath>

using namespace vsm::interchange;
using vsm::sequencer::Project;
using vsm::sequencer::Track;

namespace {

/// Projet de travail : deux pistes, un changement de tempo, une mesure en 3/4.
Project buildProject() {
    Project project;
    project.title = "Morceau de test";
    project.ticksPerQuarterNote = 480;
    project.tempoMap.addTempoChange(0, 500000);      // 120 BPM
    project.tempoMap.addTempoChange(1920, 400000);   // 150 BPM à la mesure 2
    project.timeSignatureMap.addChange(0, 4, 2);     // 4/4
    project.timeSignatureMap.addChange(1920, 3, 2);  // 3/4

    uint64_t ids = 1;
    Track bass;
    bass.name = "Basse";
    bass.channel = 1;
    bass.colorRgba = 0xFF3366CCu;
    bass.instrumentId = "vsm.tb303";
    bass.volume = 0.8f;
    bass.pan = -0.25f;
    bass.sendLevels = {0.3f, 0.1f};
    bass.addNote(0, 480, 36, 100, 1, ids);
    project.tracks.push_back(bass);

    Track pad;
    pad.name = "Nappe";
    pad.channel = 2;
    pad.instrumentId = "vsm.juno106";
    pad.muted = true;
    pad.addNote(0, 1920, 60, 80, 2, ids);
    project.tracks.push_back(pad);
    return project;
}

} // namespace

VSM_TEST(project_document_captures_transport_and_tracks) {
    const ProjectDocument document = documentFromProject(buildProject());
    VSM_ASSERT_EQ(document.title, std::string("Morceau de test"));
    VSM_ASSERT_EQ(document.transport.ticksPerQuarterNote, 480);
    VSM_ASSERT_EQ(document.transport.tempoChanges.size(), size_t{2});
    VSM_ASSERT_NEAR(document.transport.tempoChanges[0].bpm, 120.0, 0.001);
    VSM_ASSERT_NEAR(document.transport.tempoChanges[1].bpm, 150.0, 0.001);
    VSM_ASSERT_EQ(document.transport.timeSignatures.size(), size_t{2});
    VSM_ASSERT_EQ(document.transport.timeSignatures[1].numerator, 3);
    VSM_ASSERT_EQ(document.transport.timeSignatures[1].denominator, 4);
    VSM_ASSERT_EQ(document.tracks.size(), size_t{2});
    VSM_ASSERT_EQ(document.tracks[0].preferredPlugin, std::string("vsm.tb303"));
}

VSM_TEST(project_document_does_not_duplicate_the_notes) {
    // Les notes ont déjà un format universel (.mid) : les recopier en JSON
    // créerait deux vérités qui divergeraient, sans qu'on sache laquelle croire.
    const std::string text = projectDocumentToJson(documentFromProject(buildProject())).toString();
    VSM_ASSERT(text.find("\"midi\"") != std::string::npos);
    VSM_ASSERT(text.find("arrangement.mid") != std::string::npos);
    VSM_ASSERT(text.find("\"notes\"") == std::string::npos);
    VSM_ASSERT(text.find("\"startTick\"") == std::string::npos ||
                text.find("\"loop\"") != std::string::npos); // startTick n'existe que pour la boucle
}

VSM_TEST(project_document_round_trips_through_json) {
    const ProjectDocument original = documentFromProject(buildProject());
    const ProjectLoadResult loaded = parseProjectDocument(projectDocumentToJson(original).toString());
    VSM_ASSERT(loaded.success);

    const ProjectDocument& copy = loaded.document;
    VSM_ASSERT_EQ(copy.title, original.title);
    VSM_ASSERT_EQ(copy.midiPath, original.midiPath);
    VSM_ASSERT_EQ(copy.transport.ticksPerQuarterNote, original.transport.ticksPerQuarterNote);
    VSM_ASSERT_EQ(copy.transport.tempoChanges.size(), original.transport.tempoChanges.size());
    VSM_ASSERT_NEAR(copy.transport.tempoChanges[1].bpm, original.transport.tempoChanges[1].bpm, 0.001);
    VSM_ASSERT_EQ(copy.tracks.size(), original.tracks.size());
    for (size_t i = 0; i < copy.tracks.size(); ++i) {
        VSM_ASSERT_EQ(copy.tracks[i].name, original.tracks[i].name);
        VSM_ASSERT_EQ(copy.tracks[i].channel, original.tracks[i].channel);
        VSM_ASSERT_EQ(copy.tracks[i].preferredPlugin, original.tracks[i].preferredPlugin);
        VSM_ASSERT_EQ(copy.tracks[i].colorRgba, original.tracks[i].colorRgba);
        VSM_ASSERT_NEAR(copy.tracks[i].volume, original.tracks[i].volume, 1e-6);
        VSM_ASSERT_NEAR(copy.tracks[i].pan, original.tracks[i].pan, 1e-6);
        VSM_ASSERT_EQ(copy.tracks[i].muted, original.tracks[i].muted);
        VSM_ASSERT_NEAR(copy.tracks[i].sendLevels[0], original.tracks[i].sendLevels[0], 1e-6);
    }
}

VSM_TEST(applying_a_document_restores_transport_and_mix) {
    // Le vrai parcours d'import : le MIDI apporte les notes et rien d'autre,
    // le project.json rétablit tout le contexte.
    const ProjectDocument document = documentFromProject(buildProject());

    Project fromMidi; // ce que donnerait la lecture d'un .mid : notes + noms bruts
    fromMidi.ticksPerQuarterNote = 480;
    uint64_t ids = 1;
    Track a, b;
    a.addNote(0, 480, 36, 100, 0, ids);
    b.addNote(0, 1920, 60, 80, 0, ids);
    fromMidi.tracks.push_back(a);
    fromMidi.tracks.push_back(b);

    const ImportReport report = applyDocumentToProject(document, fromMidi);
    VSM_ASSERT_EQ(report.missingInstruments.size(), size_t{0});
    VSM_ASSERT_EQ(fromMidi.title, std::string("Morceau de test"));
    VSM_ASSERT_EQ(fromMidi.tracks[0].name, std::string("Basse"));
    VSM_ASSERT_EQ(fromMidi.tracks[0].instrumentId, std::string("vsm.tb303"));
    VSM_ASSERT_NEAR(fromMidi.tracks[0].volume, 0.8f, 1e-6);
    VSM_ASSERT_NEAR(fromMidi.tracks[0].pan, -0.25f, 1e-6);
    VSM_ASSERT(fromMidi.tracks[1].muted);
    VSM_ASSERT_NEAR(fromMidi.tempoMap.bpmAt(1920), 150.0, 0.01);
    VSM_ASSERT_EQ(static_cast<int>(fromMidi.timeSignatureMap.numeratorAt(1920)), 3);
    // Les notes du MIDI n'ont pas été touchées.
    VSM_ASSERT_EQ(fromMidi.tracks[0].notes.size(), size_t{1});
}

VSM_TEST(missing_instrument_is_reported_and_never_substituted) {
    // Règle du § 5 de la roadmap : jamais de reconstruction silencieuse fausse.
    ProjectDocument document;
    ProjectTrack track;
    track.name = "Lead";
    track.preferredPlugin = "com.autre.editeur.super-synth";
    document.tracks.push_back(track);

    Project project;
    project.tracks.push_back(Track{});
    const ImportReport report = applyDocumentToProject(document, project);

    VSM_ASSERT_EQ(report.missingInstruments.size(), size_t{1});
    VSM_ASSERT_EQ(report.missingInstruments[0], std::string("com.autre.editeur.super-synth"));
    VSM_ASSERT(report.hasWarnings());
    VSM_ASSERT(report.summary().find("Instrument manquant") != std::string::npos);
    // Rien n'a été substitué : la piste reste sans instrument, et son nom
    // comme ses notes sont intacts -- l'utilisateur peut installer la machine
    // et rouvrir le projet.
    VSM_ASSERT(project.tracks[0].instrumentId.empty());
    VSM_ASSERT_EQ(project.tracks[0].name, std::string("Lead"));
}

VSM_TEST(track_count_mismatch_is_reported_not_hidden) {
    ProjectDocument document = documentFromProject(buildProject()); // 2 pistes
    Project project;
    project.tracks.push_back(Track{}); // le MIDI n'en a qu'une

    const ImportReport report = applyDocumentToProject(document, project);
    VSM_ASSERT_EQ(report.tracksInDocument, size_t{2});
    VSM_ASSERT_EQ(report.tracksInProject, size_t{1});
    VSM_ASSERT_EQ(report.warnings.size(), size_t{1});
    VSM_ASSERT_EQ(project.tracks[0].name, std::string("Basse")); // la piste commune est bien configurée
}

VSM_TEST(non_portable_paths_are_refused) {
    // Un projet doit s'ouvrir sur une autre machine : accepter un chemin
    // absolu reviendrait à déplacer le problème chez quelqu'un d'autre.
    VSM_ASSERT(isPortableRelativePath("midi/arrangement.mid"));
    VSM_ASSERT(isPortableRelativePath("instruments/track_00.synth.json"));
    VSM_ASSERT(isPortableRelativePath(""));
    VSM_ASSERT(!isPortableRelativePath("/home/moi/projet/midi/a.mid"));
    VSM_ASSERT(!isPortableRelativePath("C:\\Projets\\a.mid"));
    VSM_ASSERT(!isPortableRelativePath("midi\\arrangement.mid"));
    VSM_ASSERT(!isPortableRelativePath("../../etc/passwd"));
    VSM_ASSERT(!isPortableRelativePath("midi/../../secret"));

    const auto absolute = parseProjectDocument(R"({"format":"vsm-project","version":1,
        "midi":{"file":"/home/moi/a.mid"},"tracks":[]})");
    VSM_ASSERT(!absolute.success);
    VSM_ASSERT(absolute.error.find("non portable") != std::string::npos);
}

VSM_TEST(project_refuses_unknown_format_or_version) {
    const auto wrongFormat = parseProjectDocument(R"({"format":"autre","version":1,"tracks":[]})");
    VSM_ASSERT(!wrongFormat.success);
    VSM_ASSERT(wrongFormat.error.find("format") != std::string::npos);

    const auto futureVersion = parseProjectDocument(R"({"format":"vsm-project","version":42,"tracks":[]})");
    VSM_ASSERT(!futureVersion.success);
    VSM_ASSERT(futureVersion.error.find("version") != std::string::npos);
}

VSM_TEST(a_project_written_by_hand_is_accepted) {
    // Le cas d'usage de la Phase 7 : un script Python écrit ce fichier sans
    // rien connaître du code du DAW.
    const char* handWritten = R"({
        "format": "vsm-project",
        "version": 1,
        "title": "Reconstruction",
        "midi": { "file": "midi/arrangement.mid" },
        "transport": {
            "ticksPerQuarterNote": 960,
            "tempoChanges": [ { "tick": 0, "bpm": 128 } ],
            "timeSignatures": [ { "tick": 0, "numerator": 7, "denominator": 8 } ]
        },
        "tracks": [
            { "name": "Acid", "channel": 0,
              "instrument": { "preferredPlugin": "vsm.tb303", "preset": "instruments/track_00.synth.json" },
              "mix": { "volume": 0.9, "pan": 0.1, "sends": [0.2, 0] } }
        ]
    })";
    const auto loaded = parseProjectDocument(handWritten);
    VSM_ASSERT(loaded.success);
    VSM_ASSERT_EQ(loaded.document.transport.ticksPerQuarterNote, 960);
    VSM_ASSERT_NEAR(loaded.document.transport.tempoChanges[0].bpm, 128.0, 0.001);
    VSM_ASSERT_EQ(loaded.document.transport.timeSignatures[0].denominator, 8);
    VSM_ASSERT_EQ(loaded.document.tracks.size(), size_t{1});
    VSM_ASSERT_EQ(loaded.document.tracks[0].presetPath, std::string("instruments/track_00.synth.json"));

    Project project;
    project.tracks.push_back(Track{});
    const ImportReport report = applyDocumentToProject(loaded.document, project);
    VSM_ASSERT_EQ(report.missingInstruments.size(), size_t{0});
    VSM_ASSERT_EQ(project.ticksPerQuarterNote, 960);
    VSM_ASSERT_NEAR(project.tempoMap.bpmAt(0), 128.0, 0.01);
    VSM_ASSERT_EQ(static_cast<int>(project.timeSignatureMap.numeratorAt(0)), 7);
    VSM_ASSERT_EQ(static_cast<int>(project.timeSignatureMap.denominatorAt(0)), 8);
}

VSM_TEST(bpm_survives_the_round_trip_through_microseconds) {
    // Le fichier parle en BPM (lisible), le moteur en microsecondes par noire.
    // La conversion aller-retour ne doit pas décaler le tempo.
    for (double bpm : {60.0, 90.0, 120.0, 128.0, 140.0, 174.0, 200.0}) {
        Project project;
        project.tempoMap.clearTempoChanges();
        project.tempoMap.addTempoChange(0, static_cast<uint32_t>(std::llround(60000000.0 / bpm)));

        const ProjectDocument document = documentFromProject(project);
        Project restored;
        restored.tracks.push_back(Track{});
        applyDocumentToProject(document, restored);
        VSM_ASSERT_NEAR(restored.tempoMap.bpmAt(0), bpm, 0.001);
    }
}

VSM_TEST(tempo_is_written_in_its_simplest_exact_form) {
    // Le moteur stocke des microsecondes par noire : 130 BPM y devient 461538,
    // qui se reconvertit en 130,00014. Écrire ce nombre-là donnerait un fichier
    // illisible ET l'impression fausse d'une dérive de tempo. Le fichier doit
    // afficher "130" -- tout en désignant exactement le même tempo interne.
    Project project;
    project.tempoMap.clearTempoChanges();
    project.tempoMap.addTempoChange(0, 461538); // ce que produit "130 BPM"
    const std::string text = projectDocumentToJson(documentFromProject(project)).toString(-1);
    VSM_ASSERT(text.find("\"bpm\":130") != std::string::npos);

    const auto loaded = parseProjectDocument(text);
    VSM_ASSERT(loaded.success);
    Project restored;
    restored.tracks.push_back(Track{});
    applyDocumentToProject(loaded.document, restored);
    VSM_ASSERT_EQ(restored.tempoMap.changes()[0].microsecondsPerQuarterNote, uint32_t{461538});
}

VSM_TEST(effects_are_described_semantically_on_a_track) {
    ProjectDocument document;
    ProjectTrack track;
    track.name = "Guitare";
    ProjectEffect reverb;
    reverb.type = "reverb";
    reverb.parameters["effect.reverb.mix"] = 0.35f;
    reverb.parameters["effect.reverb.size"] = 0.7f;
    track.effects.push_back(reverb);
    document.tracks.push_back(track);

    const auto loaded = parseProjectDocument(projectDocumentToJson(document).toString());
    VSM_ASSERT(loaded.success);
    VSM_ASSERT_EQ(loaded.document.tracks[0].effects.size(), size_t{1});
    VSM_ASSERT_EQ(loaded.document.tracks[0].effects[0].type, std::string("reverb"));
    VSM_ASSERT_NEAR(loaded.document.tracks[0].effects[0].parameters.at("effect.reverb.mix"), 0.35f, 1e-6);
}


VSM_TEST(automation_round_trips_and_stays_optional) {
    // L'automation est un champ FACULTATIF : un document sans elle doit
    // produire exactement le JSON d'avant (aucune clé nouvelle), et un projet
    // ancien doit se charger sans rien remarquer. Avec elle, ticks, valeurs
    // en unités réelles et paliers doivent survivre à l'aller-retour.
    ProjectDocument document = documentFromProject(buildProject());
    const std::string sans = projectDocumentToJson(document).toString();
    VSM_ASSERT(sans.find("automation") == std::string::npos);
    VSM_ASSERT(parseProjectDocument(sans).success);

    ProjectAutomationLane lane;
    lane.parameter = "filter.1.cutoff";
    lane.points = {{0, 220.0f, false}, {960, 4800.0f, false}, {1920, 350.0f, true}};
    document.tracks[0].automation.push_back(lane);

    const ProjectLoadResult loaded =
        parseProjectDocument(projectDocumentToJson(document).toString());
    VSM_ASSERT(loaded.success);
    const auto& copie = loaded.document.tracks[0].automation;
    VSM_ASSERT_EQ(copie.size(), size_t(1));
    VSM_ASSERT_EQ(copie[0].parameter, std::string("filter.1.cutoff"));
    VSM_ASSERT_EQ(copie[0].points.size(), size_t(3));
    VSM_ASSERT_EQ(copie[0].points[1].tick, int64_t(960));
    VSM_ASSERT_NEAR(copie[0].points[1].value, 4800.0f, 1e-3);
    VSM_ASSERT(!copie[0].points[1].step);
    VSM_ASSERT(copie[0].points[2].step);

    // Une courbe sans cible ou sans point ne commande rien : filtrée à la
    // lecture, pas traînée jusqu'au rendu.
    document.tracks[0].automation.push_back(ProjectAutomationLane{});
    const ProjectLoadResult filtre =
        parseProjectDocument(projectDocumentToJson(document).toString());
    VSM_ASSERT(filtre.success);
    VSM_ASSERT_EQ(filtre.document.tracks[0].automation.size(), size_t(1));
}

// --- D0.1 / D0.2 : ce que le modèle porte doit revenir du disque ------------
//
// Le format savait écrire les effets, l'automation et la boucle depuis le
// début -- les tests ci-dessus le prouvent. Ce qui manquait était le maillon
// d'avant : `Track` ne les portait pas, si bien que `documentFromProject()`
// écrivait des tableaux vides et que l'application perdait tout à la
// fermeture. Ces trois tests couvrent le trajet COMPLET, modèle -> document ->
// JSON -> document -> modèle, qui est le seul dont l'utilisateur fasse
// l'expérience.

VSM_TEST(effects_and_automation_survive_the_trip_through_the_model) {
    Project project = buildProject();
    project.tracks[0].effects.push_back({"reverb", {{"reverb.1.mix", 0.35f},
                                                     {"reverb.1.size", 0.8f}}});
    project.tracks[0].effects.push_back({"delay", {{"delay.1.time", 0.375f}}});
    vsm::sequencer::AutomationCurve curve;
    curve.parameter = "filter.1.cutoff";
    curve.points = {{0, 220.0f, false}, {960, 4800.0f, true}};
    project.tracks[1].automation.push_back(curve);

    const ProjectLoadResult relu =
        parseProjectDocument(projectDocumentToJson(documentFromProject(project)).toString());
    VSM_ASSERT(relu.success);

    Project rejoue = project;          // mêmes pistes, mêmes notes
    for (auto& track : rejoue.tracks) { track.effects.clear(); track.automation.clear(); }
    applyDocumentToProject(relu.document, rejoue);

    // L'ORDRE de la chaîne compte autant que son contenu : un delay avant une
    // reverb ne sonne pas comme une reverb avant un delay.
    VSM_ASSERT_EQ(rejoue.tracks[0].effects.size(), size_t(2));
    VSM_ASSERT_EQ(rejoue.tracks[0].effects[0].type, std::string("reverb"));
    VSM_ASSERT_EQ(rejoue.tracks[0].effects[1].type, std::string("delay"));
    VSM_ASSERT_NEAR(rejoue.tracks[0].effects[0].parameters.at("reverb.1.mix"), 0.35f, 1e-6);
    VSM_ASSERT_NEAR(rejoue.tracks[0].effects[1].parameters.at("delay.1.time"), 0.375f, 1e-6);

    VSM_ASSERT_EQ(rejoue.tracks[1].automation.size(), size_t(1));
    VSM_ASSERT_EQ(rejoue.tracks[1].automation[0].parameter, std::string("filter.1.cutoff"));
    VSM_ASSERT_EQ(rejoue.tracks[1].automation[0].points.size(), size_t(2));
    VSM_ASSERT_NEAR(rejoue.tracks[1].automation[0].points[1].value, 4800.0f, 1e-3);
    VSM_ASSERT(rejoue.tracks[1].automation[0].points[1].step);
}

VSM_TEST(removing_a_track_takes_its_effects_with_it) {
    // LA RÉGRESSION QUE CE TEST INTERDIT. Les chaînes vivaient dans une
    // `std::map<int, Chain>` indexée par numéro de piste, hors du projet :
    // supprimer la piste 0 laissait la chaîne de l'ancienne piste 1 sous
    // l'index 1, désormais occupé par une AUTRE piste. Les effets changeaient
    // de propriétaire en silence. Rangés dans la piste, ils la suivent.
    Project project = buildProject();
    project.tracks[0].effects.push_back({"distortion", {{"distortion.1.drive", 0.9f}}});
    project.tracks[1].effects.push_back({"chorus", {{"chorus.1.depth", 0.2f}}});

    project.tracks.erase(project.tracks.begin());

    VSM_ASSERT_EQ(project.tracks.size(), size_t(1));
    VSM_ASSERT_EQ(project.tracks[0].effects.size(), size_t(1));
    VSM_ASSERT_EQ(project.tracks[0].effects[0].type, std::string("chorus"));

    // Et la survie au disque vaut aussi après la suppression.
    const ProjectLoadResult relu =
        parseProjectDocument(projectDocumentToJson(documentFromProject(project)).toString());
    VSM_ASSERT(relu.success);
    VSM_ASSERT_EQ(relu.document.tracks.size(), size_t(1));
    VSM_ASSERT_EQ(relu.document.tracks[0].effects[0].type, std::string("chorus"));
}

VSM_TEST(the_loop_region_is_project_data_not_screen_state) {
    Project project = buildProject();
    project.loopEnabled = true;
    project.loopStartTick = 1920;
    project.loopEndTick = 3840;

    const ProjectLoadResult relu =
        parseProjectDocument(projectDocumentToJson(documentFromProject(project)).toString());
    VSM_ASSERT(relu.success);

    Project rejoue = project;
    rejoue.loopEnabled = false;
    rejoue.loopStartTick = 0;
    rejoue.loopEndTick = 0;
    applyDocumentToProject(relu.document, rejoue);

    VSM_ASSERT(rejoue.loopEnabled);
    VSM_ASSERT_EQ(rejoue.loopStartTick, vsm::midi::Tick(1920));
    VSM_ASSERT_EQ(rejoue.loopEndTick, vsm::midi::Tick(3840));
}

// Une piste SANS instrument peut porter des effets : le chargement s'arrêtait
// avant de les lire pour ces pistes-là, ce qui les aurait perdues sans bruit.
VSM_TEST(a_track_without_an_instrument_still_keeps_its_effects) {
    Project project = buildProject();
    project.tracks[1].instrumentId.clear();
    project.tracks[1].effects.push_back({"phaser", {{"phaser.1.rate", 0.5f}}});

    const ProjectLoadResult relu =
        parseProjectDocument(projectDocumentToJson(documentFromProject(project)).toString());
    VSM_ASSERT(relu.success);

    Project rejoue = project;
    for (auto& track : rejoue.tracks) track.effects.clear();
    applyDocumentToProject(relu.document, rejoue);
    VSM_ASSERT_EQ(rejoue.tracks[1].effects.size(), size_t(1));
    VSM_ASSERT_EQ(rejoue.tracks[1].effects[0].type, std::string("phaser"));
}
