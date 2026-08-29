#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/audio/effect/ChannelStrip.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

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

// --- D4.3 : départs pré / post-fader ---------------------------------------

namespace {
/// Rend le pic du master pour un projet à un départ, avec le volume de piste
/// et le mode de départ donnés. L'effet du bus est un gain neutre, de sorte que
/// tout ce qu'on mesure vient du RETOUR.
float peakWithSend(float trackVolume, bool preFader) {
    vsm::sequencer::Project projet = oneNoteProject();
    vsm::sequencer::SendBusDescription bus;
    bus.name = "A";
    bus.effectType = "reverb";
    bus.preFader = preFader;
    projet.sends.push_back(bus);
    projet.tracks[0].setSendLevel(0, 1.0f);
    projet.tracks[0].volume = trackVolume;

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(projet);
    graph.setSendEffect(0, std::make_shared<GainEffect>(1.0f));
    graph.setSendReturn(0, 1.0f);
    return peakOf(OfflineRenderer::render(graph, 8000.0, 256, 1.0).left);
}
} // namespace

VSM_TEST(a_post_fader_send_follows_the_fader) {
    // Post-fader : baisser la piste baisse aussi ce qu'elle envoie, donc la
    // proportion d'effet reste constante. C'est ce qu'on veut d'une
    // réverbération -- une piste retirée du mixage ne doit pas laisser sa
    // réverbération toute seule.
    const float plein = peakWithSend(1.0f, false);
    const float baisse = peakWithSend(0.25f, false);
    VSM_ASSERT(plein > 0.01f);
    // Tout est divisé par quatre : le direct comme le départ.
    VSM_ASSERT_NEAR(baisse, plein * 0.25f, plein * 0.02f);
}

VSM_TEST(a_pre_fader_send_ignores_the_fader) {
    // Pré-fader : le départ prélève avant le fader. C'est ce qu'il faut pour un
    // retour de casque, ou pour envoyer une piste dans un effet SANS l'entendre
    // en direct -- fader à zéro, seul l'effet subsiste.
    const float plein = peakWithSend(1.0f, true);
    const float baisse = peakWithSend(0.25f, true);
    VSM_ASSERT(plein > 0.01f);
    // Le direct a baissé, le départ non : le total reste bien au-dessus du
    // quart qu'aurait donné un post-fader.
    VSM_ASSERT(baisse > plein * 0.5f);
}

VSM_TEST(a_pre_fader_send_still_sounds_with_the_fader_at_zero) {
    // Le cas qui justifie à lui seul l'existence du pré-fader.
    const float muet = peakWithSend(0.0f, false);
    const float prefader = peakWithSend(0.0f, true);
    VSM_ASSERT(muet < 1e-6f);        // post-fader : plus rien du tout
    VSM_ASSERT(prefader > 0.01f);    // pré-fader : l'effet subsiste
}

VSM_TEST(muting_a_track_silences_its_sends_pre_fader_included) {
    // CHOIX ÉCRIT : le muet coupe TOUT, y compris les départs pré-fader. Une
    // console les câble parfois avant le muet, mais dans cette application
    // « muet » veut dire « je ne veux plus l'entendre », et une piste muette
    // dont la réverbération continue de sonner serait déroutante.
    vsm::sequencer::Project projet = oneNoteProject();
    vsm::sequencer::SendBusDescription bus;
    bus.effectType = "reverb";
    bus.preFader = true;
    projet.sends.push_back(bus);
    projet.tracks[0].setSendLevel(0, 1.0f);
    projet.tracks[0].muted = true;

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(projet);
    graph.setSendEffect(0, std::make_shared<GainEffect>(1.0f));
    graph.setSendReturn(0, 1.0f);
    VSM_ASSERT(peakOf(OfflineRenderer::render(graph, 8000.0, 256, 1.0).left) < 1e-6f);
}

// --- D4.4 : la chaîne latérale ---------------------------------------------

