#!/usr/bin/env python3
"""D282 : la coupure adaptée de la basse, jugée EN BOUT DE CHAÎNE — témoin contre traité.

    analyse/.venv/bin/python tools/bilan-coupure-basse.py reconstruction/travail/d282-temoin reconstruction/travail/d282-coupure

RÈGLE QUE CET OUTIL FAIT RESPECTER (docs/ROADMAP-daw.md D282). L'attendu a été
écrit AVANT la course, avec ses quatre critères et leurs seuils ; cet outil les
lit tels quels et rend un verdict par critère — il ne les retouche pas, et il
publie le balayage ENTIER (les dix morceaux), jamais son meilleur point.

  1. la distance de la piste « bass » baisse : en médiane sur les dix, ET sur
     six morceaux au moins ;
  2. CONTRÔLE : la piste « bass » ne perd pas plus de 10 % de ses notes justes
     (total sur les dix morceaux, traité ≥ 0,90 × témoin) ;
  3. la bonne hauteur de la piste « bass » (justes / appariées) monte de trois
     points au moins sur le total ;
  4. la distance globale ne monte pas : médiane des dix, traité ≤ témoin + 0,5 %.

LES NOTES SE LISENT DANS LE PROJET ÉCRIT (`rapport.json`, `stems[].noteConfidence`
des pistes dont le nom commence par « bass »), appariées à la vérité de `s1-sec`
à ±60 ms comme D281 — même appariement, même tolérance, sans quoi les deux
mesures ne se compareraient pas. Un morceau dont l'un des deux côtés manque est
DIT « manquant » et sort du compte : une comparaison dont un côté manque rend
« différent », pas « raté » (piège du 13/09).
"""
from __future__ import annotations

import json
import statistics
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
SRC = RACINE / "reconstruction/travail/s1-sec"
TOLERANCE = 0.06

SEUIL_JUSTES_PERDUES = 0.10      # critère 2
SEUIL_BONNE_HAUTEUR = 3.0        # critère 3, en points
SEUIL_GLOBAL = 0.005             # critère 4, en part de la distance témoin
MORCEAUX_MINIMUM_EN_BAISSE = 6   # critère 1


def notes_de_la_basse(rapport: dict) -> list[tuple[float, int]]:
    notes = []
    for stem in rapport.get("stems", []):
        if str(stem.get("name", "")).startswith("bass"):
            for n in stem.get("noteConfidence", []) or []:
                notes.append((float(n["start"]), int(n["note"])))
    return notes


def distance_de_la_basse(rapport: dict):
    """La distance de la piste nommée exactement « bass » — la seule qui se
    compare d'un lot à l'autre. Une basse DÉCOUPÉE en voix (« bass · voix 1 »)
    n'a pas de distance de piste unique : dite, pas inventée."""
    for stem in rapport.get("stems", []):
        if stem.get("name") == "bass":
            return stem.get("distance")
    return None


def compter(notes: list[tuple[float, int]], vraies: list[tuple[float, int]]) -> dict[str, int]:
    c = {"ecrites": 0, "justes": 0, "bas": 0, "haut": 0, "autre": 0, "inventees": 0}
    for t, h in notes:
        c["ecrites"] += 1
        proches = [hv for tv, hv in vraies if abs(tv - t) <= TOLERANCE]
        if not proches:
            c["inventees"] += 1
        elif h in proches:
            c["justes"] += 1
        elif any(hv - h in (12, 24) for hv in proches):
            c["bas"] += 1
        elif any(hv - h in (-12, -24) for hv in proches):
            c["haut"] += 1
        else:
            c["autre"] += 1
    return c


def lire(lot: Path, nom: str):
    rapport = lot / nom / "course" / "rapport.json"
    if not rapport.is_file() or rapport.stat().st_size == 0:
        return None
    return json.loads(rapport.read_text(encoding="utf-8"))


