#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeSto(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.stochastic");
    p->initialize(sr, 512);
    return p;
}

float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p;
}

ParamId paramByName(const ISynthPlugin& p, const std::string& n) {
    for (const auto& info : p.parameterList()) if (info.name == n) return info.id;
    return 0;
}

void setByName(ISynthPlugin& p, const std::string& n, float v) {
    p.setParameter(paramByName(p, n), v);
}

MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t vel = 110) {
    return MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, offset, 0, note, vel};
}

std::vector<float> play(SynthPluginPtr& p, uint8_t note, int numSamples, uint8_t vel = 110) {
    std::vector<float> l(static_cast<size_t>(numSamples), 0.0f);
    std::vector<float> r(static_cast<size_t>(numSamples), 0.0f);
    const MidiNoteEvent ev = noteOn(0, note, vel);
    p->process(&ev, 1, l.data(), r.data(), numSamples);
    return l;
}

double midiToHz(int note) {
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

/// Écart moyen entre une période et la SUIVANTE, rapporté au niveau du signal.
/// Zéro veut dire « exactement périodique » ; c'est la mesure qui sépare cette
/// machine de toutes les autres du parc.
///
/// LA PÉRIODE EST FRACTIONNAIRE, ET IL FAUT INTERPOLER. `sampleRate / f0` ne
/// tombe presque jamais sur un nombre entier d'échantillons : comparer `x[i]` à
/// `x[i + T]` avec un `T` arrondi décale les deux périodes d'une fraction
/// d'échantillon, et cet écart-là -- qui n'est qu'un artefact de mesure --
/// dépassait le seuil à lui seul. On lit donc le second point par interpolation
/// linéaire à la position exacte.
double ecartEntrePeriodes(const std::vector<float>& x, size_t depuis, double f0) {
    const double T = kSampleRate / f0;
    const size_t n = static_cast<size_t>(T);
    if (n < 8 || depuis + 3 * n >= x.size()) return -1.0;
    double diff = 0.0, niveau = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double pos = static_cast<double>(depuis + i) + T;
        const size_t j = static_cast<size_t>(pos);
        if (j + 1 >= x.size()) break;
        const double f = pos - static_cast<double>(j);
        const double a = x[depuis + i];
        const double b = x[j] * (1.0 - f) + x[j + 1] * f;
        diff += std::abs(a - b);
        niveau += std::abs(a);
    }
    return niveau > 1e-9 ? diff / niveau : -1.0;
}

/// Réglages qui tiennent la note : c'est la condition pour comparer deux
/// périodes successives.
void tenue(ISynthPlugin& p) {
    setByName(p, "Attack", 0.002f);
    setByName(p, "Decay", 0.01f);
    setByName(p, "Sustain", 1.0f);
    setByName(p, "Tone", 16000.0f);
    setByName(p, "Velocity to Wander", 0.0f);
}

} // namespace

VSM_TEST(stochastic_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.stochastic"));
}

VSM_TEST(stochastic_silent_with_no_events) {
    auto p = makeSto();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    p->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);
}

VSM_TEST(stochastic_note_produces_sound) {
    auto p = makeSto();
    tenue(*p);
    const auto out = play(p, 57, 24000);
    VSM_ASSERT(peakAbs(out) > 0.02f);
}

VSM_TEST(stochastic_no_two_periods_are_alike) {
    // LE TRAIT DISTINCTIF, ET IL EST BINAIRE.
    //
    // Sur n'importe quelle autre machine du parc, deux périodes successives
    // d'une note tenue sont IDENTIQUES : un oscillateur relit la même forme
    // d'onde à chaque tour. Ici, chaque point de brisure se déplace d'un tour au
    // suivant, et l'onde n'est jamais deux fois la même.
    //
    // Le test mesure les DEUX moitiés. Sans la première -- divagation nulle,
    // périodes identiques -- on ne saurait pas que l'écart vient du réglage et
    // non du bruit d'un calcul.
    const int note = 45;
    const double f0 = midiToHz(note);

    auto fige = makeSto();
    tenue(*fige);
    setByName(*fige, "Shape Wander", 0.0f);
    setByName(*fige, "Time Wander", 0.0f);
    const auto a = play(fige, static_cast<uint8_t>(note), 48000);
    const double ecartFige = ecartEntrePeriodes(a, 24000, f0);
    VSM_ASSERT(ecartFige >= 0.0);
    // Le résidu qui reste est celui de l'interpolation, pas de la machine.
    VSM_ASSERT(ecartFige < 0.01);          // exactement périodique

    auto vivant = makeSto();
    tenue(*vivant);
    setByName(*vivant, "Shape Wander", 0.4f);
    setByName(*vivant, "Time Wander", 0.0f);
    const auto b = play(vivant, static_cast<uint8_t>(note), 48000);
    const double ecartVivant = ecartEntrePeriodes(b, 24000, f0);
    VSM_ASSERT(ecartVivant > ecartFige * 10.0);   // et là, elle divague vraiment
    VSM_ASSERT(ecartVivant > 0.10);
}

