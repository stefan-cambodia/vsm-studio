#!/usr/bin/env python3
"""D262/D263 : « Découper aux transitoires » coupe-t-il, et AUX BONS ENDROITS ?

    python3 tools/coupe-aux-transitoires.py

POURQUOI (13/09/2026). Le geste a échoué deux fois de suite, et les deux
échecs étaient muets pour qui lisait le résumé :

  D262 — il annonçait « 4 attaque(s) », puis « 0 coupe(s) ». Le clip couvrant
    était bien trouvé, mais son identifiant valait 0 : tout clip né pendant la
    séance gardait `id == 0` (`assignClipIds()` ne se rejoue qu'au chargement),
    et `if (couvrant == 0)` lisait ce 0 comme « aucun clip ».
  D263 — une fois D262 corrigé, il annonçait « 4 coupe(s) » et en posait deux
    au mauvais endroit : `snapCutToZeroCrossing` rajoutait `sourceStartSeconds`
    à une trame DÉJÀ absolue, déplaçant la coupe de la largeur de la fenêtre du
    clip. Sur le fichier d'essai, la deuxième coupe tombait à 0,894 s au lieu de
    0,697 s et la troisième à 2,091 s au lieu de 1,197 s. Invisible tant qu'un
    clip commence à 0 dans son fichier — c'est-à-dire jusqu'à la première coupe.

CE QUI EST VÉRIFIÉ, et pourquoi ainsi. Le fichier d'essai porte quatre frappes
à 0,2 / 0,7 / 1,2 / 1,7 s, et il est ENGENDRÉ ICI : une garde qui dépendrait
d'un fichier posé à côté d'elle se tairait le jour où il disparaît. On ne
compare pas les coupes à des ticks écrits en dur — ils dépendraient du tempo du
projet d'essai — mais on vérifie que le rapport `tick / seconde` est le MÊME
pour les quatre coupes. C'est exactement ce que le défaut D263 cassait, et cela
tient à n'importe quel tempo.
"""
from __future__ import annotations

import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
BINAIRE = RACINE / "build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
PROJET = RACINE / "reconstruction/travail/cdl"

ATTAQUES = (0.2, 0.7, 1.2, 1.7)     # secondes
DUREE = 2.2                          # secondes
TAUX = 48000
# La frappe : une sinusoïde de 220 Hz qui décroît en 60 ms. Assez brève pour que
# la détection d'attaques la voie seule, assez forte pour qu'elle la voie.
FRAPPE = 0.06


def ecrire_quatre_frappes(chemin: Path) -> None:
    trames = int(DUREE * TAUX)
    echantillons = [0.0] * trames
    for attaque in ATTAQUES:
        debut = int(attaque * TAUX)
        for i in range(int(FRAPPE * TAUX)):
            if debut + i >= trames:
                break
            enveloppe = math.exp(-8.0 * i / (FRAPPE * TAUX))
            echantillons[debut + i] += 0.8 * enveloppe * math.sin(2 * math.pi * 220.0 * i / TAUX)
    with wave.open(str(chemin), "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(TAUX)
        f.writeframes(b"".join(struct.pack("<hh", int(v * 32000), int(v * 32000)) for v in echantillons))


def main() -> int:
    if not BINAIRE.is_file():
        print("REFUS : l'application n'est pas compilée — rien n'a été mesuré")
        return 2
    with tempfile.TemporaryDirectory(prefix="vsm-transitoires-") as tmp:
        travail = Path(tmp)
        wav = travail / "quatre-frappes.wav"
        ecrire_quatre_frappes(wav)
        projet = travail / "projet"
        shutil.copytree(PROJET, projet)
        maison = travail / "maison"
        maison.mkdir()
        env = dict(os.environ)
        env.update({
            "HOME": str(maison),                        # D77 : jamais le HOME de l'utilisateur
            "VSM_PROJET": str(projet),
            "VSM_IMPORT_AUDIO": str(wav),
            "VSM_CLIPS": "1",
            "VSM_CAPTURE": str(travail / "ecran.png"),  # ce qui fait QUITTER l'application
            "VSM_MENU_CONTEXTE": "clip-audio:Découper aux transitoires (clips audio choisis)",
        })
        try:
            r = subprocess.run([str(BINAIRE)], env=env, cwd=str(RACINE), timeout=180,
                               capture_output=True, text=True, errors="replace")
        except subprocess.TimeoutExpired:
            print("RATÉ : l'application n'a pas rendu la main en 180 s")
            return 1
        journal = r.stderr

    # Ce que le geste DIT avoir trouvé, et où il DIT avoir coupé.
    trouvees = re.search(r"(\d+) attaque\(s\) à ([\d., ]+) s", journal)
    coupes = re.findall(r"attaque à ([\d.]+) s → clip #\d+, coupe au tick (\d+)", journal)
    if trouvees is None:
        print("RATÉ : le geste n'a annoncé aucune détection d'attaques")
        print(journal[-1500:])
        return 1
    print(f"attaques détectées : {trouvees.group(1)} à {trouvees.group(2)} s")
    if len(coupes) != len(ATTAQUES):
        print(f"RATÉ : {len(coupes)} coupe(s) pour {len(ATTAQUES)} attaque(s) — D262")
        print(journal[-1500:])
        return 1

    # LE RAPPORT tick/seconde, le même pour les quatre coupes : c'est ce que
    # D263 cassait, et cela ne dépend pas du tempo du projet d'essai.
    rapports = [int(tick) / float(sec) for sec, tick in coupes]
    ecart = max(rapports) - min(rapports)
    for (sec, tick), rap in zip(coupes, rapports):
        print(f"  coupe à {sec} s → tick {tick}  ({rap:.1f} ticks/s)")
    # Une seconde de tolérance sur le rapport vaut moins d'un tick sur la coupe
    # la plus tardive ; le défaut D263 le faisait varier de 270 à 700.
    if ecart > 2.0:
        print(f"RATÉ : les coupes ne suivent pas les attaques — rapport tick/s de "
              f"{min(rapports):.1f} à {max(rapports):.1f} (écart {ecart:.1f}) — D263")
        return 1
    print(f"OK : {len(coupes)} coupe(s) aux {len(ATTAQUES)} attaques, "
          f"rapport tick/s constant à {ecart:.2f} près")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
