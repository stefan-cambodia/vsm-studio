"""H76 — la MACHINE de chaque pièce de batterie, choisie par le résidu de H75 (docs/CDC-reload-indifferenciable.md § 37).

Pour chaque pièce *i* et chaque machine candidate *M* : le solo de *i* est remplacé par celui de *M*, les autres pièces
gardant leur solo TÉMOIN (celui de H75) ; le système de H75 (`mesure_h74.resoudre_toutes_bandes`) est résolu et son
résidu pondéré `R(i, M)` relevé. La pièce prend la machine de plus petit résidu SI elle le baisse d'au moins `SEUIL`
(5 %) par rapport au témoin ; sinon elle garde sa machine. Un solo MUET (à plus de `MUET_DB` sous le solo témoin) est
écarté ET DIT. La combinaison des choix est résolue une fois de plus, et ce que le choix CASSE est relevé : les bandes
d'une pièce changée qui étaient à moins de `PRES_DB` du stem et passent au-delà de `LOIN_DB`.

Le choix se fait sur le STEM ; il se juge au mélange, ailleurs (`tools/ecart-a-l-original.py`). LIMITE DITE : le résidu
pondéré est celui du système AVANT le bornage des facteurs (comme en H75) — un candidat dont un facteur touche la
borne a un résidu optimiste, et chaque borne atteinte est relevée à côté de son résidu.

    mesure_h76.py --projet <dossier> --stem <drums.wav> --temoin "Batterie - snare=<wav>" ... \
        --candidat "Batterie - snare:vsm.tr909=<wav>" ... --sortie <json>
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence, Tuple

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mesure_h69 as h69  # noqa: E402
import mesure_h74 as h74  # noqa: E402

SEUIL = 0.05      # la baisse relative du résidu qui fait un choix
MUET_DB = -60.0   # un solo à ce niveau (ou moins) sous le solo témoin est muet
PRES_DB = 3.0     # « près du stem » avant le choix
LOIN_DB = 6.0     # « loin du stem » après


def choisir(residu_temoin: float, residus: Mapping[str, float], seuil: float = SEUIL) -> Optional[str]:
    """La machine de plus petit résidu, si elle baisse celui du témoin d'au moins `seuil` (relatif) ; sinon `None` (la
    pièce garde sa machine). À résidu égal, la première dans l'ordre des noms : un choix rejouable."""
    if not residus or residu_temoin <= 0:
        return None
    meilleure = min(sorted(residus), key=lambda m: residus[m])
    return meilleure if residus[meilleure] <= residu_temoin * (1.0 - seuil) else None


def bandes_cassees(avant: Mapping[str, float], apres: Mapping[str, float],
                   pres: float = PRES_DB, loin: float = LOIN_DB) -> List[str]:
    """Les bandes à moins de `pres` dB du stem AVANT le choix qui sont à plus de `loin` dB APRÈS."""
    return [b for b in avant if b in apres and abs(avant[b]) < pres and abs(apres[b]) > loin]


def niveau_relatif_db(solo: np.ndarray, temoin: np.ndarray) -> float:
    """L'énergie totale d'un solo (bandes × trames) rapportée à celle du solo témoin, en dB ; −inf si rien."""
    e, t = float(solo.sum()), float(temoin.sum())
    if e <= 0 or t <= 0:
        return -math.inf
    return 10.0 * math.log10(e / t)


def evaluer(stem: np.ndarray, temoins: Mapping[str, np.ndarray],
            candidats: Mapping[str, Mapping[str, np.ndarray]], frappes: Mapping[str, Sequence[float]],
            seuil: float = SEUIL) -> Dict[str, object]:
    """Le protocole de H76 sur des bandes déjà calculées. `candidats` : pièce → machine → bandes du solo."""
    base = h74.resoudre_toutes_bandes(stem, dict(temoins), frappes)
    r0 = float(base["residu_pondere"])  # type: ignore[arg-type]
    table: Dict[str, Dict[str, object]] = {}
    choix: Dict[str, Optional[str]] = {}
    dits: List[str] = []
    for piece in temoins:
        lignes: Dict[str, object] = {}
        residus: Dict[str, float] = {}
        for machine, solo in candidats.get(piece, {}).items():
            niveau = niveau_relatif_db(solo, temoins[piece])
            if niveau <= MUET_DB:
                dits.append(f"« {piece} » · {machine} : solo MUET ({niveau:.1f} dB sous le témoin), écarté")
                lignes[machine] = {"muet": True, "niveau_db": None if math.isinf(niveau) else round(niveau, 1)}
                continue
            essai = dict(temoins)
            essai[piece] = solo
            r = h74.resoudre_toutes_bandes(stem, essai, frappes)
            residus[machine] = float(r["residu_pondere"])  # type: ignore[arg-type]
            lignes[machine] = {"muet": False, "niveau_db": round(niveau, 1), "residu_pondere": r["residu_pondere"],
                               "facteur": r["facteurs"][piece],  # type: ignore[index]
                               "bornes": [d for d in r["dits"]]}  # type: ignore[attr-defined]
        choix[piece] = choisir(r0, residus, seuil)
        table[piece] = lignes
    combine = dict(temoins)
    for piece, choisie in choix.items():
        if choisie is not None:
            combine[piece] = candidats[piece][choisie]
    final = h74.resoudre_toutes_bandes(stem, combine, frappes) if any(choix.values()) else base
    avant: Mapping[str, Mapping[str, float]] = base["rendu_sur_stem_db"]  # type: ignore[assignment]
    apres: Mapping[str, Mapping[str, float]] = final["rendu_sur_stem_db"]  # type: ignore[assignment]
    casse = {p: bandes_cassees(avant[p], apres[p]) for p, m in choix.items() if m is not None}
    return {"seuil": seuil, "residu_temoin": r0, "temoin": base, "table": table, "choix": choix,
            "combinaison": final, "casse": casse, "dits": dits}


