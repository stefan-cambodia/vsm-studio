#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"

/// Règle temporelle du piano roll : numéros de mesure, tête de lecture,
/// région de boucle. Partage les conversions tick <-> pixel du piano roll
/// (tickToX/keyboardWidth), donc tout reste aligné à la colonne près quel que
/// soit le zoom -- une règle qui aurait son propre calcul finirait toujours
/// par dériver d'un pixel ou deux.
///
/// Interactions : clic (ou glissé) = déplacer la tête de lecture ;
/// Maj + glissé = définir la région de boucle ; double-clic = désactiver la
/// boucle ; clic droit = poser ou retirer un repère.
///
/// LES REPÈRES SE DESSINENT ICI parce que c'est là qu'ils sont : sur la ligne
/// de temps, et non dans une piste. Le format MIDI en porte depuis toujours et
/// ce projet les conservait en octets opaques -- lus, réexportés fidèlement, et
/// invisibles.
class PianoRollRulerComponent : public juce::Component {
public:
    explicit PianoRollRulerComponent(PianoRollComponent& pianoRoll);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    void setLoopRegion(vsm::midi::Tick start, vsm::midi::Tick end, bool active);
    /// RÉGION DE PUNCH (D3.5) : là où l'enregistrement capte, et nulle part
    /// ailleurs. Dessinée en rouge, distincte de la boucle, parce qu'on les
    /// règle souvent au même endroit sans qu'elles disent la même chose.
    void setPunchRegion(vsm::midi::Tick start, vsm::midi::Tick end, bool active);

    std::function<void(vsm::midi::Tick)> onPlayheadRequested;
    /// Clic droit sur la règle : poser un repère à cet endroit. L'interface
    /// demande son nom, parce qu'un repère sans nom ne repère rien.
    std::function<void(vsm::midi::Tick)> onMarkerRequested;
    /// Retirer le repère d'index donné.
    std::function<void(size_t)> onMarkerRemoved;
    /// Renommer le repère d'index donné (D16.4 : le même geste que dans
    /// l'arrangement ; deux vues, un geste).
    std::function<void(size_t)> onMarkerRenameRequested;
    std::function<void(vsm::midi::Tick start, vsm::midi::Tick end, bool active)> onLoopRegionChanged;
    /// La région de punch a été dessinée à la souris (Alt + glisser).
    std::function<void(vsm::midi::Tick start, vsm::midi::Tick end, bool active)> onPunchRegionChanged;

private:
    PianoRollComponent& pianoRoll_;
    vsm::midi::Tick loopStart_ = 0, loopEnd_ = 0;
    bool loopActive_ = false;
    vsm::midi::Tick loopDragAnchor_ = 0;
    vsm::midi::Tick punchStart_ = 0;
    vsm::midi::Tick punchEnd_ = 0;
    bool punchActive_ = false;
    vsm::midi::Tick punchDragAnchor_ = 0;
};
