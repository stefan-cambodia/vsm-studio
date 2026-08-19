#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

// Vecteur de 4 flottants, portable : SSE2 sur x86, NEON sur ARM, et un repli
// scalaire partout ailleurs. Volontairement minimal -- uniquement ce dont les
// filtres du projet ont besoin.
//
// POURQUOI DU SIMD ICI, et pas ailleurs (voir ARCHITECTURE.md § 9 sexies) : le
// filtre ladder est la brique la plus chère du moteur (70 ns par échantillon,
// 7 machines sur 12 l'utilisent), et la mesure a montré qu'il est limité par
// la LATENCE, pas par la quantité de calcul -- chaque opération attend le
// résultat de la précédente. Lui retirer du travail ne l'accélère quasiment
// pas (mesuré : -30 % en supprimant les deux tanh ET en déroulant les
// boucles). La seule façon d'aller vraiment plus vite est de garder le
// processeur occupé pendant ces attentes, donc de traiter PLUSIEURS VOIX
// indépendantes en même temps, une par ligne SIMD.
//
// Le repli scalaire n'est pas un pis-aller de compilation : il garantit que le
// projet reste juste et testable sur une architecture sans SIMD, et sert de
// définition de référence de ce que les versions vectorisées doivent calculer.

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
  #include <emmintrin.h>
  #define VSM_SIMD_SSE2 1
#else
  #define VSM_SIMD_SSE2 0
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
  #include <arm_neon.h>
  #define VSM_SIMD_NEON 1
#else
  #define VSM_SIMD_NEON 0
#endif

namespace vsm::audio::dsp {

class SimdFloat4 {
public:
    static constexpr size_t kLanes = 4;

#if VSM_SIMD_SSE2
    using Register = __m128;
#elif VSM_SIMD_NEON
    using Register = float32x4_t;
#else
    struct Register { float v[4]; };
#endif

    SimdFloat4() : SimdFloat4(0.0f) {}

    explicit SimdFloat4(float value) {
#if VSM_SIMD_SSE2
        reg_ = _mm_set1_ps(value);
#elif VSM_SIMD_NEON
        reg_ = vdupq_n_f32(value);
#else
        for (auto& v : reg_.v) v = value;
#endif
    }

    explicit SimdFloat4(Register reg) : reg_(reg) {}

    static SimdFloat4 load(const float* aligned4) {
#if VSM_SIMD_SSE2
        return SimdFloat4(_mm_loadu_ps(aligned4));
#elif VSM_SIMD_NEON
        return SimdFloat4(vld1q_f32(aligned4));
#else
        Register r;
        for (size_t i = 0; i < kLanes; ++i) r.v[i] = aligned4[i];
        return SimdFloat4(r);
#endif
    }

    void store(float* destination4) const {
#if VSM_SIMD_SSE2
        _mm_storeu_ps(destination4, reg_);
#elif VSM_SIMD_NEON
        vst1q_f32(destination4, reg_);
#else
        for (size_t i = 0; i < kLanes; ++i) destination4[i] = reg_.v[i];
#endif
    }

    float lane(size_t index) const {
        float tmp[kLanes];
        store(tmp);
        return tmp[index];
    }

    SimdFloat4 operator+(const SimdFloat4& other) const {
#if VSM_SIMD_SSE2
        return SimdFloat4(_mm_add_ps(reg_, other.reg_));
#elif VSM_SIMD_NEON
        return SimdFloat4(vaddq_f32(reg_, other.reg_));
#else
        return apply(other, [](float a, float b) { return a + b; });
#endif
    }

    SimdFloat4 operator-(const SimdFloat4& other) const {
#if VSM_SIMD_SSE2
        return SimdFloat4(_mm_sub_ps(reg_, other.reg_));
#elif VSM_SIMD_NEON
        return SimdFloat4(vsubq_f32(reg_, other.reg_));
#else
        return apply(other, [](float a, float b) { return a - b; });
#endif
    }

    SimdFloat4 operator*(const SimdFloat4& other) const {
#if VSM_SIMD_SSE2
        return SimdFloat4(_mm_mul_ps(reg_, other.reg_));
#elif VSM_SIMD_NEON
        return SimdFloat4(vmulq_f32(reg_, other.reg_));
#else
        return apply(other, [](float a, float b) { return a * b; });
#endif
    }

