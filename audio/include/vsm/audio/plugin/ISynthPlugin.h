#pragma once
#include "ParameterTypes.h"
#include <memory>

namespace vsm::audio::plugin {

/// Interface commune à TOUS les instruments virtuels, du plus simple synthé
/// de test à la future modélisation complète d'un Minimoog. Ajouter une
/// machine ne doit jamais nécessiter de modifier AudioEngine, Track, le
/// Piano Roll ou le Mixer (section 22 du cahier des charges) -- tout passe
/// par cette interface, découverte via PluginRegistry.
///
/// RÈGLE ABSOLUE (section 13) : process() est appelé depuis le thread audio
/// temps réel. Aucune implémentation ne doit y allouer de mémoire, prendre
/// un lock, ou faire une quelconque I/O. setParameter()/getParameter()
/// peuvent être appelés depuis le thread UI ET le thread audio -- toute
/// implémentation doit les rendre thread-safe (typiquement via des
/// std::atomic<float> en interne, jamais un mutex).
class ISynthPlugin {
public:
    virtual ~ISynthPlugin() = default;

    virtual void initialize(double sampleRate, int maxBlockSize) = 0;

    /// Traite un bloc complet de `numSamples` échantillons : applique tous
    /// les `events` aux bons `sampleOffset`, synthétise dans outputL/outputR
    /// (déjà alloués par l'appelant, jamais par l'implémentation).
    virtual void process(const MidiNoteEvent* events, int numEvents,
                          float* outputL, float* outputR, int numSamples) = 0;

    /// Applique un événement de contrôle (pitch bend, molette, pression...).
    ///
    /// NON PURE, ET LE DÉFAUT EST « JE NE SAIS PAS FAIRE ». Une machine qui
    /// n'implémente rien se comporte exactement comme avant -- mais le moteur
    /// COMPTE ce qu'elle a refusé et le rapporte
    /// (`ProcessGraph::ignoredControlEvents()`). C'est la différence entre une
    /// machine qui ignore un contrôleur, ce qui est un choix légitime, et un
    /// moteur qui le jette, ce qui était le cas jusqu'ici : le projet portait
    /// des pitch bends, les écrivait dans le fichier, les exportait en SMF, et
    /// aucun n'atteignait jamais un instrument.
    ///
    /// Renvoie true si l'événement a été pris en compte.
    virtual bool handleControlEvent(const MidiControlEvent&) { return false; }

    virtual void setParameter(ParamId id, float value) = 0;
    virtual float getParameter(ParamId id) const = 0;
    virtual const ParameterList& parameterList() const = 0;

    virtual PresetState saveState() const = 0;
    virtual void loadState(const PresetState& state) = 0;

    virtual const char* machineName() const = 0;

    /// Nombre de voix actuellement actives -- utilisé pour l'affichage
    /// CPU/voix (section 23) et pour avertir l'utilisateur en cas de
    /// surcharge polyphonique.
    virtual int activeVoiceCount() const = 0;
    /// LE RETARD QUE CETTE MACHINE INTRODUIT, en échantillons (D4.5).
    ///
    /// Zéro par défaut, et c'est le cas des trente-quatre machines du parc :
    /// elles rendent l'échantillon qu'elles viennent de calculer. Le champ
    /// existe pour qu'une machine à traitement anticipé -- un vocodeur de
    /// phase, un limiteur à lookahead -- puisse le DIRE au lieu de décaler sa
    /// piste en silence.
    virtual int latencySamples() const { return 0; }

};

using SynthPluginPtr = std::shared_ptr<ISynthPlugin>;

} // namespace vsm::audio::plugin
