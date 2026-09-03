#pragma once
#include <JuceHeader.h>
#include "vsm/audio/engine/Mixer.h"
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

/// Petit vu-mètre vertical (échelle dB), rafraîchi par le parent.
///
/// DEPUIS D4.7 IL EN MONTRE DEUX : la barre pleine est la valeur EFFICACE
/// (RMS), le trait fin la CRÊTE. La crête seule disait si ça écrête ; elle ne
/// disait pas si c'était fort, et deux pistes de même crête peuvent être
/// séparées de quinze décibels perçus. Les deux dans le même mètre, c'est ce
/// que fait toute console, et pour la même raison : on lit d'un coup d'œil
/// l'écart entre les deux, qui est la densité de la piste.
class LevelMeter : public juce::Component {
public:
    /// Le niveau efficace, qui remplit la barre.
    void setRms(float linearRms) {
        const float db = linearRms > 1.0e-5f ? 20.0f * std::log10(linearRms) : -100.0f;
        const float pos = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        if (std::abs(pos - rms_) > 1.0e-4f) { rms_ = pos; repaint(); }
    }
    /// La corrélation de phase, de -1 à +1, peinte en pied de mètre.
    void setCorrelation(float value) {
        if (std::abs(value - correlation_) > 1.0e-3f) { correlation_ = value; repaint(); }
    }
    float correlation() const { return correlation_; }
    float rmsPosition() const { return rms_; }

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
        // LA BANDE DU BAS EST LA CORRÉLATION DE PHASE : au centre, sans
        // rapport ; à droite, en phase ; à GAUCHE, en opposition -- et c'est le
        // seul endroit du logiciel qui dise qu'une piste va disparaître en mono.
        auto barre = r;
        const float hauteurPhase = 4.0f;
        auto phase = barre.removeFromBottom(hauteurPhase);
        barre.removeFromBottom(2.0f);

        if (level_ > 0.0f) {
            float h = barre.getHeight() * level_;
            juce::Rectangle<float> bar(barre.getX(), barre.getBottom() - h, barre.getWidth(), h);
            juce::ColourGradient grad(vsm::ui::Palette::accentTeal, 0, barre.getBottom(),
                                       vsm::ui::Palette::accentRed, 0, barre.getY(), false);
            grad.addColour(0.75, vsm::ui::Palette::accentAmber);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(bar, 2.0f);
        }
        // LA CRÊTE EST UN TRAIT, le RMS remplit : la barre pleine dit le
        // niveau, le trait dit la marge avant écrêtage, et l'écart entre les
        // deux dit la densité de la piste.
        if (rms_ > 0.0f) {
            const float h = barre.getHeight() * rms_;
            g.setColour(vsm::ui::Palette::textPrimary.withAlpha(0.35f));
            g.fillRect(barre.getX(), barre.getBottom() - h, barre.getWidth(), 1.0f);
        }
        if (peakHold_ > 0.0f) {
            float y = barre.getBottom() - barre.getHeight() * peakHold_;
            g.setColour(vsm::ui::Palette::textPrimary);
            g.fillRect(barre.getX(), y, barre.getWidth(), 1.5f);
        }

        g.setColour(vsm::ui::Palette::pianoKeyBlack);
        g.fillRect(phase);
        const float centre = phase.getCentreX();
        const float x = centre + correlation_ * phase.getWidth() * 0.5f;
        // Rouge dès que la corrélation devient négative : ce n'est pas une
        // nuance, c'est un avertissement.
        g.setColour(correlation_ < 0.0f ? vsm::ui::Palette::accentRed
                                        : vsm::ui::Palette::accentTeal);
        g.fillRect(juce::Rectangle<float>(std::min(centre, x), phase.getY(),
                                           std::abs(x - centre) + 1.0f, phase.getHeight()));
    }
private:
    float level_ = 0.0f, peakHold_ = 0.0f;
    float rms_ = 0.0f;
    float correlation_ = 1.0f;
};

/// Tranche d'une piste.
class ChannelStrip : public juce::Component {
public:
    /// `sendNames` donne un bouton par bus DÉCLARÉ par le projet, dans son
    /// ordre. Deux boutons figés promettaient deux départs qui n'étaient écrits
    /// nulle part et dont rien ne disait le contenu ; un bouton par bus nommé
    /// dit ce qu'on alimente.
    /// Pour un bus de groupe : les pistes routées vers lui, dites en infobulle.
    void setMembers(const juce::StringArray& membres);
    ChannelStrip(vsm::sequencer::Track& track, size_t index,
                  const std::vector<std::string>& sendNames);
    void resized() override;
    void paint(juce::Graphics&) override;
    void setMeasurement(const vsm::audio::engine::TrackMeasurement& m) {
        meter_.setLevel(m.peak);
        meter_.setRms(m.rms);
        meter_.setCorrelation(m.correlation);
    }

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

    void setMeters(double lufs, float linearPeak, float linearRms, float correlation) {
        meter_.setLevel(linearPeak);
        meter_.setRms(linearRms);
        meter_.setCorrelation(correlation);
        lufsLabel_.setText(lufs <= vsm::audio::dsp::LufsMeter::kSilence + 1.0
                               ? juce::String("-inf LUFS")
                               : juce::String(lufs, 1) + " LUFS",
                           juce::dontSendNotification);
        // LE CHIFFRE DE LA CORRÉLATION EN CLAIR, à côté de l'aiguille : une
        // bande colorée dit qu'il y a un problème, elle ne dit pas s'il est de
        // -0,1 ou de -0,9, et c'est ce qui décide si on va chercher.
        phaseLabel_.setText("phase " + juce::String(correlation, 2), juce::dontSendNotification);
        phaseLabel_.setColour(juce::Label::textColourId,
                               correlation < 0.0f ? vsm::ui::Palette::accentRed
                                                  : vsm::ui::Palette::textSecondary);
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
    juce::Label titleLabel_, lufsLabel_, phaseLabel_;
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
    /// Rafraîchit tous les mètres. `trackMeasure` rend les trois mesures d'une
    /// piste (D4.7) : la crête seule ne disait pas si c'était fort, et rien ne
    /// disait ce qu'il resterait du mixage en mono.
    void updateMeters(const std::function<vsm::audio::engine::TrackMeasurement(size_t)>& trackMeasure,
                      double masterLufs, float masterPeak, float masterRms,
                      float masterCorrelation);

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
