"""L'instrument de H94 (CDC-reload § 55) sur une frappe CONNUE, avant toute mesure."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_true, run, test  # noqa: E402

import mesure_h94 as h94  # noqa: E402

SR, DEBUT = 44100, 0.010
T = np.arange(int(0.310 * SR)) / SR - DEBUT


def frappe(queue_db):
    """40 Hz de 0 à 100 ms ; puis 100 Hz de 100 à 300 ms, `queue_db` sous la tête."""
    return np.where((T >= 0) & (T < 0.1), np.sin(2 * np.pi * 40 * T), 0.0) + \
        np.where(T >= 0.1, 10 ** (queue_db / 20) * np.sin(2 * np.pi * 100 * T), 0.0)


@test
def test_h94_la_tete_est_au_sub_la_queue_a_la_basse():
    p = h94.profil(frappe(-12.0), SR, DEBUT)
    assert_true(p["sub"][0] - p["basse"][0] > 10.0, f"tête : {p}")
    assert_true(p["basse"][1] - p["sub"][1] > 3.0, f"queue : {p}")
    # la queue à 100 Hz, 12 dB sous la tête à 40 Hz : retrouvée à 1,5 dB (bande contre bande)
    assert_true(abs((p["basse"][1] - p["sub"][0]) - (-12.0)) < 1.5, f"niveau de la queue : {p}")
    assert_true(abs(p["basse"][2] - p["basse"][1]) < 1.0, f"la queue tient sur les deux tranches : {p}")


@test
def test_h94_ce_que_la_tete_bave_dans_la_tranche_suivante():
    seule = h94.profil(frappe(-200.0), SR, DEBUT)      # pas de queue : ce qui reste à 100-200 ms est de la bavure
    assert_true(seule["sub"][1] < seule["sub"][0] - 6.0, f"le sub bave, mais de moins que cela : {seule}")
    assert_true(seule["basse"][1] < seule["sub"][0] - 20.0, f"et la basse presque pas : {seule}")
    assert_true(seule["sub"][2] < seule["sub"][0] - 25.0, f"à 200-300 ms il ne reste rien : {seule}")


@test
def test_h94_une_frappe_trop_courte_est_un_refus():
    try:
        h94.profil(frappe(-12.0)[:int(0.2 * SR)], SR, DEBUT)
    except ValueError:
        pass
    else:
        raise AssertionError("une tranche hors du signal a été lue")


if __name__ == "__main__":
    raise SystemExit(run())
