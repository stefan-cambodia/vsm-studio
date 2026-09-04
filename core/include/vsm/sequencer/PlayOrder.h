#pragma once
#include "vsm/sequencer/Project.h"
#include <string>
#include <vector>

// D18.4 de docs/ROADMAP-daw.md — L'ORDRE DE JEU (la piste d'Arrangement de
// Cubase).
//
// LE PROBLÈME : les repères NOMMENT des endroits depuis D16.4, mais rien ne
// permet de rejouer « couplet, couplet, refrain » sans tout recopier à la
// main. Or c'est le geste par lequel on essaie une structure -- et l'essayer
// est tout l'intérêt, puisqu'on ne sait pas d'avance laquelle est la bonne.
//
// LA RÉDUCTION QUI REND CETTE ÉTAPE PETITE : une section N'EST PAS UN OBJET DE
// PLUS. Elle se DÉDUIT des repères -- de celui-ci jusqu'au suivant --, parce
// que c'est déjà ainsi qu'on s'en sert : on pose « Refrain » au début du
// refrain, et la section refrain va de là au repère d'après. Ajouter un
// second modèle de « zone nommée » à côté des repères aurait donné deux
// vérités sur la même chose, et c'est toujours la seconde qui ment. Seul
// l'ORDRE est un objet nouveau, et il tient dans une liste d'entiers.

namespace vsm::sequencer {

/// Une section : un repère, et ce qui le sépare du suivant.
struct Section {
    std::string name;
    Tick startTick = 0;
    Tick endTick = 0;      ///< exclu
    Tick length() const { return endTick - startTick; }
};

/// LES SECTIONS DÉDUITES DES REPÈRES, dans l'ordre du morceau.
///
/// La dernière va du dernier repère à la fin du matériau. Un repère posé
/// APRÈS tout le matériau ne produit pas de section : elle serait vide, et une
/// section vide dans un ordre de jeu ne se voit qu'à ce qu'elle ne fait rien.
/// Sans repère, aucune section -- le morceau entier n'en est pas une, sinon
/// « aplatir » sur un projet sans repère se contenterait de le recopier en
/// donnant l'impression d'avoir travaillé.
std::vector<Section> sectionsFromMarkers(const Project& project);

/// APLATIR : réécrit le projet pour qu'il JOUE l'ordre demandé.
///
/// `order` contient des indices de section, répétables. Le résultat est un
/// projet ordinaire -- des notes, des clips et des courbes à leur place --,
/// et c'est le SEUL moment où l'ordre de jeu touche au matériau : tant qu'on
/// n'aplatit pas, on n'a rien cassé et l'on peut essayer autre chose.
///
/// CE QUI EST TRANSPORTÉ : les notes, les clips, les courbes d'automation et
/// les repères. Une note qui déborderait de sa section est COUPÉE à sa fin --
/// laissée entière, elle empiéterait sur la section suivante, que personne
/// n'a arrangée ainsi ; c'est la règle qu'applique déjà `splitClips` au bord
/// d'un clip.
///
/// CE QUI NE L'EST PAS, ET C'EST DIT : la carte de TEMPO et celle des
/// SIGNATURES. Elles décrivent la ligne de temps, pas les sections ; les
/// réordonner demanderait de décider ce que devient un ralenti joué deux fois,
/// et la réponse n'est pas la même selon qu'on répète un refrain ou qu'on
/// déplace une coda. Tant que le morceau garde un tempo constant -- le cas de
/// presque toutes les reconstructions -- cela ne change rien ; sinon,
/// l'appelant est prévenu par `flattenChangesTempoMeaning`.
///
/// Rend faux si l'ordre est vide ou ne désigne aucune section valide : dans ce
/// cas le projet n'est pas touché du tout.
bool flattenPlayOrder(Project& project, const std::vector<int>& order);

/// Vrai si le projet a plus d'un tempo ou plus d'une signature : aplatir
/// laissera alors la carte en place alors que le matériau, lui, aura bougé.
/// À DIRE avant d'aplatir, jamais après.
bool flattenChangesTempoMeaning(const Project& project);

} // namespace vsm::sequencer
