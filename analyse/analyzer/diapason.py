"""H42 — le diapason d'un morceau : estimé sur le mélange, porté par chaque rendu.

docs/CDC-reload-indifferenciable.md § 4. « Reload » est accordé 12,3 cents au-dessus
de 440 Hz ; rejoué à 440, chaque note tenue bat contre l'original.

L'ESTIMATEUR est celui de `tools/ecart-a-l-original.py`, validé sur perturbations
connues (−13 cents lus −13,5 ; +20 lus +20,5) : moyenne CIRCULAIRE, pondérée par la
puissance, des écarts au demi-ton des pics spectraux tenus entre 100 et 2 000 Hz
(Welch à fenêtre de 1,5 s, tranches de 20 s). `librosa.estimate_tuning` sur le
mélange a été ESSAYÉ et ÉCARTÉ : la batterie y noie les partiels (+7 lus pour +13).

LA VALEUR DE SESSION. Tous les rendus d'une course — ceux de l'arbitrage (requêtes
`--serve`, champ « diapason ») comme les projets écrits (`transport.referenceA4Hz`) —
doivent jouer au même diapason, sans quoi l'arbitrage comparerait des timbres
désaccordés au stem et choisirait autre chose que ce que le projet jouera. Une
course la pose une fois (`poser`) ; 440 par défaut, la chaîne d'avant au bit près.
"""

from __future__ import annotations

from typing import Dict, Optional

import numpy as np

_SESSION = {"la4": 440.0}


def poser(la4_hz: float) -> None:
    """Pose le diapason de la course (Hz). Hors de 400-480 : refusé, bruyamment."""
    if not (400.0 <= float(la4_hz) <= 480.0):
        raise ValueError(f"diapason {la4_hz} Hz hors des bornes 400-480 Hz")
    _SESSION["la4"] = float(la4_hz)


def valeur() -> float:
    return _SESSION["la4"]


def estimer_cents(mono: np.ndarray, sr: int) -> Dict[str, Optional[float]]:
    """L'écart au diapason 440, en cents ; `cents` = None si non mesurable (< 20 pics)."""
    from scipy.signal import find_peaks, welch
    tranche = 20 * sr
    angles, poids = [], []
    for debut in range(0, max(1, len(mono) - sr), tranche):
        z = mono[debut:debut + tranche]
        if len(z) < 2 * 65536:
            continue
        f, P = welch(z, fs=sr, nperseg=65536, noverlap=32768)
        k = (f > 100) & (f < 2000)
        Pdb = 10 * np.log10(P[k] + 1e-20)
        pics, _ = find_peaks(Pdb, prominence=12, distance=6)
        for i in pics:
            fi = f[k][i]
            if 0 < i < len(Pdb) - 1:
                a, b, c = Pdb[i - 1], Pdb[i], Pdb[i + 1]
                if (a - 2 * b + c) != 0:
                    fi = fi + 0.5 * (a - c) / (a - 2 * b + c) * (f[1] - f[0])
            midi = 69 + 12 * np.log2(fi / 440.0)
            angles.append(2 * np.pi * (midi - np.round(midi)))
            poids.append(10 ** (Pdb[i] / 10))
    if len(angles) < 20:
        return {"cents": None, "pics": float(len(angles))}
    w = np.array(poids)
    z = np.sum(w * np.exp(1j * np.array(angles))) / np.sum(w)
    return {"cents": round(float(np.angle(z) / (2 * np.pi) * 100), 1), "pics": float(len(angles)),
            "concentration": round(float(np.abs(z)), 2)}


def la4_depuis_cents(cents: float) -> float:
    return 440.0 * 2.0 ** (cents / 1200.0)
