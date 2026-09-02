#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::theremin {

/// LE THÉRÉMINE — un instrument sans touches, donc sans sauts.
///
/// POURQUOI CETTE MACHINE. Un thérémine se joue dans l'air, entre deux
/// antennes : la main droite fait la hauteur, la gauche le volume. **Il n'y a
/// rien à toucher, donc aucune discontinuité possible** — pour aller d'une note
/// à l'autre, la main traverse toutes celles du milieu, et on les entend. Le
/// glissando n'est pas un effet qu'on ajoute ici, c'est la seule façon dont
/// l'instrument sait changer de note.
///
/// **Aucune machine du parc ne refuse les sauts.** Toutes ont un portamento
/// réglable, c'est-à-dire optionnel, et à zéro par défaut. Sur celle-ci il ne
/// peut pas être nul : `Glide` a une borne basse de vingt millisecondes, et ce
/// n'est pas une précaution, c'est la définition de l'instrument.
///
/// **LE VOLUME NE VIENT PAS DE LA FRAPPE — il n'y a pas de frappe.** La
/// vélocité MIDI ne dit rien d'un thérémine ; c'est la main gauche qui fait le
/// niveau, et elle en fait TOUT : l'attaque, les nuances, l'extinction.
/// `vsm.juno106` ignore déjà la vélocité et un test le verrouille, mais il la
/// remplace par une constante ; ici elle est remplacée par un GESTE, la
/// pression de canal, qui est ce qu'un clavier sait envoyer de plus proche
/// d'une main dans l'air.
///
/// MONOPHONIQUE, ET PAS PAR ÉCONOMIE : une main ne peut être qu'à un endroit.
/// Jouer un accord sur un thérémine n'a aucun sens, et une machine qui
/// l'accepterait mentirait sur ce qu'elle imite. Une nouvelle note ne prend
/// donc pas une voix de plus — elle déplace la main.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « inspiré » : le timbre d'un
/// thérémine vient du battement de deux oscillateurs radio dont on filtre la
/// différence, ce qui donne une onde presque sinusoïdale mais jamais
/// tout à fait ; on la rend ici par une sinusoïde à laquelle on ajoute deux
/// rangs, plutôt que de simuler l'hétérodynage lui-même. Et le corps de
/// l'instrument — un coffret de bois qui rayonne — n'est pas modélisé.
class ThereminSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    enum ParamIds : vsm::audio::plugin::ParamId {
        kGlide = 1, kWarmth, kVibratoDepth, kVibratoRate,
        kVolumeResponse, kCutoff, kOutputLevel,
    };

    ThereminSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        using Kind = vsm::audio::plugin::MidiControlEvent::Kind;
        // LA MAIN GAUCHE. C'est le geste principal de l'instrument, et il n'a
        // rien d'un « effet » : sans lui, un thérémine reste muet.
        if (event.kind == Kind::ChannelPressure) {
            mainGauche_.store(std::clamp(event.value, 0.0f, 1.0f), std::memory_order_relaxed);
            return true;
        }
        if (event.kind == Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Theremin (la main dans l'air)"; }
    int activeVoiceCount() const override { return noteTenue_ >= 0 ? 1 : 0; }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};

    /// LA MAIN : une seule position, qui GLISSE vers la note demandée. Il n'y
    /// a pas de tableau de voix ici, et c'est le trait de la machine inscrit
    /// dans sa structure même.
    double phase_ = 0.0;
    float hzCourant_ = 0.0f;      // où la main EST
    float hzVise_ = 0.0f;         // où elle VA
    float niveau_ = 0.0f;         // suivi lissé de la main gauche
    float phaseVibrato_ = 0.0f;
    int noteTenue_ = -1;

    std::atomic<float> mainGauche_{-1.0f};   // < 0 : aucune pression reçue
    std::atomic<float> bendSemitones_{0.0f};
    vsm::audio::dsp::StateVariableFilter filtre_;
};

} // namespace vsm::plugins::theremin
