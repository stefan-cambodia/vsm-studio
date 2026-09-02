"""La provenance doit nommer le MOTEUR, pas seulement le commit.

Ces tests verrouillent une panne muette trouvée le 02/09/2026, puis sa
récidive trouvée le même jour.

`rapport.json` inscrivait le commit du dépôt et rien sur le binaire qui a
rendu l'audio. Or le rendu ne sort pas du dépôt : il sort de
`build/tools/vsm-render`, qui peut dater d'avant le commit annoncé. Les
courses v13 et v14, terminées à 10:12 et 11:05, ont tourné avec un moteur
compilé à 08:48 — donc sans les sept machines écrites entre 09:13 et 10:17.
Leur rapport annonce un commit dont le vivier compte quarante-sept machines
mélodiques ; la course en a vu quarante et une, et **rien ne le disait**.

La récidive : la première version de la garde interrogeait le moteur au
moment d'écrire le rapport, donc APRÈS la fermeture du processus, et
retombait sur le repli « je ne sais pas » à chaque course — en silence, ce
que le rapport de v11a a montré (`compile: ""`). D'où la forme actuelle :
l'identité se CAPTURE moteur vivant, et la provenance la reçoit toute faite.

Les gardes vivent dans `analyzer/vsm_engine.py`, à côté de
`find_vsm_render` : tous les programmes qui créent un moteur (corpus, banc de
batterie, classifieur…) doivent pouvoir s'en servir — un corpus bâti sur un
moteur périmé empoisonne les modèles plus durablement qu'une course.
"""

from __future__ import annotations

import contextlib
import os
import sys
import tempfile
import time
import types
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_engine import identite_du_moteur, moteur_perime  # noqa: E402
from reconstruire import provenance  # noqa: E402


class MoteurFactice:
    """Le strict nécessaire : un chemin de binaire et une liste de machines."""

    def __init__(self, binary, machines):
        self.binary = binary
        self._machines = list(machines)

    def machines(self):
        return list(self._machines)


@contextlib.contextmanager
def depot_factice(source_du_moteur=None, source_hors_moteur=None):
    """Un faux dépôt : `audio/` (et `app/` au besoin), un binaire, une racine.

    Rend `(racine, moteur)`. Le binaire date d'il y a une heure ; les sources
    passées en argument sont écrites MAINTENANT, donc après lui.
    """
    with tempfile.TemporaryDirectory() as dossier:
        racine = Path(dossier)
        (racine / "audio").mkdir()
        binaire = racine / "vsm-render"
        binaire.write_bytes(b"moteur")
        vieux = time.time() - 3600
        os.utime(binaire, (vieux, vieux))
        if source_du_moteur is not None:
            chemin = racine / source_du_moteur
            chemin.parent.mkdir(parents=True, exist_ok=True)
            chemin.write_text("// ecrit APRES la compilation\n")
        if source_hors_moteur is not None:
            chemin = racine / source_hors_moteur
            chemin.parent.mkdir(parents=True, exist_ok=True)
            chemin.write_text("// ecrit APRES la compilation, hors moteur\n")
        yield racine, MoteurFactice(binaire, [])


@test
def l_identite_du_moteur_porte_la_date_la_taille_et_le_compte():
    with tempfile.TemporaryDirectory() as dossier:
        binaire = Path(dossier) / "vsm-render"
        binaire.write_bytes(b"x" * 1234)
        identite = identite_du_moteur(MoteurFactice(binaire, ["vsm.a", "vsm.b", "vsm.c"]))

    assert_equal(identite["octets"], 1234, "taille du binaire")
    assert_equal(identite["machines"], 3, "machines déclarées")
    assert_true(identite["chemin"].endswith("vsm-render"), "chemin du binaire")
    # Une date ISO, pas un horodatage brut : elle se lit dans le rapport.
    assert_true(identite["compile"].count("-") >= 2 and "T" in identite["compile"],
                "date de compilation en ISO : " + identite["compile"])


@test
def un_moteur_indescriptible_le_dit_au_lieu_de_planter():
    """Un binaire introuvable ne doit pas faire tomber la course.

    La provenance est un compte rendu, pas une condition d'exécution : elle
    dit « je ne sais pas » plutôt que d'interrompre un rendu de deux heures.
    """
    identite = identite_du_moteur(MoteurFactice("/n/existe/pas/vsm-render", []))
    assert_equal(identite["octets"], 0, "taille inconnue")
    assert_equal(identite["machines"], 0, "compte inconnu")


@test
def un_moteur_plus_vieux_que_ses_sources_se_plaint_en_nommant_le_fichier():
    """Le cœur de la garde : la phrase doit NOMMER le fichier plus récent.

    Un avertissement qui dirait seulement « le moteur est périmé » laisserait
    chercher lequel des mille fichiers a bougé.
    """
    with depot_factice(source_du_moteur="audio/plugins/neuve/NeuveSynth.h") as (racine, moteur):
        plainte = moteur_perime(moteur, racine=racine)
    assert_true(plainte is not None, "un moteur périmé doit se plaindre")
    assert_true("NeuveSynth.h" in plainte, "la plainte nomme le fichier : " + str(plainte))
    assert_true("ATTENTION" in plainte, "la plainte se voit dans un journal")


