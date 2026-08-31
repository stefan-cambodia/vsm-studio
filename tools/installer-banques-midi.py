#!/usr/bin/env python3
"""
Installe les banques General MIDI libres comme profils de `vsm.multisample`.

POURQUOI CE SCRIPT. Le § 9 du cahier des charges (docs/CDC-multisample.md)
interdit de commettre une banque dans le dépôt ; et les profils FluidR3 et
GeneralUser installés jusqu'ici l'avaient été À LA MAIN, sans trace rejouable.
Ce script est la trace : il télécharge chaque banque, vérifie son empreinte
SHA-256 contre celle épinglée ici, et convertit par `vsm-sf2` un ensemble
CANONIQUE de programmes General MIDI en profils — mêmes noms d'une banque à
l'autre (FR3-Grand-Piano, GU-Grand-Piano, MS-Grand-Piano), pour que
l'arbitrage par profil compare des timbres, pas des catalogues.

CE QU'IL REFUSE : une banque sans licence écrite dans le manifeste, une
archive dont l'empreinte ne correspond pas, et l'écrasement d'un profil déjà
installé (sauf --forcer). Ce qu'il ne trouve pas dans une banque est DIT,
jamais passé sous silence.

Usage :
    python3 tools/installer-banques-midi.py                # tout installer
    python3 tools/installer-banques-midi.py --banque fluidr3
    python3 tools/installer-banques-midi.py --lister
    python3 tools/installer-banques-midi.py --programmes 0,33,52
"""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

# Troncature des échantillons, en secondes — la valeur mesurée du CDC
# multisample (§ 12 : à six secondes, une note de piano a déjà perdu 50 dB).
DUREE_MAX = 6.0


@dataclass
class Banque:
    """Une banque libre, décrite ENTIÈREMENT ici — URL, licence, crédit.

    L'attribution est recopiée telle quelle dans chaque profil converti. Une
    banque sans licence écrite n'a pas d'entrée dans cette table, et le script
    ne sait donc pas l'installer.
    """

    cle: str
    nom: str
    fichier: str                      # nom du SF2 dans le dossier des banques
    url: str
    sha256: str
    licence: str
    credit: str
    prefixe: str                      # préfixe des noms de profils
    attribution: str
    # Programmes dont le profil porte DÉJÀ un autre nom (installations
    # antérieures à ce script) : on les respecte au lieu de les doubler.
    noms_particuliers: Dict[int, str] = field(default_factory=dict)


BANQUES: Dict[str, Banque] = {
    "fluidr3": Banque(
        cle="fluidr3",
        nom="FluidR3 GM/GS (Frank Wen)",
        fichier="FluidR3_GM_GS.sf2",
        url="https://archive.org/download/fluidr3-gm-gs/FluidR3_GM_GS.sf2",
        sha256="545b2833936f15f04df5f0c5c4096b3ba6ced46ec7031f61991cae46f8681986",
        licence="MIT",
        credit="Frank Wen",
        prefixe="FR3",
        attribution="FluidR3 GM par Frank Wen — licence MIT",
    ),
    "generaluser": Banque(
        cle="generaluser",
        nom="GeneralUser GS 2.0.3 (S. Christian Collins)",
        fichier="GeneralUser-GS.sf2",
        url="https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/main/GeneralUser-GS.sf2",
        sha256="9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe",
        licence="GeneralUser GS v2.0 (libre d'utilisation avec crédit)",
        credit="S. Christian Collins",
        prefixe="GU",
        attribution=("GeneralUser GS 2.0.3 par S. Christian Collins — licence "
                     "GeneralUser GS v2.0, libre d'utilisation avec crédit"),
        noms_particuliers={52: "Concert-Choir", 48: "Fast-Strings"},
    ),
    "musescore": Banque(
        cle="musescore",
        nom="MuseScore_General v0.2 (Wen, Cowgill, Collins)",
        fichier="MuseScore_General.sf2",
        url="https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/"
            "MuseScore_General.sf2",
        sha256="ee51d2c4b1525e70f19a45909c4fd7a2e26d91d115fa89dbf5a6bc413d8b9bf3",
        licence="MIT",
        credit="Frank Wen, Michael Cowgill, S. Christian Collins",
        prefixe="MS",
        attribution=("MuseScore_General v0.2 (Frank Wen, Michael Cowgill, "
                     "S. Christian Collins) — licence MIT"),
    ),
}

