#include "vsm/midi/MidiFileWriter.h"
#include <array>
#include "vsm/midi/TextEncoding.h"
#include "vsm/midi/VariableLengthQuantity.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace vsm::midi {

namespace {

void pushU16BE(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}

void pushU32BE(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>(x & 0xFF));
}

void writeChunk(std::vector<uint8_t>& out, const char id[4], const std::vector<uint8_t>& body) {
    out.insert(out.end(), id, id + 4);
    pushU32BE(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

/// Écrit un seul MidiEvent (status + data) dans `out`. Le delta-time doit
/// avoir été écrit par l'appelant juste avant.
void writeEventBody(std::vector<uint8_t>& out, const MidiEventData& data) {
    std::visit([&out](auto&& ev) {
        using T = std::decay_t<decltype(ev)>;

        if constexpr (std::is_same_v<T, NoteOffEvent>) {
            out.push_back(static_cast<uint8_t>(0x80 | (ev.channel & 0x0F)));
            out.push_back(ev.note); out.push_back(ev.velocity);
        } else if constexpr (std::is_same_v<T, NoteOnEvent>) {
            out.push_back(static_cast<uint8_t>(0x90 | (ev.channel & 0x0F)));
            out.push_back(ev.note); out.push_back(ev.velocity);
        } else if constexpr (std::is_same_v<T, PolyPressureEvent>) {
            out.push_back(static_cast<uint8_t>(0xA0 | (ev.channel & 0x0F)));
            out.push_back(ev.note); out.push_back(ev.pressure);
        } else if constexpr (std::is_same_v<T, ControlChangeEvent>) {
            out.push_back(static_cast<uint8_t>(0xB0 | (ev.channel & 0x0F)));
            out.push_back(ev.controller); out.push_back(ev.value);
        } else if constexpr (std::is_same_v<T, ProgramChangeEvent>) {
            out.push_back(static_cast<uint8_t>(0xC0 | (ev.channel & 0x0F)));
            out.push_back(ev.program);
        } else if constexpr (std::is_same_v<T, ChannelPressureEvent>) {
            out.push_back(static_cast<uint8_t>(0xD0 | (ev.channel & 0x0F)));
            out.push_back(ev.pressure);
        } else if constexpr (std::is_same_v<T, PitchBendEvent>) {
            int raw = static_cast<int>(ev.value) + 8192;
            out.push_back(static_cast<uint8_t>(0xE0 | (ev.channel & 0x0F)));
            out.push_back(static_cast<uint8_t>(raw & 0x7F));
            out.push_back(static_cast<uint8_t>((raw >> 7) & 0x7F));
        } else if constexpr (std::is_same_v<T, TempoEvent>) {
            out.push_back(0xFF); out.push_back(0x51); out.push_back(0x03);
            out.push_back(static_cast<uint8_t>((ev.microsecondsPerQuarterNote >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((ev.microsecondsPerQuarterNote >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(ev.microsecondsPerQuarterNote & 0xFF));
        } else if constexpr (std::is_same_v<T, TimeSignatureEvent>) {
            out.push_back(0xFF); out.push_back(0x58); out.push_back(0x04);
            out.push_back(ev.numerator); out.push_back(ev.denominatorPow2);
            out.push_back(ev.clocksPerClick); out.push_back(ev.notated32ndsPerQuarter);
        } else if constexpr (std::is_same_v<T, TrackNameEvent>) {
            // D386 : en Latin-1, par la table de la chaîne (TextEncoding.h).
            const std::string nom = utf8VersTexteMidi(ev.name);
            out.push_back(0xFF); out.push_back(0x03);
            writeVLQ(out, static_cast<uint32_t>(nom.size()));
            out.insert(out.end(), nom.begin(), nom.end());
        } else if constexpr (std::is_same_v<T, EndOfTrackEvent>) {
            out.push_back(0xFF); out.push_back(0x2F); out.push_back(0x00);
        } else if constexpr (std::is_same_v<T, TextMetaEvent>) {
            const std::string texte = utf8VersTexteMidi(ev.text);   // D386
            out.push_back(0xFF); out.push_back(ev.metaType);
            writeVLQ(out, static_cast<uint32_t>(texte.size()));
            out.insert(out.end(), texte.begin(), texte.end());
        } else if constexpr (std::is_same_v<T, SysExEvent>) {
            out.push_back(0xF0);
            writeVLQ(out, static_cast<uint32_t>(ev.data.size()));
            out.insert(out.end(), ev.data.begin(), ev.data.end());
        } else if constexpr (std::is_same_v<T, UnknownMetaEvent>) {
            out.push_back(0xFF); out.push_back(ev.metaType);
            writeVLQ(out, static_cast<uint32_t>(ev.data.size()));
            out.insert(out.end(), ev.data.begin(), ev.data.end());
        }
    }, data);
}

bool isEndOfTrack(const MidiEventData& data) {
    return std::holds_alternative<EndOfTrackEvent>(data);
}

/// D623.1 — À TICK ÉGAL, LA NOTE QUI FINIT SE FERME AVANT QUE LA SUIVANTE S'OUVRE. Deux frappes de même hauteur bout à
/// bout s'écrivaient « on, puis off » au tick de jonction quand la seconde précédait la première dans la piste (une note
/// créée s'ajoute à la fin) : un lecteur extérieur ouvrait la seconde et la refermait aussitôt. Par APPARIEMENT, pas par
/// type : seuls avancent les note-off qui ferment une note ouverte à un tick ANTÉRIEUR (une note de longueur nulle garde
/// son off après son on, sinon elle pendrait) ; tout le reste garde son ordre — sans jonction, les octets d'avant.
void fermerAvantDOuvrir(std::vector<MidiEvent>& events) {
    const auto cle = [](const MidiEventData& d, bool& estOff) -> int {
        estOff = false;
        if (const auto* off = std::get_if<NoteOffEvent>(&d)) { estOff = true; return (off->channel & 0x0F) * 128 + (off->note & 0x7F); }
        if (const auto* on = std::get_if<NoteOnEvent>(&d)) {
            estOff = on->velocity == 0;   // la norme : un note-on de vélocité nulle est un note-off
            return (on->channel & 0x0F) * 128 + (on->note & 0x7F);
        }
        return -1;
    };
    std::array<int, 16 * 128> ouvertes{};   // les notes ouvertes AVANT le tick du groupe, par canal et hauteur
    size_t debut = 0;
    while (debut < events.size()) {
        size_t fin = debut;
        while (fin < events.size() && events[fin].tick == events[debut].tick) ++fin;
        std::array<int, 16 * 128> disponibles = ouvertes;
        std::vector<MidiEvent> tete, reste;
        for (size_t i = debut; i < fin; ++i) {
            bool estOff = false;
            const int k = cle(events[i].data, estOff);
            if (estOff && k >= 0 && disponibles[static_cast<size_t>(k)] > 0) {
                --disponibles[static_cast<size_t>(k)];
                tete.push_back(events[i]);
            } else {
                reste.push_back(events[i]);
            }
        }
        if (!tete.empty()) {
            size_t i = debut;
            for (auto& e : tete) events[i++] = std::move(e);
            for (auto& e : reste) events[i++] = std::move(e);
        }
        for (size_t i = debut; i < fin; ++i) {   // le compte, dans l'ordre désormais écrit
            bool estOff = false;
            const int k = cle(events[i].data, estOff);
            if (k < 0) continue;
            auto& n = ouvertes[static_cast<size_t>(k)];
            if (!estOff) ++n;
            else if (n > 0) --n;
        }
        debut = fin;
    }
}

std::vector<uint8_t> serializeTrack(const ParsedTrack& track) {
    // Tri stable par tick : préserve l'ordre relatif des événements à
    // égalité de tick (important pour CC/PC juste avant une note, etc.)
    std::vector<MidiEvent> events = track.events;
    std::stable_sort(events.begin(), events.end(),
                      [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    fermerAvantDOuvrir(events);   // D623.1

    bool hasEndOfTrack = !events.empty() && isEndOfTrack(events.back().data);
    Tick lastTick = events.empty() ? 0 : events.back().tick;

    std::vector<uint8_t> body;
    Tick prevTick = 0;
    for (const auto& ev : events) {
        writeVLQ(body, static_cast<uint32_t>(ev.tick - prevTick));
        prevTick = ev.tick;
        writeEventBody(body, ev.data);
    }
    if (!hasEndOfTrack) {
        writeVLQ(body, static_cast<uint32_t>(lastTick >= prevTick ? lastTick - prevTick : 0));
        writeEventBody(body, EndOfTrackEvent{});
    }
    return body;
}

} // namespace

std::vector<uint8_t> MidiFileWriter::write(const ParsedFile& file) {
    if (file.isSmpteTiming)
        throw std::runtime_error("MidiFileWriter: export SMPTE non encore supporté (Phase 1 = timing métrique)");

    std::vector<uint8_t> out;

    std::vector<uint8_t> header;
    pushU16BE(header, static_cast<uint16_t>(file.format));
    pushU16BE(header, static_cast<uint16_t>(file.tracks.size()));
    pushU16BE(header, file.ticksPerQuarterNote);
    writeChunk(out, "MThd", header);

    for (const auto& track : file.tracks)
        writeChunk(out, "MTrk", serializeTrack(track));

    return out;
}

void MidiFileWriter::writeFile(const ParsedFile& file, const std::string& path) {
    std::vector<uint8_t> bytes = write(file);
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("MidiFileWriter: impossible d'écrire: " + path);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace vsm::midi
