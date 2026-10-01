#!/usr/bin/env python3
"""H55 — LE FOND DES RAIES EST-IL DU BRUIT OU UNE STRUCTURE ? (docs/CDC-reload-indifferenciable.md § 17)

Si deux LFO libres commandent la modulation du pad, l'enveloppe complexe `E(t)` d'une
raie ne dépend à chaque instant que des deux phases `(φ1, φ2)` : c'est une fonction
du TORE, donc une somme de composantes aux fréquences `k·f1 + l·f2` — la famille de
H50. Un bruit n'en est pas une.

  - LA FAMILLE : les composantes `k·f1 + l·f2` (|k| ≤ 8, |l| ≤ 6) qui tombent dans la
    bande de la raie, ajustées à `E(t)` par moindres carrés.
  - LA PART DÉTERMINISTE D, VALIDÉE PAR MOITIÉS : la famille ajustée sur une moitié de
    l'extrait PRÉDIT l'autre ; D = 1 − Σ|E − prédiction|² / Σ|E − Ē|², les deux sens
    sommés. Un ajustement non validé ne vaut rien ici : une centaine de sinusoïdes
    « expliquent » jusqu'à 30 % d'une bande de bruit de 16 Hz sur 22 s ; un bruit, lui,
    ne se prédit pas.
  - LES CADENCES, communes aux raies vues d'un extrait, au maximum de la part
    expliquée par la famille (moindres carrés exacts, extrait entier), sur une grille de
    1 mHz puis de 0,25 mHz — une recherche de deux nombres : les témoins la subissent aussi.
  - LA FORME REPLIÉE : la famille ajustée sur l'extrait entier, évaluée sur une grille
    de 24 × 24 couples de phases, jugée comme au § 15.1 (circularité, aplatissement),
    avec l'ARC COUVERT — 360° moins le plus grand vide angulaire autour du centre —,
    puisque des points du tore n'ont pas d'ordre temporel.

POURQUOI PAS DES CASES (écrit avec les tests, AVANT la mesure, 01/10/2026). Le
premier instrument rangeait les instants dans 8 × 8 cases de phases : sous une
modulation profonde, la phase de la raie tourne de plus de deux radians à
l'intérieur d'une seule case, et une lecture PARFAITE n'y donnait que D = 0,78 sur
do♯5 (0,51 pour la série). La famille n'a pas de cases.

    analyse/.venv/bin/python analyse/mesure_h55.py ORIGINAL.wav \\
        --oracle-h47 reconstruction/travail/reload-h47/mesure.json \\
        --mesure-h50 reconstruction/travail/reload-h50/mesure.json --sortie reconstruction/travail/reload-h55

Rend 0 quand la mesure est complète, 2 sur un refus.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Sequence, Tuple

import numpy as np

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))

import mesure_h47 as h47  # noqa: E402
import mesure_h49 as h49  # noqa: E402
import mesure_h50 as h50  # noqa: E402
import mesure_h53 as h53  # noqa: E402
import mesure_h54 as h54  # noqa: E402

ORDRE_F1, ORDRE_F2 = 8, 6
GRILLE_PHASES = 24
DECIMATION = 4                         # 200 Hz → 50 Hz ; Nyquist 25 Hz au-dessus des 15 Hz de la bande
GRILLE_F1 = np.arange(1.744, 1.756 + 1e-9, 0.0005)
GRILLE_F2 = np.arange(2.355, 2.385 + 1e-9, 0.0005)
H50_F1, H50_F2 = 1.7498, 2.37
TOLERANCE_CADENCE_HZ = 0.003
D_STRUCTURE, D_BRUIT = 0.7, 0.3


def trajectoire(x: np.ndarray, sr: int, note: int, demi_bande: float) -> Tuple[np.ndarray, float, Dict[str, Any]]:
    """`E(t)` d'une raie, construite comme au § 15 : (E, taux, fiche de la raie)."""
    nominal = h47.hz_de(note, h53.LA4_HZ)
    f, P = h49.spectre_entier(x, sr)
    forme = h49.forme_de_raie(f, P, nominal)
    fiche: Dict[str, Any] = {"note": note, "nominal_hz": round(nominal, 3), "demi_bande_hz": demi_bande,
                             "rapport_au_fond_db": forme["rapport_au_fond_db"],
                             "vue": bool(forme["rapport_au_fond_db"] >= h49.VUE_DB)}
    comps = h49.composantes(x, sr, nominal)
    if not comps:
        fiche["vue"] = False
        return np.zeros(0, dtype=complex), 0.0, fiche
    porteuse = h50.lire_raie(comps, nominal)["porteuse_hz"]
    z, taux = h53.bande_de_base(x, sr, porteuse, demi_bande)
    z, delta = h53.affiner_la_porteuse(z, taux)
    fiche["porteuse_hz"] = round(porteuse + delta, 4)
    # DÉCIMÉE à 50 Hz : la bande ne dépasse pas ± 15 Hz, et chaque ajustement de la famille
    # coûte quatre fois moins (la recherche des cadences en fait des milliers).
    return z[::DECIMATION], taux / DECIMATION, fiche


