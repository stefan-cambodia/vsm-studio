#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DecayEnvelope.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace vsm::plugins::dx7 {

inline constexpr int kNumOperators = 6;

/// Un opérateur FM : un oscillateur sinus à accumulateur de phase acceptant
/// une entrée de MODULATION DE PHASE (le coeur de la synthèse FM/PM Yamaha),
/// avec sa propre enveloppe de niveau. Volontairement NON band-limited : à
/// index de modulation modéré une sinusoïde FM ne replie quasiment pas ; aux
/// forts index/feedback un léger aliasing peut apparaître, à traiter par
/// oversampling en Phase 6 (section 5, documenté). Spécifique au DX7 -> gardé
/// dans le dossier du plugin (guide §4), pas dans dsp/.
class FmOperator {
public:
    void setSampleRate(double sr) { sampleRate_ = sr > 0.0 ? sr : 48000.0; env_.setSampleRate(sampleRate_); }
    void reset() { phase_ = 0.0; last_ = 0.0f; prev_ = 0.0f; }

    void setRatio(float ratio) { ratio_ = ratio; }
    void setFixedHz(float hz) { fixedHz_ = hz; }
    void setFixedMode(bool fixed) { fixedMode_ = fixed; }
    void setLevel(float level) { level_ = level; }
    void setSettings(const vsm::audio::dsp::AdsrSettings& s) { env_.setSettings(s); }

    void noteOn() { env_.noteOn(); }
    void noteOff() { env_.noteOff(); }
    bool envActive() const { return env_.isActive(); }

    /// Calcule un échantillon. `baseHz` = fréquence de la note ; `phaseMod` =
    /// modulation de phase (en radians) issue des modulateurs. `envLevel` est
    /// avancé ici (un appel par échantillon et par opérateur).
    float render(float baseHz, float phaseMod) {
        const float hz = fixedMode_ ? fixedHz_ : baseHz * ratio_;
        const double inc = static_cast<double>(hz) / sampleRate_;
        const float e = env_.nextSample();
        const float out = std::sin(phase_ * vsm::audio::dsp::kTwoPi + static_cast<double>(phaseMod)) * e * level_;
        phase_ += inc;
        if (phase_ >= 1.0) phase_ -= 1.0;
        prev_ = last_;
        last_ = out;
        return out;
    }

    /// Moyenne des deux dernières sorties : lissage du chemin de feedback,
    /// comme sur le DX7 (évite un feedback trop bruyant/instable).
    float feedbackAverage() const { return 0.5f * (last_ + prev_); }

private:
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    float ratio_ = 1.0f, fixedHz_ = 440.0f, level_ = 1.0f;
    bool fixedMode_ = false;
    float last_ = 0.0f, prev_ = 0.0f;
    vsm::audio::dsp::AdsrEnvelope env_;
};

/// Routage d'un algorithme : qui module qui, qui est porteuse, et quel
/// opérateur porte le feedback. `modMask[i]` = bits des opérateurs qui
/// modulent l'opérateur i (indices 0..5). `carriers` = bits des opérateurs
/// sommés vers la sortie. `feedbackOp` = index de l'opérateur en auto-feedback.
struct Algorithm {
    std::array<uint8_t, kNumOperators> modMask;
    uint8_t carriers;
    int feedbackOp;
};

/// Le DX7 possède 32 algorithmes. On en implémente ici HUIT, spécifiés
/// exactement et couvrant les topologies typiques (piles simples, branches,
/// additif). Le moteur (matrice de modulation généraliste) accepte n'importe
/// quel routage 6 opérateurs : compléter les 32 se fait en ajoutant des
/// lignes à cette table, sans toucher au DSP (section 27 : on ne prétend pas
/// reproduire les 32 sans les avoir spécifiés ; on documente le sous-ensemble).
inline const std::array<Algorithm, 8>& algorithmTable() {
    auto bit = [](std::initializer_list<int> ops) -> uint8_t {
        uint8_t m = 0; for (int o : ops) m |= static_cast<uint8_t>(1u << o); return m;
    };
    static const std::array<Algorithm, 8> table = {{
        // 1 : deux piles de 3. Porteuses op1(0) et op4(3).
        { { bit({1}), bit({2}), 0, bit({4}), bit({5}), 0 }, bit({0, 3}), 5 },
        // 2 : trois piles de 2. Porteuses op1, op3, op5.
        { { bit({1}), 0, bit({3}), 0, bit({5}), 0 }, bit({0, 2, 4}), 5 },
        // 3 : une pile de 6. Porteuse op1 uniquement.
        { { bit({1}), bit({2}), bit({3}), bit({4}), bit({5}), 0 }, bit({0}), 5 },
        // 4 : une porteuse modulée par deux modulateurs empilés en parallèle.
        { { bit({1, 2}), bit({3}), bit({4, 5}), 0, 0, 0 }, bit({0}), 5 },
        // 5 : branche en Y (op4 modulé par op5+op6, remonte vers op2->op1).
        { { bit({1}), bit({2, 3}), 0, bit({4, 5}), 0, 0 }, bit({0}), 5 },
        // 6 : pile de 4 + pile de 2. Porteuses op1 et op5.
        { { bit({1}), bit({2}), bit({3}), 0, bit({5}), 0 }, bit({0, 4}), 5 },
        // 7 : trois porteuses partageant un étage de modulateurs.
        { { bit({3}), bit({4}), bit({5}), 0, 0, 0 }, bit({0, 1, 2}), 5 },
        // 8 : additif pur, les six opérateurs sont porteuses (feedback op6).
        { { 0, 0, 0, 0, 0, 0 }, bit({0, 1, 2, 3, 4, 5}), 5 },
    }};
    return table;
}

