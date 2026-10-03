#pragma once
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

} // namespace vsm::sequencer
