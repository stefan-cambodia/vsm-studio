"""Le chemin donné à la séparation survit au changement de dossier.

POURQUOI CE FICHIER EXISTE. `separer()` lance demucs dans un SOUS-PROCESSUS —
il le faut, torch et demucs restent résidents à ~7 Go et l'addition avec Basic
Pitch a fait abattre deux courses par l'OOM killer. Ce sous-processus tourne
dans `analyse/`, pour y trouver le module `analyzer.separation`.

Un chemin RELATIF donné en ligne de commande est relatif au dossier de
L'APPELANT, pas à `analyse/`. Il n'existait donc pas pour le sous-processus,
demucs rendait « No such file or directory », et la chaîne se repliait sur le
mélange entier : une course qui va au bout, rend un rapport et une distance,
d'un morceau reconstruit en UNE piste.
"""
from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_true, test  # noqa: E402

import reconstruire  # noqa: E402


@test
def le_chemin_relatif_est_resolu_avant_de_partir_au_sous_processus():
    vus = []

    def commande_factice(*_a, **_k):
        raise AssertionError("jamais appelée")

    # On intercepte `subprocess.run` pour LIRE la ligne de commande construite,
    # sans lancer quoi que ce soit : c'est la ligne qui portait le défaut.
    import subprocess
    vrai_run = subprocess.run

    class Resultat:
        returncode = 0

    def espion(argv, **kwargs):
        vus.append((list(argv), kwargs.get("cwd")))
        return Resultat()

    subprocess.run = espion
    try:
        try:
            reconstruire.separer(Path("reconstruction/travail/sources/x.wav"),
                                  Path("un/dossier"), "htdemucs_6s",
                                  commande=["faux"])
        except RuntimeError:
            pass  # « aucun stem » : attendu, rien n'a été séparé
    finally:
        subprocess.run = vrai_run

    assert_true(len(vus) == 1, "la commande a bien été construite")
    argv, cwd = vus[0]
    entree, sortie = argv[1], argv[2]
    assert_true(Path(entree).is_absolute(),
                f"le chemin d'ENTRÉE part en absolu (reçu {entree!r})")
    assert_true(Path(sortie).is_absolute(),
                f"le dossier de SORTIE part en absolu (reçu {sortie!r})")
    assert_true(cwd is not None and Path(cwd).name == "analyse",
                "et le sous-processus tourne bien dans analyse/, ce qui est la cause")
