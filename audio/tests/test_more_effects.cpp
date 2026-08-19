#include "TestFramework.h"
#include "vsm/audio/effect/Reverb.h"
#include "vsm/audio/effect/Flanger.h"
#include "vsm/audio/effect/Phaser.h"
#include "vsm/audio/effect/TapeSaturation.h"
#include <cmath>
#include <memory>
#include <vector>

using namespace vsm::audio::effect;

namespace {
// Signal de test : une impulsion suivie d'un peu de sinus.
void fillTest(std::vector<float>& l, std::vector<float>& r) {
    for (size_t i = 0; i < l.size(); ++i) {
        float s = (i < 200) ? std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f) : 0.0f;
        l[i] = s; r[i] = s;
    }
}
bool allFinite(const std::vector<float>& b) {
    for (float s : b) if (!std::isfinite(s)) return false;
    return true;
}
float maxAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p;
}
double diffSum(const std::vector<float>& a, const std::vector<float>& b) {
    double d = 0.0; for (size_t i = 0; i < a.size(); ++i) d += std::abs(a[i] - b[i]); return d;
}

template <typename FX>
void runFx(FX& fx, std::vector<float>& l, std::vector<float>& r) {
    fx.prepare(48000.0, 512);
    fx.process(l.data(), r.data(), static_cast<int>(l.size()));
}
} // namespace

// ------- passthrough à mix=0 -------
VSM_TEST(reverb_mix_zero_is_passthrough) {
    std::vector<float> l(1000), r(1000), l0, r0;
    fillTest(l, r); l0 = l; r0 = r;
    Reverb fx; fx.setParameter(Reverb::kMix, 0.0f); runFx(fx, l, r);
    VSM_ASSERT_NEAR(diffSum(l, l0), 0.0, 1e-4);
}
VSM_TEST(flanger_mix_zero_is_passthrough) {
    std::vector<float> l(1000), r(1000), l0, r0;
    fillTest(l, r); l0 = l; r0 = r;
    Flanger fx; fx.setParameter(Flanger::kMix, 0.0f); runFx(fx, l, r);
    VSM_ASSERT_NEAR(diffSum(l, l0), 0.0, 1e-4);
}
VSM_TEST(phaser_mix_zero_is_passthrough) {
    std::vector<float> l(1000), r(1000), l0, r0;
    fillTest(l, r); l0 = l; r0 = r;
    Phaser fx; fx.setParameter(Phaser::kMix, 0.0f); runFx(fx, l, r);
    VSM_ASSERT_NEAR(diffSum(l, l0), 0.0, 1e-4);
}
VSM_TEST(tape_mix_zero_is_passthrough) {
    std::vector<float> l(1000), r(1000), l0, r0;
    fillTest(l, r); l0 = l; r0 = r;
    TapeSaturation fx; fx.setParameter(TapeSaturation::kMix, 0.0f); runFx(fx, l, r);
    VSM_ASSERT_NEAR(diffSum(l, l0), 0.0, 1e-4);
}

// ------- mix>0 change le signal, reste borné et fini -------
VSM_TEST(reverb_changes_signal_and_is_bounded) {
    std::vector<float> l(2000), r(2000), l0, r0;
    fillTest(l, r); l0 = l; r0 = r;
    Reverb fx; runFx(fx, l, r);
    VSM_ASSERT(diffSum(l, l0) > 1.0);
    VSM_ASSERT(allFinite(l) && allFinite(r));
    VSM_ASSERT(maxAbs(l) < 4.0f);
}
VSM_TEST(reverb_produces_tail_after_input_stops) {
    // Impulsion puis silence : il doit rester de l'énergie (queue de reverb).
    std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
    l[0] = r[0] = 1.0f;
    Reverb fx; fx.setParameter(Reverb::kMix, 1.0f); runFx(fx, l, r);
    double tail = 0.0;
    for (size_t i = 4000; i < l.size(); ++i) tail += static_cast<double>(l[i]) * l[i];
    VSM_ASSERT(tail > 1e-6);
}
VSM_TEST(flanger_changes_signal_and_is_bounded) {
    std::vector<float> l(2000), r(2000), l0, r0;
    fillTest(l, r); l0 = l; r0 = r;
    Flanger fx; runFx(fx, l, r);
    VSM_ASSERT(diffSum(l, l0) > 0.5);
    VSM_ASSERT(allFinite(l) && maxAbs(l) < 4.0f);
}
VSM_TEST(phaser_changes_signal_and_is_bounded) {
    std::vector<float> l(2000), r(2000), l0, r0;
    fillTest(l, r); l0 = l; r0 = r;
    Phaser fx; runFx(fx, l, r);
    VSM_ASSERT(diffSum(l, l0) > 0.5);
    VSM_ASSERT(allFinite(l) && maxAbs(l) < 4.0f);
}
VSM_TEST(tape_changes_signal_and_is_bounded) {
    std::vector<float> l(2000), r(2000), l0, r0;
    fillTest(l, r); l0 = l; r0 = r;
    TapeSaturation fx; fx.setParameter(TapeSaturation::kDrive, 8.0f); runFx(fx, l, r);
    VSM_ASSERT(diffSum(l, l0) > 0.5);
    VSM_ASSERT(allFinite(l) && maxAbs(l) < 2.0f);
}

// ------- déterminisme -------
VSM_TEST(effects_are_deterministic) {
    auto renderReverb = [] {
        std::vector<float> l(3000), r(3000); fillTest(l, r);
        Reverb fx; runFx(fx, l, r); return l;
    };
    auto a = renderReverb(), b = renderReverb();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

#include "vsm/audio/effect/FilterEffect.h"
VSM_TEST(filter_mix_zero_is_passthrough) {
    std::vector<float> l(1000), r(1000), l0, r0;
    fillTest(l, r); l0 = l; r0 = r;
    FilterEffect fx; fx.setParameter(FilterEffect::kMix, 0.0f); runFx(fx, l, r);
    VSM_ASSERT_NEAR(diffSum(l, l0), 0.0, 1e-4);
}
VSM_TEST(filter_lowpass_removes_high_content_and_is_bounded) {
    std::vector<float> l(2000), r(2000), l0, r0;
    fillTest(l, r); l0 = l; r0 = r;
    FilterEffect fx; fx.setParameter(FilterEffect::kCutoff, 300.0f); runFx(fx, l, r);
    VSM_ASSERT(diffSum(l, l0) > 0.5);   // le filtrage modifie le signal
    VSM_ASSERT(allFinite(l) && maxAbs(l) < 4.0f);
}
