#include "TestFramework.h"
#include "vsm/audio/effect/ChannelStrip.h"
#include "vsm/audio/effect/EffectFactory.h"
#include <cmath>
#include <vector>

using namespace vsm::audio::effect;

// D4.1 de docs/ROADMAP-daw.md — LA TRANCHE, ENFIN PAR PISTE.
//
// Le DSP existait, entier et testé, et n'était accessible QUE sur le master.
// Une console dont on ne peut pas égaliser une piste n'est pas une console. Ce
// qui suit vérifie que les quatre effets FONT ce que leur nom promet -- car
// c'est là qu'un habillage se trompe : brancher le mauvais paramètre sur le
// bon DSP ne se voit pas à la compilation.

namespace {

/// Une sinusoïde stéréo d'amplitude et de fréquence données.
void remplirSinus(std::vector<float>& g, std::vector<float>& d, double freq, double sr, float ampl) {
    for (size_t i = 0; i < g.size(); ++i) {
        const float e = static_cast<float>(std::sin(2.0 * M_PI * freq * static_cast<double>(i) / sr)) * ampl;
        g[i] = e;
        d[i] = e;
    }
}

float crete(const std::vector<float>& v, size_t depuis = 0) {
    float m = 0.0f;
    for (size_t i = depuis; i < v.size(); ++i) m = std::max(m, std::abs(v[i]));
    return m;
}

/// Amplitude d'une sinusoïde après traitement, mesurée sur la seconde moitié
/// pour laisser passer le régime transitoire des filtres.
float amplitudeApres(IAudioEffect& effet, double freq, double sr, float ampl) {
    const int n = static_cast<int>(sr * 0.5);
    std::vector<float> g(static_cast<size_t>(n)), d(static_cast<size_t>(n));
    remplirSinus(g, d, freq, sr, ampl);
    effet.prepare(sr, n);
    effet.process(g.data(), d.data(), n);
    return crete(g, g.size() / 2);
}

} // namespace

VSM_TEST(the_four_channel_strip_effects_are_in_the_factory) {
    // Le critère de l'étape, littéralement : quatre effets de plus.
    const auto& liste = EffectFactory::available();
    for (const char* id : {"eq", "compressor", "gate", "limiter"}) {
        bool trouve = false;
        for (const auto& e : liste) if (e.id == id) trouve = true;
        VSM_ASSERT(trouve);
        auto fx = EffectFactory::create(id);
        VSM_ASSERT(fx != nullptr);
        VSM_ASSERT(!fx->parameterList().empty());
    }
    VSM_ASSERT(liste.size() >= 13);
}

VSM_TEST(the_equaliser_lifts_the_band_it_is_told_to_and_leaves_the_others) {
    // Le piège d'un habillage : brancher le gain des graves sur la fréquence
    // des aigus compile parfaitement et ne s'entend qu'à l'usage.
    constexpr double sr = 48000.0;
    EqualiserEffect eq;
    eq.setParameter(EqualiserEffect::kLowGainDb, 12.0f);
    eq.setParameter(EqualiserEffect::kLowFreq, 120.0f);

    const float grave = amplitudeApres(eq, 60.0, sr, 0.2f);
    EqualiserEffect eq2;
    eq2.setParameter(EqualiserEffect::kLowGainDb, 12.0f);
    eq2.setParameter(EqualiserEffect::kLowFreq, 120.0f);
    const float aigu = amplitudeApres(eq2, 6000.0, sr, 0.2f);

    VSM_ASSERT(grave > 0.2f * 2.0f);        // +12 dB, soit environ x4 au fond
    VSM_ASSERT_NEAR(aigu, 0.2f, 0.02f);     // les aigus n'ont pas bougé
}

VSM_TEST(the_equaliser_at_zero_gain_leaves_the_signal_alone) {
    // Un effet neutre doit être NEUTRE : c'est ce qui autorise à l'insérer
    // partout sans se demander ce qu'il coûte.
    constexpr double sr = 48000.0;
    EqualiserEffect eq;
    const float sortie = amplitudeApres(eq, 1000.0, sr, 0.3f);
    VSM_ASSERT_NEAR(sortie, 0.3f, 0.002f);
}

