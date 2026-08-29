#pragma once
#include <JuceHeader.h>
#include "vsm/sequencer/AutomationEdit.h"
#include "vsm/sequencer/ClipEdit.h"
#include "vsm/sequencer/Quantizer.h"
#include "vsm/sequencer/Project.h"
#include <functional>
#include <map>
#include <string>

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
    /// LES BORNES D'UN PARAMÈTRE AUTOMATISÉ (D5.4), pour savoir où placer un
    /// point dans la hauteur de la piste. Fournies par l'application : elles
    /// viennent des listes de paramètres des machines et des effets, que ce
    /// composant n'a pas à connaître. Rend faux quand le paramètre est inconnu
    /// -- la courbe est alors dessinée mais non modifiable, plutôt que
    /// modifiable sur une échelle inventée.
    std::function<bool(size_t trackIndex, const std::string& parameter,
                        float& minimum, float& maximum)> automationRange;

    /// Bascule l'affichage des courbes. `A` au clavier.
    void toggleAutomation();
    bool automationVisible() const { return automationVisible_; }
    /// Choisit la courbe montrée sur une piste (index dans `Track::automation`).
    void showAutomationCurve(size_t trackIndex, int curveIndex);

    /// L'utilisateur a cliqué le bandeau de couleur d'une piste : c'est à
    /// l'application d'ouvrir le sélecteur, ce composant ne connaît pas JUCE
    /// au-delà du dessin.
    std::function<void(size_t)> onColourRequested;

    void zoomHorizontally(float facteur);
    /// Suppr. efface la sélection, +/- zooment. Publique pour que les fenêtres
    /// voisines puissent renvoyer une touche non consommée.
    bool keyPressed(const juce::KeyPress& key) override;
    void setSnapEnabled(bool actif) { snap_ = actif; }
    bool snapEnabled() const { return snap_; }
    /// LA GRILLE FINE EST CELLE DU PIANO ROLL, lue à l'usage plutôt que
    /// recopiée (D5.2 : « mêmes gestes et mêmes raccourcis »). Deux réglages de
    /// grille dans deux vues du même morceau finiraient par se contredire, et
    /// l'utilisateur ne saurait plus laquelle il vient de changer.
    std::function<vsm::sequencer::GridResolution()> gridProvider;

    /// Supprime les clips sélectionnés.
    void deleteSelection();
    bool hasSelection() const { return !selection_.empty(); }
    /// Les mêmes trois gestes que le piano roll, aux mêmes raccourcis.
    void copySelection();
    void paste();
    void duplicateSelection();

    static constexpr int kHeaderWidth = 150;
    static constexpr int kRulerHeight = 22;
    /// Hauteur d'une piste PLIÉE. Assez pour son nom et rien d'autre : c'est
    /// tout l'intérêt de plier. Seize pistes pliées tiennent alors dans
    /// 16 x 20 + 22 = 342 pixels, ce que demande le critère de l'étape.
    static constexpr int kFoldedHeight = 20;
    static constexpr int kMinHeight = 24;
    static constexpr int kMaxHeight = 400;

private:
    /// Ce qu'on est en train de faire à la souris. Un état explicite plutôt que
    /// trois booléens : « je déplace ET je redimensionne » n'existe pas, et
    /// l'écrire ainsi le rend impossible.
    enum class Geste { Aucun, Deplacer, BordGauche, BordDroit, Hauteur, Reordonner, Point };

    float tickToX(vsm::midi::Tick tick) const;
    vsm::midi::Tick xToTick(float x) const;
    vsm::midi::Tick snapTick(vsm::midi::Tick tick) const;
    int trackAtY(float y) const;
    /// La hauteur affichée d'une piste : celle qu'elle déclare, ou celle d'une
    /// piste pliée. Plier n'écrase pas le réglage, il le met de côté.
    int trackHeight(const vsm::sequencer::Track& track) const;
    /// Le haut de la piste `index`, en pixels, hauteurs variables comprises.
    int trackTop(size_t index) const;
    /// La zone du triangle de pliage, dans l'en-tête de la piste.
    juce::Rectangle<float> foldZone(size_t index) const;
    juce::Rectangle<float> colourZone(size_t index) const;
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
    /// Aimanter à la MESURE (le défaut, parce qu'on arrange par mesures) ou à
    /// la grille fine du piano roll. `G` bascule, `S` coupe l'aimantation.
    bool aimanteALaMesure_ = true;
    /// Le presse-papiers PORTE SES CLIPS, pas des identifiants : coller doit
    /// marcher après avoir supprimé l'original, et un identifiant ne désigne
    /// alors plus rien.
    std::vector<vsm::sequencer::Clip> presse_papiers_;
    /// La piste d'où vient le presse-papiers, pour y recoller par défaut.
    size_t pistePressePapiers_ = 0;
    size_t pisteCourante_ = 0;

    Geste geste_ = Geste::Aucun;
    vsm::midi::Tick gesteOrigine_ = 0;
    vsm::midi::Tick gesteDernier_ = 0;
    Geste survol_ = Geste::Aucun;
    /// La piste saisie par son en-tête, pendant un réordonnancement ou un
    /// réglage de hauteur.
    int pisteSaisie_ = -1;
    int hauteurOrigine_ = 0;
    float ySaisie_ = 0.0f;
    bool reordonnancementOuvert_ = false;

    // --- Automation dessinée SUR l'arrangement (D5.4) ---------------------
    //
    // « Plus une lane isolée dans un onglet » : une courbe se lit par rapport à
    // ce qu'elle pilote, et un onglet à part oblige à faire l'aller-retour des
    // yeux entre le fondu et le clip qu'il éteint.
    bool automationVisible_ = false;
    /// La courbe montrée par piste. -1 = la première qu'elle a, s'il y en a.
    std::map<size_t, int> courbeMontree_;
    int pisteCourbeSaisie_ = -1;
    size_t pointSaisi_ = 0;

    /// La courbe actuellement montrée sur une piste, ou nullptr.
    vsm::sequencer::AutomationCurve* curveShownOn(size_t trackIndex);
    /// Convertit une valeur en ordonnée dans la bande d'automation, et
    /// réciproquement.
    float valueToY(float valeur, float minimum, float maximum, int haut, int hauteur) const;
    float yToValue(float y, float minimum, float maximum, int haut, int hauteur) const;
};
