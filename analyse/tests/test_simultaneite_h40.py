"""H40 (§ 13 du CDC recensement) : la simultanéité des notes entre deux grappes.

Ce que ces tests fixent, sur le type RÉEL des notes de la chaîne (`StemNote`,
ce que `extraire_notes` rend) : deux grappes qui alternent → 0 ; une nappe tenue
sous une mélodie → 1 ; moins de 3 notes → aucun témoignage (None) ; une note
qui commence EXACTEMENT à la fin d'une autre ne sonne pas avec elle.
"""
from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_reconstruct import StemNote  # noqa: E402
from analyzer.vsm_recensement import simultaneite  # noqa: E402


def n(debut: float, duree: float, hauteur: int = 60) -> StemNote:
    return StemNote(note=hauteur, velocity=90, start=debut, duration=duree)


@test
def deux_grappes_qui_alternent_ne_sont_pas_simultanees():
    a = [n(0.0, 0.4), n(1.0, 0.4), n(2.0, 0.4)]
    b = [n(0.5, 0.4), n(1.5, 0.4), n(2.5, 0.4)]
    assert_equal(simultaneite(a, b), 0.0)


@test
def une_nappe_sous_une_melodie_est_simultanee():
    nappe = [n(0.0, 4.0, 48), n(4.0, 4.0, 48), n(8.0, 4.0, 48)]
    melodie = [n(0.5, 0.3, 72), n(1.5, 0.3, 74), n(4.5, 0.3, 76), n(9.0, 0.3, 77)]
    assert_equal(simultaneite(nappe, melodie), 1.0)


@test
def moins_de_trois_notes_ne_temoignent_de_rien():
    assert_true(simultaneite([n(0.0, 1.0), n(1.0, 1.0)], [n(0.5, 0.1)] * 5) is None, "deux notes")


@test
def une_attaque_a_la_fin_exacte_d_une_note_ne_compte_pas():
    a = [n(0.0, 1.0), n(2.0, 1.0), n(4.0, 1.0)]
    b = [n(1.0, 0.5), n(3.0, 0.5), n(5.0, 0.5)]
    assert_equal(simultaneite(a, b), 0.0)


if __name__ == "__main__":
    raise SystemExit(run())