VSM_TEST(stochastic_pitch_lock_holds_the_note) {
    // LE RÉGLAGE QUI REND LA MACHINE JOUABLE. Une marche aléatoire sur les
    // durées fait dériver la période, donc la note. Le verrou renormalise la
    // somme des durées à chaque tour : la forme continue de divaguer, la
    // hauteur non.
    //
    // On mesure la période moyenne par comptage des passages par zéro montants,
    // verrou serré puis verrou lâche.
    const int note = 45;
    const double f0 = midiToHz(note);
    auto mesurer = [&](float verrou) {
        auto p = makeSto();
        tenue(*p);
        setByName(*p, "Time Wander", 0.4f);
        setByName(*p, "Shape Wander", 0.0f);
        setByName(*p, "Pitch Lock", verrou);
        const auto out = play(p, static_cast<uint8_t>(note), 96000);
        int montees = 0; size_t premier = 0, dernier = 0;
        for (size_t i = 24001; i < out.size(); ++i) {
            if (out[i - 1] <= 0.0f && out[i] > 0.0f) {
                if (montees == 0) premier = i;
                dernier = i; ++montees;
            }
        }
        if (montees < 4) return 0.0;
        return static_cast<double>(montees - 1) * kSampleRate / static_cast<double>(dernier - premier);
    };
    const double serre = mesurer(1.0f);
    const double lache = mesurer(0.0f);
    VSM_ASSERT(serre > 0.0 && lache > 0.0);
    // Verrou serré : la note est là, à un demi-ton près.
    VSM_ASSERT(std::abs(1200.0 * std::log2(serre / f0)) < 50.0);
    // Verrou lâche : elle s'en va franchement plus loin.
    VSM_ASSERT(std::abs(1200.0 * std::log2(lache / f0))
               > std::abs(1200.0 * std::log2(serre / f0)) + 30.0);
}

VSM_TEST(stochastic_breakpoint_count_changes_the_timbre) {
    // Un réglage qui ne ferait rien serait pire qu'absent : plus il y a de
    // points, plus l'onde a d'angles par période, donc d'énergie en haut du
    // spectre. On le mesure au taux de passages par zéro.
    auto compter = [](const std::vector<float>& x) {
        int n = 0;
        for (size_t i = 24001; i < x.size(); ++i)
            if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++n;
        return n;
    };
    auto peu = makeSto();
    tenue(*peu);
    setByName(*peu, "Breakpoints", 2.0f);
    auto beaucoup = makeSto();
    tenue(*beaucoup);
    setByName(*beaucoup, "Breakpoints", 16.0f);
    VSM_ASSERT(compter(play(beaucoup, 45, 48000)) > compter(play(peu, 45, 48000)) * 1.5);
}

VSM_TEST(stochastic_is_polyphonic) {
    auto p = makeSto();
    tenue(*p);
    std::vector<MidiNoteEvent> ev{noteOn(0, 48), noteOn(0, 52), noteOn(0, 55)};
    std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 24000);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 3);
}

VSM_TEST(stochastic_a_full_chord_does_not_clip) {
    auto p = makeSto();
    tenue(*p);
    std::vector<MidiNoteEvent> ev;
    for (int k = 0; k < 8; ++k) ev.push_back(noteOn(0, static_cast<uint8_t>(45 + 2 * k), 110));
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 48000);
    VSM_ASSERT(peakAbs(l) > 0.05f);
    VSM_ASSERT(peakAbs(l) < 1.0f);
}

VSM_TEST(stochastic_stays_finite_under_extreme_settings) {
    auto p = makeSto();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto a = play(p, 36, 48000, 127);
    for (float s : a) VSM_ASSERT(std::isfinite(s));
    auto q = makeSto();
    for (const auto& info : q->parameterList()) q->setParameter(info.id, info.minValue);
    const auto b = play(q, 96, 48000, 127);
    for (float s : b) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(stochastic_is_deterministic) {
    // UNE MACHINE « ALÉATOIRE » DONT LE HASARD NE SE REJOUE PAS SERAIT
    // INUTILISABLE ICI : ni empreinte de non-régression, ni recherche de patch.
    // Tout passe par DeterministicRng, seedé.
    auto a = makeSto();
    auto b = makeSto();
    const auto x = play(a, 45, 48000);
    const auto y = play(b, 45, 48000);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT_EQ(x[i], y[i]);
}

VSM_TEST(stochastic_save_load_roundtrip) {
    auto p = makeSto();
    setByName(*p, "Shape Wander", 0.27f);
    setByName(*p, "Pitch Lock", 0.42f);
    const auto state = p->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.stochastic"));
    auto autre = makeSto();
    autre->loadState(state);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Shape Wander")), 0.27f, 1e-6);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Pitch Lock")), 0.42f, 1e-6);
}

VSM_TEST(stochastic_parameter_list_is_complete) {
    auto p = makeSto();
    VSM_ASSERT_EQ(static_cast<int>(p->parameterList().size()), 11);
}
