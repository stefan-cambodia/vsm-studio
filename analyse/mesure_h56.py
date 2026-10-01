#!/usr/bin/env python3
"""H56 — ACCORD PAR ACCORD : LÀ OÙ UNE RAIE SONNE, DÉCRIT-ELLE UN CERCLE ? (docs/CDC-reload-indifferenciable.md § 18)

H53 à H55 jugeaient des raies qui s'éteignaient de 10 à 29 dB au fil des accords
(§ 17.2) : une note qui s'éteint ramène sa trajectoire vers l'origine. Ici, chaque raie
n'est jugée que LÀ OÙ ELLE SONNE :

  - l'ENVELOPPE LENTE env(t) : racine de la moyenne glissante de |E|² sur 1,5 s — deux
    cycles et demi de f1 : le mouvement d'une lecture s'y moyenne, un accord non ;
  - les SEGMENTS : env à moins de 10 dB de son maximum sur l'extrait, d'un seul tenant
    pendant au moins 3 s, puis rognés d'une DEMI-FENÊTRE (0,75 s) à chaque bout ;
  - sur chaque segment, la porteuse AFFINÉE SUR LE SEGMENT, puis E jugée par
    l'instrument du § 15.1 TEL QUEL (`mesure_h53.lire_la_trajectoire`).

TROIS CHOIX DU § 18 DÉFAITS PAR LES TESTS, AVANT LA MESURE (01-02/10/2026) :
  1. la porteuse affinée sur l'extrait ENTIER se trompe de 0,03 à 0,10 Hz quand la note
     s'allume et s'éteint (le filtre de ± 0,3 Hz voit les bandes de la porte) : sur 5 s,
     un cercle devient un anneau — une lecture parfaite, 0 segment en cercle sur 11.
     Elle est affinée SUR CHAQUE SEGMENT : le décalage, cherché à ± 0,15 Hz par pas de
     2 mHz, qui rend la trajectoire la plus circulaire. Une recherche qui favorise le
     cercle : la série la subit aussi, et n'en devient pas un (0 segment sur 6) ;
  2. la trajectoire NORMALISÉE par l'enveloppe (E / env) : une fenêtre de 1,5 s ne
     moyenne pas un nombre entier de cycles des LFO, ses ondulations déformaient le
     rayon — 6 à 13 % de circularité sur une lecture parfaite. L'enveloppe ne sert plus
     qu'à trouver les segments ;
  3. le rognage de 0,25 s laissait 0,5 s de transition dans chaque segment : il vaut
     une demi-fenêtre d'enveloppe.

LE CONTRÔLE (attendu 2) : une lecture synthétique parfaite aux mêmes hauteurs, chaque
raie portant l'enveloppe lente MESURÉE sur l'original, jugée sur les MÊMES segments.

    analyse/.venv/bin/python analyse/mesure_h56.py ORIGINAL.wav \\
        --oracle-h47 reconstruction/travail/reload-h47/mesure.json \\
        --mesure-h50 reconstruction/travail/reload-h50/mesure.json --sortie reconstruction/travail/reload-h56

Rend 0 quand la mesure est complète, 2 sur un refus.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

import numpy as np

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))

import mesure_h47 as h47  # noqa: E402
import mesure_h49 as h49  # noqa: E402
import mesure_h50 as h50  # noqa: E402
import mesure_h53 as h53  # noqa: E402
import mesure_h54 as h54  # noqa: E402

FENETRE_ENV_S = 1.5
SOUS_MAX_DB = 10.0
SEGMENT_MIN_S = 3.0                  # d'un seul tenant AVANT d'être rogné
ROGNE_S = 0.75                       # une demi-fenêtre d'enveloppe
DELTAS_HZ = np.arange(-0.15, 0.15 + 1e-9, 0.002)
DOSAGE_CONTROLE = 0.5
BASE_S, F1_S, F2_S = 1.8e-3, 0.85e-3, 0.3e-3
PART_CERCLES_OUI, PART_CERCLES_NON = 0.8, 0.2

Segment = Tuple[int, int]


def trajectoire(x: np.ndarray, sr: int, note: int, demi_bande: float) -> Tuple[np.ndarray, float, Dict[str, Any]]:
    """`E(t)` d'une raie comme au § 15, à 200 Hz : (E, taux, fiche)."""
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
    return z, taux, fiche


def enveloppe(z: np.ndarray, taux: float) -> np.ndarray:
    k = max(1, int(round(FENETRE_ENV_S * taux)))
    return np.sqrt(np.convolve(np.abs(z) ** 2, np.ones(k) / k, mode="same"))


