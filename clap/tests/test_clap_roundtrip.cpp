#include "TestFramework.h"
#include "ClapPluginHost.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/interchange/ClapParameterIds.h"
#include "vsm/interchange/SynthPreset.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

// L'adaptateur (P5) et l'hôte (P6) se valident MUTUELLEMENT : l'hôte charge le
// `.clap` que ce même dépôt vient de construire. Aucun plugin tiers n'est
// nécessaire pour prouver que les deux moitiés fonctionnent -- et le jour où
// un plugin tiers pose problème, on saura que le défaut vient de lui, puisque
// ce circuit fermé est vert.

using namespace vsm::clap;
using vsm::audio::plugin::MidiNoteEvent;

namespace {

const char* adapterPath() { return VSM_CLAP_ADAPTER_PATH; }

MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}

float peakOf(const std::vector<float>& buffer) {
    float peak = 0.0f;
    for (float sample : buffer) peak = std::max(peak, std::abs(sample));
    return peak;
}

vsm::audio::plugin::ParamId paramIdBySemantic(const std::string& machineId, const std::string& semanticId) {
    return vsm::interchange::clapParameterId(semanticId);
    (void)machineId;
}

} // namespace

VSM_TEST(adapter_exposes_every_machine) {
    // « Every machine » se vérifie contre LE REGISTRE, pas contre un nombre.
    // La première version de ce test se contentait de « >= 11 » : huit
    // machines ont été ajoutées au DAW sans jamais apparaître dans les hôtes
    // CLAP, et le test est resté vert pendant tout ce temps. Un seuil ne
    // prouve pas une énumération ; une comparaison ensembliste, si.
    std::string error;
    const auto plugins = scanClapFile(adapterPath(), error);
    VSM_ASSERT(error.empty());

    std::set<std::string> exposed;
    for (const auto& info : plugins) {
        VSM_ASSERT(!info.id.empty());
        VSM_ASSERT(!info.name.empty());
        VSM_ASSERT_EQ(info.vendor, std::string("VSM Studio"));
        exposed.insert(info.id);
    }

    vsm::audio::plugin::registerBuiltInPlugins();
    size_t expected = 0;
    for (const auto& [machineId, displayName] : vsm::audio::plugin::PluginRegistry::instance().listAvailable()) {
        if (machineId == "vsm.testtone") continue; // outil de test, volontairement absent
        ++expected;
        VSM_ASSERT(exposed.count(vsm::interchange::clapPluginId(machineId)) == 1);
    }
    VSM_ASSERT_EQ(exposed.size(), expected);
    VSM_ASSERT(exposed.count("com.vsmstudio.testtone") == 0);
}

VSM_TEST(host_loads_a_plugin_and_sees_its_parameters) {
    std::string error;
    auto instrument = createClapInstrument(adapterPath(), "com.vsmstudio.minimoog", error);
    VSM_ASSERT(instrument != nullptr);
    VSM_ASSERT(error.empty());
    VSM_ASSERT(instrument->parameterList().size() > 10);

    bool foundCutoff = false;
    for (const auto& info : instrument->parameterList())
        if (info.name == "Filter Cutoff") {
            foundCutoff = true;
            VSM_ASSERT(info.maxValue > info.minValue);
            // L'identifiant vu par l'hôte est bien le clap_id sémantique et
            // stable, pas un numéro d'ordre.
            VSM_ASSERT_EQ(info.id, vsm::interchange::clapParameterId("filter.1.cutoff"));
        }
    VSM_ASSERT(foundCutoff);
}

