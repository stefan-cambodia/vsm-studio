"""
Distance audio, quatrième version : la troisième, plus un terme de DYNAMIQUE.

CE QUI N'ALLAIT PAS, ET LA MESURE QUI L'A DIT. Sur la batterie de *Sky and
Sand*, le réglage de piste a retenu `drum.kick.decay = 1,246 s` -- pour un kick
toutes les 330 ms, chaque frappe recouvre les trois suivantes -- et
`drum.snare.level = 0`, c'est-à-dire une caisse claire muette. Ce n'était pas un
accident de la descente par coordonnées : rendus à la main, TOUS les patchs plus
courts sont moins bons au sens de v2 (0,2117 à 1,246 s contre 0,3667 à 0,150 s),
et rallumer la caisse claire aussi. La métrique récompensait un bourdon continu.

Or le stem réel est au-dessus du dixième de sa crête 52 % du temps, la
reconstruction 100 % ; la crête du rendu plafonne à 0,26 quand le réel monte à
0,587. Une batterie qui ne se tait jamais n'est pas une batterie. Détail dans
ROADMAP-fusion.md § 5 octies.

POURQUOI v2 NE POUVAIT PAS LE VOIR, alors qu'elle a un terme « envelope ». Ce
terme compare des enveloppes NORMALISÉES (`normalize(rms)`, centrées réduites) :
la normalisation détruit précisément ce qui distingue une suite de frappes d'un
bourdon de même énergie moyenne, à savoir le rapport entre les crêtes et le
niveau courant. v2 compare la FORME de l'enveloppe et jamais son RELIEF.

CE QUE FAIT CETTE VERSION. Un terme de plus, et un seul : le FACTEUR DE CRÊTE de
l'enveloppe -- son maximum rapporté à sa valeur efficace --, comparé en rapport
(c'est-à-dire en octaves) entre cible et candidat, exactement comme v3 compare
les hauteurs. Un facteur de crête faux d'une octave vaut 1. Mesuré sur la
batterie de *Sky and Sand* (40 s du passage dense) : stem réel 4,10 ; le patch
que v2 retient 1,44 (1,51 octave d'écart) ; le même à 0,150 s 2,39 (0,78). Le
terme sépare les candidats de façon monotone, et dans le sens que l'enveloppe
et l'oreille désignent.

SUR L'ENVELOPPE BRUTE, ET C'EST TOUT LE POINT. La première écriture de ce terme
réutilisait `audio_distance.envelope`, qui rend une enveloppe CENTRÉE RÉDUITE.
Or retirer la moyenne détruit exactement ce qu'on veut mesurer : sur un bourdon,
dont l'enveloppe est presque plate, il ne reste qu'une ondulation minuscule que
la division par l'écart-type ramène à l'unité -- et le « facteur de crête »
obtenu vaut alors 3,9 contre 2,5 pour de vraies frappes, c'est-à-dire l'inverse
de la vérité. C'est un test de laboratoire qui l'a attrapé. Ce module calcule
donc sa PROPRE enveloppe, en valeur efficace non normalisée ; le rapport
crête/efficace y est naturellement insensible au volume, puisque numérateur et
dénominateur suivent la même échelle.

CE QU'ELLE NE FAIT PAS. Elle ne touche ni aux sept termes de v2, ni au terme de
hauteur de v3, ni à leurs poids. Un défaut à la fois, et on mesure -- la règle
de v2 et de v3, reprise telle quelle. Elle ne pousse pas non plus vers le plus
court possible : sur le balayage d'extinctions ci-dessus, l'optimum de v4 tombe
à 0,300 s, pas à 0,150 -- le terme de dynamique tire dans un sens, les sept
termes spectraux dans l'autre, et le compromis est ce qu'on cherchait.

CE QU'ELLE COÛTE, ET IL FAUT LE SAVOIR AVANT DE L'ACTIVER. Elle change les
verdicts partout où la dynamique compte, donc sur toutes les batteries. La règle
du § 10.3 vaut sans exception : deux métriques ne se comparent pas, une distance
v4 ne se compare qu'à une distance v4, et TOUTES les distances publiées du
projet sont en v2. La chaîne reste donc en v2 par défaut ; `--metrique v4`
l'active.
"""

from __future__ import annotations

from typing import Dict

import numpy as np

import librosa

