#include "TestFramework.h"
#include "vsm/audio/dsp/Envelope.h"
#include <cmath>

using namespace vsm::audio::dsp;

VSM_TEST(envelope_idle_by_default) {
    AdsrEnvelope env;
    VSM_ASSERT(!env.isActive());
    VSM_ASSERT_NEAR(env.currentLevel(), 0.0, 1e-6);
}

VSM_TEST(envelope_attack_reaches_full_level) {
    AdsrEnvelope env;
    env.setSampleRate(1000.0); // 1000 Hz pour des durées d'échantillons rondes
    env.setSettings({0.01f, 0.05f, 0.5f, 0.1f}); // attack = 10 échantillons
    env.noteOn();

    float last = 0.0f;
    for (int i = 0; i < 15; ++i) last = env.nextSample();

    VSM_ASSERT_NEAR(last, 1.0f, 0.15); // devrait avoir dépassé l'attaque et être en decay, proche de 1
    VSM_ASSERT(env.stage() == EnvelopeStage::Decay || env.stage() == EnvelopeStage::Attack);
}

VSM_TEST(envelope_decay_settles_at_sustain_level) {
    AdsrEnvelope env;
    env.setSampleRate(1000.0);
    env.setSettings({0.001f, 0.02f, 0.4f, 0.1f}); // attack ~1 échantillon, decay = 20 échantillons
    env.noteOn();

    for (int i = 0; i < 50; ++i) env.nextSample();

    VSM_ASSERT(env.stage() == EnvelopeStage::Sustain);
    VSM_ASSERT_NEAR(env.currentLevel(), 0.4, 0.02);
}

VSM_TEST(envelope_sustain_holds_constant) {
    AdsrEnvelope env;
    env.setSampleRate(1000.0);
    env.setSettings({0.001f, 0.01f, 0.6f, 0.1f});
    env.noteOn();
    for (int i = 0; i < 30; ++i) env.nextSample(); // dépasse largement attack+decay

    float a = env.nextSample();
    float b = env.nextSample();
    float c = env.nextSample();
    VSM_ASSERT_NEAR(a, b, 1e-6);
    VSM_ASSERT_NEAR(b, c, 1e-6);
    VSM_ASSERT_NEAR(a, 0.6, 0.02);
}

VSM_TEST(envelope_release_reaches_zero_and_goes_idle) {
    AdsrEnvelope env;
    env.setSampleRate(1000.0);
    env.setSettings({0.001f, 0.01f, 0.5f, 0.02f}); // release = 20 échantillons
    env.noteOn();
    for (int i = 0; i < 30; ++i) env.nextSample(); // atteint le sustain
    env.noteOff();
    for (int i = 0; i < 30; ++i) env.nextSample(); // dépasse largement le release

    VSM_ASSERT(env.stage() == EnvelopeStage::Idle);
    VSM_ASSERT_NEAR(env.currentLevel(), 0.0, 1e-4);
}

VSM_TEST(envelope_retrigger_during_release_has_no_click) {
    AdsrEnvelope env;
    env.setSampleRate(1000.0);
    env.setSettings({0.05f, 0.01f, 0.8f, 0.05f}); // attack=50 échantillons, decay=10, release=50
    env.noteOn();
    for (int i = 0; i < 70; ++i) env.nextSample(); // dépasse attack(50)+decay(10) : atteint le sustain
    VSM_ASSERT(env.stage() == EnvelopeStage::Sustain);

    env.noteOff();

    float beforeRetrigger = 0.0f;
    for (int i = 0; i < 10; ++i) beforeRetrigger = env.nextSample(); // en cours de release (10/50 échantillons)

    env.noteOn(); // retrigger rapide : ne doit PAS sauter à 0 puis remonter
    float afterRetrigger = env.nextSample();

    VSM_ASSERT(env.stage() == EnvelopeStage::Attack);
    // Le niveau ne doit pas avoir chuté brutalement : la discontinuité
    // sample-à-sample doit rester faible (pas de "click").
    VSM_ASSERT(std::abs(afterRetrigger - beforeRetrigger) < 0.05f);
}
