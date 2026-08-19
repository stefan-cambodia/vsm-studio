#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Description des façades « façon hardware », machine par machine
// (sections 6 et 21 du cahier des charges).
//
// POURQUOI UNE DESCRIPTION PLUTÔT QUE DU CODE DE DESSIN PAR MACHINE : une
// façade, c'est une DISPOSITION -- quelles commandes, de quel type, dans
// quelle section, à quelle place. Écrire douze composants JUCE reviendrait à
// recopier douze fois la même mécanique (créer un potentiomètre, le relier à
// un paramètre, le placer, le redessiner), avec douze occasions de diverger.
// Ici, chaque machine est une DONNÉE que des tests peuvent vérifier -- sans
// serveur graphique -- et un unique composant sait rendre n'importe laquelle.
//
// CE QUE CES FAÇADES REPRODUISENT, ET CE QU'ELLES NE REPRODUISENT PAS : elles
// suivent l'agencement, les familles de commandes et l'esprit visuel des
// machines d'origine -- ce qui les rend reconnaissables et, surtout,
// utilisables par qui connaît l'original (les gestes sont au même endroit).
// Elles ne reprennent ni logo, ni marque, ni sérigraphie littérale : le projet
// dit « -style » partout, et cette règle vaut aussi pour l'image.
//
// Cette bibliothèque ne dépend ni de JUCE ni du moteur audio : elle décrit,
// elle ne dessine pas et ne sonne pas.

namespace vsm::panels {

enum class ControlStyle {
    Knob,             ///< potentiomètre rotatif standard
    LargeKnob,        ///< potentiomètre principal (coupure du Minimoog...)
    VerticalSlider,   ///< curseur vertical (Juno, Jupiter, SH-101, ARP)
    HorizontalSlider,
    Toggle,           ///< interrupteur à bascule, deux positions
    Selector,         ///< sélecteur à positions discrètes (forme d'onde)
    LedButton         ///< bouton lumineux (pas de séquenceur, accent)
};

/// Une commande de la façade. `parameterName` doit correspondre EXACTEMENT à
/// un paramètre de la machine (`ISynthPlugin::parameterList()`) : un test le
/// vérifie, pour qu'un renommage de paramètre ne laisse pas une façade
/// pointer dans le vide.
struct PanelControl {
    std::string parameterName;
    std::string caption;      ///< sérigraphie ; vide = reprendre le nom du paramètre
    ControlStyle style = ControlStyle::Knob;
    int column = 0;           ///< position en unités de grille, dans la SECTION
    int row = 0;
    int columnSpan = 1;
    int rowSpan = 1;
};

/// Un bloc fonctionnel de la façade (« OSCILLATOR BANK », « MODIFIERS »...),
/// tel que la machine d'origine le regroupe.
struct PanelSection {
    std::string title;
    std::string accentColour = "#8A8892"; ///< liseré et sérigraphie du bloc
    int column = 0;                        ///< position dans la GRILLE de la façade
    int row = 0;
    int columnSpan = 1;
    int rowSpan = 1;
    /// Nombre de colonnes de la grille INTERNE du bloc. 0 = déduit des
    /// commandes. À forcer quand un bloc large ne contient que deux ou trois
    /// commandes : sans cela, elles s'étalent sur toute la largeur au lieu de
    /// rester groupées comme sur la façade d'origine.
    int contentColumns = 0;
    std::vector<PanelControl> controls;
};

/// Matière du châssis : détermine le rendu des bords (flancs de bois du
/// Minimoog et du Prophet, tôle pliée des boîtes à rythmes, plastique).
enum class Chassis { Wood, Metal, Plastic };

/// Séquenceur intégré. Sur ces machines, ce n'est pas un accessoire : on ne
/// joue pas un TR-808 au clavier, on allume des pas. Une façade sans sa grille
/// serait fausse, quelle que soit la fidélité des potentiomètres.
enum class SequencerKind {
    None,
    DrumGrid,    ///< une ligne par pièce (boîtes à rythmes)
    MonoPattern  ///< une ligne, hauteur réglable par pas (TB-303)
};

struct SequencerSpec {
    SequencerKind kind = SequencerKind::None;
    std::string title = "PATTERN";
    int stepCount = 16;
    /// Lignes de la grille : nom sérigraphié + note MIDI de la pièce.
    std::vector<std::pair<std::string, int>> lanes;
    uint8_t defaultNote = 36; ///< motif mélodique : hauteur de départ
    /// Hauteur réservée au séquenceur, en rangées de la grille de façade.
    int rowSpan = 2;
    /// Couleurs des boutons de pas, par groupes de quatre temps -- c'est la
    /// signature visuelle de ces machines, et c'est ce qui permet de compter
    /// les temps d'un coup d'œil.
    std::vector<std::string> stepGroupColours = {"#C4462F", "#D96C2C", "#D9B23A", "#D8D2C4"};
};

struct MachinePanel {
    std::string pluginId;      ///< ex. "vsm.minimoog"
    std::string displayName;
    Chassis chassis = Chassis::Metal;
    std::string panelColour = "#2B2B30";   ///< fond de la façade
    std::string sectionColour = "#232328";  ///< fond des blocs
    std::string textColour = "#E8E6DF";
    /// Couleur des boutons et capuchons de curseur : noire sur les façades
    /// claires, claire sur les façades sombres. C'est un des traits qui font
    /// reconnaître une machine de loin.
    std::string knobColour = "#D8D5CC";
    int gridColumns = 12;
    int gridRows = 6;
    std::vector<PanelSection> sections;
    SequencerSpec sequencer;
    /// Paramètres volontairement absents de la façade, avec leur raison. Un
    /// test exige que CHAQUE paramètre soit soit posé sur la façade, soit
    /// listé ici : une commande oubliée en silence deviendrait un réglage
    /// inatteignable pour l'utilisateur.
    std::vector<std::pair<std::string, std::string>> omittedParameters;
};

/// Façade d'une machine, ou nullptr si elle n'en a pas encore (le panneau
/// générique prend alors le relais).
const MachinePanel* findMachinePanel(const std::string& pluginId);

/// Machines disposant d'une façade dédiée.
std::vector<std::string> machinePanelIds();

/// Toutes les commandes d'une façade, sections confondues (pour les tests et
/// pour le rendu).
std::vector<PanelControl> allControls(const MachinePanel& panel);

} // namespace vsm::panels
