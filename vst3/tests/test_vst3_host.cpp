#include "TestFramework.h"
#include "Vst3PluginHost.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/interchange/SynthPreset.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

// D7.2 — HÉBERGER UN INSTRUMENT VST3.
//
// L'hôte charge le `.vst3` que ce même dépôt vient de construire (voir
// `tests/TestInstrument.cpp`) : le circuit est fermé, aucun plugin tiers
// installé n'est nécessaire, et le jour où un plugin tiers pose problème on
// saura que le défaut vient de lui puisque ces tests-là sont verts.
//
// Le critère de l'étape a deux moitiés, et chacune a ses tests : « un
// instrument tiers JOUE » et « SE SAUVEGARDE dans le projet ».

using namespace vsm::vst3;
using vsm::audio::plugin::MidiNoteEvent;

namespace {

/// Le bundle `.vst3` que ce dépôt vient de construire. CMake ne connaît que le
/// dossier du BINAIRE, deux niveaux plus bas ; le chemin est remonté puis
/// normalisé ici, parce qu'un chemin qui ne se termine pas littéralement par
/// « .vst3 » est refusé par JUCE sans le moindre message.
const std::string& dossierDuPlugin() {
    static const std::string chemin =
        std::filesystem::path(VSM_VST3_TEST_INSTRUMENT_BINARY_DIR)
            .parent_path().parent_path().lexically_normal().string();
    return chemin;
}

MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}

MidiNoteEvent noteOff(int offset, uint8_t note) {
    return {MidiNoteEvent::Kind::NoteOff, offset, 0, note, 0};
}

/// La crête d'un bloc rendu par l'instrument.
float creteApres(vsm::audio::plugin::ISynthPlugin& instrument, const MidiNoteEvent* events,
                  int numEvents, int blocs = 4) {
    std::vector<float> gauche(256, 0.0f), droite(256, 0.0f);
    float crete = 0.0f;
    for (int passe = 0; passe < blocs; ++passe) {
        instrument.process(passe == 0 ? events : nullptr, passe == 0 ? numEvents : 0,
                            gauche.data(), droite.data(), 256);
        for (int i = 0; i < 256; ++i) crete = std::max(crete, std::abs(gauche[i]));
    }
    return crete;
}

} // namespace

VSM_TEST(a_vst3_file_declares_what_it_contains) {
    std::string erreur;
    const auto trouves = scanVst3File(dossierDuPlugin(), erreur);
    VSM_ASSERT(!trouves.empty());
    VSM_ASSERT(erreur.empty());
    VSM_ASSERT(!trouves[0].id.empty());
    VSM_ASSERT(!trouves[0].name.empty());
    // LA DISTINCTION INSTRUMENT/EFFET EST LUE DANS LE FICHIER, pas devinée du
    // nom : poser un effet là où une piste attend un instrument donnerait une
    // piste muette sans rien expliquer.
    VSM_ASSERT(trouves[0].isInstrument);
}

VSM_TEST(a_broken_file_is_reported_and_never_crashes_the_scan) {
    std::string erreur;
    const auto trouves = scanVst3File("/nulle/part/absent.vst3", erreur);
    VSM_ASSERT(trouves.empty());
    VSM_ASSERT(!erreur.empty());
}

VSM_TEST(a_third_party_instrument_plays) {
    std::string erreur;
    const auto instrument = createVst3Instrument(dossierDuPlugin(), "", erreur);
    VSM_ASSERT(instrument != nullptr);
    VSM_ASSERT(erreur.empty());

    instrument->initialize(48000.0, 256);
    const MidiNoteEvent depart = noteOn(0, 69, 100);
    VSM_ASSERT(creteApres(*instrument, &depart, 1) > 0.0f);

    // ET IL SE TAIT. Un instrument qui sonne sans jamais s'arrêter passerait le
    // premier test et rendrait le morceau inécoutable.
    const MidiNoteEvent arret = noteOff(0, 69);
    instrument->process(&arret, 1, nullptr, nullptr, 0); // événement seul, bloc nul
    std::vector<float> gauche(256, 0.0f), droite(256, 0.0f);
    instrument->process(&arret, 1, gauche.data(), droite.data(), 256);
    float queue = 0.0f;
    for (int passe = 0; passe < 4; ++passe) {
        instrument->process(nullptr, 0, gauche.data(), droite.data(), 256);
        for (int i = 0; i < 256; ++i) queue = std::max(queue, std::abs(gauche[i]));
    }
    VSM_ASSERT(queue <= 0.0f);
}

