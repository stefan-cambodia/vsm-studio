"""D20.4 — transcrire une PLAGE d'un fichier en notes (`transcrire_clip.py`).

Sur la basse du morceau minuscule commis, les notes rendues sont celles de la
vérité à ±1 demi-ton et ±50 ms, remises en secondes DANS LE FICHIER même
quand on ne transcrit qu'une plage ; une plage vide est une erreur nommée.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_banc import apparier  # noqa: E402
from transcrire_clip import transcrire_plage  # noqa: E402

FIXTURE = Path(__file__).resolve().parent / "donnees" / "banc-minuscule" / "morceau"


def _basse():
    verite = json.loads((FIXTURE / "verite.json").read_text(encoding="utf-8"))
    partie = next(p for p in verite["parties"] if p["role"] == "basse")
    return FIXTURE / partie["fichier"], [list(n) for n in partie["notes"]]


@test
def la_basse_du_morceau_minuscule_est_transcrite_comme_la_verite():
    fichier, vraies = _basse()
    resultat = transcrire_plage(fichier)
    notes = resultat["notes"]
    assert_true(len(notes) >= len(vraies), f"au moins {len(vraies)} notes : {len(notes)}")
    transcrites = [[n["note"], n["velocity"], n["start"], n["duration"]] for n in notes]
    paires = apparier(vraies, transcrites)
    assert_equal(len(paires), len(vraies), "chaque note vraie est appariée à ±1 demi-ton, ±50 ms")
    for n in notes:
        assert_true(0.0 <= n["confidence"] <= 1.0 and 1 <= n["velocity"] <= 127, "note bien formée : " + str(n))
    assert_equal(resultat["debut"], 0.0, "depuis le début")
    assert_true(resultat["secondes"] > 0, "le coût est publié")


@test
def une_plage_rend_ses_notes_en_secondes_dans_le_fichier():
    fichier, vraies = _basse()
    # La fin seulement, coupée ENTRE deux notes (la troisième finit à 1,38 s,
    # la quatrième commence à 1,48 s) : une note coupée en son milieu n'est
    # plus la note de la vérité, et ce n'est pas ce qu'on mesure ici. Les
    # instants rendus sont ceux du FICHIER, pas de l'extrait.
    milieu = 1.4
    resultat = transcrire_plage(fichier, debut=milieu)
    assert_equal(resultat["debut"], milieu, "la plage est rappelée")
    for n in resultat["notes"]:
        assert_true(n["start"] >= milieu - 0.06, f"une note à {n['start']} s est avant la plage")
    attendues = [v for v in vraies if v[2] >= milieu - 0.05]
    transcrites = [[n["note"], n["velocity"], n["start"], n["duration"]] for n in resultat["notes"]]
    assert_equal(len(apparier(attendues, transcrites)), len(attendues), "les notes de la plage sont là")


@test
def une_plage_vide_est_une_erreur_nommee():
    fichier, _ = _basse()
    try:
        transcrire_plage(fichier, debut=2.0, fin=1.0)
    except ValueError as erreur:
        assert_true("plage vide" in str(erreur), str(erreur))
    else:
        raise AssertionError("une plage vide doit lever")


if __name__ == "__main__":
    raise SystemExit(run())
