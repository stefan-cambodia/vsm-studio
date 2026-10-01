#!/usr/bin/env python3
"""H48 — le verdict du cache des mesures de PROJET (docs/CDC-reload-indifferenciable.md § 10).

    python analyse/verdict_h48.py reconstruction/travail/h48

Le dossier porte quatre courses du même morceau, écrites par `ab.sh` :
  T/  le témoin (`--sans-cache-rendus`)          T.log
  A/  avec le cache, qu'elle remplit             A.log
  B/  la même, rejouée sur le cache de A         B.log
  C/  tuée à son premier réglage au mélange      C-tuee.log, puis relancée : C.log
et `cache-apres-T.txt` (le nombre de fichiers du cache après le témoin).

Il RECALCULE les cinq attendus depuis les fichiers, sans rien rendre. Une comparaison
dont un côté manque rend « NON MESURÉ », jamais « différent ». Rend 0 si les attendus
1, 2 et 3 tiennent, 1 sinon, 2 si une course manque.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

VERDICT = re.compile(r"verdict du mélange : .* en (\d+) s")
REGLAGE = re.compile(r"réglage au MÉLANGE .*?, (\d+) s\)")
SECOND = re.compile(r"second verdict -> .* en (\d+) s")
COMPTE = re.compile(r"mesures de projet : (\d+) payée\(s\), (\d+) relue\(s\)")


def durees(journal: Path) -> Dict[str, Any]:
    """Ce que le journal d'une course dit de ses étapes au mélange, en secondes."""
    texte = journal.read_text(encoding="utf-8", errors="replace")
    verdicts = [int(x) for x in VERDICT.findall(texte)]
    reglages = [int(x) for x in REGLAGE.findall(texte)]
    seconds = [int(x) for x in SECOND.findall(texte)]
    compte = COMPTE.search(texte)
    return {"verdict": verdicts, "reglages": reglages, "seconds": seconds,
            "total": sum(verdicts) + sum(reglages) + sum(seconds),
            "compte": (int(compte.group(1)), int(compte.group(2))) if compte else None,
            "finie": "[5/5]" in texte}


def differences(a: Any, b: Any, chemin: str = "") -> List[str]:
    """Les chemins où deux documents JSON diffèrent, valeur par valeur."""
    if isinstance(a, dict) and isinstance(b, dict):
        sortie: List[str] = []
        for cle in sorted(set(a) | set(b)):
            if cle not in a or cle not in b:
                sortie.append(f"{chemin}/{cle} (présent d'un seul côté)")
            else:
                sortie += differences(a[cle], b[cle], f"{chemin}/{cle}")
        return sortie
    if isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            return [f"{chemin} (longueurs {len(a)} et {len(b)})"]
        sortie = []
        for i, (x, y) in enumerate(zip(a, b, strict=True)):
            sortie += differences(x, y, f"{chemin}[{i}]")
        return sortie
    return [] if a == b else [f"{chemin} : {a!r} ≠ {b!r}"]


def comparer(temoin: Path, essai: Path) -> Tuple[Optional[bool], str]:
    """(identique ?, récit) — `None` si un fichier manque d'un côté."""
    for dossier in (temoin, essai):
        for nom in ("project.json", "rapport.json"):
            f = dossier / nom
            if not f.is_file() or f.stat().st_size == 0:
                return None, f"NON MESURÉ — {f} absent ou vide"
    projet = (temoin / "project.json").read_bytes() == (essai / "project.json").read_bytes()
    ra = json.loads((temoin / "rapport.json").read_text(encoding="utf-8"))
    rb = json.loads((essai / "rapport.json").read_text(encoding="utf-8"))
    dans_provenance = differences(ra.get("provenance"), rb.get("provenance"), "provenance")
    ra.pop("provenance", None)
    rb.pop("provenance", None)
    hors = differences(ra, rb)
    recit = (f"project.json {'identique à l’octet' if projet else 'DIFFÉRENT'} ; rapport hors provenance : "
             f"{len(hors)} différence(s)" + (f" — {' ; '.join(hors[:4])}" if hors else "")
             + f" ; distance globale {ra.get('globalDistance')!r} / {rb.get('globalDistance')!r}"
             + f" ; dans la provenance : {len(dans_provenance)} ({', '.join(d.split(' : ')[0] for d in dans_provenance[:6])})")
    return projet and not hors, recit


