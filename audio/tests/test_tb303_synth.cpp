#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

SynthPluginPtr makeTb303(double sampleRate = 48000.0) {
    auto plugin = PluginRegistry::instance().create("vsm.tb303");
    plugin->initialize(sampleRate, 512);
    return plugin;
}

float peakAbs(const std::vector<float>& buf) {
    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    return peak;
}

ParamId findParamIdByName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList())
        if (info.name == name) return info.id;
    throw std::runtime_error("Paramètre introuvable: " + name);
}

} // namespace

VSM_TEST(tb303_registered_in_plugin_registry) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.tb303"));
}

VSM_TEST(tb303_silent_with_no_events) {
    auto synth = makeTb303();
    std::vector<float> outL(512, 0.0f), outR(512, 0.0f);
    synth->process(nullptr, 0, outL.data(), outR.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(outL), 0.0, 1e-6);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(tb303_note_on_produces_sound) {
    auto synth = makeTb303();
    MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 45, 110};
    std::vector<float> outL(4000, 0.0f), outR(4000, 0.0f);
    synth->process(&noteOn, 1, outL.data(), outR.data(), 4000);

    VSM_ASSERT(peakAbs(outL) > 0.01f);
    for (float s : outL) VSM_ASSERT(std::isfinite(s));
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1);
}

VSM_TEST(tb303_note_off_eventually_silences) {
    auto synth = makeTb303(1000.0); // sample rate basse -> test court, temps ronds
    synth->setParameter(findParamIdByName(*synth, "Analog Character"), 0.0f);

    MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 50, 100};
    std::vector<float> outL(500, 0.0f), outR(500, 0.0f);
    synth->process(&noteOn, 1, outL.data(), outR.data(), 500);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1);

    MidiNoteEvent noteOff{MidiNoteEvent::Kind::NoteOff, 0, 0, 50, 64};
    synth->process(&noteOff, 1, outL.data(), outR.data(), 500); // release ~40ms, largement dépassé à 1kHz sur 500 échantillons

    std::vector<float> silenceL(1000, 0.0f), silenceR(1000, 0.0f);
    synth->process(nullptr, 0, silenceL.data(), silenceR.data(), 1000);

    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
    VSM_ASSERT_NEAR(peakAbs(silenceL), 0.0, 1e-4);
}

VSM_TEST(tb303_overlapping_notes_slide_differently_than_separate_notes) {
    // Deux notes MIDI qui SE CHEVAUCHENT doivent produire un glissando
    // (slide, sans retrigger) ; les mêmes notes séparées par un note-off
    // doivent retrigger normalement -- le transitoire juste après le
    // changement de note doit donc mesurablement différer entre les deux cas.
    auto renderTransition = [](bool overlapping) {
        auto synth = makeTb303(8000.0);
        synth->setParameter(findParamIdByName(*synth, "Analog Character"), 0.0f);
        synth->setParameter(findParamIdByName(*synth, "Glide Time"), 0.15f);

        MidiNoteEvent noteA{MidiNoteEvent::Kind::NoteOn, 0, 0, 48, 100};
        std::vector<float> settleL(3000, 0.0f), settleR(3000, 0.0f);
        synth->process(&noteA, 1, settleL.data(), settleR.data(), 3000);

        std::vector<MidiNoteEvent> events;
        if (!overlapping)
            events.push_back(MidiNoteEvent{MidiNoteEvent::Kind::NoteOff, 0, 0, 48, 64});
        events.push_back(MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, 60, 100}); // une octave plus haut

        std::vector<float> afterL(300, 0.0f), afterR(300, 0.0f);
        synth->process(events.data(), static_cast<int>(events.size()), afterL.data(), afterR.data(), 300);
        return afterL;
    };

    auto slideOutput = renderTransition(true);
    auto separateOutput = renderTransition(false);

    bool anyDifference = false;
    for (size_t i = 0; i < slideOutput.size(); ++i)
        if (std::abs(slideOutput[i] - separateOutput[i]) > 0.01f) anyDifference = true;
    VSM_ASSERT(anyDifference);
}

