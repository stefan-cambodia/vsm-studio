#include "vsm/sequencer/MidiRecorder.h"
#include <algorithm>
#include <deque>
#include <map>
#include <utility>

namespace vsm::sequencer {

using midi::Tick;

void MidiRecorder::begin(double startSeconds) {
    startSeconds_ = startSeconds;
    events_.clear();
}

void MidiRecorder::push(const RecordedNoteEvent& event) {
    if (event.seconds < startSeconds_) return;
    events_.push_back(event);
}

std::vector<Note> MidiRecorder::finish(double endSeconds,
                                        const std::function<Tick(double)>& secondsToTicks,
                                        uint64_t& idCounter) const {
    std::vector<Note> notes;
    if (events_.empty() || !secondsToTicks) return notes;

    // TRI STABLE, et il n'est pas décoratif. Les événements arrivent du thread
    // MIDI dans l'ordre, mais leur position est calculée par rapport à un
    // repère que le thread audio republie à chaque bloc : deux messages séparés
    // par une frontière de bloc peuvent, à la microseconde près, ressortir
    // inversés. Un tri STABLE remet l'ordre temporel sans jamais faire passer
    // un relâchement avant l'enfoncement qu'il ferme -- ce qu'un tri quelconque
    // ferait sur deux événements de même date.
    std::vector<RecordedNoteEvent> tries = events_;
    std::stable_sort(tries.begin(), tries.end(),
                      [](const RecordedNoteEvent& a, const RecordedNoteEvent& b) {
                          return a.seconds < b.seconds;
                      });

    /// Les enfoncements en attente d'un relâchement, par (canal, hauteur).
    struct Enfoncee {
        double seconds;
        uint8_t velocity;
    };
    std::map<std::pair<uint8_t, uint8_t>, std::deque<Enfoncee>> enAttente;

    // Ordre d'apparition des notes terminées, pour que la prise ressorte dans
    // l'ordre où elle a été JOUÉE et non dans celui où les touches ont été
    // relâchées.
    struct Appariee {
        double debut;
        double fin;
        uint8_t note;
        uint8_t velocity;
        uint8_t releaseVelocity;
        uint8_t channel;
    };
    std::vector<Appariee> appariees;

    for (const auto& ev : tries) {
        const auto cle = std::make_pair(ev.channel, ev.note);
        if (ev.noteOn) {
            enAttente[cle].push_back({ev.seconds, ev.velocity});
            continue;
        }
        auto it = enAttente.find(cle);
        // UN RELÂCHEMENT SANS ENFONCEMENT S'IGNORE, et c'est un vrai cas :
        // la touche était déjà tenue quand on a appuyé sur Rec. Inventer une
        // note qui commencerait au point d'entrée serait écrire ce qui n'a pas
        // été joué.
        if (it == enAttente.end() || it->second.empty()) continue;
        // PREMIER ENFONCÉ, PREMIER FERMÉ. Sur une même hauteur frappée deux
        // fois avant d'être relâchée une fois -- un trille appuyé, un accord
        // répété -- fermer le plus RÉCENT laisserait la première note traîner
        // jusqu'à la fin de la prise.
        const Enfoncee ouverte = it->second.front();
        it->second.pop_front();
        appariees.push_back({ouverte.seconds, ev.seconds, ev.note, ouverte.velocity,
                              ev.velocity, ev.channel});
    }

    // CE QUI EST ENCORE TENU SE FERME À L'ARRÊT. Une touche maintenue quand on
    // appuie sur Stop a bel et bien été jouée ; la jeter perdrait la dernière
    // note de chaque prise, qui est souvent la note tenue de la fin.
    for (auto& [cle, file] : enAttente)
        for (const auto& ouverte : file)
            appariees.push_back({ouverte.seconds, std::max(endSeconds, ouverte.seconds),
                                  cle.second, ouverte.velocity, 64, cle.first});

    std::stable_sort(appariees.begin(), appariees.end(),
                      [](const Appariee& a, const Appariee& b) { return a.debut < b.debut; });

    notes.reserve(appariees.size());
    for (const auto& a : appariees) {
        Note note;
        note.startTick = secondsToTicks(a.debut);
        note.endTick = secondsToTicks(a.fin);
        // UNE NOTE DE DURÉE NULLE N'EXISTE PAS. Une frappe très brève, ou un
        // tempo très lent, peut faire tomber le début et la fin sur le même
        // tick : la note serait invisible dans le piano roll et muette au
        // rendu. Un tick de durée est le minimum honnête -- on a joué quelque
        // chose, il doit rester quelque chose.
        if (note.endTick <= note.startTick) note.endTick = note.startTick + 1;
        note.channel = a.channel;
        note.number = a.note;
        note.velocity = a.velocity;
        note.releaseVelocity = a.releaseVelocity;
        note.id = idCounter++;
        notes.push_back(note);
    }
    return notes;
}

void applyRecording(Track& track, const std::vector<Note>& take, RecordMode mode,
                     Tick spanStart, Tick spanEnd) {
    if (mode == RecordMode::Replace && spanEnd > spanStart) {
        track.notes.erase(std::remove_if(track.notes.begin(), track.notes.end(),
                                          [spanStart, spanEnd](const Note& n) {
                                              return n.startTick >= spanStart && n.startTick < spanEnd;
                                          }),
                           track.notes.end());
    }
    for (Note note : take) {
        note.channel = track.channel;
        track.notes.push_back(note);
    }
    track.sortEvents();
}

} // namespace vsm::sequencer
