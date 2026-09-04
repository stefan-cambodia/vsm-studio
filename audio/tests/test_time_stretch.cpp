#include "TestFramework.h"
#include "vsm/audio/dsp/TimeStretch.h"
#include "vsm/audio/dsp/TransientDetector.h"
#include "vsm/audio/engine/SampleStore.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// LE BANC DE D12.2 ET D12.3 (`docs/CDC-etirement-temporel.md`, § 5), écrit
// avant la première mesure : la hauteur ne bouge pas, la durée est exacte, le
// rapport 1 rend l'original au bit près, les transitoires passent une fois, un
// son tenu ne flotte pas trop, tout est déterministe et indépendant des blocs.

namespace {

using vsm::audio::engine::MemorySampleStore;
using Stretch = vsm::audio::dsp::TimeStretch<MemorySampleStore>;
constexpr double kSr = 48000.0;

MemorySampleStore sinus(double hz, double secondes, float amplitude = 0.5f) {
    const auto n = static_cast<size_t>(secondes * kSr);
    std::vector<float> l(n), r(n);
    for (size_t i = 0; i < n; ++i) {
        l[i] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * hz * static_cast<double>(i) / kSr));
        r[i] = l[i] * 0.8f;
    }
    return MemorySampleStore(std::move(l), std::move(r));
}
/// Un signal riche et non périodique : deux sinus incommensurables et un bruit
/// déterministe, pour que « au bit près » veuille dire quelque chose.
MemorySampleStore riche(double secondes) {
    const auto n = static_cast<size_t>(secondes * kSr);
    std::vector<float> l(n), r(n);
    uint32_t etat = 12345u;
    for (size_t i = 0; i < n; ++i) {
        etat = etat * 1664525u + 1013904223u;
        const float bruit = (static_cast<float>(etat >> 8) / 16777216.0f - 0.5f) * 0.1f;
        const double t = static_cast<double>(i) / kSr;
        l[i] = 0.3f * static_cast<float>(std::sin(2.0 * M_PI * 220.0 * t)) + 0.2f * static_cast<float>(std::sin(2.0 * M_PI * 1234.5 * t)) + bruit;
        r[i] = 0.25f * static_cast<float>(std::sin(2.0 * M_PI * 330.0 * t)) - bruit;
    }
    return MemorySampleStore(std::move(l), std::move(r));
}
/// Seize clics espacés de 250 ms, sur fond de silence.
MemorySampleStore clics(int nombre, double espacementSecondes) {
    const auto n = static_cast<size_t>((nombre + 1) * espacementSecondes * kSr);
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    for (int c = 0; c < nombre; ++c) {
        const auto p = static_cast<size_t>((c + 0.5) * espacementSecondes * kSr);
        for (size_t i = 0; i < 24 && p + i < n; ++i) {
            const float v = 0.8f * static_cast<float>(std::exp(-static_cast<double>(i) / 6.0));
            l[p + i] = v; r[p + i] = v;
        }
    }
    return MemorySampleStore(std::move(l), std::move(r));
}

