#pragma once
#include "vsm/midi/MidiFileParser.h"
#include "vsm/sequencer/TempoMap.h"
#include "vsm/sequencer/TimeSignatureMap.h"
#include "vsm/sequencer/Track.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vsm::sequencer {

/// UN BUS DE DÉPART : un effet PARTAGÉ que toutes les pistes peuvent
/// alimenter, et dont le retour est ajouté au mixage.
///
/// C'est ce qui distingue un départ d'un insert : une réverbération placée en
/// insert sur six pistes, ce sont six réverbérations qui tournent et six
/// espaces différents ; placée en départ, c'est UN espace dans lequel six
/// pistes se trouvent. La différence s'entend, et elle coûte six fois moins
/// cher à calculer.
///
/// L'effet est décrit -- un identifiant de fabrique et des valeurs nommées --
/// et non instancié, pour la même raison que `TrackEffect` : `core/` ne
/// connaît pas `audio/`, et le disque ne connaît que des noms et des nombres.
struct SendBusDescription {
    std::string name;
    std::string effectType;                  ///< identifiant EffectFactory ("reverb"...)
    std::map<std::string, float> parameters; ///< identité sémantique -> valeur réelle
    /// Gain du RETOUR dans le mixage. Baisser le retour n'est pas la même
    /// chose que baisser les départs : l'un change le niveau de l'effet pour
    /// toutes les pistes d'un coup, l'autre change qui l'alimente.
    float returnGain = 1.0f;

    /// PRÉ-FADER : le départ prélève le signal AVANT le fader de la piste
    /// (D4.3). Post-fader par défaut, ce qui était jusqu'ici codé en dur.
    ///
    /// CE QUE CHACUN VEUT DIRE, ET QUAND ON VEUT L'UN OU L'AUTRE. En
    /// post-fader, baisser une piste baisse aussi ce qu'elle envoie : la
    /// proportion d'effet reste constante, et c'est ce qu'on veut pour une
    /// réverbération -- une piste qu'on retire du mixage ne doit pas laisser sa
    /// réverbération toute seule. En pré-fader, le départ ignore le fader :
    /// c'est ce qu'il faut pour un retour de casque, ou pour envoyer une piste
    /// dans un effet SANS l'entendre en direct -- on descend le fader à zéro et
    /// seul l'effet subsiste.
    ///
    /// C'EST UN RÉGLAGE DU BUS ET NON DE CHAQUE PISTE, comme sur une console où
    /// un auxiliaire est câblé pré ou post pour tout le monde. Le rendre
    /// indépendant par piste multiplierait les commutateurs par le nombre de
    /// pistes pour un besoin que rien n'a exprimé, et le jour où il le sera, ce
    /// champ deviendra le DÉFAUT du bus.
    bool preFader = false;
};

/// Modèle "métier" du morceau : c'est CE que le piano roll, le mixer et
/// l'automation manipulent — jamais les octets bruts du fichier MIDI.
///
/// Project::fromParsedFile() convertit une lecture brute (ParsedFile) en ce
/// modèle éditable (appariement Note On/Off, extraction du tempo/de la
/// signature rythmique...). Project::toParsedFile() fait le chemin inverse
/// pour l'export. Les deux fonctions sont testées en aller-retour (voir
/// tests/test_midi_writer.cpp) pour garantir qu'aucune information musicale
/// n'est perdue.
class Project {
public:
    std::string title = "Sans titre";
    midi::SmfFormat exportFormat = midi::SmfFormat::Type1;
    uint16_t ticksPerQuarterNote = 480;

    TempoMap tempoMap;
    TimeSignatureMap timeSignatureMap;
    std::vector<Track> tracks;

    /// Repères nommés de la ligne de temps, triés par tick.
    std::vector<Marker> markers;

    /// LES BUS DE DÉPART (sends) DU PROJET (D4.2).
    ///
    /// Ils étaient DEUX, figés dans le constructeur de l'application sur une
    /// réverbération et un delay, et le nombre était une constante du moteur.
    /// Ce n'était pas seulement rigide : c'était invisible, puisque rien dans
    /// le projet ne disait ce que les deux boutons « send » de chaque tranche
    /// alimentaient. Un projet déclare désormais SES bus, avec leur nom et
    /// leur effet, et le fichier le dit.
    ///
    /// Vide = aucun départ, et les boutons correspondants n'existent pas dans
    /// le mixeur -- plutôt que deux boutons qui n'envoient nulle part.
    std::vector<SendBusDescription> sends;

    /// Région de boucle. Elle vivait jusqu'ici dans l'interface seule, ce qui
    /// la faisait disparaître à la fermeture alors que le format de projet
    /// avait déjà un champ pour l'écrire : une donnée de morceau, au même
    /// titre que le tempo, rangée là où le morceau est rangé.
    bool loopEnabled = false;
    midi::Tick loopStartTick = 0;
    midi::Tick loopEndTick = 0;

