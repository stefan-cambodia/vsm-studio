#include "TestFramework.h"
#include "vsm/interchange/Json.h"
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
        // Les niveaux d'envoi sont un VECTEUR depuis D4.2 : on compare la liste
        // entière, pas son premier élément -- une piste peut n'en déclarer
        // aucun, et l'indexer aveuglément était précisément le défaut que ce
        // test a attrapé au moment du changement.
        VSM_ASSERT_EQ(copy.tracks[i].sendLevels.size(), original.tracks[i].sendLevels.size());
        for (size_t b = 0; b < copy.tracks[i].sendLevels.size(); ++b)
            VSM_ASSERT_NEAR(copy.tracks[i].sendLevels[b], original.tracks[i].sendLevels[b], 1e-6);
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

// --- D1.3 : la version 2, et la migration qui ne perd rien ------------------

VSM_TEST(clips_and_markers_survive_the_trip_through_the_model) {
    Project project = buildProject();
    vsm::sequencer::Clip clip;
    clip.sourceStart = 480; clip.sourceLength = 960;
    clip.startTick = 1920; clip.length = 2880;   // bouclé trois fois
    clip.muted = false; clip.name = "Refrain"; clip.colorRgba = 0xFF8ED081u;
    project.tracks[0].clips.push_back(clip);
    project.markers.push_back({0, "Intro"});
    project.markers.push_back({1920, "Refrain"});

    const ProjectLoadResult relu =
        parseProjectDocument(projectDocumentToJson(documentFromProject(project)).toString());
    VSM_ASSERT(relu.success);

    Project rejoue = project;
    for (auto& t : rejoue.tracks) t.clips.clear();
    rejoue.markers.clear();
    applyDocumentToProject(relu.document, rejoue);

    VSM_ASSERT_EQ(rejoue.tracks[0].clips.size(), size_t(1));
    const auto& c = rejoue.tracks[0].clips[0];
    VSM_ASSERT_EQ(c.sourceStart, vsm::midi::Tick(480));
    VSM_ASSERT_EQ(c.sourceLength, vsm::midi::Tick(960));
    VSM_ASSERT_EQ(c.startTick, vsm::midi::Tick(1920));
    VSM_ASSERT_EQ(c.length, vsm::midi::Tick(2880));
    VSM_ASSERT_EQ(c.name, std::string("Refrain"));
    VSM_ASSERT_EQ(c.colorRgba, 0xFF8ED081u);

    VSM_ASSERT_EQ(rejoue.markers.size(), size_t(2));
    VSM_ASSERT_EQ(rejoue.markers[1].tick, vsm::midi::Tick(1920));
    VSM_ASSERT_EQ(rejoue.markers[1].name, std::string("Refrain"));
}

VSM_TEST(a_project_without_clips_writes_the_very_same_file_as_before) {
    // La règle de tout le format : un champ facultatif absent ne s'écrit pas.
    // Sans elle, la version 2 réécrirait chaque projet du dépôt avec des
    // tableaux vides, et toute comparaison de fichiers deviendrait bruyante.
    const std::string json = projectDocumentToJson(documentFromProject(buildProject())).toString();
    VSM_ASSERT(json.find("\"clips\"") == std::string::npos);
    VSM_ASSERT(json.find("\"markers\"") == std::string::npos);
}

