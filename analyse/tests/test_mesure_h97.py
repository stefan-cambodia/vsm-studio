"""L'instrument de H97 (CDC-reload § 58) sur des profils connus, avant toute mesure."""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

import mesure_h97 as h97  # noqa: E402

ORIGINAL = {"sub": [9.75, -1.32, -3.41], "basse": [23.92, 13.69, 10.92]}       # § 55.2, stem drums
TR808 = {"sub": [3.77, 9.14, -7.04], "basse": [22.07, 0.71, -23.91]}           # § 55.2, kick de H70


@test
def test_h97_le_niveau_ne_compte_pas_la_forme_oui():
    assert_equal(h97.ecart_profil(ORIGINAL, ORIGINAL), 0.0)
    plus_fort = {k: [x + 12.0 for x in v] for k, v in ORIGINAL.items()}
    assert_equal(h97.ecart_profil(ORIGINAL, plus_fort), 0.0)
    assert_true(h97.ecart_profil(ORIGINAL, TR808) > 10.0, f"le kick de H70 est loin : {h97.ecart_profil(ORIGINAL, TR808)}")
    assert_equal(h97.resume(ORIGINAL)[4:], [23.92 - 9.75, 13.69 + 1.32, 10.92 + 3.41])


@test
def test_h97_chaque_chiffre_du_resume_pese():
    for bande in ("sub", "basse"):
        for k in (1, 2):
            p = {b: list(v) for b, v in ORIGINAL.items()}
            p[bande][k] -= 7.0
            assert_equal(h97.ecart_profil(ORIGINAL, p), 2.0)      # deux des sept chiffres bougent de 7 dB
    try:
        h97.resume({"sub": [1.0, 2.0], "basse": [1.0, 2.0, 3.0]})
    except ValueError:
        pass
    else:
        raise AssertionError("un profil tronqué a été lu")


@test
def test_h98_le_bas_medium_entre_dans_le_critere():
    o = {**ORIGINAL, "bas-médium": [20.0, 5.0, 0.0]}
    assert_equal(h97.ecart_profil3(o, {k: [x + 6.0 for x in v] for k, v in o.items()}), 0.0)
    gros = {**o, "bas-médium": [30.0, 15.0, 10.0]}          # un corps 10 dB plus fort, même forme
    assert_equal(h97.ecart_profil(o, gros), 0.0)             # l'ancien critère ne le voit pas
    assert_equal(h97.ecart_profil3(o, gros), round(30.0 / 12, 2))    # le neuf : trois chiffres sur douze, de 10 dB
    try:
        h97.ecart_profil3(ORIGINAL, o)
    except ValueError:
        pass
    else:
        raise AssertionError("un profil sans bas-médium a été comparé")


if __name__ == "__main__":
    raise SystemExit(run())
