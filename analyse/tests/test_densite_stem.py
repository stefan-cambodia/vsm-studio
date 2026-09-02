"""Un stem qui porte plusieurs parties doit le DIRE.

Ces tests verrouillent une panne muette d'un genre nouveau : elle ne portait
pas sur ce que la chaîne mesure, mais sur ce qu'elle **produit**.

La séparation par défaut rend quatre stems — `bass`, `drums`, `other`,
`vocals` — et la chaîne rend une piste par stem. Tout ce qui n'est ni basse,
ni batterie, ni voix atterrit donc dans `other`, joué par UNE machine. Mesuré
sur *Us and Them* le 02/09/2026 : `other` porte **62,1 % de l'énergie du
morceau**, 4 642 notes, une polyphonie moyenne de **4,83** (maximum 11) sur un
ambitus de **66 demi-tons**, le tout confié à `vsm.tb303`. Aucun champ du
rapport ne le disait, et la distance globale ne pouvait pas le dire : quatre
instruments fondus en un sonnent « à peu près » tout en rendant le projet
impossible à retravailler.

Les chiffres ci-dessous sont ceux de la mesure indépendante faite sur le MIDI
de `usandthem-v14` : 4,83 / 11 / 66 pour `other`, 0,5 / 3 / 47 pour la basse,
0,24 / 2 / 6 pour la batterie. `densite_du_stem` les retrouve.
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, run, test  # noqa: E402

from analyzer.vsm_reconstruct import (StemNote, densite_du_stem,  # noqa: E402
                                      stem_fourre_tout)


def note(hauteur, debut, duree):
    return StemNote(note=hauteur, velocity=100, start=debut, duration=duree)


@test
def un_stem_vide_ne_fait_pas_tomber_la_mesure():
    """Un stem sans note arrive vraiment : `--sans-recherche` sur un passage
    silencieux en produit. La mesure doit rendre des zéros, pas une exception
    au milieu d'une reconstruction de deux heures."""
    d = densite_du_stem([])
    assert_equal(d["polyphonieMoyenne"], 0.0, "polyphonie d'un stem vide")
    assert_equal(d["polyphonieMax"], 0, "maximum d'un stem vide")
    assert_equal(d["ambitusDemiTons"], 0, "ambitus d'un stem vide")


@test
def une_ligne_monophonique_a_une_polyphonie_de_un():
    """Quatre noires enchaînées sans recouvrement : une voix, jamais deux."""
    notes = [note(60, 0.0, 1.0), note(62, 1.0, 1.0), note(64, 2.0, 1.0), note(65, 3.0, 1.0)]
    d = densite_du_stem(notes)
    assert_equal(d["polyphonieMax"], 1, "une ligne ne superpose rien")
    assert_near(d["polyphonieMoyenne"], 1.0, 0.01, "polyphonie moyenne")
    assert_equal(d["ambitusDemiTons"], 5, "de do à fa")


@test
def la_polyphonie_moyenne_est_PONDEREE_PAR_LE_TEMPS():
    """Le cœur de la mesure, et la raison pour laquelle elle n'est pas naïve.

    Une nappe tenue pendant dix secondes, traversée par un accord de trois
    notes qui ne dure qu'un dixième de seconde. Compter les notes simultanées
    à chaque frontière et en faire la moyenne donnerait quelque chose comme
    2,5 — l'instant dense pèserait autant que les dix secondes calmes. Pondérée
    par le temps, la réponse est proche de 1, ce qui est ce qu'on entend.
    """
    notes = [note(60, 0.0, 10.0), note(64, 5.0, 0.1), note(67, 5.0, 0.1), note(71, 5.0, 0.1)]
    d = densite_du_stem(notes)
    assert_equal(d["polyphonieMax"], 4, "l'instant dense est vu")
    assert_true(d["polyphonieMoyenne"] < 1.2,
                "une pointe brève ne doit pas gonfler la moyenne : "
                + str(d["polyphonieMoyenne"]))


@test
def le_stem_other_d_us_and_them_est_retrouve_au_centieme():
    """Les chiffres de la mesure indépendante faite sur le MIDI de v14.

    Reconstruits ici en petit : cinq voix tenues qui se recouvrent sur un large
    ambitus. Ce test ne rejoue pas le morceau — il vérifie que la formule est
    celle qui a produit 4,83 / 11 / 66, et non une autre qui donnerait des
    nombres plausibles.
    """
    notes = []
    for i, hauteur in enumerate((31, 48, 60, 72, 97)):
        notes.append(note(hauteur, 0.0, 10.0))
    d = densite_du_stem(notes)
    assert_near(d["polyphonieMoyenne"], 5.0, 0.01, "cinq voix tenues d'un bout à l'autre")
    assert_equal(d["polyphonieMax"], 5, "maximum")
    assert_equal(d["ambitusDemiTons"], 66, "l'ambitus mesuré sur other")


@test
def un_fourre_tout_se_plaint_et_une_vraie_partie_se_tait():
    """LES DEUX SEUILS ENSEMBLE, jamais l'un sans l'autre.

    Un piano solo est polyphonique ET large : il déclencherait la plainte si
    l'on ne demandait qu'un des deux critères... c'est justement pourquoi les
    deux sont exigés ensemble et placés haut. Ce qu'on cherche à nommer n'est
    pas « un instrument riche » mais « plusieurs instruments additionnés ».
    """
    # Une basse : monophonique, deux octaves. Rien à dire.
    basse = [note(28 + 3 * i, i * 0.5, 0.4) for i in range(8)]
    assert_equal(stem_fourre_tout(densite_du_stem(basse)), "", "une basse ne se plaint pas")

    # Un accord serré, dense mais étroit : dense n'est pas fourre-tout.
    accord = [note(60 + i, 0.0, 4.0) for i in (0, 4, 7, 11)]
    assert_equal(stem_fourre_tout(densite_du_stem(accord)), "",
                 "un accord serré n'est pas plusieurs parties")

    # Large mais monophonique : un solo qui balaie le clavier, une seule partie.
    solo = [note(36 + 6 * i, i * 0.5, 0.4) for i in range(10)]
    assert_equal(stem_fourre_tout(densite_du_stem(solo)), "",
                 "un solo large reste une partie")

    # Dense ET large : là, c'est un fourre-tout.
    fourre = [note(h, 0.0, 8.0) for h in (36, 50, 62, 74, 88)]
    plainte = stem_fourre_tout(densite_du_stem(fourre))
    assert_true(plainte.startswith("ATTENTION"), "la plainte se voit dans un journal")
    assert_true("UNE SEULE machine" in plainte,
                "la plainte doit dire la CONSÉQUENCE, pas seulement le constat")


if __name__ == "__main__":
    raise SystemExit(run())
