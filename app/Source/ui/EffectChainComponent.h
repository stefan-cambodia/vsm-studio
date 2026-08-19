#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/audio/effect/IAudioEffect.h"
#include <functional>
#include <map>
#include <memory>
#include <vector>

// Éditeur de chaîne d'effets d'insert (section 5 : TRACK -> SYNTH -> EFFECTS
// -> MIX). Dernière pièce UI de la Phase 2. Pour la piste active :
//  - "Ajouter" liste les effets de EffectFactory ;
//  - la chaîne courante s'affiche (sélection, monter/descendre, supprimer) ;
//  - les paramètres de l'effet sélectionné sont éditables (un knob par
//    entrée de parameterList(), comme le Synth Rack -- zéro code par effet).
//
// Les effets sont prepare()és sur le thread UI AVANT publication ; la chaîne
// est ensuite publiée atomiquement au moteur via onChainChanged (RT-safe).
class EffectChainComponent : public juce::Component {
public:
    using EffectPtr = std::shared_ptr<vsm::audio::effect::IAudioEffect>;
    using Chain = std::vector<EffectPtr>;

    EffectChainComponent();

    void paint(juce::Graphics&) override;
    void resized() override;

    void setAudioConfig(double sampleRate, int blockSize) { sampleRate_ = sampleRate; blockSize_ = blockSize; }
    void setActiveTrack(int trackIndex);

    /// Publie la chaîne (immuable) de la piste au moteur.
    std::function<void(size_t trackIndex, std::shared_ptr<const Chain>)> onChainChanged;

private:
    void rebuildEffectList();
    void rebuildParamControls();
    void publishActiveChain();
    Chain* activeChain();

    int activeTrack_ = -1;
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    int selectedEffect_ = -1;

    std::map<int, Chain> chains_; // par piste

    juce::Label titleLabel_;
    juce::Label addLabel_;
    juce::ComboBox addBox_;

    // Rangées d'effets (nom + monter/descendre/supprimer).
    struct EffectRow {
        std::unique_ptr<juce::TextButton> select;
        std::unique_ptr<juce::TextButton> up;
        std::unique_ptr<juce::TextButton> down;
        std::unique_ptr<juce::TextButton> remove;
    };
    std::vector<EffectRow> rows_;

    // Contrôles de paramètres de l'effet sélectionné.
    struct ParamControl {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
    };
    std::vector<ParamControl> params_;
    juce::Label paramHeader_;
};
