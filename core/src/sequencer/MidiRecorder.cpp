#include "vsm/sequencer/MidiRecorder.h"
#include <limits>
#include <algorithm>
#include <deque>
#include <map>
#include <utility>

namespace vsm::sequencer {

using midi::Tick;

void MidiRecorder::begin(double startSeconds, double endSeconds) {
    startSeconds_ = startSeconds;
    endSeconds_ = endSeconds;
    events_.clear();
}

void MidiRecorder::push(const RecordedNoteEvent& event) {
    // HORS DES BORNES, ON N'ÉCRIT PAS. Avant le point d'entrée c'est le
    // décompte ; après le point de sortie c'est le « punch out », où l'on
    // entend ce qui était déjà là. Dans les deux cas, jouer n'est pas
    // enregistrer.
    if (event.seconds < startSeconds_ || event.seconds > endSeconds_) return;
    events_.push_back(event);
}

bool MidiRecorder::hasPass(uint32_t pass) const {
    for (const auto& ev : events_)
        if (ev.pass == pass && ev.noteOn) return true;
    return false;
}

std::vector<Note> MidiRecorder::finish(double endSeconds,
                                        const std::function<Tick(double)>& secondsToTicks,
                                        uint64_t& idCounter) const {
    return apparier(events_, endSeconds, secondsToTicks, idCounter);
}

std::vector<Note> MidiRecorder::finishPass(uint32_t pass, double endSeconds,
                                            const std::function<Tick(double)>& secondsToTicks,
                                            uint64_t& idCounter) const {
    std::vector<RecordedNoteEvent> deLaPasse;
    deLaPasse.reserve(events_.size());
    for (const auto& ev : events_)
        if (ev.pass == pass) deLaPasse.push_back(ev);
    return apparier(deLaPasse, endSeconds, secondsToTicks, idCounter);
}

std::vector<Note> MidiRecorder::apparier(const std::vector<RecordedNoteEvent>& evenements,
                                          double endSeconds,
                                          const std::function<Tick(double)>& secondsToTicks,
                                          uint64_t& idCounter) const {
    std::vector<Note> notes;
    if (evenements.empty() || !secondsToTicks) return notes;

    // TRI STABLE, et il n'est pas décoratif. Les événements arrivent du thread
    // MIDI dans l'ordre, mais leur position est calculée par rapport à un
    // repère que le thread audio republie à chaque bloc : deux messages séparés
    // par une frontière de bloc peuvent, à la microseconde près, ressortir
    // inversés. Un tri STABLE remet l'ordre temporel sans jamais faire passer
    // un relâchement avant l'enfoncement qu'il ferme -- ce qu'un tri quelconque
    // ferait sur deux événements de même date.
    std::vector<RecordedNoteEvent> tries = evenements;
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

// ---------------------------------------------------------------------------
// LA CAPTURE RÉTROSPECTIVE (D17.3)
// ---------------------------------------------------------------------------

void RetrospectiveBuffer::push(const RecordedNoteEvent& event) {
    if (evenements_.size() < capacite_) {
        evenements_.push_back(event);
        return;
    }
    // PLEIN : le plus ancien cède la place. Un tampon qui refuserait les
    // nouveaux au lieu d'oublier les vieux garderait exactement ce dont on n'a
    // pas besoin -- ce qu'on veut récupérer, c'est ce qu'on vient de jouer.
    evenements_[debut_] = event;
    debut_ = (debut_ + 1) % capacite_;
}

std::vector<RecordedNoteEvent> RetrospectiveBuffer::events() const {
    std::vector<RecordedNoteEvent> ordonnes;
    ordonnes.reserve(evenements_.size());
    for (size_t i = 0; i < evenements_.size(); ++i)
        ordonnes.push_back(evenements_[(debut_ + i) % evenements_.size()]);
    return ordonnes;
}

double RetrospectiveBuffer::earliestSeconds() const {
    if (evenements_.empty()) return std::numeric_limits<double>::infinity();
    double plusTot = std::numeric_limits<double>::infinity();
    for (const auto& e : evenements_) plusTot = std::min(plusTot, e.seconds);
    return plusTot;
}

std::vector<Note> recoverRetrospective(const RetrospectiveBuffer& buffer,
                                        const std::function<midi::Tick(double)>& secondsToTicks,
                                        uint64_t& idCounter) {
    const auto evenements = buffer.events();
    if (evenements.empty()) return {};

    // UN ENREGISTREUR NEUF, ET C'EST TOUT LE DESSIN : l'appariement des touches
    // en notes est déjà écrit et déjà testé, avec ses cas tordus (une touche
    // encore tenue à la fin, un relâchement sans enfoncement). L'écrire une
    // seconde fois ici donnerait deux appariements qui finiraient par diverger.
    MidiRecorder enregistreur;
    // Le point d'entrée est le plus ancien événement gardé : sans cela, le
    // point d'entrée par défaut (zéro) écarterait tout ce qui a été joué avant
    // le début du morceau -- et l'on joue justement souvent avant.
    double fin = -std::numeric_limits<double>::infinity();
    for (const auto& e : evenements) fin = std::max(fin, e.seconds);
    enregistreur.begin(buffer.earliestSeconds());
    for (const auto& e : evenements) enregistreur.push(e);
    return enregistreur.finish(fin, secondsToTicks, idCounter);
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
