#pragma once
#include "vsm/audio/plugin/ParameterTypes.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace vsm::audio::engine {

/// CE QU'UN POTENTIOMÈTRE PHYSIQUE PEUT PILOTER (D10.2).
///
/// Il ne pouvait piloter qu'un paramètre de machine. C'était le plus utile, et
/// c'était aussi le seul endroit où la valeur se règle par un `std::atomic` :
/// tout le reste -- volume, panoramique, muet, départs, transport -- vit dans
/// le PROJET, que seul le thread de l'interface a le droit de modifier. Le
/// manque n'était donc pas un oubli, c'était une frontière de threads, et
/// c'est en la nommant qu'on la franchit proprement (voir `AudioEngine`).
enum class MidiLearnKind : uint8_t {
    InstrumentParam = 0,  ///< un paramètre de la machine d'une piste
    TrackVolume,
    TrackPan,
    TrackMute,            ///< bascule : CC >= 64 = enfoncé
    TrackSolo,            ///< bascule
    TrackSend,            ///< le départ `slot` de la piste
    TransportPlay,        ///< bascule lecture / arrêt
    TransportStop,
    TransportRecord,
    TransportLoop,
};

/// Une cible n'appartient-elle pas à une piste ? Le transport et le bus master
/// n'en ont pas, et afficher « Piste 1 » à côté de « Lecture » serait faux.
inline bool targetHasTrack(MidiLearnKind kind) {
    return kind != MidiLearnKind::TransportPlay && kind != MidiLearnKind::TransportStop
           && kind != MidiLearnKind::TransportRecord && kind != MidiLearnKind::TransportLoop;
}

/// PAS DE « VOLUME GÉNÉRAL » DANS CETTE LISTE, ET C'EST UN CONSTAT, PAS UN
/// OUBLI : le modèle n'a pas de fader master. La tranche master est un
/// correcteur, un compresseur et un limiteur (voir `MasterBus`) ; lui ajouter
/// un gain de sortie pour que le MIDI learn ait quelque chose à piloter
/// mettrait dans le chemin audio un réglage qu'aucune interface ne montre, et
/// qu'on retrouverait un jour à une valeur qu'on n'a jamais choisie. « Piloter
/// le mixeur » veut donc dire ici : volume, panoramique, muet, solo et départs
/// des PISTES.

/// Une cible est-elle une BASCULE (on appuie) plutôt qu'un réglage continu ?
/// La distinction décide de ce qu'on fait d'une valeur de CC : un fader donne
/// une position, un bouton donne un appui, et traiter l'un comme l'autre ferait
/// démarrer la lecture au milieu d'une course de potentiomètre.
inline bool targetIsMomentary(MidiLearnKind kind) {
    switch (kind) {
        case MidiLearnKind::TrackMute:
        case MidiLearnKind::TrackSolo:
        case MidiLearnKind::TransportPlay:
        case MidiLearnKind::TransportStop:
        case MidiLearnKind::TransportRecord:
        case MidiLearnKind::TransportLoop:
            return true;
        default:
            return false;
    }
}

/// Cible d'un apprentissage MIDI : quoi, sur quelle piste, et sur quelle plage
/// le mapper. `min`/`max` proviennent du ParameterInfo du plugin, pour
/// convertir une valeur CC 0..127 en valeur de paramètre.
struct MidiLearnTarget {
    MidiLearnKind kind = MidiLearnKind::InstrumentParam;
    size_t trackIndex = 0;
    vsm::audio::plugin::ParamId paramId = 0;
    /// Le départ visé quand `kind == TrackSend`. Ignoré ailleurs.
    uint8_t slot = 0;
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
