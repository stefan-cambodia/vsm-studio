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

// ---------------------------------------------------------------------------
// LE VERROU (D16.5), ET OÙ PASSE LA FRONTIÈRE.
//
// Les fonctions qui prennent un `std::vector<Clip>&` sont la GÉOMÉTRIE pure :
// elles ne savent pas à quelle piste appartiennent les clips, et ne peuvent
// donc pas savoir si elle est verrouillée. Ce sont elles que les tests
// exercent, et elles ne vérifient rien.
//
// Les surcharges qui prennent un `Track&` sont celles qu'un ÉDITEUR appelle :
// elles refusent tout sur une piste verrouillée, en un seul endroit par geste,
// et rendent ce qu'elles ont fait (zéro quand elles ont refusé). C'est là que
// vit le cadenas -- pas dans les quarante gestes des deux composants, où le
// quarante-et-unième l'oublierait.
//
// Une sélection à cheval sur plusieurs pistes ne perd donc que les pistes
// verrouillées : les autres bougent, et l'appelant compte les refus pour les
// dire.
// ---------------------------------------------------------------------------

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

/// CRÉER UN CLIP (D16.1) : la fenêtre IDENTITÉ sur le matériau déjà là,
/// posée de `startTick` à `startTick + length`.
///
/// Jusqu'ici un clip ne naissait qu'à l'ouverture d'un projet (la
/// matérialisation de la fenêtre implicite) ou d'un clip existant (couper,
/// dupliquer) : sur une piste neuve, des notes écrites au piano roll ne
/// produisaient AUCUN clip visible tant qu'on n'avait pas sauvegardé et
/// rouvert. C'est le geste qui manquait.
///
/// La fenêtre est l'IDENTITÉ -- `sourceStart == startTick`, même longueur --
/// pour la même raison que la matérialisation à l'ouverture : ce qui sonnait
/// à la mesure 3 continue de sonner à la mesure 3. Un clip créé qui montrerait
/// le début du matériau déplacerait le morceau à sa naissance.
///
/// LA RÈGLE DU CHEVAUCHEMENT, et celle qui a été écartée. Deux clips d'une
/// même piste dont les fenêtres se recouvrent lisent DEUX FOIS le même
/// matériau : le passage se joue en double, alors qu'aucune note n'est en
/// double. Déplacer ou dupliquer laissent cela possible, et c'est assumé --
/// on VOIT les deux clips qu'on empile. Créer, non : le geste vise ce qui a
/// l'air d'être du vide, et une création qui recouvre en silence serait une
/// panne muette. Donc le nouveau clip s'arrête au clip suivant (`truncated`
/// le dit), et si son début est DÉJÀ pris, rien n'est créé -- un refus que
/// l'appelant doit dire, jamais une création discrète ailleurs.
struct ClipCreation {
    uint64_t id = 0;          ///< 0 = refusé : le début est déjà couvert.
    Tick startTick = 0;
    Tick length = 0;          ///< la longueur RÉELLEMENT obtenue.
    bool truncated = false;   ///< raccourcie par le clip suivant.
};
ClipCreation createClip(std::vector<Clip>& clips, Tick startTick, Tick length,
                        uint64_t& idCounter, Tick materialEnd);

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
                                    int deltaTracks, bool automationFollows = false,
                                    Tick materialEnd = 0);

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

/// À L'ENVERS OU À L'ENDROIT (D13.4) : bascule le sens de lecture de chaque
/// clip de la sélection -- chacun le sien, comme l'inversion de phase.
void toggleClipReverse(std::vector<Clip>& clips, const ClipSelection& selection);

/// ÉTIRER PAR LE BORD DROIT (D13.2) : la durée jouée change de `deltaTicks`
/// et le MATÉRIAU suit — les marqueurs glissent en proportion, le dernier
/// suit le bord. Un clip qui ne suivait pas le tempo reçoit d'abord sa paire
/// neutre et passe en « hauteur conservée » : c'est le geste de Live (Alt +
/// bord) et de Cubase (« le redimensionnement étire »). Ne descend jamais
/// sous un tick. Rend vrai si quelque chose a bougé.
bool stretchClipsEnd(std::vector<Clip>& clips, const ClipSelection& selection, Tick deltaTicks,
                     Tick materialEnd, const std::function<double(Tick)>& ticksToSeconds);

