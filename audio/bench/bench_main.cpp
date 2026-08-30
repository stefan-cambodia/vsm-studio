#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/LadderFilterZDF.h"
#include "vsm/audio/dsp/LadderFilterZDFx4.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/engine/RenderThreadPool.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Banc de mesure CPU (Phase 6 -- "profiling", ARCHITECTURE.md § 10).
//
// POURQUOI CET OUTIL AVANT TOUTE OPTIMISATION : la suite de la Phase 6 (SIMD,
// oversampling sélectif) consiste à rendre le moteur plus rapide. Optimiser
// sans mesurer, c'est optimiser au hasard -- et dans un moteur audio, la
// question n'est même pas "combien de temps ça prend" mais "est-ce que le
// PIRE bloc tient dans le budget temps réel", puisqu'un seul bloc en retard
// s'entend (clic/dropout) alors qu'une moyenne excellente ne prouve rien.
// Ce banc mesure donc la moyenne ET la queue de distribution (p99, pire cas),
// exprimées en pourcentage du budget d'un bloc.
//
// Ce qu'il n'est PAS : un profileur de fonctions (pas de granularité
// intra-DSP -- pour ça, perf/callgrind sur ce même binaire) ni un jugement de
// qualité sonore. Il ne remplace pas non plus les tests : il ne vérifie rien,
// il mesure.
//
// PIÈGE MESURÉ (et qui a d'abord produit des chiffres incohérents ici) : sur
// un CPU HYBRIDE -- Intel Core Ultra, Apple Silicon, ARM big.LITTLE -- le
// thread de mesure migre entre cœurs P et cœurs E, dont les performances
// diffèrent d'un facteur 2 et plus. Deux exécutions de suite du même binaire
// donnaient un rapport de 2,4x sur un effet que la mesure ne touchait même
// pas. D'où deux précautions : ÉPINGLER le processus sur un cœur (`taskset -c
// 2 ./vsm_audio_bench` sous Linux) et comparer les colonnes `min` (le coût du
// DSP sans interférence) plutôt que les moyennes.
//
// Usage : cmake -B build-bench -DVSM_BUILD_BENCH=ON -DVSM_BUILD_TESTS=OFF
//         cmake --build build-bench -j
//         taskset -c 2 ./build-bench/audio/vsm_audio_bench
// ---------------------------------------------------------------------------

using namespace vsm::audio;
using Clock = std::chrono::steady_clock;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;
constexpr double kBlockBudgetMs = 1000.0 * kBlockSize / kSampleRate; // 10,667 ms
constexpr int kNumBlocks = 1000; // ~10,7 s de signal par mesure

/// Statistiques d'une série de blocs, en millisecondes.
struct BlockStats {
    double minMs = 0.0, meanMs = 0.0, p50Ms = 0.0, p99Ms = 0.0, maxMs = 0.0;
    double calibrationNs = 1.0; // coût d'une opération étalon, mesuré juste avant cette ligne

    /// Coût exprimé en "unités étalon" (combien d'opérations étalon coûte un
    /// bloc). C'est la SEULE grandeur comparable entre deux exécutions : les
    /// millisecondes, elles, dépendent de la fréquence à laquelle tournait le
    /// cœur à cet instant précis -- et sur un portable, elle bouge de 30 %
    /// entre le début et la fin d'un même banc (turbo puis throttling).
    double relative() const { return minMs * 1.0e6 / calibrationNs; }

    static BlockStats from(std::vector<double> samples) {
        BlockStats s;
        if (samples.empty()) return s;
        double total = 0.0;
        for (double v : samples) total += v;
        s.meanMs = total / static_cast<double>(samples.size());
        std::sort(samples.begin(), samples.end());
        s.minMs = samples.front();
        s.p50Ms = samples[samples.size() / 2];
        s.p99Ms = samples[static_cast<size_t>(0.99 * static_cast<double>(samples.size() - 1))];
        s.maxMs = samples.back();
        return s;
    }
};

/// % d'un cœur consommé pour tenir le temps réel : 100 % = le bloc met
/// exactement son propre temps de lecture à être calculé (aucune marge).
double coreLoadPercent(double ms) { return 100.0 * ms / kBlockBudgetMs; }

