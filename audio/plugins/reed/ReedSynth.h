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

namespace vsm::plugins::reed {

/// ANCHE LIBRE — harmonium, accordéon, harmonica : la lame qui bat DANS son
/// cadre, et qui n'a pas de colonne d'air.
///
/// POURQUOI CE N'EST PAS LA SIXIÈME TENTATIVE DE VENT. Le dépôt a mesuré CINQ
/// échecs sur les instruments à vent (§ 33 et § 44 d'ARCHITECTURE) et en a tiré
/// une conclusion qui sert de garde-fou : « ce n'est ni la forme de la perce ni
/// la nature de l'excitateur qui bloque, c'est la formulation du COUPLAGE entre
/// l'excitateur et la colonne d'air ». Il fallait donc répondre à cela avant
/// d'écrire une ligne, et la réponse est structurelle : **une anche libre n'a
/// pas de colonne d'air.**
///
/// Dans une clarinette, l'anche bat CONTRE une table et sa fréquence est
/// imposée par le tuyau ; c'est ce couplage-là qui a divergé cinq fois. Dans un
/// harmonium, la lame bat DANS son cadre sans le toucher, et **sa fréquence est
/// la sienne propre**, celle d'une lame encastrée. Le corps ne fait que
/// rayonner. Il n'y a donc aucun guide d'ondes : un seul résonateur mécanique
/// entretenu par un flux, c'est-à-dire une boucle LOCALE, sans les deux cents
/// échantillons de retard qui rendaient les précédentes intraitables.
///
/// LE TRAIT DISTINCTIF : **souffler plus fort fait BAISSER la note.** Fletcher
/// et Rossing le donnent, et c'est contre-intuitif — la charge d'air ajoutée
/// alourdit la lame, donc la ralentit. Mesuré ici sur la course de pression,
/// note 57 : **+3,1 · 0,0 · −3,1 · −6,8 · −8,9 cents**, monotone.
///
/// L'ATTENDU DE DÉPART ÉTAIT PLUS BEAU QUE LA RÉALITÉ, ET C'EST LA MESURE QUI
/// L'A DIT. L'hypothèse H11 annonçait un contraste de SENS avec `vsm.wind` :
/// l'anche battante devait MONTER en se raidissant contre sa table. Mesurée au
/// même protocole, `vsm.wind` ne monte pas — elle ne bouge pas du tout (+0,5 à
/// +1,0 cent, c'est-à-dire le bruit de la mesure), sa hauteur étant imposée par
/// la longueur du tuyau et non par l'anche. Le contraste existe donc, mais il
/// n'est pas celui qu'on attendait : il oppose une machine SENSIBLE à une
/// machine INSENSIBLE, et non deux sens opposés. C'est écrit ainsi plutôt que
/// corrigé en silence, parce qu'une hypothèse à demi vérifiée qu'on reformule
/// après coup ne vaut plus rien.
///
/// Il reste que c'est **le seul endroit du parc où la pression de jeu change la
/// hauteur**, et c'est bien ce qui fait le « souffle » d'un accordéon.
///
/// LA SORTIE EST LE DÉBIT, PAS LA POSITION. Ce n'est pas un détail de câblage :
/// une lame se déplace de façon quasi sinusoïdale, mais le passage qu'elle
/// ouvre ne dépend que de son ÉCART au cadre, des deux côtés — c'est un
/// redressement. Le débit contient donc des harmoniques que la position n'a
/// pas, et c'est ce qui donne à l'accordéon son grain nasillard là où un simple
/// oscillateur sonnerait creux.
///
/// APPROXIMATIONS ASSUMÉES (§ 8), statut « inspiré », et il faut le dire
/// franchement : l'entretien n'est pas dérivé des équations de Bernoulli dans
/// le canal, mais modélisé par un amortissement qui devient NÉGATIF au-delà
/// d'une pression de seuil, borné par la butée du cadre. C'est le comportement
/// (seuil d'amorçage, cycle limite, spectre du débit) sans la mécanique fine.
/// La baisse de fréquence sous pression, elle, est portée par une raideur
/// effective décroissante — l'explication usuelle de la charge d'air ajoutée.
class ReedVoice {
public:
    struct Params {
        float pressure = 0.6f;        // pression de soufflerie
        float stiffness = 0.5f;       // raideur de la lame : le seuil d'amorçage
        float airLoading = 0.5f;      // combien la pression alourdit la lame
        float cutoff = 6000.0f;
        float resonance = 0.1f;
        float velocityToPressure = 0.4f;
        float bendSemitones = 0.0f;
    };

