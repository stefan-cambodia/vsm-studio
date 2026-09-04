#pragma once
#include <cstdint>
#include <functional>

// D17.6 de docs/ROADMAP-daw.md — DÉTECTER LE SILENCE.
//
// À QUOI CELA SERT ICI, ET PAS SEULEMENT DANS UN DAW EN GÉNÉRAL : un stem
// reconstruit par la chaîne d'analyse commence presque toujours par du rien --
// quatre cents millisecondes de plancher de séparation avant la première
// attaque --, et l'on tirait le bord du clip à l'œil, à la souris, sur une
// forme d'onde où ce plancher est invisible parce qu'il est à -80 dB.
//
// POURQUOI PAS LE CACHE D'APERÇU, qui est déjà là et que « Normaliser » (D13.6)
// utilise justement pour ne pas relire le fichier : ses tranches font 256
// échantillons, soit 5,3 ms à 48 kHz, et cette étape promet la milliseconde.
// Une attaque posée cinq millisecondes trop tôt s'entend -- c'est un clic --,
// et une posée cinq millisecondes trop tard mange le transitoire. On lit donc
// les échantillons.

namespace vsm::audio::io {

/// Ce que la détection a trouvé.
struct SoundBounds {
    /// Première et dernière trame (exclusive) qui dépassent le seuil.
    int64_t firstFrame = 0;
    int64_t lastFrame = 0;
    /// Faux quand TOUT est sous le seuil. L'appelant ne doit alors rien
    /// rogner : un clip entièrement silencieux réduit à zéro tick
    /// disparaîtrait, et personne n'a demandé de le supprimer.
    bool found = false;
};

/// Cherche les bornes de ce qui SONNE dans `[0, frames)`.
///
/// `frameAt` rend un échantillon (les deux canaux) ; c'est un rappel plutôt
/// qu'un pointeur parce que le matériau peut être diffusé depuis le disque et
/// n'existe alors nulle part en un seul bloc.
///
/// `thresholdDb` est un niveau de CRÊTE, en dB pleine échelle (par exemple
/// -60). `preAttackSeconds` est la marge gardée AVANT la première trame
/// trouvée : une attaque n'est jamais un mur, et couper à l'échantillon exact
/// où le seuil est franchi rabote le début de la transitoire.
///
/// `minSilenceSeconds` est le silence minimal qui vaut la peine d'être rogné.
/// En deçà, les bornes rendues sont celles du matériau entier : sans ce
/// garde-fou, la commande grignoterait trois millisecondes à chaque clip
/// qu'on lui donne, et l'on ne saurait jamais si elle a fait quelque chose.
SoundBounds detectSound(const std::function<bool(int64_t, float&, float&)>& frameAt,
                         int64_t frames, double sampleRate, double thresholdDb = -60.0,
                         double preAttackSeconds = 0.005,
                         double minSilenceSeconds = 0.020);

} // namespace vsm::audio::io
