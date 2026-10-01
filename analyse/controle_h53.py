#!/usr/bin/env python3
"""H53, LE CONTRÔLE DE L'INSTRUMENT AU BRUIT DE L'ORIGINAL (docs/CDC-reload-indifferenciable.md § 15.2).

L'instrument de H53 n'a été éprouvé qu'à un bruit de −30 dB ; les raies de l'original
sont à 10-24 dB de leur fond. Avant de lire « 0 cercle sur 10 » comme une structure,
on lui donne la structure qu'il cherche — un direct plus UNE lecture (dosage 0,5 ;
retard 1,8 ms ± 0,85 à `f1` ± 0,3 à `f2`) — aux dix hauteurs de l'oracle, chaque raie
noyée à SON rapport au fond de l'original, mesuré par la MÊME fonction
(`mesure_h49.forme_de_raie`), à ± 1 dB. Trois tirages de bruit.

Règle écrite AVANT d'être lancée (§ 15.2) : au moins 8 cercles sur 10 dans au moins
deux tirages — l'instrument voit à ce bruit, l'échec de l'attendu 2 dit la structure ;
5 ou moins dans au moins deux tirages — aveugle, H53 NON CONCLUANTE ; sinon entre les
deux, non concluante aussi.

    analyse/.venv/bin/python analyse/controle_h53.py reconstruction/travail/reload-h53/mesure.json

Le réglage du bruit est publié raie par raie : une raie qui n'atteint pas sa cible à
± 1 dB est NOMMÉE (le fond de H49 est pris à 6-6,9 Hz de la raie, où tombent aussi des
bandes latérales : un rapport peut plafonner sans bruit). Rend 0 quand le contrôle est
complet, 2 sur un refus.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Sequence

import numpy as np

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))

import mesure_h47 as h47  # noqa: E402
import mesure_h49 as h49  # noqa: E402
import mesure_h53 as h  # noqa: E402

SR = 22050
SECONDES = 24.0
DOSAGE = 0.5
BASE_S, F1_S, F2_S = 1.8e-3, 0.85e-3, 0.3e-3
TIRAGES = (101, 102, 103)
TOLERANCE_DB = 1.0
PASSES = 6


def _notes_pures(notes: Sequence[int], t: np.ndarray) -> Dict[int, np.ndarray]:
    tau = BASE_S + F1_S * np.sin(2 * np.pi * h.F1_HZ * t) + F2_S * np.sin(2 * np.pi * h.F2_HZ * t + 0.7)
    sortie = {}
    for note in notes:
        f = h47.hz_de(note, h.LA4_HZ)
        sortie[note] = (1 - DOSAGE) * np.sin(2 * np.pi * f * t) + DOSAGE * np.sin(2 * np.pi * f * (t - tau))
    return sortie


def _rapports(x: np.ndarray, notes: Sequence[int]) -> Dict[int, float]:
    f, P = h49.spectre_entier(x, SR)
    return {n: float(h49.forme_de_raie(f, P, h47.hz_de(n, h.LA4_HZ))["rapport_au_fond_db"]) for n in notes}


def regler(notes: Sequence[int], cibles: Dict[int, float], graine: int) -> Dict[str, Any]:
    """Les amplitudes par note qui mettent chaque raie à sa cible, sous un bruit blanc fixe."""
    t = np.arange(int(SECONDES * SR)) / SR
    pures = _notes_pures(notes, t)
    bruit = np.random.default_rng(graine).standard_normal(len(t)) * 0.01
    gains = {n: 1.0 for n in notes}
    for _ in range(PASSES):
        x = sum(gains[n] * pures[n] for n in notes) + bruit
        lus = _rapports(x, notes)
        if all(abs(lus[n] - cibles[n]) <= TOLERANCE_DB for n in notes):
            break
        for n in notes:
            gains[n] *= 10 ** ((cibles[n] - lus[n]) / 20.0)
    x = sum(gains[n] * pures[n] for n in notes) + bruit
    lus = _rapports(x, notes)
    return {"x": x, "rapports": lus, "gains": gains}


def controler(mesure: Dict[str, Any]) -> Dict[str, Any]:
    raies = mesure["raies"]
    notes = [int(r["note"]) for r in raies]
    cibles = {int(r["note"]): float(r["rapport_au_fond_db"]) for r in raies}
    tirages: List[Dict[str, Any]] = []
    for graine in TIRAGES:
        reglage = regler(notes, cibles, graine)
        lues = h.analyser(reglage["x"], SR, notes)
        fiches = []
        for r in lues:
            n = int(r["note"])
            fiches.append({"note": n, "cible_db": cibles[n], "rapport_db": round(reglage["rapports"][n], 2),
                           "atteint": bool(abs(reglage["rapports"][n] - cibles[n]) <= TOLERANCE_DB),
                           "circularite": r.get("circularite"), "arc_deg": r.get("arc_deg"),
                           "aplatissement": r.get("aplatissement"), "cercle": bool(r.get("cercle")),
                           "dosage": r.get("dosage"), "retard_f1_ms": (r.get("retard_ms") or {}).get("f1")})
        tirages.append({"graine": graine, "cercles": sum(f["cercle"] for f in fiches), "raies": fiches})
    voit = sum(1 for tg in tirages if tg["cercles"] >= 8)
    aveugle = sum(1 for tg in tirages if tg["cercles"] <= 5)
    issue = "VOIT" if voit >= 2 else ("AVEUGLE" if aveugle >= 2 else "ENTRE LES DEUX")
    return {"provenance": {"dosage": DOSAGE, "base_ms": BASE_S * 1e3, "f1_ms": F1_S * 1e3, "f2_ms": F2_S * 1e3,
                           "sr": SR, "secondes": SECONDES, "tolerance_db": TOLERANCE_DB, "tirages": list(TIRAGES)},
            "tirages": tirages, "issue": issue}


def imprimer(controle: Dict[str, Any]) -> None:
    for tg in controle["tirages"]:
        print(f"tirage {tg['graine']} : {tg['cercles']} cercle(s) sur {len(tg['raies'])}")
        for f in tg["raies"]:
            circ = "—" if f["circularite"] is None else f"{100 * f['circularite']:.1f} %"
            manque = "" if f["atteint"] else "  CIBLE NON ATTEINTE"
            print(f"  note {f['note']}  cible {f['cible_db']:5.1f} dB  lu {f['rapport_db']:5.1f} dB  circularité {circ}  "
                  f"arc {f['arc_deg'] or 0:.0f}°  aplatissement {f['aplatissement'] or 0:.3f}  "
                  f"{'CERCLE' if f['cercle'] else 'pas un cercle'}  dosage {f['dosage']}  retard f1 {f['retard_f1_ms']}{manque}")
    print(f"ISSUE (règle du § 15.2) : {controle['issue']}")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    chemin = Path(sys.argv[1])
    if not chemin.is_file() or chemin.stat().st_size == 0:
        print(f"REFUS : {chemin} absent ou vide")
        return 2
    controle = controler(json.loads(chemin.read_text(encoding="utf-8")))
    sortie = chemin.with_name("controle.json")
    sortie.write_text(json.dumps(controle, indent=1, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8")
    print(f"ÉCRIT : {sortie}")
    imprimer(controle)
    return 0


if __name__ == "__main__":
    sys.exit(main())