def colonnes(f1: float, f2: float, demi_bande: float) -> np.ndarray:
    """Les fréquences k·f1 + l·f2 de la famille qui tombent dans la bande : tableau (k, l, hz)."""
    sortie = [(k, m, k * f1 + m * f2) for k in range(-ORDRE_F1, ORDRE_F1 + 1) for m in range(-ORDRE_F2, ORDRE_F2 + 1)
              if abs(k * f1 + m * f2) <= demi_bande]
    return np.array(sortie, dtype=float)


def _matrice(t: np.ndarray, freqs: np.ndarray) -> np.ndarray:
    return np.exp(2j * np.pi * np.outer(t, freqs))


def ajuster(z: np.ndarray, t: np.ndarray, freqs: np.ndarray) -> np.ndarray:
    return np.linalg.lstsq(_matrice(t, freqs), z, rcond=None)[0]


def expliquee(z: np.ndarray, taux: float, f1: float, f2: float, demi_bande: float) -> Tuple[float, float]:
    """(somme des carrés résiduelle, somme des carrés totale) de la famille ajustée sur l'extrait entier."""
    t = np.arange(len(z)) / taux
    freqs = colonnes(f1, f2, demi_bande)[:, 2]
    c = ajuster(z, t, freqs)
    return float(np.sum(np.abs(z - _matrice(t, freqs) @ c) ** 2)), float(np.sum(np.abs(z - z.mean()) ** 2))


def part_deterministe(z: np.ndarray, taux: float, f1: float, f2: float, demi_bande: float) -> float:
    """D validée par moitiés : la famille ajustée sur une moitié prédit l'autre, dans les deux sens."""
    t = np.arange(len(z)) / taux
    freqs = colonnes(f1, f2, demi_bande)[:, 2]
    m = len(z) // 2
    residu = total = 0.0
    for app, jug in ((slice(0, m), slice(m, None)), (slice(m, None), slice(0, m))):
        c = ajuster(z[app], t[app], freqs)
        residu += float(np.sum(np.abs(z[jug] - _matrice(t[jug], freqs) @ c) ** 2))
        total += float(np.sum(np.abs(z[jug] - z[jug].mean()) ** 2))
    return 1.0 - residu / total if total > 0 else 0.0


def _part_poolee(trajectoires: Sequence[Tuple[np.ndarray, float, float]], f1: float, f2: float) -> float:
    residu = total = 0.0
    for z, taux, demi_bande in trajectoires:
        r, tt = expliquee(z, taux, f1, f2, demi_bande)
        residu += r
        total += tt
    return 1.0 - residu / total if total > 0 else 0.0


def chercher_les_cadences(trajectoires: Sequence[Tuple[np.ndarray, float, float]]) -> Tuple[float, float, float]:
    """Les cadences communes qui maximisent la part EXPLIQUÉE poolée (moindres carrés exacts,
    extrait entier) : une grille de 1 mHz, puis de 0,25 mHz à ± 1 mHz du meilleur point.

    Une PROJECTION (le périodogramme aux fréquences de la famille) allait mille fois plus
    vite et se trompait : sur une lecture parfaite à 1,750 / 2,370 Hz, elle choisissait
    1,752 / 2,3575 — des membres voisins de la famille (7,00 et 7,11 Hz, non orthogonaux
    sur 22 s) y comptent deux fois le même pic. Trouvé par le premier test, avant la mesure."""
    grossiere = [(float(a), float(b)) for a in GRILLE_F1[::2] for b in GRILLE_F2[::2]]
    meilleur = max(((f1, f2, _part_poolee(trajectoires, f1, f2)) for f1, f2 in grossiere), key=lambda c: c[2])
    fines = [(meilleur[0] + 0.00025 * i, meilleur[1] + 0.00025 * j) for i in range(-4, 5) for j in range(-4, 5)]
    return max(((f1, f2, _part_poolee(trajectoires, f1, f2)) for f1, f2 in fines), key=lambda c: c[2])


