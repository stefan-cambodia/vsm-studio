"""H64 — chaque pièce d'une batterie calée sur la bande qu'elle domine, sous une garde (CDC-reload § 25).

Les cas sont faits de main, une énergie par bande des sept de `BANDES` : ce qu'ils verrouillent est la RÈGLE — la
bande dominée, le facteur qui égale la somme au stem sur elle, et la garde qui refuse en le disant.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, test  # noqa: E402

from analyzer.vsm_calage_bandes import facteurs_par_bande  # noqa: E402


def _par_nom(facteurs):
    return {f.nom: f for f in facteurs}


@test
def un_charleston_trop_bas_monte_jusqu_au_stem_sur_ses_aigus():
    # Le charleston porte seul les deux bandes aiguës, dix fois trop bas en énergie ; le kick porte le grave, juste.
    stem = [1.0, 10.0, 1.0, 0.1, 0.1, 10.0, 10.0]
    pieces = {"charleston": [0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0],
              "kick": [1.0, 10.0, 1.0, 0.1, 0.1, 0.0, 0.0]}
    f = _par_nom(facteurs_par_bande(stem, pieces))
    assert_near(f["charleston"].facteur, math.sqrt(10.0), 1e-9)
    assert_equal(f["charleston"].bandes, (5, 6))
    assert_near(f["kick"].facteur, 1.0, 1e-9, "le kick est déjà au stem")


@test
def la_garde_refuse_un_kick_dont_le_medium_s_eloignerait_et_le_dit():
    # Le kick domine le sub (huit fois trop bas) mais porte AUSSI la moitié d'un bas-médium déjà 6 dB trop fort :
    # le monter de × 2,8 pour son sub éloignerait ce bas-médium bien au-delà de la garde de 3 dB.
    stem = [8.0, 8.0, 1.0, 1.0, 1.0, 1.0, 1.0]
    pieces = {"kick": [1.0, 1.0, 2.0, 0.0, 0.0, 0.0, 0.0],
              "caisse": [0.0, 0.0, 2.0, 1.0, 1.0, 1.0, 1.0]}
    f = _par_nom(facteurs_par_bande(stem, pieces))
    assert_near(f["kick"].facteur, 1.0, 1e-9)
    assert_true(f["kick"].raison.startswith("REFUSÉ"), f["kick"].raison)
    assert_true("150-500" in f["kick"].raison, f["kick"].raison)


@test
def une_piece_qui_ne_domine_rien_garde_son_niveau_et_le_dit():
    stem = [1.0] * 7
    pieces = {"a": [0.5] * 7, "b": [0.5] * 7}
    for facteur in facteurs_par_bande(stem, pieces):
        assert_near(facteur.facteur, 1.0, 1e-9)
        assert_true("ne domine aucune bande" in facteur.raison, facteur.raison)


@test
def le_facteur_est_borne_a_douze_decibels():
    stem = [0.0, 0.0, 0.0, 0.0, 0.0, 1000.0, 0.0]
    pieces = {"charleston": [0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0]}
    f = facteurs_par_bande(stem, pieces)[0]
    assert_near(f.facteur, 4.0, 1e-9)
    assert_true("(borné)" in f.raison, f.raison)
