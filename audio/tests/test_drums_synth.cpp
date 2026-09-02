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

SynthPluginPtr makeDrums(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.drums");
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
void set(ISynthPlugin& plugin, const std::string& name, float value) {
    plugin.setParameter(byName(plugin, name), value);
}
MidiNoteEvent hit(int offset, uint8_t note, uint8_t velocity = 100) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}
float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float v : b) p = std::max(p, std::abs(v)); return p;
}
float windowPeak(const std::vector<float>& x, size_t from, size_t count) {
    float p = 0.0f;
    for (size_t i = from; i < from + count && i < x.size(); ++i) p = std::max(p, std::abs(x[i]));
    return p;
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
double brightness(const std::vector<float>& b, size_t from, size_t count) {
    double energy = 0.0;
    for (size_t i = from + 1; i < from + count && i < b.size(); ++i) {
        const double d = static_cast<double>(b[i]) - b[i - 1];
        energy += d * d;
    }
    return energy;
}
} // namespace

VSM_TEST(drums_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.drums"));
}

VSM_TEST(drums_silent_with_no_events) {
    auto synth = makeDrums();
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(left), 0.0, 1e-9);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(drums_every_piece_produces_sound) {
    for (uint8_t note : {36, 38, 41, 45, 48, 42, 46, 49, 51}) {
        auto synth = makeDrums();
        const auto audio = render(synth, {hit(0, note, 110)}, 24000);
        VSM_ASSERT(peakAbs(audio) > 0.01f);
        for (float v : audio) VSM_ASSERT(std::isfinite(v));
    }
}

// --- Le trait distinctif : une PEAU, pas une sinusoïde --------------------

VSM_TEST(drums_membrane_partials_are_inharmonic) {
    // LE trait de cette machine. Une corde vibre sur des multiples entiers ;
    // une peau vibre sur les zéros de Bessel -- 1,000 / 1,593 / 2,135 -- et
    // c'est cette inharmonicité qu'on entend comme « peau » plutôt que comme
    // « note ». Une boîte à rythmes analogique n'a qu'une sinusoïde ; c'est
    // exactement ce qui la trahit sur un stem acoustique.
    auto synth = makeDrums();
    set(*synth, "Room Level", 0.0f);
    set(*synth, "Tom Tune", 120.0f);
    set(*synth, "Tom Decay", 1.2f);
    set(*synth, "Kick Level", 0.0f);
    const auto audio = render(synth, {hit(0, 41, 120)}, 32768);

    const double f0 = 120.0;
    // Le deuxième mode est à 1,593 f0, PAS à 2 f0. On vérifie les deux.
    const double atMode2 = magnitudeAt(audio, 1024, 16384, f0 * 1.593);
    const double atOctave = magnitudeAt(audio, 1024, 16384, f0 * 2.0);
    VSM_ASSERT(atMode2 > atOctave * 2.0);
}

VSM_TEST(drums_upper_modes_die_before_the_fundamental) {
    // Un coup est riche pendant trente millisecondes puis devient un
    // bourdonnement. Une enveloppe commune à tous les modes donnerait une
    // cloche, pas un tom.
    auto synth = makeDrums();
    set(*synth, "Room Level", 0.0f);
    set(*synth, "Tom Decay", 1.2f);
    const auto audio = render(synth, {hit(0, 41, 120)}, 48000);
    const double early = brightness(audio, 500, 4000) /
                         std::max(1e-9, static_cast<double>(windowPeak(audio, 500, 4000)));
    const double late = brightness(audio, 24000, 4000) /
                        std::max(1e-9, static_cast<double>(windowPeak(audio, 24000, 4000)));
    VSM_ASSERT(early > late * 2.0);
}

