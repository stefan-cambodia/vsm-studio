#include "TestFramework.h"
#include "vsm/sequencer/ProjectHistory.h"

using namespace vsm::sequencer;

// D1.5 de docs/ROADMAP-daw.md — L'ANNULATION COUVRE LE PROJET.
//
// L'historique d'origine mémorisait le seul vecteur de notes de la piste
// active. Il devait donc être vidé à chaque changement de piste, et il ne
// pouvait rien annuler d'autre que des notes : ajouter une piste, régler un
// fader, insérer un effet, dessiner une automation, poser un repère — tout cela
// était définitif, sans que rien ne le dise.

namespace {

Project deuxPistes() {
    Project project;
    project.ticksPerQuarterNote = 480;
    uint64_t ids = 1;
    Track basse; basse.name = "Basse"; basse.volume = 1.0f;
    basse.addNote(0, 480, 36, 100, 0, ids);
    Track lead; lead.name = "Lead";
    lead.addNote(0, 480, 72, 100, 1, ids);
    project.tracks = {basse, lead};
    return project;
}

} // namespace

VSM_TEST(undoing_covers_the_mix_not_only_the_notes) {
    Project project = deuxPistes();
    ProjectHistory history;

    history.beginEdit(project, "Volume");
    project.tracks[0].volume = 0.25f;
    project.tracks[0].muted = true;

    VSM_ASSERT(history.canUndo());
    VSM_ASSERT_EQ(history.undoLabel(), std::string("Volume"));
    VSM_ASSERT(history.undo(project));
    VSM_ASSERT_NEAR(project.tracks[0].volume, 1.0f, 1e-6);
    VSM_ASSERT(!project.tracks[0].muted);

    VSM_ASSERT(history.redo(project));
    VSM_ASSERT_NEAR(project.tracks[0].volume, 0.25f, 1e-6);
}

VSM_TEST(undoing_covers_adding_and_removing_a_track) {
    Project project = deuxPistes();
    ProjectHistory history;

    history.beginEdit(project, "Ajouter une piste");
    project.tracks.push_back(Track{});
    VSM_ASSERT_EQ(project.tracks.size(), size_t(3));
    VSM_ASSERT(history.undo(project));
    VSM_ASSERT_EQ(project.tracks.size(), size_t(2));

    history.beginEdit(project, "Supprimer une piste");
    project.tracks.erase(project.tracks.begin());
    VSM_ASSERT_EQ(project.tracks.size(), size_t(1));
    VSM_ASSERT(history.undo(project));
    VSM_ASSERT_EQ(project.tracks.size(), size_t(2));
    // Et la piste revient AVEC ses notes : c'est la différence entre annuler et
    // recréer une piste vide du même nom.
    VSM_ASSERT_EQ(project.tracks[0].name, std::string("Basse"));
    VSM_ASSERT_EQ(project.tracks[0].notes.size(), size_t(1));
}

VSM_TEST(undoing_covers_effects_automation_clips_and_markers) {
    Project project = deuxPistes();
    ProjectHistory history;

    history.beginEdit(project, "Tout à la fois");
    project.tracks[0].effects.push_back({"reverb", {{"reverb.1.mix", 0.4f}}});
    AutomationCurve courbe;
    courbe.parameter = "filter.1.cutoff";
    courbe.points = {{0, 400.0f, false}};
    project.tracks[1].automation.push_back(courbe);
    Clip clip; clip.sourceLength = 480; clip.startTick = 960;
    project.tracks[1].clips.push_back(clip);
    project.markers.push_back({480, "Pont"});

    VSM_ASSERT(history.undo(project));
    VSM_ASSERT(project.tracks[0].effects.empty());
    VSM_ASSERT(project.tracks[1].automation.empty());
    VSM_ASSERT(project.tracks[1].clips.empty());
    VSM_ASSERT(project.markers.empty());
}

VSM_TEST(the_history_no_longer_has_to_be_cleared_when_the_edited_track_changes) {
    // LE DÉFAUT QUE CE TEST INTERDIT. L'ancien historique portait sur les notes
    // d'UNE piste : le piano roll devait le vider en changeant de piste, sans
    // quoi il aurait restauré les notes de l'une dans l'autre. Autrement dit,
    // regarder une autre piste effaçait tout ce qu'on pouvait annuler.
    Project project = deuxPistes();
    ProjectHistory history;

    history.beginEdit(project, "Éditer la basse");
    project.tracks[0].notes[0].velocity = 20;

    history.beginEdit(project, "Éditer le lead");
    project.tracks[1].notes[0].velocity = 20;

    // Deux gestes sur deux pistes différentes, et les deux s'annulent.
    VSM_ASSERT(history.undo(project));
    VSM_ASSERT_EQ(static_cast<int>(project.tracks[1].notes[0].velocity), 100);
    VSM_ASSERT_EQ(static_cast<int>(project.tracks[0].notes[0].velocity), 20);
    VSM_ASSERT(history.undo(project));
    VSM_ASSERT_EQ(static_cast<int>(project.tracks[0].notes[0].velocity), 100);
}

VSM_TEST(a_new_edit_drops_the_redo_branch) {
    Project project = deuxPistes();
    ProjectHistory history;
    history.beginEdit(project, "Un");
    project.tracks[0].volume = 0.5f;
    VSM_ASSERT(history.undo(project));
    VSM_ASSERT(history.canRedo());

    history.beginEdit(project, "Deux");
    project.tracks[0].pan = 0.5f;
    VSM_ASSERT(!history.canRedo());
}
