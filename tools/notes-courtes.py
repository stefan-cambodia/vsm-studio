#!/usr/bin/env python3
"""D264/B15 : le transcripteur jette-t-il les notes brèves, et les retrouve-t-on ?

    analyse/.venv/bin/python tools/notes-courtes.py 127.7 30 --morceaux 3

POURQUOI (13/09/2026). D260 a mesuré que la chaîne rate 96,7 % des notes de moins
de 150 ms. `analyzer/note_extraction.py` appelle `basic_pitch.inference.predict`
sans aucun argument, et le défaut de cette fonction est
`minimum_note_length = 127.7` millisecondes : toute note plus courte est
SUPPRIMÉE par le transcripteur avant que la chaîne la voie.

CE QUE CE BANC MESURE, ET POURQUOI AINSI. Il transcrit les **stems VRAIS** du
corpus synthétique (`stems-vrais/NN-role.wav`), dont les notes sont connues
exactement (`verite.json`). La séparation est donc hors du chemin : une valeur de
`minimum_note_length` est la SEULE variable entre deux passes, et le témoin est
la valeur d'aujourd'hui, passée au même code par la même ligne de commande.
Jamais une constante éditée entre deux mesures.

LA RÈGLE DE CORRESPONDANCE, écrite avant de compter, et la même pour toutes les
valeurs : une note vraie (hauteur h, début t) est RETROUVÉE s'il existe une note
transcrite de hauteur h commençant à moins de TOLERANCE secondes de t. Une note
transcrite est INVENTÉE si aucune note vraie de sa hauteur ne sonne à son début.
C'est la règle de `tools/confiance-contre-verite.py`, pour que les deux mesures
se comparent.
"""
from __future__ import annotations

import argparse
import json
import os
from bisect import bisect_left
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
CORPUS_DEFAUT = RACINE / "reconstruction/travail/s1-sec"
# Le corpus se DÉSIGNE (--corpus) : il y en a désormais plus d'un, et un outil
# qui n'en connaîtrait qu'un mesurerait toujours l'ancien en croyant juger le neuf.
CORPUS = CORPUS_DEFAUT
TOLERANCE = float(os.environ.get("VSM_TOLERANCE", "0.05"))   # secondes
COURTE = float(os.environ.get("VSM_COURTE", "0.150"))        # la frontière de D260


def retrouvee(debuts: dict[int, list[float]], hauteur: int, debut: float) -> bool:
    liste = debuts.get(hauteur)
    if not liste:
        return False
    i = bisect_left(liste, debut)
    for j in (i - 1, i):
        if 0 <= j < len(liste) and abs(liste[j] - debut) <= TOLERANCE:
            return True
    return False


def mesurer(valeur: float, morceaux: list[Path]) -> dict[str, float]:
    """Transcrit chaque stem vrai à cette valeur, et compte."""
    from basic_pitch import ICASSP_2022_MODEL_PATH
    from basic_pitch.inference import predict

    vraies_courtes = trouvees_courtes = 0
    vraies_longues = trouvees_longues = 0
    ecrites = inventees = ecrites_courtes = 0
    for dossier in morceaux:
        verite = json.loads((dossier / "verite.json").read_text(encoding="utf-8"))
        for partie in verite.get("parties", []):
            fichier = dossier / partie["fichier"]
            if not fichier.is_file():
                print(f"  ATTENTION : {fichier} absent, partie ignorée")
                continue
            _, _, evenements = predict(str(fichier),
                                       model_or_model_path=ICASSP_2022_MODEL_PATH,
                                       minimum_note_length=valeur)
            transcrites: dict[int, list[float]] = {}
            for e in evenements:
                transcrites.setdefault(int(e[2]), []).append(float(e[0]))
            for liste in transcrites.values():
                liste.sort()

            # Les notes VRAIES de cette partie, et ce qu'il en advient.
            for note in partie_notes(verite, partie):
                hauteur, debut, duree = int(note[0]), float(note[2]), float(note[3])
                court = duree < COURTE
                if court:
                    vraies_courtes += 1
                    trouvees_courtes += 1 if retrouvee(transcrites, hauteur, debut) else 0
                else:
                    vraies_longues += 1
                    trouvees_longues += 1 if retrouvee(transcrites, hauteur, debut) else 0

            # LE REVERS : ce qui est écrit sans qu'aucune note vraie le justifie.
            vraies_debuts: dict[int, list[float]] = {}
            for note in partie_notes(verite, partie):
                vraies_debuts.setdefault(int(note[0]), []).append(float(note[2]))
            for liste in vraies_debuts.values():
                liste.sort()
            for e in evenements:
                ecrites += 1
                if float(e[1]) - float(e[0]) < COURTE:
                    ecrites_courtes += 1
                if not retrouvee(vraies_debuts, int(e[2]), float(e[0])):
                    inventees += 1
    return {
        "vraies_courtes": vraies_courtes,
        "rappel_courtes": trouvees_courtes / vraies_courtes if vraies_courtes else 0.0,
        "vraies_longues": vraies_longues,
        "rappel_longues": trouvees_longues / vraies_longues if vraies_longues else 0.0,
        "ecrites": ecrites,
        "ecrites_courtes": ecrites_courtes,
        "part_inventee": inventees / ecrites if ecrites else 0.0,
    }


def partie_notes(verite: dict, partie: dict) -> list:
    """Les notes d'une partie : [hauteur, vélocité, début, durée].

    `verite.json` porte le COMPTE dans `partie["notes"]` et la liste dans
    `verite["parties_notes"]` ou dans la partie elle-même selon la version du
    corpus — on cherche la liste, et l'on DIT si on ne la trouve pas plutôt que
    de rendre zéro en silence.
    """
    brut = partie.get("notes")
    if isinstance(brut, list):
        return brut
    raise SystemExit(
        "verite.json ne porte pas la liste des notes par partie "
        f"(partie['notes'] = {type(brut).__name__}) — le banc ne devine pas"
    )


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("valeurs", nargs="+", type=float,
                   help="les minimum_note_length à comparer, en ms ; la PREMIÈRE est le témoin")
    p.add_argument("--morceaux", type=int, default=3, help="combien de morceaux du corpus")
    p.add_argument("--corpus", type=Path, default=CORPUS_DEFAUT,
                   help="dossier du lot du banc synthétique (défaut : s1-sec)")
    a = p.parse_args()

    morceaux = sorted(a.corpus.glob("morceau-*"))[: a.morceaux]
    if not morceaux:
        print(f"REFUS : aucun morceau sous {a.corpus}")
        return 2
    print(f"{len(morceaux)} morceau(x) du corpus, stems VRAIS, tolérance {TOLERANCE * 1000:.0f} ms, "
          f"note courte < {COURTE * 1000:.0f} ms")
    print(f"{'min_note_length':>16} {'vraies <150':>12} {'rappel <150':>12} "
          f"{'rappel >=150':>12} {'écrites':>9} {'inventées':>10}")
    for i, valeur in enumerate(a.valeurs):
        r = mesurer(valeur, morceaux)
        marque = "  (témoin)" if i == 0 else ""
        print(f"{valeur:14.1f} ms {r['vraies_courtes']:12d} {100 * r['rappel_courtes']:11.1f}% "
              f"{100 * r['rappel_longues']:11.1f}% {r['ecrites']:9d} "
              f"{100 * r['part_inventee']:9.1f}%{marque}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
