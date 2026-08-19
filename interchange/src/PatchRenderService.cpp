#include "vsm/interchange/PatchRenderService.h"
#include "vsm/interchange/SearchProfile.h"
#include "vsm/interchange/ParameterDescriptor.h"
#include "vsm/audio/io/WavFileWriter.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/ISampleLoader.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <map>
#include <cstdlib>
#include <memory>
#include <mutex>

namespace vsm::interchange {

namespace {

using vsm::audio::plugin::ISynthPlugin;
using vsm::audio::plugin::MidiNoteEvent;

const char* kBase64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeBase64(const uint8_t* data, size_t size) {
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t byte0 = data[i];
        const uint32_t byte1 = (i + 1 < size) ? data[i + 1] : 0;
        const uint32_t byte2 = (i + 2 < size) ? data[i + 2] : 0;
        const uint32_t triple = (byte0 << 16) | (byte1 << 8) | byte2;

        out += kBase64Alphabet[(triple >> 18) & 0x3F];
        out += kBase64Alphabet[(triple >> 12) & 0x3F];
        out += (i + 1 < size) ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=';
        out += (i + 2 < size) ? kBase64Alphabet[triple & 0x3F] : '=';
    }
    return out;
}

} // namespace

JsonValue PatchRenderResponse::toJson() const {
    JsonValue root = JsonValue::makeObject();
    if (!requestId.empty()) root.set("id", JsonValue::makeString(requestId));
    root.set("ok", JsonValue::makeBoolean(ok));
    if (!error.empty()) root.set("error", JsonValue::makeString(error));
    root.set("frames", JsonValue::makeNumber(static_cast<double>(frames)));
    root.set("peak", JsonValue::makeFloat(peak));
    root.set("renderSeconds", JsonValue::makeFloat(static_cast<float>(renderSeconds)));
    if (!audioBase64.empty()) root.set("audio", JsonValue::makeString(audioBase64));
    if (!batchAudioBase64.empty()) {
        JsonValue audios = JsonValue::makeArray();
        for (const auto& un : batchAudioBase64) audios.append(JsonValue::makeString(un));
        root.set("batchAudio", std::move(audios));
        JsonValue pics = JsonValue::makeArray();
        for (float pic : batchPeaks) pics.append(JsonValue::makeFloat(pic));
        root.set("batchPeaks", std::move(pics));
    }
    if (!warnings.empty()) {
        JsonValue list = JsonValue::makeArray();
        for (const auto& warning : warnings) list.append(JsonValue::makeString(warning));
        root.set("warnings", std::move(list));
    }
    return root;
}

PatchRequestParseResult parsePatchRequest(const std::string& jsonLine) {
    PatchRequestParseResult result;
    JsonParseResult parsed = parseJson(jsonLine);
    if (!parsed.success) {
        result.error = "JSON invalide : " + parsed.error;
        return result;
    }
    const JsonValue& json = parsed.value;
    if (!json.isObject()) { result.error = "objet JSON attendu"; return result; }

    PatchRenderRequest request;
    request.requestId = json["id"].isNumber() ? std::to_string(static_cast<long long>(json["id"].asNumber()))
                                               : json["id"].asString();
    request.machineId = json["machine"].asString("vsm.minimoog");
    request.sampleRate = json["sampleRate"].asNumber(48000.0);
    request.durationSeconds = json["duration"].asNumber(1.0);
    request.blockSize = static_cast<int>(json["blockSize"].asNumber(256.0));
    request.outputPath = json["output"].asString();
    request.returnAudio = json["returnAudio"].asString();

    if (request.sampleRate <= 0.0 || request.durationSeconds <= 0.0) {
        result.error = "sampleRate et duration doivent être positifs";
        return result;
    }
    // Garde-fou : une durée démesurée demandée par erreur ferait allouer des
    // gigaoctets et bloquerait le service, sans message utile.
    if (request.durationSeconds > 120.0) {
        result.error = "durée trop longue (max 120 s pour un rendu de patch)";
        return result;
    }

    for (const auto& [semanticId, value] : json["parameters"].members())
        if (value.isNumber()) request.parameters.emplace_back(semanticId, static_cast<float>(value.asNumber()));

    for (const auto& [slotText, path] : json["samples"].members()) {
        if (!path.isString()) continue;
        request.samples.emplace_back(std::atoi(slotText.c_str()), path.asString());
    }

    // Lot : une liste d'objets de paramètres. Les autres champs (machine,
    // notes, durée) valent pour tout le lot.
    for (const auto& entree : json["batch"].elements()) {
        std::vector<std::pair<std::string, float>> jeu;
        for (const auto& [semanticId, value] : entree.members())
            if (value.isNumber()) jeu.emplace_back(semanticId, static_cast<float>(value.asNumber()));
        request.batch.push_back(std::move(jeu));
    }
    if (request.batch.size() > 512) {
        // Garde-fou : un lot démesuré ferait allouer des centaines de
        // mégaoctets de réponse et bloquerait le service sans message utile.
        result.error = "lot trop grand (" + std::to_string(request.batch.size()) + ", maximum 512)";
        return result;
    }

    for (const auto& entry : json["notes"].elements()) {
        PatchNote note;
        note.noteNumber = static_cast<int>(entry["note"].asNumber(60.0));
        note.velocity = static_cast<int>(entry["velocity"].asNumber(100.0));
        note.startSeconds = entry["start"].asNumber(0.0);
        note.durationSeconds = entry["duration"].asNumber(request.durationSeconds * 0.75);
        request.notes.push_back(note);
    }
    // Aucune note : on rend le silence plutôt que de refuser -- c'est utile
    // pour mesurer le bruit de fond d'une machine.

    result.success = true;
    result.request = std::move(request);
    return result;
}

