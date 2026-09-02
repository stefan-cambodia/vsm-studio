#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::terrain {

/// LE TERRAIN D'ONDES — une surface, une orbite, et c'est le CHEMIN qui fait
/// le timbre.
///
/// LA FAMILLE (Mitsuhashi ; Borgonovo et Haus, années 1980). On définit une
/// surface `z = f(x, y)` et on la parcourt : l'onde de sortie est l'altitude
/// rencontrée le long du chemin. Le timbre ne vient donc ni d'une table ni
/// d'un filtre, mais de la forme du trajet sur le relief.
///
/// ```
///        z = f(x, y)          l'orbite grandit ──> elle franchit des reliefs
///     ／‾‾＼_／‾‾＼            qu'elle ne touchait pas ──> spectre AUTRE
///    ／      ＼                (et non le même spectre plus fort)
///   ● ─ ─ ─ ─ ─ ●  orbite
/// ```
///
/// EN QUOI EST-CE AUTRE CHOSE QUE `vsm.vector` ? La question devait être
/// tranchée avant d'écrire une ligne, parce que les deux machines ont une
/// orbite dans un plan. La réponse tient en un mot : LINÉARITÉ. `vsm.vector`
/// mélange bilinéairement quatre formes d'onde — sa sortie est une combinaison
/// LINÉAIRE des quatre coins, si bien qu'agrandir l'orbite redose le mélange
/// mais **ne peut créer aucun contenu qui n'était pas déjà dans un coin**. Un
/// terrain est une fonction NON LINÉAIRE des coordonnées : agrandir l'orbite
/// fait franchir des bosses, et le spectre change de NATURE.
///
/// C'est mesuré, et sur les deux machines au même protocole (voir H20 du CDC
/// machines-manquantes) : le rapport h3/h1 du terrain bouge franchement avec
/// le rayon, à hauteur constante ; celui de `vsm.vector` ne peut pas.
///
/// LE RELIEF EST UNE SOMME DE DEUX SINUS CROISÉS plus un terme de produit, ce
/// qui donne une surface à bosses régulières dont on peut faire varier la
/// rugosité. Ce n'est pas la seule surface possible — la littérature en
/// propose d'infinies — mais c'en est une dont on comprend ce qu'elle fait, ce
/// qui vaut mieux qu'un relief tiré au hasard qu'on ne saurait pas régler.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « inspiré » : la trajectoire est un
/// cercle (éventuellement elliptique), là où la famille autorise n'importe
/// quelle courbe, y compris des courbes de Lissajous à rapports irrationnels ;
/// et il n'y a pas d'anti-repliement dédié — le relief est lissé par sa propre
/// nature (des sinus), et le filtre de sortie tient lieu de garde.
class TerrainVoice {
public:
    struct Params {
        float radius = 0.55f;       // taille de l'orbite : LE réglage de timbre
        float roughness = 0.5f;     // rugosité du relief
        float ellipse = 0.0f;       // orbite circulaire (0) ou aplatie (1)
        float driftRate = 0.15f;    // dérive lente du centre
        float cutoff = 10000.0f;
        float resonance = 0.08f;
        float velocityToRadius = 0.35f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        env_.setSampleRate(sampleRate_);
        filtre_.setSampleRate(sampleRate_);
        filtre_.reset();
        rng_ = vsm::util::DeterministicRng(seed);
        phase_ = 0.0;
        phaseDerive_ = 0.0f;
    }

    bool isActive() const { return env_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }
    void setEnvelope(const vsm::audio::dsp::AdsrSettings& s) { env_.setSettings(s); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        env_.noteOn();
        phase_ = 0.0;
        phaseDerive_ = 0.0f;
    }
    void noteOff(uint8_t) { env_.noteOff(); }

    /// LE RELIEF. Deux sinus croisés plus un terme de PRODUIT : c'est ce
    /// dernier qui rend la surface non séparable, donc le timbre dépendant du
    /// chemin et pas seulement de ses projections.
    static float altitude(float x, float y, float rugosite) {
        const float k = 1.0f + 3.0f * std::clamp(rugosite, 0.0f, 1.0f);
        return 0.55f * std::sin(k * x)
             + 0.35f * std::sin(k * y * 1.37f)
             + 0.45f * std::sin(k * x) * std::sin(k * y);
    }

    float render(const Params& p) {
        if (!env_.isActive()) return 0.0f;

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float rayon = std::clamp(
            p.radius * (1.0f - p.velocityToRadius * (1.0f - velocity)), 0.0f, 1.0f) * 6.0f;

        const float hz = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f);
        phase_ += static_cast<double>(hz) / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;

        // La dérive du centre : le chemin ne repasse pas exactement au même
        // endroit d'un tour à l'autre, ce qui fait respirer le timbre sans
        // qu'aucun LFO ne touche à la hauteur ni au filtre.
        phaseDerive_ += p.driftRate / static_cast<float>(sampleRate_);
        if (phaseDerive_ >= 1.0f) phaseDerive_ -= 1.0f;
        const float derive = 0.6f * std::sin(phaseDerive_ * vsm::audio::dsp::kTwoPi);

        const auto angle = static_cast<float>(phase_) * vsm::audio::dsp::kTwoPi;
        const float aplati = 1.0f - 0.85f * std::clamp(p.ellipse, 0.0f, 1.0f);
        const float x = rayon * std::cos(angle) + derive;
        const float y = rayon * aplati * std::sin(angle);

        const float z = altitude(x, y, p.roughness);

        filtre_.setCutoffHz(p.cutoff);
        filtre_.setResonance(p.resonance);
        return filtre_.process(z) * env_.nextSample() * kGain;
    }

private:
    static constexpr float kGain = 0.45f;

    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    float phaseDerive_ = 0.0f;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::StateVariableFilter filtre_;
    vsm::util::DeterministicRng rng_{0x54455252ULL};   // "TERR"
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class TerrainSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kRadius = 1, kRoughness, kEllipse, kDriftRate,
        kCutoff, kResonance,
        kAttack, kDecay, kSustain, kRelease,
        kVelocityToRadius, kOutputLevel,
    };

    TerrainSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        using Kind = vsm::audio::plugin::MidiControlEvent::Kind;
        if (event.kind == Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        // LA MOLETTE DE MODULATION AGRANDIT L'ORBITE : sur cette machine, le
        // geste de timbre le plus naturel est de marcher plus loin sur le
        // relief, et c'est exactement ce qu'une molette doit commander.
        if (event.kind == Kind::ControlChange && event.index == 1) {
            molette_.store(std::clamp(event.value, 0.0f, 1.0f), std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Terrain (le chemin fait le timbre)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<TerrainVoice, kMaxVoices> voiceManager_;
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> molette_{0.0f};
};

} // namespace vsm::plugins::terrain
