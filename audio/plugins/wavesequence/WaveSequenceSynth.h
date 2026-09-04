#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/WaveTable.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::wavesequence {

/// LE SÉQUENÇAGE D'ONDES — le seul synthé où le timbre est une SÉQUENCE
/// (esprit Wavestation).
///
/// POURQUOI CETTE MACHINE, ET POURQUOI ELLE N'EST NI `vsm.wavetable` NI
/// `vsm.vector`. La table d'ondes se BALAIE (une enveloppe, un LFO : un
/// mouvement continu) ; le vecteur se PROMÈNE (un point, une orbite). Ici le
/// timbre est une LISTE de huit pas — une forme d'onde et une durée chacun —
/// joués l'un après l'autre, avec un fondu entre deux, en boucle depuis un
/// point de retour, remis au premier pas à chaque note ou laissés courir sur
/// une horloge commune. Ni un balayage ni une orbite ne SAUTENT d'un spectre
/// à un autre à des instants réglés ; c'est le trait, et c'est ce qui fait
/// les nappes « par paliers » et les textures rythmiques des années 1990.
///
/// ```
///   horloge (pas de `stepTime`) ──> pas courant k, fondu vers k+1
///   forme(k), forme(k+1) : (table, position) dans la banque partagée
///                    └──> oscillateur (une phase, deux lectures mêlées) ──> VCF ──> ADSR ──> sortie
/// ```
///
/// LA BANQUE EST CELLE DE `vsm.wavetable` (`WaveTableBank::shared()`) : le
/// matériau est partagé, le geste ne l'est pas. Un pas est un nombre de 0 à
/// 4 : la partie entière choisit la table, la fraction la position dedans.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : huit pas de même durée
/// (le Wavestation les règle un par un, en durée et en niveau) ; un seul
/// oscillateur par voix (l'original mêle jusqu'à quatre séquences) ; le fondu
/// est linéaire ; pas de synchronisation au tempo -- la machine n'en connaît
/// pas, et la durée d'un pas se règle en millisecondes.
class WaveSequenceVoice {
public:
    static constexpr int kSteps = 8;

    struct Params {
        std::array<float, kSteps> waves{};   // 0..4 : table + position
        float stepSeconds = 0.2f;
        float crossfade = 0.3f;              // part du pas passée en fondu vers le suivant
        int loopStart = 0;                   // 0..7, premier pas de la boucle
        bool keyRestart = true;
        float cutoffHz = 6000.0f;
        float resonance = 0.2f;
        vsm::audio::dsp::AdsrSettings adsr;
        float velocitySensitivity = 0.6f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        env_.setSampleRate(sampleRate_);
        filtre_.setSampleRate(sampleRate_);
        filtre_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::LowPass);
        active_ = false;
    }
    bool isActive() const { return active_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        phase_ = 0.0f;
        env_.noteOn();
        active_ = true;
    }
    /// La position de l'horloge COMMUNE au moment de la note, en échantillons :
    /// c'est elle qui décide du pas de départ (remise à la note : le temps
    /// compte depuis elle ; course libre : elle ne sert pas).
    void setDepart(int64_t horloge) { depart_ = horloge; }
    void noteOff(uint8_t) { env_.noteOff(); }

    /// `horloge` : l'horloge commune, en échantillons.
    float render(const vsm::audio::dsp::WaveTableBank& bank, const Params& p, int64_t horloge) {
        if (!active_) return 0.0f;
        env_.setSettings(p.adsr);

        // LE PAS COURANT. Remise à la note : le temps compte depuis la note ;
        // course libre : depuis l'horloge commune, comme pour toutes les voix.
        const double pasEch = std::max(1.0, static_cast<double>(p.stepSeconds) * sampleRate_);
        const auto ecoule = static_cast<double>(p.keyRestart ? (horloge - depart_) : horloge);
        const double positionPas = ecoule / pasEch;
        const auto passes = static_cast<int64_t>(std::floor(positionPas));
        const int boucle = std::clamp(p.loopStart, 0, kSteps - 1);
        const int longueurBoucle = kSteps - boucle;
        int k = 0;
        if (passes < kSteps) k = static_cast<int>(passes);
        else k = boucle + static_cast<int>((passes - kSteps) % longueurBoucle);
        int suivant = k + 1;
        if (suivant >= kSteps) suivant = boucle;
        const auto fraction = static_cast<float>(positionPas - std::floor(positionPas));
        // LE FONDU : la fin du pas glisse vers le suivant.
        const float debutFondu = 1.0f - std::clamp(p.crossfade, 0.0f, 1.0f);
        const float melange = fraction <= debutFondu || p.crossfade <= 0.0f
                                  ? 0.0f
                                  : (fraction - debutFondu) / std::max(1e-6f, 1.0f - debutFondu);

        const float a = lire(bank, p.waves[static_cast<size_t>(k)]);
        const float b = melange > 0.0f ? lire(bank, p.waves[static_cast<size_t>(suivant)]) : 0.0f;
        avancer();
        const float onde = a * (1.0f - melange) + b * melange;

        filtre_.setCutoffHz(std::clamp(p.cutoffHz, 20.0f, 18000.0f));
        filtre_.setResonance(p.resonance);
        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float force = 1.0f - p.velocitySensitivity * (1.0f - velocity);
        const float e = env_.nextSample();
        if (!env_.isActive()) active_ = false;
        return filtre_.process(onde) * e * force;
    }

private:
    /// Lit la banque au pas `wave` (0..4), à la phase courante.
    float lire(const vsm::audio::dsp::WaveTableBank& bank, float wave) {
        const float w = std::clamp(wave, 0.0f, 3.999f);
        const auto table = static_cast<size_t>(w);
        const float position = w - static_cast<float>(table);
        return bank.read(std::min(table, bank.tableCount() - 1), position, phase_, limite());
    }
    float limite() const {
        const float hz = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
        return 0.9f * static_cast<float>(sampleRate_) * 0.5f / hz;
    }
    void avancer() {
        const float hz = 440.0f * std::exp2f((static_cast<float>(note_) + bendCourant_ - 69.0f) / 12.0f);
        phase_ += hz / static_cast<float>(sampleRate_);
        if (phase_ >= 1.0f) phase_ -= 1.0f;
    }

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::StateVariableFilter filtre_;
    float phase_ = 0.0f, bendCourant_ = 0.0f;
    int64_t depart_ = 0;
    bool active_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;

public:
    void setBend(float semitones) { bendCourant_ = semitones; }
};

class WaveSequenceSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 16;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kStep1 = 1, kStep2, kStep3, kStep4, kStep5, kStep6, kStep7, kStep8,
        kStepTime, kCrossfade, kLoopStart, kKeyRestart,
        kCutoff, kResonance, kAttack, kDecay, kSustain, kRelease,
        kVelocitySensitivity, kOutputLevel,
    };

    WaveSequenceSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Wave Sequence (le timbre est une séquence)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<WaveSequenceVoice, kMaxVoices> voiceManager_;
    std::atomic<float> bend_{0.0f};
    /// L'HORLOGE COMMUNE, en échantillons depuis `initialize` : c'est elle qui
    /// place les notes en course libre.
    int64_t horloge_ = 0;
};

} // namespace vsm::plugins::wavesequence
