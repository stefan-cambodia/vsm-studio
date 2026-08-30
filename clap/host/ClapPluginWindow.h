#pragma once
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/plugin/ISynthPlugin.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

// L'INTERFACE NATIVE D'UN PLUGIN CLAP, DANS UNE FENÊTRE (D7.4, différée puis
// faite).
//
// POURQUOI ELLE AVAIT ÉTÉ DIFFÉRÉE, ET POURQUOI ELLE NE L'EST PLUS. La note de
// D7.4 disait : « ce n'est pas long à écrire ; c'est impossible à exécuter une
// seule fois dans l'environnement où ce travail se fait, qui n'a pas
// d'affichage. Livrer cent cinquante lignes d'incrustation de fenêtre que
// personne n'a jamais vues tourner, en les déclarant faites, est exactement ce
// que ce projet refuse ailleurs. » L'affichage existe désormais. La condition
// posée est donc levée, et elle l'est **de la façon qu'elle exigeait** : la
// façade est ouverte pour de bon, sur un plugin qui implémente réellement
// l'extension d'interface CLAP.
//
// POURQUOI UN EN-TÊTE À PART, comme du côté VST3 : `ClapPluginHost.h` ne parle
// que d'`ISynthPlugin` et d'`IAudioEffect`, ce qui permet à `vsm-render` et aux
// tests de l'inclure sans rien savoir de JUCE. Une façade est du dessin ; elle
// n'a de sens que dans l'application.
//
// CE QUE LA FENÊTRE N'EST PAS : le lieu où vit l'état du plugin. L'état vit
// dans le plugin ; la fenêtre n'en montre qu'un dessin. C'est ce qui rend
// « fermable sans perte d'état » vrai par construction plutôt que par
// précaution.

namespace vsm::clap {

// `hasNativeEditor` vit dans `ClapPluginHost.h` : la question « ce plugin a-t-il
// une interface ? » se pose sans JUCE, et un test doit pouvoir la poser sans
// serveur graphique.

/// La façade native d'un instrument hébergé, ou nullptr s'il n'en a pas.
/// L'appelant en devient propriétaire ; la détruire ferme l'interface sans
/// toucher au plugin.
std::unique_ptr<juce::Component> createEditorFor(vsm::audio::plugin::ISynthPlugin& instrument);
/// Le même, pour un effet hébergé.
std::unique_ptr<juce::Component> createEditorFor(vsm::audio::effect::IAudioEffect& effect);

} // namespace vsm::clap
