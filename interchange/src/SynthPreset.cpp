#include "vsm/interchange/SynthPreset.h"
#include "vsm/interchange/MultisampleProfile.h"
#include "vsm/audio/plugin/ISampleLoader.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <sstream>

namespace vsm::interchange {

using vsm::audio::plugin::ISynthPlugin;

float SynthPreset::valueOr(const std::string& semanticId, float fallback) const {
    auto it = values.find(semanticId);
    return it == values.end() ? fallback : it->second;
}

SynthPreset capturePreset(const ISynthPlugin& plugin, const std::string& pluginId, std::string presetName) {
    SynthPreset preset;
    preset.name = std::move(presetName);
    preset.pluginId = pluginId;
    preset.machineName = plugin.machineName();

    const SemanticProfile profile = buildSemanticProfile(pluginId);
    for (const auto& descriptor : profile.parameters()) {
        if (descriptor.semanticId.empty()) continue; // paramètre sans identité : jamais inventé
        preset.values[descriptor.semanticId] = plugin.getParameter(descriptor.paramId);
    }

    // D7.2 : ET CE QUE LA TABLE NE DIT PAS. Vide pour les machines du parc, dont
    // le son EST leur table de paramètres. Une machine tierce, elle, n'a aucun
    // profil sémantique ici : sans cet appel, capturer son preset écrirait un
    // fichier au nom juste et au contenu vide, et rouvrir le morceau donnerait
    // un autre son sans que rien ne le signale.
    preset.nativeState = plugin.saveNativeState();
    return preset;
}

size_t PresetApplyReport::appliedCount() const {
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(),
        [](const Entry& e) { return e.status != SupportStatus::Unsupported; }));
}

size_t PresetApplyReport::unsupportedCount() const {
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(),
        [](const Entry& e) { return e.status == SupportStatus::Unsupported; }));
}

size_t PresetApplyReport::clampedCount() const {
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(),
        [](const Entry& e) { return e.status == SupportStatus::Approximated; }));
}

std::string PresetApplyReport::summary() const {
    std::ostringstream out;
    out << appliedCount() << " paramètre(s) appliqué(s)";
    if (clampedCount() > 0) out << ", " << clampedCount() << " borné(s)";
    if (unsupportedCount() > 0) {
        out << ", " << unsupportedCount() << " non pris en charge : ";
        bool first = true;
        for (const auto& entry : entries) {
            if (entry.status != SupportStatus::Unsupported) continue;
            if (!first) out << ", ";
            out << entry.semanticId;
            first = false;
        }
    }
    if (nativeStateApplied) out << ", état natif reposé";
    if (!nativeStateDetail.empty()) out << ", " << nativeStateDetail;
    return out.str();
}

PresetApplyReport applyPreset(const SynthPreset& preset, ISynthPlugin& plugin,
                               const std::string& targetPluginId) {
    PresetApplyReport report;
    const SemanticProfile profile = buildSemanticProfile(targetPluginId);

    // D7.2 : L'ÉTAT NATIF D'ABORD, LES VALEURS SÉMANTIQUES PAR-DESSUS. L'état
    // natif est l'instantané complet ; les valeurs nommées en sont un extrait,
    // et sont aussi ce qu'un humain ou un script a pu MODIFIER dans le fichier.
    // Les appliquer après, c'est faire gagner ce qui a été écrit exprès.
    //
    // ET SEULEMENT À LA MACHINE D'ORIGINE. Un état natif ne se transpose pas :
    // reposer celui d'un plugin dans un autre serait refusé au mieux, mal
    // interprété au pire. Le refus est DIT, jamais tu.
    if (!preset.nativeState.empty()) {
        if (!preset.pluginId.empty() && preset.pluginId != targetPluginId) {
            report.nativeStateDetail = "état natif de « " + preset.pluginId
                                       + " » non reposé sur « " + targetPluginId + " »";
        } else if (plugin.loadNativeState(preset.nativeState)) {
            report.nativeStateApplied = true;
        } else {
            report.nativeStateDetail = "la machine a refusé son état natif "
                                        "(format ou version incompatible)";
        }
    }

    for (const auto& [semanticId, requested] : preset.values) {
        PresetApplyReport::Entry entry;
        entry.semanticId = semanticId;
        entry.requestedValue = requested;

        const ParameterDescriptor* descriptor = profile.findBySemanticId(semanticId);
        if (descriptor == nullptr) {
            entry.status = SupportStatus::Unsupported;
            entry.detail = "la machine cible n'a pas ce paramètre";
            report.entries.push_back(std::move(entry));
            continue;
        }

        const float clamped = std::clamp(requested, descriptor->minimum, descriptor->maximum);
        plugin.setParameter(descriptor->paramId, clamped);
        entry.appliedValue = clamped;
        // Comparaison d'identité exacte (écrite sans `==` pour -Wfloat-equal) :
        // la question n'est pas "est-ce proche" mais "la valeur demandée a-t-elle
        // dû être modifiée", ce que l'utilisateur doit savoir.
        if (clamped < requested || requested < clamped) {
            entry.status = SupportStatus::Approximated;
            std::ostringstream detail;
            detail << "borné à [" << descriptor->minimum << ", " << descriptor->maximum << "]";
            entry.detail = detail.str();
        }
        report.entries.push_back(std::move(entry));
    }
    return report;
}

