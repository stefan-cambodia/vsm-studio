#pragma once
#include <JuceHeader.h>
#include "PianoRollComponent.h"
#include "PianoRollRulerComponent.h"
#include "PianoRollToolbar.h"
#include "VelocityLaneComponent.h"
#include "LookAndFeel/VsmLookAndFeel.h"

/// Assemble l'éditeur complet dans une seule fenêtre flottante : barre
/// d'outils, règle temporelle, piano roll, lane de vélocité, barre d'état.
///
/// Le panneau est aussi le point de raccordement des callbacks entre ces
/// composants (la règle déplace la tête de lecture du piano roll, une édition
/// de vélocité repeint le roll, etc.) : chacun reste ignorant des autres,
/// conformément à la façon dont le reste de l'application est câblée.
class PianoRollPanel : public juce::Component {
public:
    PianoRollPanel(PianoRollComponent& pianoRoll, VelocityLaneComponent& velocityLane)
        : pianoRoll_(pianoRoll), velocityLane_(velocityLane),
          toolbar_(pianoRoll), ruler_(pianoRoll) {
        addAndMakeVisible(toolbar_);
        addAndMakeVisible(ruler_);
        addAndMakeVisible(pianoRoll_);
        addAndMakeVisible(velocityLane_);
        addAndMakeVisible(statusLabel_);

        // D32.3 : LE CLAVIER À L'ÉCRAN, caché tant qu'on ne le demande pas.
        addChildComponent(clavier_);
        // SIX OCTAVES À PARTIR DE DO0, ET LA PREMIÈRE TOUCHE VISIBLE EST
        // CELLE DU BAS. La plage commençait à 36 : la note 36 tombait alors
        // sous le bouton de défilement du composant, et une basse jouée à C2
        // n'allumait rien de visible -- le masque était pourtant juste. On
        // descend la plage plutôt que de rogner le bouton.
        clavier_.setAvailableRange(24, 96);
        clavier_.setLowestVisibleKey(24);
        clavier_.setKeyWidth(18.0f);
        clavier_.setScrollButtonsVisible(false);
        // LA TOUCHE ENFONCÉE DOIT SE VOIR. Le voile par défaut de JUCE est
        // presque invisible sur une touche blanche : la première capture
        // montrait un do sonnant sans qu'aucune touche ne change -- le masque
        // était juste (0x1000000000, la note 36), c'est la PEINTURE qui ne
        // disait rien. On lui donne l'ambre de l'application, opaque.
        clavier_.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId,
                            vsm::ui::Palette::accentAmber);
        clavier_.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
                            vsm::ui::Palette::accentTeal.withAlpha(0.5f));
        clavier_.setColour(juce::MidiKeyboardComponent::shadowColourId,
                            juce::Colours::black.withAlpha(0.35f));
        clavier_.setVisible(false);
        ecoute_.panneau = this;
        etat_.addListener(&ecoute_);

        statusLabel_.setColour(juce::Label::textColourId, vsm::ui::Palette::textSecondary);
        statusLabel_.setFont(juce::Font(juce::FontOptions(12.0f)));
        statusLabel_.setText(u8"Prêt", juce::dontSendNotification);

        pianoRoll_.onStatusChanged = [this](const juce::String& text) {
            statusLabel_.setText(text, juce::dontSendNotification);
        };
        pianoRoll_.onEditStateChanged = [this] { toolbar_.refreshFromPianoRoll(); };

        ruler_.onPlayheadRequested = [this](vsm::midi::Tick tick) {
            if (pianoRoll_.onPlayheadRequested) pianoRoll_.onPlayheadRequested(tick);
            pianoRoll_.setPlayheadTick(tick);
            ruler_.repaint();
        };
        ruler_.onLoopRegionChanged = [this](vsm::midi::Tick start, vsm::midi::Tick end, bool active) {
            pianoRoll_.setLoopRegion(start, end, active);
            if (pianoRoll_.onLoopRegionChanged) pianoRoll_.onLoopRegionChanged(start, end, active);
        };
        ruler_.onPunchRegionChanged = [this](vsm::midi::Tick start, vsm::midi::Tick end, bool active) {
            if (pianoRoll_.onPunchRegionChanged) pianoRoll_.onPunchRegionChanged(start, end, active);
        };
        ruler_.onMarkerRequested = [this](vsm::midi::Tick tick) {
            if (onMarkerRequested) onMarkerRequested(tick);
        };
        ruler_.onMarkerRemoved = [this](size_t index) {
            if (onMarkerRemoved) onMarkerRemoved(index);
        };
        ruler_.onMarkerRenameRequested = [this](size_t index) {
            if (onMarkerRenameRequested) onMarkerRenameRequested(index);
        };
        velocityLane_.onVelocityEdited = [this] {
            pianoRoll_.repaint();
            if (onVelocityEdited) onVelocityEdited();
        };
    }

    /// Rafraîchit règle et barre d'outils (appelé quand la tête de lecture
    /// bouge ou qu'un projet est chargé).
    void refresh() {
        ruler_.repaint();
        toolbar_.refreshFromPianoRoll();
    }

    /// La région de punch appartient au PROJET : la règle la dessine, mais c'est
    /// l'application qui la détient. Voir `vsm::sequencer::Project::punchEnabled`.
    void setPunchRegion(vsm::midi::Tick start, vsm::midi::Tick end, bool active) {
        ruler_.setPunchRegion(start, end, active);
    }

    std::function<void()> onVelocityEdited;
    /// Poser un repère à ce tick (l'application demande son nom), ou retirer
    /// celui d'index donné.
    std::function<void(vsm::midi::Tick)> onMarkerRequested;
    std::function<void(size_t)> onMarkerRenameRequested;
    std::function<void(size_t)> onMarkerRemoved;

    void resized() override {
        auto area = getLocalBounds();
        toolbar_.setBounds(area.removeFromTop(92));   // D29.4 : trois rangées, la ligne d'information en bas
        statusLabel_.setBounds(area.removeFromBottom(20).reduced(8, 0));
        // D32.3 : LE CLAVIER SOUS LA LANE DE VÉLOCITÉ, tout en bas. C'est là
        // qu'un clavier se trouve sur un instrument, et c'est aussi l'endroit
        // où il ne coupe pas la lecture du piano roll en deux.
        if (clavier_.isVisible()) clavier_.setBounds(area.removeFromBottom(72).reduced(4, 2));
        velocityLane_.setBounds(area.removeFromBottom(110));
        ruler_.setBounds(area.removeFromTop(22));
        pianoRoll_.setBounds(area);
    }

    /// D32.3 : montre ou cache le clavier à l'écran. Caché par défaut : le
    /// piano roll a déjà son clavier vertical, et soixante-douze pixels pris à
    /// l'édition doivent se demander.
    void setKeyboardVisible(bool visible) {
        clavier_.setVisible(visible);
        resized();
    }
    bool keyboardVisible() const { return clavier_.isVisible(); }

    /// D32.3 : allume les touches que la piste choisie joue en ce moment.
    /// `basses` et `hautes` sont le masque de 128 bits du graphe.
    void setSoundingNotes(uint64_t basses, uint64_t hautes) {
        if (!clavier_.isVisible()) return;
        if (basses == sonnantesBasses_ && hautes == sonnantesHautes_) return;
        for (int note = 0; note < 128; ++note) {
            const uint64_t mot = note < 64 ? basses : hautes;
            const uint64_t avant = note < 64 ? sonnantesBasses_ : sonnantesHautes_;
            const uint64_t bit = uint64_t{1} << (note % 64);
            const bool maintenant = (mot & bit) != 0;
            if (maintenant == ((avant & bit) != 0)) continue;
            // LE MORCEAU JOUE SUR UN CANAL À PART (16). Sans cela, une note
            // que le transport tient et qu'on relâche à la souris s'éteindrait
            // deux fois, et une note qu'on tient pendant que le morceau la
            // joue s'éteindrait quand le morceau la lâche.
            if (maintenant) etat_.noteOn(16, note, 0.8f);
            else etat_.noteOff(16, note, 0.0f);
        }
        sonnantesBasses_ = basses;
        sonnantesHautes_ = hautes;
    }

    /// Une note jouée AU CLAVIER DE L'ÉCRAN. L'application l'envoie par le
    /// même chemin que le clavier d'ordinateur -- deux chemins pour une seule
    /// idée finiraient par ne plus jouer pareil.
    std::function<void(int note, float velocity, bool on)> onKeyboardNote;

    void paint(juce::Graphics& g) override { g.fillAll(vsm::ui::Palette::background); }