VSM_TEST(drums_velocity_changes_the_mode_balance_not_only_the_level) {
    // Frapper fort excite davantage les modes hauts. Comparé À NIVEAU
    // NORMALISÉ, sinon on ne mesurerait que le gain.
    auto soft = makeDrums();
    auto hard = makeDrums();
    for (auto* s : {&soft, &hard}) set(**s, "Room Level", 0.0f);
    const auto quiet = render(soft, {hit(0, 41, 25)}, 24000);
    const auto loud = render(hard, {hit(0, 41, 127)}, 24000);
    const double q = brightness(quiet, 0, 8000) / std::max(1e-9, static_cast<double>(peakAbs(quiet)));
    const double l = brightness(loud, 0, 8000) / std::max(1e-9, static_cast<double>(peakAbs(loud)));
    VSM_ASSERT(l > q * 1.3);
}

VSM_TEST(drums_snare_wires_outlast_the_head) {
    // Le timbre est ce qui fait la caisse claire : les frisés continuent de
    // vibrer après que la peau s'est tue.
    auto withWires = makeDrums();
    auto without = makeDrums();
    for (auto* s : {&withWires, &without}) {
        set(**s, "Room Level", 0.0f);
        set(**s, "Snare Decay", 0.12f);
    }
    set(*withWires, "Snare Wires", 1.0f);
    set(*without, "Snare Wires", 0.0f);
    const auto a = render(withWires, {hit(0, 38, 110)}, 24000);
    const auto b = render(without, {hit(0, 38, 110)}, 24000);
    // Tard, quand la peau est éteinte, seul le timbre reste.
    VSM_ASSERT(windowPeak(a, 9000, 3000) > windowPeak(b, 9000, 3000) * 2.0f);
}

VSM_TEST(drums_closed_hat_chokes_the_open_hat) {
    // Deux positions d'un même instrument ne peuvent pas sonner ensemble.
    auto choked = makeDrums();
    auto free = makeDrums();
    for (auto* s : {&choked, &free}) {
        set(**s, "Room Level", 0.0f);
        set(**s, "Open Hat Decay", 1.2f);
    }
    const auto a = render(choked, {hit(0, 46, 110), hit(4800, 42, 110)}, 32000);
    const auto b = render(free, {hit(0, 46, 110)}, 32000);
    VSM_ASSERT(windowPeak(a, 12000, 3000) < windowPeak(b, 12000, 3000) * 0.4f);
}

VSM_TEST(drums_the_note_tunes_the_tom) {
    // Une seule pièce pour les trois toms : c'est la NOTE qui en fixe
    // l'accord, comme sur les boîtes à rythmes du parc.
    auto low = makeDrums();
    auto high = makeDrums();
    for (auto* s : {&low, &high}) {
        set(**s, "Room Level", 0.0f);
        set(**s, "Tom Tune", 100.0f);
        set(**s, "Tom Decay", 1.0f);
    }
    const auto a = render(low, {hit(0, 41, 110)}, 32768);
    const auto b = render(high, {hit(0, 48, 110)}, 32768);
    VSM_ASSERT(magnitudeAt(a, 1024, 16384, 100.0) > magnitudeAt(a, 1024, 16384, 174.0) * 1.5);
    VSM_ASSERT(magnitudeAt(b, 1024, 16384, 174.0) > magnitudeAt(b, 1024, 16384, 100.0) * 1.5);
}

