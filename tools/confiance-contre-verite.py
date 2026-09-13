#!/usr/bin/env python3
"""A39 : la confiance d'une note transcrite prédit-elle qu'elle est JUSTE ?

    analyse/.venv/bin/python tools/confiance-contre-verite.py reconstruction/travail/r1-sec-banc

POURQUOI CETTE MESURE (13/09/2026, D223). Le seuil `kDoubtfulNoteThreshold = 0,55`
marque de 53 à 91 % des notes des reconstructions réelles : une marque qui porte
sur trois notes sur quatre ne dit plus par où commencer. Avant de la régler, il
faut savoir si la confiance VEUT DIRE quelque chose. Le corpus synthétique le sait :
chaque morceau porte sa `verite.json`, où chaque partie donne ses notes exactes.

LA RÈGLE DE CORRESPONDANCE, écrite avant de compter, et la MÊME pour toutes les
tranches de confiance : une note transcrite (hauteur h, début t) est JUSTE s'il
existe, dans la vérité du morceau, une note de hauteur h commençant à moins de
`TOLERANCE` secondes de t. C'est une règle indulgente — elle ignore la durée, la
vélocité et la partie d'origine —, et c'est voulu : on ne mesure pas ici la
qualité de la transcription, mais si la confiance SÉPARE. Une règle indulgente qui
sépare prouve autant qu'une règle sévère, et se discute moins.

CE QUI EST DIT : la part de notes justes par tranche de confiance, le compte de
chaque tranche, et la part globale. Si la part juste ne monte pas avec la
confiance, le seuil n'est pas à régler : la mesure est à refaire.
"""
from __future__ import annotations

import json
import os
import sys
from bisect import bisect_left
from pathlib import Path

# La tolérance se règle (VSM_TOLERANCE=0.025) : le RANG des tranches doit tenir
# à 25 comme à 100 ms, sans quoi la conclusion tiendrait à un réglage.
TOLERANCE = float(os.environ.get("VSM_TOLERANCE", "0.05"))   # secondes


def verite_du_morceau(dossier: Path) -> dict[int, list[float]]:
    """Les débuts (en secondes) de chaque hauteur, triés, toutes parties confondues."""
    fichier = dossier / "verite.json"
    if not fichier.is_file():
        return {}
    v = json.loads(fichier.read_text(encoding="utf-8"))
    debuts: dict[int, list[float]] = {}
    for partie in v.get("parties", []):
        for note in partie.get("notes", []):
            hauteur, _velocite, debut = int(note[0]), note[1], float(note[2])
            debuts.setdefault(hauteur, []).append(debut)
    for liste in debuts.values():
        liste.sort()
    return debuts


def juste(debuts: dict[int, list[float]], hauteur: int, debut: float) -> bool:
    liste = debuts.get(hauteur)
    if not liste:
        return False
    i = bisect_left(liste, debut)
    for j in (i - 1, i):
        if 0 <= j < len(liste) and abs(liste[j] - debut) <= TOLERANCE:
            return True
    return False


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 2
    lot = Path(argv[1])
    source = Path(argv[2]) if len(argv) > 2 else Path("reconstruction/travail/s1-sec")
    tranches = [(0.0, 0.35), (0.35, 0.45), (0.45, 0.55), (0.55, 0.65), (0.65, 0.80), (0.80, 1.01)]
    comptes = {t: [0, 0] for t in tranches}   # [notes, justes]
    morceaux = 0
    for dossier in sorted(lot.glob("morceau-*")):
        rapport = dossier / "course" / "rapport.json"
        if not rapport.is_file():
            continue
        debuts = verite_du_morceau(source / dossier.name)
        if not debuts:
            print(f"  {dossier.name} : aucune vérité, ignoré")
            continue
        morceaux += 1
        r = json.loads(rapport.read_text(encoding="utf-8"))
        for stem in r.get("stems", []):
            for n in stem.get("noteConfidence", []) or []:
                c = float(n["confidence"])
                for t in tranches:
                    if t[0] <= c < t[1]:
                        comptes[t][0] += 1
                        comptes[t][1] += 1 if juste(debuts, int(n["note"]), float(n["start"])) else 0
                        break
    if morceaux == 0:
        print("aucun morceau mesurable")
        return 1
    print(f"{morceaux} morceau(x), tolérance {TOLERANCE * 1000:.0f} ms, règle indulgente "
          f"(hauteur + début, durée et partie ignorées)")
    print(f"{'confiance':>14}  {'notes':>8}  {'justes':>8}  {'part':>6}")
    total = [0, 0]
    for t in tranches:
        notes, justes = comptes[t]
        total[0] += notes
        total[1] += justes
        part = f"{100 * justes / notes:5.1f}%" if notes else "    --"
        print(f"  [{t[0]:.2f} ; {t[1]:.2f})  {notes:8d}  {justes:8d}  {part}")
    part = f"{100 * total[1] / total[0]:5.1f}%" if total[0] else "    --"
    print(f"{'toutes':>14}  {total[0]:8d}  {total[1]:8d}  {part}")
    # LE REVERS : combien de notes VRAIES n'ont aucune note transcrite en face.
    # Sans lui, « 44 % des notes transcrites sont justes » ne dit pas si la chaîne
    # INVENTE ou si elle OUBLIE -- deux défauts opposés, deux remèdes opposés.
    vraies = retrouvees = 0
    for dossier in sorted(lot.glob("morceau-*")):
        rapport = dossier / "course" / "rapport.json"
        if not rapport.is_file():
            continue
        debuts = verite_du_morceau(source / dossier.name)
        if not debuts:
            continue
        transcrites: dict[int, list[float]] = {}
        r = json.loads(rapport.read_text(encoding="utf-8"))
        for stem in r.get("stems", []):
            for n in stem.get("noteConfidence", []) or []:
                transcrites.setdefault(int(n["note"]), []).append(float(n["start"]))
        for liste in transcrites.values():
            liste.sort()
        for hauteur, liste in debuts.items():
            for debut in liste:
                vraies += 1
                retrouvees += 1 if juste(transcrites, hauteur, debut) else 0
    if vraies:
        print(f"{'vraies':>14}  {vraies:8d}  {retrouvees:8d}  "
              f"{100 * retrouvees / vraies:5.1f}%   (rappel : les notes du morceau retrouvées)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
