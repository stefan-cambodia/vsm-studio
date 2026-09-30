#!/usr/bin/env python3
"""H50 — LE PAD EST-IL UN SINUS PAR NOTE SOUS UN CHORUS À DEUX CADENCES ? (docs/CDC-reload-indifferenciable.md § 12)

La lecture du § 11.1 a été faite APRÈS avoir vu les chiffres de 16-40 s. Cet outil la
juge sur des extraits que personne n'a regardés : chaque raie du pad doit avoir sa
PORTEUSE à la hauteur tempérée de sa note, et ses autres composantes à des écarts de
la famille {f1, 2f1, 3f1, f2, 2f2, f1+f2, f2−f1, 2f1−f2, 2f1+f2}, les mêmes en HERTZ
à toutes les hauteurs — avec f1 = 1,751 Hz et f2 = 2,37 Hz, fixés AVANT la mesure.

    analyse/.venv/bin/python analyse/mesure_h50.py mesurer ORIGINAL.wav STEM.wav \\
        --extrait 130 154 --extrait 272 296 --sortie reconstruction/travail/reload-h50
    analyse/.venv/bin/python analyse/mesure_h50.py verdict reconstruction/travail/reload-h50/mesure.json

L'oracle (les notes) est celui de H47, sur le STEM de l'extrait ; les composantes sont
celles de H49, dans l'ORIGINAL, dans une fenêtre de ± 6 Hz autour de la hauteur
TEMPÉRÉE (et non du pic le plus fort : à 555 Hz, c'est une bande latérale qui domine).

LE TAUX DU HASARD EST PUBLIÉ : neuf membres à ± 0,08 Hz couvrent 24 % de la bande de
± 6 Hz. Une part de 30 % sur la famille ne dirait rien.

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

LA4_HZ = 443.1372
F1_HZ, F2_HZ = 1.751, 2.37
TOLERANCE_HZ = 0.08
MUET = 4                             # moins de raies vues : l'extrait ne juge rien


def famille(f1: float = F1_HZ, f2: float = F2_HZ) -> Dict[str, float]:
    """Les neuf écarts de la famille, fixés au § 12."""
    return {"f1": f1, "2f1": 2 * f1, "3f1": 3 * f1, "f2": f2, "2f2": 2 * f2,
            "f1+f2": f1 + f2, "f2-f1": f2 - f1, "2f1-f2": 2 * f1 - f2, "2f1+f2": 2 * f1 + f2}


def taux_du_hasard() -> float:
    return len(famille()) * 2 * 2 * TOLERANCE_HZ / (2 * h49.DEMI_BANDE_HZ)


def membre(ecart_hz: float) -> Optional[str]:
    """Le membre de la famille le plus proche de |écart|, s'il est à ± 0,08 Hz."""
    nom, valeur = min(famille().items(), key=lambda c: abs(abs(ecart_hz) - c[1]))
    return nom if abs(abs(ecart_hz) - valeur) <= TOLERANCE_HZ else None


def lire_raie(composantes: Sequence[Dict[str, float]], nominal_hz: float) -> Dict[str, Any]:
    """La porteuse (la composante la plus proche du tempéré) et les autres, rapportées à elle."""
    porteuse = min(composantes, key=lambda c: abs(c["hz"] - nominal_hz))
    secondaires = []
    for c in composantes:
        if c is porteuse:
            continue
        ecart = c["hz"] - porteuse["hz"]
        secondaires.append({"ecart_hz": round(ecart, 3), "db": c["db"], "membre": membre(ecart)})
    return {
        "porteuse_hz": porteuse["hz"],
        "porteuse_cents": round(1200 * math.log2(porteuse["hz"] / nominal_hz), 2),
        "porteuse_db": porteuse["db"],
        "secondaires": sorted(secondaires, key=lambda c: c["ecart_hz"]),
    }


def signature(raie: Dict[str, Any]) -> Optional[bool]:
    """Attendu 5. Sous 260 Hz : la plus forte bande f1 dépasse la plus forte 2f1 d'au moins
    3 dB (ou 2f1 est absente) ; au-dessus de 450 Hz : l'inverse. `None` : raie non concernée."""
    un = [c["db"] for c in raie["secondaires"] if c["membre"] == "f1"]
    deux = [c["db"] for c in raie["secondaires"] if c["membre"] == "2f1"]
    if raie["nominal_hz"] < 260.0:
        return bool(un) and (not deux or max(un) - max(deux) >= 3.0)
    if raie["nominal_hz"] > 450.0:
        return bool(deux) and (not un or max(deux) - max(un) >= 3.0)
    return None


