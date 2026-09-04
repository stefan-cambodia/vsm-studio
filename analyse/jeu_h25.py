#!/usr/bin/env python3
"""H25 APPRIS — construire le jeu depuis les TRANSCRIPTIONS de la chaîne.

POURQUOI PAS DEPUIS LA VÉRITÉ, et c'est mesuré et non supposé : sur les notes
VRAIES de quatre-vingts morceaux générés, `registres_par_vides` ne pose la
question que **7 fois** — les parties se recouvrent en hauteur, donc la densité
n'a pas de creux. Sur la TRANSCRIPTION du premier de ces mêmes morceaux, elle
l'a posée du premier coup (trois registres). Apprendre sur la vérité, ce serait
apprendre sur des paires que la chaîne ne voit jamais.

CE QUE CE SCRIPT LIT : un dossier de courses front-end (`--sans-recherche`,
budget minimal), une par morceau, et le `verite.json` du morceau d'origine.

L'ÉTIQUETTE VIENT DE L'APPARIEMENT. Chaque note transcrite est rapprochée de
la note vraie la plus proche (±1 demi-ton, ±50 ms — la fonction du banc, pas
une seconde écrite ici), et hérite de la PARTIE d'où cette note vient. Un
registre appartient à la partie qui y pèse le plus. Deux registres voisins
dominés par la MÊME partie sont un seul instrument : c'est la question d'H25,
et sa réponse est connue par construction.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyzer.vsm_banc import apparier  # noqa: E402
from analyzer.vsm_deux_mains import descripteurs  # noqa: E402


def _notes_du_projet(projet: Path) -> Dict[str, List[List[float]]]:
    """Les notes transcrites, par nom de piste, lues dans le MIDI du projet."""
    import mido
    document = json.loads((projet / "project.json").read_text(encoding="utf-8"))
    chemin = projet / "midi" / "arrangement.mid"
    if not chemin.is_file():
        return {}
    fichier = mido.MidiFile(str(chemin))
    ppq = fichier.ticks_per_beat or 480
    # Le tempo du projet : les notes du banc sont en SECONDES.
    tempo = 500000
    for piste in fichier.tracks:
        for msg in piste:
            if msg.type == "set_tempo":
                tempo = msg.tempo
                break
    par_piste: Dict[str, List[List[float]]] = {}
    noms = [t.get("name", "") for t in document.get("tracks", [])]
    for index, piste in enumerate(fichier.tracks):
        nom = None
        for msg in piste:
            if msg.type == "track_name":
                nom = msg.name
                break
        if nom is None:
            nom = noms[index] if index < len(noms) else f"piste {index}"
        temps = 0
        ouvertes: Dict[int, Tuple[float, int]] = {}
        notes: List[List[float]] = []
        for msg in piste:
            temps += msg.time
            secondes = temps / ppq * (tempo / 1_000_000.0)
            if msg.type == "note_on" and msg.velocity > 0:
                ouvertes[msg.note] = (secondes, msg.velocity)
            elif msg.type in ("note_off",) or (msg.type == "note_on" and msg.velocity == 0):
                depart = ouvertes.pop(msg.note, None)
                if depart is not None:
                    notes.append([msg.note, depart[1], depart[0], max(1e-3, secondes - depart[0])])
        if notes:
            par_piste.setdefault(nom, []).extend(notes)
    return par_piste


def _parties_other(verite: dict) -> List[Tuple[int, List[List[float]]]]:
    sortie: List[Tuple[int, List[List[float]]]] = []
    for i, partie in enumerate(verite.get("parties", [])):
        if str(partie.get("role", "")) in ("basse", "batterie"):
            continue
        notes = partie.get("notes") or []
        if notes:
            sortie.append((i, [list(n) for n in notes]))
    return sortie


def paires_du_morceau(course: Path, verite_chemin: Path) -> List[Tuple[np.ndarray, int, str]]:
    """Les paires de registres voisins de `other`, avec leur étiquette."""
    verite = json.loads(verite_chemin.read_text(encoding="utf-8"))
    parties = _parties_other(verite)
    if not parties:
        return []
    vraies: List[List[float]] = []
    origine: List[int] = []
    for indice, notes in parties:
        for n in notes:
            vraies.append(n)
            origine.append(indice)

    pistes = _notes_du_projet(course)
    # LES REGISTRES SONT LES PISTES NOMMÉES « other · … » : c'est le découpage
    # par les vides, tel que la chaîne l'a écrit.
    # LE NOM DU MIDI N'EST PAS CELUI DU PROJET : l'écriture SMF assainit le
    # point médian de « other · F3-B4 » en tiret. On accepte les deux plutôt
    # que d'aller réparer un écrivain MIDI qui a raison d'être prudent.
    registres = [(nom, notes) for nom, notes in pistes.items()
                 if nom.startswith("other · ") or nom.startswith("other - ")]
    if len(registres) < 2:
        return []

    def hauteur_mediane(item) -> float:
        return float(np.median([n[0] for n in item[1]]))
    registres.sort(key=hauteur_mediane, reverse=True)   # de l'aigu au grave

    def dominante(notes: Sequence[Sequence[float]]) -> Optional[int]:
        paires = apparier(vraies, list(notes))
        poids: Dict[int, float] = {}
        for i, j in paires:
            poids[origine[i]] = poids.get(origine[i], 0.0) + max(1e-6, float(vraies[i][3]))
        if not poids:
            return None
        return max(poids, key=lambda k: (poids[k], -k))

    exemples: List[Tuple[np.ndarray, int, str]] = []
    for k in range(len(registres) - 1):
        (nom_h, haut), (nom_b, bas) = registres[k], registres[k + 1]
        da, db = dominante(haut), dominante(bas)
        if da is None or db is None:
            continue
        etiquette = 1 if da == db else 0
        exemples.append((descripteurs(bas, haut).vecteur(), etiquette,
                          f"{course.name} : {nom_b} / {nom_h} -> {'un seul' if etiquette else 'deux'}"))
    return exemples


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--courses", type=Path, required=True,
                    help="dossier des courses front-end (lot/morceau/)")
    ap.add_argument("--lots", type=Path, required=True, help="dossier des lots générés")
    ap.add_argument("--sortie", type=Path, required=True, help="jeu écrit ici (.npz)")
    ap.add_argument("--bavard", action="store_true")
    args = ap.parse_args()

    X: List[np.ndarray] = []
    y: List[int] = []
    morceaux = 0
    sans_paire = 0
    for course in sorted(args.courses.glob("*/morceau-*")):
        if not (course / "project.json").is_file():
            continue
        morceaux += 1
        verite = args.lots / course.parent.name / course.name / "verite.json"
        if not verite.is_file():
            continue
        paires = paires_du_morceau(course, verite)
        if not paires:
            sans_paire += 1
            continue
        for vecteur, etiquette, phrase in paires:
            X.append(vecteur)
            y.append(etiquette)
            if args.bavard:
                print("  " + phrase)

    print(f"{morceaux} courses lues, {sans_paire} sans paire, {len(X)} paires posées "
          f"({sum(y)} « un seul », {len(y) - sum(y)} « deux »)")
    if X:
        args.sortie.parent.mkdir(parents=True, exist_ok=True)
        np.savez(args.sortie, X=np.vstack(X), y=np.asarray(y, dtype=int))
        print(f"jeu écrit : {args.sortie}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
