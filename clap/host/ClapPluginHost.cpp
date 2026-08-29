#include "ClapPluginHost.h"
#include "vsm/audio/plugin/PluginRegistry.h"

#include <clap/clap.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace vsm::clap {

namespace {

using vsm::audio::plugin::ISynthPlugin;
using vsm::audio::plugin::MidiNoteEvent;
using vsm::audio::plugin::ParameterList;
using vsm::audio::plugin::ParamId;
using vsm::audio::plugin::PresetState;

/// Module dynamique chargé, avec son point d'entrée CLAP. Sa durée de vie est
/// partagée : tant qu'une instance de plugin existe, la bibliothèque doit
/// rester chargée -- la décharger avant détruirait le code en cours d'exécution.
class LoadedModule {
public:
    static std::shared_ptr<LoadedModule> load(const std::string& path, std::string& outError) {
        auto module = std::shared_ptr<LoadedModule>(new LoadedModule());
        module->path_ = path;

#if defined(_WIN32)
        module->handle_ = static_cast<void*>(LoadLibraryA(path.c_str()));
        if (!module->handle_) { outError = "chargement impossible : " + path; return nullptr; }
        auto* entry = reinterpret_cast<const clap_plugin_entry*>(
            GetProcAddress(static_cast<HMODULE>(module->handle_), "clap_entry"));
#else
        module->handle_ = dlopen(path.c_str(), RTLD_LOCAL | RTLD_NOW);
        if (!module->handle_) {
            const char* reason = dlerror();
            outError = std::string("chargement impossible : ") + (reason ? reason : path);
            return nullptr;
        }
        auto* entry = reinterpret_cast<const clap_plugin_entry*>(dlsym(module->handle_, "clap_entry"));
#endif
        if (!entry) { outError = "symbole clap_entry introuvable dans " + path; return nullptr; }
        if (!clap_version_is_compatible(entry->clap_version)) {
            outError = "version CLAP incompatible dans " + path;
            return nullptr;
        }
        if (!entry->init(path.c_str())) { outError = "clap_entry->init a échoué pour " + path; return nullptr; }

        module->entry_ = entry;
        module->factory_ = static_cast<const clap_plugin_factory*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        if (!module->factory_) { outError = "fabrique de plugins absente dans " + path; return nullptr; }
        return module;
    }

    ~LoadedModule() {
        if (entry_) entry_->deinit();
        if (!handle_) return;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
    }

    LoadedModule(const LoadedModule&) = delete;
    LoadedModule& operator=(const LoadedModule&) = delete;

    const clap_plugin_factory* factory() const { return factory_; }

private:
    LoadedModule() = default;
    std::string path_;
    void* handle_ = nullptr;
    const clap_plugin_entry* entry_ = nullptr;
    const clap_plugin_factory* factory_ = nullptr;
};

/// Liste d'événements CLAP construite au-dessus d'un vecteur pré-alloué.
/// CLAP passe les événements par une interface à fonctions virtuelles : on la
/// remplit sans allouer, condition pour rester utilisable sur le thread audio.
struct EventList {
    std::vector<clap_event_note> notes;
    std::vector<const clap_event_header*> headers;
    clap_input_events input{};

    void prepare(size_t capacity) {
        notes.resize(capacity);
        headers.resize(capacity);
        count = 0;
        input.ctx = this;
        input.size = [](const clap_input_events* list) -> uint32_t {
            return static_cast<const EventList*>(list->ctx)->count;
        };
        input.get = [](const clap_input_events* list, uint32_t index) -> const clap_event_header* {
            const auto* self = static_cast<const EventList*>(list->ctx);
            return index < self->count ? self->headers[index] : nullptr;
        };
    }

    void clear() { count = 0; }