VSM_TEST(a_version_1_project_loads_and_comes_back_as_version_2) {
    // MIGRATION : une piste de la version 1 n'a pas de clip, c'est-à-dire
    // qu'elle n'est pas découpée -- un état parfaitement représentable en
    // version 2. La conversion est donc VIDE, ce qui est précisément ce qui la
    // rend impossible à rater.
    const std::string v1 = R"({
      "format": "vsm-project",
      "version": 1,
      "title": "Ancien projet",
      "midi": { "file": "midi/arrangement.mid" },
      "transport": { "ticksPerQuarterNote": 480,
                     "tempoChanges": [ { "tick": 0, "bpm": 100.0 } ] },
      "tracks": [ { "name": "Basse", "channel": 1,
                    "instrument": { "preferredPlugin": "vsm.tb303" },
                    "mix": { "volume": 0.7, "pan": -0.2 } } ]
    })";

    const ProjectLoadResult ancien = parseProjectDocument(v1);
    VSM_ASSERT(ancien.success);
    VSM_ASSERT_EQ(ancien.document.tracks.size(), size_t(1));
    VSM_ASSERT(ancien.document.tracks[0].clips.empty());
    VSM_ASSERT_EQ(ancien.document.tracks[0].preferredPlugin, std::string("vsm.tb303"));

    // Réécrit : il porte désormais la version courante, et rien n'a été perdu.
    const std::string reecrit = projectDocumentToJson(ancien.document).toString();
    VSM_ASSERT(reecrit.find("\"version\": 2") != std::string::npos
               || reecrit.find("\"version\":2") != std::string::npos);
    const ProjectLoadResult relu = parseProjectDocument(reecrit);
    VSM_ASSERT(relu.success);
    VSM_ASSERT_EQ(relu.document.title, std::string("Ancien projet"));
    VSM_ASSERT_EQ(relu.document.tracks.size(), size_t(1));
    VSM_ASSERT_EQ(relu.document.transport.tempoChanges.size(), size_t(1));
    VSM_ASSERT_EQ(relu.document.tracks[0].name, std::string("Basse"));
    VSM_ASSERT_NEAR(relu.document.tracks[0].volume, 0.7f, 1e-6);
    VSM_ASSERT_NEAR(relu.document.transport.tempoChanges[0].bpm, 100.0, 1e-9);
}

VSM_TEST(a_version_from_the_future_is_refused_not_guessed) {
    const std::string futur = R"({"format":"vsm-project","version":99,"title":"X",
                                   "midi":{"file":"midi/arrangement.mid"},"tracks":[]})";
    const ProjectLoadResult result = parseProjectDocument(futur);
    VSM_ASSERT(!result.success);
    VSM_ASSERT(result.error.find("99") != std::string::npos);
}

// --- D2.1 : la piste audio dans le format -----------------------------------

VSM_TEST(an_audio_track_survives_the_trip_through_the_model) {
    Project project = buildProject();
    Track voix;
    voix.kind = Track::Kind::Audio;
    voix.name = "Voix";
    voix.audio = {"samples/voix.wav", 44100.0, 23468545, 2};
    vsm::sequencer::Clip clip;
    clip.startTick = 960;
    clip.length = 1920;
    clip.sourceStartSeconds = 12.5;
    clip.fadeInSeconds = 0.02;
    clip.fadeOutSeconds = 0.35;
    clip.gain = 0.7f;
    clip.invertPhase = true;
    voix.clips.push_back(clip);
    project.tracks.push_back(voix);

    const ProjectLoadResult relu =
        parseProjectDocument(projectDocumentToJson(documentFromProject(project)).toString());
    VSM_ASSERT(relu.success);

    Project rejoue = project;
    rejoue.tracks.back() = Track{};
    applyDocumentToProject(relu.document, rejoue);

    const Track& piste = rejoue.tracks.back();
    VSM_ASSERT(piste.kind == Track::Kind::Audio);
    VSM_ASSERT_EQ(piste.audio.path, std::string("samples/voix.wav"));
    VSM_ASSERT_EQ(piste.audio.frames, int64_t(23468545));
    VSM_ASSERT_EQ(piste.audio.channels, 2);
    VSM_ASSERT_NEAR(piste.audio.durationSeconds(), 532.16, 0.01);
    VSM_ASSERT_EQ(piste.clips.size(), size_t(1));
    VSM_ASSERT_NEAR(piste.clips[0].sourceStartSeconds, 12.5, 1e-9);
    VSM_ASSERT_NEAR(piste.clips[0].fadeOutSeconds, 0.35, 1e-9);
    VSM_ASSERT_NEAR(piste.clips[0].gain, 0.7f, 1e-6);
    VSM_ASSERT(piste.clips[0].invertPhase);
}

VSM_TEST(a_midi_only_project_writes_no_audio_field_at_all) {
    // La règle de tout le format : un champ facultatif absent ne s'écrit pas.
    // Un projet sans piste audio doit garder, octet pour octet, le fichier
    // qu'il avait avant que les pistes audio existent.
    const std::string json = projectDocumentToJson(documentFromProject(buildProject())).toString();
    VSM_ASSERT(json.find("\"kind\"") == std::string::npos);
    VSM_ASSERT(json.find("\"audio\"") == std::string::npos);
}