VSM_TEST(a_third_party_instrument_shows_its_parameters) {
    std::string erreur;
    const auto instrument = createVst3Instrument(dossierDuPlugin(), "", erreur);
    VSM_ASSERT(instrument != nullptr);
    instrument->initialize(48000.0, 256);

    VSM_ASSERT(!instrument->parameterList().empty());
    const auto id = instrument->parameterList()[0].id;
    instrument->setParameter(id, 0.25f);
    VSM_ASSERT_NEAR(instrument->getParameter(id), 0.25f, 1e-3);
}

VSM_TEST(a_third_party_instruments_state_survives_the_project_file) {
    // LA SECONDE MOITIÉ DU CRITÈRE. Ce qui est vérifié n'est pas « un fichier
    // est écrit » mais que l'état retrouvé porte CE QUE LA TABLE DE PARAMÈTRES
    // NE DIT PAS -- ici la « marque » du plugin de test, qu'aucun paramètre
    // n'expose. Sans cela, le test passerait aussi pour un hôte qui ne
    // sauvegarderait que ses paramètres, c'est-à-dire pour la panne même qu'on
    // veut interdire.
    std::string erreur;
    const auto original = createVst3Instrument(dossierDuPlugin(), "", erreur);
    VSM_ASSERT(original != nullptr);
    original->initialize(48000.0, 256);
    original->setParameter(0, 0.75f);
    // La marque passe par un changement de programme : c'est la seule porte
    // d'entrée du plugin de test qui n'est pas un paramètre.
    vsm::audio::plugin::MidiControlEvent programme;
    programme.kind = vsm::audio::plugin::MidiControlEvent::Kind::ProgramChange;
    programme.index = 0;
    original->handleControlEvent(programme);

    const std::string etat = original->saveNativeState();
    VSM_ASSERT(!etat.empty());

    // LE VOYAGE COMPLET : capture -> JSON -> texte -> JSON -> application.
    const std::string identifiant = vst3InstrumentId(dossierDuPlugin(), "");
    vsm::interchange::SynthPreset preset =
        vsm::interchange::capturePreset(*original, identifiant, "Prise 1");
    preset.nativeStateFormat = kVst3StateFormat;
    VSM_ASSERT(!preset.nativeState.empty());

    const std::string texte = vsm::interchange::synthPresetToJson(preset).toString();
    const auto relu = vsm::interchange::parseSynthPreset(texte);
    VSM_ASSERT(relu.success);
    VSM_ASSERT_EQ(relu.preset.nativeState, preset.nativeState);
    VSM_ASSERT_EQ(relu.preset.nativeStateFormat, std::string(kVst3StateFormat));

    const auto rouvert = createVst3Instrument(dossierDuPlugin(), "", erreur);
    VSM_ASSERT(rouvert != nullptr);
    rouvert->initialize(48000.0, 256);
    const auto rapport = vsm::interchange::applyPreset(relu.preset, *rouvert, identifiant);
    VSM_ASSERT(rapport.nativeStateApplied);
    VSM_ASSERT(rapport.nativeStateDetail.empty());
    VSM_ASSERT_NEAR(rouvert->getParameter(0), 0.75f, 1e-3);
    VSM_ASSERT_EQ(rouvert->saveNativeState(), etat);
}

