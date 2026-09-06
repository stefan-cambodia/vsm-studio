#pragma once
#include "vsm/audio/io/WavFileReader.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace vsm::audio::engine {

/// LA PRÉ-ÉCOUTE DU NAVIGATEUR (D32.1) -- le clic qui joue un échantillon.
///
/// À QUOI ELLE SERT. Le navigateur indexe les échantillons et savait seulement
/// les POSER sur une piste. Pour entendre l'un de deux cents fichiers, il
/// fallait le déposer, écouter, puis annuler -- trois gestes et une entrée
/// d'historique pour une question qui se pose deux cents fois.
///
/// TROIS RÈGLES, et chacune répond à un piège que `ReferenceTrack` avait déjà
/// eu à éviter :
///
///  1. ELLE NE PART JAMAIS DANS L'EXPORT. Le rendu hors ligne partage le même
///     `processBlock` que la lecture ; exporter un morceau avec un échantillon
///     de pré-écoute dedans donnerait un fichier que personne n'a demandé.
///     C'est `setEnabled(false)` que l'export pose, et un test le vérifie AU
///     BIT PRÈS.
///  2. ELLE PASSE APRÈS LE BUS MASTER. Un échantillon qu'on essaie ne doit pas
///     traverser le compresseur du morceau : on l'écouterait coloré, et l'on
///     choisirait sur une couleur qu'il n'aura pas.
///  3. ELLE NE DÉPEND PAS DU TRANSPORT. Une pré-écoute part à l'instant du
///     clic et court jusqu'au bout du fichier, transport arrêté ou non. Elle
///     tient donc sa PROPRE position, et c'est ce qui la distingue de la piste
///     de référence, qui suit la tête de lecture.
class AuditionPlayer {
public:
    void prepare(double sampleRate) { sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }

    /// Publie un tampon et le lance depuis le début. Thread UI ou thread de
    /// chargement, jamais le thread audio : celui-ci ne fait que lire un
    /// pointeur déjà valide.
    ///
    /// UN DÉCLENCHEMENT COUPE LE PRÉCÉDENT, net : deux échantillons qui se
    /// superposeraient ne s'écoutent ni l'un ni l'autre, et c'est justement à
    /// la volée qu'on parcourt un dossier.
    void trigger(vsm::audio::io::SampleBufferPtr audio) {
        position_.store(0.0, std::memory_order_release);
        audio_.store(std::move(audio), std::memory_order_release);
        playing_.store(true, std::memory_order_release);
    }

    /// Arrête et libère. `trigger(nullptr)` ferait de même, mais dire « stop »
    /// est plus clair là où on l'appelle.
    void stop() {
        playing_.store(false, std::memory_order_release);
        audio_.store(nullptr, std::memory_order_release);
    }

    bool isPlaying() const {
        return playing_.load(std::memory_order_acquire)
               && audio_.load(std::memory_order_acquire) != nullptr;
    }

    void setGain(float gain) { gain_.store(std::clamp(gain, 0.0f, 2.0f), std::memory_order_release); }
    float gain() const { return gain_.load(std::memory_order_acquire); }

    /// L'EXPORT LA COUPE. Le rendu hors ligne pose `false` avant de rendre et
    /// le remet après ; coupée, `mixInto` sort avant d'avoir touché la sortie.
    void setEnabled(bool on) { enabled_.store(on, std::memory_order_release); }
    bool enabled() const { return enabled_.load(std::memory_order_acquire); }

    /// Mélange la pré-écoute dans la sortie et avance sa propre position.
    ///
    /// THREAD AUDIO : ni allocation, ni verrou, ni lecture de fichier.
    void mixInto(float* outputL, float* outputR, int numSamples) {
        if (!enabled_.load(std::memory_order_acquire)) return;
        if (!playing_.load(std::memory_order_acquire)) return;
        const auto buffer = audio_.load(std::memory_order_acquire);
        if (!buffer || buffer->empty()) return;

        // Rapport de fréquences, comme pour la référence : un échantillon à
        // 44,1 kHz lu tel quel à 48 sonnerait un demi-ton trop haut, et l'on
        // choisirait sur une hauteur qu'il n'a pas.
        const double pas = buffer->sampleRate / sampleRate_;
        double position = position_.load(std::memory_order_acquire);
        const float niveau = gain_.load(std::memory_order_acquire);
        const auto frames = static_cast<double>(buffer->numFrames());
        const bool stereo = buffer->isStereo();

        for (int i = 0; i < numSamples; ++i, position += pas) {
            if (position >= frames - 1.0) {
                // ARRIVÉE AU BOUT : on s'arrête ici plutôt que de boucler. Une
                // pré-écoute qui tournerait en rond obligerait à la couper à
                // la main, ce qui est un geste de plus pour chaque fichier
                // qu'on essaie.
                playing_.store(false, std::memory_order_release);
                break;
            }
            if (position < 0.0) continue;
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
        position_.store(position, std::memory_order_release);
    }

private:
    std::atomic<vsm::audio::io::SampleBufferPtr> audio_{nullptr};
    std::atomic<double> position_{0.0};
    std::atomic<bool> playing_{false};
    std::atomic<bool> enabled_{true};
    std::atomic<float> gain_{1.0f};
    double sampleRate_ = 48000.0;
};

} // namespace vsm::audio::engine
