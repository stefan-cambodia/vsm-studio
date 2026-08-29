#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cmath>
#include <memory>

using namespace vsm::audio::engine;

namespace {

/// Effet de test déterministe : multiplie le signal par un gain fixe.
class GainEffect : public vsm::audio::effect::IAudioEffect {
public:
    explicit GainEffect(float gain) : gain_(gain) {}
    void prepare(double, int) override {}
    void reset() override {}
    void process(float* l, float* r, int n) override {
        for (int i = 0; i < n; ++i) { l[i] *= gain_; r[i] *= gain_; }
    }
    void setParameter(vsm::audio::plugin::ParamId, float) override {}
    float getParameter(vsm::audio::plugin::ParamId) const override { return 0.0f; }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return params_; }
    const char* effectName() const override { return "Gain"; }
private:
    float gain_;
    vsm::audio::plugin::ParameterList params_;
};

vsm::sequencer::Project oneNoteProject() {
    vsm::sequencer::Project project;
    project.ticksPerQuarterNote = 480;
    vsm::sequencer::Track track; track.name = "T"; track.channel = 0;
    uint64_t id = 1;
    track.addNote(0, 480 * 4, 57, 110, 0, id);
    project.tracks.push_back(track);
    return project;
}

float peakOf(const std::vector<float>& b) {
    float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p;
}

float renderPeak(vsm::sequencer::Project project,
                 const std::shared_ptr<const ProcessGraph::EffectChain>& chain) {
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(project);
    if (chain) graph.setTrackEffectChain(0, chain);
    auto out = OfflineRenderer::render(graph, 8000.0, 256, 1.0);
    return peakOf(out.left);
}

} // namespace

VSM_TEST(effect_chain_gain_zero_silences_track) {
    auto project = oneNoteProject();
    float dry = renderPeak(project, nullptr);
    VSM_ASSERT(dry > 0.05f); // référence : la piste sonne

    auto chain = std::make_shared<ProcessGraph::EffectChain>();
    chain->push_back(std::make_shared<GainEffect>(0.0f));
    float killed = renderPeak(project, chain);
    VSM_ASSERT_NEAR(killed, 0.0, 1e-5); // l'insert à gain 0 coupe la piste
}

VSM_TEST(effect_chain_gain_half_reduces_output) {
    auto project = oneNoteProject();
    float dry = renderPeak(project, nullptr);

    auto chain = std::make_shared<ProcessGraph::EffectChain>();
    chain->push_back(std::make_shared<GainEffect>(0.5f));
    float halved = renderPeak(project, chain);

    VSM_ASSERT_NEAR(halved, dry * 0.5, dry * 0.05); // ~moitié du pic
}

VSM_TEST(effect_chain_order_is_respected) {
    // gain 2 puis gain 0 -> silence ; l'ordre de la chaîne compte.
    auto project = oneNoteProject();
    auto chain = std::make_shared<ProcessGraph::EffectChain>();
    chain->push_back(std::make_shared<GainEffect>(2.0f));
    chain->push_back(std::make_shared<GainEffect>(0.0f));
    VSM_ASSERT_NEAR(renderPeak(project, chain), 0.0, 1e-5);
}

VSM_TEST(send_bus_adds_signal_to_master) {
    auto project = oneNoteProject();
    float noSend = renderPeak(project, nullptr);

    // Même projet, avec un send plein vers le bus 0 (effet pass-through).
    // LE PROJET DÉCLARE SON BUS (D4.2) : le nombre de départs n'est plus une
    // constante du moteur, c'est le morceau qui dit combien il en a.
    project.sends.push_back({"A", "reverb", {}, 1.0f});
    project.tracks[0].setSendLevel(0, 1.0f);
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(project);
    graph.setSendEffect(0, std::make_shared<GainEffect>(1.0f));
    graph.setSendReturn(0, 1.0f);
    auto out = OfflineRenderer::render(graph, 8000.0, 256, 1.0);

    // Le retour de send ajoute le signal -> master plus fort que sans send.
    VSM_ASSERT(peakOf(out.left) > noSend * 1.3f);
}

