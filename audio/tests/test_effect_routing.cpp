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
    project.tracks[0].sendLevels[0] = 1.0f;
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
