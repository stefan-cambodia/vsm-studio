#include "TestFramework.h"
#include "vsm/audio/dsp/Chorus.h"
#include "vsm/audio/effect/Flanger.h"
#include <algorithm>
#include <cmath>
#include <vector>

using vsm::audio::dsp::Chorus;

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
} // namespace

VSM_TEST(chorus_dry_when_mix_zero) {
    Chorus chorus;
    chorus.setSampleRate(48000.0);
    chorus.setMix(0.0f);

    for (int i = 0; i < 2000; ++i) {
        const float in = std::sin(static_cast<float>(kTwoPi * 220.0 * i / 48000.0));
        float l = 0.0f, r = 0.0f;
        chorus.process(in, l, r);
        // mix=0 -> les deux canaux reproduisent exactement l'entrée sèche.
        VSM_ASSERT_NEAR(l, in, 1e-6);
        VSM_ASSERT_NEAR(r, in, 1e-6);
    }
}

VSM_TEST(chorus_produces_stereo_width) {
    Chorus chorus;
    chorus.setSampleRate(48000.0);
    chorus.setRateHz(0.7f);
    chorus.setDepthMs(3.0f);
    chorus.setBaseDelayMs(8.0f);
    chorus.setMix(0.6f);

    bool anyStereoDifference = false;
    for (int i = 0; i < 8000; ++i) {
        const float in = std::sin(static_cast<float>(kTwoPi * 330.0 * i / 48000.0));
        float l = 0.0f, r = 0.0f;
        chorus.process(in, l, r);
        VSM_ASSERT(std::isfinite(l) && std::isfinite(r));
        if (std::abs(l - r) > 0.01f) anyStereoDifference = true;
    }
    // Les deux LFO en quadrature doivent créer une différence L/R mesurable.
    VSM_ASSERT(anyStereoDifference);
}

VSM_TEST(chorus_output_stays_bounded) {
    Chorus chorus;
    chorus.setSampleRate(44100.0);
    chorus.setMix(1.0f);
    chorus.setDepthMs(5.0f);

    float peak = 0.0f;
    for (int i = 0; i < 20000; ++i) {
        float l = 0.0f, r = 0.0f;
        chorus.process(1.0f, l, r); // entrée DC pleine échelle, cas défavorable
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }
    // Pas de feedback dans un chorus : la sortie ne peut pas diverger.
    VSM_ASSERT(peak <= 1.5f);
}

VSM_TEST(chorus_is_deterministic) {
    auto run = [] {
        Chorus chorus;
        chorus.setSampleRate(48000.0);
        chorus.setRateHz(0.6f);
        chorus.setDepthMs(3.0f);
        chorus.setMix(0.5f);
        std::vector<float> out;
        out.reserve(4000);
        for (int i = 0; i < 4000; ++i) {
            const float in = std::sin(static_cast<float>(kTwoPi * 200.0 * i / 48000.0));
            float l = 0.0f, r = 0.0f;
            chorus.process(in, l, r);
            out.push_back(l);
        }
        return out;
    };

    auto a = run();
    auto b = run();
    for (size_t i = 0; i < a.size(); ++i)
        VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

// D526 — LA LECTURE NE SORT JAMAIS DU TAMPON.
//
// Trouvé le 30/09/2026 en rendant trois notes tenues à travers l'insert chorus
// (cadence 1,751 Hz, profondeur 2,4 ms) : à 16,69 s, UN échantillon du canal
// gauche valait 3,9e28, puis décroissait du coefficient du passe-bas. La position
// de lecture est `writeIndex - retard`, ramenée dans le tampon par `+= size` tant
// qu'elle est négative ; quand elle est négative d'une fraction infime (le retard
// croise un entier au moment où l'écriture y passe), `position + size` s'ARRONDIT
// à `size` exactement en simple précision, et la lecture se fait une case après la
// fin du tampon. Ce que contient cette case dépend du tas : le plus souvent rien
// d'audible, ce jour-là un nombre à vingt-huit chiffres.
VSM_TEST(chorus_read_position_wraps_inside_the_buffer) {
    const float size = 2209.0f;   // 50 ms à 44,1 kHz, plus la marge
    // Les cas qui arrondissaient à `size` : une position négative plus petite que
    // la demi-résolution d'un flottant voisin de 2209.
    for (float tiny : {-1.0e-6f, -1.5e-5f, -6.0e-5f, -1.2e-4f}) {
        const float pos = Chorus::wrapReadPosition(tiny, size);
        VSM_ASSERT(pos >= 0.0f);
        VSM_ASSERT(pos < size);
    }
    // Et ce qui était juste le reste, au bit près.
    VSM_ASSERT_NEAR(Chorus::wrapReadPosition(-1.0f, size), 2208.0f, 0.0);
    VSM_ASSERT_NEAR(Chorus::wrapReadPosition(-457.25f, size), 1751.75f, 0.0);
    VSM_ASSERT_NEAR(Chorus::wrapReadPosition(12.5f, size), 12.5f, 0.0);
    VSM_ASSERT_NEAR(Chorus::wrapReadPosition(0.0f, size), 0.0f, 0.0);
}

VSM_TEST(chorus_constant_input_stays_constant) {
    // Une entrée CONSTANTE lue n'importe où dans le tampon rend la même constante :
    // la moindre lecture hors du tampon se voit. Les réglages et la durée sont ceux
    // de la mesure où le défaut s'est montré (26 s, 1,751 Hz, 2,4 ms, base 8 ms).
    Chorus chorus;
    chorus.setSampleRate(44100.0);
    chorus.setRateHz(1.751f);
    chorus.setDepthMs(2.4f);
    chorus.setBaseDelayMs(8.0f);
    chorus.setMix(1.0f);

    const int total = 26 * 44100;
    float worst = 0.0f;
    for (int i = 0; i < total; ++i) {
        float l = 0.0f, r = 0.0f;
        chorus.process(0.25f, l, r);
        if (i < 4410) continue;   // le temps que la ligne à retard et le passe-bas se remplissent
        worst = std::max(worst, std::max(std::abs(l - 0.25f), std::abs(r - 0.25f)));
    }
    VSM_ASSERT(worst < 1.0e-4f);
}

VSM_TEST(flanger_read_position_wraps_inside_the_buffer) {
    // La ligne du flanger : 12 ms à 44,1 kHz, plus la marge.
    const float size = 533.0f;
    for (float tiny : {-1.0e-6f, -5.0e-6f, -1.5e-5f, -3.0e-5f}) {
        const float pos = vsm::audio::dsp::wrapReadPosition(tiny, size);
        VSM_ASSERT(pos >= 0.0f);
        VSM_ASSERT(pos < size);
    }
    VSM_ASSERT_NEAR(vsm::audio::dsp::wrapReadPosition(-44.25f, size), 488.75f, 0.0);
}

// PAS DE TEST « ENTRÉE CONSTANTE » POUR LE FLANGER, ET C'EST DIT. Écrit deux fois le
// 30/09 (réglages par défaut sur 300 s ; puis 1 Hz et 0,7 de profondeur, où la
// position s'arrondit à la taille du tampon à 8,38 s), il est resté VERT sur le
// flanger d'avant la correction : la case lue après la fin du tampon contenait ce
// jour-là la même valeur que l'entrée. Un test qui ne tombe pas sur le défaut qu'il
// vise n'est pas une garde ; celui du chorus, lui, a été vu rouge. Ce qui garde le
// flanger est le test de la position ci-dessus, et le fait qu'il lit sa ligne par
// la même fonction.
