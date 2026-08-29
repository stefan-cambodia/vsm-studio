#include "vsm/audio/engine/AudioTrackSource.h"
#include <algorithm>
#include <cmath>

namespace vsm::audio::engine {

namespace {

/// Le gain du fondu à la position `position` dans un clip de `length`
/// échantillons. Linéaire : sur des fondus courts -- ceux qui servent à ne pas
/// entendre le raccord -- une courbe plus savante ne s'entend pas, et une
/// courbe est une chose de plus à régler.
inline float fadeGain(int64_t position, int64_t length,
                       int64_t fadeIn, int64_t fadeOut) {
    float gain = 1.0f;
    if (fadeIn > 0 && position < fadeIn)
        gain *= static_cast<float>(position) / static_cast<float>(fadeIn);
    const int64_t restant = length - position;
    if (fadeOut > 0 && restant < fadeOut)
        gain *= static_cast<float>(std::max<int64_t>(0, restant)) / static_cast<float>(fadeOut);
    return gain;
}

} // namespace

int AudioTrackSource::mixInto(float* outLeft, float* outRight,
                               int64_t timelineStart, int numSamples) const {
    if (left.empty() || numSamples <= 0) return 0;
    const int64_t total = frames();
    const bool stereo = right.size() == left.size();
    int ecrits = 0;

    for (const auto& clip : clips) {
        if (clip.lengthFrames <= 0) continue;
        // Intersection du bloc demandé et de l'étendue du clip.
        const int64_t debut = std::max(timelineStart, clip.startFrame);
        const int64_t fin = std::min(timelineStart + numSamples,
                                      clip.startFrame + clip.lengthFrames);
        if (fin <= debut) continue;

        const float signe = clip.invertPhase ? -clip.gain : clip.gain;
        for (int64_t position = debut; position < fin; ++position) {
            const int64_t dansLeClip = position - clip.startFrame;
            const int64_t dansLeFichier = clip.sourceStartFrame + dansLeClip;
            // AU-DELÀ DE LA FIN DU FICHIER, ON SE TAIT. Un clip peut être plus
            // long que ce qui reste de matériau -- après un allongement à la
            // souris, par exemple -- et lire au-delà donnerait du bruit ou un
            // débordement. Le silence est la seule réponse honnête.
            if (dansLeFichier < 0 || dansLeFichier >= total) continue;
            const float gain = signe * fadeGain(dansLeClip, clip.lengthFrames,
                                                 clip.fadeInFrames, clip.fadeOutFrames);
            const auto i = static_cast<size_t>(dansLeFichier);
            const auto j = static_cast<size_t>(position - timelineStart);
            outLeft[j] += left[i] * gain;
            outRight[j] += (stereo ? right[i] : left[i]) * gain;
            ++ecrits;
        }
    }
    return ecrits;
}

std::vector<AudioClipSpan> spansFromTrack(const vsm::sequencer::Track& track,
                                           double sampleRate,
                                           const std::function<double(int64_t)>& ticksToSeconds) {
    std::vector<AudioClipSpan> spans;
    if (track.kind != vsm::sequencer::Track::Kind::Audio || track.audio.empty())
        return spans;

    for (const auto& clip : track.clips) {
        if (clip.muted) continue;
        const double debutSecondes = ticksToSeconds(static_cast<int64_t>(clip.startTick));
        // UNE LONGUEUR NULLE VEUT DIRE « JUSQU'AU BOUT DU FICHIER », comme pour
        // un clip MIDI dont la fenêtre vide veut dire « tout le matériau ».
        const double finSecondes = clip.length > 0
            ? ticksToSeconds(static_cast<int64_t>(clip.startTick + clip.length))
            : debutSecondes + track.audio.durationSeconds() - clip.sourceStartSeconds;

        const int64_t depart = static_cast<int64_t>(std::llround(debutSecondes * sampleRate));
        const int64_t jouee = static_cast<int64_t>(std::llround((finSecondes - debutSecondes) * sampleRate));
        if (jouee <= 0) continue;

        // LA FENÊTRE, en trames. Elle vaut la durée jouée quand le clip ne
        // déclare pas de fenêtre : c'est le cas d'un clip qu'on n'a pas
        // étiré, et il ne se répète alors pas -- une seule portée.
        const double fenetreSecondes = clip.sourceLength > 0
            ? ticksToSeconds(static_cast<int64_t>(clip.startTick + clip.sourceLength)) - debutSecondes
            : 0.0;
        int64_t fenetre = static_cast<int64_t>(std::llround(fenetreSecondes * sampleRate));
        if (fenetre <= 0) fenetre = jouee;

        // LA BOUCLE DE CLIP (D5.2), et elle est ici pour que le geste veuille
        // dire la MÊME chose sur une piste audio et sur une piste MIDI. Le
        // planning MIDI répétait déjà sa fenêtre quand la durée jouée la
        // dépasse (voir `passagesOf`) ; l'audio, lui, lisait tout droit et
        // continuait dans le fichier. Étirer un clip aurait donc bouclé une
        // batterie MIDI et révélé la suite d'une prise audio -- deux réponses
        // pour un seul geste.
        for (int64_t offset = 0; offset < jouee; offset += fenetre) {
            AudioClipSpan span;
            span.startFrame = depart + offset;
            span.lengthFrames = std::min(fenetre, jouee - offset);
            span.sourceStartFrame = static_cast<int64_t>(std::llround(clip.sourceStartSeconds * sampleRate));
            // LES FONDUS APPARTIENNENT AU CLIP, PAS À CHAQUE RÉPÉTITION : celui
            // d'entrée est sur la première, celui de sortie sur la dernière.
            // Les répéter ferait un trou à chaque tour de boucle.
            span.fadeInFrames = offset == 0
                ? static_cast<int64_t>(std::llround(clip.fadeInSeconds * sampleRate)) : 0;
            span.fadeOutFrames = (offset + fenetre >= jouee)
                ? static_cast<int64_t>(std::llround(clip.fadeOutSeconds * sampleRate)) : 0;
            span.gain = clip.gain;
            span.invertPhase = clip.invertPhase;
            if (span.lengthFrames > 0) spans.push_back(span);
        }
    }

    // UNE PISTE AUDIO SANS CLIP JOUE TOUT SON FICHIER, à sa place — la même
    // règle que pour une piste MIDI sans clip, et pour la même raison : « pas
    // de clip » veut dire « pas de découpe », pas « rien ».
    if (spans.empty() && track.clips.empty()) {
        AudioClipSpan span;
        span.lengthFrames = track.audio.frames > 0
            ? static_cast<int64_t>(std::llround(track.audio.durationSeconds() * sampleRate))
            : 0;
        if (span.lengthFrames > 0) spans.push_back(span);
    }
    return spans;
}

} // namespace vsm::audio::engine
