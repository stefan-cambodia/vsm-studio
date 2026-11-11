"""H97 — l'écart de deux profils de kick (docs/CDC-reload-indifferenciable.md § 58).

Un profil est celui de `mesure_h94.profil` : {"sub": [t0, t1, t2], "basse": [t0, t1, t2]} en dB. `resume` en tire sept
chiffres — sub et basse aux tranches 1 et 2 SOUS leur tête, et basse − sub aux trois tranches ; `ecart_profil` est la
moyenne des écarts absolus de deux résumés. Le niveau absolu n'y entre pas.
"""
from __future__ import annotations

from typing import List, Mapping, Sequence


def resume(p: Mapping[str, Sequence[float]]) -> List[float]:
    if len(p["sub"]) != 3 or len(p["basse"]) != 3:
        raise ValueError("un profil a trois tranches par bande")
    s, b = p["sub"], p["basse"]
    return [s[1] - s[0], s[2] - s[0], b[1] - b[0], b[2] - b[0], b[0] - s[0], b[1] - s[1], b[2] - s[2]]


def ecart_profil(a: Mapping[str, Sequence[float]], b: Mapping[str, Sequence[float]]) -> float:
    ra, rb = resume(a), resume(b)
    return round(sum(abs(x - y) for x, y in zip(ra, rb, strict=True)) / len(ra), 2)


def resume3(p: Mapping[str, Sequence[float]]) -> List[float]:
    """Les sept chiffres de `resume`, plus cinq pour le bas-médium (H98, § 59) : sous sa tête aux tranches 1 et 2, et
    bas-médium − basse aux trois tranches."""
    if "bas-médium" not in p or len(p["bas-médium"]) != 3:
        raise ValueError("un profil à trois bandes porte « bas-médium », trois tranches")
    m, b = p["bas-médium"], p["basse"]
    return resume(p) + [m[1] - m[0], m[2] - m[0], m[0] - b[0], m[1] - b[1], m[2] - b[2]]


def ecart_profil3(a: Mapping[str, Sequence[float]], b: Mapping[str, Sequence[float]]) -> float:
    ra, rb = resume3(a), resume3(b)
    return round(sum(abs(x - y) for x, y in zip(ra, rb, strict=True)) / len(ra), 2)
