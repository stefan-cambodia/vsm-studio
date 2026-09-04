#include "vsm/audio/engine/AudioTrackSource.h"
#include "vsm/audio/dsp/TransientDetector.h"
#include "vsm/sequencer/ClipEdit.h"
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

// ---------------------------------------------------------------------------
// Le suivi de tempo (D12.5)
// ---------------------------------------------------------------------------

void ClipWarp::prepare() {
    // Le WSOLA est toujours armé : sa carte sert aussi à `sourceFor`, dont le
    // mode rééchantillonné a besoin. Le vocodeur ne l'est que s'il joue.
    stretch.prepare(kMaxBlock);
    if (map.size() >= 2) stretch.setMap(map.data(), static_cast<int>(map.size()));
    if (transients && !transients->empty())
        stretch.setTransients(transients->data(), static_cast<int>(transients->size()));
    if (vocoder && !repitch) {
        phaseVocoder.prepare(kMaxBlock);
        std::vector<vsm::audio::dsp::PhaseVocoder<SampleStore>::MapPoint> points;
        points.reserve(map.size());
        for (const auto& p : map) points.push_back({p.outputFrame, p.sourceFrame});
        if (points.size() >= 2) phaseVocoder.setMap(points.data(), static_cast<int>(points.size()));
        if (transients && !transients->empty())
            phaseVocoder.setTransients(transients->data(), static_cast<int>(transients->size()));
    }
    scratchL.assign(static_cast<size_t>(kMaxBlock), 0.0f);
    scratchR.assign(static_cast<size_t>(kMaxBlock), 0.0f);
    // LE NOYAU DU MODE RÉÉCHANTILLONNÉ est réglé sur le rapport le PLUS RAPIDE
    // de la carte : c'est lui qui décide de la coupure anti-repliement, et
    // sous-estimer la vitesse laisserait replier le passage le plus rapide.
    double plusRapide = 1.0;
    for (size_t i = 1; i < map.size(); ++i) {
        const auto dOut = static_cast<double>(map[i].outputFrame - map[i - 1].outputFrame);
        if (dOut > 0.0) plusRapide = std::max(plusRapide, (map[i].sourceFrame - map[i - 1].sourceFrame) / dOut);
    }
    kernel.prepare(plusRapide);
}

double ClipWarp::sourceFor(int64_t timelineFrame) const {
    return stretch.sourceFor(static_cast<double>(timelineFrame));
}

