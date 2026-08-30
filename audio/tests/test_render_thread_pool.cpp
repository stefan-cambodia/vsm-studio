#include "TestFramework.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/effect/ChannelStrip.h"
#include "vsm/audio/engine/RenderThreadPool.h"
#include "vsm/sequencer/Project.h"
#include <atomic>
#include <cmath>
#include <numeric>
#include <vector>

using namespace vsm::sequencer;
using namespace vsm::audio::engine;

namespace {

/// Un projet de `trackCount` pistes, chacune sur une machine différente et
/// chacune jouant un accord tenu : c'est la charge que le rendu multicœur est
/// censé répartir, et c'est aussi celle où une course se verrait.
Project buildLoadedProject(size_t trackCount, const std::vector<std::string>& machines) {
    Project project;
    const uint16_t ppq = 480;
    project.ticksPerQuarterNote = ppq;
    uint64_t idCounter = 1;
    for (size_t t = 0; t < trackCount; ++t) {
        Track track;
        track.name = machines[t % machines.size()];
        track.channel = static_cast<uint8_t>(t % 16);
        // Volumes et panoramiques tous différents : si le mixage se faisait
        // dans le désordre, l'écart se verrait au dernier bit.
        track.volume = 0.4f + 0.01f * static_cast<float>(t % 20);
        track.pan = -0.9f + 0.06f * static_cast<float>(t % 30);
        for (int n = 0; n < 4; ++n)
            track.addNote(static_cast<Tick>(n * 40), ppq * 8,
                          static_cast<uint8_t>(45 + 3 * n + static_cast<int>(t % 5)), 100, 0,
                          idCounter);
        project.tracks.push_back(track);
    }
    return project;
}

const std::vector<std::string>& machineMix() {
    static const std::vector<std::string> mix = {"vsm.juno106", "vsm.dx7",     "vsm.jupiter8",
                                                 "vsm.minimoog", "vsm.prophet", "vsm.tb303",
                                                 "vsm.tr909",    "vsm.sh101"};
    return mix;
}

/// Rend un projet et dit AUSSI combien de segments sont réellement passés par
/// le chemin parallèle : sans ce second chiffre, un test d'identité pourrait
/// comparer deux fois le chemin séquentiel et n'avoir rien vérifié.
struct Rendu {
    RenderedAudio audio;
    uint64_t segmentsParalleles = 0;
};

Rendu renderWith(size_t workerCount, size_t trackCount, double seconds) {
    ProcessGraph graph;
    graph.prepare(48000.0, 512);
    graph.setRenderThreadCount(workerCount);
    for (size_t t = 0; t < trackCount; ++t)
        graph.setTrackInstrument(t, machineMix()[t % machineMix().size()]);
    graph.setProject(buildLoadedProject(trackCount, machineMix()));
    Rendu rendu;
    rendu.audio = OfflineRenderer::render(graph, 48000.0, 512, seconds);
    rendu.segmentsParalleles = graph.parallelSpansRendered();
    return rendu;
}

} // namespace

// ---------------------------------------------------------------------------
// LE BANC DE THREADS, SEUL.
// ---------------------------------------------------------------------------

VSM_TEST(render_thread_pool_runs_every_job_exactly_once) {
    RenderThreadPool pool;
    pool.resize(4);

    // Chaque tâche marque sa case. « Exactement une fois » est ce qui compte :
    // une tâche exécutée deux fois doublerait une piste dans le mixage, et une
    // tâche oubliée en effacerait une.
    struct Ctx {
        std::vector<std::atomic<int>> hits;
        explicit Ctx(size_t n) : hits(n) {}
    };

    // Plusieurs rondes de suite, parce que le seul bug de concurrence sérieux
    // de ce banc vit à la FRONTIÈRE entre deux rondes : un travailleur qui
    // sort de la précédente pendant que la suivante se prépare.
    for (int ronde = 0; ronde < 200; ++ronde) {
        const size_t count = 1 + static_cast<size_t>(ronde % 37);
        Ctx ctx(count);
        for (auto& h : ctx.hits) h.store(0);
        pool.runParallel([](void* c, size_t i, size_t) {
            static_cast<Ctx*>(c)->hits[i].fetch_add(1);
        }, &ctx, count);
        for (size_t i = 0; i < count; ++i)
            VSM_ASSERT_EQ(ctx.hits[i].load(), 1);
    }
}

