#!/usr/bin/env python3
"""H53 — L'ENVELOPPE COMPLEXE DE CHAQUE RAIE DÉCRIT-ELLE UN CERCLE ? (docs/CDC-reload-indifferenciable.md § 15)

« Un son direct plus UNE lecture retardée » transforme une note de pulsation ω en
A·e^{jωt}·E(t), avec E(t) = (1 − m) + m·e^{−jω·τ(t)} : E(t) parcourt un CERCLE, de
centre 1 − m et de rayon m, quelle que soit la forme du retard τ(t), et l'angle
autour du centre EST le retard. Deux lectures ne donnent pas un cercle. Cet outil
LIT la structure sur la forme de la trajectoire, sans optimiseur.

    analyse/.venv/bin/python analyse/mesure_h53.py mesurer ORIGINAL.wav \\
        --oracle reconstruction/travail/reload-h47/mesure.json --sortie reconstruction/travail/reload-h53
    analyse/.venv/bin/python analyse/mesure_h53.py verdict reconstruction/travail/reload-h53/mesure.json

TROIS PRÉCAUTIONS SANS LESQUELLES RIEN NE SE LIT (les deux premières écrites avec le
code, la troisième apprise de ses tests, avant toute mesure) :

  - LA FRÉQUENCE DE LA PORTEUSE AU MILLIHERTZ. Ramener la bande en bande de base
    par une fréquence fausse de 0,05 Hz fait tourner TOUTE la figure d'un tour en
    vingt secondes : un cercle devient un anneau. La porteuse est donc affinée sur
    la phase de sa propre raie (la bande de base filtrée à ± 0,3 Hz, où elle est
    seule : la composante voisine la plus proche est à 0,62 Hz), par régression.
  - UN SEGMENT N'EST PAS UN CERCLE. Un trémolo fait aller E(t) le long d'une droite,
    et une droite s'ajuste par un cercle immense dont elle est un arc infime : la
    circularité y est excellente. Une trajectoire ne compte comme cercle que si elle
    parcourt au moins 60° autour du centre ajusté ; l'arc est publié.
  - ET UNE CORDE N'EST PAS UN ARC. Le test d'un trémolo à la cadence `f1` (01/10) a
    montré l'autre visage de la droite : parcourue en sinus, elle passe ses instants
    aux DEUX BOUTS, qui tombent sur le cercle dont elle est la corde — circularité
    9,9 %, arc 83° sur la♯4, « CERCLE ». L'ajustement ne peut pas trancher, puisque
    c'est lui qui est trompé. La trajectoire se juge donc AUSSI sans lui : son
    APLATISSEMENT, petit axe sur grand axe de son nuage (racine du rapport des valeurs
    propres de sa covariance). Un arc de demi-angle α parcouru en sinus en a ≈ α/4 —
    0,13 pour les 60° déjà exigés ; un segment bruité à −30 dB, ≈ 0,01. Le seuil,
    0,10, vient de ce calcul et non des tests.

Une raie à moins de 10 dB du fond (H49) n'est pas jugée, et elle est nommée.
Rend 0 quand la mesure est complète, 2 sur un refus. L'outil publie ; `verdict`
recalcule depuis le fichier.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

import numpy as np

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))

import mesure_h47 as h47  # noqa: E402
import mesure_h49 as h49  # noqa: E402
import mesure_h50 as h50  # noqa: E402

LA4_HZ = 443.1372
F1_HZ, F2_HZ = 1.750, 2.37
DEBUT_S, FIN_S = 16.0, 40.0
DEMI_BANDE_MAX_HZ = 15.0
PORTEUSE_SEULE_HZ = 0.3              # la bande où la porteuse est seule
TAUX_BASE_HZ = 200.0                 # la bande de base, décimée
ARC_MIN_DEG = 60.0
CERCLE_MAX = 0.10                    # circularité en deçà de laquelle c'est un cercle
APLATI_MIN = 0.10                    # aplatissement d'un arc de 60° parcouru en sinus : ≈ 0,13
PAS_CERCLE = 0.25


def demi_bandes(notes: Sequence[int], la4: float = LA4_HZ) -> Dict[int, float]:
    """La moitié de l'écart à la raie voisine la plus proche, bornée à 15 Hz."""
    hz = {n: h47.hz_de(n, la4) for n in notes}
    sortie = {}
    for n in notes:
        voisines = [abs(hz[n] - hz[m]) for m in notes if m != n]
        sortie[n] = round(min(DEMI_BANDE_MAX_HZ, 0.5 * min(voisines)) if voisines else DEMI_BANDE_MAX_HZ, 2)
    return sortie


