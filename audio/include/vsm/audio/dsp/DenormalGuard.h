#pragma once

// Un filtre résonant qui "traîne" en dénormale après une note relâchée peut
// faire chuter le CPU de façon spectaculaire (section 13 du cahier des
// charges : "éviter les locks... pas de garbage collection... denormals").
// Deux outils complémentaires :
//  - flushDenormalToZero() : neutralise explicitement une valeur, portable,
//    à utiliser dans le code des filtres/enveloppes eux-mêmes.
//  - ScopedNoDenormals : RAII, active le mode matériel FTZ/DAZ du CPU pour
//    toute la durée du scope (typiquement le callback audio entier), pour
//    couvrir aussi les calculs qu'on n'a pas neutralisés un par un.

#if defined(__SSE__) || defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86)
#include <pmmintrin.h>
#include <xmmintrin.h>
#define VSM_HAS_SSE_DENORMAL_CONTROL 1
#else
#define VSM_HAS_SSE_DENORMAL_CONTROL 0
#endif

namespace vsm::audio::dsp {

inline float flushDenormalToZero(float x) {
    constexpr float kThreshold = 1.0e-30f;
    return (x < kThreshold && x > -kThreshold) ? 0.0f : x;
}

/// RAII : active Flush-To-Zero (bit 15) + Denormals-Are-Zero (bit 6) du
/// registre MXCSR pour la durée du scope, puis restaure l'état précédent.
/// Sans effet (mais sûr) sur les architectures non-x86.
class ScopedNoDenormals {
public:
    ScopedNoDenormals() {
#if VSM_HAS_SSE_DENORMAL_CONTROL
        previousMxcsr_ = _mm_getcsr();
        _mm_setcsr(previousMxcsr_ | 0x8040u);
#endif
    }
    ~ScopedNoDenormals() {
#if VSM_HAS_SSE_DENORMAL_CONTROL
        _mm_setcsr(previousMxcsr_);
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
#if VSM_HAS_SSE_DENORMAL_CONTROL
    unsigned int previousMxcsr_ = 0;
#endif
};

} // namespace vsm::audio::dsp
