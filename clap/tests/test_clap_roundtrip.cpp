#include "TestFramework.h"
#include "ClapPluginHost.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/effect/EffectFactory.h"
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

// --- D7.1 : l'hôte est branché, un plugin tiers se charge sur une piste -----
//
// L'hôte existait, testé, et n'était appelé de nulle part. Le brancher veut
// dire une chose précise et vérifiable : qu'un identifiant `clap:` demandé au
// REGISTRE DE MACHINES rende un instrument. Tout le reste du projet -- le
// graphe, le format de projet, le rendu hors ligne -- ne parle qu'à ce
// registre, et n'a donc pas une ligne à changer.

VSM_TEST(an_instrument_id_survives_a_round_trip_through_its_text_form) {
    const std::string id = clapInstrumentId("/opt/plug ins/ma#chine.clap", "com.exemple.synthe");
    std::string chemin, plugin;
    VSM_ASSERT(parseClapInstrumentId(id, chemin, plugin));
    // Le séparateur se cherche PAR LA FIN : un chemin peut contenir un « # ».
    VSM_ASSERT_EQ(chemin, std::string("/opt/plug ins/ma#chine.clap"));
    VSM_ASSERT_EQ(plugin, std::string("com.exemple.synthe"));
}

VSM_TEST(a_native_machine_id_is_not_mistaken_for_a_clap_one) {
    std::string chemin, plugin;
    VSM_ASSERT(!parseClapInstrumentId("vsm.tb303", chemin, plugin));
    VSM_ASSERT(!parseClapInstrumentId("", chemin, plugin));
}

VSM_TEST(the_registry_loads_a_clap_file_once_the_resolver_is_installed) {
    installClapResolver();
    auto& registre = vsm::audio::plugin::PluginRegistry::instance();

    std::string erreur;
    const auto trouves = scanClapFile(adapterPath(), erreur);
    VSM_ASSERT(!trouves.empty());

    const std::string id = clapInstrumentId(adapterPath(), trouves[0].id);
    // LE REGISTRE RÉPOND FAUX À `isRegistered` -- savoir si un plugin tiers est
    // là demande d'ouvrir un fichier, ce que cette question ne doit pas faire.
    VSM_ASSERT(!registre.isRegistered(id));

    const auto instrument = registre.create(id);
    VSM_ASSERT(instrument != nullptr);
    VSM_ASSERT(instrument->machineName() != nullptr);

    // ET IL JOUE : charger n'est pas sonner, et c'est sonner qui est demandé.
    instrument->initialize(48000.0, 256);
    std::vector<float> gauche(256, 0.0f), droite(256, 0.0f);
    const MidiNoteEvent depart = noteOn(0, 60, 100);
    instrument->process(&depart, 1, gauche.data(), droite.data(), 256);
    float crete = 0.0f;
    for (int i = 0; i < 256; ++i) crete = std::max(crete, std::abs(gauche[i]));
    for (int passe = 0; passe < 8 && crete <= 0.0f; ++passe) {
        instrument->process(nullptr, 0, gauche.data(), droite.data(), 256);
        for (int i = 0; i < 256; ++i) crete = std::max(crete, std::abs(gauche[i]));
    }
    VSM_ASSERT(crete > 0.0f);
}

VSM_TEST(a_missing_clap_file_is_reported_absent_and_never_substituted) {
    // LE CRITÈRE DE LA PHASE D7, tenu dès la première étape : un plugin
    // introuvable ne devient pas une autre machine. Rendre un instrument de
    // remplacement produirait un morceau qui joue autre chose, sans le dire.
    installClapResolver();
    auto& registre = vsm::audio::plugin::PluginRegistry::instance();
    VSM_ASSERT(registre.create(clapInstrumentId("/nulle/part/absent.clap", "quoi")) == nullptr);
}

// --- D7.3 : les entrées audio, donc les effets ------------------------------
//
// Ce que ces tests doivent prouver n'est pas « un effet se charge » mais qu'il
// REÇOIT LE SIGNAL DE LA PISTE. Un hôte sans entrées produit un effet qui se
// charge, s'affiche, expose ses paramètres et rend du silence -- et rien de
// tout cela ne ressemble à une panne tant qu'on ne l'écoute pas. L'effet
// d'essai construit par ce dépôt inverse le signe : une transformation qui ne
// peut pas se produire par hasard.