def bande_de_base(x: np.ndarray, sr: int, hz: float, demi_bande: float) -> Tuple[np.ndarray, float]:
    """La bande analytique `hz` ± `demi_bande`, ramenée en bande de base par `hz` et décimée."""
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    X = np.fft.fft(x)
    f = np.fft.fftfreq(n, 1.0 / sr)
    ecart = np.abs(f - hz)
    bord = 0.1 * demi_bande
    masque = np.where(ecart <= demi_bande - bord, 1.0, 0.0)
    rampe = (ecart > demi_bande - bord) & (ecart <= demi_bande)
    masque[rampe] = 0.5 * (1 + np.cos(np.pi * (ecart[rampe] - (demi_bande - bord)) / bord))
    z = np.fft.ifft(X * masque) * np.exp(-2j * np.pi * hz * np.arange(n) / sr)
    pas = max(1, int(round(sr / TAUX_BASE_HZ)))
    return z[sr:n - sr:pas], sr / pas            # une seconde de bord retirée de chaque côté


def affiner_la_porteuse(z: np.ndarray, taux: float) -> Tuple[np.ndarray, float]:
    """Retire de `z` la rotation lente que laisse une porteuse mal connue : (z corrigé, écart en Hz).
    La porteuse est lue seule, dans ± 0,3 Hz, et sa phase déroulée donne l'écart de fréquence."""
    n = len(z)
    Z = np.fft.fft(z)
    f = np.fft.fftfreq(n, 1.0 / taux)
    seule = np.fft.ifft(np.where(np.abs(f) <= PORTEUSE_SEULE_HZ, Z, 0.0))
    t = np.arange(n) / taux
    pente = np.polyfit(t, np.unwrap(np.angle(seule)), 1, w=np.abs(seule))[0]
    delta = float(pente / (2 * np.pi))
    return z * np.exp(-2j * np.pi * delta * t), delta


def ajuster_un_cercle(points: np.ndarray) -> Tuple[complex, float]:
    """Le cercle des moindres carrés (algébrique) : (centre, rayon)."""
    x, y = points.real, points.imag
    A = np.column_stack([x, y, np.ones(len(x))])
    D, E, F = np.linalg.lstsq(A, -(x ** 2 + y ** 2), rcond=None)[0]
    centre = complex(-D / 2.0, -E / 2.0)
    return centre, float(math.sqrt(max(abs(centre) ** 2 - F, 0.0)))


def amplitude_a(signal: np.ndarray, taux: float, hz: float) -> float:
    """L'amplitude de la composante de `signal` à `hz` (± 0,06 Hz), fenêtre de Hann.

    La transformée est ÉVALUÉE sur une grille de 1 mHz, pas lue aux cases de la FFT :
    sur 22 s, 1,75 Hz tombe à mi-case (38,5 cases), où la fenêtre de Hann perd 1,42 dB
    — un sinus pur de 0,85 ms y était relu 0,7216 ms, et sur une case 0,8500. Trouvé
    par le premier test de H53, avant toute mesure."""
    w = np.hanning(len(signal))
    x = (signal - signal.mean()) * w
    t = np.arange(len(signal)) / taux
    grille = hz + np.arange(-0.06, 0.06 + 5e-4, 1e-3)
    S = np.abs(np.exp(-2j * np.pi * np.outer(grille, t)) @ x) * 2.0 / w.sum()
    return float(S.max())


def aplatissement(points: np.ndarray) -> float:
    """Petit axe sur grand axe du nuage de points, sans ajustement : 0 pour un segment, 1 pour un cercle entier."""
    valeurs = np.linalg.eigvalsh(np.cov(np.vstack([points.real, points.imag])))
    return float(math.sqrt(max(valeurs[0], 0.0) / valeurs[1])) if valeurs[1] > 0 else 0.0


