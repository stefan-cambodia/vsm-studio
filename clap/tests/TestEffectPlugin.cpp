// Un EFFET CLAP minimal, construit par ce dépôt pour se tester lui-même (D7.3).
//
// POURQUOI IL EXISTE. L'adaptateur `clap/adapter/` expose les machines VSM,
// donc uniquement des INSTRUMENTS : l'hôte d'effets n'avait rien à héberger
// dans un test. Dépendre d'un effet CLAP installé sur la machine rendrait le
// test vert ou rouge selon l'ordinateur, ce qui ne vaut rien.
//
// CE QU'IL DOIT SAVOIR FAIRE, ET RIEN DE PLUS :
//   - LIRE SON ENTRÉE, et le prouver. Il inverse le signe et applique un gain :
//     un effet qui rendrait un signal sans regarder celui qu'on lui donne
//     passerait pour fonctionnel alors que l'hôte ne lui aurait rien transmis.
//   - déclarer une entrée ET une sortie stéréo (c'est ce que l'hôte cherche) ;
//   - porter un paramètre, et un état.
//
// Il n'est PAS une machine du parc : il ne s'enregistre nulle part et ne sort
// pas de ce dossier.

#include <clap/clap.h>

#include <cstring>
#include <string>

namespace {

constexpr const char* kPluginId = "com.vsmstudio.test.effect";

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

const clap_plugin_descriptor kDescriptor = {
    CLAP_VERSION,
    kPluginId,
    "VSM Test Effect (CLAP)",
    "VSM Studio",
    "",         // url
    "",         // manual_url
    "",         // support_url
    "0.1.0",
    "Effet d'essai : inverse le signe et applique un gain",
    kFeatures,
};

struct Instance {
    clap_plugin plugin{};
    double gain = 1.0;
    /// Non exposée par un paramètre : c'est ce qui rend le test d'état
    /// significatif. Un état reconstructible depuis la table de paramètres ne
    /// prouverait rien.
    int32_t marque = 0;
};

Instance* self(const clap_plugin* plugin) { return static_cast<Instance*>(plugin->plugin_data); }

// --- audio-ports : UNE ENTRÉE ET UNE SORTIE, c'est tout le sujet -----------

uint32_t audioPortsCount(const clap_plugin*, bool) { return 1u; }

bool audioPortsGet(const clap_plugin*, uint32_t index, bool isInput, clap_audio_port_info* info) {
    if (index != 0) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = isInput ? 0 : 1;
    std::snprintf(info->name, sizeof(info->name), isInput ? "Entree" : "Sortie");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports kAudioPorts = {audioPortsCount, audioPortsGet};

// --- params ---------------------------------------------------------------

uint32_t paramsCount(const clap_plugin*) { return 1u; }

bool paramsGetInfo(const clap_plugin*, uint32_t index, clap_param_info* info) {
    if (index != 0) return false;
    std::memset(info, 0, sizeof(*info));
    info->id = 1;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::snprintf(info->name, sizeof(info->name), "Gain");
    info->min_value = 0.0;
    info->max_value = 2.0;
    info->default_value = 1.0;
    return true;
}

bool paramsGetValue(const clap_plugin* plugin, clap_id id, double* out) {
    if (id != 1) return false;
    *out = self(plugin)->gain;
    return true;
}

bool paramsValueToText(const clap_plugin*, clap_id, double value, char* out, uint32_t size) {
    std::snprintf(out, size, "%.2f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin*, clap_id, const char* text, double* out) {
    *out = std::atof(text);
    return true;
}

void appliquerEvenements(const clap_plugin* plugin, const clap_input_events* in) {
    if (in == nullptr) return;
    const uint32_t count = in->size(in);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_event_header* header = in->get(in, i);
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID
            || header->type != CLAP_EVENT_PARAM_VALUE) continue;
        const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
        if (event->param_id == 1) self(plugin)->gain = event->value;
    }
}

void paramsFlush(const clap_plugin* plugin, const clap_input_events* in,
                  const clap_output_events*) {
    appliquerEvenements(plugin, in);
}

const clap_plugin_params kParams = {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush,
};

// --- state ----------------------------------------------------------------

bool stateSave(const clap_plugin* plugin, const clap_ostream* stream) {
    const double gain = self(plugin)->gain;
    const int32_t marque = self(plugin)->marque;
    return stream->write(stream, &gain, sizeof(gain)) == sizeof(gain)
        && stream->write(stream, &marque, sizeof(marque)) == sizeof(marque);
}

bool stateLoad(const clap_plugin* plugin, const clap_istream* stream) {
    double gain = 1.0;
    int32_t marque = 0;
    if (stream->read(stream, &gain, sizeof(gain)) != sizeof(gain)) return false;
    if (stream->read(stream, &marque, sizeof(marque)) != sizeof(marque)) return false;
    self(plugin)->gain = gain;
    self(plugin)->marque = marque;
    return true;
}

const clap_plugin_state kState = {stateSave, stateLoad};

// --- le plugin ------------------------------------------------------------

bool pluginInit(const clap_plugin*) { return true; }
void pluginDestroy(const clap_plugin* plugin) { delete self(plugin); }
bool pluginActivate(const clap_plugin*, double, uint32_t, uint32_t) { return true; }
void pluginDeactivate(const clap_plugin*) {}
bool pluginStartProcessing(const clap_plugin*) { return true; }
void pluginStopProcessing(const clap_plugin*) {}
void pluginReset(const clap_plugin*) {}

clap_process_status pluginProcess(const clap_plugin* plugin, const clap_process* process) {
    appliquerEvenements(plugin, process->in_events);
    if (process->audio_inputs_count == 0 || process->audio_outputs_count == 0)
        return CLAP_PROCESS_ERROR;

    const auto& entree = process->audio_inputs[0];
    auto& sortie = process->audio_outputs[0];
    const float gain = static_cast<float>(self(plugin)->gain);
    for (uint32_t canal = 0; canal < sortie.channel_count; ++canal) {
        const uint32_t source = canal < entree.channel_count ? canal : 0;
        for (uint32_t i = 0; i < process->frames_count; ++i)
            sortie.data32[canal][i] = -entree.data32[source][i] * gain;
    }
    return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParams;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kState;
    return nullptr;
}

void pluginOnMainThread(const clap_plugin*) {}

// --- fabrique et point d'entrée -------------------------------------------

uint32_t factoryCount(const clap_plugin_factory*) { return 1u; }

const clap_plugin_descriptor* factoryGetDescriptor(const clap_plugin_factory*, uint32_t index) {
    return index == 0 ? &kDescriptor : nullptr;
}

const clap_plugin* factoryCreate(const clap_plugin_factory*, const clap_host*, const char* id) {
    if (id == nullptr || std::strcmp(id, kPluginId) != 0) return nullptr;
    auto* instance = new Instance();
    instance->plugin.desc = &kDescriptor;
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

const clap_plugin_factory kFactory = {factoryCount, factoryGetDescriptor, factoryCreate};

bool entryInit(const char*) { return true; }
void entryDeinit() {}

const void* entryGetFactory(const char* id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry clap_entry = {
    CLAP_VERSION, entryInit, entryDeinit, entryGetFactory,
};
