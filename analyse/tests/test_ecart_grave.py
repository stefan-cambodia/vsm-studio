"""`grave_aux_frappes` de tools/ecart-a-l-original.py (CDC-reload § 50) sur des signaux CONNUS, avant de servir."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

import mesure_h69 as h69  # noqa: E402
import mesure_h77 as h77  # noqa: E402

SR, DUREE = 44100, 20
OUTIL = h77.outil()
KICKS = np.arange(1.0, DUREE - 1.0, 0.5)


def kicks(decroissance):
    x = np.zeros(DUREE * SR)
    for t in KICKS:
        i = int(round(t * SR))
        k = h69.replique_tr808(45.0, decroissance, SR, duree=0.45)
        x[i:i + len(k)] += k
    return x


NOTE = 0.5 * np.sin(2 * np.pi * 100.0 * np.arange(DUREE * SR) / SR)
ORIGINAL = kicks(0.06) + NOTE


def mesurer(b):
    return OUTIL.grave_aux_frappes(ORIGINAL, b, SR, KICKS)["bandes"]


@test
def test_grave_identique_et_gain():
    for v in mesurer(ORIGINAL).values():
        assert_equal((v["aux_frappes_db"], v["entre_db"]), (0.0, 0.0))
    for v in mesurer(2.0 * ORIGINAL).values():
        assert_true(abs(v["aux_frappes_db"] - 6.02) < 0.01 and abs(v["entre_db"] - 6.02) < 0.01, f"×2 : {v}")


@test
def test_grave_un_kick_allonge_se_lit_entre_les_frappes():
    b = mesurer(kicks(0.25) + NOTE)
    assert_true(b["sub"]["entre_db"] > 6.0, f"sub entre les frappes : {b['sub']}")
    # un kick allongé a AUSSI plus d'énergie dans sa propre fenêtre (0,06 s s'éteint avant 100 ms) : ce qui le signe,
    # c'est que l'écart entre les frappes dépasse de loin celui aux frappes
    assert_true(b["sub"]["entre_db"] - b["sub"]["aux_frappes_db"] > 10.0, f"entre contre aux frappes : {b['sub']}")


@test
def test_grave_une_basse_otee_se_lit_entre_les_frappes_et_dans_sa_bande():
    b = mesurer(kicks(0.06))
    assert_true(b["basse"]["entre_db"] < -10.0, f"basse entre les frappes : {b['basse']}")
    assert_true(abs(b["sub"]["aux_frappes_db"]) < 1.0, f"sub aux frappes : {b['sub']}")


@test
def test_grave_un_break_n_est_ni_l_un_ni_l_autre_et_sans_attaque_rien_n_est_mesure():
    r = OUTIL.grave_aux_frappes(ORIGINAL, ORIGINAL, SR, np.array([2.0, 2.5, 10.0, 10.5]))
    assert_true(r["hors_s"] > 15.0 and 0.7 < r["entre_s"] < 0.9, f"le trou de 7,5 s est hors mesure : {r}")
    assert_equal(OUTIL.grave_aux_frappes(ORIGINAL, ORIGINAL, SR, np.array([]))["mesurable"], False)


@test
def test_grave_les_attaques_detectees_sont_les_kicks():
    a = OUTIL.attaques_basses(ORIGINAL, SR)
    assert_true(abs(len(a) - len(KICKS)) <= 0.1 * len(KICKS), f"{len(a)} attaques pour {len(KICKS)} kicks")


if __name__ == "__main__":
    raise SystemExit(run())
