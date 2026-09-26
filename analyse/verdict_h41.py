#!/usr/bin/env python3
"""Le verdict de H41 (§ 14 du CDC recensement), recalculé depuis les rapports.

    analyse/.venv/bin/python analyse/verdict_h41.py reconstruction/travail
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


def main() -> int:
    t = Path(sys.argv[1] if len(sys.argv) > 1 else "reconstruction/travail")
    h37 = json.loads((t / "recensement-s2" / "rapport.json").read_text())["agrege"]["comptes"]
    chemin = t / "h41-s2" / "rapport.json"
    if not chemin.exists():
        print("h41-s2 non mesuré")
        return 1
    h41 = json.loads(chemin.read_text())["agrege"]["comptes"]
    k3, k2, parite = h41["A-grp@L3"]["K"]["mae"], h41["A-grp@L2"]["K"]["mae"], h41["P@L3"]["K"]["mae"]
    print(f"  témoin (H37, s2) : L3 {h37['A-grp@L3']['K']['mae']:.2f}, L2 {h37['A-grp@L2']['K']['mae']:.2f}, "
          f"parité {h37['P@L3']['K']['mae']:.2f} ; parité de cette course : {parite:.2f}")
    a1 = "TENU" if k3 <= 1.0 else "ÉCHEC (réfuté)" if k3 >= 7.9 else "raté (zone intermédiaire)"
    a2 = "TENU" if k2 <= 3.1 else "ÉCHEC (réfuté)" if k2 >= 8.7 else "raté (zone intermédiaire)"
    print(f"   1  {a1:28s} compte L3 : {k3:.2f}")
    print(f"   2  {a2:28s} compte L2 : {k2:.2f}")
    v = "CONFIRMÉE" if a1 == "TENU" else "RÉFUTÉE" if a1.startswith("ÉCHEC") else "PARTIELLE"
    print(f"  → H41 : {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
