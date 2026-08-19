"""
Distance audio, deuxième version : des poids qui veulent enfin dire quelque
chose (étape 10.3 de la feuille de route).

CE QUI N'ALLAIT PAS. La première version additionne six termes avec des poids
explicites -- 0,20 pour le centroïde, 0,25 pour les coefficients cepstraux,
0,15 pour l'enveloppe... -- mais ne normalise pas ces termes entre eux. Mesuré,
en ne faisant varier qu'un réglage à la fois sur une cible connue :

    variation de la résonance :  cepstre 1,714  |  TOUT le reste  0,003
    variation de la coupure   :  cepstre 8,395  |  TOUT le reste  0,017

Autrement dit, la distance EST le terme cepstral, à 0,2 % près. Les cinq autres
termes sont calculés, pondérés, additionnés -- et sans effet. Les poids affichés
décrivent une intention, pas un comportement.

Deux conséquences, et la seconde est grave :

  - l'enveloppe d'amplitude, seule information TEMPORELLE de la mesure, pèse
    0,002 sur environ 5. La distance est donc quasiment aveugle à la forme dans
    le temps : deux sons d'attaques et d'extinctions opposées se ressemblent
    pour elle ;
  - les grandeurs peu dispersées -- au premier rang desquelles la résonance --
    disparaissent sous celles qui le sont beaucoup, et l'optimiseur les traite
    comme du bruit. C'est exactement le défaut relevé au § 1 de la feuille de
    route : « résonance trouvée 1,18 pour 2,2 ».

CE QUE FAIT CETTE VERSION. Rien de plus que rendre les termes comparables :
chaque écart est rapporté à la grandeur correspondante DE LA CIBLE, ce qui en
fait une erreur relative, sans dimension et d'ordre de grandeur un. Les poids
retrouvent alors le sens qu'ils annoncent.

CE QU'ELLE NE FAIT PAS. Elle ne change ni les caractéristiques mesurées, ni leur
pondération relative. C'est délibéré : on corrige un défaut à la fois, et on
mesure. Ajouter des descripteurs en même temps rendrait impossible de dire
lequel des deux changements a produit l'effet observé.
"""

from __future__ import annotations

from typing import Dict

import numpy as np

from .audio_distance import envelope, spectral_features

# Poids des six termes. Identiques à ceux de la première version -- ce sont
# désormais les termes eux-mêmes qui sont comparables, pas les poids qui ont
# changé.
WEIGHTS = {
    "centroid": 0.15,
    "bandwidth": 0.10,
    "rolloff": 0.10,
    "flatness": 0.05,
    "mfcc": 0.20,
    "envelope": 0.20,
    # CONTRASTE SPECTRAL : écart entre les crêtes et les creux à l'intérieur de
    # chaque bande. C'est la seule mesure de la liste qui voie une RÉSONANCE --
    # un pic étroit ne déplace presque pas un centroïde, ne change guère un
    # cepstre moyenné, mais creuse le contraste de sa bande.
    #
    # Sans elle, la résonance pesait 0,20 fois la coupure, dans la première
    # version comme dans la deuxième : rééquilibrer les termes existants n'y
    # changeait rien, parce qu'AUCUN d'eux ne la mesurait.
    "contrast": 0.20,
}


def spectral_contrast(y: np.ndarray, sr: int) -> np.ndarray:
    """Contraste crête/creux par bande, moyenné dans le temps."""
    import librosa

    return np.mean(
        librosa.feature.spectral_contrast(y=np.asarray(y, dtype=np.float32), sr=sr),
        axis=1,
    )


def distance_terms(
    target_features: Dict[str, object],
    candidate_features: Dict[str, object],
    target_envelope: np.ndarray,
    candidate_envelope: np.ndarray,
    target_contrast: np.ndarray,
    candidate_contrast: np.ndarray,
) -> Dict[str, float]:
    """
    Les six termes, chacun sans dimension et d'ordre de grandeur un.

    Exposés séparément parce qu'un chiffre global ne dit pas OÙ un candidat
    s'écarte de la cible -- et que c'est précisément ce qu'on veut pouvoir
    lire dans un rapport de reconstruction.
    """
    def relatif(cle: str, plancher: float) -> float:
        reference = max(abs(float(target_features[cle])), plancher)
        return abs(float(target_features[cle]) - float(candidate_features[cle])) / reference

    cible_mfcc = np.asarray(target_features["mfcc"], dtype=float)
    candidat_mfcc = np.asarray(candidate_features["mfcc"], dtype=float)
    # Rapporté à l'amplitude moyenne des coefficients de la CIBLE : c'est
    # l'échelle naturelle de ce vecteur, qui vaut plusieurs dizaines en
    # décibels là où les autres termes valent des fractions.
    echelle_mfcc = max(float(np.mean(np.abs(cible_mfcc))), 1e-3)

    commun = min(len(target_envelope), len(candidate_envelope))
    if commun > 0:
        # Les enveloppes sont déjà centrées réduites : leur écart moyen est
        # naturellement d'ordre un, il n'y a rien à rapporter.
        erreur_enveloppe = float(np.mean(np.abs(target_envelope[:commun] - candidate_envelope[:commun])))
    else:
        erreur_enveloppe = 1.0

    # Le contraste s'exprime en décibels ; on le rapporte à une échelle fixe de
    # 20 dB, qui est l'ordre de grandeur d'un écart crête/creux marqué.
    erreur_contraste = float(np.mean(np.abs(
        np.asarray(target_contrast, dtype=float) - np.asarray(candidate_contrast, dtype=float)
    ))) / 20.0

    return {
        "centroid": relatif("centroid", 1.0),
        "bandwidth": relatif("bandwidth", 1.0),
        "rolloff": relatif("rolloff", 1.0),
        # La planéité vit DÉJÀ entre 0 et 1 : son écart absolu est la bonne
        # mesure. La rapporter à la valeur de la cible -- souvent de l'ordre de
        # 1e-4 sur un son filtré -- produisait des erreurs relatives de 12,7,
        # qui écrasaient tout le reste.
        "flatness": abs(float(target_features["flatness"]) - float(candidate_features["flatness"])),
        "mfcc": float(np.mean(np.abs(cible_mfcc - candidat_mfcc))) / echelle_mfcc,
        "envelope": erreur_enveloppe,
        "contrast": erreur_contraste,
    }


def combine(terms: Dict[str, float]) -> float:
    return float(sum(WEIGHTS[cle] * valeur for cle, valeur in terms.items()))


def audio_distance_v2(target: np.ndarray, candidate: np.ndarray, sr: int) -> float:
    """Distance de la deuxième version, calculée de bout en bout."""
    longueur = min(len(target), len(candidate))
    if longueur == 0:
        return 1e6
    target = target[:longueur]
    candidate = candidate[:longueur]
    return combine(
        distance_terms(
            spectral_features(target, sr),
            spectral_features(candidate, sr),
            envelope(target),
            envelope(candidate),
            spectral_contrast(target, sr),
            spectral_contrast(candidate, sr),
        )
    )