JsonValue synthPresetToJson(const SynthPreset& preset) {
    JsonValue root = JsonValue::makeObject();
    root.set("format", JsonValue::makeString(kSynthPresetFormat));
    root.set("version", JsonValue::makeNumber(kSynthPresetVersion));
    root.set("name", JsonValue::makeString(preset.name));
    root.set("pluginId", JsonValue::makeString(preset.pluginId));
    root.set("machineName", JsonValue::makeString(preset.machineName));
    root.set("fidelity", JsonValue::makeString(fidelityName(preset.fidelity)));

    JsonValue parameters = JsonValue::makeObject();
    for (const auto& [semanticId, value] : preset.values)
        parameters.set(semanticId, JsonValue::makeFloat(value));
    root.set("parameters", std::move(parameters));

    // Écrit SEULEMENT s'il y en a : un preset sans échantillon reste
    // exactement le fichier qu'il était avant l'ajout de ce champ.
    if (!preset.samples.empty()) {
        JsonValue samples = JsonValue::makeObject();
        for (const auto& [slot, path] : preset.samples)
            samples.set(std::to_string(slot), JsonValue::makeString(path));
        root.set("samples", std::move(samples));
    }
    if (!preset.nativeState.empty()) {
        // ÉCRIT EN DERNIER, ET SEULEMENT S'IL EXISTE : c'est le seul champ
        // illisible du fichier, et il n'a aucune raison de s'interposer entre
        // le nom de la machine et les réglages qu'un humain vient y relire.
        root.set("nativeState", JsonValue::makeString(preset.nativeState));
        root.set("nativeStateFormat", JsonValue::makeString(
            preset.nativeStateFormat.empty() ? std::string("inconnu") : preset.nativeStateFormat));
    }
    if (!preset.profile.empty()) {
        root.set("profile", JsonValue::makeString(preset.profile));
    }
    return root;
}

namespace {
Fidelity fidelityFromName(const std::string& name) {
    if (name == "measured") return Fidelity::Measured;
    if (name == "derived") return Fidelity::Derived;
    if (name == "estimated") return Fidelity::Estimated;
    if (name == "approximated") return Fidelity::Approximated;
    return Fidelity::Unknown;
}
} // namespace

