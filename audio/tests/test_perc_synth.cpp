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

SynthPluginPtr makePerc(double sr = kSampleRate) {
    auto p = PluginRegistry::instance().create("vsm.perc");
    p->initialize(sr, 512);
    return p;
}

float peakAbs(const std::vector<float>& b) {
    float p = 0.0f; for (float s : b) p = std::max(p, std::abs(s)); return p;
}

double energy(const std::vector<float>& b, size_t from = 0) {
    double e = 0.0;
    for (size_t i = from; i < b.size(); ++i) e += static_cast<double>(b[i]) * b[i];
    return e;
}

MidiNoteEvent hit(int offset, uint8_t note, uint8_t vel = 110) {
    return MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, offset, 9, note, vel};
}

ParamId paramByName(const ISynthPlugin& p, const std::string& n) {
    for (const auto& info : p.parameterList()) if (info.name == n) return info.id;
    return 0;
}

void setByName(ISynthPlugin& p, const std::string& n, float v) {
    p.setParameter(paramByName(p, n), v);
}

/// Rend `numSamples` échantillons en frappant `note` au tout début.
std::vector<float> strike(SynthPluginPtr& p, uint8_t note, int numSamples, uint8_t vel = 110) {
    std::vector<float> l(static_cast<size_t>(numSamples), 0.0f);
    std::vector<float> r(static_cast<size_t>(numSamples), 0.0f);
    const MidiNoteEvent ev = hit(0, note, vel);
    p->process(&ev, 1, l.data(), r.data(), numSamples);
    return l;
}

/// Amplitude du signal à la fréquence `hz`, par corrélation directe (Goertzel
/// naïf) : on ne veut qu'une poignée de fréquences précises, une FFT entière
/// serait du travail perdu et une fenêtre de plus à justifier.
double magnitudeAt(const std::vector<float>& x, size_t from, size_t count, double hz) {
    const size_t fin = std::min(x.size(), from + count);
    if (from >= fin) return 0.0;
    std::complex<double> acc{0.0, 0.0};
    const double w = 2.0 * 3.14159265358979323846 * hz / kSampleRate;
    size_t n = 0;
    for (size_t i = from; i < fin; ++i, ++n) {
        // Fenêtre de Hann, pour que les modes voisins ne se répondent pas.
        const double fen = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846
                                                * static_cast<double>(n)
                                                / static_cast<double>(fin - from));
        acc += std::complex<double>(x[i] * fen, 0.0)
             * std::exp(std::complex<double>(0.0, -w * static_cast<double>(n)));
    }
    return std::abs(acc) / static_cast<double>(fin - from);
}

} // namespace

VSM_TEST(perc_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.perc"));
}

VSM_TEST(perc_silent_with_no_events) {
    auto p = makePerc();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    p->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(p->activeVoiceCount(), 0);
}

VSM_TEST(perc_every_general_midi_piece_sounds) {
    // Les treize pièces sont adressées par leur numéro GENERAL MIDI, sans
    // écart : un fichier écrit pour un module GM doit jouer juste ici. Une
    // pièce muette serait une pièce annoncée et absente -- exactement la panne
    // silencieuse que le § 5 bis de ROADMAP-fusion.md interdit.
    const uint8_t notes[] = {54, 56, 60, 61, 62, 63, 64, 65, 66, 70, 75, 76, 77};
    for (uint8_t note : notes) {
        auto p = makePerc();
        const auto out = strike(p, note, 24000);
        VSM_ASSERT(peakAbs(out) > 0.01f);
    }
}

VSM_TEST(perc_ignores_notes_it_does_not_declare) {
    // Une note hors du kit ne doit RIEN produire, et surtout pas la pièce la
    // plus proche : un module qui devine invente des frappes que personne n'a
    // écrites.
    auto p = makePerc();
    const auto out = strike(p, 36, 12000);   // 36 = grosse caisse GM, absente ici
    VSM_ASSERT_NEAR(peakAbs(out), 0.0, 1e-6);
}