namespace {
const char* effetDEssai() { return VSM_CLAP_TEST_EFFECT_PATH; }
} // namespace

VSM_TEST(a_clap_file_says_whether_it_holds_instruments_or_effects) {
    std::string erreur;
    const auto effets = scanClapFile(effetDEssai(), erreur);
    VSM_ASSERT(!effets.empty());
    VSM_ASSERT(!effets[0].isInstrument);

    const auto instruments = scanClapFile(adapterPath(), erreur);
    VSM_ASSERT(!instruments.empty());
    // LA DISTINCTION EST LUE DANS LES « FEATURES » que le plugin déclare, pas
    // devinée du nom ni du nombre de ports : une heuristique marcherait la
    // plupart du temps, et c'est ce qui la rend dangereuse.
    VSM_ASSERT(instruments[0].isInstrument);
}

VSM_TEST(a_third_party_clap_effect_reads_the_signal_it_is_given) {
    std::string erreur;
    auto effet = createClapEffect(effetDEssai(), "", erreur);
    VSM_ASSERT(effet != nullptr);
    VSM_ASSERT(erreur.empty());
    effet->prepare(48000.0, 256);

    std::vector<float> gauche(256, 0.5f), droite(256, -0.25f);
    effet->process(gauche.data(), droite.data(), 256);

    // Comparaison à une valeur ATTENDUE, pas à « quelque chose de non nul » :
    // un effet qui rendrait du bruit, du silence, ou son entrée intacte
    // échouerait tous les trois ici.
    for (int i = 0; i < 256; ++i) {
        VSM_ASSERT_NEAR(gauche[i], -0.5f, 1e-5);
        VSM_ASSERT_NEAR(droite[i], 0.25f, 1e-5);
    }
}

VSM_TEST(a_clap_instrument_is_refused_as_an_insert_and_the_other_way_round) {
    std::string erreur;
    VSM_ASSERT(createClapEffect(adapterPath(), "", erreur) == nullptr);
    VSM_ASSERT(!erreur.empty());

    erreur.clear();
    VSM_ASSERT(createClapInstrument(effetDEssai(), "", erreur) == nullptr);
    VSM_ASSERT(!erreur.empty());
}

VSM_TEST(a_clap_effect_parameter_reaches_the_plugin) {
    std::string erreur;
    auto effet = createClapEffect(effetDEssai(), "", erreur);
    VSM_ASSERT(effet != nullptr);
    effet->prepare(48000.0, 256);
    VSM_ASSERT(!effet->parameterList().empty());

    const auto id = effet->parameterList()[0].id;
    effet->setParameter(id, 0.5f);
    VSM_ASSERT_NEAR(effet->getParameter(id), 0.5f, 1e-5);

    // ET LE SON SUIT. Un paramètre qu'on peut relire mais qui ne change rien
    // n'a pas atteint le plugin ; c'est précisément ce que `params->flush()`
    // existe pour éviter, et ce qu'un test de lecture seule ne verrait pas.
    std::vector<float> gauche(64, 1.0f), droite(64, 1.0f);
    effet->process(gauche.data(), droite.data(), 64);
    for (int i = 0; i < 64; ++i) VSM_ASSERT_NEAR(gauche[i], -0.5f, 1e-5);
}

VSM_TEST(a_clap_effects_state_survives_and_the_sound_follows) {
    std::string erreur;
    auto original = createClapEffect(effetDEssai(), "", erreur);
    VSM_ASSERT(original != nullptr);
    original->prepare(48000.0, 64);
    original->setParameter(1, 0.25f);

    const std::string etat = original->saveNativeState();
    VSM_ASSERT(!etat.empty());

    auto rouvert = createClapEffect(effetDEssai(), "", erreur);
    VSM_ASSERT(rouvert != nullptr);
    rouvert->prepare(48000.0, 64);
    VSM_ASSERT(rouvert->loadNativeState(etat));

    std::vector<float> gauche(64, 1.0f), droite(64, 1.0f);
    rouvert->process(gauche.data(), droite.data(), 64);
    for (int i = 0; i < 64; ++i) VSM_ASSERT_NEAR(gauche[i], -0.25f, 1e-5);
}