VSM_TEST(a_compressor_ducks_a_track_when_it_listens_to_another) {
    // « Le compresseur d'une piste écoute une autre piste — la signature même
    // du genre que ce projet reconstruit. »
    //
    // CE QUI VARIE ENTRE LES DEUX RENDUS EST LE SEUL FIL QU'ON VEUT ÉPROUVER :
    // le niveau d'envoi de la frappe vers le bus d'écoute. Tout le reste --
    // instruments, notes, réglages du compresseur, chaîne latérale active -- est
    // identique. Une première version comparait « avec » et « sans » chaîne
    // latérale, et les deux compressaient autant : avec un seuil bas, le
    // compresseur écrase aussi bien son propre signal que celui qu'il écoute, et
    // le test ne prouvait rien.
    auto monter = [](float niveauDEcoute) {
        vsm::sequencer::Project projet;
        projet.ticksPerQuarterNote = 480;
        uint64_t ids = 1;

        vsm::sequencer::Track tenue;          // ce qu'on veut faire plonger
        tenue.name = "Nappe";
        tenue.addNote(0, 1920, 60, 100, 0, ids);
        projet.tracks.push_back(tenue);

        vsm::sequencer::Track frappe;         // ce qui la fait plonger
        frappe.name = "Grosse caisse";
        frappe.addNote(0, 1920, 84, 127, 0, ids);
        projet.tracks.push_back(frappe);

        // LE BUS D'ÉCOUTE EST PRÉ-FADER, et c'est D4.3 qui rend D4.4 utilisable :
        // la frappe COMMANDE sans s'entendre. Fader à zéro, départ pré-fader.
        //
        // La couper au MUET ne marcherait pas, et c'est cohérent : le muet coupe
        // aussi les départs (voir
        // `muting_a_track_silences_its_sends_pre_fader_included`). Une source de
        // chaîne latérale se retire du mixage par son fader, pas par son muet --
        // ce test l'a appris en échouant.
        vsm::sequencer::SendBusDescription bus;
        bus.name = "Ecoute";
        bus.effectType = "reverb";
        bus.preFader = true;
        projet.sends.push_back(bus);
        projet.tracks[1].setSendLevel(0, niveauDEcoute);
        projet.tracks[1].volume = 0.0f;

        auto graph = std::make_unique<ProcessGraph>();
        graph->prepare(8000.0, 256);
        graph->setTrackInstrument(0, "vsm.minimoog");
        graph->setTrackInstrument(1, "vsm.minimoog");
        graph->setProject(projet);
        // Le RETOUR du bus est coupé : on mesure l'effet de l'écoute sur la
        // nappe, pas le bus lui-même.
        graph->setSendEffect(0, std::make_shared<GainEffect>(1.0f));
        graph->setSendReturn(0, 0.0f);

        auto compresseur = std::make_shared<vsm::audio::effect::CompressorEffect>();
        compresseur->prepare(8000.0, 256);
        // Le seuil est placé SOUS le niveau de la frappe dans le bus (mesuré à
        // -10 dB) et le compresseur écoute le bus dans les DEUX rendus : quand
        // la frappe ne l'alimente pas, il détecte du silence et ne fait rien.
        // C'est ce qui rend la mesure lisible -- et c'est aussi le comportement
        // qu'on veut d'un compresseur à qui on désigne un bus vide.
        compresseur->setParameter(vsm::audio::effect::CompressorEffect::kThresholdDb, -20.0f);
        compresseur->setParameter(vsm::audio::effect::CompressorEffect::kRatio, 20.0f);
        compresseur->setParameter(vsm::audio::effect::CompressorEffect::kAttackMs, 0.5f);
        compresseur->setParameter(vsm::audio::effect::CompressorEffect::kSidechain, 1.0f);
        auto chain = std::make_shared<ProcessGraph::EffectChain>();
        chain->push_back(compresseur);
        graph->setTrackEffectChain(0, chain);   // sur la NAPPE

        return peakOf(OfflineRenderer::render(*graph, 8000.0, 256, 0.5).left);
    };

    const float ecouteMuette = monter(0.0f);   // la frappe n'alimente pas le bus
    const float ecoutePleine = monter(1.0f);   // elle l'alimente à fond
    VSM_ASSERT(ecouteMuette > 0.01f);
    // La nappe est écrasée par une frappe qu'on n'entend pas.
    VSM_ASSERT(ecoutePleine < ecouteMuette * 0.7f);
}

VSM_TEST(without_a_sidechain_the_render_order_is_left_alone) {
    // GARDE-FOU : réordonner les additions changerait le dernier bit du mixage
    // sans raison. Tant qu'aucun effet n'écoute, l'ordre reste celui des
    // pistes, et le rendu est identique AU BIT PRÈS à ce qu'il était.
    vsm::sequencer::Project projet;
    projet.ticksPerQuarterNote = 480;
    uint64_t ids = 1;
    for (int i = 0; i < 3; ++i) {
        vsm::sequencer::Track t;
        t.addNote(0, 480, static_cast<uint8_t>(60 + i * 4), 100, 0, ids);
        projet.tracks.push_back(t);
    }

    auto rendre = [&](bool avecChaine) {
        ProcessGraph graph;
        graph.prepare(8000.0, 256);
        for (int i = 0; i < 3; ++i) graph.setTrackInstrument(static_cast<size_t>(i), "vsm.minimoog");
        graph.setProject(projet);
        if (avecChaine) {
            auto compresseur = std::make_shared<vsm::audio::effect::CompressorEffect>();
            compresseur->prepare(8000.0, 256);
            compresseur->setParameter(vsm::audio::effect::CompressorEffect::kSidechain, 0.0f);
            auto chain = std::make_shared<ProcessGraph::EffectChain>();
            chain->push_back(compresseur);
            graph.setTrackEffectChain(0, chain);
            graph.setTrackEffectChain(0, nullptr);   // et on la retire
        }
        return OfflineRenderer::render(graph, 8000.0, 256, 0.4);
    };

    const auto a = rendre(false);
    const auto b = rendre(true);
    VSM_ASSERT_EQ(a.left.size(), b.left.size());
    for (size_t i = 0; i < a.left.size(); ++i)
        VSM_ASSERT_NEAR(a.left[i], b.left[i], 0.0f);   // au bit près
}