VSM_TEST(an_absolute_audio_path_is_refused_like_an_absolute_preset_path) {
    const std::string json = R"({"format":"vsm-project","version":2,"title":"X",
        "midi":{"file":"midi/arrangement.mid"},
        "tracks":[{"name":"Voix","kind":"audio",
                    "audio":{"file":"/home/quelquun/voix.wav","sampleRate":44100,
                              "frames":100,"channels":2}}]})";
    const ProjectLoadResult result = parseProjectDocument(json);
    VSM_ASSERT(!result.success);
    VSM_ASSERT(result.error.find("non portable") != std::string::npos);
}

// --- D4.2 : les bus de départ ----------------------------------------------

VSM_TEST(send_buses_survive_the_round_trip_with_their_effect_and_settings) {
    // Ils étaient DEUX, figés dans le code sur une réverbération et un delay, et
    // rien dans le projet ne disait ce que les boutons « send » alimentaient.
    // Le fichier le dit maintenant, et c'est cela qu'on vérifie.
    Project projet;
    projet.tracks.emplace_back();
    uint64_t ids = 1;
    projet.tracks[0].addNote(0, 480, 60, 100, 0, ids);

    vsm::sequencer::SendBusDescription salle;
    salle.name = "Grande salle";
    salle.effectType = "reverb";
    salle.parameters["effect.reverb.size"] = 0.85f;
    salle.returnGain = 0.6f;
    projet.sends.push_back(salle);

    vsm::sequencer::SendBusDescription echo;
    echo.name = "Echo court";
    echo.effectType = "delay";
    projet.sends.push_back(echo);

    projet.tracks[0].setSendLevel(1, 0.42f);

    const ProjectDocument document = documentFromProject(projet);
    const ProjectLoadResult relu = parseProjectDocument(projectDocumentToJson(document).toString());
    VSM_ASSERT(relu.success);
    VSM_ASSERT_EQ(relu.document.sends.size(), size_t(2));
    VSM_ASSERT_EQ(relu.document.sends[0].name, std::string("Grande salle"));
    VSM_ASSERT_EQ(relu.document.sends[0].effectType, std::string("reverb"));
    VSM_ASSERT_NEAR(relu.document.sends[0].returnGain, 0.6f, 1e-6);
    VSM_ASSERT_NEAR(relu.document.sends[0].parameters.at("effect.reverb.size"), 0.85f, 1e-6);
    VSM_ASSERT_EQ(relu.document.sends[1].effectType, std::string("delay"));

    // Et les niveaux de la piste sont revenus sur le BON bus.
    Project restaure;
    restaure.tracks.emplace_back();
    applyDocumentToProject(relu.document, restaure);
    VSM_ASSERT_EQ(restaure.sends.size(), size_t(2));
    VSM_ASSERT_NEAR(restaure.tracks[0].sendLevel(1), 0.42f, 1e-6);
    VSM_ASSERT_NEAR(restaure.tracks[0].sendLevel(0), 0.0f, 1e-6);
}

VSM_TEST(a_project_without_send_buses_writes_no_sends_key) {
    // Champ facultatif, comme les clips et les prises : un projet qui n'en
    // déclare pas garde exactement le fichier qu'il avait.
    //
    // La vérification passe par le JSON ANALYSÉ et non par une recherche de
    // texte : chaque piste écrit déjà ses NIVEAUX d'envoi sous la clé « sends »
    // de son bloc de mixage, et chercher la chaîne dans le fichier entier
    // trouve celle-là. Deux choses différentes portent le même nom à deux
    // niveaux différents, et seule la structure les distingue.
    Project projet;
    projet.tracks.emplace_back();
    const auto texte = projectDocumentToJson(documentFromProject(projet)).toString();
    const auto analyse = parseJson(texte);
    VSM_ASSERT(analyse.success);
    VSM_ASSERT(!analyse.value["sends"].isArray());

    projet.sends.push_back({"Un bus", "reverb", {}, 1.0f});
    const auto avec = parseJson(projectDocumentToJson(documentFromProject(projet)).toString());
    VSM_ASSERT(avec.success);
    VSM_ASSERT(avec.value["sends"].isArray());
    VSM_ASSERT_EQ(avec.value["sends"].size(), size_t(1));
}

