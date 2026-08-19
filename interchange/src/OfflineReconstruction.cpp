#include "vsm/interchange/OfflineReconstruction.h"
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