VSM_TEST(a_send_level_without_a_declared_bus_goes_nowhere) {
    // LE CONTRAT DE D4.2 : un départ existe parce que le PROJET le déclare. Une
    // piste qui garde un niveau d'envoi vers un bus disparu ne doit pas
    // alimenter le bus suivant, qui contiendrait alors autre chose -- c'est le
    // genre de décalage silencieux que le passage d'un tableau figé à une liste
    // rend possible s'il n'est pas gardé.
    auto project = oneNoteProject();
    project.tracks[0].setSendLevel(0, 1.0f);   // niveau réglé, aucun bus déclaré
    VSM_ASSERT(project.sends.empty());

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(project);
    graph.setSendEffect(0, std::make_shared<GainEffect>(4.0f));   // gain énorme
    graph.setSendReturn(0, 1.0f);
    VSM_ASSERT_EQ(graph.activeSendCount(), size_t(0));
    const auto avec = OfflineRenderer::render(graph, 8000.0, 256, 1.0);

    const float sans = renderPeak(project, nullptr);
    VSM_ASSERT_NEAR(peakOf(avec.left), sans, 1e-6f);
}

VSM_TEST(a_project_may_declare_more_than_two_sends) {
    // « Le nombre de départs n'est plus une constante » : la vérification la
    // plus directe du critère de l'étape.
    auto project = oneNoteProject();
    for (int i = 0; i < 5; ++i)
        project.sends.push_back({"Bus " + std::to_string(i), "reverb", {}, 1.0f});
    project.tracks[0].setSendLevel(4, 1.0f);   // le CINQUIÈME bus

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(project);
    VSM_ASSERT_EQ(graph.activeSendCount(), size_t(5));
    graph.setSendEffect(4, std::make_shared<GainEffect>(1.0f));
    graph.setSendReturn(4, 1.0f);
    const auto out = OfflineRenderer::render(graph, 8000.0, 256, 1.0);

    VSM_ASSERT(peakOf(out.left) > renderPeak(project, nullptr) * 1.3f);
    VSM_ASSERT_EQ(graph.droppedSendBuses(), uint64_t(0));
}

VSM_TEST(more_sends_than_the_ceiling_are_counted_not_swallowed) {
    // Un départ qu'on aurait réglé et qui ne sonnerait pas est exactement le
    // genre de silence qu'on cherche des heures.
    auto project = oneNoteProject();
    for (size_t i = 0; i < ProcessGraph::kMaxSends + 3; ++i)
        project.sends.push_back({"Bus", "reverb", {}, 1.0f});

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setProject(project);
    VSM_ASSERT_EQ(graph.activeSendCount(), ProcessGraph::kMaxSends);
    VSM_ASSERT(graph.droppedSendBuses() > 0);
}

