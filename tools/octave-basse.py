#!/usr/bin/env python3
"""D270 : relire l'octave des notes de basse dans le MÉLANGE, qui l'a gardée.

    analyse/.venv/bin/python tools/octave-basse.py reconstruction/travail/r1f-13sep

POURQUOI (D269, 13/09/2026). La séparation rend une basse passée au filtre : la
part de son énergie au-dessus de 300 Hz tombe de 25,5 % dans la partie jouée à
1,7 % dans le stem. Ce sont exactement les partielles qui permettent de trancher
une octave, et le transcripteur, privé d'elles, choisit bas — 6,9 fois plus
souvent que haut, quand sur le stem VRAI il choisit haut (0,3×). Corriger
l'estimateur ne rendra pas ces harmoniques.

LE MÉLANGE, LUI, LES A TOUJOURS : il n'a jamais été filtré. Ce banc relit donc
chaque note écrite par le stem `bass` dans le mélange d'origine.

LE TEST, et c'est le seul qui distingue les deux hypothèses. Pour une note écrite
à la hauteur h, les candidats sont h et h+12. Ils ne se séparent PAS par la
partielle à 2·f(h) : elle appartient aux deux séries. Ils se séparent par les
harmoniques IMPAIRS de h — 3·f(h), 5·f(h) —, qui n'existent pas dans la série de
h+12. Le rapport « énergie aux rangs impairs / énergie aux rangs pairs » décide.

CE QUI EST DIT : les quatre chiffres de l'attendu, témoin compris, et le CONTRÔLE
qui manque à toute mesure de correction — combien de notes DÉJÀ JUSTES la
correction casse. Un correcteur qui gagne cent notes et en perd cent n'a rien
fait.
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

TOLERANCE = 0.06     # secondes, pour apparier une note écrite à une note vraie
SEUIL = float(os.environ.get("VSM_SEUIL", "0.5"))   # rapport impairs/pairs sous lequel la note est jugée une octave trop bas


def frequence(note: int) -> float:
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def energie_aux_rangs(x: np.ndarray, sr: float, f0: float, rangs: list[int]) -> float:
    """L'énergie du signal aux rangs demandés de f0 (Goertzel, sur tout l'extrait)."""
    total = 0.0
    for rang in rangs:
        f = f0 * rang
        if f >= sr / 2:
            continue
        w = 2.0 * np.pi * f / sr
        coeff = 2.0 * np.cos(w)
        s1 = s2 = 0.0
        for v in x:
            s0 = v + coeff * s1 - s2
            s2, s1 = s1, s0
        total += max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)
    return total


def une_octave_trop_bas(x: np.ndarray, sr: float, note: int) -> bool:
    """Vrai si le mélange ne porte PAS les harmoniques impairs de `note`."""
    f0 = frequence(note)
    if f0 < 20.0:
        return False
    impairs = energie_aux_rangs(x, sr, f0, [1, 3, 5])
    pairs = energie_aux_rangs(x, sr, f0, [2, 4, 6])
    if pairs <= 0.0:
        return False
    return (impairs / pairs) < SEUIL


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 2
    lot = Path(argv[1])
    source = Path(argv[2]) if len(argv) > 2 else Path("reconstruction/travail/s1-sec")

    t_juste = t_bas = t_haut = 0            # témoin : ce que la chaîne écrit
    c_juste = c_bas = c_haut = 0            # après relecture
    casse = corrige = 0                     # le contrôle : justes cassées, basses corrigées
    for dossier in sorted(lot.glob("morceau-*")):
        rapport = dossier / "course" / "rapport.json"
        melange = source / dossier.name / "morceau.wav"
        verite = source / dossier.name / "verite.json"
        if not (rapport.is_file() and melange.is_file() and verite.is_file()):
            continue
        v = json.loads(verite.read_text(encoding="utf-8"))
        r = json.loads(rapport.read_text(encoding="utf-8"))
        vraies = sorted((float(n[2]), int(n[0])) for p in v.get("parties", [])
                        if p.get("role") != "batterie" for n in p.get("notes", []))
        son, sr = sf.read(str(melange), always_2d=True)
        son = son.mean(axis=1)

        for stem in r.get("stems", []):
            if not stem.get("name", "").startswith("bass"):
                continue
            for n in stem.get("noteConfidence", []) or []:
                h, t = int(n["note"]), float(n["start"])
                proches = [hv for tv, hv in vraies if abs(tv - t) <= TOLERANCE]
                if not proches:
                    continue
                # L'ÉTAT AVANT : ce que la chaîne a écrit.
                if h in proches:
                    avant = "juste"
                    t_juste += 1
                elif any(hv - h in (12, 24) for hv in proches):
                    avant = "bas"
                    t_bas += 1
                elif any(hv - h in (-12, -24) for hv in proches):
                    avant = "haut"
                    t_haut += 1
                else:
                    continue
                # LA RELECTURE : sur la fenêtre de la note, dans le mélange.
                i0 = int(t * sr)
                i1 = min(len(son), i0 + int(0.25 * sr))
                if i1 - i0 < 512:
                    corrige_h = h
                else:
                    corrige_h = h + 12 if une_octave_trop_bas(son[i0:i1], float(sr), h) else h
                # L'ÉTAT APRÈS.
                if corrige_h in proches:
                    c_juste += 1
                elif any(hv - corrige_h in (12, 24) for hv in proches):
                    c_bas += 1
                elif any(hv - corrige_h in (-12, -24) for hv in proches):
                    c_haut += 1
                if avant == "juste" and corrige_h not in proches:
                    casse += 1
                if avant == "bas" and corrige_h in proches:
                    corrige += 1

    total = t_juste + t_bas + t_haut
    if total == 0:
        print("aucune note de basse appariable")
        return 1
    print(f"notes du stem « bass » appariées à une note vraie : {total}\n")
    print(f"{'':22s} {'justes':>8} {'8ve bas':>8} {'8ve haut':>9} {'bonne hauteur':>14}")
    print(f"{'TÉMOIN (la chaîne)':22s} {t_juste:8d} {t_bas:8d} {t_haut:9d} "
          f"{100 * t_juste / total:13.1f}%")
    print(f"{'après relecture':22s} {c_juste:8d} {c_bas:8d} {c_haut:9d} "
          f"{100 * c_juste / total:13.1f}%")
    print(f"\nLE CONTRÔLE : {corrige} note(s) une octave trop bas corrigée(s), "
          f"{casse} note(s) déjà juste CASSÉE(S)")
    if t_juste:
        print(f"  soit {100 * casse / t_juste:.1f} % des justes cassées "
              f"(l'attendu permet 5 %)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