VSM_TEST(the_effect_factory_loads_a_clap_once_the_resolver_is_installed) {
    installClapResolver();
    auto effet = vsm::audio::effect::EffectFactory::create(clapInstrumentId(effetDEssai(), ""));
    VSM_ASSERT(effet != nullptr);

    // ET LES EFFETS INTERNES RÉPONDENT TOUJOURS : poser un résolveur ne doit
    // rien retirer à ce qui marchait avant lui.
    VSM_ASSERT(vsm::audio::effect::EffectFactory::create("delay") != nullptr);
    VSM_ASSERT(vsm::audio::effect::EffectFactory::create("pas-un-effet") == nullptr);
}

// --- D7.4 : le transport transmis au plugin ---------------------------------
//
// LE CRITÈRE DIT « UN DELAY SYNCHRONISÉ AU TEMPO SUIT LE TEMPO », et c'est
// exactement ce qui est mesuré : l'effet d'essai retarde d'une NOIRE, dont la
// durée n'existe que dans le transport. On lui envoie une impulsion, on cherche
// l'écho, et on regarde s'il tombe là où le tempo l'exige. Un hôte qui ne
// transmettrait rien laisserait le plugin sur 120 BPM d'usine : à 90 BPM
// l'écho tomberait 167 ms trop tôt, ce qui s'entend et ce que ce test voit.

namespace {

/// L'indice du premier échantillon dépassant `seuil`, ou -1.
int premierPic(vsm::audio::effect::IAudioEffect& effet, double tempo, int total, float seuil) {
    vsm::audio::plugin::TransportInfo transport;
    transport.playing = true;
    transport.tempoBpm = tempo;
    effet.setTransportInfo(transport);

    constexpr int kBloc = 512;
    std::vector<float> gauche(kBloc, 0.0f), droite(kBloc, 0.0f);
    int rendus = 0;
    bool impulsionEnvoyee = false;
    while (rendus < total) {
        std::fill(gauche.begin(), gauche.end(), 0.0f);
        std::fill(droite.begin(), droite.end(), 0.0f);
        if (!impulsionEnvoyee) {
            gauche[0] = 1.0f;
            droite[0] = 1.0f;
            impulsionEnvoyee = true;
        }
        effet.setTransportInfo(transport);
        effet.process(gauche.data(), droite.data(), kBloc);
        for (int i = 0; i < kBloc; ++i) {
            // On saute l'impulsion directe elle-même : ce qu'on cherche est
            // l'ÉCHO, donc le premier pic après le tout début.
            if (rendus + i < 8) continue;
            if (std::abs(gauche[i]) > seuil) return rendus + i;
        }
        rendus += kBloc;
    }
    return -1;
}

} // namespace

VSM_TEST(a_tempo_synced_delay_follows_the_tempo) {
    std::string erreur;
    auto lent = createClapEffect(effetDEssai(), "", erreur);
    auto rapide = createClapEffect(effetDEssai(), "", erreur);
    VSM_ASSERT(lent != nullptr && rapide != nullptr);
    lent->prepare(48000.0, 512);
    rapide->prepare(48000.0, 512);

    const int aQuatreVingtDix = premierPic(*lent, 90.0, 48000 * 2, 0.3f);
    const int aCentQuatreVingts = premierPic(*rapide, 180.0, 48000 * 2, 0.3f);
    VSM_ASSERT(aQuatreVingtDix > 0);
    VSM_ASSERT(aCentQuatreVingts > 0);

    // Une noire à 90 BPM dure 2/3 de seconde, à 180 BPM un tiers. La tolérance
    // est d'un bloc : on vérifie que l'écho SUIT le tempo, pas la précision
    // d'un plugin d'essai.
    VSM_ASSERT(std::abs(aQuatreVingtDix - 32000) < 600);
    VSM_ASSERT(std::abs(aCentQuatreVingts - 16000) < 600);

    // ET LE RAPPORT EST BIEN CELUI DES TEMPOS : deux fois plus vite, deux fois
    // plus court. C'est ce qui distingue « le plugin a reçu un tempo » de « le
    // plugin a reçu LE tempo ».
    VSM_ASSERT(std::abs(aQuatreVingtDix - 2 * aCentQuatreVingts) < 1200);
}

