#pragma once
#include "vsm/sequencer/Track.h"
#include <cstdint>
#include <functional>
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
struct RecordedNoteEvent {
    double seconds = 0.0;
    uint8_t note = 60;
    uint8_t velocity = 100;
    uint8_t channel = 0;
    bool noteOn = true;
};

/// CE QU'ON FAIT DE CE QUI ÉTAIT DÉJÀ LÀ.
///
/// `Overdub` (le défaut) SUPERPOSE : la prise s'ajoute au matériau existant,
/// ce qui permet de construire une partie en plusieurs passes -- la grosse
/// caisse d'abord, la caisse claire par-dessus. `Replace` efface d'abord ce
/// que la prise recouvre.
enum class RecordMode { Overdub, Replace };

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
    /// Ouvre une prise. `startSeconds` est le POINT D'ENTRÉE : tout ce qui
    /// arrive avant est écarté, ce qui est précisément ce qu'il faut pendant un
    /// décompte -- on compte pour se caler, on ne joue pas encore.
    void begin(double startSeconds);

    /// Ajoute un événement. Ceux qui précèdent le point d'entrée sont ignorés
    /// en silence : ce n'est pas une perte, c'est la définition du point
    /// d'entrée.
    void push(const RecordedNoteEvent& event);

    bool empty() const { return events_.empty(); }
    size_t eventCount() const { return events_.size(); }
    double startSeconds() const { return startSeconds_; }

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

private:
    double startSeconds_ = 0.0;
    std::vector<RecordedNoteEvent> events_;
};

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
void applyRecording(Track& track, const std::vector<Note>& take, RecordMode mode,
                     midi::Tick spanStart, midi::Tick spanEnd);

} // namespace vsm::sequencer
