#pragma once
#include "vsm/interchange/Json.h"
#include "vsm/interchange/ParameterDescriptor.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <map>
#include <string>
#include <vector>

// Preset SÉMANTIQUE `*.synth.json` (Phase 7, P3 -- docs/ROADMAP-interop.md § 4).
//
// Ce que ce format N'EST PAS : une copie de l'état interne d'une machine. Ça
// existe déjà (`PresetState`, une table ParamId -> valeur) et ça ne
// s'interprète que si l'on connaît la machine ET sa version. Un fichier
// `.synth.json` décrit un son en termes que l'extérieur comprend --
// `filter.1.cutoff: 1200 Hz` -- ce qui le rend lisible par un humain,
// produisible par un script Python d'analyse, et applicable, avec des pertes
// explicites, à une machine qui n'est pas celle d'origine.
//
// VERSIONNÉ DÈS LA V1 : `format` et `version` sont vérifiés à la lecture. Un
// fichier d'un format inconnu est REFUSÉ avec un message clair plutôt que
// chargé au petit bonheur -- une valeur mal interprétée produirait un son faux
// sans prévenir, ce qui est le pire des échecs pour un outil de reconstruction.

namespace vsm::interchange {

inline constexpr const char* kSynthPresetFormat = "vsm-synth-preset";
inline constexpr int kSynthPresetVersion = 1;

struct SynthPreset {
    std::string name = "Sans titre";
    std::string pluginId;        ///< machine d'origine, ex. "vsm.minimoog"
    std::string machineName;     ///< nom lisible, ex. "Minimoog-style Monosynth"
    /// Valeurs en UNITÉS RÉELLES (Hz, secondes, demi-tons), pas en normalisé :
    /// c'est ce qui rend le fichier interprétable sans connaître la machine.
    std::map<std::string, float> values; ///< semanticId -> valeur
    Fidelity fidelity = Fidelity::Derived;

    /// ÉCHANTILLONS de la machine, par emplacement : `slot -> chemin`.
    ///
    /// POURQUOI CE CHAMP EXISTE À PART DES PARAMÈTRES : `setParameter` ne
    /// transporte que des flottants -- c'est ce qui rend l'automation, le MIDI
    /// Learn et l'interop CLAP uniformes pour toutes les machines. Un chemin
    /// de fichier n'entre pas dans ce moule (voir `ISampleLoader`), et l'y
    /// forcer créerait une indirection fragile pour toutes les machines afin
    /// de servir une seule famille.
    ///
    /// LES CHEMINS SONT RELATIFS au dossier de projet, toujours. Un chemin
    /// absolu rendrait le projet non transportable : il s'ouvrirait sur la
    /// machine qui l'a produit et nulle part ailleurs, ce qui est le contraire
    /// de ce que ce format sert à faire.
    ///
    /// Champ FACULTATIF : un preset qui n'en déclare pas reste valide et se lit
    /// à l'identique. C'est pourquoi son ajout ne change pas le numéro de
    /// version du format -- les fichiers existants restent lisibles, et les
    /// nouveaux le restent pour un lecteur ancien, qui ignorera simplement des
    /// échantillons dont il ne saurait rien faire.
    std::map<int, std::string> samples;

    float valueOr(const std::string& semanticId, float fallback) const;
};

/// Capture l'état courant d'une machine sous forme de preset sémantique.
SynthPreset capturePreset(const vsm::audio::plugin::ISynthPlugin& plugin,
                           const std::string& pluginId, std::string presetName);

/// Ce qui s'est réellement passé à l'application d'un preset -- jamais
/// silencieux : chaque paramètre non appliqué est nommé, avec sa raison.
struct PresetApplyReport {
    struct Entry {
        std::string semanticId;
        SupportStatus status = SupportStatus::Supported;
        float requestedValue = 0.0f;
        float appliedValue = 0.0f;   ///< après bornage aux limites de la machine
        std::string detail;          ///< explication quand ce n'est pas exact
    };
    std::vector<Entry> entries;

    size_t appliedCount() const;
    size_t unsupportedCount() const;
    size_t clampedCount() const;
    /// Résumé lisible destiné à l'utilisateur (dialogue d'import, journal).
    std::string summary() const;
};

/// Applique un preset sémantique à une machine, qui n'est pas forcément celle
/// d'origine : les paramètres que la cible ne connaît pas sont rapportés
/// `Unsupported`, ceux qui sortent de ses bornes sont bornés et rapportés
/// `Approximated`. Rien n'est jamais appliqué en douce.
PresetApplyReport applyPreset(const SynthPreset& preset, vsm::audio::plugin::ISynthPlugin& plugin,
                               const std::string& targetPluginId);

/// Ce qui s'est passé au chargement des échantillons d'un preset.
struct SampleLoadReport {
    /// Emplacements réellement chargés.
    std::vector<std::pair<int, std::string>> loaded;
    /// Échecs, avec leur raison. JAMAIS silencieux : un échantillon manquant
    /// produit une piste muette, et une piste muette sans message est le genre
    /// de panne qu'on cherche pendant une heure.
    std::vector<std::string> failures;

    bool empty() const { return loaded.empty() && failures.empty(); }
    std::string summary() const;
};

/// Charge les échantillons déclarés par un preset dans la machine.
///
/// `baseFolder` est le dossier du projet : les chemins du preset sont relatifs
/// à lui. Ne fait rien -- sans erreur -- si la machine n'accepte pas
/// d'échantillons ou si le preset n'en déclare aucun.
///
/// FAIT DES ENTRÉES/SORTIES FICHIER : à n'appeler ni depuis le thread audio,
/// ni pendant la lecture.
SampleLoadReport applyPresetSamples(const SynthPreset& preset,
                                     vsm::audio::plugin::ISynthPlugin& plugin,
                                     const std::string& baseFolder);

JsonValue synthPresetToJson(const SynthPreset& preset);

struct SynthPresetLoadResult {
    bool success = false;
    SynthPreset preset;
    std::string error;
};

SynthPresetLoadResult synthPresetFromJson(const JsonValue& json);
SynthPresetLoadResult parseSynthPreset(const std::string& jsonText);

} // namespace vsm::interchange
