#!/usr/bin/env python3
"""L'interface ANGLAISE, en marche, écrit-elle encore du français ?

    python3 tools/anglais-a-l-ecran.py [chemin/du/binaire] [--tout]

LA RÈGLE GARDÉE (28/09/2026, D476). Sous `VSM_LANGUE=en`, aucun texte affiché ne
porte une lettre propre au français (é è ê ë à â ù û ç ô î ï œ). La garde de langue
statique (`inventaire_langue.py`) lit les LITTÉRAUX d'`app/Source` ; elle ne voit
pas un nom qui arrive de `core/` ou d'`audio/` et s'affiche sans passer par
`tr()`. C'est ainsi que le panneau d'effets écrivait « Arpégiateur — parameters »
et un bouton « Arpégiateur » pendant que le menu, lui, disait « Add: Arpeggiator » :
la traduction existait, un seul des chemins s'en servait.

COMMENT. Plusieurs vues sont ouvertes en anglais, chacune sous un HOME neuf (D318),
avec de quoi les remplir : un effet d'insert et un effet MIDI posés sur la piste
de démarrage (les noms de types viennent de `audio/` et de `core/`). Le relevé
`VSM_TEXTES_LISTE` donne chaque texte que la fenêtre montre ; un texte est suspect
s'il porte une lettre accentuée française.

CE QU'ELLE NE VOIT PAS, DIT PLUTÔT QUE TU : un mot français SANS accent (« Piste »,
« Mode »…) ; un texte PEINT (`g.drawText`), invisible au relevé (D149, D152) ; les
boîtes de dialogue, qui n'existent qu'après leur geste (D95). Les noms que
l'UTILISATEUR a écrits (une piste « Batterie · caisse claire ») ne sont pas des
fautes : le projet de démarrage n'en porte pas, et la garde n'en crée pas.

Rend 0 si aucun texte suspect, 1 sinon, 2 si le binaire manque ou si une vue n'a
rien relevé (une mesure qui ne voit rien doit le dire, D265).
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
BINAIRE = RACINE / "build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
FRANCAIS = re.compile(r"[éèêëàâùûçôîïœÉÈÊÀÂÙÛÇÔÎÏŒ]")

# (nom, variables du banc). L'effet MIDI se pose par le menu Piste (en anglais :
# « Add: Arpeggiator »), l'effet d'insert par le verbe de clic droit du panneau.
VUES = [
    ("demarrage", {}),
    ("effets", {"VSM_MENU": "Add: Arpeggiator", "VSM_MENU_CONTEXTE": "ajout-effet:Reverb",
                "VSM_VUE": "effets,agrandir:bas"}),
    ("mixeur", {"VSM_VUE": "mixer,agrandir:bas"}),
    ("automation", {"VSM_VUE": "automation,agrandir:bas"}),
    ("midi-cc", {"VSM_VUE": "midi-cc,agrandir:bas"}),
    ("liste", {"VSM_VUE": "liste,agrandir:bas"}),
    ("tempo", {"VSM_VUE": "tempo,agrandir:bas"}),
    ("arrangement", {"VSM_VUE": "arrangement"}),
]


def main() -> int:
    arguments = [a for a in sys.argv[1:] if not a.startswith("--")]
    tout = "--tout" in sys.argv
    binaire = Path(arguments[0]) if arguments else BINAIRE
    if not binaire.exists():
        print(f"REFUS : {binaire} absent — compiler d'abord")
        return 2
    print("=== D476 : l'interface anglaise, en marche, n'écrit pas de français ===")
    suspects = 0
    muettes = 0
    with tempfile.TemporaryDirectory(prefix="vsm-anglais-") as brouillon:
        for nom, variables in VUES:
            maison = tempfile.mkdtemp(dir=brouillon)
            env = dict(os.environ, HOME=maison, VSM_LANGUE="en", VSM_TEXTES_LISTE="1",
                       VSM_TAILLE="1600x1000", VSM_DELAI="800",
                       VSM_CAPTURE=str(Path(brouillon) / f"{nom}.png"), **variables)
            sortie = subprocess.run([str(binaire)], env=env, capture_output=True, text=True,
                                    errors="replace", timeout=60).stderr
            textes = [ligne.split(" : ", 2)[2] for ligne in sortie.splitlines()
                      if ligne.startswith("VSM_TEXTE : ") and ligne.count(" : ") >= 2]
            avertis = [ligne for ligne in sortie.splitlines()
                       if re.match(r"VSM_(MENU|MENU_CONTEXTE|VUE) : .*(aucune|inconnu|introuvable)", ligne)]
            if not textes:
                print(f"  RATÉ {nom:<12} aucun texte relevé — la vue n'a rien mesuré")
                muettes += 1
                continue
            fautes = sorted({t for t in textes if FRANCAIS.search(t)})
            for a in avertis:
                print(f"        journal : {a}")
            if fautes:
                suspects += len(fautes)
                print(f"  RATÉ {nom:<12} {len(fautes)} texte(s) français sur {len(textes)} : "
                      + " | ".join(f"« {f[:60]} »" for f in fautes[:8]))
            else:
                print(f"  OK   {nom:<12} {len(textes)} texte(s), aucun français")
            if tout:
                for t in sorted(set(textes)):
                    print(f"         {t[:100]}")
    print(f"--- {suspects} texte(s) français dans l'interface anglaise, {muettes} vue(s) muette(s)")
    if muettes:
        return 2
    return 1 if suspects else 0


if __name__ == "__main__":
    sys.exit(main())
