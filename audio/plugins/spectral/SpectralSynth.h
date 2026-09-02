#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/RealFft.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::spectral {

/// SYNTHÈSE SPECTRALE — on ÉCRIT le spectre, et on redescend dans le temps.
///
/// POURQUOI CETTE MACHINE, ET C'EST LA DERNIÈRE GRANDE FAMILLE QUI MANQUAIT.
/// Toutes les autres machines du parc fabriquent une forme d'onde ; leur
/// spectre est ce qui en RÉSULTE. Ici on pose les amplitudes case par case et
/// une transformée inverse rend le signal. `vsm.chebyshev` approche l'idée —
/// « on écrit le spectre voulu et on l'obtient exactement » — mais il ne peut
/// poser que huit rangs HARMONIQUES, un polynôme de rang n ne rendant que
/// l'harmonique n.
///
/// **CE QUE CELA PERMET ET QUE RIEN D'AUTRE NE PERMET : un spectre DENSE et
/// INHARMONIQUE à coût constant.** `vsm.additive` pose des rangs entiers ;
/// `vsm.modal` a vingt-quatre modes et `vsm.plate` seize, chacun coûtant un
/// oscillateur. Une trame de mille vingt-quatre points rend cinq cent douze
/// raies pour le même prix, à des fréquences quelconques — le « bruit
/// accordé » d'une cloche géante ou d'une nappe de verre, qu'aucune de ces
/// machines ne peut approcher.
///
/// **ET LA POLYPHONIE EST GRATUITE**, ce qui n'est vrai nulle part ailleurs.
/// Il n'y a pas de voix : toutes les notes tenues déposent leurs partiels dans
/// LE MÊME spectre, et la transformée coûte la même chose qu'on en joue une ou
/// huit. C'est le seul endroit du parc où ajouter une note ne coûte rien.
///
/// ```
///   notes tenues ──> partiels (f, amplitude) ──> spectre d'une trame
///                                                      │  IFFT
///                                                      v
///                          fenêtre de Hann ──> recouvrement 50 % ──> sortie
/// ```
///
/// LE RECOUVREMENT EST LE PIÈGE DE CETTE FAMILLE, et il est traité : la
/// fenêtre de Hann à saut de moitié somme à une constante (condition COLA),
/// donc le raccord entre deux trames est exactement continu. Les PHASES
/// avancent d'une trame à l'autre de `2π·f·saut/fs` : sans cela, le signal se
/// répéterait à l'identique toutes les mille vingt-quatre trames, ce qui
/// s'entendrait comme un bourdonnement à quarante-six hertz.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « inspiré » : chaque partiel est
/// déposé dans une seule case, sans le noyau de la fenêtre qui l'étalerait
/// proprement — ce qui suffit pour un spectre dense mais raidit un partiel
/// isolé ; et il y a une LATENCE d'une demi-trame (environ onze millisecondes
/// à 48 kHz), inhérente à la méthode et impossible à supprimer.
class SpectralSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kTrame = 1024;
    static constexpr size_t kSaut = kTrame / 2;
    static constexpr int kMaxPartiels = 256;
    static constexpr size_t kMaxNotes = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kPartials = 1, kStretch, kTilt, kSpread,
        kAttack, kDecay, kSustain, kRelease,
        kVelocitySensitivity, kOutputLevel,
    };

    SpectralSynth();

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
    const char* machineName() const override { return "Spectral (le spectre écrit)"; }
    int activeVoiceCount() const override;

private:
    struct Note {
        vsm::audio::dsp::AdsrEnvelope env;
        float phase = 0.0f;        // phase commune, avancée d'une trame à l'autre
        uint8_t note = 60, channel = 0, velocity = 100;
        bool used = false;
    };

    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);
    /// Construit le spectre d'une trame et l'ajoute au tampon de sortie.
    void rendreUneTrame();

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};

    vsm::audio::dsp::RealIfft<kTrame> ifft_;
    std::array<Note, kMaxNotes> notes_{};
    /// Tampons de travail, TOUS pré-alloués : `process` n'alloue rien.
    std::array<float, vsm::audio::dsp::RealIfft<kTrame>::kBins> re_{}, im_{};
    std::array<float, kTrame> trame_{}, fenetre_{};
    /// File de sortie : la trame courante s'y ajoute par recouvrement.
    std::array<float, kTrame * 2> file_{};
    size_t lecture_ = 0;       // position de lecture dans la file
    size_t restant_ = 0;       // échantillons prêts avant la prochaine trame
    std::atomic<float> bendSemitones_{0.0f};
};

} // namespace vsm::plugins::spectral
