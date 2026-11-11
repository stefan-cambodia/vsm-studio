"""L'instrument de H81 (CDC-reload § 42) sur des signaux CONNUS, avant toute mesure.

Un kick court (0,06 s) toutes les 0,5 s et une « basse » — un sinus de 60 Hz tenu. Aux frappes du kick, c'est le kick
qui porte le grave ; aux instants d'entre les kicks, c'est la basse. L'instrument doit le dire, sommer juste, et
refuser une somme à laquelle il manque une partie.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

import mesure_h80 as h80  # noqa: E402
import mesure_h81 as h81  # noqa: E402
from test_mesure_h74 import DUREE, SR, grille  # noqa: E402
from test_mesure_h80 import kick  # noqa: E402


def scene():
    g = grille()
    k = kick(g["kick"], 0.06)
    basse = 0.05 * np.sin(2 * np.pi * 60.0 * np.arange(DUREE * SR) / SR)
    aigu = 0.02 * np.sin(2 * np.pi * 3000.0 * np.arange(DUREE * SR) / SR)
    populations = {"aux kicks": g["kick"], "entre les kicks": h80.frappes_hors(g["charleston"], g["kick"])}
    return {"kick": k, "basse": basse, "aigu": aigu, "mélange": k + basse + aigu}, populations


@test
def test_h81_qui_porte_le_grave_depend_de_la_population():
    ondes, populations = scene()
    e = h81.energies(ondes, populations, [0], SR)
    r = h81.rapport_db(e, "kick", "basse")
    assert_true(r["aux kicks"]["30-150"] > 6.0, f"aux kicks, le kick porte : {r}")
    assert_true(r["entre les kicks"]["30-150"] < -6.0, f"entre les kicks, la basse porte : {r}")


@test
def test_h81_la_somme_des_parties_rend_le_melange_et_une_partie_manquante_se_voit():
    ondes, populations = scene()
    ondes["Σ juste"] = ondes["kick"] + ondes["basse"] + ondes["aigu"]
    ondes["Σ sans la basse"] = ondes["kick"] + ondes["aigu"]
    e = h81.energies(ondes, populations, [0, 3], SR)
    assert_equal(h81.controle_de_somme(h81.rapport_db(e, "Σ juste", "mélange")), True)
    manque = h81.rapport_db(e, "Σ sans la basse", "mélange")
    assert_equal(h81.controle_de_somme(manque), False)
    assert_true(manque["entre les kicks"]["30-150"] < -6.0, f"c'est entre les kicks que la basse manque : {manque}")
    assert_true(abs(manque["entre les kicks"]["2000-4000"]) < 0.5, f"et pas dans l'aigu : {manque}")


@test
def test_h81_un_signal_muet_ne_rend_pas_un_zero():
    ondes, populations = scene()
    ondes["muet"] = np.zeros_like(ondes["kick"])
    e = h81.energies(ondes, populations, [0], SR)
    r = h81.rapport_db(e, "muet", "mélange")
    assert_equal(r["aux kicks"]["30-150"], None)
    assert_equal(h81.controle_de_somme(r), False)
    assert_equal(h81.controle_de_somme({}), False)


@test
def test_h81_une_population_vide_est_un_refus():
    ondes, populations = scene()
    try:
        h81.energies(ondes, {**populations, "vide": []}, [0], SR)
    except ValueError as e:
        assert_true("vide" in str(e), str(e))
    else:
        raise AssertionError("une population vide a été mesurée")


if __name__ == "__main__":
    raise SystemExit(run())
