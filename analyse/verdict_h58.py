#!/usr/bin/env python3
"""H58 — le verdict du cache des niveaux SOLO du calage (docs/CDC-reload-indifferenciable.md § 10.4).

    python analyse/verdict_h58.py reconstruction/travail/h58

Le dossier porte trois courses du même morceau, écrites par `ab.sh` :
  T/  le témoin (`--sans-cache-rendus`)          T.log
  A/  avec le cache, qu'elle remplit             A.log
  B/  la même, rejouée sur le cache de A         B.log
et `cache-apres-T.txt`. Il RECALCULE les cinq attendus depuis les fichiers, sans rien
rendre ; la lecture des journaux et la comparaison sont celles de `verdict_h48`. Une
comparaison dont un côté manque rend « NON MESURÉ », jamais « différent ». Rend 0 si
les attendus 1 à 4 tiennent, 1 sinon, 2 si une course manque.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Dict, Optional, Tuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

from verdict_h48 import comparer, durees, rapport_pct  # noqa: E402

# La course B de H48 (§ 10.1) : le même banc, sans ce cache — 176 s d'étapes au mélange.
B_DE_H48_S = 176
NIVEAUX = re.compile(r"niveaux solo du calage : (\d+) payé\(s\), (\d+) relu\(s\)")


def niveaux(journal: Path) -> Optional[Tuple[int, int]]:
    m = NIVEAUX.search(journal.read_text(encoding="utf-8", errors="replace"))
    return (int(m.group(1)), int(m.group(2))) if m else None


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    s = Path(sys.argv[1])
    journaux = {nom: s / f"{nom}.log" for nom in ("T", "A", "B")}
    manquants = [str(j) for j in journaux.values() if not j.is_file() or j.stat().st_size == 0]
    if manquants:
        print("REFUS : journal absent ou vide — " + ", ".join(manquants))
        return 2
    d = {nom: durees(j) for nom, j in journaux.items()}
    n = {nom: niveaux(j) for nom, j in journaux.items()}
    for nom, x in d.items():
        print(f"  {nom} verdict {x['verdict']} s, réglages {x['reglages']} s, seconds verdicts {x['seconds']} s — "
              f"total {x['total']} s ; mesures de projet (payées, relues) : {x['compte']} ; "
              f"niveaux solo (payés, relus) : {n[nom]} ; {'finie' if x['finie'] else 'NON finie'}")

    tenus: Dict[int, bool] = {}
    print("ATTENDU 1 — l'identité, contre T :")
    identiques = []
    for nom in ("A", "B"):
        ok, recit = comparer(s / "T", s / nom)
        identiques.append(ok)
        print(f"    {nom} : {'IDENTIQUE' if ok else ('NON MESURÉ' if ok is None else 'DIFFÉRENT')} — {recit}")
    tenus[1] = all(x is True for x in identiques)
    print(f"  → {'TENU' if tenus[1] else 'ÉCHEC' if all(x is not None for x in identiques) else 'NON MESURÉ'}")

    p = rapport_pct(d["B"]["total"], B_DE_H48_S)
    tenus[2] = p is not None and p <= 50.0
    issue = "TENU" if tenus[2] else ("NON MESURÉ" if p is None else "ÉCHEC" if p > 80.0 else "ENTRE LES DEUX")
    print(f"ATTENDU 2 — le rejeu : B {d['B']['total']} s pour les {B_DE_H48_S} s de la course B de H48"
          f"{'' if p is None else f', soit {p:.1f} %'} → {issue}")

    payees_projet = d["B"]["compte"][0] if d["B"]["compte"] else None
    payes_niveaux = n["B"][0] if n["B"] else None
    tenus[3] = payees_projet == 0 and payes_niveaux == 0
    print(f"ATTENDU 3 — ce que B paie : {payes_niveaux} niveau(x) solo, {payees_projet} mesure(s) de projet "
          f"→ {'TENU' if tenus[3] else 'ÉCHEC'}")

    compte_t = s / "cache-apres-T.txt"
    if not compte_t.is_file():
        print("ATTENDU 4 — le témoin n'écrit rien : NON MESURÉ (cache-apres-T.txt absent)")
        tenus[4] = False
    else:
        fichiers = int(compte_t.read_text().strip() or "0")
        tenus[4] = fichiers == 0 and d["T"]["compte"] is None and n["T"] is None
        print(f"ATTENDU 4 — le témoin n'écrit rien : {fichiers} fichier(s) au cache après T, lignes de compte "
              f"{'absentes' if d['T']['compte'] is None and n['T'] is None else 'PRÉSENTES'} → "
              f"{'TENU' if tenus[4] else 'ÉCHEC'}")

    p5 = rapport_pct(d["A"]["total"], d["T"]["total"])
    if p5 is None:
        print("ATTENDU 5 — le coût de la première passe : NON MESURÉ")
    else:
        issue = "TENU" if p5 <= 110.0 else ("ÉCHEC" if p5 > 125.0 else "ENTRE LES DEUX")
        print(f"ATTENDU 5 — le coût de la première passe : A {d['A']['total']} s pour T {d['T']['total']} s, "
              f"soit {p5:.1f} % → {issue}")
    return 0 if all(tenus.get(i) for i in (1, 2, 3, 4)) else 1


if __name__ == "__main__":
    sys.exit(main())