    void prepare(double sampleRate, uint64_t seed) {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        env_.setSampleRate(sampleRate_);
        filtre_.setSampleRate(sampleRate_);
        filtre_.reset();
        rng_ = vsm::util::DeterministicRng(seed);
        y_ = v_ = 0.0f;
        dcX_ = dcY_ = 0.0f;
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
        // UNE LAME AU REPOS NE DÉMARRE PAS TOUTE SEULE : il lui faut une
        // chiquenaude, et dans l'instrument c'est la turbulence du premier
        // souffle. Déterministe (graine par voix), donc deux rendus du même
        // projet restent identiques au bit près.
        y_ = 0.02f * rng_.nextBipolar();
        v_ = 0.0f;
    }
    void noteOff(uint8_t) { env_.noteOff(); }

    float render(const Params& p) {
        if (!env_.isActive()) return 0.0f;

        const float velocity = static_cast<float>(velocity_) / 127.0f;
        const float pression = std::clamp(
            p.pressure * (1.0f - p.velocityToPressure * (1.0f - velocity)), 0.0f, 1.0f);

        const float hz = 440.0f * std::exp2f(
            (static_cast<float>(note_) + p.bendSemitones - 69.0f) / 12.0f);

        // LA RAIDEUR EFFECTIVE BAISSE AVEC LA PRESSION — c'est le trait de la
        // famille, et c'est la seule ligne qui le porte. La charge d'air
        // alourdit la lame, donc la ralentit. Une anche BATTANTE ferait le
        // contraire, en se raidissant contre sa table.
        // L'ampleur est celle du vrai instrument, et le premier essai l'avait
        // prise dix fois trop grande : à 10 % de raideur en moins, la note
        // descendait de 165 cents — un ton et demi, c'est-à-dire une machine
        // fausse, pas une machine expressive. Réduit à 1,2 %, l'écart plafonne
        // à une vingtaine de cents, ce qui est l'ordre de grandeur mesuré sur
        // les anches libres. Et l'écart est CENTRÉ sur une pression de
        // référence : la note demandée doit sortir juste au réglage normal,
        // sinon le trait se paierait d'une machine inutilisable en accord.
        const float charge = 0.012f * p.airLoading * (pression - kPressionDeReference);
        const float omega = 2.0f * static_cast<float>(M_PI) * hz * (1.0f - charge)
                          / static_cast<float>(sampleRate_);

        // L'ENTRETIEN : au-delà d'une pression de seuil, l'air rend à la lame
        // plus d'énergie qu'elle n'en perd — un amortissement NÉGATIF. La
        // butée du cadre le borne, sans quoi l'oscillation croîtrait sans fin.
        const float seuil = 0.12f + 0.5f * std::clamp(p.stiffness, 0.0f, 1.0f);
        const float apport = (pression - seuil) * 0.02f;
        const float butee = std::min(1.0f, y_ * y_ * 9.0f);
        const float amortissement = 0.0008f - apport * (1.0f - butee);

        // Oscillateur du second ordre, intégré en semi-implicite : la vitesse
        // d'abord, la position ensuite, ce qui garde l'énergie du cycle.
        v_ += -omega * omega * y_ - 2.0f * amortissement * omega * v_;
        v_ = std::clamp(v_, -1.0f, 1.0f);
        y_ = std::clamp(y_ + v_, -1.0f, 1.0f);

        // LE DÉBIT, redressé : le passage s'ouvre selon l'ÉCART au cadre, des
        // deux côtés. C'est ce redressement qui fait le grain de l'anche.
        // REDRESSEMENT SIMPLE, PAS DOUBLE — et la mesure a tranché une erreur
        // de physique que le son seul n'aurait pas dénoncée. Le premier essai
        // ouvrait le passage des DEUX côtés (`|y|`), en croyant décrire une
        // lame qui traverse son cadre. Résultat mesuré au spectre : toute
        // l'énergie à 2·f0 et RIEN au fondamental — la machine jouait une
        // octave au-dessus de la note demandée, parce qu'un redressement
        // double face double la fréquence. Une anche libre laisse passer l'air
        // d'un seul côté, celui vers lequel la soufflerie pousse ; le
        // redressement est donc SIMPLE, sa fondamentale est bien celle de la
        // lame, et l'asymétrie du débit est ce qui donne à l'instrument son
        // grain nasillard.
        //
        // Note de méthode : l'autocorrélation, elle, annonçait −700 cents sur
        // ce même signal. Les deux estimateurs se contredisaient, et c'est le
        // SPECTRE qui a dit la vérité — un rappel que l'instrument de mesure
        // doit regarder là où l'effet est censé se produire.
        const float debit = (y_ - kFente) * std::sqrt(std::max(0.0f, pression));
        const float son = std::max(0.0f, debit);

        // BLOQUEUR DE CONTINU, ET C'EST LE PIÈGE QUE LE § 44 D'ARCHITECTURE
        // RACONTE — repayé ici avant d'être reconnu. Un débit redressé est
        // POSITIF par construction : il porte une composante continue énorme.
        // Le premier essai la retirait par une constante (`- 0,25`), ce qui
        // marche au régime établi et ment partout ailleurs : à faible
        // pression, la lame n'oscillait pas du tout et la sortie valait
        // −0,25 constant, soit un rms de 0,25 qu'on aurait pu prendre pour du
        // son. C'est mot pour mot la faute de `vsm.flute` (« ce qu'on avait
        // pris pour une auto-oscillation était une composante CONTINUE »). Un
        // bloqueur du premier ordre, lui, ne peut pas se tromper.
        dcY_ = son - dcX_ + 0.9995f * dcY_;
        dcX_ = son;

        filtre_.setCutoffHz(p.cutoff);
        filtre_.setResonance(p.resonance);
        return filtre_.process(dcY_) * env_.nextSample();
    }

private:
    /// La fente : tant que la lame est à moins que cela du cadre, rien ne passe.
    static constexpr float kFente = 0.05f;
    /// La pression à laquelle la lame est JUSTE : l'écart de hauteur se compte
    /// à partir de là, en plus comme en moins.
    static constexpr float kPressionDeReference = 0.6f;

