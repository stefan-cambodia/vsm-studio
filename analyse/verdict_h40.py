#!/usr/bin/env python3
"""Le verdict de H40, recalculé depuis les rapports du banc — jamais recopié à la main.

Confronte `banc_recensement.py --regroupement-par-simultaneite` aux attendus du
§ 13 de `docs/CDC-recensement-des-sources.md` (commit 773df6d, définition
corrigée au commit 7019edd, avant la mesure), dit le témoin, compte les
DÉCISIONS prises par stem, et publie la sensibilité au seuil (0,25 ; 0,75).

    analyse/.venv/bin/python analyse/verdict_h40.py reconstruction/travail
"""

from __future__ import annotations

import collections
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


def decisions(dossier: Path) -> collections.Counter:
    c: collections.Counter = collections.Counter()
    for f in sorted(dossier.glob("morceau-*/mesure.json")):
        texte = f.read_text()
        for choix in ("grappe unique : rien de fusionné", "séparation gardée : sources simultanées",
                      "fusion : grappes non simultanées", "fusion : aucune preuve de simultanéité"):
            c[choix] += texte.count(json.dumps(choix, ensure_ascii=False))
    return c


def main() -> int:
    t = Path(sys.argv[1] if len(sys.argv) > 1 else "reconstruction/travail")
    lignes: List[str] = []

    def dire(num: str, texte: str, v: Optional[float], r: Callable[[float], bool],
             e: Callable[[float], bool]) -> str:
        x = etat(v, r, e)
        lignes.append(f"  {num:>2s}  {x:28s} {texte}")
        return x

    temoin, h37 = lire(t / "h40-temoin-s1-sec" / "rapport.json"), lire(t / "recensement-s1-sec" / "rapport.json")
    if temoin and h37:
        meme = temoin["agrege"]["comptes"] == h37["agrege"]["comptes"] and \
            temoin["agrege"]["L1"]["partUneGrappe"] == h37["agrege"]["L1"]["partUneGrappe"]
        lignes.append("  témoin sans option : " + ("IDENTIQUE à H37 (comptes et L1)" if meme
                                                     else "DIFFÉRENT de H37 : le témoin ne témoigne pas"))
    s1 = lire(t / "h40-s1-sec" / "rapport.json")
    if s1 is None:
        print("\n".join(lignes + ["  h40-s1-sec non mesuré"]))
        return 1
    ag = s1["agrege"]
    a1 = dire("1", f"L1 s1-sec : {ag['L1']['uneGrappe']}/{ag['L1']['parties']} ({ag['L1']['partUneGrappe']:.3f})"
                   " — H37 0,230, H39 0,676", ag["L1"]["partUneGrappe"], lambda v: v >= 0.60, lambda v: v < 0.35)
    k2 = ag["comptes"]["A-grp@L2"]["K"]["mae"]
    a2 = dire("2", f"compte L2 : {k2:.2f} — H37 5,20, H39 5,90", k2, lambda v: v <= 5.2, lambda v: v >= 5.9)
    k3 = ag["comptes"]["A-grp@L3"]["K"]["mae"]
    a3 = dire("3", f"compte L3 : {k3:.2f} — parité {ag['comptes']['P@L3']['K']['mae']:.2f}, H39 3,60",
              k3, lambda v: v <= 3.0, lambda v: v >= 4.0)
    s2 = lire(t / "h40-s2" / "rapport.json")
    dire("4", f"L1 s2 : {'—' if s2 is None else round(s2['agrege']['L1']['partUneGrappe'], 3)} — H39 0,446",
         None if s2 is None else s2["agrege"]["L1"]["partUneGrappe"], lambda v: v >= 0.40, lambda v: v < 0.10)
    cdl = lire(t / "h40-reels" / "clairdelune" / "rapport.json")
    if cdl:
        k = cdl["L3"]["compte"]["A-grp"]["K"]
        dire("5a", f"Clair de Lune : K = {k}", k, lambda v: v == 1, lambda v: v >= 3)
    ch = lire(t / "h40-reels" / "children" / "rapport.json")
    if ch:
        k = ch["L3"]["compte"]["A-grp"]["K_mel"]
        dire("5b", f"Children : K_mél = {k}", k, lambda v: 5 <= v <= 7, lambda v: v <= 3 or v >= 10)
    if a2.startswith("ÉCHEC"):
        v = "RÉFUTÉE — l'attendu 2 est en échec"
    elif a1 == "TENU" and a2 == "TENU" and not a3.startswith("ÉCHEC"):
        v = "CONFIRMÉE"
    else:
        v = "PARTIELLE"
    lignes.append(f"  → H40 : {v}")
    lignes.append("  décisions par stem (s1-sec, tous niveaux) : "
                  + ", ".join(f"{k} {n}" for k, n in decisions(t / "h40-s1-sec").items()))
    lignes.append("  sensibilité (hors verdict) :")
    for nom, dossier in (("0,25", "h40-s1-sec-0.25"), ("0,50", "h40-s1-sec"), ("0,75", "h40-s1-sec-0.75")):
        r = lire(t / dossier / "rapport.json")
        if r:
            a = r["agrege"]
            lignes.append(f"     seuil {nom} : L1 {a['L1']['partUneGrappe']:.3f} ; L2 "
                          f"{a['comptes']['A-grp@L2']['K']['mae']:.2f} ; L3 {a['comptes']['A-grp@L3']['K']['mae']:.2f}")
    print("\n".join(lignes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
