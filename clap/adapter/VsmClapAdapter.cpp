// Adaptateur CLAP (Phase 7, P5) : expose les machines VSM comme un plugin CLAP.
//
// CE QU'IL FAIT, ET CE QU'IL NE FAIT PAS : il ENVELOPPE `ISynthPlugin`, il ne
// le réécrit pas. Le DSP reste identique, bit pour bit, entre l'application
// autonome et l'hôte CLAP -- toute autre approche créerait deux versions du
// même instrument qui finiraient par sonner différemment, et l'utilisateur
// n'aurait aucun moyen de savoir laquelle est la bonne.
//
// RÈGLE TEMPS RÉEL INCHANGÉE : `process()` n'alloue pas, ne verrouille pas, ne
// fait pas d'I/O -- exactement comme le chemin natif (ROADMAP-interop § 0).
// Toute la préparation (instanciation de la machine, construction de la table
// de paramètres) a lieu dans `init()`/`activate()`, jamais dans le rendu.

#include "vsm/interchange/ClapParameterIds.h"
#include "vsm/interchange/SynthPreset.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"

#include <clap/clap.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

using vsm::audio::plugin::ISynthPlugin;
using vsm::audio::plugin::MidiNoteEvent;
using vsm::interchange::ClapParameterMapping;

/// Machines exposées. Les instruments à hauteur d'abord : ce sont ceux qu'un
/// hôte attend en premier dans sa liste.
/// Machines exposées à l'hôte : TOUT ce que le registre sait instancier,
/// interrogé au chargement -- jamais une liste écrite ici.
///
/// La première version énumérait onze machines à la main, et le défaut est
/// resté invisible pendant huit ajouts : le sampler, l'e-piano, l'OB-X, le
/// supersaw, la table d'ondes, l'hybride PCM, l'orgue et le synthé neutre
/// existaient dans le DAW mais pas dans les hôtes CLAP, sans erreur ni
/// avertissement nulle part. Le pont Python avait déjà rencontré -- et
/// corrigé -- exactement ce piège (`available_machines()` interroge le
/// moteur) ; la leçon vaut des deux côtés.
const std::vector<std::string>& exposedMachines() {
    static const std::vector<std::string> machines = [] {
        vsm::audio::plugin::registerBuiltInPlugins();
        std::vector<std::string> ids;
        for (const auto& [id, name] : vsm::audio::plugin::PluginRegistry::instance().listAvailable()) {
            // Le générateur de tonalité est un outil de test du moteur, pas un
            // instrument : l'exposer encombrerait la liste de l'hôte.
            if (id == "vsm.testtone") continue;
            ids.push_back(id);
        }
        // Le registre est une table de hachage : sans tri, l'ordre changerait
        // d'un chargement à l'autre, et la liste de plugins de l'hôte avec.
        std::sort(ids.begin(), ids.end());
        return ids;
    }();
    return machines;
}

struct DescriptorStorage {
    std::string id, name, vendor, description, features0;
    std::vector<const char*> features;
    clap_plugin_descriptor descriptor{};
};

/// Les descripteurs vivent aussi longtemps que la bibliothèque : CLAP ne
/// recopie pas les chaînes, il conserve les pointeurs.
std::vector<DescriptorStorage>& descriptors() {
    static std::vector<DescriptorStorage> storage;
    static std::once_flag once;
    std::call_once(once, [] {
        vsm::audio::plugin::registerBuiltInPlugins();
        for (const std::string& machineId : exposedMachines()) {
            auto plugin = vsm::audio::plugin::PluginRegistry::instance().create(machineId);
            if (!plugin) continue;

            DescriptorStorage entry;
            entry.id = vsm::interchange::clapPluginId(machineId);
            entry.name = plugin->machineName();
            entry.vendor = "VSM Studio";
            entry.description = "Machine native VSM exposée en CLAP";
            entry.features0 = CLAP_PLUGIN_FEATURE_INSTRUMENT;
            storage.push_back(std::move(entry));
        }
        for (auto& entry : storage) {
            entry.features = { entry.features0.c_str(), CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr };
            entry.descriptor.clap_version = CLAP_VERSION;
            entry.descriptor.id = entry.id.c_str();
            entry.descriptor.name = entry.name.c_str();
            entry.descriptor.vendor = entry.vendor.c_str();
            entry.descriptor.version = "0.1.0";
            entry.descriptor.description = entry.description.c_str();
            entry.descriptor.features = entry.features.data();
        }
    });
    return storage;
}

