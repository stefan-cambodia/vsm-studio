#pragma once
#include <JuceHeader.h>
#include "HardwareLookAndFeel.h"
#include "StepSequencerComponent.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/panels/MachinePanel.h"
#include <memory>
#include <vector>

/// Rend UNE façade « façon hardware » décrite par `vsm::panels::MachinePanel`,
/// pour n'importe quelle machine. Un seul composant sert les douze : ce qui
/// change d'une machine à l'autre est une donnée (blocs, commandes, couleurs),
/// pas du code de dessin -- voir MachinePanel.h pour le pourquoi.
///
/// Le composant se redimensionne en conservant les PROPORTIONS de la façade :
/// une machine large et plate (TB-303) ne doit pas devenir un carré parce que
/// la fenêtre l'est. Les commandes gardent donc leur place relative, comme sur
/// l'objet réel.
class MachinePanelComponent : public juce::Component, private juce::Timer {
public:
    MachinePanelComponent();
    ~MachinePanelComponent() override;

    /// `panel == nullptr` ou `synth == nullptr` : le composant se vide.
    void setPanel(const vsm::panels::MachinePanel* panel, vsm::audio::plugin::ISynthPlugin* synth);

    /// Piste éditée par le séquenceur intégré de la machine (grille de pas).
    /// nullptr = pas de piste : la grille s'affiche mais reste inerte.
    void setTrack(vsm::sequencer::Track* track);
    /// Position de lecture, en ticks, pour éclairer le pas en cours.
    void setPlayheadTick(vsm::midi::Tick tick);
    /// Le motif a été édité : l'application doit republier le planning.
    std::function<void()> onPatternEdited;

    /// Émis quand l'utilisateur touche une commande (MIDI Learn).
    std::function<void(vsm::audio::plugin::ParamId)> onParamTouched;

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Proportions naturelles de la façade, pour que le conteneur puisse lui
    /// réserver une place cohérente.
    double aspectRatio() const;

private:
    struct Control {
        vsm::audio::plugin::ParamId paramId = 0;
        vsm::panels::ControlStyle style = vsm::panels::ControlStyle::Knob;
        std::unique_ptr<juce::Component> widget; // Slider ou ToggleButton
        std::unique_ptr<juce::Label> caption;
        /// Position dans la grille PROPRE AU BLOC (pas celle de la façade) :
        /// chaque bloc répartit ses commandes dans sa propre surface, ce qui
        /// leur donne la plus grande taille possible.
        juce::Rectangle<float> cellInSection;
        size_t sectionIndex = 0;
    };

    void rebuild();
    void showValueReadout(const juce::String& caption, double value, const juce::String& unit);
    void timerCallback() override; ///< resynchronise l'affichage avec le moteur
    juce::Rectangle<float> gridToPixels(juce::Rectangle<float> gridBounds) const;

    const vsm::panels::MachinePanel* panel_ = nullptr;
    vsm::audio::plugin::ISynthPlugin* synth_ = nullptr;
    HardwareLookAndFeel lookAndFeel_;
    std::vector<Control> controls_;
    std::vector<std::unique_ptr<juce::Label>> sectionTitles_;
    /// Afficheur unique, en bas de façade : une vraie machine n'affiche aucun
    /// chiffre, mais un logiciel doit pouvoir en donner un quand on règle.
    /// Un seul emplacement, plutôt qu'un nombre sous chaque bouton.
    juce::Label valueReadout_;
    StepSequencerComponent sequencer_;
    vsm::sequencer::Track* track_ = nullptr;
};
