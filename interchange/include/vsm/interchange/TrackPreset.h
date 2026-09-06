#pragma once
#include "vsm/interchange/EffectPreset.h"
#include "vsm/interchange/Json.h"
#include "vsm/interchange/SynthPreset.h"
#include "vsm/sequencer/Track.h"
#include <optional>
#include <string>
#include <vector>

namespace vsm::interchange {

/// D22.5 -- LE PRESET DE PISTE. Un preset d'effet sauve UN insert, un preset
/// de synthé sauve UNE machine ; celui-ci sauve ce qui fait une piste hors
/// son contenu : la machine et son état (un `SynthPreset` emboîté), les
/// inserts (des `EffectPreset` emboîtés), les départs, le volume, le
/// panoramique, la couleur, le canal, la transposition, le décalage.
///
/// CE QU'IL NE SAUVE PAS, à dessein : les notes, les clips, les prises et
/// l'automation -- un preset est un réglage qu'on pose sur un contenu, pas
/// un contenu. Ni le solo, le muet, l'armement, le gel, le verrou : des
/// états de session, pas des réglages.
struct TrackPreset {
    std::string name = "Sans titre";
    std::string kind = "midi";                    ///< "midi" ou "audio"
    std::string instrumentId;                     ///< vide pour une piste audio
    std::optional<SynthPreset> synth;             ///< l'état de la machine, si elle a pu être lue
    std::vector<EffectPreset> effects;            ///< les inserts, dans l'ordre, chacun avec son contournement
    std::vector<bool> effectsEnabled;             ///< parallèle à `effects`
    std::vector<float> sendLevels;
    float volume = 1.0f;
    float pan = 0.0f;
    uint32_t colorRgba = 0xFF6B9BFFu;
    int channel = 0;
    int transposeSemitones = 0;
    double delayMs = 0.0;
    int outputGroup = -1;
};

inline constexpr const char* kTrackPresetFormat = "vsm-track-preset";
inline constexpr int kTrackPresetVersion = 1;
inline constexpr const char* kTrackPresetExtension = ".track.json";

/// Capture les réglages d'une piste (le `SynthPreset` est fourni par
/// l'appelant, qui seul a accès à la machine vivante ; absent pour une
/// piste audio ou une machine indisponible).
TrackPreset trackPresetFromTrack(const vsm::sequencer::Track& track, const std::string& name,
                                 std::optional<SynthPreset> synth);

/// Applique les réglages à une piste EXISTANTE : les notes, clips, prises,
/// automation et états de session sont conservés, tout le reste est
/// remplacé. Le `SynthPreset` n'est pas appliqué ici (il faut la machine,
/// voir `applyPreset`) ; l'appelant le fait après avoir instancié la machine.
void applyTrackPresetToTrack(const TrackPreset& preset, vsm::sequencer::Track& track);

JsonValue trackPresetToJson(const TrackPreset& preset);

struct TrackPresetLoadResult {
    bool success = false;
    TrackPreset preset;
    std::string error;
};
TrackPresetLoadResult trackPresetFromJson(const JsonValue& json);
TrackPresetLoadResult parseTrackPreset(const std::string& jsonText);

bool isTrackPresetFile(const std::string& path);

} // namespace vsm::interchange
