#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace vsm::audio::engine {

/// Alloue/vole des voix pour un synthé polyphonique. Générique : ne connaît
/// rien de la synthèse elle-même, uniquement le cycle de vie (active/non
/// active) des voix -- réutilisable par TOUTE future machine polyphonique
/// (Juno-106, Jupiter-8, Prophet-5...), pas seulement le synthé de test.
///
/// `VoiceT` doit exposer : `bool isActive() const`, `uint8_t note() const`,
/// `uint8_t channel() const`, `void noteOn(channel, note, velocity)`,
/// `void noteOff(velocity)`.
///
/// Politique de vol de voix : "oldest note stealing" (la voix la plus
/// ancienne est volée en premier). C'est la politique la plus simple et la
/// plus prévisible ; une politique plus fine (prioriser les voix déjà en
/// Release, ou la plus silencieuse) est un raffinement Phase 6.
template <typename VoiceT, size_t MaxVoices>
class VoiceManager {
public:
    VoiceT* noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
        for (auto& slot : voices_) {
            if (!slot.voice.isActive()) {
                slot.voice.noteOn(channel, note, velocity);
                slot.startOrder = nextOrder_++;
                return &slot.voice;
            }
        }
        // Aucune voix libre : vole la plus ancienne.
        size_t oldestIndex = 0;
        for (size_t i = 1; i < voices_.size(); ++i)
            if (voices_[i].startOrder < voices_[oldestIndex].startOrder)
                oldestIndex = i;

        voices_[oldestIndex].voice.noteOn(channel, note, velocity);
        voices_[oldestIndex].startOrder = nextOrder_++;
        return &voices_[oldestIndex].voice;
    }

    /// Relâche TOUTES les voix actives correspondant à (channel, note) --
    /// un retrigger rapide peut avoir laissé plusieurs voix sur la même
    /// hauteur (l'ancienne encore en release, la nouvelle qui vient de
    /// démarrer), le comportement standard est de toutes les relâcher.
    void noteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
        for (auto& slot : voices_)
            if (slot.voice.isActive() && slot.voice.channel() == channel && slot.voice.note() == note)
                slot.voice.noteOff(velocity);
    }

    int activeVoiceCount() const {
        int count = 0;
        for (const auto& slot : voices_)
            if (slot.voice.isActive()) ++count;
        return count;
    }

    template <typename Fn>
    void forEachVoice(Fn&& fn) {
        for (auto& slot : voices_) fn(slot.voice);
    }

    /// Accès indexé, pour les machines qui traitent leurs voix par GROUPES
    /// plutôt qu'une par une -- typiquement le filtre vectorisé
    /// (LadderFilterZDFx4), qui filtre quatre voix d'un coup et a donc besoin
    /// de les adresser par leur position, pas seulement de les parcourir.
    /// L'index n'a aucune signification musicale : c'est un emplacement.
    VoiceT& voiceAt(size_t index) { return voices_[index].voice; }
    const VoiceT& voiceAt(size_t index) const { return voices_[index].voice; }

    static constexpr size_t maxVoices() { return MaxVoices; }

private:
    struct Slot {
        VoiceT voice{};
        uint64_t startOrder = 0;
    };
    std::array<Slot, MaxVoices> voices_{};
    uint64_t nextOrder_ = 1;
};

} // namespace vsm::audio::engine
