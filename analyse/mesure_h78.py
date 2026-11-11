"""H78 — le choix de H76 rejoué avec le critère de H77 (docs/CDC-reload-indifferenciable.md § 39).

Pour chaque pièce *i* et chaque machine candidate *M* : les niveaux sont ceux du système de H75
(`mesure_h74.resoudre_toutes_bandes`) résolu avec le solo de *i* remplacé par celui de *M* ; la batterie du candidat
est la SOMME des solos à ces facteurs — aucun rendu — et son critère est le log-mel de cette somme contre le stem de
batterie (`mesure_h77.logmel_au_stem`, la fonction `logmel` de l'outil du § 0).

Le choix d'une pièce : un solo MUET est écarté et dit ; un candidat qui CASSE une bande de sa pièce
(`mesure_h76.bandes_cassees`) est refusé et dit ; parmi les autres, celui de plus petit log-mel au stem, s'il le
baisse d'au moins `GAIN_DB` sous celui du témoin ; sinon la pièce garde sa machine.

Le CONTRÔLE de la somme passe avant tout : `--controle <batterie rendue du témoin>` — la batterie témoin sommée doit
rendre le log-mel au stem de la batterie RENDUE à `TOLERANCE_DB` près, sinon REFUS (la somme n'est pas le rendu).

    mesure_h78.py --projet <dossier> --stem <drums.wav> --controle <essai-batterie.wav de H75> \
        --temoin "Batterie - snare=<wav>" ... --candidat "Batterie - snare:vsm.tr909=<wav>" ... --sortie <json>
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
import mesure_h76 as h76  # noqa: E402
import mesure_h77 as h77  # noqa: E402

GAIN_DB = 0.5         # la baisse du log-mel au stem qui fait un choix (§ 38.2 : en dessous, il ne prédit rien)
TOLERANCE_DB = 0.05   # la somme des solos contre la batterie rendue


def sommer(solos: Mapping[str, np.ndarray], facteurs: Mapping[str, float]) -> np.ndarray:
    """La batterie sans la rendre : chaque solo à son facteur. Une pièce sans facteur est un REFUS, pas un ×1."""
    manquants = [n for n in solos if n not in facteurs]
    if manquants:
        raise ValueError(f"pièce(s) sans facteur : {manquants}")
    n = min(len(s) for s in solos.values())
    return np.sum([facteurs[p] * s[:n] for p, s in solos.items()], axis=0)


def choisir(logmel_temoin: float, candidats: Mapping[str, Mapping[str, Any]], gain_db: float = GAIN_DB
            ) -> Tuple[Optional[str], List[str]]:
    """(la machine choisie ou None, les machines REFUSÉES pour une bande cassée).

    `candidats[machine] = {"logmel": …, "cassees": [...]}`. Les muets n'y sont pas (écartés avant). À log-mel égal, la
    première dans l'ordre des noms."""
    refusees = sorted(m for m, c in candidats.items() if c["cassees"])
    admis = {m: float(c["logmel"]) for m, c in candidats.items() if not c["cassees"]}
    if not admis:
        return None, refusees
    meilleure = min(sorted(admis), key=lambda m: admis[m])
    return (meilleure if admis[meilleure] <= logmel_temoin - gain_db + 1e-9 else None), refusees


