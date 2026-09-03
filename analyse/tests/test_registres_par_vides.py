"""Les registres par les VIDES : le nombre de parties est LU, pas imposé.

`separer_en_voix` rend toujours son maximum de voix sur un fourre-tout ; sur
trois registres disjoints, il en fabrique quatre. `registres_par_vides` lit
les vides de la transcription. Ces tests fixent : le découpage aux vides, la
fusion des registres trop légers, le garde-fou du fourre-tout (une mélodie
qui saute d'octave reste une partie), et « pas de vide, rien à découper ».
"""
from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_reconstruct import StemNote, registres_par_vides  # noqa: E402


def couche(hauteurs, debut=0.0, duree=1.0, pas=1.0, nombre=40):
    """Une partie continue : `nombre` notes qui tournent sur `hauteurs`."""
    return [StemNote(note=hauteurs[i % len(hauteurs)], velocity=100,
                     start=debut + i * pas, duration=duree, confidence=0.9)
            for i in range(nombre)]


def fourre_tout_a_trois_registres():
    # trois couches simultanées (polyphonie 3) sur 60 demi-tons : un fourre-tout
    return (couche([36, 40, 43, 46]) + couche([60, 64, 67, 72]) + couche([84, 88, 91, 96]))


@test
def trois_registres_disjoints_donnent_trois_voix_de_l_aigu_au_grave():
    voix = registres_par_vides(fourre_tout_a_trois_registres())
    assert_equal(len(voix), 3, "trois registres, trois voix — pas quatre")
    bornes = [(min(n.note for n in v), max(n.note for n in v)) for v in voix]
    assert_equal(bornes, [(84, 96), (60, 72), (36, 46)], "de l'aigu au grave, registres intacts")
    assert_equal(sum(len(v) for v in voix), 120, "aucune note perdue ni doublée")


@test
def un_registre_trop_leger_rejoint_son_voisin():
    notes = fourre_tout_a_trois_registres()
    # une poussière isolée très haut (deux notes brèves) : une erreur d'octave,
    # pas une partie
    notes += [StemNote(note=108, velocity=60, start=3.0, duration=0.1, confidence=0.5),
              StemNote(note=110, velocity=60, start=7.0, duration=0.1, confidence=0.5)]
    voix = registres_par_vides(notes)
    assert_equal(len(voix), 3, "la poussière n'ouvre pas de quatrième voix")
    assert_true(any(n.note == 110 for n in voix[0]), "elle rejoint le registre le plus proche (l'aigu)")


@test
def une_melodie_qui_saute_d_octave_n_est_pas_decoupee():
    # une seule voix (polyphonie 1) qui alterne deux octaves : des vides, mais
    # pas un fourre-tout — le garde-fou la laisse entière
    voix = registres_par_vides(couche([60, 72, 64, 76, 67, 79], nombre=60))
    assert_equal(len(voix), 1, "pas un fourre-tout : rien à découper")


@test
def sans_vide_rien_a_decouper():
    # fourre-tout dense (polyphonie 3, 40 demi-tons), toutes les hauteurs jouées
    notes = (couche(list(range(48, 62))) + couche(list(range(60, 75)))
             + couche(list(range(74, 89))))
    voix = registres_par_vides(notes)
    assert_equal(len(voix), 1, "aucun vide : une seule liste, l'appelant passe à --voix-par-stem")


if __name__ == "__main__":
    raise SystemExit(run())