def analyser(original: np.ndarray, stem: np.ndarray, sr: int, la4: float = LA4_HZ) -> Dict[str, Any]:
    """Les notes du stem, et pour chacune sa raie dans l'original, lue contre la famille."""
    pics = h47.pics_tenus(stem, sr)
    notes = h47.notes_de_l_oracle(pics, la4)
    f, P = h49.spectre_entier(original, sr)
    raies: List[Dict[str, Any]] = []
    for note in notes:
        nominal = h47.hz_de(note, la4)
        forme = h49.forme_de_raie(f, P, nominal)
        fiche: Dict[str, Any] = {"note": note, "nominal_hz": round(nominal, 3),
                                 "rapport_au_fond_db": forme["rapport_au_fond_db"],
                                 "vue": bool(forme["rapport_au_fond_db"] >= h49.VUE_DB)}
        comps = h49.composantes(original, sr, nominal)
        if not comps:
            fiche["vue"] = False
            fiche["secondaires"] = []
        else:
            fiche.update(lire_raie(comps, nominal))
        raies.append(fiche)
    return {"notes": notes, "raies": raies}


# ------------------------------------------------------------------ le verdict

def juger(extrait: Dict[str, Any]) -> List[Tuple[int, str, str]]:
    """Les attendus 2 à 5 du § 12 pour UN extrait."""
    vues = [r for r in extrait["raies"] if r["vue"]]
    cachees = [f"{r['nominal_hz']:.1f} Hz ({r['rapport_au_fond_db']:.1f} dB)" for r in extrait["raies"] if not r["vue"]]
    dit = f"{len(vues)} raie(s) vue(s) sur {len(extrait['raies'])}" + (f" ; non vues : {', '.join(cachees)}" if cachees else "")
    if len(vues) < MUET:
        return [(n, "MUET", dit) for n in (2, 3, 4, 5)]
    sortie: List[Tuple[int, str, str]] = []

    justes = [r for r in vues if abs(r["porteuse_cents"]) <= 1.0]
    part = len(justes) / len(vues)
    v = "TENU" if part >= 0.8 else ("ÉCHEC" if part < 0.5 else "ENTRE LES DEUX")
    sortie.append((2, v, f"{len(justes)} porteuse(s) sur {len(vues)} à ≤ 1 cent du tempéré "
                         f"(écarts : {', '.join(format(r['porteuse_cents'], '+.1f') for r in vues)} cents) ; {dit}"))

    secondaires = [c for r in vues for c in r["secondaires"]]
    if not secondaires:
        sortie.append((3, "NON MESURABLE", "aucune composante secondaire"))
    else:
        sur = [c for c in secondaires if c["membre"]]
        part = len(sur) / len(secondaires)
        v = "TENU" if part >= 0.7 else ("ÉCHEC" if part < 0.4 else "ENTRE LES DEUX")
        hors = ", ".join(format(c["ecart_hz"], "+.2f") for c in secondaires if not c["membre"]) or "aucune"
        sortie.append((3, v, f"{len(sur)} composante(s) secondaire(s) sur {len(secondaires)} sur la famille, soit "
                             f"{100 * part:.0f} % (hasard {100 * taux_du_hasard():.0f} %) ; hors famille (Hz) : {hors}"))

    ordre = {"f1": 1, "2f1": 2, "3f1": 3}
    lus = [abs(c["ecart_hz"]) / ordre[c["membre"]] for c in secondaires if c["membre"] in ordre]
    if len(lus) < 5:
        sortie.append((4, "NON MESURABLE", f"{len(lus)} composante(s) sur le peigne k·f1 : il en faut cinq"))
    else:
        moyenne = float(np.mean(lus))
        erreur = float(np.std(lus) / math.sqrt(len(lus)))
        ecart = abs(moyenne - F1_HZ)
        v = "TENU" if ecart <= 0.010 else ("ÉCHEC" if ecart > 0.030 else "ENTRE LES DEUX")
        sortie.append((4, v, f"f1 relu sur {len(lus)} composantes : {moyenne:.4f} Hz (erreur type {erreur:.4f}) "
                             f"pour {F1_HZ:.3f} attendu"))

    concernees = [(r, signature(r)) for r in vues]
    concernees = [(r, s) for r, s in concernees if s is not None]
    if not concernees:
        sortie.append((5, "NON MESURABLE", "aucune raie sous 260 Hz ni au-dessus de 450 Hz"))
    else:
        vraies = [r for r, s in concernees if s]
        part = len(vraies) / len(concernees)
        v = "TENU" if part >= 0.8 else ("ÉCHEC" if part < 0.5 else "ENTRE LES DEUX")
        fausses = ", ".join(f"{r['nominal_hz']:.0f} Hz" for r, s in concernees if not s) or "aucune"
        sortie.append((5, v, f"la signature du retard sur {len(vraies)} raie(s) sur {len(concernees)} concernées ; fausse à : {fausses}"))
    return sortie