VSM_TEST(a_clap_plugin_produces_audio_through_the_host) {
    std::string error;
    auto instrument = createClapInstrument(adapterPath(), "com.vsmstudio.minimoog", error);
    VSM_ASSERT(instrument != nullptr);
    instrument->initialize(48000.0, 512);

    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    instrument->process(nullptr, 0, left.data(), right.data(), 512);
    VSM_ASSERT_NEAR(peakOf(left), 0.0, 1e-9); // silence sans note

    const MidiNoteEvent event = noteOn(0, 57, 110);
    for (int block = 0; block < 4; ++block)
        instrument->process(block == 0 ? &event : nullptr, block == 0 ? 1 : 0,
                             left.data(), right.data(), 512);
    VSM_ASSERT(peakOf(left) > 0.01f);
    for (float sample : left) VSM_ASSERT(std::isfinite(sample));
}

VSM_TEST(parameters_set_through_clap_change_the_sound) {
    // Le test qui prouve que le chemin de paramètres traverse vraiment la
    // frontière CLAP, au lieu d'être accepté puis ignoré.
    //
    // On mesure le contenu AIGU (énergie de la différence première), pas
    // l'énergie totale : une première version de ce test comparait l'énergie
    // brute et échouait, non par un défaut du code mais parce qu'à résonance
    // 1,5 le pic résonant d'un filtre fermé à 200 Hz apporte PLUS d'énergie
    // qu'un filtre grand ouvert. L'énergie totale ne dit rien de la couleur ;
    // la différence première, si.
    auto render = [](float cutoff) {
        std::string error;
        auto instrument = createClapInstrument(VSM_CLAP_ADAPTER_PATH, "com.vsmstudio.minimoog", error);
        instrument->initialize(48000.0, 512);
        instrument->setParameter(vsm::interchange::clapParameterId("filter.1.cutoff"), cutoff);
        instrument->setParameter(vsm::interchange::clapParameterId("filter.1.resonance"), 1.5f);

        std::vector<float> left(512, 0.0f), right(512, 0.0f), collected;
        const MidiNoteEvent event = noteOn(0, 45, 110);
        for (int block = 0; block < 6; ++block) {
            instrument->process(block == 0 ? &event : nullptr, block == 0 ? 1 : 0,
                                 left.data(), right.data(), 512);
            collected.insert(collected.end(), left.begin(), left.end());
        }
        double highFrequencyEnergy = 0.0;
        for (size_t i = 1; i < collected.size(); ++i) {
            const double difference = static_cast<double>(collected[i]) - collected[i - 1];
            highFrequencyEnergy += difference * difference;
        }
        return std::make_pair(highFrequencyEnergy, collected);
    };

    const auto dark = render(200.0f);
    const auto bright = render(8000.0f);
    VSM_ASSERT(dark.second != bright.second);         // le paramètre a bien traversé CLAP
    VSM_ASSERT(bright.first > dark.first * 2.0);      // et le filtre ouvert sonne bien plus clair
}

VSM_TEST(a_clap_plugin_can_be_read_back_after_being_set) {
    std::string error;
    auto instrument = createClapInstrument(adapterPath(), "com.vsmstudio.tb303", error);
    VSM_ASSERT(instrument != nullptr);
    instrument->initialize(48000.0, 512);

    const auto cutoffId = vsm::interchange::clapParameterId("filter.1.cutoff");
    instrument->setParameter(cutoffId, 900.0f);
    VSM_ASSERT_NEAR(instrument->getParameter(cutoffId), 900.0f, 0.5f);
}

VSM_TEST(a_clap_plugin_plays_inside_the_process_graph_unchanged) {
    // LA propriété qui compte : `ProcessGraph` n'a pas une ligne de code
    // spécifique à CLAP. Un plugin tiers s'y branche comme une machine native,
    // parce qu'il arrive habillé en ISynthPlugin.
    std::string error;
    auto instrument = createClapInstrument(adapterPath(), "com.vsmstudio.juno106", error);
    VSM_ASSERT(instrument != nullptr);

    vsm::sequencer::Project project;
    project.ticksPerQuarterNote = 480;
    vsm::sequencer::Track track;
    uint64_t ids = 1;
    track.addNote(0, 960, 60, 100, 0, ids);
    project.tracks.push_back(track);

    vsm::audio::engine::ProcessGraph graph;
    graph.prepare(48000.0, 256);
    graph.setProject(project);
    graph.setTrackInstrumentInstance(0, instrument);
    graph.setPlaying(true);

    const auto rendered = vsm::audio::engine::OfflineRenderer::render(graph, 48000.0, 256, 1.0);
    float peak = 0.0f;
    for (float sample : rendered.left) peak = std::max(peak, std::abs(sample));
    VSM_ASSERT(peak > 0.01f);
}

