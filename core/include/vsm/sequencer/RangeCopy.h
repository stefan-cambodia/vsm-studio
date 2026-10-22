#pragma once
#include "vsm/midi/MidiFileParser.h"
#include "vsm/sequencer/Project.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// D535.3 de docs/ROADMAP-daw.md — COPIER UNE PLAGE SUR TOUTES LES PISTES, ET LA COLLER EN
// INSÉRANT (la « copie globale » et le *Paste Time* de Cubase).
//
// La copie garde ce que chaque piste FAIT ENTENDRE dans [de, à) — lu par les fenêtres de ses
// clips, muets compris (la leçon de D537) —, ramené au début de la plage. Le collage ouvre le
// temps à la tête (`insertTime`) et y pose la copie : un clip MIDI collé est une fenêtre
// IDENTITÉ sur ses notes, un clip audio garde sa fenêtre en secondes. Ni le tempo, ni les
// signatures, ni les repères : ils décrivent la ligne de temps, pas ce qu'on répète.

namespace vsm::sequencer {

/// Ce qu'une piste fait entendre dans la plage, en ticks RELATIFS à son début.
struct RangeTrackContent {
    uint64_t trackUid = 0;        ///< l'identité de SESSION de la piste (`Track::uid`)
    std::vector<Note> notes;
    std::vector<CcPoint> controlChanges;
    std::vector<PitchBendPoint> pitchBends;
    std::vector<PolyAftertouchPoint> polyAftertouch;
    std::vector<ChannelPressurePoint> channelPressure;
    std::vector<ProgramChangePoint> programChanges;
    std::vector<Clip> clips;      ///< les morceaux de clips coupés aux bornes, début relatif
    std::vector<AutomationCurve> automation;   ///< ouvertes par leur valeur au début de la plage
};

struct RangeClipboard {
    Tick length = 0;
    std::vector<RangeTrackContent> tracks;
    std::vector<ChordEvent> chords;   ///< relatifs ; l'accord en vigueur au début ouvre la plage
    bool empty() const { return length <= 0; }
};

/// COPIER [from, to) sur toutes les pistes. Les pistes doivent avoir leur `uid` de session
/// (`Project::assignTrackUids`) : c'est par lui que le collage les retrouve.
RangeClipboard copyRange(const Project& project, Tick from, Tick to,
                         const std::function<double(Tick)>& ticksToSeconds);

/// D553.3 — FUSIONNER LE MIDI ENTRE LES LOCATEURS (*Merge MIDI in Loop* de Cubase) : les notes que les pistes MIDI font
/// ENTENDRE dans [from, to) -- par les fenêtres de leurs clips NON muets (`clipPassages`), coupées aux bornes et à
/// leur clip --, à leur place sur la ligne de temps, la transposition de PISTE appliquée (la globale ne l'est pas :
/// elle s'appliquera à la piste qui les reçoit comme aux autres). Une piste muette, ou routée vers un groupe muet, ou
/// rendue muette par son VCA, ou tue par le solo d'une autre (sauf `soloSafe`) n'est pas entendue. Une note poussée
/// hors de 0-127 par la transposition est comptée et laissée. Chaque note retenue est numérotée par `nextNoteId`
/// (le compteur du projet qui la recevra : `Project::nextNoteId`), là où elle naît -- la règle de D262.
struct MidiMerge {
    std::vector<Note> notes;
    size_t tracksHeard = 0;
    size_t outOfRange = 0;
};
MidiMerge mergeHeardMidi(const Project& project, Tick from, Tick to, const std::function<uint64_t()>& nextNoteId);

struct RangePasteReport {
    size_t tracksPasted = 0;    ///< pistes retrouvées et collées
    size_t tracksMissing = 0;   ///< pistes de la copie supprimées depuis : laissées
    size_t notes = 0;
    size_t clips = 0;
};

/// COLLER EN INSÉRANT à `at` : le temps s'ouvre de la longueur de la plage, puis la copie s'y
/// pose. Une plage vide ne fait rien (et rend un bilan nul).
RangePasteReport pasteRangeInserting(Project& project, const RangeClipboard& clipboard, Tick at,
                                     const std::function<double(Tick)>& ticksToSeconds);

/// D568.1 — DUPLIQUER LE TEMPS (*Duplicate Time* de Live) : [from, to) copiée sur toutes les pistes et collée EN
/// INSÉRANT à `to` — ce qui suivait recule de `to - from`. Le presse-papiers de l'application n'est pas touché, et
/// les locateurs ne le sont pas ici : c'est à l'appelant de les poser sur la copie.
RangePasteReport duplicateRange(Project& project, Tick from, Tick to, const std::function<double(Tick)>& ticksToSeconds);

/// D568.2 — LE MIDI ENTRE LES LOCATEURS (*Export ▸ MIDI Loop* de Cubase) : `file` — l'arrangement que l'export écrit
/// (`toParsedFileArranged`) — coupé à [from, to) et ramené au tick 0. Les notes qui COMMENCENT dans la plage, coupées à
/// `to` ; une note commencée avant `from` n'y entre pas. Les contrôleurs, le pitch bend, la pression, les programmes, le
/// tempo et la signature : ceux de la plage, et la valeur EN VIGUEUR à `from` posée au tick 0 (la chasse de D331).
/// Les noms de piste restent ; la fin de piste est à `to - from`. Une plage vide rend des pistes sans événement.
midi::ParsedFile cropParsedFile(const midi::ParsedFile& file, Tick from, Tick to);

} // namespace vsm::sequencer
