#include "vsm/midi/MidiFileParser.h"
#include "vsm/midi/VariableLengthQuantity.h"

#include <fstream>
#include <stdexcept>

namespace vsm::midi {

namespace {

uint16_t readU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

struct ChunkView {
    char id[5] = {0};
    const uint8_t* data = nullptr;
    uint32_t length = 0;
};

/// Lit un chunk RIFF-like (4 octets d'id + longueur big-endian + données) et
/// avance `cursor` jusqu'à la fin du chunk.
ChunkView readChunk(const uint8_t*& cursor, const uint8_t* end) {
    if (end - cursor < 8)
        throw std::runtime_error("MIDI: fichier tronqué (en-tête de chunk incomplet)");
    ChunkView chunk;
    for (int i = 0; i < 4; ++i) chunk.id[i] = static_cast<char>(cursor[i]);
    chunk.length = readU32BE(cursor + 4);
    cursor += 8;
    if (static_cast<uint32_t>(end - cursor) < chunk.length)
        throw std::runtime_error(std::string("MIDI: chunk '") + chunk.id + "' plus long que le fichier");
    chunk.data = cursor;
    cursor += chunk.length;
    return chunk;
}

/// Parse le contenu d'un chunk MTrk en une liste d'événements horodatés en
/// ticks absolus, en gérant le running status conformément à la spec MIDI.
std::vector<MidiEvent> parseTrackEvents(const uint8_t* data, uint32_t length) {
    std::vector<MidiEvent> events;
    const uint8_t* p = data;
    const uint8_t* end = data + length;

    Tick absoluteTick = 0;
    uint8_t runningStatus = 0;

    while (p < end) {
        absoluteTick += readVLQ(p, end);

        if (p >= end)
            throw std::runtime_error("MIDI: événement tronqué (statut manquant)");

        uint8_t statusByte = *p;
        if (statusByte & 0x80) {
            // Nouvel octet de statut explicite.
            ++p;
            if (statusByte != 0xF0 && statusByte != 0xFF && statusByte != 0xF7)
                runningStatus = statusByte;
        } else {
            // Running status : réutilise le dernier statut de canal, cet
            // octet est la première donnée du message.
            if (runningStatus == 0)
                throw std::runtime_error("MIDI: running status utilisé sans statut précédent");
            statusByte = runningStatus;
        }

        const uint8_t highNibble = statusByte & 0xF0;
        const uint8_t channel = statusByte & 0x0F;

        auto readByte = [&]() -> uint8_t {
            if (p >= end) throw std::runtime_error("MIDI: donnée manquante en fin de piste");
            return *p++;
        };

        if (statusByte == 0xFF) {
            // Méta-événement : FF <type> <VLQ length> <data...>
            uint8_t metaType = readByte();
            uint32_t len = readVLQ(p, end);
            if (static_cast<uint32_t>(end - p) < len)
                throw std::runtime_error("MIDI: méta-événement tronqué");
            const uint8_t* metaData = p;
            p += len;

            switch (metaType) {
                case 0x51: { // Set Tempo (3 octets, 24 bits big-endian)
                    if (len != 3) throw std::runtime_error("MIDI: Set Tempo de longueur invalide");
                    uint32_t usPerQn = (static_cast<uint32_t>(metaData[0]) << 16) |
                                       (static_cast<uint32_t>(metaData[1]) << 8) |
                                       static_cast<uint32_t>(metaData[2]);
                    events.push_back({absoluteTick, TempoEvent{usPerQn}});
                    break;
                }
                case 0x58: { // Time Signature (4 octets)
                    if (len != 4) throw std::runtime_error("MIDI: Time Signature de longueur invalide");
                    events.push_back({absoluteTick, TimeSignatureEvent{
                        metaData[0], metaData[1], metaData[2], metaData[3]}});
                    break;
                }
                case 0x03: { // Track Name
                    events.push_back({absoluteTick, TrackNameEvent{
                        std::string(reinterpret_cast<const char*>(metaData), len)}});
                    break;
                }
                case 0x2F: { // End of Track
                    events.push_back({absoluteTick, EndOfTrackEvent{}});
                    break;
                }
                case 0x01: case 0x02: case 0x04: case 0x05: case 0x06: case 0x07: {
                    events.push_back({absoluteTick, TextMetaEvent{
                        metaType, std::string(reinterpret_cast<const char*>(metaData), len)}});
                    break;
                }
                default: {
                    events.push_back({absoluteTick, UnknownMetaEvent{
                        metaType, std::vector<uint8_t>(metaData, metaData + len)}});
                    break;
                }
            }
        } else if (statusByte == 0xF0 || statusByte == 0xF7) {
            // SysEx (ou continuation SysEx) : <VLQ length> <data...>
            uint32_t len = readVLQ(p, end);
            if (static_cast<uint32_t>(end - p) < len)
                throw std::runtime_error("MIDI: SysEx tronqué");
            events.push_back({absoluteTick, SysExEvent{std::vector<uint8_t>(p, p + len)}});
            p += len;
        } else {
            switch (highNibble) {
                case 0x80: { uint8_t n = readByte(), v = readByte(); events.push_back({absoluteTick, NoteOffEvent{channel, n, v}}); break; }
                case 0x90: { uint8_t n = readByte(), v = readByte(); events.push_back({absoluteTick, NoteOnEvent{channel, n, v}}); break; }
                case 0xA0: { uint8_t n = readByte(), v = readByte(); events.push_back({absoluteTick, PolyPressureEvent{channel, n, v}}); break; }
                case 0xB0: { uint8_t c = readByte(), v = readByte(); events.push_back({absoluteTick, ControlChangeEvent{channel, c, v}}); break; }
                case 0xC0: { uint8_t prog = readByte(); events.push_back({absoluteTick, ProgramChangeEvent{channel, prog}}); break; }
                case 0xD0: { uint8_t pr = readByte(); events.push_back({absoluteTick, ChannelPressureEvent{channel, pr}}); break; }
                case 0xE0: {
                    uint8_t lsb = readByte(), msb = readByte();
                    int value = ((static_cast<int>(msb) << 7) | lsb) - 8192;
                    events.push_back({absoluteTick, PitchBendEvent{channel, static_cast<int16_t>(value)}});
                    break;
                }
                default:
                    throw std::runtime_error("MIDI: octet de statut inconnu/non supporté");
            }
        }
    }
    return events;
}

} // namespace

ParsedFile MidiFileParser::parse(const std::vector<uint8_t>& bytes) {
    const uint8_t* cursor = bytes.data();
    const uint8_t* end = cursor + bytes.size();

    ChunkView header = readChunk(cursor, end);
    if (std::string(header.id) != "MThd")
        throw std::runtime_error("MIDI: chunk d'en-tête 'MThd' introuvable (fichier non-SMF ?)");
    if (header.length < 6)
        throw std::runtime_error("MIDI: en-tête MThd trop court");

    ParsedFile result;
    uint16_t formatValue = readU16BE(header.data);
    if (formatValue > 2)
        throw std::runtime_error("MIDI: format SMF inconnu (attendu 0, 1 ou 2)");
    result.format = static_cast<SmfFormat>(formatValue);

    uint16_t declaredTrackCount = readU16BE(header.data + 2);
    uint16_t division = readU16BE(header.data + 4);

    if (division & 0x8000) {
        result.isSmpteTiming = true;
        result.smpteFramesPerSecond = static_cast<int8_t>((division >> 8) & 0xFF);
        result.smpteTicksPerFrame = static_cast<uint8_t>(division & 0xFF);
    } else {
        result.ticksPerQuarterNote = division;
    }

    while (cursor < end) {
        ChunkView chunk = readChunk(cursor, end);
        if (std::string(chunk.id) != "MTrk")
            continue; // chunk inconnu (extension propriétaire) : ignoré proprement

        ParsedTrack track;
        track.events = parseTrackEvents(chunk.data, chunk.length);
        for (const auto& ev : track.events) {
            if (auto* nameEvt = std::get_if<TrackNameEvent>(&ev.data)) {
                track.name = nameEvt->name;
                break;
            }
        }
        result.tracks.push_back(std::move(track));
    }

    if (result.tracks.size() != declaredTrackCount) {
        // Non bloquant : certains exporteurs mentent sur ntrks. On fait
        // confiance au contenu réellement lu plutôt qu'à l'en-tête.
    }

    return result;
}

ParsedFile MidiFileParser::parseFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("MIDI: impossible d'ouvrir le fichier: " + path);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size))
        throw std::runtime_error("MIDI: erreur de lecture: " + path);
    return parse(bytes);
}

} // namespace vsm::midi
