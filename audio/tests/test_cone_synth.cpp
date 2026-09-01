#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

// `vsm.cone` — l'anche sur perce conique : saxophone, hautbois, basson.
//
// CE QUE CETTE SUITE VERROUILLE, ET POURQUOI C'EST UNE HISTOIRE. La case
// « saxophone, hautbois, flûte » du tableau de couverture est restée vide à
// travers CINQ topologies mesurées et rejetées (ARCHITECTURE § 33 et § 44).
// Le § 44 a fini par nommer le vrai obstacle — la formulation du COUPLAGE —
// et la sixième mesure l'a tranché en trois temps (CDC machines § 14) :
// l'anche bat déjà, un limiteur impair symétrise l'onde vers le carré, un
// limiteur ASYMÉTRIQUE rend le rang pair au point même qui fixe le cycle.
// Les seuils d'à-côté sont eux aussi mesurés : la falaise du sous-harmonique
// (asymétrie -1,4 ; souffle effectif 0,785) et la capture de mode à
// l'attaque (le mordant doit venir APRÈS l'émission). Une version antérieure
// de ce fichier testait des réglages (« Cone Taper », « Bore Decay ») d'une
// itération qui n'a jamais oscillé proprement ; elle est remplacée.

namespace {
constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeCone(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.cone");
    if (plugin) plugin->initialize(sr, 512);
    return plugin;
}
ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList()) if (info.name == name) return info.id;
    return 0;
}
void set(ISynthPlugin& plugin, const std::string& name, float value) {
    plugin.setParameter(byName(plugin, name), value);
}
MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity = 100) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}
float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float v : b) p = std::max(p, std::abs(v)); return p;
}
std::vector<float> render(SynthPluginPtr& synth, const std::vector<MidiNoteEvent>& events, int frames) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    for (int start = 0; start < frames; start += kBlock) {
        const int count = std::min(kBlock, frames - start);
        std::vector<MidiNoteEvent> block;
        for (const auto& e : events)
            if (e.sampleOffset >= start && e.sampleOffset < start + count)
                block.push_back({e.kind, e.sampleOffset - start, e.channel, e.note, e.velocity});
        synth->process(block.empty() ? nullptr : block.data(), static_cast<int>(block.size()),
                       left.data() + start, right.data() + start, count);
    }
    return left;
}
float midiToHz(int note) { return 440.0f * std::exp2f((static_cast<float>(note) - 69.0f) / 12.0f); }

double magnitudeAt(const std::vector<float>& x, size_t from, size_t count, double hz) {
    double re = 0.0, im = 0.0, norm = 0.0;
    for (size_t i = 0; i < count && from + i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count));
        const double phase = 2.0 * M_PI * hz * static_cast<double>(i) / kSampleRate;
        re += w * static_cast<double>(x[from + i]) * std::cos(phase);
        im += w * static_cast<double>(x[from + i]) * std::sin(phase);
        norm += w;
    }
    return std::sqrt(re * re + im * im) / std::max(1.0, norm);
}

double pitchOf(const std::vector<float>& x, size_t from, size_t count) {
    const float* p = x.data() + from;
    const size_t lagMin = static_cast<size_t>(kSampleRate / 2000.0);
    const size_t lagMax = std::min(count / 2, static_cast<size_t>(kSampleRate / 40.0));
    double best = -1.0; size_t bestLag = lagMin;
    for (size_t lag = lagMin; lag <= lagMax; ++lag) {
        double acc = 0.0;
        for (size_t i = 0; i + lag < count; ++i)
            acc += static_cast<double>(p[i]) * p[i + lag];
        if (acc > best) { best = acc; bestLag = lag; }
    }
    return kSampleRate / static_cast<double>(bestLag);
}

/// Réglage de base des mesures de timbre : pas de dérive, pas de vibrato, pas
/// de souffle turbulent -- on mesure la PERCE et son limiteur, pas ce qui les
/// anime. Brassiness reste au défaut : le limiteur est structurel.
void steady(ISynthPlugin& plugin) {
    set(plugin, "Analog Character", 0.0f);
    set(plugin, "Vibrato Depth", 0.0f);
    set(plugin, "Breath Noise", 0.0f);
    set(plugin, "Tone Bass", 0.0f);
    set(plugin, "Tone Treble", 0.0f);
}
} // namespace

VSM_TEST(cone_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.cone"));
}