def segments(env: np.ndarray, taux: float) -> List[Segment]:
    """Les tenues d'au moins 3 s à moins de 10 dB du maximum, rognées de 0,75 s à chaque bout."""
    if len(env) == 0 or env.max() <= 0:
        return []
    sonne = env >= env.max() * 10 ** (-SOUS_MAX_DB / 20)
    bords = np.flatnonzero(np.diff(np.concatenate([[0], sonne.astype(int), [0]])))
    rogne = int(round(ROGNE_S * taux))
    sortie = []
    for a, b in zip(bords[::2], bords[1::2], strict=True):
        if (b - a) / taux >= SEGMENT_MIN_S:
            sortie.append((int(a + rogne), int(b - rogne)))
    return sortie


def _circularite(e: np.ndarray) -> float:
    centre, rayon = h53.ajuster_un_cercle(e)
    return float(np.std(np.abs(e - centre)) / rayon) if rayon > 0 and np.isfinite(rayon) else float("inf")


def juger_les_segments(z: np.ndarray, taux: float, porteuse_hz: float, env: np.ndarray,
                       segs: Sequence[Segment]) -> List[Dict[str, Any]]:
    sortie = []
    for a, b in segs:
        e = z[a:b]
        t = np.arange(len(e)) / taux
        delta = float(min(DELTAS_HZ, key=lambda d: _circularite(e * np.exp(-2j * np.pi * d * t))))
        fiche = h53.lire_la_trajectoire(e * np.exp(-2j * np.pi * delta * t), taux, porteuse_hz + delta)
        sortie.append({"debut_s": round(a / taux, 2), "fin_s": round(b / taux, 2), "delta_porteuse_hz": round(delta, 3),
                       "niveau_db": round(float(20 * np.log10(np.sqrt(np.mean(env[a:b] ** 2)) / env.max())), 1),
                       "circularite": fiche.get("circularite"), "arc_deg": fiche.get("arc_deg"),
                       "aplatissement": fiche.get("aplatissement"), "cercle": bool(fiche.get("cercle")),
                       "dosage": fiche.get("dosage"), "retard_f1_ms": (fiche.get("retard_ms") or {}).get("f1")})
    return sortie


def analyser(x: np.ndarray, sr: int, notes: Sequence[int],
             segments_imposes: Optional[Dict[int, List[Segment]]] = None) -> List[Dict[str, Any]]:
    """Chaque raie : son enveloppe, ses segments (les siens, ou ceux qu'on lui impose), et leur jugement."""
    bandes = h53.demi_bandes(notes)
    raies = []
    for n in notes:
        z, taux, fiche = trajectoire(x, sr, n, bandes[n])
        if not fiche["vue"] or not len(z):
            fiche["segments"] = []
            raies.append(fiche)
            continue
        env = enveloppe(z, taux)
        segs = segments_imposes.get(n, []) if segments_imposes is not None else segments(env, taux)
        fiche["taux"] = taux
        fiche["bornes"] = [list(s) for s in segs]
        fiche["env"] = env
        fiche["segments"] = juger_les_segments(z, taux, fiche["porteuse_hz"], env, segs)
        raies.append(fiche)
    return raies


def lecture_sous_enveloppes(notes: Sequence[int], sr: int, n_echantillons: int,
                            enveloppes: Dict[int, Tuple[np.ndarray, float]], graine: int = 56) -> np.ndarray:
    """La lecture parfaite du contrôle, chaque note portant l'enveloppe lente mesurée (ramenée à 1 au maximum).

    L'enveloppe est mesurée sur E(t), qui commence une seconde après l'extrait : elle est
    interpolée sur les instants de l'extrait, tenue à ses bords."""
    t = np.arange(n_echantillons) / sr
    tau = BASE_S + F1_S * np.sin(2 * np.pi * h53.F1_HZ * t) + F2_S * np.sin(2 * np.pi * h53.F2_HZ * t + 0.7)
    y = np.zeros_like(t)
    for n in notes:
        f = h47.hz_de(n, h53.LA4_HZ)
        voix = (1 - DOSAGE_CONTROLE) * np.sin(2 * np.pi * f * t) + DOSAGE_CONTROLE * np.sin(2 * np.pi * f * (t - tau))
        if n in enveloppes:
            env, taux = enveloppes[n]
            te = 1.0 + np.arange(len(env)) / taux
            y += np.interp(t, te, env / env.max()) * voix
    bruit = np.random.default_rng(graine).standard_normal(len(t)) * np.sqrt(0.5 * 10 ** (-30 / 10))
    return y + bruit


def part_de_cercles(raies: Sequence[Dict[str, Any]], notes: Optional[Sequence[int]] = None) -> Tuple[int, int]:
    segs = [s for r in raies if notes is None or r["note"] in notes for s in r.get("segments", [])]
    return sum(s["cercle"] for s in segs), len(segs)


