#pragma once
#include "vsm/audio/plugin/ParameterTypes.h"
#include <memory>

namespace vsm::audio::effect {

/// Contrat commun à tous les effets d'insert (section 16). Volontairement
/// calqué sur ISynthPlugin : mêmes `ParameterList`/`ParamId` que les synthés,
/// pour que synthés ET effets soient décrits par UN SEUL modèle de paramètres
/// -- directement "ParameterDescriptor-ready" pour la couche d'interopérabilité
/// de la Phase 7 (addon), sans en dépendre aujourd'hui.
///
/// Traitement STÉRÉO EN PLACE (`left`/`right` séparés, comme le bus master et
/// le ProcessGraph). Contraintes temps réel identiques au reste du moteur :
/// prepare()/reset() côté thread UI peuvent allouer ; process() n'alloue
/// jamais et ne verrouille jamais ; les paramètres transitent par std::atomic.
class IAudioEffect {
public:
    virtual ~IAudioEffect() = default;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;

    /// Traite un bloc stéréo en place. numSamples <= maxBlockSize de prepare().
    virtual void process(float* left, float* right, int numSamples) = 0;

    virtual void setParameter(vsm::audio::plugin::ParamId id, float value) = 0;
    virtual float getParameter(vsm::audio::plugin::ParamId id) const = 0;
    virtual const vsm::audio::plugin::ParameterList& parameterList() const = 0;

    virtual const char* effectName() const = 0;
};

using AudioEffectPtr = std::unique_ptr<IAudioEffect>;

} // namespace vsm::audio::effect
