#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
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
        undoStack_.push_back({stateBeforeEdit, std::move(label), etat_});
        etat_ = prochainEtat_++;   // D588.4 : l'état qui suit le pas est un état neuf
        rogner();   // D588.3 : le plus ancien pas sorti est GARDÉ pour être dit, plus perdu en silence
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
        etat_ = undoStack_.back().etat;   // D588.4 : rien n'a changé, l'état est celui d'avant le pas
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
        redoStack_.push_back({current, entry.label, etat_});
        current = std::move(entry.state);
        etat_ = entry.etat;
        return true;
    }

    bool redo(StateT& current) {
        oublierLeRetrait();
        if (redoStack_.empty()) return false;
        Entry entry = std::move(redoStack_.back());
        redoStack_.pop_back();
        undoStack_.push_back({current, entry.label, etat_});
        current = std::move(entry.state);
        etat_ = entry.etat;
        rogner();   // D588.3 : une profondeur abaissée entre-temps vaut aussi pour ce qu'on rétablit
        return true;
    }

    /// D588.4 : vidé, l'historique tient un état NEUF — un repère d'enregistrement d'avant ne le désigne plus, comme
    /// la profondeur 0 ne valait plus celle d'avant. D588.3 : et le compte des pas oubliés repart de zéro.
    void clear() {
        undoStack_.clear();
        redoStack_.clear();
        oublierLeRetrait();
        etat_ = prochainEtat_++;
        pasOublies_ = 0;
    }

    /// D588.4 : L'IDENTITÉ DE L'ÉTAT COURANT — un numéro qui ne sert qu'une fois : chaque pas en donne un neuf, annuler
    /// et rétablir rendent celui de l'état qu'ils ramènent, `retirerLeDernierPas` celui d'avant le pas. Deux états de
    /// même numéro sont le même état ; deux profondeurs égales ne le disaient pas (historique plein, ou annuler puis
    /// refaire autre chose : la marque « non enregistré » de D174 s'y trompait).
    uint64_t etatCourant() const { return etat_; }

    /// D588.3 : LA PROFONDEUR SE RÈGLE (au moins 1) ; abaissée sous le nombre de pas, elle sort aussitôt les plus
    /// anciens — dits d'un seul appel de `quandDesPasSortent`.
    void setMaxDepth(size_t profondeur) {
        maxDepth_ = profondeur == 0 ? 1 : profondeur;
        rogner();
    }
    /// D588.3 : APPELÉ À CHAQUE SORTIE, avec les libellés sortis (du plus ancien au plus récent) — un geste en sort un,
    /// une profondeur abaissée tous ceux qu'elle retire. Posé par celui qui tient l'historique, pour DIRE ce qui ne
    /// pourra plus s'annuler : les pas s'ouvrent par plusieurs portes (`beginProjectEdit`, le piano roll, les lanes),
    /// et une relève faite à l'une d'elles raterait les autres.
    std::function<void(const std::vector<std::string>&)> quandDesPasSortent;
    /// D588.3 : combien de pas sont sortis depuis le dernier `clear`.
    size_t pasOublies() const { return pasOublies_; }

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
    /// D564.1 : chaque état que l'historique peut RAPPELER (annuler, rétablir, et le rétablissement gardé d'un pas
    /// retirable) — ce qu'un nettoyage du dossier doit laisser, pour qu'aucun pas ne rappelle un fichier parti.
    template <class F> void forEachState(F&& f) const {
        for (const auto& e : undoStack_) f(e.state);
        for (const auto& e : redoStack_) f(e.state);
        for (const auto& e : redoAvantDernierPas_) f(e.state);
    }
    size_t redoDepth() const { return redoStack_.size(); }
    size_t maxDepth() const { return maxDepth_; }

private:
    struct Entry {
        StateT state;
        std::string label;
        uint64_t etat = 0;   ///< D588.4 : l'identité de `state`
    };
    std::deque<Entry> undoStack_, redoStack_;
    size_t maxDepth_;
    uint64_t etat_ = 0;           ///< D588.4
    uint64_t prochainEtat_ = 1;   ///< D588.4 : jamais rendu deux fois
    size_t pasOublies_ = 0;   ///< D588.3
    void rogner() {
        if (undoStack_.size() <= maxDepth_) return;
        std::vector<std::string> sortis;
        while (undoStack_.size() > maxDepth_) {
            sortis.push_back(std::move(undoStack_.front().label));
            undoStack_.pop_front();
            ++pasOublies_;
        }
        if (quandDesPasSortent) quandDesPasSortent(sortis);
    }
    /// D511 : la branche « rétablir » vidée par le dernier `beginEdit`, et s'il
    /// peut encore être retiré.
    std::deque<Entry> redoAvantDernierPas_;
    bool dernierPasRetirable_ = false;
    void oublierLeRetrait() { redoAvantDernierPas_.clear(); dernierPasRetirable_ = false; }
};

} // namespace vsm::sequencer
