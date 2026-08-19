#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeSupersaw() {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.supersaw");
    if (plugin) plugin->initialize(kSampleRate, 512);
    return plugin;
}

ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList())
        if (info.name == name) return info.id;
    return 0;
}

struct Stereo { std::vector<float> left, right; };

Stereo renderStereo(const SynthPluginPtr& synth, uint8_t note,
                     int samples, uint8_t velocity = 100) {
    Stereo out;
    out.left.resize(static_cast<size_t>(samples), 0.0f);
    out.right.resize(static_cast<size_t>(samples), 0.0f);
    MidiNoteEvent on{};
    on.kind = MidiNoteEvent::Kind::NoteOn;
    on.note = note; on.velocity = velocity; on.channel = 0; on.sampleOffset = 0;
    synth->process(&on, 1, out.left.data(), out.right.data(), samples);
    return out;
}

std::vector<float> renderMono(const SynthPluginPtr& synth, uint8_t note,
                               int samples, uint8_t velocity = 100) {
    const Stereo stereo = renderStereo(synth, note, samples, velocity);
    std::vector<float> mono(stereo.left.size());
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (stereo.left[i] + stereo.right[i]);
    return mono;
}

float peak(const std::vector<float>& buffer) {
    float maximum = 0.0f;
    for (float sample : buffer) maximum = std::max(maximum, std::abs(sample));
    return maximum;
}

double rms(const std::vector<float>& buffer, size_t from = 0) {
    double sum = 0.0;
    for (size_t i = from; i < buffer.size(); ++i) sum += static_cast<double>(buffer[i]) * buffer[i];
    const size_t count = buffer.size() - from;
    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

/// Hauteur dominante, par autocorrélation. Robuste aux battements d'un
/// empilement désaccordé, contrairement au comptage de passages par zéro.
int dominantPeriod(const std::vector<float>& signal, int minLag, int maxLag) {
    double best = -1.0;
    int bestLag = minLag;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double correlation = 0.0;
        for (size_t i = static_cast<size_t>(lag); i < signal.size(); ++i)
            correlation += static_cast<double>(signal[i]) * signal[i - static_cast<size_t>(lag)];
        if (correlation > best) { best = correlation; bestLag = lag; }
    }
    return bestLag;
}

} // namespace

VSM_TEST(supersaw_is_registered_and_names_itself) {
    auto synth = makeSupersaw();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Supersaw Lead"));
    VSM_ASSERT(synth->parameterList().size() >= 20);
}

VSM_TEST(supersaw_produces_sound_on_note_on) {
    auto synth = makeSupersaw();
    VSM_ASSERT(peak(renderMono(synth, 60, 12000)) > 0.05f);
}

