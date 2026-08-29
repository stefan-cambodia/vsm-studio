#include "vsm/interchange/EffectDescription.h"
#include "vsm/interchange/ParameterDescriptor.h"
#include <algorithm>

namespace vsm::interchange {

using vsm::sequencer::TrackEffect;

namespace {

/// Le nom sous lequel un paramètre est écrit sur le disque : son identité
/// sémantique quand elle existe, son nom affiché sinon.
///
/// LE REPLI EST DÉLIBÉRÉ, ET IL EST SYMÉTRIQUE. Un effet dont la table
/// sémantique n'aurait pas encore été écrite doit tout de même pouvoir être
/// sauvegardé et rechargé : refuser reviendrait à perdre le réglage, ce que ce
/// travail-ci a précisément pour but d'empêcher. Écriture et lecture passent
/// toutes deux par cette fonction, donc l'aller-retour tient dans les deux cas.
std::string parameterKey(const SemanticProfile& profile,
                          const vsm::audio::plugin::ParameterInfo& info) {
    if (const auto* descriptor = profile.findByParamId(info.id))
        if (!descriptor->semanticId.empty()) return descriptor->semanticId;
    return info.name;
}

} // namespace

std::string effectSemanticPluginId(const std::string& factoryTypeId) {
    return "fx." + factoryTypeId;
}

TrackEffect describeEffect(const std::string& factoryTypeId,
                            const vsm::audio::effect::IAudioEffect& effect) {
    TrackEffect described;
    described.type = factoryTypeId;
    const SemanticProfile profile = buildSemanticProfile(effectSemanticPluginId(factoryTypeId));
    for (const auto& info : effect.parameterList())
        described.parameters[parameterKey(profile, info)] = effect.getParameter(info.id);
    // D7.3 : ET CE QUE LA TABLE NE DIT PAS. Vide pour les effets internes ; un
    // effet tiers, lui, n'a aucun profil sémantique ici, si bien que sans cet
    // appel sa description serait un type et rien d'autre.
    described.nativeState = effect.saveNativeState();
    return described;
}

EffectApplyReport applyEffectDescription(const TrackEffect& described,
                                          vsm::audio::effect::IAudioEffect& effect) {
    EffectApplyReport report;
    const SemanticProfile profile = buildSemanticProfile(effectSemanticPluginId(described.type));

    // D7.3 : L'ÉTAT NATIF D'ABORD, LES VALEURS NOMMÉES PAR-DESSUS. Même ordre
    // et même raison que pour les machines (voir `applyPreset`) : l'état natif
    // est l'instantané complet, les valeurs nommées en sont l'extrait qu'un
    // humain ou un script a pu modifier exprès.
    if (!described.nativeState.empty())
        report.nativeStateApplied = effect.loadNativeState(described.nativeState);

    // On parcourt les paramètres de L'EFFET, pas ceux de la description : c'est
    // ce qui permet de nommer ensuite ce que la description contenait en trop.
    std::vector<std::string> connus;
    for (const auto& info : effect.parameterList()) {
        const std::string key = parameterKey(profile, info);
        connus.push_back(key);
        const auto found = described.parameters.find(key);
        if (found == described.parameters.end()) continue;
        effect.setParameter(info.id, found->second);
        ++report.applied;
    }

    for (const auto& [key, value] : described.parameters) {
        (void)value;
        if (std::find(connus.begin(), connus.end(), key) == connus.end())
            report.unknownParameters.push_back(key);
    }
    return report;
}

std::map<std::string, float> describeMasterBus(const vsm::audio::engine::MasterBus& bus) {
    std::map<std::string, float> described;
    for (const auto& info : bus.parameterList())
        described[info.name] = bus.getParameter(info.id);
    return described;
}

int applyMasterDescription(const std::map<std::string, float>& described,
                            vsm::audio::engine::MasterBus& bus) {
    int applied = 0;
    for (const auto& info : bus.parameterList()) {
        const auto found = described.find(info.name);
        if (found == described.end()) continue;
        bus.setParameter(info.id, found->second);
        ++applied;
    }
    return applied;
}

} // namespace vsm::interchange
