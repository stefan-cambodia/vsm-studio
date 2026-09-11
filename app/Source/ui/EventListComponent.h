#pragma once
#include <JuceHeader.h>
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/sequencer/EventList.h"
#include "vsm/sequencer/Project.h"
#include <functional>
#include <vector>

namespace vsm::app::ui {

/// LA LISTE DES ÉVÉNEMENTS (D32.2) -- l'éditeur de liste de Cubase.
///
/// CE QU'ELLE MONTRE, ET QUE RIEN NE MONTRAIT. Les notes se voient au piano
/// roll, les contrôleurs dans leur onglet ; les changements de programme, les
/// plis de hauteur, la pression de canal et la pression polyphonique n'avaient
/// aucune vue. Le modèle les portait, le planificateur les jouait, l'import les
/// gardait : on pouvait ouvrir un morceau importé qui changeait de programme au
/// refrain sans jamais pouvoir le lire, encore moins le retirer.
///
/// LES NOMBRES SONT BRUTS -- 8192, pas « au centre ». C'est tout l'intérêt
/// d'une liste : le piano roll dessine, la liste chiffre. Un pli montré en
/// pourcentage obligerait à convertir de tête pour le comparer à ce que la
/// chaîne d'analyse a écrit.
///
/// ELLE EST PARTAGÉE AVEC `core/` : l'énumération et la suppression sont des
/// fonctions pures (`listTrackEvents`, `removeTrackEvent`), ce qui permet de
/// VÉRIFIER qu'aucune famille n'est oubliée sans ouvrir de fenêtre.
class EventListComponent : public juce::Component,
                            public juce::TableListBoxModel {
public:
    EventListComponent();

    void resized() override;
    void paint(juce::Graphics&) override;

    void setProject(vsm::sequencer::Project* project);
    /// Relit la piste choisie. Garde le filtre et, autant que possible, la
    /// ligne sélectionnée.
    void refresh();
    void setActiveTrack(int trackIndex);
    /// D94 : les colonnes, le filtre, le titre et le compte, dans la langue courante.
    void retraduire();

    /// Prévenu AVANT une suppression : c'est là que l'application prend son
    /// instantané d'annulation.
    std::function<void(const juce::String&)> onEditStarted;
    /// Un événement a été retiré : le projet doit être republié au séquenceur.
    std::function<void()> onEventsChanged;
    /// Double-clic sur une ligne : la tête de lecture va là.
    std::function<void(vsm::midi::Tick)> onSeekRequested;

    // --- TableListBoxModel ---
    int getNumRows() override { return static_cast<int>(lignes_.size()); }
    void paintRowBackground(juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell(juce::Graphics&, int row, int columnId, int w, int h, bool selected) override;
    void cellDoubleClicked(int row, int columnId, const juce::MouseEvent&) override;
    void deleteKeyPressed(int lastRowSelected) override;

private:
    void rebuild();
    /// Le texte d'une case -- une seule écriture, employée par le dessin comme
    /// par le compte rendu au journal.
    juce::String texteDe(const vsm::sequencer::EventRow& ligne, int columnId) const;

    vsm::sequencer::Project* project_ = nullptr;
    int activeTrack_ = -1;
    std::vector<vsm::sequencer::EventRow> lignes_;

    juce::Label titre_;
    /// Le filtre par nature. « Tout » en tête : la liste s'ouvre PLEINE, comme
    /// le navigateur -- on l'ouvre justement pour voir ce qu'il y a.
    juce::ComboBox filtre_;
    juce::Label compte_;
    juce::TableListBox table_ { "evenements", this };
};

} // namespace vsm::app::ui