@test
def un_moteur_a_jour_ne_dit_rien():
    """La garde ne doit pas crier à tort : un journal qui crie toujours ne se
    lit plus, et c'est ainsi qu'on rate le seul avertissement qui comptait."""
    with depot_factice() as (racine, moteur):
        # La seule source est ANTÉRIEURE au binaire.
        source = racine / "audio" / "Vieille.h"
        source.write_text("// une source anterieure\n")
        encore_plus_vieux = time.time() - 7200
        os.utime(source, (encore_plus_vieux, encore_plus_vieux))
        plainte = moteur_perime(moteur, racine=racine)
    assert_true(plainte is None, "aucune plainte attendue, reçu : " + str(plainte))


@test
def seules_les_sources_du_MOTEUR_periment_le_moteur():
    """Un changement dans `app/` ne périme pas un rendu.

    Sans cette borne, toute retouche d'interface ferait crier la chaîne, et
    l'avertissement deviendrait un bruit de fond qu'on apprend à ignorer —
    c'est-à-dire l'inverse de ce qu'il est là pour faire.
    """
    with depot_factice(source_hors_moteur="app/Source/Ecran.cpp") as (racine, moteur):
        plainte = moteur_perime(moteur, racine=racine)
    assert_true(plainte is None, "app/ ne doit pas périmer le moteur, reçu : " + str(plainte))


def _args(**surcharges):
    """Les options minimales que `provenance` lit, en un seul endroit."""
    base = dict(
        sans_separation=False, sans_sampler=False, sans_arbitrage=False,
        sans_arbitrage_batterie=False, sans_reglage_piste=False, sans_recherche=True,
        machines_au_melange=6, sans_reglage_melange=False, budget_melange=30,
        tours_verdict=3, garder_pieces_non_isolees=False, rendus_paralleles=8,
        sans_cache_rendus=False, budget_piste=120, axes_piste=21, finalistes=None,
        preselection_apprise=0, machines="", machines_exclues="",
        modele="htdemucs", stems="", voix_par_stem=0, batterie_par_piece=False, voix_tete_choeurs=False,
        seuil_stem=0.5)
    base.update(surcharges)
    return types.SimpleNamespace(**base)


@test
def le_modele_de_separation_va_dans_la_provenance():
    """L'OPTION QUI DÉCIDE DU NOMBRE DE PISTES, et qui n'y était pas.

    `--modele` choisit le modèle de séparation : `htdemucs` rend quatre stems,
    `htdemucs_6s` en rend six — donc quatre pistes contre six. Deux rapports
    séparés par des modèles différents ne décrivent pas le même morceau : l'un
    met le piano et la guitare dans `other`, l'autre leur donne une piste
    chacun. C'est, de toutes les options de la chaîne, celle qui conditionne le
    plus lourdement le résultat, et elle manquait à la provenance.
    """
    p = provenance(_args(modele="htdemucs_6s"), None, None)
    assert_equal(p["options"]["modeleSeparation"], "htdemucs_6s",
                 "le modèle de séparation est inscrit")
    assert_true(p["options"]["stemsRepris"] is None, "aucun dossier de stems repris")

    # DES STEMS REPRIS D'UN DOSSIER : la séparation n'a PAS eu lieu, et dire
    # « htdemucs » serait alors un mensonge — le rapport nommerait un modèle
    # qui n'a pas tourné. Le champ vaut null, et le dossier est nommé.
    p = provenance(_args(stems="/un/dossier/de/stems"), None, None)
    assert_true(p["options"]["modeleSeparation"] is None,
                "aucun modèle ne doit être nommé quand la séparation n'a pas eu lieu")
    assert_equal(p["options"]["stemsRepris"], "/un/dossier/de/stems",
                 "le dossier de stems repris est inscrit")


@test
def la_provenance_reprend_l_identite_capturee_moteur_vivant():
    """LA RÉCIDIVE VERROUILLÉE : `provenance` ne doit plus interroger le
    moteur — elle reçoit une identité déjà capturée, et l'inscrit telle
    quelle. La première forme appelait `machines()` après la fermeture du
    processus et publiait « je ne sais pas » à chaque course, en silence."""
    identite = {"chemin": "/x/vsm-render", "compile": "2026-09-02T13:17:23",
                "octets": 156023512, "machines": 48}
    p = provenance(_args(), None, None, identite)
    assert_equal(p["moteur"], identite, "l'identité passe telle quelle")
    # Sans identité : null, qui se voit — jamais un moteur à moitié décrit.
    p = provenance(_args(), None, None)
    assert_true(p["moteur"] is None, "pas d'identité inventée")


if __name__ == "__main__":
    raise SystemExit(run())
