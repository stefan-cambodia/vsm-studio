#pragma once
#include <JuceHeader.h>
#include "vsm/sequencer/ClipEdit.h"
#include "vsm/sequencer/Project.h"
#include <functional>

// LA VUE D'ARRANGEMENT (D5.1 de docs/ROADMAP-daw.md).
//
// D1 a mis les clips dans le MODÈLE : ils s'y rangeaient, s'y sauvegardaient et
// s'y jouaient, mais rien ne permettait de les toucher. Un morceau ne
// s'arrangeait donc pas -- on pouvait éditer les notes d'une piste dans le
// piano roll, et c'est tout. Déplacer un refrain demandait de déplacer chaque
// note qui le compose.
//
// CE COMPOSANT NE CONTIENT AUCUNE LOGIQUE DE MONTAGE, et c'est délibéré :
// déplacer, redimensionner et couper sont dans `vsm::sequencer::ClipEdit`, en
// fonctions pures, testées sans serveur graphique. Ici il n'y a que du dessin,
// des coordonnées et des gestes -- ce qui reste quand on a retiré ce qui peut
// être faux en silence.
class ArrangementComponent : public juce::Component {
public:
    ArrangementComponent();

    void setProject(vsm::sequencer::Project* project);
    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;

    /// La tête de lecture, pour que la vue montre où l'on en est.
    void setPlayheadTick(vsm::midi::Tick tick);
    /// Un geste va modifier le projet : l'application prend son instantané
    /// d'annulation. Appelé UNE fois par geste, au `mouseDown` -- un
    /// glissement continu est une action, pas trois cents.
    std::function<void(const juce::String&)> onEditStarted;
    /// Le projet a changé : republier au moteur.
    std::function<void()> onClipsChanged;
    /// L'utilisateur veut déplacer la tête de lecture.
    std::function<void(vsm::midi::Tick)> onPlayheadRequested;
    /// Une piste a été choisie (clic sur son en-tête ou sur un de ses clips).
    std::function<void(size_t)> onTrackSelected;

    void zoomHorizontally(float facteur);
    /// Suppr. efface la sélection, +/- zooment. Publique pour que les fenêtres
    /// voisines puissent renvoyer une touche non consommée.
    bool keyPressed(const juce::KeyPress& key) override;
    void setSnapEnabled(bool actif) { snap_ = actif; }
    bool snapEnabled() const { return snap_; }
    /// Supprime les clips sélectionnés.
    void deleteSelection();
    bool hasSelection() const { return !selection_.empty(); }

    static constexpr int kHeaderWidth = 150;
    static constexpr int kRulerHeight = 22;
    static constexpr int kTrackHeight = 56;

private:
    /// Ce qu'on est en train de faire à la souris. Un état explicite plutôt que
    /// trois booléens : « je déplace ET je redimensionne » n'existe pas, et
    /// l'écrire ainsi le rend impossible.
    enum class Geste { Aucun, Deplacer, BordGauche, BordDroit };

    float tickToX(vsm::midi::Tick tick) const;
    vsm::midi::Tick xToTick(float x) const;
    vsm::midi::Tick snapTick(vsm::midi::Tick tick) const;
    int trackAtY(float y) const;
    /// Le clip sous le point donné, et le bord qu'on y touche.
    vsm::sequencer::Clip* clipAt(juce::Point<float> point, size_t& trackIndex, Geste& bord);
    vsm::midi::Tick materialEnd(const vsm::sequencer::Track& track) const;
    void notifyChanged();

    vsm::sequencer::Project* project_ = nullptr;
    vsm::sequencer::ClipSelection selection_;
    vsm::midi::Tick playhead_ = 0;
    vsm::midi::Tick scrollTick_ = 0;
    double pixelsPerTick_ = 0.06;
    bool snap_ = true;

    Geste geste_ = Geste::Aucun;
    vsm::midi::Tick gesteOrigine_ = 0;
    vsm::midi::Tick gesteDernier_ = 0;
    Geste survol_ = Geste::Aucun;
};