struct PatchRenderService::Impl {
    /// Une instance NEUVE par requête, et c'est délibéré : une machine
    /// réutilisée conserve de l'état (phases d'oscillateurs, dérive analogique
    /// seedée qui a avancé, état des filtres), si bien que deux requêtes
    /// identiques ne donnent pas le même son. Une boucle d'optimisation
    /// comparerait alors ce résidu autant que le patch.
    ///
    /// On a d'abord mis en cache les instances, en supposant que les créer
    /// coûtait cher. Mesure faite : création + initialisation d'un Minimoog,
    /// d'un DX7 ou d'un Jupiter-8 coûte **moins de 0,01 ms**, contre ~10 ms de
    /// rendu. Le cache ne gagnait donc rien et cassait le déterminisme.
    ///
    /// Le profil sémantique, lui, reste en cache : le construire instancie la
    /// machine et parcourt sa table de paramètres.
    std::map<std::string, SemanticProfile> profiles;
    std::vector<float> left, right;
    std::vector<MidiNoteEvent> events;
};

PatchRenderService::PatchRenderService() : impl_(std::make_unique<Impl>()) {
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });
}

PatchRenderService::~PatchRenderService() = default;

namespace {
/// Un rendu, un jeu de paramètres. Renvoie l'audio mono si demandé.
struct SingleRenderOutcome {
    bool ok = false;
    std::string error;
    float peak = 0.0f;
    size_t frames = 0;
    double renderSeconds = 0.0;
    std::string audioBase64;
    std::vector<std::string> warnings;
};
} // namespace