VSM_TEST(more_send_levels_than_buses_do_not_shift_onto_the_wrong_bus) {
    // Le vrai risque du passage à une liste : un niveau qui glisse d'un bus à
    // l'autre s'entend, mais on cherche longtemps pourquoi une piste part dans
    // le delay alors qu'on avait réglé la réverbération.
    Project projet;
    projet.tracks.emplace_back();
    projet.sends.push_back({"Un seul", "reverb", {}, 1.0f});
    projet.tracks[0].setSendLevel(0, 0.5f);
    projet.tracks[0].setSendLevel(3, 0.9f);   // vers un bus qui n'existe pas

    const ProjectDocument document = documentFromProject(projet);
    Project restaure;
    restaure.tracks.emplace_back();
    applyDocumentToProject(document, restaure);
    VSM_ASSERT_NEAR(restaure.tracks[0].sendLevel(0), 0.5f, 1e-6);
    VSM_ASSERT_NEAR(restaure.tracks[0].sendLevel(1), 0.0f, 1e-6);
    VSM_ASSERT_NEAR(restaure.tracks[0].sendLevel(2), 0.0f, 1e-6);
    VSM_ASSERT_NEAR(restaure.tracks[0].sendLevel(3), 0.9f, 1e-6);
}

VSM_TEST(the_pre_fader_switch_of_a_send_survives_the_round_trip) {
    // Post-fader est le défaut et l'était de force avant D4.3 : le fichier ne
    // porte donc la clé que lorsqu'elle dit quelque chose.
    Project projet;
    projet.tracks.emplace_back();
    projet.sends.push_back({"Casque", "delay", {}, 1.0f, true});
    projet.sends.push_back({"Salle", "reverb", {}, 1.0f, false});

    const auto texte = projectDocumentToJson(documentFromProject(projet)).toString();
    const auto analyse = parseJson(texte);
    VSM_ASSERT(analyse.success);
    VSM_ASSERT(analyse.value["sends"].at(0)["preFader"].asBoolean(false));
    VSM_ASSERT(!analyse.value["sends"].at(1)["preFader"].isBoolean());

    const ProjectLoadResult relu = parseProjectDocument(texte);
    VSM_ASSERT(relu.success);
    VSM_ASSERT(relu.document.sends[0].preFader);
    VSM_ASSERT(!relu.document.sends[1].preFader);

    Project restaure;
    restaure.tracks.emplace_back();
    applyDocumentToProject(relu.document, restaure);
    VSM_ASSERT(restaure.sends[0].preFader);
    VSM_ASSERT(!restaure.sends[1].preFader);
}

VSM_TEST(a_group_track_and_its_routing_survive_the_round_trip) {
    Project projet;
    uint64_t ids = 1;
    Track voix;
    voix.name = "Voix";
    voix.addNote(0, 480, 60, 100, 0, ids);
    voix.outputGroup = 1;
    projet.tracks.push_back(voix);

    Track groupe;
    groupe.kind = Track::Kind::Group;
    groupe.name = "Chœurs";
    groupe.volume = 0.7f;
    projet.tracks.push_back(groupe);

    const ProjectDocument document = documentFromProject(projet);
    const ProjectLoadResult relu = parseProjectDocument(projectDocumentToJson(document).toString());
    VSM_ASSERT(relu.success);
    VSM_ASSERT_EQ(relu.document.tracks[1].kind, std::string("group"));
    VSM_ASSERT_EQ(relu.document.tracks[0].outputGroup, 1);

    Project restaure;
    restaure.tracks.emplace_back();
    restaure.tracks.emplace_back();
    applyDocumentToProject(relu.document, restaure);
    VSM_ASSERT(restaure.tracks[1].kind == Track::Kind::Group);
    VSM_ASSERT_EQ(restaure.tracks[0].outputGroup, 1);
    VSM_ASSERT_NEAR(restaure.tracks[1].volume, 0.7f, 1e-6);
}

VSM_TEST(a_track_that_goes_to_the_master_writes_no_output_key) {
    // Facultatif : une piste qui va au master garde le fichier qu'elle avait
    // avant que les groupes existent.
    Project projet;
    projet.tracks.emplace_back();
    const auto analyse = parseJson(projectDocumentToJson(documentFromProject(projet)).toString());
    VSM_ASSERT(analyse.success);
    VSM_ASSERT(!analyse.value["tracks"].at(0)["output"].isNumber());
}

