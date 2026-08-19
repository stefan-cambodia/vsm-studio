#pragma once
#include "vsm/sequencer/Track.h"
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace vsm::sequencer {

/// Historique d'édition annuler/rétablir, par INSTANTANÉS du vecteur de notes.
///
/// Pourquoi des instantanés plutôt qu'un journal de commandes inversibles :
/// une piste de piano roll, ce sont quelques milliers de `Note` de 32 octets,
/// soit quelques dizaines de kilo-octets par instantané -- négligeable pour
/// une profondeur de 128 pas, alors qu'un système de commandes inversibles
/// exigerait d'écrire (et de tester) une inverse correcte pour CHACUNE des
/// vingt et quelques opérations d'édition, y compris les composées. Le coût
/// est en mémoire, le gain est qu'aucune opération ne peut avoir un undo
/// faux : on restaure un état, on ne rejoue pas un raisonnement.
///
/// PROTOCOLE : l'appelant appelle beginEdit(état AVANT modification, libellé)
/// juste avant de modifier, puis modifie. undo() renvoie l'état précédent et
/// empile l'état courant côté rétablir.
class EditHistory {
public:
    explicit EditHistory(size_t maxDepth = 128) : maxDepth_(maxDepth == 0 ? 1 : maxDepth) {}

    /// Mémorise l'état AVANT une modification. Le libellé (« Transposer »,
    /// « Coller »...) sert à l'affichage du menu Édition.
    void beginEdit(const std::vector<Note>& stateBeforeEdit, std::string label) {
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
    bool undo(std::vector<Note>& current) {
        if (undoStack_.empty()) return false;
        Entry entry = std::move(undoStack_.back());
        undoStack_.pop_back();
        redoStack_.push_back({current, entry.label});
        current = std::move(entry.notes);
        return true;
    }

    bool redo(std::vector<Note>& current) {
        if (redoStack_.empty()) return false;
        Entry entry = std::move(redoStack_.back());
        redoStack_.pop_back();
        undoStack_.push_back({current, entry.label});
        current = std::move(entry.notes);
        return true;
    }

    /// À appeler quand on change de piste ou de projet : l'historique d'une
    /// piste n'a aucun sens appliqué à une autre (les identifiants de notes
    /// n'y existent pas).
    void clear() { undoStack_.clear(); redoStack_.clear(); }

    size_t undoDepth() const { return undoStack_.size(); }
    size_t redoDepth() const { return redoStack_.size(); }
    size_t maxDepth() const { return maxDepth_; }

private:
    struct Entry {
        std::vector<Note> notes;
        std::string label;
    };
    std::deque<Entry> undoStack_, redoStack_;
    size_t maxDepth_;
};

} // namespace vsm::sequencer
