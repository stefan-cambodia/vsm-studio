#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Chorus.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/dsp/LadderFilterZDFx4.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::jupiter8 {

enum ParamIds : vsm::audio::plugin::ParamId {
    kVco1Level = 0,
    kVco1Shape,        // 0 = saw, 1 = pulse, 2 = triangle
    kVco1PulseWidth,
    kVco2Level,
    kVco2Shape,        // 0 = saw, 1 = pulse, 2 = triangle
    kVco2PulseWidth,
    kVco2Detune,       // demi-tons
    kCrossMod,         // VCO-2 -> fréquence VCO-1 (FM audio)
    kSync,             // 0/1 : hard-sync VCO-2 -> VCO-1
    kHpfCutoff,        // passe-haut non résonant en amont du VCF (spécificité Jupiter)
    kFilterCutoff,
    kFilterResonance,
    kFilterEnvAmount,  // ENV-1 -> coupure
    kFilterKeyTrack,
    kEnv1Attack,       // ENV-1 : enveloppe du filtre
    kEnv1Decay,
    kEnv1Sustain,
    kEnv1Release,
    kEnv2Attack,       // ENV-2 : enveloppe d'amplitude
    kEnv2Decay,
    kEnv2Sustain,
    kEnv2Release,
    kLfoRate,
    kLfoToPitch,
    kLfoToFilter,
    kLfoToPwm,
    kChorusMode,       // 0 = off, 1 = mode I, 2 = mode II (plus profond/rapide)
    kAnalogCharacter,
    kNumParams
};

/// Paramètres partagés lus une fois par bloc et passés à chaque voix.
struct Jupiter8Params {
    float vco1Level, vco1Pw; int vco1Shape;
    float vco2Level, vco2Pw, vco2Detune; int vco2Shape;
    float crossMod; bool sync;
    float hpfCutoff;
    float cutoffBase, resonance, filterEnvAmount, keyTrack;
    float lfoToPitch, lfoToFilter, lfoToPwm;
    float analogCharacter;
    // Molette de hauteur, en OCTAVES (les exposants des VCO se somment en
    // octaves). À zéro l'addition est exacte : empreinte inchangée au bit.
    float bendOctaves = 0.0f;
};

/// Une voix Jupiter-8-style : DEUX VCO (saw/pulse/tri) avec désaccord, un
/// étage de cross-modulation (VCO-2 module en fréquence le VCO-1) et un
/// hard-sync optionnel, suivis d'un passe-haut NON résonant (caractéristique
/// du Jupiter, absent du Juno) puis d'un VCF passe-bas 4 pôles résonant.
/// Deux enveloppes ADSR indépendantes comme le hardware : ENV-1 pour le
/// filtre, ENV-2 pour l'amplitude.
///
/// Le clavier du Jupiter-8 n'est PAS sensible à la vélocité -> le VCA ne
/// dépend que de ENV-2 (documenté, testé).
///
/// Approximations assumées (section 27) : le VCF réel (IR3109 OTA 4 pôles)
/// est modélisé par le ladder ZDF déjà présent (même pente 24 dB/oct et
/// auto-oscillation, topologie OTA vs ladder différente). Le hard-sync se
/// fait par remise à zéro de la phase du VCO-1 au cycle du VCO-2, sans
/// correction BLEP du point de sync (léger repliement, raffinement Phase 6).
/// La cross-modulation est une FM linéaire simplifiée. Le chorus stéréo est
/// appliqué au niveau du synthé (voir Jupiter8Synth), pas par voix. Aucune
/// mesure comparative avec un Jupiter-8 matériel n'a été faite.
class Jupiter8Voice {
public:
    void prepare(double sampleRate, uint64_t seed) {
        using namespace vsm::audio::dsp;
        sampleRate_ = sampleRate;
        vco1_.setSampleRate(sampleRate);
        vco2_.setSampleRate(sampleRate);
        hpf_.setSampleRate(sampleRate);
        hpf_.setMode(StateVariableFilter::Mode::HighPass);
        hpf_.setResonance(0.707f); // Butterworth, non résonant
        // Le VCF (4 pôles, drive 1.0) n'appartient plus à la voix : il est
        // partagé par groupe de quatre voix et configuré par le synthé.
        ampEnv_.setSampleRate(sampleRate);
        filterEnv_.setSampleRate(sampleRate);
        drift1_.setSampleRate(sampleRate);  drift1_.setSeed(seed);                 drift1_.setRateHz(0.13f);
        drift2_.setSampleRate(sampleRate);  drift2_.setSeed(seed ^ 0xA11CEULL);    drift2_.setRateHz(0.11f);
        cutoffDrift_.setSampleRate(sampleRate); cutoffDrift_.setSeed(seed ^ 0x5555ULL); cutoffDrift_.setRateHz(0.09f);
    }

