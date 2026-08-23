"""A1.3 — le classifieur dans la chaîne : consigné, jamais suivi par défaut.

La mesure a tranché avant que le code ne soit écrit : sur *Clair de Lune*, le
classifieur place `vsm.piano` — la machine que l'arbitrage retient réellement —
au rang médian 16 sur 20. S'en servir pour dégrossir aurait éliminé la
gagnante. Ce que ces tests verrouillent est donc la RETENUE du dispositif :

  - avec un classifieur, le verdict est le MÊME que sans ;
  - son avis est tout de même enregistré, pour qu'on puisse un jour juger sur
    pièces s'il mérite qu'on le suive ;
  - la présélection apprise existe, mais il faut la demander.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer.vsm_classifier import charge_corpus, entraine  # noqa: E402
from analyzer.vsm_corpus_build import (GrilleDeNotes, genere_lot, machine_fingerprint,
                                        nouveau_manifeste)  # noqa: E402
from analyzer.vsm_engine import Note, VsmEngine  # noqa: E402
from analyzer.vsm_patch_optimizer import search_space_for_machine  # noqa: E402
from analyzer.vsm_reconstruct import StemNote, reconstruct_stem  # noqa: E402

SR = 44100
MACHINES = ("vsm.minimoog", "vsm.juno106", "vsm.dx7")
GRILLE = GrilleDeNotes(hauteurs=(48, 64), durees=(0.5,), velocites=(60, 105))
_CACHE: dict = {}


def modele_et_cible():
    """Un petit classifieur et un stem d'essai, engendrés une fois."""
    if "modele" in _CACHE:
        return _CACHE["modele"], _CACHE["audio"], _CACHE["notes"]

    dossier = Path(tempfile.mkdtemp(prefix="vsm-a13-"))
    with VsmEngine(sample_rate=SR) as moteur:
        manifeste = nouveau_manifeste(SR, 5, 10, GRILLE, [], "2026-08-23T00:00:00+00:00")
        for machine in MACHINES:
            manifeste.empreintes[machine] = machine_fingerprint(moteur, machine, SR)
            (dossier / machine).mkdir(parents=True, exist_ok=True)
            lot = genere_lot(machine, search_space_for_machine(machine, moteur), moteur,
                             patchs=10, grille=GRILLE, graine=5, sample_rate=SR)
            lot.enregistre(dossier / machine / "lot-000.npz")
            manifeste.exemples[machine] = len(lot.X)
        manifeste.enregistre(dossier / "manifeste.json")

        # La CIBLE est un rendu du Juno : un son que le parc sait produire, donc
        # un cas où le classifieur a toutes ses chances. S'il devait dévier le
        # verdict quelque part, ce serait ici.
        audio = moteur.render("vsm.juno106", {}, [Note(60, 100, 0.0, 0.8)], 1.2)

    modele, _ = entraine(charge_corpus(dossier), graine=5, part_epreuve=0.3)
    notes = [StemNote(note=60, velocity=100, start=0.0, duration=0.8)]
    _CACHE.update(modele=modele, audio=audio, notes=notes)
    return modele, audio, notes


@test
def integration_le_classifieur_ne_change_PAS_le_verdict():
    """Le point entier d'A1.3 : par défaut, il conseille et ne décide pas."""
    modele, audio, notes = modele_et_cible()
    with VsmEngine(sample_rate=SR) as moteur:
        sans = reconstruct_stem("essai", audio, notes, moteur, sample_rate=SR,
                                machines=list(MACHINES), max_iterations=4, shortlist=0)
        avec = reconstruct_stem("essai", audio, notes, moteur, sample_rate=SR,
                                machines=list(MACHINES), max_iterations=4, shortlist=0,
                                classifieur=modele)
    assert_true(sans is not None and avec is not None, "les deux reconstructions aboutissent")
    assert_equal(avec.machine, sans.machine, "MÊME machine retenue")
    assert_true(abs(avec.distance - sans.distance) < 1e-9, "MÊME distance")
    assert_equal(sorted(m for m, _ in avec.considered), sorted(m for m, _ in sans.considered),
                 "MÊMES candidates mesurées : rien n'a été écarté")


@test
def integration_l_avis_du_classifieur_est_bien_consigne():
    """Consigner sans suivre n'est pas une demi-mesure : c'est ce qui permettra
    de juger sur pièces, exécution après exécution."""
    modele, audio, notes = modele_et_cible()
    with VsmEngine(sample_rate=SR) as moteur:
        resultat = reconstruct_stem("essai", audio, notes, moteur, sample_rate=SR,
                                    machines=list(MACHINES), max_iterations=4,
                                    shortlist=0, classifieur=modele)
    assert_true(resultat.classifier_ranking or resultat.classifier_abstention,
                "l'avis doit être là, classement OU abstention motivée")
    if resultat.classifier_ranking:
        machines = [m for m, _ in resultat.classifier_ranking]
        assert_true(all(m in MACHINES for m in machines), "des machines du parc")
        scores = [s for _, s in resultat.classifier_ranking]
        assert_true(scores == sorted(scores, reverse=True), "classement décroissant")


@test
def integration_la_preselection_apprise_doit_etre_DEMANDEE():
    """Elle existe — il faut pouvoir l'essayer — mais elle est éteinte par
    défaut, et l'allumer se voit : le nombre de candidates mesurées tombe."""
    modele, audio, notes = modele_et_cible()
    with VsmEngine(sample_rate=SR) as moteur:
        complet = reconstruct_stem("essai", audio, notes, moteur, sample_rate=SR,
                                   machines=list(MACHINES), max_iterations=4,
                                   shortlist=0, classifieur=modele)
        restreint = reconstruct_stem("essai", audio, notes, moteur, sample_rate=SR,
                                     machines=list(MACHINES), max_iterations=4,
                                     shortlist=0, classifieur=modele,
                                     preselection_apprise=1)
    if restreint.classifier_abstention:
        # Le modèle s'est abstenu : la présélection ne s'applique pas, et c'est
        # le comportement voulu — on ne dégrossit pas sur un avis qu'on refuse.
        assert_equal(len(restreint.considered), len(complet.considered),
                     "une abstention ne doit RIEN écarter")
    else:
        assert_true(len(restreint.considered) < len(complet.considered),
                    "demandée, la présélection écarte réellement des candidates")