/// Une voix DX7-style : 6 opérateurs FM routés selon un algorithme, avec
/// feedback sur un opérateur et sensibilité à la vélocité (qui augmente la
/// profondeur de modulation ET le niveau -> plus la frappe est forte, plus le
/// timbre est brillant, comportement FM typique).
class DX7Voice {
public:
    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate;
        for (auto& op : ops_) op.setSampleRate(sampleRate);
        pitchEnv_.setSampleRate(sampleRate);
        drift_.setSampleRate(sampleRate); drift_.setSeed(seed); drift_.setRateHz(0.12f);
    }

    // Contrat VoiceManager
    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel; note_ = note; velocity_ = velocity;
        // Vélocité (voir plus bas) ET keyboard level scaling (§11) sont
        // appliqués ICI, au déclenchement : le scaling atténue (ou renforce)
        // le niveau des opérateurs selon la hauteur jouée, comme sur le DX7
        // (typiquement, on adoucit les aigus pour éviter une FM criarde).
        const float velNorm = static_cast<float>(velocity_) / 127.0f;
        velFactor_ = 1.0f - velocitySens_ * (1.0f - velNorm);
        const float keyScale = std::clamp(
            1.0f - keyLevelScaling_ * (static_cast<float>(note_) - 60.0f) / 48.0f, 0.05f, 2.0f);
        for (int i = 0; i < kNumOperators; ++i) {
            const size_t s = static_cast<size_t>(i);
            ops_[s].setLevel(baseLevels_[s] * velFactor_ * keyScale);
            ops_[s].reset();
            ops_[s].noteOn();
        }
        pitchEnv_.trigger(); // enveloppe de pitch globale (§11)
    }
    void noteOff(uint8_t /*velocity*/) { for (auto& op : ops_) op.noteOff(); }
    bool isActive() const {
        // Actif tant qu'AU MOINS une porteuse a une enveloppe active.
        for (int i = 0; i < kNumOperators; ++i)
            if ((carriers_ & (1u << i)) && ops_[static_cast<size_t>(i)].envActive()) return true;
        return false;
    }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void configure(const Algorithm& algo, float feedbackAmount, float velocitySens,
                   float pitchEnvAmountSemis, float pitchEnvTimeSeconds, float keyLevelScaling,
                   const std::array<float, kNumOperators>& levels,
                   const std::array<float, kNumOperators>& ratios,
                   const std::array<bool, kNumOperators>& fixedModes,
                   const std::array<float, kNumOperators>& fixedHz,
                   const std::array<vsm::audio::dsp::AdsrSettings, kNumOperators>& envs) {
        algo_ = &algo;
        carriers_ = algo.carriers;
        feedbackAmount_ = feedbackAmount;
        velocitySens_ = velocitySens;
        pitchEnvAmount_ = pitchEnvAmountSemis;
        keyLevelScaling_ = keyLevelScaling;
        pitchEnv_.setDecaySeconds(pitchEnvTimeSeconds);
        for (int i = 0; i < kNumOperators; ++i) {
            const size_t s = static_cast<size_t>(i);
            baseLevels_[s] = levels[s];
            ops_[s].setLevel(baseLevels_[s] * velFactor_); // velFactor/keyScale recalculés au noteOn
            ops_[s].setRatio(ratios[s]);
            ops_[s].setFixedMode(fixedModes[s]);
            ops_[s].setFixedHz(fixedHz[s]);
            ops_[s].setSettings(envs[s]);
        }
    }
    void setDriftAmount(float a) { drift_.setAmount(a); }

    float render(float lfoPitchSemis) {
        using namespace vsm::audio::dsp;
        if (!isActive() || algo_ == nullptr) return 0.0f;

        const float driftSemis = drift_.nextValue() * kDriftSemis;
        const float pitchEnvSemis = pitchEnvAmount_ * pitchEnv_.next(); // enveloppe de pitch (§11)
        const float baseHz = 440.0f * std::exp2f(
            (static_cast<float>(note_) + driftSemis + pitchEnvSemis + lfoPitchSemis - 69.0f) / 12.0f);

        std::array<float, kNumOperators> out{};
        // Ordre 6 -> 1 (indices 5 -> 0) : dans les algorithmes retenus les
        // modulateurs ont toujours un indice supérieur à leur porteuse, donc
        // leur sortie de CET échantillon est déjà calculée quand on les lit.
        for (int i = kNumOperators - 1; i >= 0; --i) {
            const size_t s = static_cast<size_t>(i);
            float phaseMod = 0.0f;
            const uint8_t mask = algo_->modMask[s];
            for (int j = 0; j < kNumOperators; ++j)
                if (mask & (1u << j)) phaseMod += out[static_cast<size_t>(j)];

            if (i == algo_->feedbackOp)
                phaseMod += ops_[s].feedbackAverage() * feedbackAmount_ * kFeedbackScale;

            out[s] = ops_[s].render(baseHz, phaseMod * kModIndexScale);
        }

        float sum = 0.0f;
        int carrierCount = 0;
        for (int i = 0; i < kNumOperators; ++i)
            if (carriers_ & (1u << i)) { sum += out[static_cast<size_t>(i)]; ++carrierCount; }
        if (carrierCount > 0) sum /= static_cast<float>(carrierCount);
        return sum;
    }