struct Rendu { std::vector<float> l, r; };
Rendu rendre(Stretch& s, const MemorySampleStore& src, int64_t trames, int bloc, int64_t depart = 0) {
    Rendu out;
    out.l.assign(static_cast<size_t>(trames), 0.0f);
    out.r.assign(static_cast<size_t>(trames), 0.0f);
    for (int64_t pos = 0; pos < trames; pos += bloc) {
        const int n = static_cast<int>(std::min<int64_t>(bloc, trames - pos));
        s.render(src, depart + pos, n, out.l.data() + pos, out.r.data() + pos);
    }
    return out;
}
double rmsOf(const std::vector<float>& x, size_t from, size_t count) {
    double s = 0.0; size_t n = 0;
    for (size_t i = from; i < from + count && i < x.size(); ++i) { s += x[i] * x[i]; ++n; }
    return n ? std::sqrt(s / static_cast<double>(n)) : 0.0;
}
double magnitudeAt(const std::vector<float>& x, size_t from, size_t count, double hz) {
    double re = 0.0, im = 0.0, norm = 0.0;
    for (size_t i = 0; i < count && from + i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(count));
        const double ph = 2.0 * M_PI * hz * static_cast<double>(i) / kSr;
        re += w * static_cast<double>(x[from + i]) * std::cos(ph);
        im += w * static_cast<double>(x[from + i]) * std::sin(ph);
        norm += w;
    }
    return std::sqrt(re * re + im * im) / std::max(1.0, norm);
}
/// La fréquence du pic, au dixième de hertz, entre lo et hi.
double picHz(const std::vector<float>& x, size_t from, size_t count, double lo, double hi) {
    double meilleur = lo, m = -1.0;
    for (double f = lo; f <= hi; f += 0.1) {
        const double v = magnitudeAt(x, from, count, f);
        if (v > m) { m = v; meilleur = f; }
    }
    return meilleur;
}
/// Les attaques : les instants où l'énergie sur 1 ms dépasse dix fois celle
/// des 10 ms précédentes, avec 50 ms de repos entre deux.
std::vector<int64_t> attaques(const std::vector<float>& x) {
    std::vector<int64_t> out;
    const size_t court = 48, longue = 480;
    int64_t dernier = -100000;
    for (size_t i = longue; i + court < x.size(); ++i) {
        const double e = rmsOf(x, i, court), avant = rmsOf(x, i - longue, longue);
        if (e > 0.02 && e > 10.0 * avant && static_cast<int64_t>(i) - dernier > 2400) {
            out.push_back(static_cast<int64_t>(i));
            dernier = static_cast<int64_t>(i);
        }
    }
    return out;
}

} // namespace

/// BANC 3 (première moitié) : rapport 1,0 = l'original au bit près, et le
/// court-circuit est bien pris.
VSM_TEST(time_stretch_ratio_one_is_the_original_bit_for_bit) {
    auto src = riche(1.0);
    Stretch s;
    s.prepare(512);
    s.setRatio(0, 0.0, 1.0);
    VSM_ASSERT(s.bypassed());
    auto out = rendre(s, src, 48000, 512);
    for (int64_t i = 0; i < 48000; ++i) {
        float g = 0.0f, d = 0.0f;
        src.frameAt(i, g, d);
        VSM_ASSERT(out.l[static_cast<size_t>(i)] == g);
        VSM_ASSERT(out.r[static_cast<size_t>(i)] == d);
    }
    // Un décalage entier reste un court-circuit ; un rapport 1,0000001, non.
    s.setRatio(1000, 3.0, 1.0);
    VSM_ASSERT(s.bypassed());
    s.setRatio(0, 0.0, 1.0000001);
    VSM_ASSERT(!s.bypassed());
}

/// BANC 1 : la hauteur ne bouge pas. Un la3 tenu étiré ×0,75 et ×1,5 : le pic
/// reste à 220 Hz à 5 cents près (0,64 Hz).
VSM_TEST(time_stretch_keeps_the_pitch_within_five_cents) {
    auto src = sinus(220.0, 3.0);
    for (double ratio : {0.75, 1.5}) {
        Stretch s;
        s.prepare(512);
        s.setRatio(0, 0.0, ratio);
        const auto trames = static_cast<int64_t>(std::llround(3.0 * kSr * ratio));
        auto out = rendre(s, src, trames, 512);
        // Deux secondes au milieu.
        const size_t debut = static_cast<size_t>(trames / 2) - 48000;
        const double pic = picHz(out.l, debut, 96000, 215.0, 225.0);
        const double cents = 1200.0 * std::log2(pic / 220.0);
        std::printf("    [banc étirement] ×%.2f : pic à %.1f Hz (%.1f cents)\n", ratio, pic, cents);
        VSM_ASSERT(std::abs(cents) <= 5.0);
    }
}

