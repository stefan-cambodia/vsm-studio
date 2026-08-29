#pragma once
#include <cstddef>
#include <deque>
#include <string>
#include <utility>

namespace vsm::sequencer {

/// Historique annuler/rétablir, par INSTANTANÉS d'un état copiable.
///
/// POURQUOI DES INSTANTANÉS PLUTÔT QU'UN JOURNAL DE COMMANDES INVERSIBLES :
/// un système de commandes exigerait d'écrire -- et de tester -- une inverse
/// correcte pour CHACUNE des vingt et quelques opérations d'édition, y compris
/// les composées, et pour tout ce qui s'y ajoutera. Le coût d'un instantané est
/// en mémoire ; le gain est qu'aucune opération ne peut avoir un « annuler »
/// faux : on restaure un état, on ne rejoue pas un raisonnement à l'envers.
///
/// PROTOCOLE : l'appelant appelle `beginEdit(état AVANT modification, libellé)`
/// juste avant de modifier, puis modifie. `undo()` restaure l'état précédent et
/// empile l'état courant côté rétablir.
///
/// LE TYPE DE L'INSTANTANÉ DIT CE QUE L'ANNULATION COUVRE, et c'est tout
/// l'enjeu. Un instantané du seul vecteur de notes d'une piste ne peut pas
/// annuler l'ajout d'une piste, un réglage de mixage ou une chaîne d'effets ; il
/// doit être vidé au changement de piste, faute de quoi il restaurerait les
/// notes d'une piste dans une autre. Un instantané du PROJET n'a aucune de ces
/// limites -- il coûte plus cher en mémoire, et c'est le seul prix.
template <typename StateT>
class SnapshotHistory {
public:
    explicit SnapshotHistory(size_t maxDepth = 128) : maxDepth_(maxDepth == 0 ? 1 : maxDepth) {}

    /// Mémorise l'état AVANT une modification. Le libellé (« Transposer »,
    /// « Coller »...) sert à l'affichage du menu Édition.
    void beginEdit(const StateT& stateBeforeEdit, std::string label) {
        undoStack_.push_back({stateBeforeEdit, std::move(label)});
        if (undoStack_.size() > maxDepth_) undoStack_.pop_front();
        redoStack_.clear(); // une nouvelle édition invalide la branche "rétablir"
    }

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    /// Libellé de la prochaine action annulable/rétablissable (vide si aucune).
    std::string undoLabel() const { return undoStack_.empty() ? std::string() : undoStack_.back().label; }
    std::string redoLabel() const { return redoStack_.empty() ? std::string() : redoStack_.back().label; }

    /// Restaure l'état précédent dans `current` (qui est empilé côté rétablir).
    bool undo(StateT& current) {
        if (undoStack_.empty()) return false;
        Entry entry = std::move(undoStack_.back());
        undoStack_.pop_back();
        redoStack_.push_back({current, entry.label});
        current = std::move(entry.state);
        return true;
    }

    bool redo(StateT& current) {
        if (redoStack_.empty()) return false;
        Entry entry = std::move(redoStack_.back());
        redoStack_.pop_back();
        undoStack_.push_back({current, entry.label});
        current = std::move(entry.state);
        return true;
    }

    void clear() { undoStack_.clear(); redoStack_.clear(); }

    size_t undoDepth() const { return undoStack_.size(); }
    size_t redoDepth() const { return redoStack_.size(); }
    size_t maxDepth() const { return maxDepth_; }

private:
    struct Entry {
        StateT state;
        std::string label;
    };
    std::deque<Entry> undoStack_, redoStack_;
    size_t maxDepth_;
};

} // namespace vsm::sequencer
