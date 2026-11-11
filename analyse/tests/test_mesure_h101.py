"""L'instrument de H101 (CDC-reload § 62) sur des signaux connus, avant toute mesure."""

from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_true, run, test  # noqa: E402

import mesure_h101 as h101  # noqa: E402

SR = 44100
T = np.arange(4 * SR) / SR
BANDES = [(20, 150), (4000, 8000), (11000, 16000), (16000, 22000)]


@test
def test_h101_chaque_note_est_dans_sa_bande_a_son_niveau():
    x = np.sin(2 * np.pi * 100 * T) + 0.1 * np.sin(2 * np.pi * 13000 * T)
    e = h101.energie_par_bande(x, SR, BANDES)
    assert_true(abs(e["20-150"] - 0.5) < 0.02, f"un sinus d'amplitude 1 : énergie 0,5 — {e}")
    assert_true(abs(10 * math.log10(e["11000-16000"] / e["20-150"]) + 20.0) < 0.3, f"la note de 13 kHz, 20 dB dessous : {e}")
    assert_true(e["4000-8000"] < 1e-6 and e["16000-22000"] < 1e-6, f"rien ailleurs : {e}")
    try:
        h101.energie_par_bande(x, SR, [(16000, 24000)])
    except ValueError:
        pass
    else:
        raise AssertionError("une bande au-delà de Nyquist a été lue")


@test
def test_h101_le_centroide_et_sa_coupure():
    x = np.sin(2 * np.pi * 1000 * T) + np.sin(2 * np.pi * 13000 * T)
    assert_true(abs(h101.centroide(x, SR) - 7000.0) < 50.0, f"deux notes égales : le milieu — {h101.centroide(x, SR)}")
    assert_true(abs(h101.centroide(x, SR, 11000.0) - 1000.0) < 50.0, f"coupé à 11 kHz : {h101.centroide(x, SR, 11000.0)}")
    try:
        h101.centroide(np.zeros(SR), SR, 11000.0)
    except ValueError:
        pass
    else:
        raise AssertionError("un centroïde sans énergie a été rendu")


if __name__ == "__main__":
    raise SystemExit(run())
