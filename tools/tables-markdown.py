#!/usr/bin/env python3
"""Les tables Markdown du dépôt ont-elles toutes le même nombre de cellules ?

    analyse/.venv/bin/python tools/tables-markdown.py            # tout le dépôt
    analyse/.venv/bin/python tools/tables-markdown.py docs/INDEX.md

POURQUOI (13/09/2026). Une valeur qui contient une barre verticale — « 00:33,000 |
mes. 17 · 3 », ou un `VSM_ABANDON=abandonner|annuler|enregistrer` — coupe sa ligne
en deux cellules de plus et décale toute la fin du tableau. À l'œil, dans un
fichier de vingt-deux mille lignes, cela ne se voit pas ; au rendu, la table est
fausse. Deux lignes ont été écrites ainsi dans la même journée.

LA RÈGLE : dans un bloc de lignes commençant par « | », toutes doivent porter le
MÊME nombre de barres, les barres ÉCHAPPÉES (\\|) ne comptant pas. Le nombre
attendu est celui de la majorité — c'est la ligne isolée qui est fausse, pas les
douze autres.
"""
from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]


def mauvaises_lignes(chemin: Path) -> list[tuple[int, int, int]]:
    lignes = chemin.read_text(encoding="utf-8").split("\n")
    tables: list[list[tuple[int, int]]] = []
    courante: list[tuple[int, int]] = []
    for i, ligne in enumerate(lignes, start=1):
        if ligne.lstrip().startswith("|"):
            courante.append((i, ligne.replace("\\|", "").count("|")))
        elif courante:
            tables.append(courante)
            courante = []
    if courante:
        tables.append(courante)
    fautives: list[tuple[int, int, int]] = []
    for table in tables:
        if len(table) < 3:      # deux lignes ne font pas une table : un tableau a son en-tête
            continue
        comptes = Counter(n for _, n in table)
        if len(comptes) == 1:
            continue
        attendu = comptes.most_common(1)[0][0]
        fautives += [(ligne, n, attendu) for ligne, n in table if n != attendu]
    return fautives


def main(argv: list[str]) -> int:
    cibles = [Path(a) for a in argv[1:]] or sorted(
        list((RACINE / "docs").glob("*.md")) + [RACINE / "README.md", RACINE / "CLAUDE.md"])
    total = 0
    for chemin in cibles:
        if not chemin.is_file():
            continue
        for ligne, n, attendu in mauvaises_lignes(chemin):
            rel = chemin.relative_to(RACINE) if chemin.is_absolute() else chemin
            print(f"  {rel}:{ligne} : {n} barres au lieu de {attendu}")
            total += 1
    print(f"TABLES_MALFORMEES {total}")
    return 1 if total else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
