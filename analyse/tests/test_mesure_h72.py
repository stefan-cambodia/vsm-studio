"""L'instrument de H72 (CDC-reload § 33) sur des signaux CONNUS, avant toute mesure de l'original.

Des charlestons de BRUIT (une graine par frappe : rien ne se répète d'une frappe à l'autre, comme le TR-808 dont les
oscillateurs tournent librement) à décroissance connue, sur une grille de double croche, dans un fond grave.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, run, test  # noqa: E402

import mesure_h69 as h69  # noqa: E402
import mesure_h72 as h72  # noqa: E402

SR = 44100


def scene(decroissance, gain=1.0, graine=1, fond=True):
    rng = np.random.default_rng(graine)
    x = np.zeros(40 * SR)
    instants = np.arange(1.0, 39.0, 0.1074)
    n = int(0.4 * SR)
    env = np.exp(-np.arange(n) / (decroissance * SR))
    for t in instants:
        i = int(round(t * SR))
        bruit = h69.filtrer(rng.standard_normal(n + 400), SR, 6000, None)[200:200 + n]
        x[i:i + n] += gain * bruit * env
    if fond:
        tt = np.arange(len(x)) / SR
        x += 0.5 * np.sin(2 * np.pi * 80 * tt)          # sous la bande : ne doit rien changer
    transcrits = instants + rng.uniform(-0.006, 0.006, len(instants))
    return x, transcrits


@test
def test_h72_le_choix_tombe_sur_la_decroissance_de_l_original():
    xo, inst = scene(0.12, graine=1)
    original, _ = h72.enveloppe_moyenne(xo, SR, inst)
    rendus = {}
    for d in (0.05, 0.08, 0.12, 0.16, 0.20):
        xr, _ = scene(d, gain=0.5, graine=2)
        rendus[d], _ = h72.enveloppe_moyenne(xr, SR, inst)
    choix = h72.choisir(original, rendus, SR)
    assert_equal(choix["decroissance_s"], 0.12, f"écarts {choix['ecarts_forme_db']}")
    assert_true(choix["ecarts_forme_db"]["0.05"] > 2.0, f"0,05 s : {choix['ecarts_forme_db']['0.05']}")
    assert_near(choix["facteur_niveau"], 2.0, 0.1, "facteur de niveau")
    # la constante relevée sur la forme : celle de l'enveloppe d'amplitude
    assert_near(h72.constante_db(original, SR), 0.12, 0.012, "constante relevée")


@test
def test_h72_la_forme_ignore_un_gain():
    xo, inst = scene(0.08, graine=3, fond=False)
    a, _ = h72.enveloppe_moyenne(xo, SR, inst)
    assert_near(h72.ecart_forme(a, 4.0 * a, SR), 0.0, 1e-9, "un gain n'est pas une forme")


@test
def test_h72_la_densite_lit_un_trou_entre_les_frappes():
    """Un charleston bref laisse retomber la bande entre deux frappes ; un long, non : le 10e centile le dit."""
    long_, _ = scene(0.20, graine=4, fond=False)
    bref, _ = scene(0.02, graine=5, fond=False)
    r = h72.densite(long_, bref, SR, [(2.0, 38.0)])
    ecart_10 = r["4000-8000"]["ecart"][0]
    assert_true(ecart_10 < -15.0, f"10e centile, bref − long : {ecart_10}")
    r2 = h72.densite(long_, long_, SR, [(2.0, 38.0)])
    assert_near(r2["4000-8000"]["ecart"][0], 0.0, 1e-9, "contre lui-même")


@test
def test_h72_retenir_dit_chaque_exclusion():
    gardes, compte = h72.retenir([10.0, 10.5, 11.0, 30.0], [10.45], [(9.0, 20.0)])
    assert_equal(gardes, [10.0, 11.0])
    assert_equal(compte, {"charlestons": 4, "hors_sections": 1, "caisse_proche": 1, "retenues": 2})


if __name__ == "__main__":
    raise SystemExit(run())