int AudioTrackSource::mixInto(float* outLeft, float* outRight,
                               int64_t timelineStart, int numSamples) const {
    if (!samples || samples->frames() == 0 || numSamples <= 0) return 0;
    int ecrits = 0;

    // LA GARDE COUVRE TOUT LE BLOC, et non chaque échantillon : elle dit au
    // thread de diffusion « je suis en train de lire », et il doit l'entendre
    // avant la première lecture, pas entre deux (voir `StreamedSampleStore`).
    const SampleStore::ReadGuard garde(samples.get());

    for (const auto& clip : clips) {
        if (clip.lengthFrames <= 0) continue;
        // Intersection du bloc demandé et de l'étendue du clip.
        const int64_t debut = std::max(timelineStart, clip.startFrame);
        const int64_t fin = std::min(timelineStart + numSamples,
                                      clip.startFrame + clip.lengthFrames);
        if (fin <= debut) continue;

        // LE CLIP QUI SUIT LE TEMPO (D12.5). Le chemin est SÉPARÉ, et c'est
        // volontaire : un clip qui ne suit pas le tempo -- c'est-à-dire
        // presque tous -- passe exactement par le code d'avant D12, sans un
        // test de plus par échantillon.
        if (clip.warp && clip.warp->stretch.isPrepared()) {
            const auto& w = *clip.warp;
            const float signeW = clip.invertPhase ? -clip.gain : clip.gain;
            int64_t position = debut;
            while (position < fin) {
                const int n = static_cast<int>(std::min<int64_t>(ClipWarp::kMaxBlock, fin - position));
                std::fill(w.scratchL.begin(), w.scratchL.begin() + n, 0.0f);
                std::fill(w.scratchR.begin(), w.scratchR.begin() + n, 0.0f);
                if (w.repitch) {
                    // LE VINYLE QU'ON RALENTIT : on lit la source à une
                    // position fractionnaire, par le noyau fenêtré de D12.1,
                    // à travers le magasin (résident ou diffusé).
                    const auto* magasin = samples.get();
                    const auto lire = [magasin](int64_t i, float& g, float& d) {
                        return magasin->frameAt(i, g, d);
                    };
                    const double sDebut = w.sourceFor(position);
                    const double sFin = w.sourceFor(position + n);
                    samples->requestRange(static_cast<int64_t>(std::floor(std::min(sDebut, sFin))) - 64,
                                          static_cast<int64_t>(std::abs(sFin - sDebut)) + 128);
                    for (int i = 0; i < n; ++i)
                        w.kernel.stereoAt(lire, w.sourceFor(position + i),
                                           w.scratchL[static_cast<size_t>(i)],
                                           w.scratchR[static_cast<size_t>(i)]);
                } else if (w.vocoder) {
                    w.phaseVocoder.render(*samples, position, n, w.scratchL.data(), w.scratchR.data(), 1.0f);
                } else {
                    w.stretch.render(*samples, position, n, w.scratchL.data(), w.scratchR.data(), 1.0f);
                }
                for (int i = 0; i < n; ++i) {
                    const int64_t dansLeClip = position + i - clip.startFrame;
                    const float gain = signeW * fadeGain(dansLeClip, clip.lengthFrames,
                                                          clip.fadeInFrames, clip.fadeOutFrames);
                    const auto j = static_cast<size_t>(position + i - timelineStart);
                    outLeft[j] += w.scratchL[static_cast<size_t>(i)] * gain;
                    outRight[j] += w.scratchR[static_cast<size_t>(i)] * gain;
                }
                position += n;
                ecrits += n;
            }
            continue;
        }

        // ON DIT OÙ ON VA AVANT D'Y ALLER. Le matériau résident n'en fait
        // rien ; le matériau diffusé en fait tout, puisque c'est sa seule
        // façon de savoir qu'un saut de tête de lecture vient de l'envoyer
        // trois minutes plus loin.
        samples->requestRange(clip.sourceStartFrame + (debut - clip.startFrame), fin - debut);

        const float signe = clip.invertPhase ? -clip.gain : clip.gain;
        for (int64_t position = debut; position < fin; ++position) {
            const int64_t dansLeClip = position - clip.startFrame;
            const int64_t dansLeFichier = clip.sourceStartFrame + dansLeClip;
            // AU-DELÀ DE LA FIN DU FICHIER, ON SE TAIT. Un clip peut être plus
            // long que ce qui reste de matériau -- après un allongement à la
            // souris, par exemple -- et lire au-delà donnerait du bruit ou un
            // débordement. Le silence est la seule réponse honnête. Un
            // matériau diffusé répond faux pour la même raison quand le disque
            // n'a pas encore livré : le trou est alors COMPTÉ (`cacheMisses`).
            float g = 0.0f, d = 0.0f;
            if (!samples->frameAt(dansLeFichier, g, d)) continue;
            const float gain = signe * fadeGain(dansLeClip, clip.lengthFrames,
                                                 clip.fadeInFrames, clip.fadeOutFrames);
            const auto j = static_cast<size_t>(position - timelineStart);
            outLeft[j] += g * gain;
            outRight[j] += d * gain;
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

        // UN CLIP QUI SUIT LE TEMPO DONNE UNE SEULE PORTÉE, et sa carte dit ce
        // qu'il joue (D12.5). Il ne boucle pas : répéter une fenêtre ET suivre
        // une carte sont deux réponses à la même question, et c'est la carte
        // qui a été posée à la main. Dit dans le CDC (§ 8, ce qui n'est pas au
        // programme).
        if (vsm::sequencer::clipIsWarped(clip)) {
            // LA LONGUEUR JOUÉE D'UN CLIP ÉTIRÉ se lit dans le clip, et à
            // défaut dans son DERNIER MARQUEUR : c'est la carte qui dit
            // jusqu'où le matériau va, et elle est en ticks -- pas besoin de
            // deviner la fin du matériau en secondes.
            const auto longueur = static_cast<int64_t>(
                clip.length > 0 ? clip.length
                : clip.sourceLength > 0 ? clip.sourceLength
                                        : clip.warpMarkers.back().tick);
            const double finSecondesW = ticksToSeconds(static_cast<int64_t>(clip.startTick) + longueur);
            const int64_t depart = static_cast<int64_t>(std::llround(debutSecondes * sampleRate));
            const int64_t jouee = static_cast<int64_t>(std::llround((finSecondesW - debutSecondes) * sampleRate));
            if (jouee <= 0) continue;
            AudioClipSpan span;
            span.startFrame = depart;
            span.lengthFrames = jouee;
            span.sourceStartFrame = static_cast<int64_t>(std::llround(clip.sourceStartSeconds * sampleRate));
            span.fadeInFrames = static_cast<int64_t>(std::llround(clip.fadeInSeconds * sampleRate));
            span.fadeOutFrames = static_cast<int64_t>(std::llround(clip.fadeOutSeconds * sampleRate));
            span.gain = clip.gain;
            span.invertPhase = clip.invertPhase;
            auto warp = std::make_shared<ClipWarp>();
            warp->repitch = clip.warpMode == vsm::sequencer::WarpMode::Repitch;
            // LE VOCODEUR EST LE DÉFAUT DE « HAUTEUR CONSERVÉE » (D12.8, banc
            // 8 tenu) ; le WSOLA reste le témoin, choisi par le clip.
            warp->vocoder = clip.warpMode == vsm::sequencer::WarpMode::KeepPitch;
            for (const auto& m : clip.warpMarkers) {
                const double sortieSecondes = ticksToSeconds(static_cast<int64_t>(clip.startTick) + m.tick);
                warp->map.push_back({static_cast<int64_t>(std::llround(sortieSecondes * sampleRate)),
                                      m.sourceSeconds * sampleRate});
            }
            span.warp = std::move(warp);
            spans.push_back(std::move(span));
            continue;
        }
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

void prepareWarpedSpans(AudioTrackSource& source) {
    if (!source.samples) return;
    bool besoin = false;
    for (const auto& span : source.clips) if (span.warp) { besoin = true; break; }
    if (!besoin) return;

    // LES ATTAQUES SONT UNE PROPRIÉTÉ DU FICHIER, pas du clip : on les cherche
    // UNE fois, et toutes les portées de la piste partagent la liste. Un clip
    // rééchantillonné n'en a pas besoin (il ne recolle rien), mais la détection
    // est faite quand au moins une portée conserve la hauteur.
    std::shared_ptr<const std::vector<int64_t>> attaques;
    for (const auto& span : source.clips) {
        if (span.warp && !span.warp->repitch) {
            const SampleStore::ReadGuard garde(source.samples.get());
            attaques = std::make_shared<const std::vector<int64_t>>(
                vsm::audio::dsp::TransientDetector::detect(*source.samples, 0, source.samples->frames()));
            break;
        }
    }
    for (auto& span : source.clips) {
        if (!span.warp) continue;
        span.warp->transients = attaques;
        span.warp->prepare();
    }
}

} // namespace vsm::audio::engine
