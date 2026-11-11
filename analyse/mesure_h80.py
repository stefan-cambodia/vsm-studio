"""H80 — la décroissance d'une pièce (le kick), choisie par le grave aux frappes des AUTRES pièces
(docs/CDC-reload-indifferenciable.md § 41).

Pour chaque décroissance candidate (un solo de la pièce, déjà rendu) :
- la QUEUE du solo dans la bande (énergie de `QUEUE_S` après la frappe rapportée à celle de `TETE_S`, dB) — le témoin
  que le réglage atteint le moteur : elle doit croître STRICTEMENT avec la décroissance, sinon REFUS ;
- le facteur `f*` de la pièce, recalé sur sa bande à ses frappes (`mesure_h79.facteur_cale`) : le niveau est une
  conséquence de la décroissance, pas une seconde variable ;
- la batterie sommée, son log-mel au stem (H77), le résidu de CHAQUE pièce à ses frappes, les bandes éloignées (H79) ;
- le DÉBORD : la moyenne des |rendu / stem| dans la bande aux frappes des pièces désignées (`--deborde-sur`) —
  mais à leurs seules frappes ÉLOIGNÉES de `HORS_S` au moins de toute frappe de la pièce réglée : une frappe qui tombe
  SUR un kick lit sa tête, pas sa queue, et la noie (scène d'essai : 0,9 dB toutes frappes confondues, là où les
  frappes d'entre les kicks en disent plus de 6). Le nombre de frappes gardées est publié ; une pièce qui n'en a
  aucune est écartée ET DITE ; s'il n'en reste aucune, REFUS.

Le choix : la décroissance de plus petit débord, arrondi au dixième de dB ; à égalité, la plus longue.

    mesure_h80.py --projet <dossier> --stem <drums.wav> --piece "Batterie - kick+kick2" --bande 0 \
        --point "0.05=<solo du kick>" ... --solo "Batterie - hihat=<wav>" ... --facteur "Batterie - hihat=2.0593" ... \
        --deborde-sur "Batterie - hihat" --deborde-sur "Batterie - snare" --avant <choix.json de H78> --sortie <json>
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Callable, Dict, List, Mapping, Optional, Sequence, Tuple

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mesure_h69 as h69  # noqa: E402
import mesure_h74 as h74  # noqa: E402
import mesure_h77 as h77  # noqa: E402
import mesure_h78 as h78  # noqa: E402
import mesure_h79 as h79  # noqa: E402

TETE_S = (0.0, 0.1)    # la frappe
QUEUE_S = (0.1, 0.3)   # ce qui sonne après
HORS_S = 0.1           # une frappe « d'entre les kicks » est à au moins cela de toute frappe de la pièce réglée


def frappes_hors(instants: Sequence[float], autres: Sequence[float], marge_s: float = HORS_S) -> List[float]:
    """Les `instants` à au moins `marge_s` de TOUT instant de `autres` (avant comme après)."""
    tries = np.sort(np.asarray(list(autres), dtype=float))
    if len(tries) == 0:
        return list(instants)
    gardes: List[float] = []
    for t in instants:
        i = int(np.searchsorted(tries, t))
        voisins = [abs(t - tries[j]) for j in (i - 1, i) if 0 <= j < len(tries)]
        if min(voisins) >= marge_s - 1e-9:
            gardes.append(float(t))
    return gardes


def queue_db(bandes: np.ndarray, instants: Sequence[float], bande: int) -> float:
    """L'énergie de la queue rapportée à celle de la tête, en dB, sommée sur les frappes entières ; −inf si muette."""
    trames = bandes.shape[1]
    par_s = h74.SR_ANALYSE / h74.PAS
    tete, queue = 0.0, 0.0
    for t in instants:
        i0, i1, i2 = (int(round((t + d) * par_s)) for d in (TETE_S[0], TETE_S[1], QUEUE_S[1]))
        if i0 < 0 or i2 > trames:
            continue
        tete += float(bandes[bande, i0:i1].sum())
        queue += float(bandes[bande, i1:i2].sum())
    if tete <= 0:
        raise ValueError("la pièce n'a rien dans la bande à ses frappes")
    return -math.inf if queue <= 0 else round(10 * math.log10(queue / tete), 2)


def queue_croissante(points: Sequence[Tuple[float, float]]) -> bool:
    """Vrai si la queue croît STRICTEMENT avec la décroissance (au moins deux points)."""
    tries = sorted(points)
    return len(tries) >= 2 and all(b[1] > a[1] for a, b in zip(tries, tries[1:], strict=False))


