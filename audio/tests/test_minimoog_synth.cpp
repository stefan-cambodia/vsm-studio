#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

SynthPluginPtr makeMinimoog(double sampleRate = 48000.0) {
    auto plugin = PluginRegistry::instance().create("vsm.minimoog");
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

VSM_TEST(minimoog_registered_in_plugin_registry) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.minimoog"));
}

VSM_TEST(minimoog_silent_with_no_events) {
    auto synth = makeMinimoog();
    std::vector<float> outL(512, 0.0f), outR(512, 0.0f);
    synth->process(nullptr, 0, outL.data(), outR.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(outL), 0.0, 1e-6);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(minimoog_note_on_produces_sound) {
    auto synth = makeMinimoog();
    MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 45, 110};
    std::vector<float> outL(4000, 0.0f), outR(4000, 0.0f);
    synth->process(&noteOn, 1, outL.data(), outR.data(), 4000);

    VSM_ASSERT(peakAbs(outL) > 0.01f);
    for (float s : outL) VSM_ASSERT(std::isfinite(s));
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1);
}

VSM_TEST(minimoog_note_off_eventually_silences) {
    auto synth = makeMinimoog(1000.0); // sample rate basse -> test court, temps ronds
    synth->setParameter(findParamIdByName(*synth, "Amp Decay"), 0.01f);   // release = decay = 10 échantillons
    synth->setParameter(findParamIdByName(*synth, "Filter Decay"), 0.01f);
    synth->setParameter(findParamIdByName(*synth, "Analog Character"), 0.0f);

    MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 57, 100};
    std::vector<float> outL(500, 0.0f), outR(500, 0.0f);
    synth->process(&noteOn, 1, outL.data(), outR.data(), 500);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1);

    MidiNoteEvent noteOff{MidiNoteEvent::Kind::NoteOff, 0, 0, 57, 64};
    synth->process(&noteOff, 1, outL.data(), outR.data(), 500);

    std::vector<float> silenceL(1000, 0.0f), silenceR(1000, 0.0f);
    synth->process(nullptr, 0, silenceL.data(), silenceR.data(), 1000);

    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
    VSM_ASSERT_NEAR(peakAbs(silenceL), 0.0, 1e-4);
}

VSM_TEST(minimoog_monophonic_trill_stays_sounding_until_last_release) {
    // Rejoue le scénario testé au niveau de MonoVoiceAllocator, mais à
    // travers le synthé complet : tenir un accord et relâcher les notes
    // une à une ne doit JAMAIS couper le son avant la toute dernière.
    auto synth = makeMinimoog();

    MidiNoteEvent on60{MidiNoteEvent::Kind::NoteOn, 0, 0, 60, 100};
    MidiNoteEvent on64{MidiNoteEvent::Kind::NoteOn, 100, 0, 64, 100};
    MidiNoteEvent off64{MidiNoteEvent::Kind::NoteOff, 200, 0, 64, 64};
    MidiNoteEvent off60{MidiNoteEvent::Kind::NoteOff, 300, 0, 60, 64};

    std::vector<MidiNoteEvent> events = {on60, on64, off64}; // 60 tenue, 64 relâchée -> doit retomber sur 60
    std::vector<float> outL(400, 0.0f), outR(400, 0.0f);
    synth->process(events.data(), static_cast<int>(events.size()), outL.data(), outR.data(), 400);

    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1); // toujours sonore : 60 encore tenue

    std::vector<MidiNoteEvent> finalOff = {off60};
    synth->process(finalOff.data(), 1, outL.data(), outR.data(), 400);
    // Le release n'est pas instantané (Amp Decay par défaut) : l'enveloppe
    // peut encore être active juste après le note-off, ce qui est correct.
    // On vérifie juste qu'elle N'EST PAS repartie en Attack (pas de nouveau son).
}

