#include "TestFramework.h"
#include "vsm/audio/dsp/LadderFilterZDF.h"
#include "vsm/audio/dsp/LadderFilterZDFx4.h"
#include "vsm/audio/dsp/SimdFloat4.h"
#include <cmath>
#include <vector>

using vsm::audio::dsp::LadderFilterZDF;
using vsm::audio::dsp::LadderFilterZDFx4;
using vsm::audio::dsp::SimdFloat4;
using vsm::audio::dsp::fastTanh;

namespace {
std::vector<float> testSignal(int n) {
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i);
        x[static_cast<size_t>(i)] = static_cast<float>(0.5 * std::sin(0.031 * t) + 0.3 * std::sin(0.21 * t));
    }
    return x;
}
} // namespace

// --- Le type vectoriel lui-même -------------------------------------------

VSM_TEST(simd_float4_basic_arithmetic_per_lane) {
    const float a[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    const float b[4] = {10.0f, 20.0f, 30.0f, 40.0f};
    const SimdFloat4 va = SimdFloat4::load(a), vb = SimdFloat4::load(b);

    float out[4];
    (va + vb).store(out);
    for (int i = 0; i < 4; ++i) VSM_ASSERT_NEAR(out[i], a[i] + b[i], 1e-6);
    (vb - va).store(out);
    for (int i = 0; i < 4; ++i) VSM_ASSERT_NEAR(out[i], b[i] - a[i], 1e-6);
    (va * vb).store(out);
    for (int i = 0; i < 4; ++i) VSM_ASSERT_NEAR(out[i], a[i] * b[i], 1e-6);
    (vb / va).store(out);
    for (int i = 0; i < 4; ++i) VSM_ASSERT_NEAR(out[i], b[i] / a[i], 1e-5);
}

VSM_TEST(simd_float4_lanes_never_mix) {
    // Le piège classique du SIMD : une opération qui contamine les lignes
    // voisines. On met des valeurs très différentes pour que toute fuite se
    // voie immédiatement.
    const float a[4] = {-1000.0f, 0.0f, 0.001f, 1000.0f};
    const SimdFloat4 v = SimdFloat4::load(a);
    for (size_t i = 0; i < 4; ++i) VSM_ASSERT_NEAR(v.lane(i), a[i], 1e-6);

    float out[4];
    v.clamped(-1.0f, 1.0f).store(out);
    VSM_ASSERT_NEAR(out[0], -1.0f, 1e-6);
    VSM_ASSERT_NEAR(out[1], 0.0f, 1e-6);
    VSM_ASSERT_NEAR(out[2], 0.001f, 1e-6);
    VSM_ASSERT_NEAR(out[3], 1.0f, 1e-6);
}

// --- L'approximation de tanh ----------------------------------------------

VSM_TEST(fast_tanh_matches_std_tanh_closely) {
    // Bornes MESURÉES, pas souhaitées -- et volontairement séparées par plage,
    // parce que l'erreur de cette approximation n'est pas uniforme et qu'il
    // serait malhonnête de n'annoncer que son meilleur chiffre :
    //   - |x| <= 2 (le régime musical courant, où la saturation façonne
    //     réellement le timbre) : ~2e-7, soit -134 dBFS ;
    //   - au-delà : jusqu'à ~1e-4 vers |x| ~ 5, mais tanh y vaut déjà 0,9999 --
    //     le signal est en pleine saturation, et un écart de 1e-4 sur une
    //     valeur écrêtée est inaudible.
    auto worstOver = [](double limit) {
        double worst = 0.0;
        for (double x = -limit; x <= limit; x += 0.0005)
            worst = std::max(worst, std::abs(static_cast<double>(fastTanh(static_cast<float>(x))) - std::tanh(x)));
        return worst;
    };
    VSM_ASSERT(worstOver(2.0) < 5.0e-7);
    VSM_ASSERT(worstOver(20.0) < 2.0e-4);
}

VSM_TEST(fast_tanh_keeps_the_shape_of_a_saturation) {
    // Une saturation doit rester impaire, monotone et bornée : trois
    // propriétés que l'oreille entend immédiatement si elles se cassent,
    // contrairement à une erreur d'amplitude minuscule.
    VSM_ASSERT_NEAR(fastTanh(0.0f), 0.0f, 1e-9);
    VSM_ASSERT_NEAR(fastTanh(0.7f), -fastTanh(-0.7f), 1e-6);
    float previous = -2.0f;
    for (float x = -8.0f; x <= 8.0f; x += 0.01f) {
        const float y = fastTanh(x);
        VSM_ASSERT(y >= previous - 1e-6f); // monotone croissante
        VSM_ASSERT(y <= 1.0f && y >= -1.0f);
        previous = y;
    }
    VSM_ASSERT_NEAR(fastTanh(50.0f), 1.0f, 1e-6);   // bornée même très loin
    VSM_ASSERT_NEAR(fastTanh(-50.0f), -1.0f, 1e-6);
}

VSM_TEST(fast_tanh_simd_matches_its_scalar_version) {
    const float x[4] = {-3.0f, -0.25f, 0.4f, 2.5f};
    float out[4];
    fastTanh(SimdFloat4::load(x)).store(out);
    for (int i = 0; i < 4; ++i) VSM_ASSERT_NEAR(out[i], fastTanh(x[i]), 1e-6);
}

// --- Le filtre 4 lignes ----------------------------------------------------

VSM_TEST(simd_ladder_matches_the_scalar_ladder) {
    // LE test qui compte : la version vectorisée doit produire le MÊME signal
    // que la version scalaire déjà validée, sur une vraie séquence audio avec
    // résonance. Sans lui, "c'est plus rapide" ne veut rien dire.
    const auto signal = testSignal(4000);

    LadderFilterZDF scalar;
    scalar.setSampleRate(48000.0);
    scalar.setCutoffHz(900.0f);
    scalar.setResonance(2.0f);

    LadderFilterZDFx4 simd;
    simd.setSampleRate(48000.0);
    for (size_t lane = 0; lane < LadderFilterZDFx4::kLanes; ++lane) {
        simd.setCutoffHz(lane, 900.0f);
        simd.setResonance(lane, 2.0f);
    }

    double worst = 0.0;
    for (float x : signal) {
        const float expected = scalar.process(x);
        const SimdFloat4 got = simd.process(SimdFloat4(x));
        for (size_t lane = 0; lane < LadderFilterZDFx4::kLanes; ++lane)
            worst = std::max(worst, std::abs(static_cast<double>(got.lane(lane)) - static_cast<double>(expected)));
    }
    VSM_ASSERT(worst < 1.0e-4);
}

VSM_TEST(simd_ladder_lanes_are_independent) {
    // Quatre voix, quatre coupures : chaque ligne doit se comporter comme un
    // filtre séparé. On vérifie par l'énergie : plus la coupure est basse,
    // moins il reste de signal d'un contenu large bande.
    const auto signal = testSignal(6000);
    LadderFilterZDFx4 simd;
    simd.setSampleRate(48000.0);
    const float cutoffs[4] = {200.0f, 800.0f, 3000.0f, 12000.0f};
    for (size_t lane = 0; lane < 4; ++lane) {
        simd.setCutoffHz(lane, cutoffs[lane]);
        simd.setResonance(lane, 0.5f);
    }

    double energy[4] = {0, 0, 0, 0};
    for (float x : signal) {
        const SimdFloat4 y = simd.process(SimdFloat4(x));
        for (size_t lane = 0; lane < 4; ++lane) {
            const double v = static_cast<double>(y.lane(lane));
            energy[lane] += v * v;
            VSM_ASSERT(std::isfinite(v));
        }
    }
    VSM_ASSERT(energy[0] < energy[1]);
    VSM_ASSERT(energy[1] < energy[2]);
    VSM_ASSERT(energy[2] < energy[3]);
}

VSM_TEST(simd_ladder_lane_reset_leaves_the_others_alone) {
    const auto signal = testSignal(500);
    LadderFilterZDFx4 simd;
    simd.setSampleRate(48000.0);
    for (size_t lane = 0; lane < 4; ++lane) { simd.setCutoffHz(lane, 1000.0f); simd.setResonance(lane, 1.0f); }
    for (float x : signal) simd.process(SimdFloat4(x));

    simd.resetLane(1);
    const SimdFloat4 y = simd.process(SimdFloat4(0.0f));
    // La ligne remise à zéro ne sort rien ; les autres ont encore de l'état.
    VSM_ASSERT_NEAR(y.lane(1), 0.0f, 1e-6);
    VSM_ASSERT(std::abs(y.lane(0)) > 1e-6f);
}

VSM_TEST(simd_ladder_stays_bounded_at_maximum_resonance) {
    LadderFilterZDFx4 simd;
    simd.setSampleRate(48000.0);
    for (size_t lane = 0; lane < 4; ++lane) {
        simd.setCutoffHz(lane, 1200.0f);
        simd.setResonance(lane, 4.2f); // au-delà du seuil d'auto-oscillation
        simd.setDrive(lane, 1.0f);
    }
    float peak = 0.0f;
    for (int i = 0; i < 40000; ++i) {
        const float impulse = (i == 0) ? 1.0f : 0.0f;
        const SimdFloat4 y = simd.process(SimdFloat4(impulse));
        for (size_t lane = 0; lane < 4; ++lane) {
            VSM_ASSERT(std::isfinite(y.lane(lane)));
            peak = std::max(peak, std::abs(y.lane(lane)));
        }
    }
    VSM_ASSERT(peak < 8.0f); // auto-oscillation BORNÉE, comme la version scalaire
}

VSM_TEST(simd_ladder_pole_count_changes_the_slope) {
    // Même vérification que pour le filtre scalaire : 2 pôles laissent passer
    // plus d'aigus que 4.
    const auto signal = testSignal(8000);
    auto energyWithPoles = [&signal](int poles) {
        LadderFilterZDFx4 f;
        f.setSampleRate(48000.0);
        f.setPoleCount(poles);
        for (size_t lane = 0; lane < 4; ++lane) { f.setCutoffHz(lane, 600.0f); f.setResonance(lane, 0.3f); }
        double energy = 0.0;
        for (float x : signal) {
            const double v = static_cast<double>(f.process(SimdFloat4(x)).lane(0));
            energy += v * v;
        }
        return energy;
    };
    VSM_ASSERT(energyWithPoles(2) > energyWithPoles(4));
}