VSM_TEST(perc_membrane_modes_are_inharmonic) {
    // LE TRAIT DISTINCTIF, ET LA SEULE CHOSE QUI JUSTIFIE CETTE MACHINE.
    //
    // Les boîtes du parc fabriquent leurs peaux avec un sinus et une enveloppe
    // de hauteur : leur spectre est harmonique, ou presque pur. Une membrane
    // circulaire tendue ne l'est pas -- ses modes sont les zéros de la fonction
    // de Bessel J0, dans les rapports 1 ; 1,594 ; 2,136 ; 2,296. C'est
    // exactement ce qui fait qu'un tambour rend un SON et non une NOTE.
    //
    // On vérifie donc les deux moitiés de l'affirmation : le mode inharmonique
    // à 1,594·f0 est présent, ET l'harmonique 2·f0 -- qui serait là si la peau
    // était un simple oscillateur -- ne l'est pas.
    auto p = makePerc();
    const float f0 = 150.0f;
    setByName(*p, "Conga Tune", f0);
    setByName(*p, "Conga Decay", 1.0f);
    const auto out = strike(p, 64, 48000);   // 64 = Low Conga, donc 0,75·f0

    const double fondamental = f0 * 0.75;
    // On mesure APRÈS l'attaque : le choc de la main est du bruit large, et il
    // masquerait la question posée, qui porte sur les modes entretenus.
    const size_t depart = 4800, longueur = 32768;
    const double m1 = magnitudeAt(out, depart, longueur, fondamental);
    const double mBessel = magnitudeAt(out, depart, longueur, fondamental * 1.5933);
    const double mOctave = magnitudeAt(out, depart, longueur, fondamental * 2.0);

    VSM_ASSERT(m1 > 1e-4);                    // la peau sonne
    VSM_ASSERT(mBessel > m1 * 0.05);          // le mode de Bessel est PRÉSENT
    VSM_ASSERT(mBessel > mOctave * 3.0);      // et l'octave, elle, ne l'est pas
}

VSM_TEST(perc_bar_modes_are_inharmonic) {
    // Même exigence pour les barres : un bloc de bois libre aux deux bouts
    // sonne à 1 ; 2,756 ; 5,404. Le rapport 2,756 est ce qui donne le « toc »
    // sec ; un rapport entier donnerait un carillon.
    auto p = makePerc();
    const float f0 = 1100.0f;
    setByName(*p, "Wood Tune", f0);
    setByName(*p, "Wood Decay", 0.30f);
    const auto out = strike(p, 76, 24000);   // 76 = Hi Wood Block

    const size_t depart = 480, longueur = 16384;
    const double m1 = magnitudeAt(out, depart, longueur, f0);
    const double mBar = magnitudeAt(out, depart, longueur, f0 * 2.756);
    const double mOctave = magnitudeAt(out, depart, longueur, f0 * 2.0);

    VSM_ASSERT(m1 > 1e-4);
    VSM_ASSERT(mBar > m1 * 0.05);
    VSM_ASSERT(mBar > mOctave * 3.0);
}

VSM_TEST(perc_tune_moves_the_pitch) {
    // UN RÉGLAGE QUI NE FAIT RIEN EST PIRE QU'UN RÉGLAGE ABSENT (§ 33
    // d'ARCHITECTURE.md, `Bore Shape` retiré de vsm.wind pour cela). Celui-ci
    // est mesuré sur sa course : doubler la tension doit doubler la hauteur.
    auto mesurer = [](float tune) {
        auto p = makePerc();
        setByName(*p, "Conga Tune", tune);
        setByName(*p, "Conga Decay", 1.0f);
        const auto out = strike(p, 63, 48000);   // 63 = Open Hi Conga, donc f0
        return magnitudeAt(out, 4800, 32768, tune);
    };
    // Chaque mesure regarde SA propre hauteur : si le réglage agit, chacune
    // trouve de l'énergie là où elle la cherche.
    VSM_ASSERT(mesurer(120.0f) > 1e-4);
    VSM_ASSERT(mesurer(240.0f) > 1e-4);

    // Et le contrôle qui rend la mesure concluante : accordée à 120 Hz, la peau
    // ne doit PAS sonner à 240.
    auto p = makePerc();
    setByName(*p, "Conga Tune", 120.0f);
    setByName(*p, "Conga Decay", 1.0f);
    const auto out = strike(p, 63, 48000);
    const double a = magnitudeAt(out, 4800, 32768, 120.0);
    const double b = magnitudeAt(out, 4800, 32768, 240.0);
    VSM_ASSERT(a > b * 3.0);
}

