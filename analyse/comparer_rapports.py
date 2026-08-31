#!/usr/bin/env python3
"""Compare deux rapports de reconstruction, pour un A/B.

Ce qui compte dans l'A/B des « machines suivantes remises en jeu » n'est pas la
seule distance globale : c'est de savoir SI le verdict du melange a change
d'avis, et grace a quelle proposition. Un gain global sans changement de
decision voudrait dire qu'on mesure autre chose que ce qu'on croit.
"""
import json, sys
from pathlib import Path

def lire(p):
    return json.load(open(p, encoding="utf-8"))

def resume(d, titre):
    prov = d.get("provenance", {})
    opts = prov.get("options", {})
    print(f"=== {titre}")
    print(f"    commit {prov.get('commit','?')} · metrique {d.get('metric')} "
          f"· iterations {d.get('iterations')} "
          f"· machinesAuMelange {opts.get('machinesAuMelange', '(absent : code anterieur)')}")
    g = d.get("globalDistance")
    print(f"    distance globale : {g:.4f}" if g is not None else "    distance globale : (absente)")
    for s in d.get("stems", []):
        print(f"    stem {s.get('name','?'):8s} machine {s.get('machine','?'):16s} "
              f"D={s.get('distance', float('nan')):.4f}")
    for v in d.get("mixVerdict", []):
        gardee = v.get("kept")
        ecartees = ", ".join(f"{r.get('label')} {r.get('mixDistance'):.4f}"
                             for r in v.get("rejected", []))
        muet = v.get("mixDistanceMuted")
        print(f"    melange {v.get('track','?'):9s} GARDE « {gardee} » {v.get('mixDistance', float('nan')):.4f}"
              + (f" · sans la piste {muet:.4f}" if muet is not None else "")
              + (f"\n              ecarte : {ecartees}" if ecartees else ""))

def main():
    a, b = Path(sys.argv[1]), Path(sys.argv[2])
    da, db = lire(a), lire(b)
    resume(da, a.parent.name)
    print()
    resume(db, b.parent.name)
    ga, gb = da.get("globalDistance"), db.get("globalDistance")
    if ga and gb:
        print(f"\n=== ECART : {a.parent.name} {ga:.4f} contre {b.parent.name} {gb:.4f} "
              f"— {(gb-ga)/ga*100:+.1f} % pour le second")

main()