def debord(residus: Mapping[str, Mapping[str, float]], pieces: Sequence[str], bande: str) -> float:
    """La moyenne des |rendu / stem| (dB) dans `bande` aux frappes des `pieces`. Une pièce ou une bande absente : REFUS."""
    if not pieces:
        raise ValueError("aucune pièce où lire le débord")
    return round(sum(abs(residus[p][bande]) for p in pieces) / len(pieces), 2)


def choisir(debords: Mapping[float, float]) -> Optional[float]:
    """La décroissance de plus petit débord (au dixième de dB) ; à égalité, la plus longue."""
    if not debords:
        return None
    return min(debords, key=lambda d: (round(debords[d], 1), -d))


def evaluer(stem: np.ndarray, autres: Mapping[str, np.ndarray], candidats: Mapping[float, np.ndarray],
            frappes: Mapping[str, Sequence[float]], facteurs: Mapping[str, float], piece: str, bande: int,
            deborde_sur: Sequence[str], avants: Mapping[str, Mapping[str, float]], sr: int,
            critere: Callable[[np.ndarray, np.ndarray], Dict[str, float]]) -> Dict[str, Any]:
    """Le protocole de H80 sur des SIGNAUX mono. `autres` : les solos des autres pièces ; `candidats` : décroissance →
    solo de `piece`. Rend {"refus": …} si la queue ne croît pas avec la décroissance."""
    nom_bande = h79.noms_des_bandes()[bande]
    entre = {n: frappes_hors(frappes[n], frappes[piece]) for n in deborde_sur}
    dits_debord = [f"« {n} » : aucune frappe à {HORS_S} s au moins d'une frappe de « {piece} », écartée du débord"
                   for n in deborde_sur if not entre[n]]
    lues = [n for n in deborde_sur if entre[n]]
    if not lues:
        raise ValueError(f"aucune pièce n'a de frappe hors de celles de « {piece} » : le débord ne peut pas être lu")
    bstem = h74.bandes_par_trame(stem, sr)
    b_autres = {n: h74.bandes_par_trame(x, sr) for n, x in autres.items()}
    points: List[Dict[str, Any]] = []
    for d in sorted(candidats):
        solo = candidats[d]
        bsolo = h74.bandes_par_trame(solo, sr)
        bandes = {**b_autres, piece: bsolo}
        ondes = {**autres, piece: solo}
        f, dits = h79.facteur_cale(bstem, bandes, frappes, facteurs, piece, bande)
        fs = {**{n: facteurs[n] for n in autres}, piece: f}
        e = critere(stem, h78.sommer(ondes, fs))
        residus = {n: h79.rendu_sur_stem(bstem, bandes, frappes, fs, n) for n in bandes}
        points.append({"decroissance_s": d, "queue_db": queue_db(bsolo, frappes[piece], bande), "facteur": f,
                       "bornes": dits, "logmel": e["moyen"], "logmel_median": e["median"],
                       "rendu_sur_stem_db": residus,
                       "entre_db": (hors := {n: h79.rendu_sur_stem(bstem, bandes, {**frappes, n: entre[n]}, fs, n)
                                             for n in lues}),
                       "debord_db": debord(hors, lues, nom_bande),
                       "eloignees": {n: h79.bandes_eloignees(avants[n], residus[n]) for n in bandes if n in avants}})
    sortie: Dict[str, Any] = {"bande": nom_bande, "deborde_sur": lues, "points": points, "choix": None,
                              "frappes_entre": {n: [len(entre[n]), len(frappes[n])] for n in deborde_sur},
                              "dits": dits_debord}
    if not queue_croissante([(p["decroissance_s"], p["queue_db"]) for p in points]):
        sortie["refus"] = "la queue du solo ne croît pas strictement avec la décroissance : le réglage n'atteint pas le moteur"
        return sortie
    sortie["choix"] = choisir({p["decroissance_s"]: p["debord_db"] for p in points})
    return sortie


