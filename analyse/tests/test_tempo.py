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
