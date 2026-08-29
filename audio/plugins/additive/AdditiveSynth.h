#pragma once
#include "vsm/audio/dsp/AnalogDrift.h"
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::additive {

/// SYNTHÈSE ADDITIVE — le son construit rang par rang, et non taillé au filtre.
///
/// POURQUOI CETTE MACHINE. Le § 7 de `docs/CDC-machines-manquantes.md` demande
/// de juger une machine sur la COUVERTURE qu'elle ajoute et déconseille
/// explicitement « un septième soustractif » : ce serait un nom sur une liste,
/// pas une famille de sons. L'additif EST une famille absente. Le parc a des
/// soustractifs (Minimoog, Prophet, OB-X, Juno, Jupiter, MS-20, ARP, SH-101,
/// TB-303, supersaw), de la FM (DX7), de la table d'ondes, de l'hybride PCM, de
/// la modélisation physique (corde, vent, piano), du report d'échantillons — et
/// une seule chose qui empile des sinus, l'orgue à roues phoniques, dont les
/// neuf tirettes sont à des rapports FIXES et sans enveloppe propre. Personne
/// ne peut poser un spectre arbitraire.
///
/// CE QUE ÇA CHANGE POUR LA CHAÎNE DE RECONSTRUCTION, et c'est l'argument qui
/// compte ici. Toutes les autres machines fabriquent un spectre par un chemin
/// INDIRECT : on règle une coupure, une résonance, un indice de modulation, et
/// le spectre en découle. Chercher un patch, c'est alors inverser cette
/// application. L'additif est le seul dont les réglages DÉCRIVENT le spectre :
/// ce que la mesure voit est ce que la machine expose. C'est la machine la plus
/// directement inversible du parc, et c'est vérifiable — voir
/// `additive_reaches_a_spectrum_no_filter_can_make`.
///
/// LE TRAIT DISTINCTIF, ET IL EST TESTÉ DEUX FOIS.
///
///  1. **Un spectre à TROUS.** Un filtre est une fonction de transfert
///     CONTINUE : atténuer le rang 3 en laissant intacts les rangs 2 et 4 lui
///     est impossible, quelle que soit sa résonance. Ici, le réglage
///     `Odd/Even Balance` éteint un rang sur deux exactement, et le test mesure
///     les deux moitiés : les rangs impairs présents, les pairs à zéro.
///  2. **Des partiels ÉTIRÉS.** Une corde raide n'a pas ses partiels aux
///     multiples entiers de `f0` mais à `n·f0·sqrt(1 + B·n²)` -- c'est ce qui
///     fait qu'un piano s'accorde faux exprès. Aucune machine du parc ne sait
///     étirer un spectre ainsi ; ici c'est un réglage, et le test vérifie que
///     le rang 8 se déplace bien de plusieurs pour cent quand on le pousse.
///
/// CE QUE LA MACHINE N'EST PAS. Ce n'est pas un resynthétiseur : elle n'analyse
/// aucun son et ne rejoue aucune trajectoire relevée. Ses trente-deux partiels
/// sont pilotés par SIX réglages de forme, pas par soixante-quatre curseurs --
/// un curseur par rang serait fidèle à un Synclavier et inutilisable par une
/// recherche de patch, qui a besoin d'un espace de petite dimension (§ 6 de
/// CDC-machines-manquantes.md, « profil de recherche »).
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de CDC-nouvelle-machine.md), statut « dérivé » :
///
///  - **Trente-deux partiels, pas davantage.** Au-delà, les rangs d'une note
///    grave sortent de la bande audible et ceux d'une note aiguë se replient.
///    Les partiels au-dessus de Nyquist sont ÉTEINTS, pas repliés -- c'est la
///    seule façon honnête, et c'est ce qui distingue un additif d'un
///    oscillateur à table mal filtré.
///  - **Une seule enveloppe d'amplitude pour toute la voix**, plus une DÉRIVE
///    d'extinction par rang : les rangs hauts meurent plus vite que les bas,
///    d'un facteur réglable. Trente-deux enveloppes indépendantes seraient plus
///    fidèles à la famille et ingérables par la recherche.
///  - **Pas de partiels inharmoniques arbitraires** (cloches, gongs) : l'étirement
///    est celui d'une corde raide, à un paramètre. Une cloche demanderait des
///    rapports libres, donc autant de réglages que de rangs.

class AdditiveVoice {
public:
    /// Trente-deux rangs : au-delà, une note grave dépasse la bande audible.
    static constexpr int kPartials = 32;

    struct Params {
        float partialCount = 16.0f;     // combien de rangs sonnent (1..32)
        float tiltDbPerOct = -6.0f;     // pente du spectre, en dB par octave
        float oddEven = 0.5f;           // 0 = impairs seuls, 1 = pairs seuls
        float inharmonicity = 0.0f;     // raideur : rang n à n·f0·sqrt(1+B·n²)
        float decayTilt = 0.5f;         // 0 = tous les rangs meurent ensemble
        float attackSpread = 0.0f;      // les rangs hauts arrivent en retard
        float velocityToTilt = 0.4f;    // jouer fort ouvre le spectre
        float drift = 0.15f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate;
        ampEnv_.setSampleRate(sampleRate);
        drift_.setSampleRate(sampleRate);
        drift_.setSeed(seed);
        // La dérive d'un additif est LENTE, comme celle des autres machines :
        // c'est une instabilité d'accord, pas un vibrato.
        drift_.setRateHz(0.09f);
        drift_.setAmount(0.0f);
        for (auto& p : phase_) p = 0.0f;
    }

