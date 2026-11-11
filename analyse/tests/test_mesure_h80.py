"""L'instrument de H80 (CDC-reload § 41) sur des signaux CONNUS, avant toute mesure.

La scène de H74 : kick toutes les 0,5 s, caisse sur un kick sur deux, charleston toutes les 0,25 s (un sur deux ENTRE
les kicks). Le kick de l'original décroît en 0,12 s ; trois candidats : 0,06, 0,12 et 0,30 s. Le trop long déborde
sur les charlestons d'entre les kicks ; l'instrument doit choisir 0,12 s, et refuser un balayage dont la queue ne
suit pas la décroissance (un réglage qui n'atteint pas le moteur rend trois fois le même solo).
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
import mesure_h77 as h77  # noqa: E402
import mesure_h80 as h80  # noqa: E402
from test_mesure_h74 import DUREE, SR, grille, piece  # noqa: E402

GAINS = {"kick": 1.0, "caisse": 2.0, "charleston": 0.5}
_LOGMEL = h77.outil().logmel


def critere(stem, batterie):
    return h77.logmel_au_stem(stem.astype(np.float32), batterie.astype(np.float32), SR, _LOGMEL)


def kick(instants, decroissance):
    x = np.zeros(DUREE * SR)
    for t in instants:
        i = int(round(t * SR))
        k = h69.replique_tr808(55.0, decroissance, SR, duree=0.45)
        x[i:i + len(k)] += k[:len(x) - i]
    return x


def scene():
    g = grille()
    autres = {n: piece(n, g[n], 10 + k) for k, n in enumerate(GAINS) if n != "kick"}
    stem = kick(g["kick"], 0.12) + sum(GAINS[n] * x for n, x in autres.items())
    return g, stem, autres


AVANTS = {n: {b: 0.0 for b in ("30-150", "150-500", "500-2000", "2000-4000", "4000-8000", "8000-11000")} for n in GAINS}
FACTEURS = {"caisse": 2.0, "charleston": 0.5}


@test
def test_h80_le_choix_prend_le_plus_petit_debord_et_la_plus_longue_a_egalite():
    assert_equal(h80.choisir({0.05: 4.0, 0.12: 1.2, 0.32: 9.0}), 0.12)
    assert_equal(h80.choisir({0.05: 1.24, 0.12: 1.16, 0.32: 9.0}), 0.12)      # 1,2 et 1,2 au dixième : la plus longue
    assert_equal(h80.choisir({0.05: 1.1, 0.12: 1.3}), 0.05)
    assert_equal(h80.choisir({}), None)


@test
def test_h80_la_queue_doit_suivre_la_decroissance():
    assert_equal(h80.queue_croissante([(0.05, -30.0), (0.12, -12.0), (0.32, -3.0)]), True)
    assert_equal(h80.queue_croissante([(0.32, -3.0), (0.05, -30.0), (0.12, -12.0)]), True)     # l'ordre donné ne compte pas
    assert_equal(h80.queue_croissante([(0.05, -12.0), (0.12, -12.0), (0.32, -12.0)]), False)   # trois fois le même solo
    assert_equal(h80.queue_croissante([(0.05, -12.0), (0.12, -14.0), (0.32, -3.0)]), False)
    assert_equal(h80.queue_croissante([(0.05, -12.0)]), False)                                 # un point ne prouve rien


@test
def test_h80_le_debord_est_une_moyenne_de_valeurs_absolues():
    r = {"a": {"30-150": 13.3}, "b": {"30-150": -8.7}, "c": {"30-150": 40.0}}
    assert_equal(h80.debord(r, ["a", "b"], "30-150"), 11.0)
    try:
        h80.debord(r, [], "30-150")
    except ValueError:
        pass
    else:
        raise AssertionError("un débord sans pièce a été calculé")


@test
def test_h80_la_bonne_decroissance_est_choisie_et_le_trop_long_deborde():
    g, stem, autres = scene()
    candidats = {d: kick(g["kick"], d) for d in (0.06, 0.12, 0.30)}
    r = h80.evaluer(stem, autres, candidats, g, FACTEURS, "kick", 0, ["charleston", "caisse"], AVANTS, SR, critere)
    assert_true("refus" not in r, f"refus inattendu : {r.get('refus')}")
    assert_equal(r["choix"], 0.12)
    par = {p["decroissance_s"]: p for p in r["points"]}
    assert_true(par[0.06]["queue_db"] < par[0.12]["queue_db"] < par[0.30]["queue_db"], "la queue suit la décroissance")
    assert_true(abs(par[0.12]["facteur"] - 1.0) < 0.05, f"à la bonne décroissance, le niveau est retrouvé : {par[0.12]['facteur']}")
    assert_true(par[0.12]["debord_db"] < 1.0, f"débord au bon point : {par[0.12]['debord_db']}")
    assert_true(par[0.30]["debord_db"] > 6.0, f"le trop long déborde : {par[0.30]['debord_db']}")
    assert_true(par[0.30]["entre_db"]["charleston"]["30-150"] > 6.0, "le grave du kick long, aux charlestons d'entre les kicks")
    assert_true(par[0.06]["entre_db"]["charleston"]["30-150"] < -3.0, "le trop court laisse un creux au même endroit")
    # la caisse tombe toujours SUR un kick : écartée du débord, et dite ; un charleston sur deux est gardé
    assert_equal(r["deborde_sur"], ["charleston"])
    assert_true(any("caisse" in d and "écartée" in d for d in r["dits"]), f"dits : {r['dits']}")
    gardees, toutes = r["frappes_entre"]["charleston"]
    assert_true(gardees * 2 == toutes, f"un charleston sur deux est entre les kicks : {gardees} / {toutes}")
    assert_equal(r["frappes_entre"]["caisse"][0], 0)
    # toutes frappes confondues, le même débord est noyé par les têtes des kicks : moins de 1,5 dB
    partout, entre = par[0.30]["rendu_sur_stem_db"]["charleston"]["30-150"], par[0.30]["entre_db"]["charleston"]["30-150"]
    assert_true(abs(partout) < 0.5 * entre, f"toutes frappes confondues le débord est noyé : {partout} contre {entre}")


@test
def test_h80_un_reglage_qui_n_atteint_pas_le_moteur_est_refuse():
    g, stem, autres = scene()
    meme = kick(g["kick"], 0.12)
    r = h80.evaluer(stem, autres, {0.06: meme, 0.12: meme, 0.30: meme}, g, FACTEURS, "kick", 0, ["charleston"], AVANTS,
                    SR, critere)
    assert_true("refus" in r and r["choix"] is None, f"trois solos identiques doivent faire un refus : {r.get('choix')}")


@test
def test_h80_les_frappes_d_entre_les_kicks():
    assert_equal(h80.frappes_hors([1.0, 1.25, 1.5, 1.59, 1.61, 1.95], [1.0, 1.5, 2.0]), [1.25, 1.61])
    assert_equal(h80.frappes_hors([1.0, 1.4], [1.5]), [1.0, 1.4])
    assert_equal(h80.frappes_hors([1.0], []), [1.0])
    assert_equal(h80.frappes_hors([1.41], [1.5]), [])                    # 90 ms AVANT un kick : sa tête est dans la fenêtre


@test
def test_h80_tout_sur_les_kicks_est_un_refus():
    g, stem, autres = scene()
    candidats = {d: kick(g["kick"], d) for d in (0.06, 0.30)}
    try:
        h80.evaluer(stem, autres, candidats, g, FACTEURS, "kick", 0, ["caisse"], AVANTS, SR, critere)
    except ValueError as e:
        assert_true("débord" in str(e), str(e))
    else:
        raise AssertionError("un débord sans une seule frappe d'entre les kicks a été lu")


@test
def test_h80_la_queue_d_un_solo():
    g = grille()
    court, long_ = (h74.bandes_par_trame(kick(g["kick"], d), SR) for d in (0.06, 0.30))
    assert_true(h80.queue_db(court, g["kick"], 0) < h80.queue_db(long_, g["kick"], 0) - 6.0, "queue courte contre longue")
    try:
        h80.queue_db(np.zeros_like(court), g["kick"], 0)
    except ValueError:
        pass
    else:
        raise AssertionError("un solo muet a rendu une queue")


if __name__ == "__main__":
    raise SystemExit(run())
