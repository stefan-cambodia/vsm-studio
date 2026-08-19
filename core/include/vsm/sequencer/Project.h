#pragma once
#include "vsm/midi/MidiFileParser.h"
#include "vsm/sequencer/TempoMap.h"
#include "vsm/sequencer/TimeSignatureMap.h"
#include "vsm/sequencer/Track.h"
#include <cstdint>
#include <string>
#include <vector>

namespace vsm::sequencer {

/// Modèle "métier" du morceau : c'est CE que le piano roll, le mixer et
/// l'automation manipulent — jamais les octets bruts du fichier MIDI.
///
/// Project::fromParsedFile() convertit une lecture brute (ParsedFile) en ce
/// modèle éditable (appariement Note On/Off, extraction du tempo/de la
/// signature rythmique...). Project::toParsedFile() fait le chemin inverse
/// pour l'export. Les deux fonctions sont testées en aller-retour (voir
/// tests/test_midi_writer.cpp) pour garantir qu'aucune information musicale
/// n'est perdue.
class Project {
public:
    std::string title = "Sans titre";
    midi::SmfFormat exportFormat = midi::SmfFormat::Type1;
    uint16_t ticksPerQuarterNote = 480;

    TempoMap tempoMap;
    TimeSignatureMap timeSignatureMap;
    std::vector<Track> tracks;

    static Project fromParsedFile(const midi::ParsedFile& parsed);
    midi::ParsedFile toParsedFile() const;

    double ticksToSeconds(midi::Tick tick) const { return tempoMap.ticksToSeconds(tick, ticksPerQuarterNote); }
    midi::Tick secondsToTicks(double seconds) const { return tempoMap.secondsToTicks(seconds, ticksPerQuarterNote); }

    /// Dernier tick utilisé par une note ou un événement de contrôle, tous
    /// pistes confondues (0 si le projet est vide). Utile pour la longueur
    /// d'affichage par défaut et les bornes d'export.
    midi::Tick lastUsedTick() const;

    uint64_t nextNoteId() { return nextNoteId_++; }

    /// Prochain identifiant SANS le consommer. Utile avec les opérations de
    /// NoteEdit.h, qui prennent un compteur par référence et l'incrémentent
    /// elles-mêmes : on leur passe `peekNextNoteId() - 1`, puis on recale le
    /// projet avec ensureNoteIdAbove(). Sans ce couple, l'appelant devrait
    /// deviner combien de notes l'opération va créer -- et une erreur de
    /// comptage donnerait deux notes de MÊME identifiant, ce qui casserait
    /// silencieusement la sélection et l'annulation.
    uint64_t peekNextNoteId() const { return nextNoteId_; }
    void ensureNoteIdAbove(uint64_t usedId) { if (nextNoteId_ <= usedId) nextNoteId_ = usedId + 1; }

private:
    uint64_t nextNoteId_ = 1;
};

} // namespace vsm::sequencer
