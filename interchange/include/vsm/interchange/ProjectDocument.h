#pragma once
#include "vsm/interchange/Json.h"
#include "vsm/interchange/SynthPreset.h"
#include "vsm/sequencer/Project.h"
#include <map>
#include <string>
#include <vector>

// Format de projet `project.json` (Phase 7, P4 -- docs/ROADMAP-interop.md § 4).
//
// CE QUE CE FICHIER CONTIENT, ET SURTOUT CE QU'IL NE CONTIENT PAS : il décrit
// le TRANSPORT (tempo, signatures, résolution, boucle), la composition des
// PISTES (nom, canal, couleur, instrument souhaité, mixage, effets) et
// RÉFÉRENCE le MIDI -- il ne recopie jamais les notes. Les notes ont déjà un
// format universel, `.mid`, que tout le monde sait lire et écrire ; les
// dupliquer en JSON créerait deux vérités qui divergeraient au premier
// désaccord, et personne ne saurait laquelle croire.
//
//     VSMProject/
//     ├── project.json                  <- ce fichier
//     ├── midi/arrangement.mid          <- les notes
//     └── instruments/track_00.synth.json  <- un preset par piste
//
// CHEMINS RELATIFS UNIQUEMENT, séparateurs `/`. Un projet doit s'ouvrir sur
// une autre machine, un autre système, un autre utilisateur : un chemin absolu
// (`/home/moi/...`, `C:\Users\...`) rendrait le dossier non transportable. Les
// chemins absolus et les antislashs sont REFUSÉS à la lecture, pas corrigés en
// silence -- accepter un fichier non portable revient à laisser le problème
// apparaître chez quelqu'un d'autre.