# L'ensemble CANONIQUE : un nom par programme General MIDI, le même pour
# toutes les banques. La sélection couvre les familles qu'une reconstruction
# rencontre (claviers, orgues, guitares, basses, cordes, chœurs, cuivres,
# anches, flûtes, nappes) et laisse dehors les bruitages et percussions
# chromatiques rares — chaque profil installé est une candidate de plus à
# l'arbitrage de piste, et ce coût se paie à chaque morceau.
PROGRAMMES_GM: Dict[int, str] = {
    0: "Grand-Piano",
    4: "E-Piano-Tine",
    5: "E-Piano-FM",
    16: "Drawbar-Organ",
    18: "Rock-Organ",
    19: "Church-Organ",
    21: "Accordion",
    24: "Nylon-Guitar",
    25: "Steel-Guitar",
    26: "Jazz-Guitar",
    27: "Clean-Guitar",
    29: "Overdrive-Guitar",
    30: "Distortion-Guitar",
    32: "Acoustic-Bass",
    33: "Finger-Bass",
    34: "Pick-Bass",
    35: "Fretless-Bass",
    38: "Synth-Bass-1",
    39: "Synth-Bass-2",
    40: "Violin",
    42: "Cello",
    46: "Harp",
    48: "Strings",
    49: "Slow-Strings",
    50: "Synth-Strings-1",
    52: "Choir-Aahs",
    53: "Voice-Oohs",
    56: "Trumpet",
    57: "Trombone",
    61: "Brass-Section",
    62: "Synth-Brass-1",
    64: "Soprano-Sax",
    65: "Alto-Sax",
    66: "Tenor-Sax",
    68: "Oboe",
    71: "Clarinet",
    73: "Flute",
    80: "Square-Lead",
    81: "Saw-Lead",
    88: "New-Age-Pad",
    89: "Warm-Pad",
    90: "Polysynth",
    91: "Choir-Pad",
    94: "Halo-Pad",
    95: "Sweep-Pad",
}


def dossier_donnees() -> Path:
    if sys.platform == "win32":
        return Path(os.environ.get("APPDATA", ".")) / "vsm-studio"
    return Path.home() / ".local" / "share" / "vsm-studio"


def empreinte_sha256(chemin: Path) -> str:
    somme = hashlib.sha256()
    with chemin.open("rb") as flux:
        for bloc in iter(lambda: flux.read(1 << 20), b""):
            somme.update(bloc)
    return somme.hexdigest()


def trouver_vsm_sf2() -> Path:
    racine = Path(__file__).resolve().parent.parent
    candidats = [racine / "build" / "tools" / "vsm-sf2"]
    for candidat in candidats:
        if candidat.is_file() and os.access(candidat, os.X_OK):
            return candidat
    raise SystemExit("vsm-sf2 introuvable — compiler la cible : "
                     "cmake --build build --target vsm-sf2")


def obtenir_banque(banque: Banque, dossier: Path) -> Path:
    """Rend le SF2 vérifié de la banque, en le téléchargeant au besoin."""
    chemin = dossier / banque.fichier
    if not chemin.is_file():
        print(f"  téléchargement de {banque.url}")
        dossier.mkdir(parents=True, exist_ok=True)
        temporaire = chemin.with_suffix(".part")
        with urllib.request.urlopen(banque.url) as reponse, temporaire.open("wb") as sortie:
            while True:
                bloc = reponse.read(1 << 20)
                if not bloc:
                    break
                sortie.write(bloc)
        temporaire.rename(chemin)
    somme = empreinte_sha256(chemin)
    if somme != banque.sha256:
        raise SystemExit(
            f"EMPREINTE FAUSSE pour {banque.fichier} :\n"
            f"  attendue {banque.sha256}\n  obtenue  {somme}\n"
            f"Le fichier n'est pas celui que le manifeste décrit ; rien n'est installé.")
    return chemin