PatchRenderResponse PatchRenderService::render(const PatchRenderRequest& request) {
    // --- LOT (étape 10.1) ---------------------------------------------------
    //
    // Chaque jeu de paramètres reçoit sa PROPRE instance de machine, comme une
    // requête isolée : réutiliser l'instance ferait hériter chaque rendu de
    // l'état du précédent (phases d'oscillateurs, dérive avancée, filtres
    // chargés) et deux lots identiques ne donneraient pas le même son. Le coût
    // d'une création a été mesuré sous 0,01 ms, contre ~7,6 ms de rendu : il
    // n'y a rien à gagner à la mutualiser, et le déterminisme à y perdre.
    if (!request.batch.empty()) {
        PatchRenderResponse groupe;
        groupe.requestId = request.requestId;
        groupe.batchAudioBase64.reserve(request.batch.size());
        groupe.batchPeaks.reserve(request.batch.size());
        for (const auto& jeu : request.batch) {
            PatchRenderRequest unitaire = request;
            unitaire.batch.clear();
            unitaire.parameters = jeu;
            unitaire.outputPath.clear(); // un lot n'écrit pas N fois le même fichier
            const PatchRenderResponse un = render(unitaire);
            if (!un.ok) {
                groupe.error = un.error;
                return groupe;
            }
            groupe.batchAudioBase64.push_back(un.audioBase64);
            groupe.batchPeaks.push_back(un.peak);
            groupe.frames = un.frames;
            groupe.renderSeconds += un.renderSeconds;
            // Les avertissements ne sont rapportés QU'UNE FOIS : ils portent
            // sur la machine et les notes, identiques pour tout le lot, et les
            // répéter N fois noierait le message utile.
            if (groupe.warnings.empty()) groupe.warnings = un.warnings;
        }
        groupe.ok = true;
        return groupe;
    }

    PatchRenderResponse response;
    response.requestId = request.requestId;

    auto plugin = vsm::audio::plugin::PluginRegistry::instance().create(request.machineId);
    if (!plugin) {
        response.error = "machine inconnue : " + request.machineId;
        return response;
    }
    ISynthPlugin& machine = *plugin;
    machine.initialize(request.sampleRate, request.blockSize);

    auto cachedProfile = impl_->profiles.find(request.machineId);
    if (cachedProfile == impl_->profiles.end())
        cachedProfile = impl_->profiles.emplace(request.machineId,
                                                 buildSemanticProfile(request.machineId)).first;
    // Chargement des échantillons AVANT les paramètres : un emplacement vide
    // ignorerait son accord et son enveloppe.
    if (!request.samples.empty()) {
        auto* loader = dynamic_cast<vsm::audio::plugin::ISampleLoader*>(&machine);
        if (loader == nullptr) {
            response.warnings.push_back("cette machine n'accepte pas d'échantillons : "
                                         + request.machineId);
        } else {
            for (const auto& [slot, path] : request.samples) {
                std::string error;
                if (!loader->loadSample(slot, path, error))
                    // Échec SIGNALÉ, jamais remplacé par un autre son : un
                    // fichier manquant doit s'entendre comme un silence
                    // expliqué, pas comme un coup approximatif.
                    response.warnings.push_back("emplacement " + std::to_string(slot) + " : " + error);
            }
        }
    }

    const SemanticProfile& profile = cachedProfile->second;
    for (const auto& [semanticId, value] : request.parameters) {
        const ParameterDescriptor* descriptor = profile.findBySemanticId(semanticId);
        if (!descriptor) {
            response.warnings.push_back("paramètre ignoré (absent de cette machine) : " + semanticId);
            continue;
        }
        const float clamped = std::clamp(value, descriptor->minimum, descriptor->maximum);
        if (clamped < value || value < clamped)
            response.warnings.push_back("valeur bornée : " + semanticId);
        machine.setParameter(descriptor->paramId, clamped);
    }

    const size_t totalFrames =
        static_cast<size_t>(std::llround(request.durationSeconds * request.sampleRate));
    impl_->left.assign(totalFrames, 0.0f);
    impl_->right.assign(totalFrames, 0.0f);

    const auto started = std::chrono::steady_clock::now();
    const int blockSize = std::max(16, request.blockSize);

    for (size_t start = 0; start < totalFrames; start += static_cast<size_t>(blockSize)) {
        const int count = static_cast<int>(std::min<size_t>(static_cast<size_t>(blockSize), totalFrames - start));

        impl_->events.clear();
        const double blockStart = static_cast<double>(start) / request.sampleRate;
        const double blockEnd = static_cast<double>(start + static_cast<size_t>(count)) / request.sampleRate;
        for (const auto& note : request.notes) {
            const double noteOff = note.startSeconds + note.durationSeconds;
            if (note.startSeconds >= blockStart && note.startSeconds < blockEnd) {
                MidiNoteEvent event;
                event.kind = MidiNoteEvent::Kind::NoteOn;
                event.sampleOffset = static_cast<int>(std::llround((note.startSeconds - blockStart) * request.sampleRate));
                event.note = static_cast<uint8_t>(std::clamp(note.noteNumber, 0, 127));
                event.velocity = static_cast<uint8_t>(std::clamp(note.velocity, 1, 127));
                impl_->events.push_back(event);
            }
            if (noteOff >= blockStart && noteOff < blockEnd) {
                MidiNoteEvent event;
                event.kind = MidiNoteEvent::Kind::NoteOff;
                event.sampleOffset = static_cast<int>(std::llround((noteOff - blockStart) * request.sampleRate));
                event.note = static_cast<uint8_t>(std::clamp(note.noteNumber, 0, 127));
                event.velocity = 64;
                impl_->events.push_back(event);
            }
        }

        machine.process(impl_->events.data(), static_cast<int>(impl_->events.size()),
                         impl_->left.data() + start, impl_->right.data() + start, count);
    }

    response.renderSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    for (size_t i = 0; i < totalFrames; ++i)
        response.peak = std::max(response.peak, std::max(std::abs(impl_->left[i]), std::abs(impl_->right[i])));
    response.frames = totalFrames;

    if (!request.outputPath.empty()) {
        try {
            vsm::audio::io::WavFileWriter::writeFile(impl_->left.data(), impl_->right.data(), totalFrames,
                                                      request.sampleRate,
                                                      vsm::audio::io::SampleFormat::Float32,
                                                      request.outputPath);
        } catch (const std::exception& e) {
            response.error = std::string("écriture WAV impossible : ") + e.what();
            return response;
        }
    }

    if (request.returnAudio == "base64-f32-mono") {
        // Somme mono : l'analyse compare des enveloppes et des spectres, la
        // largeur stéréo ne l'intéresse pas -- et cela divise par deux ce qui
        // transite par le tube.
        std::vector<float> mono(totalFrames);
        for (size_t i = 0; i < totalFrames; ++i)
            mono[i] = 0.5f * (impl_->left[i] + impl_->right[i]);
        response.audioBase64 = encodeBase64(reinterpret_cast<const uint8_t*>(mono.data()),
                                             mono.size() * sizeof(float));
    }

    response.ok = true;
    return response;
}

