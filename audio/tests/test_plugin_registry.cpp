#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <memory>
#include <string>

using namespace vsm::audio::plugin;

// Ce test vérifie le mécanisme d'AUTO-enregistrement réel : TestToneSynth.cpp
// est compilé directement comme source de vsm_audio (voir audio/CMakeLists.txt
// et le commentaire dans PluginRegistry.h), son registrar statique doit donc
// s'être exécuté avant main() sans qu'aucun code ici ne référence
// TestToneSynth explicitement.
VSM_TEST(registry_contains_testtone_via_auto_registration) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.testtone"));
}

VSM_TEST(registry_create_returns_working_instance) {
    auto plugin = PluginRegistry::instance().create("vsm.testtone");
    VSM_ASSERT(plugin != nullptr);
    VSM_ASSERT(std::string(plugin->machineName()).size() > 0);
}

VSM_TEST(registry_create_unknown_id_returns_null) {
    auto plugin = PluginRegistry::instance().create("does.not.exist");
    VSM_ASSERT(plugin == nullptr);
}

VSM_TEST(registry_list_available_includes_testtone) {
    auto list = PluginRegistry::instance().listAvailable();
    bool found = false;
    for (const auto& [id, name] : list)
        if (id == "vsm.testtone") found = true;
    VSM_ASSERT(found);
}

namespace {
struct DummyPlugin : ISynthPlugin {
    void initialize(double, int) override {}
    void process(const MidiNoteEvent*, int, float* outL, float* outR, int n) override {
        for (int i = 0; i < n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }
    }
    void setParameter(ParamId, float) override {}
    float getParameter(ParamId) const override { return 0.0f; }
    const ParameterList& parameterList() const override {
        static ParameterList empty;
        return empty;
    }
    PresetState saveState() const override { return {}; }
    void loadState(const PresetState&) override {}
    const char* machineName() const override { return "Dummy"; }
    int activeVoiceCount() const override { return 0; }
};
} // namespace

VSM_TEST(registry_manual_registration_and_creation) {
    PluginRegistry::instance().registerPlugin("test.dummy", "Dummy Test Plugin",
                                                [] { return std::make_shared<DummyPlugin>(); });

    VSM_ASSERT(PluginRegistry::instance().isRegistered("test.dummy"));
    auto instance = PluginRegistry::instance().create("test.dummy");
    VSM_ASSERT(instance != nullptr);
    VSM_ASSERT_EQ(std::string(instance->machineName()), std::string("Dummy"));
}