/// JOINDRE DES CLIPS (D16.3) : la Colle de Cubase, le Consolidate de Live.
///
/// L'INVERSE EXACT DE `splitClips`, et c'est la règle qui définit ce qui se
/// joint. Deux clips fusionnent quand le second est précisément ce qu'une
/// coupe aurait produit du premier :
///
///  1. ils se touchent sur la ligne de temps (la fin jouée de l'un est le
///     début de l'autre) ;
///  2. leur FENÊTRE se prolonge (la fin de la fenêtre de l'un est le début de
///     celle de l'autre), sans quoi le clip joint jouerait autre chose que les
///     deux clips séparés -- et le seul critère qui vaille ici est que le son
///     ne change pas d'une note ;
///  3. aucun des deux ne BOUCLE (durée jouée = fenêtre) : joindre deux boucles
///     donnerait une fenêtre qui n'est plus celle qu'on répétait ;
///  4. ils ont les mêmes réglages de montage (gain, phase, sens, muet) -- deux
///     clips réglés différemment ne peuvent pas devenir un clip à un réglage
///     sans qu'on perde silencieusement l'un des deux ;
///  5. aucun ne SUIT LE TEMPO. Deux cartes de warp mises bout à bout ne font
///     pas une carte : le prolongement des rapports aux bords se croiserait.
///     Refusé plutôt que joint de travers.
///  6. pour un clip AUDIO, la fenêtre dans le FICHIER se prolonge aussi, en
///     secondes -- c'est la même exigence que 2, dans l'unité du matériau.
///
/// `audioTrack` DIT LEQUEL DES DEUX MATÉRIAUX ON JOINT, et il est explicite
/// plutôt que deviné : un clip est une fenêtre, il ne sait pas s'il montre des
/// notes ou un fichier -- c'est sa PISTE qui le sait (voir `Track::kind`). Le
/// déduire de `sourceStartSeconds` marcherait presque, et « presque » veut dire
/// qu'une paire de clips MIDI se ferait refuser sur un critère qui ne la
/// concerne pas.
///
/// Le clip joint garde le fondu d'ENTRÉE du premier et celui de SORTIE du
/// dernier : ce sont les deux bords qui restent des bords.
///
/// Ce qui ne peut pas se joindre est COMPTÉ et rendu à l'appelant, pour qu'il
/// le dise. Rien n'est modifié à moitié : une paire refusée laisse ses deux
/// clips exactement où ils étaient.
struct ClipJoin {
    size_t joined = 0;    ///< nombre de clips ABSORBÉS (deux clips joints = 1).
    size_t refused = 0;   ///< paires voisines de la sélection qui n'ont pas pu.
};
ClipJoin joinClips(std::vector<Clip>& clips, const ClipSelection& selection, Tick materialEnd,
                    bool audioTrack, const std::function<double(Tick)>& ticksToSeconds);

size_t splitClips(std::vector<Clip>& clips, const ClipSelection& selection, Tick atTick,
                   Tick materialEnd, uint64_t& idCounter,
                   const std::function<double(Tick)>& ticksToSeconds);

/// LES SURCHARGES VERROUILLABLES (D16.5). Chacune rend le nombre de clips
/// qu'elle a touchés -- zéro quand la piste est verrouillée, et alors PAS UN
/// TICK n'a bougé.
/// `automationFollows` (D17.2) : les courbes de la piste suivent les clips
/// déplacés — c'est « l'automation suit les événements » de Cubase, actif par
/// défaut chez lui. Passé jusqu'ici plutôt que lu quelque part : le réglage est
/// une PRÉFÉRENCE de l'application, et `core/` n'en connaît aucune. Le mettre
/// dans le paramètre plutôt qu'à côté de l'appel garantit qu'un appelant ne
/// peut pas déplacer un clip en oubliant sa courbe — c'est le même
/// raisonnement que pour le verrou.
size_t moveClips(Track& track, const ClipSelection& selection, Tick deltaTicks,
                  bool automationFollows, Tick materialEnd);
size_t resizeClipsEnd(Track& track, const ClipSelection& selection, Tick deltaTicks,
                       Tick materialEnd);