VSM_TEST(a_native_state_is_never_applied_to_another_machine) {
    // Reposer l'état d'un plugin dans un autre serait refusé au mieux, mal
    // interprété au pire. Le refus est DIT.
    vsm::interchange::SynthPreset preset;
    preset.pluginId = "vst3:/un/autre.vst3#7";
    preset.nativeState = "Zm91";
    preset.nativeStateFormat = kVst3StateFormat;

    std::string erreur;
    const auto instrument = createVst3Instrument(dossierDuPlugin(), "", erreur);
    VSM_ASSERT(instrument != nullptr);
    instrument->initialize(48000.0, 256);

    const auto rapport = vsm::interchange::applyPreset(
        preset, *instrument, vst3InstrumentId(dossierDuPlugin(), ""));
    VSM_ASSERT(!rapport.nativeStateApplied);
    VSM_ASSERT(!rapport.nativeStateDetail.empty());
}

VSM_TEST(an_instrument_id_survives_a_round_trip_through_its_text_form) {
    const std::string id = vst3InstrumentId("/opt/plug ins/ma#chine.vst3", "1234");
    std::string chemin, plugin;
    VSM_ASSERT(parseVst3InstrumentId(id, chemin, plugin));
    VSM_ASSERT_EQ(chemin, std::string("/opt/plug ins/ma#chine.vst3"));
    VSM_ASSERT_EQ(plugin, std::string("1234"));
    // Un identifiant de machine native, ou CLAP, ne regarde pas cette couche.
    VSM_ASSERT(!parseVst3InstrumentId("vsm.tb303", chemin, plugin));
    VSM_ASSERT(!parseVst3InstrumentId("clap:/x.clap#y", chemin, plugin));
}

VSM_TEST(the_registry_loads_a_vst3_once_the_resolver_is_installed) {
    installVst3Resolver();
    auto& registre = vsm::audio::plugin::PluginRegistry::instance();

    const std::string id = vst3InstrumentId(dossierDuPlugin(), "");
    VSM_ASSERT(!registre.isRegistered(id));
    const auto instrument = registre.create(id);
    VSM_ASSERT(instrument != nullptr);

    instrument->initialize(48000.0, 256);
    const MidiNoteEvent depart = noteOn(0, 60, 100);
    VSM_ASSERT(creteApres(*instrument, &depart, 1) > 0.0f);
}

VSM_TEST(a_missing_vst3_is_reported_absent_and_never_substituted) {
    installVst3Resolver();
    auto& registre = vsm::audio::plugin::PluginRegistry::instance();
    VSM_ASSERT(registre.create(vst3InstrumentId("/nulle/part/absent.vst3", "1")) == nullptr);
    // ET LES MACHINES DU PARC RÉPONDENT TOUJOURS : poser un résolveur ne doit
    // rien retirer à ce qui marchait avant lui.
    VSM_ASSERT(registre.create("vsm.tb303") != nullptr);
}

// --- D7.3 : les entrées audio, donc les effets ------------------------------
//
// Ce que ces tests doivent prouver n'est pas « un effet se charge » mais qu'il
// REÇOIT LE SIGNAL DE LA PISTE. Un hôte sans entrées produit un effet qui se
// charge, s'affiche, expose ses paramètres et rend du silence -- et rien dans
// tout cela ne ressemble à une panne tant qu'on ne l'écoute pas.

namespace {

const std::string& fichierDeLEffet() {
    static const std::string chemin =
        std::filesystem::path(VSM_VST3_TEST_EFFECT_BINARY_DIR)
            .parent_path().parent_path().lexically_normal().string();
    return chemin;
}

} // namespace

VSM_TEST(a_vst3_effect_file_says_it_is_not_an_instrument) {
    std::string erreur;
    const auto trouves = scanVst3File(fichierDeLEffet(), erreur);
    VSM_ASSERT(!trouves.empty());
    VSM_ASSERT(!trouves[0].isInstrument);
}

