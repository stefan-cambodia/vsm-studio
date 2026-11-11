"""L'instrument de H91 (CDC-reload § 52) sur des signaux connus, avant toute mesure."""

from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_true, run, test  # noqa: E402

import mesure_h91 as h91  # noqa: E402

SR = 44100
T = np.arange(10 * SR) / SR


def db(a, b):
    return 10 * math.log10(a / b)


@test
def test_h91_une_note_est_dans_sa_bande_et_pas_dans_l_autre():
    x = np.sin(2 * np.pi * 40.0 * T)
    inst = [2.0, 3.0, 4.0]
    sub, basse = h91.energie_fenetres(x, SR, inst, (20, 60)), h91.energie_fenetres(x, SR, inst, (60, 150))
    assert_true(db(sub, basse) > 20.0, f"40 Hz : sub {sub}, basse {basse}")
    assert_true(abs(db(h91.energie_fenetres(2 * x, SR, inst, (20, 60)), sub) - 6.02) < 0.01, "×2 → +6,02 dB")
    assert_true(abs(sub - 0.5 * 0.110 * SR) / sub < 0.05, f"un sinus d'amplitude 1 sur 110 ms : {sub}")


@test
def test_h91_la_fenetre_lit_son_instant_et_pas_un_autre():
    x = np.where((T > 5.0) & (T < 5.1), np.sin(2 * np.pi * 100.0 * T), 0.0)
    dedans, dehors = h91.energie_fenetres(x, SR, [5.0], (60, 150)), h91.energie_fenetres(x, SR, [7.0], (60, 150))
    assert_true(db(dedans, max(dehors, 1e-30)) > 40.0, f"{dedans} contre {dehors}")
    try:
        h91.energie_fenetres(x, SR, [0.001, 9.99], (60, 150))
    except ValueError:
        pass
    else:
        raise AssertionError("des fenêtres hors du signal ont été lues")


if __name__ == "__main__":
    raise SystemExit(run())
