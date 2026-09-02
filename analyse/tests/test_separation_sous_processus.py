"""La séparation vit dans un sous-processus qui meurt — et ça se teste sans demucs.

Le 02/09/2026, deux courses ont été abattues par l'OOM killer : torch et
demucs restaient résidents (~7 Go) dans le processus de la chaîne pendant que
Basic Pitch chargeait son propre modèle. `separer` lance désormais
`python -m analyzer.separation` en sous-processus — demucs vit, écrit ses
stems, meurt, et la mémoire revient. C'est la condition technique du modèle
six sources par défaut (§ 4.2 du CDC multipiste).

Ces tests injectent une COMMANDE factice : ils vérifient la plomberie — le
sous-processus reçoit les bons arguments, les stems écrits sont relevés, un
échec est une erreur nommée et jamais un dossier vide pris pour un résultat —
sans payer une séparation réelle. La séparation réelle, elle, se vérifie par
l'égalité bit à bit avec les stems de référence (déterminisme shifts=0),
consignée au CDC.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from reconstruire import construire_parseur, separer  # noqa: E402


def commande_factice(script: str):
    """Une « séparation » jouée par python -c : mêmes conventions d'appel."""
    return [sys.executable, "-c", script]


@test
def les_stems_ecrits_par_le_sous_processus_sont_releves():
    script = (
        "import sys, pathlib\n"
        "entree, dossier, modele = sys.argv[1], pathlib.Path(sys.argv[2]), sys.argv[3]\n"
        "dossier.mkdir(parents=True, exist_ok=True)\n"
        "assert modele == 'htdemucs_6s', modele\n"
        "for nom in ('bass', 'drums', 'guitar'):\n"
        "    (dossier / (nom + '.wav')).write_bytes(b'RIFF')\n"
    )
    with tempfile.TemporaryDirectory() as d:
        dossier = Path(d) / "stems"
        stems = separer(Path("/une/entree.mp3"), dossier, "htdemucs_6s",
                        commande=commande_factice(script))
    assert_equal(sorted(stems), ["bass", "drums", "guitar"], "les stems sont relevés")


@test
def un_sous_processus_qui_echoue_est_une_erreur_nommee():
    """Un code non nul ne doit JAMAIS passer pour « zéro stem trouvé » : c'est
    la différence entre une panne dite et un morceau silencieusement amputé.
    L'appelant (`obtenir_stems`) replie sur le mélange entier EN LE DISANT."""
    with tempfile.TemporaryDirectory() as d:
        try:
            separer(Path("/une/entree.mp3"), Path(d) / "stems", "htdemucs",
                    commande=commande_factice("import sys; sys.exit(7)"))
        except RuntimeError as erreur:
            assert_true("code 7" in str(erreur), "l'erreur porte le code : " + str(erreur))
        else:
            raise AssertionError("un échec du sous-processus doit lever")


@test
def un_succes_sans_stems_est_une_erreur_aussi():
    """Le sous-processus peut sortir à zéro sans avoir rien écrit (disque
    plein géré en silence, mauvais dossier…) : un dossier vide n'est pas une
    séparation, et le dire évite de reconstruire un morceau sur du néant."""
    with tempfile.TemporaryDirectory() as d:
        try:
            separer(Path("/une/entree.mp3"), Path(d) / "stems", "htdemucs",
                    commande=commande_factice(
                        "import sys, pathlib\n"
                        "pathlib.Path(sys.argv[2]).mkdir(parents=True, exist_ok=True)\n"))
        except RuntimeError as erreur:
            assert_true("aucun stem" in str(erreur), str(erreur))
        else:
            raise AssertionError("zéro stem doit lever, pas rendre un dict vide")


@test
def le_defaut_du_modele_est_six_sources():
    """La décision § 4.2 du CDC multipiste, gardée par un test : le défaut est
    passé à htdemucs_6s le 03/09/2026 (−10,4 % ET deux pistes de plus sur le
    témoin H22). Si quelqu'un le redescend, ce test le fera dire ici."""
    args = construire_parseur().parse_args(["morceau.mp3"])
    assert_equal(args.modele, "htdemucs_6s", "le défaut de --modele")


if __name__ == "__main__":
    raise SystemExit(run())
