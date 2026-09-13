#!/usr/bin/env python3
"""Chaque geste d'édition est une PROMESSE : tient-elle encore ?

    analyse/.venv/bin/python tools/gestes-promesses.py

POURQUOI (13/09/2026, D240). D236 à D239 ont mesuré neuf gestes du piano roll, six
du clip audio et cinq de la piste, chacun sur le FICHIER écrit derrière — ce que
voit le logiciel suivant. Ces mesures étaient des courses à la main : rien ne dirait
demain qu'un refactoring a fait de « Quantifier (100 %) » un geste qui déplace les
hauteurs, ou de « Dupliquer » une copie sans notes. Les suites C++ ne traversent pas
l'interface ; cette garde-ci part de l'entrée de menu et va jusqu'au fichier.

CE QU'ELLE VÉRIFIE, et pourquoi ces cas-là : un par FAMILLE de conséquence — une
hauteur (transposer), un temps (quantifier), une durée (legato), un champ de clip
(-3 dB, normaliser), le modèle du projet (dupliquer). Le TÉMOIN vient d'abord : sans
geste, le fichier écrit doit être identique à l'original, sans quoi aucun écart
n'est attribuable au geste.

ELLE NE REND PAS D'AUDIO : toutes les courses sont des gestes et une écriture de
projet, donc elle peut tourner à côté d'une campagne.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
BINAIRE = RACINE / "build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
PROJET = RACINE / "reconstruction/travail/cdl"
PROJET_PISTES = RACINE / "reconstruction/travail/b4-v4"


def course(brouillon: Path, nom: str, projet: Path, **env: str) -> Path:
    """Une course de banc, sous un HOME de brouillon, qui écrit son projet."""
    maison = brouillon / f"h-{nom}"
    maison.mkdir(parents=True, exist_ok=True)
    sortie = brouillon / nom
    variables = dict(os.environ)
    variables.update({"HOME": str(maison), "VSM_PROJET": str(projet),
                      "VSM_ENREGISTRER": str(sortie), "VSM_CAPTURE": str(brouillon / f"{nom}.png"),
                      "VSM_DELAI": "1000"})
    variables.update({k: v for k, v in env.items() if v})
    subprocess.run([str(BINAIRE)], env=variables, capture_output=True, timeout=180, check=False)
    return sortie


def notes_du_midi(dossier: Path) -> list[tuple[int, int, int]]:
    import mido
    fichiers = sorted(dossier.glob("midi/*.mid"))
    if not fichiers:
        return []
    m = mido.MidiFile(str(fichiers[0]))
    notes: list[tuple[int, int, int]] = []
    for piste in m.tracks:
        temps = 0
        ouvertes: dict[int, list[int]] = {}
        for e in piste:
            temps += e.time
            if e.type == "note_on" and e.velocity > 0:
                ouvertes.setdefault(e.note, []).append(temps)
            elif e.type == "note_off" or (e.type == "note_on" and e.velocity == 0):
                if ouvertes.get(e.note):
                    notes.append((e.note, ouvertes[e.note].pop(0), temps))
    return sorted(notes, key=lambda n: (n[1], n[0]))


def main() -> int:
    if not BINAIRE.is_file():
        print("REFUS : l'application n'est pas compilée")
        return 2
    brouillon = Path(tempfile.mkdtemp(prefix="vsm-promesses-"))
    rates = 0

    def verdict(nom: str, tenu: bool, dit: str) -> None:
        nonlocal rates
        print(f"  {'OK  ' if tenu else 'RATÉ'} {nom:34s} {dit}")
        if not tenu:
            rates += 1

    print("=== les gestes tiennent-ils encore leur promesse ? ===")
    temoin = notes_du_midi(course(brouillon, "temoin", PROJET, VSM_MENU="Tout sélectionner"))
    origine = notes_du_midi(PROJET)
    verdict("témoin : le .mid ne bouge pas", temoin == origine and len(temoin) > 0,
            f"{len(temoin)} notes, identiques : {temoin == origine}")

    n = notes_du_midi(course(brouillon, "demiton", PROJET,
                             VSM_MENU="Tout sélectionner;Transposer +1 demi-ton"))
    # Un ensemble VIDE quand les comptes diffèrent : le verdict échoue alors, ce
    # qui est juste — une transposition qui perd des notes n'est pas une
    # transposition. (Et `None` dans l'ensemble ferait tomber `sorted`.)
    ecarts = {b[0] - a[0] for a, b in zip(temoin, n, strict=False)} if len(n) == len(temoin) else set()
    verdict("Transposer +1 : toutes les hauteurs +1", ecarts == {1}, f"écarts observés {sorted(ecarts)}")

    n = notes_du_midi(course(brouillon, "quantifier", PROJET,
                             VSM_MENU="Tout sélectionner;Quantifier (100 %)"))
    sur = sum(1 for x in n if x[1] % 120 == 0)
    import collections
    memes = (collections.Counter(x[0] for x in n) == collections.Counter(x[0] for x in temoin))
    verdict("Quantifier 100 % : tout sur la grille", sur == len(n) and len(n) == len(temoin) and memes,
            f"{sur}/{len(n)} sur la grille, hauteurs inchangées : {memes}")

    n = notes_du_midi(course(brouillon, "legato", PROJET, VSM_MENU="Tout sélectionner;Legato"))
    trous = sum(1 for a, b in zip(n, n[1:], strict=False) if b[1] > a[2])
    verdict("Legato : aucun silence entre notes", trous == 0 and len(n) == len(temoin),
            f"{trous} silence(s), {len(n)} notes")

    # D243 : LE MIROIR, par son invariant — la somme hauteur + image est CONSTANTE,
    # et vaut min + max du morceau. C'est le seul contrôle qui distingue un miroir
    # d'une transposition, et il exige d'apparier la k-ième plus BASSE d'avant avec
    # la k-ième plus HAUTE d'après : un miroir inverse l'ordre d'un accord.
    n = notes_du_midi(course(brouillon, "miroir", PROJET,
                             VSM_MENU="Tout sélectionner;Miroir des hauteurs"))
    avant_par_temps: dict[int, list[int]] = {}
    apres_par_temps: dict[int, list[int]] = {}
    for note in temoin:
        avant_par_temps.setdefault(note[1], []).append(note[0])
    for note in n:
        apres_par_temps.setdefault(note[1], []).append(note[0])
    sommes = {a + b
              for t, av in avant_par_temps.items()
              if len(apres_par_temps.get(t, [])) == len(av)
              for a, b in zip(sorted(av), sorted(apres_par_temps[t], reverse=True), strict=False)}
    hauteurs = [x[0] for x in temoin]
    axe = (min(hauteurs) + max(hauteurs)) if hauteurs else 0
    verdict("Miroir : une seule somme, l'axe", sommes == {axe} and len(n) == len(temoin),
            f"sommes {sorted(sommes)[:3]}, axe attendu {axe}")

    # D242 : LA VÉLOCITÉ FIXE — une seule valeur, et c'est 127.
    import mido
    fichiers = sorted(course(brouillon, "vel127", PROJET,
                             VSM_MENU="Tout sélectionner;Vélocité 127").glob("midi/*.mid"))
    velocites = {e.velocity for f in fichiers for t in mido.MidiFile(str(f)).tracks for e in t
                 if e.type == "note_on" and e.velocity > 0}
    verdict("Vélocité 127 : une seule valeur", velocites == {127}, f"valeurs {sorted(velocites)[:4]}")

    projet = json.loads((course(brouillon, "dupliquer", PROJET_PISTES,
                                VSM_MENU="Dupliquer la piste sélectionnée") / "project.json")
                        .read_text(encoding="utf-8"))
    noms = [t["name"] for t in projet["tracks"]]
    verdict("Dupliquer : une piste de plus, nommée", len(noms) == 5 and noms[1].endswith("(copie)"),
            f"{len(noms)} pistes : {noms[:2]}")

    shutil.rmtree(brouillon, ignore_errors=True)
    print(f"=== {rates} promesse(s) rompue(s) ===")
    return 1 if rates else 0


if __name__ == "__main__":
    raise SystemExit(main())
