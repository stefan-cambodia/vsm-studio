#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

SynthPluginPtr makeTestTone(double sampleRate = 48000.0) {
    auto plugin = PluginRegistry::instance().create("vsm.testtone");
    plugin->initialize(sampleRate, 512);
    return plugin;
}

float peakAbs(const std::vector<float>& buf) {
    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    return peak;
}

/// Recherche un paramètre par son NOM plutôt qu'un id interne codé en dur :
/// c'est ce qu'un vrai hôte ferait via parameterList(), et ça évite au test
/// de dépendre de détails d'implémentation privés du plugin (ParamIds vit
/// dans un header privé de vsm_audio, non exposé aux tests -- à dessein).
ParamId findParamIdByName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList())
        if (info.name == name) return info.id;
    throw std::runtime_error("Paramètre introuvable: " + name);
}

} // namespace

VSM_TEST(testtone_silent_with_no_events) {
    auto synth = makeTestTone();
    std::vector<float> outL(512, 0.0f), outR(512, 0.0f);
    synth->process(nullptr, 0, outL.data(), outR.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(outL), 0.0, 1e-6);
}

VSM_TEST(testtone_note_on_produces_sound) {
    auto synth = makeTestTone();
    MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 69, 127}; // A4, vélocité max
    std::vector<float> outL(2048, 0.0f), outR(2048, 0.0f);
    synth->process(&noteOn, 1, outL.data(), outR.data(), 2048);

    VSM_ASSERT(peakAbs(outL) > 0.01f);
    for (float s : outL) VSM_ASSERT(std::isfinite(s));
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1);
}

VSM_TEST(testtone_note_off_eventually_silences) {
    auto synth = makeTestTone(1000.0); // sample rate basse -> test court, release en échantillons ronds
    synth->setParameter(findParamIdByName(*synth, "Release"), 0.01f); // 10 échantillons à 1 kHz
    synth->setParameter(findParamIdByName(*synth, "Sustain"), 0.8f);

    MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 69, 127};
    std::vector<float> outL(500, 0.0f), outR(500, 0.0f);
    synth->process(&noteOn, 1, outL.data(), outR.data(), 500); // laisse la note s'établir (attack+decay+sustain)
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1);

    MidiNoteEvent noteOff{MidiNoteEvent::Kind::NoteOff, 0, 0, 69, 64};
    synth->process(&noteOff, 1, outL.data(), outR.data(), 500); // largement au-delà des 10 échantillons de release

    std::vector<float> silenceL(1000, 0.0f), silenceR(1000, 0.0f);
    synth->process(nullptr, 0, silenceL.data(), silenceR.data(), 1000);

    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
    VSM_ASSERT_NEAR(peakAbs(silenceL), 0.0, 1e-5);
}

VSM_TEST(testtone_polyphony_multiple_simultaneous_notes) {
    auto synth = makeTestTone();
    MidiNoteEvent notes[3] = {
        {MidiNoteEvent::Kind::NoteOn, 0, 0, 60, 100},
        {MidiNoteEvent::Kind::NoteOn, 0, 0, 64, 100},
        {MidiNoteEvent::Kind::NoteOn, 0, 0, 67, 100},
    };
    std::vector<float> outL(1024, 0.0f), outR(1024, 0.0f);
    synth->process(notes, 3, outL.data(), outR.data(), 1024);

    VSM_ASSERT_EQ(synth->activeVoiceCount(), 3);
    VSM_ASSERT(peakAbs(outL) > 0.01f);
}

VSM_TEST(testtone_set_get_parameter_roundtrip) {
    auto synth = makeTestTone();
    ParamId cutoffId = findParamIdByName(*synth, "Filter Cutoff");
    synth->setParameter(cutoffId, 3000.0f);
    VSM_ASSERT_NEAR(synth->getParameter(cutoffId), 3000.0, 1e-3);
}

VSM_TEST(testtone_save_load_state_roundtrip) {
    auto synthA = makeTestTone();
    ParamId cutoffId = findParamIdByName(*synthA, "Filter Cutoff");
    ParamId resonanceId = findParamIdByName(*synthA, "Resonance");
    synthA->setParameter(cutoffId, 4321.0f);
    synthA->setParameter(resonanceId, 3.5f);

    PresetState state = synthA->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.testtone"));

    auto synthB = makeTestTone();
    synthB->loadState(state);

    VSM_ASSERT_NEAR(synthB->getParameter(cutoffId), 4321.0, 1e-2);
    VSM_ASSERT_NEAR(synthB->getParameter(resonanceId), 3.5, 1e-3);
}

VSM_TEST(testtone_processing_is_deterministic) {
    MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 69, 100};

    auto synthA = makeTestTone();
    std::vector<float> outAL(2000, 0.0f), outAR(2000, 0.0f);
    synthA->process(&noteOn, 1, outAL.data(), outAR.data(), 2000);

    auto synthB = makeTestTone();
    std::vector<float> outBL(2000, 0.0f), outBR(2000, 0.0f);
    synthB->process(&noteOn, 1, outBL.data(), outBR.data(), 2000);

    for (size_t i = 0; i < outAL.size(); ++i)
        VSM_ASSERT_NEAR(outAL[i], outBL[i], 1e-9);
}
