#include "vsm/interchange/BrowserIndex.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace vsm::interchange {

namespace {

namespace fs = std::filesystem;

std::string lowered(std::string texte) {
    std::transform(texte.begin(), texte.end(), texte.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return texte;
}

bool endsWith(const std::string& texte, const std::string& fin) {
    return texte.size() >= fin.size()
           && texte.compare(texte.size() - fin.size(), fin.size(), fin) == 0;
}

} // namespace

bool isPresetFile(const std::string& path) { return endsWith(lowered(path), ".synth.json"); }
bool isProfileFile(const std::string& path) { return endsWith(lowered(path), ".profile.json"); }

bool isSampleFile(const std::string& path) {
    const std::string bas = lowered(path);
    for (const char* ext : {".wav", ".flac", ".aiff", ".aif", ".ogg", ".mp3"})
        if (endsWith(bas, ext)) return true;
    return false;
}

const char* browserKindLabel(BrowserItemKind kind) {
    switch (kind) {
        case BrowserItemKind::Machine: return "Machines";
        case BrowserItemKind::Preset:  return "Presets";
        case BrowserItemKind::Profile: return "Profils";
        case BrowserItemKind::Sample:  return "Échantillons";
    }
    return "?";
}

void indexFolder(const std::string& folderPath, const std::string& origin,
                  std::vector<BrowserItem>& out, int maxDepth, size_t maxItems) {
    std::error_code ec;
    if (folderPath.empty() || !fs::is_directory(folderPath, ec)) return;

    fs::recursive_directory_iterator it(folderPath, fs::directory_options::skip_permission_denied, ec);
    if (ec) return;
    const fs::recursive_directory_iterator fin;
    for (; it != fin; it.increment(ec)) {
        if (ec) break;
        if (out.size() >= maxItems) break;
        // LA PROFONDEUR EST BORNÉE : un dossier d'échantillons peut être
        // n'importe quoi, y compris la racine d'un disque désignée par
        // mégarde, et une exploration sans fond transformerait une erreur de
        // clic en gel de plusieurs minutes.
        if (it.depth() >= maxDepth) { it.disable_recursion_pending(); }
        if (!it->is_regular_file(ec)) continue;

        const std::string chemin = it->path().string();
        BrowserItem entree;
        if (isPresetFile(chemin))       entree.kind = BrowserItemKind::Preset;
        else if (isProfileFile(chemin)) entree.kind = BrowserItemKind::Profile;
        else if (isSampleFile(chemin))  entree.kind = BrowserItemKind::Sample;
        else continue;

        entree.name = it->path().filename().string();
        // Les doubles extensions du projet (`.synth.json`) sont retirées en
        // entier : « basse-acide.synth » se lit mal.
        if (entree.kind == BrowserItemKind::Preset)
            entree.name = entree.name.substr(0, entree.name.size() - 11);
        else if (entree.kind == BrowserItemKind::Profile)
            entree.name = entree.name.substr(0, entree.name.size() - 13);
        else
            entree.name = it->path().stem().string();

        entree.reference = chemin;
        // L'origine dit LE DOSSIER, pas seulement la bibliothèque : deux
        // « basse » dans deux sous-dossiers doivent se distinguer sans qu'on
        // ait à les essayer.
        const fs::path relatif = fs::relative(it->path().parent_path(), folderPath, ec);
        entree.origin = origin;
        if (!ec && !relatif.empty() && relatif != ".")
            entree.origin += " / " + relatif.string();
        out.push_back(std::move(entree));
    }
}

std::vector<BrowserItem> filterBrowserItems(const std::vector<BrowserItem>& items,
                                             const std::string& query) {
    const std::string requete = lowered(query);
    std::vector<std::string> mots;
    std::istringstream flux(requete);
    std::string mot;
    while (flux >> mot) mots.push_back(mot);
    if (mots.empty()) return items;

    std::vector<BrowserItem> retenus;
    for (const auto& entree : items) {
        const std::string foin = lowered(entree.name + " " + entree.origin + " " + entree.reference);
        // TOUS LES MOTS, DANS N'IMPORTE QUEL ORDRE. « 303 acid » trouve
        // « TB-303 Acid Lead » comme « acid lead (tb303) ».
        const bool tous = std::all_of(mots.begin(), mots.end(), [&foin](const std::string& m) {
            return foin.find(m) != std::string::npos;
        });
        if (tous) retenus.push_back(entree);
    }
    return retenus;
}

} // namespace vsm::interchange
