"""H47 — l'instrument qui demande si le parc contient le pad de « Reload ».

docs/CDC-reload-indifferenciable.md § 9. Ces tests ne rendent rien par le moteur :
ils verrouillent l'INSTRUMENT sur des signaux dont la vérité est connue par
construction, et le VERDICT sur des mesures fabriquées — chaque issue vue, l'échec
compris (une garde qu'on n'a pas vue rouge n'est pas une garde) :

  - l'oracle retrouve l'accord du § 1 au diapason du morceau, ignore un partiel à
    plus de 20 dB sous le plus fort, et fond deux pics d'une même note ;
  - l'amplitude lue sur un pic est celle du sinus qui l'a produit ;
  - l'extrait contre lui-même rend zéro, et `B_mesuré` fait mieux que `B_égal` ;
  - la tenue sépare une note tenue d'une note frappée, et DIT qu'elle ne peut pas
    mesurer un silence ;
  - une machine brillante là où l'extrait se tait se voit à l'équilibre ;
  - le verdict rend « tenu », « entre les deux » et « échec » aux seuils écrits.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, test  # noqa: E402

import mesure_h47 as h  # noqa: E402

SR = 22050
SECONDES = 24.0
ACCORD = ((59, 1.0), (61, 0.8), (64, 0.6), (66, 0.5), (70, 0.3))   # si, do♯, mi, fa♯, la♯


def _t() -> np.ndarray:
    return np.arange(int(SECONDES * SR)) / SR


def _accord(amplitudes: bool = True) -> np.ndarray:
    t = _t()
    y = np.zeros_like(t)
    for note, amplitude in ACCORD:
        y += (amplitude if amplitudes else 1.0) * np.sin(2 * np.pi * h.hz_de(note, h.LA4_HZ) * t)
    return 0.1 * y


def _mesure(bandes: Dict[str, Optional[float]], logmel: float, tenue: Optional[float] = 0.0,
            d: float = 0.2) -> Dict[str, Any]:
    return {"machine": "vsm.essai", "profil": "", "D": d, "logmel": logmel, "bandes": bandes,
            "tenue_db": tenue, "ecartee": None}


def _fabriquee(premiere: Dict[str, Any], reglee: Dict[str, Any], rho: float = 0.8,
               classes: Optional[List[int]] = None) -> Dict[str, Any]:
    autres = [_mesure({"médium": 0.5}, 9.0, tenue=-40.0, d=0.3 + 0.01 * i) for i in range(4)]
    regle = dict(reglee, D_depart=0.2, evaluations=40)
    return {
        "oracle": {"classes": sorted(h.CLASSES_ATTENDUES) if classes is None else classes},
        "bornes": {"soi": {"logmel": 0.0, "D": 0.0}, "egale": {"logmel": 6.0}, "mesuree": {"logmel": 4.0}},
        "candidates": [premiere] + autres,
        "classement": [0, 1, 2, 3, 4],
        "reglage": [regle],
        "spearman": {"rho": rho, "n": 5, "distincts_D": 5, "distincts_logmel": 2, "rho_toutes": rho, "n_toutes": 5},
    }


def _par_numero(mesure: Dict[str, Any]) -> Dict[int, str]:
    return {numero: v for numero, v, _detail in h.verdict(mesure)}


@test
def h47_l_oracle_retrouve_l_accord_et_ignore_un_partiel_faible():
    y = _accord()
    # un partiel à −26 dB du plus fort (hors de la règle des 20 dB), et un souffle
    y += 0.1 * 0.05 * np.sin(2 * np.pi * h.hz_de(78, h.LA4_HZ) * _t())
    y += 1e-5 * np.random.default_rng(7).standard_normal(len(y))
    pics = h.pics_tenus(y, SR)
    notes = h.notes_de_l_oracle(pics, h.LA4_HZ)
    assert_equal(notes, [note for note, _a in ACCORD], "les cinq notes, aucune autre")
    assert_equal({note % 12 for note in notes}, set(h.CLASSES_ATTENDUES), "les cinq classes du § 1")
    for p, (_note, amplitude) in zip(pics, ACCORD, strict=True):
        assert_near(p["amplitude"], 0.1 * amplitude, 0.1 * amplitude * 0.03, "amplitude du sinus, à 3 %")


@test
def h47_deux_pics_sur_la_meme_note_n_en_font_qu_une():
    t = _t()
    f = h.hz_de(66, h.LA4_HZ)
    y = 0.1 * (np.sin(2 * np.pi * f * t) + 0.9 * np.sin(2 * np.pi * (f + 4.0) * t))
    pics = h.pics_tenus(y, SR)
    assert_equal(len(pics), 2, "deux pics")
    assert_equal(h.notes_de_l_oracle(pics, h.LA4_HZ), [66], "une seule note")


@test
def h47_un_extrait_trop_court_est_refuse():
    try:
        h.pics_tenus(np.zeros(SR), SR)
    except ValueError:
        return
    assert_true(False, "un extrait d'une seconde doit être refusé, pas lu comme « aucun pic »")


@test
def h47_l_extrait_contre_lui_meme_rend_zero_et_les_bornes_s_ordonnent():
    from analyzer.vsm_distance_cache import cached_distance_for

    extrait = _accord() + 1e-4 * np.random.default_rng(3).standard_normal(len(_t()))
    outil = h.outil_ecart()
    distance = cached_distance_for("v2")(np.asarray(extrait, dtype=np.float32), SR)
    pics = h.pics_tenus(extrait, SR)
    notes = h.notes_de_l_oracle(pics, h.LA4_HZ)
    soi = h.mesures_de(extrait, extrait, SR, outil, distance, 6.957)
    assert_equal(soi["logmel"], 0.0, "log-mel de l'extrait contre lui-même")
    assert_near(soi["D"], 0.0, 1e-9, "D de l'extrait contre lui-même")
    assert_true(all(e is None or e == 0.0 for e in soi["bandes"].values()), "aucun écart de bande")
    egale = h.mesures_de(extrait, h.borne_egale(notes, h.LA4_HZ, len(extrait), SR), SR, outil, distance, 6.957)
    mesuree = h.mesures_de(extrait, h.borne_mesuree(pics, len(extrait), SR), SR, outil, distance, 6.957)
    assert_true(mesuree["logmel"] < egale["logmel"],
                f"B_mesuré ({mesuree['logmel']}) doit faire mieux que B_égal ({egale['logmel']})")


@test
def h47_la_tenue_separe_tenu_et_frappe_et_dit_le_silence():
    t = _t()
    tenu = 0.1 * np.sin(2 * np.pi * 440 * t)
    frappe = tenu * np.exp(-t / 1.0)
    assert_near(h.tenue_db(tenu), 0.0, 0.05, "une note tenue")
    assert_true(h.tenue_db(frappe) < -60.0, f"une note frappée : {h.tenue_db(frappe)} dB")
    assert_equal(h.tenue_db(np.zeros(len(t))), None, "un silence ne se mesure pas")


@test
def h47_une_machine_brillante_la_ou_l_extrait_se_tait_se_voit():
    extrait = _accord()
    outil = h.outil_ecart()
    brillante = extrait + 0.03 * np.sin(2 * np.pi * 8000 * _t())
    seule = h.equilibre(extrait, extrait, SR, outil, 6.957)
    avec = h.equilibre(extrait, h.caler(brillante, extrait), SR, outil, 6.957)
    assert_equal(seule["aigus"], None, "l'extrait ne porte rien dans les aigus")
    assert_true(avec["aigus"] is not None and avec["aigus"] > 20.0,
                f"la bande des aigus doit porter dans le rendu et se voir : {avec['aigus']}")


@test
def h47_le_garde_fou_de_niveau_est_celui_de_l_arbitrage():
    assert_equal(h.ecartee_par_le_niveau(0.0, 0.05), "silence")
    assert_equal(h.ecartee_par_le_niveau(0.05, 0.05), None, "au niveau de l'extrait")
    assert_true((h.ecartee_par_le_niveau(0.001, 0.05) or "").startswith("trop faible"), "×45 au fader")
    assert_equal(h.ecartee_par_le_niveau(0.0046, 0.05), None, "×9,8 : sous le plafond de ×10")


@test
def h47_le_verdict_rend_ses_trois_issues_aux_seuils_ecrits():
    tenu = _mesure({"bas-médium": 0.4, "médium": -0.9, "sub": None}, 6.8)
    entre = _mesure({"bas-médium": 0.4, "médium": -2.0}, 6.8)
    echec_bande = _mesure({"bas-médium": 3.4, "médium": 0.1}, 6.8)
    echec_logmel = _mesure({"bas-médium": 0.4}, 9.5)
    assert_equal(_par_numero(_fabriquee(tenu, tenu))[3], "TENU")
    assert_equal(_par_numero(_fabriquee(entre, tenu))[3], "ENTRE LES DEUX")
    assert_equal(_par_numero(_fabriquee(echec_bande, tenu))[3], "ÉCHEC")
    assert_equal(_par_numero(_fabriquee(echec_logmel, tenu))[3], "ÉCHEC")
    assert_equal(_par_numero(_fabriquee(echec_bande, tenu))[4], "TENU", "le réglage se juge à part")
    assert_equal(_par_numero(_fabriquee(tenu, echec_bande))[4], "ÉCHEC")
    assert_equal(_par_numero(_fabriquee(_mesure({"sub": None}, 6.8), tenu))[3], "NON MESURABLE")


@test
def h47_le_verdict_juge_l_oracle_le_choix_et_la_tenue():
    tenu = _mesure({"médium": 0.2}, 6.5)
    v = _par_numero(_fabriquee(tenu, tenu))
    assert_equal((v[1], v[2], v[5]), ("TENU", "TENU", "TENU"))
    assert_equal(v[6], "ENTRE LES DEUX", "une seule des cinq premières tient la note")
    assert_equal(_par_numero(_fabriquee(tenu, tenu, classes=[1, 4, 6, 10]))[1], "ÉCHEC", "le si manque")
    assert_equal(_par_numero(_fabriquee(tenu, tenu, classes=[1, 4, 6, 8, 10, 11]))[1], "ÉCHEC", "un sol♯ en trop")
    assert_equal(_par_numero(_fabriquee(tenu, tenu, rho=0.55))[5], "ENTRE LES DEUX")
    assert_equal(_par_numero(_fabriquee(tenu, tenu, rho=0.39))[5], "ÉCHEC")
    frappee = _mesure({"médium": 0.2}, 6.5, tenue=-30.0)
    assert_equal(_par_numero(_fabriquee(frappee, tenu))[6], "ÉCHEC", "aucune des cinq ne tient")
    inversees = _fabriquee(tenu, tenu)
    inversees["bornes"]["mesuree"]["logmel"] = 7.0
    assert_equal(_par_numero(inversees)[2], "ÉCHEC", "B_mesuré au-dessus de B_égal : l'instrument est faux")


@test
def h47_un_coefficient_sur_moins_de_trois_points_distincts_ne_se_lit_pas():
    pareilles = [{"D": 0.2, "logmel": 5.0}, {"D": 0.2, "logmel": 5.0}, {"D": 0.3, "logmel": 6.0}]
    rho, n, distincts_d, _distincts_l = h.spearman(pareilles)
    assert_equal((rho, n, distincts_d), (None, 3, 2))
    rho, n, _d, _l = h.spearman([{"D": 0.1 * i, "logmel": float(i)} for i in range(1, 6)])
    assert_equal((rho, n), (1.0, 5))
