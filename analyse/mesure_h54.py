#!/usr/bin/env python3
"""H54 — SUR LE STEM « OTHER », LES RAIES DU PAD DÉCRIVENT-ELLES UN CERCLE ? (docs/CDC-reload-indifferenciable.md § 16)

Une seule variable change par rapport à H53 : la SOURCE, le stem au lieu de
l'original. L'instrument est celui de H53 (`mesure_h53.analyser`, `mesure_h53.verdict`),
le contrôle d'aveuglement celui du § 15.2 (`controle_h53.controler`), réglé cette fois
au rapport au fond que chaque raie a DANS LE STEM. Rien n'est réglé ici.

    analyse/.venv/bin/python analyse/mesure_h54.py STEM_OTHER.wav \\
        --oracle-h47 reconstruction/travail/reload-h47/mesure.json \\
        --mesure-h50 reconstruction/travail/reload-h50/mesure.json \\
        --sortie reconstruction/travail/reload-h54

Trois extraits : 16-40 s (les dix notes de l'oracle de H47) ; 130-154 et 272-296 s (les
onze notes que H50 a lues sur le stem de chacun). Rend 0 quand la mesure est complète,
2 sur un refus.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Tuple

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))

import controle_h53 as controle  # noqa: E402
import mesure_h49 as h49  # noqa: E402
import mesure_h53 as h53  # noqa: E402


def extraits(oracle_h47: Path, mesure_h50: Path) -> List[Tuple[float, float, List[int]]]:
    sortie = [(16.0, 40.0, [int(n) for n in json.loads(oracle_h47.read_text(encoding="utf-8"))["oracle"]["notes"]])]
    for e in json.loads(mesure_h50.read_text(encoding="utf-8"))["extraits"]:
        sortie.append((float(e["debut_s"]), float(e["fin_s"]), [int(n) for n in e["notes"]]))
    return sortie


def mesurer_un_extrait(stem: Path, debut: float, fin: float, notes: List[int]) -> Dict[str, Any]:
    x, sr = h49.lire_extrait(stem, debut, fin)
    raies = h53.analyser(x, sr, notes)
    ctrl = controle.controler({"raies": raies})
    a1 = {"VOIT": "TENU", "AVEUGLE": "ÉCHEC"}.get(ctrl["issue"], "ENTRE LES DEUX")
    verdicts = [(int(n), v, d) for n, v, d in h53.verdict({"raies": raies})]
    return {"debut_s": debut, "fin_s": fin, "notes": notes, "raies": raies,
            "controle": {"issue": ctrl["issue"], "cercles_par_tirage": [tg["cercles"] for tg in ctrl["tirages"]],
                         "tirages": ctrl["tirages"]},
            "attendu_1": a1, "verdict_h53": verdicts}


def juger(mesure: Dict[str, Any]) -> List[Tuple[str, str, str]]:
    """(attendu, issue, détail) — les attendus du § 16, recalculés depuis le fichier."""
    sortie: List[Tuple[str, str, str]] = []
    jugeants = []
    for e in mesure["extraits"]:
        nom = f"{e['debut_s']:.0f}-{e['fin_s']:.0f} s"
        sortie.append((f"1 ({nom})", e["attendu_1"], f"cercles de la lecture parfaite par tirage : {e['controle']['cercles_par_tirage']}"))
        v2 = next(v for n, v, _ in e["verdict_h53"] if n == 2)
        d2 = next(d for n, _, d in e["verdict_h53"] if n == 2)
        if e["attendu_1"] != "TENU":
            sortie.append((f"2 ({nom})", "NE JUGE PAS", f"l'attendu 1 n'y tient pas ; lu pour mémoire : {v2} — {d2}"))
            continue
        jugeants.append((nom, v2))
        sortie.append((f"2 ({nom})", v2, d2))
        if v2 == "TENU":
            for n, v, d in e["verdict_h53"]:
                if n in (3, 4, 5):
                    sortie.append((f"4 ({nom}, attendu {n} du § 15)", v, d))
    issues = {v for _, v in jugeants}
    if len(jugeants) < 2:
        sortie.append(("3", "SANS OBJET", f"{len(jugeants)} extrait(s) où l'attendu 1 tient"))
    elif {"TENU", "ÉCHEC"} <= issues:
        sortie.append(("3", "ÉCHEC", "verdicts opposés : " + ", ".join(f"{n} {v}" for n, v in jugeants)))
    elif len(issues) == 1:
        sortie.append(("3", "TENU", "même verdict : " + ", ".join(f"{n} {v}" for n, v in jugeants)))
    else:
        sortie.append(("3", "ENTRE LES DEUX", ", ".join(f"{n} {v}" for n, v in jugeants)))
    return sortie


def imprimer(mesure: Dict[str, Any]) -> None:
    for e in mesure["extraits"]:
        print(f"EXTRAIT {e['debut_s']:.0f}-{e['fin_s']:.0f} s — notes {e['notes']}")
        for r in e["raies"]:
            if not r["vue"]:
                print(f"  note {r['note']} {r['nominal_hz']:7.2f} Hz  NON VUE (fond {r['rapport_au_fond_db']:.1f} dB)")
                continue
            circ = "—" if r.get("circularite") is None else f"{100 * r['circularite']:.1f} %"
            ligne = (f"  note {r['note']} {r['nominal_hz']:7.2f} Hz  fond {r['rapport_au_fond_db']:5.1f} dB  circularité {circ}  "
                     f"arc {r.get('arc_deg') or 0:.0f}°  aplatissement {r.get('aplatissement') or 0:.3f}  "
                     f"{'CERCLE' if r.get('cercle') else 'pas un cercle'}")
            if "dosage" in r:
                ligne += f"  dosage {r['dosage']:.3f}  retard f1 {r['retard_ms']['f1']:.3f} ms, 2f1 {r['retard_ms']['2f1']:.3f}"
            print(ligne)
        print(f"  contrôle (lecture parfaite au même rapport au fond) : {e['controle']['cercles_par_tirage']} cercles "
              f"par tirage → {e['controle']['issue']}")
    print("VERDICT (recalculé depuis la mesure) :")
    for numero, v, detail in juger(mesure):
        print(f"  attendu {numero} : {v} — {detail}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("stem", type=Path)
    ap.add_argument("--oracle-h47", type=Path, required=True)
    ap.add_argument("--mesure-h50", type=Path, required=True)
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args()
    for p in (a.stem, a.oracle_h47, a.mesure_h50):
        if not p.is_file() or p.stat().st_size == 0:
            print(f"REFUS : {p} absent ou vide")
            return 2
    mesure: Dict[str, Any] = {"provenance": {"stem": str(a.stem), "oracle_h47": str(a.oracle_h47),
                                             "mesure_h50": str(a.mesure_h50), "la4_hz": h53.LA4_HZ,
                                             "cercle_max": h53.CERCLE_MAX, "arc_min_deg": h53.ARC_MIN_DEG,
                                             "aplati_min": h53.APLATI_MIN},
                              "extraits": []}
    for debut, fin, notes in extraits(a.oracle_h47, a.mesure_h50):
        try:
            mesure["extraits"].append(mesurer_un_extrait(a.stem, debut, fin, notes))
        except ValueError as e:
            print(f"REFUS : {e}")
            return 2
    a.sortie.mkdir(parents=True, exist_ok=True)
    (a.sortie / "mesure.json").write_text(json.dumps(mesure, indent=1, ensure_ascii=False, sort_keys=True) + "\n",
                                          encoding="utf-8")
    print(f"ÉCRIT : {a.sortie / 'mesure.json'}")
    imprimer(mesure)
    return 0


if __name__ == "__main__":
    sys.exit(main())