VSM_TEST(loading_a_broken_file_fails_cleanly) {
    // Un plugin tiers cassé ne doit jamais faire tomber l'application qui le
    // scanne : erreur explicite, pas de plantage.
    std::string error;
    const auto plugins = scanClapFile("/chemin/qui/nexiste/pas.clap", error);
    VSM_ASSERT(plugins.empty());
    VSM_ASSERT(!error.empty());

    std::string createError;
    auto instrument = createClapInstrument(adapterPath(), "com.inconnu.plugin", createError);
    VSM_ASSERT(instrument == nullptr);
    VSM_ASSERT(createError.find("absent") != std::string::npos);
}

VSM_TEST(clap_and_native_paths_produce_the_same_audio) {
    // La raison d'être de l'adaptateur : il ENVELOPPE la machine native, il ne
    // la réimplémente pas. Le même patch doit donc sonner pareil des deux
    // côtés -- sinon l'utilisateur aurait deux versions du même instrument sans
    // savoir laquelle est la bonne.
    vsm::audio::plugin::registerBuiltInPlugins();
    auto native = vsm::audio::plugin::PluginRegistry::instance().create("vsm.minimoog");
    VSM_ASSERT(native != nullptr);
    native->initialize(48000.0, 512);

    std::string error;
    auto viaClap = createClapInstrument(adapterPath(), "com.vsmstudio.minimoog", error);
    VSM_ASSERT(viaClap != nullptr);
    viaClap->initialize(48000.0, 512);

    const MidiNoteEvent event = noteOn(0, 52, 100);
    std::vector<float> nativeL(512, 0.0f), nativeR(512, 0.0f);
    std::vector<float> clapL(512, 0.0f), clapR(512, 0.0f);
    for (int block = 0; block < 3; ++block) {
        native->process(block == 0 ? &event : nullptr, block == 0 ? 1 : 0,
                         nativeL.data(), nativeR.data(), 512);
        viaClap->process(block == 0 ? &event : nullptr, block == 0 ? 1 : 0,
                          clapL.data(), clapR.data(), 512);
    }
    for (size_t i = 0; i < nativeL.size(); ++i)
        VSM_ASSERT_NEAR(clapL[i], nativeL[i], 1e-6); // même DSP, mêmes échantillons
}

