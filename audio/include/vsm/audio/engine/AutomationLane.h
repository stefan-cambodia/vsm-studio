#pragma once
#include "vsm/audio/plugin/ParameterTypes.h"
#include "vsm/midi/MidiEvent.h" // vsm::midi::Tick
#include <algorithm>
#include <cstddef>
#include <vector>

namespace vsm::audio::engine {

using vsm::midi::Tick;

enum class AutomationCurve { Linear, Step };

struct AutomationPoint {
    Tick tick = 0;
    float value = 0.0f;
    AutomationCurve curveToNext = AutomationCurve::Linear; // forme du segment DEPUIS ce point
};

/// Une lane d'automation cible un paramètre d'une instance de plugin
/// (section 17 : "Track 1 -> Juno-106 -> Filter Cutoff"). `valueAt()` est
/// une fonction pure -- comme TempoMap/PlaybackScheduler dans vsm_core,
/// c'est ce qui garantit une lecture "sample accurate" identique en temps
/// réel et lors d'un futur rendu offline.
///
/// Cible une piste par INDEX plutôt que par id-chaîne : Phase 2 ne modélise
/// qu'un seul instrument par piste, l'index de piste est donc une clé
/// suffisante et évite toute comparaison de chaînes dans le chemin temps
/// réel (ProcessGraph::processBlock). Un id d'instance plus riche (pour un
/// futur multi-instruments par piste / sends) pourra remplacer ceci sans
/// changer la logique d'interpolation ci-dessous.
/// CE QU'UNE COURBE PILOTE (D4.6).
///
/// Jusqu'ici il n'y en avait qu'un seul : le paramètre d'un instrument. Un
/// fondu — le geste d'automation le plus courant qui soit — était donc
/// impossible à écrire, et le mixage tout entier échappait à l'automation
/// alors que le format savait déjà l'écrire.
enum class AutomationTarget {
    InstrumentParam,  ///< un réglage de la machine de la piste (le cas historique)
    TrackVolume,      ///< le fader de la piste
    TrackPan,         ///< son panoramique
    TrackSend,        ///< son niveau vers le bus `targetSlot`
    InsertParam,      ///< un réglage de l'insert n° `targetSlot` de la piste
    MasterParam,      ///< un réglage de la tranche master (la piste est ignorée)
};

class AutomationLane {
public:
    AutomationTarget target = AutomationTarget::InstrumentParam;
    size_t targetTrackIndex = 0;
    /// Numéro de bus (pour `TrackSend`) ou d'insert (pour `InsertParam`).
    size_t targetSlot = 0;
    vsm::audio::plugin::ParamId targetParam = 0;

    void addPoint(Tick tick, float value, AutomationCurve curve = AutomationCurve::Linear) {
        auto it = std::find_if(points_.begin(), points_.end(),
                                [tick](const AutomationPoint& p) { return p.tick == tick; });
        if (it != points_.end()) {
            it->value = value;
            it->curveToNext = curve;
            return;
        }
        points_.push_back({tick, value, curve});
        std::sort(points_.begin(), points_.end(),
                  [](const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });
    }

    void clear() { points_.clear(); }

    /// Valeur interpolée à `tick`. En dehors de la plage définie, la valeur
    /// est maintenue constante (celle du premier/dernier point).
    float valueAt(Tick tick) const {
        if (points_.empty()) return 0.0f;
        if (tick <= points_.front().tick) return points_.front().value;
        if (tick >= points_.back().tick) return points_.back().value;

        for (size_t i = 0; i + 1 < points_.size(); ++i) {
            const auto& a = points_[i];
            const auto& b = points_[i + 1];
            if (tick >= a.tick && tick <= b.tick) {
                if (a.curveToNext == AutomationCurve::Step || b.tick == a.tick)
                    return a.value;
                double ratio = static_cast<double>(tick - a.tick) / static_cast<double>(b.tick - a.tick);
                return static_cast<float>(a.value + ratio * (b.value - a.value));
            }
        }
        return points_.back().value; // ne devrait pas être atteint, filet de sécurité
    }

    const std::vector<AutomationPoint>& points() const { return points_; }

private:
    std::vector<AutomationPoint> points_; // toujours trié par tick croissant
};

} // namespace vsm::audio::engine