std::string vsmIdForClapId(const std::string& clapId) {
    for (const std::string& machineId : exposedMachines())
        if (vsm::interchange::clapPluginId(machineId) == clapId) return machineId;
    return {};
}

// ---------------------------------------------------------------------------
// Instance de plugin
// ---------------------------------------------------------------------------

struct VsmClapPlugin {
    clap_plugin plugin{};
    const clap_host* host = nullptr;
    std::string vsmPluginId;
    std::shared_ptr<ISynthPlugin> instrument;
    std::vector<ClapParameterMapping> parameters;
    /// Buffer d'événements pré-alloué : `process()` ne doit jamais allouer.
    std::vector<MidiNoteEvent> events;
    double sampleRate = 48000.0;
};

VsmClapPlugin* self(const clap_plugin* plugin) {
    return static_cast<VsmClapPlugin*>(plugin->plugin_data);
}

// --- extension "params" ----------------------------------------------------

uint32_t paramsCount(const clap_plugin* plugin) {
    return static_cast<uint32_t>(self(plugin)->parameters.size());
}

bool paramsGetInfo(const clap_plugin* plugin, uint32_t index, clap_param_info* info) {
    auto* instance = self(plugin);
    if (index >= instance->parameters.size()) return false;
    const ClapParameterMapping& mapping = instance->parameters[index];

    std::memset(info, 0, sizeof(*info));
    info->id = mapping.clapId;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    info->min_value = static_cast<double>(mapping.minimum);
    info->max_value = static_cast<double>(mapping.maximum);
    info->default_value = static_cast<double>(mapping.defaultValue);
    std::snprintf(info->name, sizeof(info->name), "%s", mapping.displayName.c_str());
    // Le module devient un chemin de menu dans l'hôte : les paramètres d'un
    // même bloc (filtre, enveloppe...) s'y regroupent naturellement.
    std::snprintf(info->module, sizeof(info->module), "%s", mapping.module.c_str());
    return true;
}

bool paramsGetValue(const clap_plugin* plugin, clap_id paramId, double* outValue) {
    auto* instance = self(plugin);
    for (const auto& mapping : instance->parameters) {
        if (mapping.clapId != paramId) continue;
        *outValue = static_cast<double>(instance->instrument->getParameter(mapping.vsmParamId));
        return true;
    }
    return false;
}

bool paramsValueToText(const clap_plugin* plugin, clap_id paramId, double value,
                        char* out, uint32_t size) {
    auto* instance = self(plugin);
    for (const auto& mapping : instance->parameters) {
        if (mapping.clapId != paramId) continue;
        std::snprintf(out, size, "%.3f", value);
        return true;
    }
    return false;
}

/// ICI, ET SEULEMENT ICI, LA LOCALE A RAISON — et c'est une décision, pas un
/// oubli. Ailleurs dans le projet, un nombre écrit en texte traverse une
/// frontière (un fichier, une ligne de commande) et doit se lire en locale C
/// quoi qu'il arrive : c'est la règle d'`interchange/NumberText.h`, écrite
/// après avoir trouvé des `project.json` contenant `0,8`. Ce texte-ci ne
/// traverse rien : il est AFFICHÉ par l'hôte à un être humain et RETAPÉ par
/// lui, dans sa langue. `%.3f` et `strtod` consultent la même locale, celle du
/// processus hôte, donc l'aller-retour est cohérent — et un utilisateur
/// français qui tape « 0,5 » doit obtenir un demi, pas une erreur.
bool paramsTextToValue(const clap_plugin*, clap_id, const char* text, double* outValue) {
    *outValue = std::strtod(text, nullptr);
    return true;
}

void paramsFlush(const clap_plugin* plugin, const clap_input_events* in, const clap_output_events*) {
    auto* instance = self(plugin);
    const uint32_t count = in ? in->size(in) : 0;
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header* header = in->get(in, i);
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
        for (const auto& mapping : instance->parameters)
            if (mapping.clapId == event->param_id)
                instance->instrument->setParameter(mapping.vsmParamId, static_cast<float>(event->value));
    }
}

const clap_plugin_params paramsExtension = {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush
};

// --- extensions "audio-ports" et "note-ports" ------------------------------

uint32_t audioPortsCount(const clap_plugin*, bool isInput) { return isInput ? 0u : 1u; }

