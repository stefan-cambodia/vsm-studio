#pragma once
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/engine/VoiceManager.h"
#include "vsm/audio/io/WavFileReader.h"
#include "vsm/audio/plugin/IMultisampleBank.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vsm::plugins::multisample {

/// Lecteur MULTI-ÉCHANTILLONS : l'acoustique mélodique, par report honnête.
///
/// RAISON D'ÊTRE, et elle est CHIFFRÉE (docs/CDC-multisample.md § 1). Clair de
/// Lune reconstruit par la chaîne complète le 23/08/2026 : les huit premières
/// machines finissent entre 0,2590 et 0,3217, une photo-finish qui ne dit pas
/// « le supersaw ressemble à un piano » mais « aucune ne lui ressemble, et
/// toutes s'en écartent autant ». L'essentiel de la distance globale (1,639)
/// était rattrapé par l'automation de coupure -- six cent six points pour
/// passer de 8,34 à 1,64. Le parc COMPENSAIT au lieu de REPRODUIRE.
///
/// CE QU'ELLE N'EST PAS. Ce n'est pas `vsm.sampler` élargi. Le sampler est
/// percussif par construction : un échantillon par emplacement, déclenché par
/// note fixe, sans transposition ni couche de vélocité, parce que transposer
/// un coup de caisse claire selon la touche produirait n'importe quoi.
/// L'étendre le dénaturerait. Ici, la note SÉLECTIONNE une zone ET la
/// transpose, et la vélocité choisit une couche.
///
/// CE QU'ELLE N'ESSAIE PAS D'ÊTRE. Un piano de synthèse (marteau-corde,
/// résonance sympathique, table d'harmonie) est un sujet de recherche entier ;
/// `vsm.piano` en donne déjà une version modélisée et assumée comme telle.
/// Cette machine-ci fait l'autre choix, celui que le § 27 d'ARCHITECTURE.md
/// autorise explicitement : reporter l'enregistrement et le DIRE. Un lecteur
/// d'échantillons honnête vaut mieux qu'une modélisation vantée.
///
/// PAS DE BANQUE DANS LE DÉPÔT. Aucun échantillon n'est commis : les profils
/// s'installent (`tools/telecharger-profil-piano.py`). Sans profil, la machine
/// est SILENCIEUSE et le dit -- elle ne se rabat pas sur un son de synthèse,
/// ce qui masquerait l'absence d'installation derrière un son plausible.

/// Une zone chargée : la déclaration du profil, plus l'échantillon en mémoire.
struct LoadedZone {
    int program = 0;
    int lowNote = 0, highNote = 127;
    int lowVelocity = 1, highVelocity = 127;
    int rootNote = 60;
    float tuneCents = 0.0f;
    float level = 1.0f;
    bool loopEnabled = false;
    uint64_t loopStart = 0, loopEnd = 0;
    vsm::audio::io::SampleBufferPtr sample;
    std::string relativePath;
};

/// Un profil publié : immuable une fois construit, partagé par shared_ptr.
///
/// IMMUABLE PAR NÉCESSITÉ, pas par goût : une voix qui joue garde son profil
/// en vie par le shared_ptr qu'elle a capturé au déclenchement. Charger un
/// autre profil pendant qu'une note tient ne coupe donc pas cette note et ne
/// lit jamais de la mémoire libérée.
struct LoadedProfile {
    std::string name;
    std::string attribution;
    std::string sourcePath;
    std::vector<std::string> programNames;
    std::vector<LoadedZone> zones;
    size_t memoryBytes = 0;

    /// Première zone qui contient (programme, note, vélocité) -- l'ordre du
    /// profil fait foi. `nullptr` si aucune : la note ne sonne pas, et c'est
    /// la bonne réponse. Emprunter la zone voisine ferait jouer un son faux
    /// que personne ne rattacherait au trou dans le profil.
    const LoadedZone* select(int program, int note, int velocity) const;

    int programCount() const;
};

using ProfilePtr = std::shared_ptr<const LoadedProfile>;

/// Une voix : une zone, un pointeur de lecture, une enveloppe, un filtre.
class MultisampleVoice {
public:
    void prepare(double sampleRate);

    // --- contrat attendu par VoiceManager ---------------------------------
    bool isActive() const { return active_; }
    uint8_t note() const { return note_; }
    uint8_t channel() const { return channel_; }
    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void noteOff(uint8_t) { env_.noteOff(); }

    /// Attache la zone à jouer. Appelé juste après `noteOn`, une fois que
    /// l'appelant a trouvé la zone -- lui seul connaît le profil courant.
    void attach(ProfilePtr profile, const LoadedZone& zone, double engineRate,
                float velocityGain, float globalTuneCents);