def evaluer(stem: np.ndarray, temoins: Mapping[str, np.ndarray], candidats: Mapping[str, Mapping[str, np.ndarray]],
            frappes: Mapping[str, Sequence[float]], sr: int, critere: Callable[[np.ndarray, np.ndarray], Dict[str, float]],
            gain_db: float = GAIN_DB) -> Dict[str, Any]:
    """Le protocole de H78 sur des SIGNAUX mono (le critère veut l'onde, le système de H75 ses bandes).

    `critere(stem, batterie)` rend `{"moyen": …, "median": …}`."""
    bandes = {n: h74.bandes_par_trame(x, sr) for n, x in temoins.items()}
    bstem = h74.bandes_par_trame(stem, sr)
    base = h74.resoudre_toutes_bandes(bstem, dict(bandes), frappes)
    l0 = critere(stem, sommer(temoins, base["facteurs"]))  # type: ignore[arg-type]
    avant: Mapping[str, Mapping[str, float]] = base["rendu_sur_stem_db"]  # type: ignore[assignment]
    table: Dict[str, Dict[str, Any]] = {}
    choix: Dict[str, Optional[str]] = {}
    dits: List[str] = []
    for piece in temoins:
        lignes: Dict[str, Any] = {}
        for machine, solo in candidats.get(piece, {}).items():
            bsolo = h74.bandes_par_trame(solo, sr)
            niveau = h76.niveau_relatif_db(bsolo, bandes[piece])
            if niveau <= h76.MUET_DB:
                dits.append(f"« {piece} » · {machine} : solo MUET ({niveau:.1f} dB sous le témoin), écarté")
                lignes[machine] = {"muet": True}
                continue
            essai_b = dict(bandes)
            essai_b[piece] = bsolo
            r = h74.resoudre_toutes_bandes(bstem, essai_b, frappes)
            essai = dict(temoins)
            essai[piece] = solo
            e = critere(stem, sommer(essai, r["facteurs"]))  # type: ignore[arg-type]
            cassees = h76.bandes_cassees(avant[piece], r["rendu_sur_stem_db"][piece])  # type: ignore[index]
            if cassees:
                dits.append(f"« {piece} » · {machine} : casse {cassees}, refusée")
            lignes[machine] = {"muet": False, "logmel": e["moyen"], "logmel_median": e["median"],
                               "facteurs": r["facteurs"], "bornes": list(r["dits"]),  # type: ignore[call-overload]
                               "cassees": cassees}
        choix[piece], _ = choisir(l0["moyen"], {m: c for m, c in lignes.items() if not c["muet"]}, gain_db)
        table[piece] = lignes
    combinaison: Optional[Dict[str, Any]] = None
    if any(choix.values()):
        sig = dict(temoins)
        bnd = dict(bandes)
        for piece, m in choix.items():
            if m is not None:
                sig[piece] = candidats[piece][m]
                bnd[piece] = h74.bandes_par_trame(sig[piece], sr)
        final = h74.resoudre_toutes_bandes(bstem, bnd, frappes)
        e = critere(stem, sommer(sig, final["facteurs"]))  # type: ignore[arg-type]
        apres: Mapping[str, Mapping[str, float]] = final["rendu_sur_stem_db"]  # type: ignore[assignment]
        combinaison = {"facteurs": final["facteurs"], "logmel": e["moyen"], "logmel_median": e["median"],
                       "bornes": list(final["dits"]),  # type: ignore[call-overload]
                       "rendu_sur_stem_db": apres,
                       "cassees": {p: h76.bandes_cassees(avant[p], apres[p]) for p, m in choix.items() if m is not None}}
    return {"gain_db": gain_db, "logmel_temoin": l0["moyen"], "logmel_temoin_median": l0["median"],
            "temoin": {"facteurs": base["facteurs"], "rendu_sur_stem_db": avant}, "table": table, "choix": choix,
            "combinaison": combinaison, "dits": dits}


def controle_de_la_somme(somme_db: float, rendu_db: float, tolerance_db: float = TOLERANCE_DB) -> bool:
    return math.isfinite(somme_db) and math.isfinite(rendu_db) and abs(somme_db - rendu_db) <= tolerance_db + 1e-9


