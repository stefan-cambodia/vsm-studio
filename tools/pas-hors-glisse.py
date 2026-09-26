#!/usr/bin/env python3
"""LA GARDE DE D427-D429 : UN CURSEUR QUI S'ANNULE AU GLISSÉ S'ANNULE AUSSI À LA SAISIE.

RÈGLE GARDÉE (27/09/2026) : dans `app/Source/ui/`, tout curseur dont le
`onDragStart` ouvre un pas d'annulation (un appel dont le nom contient `Edit`,
`edit`, `debutEdition` ou `Started`) doit AUSSI en ouvrir un dans son
`onValueChange` hors glissé — ou poser un drapeau de glissé (`glisse…`) que
son `onValueChange` consulte. La molette, le clavier, la saisie dans la case et
le double-clic de valeur d'usine changent la valeur SANS glissé.

POURQUOI CETTE GARDE EXISTE. La ligne de piste avait payé ce défaut et l'avait
réparé ; la tranche du mixeur (D427 : fader, panoramique, trim, délai,
transposition), ses départs et les boutons MASTER (D428), puis les réglages
d'effet (D429) l'avaient gardé : un fader saisi à -6 dB restait à -6 dB après
Ctrl+Z, historique vide.

LIMITE, dite : la garde lit le TEXTE. Un curseur dont les rappels sont posés
ailleurs que sous la forme `x.onDragStart = [..] { … }` lui échappe.

  tools/pas-hors-glisse.py [fichier.cpp …]   (défaut : app/Source/ui/**/*.cpp)

Rend 0 si aucun curseur n'est en défaut, 1 sinon (chacun nommé).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
OUVRE_UN_PAS = re.compile(r"Edit|edit|debutEdition|Started")
GLISSE_OU_PAS = re.compile(r"glisse|Edit|edit|debutEdition|Started")
DEBUT = re.compile(r"([\w\->\.\[\]\*]+?)(?:->|\.)onDragStart\s*=\s*\[[^\]]*\]\s*\{")


def corps(texte: str, debut: int) -> str:
    """Le corps d'une lambda dont l'accolade ouvrante précède `debut`."""
    profondeur, fin = 1, debut
    while profondeur and fin < len(texte):
        profondeur += {"{": 1, "}": -1}.get(texte[fin], 0)
        fin += 1
    return texte[debut:fin]


def juger(fichier: Path) -> tuple[int, list[str]]:
    texte = fichier.read_text(encoding="utf-8", errors="replace")
    vus, fautes = 0, []
    for m in DEBUT.finditer(texte):
        if not OUVRE_UN_PAS.search(corps(texte, m.end())):
            continue
        cible = m.group(1)
        valeur = re.compile(re.escape(cible) + r"(?:->|\.)onValueChange\s*=\s*\[[^\]]*\]\s*\{")
        # Le plus PROCHE, avant ou après : le bouton MASTER pose son
        # `onValueChange` AVANT son `onDragStart`, et une recherche vers l'avant
        # seule le manquait (vu en construisant la garde : 10 curseurs jugés sur 11).
        candidats = list(valeur.finditer(texte))
        if not candidats:
            continue
        mv = min(candidats, key=lambda c: abs(c.start() - m.start()))
        vus += 1
        if not GLISSE_OU_PAS.search(corps(texte, mv.end())):
            ligne = texte.count("\n", 0, m.start()) + 1
            fautes.append(f"RATÉ {fichier.relative_to(RACINE) if fichier.is_relative_to(RACINE) else fichier}:{ligne}: "
                          f"« {cible} » ouvre un pas au glissé, pas à la saisie")
    return vus, fautes


def main() -> int:
    fichiers = [Path(a).resolve() for a in sys.argv[1:]] or sorted((RACINE / "app" / "Source" / "ui").rglob("*.cpp"))
    total, toutes = 0, []
    for f in fichiers:
        vus, fautes = juger(f)
        total += vus
        toutes += fautes
    for faute in toutes:
        print(faute)
    print(f"PAS HORS GLISSÉ : {total} curseur(s) jugé(s), {len(toutes)} en défaut")
    return 1 if toutes else 0


if __name__ == "__main__":
    sys.exit(main())