/// BANC 2 : la durée est exacte. Une source de N trames étirée au rapport r
/// sonne jusqu'à round(N·r) et plus après : la dernière fenêtre a du niveau,
/// la suivante n'en a plus.
VSM_TEST(time_stretch_plays_exactly_to_the_scaled_end) {
    auto src = sinus(220.0, 1.0);
    for (double ratio : {0.66, 1.5}) {
        Stretch s;
        s.prepare(1024);
        s.setRatio(0, 0.0, ratio);
        const auto fin = static_cast<int64_t>(std::llround(48000.0 * ratio));
        auto out = rendre(s, src, fin + 8192, 1024);
        const double milieu = rmsOf(out.l, static_cast<size_t>(fin / 2), 2048);
        const double dernier = rmsOf(out.l, static_cast<size_t>(fin - 2048), 1024);
        const double apres = rmsOf(out.l, static_cast<size_t>(fin + 3072), 4096);
        std::printf("    [banc étirement] ×%.2f : rms milieu %.4f, avant la fin %.4f, après la fin %.6f\n",
                    ratio, milieu, dernier, apres);
        VSM_ASSERT(dernier > milieu * 0.5);
        VSM_ASSERT(apres < milieu * 0.01);
    }
}

/// BANC 6 ET LA CONDITION DE D2.6 : déterministe, et indépendant de la taille
/// des blocs — 256 et 4 096 donnent les mêmes échantillons.
VSM_TEST(time_stretch_is_deterministic_and_block_size_independent) {
    auto src = riche(2.0);
    auto rendreEn = [&](int bloc) {
        Stretch s;
        s.prepare(4096);
        s.setRatio(0, 0.0, 1.31);
        return rendre(s, src, 120000, bloc);
    };
    auto a = rendreEn(256), b = rendreEn(4096), c = rendreEn(256);
    VSM_ASSERT(a.l == c.l && a.r == c.r);
    VSM_ASSERT(a.l == b.l && a.r == b.r);
}

/// BANC 5 : un son tenu ne flotte pas trop. la3 étiré ×1,5 : l'enveloppe
/// (rms par 100 ms) sur la seconde du milieu varie de moins de 10 %.
VSM_TEST(time_stretch_sustained_tone_flutters_less_than_ten_percent) {
    auto src = sinus(220.0, 3.0);
    Stretch s;
    s.prepare(512);
    s.setRatio(0, 0.0, 1.5);
    auto out = rendre(s, src, 216000, 512);
    double lo = 1e9, hi = 0.0;
    for (size_t debut = 84000; debut < 84000 + 48000; debut += 4800) {
        const double e = rmsOf(out.l, debut, 4800);
        lo = std::min(lo, e); hi = std::max(hi, e);
    }
    std::printf("    [banc étirement] flottement ×1,5 : rms min %.4f, max %.4f (%.1f %%)\n", lo, hi, 100.0 * (1.0 - lo / hi));
    VSM_ASSERT(1.0 - lo / hi <= 0.10);
}

/// BANC 4 (D12.3) : les transitoires ne se doublent ni ne se perdent. Seize
/// clics à 250 ms étirés ×1,5 et ×0,66 : seize attaques en sortie, chacune à
/// ≤ 1 ms de sa position théorique — quand les transitoires sont déclarés.
VSM_TEST(time_stretch_locks_transients_once_and_in_place) {
    auto src = clics(16, 0.25);
    std::vector<int64_t> positions;
    for (int c = 0; c < 16; ++c) positions.push_back(static_cast<int64_t>((c + 0.5) * 0.25 * kSr));
    for (double ratio : {1.5, 0.66}) {
        Stretch s;
        s.prepare(512);
        s.setRatio(0, 0.0, ratio);
        s.setTransients(positions.data(), static_cast<int>(positions.size()));
        const auto trames = static_cast<int64_t>(17 * 0.25 * kSr * ratio);
        auto out = rendre(s, src, trames, 512);
        auto trouvees = attaques(out.l);
        double pire = 0.0;
        std::printf("    [banc étirement] écarts (ms) :");
        for (size_t i = 0; i < trouvees.size() && i < positions.size(); ++i) {
            const double ecart = (static_cast<double>(trouvees[i]) - static_cast<double>(positions[i]) * ratio) / kSr * 1000.0;
            std::printf(" %.2f", ecart);
            pire = std::max(pire, std::abs(ecart));
        }
        std::printf("\n");
        std::printf("    [banc étirement] transitoires ×%.2f : %zu attaques (16 attendues), écart max %.2f ms\n",
                    ratio, trouvees.size(), pire);
        VSM_ASSERT_EQ(trouvees.size(), size_t{16});
        VSM_ASSERT(pire <= 1.0);
    }
}

