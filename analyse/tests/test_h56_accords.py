"""H56 — l'instrument qui juge chaque raie là où elle sonne (docs/CDC-reload-indifferenciable.md § 18).

L'ATTENDU 1 du § 18, écrit avant toute mesure de l'original. Cinq hauteurs, bruit à
−30 dB, des notes qui s'allument et s'éteignent par segments de 4 à 7 s (fondus de
50 ms, −30 dB quand elles se taisent) :
  (a) une lecture : au moins 90 % des segments en cercle, retard à f1 relu à ± 15 % ;
  (b) deux étages en série : au plus 20 % des segments en cercle — sur TOUTES les raies :
      intermittentes, les deux du haut tombent sous 10 dB de leur fond (7,3 et 8,8 dB) et
      n'ont plus de segment ;
  (c) une lecture sous des notes tenues 24 s : toutes les raies en cercle.
Et le verdict : un extrait dont le contrôle n'y voit pas ne juge rien.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Callable, Dict, List

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

import mesure_h47 as h47  # noqa: E402
import mesure_h53 as h53  # noqa: E402
import mesure_h56 as h  # noqa: E402
import test_h53_cercle as t53  # noqa: E402

NOTES = t53.NOTES
SR = t53.SR


def _portes(graine: int) -> Dict[int, np.ndarray]:
    """Pour chaque note, une porte qui s'ouvre et se ferme par segments de 4 à 7 s (fondus de 50 ms)."""
    t = t53._t()
    rng = np.random.default_rng(graine)
    sortie = {}
    for note in NOTES:
        porte = np.full(len(t), 10 ** (-30 / 20))
        instant, ouverte = rng.uniform(-3.0, 0.0), bool(rng.integers(0, 2))
        while instant < t[-1]:
            duree = rng.uniform(4.0, 7.0)
            if ouverte:
                porte[(t >= instant) & (t < instant + duree)] = 1.0
            instant += duree
            ouverte = not ouverte
        k = int(0.05 * SR)
        sortie[note] = np.convolve(porte, np.ones(k) / k, mode="same")
    return sortie


def _voix_une_lecture(t: np.ndarray, f: float) -> np.ndarray:
    tau = t53._retard(t, t53.BASE_S, t53.F1_S, t53.F2_S)
    return (1 - t53.DOSAGE) * np.sin(2 * np.pi * f * t) + t53.DOSAGE * np.sin(2 * np.pi * f * (t - tau))


def _voix_serie(t: np.ndarray, f: float) -> np.ndarray:
    m1, m2 = 0.47, 0.41
    tau2 = 6.81e-3 + 1.10e-3 * np.sin(2 * np.pi * h53.F2_HZ * t)

    def etage1(u: np.ndarray) -> np.ndarray:
        retard = 1.81e-3 + 1.68e-3 * np.sin(2 * np.pi * h53.F1_HZ * u)
        return (1 - m1) * np.sin(2 * np.pi * f * u) + m1 * np.sin(2 * np.pi * f * (u - retard))

    return (1 - m2) * etage1(t) + m2 * etage1(t - tau2)


def _signal(voix: Callable[[np.ndarray, float], np.ndarray], portes: Dict[int, np.ndarray] | None) -> np.ndarray:
    t = t53._t()
    y = np.zeros_like(t)
    for note in NOTES:
        v = voix(t, h47.hz_de(note, h53.LA4_HZ))
        y += v * portes[note] if portes is not None else v
    return y + t53._bruit(57, len(t))


_MEMO: Dict[str, List[Dict[str, Any]]] = {}


def _raies(nom: str, fabrique: Callable[[], np.ndarray]) -> List[Dict[str, Any]]:
    if nom not in _MEMO:
        _MEMO[nom] = h.analyser(fabrique(), SR, NOTES)
    return _MEMO[nom]


def _decrire(raies: List[Dict[str, Any]]) -> str:
    return " | ".join(f"{r['note']}: " + ", ".join(
        f"{s['debut_s']}-{s['fin_s']} {s['circularite']}/{s['arc_deg']}/{s['aplatissement']} r{s['retard_f1_ms']}"
        for s in r.get("segments", [])) for r in raies)


@test
def h56_une_lecture_intermittente_se_lit_segment_par_segment():
    raies = _raies("une", lambda: _signal(_voix_une_lecture, _portes(11)))
    c, n = h.part_de_cercles(raies)
    assert_true(n >= 8, f"{n} segment(s) seulement — {_decrire(raies)}")
    assert_true(c >= 0.9 * n, f"{c} cercle(s) sur {n} segments — {_decrire(raies)}")
    for r in raies:
        for s in r["segments"]:
            if s["cercle"]:
                assert_true(abs(s["retard_f1_ms"] - 0.85) <= 0.15 * 0.85,
                            f"raie {r['note']} {s['debut_s']}-{s['fin_s']} s : retard {s['retard_f1_ms']} ms pour 0,85")


@test
def h56_la_serie_intermittente_n_est_pas_un_cercle():
    raies = _raies("serie", lambda: _signal(_voix_serie, _portes(11)))
    c, n = h.part_de_cercles(raies)
    assert_true(n >= 4, f"{n} segment(s) — {_decrire(raies)}")
    assert_true(c <= 0.2 * n, f"{c} cercle(s) sur {n} segments — {_decrire(raies)}")


@test
def h56_des_notes_tenues_sont_des_cercles():
    raies = _raies("tenues", lambda: _signal(_voix_une_lecture, None))
    for r in raies:
        assert_true(len(r["segments"]) >= 1, f"raie {r['note']} sans segment — {_decrire(raies)}")
        assert_true(all(s["cercle"] for s in r["segments"]), f"raie {r['note']} — {_decrire(raies)}")


@test
def h56_un_extrait_dont_le_controle_ne_voit_pas_ne_juge_rien():
    def raie(cercles: List[bool]) -> Dict[str, Any]:
        return {"note": 60, "vue": True, "segments": [{"cercle": c, "retard_f1_ms": 0.85} for c in cercles]}

    aveugle = {int(n): v for n, v, _ in h.verdict({"controle": [raie([True, False, False])], "raies": [raie([True] * 3)]})}
    assert_equal(aveugle, {2: "ÉCHEC", 3: "NE JUGE PAS", 4: "SANS OBJET"})
    voit = {int(n): v for n, v, _ in h.verdict({"controle": [raie([True] * 5)], "raies": [raie([False] * 5)]})}
    assert_equal(voit, {2: "TENU", 3: "ÉCHEC", 4: "SANS OBJET"})
    bus = {int(n): v for n, v, _ in h.verdict({"controle": [raie([True] * 5)], "raies": [raie([True] * 5)]})}
    assert_equal(bus, {2: "TENU", 3: "TENU", 4: "TENU"})