def forme_repliee(z: np.ndarray, taux: float, f1: float, f2: float, demi_bande: float) -> np.ndarray:
    """La famille ajustée sur l'extrait entier, évaluée sur une grille de couples de phases."""
    t = np.arange(len(z)) / taux
    kl = colonnes(f1, f2, demi_bande)
    c = ajuster(z, t, kl[:, 2])
    phi = 2 * np.pi * np.arange(GRILLE_PHASES) / GRILLE_PHASES
    p1, p2 = np.meshgrid(phi, phi)
    return (np.exp(1j * (np.outer(p1.ravel(), kl[:, 0]) + np.outer(p2.ravel(), kl[:, 1]))) @ c)


def arc_couvert(points: np.ndarray, centre: complex) -> float:
    """360° moins le plus grand vide angulaire autour du centre."""
    if len(points) < 2:
        return 0.0
    a = np.sort(np.angle(points - centre))
    vides = np.diff(np.concatenate([a, [a[0] + 2 * np.pi]]))
    return float(np.degrees(2 * np.pi - vides.max()))


def juger_la_forme(pts: np.ndarray) -> Dict[str, Any]:
    centre, rayon = h53.ajuster_un_cercle(pts)
    if rayon <= 0.0 or not np.isfinite(rayon):
        return {"circularite": None, "arc_couvert_deg": 0.0, "aplatissement": None, "cercle": False}
    circ = float(np.std(np.abs(pts - centre)) / rayon)
    arc = arc_couvert(pts, centre)
    apl = h53.aplatissement(pts)
    return {"circularite": round(circ, 4), "arc_couvert_deg": round(arc, 1), "aplatissement": round(apl, 4),
            "dosage": round(rayon / (rayon + abs(centre)), 4),
            "cercle": bool(circ <= h53.CERCLE_MAX and arc >= h53.ARC_MIN_DEG and apl >= h53.APLATI_MIN)}


def analyser(x: np.ndarray, sr: int, notes: Sequence[int]) -> Dict[str, Any]:
    bandes = h53.demi_bandes(notes)
    lues = [(trajectoire(x, sr, n, bandes[n]), bandes[n]) for n in notes]
    vues = [(z, taux, b) for (z, taux, fiche), b in lues if fiche["vue"] and len(z)]
    if not vues:
        return {"f1_hz": None, "f2_hz": None, "part_expliquee_poolee": None,
                "raies": [fiche for (_, _, fiche), _ in lues]}
    f1, f2, part = chercher_les_cadences(vues)
    raies = []
    for (z, taux, fiche), b in lues:
        if fiche["vue"] and len(z):
            fiche["d"] = round(part_deterministe(z, taux, f1, f2, b), 4)
            fiche["composantes"] = int(len(colonnes(f1, f2, b)))
            fiche.update(juger_la_forme(forme_repliee(z, taux, f1, f2, b)))
        raies.append(fiche)
    return {"f1_hz": round(f1, 4), "f2_hz": round(f2, 4), "part_expliquee_poolee": round(part, 4), "raies": raies}


