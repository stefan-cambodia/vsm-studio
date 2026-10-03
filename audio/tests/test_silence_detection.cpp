#include "TestFramework.h"
#include "vsm/audio/io/SilenceDetection.h"
#include <cmath>
#include <vector>

using namespace vsm::audio::io;

// D17.6 de docs/ROADMAP-daw.md — DÉTECTER LE SILENCE.
//
// Un stem reconstruit commence presque toujours par du rien : quatre cents
// millisecondes de plancher de séparation avant la première attaque, et l'on
// tirait le bord du clip à l'œil sur une forme d'onde où ce plancher est
// invisible parce qu'il est à -80 dB.

namespace {

/// `silenceAvant` secondes à -80 dB, puis du bruit à -6 dB, puis
/// `silenceApres` secondes à -80 dB.
std::vector<float> materiau(double sampleRate, double silenceAvant, double son,
                             double silenceApres) {
    const auto n = static_cast<size_t>((silenceAvant + son + silenceApres) * sampleRate);
    std::vector<float> s(n, 0.0f);
    const auto debut = static_cast<size_t>(silenceAvant * sampleRate);
    const auto fin = static_cast<size_t>((silenceAvant + son) * sampleRate);
    uint32_t etat = 12345u;
    for (size_t i = 0; i < n; ++i) {
        etat = etat * 1664525u + 1013904223u;
        const float v = static_cast<float>(static_cast<int32_t>(etat >> 8) % 20001 - 10000) / 10000.0f;
        // -80 dB hors du son : un vrai plancher de séparation, pas un zéro.
        s[i] = v * (i >= debut && i < fin ? 0.5f : 0.0001f);
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

VSM_TEST(silence_before_the_attack_is_found_to_the_millisecond) {
    constexpr double kSr = 48000.0;
    const auto s = materiau(kSr, 0.5, 1.0, 0.0);
    const auto bornes = detectSound(lecteur(s), static_cast<int64_t>(s.size()), kSr, -60.0, 0.0);
    VSM_ASSERT(bornes.found);
    // 500 ms, à la milliseconde près (48 échantillons).
    VSM_ASSERT(std::llabs(bornes.firstFrame - 24000) <= 48);
}

VSM_TEST(the_margin_keeps_the_transient_and_never_cuts_into_it) {
    constexpr double kSr = 48000.0;
    const auto s = materiau(kSr, 0.5, 1.0, 0.0);
    // Cinq millisecondes de marge : on coupe AVANT l'attaque, jamais dedans.
    const auto bornes = detectSound(lecteur(s), static_cast<int64_t>(s.size()), kSr, -60.0, 0.005);
    VSM_ASSERT(bornes.found);
    VSM_ASSERT(bornes.firstFrame < 24000);
    VSM_ASSERT(std::llabs(bornes.firstFrame - (24000 - 240)) <= 48);
}

VSM_TEST(a_file_that_starts_on_the_sound_is_not_touched) {
    // Le garde-fou du silence minimal : sans lui, la commande grignoterait
    // quelques millisecondes à chaque clip et l'on ne saurait jamais si elle a
    // fait quelque chose.
    constexpr double kSr = 48000.0;
    const auto s = materiau(kSr, 0.0, 1.0, 0.0);
    const auto bornes = detectSound(lecteur(s), static_cast<int64_t>(s.size()), kSr);
    VSM_ASSERT(bornes.found);
    VSM_ASSERT_EQ(bornes.firstFrame, int64_t(0));
    VSM_ASSERT_EQ(bornes.lastFrame, static_cast<int64_t>(s.size()));
}

VSM_TEST(both_ends_are_trimmed_and_a_wholly_silent_file_is_left_alone) {
    constexpr double kSr = 48000.0;
    const auto s = materiau(kSr, 0.5, 1.0, 0.5);
    const auto bornes = detectSound(lecteur(s), static_cast<int64_t>(s.size()), kSr, -60.0, 0.0);
    VSM_ASSERT(bornes.found);
    VSM_ASSERT(std::llabs(bornes.firstFrame - 24000) <= 48);
    VSM_ASSERT(std::llabs(bornes.lastFrame - 72000) <= 48);

    // Tout sous le seuil : on ne rogne RIEN. Un clip entièrement silencieux
    // réduit à zéro tick disparaîtrait, et personne n'a demandé de le
    // supprimer.
    std::vector<float> rien(48000, 0.00001f);
    const auto vide = detectSound(lecteur(rien), 48000, kSr);
    VSM_ASSERT(!vide.found);
}

// D542.3 : LES PASSAGES QUI SONNENT. Des salves de bruit à -6 dB séparées de ZÉROS.
namespace {
/// `schema` alterne silence et son, en secondes, en commençant par un silence.
std::vector<float> salves(double sampleRate, const std::vector<double>& schema) {
    std::vector<float> s;
    uint32_t etat = 777u;
    for (size_t k = 0; k < schema.size(); ++k) {
        const auto n = static_cast<size_t>(schema[k] * sampleRate);
        for (size_t i = 0; i < n; ++i) {
            etat = etat * 1664525u + 1013904223u;
            const float v = static_cast<float>(static_cast<int32_t>(etat >> 8) % 20001 - 10000) / 10000.0f;
            s.push_back(k % 2 == 1 ? 0.5f * v : 0.0f);
        }
    }
    return s;
}
}

VSM_TEST(trois_salves_separees_de_silences_font_trois_passages) {
    const double sr = 8000.0;
    // Des silences de BORD de 300 ms — au-dessus du minimum de 200 ms, donc retirés : la règle des
    // bords de `detectSound` garderait un silence de bord plus court.
    const auto s = salves(sr, {0.3, 0.2, 0.3, 0.2, 0.3, 0.2, 0.3});
    const auto p = detectSoundRegions(lecteur(s), static_cast<int64_t>(s.size()), sr);
    VSM_ASSERT_EQ(p.size(), size_t(3));
    // Bornes à la marge près (5 ms = 40 trames) : la première salve va de 2 400 à 4 000.
    VSM_ASSERT(std::llabs(p[0].firstFrame - (2400 - 40)) <= 2);
    VSM_ASSERT(std::llabs(p[0].lastFrame - (4000 + 40)) <= 2);
    VSM_ASSERT(std::llabs(p[1].firstFrame - (6400 - 40)) <= 2);
    VSM_ASSERT(std::llabs(p[2].lastFrame - (static_cast<int64_t>(s.size()) - 2400 + 40)) <= 2);
    // Un silence de bord PLUS COURT que le minimum n'est pas retiré.
    const auto court = salves(sr, {0.1, 0.2, 0.3, 0.2, 0.1});
    const auto q = detectSoundRegions(lecteur(court), static_cast<int64_t>(court.size()), sr);
    VSM_ASSERT_EQ(q.size(), size_t(2));
    VSM_ASSERT_EQ(q.front().firstFrame, int64_t(0));
    VSM_ASSERT_EQ(q.back().lastFrame, static_cast<int64_t>(court.size()));
}

VSM_TEST(un_trou_plus_court_que_le_silence_minimal_reste_dans_le_passage) {
    const double sr = 8000.0;
    const auto s = salves(sr, {0.1, 0.2, 0.05, 0.2, 0.1});
    const auto p = detectSoundRegions(lecteur(s), static_cast<int64_t>(s.size()), sr, -60.0, 0.005, 0.200);
    VSM_ASSERT_EQ(p.size(), size_t(1));
}

VSM_TEST(tout_sous_le_seuil_ne_fait_aucun_passage_et_un_son_aux_deux_bords_en_fait_un) {
    const double sr = 8000.0;
    const std::vector<float> zeros(8000, 0.0f);
    VSM_ASSERT(detectSoundRegions(lecteur(zeros), 8000, sr).empty());
    const auto plein = salves(sr, {0.0, 1.0});
    const auto p = detectSoundRegions(lecteur(plein), static_cast<int64_t>(plein.size()), sr);
    VSM_ASSERT_EQ(p.size(), size_t(1));
    VSM_ASSERT_EQ(p[0].firstFrame, int64_t(0));
    VSM_ASSERT_EQ(p[0].lastFrame, static_cast<int64_t>(plein.size()));
}
