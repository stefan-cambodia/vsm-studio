"""L'instrument de H69 (CDC-reload § 30) sur des signaux CONNUS, avant toute mesure de l'original.

Un kick de forme connue (la réplique du TR-808 à d'autres réglages que ceux de « Reload ») est posé à des instants
connus, dans un fond qui ne frappe pas avec lui ; on donne à l'instrument des instants faux de quelques
millisecondes, comme la transcription. Il doit rendre la forme, la trajectoire et le modèle.
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

SR = 44100


def scene(accord=55.0, decroissance=0.2, profondeur=3.0, constante=0.02, bruit=0.1, graine=1):
    """60 s : un kick toutes les 0,6 s, un fond (bruit + une note de basse non synchrone), et des instants
    transcrits faux de ± 8 ms."""
    rng = np.random.default_rng(graine)
    kick = h69.replique_tr808(accord, decroissance, SR, duree=0.5, profondeur=profondeur, constante=constante)
    x = np.zeros(60 * SR)
    vrais = np.arange(1.0, 58.0, 0.6)
    for t in vrais:
        i = int(round(t * SR))
        x[i:i + len(kick)] += kick * rng.uniform(0.7, 1.0)
    tt = np.arange(len(x)) / SR
    x += bruit * rng.standard_normal(len(x)) + 0.3 * np.sin(2 * np.pi * 97.3 * tt)
    transcrits = vrais + rng.uniform(-0.008, 0.008, len(vrais))
    return x, vrais, transcrits, kick


@test
def test_h69_la_moyenne_rend_la_forme_et_la_trajectoire():
    x, _vrais, transcrits, kick = scene()
    r = h69.mesurer_un(x, SR, transcrits)
    for ms, attendu in ((10, 55 * (1 + 3 * np.exp(-0.010 / 0.02))), (25, 55 * (1 + 3 * np.exp(-0.025 / 0.02))),
                        (50, 55 * (1 + 3 * np.exp(-0.050 / 0.02))), (100, 55 * (1 + 3 * np.exp(-0.100 / 0.02)))):
        lu = r["frequence_hz"][str(ms)]
        assert_true(lu is not None and abs(lu / attendu - 1) < 0.05, f"{ms} ms : lu {lu}, attendu {attendu:.1f}")
    aj = r["ajustement"]
    assert_near(aj["f1_hz"], 55.0, 55 * 0.05, "f1")
    assert_near(aj["r"], 3.0, 3.0 * 0.15, "profondeur")
    assert_near(aj["tau_s"], 0.02, 0.02 * 0.15, "constante du balayage")
    assert_near(aj["tau_a_s"], 0.2, 0.2 * 0.15, "décroissance")
    assert_true(r["correlation_paires_impaires"] >= 0.95, f"paires/impaires {r['correlation_paires_impaires']}")
    # la part au-dessus de 150 Hz : celle du kick seul, passé par le même chemin
    seul = np.concatenate([np.zeros(int(h69.AVANT_S * SR)), kick])
    ref = h69.decrire(h69.filtrer(seul, SR, None, h69.PASSE_BAS_HZ), SR)
    assert_near(r["part_au_dessus_db"], ref["part_au_dessus_db"], 0.5, "part au-dessus de 150 Hz")


@test
def test_h69_l_instrument_distingue_deux_balayages():
    """Le contrôle de l'attendu 3 : deux kicks dont la part au-dessus de 150 Hz diffère de plusieurs dB doivent
    être lus différents — sinon l'instrument ne pourrait pas trancher."""
    lent, _, t1, _ = scene(accord=60.0, decroissance=0.15, profondeur=5.0, constante=0.045, graine=2)
    vif, _, t2, _ = scene(accord=60.0, decroissance=0.15, profondeur=2.0, constante=0.010, graine=3)
    a, b = h69.mesurer_un(lent, SR, t1), h69.mesurer_un(vif, SR, t2)
    assert_true(a["part_au_dessus_db"] - b["part_au_dessus_db"] > 3.0,
                f"{a['part_au_dessus_db']} contre {b['part_au_dessus_db']}")
    assert_true(a["sous_150_hz_ms"] > 45 and b["sous_150_hz_ms"] < 20,
                f"{a['sous_150_hz_ms']} et {b['sous_150_hz_ms']} ms")


@test
def test_h69_un_fond_sans_kick_ne_se_lit_pas_comme_un_echantillon_repete():
    """Le contrôle de l'attendu 2 : sans kick répété, les moyennes paires et impaires ne se ressemblent pas."""
    rng = np.random.default_rng(4)
    x = rng.standard_normal(60 * SR)
    r = h69.mesurer_un(x, SR, np.arange(1.0, 58.0, 0.6))
    assert_true(r["correlation_paires_impaires"] < 0.7, f"paires/impaires {r['correlation_paires_impaires']}")


@test
def test_h69_retenir_dit_chaque_exclusion():
    kicks = [10.0, 10.2, 11.0, 12.0, 13.0, 30.0]
    caisses = [12.1]
    gardes, compte = h69.retenir(kicks, caisses, [(9.0, 20.0)])
    assert_equal(gardes, [11.0, 13.0])
    assert_equal(compte, {"kicks": 6, "hors_sections": 1, "caisse_proche": 1, "kick_voisin": 2, "retenues": 2})


if __name__ == "__main__":
    raise SystemExit(run())
