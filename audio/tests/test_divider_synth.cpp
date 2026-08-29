#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <numeric>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeDiv(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.divider");
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

std::vector<float> jouer(SynthPluginPtr& p, const std::vector<uint8_t>& notes, int numSamples) {
    std::vector<float> l(static_cast<size_t>(numSamples), 0.0f);
    std::vector<float> r(static_cast<size_t>(numSamples), 0.0f);
    std::vector<MidiNoteEvent> ev;
    for (uint8_t n : notes) ev.push_back(noteOn(0, n));
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), numSamples);
    return l;
}

/// Enveloppe lente : le niveau efficace par tranches de 50 ms. C'est là qu'un
/// BATTEMENT entre deux oscillateurs se voit -- il module lentement le niveau.
std::vector<double> enveloppeLente(const std::vector<float>& x, size_t depuis) {
    const size_t fen = static_cast<size_t>(0.05 * kSampleRate);
    std::vector<double> e;
    for (size_t i = depuis; i + fen < x.size(); i += fen) {
        double acc = 0.0;
        for (size_t j = i; j < i + fen; ++j) acc += static_cast<double>(x[j]) * x[j];
        e.push_back(std::sqrt(acc / static_cast<double>(fen)));
    }
    return e;
}

/// Combien l'enveloppe ONDULE, rapporté à son niveau : c'est la mesure d'un
/// battement, indépendante du volume.
double ondulation(const std::vector<double>& e) {
    if (e.size() < 4) return 0.0;
    const double moy = std::accumulate(e.begin(), e.end(), 0.0) / static_cast<double>(e.size());
    if (moy < 1e-9) return 0.0;
    double var = 0.0;
    for (double v : e) var += (v - moy) * (v - moy);
    return std::sqrt(var / static_cast<double>(e.size())) / moy;
}

/// Réglages qui isolent l'architecture : pas d'ensemble (son chorus module le
/// niveau, ce qui masquerait exactement ce qu'on veut mesurer), attaque courte,
/// un seul registre.
void nu(ISynthPlugin& p) {
    setByName(p, "Ensemble", 0.0f);
    setByName(p, "Attack", 0.005f);
    setByName(p, "16' Level", 0.0f);
    setByName(p, "8' Level", 1.0f);
    setByName(p, "Tone", 12000.0f);
}

} // namespace

VSM_TEST(divider_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.divider"));
}

VSM_TEST(divider_silent_with_no_events) {
    auto p = makeDiv();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    p->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);
}

VSM_TEST(divider_note_produces_sound) {
    auto p = makeDiv();
    nu(*p);
    const auto out = jouer(p, {57}, 48000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 1);
}

VSM_TEST(divider_octaves_cannot_beat_but_fifths_can) {
    // LE TRAIT DISTINCTIF, ET IL DÉCOULE DE L'ARCHITECTURE.
    //
    // Deux notes à l'octave viennent du MÊME oscillateur maître, l'une étant
    // l'autre divisée par deux : leur rapport est exactement 2, pour toujours,
    // quelle que soit la dérive -- puisque la dérive est commune. Elles ne
    // peuvent donc pas battre. Une quinte, elle, met en jeu DEUX maîtres
    // différents, qui dérivent chacun de leur côté : elle bat.
    //
    // Les deux moitiés comptent. Sans la seconde, le test passerait aussi sur
    // une machine qui n'aurait tout simplement pas de dérive.
    auto octave = makeDiv();
    nu(*octave);
    setByName(*octave, "Analog Character", 1.0f);   // dérive au maximum
    const auto a = jouer(octave, {45, 57}, 10 * static_cast<int>(kSampleRate));

    auto quinte = makeDiv();
    nu(*quinte);
    setByName(*quinte, "Analog Character", 1.0f);
    const auto b = jouer(quinte, {45, 52}, 10 * static_cast<int>(kSampleRate));

    const size_t apresAttaque = static_cast<size_t>(0.5 * kSampleRate);
    const double battementOctave = ondulation(enveloppeLente(a, apresAttaque));
    const double battementQuinte = ondulation(enveloppeLente(b, apresAttaque));

    VSM_ASSERT(battementOctave < 0.02);                       // l'octave est figée
    VSM_ASSERT(battementQuinte > battementOctave * 3.0);      // la quinte bat
}

