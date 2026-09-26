#!/usr/bin/env python3
"""H41 rejugée MORCEAU PAR MORCEAU (§ 14.1 du CDC recensement) : le compte de H39
contre celui de la parité, sur les SEULS morceaux où la chaîne a couru.

Le § 14 comparait une erreur moyenne sur dix morceaux à une parité mesurée sur un
seul ; ce script ne compare que des morceaux communs, et dit combien il en a.
La règle du verdict est celle du § 14, inchangée : CONFIRMÉE si l'erreur de H39
est ≤ celle de la parité (« bat la parité ») sur l'ensemble commun ; RÉFUTÉE si
elle est ≥ 7,90 ; PARTIELLE sinon.

    analyse/.venv/bin/python analyse/rejuger_h41.py reconstruction/travail
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


def main() -> int:
    t = Path(sys.argv[1] if len(sys.argv) > 1 else "reconstruction/travail")
    banc = json.loads((t / "s2-banc" / "rapport.json").read_text())
    parite = {m["morceau"]: m["parite"] for m in banc["morceaux"] if "parite" in m}
    lignes, e_p, e_h, e_h37 = [], [], [], []
    for nom in sorted(parite):
        h41 = t / "h41-s2" / nom / "mesure.json"
        h37 = t / "recensement-s2" / nom / "mesure.json"
        if not h41.exists():
            continue
        m = json.loads(h41.read_text())
        vrai = m["vrai"]["K"]
        k_h = m["L3"]["compte"]["A-grp"]["K"]
        k_p = parite[nom]["pistes_obtenues"]
        k_37 = json.loads(h37.read_text())["L3"]["compte"]["A-grp"]["K"] if h37.exists() else None
        e_p.append(abs(k_p - vrai))
        e_h.append(abs(k_h - vrai))
        if k_37 is not None:
            e_h37.append(abs(k_37 - vrai))
        lignes.append(f"  {nom:18s} vrai {vrai:3d} | parité {k_p:3d} (err {abs(k_p - vrai):2d}) | "
                      f"H39 {k_h:3d} (err {abs(k_h - vrai):2d}) | H37 {k_37}")
    n = len(e_p)
    print("\n".join(lignes))
    if n == 0:
        print("  aucun morceau commun")
        return 1
    mp, mh = sum(e_p) / n, sum(e_h) / n
    m37 = sum(e_h37) / len(e_h37) if e_h37 else float("nan")
    print(f"  {n} morceau(x) commun(s) : erreur moyenne parité {mp:.2f} ; H39 {mh:.2f} ; H37 {m37:.2f}")
    v = "CONFIRMÉE" if mh <= mp else "RÉFUTÉE" if mh >= 7.9 else "PARTIELLE"
    print(f"  → H41 (rejugée, {n}/10 morceaux) : {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
