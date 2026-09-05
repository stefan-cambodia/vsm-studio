#pragma once
#include <cstdint>
#include <functional>

// D21.3 de docs/ROADMAP-daw.md — COUPER AU PASSAGE PAR ZÉRO.
//
// Une coupe audio posée sur un ventre de forme d'onde fait un clic : le
// signal saute de sa valeur à zéro en un échantillon. Cubase aimante ses
// coupes au passage par zéro (Snap to Zero Crossing) ; Live pose des fondus
// automatiques. Ici, chaque coupe d'un clip audio -- à la tête de lecture, ou
// aux transitoires de D20.3 -- se déplace au passage par zéro le PLUS PROCHE
// dans une fenêtre de ±2 ms : assez pour trouver un zéro sur tout ce qui
// descend jusqu'à 250 Hz, assez peu pour ne pas déplacer une coupe posée sur
// une attaque au-delà de ce qu'une oreille entend.
//
// « Passage par zéro » : l'échantillon où le signal (la moyenne des deux
// canaux) change de signe, ou vaut zéro. Sans passage dans la fenêtre -- un
// grave tenu sous 250 Hz, une composante continue --, l'instant demandé est
// rendu tel quel, et l'appelant le dit.

namespace vsm::audio::io {

/// L'échantillon du passage par zéro le plus proche de `frame` dans
/// `[frame - windowFrames, frame + windowFrames]`, ou `frame` s'il n'y en a
/// aucun. `frameAt` rend les deux canaux ; faux hors du matériau.
int64_t nearestZeroCrossing(const std::function<bool(int64_t, float&, float&)>& frameAt,
                            int64_t frame, int64_t windowFrames);

} // namespace vsm::audio::io