// --- D4.5 : compensation de latence (PDC) ----------------------------------

namespace {
/// Un effet qui RETARDE d'un nombre d'échantillons connu, et le déclare. Le
/// pendant exact de ce que fait un suréchantillonneur, en tenant dans dix
/// lignes : on éprouve la compensation, pas le filtre.
class LatencyEffect : public vsm::audio::effect::IAudioEffect {
public:
    // Les lignes sont dimensionnées DÈS LA CONSTRUCTION, et pas seulement dans
    // `prepare` : le graphe exige qu'un effet soit préparé avant d'être publié,
    // mais un test qui l'oublie doit échouer sur une assertion, pas sur un
    // segment de mémoire -- ce qui est arrivé en écrivant ces tests.
    explicit LatencyEffect(int retard) : retard_(retard) { reset(); }
    void prepare(double, int) override { reset(); }
    void reset() override {
        ligneL_.assign(static_cast<size_t>(std::max(1, retard_)), 0.0f);
        ligneR_ = ligneL_;
        pos_ = 0;
    }
    void process(float* l, float* r, int n) override {
        if (retard_ <= 0) return;
        for (int i = 0; i < n; ++i) {
            const float sl = ligneL_[static_cast<size_t>(pos_)];
            const float sr = ligneR_[static_cast<size_t>(pos_)];
            ligneL_[static_cast<size_t>(pos_)] = l[i];
            ligneR_[static_cast<size_t>(pos_)] = r[i];
            l[i] = sl;
            r[i] = sr;
            if (++pos_ >= retard_) pos_ = 0;
        }
    }
    void setParameter(vsm::audio::plugin::ParamId, float) override {}
    float getParameter(vsm::audio::plugin::ParamId) const override { return 0.0f; }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return vide_; }
    const char* effectName() const override { return "Latency"; }
    int latencySamples() const override { return retard_; }

private:
    int retard_;
    std::vector<float> ligneL_, ligneR_;
    int pos_ = 0;
    vsm::audio::plugin::ParameterList vide_;
};

/// Deux pistes qui jouent la MÊME note au même instant. Si elles restent
/// alignées, leur somme est exactement le double d'une seule ; si l'une glisse,
/// la somme n'est plus le double -- et c'est ce décalage qu'on mesure.
vsm::sequencer::Project twinProject() {
    vsm::sequencer::Project projet;
    projet.ticksPerQuarterNote = 480;
    uint64_t ids = 1;
    for (int i = 0; i < 2; ++i) {
        vsm::sequencer::Track t;
        t.addNote(0, 960, 60, 100, 0, ids);
        projet.tracks.push_back(t);
    }
    return projet;
}
} // namespace

VSM_TEST(an_effect_with_latency_shifts_its_track_and_the_graph_says_so) {
    // D'ABORD LE DÉFAUT, pour être sûr qu'on mesure quelque chose : sans plan
    // de compensation, la piste retardée n'est plus en place.
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setTrackInstrument(1, "vsm.minimoog");
    graph.setProject(twinProject());
    VSM_ASSERT_EQ(graph.graphLatencySamples(), 0);

    auto chain = std::make_shared<ProcessGraph::EffectChain>();
    auto retard = std::make_shared<LatencyEffect>(64);
    retard->prepare(8000.0, 256);
    chain->push_back(retard);
    graph.setTrackEffectChain(0, chain);
    // Le graphe DÉCLARE la latence de son chemin le plus long.
    VSM_ASSERT_EQ(graph.graphLatencySamples(), 64);
}

