"""H79 — le niveau d'UNE pièce calé sur sa bande à ses frappes, les autres à facteur FIXE ; le balayage ; la garde
refaite (docs/CDC-reload-indifferenciable.md § 40).

- `facteur_cale` : le facteur `f*` qui porte la batterie, aux frappes de la pièce et dans sa bande, à l'énergie du
  stem : `f*² = (S − Σ_{j≠pièce} fⱼ² Eⱼ) / E_pièce`. Un numérateur négatif ou nul est un REFUS (les autres pièces
  dépassent déjà le stem) ; une borne atteinte est dite.
- `rendu_sur_stem` : le résidu par pièce et par bande (rendu / stem aux frappes, dB) pour des facteurs DONNÉS — celui
  de H75, sans résoudre.
- `bandes_eloignees` : la garde refaite — une bande finit à plus de `LOIN_DB` du stem ET à plus de `MARGE_DB` plus
  loin qu'avant, qu'elle ait été proche ou non (H78 : −3,5 → −11,2 dB sans être « cassée »).
- la ligne de commande : pour chaque facteur du balayage et pour `f*`, la batterie SOMMÉE, son log-mel au stem (H77),
  le résidu de la pièce, ses bandes éloignées par rapport à `--avant` (le résidu du témoin, un choix.json de H78).

    mesure_h79.py --projet <dossier> --stem <drums.wav> --solo "Batterie - kick+kick2=<wav>" ... \
        --facteur "Batterie - hihat=2.0593" ... --piece "Batterie - kick+kick2" --bande 0 \
        --balayage-db 0,3,6,9,12 --avant <choix.json de H78> --sortie <json>
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Dict, List, Mapping, Sequence, Tuple

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mesure_h69 as h69  # noqa: E402
import mesure_h72 as h72  # noqa: E402
import mesure_h74 as h74  # noqa: E402
import mesure_h77 as h77  # noqa: E402
import mesure_h78 as h78  # noqa: E402

LOIN_DB = 6.0    # « loin du stem » après
MARGE_DB = 3.0   # « plus loin qu'avant » d'au moins cela


def noms_des_bandes() -> List[str]:
    return [f"{a}-{b}" for a, b in h72.BANDES_DENSITE]


def facteur_cale(stem: np.ndarray, solos: Mapping[str, np.ndarray], frappes: Mapping[str, Sequence[float]],
                 facteurs: Mapping[str, float], piece: str, bande: int) -> Tuple[float, List[str]]:
    """(f*, dits) sur des BANDES déjà calculées. `facteurs` : ceux des AUTRES pièces (celui de `piece` est ignoré)."""
    autres = [n for n in solos if n != piece]
    manquants = [n for n in autres if n not in facteurs]
    if manquants:
        raise ValueError(f"pièce(s) sans facteur : {manquants}")
    s = h74.energie_aux_frappes(stem, frappes[piece], bande)
    e = h74.energie_aux_frappes(solos[piece], frappes[piece], bande)
    reste = s - sum(facteurs[n] ** 2 * h74.energie_aux_frappes(solos[n], frappes[piece], bande) for n in autres)
    if e <= 0:
        raise ValueError(f"« {piece} » n'a rien dans la bande {bande} à ses frappes")
    if reste <= 0:
        raise ValueError(f"les autres pièces dépassent déjà le stem dans la bande {bande} aux frappes de « {piece} »")
    f = math.sqrt(reste / e)
    dits: List[str] = []
    if f < h74.BORNES[0] or f > h74.BORNES[1]:
        dits.append(f"« {piece} » : facteur calé {f:.3f} hors de {h74.BORNES}, ramené à la borne")
        f = min(max(f, h74.BORNES[0]), h74.BORNES[1])
    return round(f, 4), dits


def rendu_sur_stem(stem: np.ndarray, solos: Mapping[str, np.ndarray], frappes: Mapping[str, Sequence[float]],
                   facteurs: Mapping[str, float], piece: str) -> Dict[str, float]:
    """Rendu / stem (dB) par bande aux frappes de `piece`, tous les solos à leur facteur DONNÉ."""
    sortie: Dict[str, float] = {}
    for k, nom in enumerate(noms_des_bandes()):
        s = h74.energie_aux_frappes(stem, frappes[piece], k)
        if s <= 0:
            continue
        r = sum(facteurs[n] ** 2 * h74.energie_aux_frappes(solos[n], frappes[piece], k) for n in solos)
        sortie[nom] = round(10 * math.log10(max(r / s, 1e-12)), 2)
    return sortie


def bandes_eloignees(avant: Mapping[str, float], apres: Mapping[str, float], loin: float = LOIN_DB,
                     marge: float = MARGE_DB) -> List[str]:
    """Les bandes à plus de `loin` dB du stem APRÈS et à plus de `marge` dB plus loin qu'AVANT."""
    return [b for b in avant if b in apres and abs(apres[b]) > loin and abs(apres[b]) - abs(avant[b]) > marge]