VSM_TEST(render_thread_pool_worker_ids_stay_within_range) {
    RenderThreadPool pool;
    pool.resize(3);

    // L'identifiant du thread sert à choisir un tampon de travail : hors
    // plage, il écrirait à côté. Le thread appelant est le 0.
    struct Ctx { std::atomic<size_t> maxId{0}; size_t limit = 0; std::atomic<int> bad{0}; };
    Ctx ctx;
    ctx.limit = pool.parallelism();
    pool.runParallel([](void* c, size_t, size_t workerId) {
        auto* x = static_cast<Ctx*>(c);
        if (workerId >= x->limit) x->bad.fetch_add(1);
        size_t previous = x->maxId.load();
        while (workerId > previous && !x->maxId.compare_exchange_weak(previous, workerId)) {}
    }, &ctx, 500);

    VSM_ASSERT_EQ(ctx.bad.load(), 0);
    VSM_ASSERT_EQ(pool.parallelism(), pool.workerCount() + 1);
}

VSM_TEST(render_thread_pool_without_workers_is_a_plain_loop) {
    RenderThreadPool pool; // aucun resize : zéro thread auxiliaire
    VSM_ASSERT_EQ(pool.workerCount(), size_t{0});
    VSM_ASSERT_EQ(pool.parallelism(), size_t{1});

    std::vector<int> order;
    struct Ctx { std::vector<int>* order; };
    Ctx ctx{&order};
    pool.runParallel([](void* c, size_t i, size_t workerId) {
        VSM_ASSERT_EQ(workerId, size_t{0});
        static_cast<Ctx*>(c)->order->push_back(static_cast<int>(i));
    }, &ctx, 8);

    // Sans travailleur, l'ordre est celui de la boucle -- littéralement la
    // même boucle qu'avant l'existence du banc.
    VSM_ASSERT_EQ(order.size(), size_t{8});
    for (int i = 0; i < 8; ++i) VSM_ASSERT_EQ(order[static_cast<size_t>(i)], i);
}

// ---------------------------------------------------------------------------
// LE GRAPHE : LE MULTICŒUR NE CHANGE PAS UN SEUL ÉCHANTILLON.
// ---------------------------------------------------------------------------
//
// C'est LA propriété qui rend la phase D8 défendable. Un moteur qui rendrait
// « à peu près pareil » selon le nombre de cœurs ferait mentir la règle
// d'ARCHITECTURE.md § 5 -- un export doit reproduire ce qu'on a entendu --
// dès qu'on changerait de machine, et personne ne saurait pourquoi.

VSM_TEST(process_graph_multicore_render_is_bit_identical) {
    const size_t tracks = 12;
    const double seconds = 0.6;

    const Rendu mono = renderWith(0, tracks, seconds);
    const Rendu multi = renderWith(4, tracks, seconds);

    // Les deux moitiés de la vérification : le chemin séquentiel n'a réveillé
    // personne, et le chemin parallèle a bien été emprunté.
    VSM_ASSERT_EQ(mono.segmentsParalleles, uint64_t{0});
    VSM_ASSERT(multi.segmentsParalleles > 0);

    VSM_ASSERT_EQ(mono.audio.left.size(), multi.audio.left.size());
    VSM_ASSERT(!mono.audio.left.empty());

    // Le rendu doit aussi contenir quelque chose : deux silences seraient
    // identiques sans rien prouver.
    float crete = 0.0f;
    for (float s : mono.audio.left) crete = std::max(crete, std::abs(s));
    VSM_ASSERT(crete > 0.001f);

    for (size_t i = 0; i < mono.audio.left.size(); ++i) {
        // AU BIT PRÈS, et non « à epsilon près » : le mixage est fait dans le
        // même ordre par les deux chemins, donc les mêmes additions
        // flottantes dans le même ordre. Une tolérance masquerait exactement
        // le défaut qu'on cherche.
        VSM_ASSERT_EQ(mono.audio.left[i], multi.audio.left[i]);
        VSM_ASSERT_EQ(mono.audio.right[i], multi.audio.right[i]);
    }
}

