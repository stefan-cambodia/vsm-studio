#pragma once
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/engine/AudioTrackSource.h"
#include "vsm/audio/engine/Metronome.h"
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

    /// Le matériau audio d'une piste, déjà décodé et découpé en clips.
    ///
    /// Publié comme les chaînes d'effets : un pointeur partagé, échangé
    /// atomiquement. Le thread audio ne fait que le lire ; le décodage, le
    /// rééchantillonnage et la conversion des clips en échantillons ont eu lieu
    /// sur le thread de l'interface. `nullptr` efface le matériau de la piste.
    void setTrackAudio(size_t trackIndex, std::shared_ptr<const AudioTrackSource> source); // thread UI

    /// Notes qui n'ont PAS été jouées faute de place dans le tableau de
    /// travail d'un sous-segment. Doit rester à zéro ; toute autre valeur est
    /// un morceau qu'on n'entend pas en entier.
    uint64_t droppedNoteEvents() const { return droppedNoteEvents_.load(std::memory_order_relaxed); }
    /// Événements de contrôle livrés à une machine qui a répondu « je ne sais
    /// pas quoi en faire ». Ce n'est PAS une anomalie du moteur -- une boîte à
    /// rythmes n'a que faire d'un pitch bend -- mais c'est ce qui permet à
    /// l'interface de dire pourquoi une modulation ne s'entend pas.
    uint64_t ignoredControlEvents() const { return ignoredControlEvents_.load(std::memory_order_relaxed); }
    /// LE MÉTRONOME. Éteint par défaut, et il doit le rester pour le rendu
    /// hors ligne : un clic dans un fichier exporté serait une faute grossière.
    /// Le rendu hors ligne monte son propre graphe et ne l'allume jamais ;
    /// c'est l'application qui décide, et elle seule.
    void setMetronomeEnabled(bool on) { metronomeEnabled_.store(on, std::memory_order_relaxed); }
    bool metronomeEnabled() const { return metronomeEnabled_.load(std::memory_order_relaxed); }
    void setMetronomeLevel(float level) { metronome_.setLevel(level); }

    void resetEventCounters() {
        droppedNoteEvents_.store(0, std::memory_order_relaxed);
        ignoredControlEvents_.store(0, std::memory_order_relaxed);
    }

    /// PLAFOND du nombre de bus de départ, et non plus leur nombre (D4.2).
    ///
    /// Le nombre RÉEL vient du projet (`Project::sends`) : c'est lui qui décide
    /// combien de départs existent, et lui qui dit ce que chacun contient. Ce
    /// plafond ne subsiste que parce que le chemin temps réel n'alloue pas --
    /// les tampons des bus sont dimensionnés une fois dans `prepare()`. Huit
    /// est très au-delà de ce qu'un mixage demande (une réverbération courte,
    /// une longue, un delay, une chambre), et le franchir est COMPTÉ plutôt
    /// que d'être ignoré en silence.
    static constexpr size_t kMaxSends = 8;
    /// PLAFOND du nombre de groupes qui peuvent recevoir des pistes, pour la
    /// même raison que celui des départs : leurs tampons sont dimensionnés une
    /// fois dans `prepare()`, le chemin temps réel n'allouant pas. Un projet
    /// peut avoir davantage de PISTES de groupe ; au-delà du plafond, les
    /// suivantes vont au master et c'est COMPTÉ.
    static constexpr size_t kMaxGroups = 8;
    /// Groupes qui n'ont pas pu recevoir leurs pistes faute de tampon. Doit
    /// rester à zéro ; toute autre valeur est un routage qui ne fait pas ce
    /// qu'il dit.
    uint64_t droppedGroupBuses() const { return droppedGroupBuses_.load(std::memory_order_relaxed); }
    /// Nombre de bus réellement déclarés par le projet publié.
    size_t activeSendCount() const { return activeSends_.load(std::memory_order_acquire); }
    /// Nombre de fois qu'un projet a déclaré plus de départs que le plafond.
    /// Doit rester à zéro ; toute autre valeur est un départ qu'on a réglé et
    /// qui ne sonne pas.
    uint64_t droppedSendBuses() const { return droppedSendBuses_.load(std::memory_order_relaxed); }
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
    double loopStartSeconds() const { return loopStartSeconds_.load(std::memory_order_acquire); }
    double loopEndSeconds() const { return loopEndSeconds_.load(std::memory_order_acquire); }

    /// NOMBRE DE FOIS QUE LA BOUCLE S'EST REFERMÉE depuis le dernier `seekSeconds`.
    ///
    /// C'est ce qui rend l'enregistrement en boucle possible (D3.5). Une passe
    /// et la suivante occupent EXACTEMENT les mêmes positions sur la ligne de
    /// temps : la date d'une note ne dit donc pas à quelle passe elle
    /// appartient, et deux passes se mélangeraient en une bouillie. Le
    /// compteur, lui, les sépare sans ambiguïté.
    uint64_t loopWrapCount() const { return loopWrapCount_.load(std::memory_order_acquire); }

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

    /// Traite les pistes de GROUPE une fois que tous leurs membres ont écrit
    /// dans leur tampon : inserts du groupe, puis volume/panoramique, puis
    /// mélange au master et alimentation des départs. Appelée une fois par
    /// bloc, comme les bus de départ, et pour la même raison -- un insert de
    /// groupe traite le groupe entier, pas chaque sous-segment d'automation.
    void renderGroupBuses(const GraphSnapshot& snapshot, bool anySolo, int numSamples,
                           float* outputL, float* outputR);
    /// L'index du tampon de groupe d'une piste, ou -1 si elle va au master.
    int groupBufferFor(const vsm::sequencer::Project& project, size_t trackIndex) const;

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
    std::atomic<uint64_t> loopWrapCount_{0};

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
    std::array<SendBus, kMaxSends> sends_;
    std::array<std::vector<float>, kMaxSends> sendL_, sendR_;
    std::atomic<size_t> activeSends_{0};
    /// Un bit par bus : 1 = pré-fader. Un masque plutôt qu'une lecture du
    /// projet dans la boucle de mixage -- le chemin audio a déjà le snapshot
    /// sous la main, mais un entier se lit une fois par bloc là où le vecteur
    /// se relirait par piste et par sous-segment.
    std::atomic<uint32_t> preFaderMask_{0};

    // Tampons des groupes : une piste routée vers un groupe s'y mélange au lieu
    // d'aller au master, et le groupe est traité APRÈS, une fois que tous ses
    // membres ont écrit. D'où deux passes, et non un ordre de pistes malin :
    // l'ordre des pistes appartient à l'utilisateur, pas au moteur.
    std::array<std::vector<float>, kMaxGroups> groupL_, groupR_;
    std::atomic<uint64_t> droppedGroupBuses_{0};
    /// Plafond du tableau de travail des événements de note, PAR PISTE ET PAR
    /// SOUS-SEGMENT. Il existe parce que le chemin temps réel n'alloue pas.
    /// Relevé de 256 à 1024 : à 48 kHz, un sous-segment d'automation dure
    /// 1,3 ms, et 256 notes en 1,3 ms n'arrivent pas -- mais une piste de
    /// batterie reconstruite porte plus de quatre mille notes, et un plafond
    /// qu'on n'a jamais mesuré est un plafond dont on ignore s'il tient.
    /// Depuis, le franchir est COMPTÉ (voir `droppedNoteEvents()`).
    static constexpr int kMaxEventsPerBlock = 1024;
    std::vector<vsm::audio::plugin::MidiNoteEvent> scratchEvents_;

    /// Ce que le moteur n'a pas pu jouer, et qu'il ne cache plus. Écrits
    /// depuis le thread audio, lus par l'interface : `relaxed` suffit, aucune
    /// autre donnée n'en dépend.
    std::array<std::atomic<std::shared_ptr<const AudioTrackSource>>, kMaxTracks> audioSources_;

    Metronome metronome_;
    std::atomic<bool> metronomeEnabled_{false};

    std::atomic<uint64_t> droppedSendBuses_{0};
    std::atomic<uint64_t> droppedNoteEvents_{0};
    std::atomic<uint64_t> ignoredControlEvents_{0};

    static constexpr size_t kLiveQueueCapacity = 256;
    static constexpr size_t kMaxLiveEventsPerBlock = 64;
    std::array<vsm::audio::util::LockFreeRingBuffer<LiveNoteEvent, kLiveQueueCapacity>, kNumLiveSources> liveQueues_;
    std::array<LiveNoteEvent, kMaxLiveEventsPerBlock> drainedLive_{};
    int drainedLiveCount_ = 0;
};

} // namespace vsm::audio::engine