    void push(const MidiNoteEvent& event) {
        if (count >= notes.size()) return; // saturation : on perd l'événement plutôt que d'allouer
        clap_event_note& note = notes[count];
        std::memset(&note, 0, sizeof(note));
        note.header.size = sizeof(clap_event_note);
        note.header.time = static_cast<uint32_t>(std::max(0, event.sampleOffset));
        note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        note.header.type = (event.kind == MidiNoteEvent::Kind::NoteOn) ? CLAP_EVENT_NOTE_ON
                                                                       : CLAP_EVENT_NOTE_OFF;
        note.header.flags = 0;
        note.note_id = -1;
        note.port_index = 0;
        note.channel = static_cast<int16_t>(event.channel);
        note.key = static_cast<int16_t>(event.note);
        note.velocity = static_cast<double>(event.velocity) / 127.0;
        headers[count] = &note.header;
        ++count;
    }

    uint32_t count = 0;
};

/// Sortie d'événements minimale : le plugin peut nous renvoyer des choses
/// (changements de paramètres, notes), qu'on ignore pour l'instant -- mais il
/// faut lui fournir une interface valide, sans quoi certains plugins refusent
/// de traiter.
struct OutputEvents {
    clap_output_events output{};
    void prepare() {
        output.ctx = this;
        output.try_push = [](const clap_output_events*, const clap_event_header*) -> bool { return true; };
    }
};

/// Un plugin CLAP présenté au moteur VSM comme n'importe quelle machine native.
class ClapInstrument : public ISynthPlugin {
public:
    ClapInstrument(std::shared_ptr<LoadedModule> module, const clap_plugin* plugin, std::string name)
        : module_(std::move(module)), plugin_(plugin), name_(std::move(name)) {}

    ~ClapInstrument() override {
        if (!plugin_) return;
        if (activated_) { plugin_->stop_processing(plugin_); plugin_->deactivate(plugin_); }
        plugin_->destroy(plugin_);
    }

    bool prepareExtensions() {
        params_ = static_cast<const clap_plugin_params*>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
        state_ = static_cast<const clap_plugin_state*>(plugin_->get_extension(plugin_, CLAP_EXT_STATE));
        buildParameterList();
        return true;
    }

    void initialize(double sampleRate, int maxBlockSize) override {
        if (activated_) { plugin_->stop_processing(plugin_); plugin_->deactivate(plugin_); activated_ = false; }
        const uint32_t frames = static_cast<uint32_t>(std::max(1, maxBlockSize));
        if (!plugin_->activate(plugin_, sampleRate, 1, frames)) return;
        plugin_->start_processing(plugin_);
        activated_ = true;

        events_.prepare(256);
        outputs_.prepare();
        silence_.assign(frames, 0.0f);
        maxFrames_ = frames;
    }

    void process(const MidiNoteEvent* events, int numEvents,
                  float* outputL, float* outputR, int numSamples) override {
        std::fill(outputL, outputL + numSamples, 0.0f);
        std::fill(outputR, outputR + numSamples, 0.0f);
        if (!activated_) return;

        events_.clear();
        for (int i = 0; i < numEvents; ++i) events_.push(events[i]);

        float* channels[2] = { outputL, outputR };
        clap_audio_buffer outputBuffer{};
        outputBuffer.data32 = channels;
        outputBuffer.channel_count = 2;

        clap_process process{};
        process.steady_time = steadyTime_;
        process.frames_count = static_cast<uint32_t>(numSamples);
        process.audio_inputs = nullptr;
        process.audio_inputs_count = 0;
        process.audio_outputs = &outputBuffer;
        process.audio_outputs_count = 1;
        process.in_events = &events_.input;
        process.out_events = &outputs_.output;

        plugin_->process(plugin_, &process);
        steadyTime_ += numSamples;
    }

    void setParameter(ParamId id, float value) override {
        // CLAP ne permet pas d'écrire un paramètre hors du flux d'événements ;
        // `params->flush()` est prévu exactement pour ça (hors traitement).
        if (!params_) return;
        pendingValues_[id] = value;
        flushPending();
    }

    float getParameter(ParamId id) const override {
        if (!params_) return 0.0f;
        double value = 0.0;
        if (params_->get_value(plugin_, id, &value)) return static_cast<float>(value);
        auto it = pendingValues_.find(id);
        return it == pendingValues_.end() ? 0.0f : it->second;
    }

    const ParameterList& parameterList() const override { return parameters_; }

