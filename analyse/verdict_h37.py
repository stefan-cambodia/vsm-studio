#!/usr/bin/env python3
"""Le verdict de H37, recalculé depuis les rapports du banc — jamais recopié à la main.

Lit les `rapport.json` de `banc_recensement.py` (lots du banc) et des disques,
et confronte chaque chiffre aux 14 attendus du § 0.5 de
`docs/CDC-recensement-des-sources.md`, écrits avant la mesure (commit a110fd8).
Les seuils ci-dessous sont RECOPIÉS de ce paragraphe ; les changer ici sans le
changer là ferait mentir le verdict.

    analyse/.venv/bin/python analyse/verdict_h37.py reconstruction/travail
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional


def lire(chemin: Path) -> Optional[Dict[str, Any]]:
    return json.loads(chemin.read_text()) if chemin.exists() else None


def verdict(valeur: Optional[float], reussite, echec) -> str:
    if valeur is None:
        return "NON MESURÉ"
    if echec is not None and echec(valeur):
        return "ÉCHEC (réfuté)"
    if reussite(valeur):
        return "TENU"
    return "raté (zone intermédiaire)"


def main() -> int:
    travail = Path(sys.argv[1] if len(sys.argv) > 1 else "reconstruction/travail")
    lots = {n: lire(travail / f"recensement-{n}" / "rapport.json") for n in ("s1-sec", "s1-prod", "s2")}
    lignes: List[str] = []

    def dire(num: str, texte: str, v: Optional[float], r, e) -> str:
        etat = verdict(v, r, e)
        lignes.append(f"{num:>3s}  {etat:28s} {texte}")
        return etat

    sec = lots["s1-sec"]
    if sec is None:
        print("s1-sec non mesuré")
        return 2
    ag = sec["agrege"]
    c = ag["comptes"]
    meilleure = ag["meilleureA"]
    e3 = c[f"{meilleure}@L3"]["K"]
    a1 = dire("1", f"meilleure A en L3 ({meilleure}) : erreur sur K {e3['mae']:.2f}, "
                   f"{e3['aUnPres']}/{e3['n']} à ±1 — parité P {c['P@L3']['K']['mae']:.2f}",
              e3["mae"], lambda v: v <= 2.0 and e3["aUnPres"] >= 4, lambda v: v >= 3.8)
    part = ag["partSeparation"]
    perdues_l3 = ag["partPerduesD_abordEnL3"]
    dire("2", f"(E_L3 − E_L2)/E_L3 = {part:.3f} ({meilleure}) ; perdues d'abord en L3 : "
              f"{perdues_l3:.3f} ; par approche : "
              + ", ".join(f"{a} {(c[a + '@L3']['K']['mae'] - c[a + '@L2']['K']['mae']) / c[a + '@L3']['K']['mae']:+.2f}"
                          for a in ("A-grp", "A-pal", "A-voix")),
         part, lambda v: v >= 0.50 and (perdues_l3 or 0) >= 0.50, lambda v: v < 0.25)
    l1 = ag["L1"]
    dire("3", f"L1 : {l1['uneGrappe']}/{l1['parties']} parties seules à une grappe ; "
              f"distribution {l1['distribution']} ; deux-mains coupées {l1['deuxMainsCoupees']}/{l1['deuxMains']}",
         l1["partUneGrappe"], lambda v: v >= 0.80, lambda v: v < 0.60)
    l2, l3 = ag["L2"], ag["L3"]
    ari2 = (l2["ariMoyen"] or {}).get("moyenne")
    nmi2 = (l2["nmiMoyen"] or {}).get("moyenne")
    kmel_l2 = c["A-grp@L2"]["K_mel"]["aUnPres"]
    dire("4", f"L2 other : ARI moyen {ari2:.3f}, NMI {nmi2:.3f} (sur {l2['ariMoyen']['n']} morceaux où "
              f"l'ARI est défini) ; K_mél A-grp à ±1 : {kmel_l2}/10",
         ari2, lambda v: v >= 0.30 and (nmi2 or 0) >= 0.45 and kmel_l2 >= 5, lambda v: v < 0.10)
    ari3 = (l3["ariMoyen"] or {}).get("moyenne")
    ecart = None if ari2 is None or ari3 is None else ari2 - ari3
    dire("5", f"ARI L2 − L3 = {'—' if ecart is None else f'{ecart:+.3f}'} "
              f"(L2 {ari2:.3f}, L3 {'—' if ari3 is None else f'{ari3:.3f}'})",
         ecart, lambda v: v >= 0.10, lambda v: v <= 0.0)
    p = l3["pieces"]
    kick = p["grosse caisse"]["rappel"]
    dire("6", "L3 pièces : " + " · ".join(
        f"{k} {v['rappel']:.2f} ({v['morceaux']})" if isinstance(v, dict) else f"{k} {v}" for k, v in p.items()),
         kick, lambda v: v >= 0.9 and p["charleston"]["rappel"] >= 0.8 and p["caisse claire"]["rappel"] >= 0.7
         and p["toms"]["rappel"] >= 0.5, lambda v: v < 0.7)
    dire("7", f"L2 F1 macro rôles (lead, nappe, accompagnement, piano) : {l2['f1MacroGlobal']:.3f}",
         l2["f1MacroGlobal"], lambda v: v >= 0.40, lambda v: v < 0.25)
    n1 = l1["n3"]
    dire("8", f"N3 L1 : top 1 {n1['top1']:.3f}, top 5 {n1['top5']:.3f}, rang médian {n1['rangMedian']} (n={n1['n']})",
         n1["top5"], lambda v: v >= 0.80 and n1["top1"] >= 0.50, lambda v: v < 0.50)
    n2, n3 = l2["n3"], l3["n3"]
    dire("9", f"N3 L2 : top 1 {n2.get('top1', 0):.3f}, top 5 {n2.get('top5', 0):.3f} (n={n2['n']}) ; "
              f"L3 : top 1 {n3.get('top1', 0):.3f}, top 5 {n3.get('top5', 0):.3f} (n={n3['n']})",
         n2.get("top5"), lambda v: v >= 0.40 and n2["top1"] >= 0.20 and n3.get("top1", 0) <= n2["top1"] - 0.05,
         lambda v: v < 0.15)
    d1, d2, d3 = l1["distanceMediane"], l2["distanceMediane"], l3["distanceMediane"]
    dire("10", f"distance médiane au corpus : L1 {d1:.2f} · L2 {d2:.2f} · L3 {d3:.2f}",
         d3, lambda v: d1 < d2 < v and d1 <= 3.91 and v >= 5.0, lambda v: v <= d2)
    eb = c["B@L4"]["K_mel"]["mae"]
    ea = c[f"{meilleure}@L3"]["K_mel"]["mae"]
    dire("11", f"K_mél : B@L4 {eb:.2f} contre {meilleure}@L3 {ea:.2f}",
         eb - ea, lambda v: v >= 0.0, lambda v: v < -0.5)
    dire("12", f"coût L3 : {l3['secondesMoyennes']:.1f} s par morceau de 30 s "
               f"({l3['secondesMoyennes'] / 30.0:.2f}× la durée)",
         l3["secondesMoyennes"] / 30.0, lambda v: v <= 2.0, lambda v: v > 10.0)
    reels = {n: lire(travail / "recensement-reels" / n / "rapport.json")
             for n in ("clairdelune", "children", "b4wuzthen", "usandthem", "skyandsand")}
    cdl = reels["clairdelune"]
    if cdl:
        k = cdl["L3"]["compte"]
        dire("13a", "Clair de Lune (1 partie) : " + ", ".join(f"{a} K={v['K']}" for a, v in k.items())
             + f", B K={cdl['L4']['compte']['B']['K']}", k["A-grp"]["K"], lambda v: v == 1, lambda v: v >= 3)
    ch = reels["children"]
    if ch:
        k = ch["L3"]["compte"]
        dire("13b", "Children [8 ; 10] : " + ", ".join(f"{a} K={v['K']}" for a, v in k.items())
             + f", B K={ch['L4']['compte']['B']['K']}", k["A-grp"]["K"], lambda v: 8 <= v <= 10,
             lambda v: v <= 5 or v >= 13)
    prod = lots["s1-prod"]
    if prod:
        cp = prod["agrege"]["comptes"]
        best_p = min(cp[f"{a}@L3"]["K"]["mae"] for a in ("A-grp", "A-pal", "A-voix"))
        best_s = min(c[f"{a}@L3"]["K"]["mae"] for a in ("A-grp", "A-pal", "A-voix"))
        dire("14a", f"s1-prod : meilleure A en L3 {best_p:.2f} contre {best_s:.2f} sur s1-sec",
             best_p - best_s, lambda v: v <= 0.5, None)
    s2 = lots["s2"]
    if s2:
        k = s2["agrege"]["comptes"]["A-grp@L2"]["K_mel"]
        dire("14b", f"s2 : K_mél A-grp en L2 à ±1 : {k['aUnPres']}/{k['n']}", k["aUnPres"],
             lambda v: v >= 3, None)

    print("\n".join(lignes))
    print()
    if a1.startswith("ÉCHEC"):
        print("VERDICT (§ 0.6) : H37 RÉFUTÉE — l'attendu 1 est dans sa zone d'échec.")
    elif a1 == "TENU" and verdict(part, lambda v: v >= 0.50, lambda v: v < 0.25) == "TENU":
        print("VERDICT (§ 0.6) : H37 CONFIRMÉE.")
    else:
        print("VERDICT (§ 0.6) : H37 PARTIELLE.")
    for nom, r in reels.items():
        if r:
            print(f"  disque {nom:12s} L3 {r['L3']['compte']} · B {r['L4']['compte']['B']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
