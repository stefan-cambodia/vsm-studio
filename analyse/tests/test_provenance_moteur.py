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

from reconstruire import identite_du_moteur, moteur_perime  # noqa: E402


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


if __name__ == "__main__":
    raise SystemExit(run())