    PresetState saveState() const override {
        PresetState state;
        state.pluginTypeId = pluginId_;
        for (const auto& info : parameters_)
            state.parameterValues[info.id] = getParameter(info.id);
        return state;
    }

    void loadState(const PresetState& state) override {
        for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
    }

    const char* machineName() const override { return name_.c_str(); }

    /// CLAP n'expose pas de compte de voix : renvoyer 0 est la seule réponse
    /// honnête (l'affichage de charge le traitera comme « inconnu » plutôt que
    /// d'inventer un chiffre).
    int activeVoiceCount() const override { return 0; }

    void setPluginId(std::string id) { pluginId_ = std::move(id); }

private:
    void buildParameterList() {
        parameters_.clear();
        if (!params_) return;
        const uint32_t count = params_->count(plugin_);
        for (uint32_t i = 0; i < count; ++i) {
            clap_param_info info{};
            if (!params_->get_info(plugin_, i, &info)) continue;
            vsm::audio::plugin::ParameterInfo entry;
            entry.id = info.id;
            entry.name = info.name;
            entry.minValue = static_cast<float>(info.min_value);
            entry.maxValue = static_cast<float>(info.max_value);
            entry.defaultValue = static_cast<float>(info.default_value);
            parameters_.push_back(std::move(entry));
        }
    }

    void flushPending() const {
        if (!params_ || pendingValues_.empty()) return;
        EventList list;
        list.prepare(pendingValues_.size() + 1);
        std::vector<clap_event_param_value> values(pendingValues_.size());
        std::vector<const clap_event_header*> headers;
        headers.reserve(pendingValues_.size());

        size_t index = 0;
        for (const auto& [id, value] : pendingValues_) {
            clap_event_param_value& event = values[index];
            std::memset(&event, 0, sizeof(event));
            event.header.size = sizeof(clap_event_param_value);
            event.header.time = 0;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = CLAP_EVENT_PARAM_VALUE;
            event.param_id = id;
            event.cookie = nullptr;
            event.note_id = -1;
            event.port_index = -1;
            event.channel = -1;
            event.key = -1;
            event.value = static_cast<double>(value);
            headers.push_back(&event.header);
            ++index;
        }

        struct Bridge { const std::vector<const clap_event_header*>* headers; };
        Bridge bridge{&headers};
        clap_input_events input{};
        input.ctx = &bridge;
        input.size = [](const clap_input_events* self) -> uint32_t {
            return static_cast<uint32_t>(static_cast<const Bridge*>(self->ctx)->headers->size());
        };
        input.get = [](const clap_input_events* self, uint32_t i) -> const clap_event_header* {
            const auto* headerList = static_cast<const Bridge*>(self->ctx)->headers;
            return i < headerList->size() ? (*headerList)[i] : nullptr;
        };

        OutputEvents out;
        out.prepare();
        params_->flush(plugin_, &input, &out.output);
        pendingValues_.clear();
    }

    std::shared_ptr<LoadedModule> module_;
    const clap_plugin* plugin_ = nullptr;
    std::string name_;
    std::string pluginId_;
    const clap_plugin_params* params_ = nullptr;
    const clap_plugin_state* state_ = nullptr;

public:
    /// Accès à l'extension d'état, pour les fonctions libres ci-dessous.
    /// Publique parce qu'un `dynamic_cast` depuis l'extérieur ne peut pas
    /// atteindre un membre privé -- et qu'ouvrir cet accès est moins coûteux
    /// que de dupliquer la logique de chargement.
    const clap_plugin_state* stateExtension() const { return state_; }
    const clap_plugin* rawPlugin() const { return plugin_; }

private:
    ParameterList parameters_;
    mutable std::map<ParamId, float> pendingValues_;
    EventList events_;
    OutputEvents outputs_;
    std::vector<float> silence_;
    uint32_t maxFrames_ = 512;
    int64_t steadyTime_ = 0;
    bool activated_ = false;
};

/// Hôte minimal mais conforme : les plugins interrogent ces champs dès
/// `create()`, et plusieurs refusent de s'instancier si l'hôte ment sur sa
/// version ou ne fournit pas les rappels de base.
const clap_host& minimalHost() {
    static clap_host host = [] {
        clap_host h{};
        h.clap_version = CLAP_VERSION;
        h.host_data = nullptr;
        h.name = "VSM Studio";
        h.vendor = "VSM Studio";
        h.url = "";
        h.version = "0.1.0";
        h.get_extension = [](const clap_host*, const char*) -> const void* { return nullptr; };
        h.request_restart = [](const clap_host*) {};
        h.request_process = [](const clap_host*) {};
        h.request_callback = [](const clap_host*) {};
        return h;
    }();
    return host;
}

} // namespace

