#include "TestFramework.h"
#include "vsm/audio/dsp/PitchDetector.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/effect/TunerEffect.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using vsm::audio::dsp::detectPitch;
using vsm::audio::dsp::PitchReading;

// D605.1 — L'ACCORDEUR. L'attendu écrit avant le code (ROADMAP-daw, D605, corrigé à 22 h 25) : le détecteur lit 4 096
// échantillons à 48 kHz ramenés à 2 048 à 24 kHz (la moyenne de deux), comme l'effet.
namespace {
constexpr double kTaux = 48000.0;
std::vector<float> decimer(const std::vector<float>& x) {
    std::vector<float> d(x.size() / 2);
    for (size_t i = 0; i < d.size(); ++i) d[i] = 0.5f * (x[2 * i] + x[2 * i + 1]);
    return d;
}
PitchReading mesurer(const std::vector<float>& signal48k, float reference = 440.0f) {
    const auto d = decimer(signal48k);
    std::vector<float> travail;
    return detectPitch(d.data(), static_cast<int>(d.size()), kTaux / 2.0, reference, travail);
}
std::vector<float> sinus(double hz, double amplitude = 0.5, int n = 4096) {
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) x[static_cast<size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * M_PI * hz * i / kTaux));
    return x;
}
std::vector<float> dentDeScie(double hz, int n = 4096) {
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double phase = std::fmod(hz * i / kTaux, 1.0);
        x[static_cast<size_t>(i)] = static_cast<float>(0.5 * (2.0 * phase - 1.0));
    }
    return x;
}
} // namespace

VSM_TEST(the_pitch_detector_reads_the_fundamental_and_says_when_it_cannot_d605) {
    const auto la = mesurer(sinus(440.0));
    std::printf("      [D605.1] sinus 440 : %.3f Hz, note %d, %+.2f c\n", la.hz, la.note, la.cents);
    VSM_ASSERT(la.found);
    VSM_ASSERT_NEAR(la.hz, 440.0f, 0.2f);
    VSM_ASSERT_EQ(la.note, 69);
    VSM_ASSERT_NEAR(la.cents, 0.0f, 1.0f);
    const auto haut = mesurer(sinus(446.0));
    std::printf("      [D605.1] sinus 446 : %.3f Hz, %+.2f c\n", haut.hz, haut.cents);
    VSM_ASSERT_EQ(haut.note, 69);
    VSM_ASSERT_NEAR(haut.cents, 23.4f, 1.0f);
    const auto scie = mesurer(dentDeScie(110.0));
    std::printf("      [D605.1] dent de scie 110 : %.3f Hz\n", scie.hz);
    VSM_ASSERT(scie.found);
    VSM_ASSERT_NEAR(scie.hz, 110.0f, 0.2f);   // pas 220 ni 330
    const auto do6 = mesurer(sinus(1046.5));
    VSM_ASSERT_EQ(do6.note, 84);
    // L'AIGU, AUX DEUX FRÉQUENCES D'ÉCHANTILLONNAGE — ajouté après la mesure de l'application (+2,03 c au do 6 à 44,1 kHz,
    // l'interpolation faite sur la courbe normalisée) : à moins d'un cent jusqu'à 2 kHz.
    for (double taux : {44100.0, 48000.0})
        for (double hz : {1046.5, 1975.5}) {
            std::vector<float> x(4096);
            for (size_t i = 0; i < x.size(); ++i) x[i] = static_cast<float>(0.5 * std::sin(2.0 * M_PI * hz * i / taux));
            const auto d = decimer(x);
            std::vector<float> travail;
            const auto r = detectPitch(d.data(), static_cast<int>(d.size()), taux / 2.0, 440.0f, travail);
            const double cents = 1200.0 * std::log2(r.hz / hz);
            std::printf("      [D605.1] %.0f Hz, sinus %.1f : %+.2f c\n", taux, hz, cents);
            VSM_ASSERT(r.found);
            VSM_ASSERT(std::abs(cents) < 1.0);
        }
    const auto grave = mesurer(sinus(41.0));
    std::printf("      [D605.1] sinus 41 : %s, %.3f Hz\n", grave.found ? "mesuré" : "rien", grave.hz);
    VSM_ASSERT(grave.found);
    VSM_ASSERT_NEAR(grave.hz, 41.0f, 0.3f);
    VSM_ASSERT(!mesurer(std::vector<float>(4096, 0.0f)).found);   // le silence
    std::vector<float> bruit(4096);
    uint32_t graine = 12345u;
    for (auto& e : bruit) {
        graine = graine * 1664525u + 1013904223u;
        e = 0.5f * (static_cast<float>(graine >> 8) / 8388608.0f - 1.0f);
    }
    const auto b = mesurer(bruit);
    std::printf("      [D605.1] bruit blanc : %s\n", b.found ? "MESURÉ" : "rien");
    VSM_ASSERT(!b.found);
    const auto ref442 = mesurer(sinus(440.0), 442.0f);
    std::printf("      [D605.1] sinus 440 au diapason 442 : note %d, %+.2f c\n", ref442.note, ref442.cents);
    VSM_ASSERT_EQ(ref442.note, 69);
    VSM_ASSERT_NEAR(ref442.cents, -7.9f, 1.0f);
}

