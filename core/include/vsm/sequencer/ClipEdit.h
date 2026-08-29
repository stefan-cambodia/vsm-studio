#pragma once
#include "vsm/sequencer/Track.h"
#include <cstdint>
#include <functional>
#include <set>
#include <vector>

// Les gestes de la vue d'arrangement -- déplacer, redimensionner, couper --, en
// fonctions PURES sur un vecteur de clips. Aucune dépendance à JUCE.
//
// POURQUOI ICI ET PAS DANS LE COMPOSANT : la même règle que pour `NoteEdit.h`.
// Couper un clip n'est pas du dessin, c'est du montage : dans le composant, ce
// serait intestable (il faudrait un serveur graphique pour vérifier qu'un clip
// coupé en deux rejoue exactement le même son) et inutilisable ailleurs.
//
// CE QUI REND CES OPÉRATIONS PARTICULIÈRES, et ce qu'il faut avoir en tête pour
// les lire : un clip est une FENÊTRE sur le matériau de la piste, pas un
// conteneur qui l'emporte (voir `Clip`). Déplacer un clip ne déplace donc
// aucune note -- il déplace la fenêtre. Et tirer son bord GAUCHE ne le pousse
// pas : cela révèle ou masque du matériau par la tête, en laissant ce qui reste
// exactement là où il était sur la ligne de temps. C'est ce que fait un éditeur
// de régions, et c'est ce qu'on attend quand on rogne le début d'une prise.

namespace vsm::sequencer {

/// La sélection est un ensemble d'IDENTIFIANTS, jamais d'indices : couper un
/// clip en insère un, et une sélection par indice désignerait alors le voisin.
using ClipSelection = std::set<uint64_t>;

/// Durée EFFECTIVEMENT jouée d'un clip. Zéro veut dire « celle de la fenêtre »,
/// et une fenêtre de zéro veut dire « jusqu'au bout du matériau » : les deux
/// conventions se rencontrent ici plutôt que dans chaque appelant.
Tick clipPlayedLength(const Clip& clip, Tick materialEnd);

/// Déplace les clips sélectionnés de `deltaTicks` sur la ligne de temps.
///
/// AUCUN CLIP NE PASSE AVANT ZÉRO, et le décalage est alors réduit POUR TOUS :
/// une sélection qui se déformerait parce qu'un de ses clips bute sur le début
/// du morceau ne serait plus la figure qu'on a saisie.
void moveClips(std::vector<Clip>& clips, const ClipSelection& selection, Tick deltaTicks);

/// Tire le bord DROIT : change ce qui est joué, donc la fenêtre.
///
/// LA BOUCLE VIENT DU MÊME GESTE (D5.2), et c'est ce que veut dire « boucle de
/// clip par ÉTIREMENT ». Tant qu'il reste du matériau, tirer le bord droit en
/// révèle davantage ; une fois au bout, la fenêtre ne peut plus grandir et
/// c'est la durée JOUÉE qui continue -- le clip répète alors sa fenêtre, sans
/// qu'une seule note soit copiée. Un modificateur ou un second outil pour
/// « boucler » demanderait de savoir à l'avance si l'on est au bout du
/// matériau, ce que personne ne sait en tirant.
///
/// La longueur ne descend jamais sous un tick -- un clip de durée nulle serait
/// invisible et injouable, exactement comme une note de durée nulle.
void resizeClipsEnd(std::vector<Clip>& clips, const ClipSelection& selection,
                     Tick deltaTicks, Tick materialEnd);

/// Duplique les clips sélectionnés, décalés de `offsetTicks`, et rend la
/// sélection des COPIES -- pour que le geste suivant porte sur ce qu'on vient
/// de créer, comme dans le piano roll.
ClipSelection duplicateClips(std::vector<Clip>& clips, const ClipSelection& selection,
                              Tick offsetTicks, uint64_t& idCounter);

/// Les bornes d'une sélection sur la ligne de temps, pour savoir de combien
/// décaler une duplication. Rend faux si la sélection est vide.
bool clipSelectionBounds(const std::vector<Clip>& clips, const ClipSelection& selection,
                          Tick materialEnd, Tick& startTick, Tick& endTick);

/// Tire le bord GAUCHE : révèle ou masque du matériau par la tête.
///
/// `ticksToSeconds` sert aux clips AUDIO, dont la fenêtre dans le fichier est
/// en secondes et non en ticks (voir `Clip::sourceStartSeconds`) : sans elle,
/// rogner le début d'une prise décalerait le son au lieu de le rogner. Passée
/// par l'appelant pour la même raison que dans `spansFromTrack` -- la carte de
/// tempo appartient au projet.
void resizeClipsStart(std::vector<Clip>& clips, const ClipSelection& selection,
                       Tick deltaTicks, Tick materialEnd,
                       const std::function<double(Tick)>& ticksToSeconds);

/// Coupe en deux les clips sélectionnés qui traversent `atTick`.
///
/// Les deux moitiés rejouent EXACTEMENT ce que jouait l'original : la seconde
/// reprend la fenêtre là où la première l'a laissée. Rend le nombre de coupes
/// réellement faites -- zéro quand le point tombe en dehors, sur un bord, ou
/// sur aucun clip sélectionné.
size_t splitClips(std::vector<Clip>& clips, const ClipSelection& selection, Tick atTick,
                   Tick materialEnd, uint64_t& idCounter,
                   const std::function<double(Tick)>& ticksToSeconds);

} // namespace vsm::sequencer