    // Contrat VoiceManager
    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel; note_ = note; velocity_ = velocity;
        baseHz_ = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
        ampEnv_.noteOn(); filterEnv_.noteOn();
        vco1_.reset(0.0); vco2_.reset(0.0); syncPhase_ = 0.0f;
    }
    void noteOff(uint8_t /*velocity*/) { ampEnv_.noteOff(); filterEnv_.noteOff(); }
    bool isActive() const { return ampEnv_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setSettings(const vsm::audio::dsp::AdsrSettings& amp,
                     const vsm::audio::dsp::AdsrSettings& filt) {
        ampEnv_.setSettings(amp); filterEnv_.setSettings(filt);
    }
    void setDriftAmount(float a) { drift1_.setAmount(a); drift2_.setAmount(a); cutoffDrift_.setAmount(a); }

    // ------------------------------------------------------------------
    // Rendu en DEUX temps, et pas par goût de la complication : les huit voix
    // partagent deux filtres vectorisés (LadderFilterZDFx4, quatre voix par
    // filtre), donc le VCF ne peut plus être appelé au milieu du rendu d'une
    // voix isolée. La voix produit d'abord son signal jusqu'à l'entrée du VCF
    // (renderPreFilter), le synthé filtre quatre voix d'un coup, puis chaque
    // voix termine son propre VCA (applyFilterOutput).
    //
    // Le découpage ne change RIEN au calcul : mêmes opérations, même ordre,
    // seul le filtre passe de un-à-la-fois à quatre-en-parallèle. Le gain
    // vient de là -- un filtre récursif attend ses propres résultats, quatre
    // filtres indépendants remplissent ces attentes (voir SimdFloat4.h).
    // ------------------------------------------------------------------

    /// Premier temps : tout ce qui précède le VCF. Renvoie le signal à filtrer
    /// (0 si la voix est éteinte) et mémorise la coupure de CET échantillon.
    float renderPreFilter(const Jupiter8Params& p, float lfo) {
        using namespace vsm::audio::dsp;
        active_ = ampEnv_.isActive();
        if (!active_) {
            // Une voix éteinte doit quand même publier une coupure valide : sa
            // ligne SIMD est calculée de toute façon, et une valeur aberrante
            // y produirait des dénormales ou des NaN qui coûteraient cher.
            pendingCutoff_ = p.cutoffBase;
            pendingAmpLevel_ = 0.0f;
            return 0.0f;
        }

        // baseHz ne dépend que du numéro de note : calculé une fois au
        // déclenchement (noteOn) plutôt qu'à chaque échantillon. Le drift, le
        // vibrato et le detune, eux, restent per-échantillon -- ils modulent.
        const float filtEnvLevel = filterEnv_.nextSample();
        const float ampLevel = ampEnv_.nextSample();

        const float pwm = lfo * p.lfoToPwm * 0.45f;

        // --- VCO-2 (source de cross-mod et de sync) ---
        const float dr2oct = drift2_.nextValue() * kDriftSemis / 12.0f;
        const float freq2 = baseHz_ * std::exp2f(p.vco2Detune / 12.0f + dr2oct
                                                 + p.bendOctaves);
        vco2_.setFrequency(freq2);
        vco2_.setWaveform(shapeToWave(p.vco2Shape));
        if (p.vco2Shape == 1)
            vco2_.setPulseWidth(std::clamp(p.vco2Pw + pwm, 0.05f, 0.95f));
        const float raw2 = vco2_.nextSample();

        // --- Hard-sync : reset de la phase du VCO-1 au cycle du VCO-2 ---
        syncPhase_ += freq2 / static_cast<float>(sampleRate_);
        if (syncPhase_ >= 1.0f) { syncPhase_ -= 1.0f; if (p.sync) vco1_.reset(0.0); }

        // --- VCO-1 (cible de la cross-mod + vibrato LFO) ---
        const float dr1semi = drift1_.nextValue() * kDriftSemis;
        const float freq1Oct = p.crossMod * raw2 * kCrossModOctaves
                             + lfo * p.lfoToPitch * kLfoPitchSemis / 12.0f
                             + dr1semi / 12.0f + p.bendOctaves;
        const float freq1 = baseHz_ * std::exp2f(freq1Oct);
        vco1_.setFrequency(freq1);
        vco1_.setWaveform(shapeToWave(p.vco1Shape));
        if (p.vco1Shape == 1)
            vco1_.setPulseWidth(std::clamp(p.vco1Pw + pwm, 0.05f, 0.95f));
        const float raw1 = vco1_.nextSample();

        float mix = (raw1 * p.vco1Level + raw2 * p.vco2Level) * 0.5f;

        // --- Passe-haut non résonant, puis VCF passe-bas résonant ---
        hpf_.setCutoffHz(p.hpfCutoff);
        mix = hpf_.process(mix);

        const float cutoffDriftOct = cutoffDrift_.nextValue() * kCutoffDriftOct;
        const float envOct = p.filterEnvAmount * filtEnvLevel * kFilterEnvOctaves;
        const float lfoOct = lfo * p.lfoToFilter * kLfoFilterOctaves;
        const float trackOct = p.keyTrack * (static_cast<float>(note_) - 60.0f) / 12.0f;
        pendingCutoff_ = p.cutoffBase * std::exp2f(envOct + lfoOct + trackOct + cutoffDriftOct);
        pendingAmpLevel_ = ampLevel;
        return mix;
    }

    /// Second temps : VCA sur la sortie du VCF (pas de vélocité -- le clavier
    /// du Jupiter-8 n'en a pas, trait authentique déjà couvert par un test).
    float applyFilterOutput(float filtered) const {
        return active_ ? filtered * pendingAmpLevel_ : 0.0f;
    }

    float pendingCutoffHz() const { return pendingCutoff_; }

private:
    static vsm::audio::dsp::Waveform shapeToWave(int shape) {
        using vsm::audio::dsp::Waveform;
        switch (shape) {
            case 1: return Waveform::Square;
            case 2: return Waveform::Triangle;
            default: return Waveform::Saw;
        }
    }

    static constexpr float kDriftSemis = 0.05f;
    static constexpr float kCutoffDriftOct = 0.12f;
    static constexpr float kFilterEnvOctaves = 6.0f;
    static constexpr float kLfoPitchSemis = 7.0f;
    static constexpr float kLfoFilterOctaves = 4.0f;
    static constexpr float kCrossModOctaves = 2.5f;

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::BandLimitedOscillator vco1_, vco2_;
    vsm::audio::dsp::StateVariableFilter hpf_;
    // Plus de VCF ici : il vit dans le synthé, partagé par groupes de quatre
    // voix (Jupiter8Synth::voiceFilters_).
    vsm::audio::dsp::AdsrEnvelope ampEnv_, filterEnv_;
    vsm::audio::dsp::AnalogDrift drift1_, drift2_, cutoffDrift_;
    float syncPhase_ = 0.0f;
    float pendingCutoff_ = 1600.0f;  // coupure du VCF pour l'échantillon en cours
    float pendingAmpLevel_ = 0.0f;   // niveau du VCA pour l'échantillon en cours
    bool active_ = false;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
    float baseHz_ = 261.6256f; // hauteur de la note en cours, figée au note-on (do3 par défaut)
};