bool audioPortsGet(const clap_plugin*, uint32_t index, bool isInput, clap_audio_port_info* info) {
    if (isInput || index != 0) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    std::snprintf(info->name, sizeof(info->name), "Sortie stéréo");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports audioPortsExtension = { audioPortsCount, audioPortsGet };

uint32_t notePortsCount(const clap_plugin*, bool isInput) { return isInput ? 1u : 0u; }

bool notePortsGet(const clap_plugin*, uint32_t index, bool isInput, clap_note_port_info* info) {
    if (!isInput || index != 0) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 0;
    // On accepte les deux dialectes : CLAP natif ET MIDI brut, parce que les
    // hôtes ne proposent pas tous le premier.
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::snprintf(info->name, sizeof(info->name), "Entrée notes");
    return true;
}

const clap_plugin_note_ports notePortsExtension = { notePortsCount, notePortsGet };

// --- extension "state" -----------------------------------------------------
//
// L'état est écrit en `*.synth.json` SÉMANTIQUE plutôt qu'en table d'ID
// internes : un projet d'hôte enregistré aujourd'hui reste lisible même si les
// identifiants internes de la machine changent, et reste inspectable à la main.

bool stateSave(const clap_plugin* plugin, const clap_ostream* stream) {
    auto* instance = self(plugin);
    const vsm::interchange::SynthPreset preset =
        vsm::interchange::capturePreset(*instance->instrument, instance->vsmPluginId, "CLAP state");
    const std::string text = vsm::interchange::synthPresetToJson(preset).toString();

    size_t written = 0;
    while (written < text.size()) {
        const int64_t result = stream->write(stream, text.data() + written, text.size() - written);
        if (result <= 0) return false;
        written += static_cast<size_t>(result);
    }
    return true;
}

bool stateLoad(const clap_plugin* plugin, const clap_istream* stream) {
    std::string text;
    char buffer[4096];
    while (true) {
        const int64_t read = stream->read(stream, buffer, sizeof(buffer));
        if (read < 0) return false;
        if (read == 0) break;
        text.append(buffer, static_cast<size_t>(read));
    }

    auto* instance = self(plugin);
    auto loaded = vsm::interchange::parseSynthPreset(text);
    if (!loaded.success) return false;
    vsm::interchange::applyPreset(loaded.preset, *instance->instrument, instance->vsmPluginId);
    return true;
}

const clap_plugin_state stateExtension = { stateSave, stateLoad };

// --- cycle de vie ----------------------------------------------------------

bool pluginInit(const clap_plugin* plugin) {
    auto* instance = self(plugin);
    instance->instrument = vsm::audio::plugin::PluginRegistry::instance().create(instance->vsmPluginId);
    if (!instance->instrument) return false;
    instance->parameters = vsm::interchange::clapParameterMap(instance->vsmPluginId);
    return true;
}

void pluginDestroy(const clap_plugin* plugin) { delete self(plugin); }

bool pluginActivate(const clap_plugin* plugin, double sampleRate, uint32_t, uint32_t maxFrames) {
    auto* instance = self(plugin);
    instance->sampleRate = sampleRate;
    instance->instrument->initialize(sampleRate, static_cast<int>(maxFrames));
    // Pré-allocation : au-delà, `process()` ignore les événements en trop
    // plutôt que d'allouer sur le thread audio.
    instance->events.resize(std::max<size_t>(256, maxFrames));
    return true;
}

void pluginDeactivate(const clap_plugin*) {}
bool pluginStartProcessing(const clap_plugin*) { return true; }
void pluginStopProcessing(const clap_plugin*) {}
void pluginReset(const clap_plugin*) {}

clap_process_status pluginProcess(const clap_plugin* plugin, const clap_process* process) {
    auto* instance = self(plugin);
    if (process->audio_outputs_count < 1 || process->audio_outputs[0].channel_count < 2)
        return CLAP_PROCESS_ERROR;

    const uint32_t frames = process->frames_count;
    int noteCount = 0;

    const clap_input_events* in = process->in_events;
    const uint32_t eventCount = in ? in->size(in) : 0;
    for (uint32_t i = 0; i < eventCount; ++i) {
        const clap_event_header* header = in->get(in, i);
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;

        switch (header->type) {
            case CLAP_EVENT_PARAM_VALUE: {
                const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
                for (const auto& mapping : instance->parameters)
                    if (mapping.clapId == event->param_id)
                        instance->instrument->setParameter(mapping.vsmParamId,
                                                            static_cast<float>(event->value));
                break;
            }
            case CLAP_EVENT_NOTE_ON:
            case CLAP_EVENT_NOTE_OFF: {
                if (static_cast<size_t>(noteCount) >= instance->events.size()) break;
                const auto* event = reinterpret_cast<const clap_event_note*>(header);
                MidiNoteEvent note;
                note.kind = (header->type == CLAP_EVENT_NOTE_ON) ? MidiNoteEvent::Kind::NoteOn
                                                                  : MidiNoteEvent::Kind::NoteOff;
                note.sampleOffset = static_cast<int>(std::min<uint32_t>(header->time, frames ? frames - 1 : 0));
                // channel/key sont des int16_t côté CLAP et valent -1 pour
                // « toutes » : on les ramène dans les bornes MIDI.
                note.channel = static_cast<uint8_t>(std::max<int>(0, event->channel));
                note.note = static_cast<uint8_t>(std::clamp<int>(event->key, 0, 127));
                note.velocity = static_cast<uint8_t>(std::clamp(event->velocity * 127.0, 1.0, 127.0));
                instance->events[static_cast<size_t>(noteCount++)] = note;
                break;
            }
            case CLAP_EVENT_MIDI: {
                // Dialecte MIDI brut : 0x90 = note on, 0x80 = note off. Une
                // note on de vélocité 0 est un note off (convention MIDI).
                if (static_cast<size_t>(noteCount) >= instance->events.size()) break;
                const auto* event = reinterpret_cast<const clap_event_midi*>(header);
                const uint8_t status = static_cast<uint8_t>(event->data[0] & 0xF0);
                if (status != 0x90 && status != 0x80) break;
                MidiNoteEvent note;
                const bool isOn = (status == 0x90) && event->data[2] > 0;
                note.kind = isOn ? MidiNoteEvent::Kind::NoteOn : MidiNoteEvent::Kind::NoteOff;
                note.sampleOffset = static_cast<int>(std::min<uint32_t>(header->time, frames ? frames - 1 : 0));
                note.channel = static_cast<uint8_t>(event->data[0] & 0x0F);
                note.note = event->data[1];
                note.velocity = std::max<uint8_t>(1, event->data[2]);
                instance->events[static_cast<size_t>(noteCount++)] = note;
                break;
            }
            default: break;
        }
    }

    float* left = process->audio_outputs[0].data32[0];
    float* right = process->audio_outputs[0].data32[1];
    instance->instrument->process(instance->events.data(), noteCount, left, right,
                                   static_cast<int>(frames));
    return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &paramsExtension;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audioPortsExtension;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &notePortsExtension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &stateExtension;
    return nullptr;
}

void pluginOnMainThread(const clap_plugin*) {}

// --- fabrique et point d'entrée --------------------------------------------

uint32_t factoryCount(const clap_plugin_factory*) {
    return static_cast<uint32_t>(descriptors().size());
}

const clap_plugin_descriptor* factoryGetDescriptor(const clap_plugin_factory*, uint32_t index) {
    auto& storage = descriptors();
    return index < storage.size() ? &storage[index].descriptor : nullptr;
}

const clap_plugin* factoryCreate(const clap_plugin_factory*, const clap_host* host, const char* pluginId) {
    const std::string vsmId = vsmIdForClapId(pluginId ? pluginId : "");
    if (vsmId.empty()) return nullptr;

    auto* instance = new VsmClapPlugin();
    instance->host = host;
    instance->vsmPluginId = vsmId;

    for (const auto& entry : descriptors())
        if (entry.id == pluginId) instance->plugin.desc = &entry.descriptor;

    instance->plugin.plugin_data = instance;
    instance->plugin.init = pluginInit;
    instance->plugin.destroy = pluginDestroy;
    instance->plugin.activate = pluginActivate;
    instance->plugin.deactivate = pluginDeactivate;
    instance->plugin.start_processing = pluginStartProcessing;
    instance->plugin.stop_processing = pluginStopProcessing;
    instance->plugin.reset = pluginReset;
    instance->plugin.process = pluginProcess;
    instance->plugin.get_extension = pluginGetExtension;
    instance->plugin.on_main_thread = pluginOnMainThread;
    return &instance->plugin;
}

const clap_plugin_factory pluginFactory = { factoryCount, factoryGetDescriptor, factoryCreate };

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* factoryId) {
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &pluginFactory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    CLAP_VERSION, entryInit, entryDeinit, entryGetFactory
};