size_t resizeClipsStart(Track& track, const ClipSelection& selection, Tick deltaTicks,
                         Tick materialEnd, const std::function<double(Tick)>& ticksToSeconds);
size_t stretchClipsEnd(Track& track, const ClipSelection& selection, Tick deltaTicks,
                        Tick materialEnd, const std::function<double(Tick)>& ticksToSeconds);
size_t splitClips(Track& track, const ClipSelection& selection, Tick atTick, Tick materialEnd,
                   uint64_t& idCounter, const std::function<double(Tick)>& ticksToSeconds);
ClipSelection duplicateClips(Track& track, const ClipSelection& selection, Tick offsetTicks,
                              uint64_t& idCounter);
ClipCreation createClip(Track& track, Tick startTick, Tick length, uint64_t& idCounter,
                         Tick materialEnd);
ClipJoin joinClips(Track& track, const ClipSelection& selection, Tick materialEnd,
                    const std::function<double(Tick)>& ticksToSeconds);

/// Combien de clips de la sélection appartiennent à une piste verrouillée :
/// ce que l'appelant doit DIRE quand un geste n'a pas tout fait.
/// ÉLARGIR UNE SÉLECTION AUX GROUPES D'ÉDITION (D18.3).
///
/// Pour chaque clip choisi appartenant à une piste qui a un `editGroup`, les
/// clips des AUTRES pistes du même groupe qui couvrent les mêmes ticks entrent
/// dans la sélection. Les gestes de montage — couper, déplacer, joindre,
/// redimensionner — héritent alors du groupe SANS UNE LIGNE DE PLUS : ils
/// travaillent déjà sur une sélection, et c'est la sélection qui a grandi.
///
/// C'EST POURQUOI IL N'Y A QU'UNE FONCTION ET PAS SIX. Écrire « et fais la
/// même chose sur les pistes du groupe » dans chacun des six gestes
/// garantirait que le septième l'oublie — c'est le raisonnement du verrou
/// (D16.5), appliqué là où il marche encore mieux.
///
/// « Couvrir les mêmes ticks » et non « avoir le même début » : deux micros
/// d'une même batterie sont découpés pareil, mais un clip peut avoir été
/// rogné. Le recouvrement est le critère qui décrit ce qu'on veut couper
/// ensemble.
ClipSelection expandSelectionToEditGroups(const std::vector<Track>& tracks,
                                           const ClipSelection& selection, Tick materialEnd);

size_t lockedClipsInSelection(const std::vector<Track>& tracks, const ClipSelection& selection);


// ---------------------------------------------------------------------------
// RÉPÉTER (D20.1). Poser un motif d'une mesure sur seize demandait seize
// « dupliquer » ; Cubase a « Repeat… », Live répète Ctrl+D. Les copies se
// posent À LA SUITE l'une de l'autre : la répétition k est décalée de
// k × spanTicks, où spanTicks est la longueur de la sélection (arrondie par
// l'appelant à la mesure ou à la grille, comme pour dupliquer). Les copies
// reçoivent des identifiants neufs et sont rendues, pour que le geste suivant
// porte sur elles. Rien pour un nombre nul ou une sélection vide.
// ---------------------------------------------------------------------------
ClipSelection repeatClips(std::vector<Clip>& clips, const ClipSelection& selection, int count,
                          Tick spanTicks, uint64_t& idCounter);
/// Sur une piste : le verrou refuse tout, comme pour les autres gestes.
ClipSelection repeatClips(Track& track, const ClipSelection& selection, int count,
                          Tick spanTicks, uint64_t& idCounter);
/// COMBIEN DE RÉPÉTITIONS TIENNENT avant `untilTick` (la fin de la boucle) :
/// autant que de copies dont la FIN ne le dépasse pas. Zéro quand la
/// première n'y tient pas -- « jusqu'à la fin de la boucle » ne déborde jamais
/// de la boucle, sinon la seizième mesure sonnerait après le rebouclage.
inline int repeatsThatFit(Tick selectionEnd, Tick spanTicks, Tick untilTick) {
    if (spanTicks <= 0 || untilTick <= selectionEnd) return 0;
    return static_cast<int>((untilTick - selectionEnd) / spanTicks);
}

} // namespace vsm::sequencer
