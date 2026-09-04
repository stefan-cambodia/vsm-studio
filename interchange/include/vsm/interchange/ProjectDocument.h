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
///
/// VERSION 3 : le suivi de tempo d'un clip audio (D12, `warp` et
/// `warpMarkers`) et le clip à l'envers (D13.4, `reversed`). Écrite SEULEMENT si un clip s'en sert — un projet qui ne
/// suit pas le tempo garde son fichier de version 2, octet pour octet — parce
/// qu'un lecteur de la version 2 jouerait un clip étiré SANS l'étirer, en
/// silence, et que ce format refuse plutôt qu'il ne devine.
inline constexpr int kProjectVersion = 3;
/// La version qu'un projet sans suivi de tempo continue d'écrire.
inline constexpr int kProjectVersionWithoutWarp = 2;
/// La plus ancienne version qu'on sache encore lire.
inline constexpr int kOldestReadableProjectVersion = 1;

struct ProjectTempoChange { int64_t tick = 0; double bpm = 120.0; };
struct ProjectTimeSignature { int64_t tick = 0; int numerator = 4; int denominator = 4; };

/// UN BUS DE DÉPART du projet (D4.2). Voir `vsm::sequencer::SendBusDescription`
/// pour ce qui distingue un départ d'un insert.
///
/// Champ FACULTATIF du format : un projet qui n'en déclare pas garde exactement
/// le fichier qu'il avait avant que les départs soient nommés.
struct ProjectSendBus {
    std::string name;
    std::string effectType;
    std::map<std::string, float> parameters;
    float returnGain = 1.0f;
    /// Le départ prélève AVANT le fader de la piste. Voir
    /// `vsm::sequencer::SendBusDescription::preFader`.
    bool preFader = false;
};

struct ProjectTransport {
    int ticksPerQuarterNote = 480;
    std::vector<ProjectTempoChange> tempoChanges;
    std::vector<ProjectTimeSignature> timeSignatures;
    bool loopEnabled = false;
    int64_t loopStartTick = 0;
    int64_t loopEndTick = 0;
    /// RÉGION DE PUNCH (D3.5) : entre ces deux ticks, et seulement là,
    /// l'enregistrement capte. Champ FACULTATIF -- un projet qui n'en déclare
    /// pas garde exactement le fichier qu'il avait.
    bool punchEnabled = false;
    int64_t punchStartTick = 0;
    int64_t punchEndTick = 0;
};

struct ProjectEffect {
    /// Identifiant `EffectFactory` : « reverb », ou `clap:`/`vst3:` pour un
    /// effet qu'on n'a pas écrit (D7.3).
    std::string type;
    std::map<std::string, float> parameters; ///< semanticId -> valeur
    /// État natif d'un effet tiers (D7.3), en texte. Vide pour les effets
    /// internes ; écrit seulement quand il existe, si bien qu'un projet sans
    /// plugin tiers garde exactement le fichier qu'il avait.
    std::string nativeState;
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
    /// Clip AUDIO : la fenêtre dans le fichier est en SECONDES, parce qu'un
    /// enregistrement ne suit pas le tempo. Voir `vsm::sequencer::Clip`.
    double sourceStartSeconds = 0.0;
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    float gain = 1.0f;
    bool invertPhase = false;
    /// Le suivi de tempo (D12) : 0 = éteint, 1 = hauteur conservée (vocodeur
    /// de phase), 2 = rééchantillonné, 3 = hauteur conservée par le WSOLA (le
    /// témoin) ; et les marqueurs (secondes de fichier, tick relatif au début
    /// du clip). Voir `vsm::sequencer::Clip`.
    int warpMode = 0;
    std::vector<std::pair<double, int64_t>> warpMarkers;
    /// À l'envers (D13.4). Comme le suivi de tempo, il fait monter la version
    /// du fichier : un lecteur ancien jouerait le clip à l'endroit sans un mot.
    bool reversed = false;
};

/// Le fichier que joue une piste audio. `path` est RELATIF au dossier de
/// projet, comme les presets : un chemin absolu est refusé, jamais réécrit.
struct ProjectAudioSource {
    std::string path;
    double sampleRate = 0.0;
    int64_t frames = 0;
    int channels = 0;
};

/// Un repère nommé sur la ligne de temps.
struct ProjectMarker {
    int64_t tick = 0;
    std::string name;
};

/// UNE PRISE CONSERVÉE (D3.5). Voir `vsm::sequencer::Take` pour le modèle.
///
/// SES NOTES NE SONT PAS ICI, et c'est la règle du format : les notes ont déjà
/// un format universel et testé, elles vont dans un `.mid`. Celles des prises
/// vont dans `midi/prises.mid` -- un fichier SÉPARÉ d'`arrangement.mid`, parce
/// que l'arrangement doit rester ce qu'on entend et non l'archive de tout ce
/// qu'on a essayé. `midiTrackIndex` dit quelle piste de `prises.mid` porte
/// celles-ci ; -1 quand la prise n'a pas de notes (une prise purement audio).
struct ProjectTake {
    std::string name;
    int64_t startTick = 0;
    int64_t endTick = 0;
    int midiTrackIndex = -1;
    ProjectAudioSource audio;
    std::vector<ProjectClip> clips;
};

struct ProjectTrack {
    /// Où va la sortie : index de la piste de GROUPE qui la reçoit, -1 pour le
    /// master. Facultatif : absent vaut -1, donc les projets d'avant les
    /// groupes se lisent inchangés.
    int outputGroup = -1;
    /// Hauteur de la piste dans la vue d'arrangement et si elle est pliée
    /// (D5.3). Facultatifs : une piste à la hauteur standard et dépliée garde
    /// exactement le fichier qu'elle avait.
    int arrangementHeight = 56;
    bool folded = false;
    /// La piste est GELÉE et joue `frozenAudio` au lieu de son instrument
    /// (D5.5). Facultatifs : une piste non gelée garde le fichier qu'elle avait.
    bool frozen = false;
    ProjectAudioSource frozenAudio;
    /// « midi » (défaut), « audio » ou « group ». Absent du fichier pour une piste MIDI :
    /// un projet qui n'a que des pistes MIDI garde octet pour octet le fichier
    /// qu'il avait avant que les pistes audio existent.
    std::string kind;
    ProjectAudioSource audio;
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
    /// Niveaux d'envoi, un par bus déclaré par le projet. Un vecteur plus
    /// court que la liste des bus vaut « pas d'envoi » sur les suivants.
    std::vector<float> sendLevels;
    std::vector<ProjectEffect> effects;
    std::vector<ProjectAutomationLane> automation;
    /// Champ FACULTATIF : une piste sans clip n'est pas découpée, et son
    /// fichier reste identique octet pour octet à ce qu'il était.
    std::vector<ProjectClip> clips;
    /// Les prises empilées, et celle qui est active. Facultatifs comme les
    /// clips : une piste qui n'a jamais servi à un enregistrement empilé
    /// n'écrit rien de plus qu'avant.
    std::vector<ProjectTake> takes;
    int activeTake = -1;
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
    /// Les bus de départ partagés. Facultatif, comme les clips et les prises.
    std::vector<ProjectSendBus> sends;
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
