#!/usr/bin/env python3
"""Un lot du banc synthétique tient-il les exigences du § 7 bis ? (B5)

    analyse/.venv/bin/python tools/corpus-exigences.py reconstruction/travail/s2

LA RÈGLE QUE CETTE GARDE FAIT TENIR, et elle est écrite dans
`docs/CDC-banc-synthetique.md` § 7 bis.1, jamais ici :

  1. des morceaux LONGS dont les parties entrent et sortent — au moins la
     moitié des morceaux durent plus de 180 s, et CHACUN porte au moins deux
     parties qui ne sonnent pas d'un bout à l'autre ;
  2. des notes mélodiques BRÈVES — au moins un cinquième des parties mélodiques
     portent des notes sous 120 ms, et le corpus en compte au moins 500 ;
  3. des parties à ÉCHANTILLONS et de la VOIX — au moins un morceau sur trois
     porte une partie `vsm.sampler` ou `vsm.multisample`, au moins un sur
     quatre un rôle chanté ;
  4. les paramètres qui déplacent la hauteur, TIRÉS SUR UNE GRILLE CONNUE —
     aucune partie ne dépasse deux demi-tons de désaccord. (Le second volet de
     l'exigence 4, « `corpus-hauteurs.py` rend MORCEAUX_INUTILISABLES 0 », se
     mesure en TRANSCRIVANT les stems : c'est cet outil-là qui le dit, celui-ci
     ne juge que ce que la vérité écrit.)
  5. retirée le 13/09 (D278) : plus rien à demander au corpus de ce côté.

POURQUOI UNE GARDE ET PAS UN SCRIPT DE PHASE. Un script écrit pour une phase
n'est ni relu, ni rejoué, ni corrigé (D150). Ce qui doit empêcher le corpus de
repartir en arrière — et un corpus se réengendre — vit dans `tools/`.

CE QU'ELLE NE FAIT PAS : écouter, ni transcrire. Elle lit `verite.json`, qui
est l'étalon ; un corpus dont la vérité ment est l'affaire de
`tools/corpus-hauteurs.py`, et les deux se lancent ensemble.

Sortie : une ligne par exigence, le verdict, et un code de retour non nul dès
qu'une exigence tombe — pour qu'un enchaînement s'arrête dessus.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List

RACINE = Path(__file__).resolve().parent.parent
BREVE_S = 0.120
LONG_S = 180.0
ROLES_MELODIQUES = ("basse", "accompagnement", "melodie", "nappe", "voix", "piano-deux-mains")
ROLE_VOIX = "voix"
MACHINES_ECHANTILLONS = ("vsm.sampler", "vsm.multisample")


def verites(dossier: Path) -> List[Dict[str, Any]]:
    trouvees = []
    for chemin in sorted(dossier.glob("morceau-*/verite.json")):
        trouvees.append(json.loads(chemin.read_text(encoding="utf-8")))
    return trouvees


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("lot", type=Path, help="dossier du lot (un sous-dossier par morceau)")
    args = ap.parse_args()

    lot = verites(args.lot)
    if not lot:
        print(f"AUCUN MORCEAU dans {args.lot} — rien à juger, et ce n'est pas un succès")
        return 2
    print(f"{args.lot} : {len(lot)} morceaux")

    echecs: List[str] = []

    # ------------------------------------------------------------------ 1
    longs = [v for v in lot if float(v["duree"]) > LONG_S]
    sans_deux_partielles = []
    for v in lot:
        parties = v["parties"]
        total = len((v.get("arrangement") or {}).get("sections", []) or [])
        partielles = [p for p in parties
                      if p.get("sections") is not None and len(p["sections"]) < total]
        if len(partielles) < 2:
            sans_deux_partielles.append(f"{v['graine']} ({len(partielles)})")
    ok1 = len(longs) * 2 >= len(lot) and not sans_deux_partielles
    print(f"1. morceaux longs et parties qui entrent/sortent : "
          f"{len(longs)}/{len(lot)} au-delà de {LONG_S:.0f} s "
          f"(il en faut {(len(lot) + 1) // 2}), "
          f"{len(lot) - len(sans_deux_partielles)}/{len(lot)} morceaux à deux parties partielles"
          + (f" — manquent : {', '.join(sans_deux_partielles[:5])}" if sans_deux_partielles else ""))
    if not ok1:
        echecs.append("1")

    # ------------------------------------------------------------------ 2
    melodiques = [p for v in lot for p in v["parties"] if p["role"] in ROLES_MELODIQUES]
    avec_breves = [p for p in melodiques
                   if any(float(n[3]) < BREVE_S for n in p["notes"])]
    breves = sum(1 for p in melodiques for n in p["notes"] if float(n[3]) < BREVE_S)
    abandons = [p for v in lot for p in v["parties"]
                if (p.get("phrase_breve") or {}).get("abandonnee")]
    ok2 = len(avec_breves) * 5 >= len(melodiques) and breves >= 500
    print(f"2. notes mélodiques brèves : {len(avec_breves)}/{len(melodiques)} parties mélodiques "
          f"(il en faut {(len(melodiques) + 4) // 5}), {breves} notes sous {BREVE_S * 1000:.0f} ms "
          f"(il en faut 500)"
          + (f" ; {len(abandons)} phrasé(s) bref(s) abandonné(s), dits par la vérité" if abandons else ""))
    if not ok2:
        echecs.append("2")

    # ------------------------------------------------------------------ 3
    avec_echantillons = [v for v in lot
                         if any(p["machine"] in MACHINES_ECHANTILLONS for p in v["parties"])]
    avec_voix = [v for v in lot if any(p["role"] == ROLE_VOIX for p in v["parties"])]
    ok3 = len(avec_echantillons) * 3 >= len(lot) and len(avec_voix) * 4 >= len(lot)
    sans_empreinte = [p["profil"] for v in lot for p in v["parties"]
                      if p["machine"] in MACHINES_ECHANTILLONS and not p.get("profil_empreinte")]
    print(f"3. échantillons et voix : {len(avec_echantillons)}/{len(lot)} morceaux échantillonnés "
          f"(il en faut {(len(lot) + 2) // 3}), {len(avec_voix)}/{len(lot)} chantés "
          f"(il en faut {(len(lot) + 3) // 4})")
    if sans_empreinte:
        print(f"   ATTENTION : {len(sans_empreinte)} partie(s) à échantillons sans empreinte de "
              f"profil — le corpus n'est pas reproductible ailleurs")
        echecs.append("3 (empreinte)")
    if not ok3:
        echecs.append("3")

    # ------------------------------------------------------------------ 4
    # UNE VÉRITÉ QUI NE PORTE PAS LE CHAMP NE VAUT PAS ZÉRO. `desaccords_demi_tons`
    # est né avec D277 ; les corpus d'avant ne l'ont pas. Sans cette distinction,
    # la garde annonçait « désaccord maximal 0,00 demi-ton » sur `s1-sec`, dont
    # D267 a mesuré des parties à ±24 — c'est le piège de D265, une mesure qui ne
    # peut pas voir une chose et la compte absente. Elle le DIT et ne conclut pas.
    borne = max((float((v.get("exigences") or {}).get("borne_hauteur", 0.0)) for v in lot), default=0.0)
    pires: List[str] = []
    pire = 0.0
    muettes = 0
    lues = 0
    for v in lot:
        for p in v["parties"]:
            if "desaccords_demi_tons" not in p:
                muettes += 1
                continue
            lues += 1
            for clef, demi in (p.get("desaccords_demi_tons") or {}).items():
                if abs(float(demi)) > 2.0 + 1e-6:
                    pires.append(f"g{v['graine']}/{p['role']} {clef}={float(demi):+.2f}")
                pire = max(pire, abs(float(demi)))
    ok4 = not pires and muettes == 0
    if muettes:
        print(f"4. hauteurs bornées : INDÉCIDABLE — {muettes} partie(s) sur {muettes + lues} sans "
              f"champ « desaccords_demi_tons » (vérité antérieure à D277) ; ce qui ne peut pas être "
              f"vu n'est pas compté nul")
    else:
        print(f"4. hauteurs bornées : borne déclarée {borne:.1f} demi-ton(s), "
              f"désaccord maximal mesuré {pire:.2f} demi-ton sur {lues} parties"
              + (f" — HORS BORNE : {', '.join(pires[:5])}" if pires else ""))
    if not ok4:
        echecs.append("4")

    print()
    if echecs:
        print(f"EXIGENCES TENUES : {4 - len(set(e[0] for e in echecs))}/4 — "
              f"tombe sur {', '.join(sorted(set(echecs)))}")
        return 1
    print("EXIGENCES TENUES : 4/4 — le corpus porte ce que le § 7 bis demande")
    return 0


if __name__ == "__main__":
    sys.exit(main())
