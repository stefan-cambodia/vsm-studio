#include "TestFramework.h"
#include "vsm/audio/effect/BypassableEffect.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace vsm::audio::effect;

// D15.1 -- CONTOURNER UN INSERT. Ce que « contourné » doit valoir, effet par
// effet, sur les seize de la fabrique : la sortie est le signal SEC, retardé
// d'exactement la latence que l'effet déclare -- ni plus, ni moins, ni
// coloré -- et la latence déclarée ne bouge pas d'un échantillon entre les
// deux états. C'est ce qui garantit que la compensation de latence de la
// piste ne se déplace pas quand on compare avec / sans.

namespace {
std::vector<float> signal(size_t n) {
    std::vector<float> s(n);
    for (size_t i = 0; i < n; ++i)
        s[i] = 0.4f * std::sin(2.0f * 3.14159265f * 311.13f * static_cast<float>(i) / 48000.0f)
             + (i % 977 == 0 ? 0.5f : 0.0f);
    return s;
}
}

VSM_TEST(every_bypassed_effect_returns_the_dry_signal_delayed_by_its_latency) {
    const int bloc = 256;
    const size_t total = 48128;   // un multiple du bloc : le dernier bloc ne déborde pas
    for (const auto& info : EffectFactory::available()) {
        auto brut = EffectFactory::create(info.id);
        VSM_ASSERT(brut != nullptr);
        BypassableEffect fx(std::move(brut));
        fx.prepare(48000.0, bloc);
        for (const auto& p : fx.parameterList())
            fx.setParameter(p.id, p.minValue + 0.6f * (p.maxValue - p.minValue));
        const int latenceActif = fx.latencySamples();
        fx.setBypassed(true);
        VSM_ASSERT_EQ(fx.latencySamples(), latenceActif);

        const auto sec = signal(total);
        std::vector<float> l = sec, r = sec;
        for (size_t debut = 0; debut < total; debut += static_cast<size_t>(bloc))
            fx.process(l.data() + debut, r.data() + debut, bloc);

        const auto retard = static_cast<size_t>(latenceActif);
        float pire = 0.0f;
        for (size_t i = retard; i < total; ++i)
            pire = std::max(pire, std::max(std::abs(l[i] - sec[i - retard]), std::abs(r[i] - sec[i - retard])));
        for (size_t i = 0; i < retard; ++i) pire = std::max(pire, std::abs(l[i]));
        if (pire > 0.0f)
            std::printf("    %s : latence %d, pire ecart %.3g\n", info.id.c_str(), latenceActif,
                        static_cast<double>(pire));
        VSM_ASSERT(pire == 0.0f);
    }
}

VSM_TEST(un_effet_contourne_puis_remis_reprend_la_ou_il_en_etait) {
    // L'effet a continué de tourner pendant le contournement : le remettre
    // donne exactement ce qu'un effet jamais contourné aurait donné.
    const int bloc = 256;
    const size_t total = 24576;
    for (const auto& info : EffectFactory::available()) {
        auto rendre = [&](bool contournerAuMilieu) {
            BypassableEffect fx(EffectFactory::create(info.id));
            fx.prepare(48000.0, bloc);
            for (const auto& p : fx.parameterList())
                fx.setParameter(p.id, p.minValue + 0.6f * (p.maxValue - p.minValue));
            const auto sec = signal(total);
            std::vector<float> l = sec, r = sec;
            for (size_t debut = 0; debut < total; debut += static_cast<size_t>(bloc)) {
                fx.setBypassed(contournerAuMilieu && debut >= total / 3 && debut < 2 * total / 3);
                fx.process(l.data() + debut, r.data() + debut, bloc);
            }
            return l;
        };
        const auto temoin = rendre(false), essai = rendre(true);
        for (size_t i = 2 * total / 3; i < total; ++i) VSM_ASSERT_EQ(essai[i], temoin[i]);
    }
}

VSM_TEST(the_graph_latency_does_not_move_when_an_insert_is_bypassed) {
    using vsm::audio::engine::ProcessGraph;
    auto latenceAvec = [](bool contourne) {
        ProcessGraph graphe;
        graphe.prepare(48000.0, 512);
        graphe.setTrackInstrument(0, "vsm.testtone");
        auto chaine = std::make_shared<ProcessGraph::EffectChain>();
        int somme = 0;
        for (const auto& info : EffectFactory::available()) {
            auto fx = std::make_shared<BypassableEffect>(EffectFactory::create(info.id));
            fx->prepare(48000.0, 512);
            for (const auto& p : fx->parameterList())
                fx->setParameter(p.id, p.minValue + 0.6f * (p.maxValue - p.minValue));
            fx->setBypassed(contourne);
            somme += fx->latencySamples();
            chaine->push_back(std::move(fx));
        }
        graphe.setTrackEffectChain(0, chaine);
        vsm::sequencer::Project projet;
        projet.ticksPerQuarterNote = 480;
        projet.tracks.emplace_back();   // la compensation ne se calcule que pour les pistes du projet
        graphe.setProject(projet);
        return std::make_pair(graphe.graphLatencySamples(), somme);
    };
    const auto actif = latenceAvec(false), contourne = latenceAvec(true);
    VSM_ASSERT(actif.second > 0);   // au moins un effet a une latence, sinon le test ne teste rien
    VSM_ASSERT_EQ(actif.first, contourne.first);
    VSM_ASSERT_EQ(actif.first, actif.second);
}
