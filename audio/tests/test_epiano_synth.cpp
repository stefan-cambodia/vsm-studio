#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {
SynthPluginPtr makeEPiano(double sr = 48000.0) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.epiano");
    if (plugin) plugin->initialize(sr, 512);
    return plugin;
}
/// UN NOM INCONNU EST UNE ERREUR, ET IL DOIT LE DIRE. Ce helper renvoyait 0
/// quand il ne trouvait pas le paramètre ; `setParameter(0, v)` ne fait rien
/// et ne se plaint pas, si bien qu'un test réglant `"Damping"` sur une machine
/// qui expose `"String Damping"` mesurait la machine par défaut en croyant
/// mesurer autre chose. C'est arrivé au banc de H10 (CDC machines-manquantes,
/// § 12) : quatre lignes d'un balayage étaient identiques sans que cela
/// alerte, et il s'en est fallu de peu qu'on écrive une machine inutile sur
/// cette base. Panne muette interdite, ici comme ailleurs.
ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList()) if (info.name == name) return info.id;
    throw vsm::test::AssertionFailure("paramètre inconnu : « " + name + " » — la machine expose d'autres noms");
}
MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity = 100) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}
float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float v : b) p = std::max(p, std::abs(v)); return p;
}
/// Énergie des aigus : la différence première accentue les hautes fréquences.
double brightness(const std::vector<float>& b) {
    double energy = 0.0;
    for (size_t i = 1; i < b.size(); ++i) {
        const double d = static_cast<double>(b[i]) - b[i - 1];
        energy += d * d;
    }
    return energy;
}
std::vector<float> renderNote(SynthPluginPtr& synth, uint8_t note, uint8_t velocity, int frames = 24000) {
    const auto event = noteOn(0, note, velocity);
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    synth->process(&event, 1, left.data(), right.data(), frames);
    return left;
}
} // namespace

VSM_TEST(epiano_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.epiano"));
}

VSM_TEST(epiano_silent_with_no_events) {
    auto synth = makeEPiano();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(epiano_note_produces_sound) {
    auto synth = makeEPiano();
    const auto audio = renderNote(synth, 60, 100);
    VSM_ASSERT(peakAbs(audio) > 0.02f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

VSM_TEST(epiano_velocity_changes_timbre_not_only_level) {
    // LE trait de cet instrument : frapper fort ne fait pas qu'augmenter le
    // volume, cela réveille la cloche et le choc du marteau. On compare donc
    // la brillance À NIVEAU NORMALISÉ -- sinon on ne mesurerait que le gain.
    auto soft = makeEPiano();
    auto hard = makeEPiano();
    const auto quiet = renderNote(soft, 60, 30);
    const auto loud = renderNote(hard, 60, 127);

    const double quietBrightness = brightness(quiet) / std::max(1e-9, static_cast<double>(peakAbs(quiet)));
    const double loudBrightness = brightness(loud) / std::max(1e-9, static_cast<double>(peakAbs(loud)));
    VSM_ASSERT(loudBrightness > quietBrightness * 1.5);
}

VSM_TEST(epiano_bell_level_adds_high_partials) {
    auto dull = makeEPiano();
    auto bright = makeEPiano();
    dull->setParameter(byName(*dull, "Bell Level"), 0.0f);
    bright->setParameter(byName(*bright, "Bell Level"), 1.0f);
    VSM_ASSERT(brightness(renderNote(bright, 60, 120)) > brightness(renderNote(dull, 60, 120)) * 1.3);
}

VSM_TEST(epiano_has_no_sustain_plateau) {
    // Une lame décroît continûment jusqu'à l'étouffoir : elle n'a pas de
    // palier. Un ADSR à sustain donnerait un orgue, pas un piano.
    auto synth = makeEPiano();
    synth->setParameter(byName(*synth, "Tine Decay"), 2.0f);
    const auto audio = renderNote(synth, 60, 110, 96000);

    auto windowPeak = [&audio](size_t from, size_t count) {
        float p = 0.0f;
        for (size_t i = from; i < from + count && i < audio.size(); ++i) p = std::max(p, std::abs(audio[i]));
        return p;
    };
    const float early = windowPeak(1000, 4000);
    const float late = windowPeak(80000, 4000);
    VSM_ASSERT(early > 0.01f);
    VSM_ASSERT(late < early * 0.35f); // ça décroît vraiment
}

VSM_TEST(epiano_tremolo_is_stereo) {
    // Sur ces instruments le trémolo fait passer le son d'une enceinte à
    // l'autre : le réduire à une modulation d'amplitude perdrait ce qui le
    // rend reconnaissable.
    auto synth = makeEPiano();
    synth->setParameter(byName(*synth, "Tremolo Depth"), 1.0f);
    synth->setParameter(byName(*synth, "Tremolo Stereo"), 1.0f);
    synth->setParameter(byName(*synth, "Tremolo Rate"), 6.0f);

    const auto event = noteOn(0, 60, 110);
    std::vector<float> left(48000, 0.0f), right(48000, 0.0f);
    synth->process(&event, 1, left.data(), right.data(), 48000);

    size_t differing = 0;
    for (size_t i = 0; i < left.size(); ++i)
        if (std::abs(left[i] - right[i]) > 1e-5f) ++differing;
    VSM_ASSERT(differing > left.size() / 4);
}

VSM_TEST(epiano_character_changes_the_timbre) {
    // De la lame frappée vers l'anche : les partiels se resserrent vers
    // l'harmonique, le son change de famille.
    auto tine = makeEPiano();
    auto reed = makeEPiano();
    tine->setParameter(byName(*tine, "Character"), 0.0f);
    reed->setParameter(byName(*reed, "Character"), 1.0f);
    const auto a = renderNote(tine, 55, 110), b = renderNote(reed, 55, 110);

    bool different = false;
    for (size_t i = 0; i < a.size(); ++i) if (std::abs(a[i] - b[i]) > 1e-4f) different = true;
    VSM_ASSERT(different);
}

VSM_TEST(epiano_is_polyphonic) {
    auto synth = makeEPiano();
    std::vector<MidiNoteEvent> chord = {noteOn(0, 48), noteOn(0, 52), noteOn(0, 55)};
    std::vector<float> left(4000, 0.0f), right(4000, 0.0f);
    synth->process(chord.data(), 3, left.data(), right.data(), 4000);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 3);
}

VSM_TEST(epiano_is_deterministic) {
    auto render = [] {
        auto synth = makeEPiano();
        synth->setParameter(byName(*synth, "Analog Character"), 0.8f);
        return renderNote(synth, 57, 100, 12000);
    };
    const auto a = render(), b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(epiano_save_load_roundtrip) {
    auto source = makeEPiano();
    source->setParameter(byName(*source, "Bell Level"), 0.85f);
    source->setParameter(byName(*source, "Tine Decay"), 7.5f);
    const auto state = source->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.epiano"));

    auto target = makeEPiano();
    target->loadState(state);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Bell Level")), 0.85f, 1e-6);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Tine Decay")), 7.5f, 1e-6);
}

VSM_TEST(epiano_parameter_list_size) {
    auto synth = makeEPiano();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{15});
}
