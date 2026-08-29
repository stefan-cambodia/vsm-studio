#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/Filter.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/util/DeterministicRng.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace vsm::plugins::stochastic {

/// SYNTHÈSE STOCHASTIQUE — une forme d'onde qui ne se répète JAMAIS.
///
/// POURQUOI CETTE MACHINE. Les trente-quatre autres ont un point commun qu'on ne
/// remarque pas tant qu'on n'a que celles-là : **elles sont exactement
/// périodiques**. Un oscillateur relit la même forme d'onde à chaque tour, au bit
/// près ; même la puce 8 bits, même le diviseur de fréquence. Ce qui varie d'un
/// tour à l'autre, quand quelque chose varie, est un filtre ou une enveloppe --
/// jamais l'onde elle-même.
///
/// Xenakis a proposé l'inverse en 1971, et cette famille n'a aucun représentant
/// ici : la forme d'onde est décrite par une poignée de POINTS DE BRISURE, et
/// chacun d'eux se DÉPLACE d'un tour au suivant, par une marche aléatoire
/// bornée. Le son garde une hauteur -- la période reste la période -- mais son
/// timbre bouge en permanence, sans qu'aucun filtre ni aucune modulation ne s'en
/// mêle. C'est un bruit qui a une note, ou une note qui a la texture d'un bruit,
/// selon d'où on l'écoute.
///
/// LE TRAIT DISTINCTIF, ET IL EST BINAIRE. Sur n'importe quelle autre machine du
/// parc, deux périodes successives d'une note tenue sont IDENTIQUES. Ici elles
/// ne le sont jamais -- et le test mesure les deux moitiés : à divagation nulle
/// la machine redevient exactement périodique (deux périodes identiques au
/// millionième), à divagation ouverte elles diffèrent franchement. Sans la
/// première moitié, on ne saurait pas que la différence vient du réglage et non
/// du bruit d'un calcul.
///
/// LE SECOND RÉGLAGE EST CELUI QUI REND LA MACHINE JOUABLE : le VERROU DE
/// HAUTEUR. Laissée libre, une marche aléatoire sur les durées fait dériver la
/// période, donc la note -- c'est le son de Xenakis, et c'est inutilisable dans
/// un morceau. Le verrou renormalise les durées à chaque tour pour que leur
/// somme retombe sur la note demandée : la forme d'onde continue de divaguer,
/// la hauteur non. À zéro, la machine dérive ; à un, elle est juste. Les deux
/// états sont mesurés.
///
/// APPROXIMATIONS ASSUMÉES (§ 8 de `CDC-nouvelle-machine.md`), statut
/// « dérivé » :
///
///  - **Marche aléatoire à pas uniforme et miroir aux bornes**, là où Xenakis
///    proposait plusieurs lois de probabilité. Le pas uniforme est celui dont
///    on entend le mieux l'effet ; d'autres lois seraient un réglage de plus
///    pour une différence que rien ne mesure.
///  - **Interpolation linéaire entre les points**, ce qui produit des angles,
///    donc des harmoniques hautes. C'est le son de cette famille ; l'arrondir
///    la ferait ressembler à une table d'ondes, que le parc a déjà.
///  - **Tout l'aléatoire passe par `DeterministicRng`, seedé** : deux rendus
///    d'une même session sont identiques au bit près, sans quoi ni l'empreinte
///    de non-régression ni la recherche de patch ne seraient possibles. Une
///    machine « aléatoire » dont le hasard n'est pas reproductible serait
///    inutilisable ici.

class StochasticVoice {
public:
    /// Seize points au plus : au-delà, la forme d'onde devient si détaillée que
    /// la marche aléatoire ne s'entend plus comme un timbre qui bouge mais
    /// comme du bruit.
    static constexpr int kMaxPoints = 16;

    struct Params {
        float points = 8.0f;         // combien de points de brisure
        float ampWander = 0.15f;     // pas de la marche sur les amplitudes
        float timeWander = 0.10f;    // pas de la marche sur les durées
        float pitchLock = 1.0f;      // 0 = la hauteur dérive, 1 = elle tient
        float velocityToWander = 0.3f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate;
        env_.setSampleRate(sampleRate);
        rng_ = vsm::util::DeterministicRng{seed};
        tone_.setSampleRate(sampleRate);
        tone_.setMode(vsm::audio::dsp::StateVariableFilter::Mode::LowPass);
        tone_.setResonance(0.1f);
        reinitialiser();
    }