/// Un saut de transport recale la chaîne, et le dit : après `seek`, le rendu
/// depuis la nouvelle position est celui d'un départ à froid — identique à
/// un rendu qui aurait commencé là.
VSM_TEST(time_stretch_seek_restarts_the_chain_deterministically) {
    auto src = riche(2.0);
    Stretch a, b;
    a.prepare(512); b.prepare(512);
    a.setRatio(0, 0.0, 1.2); b.setRatio(0, 0.0, 1.2);
    rendre(a, src, 30000, 512);          // a a déjà joué
    a.seek(60000);
    auto ra = rendre(a, src, 20000, 512, 60000);
    b.seek(60000);
    auto rb = rendre(b, src, 20000, 512, 60000);
    VSM_ASSERT(ra.l == rb.l && ra.r == rb.r);
}

/// D12.3, LE DÉTECTEUR : seize clics détectés à ≤ 1 ms, aucun sur un sinus
/// tenu, et le banc 4 tient avec les transitoires DÉTECTÉS, pas déclarés.
VSM_TEST(transient_detector_finds_clicks_and_ignores_a_held_tone) {
    using vsm::audio::dsp::TransientDetector;
    auto src = clics(16, 0.25);
    auto trouves = TransientDetector::detect(src, 0, src.frames());
    double pire = 0.0;
    for (size_t i = 0; i < trouves.size() && i < 16; ++i) {
        const double attendu = (static_cast<double>(i) + 0.5) * 0.25 * kSr;
        pire = std::max(pire, std::abs(static_cast<double>(trouves[i]) - attendu) / kSr * 1000.0);
    }
    std::printf("    [banc étirement] détecteur : %zu clics trouvés sur 16, écart max %.2f ms\n", trouves.size(), pire);
    VSM_ASSERT_EQ(trouves.size(), size_t{16});
    VSM_ASSERT(pire <= 1.0);
    auto tenu = sinus(220.0, 3.0);
    auto rien = TransientDetector::detect(tenu, 0, tenu.frames());
    std::printf("    [banc étirement] détecteur : %zu transitoire(s) sur un la3 tenu (0 attendu)\n", rien.size());
    VSM_ASSERT_EQ(rien.size(), size_t{0});

    // Et le verrouillage avec ce qu'il a trouvé.
    Stretch s;
    s.prepare(512);
    s.setRatio(0, 0.0, 1.5);
    s.setTransients(trouves.data(), static_cast<int>(trouves.size()));
    auto out = rendre(s, src, static_cast<int64_t>(17 * 0.25 * kSr * 1.5), 512);
    auto attaquesSortie = attaques(out.l);
    double pireSortie = 0.0;
    for (size_t i = 0; i < attaquesSortie.size() && i < 16; ++i) {
        const double attendu = (static_cast<double>(i) + 0.5) * 0.25 * kSr * 1.5;
        pireSortie = std::max(pireSortie, std::abs(static_cast<double>(attaquesSortie[i]) - attendu) / kSr * 1000.0);
    }
    std::printf("    [banc étirement] verrou sur transitoires détectés ×1,5 : %zu attaques, écart max %.2f ms\n",
                attaquesSortie.size(), pireSortie);
    VSM_ASSERT_EQ(attaquesSortie.size(), size_t{16});
    VSM_ASSERT(pireSortie <= 1.0);
}
