"""H90 — des attaques détectées appariées aux notes d'un projet (docs/CDC-reload-indifferenciable.md § 51).

`apparier(a, b, tol)` : pour chaque instant de `a`, vrai s'il y a un instant de `b` à `tol` au plus.
`position_16e(t, tempo, origine)` : la double-croche du temps (0 à 3) la plus proche, et l'écart à elle en secondes.
`suit_de_pres(a, delai)` : pour chaque instant de `a` (trié), vrai s'il suit le précédent de moins de `delai`.
"""
from __future__ import annotations

from typing import Sequence, Tuple

import numpy as np


def apparier(a: Sequence[float], b: Sequence[float], tol: float) -> np.ndarray:
    x, y = np.asarray(list(a), dtype=float), np.sort(np.asarray(list(b), dtype=float))
    if len(y) == 0:
        return np.zeros(len(x), dtype=bool)
    i = np.clip(np.searchsorted(y, x), 1, len(y) - 1) if len(y) > 1 else np.zeros(len(x), dtype=int)
    d = np.abs(x - y[i]) if len(y) == 1 else np.minimum(np.abs(x - y[i - 1]), np.abs(x - y[i]))
    return d <= tol + 1e-9


def position_16e(t: float, tempo: float, origine: float) -> Tuple[int, float]:
    pas = 60.0 / tempo / 4.0
    k = round((t - origine) / pas)
    return int(k % 4), float(t - origine - k * pas)


def suit_de_pres(a: Sequence[float], delai: float) -> np.ndarray:
    x = np.asarray(list(a), dtype=float)
    if len(x) and np.any(np.diff(x) < 0):
        raise ValueError("instants non triés")
    return np.concatenate([[False], np.diff(x) < delai]) if len(x) else np.zeros(0, dtype=bool)
