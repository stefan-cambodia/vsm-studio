#!/usr/bin/env python3
"""Un geste de menu CHANGE le projet, ou DIT pourquoi il ne le change pas.

    analyse/.venv/bin/python tools/gestes-vivants.py [--menu clip-audio|clip-midi|piste]

POURQUOI (13/09/2026, D275). « Découper aux transitoires » a trouvé ses quatre
attaques et n'a rien coupé (D262), puis a coupé aux mauvais endroits (D263), sans
qu'aucune garde ne s'en aperçoive. En balayant le menu entier après coup, une
TROISIÈME entrée s'est révélée morte : « Le clip fait N mesures… » ouvre une
fenêtre à deux boutons et sans champ, à laquelle aucun verbe de banc ne savait
répondre — le geste n'aboutissait jamais et le projet restait identique.

LA RÈGLE, qui est la leçon de ces trois défauts : **une entrée de menu ACTIVE
laisse forcément une trace.** Soit elle change le projet écrit, soit elle DIT
pourquoi elle ne le change pas — « Il y a déjà un clip à cet endroit de la
piste », par exemple. Une entrée qui ne fait ni l'un ni l'autre est un geste mort,
et c'est exactement ce qu'on ne voit pas à l'usage : on clique, rien ne casse, et
l'on croit s'être trompé de clic.

LES TROIS SEULES EXCUSES, écrites ici plutôt que devinées :
  * elle ne touche que la VUE (zoom, repli) — nommée dans `VUE_SEULEMENT` ;
  * elle est GRISÉE — le banc ne l'exécute pas, et le dit ;
  * elle pose une valeur DÉJÀ EN PLACE (le défaut du clip, la couleur de sa
    piste) — elle est alors nommée dans `DEJA_EN_PLACE`, avec sa raison.

CE QUI N'EST PAS BALAYÉ : les entrées qui RENDENT de l'audio (geler, reporter) ou
qui ouvrent un sélecteur de fichier. Elles coûtent cher et se mesurent ailleurs.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
BINAIRE = RACINE / "build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
PROJET = RACINE / "reconstruction/travail/cdl"

# Les gestes qui ne touchent QUE la vue : ils ne doivent rien écrire.
VUE_SEULEMENT = {
    "Zoom : tout voir": "un cadrage, pas une donnée",
    "Zoom : la sélection": "un cadrage, pas une donnée",
}

# Les gestes qui posent une valeur DÉJÀ EN PLACE dans le projet d'essai.
DEJA_EN_PLACE = {
    "Non": "c'est le défaut du clip (ne pas suivre le tempo) — le menu le montre coché",
    "Droite (matériau corrélé)": "FadeShape::Linear = 0, le défaut aussi",
    "Couleur de la piste": "le clip est IMPLICITE et tient déjà la couleur de sa piste",
}

MENUS = {
    "clip-audio": ["Rendre muet", "À l'envers", "Normaliser (gain = 1 / crête)", "-6 dB",
                   "-3 dB", "-1 dB", "+1 dB", "2 fois", "3 fois", "Couleur de la piste",
                   "Hauteur conservée", "Rééchantillonné", "Non",
                   "Droite (matériau corrélé)", "Égale puissance (matériau décorrélé)",
                   "Lente au départ", "Rapide au départ", "Le clip fait N mesures…"],
    "clip-midi": ["Rendre muet", "Couleur de la piste", "2 fois", "3 fois", "16 fois",
                  "Zoom : tout voir", "Zoom : la sélection"],
    "piste": ["Ajouter une piste MIDI", "Ajouter une piste audio", "Ajouter un groupe",
              "Dupliquer la piste sélectionnée",
              "Créer un clip d'une mesure à la tête de lecture",
              "Solo exclusif de la piste choisie (Ctrl+clic sur Solo)",
              "Protéger cette piste du solo des autres (Alt+clic sur Solo)",
              "Masquer la piste (elle continue de sonner)",
              "Verrouiller la piste (le montage s'arrête)",
              "Désactiver la piste (sa machine et ses inserts sont libérés)",
              "Ranger cette piste dans un dossier neuf",
              "Supprimer la piste sélectionnée"],
}


def ecrire_un_son(chemin: Path) -> None:
    """Deux secondes de son, engendrées ici : une garde ne dépend pas d'un fichier voisin."""
    import math
    import struct
    import wave
    taux, trames = 44100, 88200
    with wave.open(str(chemin), "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(taux)
        f.writeframes(b"".join(struct.pack("<hh", v, v) for v in
                               (int(12000 * math.sin(2 * math.pi * 220.0 * i / taux))
                                for i in range(trames))))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--menu", choices=sorted(MENUS), action="append")
    a = p.parse_args()
    if not BINAIRE.is_file():
        print("REFUS : l'application n'est pas compilée — rien n'a été mesuré")
        return 2
    menus = a.menu or sorted(MENUS)

    brouillon = Path(tempfile.mkdtemp(prefix="vsm-vivants-"))
    son = brouillon / "essai.wav"
    ecrire_un_son(son)
    projet = brouillon / "projet"
    shutil.copytree(PROJET, projet)
    rates = 0

    def course(nom: str, menu: str, libelle: str) -> tuple[str, str]:
        maison = brouillon / f"h-{nom}"
        maison.mkdir(parents=True, exist_ok=True)
        sortie = brouillon / nom
        env = dict(os.environ)
        env.update({"HOME": str(maison), "VSM_PROJET": str(projet),
                    "VSM_ENREGISTRER": str(sortie), "VSM_CAPTURE": str(brouillon / f"{nom}.png"),
                    "VSM_DELAI": "600", "VSM_CONFIRMER": "oui"})
        if menu == "clip-audio":
            env["VSM_IMPORT_AUDIO"] = str(son)
        if libelle:
            if menu == "piste":
                env["VSM_MENU"] = libelle
            else:
                env["VSM_MENU_CONTEXTE"] = f"{menu}-tous:{libelle}"
        r = subprocess.run([str(BINAIRE)], env=env, capture_output=True, timeout=180, check=False)
        fichier = sortie / "project.json"
        return ((fichier.read_text(encoding="utf-8") if fichier.is_file() else ""),
                r.stderr.decode("utf-8", "replace"))

    for menu in menus:
        print(f"=== menu {menu} ===")
        temoin, _ = course(f"{menu}-temoin", menu, "")
        if not temoin:
            print("  RATÉ  le témoin n'a même pas écrit de projet")
            rates += 1
            continue
        for i, entree in enumerate(MENUS[menu]):
            texte, journal = course(f"{menu}-{i}", menu, entree)
            change = bool(texte) and texte != temoin
            # UNE FENÊTRE QUI S'OUVRE N'EST PAS UNE EXPLICATION. Les deux formes
            # passent par `VSM_BOITE`, et les confondre rendait cette garde
            # aveugle au défaut même qui l'a fait écrire : avec le correctif de
            # D275 retiré, elle restait verte, parce que la modale sans réponse
            # imprimait bien une ligne. Une REFUS s'écrit « titre : message » ;
            # une modale restée ouverte s'écrit « … sans réponse de banc ».
            sans_reponse = any("sans réponse de banc" in ligne
                               for ligne in journal.splitlines() if "VSM_BOITE" in ligne)
            dit = ("VSM_BOITE" in journal) and not sans_reponse
            excuse = VUE_SEULEMENT.get(entree) or DEJA_EN_PLACE.get(entree)
            vivant = change or dit or excuse is not None
            if not vivant:
                rates += 1
            etat = ("changé" if change else
                    "refusé et DIT" if dit else
                    (excuse or ("modale SANS RÉPONSE" if sans_reponse else "RIEN")))
            print(f"  {'OK  ' if vivant else 'MORT'} {entree:54s} {etat}")

    shutil.rmtree(brouillon, ignore_errors=True)
    print(f"=== {rates} geste(s) mort(s) ===")
    return 1 if rates else 0


if __name__ == "__main__":
    raise SystemExit(main())
