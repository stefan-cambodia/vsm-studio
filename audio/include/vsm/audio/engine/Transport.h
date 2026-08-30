#pragma once
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/sequencer/Project.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace vsm::audio::engine {

enum class TransportState { Stopped, Playing, Paused };

/// LE TRANSPORT, ET IL N'Y EN A PLUS QU'UN (D8.3).
///
/// **CE QUI EXISTAIT AVANT, ET POURQUOI C'ÉTAIT INTENABLE.** Deux notions de
/// position coexistaient. `RealtimeTransport` (Phase 1) tenait la sienne sur un
/// thread dédié, à l'horloge système ; `ProcessGraph` tenait la sienne en
/// comptant les échantillons réellement sortis de la carte. L'interface
/// pilotait la PREMIÈRE et recopiait son état dans la seconde une fois par
/// tour de minuterie, en comparant deux booléens. Trois défauts en
/// découlaient, dont aucun n'était une erreur d'écriture mais tous une
/// conséquence de la structure :
///
/// 1. **La position n'avançait qu'aux événements.** Le thread MIDI dormait
///    jusqu'à la note suivante ; entre deux notes espacées, le curseur ne
///    bougeait pas d'un pixel. Sur une nappe tenue, il restait figé.
/// 2. **Un projet uniquement AUDIO ne pouvait pas jouer du tout.** Sans note,
///    le planning était vide, la passe se terminait « naturellement » aussitôt
///    et le transport s'arrêtait avant d'avoir commencé -- alors que le son,
///    lui, était là. Le DAW savait charger une prise de neuf minutes et
///    refusait de la lire.
/// 3. **Démarrer la lecture repositionnait le moteur audio sur l'horloge du
///    thread MIDI**, c'est-à-dire sur la moins exacte des deux.
///
/// **CE QU'IL FAIT MAINTENANT** : il ne tient AUCUNE position. Il en lit une,
/// celle du graphe, et n'ajoute que ce que le graphe n'a pas à connaître --
/// l'état (arrêté, en lecture, en pause), la conversion en ticks, et l'arrêt en
/// fin de morceau. « Arrêté » et « en pause » ne diffèrent d'ailleurs que par
/// une chose : l'arrêt revient à zéro.
///
/// **L'HORLOGE DE SECOURS.** Sans carte son, personne n'appelle `processBlock`,
/// donc rien n'avance -- et l'application doit rester utilisable pour éditer,
/// faire défiler et exporter sur une machine sans audio. Un thread appelle
/// alors `processBlock` dans un tampon qu'on jette, au rythme du temps réel.
/// C'est volontairement le MÊME chemin de calcul : une seconde façon de faire
/// avancer le temps serait une seconde façon de se tromper, et c'est
/// exactement ce dont cette phase se débarrasse.
class Transport {
public:
    explicit Transport(ProcessGraph& graph);
    ~Transport();
    Transport(const Transport&) = delete;
    Transport& operator=(const Transport&) = delete;

    /// La carte de tempo et la fin du morceau. Ne déplace pas la tête de
    /// lecture : republier un projet pendant qu'on écoute ne doit pas faire
    /// sauter le son.
    void setProject(const vsm::sequencer::Project& project);

    void play();
    void pause();
    void stop();

    void seekToTick(vsm::midi::Tick tick);
    void seekSeconds(double seconds);
    void setLoopRegion(vsm::midi::Tick startTick, vsm::midi::Tick endTick, bool enabled);

    TransportState state() const { return state_.load(std::memory_order_acquire); }
    double currentSeconds() const { return graph_.currentSeconds(); }
    vsm::midi::Tick currentTick() const;

    /// Thread UI, à chaque tour : arrête le transport quand le morceau est
    /// fini. C'est la SEULE chose que le graphe ne peut pas décider seul --
    /// il ne sait pas ce qu'est « la fin », il ne sait que rendre.
    void poll();

    /// La fin du morceau, en secondes : la dernière chose qui sonne, plus une
    /// noire de marge pour ne pas couper une résonance.
    double endOfSongSeconds() const;

    /// L'HORLOGE DE SECOURS S'ALLUME QUAND LA CARTE SON S'ÉTEINT, et pas
    /// avant : deux moteurs qui avanceraient le même graphe le feraient avancer
    /// deux fois plus vite. Appelée par `AudioEngine` quand le périphérique
    /// s'ouvre ou se ferme.
    void setAudioDeviceOpen(bool open, double sampleRate = 48000.0, int blockSize = 512);
    bool fallbackClockRunning() const { return fallbackRunning_.load(std::memory_order_acquire); }

private:
    void startFallbackClock(double sampleRate, int blockSize);
    void stopFallbackClock();
    void fallbackLoop(double sampleRate, int blockSize);

    ProcessGraph& graph_;
    mutable std::mutex mutex_;          ///< protège la copie du projet (thread UI seulement)
    vsm::sequencer::Project project_;
    std::atomic<TransportState> state_{TransportState::Stopped};
    std::atomic<double> endSeconds_{0.0};

    std::thread fallbackThread_;
    std::atomic<bool> fallbackRunning_{false};
    std::atomic<bool> fallbackShouldStop_{false};
};

} // namespace vsm::audio::engine
