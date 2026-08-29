#pragma once
#include "vsm/interchange/ProjectDocument.h"
#include "vsm/interchange/SynthPreset.h"
#include "vsm/sequencer/Project.h"
#include <map>
#include <string>
#include <vector>

// Dossier de projet complet (Phase 7, P7 -- « Mode A » de
// docs/ROADMAP-interop.md § 7) : lire et écrire l'ensemble
// `project.json` + `midi/arrangement.mid` + `instruments/*.synth.json`.
//
// LE MODE A EN UNE PHRASE : un outil extérieur (le projet Python d'analyse)
// produit ces fichiers, le DAW les charge et les rend. Pas de réseau, pas de
// protocole, pas de processus à synchroniser -- des fichiers, qu'on peut
// inspecter, versionner, rejouer et comparer. C'est ce qui rend la boucle
// audio -> Python -> DAW -> rendu -> Python reproductible et débogable, et
// c'est pourquoi la roadmap la fait passer AVANT toute API temps réel.
//
// Cette couche fait des I/O fichier : elle n'est appelée ni depuis le thread
// audio, ni depuis `core/`, ni depuis `audio/`.

namespace vsm::interchange {

/// Noms de fichiers/dossiers du format. Centralisés : un chemin en dur
/// dispersé dans le code finirait par diverger entre l'écriture et la lecture.
inline constexpr const char* kProjectFileName = "project.json";
inline constexpr const char* kDefaultMidiPath = "midi/arrangement.mid";
/// LES NOTES DES PRISES CONSERVÉES (D3.5), dans un fichier SÉPARÉ.
///
/// Elles ne peuvent pas aller dans `arrangement.mid` : celui-ci est ce qu'on
/// ENTEND, et c'est aussi ce qu'on exporte pour l'ouvrir ailleurs. Y verser
/// toutes les passes qu'on a essayées et écartées ferait de l'arrangement une
/// archive, et l'ouvrir dans un autre logiciel montrerait des pistes qui ne
/// jouent pas. Deux fichiers, deux rôles : l'arrangement et le tiroir.
///
/// Absent quand aucune piste n'a de prise -- un projet qui n'en a pas garde
/// exactement les fichiers qu'il avait.
inline constexpr const char* kTakesMidiPath = "midi/prises.mid";
inline constexpr const char* kInstrumentsFolder = "instruments";

struct LoadedBundle {
    /// Projet complet : notes venues du `.mid`, contexte venu du `project.json`.
    vsm::sequencer::Project project;
    ProjectDocument document;
    /// Preset par index de piste (absent = la piste n'en déclare pas).
    std::map<size_t, SynthPreset> presetsByTrack;
    /// Dossier d'où vient le projet. NÉCESSAIRE, pas décoratif : les chemins
    /// d'échantillons des presets sont relatifs à lui, et un projet chargé qui
    /// ignorerait d'où il vient ne saurait pas les retrouver.
    std::string folderPath;
    ImportReport report;
};

struct BundleLoadResult {
    bool success = false;
    LoadedBundle bundle;
    std::string error;
    /// Anomalies NON bloquantes : preset introuvable, machine absente...
    /// Le chargement continue et les nomme -- un projet incomplet doit
    /// s'ouvrir et se dire incomplet, pas refuser de s'ouvrir.
    std::vector<std::string> warnings;
};

/// Charge un dossier de projet. `folderPath` contient `project.json`.
BundleLoadResult loadProjectBundle(const std::string& folderPath);

struct BundleSaveResult {
    bool success = false;
    std::string error;
    std::vector<std::string> writtenFiles; // chemins relatifs, pour journalisation
};

/// Écrit un dossier de projet complet à partir d'un projet en mémoire :
/// `project.json`, le MIDI (notes) et un `*.synth.json` par piste ayant un
/// instrument. Les presets sont capturés depuis l'état PAR DÉFAUT de chaque
/// machine tant que l'application ne fournit pas d'états réels -- l'appelant
/// peut les remplacer via `presetsByTrack`.
BundleSaveResult saveProjectBundle(const vsm::sequencer::Project& project,
                                    const std::string& folderPath,
                                    const std::map<size_t, SynthPreset>& presetsByTrack = {});

// --- D6.4 : un projet qui s'ouvre ailleurs ----------------------------------
//
// LE FORMAT ÉTAIT DÉJÀ PORTABLE ; L'ENREGISTREMENT NE L'ÉTAIT PAS. Tous les
// chemins d'un projet sont relatifs à son dossier, et la lecture refuse même
// un chemin absolu. Mais `saveProjectBundle` n'écrit que `project.json`, le
// MIDI et les presets : il ne COPIE aucun média. Enregistrer sous un autre
// dossier produisait donc un projet dont le `project.json` désignait des
// fichiers restés dans l'ancien -- illisible sur une autre machine, et
// silencieusement incomplet sur celle-ci.

/// Tous les fichiers que le projet DÉSIGNE, en chemins relatifs et sans
/// doublon : l'audio de chaque piste, son gel, l'audio de chaque prise, les
/// échantillons et le profil de chaque preset.
std::vector<std::string> referencedMediaPaths(const LoadedBundle& bundle);

/// Ceux d'entre eux qui MANQUENT à côté du projet. Vide = le dossier s'ouvre
/// ailleurs tel quel, ce qui est exactement le critère de D6.4.
std::vector<std::string> missingMediaPaths(const LoadedBundle& bundle);

struct StandaloneExportResult {
    bool success = false;
    std::string error;
    std::vector<std::string> copiedFiles;
    /// Référencés mais introuvables à la source : le dossier écrit est
    /// incomplet, et il le DIT. Refuser d'écrire serait pire -- on perdrait
    /// aussi les quinze pistes qui, elles, sont là.
    std::vector<std::string> missing;
};

/// Écrit un dossier de projet AUTONOME : le projet, plus une copie de chaque
/// média qu'il désigne. C'est `saveProjectBundle` suivi de la copie que
/// celui-ci ne fait pas.
StandaloneExportResult exportStandaloneProject(const LoadedBundle& bundle,
                                                const std::string& destFolder);

/// Lecture/écriture de fichiers texte, exposées parce que les outils en ligne
/// de commande et les tests en ont besoin (et pour ne pas éparpiller trois
/// variantes d'ouverture de fichier dans le projet).
bool readTextFile(const std::string& path, std::string& outText, std::string& outError);
bool writeTextFile(const std::string& path, const std::string& text, std::string& outError);

} // namespace vsm::interchange
