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

CE QUE CET OUTIL NE PEUT PAS VOIR, ET QUI A FAIT ÉCRIRE TROIS FAUSSETÉS (D265,
13/09/2026). Il lit `stems[].noteConfidence` de `rapport.json`. **La BATTERIE n'y
figure pas** : elle a son propre chemin et son propre compte (`drums.hits`), et
aucun stem de percussion ne porte de liste de confiance. Les frappes vraies
étaient donc comptées ABSENTES, toutes, en silence — et comme elles sont les
SEULES notes brèves du corpus (1 865 sur 1 865 sous 150 ms), cela a produit
« 96,7 % des notes de moins de 150 ms sont ratées », qui ne mesurait que
l'aveuglement de l'instrument. Le rappel porte donc désormais sur les notes
MÉLODIQUES, et les frappes sont comptées à part, avec ce que `drums.hits`
annonce. Une mesure qui ne peut pas voir une chose le DIT au lieu de compter zéro.
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


def tenues_du_morceau(dossier: Path) -> list[tuple[float, float, int]]:
    """(début, fin, hauteur) de chaque note vraie — pour savoir ce qui SONNE.

    D232 : une règle qui exige un début exact compte comme fausses les notes
    RÉPÉTÉES d'une tenue. Une ligne de basse tenue, re-articulée par le
    transcripteur, joue la bonne note et tombait en faute : sur les basses les plus
    sûres, cela déplace le résultat de 19 % à 60 %. La colonne « tenues » compte
    juste toute note dont la hauteur SONNE à cet instant.
    """
    fichier = dossier / "verite.json"
    if not fichier.is_file():
        return []
    v = json.loads(fichier.read_text(encoding="utf-8"))
    tenues: list[tuple[float, float, int]] = []
    for partie in v.get("parties", []):
        for note in partie.get("notes", []):
            hauteur, debut, duree = int(note[0]), float(note[2]), float(note[3])
            tenues.append((debut, debut + duree, hauteur))
    tenues.sort()
    return tenues


def sonne(tenues: list[tuple[float, float, int]], hauteur: int, instant: float) -> bool:
    return any(d <= instant <= f and h == hauteur for d, f, h in tenues)


def juste(debuts: dict[int, list[float]], hauteur: int, debut: float) -> bool:
    liste = debuts.get(hauteur)
    if not liste:
        return False
    i = bisect_left(liste, debut)
    for j in (i - 1, i):
        if 0 <= j < len(liste) and abs(liste[j] - debut) <= TOLERANCE:
            return True
    return False


