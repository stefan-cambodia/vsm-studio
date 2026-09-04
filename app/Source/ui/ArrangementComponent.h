#pragma once
#include <JuceHeader.h>
#include "vsm/audio/io/WaveformPeaks.h"
#include "vsm/sequencer/AutomationEdit.h"
#include <memory>
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
class ArrangementComponent : public juce::Component,
                              public juce::DragAndDropTarget {
public:
    ArrangementComponent();

    void setProject(vsm::sequencer::Project* project);
    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    /// D11.4 : double-clic sur un clip = le renommer.
    void mouseDoubleClick(const juce::MouseEvent&) override;
    juce::MouseCursor getMouseCursor() override;

    /// La tête de lecture, pour que la vue montre où l'on en est.
    void setPlayheadTick(vsm::midi::Tick tick);
    /// D11.3 : l'arrangement défile derrière la tête de lecture, par pages, comme
    /// le piano roll. `F` bascule, et la règle le dit.
    void setFollowPlayhead(bool suit) { followPlayhead_ = suit; repaint(); }
    bool followPlayhead() const { return followPlayhead_; }
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

    /// LE CACHE D'APERÇU d'une piste audio (D5.7), ou nullptr s'il n'y en a
    /// pas. Fourni par l'application : la vue ne lit aucun fichier -- c'est
    /// exactement ce qui fait que neuf minutes s'affichent sans la bloquer.
    std::function<std::shared_ptr<const std::vector<vsm::audio::io::PeakBin>>(size_t)>
        waveformProvider;
    /// La fréquence de la session, pour convertir la fenêtre d'un clip audio
    /// (qui est en secondes) en trames du cache.
    std::function<double()> sampleRateProvider;

    /// L'utilisateur a cliqué le bandeau de couleur d'une piste : c'est à
    /// l'application d'ouvrir le sélecteur, ce composant ne connaît pas JUCE
    /// au-delà du dessin.
    std::function<void(size_t)> onColourRequested;

    /// D10.1 : QUELQUE CHOSE A ÉTÉ LÂCHÉ DEPUIS LE NAVIGATEUR, sur une piste ET
    /// à un endroit de la ligne de temps.
    ///
    /// C'est ce que la liste des pistes ne pouvait pas fournir, et c'est
    /// pourquoi la pose d'un échantillon avait été laissée de côté : elle
    /// demande de décider quelle piste il devient ET où il commence. Cette
    /// seconde moitié de la réponse n'existe que dans l'arrangement.
    ///
    /// La description est celle du navigateur ; l'arrangement ne l'interprète
    /// pas — il n'a pas à savoir ce qu'est un `*.synth.json`.
    std::function<void(size_t trackIndex, vsm::midi::Tick tick, const juce::String& description)>
        onBrowserItemDropped;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

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
    /// D15.2 : LA SÉLECTION AU CLAVIER. Le pas est celui de l'aimantation --
    /// la mesure, ou la grille fine du piano roll selon `G` -- et c'est le
    /// même qu'à la souris ; annulable ; un clip ne passe pas avant zéro (la
    /// figure garde sa forme, voir `moveClips`).
    void nudgeSelection(vsm::midi::Tick delta);
    /// D15.2 : vers la piste voisine du même genre, en emportant les notes que
    /// la fenêtre couvre ; ce qui est refusé est compté et dit.
    void moveSelectionAcrossTracks(int deltaTracks);
    /// Le pas d'aimantation à cet endroit du morceau (mesure ou grille fine).
    vsm::midi::Tick snapStep(vsm::midi::Tick tick) const;
    bool hasSelection() const { return !selection_.empty(); }

    /// D18.3 : LA SÉLECTION TELLE QUE LE MONTAGE LA VOIT — celle de
    /// l'utilisateur, élargie aux pistes du même groupe d'édition.
    ///
    /// `selection_` reste EXACTEMENT ce qu'on a cliqué : un clic qui ferait
    /// grossir la sélection en silence rendrait impossible de savoir ce qu'on
    /// a pris. L'élargissement est calculé au moment de s'en servir, et il a
    /// DEUX consommateurs, un seul et même résultat : les gestes de temps, et
    /// le dessin (qui montre les clips liés comme choisis, sans quoi on
    /// couperait trois pistes en croyant en couper une).
    vsm::sequencer::ClipSelection montageSelection() const;
    /// L'ÉTENDUE DE LA SÉLECTION (D14.1), en ticks, toutes pistes confondues.
    /// Faux si rien n'est choisi.
    bool selectionBounds(vsm::midi::Tick& debut, vsm::midi::Tick& fin) const;
    /// ZOOM SUR TOUT / SUR LA SÉLECTION (D14.2) : ce que le piano roll avait
    /// et que l'arrangement n'avait pas.
    void zoomToFit();
    void zoomToSelection();
    /// LES BORNES DE LA SÉLECTION sur la ligne de temps, toutes pistes
    /// confondues (D6.1 : « exporter la sélection »). Rend faux si rien n'est
    /// sélectionné. Les clips sélectionnés peuvent appartenir à des pistes
    /// différentes : ce qu'on exporte est la PLAGE DE TEMPS qu'ils couvrent,
    /// avec tout ce qui sonne pendant -- exporter « seulement les pistes
    /// sélectionnées » est une autre fonction, celle des stems (D6.2).
    bool selectionTickRange(vsm::midi::Tick& debut, vsm::midi::Tick& fin) const;
    /// D18.1 : LES IDENTIFIANTS DES CLIPS CHOISIS, pour que l'application
    /// puisse reporter la sélection en audio. Elle en a besoin telle quelle :
    /// un report ne rend pas « la piste », il rend CE QUI EST CHOISI dessus.
    const vsm::sequencer::ClipSelection& selectedClipIds() const { return selection_; }
    /// Les mêmes trois gestes que le piano roll, aux mêmes raccourcis.
    void copySelection();
    void paste();
    void duplicateSelection();
    /// Tous les clips de toutes les pistes (Ctrl+A, D11.2).
    void selectAll();
    /// D18.3 : ne choisir QUE le premier clip de la piste `index`. Sert à
    /// photographier ce qu'un groupe d'édition fait -- « tout choisir » ne
    /// prouverait rien, puisque tout serait déjà pris.
    void selectFirstClipOf(size_t index);
    /// Appelé au relâchement quand un déplacement de piste a refusé des clips
    /// (nombre refusé) : la vue ne sait pas parler, l'application si.
    std::function<void(size_t)> onClipsRefused;
    /// D11.4 : renommer et colorer UN clip. Les fenêtres sont de JUCE, donc
    /// dans l'application ; la vue demande (piste, identifiant du clip).
    std::function<void(size_t, uint64_t)> onClipRenameRequested;
    std::function<void(size_t, uint64_t)> onClipColourRequested;

    /// « LE CLIP FAIT N MESURES » (D12.6, § 6 du CDC d'étirement) : la vue
    /// demande le nombre, l'application le saisit et dit le tempo déduit.
    /// C'est la même division du travail que pour renommer et colorer -- une
    /// fenêtre est une affaire d'application, pas de vue.
    std::function<void(size_t, uint64_t)> onClipBarsRequested;

    /// D17.6 : ROGNER LE CLIP À CE QUI SONNE. La vue demande ; la mesure a
    /// besoin des ÉCHANTILLONS du fichier, que l'application seule sait
    /// retrouver — un composant de dessin n'ouvre pas de fichier.
    std::function<void(size_t, uint64_t)> onClipTrimToSoundRequested;

    /// D17.2 : « L'AUTOMATION SUIT LES ÉVÉNEMENTS », la préférence de Cubase,
    /// active par défaut. Réglée par l'application, qui la retient ; la vue ne
    /// fait que la transmettre à `ClipEdit`, qui décide.
    void setAutomationFollowsClips(bool suit) { automationSuit_ = suit; }
    bool automationFollowsClips() const { return automationSuit_; }

    /// D16.5 : des clips d'une piste VERROUILLÉE ont été refusés (leur
    /// nombre), pour que l'application le dise. Le refus lui-même est dans
    /// `ClipEdit` ; la vue ne teste jamais le cadenas, elle le rapporte.
    std::function<void(size_t)> onLockRefused;

    /// JOINDRE ET COUPER AU CLAVIER (D16.3). `Ctrl+J` recolle les clips
    /// choisis quand le second est exactement ce qu'une coupe aurait produit
    /// du premier ; `Ctrl+E` coupe la sélection à la tête de lecture. Les deux
    /// étaient dans la table des raccourcis depuis D10 et seul le piano roll
    /// les entendait -- or c'est dans l'arrangement qu'on colle des clips.
    void joinSelection();
    void splitSelectionAtPlayhead();
    /// Ce qui n'a pas pu être joint (nombre de paires), pour que
    /// l'application le dise : la vue ne sait pas parler.
    std::function<void(size_t)> onJoinRefused;

    /// CRÉER UN CLIP (D16.1). Un double-clic sur le vide d'une piste demande
    /// un clip d'une mesure aimantée à cet endroit. La vue ne le fabrique pas
    /// elle-même : le geste doit être annulable et republié au moteur, et cela
    /// n'appartient pas à un composant de dessin -- même partage que pour les
    /// repères. Le tick est DÉJÀ aimanté quand il arrive.
    std::function<void(size_t trackIndex, vsm::midi::Tick tick)> onClipCreationRequested;

    /// LES REPÈRES DANS L'ARRANGEMENT (D16.4). `Project::markers` n'était
    /// dessiné et posé que par la règle du piano roll ; on naviguait donc à
    /// l'aveugle (Maj+N/B) là où l'on arrange. Mêmes rappels et mêmes gestes
    /// que `PianoRollRulerComponent` -- les fenêtres (le nom) sont de
    /// l'application, la vue ne fait que demander.
    std::function<void(vsm::midi::Tick)> onMarkerRequested;
    std::function<void(size_t)> onMarkerRenameRequested;
    std::function<void(size_t)> onMarkerRemoved;
    /// Le repère le plus proche d'une abscisse, à dix pixels près, ou -1 : on
    /// vise un trait à la souris, pas un tick.
    int markerAt(float x) const;

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
    enum class Geste { Aucun, Deplacer, BordGauche, BordDroit, Hauteur, Reordonner, Point,
                        FonduEntree, FonduSortie, Lasso, MarqueurWarp, Etirer,
                        /// D17.7 : la poignée du MILIEU d'un segment d'automation,
                        /// qu'on tire vers le haut ou le bas pour le courber.
                        CourbureAutomation };
    /// D17.7 : la courbure du segment saisi au moment du clic, et l'ordonnée
    /// du clic. La courbure se calcule par rapport à ces deux-là plutôt qu'en
    /// s'accumulant : un glissement qui repasse par son point de départ doit
    /// rendre la courbure de départ, au bit près.
    float courbureAuClic_ = 0.0f;
    float yAuClic_ = 0.0f;

    /// LE MARQUEUR DE WARP SOUS LE POINTEUR (D12.6), s'il y en a un. Rend
    /// l'indice dans `clip.warpMarkers`, ou -1. Les marqueurs ne sont
    /// saisissables que sur un clip qui suit le tempo : sur les autres il n'y
    /// en a pas à voir, et huit pixels de la largeur d'un clip ne doivent pas
    /// se comporter autrement sans raison visible.
    int marqueurAt(const vsm::sequencer::Clip& clip, float x) const;
    /// L'indice du marqueur qu'on déplace, et le tick du dernier clic droit --
    /// le menu en a besoin pour savoir OÙ ajouter un marqueur.
    int marqueurGeste_ = -1;
    vsm::midi::Tick clicTick_ = 0;

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
    /// Le clip saisi pour un fondu, pendant le geste.
    uint64_t clipFondu_ = 0;
    vsm::midi::Tick materialEnd(const vsm::sequencer::Track& track) const;
    void notifyChanged();

    /// La cible d'un glisser en cours : la piste survolée et la position
    /// aimantée. Sans ce retour, on lâche à l'aveugle -- et pour un
    /// échantillon, « à l'aveugle » veut dire à la mauvaise mesure.
    int dropTrack_ = -1;
    vsm::midi::Tick dropTick_ = 0;

    vsm::sequencer::Project* project_ = nullptr;
    vsm::sequencer::ClipSelection selection_;
    vsm::midi::Tick playhead_ = 0;
    vsm::midi::Tick scrollTick_ = 0;
    double pixelsPerTick_ = 0.06;
    bool snap_ = true;
    bool automationSuit_ = true;
    /// Aimanter à la MESURE (le défaut, parce qu'on arrange par mesures) ou à
    /// la grille fine du piano roll. `G` bascule, `S` coupe l'aimantation.
    bool aimanteALaMesure_ = true;
    bool followPlayhead_ = true;
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

    // --- D11.1 : le clip change de piste ; D11.2 : le lasso ---------------
    /// La piste sous le pointeur au dernier pas du déplacement : le décalage
    /// de pistes est RELATIF, comme celui du temps.
    int pisteDerniere_ = -1;
    /// Ce que le geste a refusé (clips audio vers un autre fichier, groupes),
    /// rendu à `onClipsRefused` au relâchement — jamais tu.
    size_t refusesPendantLeGeste_ = 0;
    juce::Point<float> lassoOrigine_;
    juce::Rectangle<float> lasso_;
    void selectClipsInLasso(bool etendre);
    void clipMenuAction(size_t piste, uint64_t clipId, int choix);

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