void printHeader(const char* title) {
    std::printf("\n%s\n", title);
    std::printf("%-34s %9s %9s %9s %9s %9s %8s %10s\n", "", "min (ms)", "moy", "p50", "p99", "max", "%cœur", "étalons");
    std::printf("%s\n", std::string(105, '-').c_str());
}

void printRow(const std::string& label, const BlockStats& s) {
    std::printf("%-34s %9.4f %9.4f %9.4f %9.4f %9.4f %7.1f%% %10.0f\n",
                label.c_str(), s.minMs, s.meanMs, s.p50Ms, s.p99Ms, s.maxMs,
                coreLoadPercent(s.p50Ms), s.relative());
}

/// Étalon : coût moyen, en nanosecondes, d'une opération DSP triviale et
/// stable (une enveloppe ADSR). Mesuré JUSTE AVANT chaque ligne du banc, il
/// capture l'état réel du cœur à cet instant (fréquence, throttling, cœur P
/// ou E) et permet d'exprimer les coûts en unités indépendantes de cet état.
double measureCalibrationNs();

/// Empêche le compilateur d'éliminer un calcul dont le résultat n'est pas lu.
/// Sans ça, une mesure peut chronométrer... rien du tout.
[[maybe_unused]] volatile float g_sink = 0.0f;
void consume(const std::vector<float>& buffer) {
    float acc = 0.0f;
    for (float v : buffer) acc += v;
    g_sink = acc;
}

// --- Machines --------------------------------------------------------------

/// Accord de `voices` notes tenues : mesure la machine dans le régime qui
/// coûte le plus cher (toutes ses voix actives), pas sur une note isolée.
/// Les boîtes à rythmes reçoivent leurs propres notes (kick/charleston/caisse).
std::vector<plugin::MidiNoteEvent> chordFor(const std::string& pluginId, int voices) {
    std::vector<plugin::MidiNoteEvent> events;
    const bool drums = (pluginId == "vsm.tr808" || pluginId == "vsm.tr909");
    const uint8_t drumNotes[] = {36, 38, 42, 46, 45, 49, 39, 50};
    for (int i = 0; i < voices; ++i) {
        const uint8_t note = drums ? drumNotes[static_cast<size_t>(i) % 8]
                                   : static_cast<uint8_t>(48 + 4 * i);
        events.push_back({plugin::MidiNoteEvent::Kind::NoteOn, i * 16, 0, note, 100});
    }
    return events;
}

BlockStats benchMachine(const std::string& pluginId, int voices) {
    auto synth = plugin::PluginRegistry::instance().create(pluginId);
    if (!synth) return {};
    synth->initialize(kSampleRate, kBlockSize);

    std::vector<float> left(kBlockSize, 0.0f), right(kBlockSize, 0.0f);
    const auto events = chordFor(pluginId, voices);

    // Rodage : premiers blocs hors mesure (caches froids, première allocation
    // de tables, montée des enveloppes) -- ils ne représentent pas le régime
    // permanent qu'on cherche à caractériser.
    synth->process(events.data(), static_cast<int>(events.size()), left.data(), right.data(), kBlockSize);
    for (int i = 0; i < 20; ++i)
        synth->process(nullptr, 0, left.data(), right.data(), kBlockSize);

    const double calibration = measureCalibrationNs();
    std::vector<double> samples;
    samples.reserve(kNumBlocks);
    for (int b = 0; b < kNumBlocks; ++b) {
        // Les notes sont retriggées régulièrement pour que les machines
        // percussives (enveloppes courtes) restent réellement actives.
        const bool retrigger = (b % 40) == 0;
        const auto t0 = Clock::now();
        synth->process(retrigger ? events.data() : nullptr, retrigger ? static_cast<int>(events.size()) : 0,
                       left.data(), right.data(), kBlockSize);
        const auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        consume(left);
    }
    BlockStats stats = BlockStats::from(std::move(samples));
    stats.calibrationNs = calibration;
    return stats;
}

// --- Effets ----------------------------------------------------------------