// LE PASSAGE AU BIT PRÈS : la sortie de l'accordeur égale son entrée sur un bloc quelconque, sans latence ; et il mesure
// ce qui le traverse (un sinus à 440 Hz, en blocs de 512, après une seconde).
VSM_TEST(the_tuner_passes_audio_bit_exactly_and_measures_it_d605) {
    // APRÈS LES DIX-HUIT D'AVANT : le menu « ajouter un effet » garde leur rang.
    const auto& liste = vsm::audio::effect::EffectFactory::available();
    VSM_ASSERT_EQ(liste.size(), static_cast<size_t>(19));
    VSM_ASSERT(liste[18].id == "tuner");
    auto fx = vsm::audio::effect::EffectFactory::create("tuner");
    VSM_ASSERT(fx != nullptr);
    fx->prepare(kTaux, 512);
    VSM_ASSERT_EQ(fx->latencySamples(), 0);
    VSM_ASSERT_NEAR(fx->getParameter(vsm::audio::effect::TunerEffect::kReference), 440.0f, 1e-6f);
    const auto entree = sinus(440.0, 0.4, 48000);
    std::vector<float> l = entree, r = entree;
    // DEUX VOIES DIFFÉRENTES, LE MÊME SON : la droite, le sinus déphasé et plus bas. CORRIGÉ APRÈS LE PREMIER ESSAI : elle
    // portait un sinus à 2 826 Hz ajouté (0,37 rad par échantillon), sans rapport harmonique avec 440 — la somme mono
    // n'était plus « un sinus à 440 Hz » (l'attendu), et l'accordeur a lu 464 Hz. C'est une LIMITE du détecteur, nommée
    // au cahier (une composante inharmonique forte creuse YIN avant la période), pas ce que ce test mesure.
    for (size_t i = 0; i < r.size(); ++i)
        r[i] = static_cast<float>(0.3 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / kTaux + 1.0));
    const std::vector<float> l0 = l, r0 = r;
    for (size_t i = 0; i + 512 <= l.size(); i += 512) fx->process(l.data() + i, r.data() + i, 512);
    VSM_ASSERT(l == l0);
    VSM_ASSERT(r == r0);
    auto* tuner = dynamic_cast<vsm::audio::effect::TunerEffect*>(fx.get());
    VSM_ASSERT(tuner != nullptr);
    uint32_t generation = 0;
    // la somme mono de deux voies où le sinus domine : mesuré, à la note 69
    const auto lu = tuner->lecture(generation);
    std::printf("      [D605.1] l'effet : génération %u, %s, %.2f Hz, note %d\n", generation, lu.found ? "mesuré" : "rien",
                lu.hz, lu.note);
    VSM_ASSERT(generation > 0);
    VSM_ASSERT(lu.found);
    VSM_ASSERT_EQ(lu.note, 69);
}
