#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/audio/engine/AutomationLane.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/sequencer/Project.h"
#include <functional>
#include <vector>

// Éditeur de courbes d'automation (section 17, dernière pièce de l'UI Phase 2).
// L'utilisateur choisit une piste + un paramètre de son instrument, puis
// édite des points (clic = ajouter, glisser = déplacer, clic droit =
// supprimer). Chaque édition reconstruit la liste complète de lanes et la
// publie via onAutomationChanged -> ProcessGraph::setAutomationLanes (chemin
// RT-safe). L'axe horizontal = temps (ticks), vertical = valeur du paramètre
// (bornes lues dans son ParameterInfo).
//
// Aucune logique DSP ici : l'interpolation et l'application temps réel vivent
// dans vsm::audio::engine::AutomationLane / ProcessGraph (déjà testés).
class AutomationComponent : public juce::Component {
public:
    AutomationComponent();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    /// (Re)lit les pistes depuis le projet. Conserve les lanes déjà éditées.
    void setProject(vsm::sequencer::Project* project);

    /// Fournit l'instrument d'une piste (pour lister ses paramètres + bornes).
    std::function<vsm::audio::plugin::ISynthPlugin*(size_t)> instrumentProvider;

    /// Émis après chaque édition : liste complète des lanes à publier.
    std::function<void(const std::vector<vsm::audio::engine::AutomationLane>&)> onAutomationChanged;

private:
    void rebuildTrackBox();
    void rebuildParamBox();
    void loadSelectedLane();        // remplit editPoints_ depuis lanes_
    void commit();                  // reconstruit la lane sélectionnée + notifie

    juce::Rectangle<int> editorArea() const;
    int   tickToX(vsm::audio::engine::Tick tick) const;
    vsm::audio::engine::Tick xToTick(int x) const;
    int   valueToY(float value) const;
    float yToValue(int y) const;
    int   findPointNear(juce::Point<int> p) const;

    vsm::sequencer::Project* project_ = nullptr;

    juce::Label trackLabel_, paramLabel_, hintLabel_;
    juce::ComboBox trackBox_, paramBox_;

    struct ParamEntry { vsm::audio::plugin::ParamId id; float min; float max; };
    std::vector<ParamEntry> paramEntries_; // parallèle aux items de paramBox_

    std::vector<vsm::audio::engine::AutomationLane> lanes_;
    std::vector<vsm::audio::engine::AutomationPoint> editPoints_; // lane courante en cours d'édition

    size_t selectedTrack_ = 0;
    vsm::audio::plugin::ParamId selectedParam_ = 0;
    float paramMin_ = 0.0f, paramMax_ = 1.0f;
    bool hasSelection_ = false;

    int dragIndex_ = -1;
    vsm::audio::engine::Tick maxTick_ = 1920 * 4;

    static constexpr int kPointRadius = 5;
};
