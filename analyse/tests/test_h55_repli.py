"""H55 — l'instrument qui lit la trajectoire sur la famille des deux cadences (docs/CDC-reload-indifferenciable.md § 17).

L'ATTENDU 1 du § 17, écrit avant toute mesure de l'original. Cinq hauteurs, bruit à
−30 dB :
  (a) UNE LECTURE sous deux LFO : D ≥ 0,9 par raie, la forme repliée en cercle (≤ 5 %),
      les cadences relues à ± 2 mHz ;
  (b) DEUX ÉTAGES EN SÉRIE (S de H51) : D ≥ 0,9, la forme repliée hors cercle (> 15 %)
      sur les raies du haut ;
  (c) une modulation ALÉATOIRE (bruit complexe passe-bas à 8 Hz, indépendant par note,
      un quart de l'amplitude) : D ≤ 0,3 — la famille ne PRÉDIT pas un bruit.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Callable, Dict

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_true, test  # noqa: E402

import mesure_h47 as h47  # noqa: E402
import mesure_h53 as h53  # noqa: E402
import mesure_h55 as h  # noqa: E402
import test_h53_cercle as t53  # noqa: E402

NOTES = t53.NOTES
HAUT = t53.HAUT


def _aleatoire() -> np.ndarray:
    t = t53._t()
    sr = t53.SR
    y = np.zeros_like(t)
    rng = np.random.default_rng(55)
    for note in NOTES:
        bruit = rng.standard_normal(len(t)) + 1j * rng.standard_normal(len(t))
        B = np.fft.fft(bruit)
        ff = np.fft.fftfreq(len(t), 1.0 / sr)
        n = np.fft.ifft(np.where(np.abs(ff) <= 8.0, B, 0.0))
        n = n / np.sqrt(np.mean(np.abs(n) ** 2))
        env = 1.0 + 0.25 * n            # à 0,5, les raies tombaient sous 10 dB de leur fond : plus rien à juger
        hz = h47.hz_de(note, h53.LA4_HZ)
        y += np.real(env * np.exp(2j * np.pi * hz * t))
    return y + t53._bruit(56, len(t))


_MEMO: Dict[str, Dict[str, Any]] = {}


def _mesure(nom: str, fabrique: Callable[[], np.ndarray]) -> Dict[str, Any]:
    if nom not in _MEMO:
        _MEMO[nom] = h.analyser(fabrique(), t53.SR, NOTES)
    return _MEMO[nom]


def _decrire(m: Dict[str, Any]) -> str:
    return f"cadences {m['f1_hz']} / {m['f2_hz']} ; " + " ; ".join(
        f"{r['note']}: D={r.get('d')} circ={r.get('circularite')} arc={r.get('arc_couvert_deg')} apl={r.get('aplatissement')}"
        for r in m["raies"])


@test
def h55_une_lecture_se_replie_en_cercle():
    m = _mesure("une", t53._une_lecture)
    assert_true(abs(m["f1_hz"] - h53.F1_HZ) <= 0.002 and abs(m["f2_hz"] - h53.F2_HZ) <= 0.002, _decrire(m))
    for r in m["raies"]:
        assert_true(r["d"] >= 0.9, f"raie {r['note']} : D {r['d']} < 0,9 — {_decrire(m)}")
        assert_true(r["circularite"] is not None and r["circularite"] <= 0.05 and r["cercle"],
                    f"raie {r['note']} : forme repliée {r['circularite']} — {_decrire(m)}")


@test
def h55_la_serie_se_replie_hors_du_cercle():
    m = _mesure("serie", t53._serie)
    for r in m["raies"]:
        assert_true(r["d"] >= 0.9, f"raie {r['note']} : D {r['d']} < 0,9 — {_decrire(m)}")
    for r in (r for r in m["raies"] if r["note"] in HAUT):
        assert_true(r["circularite"] is not None and r["circularite"] > 0.15 and not r["cercle"],
                    f"raie {r['note']} : forme repliée {r['circularite']} — {_decrire(m)}")


@test
def h55_une_modulation_aleatoire_ne_se_replie_pas():
    m = _mesure("aleatoire", _aleatoire)
    vues = [r for r in m["raies"] if "d" in r]
    assert_true(len(vues) >= 3, f"{len(vues)} raie(s) vue(s) sous la modulation aléatoire — {_decrire(m)}")
    for r in vues:
        assert_true(r["d"] <= 0.3, f"raie {r['note']} : D {r['d']} > 0,3 — {_decrire(m)}")