private:
    PianoRollComponent& pianoRoll_;
    VelocityLaneComponent& velocityLane_;
    PianoRollToolbar toolbar_;
    PianoRollRulerComponent ruler_;
    juce::Label statusLabel_;

    // D32.3 — LE CLAVIER À L'ÉCRAN.
    juce::MidiKeyboardState etat_;
    juce::MidiKeyboardComponent clavier_ { etat_, juce::MidiKeyboardComponent::horizontalKeyboard };
    uint64_t sonnantesBasses_ = 0, sonnantesHautes_ = 0;
    /// Reçoit les notes de `etat_` -- celles de la souris comme celles qu'on y
    /// pose pour le voyant. Le canal 16 est celui du MORCEAU : on ne le
    /// renvoie pas à l'application, sans quoi le transport se rejouerait
    /// lui-même.
    struct EcouteClavier : juce::MidiKeyboardState::Listener {
        PianoRollPanel* panneau = nullptr;
        void handleNoteOn(juce::MidiKeyboardState*, int canal, int note, float velo) override {
            if (canal != 16 && panneau && panneau->onKeyboardNote)
                panneau->onKeyboardNote(note, velo, true);
        }
        void handleNoteOff(juce::MidiKeyboardState*, int canal, int note, float velo) override {
            if (canal != 16 && panneau && panneau->onKeyboardNote)
                panneau->onKeyboardNote(note, velo, false);
        }
    };
    EcouteClavier ecoute_;

public:
    ~PianoRollPanel() override { etat_.removeListener(&ecoute_); }
};
