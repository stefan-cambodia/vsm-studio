"""L'instrument de H77 (CDC-reload § 38) sur des cas CONNUS, avant toute mesure.

Le compte des paires : tranchée ou non (0,10 dB au mélange), concordante ou non, une égalité au stem comptée
discordante. Et le log-mel au stem, par la fonction `logmel` de l'outil du § 0 : nul d'un signal à lui-même, et plus
grand pour un bruit vidé de son aigu que pour le même bruit à −1 dB.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

import mesure_h77 as h77  # noqa: E402


@test
def test_h77_une_paire_sous_le_seuil_n_est_pas_tranchee():
    ps = h77.paires({"a": {"melange": 8.37, "stem": 9.0}, "b": {"melange": 8.42, "stem": 7.0}})
    assert_equal(ps[0]["tranchee"], False)
    assert_equal(ps[0]["concordante"], None)
    assert_equal(h77.bilan(ps), {"paires": 1, "tranchees": 0, "concordantes": 0, "discordantes": 0,
                                 "discordantes_grandes": 0})


@test
def test_h77_le_seuil_lui_meme_tranche():
    ps = h77.paires({"a": {"melange": 8.0, "stem": 9.0}, "b": {"melange": 8.1, "stem": 9.5}})
    assert_equal(ps[0]["tranchee"], True)
    assert_equal(ps[0]["concordante"], True)


@test
def test_h77_l_ordre_inverse_au_stem_est_discordant_et_grand_au_dela_d_un_demi_decibel():
    ps = h77.paires({"h75": {"melange": 8.42, "stem": 9.0}, "h76": {"melange": 9.34, "stem": 8.5},
                     "temoin": {"melange": 8.57, "stem": 9.4}})
    par = {(p["a"], p["b"]): p for p in ps}
    assert_equal(par[("h75", "h76")]["concordante"], False)
    assert_equal(par[("h75", "h76")]["grande"], True)
    assert_equal(par[("h75", "temoin")]["concordante"], True)
    assert_equal(par[("h75", "temoin")]["grande"], False)
    assert_equal(h77.bilan(ps), {"paires": 3, "tranchees": 3, "concordantes": 1, "discordantes": 2,
                                 "discordantes_grandes": 2})


@test
def test_h77_une_egalite_au_stem_ne_predit_rien():
    ps = h77.paires({"a": {"melange": 8.0, "stem": 9.0}, "b": {"melange": 9.0, "stem": 9.0}})
    assert_equal(ps[0]["concordante"], False)
    assert_equal(h77.bilan(ps)["discordantes_grandes"], 1)


@test
def test_h77_le_log_mel_au_stem_voit_une_bande_videe():
    from scipy.signal import butter, sosfilt
    sr = 44100
    stem = np.random.default_rng(7).standard_normal(sr * 4).astype(np.float32) * 0.1
    logmel = h77.outil().logmel
    pareil = h77.logmel_au_stem(stem, stem, sr, logmel)
    assert_equal(pareil["moyen"], 0.0)
    plus_bas = h77.logmel_au_stem(stem, stem * 10 ** (-1 / 20), sr, logmel)
    assert_true(0.9 <= plus_bas["moyen"] <= 1.1, f"−1 dB partout : {plus_bas}")
    vide = sosfilt(butter(8, 4000, btype="low", fs=sr, output="sos"), stem).astype(np.float32)
    sans_aigu = h77.logmel_au_stem(stem, vide, sr, logmel)
    assert_true(sans_aigu["moyen"] > 5 * plus_bas["moyen"], f"aigu vidé : {sans_aigu} contre {plus_bas}")


if __name__ == "__main__":
    raise SystemExit(run())
