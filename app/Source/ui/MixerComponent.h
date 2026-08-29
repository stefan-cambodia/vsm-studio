#pragma once
#include <JuceHeader.h>
#include <string>
#include <vector>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/audio/engine/MasterBus.h"
#include "vsm/sequencer/Project.h"
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

// Console de mixage (section 15/21, "Phase 2 UI"). Une tranche par piste +
// une tranche master. Le Mixer n'est PAS une source de vérité : il lit/écrit
// directement les champs de vsm::sequencer::Track (volume/pan/muted/solo) --
// comme le documente Mixer.h côté moteur -- et notifie le parent via
// onMixChanged() pour qu'il republie le snapshot audio. Les paramètres du
// bus master transitent par onMasterParam()/onMasterEnable() (le MasterBus
// est déjà thread-safe : setParameter y est atomique).
//
// Aucune logique DSP ici : cette couche est purement UI, testée à la main
// puisqu'elle dépend de JUCE (le DSP correspondant, lui, est couvert par les
// tests de vsm_audio : Mixer, MasterBus, MeterBank).

/// Petit vu-mètre crête vertical (échelle dB), rafraîchi par le parent.
class LevelMeter : public juce::Component {
public:
    void setLevel(float linearPeak) {
        // Amplitude linéaire -> position 0..1 sur une échelle -60..0 dBFS.
        float db = linearPeak > 1.0e-5f ? 20.0f * std::log10(linearPeak) : -100.0f;
        float pos = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (std::abs(pos - level_) > 1.0e-4f || pos > level_) {
            level_ = pos;
            if (pos > peakHold_) peakHold_ = pos;
            else peakHold_ = juce::jmax(0.0f, peakHold_ - 0.01f); // redescente lente
            repaint();
        }
    }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(vsm::ui::Palette::pianoKeyBlack);
        g.fillRoundedRectangle(r, 2.0f);
        if (level_ > 0.0f) {
            float h = r.getHeight() * level_;
            juce::Rectangle<float> bar(r.getX(), r.getBottom() - h, r.getWidth(), h);
            juce::ColourGradient grad(vsm::ui::Palette::accentTeal, 0, r.getBottom(),
                                       vsm::ui::Palette::accentRed, 0, r.getY(), false);
            grad.addColour(0.75, vsm::ui::Palette::accentAmber);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(bar, 2.0f);
        }
        if (peakHold_ > 0.0f) {
            float y = r.getBottom() - r.getHeight() * peakHold_;
            g.setColour(vsm::ui::Palette::textPrimary);
            g.fillRect(r.getX(), y, r.getWidth(), 1.5f);
        }
    }
private:
    float level_ = 0.0f, peakHold_ = 0.0f;
};

/// Tranche d'une piste.
class ChannelStrip : public juce::Component {
public:
    /// `sendNames` donne un bouton par bus DÉCLARÉ par le projet, dans son
    /// ordre. Deux boutons figés promettaient deux départs qui n'étaient écrits
    /// nulle part et dont rien ne disait le contenu ; un bouton par bus nommé
    /// dit ce qu'on alimente.
    ChannelStrip(vsm::sequencer::Track& track, size_t index,
                  const std::vector<std::string>& sendNames);
    void resized() override;
    void paint(juce::Graphics&) override;
    void setMeterLevel(float linearPeak) { meter_.setLevel(linearPeak); }

    std::function<void()> onMixChanged;
    /// Prévenu AVANT qu'un geste ne modifie le mixage : c'est là que
    /// l'application prend son instantané d'annulation. Séparé de
    /// `onMixChanged`, qui arrive après et à chaque échantillon d'un glissé --
    /// s'en servir empilerait trois cents pas d'annulation pour un seul
    /// mouvement de fader.
    std::function<void()> onMixEditStarted;

private:
    vsm::sequencer::Track& track_;
    size_t index_;
    juce::Label nameLabel_;
    juce::Slider volume_;
    juce::Slider pan_;
    /// Un bouton par bus de départ du projet. `OwnedArray` et non deux membres :
    /// leur nombre n'est plus connu à la compilation.
    juce::OwnedArray<juce::Slider> sends_;
    juce::TextButton mute_ { "M" };
    juce::TextButton solo_ { "S" };
    LevelMeter meter_;
};

/// Tranche master : EQ 3 bandes, compresseur, saturation, plafond limiteur,
/// mètre LUFS + crête. Liée aux paramètres du MasterBus via callbacks.
class MasterStrip : public juce::Component {
public:
    MasterStrip();
    void resized() override;
    void paint(juce::Graphics&) override;

    void setMeters(double lufs, float linearPeak) {
        meter_.setLevel(linearPeak);
        lufsLabel_.setText(lufs <= vsm::audio::dsp::LufsMeter::kSilence + 1.0
                               ? juce::String("-inf LUFS")
                               : juce::String(lufs, 1) + " LUFS",
                           juce::dontSendNotification);
    }

    // Fournit/pousse les paramètres du bus master.
    std::function<void(vsm::audio::plugin::ParamId, float)> onMasterParam;
    std::function<void(bool)> onMasterEnable;
    std::function<float(vsm::audio::plugin::ParamId)> masterParamProvider;

    /// Synchronise l'UI depuis les valeurs courantes du bus master.
    void syncFromEngine();

private:
    juce::Slider& addKnob(vsm::audio::plugin::ParamId id, const juce::String& label,
                          float min, float max, float def, const juce::String& suffix);

    juce::TextButton enableButton_ { "MASTER" };
    juce::Label titleLabel_, lufsLabel_;
    LevelMeter meter_;

    struct Knob {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
        vsm::audio::plugin::ParamId id;
    };
    std::vector<Knob> knobs_;
};

/// Console complète : défilement horizontal des tranches de piste + master
/// fixe à droite.
class MixerComponent : public juce::Component {
public:
    MixerComponent();
    void resized() override;
    void paint(juce::Graphics&) override;

    /// (Re)construit les tranches depuis le projet.
    void setProject(vsm::sequencer::Project* project);

    /// Rafraîchit les vu-mètres (appelé par le timer du parent, thread UI).
    void updateMeters(const std::function<float(size_t)>& trackPeak,
                      double masterLufs, float masterPeak);

    std::function<void()> onMixChanged;
    std::function<void()> onMixEditStarted;
    std::function<void(vsm::audio::plugin::ParamId, float)> onMasterParam;
    std::function<void(bool)> onMasterEnable;
    std::function<float(vsm::audio::plugin::ParamId)> masterParamProvider;

private:
    vsm::sequencer::Project* project_ = nullptr;
    juce::Viewport viewport_;
    juce::Component stripContainer_;
    juce::OwnedArray<ChannelStrip> strips_;
    MasterStrip master_;

    static constexpr int kStripWidth = 76;
    static constexpr int kMasterWidth = 150;
};
