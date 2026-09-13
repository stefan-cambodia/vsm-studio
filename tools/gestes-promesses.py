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

LES GESTES DU CLIP AUDIO Y SONT ENTRÉS LE 13/09/2026, et il a fallu un défaut pour
y penser. D239 les avait mesurés à la main, sans les garder : « Découper aux
transitoires » a donc pu se casser en silence — il annonçait quatre attaques et ne
coupait rien (D262), puis coupait aux mauvais endroits (D263) — sans qu'aucune
garde ne s'en aperçoive. Les six cas ajoutés ici lisent le `project.json` écrit, où
un champ de clip n'apparaît QUE s'il s'écarte de son défaut : une promesse tenue
s'y voit, une promesse morte aussi.

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


def clip_audio_ecrit(dossier: Path) -> dict:
    """Le premier clip de la première piste AUDIO du projet écrit.

    Les champs d'un clip ne sont écrits que s'ils s'écartent de leur défaut
    (`muted`, `reversed`, `gain`, `pitch`…) : leur PRÉSENCE est donc déjà la
    moitié de la preuve, et leur absence l'autre moitié.
    """
    fichier = dossier / "project.json"
    if not fichier.is_file():
        return {}
    d = json.loads(fichier.read_text(encoding="utf-8"))
    for piste in d.get("tracks", []):
        if piste.get("kind") == "audio" and piste.get("clips"):
            return piste["clips"][0]
    return {}


def clips_audio_ecrits(dossier: Path) -> list:
    fichier = dossier / "project.json"
    if not fichier.is_file():
        return []
    d = json.loads(fichier.read_text(encoding="utf-8"))
    for piste in d.get("tracks", []):
        if piste.get("kind") == "audio" and piste.get("clips"):
            return piste["clips"]
    return []


def ecrire_un_son(chemin: Path) -> None:
    """Deux secondes de son ENGENDRÉES ICI : une garde qui dépendrait d'un fichier
    posé à côté d'elle se tairait le jour où il disparaît."""
    import math
    import struct
    import wave

    taux, duree = 44100, 2.0
    trames = int(taux * duree)
    with wave.open(str(chemin), "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(taux)
        f.writeframes(b"".join(
            struct.pack("<hh", v, v) for v in
            (int(12000 * math.sin(2 * math.pi * 220.0 * i / taux)) for i in range(trames))))


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

    # --- LES GESTES DU CLIP AUDIO (13/09/2026) ------------------------------
    #
    # Un projet d'essai est fabriqué ici : un son de deux secondes importé sur une
    # piste neuve. `cdl` n'a aucune piste audio, et emprunter un projet qui en a
    # rendrait la garde dépendante de son contenu.
    son = brouillon / "essai.wav"
    ecrire_un_son(son)
    projet_audio = brouillon / "projet-audio"
    shutil.copytree(PROJET, projet_audio)

    def geste_audio(nom: str, libelle: str = "") -> Path:
        """Importe le son, applique (ou non) UNE entrée du menu du clip, écrit."""
        return course(brouillon, nom, projet_audio,
                      VSM_IMPORT_AUDIO=str(son),
                      VSM_MENU_CONTEXTE=(f"clip-audio-tous:{libelle}" if libelle else ""))

    # LE TÉMOIN D'ABORD : sans geste, le clip importé ne porte aucun des champs
    # que les gestes suivants doivent poser. Sans lui, « muted est là » ne
    # prouverait pas que c'est le geste qui l'a mis.
    t = clip_audio_ecrit(geste_audio("audio-temoin"))
    propre = bool(t) and not any(k in t for k in ("muted", "reversed", "gain", "pitch"))
    verdict("témoin audio : un clip neuf est nu", propre,
            f"champs : {sorted(t)}" if t else "AUCUN clip audio écrit")

    c = clip_audio_ecrit(geste_audio("audio-muet", "Rendre muet"))
    verdict("Rendre muet : le clip porte muted", c.get("muted") is True,
            f"muted = {c.get('muted')}")

    c = clip_audio_ecrit(geste_audio("audio-envers", "À l'envers"))
    verdict("À l'envers : le clip porte reversed", c.get("reversed") is True,
            f"reversed = {c.get('reversed')}")

    # -3 dB, c'est un gain de 10^(-3/20) = 0,708. On vérifie le CHIFFRE, pas la
    # seule présence du champ : un geste qui écrirait « gain: 1 » serait muet.
    c = clip_audio_ecrit(geste_audio("audio-3db", "-3 dB"))
    gain = float(c.get("gain", 1.0))
    verdict("-3 dB : le gain vaut 0,708", abs(gain - 0.7079) < 0.005, f"gain = {gain:.4f}")

    c = clip_audio_ecrit(geste_audio("audio-normaliser", "Normaliser (gain = 1 / crête)"))
    gain = float(c.get("gain", 1.0))
    verdict("Normaliser : le gain quitte 1", abs(gain - 1.0) > 1e-6, f"gain = {gain:.4f}")

    # « 2 fois » veut dire RÉPÉTER DEUX FOIS, donc deux copies EN PLUS de
    # l'original : trois clips, et non deux. Vérifié dans le code avant d'être
    # écrit ici — `repeatClips(track, selection, count, …)` pose `count` copies,
    # et `repeatsUntilLoopEnd` compte de même. Mon premier attendu disait deux et
    # la garde criait au défaut : c'était l'attendu qui lisait mal le libellé.
    #
    # Et compter les clips ne suffit pas : trois clips posés au même endroit ne
    # répètent rien. On vérifie qu'ils sont BOUT À BOUT.
    clips = clips_audio_ecrits(geste_audio("audio-2fois", "2 fois"))
    bout_a_bout = (len(clips) == 3 and all(
        clips[i + 1]["start"] == clips[i]["start"] + clips[i]["length"] for i in range(2)))
    verdict("2 fois : trois clips bout à bout", bout_a_bout,
            f"{len(clips)} clip(s) : " + ", ".join(f"{c['start']}+{c['length']}" for c in clips[:3]))

    shutil.rmtree(brouillon, ignore_errors=True)
    print(f"=== {rates} promesse(s) rompue(s) ===")
    return 1 if rates else 0


if __name__ == "__main__":
    raise SystemExit(main())
