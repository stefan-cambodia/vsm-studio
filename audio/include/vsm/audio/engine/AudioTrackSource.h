#pragma once
#include "vsm/sequencer/Track.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace vsm::audio::engine {

/// UN CLIP AUDIO, TRADUIT EN ÉCHANTILLONS.
///
/// Le modèle (`vsm::sequencer::Clip`) parle en ticks pour la position et en
/// secondes pour la fenêtre dans le fichier ; le chemin temps réel, lui, ne
/// doit connaître que des indices d'échantillons. La conversion se fait UNE
/// FOIS, sur le thread de l'interface, au moment où la piste est publiée --
/// jamais dans `process()`, qui n'a pas à connaître la carte de tempo.
struct AudioClipSpan {
    int64_t startFrame = 0;        ///< où le clip commence, sur la ligne de temps
    int64_t lengthFrames = 0;      ///< combien de temps il dure
    int64_t sourceStartFrame = 0;  ///< où il commence dans le fichier
    int64_t fadeInFrames = 0;
    int64_t fadeOutFrames = 0;
    float gain = 1.0f;
    bool invertPhase = false;
};

/// LE MATÉRIAU AUDIO D'UNE PISTE, prêt à jouer.
///
/// PRÉCHARGÉ EN MÉMOIRE, ET C'EST UN CHOIX ÉCRIT. La voix reconstruite de
/// *Sky and Sand* pèse 47 Mo sur le disque, soit 190 Mo une fois décodée en
/// flottants stéréo : c'est tenable pour une poignée de pistes, et c'est
/// exactement le cas d'usage que la phase D2 doit débloquer. La diffusion
/// depuis le disque -- qui seule permet vingt pistes de neuf minutes -- est la
/// phase D8.2, et elle changera CETTE classe sans toucher au reste : le graphe
/// ne connaît que `frameAt()`.
///
/// Le préchargement, le décodage et le rééchantillonnage se font sur le thread
/// de l'interface. `process()` ne fait que lire ce tableau : aucune allocation,
/// aucune I/O, aucun verrou.
struct AudioTrackSource {
    /// Échantillons décodés À LA FRÉQUENCE DE LA SESSION. Le rééchantillonnage
    /// est fait au chargement : le laisser au chemin temps réel obligerait à y
    /// interpoler à chaque bloc, pour un fichier qui ne change jamais.
    std::vector<float> left;
    std::vector<float> right;
    std::vector<AudioClipSpan> clips;

    bool empty() const { return left.empty() || clips.empty(); }
    int64_t frames() const { return static_cast<int64_t>(left.size()); }

    /// Mélange la portion demandée de la ligne de temps dans les tampons de
    /// sortie. `timelineStart` est l'indice du premier échantillon du bloc sur
    /// la LIGNE DE TEMPS, pas dans le fichier.
    ///
    /// Rend le nombre d'échantillons réellement écrits, pour que l'appelant
    /// puisse dire qu'une piste n'a rien joué plutôt que de le supposer.
    int mixInto(float* outLeft, float* outRight, int64_t timelineStart, int numSamples) const;
};

/// Traduit les clips du modèle en portées d'échantillons.
///
/// `ticksToSeconds` est passée par l'appelant : `audio/` ne connaît pas la
/// carte de tempo, qui vit dans `core/`, et n'a pas à la connaître.
std::vector<AudioClipSpan> spansFromTrack(const vsm::sequencer::Track& track,
                                           double sampleRate,
                                           const std::function<double(int64_t)>& ticksToSeconds);

} // namespace vsm::audio::engine
