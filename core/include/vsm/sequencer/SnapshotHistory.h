#pragma once
#include <cstddef>
#include <deque>
#include <string>
#include <utility>
#include <vector>

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
        // Une nouvelle édition invalide la branche "rétablir" -- gardée de côté
        // (D511) le temps de savoir si l'édition a changé quelque chose.
        redoAvantDernierPas_ = std::move(redoStack_);
        redoStack_.clear();
        dernierPasRetirable_ = true;
    }

    /// D511 : L'ÉTAT MÉMORISÉ PAR LE DERNIER `beginEdit` (nul si la pile est vide),
    /// pour comparer ce que l'édition a produit à ce qu'il y avait avant.
    const StateT* etatDuDernierPas() const { return undoStack_.empty() ? nullptr : &undoStack_.back().state; }

    /// D511 : RETIRE LE DERNIER PAS, QUI N'A RIEN CHANGÉ, et rend la branche
    /// « rétablir » que son `beginEdit` avait vidée. Un geste sans effet (arpéger
    /// des notes seules, fusionner ce qui ne se touche pas) laissait un Ctrl+Z qui
    /// n'annulait rien — et effaçait ce qu'on pouvait rétablir. Permis SEULEMENT
    /// juste après le `beginEdit` : après un annuler ou un rétablir, le dernier pas
    /// n'est plus celui qu'on vient d'ouvrir. (Si la pile était pleine, le plus
    /// ancien pas, déjà sorti, ne revient pas : il était perdu de toute façon.)
    bool retirerLeDernierPas() {
        if (!dernierPasRetirable_ || undoStack_.empty()) return false;
        undoStack_.pop_back();
        redoStack_ = std::move(redoAvantDernierPas_);
        redoAvantDernierPas_.clear();
        dernierPasRetirable_ = false;
        return true;
    }

    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    /// Libellé de la prochaine action annulable/rétablissable (vide si aucune).
    std::string undoLabel() const { return undoStack_.empty() ? std::string() : undoStack_.back().label; }
    std::string redoLabel() const { return redoStack_.empty() ? std::string() : redoStack_.back().label; }

    /// Restaure l'état précédent dans `current` (qui est empilé côté rétablir).
    bool undo(StateT& current) {
        oublierLeRetrait();
        if (undoStack_.empty()) return false;
        Entry entry = std::move(undoStack_.back());
        undoStack_.pop_back();
        redoStack_.push_back({current, entry.label});
        current = std::move(entry.state);
        return true;
    }

    bool redo(StateT& current) {
        oublierLeRetrait();
        if (redoStack_.empty()) return false;
        Entry entry = std::move(redoStack_.back());
        redoStack_.pop_back();
        undoStack_.push_back({current, entry.label});
        current = std::move(entry.state);
        return true;
    }

    void clear() { undoStack_.clear(); redoStack_.clear(); oublierLeRetrait(); }

    /// LES LIBELLÉS, POUR UNE FENÊTRE D'HISTORIQUE (D11) : les pas
    /// annulables du plus ancien au plus récent, les pas rétablissables du
    /// prochain au plus lointain. Des copies : la pile reste à elle.
    std::vector<std::string> undoLabels() const {
        std::vector<std::string> labels;
        labels.reserve(undoStack_.size());
        for (const auto& e : undoStack_) labels.push_back(e.label);
        return labels;
    }
    std::vector<std::string> redoLabels() const {
        std::vector<std::string> labels;
        labels.reserve(redoStack_.size());
        for (auto it = redoStack_.rbegin(); it != redoStack_.rend(); ++it) labels.push_back(it->label);
        return labels;
    }
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
    /// D511 : la branche « rétablir » vidée par le dernier `beginEdit`, et s'il
    /// peut encore être retiré.
    std::deque<Entry> redoAvantDernierPas_;
    bool dernierPasRetirable_ = false;
    void oublierLeRetrait() { redoAvantDernierPas_.clear(); dernierPasRetirable_ = false; }
};

} // namespace vsm::sequencer