BlockStats benchEffect(const std::string& effectId) {
    auto fx = effect::EffectFactory::create(effectId);
    if (!fx) return {};
    fx->prepare(kSampleRate, kBlockSize);

    std::vector<float> left(kBlockSize), right(kBlockSize);
    for (int i = 0; i < kBlockSize; ++i) {
        const float phase = static_cast<float>(i) * 0.01f;
        left[static_cast<size_t>(i)] = 0.3f * std::sin(phase);
        right[static_cast<size_t>(i)] = 0.3f * std::sin(phase * 1.5f);
    }

    for (int i = 0; i < 20; ++i) fx->process(left.data(), right.data(), kBlockSize);

    const double calibration = measureCalibrationNs();
    std::vector<double> samples;
    samples.reserve(kNumBlocks);
    for (int b = 0; b < kNumBlocks; ++b) {
        const auto t0 = Clock::now();
        fx->process(left.data(), right.data(), kBlockSize);
        const auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        consume(left);
    }
    BlockStats stats = BlockStats::from(std::move(samples));
    stats.calibrationNs = calibration;
    return stats;
}

// --- Graphe complet --------------------------------------------------------

/// Projet réaliste : `trackCount` pistes qui jouent chacune un accord tenu,
/// chacune sur une machine différente -- c'est la charge que verra vraiment le
/// callback audio, inserts et mixage compris.
vsm::sequencer::Project buildProject(size_t trackCount, const std::vector<std::string>& machines,
                                     int voicesPerTrack = 4) {
    vsm::sequencer::Project project;
    const uint16_t ppq = 480;
    project.ticksPerQuarterNote = ppq;
    uint64_t idCounter = 1;
    for (size_t t = 0; t < trackCount; ++t) {
        vsm::sequencer::Track track;
        track.name = machines[t % machines.size()];
        track.channel = static_cast<uint8_t>(t % 16);
        for (int n = 0; n < voicesPerTrack; ++n)
            track.addNote(0, ppq * 32, static_cast<uint8_t>(40 + 3 * n), 100, 0, idCounter);
        project.tracks.push_back(track);
    }
    return project;
}

/// LA CHAÎNE D'INSERTS D'UNE PISTE « CHARGÉE ». Trois effets, dont la
/// distorsion qui suréchantillonne -- de loin le plus cher du parc, et
/// précisément celui qu'on met sur une guitare ou une basse. Une piste de
/// mixage réel porte à peu près ça ; c'est ce que « 32 pistes chargées » veut
/// dire dans le critère de la phase D8.
std::shared_ptr<const engine::ProcessGraph::EffectChain> loadedChain() {
    auto chaine = std::make_shared<engine::ProcessGraph::EffectChain>();
    for (const char* id : {"eq", "compressor", "distortion"}) {
        auto fx = effect::EffectFactory::create(id);
        if (!fx) continue;
        fx->prepare(kSampleRate, kBlockSize);
        chaine->push_back(std::move(fx));
    }
    return chaine;
}

BlockStats benchGraph(size_t trackCount, const std::vector<std::string>& machines,
                      bool masterBusEnabled, size_t renderThreads = 0,
                      int voicesPerTrack = 4, bool withInserts = false) {
    engine::ProcessGraph graph;
    graph.prepare(kSampleRate, kBlockSize);
    graph.setRenderThreadCount(renderThreads);
    for (size_t t = 0; t < trackCount; ++t) {
        graph.setTrackInstrument(t, machines[t % machines.size()]);
        if (withInserts) graph.setTrackEffectChain(t, loadedChain());
    }
    graph.setProject(buildProject(trackCount, machines, voicesPerTrack));
    graph.masterBus().setEnabled(masterBusEnabled);
    graph.seekSeconds(0.0);
    graph.setPlaying(true);

    std::vector<float> left(kBlockSize, 0.0f), right(kBlockSize, 0.0f);
    for (int i = 0; i < 20; ++i) graph.processBlock(left.data(), right.data(), kBlockSize);

    const double calibration = measureCalibrationNs();
    std::vector<double> samples;
    samples.reserve(kNumBlocks);
    for (int b = 0; b < kNumBlocks; ++b) {
        const auto t0 = Clock::now();
        graph.processBlock(left.data(), right.data(), kBlockSize);
        const auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        consume(left);
    }
    BlockStats stats = BlockStats::from(std::move(samples));
    stats.calibrationNs = calibration;
    return stats;
}

