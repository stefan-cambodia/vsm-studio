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
