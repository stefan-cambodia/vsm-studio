#!/usr/bin/env python3
"""LA GARDE DE D433 : UNE BRANCHE « MAJ » NE SE CACHE PAS DERRIÈRE `key == KeyPress::…Key`.

RÈGLE GARDÉE (27/09/2026) : dans `app/Source/`, une comparaison
`key == juce::KeyPress::<touche>Key` ne doit pas avoir, dans les quatre lignes qui la
précèdent ni les six qui la suivent, de test de modificateur (`isShiftDown`, `isCtrlDown`,
`isCommandDown`, `isAltDown`). L'`operator==(int)` de JUCE exige qu'AUCUN
modificateur ne soit tenu : le test qui suit est une branche MORTE. Comparer
`key.getKeyCode()` à la place.

POURQUOI CETTE GARDE EXISTE. D433 : la page des raccourcis promettait « Maj :
d'une octave » et « Maj : quatre pas » au piano roll et dans l'arrangement ; le
code le voulait, derrière `key == juce::KeyPress::upKey`, et rien ne se passait
depuis toujours.

  tools/touches-modifiees.py [fichier …]   (défaut : app/Source/**/*.cpp et *.h)

Rend 0 si aucune branche morte, 1 sinon (chacune nommée).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
COMPARAISON = re.compile(r"\b\w+\s*==\s*juce::KeyPress::\w+Key\b")
MODIFICATEUR = re.compile(r"is(Shift|Ctrl|Command|Alt)Down")


def juger(fichier: Path) -> tuple[int, list[str]]:
    lignes = fichier.read_text(encoding="utf-8", errors="replace").split("\n")
    vues, fautes = 0, []
    for i, ligne in enumerate(lignes):
        if not COMPARAISON.search(ligne):
            continue
        vues += 1
        # Avant ET après : au piano roll, le pas « Maj : quatre pas » était calculé
        # la ligne AU-DESSUS de `key == leftKey` -- une fenêtre vers l'avant seule
        # l'a manqué (vu en construisant la garde : 3 défauts sur 4).
        if MODIFICATEUR.search("\n".join(lignes[max(0, i - 4):i + 6])):
            nom = fichier.relative_to(RACINE) if fichier.is_relative_to(RACINE) else fichier
            fautes.append(f"RATÉ {nom}:{i + 1}: « {ligne.strip()[:80]} » suivie d'un test de modificateur")
    return vues, fautes


def main() -> int:
    fichiers = [Path(a).resolve() for a in sys.argv[1:]] or sorted(
        list((RACINE / "app" / "Source").rglob("*.cpp")) + list((RACINE / "app" / "Source").rglob("*.h")))
    vues, toutes = 0, []
    for f in fichiers:
        v, fautes = juger(f)
        vues += v
        toutes += fautes
    for faute in toutes:
        print(faute)
    print(f"TOUCHES MODIFIÉES : {vues} comparaison(s) lue(s), {len(toutes)} branche(s) morte(s)")
    return 1 if toutes else 0


if __name__ == "__main__":
    sys.exit(main())
