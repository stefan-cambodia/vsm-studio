"""La provenance doit nommer le MOTEUR, pas seulement le commit.

Ces tests verrouillent une panne muette trouvée le 02/09/2026.

`rapport.json` inscrivait le commit du dépôt et rien sur le binaire qui a
rendu l'audio. Or le rendu ne sort pas du dépôt : il sort de
`build/tools/vsm-render`, qui peut dater d'avant le commit annoncé. Les
courses v13 et v14, terminées à 10:12 et 11:05, ont tourné avec un moteur
compilé à 08:48 — donc sans les sept machines écrites entre 09:13 et 10:17.
Leur rapport annonce un commit dont le vivier compte quarante-sept machines
mélodiques ; la course en a vu quarante et une, et **rien ne le disait**.

Le verdict de H13 n'en souffre pas : v13 et v14 partagent ce binaire, donc
leur comparaison n'a bien qu'une variable. Ce qui était faux, c'est le nombre
inscrit à côté — et il l'était en silence, ce que le cahier des charges
interdit, avec la circonstance aggravante que la panne touche l'instrument de
mesure lui-même.

Deux gardes, donc, et un test pour chacune :
  - `identite_du_moteur` met dans la provenance la date de compilation, la
    taille et le nombre de machines déclarées ;
  - `moteur_perime` rend une phrase à imprimer dès que le binaire est plus
    vieux qu'un fichier source du moteur.
"""

from __future__ import annotations

import os
import sys
import tempfile
import time
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from reconstruire import identite_du_moteur, moteur_perime, provenance  # noqa: E402


class MoteurFactice:
    """Le strict nécessaire : un chemin de binaire et une liste de machines."""

    def __init__(self, binary, machines):
        self.binary = binary
        self._machines = list(machines)

    def machines(self):
        return list(self._machines)


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
    with tempfile.TemporaryDirectory() as dossier:
        racine = Path(dossier)
        (racine / "analyse").mkdir()
        (racine / "audio" / "plugins" / "neuve").mkdir(parents=True)
        binaire = racine / "vsm-render"
        binaire.write_bytes(b"moteur")
        source = racine / "audio" / "plugins" / "neuve" / "NeuveSynth.h"
        source.write_text("// une machine ecrite APRES la compilation\n")

        vieux = time.time() - 3600
        os.utime(binaire, (vieux, vieux))

        # `moteur_perime` cherche les sources relativement au dossier parent
        # de `reconstruire.py` : on le fait pointer vers notre faux dépôt.
        import reconstruire
        vrai = reconstruire.__file__
        try:
            reconstruire.__file__ = str(racine / "analyse" / "reconstruire.py")
            plainte = moteur_perime(MoteurFactice(binaire, []))
        finally:
            reconstruire.__file__ = vrai

    assert_true(plainte is not None, "un moteur périmé doit se plaindre")
    assert_true("NeuveSynth.h" in plainte, "la plainte nomme le fichier : " + str(plainte))
    assert_true("ATTENTION" in plainte, "la plainte se voit dans un journal")


@test
def un_moteur_a_jour_ne_dit_rien():
    """La garde ne doit pas crier à tort : un journal qui crie toujours ne se
    lit plus, et c'est ainsi qu'on rate le seul avertissement qui comptait."""
    with tempfile.TemporaryDirectory() as dossier:
        racine = Path(dossier)
        (racine / "analyse").mkdir()
        (racine / "audio").mkdir()
        source = racine / "audio" / "Vieille.h"
        source.write_text("// une source ANTERIEURE au binaire\n")
        vieux = time.time() - 3600
        os.utime(source, (vieux, vieux))

        binaire = racine / "vsm-render"
        binaire.write_bytes(b"moteur")

        import reconstruire
        vrai = reconstruire.__file__
        try:
            reconstruire.__file__ = str(racine / "analyse" / "reconstruire.py")
            plainte = moteur_perime(MoteurFactice(binaire, []))
        finally:
            reconstruire.__file__ = vrai

    assert_true(plainte is None, "aucune plainte attendue, reçu : " + str(plainte))


@test
def seules_les_sources_du_MOTEUR_periment_le_moteur():
    """Un changement dans `app/` ne périme pas un rendu.

    Sans cette borne, toute retouche d'interface ferait crier la chaîne, et
    l'avertissement deviendrait un bruit de fond qu'on apprend à ignorer —
    c'est-à-dire l'inverse de ce qu'il est là pour faire.
    """
    with tempfile.TemporaryDirectory() as dossier:
        racine = Path(dossier)
        (racine / "analyse").mkdir()
        (racine / "audio").mkdir()
        (racine / "app" / "Source").mkdir(parents=True)
        binaire = racine / "vsm-render"
        binaire.write_bytes(b"moteur")
        # Écrit APRÈS le binaire, mais dans `app/` : sans effet sur le rendu.
        (racine / "app" / "Source" / "Ecran.cpp").write_text("// interface\n")

        import reconstruire
        vrai = reconstruire.__file__
        try:
            reconstruire.__file__ = str(racine / "analyse" / "reconstruire.py")
            plainte = moteur_perime(MoteurFactice(binaire, []))
        finally:
            reconstruire.__file__ = vrai

    assert_true(plainte is None, "app/ ne doit pas périmer le moteur, reçu : " + str(plainte))


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
    class Args:
        pass
    a = Args()
    for nom, valeur in dict(
            sans_separation=False, sans_sampler=False, sans_arbitrage=False,
            sans_arbitrage_batterie=False, sans_reglage_piste=False, sans_recherche=True,
            machines_au_melange=6, sans_reglage_melange=False, budget_melange=30,
            tours_verdict=3, garder_pieces_non_isolees=False, rendus_paralleles=8,
            sans_cache_rendus=False, budget_piste=120, axes_piste=21, finalistes=None,
            preselection_apprise=0, machines="", machines_exclues="",
            modele="htdemucs_6s", stems="").items():
        setattr(a, nom, valeur)

    p = provenance(a, None, None)
    assert_equal(p["options"]["modeleSeparation"], "htdemucs_6s",
                 "le modèle de séparation est inscrit")
    assert_true(p["options"]["stemsRepris"] is None, "aucun dossier de stems repris")

    # DES STEMS REPRIS D'UN DOSSIER : la séparation n'a PAS eu lieu, et dire
    # « htdemucs » serait alors un mensonge — le rapport nommerait un modèle
    # qui n'a pas tourné. Le champ vaut null, et le dossier est nommé.
    a.stems = "/un/dossier/de/stems"
    p = provenance(a, None, None)
    assert_true(p["options"]["modeleSeparation"] is None,
                "aucun modèle ne doit être nommé quand la séparation n'a pas eu lieu")
    assert_equal(p["options"]["stemsRepris"], "/un/dossier/de/stems",
                 "le dossier de stems repris est inscrit")


if __name__ == "__main__":
    raise SystemExit(run())
