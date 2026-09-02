#include "vsm/interchange/ReconstructionChain.h"
#include <algorithm>
#include <filesystem>

namespace vsm::interchange {

namespace {

namespace fs = std::filesystem;

/// L'interpréteur de l'environnement virtuel de la chaîne, s'il existe.
/// POSIX et Windows ne rangent pas au même endroit ; on essaie les deux plutôt
/// que de compiler deux versions de cette fonction, parce qu'un dossier
/// `analyse/` peut très bien avoir été créé sur l'un et ouvert sur l'autre.
std::string interpreterIn(const fs::path& chainFolder) {
    const fs::path candidats[] = {
        chainFolder / ".venv" / "bin" / "python",
        chainFolder / ".venv" / "bin" / "python3",
        chainFolder / ".venv" / "Scripts" / "python.exe",
    };
    std::error_code ec;
    for (const auto& candidat : candidats)
        if (fs::exists(candidat, ec)) return candidat.string();
    return {};
}

bool hasScript(const fs::path& chainFolder) {
    std::error_code ec;
    return fs::exists(chainFolder / "reconstruire.py", ec);
}

} // namespace

ReconstructionChain ReconstructionChain::locate(const std::string& startFolder,
                                                 const std::string& explicitFolder) {
    ReconstructionChain chaine;
    std::error_code ec;

    // 1. LE CHEMIN DÉSIGNÉ À LA MAIN PRIME. S'il est donné et qu'il est faux,
    // on ne va PAS chercher ailleurs en silence : l'utilisateur a dit où
    // regarder, et lui trouver autre chose lui ferait croire qu'il a raison.
    if (!explicitFolder.empty()) {
        const fs::path designe(explicitFolder);
        if (!hasScript(designe)) {
            chaine.reason = "le dossier de la chaîne d'analyse indiqué dans les préférences ne "
                            "contient pas reconstruire.py (" + explicitFolder + ")";
            chaine.remedy = "corriger le chemin, ou l'effacer pour laisser l'application chercher";
            return chaine;
        }
        chaine.chainFolder = designe.string();
    } else {
        // 2. SINON, ON REMONTE. Le binaire compilé vit dans `build/app/`, la
        // chaîne à la racine du dépôt : chercher uniquement à côté de
        // l'exécutable ne trouverait jamais rien pendant le développement, qui
        // est précisément le moment où la chaîne est là.
        fs::path courant = fs::path(startFolder);
        if (courant.empty()) courant = fs::current_path(ec);
        for (int remontees = 0; remontees < 8 && !courant.empty(); ++remontees) {
            if (hasScript(courant / "analyse")) { chaine.chainFolder = (courant / "analyse").string(); break; }
            if (hasScript(courant)) { chaine.chainFolder = courant.string(); break; }
            const fs::path parent = courant.parent_path();
            if (parent == courant) break;
            courant = parent;
        }
        if (chaine.chainFolder.empty()) {
            chaine.reason = "la chaîne d'analyse (le dossier analyse/) est introuvable à côté de "
                            "l'application";
            chaine.remedy = "indiquer son emplacement dans Fichier ▸ Chaîne d'analyse...";
            return chaine;
        }
    }

    chaine.scriptPath = (fs::path(chaine.chainFolder) / "reconstruire.py").string();
    chaine.interpreterPath = interpreterIn(fs::path(chaine.chainFolder));
    if (chaine.interpreterPath.empty()) {
        // LA DISTINCTION EST UTILE : le dossier est là, c'est l'environnement
        // Python qui n'a jamais été créé. Dire « chaîne introuvable » enverrait
        // chercher un dossier qu'on a sous les yeux.
        chaine.reason = "l'environnement Python de la chaîne n'a pas été créé (aucun .venv dans "
                        + chaine.chainFolder + ")";
        chaine.remedy = "python3 -m venv .venv && .venv/bin/pip install -r requirements.txt";
        return chaine;
    }

    chaine.available = true;
    return chaine;
}

std::vector<std::string> ReconstructionChain::commandLine(const std::string& audioFile,
                                                           const std::string& outputFolder,
                                                           bool viserLaParite) const {
    if (!available) return {};
    std::vector<std::string> commande{interpreterPath, scriptPath, audioFile,
                                      "--sortie", outputFolder};
    // UN SEUL DRAPEAU, PAS TROIS. `--parite` est le raccourci de la chaîne, et
    // l'application le passe tel quel : recopier ici les trois découpages
    // qu'il allume les ferait diverger au premier changement, et l'aide de la
    // chaîne cesserait de décrire ce que l'application fait.
    if (viserLaParite) commande.push_back("--parite");
    return commande;
}

bool isReconstructableAudio(const std::string& path) {
    const fs::path chemin(path);
    std::string ext = chemin.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // Ce que la chaîne sait ouvrir, et rien de plus. Un `.mid` glissé sur la
    // fenêtre est un projet à importer, pas un morceau à reconstruire : les
    // confondre lancerait une analyse de dix minutes sur un fichier qui
    // n'attendait qu'à être lu.
    return ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".ogg"
           || ext == ".m4a" || ext == ".aiff" || ext == ".aif";
}

} // namespace vsm::interchange
