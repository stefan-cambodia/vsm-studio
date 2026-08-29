#include "TestFramework.h"
#include "vsm/audio/engine/LatencyProbe.h"
#include <cmath>
#include <vector>

using namespace vsm::audio::engine;

// D3.6 de docs/ROADMAP-daw.md — LA LATENCE MESURÉE, PAS ESTIMÉE.
//
// La mesure elle-même demande un câble ; sa DÉTECTION, non : on peut fabriquer
// la capture qu'on aurait eue, avec le retard qu'on veut, et vérifier qu'on le
// retrouve. C'est la partie qui peut être fausse en silence, et donc la seule
// qui ait besoin d'être testée.

namespace {

/// La capture qu'on aurait obtenue : du silence, la sonde à `retard`, puis du
/// silence -- le tout noyé dans du bruit et atténué, comme un vrai retour.
std::vector<float> captureSimulee(const std::vector<float>& sonde, int retard, int longueur,
                                   float attenuation, float bruit, unsigned graine = 12345u) {
    std::vector<float> capture(static_cast<size_t>(longueur), 0.0f);
    unsigned etat = graine;
    for (int i = 0; i < longueur; ++i) {
        etat = etat * 1664525u + 1013904223u;
        const float alea = (static_cast<float>(etat >> 8) / 8388608.0f) - 1.0f;
        capture[static_cast<size_t>(i)] = alea * bruit;
    }
    for (size_t i = 0; i < sonde.size(); ++i) {
        const size_t j = static_cast<size_t>(retard) + i;
        if (j < capture.size()) capture[j] += sonde[i] * attenuation;
    }
    return capture;
}

} // namespace

VSM_TEST(the_probe_is_a_sweep_that_starts_and_ends_at_silence) {
    // Fenêtrée aux deux bouts : sans cela les extrémités claquent, et un clac
    // est précisément ce qu'on évitait en n'employant pas un clic.
    const auto sonde = LatencyProbe::makeProbe(48000.0);
    VSM_ASSERT_EQ(sonde.size(), size_t(48000 * 0.030));
    VSM_ASSERT_NEAR(sonde.front(), 0.0f, 1e-6f);
    VSM_ASSERT_NEAR(sonde.back(), 0.0f, 1e-6f);

    float crete = 0.0f;
    for (float e : sonde) crete = std::max(crete, std::abs(e));
    VSM_ASSERT(crete > 0.3f);      // il s'entend
    VSM_ASSERT(crete <= 0.5f);     // et il ne sature pas
}

VSM_TEST(a_known_delay_is_found_to_the_sample) {
    // Le critère de l'étape : « une boucle physique enregistre à l'échantillon
    // près ». Sur une boucle simulée, l'échantillon près est exigible.
    const auto sonde = LatencyProbe::makeProbe(48000.0);
    for (int retard : {0, 137, 1024, 4096, 9001}) {
        const auto capture = captureSimulee(sonde, retard, 24000, 0.6f, 0.0f);
        const auto trouve = LatencyProbe::detecter(capture, sonde);
        VSM_ASSERT(trouve.trouve());
        VSM_ASSERT_EQ(trouve.decalageEchantillons, retard);
    }
}

VSM_TEST(the_delay_is_still_found_through_noise_and_attenuation) {
    // Un vrai retour est atténué et bruité -- micro devant un haut-parleur,
    // entrée à fort gain. Un seuil s'y perdrait ; une corrélation, non.
    const auto sonde = LatencyProbe::makeProbe(48000.0);
    const auto capture = captureSimulee(sonde, 2048, 24000, 0.15f, 0.05f);
    const auto trouve = LatencyProbe::detecter(capture, sonde);
    VSM_ASSERT(trouve.trouve());
    VSM_ASSERT_EQ(trouve.decalageEchantillons, 2048);
    VSM_ASSERT(trouve.nettete > 10.0);
}

VSM_TEST(pure_noise_is_reported_as_unconvincing_instead_of_a_number) {
    // LE CAS QUI COMPTE : le câble n'est pas branché. Publier un chiffre
    // inventé serait pire que de ne rien publier -- on l'appliquerait à toutes
    // les prises suivantes sans jamais le remettre en question.
    const auto sonde = LatencyProbe::makeProbe(48000.0);
    const auto capture = captureSimulee(sonde, 0, 24000, 0.0f, 0.1f);
    const auto trouve = LatencyProbe::detecter(capture, sonde);
    VSM_ASSERT(trouve.nettete < 10.0);
}

VSM_TEST(a_capture_shorter_than_the_probe_finds_nothing_rather_than_guessing) {
    const auto sonde = LatencyProbe::makeProbe(48000.0);
    std::vector<float> courte(100, 0.0f);
    VSM_ASSERT(!LatencyProbe::detecter(courte, sonde).trouve());
    VSM_ASSERT(!LatencyProbe::detecter(courte, {}).trouve());
}
