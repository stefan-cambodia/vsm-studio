"""H101 — l'énergie d'un signal par bande, morceau entier (docs/CDC-reload-indifferenciable.md § 62).

`energie_par_bande(x, sr, bandes)` : la densité spectrale de Welch (fenêtres de 4096) intégrée sur chaque bande
[bas, haut) en Hz — une énergie par bande, linéaire. Une bande au-delà de Nyquist est un REFUS.
`centroide(x, sr, coupe_hz)` : le centroïde de cette densité, facultativement bornée en haut à `coupe_hz`.
"""
from __future__ import annotations

from typing import Dict, Optional, Sequence, Tuple

import numpy as np


def _welch(x: np.ndarray, sr: int) -> Tuple[np.ndarray, np.ndarray]:
    from scipy.signal import welch

    return welch(np.asarray(x, dtype=np.float64), fs=sr, nperseg=4096)


def energie_par_bande(x: np.ndarray, sr: int, bandes: Sequence[Tuple[float, float]]) -> Dict[str, float]:
    f, p = _welch(x, sr)
    df = float(f[1] - f[0])
    sortie: Dict[str, float] = {}
    for bas, haut in bandes:
        if haut > sr / 2 + 1e-6 or bas >= haut:
            raise ValueError(f"bande {bas}-{haut} Hz hors de [0, {sr / 2:.0f}]")
        sortie[f"{bas:g}-{haut:g}"] = float(p[(f >= bas) & (f < haut)].sum() * df)
    return sortie


def centroide(x: np.ndarray, sr: int, coupe_hz: Optional[float] = None) -> float:
    f, p = _welch(x, sr)
    if coupe_hz is not None:
        p = np.where(f < coupe_hz, p, 0.0)
    total = float(p.sum())
    if total <= 0:
        raise ValueError("signal sans énergie sous la coupure")
    return float((f * p).sum() / total)