SynthPresetLoadResult synthPresetFromJson(const JsonValue& json) {
    SynthPresetLoadResult result;
    if (!json.isObject()) { result.error = "racine JSON : objet attendu"; return result; }

    const std::string format = json["format"].asString();
    if (format != kSynthPresetFormat) {
        result.error = "format inattendu : \"" + format + "\" (attendu \"" + kSynthPresetFormat + "\")";
        return result;
    }
    const int version = static_cast<int>(json["version"].asNumber(-1.0));
    if (version != kSynthPresetVersion) {
        // Refus explicite plutôt que lecture optimiste : mieux vaut un message
        // clair qu'un son faux obtenu en interprétant des champs qui ont
        // changé de sens entre deux versions.
        result.error = "version de preset non prise en charge : " + std::to_string(version)
                     + " (cette version du logiciel lit la " + std::to_string(kSynthPresetVersion) + ")";
        return result;
    }

    SynthPreset preset;
    preset.name = json["name"].asString("Sans titre");
    preset.pluginId = json["pluginId"].asString();
    preset.machineName = json["machineName"].asString();
    preset.fidelity = fidelityFromName(json["fidelity"].asString("unknown"));

    const JsonValue& parameters = json["parameters"];
    if (!parameters.isObject()) { result.error = "champ \"parameters\" manquant ou mal typé"; return result; }
    for (const auto& [semanticId, value] : parameters.members()) {
        if (!value.isNumber()) continue; // valeur non numérique : ignorée, jamais devinée
        preset.values[semanticId] = static_cast<float>(value.asNumber());
    }

    // Échantillons : champ facultatif. Absent = preset sans échantillon, ce
    // qui est le cas de toutes les machines sauf celles qui en lisent.
    if (json["profile"].isString()) preset.profile = json["profile"].asString();
    if (json["nativeState"].isString()) preset.nativeState = json["nativeState"].asString();
    if (json["nativeStateFormat"].isString())
        preset.nativeStateFormat = json["nativeStateFormat"].asString();

    const JsonValue& samples = json["samples"];
    if (samples.isObject()) {
        for (const auto& [slotText, path] : samples.members()) {
            if (!path.isString()) continue; // jamais deviné
            const int slot = std::atoi(slotText.c_str());
            if (slot < 0) continue;
            preset.samples[slot] = path.asString();
        }
    }

    result.success = true;
    result.preset = std::move(preset);
    return result;
}

SynthPresetLoadResult parseSynthPreset(const std::string& jsonText) {
    SynthPresetLoadResult result;
    JsonParseResult parsed = parseJson(jsonText);
    if (!parsed.success) {
        result.error = "JSON invalide : " + parsed.error + " (position " + std::to_string(parsed.errorOffset) + ")";
        return result;
    }
    return synthPresetFromJson(parsed.value);
}


std::string SampleLoadReport::summary() const {
    if (empty()) return "aucun échantillon déclaré";
    std::ostringstream out;
    out << loaded.size() << " échantillon(s) chargé(s)";
    if (!failures.empty()) {
        out << ", " << failures.size() << " en échec :";
        for (const auto& failure : failures) out << "\n  - " << failure;
    }
    return out.str();
}