    bool isActive() const { return env_.isActive(); }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }

    void setSettings(const vsm::audio::dsp::AdsrSettings& s) { env_.setSettings(s); }
    void setToneHz(float hz) { tone_.setCutoffHz(hz); }

    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        channel_ = channel;
        note_ = note;
        velocity_ = velocity;
        baseHz_ = 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
        env_.noteOn();
        reinitialiser();
    }

    void noteOff(uint8_t) { env_.noteOff(); }

    float render(const Params& p) {
        if (!env_.isActive()) return 0.0f;

        const int n = std::clamp(static_cast<int>(p.points + 0.5f), 2, kMaxPoints);
        const float vel = static_cast<float>(velocity_) / 127.0f;

        if (n != points_ || periode_ <= 0.0f) { points_ = n; nouveauTour(n, p); position_ = 0.0f; }

        // UNE PHASE CONTINUE, ET NON UN COMPTEUR DÉCRÉMENTÉ.
        //
        // La première version tenait un « reste » qu'elle décrémentait d'une
        // unité par échantillon, et changeait de segment quand il passait sous
        // zéro. Les durées étant fractionnaires, les frontières tombaient à une
        // position sous-échantillon DIFFÉRENTE à chaque tour : la forme était
        // rééchantillonnée autrement d'une période à l'autre. Mesuré, deux
        // périodes successives différaient de 11,6 % ALORS QUE RIEN NE
        // DIVAGUAIT -- un artefact d'implémentation que le test du trait
        // distinctif ne pouvait pas distinguer du phénomène qu'il mesure.
        //
        // La position est donc absolue dans la période, et le segment s'en
        // déduit. À divagation nulle, la machine est exactement périodique.
        position_ += 1.0f;
        if (position_ >= periode_) {
            position_ -= periode_;
            nouveauTour(n, p);
        }

        // Dans quel segment tombe-t-on ?
        float debut = 0.0f;
        int k = 0;
        for (; k < n; ++k) {
            const float fin = debut + durees_[static_cast<size_t>(k)];
            if (position_ < fin || k == n - 1) break;
            debut = fin;
        }
        const float duree = std::max(durees_[static_cast<size_t>(k)], 1e-6f);
        const float avance = std::clamp((position_ - debut) / duree, 0.0f, 1.0f);
        const float a = (k == 0) ? amplitudes_[static_cast<size_t>(n - 1)]
                                 : amplitudes_[static_cast<size_t>(k - 1)];
        const float b = amplitudes_[static_cast<size_t>(k)];
        const float brut = a + (b - a) * avance;

        return tone_.process(brut) * env_.nextSample() * (0.35f + 0.65f * vel);
    }

private:
    void reinitialiser() {
        for (int k = 0; k < kMaxPoints; ++k) {
            amplitudes_[static_cast<size_t>(k)] = rng_.nextBipolar() * 0.7f;
            durees_[static_cast<size_t>(k)] = 1.0f;
        }
        position_ = 0.0f;
        periode_ = 0.0f;
        points_ = 0;
    }

    /// UN TOUR COMPLET DE LA FORME D'ONDE : chaque point se déplace, puis les
    /// durées sont renormalisées si le verrou de hauteur le demande.
    void nouveauTour(int n, const Params& p) {
        const float ampPas = p.ampWander * (1.0f + p.velocityToWander
                                            * static_cast<float>(velocity_) / 127.0f);
        for (int k = 0; k < n; ++k) {
            amplitudes_[static_cast<size_t>(k)] =
                miroir(amplitudes_[static_cast<size_t>(k)] + rng_.nextBipolar() * ampPas, 1.0f);
            durees_[static_cast<size_t>(k)] =
                std::clamp(durees_[static_cast<size_t>(k)]
                           * (1.0f + rng_.nextBipolar() * p.timeWander), 0.3f, 3.0f);
        }

        // LE VERROU DE HAUTEUR. La période vaut la somme des durées ; sans
        // renormalisation elle dérive avec la marche aléatoire, et la note avec
        // elle. On ramène donc cette somme vers `sampleRate / f0`, d'autant plus
        // fort que le verrou est serré.
        const float visee = static_cast<float>(sampleRate_) / std::max(baseHz_, 1.0f);
        float somme = 0.0f;
        for (int k = 0; k < n; ++k) somme += durees_[static_cast<size_t>(k)];
        if (somme > 1e-6f) {
            const float exact = visee / somme;
            const float facteur = 1.0f + (exact - 1.0f) * std::clamp(p.pitchLock, 0.0f, 1.0f);
            for (int k = 0; k < n; ++k) durees_[static_cast<size_t>(k)] *= facteur;
        }
        periode_ = 0.0f;
        for (int k = 0; k < n; ++k) periode_ += durees_[static_cast<size_t>(k)];
        periode_ = std::max(periode_, 2.0f);
    }

    /// Marche aléatoire BORNÉE par réflexion : au lieu d'écrêter -- ce qui
    /// collerait les points aux bornes et figerait la forme --, on renvoie le
    /// point vers l'intérieur. La marche reste vivante.
    static float miroir(float x, float borne) {
        while (x > borne || x < -borne) {
            if (x > borne) x = 2.0f * borne - x;
            if (x < -borne) x = -2.0f * borne - x;
        }
        return x;
    }

    double sampleRate_ = 48000.0;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::StateVariableFilter tone_;
    vsm::util::DeterministicRng rng_{0x53544F4348ULL};
    std::array<float, kMaxPoints> amplitudes_{};
    std::array<float, kMaxPoints> durees_{};
    int points_ = 0;
    float position_ = 0.0f, periode_ = 0.0f;
    float baseHz_ = 261.6f;
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class StochasticSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kPoints = 1, kAmpWander, kTimeWander, kPitchLock,
        kTone,
        kAttack, kDecay, kSustain, kRelease,
        kVelocityToWander, kOutputLevel,
    };

    StochasticSynth();

    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Stochastic (la forme qui divague)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<StochasticVoice, kMaxVoices> voiceManager_;
};

} // namespace vsm::plugins::stochastic
