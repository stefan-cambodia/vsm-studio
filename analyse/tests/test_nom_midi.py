"""Un nom de piste ne doit JAMAIS faire tomber l'écriture du projet.

TROUVÉ PAR LA PARITÉ (03/09/2026). Le format MIDI écrit ses méta-textes en
Latin-1 — contrainte du format, pas de nous — et `mido` lève
`UnicodeEncodeError` sur tout caractère hors de cette table. Le défaut a dormi
tant que les pistes s'appelaient « bass », « other » ou « Batterie ». Il est
tombé au premier nom composé par la chaîne elle-même : **« Voix · chœurs »**,
dont le « œ » n'existe pas en Latin-1. Il a fait tomber TOUTE la
reconstruction à l'écriture du projet — après le calcul, donc au pire moment.

La règle retenue : translittérer ce qu'on sait remplacer sans perdre le sens,
remplacer le reste plutôt qu'abandonner. Un nom approché dans le MIDI vaut
mieux qu'un projet perdu ; le nom complet survit dans `project.json`, qui est
de l'UTF-8, et c'est celui que l'application affiche.
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_project_export import nom_midi_lisible  # noqa: E402


@test
def le_nom_qui_a_fait_tomber_la_chaine_passe_maintenant():
    assert_equal(nom_midi_lisible("Voix · chœurs"), "Voix - choeurs",
                 "le nom qui a coûté une reconstruction entière")
    assert_equal(nom_midi_lisible("Voix · tête"), "Voix - tête",
                 "« ê » EST du Latin-1 : on ne l'abîme pas")


@test
def tous_les_noms_que_la_chaine_compose_s_encodent():
    """Les formes réellement produites par les découpages : voix par
    registres, batterie par pièce, tête et chœurs."""
    for nom in ("other · voix 1", "Batterie · kick+tom", "Batterie · autres",
                "Voix · chœurs", "guitar · voix 3"):
        # C'est le nom TRANSLITTÉRÉ qui doit s'encoder — le brut, non, et
        # c'est tout l'objet de la fonction. La première version de ce test
        # encodait le brut « pour vérifier », et échouait donc exactement là
        # où le correctif marche.
        assert_true(nom_midi_lisible(nom).encode("latin-1", errors="strict"),
                    f"« {nom} » doit s'encoder une fois translittéré")


@test
def un_caractere_inconnu_est_remplace_et_non_fatal():
    """Un nom venu d'un import (un projet Live nommé en japonais, par
    exemple) ne doit pas coûter le projet."""
    approche = nom_midi_lisible("ピアノ 1")
    approche.encode("latin-1", errors="strict")
    assert_true("1" in approche, "ce qui est encodable survit : " + approche)


@test
def un_nom_ordinaire_ne_bouge_pas():
    """La translittération ne doit pas abîmer ce qui allait bien : c'est le
    risque d'une correction trop large."""
    for nom in ("bass", "other", "Batterie", "Voix", "Lead Été", "kick2"):
        assert_equal(nom_midi_lisible(nom), nom, f"« {nom} » ne doit pas bouger")


if __name__ == "__main__":
    raise SystemExit(run())
