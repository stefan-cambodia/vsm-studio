#pragma once
#include <JuceHeader.h>
#include "FenetreDeTemps.h"
#include "LookAndFeel/VsmLookAndFeel.h"
#include "vsm/audio/engine/AutomationLane.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include "vsm/sequencer/Project.h"
#include <functional>
#include <vector>

// Éditeur de courbes d'automation (section 17, dernière pièce de l'UI Phase 2).
// L'utilisateur choisit une piste + un paramètre de son instrument, puis
// édite des points (clic = ajouter, glisser = déplacer, clic droit =
// supprimer). Chaque édition reconstruit la liste complète de lanes et la
// publie via onAutomationChanged -> ProcessGraph::setAutomationLanes (chemin
// RT-safe). L'axe horizontal = temps (ticks), vertical = valeur du paramètre
// (bornes lues dans son ParameterInfo).
//
// Aucune logique DSP ici : l'interpolation et l'application temps réel vivent
// dans vsm::audio::engine::AutomationLane / ProcessGraph (déjà testés).
class AutomationComponent : public juce::Component {
public:
    AutomationComponent();

    void paint(juce::Graphics&) override;
    void resized() override;
    /// D286 : la fenêtre de temps de l'arrangement. Fournie, la lane s'y aligne
    /// (même tick, même colonne d'écran) ; absente, elle montre le morceau entier.
    vsm::app::ui::FournisseurDeFenetre fenetreProvider;
    /// D285 : LA TÊTE DE LECTURE SE VOIT DANS LA LANE, comme dans l'arrangement
    /// et le piano roll -- un trait ambre, redessiné par colonne et non par lane.
    void setPlayheadTick(vsm::audio::engine::Tick tick);
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    /// (Re)lit les pistes depuis le projet. Conserve les lanes déjà éditées.
    void setProject(vsm::sequencer::Project* project);

    /// D234 (A41) : LES COURBES DU PROJET, DONNÉES AU PANNEAU.
    ///
    /// Ce panneau ne lisait JAMAIS les courbes du projet ouvert : `lanes_` ne
    /// contenait que ce qu'on y dessinait dans la session. Deux conséquences, et la
    /// seconde détruit : l'onglet montrait une zone vide sur un projet qui porte
    /// une automation, et le premier point posé publiait `lanes_` — c'est-à-dire
    /// RIEN — par-dessus l'automation du projet. Mesuré : `cdl`, une courbe de 606
    /// points sur `filter.1.cutoff`, un point posé, et le projet enregistré ne
    /// portait plus que le point posé.
    void setLanes(const std::vector<vsm::audio::engine::AutomationLane>& lanes);

    /// Fournit l'instrument d'une piste (pour lister ses paramètres + bornes).
    std::function<vsm::audio::plugin::ISynthPlugin*(size_t)> instrumentProvider;

    /// Émis après chaque édition : liste complète des lanes à publier.
    std::function<void(const std::vector<vsm::audio::engine::AutomationLane>&)> onAutomationChanged;

    /// D37.1 : relit les NOMS des pistes dans la liste déroulante, sans
    /// toucher aux lanes. Le nom d'une piste s'affiche à sept endroits ; celui
    /// d'ici était posé une fois pour toutes et montrait l'ancien indéfiniment.
    void refreshTrackNames() { rebuildTrackBox(); }
    /// D303 : L'ONGLET SUIT LA PISTE CHOISIE, comme ses trois voisins (Effets,
    /// MIDI CC, Liste) et le rack. Il gardait la piste de sa liste déroulante
    /// -- « bass » sous une piste de batterie sélectionnée -- seul des quatre.
    /// Le sens reste unique : choisir ici ne change pas la sélection globale.
    void setActiveTrackIndex(size_t trackIndex);
    /// D94 : les libellés, dans la langue courante.
    void retraduire();
    /// D234 : un point d'automation posé SANS SOURIS, par le même `mouseDown` que
    /// la souris (la leçon de D145) — `fraction` situe le clic dans la zone
    /// d'édition, 0 = bord gauche/haut, 1 = bord droit/bas. Rend faux si aucun
    /// paramètre n'est choisi. Le compte de points est écrit au journal.
    bool poserUnPointPourCapture(double fractionX, double fractionY);

private:
    void rebuildTrackBox();
    void rebuildParamBox();
    void loadSelectedLane();        // remplit editPoints_ depuis lanes_
    void commit();                  // reconstruit la lane sélectionnée + notifie

    juce::Rectangle<int> editorArea() const;
    void dessinerTete(juce::Graphics& g) const;   // D285
    int   tickToX(vsm::audio::engine::Tick tick) const;
    vsm::audio::engine::Tick xToTick(int x) const;
    int   valueToY(float value) const;
    float yToValue(int y) const;
    int   findPointNear(juce::Point<int> p) const;

    vsm::sequencer::Project* project_ = nullptr;

    juce::Label trackLabel_, paramLabel_, hintLabel_;
    juce::ComboBox trackBox_, paramBox_;

    struct ParamEntry { vsm::audio::plugin::ParamId id; float min; float max; };
    std::vector<ParamEntry> paramEntries_; // parallèle aux items de paramBox_

    std::vector<vsm::audio::engine::AutomationLane> lanes_;
    std::vector<vsm::audio::engine::AutomationPoint> editPoints_; // lane courante en cours d'édition

    size_t selectedTrack_ = 0;
    vsm::audio::plugin::ParamId selectedParam_ = 0;
    float paramMin_ = 0.0f, paramMax_ = 1.0f;
    bool hasSelection_ = false;

    int dragIndex_ = -1;
    vsm::audio::engine::Tick maxTick_ = 1920 * 4;
    vsm::audio::engine::Tick playheadTick_ = -1;   // D285 : hors lane tant qu'aucun tick reçu

    static constexpr int kPointRadius = 5;
};
