#pragma once
#include "vsm/sequencer/NoteEdit.h"
#include "vsm/sequencer/Track.h"
#include <string>
#include <vector>

// D17.8 de docs/ROADMAP-daw.md — LE GROOVE : l'extraire d'une piste, le donner
// à une autre.
//
// CE QUE LA QUANTIFICATION NE SAIT PAS FAIRE. `Quantizer` connaît la grille et
// le swing : il RAPPROCHE d'un idéal calculé. Un groove fait l'inverse — il
// prend le placement RÉEL d'une partie qu'on trouve bonne et l'impose à une
// autre. Sur ce projet, c'est le geste qui manquait le plus : la chaîne
// d'analyse reconstruit une batterie avec son placement d'origine, et il n'y
// avait aucun moyen de donner ce placement à une basse écrite droite.
//
// LE GROOVE EST UNE SUITE D'ÉCARTS, PAS UNE SUITE DE POSITIONS. Un écart est
// relatif à un pas de grille, en FRACTION de pas : le même groove s'applique
// donc à n'importe quel tempo et à n'importe quelle résolution de projet, ce
// qu'une suite de ticks ne permettrait pas.

namespace vsm::sequencer {

/// Un pas du groove.
struct GrooveStep {
    /// L'écart au pas, en fraction de pas. -0,5 est un demi-pas en avance,
    /// +0,5 un demi-pas en retard.
    double offset = 0.0;
    /// La vélocité MOYENNE observée sur ce pas, de 0 à 1. Un groove porte le
    /// placement ET l'accentuation : c'est ce qui fait la différence entre
    /// « en retard » et « qui balance ».
    float velocity = 0.0f;
    /// Faux quand la partie d'origine n'avait RIEN sur ce pas. Un pas muet ne
    /// dit rien du placement, et prétendre le contraire ferait tirer vers zéro
    /// les notes qui tombent dessus.
    bool present = false;
};

/// UN GROOVE : le placement d'une partie, décollé de ses notes.
struct Groove {
    std::string name;
    /// Le nombre de pas par mesure sur lequel les écarts ont été mesurés.
    /// Seize par défaut : la double croche, la grille sur laquelle se joue
    /// presque tout ce qui balance.
    int stepsPerBar = 16;
    std::vector<GrooveStep> steps;

    bool empty() const { return steps.empty(); }
};

/// EXTRAIRE le groove de notes déjà jouées.
///
/// Chaque note est rattachée au pas dont elle est la plus PROCHE, et non à
/// celui qui la précède : une note jouée deux millisecondes en avance
/// appartient au pas qu'elle anticipe, pas au précédent. Les écarts d'un même
/// pas sont MOYENNÉS -- deux notes sur le même temps (une grosse caisse et un
/// charley) décrivent le même instant musical.
///
/// Les écarts au-delà d'un demi-pas sont ignorés : au-delà, la note n'est plus
/// « en avance » sur son pas, elle est sur un autre.
Groove extractGroove(const std::vector<Note>& notes, Tick ticksPerBar, int stepsPerBar,
                      const std::string& name = {});

/// APPLIQUER un groove à une sélection.
///
/// `strength` va de 0 à 1 : à un, la note prend exactement l'écart du groove ;
/// à un demi, la moitié du chemin depuis sa position actuelle. C'est ce qui
/// permet de « teinter » une partie sans la déplacer entièrement -- et c'est
/// pourquoi la force est un paramètre de l'APPLICATION et non du groove : le
/// même groove sert à cent pour cent sur une basse et à trente sur un piano.
///
/// `applyVelocity` fait aussi suivre l'accentuation. Séparé, parce qu'on veut
/// souvent le placement sans toucher aux nuances qu'on a écrites.
///
/// Les notes qui tombent sur un pas ABSENT du groove ne bougent pas. Rend le
/// nombre de notes déplacées.
size_t applyGroove(std::vector<Note>& notes, const NoteSelection& selection,
                    const Groove& groove, Tick ticksPerBar, float strength = 1.0f,
                    bool applyVelocity = false);

} // namespace vsm::sequencer
