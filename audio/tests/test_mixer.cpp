#include "TestFramework.h"
#include "vsm/audio/engine/Mixer.h"
#include <cmath>
#include <vector>

using namespace vsm::audio::engine;

VSM_TEST(equal_power_pan_center_is_balanced_and_above_half_power) {
    float gainL, gainR;
    equalPowerPan(0.0f, gainL, gainR);
    VSM_ASSERT_NEAR(gainL, gainR, 1e-5);
    // Loi equal-power : au centre, gainL = gainR = cos(pi/4) = sin(pi/4) ~ 0.707,
    // PAS 0.5 (c'est précisément ce qui évite le creux perçu au centre).
    VSM_ASSERT_NEAR(gainL, 0.70710678, 1e-4);
}

VSM_TEST(equal_power_pan_hard_left_and_right) {
    float gainL, gainR;
    equalPowerPan(-1.0f, gainL, gainR);
    VSM_ASSERT_NEAR(gainL, 1.0, 1e-4);
    VSM_ASSERT_NEAR(gainR, 0.0, 1e-4);

    equalPowerPan(1.0f, gainL, gainR);
    VSM_ASSERT_NEAR(gainL, 0.0, 1e-4);
    VSM_ASSERT_NEAR(gainR, 1.0, 1e-4);
}

VSM_TEST(equal_power_pan_constant_power_at_all_positions) {
    for (float pan = -1.0f; pan <= 1.0f; pan += 0.1f) {
        float gainL, gainR;
        equalPowerPan(pan, gainL, gainR);
        float power = gainL * gainL + gainR * gainR;
        VSM_ASSERT_NEAR(power, 1.0, 1e-4); // puissance constante quelle que soit la position
    }
}

VSM_TEST(mix_channel_into_accumulates_without_overwriting) {
    std::vector<float> input(4, 1.0f);
    std::vector<float> busL(4, 0.5f); // déjà occupé par une autre piste
    std::vector<float> busR(4, 0.5f);

    mixChannelInto(input.data(), 4, 1.0f, 0.0f, true, busL.data(), busR.data());

    for (int i = 0; i < 4; ++i) {
        VSM_ASSERT(busL[i] > 0.5f); // s'est bien ADDITIONNÉ, pas écrasé
        VSM_ASSERT(busR[i] > 0.5f);
    }
}

VSM_TEST(mix_channel_into_muted_track_produces_no_change) {
    std::vector<float> input(4, 1.0f);
    std::vector<float> busL(4, 0.25f);
    std::vector<float> busR(4, 0.25f);

    float peak = mixChannelInto(input.data(), 4, 1.0f, 0.0f, /*audible=*/false, busL.data(), busR.data());

    VSM_ASSERT_NEAR(peak, 0.0, 1e-6);
    for (int i = 0; i < 4; ++i) {
        VSM_ASSERT_NEAR(busL[i], 0.25, 1e-6);
        VSM_ASSERT_NEAR(busR[i], 0.25, 1e-6);
    }
}

VSM_TEST(mix_channel_into_respects_volume) {
    std::vector<float> input(4, 1.0f);
    std::vector<float> busL(4, 0.0f);
    std::vector<float> busR(4, 0.0f);

    float peak = mixChannelInto(input.data(), 4, 0.5f, 0.0f, true, busL.data(), busR.data());
    VSM_ASSERT_NEAR(peak, 0.5, 1e-4);
}

VSM_TEST(meter_bank_reports_and_resets) {
    MeterBank bank;
    bank.reportPeak(3, 0.8f);
    VSM_ASSERT_NEAR(bank.readPeak(3), 0.8, 1e-6);
    VSM_ASSERT_NEAR(bank.readPeak(4), 0.0, 1e-6); // canal non touché

    bank.resetAll();
    VSM_ASSERT_NEAR(bank.readPeak(3), 0.0, 1e-6);
}

VSM_TEST(meter_bank_ignores_out_of_range_channel_safely) {
    MeterBank bank;
    bank.reportPeak(MeterBank::kMaxChannels + 10, 1.0f); // ne doit pas planter
    VSM_ASSERT_NEAR(bank.readPeak(MeterBank::kMaxChannels + 10), 0.0, 1e-6);
}

