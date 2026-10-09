#include "TestFramework.h"
#include "vsm/audio/io/TempoDetection.h"
#include <cmath>
#include <cstdio>
#include <vector>

// D545.2 — LE TEMPO D'UNE PRISE, sur des boucles de batterie de synthèse de tempo CONNU. L'attendu, écrit
// avant le code (ROADMAP-daw.md) : ±0,1 BPM à 92, 120 et 137,5 ; 100 et non 200 avec des doubles croches ;
// 120 et non 240 pour un charleston seul aux croches ; rien pour un bruit ou un son tenu, et c'est dit.
using namespace vsm::audio::io;

namespace {
constexpr double kSr = 44100.0;

/// Un générateur déterministe : le même bruit à chaque course.
struct Bruit {
    uint32_t etat = 2463534242u;
    float operator()() {
        etat ^= etat << 13; etat ^= etat >> 17; etat ^= etat << 5;
        return static_cast<float>(etat) / 4294967295.0f * 2.0f - 1.0f;
    }
};

/// Quatre mesures (ou `mesures`) : grosse caisse sur 1 et 3, caisse claire sur 2 et 4, charleston tous les
/// `parTemps`-ièmes de temps (0 : aucun) ; `sansFutsEtCaisse` : le charleston seul.
std::vector<float> boucle(double bpm, int parTemps, bool sansFutsEtCaisse = false, int mesures = 4) {
    const double temps = 60.0 / bpm;
    const auto n = static_cast<size_t>(mesures * 4 * temps * kSr) + 1;
    std::vector<float> x(n, 0.0f);
    Bruit bruit;
    const auto poser = [&](double t, double duree, auto&& onde) {
        const auto debut = static_cast<size_t>(std::llround(t * kSr));
        for (size_t i = 0; i < static_cast<size_t>(duree * kSr) && debut + i < n; ++i)
            x[debut + i] += onde(static_cast<double>(i) / kSr);
    };
    for (int b = 0; b < mesures * 4; ++b) {
        const double t = b * temps;
        if (!sansFutsEtCaisse) {
            if (b % 2 == 0)
                poser(t, 0.15, [](double s) { return 0.8f * static_cast<float>(std::sin(2 * M_PI * 60 * s) * std::exp(-s / 0.05)); });
            else
                poser(t, 0.10, [&](double s) { return 0.5f * bruit() * static_cast<float>(std::exp(-s / 0.03)); });
        }
        for (int k = 0; k < parTemps; ++k)
            poser(t + k * temps / parTemps, 0.03, [&](double s) {
                return 0.25f * bruit() * static_cast<float>(std::exp(-s / 0.008));
            });
    }
    return x;
}

TempoEstimate estimer(const std::vector<float>& x) {
    return estimateTempo([&x](int64_t i, float& g, float& d) {
        if (i < 0 || static_cast<size_t>(i) >= x.size()) return false;
        g = d = x[static_cast<size_t>(i)];
        return true;
    }, static_cast<int64_t>(x.size()), kSr);
}
}

VSM_TEST(the_tempo_of_a_drum_loop_is_found_within_a_tenth_of_a_bpm) {
    for (const double bpm : {92.0, 120.0, 137.5}) {
        const auto e = estimer(boucle(bpm, 2));
        std::printf("    [banc tempo] boucle à %.1f BPM : %.2f BPM (confiance %.2f)\n", bpm, e.bpm, e.confidence);
        VSM_ASSERT(e.found);
        VSM_ASSERT(std::fabs(e.bpm - bpm) <= 0.1);
    }
}

VSM_TEST(sixteenths_or_eighths_alone_do_not_double_the_tempo) {
    const auto doubles = estimer(boucle(100.0, 4));
    const auto seul = estimer(boucle(120.0, 2, true));
    std::printf("    [banc tempo] doubles croches à 100 : %.2f ; charleston seul aux croches à 120 : %.2f\n",
                doubles.bpm, seul.bpm);
    VSM_ASSERT(doubles.found && std::fabs(doubles.bpm - 100.0) <= 0.1);
    VSM_ASSERT(seul.found && std::fabs(seul.bpm - 120.0) <= 0.1);
}

VSM_TEST(noise_and_a_held_tone_have_no_tempo_and_it_is_said) {
    Bruit bruit;
    std::vector<float> blanc(static_cast<size_t>(8 * kSr));
    for (auto& v : blanc) v = 0.3f * bruit();
    std::vector<float> tenu(static_cast<size_t>(8 * kSr));
    for (size_t i = 0; i < tenu.size(); ++i) tenu[i] = 0.3f * static_cast<float>(std::sin(2 * M_PI * 220.0 * i / kSr));
    const auto a = estimer(blanc), b = estimer(tenu);
    std::printf("    [banc tempo] bruit : %s (confiance %.2f) ; sinus tenu : %s\n",
                a.found ? "TROUVÉ" : a.reason.c_str(), a.confidence, b.found ? "TROUVÉ" : b.reason.c_str());
    VSM_ASSERT(!a.found && !a.reason.empty());
    VSM_ASSERT(!b.found && !b.reason.empty());
}