VSM_TEST(drums_room_adds_width_and_zero_is_exactly_dry) {
    // Une batterie est DANS une pièce, et c'est ce qui la sépare le plus vite
    // d'un kit électronique. Mais à zéro, la machine doit être EXACTEMENT
    // sèche et mono -- pas « presque ».
    // Décroissance de peau la plus courte : c'est la seule façon d'ISOLER la
    // pièce. À la décroissance par défaut, la caisse claire sonne encore à
    // 250 ms (mesuré : -11 dB) et l'on comparerait la pièce à la peau.
    auto dry = makeDrums();
    set(*dry, "Room Level", 0.0f);
    set(*dry, "Snare Decay", 0.05f);
    set(*dry, "Snare Wires", 0.0f);
    constexpr int kFrames = 24064; // multiple entier du bloc de 256
    std::vector<float> left(kFrames, 0.0f), right(kFrames, 0.0f);
    const auto event = hit(0, 38, 110);
    for (int start = 0; start < kFrames; start += 256)
        dry->process(start == 0 ? &event : nullptr, start == 0 ? 1 : 0,
                     left.data() + start, right.data() + start, 256);
    for (size_t i = 0; i < left.size(); ++i) VSM_ASSERT_NEAR(left[i], right[i], 1e-9);

    auto wet = makeDrums();
    set(*wet, "Room Level", 1.0f);
    set(*wet, "Room Size", 0.9f);
    set(*wet, "Snare Decay", 0.05f);
    set(*wet, "Snare Wires", 0.0f);
    std::vector<float> wl(kFrames, 0.0f), wr(kFrames, 0.0f);
    for (int start = 0; start < kFrames; start += 256)
        wet->process(start == 0 ? &event : nullptr, start == 0 ? 1 : 0,
                     wl.data() + start, wr.data() + start, 256);
    size_t differing = 0;
    for (size_t i = 0; i < wl.size(); ++i) if (std::abs(wl[i] - wr[i]) > 1e-5f) ++differing;
    VSM_ASSERT(differing > wl.size() / 20);
    // Et la pièce prolonge le son : il reste quelque chose là où le sec s'est tu.
    VSM_ASSERT(windowPeak(wl, 12000, 4000) > windowPeak(left, 12000, 4000) * 1.5f);
}

VSM_TEST(drums_a_real_pattern_does_not_clip) {
    // Le niveau se calibre sur un MOTIF, pas sur un cas d'école. Une première
    // version exigeait que les neuf pièces frappées ensemble restent sous 1,0 ;
    // le critère paraissait prudent et coûtait un facteur trois de niveau
    // utile, au point que le calage automatique des volumes butait sur sa
    // borne et laissait la batterie deux fois trop faible dans le mélange.
    // Les deux boîtes à rythmes du parc dépassent elles aussi 1,0 sur ce cas
    // artificiel (1,76 pour la 909, 1,45 pour la 808) : il n'arrive dans aucun
    // motif réel.
    auto synth = makeDrums();
    set(*synth, "Room Level", 1.0f);
    std::vector<MidiNoteEvent> pattern;
    for (int i = 0; i < 16; ++i) {
        const int t = i * 6000;
        pattern.push_back(hit(t, 42, 90));
        if (i % 4 == 0) pattern.push_back(hit(t, 36, 127));
        if (i % 4 == 2) pattern.push_back(hit(t, 38, 120));
    }
    const auto audio = render(synth, pattern, 96000);
    VSM_ASSERT(peakAbs(audio) < 1.0f);
    // ...et pas discrète non plus : une batterie trop faible est un défaut
    // aussi réel qu'une batterie qui écrête, et c'est celui qu'on a eu.
    VSM_ASSERT(peakAbs(audio) > 0.5f);
}

VSM_TEST(drums_stays_finite_under_extreme_settings) {
    auto synth = makeDrums();
    for (const auto& info : synth->parameterList()) synth->setParameter(info.id, info.maxValue);
    std::vector<MidiNoteEvent> pattern;
    for (int i = 0; i < 32; ++i)
        pattern.push_back(hit(i * 300, static_cast<uint8_t>(36 + (i % 16)), 127));
    const auto audio = render(synth, pattern, 24000);
    for (float v : audio) VSM_ASSERT(std::isfinite(v));
    VSM_ASSERT(peakAbs(audio) < 12.0f);
}

VSM_TEST(drums_is_deterministic) {
    auto once = [] {
        auto synth = makeDrums();
        return render(synth, {hit(0, 36, 110), hit(6000, 38, 90), hit(12000, 42, 70)}, 24000);
    };
    const auto a = once(), b = once();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(drums_save_load_roundtrip) {
    auto source = makeDrums();
    set(*source, "Kick Tune", 47.5f);
    set(*source, "Snare Wires", 0.83f);
    const auto state = source->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.drums"));
    auto target = makeDrums();
    target->loadState(state);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Kick Tune")), 47.5f, 1e-6);
    VSM_ASSERT_NEAR(target->getParameter(byName(*target, "Snare Wires")), 0.83f, 1e-6);
}

VSM_TEST(drums_parameter_list_size) {
    auto synth = makeDrums();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t{22});
}
