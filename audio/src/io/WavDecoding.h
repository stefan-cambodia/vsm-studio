#pragma once
#include <cstdint>
#include <cstring>

/// LA CONVERSION D'UN ÉCHANTILLON WAV VERS LE FLOTTANT, à un seul endroit.
///
/// Elle vivait dans l'anonymat de `WavFileReader.cpp`, ce qui allait tant qu'un
/// seul lecteur existait. La diffusion depuis le disque (D8.2) en ajoute un
/// second, qui lit des MORCEAUX du même fichier : deux copies de ce code
/// finiraient par diverger sur un cas de bord -- l'extension de signe du 24
/// bits, ou le fait que le PCM 8 bits est le seul format NON signé -- et la
/// même prise sonnerait autrement selon qu'elle est chargée entière ou
/// diffusée. C'est exactement le genre d'écart qu'on ne rattache jamais à sa
/// cause.
namespace vsm::audio::io::detail {

inline uint16_t readU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline bool tagMatches(const uint8_t* p, const char* tag) { return std::memcmp(p, tag, 4) == 0; }

/// Conversion vers float, une fois pour toutes. Les entiers signés sont
/// ramenés à [-1, 1] par leur pleine échelle NÉGATIVE (32768 pour du 16 bits),
/// convention usuelle : elle garantit qu'aucun échantillon ne dépasse 1,0 et
/// évite un écrêtage à la relecture d'un fichier déjà au maximum.
inline float convertSample(const uint8_t* data, uint16_t bitsPerSample, uint16_t formatCode) {
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

} // namespace vsm::audio::io::detail