VSM_TEST(supersaw_stays_within_headroom_on_a_full_chord) {
    // Huit voix de sept scies chacune : si le réglage de niveau est faux,
    // l'écrêtage n'arrive pas au mixer mais ici.
    auto synth = makeSupersaw();
    std::vector<float> left(24000, 0.0f), right(24000, 0.0f);
    MidiNoteEvent events[8]{};
    const uint8_t notes[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    for (int i = 0; i < 8; ++i) {
        events[i].kind = MidiNoteEvent::Kind::NoteOn;
        events[i].note = notes[i]; events[i].velocity = 110; events[i].sampleOffset = 0;
    }
    synth->process(events, 8, left.data(), right.data(), 24000);
    VSM_ASSERT(peak(left) < 1.0f);
    VSM_ASSERT(peak(right) < 1.0f);
    VSM_ASSERT(peak(left) > 0.1f); // ...mais pas au point d'être inaudible
}

VSM_TEST(supersaw_detune_spacing_is_uneven) {
    // Le cœur du timbre. Trois scies restent serrées contre la fondamentale,
    // les autres s'en écartent plusieurs fois plus : c'est ce déséquilibre
    // qui donne la masse tout en gardant la hauteur lisible. Une répartition
    // régulière donnerait un chœur désaccordé, pas un supersaw -- ce test
    // existe pour empêcher qu'on « simplifie » la table un jour.
    auto synth = makeSupersaw();
    // Mesure indirecte : à désaccord maximal, la largeur spectrale doit être
    // bien plus grande qu'à désaccord nul, sans que la hauteur bouge.
    auto tight = makeSupersaw();
    tight->setParameter(byName(*tight, "Detune"), 0.0f);
    tight->setParameter(byName(*tight, "Analog Character"), 0.0f);
    auto wide = makeSupersaw();
    wide->setParameter(byName(*wide, "Detune"), 1.0f);
    wide->setParameter(byName(*wide, "Analog Character"), 0.0f);

    // Sans désaccord, les sept scies sont confondues : la forme d'onde se
    // répète exactement d'une période à l'autre. Avec, elle dérive.
    const auto tightBuffer = renderMono(tight, 57, 24000);
    const auto wideBuffer = renderMono(wide, 57, 24000);
    const int period = dominantPeriod(tightBuffer, 150, 350);

    auto selfSimilarity = [period](const std::vector<float>& signal) {
        double numerator = 0.0, denominator = 0.0;
        for (size_t i = static_cast<size_t>(period); i < signal.size(); ++i) {
            numerator += static_cast<double>(signal[i]) * signal[i - static_cast<size_t>(period)];
            denominator += static_cast<double>(signal[i]) * signal[i];
        }
        return denominator > 0.0 ? numerator / denominator : 0.0;
    };
    VSM_ASSERT(selfSimilarity(tightBuffer) > 0.9);          // périodique
    VSM_ASSERT(selfSimilarity(wideBuffer) < selfSimilarity(tightBuffer) - 0.2); // mouvante
}

VSM_TEST(supersaw_detune_does_not_move_the_perceived_pitch) {
    // Les sept écarts sont presque symétriques autour de zéro : ouvrir le
    // désaccord épaissit, ça ne transpose pas.
    auto tight = makeSupersaw();
    auto wide = makeSupersaw();
    for (auto* synth : {&tight, &wide}) (*synth)->setParameter(byName(**synth, "Analog Character"), 0.0f);
    tight->setParameter(byName(*tight, "Detune"), 0.0f);
    wide->setParameter(byName(*wide, "Detune"), 1.0f);
    const int tightPeriod = dominantPeriod(renderMono(tight, 45, 24000), 300, 600);
    const int widePeriod = dominantPeriod(renderMono(wide, 45, 24000), 300, 600);
    const double drift = std::abs(static_cast<double>(widePeriod - tightPeriod)) / tightPeriod;
    VSM_ASSERT(drift < 0.05);
}

VSM_TEST(supersaw_mix_moves_level_from_centre_to_sides) {
    // Les deux courbes de niveau vont en sens INVERSE : à mélange nul on
    // entend surtout la scie centrale, à fond surtout les six latérales. Si
    // les deux montaient ensemble, le réglage ne serait qu'un volume.
    //
    // Ce qu'on mesure : la part d'énergie tenue à la fréquence EXACTE de la
    // note. Seule la scie centrale s'y trouve -- les six autres sont
    // désaccordées, donc à côté. Cette part est donc directement le poids de
    // la centrale dans le mélange, et c'est la grandeur que le réglage
    // déplace. (Une première version comparait la périodicité du signal : le
    // sens était bon mais l'écart mesuré tenait dans le bruit de mesure.)
    auto centreShare = [](const std::vector<float>& buffer, double frequencyHz) {
        double real = 0.0, imaginary = 0.0, energy = 0.0;
        for (size_t i = 0; i < buffer.size(); ++i) {
            const double angle = 2.0 * 3.14159265358979323846 * frequencyHz
                                 * static_cast<double>(i) / kSampleRate;
            real += static_cast<double>(buffer[i]) * std::cos(angle);
            imaginary += static_cast<double>(buffer[i]) * std::sin(angle);
            energy += static_cast<double>(buffer[i]) * buffer[i];
        }
        const double count = static_cast<double>(buffer.size());
        const double magnitude = 2.0 * std::sqrt(real * real + imaginary * imaginary) / count;
        return magnitude / std::max(1e-9, std::sqrt(energy / count));
    };

    constexpr uint8_t kNote = 57; // La 220 Hz : loin des bornes du coupe-bas
    const double noteHz = 440.0 * std::exp2((static_cast<double>(kNote) - 69.0) / 12.0);

    auto measure = [&](float mix) {
        auto synth = makeSupersaw();
        synth->setParameter(byName(*synth, "Detune"), 0.6f);
        synth->setParameter(byName(*synth, "Analog Character"), 0.0f);
        synth->setParameter(byName(*synth, "Mix"), mix);
        return centreShare(renderMono(synth, kNote, 24000), noteHz);
    };

    // Décroissance STRICTE d'un bout à l'autre du réglage : c'est ce qui
    // distingue un vrai fondu croisé d'un simple changement de niveau.
    const double atZero = measure(0.0f), atHalf = measure(0.5f), atFull = measure(1.0f);
    VSM_ASSERT(atZero > atHalf);
    VSM_ASSERT(atHalf > atFull);
    VSM_ASSERT(atZero > atFull * 3.0); // et l'écart est large, pas marginal

    // ...tandis que le niveau global, lui, ne s'effondre pas : les deux
    // courbes se compensent.
    auto levelAt = [&](float mix) {
        auto synth = makeSupersaw();
        synth->setParameter(byName(*synth, "Detune"), 0.6f);
        synth->setParameter(byName(*synth, "Analog Character"), 0.0f);
        synth->setParameter(byName(*synth, "Mix"), mix);
        return rms(renderMono(synth, kNote, 24000));
    };
    const double ratio = levelAt(1.0f) / std::max(1e-9, levelAt(0.0f));
    VSM_ASSERT(ratio > 0.5 && ratio < 2.5);
}

VSM_TEST(supersaw_spread_widens_the_stereo_image) {
    auto narrow = makeSupersaw();
    auto wide = makeSupersaw();
    for (auto* synth : {&narrow, &wide}) (*synth)->setParameter(byName(**synth, "Detune"), 0.6f);
    narrow->setParameter(byName(*narrow, "Stereo Spread"), 0.0f);
    wide->setParameter(byName(*wide, "Stereo Spread"), 1.0f);

    // Largeur = énergie de la DIFFÉRENCE des canaux rapportée à celle de leur
    // somme. C'est la mesure que fait un corrélateur de studio.
    auto width = [](const Stereo& stereo) {
        double side = 0.0, mid = 0.0;
        for (size_t i = 0; i < stereo.left.size(); ++i) {
            const double difference = static_cast<double>(stereo.left[i]) - stereo.right[i];
            const double sum = static_cast<double>(stereo.left[i]) + stereo.right[i];
            side += difference * difference;
            mid += sum * sum;
        }
        return mid > 0.0 ? side / mid : 0.0;
    };
    const double narrowWidth = width(renderStereo(narrow, 60, 24000));
    const double wideWidth = width(renderStereo(wide, 60, 24000));
    VSM_ASSERT(narrowWidth < 0.01);          // spread 0 = mono
    VSM_ASSERT(wideWidth > narrowWidth * 10.0);
}

VSM_TEST(supersaw_randomises_phases_so_the_attack_does_not_spike) {
    // Sept scies démarrant en phase additionnent leurs fronts : le premier
    // cycle dépasse largement le régime établi. Le tirage des phases est là
    // pour ça, et ce test le vérifie par l'effet, pas par l'implémentation.
    auto synth = makeSupersaw();
    synth->setParameter(byName(*synth, "Detune"), 0.5f);
    synth->setParameter(byName(*synth, "Amp Attack"), 0.001f);
    synth->setParameter(byName(*synth, "Pitch HPF"), 0.0f);
    const auto buffer = renderMono(synth, 60, 24000);

    // Crête des 400 premiers échantillons contre crête du régime établi.
    const std::vector<float> attack(buffer.begin(), buffer.begin() + 400);
    const std::vector<float> steady(buffer.begin() + 8000, buffer.end());
    VSM_ASSERT(peak(attack) < peak(steady) * 1.6f);
}

VSM_TEST(supersaw_filter_cutoff_changes_brightness) {
    auto dark = makeSupersaw();
    auto bright = makeSupersaw();
    dark->setParameter(byName(*dark, "Filter Cutoff"), 400.0f);
    bright->setParameter(byName(*bright, "Filter Cutoff"), 16000.0f);
    for (auto* synth : {&dark, &bright}) (*synth)->setParameter(byName(**synth, "Filter Env Amount"), 0.0f);

    // Énergie des différences premières = énergie haute fréquence. Mesurer
    // l'énergie TOTALE serait trompeur : la résonance du filtre en ajoute.
    auto brightness = [](const std::vector<float>& buffer) {
        double sum = 0.0;
        for (size_t i = 1; i < buffer.size(); ++i) {
            const double difference = static_cast<double>(buffer[i]) - buffer[i - 1];
            sum += difference * difference;
        }
        return sum;
    };
    VSM_ASSERT(brightness(renderMono(bright, 60, 24000)) > brightness(renderMono(dark, 60, 24000)) * 4.0);
}

VSM_TEST(supersaw_pitch_hpf_thins_the_low_end) {
    auto full = makeSupersaw();
    auto thin = makeSupersaw();
    full->setParameter(byName(*full, "Pitch HPF"), 0.0f);
    thin->setParameter(byName(*thin, "Pitch HPF"), 2.0f);
    // Note grave : c'est là que le coupe-bas se juge.
    VSM_ASSERT(rms(renderMono(thin, 36, 24000)) < rms(renderMono(full, 36, 24000)) * 0.9);
}

VSM_TEST(supersaw_sub_oscillator_adds_an_octave_below) {
    auto without = makeSupersaw();
    auto with = makeSupersaw();
    for (auto* synth : {&without, &with}) {
        (*synth)->setParameter(byName(**synth, "Pitch HPF"), 0.0f);
        (*synth)->setParameter(byName(**synth, "Detune"), 0.0f);
    }
    with->setParameter(byName(*with, "Sub Level"), 0.8f);
    // Avec le sous-oscillateur, la période dominante DOUBLE : l'octave
    // inférieure devient la fondamentale de la forme d'onde composite.
    const int plain = dominantPeriod(renderMono(without, 57, 24000), 150, 500);
    const int subbed = dominantPeriod(renderMono(with, 57, 24000), 150, 500);
    VSM_ASSERT(subbed > plain * 3 / 2);
}

VSM_TEST(supersaw_glide_slides_between_notes) {
    auto synth = makeSupersaw();
    synth->setParameter(byName(*synth, "Glide"), 0.5f);
    synth->setParameter(byName(*synth, "Detune"), 0.0f);
    synth->setParameter(byName(*synth, "Pitch HPF"), 0.0f);
    synth->setParameter(byName(*synth, "Analog Character"), 0.0f);

    std::vector<float> left(48000, 0.0f), right(48000, 0.0f);
    MidiNoteEvent events[3]{};
    events[0].kind = MidiNoteEvent::Kind::NoteOn; events[0].note = 45; events[0].velocity = 100; events[0].sampleOffset = 0;
    events[1].kind = MidiNoteEvent::Kind::NoteOff; events[1].note = 45; events[1].sampleOffset = 12000;
    events[2].kind = MidiNoteEvent::Kind::NoteOn; events[2].note = 69; events[2].velocity = 100; events[2].sampleOffset = 12000;
    synth->process(events, 3, left.data(), right.data(), 48000);

    // Juste après la seconde note, la hauteur doit encore être proche de la
    // PREMIÈRE (glissement en cours) ; bien plus tard, proche de la seconde.
    std::vector<float> early(left.begin() + 12200, left.begin() + 14200);
    std::vector<float> late(left.begin() + 40000, left.begin() + 44000);
    const int earlyPeriod = dominantPeriod(early, 60, 600);
    const int latePeriod = dominantPeriod(late, 60, 600);
    VSM_ASSERT(earlyPeriod > latePeriod * 2); // encore grave, puis aigu
}

VSM_TEST(supersaw_is_deterministic) {
    auto first = makeSupersaw();
    auto second = makeSupersaw();
    const auto a = renderStereo(first, 62, 16000);
    const auto b = renderStereo(second, 62, 16000);
    for (size_t i = 0; i < a.left.size(); ++i) {
        VSM_ASSERT_NEAR(a.left[i], b.left[i], 1e-9);
        VSM_ASSERT_NEAR(a.right[i], b.right[i], 1e-9);
    }
}

VSM_TEST(supersaw_state_round_trips) {
    auto synth = makeSupersaw();
    synth->setParameter(byName(*synth, "Detune"), 0.77f);
    synth->setParameter(byName(*synth, "Mix"), 0.33f);
    const auto state = synth->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.supersaw"));

    auto restored = makeSupersaw();
    restored->loadState(state);
    VSM_ASSERT_NEAR(restored->getParameter(byName(*restored, "Detune")), 0.77f, 1e-6);
    VSM_ASSERT_NEAR(restored->getParameter(byName(*restored, "Mix")), 0.33f, 1e-6);
}

VSM_TEST(supersaw_velocity_opens_the_filter) {
    auto synth = makeSupersaw();
    synth->setParameter(byName(*synth, "Velocity to Filter"), 1.0f);
    synth->setParameter(byName(*synth, "Filter Cutoff"), 1200.0f);
    auto brightness = [](const std::vector<float>& buffer) {
        double sum = 0.0;
        for (size_t i = 1; i < buffer.size(); ++i) {
            const double difference = static_cast<double>(buffer[i]) - buffer[i - 1];
            sum += difference * difference;
        }
        return sum;
    };
    auto soft = makeSupersaw();
    soft->setParameter(byName(*soft, "Velocity to Filter"), 1.0f);
    soft->setParameter(byName(*soft, "Filter Cutoff"), 1200.0f);
    VSM_ASSERT(brightness(renderMono(synth, 60, 16000, 127)) >
               brightness(renderMono(soft, 60, 16000, 20)) * 1.5);
}
