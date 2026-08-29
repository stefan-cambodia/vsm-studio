"""La métrique v3 : v2 plus un terme de hauteur pour le grave.

Ce que ces tests verrouillent est ce qui a motivé v3 et ce qui ne doit pas
bouger en l'ajoutant : sur une cible sans grave le terme est nul (v3 = v2) ;
sur un kick, v3 pénalise une hauteur fausse en octaves ; et la fabrique de
métriques refuse ce qu'elle ne connaît pas.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_raises, assert_true, test  # noqa: E402

from analyzer.audio_distance_v3 import low_pitch, pitch_term  # noqa: E402
from analyzer.vsm_distance_cache import METRIQUES, cached_distance_for  # noqa: E402
from analyzer.vsm_engine import Note, VsmEngine  # noqa: E402

SR = 44100


def sinus(freq: float, secondes: float = 1.0) -> np.ndarray:
    t = np.arange(int(secondes * SR)) / SR
    return (0.5 * np.sin(2 * np.pi * freq * t)).astype(np.float32)


@test
def metrique_v3_lit_la_hauteur_du_grave():
    pic, part = low_pitch(sinus(60.0), SR)
    assert_near(pic, 60.0, 3.0, "pic à 60 Hz")
    assert_true(part > 0.9, "tout le grave")
    assert_near(pitch_term((60.0, 0.9), (120.0, 0.9)), 1.0, 1e-6, "une octave = 1")
    assert_near(pitch_term((60.0, 0.9), (30.0, 0.9)), 1.0, 1e-6, "dans l'autre sens aussi")
    assert_near(pitch_term((60.0, 0.9), (60.0, 0.9)), 0.0, 1e-6, "même hauteur = 0")


@test
def metrique_v3_est_nulle_sans_grave_donc_egale_a_v2():
    """Une nappe aiguë ne doit pas être jugée sur un pic grave qui n'est que du bruit."""
    cible = sinus(1000.0)
    candidat = sinus(1200.0)
    _, part = low_pitch(cible, SR)
    assert_true(part < 0.1, "pas de grave dans une sinusoïde à 1 kHz")
    v2 = cached_distance_for("v2")(cible, SR)(candidat)
    v3 = cached_distance_for("v3")(cible, SR)(candidat)
    assert_near(v3, v2, 1e-9, "sans grave, v3 == v2 exactement")


@test
def metrique_v3_penalise_un_kick_a_la_mauvaise_hauteur():
    """Le cas qui a motivé v3. Sur un kick 808 à 60 Hz, v2 et v3 tombent
    juste ; mais v3 écarte BEAUCOUP plus un kick à 30 Hz, et c'est le terme de
    hauteur qui le fait."""
    with VsmEngine(sample_rate=SR) as moteur:
        notes = [Note(36, 110, i * 0.5, 0.1) for i in range(4)]
        cible = moteur.render("vsm.tr808", {"drum.kick.tune": 60.0}, notes, 2.3)
        faux = moteur.render("vsm.tr808", {"drum.kick.tune": 30.0}, notes, 2.3)
        juste = moteur.render("vsm.tr808", {"drum.kick.tune": 60.0}, notes, 2.3)
    m3 = cached_distance_for("v3")(cible, SR)
    m2 = cached_distance_for("v2")(cible, SR)
    assert_near(m3(juste), 0.0, 1e-6, "la cible contre elle-même")
    assert_true(m3(faux) > 1.5 * m2(faux), "v3 punit l'octave nettement plus que v2 (mesuré : ×2,1)")
    assert_true(m3.terms(faux)["pitch"] > 0.7, "et c'est le terme de hauteur qui le fait")


@test
def metrique_la_fabrique_refuse_l_inconnu():
    """Cinq modules choisissaient la métrique chacun par un `if` ; une
    troisième aurait été oubliée dans l'un d'eux. Une seule fabrique, qui
    REFUSE ce qu'elle ne connaît pas plutôt que de se rabattre en silence."""
    assert_equal(METRIQUES, ("v1", "v2", "v3", "v4"), "les quatre versions")
    for m in METRIQUES:
        assert_true(cached_distance_for(m) is not None, f"{m} existe")
    assert_raises(ValueError, lambda: cached_distance_for("v5"), "v5 n'existe pas")
