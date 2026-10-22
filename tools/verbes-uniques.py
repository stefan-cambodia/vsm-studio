#!/usr/bin/env python3
"""UN VERBE DE BANC, UN SEUL GESTE : aucun verbe de `VSM_GESTE_APRES` n'en cache un autre.

    python3 tools/verbes-uniques.py

LA RÈGLE GARDÉE (22/10/2026, D571). Les gestes différés des bancs (`VSM_GESTE_APRES`) se
reconnaissent dans `MainComponent.h` par une suite de `geste.equalsIgnoreCase("…")` et de
`geste.startsWithIgnoreCase("…")`, essayés DANS L'ORDRE : le premier qui répond prend le
geste. Un second verbe du même nom, ou un verbe que le préfixe d'un précédent couvre, n'est
donc JAMAIS joué — sans un mot, puisque le premier a répondu.

POURQUOI CETTE GARDE. D570.3 a posé `relever-notes` (les notes du piano roll à l'écran)
quarante lignes AVANT le `relever-notes` de D509 (le texte de l'éditeur des notes du
projet). Le banc de D509, `notes-du-projet.sh`, ne recevait plus aucune ligne
`VSM_NOTES_EDITEUR` et lisait « ? » : rouge dans la série de D571, vert sur le binaire de
D566 — et la série de D570, qui l'avait sous les yeux, ne l'a pas dit. Le défaut n'était
dans aucun des deux gestes : il était dans leur nom.

CE QU'ELLE LIT : `app/Source/MainComponent.h`, `MainComponent.cpp` et `Main.cpp`, chaque
fichier pour lui-même, dans l'ordre des lignes. Un verbe est CACHÉ quand un verbe plus haut
du même fichier a le même nom (égalité), ou quand un `startsWith` plus haut est un préfixe
de son nom. Elle REFUSE (code 2) si elle ne trouve aucun verbe : une garde qui ne lit rien
et se tait est pire que pas de garde (D355).

Rend 0 si aucun verbe n'est caché, 1 sinon, 2 si elle ne reconnaît plus les sources.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
FICHIERS = ("app/Source/MainComponent.h", "app/Source/MainComponent.cpp", "app/Source/Main.cpp")
MOTIF = re.compile(r'\bgeste\.(equalsIgnoreCase|startsWithIgnoreCase)\("([^"]+)"\)')


def main() -> int:
    total = 0
    caches: list[str] = []
    for nom in FICHIERS:
        chemin = RACINE / nom
        if not chemin.exists():
            continue
        vus: list[tuple[str, str, int]] = []   # (sorte, verbe, ligne)
        for n, ligne in enumerate(chemin.read_text(encoding="utf-8").splitlines(), start=1):
            for sorte, verbe in MOTIF.findall(ligne):
                total += 1
                v = verbe.lower()
                for sorte0, verbe0, n0 in vus:
                    v0 = verbe0.lower()
                    if v == v0 or (sorte0 == "startsWithIgnoreCase" and v.startswith(v0)):
                        caches.append(f"{nom}:{n} « {verbe} » caché par « {verbe0} » ({nom}:{n0})")
                        break
                vus.append((sorte, verbe, n))
    if total == 0:
        print("VERBES : aucun verbe de geste reconnu — la garde ne lit plus les sources (refus)")
        return 2
    for c in caches:
        print("  CACHÉ " + c)
    print(f"VERBES : {total} verbe(s) lu(s), {len(caches)} caché(s)")
    return 1 if caches else 0


if __name__ == "__main__":
    sys.exit(main())
