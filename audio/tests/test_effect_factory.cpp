#include "TestFramework.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <cmath>
#include <set>
#include <string>

using namespace vsm::audio::effect;

VSM_TEST(effect_factory_lists_all_effects) {
    const auto& list = EffectFactory::available();
    VSM_ASSERT(list.size() >= 9); // section 16 au complet
    std::set<std::string> ids;
    for (const auto& e : list) {
        VSM_ASSERT(!e.id.empty());
        VSM_ASSERT(!e.displayName.empty());
        ids.insert(e.id);
    }
    VSM_ASSERT_EQ(ids.size(), list.size()); // pas de doublon
    for (const char* expected : {"reverb", "flanger", "phaser", "tape", "filter", "delay"})
        VSM_ASSERT(ids.count(expected) == 1);
}

VSM_TEST(effect_factory_creates_valid_instances) {
    for (const auto& e : EffectFactory::available()) {
        auto fx = EffectFactory::create(e.id);
        VSM_ASSERT(fx != nullptr);
        fx->prepare(48000.0, 512);
        VSM_ASSERT(!fx->parameterList().empty()); // chaque effet expose des params
        // process passthrough basique : ne doit pas crasher / produire du NaN
        float l[64] = {0}, r[64] = {0};
        for (int i = 0; i < 64; ++i) l[i] = r[i] = 0.1f;
        fx->process(l, r, 64);
        for (int i = 0; i < 64; ++i) { VSM_ASSERT(std::isfinite(l[i])); VSM_ASSERT(std::isfinite(r[i])); }
    }
}

VSM_TEST(effect_factory_unknown_id_returns_null) {
    VSM_ASSERT(EffectFactory::create("does_not_exist") == nullptr);
}

// D473 : UN SÉLECTEUR DÉCLARE UNE POSITION PAR VALEUR ENTIÈRE, ni plus ni moins.
// Le panneau d'effets en tire un pas entier et le libellé de la position ; une
// liste plus courte que la course laisserait une position sans nom, une bornée
// sur 0,5 n'aurait pas de position du tout.
VSM_TEST(effect_selectors_declare_one_label_per_integer_position) {
    int selecteurs = 0;
    for (const auto& e : EffectFactory::available()) {
        auto fx = EffectFactory::create(e.id);
        for (const auto& p : fx->parameterList()) {
            if (p.choices.empty()) continue;
            ++selecteurs;
            VSM_ASSERT(std::floor(p.minValue) == p.minValue);
            VSM_ASSERT(std::floor(p.maxValue) == p.maxValue);
            VSM_ASSERT_EQ(p.choices.size(), static_cast<size_t>(p.maxValue - p.minValue) + 1);
            for (const auto& libelle : p.choices) VSM_ASSERT(!libelle.empty());
        }
    }
    // Distortion Mode, Filter Mode, Delay Ping-Pong, Compressor Sidechain Bus.
    VSM_ASSERT_EQ(selecteurs, 4);
}

// D475 : « % » VEUT DIRE UNE PART DE −1 À 1, ÉCRITE × 100. Un paramètre qui la
// déclarerait sur 0..100 s'écrirait « 5000 % » : la règle vaut pour tout le
// parc, effets ET machines, et c'est ici qu'elle se vérifie.
VSM_TEST(percent_unit_means_a_part_between_minus_one_and_one) {
    int parts = 0;
    auto verifier = [&parts](const vsm::audio::plugin::ParameterList& liste) {
        for (const auto& p : liste) {
            if (p.unit != "%") continue;
            ++parts;
            VSM_ASSERT(p.minValue >= -1.0f);
            VSM_ASSERT(p.maxValue <= 1.0f);
        }
    };
    for (const auto& e : EffectFactory::available()) verifier(EffectFactory::create(e.id)->parameterList());
    for (const auto& [id, nom] : vsm::audio::plugin::PluginRegistry::instance().listAvailable()) {
        auto machine = vsm::audio::plugin::PluginRegistry::instance().create(id);
        if (machine) verifier(machine->parameterList());
    }
    VSM_ASSERT(parts >= 24);   // les vingt-quatre parts d'effet de D475
}