def bonne_hauteur(c: dict[str, int]) -> float | None:
    appariees = c["justes"] + c["bas"] + c["haut"] + c["autre"]
    return 100.0 * c["justes"] / appariees if appariees else None


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 2
    temoin, traite = Path(argv[1]), Path(argv[2])
    noms = sorted({d.name for d in temoin.glob("morceau-*")} | {d.name for d in traite.glob("morceau-*")})
    manquants: list[str] = []
    lignes = []
    tot_t = {"ecrites": 0, "justes": 0, "bas": 0, "haut": 0, "autre": 0, "inventees": 0}
    tot_c = dict(tot_t)
    for nom in noms:
        rt, rc = lire(temoin, nom), lire(traite, nom)
        verite = SRC / nom / "verite.json"
        if rt is None or rc is None or not verite.is_file():
            manquants.append(f"{nom} ({'témoin ' if rt is None else ''}{'traité ' if rc is None else ''}"
                             f"{'vérité ' if not verite.is_file() else ''}absent)")
            continue
        v = json.loads(verite.read_text(encoding="utf-8"))
        vraies = sorted((float(n[2]), int(n[0])) for p in v.get("parties", [])
                        if p.get("role") != "batterie" for n in p.get("notes", []))
        ct, cc = compter(notes_de_la_basse(rt), vraies), compter(notes_de_la_basse(rc), vraies)
        for k in tot_t:
            tot_t[k] += ct[k]
            tot_c[k] += cc[k]
        coupures = [d for d in rc.get("coupureBasse", []) if d.get("stem") == "bass"]
        coupure = coupures[0] if coupures else None
        lignes.append({
            "nom": nom, "coupure": coupure,
            "d_bass_t": distance_de_la_basse(rt), "d_bass_c": distance_de_la_basse(rc),
            "g_t": rt.get("globalDistance"), "g_c": rc.get("globalDistance"),
            "ct": ct, "cc": cc,
        })

    if manquants:
        print("MANQUANTS (hors du compte) : " + ", ".join(manquants))
    if not lignes:
        print("aucun morceau comparable")
        return 1

    print(f"{len(lignes)} morceaux comparés — témoin {temoin.name}, traité {traite.name}\n")
    print(f"{'morceau':>16} {'coupure':>9} {'d bass T':>9} {'d bass C':>9} {'Δ':>8} "
          f"{'justes T':>8} {'justes C':>8} {'bonne h. T':>10} {'bonne h. C':>10} "
          f"{'global T':>9} {'global C':>9} {'Δ %':>7}")
    baisses_bass = 0
    comparables_bass = 0
    for ligne in lignes:
        cp = ligne["coupure"]
        coupure = "—" if cp is None else ("aucune" if not cp.get("coupureHz") else f"{cp['coupureHz']:.1f} Hz")
        dt, dc = ligne["d_bass_t"], ligne["d_bass_c"]
        if dt is not None and dc is not None:
            comparables_bass += 1
            if dc < dt:
                baisses_bass += 1
            delta = f"{dc - dt:+.4f}"
        else:
            delta = "n/c"
        bt, bc = bonne_hauteur(ligne["ct"]), bonne_hauteur(ligne["cc"])
        gt, gc = ligne["g_t"], ligne["g_c"]
        dg = f"{100 * (gc - gt) / gt:+.2f}" if (gt and gc is not None) else "n/c"
        fmt = lambda x: "—" if x is None else f"{x:.4f}"  # noqa: E731
        fmh = lambda x: "—" if x is None else f"{x:.1f}%"  # noqa: E731
        print(f"{ligne['nom']:>16} {coupure:>9} {fmt(dt):>9} {fmt(dc):>9} {delta:>8} "
              f"{ligne['ct']['justes']:8d} {ligne['cc']['justes']:8d} {fmh(bt):>10} {fmh(bc):>10} "
              f"{fmt(gt):>9} {fmt(gc):>9} {dg:>7}")

    # ---- les quatre critères, aux seuils écrits avant la course ----
    print("\nVERDICT, aux seuils de D282 (écrits avant la course) :")
    paires = [(ligne["d_bass_t"], ligne["d_bass_c"]) for ligne in lignes
              if ligne["d_bass_t"] is not None and ligne["d_bass_c"] is not None]
    verdicts = []
    if paires:
        med_t = statistics.median(t for t, _ in paires)
        med_c = statistics.median(c for _, c in paires)
        ok1 = med_c < med_t and baisses_bass >= MORCEAUX_MINIMUM_EN_BAISSE
        verdicts.append(ok1)
        print(f"  1. distance de la piste bass : médiane {med_t:.4f} → {med_c:.4f}, en baisse sur "
              f"{baisses_bass}/{comparables_bass} morceaux (il en faut {MORCEAUX_MINIMUM_EN_BAISSE}) "
              f"→ {'TENU' if ok1 else 'TOMBE'}")
    else:
        verdicts.append(False)
        print("  1. distance de la piste bass : AUCUNE paire comparable (basse découpée ou absente) → TOMBE")
    jt, jc = tot_t["justes"], tot_c["justes"]
    ok2 = jt > 0 and jc >= (1.0 - SEUIL_JUSTES_PERDUES) * jt
    verdicts.append(ok2)
    perte = (100.0 * (jt - jc) / jt) if jt else float("nan")
    print(f"  2. CONTRÔLE — justes de la piste bass : {jt} → {jc} ({perte:+.1f} % perdues, plafond "
          f"{100 * SEUIL_JUSTES_PERDUES:.0f} %) → {'TENU' if ok2 else 'TOMBE'}")
    bt, bc = bonne_hauteur(tot_t), bonne_hauteur(tot_c)
    ok3 = bt is not None and bc is not None and bc - bt >= SEUIL_BONNE_HAUTEUR
    verdicts.append(ok3)
    if bt is not None and bc is not None:
        print(f"  3. bonne hauteur de la piste bass : {bt:.1f} % → {bc:.1f} % ({bc - bt:+.1f} pt, il en faut "
              f"+{SEUIL_BONNE_HAUTEUR:.0f}) ; 8ve bas {tot_t['bas']} → {tot_c['bas']}, 8ve haut "
              f"{tot_t['haut']} → {tot_c['haut']}, inventées {tot_t['inventees']} → {tot_c['inventees']}, "
              f"écrites {tot_t['ecrites']} → {tot_c['ecrites']} → {'TENU' if ok3 else 'TOMBE'}")
    else:
        print("  3. bonne hauteur : aucune note appariée d'un côté → TOMBE")
    globaux = [(ligne["g_t"], ligne["g_c"]) for ligne in lignes if ligne["g_t"] is not None and ligne["g_c"] is not None]
    if globaux:
        mg_t = statistics.median(t for t, _ in globaux)
        mg_c = statistics.median(c for _, c in globaux)
        ok4 = mg_c <= mg_t * (1.0 + SEUIL_GLOBAL)
        verdicts.append(ok4)
        print(f"  4. distance globale : médiane {mg_t:.4f} → {mg_c:.4f} ({100 * (mg_c - mg_t) / mg_t:+.2f} %, "
              f"plafond +{100 * SEUIL_GLOBAL:.1f} %) → {'TENU' if ok4 else 'TOMBE'}")
    else:
        verdicts.append(False)
        print("  4. distance globale : aucune paire → TOMBE")
    print(f"\n{sum(verdicts)}/4 critères tenus" + (" — l'attendu est TENU" if all(verdicts) else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