def lire_la_trajectoire(z: np.ndarray, taux: float, porteuse_hz: float) -> Dict[str, Any]:
    """Le cercle, la circularité, l'arc, le dosage et le retard d'une trajectoire `E(t)`."""
    centre, rayon = ajuster_un_cercle(z)
    fiche: Dict[str, Any] = {"rayon": rayon, "centre": abs(centre)}
    if rayon <= 0.0 or not np.isfinite(rayon):
        fiche.update({"circularite": None, "arc_deg": 0.0, "aplatissement": None, "cercle": False})
        return fiche
    distances = np.abs(z - centre)
    angle = np.unwrap(np.angle(z - centre))
    fiche["circularite"] = round(float(np.std(distances) / rayon), 4)
    fiche["arc_deg"] = round(float(np.degrees(angle.max() - angle.min())), 1)
    fiche["aplatissement"] = round(aplatissement(z), 4)
    fiche["cercle"] = bool(fiche["circularite"] <= CERCLE_MAX and fiche["arc_deg"] >= ARC_MIN_DEG
                           and fiche["aplatissement"] >= APLATI_MIN)
    fiche["dosage"] = round(rayon / (rayon + abs(centre)), 4)
    retard_ms = -(angle - angle.mean()) / (2 * np.pi * porteuse_hz) * 1000.0
    fiche["retard_ms"] = {"f1": round(amplitude_a(retard_ms, taux, F1_HZ), 4),
                          "2f1": round(amplitude_a(retard_ms, taux, 2 * F1_HZ), 4),
                          "f2": round(amplitude_a(retard_ms, taux, F2_HZ), 4),
                          "2f2": round(amplitude_a(retard_ms, taux, 2 * F2_HZ), 4),
                          "crete_a_crete": round(float(retard_ms.max() - retard_ms.min()), 4)}
    return fiche


def analyser(x: np.ndarray, sr: int, notes: Sequence[int], la4: float = LA4_HZ) -> List[Dict[str, Any]]:
    """Chaque raie : sa porteuse, sa bande, et ce que sa trajectoire décrit."""
    f, P = h49.spectre_entier(x, sr)
    bandes = demi_bandes(notes, la4)
    raies: List[Dict[str, Any]] = []
    for note in notes:
        nominal = h47.hz_de(note, la4)
        forme = h49.forme_de_raie(f, P, nominal)
        fiche: Dict[str, Any] = {"note": note, "nominal_hz": round(nominal, 3), "demi_bande_hz": bandes[note],
                                 "rapport_au_fond_db": forme["rapport_au_fond_db"],
                                 "vue": bool(forme["rapport_au_fond_db"] >= h49.VUE_DB)}
        comps = h49.composantes(x, sr, nominal)
        if not comps:
            fiche.update({"vue": False, "cercle": False})
            raies.append(fiche)
            continue
        porteuse = h50.lire_raie(comps, nominal)["porteuse_hz"]
        z, taux = bande_de_base(x, sr, porteuse, bandes[note])
        z, delta = affiner_la_porteuse(z, taux)
        fiche["porteuse_hz"] = round(porteuse + delta, 4)
        fiche.update(lire_la_trajectoire(z, taux, porteuse + delta))
        raies.append(fiche)
    return raies


def dispersion(valeurs: Sequence[float]) -> Optional[float]:
    if len(valeurs) < 3 or np.mean(valeurs) <= 0:
        return None
    return float(np.std(valeurs) / np.mean(valeurs))


# ------------------------------------------------------------------ le verdict

def verdict(mesure: Dict[str, Any]) -> List[Tuple[int, str, str]]:
    raies = mesure["raies"]
    vues = [r for r in raies if r["vue"]]
    cachees = [f"{r['nominal_hz']:.1f} Hz" for r in raies if not r["vue"]]
    dit = f"{len(vues)} raie(s) vue(s) sur {len(raies)}" + (f" ; non vues : {', '.join(cachees)}" if cachees else "")
    sortie: List[Tuple[int, str, str]] = []
    cercles = [r for r in vues if r["cercle"]]
    loin = [r for r in vues if r.get("circularite") is None or r["circularite"] > PAS_CERCLE
            or r["arc_deg"] < ARC_MIN_DEG or (r.get("aplatissement") or 0.0) < APLATI_MIN]
    lues = ", ".join(f"{'—' if r.get('circularite') is None else format(100 * r['circularite'], '.0f')} %/{r['arc_deg']:.0f}°" for r in vues)
    if len(cercles) >= 8:
        v = "TENU"
    elif len(loin) >= 5:
        v = "ÉCHEC"
    else:
        v = "ENTRE LES DEUX"
    sortie.append((2, v, f"{len(cercles)} raie(s) en cercle (circularité ≤ {100 * CERCLE_MAX:.0f} %, arc ≥ {ARC_MIN_DEG:.0f}°, "
                         f"aplatissement ≥ {APLATI_MIN:.2f}), "
                         f"{len(loin)} franchement hors (circularité / arc par raie : {lues}) ; {dit}"))
    if v == "ÉCHEC":
        return sortie + [(n, "SANS OBJET", "l'attendu 2 a échoué : il n'y a pas de cercle à lire") for n in (3, 4, 5)]

    def juger(numero: int, valeurs: List[float], nom: str, unite: str) -> None:
        d = dispersion(valeurs)
        if d is None:
            sortie.append((numero, "NON MESURABLE", f"{len(valeurs)} raie(s) à cercle : il en faut trois"))
            return
        issue = "TENU" if d <= 0.15 else ("ÉCHEC" if d > 0.40 else "ENTRE LES DEUX")
        sortie.append((numero, issue, f"{nom} sur {len(valeurs)} raies à cercle : dispersion {100 * d:.0f} % "
                                      f"(moyenne {np.mean(valeurs):.3f} {unite} ; par raie : {', '.join(format(x, '.3f') for x in valeurs)})"))

    juger(3, [r["retard_ms"]["f1"] for r in cercles], "amplitude du retard à f1", "ms")
    juger(4, [r["dosage"] for r in cercles], "dosage", "")
    rapports = [r["retard_ms"]["2f1"] / r["retard_ms"]["f1"] for r in cercles if r["retard_ms"]["f1"] > 0]
    if not rapports:
        sortie.append((5, "NON MESURABLE", "aucune raie à cercle"))
    else:
        mediane = float(np.median(rapports))
        issue = "TENU" if mediane <= 0.10 else ("ÉCHEC" if mediane > 0.30 else "ENTRE LES DEUX")
        sortie.append((5, issue, f"2·f1 rapporté à f1 dans le retard : médiane {100 * mediane:.0f} % "
                                 f"(par raie : {', '.join(format(100 * x, '.0f') for x in rapports)} %)"))
    return sortie


