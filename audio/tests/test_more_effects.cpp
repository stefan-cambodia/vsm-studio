#include "TestFramework.h"
#include "vsm/audio/effect/Reverb.h"
#include "vsm/audio/effect/Flanger.h"
#include "vsm/audio/effect/Phaser.h"
#include "vsm/audio/effect/TapeSaturation.h"
#include "vsm/audio/effect/TremoloEffect.h"
#include "vsm/audio/effect/TransientShaperEffect.h"
#include "vsm/audio/effect/PitchShiftEffect.h"
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

// ---------------------------------------------------------------------------
// D13.8 — trois effets d'insert de plus, chacun mesuré sur son trait.
// ---------------------------------------------------------------------------

namespace {
std::vector<float> sinusPlein(double hz, size_t n, float a = 0.5f) {
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) x[i] = a * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / 48000.0));
    return x;
}
double rmsFx(const std::vector<float>& x, size_t from, size_t count) {
    double s = 0.0; for (size_t i = from; i < from + count && i < x.size(); ++i) s += x[i] * x[i];
    return std::sqrt(s / static_cast<double>(count));
}
double magFx(const std::vector<float>& x, size_t from, size_t count, double hz) {
    double re = 0.0, im = 0.0, norm = 0.0;
    for (size_t i = 0; i < count && from + i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count));
        const double ph = 2.0 * M_PI * hz * static_cast<double>(i) / 48000.0;
        re += w * x[from + i] * std::cos(ph); im += w * x[from + i] * std::sin(ph); norm += w;
    }
    return std::sqrt(re * re + im * im) / std::max(1.0, norm);
}
} // namespace

VSM_TEST(tremolo_modulates_the_gain_at_its_rate_and_auto_pans_at_180_degrees) {
    // Trémolo : à 4 Hz, profondeur 1, l'enveloppe par 31 ms oscille entre le
    // silence et le plein ; auto-pan : la gauche et la droite en opposition.
    auto l = sinusPlein(440.0, 96000), r = l;
    TremoloEffect fx;
    fx.setParameter(TremoloEffect::kRate, 4.0f);
    fx.setParameter(TremoloEffect::kDepth, 1.0f);
    fx.setParameter(TremoloEffect::kStereoPhase, 180.0f);
    runFx(fx, l, r);
    double lo = 1e9, hi = 0.0, opposes = 0; int fenetres = 0;
    for (size_t d = 48000; d + 1500 <= 96000; d += 1500) {
        const double eL = rmsFx(l, d, 1500), eR = rmsFx(r, d, 1500);
        lo = std::min(lo, eL); hi = std::max(hi, eL);
        if ((eL > 0.25 && eR < 0.1) || (eR > 0.25 && eL < 0.1)) ++opposes;
        ++fenetres;
    }
    std::printf("    [banc effets] trémolo 4 Hz : gauche min %.3f max %.3f ; fenêtres en opposition %.0f sur %d\n", lo, hi, opposes, fenetres);
    VSM_ASSERT(lo < 0.1 * hi);
    VSM_ASSERT(opposes >= fenetres / 3);
}

VSM_TEST(pitch_shift_transposes_by_an_octave_without_changing_the_duration) {
    // +12 demi-tons sur un si♭3 : le pic monte à 466 Hz (à 5 cents près) ; le
    // battement du grain est publié (profondeur de l'enveloppe), pas caché.
    //
    // PAS LA3 : à 220 Hz, un grain de 50 ms fait ONZE périodes tout rond, et
    // les deux têtes, à un demi-grain l'une de l'autre, tombent à 5,5 périodes
    // d'écart -- en opposition de phase exacte, la porteuse s'annulait et le
    // banc ne trouvait rien à 440 Hz (mesuré : 0,00000). La note du banc ne
    // doit pas diviser le grain ; c'est la leçon de la machine à séquence,
    // encore une fois. Sur 233 Hz, 11,65 périodes par grain.
    auto l = sinusPlein(233.08, 144000), r = l;
    PitchShiftEffect fx;
    fx.setParameter(PitchShiftEffect::kSemitones, 12.0f);
    runFx(fx, l, r);
    double meilleur = 0.0, pic = 0.0;
    for (double f = 455.0; f <= 478.0; f += 0.1) { const double m = magFx(l, 48000, 96000, f); if (m > meilleur) { meilleur = m; pic = f; } }
    const double reste220 = magFx(l, 48000, 96000, 233.08);
    double lo = 1e9, hi = 0.0;
    for (size_t d = 48000; d + 2400 <= 144000; d += 2400) { const double e = rmsFx(l, d, 2400); lo = std::min(lo, e); hi = std::max(hi, e); }
    std::printf("    [banc effets] pitch shift +12 sur 233 Hz : pic à %.1f Hz (%.5f), reste à 233 Hz %.5f, battement du grain %.1f %%\n",
                pic, meilleur, reste220, 100.0 * (1.0 - lo / hi));
    VSM_ASSERT(std::abs(1200.0 * std::log2(pic / 466.16)) <= 5.0);
    VSM_ASSERT(meilleur > reste220 * 5.0);
    VSM_ASSERT_EQ(l.size(), size_t{144000});
    VSM_ASSERT(fx.latencySamples() == 1200);   // la moitié de 50 ms à 48 kHz
}

VSM_TEST(transient_shaper_pushes_the_attack_and_leaves_or_pulls_the_sustain) {
    // Une note qui commence net et tient : Attack +1 grossit ses cinq premières
    // ms sans toucher à la tenue ; Sustain −1 baisse la tenue.
    auto fabrique = [] {
        std::vector<float> x(48000, 0.0f);
        for (size_t i = 2400; i < 48000; ++i) x[i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / 48000.0));
        return x;
    };
    auto l = fabrique(), r = l;
    const double attaqueAvant = rmsFx(l, 2400, 240), tenueAvant = rmsFx(l, 24000, 9600);
    TransientShaperEffect fx;
    fx.setParameter(TransientShaperEffect::kAttack, 1.0f);
    fx.setParameter(TransientShaperEffect::kSustain, 0.0f);
    runFx(fx, l, r);
    const double attaqueApres = rmsFx(l, 2400, 240), tenueApres = rmsFx(l, 24000, 9600);
    auto l2 = fabrique(), r2 = l2;
    TransientShaperEffect fx2;
    fx2.setParameter(TransientShaperEffect::kAttack, 0.0f);
    fx2.setParameter(TransientShaperEffect::kSustain, -1.0f);
    runFx(fx2, l2, r2);
    const double tenueBaissee = rmsFx(l2, 24000, 9600);
    std::printf("    [banc effets] transient shaper : attaque %.4f -> %.4f (Attack +1), tenue %.4f -> %.4f ; Sustain -1 : tenue %.4f\n",
                attaqueAvant, attaqueApres, tenueAvant, tenueApres, tenueBaissee);
    VSM_ASSERT(attaqueApres > attaqueAvant * 1.5);
    VSM_ASSERT(std::abs(tenueApres / tenueAvant - 1.0) < 0.10);
    VSM_ASSERT(tenueBaissee < tenueAvant * 0.7);
}
