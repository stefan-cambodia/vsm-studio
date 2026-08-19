#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Construit des fichiers SMF programmatiquement (pas de .mid binaire versionné
// dans le dépôt) : la fixture documente elle-même exactement ce qu'elle
// contient, et il n'y a aucun risque d'erreur de calcul manuel de longueur
// de chunk (calculée automatiquement depuis la taille réelle des buffers).

namespace vsm::test::fixtures {

inline void pushU16BE(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}

inline void pushU32BE(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}

inline void pushVLQ(std::vector<uint8_t>& v, uint32_t value) {
    uint32_t buffer = value & 0x7F;
    while ((value >>= 7) > 0) {
        buffer <<= 8;
        buffer |= 0x80 | (value & 0x7F);
    }
    while (true) {
        v.push_back(static_cast<uint8_t>(buffer & 0xFF));
        if (buffer & 0x80) buffer >>= 8; else break;
    }
}

inline std::vector<uint8_t> wrapChunk(const char id[4], const std::vector<uint8_t>& body) {
    std::vector<uint8_t> chunk;
    chunk.reserve(8 + body.size()); // évite un faux positif -Wstringop-overflow sur GCC 16+
    chunk.insert(chunk.end(), id, id + 4);
    pushU32BE(chunk, static_cast<uint32_t>(body.size()));
    chunk.insert(chunk.end(), body.begin(), body.end());
    return chunk;
}

/// Fichier Type 1, 2 pistes :
///  - piste 0 (conductor) : tempo 120 BPM, signature 4/4
///  - piste 1 ("Bass") : CC1=64 au tick 0, note C3(36) vel100 durée 1 noire,
///    pitch bend, note E3(40) vel90 durée 1 croche
inline std::vector<uint8_t> buildTestSmf(uint16_t ppq = 480) {
    std::vector<uint8_t> conductor;
    pushVLQ(conductor, 0);
    conductor.insert(conductor.end(), {0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20}); // 500000 us/qn = 120 BPM
    pushVLQ(conductor, 0);
    conductor.insert(conductor.end(), {0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08}); // 4/4
    pushVLQ(conductor, 0);
    conductor.insert(conductor.end(), {0xFF, 0x2F, 0x00});

    std::vector<uint8_t> bass;
    pushVLQ(bass, 0);
    std::string name = "Bass";
    bass.push_back(0xFF); bass.push_back(0x03);
    pushVLQ(bass, static_cast<uint32_t>(name.size()));
    bass.insert(bass.end(), name.begin(), name.end());

    pushVLQ(bass, 0);
    bass.insert(bass.end(), {0xB0, 0x01, 0x40}); // CC1 (mod wheel) = 64, canal 0

    pushVLQ(bass, 0);
    bass.insert(bass.end(), {0x90, 36, 100}); // Note On C3 vel100

    pushVLQ(bass, ppq);
    bass.insert(bass.end(), {0x80, 36, 64}); // Note Off C3, une noire plus tard

    pushVLQ(bass, 0);
    bass.insert(bass.end(), {0xE0, 0x00, 0x60}); // pitch bend

    pushVLQ(bass, 0);
    bass.insert(bass.end(), {0x90, 40, 90}); // Note On E3 vel90

    pushVLQ(bass, static_cast<uint32_t>(ppq / 2));
    bass.insert(bass.end(), {0x80, 40, 64}); // Note Off E3, une croche plus tard

    pushVLQ(bass, 0);
    bass.insert(bass.end(), {0xFF, 0x2F, 0x00});

    std::vector<uint8_t> header;
    pushU16BE(header, 1); // format 1
    pushU16BE(header, 2); // ntrks
    pushU16BE(header, ppq);

    std::vector<uint8_t> out;
    auto mthd = wrapChunk("MThd", header);
    auto mtrk0 = wrapChunk("MTrk", conductor);
    auto mtrk1 = wrapChunk("MTrk", bass);
    out.insert(out.end(), mthd.begin(), mthd.end());
    out.insert(out.end(), mtrk0.begin(), mtrk0.end());
    out.insert(out.end(), mtrk1.begin(), mtrk1.end());
    return out;
}

} // namespace vsm::test::fixtures
