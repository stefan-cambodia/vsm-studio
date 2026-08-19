#pragma once
#include "vsm/sequencer/Track.h"
#include <cstdint>
#include <string>
#include <vector>

// Motifs à pas — le séquenceur intégré des boîtes à rythmes et du TB-303
// (sections 6 et 21 : « façon hardware »).
//
// POURQUOI CE MODÈLE EXISTE : sur ces machines, le séquenceur N'EST PAS un
// accessoire, c'est l'instrument. On ne joue pas un TR-808 au clavier, on
// allume des pas ; un TB-303 sans son éditeur de motif n'est qu'un filtre.
// Une façade sans grille de pas serait donc une façade fausse, quelle que
// soit la fidélité des potentiomètres.
//
// DÉCISION STRUCTURANTE : un motif n'est PAS une donnée parallèle. C'est une
// VUE sur les notes de la piste, convertie dans les deux sens. Stocker les
// motifs à part créerait deux vérités -- ce que montre la grille et ce que
// joue le moteur -- qui divergeraient à la première édition faite dans le
// piano roll. Ici, allumer un pas écrit une note ; dessiner cette note dans
// le piano roll allume le pas. Un seul morceau, deux façons de le regarder.

namespace vsm::sequencer {

/// Un pas. `accent` et `slide` sont les deux nuances que ces machines
/// possèdent réellement -- on ne modélise pas ce qu'elles n'ont pas.
struct StepCell {
    bool active = false;
    bool accent = false;
    /// Legato vers le pas suivant (TB-303). Sans objet pour une percussion.
    bool slide = false;
    /// Hauteur du pas pour un motif mélodique. 0 = reprendre celle de la lane
    /// (cas des percussions, où la hauteur désigne la pièce).
    uint8_t noteNumber = 0;
};

/// Une ligne du motif : une pièce de batterie, ou la ligne mélodique unique
/// d'un synthé monophonique.
struct StepLane {
    uint8_t noteNumber = 36; ///< pièce (36 = grosse caisse) ou hauteur de repli
    std::string name;        ///< « BASS DRUM », « SNARE »...
    std::vector<StepCell> steps;
};

struct StepPattern {
    int stepCount = 16;
    /// Durée d'un pas. 120 ticks = une double croche à 480 PPQ, la résolution
    /// de ces machines.
    midi::Tick stepTicks = 120;
    midi::Tick startTick = 0;
    std::vector<StepLane> lanes;

    /// Longueur totale du motif en ticks.
    midi::Tick lengthTicks() const { return static_cast<midi::Tick>(stepCount) * stepTicks; }
};

/// Vélocités : ces machines n'ont pas de clavier sensible, l'accent est leur
/// SEULE nuance. Deux valeurs franches valent mieux qu'un dégradé qui
/// n'existe pas sur l'objet réel.
inline constexpr uint8_t kStepVelocity = 92;
inline constexpr uint8_t kAccentVelocity = 127;

/// Proportion de la durée d'un pas qu'occupe une note « normale ». Un pas
/// n'est pas tenu jusqu'au suivant : c'est ce qui donne aux boîtes à rythmes
/// leur détaché. Un pas en slide, lui, chevauche le suivant (voir ci-dessous).
inline constexpr double kStepGateRatio = 0.5;

/// Convertit un motif en notes jouables.
///
/// Le SLIDE est traduit par un CHEVAUCHEMENT avec le pas suivant : c'est
/// exactement ainsi que le TB-303-style du projet interprète un slide (deux
/// notes qui se chevauchent = glissando sans réattaque, voir ARCHITECTURE.md
/// § 8). Le motif n'invente donc aucune convention : il produit ce que le
/// moteur sait déjà lire.
std::vector<Note> patternToNotes(const StepPattern& pattern, uint8_t channel, uint64_t& idCounter);

/// Relit un motif depuis des notes existantes, en utilisant `reference` pour
/// la grille (nombre de pas, durée, départ) et la liste des lignes. Les notes
/// hors grille sont ignorées : elles restent dans la piste et visibles au
/// piano roll, mais la grille ne prétend pas les représenter.
StepPattern patternFromNotes(const std::vector<Note>& notes, const StepPattern& reference);

/// Remplace, dans la piste, la zone couverte par le motif. Les notes situées
/// hors de cette zone ne sont pas touchées : une grille de 16 pas ne doit pas
/// effacer le reste du morceau.
void writePatternToTrack(Track& track, const StepPattern& pattern, uint64_t& idCounter);

/// Grille toute faite pour une boîte à rythmes, à partir de ses pièces.
StepPattern makeDrumPattern(const std::vector<std::pair<std::string, uint8_t>>& pieces,
                             int stepCount = 16, midi::Tick stepTicks = 120);

/// Grille monophonique (TB-303) : une ligne, hauteur réglable par pas.
StepPattern makeMonoPattern(uint8_t defaultNote = 36, int stepCount = 16, midi::Tick stepTicks = 120);

} // namespace vsm::sequencer