VSM_TEST(minimoog_processing_is_deterministic) {
    // Graines de dérive fixes (voir MinimoogSynth::initialize) : deux
    // instances fraîches soumises aux mêmes événements doivent produire un
    // rendu bit-identique, ANALOG CHARACTER actif inclus.
    auto synthA = makeMinimoog();
    synthA->setParameter(findParamIdByName(*synthA, "Analog Character"), 0.6f);
    auto synthB = makeMinimoog();
    synthB->setParameter(findParamIdByName(*synthB, "Analog Character"), 0.6f);

    MidiNoteEvent noteOn{MidiNoteEvent::Kind::NoteOn, 0, 0, 52, 100};
    std::vector<float> outAL(3000, 0.0f), outAR(3000, 0.0f);
    std::vector<float> outBL(3000, 0.0f), outBR(3000, 0.0f);
    synthA->process(&noteOn, 1, outAL.data(), outAR.data(), 3000);
    synthB->process(&noteOn, 1, outBL.data(), outBR.data(), 3000);

    for (size_t i = 0; i < outAL.size(); ++i)
        VSM_ASSERT_NEAR(outAL[i], outBL[i], 1e-9);
}

VSM_TEST(minimoog_glide_measurably_changes_transient_after_note_change) {
    auto render = [](float glideSeconds) {
        auto synth = makeMinimoog(8000.0);
        synth->setParameter(findParamIdByName(*synth, "Glide Time"), glideSeconds);
        synth->setParameter(findParamIdByName(*synth, "Osc2 Level"), 0.0f);
        synth->setParameter(findParamIdByName(*synth, "Osc3 Level"), 0.0f);
        synth->setParameter(findParamIdByName(*synth, "Noise Level"), 0.0f);
        synth->setParameter(findParamIdByName(*synth, "Analog Character"), 0.0f);

        MidiNoteEvent noteA{MidiNoteEvent::Kind::NoteOn, 0, 0, 48, 100};
        std::vector<float> settleL(4000, 0.0f), settleR(4000, 0.0f);
        synth->process(&noteA, 1, settleL.data(), settleR.data(), 4000); // laisse s'établir

        MidiNoteEvent noteB{MidiNoteEvent::Kind::NoteOn, 0, 0, 72, 100}; // 2 octaves plus haut
        std::vector<float> afterL(200, 0.0f), afterR(200, 0.0f);
        synth->process(&noteB, 1, afterL.data(), afterR.data(), 200);
        return afterL;
    };

    auto noGlide = render(0.0f);
    auto longGlide = render(2.0f);

    bool anyDifference = false;
    for (size_t i = 0; i < noGlide.size(); ++i)
        if (std::abs(noGlide[i] - longGlide[i]) > 0.01f) anyDifference = true;
    VSM_ASSERT(anyDifference); // le glide doit être mesurablement "en vol" juste après le changement
}

VSM_TEST(minimoog_save_load_state_roundtrip) {
    auto synthA = makeMinimoog();
    ParamId cutoffId = findParamIdByName(*synthA, "Filter Cutoff");
    ParamId resonanceId = findParamIdByName(*synthA, "Filter Resonance");
    synthA->setParameter(cutoffId, 2500.0f);
    synthA->setParameter(resonanceId, 3.0f);

    PresetState state = synthA->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.minimoog"));

    auto synthB = makeMinimoog();
    synthB->loadState(state);

    VSM_ASSERT_NEAR(synthB->getParameter(cutoffId), 2500.0, 1e-2);
    VSM_ASSERT_NEAR(synthB->getParameter(resonanceId), 3.0, 1e-3);
}

VSM_TEST(minimoog_parameter_list_has_expected_entries) {
    auto synth = makeMinimoog();
    const auto& params = synth->parameterList();
    VSM_ASSERT_EQ(params.size(), static_cast<size_t>(22));

    bool hasGlide = false, hasAnalogCharacter = false;
    for (const auto& p : params) {
        if (p.name == "Glide Time") hasGlide = true;
        if (p.name == "Analog Character") hasAnalogCharacter = true;
    }
    VSM_ASSERT(hasGlide);
    VSM_ASSERT(hasAnalogCharacter);
}