from .audio_distance import envelope, spectral_features
from .audio_distance_v2 import spectral_contrast
from .audio_distance_v3 import WEIGHTS as WEIGHTS_V3
from .audio_distance_v3 import distance_terms as distance_terms_v3
from .audio_distance_v3 import low_pitch

# Le terme de dynamique pèse autant que l'enveloppe, le contraste ou la
# hauteur : confondre une suite de frappes avec un bourdon est une faute du
# même rang qu'une hauteur fausse d'une octave, pas un détail de finition.
# Les huit autres poids sont ceux de v3, inchangés.
WEIGHTS: Dict[str, float] = {**WEIGHTS_V3, "dynamics": 0.20}

# En deçà de ce nombre de trames d'enveloppe (512 échantillons de saut, soit
# ~12 ms à 44,1 kHz), un facteur de crête ne veut rien dire : sur une note
# isolée d'une seconde il y a de quoi mesurer, sur un extrait de 50 ms non.
TRAMES_MINIMALES = 16


def raw_envelope(y: np.ndarray) -> np.ndarray:
    """L'enveloppe en valeur efficace, NON normalisée.

    `audio_distance.envelope` centre et réduit la sienne, ce qui convient au
    terme de FORME de v2 et ruine toute mesure de relief : voir l'en-tête.
    Mêmes fenêtre et même saut, pour que les deux décrivent le même découpage
    du temps.
    """
    y = np.asarray(y, dtype=np.float32)
    if y.size < 2048:
        return np.zeros(0)
    return librosa.feature.rms(y=y, frame_length=2048, hop_length=512)[0]


def crest_factor(y: np.ndarray, deja_enveloppe: bool = False) -> float:
    """Crête de l'enveloppe rapportée à sa valeur efficace, ou 0 si trop court.

    Un RAPPORT, donc insensible au volume : numérateur et dénominateur suivent
    la même échelle. La métrique entière est insensible au niveau -- une machine
    ne doit pas gagner parce qu'elle sort plus fort -- et ce terme ne fait pas
    exception.
    """
    env = np.asarray(y, dtype=np.float64) if deja_enveloppe else raw_envelope(y)
    if env.size < TRAMES_MINIMALES:
        return 0.0
    efficace = float(np.sqrt(np.mean(env ** 2)))
    if efficace <= 1e-12:
        return 0.0
    return float(np.max(np.abs(env))) / efficace


def dynamics_term(target_crest: float, candidate_crest: float) -> float:
    """Écart de facteur de crête en OCTAVES, ou 0 si l'un des deux est muet.

    Rendre 0 quand on ne sait pas est la même règle que le terme de hauteur de
    v3 : un terme qui n'a rien à dire se tait, il n'invente pas une pénalité.
    """
    if target_crest <= 0.0 or candidate_crest <= 0.0:
        return 0.0
    return float(abs(np.log2(candidate_crest / target_crest)))


def distance_terms(
    target_features: Dict[str, object],
    candidate_features: Dict[str, object],
    target_envelope: np.ndarray,
    candidate_envelope: np.ndarray,
    target_contrast: np.ndarray,
    candidate_contrast: np.ndarray,
    target_pitch: tuple,
    candidate_pitch: tuple,
    target_crest: float,
    candidate_crest: float,
) -> Dict[str, float]:
    terms = distance_terms_v3(target_features, candidate_features,
                              target_envelope, candidate_envelope,
                              target_contrast, candidate_contrast,
                              target_pitch, candidate_pitch)
    terms["dynamics"] = dynamics_term(target_crest, candidate_crest)
    return terms


def combine(terms: Dict[str, float]) -> float:
    return float(sum(WEIGHTS[cle] * valeur for cle, valeur in terms.items()))


def audio_distance_v4(target: np.ndarray, candidate: np.ndarray, sr: int) -> float:
    longueur = min(len(target), len(candidate))
    if longueur == 0:
        return 1e6
    target = np.asarray(target)[:longueur]
    candidate = np.asarray(candidate)[:longueur]
    return combine(distance_terms(
        spectral_features(target, sr), spectral_features(candidate, sr),
        envelope(target), envelope(candidate),
        spectral_contrast(target, sr), spectral_contrast(candidate, sr),
        low_pitch(target, sr), low_pitch(candidate, sr),
        crest_factor(target), crest_factor(candidate),
    ))
