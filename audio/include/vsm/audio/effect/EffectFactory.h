#pragma once
#include "IAudioEffect.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vsm::audio::effect {

/// Métadonnée d'un type d'effet disponible.
struct EffectInfo {
    std::string id;          // identifiant stable (ex. "reverb")
    std::string displayName; // nom affiché (ex. "Reverb")
};

/// Fabrique centrale des effets d'insert. L'UI liste `available()` pour
/// remplir son menu "ajouter un effet", puis `create(id)` pour instancier.
/// Pensée "ParameterDescriptor-ready" (addon Phase 7) : chaque effet expose
/// déjà la même `ParameterList` que les synthés, donc un adaptateur CLAP ou
/// un export sémantique se branchera dessus sans toucher au DSP.
/// Fabrique un effet d'après un identifiant qu'elle est seule à savoir lire.
using AudioEffectFactoryById = std::function<std::unique_ptr<IAudioEffect>(const std::string&)>;

class EffectFactory {
public:
    static const std::vector<EffectInfo>& available();
    static std::unique_ptr<IAudioEffect> create(const std::string& id);

    /// LES EFFETS QU'ON N'A PAS ÉCRITS (D7.3). Exactement le crochet que
    /// `PluginRegistry` a reçu pour les instruments (D7.1), et pour les mêmes
    /// raisons : un effet tiers vit dans un fichier que l'utilisateur désigne
    /// en cours de route, et `audio/` ne doit rien savoir de CLAP, de VST3 ni
    /// de JUCE. Ce sont les couches d'hébergement qui se posent ici.
    ///
    /// Appelé seulement quand l'identifiant demandé n'est pas un effet interne.
    /// Les résolveurs s'ENCHAÎNENT (chacun garde celui d'avant), si bien que
    /// l'ordre dans lequel CLAP et VST3 se posent n'a aucune importance.
    static void setExternalResolver(AudioEffectFactoryById resolver);
    static const AudioEffectFactoryById& externalResolver();
};

} // namespace vsm::audio::effect