def imprimer(mesure: Dict[str, Any]) -> None:
    for r in mesure["raies"]:
        if not r["vue"]:
            print(f"  note {r['note']} {r['nominal_hz']:7.2f} Hz  NON VUE (fond {r['rapport_au_fond_db']:.1f} dB)")
            continue
        circ = "—" if r.get("circularite") is None else f"{100 * r['circularite']:.1f} %"
        ligne = (f"  note {r['note']} {r['nominal_hz']:7.2f} Hz  bande ± {r['demi_bande_hz']:.1f} Hz  fond {r['rapport_au_fond_db']:5.1f} dB  "
                 f"porteuse {r['porteuse_hz']:.3f} Hz  circularité {circ}  arc {r['arc_deg']:.0f}°  "
                 f"aplatissement {r.get('aplatissement') or 0.0:.3f}  "
                 f"{'CERCLE' if r['cercle'] else 'pas un cercle'}")
        if "dosage" in r:
            t = r["retard_ms"]
            ligne += (f"  dosage {r['dosage']:.3f}  retard : f1 {t['f1']:.3f} ms, 2f1 {t['2f1']:.3f}, f2 {t['f2']:.3f}, "
                      f"2f2 {t['2f2']:.3f}, crête à crête {t['crete_a_crete']:.3f} ms")
        print(ligne)
    print("VERDICT (recalculé depuis la mesure ; l'attendu 1 est la suite de tests) :")
    for numero, v, detail in verdict(mesure):
        print(f"  attendu {numero} : {v} — {detail}")


def mesurer(a: argparse.Namespace) -> int:
    for p in (a.original, a.oracle):
        if not p.is_file() or p.stat().st_size == 0:
            print(f"REFUS : {p} absent ou vide")
            return 2
    notes = [int(n) for n in json.loads(a.oracle.read_text(encoding="utf-8"))["oracle"]["notes"]]
    try:
        x, sr = h49.lire_extrait(a.original, a.debut, a.fin)
    except ValueError as e:
        print(f"REFUS : {e}")
        return 2
    mesure = {"provenance": {"original": str(a.original), "oracle": str(a.oracle), "debut_s": a.debut, "fin_s": a.fin,
                             "la4_hz": LA4_HZ, "f1_hz": F1_HZ, "f2_hz": F2_HZ, "demi_bande_max_hz": DEMI_BANDE_MAX_HZ,
                             "arc_min_deg": ARC_MIN_DEG, "aplati_min": APLATI_MIN},
              "raies": analyser(x, sr, notes)}
    a.sortie.mkdir(parents=True, exist_ok=True)
    (a.sortie / "mesure.json").write_text(json.dumps(mesure, indent=1, ensure_ascii=False, sort_keys=True) + "\n",
                                          encoding="utf-8")
    print(f"EXTRAIT {a.debut:.0f}-{a.fin:.0f} s ; ÉCRIT : {a.sortie / 'mesure.json'}")
    imprimer(mesure)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sous = ap.add_subparsers(dest="mode", required=True)
    m = sous.add_parser("mesurer")
    m.add_argument("original", type=Path)
    m.add_argument("--oracle", type=Path, required=True)
    m.add_argument("--sortie", type=Path, required=True)
    m.add_argument("--debut", type=float, default=DEBUT_S)
    m.add_argument("--fin", type=float, default=FIN_S)
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
