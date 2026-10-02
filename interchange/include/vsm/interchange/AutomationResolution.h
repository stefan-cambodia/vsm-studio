#pragma once
#include "vsm/audio/engine/AutomationLane.h"
#include "vsm/sequencer/Project.h"
#include <string>
#include <vector>

// D534 : LA RÉSOLUTION UNIQUE DES COURBES D'AUTOMATION.
//
// Une courbe du projet vise un NOM (`mix.volume`, `insert.1.effect.reverb.mix`,
// `filter.1.cutoff`…, les conventions de `AutomationCurve::parameter`) ; le
// moteur parle en CIBLES (`AutomationTarget`, un insert, un ParamId). Il y avait
// deux traductions : celle de l'application, qui connaissait les six familles de
// mixage, et celle de l'export, qui ne connaissait QUE les réglages de machine —
// si bien qu'un fondu de volume, un panoramique, un départ, un insert ou le
// master automatisés s'entendaient en lecture et disparaissaient à l'export, sans
// que rien ne le dise (mesuré : le stem d'une piste dont le volume descend de 1 à
// 0 sortait égal AU BIT à celui de la même piste sans courbe). C'est la panne de
// D332 : une règle qui fait le son posée dans un seul chemin. Il n'y a donc plus
// qu'une traduction, ici, et les deux chemins l'appellent (contrat de D21).

namespace vsm::interchange {

struct AutomationResolution {
    /// Les courbes que le moteur sait jouer, dans l'ordre du projet.
    std::vector<vsm::audio::engine::AutomationLane> lanes;
    /// UNE PHRASE PAR COURBE NON RÉSOLUE, qui dit laquelle et pourquoi. La courbe
    /// reste dans le projet (D4.6 : la supprimer perdrait le travail à la première
    /// ouverture) ; elle n'est simplement pas jouée — et on le DIT.
    std::vector<std::string> warnings;
};

/// Traduit toutes les courbes du projet. Une courbe sans point ne produit rien et
/// n'est pas une perte. Les valeurs d'un réglage de machine ou d'insert sont
/// BORNÉES à la plage de leur descripteur, pour que l'interpolation reste dans
/// l'espace réel du réglage ; les familles de mixage n'ont pas de descripteur et
/// ne sont pas bornées. La courbure de chaque point (D17.7) est portée.
AutomationResolution resolveAutomation(const vsm::sequencer::Project& project);

} // namespace vsm::interchange