/// Jupiter-8-style : polysynthé 8 voix, 2 VCO/voix avec cross-mod et sync,
/// passe-haut + VCF 4 pôles, DEUX enveloppes, LFO global, et le chorus
/// stéréo BBD caractéristique (brique dsp::Chorus déjà présente) appliqué au
/// mix mono des voix -- c'est lui qui donne au Jupiter sa largeur stéréo.
class Jupiter8Synth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    Jupiter8Synth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;

    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;

    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;

    const char* machineName() const override { return "Jupiter-8-style Polysynth"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& ev);

    vsm::audio::engine::VoiceManager<Jupiter8Voice, kMaxVoices> voiceManager_;

    // Un filtre vectorisé par groupe de quatre voix : 8 voix = 2 filtres.
    // static_assert plutôt qu'un commentaire d'espoir -- si quelqu'un passe le
    // Jupiter à 6 ou 10 voix un jour, le build casse ici, à l'endroit exact
    // où le raisonnement ne tient plus, au lieu de produire un son faux.
    static_assert(kMaxVoices % vsm::audio::dsp::LadderFilterZDFx4::kLanes == 0,
                  "le nombre de voix doit être un multiple de la largeur SIMD");
    static constexpr size_t kVoiceGroups = kMaxVoices / vsm::audio::dsp::LadderFilterZDFx4::kLanes;
    std::array<vsm::audio::dsp::LadderFilterZDFx4, kVoiceGroups> voiceFilters_;
    vsm::audio::dsp::Chorus chorus_;
    double lfoPhase_ = 0.0, lfoIncrement_ = 0.0;
    int chorusMode_ = 1;

    // Molette de hauteur (demi-tons), même contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};

    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::jupiter8
