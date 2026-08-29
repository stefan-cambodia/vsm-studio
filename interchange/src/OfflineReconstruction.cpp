#include "vsm/interchange/OfflineReconstruction.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/engine/AutomationLane.h"
#include "vsm/interchange/EffectDescription.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <sstream>

namespace vsm::interchange {

using vsm::audio::engine::OfflineRenderer;
using vsm::audio::engine::ProcessGraph;

std::string RenderResult::summary() const {
    std::ostringstream out;
    if (!success) return "échec : " + error;
    out << "rendu " << renderedSeconds << " s (" << framesWritten << " échantillons), "
        << tracksWithInstrument << "/" << trackCount << " piste(s) sonorisée(s), pic "
        << peakLevel;
    for (const auto& warning : warnings) out << "\n  " << warning;
    return out.str();
}

RenderResult renderBundleToWav(const LoadedBundle& bundle, const std::string& wavPath,
                                const RenderOptions& options) {
    RenderResult result;
    result.trackCount = bundle.project.tracks.size();

    if (options.sampleRate <= 0.0 || options.blockSize <= 0) {
        result.error = "fréquence d'échantillonnage ou taille de bloc invalide";
        return result;
    }

    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });

    ProcessGraph graph;
    graph.prepare(options.sampleRate, options.blockSize);
    graph.setProject(bundle.project);

    for (size_t i = 0; i < bundle.project.tracks.size() && i < ProcessGraph::kMaxTracks; ++i) {
        const std::string& pluginId = bundle.project.tracks[i].instrumentId;
        if (pluginId.empty()) {
            result.warnings.push_back("Piste " + std::to_string(i) + " (" + bundle.project.tracks[i].name +
                                       ") : aucun instrument, elle restera silencieuse");
            continue;
        }
        graph.setTrackInstrument(i, pluginId);
        auto* instrument = graph.trackInstrument(i);
        if (instrument == nullptr) {
            result.warnings.push_back("Piste " + std::to_string(i) + " : instrument \"" + pluginId +
                                       "\" indisponible");
            continue;
        }
        ++result.tracksWithInstrument;

        // Presets : appliqués APRÈS l'assignation de l'instrument, et jamais
        // en silence -- ce que la machine ne sait pas faire est rapporté.
        auto preset = bundle.presetsByTrack.find(i);
        if (preset == bundle.presetsByTrack.end()) continue;
        const PresetApplyReport applyReport = applyPreset(preset->second, *instrument, pluginId);
        if (applyReport.unsupportedCount() > 0 || applyReport.clampedCount() > 0)
            result.warnings.push_back("Piste " + std::to_string(i) + " : " + applyReport.summary());

        // ÉCHANTILLONS. Chargés après le preset, et avant toute lecture : la
        // lecture de fichiers n'a rien à faire dans le thread audio.
        //
        // Sans cette étape, un projet utilisant le sampler se chargeait
        // proprement, sans le moindre avertissement... et rendait du silence.
        // C'est exactement le genre de panne que ce projet refuse : une piste
        // muette doit dire pourquoi elle est muette.
        const SampleLoadReport sampleReport =
            applyPresetSamples(preset->second, *instrument, bundle.folderPath);
        if (!sampleReport.failures.empty())
            result.warnings.push_back("Piste " + std::to_string(i) + " : " + sampleReport.summary());
    }

    // INSERTS. Ils étaient décrits dans `project.json` et jamais posés sur le
    // graphe : un projet portant une réverbération se rendait sans elle, sans
    // le moindre avertissement. Le fichier rendu ne correspondait donc pas au
    // projet, et rien ne permettait de s'en apercevoir autrement qu'à
    // l'oreille.
    for (size_t i = 0; i < bundle.project.tracks.size() && i < ProcessGraph::kMaxTracks; ++i) {
        const auto& described = bundle.project.tracks[i].effects;
        if (described.empty()) continue;
        auto chain = std::make_shared<ProcessGraph::EffectChain>();
        for (const auto& entry : described) {
            auto effect = vsm::audio::effect::EffectFactory::create(entry.type);
            if (!effect) {
                // Type inconnu : nommé, jamais remplacé par un autre effet.
                result.warnings.push_back("Piste " + std::to_string(i) + " : effet « " + entry.type +
                                           " » inconnu, non appliqué");
                continue;
            }
            effect->prepare(options.sampleRate, options.blockSize);
            const EffectApplyReport applyReport = applyEffectDescription(entry, *effect);
            for (const auto& unknown : applyReport.unknownParameters)
                result.warnings.push_back("Piste " + std::to_string(i) + " : effet « " + entry.type +
                                           " », réglage inconnu « " + unknown + " »");
            chain->push_back(std::move(effect));
        }
        graph.setTrackEffectChain(i, chain);
    }

    // TRANCHE MASTER. Même histoire : décrite dans le projet depuis qu'elle
    // s'y écrit, et sans effet sur le rendu tant que personne ne la reposait.
    if (!bundle.project.masterParameters.empty())
        applyMasterDescription(bundle.project.masterParameters, graph.masterBus());

    // AUTOMATION. Les courbes du document ciblent des identités SÉMANTIQUES ;
    // le moteur, lui, parle en ParamId. La résolution se fait ici, une fois,
    // et jamais en silence : une courbe qui vise un paramètre que la machine
    // n'a pas est RAPPORTÉE, pas ignorée -- une automation muette qui ne dit
    // pas pourquoi est exactement le genre de panne que ce projet refuse.
    std::vector<vsm::audio::engine::AutomationLane> automationLanes;
    for (size_t i = 0; i < bundle.document.tracks.size() && i < ProcessGraph::kMaxTracks; ++i) {
        const auto& documentTrack = bundle.document.tracks[i];
        if (documentTrack.automation.empty()) continue;
        if (i >= bundle.project.tracks.size()) continue;
        const std::string& pluginId = bundle.project.tracks[i].instrumentId;
        if (pluginId.empty()) {
            result.warnings.push_back("Piste " + std::to_string(i) +
                                       " : automation sans instrument, ignorée");
            continue;
        }
        const SemanticProfile profile = buildSemanticProfile(pluginId);
        for (const auto& lane : documentTrack.automation) {
            const ParameterDescriptor* descriptor = profile.findBySemanticId(lane.parameter);
            if (descriptor == nullptr) {
                result.warnings.push_back("Piste " + std::to_string(i) + " : automation « " +
                                           lane.parameter + " » : la machine n'a pas ce paramètre");
                continue;
            }
            vsm::audio::engine::AutomationLane engineLane;
            engineLane.targetTrackIndex = i;
            engineLane.targetParam = descriptor->paramId;
            for (const auto& point : lane.points) {
                // Bornée à la plage RÉELLE du paramètre : une valeur hors
                // bornes serait écrêtée par la machine de toute façon, mais
                // l'écrêter ici garde l'interpolation dans le vrai espace.
                const float value = std::clamp(point.value, descriptor->minimum, descriptor->maximum);
                engineLane.addPoint(static_cast<vsm::midi::Tick>(point.tick), value,
                                     point.step ? vsm::audio::engine::AutomationCurve::Step
                                                : vsm::audio::engine::AutomationCurve::Linear);
            }
            automationLanes.push_back(std::move(engineLane));
        }
    }
    if (!automationLanes.empty())
        graph.setAutomationLanes(std::move(automationLanes));

    // Durée : jusqu'à la dernière note, plus une queue pour ne pas couper les
    // résonances. Un projet vide produit tout de même un fichier valide (la
    // queue), plutôt qu'un WAV de zéro échantillon que certains lecteurs
    // refusent d'ouvrir.
    double duration = options.durationSeconds;
    if (duration <= 0.0) {
        const double lastNoteSeconds = bundle.project.ticksToSeconds(bundle.project.lastUsedTick());
        duration = lastNoteSeconds + std::max(0.0, options.tailSeconds);
    }
    if (duration <= 0.0) duration = std::max(0.1, options.tailSeconds);

    graph.seekSeconds(0.0);
    graph.setPlaying(true);
    const auto rendered = OfflineRenderer::render(graph, options.sampleRate, options.blockSize, duration);
    graph.setPlaying(false);

    for (size_t i = 0; i < rendered.left.size(); ++i)
        result.peakLevel = std::max(result.peakLevel,
                                     std::max(std::abs(rendered.left[i]), std::abs(rendered.right[i])));

    try {
        vsm::audio::io::WavFileWriter::writeFile(rendered.left.data(), rendered.right.data(),
                                                  rendered.numFrames(), options.sampleRate,
                                                  options.format, wavPath);
    } catch (const std::exception& e) {
        result.error = std::string("écriture WAV impossible : ") + e.what();
        return result;
    }

    result.success = true;
    result.renderedSeconds = duration;
    result.framesWritten = rendered.numFrames();
    return result;
}

RenderResult renderProjectFolderToWav(const std::string& folderPath, const std::string& wavPath,
                                       const RenderOptions& options) {
    BundleLoadResult loaded = loadProjectBundle(folderPath);
    if (!loaded.success) {
        RenderResult result;
        result.error = loaded.error;
        return result;
    }
    RenderResult result = renderBundleToWav(loaded.bundle, wavPath, options);
    // Les avertissements du CHARGEMENT (preset manquant, machine absente)
    // comptent autant que ceux du rendu : ils sont réunis dans un seul rapport,
    // pour qu'un script Python n'ait qu'un endroit à lire.
    result.warnings.insert(result.warnings.begin(), loaded.warnings.begin(), loaded.warnings.end());
    return result;
}

} // namespace vsm::interchange
