#!/usr/bin/env python3
"""
Installe le profil PIANO de `vsm.multisample` à partir d'une banque libre.

POURQUOI UN SCRIPT ET PAS DES FICHIERS DANS LE DÉPÔT. Le § 9 du cahier des
charges (docs/CDC-multisample.md) l'interdit : aucune banque n'est commise,
quelle que soit sa licence. Le dépôt doit rester léger et compilable hors
ligne, et une banque de piano pèse le millier de mégaoctets. Elle s'installe
donc, et c'est ce script qui l'installe -- en vérifiant ce qu'il télécharge, en
écrivant l'attribution, et en refusant tout ce qu'il ne sait pas justifier.

CE QU'IL FAIT, DANS L'ORDRE :

  1. télécharge l'archive de la banque choisie ;
  2. vérifie son empreinte SHA-256 contre celle du manifeste ;
  3. en extrait un SOUS-ENSEMBLE mesuré (un échantillon tous les N demi-tons,
     C couches de vélocité), converti en WAV ;
  4. écrit le `*.profile.json` et le fichier d'attribution à côté ;
  5. annonce l'empreinte mémoire du profil, et REFUSE de dépasser le budget.

CE QU'IL NE FAIT PAS : deviner. Une banque dont la licence n'est pas écrite
dans le manifeste n'est pas installable par ce script (§ 28 d'ARCHITECTURE.md).

Usage :
    python3 tools/installer-profil-piano.py --banque salamander
    python3 tools/installer-profil-piano.py --banque salamander --pas 3 --couches 4
    python3 tools/installer-profil-piano.py --lister
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

BUDGET_OCTETS = 256 * 1024 * 1024  # § 3 du cahier des charges


@dataclass
class Banque:
    """Une banque libre, décrite ENTIÈREMENT ici -- URL, licence, crédit.

    L'attribution n'est pas un commentaire : elle est recopiée telle quelle
    dans le profil et dans un fichier `ATTRIBUTION.txt` posé à côté des
    échantillons. Une banque sans licence écrite n'a pas d'entrée dans cette
    table, et le script ne sait donc pas l'installer.
    """

    cle: str
    nom: str
    url: str
    licence: str
    credit: str
    # Empreinte SHA-256 de l'archive. `None` = pas encore relevée dans ce
    # dépôt : le script REFUSE alors d'installer, sauf `--epingler-empreinte`,
    # et il dit dans les deux cas ce qu'il en est. Épingler une empreinte est
    # un geste de mainteneur, qui se relit ensuite dans le diff.
    sha256: Optional[str] = None
    # Comment lire les noms de fichiers de la banque. Le premier groupe est la
    # note (nom anglo-saxon + octave), le second la couche de vélocité.
    # Le motif est ANCRÉ, et c'est lui qui écarte ce que la v1 n'utilise pas :
    # les échantillons de relâchement (`rel79.wav`) et les harmoniques
    # (`harmSA4.wav`) de Salamander ne lui correspondent pas, donc ils sont
    # ignorés — omission déclarée au § 9, pas oubli silencieux.
    motif: str = r"^([A-G][#b]?)(\d+)v(\d+)\.(flac|wav|ogg)$"
    # Nombre de couches de vélocité que la banque contient réellement.
    couches_source: int = 16


BANQUES: Dict[str, Banque] = {
    "salamander": Banque(
        cle="salamander",
        nom="Salamander Grand Piano",
        url="https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/"
            "SalamanderGrandPianoV3+20161209_44khz16bit.tar.xz",
        licence="CC-BY 3.0",
        credit="Salamander Grand Piano V3 — Alexander Holm, CC-BY 3.0 "
               "(https://freepats.zenvoid.org/Piano/acoustic-grand-piano.html)",
        sha256="58750eb1366761e187f71ddb9b932355ea894d28ec4331e74ab8acb44c819936",
        motif=r"^([A-G][#b]?)(\d+)v(\d+)\.(flac|wav|ogg)$",
        couches_source=16,
    ),
    "iowa": Banque(
        cle="iowa",
        nom="University of Iowa Piano",
        url="",  # à renseigner : l'archive d'Iowa se distribue note par note
        licence="domaine public déclaré par l'université",
        credit="University of Iowa Electronic Music Studios, Lawrence Fritts",
        sha256=None,
    ),
}

# Dièses ET bémols : les banques libres n'ont pas la même habitude (Salamander
# écrit « D#1 », d'autres « Eb1 »), et refuser l'une des deux conventions ferait
# passer une banque entière pour vide.
DEMI_TONS = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5,
             "F#": 6, "Gb": 6, "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10,
             "B": 11}


def note_midi(nom: str, octave: int) -> int:
    """Nom anglo-saxon + octave -> numéro MIDI (C4 = 60, convention Yamaha)."""
    return DEMI_TONS[nom] + (octave + 1) * 12


def empreinte(chemin: Path) -> str:
    somme = hashlib.sha256()
    with chemin.open("rb") as flux:
        for bloc in iter(lambda: flux.read(1 << 20), b""):
            somme.update(bloc)
    return somme.hexdigest()


def telecharger(url: str, destination: Path) -> None:
    print(f"  téléchargement de {url}")
    with urllib.request.urlopen(url) as reponse, destination.open("wb") as sortie:
        total = int(reponse.headers.get("Content-Length") or 0)
        lus = 0
        while True:
            bloc = reponse.read(1 << 20)
            if not bloc:
                break
            sortie.write(bloc)
            lus += len(bloc)
            if total:
                print(f"\r  {lus / 1e6:7.1f} / {total / 1e6:.1f} Mo", end="", flush=True)
        print()


def extraire(archive: Path, dossier: Path) -> None:
    if archive.name.endswith((".tar.xz", ".tar.gz", ".tar.bz2", ".tgz")):
        with tarfile.open(archive) as paquet:
            # `filter="data"` refuse les entrées absolues ou remontantes : une
            # archive tierce ne doit pas pouvoir écrire hors du dossier voulu.
            paquet.extractall(dossier, filter="data")
    elif archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive) as paquet:
            paquet.extractall(dossier)
    else:
        raise SystemExit(f"archive de type inconnu : {archive.name}")


@dataclass
class Echantillon:
    chemin: Path
    note: int
    couche: int


def recenser(dossier: Path, banque: Banque) -> List[Echantillon]:
    motif = re.compile(banque.motif)
    trouves: List[Echantillon] = []
    for chemin in sorted(dossier.rglob("*")):
        if not chemin.is_file():
            continue
        correspondance = motif.match(chemin.name)
        if not correspondance:
            continue
        nom, octave, couche, _ = correspondance.groups()
        trouves.append(Echantillon(chemin, note_midi(nom, int(octave)), int(couche)))
    return trouves


def numpy_linspace(start: float, stop: float, count: int):
    """`numpy.linspace`, importé à l'usage : l'outil doit pouvoir s'exécuter
    pour `--lister` sans dépendre de l'environnement d'analyse."""
    import numpy
    return numpy.linspace(start, stop, count)


def convertir(source: Path, destination: Path, duree_max: float) -> Tuple[int, int, float]:
    """Convertit un échantillon en WAV, tronqué à `duree_max`.

    Rend (trames, canaux, fréquence). Utilise `soundfile`, déjà présent dans
    l'environnement d'analyse : le moteur, lui, ne lit que du WAV, et c'est
    volontaire -- une dépendance de décodage dans le chemin audio serait une
    dépendance de plus à embarquer sur trois plateformes.
    """
    try:
        import soundfile
    except ImportError as erreur:  # pragma: no cover - dépend de l'installation
        raise SystemExit(
            "le module `soundfile` est requis pour convertir la banque.\n"
            "  Installez-le dans l'environnement d'analyse :\n"
            "    analyse/.venv/bin/pip install soundfile"
        ) from erreur

    donnees, frequence = soundfile.read(str(source), always_2d=True)
    limite = int(duree_max * frequence)
    if 0 < limite < len(donnees):
        # FONDU DE SORTIE SUR LA TRONCATURE, et ce n'est pas un raffinement.
        # Une note de piano enregistrée résonne dix secondes ; coupée net à six,
        # elle s'arrête sur une valeur qui n'est pas zéro, et le lecteur produit
        # un CLIC à chaque note tenue assez longtemps. C'est l'outil qui coupe,
        # c'est donc à lui de couper proprement : le moteur, lui, joue ce qu'on
        # lui donne et n'a pas à deviner qu'un fichier a été tronqué.
        #
        # Trente millisecondes : assez court pour ne pas raccourcir la note à
        # l'oreille, assez long pour que la discontinuité passe sous le seuil
        # d'audibilité même sur un grave riche.
        fondu = min(int(0.030 * frequence), limite)
        donnees = donnees[:limite].copy()
        if fondu > 1:
            rampe = numpy_linspace(1.0, 0.0, fondu)
            donnees[-fondu:] *= rampe[:, None]
    elif limite > 0:
        donnees = donnees[:limite]
    destination.parent.mkdir(parents=True, exist_ok=True)
    soundfile.write(str(destination), donnees, int(frequence), subtype="PCM_16")
    return len(donnees), donnees.shape[1], float(frequence)


def choisir(echantillons: List[Echantillon], pas: int, couches: int,
            couches_source: int) -> List[Echantillon]:
    """Retient un échantillon tous les `pas` demi-tons, sur `couches` couches.

    LE PAS ET LES COUCHES NE SONT PAS ÉCRITS DANS LE CODE (§ 4 du cahier des
    charges) : ils se choisissent à l'oreille et au budget, ils se passent en
    argument, et le profil produit les inscrit dans son JSON. Ce qui est écrit
    ici n'est que la MÉCANIQUE du choix.
    """
    notes = sorted({e.note for e in echantillons})
    if not notes:
        return []
    retenues = set(notes[::pas])
    retenues.add(notes[-1])  # toujours garder l'extrême aigu

    # Couches réparties régulièrement dans celles que la banque propose : de la
    # plus douce à la plus forte, sans privilégier une extrémité.
    if couches >= couches_source:
        couches_retenues = set(range(1, couches_source + 1))
    else:
        couches_retenues = {
            1 + round(i * (couches_source - 1) / (couches - 1)) if couches > 1 else 1
            for i in range(couches)
        }

    return [e for e in echantillons if e.note in retenues and e.couche in couches_retenues]


def construire_profil(banque: Banque, retenus: List[Echantillon], dossier: Path,
                      duree_max: float) -> Tuple[dict, int]:
    """Convertit les échantillons retenus et rend (profil, octets en mémoire)."""
    notes_racines = sorted({e.note for e in retenus})
    couches = sorted({e.couche for e in retenus})

    # Étendue de notes de chaque racine : jusqu'à mi-chemin de la racine
    # suivante. Un simple « racine .. racine + pas - 1 » transposerait toujours
    # vers le HAUT, ce qui rend un piano brillant et faux dans le grave de
    # chaque zone.
    bornes: Dict[int, Tuple[int, int]] = {}
    for index, racine in enumerate(notes_racines):
        precedente = notes_racines[index - 1] if index > 0 else None
        suivante = notes_racines[index + 1] if index + 1 < len(notes_racines) else None
        basse = 0 if precedente is None else (precedente + racine) // 2 + 1
        haute = 127 if suivante is None else (racine + suivante) // 2
        bornes[racine] = (basse, haute)

    # Étendues de vélocité : découpage régulier de 1..127 entre les couches
    # retenues, la plus douce en premier.
    seuils: Dict[int, Tuple[int, int]] = {}
    for index, couche in enumerate(couches):
        basse = 1 if index == 0 else int(round(127 * index / len(couches))) + 1
        haute = 127 if index == len(couches) - 1 else int(round(127 * (index + 1) / len(couches)))
        seuils[couche] = (basse, haute)

    zones = []
    octets = 0
    for numero, echantillon in enumerate(sorted(retenus, key=lambda e: (e.note, e.couche))):
        relatif = f"echantillons/{echantillon.note:03d}v{echantillon.couche:02d}.wav"
        trames, canaux, _ = convertir(echantillon.chemin, dossier / relatif, duree_max)
        octets += trames * canaux * 4  # le moteur garde tout en float 32 bits
        basse_note, haute_note = bornes[echantillon.note]
        basse_velocite, haute_velocite = seuils[echantillon.couche]
        zones.append({
            "sample": relatif,
            "rootNote": echantillon.note,
            "lowNote": basse_note,
            "highNote": haute_note,
            "lowVelocity": basse_velocite,
            "highVelocity": haute_velocite,
        })
        if numero % 10 == 0:
            print(f"\r  {numero + 1}/{len(retenus)} échantillons convertis", end="", flush=True)
    print(f"\r  {len(retenus)}/{len(retenus)} échantillons convertis")

    profil = {
        "format": "vsm-multisample-profile",
        "version": 1,
        "name": banque.nom,
        "attribution": banque.credit if banque.licence in banque.credit else f"{banque.credit} — {banque.licence}",
        "programs": [banque.nom],
        "zones": zones,
    }
    return profil, octets


def dossier_profils() -> Path:
    """Même règle que `multisampleProfileFolder()` côté C++, et il FAUT que ce
    soit la même : deux définitions divergeraient au premier changement."""
    surcharge = os.environ.get("VSM_PROFILS")
    if surcharge:
        return Path(surcharge)
    if sys.platform.startswith("win"):
        return Path(os.environ.get("APPDATA", ".")) / "vsm-studio" / "profils"
    return Path.home() / ".local" / "share" / "vsm-studio" / "profils"


def main() -> int:
    analyseur = argparse.ArgumentParser(description=__doc__,
                                         formatter_class=argparse.RawDescriptionHelpFormatter)
    analyseur.add_argument("--banque", default="salamander", choices=sorted(BANQUES))
    analyseur.add_argument("--pas", type=int, default=3,
                            help="un échantillon tous les N demi-tons (défaut : 3)")
    analyseur.add_argument("--couches", type=int, default=4,
                            help="nombre de couches de vélocité retenues (défaut : 4)")
    analyseur.add_argument("--duree-max", type=float, default=6.0,
                            help="troncature des échantillons, en secondes (défaut : 6)")
    analyseur.add_argument("--destination", type=Path, default=None,
                            help="dossier d'installation (défaut : dossier des profils)")
    analyseur.add_argument("--archive", type=Path, default=None,
                            help="archive déjà téléchargée, au lieu de la récupérer")
    analyseur.add_argument("--epingler-empreinte", action="store_true",
                            help="relever l'empreinte SHA-256 de l'archive et l'afficher, "
                                 "quand le manifeste n'en porte pas encore")
    analyseur.add_argument("--lister", action="store_true", help="lister les banques connues")
    arguments = analyseur.parse_args()

    if arguments.lister:
        for banque in BANQUES.values():
            etat = "empreinte épinglée" if banque.sha256 else "EMPREINTE NON ÉPINGLÉE"
            print(f"{banque.cle:12s} {banque.nom}\n"
                  f"             licence : {banque.licence}\n"
                  f"             {etat}\n"
                  f"             {banque.url or 'URL à renseigner'}")
        return 0

    banque = BANQUES[arguments.banque]
    if not banque.url and arguments.archive is None:
        print(f"la banque « {banque.cle} » n'a pas d'URL renseignée : passez --archive",
              file=sys.stderr)
        return 2

    destination = arguments.destination or dossier_profils()
    destination = destination / f"piano-{banque.cle}"
    destination.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="vsm-banque-") as temporaire:
        travail = Path(temporaire)
        archive = arguments.archive
        if archive is None:
            archive = travail / Path(banque.url).name
            telecharger(banque.url, archive)

        somme = empreinte(archive)
        print(f"  SHA-256 : {somme}")
        if banque.sha256 is None:
            if not arguments.epingler_empreinte:
                print("\nEMPREINTE NON VÉRIFIÉE : le manifeste de ce dépôt ne porte pas encore\n"
                      "l'empreinte de cette archive, donc rien ne prouve que le fichier\n"
                      "téléchargé est celui qu'on croit. Installation REFUSÉE.\n\n"
                      "Pour l'épingler (geste de mainteneur, à relire dans le diff) :\n"
                      f"    --epingler-empreinte\n"
                      f"puis recopier l'empreinte ci-dessus dans BANQUES[\"{banque.cle}\"].sha256",
                      file=sys.stderr)
                return 3
            print("  (empreinte relevée, non vérifiée : premier passage)")
        elif somme != banque.sha256:
            print(f"\nEMPREINTE DIFFÉRENTE de celle du manifeste :\n"
                  f"  attendue {banque.sha256}\n  obtenue  {somme}\n"
                  "Installation REFUSÉE -- l'archive n'est pas celle qui a été validée.",
                  file=sys.stderr)
            return 4

        extrait = travail / "extrait"
        extrait.mkdir()
        print("  extraction…")
        extraire(archive, extrait)

        echantillons = recenser(extrait, banque)
        if not echantillons:
            print("aucun échantillon reconnu dans l'archive : le motif de nommage du manifeste\n"
                  f"  ({banque.motif}) ne correspond à rien. Banque non installée.", file=sys.stderr)
            return 5
        print(f"  {len(echantillons)} échantillons dans la banque, "
              f"{len({e.note for e in echantillons})} notes")

        retenus = choisir(echantillons, arguments.pas, arguments.couches, banque.couches_source)
        print(f"  {len(retenus)} retenus (pas de {arguments.pas} demi-tons, "
              f"{arguments.couches} couches)")

        profil, octets = construire_profil(banque, retenus, destination, arguments.duree_max)

    print(f"  empreinte mémoire du profil : {octets / (1024 * 1024):.0f} Mo "
          f"(budget {BUDGET_OCTETS // (1024 * 1024)} Mo)")
    if octets > BUDGET_OCTETS:
        print("\nPROFIL AU-DELÀ DU BUDGET. Le moteur le refusera au chargement, donc le script\n"
              "refuse de l'écrire. Réduisez : --pas plus grand, --couches moins nombreuses,\n"
              "ou --duree-max plus courte.", file=sys.stderr)
        shutil.rmtree(destination, ignore_errors=True)
        return 6

    chemin_profil = destination.parent / f"piano-{banque.cle}.profile.json"
    # Les chemins du profil sont relatifs à SON dossier : le profil se pose donc
    # à côté du dossier d'échantillons, et le champ « sample » commence par le
    # nom de ce dossier.
    for zone in profil["zones"]:
        zone["sample"] = f"piano-{banque.cle}/{zone['sample']}"
    chemin_profil.write_text(json.dumps(profil, indent=2, ensure_ascii=False), encoding="utf-8")

    (destination / "ATTRIBUTION.txt").write_text(
        f"{banque.nom}\n{banque.credit}\nLicence : {banque.licence}\nSource : {banque.url}\n",
        encoding="utf-8")

    print(f"\nprofil écrit : {chemin_profil}")
    print(f"attribution  : {destination / 'ATTRIBUTION.txt'}")
    print(f"{len(profil['zones'])} zones. La machine vsm.multisample le verra à sa prochaine ouverture.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
