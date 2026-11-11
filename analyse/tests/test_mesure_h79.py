"""L'instrument de H79 (CDC-reload § 40) sur des signaux CONNUS, avant toute mesure.

La scène de H74 : un kick, une caisse claire, un charleston mélangés à des gains connus (×1 · ×2 · ×0,5). Le facteur
calé du kick, les autres pièces à leur vrai gain, doit retrouver ×1 ; avec des autres pièces trop fortes au point de
dépasser le stem, c'est un REFUS. La garde refaite doit nommer le cas de H78 (−3,5 → −11,2 dB), que l'ancienne taisait.
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

import mesure_h74 as h74  # noqa: E402
import mesure_h76 as h76  # noqa: E402
import mesure_h79 as h79  # noqa: E402
from test_mesure_h74 import SR, grille, piece  # noqa: E402

GAINS = {"kick": 1.0, "caisse": 2.0, "charleston": 0.5}


def scene():
    g = grille()
    stem = sum(GAINS[n] * piece(n, g[n], 10 + k) for k, n in enumerate(GAINS))
    solos = {n: h74.bandes_par_trame(piece(n, g[n], 10 + k), SR) for k, n in enumerate(GAINS)}
    return g, h74.bandes_par_trame(stem, SR), solos


@test
def test_h79_la_garde_refaite_nomme_le_trou_de_h78():
    avant = {"30-150": -3.54, "150-500": -7.39, "500-2000": -16.26, "8000-11000": 2.0}
    apres = {"30-150": -11.19, "150-500": -8.67, "500-2000": -10.62, "8000-11000": 5.9}
    assert_equal(h76.bandes_cassees(avant, apres), [])                    # l'ancienne garde se tait : c'est le trou
    assert_equal(h79.bandes_eloignees(avant, apres), ["30-150"])          # la neuve le nomme
    # loin mais pas plus loin de 3 dB : non ; plus loin de 3 dB mais encore sous 6 dB : non
    assert_equal(h79.bandes_eloignees({"a": -7.0, "b": 1.0}, {"a": -9.9, "b": -5.9}), [])
    assert_equal(h79.bandes_eloignees({"a": -7.0, "b": 1.0}, {"a": 10.1, "b": -6.1}), ["a", "b"])


@test
def test_h79_le_facteur_cale_retrouve_le_gain_connu():
    g, stem, solos = scene()
    f, dits = h79.facteur_cale(stem, solos, g, {"caisse": 2.0, "charleston": 0.5}, "kick", 0)
    assert_true(abs(f - 1.0) < 0.05, f"kick calé à {f}, attendu 1")
    assert_equal(dits, [])
    # le facteur donné pour la pièce calée elle-même est ignoré
    f2, _ = h79.facteur_cale(stem, solos, g, {"kick": 9.0, "caisse": 2.0, "charleston": 0.5}, "kick", 0)
    assert_equal(f2, f)


@test
def test_h79_le_resultat_depend_des_autres_pieces_et_le_refus_est_dit():
    g, stem, solos = scene()
    doubles = {n: 2.0 * 2.0 * s for n, s in solos.items()}   # des solos rendus deux fois trop fort (énergie × 4)
    f, _ = h79.facteur_cale(stem, {**solos, "kick": doubles["kick"]}, g, {"caisse": 2.0, "charleston": 0.5}, "kick", 0)
    assert_true(abs(f - 0.5) < 0.03, f"un solo deux fois trop fort se cale à la moitié : {f}")
    try:   # la caisse seule dépasse le stem dans SA bande à ses frappes : rien à caler pour elle
        h79.facteur_cale(stem, solos, g, {"kick": 1.0, "charleston": 40.0}, "caisse", 4)
    except ValueError as e:
        assert_true("dépassent" in str(e), str(e))
    else:
        raise AssertionError("un numérateur négatif n'a pas été refusé")
    try:
        h79.facteur_cale(stem, solos, g, {"caisse": 2.0}, "kick", 0)
    except ValueError as e:
        assert_true("charleston" in str(e), str(e))
    else:
        raise AssertionError("une pièce sans facteur n'a pas été refusée")


@test
def test_h79_une_borne_atteinte_est_dite():
    g, stem, solos = scene()
    faible = {**solos, "kick": solos["kick"] / 100.0}        # un solo dix fois trop bas : ×10 demandé, borne ×4
    f, dits = h79.facteur_cale(stem, faible, g, {"caisse": 2.0, "charleston": 0.5}, "kick", 0)
    assert_equal(f, 4.0)
    assert_true(len(dits) == 1 and "borne" in dits[0], f"dits : {dits}")


@test
def test_h79_le_residu_a_facteurs_donnes_est_nul_aux_vrais_gains():
    g, stem, solos = scene()
    r = h79.rendu_sur_stem(stem, solos, g, GAINS, "kick")
    assert_true(abs(r["30-150"]) < 0.5, f"aux vrais gains, le grave du kick est au stem : {r}")
    bas = h79.rendu_sur_stem(stem, solos, g, {**GAINS, "kick": 0.25}, "kick")
    assert_true(bas["30-150"] < -9.0, f"kick à ×0,25 : {bas}")
    assert_equal(h79.bandes_eloignees(r, bas)[:1], ["30-150"])


if __name__ == "__main__":
    raise SystemExit(run())