    double sampleRate_ = 48000.0;
    float y_ = 0.0f, v_ = 0.0f;
    float dcX_ = 0.0f, dcY_ = 0.0f;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::StateVariableFilter filtre_;
    vsm::util::DeterministicRng rng_{0x52454544ULL};   // "REED"
    uint8_t note_ = 60, channel_ = 0, velocity_ = 100;
};

class ReedSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    static constexpr size_t kMaxVoices = 8;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kPressure = 1, kStiffness, kAirLoading,
        kCutoff, kResonance,
        kAttack, kDecay, kSustain, kRelease,
        kVelocityToPressure, kOutputLevel,
    };

    ReedSynth();

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
        // LA PRESSION DE CANAL EST LE SOUFFLE, et sur cette machine c'est le
        // geste principal : un accordéoniste ne joue pas au clavier seul, il
        // pousse. La brancher ailleurs qu'ici serait la gaspiller.
        if (event.kind == Kind::ChannelPressure) {
            pressionDeCanal_.store(std::clamp(event.value, 0.0f, 1.0f), std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Reed (l\'anche libre)"; }
    int activeVoiceCount() const override { return voiceManager_.activeVoiceCount(); }

private:
    void applyNoteEvent(const vsm::audio::plugin::MidiNoteEvent& event);

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kOutputLevel + 1> params_{};
    vsm::audio::engine::VoiceManager<ReedVoice, kMaxVoices> voiceManager_;
    std::atomic<float> bendSemitones_{0.0f};
    std::atomic<float> pressionDeCanal_{-1.0f};   // < 0 : aucune pression reçue
};

} // namespace vsm::plugins::reed
