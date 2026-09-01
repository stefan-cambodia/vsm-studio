#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Oversampler.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace vsm::plugins::chebyshev {

/// WAVESHAPING PAR POLYNÔMES DE TCHEBYCHEV — le spectre se COMMANDE, et la
/// brillance suit l'amplitude sans qu'aucun filtre ne s'en mêle.
///
/// POURQUOI CETTE MACHINE. Le parc sait déformer une onde de plusieurs
/// façons : `vsm.phasedist` déforme le TEMPS (la phase est relue de travers),
/// `vsm.westcoast` PLIE l'amplitude, `vsm.dx7` module une fréquence par une
/// autre. Personne n'y fait le waveshaping CLASSIQUE — une non-linéarité sans
/// mémoire appliquée à un sinus — et c'est dommage, parce que sa version
/// canonique (Arfib et Le Brun, 1979) a une propriété qu'aucune autre méthode
/// n'offre : **on écrit le spectre qu'on veut, et on l'obtient exactement.**
///
/// LA PROPRIÉTÉ, ET ELLE EST EXACTE, PAS APPROCHÉE. Le polynôme de Tchebychev
/// de rang n vérifie `T_n(cos θ) = cos(n·θ)` : appliqué à un SINUS d'amplitude
/// 1, il rend exactement l'harmonique n, sans aucun autre rang. Une somme
/// pondérée de ces polynômes rend donc exactement la somme des harmoniques
/// correspondants — le spectre est une commande, pas un résultat qu'on
/// approche en tournant des potentiomètres. C'est ce que mesure le trait
/// distinctif : poids sur le seul rang 3, et le rendu ne contient QUE
/// l'harmonique 3.
///
/// ET LE TRAIT MUSICAL EN DÉCOULE : **l'index d'entrée fait la brillance.**
/// Sous l'amplitude 1, l'égalité `T_n(cos θ) = cos(n θ)` cesse de tenir, et
/// les rangs élevés s'effondrent BEAUCOUP plus vite que les bas — un rang n
/// se comporte en `A^n` près de zéro. Une note qui décroît perd donc ses
/// aigus d'elle-même, exactement comme une corde réelle, sans enveloppe de
/// filtre pour le simuler. Le second test mesure cela : à index réduit, le
/// rapport rang 5 sur rang 1 s'effondre.
///
/// LE REPLIEMENT EST LE PIÈGE DE CETTE FAMILLE, et il est traité : un
/// polynôme de rang 8 multiplie par huit la bande occupée, donc la machine
/// travaille en SUR-ÉCHANTILLONNÉ (`dsp/Oversampler.h`, la brique du parc)
/// et redescend après le shaper. Sans cela, jouer haut ferait redescendre les
/// rangs hauts sous forme de sifflements inharmoniques — le contraire d'une
/// machine dont l'argument est un spectre exact.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « dérivé » : le shaper est appliqué
/// à la SOMME des voix, pas voix par voix — c'est le choix d'un waveshaper
/// analogique classique (un seul étage), et il a une conséquence audible que
/// le musicien doit connaître : deux notes tenues ensemble s'intermodulent,
/// comme sur un vrai circuit. La rendre par-voix coûterait huit
/// sur-échantillonneurs pour supprimer un effet qui fait partie du son.
class ChebyshevVoice {
public:
    struct Params {
        float index = 0.8f;          // amplitude d'entrée du shaper : LA brillance
        float velocityToIndex = 0.6f;
        float detune = 0.0f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        env_.setSampleRate(sampleRate_);
        (void)seed;
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
        // PHASE REMISE À ZÉRO : le shaper est sans mémoire, donc deux rendus
        // du même projet doivent partir du même point pour être identiques
        // au bit près.
        phase_ = 0.0;
    }
    void noteOff(uint8_t) { env_.noteOff(); }

