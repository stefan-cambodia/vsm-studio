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
from typing import Any, Dict

import numpy as np

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
    ap.add_argument("--borner-hauteur", type=float, default=0.0, metavar="DEMI-TONS",
                    help="B5 / § 7 bis du cahier des charges : borner les paramètres du patch qui "
                         "DÉPLACENT la hauteur (détune d'oscillateur, accord d'échantillon). Sans "
                         "borne, une partie tirée peut sonner huit demi-tons à côté des notes que "
                         "sa vérité annonce : 9,9 %% des notes de s1-sec sont dans ce cas et "
                         "morceau-0001-g1 l'est ENTIÈREMENT, son F1 valant 0,027 ou 0,567 selon la "
                         "hauteur qu'on compare. Le corpus suivant se tire à 2. "
                         "0 (le défaut) : le corpus d'avant, au bit près")
    ap.add_argument("--sections", action="store_true",
                    help="B5 / exigence 1 : le morceau est découpé en SECTIONS de huit mesures et "
                         "les parties y entrent et en sortent (au moins deux ne sonnent pas d'un "
                         "bout à l'autre). Sans elle, toutes les parties jouent du début à la fin, "
                         "c'est-à-dire le corpus d'avant")
    ap.add_argument("--notes-breves", action="store_true",
                    help="B5 / exigence 2 : des parties mélodiques jouent BREF (frappes de 55 à "
                         "110 ms sur la grille de double croche). Le corpus d'avant ne porte aucune "
                         "note mélodique sous 150 ms — ses 1 865 notes courtes sont des frappes de "
                         "batterie, ce qui a fait publier trois phases de conclusions fausses")
    ap.add_argument("--echantillons", action="store_true",
                    help="B5 / exigence 3 : des parties à ÉCHANTILLONS (vsm.multisample, avec un "
                         "profil installé sur le poste) et un rôle CHANTÉ (vsm.vocal ou un chœur "
                         "échantillonné). Le corpus en dépend alors d'une banque hors du dépôt : la "
                         "vérité porte le nom, le chemin et l'empreinte de chaque profil")
    ap.add_argument("--duree-max", type=float, default=0.0, metavar="SECONDES",
                    help="durée TIRÉE entre --duree et cette valeur, par morceau. Le § 7 bis "
                         "demande des morceaux de trois à cinq minutes, et un lot où tous durent "
                         "la même chose ne mesure pas la longueur, il mesure une longueur. Le "
                         "tirage a sa propre graine (celle du morceau, décalée) pour que la durée "
                         "ne déplace pas le morceau lui-même")
    ap.add_argument("--b5", action="store_true",
                    help="les trois exigences ci-dessus ET --borner-hauteur 2 : le corpus complet du "
                         "§ 7 bis du cahier des charges, qui ne se paie qu'une fois")
    ap.add_argument("--moteur", default=None, help="chemin de vsm-render")
    args = ap.parse_args()
    if args.b5:
        args.sections = args.notes_breves = args.echantillons = True
        if args.borner_hauteur <= 0.0:
            args.borner_hauteur = 2.0
        if args.duree <= 30.0 and args.duree_max <= 0.0:
            args.duree, args.duree_max = 180.0, 300.0

    args.sortie.mkdir(parents=True, exist_ok=True)
    machines = [m for m in args.machines.split(",") if m] or None
    depart = time.time()
    bilan: Dict[str, Any] = {"format": "vsm-lot-synthetique", "version": 1, "options": vars(args) | {"sortie": str(args.sortie)},
             "morceaux": []}
    code = 0
    try:
        with VsmEngine(binary=args.moteur, sample_rate=44100) as moteur:
            generateur = Generateur(moteur, machines=machines, journal=print,
                                    borne_hauteur=args.borner_hauteur,
                                    arrangement=args.sections, notes_breves=args.notes_breves,
                                    echantillons=args.echantillons)
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
                duree = args.duree
                if args.duree_max > args.duree:
                    # Graine PROPRE à la durée : tirer dans le flux du morceau
                    # déplacerait le morceau lui-même, et deux lots de durées
                    # différentes ne se compareraient plus sur rien.
                    duree = float(np.random.default_rng(graine ^ 0x5D).uniform(args.duree, args.duree_max))
                verite, stems, melange = generateur.fabriquer(
                    graine, duree=duree, production=args.production, cas=args.cas,
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
