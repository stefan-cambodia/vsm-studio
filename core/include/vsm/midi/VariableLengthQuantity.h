#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace vsm::midi {

/// Lit une Variable Length Quantity (delta-time MIDI) à partir de [ptr, end).
/// Avance `ptr` du nombre d'octets consommés. Lève std::runtime_error si le
/// flux est tronqué ou si la VLQ dépasse 4 octets (fichier corrompu).
inline uint32_t readVLQ(const uint8_t*& ptr, const uint8_t* end) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        if (ptr >= end)
            throw std::runtime_error("MIDI: fin de flux inattendue pendant la lecture d'une VLQ");
        uint8_t byte = *ptr++;
        value = (value << 7) | (byte & 0x7F);
        if (!(byte & 0x80))
            return value;
    }
    throw std::runtime_error("MIDI: VLQ invalide (plus de 4 octets)");
}

/// Écrit `value` au format Variable Length Quantity, en ajoutant les octets
/// résultants à la fin de `out`.
inline void writeVLQ(std::vector<uint8_t>& out, uint32_t value) {
    uint32_t buffer = value & 0x7F;
    while ((value >>= 7) > 0) {
        buffer <<= 8;
        buffer |= 0x80 | (value & 0x7F);
    }
    while (true) {
        out.push_back(static_cast<uint8_t>(buffer & 0xFF));
        if (buffer & 0x80)
            buffer >>= 8;
        else
            break;
    }
}

} // namespace vsm::midi