    /// Rend le SINUS d'entrée du shaper, déjà mis à l'échelle de l'index :
    /// c'est la somme de ces sinus que la machine fera passer dans les
    /// polynômes, une seule fois par bloc.
    float render(const Params& p) {
        if (!env_.isActive()) return 0.0f;
        const float hz = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.detune + p.bendSemitones - 69.0f) / 12.0f);
        phase_ += static_cast<double>(hz) / sampleRate_;
        if (phase_ >= 1.0) phase_ -= 1.0;

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        // L'INDEX EST MODULÉ PAR L'ENVELOPPE : c'est ce qui fait que la note
        // s'assombrit en mourant, sans filtre.
        const float index = std::clamp(
            p.index * (1.0f - p.velocityToIndex * (1.0f - velocity)) * env_.nextSample(),
            0.0f, 1.0f);
        return static_cast<float>(std::sin(phase_ * vsm::audio::dsp::kTwoPi)) * index;
    }

private:
    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    vsm::audio::dsp::AdsrEnvelope env_;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class ChebyshevSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;
    static constexpr int kPartials = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kIndex = 1, kVelocityToIndex,
        kW1, kW2, kW3, kW4, kW5, kW6, kW7, kW8,
        kAttack, kDecay, kSustain, kRelease,
        kOutputLevel,
    };

    ChebyshevSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        if (event.kind == vsm::audio::plugin::MidiControlEvent::Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Chebyshev (le spectre commandé)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    /// La somme pondérée des polynômes de Tchebychev, par la récurrence
    /// `T_{n+1} = 2x·T_n - T_{n-1}` : huit rangs pour huit multiplications,
    /// sans aucune fonction transcendante — ce qui compte dans un chemin
    /// suréchantillonné quatre fois.
    static float shape(float x, const std::array<float, kPartials>& poids) {
        const float borne = std::clamp(x, -1.0f, 1.0f);
        float tPrecedent = 1.0f;     // T_0
        float t = borne;             // T_1
        float sortie = poids[0] * t;
        for (int n = 1; n < kPartials; ++n) {
            const float suivant = 2.0f * borne * t - tPrecedent;
            tPrecedent = t;
            t = suivant;
            sortie += poids[static_cast<size_t>(n)] * t;
        }
        // LE CONTINU DES RANGS PAIRS, RETIRÉ — ET C'EST UN DÉFAUT TROUVÉ PAR
        // LE TEST, PAS UNE PRÉCAUTION. `T_n(0)` vaut ±1 pour n PAIR (T_2(0) =
        // −1, T_4(0) = +1…) : entrée nulle, sortie NON nulle. La machine
        // sortait donc 0,30 de continu au repos, sans qu'aucune note ne soit
        // jouée -- le silence n'était pas silencieux. C'est la faute que le
        // § 44 d'ARCHITECTURE raconte pour la flûte, sous une autre forme :
        // une boucle ou un polynôme qui porte du continu écrase ce qu'on veut
        // entendre, et ne se voit dans aucune mesure de niveau.
        //
        // On retire `shape(0)`, qui est exactement cette valeur : la sortie
        // est alors nulle à entrée nulle, EXACTEMENT, et sans état ni
        // transitoire (un bloqueur de continu en aurait). Les harmoniques ne
        // bougent pas : on ne retire qu'une constante, et une constante n'est
        // l'harmonique de rien.
        float dcPrecedent = 1.0f;
        float dc = 0.0f;             // T_1(0) = 0
        float offset = 0.0f;
        for (int n = 1; n < kPartials; ++n) {
            const float suivant = -dcPrecedent;   // 2·0·T_n − T_{n−1}
            dcPrecedent = dc;
            dc = suivant;
            offset += poids[static_cast<size_t>(n)] * dc;
        }
        return sortie - offset;
    }

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<ChebyshevVoice, kMaxVoices> voiceManager_;
    vsm::audio::dsp::Oversampler oversampler_;
    std::vector<float> bloc_;
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::chebyshev
