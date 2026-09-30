#!/usr/bin/env python3
"""H51 — UN CHORUS À DEUX CADENCES REND-IL LES NIVEAUX DES RAIES DU PAD ? (docs/CDC-reload-indifferenciable.md § 13)

H50 a établi que le pad de « Reload » est un sinus par note sous une modulation de
retard à deux cadences. Cet outil demande si un modèle fait des MÊMES PIÈCES que
l'effet chorus du rack (`audio/include/vsm/audio/dsp/Chorus.h` : un retard qui va de
la base à la base plus la profondeur, modulé par un sinus, mélangé au son direct),
appliqué deux fois, rend les niveaux des composantes de chaque raie de l'original.

    analyse/.venv/bin/python -u analyse/mesure_h51.py mesurer ORIGINAL.wav \\
        --moteur build-h42/tools/vsm-render --sortie reconstruction/travail/reload-h51
    analyse/.venv/bin/python analyse/mesure_h51.py verdict reconstruction/travail/reload-h51/mesure.json

LE MODÈLE EST EXACT, PAS APPROCHÉ : la source est un sinus, donc `x(t − τ(t))` s'écrit
`sin(2πf·(t − τ(t)))` sans ligne à retard ni interpolation ; deux étages en série se
composent de la même façon. Ce que le modèle ne porte PAS de l'effet du rack : son
passe-bas de 6 kHz sur le son traité (−0,04 dB à 558 Hz ; son retard de 17 µs à
44,1 kHz est ajouté à la base) et l'interpolation linéaire de sa ligne.

RÉGLAGE ET VALIDATION SÉPARÉS : réglé sur cinq raies, jugé sur cinq autres. Trois
topologies, toutes publiées (S série, M un retard à deux LFO, P parallèle), plus deux
témoins (un sinus nu, le meilleur chorus à UNE cadence).

Rend 0 quand la mesure est complète, 2 sur un refus. L'outil publie ; `verdict`
recalcule depuis le fichier.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple

import numpy as np

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))

import mesure_h47 as h47  # noqa: E402
import mesure_h49 as h49  # noqa: E402
import mesure_h50 as h50  # noqa: E402

LA4_HZ = 443.1372
F1_HZ, F2_HZ = 1.750, 2.37
SR_MODELE = 4000                     # les raies vont de 166 à 558 Hz
DUREE_S = 24.0
PLANCHER_DB = -15.0                  # un membre absent : le seuil des composantes de H49
REGLAGE = (52, 58, 61, 66, 71)       # mi3, la♯3, do♯4, fa♯4, si4
VALIDATION = (54, 59, 64, 70, 73)    # fa♯3, si3, mi4, la♯4, do♯5
DEBUT_S, FIN_S = 16.0, 40.0
BORNES = {"profondeur": (0.1, 8.0), "dosage": (0.0, 1.0), "base": (1.0, 20.0)}
RACK = {"cadence": 1.751, "profondeur": 2.4, "dosage": 0.5, "base": 8.0 + 0.0168}
NOTES_DU_RACK = (52, 66, 73)
MACHINES = ("vsm.pipeorgan", "vsm.additive", "vsm.tonewheel", "vsm.generic")

Signal = Callable[[np.ndarray], np.ndarray]


# ------------------------------------------------------------------ le modèle

def sinus(hz: float) -> Signal:
    return lambda t: np.sin(2 * np.pi * hz * t)


def retard_s(t: np.ndarray, cadence: float, profondeur_ms: float, base_ms: float, phase: float = 0.0) -> np.ndarray:
    """Le retard de `Chorus::readTap` : de la base à la base plus la profondeur, en secondes."""
    return (base_ms + profondeur_ms * (0.5 + 0.5 * np.sin(2 * np.pi * cadence * t + phase))) / 1000.0


def etage(source: Signal, cadence: float, profondeur_ms: float, dosage: float, base_ms: float,
          phases: Sequence[float] = (0.0,)) -> Signal:
    """Un étage de chorus : le son direct et la moyenne des lectures retardées (une lecture, ou
    les deux lectures en quadrature de l'effet du rack sommées en mono)."""
    def sortie(t: np.ndarray) -> np.ndarray:
        humide = sum(source(t - retard_s(t, cadence, profondeur_ms, base_ms, p)) for p in phases) / len(phases)
        return (1.0 - dosage) * source(t) + dosage * humide
    return sortie


def topologie(nom: str, hz: float, p: Sequence[float]) -> Signal:
    """Le signal d'UNE note sous la topologie `nom`, réglages `p` (voir `PARAMETRES`)."""
    s = sinus(hz)
    if nom == "N":
        return s
    if nom in ("U1", "U2"):
        return etage(s, F1_HZ if nom == "U1" else F2_HZ, p[0], p[1], p[2])
    if nom == "S":
        return etage(etage(s, F1_HZ, p[0], p[1], p[2]), F2_HZ, p[3], p[4], p[5])
    if nom == "M":
        def un_retard(t: np.ndarray) -> np.ndarray:
            tau = (p[3] + p[0] * (0.5 + 0.5 * np.sin(2 * np.pi * F1_HZ * t))
                   + p[1] * (0.5 + 0.5 * np.sin(2 * np.pi * F2_HZ * t))) / 1000.0
            return (1.0 - p[2]) * s(t) + p[2] * s(t - tau)
        return un_retard
    if nom == "P":
        def paralleles(t: np.ndarray) -> np.ndarray:
            direct = max(0.0, 1.0 - p[1] - p[4])
            return (direct * s(t) + p[1] * s(t - retard_s(t, F1_HZ, p[0], p[2]))
                    + p[4] * s(t - retard_s(t, F2_HZ, p[3], p[5])))
        return paralleles
    raise ValueError(f"topologie inconnue : {nom}")


PARAMETRES: Dict[str, List[str]] = {
    "N": [],
    "U1": ["profondeur", "dosage", "base"],
    "U2": ["profondeur", "dosage", "base"],
    "S": ["profondeur", "dosage", "base", "profondeur", "dosage", "base"],
    "M": ["profondeur", "profondeur", "dosage", "base"],
    "P": ["profondeur", "dosage", "base", "profondeur", "dosage", "base"],
}
NOMS = {"N": "sinus nu", "U1": "une cadence (f1)", "U2": "une cadence (f2)", "S": "deux étages en série",
        "M": "un retard, deux LFO", "P": "deux retards en parallèle"}


# ------------------------------------------------------------------ la lecture d'une raie

def table_de_raie(x: np.ndarray, sr: int, nominal_hz: float) -> Dict[str, float]:
    """Les niveaux (dB, relatifs à la plus forte composante) de la porteuse « 0 » et des membres
    de la famille, signés : « -f1 », « +2f1 »… Les composantes hors famille sont comptées « ? »."""
    comps = h49.composantes(x, sr, nominal_hz)
    if not comps:
        return {}
    lue = h50.lire_raie(comps, nominal_hz)
    table: Dict[str, float] = {"0": float(lue["porteuse_db"])}
    hors = 0
    for c in lue["secondaires"]:
        if not c["membre"]:
            hors += 1
            continue
        cle = ("+" if c["ecart_hz"] > 0 else "-") + c["membre"]
        table[cle] = max(table.get(cle, -99.0), float(c["db"]))
    if hors:
        table["?"] = float(hors)
    return table


def distance(original: Dict[str, float], modele: Dict[str, float]) -> float:
    """Moyenne des écarts absolus de niveau sur la porteuse et les membres présents d'un côté ou
    de l'autre ; un membre absent vaut le plancher. Le compte « ? » n'entre pas."""
    cles = (set(original) | set(modele)) - {"?"}
    if not cles:
        return abs(PLANCHER_DB)
    return float(np.mean([abs(original.get(k, PLANCHER_DB) - modele.get(k, PLANCHER_DB)) for k in cles]))


def table_du_modele(nom: str, note: int, p: Sequence[float], t: np.ndarray) -> Dict[str, float]:
    hz = h47.hz_de(note, LA4_HZ)
    return table_de_raie(topologie(nom, hz, p)(t), SR_MODELE, hz)


def distance_moyenne(nom: str, p: Sequence[float], notes: Sequence[int], originales: Dict[int, Dict[str, float]],
                     t: np.ndarray) -> float:
    return float(np.mean([distance(originales[n], table_du_modele(nom, n, p, t)) for n in notes]))


def temps_du_modele() -> np.ndarray:
    return np.arange(int(DUREE_S * SR_MODELE)) / SR_MODELE


def regler(nom: str, originales: Dict[int, Dict[str, float]], t: np.ndarray, graine: int = 1,
           iterations: int = 60, population: int = 12) -> Tuple[List[float], float, int]:
    """Les réglages de `nom` qui rapprochent le plus les cinq raies de RÉGLAGE : (réglages, distance, évaluations)."""
    from scipy.optimize import differential_evolution

    bornes = [BORNES[x] for x in PARAMETRES[nom]]
    if not bornes:
        return [], distance_moyenne(nom, [], REGLAGE, originales, t), 1
    issue = differential_evolution(lambda p: distance_moyenne(nom, p, REGLAGE, originales, t), bornes,
                                   seed=graine, maxiter=iterations, popsize=population, tol=1e-4,
                                   polish=False, updating="immediate")
    return [float(v) for v in issue.x], float(issue.fun), int(issue.nfev)


# ------------------------------------------------------------------ l'effet du rack, par le vrai moteur

def modele_du_rack(hz: float) -> Signal:
    """L'insert chorus du rack, somme mono : deux lectures en quadrature, base de 8 ms."""
    return etage(sinus(hz), RACK["cadence"], RACK["profondeur"], RACK["dosage"], RACK["base"],
                 phases=(0.0, math.pi / 2))


def rendre_par_le_moteur(moteur: Path, machine: str, avec_effet: bool, dossier: Path) -> Optional[Tuple[np.ndarray, int]]:
    """Trois notes tenues 26 s par `machine`, avec ou sans l'insert chorus ; les 24 dernières secondes, en mono."""
    from analyzer import diapason
    from analyzer.vsm_offline_render import render_track_offline
    from analyzer.vsm_project_export import ExportNote, ExportTrack

    diapason.poser(LA4_HZ)
    sr, duree = 44100, 26.0
    effets: List[Dict[str, object]] = []
    if avec_effet:
        effets.append({"type": "chorus", "parameters": {"effect.chorus.rate": RACK["cadence"],
                                                        "effect.chorus.depth": RACK["profondeur"],
                                                        "effect.chorus.mix": RACK["dosage"]}})
    piste = ExportTrack(name="rack", machine=machine,
                        notes=[ExportNote(note=n, velocity=100, start=0.0, duration=duree) for n in NOTES_DU_RACK],
                        effects=effets)
    dossier.mkdir(parents=True, exist_ok=True)
    rendu = render_track_offline(piste, dossier, sr, duration=duree, tempo=138.0, binary=str(moteur), title="h51-rack")
    (dossier / "rendu.wav").unlink(missing_ok=True)
    if rendu is None or rendu.size < int(duree * sr) - sr:
        return None
    return np.asarray(rendu[2 * sr:int(duree * sr)], dtype=np.float64), sr


def mesurer_le_rack(moteur: Path, sortie: Path) -> Dict[str, Any]:
    """Attendu 1 : l'effet du rack rendu par le moteur, contre son modèle."""
    t = temps_du_modele()
    essais: List[Dict[str, Any]] = []
    for machine in MACHINES:
        nu = rendre_par_le_moteur(moteur, machine, False, sortie / "travail" / "nu")
        if nu is None:
            essais.append({"machine": machine, "dit": "rendu sans effet en échec"})
            continue
        tables_nues = {n: table_de_raie(nu[0], nu[1], h47.hz_de(n, LA4_HZ)) for n in NOTES_DU_RACK}
        propre = all(set(tb) == {"0"} for tb in tables_nues.values())
        essais.append({"machine": machine, "sans_effet": {str(n): tb for n, tb in tables_nues.items()}, "propre": propre})
        if not propre:
            continue
        avec = rendre_par_le_moteur(moteur, machine, True, sortie / "travail" / "avec")
        if avec is None:
            essais[-1]["dit"] = "rendu avec effet en échec"
            continue
        raies = []
        for n in NOTES_DU_RACK:
            hz = h47.hz_de(n, LA4_HZ)
            moteur_table = table_de_raie(avec[0], avec[1], hz)
            modele_table = table_de_raie(modele_du_rack(hz)(t), SR_MODELE, hz)
            cles = sorted((set(moteur_table) | set(modele_table)) - {"?"})
            ecarts = {k: round(moteur_table.get(k, PLANCHER_DB) - modele_table.get(k, PLANCHER_DB), 2) for k in cles}
            raies.append({"note": n, "hz": round(hz, 2), "moteur": moteur_table, "modele": modele_table,
                          "ecarts_db": ecarts, "pire_db": max(abs(v) for v in ecarts.values()) if ecarts else None})
        return {"machine": machine, "essais": essais, "raies": raies, "reglages": RACK}
    return {"machine": None, "essais": essais, "raies": [], "reglages": RACK}


# ------------------------------------------------------------------ le verdict

def verdict(mesure: Dict[str, Any]) -> List[Tuple[int, str, str]]:
    sortie: List[Tuple[int, str, str]] = []
    rack = mesure["rack"]
    if not rack["raies"]:
        sortie.append((1, "NON MESURÉ", "aucune machine de la liste n'a une raie propre sans effet, ou le rendu a échoué : "
                          + " ; ".join(f"{e['machine']} {'propre' if e.get('propre') else e.get('dit', 'pas propre')}" for e in rack["essais"])))
    else:
        pire = max(r["pire_db"] for r in rack["raies"])
        v = "TENU" if pire <= 1.0 else ("ÉCHEC" if pire > 3.0 else "ENTRE LES DEUX")
        sortie.append((1, v, f"{rack['machine']} à travers le chorus du rack : pire écart au modèle {pire:.2f} dB "
                             f"(par raie : {', '.join(format(r['pire_db'], '.2f') for r in rack['raies'])})"))

    topos = mesure["topologies"]
    doubles = {k: v for k, v in topos.items() if k in ("S", "M", "P")}
    meilleure = min(doubles, key=lambda k: doubles[k]["reglage"])
    m = doubles[meilleure]
    v = "TENU" if m["reglage"] <= 2.0 else ("ÉCHEC" if m["reglage"] > 4.0 else "ENTRE LES DEUX")
    sortie.append((2, v, f"meilleure topologie au réglage : {meilleure} ({NOMS[meilleure]}), {m['reglage']:.2f} dB sur les cinq raies de réglage"))
    v = "TENU" if m["validation"] <= 3.0 else ("ÉCHEC" if m["validation"] > 6.0 else "ENTRE LES DEUX")
    sortie.append((3, v, f"{meilleure} sur les cinq raies jamais vues : {m['validation']:.2f} dB "
                         f"(par raie : {', '.join(format(d, '.2f') for d in m['validation_par_raie'])})"))

    nu = topos["N"]["validation"]
    une = min(topos["U1"]["validation"], topos["U2"]["validation"])
    marge = min(nu, une) - m["validation"]
    sortie.append((4, "TENU" if marge > 2.0 else "ÉCHEC",
                   f"en validation : sinus nu {nu:.2f} dB, meilleur chorus à une cadence {une:.2f} dB, {meilleure} {m['validation']:.2f} dB "
                   f"— marge {marge:+.2f} dB"))

    sm = min(("S", "M"), key=lambda k: topos[k]["validation"])
    ecart = topos["P"]["validation"] - topos[sm]["validation"]
    sortie.append((5, "TENU" if ecart >= 1.0 else "NON TRANCHÉ",
                   f"en validation : P {topos['P']['validation']:.2f} dB, {sm} {topos[sm]['validation']:.2f} dB — écart {ecart:+.2f} dB"))

    manques = []
    noms = PARAMETRES[meilleure]
    for nom, valeur in zip(noms, m["reglages"], strict=True):
        if nom == "base" and abs(valeur - 8.0) > 0.5:
            manques.append(f"base {valeur:.2f} ms (le rack : 8 ms, fixe)")
        if nom == "profondeur" and not (0.5 <= valeur <= 8.0):
            manques.append(f"profondeur {valeur:.2f} ms (le rack : 0,5 à 8)")
    manques.append("deux cadences composées (le rack : une seule)" if meilleure in ("S", "M") else "deux lectures indépendantes")
    manques.append("une lecture unique en mono (le rack : deux lectures en quadrature, dont la somme mono éteint 2·f1)")
    sortie.append((6, "RELEVÉ", " ; ".join(manques)))
    return sortie


def imprimer(mesure: Dict[str, Any]) -> None:
    rack = mesure["rack"]
    print(f"LE RACK par le moteur (machine : {rack['machine']}) :")
    for e in rack["essais"]:
        print(f"  essai {e['machine']} : {'raie propre sans effet' if e.get('propre') else e.get('dit', 'raie PAS propre sans effet')}")
    for r in rack["raies"]:
        print(f"  note {r['note']} ({r['hz']} Hz) : moteur {r['moteur']} ; modèle {r['modele']} ; écarts {r['ecarts_db']}")
    print("TOPOLOGIES (distance moyenne en dB ; réglage sur 5 raies, validation sur 5 autres) :")
    for nom, tp in mesure["topologies"].items():
        reglages = ", ".join(f"{n} {v:.3f}" for n, v in zip(PARAMETRES[nom], tp["reglages"], strict=True)) or "—"
        print(f"  {nom:2s} {NOMS[nom]:26s} réglage {tp['reglage']:5.2f}  validation {tp['validation']:5.2f}  "
              f"({tp['evaluations']} évaluations) — {reglages}")
    meilleure = min((k for k in mesure["topologies"] if k in ("S", "M", "P")), key=lambda k: mesure["topologies"][k]["reglage"])
    print(f"RAIE PAR RAIE, topologie {meilleure} :")
    for r in mesure["topologies"][meilleure]["raies"]:
        print(f"  note {r['note']} ({'réglage' if r['note'] in REGLAGE else 'VALIDATION'}) distance {r['distance']:.2f} dB — "
              f"original {r['original']} ; modèle {r['modele']}")
    print("VERDICT (recalculé depuis la mesure) :")
    for numero, v, detail in verdict(mesure):
        print(f"  attendu {numero} : {v} — {detail}")


# ------------------------------------------------------------------ la course

def mesurer(a: argparse.Namespace) -> int:
    for p in (a.original, a.moteur):
        if not p.is_file() or p.stat().st_size == 0:
            print(f"REFUS : {p} absent ou vide")
            return 2
    try:
        original, sr = h49.lire_extrait(a.original, DEBUT_S, FIN_S)
    except ValueError as e:
        print(f"REFUS : {e}")
        return 2
    depart = time.time()
    originales = {n: table_de_raie(original, sr, h47.hz_de(n, LA4_HZ)) for n in REGLAGE + VALIDATION}
    vides = [n for n, tb in originales.items() if not tb]
    if vides:
        print(f"REFUS : aucune composante dans l'original pour les notes {vides}")
        return 2
    print("ORIGINAL, raie par raie :")
    for n in sorted(originales):
        print(f"  note {n} : {originales[n]}")
    a.sortie.mkdir(parents=True, exist_ok=True)
    rack = mesurer_le_rack(a.moteur, a.sortie)
    t = temps_du_modele()
    topologies: Dict[str, Any] = {}
    for nom in ("N", "U1", "U2", "S", "M", "P"):
        reglages, d_reglage, evaluations = regler(nom, originales, t, iterations=a.iterations)
        raies = []
        for n in sorted(REGLAGE + VALIDATION):
            table = table_du_modele(nom, n, reglages, t)
            raies.append({"note": n, "original": originales[n], "modele": table,
                          "distance": round(distance(originales[n], table), 3)})
        par_note = {r["note"]: r["distance"] for r in raies}
        topologies[nom] = {"reglages": reglages, "reglage": round(d_reglage, 3),
                           "validation": round(float(np.mean([par_note[n] for n in VALIDATION])), 3),
                           "validation_par_raie": [par_note[n] for n in VALIDATION],
                           "evaluations": evaluations, "raies": raies}
        print(f"  {nom} réglée : {d_reglage:.2f} dB au réglage, {topologies[nom]['validation']:.2f} en validation ({evaluations} évaluations)")
    mesure = {"provenance": {"original": str(a.original), "debut_s": DEBUT_S, "fin_s": FIN_S, "moteur": str(a.moteur),
                             "la4_hz": LA4_HZ, "f1_hz": F1_HZ, "f2_hz": F2_HZ, "sr_modele": SR_MODELE, "graine": 1,
                             "iterations": a.iterations, "bornes": BORNES, "reglage": list(REGLAGE),
                             "validation": list(VALIDATION), "duree_s": round(time.time() - depart, 1)},
              "originales": {str(n): tb for n, tb in originales.items()},
              "rack": rack, "topologies": topologies}
    (a.sortie / "mesure.json").write_text(json.dumps(mesure, indent=1, ensure_ascii=False, sort_keys=True) + "\n",
                                          encoding="utf-8")
    print(f"ÉCRIT : {a.sortie / 'mesure.json'} ({mesure['provenance']['duree_s']} s)")
    imprimer(mesure)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sous = ap.add_subparsers(dest="mode", required=True)
    m = sous.add_parser("mesurer")
    m.add_argument("original", type=Path)
    m.add_argument("--moteur", type=Path, required=True)
    m.add_argument("--sortie", type=Path, required=True)
    m.add_argument("--iterations", type=int, default=60)
    v = sous.add_parser("verdict")
    v.add_argument("mesure", type=Path)
    a = ap.parse_args()
    if a.mode == "verdict":
        if not a.mesure.is_file() or a.mesure.stat().st_size == 0:
            print(f"REFUS : {a.mesure} absent ou vide")
            return 2
        imprimer(json.loads(a.mesure.read_text(encoding="utf-8")))
        return 0
    return mesurer(a)


if __name__ == "__main__":
    sys.exit(main())
