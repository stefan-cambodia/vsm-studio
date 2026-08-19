#pragma once
#include "vsm/midi/MidiEvent.h"
#include <cstdint>
#include <string>
#include <vector>

namespace vsm::midi {

enum class SmfFormat : uint8_t { Type0 = 0, Type1 = 1, Type2 = 2 };

/// Une piste brute telle que lue dans le fichier : proche 1:1 du chunk MTrk,
/// évènements en ticks absolus (delta-times déjà résolus).
struct ParsedTrack {
    std::string name; // extrait automatiquement d'un éventuel TrackNameEvent
    std::vector<MidiEvent> events;
};

/// Résultat complet du parsing d'un Standard MIDI File.
struct ParsedFile {
    SmfFormat format = SmfFormat::Type1;

    // --- Timing métrique (cas standard) ---
    uint16_t ticksPerQuarterNote = 480;

    // --- Timing SMPTE (division avec bit de poids fort à 1) ---
    bool isSmpteTiming = false;
    int8_t smpteFramesPerSecond = 0; // -24, -25, -29 (30 drop-frame) ou -30
    uint8_t smpteTicksPerFrame = 0;

    std::vector<ParsedTrack> tracks;
};

/// Parse un Standard MIDI File (Type 0 ou 1 ; Type 2 accepté en lecture mais
/// non prioritaire) depuis un buffer d'octets déjà chargé en mémoire.
///
/// Déterministe et strict : toute anomalie structurelle (chunk manquant,
/// longueur incohérente, VLQ malformée, statut MIDI invalide) lève une
/// std::runtime_error explicite plutôt que de produire un résultat
/// partiellement correct en silence.
class MidiFileParser {
public:
    static ParsedFile parse(const std::vector<uint8_t>& bytes);
    static ParsedFile parseFile(const std::string& path);
};

} // namespace vsm::midi