VSM_TEST(cone_silent_with_no_events) {
    auto synth = makeCone();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(cone_note_produces_sound_and_stays_finite) {
    auto synth = makeCone();
    const auto audio = render(synth, {noteOn(0, 58, 100)}, 48128);
    VSM_ASSERT(peakAbs(audio) > 0.01f);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
}

VSM_TEST(cone_oscillation_needs_breath) {
    // Pas de souffle, pas de note : la course du souffle part de zéro, comme
    // `vsm.wind` -- une recompression qui décollerait le zéro ferait sonner
    // une machine qu'on n'a pas soufflée.
    auto synth = makeCone();
    steady(*synth);
    set(*synth, "Breath Pressure", 0.0f);
    const auto audio = render(synth, {noteOn(0, 60, 110)}, 24064);
    VSM_ASSERT(peakAbs(audio) < 1e-3f);
}

// --- LE trait distinctif : la perce conique porte les rangs PAIRS ----------

VSM_TEST(cone_bore_carries_even_harmonics) {
    // Le miroir exact de `wind_bore_supports_only_odd_harmonics`, et la
    // raison d'être de la machine : un cône résonne sur TOUS les multiples de
    // f0, et le limiteur asymétrique fait vivre le rang 2 -- mesuré à
    // h2/h1 = 0,37 en moyenne sur 45 configurations (cible : le ténor réel du
    // CDC § 14, h2/h1 = 0,42). Le seuil du test garde une marge (0,20) : il
    // verrouille la PRÉSENCE structurelle du pair, pas un point de réglage.
    auto synth = makeCone();
    steady(*synth);
    set(*synth, "Breath Pressure", 0.7f);
    set(*synth, "Attack", 0.01f);

    const int note = 58;
    const double f0 = midiToHz(note);
    const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 110)}, 98304);

    const double h1 = magnitudeAt(audio, 65536, 32768, f0);
    const double h2 = magnitudeAt(audio, 65536, 32768, 2.0 * f0);
    const double h4 = magnitudeAt(audio, 65536, 32768, 4.0 * f0);
    VSM_ASSERT(h1 > 0.01);              // ça sonne vraiment
    VSM_ASSERT(h2 > h1 * 0.20);         // le rang 2 est là -- un saxophone
    VSM_ASSERT(h4 > h1 * 0.10);         // le rang 4 aussi
}

VSM_TEST(cone_stays_on_the_fundamental_across_the_knobs) {
    // Les deux pannes mesurées de cette boucle sont des sauts de mode : le
    // SOUS-harmonique au sur-souffle (-1200 cents), le mode HAUT à l'attaque
    // quand le mordant monte trop vite (+2521 cents). Les gardes sont la
    // course de souffle plafonnée à 0,75 et le mordant asservi à un souffle
    // ralenti ; ce test parcourt les coins du panneau et exige la note
    // demandée, à ±40 cents.
    for (float breath : {0.4f, 1.0f}) {
        for (float brass : {0.0f, 1.0f}) {
            for (int note : {46, 58, 70}) {
                auto synth = makeCone();
                steady(*synth);
                set(*synth, "Breath Pressure", breath);
                set(*synth, "Brassiness", brass);
                const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 100)}, 98304);
                const double mesure = pitchOf(audio, 48000, 48000);
                const double cents = 1200.0 * std::log2(mesure / midiToHz(note));
                VSM_ASSERT(std::abs(cents) < 40.0);
            }
        }
    }
}

VSM_TEST(cone_tuning_is_compensated_at_f0_not_at_dc) {
    // HC1 du CDC machines § 14 : la phase de la cascade de boucle (deux pôles
    // de perte, apex) se compense À f0. Compensée au continu, la note sortait
    // de +36 à +78 cents, l'écart croissant vers le grave -- la signature de
    // l'AVANCE de phase de l'apex, ignorée. Mesuré après correction : -13 à
    // +8 cents sur 45 configurations.
    for (int note : {46, 65}) {
        auto synth = makeCone();
        steady(*synth);
        set(*synth, "Breath Pressure", 0.7f);
        const auto audio = render(synth, {noteOn(0, static_cast<uint8_t>(note), 100)}, 98304);
        const double mesure = pitchOf(audio, 48000, 48000);
        const double cents = 1200.0 * std::log2(mesure / midiToHz(note));
        VSM_ASSERT(std::abs(cents) < 30.0);
    }
}

VSM_TEST(cone_level_is_regulated_not_runaway) {
    // Gain de régénération au-dessus du seuil + limiteur : le cycle limite
    // tient, il ne s'éteint ni ne s'emballe (mesuré : rms 0,19-0,21 sur la
    // grille). La fenêtre mesurée saute l'attaque.
    auto synth = makeCone();
    steady(*synth);
    set(*synth, "Breath Pressure", 0.7f);
    const auto audio = render(synth, {noteOn(0, 58, 100)}, 98304);
    double rms = 0.0;
    for (size_t i = 48000; i < audio.size(); ++i)
        rms += static_cast<double>(audio[i]) * audio[i];
    rms = std::sqrt(rms / static_cast<double>(audio.size() - 48000));
    VSM_ASSERT(rms > 0.02);
    VSM_ASSERT(peakAbs(audio) < 1.0f);
}

VSM_TEST(cone_honours_pitch_bend_and_mod_wheel) {
    // La doctrine du § 10 du CDC nouvelle machine : une anche a un geste de
    // hauteur continue, elle honore les deux molettes -- et refuse le reste
    // en le disant.
    auto avecBend = makeCone();
    steady(*avecBend);
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(avecBend->handleControlEvent(bend));
    const auto haut = render(avecBend, {noteOn(0, 58, 100)}, 72000);

    auto temoin = makeCone();
    steady(*temoin);
    const auto nu = render(temoin, {noteOn(0, 58, 100)}, 72000);

    const double fHaut = pitchOf(haut, 36000, 32000);
    const double fNu = pitchOf(nu, 36000, 32000);
    VSM_ASSERT(fHaut > fNu * 1.05);     // deux demi-tons = +12,2 %

    MidiControlEvent cc;
    cc.kind = MidiControlEvent::Kind::ControlChange;
    cc.index = 1;
    cc.value = 1.0f;
    VSM_ASSERT(temoin->handleControlEvent(cc));
    MidiControlEvent autre;
    autre.kind = MidiControlEvent::Kind::ControlChange;
    autre.index = 7;                     // le volume MIDI n'a pas de sens ici
    VSM_ASSERT(!temoin->handleControlEvent(autre));
}