def main(argv: Optional[Sequence[str]] = None) -> int:
    from mesure_h65 import frappes_par_piste

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--projet", type=Path, required=True)
    ap.add_argument("--stem", type=Path, required=True)
    ap.add_argument("--piece", required=True)
    ap.add_argument("--bande", type=int, default=0)
    ap.add_argument("--point", action="append", required=True, help="« décroissance=solo de la pièce.wav »")
    ap.add_argument("--solo", action="append", required=True, help="« nom de la piste MIDI=fichier.wav » (les AUTRES pièces)")
    ap.add_argument("--facteur", action="append", required=True, help="« nom de la piste MIDI=facteur » (les AUTRES pièces)")
    ap.add_argument("--deborde-sur", action="append", required=True)
    ap.add_argument("--avant", type=Path, required=True, help="choix.json de H78 : le résidu du TÉMOIN, pour la garde")
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args(argv)
    pts = [(float(s.split("=", 1)[0]), Path(s.split("=", 1)[1])) for s in a.point]
    chemins = [(s.split("=", 1)[0], Path(s.split("=", 1)[1])) for s in a.solo]
    facteurs = {s.rsplit("=", 1)[0]: float(s.rsplit("=", 1)[1]) for s in a.facteur}
    for chemin in [a.projet / "project.json", a.stem, a.avant] + [c for _, c in chemins] + [c for _, c in pts]:
        if not chemin.is_file() or chemin.stat().st_size == 0:
            print(f"REFUS : {chemin} absent ou vide")
            return 2
    noms = [n for n, _ in chemins]
    if a.piece in noms or sorted(facteurs) != sorted(noms) or any(p not in noms for p in a.deborde_sur):
        print(f"REFUS : autres pièces {noms}, facteurs {sorted(facteurs)}, pièce « {a.piece} », débord sur {a.deborde_sur}")
        return 2
    if len({d for d, _ in pts}) != len(pts):
        print("REFUS : une décroissance donnée deux fois")
        return 2
    p = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    pistes = frappes_par_piste(a.projet, p)
    for nom in noms + [a.piece]:
        if nom not in pistes or not pistes[nom]:
            print(f"REFUS : piste « {nom} » absente ou sans frappe")
            return 2
    frappes = {n: pistes[n] for n in noms + [a.piece]}
    avants: Mapping[str, Mapping[str, float]] = json.loads(a.avant.read_text(encoding="utf-8"))["temoin"]["rendu_sur_stem_db"]
    stem, sr = h69.lire_mono(a.stem)

    def lire(chemin: Path) -> np.ndarray:
        x, s = h69.lire_mono(chemin)
        if s != sr:
            raise SystemExit(f"REFUS : {chemin} à {s} Hz, le stem à {sr} Hz")
        return x

    logmel = h77.outil().logmel

    def critere(s: np.ndarray, b: np.ndarray) -> Dict[str, float]:
        return h77.logmel_au_stem(s.astype(np.float32), b.astype(np.float32), sr, logmel)

    try:
        r = evaluer(stem, {n: lire(c) for n, c in chemins}, {d: lire(c) for d, c in pts}, frappes, facteurs, a.piece,
                    a.bande, a.deborde_sur, avants, sr, critere)
    except ValueError as refus:
        print(f"REFUS : {refus}")
        return 3
    for q in r["points"]:
        res = {n: v[r["bande"]] for n, v in q["entre_db"].items()}
        loin = {n: v for n, v in q["eloignees"].items() if v}
        print(f"{q['decroissance_s']:.2f} s : queue {q['queue_db']:+.2f} dB · f* {q['facteur']}{' (borne)' if q['bornes'] else ''} · "
              f"au stem {q['logmel']:.2f} · débord {q['debord_db']:.2f} dB · {r['bande']} entre les frappes {res} · "
              f"éloignées {loin or 'aucune'}")
    print("frappes entre celles de la pièce réglée (gardées / toutes) :", r["frappes_entre"])
    for ligne in r["dits"]:
        print("DIT :", ligne)
    res_json = {"provenance": {k: str(v) for k, v in vars(a).items()},
                "constantes": {"tete_s": TETE_S, "queue_s": QUEUE_S, "loin_db": h79.LOIN_DB, "marge_db": h79.MARGE_DB,
                               "bornes": h74.BORNES}, **r}
    a.sortie.parent.mkdir(parents=True, exist_ok=True)
    if "refus" in r:
        a.sortie.with_suffix(".refus.json").write_text(json.dumps(res_json, indent=2, ensure_ascii=False), encoding="utf-8")
        print("REFUS :", r["refus"])
        return 3
    a.sortie.write_text(json.dumps(res_json, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"choix : {r['choix']} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
