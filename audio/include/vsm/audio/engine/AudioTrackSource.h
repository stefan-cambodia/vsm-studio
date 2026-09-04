#pragma once
#include "vsm/audio/dsp/PhaseVocoder.h"
#include "vsm/audio/dsp/SincResampler.h"
#include "vsm/audio/dsp/TimeStretch.h"
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
/// LE SUIVI DE TEMPO D'UNE PORTÉE (D12.5, `docs/CDC-etirement-temporel.md`).
///
/// Ce que le modèle exprime en ticks et en secondes est traduit ICI, une fois,
/// en trames -- comme le reste de `AudioClipSpan`, et pour la même raison : le
/// chemin temps réel ne doit connaître que des indices d'échantillons.
///
/// L'ÉTAT DE RENDU EST DANS LA PORTÉE, et il est `mutable` : `mixInto()` est
/// const parce qu'elle ne change pas ce que la piste JOUE, mais un étireur a
/// une mémoire (le grain précédent, le tampon de recouvrement) et il faut bien
/// qu'elle vive quelque part. Elle est allouée à la publication, jamais dans
/// `process()`.
struct ClipWarp {
    /// Un bloc plus long que celui-ci est rendu en plusieurs passes. L'étireur
    /// est indépendant de la taille des blocs (testé au bit près), donc
    /// découper ne change pas une valeur -- ce qui évite de faire descendre la
    /// taille maximale de bloc jusqu'ici.
    static constexpr int kMaxBlock = 8192;

    /// `false` : hauteur conservée. `true` : rééchantillonné, la hauteur
    /// suit la durée comme un vinyle qu'on ralentit.
    bool repitch = false;
    /// Hauteur conservée PAR LE VOCODEUR DE PHASE (D12.8) -- le défaut de
    /// `WarpMode::KeepPitch` depuis que le banc 8 l'a tranché -- ou par le
    /// WSOLA (`WarpMode::KeepPitchWsola`, le témoin). Décidé par le clip, à la
    /// publication, jamais dans `process()`.
    bool vocoder = false;
    /// La carte, sur la ligne de temps ABSOLUE (trames), vers le fichier.
    std::vector<vsm::audio::dsp::TimeStretch<SampleStore>::MapPoint> map;
    /// Les attaques du matériau, partagées par toutes les portées d'une piste :
    /// elles sont une propriété du FICHIER, pas du clip.
    std::shared_ptr<const std::vector<int64_t>> transients;

    mutable vsm::audio::dsp::TimeStretch<SampleStore> stretch;
    mutable vsm::audio::dsp::PhaseVocoder<SampleStore> phaseVocoder;
    mutable std::vector<float> scratchL, scratchR;
    vsm::audio::dsp::SincResampler kernel;

    /// Arme l'étireur et les tampons. Hors thread audio (alloue).
    void prepare();
    /// La position dans le fichier pour une trame de la ligne de temps.
    double sourceFor(int64_t timelineFrame) const;
};

struct AudioClipSpan {
    int64_t startFrame = 0;        ///< où le clip commence, sur la ligne de temps
    int64_t lengthFrames = 0;      ///< combien de temps il dure
    int64_t sourceStartFrame = 0;  ///< où il commence dans le fichier
    int64_t fadeInFrames = 0;
    int64_t fadeOutFrames = 0;
    /// D17.1 : la forme des deux fondus. `Linear` par défaut, et le chemin de
    /// lecture est alors exactement celui d'avant.
    vsm::sequencer::FadeShape fadeShape = vsm::sequencer::FadeShape::Linear;
    float gain = 1.0f;
    bool invertPhase = false;
    /// À l'envers (D13.4). Traduit à la publication par `prepareWarpedSpans`
    /// en un miroir du matériau et une fenêtre convertie.
    bool reversed = false;
    /// Nul quand le clip ne suit pas le tempo -- c'est-à-dire presque toujours,
    /// et le chemin de lecture est alors exactement celui d'avant D12.
    std::shared_ptr<ClipWarp> warp;
    /// LE MATÉRIAU DE CETTE PORTÉE, s'il n'est pas celui de la piste (D13.4 :
    /// un clip à l'envers lit un MIROIR du magasin de la piste). Nul sinon.
    std::shared_ptr<const SampleStore> source;
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

/// ARME LES PORTÉES ÉTIRÉES ET À L'ENVERS d'une piste, une fois que son
/// matériau est là (D12.5, D13.4) : détecte les attaques du fichier -- UNE fois, partagées par toutes
/// les portées -- et prépare les étireurs. Sans matériau ou sans portée
/// étirée, elle ne fait rien. Hors thread audio : elle lit tout le matériau et
/// elle alloue.
void prepareWarpedSpans(AudioTrackSource& source);

} // namespace vsm::audio::engine
