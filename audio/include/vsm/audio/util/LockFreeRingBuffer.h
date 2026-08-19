#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace vsm::audio::util {

/// File circulaire lock-free à un seul producteur / un seul consommateur
/// (SPSC). C'est le mécanisme de base pour faire passer des événements
/// (MIDI, changements de patch) du thread UI vers le thread audio SANS
/// mutex ni allocation dans le chemin temps réel (section 13 du cahier des
/// charges). Un seul thread doit appeler push(), un seul (potentiellement
/// différent) doit appeler pop() -- utiliser plusieurs producteurs ou
/// plusieurs consommateurs sur la même instance n'est PAS sûr.
template <typename T, size_t Capacity>
class LockFreeRingBuffer {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity doit être une puissance de 2 (>= 2)");

public:
    bool push(const T& item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t nextHead = (head + 1) & kMask;
        if (nextHead == tail_.load(std::memory_order_acquire))
            return false; // plein
        buffer_[head] = item;
        head_.store(nextHead, std::memory_order_release);
        return true;
    }

    bool pop(T& outItem) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false; // vide
        outItem = buffer_[tail];
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    /// Nombre d'éléments en attente, approximatif si appelé depuis un
    /// troisième thread (usage typique : diagnostic/monitoring uniquement).
    size_t sizeApprox() const {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & kMask;
    }

    static constexpr size_t capacity() { return Capacity - 1; } // une case sacrifiée pour distinguer plein/vide

private:
    static constexpr size_t kMask = Capacity - 1;

    std::array<T, Capacity> buffer_{};
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

} // namespace vsm::audio::util