    bool isActive() const { return ampEnv_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setSettings(const vsm::audio::dsp::AdsrSettings& amp) { ampEnv_.setSettings(amp); }
    void setDriftAmount(float amount) { drift_.setAmount(amount); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        baseHz_ = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
        ampEnv_.noteOn();
        elapsed_ = 0.0f;
        // LES PHASES SONT REMISES À ZÉRO, et ce n'est pas anodin : trente-deux
        // sinus qui démarrent ensemble donnent une attaque nette et, surtout,
        // un rendu DÉTERMINISTE. Des phases tirées au sort donneraient une
        // crête différente à chaque note, donc une empreinte de non-régression
        // impossible à écrire.
        for (auto& p : phase_) p = 0.0f;
    }

    void noteOff(uint8_t) { ampEnv_.noteOff(); }

    float render(const Params& p) {
        if (!ampEnv_.isActive()) return 0.0f;

        const float vel = static_cast<float>(velocity_) / 127.0f;
        // Jouer fort redresse la pente : c'est ce qu'on entend d'un instrument
        // poussé, et c'est le seul rôle de la vélocité sur le timbre ici.
        const float tilt = p.tiltDbPerOct + p.velocityToTilt * vel * 6.0f;
        // La dérive est en DEMI-TONS, comme partout ailleurs dans le parc.
        const float f0 = baseHz_ * std::pow(2.0f, drift_.nextValue() * 0.05f / 12.0f);
        const int rangs = std::max(1, std::min(kPartials, static_cast<int>(p.partialCount + 0.5f)));
        const float nyquist = static_cast<float>(sampleRate_) * 0.5f;
        const float B = p.inharmonicity * 0.0008f;

        float sum = 0.0f;
        float sommeAmplitudes = 0.0f;
        for (int k = 0; k < rangs; ++k) {
            const float n = static_cast<float>(k + 1);
            // ÉTIREMENT D'UNE CORDE RAIDE : n·f0·sqrt(1 + B·n²). À B = 0 on
            // retrouve exactement la série harmonique.
            const float freq = n * f0 * std::sqrt(1.0f + B * n * n);
            // AU-DESSUS DE NYQUIST, ON ÉTEINT. Replier serait produire une
            // fréquence que personne n'a demandée.
            if (freq >= nyquist) break;

            // Pente : -tilt dB par octave au-dessus du fondamental.
            float amp = std::pow(n, tilt / 6.0206f);
            // Balance impairs/pairs : à 0, seuls les rangs impairs sonnent ; à
            // 1, seuls les pairs ; à 0,5, tous à égalité. C'est ce réglage qui
            // fait le trou qu'aucun filtre ne peut faire.
            const bool pair = ((k + 1) % 2) == 0;
            amp *= pair ? (p.oddEven * 2.0f) : ((1.0f - p.oddEven) * 2.0f);
            // Les rangs hauts meurent plus vite : une corde perd ses aigus.
            amp *= std::exp(-p.decayTilt * elapsed_ * n * 0.8f);
            // …et arrivent plus tard, si on le demande.
            if (p.attackSpread > 0.0f) {
                const float seuil = p.attackSpread * 0.02f * n;
                if (elapsed_ < seuil) amp *= elapsed_ / std::max(seuil, 1e-6f);
            }

            phase_[static_cast<size_t>(k)] +=
                static_cast<float>(vsm::audio::dsp::kTwoPi) * freq / static_cast<float>(sampleRate_);
            if (phase_[static_cast<size_t>(k)] > static_cast<float>(vsm::audio::dsp::kTwoPi))
                phase_[static_cast<size_t>(k)] -= static_cast<float>(vsm::audio::dsp::kTwoPi);
            sum += std::sin(phase_[static_cast<size_t>(k)]) * amp;
            sommeAmplitudes += std::abs(amp);
        }

        elapsed_ += 1.0f / static_cast<float>(sampleRate_);
        // NORMALISATION PAR LA SOMME DES AMPLITUDES, et c'est la seule qui
        // BORNE. Trente-deux sinus dont les phases partent alignées peuvent
        // s'additionner en crête : la somme de leurs amplitudes est alors
        // atteinte, et c'est exactement ce majorant qu'on divise. Une
        // normalisation par la racine du nombre de rangs -- l'usage, qui
        // suppose des phases indépendantes -- laissait passer un facteur 5,7
        // dans ce cas-là, mesuré : un accord de huit notes à pente nulle
        // crêtait au-dessus de 1.
        //
        // Le prix est connu et assumé : sur un spectre en pente raide, où un
        // seul rang porte presque tout, la somme des amplitudes vaut à peu près
        // ce rang, donc la normalisation ne coûte rien ; sur un spectre plat,
        // elle rend la machine plus discrète qu'un soustractif à niveau égal.
        // C'est le sens physique d'un additif : trente-deux rangs se PARTAGENT
        // l'énergie, ils ne l'additionnent pas.
        const float normalisation = 1.0f / std::max(sommeAmplitudes, 1e-6f);
        return sum * normalisation * ampEnv_.nextSample() * (0.3f + 0.7f * vel);
    }

private:
    double sampleRate_ = 48000.0;
    vsm::audio::dsp::AdsrEnvelope ampEnv_;
    vsm::audio::dsp::AnalogDrift drift_;
    std::array<float, kPartials> phase_{};
    float baseHz_ = 261.6f, elapsed_ = 0.0f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class AdditiveSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kPartialCount = 1, kSpectralTilt, kOddEven, kInharmonicity,
        kDecayTilt, kAttackSpread,
        kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
        kVelocityToTilt, kAnalogCharacter, kOutputLevel,
    };

    AdditiveSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Additive (le spectre rang par rang)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<AdditiveVoice, kMaxVoices> voiceManager_;
};

} // namespace vsm::plugins::additive