def mesurer_un_extrait(x: np.ndarray, sr: int, notes: List[int]) -> Dict[str, Any]:
    raies = analyser(x, sr, notes)
    enveloppes = {r["note"]: (r["env"], r["taux"]) for r in raies if "env" in r}
    imposes = {r["note"]: [tuple(b) for b in r["bornes"]] for r in raies if "bornes" in r}
    controle = analyser(lecture_sous_enveloppes(notes, sr, len(x), enveloppes), sr, notes,
                        segments_imposes={n: [(int(a), int(b)) for a, b in s] for n, s in imposes.items()})
    for r in raies + controle:
        r.pop("env", None)
        r.pop("taux", None)
    return {"raies": raies, "controle": controle}


def verdict(extrait: Dict[str, Any]) -> List[Tuple[int, str, str]]:
    sortie: List[Tuple[int, str, str]] = []
    oc, on = part_de_cercles(extrait["controle"])
    p2 = oc / on if on else 0.0
    v2 = "TENU" if on and p2 >= PART_CERCLES_OUI else "ÉCHEC"
    sortie.append((2, v2, f"contrôle : {oc} segment(s) en cercle sur {on} ({100 * p2:.0f} %)"))
    c, n = part_de_cercles(extrait["raies"])
    p3 = c / n if n else 0.0
    detail = f"original : {c} segment(s) en cercle sur {n} ({100 * p3:.0f} %)"
    if v2 != "TENU":
        sortie.append((3, "NE JUGE PAS", detail + " — le contrôle n'y voit pas"))
        sortie.append((4, "SANS OBJET", "l'attendu 3 ne juge pas"))
        return sortie
    v3 = "TENU" if n and p3 >= PART_CERCLES_OUI else ("ÉCHEC" if p3 <= PART_CERCLES_NON else "ENTRE LES DEUX")
    sortie.append((3, v3, detail))
    if v3 != "TENU":
        sortie.append((4, "SANS OBJET", "l'attendu 3 ne tient pas"))
        return sortie
    retards = [s["retard_f1_ms"] for r in extrait["raies"] for s in r["segments"] if s["cercle"] and s["retard_f1_ms"]]
    if len(retards) < 3:
        sortie.append((4, "NON MESURABLE", f"{len(retards)} segment(s) à cercle"))
        return sortie
    d = float(np.std(retards) / np.mean(retards))
    v4 = "TENU" if d <= 0.15 else ("ÉCHEC" if d > 0.40 else "ENTRE LES DEUX")
    sortie.append((4, v4, f"retard à f1 sur {len(retards)} segments : moyenne {np.mean(retards):.3f} ms, dispersion {100 * d:.0f} %"))
    return sortie


def imprimer(mesure: Dict[str, Any]) -> None:
    for e in mesure["extraits"]:
        print(f"EXTRAIT {e['debut_s']:.0f}-{e['fin_s']:.0f} s")
        for nom in ("raies", "controle"):
            print(f"  — {'original' if nom == 'raies' else 'contrôle (lecture parfaite sous les enveloppes de l original)'}")
            for r in e[nom]:
                if not r["vue"]:
                    print(f"    note {r['note']}  NON VUE (fond {r['rapport_au_fond_db']:.1f} dB)")
                    continue
                segs = " ; ".join(
                    f"{s['debut_s']:.1f}-{s['fin_s']:.1f} s {('—' if s['circularite'] is None else format(100 * s['circularite'], '.0f'))} %/"
                    f"{s['arc_deg']:.0f}°/{s['aplatissement'] or 0:.2f}{' CERCLE' if s['cercle'] else ''}"
                    f" ret {s['retard_f1_ms']}" for s in r["segments"]) or "aucun segment"
                print(f"    note {r['note']}  {segs}")
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
    mesure: Dict[str, Any] = {"provenance": {"original": str(a.original), "fenetre_env_s": FENETRE_ENV_S,
                                             "sous_max_db": SOUS_MAX_DB, "segment_min_s": SEGMENT_MIN_S,
                                             "rogne_s": ROGNE_S, "cercle_max": h53.CERCLE_MAX,
                                             "arc_min_deg": h53.ARC_MIN_DEG, "aplati_min": h53.APLATI_MIN},
                              "extraits": []}
    for debut, fin, notes in h54.extraits(a.oracle_h47, a.mesure_h50):
        try:
            x, sr = h49.lire_extrait(a.original, debut, fin)
        except ValueError as e:
            print(f"REFUS : {e}")
            return 2
        fiche = mesurer_un_extrait(x, sr, notes)
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