VSM_TEST(a_plugin_without_transport_is_left_on_its_factory_tempo) {
    // LE TEST QUI DONNE SON SENS AU PRÉCÉDENT. Sans transport, l'effet d'essai
    // retombe sur 120 BPM -- une demi-seconde. Si les deux tests passaient avec
    // le même chiffre, c'est que le transport n'aurait jamais servi à rien.
    std::string erreur;
    auto effet = createClapEffect(effetDEssai(), "", erreur);
    VSM_ASSERT(effet != nullptr);
    effet->prepare(48000.0, 512);

    constexpr int kBloc = 512;
    std::vector<float> gauche(kBloc, 0.0f), droite(kBloc, 0.0f);
    int rendus = 0, pic = -1;
    bool envoyee = false;
    while (rendus < 48000 && pic < 0) {
        std::fill(gauche.begin(), gauche.end(), 0.0f);
        std::fill(droite.begin(), droite.end(), 0.0f);
        if (!envoyee) { gauche[0] = 1.0f; envoyee = true; }
        effet->process(gauche.data(), droite.data(), kBloc);   // aucun transport livré
        for (int i = 0; i < kBloc && pic < 0; ++i)
            if (rendus + i >= 8 && std::abs(gauche[i]) > 0.3f) pic = rendus + i;
        rendus += kBloc;
    }
    VSM_ASSERT(pic > 0);
    VSM_ASSERT(std::abs(pic - 24000) < 600);   // 120 BPM d'usine
}

// ---------------------------------------------------------------------------
// D7.4 : LA FAÇADE NATIVE, ET CE QU'ON PEUT EN VÉRIFIER SANS ÉCRAN
// ---------------------------------------------------------------------------
//
// Ouvrir une interface demande un serveur graphique ; SAVOIR SI UN PLUGIN EN A
// UNE n'en demande pas. C'est cette moitié-là que la suite garde, parce que
// c'est elle qui décide si l'application propose l'entrée de menu -- et
// proposer d'ouvrir une fenêtre qui n'existe pas est précisément le genre de
// promesse en trop que ce dépôt refuse.
//
// L'AUTRE MOITIÉ -- l'incrustation elle-même -- se vérifie en l'ouvrant, avec
// `vsm-clap-gui-check`. Elle ne peut pas se simuler : c'est tout le motif pour
// lequel D7.4 l'avait différée.

VSM_TEST(clap_a_plugin_without_a_gui_says_so) {
    // L'ADAPTATEUR DU DÉPÔT N'A PAS D'INTERFACE NATIVE, et c'est voulu : les
    // machines VSM ont leur façade, montrée par le Synth Rack. Deux chemins
    // vers la même chose, dont l'un ne mène nulle part, valent moins qu'un
    // seul (note de D7.4).
    std::string erreur;
    auto instrument = vsm::clap::createClapInstrument(VSM_CLAP_ADAPTER_PATH, "", erreur);
    VSM_ASSERT(instrument != nullptr);
    VSM_ASSERT(!vsm::clap::hasNativeEditor(*instrument));
}

#ifdef VSM_CLAP_TEST_GUI_PATH
VSM_TEST(clap_a_plugin_with_an_embeddable_gui_says_so_too) {
    // ET LA RÉPONSE INVERSE DOIT ÊTRE VRAIE AUSSI. Un prédicat qui répondrait
    // toujours faux passerait le test précédent sans rien garantir -- c'est la
    // même précaution que le « garde-fou du garde-fou » du compteur
    // d'allocations.
    std::string erreur;
    auto instrument = vsm::clap::createClapInstrument(VSM_CLAP_TEST_GUI_PATH, "", erreur);
    VSM_ASSERT(instrument != nullptr);
    VSM_ASSERT(vsm::clap::hasNativeEditor(*instrument));
}
#endif
