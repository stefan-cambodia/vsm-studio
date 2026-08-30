#include "vsm/audio/io/WavStreamReader.h"
#include "WavDecoding.h"
#include <algorithm>

namespace vsm::audio::io {

using namespace vsm::audio::io::detail;

WavStreamReader::OpenResult WavStreamReader::open(const std::string& path) {
    OpenResult result;
    auto reader = std::shared_ptr<WavStreamReader>(new WavStreamReader());
    reader->path_ = path;
    reader->stream_.open(path, std::ios::binary);
    if (!reader->stream_) { result.error = "impossible d'ouvrir : " + path; return result; }

    reader->stream_.seekg(0, std::ios::end);
    const int64_t taille = static_cast<int64_t>(reader->stream_.tellg());
    reader->stream_.seekg(0, std::ios::beg);
    if (taille < 44) { result.error = "fichier trop court pour un WAV : " + path; return result; }

    uint8_t entete[12];
    reader->stream_.read(reinterpret_cast<char*>(entete), 12);
    if (!tagMatches(entete, "RIFF") || !tagMatches(entete + 8, "WAVE")) {
        result.error = "en-tête RIFF/WAVE absent : " + path;
        return result;
    }

    // PARCOURS DES CHUNKS EN SAUTANT, PAS EN LISANT. Un WAV réel contient
    // souvent LIST, fact, smpl... avant `data`, et certains outils y écrivent
    // des mégaoctets de métadonnées. Les charger pour trouver l'audio serait
    // exactement ce que cette classe existe pour éviter.
    int64_t offset = 12;
    int64_t dataOffset = -1, dataSize = 0;
    bool formatVu = false;
    while (offset + 8 <= taille) {
        uint8_t chunk[8];
        reader->stream_.seekg(offset, std::ios::beg);
        reader->stream_.read(reinterpret_cast<char*>(chunk), 8);
        if (!reader->stream_) break;
        const int64_t chunkSize = static_cast<int64_t>(readU32LE(chunk + 4));
        const int64_t contenu = offset + 8;

        if (tagMatches(chunk, "fmt ") && chunkSize >= 16) {
            std::vector<uint8_t> fmt(static_cast<size_t>(std::min<int64_t>(chunkSize, 40)));
            reader->stream_.read(reinterpret_cast<char*>(fmt.data()),
                                  static_cast<std::streamsize>(fmt.size()));
            reader->formatCode_ = readU16LE(fmt.data());
            reader->channels_ = readU16LE(fmt.data() + 2);
            reader->sampleRate_ = static_cast<double>(readU32LE(fmt.data() + 4));
            reader->bitsPerSample_ = readU16LE(fmt.data() + 14);
            if (reader->formatCode_ == 0xFFFE && fmt.size() >= 26)
                reader->formatCode_ = readU16LE(fmt.data() + 24);
            formatVu = true;
        } else if (tagMatches(chunk, "data")) {
            dataOffset = contenu;
            // Un `data` déclarant plus que ce que contient le fichier est un
            // fichier tronqué : on lit ce qui existe réellement.
            dataSize = std::min(chunkSize, taille - contenu);
        }
        offset = contenu + chunkSize + (chunkSize % 2); // chunks alignés sur 2 octets
    }

    if (!formatVu || reader->channels_ == 0 || reader->bitsPerSample_ == 0) {
        result.error = "chunk fmt manquant ou invalide : " + path;
        return result;
    }
    if (reader->formatCode_ != 1 && reader->formatCode_ != 3) {
        result.error = "format compressé non pris en charge (code "
                       + std::to_string(reader->formatCode_) + ") : " + path;
        return result;
    }
    const uint16_t bits = reader->bitsPerSample_;
    if (bits != 8 && bits != 16 && bits != 24 && bits != 32 && bits != 64) {
        result.error = std::to_string(bits) + " bits par échantillon : non pris en charge";
        return result;
    }
    if (dataOffset < 0) { result.error = "chunk data absent : " + path; return result; }

    reader->frameSize_ = static_cast<size_t>(bits / 8) * reader->channels_;
    if (reader->frameSize_ == 0) { result.error = "taille de trame nulle : " + path; return result; }
    reader->dataOffset_ = dataOffset;
    reader->frames_ = dataSize / static_cast<int64_t>(reader->frameSize_);
    if (reader->sampleRate_ <= 0.0) reader->sampleRate_ = 44100.0;

    result.reader = std::move(reader);
    return result;
}

int64_t WavStreamReader::readFrames(int64_t startFrame, int64_t count, float* left, float* right) {
    if (count <= 0 || left == nullptr || right == nullptr) return 0;
    std::fill(left, left + count, 0.0f);
    std::fill(right, right + count, 0.0f);
    if (startFrame >= frames_) return 0;

    const int64_t depart = std::max<int64_t>(0, startFrame);
    const int64_t decalage = depart - startFrame;   // trames de silence avant le fichier
    const int64_t disponibles = std::min(count - decalage, frames_ - depart);
    if (disponibles <= 0) return 0;

    std::lock_guard<std::mutex> verrou(mutex_);
    const size_t octets = static_cast<size_t>(disponibles) * frameSize_;
    if (scratch_.size() < octets) scratch_.resize(octets);
    stream_.clear();
    stream_.seekg(dataOffset_ + depart * static_cast<int64_t>(frameSize_), std::ios::beg);
    stream_.read(reinterpret_cast<char*>(scratch_.data()), static_cast<std::streamsize>(octets));
    const int64_t lues = static_cast<int64_t>(stream_.gcount()) / static_cast<int64_t>(frameSize_);

    const size_t parEchantillon = static_cast<size_t>(bitsPerSample_ / 8);
    for (int64_t i = 0; i < lues; ++i) {
        const uint8_t* base = scratch_.data() + static_cast<size_t>(i) * frameSize_;
        const float g = convertSample(base, bitsPerSample_, formatCode_);
        // UN FICHIER MONO REMPLIT LES DEUX CANAUX : le graphe ne connaît que du
        // stéréo, et laisser la droite à zéro ferait sonner toutes les prises
        // mono entièrement à gauche.
        const float d = channels_ >= 2
            ? convertSample(base + parEchantillon, bitsPerSample_, formatCode_) : g;
        left[decalage + i] = g;
        right[decalage + i] = d;
    }
    return lues;
}

} // namespace vsm::audio::io
