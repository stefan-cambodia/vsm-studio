"""H49 — l'instrument qui décrit les raies du pad (docs/CDC-reload-indifferenciable.md § 11).

C'est l'ATTENDU 1 du § 11, écrit avant toute mesure sur « Reload » : sur des signaux
fabriqués, sous un bruit à −30 dB, l'instrument doit lire

  - un sinus seul : UNE composante, pas de jupe, pas de mouvement ;
  - trois sinus à −7, 0, +7 cents : TROIS composantes, les mêmes écarts en cents à
    toutes les hauteurs — et la forme « désaccord » ;
  - un sinus à trémolo de 5 Hz : des bandes latérales à ± 5 Hz à toutes les hauteurs,
    la cadence de modulation à 5 Hz — et la forme « cadence fixe » ;
  - une raie noyée dans le fond : « non vue », jamais comptée ;
  - un partiel tenu, et pas une frappe qui ne dure qu'un tiers de l'extrait.

Et le verdict, sur des mesures fabriquées, rend chacune de ses issues.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Dict, List

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, test  # noqa: E402

import mesure_h49 as h  # noqa: E402

SR = 11025
SECONDES = 24.0
HAUTEURS = (186.29, 333.76, 497.34)      # trois raies de l'oracle de H47


def _t() -> np.ndarray:
    return np.arange(int(SECONDES * SR)) / SR


def _bruit(graine: int, niveau_db: float = -30.0) -> np.ndarray:
    # −30 dB sous un sinus d'amplitude 1 (puissance 0,5)
    return np.random.default_rng(graine).standard_normal(len(_t())) * np.sqrt(0.5 * 10 ** (niveau_db / 10))


def _seuls() -> np.ndarray:
    t = _t()
    return sum(np.sin(2 * np.pi * f * t) for f in HAUTEURS) + _bruit(1)


def _desaccordes() -> np.ndarray:
    t = _t()
    y = np.zeros_like(t)
    for f in HAUTEURS:
        for cents, amplitude in ((-7.0, 0.7), (0.0, 1.0), (7.0, 0.6)):
            y += amplitude * np.sin(2 * np.pi * f * 2 ** (cents / 1200) * t + f)
    return y + _bruit(2)


def _tremolo() -> np.ndarray:
    t = _t()
    y = np.zeros_like(t)
    for f in HAUTEURS:
        y += (1 + 0.5 * np.sin(2 * np.pi * 5.0 * t)) * np.sin(2 * np.pi * f * t)
    return y + _bruit(3)


@test
def h49_un_sinus_seul_est_une_composante_sans_jupe_ni_mouvement():
    for r in h.decrire(_seuls(), SR, HAUTEURS):
        assert_true(r["vue"], f"la raie se voit : {r['rapport_au_fond_db']} dB")
        assert_equal(len(r["composantes"]), 1, f"une seule composante à {r['hz']} Hz")
        assert_true(r["part_jupe"] < 2.0, f"pas de jupe : {r['part_jupe']} %")
        assert_true(r["am_ecart_type_db"] < 0.5, f"pas de modulation d'amplitude : {r['am_ecart_type_db']} dB")
        assert_true(r["fm_ecart_type_cents"] < 1.0, f"pas de modulation de fréquence : {r['fm_ecart_type_cents']} c")
    assert_equal(h.forme_du_mouvement(h.decrire(_seuls(), SR, HAUTEURS))["forme"], "non mesurable",
                 "aucune composante secondaire : la forme ne se lit pas")


@test
def h49_trois_sinus_desaccordes_sont_trois_composantes_aux_memes_cents():
    raies = h.decrire(_desaccordes(), SR, HAUTEURS)
    for r in raies:
        assert_equal(len(r["composantes"]), 3, f"trois composantes à {r['hz']} Hz")
        ecarts = sorted(c["ecart_cents"] for c in r["composantes"])
        assert_near(ecarts[0], -7.0, 1.0, "la composante du bas")
        assert_near(ecarts[2], 7.0, 1.0, "la composante du haut")
        assert_near(abs(r["composantes"][1]["ecart_cents"]), 7.0, 1.0, "la secondaire la plus forte")
        assert_true(r["am_etendue_db"] > 6.0, f"des battements : {r['am_etendue_db']} dB d'étendue")
    forme = h.forme_du_mouvement(raies)
    assert_equal(forme["forme"], "désaccord", f"mêmes écarts en cents : {forme}")


@test
def h49_un_tremolo_donne_les_memes_bandes_laterales_en_hertz():
    raies = h.decrire(_tremolo(), SR, HAUTEURS)
    for r in raies:
        assert_equal(len(r["composantes"]), 3, f"la porteuse et ses deux bandes latérales à {r['hz']} Hz")
        assert_near(abs(r["composantes"][1]["ecart_hz"]), 5.0, 0.2, "bande latérale à 5 Hz")
        assert_near(r["am_cadence_hz"], 5.0, 0.1, "la cadence de la modulation")
    forme = h.forme_du_mouvement(raies)
    assert_equal(forme["forme"], "cadence fixe", f"mêmes écarts en hertz : {forme}")


@test
def h49_une_raie_noyee_dans_le_fond_n_est_pas_vue():
    t = _t()
    y = 0.02 * np.sin(2 * np.pi * 333.76 * t) + np.random.default_rng(4).standard_normal(len(t))
    raie = h.decrire(y, SR, [333.76])[0]
    assert_true(not raie["vue"], f"rapport au fond {raie['rapport_au_fond_db']} dB : la raie ne se juge pas")


@test
def h49_un_partiel_tenu_est_garde_une_frappe_non():
    t = _t()
    y = np.sin(2 * np.pi * 372.66 * t) + 10 ** (-25 / 20) * np.sin(2 * np.pi * 1200.0 * t)
    frappe = 10 ** (-10 / 20) * np.sin(2 * np.pi * 2500.0 * t)
    frappe[len(t) // 3:] = 0.0
    y = y + frappe + _bruit(5, -60.0)
    partiels = h.partiels_tenus(y, SR, h.niveau_de_reference(y, SR, 372.66))
    assert_equal([round(p["hz"]) for p in partiels], [1200], "le partiel tenu, et lui seul")
    assert_near(partiels[0]["db"], -25.0, 1.0, "à son niveau")


def _raie(jupe: float, composantes: int, cadence: float = 1.3, ecart_cents: float = 7.0, hz: float = 300.0,
          vue: bool = True) -> Dict[str, Any]:
    comps: List[Dict[str, float]] = [{"hz": hz, "db": 0.0, "ecart_hz": 0.0, "ecart_cents": 0.0}]
    for i in range(1, composantes):
        e = hz * (2 ** (ecart_cents * i / 1200) - 1)
        comps.append({"hz": hz + e, "db": -3.0 * i, "ecart_hz": e, "ecart_cents": ecart_cents * i})
    return {"hz": hz, "vue": vue, "rapport_au_fond_db": 25.0 if vue else 4.0, "part_jupe": jupe,
            "composantes": comps, "am_cadence_hz": cadence, "niveau_db": 0.0}


def _mesure(original: List[Dict[str, Any]], stem: List[Dict[str, Any]], partiels=None) -> Dict[str, Any]:
    return {"original": original, "stem": stem, "partiels_original": partiels or []}


def _par_numero(mesure: Dict[str, Any]) -> Dict[int, str]:
    return {numero: v for numero, v, _detail in h.verdict(mesure)}


@test
def h49_le_verdict_rend_ses_issues():
    hauteurs = (166.0, 186.0, 235.0, 249.0, 279.0, 334.0, 373.0, 470.0, 497.0, 555.0)
    original = [_raie(20.0, 3, hz=f) for f in hauteurs]
    v = _par_numero(_mesure(original, [_raie(22.0, 3, hz=f) for f in hauteurs]))
    assert_equal((v[2], v[3], v[4], v[5], v[6]), ("TENU", "TENU", "TENU (désaccord)", "TENU", "TENU"))

    v = _par_numero(_mesure(original, [_raie(45.0, 3, hz=f) for f in hauteurs]))
    assert_equal(v[2], "ÉCHEC", "le stem a 25 points de jupe de plus : la séparation déforme")
    v = _par_numero(_mesure(original, [_raie(30.0, 3, hz=f) for f in hauteurs]))
    assert_equal(v[2], "ENTRE LES DEUX")

    continues = [_raie(20.0, 1, hz=f) for f in hauteurs]
    v = _par_numero(_mesure(continues, continues))
    assert_equal((v[3], v[4]), ("ÉCHEC", "NON MESURABLE"), "aucune raie résolue : continu, et la forme ne se lit pas")

    # mêmes écarts en HERTZ : l'écart en cents est inversement proportionnel à la hauteur
    fixes = [_raie(20.0, 2, hz=f, ecart_cents=1200 * np.log2((f + 2.3) / f)) for f in hauteurs]
    assert_equal(_par_numero(_mesure(fixes, fixes))[4], "ÉCHEC (cadence fixe)")

    pompees = [_raie(20.0, 3, hz=f, cadence=2.31) for f in hauteurs]
    assert_equal(_par_numero(_mesure(pompees, pompees))[6], "ÉCHEC", "dix raies au temps : un pompage")
    assert_equal(_par_numero(_mesure(original, original, [{"hz": 745.0, "db": -12.0}]))[5], "ÉCHEC")
    assert_equal(_par_numero(_mesure(original, original, [{"hz": 745.0, "db": -34.0}]))[5], "TENU")

    cachees = [_raie(20.0, 3, hz=f, vue=False) for f in hauteurs]
    assert_equal(set(_par_numero(_mesure(cachees, cachees)).values()), {"NON MESURABLE"},
                 "aucune raie vue : rien ne se juge")
