#include "TestFramework.h"
#include "vsm/audio/effect/EffectFactory.h"
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
