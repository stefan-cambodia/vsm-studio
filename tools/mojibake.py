#!/usr/bin/env python3
"""LE MOJIBAKE DANS LES CHAÎNES DU CODE : des octets UTF-8 relus en Latin-1, puis réécrits en UTF-8.

RÈGLE GARDÉE (30/10/2026, D604). Un littéral C++ qui contient un caractère de U+00C2 à U+00F4 suivi d'un caractère de
U+0080 à U+00BF n'est pas du français : c'est la LECTURE LATIN-1 D'UNE SÉQUENCE UTF-8 — « Ã© » pour « é », « Â« » pour
« « », « â » suivi de deux caractères de contrôle invisibles pour « — ». L'interface et les relevés l'écrivent tel quel.

POURQUOI ELLE EXISTE. Payé TROIS FOIS le même jour (D600 : « allumÃ© » au relevé du R ; D604 : « Â« Cycle 1 Â» …
fenÃªtre » au relevé du zoom ; et une troisième fois, prise avant le commit) : une édition SCRIPTÉE écrivait du C++ depuis
une chaîne Python non brute, et Python interprétait `\\xc3\\xa9` en deux caractères au lieu de laisser l'échappement au
compilateur. `echappements-hex.py` ne le voit pas — il cherche les échappements qui avalent la lettre suivante, pas les
octets déjà décodés —, et le compilateur non plus : la chaîne est valide.

CE QU'ELLE NE REGARDE PAS : les COMMENTAIRES, qui racontent le piège en le montrant (« « Ã© » »), et les mots français
où « Â » est suivi d'une lettre (« PÂLIE ») — la règle exige un caractère de U+0080 à U+00BF derrière.

    tools/mojibake.py [fichiers…]   (sans argument : app/, core/, audio/, interchange/, panels/, clap/ ; .cpp et .h)
Rend 0 si aucun littéral n'en porte, 1 sinon (chaque cas nommé : fichier, ligne, extrait).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
DOSSIERS = ("app", "core", "audio", "interchange", "panels", "clap")
MOTIF = re.compile("[Â-ô][\u0080-¿]")


def litteraux(texte: str) -> list[tuple[int, str]]:
    """Les littéraux de chaîne d'un source C++ (hors commentaires), avec leur ligne. Les littéraux bruts R"(…)" compris."""
    sortie: list[tuple[int, str]] = []
    i, n, ligne = 0, len(texte), 1
    while i < n:
        c = texte[i]
        if c == "\n":
            ligne += 1
            i += 1
        elif texte.startswith("//", i):
            j = texte.find("\n", i)
            i = n if j < 0 else j
        elif texte.startswith("/*", i):
            j = texte.find("*/", i + 2)
            fin = n if j < 0 else j + 2
            ligne += texte.count("\n", i, fin)
            i = fin
        elif c == "R" and re.match(r'R"([^(\s]{0,16})\(', texte[i:i + 20]):
            m = re.match(r'R"([^(\s]{0,16})\(', texte[i:i + 20])
            assert m is not None
            fin = texte.find(")" + m.group(1) + '"', i + m.end())
            fin = n if fin < 0 else fin + len(m.group(1)) + 2
            sortie.append((ligne, texte[i:fin]))
            ligne += texte.count("\n", i, fin)
            i = fin
        elif c == '"':
            j = i + 1
            while j < n and texte[j] != '"' and texte[j] != "\n":
                j += 2 if texte[j] == "\\" else 1
            sortie.append((ligne, texte[i:j + 1]))
            i = j + 1
        elif c == "'":
            j = i + 1
            while j < n and texte[j] != "'" and texte[j] != "\n":
                j += 2 if texte[j] == "\\" else 1
            i = j + 1
        else:
            i += 1
    return sortie


def fichiers(arguments: list[str]) -> list[Path]:
    if arguments:
        return [Path(a) for a in arguments]
    trouves: list[Path] = []
    for dossier in DOSSIERS:
        base = RACINE / dossier
        if base.is_dir():
            trouves += [p for p in base.rglob("*") if p.suffix in (".cpp", ".h") and "build" not in p.parts]
    return sorted(trouves)


def main() -> int:
    cas = 0
    lus = 0
    for chemin in fichiers(sys.argv[1:]):
        try:
            texte = chemin.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as erreur:
            print(f"  ILLISIBLE {chemin}: {erreur}")
            cas += 1
            continue
        lus += 1
        for ligne, litteral in litteraux(texte):
            if MOTIF.search(litteral):
                cas += 1
                visible = litteral.encode("unicode_escape").decode("ascii")[:90]
                try:
                    nom = chemin.resolve().relative_to(RACINE)
                except ValueError:
                    nom = chemin
                print(f"  RATÉ {nom}:{ligne}: {visible}")
    print(f"MOJIBAKE : {lus} fichier(s) lu(s), {cas} littéral(aux) en mojibake")
    return 1 if cas else 0


if __name__ == "__main__":
    sys.exit(main())
