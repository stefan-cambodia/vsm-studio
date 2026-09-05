#include "TestFramework.h"
#include "vsm/audio/io/OnsetDetection.h"
#include <cmath>
#include <cstdlib>
#include <vector>

using namespace vsm::audio::io;

// D20.3 de docs/ROADMAP-daw.md — DÉTECTER LES ATTAQUES.
//
// Huit coups dans du bruit sont trouvés à ±2 ms, une sinusoïde tenue n'en
// donne aucun, deux coups à 10 ms n'en donnent qu'un, l'écart minimal se règle :
// c'est le contrat que
// « Découper aux transitoires » promet, et il se vérifie sans écran.

namespace {

constexpr double kSr = 48000.0;

/// Un coup : une décroissance exponentielle de bruit, 60 ms, à -6 dB de crête.
void poserUnCoup(std::vector<float>& s, double seconde, uint32_t& etat) {
    const auto debut = static_cast<size_t>(seconde * kSr);
    const auto duree = static_cast<size_t>(0.060 * kSr);
    for (size_t i = 0; i < duree && debut + i < s.size(); ++i) {
        etat = etat * 1664525u + 1013904223u;
        const float v = static_cast<float>(static_cast<int32_t>(etat >> 8) % 20001 - 10000) / 10000.0f;
        const float enveloppe = std::exp(-static_cast<float>(i) / (0.012f * static_cast<float>(kSr)));
        s[debut + i] += 0.5f * v * enveloppe;
    }
}

/// Du bruit de fond à -60 dB, sur `secondes`.
std::vector<float> fond(double secondes, uint32_t& etat) {
    std::vector<float> s(static_cast<size_t>(secondes * kSr), 0.0f);
    for (auto& v : s) {
        etat = etat * 1664525u + 1013904223u;
        v = static_cast<float>(static_cast<int32_t>(etat >> 8) % 20001 - 10000) / 10000.0f * 0.001f;
    }
    return s;
}

auto lecteur(const std::vector<float>& s) {
    return [&s](int64_t i, float& g, float& d) {
        if (i < 0 || i >= static_cast<int64_t>(s.size())) return false;
        g = d = s[static_cast<size_t>(i)];
        return true;
    };
}

} // namespace

VSM_TEST(eight_hits_in_noise_are_found_within_two_milliseconds) {
    uint32_t etat = 7u;
    std::vector<float> s = fond(2.0, etat);
    const double instants[] = {0.100, 0.350, 0.500, 0.775, 1.000, 1.240, 1.500, 1.900};
    for (double t : instants) poserUnCoup(s, t, etat);

    const auto attaques = detectOnsets(lecteur(s), static_cast<int64_t>(s.size()), kSr);
    VSM_ASSERT_EQ(attaques.size(), size_t(8));
    for (size_t k = 0; k < 8; ++k) {
        const double trouve = static_cast<double>(attaques[k]) / kSr;
        // AVANT le coup, jamais après : une coupe posée après l'attaque
        // mangerait le transitoire. La marge rendue est de 3 ms ; l'instant
        // trouvé est donc entre 3 et 5 ms avant le coup -- deux millisecondes
        // d'erreur au plus sur l'attaque elle-même.
        VSM_ASSERT(trouve <= instants[k] - 0.003 + 1e-9);
        VSM_ASSERT(instants[k] - trouve < 0.005);
    }
    // Croissantes, et aucune en zéro : le début du clip n'est pas une coupe.
    for (size_t k = 1; k < attaques.size(); ++k) VSM_ASSERT(attaques[k] > attaques[k - 1]);
    VSM_ASSERT(attaques.front() > 0);
}

VSM_TEST(a_held_sine_and_silence_give_no_onset_at_all) {
    std::vector<float> sinus(static_cast<size_t>(1.5 * kSr));
    for (size_t i = 0; i < sinus.size(); ++i)
        sinus[i] = 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / static_cast<float>(kSr));
    // Un fondu de 20 ms au départ : une sinusoïde qui démarre EST une
    // attaque, et elle est trouvée à raison, dans ses premières
    // millisecondes ; ce qu'on vérifie ici, c'est qu'un son TENU n'en produit
    // aucune ENSUITE -- la fluctuation d'énergie d'une trame à l'autre d'une
    // sinusoïde ne bondit jamais de 8 dB.
    const auto fondu = static_cast<size_t>(0.020 * kSr);
    for (size_t i = 0; i < fondu; ++i) sinus[i] *= static_cast<float>(i) / static_cast<float>(fondu);
    const auto tenue = detectOnsets(lecteur(sinus), static_cast<int64_t>(sinus.size()), kSr);
    VSM_ASSERT(tenue.size() <= size_t(1));
    for (int64_t a : tenue) VSM_ASSERT(static_cast<double>(a) / kSr < 0.030);

    uint32_t etat = 3u;
    const std::vector<float> rien = fond(1.0, etat);
    VSM_ASSERT(detectOnsets(lecteur(rien), static_cast<int64_t>(rien.size()), kSr).empty());
    VSM_ASSERT(detectOnsets(lecteur(rien), 0, kSr).empty());
}

