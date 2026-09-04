#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>

namespace vsm::audio::dsp {

/// LA PRISE DU MASTER POUR L'ANALYSEUR DE SPECTRE (D15.3).
///
/// Le fil audio y dépose la somme mono du bus final, échantillon après
/// échantillon, dans un anneau de taille fixe ; le fil de l'interface vient y
/// lire les N derniers échantillons quand il veut dessiner. Aucune allocation,
/// aucun verrou : un compteur d'écriture atomique, et c'est tout.
///
/// CE QUE LA LECTURE PEUT VOIR DE FAUX, ET POURQUOI C'EST ACCEPTÉ. Le lecteur
/// copie une fenêtre que l'écrivain peut être en train de dépasser : au pire,
/// quelques échantillons du bord de la fenêtre sont d'un tour plus récent.
/// C'est un tremblement d'une image de spectre à 25 images par seconde, pas
/// une mesure -- la mesure (LUFS, crête, corrélation) vit dans `MasterBus`,
/// et cette prise ne la remplace pas. Un tampon SPSC classique (`pop`) ne
/// conviendrait pas : la vue veut « les 4 096 derniers », pas « ce qui
/// n'a pas encore été lu ».
///
/// ÉTEINTE PAR DÉFAUT : la fenêtre d'analyse l'allume en s'ouvrant et
/// l'éteint en se fermant. Sans analyseur ouvert, le fil audio ne paie
/// qu'un test atomique par bloc.
class SpectrumTap {
public:
    static constexpr size_t kCapacity = 16384;   // puissance de deux : l'index se masque
    static_assert((kCapacity & (kCapacity - 1)) == 0);

    void setEnabled(bool on) { enabled_.store(on, std::memory_order_relaxed); }
    bool enabled() const { return enabled_.load(std::memory_order_relaxed); }

    /// Fil audio. Somme mono (L + R) / 2, pour que l'échelle en dB soit celle
    /// d'un signal mono plein-échelle.
    void write(const float* left, const float* right, int numSamples) {
        if (!enabled() || numSamples <= 0) return;
        size_t w = written_.load(std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i) {
            ring_[w & (kCapacity - 1)] = 0.5f * (left[i] + right[i]);
            ++w;
        }
        written_.store(w, std::memory_order_release);
    }

    /// Fil de l'interface : les `n` derniers échantillons, dans l'ordre, dans
    /// `out` (n ≤ kCapacity). Ce qui n'a pas encore été écrit est à zéro.
    /// Rend le nombre d'échantillons réellement disponibles.
    size_t readLatest(float* out, size_t n) const {
        if (n > kCapacity) n = kCapacity;
        const size_t w = written_.load(std::memory_order_acquire);
        const size_t disponibles = w < n ? w : n;
        const size_t zeros = n - disponibles;
        for (size_t i = 0; i < zeros; ++i) out[i] = 0.0f;
        const size_t depart = w - disponibles;
        for (size_t i = 0; i < disponibles; ++i) out[zeros + i] = ring_[(depart + i) & (kCapacity - 1)];
        return disponibles;
    }

    size_t totalWritten() const { return written_.load(std::memory_order_acquire); }

private:
    std::array<float, kCapacity> ring_{};
    std::atomic<size_t> written_{0};
    std::atomic<bool> enabled_{false};
};

} // namespace vsm::audio::dsp
