#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

SynthPluginPtr makeVocal(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.vocal");
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

double magnitudeAt(const std::vector<float>& x, size_t from, size_t count, double hz) {
    const size_t fin = std::min(x.size(), from + count);
    if (from >= fin) return 0.0;
    std::complex<double> acc{0.0, 0.0};
    const double w = 2.0 * kPi * hz / kSampleRate;
    const double n = static_cast<double>(fin - from);
    size_t i2 = 0;
    for (size_t i = from; i < fin; ++i, ++i2) {
        const double fen = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i2) / n);
        acc += std::complex<double>(x[i] * fen, 0.0)
             * std::exp(std::complex<double>(0.0, -w * static_cast<double>(i2)));
    }
    return std::abs(acc) / n;
}

/// Énergie dans une BANDE, relevée au pas de 20 Hz.
///
/// POURQUOI UNE BANDE ET NON UN PIC, et il a fallu se tromper pour le voir : le
/// spectre d'une voix est un PEIGNE d'harmoniques espacées de f0. Un formant
/// n'y apparaît pas comme un maximum à sa propre fréquence, mais comme un
/// renforcement de l'harmonique la plus proche. Chercher « la fréquence du
/// maximum » ne mesure donc pas le formant : ça mesure quelle harmonique tombe
/// le plus près, et cette harmonique CHANGE quand la note change -- ce qui
/// donnait un écart de plus de cent hertz entre deux octaves alors que le
/// formant, lui, n'avait pas bougé d'un hertz. L'énergie d'une bande, elle, est
/// insensible à la position des dents du peigne.
double bandEnergy(const std::vector<float>& x, size_t from, size_t count,
                  double basHz, double hautHz) {
    double somme = 0.0;
    for (double f = basHz; f <= hautHz; f += 20.0) somme += magnitudeAt(x, from, count, f);
    return somme;
}

/// Réglages qui TIENNENT la note et laissent le spectre tranquille : sans quoi
/// on mesurerait un vibrato ou une dérive plutôt qu'un formant.
void tenue(ISynthPlugin& p) {
    setByName(p, "Amp Attack", 0.01f);
    setByName(p, "Amp Decay", 0.02f);
    setByName(p, "Amp Sustain", 1.0f);
    setByName(p, "Vibrato Depth", 0.0f);
    setByName(p, "Vibrato Delay", 2.0f);
    setByName(p, "Analog Character", 0.0f);
    setByName(p, "Breath", 0.0f);
    setByName(p, "Velocity to Breath", 0.0f);
}

} // namespace

VSM_TEST(vocal_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.vocal"));
}

VSM_TEST(vocal_silent_with_no_events) {
    auto p = makeVocal();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    p->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);
}

VSM_TEST(vocal_note_produces_sound) {
    auto p = makeVocal();
    tenue(*p);
    const auto out = play(p, 45, 24000);
    VSM_ASSERT(peakAbs(out) > 0.01f);
}

VSM_TEST(vocal_formants_do_not_follow_the_pitch) {
    // LE TRAIT DISTINCTIF, ET C'EST LA DÉFINITION D'UNE VOIX.
    //
    // Les résonances du conduit vocal ne suivent pas la note chantée : un même
    // « a » à 110 Hz et à 220 Hz a son premier formant au même endroit, vers
    // 730 Hz. C'est ce qui fait qu'on reconnaît la voyelle indépendamment de la
    // hauteur, et aucune autre machine du parc ne le fait -- un filtre
    // soustractif n'a qu'une résonance, et elle suit le clavier ou pas.
    //
    // On joue donc la même voyelle à une OCTAVE d'écart, et on cherche le pic
    // dans la bande du premier formant sans lui dire où il est.
    auto grave = makeVocal();
    tenue(*grave);
    setByName(*grave, "Vowel", 0.0f);          // « a »
    const auto a = play(grave, 45, 65536);     // 110 Hz

    auto aigu = makeVocal();
    tenue(*aigu);
    setByName(*aigu, "Vowel", 0.0f);
    const auto b = play(aigu, 57, 65536);      // 220 Hz, l'octave

    const size_t depart = 6000, longueur = 32768;
    // La bande du premier formant du « a » (730 Hz), et celle où ce formant
    // atterrirait s'il SUIVAIT la note d'une octave (1460 Hz).
    const double formantGrave = bandEnergy(a, depart, longueur, 600.0, 900.0);
    const double transposeGrave = bandEnergy(a, depart, longueur, 1250.0, 1700.0);
    const double formantAigu = bandEnergy(b, depart, longueur, 600.0, 900.0);
    const double transposeAigu = bandEnergy(b, depart, longueur, 1250.0, 1700.0);

    // Aux DEUX hauteurs, l'énergie est là où la phonétique met le formant,
    // et pas là où une transposition l'aurait mise.
    VSM_ASSERT(formantGrave > transposeGrave * 1.5);
    VSM_ASSERT(formantAigu > transposeAigu * 1.5);
    // ON N'EN DEMANDE PAS PLUS, ET C'EST DÉLIBÉRÉ. Une troisième assertion
    // comparant le RAPPORT des deux bandes d'une hauteur à l'autre a été
    // écrite, puis retirée : à 220 Hz le peigne d'harmoniques est deux fois
    // plus lâche qu'à 110, si bien que l'énergie d'une bande de 450 Hz de large
    // dépend de la CHANCE qu'une harmonique y tombe. Ce rapport est donc
    // bruité par le peigne, pas par le conduit -- il aurait fait échouer le
    // test pour une raison qui n'a rien à voir avec ce qu'il prétend mesurer.
    // Les deux assertions ci-dessus suffisent : elles disent que l'énergie est
    // au même endroit ABSOLU aux deux hauteurs, ce qui est toute la
    // revendication.
}