def main() -> int:
    from mesure_h65 import frappes_par_piste

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--projet", type=Path, required=True)
    ap.add_argument("--stem", type=Path, required=True)
    ap.add_argument("--temoin", action="append", required=True, help="« nom de la piste MIDI=fichier.wav » (le solo de H75)")
    ap.add_argument("--candidat", action="append", default=[], help="« nom de la piste MIDI:machine=fichier.wav »")
    ap.add_argument("--seuil", type=float, default=SEUIL)
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args()
    temoins_chemins: List[Tuple[str, Path]] = [(s.split("=", 1)[0], Path(s.split("=", 1)[1])) for s in a.temoin]
    candidats_chemins: List[Tuple[str, str, Path]] = []
    for s in a.candidat:
        cle, chemin = s.split("=", 1)
        piece, machine = cle.rsplit(":", 1)
        candidats_chemins.append((piece, machine, Path(chemin)))
    for chemin in ([a.projet / "project.json", a.stem] + [c for _, c in temoins_chemins]
                   + [c for _, _, c in candidats_chemins]):
        if not chemin.is_file() or chemin.stat().st_size == 0:
            print(f"REFUS : {chemin} absent ou vide")
            return 2
    p = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    pistes = frappes_par_piste(a.projet, p)
    noms = [n for n, _ in temoins_chemins]
    for nom in noms:
        if nom not in pistes or not pistes[nom]:
            print(f"REFUS : piste « {nom} » absente ou sans frappe ({', '.join(pistes)})")
            return 2
    for piece, machine, _ in candidats_chemins:
        if piece not in noms:
            print(f"REFUS : le candidat {machine} désigne « {piece} », qui n'a pas de témoin")
            return 2

    def bandes(chemin: Path, sr_attendu: Optional[int]) -> Tuple[np.ndarray, int]:
        x, sr = h69.lire_mono(chemin)
        if sr_attendu is not None and sr != sr_attendu:
            raise SystemExit(f"REFUS : {chemin} à {sr} Hz, le stem à {sr_attendu} Hz")
        return h74.bandes_par_trame(x, sr), sr

    stem, sr = bandes(a.stem, None)
    temoins = {n: bandes(c, sr)[0] for n, c in temoins_chemins}
    candidats: Dict[str, Dict[str, np.ndarray]] = {}
    for piece, machine, chemin in candidats_chemins:
        candidats.setdefault(piece, {})[machine] = bandes(chemin, sr)[0]
    frappes = {n: pistes[n] for n in noms}
    r = evaluer(stem, temoins, candidats, frappes, a.seuil)
    res = {"provenance": {k: str(v) for k, v in vars(a).items()},
           "constantes": {"seuil": a.seuil, "muet_db": MUET_DB, "pres_db": PRES_DB, "loin_db": LOIN_DB,
                          "avant_s": h74.AVANT_S, "apres_s": h74.APRES_S, "bornes": h74.BORNES},
           "frappes": {n: len(v) for n, v in frappes.items()}, **r}
    a.sortie.parent.mkdir(parents=True, exist_ok=True)
    a.sortie.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"résidu pondéré du témoin (H75) : {r['residu_temoin']}")
    table: Mapping[str, Mapping[str, Mapping[str, object]]] = r["table"]  # type: ignore[assignment]
    choix: Mapping[str, Optional[str]] = r["choix"]  # type: ignore[assignment]
    for piece in noms:
        cases = []
        for machine, ligne in table[piece].items():
            if ligne["muet"]:
                cases.append(f"{machine} MUET")
            else:
                borne = " (borne)" if ligne["bornes"] else ""
                cases.append(f"{machine} {ligne['residu_pondere']}{borne}")
        print(f"« {piece} » : {' · '.join(cases) or 'aucun candidat'} → {choix[piece] or 'garde sa machine'}")
    final: Mapping[str, object] = r["combinaison"]  # type: ignore[assignment]
    print(f"résidu pondéré de la combinaison : {final['residu_pondere']}")
    for n in noms:
        print(f"  « {n} » : facteur {final['facteurs'][n]} ; rendu/stem (dB) : {final['rendu_sur_stem_db'][n]}")  # type: ignore[index]
    for ligne in list(final["dits"]) + list(r["dits"]):  # type: ignore[call-overload]
        print("DIT :", ligne)
    for piece, cassees in r["casse"].items():  # type: ignore[attr-defined]
        print(f"CASSE « {piece} » : {cassees or 'rien'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
