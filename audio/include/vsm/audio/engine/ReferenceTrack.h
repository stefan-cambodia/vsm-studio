#pragma once
#include "vsm/audio/io/WavFileReader.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace vsm::audio::engine {

/// Piste de RÉFÉRENCE : l'enregistrement d'origine, joué en regard de la
/// reconstruction (étape 11.2 de la feuille de route globale).
///
/// À QUOI ELLE SERT. Une distance publiée dit de combien on s'écarte ; elle ne
/// dit pas en quoi. Pour corriger une reconstruction, il faut ENTENDRE les deux
/// versions au même endroit du morceau, et pouvoir passer de l'une à l'autre
/// sans quitter le transport. C'est le seul moyen de repérer qu'une attaque est
/// trop molle ou qu'une note manque.
///
/// TROIS RÈGLES, et chacune répond à un piège :
///
///  1. ELLE NE PART JAMAIS DANS L'EXPORT. Le rendu hors ligne partage le même
///     `processBlock` que la lecture temps réel -- c'est voulu, il ne doit
///     exister qu'un seul chemin de calcul. Mais exporter la reconstruction
///     avec l'original mélangé dedans produirait un fichier qui n'est ni l'un
///     ni l'autre. L'export coupe donc explicitement la référence, et un test
///     le vérifie.
///  2. ELLE PASSE APRÈS LE BUS MASTER. La tranche master appartient à la
///     reconstruction ; la faire agir sur l'original le colorerait, et on
///     comparerait alors deux sons également traités au lieu de comparer une
///     copie à son modèle.
///  3. LA LECTURE DU FICHIER A LIEU AILLEURS. Le tampon est publié par échange
///     atomique, comme les échantillons du sampler : le thread audio ne fait
///     que lire un pointeur déjà valide.
class ReferenceTrack {
public:
    /// Ce qu'on entend.
    enum class Mode {
        Off,   ///< reconstruction seule -- l'état normal
        Mix,   ///< les deux ensemble : on entend ce qui diverge
        Solo,  ///< l'original seul : le point de comparaison
    };

    void prepare(double sampleRate) { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }

    /// Publie l'enregistrement. Thread UI ou thread de chargement, jamais le
    /// thread audio. Un pointeur nul retire la référence.
    void setAudio(vsm::audio::io::SampleBufferPtr audio) {
        audio_.store(std::move(audio), std::memory_order_release);
    }
    vsm::audio::io::SampleBufferPtr audio() const { return audio_.load(std::memory_order_acquire); }
    bool hasAudio() const { return audio_.load(std::memory_order_acquire) != nullptr; }

    void setMode(Mode mode) { mode_.store(mode, std::memory_order_release); }
    Mode mode() const { return mode_.load(std::memory_order_acquire); }

    void setGain(float gain) { gain_.store(std::clamp(gain, 0.0f, 2.0f), std::memory_order_release); }
    float gain() const { return gain_.load(std::memory_order_acquire); }

    /// Décalage en secondes appliqué à la lecture. La reconstruction commence
    /// à zéro ; l'enregistrement, lui, peut avoir du silence en tête ou avoir
    /// été découpé. Sans ce réglage, on comparerait deux sons décalés et tout
    /// paraîtrait faux.
    void setOffsetSeconds(double seconds) { offsetSeconds_.store(seconds, std::memory_order_release); }
    double offsetSeconds() const { return offsetSeconds_.load(std::memory_order_acquire); }

    /// La reconstruction doit-elle être rendue muette ? Vrai en mode Solo.
    bool silencesReconstruction() const { return mode() == Mode::Solo; }

    /// Mélange la référence dans la sortie. `startSeconds` est la position du
    /// transport au début du bloc.
    ///
    /// THREAD AUDIO : ni allocation, ni verrou, ni lecture de fichier.
    void mixInto(float* outputL, float* outputR, int numSamples, double startSeconds) const {
        const Mode courant = mode();
        if (courant == Mode::Off) return;
        const auto buffer = audio_.load(std::memory_order_acquire);
        if (!buffer || buffer->empty()) return;

        // Rapport de fréquences : l'enregistrement est souvent à 44,1 kHz quand
        // le moteur tourne à 48. Le lire tel quel le transposerait d'un demi-ton
        // -- et la comparaison porterait alors sur une erreur qu'on aurait
        // introduite soi-même.
        const double pas = buffer->sampleRate / sampleRate_;
        double position = (startSeconds + offsetSeconds_.load(std::memory_order_acquire))
                          * buffer->sampleRate;
        const float niveau = gain_.load(std::memory_order_acquire);
        const auto frames = static_cast<double>(buffer->numFrames());
        const bool stereo = buffer->isStereo();

        for (int i = 0; i < numSamples; ++i, position += pas) {
            if (position < 0.0 || position >= frames - 1.0) continue;
            const auto index = static_cast<size_t>(position);
            const auto fraction = static_cast<float>(position - static_cast<double>(index));
            const float gauche = buffer->left[index]
                               + (buffer->left[index + 1] - buffer->left[index]) * fraction;
            const float droite = stereo
                ? buffer->right[index] + (buffer->right[index + 1] - buffer->right[index]) * fraction
                : gauche;
            outputL[i] += gauche * niveau;
            outputR[i] += droite * niveau;
        }
    }

private:
    double sampleRate_ = 48000.0;
    std::atomic<vsm::audio::io::SampleBufferPtr> audio_{};
    std::atomic<Mode> mode_{Mode::Off};
    std::atomic<float> gain_{1.0f};
    std::atomic<double> offsetSeconds_{0.0};
};

} // namespace vsm::audio::engine
