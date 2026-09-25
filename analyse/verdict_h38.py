#!/usr/bin/env python3
"""Le verdict de H38, recalculé depuis les rapports du banc — jamais recopié à la main.

Confronte, pour CHAQUE embedding (`clap`, `ast`), les rapports de
`banc_recensement.py --embedding …` aux sept attendus du § 11 de
`docs/CDC-recensement-des-sources.md`, écrits avant la mesure (commit b8ea86f).
Les seuils ci-dessous sont RECOPIÉS de ce paragraphe.

    analyse/.venv/bin/python analyse/verdict_h38.py reconstruction/travail
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional


def lire(chemin: Path) -> Optional[Dict[str, Any]]:
    return json.loads(chemin.read_text()) if chemin.exists() else None


def etat(v: Optional[float], reussite: Callable[[float], bool],
         echec: Optional[Callable[[float], bool]]) -> str:
    if v is None:
        return "NON MESURÉ"
    if echec is not None and echec(v):
        return "ÉCHEC (réfuté)"
    return "TENU" if reussite(v) else "raté (zone intermédiaire)"


def juger(travail: Path, e: str) -> List[str]:
    lignes: List[str] = []

    def dire(num: str, texte: str, v, r, ec) -> str:
        x = etat(v, r, ec)
        lignes.append(f"  {num:>2s}  {x:28s} {texte}")
        return x

    s1 = lire(travail / f"h38-{e}-s1-sec" / "rapport.json")
    if s1 is None:
        return [f"  s1-sec non mesuré pour {e}"]
    ag = s1["agrege"]
    l1 = ag["L1"]
    part, eta = l1["partUneGrappe"], l1.get("eta2NiveauMedian")
    a1 = dire("1", f"L1 s1-sec : {l1['uneGrappe']}/{l1['parties']} parties seules à une grappe "
                   f"({part:.3f}) ; η² médian du niveau {eta:.3f}",
              part, lambda v: v >= 0.60 and eta < 0.20, lambda v: v < 0.35 or eta >= 0.40)
    s2 = lire(travail / f"h38-{e}-s2" / "rapport.json")
    p2 = s2["agrege"]["L1"]["partUneGrappe"] if s2 else None
    dire("2", f"L1 s2 : {'—' if p2 is None else f'{p2:.3f}'}"
              + ("" if s2 else " (non lancé : voir le journal de campagne)"),
         p2, lambda v: v >= 0.40, lambda v: v < 0.10)
    c = ag["comptes"]
    k3 = c["A-grp@L3"]["K"]["mae"]
    a3 = dire("3", f"compte L3 (A-grp) : {k3:.2f} (parité {c['P@L3']['K']['mae']:.2f})",
              k3, lambda v: v <= 3.0, lambda v: v >= 3.8)
    k2 = c["A-grp@L2"]["K"]["mae"]
    dire("4", f"compte L2 (A-grp) : {k2:.2f} (H37 : 5,20)", k2, lambda v: v <= 2.5, lambda v: v >= 5.2)
    ari = (ag["L2"]["ariMoyen"] or {}).get("moyenne")
    dire("5", f"ARI L2 de other : {'—' if ari is None else f'{ari:.3f}'} (H37 : 0,157)",
         ari, lambda v: v >= 0.30, lambda v: v < 0.157)
    cdl = lire(travail / f"h38-{e}-reels" / "clairdelune" / "rapport.json")
    if cdl:
        k = cdl["L3"]["compte"]["A-grp"]["K"]
        dire("6a", f"Clair de Lune : K = {k}", k, lambda v: v == 1, lambda v: v >= 3)
    else:
        dire("6a", "Clair de Lune : non mesuré (rapport absent)", None, lambda v: True, None)
    ch = lire(travail / f"h38-{e}-reels" / "children" / "rapport.json")
    if ch:
        k = ch["L3"]["compte"]["A-grp"]["K_mel"]
        dire("6b", f"Children : K_mél = {k} (K = {ch['L3']['compte']['A-grp']['K']})",
             k, lambda v: 5 <= v <= 7, lambda v: v <= 3 or v >= 10)
    else:
        dire("6b", "Children : non mesuré (rapport absent)", None, lambda v: True, None)
    cout = ag["L3"]["secondesMoyennes"] / 30.0
    dire("7", f"coût L3 : {ag['L3']['secondesMoyennes']:.1f} s par morceau de 30 s ({cout:.2f}×)",
         cout, lambda v: v <= 2.0, lambda v: v > 10.0)
    if a1.startswith("ÉCHEC"):
        v = "RÉFUTÉE — l'attendu 1 est dans sa zone d'échec"
    elif a1 == "TENU" and a3 == "TENU":
        v = "CONFIRMÉE"
    else:
        v = "PARTIELLE"
    lignes.append(f"  → {e} : {v}")
    return lignes


def main() -> int:
    travail = Path(sys.argv[1] if len(sys.argv) > 1 else "reconstruction/travail")
    for e in ("clap", "ast"):
        print(f"[{e}]")
        print("\n".join(juger(travail, e)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
