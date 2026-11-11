"""L'instrument de H82 (CDC-reload § 43) sur des cas connus, avant toute mesure."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

import mesure_h82 as h82  # noqa: E402


@test
def test_h82_le_gain_porte_le_solo_a_l_energie_du_stem():
    x = np.random.default_rng(1).standard_normal(4000)
    g = h82.gain_d_energie(x, 0.25 * x)
    assert_true(g is not None and abs(g - 4.0) < 1e-9, f"gain {g}")
    assert_equal(h82.gain_d_energie(x, np.zeros(4000)), None)
    assert_equal(h82.gain_d_energie(x, 1e-4 * x), None)          # −80 dB : muet, pas ×10 000
    try:
        h82.gain_d_energie(np.zeros(4000), x)
    except ValueError:
        pass
    else:
        raise AssertionError("un stem muet a rendu un gain")


@test
def test_h82_le_choix_veut_un_demi_decibel():
    assert_equal(h82.choisir(9.0, {"a": 8.6, "b": 8.9}), None)
    assert_equal(h82.choisir(9.0, {"a": 8.5, "b": 8.9}), "a")
    assert_equal(h82.choisir(9.0, {"b": 8.0, "a": 8.0}), "a")
    assert_equal(h82.choisir(9.0, {}), None)


if __name__ == "__main__":
    raise SystemExit(run())
