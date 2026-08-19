#include "vsm/audio/io/WavFileReader.h"
#include <cstring>
#include <fstream>

namespace vsm::audio::io {

namespace {

uint16_t readU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool tagMatches(const uint8_t* p, const char* tag) { return std::memcmp(p, tag, 4) == 0; }

/// Conversion vers float, une fois pour toutes. Les entiers signés sont
/// ramenés à [-1, 1] par leur pleine échelle NÉGATIVE (32768 pour du 16 bits),
/// convention usuelle : elle garantit qu'aucun échantillon ne dépasse 1,0 et
/// évite un écrêtage à la relecture d'un fichier déjà au maximum.
float convertSample(const uint8_t* data, uint16_t bitsPerSample, uint16_t formatCode) {
    if (formatCode == 3) { // IEEE float
        if (bitsPerSample == 32) {
            float value = 0.0f;
            std::memcpy(&value, data, sizeof(float));
            return value;
        }
        double value = 0.0;
        std::memcpy(&value, data, sizeof(double));
        return static_cast<float>(value);
    }

    switch (bitsPerSample) {
        case 8: // le PCM 8 bits est NON signé, seul cas du format
            return (static_cast<float>(data[0]) - 128.0f) / 128.0f;
        case 16: {
            const int16_t value = static_cast<int16_t>(readU16LE(data));
            return static_cast<float>(value) / 32768.0f;
        }
        case 24: {
            int32_t value = static_cast<int32_t>(data[0]) | (static_cast<int32_t>(data[1]) << 8) |
                            (static_cast<int32_t>(data[2]) << 16);
            if (value & 0x800000) value |= ~0xFFFFFF; // extension de signe sur 24 bits
            return static_cast<float>(value) / 8388608.0f;
        }
        case 32: {
            const int32_t value = static_cast<int32_t>(readU32LE(data));
            return static_cast<float>(value) / 2147483648.0f;
        }
        default:
            return 0.0f;
    }
}

} // namespace

WavFileReader::Result WavFileReader::read(const std::vector<uint8_t>& bytes) {
    Result result;
    if (bytes.size() < 44) { result.error = "fichier trop court pour un WAV"; return result; }
    if (!tagMatches(bytes.data(), "RIFF") || !tagMatches(bytes.data() + 8, "WAVE")) {
        result.error = "en-tête RIFF/WAVE absent";
        return result;
    }

    uint16_t formatCode = 0, channels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    const uint8_t* audioData = nullptr;
    size_t audioSize = 0;

    // Parcours des chunks : un WAV réel contient souvent LIST, fact, smpl...
    // entre `fmt ` et `data`. Sauter les chunks inconnus est la seule façon de
    // lire les fichiers produits par de vrais outils.
    size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const uint8_t* header = bytes.data() + offset;
        const uint32_t chunkSize = readU32LE(header + 4);
        const size_t contentOffset = offset + 8;
        if (contentOffset + chunkSize > bytes.size() && !tagMatches(header, "data")) {
            result.error = "chunk déclaré plus grand que le fichier";
            return result;
        }

        if (tagMatches(header, "fmt ")) {
            if (chunkSize < 16) { result.error = "chunk fmt incomplet"; return result; }
            const uint8_t* fmt = bytes.data() + contentOffset;
            formatCode = readU16LE(fmt);
            channels = readU16LE(fmt + 2);
            sampleRate = readU32LE(fmt + 4);
            bitsPerSample = readU16LE(fmt + 14);
            if (formatCode == 0xFFFE && chunkSize >= 40) {
                // WAVE_FORMAT_EXTENSIBLE : le vrai format est dans le GUID,
                // dont les deux premiers octets reprennent le code classique.
                formatCode = readU16LE(fmt + 24);
            }
        } else if (tagMatches(header, "data")) {
            audioData = bytes.data() + contentOffset;
            // Un `data` déclarant plus que ce que contient le fichier est un
            // fichier tronqué : on lit ce qui existe réellement plutôt que de
            // sortir des limites -- mais on ne prétend pas que c'est complet.
            audioSize = std::min(static_cast<size_t>(chunkSize), bytes.size() - contentOffset);
        }

        offset = contentOffset + chunkSize + (chunkSize % 2); // les chunks sont alignés sur 2 octets
    }

    if (formatCode == 0 || channels == 0 || bitsPerSample == 0) {
        result.error = "chunk fmt manquant ou invalide";
        return result;
    }
    if (formatCode != 1 && formatCode != 3) {
        result.error = "format compressé non pris en charge (code " + std::to_string(formatCode) + ")";
        return result;
    }
    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 &&
        bitsPerSample != 32 && bitsPerSample != 64) {
        result.error = std::to_string(bitsPerSample) + " bits par échantillon : non pris en charge";
        return result;
    }
    if (audioData == nullptr) { result.error = "chunk data absent"; return result; }

    const size_t bytesPerSample = bitsPerSample / 8;
    const size_t frameSize = bytesPerSample * channels;
    if (frameSize == 0) { result.error = "taille de trame nulle"; return result; }
    const size_t frames = audioSize / frameSize;

    result.buffer.sampleRate = sampleRate > 0 ? static_cast<double>(sampleRate) : 44100.0;
    result.buffer.left.resize(frames);
    if (channels >= 2) result.buffer.right.resize(frames);

    for (size_t frame = 0; frame < frames; ++frame) {
        const uint8_t* base = audioData + frame * frameSize;
        result.buffer.left[frame] = convertSample(base, bitsPerSample, formatCode);
        if (channels >= 2)
            result.buffer.right[frame] = convertSample(base + bytesPerSample, bitsPerSample, formatCode);
    }

    result.success = true;
    return result;
}

WavFileReader::Result WavFileReader::readFile(const std::string& path) {
    Result result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) { result.error = "impossible d'ouvrir : " + path; return result; }

    stream.seekg(0, std::ios::end);
    const std::streampos size = stream.tellg();
    stream.seekg(0, std::ios::beg);
    if (size <= 0) { result.error = "fichier vide : " + path; return result; }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    result = read(bytes);
    if (result.success) result.buffer.sourcePath = path;
    return result;
}

} // namespace vsm::audio::io
