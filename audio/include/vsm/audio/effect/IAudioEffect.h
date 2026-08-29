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

    /// LE RETARD QUE CET EFFET INTRODUIT, en échantillons (D4.5).
    ///
    /// Zéro par défaut : la grande majorité des effets rend l'échantillon qu'on
    /// vient de lui donner. Ceux qui suréchantillonnent, eux, filtrent -- et un
    /// filtre à phase linéaire retarde. Tant que ce chiffre n'était déclaré
    /// nulle part, insérer une distorsion suréchantillonnée décalait la piste
    /// de seize échantillons SANS QUE RIEN NE LE DISE : le son restait juste,
    /// mais la piste n'était plus en place, et deux prises censées coïncider
    /// cessaient de coïncider selon les effets qu'on leur avait mis.
    ///
    /// À déclarer après `prepare()`, qui est ce qui fixe la valeur.
    virtual int latencySamples() const { return 0; }

    // --- CHAÎNE LATÉRALE (sidechain, D4.4) --------------------------------
    //
    // DEUX MÉTHODES FACULTATIVES plutôt qu'un `process` élargi : douze effets
    // sur treize n'écoutent rien d'autre que ce qu'ils traitent, et leur
    // imposer un paramètre de plus les obligerait tous à le documenter, le
    // tester et l'ignorer. Le défaut « je n'écoute rien » les laisse
    // rigoureusement inchangés.

    /// Le bus de départ que cet effet ÉCOUTE, ou 0 s'il n'écoute rien.
    ///
    /// Un bus de départ et non une piste : le signal d'écoute passe par le même
    /// chemin que n'importe quel envoi, avec son bouton sur chaque tranche.
    /// Router « la grosse caisse vers le bus 3 » et « ce compresseur écoute le
    /// bus 3 » emploie ce qui existe déjà, là où une référence de piste à piste
    /// aurait demandé un second système de routage à tenir d'accord avec le
    /// premier.
    virtual int sidechainBus() const { return 0; }

    /// Donne à l'effet le signal qu'il a demandé, juste avant `process`. Les
    /// pointeurs ne sont valides QUE pendant l'appel à `process` qui suit.
    virtual void setSidechainInput(const float* /*left*/, const float* /*right*/,
                                    int /*numSamples*/) {}
    /// CET EFFET EXIGE-T-IL D'ÊTRE RENDU EN TEMPS RÉEL (D6.5) ? Faux pour tous
    /// les effets internes, qui sont déterministes. Voir
    /// `ISynthPlugin::requiresRealtimeRender()` : même question, même raison,
    /// et c'est la phase D7 qui la rendra utile.
    virtual bool requiresRealtimeRender() const { return false; }

};

using AudioEffectPtr = std::unique_ptr<IAudioEffect>;

} // namespace vsm::audio::effect
