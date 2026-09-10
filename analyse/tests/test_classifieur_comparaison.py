"""Tests de `--comparer-a` (ROADMAP-apprentissage.md, A6) : le top 1 d'un modèle
plus large, ramené à la question d'un modèle plus étroit.

Deux top 1 ne se comparent que s'ils répondent à la même question. Ce qui se
teste ici est la RÈGLE du calcul, sur des probabilités écrites à la main : un
exemple qu'une machine nouvelle attire est juste pour la question de l'ancien
modèle et faux pour celle du nouveau ; les machines que l'ancien modèle ne
connaissait pas sortent du compte.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from classifieur import top1_restreint  # noqa: E402

NOMS = ["vsm.a", "vsm.b", "vsm.c"]   # vsm.c : la nouvelle venue


@test
def une_machine_nouvelle_qui_attire_un_exemple_ne_le_rend_faux_que_pour_la_question_neuve():
    probabilites = np.array([
        [0.2, 0.1, 0.7],   # vraie a : attirée par c -> juste parmi {a, b}, faux parmi les trois
        [0.1, 0.6, 0.3],   # vraie b : juste des deux côtés
        [0.5, 0.4, 0.1],   # vraie b : faux des deux côtés
        [0.1, 0.1, 0.8],   # vraie c : hors du compte, l'ancien modèle ne la connaissait pas
    ])
    vraies = np.array([0, 1, 1, 2])
    restreint, complet, n = top1_restreint(probabilites, vraies, NOMS, ["vsm.a", "vsm.b"])
    assert_equal(n, 3)
    assert_true(abs(restreint - 2 / 3) < 1e-12, f"restreint {restreint}")
    assert_true(abs(complet - 1 / 3) < 1e-12, f"complet {complet}")


@test
def sans_machine_commune_la_comparaison_rend_nan_et_zero_exemple():
    probabilites = np.array([[0.5, 0.3, 0.2]])
    restreint, complet, n = top1_restreint(probabilites, np.array([0]), NOMS, ["vsm.z"])
    assert_equal(n, 0)
    assert_true(math.isnan(restreint) and math.isnan(complet), "NaN attendu")


@test
def une_machine_commune_sans_exemple_d_epreuve_ne_compte_pas_pour_juste():
    probabilites = np.array([[0.6, 0.3, 0.1], [0.2, 0.7, 0.1]])
    restreint, complet, n = top1_restreint(probabilites, np.array([0, 1]), NOMS, ["vsm.c"])
    assert_equal(n, 0)
    assert_true(math.isnan(restreint) and math.isnan(complet), "NaN attendu")
