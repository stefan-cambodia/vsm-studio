#pragma once
#include <string>
#include <vector>

namespace vsm::interchange {

/// OÙ EST LA CHAÎNE DE RECONSTRUCTION, ET SI ELLE N'EST PAS LÀ, POURQUOI (D9.1).
///
/// **LA RÈGLE QUI COMMANDE TOUT LE RESTE** (§ 0, règle n° 2 de
/// `ROADMAP-daw.md`) : le DAW se compile, se teste et fonctionne SANS Python.
/// La chaîne d'analyse -- séparation, transcription, recherche de patch -- vit
/// dans `analyse/`, en Python, avec des dépendances lourdes (torch, demucs).
/// Rien ici ne les lie au binaire : ce module ne fait que REGARDER si les
/// fichiers existent.
///
/// **ET IL NE LANCE RIEN POUR LE SAVOIR.** La tentation est d'exécuter
/// `python -c "import demucs"` pour vérifier que l'environnement est complet.
/// C'est refusé : cela ferait dépendre l'ouverture d'un menu du démarrage d'un
/// interpréteur, qui prend une seconde quand tout va bien et se bloque quand
/// tout va mal. La détection est donc PUREMENT une question de fichiers, elle
/// coûte quelques `stat`, et elle ne peut ni échouer ni attendre.
///
/// **CE QU'ELLE NE PEUT DONC PAS PROMETTRE**, et il faut l'écrire plutôt que
/// de le découvrir : trouver l'interpréteur ne prouve pas que `torch` est
/// installé. Une dépendance manquante se verra au lancement de la chaîne, dans
/// sa sortie, et c'est le bon endroit -- c'est là qu'elle est nommée.
///
/// **LE CRITÈRE DE D9.1 EST « JAMAIS UNE ERREUR »** : quand la chaîne manque,
/// l'application ne montre pas un message d'échec, elle montre une fonction
/// grisée QUI DIT POURQUOI et ce qu'il faudrait faire. Un menu inerte sans
/// explication et un message d'erreur sont deux façons de laisser l'utilisateur
/// devant un mur.
struct ReconstructionChain {
    /// La chaîne est-elle utilisable ?
    bool available = false;
    /// Le script `reconstruire.py`, vide s'il n'a pas été trouvé.
    std::string scriptPath;
    /// L'interpréteur Python de l'environnement de la chaîne, vide de même.
    std::string interpreterPath;
    /// Le dossier `analyse/`, qui sert de répertoire de travail au lancement.
    std::string chainFolder;
    /// POURQUOI ELLE NE L'EST PAS, en une phrase destinée à être LUE par
    /// l'utilisateur -- pas un code, pas un chemin nu. Vide quand tout va bien.
    std::string reason;
    /// Ce qu'il faudrait taper pour que ça marche, quand la réponse tient en
    /// une commande. Vide sinon.
    std::string remedy;

    /// Cherche la chaîne. `startFolder` est typiquement le dossier de
    /// l'exécutable ; la recherche remonte de dossier en dossier jusqu'à
    /// trouver un `analyse/reconstruire.py`, parce qu'un binaire compilé vit
    /// dans `build/app/` et la chaîne à la racine du dépôt.
    ///
    /// `explicitFolder`, s'il est fourni, est essayé EN PREMIER : c'est le
    /// chemin que l'utilisateur a pu désigner à la main, et il doit primer sur
    /// ce que la recherche devine.
    static ReconstructionChain locate(const std::string& startFolder,
                                       const std::string& explicitFolder = {});

    /// La ligne de commande à exécuter pour reconstruire `audioFile` dans
    /// `outputFolder`. Vide si la chaîne n'est pas disponible.
    ///
    /// RENDUE COMME UNE LISTE D'ARGUMENTS, jamais comme une chaîne à faire
    /// passer par un interpréteur de commandes : un morceau s'appelle « Sweet
    /// Child O' Mine.mp3 », avec une apostrophe et des espaces, et concaténer
    /// des chemins dans une ligne de shell est la façon la plus sûre de
    /// transformer un nom de fichier en commande.
    /// La ligne de commande de la chaîne.
    ///
    /// `viserLaParite` passe `--parite` -- sinon `--sans-parite`, parce que la
    /// parité est le défaut de la chaîne depuis le 04/09/2026 et qu'une case
    /// décochée doit désactiver quelque chose. `--parite` : autant de pistes que le morceau a
    /// de parties (docs/CDC-detection-multipiste.md). SANS ce drapeau,
    /// l'application obtenait quatre pistes là où la ligne de commande en
    /// donnait treize — le musicien qui glisse son morceau dans la fenêtre
    /// n'avait aucun moyen d'atteindre ce que la chaîne sait faire.
    std::vector<std::string> commandLine(const std::string& audioFile,
                                          const std::string& outputFolder,
                                          bool viserLaParite = false) const;
};

/// Les extensions que la chaîne sait lire. Utilisé par le glisser-déposer pour
/// distinguer « un morceau à reconstruire » d'un fichier MIDI ou d'un projet.
bool isReconstructableAudio(const std::string& path);

} // namespace vsm::interchange
