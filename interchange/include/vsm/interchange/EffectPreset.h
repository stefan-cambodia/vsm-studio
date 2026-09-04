#pragma once
#include "vsm/interchange/Json.h"
#include "vsm/sequencer/Track.h"
#include <map>
#include <string>

namespace vsm::interchange {

/// UN PRESET D'EFFET (D15.4) : ce qu'un insert porte de réglable, avec un nom,
/// dans un fichier `*.effect.json`. Les machines avaient les leurs
/// (`SynthPreset`, `*.synth.json`) ; les seize effets n'en avaient aucun, et
/// une réverbération réglée se refaisait à chaque piste.
///
/// LE CONTENU EST LA DESCRIPTION DE L'INSERT, ni plus ni moins : le type de
/// fabrique, la valeur de chaque réglage EN UNITÉS RÉELLES sous son nom
/// sémantique (`reverb.1.mix`), et l'état natif d'un effet tiers quand il
/// existe. C'est exactement ce que `project.json` écrit pour un insert --
/// un preset est un insert sorti de son projet, et il y rentre tel quel.
/// L'état « contourné » n'en fait PAS partie : c'est une décision de mixage,
/// pas un réglage de l'effet.
struct EffectPreset {
    std::string name;
    std::string type;                            ///< identifiant `EffectFactory`
    std::map<std::string, float> parameters;     ///< nom sémantique -> valeur réelle
    std::string nativeState;                     ///< vide pour un effet interne
};

inline constexpr const char* kEffectPresetFormat = "vsm-effect-preset";
inline constexpr int kEffectPresetVersion = 1;
inline constexpr const char* kEffectPresetExtension = ".effect.json";

EffectPreset effectPresetFromDescription(const vsm::sequencer::TrackEffect& described,
                                         const std::string& name);
vsm::sequencer::TrackEffect descriptionFromEffectPreset(const EffectPreset& preset);

JsonValue effectPresetToJson(const EffectPreset& preset);

struct EffectPresetLoadResult {
    bool success = false;
    EffectPreset preset;
    std::string error;
};
EffectPresetLoadResult effectPresetFromJson(const JsonValue& json);
EffectPresetLoadResult parseEffectPreset(const std::string& jsonText);

/// `*.effect.json`, sans égard à la casse.
bool isEffectPresetFile(const std::string& path);

} // namespace vsm::interchange