private:
    static constexpr float kDriftSemis = 0.04f;
    static constexpr float kModIndexScale = 6.0f;   // profondeur FM (radians par unité)
    static constexpr float kFeedbackScale = 3.0f;

    double sampleRate_ = 48000.0;
    std::array<FmOperator, kNumOperators> ops_;
    vsm::audio::dsp::DecayEnvelope pitchEnv_;
    vsm::audio::dsp::AnalogDrift drift_;
    const Algorithm* algo_ = nullptr;
    uint8_t carriers_ = 0;
    float feedbackAmount_ = 0.0f, velFactor_ = 1.0f, velocitySens_ = 0.0f;
    float pitchEnvAmount_ = 0.0f, keyLevelScaling_ = 0.0f;
    std::array<float, kNumOperators> baseLevels_{};
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

// --- Paramètres : 9 globaux + 7 par opérateur x 6 opérateurs = 51 ----------
inline constexpr vsm::audio::plugin::ParamId kAlgorithm = 0;
inline constexpr vsm::audio::plugin::ParamId kFeedback = 1;
inline constexpr vsm::audio::plugin::ParamId kLfoRate = 2;
inline constexpr vsm::audio::plugin::ParamId kLfoToPitch = 3;
inline constexpr vsm::audio::plugin::ParamId kVelocitySens = 4;
inline constexpr vsm::audio::plugin::ParamId kPitchEnvAmount = 5;  // §11 : enveloppe de pitch (demi-tons)
inline constexpr vsm::audio::plugin::ParamId kPitchEnvTime = 6;    // §11 : durée de décroissance
inline constexpr vsm::audio::plugin::ParamId kKeyLevelScaling = 7; // §11 : keyboard level scaling
inline constexpr vsm::audio::plugin::ParamId kAnalogCharacter = 8;
inline constexpr vsm::audio::plugin::ParamId kOpParamBase = 9;
inline constexpr int kParamsPerOp = 7; // ratio, level, attack, decay, sustain, release, fixed

enum OpField { kOpRatio = 0, kOpLevel, kOpAttack, kOpDecay, kOpSustain, kOpRelease, kOpFixed };

inline vsm::audio::plugin::ParamId opParam(int op, OpField field) {
    return kOpParamBase + static_cast<vsm::audio::plugin::ParamId>(op * kParamsPerOp + field);
}
inline constexpr vsm::audio::plugin::ParamId kNumParams =
    kOpParamBase + static_cast<vsm::audio::plugin::ParamId>(kNumOperators * kParamsPerOp);

/// DX7-style : synthèse FM 6 opérateurs, polyphonique, avec algorithmes,
/// feedback, enveloppes par opérateur, sensibilité à la vélocité et LFO.
///
/// Approximations assumées (section 27) : enveloppes par opérateur modélisées
/// par des ADSR linéaires (le DX7 utilise des EG rate/level à 4 étages ;
/// même rôle, forme différente). 8 des 32 algorithmes implémentés (voir
/// algorithmTable()). Opérateurs non band-limited (aliasing aux forts index
/// -> oversampling Phase 6). Import de presets SysEx DX7 NON inclus (risque
/// de propriété intellectuelle sur les banques commerciales, section 28) :
/// pourra être ajouté séparément, sur fichiers fournis par l'utilisateur.
/// Aucune mesure comparative avec un DX7 matériel n'a été faite.
class DX7Synth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    DX7Synth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "DX7-style FM Synthesis"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& ev);

    vsm::audio::engine::VoiceManager<DX7Voice, kMaxVoices> voiceManager_;
    double lfoPhase_ = 0.0, lfoIncrement_ = 0.0;

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::dx7