// --- Le gain du rendu multicœur (D8.1) ------------------------------------
//
// CE QU'ON MESURE ICI N'EST PAS « EST-CE PLUS RAPIDE » mais « combien de
// pistes tiennent ». Le critère de la phase D8 est un chiffre, pas une
// impression : trente-deux pistes chargées doivent tenir dans le budget d'un
// bloc, et le rapport entre le coût mono-cœur et le coût multicœur doit être
// dit, pas supposé.
//
// LA COLONNE QUI COMPTE EST p99, ET NON `min`. Ailleurs dans ce banc, `min`
// est le bon chiffre : il donne le coût du DSP sans interférence. Ici,
// l'interférence EST le sujet -- un travailleur préempté, une ronde qui traîne
// -- et c'est la queue de distribution qui dit si le son passe. Un multicœur
// dont la moyenne est excellente et le p99 au-dessus du budget produit des
// clics ; un mono-cœur régulièrement à 90 % n'en produit pas.
void benchMulticore(const std::vector<std::string>& mix) {
    const size_t recommande = engine::ProcessGraph::recommendedRenderThreadCount();
    std::printf("\n== Rendu multicœur (D8.1) -- 32 pistes CHARGÉES ==\n");
    std::printf("Charge par piste : 8 voix tenues + 3 inserts (EQ, compresseur, distorsion).\n");
    std::printf("Cœurs annoncés par la machine : %u ; threads auxiliaires recommandés : %zu\n",
                std::thread::hardware_concurrency(), recommande);

    auto mesure = [&mix](size_t threads) {
        return benchGraph(32, mix, false, threads, /*voicesPerTrack=*/8, /*withInserts=*/true);
    };

    printHeader("");
    const BlockStats mono = mesure(0);
    printRow("32 pistes, mono-cœur (référence)", mono);

    std::vector<std::pair<size_t, BlockStats>> resultats;
    for (size_t threads : {size_t{1}, size_t{2}, size_t{3}, size_t{4}, size_t{6},
                           size_t{8}, size_t{12}, size_t{16}}) {
        if (threads > engine::RenderThreadPool::kMaxWorkers) continue;
        if (threads + 1 > std::thread::hardware_concurrency()) continue;
        const BlockStats s = mesure(threads);
        resultats.emplace_back(threads, s);
        printRow("32 pistes, " + std::to_string(threads) + " thread(s) auxiliaire(s)", s);
    }
    if (recommande > 0)
        printRow("32 pistes, réglage recommandé (" + std::to_string(recommande) + ")",
                 mesure(recommande));

    std::printf("\nGain, mesuré sur le p99 (ce qui décide s'il y a un clic) :\n");
    for (const auto& [threads, s] : resultats) {
        const double gain = s.p99Ms > 0.0 ? mono.p99Ms / s.p99Ms : 0.0;
        std::printf("  %2zu thread(s) auxiliaire(s) : x%.2f   (%.1f %% d'un cœur -> %.1f %%)%s\n",
                    threads, gain, coreLoadPercent(mono.p99Ms), coreLoadPercent(s.p99Ms),
                    coreLoadPercent(s.p99Ms) >= 100.0 ? "   *** NE TIENT PAS ***" : "");
    }
    if (coreLoadPercent(mono.p99Ms) >= 100.0)
        std::printf("  (mono-cœur : %.1f %% du budget -- NE TIENT PAS)\n", coreLoadPercent(mono.p99Ms));
}

// --- Briques DSP élémentaires ---------------------------------------------
//
// Faute de `perf`/`callgrind` sur cette machine, la granularité intra-DSP
// s'obtient autrement : mesurer chaque brique isolément, au coût par
// ÉCHANTILLON, puis confronter la somme au coût réel d'une voix. Ce qui
// dépasse le compte est ce qu'il faut aller regarder.

/// Coût moyen d'une opération, en nanosecondes, sur `kBrickIterations` appels.
template <typename Fn>
double benchBrickNs(Fn fn) {
    constexpr int kBrickIterations = 200000;
    for (int i = 0; i < 1000; ++i) fn(static_cast<float>(i) * 0.001f); // rodage
    float acc = 0.0f;
    const auto t0 = Clock::now();
    for (int i = 0; i < kBrickIterations; ++i)
        acc += fn(static_cast<float>(i) * 0.0001f);
    const auto t1 = Clock::now();
    g_sink = acc;
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / kBrickIterations;
}