def verdict(extrait: Dict[str, Any]) -> List[Tuple[int, str, str]]:
    vues = [r for r in extrait["raies"] if r["vue"] and "d" in r]
    ds = [r["d"] for r in vues]
    nommees = ", ".join(f"{r['note']}:{r['d']:.2f}" for r in vues)
    structure = sum(d >= D_STRUCTURE for d in ds)
    bruit = sum(d <= D_BRUIT for d in ds)
    v2 = "TENU" if structure >= 8 else ("ÉCHEC" if bruit >= 5 else "ENTRE LES DEUX")
    sortie = [(2, v2, f"D ≥ {D_STRUCTURE} sur {structure} raie(s), ≤ {D_BRUIT} sur {bruit}, {len(vues)} vue(s) ({nommees})")]
    if v2 == "TENU":
        cercles = sum(bool(r["cercle"]) for r in vues)
        hors = sum(1 for r in vues if r["circularite"] is None or r["circularite"] > h53.PAS_CERCLE
                   or (r["aplatissement"] or 0.0) < h53.APLATI_MIN)
        v3 = "TENU" if cercles >= 8 else ("ÉCHEC" if hors >= 5 else "ENTRE LES DEUX")
        formes = ", ".join(f"{r['note']}:{'—' if r['circularite'] is None else format(100 * r['circularite'], '.0f')} %"
                           for r in vues)
        sortie.append((3, v3, f"{cercles} forme(s) repliée(s) en cercle, {hors} franchement hors ({formes})"))
    else:
        sortie.append((3, "SANS OBJET", "l'attendu 2 ne tient pas"))
    ok = (extrait["f1_hz"] is not None and abs(extrait["f1_hz"] - H50_F1) <= TOLERANCE_CADENCE_HZ
          and abs(extrait["f2_hz"] - H50_F2) <= TOLERANCE_CADENCE_HZ)
    sortie.append((4, "TENU" if ok else "ÉCHEC", f"cadences trouvées {extrait['f1_hz']} et {extrait['f2_hz']} Hz "
                                                 f"(H50 : {H50_F1} et {H50_F2}, ± {TOLERANCE_CADENCE_HZ * 1000:.0f} mHz)"))
    return sortie


def imprimer(mesure: Dict[str, Any]) -> None:
    for e in mesure["extraits"]:
        print(f"EXTRAIT {e['debut_s']:.0f}-{e['fin_s']:.0f} s — cadences {e['f1_hz']} et {e['f2_hz']} Hz, part expliquée poolée {e['part_expliquee_poolee']}")
        for r in e["raies"]:
            if not r["vue"] or "d" not in r:
                print(f"  note {r['note']} {r['nominal_hz']:7.2f} Hz  NON VUE (fond {r['rapport_au_fond_db']:.1f} dB)")
                continue
            circ = "—" if r["circularite"] is None else f"{100 * r['circularite']:.1f} %"
            print(f"  note {r['note']} {r['nominal_hz']:7.2f} Hz  fond {r['rapport_au_fond_db']:5.1f} dB  D {r['d']:.3f}  "
                  f"forme repliée : circularité {circ}, arc couvert {r['arc_couvert_deg']:.0f}°, "
                  f"aplatissement {r['aplatissement'] or 0:.3f}, dosage {r.get('dosage')}  "
                  f"{'CERCLE' if r['cercle'] else 'pas un cercle'}")
        for numero, v, detail in verdict(e):
            print(f"  attendu {numero} : {v} — {detail}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("original", type=Path)
    ap.add_argument("--oracle-h47", type=Path, required=True)
    ap.add_argument("--mesure-h50", type=Path, required=True)
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args()
    for p in (a.original, a.oracle_h47, a.mesure_h50):
        if not p.is_file() or p.stat().st_size == 0:
            print(f"REFUS : {p} absent ou vide")
            return 2
    mesure: Dict[str, Any] = {"provenance": {"original": str(a.original), "ordres": [ORDRE_F1, ORDRE_F2],
                                             "grille_phases": GRILLE_PHASES,
                                             "grille_f1": [float(GRILLE_F1[0]), float(GRILLE_F1[-1]), 0.0005],
                                             "grille_f2": [float(GRILLE_F2[0]), float(GRILLE_F2[-1]), 0.0005],
                                             "d_structure": D_STRUCTURE, "d_bruit": D_BRUIT},
                              "extraits": []}
    for debut, fin, notes in h54.extraits(a.oracle_h47, a.mesure_h50):
        try:
            x, sr = h49.lire_extrait(a.original, debut, fin)
        except ValueError as e:
            print(f"REFUS : {e}")
            return 2
        fiche = analyser(x, sr, notes)
        fiche.update({"debut_s": debut, "fin_s": fin, "notes": notes})
        fiche["verdict"] = [list(v) for v in verdict(fiche)]
        mesure["extraits"].append(fiche)
    a.sortie.mkdir(parents=True, exist_ok=True)
    (a.sortie / "mesure.json").write_text(json.dumps(mesure, indent=1, ensure_ascii=False, sort_keys=True) + "\n",
                                          encoding="utf-8")
    print(f"ÉCRIT : {a.sortie / 'mesure.json'}")
    imprimer(mesure)
    return 0


if __name__ == "__main__":
    sys.exit(main())