VSM_TEST(a_third_party_effect_reads_the_signal_it_is_given) {
    std::string erreur;
    const auto effet = createVst3Effect(fichierDeLEffet(), "", erreur);
    VSM_ASSERT(effet != nullptr);
    VSM_ASSERT(erreur.empty());
    effet->prepare(48000.0, 256);

    std::vector<float> gauche(256, 0.0f), droite(256, 0.0f);
    for (int i = 0; i < 256; ++i) {
        gauche[i] = 0.5f;
        droite[i] = -0.25f;
    }
    effet->process(gauche.data(), droite.data(), 256);

    // L'EFFET DE TEST INVERSE LE SIGNE. Comparer à une valeur ATTENDUE et non à
    // « quelque chose de non nul » : un effet qui rendrait du bruit, ou du
    // silence, ou son entrée intacte, échouerait tous les trois ici, alors
    // qu'un test de non-silence n'en attraperait qu'un.
    for (int i = 0; i < 256; ++i) {
        VSM_ASSERT_NEAR(gauche[i], -0.5f, 1e-5);
        VSM_ASSERT_NEAR(droite[i], 0.25f, 1e-5);
    }
}

VSM_TEST(an_instrument_is_refused_as_an_insert_and_the_other_way_round) {
    // Poser un instrument dans une chaîne d'inserts lui ferait ignorer le
    // signal : la piste deviendrait muette, et il faudrait le deviner à
    // l'oreille. Le refus est dit dans les deux sens.
    std::string erreur;
    VSM_ASSERT(createVst3Effect(dossierDuPlugin(), "", erreur) == nullptr);
    VSM_ASSERT(!erreur.empty());

    erreur.clear();
    VSM_ASSERT(createVst3Instrument(fichierDeLEffet(), "", erreur) == nullptr);
    VSM_ASSERT(!erreur.empty());
}

VSM_TEST(the_effect_factory_loads_a_vst3_once_the_resolver_is_installed) {
    installVst3Resolver();
    const std::string id = vst3InstrumentId(fichierDeLEffet(), "");
    auto effet = vsm::audio::effect::EffectFactory::create(id);
    VSM_ASSERT(effet != nullptr);

    // ET LES EFFETS INTERNES RÉPONDENT TOUJOURS : poser un résolveur ne doit
    // rien retirer à ce qui marchait avant lui.
    VSM_ASSERT(vsm::audio::effect::EffectFactory::create("reverb") != nullptr);
    VSM_ASSERT(vsm::audio::effect::EffectFactory::create("cet-effet-n-existe-pas") == nullptr);
}

VSM_TEST(a_third_party_effects_state_survives_the_project_file) {
    std::string erreur;
    const auto original = createVst3Effect(fichierDeLEffet(), "", erreur);
    VSM_ASSERT(original != nullptr);
    original->prepare(48000.0, 256);
    original->setParameter(0, 0.25f);

    const std::string etat = original->saveNativeState();
    VSM_ASSERT(!etat.empty());

    const auto rouvert = createVst3Effect(fichierDeLEffet(), "", erreur);
    VSM_ASSERT(rouvert != nullptr);
    rouvert->prepare(48000.0, 256);
    VSM_ASSERT(rouvert->loadNativeState(etat));
    VSM_ASSERT_NEAR(rouvert->getParameter(0), 0.25f, 1e-3);

    // ET LE SON SUIT, ce qui est la seule chose qui compte : un état reposé qui
    // ne changerait pas ce qu'on entend n'aurait rien reposé du tout.
    std::vector<float> gauche(64, 1.0f), droite(64, 1.0f);
    rouvert->process(gauche.data(), droite.data(), 64);
    std::vector<float> temoin(64, 1.0f), temoinD(64, 1.0f);
    original->process(temoin.data(), temoinD.data(), 64);
    for (int i = 0; i < 64; ++i) VSM_ASSERT_NEAR(gauche[i], temoin[i], 1e-6);
}
