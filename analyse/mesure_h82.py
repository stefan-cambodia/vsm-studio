"""H82 — la machine d'une piste mélodique choisie contre son stem (docs/CDC-reload-indifferenciable.md § 43).

`gain_d_energie` : le gain qui porte un solo à l'énergie du stem sur le morceau entier ; un solo à `MUET_DB` ou moins
sous le stem est MUET (None), jamais un gain géant. `choisir` : le candidat de plus petit log-mel au stem, s'il gagne
au moins `GAIN_DB` sur le témoin ; à égalité, le premier dans l'ordre des noms.
"""
from __future__ import annotations

import math
from typing import Mapping, Optional

import numpy as np

MUET_DB = -60.0
GAIN_DB = 0.5


def gain_d_energie(stem: np.ndarray, solo: np.ndarray, muet_db: float = MUET_DB) -> Optional[float]:
    n = min(len(stem), len(solo))
    es, eo = float(np.sum(stem[:n] ** 2)), float(np.sum(solo[:n] ** 2))
    if es <= 0:
        raise ValueError("le stem est muet")
    if eo <= 0 or 10 * math.log10(eo / es) <= muet_db:
        return None
    return math.sqrt(es / eo)


def choisir(temoin: float, candidats: Mapping[str, float], gain_db: float = GAIN_DB) -> Optional[str]:
    if not candidats:
        return None
    m = min(sorted(candidats), key=lambda k: candidats[k])
    return m if candidats[m] <= temoin - gain_db + 1e-9 else None