VSM_TEST(tb303_accent_knob_increases_output_level_at_fixed_velocity) {
    // Vélocité FIXE (127, au-dessus du seuil) : seul le knob "Accent" varie,
    // isolant son effet de celui, non intéressant ici, de la vélocité brute.
    auto renderWithAccentKnob = [](float accentKnobValue) {
        auto synth = makeTb303();
        synth->setParameter(findParamIdByName(*synth, "Analog Character"), 0.0f);
        synth->setParameter(findParamIdByName(*synth, "Accent Threshold"), 90.0f);
        synth->setParameter(findParamIdByName(*synth, "Accent"), accentKnobValue);

        MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 50, 127};
        std::vector<float> outL(2000, 0.0f), outR(2000, 0.0f);
        synth->process(&noteOn, 1, outL.data(), outR.data(), 2000);
        return peakAbs(outL);
    };

    float peakNoAccent = renderWithAccentKnob(0.0f);
    float peakFullAccent = renderWithAccentKnob(1.0f);
    VSM_ASSERT(peakFullAccent > peakNoAccent * 1.1f);
}

VSM_TEST(tb303_velocity_below_threshold_produces_no_accent_bonus) {
    auto renderWithVelocity = [](uint8_t velocity) {
        auto synth = makeTb303();
        synth->setParameter(findParamIdByName(*synth, "Analog Character"), 0.0f);
        synth->setParameter(findParamIdByName(*synth, "Accent Threshold"), 100.0f);
        synth->setParameter(findParamIdByName(*synth, "Accent"), 1.0f);

        MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 50, velocity};
        std::vector<float> outL(2000, 0.0f), outR(2000, 0.0f);
        synth->process(&noteOn, 1, outL.data(), outR.data(), 2000);
        return peakAbs(outL);
    };

    float peakJustBelow = renderWithVelocity(99);  // sous le seuil -> pas d'accent
    float peakWayAbove = renderWithVelocity(127);   // largement au-dessus -> accent quasi maximal

    // Ratio "vélocité pure" attendu (sans aucune contribution d'accent) :
    // 127/99 ~= 1.28. Le ratio réel doit être clairement supérieur --
    // preuve que l'accent ajoute bien quelque chose au-delà de la simple
    // proportionnalité à la vélocité.
    VSM_ASSERT(peakWayAbove > peakJustBelow * 1.4f);
}

VSM_TEST(tb303_processing_is_deterministic) {
    auto synthA = makeTb303();
    synthA->setParameter(findParamIdByName(*synthA, "Analog Character"), 0.5f);
    auto synthB = makeTb303();
    synthB->setParameter(findParamIdByName(*synthB, "Analog Character"), 0.5f);

    MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 48, 120};
    std::vector<float> outAL(3000, 0.0f), outAR(3000, 0.0f);
    std::vector<float> outBL(3000, 0.0f), outBR(3000, 0.0f);
    synthA->process(&noteOn, 1, outAL.data(), outAR.data(), 3000);
    synthB->process(&noteOn, 1, outBL.data(), outBR.data(), 3000);

    for (size_t i = 0; i < outAL.size(); ++i)
        VSM_ASSERT_NEAR(outAL[i], outBL[i], 1e-9);
}

VSM_TEST(tb303_save_load_state_roundtrip) {
    auto synthA = makeTb303();
    ParamId cutoffId = findParamIdByName(*synthA, "Cutoff");
    ParamId resonanceId = findParamIdByName(*synthA, "Resonance");
    synthA->setParameter(cutoffId, 1234.0f);
    synthA->setParameter(resonanceId, 0.85f);

    PresetState state = synthA->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.tb303"));

    auto synthB = makeTb303();
    synthB->loadState(state);

    VSM_ASSERT_NEAR(synthB->getParameter(cutoffId), 1234.0, 1e-2);
    VSM_ASSERT_NEAR(synthB->getParameter(resonanceId), 0.85, 1e-3);
}

VSM_TEST(tb303_parameter_list_has_expected_entries) {
    auto synth = makeTb303();
    const auto& params = synth->parameterList();
    VSM_ASSERT_EQ(params.size(), static_cast<size_t>(9));

    bool hasAccent = false, hasAccentThreshold = false, hasGlide = false;
    for (const auto& p : params) {
        if (p.name == "Accent") hasAccent = true;
        if (p.name == "Accent Threshold") hasAccentThreshold = true;
        if (p.name == "Glide Time") hasGlide = true;
    }
    VSM_ASSERT(hasAccent);
    VSM_ASSERT(hasAccentThreshold);
    VSM_ASSERT(hasGlide);
}

VSM_TEST(tb303_waveform_restricted_to_saw_or_square) {
    auto synth = makeTb303();
    ParamId waveformId = findParamIdByName(*synth, "Waveform");
    for (const auto& info : synth->parameterList()) {
        if (info.id == waveformId) {
            VSM_ASSERT_NEAR(info.minValue, 0.0, 1e-6);
            VSM_ASSERT_NEAR(info.maxValue, 1.0, 1e-6); // uniquement Saw(0)/Square(1), pas Sine/Triangle
        }
    }
}
