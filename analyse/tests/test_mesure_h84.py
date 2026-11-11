"""L'instrument de H84 (CDC-reload § 45) sur des kicks CONNUS (la réplique du TR-808 de H69), avant toute mesure."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

import mesure_h69 as h69  # noqa: E402
import mesure_h84 as h84  # noqa: E402

SR = 44100


def k(accord, decroissance):
    return h69.replique_tr808(accord, decroissance, SR, duree=0.4)


@test
def test_h84_le_niveau_n_est_pas_juge_l_accord_et_la_longueur_le_sont():
    ref = k(45.0, 0.12)
    assert_equal(h84.ecart_tf(ref, ref, SR), 0.0)
    assert_true(h84.ecart_tf(ref, 0.1 * ref, SR) < 1e-6, "un gain ne change rien")
    court, aigu = h84.ecart_tf(ref, k(45.0, 0.05), SR), h84.ecart_tf(ref, k(80.0, 0.12), SR)
    assert_true(court > 2.0 and aigu > 2.0, f"longueur {court} · accord {aigu}")
    proche = h84.ecart_tf(ref, k(47.0, 0.13), SR)
    assert_true(proche < 0.5 * min(court, aigu), f"un voisin est plus près : {proche} contre {court} et {aigu}")


@test
def test_h84_le_bon_reglage_est_retrouve_dans_une_grille():
    ref = k(45.0, 0.12)
    grille = {(a, d): h84.ecart_tf(ref, k(a, d), SR) for a in (35.0, 45.0, 55.0, 80.0) for d in (0.05, 0.12, 0.32)}
    assert_equal(min(grille, key=lambda c: grille[c]), (45.0, 0.12))


@test
def test_h84_un_signal_sans_grave_est_un_refus():
    try:
        h84.ecart_tf(k(45.0, 0.12), np.zeros(1000), SR)
    except ValueError:
        pass
    else:
        raise AssertionError("un kick muet a été comparé")


if __name__ == "__main__":
    raise SystemExit(run())
