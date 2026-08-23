"""
Distance audio, troisième version : la deuxième, plus un terme de HAUTEUR pour
les sons graves.

CE QUI N'ALLAIT PAS, ET LA MESURE QUI L'A DIT. Les sept termes de v2 sont des
statistiques de FORME spectrale -- centroïde, largeur, roll-off, planéité,
coefficients cepstraux, enveloppe, contraste -- et aucun ne mesure une hauteur.
Sur une cible contrôlée (un kick 808 à 60 Hz contre des kicks 808 dont seul
l'accord varie), v2 tombe juste : minimum exact à 60 Hz, 30 Hz le pire. Mais
sur un VRAI kick dont le pic mesuré est à 59 Hz, elle préfère 30 Hz et trouve
60 Hz le pire (0,5807 contre 0,6212 sur une frappe isolée). Un 808 à 30 Hz avec
sa longue queue ressemble davantage EN SILHOUETTE à un kick compressé et cliqué
qu'un 808 à 60 Hz, hauteur fausse ou pas. L'oreille entend une hauteur ; v2
entend une silhouette. Détail dans ROADMAP-fusion.md § 5 septies.

CE QUE FAIT CETTE VERSION. Un terme de plus, et un seul : la position du pic
d'énergie sous 150 Hz, comparée EN RAPPORT DE FRÉQUENCES (c'est-à-dire en
octaves), entre cible et candidat. Une octave d'écart vaut 1 ; rien d'autre ne
change. Le terme est NUL quand la cible n'a pas de grave (moins d'un dixième de
son énergie sous 150 Hz) : une nappe aiguë ne doit pas être jugée sur un pic
grave qui n'est que du bruit.

CE QU'ELLE NE FAIT PAS. Elle ne touche pas aux sept termes existants ni à leurs
poids. Un défaut à la fois, et on mesure -- la règle de v2, reprise telle
quelle.

ET LA RÈGLE QUI PRIME : deux métriques ne se comparent pas (§ 10.3 de la
feuille de route). Toute distance publiée porte sa métrique, et « v3 » n'est
comparable qu'à « v3 ». La chaîne reste en v2 par défaut tant que v3 n'a pas
été mesurée sur les bancs existants ; `--metrique v3` l'active.
"""

from __future__ import annotations

from typing import Dict

import numpy as np

from .audio_distance import envelope, spectral_features
from .audio_distance_v2 import WEIGHTS as WEIGHTS_V2
from .audio_distance_v2 import distance_terms as distance_terms_v2
from .audio_distance_v2 import spectral_contrast

# Le terme de hauteur pèse autant que l'enveloppe ou le contraste : une
# hauteur fausse d'une octave est une faute de même rang qu'une enveloppe
# fausse, pas un détail. Les sept autres poids sont ceux de v2, inchangés.
WEIGHTS: Dict[str, float] = {**WEIGHTS_V2, "pitch": 0.20}

# Borne haute de la zone « grave » : au-dessus, on n'est plus dans le
# fondamental d'un kick ou d'une basse, et le centroïde fait déjà le travail.
GRAVE_MAX_HZ = 150.0
# Part d'énergie sous GRAVE_MAX_HZ en deçà de laquelle la cible n'a pas de
# grave à comparer : le terme est alors nul, pas calculé sur du bruit.
GRAVE_PART_MINIMALE = 0.10


def low_pitch(y: np.ndarray, sr: int) -> tuple:
    """(fréquence du pic sous 150 Hz, part d'énergie du grave).

    Le pic est cherché dans le spectre de puissance MOYEN -- la somme des
    trames, pas une seule --, ce qui le rend insensible à la phase et au
    glissando d'attaque d'un kick : c'est la hauteur TENUE qu'on veut, celle
    que l'oreille retient.
    """
    y = np.asarray(y, dtype=np.float64)
    if y.size < 2048:
        return 0.0, 0.0
    n = 8192
    hop = 2048
    fenetre = np.hanning(n)
    puissance = np.zeros(n // 2 + 1)
    for debut in range(0, max(1, y.size - n), hop):
        tranche = y[debut:debut + n]
        if tranche.size < n:
            break
        puissance += np.abs(np.fft.rfft(tranche * fenetre)) ** 2
    frequences = np.fft.rfftfreq(n, 1.0 / sr)
    total = float(puissance[frequences >= 20.0].sum()) + 1e-12
    grave = (frequences >= 20.0) & (frequences <= GRAVE_MAX_HZ)
    part = float(puissance[grave].sum()) / total
    if not grave.any():
        return 0.0, part
    pic = float(frequences[grave][int(np.argmax(puissance[grave]))])
    return pic, part


def pitch_term(target_pitch: tuple, candidate_pitch: tuple) -> float:
    """Écart de hauteur en OCTAVES, ou 0 si la cible n'a pas de grave."""
    pic_cible, part_cible = target_pitch
    pic_candidat, _ = candidate_pitch
    if part_cible < GRAVE_PART_MINIMALE or pic_cible <= 0.0:
        return 0.0
    if pic_candidat <= 0.0:
        # Le candidat n'a pas de grave du tout là où la cible en a : une octave
        # d'écart, c'est-à-dire le prix d'une hauteur franchement fausse.
        return 1.0
    return float(abs(np.log2(pic_candidat / pic_cible)))


def distance_terms(
    target_features: Dict[str, object],
    candidate_features: Dict[str, object],
    target_envelope: np.ndarray,
    candidate_envelope: np.ndarray,
    target_contrast: np.ndarray,
    candidate_contrast: np.ndarray,
    target_pitch: tuple,
    candidate_pitch: tuple,
) -> Dict[str, float]:
    terms = distance_terms_v2(target_features, candidate_features, target_envelope,
                              candidate_envelope, target_contrast, candidate_contrast)
    terms["pitch"] = pitch_term(target_pitch, candidate_pitch)
    return terms


def combine(terms: Dict[str, float]) -> float:
    return float(sum(WEIGHTS[cle] * valeur for cle, valeur in terms.items()))


def audio_distance_v3(target: np.ndarray, candidate: np.ndarray, sr: int) -> float:
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
    ))
