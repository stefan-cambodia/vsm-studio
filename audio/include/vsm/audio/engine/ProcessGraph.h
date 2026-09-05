#pragma once
#include "vsm/audio/effect/IAudioEffect.h"
#include "vsm/audio/engine/AudioTrackSource.h"
#include "vsm/audio/engine/Metronome.h"
#include "vsm/audio/engine/AutomationLane.h"
#include "vsm/audio/dsp/SpectrumTap.h"
#include "vsm/audio/engine/MasterBus.h"
#include "vsm/audio/engine/ReferenceTrack.h"
#include "vsm/audio/engine/Mixer.h"
#include "vsm/audio/engine/RenderThreadPool.h"
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
    /// D16.2 : les valeurs chassées que la file n'a pas pu porter. Comptées
    /// plutôt que tues -- une pédale qui manque est le genre de silence qu'on
    /// cherche des heures.
    uint64_t droppedChasedControls() const { return droppedChasedControls_.load(std::memory_order_relaxed); }
    /// D18.7b : les sorties d'instrument RÉCLAMÉES et non rendues -- une piste
    /// qui demande la sortie n° 4 d'une machine qui n'en a que deux, ou une
    /// publication de plus que le graphe ne sait porter. La piste sort alors
    /// SILENCIEUSE, et c'est le genre de silence qu'on met une soirée à ne pas
    /// comprendre : il est compté ici pour que l'interface puisse le dire.
    uint64_t droppedInstrumentOutputs() const {
        return droppedInstrumentOutputs_.load(std::memory_order_relaxed);
    }
    /// LE MÉTRONOME. Éteint par défaut, et il doit le rester pour le rendu
    /// hors ligne : un clic dans un fichier exporté serait une faute grossière.
    /// Le rendu hors ligne monte son propre graphe et ne l'allume jamais ;
    /// c'est l'application qui décide, et elle seule.
    void setMetronomeEnabled(bool on) { metronomeEnabled_.store(on, std::memory_order_relaxed); }
    bool metronomeEnabled() const { return metronomeEnabled_.load(std::memory_order_relaxed); }
    void setMetronomeLevel(float level) { metronome_.setLevel(level); }
    float metronomeLevel() const { return metronome_.level(); }

    /// D16.6 — QUAND LE CLIC BAT, au-delà de l'interrupteur (Metronome Setup
    /// de Cubase, le Count-in de Live).
    ///
    /// `countInOnly` : le clic ne bat QUE pendant le décompte -- on veut
    /// entrer en mesure et ne plus rien entendre ensuite. `recordOnly` : il
    /// ne bat que pendant l'enregistrement -- c'est là qu'on en a besoin, et
    /// nulle part ailleurs. Les deux se cumulent, et AUCUN des deux ne fait
    /// taire le DÉCOMPTE : un décompte qu'on n'entend pas ne compte rien,
    /// c'est sa seule raison d'être, et cette règle-là ne se règle pas.
    void setMetronomeCountInOnly(bool actif) {
        metronomeCountInOnly_.store(actif, std::memory_order_relaxed);
    }
    bool metronomeCountInOnly() const {
        return metronomeCountInOnly_.load(std::memory_order_relaxed);
    }
    void setMetronomeRecordOnly(bool actif) {
        metronomeRecordOnly_.store(actif, std::memory_order_relaxed);
    }
    bool metronomeRecordOnly() const {
        return metronomeRecordOnly_.load(std::memory_order_relaxed);
    }
    /// L'application dit au graphe qu'elle enregistre : lui seul sait si le
    /// clic doit battre, et il ne peut pas le deviner.
    void setRecording(bool actif) { recording_.store(actif, std::memory_order_relaxed); }

    void resetEventCounters() {
        droppedNoteEvents_.store(0, std::memory_order_relaxed);
        ignoredControlEvents_.store(0, std::memory_order_relaxed);
        droppedInstrumentOutputs_.store(0, std::memory_order_relaxed);
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

    /// L'ÉTAT DU TRANSPORT à un instant de la ligne de temps (D7.4), tel qu'il
    /// est livré aux plugins tiers juste avant qu'ils traitent.
    ///
    /// STATIQUE ET PRENANT LE PROJET, plutôt qu'une méthode lisant l'état du
    /// graphe : c'est une CONVERSION -- des secondes vers un tempo, une
    /// signature et une position en noires -- et une conversion se vérifie sans
    /// moteur, sans carte son et sans plugin. Le graphe s'en sert ; le test
    /// aussi, sur les mêmes lignes.
    static vsm::audio::plugin::TransportInfo transportFor(const vsm::sequencer::Project& project,
                                                           double seconds, bool playing);
    double currentSeconds() const { return currentSeconds_.load(std::memory_order_acquire); }

    /// Rendu temps réel OU offline : à appeler en boucle depuis le callback
    /// audio (Phase 2) ou depuis OfflineRenderer (export WAV). Écrit
    /// EXACTEMENT numSamples dans outputL/outputR (déjà alloués par
    /// l'appelant, jamais par ProcessGraph). Avance currentSeconds_ de
    /// numSamples/sampleRate si en lecture.
    void processBlock(float* outputL, float* outputR, int numSamples);

    // --- RENDU MULTICŒUR (D8.1) -------------------------------------------
    //
    // Une piste ne dépend d'aucune autre tant qu'elle n'est pas MÉLANGÉE : son
    // instrument, son matériau audio et ses inserts ne lisent qu'elle. C'est
    // ce qui rend le rendu parallélisable -- et c'est aussi ce qui dit où
    // s'arrête le parallélisme. Le mixage vers le master, les groupes, les
    // départs et les mètres reste SÉQUENTIEL et dans l'ordre de rendu : mis en
    // désordre, il changerait le dernier bit d'un mixage sans rien y gagner,
    // puisqu'additionner trente-deux tampons ne coûte rien à côté de les
    // calculer.
    //
    // LE RÉSULTAT EST DONC IDENTIQUE AU BIT PRÈS, quel que soit le nombre de
    // threads. Un test le vérifie ; sans cette propriété, un export ne
    // reproduirait pas ce qu'on a entendu dès qu'une machine changerait de
    // nombre de cœurs, ce qui ruinerait la règle de l'ARCHITECTURE.md § 5.

    /// Thread UI, et JAMAIS pendant que le rendu tourne : crée et détruit des
    /// threads. `workerCount` compte les threads AUXILIAIRES -- le thread audio
    /// travaille aussi. Zéro rétablit exactement le chemin mono-cœur.
    void setRenderThreadCount(size_t workerCount);
    size_t renderThreadCount() const { return renderPool_.workerCount(); }

    /// COMBIEN DE SEGMENTS ONT RÉELLEMENT ÉTÉ RENDUS EN PARALLÈLE.
    ///
    /// Ce compteur n'existe pas pour l'interface : il existe pour que le test
    /// d'identité au bit près puisse distinguer « les deux chemins donnent le
    /// même son » de « on a mesuré deux fois le même chemin ». Les conditions
    /// qui font retomber le rendu en séquentiel (chaîne latérale, segment
    /// court, trop peu de pistes) sont assez nombreuses pour qu'un test muet
    /// sur ce point ne prouve rien.
    uint64_t parallelSpansRendered() const {
        return parallelSpans_.load(std::memory_order_relaxed);
    }
    /// Ce qu'on prend par défaut sur cette machine (voir RenderThreadPool).
    static size_t recommendedRenderThreadCount() {
        return RenderThreadPool::recommendedWorkerCount();
    }

    float readMeterPeak(size_t trackIndex) const { return meters_.readPeak(trackIndex); }
    /// Valeur EFFICACE (RMS) de la piste sur le dernier bloc. La crête dit si
    /// ça écrête ; le RMS dit si c'est fort — et c'est le second qu'on cherche
    /// quand on équilibre un mixage.
    float readMeterRms(size_t trackIndex) const { return meters_.readRms(trackIndex); }
    /// Corrélation de phase entre les deux canaux de la piste, de -1 à +1.
    /// Négative, la piste disparaît en mono.
    float readMeterCorrelation(size_t trackIndex) const { return meters_.readCorrelation(trackIndex); }
    int totalActiveVoices() const;

    /// Tranche master appliquée au bus stéréo final. Désactivée par défaut :
    /// tant qu'elle n'est pas activée, le rendu est identique à l'historique.
    /// Thread UI pour la configurer, lue côté audio dans processBlock().
    MasterBus& masterBus() { return masterBus_; }
    const MasterBus& masterBus() const { return masterBus_; }

    /// D15.3 : la prise du bus final pour l'analyseur de spectre. Éteinte tant
    /// qu'aucune fenêtre d'analyse n'est ouverte.
    vsm::audio::dsp::SpectrumTap& spectrumTap() { return spectrumTap_; }
    const vsm::audio::dsp::SpectrumTap& spectrumTap() const { return spectrumTap_; }

    /// Piste de référence : l'enregistrement d'origine, pour l'écoute A/B.
    /// Configurée depuis le thread UI, lue dans processBlock().
    ///
    /// Elle est mélangée APRÈS le bus master, et le rendu hors ligne la coupe :
    /// voir `ReferenceTrack` pour le détail de ces deux règles.
    ReferenceTrack& referenceTrack() { return referenceTrack_; }
    const ReferenceTrack& referenceTrack() const { return referenceTrack_; }

private:
    /// Annoncée ici parce que `renderTrackVoice` et `VoiceBatch` en portent un
    /// pointeur ; définie plus bas, avec l'explication de ce qu'elle compense.
    struct Compensation;

    struct GraphSnapshot {
        vsm::sequencer::Project project;

        /// LE PLANNING, RANGÉ PAR PISTE (D8.4).
        ///
        /// Il était trié par TEMPS, et chaque piste le parcourait en entier
        /// pour n'en garder que ce qui la concernait. Le coût d'un bloc valait
        /// donc « nombre de pistes x nombre total d'événements » -- une
        /// quadratique, invisible sur les projets d'essai et écrasante sur un
        /// vrai : trente-deux pistes de quatre mille notes consommaient
        /// **99,5 % du budget d'un bloc**, dont l'essentiel pour écarter des
        /// notes qui ne sonnaient pas encore. Et le découpage en sous-segments
        /// d'automation multipliait le tout par huit.
        ///
        /// Le tri est désormais par (piste, temps) -- un tri STABLE sur la
        /// piste conserve l'ordre temporel que le planificateur a établi --, et
        /// chaque piste ne voit que sa tranche, dans laquelle elle entre par
        /// recherche dichotomique.
        std::vector<vsm::sequencer::ScheduledEvent> schedule;
        /// [début, fin) dans `schedule`, par piste. Vide pour une piste sans
        /// événement.
        std::array<std::pair<uint32_t, uint32_t>, kMaxTracks> trackRange{};
    };

    /// UNE VALEUR CHASSÉE (D16.2), en attente de livraison à sa piste.
    ///
    /// Elle est CALCULÉE SUR LE FIL DE L'INTERFACE, dans `seekSeconds`, et
    /// passe au fil audio par une file sans verrou -- exactement comme une
    /// note jouée au clavier. Le chemin audio ne parcourt donc jamais le
    /// planning à rebours pour retrouver « la valeur d'avant », ce qui serait
    /// un coût non borné à l'endroit où il n'y en a pas le droit.
    struct ChasedControlEvent {
        uint32_t trackIndex = 0;
        vsm::audio::plugin::MidiControlEvent event;
    };

    struct LiveNoteEvent {
        uint32_t trackIndex = 0;
        uint8_t note = 60;
        uint8_t velocity = 100;
        bool noteOn = true;
    };

    /// Vide la file des valeurs chassées dans drainedChase_ (début de bloc).
    void drainChasedControls();

    /// Vide les files de notes live dans drainedLive_ (début de bloc).
    void drainLiveNotes();

    /// Rend [sampleStart, sampleStart+count) en tenant compte de l'automation
    /// (découpage en sous-segments) -- extrait de processBlock pour que le
    /// rebouclage puisse redécouper le bloc sans dupliquer cette logique.
    void renderSpan(const GraphSnapshot& snapshot, bool anySolo, int sampleStart, int sampleCount,
                    double startSeconds, float* outputL, float* outputR,
                    const std::vector<AutomationLane>* lanes);

    /// LA MOITIÉ QUI NE PARTAGE RIEN : événements, instrument, matériau audio,
    /// inserts et compensation d'une SEULE piste, écrits dans destL/destR.
    /// Ne touche que des données propres à `trackIndex` -- c'est ce qui permet
    /// de l'exécuter sur un autre cœur. `events` est le tableau de travail du
    /// thread appelant, jamais un tableau partagé.
    /// Renvoie false quand la piste n'a RIEN à rendre (ni instrument, ni
    /// matériau) : les tampons sont alors laissés tels quels, et le mixage
    /// doit la sauter. Ce booléen n'est pas un raffinement -- c'est lui qui
    /// garantit que les deux moitiés prennent la MÊME décision, même si
    /// l'interface change l'instrument d'une piste entre les deux.
    bool renderTrackVoice(const GraphSnapshot& snapshot, size_t trackIndex, int sampleStart,
                          int sampleCount, double rangeStartSeconds, bool includeScheduledEvents,
                          Compensation* compensation, vsm::audio::plugin::MidiNoteEvent* events,
                          float* destL, float* destR);

    /// LA MOITIÉ QUI DOIT RESTER EN ORDRE : mixage vers le master ou un groupe,
    /// mesures, alimentation des départs. Appelée piste par piste, dans l'ordre
    /// de rendu, sur le seul thread audio.
    void mixTrackInto(const GraphSnapshot& snapshot, bool anySolo, size_t trackIndex,
                      int sampleStart, int sampleCount, const float* srcL, const float* srcR,
                      float* outputL, float* outputR);

    /// Ce que le banc de threads reçoit : tout ce dont `renderTrackVoice` a
    /// besoin, plus la liste des pistes à rendre. Vit sur la pile du thread
    /// audio pendant la ronde -- rien n'est alloué.
    struct VoiceBatch {
        ProcessGraph* self = nullptr;
        const GraphSnapshot* snapshot = nullptr;
        const std::vector<size_t>* order = nullptr;  ///< nullptr = ordre naturel
        /// Le plan de compensation, chargé UNE fois par le thread audio et
        /// prêté aux travailleurs. Le charger dans chaque tâche ferait se
        /// disputer tous les cœurs le même `atomic<shared_ptr>`.
        Compensation* compensation = nullptr;
        int sampleStart = 0;
        int sampleCount = 0;
        double rangeStartSeconds = 0.0;
        bool includeScheduledEvents = true;
    };
    static void renderVoiceJob(void* context, size_t index, size_t workerId);
    /// Alloue (thread UI) les tampons par piste et par thread dont le chemin
    /// parallèle a besoin. Le chemin mono-cœur n'en alloue aucun.
    void ensureParallelBuffers();

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
    /// Conclut les mesures accumulées sur le bloc et les publie.
    void publishMeasurement(size_t trackIndex);

    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 512;
    std::array<float, kMaxTracks> blockPeak_{}; // pic par piste, cumulé sur les sous-segments
    // RMS et corrélation de phase par piste (D4.7). Accumulés sur TOUT le bloc,
    // sous-segments d'automation compris : une mesure prise sur un
    // sous-segment de 64 échantillons sauterait à chaque changement de valeur
    // automatisée, et l'aiguille deviendrait illisible là où le son ne bouge
    // presque pas.
    std::array<double, kMaxTracks> blockSumL2_{}, blockSumR2_{}, blockSumLR_{};
    std::array<int, kMaxTracks> blockCount_{};

    std::atomic<std::shared_ptr<const GraphSnapshot>> snapshot_;
    std::array<std::atomic<std::shared_ptr<vsm::audio::plugin::ISynthPlugin>>, kMaxTracks> instruments_{};
    std::array<std::string, kMaxTracks> instrumentIds_; // bookkeeping thread UI uniquement

    // Lanes d'automation publiées via un snapshot atomique (comme snapshot_
    // et instruments_) : le thread audio ne fait que load() un shared_ptr vers
    // un vecteur IMMUABLE ; l'UI publie une nouvelle liste à chaque édition.
    // Plus de course de données possible entre édition et lecture.
    std::atomic<std::shared_ptr<const std::vector<AutomationLane>>> automationLanes_;

    // --- MIXAGE AUTOMATISÉ (D4.6) -----------------------------------------
    //
    // Le volume, le panoramique et les départs vivent dans le PROJET, que le
    // thread audio ne lit qu'en lecture seule (c'est tout l'intérêt du
    // snapshot). Une courbe qui les pilote ne peut donc pas les modifier là où
    // ils sont : elle écrit dans ces surcharges, que le mixage consulte à la
    // place quand la piste est automatisée.
    //
    // Écrites et lues par le SEUL thread audio, entre `applyAutomationAt` et le
    // rendu du sous-segment qui suit : pas d'atomique nécessaire, et surtout
    // pas de verrou.
    std::array<float, kMaxTracks> autoVolume_{};
    std::array<float, kMaxTracks> autoPan_{};
    std::array<std::array<float, kMaxSends>, kMaxTracks> autoSend_{};
    /// Quels réglages sont pilotés, par piste : bit 0 volume, bit 1
    /// panoramique, bits 2..9 les huit départs. Calculé quand les courbes sont
    /// publiées, pour que le mixage n'ait qu'un entier à consulter.
    std::array<uint16_t, kMaxTracks> autoMask_{};
    static constexpr uint16_t kAutoVolume = 1u << 0;
    static constexpr uint16_t kAutoPan = 1u << 1;
    static constexpr uint16_t kAutoSendFirst = 2;
    void refreshAutomationMask();
    MeterBank meters_;
    MasterBus masterBus_;
    vsm::audio::dsp::SpectrumTap spectrumTap_;
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

    /// L'ORDRE DANS LEQUEL LES PISTES SONT RENDUES (D4.4).
    ///
    /// Il vaut l'ordre naturel tant qu'aucune chaîne latérale n'existe -- et
    /// c'est important : réordonner les additions changerait le dernier bit du
    /// mixage sans raison. Dès qu'un effet ÉCOUTE un bus, les pistes qui
    /// ALIMENTENT ce bus passent devant : pour que la grosse caisse fasse
    /// plonger la basse dans le bloc courant, il faut qu'elle ait déjà été
    /// calculée. Un ordre publié depuis le thread UI, plutôt qu'un tri dans le
    /// rappel audio, qui n'alloue pas.
    std::atomic<std::shared_ptr<const std::vector<size_t>>> renderOrder_{nullptr};
    /// Recalcule et publie l'ordre. Thread UI ; appelée quand le projet ou une
    /// chaîne d'inserts change, c'est-à-dire aux deux seuls endroits d'où la
    /// réponse peut bouger.
    void refreshRenderOrder();

    // --- LA PUBLICATION DES SORTIES D'INSTRUMENT (D18.7b) ------------------
    //
    // LE PROBLÈME. Une boîte à rythmes rend ses six pièces MIXÉES sur deux
    // canaux. Une reconstruction qui a séparé la grosse caisse de la caisse
    // claire -- ce que la chaîne d'analyse fait, et c'est l'objectif de PARITÉ
    // des pistes -- les recolle donc au moment de les jouer, et l'on ne peut
    // ni compresser la caisse claire seule, ni la baisser de deux décibels.
    //
    // LA FORME RETENUE. Une piste dit « je porte la sortie n° k de la piste
    // j » (`Track::outputSourceTrack`). Le graphe rend alors la machine de j
    // par `processMultiOut` : la sortie 0 reste sur j -- donc une machine dont
    // personne ne réclame les sorties annexes emprunte le chemin d'avant, au
    // bit près -- et chaque autre atterrit dans un tampon ANNEXE que la piste
    // qui l'a réclamée vient lire.
    //
    // CE QUE ÇA COÛTE, ET QUI EST ASSUMÉ. Le calcul d'une piste dépend
    // désormais de celui d'une autre, ce qui est exactement ce qui interdisait
    // le parallélisme aux chaînes latérales. On ne l'interdit pas ici : on rend
    // en DEUX VAGUES -- d'abord les pistes qui ne lisent rien, ensuite celles
    // qui lisent -- chacune parallèle. Le MÉLANGE, lui, garde l'ordre qu'il a
    // toujours eu, parce que réordonner des additions flottantes changerait le
    // dernier bit sans raison.
    static constexpr int kMaxInstrumentOutputs = 16;
    /// Le nombre de sorties ANNEXES (index >= 1) que le graphe sait porter en
    /// tout, tous instruments confondus. Au-delà, la publication est REFUSÉE
    /// et comptée -- jamais silencieusement ignorée.
    static constexpr size_t kMaxExtraOutputs = 64;

    struct OutputRouting {
        /// Combien de sorties rendre pour la piste `t`. 0 ou 1 = le chemin
        /// d'avant, `process` appelé tel quel.
        std::array<int, kMaxTracks> outputsToRender{};
        /// Le tampon annexe où verser la sortie `k` de la piste `t`, ou -1
        /// quand personne ne la réclame.
        std::array<std::array<int16_t, kMaxInstrumentOutputs>, kMaxTracks> slotOf{};
        /// Le tampon annexe que la piste `t` doit LIRE, ou -1 si elle ne
        /// publie la sortie de personne.
        std::array<int16_t, kMaxTracks> readSlot{};
        /// Combien de tampons annexes sont réellement utilisés : c'est le
        /// nombre qu'il faut remettre à zéro au début de chaque segment.
        size_t usedSlots = 0;
        /// Les deux vagues de rendu (voir ci-dessus).
        std::vector<size_t> firstWave, secondWave;
    };
    /// Nul tant qu'aucune piste ne publie de sortie -- et dans ce cas le rendu
    /// emprunte exactement le chemin qu'il avait.
    std::atomic<std::shared_ptr<const OutputRouting>> outputRouting_{nullptr};
    /// Thread UI, appelée aux mêmes endroits que `refreshRenderOrder`.
    void refreshOutputRouting();
    /// Les tampons annexes, alloués par `prepare` et jamais dans le rappel
    /// audio.
    std::vector<std::vector<float>> extraOutL_, extraOutR_;
    /// Une piste a réclamé une sortie que sa machine n'a pas, ou le graphe a
    /// manqué de tampons. Compté, publié, jamais tu.
    std::atomic<uint64_t> droppedInstrumentOutputs_{0};

    // --- COMPENSATION DE LATENCE (PDC, D4.5) -------------------------------
    //
    // LE PROBLÈME. Un effet qui suréchantillonne filtre, et un filtre à phase
    // linéaire retarde : insérer une distorsion sur une piste la décalait de
    // seize échantillons. Le son restait juste, mais la piste n'était plus en
    // place -- et deux prises censées coïncider cessaient de coïncider selon
    // les effets qu'on leur avait mis. C'est le genre de défaut qu'on attribue
    // à tout sauf à sa cause.
    //
    // LA SOLUTION, qui est celle de tous les séquenceurs : on ne peut pas
    // AVANCER une piste, alors on RETARDE toutes les autres. Le graphe calcule
    // la latence de chaque chemin, prend le maximum, et donne à chaque piste la
    // différence.
    struct Compensation {   // (annoncée plus haut : `VoiceBatch` en porte un pointeur)
        int graphLatency = 0;                       ///< le maximum, en échantillons
        std::array<int, kMaxTracks> delay{};        ///< retard à appliquer, par piste
        /// Lignes à retard, une paire par piste retardée. Allouées sur le
        /// thread UI et publiées entières ; le thread audio n'y écrit que des
        /// échantillons, jamais leur taille.
        std::array<std::vector<float>, kMaxTracks> lineL, lineR;
        std::array<int, kMaxTracks> writePos{};
    };
    /// Le plan de compensation courant, ou nullptr quand rien n'a de latence --
    /// et dans ce cas le rendu emprunte exactement le chemin qu'il avait.
    std::atomic<std::shared_ptr<Compensation>> compensation_{nullptr};
    /// Recalcule et publie le plan. Thread UI, comme l'ordre de rendu.
    void refreshCompensation();
    /// Applique le retard d'une piste à son bloc, en place.
    void applyCompensation(Compensation& plan, size_t trackIndex, float* left, float* right,
                            int numSamples);

public:
    /// Latence totale du graphe, en échantillons : le chemin le plus long, que
    /// tous les autres rejoignent. Zéro tant qu'aucun effet n'en déclare.
    int graphLatencySamples() const {
        auto plan = compensation_.load(std::memory_order_acquire);
        return plan ? plan->graphLatency : 0;
    }

private:

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

    // --- Tampons du rendu multicœur (D8.1) --------------------------------
    //
    // UN TAMPON STÉRÉO PAR PISTE, parce que les pistes sont calculées en même
    // temps et mélangées après ; et UN TABLEAU D'ÉVÉNEMENTS PAR THREAD, parce
    // qu'un tableau d'événements ne survit pas à la piste qui le consomme.
    // Alloués seulement quand des threads existent : à zéro thread, le graphe
    // occupe exactement la mémoire qu'il occupait.
    RenderThreadPool renderPool_;
    std::vector<std::vector<float>> parallelL_, parallelR_;
    std::vector<std::vector<vsm::audio::plugin::MidiNoteEvent>> parallelEvents_;
    /// Quelles pistes la ronde a réellement rendues. Écrit par les
    /// travailleurs (une case chacun), lu par le mixage juste après.
    std::array<uint8_t, kMaxTracks> parallelActive_{};

    /// CHANGER LE NOMBRE DE THREADS PENDANT QUE LE SON TOURNE est une chose
    /// qu'un utilisateur fait, et détruire un thread qui rend un bloc en est
    /// une autre. Ces deux atomiques évitent la seconde sans verrou : l'UI
    /// interdit d'abord le chemin parallèle, puis attend que le bloc en cours
    /// en soit sorti. Les deux côtés sont en `seq_cst` -- c'est la seule
    /// cohérence qui garantit qu'au moins l'un des deux voit l'autre.
    std::atomic<bool> parallelAllowed_{false};
    std::atomic<int> renderBusy_{0};
    std::atomic<uint64_t> parallelSpans_{0};

    /// UNE CHAÎNE LATÉRALE INTERDIT LE PARALLÉLISME, et c'est le seul cas.
    /// Un effet qui écoute un départ lit ce que les pistes précédentes viennent
    /// d'y verser : le calcul d'une piste dépend alors du MÉLANGE d'une autre,
    /// et l'indépendance sur laquelle tout repose n'existe plus. Publié par
    /// `refreshRenderOrder`, qui fait déjà exactement cette recherche.
    std::atomic<bool> sidechainActive_{false};

    /// EN DESSOUS DE CE NOMBRE D'ÉCHANTILLONS, ON NE RÉVEILLE PERSONNE. Une
    /// ronde coûte quelques microsecondes de réveils ; les rendre sur un
    /// sous-segment d'automation de 64 échantillons coûterait plus que le
    /// calcul qu'on distribue.
    static constexpr int kMinParallelSamples = 128;
    /// Et en dessous de ce nombre de pistes non plus : à deux pistes, le second
    /// thread passe son temps à se réveiller pour une seule.
    static constexpr size_t kMinParallelTracks = 4;

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

    /// LA CHASSE AUX CONTRÔLEURS (D16.2). La file est large : un déplacement
    /// de tête peut rendre plusieurs valeurs par piste, sur des dizaines de
    /// pistes. Ce qui déborderait est COMPTÉ, jamais tu -- une pédale qui
    /// manque est exactement le genre de silence qu'on cherche des heures.
    static constexpr size_t kChaseQueueCapacity = 1024;
    static constexpr size_t kMaxChasedPerBlock = 512;
    vsm::audio::util::LockFreeRingBuffer<ChasedControlEvent, kChaseQueueCapacity> chaseQueue_;
    std::array<ChasedControlEvent, kMaxChasedPerBlock> drainedChase_{};
    int drainedChaseCount_ = 0;
    std::atomic<uint64_t> droppedChasedControls_{0};
    std::atomic<bool> metronomeCountInOnly_{false};
    std::atomic<bool> metronomeRecordOnly_{false};
    std::atomic<bool> recording_{false};
};

} // namespace vsm::audio::engine
