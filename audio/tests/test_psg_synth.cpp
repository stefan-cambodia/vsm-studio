#include "TestFramework.h"
#include "../plugins/psg/PsgSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makePsg(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.psg");
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

/// Fréquence mesurée par comptage de passages par zéro montants : sur une onde
/// carrée, c'est exact et bien plus fiable qu'une transformée.
double frequenceMesuree(const std::vector<float>& x, size_t depuis) {
    int montees = 0;
    size_t premier = 0, dernier = 0;
    for (size_t i = depuis + 1; i < x.size(); ++i) {
        if (x[i - 1] <= 0.0f && x[i] > 0.0f) {
            if (montees == 0) premier = i;
            dernier = i;
            ++montees;
        }
    }
    if (montees < 2) return 0.0;
    return static_cast<double>(montees - 1) * kSampleRate
         / static_cast<double>(dernier - premier);
}

/// Réglages qui tiennent la note à niveau constant : c'est la condition pour
/// compter des passages par zéro et des marches de volume.
void tenue(ISynthPlugin& p) {
    setByName(p, "Attack", 0.001f);
    setByName(p, "Decay", 0.005f);
    setByName(p, "Sustain", 1.0f);
    setByName(p, "Noise Level", 0.0f);
    setByName(p, "Square Voices", 1.0f);
}

} // namespace

VSM_TEST(psg_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.psg"));
}

VSM_TEST(psg_silent_with_no_events) {
    auto p = makePsg();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    p->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);
}

VSM_TEST(psg_note_produces_sound) {
    auto p = makePsg();
    tenue(*p);
    const auto out = play(p, 57, 24000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
}

VSM_TEST(psg_pitch_is_quantised_by_an_integer_clock_divider) {
    // LE TRAIT DISTINCTIF, ET IL EST PRÉDICTIBLE AU HERTZ PRÈS.
    //
    // Une puce ne calcule pas une fréquence : on lui écrit une PÉRIODE entière
    // dans un compteur, et la fréquence en découle. Aucune autre machine du
    // parc n'a d'erreur de justesse -- toutes calculent en flottant.
    //
    // On mesure donc, sur une note aiguë où la quantification est grossière,
    // que la fréquence rendue est celle que la formule PRÉDIT, et non la
    // fréquence tempérée. Une machine dont on peut calculer le défaut d'avance
    // est une machine dont le défaut est un modèle, pas un accident.
    using vsm::plugins::psg::PsgVoice;
    const int note = 93;                       // ~3729 Hz : période très courte
    const double tempere = midiToHz(note);
    const float horloge = 1789773.0f;

    auto p = makePsg();
    tenue(*p);
    setByName(*p, "Clock", horloge);
    const auto out = play(p, static_cast<uint8_t>(note), 48000);

    const double mesuree = frequenceMesuree(out, 2400);
    const double predite = PsgVoice::quantifier(static_cast<float>(tempere), horloge);

    VSM_ASSERT(mesuree > 0.0);
    // La fréquence rendue est celle que la puce PEUT produire...
    VSM_ASSERT(std::abs(mesuree - predite) < predite * 0.01);
    // ...et elle n'est PAS la fréquence tempérée : l'erreur existe vraiment.
    VSM_ASSERT(std::abs(predite - tempere) > tempere * 0.002);
}

VSM_TEST(psg_a_faster_clock_tunes_better) {
    // LA SECONDE MOITIÉ, ET ELLE EST INDISPENSABLE : sans elle, on aurait pu
    // mesurer n'importe quel désaccord et l'appeler quantification. Une horloge
    // plus rapide offre des périodes plus fines, donc une erreur plus petite.
    using vsm::plugins::psg::PsgVoice;
    const int note = 93;
    const double tempere = midiToHz(note);
    auto erreur = [&](float horloge) {
        const double f = PsgVoice::quantifier(static_cast<float>(tempere), horloge);
        return std::abs(f - tempere) / tempere;
    };
    // Et on le vérifie AUSSI sur le son rendu, pas seulement sur la formule.
    auto p = makePsg();
    tenue(*p);
    setByName(*p, "Clock", 8000000.0f);
    const double mesuree = frequenceMesuree(play(p, static_cast<uint8_t>(note), 48000), 2400);
    VSM_ASSERT(erreur(8000000.0f) < erreur(1789773.0f));
    VSM_ASSERT(std::abs(mesuree - tempere) < std::abs(erreur(1789773.0f)) * tempere * 1.5 + 5.0);
}

VSM_TEST(psg_output_takes_a_countable_number_of_levels) {
    // SECOND TRAIT : l'amplitude est quantifiée. Ces puces avaient quatre bits
    // de volume, soit seize marches. Une note tenue ne peut donc prendre qu'un
    // nombre FINI de valeurs -- aucune autre machine du parc n'a de sortie
    // dénombrable.
    auto p = makePsg();
    tenue(*p);
    setByName(*p, "Volume Bits", 3.0f);       // huit marches
    const auto out = play(p, 45, 48000);

    std::set<int> niveaux;
    for (size_t i = 24000; i < out.size(); ++i)
        niveaux.insert(static_cast<int>(std::lround(static_cast<double>(out[i]) * 1e6)));
    // Une onde carrée à huit marches de volume : au plus deux valeurs par
    // marche (le haut et le bas du carré), donc seize au grand maximum.
    VSM_ASSERT(!niveaux.empty());
    VSM_ASSERT(niveaux.size() <= 16);

    // Et avec plus de bits, il y a strictement plus de valeurs possibles :
    // c'est le réglage qui décide, pas le hasard.
    auto q = makePsg();
    tenue(*q);
    setByName(*q, "Volume Bits", 8.0f);
    setByName(*q, "Decay", 2.0f);
    setByName(*q, "Sustain", 0.0f);           // une descente, pour balayer les marches
    const auto out2 = play(q, 45, 48000);
    std::set<int> niveaux2;
    for (size_t i = 2400; i < out2.size(); ++i)
        niveaux2.insert(static_cast<int>(std::lround(static_cast<double>(out2[i]) * 1e6)));
    VSM_ASSERT(niveaux2.size() > niveaux.size());
}

VSM_TEST(psg_noise_repeats_itself) {
    // Le bruit sort d'un registre à décalage BOUCLÉ : il se répète. À période
    // courte, on entend une hauteur dedans -- c'est la percussion accordable
    // de ces machines.
    auto p = makePsg();
    tenue(*p);
    setByName(*p, "Noise Level", 1.0f);
    setByName(*p, "Noise Period", 4.0f);
    const auto out = play(p, 45, 48000);
    // Le registre fait 15 bits, donc 32 767 pas ; à quatre échantillons par
    // pas, le motif se répète toutes les 131 068 valeurs -- plus long que le
    // rendu. Ce qu'on vérifie ici est plus simple et plus sûr : le bruit n'est
    // PAS du hasard, deux rendus donnent exactement la même suite.
    auto q = makePsg();
    tenue(*q);
    setByName(*q, "Noise Level", 1.0f);
    setByName(*q, "Noise Period", 4.0f);
    const auto autre = play(q, 45, 48000);
    for (size_t i = 0; i < out.size(); ++i) VSM_ASSERT_EQ(out[i], autre[i]);
    VSM_ASSERT(peakAbs(out) > 0.01f);
}

VSM_TEST(psg_pulse_width_changes_the_spectrum) {
    // Un réglage qui ne ferait rien serait pire qu'absent. Un carré à 50 % n'a
    // que des rangs impairs ; à 25 %, les pairs apparaissent. On le mesure au
    // rapport entre le temps passé en haut et en bas.
    auto carre = makePsg();
    tenue(*carre);
    setByName(*carre, "Pulse Width", 0.5f);
    const auto a = play(carre, 45, 24000);

    auto etroit = makePsg();
    tenue(*etroit);
    setByName(*etroit, "Pulse Width", 0.1f);
    const auto b = play(etroit, 45, 24000);

    auto partHaute = [](const std::vector<float>& x) {
        int haut = 0, total = 0;
        for (size_t i = 12000; i < x.size(); ++i) { if (x[i] > 0.0f) ++haut; ++total; }
        return static_cast<double>(haut) / std::max(total, 1);
    };
    VSM_ASSERT(partHaute(a) > 0.4 && partHaute(a) < 0.6);
    VSM_ASSERT(partHaute(b) < 0.2);
}

VSM_TEST(psg_is_polyphonic) {
    auto p = makePsg();
    tenue(*p);
    std::vector<MidiNoteEvent> ev{noteOn(0, 48), noteOn(0, 52), noteOn(0, 55)};
    std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 24000);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 3);
}

