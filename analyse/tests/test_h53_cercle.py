"""H53 — l'instrument qui lit la STRUCTURE du mouvement (docs/CDC-reload-indifferenciable.md § 15).

C'est l'ATTENDU 1 du § 15, écrit avant toute mesure sur l'original. Cinq hauteurs de
l'accord, un bruit à −30 dB, trois structures :

  - UN DIRECT PLUS UNE LECTURE (dosage 0,47 ; retard de 1,8 ms ± 0,85 à `f1` ± 0,3 à
    `f2`) : chaque raie décrit un cercle — circularité ≤ 5 % —, le dosage se relit à
    ± 0,05 et l'amplitude du retard à `f1` à ± 10 % ;
  - DEUX ÉTAGES EN SÉRIE (les réglages de S trouvés par H51) : le produit de deux
    cercles n'en est pas un — circularité > 15 % sur les raies du haut ;
  - UN TRÉMOLO : E(t) va le long d'une droite — pas un cercle (circularité > 15 %, ou un
    arc de moins de 60°).

Et les deux précautions que l'en-tête de l'outil dit indispensables, chacune avec son
cas : une porteuse décalée de 0,05 Hz du tempéré (sans l'affinage, le cercle devient
un anneau), et le trémolo, qu'un critère de circularité SANS l'arc prendrait pour un
cercle immense.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Callable, Dict, List

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

import mesure_h47 as h47  # noqa: E402
import mesure_h53 as h  # noqa: E402

SR = 11025
SECONDES = 24.0
NOTES = (52, 59, 64, 70, 73)             # mi3, si3, mi4, la♯4, do♯5 : de 166 à 558 Hz
HAUT = (70, 73)                          # « les raies du haut » de l'attendu 1
DOSAGE = 0.47
BASE_S, F1_S, F2_S = 1.8e-3, 0.85e-3, 0.3e-3


def _t() -> np.ndarray:
    return np.arange(int(SECONDES * SR)) / SR


def _bruit(graine: int, n: int) -> np.ndarray:
    return np.random.default_rng(graine).standard_normal(n) * np.sqrt(0.5 * 10 ** (-30 / 10))


def _retard(t: np.ndarray, base: float, a1: float, a2: float) -> np.ndarray:
    return base + a1 * np.sin(2 * np.pi * h.F1_HZ * t) + a2 * np.sin(2 * np.pi * h.F2_HZ * t + 0.7)


def _une_lecture(decalage_hz: float = 0.0) -> np.ndarray:
    t = _t()
    tau = _retard(t, BASE_S, F1_S, F2_S)
    y = np.zeros_like(t)
    for note in NOTES:
        f = h47.hz_de(note, h.LA4_HZ) + decalage_hz
        y += (1 - DOSAGE) * np.sin(2 * np.pi * f * t) + DOSAGE * np.sin(2 * np.pi * f * (t - tau))
    return y + _bruit(31, len(t))


def _serie() -> np.ndarray:
    """Deux étages : s2(t) = (1 − m2)·s1(t) + m2·s1(t − τ2(t)), chacun d'un direct et d'une lecture."""
    t = _t()
    m1, m2 = 0.47, 0.41
    tau2 = 6.81e-3 + 1.10e-3 * np.sin(2 * np.pi * h.F2_HZ * t)

    def etage1(f: float, u: np.ndarray) -> np.ndarray:
        retard = 1.81e-3 + 1.68e-3 * np.sin(2 * np.pi * h.F1_HZ * u)
        return (1 - m1) * np.sin(2 * np.pi * f * u) + m1 * np.sin(2 * np.pi * f * (u - retard))

    y = np.zeros_like(t)
    for note in NOTES:
        f = h47.hz_de(note, h.LA4_HZ)
        y += (1 - m2) * etage1(f, t) + m2 * etage1(f, t - tau2)
    return y + _bruit(32, len(t))


def _tremolo(cadence: float) -> np.ndarray:
    t = _t()
    y = np.zeros_like(t)
    for note in NOTES:
        y += (1 + 0.6 * np.sin(2 * np.pi * cadence * t)) * np.sin(2 * np.pi * h47.hz_de(note, h.LA4_HZ) * t)
    return y + _bruit(33, len(t))


_MEMO: Dict[str, List[Dict[str, Any]]] = {}


def _raies(nom: str, fabrique: Callable[[], np.ndarray]) -> List[Dict[str, Any]]:
    if nom not in _MEMO:
        _MEMO[nom] = h.analyser(fabrique(), SR, NOTES)
    return _MEMO[nom]


def _decrire(raies: List[Dict[str, Any]]) -> str:
    return " ; ".join(
        f"{r['note']}: vue={r['vue']} circ={r.get('circularite')} arc={r.get('arc_deg')} "
        f"aplati={r.get('aplatissement')} dosage={r.get('dosage')} f1={(r.get('retard_ms') or {}).get('f1')}" for r in raies)


@test
def h53_une_lecture_decrit_un_cercle_et_s_y_lit():
    raies = _raies("une", _une_lecture)
    assert_equal(len(raies), len(NOTES))
    for r in raies:
        assert_true(r["vue"], f"raie {r['note']} non vue : {_decrire(raies)}")
        assert_true(r["circularite"] is not None and r["circularite"] <= 0.05,
                    f"raie {r['note']} : circularité {r.get('circularite')} > 5 % — {_decrire(raies)}")
        assert_true(r["cercle"], f"raie {r['note']} non jugée cercle — {_decrire(raies)}")
        assert_true(abs(r["dosage"] - DOSAGE) <= 0.05, f"raie {r['note']} : dosage {r['dosage']} pour {DOSAGE}")
        relu = r["retard_ms"]["f1"]
        assert_true(abs(relu - F1_S * 1000) <= 0.10 * F1_S * 1000,
                    f"raie {r['note']} : retard à f1 relu {relu} ms pour {F1_S * 1000} ms")


@test
def h53_une_porteuse_decalee_reste_un_cercle():
    """La précision 1 de l'outil : 0,05 Hz d'écart au tempéré feraient tourner la figure
    d'un tour en vingt secondes. Avec l'affinage, chaque raie reste un cercle."""
    raies = _raies("decalee", lambda: _une_lecture(decalage_hz=0.05))
    for r in raies:
        assert_true(r["vue"] and r["circularite"] is not None and r["circularite"] <= 0.05,
                    f"raie {r['note']} : circularité {r.get('circularite')} — {_decrire(raies)}")
        assert_true(abs(r["dosage"] - DOSAGE) <= 0.05, f"raie {r['note']} : dosage {r['dosage']}")


@test
def h53_deux_etages_en_serie_ne_font_pas_un_cercle():
    raies = {r["note"]: r for r in _raies("serie", _serie)}
    for note in HAUT:
        r = raies[note]
        assert_true(r["vue"], f"raie {note} non vue : {_decrire(list(raies.values()))}")
        assert_true(r["circularite"] is not None and r["circularite"] > 0.15,
                    f"raie {note} : circularité {r.get('circularite')} ≤ 15 % — la série passerait pour une lecture ; "
                    f"{_decrire(list(raies.values()))}")
        assert_true(not r["cercle"], f"raie {note} jugée cercle")


def _pas_un_cercle(raies: List[Dict[str, Any]]) -> None:
    for r in raies:
        assert_true(r["vue"], f"raie {r['note']} non vue")
        pas_un_cercle = (r["circularite"] is None or r["circularite"] > 0.15 or r["arc_deg"] < h.ARC_MIN_DEG
                         or r["aplatissement"] < h.APLATI_MIN)
        assert_true(pas_un_cercle, f"raie {r['note']} : circularité {r.get('circularite')}, arc {r.get('arc_deg')}°, "
                                   f"aplatissement {r.get('aplatissement')}")
        assert_true(not r["cercle"], f"raie {r['note']} jugée cercle — {_decrire(raies)}")


@test
def h53_un_tremolo_n_est_pas_un_cercle():
    _pas_un_cercle(_raies("tremolo-5", lambda: _tremolo(5.0)))


@test
def h53_un_tremolo_a_la_cadence_f1_n_est_pas_un_cercle():
    """La cadence qui compte sur l'original. Sans l'aplatissement, la♯4 y était « CERCLE »
    (circularité 9,9 %, arc 83°) : un segment parcouru en sinus passe ses instants aux deux
    bouts, qui tombent sur le cercle dont il est la corde."""
    _pas_un_cercle(_raies("tremolo-f1", lambda: _tremolo(h.F1_HZ)))


@test
def h53_le_verdict_suit_ce_qui_est_ecrit():
    def raie(cercle: bool, circ: float, arc: float, f1: float = 0.85, dosage: float = 0.47) -> Dict[str, Any]:
        return {"note": 60, "nominal_hz": 260.0, "vue": True, "cercle": cercle, "circularite": circ,
                "arc_deg": arc, "aplatissement": 0.5 if cercle else 0.01, "dosage": dosage,
                "retard_ms": {"f1": f1, "2f1": 0.02 * f1, "f2": 0.3, "2f2": 0.01, "crete_a_crete": 2.0}}

    tenu = {int(n): v for n, v, _ in h.verdict({"raies": [raie(True, 0.03, 300.0) for _ in range(9)]})}
    assert_equal(tenu, {2: "TENU", 3: "TENU", 4: "TENU", 5: "TENU"})
    echec = {int(n): v for n, v, _ in h.verdict({"raies": [raie(False, 0.4, 20.0) for _ in range(6)]})}
    assert_equal(echec, {2: "ÉCHEC", 3: "SANS OBJET", 4: "SANS OBJET", 5: "SANS OBJET"})
    # Des retards par note — trois cercles à 0,4, 0,85 et 1,6 ms : la modulation n'est pas sur le bus.
    disperses = [raie(True, 0.03, 300.0, f1=v) for v in (0.4, 0.85, 1.6, 0.4, 0.85, 1.6, 0.4, 0.85)]
    par_note = {int(n): v for n, v, _ in h.verdict({"raies": disperses})}
    assert_equal(par_note[2], "TENU")
    assert_equal(par_note[3], "ÉCHEC")