VSM_TEST(the_compressor_reduces_what_is_above_the_threshold_and_says_by_how_much) {
    constexpr double sr = 48000.0;
    CompressorEffect comp;
    comp.setParameter(CompressorEffect::kThresholdDb, -20.0f);
    comp.setParameter(CompressorEffect::kRatio, 8.0f);
    comp.setParameter(CompressorEffect::kAttackMs, 1.0f);

    const float fort = amplitudeApres(comp, 220.0, sr, 0.8f);
    VSM_ASSERT(fort < 0.5f);                       // franchement réduit
    // LA RÉDUCTION EST PUBLIÉE : un compresseur qu'on ne voit pas travailler se
    // règle au hasard.
    VSM_ASSERT(comp.gainReduction() < 0.5f);

    CompressorEffect calme;
    calme.setParameter(CompressorEffect::kThresholdDb, -6.0f);
    const float faible = amplitudeApres(calme, 220.0, sr, 0.05f);
    VSM_ASSERT_NEAR(faible, 0.05f, 0.005f);        // sous le seuil : intact
    VSM_ASSERT_NEAR(calme.gainReduction(), 1.0f, 1e-3f);
}

VSM_TEST(the_gate_closes_on_silence_and_opens_on_signal) {
    constexpr double sr = 48000.0;
    const int n = static_cast<int>(sr);
    std::vector<float> g(static_cast<size_t>(n), 0.0f), d(static_cast<size_t>(n), 0.0f);
    // Une demi-seconde de souffle très faible, puis une demi-seconde de note.
    for (int i = 0; i < n / 2; ++i) {
        const float e = static_cast<float>(std::sin(2.0 * M_PI * 300.0 * i / sr)) * 0.002f;
        g[static_cast<size_t>(i)] = d[static_cast<size_t>(i)] = e;
    }
    for (int i = n / 2; i < n; ++i) {
        const float e = static_cast<float>(std::sin(2.0 * M_PI * 300.0 * i / sr)) * 0.5f;
        g[static_cast<size_t>(i)] = d[static_cast<size_t>(i)] = e;
    }

    GateEffect porte;
    porte.setParameter(GateEffect::kThresholdDb, -30.0f);
    porte.setParameter(GateEffect::kRangeDb, -80.0f);
    // RELÂCHEMENT COURT, ET C'EST DÉLIBÉRÉ DANS CE TEST. Le relâchement est une
    // CONSTANTE DE TEMPS, pas un délai de fermeture : il en faut trois ou
    // quatre pour arriver au plancher (voir `dsp::NoiseGate`). Avec les 150 ms
    // par défaut, la porte serait encore à un quart de son gain à mi-parcours
    // du souffle, et le test mesurerait une fermeture en cours plutôt qu'une
    // fermeture.
    porte.setParameter(GateEffect::kReleaseMs, 20.0f);
    porte.prepare(sr, n);
    porte.process(g.data(), d.data(), n);

    float souffle = 0.0f, note = 0.0f;
    for (int i = n / 4; i < n / 2; ++i) souffle = std::max(souffle, std::abs(g[static_cast<size_t>(i)]));
    for (int i = 3 * n / 4; i < n; ++i) note = std::max(note, std::abs(g[static_cast<size_t>(i)]));
    VSM_ASSERT(souffle < 1.0e-4f);   // le souffle est tu
    VSM_ASSERT(note > 0.4f);         // la note passe
}