def issue(mesure: Dict[str, Any]) -> str:
    """H50 tenue si 2, 3, 4 et 5 tiennent sur tous les extraits ; réfutée si 3 échoue sur l'un."""
    verdicts = [dict((n, v) for n, v, _ in juger(e)) for e in mesure["extraits"]]
    if any(v[3] == "ÉCHEC" for v in verdicts):
        return "RÉFUTÉE"
    if all(v[n] == "TENU" for v in verdicts for n in (2, 3, 4, 5)):
        return "TENUE"
    return "ENTRE LES DEUX"


def imprimer(mesure: Dict[str, Any]) -> None:
    for e in mesure["extraits"]:
        print(f"EXTRAIT {e['debut_s']:.0f}-{e['fin_s']:.0f} s — notes {e['notes']}")
        for r in e["raies"]:
            if not r["vue"]:
                print(f"  note {r['note']} nominal {r['nominal_hz']:7.2f} Hz  NON VUE (fond {r['rapport_au_fond_db']:.1f} dB)")
                continue
            sec = "  ".join(f"{c['ecart_hz']:+.2f}{'=' + c['membre'] if c['membre'] else ' ?'} {c['db']:+.1f}" for c in r["secondaires"]) or "—"
            print(f"  note {r['note']} nominal {r['nominal_hz']:7.2f} Hz  porteuse {r['porteuse_cents']:+.1f} c, {r['porteuse_db']:+.1f} dB : {sec}")
        for numero, v, detail in juger(e):
            print(f"  attendu {numero} : {v} — {detail}")
    print(f"H50 : {issue(mesure)} (l'attendu 1 est la suite de tests)")


def mesurer(a: argparse.Namespace) -> int:
    for p in (a.original, a.stem):
        if not p.is_file() or p.stat().st_size == 0:
            print(f"REFUS : {p} absent ou vide")
            return 2
    if not a.extrait:
        print("REFUS : aucun --extrait")
        return 2
    extraits = []
    for debut, fin in a.extrait:
        try:
            original, sr = h49.lire_extrait(a.original, debut, fin)
            stem, sr_stem = h49.lire_extrait(a.stem, debut, fin)
        except ValueError as e:
            print(f"REFUS : {e}")
            return 2
        if sr != sr_stem:
            print(f"REFUS : l'original est à {sr} Hz et le stem à {sr_stem} Hz")
            return 2
        fiche = analyser(original, stem, sr)
        fiche.update({"debut_s": debut, "fin_s": fin})
        extraits.append(fiche)
    mesure = {"provenance": {"original": str(a.original), "stem": str(a.stem), "la4_hz": LA4_HZ,
                             "f1_hz": F1_HZ, "f2_hz": F2_HZ, "tolerance_hz": TOLERANCE_HZ,
                             "famille": famille(), "hasard": round(taux_du_hasard(), 3)},
              "extraits": extraits}
    a.sortie.mkdir(parents=True, exist_ok=True)
    (a.sortie / "mesure.json").write_text(json.dumps(mesure, indent=1, ensure_ascii=False, sort_keys=True) + "\n",
                                          encoding="utf-8")
    print(f"ÉCRIT : {a.sortie / 'mesure.json'}")
    imprimer(mesure)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sous = ap.add_subparsers(dest="mode", required=True)
    m = sous.add_parser("mesurer")
    m.add_argument("original", type=Path)
    m.add_argument("stem", type=Path)
    m.add_argument("--extrait", nargs=2, type=float, action="append", metavar=("DEBUT", "FIN"))
    m.add_argument("--sortie", type=Path, required=True)
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