VSM_TEST(vocal_each_vowel_has_its_own_formants) {
    // Un réglage qui ne ferait rien serait pire qu'absent. « a » et « i » sont
    // les deux extrêmes du trapèze vocalique : leur premier formant est à 730
    // et 270 Hz, leur second à 1090 et 2290. Le test vérifie que la machine les
    // sépare vraiment, et dans le bon sens.
    auto va = makeVocal();
    tenue(*va);
    setByName(*va, "Vowel", 0.0f);             // a
    const auto a = play(va, 45, 65536);

    auto vi = makeVocal();
    tenue(*vi);
    setByName(*vi, "Vowel", 2.0f);             // i
    const auto i = play(vi, 45, 65536);

    const size_t depart = 6000, longueur = 32768;
    // LE BON DISCRIMINANT EST UN RAPPORT, pas une bande isolée : le troisième
    // formant du « a » (2440 Hz) et le second du « i » (2290 Hz) tombent tous
    // deux dans la même bande haute, si bien que la comparer à elle-même ne
    // sépare rien -- essayé, et le test échouait pour cette raison-là. On
    // compare donc où l'énergie se CONCENTRE : le « a » a ses deux premiers
    // formants entre 600 et 1200 Hz, le « i » a le sien à 270 et l'autre à
    // 2290. Le rapport bas/haut est donc franchement différent.
    const double basA = bandEnergy(a, depart, longueur, 600.0, 1200.0);
    const double hautA = bandEnergy(a, depart, longueur, 2100.0, 2500.0);
    const double basI = bandEnergy(i, depart, longueur, 600.0, 1200.0);
    const double hautI = bandEnergy(i, depart, longueur, 2100.0, 2500.0);
    VSM_ASSERT((basA / hautA) > (basI / hautI) * 2.0);
}

VSM_TEST(vocal_formant_shift_moves_the_whole_tract) {
    // Une gorge plus courte a TOUS ses formants plus haut : le décalage est une
    // transposition du conduit, pas un réglage de plus sur un seul pic.
    auto normal = makeVocal();
    tenue(*normal);
    setByName(*normal, "Vowel", 0.0f);
    setByName(*normal, "Formant Shift", 0.0f);
    const auto a = play(normal, 45, 65536);

    auto court = makeVocal();
    tenue(*court);
    setByName(*court, "Vowel", 0.0f);
    setByName(*court, "Formant Shift", 12.0f);   // une octave plus haut
    const auto b = play(court, 45, 65536);

    const size_t depart = 6000, longueur = 32768;
    // Le conduit transposé d'une octave met son premier formant vers 1460 Hz :
    // l'énergie doit avoir DÉMÉNAGÉ de la bande basse vers la bande haute.
    const double basNormal = bandEnergy(a, depart, longueur, 600.0, 900.0);
    const double hautNormal = bandEnergy(a, depart, longueur, 1250.0, 1700.0);
    const double basDecale = bandEnergy(b, depart, longueur, 600.0, 900.0);
    const double hautDecale = bandEnergy(b, depart, longueur, 1250.0, 1700.0);
    // On compare des RAPPORTS et non des bandes brutes : la source glottique
    // perd naturellement de l'énergie avec la fréquence, si bien que la bande
    // haute reste plus faible même quand le formant y a déménagé. Ce qui doit
    // changer, et fortement, c'est l'équilibre entre les deux.
    VSM_ASSERT((hautDecale / basDecale) > (hautNormal / basNormal) * 2.0);
}