def main() -> int:
    from mesure_h65 import frappes_par_piste

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--projet", type=Path, required=True)
    ap.add_argument("--stem", type=Path, required=True)
    ap.add_argument("--solo", action="append", required=True, help="« nom de la piste MIDI=fichier.wav »")
    ap.add_argument("--facteur", action="append", required=True, help="« nom de la piste MIDI=facteur » (toutes les pièces)")
    ap.add_argument("--piece", required=True)
    ap.add_argument("--bande", type=int, default=0)
    ap.add_argument("--balayage-db", default="0,3,6,9,12", help="dB au-dessus du facteur donné de la pièce")
    ap.add_argument("--avant", type=Path, required=True, help="choix.json de H78 : le résidu du TÉMOIN, pour la garde")
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args()
    chemins = [(s.split("=", 1)[0], Path(s.split("=", 1)[1])) for s in a.solo]
    facteurs = {s.rsplit("=", 1)[0]: float(s.rsplit("=", 1)[1]) for s in a.facteur}
    for chemin in [a.projet / "project.json", a.stem, a.avant] + [c for _, c in chemins]:
        if not chemin.is_file() or chemin.stat().st_size == 0:
            print(f"REFUS : {chemin} absent ou vide")
            return 2
    noms = [n for n, _ in chemins]
    if a.piece not in noms or sorted(facteurs) != sorted(noms):
        print(f"REFUS : pièces {noms}, facteurs {sorted(facteurs)}, pièce calée « {a.piece} »")
        return 2
    p = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    pistes = frappes_par_piste(a.projet, p)
    for nom in noms:
        if nom not in pistes or not pistes[nom]:
            print(f"REFUS : piste « {nom} » absente ou sans frappe")
            return 2
    frappes = {n: pistes[n] for n in noms}
    avant: Mapping[str, float] = json.loads(a.avant.read_text(encoding="utf-8"))["temoin"]["rendu_sur_stem_db"][a.piece]
    stem, sr = h69.lire_mono(a.stem)
    ondes: Dict[str, np.ndarray] = {}
    for n, c in chemins:
        x, s = h69.lire_mono(c)
        if s != sr:
            print(f"REFUS : {c} à {s} Hz, le stem à {sr} Hz")
            return 2
        ondes[n] = x
    bstem = h74.bandes_par_trame(stem, sr)
    bandes = {n: h74.bandes_par_trame(x, sr) for n, x in ondes.items()}
    try:
        cale, dits = facteur_cale(bstem, bandes, frappes, facteurs, a.piece, a.bande)
    except ValueError as refus:
        print(f"REFUS : {refus}")
        return 3
    logmel = h77.outil().logmel
    points: List[Dict[str, Any]] = []
    depart = facteurs[a.piece]
    liste = [(f"+{d:g} dB", round(depart * 10 ** (float(d) / 20), 4)) for d in a.balayage_db.split(",")] + [("f*", cale)]
    for nom, f in sorted(liste, key=lambda t: t[1]):
        fs = dict(facteurs)
        fs[a.piece] = f
        e = h77.logmel_au_stem(stem.astype(np.float32), h78.sommer(ondes, fs).astype(np.float32), sr, logmel)
        r = rendu_sur_stem(bstem, bandes, frappes, fs, a.piece)
        points.append({"nom": nom, "facteur": f, "facteur_db": round(20 * math.log10(f), 2), "logmel": e["moyen"],
                       "logmel_median": e["median"], "rendu_sur_stem_db": r, "eloignees": bandes_eloignees(avant, r)})
        print(f"{nom:>7} : facteur {f} ({points[-1]['facteur_db']:+.2f} dB) · au stem {e['moyen']:.2f} · "
              f"« {a.piece} » {r} · éloignées {points[-1]['eloignees'] or 'aucune'}")
    for ligne in dits:
        print("DIT :", ligne)
    res = {"provenance": {k: str(v) for k, v in vars(a).items()},
           "constantes": {"loin_db": LOIN_DB, "marge_db": MARGE_DB, "bornes": h74.BORNES,
                          "bande": noms_des_bandes()[a.bande]},
           "facteur_cale": cale, "dits": dits, "avant": avant, "points": points}
    a.sortie.parent.mkdir(parents=True, exist_ok=True)
    a.sortie.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
