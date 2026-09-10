"""Ce qu'un classifieur de machine SAIT nommer parmi les candidates.

POURQUOI CE FICHIER EXISTE. `verifie_fraicheur` vérifie que le SON des machines
connues n'a pas bougé — elle bâtit son manifeste sur les empreintes du modèle
lui-même, et ne peut donc rien dire des machines qu'il n'a JAMAIS entendues. Le
parc, lui, s'élargit sans arrêt : un modèle entraîné sur vingt machines reste
« frais » pendant que le vivier en compte soixante-trois, et la ligne imprimée
au chargement — « empreintes vérifiées » — se lit comme une approbation.

Ces trois tests tiennent la contrepartie : la couverture se calcule, elle se
compte, et elle ne se tait pas.
"""
from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer.vsm_classifier import Classifieur  # noqa: E402


def _modele(noms):
    """Un classifieur minimal : seuls ses NOMS comptent pour la couverture."""
    import numpy as np
    return Classifieur(noms=list(noms), moyenne=np.zeros(1), echelle=np.ones(1),
                       modele=None, empreintes={n: "x" for n in noms},
                       seuil_abstention=0.2, rayon_nouveaute=1.0,
                       reference_nouveaute=np.zeros((1, 1)), graine=0, date="",
                       versions={})


@test
def un_modele_qui_couvre_tout_le_vivier_ne_laisse_rien_d_inconnu():
    m = _modele(["vsm.a", "vsm.b"])
    connues, inconnues = m.couverture(["vsm.a", "vsm.b"])
    assert_equal(connues, ["vsm.a", "vsm.b"], "les deux sont connues")
    assert_equal(inconnues, [], "et rien n'est inconnu")


@test
def les_machines_jamais_entendues_sont_nommees_une_par_une():
    # LE CAS RÉEL, ET IL EST MESURÉ : le modèle du 28/08 connaît vingt machines
    # quand le parc en compte soixante-trois. Ce n'est pas une hypothèse.
    m = _modele(["vsm.a", "vsm.b"])
    connues, inconnues = m.couverture(["vsm.a", "vsm.c", "vsm.b", "vsm.d"])
    assert_equal(connues, ["vsm.a", "vsm.b"], "les connues, dans l'ordre des candidates")
    assert_equal(inconnues, ["vsm.c", "vsm.d"], "les inconnues sont NOMMÉES, pas comptées")


@test
def la_couverture_ne_ment_pas_quand_le_modele_connait_des_machines_hors_lice():
    # Un modèle peut connaître des machines qui ne concourent pas : elles ne
    # comptent NI comme couverture NI comme manque. Ce qui se mesure est ce
    # que le modèle sait dire DE CE QU'ON LUI DEMANDE.
    m = _modele(["vsm.a", "vsm.b", "vsm.z"])
    connues, inconnues = m.couverture(["vsm.a", "vsm.c"])
    assert_equal(connues, ["vsm.a"], "une seule des deux candidates est connue")
    assert_equal(inconnues, ["vsm.c"], "l'autre est inconnue")
    assert_true("vsm.z" not in connues and "vsm.z" not in inconnues,
                "la machine hors lice ne pèse d'aucun côté")