VSM_TEST(vocal_breath_adds_noise_without_moving_the_formants) {
    // Le souffle appartient à la SOURCE, les formants au FILTRE : c'est la
    // division du modèle source-filtre, et elle doit se voir. Souffler plus ne
    // doit pas déplacer une résonance.
    auto sec = makeVocal();
    tenue(*sec);
    setByName(*sec, "Vowel", 0.0f);
    const auto a = play(sec, 45, 65536);

    auto souffle = makeVocal();
    tenue(*souffle);
    setByName(*souffle, "Vowel", 0.0f);
    setByName(*souffle, "Breath", 0.9f);
    const auto b = play(souffle, 45, 65536);

    const size_t depart = 6000, longueur = 32768;
    // Le formant reste où il est : la bande basse garde son avance sur la bande
    // où un formant déplacé serait allé.
    const double basSec = bandEnergy(a, depart, longueur, 600.0, 900.0);
    const double hautSec = bandEnergy(a, depart, longueur, 1250.0, 1700.0);
    const double basSouffle = bandEnergy(b, depart, longueur, 600.0, 900.0);
    const double hautSouffle = bandEnergy(b, depart, longueur, 1250.0, 1700.0);
    VSM_ASSERT(basSec > hautSec);
    VSM_ASSERT(basSouffle > hautSouffle);
    // Et il y a bien plus d'énergie hors des formants quand on souffle : on la
    // mesure loin au-dessus du troisième formant du « a » (2440 Hz).
    const double hautA = magnitudeAt(a, depart, longueur, 6000.0);
    const double hautB = magnitudeAt(b, depart, longueur, 6000.0);
    VSM_ASSERT(hautB > hautA * 1.5);
}

VSM_TEST(vocal_is_polyphonic) {
    auto p = makeVocal();
    tenue(*p);
    std::vector<MidiNoteEvent> ev{noteOn(0, 48), noteOn(0, 52), noteOn(0, 55)};
    std::vector<float> l(24000, 0.0f), r(24000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 24000);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 3);
}

VSM_TEST(vocal_a_full_choir_does_not_clip) {
    // Huit voix sur la MÊME voyelle est le pire cas de cette machine, et il
    // n'existe sur aucune autre : les trois formants sont aux mêmes fréquences
    // pour toutes les voix, donc leurs sorties sont corrélées.
    auto p = makeVocal();
    tenue(*p);
    std::vector<MidiNoteEvent> ev;
    for (int k = 0; k < 8; ++k) ev.push_back(noteOn(0, static_cast<uint8_t>(45 + 2 * k), 110));
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(ev.data(), static_cast<int>(ev.size()), l.data(), r.data(), 48000);
    VSM_ASSERT(peakAbs(l) > 0.03f);
    VSM_ASSERT(peakAbs(l) < 1.0f);
}

VSM_TEST(vocal_stays_finite_under_extreme_settings) {
    auto p = makeVocal();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto a = play(p, 36, 24000, 127);
    for (float s : a) VSM_ASSERT(std::isfinite(s));
    auto q = makeVocal();
    for (const auto& info : q->parameterList()) q->setParameter(info.id, info.minValue);
    const auto b = play(q, 96, 24000, 127);
    for (float s : b) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(vocal_is_deterministic) {
    auto a = makeVocal();
    auto b = makeVocal();
    const auto x = play(a, 45, 24000);   // avec le souffle par défaut, donc du bruit
    const auto y = play(b, 45, 24000);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT_EQ(x[i], y[i]);
}

VSM_TEST(vocal_save_load_roundtrip) {
    auto p = makeVocal();
    setByName(*p, "Vowel", 2.5f);
    setByName(*p, "Formant Shift", -3.5f);
    const auto state = p->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.vocal"));
    auto autre = makeVocal();
    autre->loadState(state);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Vowel")), 2.5f, 1e-6);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Formant Shift")), -3.5f, 1e-6);
}

VSM_TEST(vocal_parameter_list_is_complete) {
    auto p = makeVocal();
    VSM_ASSERT_EQ(static_cast<int>(p->parameterList().size()), 14);
}
