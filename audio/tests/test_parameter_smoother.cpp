#include "TestFramework.h"
#include "vsm/audio/dsp/ParameterSmoother.h"
#include <cmath>

using namespace vsm::audio::dsp;

VSM_TEST(smoother_converges_to_target) {
    ParameterSmoother smoother;
    smoother.setSampleRate(1000.0);
    smoother.setSmoothingTimeMs(20.0f);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);

    float last = 0.0f;
    for (int i = 0; i < 500; ++i) last = smoother.nextValue();

    VSM_ASSERT_NEAR(last, 1.0, 0.01);
    VSM_ASSERT(!smoother.isSmoothing());
}

VSM_TEST(smoother_never_overshoots_for_step_target) {
    ParameterSmoother smoother;
    smoother.setSampleRate(1000.0);
    smoother.setSmoothingTimeMs(10.0f);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);

    for (int i = 0; i < 200; ++i) {
        float v = smoother.nextValue();
        VSM_ASSERT(v <= 1.0001f); // jamais au-delà de la cible pour un échelon croissant
    }
}

VSM_TEST(smoother_reset_snaps_immediately_no_zipper) {
    ParameterSmoother smoother;
    smoother.setSampleRate(1000.0);
    smoother.setSmoothingTimeMs(50.0f);
    smoother.reset(5.0f);
    VSM_ASSERT_NEAR(smoother.currentValue(), 5.0, 1e-6);
    VSM_ASSERT(!smoother.isSmoothing());
}

VSM_TEST(smoother_zero_smoothing_time_jumps_instantly) {
    ParameterSmoother smoother;
    smoother.setSampleRate(1000.0);
    smoother.setSmoothingTimeMs(0.0f);
    smoother.reset(0.0f);
    smoother.setTarget(1.0f);

    float v = smoother.nextValue();
    VSM_ASSERT_NEAR(v, 1.0, 1e-6);
}
