"""Le diapason (H42), la réunion des tenues (H43) et l'indice de hachure (H45).

docs/CDC-reload-indifferenciable.md § 4 à § 7. Ces tests verrouillent les
comportements que les verdicts ont mesurés, sur des signaux dont la vérité est
connue par construction :

  - l'estimateur de diapason lit un accord désaccordé de +12 cents à ±1 cent, et
    DIT qu'il ne peut pas mesurer un bruit (jamais « 0 cent ») ;
  - la valeur de session refuse un diapason hors 400-480 Hz ;
  - la réunion fond deux fragments d'un son tenu et laisse deux notes REJOUÉES ;
  - l'indice de hachure compte les notes suivies de la même hauteur à < 30 ms.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer import diapason  # noqa: E402
from analyzer.note_extraction import indice_de_hachure  # noqa: E402
from analyzer.tenues import reunir_tenues  # noqa: E402
from analyzer.vsm_reconstruct import StemNote  # noqa: E402

SR = 22050


def _accord(cents: float, secondes: float = 60.0) -> np.ndarray:
    t = np.arange(int(secondes * SR)) / SR
    y = np.zeros_like(t)
    for midi in (59, 61, 64, 66, 70):   # si, do♯, mi, fa♯, la♯ — l'accord de « Reload »
        f = 440.0 * 2 ** ((midi - 69 + cents / 100) / 12)
        y += np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t)
    return (y / 10).astype(np.float32)


@test
def le_diapason_d_un_accord_desaccorde_se_lit_a_un_cent_pres():
    for cents in (12.0, -8.0):
        lu = diapason.estimer_cents(_accord(cents), SR)
        assert_true(lu["cents"] is not None and abs(lu["cents"] - cents) <= 1.0, f"{cents} lu {lu}")


@test
def un_bruit_n_a_pas_de_diapason_et_le_dit():
    bruit = np.random.default_rng(3).normal(0, 0.1, 60 * SR).astype(np.float32)
    assert_equal(diapason.estimer_cents(bruit, SR)["cents"], None)


@test
def la_valeur_de_session_refuse_un_diapason_absurde():
    avant = diapason.valeur()
    try:
        diapason.poser(1000.0)
        assert_true(False, "1000 Hz accepté")
    except ValueError:
        pass
    assert_equal(diapason.valeur(), avant)
    diapason.poser(443.0)
    assert_equal(diapason.valeur(), 443.0)
    diapason.poser(440.0)


@test
def la_reunion_fond_un_son_tenu_et_laisse_une_note_rejouee():
    t = np.arange(4 * SR) / SR
    f = 440.0 * 2 ** ((66 - 69) / 12)
    # 0-2 s : un fa♯ TENU (amplitude constante), qui s'éteint de 2 à 2,995 s SANS
    # nouvelle attaque ; puis le même fa♯ REJOUÉ à 3 s (silence de 5 ms, attaque franche).
    # Trois jonctions : 1,0 et 2,0 sont à l'intérieur du même son (réunies), 3,0 est une
    # note rejouée (gardée). La première écriture de ce test attendait UNE réunion : elle
    # oubliait que 2,0 n'est pas une attaque — le module avait raison, le banc tort.
    y = np.sin(2 * np.pi * f * t) * 0.5
    env = np.ones_like(t)
    env[int(2.0 * SR):] = np.exp(-(t[int(2.0 * SR):] - 2.0) * 6)
    env[int(2.995 * SR):int(3.0 * SR)] = 0.0
    env[int(3.0 * SR):] = np.exp(-(t[int(3.0 * SR):] - 3.0) * 6)
    y = (y * env).astype(np.float32)
    # LE TYPE DU CHEMIN RÉEL : `reunir_tenues` reçoit des StemNote de la chaîne.
    notes = [StemNote(note=66, velocity=100, start=0.0, duration=1.0, confidence=1.0),    # tenu,
             StemNote(note=66, velocity=100, start=1.0, duration=1.0, confidence=1.0),    # haché en deux
             StemNote(note=66, velocity=100, start=2.0, duration=0.995, confidence=1.0),  # rejoué
             StemNote(note=66, velocity=100, start=3.0, duration=1.0, confidence=1.0)]
    sortie, bilan = reunir_tenues(notes, y, SR)
    assert_equal(bilan["reunions"], 2)
    assert_equal(bilan["refuseesParAttaque"], 1)
    assert_equal([(round(n.start, 3), round(n.start + n.duration, 3)) for n in sortie], [(0.0, 2.995), (3.0, 4.0)])


@test
def l_indice_de_hachure_compte_les_notes_suivies_de_la_meme_hauteur():
    ev = [(0.0, 1.0, 60), (1.01, 2.0, 60), (2.2, 3.0, 60), (0.0, 1.0, 64)]
    assert_true(abs(indice_de_hachure(ev) - 0.25) < 1e-9)
