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

/// Tire le fondu d'ENTRÉE d'un clip jusqu'à `atTick` (D5.6).
///
/// Le fondu est en SECONDES dans le modèle -- comme la fenêtre d'un clip audio,
/// et pour la même raison : un fondu suit le son, pas le tempo. Accélérer un
/// morceau ne doit pas raccourcir ses fondus. D'où la conversion, passée par
/// l'appelant comme partout ailleurs.
///
/// Le fondu ne dépasse jamais le clip : au-delà, il mangerait ce qui vient
/// après et ne s'entendrait plus comme un fondu.
void setClipFadeIn(std::vector<Clip>& clips, uint64_t clipId, Tick atTick, Tick materialEnd,
                    const std::function<double(Tick)>& ticksToSeconds);

/// Tire le fondu de SORTIE depuis `atTick` jusqu'à la fin du clip.
void setClipFadeOut(std::vector<Clip>& clips, uint64_t clipId, Tick atTick, Tick materialEnd,
                     const std::function<double(Tick)>& ticksToSeconds);

/// Règle le gain des clips sélectionnés, en gain LINÉAIRE. Jamais négatif : une
/// inversion de phase est un réglage à part (`Clip::invertPhase`), et la
/// confondre avec un gain négatif rendrait le bouton illisible.
void setClipGain(std::vector<Clip>& clips, const ClipSelection& selection, float gain);

/// Inverse la phase des clips sélectionnés (bascule).
void toggleClipPhase(std::vector<Clip>& clips, const ClipSelection& selection);

/// Les bornes d'une sélection sur la ligne de temps, pour savoir de combien
/// décaler une duplication. Rend faux si la sélection est vide.
/// D11.1 — LE CLIP CHANGE DE PISTE, ET IL EMPORTE CE QUE SA FENÊTRE COUVRE.
///
/// Un clip est une fenêtre sur le matériau de SA piste (voir Track.h) : le
/// poser sur une autre piste n'a de sens que si les notes qu'il montre le
/// suivent. Elles quittent donc la piste d'origine et entrent dans la piste
/// cible AUX MÊMES TICKS DE MATÉRIAU — le clip y montre exactement ce qu'il
/// montrait. Le prix du modèle de la région, dit ici : un autre clip de la
/// piste cible dont la fenêtre couvre ces ticks les verra aussi, comme il
/// verrait des notes qu'on y aurait enregistrées ; et un autre clip de la
/// piste d'origine sur le même matériau ne les voit plus.
///
/// Un clip AUDIO ne change de piste que vers une piste audio qui porte le
/// même fichier — ou aucun, et elle l'adopte alors ; le reste est REFUSÉ et
/// compté, jamais silencieux. Une piste de groupe ne reçoit rien. Le décalage
/// de pistes est réduit pour TOUS quand l'un des clips sortirait de la liste,
/// comme `moveClips` le fait pour le temps : la figure saisie garde sa forme.
struct ClipTrackMove {
    size_t moved = 0;
    size_t refused = 0;
    /// Le décalage réellement appliqué, après réduction aux bords.
    int applied = 0;
};
ClipTrackMove moveClipsAcrossTracks(std::vector<Track>& tracks, const ClipSelection& selection,
                                    int deltaTracks);

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
// ---------------------------------------------------------------------------
// LE SUIVI DE TEMPO (D12.4, `docs/CDC-etirement-temporel.md` § 2 et § 4).
// Les marqueurs sont RELATIFS au début du clip : déplacer le clip ne les
// touche pas ; le rogner, le couper, les transportent (testé).
// ---------------------------------------------------------------------------

/// Un clip suit-il le tempo ? Il faut le mode ET deux marqueurs au moins.
bool clipIsWarped(const Clip& clip);

/// La position dans le FICHIER, en secondes, pour un tick relatif au début
/// du clip, d'après ses marqueurs (linéaire entre deux, prolongé au-delà).
/// Sans marqueurs suffisants, rend `sourceStartSeconds`.
double warpSourceSecondsAt(const Clip& clip, Tick relativeTick);

/// L'inverse : le tick relatif où une position du fichier tombe.
Tick warpTickAtSeconds(const Clip& clip, double sourceSeconds);

/// Allume ou éteint le suivi de tempo. L'allumer sur un clip sans marqueurs
/// pose la PAIRE NEUTRE (début et fin, au rapport un) : rien ne change au
/// son tant qu'on ne bouge rien — et le rapport un est un court-circuit au
/// bit près dans le moteur. `ticksToSeconds` sert à mesurer la durée jouée.
void setClipWarpMode(std::vector<Clip>& clips, const ClipSelection& selection, WarpMode mode,
                     Tick materialEnd, const std::function<double(Tick)>& ticksToSeconds);

/// « LE CLIP FAIT N MESURES » : la première commande du § 6. Le matériau
/// actuellement joué (en secondes) est réparti sur `bars × ticksPerBar`
/// ticks ; le clip prend cette longueur, ses marqueurs deviennent la paire
/// début/fin, et le mode s'allume (KeepPitch) s'il était éteint. Rend le
/// tempo d'origine déduit, en BPM, pour l'afficher — ou 0 si rien n'a changé.
double setClipBars(std::vector<Clip>& clips, uint64_t clipId, int bars, Tick ticksPerBar,
                   Tick materialEnd, const std::function<double(Tick)>& ticksToSeconds);

/// Ajoute un marqueur à `relativeTick` (strictement entre le premier et le
/// dernier), à la position du fichier que la carte y met déjà : le son ne
/// change pas, mais le point peut maintenant se déplacer. Rend son indice, ou
/// -1.
int addWarpMarker(std::vector<Clip>& clips, uint64_t clipId, Tick relativeTick);

/// Déplace le marqueur `index` en musique (sa position dans le fichier ne
/// bouge pas) : c'est le geste de calage. Le premier marqueur reste à 0 ;
/// les autres restent strictement entre leurs voisins.
bool moveWarpMarker(std::vector<Clip>& clips, uint64_t clipId, size_t index, Tick relativeTick);

/// Retire le marqueur `index` — jamais le premier, jamais sous deux.
bool removeWarpMarker(std::vector<Clip>& clips, uint64_t clipId, size_t index);

size_t splitClips(std::vector<Clip>& clips, const ClipSelection& selection, Tick atTick,
                   Tick materialEnd, uint64_t& idCounter,
                   const std::function<double(Tick)>& ticksToSeconds);

} // namespace vsm::sequencer
