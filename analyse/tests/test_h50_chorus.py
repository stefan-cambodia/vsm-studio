"""H50 — l'instrument qui juge « un sinus par note sous un chorus » (docs/CDC-reload-indifferenciable.md § 12).

C'est l'ATTENDU 1 du § 12, écrit avant toute mesure sur les deux extraits neufs :

  - des sinus sous un RETARD MODULÉ à 1,751 Hz (± 1,2 ms, moitié direct), à cinq
    hauteurs de l'accord : porteuses au tempéré, ≥ 90 % des composantes sur la
    famille, `f1` relu à ± 0,01 Hz, la signature « f1 en bas, 2·f1 en haut » lue ;
  - les mêmes sous un TRÉMOLO de 5 Hz : moins de 40 % sur la famille — l'instrument
    ne dit pas « chorus » de n'importe quelle modulation ;
  - un extrait où moins de quatre raies se voient est « muet », jamais jugé.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Dict

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

import mesure_h47 as h47  # noqa: E402
import mesure_h50 as h  # noqa: E402

SR = 11025
SECONDES = 24.0
NOTES = (52, 59, 64, 70, 73)             # mi3, si3, mi4, la♯4, do♯5 : de 166 à 558 Hz


def _t() -> np.ndarray:
    return np.arange(int(SECONDES * SR)) / SR


def _bruit(graine: int) -> np.ndarray:
    return np.random.default_rng(graine).standard_normal(len(_t())) * np.sqrt(0.5 * 10 ** (-30 / 10))


def _chorus(profondeur_s: float = 1.2e-3, cadence: float = 1.751) -> np.ndarray:
    t = _t()
    retard = 5e-3 + profondeur_s * np.sin(2 * np.pi * cadence * t)
    y = np.zeros_like(t)
    for note in NOTES:
        f = h47.hz_de(note, h.LA4_HZ)
        y += 0.5 * np.sin(2 * np.pi * f * t) + 0.5 * np.sin(2 * np.pi * f * (t - retard))
    return y + _bruit(11)


def _tremolo() -> np.ndarray:
    t = _t()
    y = np.zeros_like(t)
    for note in NOTES:
        y += (1 + 0.6 * np.sin(2 * np.pi * 5.0 * t)) * np.sin(2 * np.pi * h47.hz_de(note, h.LA4_HZ) * t)
    return y + _bruit(12)


def _verdicts(extrait: Dict[str, Any]) -> Dict[int, str]:
    return {n: v for n, v, _detail in h.juger(extrait)}


@test
def h50_un_retard_module_est_lu_comme_tel():
    x = _chorus()
    extrait = h.analyser(x, x, SR)
    assert_equal(extrait["notes"], list(NOTES), "les cinq notes, malgré les bandes latérales")
    vues = [r for r in extrait["raies"] if r["vue"]]
    assert_equal(len(vues), len(NOTES), "toutes les raies se voient")
    for r in vues:
        assert_true(abs(r["porteuse_cents"]) <= 1.0, f"la porteuse au tempéré : {r['porteuse_cents']} cents à {r['nominal_hz']} Hz")
    secondaires = [c for r in vues for c in r["secondaires"]]
    sur = [c for c in secondaires if c["membre"]]
    assert_true(len(secondaires) >= 10, f"des bandes latérales à lire : {len(secondaires)}")
    assert_true(len(sur) / len(secondaires) >= 0.9, f"{len(sur)} sur {len(secondaires)} sur la famille")
    v = _verdicts(extrait)
    assert_equal((v[2], v[3], v[4], v[5]), ("TENU", "TENU", "TENU", "TENU"), "les quatre attendus")
    assert_equal(h.issue({"extraits": [extrait, extrait]}), "TENUE")


@test
def h50_une_autre_cadence_fait_tomber_f1():
    # le même chorus à 1,80 Hz : les composantes restent à ± 0,08 Hz de la famille pour k = 1,
    # mais f1 relu sort de ± 0,030 Hz — l'attendu 4 doit le voir
    x = _chorus(cadence=1.80)
    v = _verdicts(h.analyser(x, x, SR))
    assert_equal(v[4], "ÉCHEC", "f1 relu à 1,80 Hz n'est pas 1,751")


@test
def h50_un_tremolo_n_est_pas_un_chorus():
    x = _tremolo()
    extrait = h.analyser(x, x, SR)
    v = _verdicts(extrait)
    assert_equal(v[3], "ÉCHEC", "des bandes latérales à ± 5 Hz ne sont pas sur la famille")
    assert_equal(h.issue({"extraits": [extrait]}), "RÉFUTÉE")


@test
def h50_un_extrait_sans_raies_est_muet():
    t = _t()
    stem = np.sin(2 * np.pi * h47.hz_de(64, h.LA4_HZ) * t) + _bruit(13)
    original = 0.01 * stem + 3.0 * np.random.default_rng(14).standard_normal(len(t))
    extrait = h.analyser(original, stem, SR)
    assert_equal(set(_verdicts(extrait).values()), {"MUET"}, "rien ne se voit dans l'original : rien ne se juge")


@test
def h50_la_famille_et_son_hasard():
    assert_equal(len(h.famille()), 9, "neuf membres")
    assert_true(abs(h.taux_du_hasard() - 0.24) < 0.005, f"24 % de la bande : {h.taux_du_hasard()}")
    assert_equal(h.membre(-1.75), "f1")
    assert_equal(h.membre(3.52), "2f1")
    assert_equal(h.membre(-5.89), "2f1+f2")
    assert_equal(h.membre(0.67), "f2-f1")
    assert_equal(h.membre(5.0), None, "5,0 Hz n'est sur aucun membre")
