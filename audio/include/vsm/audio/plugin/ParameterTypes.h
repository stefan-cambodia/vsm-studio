#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vsm::audio::plugin {

using ParamId = uint32_t;

struct ParameterInfo {
    ParamId id = 0;
    std::string name;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    std::string unit; // "Hz", "dB", "%", "" ...
};

using ParameterList = std::vector<ParameterInfo>;

/// Sérialisation opaque et indépendante du plugin concret : une simple
/// table id -> valeur. Suffisant pour save/load d'état et pour le futur
/// Preset Manager (voir ARCHITECTURE.md section 10) sans que le moteur ait
/// besoin de connaître la structure interne de chaque synthé.
struct PresetState {
    std::string pluginTypeId; // ex: "vsm.testtone"
    std::unordered_map<ParamId, float> parameterValues;
};

/// Événement note, horodaté en position d'échantillon À L'INTÉRIEUR du bloc
/// traité par process() -- c'est ce qui rend le déclenchement des notes
/// "sample accurate" plutôt qu'aligné uniquement sur le début du bloc audio.
struct MidiNoteEvent {
    enum class Kind : uint8_t { NoteOn, NoteOff };
    Kind kind = Kind::NoteOn;
    int sampleOffset = 0;
    uint8_t channel = 0;
    uint8_t note = 60;
    uint8_t velocity = 100;
};

} // namespace vsm::audio::plugin
