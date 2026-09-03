#include "vsm/audio/io/AudioTrackLoader.h"
#include "vsm/audio/dsp/SincResampler.h"
#include "vsm/audio/engine/SampleStore.h"
#include "vsm/audio/io/WavFileReader.h"
#include "vsm/audio/io/WavStreamReader.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::io {

namespace {

/// Un canal vers une nouvelle fréquence, par le noyau fenêtré de D12.1 — le
/// MÊME noyau que la diffusion depuis le disque, pour que la même prise
/// sonne pareil quelle que soit sa durée.
std::vector<float> reechantillonner(const std::vector<float>& source, double ratio,
                                     size_t framesCibles) {
    vsm::audio::dsp::SincResampler noyau;
    noyau.prepare(ratio);
    return noyau.resample(source, framesCibles);
}

} // namespace

AudioTrackLoadResult loadAudioTrack(const std::string& path, double sessionSampleRate,
                                    AudioLoadPolicy policy) {
    AudioTrackLoadResult result;
    result.sessionSampleRate = sessionSampleRate;
    if (sessionSampleRate <= 0.0) {
        result.error = "fréquence de session invalide";
        return result;
    }

    // ON REGARDE L'EN-TÊTE AVANT DE DÉCIDER (D8.2), et surtout avant de lire les
    // deux cents mégaoctets qu'on cherche justement à ne pas lire. Ouvrir en
    // diffusion ne coûte qu'un `seek` : c'est ce qui permet de choisir sur la
    // durée réelle du fichier plutôt que sur ce que le projet en dit.
    if (policy != AudioLoadPolicy::ForceResident) {
        auto entete = WavStreamReader::open(path);
        if (entete.reader) {
            const double duree = entete.reader->sampleRate() > 0.0
                ? static_cast<double>(entete.reader->frames()) / entete.reader->sampleRate() : 0.0;
            if (duree > kStreamAboveSeconds) {
                using vsm::audio::engine::StreamedSampleStore;
                const auto mode = policy == AudioLoadPolicy::Offline
                    ? StreamedSampleStore::Mode::Blocking
                    : StreamedSampleStore::Mode::Realtime;
                std::string erreur;
                auto store = StreamedSampleStore::open(path, sessionSampleRate, mode, erreur);
                if (store) {
                    auto source = std::make_shared<vsm::audio::engine::AudioTrackSource>();
                    source->samples = store;
                    result.fileSampleRate = store->fileSampleRate();
                    result.resampled = store->resampled();
                    result.streamed = true;
                    result.residentBytes = store->residentBytes();
                    result.source = std::move(source);
                    result.success = true;
                    return result;
                }
                // LA DIFFUSION A ÉCHOUÉ : on retombe sur la lecture complète
                // plutôt que de refuser la piste. Mieux vaut un projet lourd
                // qu'un projet muet -- et l'erreur, elle, sera dite par le
                // chemin résident s'il échoue à son tour.
            }
        }
    }

    const WavFileReader::Result lu = WavFileReader::readFile(path);
    if (!lu.success) {
        result.error = "audio illisible (" + path + ") : " + lu.error;
        return result;
    }
    if (lu.buffer.empty()) {
        result.error = "audio vide : " + path;
        return result;
    }

    result.fileSampleRate = lu.buffer.sampleRate;
    auto source = std::make_shared<vsm::audio::engine::AudioTrackSource>();

    const bool memeFrequence = std::abs(lu.buffer.sampleRate - sessionSampleRate) < 1e-6;
    if (memeFrequence) {
        source->setMemorySamples(lu.buffer.left,
                                  lu.buffer.isStereo() ? lu.buffer.right : lu.buffer.left);
    } else {
        const double ratio = lu.buffer.sampleRate / sessionSampleRate;
        const size_t frames = static_cast<size_t>(
            std::llround(static_cast<double>(lu.buffer.numFrames()) / ratio));
        source->setMemorySamples(
            reechantillonner(lu.buffer.left, ratio, frames),
            reechantillonner(lu.buffer.isStereo() ? lu.buffer.right : lu.buffer.left, ratio, frames));
        result.resampled = true;
    }

    result.residentBytes = source->residentBytes();
    result.source = std::move(source);
    result.success = true;
    return result;
}

} // namespace vsm::audio::io