namespace vsm::interchange {

inline constexpr const char* kProjectFormat = "vsm-project";
/// VERSION 2 : les clips et les repères. La version 1 se lit toujours, et se
/// convertit sans rien perdre -- une piste de la version 1 n'a pas de clip,
/// c'est-à-dire qu'elle n'est pas découpée, ce qui est un état parfaitement
/// représentable en version 2. La conversion est donc VIDE, et c'est exactement
/// ce qui la rend impossible à rater.
///
/// Dans l'autre sens, un fichier version 2 est refusé par un logiciel qui ne
/// lit que la 1 -- refusé et non deviné, comme tout ce que ce format ne
/// comprend pas.
inline constexpr int kProjectVersion = 2;
/// La plus ancienne version qu'on sache encore lire.
inline constexpr int kOldestReadableProjectVersion = 1;

struct ProjectTempoChange { int64_t tick = 0; double bpm = 120.0; };
struct ProjectTimeSignature { int64_t tick = 0; int numerator = 4; int denominator = 4; };

struct ProjectTransport {
    int ticksPerQuarterNote = 480;
    std::vector<ProjectTempoChange> tempoChanges;
    std::vector<ProjectTimeSignature> timeSignatures;
    bool loopEnabled = false;
    int64_t loopStartTick = 0;
    int64_t loopEndTick = 0;
};

struct ProjectEffect {
    std::string type;                       ///< identifiant EffectFactory ("reverb"...)
    std::map<std::string, float> parameters; ///< semanticId -> valeur
};

/// Un point d'automation. `tick` est dans la résolution du transport (la même
/// que le MIDI et la boucle) ; `value` est en UNITÉS RÉELLES (Hz, secondes),
/// jamais en normalisé -- la règle de tout le format. `step` dit que le
/// segment PARTANT de ce point est un palier, pas une rampe.
struct ProjectAutomationPoint {
    int64_t tick = 0;
    float value = 0.0f;
    bool step = false;
};

/// Une courbe d'automation d'une piste, ciblant un paramètre par son identité
/// SÉMANTIQUE (« filter.1.cutoff ») : c'est ce qui permet à la chaîne
/// d'analyse d'écrire « la coupure suit cette trajectoire » sans connaître la
/// machine, et au projet de survivre à un changement de machine.
///
/// Champ FACULTATIF du format (comme `samples` dans les presets) : un projet
/// sans automation reste identique octet pour octet, et un projet ancien se
/// charge sans rien remarquer.
struct ProjectAutomationLane {
    std::string parameter;
    std::vector<ProjectAutomationPoint> points;
};

/// Un clip : une région du matériau de la piste, posée sur la ligne de temps.
/// Voir `vsm::sequencer::Clip` pour le raisonnement.
struct ProjectClip {
    int64_t sourceStart = 0;
    int64_t sourceLength = 0;   ///< 0 = jusqu'à la fin du matériau
    int64_t startTick = 0;
    int64_t length = 0;         ///< 0 = celle de la fenêtre ; plus grande = boucle
    bool muted = false;
    std::string name;
    uint32_t colorRgba = 0xFF6B9BFFu;
};

/// Un repère nommé sur la ligne de temps.
struct ProjectMarker {
    int64_t tick = 0;
    std::string name;
};

struct ProjectTrack {
    std::string name;
    int channel = 0;
    uint32_t colorRgba = 0xFF6B9BFFu;
    /// Instrument SOUHAITÉ : un projet peut nommer une machine que cette
    /// installation n'a pas. On garde alors l'intention telle quelle (voir
    /// ImportReport::missingInstruments) au lieu d'y substituer autre chose.
    std::string preferredPlugin;
    /// Chemin RELATIF du preset, ex. "instruments/track_00.synth.json".
    /// Vide = pas de preset associé.
    std::string presetPath;
    float volume = 1.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    std::array<float, 2> sendLevels{{0.0f, 0.0f}};
    std::vector<ProjectEffect> effects;
    std::vector<ProjectAutomationLane> automation;
    /// Champ FACULTATIF : une piste sans clip n'est pas découpée, et son
    /// fichier reste identique octet pour octet à ce qu'il était.
    std::vector<ProjectClip> clips;
};

struct ProjectDocument {
    std::string title = "Sans titre";
    /// Réglages de la tranche master, par nom. Champ FACULTATIF du format,
    /// comme `automation` : un projet qui n'en a pas garde exactement le
    /// fichier qu'il a toujours eu, et un fichier ancien se charge sans rien
    /// remarquer.
    std::map<std::string, float> master;
    /// Chemin RELATIF du fichier MIDI qui porte les notes.
    std::string midiPath = "midi/arrangement.mid";
    ProjectTransport transport;
    std::vector<ProjectTrack> tracks;
    /// Repères nommés. Facultatif, comme les clips.
    std::vector<ProjectMarker> markers;
};

/// Décrit un projet en mémoire (hors notes, qui partent dans le `.mid`).
ProjectDocument documentFromProject(const vsm::sequencer::Project& project);

/// Ce que l'import a réellement pu faire. Rien n'est jamais deviné en silence.
struct ImportReport {
    /// Machines nommées par le projet mais absentes de cette installation.
    /// Le MIDI, le nom de la piste et l'identifiant demandé sont conservés :
    /// l'utilisateur peut installer la machine et rouvrir, ou choisir lui-même
    /// un remplacement.
    std::vector<std::string> missingInstruments;
    /// Pistes du projet sans équivalent dans le MIDI (ou l'inverse).
    size_t tracksInDocument = 0;
    size_t tracksInProject = 0;
    std::vector<std::string> warnings;

    bool hasWarnings() const { return !warnings.empty() || !missingInstruments.empty(); }
    std::string summary() const;
};

/// Applique le document à un projet déjà chargé depuis le `.mid` : transport,
/// noms, canaux, mixage, instruments. Les pistes sont appariées par ORDRE
/// (piste 0 du document -> piste 0 du MIDI), la convention la plus simple et
/// la seule qui ne dépende pas de noms que l'export MIDI peut avoir modifiés.
ImportReport applyDocumentToProject(const ProjectDocument& document, vsm::sequencer::Project& project);

JsonValue projectDocumentToJson(const ProjectDocument& document);

struct ProjectLoadResult {
    bool success = false;
    ProjectDocument document;
    std::string error;
};

ProjectLoadResult projectDocumentFromJson(const JsonValue& json);
ProjectLoadResult parseProjectDocument(const std::string& jsonText);

/// Vrai si le chemin est utilisable dans un projet transportable : relatif,
/// séparateurs `/`, sans remontée `..`. Exposé pour être testé directement.
bool isPortableRelativePath(const std::string& path);

} // namespace vsm::interchange