VSM_TEST(the_graph_realigns_the_tracks_that_have_no_latency) {
    // LE CRITÈRE DE L'ÉTAPE : insérer un effet à latence connue ne décale plus
    // la piste. On le vérifie en comparant à un rendu où les DEUX pistes
    // portent le même effet -- elles sont alors forcément alignées entre elles,
    // et c'est la référence.
    auto rendre = [](bool surLesDeux) {
        ProcessGraph graph;
        graph.prepare(8000.0, 256);
        graph.setTrackInstrument(0, "vsm.minimoog");
        graph.setTrackInstrument(1, "vsm.minimoog");
        graph.setProject(twinProject());
        auto faireRetard = [] {
            auto fx = std::make_shared<LatencyEffect>(64);
            fx->prepare(8000.0, 256);   // le graphe exige un effet préparé
            return fx;
        };
        auto a = std::make_shared<ProcessGraph::EffectChain>();
        a->push_back(faireRetard());
        graph.setTrackEffectChain(0, a);
        if (surLesDeux) {
            auto b = std::make_shared<ProcessGraph::EffectChain>();
            b->push_back(faireRetard());
            graph.setTrackEffectChain(1, b);
        }
        return OfflineRenderer::render(graph, 8000.0, 256, 0.6);
    };

    const auto reference = rendre(true);    // les deux retardées : alignées
    const auto compense = rendre(false);    // une seule : le graphe compense

    VSM_ASSERT_EQ(reference.left.size(), compense.left.size());
    for (size_t i = 0; i < reference.left.size(); ++i)
        VSM_ASSERT_NEAR(compense.left[i], reference.left[i], 1e-6f);
}

VSM_TEST(the_oversampling_distortion_declares_its_sixteen_samples) {
    // Le cas RÉEL que la roadmap demande : un effet du parc qui retarde parce
    // qu'il suréchantillonne, et qui le dit au lieu de décaler en silence.
    auto distorsion = vsm::audio::effect::EffectFactory::create("distortion");
    VSM_ASSERT(distorsion != nullptr);
    distorsion->prepare(48000.0, 256);
    VSM_ASSERT_EQ(distorsion->latencySamples(), 16);

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setTrackInstrument(1, "vsm.minimoog");
    graph.setProject(twinProject());
    auto chain = std::make_shared<ProcessGraph::EffectChain>();
    chain->push_back(std::shared_ptr<vsm::audio::effect::IAudioEffect>(std::move(distorsion)));
    graph.setTrackEffectChain(0, chain);
    VSM_ASSERT_EQ(graph.graphLatencySamples(), 16);
}

VSM_TEST(latencies_add_up_along_a_chain) {
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setProject(twinProject());
    auto chain = std::make_shared<ProcessGraph::EffectChain>();
    chain->push_back(std::make_shared<LatencyEffect>(32));
    chain->push_back(std::make_shared<LatencyEffect>(48));   // prêts dès la construction
    graph.setTrackEffectChain(0, chain);
    VSM_ASSERT_EQ(graph.graphLatencySamples(), 80);
}

VSM_TEST(a_group_insert_latency_counts_for_the_tracks_that_pass_through_it) {
    // Une piste groupée traverse DEUX chaînes avant le master. Ne compter que
    // la sienne la laisserait décalée du retard de son groupe -- un décalage
    // qui n'apparaîtrait qu'en groupant, c'est-à-dire au moment où on
    // soupçonnerait le moins l'insert.
    vsm::sequencer::Project projet = twinProject();
    vsm::sequencer::Track groupe;
    groupe.kind = vsm::sequencer::Track::Kind::Group;
    projet.tracks.push_back(groupe);      // index 2
    projet.tracks[0].outputGroup = 2;     // la piste 0 passe par le groupe

    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setProject(projet);
    auto chain = std::make_shared<ProcessGraph::EffectChain>();
    chain->push_back(std::make_shared<LatencyEffect>(40));
    graph.setTrackEffectChain(2, chain);  // l'insert est sur LE GROUPE
    // Le chemin de la piste 0 vaut 40 ; celui de la piste 1, zéro. Le maximum
    // est donc 40, et c'est la piste 1 qu'il faut retarder.
    VSM_ASSERT_EQ(graph.graphLatencySamples(), 40);
}

VSM_TEST(without_any_latency_no_compensation_plan_is_published) {
    // Même garde-fou que pour l'ordre de rendu : pas une ligne à retard de
    // longueur zéro à traverser pour rien, et le rendu reste au bit près celui
    // qu'il était.
    ProcessGraph graph;
    graph.prepare(8000.0, 256);
    graph.setTrackInstrument(0, "vsm.minimoog");
    graph.setProject(twinProject());
    auto chain = std::make_shared<ProcessGraph::EffectChain>();
    chain->push_back(std::make_shared<GainEffect>(0.5f));   // aucun retard
    graph.setTrackEffectChain(0, chain);
    VSM_ASSERT_EQ(graph.graphLatencySamples(), 0);
}
