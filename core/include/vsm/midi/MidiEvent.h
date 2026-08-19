#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace vsm::midi {

/// Position temporelle en ticks absolus depuis le début de la piste
/// (résolution définie par `ticksPerQuarterNote` du fichier).
using Tick = int64_t;

// ---------------------------------------------------------------------------
// Événements "channel voice" (un par type de message MIDI standard)
// ---------------------------------------------------------------------------

struct NoteOnEvent          { uint8_t channel; uint8_t note; uint8_t velocity; };
struct NoteOffEvent         { uint8_t channel; uint8_t note; uint8_t velocity; };
struct PolyPressureEvent    { uint8_t channel; uint8_t note; uint8_t pressure; };
struct ControlChangeEvent   { uint8_t channel; uint8_t controller; uint8_t value; };
struct ProgramChangeEvent   { uint8_t channel; uint8_t program; };
struct ChannelPressureEvent { uint8_t channel; uint8_t pressure; };

/// Pitch bend normalisé : 0 = centre, plage [-8192, 8191] (14 bits signés).
struct PitchBendEvent       { uint8_t channel; int16_t value; };

// ---------------------------------------------------------------------------
// Méta-événements (non sonores, mais essentiels : tempo, structure...)
// ---------------------------------------------------------------------------

struct TempoEvent          { uint32_t microsecondsPerQuarterNote; };
struct TimeSignatureEvent  { uint8_t numerator; uint8_t denominatorPow2; uint8_t clocksPerClick; uint8_t notated32ndsPerQuarter; };
struct TrackNameEvent      { std::string name; };
struct EndOfTrackEvent     {};

/// Regroupe Text/Copyright/Lyric/Marker/CuePoint/InstrumentName (0x01-0x07,
/// hors TrackName géré séparément) : le contenu métier importe peu au moteur,
/// seule la préservation pour un export fidèle compte.
struct TextMetaEvent       { uint8_t metaType; std::string text; };

struct SysExEvent          { std::vector<uint8_t> data; };

/// Tout méta-événement non modélisé explicitement (KeySignature, SMPTE
/// Offset, SequencerSpecific...) : conservé tel quel pour ne rien perdre à
/// l'export, même si le moteur ne l'interprète pas encore.
struct UnknownMetaEvent    { uint8_t metaType; std::vector<uint8_t> data; };

using MidiEventData = std::variant<
    NoteOnEvent, NoteOffEvent, PolyPressureEvent, ControlChangeEvent,
    ProgramChangeEvent, ChannelPressureEvent, PitchBendEvent,
    TempoEvent, TimeSignatureEvent, TrackNameEvent, EndOfTrackEvent,
    TextMetaEvent, SysExEvent, UnknownMetaEvent
>;

/// Un événement MIDI horodaté en ticks absolus (les delta-times du fichier
/// brut sont résolus dès le parsing : le reste du moteur ne manipule que des
/// positions absolues, plus simples et plus sûres).
struct MidiEvent {
    Tick tick = 0;
    MidiEventData data;
};

} // namespace vsm::midi