VSM_TEST(two_hits_ten_milliseconds_apart_are_one_onset_and_the_gap_is_a_setting) {
    uint32_t etat = 11u;
    std::vector<float> s = fond(1.0, etat);
    poserUnCoup(s, 0.300, etat);
    poserUnCoup(s, 0.310, etat);   // le rebond : dans la même trame de 20 ms, un seul coup
    poserUnCoup(s, 0.600, etat);
    poserUnCoup(s, 0.700, etat);   // à 100 ms : deux coups au réglage par défaut (50 ms)
    const auto attaques = detectOnsets(lecteur(s), static_cast<int64_t>(s.size()), kSr);
    VSM_ASSERT_EQ(attaques.size(), size_t(3));
    VSM_ASSERT(std::fabs(static_cast<double>(attaques[0]) / kSr - (0.300 - 0.003)) < 0.002);
    VSM_ASSERT(std::fabs(static_cast<double>(attaques[1]) / kSr - (0.600 - 0.003)) < 0.002);
    VSM_ASSERT(std::fabs(static_cast<double>(attaques[2]) / kSr - (0.700 - 0.003)) < 0.002);
    // L'écart minimal se règle : à 200 ms, le coup de 0,7 s rejoint celui de 0,6 s.
    VSM_ASSERT_EQ(detectOnsets(lecteur(s), static_cast<int64_t>(s.size()), kSr, 8.0, 0.200).size(), size_t(2));
}

// UN MOTIF DE BOÎTE À RYTHMES : une grosse caisse (sinus de 55 Hz qui descend,
// 200 ms) et un charleston (bruit bref, 30 ms) en alternance, à la double
// croche de 120 BPM. Sur ce matériau, la fenêtre de 5 ms d'un flux d'énergie
// traverse la période de la grosse caisse, et le test dit si les attaques
// tiennent quand même -- c'est le cas d'usage réel, pas les impulsions.
VSM_TEST(a_drum_machine_pattern_gives_one_onset_per_hit) {
    constexpr double kSr44 = 44100.0;
    std::vector<float> s(static_cast<size_t>(2.0 * kSr44), 0.0f);
    uint32_t etat = 5u;
    const double kicks[] = {0.0, 1.0};
    const double hats[] = {0.25, 0.5, 0.75, 1.25, 1.5, 1.75};
    for (double t : kicks) {
        const auto debut = static_cast<size_t>(t * kSr44);
        for (size_t i = 0; i < static_cast<size_t>(0.200 * kSr44) && debut + i < s.size(); ++i) {
            const float temps = static_cast<float>(i) / static_cast<float>(kSr44);
            const float frequence = 55.0f + 60.0f * std::exp(-temps * 40.0f);   // la hauteur qui descend
            s[debut + i] += 0.8f * std::exp(-temps * 12.0f) * std::sin(2.0f * 3.14159265f * frequence * temps);
        }
    }
    for (double t : hats) {
        const auto debut = static_cast<size_t>(t * kSr44);
        for (size_t i = 0; i < static_cast<size_t>(0.030 * kSr44) && debut + i < s.size(); ++i) {
            etat = etat * 1664525u + 1013904223u;
            const float v = static_cast<float>(static_cast<int32_t>(etat >> 8) % 20001 - 10000) / 10000.0f;
            s[debut + i] += 0.35f * v * std::exp(-static_cast<float>(i) / (0.008f * static_cast<float>(kSr44)));
        }
    }
    const auto attaques = detectOnsets(lecteur(s), static_cast<int64_t>(s.size()), kSr44);
    // La grosse caisse à zéro est le début du clip : sept coupes, pas huit.
    VSM_ASSERT_EQ(attaques.size(), size_t(7));
    const double attendus[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75};
    for (size_t k = 0; k < 7; ++k)
        VSM_ASSERT(std::fabs(static_cast<double>(attaques[k]) / kSr44 - (attendus[k] - 0.003)) < 0.002);
}