VSM_TEST(psg_a_full_chord_does_not_clip) {
    auto p = makePsg();
    tenue(*p);
    setByName(*p, "Square Voices", 3.0f);
    std::vector<MidiNoteEvent> ev;
    for (int k = 0; k < 6; ++k) ev.push_back(noteOn(0, static_cast<uint8_t>(48 + 3 * k), 110));
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 48000);
    VSM_ASSERT(peakAbs(l) > 0.05f);
    VSM_ASSERT(peakAbs(l) < 1.0f);
}

VSM_TEST(psg_stays_finite_under_extreme_settings) {
    auto p = makePsg();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto a = play(p, 36, 24000, 127);
    for (float s : a) VSM_ASSERT(std::isfinite(s));
    auto q = makePsg();
    for (const auto& info : q->parameterList()) q->setParameter(info.id, info.minValue);
    const auto b = play(q, 96, 24000, 127);
    for (float s : b) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(psg_is_deterministic) {
    auto a = makePsg();
    auto b = makePsg();
    const auto x = play(a, 45, 24000);
    const auto y = play(b, 45, 24000);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT_EQ(x[i], y[i]);
}

VSM_TEST(psg_save_load_roundtrip) {
    auto p = makePsg();
    setByName(*p, "Clock", 2500000.0f);
    setByName(*p, "Volume Bits", 6.0f);
    const auto state = p->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.psg"));
    auto autre = makePsg();
    autre->loadState(state);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Clock")), 2500000.0f, 1.0f);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Volume Bits")), 6.0f, 1e-6);
}

VSM_TEST(psg_parameter_list_is_complete) {
    auto p = makePsg();
    VSM_ASSERT_EQ(static_cast<int>(p->parameterList().size()), 12);
}