    void setEnvelope(const vsm::audio::dsp::AdsrSettings& settings) { env_.setSettings(settings); }
    /// Coefficient d'un passe-bas à un pôle, ou <= 0 pour « neutre ».
    void setToneCoefficient(float coefficient) { toneCoefficient_ = coefficient; }
    /// Rapport de la molette de hauteur (2^(demi-tons/12)), poussé une fois
    /// par bloc. À 1,0 exactement, `increment_ * 1.0` est exact : l'empreinte
    /// ne bouge pas d'un bit sans molette.
    void setBendRatio(double ratio) { bendRatio_ = ratio; }

    /// Rend un échantillon stéréo. Additionne dans `outL`/`outR`.
    void render(float& outL, float& outR);

private:
    /// Lit une trame du fichier, en repliant l'index dans la boucle si la zone
    /// en déclare une. C'est ce repli qui permet à l'interpolation cubique de
    /// rester continue AU POINT DE BOUCLAGE : ses quatre prises voisines sont
    /// alors celles que la boucle enchaîne réellement, et non des zéros pris
    /// au-delà de la fin du fichier -- lesquels produiraient précisément le
    /// clic que le § 3 du cahier des charges interdit.
    float frameAt(const std::vector<float>& channel, int64_t index) const;
    float interpolate(const std::vector<float>& channel, double position) const;

    ProfilePtr profile_;          // maintient l'échantillon en vie
    const LoadedZone* zone_ = nullptr;
    const vsm::audio::io::SampleBuffer* sample_ = nullptr;

    double position_ = 0.0;       // en trames du FICHIER
    double increment_ = 1.0;
    double bendRatio_ = 1.0;      // molette de hauteur, appliquée à l'avance
    float gain_ = 1.0f;
    bool active_ = false;
    uint8_t note_ = 60, channel_ = 0;

    vsm::audio::dsp::AdsrEnvelope env_;
    float toneCoefficient_ = 0.0f;
    float toneStateL_ = 0.0f, toneStateR_ = 0.0f;
};

class MultisampleSynth : public vsm::audio::plugin::ISynthPlugin,
                          public vsm::audio::plugin::IMultisampleBank {
public:
    /// Trente-deux voix, et le chiffre vient de l'usage : un piano à la pédale
    /// tient des dizaines de notes à la fois, et voler une voix encore
    /// résonnante s'entend immédiatement sur un instrument acoustique, là où
    /// un soustractif le pardonne.
    static constexpr size_t kMaxVoices = 32;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kOutputLevel = 1,
        kTune = 2,
        kAttack = 3,
        kRelease = 4,
        kToneCutoff = 5,
        kProgram = 6,
        kVelocityAmount = 7,
        kParamCount = 8,
    };

    MultisampleSynth();

    // --- ISynthPlugin ------------------------------------------------------
    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    bool handleControlEvent(const vsm::audio::plugin::MidiControlEvent& event) override {
        // La molette de hauteur re-hausse la lecture, comme sur un sampler
        // matériel ; le CC 1 est refusé en le disant (pas de LFO ici -- le
        // vibrato d'un instrument échantillonné est DANS ses échantillons).
        if (event.kind == vsm::audio::plugin::MidiControlEvent::Kind::PitchBend) {
            bendSemitones_.store(event.value, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Multisample (acoustique échantillonné)"; }
    int activeVoiceCount() const override { return voices_.activeVoiceCount(); }

    // --- IMultisampleBank --------------------------------------------------
    bool loadProfile(const vsm::audio::plugin::MultisampleProfileSpec& spec,
                     std::string& outError,
                     vsm::audio::plugin::MultisampleSampleCache* cache) override;
    void clearProfile() override;
    std::string profileName() const override;
    std::string profileSourcePath() const override;
    std::string profileAttribution() const override;
    int zoneCount() const override;
    int programCount() const override;
    size_t profileMemoryBytes() const override;

    /// Publie un profil déjà construit en mémoire. Utilisé par les tests et
    /// par l'empreinte de non-régression, qui ne doivent dépendre d'aucun
    /// fichier -- même règle que `SamplerSynth::setSample`.
    void setProfile(ProfilePtr profile);
    ProfilePtr profile() const { return profile_.load(std::memory_order_acquire); }

private:
    float param(vsm::audio::plugin::ParamId id) const {
        return params_[id].load(std::memory_order_relaxed);
    }

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kParamCount> params_{};
    // Molette de hauteur (demi-tons), même contrat que params_.
    std::atomic<float> bendSemitones_{0.0f};

    std::atomic<ProfilePtr> profile_{};
    vsm::audio::engine::VoiceManager<MultisampleVoice, kMaxVoices> voices_;
};

} // namespace vsm::plugins::multisample
