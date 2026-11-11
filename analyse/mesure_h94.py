"""H94 — le profil d'une frappe moyenne : énergie par bande et par tranche (docs/CDC-reload-indifferenciable.md § 55).

`profil(y, sr, debut_s)` : `y` est une frappe (le kick moyen de H69, qui commence `debut_s` après le début du tableau) ;
rend, pour chaque bande de `BANDES` et chaque tranche de `TRANCHES_S` (comptées depuis la frappe), l'énergie en dB.
Le signal est filtré APRÈS un bourrage de zéros d'une seconde de chaque côté (un filtre de 20 Hz sur 300 ms résonne
sur ses bords). Une tranche qui dépasse le signal est un REFUS.
"""
from __future__ import annotations

from typing import Dict, List

import numpy as np

BANDES = {"sub": (20.0, 60.0), "basse": (60.0, 150.0), "bas-médium": (150.0, 500.0)}
TRANCHES_S = ((0.0, 0.1), (0.1, 0.2), (0.2, 0.3))


def filtrer_bande(y: np.ndarray, sr: int, bande: str) -> np.ndarray:
    from scipy.signal import butter, sosfiltfilt

    marge = sr
    x = np.concatenate([np.zeros(marge), y, np.zeros(marge)])
    return sosfiltfilt(butter(4, list(BANDES[bande]), btype="band", fs=sr, output="sos"), x)[marge:marge + len(y)]


def profil(y: np.ndarray, sr: int, debut_s: float) -> Dict[str, List[float]]:
    d = int(round(debut_s * sr))
    sortie: Dict[str, List[float]] = {}
    for bande in BANDES:
        p = filtrer_bande(y, sr, bande) ** 2
        valeurs = []
        for a, b in TRANCHES_S:
            i0, i1 = d + int(round(a * sr)), d + int(round(b * sr))
            if i1 > len(p) + 1:
                raise ValueError(f"la tranche {a}-{b} s dépasse la frappe ({len(p) / sr:.3f} s)")
            valeurs.append(round(float(10 * np.log10(max(float(p[i0:i1].sum()), 1e-30))), 2))
        sortie[bande] = valeurs
    return sortie