    /// RÉGION DE PUNCH (D3.5) : entre ces deux ticks, et seulement là,
    /// l'enregistrement capte -- avant et après, on entend ce qui est déjà là.
    ///
    /// POURQUOI C'EST UNE DONNÉE DE MORCEAU et pas un réglage de session : on
    /// refait le même passage vingt fois, et devoir le redéfinir à chaque
    /// ouverture reviendrait à perdre l'endroit précis qu'on avait mis dix
    /// minutes à cerner. C'est la même raison qui a fait entrer la boucle dans
    /// le projet.
    bool punchEnabled = false;
    midi::Tick punchStartTick = 0;
    midi::Tick punchEndTick = 0;

    /// Réglages de la tranche master (égaliseur, compresseur, saturation,
    /// largeur, limiteur), nommés et non numérotés, comme les effets de piste.
    /// Vide = la tranche garde ses valeurs d'usine.
    ///
    /// POURQUOI ICI. Ils ne vivaient que dans l'objet `MasterBus` du moteur :
    /// ni sauvegardés, ni écrits dans le fichier, ni transmis au rendu hors
    /// ligne. Un morceau mixé se rouvrait donc avec un master d'usine, et son
    /// export ne ressemblait pas à ce qu'on venait d'écouter.
    std::map<std::string, float> masterParameters;

    static Project fromParsedFile(const midi::ParsedFile& parsed);
    midi::ParsedFile toParsedFile() const;

    double ticksToSeconds(midi::Tick tick) const { return tempoMap.ticksToSeconds(tick, ticksPerQuarterNote); }
    midi::Tick secondsToTicks(double seconds) const { return tempoMap.secondsToTicks(seconds, ticksPerQuarterNote); }

    /// Dernier tick utilisé par une note ou un événement de contrôle, tous
    /// pistes confondues (0 si le projet est vide). Utile pour la longueur
    /// d'affichage par défaut et les bornes d'export.
    midi::Tick lastUsedTick() const;

    uint64_t nextNoteId() { return nextNoteId_++; }
    /// Identifiants de CLIPS, comptés à part de ceux des notes : les deux ne se
    /// rencontrent jamais, et un compteur commun laisserait croire qu'un clip
    /// et une note pourraient être confondus.
    uint64_t nextClipId() { return nextClipId_++; }
    /// Donne un identifiant à tout clip qui n'en a pas encore. Appelée au
    /// chargement d'un projet, dont le format n'écrit pas les identifiants.
    void assignClipIds() {
        for (auto& track : tracks)
            for (auto& clip : track.clips)
                if (clip.id == 0) clip.id = nextClipId();
    }

    /// Prochain identifiant SANS le consommer. Utile avec les opérations de
    /// NoteEdit.h, qui prennent un compteur par référence et l'incrémentent
    /// elles-mêmes : on leur passe `peekNextNoteId() - 1`, puis on recale le
    /// projet avec ensureNoteIdAbove(). Sans ce couple, l'appelant devrait
    /// deviner combien de notes l'opération va créer -- et une erreur de
    /// comptage donnerait deux notes de MÊME identifiant, ce qui casserait
    /// silencieusement la sélection et l'annulation.
    uint64_t peekNextNoteId() const { return nextNoteId_; }
    void ensureNoteIdAbove(uint64_t usedId) { if (nextNoteId_ <= usedId) nextNoteId_ = usedId + 1; }
    uint64_t peekNextClipId() const { return nextClipId_; }
    void ensureClipIdAbove(uint64_t usedId) { if (nextClipId_ <= usedId) nextClipId_ = usedId + 1; }

private:
    uint64_t nextNoteId_ = 1;
    uint64_t nextClipId_ = 1;
};

/// Supprime une piste ET RÉPARE LES ROUTAGES QUI LA SUIVENT.
///
/// POURQUOI CE N'EST PAS UN `erase` DANS L'INTERFACE. Supprimer une piste
/// décale toutes les suivantes : un routage qui visait la piste 5 viserait la
/// 4, et le mixage partirait dans un autre groupe sans qu'aucun réglage n'ait
/// bougé. C'est exactement le défaut qui avait fait ranger les chaînes d'effets
/// DANS la piste (voir `TrackEffect`), et il revient dès qu'une piste en
/// référence une autre.
///
/// Les pistes qui allaient dans le groupe supprimé retournent au MASTER : c'est
/// le seul choix qui ne fasse pas disparaître leur son.
///
/// Dans `core/` et non dans l'application, parce que c'est une règle du MODÈLE
/// et qu'une règle qu'on ne peut pas tester n'est qu'une intention.
void removeTrack(Project& project, size_t index);

} // namespace vsm::sequencer