// --- D4.7 : ce qu'on mesure d'une piste ------------------------------------
//
// Un mètre faux ne se voit pas : il affiche un chiffre plausible. C'est
// pourquoi la partie calculatoire est une fonction PURE, éprouvée ici sur des
// signaux dont on connaît la réponse à l'avance.

VSM_TEST(phase_correlation_is_one_for_a_mono_signal) {
    // Un signal identique des deux côtés EST parfaitement corrélé.
    std::vector<float> canal(512);
    for (size_t i = 0; i < canal.size(); ++i)
        canal[i] = static_cast<float>(std::sin(i * 0.05));
    VSM_ASSERT_NEAR(phaseCorrelation(canal.data(), canal.data(), 512), 1.0f, 1e-5f);
}

VSM_TEST(phase_correlation_is_minus_one_when_the_channels_oppose) {
    // LE CAS QUI COMPTE : la piste DISPARAÎT dès qu'on somme en mono, ce qui
    // arrive à qui écoute sur un téléphone. Rien ne le signalait.
    std::vector<float> gauche(512), droite(512);
    for (size_t i = 0; i < gauche.size(); ++i) {
        gauche[i] = static_cast<float>(std::sin(i * 0.05));
        droite[i] = -gauche[i];
    }
    VSM_ASSERT_NEAR(phaseCorrelation(gauche.data(), droite.data(), 512), -1.0f, 1e-5f);
}

VSM_TEST(phase_correlation_is_near_zero_for_unrelated_channels) {
    // Deux sinusoïdes en quadrature : autant d'accord que de désaccord.
    std::vector<float> gauche(4800), droite(4800);
    for (size_t i = 0; i < gauche.size(); ++i) {
        gauche[i] = static_cast<float>(std::sin(i * 0.05));
        droite[i] = static_cast<float>(std::cos(i * 0.05));
    }
    VSM_ASSERT_NEAR(phaseCorrelation(gauche.data(), droite.data(), 4800), 0.0f, 0.02f);
}

VSM_TEST(silence_reads_as_perfectly_correlated_not_as_unrelated) {
    // Afficher zéro sur une piste qui se tait ferait clignoter l'aiguille au
    // milieu à chaque blanc.
    std::vector<float> vide(256, 0.0f);
    VSM_ASSERT_NEAR(phaseCorrelation(vide.data(), vide.data(), 256), 1.0f, 1e-6f);
}

VSM_TEST(the_meter_bank_carries_the_three_measurements) {
    MeterBank banque;
    TrackMeasurement mesure;
    mesure.peak = 0.8f;
    mesure.rms = 0.4f;
    mesure.correlation = -0.3f;
    banque.reportMeasurement(3, mesure);
    VSM_ASSERT_NEAR(banque.readPeak(3), 0.8f, 1e-6f);
    VSM_ASSERT_NEAR(banque.readRms(3), 0.4f, 1e-6f);
    VSM_ASSERT_NEAR(banque.readCorrelation(3), -0.3f, 1e-6f);
    // Hors bornes : des valeurs neutres, jamais un accès douteux.
    VSM_ASSERT_NEAR(banque.readRms(9999), 0.0f, 1e-6f);
    VSM_ASSERT_NEAR(banque.readCorrelation(9999), 1.0f, 1e-6f);
}

VSM_TEST(rms_and_peak_say_different_things_about_the_same_track) {
    // C'EST TOUTE LA RAISON D'AJOUTER LE RMS : deux signaux de MÊME crête
    // peuvent être très éloignés en niveau perçu. Une sinusoïde pleine et une
    // impulsion isolée culminent toutes deux à 1, et l'une s'entend mille fois
    // plus que l'autre.
    std::vector<float> pleine(4800), creuse(4800, 0.0f);
    for (size_t i = 0; i < pleine.size(); ++i)
        pleine[i] = static_cast<float>(std::sin(i * 0.05));
    creuse[0] = 1.0f;

    auto rms = [](const std::vector<float>& v) {
        double somme = 0.0;
        for (float e : v) somme += static_cast<double>(e) * e;
        return std::sqrt(somme / static_cast<double>(v.size()));
    };
    VSM_ASSERT_NEAR(static_cast<float>(rms(pleine)), 0.707f, 0.01f);
    VSM_ASSERT(rms(creuse) < 0.02);
    // Même crête, cent fois moins de niveau efficace.
    VSM_ASSERT(rms(pleine) > rms(creuse) * 40.0);
}