std::vector<ClapPluginInfo> scanClapFile(const std::string& clapFilePath, std::string& outError) {
    std::vector<ClapPluginInfo> found;
    auto module = LoadedModule::load(clapFilePath, outError);
    if (!module) return found;

    const clap_plugin_factory* factory = module->factory();
    const uint32_t count = factory->get_plugin_count(factory);
    for (uint32_t i = 0; i < count; ++i) {
        const clap_plugin_descriptor* descriptor = factory->get_plugin_descriptor(factory, i);
        if (!descriptor) continue;
        ClapPluginInfo info;
        info.id = descriptor->id ? descriptor->id : "";
        info.name = descriptor->name ? descriptor->name : "";
        info.vendor = descriptor->vendor ? descriptor->vendor : "";
        info.version = descriptor->version ? descriptor->version : "";
        found.push_back(std::move(info));
    }
    return found;
}

vsm::audio::plugin::SynthPluginPtr createClapInstrument(const std::string& clapFilePath,
                                                         const std::string& pluginId,
                                                         std::string& outError) {
    auto module = LoadedModule::load(clapFilePath, outError);
    if (!module) return nullptr;

    const clap_plugin_factory* factory = module->factory();
    const uint32_t count = factory->get_plugin_count(factory);
    if (count == 0) { outError = "aucun plugin dans " + clapFilePath; return nullptr; }

    const clap_plugin_descriptor* chosen = nullptr;
    for (uint32_t i = 0; i < count && !chosen; ++i) {
        const clap_plugin_descriptor* descriptor = factory->get_plugin_descriptor(factory, i);
        if (!descriptor) continue;
        if (pluginId.empty() || (descriptor->id && pluginId == descriptor->id)) chosen = descriptor;
    }
    if (!chosen) { outError = "plugin \"" + pluginId + "\" absent de " + clapFilePath; return nullptr; }

    const clap_plugin* plugin = factory->create_plugin(factory, &minimalHost(), chosen->id);
    if (!plugin) { outError = "instanciation refusée par le plugin"; return nullptr; }
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        outError = "init() du plugin a échoué";
        return nullptr;
    }

    auto instrument = std::make_shared<ClapInstrument>(std::move(module), plugin,
                                                        chosen->name ? chosen->name : "CLAP");
    instrument->setPluginId(chosen->id ? chosen->id : "");
    instrument->prepareExtensions();
    return instrument;
}

namespace {

/// Flux d'écriture CLAP au-dessus d'une chaîne.
struct StringOutStream {
    clap_ostream stream{};
    std::string* text = nullptr;
};

int64_t writeToString(const clap_ostream* stream, const void* buffer, uint64_t size) {
    auto* holder = static_cast<const StringOutStream*>(stream->ctx);
    holder->text->append(static_cast<const char*>(buffer), static_cast<size_t>(size));
    return static_cast<int64_t>(size);
}

/// Flux de lecture CLAP au-dessus d'une chaîne.
struct StringInStream {
    clap_istream stream{};
    const std::string* text = nullptr;
    size_t position = 0;
};

int64_t readFromString(const clap_istream* stream, void* buffer, uint64_t size) {
    auto* holder = static_cast<StringInStream*>(stream->ctx);
    const size_t reste = holder->text->size() - holder->position;
    const size_t lu = std::min(static_cast<size_t>(size), reste);
    std::memcpy(buffer, holder->text->data() + holder->position, lu);
    holder->position += lu;
    return static_cast<int64_t>(lu);
}

const ClapInstrument* asClapInstrument(vsm::audio::plugin::ISynthPlugin& instrument,
                                        std::string& outError) {
    const auto* clapInstrument = dynamic_cast<const ClapInstrument*>(&instrument);
    if (clapInstrument == nullptr) {
        outError = "cet instrument n'est pas un plugin CLAP";
        return nullptr;
    }
    if (clapInstrument->stateExtension() == nullptr) {
        outError = "ce plugin CLAP n'expose pas l'extension d'état";
        return nullptr;
    }
    return clapInstrument;
}

} // namespace

