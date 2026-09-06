#pragma once
#include "vsm/sequencer/Track.h"
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace vsm::sequencer {

/// UNE TOUCHE ENFONCÉE OU RELÂCHÉE, HORODATÉE SUR LA LIGNE DE TEMPS.
///
/// `seconds` est une position de MORCEAU, pas une heure : c'est déjà le
/// résultat de la conversion faite par le moteur audio entre l'horloge du
/// système (à laquelle le pilote MIDI date ses messages) et l'horloge du
/// transport. Le rendre autrement -- en passant ici une heure système et la
/// carte de tempo -- ferait entrer le temps réel dans `core/`, qui n'en veut
/// pas et qui n'a pas à savoir qu'une carte son existe.
/// D24.2 : UN CONTRÔLEUR CAPTÉ -- molette de hauteur, CC, pression --, daté
/// en secondes comme une note, avec son numéro de passe. `value` porte la
/// valeur MIDI brute : 0..127 pour un CC ou une pression, -8192..8191 pour
/// la molette de hauteur.
struct RecordedControlEvent {
    enum class Kind : uint8_t { ControlChange, PitchBend, ChannelPressure, PolyPressure };
    double seconds = 0.0;
    Kind kind = Kind::ControlChange;
    uint8_t channel = 0;
    uint8_t index = 0;    ///< numéro de CC, ou de note pour la pression polyphonique
    int16_t value = 0;
    uint32_t pass = 0;
};

/// D24.2 : ce qu'une prise rend en plus des notes, en ticks, prêt à être
/// posé sur la piste.
struct RecordedControls {
    std::vector<CcPoint> controlChanges;
    std::vector<PitchBendPoint> pitchBends;
    std::vector<ChannelPressurePoint> channelPressure;
    std::vector<PolyAftertouchPoint> polyAftertouch;
    bool empty() const {
        return controlChanges.empty() && pitchBends.empty() && channelPressure.empty() && polyAftertouch.empty();
    }
};

struct RecordedNoteEvent {
    double seconds = 0.0;
    uint8_t note = 60;
    uint8_t velocity = 100;
    uint8_t channel = 0;
    bool noteOn = true;
    /// LE NUMÉRO DE PASSE, pour l'enregistrement en boucle (D3.5).
    ///
    /// Deux passes de boucle occupent EXACTEMENT les mêmes positions sur la
    /// ligne de temps : la date d'une note ne dit donc pas à laquelle elle
    /// appartient, et sans ce numéro les passes se mélangeraient en une
    /// bouillie. Il est posé au moment de la capture, d'après le compteur de
    /// rebouclages du moteur -- pas déduit après coup, ce qui serait impossible.
    uint32_t pass = 0;
};

/// CE QU'ON FAIT DE CE QUI ÉTAIT DÉJÀ LÀ.
///
/// `Overdub` (le défaut) SUPERPOSE : la prise s'ajoute au matériau existant,
/// ce qui permet de construire une partie en plusieurs passes -- la grosse
/// caisse d'abord, la caisse claire par-dessus. `Replace` efface d'abord ce
/// que la prise recouvre.
enum class RecordMode {
    Overdub,
    Replace,
    /// EMPILER : chaque passe devient une prise conservée, et la piste joue la
    /// dernière. C'est le mode de l'enregistrement en boucle, où l'on refait le
    /// même passage jusqu'à en tenir un bon -- superposer donnerait alors dix
    /// couches simultanées, et remplacer effacerait la seule bonne.
    Stack
};

/// L'ENREGISTREUR MIDI : il accumule des touches et rend des NOTES.
///
/// La différence n'est pas cosmétique. Le MIDI ne connaît que des instants
/// isolés -- une touche enfoncée, une touche relâchée -- alors que tout le
/// reste de ce logiciel (le piano roll, la quantification, l'export) travaille
/// sur des notes qui ont un début ET une fin. Apparier les deux est le seul
/// vrai travail de l'enregistrement, et c'est aussi là que se trouvent tous les
/// cas tordus : une touche encore tenue quand on arrête, un relâchement dont
/// l'enfoncement précède la prise, la même note frappée deux fois avant d'être
/// relâchée une fois.
///
/// La classe est PURE et ne connaît ni thread ni carte son : elle se teste
/// entièrement sans matériel, exactement comme `PlaybackScheduler`.
class MidiRecorder {
public:
    /// Ouvre une prise entre deux bornes. `startSeconds` est le POINT
    /// D'ENTRÉE : tout ce qui arrive avant est écarté, ce qui est précisément ce
    /// qu'il faut pendant un décompte -- on compte pour se caler, on ne joue pas
    /// encore. `endSeconds` est le POINT DE SORTIE (le « punch out ») : au-delà,
    /// on entend ce qui était déjà là et on n'écrit plus rien. Par défaut il
    /// n'y en a pas, et la prise dure jusqu'à ce qu'on l'arrête.
    void begin(double startSeconds,
                double endSeconds = std::numeric_limits<double>::infinity());

    /// Ajoute un événement. Ceux qui précèdent le point d'entrée sont ignorés
    /// en silence : ce n'est pas une perte, c'est la définition du point
    /// d'entrée.
    void push(const RecordedNoteEvent& event);
    /// D24.2 : un contrôleur, même règle du point d'entrée.
    void pushControl(const RecordedControlEvent& event);
    bool hasControls() const { return !controls_.empty(); }

    bool empty() const { return events_.empty(); }
    size_t eventCount() const { return events_.size(); }
    double startSeconds() const { return startSeconds_; }
    double endSeconds() const { return endSeconds_; }
    /// Vrai si la passe donnée a capté quelque chose. Une passe de boucle
    /// pendant laquelle on n'a rien joué ne doit pas produire une prise vide,
    /// qu'il faudrait ensuite écarter à la main.
    bool hasPass(uint32_t pass) const;

    /// Apparie tout et rend les notes de la prise, en TICKS.
    ///
    /// `secondsToTicks` est fournie par l'appelant pour la même raison que dans
    /// `AudioTrackSource` : la carte de tempo appartient au projet, et
    /// l'enregistreur n'a pas à la porter.
    ///
    /// CONST, et volontairement : la même prise est écrite sur CHAQUE piste
    /// armée, et chacune a besoin de ses propres identifiants de note. Une
    /// méthode qui viderait son tampon interdirait le deuxième appel.
    std::vector<Note> finish(double endSeconds,
                              const std::function<midi::Tick(double)>& secondsToTicks,
                              uint64_t& idCounter) const;

    /// Les notes d'UNE passe de boucle, et d'elle seule.
    ///
    /// Une note tenue par-dessus la frontière de boucle est fermée à la fin de
    /// sa passe, et son relâchement -- qui appartient à la passe suivante --
    /// est ignoré là-bas faute d'enfoncement. C'est exactement ce qu'on veut :
    /// chaque passe est un enregistrement complet en lui-même.
    std::vector<Note> finishPass(uint32_t pass, double endSeconds,
                                  const std::function<midi::Tick(double)>& secondsToTicks,
                                  uint64_t& idCounter) const;

    /// D24.2 : les contrôleurs de la prise, en ticks, triés ; ce qui dépasse
    /// le point de sortie est écarté. CONST pour la même raison que `finish`.
    RecordedControls finishControls(double endSeconds,
                                    const std::function<midi::Tick(double)>& secondsToTicks) const;

private:
    std::vector<Note> apparier(const std::vector<RecordedNoteEvent>& evenements, double endSeconds,
                                const std::function<midi::Tick(double)>& secondsToTicks,
                                uint64_t& idCounter) const;

    double startSeconds_ = 0.0;
    double endSeconds_ = std::numeric_limits<double>::infinity();
    std::vector<RecordedNoteEvent> events_;
    std::vector<RecordedControlEvent> controls_;
};

/// D24.2 : pose les contrôleurs d'une prise sur la piste. En mode
/// « remplacer », ce que la piste portait entre les deux bornes s'efface
/// d'abord ; en mode « superposer », ils s'ajoutent. Toujours triés par tick
/// à la fin : c'est ce que le planificateur attend.
void applyRecordedControls(Track& track, const RecordedControls& controls, bool replaceRange,
                           midi::Tick rangeStart, midi::Tick rangeEnd);

/// Écrit une prise dans une piste.
///
/// LE CANAL DE LA PISTE L'EMPORTE sur celui du clavier. Un clavier maître
/// transpose parfois son canal d'émission sans qu'on y prenne garde ; une prise
/// qui garderait ce canal ferait jouer la piste sur deux canaux à la fois, et
/// la moitié des notes sortirait par le mauvais bout à l'export.
///
/// EN MODE `Replace`, on efface les notes dont le DÉBUT tombe dans la prise,
/// pas celles qui la traversent. Une note tenue commencée avant le point
/// d'entrée appartient à ce qui précède ; l'effacer reviendrait à détruire hors
/// de la région qu'on a désignée.
/// LA CAPTURE RÉTROSPECTIVE (D17.3) — le Retrospective Record de Cubase, le
/// Capture MIDI de Live.
///
/// LE PROBLÈME QU'ELLE RÉSOUT, et il est banal : on joue une phrase sans avoir
/// armé quoi que ce soit, on la trouve bonne, et il n'existe aucun moyen de la
/// garder. C'est le seul geste d'un séquenceur dont la valeur est entièrement
/// dans ce qui a été fait AVANT qu'on y pense.
///
/// C'EST DONC UN TAMPON QUI TOURNE EN PERMANENCE, alimenté dès que
/// l'application tourne et non seulement pendant l'enregistrement — un tampon
/// qui ne se remplirait qu'une fois l'enregistrement lancé ne servirait à
/// rien. Il garde les `capacity` derniers ÉVÉNEMENTS (pas les dernières
/// minutes : une capacité en événements est bornée en mémoire quoi qu'on
/// joue, alors qu'une capacité en minutes ne l'est pas).
///
/// IL NE FAIT PAS L'APPARIEMENT LUI-MÊME. Récupérer, c'est reverser ses
/// événements dans un `MidiRecorder` neuf et lui demander ses notes : le seul
/// vrai travail — apparier des touches en notes, avec les cas tordus qui vont
/// avec — est déjà écrit et déjà testé, et l'écrire une seconde fois ici
/// donnerait deux appariements qui finiraient par diverger.
class RetrospectiveBuffer {
public:
    explicit RetrospectiveBuffer(size_t capacity = 4096) : capacite_(capacity ? capacity : 1) {}

    void push(const RecordedNoteEvent& event);
    /// D25.4 : un contrôleur, dans la même fenêtre et la même capacité.
    void pushControl(const RecordedControlEvent& event);
    /// D25.4 : les contrôleurs de la fenêtre [startSeconds, endSeconds], en
    /// ticks, triés ; sans vider le tampon (comme les notes).
    RecordedControls takeControls(double startSeconds, double endSeconds,
                                  const std::function<midi::Tick(double)>& secondsToTicks) const;

    void clear() { evenements_.clear(); debut_ = 0; controles_.clear(); debutControles_ = 0; }
    bool empty() const { return evenements_.empty() && controles_.empty(); }   // D25.4 : une molette seule se récupère aussi
    size_t size() const { return evenements_.size(); }

    /// Les événements gardés, du plus ancien au plus récent.
    std::vector<RecordedNoteEvent> events() const;

    /// LA POSITION DU PLUS ANCIEN ÉVÉNEMENT GARDÉ, en secondes de morceau, ou
    /// l'infini si le tampon est vide : c'est le point d'entrée à donner au
    /// `MidiRecorder` pour ne rien écarter.
    double earliestSeconds() const;

private:
    std::vector<RecordedNoteEvent> evenements_;
    size_t capacite_;
    size_t debut_ = 0;   ///< index du plus ancien, une fois le tampon plein
    /// D25.4 : les contrôleurs, même anneau, même capacité, à part -- une
    /// molette ne doit pas chasser les notes du tampon.
    std::vector<RecordedControlEvent> controles_;
    size_t debutControles_ = 0;
};

/// RÉCUPÉRER CE QUI VIENT D'ÊTRE JOUÉ (D17.3) : les notes du tampon, en ticks,
/// à leur place RÉELLE sur la ligne de temps — celle où elles ont été jouées,
/// pas le début du morceau. Vide si le tampon l'est.
std::vector<Note> recoverRetrospective(const RetrospectiveBuffer& buffer,
                                        const std::function<midi::Tick(double)>& secondsToTicks,
                                        uint64_t& idCounter);

void applyRecording(Track& track, const std::vector<Note>& take, RecordMode mode,
                     midi::Tick spanStart, midi::Tick spanEnd);

} // namespace vsm::sequencer
