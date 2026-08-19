#pragma once
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/engine/AutomationLane.h"
#include "vsm/audio/engine/MasterBus.h"
#include "vsm/audio/engine/ReferenceTrack.h"
#include "vsm/audio/engine/Mixer.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/audio/util/LockFreeRingBuffer.h"
#include "vsm/sequencer/PlaybackScheduler.h"
#include "vsm/sequencer/Project.h"
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace vsm::audio::engine {

/// Assemble Project (vsm_core) + instances de synthés (une par piste,
/// résolues via PluginRegistry) + Mixer, et sait produire des blocs audio
/// stéréo de façon déterministe.
///
/// C'est le SEUL endroit qui sait "rendre" un projet en audio : le callback
/// temps réel de l'app standalone et un futur rendu offline (export WAV)
/// appellent tous les deux processBlock() -- jamais deux chemins de calcul
/// différents, pour qu'un export soit fidèle à ce qu'on a entendu en
/// composant (voir ARCHITECTURE.md section 5).
///
/// RÈGLE : processBlock() tourne dans le thread audio temps réel. Aucune
/// allocation, aucun lock explicite. Tout le reste (setProject,
/// setTrackInstrument, addAutomationLane...) tourne côté thread UI et PEUT
/// allouer -- la communication vers le thread audio passe uniquement par
/// des std::atomic, jamais par une donnée partagée modifiée en place :
///   - `snapshot_` publie D'UN COUP le Project (pour tempo/volume/pan/
///     mute/solo) ET le planning déterministe précalculé qui en découle,
///     pour qu'ils soient TOUJOURS cohérents entre eux côté audio thread.
///   - `instruments_[i]` publie l'instance de plugin de la piste i,
///     indépendamment (changer d'instrument n'invalide pas le snapshot).
class ProcessGraph {
public:
    static constexpr size_t kMaxTracks = MeterBank::kMaxChannels;

    void prepare(double sampleRate, int maxBlockSize);

    /// Thread UI. Reconstruit le planning déterministe du projet
    /// (PlaybackScheduler) et le publie pour le thread audio. N'affecte le
    /// rendu qu'à partir du prochain processBlock().
    void setProject(const vsm::sequencer::Project& project);

    /// Thread UI. Assigne (pluginId non vide) ou retire (pluginId vide) un
    /// plugin instrument à une piste. `trackIndex` doit correspondre à
    /// l'index de piste tel qu'utilisé par setProject().
    void setTrackInstrument(size_t trackIndex, const std::string& pluginId);
    /// Assigne une instance d'instrument DÉJÀ construite, sans passer par le
    /// registre. C'est ce qui permet à une machine que le moteur ne connaît
    /// pas -- un plugin CLAP tiers chargé à l'exécution, un instrument de test
    /// -- d'entrer dans le graphe : elle arrive habillée en ISynthPlugin, et
    /// le graphe n'a pas à savoir d'où elle vient. Thread UI, comme
    /// setTrackInstrument().
    void setTrackInstrumentInstance(size_t trackIndex, vsm::audio::plugin::SynthPluginPtr instrument,
                                     const std::string& identifier = "external");

    std::string trackInstrumentId(size_t trackIndex) const;
    vsm::audio::plugin::ISynthPlugin* trackInstrument(size_t trackIndex) const;

    /// Règle un paramètre de l'instrument d'une piste de façon thread-safe :
    /// capture le shared_ptr (le maintient en vie pendant l'appel), puis
    /// appelle setParameter (atomique). Sûr depuis n'importe quel thread
    /// (UI, MIDI input...) -- utilisé par le MIDI Learn. No-op si la piste
    /// n'a pas d'instrument.
    void setInstrumentParameter(size_t trackIndex, vsm::audio::plugin::ParamId paramId, float value);

    void addAutomationLane(AutomationLane lane);                 // thread UI
    void clearAutomationLanes();                                 // thread UI
    /// Remplace TOUTES les lanes d'un coup (usage principal de l'éditeur
    /// d'automation : reconstruire la liste puis publier). Thread UI.
    void setAutomationLanes(std::vector<AutomationLane> lanes);  // thread UI

    // --- Effets d'insert par piste + sends (sections 5 et 15) -------------
    /// Chaîne d'inserts d'une piste : appliquée à la sortie stéréo de
    /// l'instrument AVANT le mixage (volume/pan). Les effets doivent être
    /// prepare()és par l'appelant AVANT publication (jamais sur le thread
    /// audio). Publiée atomiquement (comme instruments_). nullptr = aucun.
    using EffectChain = std::vector<std::shared_ptr<vsm::audio::effect::IAudioEffect>>;
    void setTrackEffectChain(size_t trackIndex, std::shared_ptr<const EffectChain> chain); // thread UI

    static constexpr size_t kNumSends = 2;
    /// Effet d'un bus auxiliaire (ex. reverb sur le send A). prepare() par
    /// l'appelant avant publication. Le signal de retour est ajouté au master.
    void setSendEffect(size_t busIndex, std::shared_ptr<vsm::audio::effect::IAudioEffect> effect); // thread UI
    void setSendReturn(size_t busIndex, float gain); // thread UI

    // --- Notes "live" (écoute au clavier, saisie MIDI, piano roll) --------
    //
    // Permet de jouer une note IMMÉDIATEMENT sur l'instrument d'une piste,
    // sans passer par le planning du projet : c'est ce qui fait sonner un
    // clic sur le clavier du piano roll ou une touche d'un clavier MIDI
    // branché. Les événements traversent une file lock-free (aucun mutex,
    // aucune allocation côté audio, règle temps réel section 13) et sont
    // joués au début du bloc suivant -- soit une latence maximale d'un bloc,
    // imperceptible aux tailles usuelles (10,7 ms à 512 échantillons).
    //
    // UNE FILE PAR SOURCE, et c'est important : LockFreeRingBuffer est
    // strictement un producteur / un consommateur. L'UI et le thread MIDI
    // sont deux threads différents ; les faire écrire dans la même file
    // serait un bug de concurrence silencieux, qui ne se manifesterait que
    // rarement et sous charge. Chaque source a donc la sienne.
    enum class LiveNoteSource { Ui = 0, MidiInput = 1 };
    static constexpr size_t kNumLiveSources = 2;

    /// Thread UI ou thread MIDI selon `source` (jamais deux threads sur la
    /// MÊME source). Renvoie false si la file est pleine -- l'événement est
    /// alors abandonné plutôt que de bloquer, un choix délibéré : mieux vaut
    /// perdre une note d'écoute qu'introduire une attente sur le chemin audio.
    bool sendLiveNote(LiveNoteSource source, size_t trackIndex, uint8_t note,
                      uint8_t velocity, bool noteOn);

    // --- Boucle de lecture -------------------------------------------------
    //
    // La boucle appartient au moteur AUDIO, et pas seulement au transport MIDI
    // de la Phase 1 : c'est l'horloge audio qui fait référence désormais (voir
    // ARCHITECTURE.md § 6), donc c'est elle qui doit reboucler -- sinon le son
    // et la position affichée se contrediraient dès le premier tour.
    //
    // Le rebouclage est ÉCHANTILLON-EXACT : le bloc est découpé à la frontière
    // de boucle, pas arrondi à la taille de bloc. Un motif d'une mesure rejoué
    // mille fois ne dérive donc pas d'un seul échantillon.
    void setLoopRegion(double startSeconds, double endSeconds, bool active); // thread UI
    bool isLoopActive() const { return loopActive_.load(std::memory_order_acquire); }

    void seekSeconds(double seconds);  // thread UI
    void setPlaying(bool playing);     // thread UI
    bool isPlaying() const { return playing_.load(std::memory_order_acquire); }
    double currentSeconds() const { return currentSeconds_.load(std::memory_order_acquire); }

    /// Rendu temps réel OU offline : à appeler en boucle depuis le callback
    /// audio (Phase 2) ou depuis OfflineRenderer (export WAV). Écrit
    /// EXACTEMENT numSamples dans outputL/outputR (déjà alloués par
    /// l'appelant, jamais par ProcessGraph). Avance currentSeconds_ de
    /// numSamples/sampleRate si en lecture.
    void processBlock(float* outputL, float* outputR, int numSamples);

    float readMeterPeak(size_t trackIndex) const { return meters_.readPeak(trackIndex); }
    int totalActiveVoices() const;

    /// Tranche master appliquée au bus stéréo final. Désactivée par défaut :
    /// tant qu'elle n'est pas activée, le rendu est identique à l'historique.
    /// Thread UI pour la configurer, lue côté audio dans processBlock().
    MasterBus& masterBus() { return masterBus_; }
    const MasterBus& masterBus() const { return masterBus_; }

    /// Piste de référence : l'enregistrement d'origine, pour l'écoute A/B.
    /// Configurée depuis le thread UI, lue dans processBlock().
    ///
    /// Elle est mélangée APRÈS le bus master, et le rendu hors ligne la coupe :
    /// voir `ReferenceTrack` pour le détail de ces deux règles.
    ReferenceTrack& referenceTrack() { return referenceTrack_; }
    const ReferenceTrack& referenceTrack() const { return referenceTrack_; }

private:
    struct GraphSnapshot {
        vsm::sequencer::Project project;
        std::vector<vsm::sequencer::ScheduledEvent> schedule;
    };

    struct LiveNoteEvent {
        uint32_t trackIndex = 0;
        uint8_t note = 60;
        uint8_t velocity = 100;
        bool noteOn = true;
    };

    /// Vide les files de notes live dans drainedLive_ (début de bloc).
    void drainLiveNotes();

    /// Rend [sampleStart, sampleStart+count) en tenant compte de l'automation
    /// (découpage en sous-segments) -- extrait de processBlock pour que le
    /// rebouclage puisse redécouper le bloc sans dupliquer cette logique.
    void renderSpan(const GraphSnapshot& snapshot, bool anySolo, int sampleStart, int sampleCount,
                    double startSeconds, float* outputL, float* outputR,
                    const std::vector<AutomationLane>* lanes);

    /// Rend et mixe toutes les pistes pour une sous-plage [sampleStart,
    /// sampleStart+sampleCount) du bloc, en filtrant les événements sur
    /// [rangeStartSeconds, rangeStartSeconds + sampleCount/sr). Accumule les
    /// sends et met à jour blockPeak_ (max par piste). Extrait de processBlock
    /// pour permettre le découpage en sous-segments (automation sample-accurate).
    /// `includeScheduledEvents` = false sert au rendu À L'ARRÊT : on veut que
    /// les notes d'écoute sonnent (et que les voix déjà déclenchées finissent
    /// leur release) sans rejouer le planning du projet, dont la position ne
    /// bouge pas.
    void renderTrackRange(const GraphSnapshot& snapshot, bool anySolo,
                          int sampleStart, int sampleCount, double rangeStartSeconds,
                          float* outputL, float* outputR, bool includeScheduledEvents = true);

    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 512;
    std::array<float, kMaxTracks> blockPeak_{}; // pic par piste, cumulé sur les sous-segments

    std::atomic<std::shared_ptr<const GraphSnapshot>> snapshot_;
    std::array<std::atomic<std::shared_ptr<vsm::audio::plugin::ISynthPlugin>>, kMaxTracks> instruments_{};
    std::array<std::string, kMaxTracks> instrumentIds_; // bookkeeping thread UI uniquement

    // Lanes d'automation publiées via un snapshot atomique (comme snapshot_
    // et instruments_) : le thread audio ne fait que load() un shared_ptr vers
    // un vecteur IMMUABLE ; l'UI publie une nouvelle liste à chaque édition.
    // Plus de course de données possible entre édition et lecture.
    std::atomic<std::shared_ptr<const std::vector<AutomationLane>>> automationLanes_;
    MeterBank meters_;
    MasterBus masterBus_;
    ReferenceTrack referenceTrack_;

    std::atomic<double> currentSeconds_{0.0};
    std::atomic<bool> playing_{false};

    std::atomic<double> loopStartSeconds_{0.0};
    std::atomic<double> loopEndSeconds_{0.0};
    std::atomic<bool> loopActive_{false};

    // Notes actuellement tenues par chaque piste, suivies au fil des
    // événements envoyés aux instruments. Sert UNIQUEMENT au rebouclage : en
    // sautant de la fin de la boucle à son début, une note dont le NoteOff se
    // trouve après la fin de boucle ne le recevrait jamais et sonnerait
    // indéfiniment. On les relâche donc explicitement au moment du saut.
    std::array<std::array<bool, 128>, kMaxTracks> soundingNotes_{};
    bool wrapNoteOffPending_ = false;

    // Buffers de travail, pré-alloués une seule fois dans prepare() --
    // jamais redimensionnés dans processBlock() (règle realtime section 13).
    std::vector<float> scratchMonoL_;
    std::vector<float> scratchStereoL_, scratchStereoR_; // rendu stéréo par piste (avant inserts+mix)

    // Chaînes d'inserts par piste (publiées atomiquement).
    std::array<std::atomic<std::shared_ptr<const EffectChain>>, kMaxTracks> effectChains_{};

    // Bus de sends auxiliaires : effet + gain de retour + buffers stéréo.
    struct SendBus {
        std::atomic<std::shared_ptr<vsm::audio::effect::IAudioEffect>> effect{nullptr};
        std::atomic<float> returnGain{1.0f};
    };
    std::array<SendBus, kNumSends> sends_;
    std::array<std::vector<float>, kNumSends> sendL_, sendR_;
    static constexpr int kMaxEventsPerBlock = 256;
    std::vector<vsm::audio::plugin::MidiNoteEvent> scratchEvents_;

    static constexpr size_t kLiveQueueCapacity = 256;
    static constexpr size_t kMaxLiveEventsPerBlock = 64;
    std::array<vsm::audio::util::LockFreeRingBuffer<LiveNoteEvent, kLiveQueueCapacity>, kNumLiveSources> liveQueues_;
    std::array<LiveNoteEvent, kMaxLiveEventsPerBlock> drainedLive_{};
    int drainedLiveCount_ = 0;
};

} // namespace vsm::audio::engine
