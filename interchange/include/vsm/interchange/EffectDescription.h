#pragma once
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/engine/MasterBus.h"
#include "vsm/sequencer/Track.h"
#include <map>
#include <string>

// Traduction entre un effet d'insert VIVANT (un `IAudioEffect`, qui traite du
// son) et sa DESCRIPTION (un `TrackEffect`, qui se range dans une piste et
// s'écrit dans `project.json`). Exact pendant de `capturePreset` /
// `applyPreset` pour les machines, et pour la même raison : `core/` ne connaît
// pas `audio/`, et le disque ne connaît que des noms et des nombres.
//
// LES PARAMÈTRES SONT NOMMÉS PAR LEUR IDENTITÉ SÉMANTIQUE (« reverb.1.mix »),
// pas par leur numéro. Un numéro de paramètre est une position dans une liste :
// intercaler un réglage dans un effet décalerait tous les suivants et
// relirait, sans rien signaler, des valeurs mises sur les mauvais boutons.
// C'est la règle du § 3 de `ROADMAP-interop.md`, appliquée aux effets.

namespace vsm::interchange {

/// L'identifiant sémantique d'un effet de fabrique : `reverb` -> `fx.reverb`.
/// Centralisé ici pour que l'écriture et la lecture ne puissent pas diverger.
std::string effectSemanticPluginId(const std::string& factoryTypeId);

/// Décrit un effet vivant : son type et la valeur COURANTE de chacun de ses
/// paramètres, en unités réelles.
vsm::sequencer::TrackEffect describeEffect(const std::string& factoryTypeId,
                                            const vsm::audio::effect::IAudioEffect& effect);

/// Ce que l'application d'une description a réellement pu faire.
struct EffectApplyReport {
    int applied = 0;
    /// Paramètres décrits que cet effet ne connaît pas. Nommés, jamais
    /// ignorés en silence : c'est le signe d'un projet écrit par une version
    /// différente, et l'utilisateur doit pouvoir l'apprendre.
    std::vector<std::string> unknownParameters;
    /// D7.3 : l'état natif d'un effet tiers a-t-il été reposé ? Faux quand il
    /// n'y en avait pas -- et aussi quand l'effet l'a REFUSÉ, ce que l'appelant
    /// doit signaler : un état refusé laisse l'effet sur d'autres réglages,
    /// donc sur un autre son.
    bool nativeStateApplied = false;
};

/// Repose une description sur un effet vivant. L'effet doit être du type
/// décrit ; la fonction ne le fabrique pas, elle le règle.
EffectApplyReport applyEffectDescription(const vsm::sequencer::TrackEffect& described,
                                          vsm::audio::effect::IAudioEffect& effect);

/// La tranche master, décrite de la même façon : par NOM de réglage.
///
/// ELLE N'A PAS DE TABLE SÉMANTIQUE, ET C'EST ASSUMÉ. Les identités
/// sémantiques servent à parler d'une machine sans la connaître -- c'est ce
/// qui permet à `analyse/` de viser « filter.1.cutoff » sur n'importe quel
/// synthé. La tranche master est unique : il n'y en a qu'une, elle ne se
/// remplace par rien, et personne n'a besoin de désigner « son compresseur »
/// de façon portable. Son nom affiché suffit, et il est stable.
std::map<std::string, float> describeMasterBus(const vsm::audio::engine::MasterBus& bus);

/// Repose une description sur la tranche master. Rend le nombre de réglages
/// réellement appliqués.
int applyMasterDescription(const std::map<std::string, float>& described,
                            vsm::audio::engine::MasterBus& bus);

} // namespace vsm::interchange
