#pragma once
#include "vsm/audio/engine/SampleStore.h"
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
/// D'OÙ VIENNENT LES ÉCHANTILLONS N'EST PLUS SON AFFAIRE (D8.2). Ils sont
/// résidents pour ce qui est court, diffusés depuis le disque pour ce qui est
/// long, et cette classe ne fait la différence nulle part : elle demande le
/// n-ième échantillon à un `SampleStore` et le place sur la ligne de temps.
/// C'est ce que la version précédente de ce commentaire annonçait -- « elle
/// changera CETTE classe sans toucher au reste » -- et c'est ce qui s'est
/// passé : `ProcessGraph` n'a pas bougé d'une ligne.
///
/// La découpe en clips, elle, se fait toujours sur le thread de l'interface :
/// `mixInto()` ne fait que lire, sans allocation, sans entrée-sortie et sans
/// verrou.
struct AudioTrackSource {
    /// Le matériau. `nullptr` = piste sans son (et non piste silencieuse : la
    /// distinction compte, voir `AudioTrackLoader`).
    std::shared_ptr<const SampleStore> samples;
    std::vector<AudioClipSpan> clips;

    /// Raccourci pour les cas où le matériau tient en mémoire -- les tests, et
    /// tout ce qui est fabriqué plutôt que lu.
    void setMemorySamples(std::vector<float> left, std::vector<float> right) {
        samples = std::make_shared<MemorySampleStore>(std::move(left), std::move(right));
    }

    bool empty() const { return !samples || samples->frames() == 0 || clips.empty(); }
    int64_t frames() const { return samples ? samples->frames() : 0; }
    /// Ce que cette piste occupe en mémoire vive. Diffusée, le chiffre ne
    /// dépend pas de la durée du fichier -- c'est tout l'objet de D8.2.
    size_t residentBytes() const { return samples ? samples->residentBytes() : 0; }
    /// Échantillons que le cache n'a pas su livrer à temps. Zéro en lecture
    /// normale ; toute autre valeur est un trou dans le son.
    uint64_t cacheMisses() const { return samples ? samples->cacheMisses() : 0; }

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
