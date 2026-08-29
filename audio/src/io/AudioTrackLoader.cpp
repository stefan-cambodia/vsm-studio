#include "vsm/audio/io/AudioTrackLoader.h"
#include "vsm/audio/io/WavFileReader.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::io {

namespace {

/// Interpolation linéaire d'un canal vers une nouvelle fréquence.
std::vector<float> reechantillonner(const std::vector<float>& source, double ratio,
                                     size_t framesCibles) {
    std::vector<float> sortie(framesCibles, 0.0f);
    if (source.empty()) return sortie;
    const double dernier = static_cast<double>(source.size() - 1);
    for (size_t i = 0; i < framesCibles; ++i) {
        const double position = std::min(static_cast<double>(i) * ratio, dernier);
        const size_t bas = static_cast<size_t>(position);
        const size_t haut = std::min(bas + 1, source.size() - 1);
        const float fraction = static_cast<float>(position - static_cast<double>(bas));
        sortie[i] = source[bas] * (1.0f - fraction) + source[haut] * fraction;
    }
    return sortie;
}

} // namespace

AudioTrackLoadResult loadAudioTrack(const std::string& path, double sessionSampleRate) {
    AudioTrackLoadResult result;
    result.sessionSampleRate = sessionSampleRate;
    if (sessionSampleRate <= 0.0) {
        result.error = "fréquence de session invalide";
        return result;
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
        source->left = lu.buffer.left;
        source->right = lu.buffer.isStereo() ? lu.buffer.right : lu.buffer.left;
    } else {
        const double ratio = lu.buffer.sampleRate / sessionSampleRate;
        const size_t frames = static_cast<size_t>(
            std::llround(static_cast<double>(lu.buffer.numFrames()) / ratio));
        source->left = reechantillonner(lu.buffer.left, ratio, frames);
        source->right = reechantillonner(
            lu.buffer.isStereo() ? lu.buffer.right : lu.buffer.left, ratio, frames);
        result.resampled = true;
    }

    result.source = std::move(source);
    result.success = true;
    return result;
}

} // namespace vsm::audio::io
