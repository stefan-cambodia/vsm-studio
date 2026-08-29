#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <vector>

namespace vsm::sequencer {

using namespace vsm::midi;

namespace {
constexpr size_t kPendingSlots = 16 * 128; // canal * 128 + note

size_t slotFor(uint8_t channel, uint8_t note) {
    return static_cast<size_t>(channel) * 128 + note;
}
} // namespace

Project Project::fromParsedFile(const ParsedFile& parsed) {
    Project project;
    project.exportFormat = parsed.format;
    project.ticksPerQuarterNote = parsed.isSmpteTiming ? 480 : parsed.ticksPerQuarterNote;
    project.timeSignatureMap.clear();

    if (parsed.isSmpteTiming) {
        double fps = static_cast<double>(-parsed.smpteFramesPerSecond);
        project.tempoMap = TempoMap::smpte(fps, parsed.smpteTicksPerFrame);
    } else {
        project.tempoMap.clearTempoChanges();
    }

    bool tempoFound = false;
    bool timeSigFound = false;

    for (const auto& parsedTrack : parsed.tracks) {
        Track track;
        track.name = parsedTrack.name;

        std::vector<std::vector<std::pair<Tick, uint8_t>>> pending(kPendingSlots);
        bool channelDetected = false;

        for (const auto& ev : parsedTrack.events) {
            std::visit([&](auto&& data) {
                using T = std::decay_t<decltype(data)>;

                if constexpr (std::is_same_v<T, NoteOnEvent>) {
                    if (!channelDetected) { track.channel = data.channel; channelDetected = true; }
                    if (data.velocity > 0) {
                        pending[slotFor(data.channel, data.note)].push_back({ev.tick, data.velocity});
                    } else {
                        // Note On vélocité 0 == Note Off implicite (convention MIDI standard)
                        auto& stack = pending[slotFor(data.channel, data.note)];
                        if (!stack.empty()) {
                            auto [startTick, startVel] = stack.front();
                            stack.erase(stack.begin());
                            uint64_t id = project.nextNoteId();
                            track.notes.push_back(Note{startTick, ev.tick, data.channel, data.note,
                                                        startVel, 64, id});
                        }
                    }
                } else if constexpr (std::is_same_v<T, NoteOffEvent>) {
                    if (!channelDetected) { track.channel = data.channel; channelDetected = true; }
                    auto& stack = pending[slotFor(data.channel, data.note)];
                    if (!stack.empty()) {
                        auto [startTick, startVel] = stack.front();
                        stack.erase(stack.begin());
                        uint64_t id = project.nextNoteId();
                        track.notes.push_back(Note{startTick, ev.tick, data.channel, data.note,
                                                    startVel, data.velocity, id});
                    }
                } else if constexpr (std::is_same_v<T, ControlChangeEvent>) {
                    if (!channelDetected) { track.channel = data.channel; channelDetected = true; }
                    track.controlChanges.push_back({ev.tick, data.channel, data.controller, data.value});
                } else if constexpr (std::is_same_v<T, PitchBendEvent>) {
                    if (!channelDetected) { track.channel = data.channel; channelDetected = true; }
                    track.pitchBends.push_back({ev.tick, data.channel, data.value});
                } else if constexpr (std::is_same_v<T, PolyPressureEvent>) {
                    track.polyAftertouch.push_back({ev.tick, data.channel, data.note, data.pressure});
                } else if constexpr (std::is_same_v<T, ChannelPressureEvent>) {
                    track.channelPressure.push_back({ev.tick, data.channel, data.pressure});
                } else if constexpr (std::is_same_v<T, ProgramChangeEvent>) {
                    track.programChanges.push_back({ev.tick, data.channel, data.program});
                } else if constexpr (std::is_same_v<T, TempoEvent>) {
                    project.tempoMap.addTempoChange(ev.tick, data.microsecondsPerQuarterNote);
                    tempoFound = true;
                } else if constexpr (std::is_same_v<T, TimeSignatureEvent>) {
                    project.timeSignatureMap.addChange(ev.tick, data.numerator, data.denominatorPow2);
                    timeSigFound = true;
                } else if constexpr (std::is_same_v<T, TrackNameEvent> ||
                                      std::is_same_v<T, EndOfTrackEvent>) {
                    // déjà géré (nom capturé par le parser / fin de piste implicite)
                } else if constexpr (std::is_same_v<T, TextMetaEvent>) {
                    // REPÈRES ET POINTS DE REPRISE (méta 0x06 et 0x07) : promus
                    // au rang d'entités du projet au lieu d'être conservés en
                    // octets opaques. Ils traversaient le logiciel sans exister
                    // pour lui -- lus, réexportés fidèlement, et invisibles.
                    //
                    // Ils deviennent GLOBAUX, et c'est ce qu'ils sont
                    // musicalement : « refrain » ne repère pas un endroit de la
                    // piste de basse, il repère un endroit du morceau. Un
                    // fichier qui en portait sur une piste autre que la
                    // première les verra donc écrits sur la première au
                    // réexport ; la position et le nom, eux, sont préservés.
                    if (data.metaType == 0x06 || data.metaType == 0x07)
                        project.markers.push_back({ev.tick, data.text});
                    else
                        track.miscEvents.push_back(ev);
                } else {
                    // SysExEvent, UnknownMetaEvent : conservés tels quels
                    track.miscEvents.push_back(ev);
                }
            }, ev.data);
        }

        // Notes orphelines (Note On sans Note Off correspondant) : fichier
        // malformé ou tronqué. On les referme au dernier tick connu de la
        // piste plutôt que de perdre l'information.
        Tick trackEndTick = parsedTrack.events.empty() ? 0 : parsedTrack.events.back().tick;
        for (size_t slot = 0; slot < pending.size(); ++slot) {
            for (auto& [startTick, vel] : pending[slot]) {
                uint8_t channel = static_cast<uint8_t>(slot / 128);
                uint8_t note = static_cast<uint8_t>(slot % 128);
                Tick end = std::max(trackEndTick, startTick + project.ticksPerQuarterNote / 4);
                uint64_t id = project.nextNoteId();
                track.notes.push_back(Note{startTick, end, channel, note, vel, 64, id});
            }
        }

        track.sortEvents();
        project.tracks.push_back(std::move(track));
    }

    (void)tempoFound;
    (void)timeSigFound;
    return project;
}

ParsedFile Project::toParsedFile() const {
    ParsedFile parsed;
    parsed.format = exportFormat;
    parsed.ticksPerQuarterNote = ticksPerQuarterNote;
    parsed.isSmpteTiming = false; // Phase 1 : export en timing métrique uniquement

    size_t trackCount = std::max<size_t>(tracks.size(), 1);
    parsed.tracks.resize(trackCount);

    // Piste 0 ("conductor") : tempo + signature rythmique, quel que soit le
    // format d'export (convention Type 1 ; sans effet audible en Type 0 où
    // tout est fusionné de toute façon).
    auto& conductorEvents = parsed.tracks[0].events;
    for (const auto& change : tempoMap.changes())
        conductorEvents.push_back({change.tick, TempoEvent{change.microsecondsPerQuarterNote}});
    for (const auto& change : timeSignatureMap.changes())
        conductorEvents.push_back({change.tick, TimeSignatureEvent{
            change.numerator, change.denominatorPow2, 24, 8}});

    for (size_t i = 0; i < tracks.size(); ++i) {
        const Track& t = tracks[i];
        auto& events = parsed.tracks[i].events;
        parsed.tracks[i].name = t.name;

        if (!t.name.empty())
            events.push_back({0, TrackNameEvent{t.name}});

        for (const auto& note : t.notes) {
            // Les notes rendues muettes dans l'éditeur ne sont pas exportées :
            // le SMF n'a aucun moyen de représenter "présente mais silencieuse",
            // et les écrire produirait un fichier qui joue autre chose que ce
            // qu'on entend dans l'application (voir Note::muted).
            if (note.muted) continue;
            events.push_back({note.startTick, NoteOnEvent{note.channel, note.number, note.velocity}});
            events.push_back({note.endTick, NoteOffEvent{note.channel, note.number, note.releaseVelocity}});
        }
        for (const auto& cc : t.controlChanges)
            events.push_back({cc.tick, ControlChangeEvent{cc.channel, cc.controller, cc.value}});
        for (const auto& pb : t.pitchBends)
            events.push_back({pb.tick, PitchBendEvent{pb.channel, pb.value}});
        for (const auto& pa : t.polyAftertouch)
            events.push_back({pa.tick, PolyPressureEvent{pa.channel, pa.note, pa.pressure}});
        for (const auto& cp : t.channelPressure)
            events.push_back({cp.tick, ChannelPressureEvent{cp.channel, cp.pressure}});
        for (const auto& pc : t.programChanges)
            events.push_back({pc.tick, ProgramChangeEvent{pc.channel, pc.program}});
        for (const auto& misc : t.miscEvents)
            events.push_back(misc);

        // Les repères sont globaux : écrits une seule fois, sur la première
        // piste. Les écrire sur chacune les multiplierait par le nombre de
        // pistes à chaque aller-retour.
        if (&t == &tracks.front())
            for (const auto& marker : markers)
                events.push_back({marker.tick, TextMetaEvent{0x06, marker.name}});

        std::stable_sort(events.begin(), events.end(),
                          [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    }

    return parsed;
}

midi::Tick Project::lastUsedTick() const {
    Tick last = 0;
    for (const auto& t : tracks) {
        for (const auto& n : t.notes) last = std::max(last, n.endTick);
        for (const auto& c : t.controlChanges) last = std::max(last, c.tick);
        for (const auto& p : t.pitchBends) last = std::max(last, p.tick);
    }
    return last;
}

void moveTrack(Project& project, size_t from, size_t to) {
    const size_t n = project.tracks.size();
    if (from >= n || to >= n || from == to) return;

    // LES ROUTAGES SUIVENT LES PISTES, PAS LES INDEX. On note d'abord, pour
    // chaque piste, VERS QUELLE PISTE elle envoie -- une identité qui survivra
    // au remaniement --, puis on retrouve les nouveaux index après coup. Tenter
    // de corriger les index au fil du déplacement demanderait de raisonner sur
    // trois cas de figure, et le troisième serait faux.
    std::vector<const Track*> destinations(n, nullptr);
    for (size_t i = 0; i < n; ++i) {
        const int cible = project.tracks[i].outputGroup;
        if (cible >= 0 && static_cast<size_t>(cible) < n)
            destinations[i] = &project.tracks[static_cast<size_t>(cible)];
    }
    // Les adresses doivent rester valides : on déplace dans un vecteur de
    // pointeurs, pas dans le vecteur de pistes.
    std::vector<Track*> ordre(n);
    for (size_t i = 0; i < n; ++i) ordre[i] = &project.tracks[i];

    std::vector<Track> remaniees;
    remaniees.reserve(n);
    std::vector<const Track*> anciennesAdresses;
    anciennesAdresses.reserve(n);
    {
        std::vector<Track*> deplace = ordre;
        Track* saisie = deplace[from];
        deplace.erase(deplace.begin() + static_cast<std::ptrdiff_t>(from));
        deplace.insert(deplace.begin() + static_cast<std::ptrdiff_t>(to), saisie);
        for (Track* t : deplace) {
            anciennesAdresses.push_back(t);
            remaniees.push_back(*t);
        }
    }

    // Où chaque ANCIENNE piste se trouve-t-elle désormais ?
    for (size_t i = 0; i < n; ++i) {
        const Track* destination = nullptr;
        for (size_t j = 0; j < n; ++j)
            if (anciennesAdresses[i] == ordre[j]) { destination = destinations[j]; break; }
        int nouveau = -1;
        if (destination != nullptr)
            for (size_t j = 0; j < n; ++j)
                if (anciennesAdresses[j] == destination) { nouveau = static_cast<int>(j); break; }
        remaniees[i].outputGroup = nouveau;
    }
    project.tracks = std::move(remaniees);
}

void removeTrack(Project& project, size_t index) {
    if (index >= project.tracks.size()) return;
    project.tracks.erase(project.tracks.begin() + static_cast<std::ptrdiff_t>(index));
    for (auto& piste : project.tracks) {
        if (piste.outputGroup < 0) continue;
        const size_t cible = static_cast<size_t>(piste.outputGroup);
        if (cible == index) piste.outputGroup = -1;      // le groupe n'existe plus
        else if (cible > index) piste.outputGroup -= 1;  // il a reculé d'un rang
    }
}

} // namespace vsm::sequencer