bool saveClapState(vsm::audio::plugin::ISynthPlugin& instrument,
                    std::string& outText, std::string& outError) {
    const auto* clapInstrument = asClapInstrument(instrument, outError);
    if (clapInstrument == nullptr) return false;

    outText.clear();
    StringOutStream holder;
    holder.text = &outText;
    holder.stream.ctx = &holder;
    holder.stream.write = writeToString;
    if (!clapInstrument->stateExtension()->save(clapInstrument->rawPlugin(), &holder.stream)) {
        outError = "le plugin a refusé d'écrire son état";
        return false;
    }
    return true;
}

bool loadClapState(vsm::audio::plugin::ISynthPlugin& instrument,
                    const std::string& text, std::string& outError) {
    const auto* clapInstrument = asClapInstrument(instrument, outError);
    if (clapInstrument == nullptr) return false;

    StringInStream holder;
    holder.text = &text;
    holder.stream.ctx = &holder;
    holder.stream.read = readFromString;
    if (!clapInstrument->stateExtension()->load(clapInstrument->rawPlugin(), &holder.stream)) {
        // Échec SIGNALÉ : un état refusé laisse la machine sur ses réglages
        // précédents, ce qui produirait un son faux sans prévenir.
        outError = "le plugin a refusé cet état (format ou version incompatible)";
        return false;
    }
    return true;
}

// --- D7.1 : branchement dans le registre de machines ------------------------

namespace {
constexpr const char* kClapPrefix = "clap:";
}

std::string clapInstrumentId(const std::string& clapFilePath, const std::string& pluginId) {
    return std::string(kClapPrefix) + clapFilePath + "#" + pluginId;
}

bool parseClapInstrumentId(const std::string& instrumentId, std::string& outFilePath,
                            std::string& outPluginId) {
    const std::string prefixe = kClapPrefix;
    if (instrumentId.rfind(prefixe, 0) != 0) return false;
    const std::string reste = instrumentId.substr(prefixe.size());
    // LE SÉPARATEUR SE CHERCHE PAR LA FIN : un chemin de fichier peut contenir
    // un « # », un identifiant de plugin CLAP est un nom pointé qui n'en
    // contient pas. Chercher par le début couperait le chemin au mauvais
    // endroit sur la machine de quelqu'un d'autre.
    const size_t diese = reste.rfind('#');
    if (diese == std::string::npos) {
        outFilePath = reste;
        outPluginId.clear();
        return !outFilePath.empty();
    }
    outFilePath = reste.substr(0, diese);
    outPluginId = reste.substr(diese + 1);
    return !outFilePath.empty();
}

void installClapResolver() {
    vsm::audio::plugin::PluginRegistry::instance().setExternalResolver(
        [](const std::string& id) -> vsm::audio::plugin::SynthPluginPtr {
            std::string chemin, pluginId;
            if (!parseClapInstrumentId(id, chemin, pluginId)) return nullptr;
            // L'ERREUR EST AVALÉE ICI, et une seule fois : le registre ne rend
            // qu'un pointeur, et l'appelant a déjà tout ce qu'il faut pour
            // dire « instrument absent » sans le remplacer. Faire remonter le
            // détail obligerait à changer la signature que trente-quatre
            // machines partagent, pour un cas sur trente-cinq.
            std::string erreur;
            return createClapInstrument(chemin, pluginId, erreur);
        });
}

} // namespace vsm::clap