def categories_par_stem(lot: Path, source: Path) -> dict[str, dict[str, int]]:
    """D254 : ce que CHAQUE note transcrite est, rangée par stem.

    Quatre cas, et ils ne se confondent pas : la bonne hauteur (attaque exacte ou
    re-attaque d'une note tenue), l'octave (à ±12 ou ±24), une autre hauteur qui
    sonne, et la note INVENTÉE — rien de cette hauteur ne sonne à cet instant.
    « 27 % de hauteurs exactes » mettait ces quatre cas dans le même sac ; séparés,
    ils disent que la chaîne n'invente presque rien (0,4 à 1,2 %) et que l'octave
    coûte un cinquième à un quart des notes de chaque stem.
    """
    par: dict[str, dict[str, int]] = {}
    for dossier in sorted(lot.glob("morceau-*")):
        rapport = dossier / "course" / "rapport.json"
        if not rapport.is_file():
            continue
        debuts = verite_du_morceau(source / dossier.name)
        tenues = tenues_du_morceau(source / dossier.name)
        if not debuts:
            continue
        r = json.loads(rapport.read_text(encoding="utf-8"))
        for stem in r.get("stems", []):
            nom = stem.get("name", "?").split(" · ")[0].split(" - ")[0]
            c = par.setdefault(nom, {"total": 0, "bonne": 0, "octave": 0, "autre": 0, "inventee": 0})
            for n in stem.get("noteConfidence", []) or []:
                hauteur, instant = int(n["note"]), float(n["start"])
                c["total"] += 1
                if juste(debuts, hauteur, instant) or sonne(tenues, hauteur, instant):
                    c["bonne"] += 1
                elif (any(juste(debuts, hauteur + o, instant) for o in (12, -12, 24, -24))
                      or any(d <= instant <= f and (h - hauteur) % 12 == 0 for d, f, h in tenues)):
                    c["octave"] += 1
                elif any(d <= instant <= f for d, f, _ in tenues):
                    c["autre"] += 1
                else:
                    c["inventee"] += 1
    return par


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 2
    categories = "--categories" in argv
    argv = [a for a in argv if a != "--categories"]
    lot = Path(argv[1])
    source = Path(argv[2]) if len(argv) > 2 else Path("reconstruction/travail/s1-sec")
    if categories:
        par = categories_par_stem(lot, source)
        if not par:
            print("aucun morceau mesurable")
            return 1
        print(f"{'stem':10s} {'notes':>7} {'bonne hauteur':>14} {'octave':>8} {'autre':>8} {'inventée':>10}")
        for nom, stat in sorted(par.items(), key=lambda kv: -kv[1]["total"]):
            n_total = stat["total"]
            print(f"{nom:10s} {n_total:7d} {100 * stat['bonne'] / n_total:13.1f}%"
                  f" {100 * stat['octave'] / n_total:7.1f}% {100 * stat['autre'] / n_total:7.1f}%"
                  f" {100 * stat['inventee'] / n_total:9.1f}%")
        return 0
    tranches = [(0.0, 0.35), (0.35, 0.45), (0.45, 0.55), (0.55, 0.65), (0.65, 0.80), (0.80, 1.01)]
    comptes = {t: [0, 0, 0] for t in tranches}   # [notes, justes au début, + notes tenues]
    morceaux = 0
    for dossier in sorted(lot.glob("morceau-*")):
        rapport = dossier / "course" / "rapport.json"
        if not rapport.is_file():
            continue
        debuts = verite_du_morceau(source / dossier.name)
        if not debuts:
            print(f"  {dossier.name} : aucune vérité, ignoré")
            continue
        tenues = tenues_du_morceau(source / dossier.name)
        morceaux += 1
        r = json.loads(rapport.read_text(encoding="utf-8"))
        for stem in r.get("stems", []):
            for n in stem.get("noteConfidence", []) or []:
                c = float(n["confidence"])
                hauteur, instant = int(n["note"]), float(n["start"])
                for t in tranches:
                    if t[0] <= c < t[1]:
                        comptes[t][0] += 1
                        au_debut = juste(debuts, hauteur, instant)
                        comptes[t][1] += 1 if au_debut else 0
                        comptes[t][2] += 1 if (au_debut or sonne(tenues, hauteur, instant)) else 0
                        break
    if morceaux == 0:
        print("aucun morceau mesurable")
        return 1
    print(f"{morceaux} morceau(x), tolérance {TOLERANCE * 1000:.0f} ms, règle indulgente "
          f"(hauteur + début, durée et partie ignorées)")
    print(f"{'confiance':>14}  {'notes':>8}  {'justes':>8}  {'part':>6}  {'+ tenues':>8}")
    total = [0, 0, 0]
    for t in tranches:
        notes, justes, tenues_aussi = comptes[t]
        total[0] += notes
        total[1] += justes
        total[2] += tenues_aussi
        part = f"{100 * justes / notes:5.1f}%" if notes else "    --"
        avec = f"{100 * tenues_aussi / notes:7.1f}%" if notes else "      --"
        print(f"  [{t[0]:.2f} ; {t[1]:.2f})  {notes:8d}  {justes:8d}  {part}  {avec}")
    part = f"{100 * total[1] / total[0]:5.1f}%" if total[0] else "    --"
    avec = f"{100 * total[2] / total[0]:7.1f}%" if total[0] else "      --"
    print(f"{'toutes':>14}  {total[0]:8d}  {total[1]:8d}  {part}  {avec}")
    # LE REVERS : combien de notes VRAIES n'ont aucune note transcrite en face.
    # Sans lui, « 44 % des notes transcrites sont justes » ne dit pas si la chaîne
    # INVENTE ou si elle OUBLIE -- deux défauts opposés, deux remèdes opposés.
    vraies = retrouvees = 0
    frappes_vraies = frappes_ecrites = 0
    for dossier in sorted(lot.glob("morceau-*")):
        rapport = dossier / "course" / "rapport.json"
        if not rapport.is_file():
            continue
        fichier = source / dossier.name / "verite.json"
        if not fichier.is_file():
            continue
        v = json.loads(fichier.read_text(encoding="utf-8"))
        transcrites: dict[int, list[float]] = {}
        r = json.loads(rapport.read_text(encoding="utf-8"))
        for stem in r.get("stems", []):
            for n in stem.get("noteConfidence", []) or []:
                transcrites.setdefault(int(n["note"]), []).append(float(n["start"]))
        for liste in transcrites.values():
            liste.sort()
        frappes_ecrites += int((r.get("drums") or {}).get("hits", 0))
        # LES FRAPPES SE COMPTENT À PART : `noteConfidence` ne les porte pas, et
        # les mêler au rappel revient à mesurer l'instrument, pas la chaîne.
        for partie in v.get("parties", []):
            percussive = partie.get("role") == "batterie"
            for note in partie.get("notes", []):
                if percussive:
                    frappes_vraies += 1
                    continue
                vraies += 1
                retrouvees += 1 if juste(transcrites, int(note[0]), float(note[2])) else 0
    if vraies:
        print(f"{'mélodiques':>14}  {vraies:8d}  {retrouvees:8d}  "
              f"{100 * retrouvees / vraies:5.1f}%   (rappel des notes vraies NON percussives)")
    if frappes_vraies:
        print(f"{'frappes':>14}  {frappes_vraies:8d}  {frappes_ecrites:8d}  "
              f"{100 * frappes_ecrites / frappes_vraies:5.1f}%   "
              f"(comptées, pas appariées : drums.hits contre les frappes vraies)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
