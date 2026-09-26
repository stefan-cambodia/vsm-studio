#!/usr/bin/env python3
"""Le verdict de H39, recalculé depuis les rapports du banc — jamais recopié à la main.

Confronte les rapports de `banc_recensement.py --hdbscan-une-grappe` aux cinq
attendus du § 12 de `docs/CDC-recensement-des-sources.md`, écrits avant la
mesure (commit 5dc88f7), et le témoin sans l'option à la course H37.

    analyse/.venv/bin/python analyse/verdict_h39.py reconstruction/travail
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional


def lire(chemin: Path) -> Optional[Dict[str, Any]]:
    return json.loads(chemin.read_text()) if chemin.exists() else None


def etat(v: Optional[float], reussite: Callable[[float], bool], echec: Callable[[float], bool]) -> str:
    if v is None:
        return "NON MESURÉ"
    if echec(v):
        return "ÉCHEC (réfuté)"
    return "TENU" if reussite(v) else "raté (zone intermédiaire)"


def main() -> int:
    t = Path(sys.argv[1] if len(sys.argv) > 1 else "reconstruction/travail")
    lignes: List[str] = []

    def dire(num: str, texte: str, v: Optional[float], r: Callable[[float], bool],
             e: Callable[[float], bool]) -> str:
        x = etat(v, r, e)
        lignes.append(f"  {num:>2s}  {x:28s} {texte}")
        return x

    temoin, h37 = lire(t / "h39-temoin-s1-sec" / "rapport.json"), lire(t / "recensement-s1-sec" / "rapport.json")
    if temoin and h37:
        a, b = temoin["agrege"], h37["agrege"]
        meme = (a["L1"]["partUneGrappe"] == b["L1"]["partUneGrappe"]
                and a["comptes"]["A-grp@L3"]["K"]["mae"] == b["comptes"]["A-grp@L3"]["K"]["mae"])
        lignes.append(f"  témoin sans l'option : L1 {a['L1']['partUneGrappe']:.4f}, L3 "
                      f"{a['comptes']['A-grp@L3']['K']['mae']:.2f} — "
                      + ("IDENTIQUE à H37" if meme else "DIFFÉRENT de H37 : le témoin ne témoigne pas"))
    s1 = lire(t / "h39-s1-sec" / "rapport.json")
    if s1 is None:
        print("h39-s1-sec non mesuré")
        return 1
    ag = s1["agrege"]
    l1 = ag["L1"]
    a1 = dire("1", f"L1 s1-sec : {l1['uneGrappe']}/{l1['parties']} parties seules à une grappe "
                   f"({l1['partUneGrappe']:.3f}) — H37 0,230", l1["partUneGrappe"],
              lambda v: v >= 0.60, lambda v: v < 0.35)
    s2 = lire(t / "h39-s2" / "rapport.json")
    dire("2", f"L1 s2 : {'—' if s2 is None else round(s2['agrege']['L1']['partUneGrappe'], 3)} — H37 0,014",
         None if s2 is None else s2["agrege"]["L1"]["partUneGrappe"], lambda v: v >= 0.40, lambda v: v < 0.10)
    c = ag["comptes"]
    k3 = c["A-grp@L3"]["K"]["mae"]
    a3 = dire("3", f"compte L3 (A-grp) : {k3:.2f} (parité {c['P@L3']['K']['mae']:.2f}, H37 4,00)",
              k3, lambda v: v <= 3.0, lambda v: v >= 4.4)
    k2 = c["A-grp@L2"]["K"]["mae"]
    dire("4", f"compte L2 (A-grp) : {k2:.2f} (H37 5,20)", k2, lambda v: v <= 2.5, lambda v: v >= 5.2)
    cdl = lire(t / "h39-reels" / "clairdelune" / "rapport.json")
    if cdl:
        k = cdl["L3"]["compte"]["A-grp"]["K"]
        dire("5a", f"Clair de Lune : K = {k}", k, lambda v: v == 1, lambda v: v >= 3)
    ch = lire(t / "h39-reels" / "children" / "rapport.json")
    if ch:
        k = ch["L3"]["compte"]["A-grp"]["K_mel"]
        dire("5b", f"Children : K_mél = {k} (K = {ch['L3']['compte']['A-grp']['K']})",
             k, lambda v: 5 <= v <= 7, lambda v: v <= 3 or v >= 10)
    if a1.startswith("ÉCHEC"):
        v = "RÉFUTÉE — l'attendu 1 est dans sa zone d'échec"
    elif a1 == "TENU" and a3 == "TENU":
        v = "CONFIRMÉE"
    elif a1 == "TENU" and a3.startswith("ÉCHEC"):
        v = "PARTIELLE — la grappe unique règle L1 en CASSANT le mélange (le risque écrit avant)"
    else:
        v = "PARTIELLE"
    lignes.append(f"  → H39 : {v}")
    print("\n".join(lignes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