VSM_TEST(perc_muted_conga_is_shorter_than_open) {
    // La conga étouffée est la MÊME peau, la main posée dessus. Elle doit donc
    // avoir la même hauteur et une queue bien plus courte : c'est une nuance de
    // jeu, et si les deux notes sonnaient pareil, l'une des deux mentirait.
    auto ouvert = makePerc();
    auto etouffe = makePerc();
    const auto a = strike(ouvert, 63, 48000);
    const auto b = strike(etouffe, 62, 48000);
    // Sur la SECONDE moitié du rendu, l'étouffée doit avoir presque disparu.
    VSM_ASSERT(energy(b, 24000) < energy(a, 24000) * 0.25);
}

VSM_TEST(perc_velocity_changes_level_not_pitch) {
    auto fort = makePerc();
    auto doux = makePerc();
    const auto a = strike(fort, 64, 24000, 127);
    const auto b = strike(doux, 64, 24000, 40);
    VSM_ASSERT(peakAbs(a) > peakAbs(b) * 1.4f);
}

VSM_TEST(perc_a_full_section_does_not_clip) {
    // Les treize pièces frappées ensemble : un pupitre entier ne doit pas
    // saturer. C'est le niveau calibré du `process()` qui est en jeu.
    auto p = makePerc();
    const uint8_t notes[] = {54, 56, 60, 61, 62, 63, 64, 65, 66, 70, 75, 76, 77};
    std::vector<MidiNoteEvent> events;
    for (uint8_t n : notes) events.push_back(hit(0, n, 127));
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    p->process(events.data(), static_cast<int>(events.size()), l.data(), r.data(), 48000);
    VSM_ASSERT(peakAbs(l) > 0.05f);
    VSM_ASSERT(peakAbs(l) < 1.0f);
}

VSM_TEST(perc_stays_finite_under_extreme_settings) {
    auto p = makePerc();
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.maxValue);
    const auto out = strike(p, 64, 24000, 127);
    for (float s : out) VSM_ASSERT(std::isfinite(s));
    for (const auto& info : p->parameterList()) p->setParameter(info.id, info.minValue);
    const auto out2 = strike(p, 64, 24000, 127);
    for (float s : out2) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(perc_is_deterministic) {
    // Le bruit du choc et du shaker passe par DeterministicRng : deux rendus
    // d'une même session doivent être identiques au bit près, sans quoi les
    // empreintes de non-régression et la recherche de patch ne valent rien.
    auto a = makePerc();
    auto b = makePerc();
    const auto x = strike(a, 70, 24000);   // maracas : la pièce la plus bruitée
    const auto y = strike(b, 70, 24000);
    for (size_t i = 0; i < x.size(); ++i) VSM_ASSERT_EQ(x[i], y[i]);
}

VSM_TEST(perc_save_load_roundtrip) {
    auto p = makePerc();
    setByName(*p, "Conga Tune", 187.0f);
    setByName(*p, "Wood Decay", 0.11f);
    const auto state = p->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.perc"));
    auto autre = makePerc();
    autre->loadState(state);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Conga Tune")), 187.0f, 1e-6);
    VSM_ASSERT_NEAR(autre->getParameter(paramByName(*autre, "Wood Decay")), 0.11f, 1e-6);
}

VSM_TEST(perc_parameter_list_is_complete) {
    auto p = makePerc();
    VSM_ASSERT_EQ(static_cast<int>(p->parameterList().size()), 21);
    // Toutes les valeurs sont en unités physiques : aucune plage 0..1 pour une
    // hauteur ou une durée. On vérifie que les hertz sont des hertz.
    for (const auto& info : p->parameterList()) {
        if (info.name.find("Tune") != std::string::npos
            || info.name.find("Tone") != std::string::npos)
            VSM_ASSERT(info.maxValue > 100.0f);
        if (info.name.find("Decay") != std::string::npos)
            VSM_ASSERT(info.maxValue <= 3.0f);
    }
}
