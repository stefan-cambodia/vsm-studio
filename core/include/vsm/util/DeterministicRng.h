#pragma once
#include <cstdint>

namespace vsm::util {

/// SplitMix64 : PRNG rapide, déterministe, bien distribué.
/// Utilisé partout où le projet a besoin d'aléatoire REPRODUCTIBLE :
/// humanisation (Phase 1), dérive analogique / voice-to-voice variation
/// (Phase 3+). Ne JAMAIS remplacer par rand()/std::random_device dans le
/// chemin de traitement : la reproductibilité d'une session en dépend
/// (voir section 8 du cahier des charges : "ANALOG CHARACTER").
class DeterministicRng {
public:
    explicit DeterministicRng(uint64_t seed) : state_(seed) {}

    uint64_t nextUInt64() {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    /// Flottant uniforme dans [-1, 1]
    float nextBipolar() {
        return nextUnipolar() * 2.0f - 1.0f;
    }

    /// Flottant uniforme dans [0, 1]
    float nextUnipolar() {
        uint64_t r = nextUInt64();
        return static_cast<float>(r >> 11) / static_cast<float>(1ULL << 53);
    }

private:
    uint64_t state_;
};

/// Combine une graine globale de session avec l'identifiant d'un objet
/// (note, voix, oscillateur...) pour dériver un flux pseudo-aléatoire
/// indépendant et stable : deux appels avec les mêmes arguments donnent
/// toujours le même flux, quel que soit l'ordre d'itération, ce qui permet
/// de rejouer une session à l'identique (édition, undo/redo, export...).
inline uint64_t deriveSeed(uint64_t globalSeed, uint64_t objectId) {
    uint64_t x = globalSeed ^ (objectId * 0xD6E8FEB86659FD93ULL);
    x ^= x >> 32;
    x *= 0xD6E8FEB86659FD93ULL;
    x ^= x >> 32;
    return x;
}

} // namespace vsm::util
