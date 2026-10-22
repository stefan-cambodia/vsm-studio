#pragma once
#include "vsm/sequencer/Project.h"
#include <cstddef>
#include <set>
#include <string>
#include <vector>

// D535.2 de docs/ROADMAP-daw.md — LES INSTANTANÉS DE LA CONSOLE (les *MixConsole Snapshots* de
// Cubase) : garder l'état du mixage sous un nom, et y revenir.
//
// L'état est porté PAR LA PISTE (`Track::mixSnapshots`) et le projet garde l'ordre des noms
// (`Project::mixSnapshotNames`). Ces fonctions ne font aucun pas d'historique : c'est
// l'application qui l'ouvre, et seulement si `previewMixRecall` dit que le rappel change quelque
// chose (D511).

namespace vsm::sequencer {

/// L'état de console d'une piste, tel que sa tranche le montre.
TrackMixState captureTrackMix(const Track& track);

bool hasMixSnapshot(const Project& project, const std::string& name);

/// `prefixe` + N, le premier N libre à partir du nombre d'instantanés + 1. Le préfixe vient de
/// l'interface (« Instantané », « Snapshot ») : un nom proposé est un texte qu'on lit.
std::string nextMixSnapshotName(const Project& project, const std::string& prefixe);

/// PRENDRE : chaque piste reçoit son état sous `name`. Un nom déjà pris est REMPLACÉ, à sa place
/// dans la liste — c'est à l'application de l'avoir demandé avant. Rend vrai s'il l'était.
bool takeMixSnapshot(Project& project, const std::string& name);

struct MixRecallReport {
    bool found = false;           ///< le nom existe
    size_t recalled = 0;          ///< pistes qui ont un état sous ce nom
    size_t withoutState = 0;      ///< pistes ajoutées après la prise : laissées
    size_t insertsLeft = 0;       ///< inserts ajoutés, retirés ou remplacés depuis : laissés
    size_t changed = 0;           ///< pistes dont la console CHANGE au rappel
};

/// Ce que ferait le rappel, sans rien toucher : l'application en tire le pas (ou son absence).
MixRecallReport previewMixRecall(const Project& project, const std::string& name);

/// RAPPELER : chaque piste qui a un état sous `name` le reprend ; les autres restent telles
/// quelles. Les inserts s'apparient par rang ET par type.
MixRecallReport recallMixSnapshot(Project& project, const std::string& name);

/// SUPPRIMER : le nom et l'état de chaque piste. Faux si le nom n'existe pas.
bool removeMixSnapshot(Project& project, const std::string& name);

/// RETIRER LE BUS DE DÉPART `bus` DES PISTES ET DES INSTANTANÉS : le rang est effacé partout,
/// pour que le départ gardé pour le bus suivant revienne sur CE bus-là au rappel. L'application
/// retire le bus de `Project::sends` elle-même.
void eraseSendLevelEverywhere(Project& project, size_t bus);

/// D559.1 — LEVER TOUS LES SOLOS, TOUS LES MUETS (*Deactivate All Solo / Mute* de Cubase). « Plus aucun solo » (D21.2)
/// n'est que le solo exclusif relâché ; ceci lève celui de TOUTES les pistes — groupes et VCA compris. Le solo PROTÉGÉ
/// (`soloSafe`, D30.1) n'est pas un solo : il reste. Les deux rendent combien de pistes ont perdu le leur.
size_t countSoloedTracks(const Project& project);
size_t countMutedTracks(const Project& project);
size_t clearAllSolos(Project& project);
size_t clearAllMutes(Project& project);

/// D559.2 — REMETTRE LA CONSOLE À ZÉRO (*Reset Mixer* de Cubase) : chaque tranche de piste revient à l'état d'une piste
/// neuve — volume 0 dB, panoramique au centre, ni muet ni solo, trim 0, polarité normale, départs à zéro (le vecteur
/// garde sa taille). Les INSERTS restent, et leur état : retirer une chaîne d'effets est un autre geste. Les bus de
/// départ et le master ne sont pas des tranches de piste et ne bougent pas. Rend le nombre de tranches CHANGÉES ;
/// `countTracksToReset` le même compte sans rien toucher (le libellé du menu).
size_t countTracksToReset(const Project& project);
size_t resetConsole(Project& project);
/// D564.2 — LA MÊME REMISE, bornée aux pistes `only` (*Reset Selected Channels*) : une seule règle de tranche neuve.
size_t countTracksToReset(const Project& project, const std::set<size_t>& only);
size_t resetConsole(Project& project, const std::set<size_t>& only);

/// D562.3 — LES RÉGLAGES D'UNE TRANCHE (*Copy / Paste Channel Settings*) : son trim, sa phase, ses départs et sa chaîne
/// d'inserts (réglages, état natif et contournement compris) — le « son » de la tranche. Le volume, le panoramique, le
/// muet, le solo et la sortie RESTENT : c'est la balance du morceau, piste par piste, et où va une piste n'est pas un
/// réglage de son. Un dossier et un VCA n'ont pas de tranche à recevoir (`channelSettingsApply` rend faux).
struct ChannelSettings {
    float inputTrimDb = 0.0f;
    bool invertPhase = false;
    std::vector<float> sendLevels;
    std::vector<TrackEffect> effects;
    bool sendsBypassed = false;   ///< D567.3 : le contournement des départs voyage avec eux
};
ChannelSettings captureChannelSettings(const Track& track);
/// Vrai si `track` peut recevoir les réglages (ni dossier, ni VCA) ET qu'ils y changeraient quelque chose.
bool channelSettingsWouldChange(const Track& track, const ChannelSettings& settings);
/// Pose les réglages ; rend vrai si quelque chose a changé.
bool applyChannelSettings(Track& track, const ChannelSettings& settings);

/// D566.3 — LIER LES TRANCHES CHOISIES (*Q-Link* de Cubase) : le volume ou le panoramique d'une piste choisie entraîne
/// ceux des autres pistes choisies du MÊME ÉCART — en dB pour le volume (le silence compte pour −60 dB, le bas du fader,
/// et le résultat est borné à −60…+6 dB), en valeur pour le panoramique (borné à ±1). L'écart se compte depuis le DÉBUT
/// du geste : `beginLinkedMix` relève les valeurs de départ, `applyLinkedMix` les relit à chaque cran — une tranche
/// bornée en route retrouve son écart si le geste revient. Un dossier ne suit rien (il n'a pas de fader) ; un VCA suit
/// le volume, pas le panoramique (il n'en a pas). Une piste touchée HORS du choix bouge seule : le geste est inactif.
enum class LinkedMixParam { Volume, Pan };
struct LinkedMixGesture {
    LinkedMixParam param = LinkedMixParam::Volume;
    size_t touched = 0;
    float touchedStart = 0.0f;                     ///< la valeur de départ de la piste touchée (dB ou panoramique)
    struct Follower {
        size_t track = 0;
        float start = 0.0f;   ///< la valeur de départ comptée (dB ou panoramique)
        float raw = 0.0f;     ///< la valeur de départ telle quelle (gain ou panoramique) : un geste revenu à zéro la rend
    };
    std::vector<Follower> followers;   ///< les autres pistes choisies
    bool active() const { return !followers.empty(); }
};
/// Le volume en dB tel que le lien le compte : le silence et tout ce qui est sous −60 dB valent −60, le haut +6.
float linkedVolumeDb(float gain);
LinkedMixGesture beginLinkedMix(const Project& project, LinkedMixParam param, size_t touched,
                                const std::set<size_t>& selection);
/// Écrit chaque piste qui suit depuis l'écart ACTUEL de la piste touchée ; rend le nombre de pistes qui ont bougé.
size_t applyLinkedMix(Project& project, const LinkedMixGesture& gesture);

/// D567.3 — CONTOURNER LES DÉPARTS DES TRANCHES CHOISIES (*Bypass Sends*). Le plan dit ce que le clic FERA, comme
/// « Contourner tout » (D15.1) : CONTOURNER si l'une des tranches choisies a un départ non nul qui ne l'est pas encore
/// — `count` tranches —, RÉTABLIR sinon — `count` tranches contournées. Un dossier et un VCA n'ont pas de départs ; une
/// tranche sans niveau non nul n'a rien à contourner. Les niveaux ne bougent jamais.
struct SendsBypassPlan {
    bool bypass = true;
    size_t count = 0;
};
SendsBypassPlan sendsBypassPlan(const Project& project, const std::set<size_t>& chosen);
/// Pose (ou retire) le contournement sur les tranches choisies que le plan compte ; rend combien ont changé.
size_t applySendsBypass(Project& project, const std::set<size_t>& chosen, bool bypass);

/// D568.3 — CONTOURNER LES INSERTS DES TRANCHES CHOISIES (*Bypass Inserts*), la règle de « Contourner tout » (D15.1)
/// sur plusieurs pistes : CONTOURNER si l'une des tranches choisies a un insert actif — `count` tranches —, RÉTABLIR
/// sinon — `count` tranches qui ont un insert contourné. Les inserts passent tous à `enabled` faux, ou tous à vrai : l'état
/// individuel d'avant n'est pas gardé, comme dans le panneau d'effets. Les descriptions seulement : le moteur suit par
/// l'application.
struct InsertsBypassPlan {
    bool bypass = true;
    size_t count = 0;
};
InsertsBypassPlan insertsBypassPlan(const Project& project, const std::set<size_t>& chosen);
size_t applyInsertsBypass(Project& project, const std::set<size_t>& chosen, bool bypass);

} // namespace vsm::sequencer
