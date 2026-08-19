#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

SynthPluginPtr makeJuno(double sampleRate = 48000.0) {
    auto plugin = PluginRegistry::instance().create("vsm.juno106");
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

MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t vel) {
    return MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, offset, 0, note, vel};
}
MidiNoteEvent noteOff(int offset, uint8_t note) {
    return MidiNoteEvent{MidiNoteEvent::Kind::NoteOff, offset, 0, note, 64};
}

} // namespace

VSM_TEST(juno106_registered_in_plugin_registry) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.juno106"));
}

VSM_TEST(juno106_silent_with_no_events) {
    auto synth = makeJuno();
    std::vector<float> outL(512, 0.0f), outR(512, 0.0f);
    synth->process(nullptr, 0, outL.data(), outR.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(outL), 0.0, 1e-6);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(juno106_note_on_produces_sound) {
    auto synth = makeJuno();
    MidiNoteEvent on = noteOn(0, 48, 100);
    std::vector<float> outL(4000, 0.0f), outR(4000, 0.0f);
    synth->process(&on, 1, outL.data(), outR.data(), 4000);

    VSM_ASSERT(peakAbs(outL) > 0.01f);
    for (float s : outL) VSM_ASSERT(std::isfinite(s));
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1);
}

VSM_TEST(juno106_is_polyphonic) {
    // Trois notes tenues simultanément -> trois voix actives (contraste
    // avec le Minimoog/TB-303 monophoniques).
    auto synth = makeJuno();
    std::vector<MidiNoteEvent> chord = {noteOn(0, 48, 100), noteOn(0, 52, 100), noteOn(0, 55, 100)};
    std::vector<float> outL(2000, 0.0f), outR(2000, 0.0f);
    synth->process(chord.data(), static_cast<int>(chord.size()), outL.data(), outR.data(), 2000);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 3);
}

VSM_TEST(juno106_steals_voices_beyond_max_polyphony) {
    // Sept notes distinctes sur un synthé 6 voix -> la plus ancienne est
    // volée, le total ne dépasse jamais 6.
    auto synth = makeJuno();
    std::vector<MidiNoteEvent> notes;
    for (int n = 0; n < 7; ++n)
        notes.push_back(noteOn(n * 10, static_cast<uint8_t>(48 + n), 100));

    std::vector<float> outL(2000, 0.0f), outR(2000, 0.0f);
    synth->process(notes.data(), static_cast<int>(notes.size()), outL.data(), outR.data(), 2000);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 6);
}

VSM_TEST(juno106_note_off_eventually_silences) {
    auto synth = makeJuno(1000.0);
    synth->setParameter(findParamIdByName(*synth, "Env Decay"), 0.01f);
    synth->setParameter(findParamIdByName(*synth, "Env Release"), 0.01f);
    synth->setParameter(findParamIdByName(*synth, "Analog Character"), 0.0f);
    synth->setParameter(findParamIdByName(*synth, "Chorus Mode"), 0.0f);

    MidiNoteEvent on = noteOn(0, 57, 100);
    std::vector<float> outL(500, 0.0f), outR(500, 0.0f);
    synth->process(&on, 1, outL.data(), outR.data(), 500);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1);

    MidiNoteEvent off = noteOff(0, 57);
    synth->process(&off, 1, outL.data(), outR.data(), 500);

    std::vector<float> silenceL(2000, 0.0f), silenceR(2000, 0.0f);
    synth->process(nullptr, 0, silenceL.data(), silenceR.data(), 2000);

    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
    VSM_ASSERT_NEAR(peakAbs(silenceL), 0.0, 1e-3);
}

VSM_TEST(juno106_is_not_velocity_sensitive) {
    // Trait authentique : le clavier du Juno-106 n'est pas vélocité-sensible.
    // Même note à vélocité 30 et 127 -> rendu strictement identique.
    auto render = [](uint8_t velocity) {
        auto synth = makeJuno();
        MidiNoteEvent on = noteOn(0, 60, velocity);
        std::vector<float> outL(3000, 0.0f), outR(3000, 0.0f);
        synth->process(&on, 1, outL.data(), outR.data(), 3000);
        return outL;
    };

    auto soft = render(30);
    auto hard = render(127);
    for (size_t i = 0; i < soft.size(); ++i)
        VSM_ASSERT_NEAR(soft[i], hard[i], 1e-9);
}

VSM_TEST(juno106_processing_is_deterministic) {
    auto render = [] {
        auto synth = makeJuno();
        synth->setParameter(findParamIdByName(*synth, "Analog Character"), 0.7f);
        std::vector<MidiNoteEvent> chord = {noteOn(0, 50, 100), noteOn(0, 57, 100)};
        std::vector<float> outL(3000, 0.0f), outR(3000, 0.0f);
        synth->process(chord.data(), static_cast<int>(chord.size()), outL.data(), outR.data(), 3000);
        return outL;
    };

    auto a = render();
    auto b = render();
    for (size_t i = 0; i < a.size(); ++i)
        VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(juno106_chorus_creates_stereo_when_on_and_mono_when_off) {
    // Chorus actif (mode I) : les deux canaux diffèrent.
    {
        auto synth = makeJuno();
        synth->setParameter(findParamIdByName(*synth, "Chorus Mode"), 1.0f);
        MidiNoteEvent on = noteOn(0, 55, 100);
        std::vector<float> outL(8000, 0.0f), outR(8000, 0.0f);
        synth->process(&on, 1, outL.data(), outR.data(), 8000);

        bool anyDifference = false;
        for (size_t i = 0; i < outL.size(); ++i)
            if (std::abs(outL[i] - outR[i]) > 0.005f) anyDifference = true;
        VSM_ASSERT(anyDifference);
    }
    // Chorus off : sortie strictement mono (L == R).
    {
        auto synth = makeJuno();
        synth->setParameter(findParamIdByName(*synth, "Chorus Mode"), 0.0f);
        MidiNoteEvent on = noteOn(0, 55, 100);
        std::vector<float> outL(4000, 0.0f), outR(4000, 0.0f);
        synth->process(&on, 1, outL.data(), outR.data(), 4000);

        for (size_t i = 0; i < outL.size(); ++i)
            VSM_ASSERT_NEAR(outL[i], outR[i], 1e-6);
    }
}

VSM_TEST(juno106_save_load_state_roundtrip) {
    auto synthA = makeJuno();
    ParamId cutoffId = findParamIdByName(*synthA, "VCF Cutoff");
    ParamId subId = findParamIdByName(*synthA, "DCO Sub Level");
    synthA->setParameter(cutoffId, 3200.0f);
    synthA->setParameter(subId, 0.85f);

    PresetState state = synthA->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.juno106"));

    auto synthB = makeJuno();
    synthB->loadState(state);
    VSM_ASSERT_NEAR(synthB->getParameter(cutoffId), 3200.0, 1e-2);
    VSM_ASSERT_NEAR(synthB->getParameter(subId), 0.85, 1e-3);
}

VSM_TEST(juno106_parameter_list_has_expected_entries) {
    auto synth = makeJuno();
    const auto& params = synth->parameterList();
    VSM_ASSERT_EQ(params.size(), static_cast<size_t>(21));

    bool hasChorus = false, hasSub = false;
    for (const auto& p : params) {
        if (p.name == "Chorus Mode") hasChorus = true;
        if (p.name == "DCO Sub Level") hasSub = true;
    }
    VSM_ASSERT(hasChorus);
    VSM_ASSERT(hasSub);
}
