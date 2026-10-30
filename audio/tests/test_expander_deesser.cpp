#include "TestFramework.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/effect/ExpanderDeEsser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace vsm::audio::effect;

// D596 — L'EXPANDEUR ET LE DE-ESSER. L'attendu écrit avant le code (ROADMAP-daw, D596) : 48 kHz, sinus, mesure sur la
// seconde qui suit une seconde de mise en route, par blocs de 512.

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Le niveau de sortie (crête d'un sinus de même valeur efficace, en dBFS) d'un sinus de crête `creteDb` à `hz`, et si
/// la seconde MESURÉE (la deuxième) est identique à l'entrée au bit près — l'attaque du son n'en est pas : un expandeur y
/// part fermé, le silence d'avant étant sous son seuil.
double sortieDb(IAudioEffect& effet, double hz, double creteDb, bool* bitPres = nullptr) {
    const int n = 96000;
    const double a = std::pow(10.0, creteDb / 20.0);
    std::vector<float> l(n), r(n);
    for (int i = 0; i < n; ++i) l[i] = r[i] = static_cast<float>(a * std::sin(2.0 * kPi * hz * i / 48000.0));
    const std::vector<float> entree = l;
    effet.prepare(48000.0, 512);
    for (int debut = 0; debut < n; debut += 512) effet.process(l.data() + debut, r.data() + debut, std::min(512, n - debut));
    if (bitPres != nullptr) *bitPres = std::equal(l.begin() + 48000, l.end(), entree.begin() + 48000);
    double s = 0.0;
    for (int i = 48000; i < n; ++i) s += static_cast<double>(l[i]) * l[i];
    return 10.0 * std::log10(2.0 * s / 48000.0);
}

} // namespace

VSM_TEST(the_expander_lowers_below_its_threshold_by_ratio_minus_one_and_stops_at_its_range) {
    ExpanderEffect e;   // réglages d'origine : seuil −40, rapport 2, plage −40
    bool bitPres = false;
    const double haut = sortieDb(e, 1000.0, -30.0, &bitPres);
    const double moyen = sortieDb(e, 1000.0, -50.0);
    const double bas = sortieDb(e, 1000.0, -70.0);
    e.setParameter(ExpanderEffect::kRangeDb, -20.0f);
    const double borne = sortieDb(e, 1000.0, -70.0);
    std::printf("    D596 expandeur : -30 -> %.3f (bit près : %s), -50 -> %.3f, -70 -> %.3f, -70 plage -20 -> %.3f dBFS\n",
                haut, bitPres ? "oui" : "non", moyen, bas, borne);
    VSM_ASSERT_NEAR(haut, -30.0, 0.05);
    VSM_ASSERT(bitPres);
    VSM_ASSERT_NEAR(moyen, -60.0, 0.3);
    VSM_ASSERT_NEAR(bas, -100.0, 0.5);
    VSM_ASSERT_NEAR(borne, -90.0, 0.5);
}

VSM_TEST(the_de_esser_lets_the_low_end_through_bit_for_bit_and_cuts_a_loud_8_khz) {
    DeEsserEffect d;   // réglages d'origine : 6 kHz, seuil −30, réduction maximale 12
    bool bitPres = false;
    const double grave = sortieDb(d, 300.0, -10.0, &bitPres);
    const double aigu = sortieDb(d, 8000.0, -10.0);
    d.setParameter(DeEsserEffect::kMaxReductionDb, 24.0f);
    const double aiguLibre = sortieDb(d, 8000.0, -10.0);
    std::printf("    D596 de-esser : 300 Hz -> %.3f (bit près : %s), 8 kHz -> %.3f, 8 kHz sans borne -> %.3f dBFS\n",
                grave, bitPres ? "oui" : "non", aigu, aiguLibre);
    VSM_ASSERT(bitPres);
    VSM_ASSERT_NEAR(aigu, -22.0, 0.5);
    VSM_ASSERT_NEAR(aiguLibre, -10.0 - 12.9, 0.5);
}

VSM_TEST(the_two_new_inserts_come_after_the_sixteen_and_are_made_by_the_factory) {
    const auto& liste = EffectFactory::available();
    // D605 : AU MOINS dix-huit — l'accordeur vient après, et c'est son test qui garde son rang ; ce qui compte ici est
    // que les deux de D596 suivent les seize d'avant.
    VSM_ASSERT(liste.size() >= static_cast<size_t>(18));
    VSM_ASSERT(liste[16].id == "expander");
    VSM_ASSERT(liste[17].id == "deesser");
    VSM_ASSERT(EffectFactory::create("expander") != nullptr);
    VSM_ASSERT(EffectFactory::create("deesser") != nullptr);
}
