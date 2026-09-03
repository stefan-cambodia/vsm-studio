#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/StringWaveguide.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::hurdygurdy {

/// LA VIELLE À ROUE — l'instrument où la vélocité ne fait pas la force mais le
/// RYTHME, et où les bourdons sonnent tant que la roue tourne.
///
/// POURQUOI CETTE MACHINE. Une roue enduite de colophane, tournée à la
/// manivelle, frotte toutes les cordes à la fois : c'est un archet qui ne
/// finit jamais. Les touches du clavier posent des sautereaux sur la
/// CHANTERELLE, qui donne la mélodie ; les BOURDONS, eux, n'ont pas de clavier
/// et sonnent tant que la roue tourne, quelle que soit la note. Et la
/// TROMPETTE porte un chevalet mobile, le CHIEN, qui bourdonne quand la roue
/// accélère : c'est le coup de poignet du vielleux qui fait le rythme, pas la
/// force sur les touches -- les touches n'ont aucune force à donner.
///
/// ```
///   manivelle ──> ROUE (colophane) ──┬── CHANTERELLE : la touche la raccourcit  ──> mélodie
///                                    ├── BOURDONS : toujours frottés          ──> tenue
///                                    └── TROMPETTE + CHIEN : le chevalet claque
///                                        au coup de poignet (la vélocité)     ──> rythme
/// ```
///
/// CE QUE LE PARC AVAIT, ET CE QUI LUI MANQUAIT. `vsm.string` frotte une corde
/// à l'archet (friction adhérence-glissement), et cette friction est
/// réemployée telle quelle. Mais aucune machine n'avait de bourdons qui
/// sonnent SANS note, ni d'inertie -- la roue continue de tourner un instant
/// après la dernière touche --, ni de chevalet qui claque. Ici la vélocité
/// d'une note ne change RIEN au niveau de la chanterelle (la roue ne pousse
/// pas plus fort parce qu'on appuie sur une touche) : elle est le coup de
/// poignet, et déclenche le chien.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : la roue est un archet à
/// vitesse constante (la vraie roue a des irrégularités qui font le grain de
/// l'instrument) ; le chien est une modulation d'amplitude de la trompette,
/// pas un contact mécanique simulé ; deux bourdons (grave à l'octave sous la
/// tonique, mouche à la quinte) là où les vielles en ont deux à quatre ; le
/// coffre n'est pas modélisé.
class WheelString {
public:
    void prepare(double sampleRate, float lowestHz) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        corde_.prepare(sampleRate_, lowestHz);
        corde_.reset();
        dcX1_ = dcY1_ = 0.0f;
    }
    void reset() { corde_.reset(); dcX1_ = dcY1_ = 0.0f; }
    void setTuning(float hz, float damping, float t60) { corde_.setTuning(hz, damping, 0.01f, t60); }

    /// Un échantillon : la roue frotte avec la force `gain` (0 = levée).
    float render(float gain, float wheelSpeed, float wheelPressure) {
        const float looped = corde_.advance();
        float drive = 0.0f;
        if (gain > 0.0f) {
            const float vitesse = 0.03f + 0.42f * std::clamp(wheelSpeed, 0.0f, 1.0f);
            const float pente = 5.0f - 4.4f * std::clamp(wheelPressure, 0.0f, 1.0f);
            const float relative = vitesse - looped;
            drive = gain * friction(relative, pente) * relative;
        }
        const float value = corde_.inject(looped, drive, corde_.contactOffset(0.12f));
        const float out = value - dcX1_ + 0.9995f * dcY1_;
        dcX1_ = value;
        dcY1_ = out;
        return out;
    }

private:
    /// La même table que l'archet de `vsm.string` : adhérence totale près de
    /// la vitesse nulle, puis décrochement -- le cycle de Helmholtz.
    static float friction(float relativeVelocity, float slope) {
        const float s = (relativeVelocity + 0.001f) * slope;
        const float r = std::pow(std::abs(s) + 0.75f, -4.0f);
        return r < 1.0f ? r : 1.0f;
    }
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::StringWaveguide corde_;
    float dcX1_ = 0.0f, dcY1_ = 0.0f;
};

class HurdyGurdyVoice {
public:
    struct Params {
        float wheelSpeed = 0.5f;
        float wheelPressure = 0.55f;
        float damping = 0.2f;
    };

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        chanterelle_.prepare(sampleRate_, 60.0f);
        niveau_ = 0.0f;
    }
    bool isActive() const { return enfoncee_ || niveau_ > 1e-5f; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    /// LA VÉLOCITÉ NE FAIT PAS LA FORCE : elle est retenue comme coup de
    /// poignet, que la machine lit pour le chien. La chanterelle, elle, est
    /// frottée par la roue à la même force quelle que soit la touche.
    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        coup_ = static_cast<float>(velocity) / 127.0f;
        chanterelle_.reset();
        enfoncee_ = true;
        niveau_ = 1.0f;
        gain_ = 0.0f;
    }
    void noteOff(uint8_t) { enfoncee_ = false; }
    float coupDePoignet() const { return coup_; }

    float render(const Params& p, float roue) {
        if (!isActive()) return 0.0f;
        const float hz = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
        // Touche levée : le sautereau quitte la corde, qui meurt vite -- la
        // roue continue, mais elle ne frotte plus CETTE longueur de corde.
        chanterelle_.setTuning(hz, p.damping, enfoncee_ ? 4.0f : 0.15f);
        const float cible = enfoncee_ ? roue : 0.0f;
        gain_ += (cible - gain_) * 0.002f;   // la roue prend la corde en douceur
        const float x = chanterelle_.render(gain_, p.wheelSpeed, p.wheelPressure);
        const float absolu = std::abs(x);
        niveau_ = absolu > niveau_ ? absolu : niveau_ + (absolu - niveau_) * 0.0002f;
        return x;
    }

private:
    double sampleRate_ = 48000.0;
    WheelString chanterelle_;
    float niveau_ = 0.0f, gain_ = 0.0f, coup_ = 0.0f;
    bool enfoncee_ = false;
    uint8_t note_ = 60, channel_ = 0;
};

class HurdyGurdySynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 6;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kDrones = 1, kDroneNote, kWheelSpeed, kWheelPressure, kWheelInertia,
        kChien, kChienBuzz, kDamping, kCutoff, kOutputLevel,
    };

    HurdyGurdySynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Hurdy-Gurdy (la vielle à roue)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<HurdyGurdyVoice, kMaxVoices> voiceManager_;
    WheelString bourdonGrave_, mouche_, trompette_;
    vsm::audio::dsp::StateVariableFilter filtre_;
    vsm::util::DeterministicRng rng_{0x56494C45ULL};   // "VILE"

    // LA ROUE : sa vitesse monte quand une touche est enfoncée et redescend
    // avec son inertie quand la dernière est lâchée. Les bourdons la suivent.
    float roue_ = 0.0f;
    int touchesEnfoncees_ = 0;
    // LE CHIEN : un claquement du chevalet, déclenché par le coup de poignet
    // (la vélocité de la note), qui module la trompette pendant un instant.
    int chienRestant_ = 0;
    float chienForce_ = 0.0f;
    float chienPhase_ = 0.0f;
};

} // namespace vsm::plugins::hurdygurdy
