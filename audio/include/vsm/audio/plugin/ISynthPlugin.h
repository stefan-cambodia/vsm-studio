#pragma once
#include "ParameterTypes.h"
#include <memory>
#include <string>

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

    /// CETTE MACHINE EXIGE-T-ELLE D'ÊTRE RENDUE EN TEMPS RÉEL (D6.5) ?
    ///
    /// FAUX pour les trente-quatre machines du parc, et ce n'est pas une
    /// commodité : elles sont purement déterministes, un bloc calculé plus vite
    /// que le temps réel donne exactement les mêmes échantillons. Rendre un
    /// morceau de neuf minutes en dix secondes est alors une propriété, pas un
    /// raccourci.
    ///
    /// LE CHAMP EXISTE POUR LES PLUGINS DES AUTRES (phase D7). Certains lisent
    /// une horloge, un générateur d'aléa lié au temps de la machine, ou font
    /// tourner leur propre thread : les rendre plus vite que le temps réel leur
    /// fait produire autre chose que ce qu'on a entendu. Ils doivent pouvoir le
    /// DIRE, plutôt que de rendre faux en silence -- exactement la raison
    /// d'être de `latencySamples()` juste au-dessus.
    virtual bool requiresRealtimeRender() const { return false; }

    /// L'ÉTAT QUI NE TIENT PAS DANS UNE TABLE DE FLOTTANTS (D7.2).
    ///
    /// VIDE pour les trente-quatre machines du parc, et ce n'est pas un oubli :
    /// leur son EST leur table de paramètres, `saveState()` la rend en entier,
    /// et un preset sémantique la décrit dans des unités qu'un humain relit.
    /// C'est une propriété qu'on ne veut pas perdre.
    ///
    /// LE CHAMP EXISTE POUR LES MACHINES QU'ON N'A PAS ÉCRITES. L'état d'un
    /// plugin tiers ne se réduit pas à ses paramètres automatisables : il y a
    /// des échantillons chargés, des matrices de modulation, des tables
    /// dessinées à la main, des choses qu'aucun `ParamId` ne désigne. Un hôte
    /// qui ne sauvegarderait que les paramètres rouvrirait le morceau avec un
    /// autre son, sans le dire -- exactement l'échec que ce projet refuse.
    ///
    /// C'EST DU TEXTE, ET C'EST UNE DÉCISION. Les états natifs sont binaires ;
    /// c'est à l'hôte qui les produit de les encoder (en base64, en pratique),
    /// parce que la couche d'interopérabilité qui les écrira dans le projet ne
    /// connaît que du JSON et ne doit pas apprendre à manipuler des octets pour
    /// une famille de machines sur trente-cinq.
    ///
    /// Vide veut dire « je n'en ai pas », jamais « la sauvegarde a échoué » :
    /// une machine qui échoue à se décrire doit le faire savoir autrement.
    /// LE TRANSPORT, LIVRÉ JUSTE AVANT `process` (D7.4). Ne rien faire est le
    /// comportement des trente-quatre machines du parc, et c'est correct : le
    /// graphe leur donne des notes déjà horodatées, elles n'ont rien à
    /// synchroniser. Une machine qu'on n'a pas écrite, elle, ne peut pas
    /// deviner le tempo -- voir `TransportInfo`.
    ///
    /// LIVRÉ AVANT ET NON PASSÉ À `process` : élargir la signature de `process`
    /// obligerait trente-quatre machines à déclarer, documenter et ignorer un
    /// paramètre de plus. Même raison, et même forme, que `setSidechainInput`
    /// pour les effets.
    virtual void setTransportInfo(const TransportInfo&) {}

    virtual std::string saveNativeState() const { return {}; }
    /// Restaure ce que `saveNativeState()` a produit. Rend faux si l'état est
    /// refusé -- et un état refusé laisse la machine sur ses réglages
    /// précédents, ce qui produirait un son faux sans prévenir : l'appelant
    /// DOIT le signaler.
    virtual bool loadNativeState(const std::string&) { return false; }

};

using SynthPluginPtr = std::shared_ptr<ISynthPlugin>;

} // namespace vsm::audio::plugin
