#include "vsm/interchange/ClapParameterIds.h"

namespace vsm::interchange {

uint32_t clapParameterId(const std::string& semanticId) {
    // FNV-1a 64 bits : simple, sans état, parfaitement reproductible d'une
    // plateforme et d'un compilateur à l'autre -- ce qui compte davantage ici
    // que la qualité statistique, puisque l'absence de collision est vérifiée
    // par test sur l'ensemble réel des paramètres plutôt qu'espérée.
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : semanticId) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    // Repli sur 31 bits : garde l'identifiant hors de CLAP_INVALID_ID
    // (0xFFFFFFFF) et à l'abri des hôtes qui les traitent en entier signé.
    return static_cast<uint32_t>((hash ^ (hash >> 32)) & 0x7FFFFFFFu);
}

std::vector<ClapParameterMapping> clapParameterMap(const std::string& pluginId) {
    std::vector<ClapParameterMapping> mappings;
    const SemanticProfile profile = buildSemanticProfile(pluginId);
    mappings.reserve(profile.parameters().size());

    for (const auto& descriptor : profile.parameters()) {
        if (descriptor.semanticId.empty()) continue; // jamais d'id inventé
        ClapParameterMapping mapping;
        mapping.clapId = clapParameterId(descriptor.semanticId);
        mapping.semanticId = descriptor.semanticId;
        mapping.displayName = descriptor.displayName;
        mapping.module = descriptor.module;
        mapping.vsmParamId = descriptor.paramId;
        mapping.minimum = descriptor.minimum;
        mapping.maximum = descriptor.maximum;
        mapping.defaultValue = descriptor.defaultValue;
        mappings.push_back(std::move(mapping));
    }
    return mappings;
}

std::string clapPluginId(const std::string& vsmPluginId) {
    const std::string prefix = "vsm.";
    const std::string bare = vsmPluginId.rfind(prefix, 0) == 0 ? vsmPluginId.substr(prefix.size())
                                                                : vsmPluginId;
    return "com.vsmstudio." + bare;
}

} // namespace vsm::interchange