VSM_TEST(the_gate_hold_prevents_chattering_on_a_wavering_note) {
    // LE PIÈGE DE LA PORTE : sans maintien, une note dont l'amplitude ondule
    // autour du seuil fait battre la porte, et le hachage s'entend bien plus
    // que le souffle qu'on voulait retirer.
    constexpr double sr = 48000.0;
    const int n = static_cast<int>(sr * 0.5);
    std::vector<float> g(static_cast<size_t>(n)), d(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sr;
        // Porteuse à 300 Hz dont l'enveloppe oscille à 20 Hz de part et d'autre
        // du seuil : le cas exact qui fait battre une porte sans maintien.
        const double enveloppe = 0.03 + 0.028 * std::sin(2.0 * M_PI * 20.0 * t);
        const float e = static_cast<float>(std::sin(2.0 * M_PI * 300.0 * t) * enveloppe);
        g[static_cast<size_t>(i)] = d[static_cast<size_t>(i)] = e;
    }

    auto compterFermetures = [&](float holdMs) {
        std::vector<float> a = g, b = d;
        GateEffect porte;
        porte.setParameter(GateEffect::kThresholdDb, -30.0f);   // ~0.032
        porte.setParameter(GateEffect::kHoldMs, holdMs);
        porte.setParameter(GateEffect::kReleaseMs, 5.0f);
        porte.setParameter(GateEffect::kRangeDb, -60.0f);
        porte.prepare(sr, n);
        // Bloc par bloc, pour lire l'ouverture au fil du temps.
        int fermetures = 0;
        bool ouverte = true;
        constexpr int bloc = 128;
        for (int i = 0; i + bloc <= n; i += bloc) {
            porte.process(a.data() + i, b.data() + i, bloc);
            const bool maintenant = porte.gateOpening() > 0.5f;
            if (ouverte && !maintenant) ++fermetures;
            ouverte = maintenant;
        }
        return fermetures;
    };

    const int sansMaintien = compterFermetures(0.0f);
    const int avecMaintien = compterFermetures(200.0f);
    VSM_ASSERT(sansMaintien > 2);              // elle bat
    VSM_ASSERT(avecMaintien < sansMaintien);   // le maintien la calme
}

VSM_TEST(the_limiter_never_lets_the_ceiling_through) {
    // La garantie est stricte, et c'est tout l'intérêt d'un limiteur : un
    // plafond qu'on dépasse « un peu » n'est pas un plafond.
    constexpr double sr = 48000.0;
    const int n = static_cast<int>(sr * 0.2);
    std::vector<float> g(static_cast<size_t>(n)), d(static_cast<size_t>(n));
    remplirSinus(g, d, 200.0, sr, 4.0f);   // très largement saturé

    LimiterEffect lim;
    lim.setParameter(LimiterEffect::kCeilingDb, -6.0f);   // 0.501
    lim.prepare(sr, n);
    lim.process(g.data(), d.data(), n);

    const float plafond = std::pow(10.0f, -6.0f / 20.0f);
    for (int i = 0; i < n; ++i) {
        VSM_ASSERT(std::isfinite(g[static_cast<size_t>(i)]));
        VSM_ASSERT(std::abs(g[static_cast<size_t>(i)]) <= plafond + 1.0e-4f);
    }
}

VSM_TEST(none_of_the_four_produces_anything_but_finite_numbers) {
    // Le garde-fou commun : un NaN dans une chaîne d'inserts contamine tout le
    // mixage jusqu'au master, et ne se voit qu'au silence total.
    constexpr double sr = 48000.0;
    for (const char* id : {"eq", "compressor", "gate", "limiter"}) {
        auto fx = EffectFactory::create(id);
        VSM_ASSERT(fx != nullptr);
        fx->prepare(sr, 256);
        for (const auto& p : fx->parameterList()) fx->setParameter(p.id, p.maxValue);
        std::vector<float> g(256, 0.0f), d(256, 0.0f);
        remplirSinus(g, d, 1000.0, sr, 0.9f);
        for (int passe = 0; passe < 8; ++passe) fx->process(g.data(), d.data(), 256);
        for (int i = 0; i < 256; ++i) {
            VSM_ASSERT(std::isfinite(g[static_cast<size_t>(i)]));
            VSM_ASSERT(std::isfinite(d[static_cast<size_t>(i)]));
        }
    }
}