double measureCalibrationNs() {
    dsp::AdsrEnvelope env;
    env.setSampleRate(kSampleRate);
    env.noteOn();
    constexpr int kIterations = 200000;
    float acc = 0.0f;
    for (int i = 0; i < 20000; ++i) acc += env.nextSample(); // rodage
    const auto t0 = Clock::now();
    for (int i = 0; i < kIterations; ++i) acc += env.nextSample();
    const auto t1 = Clock::now();
    g_sink = acc;
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / kIterations;
}

void benchBricks() {
    std::printf("\n== Briques DSP (coût par échantillon, ns) ==\n");
    std::printf("%-46s %10s\n", "", "ns/appel");
    std::printf("%s\n", std::string(58, '-').c_str());

    auto row = [](const char* label, double ns) { std::printf("%-46s %10.2f\n", label, ns); };

    row("std::pow(2.0f, x)  [conversion demi-tons -> Hz]", benchBrickNs([](float x) { return std::pow(2.0f, x); }));
    row("std::exp2f(x)      [même calcul, autre appel]", benchBrickNs([](float x) { return std::exp2f(x); }));
    row("std::tan(x)        [coefficient de filtre]", benchBrickNs([](float x) { return std::tan(0.3f + x * 0.1f); }));
    row("std::sin(x)        [opérateur FM, LFO]", benchBrickNs([](float x) { return std::sin(x); }));
    row("std::tanh(x)       [saturation]", benchBrickNs([](float x) { return std::tanh(x); }));

    dsp::BandLimitedOscillator osc;
    osc.setSampleRate(kSampleRate);
    osc.setFrequency(220.0f);
    osc.setWaveform(dsp::Waveform::Saw);
    row("BandLimitedOscillator::nextSample() (saw)", benchBrickNs([&osc](float) { return osc.nextSample(); }));

    dsp::LadderFilterZDF ladder;
    ladder.setSampleRate(kSampleRate);
    ladder.setCutoffHz(1200.0f);
    ladder.setResonance(1.5f);
    row("LadderFilterZDF::process() (4 pôles)", benchBrickNs([&ladder](float x) { return ladder.process(x * 0.5f); }));
    row("LadderFilterZDF::setCutoffHz() + process()", benchBrickNs([&ladder](float x) {
        ladder.setCutoffHz(1200.0f + x);
        return ladder.process(x * 0.5f);
    }));

    // Filtre ladder vectorisé : le coût est celui de QUATRE filtres, donc on
    // affiche aussi le coût ramené à une voix -- c'est ce chiffre-là qui se
    // compare à la ligne scalaire juste au-dessus.
    dsp::LadderFilterZDFx4 ladderX4;
    ladderX4.setSampleRate(kSampleRate);
    for (size_t lane = 0; lane < dsp::LadderFilterZDFx4::kLanes; ++lane) {
        ladderX4.setCutoffHz(lane, 1200.0f + 100.0f * static_cast<float>(lane));
        ladderX4.setResonance(lane, 1.5f);
    }
    const double x4Total = benchBrickNs([&ladderX4](float x) {
        return ladderX4.process(dsp::SimdFloat4(x * 0.5f)).lane(0);
    });
    row("LadderFilterZDFx4::process() (4 voix d'un coup)", x4Total);
    row("  ... ramené à UNE voix", x4Total / 4.0);

    dsp::StateVariableFilter svf;
    svf.setSampleRate(kSampleRate);
    svf.setCutoffHz(200.0f);
    row("StateVariableFilter::process()", benchBrickNs([&svf](float x) { return svf.process(x * 0.5f); }));
    row("StateVariableFilter::setCutoffHz() + process()", benchBrickNs([&svf](float x) {
        svf.setCutoffHz(200.0f + x);
        return svf.process(x * 0.5f);
    }));

    dsp::AdsrEnvelope env;
    env.setSampleRate(kSampleRate);
    env.noteOn();
    row("AdsrEnvelope::nextSample()", benchBrickNs([&env](float) { return env.nextSample(); }));

    dsp::AnalogDrift drift;
    drift.setSampleRate(kSampleRate);
    drift.setAmount(0.5f);
    row("AnalogDrift::nextValue()", benchBrickNs([&drift](float) { return drift.nextValue(); }));
}

} // namespace

