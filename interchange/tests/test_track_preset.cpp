#include "TestFramework.h"
#include "vsm/interchange/TrackPreset.h"

using namespace vsm::interchange;
using vsm::sequencer::Track;

// D22.5 -- LES PRESETS DE PISTE. Un aller-retour disque exact des RÉGLAGES
// d'une piste ; son contenu (notes, clips) n'y entre pas et n'en sort pas.

namespace {
Track pisteReglee() {
    Track piste;
    piste.kind = Track::Kind::Midi;
    piste.name = "Basse";
    piste.instrumentId = "vsm.minimoog";
    piste.channel = 3;
    piste.colorRgba = 0xFF112233u;
    piste.volume = 0.5f;
    piste.pan = -0.25f;
    piste.invertPhase = true;
    piste.sendLevels = {0.1f, 0.75f};
    piste.transposeSemitones = -12;
    piste.delayMs = 7.5;
    piste.outputGroup = 2;
    vsm::sequencer::TrackEffect reverb;
    reverb.type = "reverb";
    reverb.parameters = {{"reverb.1.mix", 0.35f}};
    reverb.enabled = false;
    vsm::sequencer::TrackEffect comp;
    comp.type = "compressor";
    comp.parameters = {{"compressor.1.ratio", 4.0f}};
    piste.effects = {reverb, comp};
    vsm::sequencer::Note n;
    n.startTick = 0; n.endTick = 480; n.number = 36; n.id = 1;
    piste.notes.push_back(n);
    return piste;
}
} // namespace

VSM_TEST(a_track_preset_round_trips_through_json_and_carries_no_notes) {
    const Track piste = pisteReglee();
    SynthPreset synth;
    synth.name = "Basse ronde";
    synth.pluginId = "vsm.minimoog";
    synth.values = {{"osc.1.wave", 2.0f}, {"filter.cutoff", 850.0f}};
    const TrackPreset preset = trackPresetFromTrack(piste, "Basse ronde", synth);
    const std::string texte = trackPresetToJson(preset).toString();
    VSM_ASSERT(texte.find("\"notes\"") == std::string::npos);
    VSM_ASSERT(texte.find("\"clips\"") == std::string::npos);

    const auto relu = parseTrackPreset(texte);
    VSM_ASSERT(relu.success);
    const TrackPreset& r = relu.preset;
    VSM_ASSERT_EQ(r.name, std::string("Basse ronde"));
    VSM_ASSERT_EQ(r.kind, std::string("midi"));
    VSM_ASSERT_EQ(r.instrumentId, std::string("vsm.minimoog"));
    VSM_ASSERT(r.synth.has_value());
    VSM_ASSERT_EQ(r.synth->values.at("filter.cutoff"), 850.0f);
    VSM_ASSERT_EQ(r.effects.size(), size_t(2));
    VSM_ASSERT_EQ(r.effects[0].type, std::string("reverb"));
    VSM_ASSERT(!r.effectsEnabled[0]);
    VSM_ASSERT(r.effectsEnabled[1]);
    VSM_ASSERT_EQ(r.sendLevels.size(), size_t(2));
    VSM_ASSERT_EQ(r.sendLevels[1], 0.75f);
    VSM_ASSERT_EQ(r.volume, 0.5f);
    VSM_ASSERT_EQ(r.pan, -0.25f);
    VSM_ASSERT(r.invertPhase);
    VSM_ASSERT_EQ(r.colorRgba, 0xFF112233u);
    VSM_ASSERT_EQ(r.channel, 3);
    VSM_ASSERT_EQ(r.transposeSemitones, -12);
    VSM_ASSERT_NEAR(r.delayMs, 7.5, 1e-9);
    VSM_ASSERT_EQ(r.outputGroup, 2);
}

VSM_TEST(applying_a_track_preset_keeps_the_content_and_replaces_the_settings) {
    const TrackPreset preset = trackPresetFromTrack(pisteReglee(), "Basse ronde", std::nullopt);
    Track cible;
    cible.name = "Lead";
    cible.instrumentId = "vsm.juno106";
    cible.volume = 1.0f;
    cible.solo = true;
    cible.muted = true;
    cible.armed = true;
    vsm::sequencer::Note n;
    n.startTick = 960; n.endTick = 1440; n.number = 72; n.id = 9;
    cible.notes.push_back(n);
    vsm::sequencer::Clip clip;
    clip.id = 4; clip.startTick = 0; clip.length = 1920;
    cible.clips.push_back(clip);

    applyTrackPresetToTrack(preset, cible);
    // Le contenu et les états de session restent.
    VSM_ASSERT_EQ(cible.name, std::string("Lead"));
    VSM_ASSERT_EQ(cible.notes.size(), size_t(1));
    VSM_ASSERT_EQ(cible.notes[0].number, static_cast<uint8_t>(72));
    VSM_ASSERT_EQ(cible.clips.size(), size_t(1));
    VSM_ASSERT(cible.solo);
    VSM_ASSERT(cible.muted);
    VSM_ASSERT(cible.armed);
    // Les réglages sont ceux du preset.
    VSM_ASSERT_EQ(cible.instrumentId, std::string("vsm.minimoog"));
    VSM_ASSERT_EQ(cible.effects.size(), size_t(2));
    VSM_ASSERT(!cible.effects[0].enabled);
    VSM_ASSERT_EQ(cible.volume, 0.5f);
    VSM_ASSERT(cible.invertPhase);
    VSM_ASSERT_EQ(cible.transposeSemitones, -12);
    VSM_ASSERT_EQ(cible.channel, static_cast<uint8_t>(3));
}

VSM_TEST(an_audio_preset_on_a_midi_track_keeps_its_machine) {
    Track audio;
    audio.kind = Track::Kind::Audio;
    audio.volume = 0.7f;
    const TrackPreset preset = trackPresetFromTrack(audio, "Voix", std::nullopt);
    VSM_ASSERT_EQ(preset.kind, std::string("audio"));
    Track midi;
    midi.instrumentId = "vsm.dx7";
    applyTrackPresetToTrack(preset, midi);
    VSM_ASSERT_EQ(midi.instrumentId, std::string("vsm.dx7"));
    VSM_ASSERT_EQ(midi.volume, 0.7f);
}

VSM_TEST(a_track_preset_of_another_format_or_version_is_refused_by_name) {
    const auto synth = parseTrackPreset(R"({"format":"vsm-synth-preset","version":1})");
    VSM_ASSERT(!synth.success);
    VSM_ASSERT(synth.error.find("vsm-synth-preset") != std::string::npos);
    const auto futur = parseTrackPreset(R"({"format":"vsm-track-preset","version":2})");
    VSM_ASSERT(!futur.success);
    VSM_ASSERT(futur.error.find("2") != std::string::npos);
    const auto genre = parseTrackPreset(R"({"format":"vsm-track-preset","version":1,"kind":"bus"})");
    VSM_ASSERT(!genre.success);
    const auto casse = parseTrackPreset("{");
    VSM_ASSERT(!casse.success);
    VSM_ASSERT(isTrackPresetFile("bibliotheque/pistes/Basse.TRACK.json"));
    VSM_ASSERT(!isTrackPresetFile("bibliotheque/effets/Salle.effect.json"));
}
