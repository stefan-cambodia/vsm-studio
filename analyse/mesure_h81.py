"""H81 — le grave d'entre les kicks : où il est dans l'original, stem par stem, et qui le porte dans le rendu, piste par
piste (docs/CDC-reload-indifferenciable.md § 42).

L'énergie moyenne par fenêtre (celles de H74 : `AVANT_S` avant, `APRES_S` après) dans des bandes données, sur DEUX
populations de frappes — celles de la pièce de référence (le kick : le témoin) et celles d'une autre pièce situées à
`mesure_h80.HORS_S` au moins de toute frappe de la première (« entre les kicks ») —, pour chaque signal nommé.

Deux CONTRÔLES de somme passent avant toute lecture : `--somme "mélange=partie+partie+…"` — l'énergie de la somme des
parties (somme des ONDES, pas des énergies) doit rendre celle du mélange à `TOLERANCE_DB` près dans chaque bande et
chaque population. Sinon REFUS : des parts qui ne s'additionnent pas ne se lisent pas.

    mesure_h81.py --projet <dossier> --reference "Batterie - kick+kick2" --entre "Batterie - hihat" --bandes 0,1 \
        --signal "original=<wav>" --signal "stem drums=<wav>" ... --batterie "Batterie - hihat=<solo>=<facteur>" ... \
        --somme "original=stem drums+stem bass+stem other+stem vocals" --rapport "stem bass/stem drums" ... --sortie <json>
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Dict, Mapping, Optional, Sequence

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mesure_h69 as h69  # noqa: E402
import mesure_h74 as h74  # noqa: E402
import mesure_h78 as h78  # noqa: E402
import mesure_h79 as h79  # noqa: E402
import mesure_h80 as h80  # noqa: E402

TOLERANCE_DB = 1.0


def energies(ondes: Mapping[str, np.ndarray], populations: Mapping[str, Sequence[float]], bandes: Sequence[int],
             sr: int) -> Dict[str, Dict[str, Dict[str, float]]]:
    """signal → population → bande (nom) → énergie moyenne par fenêtre (linéaire). Une population vide est un REFUS."""
    noms = h79.noms_des_bandes()
    vides = [p for p, v in populations.items() if not list(v)]
    if vides:
        raise ValueError(f"population(s) sans frappe : {vides}")
    sortie: Dict[str, Dict[str, Dict[str, float]]] = {}
    for nom, x in ondes.items():
        b = h74.bandes_par_trame(x, sr)
        sortie[nom] = {p: {noms[k]: h74.energie_aux_frappes(b, list(inst), k) for k in bandes}
                       for p, inst in populations.items()}
    return sortie


def rapport_db(e: Mapping[str, Mapping[str, Mapping[str, float]]], haut: str, bas: str) -> Dict[str, Dict[str, Optional[float]]]:
    """10·log10(E[haut] / E[bas]) par population et par bande ; None si l'un des deux est nul (DIT par l'appelant)."""
    sortie: Dict[str, Dict[str, Optional[float]]] = {}
    for p in e[haut]:
        sortie[p] = {}
        for b in e[haut][p]:
            a, c = e[haut][p][b], e[bas][p][b]
            sortie[p][b] = None if a <= 0 or c <= 0 else round(10 * math.log10(a / c), 2)
    return sortie


def controle_de_somme(ecarts: Mapping[str, Mapping[str, Optional[float]]], tolerance_db: float = TOLERANCE_DB) -> bool:
    """Vrai si la somme des parties rend le mélange à `tolerance_db` près PARTOUT ; une valeur absente est un échec."""
    valeurs = [v for par_bande in ecarts.values() for v in par_bande.values()]
    return bool(valeurs) and all(v is not None and abs(v) <= tolerance_db + 1e-9 for v in valeurs)


def main(argv: Optional[Sequence[str]] = None) -> int:
    from mesure_h65 import frappes_par_piste

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--projet", type=Path, required=True)
    ap.add_argument("--reference", required=True, help="la pièce dont les frappes font la population témoin")
    ap.add_argument("--entre", required=True, help="la pièce dont on garde les frappes hors de celles de la référence")
    ap.add_argument("--bandes", default="0,1")
    ap.add_argument("--signal", action="append", required=True, help="« nom=fichier.wav »")
    ap.add_argument("--batterie", action="append", default=[], help="« pièce=solo.wav=facteur » : sommés sous le nom « batterie »")
    ap.add_argument("--somme", action="append", default=[], help="« mélange=partie+partie » : un contrôle")
    ap.add_argument("--rapport", action="append", default=[], help="« haut/bas »")
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args(argv)
    chemins = [(s.split("=", 1)[0], Path(s.split("=", 1)[1])) for s in a.signal]
    pieces = [(s.split("=")[0], Path(s.split("=")[1]), float(s.split("=")[2])) for s in a.batterie]
    for chemin in [a.projet / "project.json"] + [c for _, c in chemins] + [c for _, c, _ in pieces]:
        if not chemin.is_file() or chemin.stat().st_size == 0:
            print(f"REFUS : {chemin} absent ou vide")
            return 2
    p = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    pistes = frappes_par_piste(a.projet, p)
    for nom in (a.reference, a.entre):
        if nom not in pistes or not pistes[nom]:
            print(f"REFUS : piste « {nom} » absente ou sans frappe")
            return 2
    hors = h80.frappes_hors(pistes[a.entre], pistes[a.reference])
    populations = {"aux kicks": pistes[a.reference], "entre les kicks": hors}
    ondes: Dict[str, np.ndarray] = {}
    sr = 0
    for nom, chemin in chemins:
        x, s = h69.lire_mono(chemin)
        if sr and s != sr:
            print(f"REFUS : {chemin} à {s} Hz, les autres à {sr} Hz")
            return 2
        sr = s
        ondes[nom] = x
    if pieces:
        solos: Dict[str, np.ndarray] = {}
        for nom, chemin, _ in pieces:
            x, s = h69.lire_mono(chemin)
            if s != sr:
                print(f"REFUS : {chemin} à {s} Hz, les autres à {sr} Hz")
                return 2
            solos[nom] = x
        ondes["batterie"] = h78.sommer(solos, {n: f for n, _, f in pieces})
    controles: Dict[str, Any] = {}
    melanges = [s.split("=", 1)[0] for s in a.somme]
    if len(set(melanges)) != len(melanges):
        print(f"REFUS : deux contrôles de somme pour le même mélange ({melanges}) — le second effacerait le premier")
        return 2
    for s in a.somme:
        melange, parties = s.split("=", 1)
        noms = parties.split("+")
        absents = [n for n in [melange] + noms if n not in ondes]
        if absents:
            print(f"REFUS : signal(aux) inconnu(s) dans la somme : {absents}")
            return 2
        n = min(len(ondes[k]) for k in noms)
        ondes[f"Σ {melange}"] = np.sum([ondes[k][:n] for k in noms], axis=0)
    bandes = [int(k) for k in a.bandes.split(",")]
    try:
        e = energies(ondes, populations, bandes, sr)
    except ValueError as refus:
        print(f"REFUS : {refus}")
        return 3
    print(f"populations : aux kicks {len(populations['aux kicks'])} · entre les kicks {len(hors)} sur {len(pistes[a.entre])} "
          f"frappes de « {a.entre} »")
    tenu = True
    for s in a.somme:
        melange = s.split("=", 1)[0]
        ecart = rapport_db(e, f"Σ {melange}", melange)
        ok = controle_de_somme(ecart)
        tenu = tenu and ok
        controles[melange] = {"ecart_db": ecart, "tenu": ok}
        print(f"contrôle de somme « {melange} » : {ecart} — {'tenu' if ok else 'EN ÉCHEC'}")
    ref = chemins[0][0]
    table = {nom: rapport_db(e, nom, ref) for nom in e}
    rapports = {}
    for r in a.rapport:
        haut, bas = r.split("/", 1)
        if haut not in e or bas not in e:
            print(f"REFUS : rapport « {r} » sur un signal inconnu")
            return 2
        rapports[r] = rapport_db(e, haut, bas)
    res = {"provenance": {k: str(v) for k, v in vars(a).items()}, "constantes": {"tolerance_db": TOLERANCE_DB,
           "hors_s": h80.HORS_S, "avant_s": h74.AVANT_S, "apres_s": h74.APRES_S},
           "populations": {k: len(v) for k, v in populations.items()}, "controles": controles,
           "reference_de_la_table": ref, "table_db": table, "rapports_db": rapports}
    a.sortie.parent.mkdir(parents=True, exist_ok=True)
    if not tenu:
        a.sortie.with_suffix(".refus.json").write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
        print("REFUS : une somme ne rend pas son mélange — aucune part n'est lue")
        return 3
    print(f"table (dB rapportés à « {ref} ») :")
    for nom, v in table.items():
        print(f"  {nom:>16} : " + " · ".join(f"{pop} {b} {x if x is None else format(x, '+.1f')}" for pop, par in v.items()
                                              for b, x in par.items()))
    for r, v in rapports.items():
        print(f"rapport {r} : {v}")
    a.sortie.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
