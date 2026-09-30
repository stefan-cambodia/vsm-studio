"""F-tempo : la reconstruction s'ouvre au tempo du morceau, estimé sur le mélange.

Un clic synthétique à 100 BPM doit rendre 100 ± 2 ; un signal trop court rend le
120 d'avant, sans lever. La mesure contre la vérité du banc est celle de
`tools/tempo-estime.py` (10/10 à ±2 BPM le 15/09/2026) ; ce test garde le
contrat du module, pas sa qualité.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, test  # noqa: E402

from analyzer.tempo import estimer_tempo  # noqa: E402


def _clic(bpm: float, secondes: float, sr: int = 22050) -> np.ndarray:
    x = np.zeros(int(secondes * sr), dtype=np.float32)
    pas = int(round(60.0 / bpm * sr))
    coup = (np.exp(-np.arange(int(0.03 * sr)) / (0.004 * sr)) * np.sin(2 * np.pi * 1000 * np.arange(int(0.03 * sr)) / sr)).astype(np.float32)
    for debut in range(int(0.25 * sr), x.size - coup.size, pas):
        x[debut:debut + coup.size] += coup
    return x


@test
def un_clic_a_100_bpm_est_estime_a_100() -> None:
    t = estimer_tempo(_clic(100.0, 20.0), 22050)
    assert_near(t.bpm, 100.0, 2.0)
    assert_true(t.temps > 20, f"{t.temps} temps suivis, plus de vingt attendus")
    assert_true(0.0 <= t.premier_temps_secondes < 1.5, f"premier temps à {t.premier_temps_secondes} s")
    assert_equal(t.json()["source"], "estime")


@test
def un_signal_trop_court_rend_le_tempo_d_avant_sans_lever() -> None:
    t = estimer_tempo(np.zeros(1000, dtype=np.float32), 22050)
    assert_equal(t.bpm, 120.0)
    assert_equal(t.temps, 0)


# --- H46 : le tempo à la précision d'une grille (docs/CDC-reload-indifferenciable.md § 8)

def _grille(bpm: float, secondes: float, positions: tuple, sr: int = 22050, graine: int = 1) -> np.ndarray:
    """Des coups aux `positions` (fractions de temps) de chaque temps, à `bpm` EXACT (pas arrondi à l'échantillon par temps)."""
    x = np.zeros(int(secondes * sr), dtype=np.float32)
    n = int(0.03 * sr)
    rng = np.random.default_rng(graine)
    coup = (np.exp(-np.arange(n) / (0.004 * sr)) * rng.standard_normal(n)).astype(np.float32)
    temps = 0
    while True:
        for p in positions:
            debut = int(round((0.25 + (temps + p) * 60.0 / bpm) * sr))
            if debut + n >= x.size:
                return x
            x[debut:debut + n] += coup * (1.0 if p == 0 else 0.6)
        temps += 1


@test
def sans_l_option_le_tempo_est_celui_d_avant() -> None:
    x = _grille(138.0, 40.0, (0.0, 0.5))
    avant = estimer_tempo(x, 22050)
    assert_true(avant.affinage is None, "un affinage sans l'avoir demandé")
    assert_equal(sorted(avant.json()), ["bpm", "premierTempsSecondes", "source", "temps"])
    assert_equal(avant.bpm, round(avant.bpm, 1))


@test
def parti_de_139_7_l_affinage_rend_138_et_retient_l_entier() -> None:
    """LE CAS DE « RELOAD » : kick sur le temps, contretemps à la croche (une grille de
    NOIRES s'y annulerait), suivi à 139,7 pour 138,00.

    L'affinage est appelé avec SON départ, pas à travers `beat_track` : sur ce signal nu
    (une pulsation de croches), le suivi choisit 92 — les deux tiers de 138, une erreur
    de niveau métrique que H46 ne traite pas, et que l'affinage garde fidèlement (92,0
    sur une grille de sextolets EST la grille de doubles croches de 138). La première
    écriture de ce test passait par `estimer_tempo` et mesurait le suivi, pas l'affinage.
    """
    import librosa

    from analyzer.tempo import _instants_d_attaque, affiner_tempo

    x = _grille(138.0, 60.0, (0.0, 0.5))
    enveloppe = librosa.onset.onset_strength(y=x, sr=22050)
    instants, poids = _instants_d_attaque(np.asarray(enveloppe), 22050, 512)
    af = affiner_tempo(instants, poids, 139.7)
    assert_true(af.concluant, f"{af}")
    assert_true(af.entier, f"l'entier n'a pas été retenu : {af}")
    assert_near(af.maximum, 138.0, 0.02)
    assert_true(af.coherence > 0.8, f"cohérence {af.coherence}")
    # et un départ trop loin (la fenêtre de ± 4 % ne contient pas 138) ne conclut pas à 138
    loin = affiner_tempo(instants, poids, 150.0)
    assert_true(not (loin.concluant and abs(loin.maximum - 138.0) < 0.5), f"{loin}")


@test
def un_tempo_non_entier_est_rendu_au_centieme_pres_et_pas_arrondi() -> None:
    t = estimer_tempo(_grille(127.4, 120.0, (0.0, 0.25, 0.5, 0.75)), 22050, affiner=True)
    assert_true(t.affinage is not None and t.affinage.concluant, f"{t.affinage}")
    assert_near(t.bpm, 127.4, 0.01)
    assert_true(not t.affinage.entier, "127,4 arrondi à l'entier")


@test
def un_ternaire_se_lit_sur_la_grille_de_sextolets() -> None:
    """Des triolets à 96 : sur la grille de doubles croches, les deux tiers des attaques
    s'opposent au temps (cohérence ~0,2) ; sur celle de sextolets, tout s'aligne. Par son
    départ, comme le test de « Reload » : le suivi lit ce signal nu à 144 (une pulsation
    de croches), ce qui est un autre niveau métrique, pas une erreur d'affinage."""
    import librosa

    from analyzer.tempo import _instants_d_attaque, affiner_tempo

    x = _grille(96.0, 90.0, (0.0, 1 / 3, 2 / 3))
    enveloppe = librosa.onset.onset_strength(y=x, sr=22050)
    instants, poids = _instants_d_attaque(np.asarray(enveloppe), 22050, 512)
    af = affiner_tempo(instants, poids, 96.9)
    assert_true(af.concluant, f"{af}")
    assert_equal(af.subdivision, 6)
    assert_near(af.maximum, 96.0, 0.02)


@test
def un_bruit_ne_conclut_pas_et_garde_le_tempo_suivi() -> None:
    bruit = np.random.default_rng(5).normal(0, 0.1, 40 * 22050).astype(np.float32)
    t = estimer_tempo(bruit, 22050, affiner=True)
    assert_true(t.affinage is not None and not t.affinage.concluant, f"{t.affinage}")
    assert_equal(t.bpm, t.affinage.depart)
    assert_true(t.affinage.raison != "", "non concluant sans dire pourquoi")
    assert_equal(t.json()["affinage"]["concluant"], False)
