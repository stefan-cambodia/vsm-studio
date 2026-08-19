#pragma once
#include "vsm/midi/MidiFileParser.h" // réutilise ParsedFile/ParsedTrack/SmfFormat
#include <cstdint>
#include <string>
#include <vector>

namespace vsm::midi {

/// Sérialise un ParsedFile en octets SMF valides.
///
/// Symétrique de MidiFileParser::parse : write(parse(bytes)) redonne un
/// fichier fonctionnellement équivalent (mêmes événements musicaux ; l'ordre
/// exact des méta-événements à égalité de tick peut différer, voir tests).
class MidiFileWriter {
public:
    static std::vector<uint8_t> write(const ParsedFile& file);
    static void writeFile(const ParsedFile& file, const std::string& path);
};

} // namespace vsm::midi
