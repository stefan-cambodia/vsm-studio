"""H51 — le modèle de chorus et sa distance (docs/CDC-reload-indifferenciable.md § 13).

Ces tests verrouillent le MODÈLE contre ce que la théorie en dit, avant toute mesure :

  - un retard modulé seul (dosage 1) donne les bandes latérales de Bessel — l'indice
    est 2π·f·(profondeur / 2) ;
  - deux lectures en QUADRATURE sommées en mono éteignent la bande 2·f1 (c'est ce que
    fait l'effet du rack, et ce que l'original ne fait pas) ;
  - deux retards en PARALLÈLE ne produisent aucune combinaison f1 + f2 ; deux étages
    en SÉRIE en produisent ;
  - la distance est nulle d'une table à elle-même, compte un membre absent au
    plancher, et ignore le compte des composantes hors famille ;
  - les réglages vrais ont une distance nulle, des réglages déplacés non.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, test  # noqa: E402

import mesure_h47 as h47  # noqa: E402
import mesure_h51 as h  # noqa: E402

FA4 = h47.hz_de(66, h.LA4_HZ)            # 372,63 Hz


@test
def h51_un_retard_module_donne_les_bandes_de_bessel():
    from scipy.special import jv

    t = h.temps_du_modele()
    profondeur = 2.4
    beta = 2 * math.pi * FA4 * profondeur / 2000.0
    table = h.table_de_raie(h.etage(h.sinus(FA4), h.F1_HZ, profondeur, 1.0, 8.0)(t), h.SR_MODELE, FA4)
    attendus = {"0": abs(jv(0, beta)), "f1": abs(jv(1, beta)), "2f1": abs(jv(2, beta)), "3f1": abs(jv(3, beta))}
    plafond = max(attendus.values())
    for membre, amplitude in attendus.items():
        db = 20 * math.log10(amplitude / plafond)
        if db < h.PLANCHER_DB + 1.0:
            continue
        for cle in ((membre,) if membre == "0" else ("-" + membre, "+" + membre)):
            assert_true(cle in table, f"la bande {cle} est attendue à {db:.1f} dB : {table}")
            assert_near(table[cle], db, 0.5, f"la bande {cle} (indice {beta:.2f})")


@test
def h51_la_quadrature_eteint_la_seconde_bande_en_mono():
    t = h.temps_du_modele()
    une = h.table_de_raie(h.etage(h.sinus(FA4), h.F1_HZ, 2.4, 1.0, 8.0)(t), h.SR_MODELE, FA4)
    deux = h.table_de_raie(h.etage(h.sinus(FA4), h.F1_HZ, 2.4, 1.0, 8.0, phases=(0.0, math.pi / 2))(t), h.SR_MODELE, FA4)
    assert_true("+2f1" in une and "-2f1" in une, f"une lecture : 2·f1 présente : {une}")
    assert_true("+2f1" not in deux and "-2f1" not in deux, f"deux lectures en quadrature : 2·f1 éteinte : {deux}")
    assert_true("+f1" in deux and "-f1" in deux, f"et f1 reste : {deux}")


@test
def h51_le_parallele_ne_combine_pas_la_serie_si():
    t = h.temps_du_modele()
    reglages = [2.4, 0.5, 8.0, 2.0, 0.5, 5.0]
    serie = h.table_de_raie(h.topologie("S", FA4, reglages)(t), h.SR_MODELE, FA4)
    parallele = h.table_de_raie(h.topologie("P", FA4, [2.4, 0.4, 8.0, 2.0, 0.4, 5.0])(t), h.SR_MODELE, FA4)
    combinaisons = {"+f1+f2", "-f1+f2", "+2f1-f2", "-2f1-f2", "+2f1+f2", "-2f1+f2", "+f2-f1", "-f2-f1"}
    assert_true(combinaisons & set(serie), f"la série produit des combinaisons : {serie}")
    assert_equal(combinaisons & set(parallele), set(), f"le parallèle n'en produit aucune : {parallele}")
    assert_true({"+f1", "+f2"} <= set(parallele), f"mais il porte les deux cadences : {parallele}")


@test
def h51_la_distance():
    a = {"0": 0.0, "+f1": -3.0, "-f1": -4.0}
    assert_equal(h.distance(a, a), 0.0, "une table contre elle-même")
    assert_near(h.distance(a, {"0": 0.0, "+f1": -3.0}), (15.0 - 4.0) / 3, 1e-9, "un membre absent vaut le plancher")
    assert_near(h.distance(a, {"0": -2.0, "+f1": -3.0, "-f1": -4.0, "?": 3.0}), 2.0 / 3, 1e-9, "le compte « ? » n'entre pas")
    assert_equal(h.distance({}, {}), 15.0, "deux tables vides : le plancher, jamais zéro")


@test
def h51_les_reglages_vrais_ont_une_distance_nulle():
    t = h.temps_du_modele()
    vrais = [2.4, 0.5, 8.0, 1.5, 0.4, 5.0]
    notes = (52, 66, 73)
    originales = {n: h.table_du_modele("S", n, vrais, t) for n in notes}
    assert_equal(h.distance_moyenne("S", vrais, notes, originales, t), 0.0, "les réglages vrais")
    deplaces = [4.8, 0.5, 8.0, 1.5, 0.4, 5.0]
    assert_true(h.distance_moyenne("S", deplaces, notes, originales, t) > 2.0, "une profondeur doublée s'éloigne")
    assert_true(h.distance_moyenne("N", [], notes, originales, t) > 5.0, "un sinus nu est loin")