VSM_TEST(mix_automation_curves_survive_the_round_trip_under_their_own_names) {
    // D4.6 : les courbes ne pilotent plus seulement des réglages de machine.
    // Le format les nomme par des identités PRÉFIXÉES -- `mix.volume`,
    // `mix.send.2`, `insert.1.…`, `master.…` -- pour qu'un nom ne puisse pas en
    // désigner deux, et pour qu'on lise à l'œil ce qu'une courbe pilote.
    Project projet;
    projet.tracks.emplace_back();
    uint64_t ids = 1;
    projet.tracks[0].addNote(0, 480, 60, 100, 0, ids);

    projet.tracks[0].automation.push_back({"mix.volume", {{0, 1.0f, false}, {960, 0.0f, false}}});
    projet.tracks[0].automation.push_back({"mix.pan", {{0, -1.0f, false}}});
    projet.tracks[0].automation.push_back({"mix.send.2", {{480, 0.5f, true}}});
    projet.tracks[0].automation.push_back({"insert.1.effect.reverb.mix", {{0, 0.3f, false}}});
    projet.tracks[0].automation.push_back({"master.Limiter Ceiling", {{0, -6.0f, false}}});

    const ProjectDocument document = documentFromProject(projet);
    const ProjectLoadResult relu = parseProjectDocument(projectDocumentToJson(document).toString());
    VSM_ASSERT(relu.success);

    Project restaure;
    restaure.tracks.emplace_back();
    applyDocumentToProject(relu.document, restaure);
    const auto& courbes = restaure.tracks[0].automation;
    VSM_ASSERT_EQ(courbes.size(), size_t(5));
    VSM_ASSERT_EQ(courbes[0].parameter, std::string("mix.volume"));
    VSM_ASSERT_EQ(courbes[0].points.size(), size_t(2));
    VSM_ASSERT_NEAR(courbes[0].points[1].value, 0.0f, 1e-6);
    VSM_ASSERT_EQ(courbes[2].parameter, std::string("mix.send.2"));
    VSM_ASSERT(courbes[2].points[0].step);          // le palier survit
    VSM_ASSERT_EQ(courbes[3].parameter, std::string("insert.1.effect.reverb.mix"));
    VSM_ASSERT_EQ(courbes[4].parameter, std::string("master.Limiter Ceiling"));
    VSM_ASSERT_NEAR(courbes[4].points[0].value, -6.0f, 1e-6);
}

VSM_TEST(track_height_and_folding_survive_the_round_trip_and_stay_optional) {
    // D5.3 : ce ne sont pas des propriétés du son mais de la façon dont on
    // REGARDE ce morceau-là. Les perdre obligerait à refaire la mise en page à
    // chaque ouverture, et sur seize pistes ce n'est pas un détail.
    Project projet;
    for (int i = 0; i < 3; ++i) projet.tracks.emplace_back();
    projet.tracks[0].arrangementHeight = 140;
    projet.tracks[1].folded = true;
    // La piste 2 garde la hauteur standard et reste dépliée.

    const auto texte = projectDocumentToJson(documentFromProject(projet)).toString();
    const auto analyse = parseJson(texte);
    VSM_ASSERT(analyse.success);
    VSM_ASSERT(analyse.value["tracks"].at(0)["height"].isNumber());
    VSM_ASSERT(analyse.value["tracks"].at(1)["folded"].asBoolean(false));
    // Facultatifs : la piste qui n'a rien de particulier n'écrit rien.
    VSM_ASSERT(!analyse.value["tracks"].at(2)["height"].isNumber());
    VSM_ASSERT(!analyse.value["tracks"].at(2)["folded"].isBoolean());

    const ProjectLoadResult relu = parseProjectDocument(texte);
    VSM_ASSERT(relu.success);
    Project restaure;
    for (int i = 0; i < 3; ++i) restaure.tracks.emplace_back();
    applyDocumentToProject(relu.document, restaure);
    VSM_ASSERT_EQ(restaure.tracks[0].arrangementHeight, 140);
    VSM_ASSERT(!restaure.tracks[0].folded);
    VSM_ASSERT(restaure.tracks[1].folded);
    VSM_ASSERT_EQ(restaure.tracks[2].arrangementHeight, 56);
}
