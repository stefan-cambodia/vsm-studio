#include "vsm/audio/plugin/IMultisampleBank.h"

namespace vsm::audio::plugin {

using vsm::audio::io::SampleBuffer;
using vsm::audio::io::SampleBufferPtr;
using vsm::audio::io::WavFileReader;

SampleBufferPtr MultisampleSampleCache::get(const std::string& path, std::string& outError) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = entries_.find(path);
        if (found != entries_.end()) return found->second;
    }

    // Lecture HORS DU VERROU : décoder deux mégaoctets en tenant le mutex
    // bloquerait tout autre chargement, et rien n'oblige à sérialiser des
    // lectures indépendantes. Le doublon possible (deux threads lisant le même
    // fichier en même temps) coûte une lecture de trop, jamais une incohérence.
    auto result = WavFileReader::readFile(path);
    if (!result.success) { outError = result.error; return nullptr; }
    if (result.buffer.empty()) { outError = "fichier sans échantillon : " + path; return nullptr; }

    const size_t frames = result.buffer.numFrames();
    const size_t channels = result.buffer.isStereo() ? 2u : 1u;
    const size_t taille = frames * channels * sizeof(float);
    auto buffer = std::make_shared<const SampleBuffer>(std::move(result.buffer));

    std::lock_guard<std::mutex> lock(mutex_);
    if (bytes_ + taille > kBudgetBytes) { entries_.clear(); bytes_ = 0; }
    auto [it, inserted] = entries_.emplace(path, buffer);
    if (inserted) bytes_ += taille;
    return it->second;
}

void MultisampleSampleCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    bytes_ = 0;
}

size_t MultisampleSampleCache::bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_;
}

} // namespace vsm::audio::plugin