VSM_TEST(divider_masters_run_whether_or_not_a_key_is_held) {
    // COROLLAIRE DE L'ARCHITECTURE, et il se vérifie : les maîtres ne sont pas
    // déclenchés par les touches. Rejouer la MÊME note après un silence ne
    // repart donc pas de la même phase -- alors que sur toute autre machine du
    // parc, un oscillateur redémarre à zéro à chaque note.
    auto p = makeDiv();
    nu(*p);
    setByName(*p, "Analog Character", 0.0f);
    const int bloc = static_cast<int>(kSampleRate);
    std::vector<float> l(static_cast<size_t>(bloc), 0.0f), r(static_cast<size_t>(bloc), 0.0f);

    const MidiNoteEvent on = noteOn(0, 57);
    p->process(&on, 1, l.data(), r.data(), bloc);
    const std::vector<float> premiere(l.begin() + 24000, l.begin() + 24512);

    // On relâche, on laisse le temps passer, on rejoue la même note.
    const MidiNoteEvent off{MidiNoteEvent::Kind::NoteOff, 0, 0, 57, 0};
    p->process(&off, 1, l.data(), r.data(), bloc);
    p->process(nullptr, 0, l.data(), r.data(), bloc);
    p->process(&on, 1, l.data(), r.data(), bloc);
    const std::vector<float> seconde(l.begin() + 24000, l.begin() + 24512);

    // Les deux extraits ne peuvent pas être identiques : la phase du maître a
    // continué de tourner pendant le silence.
    double ecart = 0.0;
    for (size_t i = 0; i < premiere.size(); ++i)
        ecart += std::abs(static_cast<double>(premiere[i]) - seconde[i]);
    VSM_ASSERT(ecart > 1e-3);
}

VSM_TEST(divider_ensemble_moves_the_sound) {
    // Une corde électronique sans son chorus n'est qu'un orgue pauvre : le
    // réglage doit faire quelque chose de mesurable, sur toute sa course. Sans
    // ensemble, le niveau d'une note tenue est PLAT ; avec, il ondule.
    auto sec = makeDiv();
    nu(*sec);
    setByName(*sec, "Analog Character", 0.0f);
    const auto a = jouer(sec, {57}, 5 * static_cast<int>(kSampleRate));

    auto mouille = makeDiv();
    nu(*mouille);
    setByName(*mouille, "Analog Character", 0.0f);
    setByName(*mouille, "Ensemble", 1.0f);
    const auto b = jouer(mouille, {57}, 5 * static_cast<int>(kSampleRate));

    const size_t apres = static_cast<size_t>(0.5 * kSampleRate);
    VSM_ASSERT(ondulation(enveloppeLente(b, apres)) > ondulation(enveloppeLente(a, apres)) * 3.0);
}

VSM_TEST(divider_registers_change_the_octave) {
    // Deux registres, deux longueurs : le 16 pieds sonne une octave PLUS BAS
    // que le 8. Un réglage qui ne ferait rien serait pire qu'absent.
    auto huit = makeDiv();
    nu(*huit);
    const auto a = jouer(huit, {57}, 48000);

    auto seize = makeDiv();
    nu(*seize);
    setByName(*seize, "8' Level", 0.0f);
    setByName(*seize, "16' Level", 1.0f);
    const auto b = jouer(seize, {57}, 48000);

    // Nombre de passages par zéro : il doit être à peu près DEUX FOIS moindre
    // sur le registre grave.
    auto passages = [](const std::vector<float>& x) {
        int n = 0;
        for (size_t i = 24000; i + 1 < x.size(); ++i)
            if ((x[i] < 0.0f) != (x[i + 1] < 0.0f)) ++n;
        return n;
    };
    const int p8 = passages(a), p16 = passages(b);
    VSM_ASSERT(p8 > 0 && p16 > 0);
    VSM_ASSERT(static_cast<double>(p8) > static_cast<double>(p16) * 1.6);
}

VSM_TEST(divider_is_fully_polyphonic) {
    // Ces machines n'ont pas de limite de voix : toutes les touches à la fois.
    // Aucune autre machine du parc ne fait ça -- les autres plafonnent à huit.
    auto p = makeDiv();
    nu(*p);
    std::vector<uint8_t> accord;
    for (int k = 0; k < 20; ++k) accord.push_back(static_cast<uint8_t>(36 + 2 * k));
    const auto out = jouer(p, accord, 48000);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 20);
    VSM_ASSERT(peakAbs(out) > 0.05f);
    VSM_ASSERT(peakAbs(out) < 1.0f);
}

VSM_TEST(divider_stays_finite_under_extreme_settings) {
    auto p = makeDiv();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto a = jouer(p, {36, 48, 60, 72}, 48000);
    for (float s : a) VSM_ASSERT(std::isfinite(s));
    auto q = makeDiv();
    for (const auto& info : q->parameterList()) q->setParameter(info.id, info.minValue);
    const auto b = jouer(q, {36, 48, 60, 72}, 48000);
    for (float s : b) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(divider_is_deterministic) {
    auto a = makeDiv();
    auto b = makeDiv();
    const auto x = jouer(a, {57, 64}, 48000);
    const auto y = jouer(b, {57, 64}, 48000);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT_EQ(x[i], y[i]);
}

VSM_TEST(divider_save_load_roundtrip) {
    auto p = makeDiv();
    setByName(*p, "Ensemble", 0.42f);
    setByName(*p, "Tone", 3100.0f);
    const auto state = p->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.divider"));
    auto autre = makeDiv();
    autre->loadState(state);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Ensemble")), 0.42f, 1e-6);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Tone")), 3100.0f, 1e-6);
}

VSM_TEST(divider_parameter_list_is_complete) {
    auto p = makeDiv();
    VSM_ASSERT_EQ(static_cast<int>(p->parameterList().size()), 8);
}