VSM_TEST(effect_routing_is_deterministic) {
    auto project = oneNoteProject();
    auto make = [&] {
        auto chain = std::make_shared<ProcessGraph::EffectChain>();
        chain->push_back(std::make_shared<GainEffect>(0.7f));
        ProcessGraph graph;
        graph.prepare(8000.0, 256);
        graph.setTrackInstrument(0, "vsm.minimoog");
        graph.setProject(project);
        graph.setTrackEffectChain(0, chain);
        return OfflineRenderer::render(graph, 8000.0, 256, 1.0).left;
    };
    auto a = make(); auto b = make();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

// ---------------------------------------------------------------------------
// Notes "live" (écoute au clavier du piano roll, saisie MIDI) : elles doivent
// sonner IMMÉDIATEMENT, y compris transport à l'arrêt -- c'est justement là
// qu'on écoute une note en la dessinant.
// ---------------------------------------------------------------------------

namespace {
vsm::sequencer::Project buildTwoTrackProject() {
    vsm::sequencer::Project project;
    project.ticksPerQuarterNote = 480;
    for (int i = 0; i < 2; ++i) {
        vsm::sequencer::Track track;
        track.name = "T" + std::to_string(i);
        track.channel = 0;
        project.tracks.push_back(track);
    }
    return project;
}

float peakOfBuffer(const std::vector<float>& b) {
    float peak = 0.0f;
    for (float s : b) peak = std::max(peak, std::abs(s));
    return peak;
}
} // namespace

VSM_TEST(live_note_sounds_while_transport_is_stopped) {
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setProject(buildTwoTrackProject());
    graph.setPlaying(false);

    std::vector<float> left(256, 0.0f), right(256, 0.0f);
    graph.processBlock(left.data(), right.data(), 256);
    VSM_ASSERT_NEAR(peakOfBuffer(left), 0.0, 1e-9); // rien avant l'écoute

    VSM_ASSERT(graph.sendLiveNote(ProcessGraph::LiveNoteSource::Ui, 0, 69, 100, true));
    graph.processBlock(left.data(), right.data(), 256);
    VSM_ASSERT(peakOfBuffer(left) > 0.01f);

    // La position de lecture ne doit PAS avoir bougé : écouter une note n'est
    // pas lire le morceau.
    VSM_ASSERT_NEAR(graph.currentSeconds(), 0.0, 1e-12);
}

VSM_TEST(live_note_off_releases_the_sound) {
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setProject(buildTwoTrackProject());
    graph.setPlaying(false);

    std::vector<float> left(256, 0.0f), right(256, 0.0f);
    graph.sendLiveNote(ProcessGraph::LiveNoteSource::Ui, 0, 69, 100, true);
    graph.processBlock(left.data(), right.data(), 256);
    VSM_ASSERT(peakOfBuffer(left) > 0.01f);

    graph.sendLiveNote(ProcessGraph::LiveNoteSource::Ui, 0, 69, 0, false);
    // Quelques blocs pour laisser le release s'éteindre.
    for (int i = 0; i < 200; ++i) graph.processBlock(left.data(), right.data(), 256);
    VSM_ASSERT(peakOfBuffer(left) < 0.001f);
}

VSM_TEST(live_note_goes_only_to_its_own_track) {
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setTrackInstrument(1, "vsm.testtone");
    auto project = buildTwoTrackProject();
    project.tracks[0].muted = true; // la piste 0 est muette : si le son sort, c'est bien la piste 1
    graph.setProject(project);
    graph.setPlaying(false);

    std::vector<float> left(256, 0.0f), right(256, 0.0f);
    graph.sendLiveNote(ProcessGraph::LiveNoteSource::Ui, 0, 69, 100, true);
    graph.processBlock(left.data(), right.data(), 256);
    VSM_ASSERT_NEAR(peakOfBuffer(left), 0.0, 1e-9); // piste 0 muette -> silence

    graph.sendLiveNote(ProcessGraph::LiveNoteSource::Ui, 1, 69, 100, true);
    graph.processBlock(left.data(), right.data(), 256);
    VSM_ASSERT(peakOfBuffer(left) > 0.01f);
}

VSM_TEST(live_note_sources_are_independent_queues) {
    // Une file par source : l'UI et le thread MIDI ne doivent jamais partager
    // un LockFreeRingBuffer (strictement SPSC). Les deux doivent arriver.
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    graph.setProject(buildTwoTrackProject());
    graph.setPlaying(false);

    VSM_ASSERT(graph.sendLiveNote(ProcessGraph::LiveNoteSource::Ui, 0, 60, 100, true));
    VSM_ASSERT(graph.sendLiveNote(ProcessGraph::LiveNoteSource::MidiInput, 0, 64, 100, true));

    std::vector<float> left(256, 0.0f), right(256, 0.0f);
    graph.processBlock(left.data(), right.data(), 256);
    VSM_ASSERT(peakOfBuffer(left) > 0.01f);
    VSM_ASSERT_EQ(graph.totalActiveVoices(), 2); // les deux notes tiennent
}

VSM_TEST(live_note_rejects_out_of_range_track) {
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    VSM_ASSERT(!graph.sendLiveNote(ProcessGraph::LiveNoteSource::Ui, 99999, 60, 100, true));
}

VSM_TEST(playback_still_works_after_live_notes) {
    // Non-régression : le chemin de lecture normal ne doit pas être perturbé
    // par l'ajout des notes live.
    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setTrackInstrument(0, "vsm.testtone");
    auto project = buildTwoTrackProject();
    uint64_t ids = 1;
    project.tracks[0].addNote(0, 480, 69, 100, 0, ids);
    graph.setProject(project);
    graph.sendLiveNote(ProcessGraph::LiveNoteSource::Ui, 0, 72, 100, true);
    graph.setPlaying(true);

    std::vector<float> left(256, 0.0f), right(256, 0.0f);
    graph.processBlock(left.data(), right.data(), 256);
    VSM_ASSERT(peakOfBuffer(left) > 0.01f);
    VSM_ASSERT(graph.currentSeconds() > 0.0); // la lecture avance normalement
}

VSM_TEST(process_graph_accepts_an_externally_created_instrument) {
    // Chemin utilisé par l'hôte CLAP : une machine construite ailleurs (donc
    // absente du registre) doit pouvoir jouer dans le graphe. Ici on réutilise
    // une machine native, créée à la main, pour vérifier le mécanisme lui-même.
    vsm::audio::plugin::registerBuiltInPlugins();
    auto instrument = vsm::audio::plugin::PluginRegistry::instance().create("vsm.testtone");
    VSM_ASSERT(instrument != nullptr);

    ProcessGraph graph;
    graph.prepare(48000.0, 256);
    auto project = buildTwoTrackProject();
    uint64_t ids = 1;
    project.tracks[0].addNote(0, 480, 69, 100, 0, ids);
    graph.setProject(project);
    graph.setTrackInstrumentInstance(0, instrument, "clap:test");
    VSM_ASSERT_EQ(graph.trackInstrumentId(0), std::string("clap:test"));
    VSM_ASSERT(graph.trackInstrument(0) != nullptr);

    graph.setPlaying(true);
    std::vector<float> left(256, 0.0f), right(256, 0.0f);
    graph.processBlock(left.data(), right.data(), 256);
    VSM_ASSERT(peakOfBuffer(left) > 0.01f);

    // Retrait : passer un pointeur nul libère la piste proprement.
    graph.setTrackInstrumentInstance(0, nullptr);
    VSM_ASSERT(graph.trackInstrument(0) == nullptr);
}

// --- D4.2 : les bus de GROUPE ----------------------------------------------

namespace {
using vsm::sequencer::Project;
using vsm::sequencer::Track;

/// Un projet à deux pistes qui jouent la même note, plus une piste de groupe.
Project groupProject() {
    Project project;
    project.ticksPerQuarterNote = 480;
    uint64_t ids = 1;
    for (int i = 0; i < 2; ++i) {
        Track t;
        t.name = "Voix " + std::to_string(i + 1);
        t.addNote(0, 480, 69, 100, 0, ids);
        project.tracks.push_back(t);
    }
    Track groupe;
    groupe.kind = Track::Kind::Group;
    groupe.name = "Groupe";
    project.tracks.push_back(groupe);
    return project;
}
} // namespace

VSM_TEST(a_group_passes_its_members_through_unchanged_when_it_is_neutral) {
    // Router deux pistes dans un groupe à volume 1 et panoramique centré ne
    // doit RIEN changer au mixage : c'est la condition pour que grouper ne soit
    // jamais un choix qu'on paie.
    vsm::sequencer::Project direct = groupProject();
    ProcessGraph a;
    a.prepare(8000.0, 256);
    a.setTrackInstrument(0, "vsm.minimoog");
    a.setTrackInstrument(1, "vsm.minimoog");
    a.setProject(direct);
    const auto sansGroupe = OfflineRenderer::render(a, 8000.0, 256, 1.0);

    vsm::sequencer::Project groupe = groupProject();
    groupe.tracks[0].outputGroup = 2;
    groupe.tracks[1].outputGroup = 2;
    ProcessGraph b;
    b.prepare(8000.0, 256);
    b.setTrackInstrument(0, "vsm.minimoog");
    b.setTrackInstrument(1, "vsm.minimoog");
    b.setProject(groupe);
    const auto avecGroupe = OfflineRenderer::render(b, 8000.0, 256, 1.0);

    VSM_ASSERT_EQ(sansGroupe.left.size(), avecGroupe.left.size());
    for (size_t i = 0; i < sansGroupe.left.size(); ++i) {
        VSM_ASSERT_NEAR(avecGroupe.left[i], sansGroupe.left[i], 1e-6f);
        VSM_ASSERT_NEAR(avecGroupe.right[i], sansGroupe.right[i], 1e-6f);
    }
}

VSM_TEST(a_group_fader_moves_all_its_members_at_once) {
    // C'est toute la raison d'être d'un groupe : un fader pour huit micros de
    // batterie, au lieu de huit gestes qu'on espère garder d'accord.
    vsm::sequencer::Project projet = groupProject();
    projet.tracks[0].outputGroup = 2;
    projet.tracks[1].outputGroup = 2;
    projet.tracks[2].volume = 0.25f;

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setTrackInstrument(1, "vsm.minimoog");
    graph.setProject(projet);
    const float avecFader = peakOf(OfflineRenderer::render(graph, 8000.0, 256, 1.0).left);

    vsm::sequencer::Project plein = groupProject();
    plein.tracks[0].outputGroup = 2;
    plein.tracks[1].outputGroup = 2;
    ProcessGraph g2;
    g2.prepare(8000.0, 256);
    g2.setTrackInstrument(0, "vsm.minimoog");
    g2.setTrackInstrument(1, "vsm.minimoog");
    g2.setProject(plein);
    const float sansFader = peakOf(OfflineRenderer::render(g2, 8000.0, 256, 1.0).left);

    VSM_ASSERT(sansFader > 0.01f);
    VSM_ASSERT_NEAR(avecFader, sansFader * 0.25f, sansFader * 0.02f);
}

VSM_TEST(a_muted_group_silences_its_members) {
    vsm::sequencer::Project projet = groupProject();
    projet.tracks[0].outputGroup = 2;
    projet.tracks[1].outputGroup = 2;
    projet.tracks[2].muted = true;

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setTrackInstrument(1, "vsm.minimoog");
    graph.setProject(projet);
    VSM_ASSERT(peakOf(OfflineRenderer::render(graph, 8000.0, 256, 1.0).left) < 1e-5f);
}

VSM_TEST(a_group_insert_treats_the_whole_group_and_not_each_track) {
    // Un gain de 0 sur le groupe éteint tout, y compris ce que les pistes
    // avaient de plus fort : c'est ce qui prouve que l'insert est APRÈS la
    // somme et non appliqué piste par piste.
    vsm::sequencer::Project projet = groupProject();
    projet.tracks[0].outputGroup = 2;
    projet.tracks[1].outputGroup = 2;

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setTrackInstrument(1, "vsm.minimoog");
    graph.setProject(projet);
    auto chain = std::make_shared<ProcessGraph::EffectChain>();
    chain->push_back(std::make_shared<GainEffect>(0.0f));
    graph.setTrackEffectChain(2, chain);   // l'insert est sur LE GROUPE
    VSM_ASSERT(peakOf(OfflineRenderer::render(graph, 8000.0, 256, 1.0).left) < 1e-5f);
}

VSM_TEST(a_group_never_routes_into_another_group) {
    // Les groupes imbriqués demanderaient un ordre topologique et une
    // détection de cycle. Un routage de groupe vers groupe est donc IGNORÉ et
    // part au master -- ce qui s'entend, là où une boucle ferait tourner le
    // rendu en rond.
    vsm::sequencer::Project projet = groupProject();
    vsm::sequencer::Track second;
    second.kind = vsm::sequencer::Track::Kind::Group;
    second.name = "Groupe 2";
    projet.tracks.push_back(second);       // index 3
    projet.tracks[0].outputGroup = 2;
    projet.tracks[2].outputGroup = 3;      // groupe -> groupe : ignoré

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(projet);
    // Le son sort quand même : le groupe 1 a été envoyé au master.
    VSM_ASSERT(peakOf(OfflineRenderer::render(graph, 8000.0, 256, 1.0).left) > 0.01f);
}

VSM_TEST(a_route_to_a_track_that_is_not_a_group_goes_to_the_master) {
    // Un index qui ne désigne pas un groupe -- piste supprimée, projet écrit
    // par une autre version -- ne doit pas faire disparaître le son.
    vsm::sequencer::Project projet = groupProject();
    projet.tracks[0].outputGroup = 1;      // la piste 1 n'est pas un groupe
    projet.tracks[1].outputGroup = 99;     // hors bornes

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setTrackInstrument(1, "vsm.minimoog");
    graph.setProject(projet);
    VSM_ASSERT(peakOf(OfflineRenderer::render(graph, 8000.0, 256, 1.0).left) > 0.01f);
}
