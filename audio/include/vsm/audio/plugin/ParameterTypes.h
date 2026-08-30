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

/// Tout ce qui n'est PAS une note : pitch bend, contrôleurs continus,
/// pression, changement de programme.
///
/// POURQUOI UN TYPE SÉPARÉ PLUTÔT QUE DEUX VALEURS DE PLUS DANS
/// `MidiNoteEvent::Kind`. Vingt-deux des trente-quatre machines dispatchent
/// leurs événements ainsi :
///
///     if (kind == Kind::NoteOn && velocity > 0) { ...déclencher... }
///     else                                      { ...relâcher...   }
///
/// Élargir l'énumération ferait donc RELÂCHER UNE NOTE à chaque contrôleur
/// reçu, dans vingt-deux machines à la fois, et il aurait fallu les relire
/// toutes pour s'en assurer. Un second flux, ignoré par défaut, ne peut pas
/// être mal interprété : une machine qui ne l'implémente pas se comporte
/// exactement comme avant, au bit près.
///
/// L'UNITÉ EST CELLE DU MUSICIEN, pas celle du câble. `value` porte des
/// DEMI-TONS pour le pitch bend et une fraction de 0 à 1 pour le reste : une
/// machine n'a pas à connaître les 14 bits signés du MIDI pour transposer.
struct MidiControlEvent {
    enum class Kind : uint8_t { PitchBend, ControlChange, ChannelPressure, PolyPressure, ProgramChange };
    Kind kind = Kind::ControlChange;
    int sampleOffset = 0;
    uint8_t channel = 0;
    /// Numéro de contrôleur (ControlChange), numéro de note (PolyPressure),
    /// numéro de programme (ProgramChange). Inutilisé ailleurs.
    uint8_t index = 0;
    /// Demi-tons pour PitchBend ; 0..1 pour les contrôleurs et les pressions.
    float value = 0.0f;
};

/// L'ÉTAT DU TRANSPORT tel qu'un plugin tiers a besoin de le connaître (D7.4).
///
/// POURQUOI CE TYPE EXISTE. Les trente-quatre machines du parc n'en ont aucun
/// besoin : le graphe leur livre des notes déjà horodatées, et un séquenceur de
/// machine tourne sur son propre compteur. Un plugin qu'on n'a pas écrit, lui,
/// ne peut pas deviner le tempo -- un delay synchronisé, un arpégiateur, un LFO
/// à la noire réglés « 1/4 » resteraient sur leur valeur d'usine, et rien dans
/// le son ne dirait qu'ils n'ont jamais rien su du morceau.
///
/// LA POSITION EST DONNÉE DEUX FOIS, en secondes ET en noires. Les deux sont
/// exactes et aucune ne se déduit de l'autre sans la carte des tempos : un
/// morceau qui accélère fait diverger « à la troisième seconde » et « au
/// troisième temps », et un plugin demande tantôt l'une, tantôt l'autre.
struct TransportInfo {
    bool playing = false;
    double tempoBpm = 120.0;
    double positionSeconds = 0.0;
    double positionBeats = 0.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
    bool looping = false;
    double loopStartBeats = 0.0;
    double loopEndBeats = 0.0;
};

} // namespace vsm::audio::plugin
