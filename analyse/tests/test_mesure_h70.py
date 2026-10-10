"""L'instrument de H70 (CDC-reload § 31) sur des signaux CONNUS, avant toute mesure de l'original.

Un « original » de forme connue — la réplique du TR-808, tenue 70 ms — et des « rendus » à d'autres tenues : le
choix doit tomber sur la bonne, et le facteur de niveau rendre un gain connu.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, run, test  # noqa: E402

import mesure_h69 as h69  # noqa: E402
import mesure_h70 as h70  # noqa: E402

SR = 44100


def kick_tenu(tenue, accord=37.0, decroissance=0.05, gain=1.0, duree=0.5):
    """La réplique du TR-808 à laquelle on ajoute la tenue telle que H70 la code : amplitude à 1, puis la
    décroissance."""
    k = h69.replique_tr808(accord, decroissance, SR, duree=duree)
    n = int(round(tenue * SR))
    amp_sans = np.exp(-np.arange(len(k)) / (decroissance * SR))
    amp = np.concatenate([np.ones(n), amp_sans[:len(k) - n]])
    return k / amp_sans * amp * gain


def scene(kick, graine):
    rng = np.random.default_rng(graine)
    x = np.zeros(60 * SR)
    instants = np.arange(1.0, 58.0, 0.6)
    for t in instants:
        i = int(round(t * SR))
        x[i:i + len(kick)] += kick
    x += 0.02 * rng.standard_normal(len(x))
    return x, instants + rng.uniform(-0.006, 0.006, len(instants))


@test
def test_h70_le_choix_tombe_sur_la_tenue_de_l_original():
    xo, inst = scene(kick_tenu(0.07), 1)
    original = h70.kick_moyen(xo, SR, inst)
    rendus = {}
    for tenue in (0.0, 0.04, 0.07, 0.10):
        xr, _ = scene(kick_tenu(tenue, gain=0.5), 2)
        rendus[tenue] = h70.kick_moyen(xr, SR, inst)
    choix = h70.choisir(original, rendus, SR)
    assert_equal(choix["tenue_s"], 0.07, f"écarts {choix['ecarts_db']}")
    assert_true(choix["ecarts_db"]["0.07"] < 0.5, f"écart à la bonne tenue {choix['ecarts_db']['0.07']}")
    assert_true(choix["ecarts_db"]["0"] > 3.0, f"sans tenue {choix['ecarts_db']['0']}")
    # le rendu est à moitié du niveau : le facteur doit rendre ×2 (+6,0 dB)
    assert_near(choix["facteur_niveau"], 2.0, 0.05, "facteur de niveau")


@test
def test_h70_l_enveloppe_est_relative_et_bornee():
    k = kick_tenu(0.05)
    y = np.concatenate([np.zeros(int(h69.AVANT_S * SR)), k])
    e1 = h70.enveloppe_db(y, SR)
    e2 = h70.enveloppe_db(3.0 * y, SR)
    assert_near(float(np.max(np.abs(e1 - e2))), 0.0, 1e-6, "un gain ne change pas l'enveloppe relative")
    assert_near(float(e1.min()), h70.PLANCHER_DB, 1e-9, "plancher")
    assert_near(float(e1.max()), 0.0, 1e-9, "maximum à 0 dB")


if __name__ == "__main__":
    raise SystemExit(run())
