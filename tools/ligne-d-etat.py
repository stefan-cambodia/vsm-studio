#!/usr/bin/env python3
"""LA GARDE DE D515 : LA LIGNE D'ÉTAT DU PIANO ROLL SUIT LA SÉLECTION, D'OÙ QU'ELLE VIENNE.

    tools/ligne-d-etat.py [chemin/du/binaire]

LA RÈGLE GARDÉE (29/09/2026). La ligne d'état résume la sélection (« 8 note(s)
sélectionnée(s) : C3 - B3, … »). Elle ne se refaisait qu'aux gestes de SOURIS :
après Ctrl+A, « Prêt » — quatre témoins sur quatre (D514). Elle se refait
désormais quand le compte de la sélection change, et un message PROPRE au geste
(« 1 note(s) plus faible(s) que 64 », « Arpéger : rien à changer ») gagne sur le
résumé.

COMMENT. Un projet d'une piste de huit notes (vélocités 60 à 116). Chaque cas a
son HOME neuf (D318) ; les gestes sont différés (`VSM_GESTE_APRES`), la ligne
d'état relevée par `relever-etat` (D514). Un cas dont le relevé manque n'est pas
jugé « faux » mais « NON MESURÉ » — et rouge : une mesure absente n'est pas un zéro.

Rend 0 si tout tient, 1 sinon, 2 si le binaire manque.
"""
from __future__ import annotations

import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
BIN = RACINE / "build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"


def vlq(n: int) -> bytes:
    b = [n & 0x7F]
    n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(b))


def engendrer(dossier: Path) -> None:
    (dossier / "midi").mkdir(parents=True)
    evs = [(0, b"\xff\x03\x03une")]
    for i in range(8):
        h = 48 + (i * 5) % 12
        evs += [(0 if i == 0 else 240, bytes([0x90, h, 60 + i * 8])), (240 + i * 30, bytes([0x80, h, 0]))]
    corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
    (dossier / "midi/arrangement.mid").write_bytes(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480) + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    (dossier / "project.json").write_text(json.dumps({
        "format": "vsm-project", "version": 1, "title": "ligne", "midi": {"file": "midi/arrangement.mid"},
        "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                      "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                      "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
        "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                    "instrument": {"preferredPlugin": "vsm.minimoog"},
                    "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                    "name": "une"}]}, indent=1))


CHOISIR = "300:touche:pianoroll:ctrl + A;"
# (nom, gestes, ce que la ligne doit dire — un début de phrase)
CAS = [
    ("Ctrl+A", CHOISIR, "8 note(s) sélectionnée(s)"),
    ("Ctrl+A, puis Inverser la sélection (menu)",
     CHOISIR + "700:menu:Édition > Sélection > Inverser la sélection;", "8 note(s) sur la piste"),
    ("Notes plus faibles que 64 (menu) — contrôle",
     "700:menu:Édition > Sélection > Notes plus faibles que 64 (1);", "1 note(s) plus faible(s) que 64"),
    ("Ctrl+A, puis Arpéger : montant — contrôle",
     CHOISIR + "700:menu:Édition > Arpèges > Arpéger : montant;", "Arpéger : rien à changer"),
]


def main() -> int:
    binaire = Path(sys.argv[1]) if len(sys.argv) > 1 else BIN
    if not binaire.exists():
        print(f"REFUS : {binaire} absent — compiler d'abord")
        return 2
    brouillon = Path(tempfile.mkdtemp(dir=os.environ.get("TMPDIR", "/tmp"), prefix="vsm-ligne-d-etat."))
    try:
        projet = brouillon / "projet"
        engendrer(projet)
        rates = 0
        print("=== D515 : la ligne d'état du piano roll suit la sélection ===")
        for k, (nom, gestes, attendu) in enumerate(CAS):
            maison = Path(tempfile.mkdtemp(dir=brouillon, prefix="home."))
            env = dict(os.environ, HOME=str(maison), VSM_TAILLE="1280x800", VSM_PROJET=str(projet),
                       VSM_VUE="sans-rapport", VSM_DELAI="1800", VSM_GESTE_APRES=gestes + "1200:relever-etat",
                       VSM_CAPTURE=str(brouillon / f"{k}.png"))
            r = subprocess.run([str(binaire)], env=env, capture_output=True, text=True, errors="replace",
                               timeout=60, check=False)
            journal = r.stdout + r.stderr
            # LE BANC RELAIE CE QUE L'APPLICATION AVERTIT (CLAUDE.md, D147)
            for x in journal.splitlines():
                if x.startswith(("VSM_MENU", "VSM_TOUCHE", "VSM_GESTE_APRES")) and \
                        any(m in x for m in ("aucune", "inconnu", "JAMAIS", "refusé", "GRISÉ", "illisible")):
                    print(f"        journal : {x.strip()}")
            lignes = [x.split(" : ", 1)[1] for x in journal.splitlines() if x.startswith("VSM_ETAT_PIANOROLL : ")]
            if not lignes:
                print(f"  NON MESURÉ {nom} — aucun relevé de la ligne d'état")
                rates += 1
                continue
            dit = lignes[-1]
            if dit.startswith(attendu):
                print(f"  OK   {nom:45s} « {dit[:70]} »")
            else:
                print(f"  RATÉ {nom:45s} « {dit[:70]} » (attendu : « {attendu}… »)")
                rates += 1
        print(f"--- {rates} raté(s) sur {len(CAS)}")
        return 1 if rates else 0
    finally:
        shutil.rmtree(brouillon, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
