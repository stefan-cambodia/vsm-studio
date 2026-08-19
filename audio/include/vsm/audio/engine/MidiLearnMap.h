#pragma once
#include "vsm/audio/plugin/ParameterTypes.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vsm::audio::engine {

/// Cible d'un apprentissage MIDI : quel paramètre de quelle piste, et sur
/// quelle plage le mapper. `min`/`max` proviennent du ParameterInfo du
/// plugin, pour convertir une valeur CC 0..127 en valeur de paramètre.
struct MidiLearnTarget {
    size_t trackIndex = 0;
    vsm::audio::plugin::ParamId paramId = 0;
    float min = 0.0f;
    float max = 1.0f;
    bool valid = false;
};

/// Associe des numéros de contrôleur MIDI (CC) à des cibles de paramètre.
/// Logique pure (aucune dépendance JUCE / audio) -> testable isolément. La
/// synchronisation (accès depuis le thread MIDI et le thread UI) est gérée
/// par l'appelant (AudioEngine), pas ici.
///
/// Correspondance 1:1 par CC : re-lier un CB déjà mappé remplace la cible.
class MidiLearnMap {
public:
    void bind(uint8_t controller, const MidiLearnTarget& target) {
        clearController(controller);
        entries_.push_back({controller, target});
    }

    void clearController(uint8_t controller) {
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->controller == controller) { entries_.erase(it); return; }
        }
    }

    void clearAll() { entries_.clear(); }

    bool hasController(uint8_t controller) const {
        for (const auto& e : entries_)
            if (e.controller == controller) return true;
        return false;
    }

    size_t size() const { return entries_.size(); }

    /// Résout un message CC (controller + valeur 7 bits) en cible + valeur de
    /// paramètre. Renvoie false s'il n'existe aucune correspondance.
    bool resolve(uint8_t controller, uint8_t value7bit,
                 MidiLearnTarget& outTarget, float& outValue) const {
        for (const auto& e : entries_) {
            if (e.controller == controller) {
                outTarget = e.target;
                const float norm = static_cast<float>(value7bit) / 127.0f;
                outValue = e.target.min + norm * (e.target.max - e.target.min);
                return true;
            }
        }
        return false;
    }

    /// Accès en lecture pour l'UI (liste des mappings actuels).
    struct Entry { uint8_t controller; MidiLearnTarget target; };
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

} // namespace vsm::audio::engine
