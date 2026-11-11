"""H91 — l'énergie d'une bande dans des fenêtres autour d'instants (docs/CDC-reload-indifferenciable.md § 52)."""
from __future__ import annotations

from typing import Sequence, Tuple

import numpy as np

AVANT_S, APRES_S = 0.010, 0.100


def energie_fenetres(x: np.ndarray, sr: int, instants: Sequence[float], bande: Tuple[float, float]) -> float:
    """L'énergie moyenne par fenêtre entière de `x` filtré dans `bande` (Hz). Aucune fenêtre entière : REFUS."""
    from scipy.signal import butter, sosfiltfilt

    y = sosfiltfilt(butter(4, list(bande), btype="band", fs=sr, output="sos"), x) ** 2
    c = np.concatenate([[0.0], np.cumsum(y)])
    total, n = 0.0, 0
    for t in instants:
        i0, i1 = int(round((t - AVANT_S) * sr)), int(round((t + APRES_S) * sr))
        if i0 < 0 or i1 > len(y):
            continue
        total += float(c[i1] - c[i0])
        n += 1
    if n == 0:
        raise ValueError("aucune fenêtre entière")
    return total / n