VSM_TEST(a_preset_written_by_the_analysis_chain_loads_as_a_clap_state) {
    // C'EST LA PROMESSE de l'étape 11.4 : le projet que produit la chaîne
    // d'analyse doit être jouable dans n'importe quel hôte CLAP, sans
    // conversion.
    //
    // Elle tient sans format supplémentaire, parce que l'adaptateur écrit son
    // état natif en preset SÉMANTIQUE : le fichier
    // `instruments/track_NN.synth.json` écrit par l'analyse EST déjà un état
    // CLAP valide. Un dossier `states/` qui reprendrait les mêmes octets sous
    // l'extension `.clapstate` ne serait que de la duplication -- et deux
    // copies d'une même vérité finissent toujours par diverger.
    vsm::audio::plugin::registerBuiltInPlugins();

    // Le preset, tel que l'écrit `vsm_project_export.py` : identités
    // sémantiques, unités réelles, fidélité déclarée.
    const std::string presetDeLAnalyse = R"({
        "format": "vsm-synth-preset",
        "version": 1,
        "name": "Basse reconstruite",
        "pluginId": "vsm.tb303",
        "machineName": "TB-303-style Acid Synth",
        "fidelity": "derived",
        "parameters": {
            "filter.1.cutoff": 480.0,
            "filter.1.resonance": 0.9,
            "envelope.1.decay": 0.3
        }
    })";

    std::string error;
    auto viaClap = createClapInstrument(adapterPath(), "com.vsmstudio.tb303", error);
    VSM_ASSERT(viaClap != nullptr);
    viaClap->initialize(48000.0, 512);
    VSM_ASSERT(loadClapState(*viaClap, presetDeLAnalyse, error));

    // La machine native, réglée par la MÊME voie sémantique, doit sonner
    // exactement pareil : sinon un projet reconstruit ne s'entendrait pas de
    // la même façon selon qu'on l'ouvre dans le DAW ou dans un hôte tiers.
    auto native = vsm::audio::plugin::PluginRegistry::instance().create("vsm.tb303");
    VSM_ASSERT(native != nullptr);
    native->initialize(48000.0, 512);
    const auto preset = vsm::interchange::parseSynthPreset(presetDeLAnalyse);
    VSM_ASSERT(preset.success);
    vsm::interchange::applyPreset(preset.preset, *native, "vsm.tb303");

    const MidiNoteEvent event = noteOn(0, 45, 110);
    std::vector<float> nativeL(512, 0.0f), nativeR(512, 0.0f);
    std::vector<float> clapL(512, 0.0f), clapR(512, 0.0f);
    for (int block = 0; block < 4; ++block) {
        native->process(block == 0 ? &event : nullptr, block == 0 ? 1 : 0,
                         nativeL.data(), nativeR.data(), 512);
        viaClap->process(block == 0 ? &event : nullptr, block == 0 ? 1 : 0,
                          clapL.data(), clapR.data(), 512);
    }
    float crete = 0.0f;
    for (size_t i = 0; i < nativeL.size(); ++i) {
        VSM_ASSERT_NEAR(clapL[i], nativeL[i], 1e-6);
        crete = std::max(crete, std::abs(nativeL[i]));
    }
    VSM_ASSERT(crete > 0.01f); // ...et il y a bien du son des deux côtés
}

VSM_TEST(a_clap_state_read_back_is_a_semantic_preset) {
    // Le format de l'état n'est pas un détail interne : c'est ce qui rend un
    // projet d'hôte lisible dans dix ans. On le VÉRIFIE plutôt que de le
    // supposer.
    vsm::audio::plugin::registerBuiltInPlugins();
    std::string error;
    auto viaClap = createClapInstrument(adapterPath(), "com.vsmstudio.juno106", error);
    VSM_ASSERT(viaClap != nullptr);
    viaClap->initialize(48000.0, 512);

    std::string etat;
    VSM_ASSERT(saveClapState(*viaClap, etat, error));
    const auto relu = vsm::interchange::parseSynthPreset(etat);
    VSM_ASSERT(relu.success);
    VSM_ASSERT_EQ(relu.preset.pluginId, std::string("vsm.juno106"));
    VSM_ASSERT(!relu.preset.values.empty());
    // Les identités sont bien SÉMANTIQUES, pas des numéros internes.
    VSM_ASSERT(relu.preset.values.count("filter.1.cutoff") == 1);
}

VSM_TEST(a_state_that_is_not_a_valid_preset_is_refused_not_half_applied) {
    vsm::audio::plugin::registerBuiltInPlugins();
    std::string error;
    auto viaClap = createClapInstrument(adapterPath(), "com.vsmstudio.minimoog", error);
    VSM_ASSERT(viaClap != nullptr);
    viaClap->initialize(48000.0, 512);
    VSM_ASSERT(!loadClapState(*viaClap, "{ ceci n'est pas du JSON", error));
    VSM_ASSERT(!error.empty());
}
