#pragma once
#include "vsm/midi/MidiEvent.h"
#include <functional>
#include <optional>

namespace vsm::app::ui {

/// D286 : LA FENÊTRE DE TEMPS DE L'ARRANGEMENT, reprise par les lanes du bas.
///
/// Les lanes d'automation, de MIDI CC et de tempo étalaient le morceau ENTIER
/// sur leur largeur, quand l'arrangement au-dessus n'en montre qu'une fenêtre :
/// une note vue là-haut à la mesure 40 n'avait aucune verticale commune avec
/// le point qu'on posait en bas. Dans Cubase les lanes SONT dans l'arrangement
/// et l'alignement va de soi ; ici elles sont dans un onglet, et l'alignement
/// se fait en leur donnant la même règle -- ce tick au bord gauche, tant de
/// pixels par tick -- et l'abscisse ÉCRAN où cette règle commence, pour que le
/// même tick tombe sur la même colonne de l'écran quels que soient les marges
/// et la disposition (dock ou panneaux flottants).
///
/// Sans fournisseur (outils d'aperçu, arrangement caché derrière le piano
/// roll), une lane retombe sur son ancienne règle : le morceau entier.
struct FenetreDeTemps {
    vsm::midi::Tick debut = 0;      // le tick au bord gauche de la zone des clips
    double pixelsParTick = 0.06;    // la même que l'arrangement, au bit près
    int xEcranOrigine = 0;          // abscisse ÉCRAN du tick `debut`
    int largeurPixels = 0;          // largeur de la zone des clips, en pixels

    /// L'abscisse, dans un composant dont le bord gauche est à `xEcranComposant`.
    double xLocal(vsm::midi::Tick tick, int xEcranComposant) const {
        return static_cast<double>(xEcranOrigine - xEcranComposant)
             + static_cast<double>(tick - debut) * pixelsParTick;
    }
    vsm::midi::Tick tickDe(int xLocal, int xEcranComposant) const {
        if (pixelsParTick <= 0.0) return debut;
        return debut + static_cast<vsm::midi::Tick>(
            (static_cast<double>(xLocal) - static_cast<double>(xEcranOrigine - xEcranComposant))
            / pixelsParTick);
    }
    /// La zone des clips de l'arrangement recouvre-t-elle, À L'ÉCRAN, la zone
    /// d'édition d'une lane ? Vrai dans le dock et en panneaux flottants empilés ;
    /// faux quand on a écarté les fenêtres à la souris -- la lane suivrait alors
    /// une colonne d'écran qui n'est plus la sienne, et se viderait.
    bool recouvre(int xEcranComposant, int aireX, int aireDroite) const {
        const int origineLocale = xEcranOrigine - xEcranComposant;
        return origineLocale < aireDroite && origineLocale + largeurPixels > aireX;
    }
    /// Sans recouvrement : la MÊME fenêtre de temps, étalée sur la zone de la lane
    /// -- ce que fait un éditeur séparé de Cubase, qui a sa largeur et non celle
    /// du projet.
    double xLocalEtale(vsm::midi::Tick tick, int aireX, int aireLargeur) const {
        const double echelle = largeurPixels > 0 ? static_cast<double>(aireLargeur) / largeurPixels : 0.0;
        return static_cast<double>(aireX) + static_cast<double>(tick - debut) * pixelsParTick * echelle;
    }
    vsm::midi::Tick tickDeEtale(int xLocal, int aireX, int aireLargeur) const {
        const double echelle = largeurPixels > 0 ? static_cast<double>(aireLargeur) / largeurPixels : 0.0;
        if (pixelsParTick * echelle <= 0.0) return debut;
        return debut + static_cast<vsm::midi::Tick>(static_cast<double>(xLocal - aireX) / (pixelsParTick * echelle));
    }
};

using FournisseurDeFenetre = std::function<std::optional<FenetreDeTemps>()>;

} // namespace vsm::app::ui