VSM_TEST(process_graph_multicore_survives_thread_count_changes) {
    // Le nombre de threads se règle en cours de route (préférences,
    // changement de périphérique) : le rendu doit rester juste, et surtout
    // identique, quel que soit le nombre choisi.
    const size_t tracks = 8;
    const Rendu reference = renderWith(0, tracks, 0.4);
    for (size_t workers : {size_t{1}, size_t{2}, size_t{7}}) {
        const Rendu essai = renderWith(workers, tracks, 0.4);
        VSM_ASSERT(essai.segmentsParalleles > 0);
        VSM_ASSERT_EQ(reference.audio.left.size(), essai.audio.left.size());
        for (size_t i = 0; i < reference.audio.left.size(); ++i) {
            VSM_ASSERT_EQ(reference.audio.left[i], essai.audio.left[i]);
            VSM_ASSERT_EQ(reference.audio.right[i], essai.audio.right[i]);
        }
    }
}

VSM_TEST(process_graph_render_thread_count_is_what_was_asked) {
    ProcessGraph graph;
    graph.prepare(48000.0, 512);
    VSM_ASSERT_EQ(graph.renderThreadCount(), size_t{0}); // mono-cœur par défaut

    graph.setRenderThreadCount(3);
    VSM_ASSERT_EQ(graph.renderThreadCount(), size_t{3});

    // ZÉRO REMET LE GRAPHE DANS SON ÉTAT D'ORIGINE, threads détruits et
    // tampons rendus : c'est ce qui permet de dire qu'un utilisateur qui ne
    // veut pas du multicœur n'en paie rien.
    graph.setRenderThreadCount(0);
    VSM_ASSERT_EQ(graph.renderThreadCount(), size_t{0});

    // Et le plafond est un plafond, pas une suggestion.
    graph.setRenderThreadCount(1000);
    VSM_ASSERT_EQ(graph.renderThreadCount(), RenderThreadPool::kMaxWorkers);
    graph.setRenderThreadCount(0);
}

// ---------------------------------------------------------------------------
// LA CHAÎNE LATÉRALE : LE SEUL CAS OÙ LE PARALLÉLISME EST REFUSÉ.
// ---------------------------------------------------------------------------

VSM_TEST(process_graph_falls_back_to_one_core_when_a_sidechain_listens) {
    // Un effet qui écoute un départ lit ce que les pistes précédentes viennent
    // d'y VERSER : le calcul d'une piste dépend alors du mélange d'une autre,
    // et l'indépendance sur laquelle repose la répartition n'existe plus. Le
    // graphe doit s'en apercevoir tout seul et retomber sur un seul cœur --
    // pas produire un compresseur qui réagit une fois sur deux.
    vsm::sequencer::Project projet;
    projet.ticksPerQuarterNote = 480;
    uint64_t ids = 1;
    for (int i = 0; i < 8; ++i) {
        Track piste;
        piste.addNote(0, 1920, static_cast<uint8_t>(52 + 2 * i), 100, 0, ids);
        projet.tracks.push_back(piste);
    }
    vsm::sequencer::SendBusDescription bus;
    bus.name = "Ecoute";
    bus.effectType = "reverb";
    bus.preFader = true;
    projet.sends.push_back(bus);
    projet.tracks[7].setSendLevel(0, 1.0f);

    ProcessGraph graph;
    graph.prepare(48000.0, 512);
    graph.setRenderThreadCount(4);
    for (size_t t = 0; t < projet.tracks.size(); ++t)
        graph.setTrackInstrument(t, "vsm.minimoog");
    graph.setProject(projet);

    auto compresseur = std::make_shared<vsm::audio::effect::CompressorEffect>();
    compresseur->prepare(48000.0, 512);
    compresseur->setParameter(vsm::audio::effect::CompressorEffect::kSidechain, 1.0f);
    auto chaine = std::make_shared<ProcessGraph::EffectChain>();
    chaine->push_back(compresseur);
    graph.setTrackEffectChain(0, chaine);

    const RenderedAudio rendu = OfflineRenderer::render(graph, 48000.0, 512, 0.3);
    VSM_ASSERT(!rendu.left.empty());
    // Quatre threads existent bel et bien, et pourtant AUCUN segment n'est
    // passé par le chemin parallèle : c'est exactement ce qu'on veut voir.
    VSM_ASSERT_EQ(graph.renderThreadCount(), size_t{4});
    VSM_ASSERT_EQ(graph.parallelSpansRendered(), uint64_t{0});
}