def rapport_pct(essai: int, temoin: int) -> Optional[float]:
    return None if temoin <= 0 else 100.0 * essai / temoin


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    s = Path(sys.argv[1])
    journaux = {nom: s / f"{nom}.log" for nom in ("T", "A", "B", "C-tuee", "C")}
    manquants = [str(j) for j in journaux.values() if not j.is_file() or j.stat().st_size == 0]
    if manquants:
        print("REFUS : journal absent ou vide — " + ", ".join(manquants))
        return 2
    d = {nom: durees(j) for nom, j in journaux.items()}
    for nom, x in d.items():
        print(f"  {nom:7s} verdict {x['verdict']} s, réglages {x['reglages']} s, seconds verdicts {x['seconds']} s — "
              f"total {x['total']} s ; mesures de projet (payées, relues) : {x['compte']} ; "
              f"{'finie' if x['finie'] else 'NON finie'}")

    tenus: Dict[int, bool] = {}
    print("ATTENDU 1 — l'identité, contre T :")
    identiques = []
    for nom in ("A", "B", "C"):
        ok, recit = comparer(s / "T", s / nom)
        identiques.append(ok)
        print(f"    {nom} : {'IDENTIQUE' if ok else ('NON MESURÉ' if ok is None else 'DIFFÉRENT')} — {recit}")
    if (s / "T2" / "rapport.json").is_file():
        ok, recit = comparer(s / "T", s / "T2")
        print(f"    T′ (second témoin) : {'IDENTIQUE' if ok else 'DIFFÉRENT'} — {recit}")
    tenus[1] = all(x is True for x in identiques)
    print(f"  → {'TENU' if tenus[1] else 'ÉCHEC' if all(x is not None for x in identiques) else 'NON MESURÉ'}")

    p = rapport_pct(d["B"]["total"], d["A"]["total"])
    payees_b = d["B"]["compte"][0] if d["B"]["compte"] else None
    if p is None or payees_b is None:
        print("ATTENDU 2 — le rejeu : NON MESURÉ (durées de A nulles, ou B sans ligne de compte)")
        tenus[2] = False
    else:
        tenus[2] = p <= 25.0 and payees_b == 0
        issue = "TENU" if tenus[2] else ("ÉCHEC" if p > 60.0 else "ENTRE LES DEUX")
        print(f"ATTENDU 2 — le rejeu : B {d['B']['total']} s pour A {d['A']['total']} s, soit {p:.1f} % ; "
              f"{payees_b} mesure(s) payée(s) dans B → {issue}")

    tuee, reprise = d["C-tuee"], d["C"]
    if not tuee["verdict"] or not tuee["reglages"] or not reprise["verdict"] or not reprise["reglages"]:
        print("ATTENDU 3 — la mort : NON MESURÉ (la course tuée n'a pas atteint son premier réglage, ou la reprise non plus)")
        tenus[3] = False
    else:
        avant = tuee["verdict"][0] + tuee["reglages"][0]
        apres = reprise["verdict"][0] + reprise["reglages"][0]
        p3 = rapport_pct(apres, avant)
        tenus[3] = p3 is not None and p3 <= 25.0 and identiques[2] is True and not tuee["finie"]
        issue = "TENU" if tenus[3] else ("ÉCHEC" if (p3 is None or p3 > 60.0 or identiques[2] is False) else "ENTRE LES DEUX")
        print(f"ATTENDU 3 — la mort : verdict + premier réglage, {avant} s dans la course tuée"
              f"{' (qui a FINI : elle n’a pas été tuée)' if tuee['finie'] else ''}, {apres} s à la reprise"
              f"{'' if p3 is None else f', soit {p3:.1f} %'} ; reprise (payées, relues) {reprise['compte']} ; "
              f"C {'identique à T' if identiques[2] else 'PAS identique à T'} → {issue}")

    compte_t = s / "cache-apres-T.txt"
    if not compte_t.is_file():
        print("ATTENDU 4 — le témoin n'écrit rien : NON MESURÉ (cache-apres-T.txt absent)")
    else:
        n = int(compte_t.read_text().strip() or "0")
        tenus[4] = n == 0 and d["T"]["compte"] is None
        print(f"ATTENDU 4 — le témoin n'écrit rien : {n} fichier(s) au cache après T, ligne de compte "
              f"{'absente' if d['T']['compte'] is None else 'PRÉSENTE'} → {'TENU' if tenus[4] else 'ÉCHEC'}")

    p5 = rapport_pct(d["A"]["total"], d["T"]["total"])
    if p5 is None:
        print("ATTENDU 5 — le coût de la première passe : NON MESURÉ")
    else:
        issue = "TENU" if p5 <= 110.0 else ("ÉCHEC" if p5 > 125.0 else "ENTRE LES DEUX")
        print(f"ATTENDU 5 — le coût de la première passe : A {d['A']['total']} s pour T {d['T']['total']} s, "
              f"soit {p5:.1f} % ; A (payées, relues) {d['A']['compte']} → {issue}")
    return 0 if all(tenus.get(i) for i in (1, 2, 3)) else 1


if __name__ == "__main__":
    sys.exit(main())