int main() {
    plugin::registerBuiltInPlugins();

    std::printf("Banc CPU du moteur audio -- %.0f Hz, blocs de %d échantillons\n", kSampleRate, kBlockSize);
    std::printf("Budget temps réel d'un bloc : %.3f ms (100 %% = un cœur entier consommé)\n", kBlockBudgetMs);
    std::printf("%d blocs mesurés par ligne, après rodage.\n", kNumBlocks);
#ifndef NDEBUG
    std::printf("\n*** ATTENTION : binaire compilé SANS optimisation (NDEBUG absent).\n"
                "    Les chiffres ci-dessous ne veulent rien dire -- recompilez en\n"
                "    RelWithDebInfo ou Release avant d'en tirer la moindre conclusion.\n");
#endif

    const std::vector<std::pair<std::string, int>> machines = {
        {"vsm.testtone", 8}, {"vsm.minimoog", 1}, {"vsm.tb303", 1},  {"vsm.sh101", 1},
        {"vsm.ms20", 1},     {"vsm.arpodyssey", 2}, {"vsm.juno106", 6}, {"vsm.prophet", 5},
        {"vsm.jupiter8", 8}, {"vsm.dx7", 8},      {"vsm.tr808", 8}, {"vsm.tr909", 8},
    };

    benchBricks();

    printHeader("== Machines (toutes voix actives, coût par bloc) ==");
    std::vector<std::pair<std::string, BlockStats>> machineResults;
    for (const auto& [id, voices] : machines) {
        const BlockStats s = benchMachine(id, voices);
        machineResults.emplace_back(id + " (" + std::to_string(voices) + " voix)", s);
    }
    std::sort(machineResults.begin(), machineResults.end(),
              [](const auto& a, const auto& b) { return a.second.minMs > b.second.minMs; });
    for (const auto& [label, s] : machineResults) printRow(label, s);

    printHeader("== Effets d'insert (bloc stéréo) ==");
    std::vector<std::pair<std::string, BlockStats>> effectResults;
    for (const auto& info : effect::EffectFactory::available())
        effectResults.emplace_back(info.displayName, benchEffect(info.id));
    std::sort(effectResults.begin(), effectResults.end(),
              [](const auto& a, const auto& b) { return a.second.minMs > b.second.minMs; });
    for (const auto& [label, s] : effectResults) printRow(label, s);

    printHeader("== Graphe complet (pistes jouant en parallèle) ==");
    const std::vector<std::string> mix = {"vsm.juno106", "vsm.dx7", "vsm.jupiter8", "vsm.minimoog",
                                          "vsm.prophet", "vsm.tb303", "vsm.tr909", "vsm.sh101"};
    for (size_t tracks : {size_t{1}, size_t{4}, size_t{8}, size_t{16}})
        printRow(std::to_string(tracks) + " pistes (bus master bypassé)", benchGraph(tracks, mix, false));
    printRow("8 pistes + bus master ACTIF", benchGraph(8, mix, true));

    benchMulticore(mix);

    std::printf("\nLecture :\n"
                "  - min  : le coût \"propre\" du DSP, sans interférence -- c'est LUI qu'il faut\n"
                "           comparer entre deux versions du code (avant/après optimisation).\n"
                "  - p99/max : ce que risque vraiment le thread audio ; un bloc au-dessus du\n"
                "           budget (%.3f ms) s'entend (clic). Un max isolé vient souvent de\n"
                "           l'ordonnanceur, pas du DSP -- comparer p99 et max avant de conclure.\n"
                "  - %%cœur : charge médiane rapportée au budget d'un bloc.\n"
                "  - étalons : le coût du bloc exprimé en \"combien d'enveloppes ADSR\", étalon\n"
                "           mesuré juste avant chaque ligne. C'est la colonne à comparer entre\n"
                "           deux exécutions : elle ne dépend pas de la fréquence du cœur.\n", kBlockBudgetMs);
    return 0;
}
