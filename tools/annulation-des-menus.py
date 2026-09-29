#!/usr/bin/env python3
"""L'AUDIT DE D506 : UNE ENTRÉE DE MENU QUI MODIFIE LE PROJET EMPILE UN PAS D'ANNULATION.

    tools/annulation-des-menus.py [chemin/du/binaire] [--seulement LIBELLÉ]

LA RÈGLE GARDÉE (29/09/2026). D36.4 l'a posée pour les widgets de la ligne de
piste (`vsm-edit-audit`) : un geste qui change le projet sans `beginProjectEdit`
ne supprime pas le Ctrl+Z, il le DÉCALE — le Ctrl+Z suivant remonte à
l'instantané d'avant et annule deux gestes en un, sans le dire. Aucun banc ne
l'éprouvait sur les ENTRÉES DE MENU.

COMMENT. Un projet de trois pistes est engendré. Une course « liste » choisit
toutes les notes du piano roll (`touche:pianoroll:ctrl + A`) puis relève les
menus (`lister-menus`) : les entrées ACTIVES des menus qui modifient le morceau
— Édition, Piste, Transport, Enregistrement, Mixage — sont retenues, sauf celles
qui ouvrent une fenêtre (libellé en « … » ou « ... »). Puis, pour chaque entrée
— jouée par son CHEMIN (`menu:Mixage > Delay > Gate`, D510 : les sous-menus de
chaque bus répètent les mêmes libellés) —, une course
sous un HOME neuf (D318) : tout choisir, l'entrée (`menu:`), enregistrer
(`enregistrer:`), relever l'historique (`relever-historique`) — les quatre en
différé, dans cet ordre. Une course témoin fait la même chose SANS l'entrée.

LE VERDICT, PAR ENTRÉE :
  * le morceau a changé (project.json sans son bloc `view`, et le .mid) ET un pas
    de plus → juste ;
  * le morceau a changé SANS pas → SUSPECT, et c'est ce que la garde attrape ;
  * un pas SANS changement → « pas pour rien », dit (un Ctrl+Z qui n'annule rien) ;
  * ni l'un ni l'autre → l'entrée ne touche pas au morceau (vue, écoute…).
Les exceptions — un changement légitimement sans pas — sont écrites ci-dessous
avec leur raison, jamais tacites.

Rend 0 si aucune entrée n'est suspecte, 1 sinon, 2 si le binaire manque.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
BIN = RACINE / "build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
MENUS = ("Édition", "Piste", "Transport", "Enregistrement", "Mixage")

# LES CHANGEMENTS LÉGITIMEMENT SANS PAS, avec leur raison. Une exception tacite
# serait la panne muette que cette garde traque.
EXCEPTIONS = {
    "Boucle (marche / arrêt)": "la boucle marche/arrêt est une bascule de transport, comme dans Cubase et Live : "
                               "elle s'enregistre avec le morceau mais ne fait pas de pas (D0, D503)",
    "Métronome (marche / arrêt)": "le clic, même règle que la boucle (D503)",
}


def vlq(n: int) -> bytes:
    b = [n & 0x7F]
    n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(b))


def engendrer(dossier: Path) -> None:
    (dossier / "midi").mkdir(parents=True)
    def piste(nom: str, base: int, tempo: bytes = b"") -> bytes:
        evs = ([(0, tempo)] if tempo else []) + [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
        for i in range(8):
            h = base + (i * 5) % 12
            evs += [(0 if i == 0 else 240, bytes([0x90, h, 60 + i * 8])), (240 + i * 30, bytes([0x80, h, 0]))]
        corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
        return b"MTrk" + struct.pack(">I", len(corps)) + corps
    (dossier / "midi/arrangement.mid").write_bytes(
        b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480)
        + piste("une", 48, b"\xff\x51\x03\x07\xa1\x20") + piste("deux", 60) + piste("trois", 72))
    def t(nom: str) -> dict:
        return {"channel": 0, "color": "#FF6B9BFF", "effects": [],
                "instrument": {"preferredPlugin": "vsm.minimoog"},
                "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                "name": nom}
    (dossier / "project.json").write_text(json.dumps({
        "format": "vsm-project", "version": 1, "title": "annulation",
        "midi": {"file": "midi/arrangement.mid"},
        "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                      "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                      "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
        "tracks": [t("une"), t("deux"), t("trois")]}, indent=1))


def lancer(binaire: Path, brouillon: Path, nom: str, projet: Path, gestes: str, delai: int) -> str:
    maison = Path(tempfile.mkdtemp(dir=brouillon, prefix="home."))
    env = dict(os.environ, HOME=str(maison), VSM_TAILLE="1280x800", VSM_PROJET=str(projet),
               VSM_VUE="sans-rapport", VSM_DELAI=str(delai), VSM_GESTE_APRES=gestes,
               VSM_CAPTURE=str(brouillon / f"{nom}.png"))
    try:
        r = subprocess.run([str(binaire)], env=env, capture_output=True, text=True,
                           errors="replace", timeout=60)
        return r.stdout + r.stderr
    except subprocess.TimeoutExpired as e:
        return (e.stdout or "") + (e.stderr or "") if isinstance(e.stdout, str) else "TIMEOUT"


def empreinte(dossier: Path) -> str:
    """Le MORCEAU écrit : project.json sans son bloc `view` (le cadrage n'est pas le
    morceau), et chaque fichier des sous-dossiers (le .mid, les presets…)."""
    h = hashlib.sha256()
    fichier = dossier / "project.json"
    if not fichier.exists():
        return "absent"
    doc = json.loads(fichier.read_text(encoding="utf-8"))
    doc.pop("view", None)
    h.update(json.dumps(doc, sort_keys=True).encode())
    for f in sorted(dossier.rglob("*")):
        if f.is_file() and f.name != "project.json":
            h.update(f.relative_to(dossier).as_posix().encode())
            h.update(f.read_bytes())
    return h.hexdigest()[:16]


def pas(journal: str) -> int:
    m = re.findall(r"VSM_HISTORIQUE_PAS : (\d+)", journal)
    return int(m[-1]) if m else -1


def main() -> int:
    args = [a for a in sys.argv[1:]]
    seulement = None
    if "--seulement" in args:
        i = args.index("--seulement")
        seulement = args[i + 1]
        del args[i:i + 2]
    binaire = Path(args[0]) if args else BIN
    if not binaire.exists():
        print(f"REFUS : {binaire} absent — compiler d'abord")
        return 2
    brouillon = Path(tempfile.mkdtemp(dir=os.environ.get("TMPDIR", "/tmp"), prefix="vsm-annulation-menus."))
    # LE BROUILLON EST UN tmpfs (CLAUDE.md) : une course y laissait 36 Mo de projets
    # écrits — dix courses, 360 Mo de RAM. CE brouillon, et lui seul, est effacé.
    try:
        return auditer(binaire, brouillon, seulement)
    finally:
        shutil.rmtree(brouillon, ignore_errors=True)


def auditer(binaire: Path, brouillon: Path, seulement: str | None) -> int:
    projet = brouillon / "projet"
    engendrer(projet)
    choisir = "300:touche:pianoroll:ctrl + A"

    liste = lancer(binaire, brouillon, "liste", projet, f"{choisir};700:lister-menus", 1200)
    lignes = [l.split("VSM_MENU_LISTE : ", 1)[1] for l in liste.splitlines() if l.startswith("VSM_MENU_LISTE : ")]
    chemins = [l.split(" [")[0] for l in lignes]
    parents = {c.rsplit(" > ", 1)[0] for c in chemins}
    barre = ("Fichier", "Édition", "Piste", "Transport", "Enregistrement", "Mixage", "Affichage", "Aide")
    entrees = [(c, l) for l, c in zip(lignes, chemins)
               if c.split(" > ")[0] in barre and c not in parents and "[titre]" not in l]
    retenues = []
    for c, l in entrees:
        menu = c.split(" > ")[0]
        texte = c.rsplit(" > ", 1)[-1]
        if menu not in MENUS or "[grisée]" in l:
            continue
        if texte.rstrip().endswith(("...", "…")) or "..." in texte or "…" in texte:
            continue
        # D510 : L'ENTRÉE EST JOUÉE PAR SON CHEMIN (`menu:Mixage > Delay > Gate`) : les
        # sous-menus de chaque bus répètent les mêmes libellés, et le premier venu
        # était toujours celui du premier bus. Plus aucune entrée n'est hors d'atteinte.
        retenues.append((c, texte))
    if seulement:
        retenues = [(c, t) for c, t in retenues if t == seulement or t.startswith(seulement)]
    print(f"=== D506 : les entrées de menu qui modifient le morceau empilent un pas ({len(retenues)} entrées) ===")

    # D510 : DE LA MARGE, ET UNE MESURE ABSENTE N'EST PAS UN ZÉRO. Dans la course
    # complète, quatre entrées (des accords) ont rendu « non jouée » ou « suspecte » :
    # rejouées seules, toutes justes. Sous la charge, un geste différé peut tomber
    # après la photo qui clôt la course ; le relevé d'historique manquait, et
    # l'ancien code le comptait « aucun pas » — un faux suspect fabriqué par le banc.
    def course(nom: str, gestes: str) -> tuple[str, int, str]:
        sortie = brouillon / f"ecrit-{nom}"
        j = lancer(binaire, brouillon, nom, projet,
                   f"{choisir};{gestes}1300:enregistrer:{sortie};1700:relever-historique", 2800)
        return empreinte(sortie), pas(j), j

    e0, p0, j0 = course("temoin", "")
    if e0 == "absent" or p0 < 0:
        print(f"  RATÉ témoin illisible (empreinte {e0}, pas {p0})")
        return 1
    suspects = 0
    incompletes = []
    pour_rien = []
    neutres = []
    for i, (chemin, texte) in enumerate(retenues):
        for essai in (1, 2):   # une mesure incomplète est rejouée UNE fois
            e, p, j = course(f"e{i}-{essai}", f"700:menu:{chemin};")
            joue = "exécutée" in j and f"« {chemin}" in j
            if joue and p >= 0 and e != "absent":
                break
        if not joue or p < 0 or e == "absent":
            manque = ("l'entrée n'a pas été jouée" if not joue
                      else "le relevé d'historique manque" if p < 0 else "le projet n'a pas été écrit")
            print(f"  ?    {chemin} — NON JUGÉE, deux fois : {manque} (une mesure absente n'est pas un zéro)")
            incompletes.append(chemin)
            continue
        change, empile = e != e0, p > p0
        if change and empile:
            print(f"  OK   {chemin}")
        elif change and not empile:
            if texte in EXCEPTIONS:
                print(f"  OK   {chemin} — sans pas, et c'est voulu : {EXCEPTIONS[texte]}")
            else:
                print(f"  SUSPECT {chemin} — le morceau change SANS pas d'annulation")
                suspects += 1
        elif empile:
            pour_rien.append(chemin)
            print(f"  NOTE {chemin} — un pas SANS changement du morceau")
        else:
            neutres.append(chemin)
    print(f"    {len(neutres)} entrée(s) sans effet sur le morceau (vue, écoute, sélection) : non jugées")
    print(f"--- {suspects} entrée(s) suspecte(s), {len(incompletes)} non jugée(s), {len(pour_rien)} pas pour rien")
    return 1 if suspects or incompletes else 0


if __name__ == "__main__":
    sys.exit(main())
