#pragma once
#include "IAudioEffect.h"
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
class EffectFactory {
public:
    static const std::vector<EffectInfo>& available();
    static std::unique_ptr<IAudioEffect> create(const std::string& id);
};

} // namespace vsm::audio::effect
