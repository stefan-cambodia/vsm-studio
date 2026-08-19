#pragma once
#include <JuceHeader.h>
#include "machines/MachinePanelComponent.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <memory>
#include <vector>

// Éditeur de synthé GÉNÉRIQUE : un knob par paramètre exposé via
// ISynthPlugin::parameterList(), sans code spécifique à une machine.
// Remplit la promesse de la section 22 du cahier des charges ("l'ajout
// d'un nouveau synthétiseur ne doit pas nécessiter de modifier le moteur
// principal") jusque dans l'UI : un nouveau plugin Phase 3+ (TB-303-style,
// Juno-106-style...) s'affiche ici sans qu'aucune ligne de ce fichier ne
// mentionne son nom.
//
// Une interface "façon hardware" dédiée par machine (sections 6 et 21 du
// cahier des charges) reste prévue plus tard ; ce panneau générique est ce
// qui permet DÈS MAINTENANT de régler et d'entendre n'importe quel plugin
// enregistré.
class SynthRackComponent : public juce::Component {
public:
    SynthRackComponent();

    /// synth == nullptr : aucune piste sélectionnée ou aucun instrument
    /// assigné à la piste sélectionnée.
    ///
    /// `pluginId` sert à retrouver la FAÇADE dédiée de la machine (façon
    /// hardware). Si elle existe, elle remplace le panneau générique ; sinon
    /// on retombe sur la liste de potentiomètres, qui reste le filet de
    /// sécurité : toute machine reste réglable, y compris une machine tierce
    /// chargée en CLAP dont on ne connaît pas la façade.
    void setSynth(vsm::audio::plugin::ISynthPlugin* synth, const juce::String& trackName,
                   const std::string& pluginId = {});

    /// Piste éditée par le séquenceur intégré des machines qui en ont un
    /// (boîtes à rythmes, TB-303). La grille de pas est une VUE sur ses notes.
    void setTrack(vsm::sequencer::Track* track);
    void setPlayheadTick(vsm::midi::Tick tick);
    /// Le motif a été édité depuis la façade : republier le planning.
    std::function<void()> onPatternEdited;

    void paint(juce::Graphics&) override;
    void resized() override;

    // MIDI Learn : le bouton arme/désarme le mode ; onParamTouched est
    // émis quand l'utilisateur bouge un knob (pour désigner la cible à lier).
    std::function<void(bool)> onLearnModeChanged;
    std::function<void(vsm::audio::plugin::ParamId)> onParamTouched;
    /// Reflète l'état d'armement réel du moteur (le bouton se désactive tout
    /// seul une fois un CC lié).
    void setLearnArmed(bool armed);
    bool isLearnMode() const { return learnMode_; }

private:
    void rebuildControls();

    vsm::audio::plugin::ISynthPlugin* synth_ = nullptr;

    juce::Label titleLabel_;
    juce::Label machineNameLabel_;
    juce::TextButton learnButton_ { "MIDI LEARN" };
    bool learnMode_ = false;
    juce::Viewport viewport_;
    juce::Component controlContainer_;
    MachinePanelComponent machinePanel_;
    bool usingMachinePanel_ = false;

    struct ParamControl {
        vsm::audio::plugin::ParamId id = 0;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> nameLabel;
    };
    std::vector<ParamControl> controls_;

    static constexpr int kRowHeight = 70;
};