def convertir(outil: Path, sf2: Path, banque: Banque, programme: int, nom: str,
              destination: Path) -> bool:
    """Convertit un programme en profil. Rend False si le preset est absent."""
    commande = [str(outil), "--convertir", str(sf2),
                "--programme", str(programme),
                "--nom", nom,
                "--attribution", banque.attribution,
                "--duree-max", str(DUREE_MAX),
                "--sortie", str(destination)]
    resultat = subprocess.run(commande, capture_output=True, text=True)
    if resultat.returncode != 0:
        message = (resultat.stderr or resultat.stdout).strip().splitlines()
        print(f"    {nom:20s} ABSENT ou refusé — {message[-1] if message else 'sans détail'}")
        return False
    return True


def main() -> int:
    analyseur = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    analyseur.add_argument("--banque", choices=sorted(BANQUES), default=None,
                           help="n'installer que cette banque (défaut : toutes)")
    analyseur.add_argument("--programmes", default=None,
                           help="programmes GM à convertir, séparés par des virgules "
                                "(défaut : l'ensemble canonique)")
    analyseur.add_argument("--destination", type=Path, default=None,
                           help="dossier d'installation (défaut : dossier des profils)")
    analyseur.add_argument("--forcer", action="store_true",
                           help="reconvertir même les profils déjà installés")
    analyseur.add_argument("--lister", action="store_true",
                           help="afficher le manifeste et sortir")
    arguments = analyseur.parse_args()

    if arguments.lister:
        for banque in BANQUES.values():
            print(f"  {banque.cle:12s} {banque.nom}\n"
                  f"               licence : {banque.licence} — crédit : {banque.credit}\n"
                  f"               {banque.url}")
        print(f"  programmes canoniques : {len(PROGRAMMES_GM)} "
              f"({', '.join(str(p) for p in sorted(PROGRAMMES_GM))})")
        return 0

    if arguments.programmes:
        try:
            demandes = [int(p) for p in arguments.programmes.split(",") if p.strip()]
        except ValueError as erreur:
            raise SystemExit(f"--programmes illisible : {erreur}") from erreur
        inconnus = [p for p in demandes if p not in PROGRAMMES_GM]
        if inconnus:
            raise SystemExit(f"programmes hors de l'ensemble canonique : {inconnus} "
                             f"— ajouter leur nom à PROGRAMMES_GM d'abord")
        programmes = {p: PROGRAMMES_GM[p] for p in demandes}
    else:
        programmes = dict(PROGRAMMES_GM)

    outil = trouver_vsm_sf2()
    donnees = dossier_donnees()
    destination = arguments.destination or (donnees / "profils")
    destination.mkdir(parents=True, exist_ok=True)
    banques = ([BANQUES[arguments.banque]] if arguments.banque
               else list(BANQUES.values()))

    installes: List[str] = []
    conserves: List[str] = []
    absents: List[str] = []
    for banque in banques:
        print(f"{banque.nom}")
        sf2 = obtenir_banque(banque, donnees / "banques")
        for programme in sorted(programmes):
            suffixe = banque.noms_particuliers.get(programme, programmes[programme])
            nom = f"{banque.prefixe}-{suffixe}"
            if (destination / f"{nom}.profile.json").is_file() and not arguments.forcer:
                conserves.append(nom)
                continue
            if convertir(outil, sf2, banque, programme, nom, destination):
                print(f"    {nom:20s} installé (programme {programme})")
                installes.append(nom)
            else:
                absents.append(nom)

    total = sum(f.stat().st_size for f in destination.rglob("*") if f.is_file())
    print(f"\n  {len(installes)} profil(s) installé(s), {len(conserves)} déjà en place "
          f"(non touchés), {len(absents)} absent(s) des banques.")
    print(f"  dossier des profils : {destination} — {total / (1 << 20):.0f} Mo au total")
    if absents:
        print(f"  absents : {', '.join(absents)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
