#include "vsm/interchange/PluginCatalogue.h"
#include <algorithm>
#include <sstream>

namespace vsm::interchange {
namespace {

/// Les tabulations et les retours à la ligne sont les seuls caractères que le
/// protocole ne supporte pas : ils sont remplacés, jamais échappés. Un
/// échappement demanderait un désamorçage symétrique, donc deux occasions de se
/// tromper, pour des noms de plugins qui n'en contiennent pas.
std::string sansSeparateur(const std::string& texte) {
    std::string propre = texte;
    for (char& c : propre)
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    return propre;
}

} // namespace

std::string CataloguedPlugin::instrumentId() const {
    return format + ":" + path + "#" + id;
}

bool PluginCatalogue::alreadyKnown(const std::string& path) const {
    for (const auto& plugin : plugins)
        if (plugin.path == path) return true;
    for (const auto& fautif : faulty)
        if (fautif.path == path) return true;
    return false;
}

std::vector<CataloguedPlugin> PluginCatalogue::instruments() const {
    std::vector<CataloguedPlugin> resultat;
    for (const auto& plugin : plugins)
        if (plugin.isInstrument) resultat.push_back(plugin);
    std::sort(resultat.begin(), resultat.end(),
               [](const CataloguedPlugin& a, const CataloguedPlugin& b) { return a.name < b.name; });
    return resultat;
}

std::vector<CataloguedPlugin> PluginCatalogue::effects() const {
    std::vector<CataloguedPlugin> resultat;
    for (const auto& plugin : plugins)
        if (!plugin.isInstrument) resultat.push_back(plugin);
    std::sort(resultat.begin(), resultat.end(),
               [](const CataloguedPlugin& a, const CataloguedPlugin& b) { return a.name < b.name; });
    return resultat;
}

JsonValue pluginCatalogueToJson(const PluginCatalogue& catalogue) {
    JsonValue root = JsonValue::makeObject();
    root.set("format", JsonValue::makeString(kPluginCatalogueFormat));
    root.set("version", JsonValue::makeNumber(kPluginCatalogueVersion));

    JsonValue plugins = JsonValue::makeArray();
    for (const auto& plugin : catalogue.plugins) {
        JsonValue entree = JsonValue::makeObject();
        entree.set("format", JsonValue::makeString(plugin.format));
        entree.set("path", JsonValue::makeString(plugin.path));
        entree.set("id", JsonValue::makeString(plugin.id));
        entree.set("name", JsonValue::makeString(plugin.name));
        entree.set("vendor", JsonValue::makeString(plugin.vendor));
        entree.set("isInstrument", JsonValue::makeBoolean(plugin.isInstrument));
        plugins.append(std::move(entree));
    }
    root.set("plugins", std::move(plugins));

    JsonValue fautifs = JsonValue::makeArray();
    for (const auto& fautif : catalogue.faulty) {
        JsonValue entree = JsonValue::makeObject();
        entree.set("path", JsonValue::makeString(fautif.path));
        entree.set("reason", JsonValue::makeString(fautif.reason));
        fautifs.append(std::move(entree));
    }
    root.set("faulty", std::move(fautifs));
    return root;
}

PluginCatalogueLoadResult parsePluginCatalogue(const std::string& jsonText) {
    PluginCatalogueLoadResult result;
    const JsonParseResult parsed = parseJson(jsonText);
    if (!parsed.success) {
        result.error = parsed.error;
        return result;
    }
    const JsonValue& root = parsed.value;
    if (root["format"].asString() != kPluginCatalogueFormat) {
        // REFUSÉ PLUTÔT QUE LU AU PETIT BONHEUR : un catalogue mal interprété
        // proposerait des plugins qui n'existent pas, et l'utilisateur
        // découvrirait l'erreur en essayant d'en charger un.
        result.error = "ce fichier n'est pas un catalogue de plugins VSM";
        return result;
    }

    for (const auto& entree : root["plugins"].elements()) {
        CataloguedPlugin plugin;
        plugin.format = entree["format"].asString();
        plugin.path = entree["path"].asString();
        plugin.id = entree["id"].asString();
        plugin.name = entree["name"].asString();
        plugin.vendor = entree["vendor"].asString();
        plugin.isInstrument = entree["isInstrument"].asBoolean(false);
        if (plugin.path.empty() || plugin.format.empty()) continue;
        result.catalogue.plugins.push_back(std::move(plugin));
    }
    for (const auto& entree : root["faulty"].elements()) {
        FaultyPlugin fautif;
        fautif.path = entree["path"].asString();
        fautif.reason = entree["reason"].asString();
        if (!fautif.path.empty()) result.catalogue.faulty.push_back(std::move(fautif));
    }

    result.success = true;
    return result;
}

std::string encodeScanLine(const CataloguedPlugin& plugin) {
    std::ostringstream ligne;
    ligne << kScanLinePrefix << sansSeparateur(plugin.format) << '\t'
          << sansSeparateur(plugin.path) << '\t' << sansSeparateur(plugin.id) << '\t'
          << sansSeparateur(plugin.name) << '\t' << sansSeparateur(plugin.vendor) << '\t'
          << (plugin.isInstrument ? '1' : '0');
    return ligne.str();
}

bool decodeScanLine(const std::string& line, CataloguedPlugin& out) {
    const std::string prefixe = kScanLinePrefix;
    if (line.rfind(prefixe, 0) != 0) return false;

    std::vector<std::string> champs;
    std::string reste = line.substr(prefixe.size());
    size_t debut = 0;
    while (true) {
        const size_t tab = reste.find('\t', debut);
        if (tab == std::string::npos) { champs.push_back(reste.substr(debut)); break; }
        champs.push_back(reste.substr(debut, tab - debut));
        debut = tab + 1;
    }
    if (champs.size() != 6) return false;

    out.format = champs[0];
    out.path = champs[1];
    out.id = champs[2];
    out.name = champs[3];
    out.vendor = champs[4];
    out.isInstrument = champs[5] == "1";
    // UN PLUGIN SANS CHEMIN NI FORMAT N'EN EST PAS UN. La ligne vient d'un
    // processus qui vient de charger du code étranger : on ne lui fait pas
    // confiance plus qu'il n'en faut.
    return !out.path.empty() && !out.format.empty();
}

} // namespace vsm::interchange
