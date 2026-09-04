#!/usr/bin/env python3
"""Fabrique un LOT de morceaux à vérité connue — le banc synthétique.

Chaque morceau est rendu par le moteur réel (`vsm-render --serve`), une passe
par partie, et vient avec `verite.json` (parties, machines, patchs, notes,
vélocités, niveaux, production, graine, empreintes, commit, coût) et ses
stems VRAIS. Même graine → même morceau au bit près. Le détail et les
attendus : docs/CDC-banc-synthetique.md.

Usage :
  analyse/.venv/bin/python analyse/morceaux.py --sortie reconstruction/travail/s1-sec --nombre 10 --graine 1
      [--duree 30] [--production] [--cas deux-mains] [--parties 4] [--machines vsm.juno106,vsm.tb303]

Interruptible (Ctrl-C) et reprenable : un morceau dont verite.json est
complet est sauté en le disant. Le coût de chaque morceau et du lot va dans
lot.json.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_engine import VsmEngine  # noqa: E402
from analyzer.vsm_morceaux import (CAS, Generateur, ecrire_morceau,  # noqa: E402
                                   morceau_complet)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sortie", type=Path, required=True, help="dossier du lot (un sous-dossier par morceau)")
    ap.add_argument("--nombre", type=int, default=10, help="nombre de morceaux")
    ap.add_argument("--graine", type=int, default=1, help="graine du premier morceau ; les suivants prennent +1")
    ap.add_argument("--duree", type=float, default=30.0, help="durée demandée en secondes (arrondie à la mesure)")
    ap.add_argument("--production", action="store_true",
                    help="réverbération courte + compression légère sur le MÉLANGE seul (dit dans la vérité)")
    ap.add_argument("--cas", default=None, choices=CAS,
                    help="imposer le cas de parité à tous les morceaux (défaut : tiré par morceau)")
    ap.add_argument("--parties", type=int, default=None, help="imposer le nombre de parties (défaut : 2 à 12, tiré)")
    ap.add_argument("--machines", default="",
                    help="restreindre le vivier mélodique (liste séparée par des virgules) ; défaut : le parc de recherche")
    ap.add_argument("--moteur", default=None, help="chemin de vsm-render")
    args = ap.parse_args()

    args.sortie.mkdir(parents=True, exist_ok=True)
    machines = [m for m in args.machines.split(",") if m] or None
    depart = time.time()
    bilan = {"format": "vsm-lot-synthetique", "version": 1, "options": vars(args) | {"sortie": str(args.sortie)},
             "morceaux": []}
    code = 0
    try:
        with VsmEngine(binary=args.moteur, sample_rate=44100) as moteur:
            generateur = Generateur(moteur, machines=machines, journal=print)
            print(f"vivier mélodique : {len(generateur.machines)} machines")
            for indice in range(args.nombre):
                graine = args.graine + indice
                dossier = args.sortie / f"morceau-{indice + 1:04d}-g{graine}"
                if morceau_complet(dossier):
                    print(f"SAUTÉ {dossier.name} (déjà fabriqué : verite.json complet)")
                    verite = json.loads((dossier / "verite.json").read_text(encoding="utf-8"))
                    bilan["morceaux"].append({"dossier": dossier.name, "graine": graine, "saute": True,
                                              "cout_s": verite["cout"]["total_s"]})
                    continue
                print(f"{dossier.name} :")
                verite, stems, melange = generateur.fabriquer(
                    graine, duree=args.duree, production=args.production, cas=args.cas,
                    nombre_de_parties=args.parties)
                ecrire_morceau(dossier, verite, stems, melange)
                print(f"  écrit : {verite['nombre_de_parties']} parties, {verite['duree']:.1f} s, "
                      f"{verite['cout']['total_s']:.1f} s de fabrication")
                bilan["morceaux"].append({"dossier": dossier.name, "graine": graine, "saute": False,
                                          "parties": verite["nombre_de_parties"], "cas": verite["cas"],
                                          "cout_s": verite["cout"]["total_s"]})
    except KeyboardInterrupt:
        print("\ninterrompu : le morceau en cours est incomplet et sera refait à la reprise")
        code = 130
    bilan["cout_total_s"] = time.time() - depart
    bilan["termine"] = code == 0
    (args.sortie / "lot.json").write_text(json.dumps(bilan, indent=1, ensure_ascii=False, default=str),
                                           encoding="utf-8")
    faits = sum(1 for m in bilan["morceaux"] if not m["saute"])
    print(f"lot : {faits} fabriqués, {len(bilan['morceaux']) - faits} sautés, "
          f"{bilan['cout_total_s']:.0f} s — {args.sortie / 'lot.json'}")
    return code


if __name__ == "__main__":
    sys.exit(main())