SampleLoadReport applyPresetSamples(const SynthPreset& preset,
                                     vsm::audio::plugin::ISynthPlugin& plugin,
                                     const std::string& baseFolder) {
    SampleLoadReport report;

    // Le PROFIL multi-échantillons passe par la même porte que les
    // échantillons, et pour la même raison : c'est une donnée externe désignée
    // par un chemin relatif, que `setParameter` ne sait pas transporter. Il est
    // traité d'abord, parce qu'une machine à profil n'a pas d'emplacements.
    if (!preset.profile.empty()) {
        auto* bank = dynamic_cast<vsm::audio::plugin::IMultisampleBank*>(&plugin);
        if (bank == nullptr) {
            report.failures.push_back(
                "la machine \"" + preset.pluginId + "\" n'accepte pas de profil "
                "multi-échantillons, « " + preset.profile + " » ignoré");
        } else if (std::filesystem::path(preset.profile).is_absolute()) {
            report.failures.push_back("profil : chemin absolu refusé (« " + preset.profile
                                       + " ») -- un projet doit rester transportable");
        } else {
            // RÉSOLUTION EN TROIS TEMPS, et c'est une décision de format.
            //
            // Un profil de piano pèse deux cents mégaoctets : le recopier dans
            // chaque projet exporté serait absurde, et l'y désigner par un
            // chemin absolu rendrait le projet non transportable. Un projet dit
            // donc soit « le profil qui est DANS mon dossier » (chemin relatif,
            // pour un profil que le projet embarque vraiment), soit « le profil
            // INSTALLÉ qui s'appelle ainsi » -- et l'ouvrir sur une autre
            // machine marche pourvu que la banque y soit installée.
            std::vector<std::string> tentatives;
            std::string resolu;

            // `is_regular_file`, PAS `exists`. Un profil converti depuis un
            // SoundFont s'appelle « X.profile.json » et pose ses échantillons
            // dans un dossier « X » : chercher « X » par `exists` trouvait le
            // DOSSIER, tentait de le lire comme un profil, échouait, et la
            // machine rendait du silence. Trouvé en installant une deuxième
            // banque de piano — la première n'avait pas la collision de noms,
            // et le défaut serait resté invisible.
            const std::string local = (std::filesystem::path(baseFolder) / preset.profile).string();
            tentatives.push_back(local);
            if (std::filesystem::is_regular_file(local)) {
                resolu = local;
            } else {
                const std::string installe =
                    (std::filesystem::path(multisampleProfileFolder()) / preset.profile).string();
                tentatives.push_back(installe);
                if (std::filesystem::is_regular_file(installe)) {
                    resolu = installe;
                } else {
                    // Dernier recours : le NOM déclaré par un profil installé.
                    for (const auto& candidat : installedMultisampleProfiles()) {
                        if (candidat.error.empty() && candidat.name == preset.profile) {
                            resolu = candidat.path;
                            break;
                        }
                    }
                }
            }

            if (resolu.empty()) {
                std::string detail = "profil « " + preset.profile + " » introuvable. Cherché : ";
                for (size_t i = 0; i < tentatives.size(); ++i)
                    detail += (i ? ", " : "") + tentatives[i];
                detail += ", et parmi les profils installés de " + multisampleProfileFolder();
                report.failures.push_back(detail);
            } else {
                const auto applied = applyMultisampleProfile(plugin, resolu);
                if (applied.error.empty())
                    report.loaded.emplace_back(-1, preset.profile);
                else
                    report.failures.push_back("profil (« " + preset.profile + " ») : " + applied.error);
            }
        }
    }

    if (preset.samples.empty()) return report;

    // La capacité de charger des échantillons est une interface À PART : le
    // moteur ne voit qu'un `ISynthPlugin` et n'a rien à savoir de tout ceci.
    auto* loader = dynamic_cast<vsm::audio::plugin::ISampleLoader*>(&plugin);
    if (loader == nullptr) {
        // Preset qui déclare des échantillons pour une machine qui n'en lit
        // pas : SIGNALÉ. C'est le symptôme d'un projet incohérent, et le taire
        // donnerait une piste muette sans explication.
        report.failures.push_back(
            "la machine \"" + preset.pluginId + "\" n'accepte pas d'échantillons, "
            + std::to_string(preset.samples.size()) + " déclaré(s) ignoré(s)");
        return report;
    }

    const std::filesystem::path base(baseFolder);
    for (const auto& [slot, relativePath] : preset.samples) {
        if (slot < 0 || slot >= loader->slotCount()) {
            report.failures.push_back("emplacement " + std::to_string(slot) + " hors bornes (0.."
                                       + std::to_string(loader->slotCount() - 1) + ")");
            continue;
        }
        // Chemin RÉSOLU par rapport au dossier de projet. Un chemin absolu
        // dans le fichier serait accepté par `operator/` mais rendrait le
        // projet non transportable ; on le refuse pour que le défaut se voie
        // au moment où il est introduit, pas chez celui qui reçoit le projet.
        if (std::filesystem::path(relativePath).is_absolute()) {
            report.failures.push_back("emplacement " + std::to_string(slot)
                                       + " : chemin absolu refusé (« " + relativePath
                                       + " ») -- un projet doit rester transportable");
            continue;
        }
        const std::string full = (base / relativePath).string();
        std::string error;
        if (loader->loadSample(slot, full, error))
            report.loaded.emplace_back(slot, relativePath);
        else
            report.failures.push_back("emplacement " + std::to_string(slot) + " (« "
                                       + relativePath + " ») : " + error);
    }
    return report;
}

} // namespace vsm::interchange
