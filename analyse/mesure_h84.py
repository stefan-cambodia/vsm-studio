"""H84 — deux kicks moyens comparés en temps-fréquence (docs/CDC-reload-indifferenciable.md § 45).

`ecart_tf(a, b, sr)` : l'écart absolu moyen, en dB, des spectrogrammes de deux frappes moyennes, de `BANDE_HZ`, sur
`DUREE_S`, chacune normalisée à son énergie dans cette région (le niveau n'est pas jugé), plancher à `PLANCHER_DB` sous
son maximum. Un signal sans énergie dans la région est un REFUS.
"""
from __future__ import annotations

import numpy as np

BANDE_HZ = (30.0, 500.0)
DUREE_S = 0.300
PLANCHER_DB = -40.0


def carte_db(y: np.ndarray, sr: int) -> np.ndarray:
    from scipy.signal import stft

    n = int(DUREE_S * sr)
    x = np.zeros(n)
    x[:min(n, len(y))] = y[:n]
    f, _, z = stft(x, fs=sr, nperseg=2048, noverlap=2048 - 256, boundary=None, padded=False)
    p = np.abs(z[(f >= BANDE_HZ[0]) & (f <= BANDE_HZ[1])]) ** 2
    total = float(p.sum())
    if total <= 0:
        raise ValueError("rien entre 30 et 500 Hz")
    d = 10 * np.log10(np.maximum(p / total, 1e-30))
    return np.maximum(d, d.max() + PLANCHER_DB)


def ecart_tf(a: np.ndarray, b: np.ndarray, sr: int) -> float:
    return round(float(np.mean(np.abs(carte_db(a, sr) - carte_db(b, sr)))), 3)
