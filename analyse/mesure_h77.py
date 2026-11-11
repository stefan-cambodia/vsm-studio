"""H77 — le log-mel de la batterie rendue contre le STEM range-t-il les points comme le log-mel du mélange ?
(docs/CDC-reload-indifferenciable.md § 38).

Pour chaque point (une batterie rendue seule, dont le MÉLANGE a déjà été jugé par `tools/ecart-a-l-original.py`),
l'écart log-mel moyen et médian entre le stem de batterie et cette batterie — par la MÊME fonction `logmel` que
l'outil, importée de son fichier, sur le morceau entier. Puis, pour chaque paire de points d'un même morceau : la
paire est TRANCHÉE si ses deux mélanges diffèrent d'au moins `TRANCHE_DB` ; une paire tranchée est CONCORDANTE si le
critère au stem range ses deux points dans le même ordre que le mélange. Une égalité au stem (écart nul) sur une paire
tranchée est DISCORDANTE : un critère qui ne départage pas ne prédit pas.

Un fichier absent ou vide est un REFUS, jamais un point de moins.

    mesure_h77.py --morceau "B4=<drums.wav>" --point "B4:H75=<batterie.wav>=8.42" ... --sortie <json>
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from itertools import combinations
from pathlib import Path
from typing import Any, Dict, List, Mapping, Sequence

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
TRANCHE_DB = 0.10   # deux mélanges à moins de cela ne sont pas départagés (§ 38)
GRAND_DB = 0.5      # au-delà, une discordance est un échec à elle seule


def outil() -> Any:
    """`tools/ecart-a-l-original.py`, chargé par son chemin (son nom porte des tirets)."""
    spec = importlib.util.spec_from_file_location("ecart_a_l_original", RACINE / "tools" / "ecart-a-l-original.py")
    if spec is None or spec.loader is None:
        raise SystemExit("REFUS : tools/ecart-a-l-original.py illisible")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def logmel_au_stem(stem: np.ndarray, rendu: np.ndarray, sr: int, logmel: Any) -> Dict[str, float]:
    """L'écart log-mel (moyen, médian) de `rendu` à `stem`, mono, comme le § 6 de l'outil le fait pour le mélange."""
    n = min(len(stem), len(rendu))
    lo, lr = logmel(stem[:n], sr), logmel(rendu[:n], sr)
    k = min(lo.shape[1], lr.shape[1])
    d = np.abs(lo[:, :k] - lr[:, :k])
    return {"moyen": round(float(d.mean()), 2), "median": round(float(np.median(d)), 2)}


def paires(points: Mapping[str, Mapping[str, float]], tranche_db: float = TRANCHE_DB,
           grand_db: float = GRAND_DB) -> List[Dict[str, Any]]:
    """Chaque paire de points d'UN morceau : {a, b, ecart_melange, ecart_stem, tranchee, grande, concordante}.

    `points[nom] = {"melange": …, "stem": …}`. `concordante` vaut None pour une paire non tranchée.
    """
    sortie: List[Dict[str, Any]] = []
    for a, b in combinations(points, 2):
        em = round(points[b]["melange"] - points[a]["melange"], 4)
        es = round(points[b]["stem"] - points[a]["stem"], 4)
        tranchee = abs(em) >= tranche_db - 1e-9
        concordante = None if not tranchee else bool(es != 0.0 and (es > 0) == (em > 0))
        sortie.append({"a": a, "b": b, "ecart_melange": em, "ecart_stem": es, "tranchee": tranchee,
                       "grande": abs(em) > grand_db, "concordante": concordante})
    return sortie


def bilan(toutes: Sequence[Mapping[str, Any]]) -> Dict[str, int]:
    tranchees = [p for p in toutes if p["tranchee"]]
    return {"paires": len(toutes), "tranchees": len(tranchees),
            "concordantes": sum(1 for p in tranchees if p["concordante"]),
            "discordantes": sum(1 for p in tranchees if not p["concordante"]),
            "discordantes_grandes": sum(1 for p in tranchees if p["grande"] and not p["concordante"])}


def main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--morceau", action="append", required=True, help="nom=<stem de batterie>")
    ap.add_argument("--point", action="append", required=True, help="morceau:nom=<batterie rendue>=<log-mel du mélange>")
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args(argv)
    o = outil()
    stems = dict(m.split("=", 1) for m in a.morceau)
    plan: Dict[str, Dict[str, tuple[Path, float]]] = {m: {} for m in stems}
    for p in a.point:
        tete, wav, melange = p.rsplit("=", 2)
        morceau, nom = tete.split(":", 1)
        if morceau not in plan:
            print(f"REFUS : point « {p} » d'un morceau sans stem")
            return 2
        plan[morceau][nom] = (Path(wav), float(melange))
    for f in [Path(s) for s in stems.values()] + [w for pts in plan.values() for w, _ in pts.values()]:
        if not f.is_file() or f.stat().st_size == 0:
            print(f"REFUS : {f} absent ou vide")
            return 2
    res: Dict[str, Any] = {"provenance": {"morceau": a.morceau, "point": a.point},
                           "constantes": {"tranche_db": TRANCHE_DB, "grand_db": GRAND_DB}, "morceaux": {}}
    toutes: List[Dict[str, Any]] = []
    for morceau, pts in plan.items():
        ys, srs = o.lire(Path(stems[morceau]))
        ms = ys.mean(axis=1)
        points: Dict[str, Dict[str, float]] = {}
        for nom, (wav, melange) in pts.items():
            yr, srr = o.lire(wav)
            if srr != srs:
                print(f"REFUS : {wav} à {srr} Hz, le stem à {srs} Hz")
                return 2
            if abs(len(yr) - len(ys)) > srs * 0.5:
                print(f"DIT : {morceau} · {nom} : durées différentes ({len(yr)/srr:.1f} s contre {len(ys)/srs:.1f} s)")
            e = logmel_au_stem(ms, yr.mean(axis=1), srs, o.logmel)
            points[nom] = {"melange": melange, "stem": e["moyen"], "stem_median": e["median"]}
            print(f"{morceau} · {nom} : mélange {melange:.2f} · au stem {e['moyen']:.2f} (médian {e['median']:.2f})")
        ps = paires(points)
        for p in ps:
            etat = "non tranchée" if not p["tranchee"] else ("concordante" if p["concordante"] else "DISCORDANTE")
            print(f"  {p['a']} → {p['b']} : mélange {p['ecart_melange']:+.2f} · stem {p['ecart_stem']:+.2f} — {etat}")
        res["morceaux"][morceau] = {"points": points, "paires": ps}
        toutes += ps
    res["bilan"] = bilan(toutes)
    print("bilan :", res["bilan"])
    a.sortie.write_text(json.dumps(res, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
