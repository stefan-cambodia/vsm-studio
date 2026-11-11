"""L'instrument de H78 (CDC-reload § 39) sur des signaux CONNUS, avant toute mesure.

La scène de H74-H76 : un kick, une caisse claire, un charleston, un « stem » qui les mélange à des gains connus ; la
caisse de l'original est large bande, le TÉMOIN la rend sans rien sous 2 kHz. Le critère est celui de H77 (la fonction
`logmel` de l'outil du § 0). L'instrument doit choisir la caisse large, écarter et dire la muette, laisser le
charleston « pareil » ; refuser un candidat qui casse une bande, même meilleur au critère ; et refuser de sommer une
pièce sans facteur.
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
import mesure_h77 as h77  # noqa: E402
import mesure_h78 as h78  # noqa: E402
from test_mesure_h74 import SR, grille, piece  # noqa: E402

_LOGMEL = h77.outil().logmel


def critere(stem, batterie):
    return h77.logmel_au_stem(stem.astype(np.float32), batterie.astype(np.float32), SR, _LOGMEL)


@test
def test_h78_le_choix_veut_un_demi_decibel_et_refuse_ce_qui_casse():
    c = {"a": {"logmel": 10.6, "cassees": []}, "b": {"logmel": 10.9, "cassees": []}}
    assert_equal(h78.choisir(11.0, c), (None, []))                                   # 0,4 dB : pas un choix
    c["a"]["logmel"] = 10.5
    assert_equal(h78.choisir(11.0, c), ("a", []))                                    # 0,5 dB : un choix
    c["a"]["cassees"] = ["8000-11000"]
    assert_equal(h78.choisir(11.0, c), (None, ["a"]))                                # le meilleur casse : refusé, b trop près
    c["b"]["logmel"] = 10.0
    assert_equal(h78.choisir(11.0, c), ("b", ["a"]))                                 # le suivant, s'il passe le seuil
    assert_equal(h78.choisir(11.0, {}), (None, []))
    assert_equal(h78.choisir(11.0, {"b": {"logmel": 9.0, "cassees": []}, "a": {"logmel": 9.0, "cassees": []}}),
                 ("a", []))                                                          # à égalité, l'ordre des noms


@test
def test_h78_la_somme_refuse_une_piece_sans_facteur():
    x = np.ones(8)
    assert_true(np.allclose(h78.sommer({"k": x, "c": x}, {"k": 2.0, "c": 0.5}), 2.5 * x), "somme pondérée")
    try:
        h78.sommer({"k": x, "c": x}, {"k": 2.0})
    except ValueError as e:
        assert_true("c" in str(e), str(e))
    else:
        raise AssertionError("une pièce sans facteur a été sommée")


@test
def test_h78_le_controle_de_la_somme():
    assert_equal(h78.controle_de_la_somme(11.52, 11.56), True)
    assert_equal(h78.controle_de_la_somme(11.52, 11.58), False)
    assert_equal(h78.controle_de_la_somme(float("nan"), 11.52), False)


@test
def test_h78_la_bonne_machine_est_choisie_la_muette_dite_la_pareille_laissee():
    g = grille()
    gains = {"kick": 1.0, "caisse": 2.0, "charleston": 0.5}
    stem = sum(gains[n] * piece(n, g[n], 10 + k, caisse_large=(n == "caisse")) for k, n in enumerate(gains))
    temoins = {n: piece(n, g[n], 20 + k) for k, n in enumerate(gains)}
    temoins["caisse"] = h69.filtrer(piece("caisse", g["caisse"], 21), SR, 2000, None)
    candidats = {"caisse": {"large": piece("caisse", g["caisse"], 31, caisse_large=True), "muette": np.zeros(len(stem))},
                 "charleston": {"pareille": piece("charleston", g["charleston"], 32)}}
    r = h78.evaluer(stem, temoins, candidats, g, SR, critere)
    assert_equal(r["choix"], {"kick": None, "caisse": "large", "charleston": None})
    assert_equal(r["table"]["caisse"]["muette"], {"muet": True})
    assert_true(any("muette" in d and "MUET" in d for d in r["dits"]), f"dits : {r['dits']}")
    large = r["table"]["caisse"]["large"]["logmel"]
    assert_true(large <= r["logmel_temoin"] - 0.5, f"la bonne machine : {large} contre {r['logmel_temoin']}")
    pareille = r["table"]["charleston"]["pareille"]["logmel"]
    assert_true(abs(pareille - r["logmel_temoin"]) < 0.5, f"la pareille : {pareille} contre {r['logmel_temoin']}")
    c = r["combinaison"]
    assert_true(c is not None and c["logmel"] <= large + 1e-9, f"combinaison : {c}")
    assert_true(abs(c["facteurs"]["caisse"] - 2.0) < 0.2, f"{c['facteurs']}")
    assert_equal(c["cassees"], {"caisse": []})


@test
def test_h78_sans_candidat_meilleur_rien_ne_change():
    g = grille()
    gains = {"kick": 1.0, "caisse": 2.0, "charleston": 0.5}
    stem = sum(gains[n] * piece(n, g[n], 10 + k) for k, n in enumerate(gains))
    temoins = {n: piece(n, g[n], 20 + k) for k, n in enumerate(gains)}
    pire = {"caisse": {"etroite": h69.filtrer(piece("caisse", g["caisse"], 21), SR, 2000, None)}}
    r = h78.evaluer(stem, temoins, pire, g, SR, critere)
    assert_equal(r["choix"], {"kick": None, "caisse": None, "charleston": None})
    assert_equal(r["combinaison"], None)


if __name__ == "__main__":
    raise SystemExit(run())
