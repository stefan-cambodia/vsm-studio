#pragma once
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/io/WavFileReader.h"
#include "vsm/audio/plugin/ISampleLoader.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace vsm::plugins::sampler {

/// Lecteur d'échantillons — la machine qui permet de reproduire ce qu'aucune
/// synthèse ne reproduit : une batterie enregistrée, une basse jouée, un piano.
///
/// RAISON D'ÊTRE (docs/CDC-machines-manquantes.md) : sur un morceau réel, les
/// stems de batterie et de basse n'ont aujourd'hui aucune machine cible. Le
/// projet d'analyse, lui, découpe déjà les coups de batterie : ces extraits
/// SONT les échantillons. La reconstruction du percussif devient alors quasi
/// exacte, là où la synthèse échouait.
///
/// CONVENTION DE JEU, empruntée aux boîtes à rythmes plutôt qu'aux samplers
/// mélodiques : la note MIDI SÉLECTIONNE un emplacement, elle ne transpose
/// pas. Un pad joue son son, à sa hauteur, quelle que soit la touche qui le
/// déclenche ; l'accord se règle par le paramètre `Tune` de l'emplacement.
/// C'est ce qu'attend un kit de batterie -- et transposer un coup de caisse
/// claire selon la touche produirait n'importe quoi.
///
/// SEIZE EMPLACEMENTS, comme le prévoit le cahier des charges. La version
/// précédente s'arrêtait à huit, non par manque de place en mémoire mais parce
/// que la façade ne savait pas les montrer : seize emplacements à sept
/// paramètres font cent douze commandes, illisibles alignées.
///
/// Le blocage était donc côté AFFICHAGE, et c'est là qu'il a été levé -- la
/// façade présente les emplacements en deux rangées de huit, chacune réduite
/// aux quatre réglages qui se jouent (niveau, accord, décroissance,
/// panoramique). Les trois autres (note de déclenchement, point de départ,
/// groupe de coupure) sont des réglages de CONFIGURATION, posés une fois par
/// l'analyse : ils restent accessibles par le panneau générique et sont
/// déclarés omis, avec leur raison.
class SamplerSynth : public vsm::audio::plugin::ISynthPlugin,
                      public vsm::audio::plugin::ISampleLoader {
public:
    static constexpr int kSlotCount = 16;
    /// Voix simultanées. Une par emplacement PLUS une marge : sur un kit
    /// dense, un coup peut encore résonner quand le suivant part, et couper la
    /// queue d'une cymbale pour jouer une charleston s'entendrait.
    static constexpr int kMaxVoices = 24;

    enum ParamIds : vsm::audio::plugin::ParamId {
        kMasterLevel = 1,
        // Les paramètres d'emplacement occupent des plages de 10, pour qu'un
        // ajout ultérieur ne décale rien : slot 0 -> 10..19, slot 1 -> 20..29.
        kSlotBase = 10,
        kSlotStride = 10,
        kSlotNote = 0,   ///< note MIDI qui déclenche l'emplacement
        kSlotTune = 1,   ///< accord en demi-tons
        kSlotLevel = 2,
        kSlotPan = 3,
        kSlotDecay = 4,  ///< 0 = jouer l'échantillon entier
        kSlotStart = 5,  ///< point de départ, en proportion du fichier
        kSlotChoke = 6,  ///< groupe de coupure (0 = aucun)
    };

    static vsm::audio::plugin::ParamId slotParam(int slot, int which) {
        // Calcul en entier SIGNÉ puis conversion unique : mélanger des
        // constantes non signées (les ParamId) et des indices signés
        // provoquerait des conversions implicites que -Wsign-conversion
        // signale à juste titre.
        const int base = static_cast<int>(kSlotBase);
        const int stride = static_cast<int>(kSlotStride);
        return static_cast<vsm::audio::plugin::ParamId>(base + slot * stride + which);
    }

    SamplerSynth();

    // --- ISynthPlugin ------------------------------------------------------
    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outputL, float* outputR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float value) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override { return parameterList_; }
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Sampler (16 emplacements)"; }
    int activeVoiceCount() const override;

    // --- ISampleLoader -----------------------------------------------------
    bool loadSample(int slot, const std::string& path, std::string& outError) override;
    void clearSample(int slot) override;
    std::string samplePath(int slot) const override;
    int slotCount() const override { return kSlotCount; }

    /// Publie un échantillon déjà en mémoire (utilisé par les tests et par un
    /// futur import qui découperait les coups lui-même, sans passer par un
    /// fichier).
    void setSample(int slot, vsm::audio::io::SampleBufferPtr sample);

private:
    struct Voice {
        vsm::audio::io::SampleBufferPtr sample; // maintient la donnée en vie pendant la lecture
        double position = 0.0;      // en trames du FICHIER
        double increment = 1.0;
        float level = 1.0f;
        float panLeft = 0.7071f, panRight = 0.7071f;
        float envelope = 1.0f;
        float envelopeDecay = 0.0f; // 0 = pas de decay imposé
        int slot = -1;
        int chokeGroup = 0;
        bool active = false;
    };

    void triggerSlot(int slot, uint8_t velocity);
    float slotValue(int slot, int which) const {
        return params_[slotParam(slot, which)].load(std::memory_order_relaxed);
    }

    double sampleRate_ = 48000.0;
    vsm::audio::plugin::ParameterList parameterList_;
    mutable std::array<std::atomic<float>, kSlotBase + kSlotCount * kSlotStride> params_{};

    /// Publication atomique : le chargement de fichier a lieu sur le thread UI,
    /// la lecture sur le thread audio. Un shared_ptr échangé atomiquement suffit
    /// -- le thread audio capture le pointeur au déclenchement et le garde en
    /// vie tant que la voix joue, même si l'emplacement est rechargé entre-temps.
    std::array<std::atomic<vsm::audio::io::SampleBufferPtr>, kSlotCount> slots_{};
    std::array<std::string, kSlotCount> slotPaths_; // thread UI uniquement
    std::array<Voice, kMaxVoices> voices_{};
    size_t nextVoice_ = 0;
};

} // namespace vsm::plugins::sampler