    SimdFloat4 operator/(const SimdFloat4& other) const {
#if VSM_SIMD_SSE2
        return SimdFloat4(_mm_div_ps(reg_, other.reg_));
#elif VSM_SIMD_NEON
        return SimdFloat4(vdivq_f32(reg_, other.reg_));
#else
        return apply(other, [](float a, float b) { return a / b; });
#endif
    }

    SimdFloat4& operator+=(const SimdFloat4& other) { *this = *this + other; return *this; }
    SimdFloat4& operator*=(const SimdFloat4& other) { *this = *this * other; return *this; }

    SimdFloat4 minWith(const SimdFloat4& other) const {
#if VSM_SIMD_SSE2
        return SimdFloat4(_mm_min_ps(reg_, other.reg_));
#elif VSM_SIMD_NEON
        return SimdFloat4(vminq_f32(reg_, other.reg_));
#else
        return apply(other, [](float a, float b) { return a < b ? a : b; });
#endif
    }

    SimdFloat4 maxWith(const SimdFloat4& other) const {
#if VSM_SIMD_SSE2
        return SimdFloat4(_mm_max_ps(reg_, other.reg_));
#elif VSM_SIMD_NEON
        return SimdFloat4(vmaxq_f32(reg_, other.reg_));
#else
        return apply(other, [](float a, float b) { return a > b ? a : b; });
#endif
    }

    SimdFloat4 clamped(float low, float high) const {
        return maxWith(SimdFloat4(low)).minWith(SimdFloat4(high));
    }

private:
#if !VSM_SIMD_SSE2 && !VSM_SIMD_NEON
    template <typename Fn>
    SimdFloat4 apply(const SimdFloat4& other, Fn fn) const {
        Register r;
        for (size_t i = 0; i < kLanes; ++i) r.v[i] = fn(reg_.v[i], other.reg_.v[i]);
        return SimdFloat4(r);
    }
#endif

    Register reg_;
};

/// Approximation rationnelle de tanh (Padé d'ordre 7/6), vectorisable car
/// elle n'utilise que +, -, * et une division -- contrairement à std::tanh,
/// qui n'existe pas en version SIMD dans la bibliothèque standard et dont
/// l'appel briserait tout l'intérêt de la vectorisation.
///
/// Écart mesuré avec std::tanh, par plage (l'erreur n'est pas uniforme, et
/// n'annoncer que le meilleur chiffre serait trompeur) :
///   - |x| <= 1 : 1,2e-7    - |x| <= 2 : 2,0e-7   (le régime musical courant)
///   - |x| <= 4 : 1,5e-5    - au-delà  : 9,6e-5 au pire, vers |x| ~ 5
/// Le pire cas tombe là où tanh vaut déjà 0,9999 : le signal y est en pleine
/// saturation, et 1e-4 sur une valeur écrêtée est inaudible. Dans la plage qui
/// façonne réellement le timbre, l'écart est de -134 dBFS.
///
/// La FORME est préservée exactement -- fonction impaire, monotone, bornée à
/// ±1 -- ce que les tests vérifient explicitement : une saturation qui perdrait
/// l'une de ces trois propriétés s'entendrait immédiatement, contrairement à
/// une erreur d'amplitude minuscule.
inline float fastTanh(float x) {
    const float x2 = x * x;
    const float numerator = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float denominator = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    const float ratio = numerator / denominator;
    return std::clamp(ratio, -1.0f, 1.0f);
}

inline SimdFloat4 fastTanh(const SimdFloat4& x) {
    const SimdFloat4 x2 = x * x;
    const SimdFloat4 numerator = x * (SimdFloat4(135135.0f) + x2 * (SimdFloat4(17325.0f) + x2 * (SimdFloat4(378.0f) + x2)));
    const SimdFloat4 denominator = SimdFloat4(135135.0f) + x2 * (SimdFloat4(62370.0f) + x2 * (SimdFloat4(3150.0f) + x2 * SimdFloat4(28.0f)));
    return (numerator / denominator).clamped(-1.0f, 1.0f);
}

} // namespace vsm::audio::dsp
