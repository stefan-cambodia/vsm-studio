"""L'instrument de H76 (CDC-reload § 37) sur des signaux CONNUS, avant toute mesure.

La scène de H74 / H75 : un kick, une caisse claire, un charleston, un « stem » qui les mélange à des gains connus. La
caisse de l'ORIGINAL est large bande (dès 200 Hz) ; le TÉMOIN la rend sans rien sous 2 kHz — le timbre faux de H75.
Trois candidats : une caisse large (la bonne machine), une caisse muette, et pour le charleston une machine de MÊME
timbre que le témoin (rien à gagner). L'instrument doit choisir la première, écarter et dire la deuxième, et ne pas
changer le charleston.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

import mesure_h69 as h69  # noqa: E402
import mesure_h74 as h74  # noqa: E402
import mesure_h76 as h76  # noqa: E402
from test_mesure_h74 import SR, grille, piece  # noqa: E402


@test
def test_h76_le_choix_veut_une_baisse_d_au_moins_le_seuil():
    assert_equal(h76.choisir(1.0, {"a": 0.96, "b": 0.99}), None)          # 4 % : pas un choix
    assert_equal(h76.choisir(1.0, {"a": 0.95, "b": 0.99}), "a")           # 5 % : un choix
    assert_equal(h76.choisir(1.0, {"b": 0.5, "a": 0.5}), "a")             # à égalité, l'ordre des noms
    assert_equal(h76.choisir(1.0, {}), None)                              # aucun candidat
    assert_equal(h76.choisir(1.0, {"a": 1.2}), None)                      # pire : la pièce garde sa machine


@test
def test_h76_une_bande_cassee_est_une_bande_proche_devenue_lointaine():
    avant = {"30-150": 1.0, "150-500": -2.9, "500-2000": -12.0, "2000-4000": 2.0}
    apres = {"30-150": 6.5, "150-500": -5.9, "500-2000": -1.0, "2000-4000": -6.1}
    # 30-150 et 2000-4000 étaient près et sont loin ; 150-500 reste sous 6 dB ; 500-2000 n'était pas près
    assert_equal(h76.bandes_cassees(avant, apres), ["30-150", "2000-4000"])


@test
def test_h76_la_bonne_machine_est_choisie_la_muette_dite_la_pareille_laissee():
    g = grille()
    gains = {"kick": 1.0, "caisse": 2.0, "charleston": 0.5}
    stem = sum(gains[n] * piece(n, g[n], 10 + k, caisse_large=(n == "caisse")) for k, n in enumerate(gains))
    temoins = {n: h74.bandes_par_trame(piece(n, g[n], 20 + k), SR) for k, n in enumerate(gains)}
    temoins["caisse"] = h74.bandes_par_trame(h69.filtrer(piece("caisse", g["caisse"], 21), SR, 2000, None), SR)
    candidats = {
        "caisse": {"large": h74.bandes_par_trame(piece("caisse", g["caisse"], 31, caisse_large=True), SR),
                   "muette": h74.bandes_par_trame(np.zeros(len(stem)), SR)},
        "charleston": {"pareille": h74.bandes_par_trame(piece("charleston", g["charleston"], 32), SR)},
    }
    r = h76.evaluer(h74.bandes_par_trame(stem, SR), temoins, candidats, g)
    assert_equal(r["choix"], {"kick": None, "caisse": "large", "charleston": None})
    assert_true(r["table"]["caisse"]["muette"]["muet"], f"la muette : {r['table']['caisse']}")
    assert_true(any("muette" in d and "MUET" in d for d in r["dits"]), f"dits : {r['dits']}")
    assert_true("residu_pondere" not in r["table"]["caisse"]["muette"], "une muette n'a pas de résidu")
    large = r["table"]["caisse"]["large"]["residu_pondere"]
    assert_true(large < 0.5 * r["residu_temoin"], f"la bonne machine divise le résidu : {large} / {r['residu_temoin']}")
    pareille = r["table"]["charleston"]["pareille"]["residu_pondere"]
    assert_true(pareille > 0.95 * r["residu_temoin"], f"la pareille ne gagne rien : {pareille} / {r['residu_temoin']}")
    # la combinaison : le résidu de la bonne machine, les gains retrouvés, rien de cassé
    assert_true(r["combinaison"]["residu_pondere"] <= large + 1e-9, f"{r['combinaison']['residu_pondere']}")
    assert_true(abs(r["combinaison"]["facteurs"]["caisse"] - 2.0) < 0.2, f"{r['combinaison']['facteurs']}")
    assert_equal(r["casse"], {"caisse": []})


@test
def test_h76_sans_candidat_meilleur_la_combinaison_est_le_temoin():
    g = grille()
    gains = {"kick": 1.0, "caisse": 2.0, "charleston": 0.5}
    stem = sum(gains[n] * piece(n, g[n], 10 + k) for k, n in enumerate(gains))
    temoins = {n: h74.bandes_par_trame(piece(n, g[n], 20 + k), SR) for k, n in enumerate(gains)}
    pire = {"caisse": {"etroite": h74.bandes_par_trame(h69.filtrer(piece("caisse", g["caisse"], 21), SR, 2000, None), SR)}}
    r = h76.evaluer(h74.bandes_par_trame(stem, SR), temoins, pire, g)
    assert_equal(r["choix"], {"kick": None, "caisse": None, "charleston": None})
    assert_equal(r["combinaison"]["residu_pondere"], r["residu_temoin"])
    assert_equal(r["casse"], {})


if __name__ == "__main__":
    raise SystemExit(run())
