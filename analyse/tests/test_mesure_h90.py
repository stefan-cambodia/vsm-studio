"""L'instrument de H90 (CDC-reload § 51) sur des cas connus, avant toute mesure."""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

import mesure_h90 as h90  # noqa: E402


@test
def test_h90_apparier():
    assert_equal(list(h90.apparier([1.0, 1.04, 1.06, 5.0], [1.0, 3.0], 0.05)), [True, True, False, False])
    assert_equal(list(h90.apparier([1.0, 2.96], [3.0], 0.05)), [False, True])      # une seule note, avant comme après
    assert_equal(list(h90.apparier([0.5, 9.0], [1.0, 2.0, 3.0], 0.05)), [False, False])   # hors des bornes
    assert_equal(list(h90.apparier([1.0], [], 0.05)), [False])


@test
def test_h90_position_dans_le_temps():
    pas = 60.0 / 120.0 / 4.0    # 0,125 s
    assert_equal(h90.position_16e(10.0, 120.0, 10.0)[0], 0)
    assert_equal(h90.position_16e(10.0 + 2 * pas, 120.0, 10.0)[0], 2)
    assert_equal(h90.position_16e(10.0 - pas, 120.0, 10.0)[0], 3)                   # avant l'origine aussi
    k, e = h90.position_16e(10.0 + 5 * pas + 0.01, 120.0, 10.0)
    assert_true(k == 1 and abs(e - 0.01) < 1e-9, f"{k} {e}")


@test
def test_h90_suit_de_pres():
    assert_equal(list(h90.suit_de_pres([1.0, 1.1, 2.0, 2.2], 0.15)), [False, True, False, False])
    assert_equal(list(h90.suit_de_pres([], 0.15)), [])
    try:
        h90.suit_de_pres([2.0, 1.0], 0.15)
    except ValueError:
        pass
    else:
        raise AssertionError("des instants non triés ont été lus")


if __name__ == "__main__":
    raise SystemExit(run())