def main() -> int:
    from mesure_h65 import frappes_par_piste

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--projet", type=Path, required=True)
    ap.add_argument("--stem", type=Path, required=True)
    ap.add_argument("--controle", type=Path, required=True, help="la batterie RENDUE du témoin (H75)")
    ap.add_argument("--temoin", action="append", required=True, help="« nom de la piste MIDI=fichier.wav »")
    ap.add_argument("--candidat", action="append", default=[], help="« nom de la piste MIDI:machine=fichier.wav »")
    ap.add_argument("--gain", type=float, default=GAIN_DB)
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args()
    tc: List[Tuple[str, Path]] = [(s.split("=", 1)[0], Path(s.split("=", 1)[1])) for s in a.temoin]
    cc: List[Tuple[str, str, Path]] = []
    for s in a.candidat:
        cle, chemin = s.split("=", 1)
        piece, machine = cle.rsplit(":", 1)
        cc.append((piece, machine, Path(chemin)))
    for chemin in [a.projet / "project.json", a.stem, a.controle] + [c for _, c in tc] + [c for _, _, c in cc]:
        if not chemin.is_file() or chemin.stat().st_size == 0:
            print(f"REFUS : {chemin} absent ou vide")
            return 2
    p = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    pistes = frappes_par_piste(a.projet, p)
    noms = [n for n, _ in tc]
    for nom in noms:
        if nom not in pistes or not pistes[nom]:
            print(f"REFUS : piste « {nom} » absente ou sans frappe ({', '.join(pistes)})")
            return 2
    for piece, machine, _ in cc:
        if piece not in noms:
            print(f"REFUS : le candidat {machine} désigne « {piece} », qui n'a pas de témoin")
            return 2
    stem, sr = h69.lire_mono(a.stem)

    def lire(chemin: Path) -> np.ndarray:
        x, s = h69.lire_mono(chemin)
        if s != sr:
            raise SystemExit(f"REFUS : {chemin} à {s} Hz, le stem à {sr} Hz")
        return x

    logmel = h77.outil().logmel

    def critere(s: np.ndarray, b: np.ndarray) -> Dict[str, float]:
        return h77.logmel_au_stem(s.astype(np.float32), b.astype(np.float32), sr, logmel)

    temoins = {n: lire(c) for n, c in tc}
    candidats: Dict[str, Dict[str, np.ndarray]] = {}
    for piece, machine, chemin in cc:
        candidats.setdefault(piece, {})[machine] = lire(chemin)
    frappes = {n: pistes[n] for n in noms}
    r = evaluer(stem, temoins, candidats, frappes, sr, critere, a.gain)
    rendu = critere(stem, lire(a.controle))
    ok = controle_de_la_somme(r["logmel_temoin"], rendu["moyen"])
    res = {"provenance": {k: str(v) for k, v in vars(a).items()},
           "constantes": {"gain_db": a.gain, "tolerance_db": TOLERANCE_DB, "muet_db": h76.MUET_DB,
                          "pres_db": h76.PRES_DB, "loin_db": h76.LOIN_DB, "bornes": h74.BORNES},
           "controle": {"somme": r["logmel_temoin"], "rendu": rendu["moyen"], "tenu": ok},
           "frappes": {n: len(v) for n, v in frappes.items()}, **r}
    a.sortie.parent.mkdir(parents=True, exist_ok=True)
    print(f"contrôle de la somme : témoin sommé {r['logmel_temoin']:.2f} · batterie rendue {rendu['moyen']:.2f} — "
          f"{'tenu' if ok else 'EN ÉCHEC'}")
    if not ok:
        a.sortie.with_suffix(".refus.json").write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
        print("REFUS : la somme des solos n'est pas la batterie rendue — aucune table n'est lue")
        return 3
    a.sortie.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    for piece in noms:
        cases = []
        for machine, ligne in r["table"][piece].items():
            if ligne["muet"]:
                cases.append(f"{machine} MUET")
            else:
                marques = (" (borne)" if ligne["bornes"] else "") + (" (CASSE)" if ligne["cassees"] else "")
                cases.append(f"{machine} {ligne['logmel']:.2f}{marques}")
        print(f"« {piece} » : {' · '.join(cases) or 'aucun candidat'} → {r['choix'][piece] or 'garde sa machine'}")
    c = r["combinaison"]
    if c is None:
        print("AUCUNE pièce ne change de machine : la combinaison est le témoin")
    else:
        print(f"combinaison : log-mel au stem {c['logmel']:.2f} (témoin {r['logmel_temoin']:.2f}) ; facteurs {c['facteurs']} ; "
              f"cassées {c['cassees']}")
        for ligne in c["bornes"]:
            print("DIT :", ligne)
    for ligne in r["dits"]:
        print("DIT :", ligne)
    return 0


if __name__ == "__main__":
    sys.exit(main())
