#include "vsm/sequencer/Project.h"
#include "vsm/sequencer/NoteEdit.h"
#include <set>
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


// --- D6.3 : ce que le SMF ne sait pas dire, écrit là où il l'ignorera --------
//
// LE PROBLÈME. Deux propriétés d'une note n'existent pas dans le format SMF :
// `muted` (présente mais silencieuse) et `confidence` (le degré de certitude
// d'une transcription). Jusqu'ici elles disparaissaient à l'export, sans
// avertissement : exporter puis réimporter son propre morceau démuselait les
// notes qu'on avait tues et effaçait le travail de vérification d'une
// transcription. Un aller-retour qui perd du travail est un piège.
//
// CE QU'ON NE FAIT PAS. Écrire les notes muettes dans le flux de notes en les
// marquant à côté : le fichier jouerait alors autre chose que ce qu'on entend
// dans l'application, et tout autre logiciel les ferait sonner. La règle tient
// et ne bouge pas -- LE FICHIER JOUE CE QU'ON ENTEND.
//
// CE QU'ON FAIT. Un événement méta 0x7F (Sequencer Specific), que la norme
// réserve précisément à cela et que tout autre logiciel ignore : il n'y a rien
// à comprendre dedans pour qui ne le connaît pas. On y écrit les notes muettes
// EN ENTIER (elles ne sont nulle part ailleurs) et les confiances des notes qui
// ne valent pas 1. Un aller-retour VSM -> .mid -> VSM redevient fidèle ; un
// aller-retour VSM -> .mid -> autre logiciel n'y perd rien qu'il aurait pu
// lire.
namespace {

constexpr uint8_t kVsmMetaType = 0x7F;
/// 0x7D est l'identifiant « non commercial » que la norme MIDI laisse aux
/// usages privés ; « VS » derrière lui suffit à ne pas confondre notre bloc
/// avec celui d'un autre logiciel qui aurait fait le même choix.
constexpr uint8_t kVsmSignature[3] = {0x7D, 'V', 'S'};
constexpr uint8_t kVsmBlockVersion = 1;

void pushBE32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

uint32_t readBE32(const std::vector<uint8_t>& data, size_t& at) {
    const uint32_t value = (static_cast<uint32_t>(data[at]) << 24)
                          | (static_cast<uint32_t>(data[at + 1]) << 16)
                          | (static_cast<uint32_t>(data[at + 2]) << 8)
                          | static_cast<uint32_t>(data[at + 3]);
    at += 4;
    return value;
}

void pushConfidence(std::vector<uint8_t>& out, float confidence) {
    const float borne = confidence < 0.0f ? 0.0f : (confidence > 1.0f ? 1.0f : confidence);
    const uint16_t quantifiee = static_cast<uint16_t>(borne * 65535.0f + 0.5f);
    out.push_back(static_cast<uint8_t>(quantifiee >> 8));
    out.push_back(static_cast<uint8_t>(quantifiee & 0xFF));
}

float readConfidence(const std::vector<uint8_t>& data, size_t& at) {
    const uint16_t quantifiee = static_cast<uint16_t>((data[at] << 8) | data[at + 1]);
    at += 2;
    return static_cast<float>(quantifiee) / 65535.0f;
}

/// Le bloc privé d'une piste, ou vide s'il n'y a rien à dire -- et alors aucun
/// événement n'est écrit : un fichier sans note muette et sans transcription
/// ne doit porter aucune trace de ce mécanisme.
std::vector<uint8_t> encodeVsmNoteBlock(const Track& track) {
    std::vector<uint8_t> muettes, confiances;
    uint32_t nbMuettes = 0, nbConfiances = 0;
    for (const auto& note : track.notes) {
        if (note.muted) {
            ++nbMuettes;
            pushBE32(muettes, static_cast<uint32_t>(note.startTick));
            pushBE32(muettes, static_cast<uint32_t>(note.endTick));
            muettes.push_back(note.channel);
            muettes.push_back(note.number);
            muettes.push_back(note.velocity);
            muettes.push_back(note.releaseVelocity);
            pushConfidence(muettes, note.confidence);
        } else if (note.confidence < 1.0f) {
            // Une note SONNANTE est retrouvée par sa position, sa hauteur et
            // son canal : elle est déjà dans le flux, on n'en réécrit pas la
            // vélocité.
            ++nbConfiances;
            pushBE32(confiances, static_cast<uint32_t>(note.startTick));
            confiances.push_back(note.channel);
            confiances.push_back(note.number);
            pushConfidence(confiances, note.confidence);
        }
    }
    if (nbMuettes == 0 && nbConfiances == 0) return {};

    std::vector<uint8_t> bloc(std::begin(kVsmSignature), std::end(kVsmSignature));
    bloc.push_back(kVsmBlockVersion);
    pushBE32(bloc, nbMuettes);
    bloc.insert(bloc.end(), muettes.begin(), muettes.end());
    pushBE32(bloc, nbConfiances);
    bloc.insert(bloc.end(), confiances.begin(), confiances.end());
    return bloc;
}

/// Vrai si l'événement est NOTRE bloc. Un 0x7F d'un autre logiciel doit
/// continuer son chemin dans `miscEvents` : le réécrire tel quel est ce que
/// fait déjà le reste du projet, et le manger ici l'effacerait.
bool isVsmNoteBlock(const UnknownMetaEvent& meta) {
    return meta.metaType == kVsmMetaType && meta.data.size() >= 8
        && meta.data[0] == kVsmSignature[0] && meta.data[1] == kVsmSignature[1]
        && meta.data[2] == kVsmSignature[2] && meta.data[3] == kVsmBlockVersion;
}

/// Repose `muted` et `confidence` sur une piste déjà reconstruite. Les notes
/// muettes n'existant nulle part ailleurs, elles sont CRÉÉES ici.
void applyVsmNoteBlock(const std::vector<uint8_t>& data, Track& track, Project& project) {
    size_t at = 4;
    if (at + 4 > data.size()) return;
    const uint32_t nbMuettes = readBE32(data, at);
    for (uint32_t i = 0; i < nbMuettes; ++i) {
        if (at + 14 > data.size()) return;
        Note note;
        note.startTick = readBE32(data, at);
        note.endTick = readBE32(data, at);
        note.channel = data[at++];
        note.number = data[at++];
        note.velocity = data[at++];
        note.releaseVelocity = data[at++];
        note.confidence = readConfidence(data, at);
        note.muted = true;
        note.id = project.nextNoteId();
        track.notes.push_back(note);
    }
    if (at + 4 > data.size()) return;
    const uint32_t nbConfiances = readBE32(data, at);
    for (uint32_t i = 0; i < nbConfiances; ++i) {
        if (at + 8 > data.size()) return;
        const Tick start = readBE32(data, at);
        const uint8_t canal = data[at++];
        const uint8_t numero = data[at++];
        const float confiance = readConfidence(data, at);
        for (auto& note : track.notes)
            if (!note.muted && note.startTick == start && note.channel == canal
                && note.number == numero)
                note.confidence = confiance;
    }
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
        std::vector<uint8_t> blocVsm;

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
                } else if constexpr (std::is_same_v<T, UnknownMetaEvent>) {
                    // NOTRE bloc est CONSOMMÉ (relu plus bas, une fois les
                    // notes reconstruites) ; celui d'un autre logiciel
                    // poursuit son chemin intact.
                    if (isVsmNoteBlock(data)) blocVsm = data.data;
                    else track.miscEvents.push_back(ev);
                } else {
                    // SysExEvent : conservé tel quel
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

        // D6.3 : APRÈS l'appariement des notes, jamais pendant -- les
        // confiances désignent des notes qui n'existent pas encore au moment
        // où le bloc est lu, et les notes muettes s'ajoutent à celles-là.
        if (!blocVsm.empty()) applyVsmNoteBlock(blocVsm, track, project);

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
    // LES RAMPES EN PALIERS D'UNE NOIRE (D15.5) : le fichier MIDI ne connaît
    // que le palier. La durée totale est conservée, la courbe approchée.
    for (const auto& change : tempoMap.flattened(ticksPerQuarterNote, ticksPerQuarterNote))
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

        // D6.3 : le bloc privé, posé au tick 0 avec le nom de la piste. Rien
        // n'est écrit quand il n'y a rien à dire.
        if (const auto bloc = encodeVsmNoteBlock(t); !bloc.empty())
            events.push_back({0, UnknownMetaEvent{kVsmMetaType, bloc}});

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

midi::Tick Project::lastSoundingTick() const {
    Tick last = lastUsedTick();
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            last = std::max(last, c.startTick + std::max<Tick>(c.length, 0));
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
    // D18.7b : LA SOURCE D'UNE SORTIE PUBLIÉE EST UN INDEX ELLE AUSSI, et elle
    // doit suivre la piste par le même chemin -- sinon déplacer une piste
    // ferait porter à la caisse claire la sortie d'une autre machine, ce qui
    // s'entend mais ne se comprend pas.
    std::vector<const Track*> sources(n, nullptr);
    for (size_t i = 0; i < n; ++i) {
        const int cible = project.tracks[i].outputSourceTrack;
        if (cible >= 0 && static_cast<size_t>(cible) < n)
            sources[i] = &project.tracks[static_cast<size_t>(cible)];
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
        const Track* source = nullptr;
        for (size_t j = 0; j < n; ++j)
            if (anciennesAdresses[i] == ordre[j]) {
                destination = destinations[j];
                source = sources[j];
                break;
            }
        auto rangDe = [&](const Track* qui) {
            if (qui == nullptr) return -1;
            for (size_t j = 0; j < n; ++j)
                if (anciennesAdresses[j] == qui) return static_cast<int>(j);
            return -1;
        };
        remaniees[i].outputGroup = rangDe(destination);
        const int rangSource = rangDe(source);
        remaniees[i].outputSourceTrack = rangSource;
        // Une publication dont la source a disparu ne désigne plus rien : son
        // index part avec elle plutôt que de rester à pointer au hasard.
        if (rangSource < 0) remaniees[i].outputIndex = 0;
    }
    project.tracks = std::move(remaniees);
}

size_t duplicateTrack(Project& project, size_t index) {
    if (index >= project.tracks.size()) return index;
    Track copie = project.tracks[index];
    copie.name = copie.name.empty() ? "(copie)" : copie.name + " (copie)";
    copie.armed = false;
    for (auto& note : copie.notes) note.id = project.nextNoteId();
    for (auto& clip : copie.clips) clip.id = project.nextClipId();
    // Les groupes situés APRÈS l'original reculent d'un rang pour tout le
    // monde, y compris pour la copie et l'original.
    const int insere = static_cast<int>(index) + 1;
    for (auto& piste : project.tracks) {
        if (piste.outputGroup >= insere) piste.outputGroup += 1;
        if (piste.outputSourceTrack >= insere) piste.outputSourceTrack += 1;
    }
    if (copie.outputGroup >= insere) copie.outputGroup += 1;
    if (copie.outputSourceTrack >= insere) copie.outputSourceTrack += 1;
    project.tracks.insert(project.tracks.begin() + insere, std::move(copie));
    return static_cast<size_t>(insere);
}

void removeTrack(Project& project, size_t index) {
    if (index >= project.tracks.size()) return;
    project.tracks.erase(project.tracks.begin() + static_cast<std::ptrdiff_t>(index));
    for (auto& piste : project.tracks) {
        if (piste.outputGroup >= 0) {
            const size_t cible = static_cast<size_t>(piste.outputGroup);
            if (cible == index) piste.outputGroup = -1;      // le groupe n'existe plus
            else if (cible > index) piste.outputGroup -= 1;  // il a reculé d'un rang
        }
        // D18.7b : MÊME RÈGLE POUR LA SOURCE D'UNE SORTIE PUBLIÉE. Supprimer
        // la piste qui portait la machine ne laisse pas six pistes à pointer
        // dans le vide : elles cessent simplement de publier, et redeviennent
        // des pistes ordinaires.
        if (piste.outputSourceTrack >= 0) {
            const size_t cible = static_cast<size_t>(piste.outputSourceTrack);
            if (cible == index) { piste.outputSourceTrack = -1; piste.outputIndex = 0; }
            else if (cible > index) piste.outputSourceTrack -= 1;
        }
    }
}

size_t publishInstrumentOutputs(Project& project, size_t source,
                                 const std::vector<std::string>& outputNames) {
    if (source >= project.tracks.size() || outputNames.size() < 2) return 0;

    // QUI PUBLIE DÉJÀ QUOI. On ne republie pas une sortie qui a sa piste :
    // c'est ce qui rend la commande rejouable sans dégât.
    std::vector<bool> deja(outputNames.size(), false);
    for (const auto& piste : project.tracks) {
        if (!piste.publishesInstrumentOutput()) continue;
        if (piste.outputSourceTrack != static_cast<int>(source)) continue;
        const size_t k = static_cast<size_t>(piste.outputIndex);
        if (k < deja.size()) deja[k] = true;
    }

    std::vector<Track> neuves;
    for (size_t k = 1; k < outputNames.size(); ++k) {
        if (deja[k]) continue;
        Track piste;
        piste.kind = Track::Kind::Midi;
        piste.name = outputNames[k].empty()
                          ? project.tracks[source].name + " - sortie " + std::to_string(k)
                          : outputNames[k];
        piste.colorRgba = project.tracks[source].colorRgba;
        piste.channel = project.tracks[source].channel;
        piste.outputSourceTrack = static_cast<int>(source);
        piste.outputIndex = static_cast<int>(k);
        neuves.push_back(std::move(piste));
    }
    if (neuves.empty()) return 0;

    // TOUT CE QUI POINTE APRÈS LE POINT D'INSERTION RECULE D'AUTANT, et les
    // pistes neuves ne sont ajoutées qu'ENSUITE : leurs propres index sont
    // déjà justes puisqu'elles désignent la source, qui ne bouge pas.
    const int insere = static_cast<int>(source) + 1;
    const int combien = static_cast<int>(neuves.size());
    for (auto& piste : project.tracks) {
        if (piste.outputGroup >= insere) piste.outputGroup += combien;
        if (piste.outputSourceTrack >= insere) piste.outputSourceTrack += combien;
    }
    project.tracks.insert(project.tracks.begin() + insere,
                           std::make_move_iterator(neuves.begin()),
                           std::make_move_iterator(neuves.end()));
    return static_cast<size_t>(combien);
}

size_t explodeTrackByPitch(Project& project, size_t index,
                            const std::function<std::string(uint8_t)>& nameFor) {
    if (index >= project.tracks.size()) return 0;
    Track& source = project.tracks[index];

    // QUELLES HAUTEURS SONT PRÉSENTES. Un `set` plutôt qu'un tri : on veut
    // l'ordre croissant et l'unicité, et le nombre de hauteurs d'une piste de
    // batterie se compte sur les doigts.
    std::set<uint8_t> hauteurs;
    for (const Note& n : source.notes) hauteurs.insert(n.number);
    // UNE SEULE HAUTEUR N'A RIEN À ÉCLATER, et zéro non plus. Rendre 0 plutôt
    // que créer une piste vide : la commande n'a pas échoué, elle n'avait rien
    // à faire, et l'appelant le DIT.
    if (hauteurs.size() < 2) return 0;

    // LA PLUS GRAVE RESTE SUR LA PISTE D'ORIGINE (voir l'en-tête) : on ne
    // fabrique donc une piste que pour les suivantes.
    std::vector<Track> neuves;
    neuves.reserve(hauteurs.size() - 1);
    bool premiere = true;
    for (uint8_t hauteur : hauteurs) {
        if (premiere) { premiere = false; continue; }
        Track piste;
        piste.kind = source.kind;
        piste.instrumentId = source.instrumentId;
        piste.channel = source.channel;
        piste.colorRgba = source.colorRgba;
        piste.volume = source.volume;
        piste.pan = source.pan;
        piste.outputGroup = source.outputGroup;
        const std::string nom = nameFor ? nameFor(hauteur) : std::string();
        piste.name = !nom.empty() ? nom
                                   : source.name + " - " + noteNumberToName(hauteur);
        // LES NOTES SONT DÉPLACÉES : elles quittent la piste d'origine plus
        // bas, une fois toutes les pistes constituées.
        for (const Note& n : source.notes)
            if (n.number == hauteur) piste.notes.push_back(n);
        // LES CLIPS SONT DES FENÊTRES sur le matériau, pas des conteneurs : la
        // découpe de la piste d'origine vaut pour chacune de ses pièces, et
        // l'oublier ferait sonner les pièces éclatées là où l'originale se
        // taisait.
        for (const Clip& c : source.clips) {
            Clip copie = c;
            copie.id = project.nextClipId();
            piste.clips.push_back(copie);
        }
        neuves.push_back(std::move(piste));
    }
    if (neuves.empty()) return 0;

    // LA PISTE D'ORIGINE NE GARDE QUE LA PLUS GRAVE.
    const uint8_t gardee = *hauteurs.begin();
    std::vector<Note> restantes;
    restantes.reserve(source.notes.size());
    for (const Note& n : source.notes)
        if (n.number == gardee) restantes.push_back(n);
    source.notes = std::move(restantes);
    if (const std::string nom = nameFor ? nameFor(gardee) : std::string(); !nom.empty())
        source.name = nom;

    // TOUT CE QUI POINTE APRÈS LE POINT D'INSERTION RECULE D'AUTANT, comme
    // pour `publishInstrumentOutputs` et `duplicateTrack`.
    const int insere = static_cast<int>(index) + 1;
    const int combien = static_cast<int>(neuves.size());
    for (auto& piste : project.tracks) {
        if (piste.outputGroup >= insere) piste.outputGroup += combien;
        if (piste.outputSourceTrack >= insere) piste.outputSourceTrack += combien;
    }
    project.tracks.insert(project.tracks.begin() + insere,
                           std::make_move_iterator(neuves.begin()),
                           std::make_move_iterator(neuves.end()));
    return static_cast<size_t>(combien);
}

std::vector<size_t> folderContents(const Project& project, size_t index) {
    std::vector<size_t> contenu;
    if (index >= project.tracks.size() || !project.tracks[index].isFolder()) return contenu;
    const int profondeur = project.tracks[index].folderDepth;
    for (size_t t = index + 1; t < project.tracks.size(); ++t) {
        // ON S'ARRÊTE À LA PREMIÈRE PISTE QUI N'EST PAS PLUS PROFONDE : elle
        // est soit la voisine du dossier, soit celle qui le suit dans un
        // dossier parent. Dans les deux cas, elle n'est pas dedans.
        if (project.tracks[t].folderDepth <= profondeur) break;
        contenu.push_back(t);
    }
    return contenu;
}

bool hiddenByCollapsedFolder(const Project& project, size_t index) {
    if (index >= project.tracks.size()) return false;
    const int profondeur = project.tracks[index].folderDepth;
    if (profondeur <= 0) return false;   // à la racine : aucun dossier au-dessus
    // ON REMONTE VERS LE HAUT DE LA LISTE en cherchant, à chaque niveau, le
    // dossier qui contient la piste. Dès qu'un ANCÊTRE est replié, la piste est
    // cachée -- même si le dossier intermédiaire, lui, est déplié : un tiroir
    // fermé ne laisse pas dépasser ce qu'il contient.
    int niveau = profondeur;
    for (size_t t = index; t > 0; --t) {
        const Track& candidat = project.tracks[t - 1];
        if (candidat.folderDepth >= niveau) continue;      // pas un ancêtre
        // Premier moins profond : c'est le contenant de ce niveau.
        if (candidat.isFolder() && candidat.folded) return true;
        niveau = candidat.folderDepth;
        if (niveau <= 0) break;
    }
    return false;
}

size_t normalizeFolderDepths(Project& project) {
    size_t corrigees = 0;
    int precedente = -1;              // profondeur de la piste d'avant
    bool precedenteEstDossier = false;
    for (auto& piste : project.tracks) {
        // Le plafond : un cran de plus que la précédente, et SEULEMENT si
        // celle-ci est un dossier. Sinon on reste à son niveau.
        const int plafond = precedente < 0 ? 0
                            : (precedenteEstDossier ? precedente + 1 : precedente);
        const int voulue = std::max(0, std::min(piste.folderDepth, plafond));
        if (voulue != piste.folderDepth) { piste.folderDepth = voulue; ++corrigees; }
        precedente = piste.folderDepth;
        precedenteEstDossier = piste.isFolder();
    }
    return corrigees;
}

} // namespace vsm::sequencer
