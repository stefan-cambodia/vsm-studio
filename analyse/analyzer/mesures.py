"""Les mesures de niveau, du côté Python — les mêmes que celles du mixeur.

POURQUOI CE MODULE EXISTE. Le critère de la phase D4 de ``docs/ROADMAP-daw.md``
est que « le mixage fait dans l'application et le mixage fait par ``analyse/``
sur les mêmes stems donnent le même LUFS à 0,1 près ». Deux moitiés d'un projet
qui mesurent différemment ne peuvent pas se comparer : la reconstruction
paraîtrait meilleure ou pire qu'elle n'est, selon celle des deux qu'on croit.

CE MODULE N'EST PAS UNE SECONDE IMPLÉMENTATION QU'ON ESPÈRE ÉQUIVALENTE. Il
suit la même norme (ITU-R BS.1770) avec les mêmes coefficients de biquad, et un
test (``analyse/tests/test_mesures.py``) le compare à ``vsm-measure``, qui
emploie le code C++ du mixeur, sur les mêmes fichiers. L'accord est vérifié, pas
supposé — c'est toute la différence.

Aucune dépendance au-delà de numpy, comme le reste de la chaîne.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Tuple

import numpy as np

# Le silence, en LUFS. Une valeur finie plutôt que -inf : elle se compare, se
# sérialise et s'affiche, là où -inf demande un cas particulier partout.
SILENCE_LUFS = -200.0


def _biquad_rbj_high_shelf(sample_rate: float, freq: float, q: float,
                            gain_db: float) -> Tuple[np.ndarray, np.ndarray]:
    """Coefficients RBJ d'un plateau aigu, identiques à ``dsp::Biquad``."""
    a_amp = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * freq / sample_rate
    cosw, sinw = math.cos(w0), math.sin(w0)
    alpha = sinw / 2.0 * math.sqrt((a_amp + 1.0 / a_amp) * (1.0 / q - 1.0) + 2.0)
    sq = 2.0 * math.sqrt(a_amp) * alpha
    b = np.array([
        a_amp * ((a_amp + 1.0) + (a_amp - 1.0) * cosw + sq),
        -2.0 * a_amp * ((a_amp - 1.0) + (a_amp + 1.0) * cosw),
        a_amp * ((a_amp + 1.0) + (a_amp - 1.0) * cosw - sq),
    ])
    a = np.array([
        (a_amp + 1.0) - (a_amp - 1.0) * cosw + sq,
        2.0 * ((a_amp - 1.0) - (a_amp + 1.0) * cosw),
        (a_amp + 1.0) - (a_amp - 1.0) * cosw - sq,
    ])
    return b / a[0], a / a[0]


def _biquad_rbj_high_pass(sample_rate: float, freq: float,
                           q: float) -> Tuple[np.ndarray, np.ndarray]:
    """Coefficients RBJ d'un passe-haut, identiques à ``dsp::Biquad``."""
    w0 = 2.0 * math.pi * freq / sample_rate
    cosw, sinw = math.cos(w0), math.sin(w0)
    alpha = sinw / (2.0 * q)
    b = np.array([(1.0 + cosw) * 0.5, -(1.0 + cosw), (1.0 + cosw) * 0.5])
    a = np.array([1.0 + alpha, -2.0 * cosw, 1.0 - alpha])
    return b / a[0], a / a[0]


def _filtrer(x: np.ndarray, b: np.ndarray, a: np.ndarray) -> np.ndarray:
    """Filtre biquad en forme directe II transposée.

    Écrit à la main plutôt qu'appelé à ``scipy.signal.lfilter`` : c'est
    EXACTEMENT la récurrence de ``dsp::Biquad``, dans le même ordre
    d'opérations, ce qui est la seule façon d'être sûr que l'écart entre les
    deux moitiés vienne du signal et non d'un détail d'implémentation.
    """
    y = np.empty_like(x, dtype=np.float64)
    z1 = 0.0
    z2 = 0.0
    for n in range(x.size):
        entree = float(x[n])
        sortie = b[0] * entree + z1
        z1 = b[1] * entree - a[1] * sortie + z2
        z2 = b[2] * entree - a[2] * sortie
        y[n] = sortie
    return y


@dataclass
class Mesures:
    """Ce qu'on mesure d'un signal, dans les mêmes unités que le mixeur."""
    peak: float
    rms: float
    lufs: float
    correlation: float


def ponderation_k(x: np.ndarray, sample_rate: float) -> np.ndarray:
    """Applique la pondération K de BS.1770 : plateau aigu puis passe-haut."""
    b1, a1 = _biquad_rbj_high_shelf(sample_rate, 1681.97, 0.7071752, 3.99984)
    b2, a2 = _biquad_rbj_high_pass(sample_rate, 38.13, 0.5003270)
    return _filtrer(_filtrer(x, b1, a1), b2, a2)


def mesurer(gauche: np.ndarray, droite: np.ndarray, sample_rate: float) -> Mesures:
    """Crête, valeur efficace, LUFS intégré et corrélation de phase.

    Un signal MONO se mesure en passant deux fois le même canal : c'est ce que
    fait le moteur d'une piste mono, et c'est aussi ce qui donne une corrélation
    de +1 plutôt qu'une division par zéro.
    """
    gauche = np.asarray(gauche, dtype=np.float64).ravel()
    droite = np.asarray(droite, dtype=np.float64).ravel()
    if gauche.size != droite.size:
        raise ValueError("les deux canaux doivent avoir la même longueur")
    if gauche.size == 0:
        return Mesures(0.0, 0.0, SILENCE_LUFS, 1.0)

    peak = float(max(np.max(np.abs(gauche)), np.max(np.abs(droite))))
    rms = float(math.sqrt((np.sum(gauche ** 2) + np.sum(droite ** 2)) / (2.0 * gauche.size)))

    kg = ponderation_k(gauche, sample_rate)
    kd = ponderation_k(droite, sample_rate)
    somme = float(np.sum(kg ** 2) + np.sum(kd ** 2))
    # Mesure INTÉGRÉE non gatée, comme `dsp::LufsMeter` : ni porte absolue à
    # -70 LUFS ni porte relative à -10 LU. Les deux moitiés simplifient de la
    # même façon, ce qui est la condition pour qu'elles s'accordent.
    lufs = SILENCE_LUFS if somme <= 0.0 else -0.691 + 10.0 * math.log10(somme / gauche.size)

    denominateur = math.sqrt(float(np.sum(gauche ** 2)) * float(np.sum(droite ** 2)))
    correlation = 1.0 if denominateur <= 1e-20 else float(
        np.clip(float(np.sum(gauche * droite)) / denominateur, -1.0, 1.0))

    return Mesures(peak=peak, rms=rms, lufs=lufs, correlation=correlation)
