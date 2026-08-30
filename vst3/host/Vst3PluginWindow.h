#pragma once
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/plugin/ISynthPlugin.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

// L'INTERFACE NATIVE D'UN PLUGIN VST3, DANS UNE FENÊTRE (D7.4).
//
// POURQUOI UN EN-TÊTE À PART. `Vst3PluginHost.h` ne parle que d'`ISynthPlugin`
// et d'`IAudioEffect` : c'est ce qui permet à `vsm-render` et aux tests de
// l'inclure sans rien savoir de JUCE ni d'une fenêtre. Une façade, elle, est du
// dessin -- elle n'a de sens que dans l'application, qui a déjà JUCE. Les deux
// vivent donc dans deux fichiers, et l'outil en ligne de commande n'emporte pas
// le second.
//
// CE QUE L'ÉDITEUR N'EST PAS : le lieu où vit l'état du plugin. L'état vit dans
// le plugin lui-même ; l'éditeur ne fait que le montrer. C'est ce qui rend
// « fermable sans perte d'état » vrai par construction plutôt que par
// précaution -- fermer la fenêtre détruit un dessin, pas un son.

namespace vsm::vst3 {

/// L'éditeur natif d'un instrument hébergé, ou nullptr s'il n'en a pas (ou si
/// l'objet n'est pas un plugin VST3). L'appelant en devient propriétaire ; le
/// détruire ferme la fenêtre sans toucher au plugin.
std::unique_ptr<juce::AudioProcessorEditor> createEditorFor(
    vsm::audio::plugin::ISynthPlugin& instrument);

/// Le même, pour un effet hébergé.
std::unique_ptr<juce::AudioProcessorEditor> createEditorFor(
    vsm::audio::effect::IAudioEffect& effect);

/// Vrai si cet objet est un plugin VST3 QUI A une interface. Répond faux pour
/// une machine du parc : les façades VSM ont leur propre fenêtre, et proposer
/// « ouvrir l'interface » sur une machine qui n'en a pas de native serait une
/// promesse en trop.
bool hasNativeEditor(const vsm::audio::plugin::ISynthPlugin& instrument);
bool hasNativeEditor(const vsm::audio::effect::IAudioEffect& effect);

} // namespace vsm::vst3