namespace {

/// Requêtes de CONSULTATION, distinctes des requêtes de rendu.
///
/// Pourquoi les mêler au même flux plutôt que d'ouvrir un second canal : le
/// client a besoin du profil de recherche JUSTE AVANT de lancer sa boucle, sur
/// la même machine et dans le même processus. Un second mécanisme (fichier
/// annexe, second exécutable) introduirait un risque de désaccord entre ce que
/// le client croit chercher et ce que le moteur sait rendre.
///
/// Renvoie false si la ligne n'est pas une consultation -- elle est alors
/// traitée comme un rendu.
bool handleQuery(const JsonValue& json, std::ostream& output) {
    const std::string query = json["query"].asString();
    if (query.empty()) return false;

    JsonValue reply = JsonValue::makeObject();
    if (json["id"].isNumber())
        reply.set("id", JsonValue::makeString(std::to_string(static_cast<long long>(json["id"].asNumber()))));
    else if (!json["id"].asString().empty())
        reply.set("id", JsonValue::makeString(json["id"].asString()));

    if (query == "machines") {
        JsonValue list = JsonValue::makeArray();
        for (const auto& id : knownSemanticPluginIds())
            if (id.rfind("vsm.", 0) == 0) list.append(JsonValue::makeString(id));
        reply.set("ok", JsonValue::makeBoolean(true));
        reply.set("machines", std::move(list));
    } else if (query == "searchProfile") {
        const std::string machine = json["machine"].asString();
        const SearchProfile profile = buildSearchProfile(machine);
        if (profile.empty()) {
            // Machine inconnue : REFUSÉE avec un message. Renvoyer un profil
            // vide laisserait le client chercher dans le vide sans le savoir.
            reply.set("ok", JsonValue::makeBoolean(false));
            reply.set("error", JsonValue::makeString(
                "aucun profil de recherche pour « " + machine + " »"));
        } else {
            reply.set("ok", JsonValue::makeBoolean(true));
            JsonValue body = profile.toJson();
            reply.set("machine", JsonValue::makeString(profile.pluginId()));
            reply.set("dimensions", body["dimensions"]);
        }
    } else if (query == "parameters") {
        const std::string machine = json["machine"].asString();
        const SemanticProfile profile = buildSemanticProfile(machine);
        if (profile.empty()) {
            reply.set("ok", JsonValue::makeBoolean(false));
            reply.set("error", JsonValue::makeString("machine inconnue : « " + machine + " »"));
        } else {
            JsonValue list = JsonValue::makeArray();
            for (const auto& parameter : profile.parameters()) {
                if (parameter.semanticId.empty()) continue;
                JsonValue entry = JsonValue::makeObject();
                entry.set("id", JsonValue::makeString(parameter.semanticId));
                entry.set("name", JsonValue::makeString(parameter.displayName));
                entry.set("min", JsonValue::makeFloat(parameter.minimum));
                entry.set("max", JsonValue::makeFloat(parameter.maximum));
                entry.set("default", JsonValue::makeFloat(parameter.defaultValue));
                if (!parameter.unit.empty()) entry.set("unit", JsonValue::makeString(parameter.unit));
                list.append(std::move(entry));
            }
            reply.set("ok", JsonValue::makeBoolean(true));
            reply.set("machine", JsonValue::makeString(profile.pluginId()));
            reply.set("parameters", std::move(list));
        }
    } else {
        reply.set("ok", JsonValue::makeBoolean(false));
        reply.set("error", JsonValue::makeString("consultation inconnue : « " + query + " »"));
    }

    output << reply.toString(-1) << "\n";
    output.flush();
    return true;
}

} // namespace

int runPatchRenderLoop(std::istream& input, std::ostream& output, std::ostream& diagnostics) {
    PatchRenderService service;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;

        // Une consultation est reconnue à son champ « query ». Elle est
        // traitée avant l'analyse de requête de rendu, qui exigerait des
        // champs qu'une consultation n'a pas.
        JsonParseResult probe = parseJson(line);
        if (probe.success && probe.value.isObject() && handleQuery(probe.value, output)) continue;

        PatchRequestParseResult parsed = parsePatchRequest(line);
        PatchRenderResponse response;
        if (!parsed.success) {
            response.ok = false;
            response.error = parsed.error;
        } else {
            response = service.render(parsed.request);
        }
        // UNE réponse par ligne, sans indentation : le client lit ligne à
        // ligne, il ne doit jamais avoir à deviner où finit un objet.
        output << response.toJson().toString(-1) << "\n";
        output.flush();
        if (!response.error.empty()) diagnostics << "vsm-render : " << response.error << "\n";
    }
    return 0;
}

} // namespace vsm::interchange
