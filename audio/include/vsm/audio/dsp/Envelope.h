#pragma once
#include <algorithm>

namespace vsm::audio::dsp {

struct AdsrSettings {
    float attackSeconds = 0.01f;
    float decaySeconds = 0.1f;
    float sustainLevel = 0.7f; // 0..1
    float releaseSeconds = 0.2f;
};

enum class EnvelopeStage { Idle, Attack, Decay, Sustain, Release };

/// Enveloppe ADSR à segments linéaires (l'anti-aliasing de segments
/// exponentiels/courbes est un raffinement Phase 6 -- voir ARCHITECTURE.md).
/// Point important : noteOn() ne réinitialise JAMAIS le niveau à zéro --
/// l'attaque redémarre depuis le niveau courant, ce qui évite un "click"
/// si une nouvelle note arrive pendant un release (voice stealing / retrigger
/// rapide, cas fréquent en musique électronique).
class AdsrEnvelope {
public:
    void setSampleRate(double sampleRate) { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
    void setSettings(const AdsrSettings& s) { settings_ = s; }
    const AdsrSettings& settings() const { return settings_; }

    void noteOn() { stage_ = EnvelopeStage::Attack; }

    void noteOff() {
        if (stage_ != EnvelopeStage::Idle) {
            stage_ = EnvelopeStage::Release;
            releaseStartLevel_ = level_;
        }
    }

    bool isActive() const { return stage_ != EnvelopeStage::Idle; }
    EnvelopeStage stage() const { return stage_; }
    float currentLevel() const { return level_; }

    /// Avance l'état d'un échantillon, renvoie le niveau courant [0..1].
    float nextSample() {
        switch (stage_) {
            case EnvelopeStage::Idle:
                level_ = 0.0f;
                break;

            case EnvelopeStage::Attack: {
                float samples = std::max(1.0f, settings_.attackSeconds * static_cast<float>(sampleRate_));
                level_ += 1.0f / samples;
                if (level_ >= 1.0f) {
                    level_ = 1.0f;
                    stage_ = EnvelopeStage::Decay;
                }
                break;
            }

            case EnvelopeStage::Decay: {
                float samples = std::max(1.0f, settings_.decaySeconds * static_cast<float>(sampleRate_));
                float delta = (1.0f - settings_.sustainLevel) / samples;
                level_ -= delta;
                if (level_ <= settings_.sustainLevel) {
                    level_ = settings_.sustainLevel;
                    stage_ = EnvelopeStage::Sustain;
                }
                break;
            }

            case EnvelopeStage::Sustain:
                level_ = settings_.sustainLevel;
                break;

            case EnvelopeStage::Release: {
                float samples = std::max(1.0f, settings_.releaseSeconds * static_cast<float>(sampleRate_));
                float delta = releaseStartLevel_ / samples;
                level_ -= delta;
                if (level_ <= 0.0f) {
                    level_ = 0.0f;
                    stage_ = EnvelopeStage::Idle;
                }
                break;
            }
        }
        return level_;
    }

private:
    double sampleRate_ = 48000.0;
    AdsrSettings settings_;
    EnvelopeStage stage_ = EnvelopeStage::Idle;
    float level_ = 0.0f;
    float releaseStartLevel_ = 0.0f;
};

} // namespace vsm::audio::dsp
