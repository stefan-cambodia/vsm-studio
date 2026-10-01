"""H54 — le jugement des attendus du § 16, sur des mesures écrites à la main.

Ce que le fichier garde : un extrait où l'instrument est aveugle (attendu 1) ne juge
RIEN, même quand son attendu 2 « tient » ; l'attendu 3 compare les seuls extraits qui
jugent, et des verdicts opposés le font échouer.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Dict

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, test  # noqa: E402

import mesure_h54 as h  # noqa: E402


def _extrait(debut: float, a1: str, v2: str) -> Dict[str, Any]:
    return {"debut_s": debut, "fin_s": debut + 24, "attendu_1": a1,
            "controle": {"cercles_par_tirage": [9, 9, 9] if a1 == "TENU" else [2, 2, 2]},
            "verdict_h53": [(2, v2, "détail"), (3, "TENU", "r"), (4, "TENU", "d"), (5, "TENU", "s")]}


def _issues(*extraits: Dict[str, Any]) -> Dict[str, str]:
    return {n: v for n, v, _ in h.juger({"extraits": list(extraits)})}


@test
def h54_un_extrait_aveugle_ne_juge_rien():
    issues = _issues(_extrait(16, "ÉCHEC", "TENU"), _extrait(130, "TENU", "ÉCHEC"), _extrait(272, "TENU", "ÉCHEC"))
    assert_equal(issues["2 (16-40 s)"], "NE JUGE PAS")
    assert_equal(issues["3"], "TENU")


@test
def h54_des_verdicts_opposes_font_echouer_l_attendu_3():
    issues = _issues(_extrait(16, "TENU", "TENU"), _extrait(130, "TENU", "ÉCHEC"), _extrait(272, "ÉCHEC", "ÉCHEC"))
    assert_equal(issues["3"], "ÉCHEC")


@test
def h54_les_attendus_du_15_ne_se_lisent_que_sur_un_cercle():
    issues = _issues(_extrait(16, "TENU", "TENU"), _extrait(130, "TENU", "ÉCHEC"))
    assert_equal(issues.get("4 (16-40 s, attendu 3 du § 15)"), "TENU")
    assert_equal("4 (130-154 s, attendu 3 du § 15)" in issues, False)


@test
def h54_un_seul_extrait_qui_juge_ne_compare_rien():
    issues = _issues(_extrait(16, "TENU", "TENU"), _extrait(130, "ÉCHEC", "TENU"), _extrait(272, "ÉCHEC", "TENU"))
    assert_equal(issues["3"], "SANS OBJET")
