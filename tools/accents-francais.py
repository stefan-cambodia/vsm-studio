#!/usr/bin/env python3
"""LA GARDE DE D374 : UN TEXTE FRANÇAIS AFFICHÉ NE PERD PAS SES ACCENTS.

RÈGLE GARDÉE (25/09/2026). Les chaînes françaises de l'interface sont les CLÉS
du dictionnaire (`app/Source/ui/Langue.cpp`, premier membre de chaque paire) :
tout texte traduit par `tr()` y figure mot pour mot. Aucune ne doit contenir un
mot dont la forme sans accent N'EXISTE PAS en français — « deplacer »,
« parametre », « entree », « a la fois »…

D'OÙ ELLE VIENT. Un audit à l'écran (D374) a lu « Glisser : deplacer » dans
l'onglet Automation, à côté de « déplacer » dans l'onglet Tempo. Ce n'était pas
une coquille isolée : douze chaînes, écrites avant que le dépôt ne passe aux
littéraux `u8"…"`, avaient été tapées SANS AUCUN accent — des infobulles de la
barre de transport, de la console, de la liste des pistes. L'une d'elles
annonçait en plus « l'enregistrement AUDIO arrive en D3.4 », une phase livrée.

POURQUOI UNE LISTE FERMÉE, ET PAS « TOUT MOT QUI EXISTE ACCENTUÉ AILLEURS ».
Ce second critère a été essayé d'abord : il rend « change », « copie »,
« touche », « mesure » — des formes justes sans accent (« il change », « une
copie »). Une garde qui accuse des phrases justes s'apprend à ignorer (D266).
La liste ci-dessous ne porte que des formes FAUSSES sans accent ; elle
s'allonge quand un audit en trouve une autre.

    python3 tools/accents-francais.py            # 0 = rien, 1 = les fautes
    python3 tools/accents-francais.py FICHIER    # juger un autre dictionnaire
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Formes qui n'existent pas en français sans leur accent (mots entiers,
# comparés en minuscules). « a » et « ou » sont traités à part : ils sont
# justes seuls (« il a », « ou bien ») et ne se jugent qu'en tournure.
FAUTIVES = set("""
deplacer deplace selectionnez selectionner parametre parametres entree entrees
reglages reglage armee armees decompte metronome correlation negatif negative
disparait ecoute ecouter recoit ecrit ecrire precedente repere evenement
evenements numero periode deja etre tres apres creer duree defaut frequence
frequences fenetre fenetres etat modele repertoire echantillon echantillons
derniere premiere entierement deuxieme
""".split())
# RETIRÉS APRÈS L'ESSAI SUR LE DICTIONNAIRE ENTIER : les mots qui sont aussi de
# l'ANGLAIS (« Wave Sequence », un nom de machine ; present, element, region,
# reference, zero, stereo, resolution…) et ceux qui sont justes sans accent en
# français (« la branche », « il dépend » s'écrit mal mais « depend » est anglais,
# « départ » manque mais « depart » est anglais aussi). Une clé de ce dictionnaire
# porte des noms de machines et de paramètres anglais : une garde qui les accuse
# accuse des phrases justes.
TOURNURES = [r"\ba la fois\b", r"\ba l'ecoute\b", r"\bou aller\b", r"\ba l'enregistrement\b"]


def cles(texte: str) -> list[tuple[int, str]]:
    """Le premier littéral de chaque paire `{ "fr", "en" }`, avec sa ligne."""
    sortie = []
    for i, ligne in enumerate(texte.split("\n"), 1):
        m = re.match(r'\s*\{\s*(?:u8)?"((?:[^"\\]|\\.)*)"', ligne)
        if m:
            sortie.append((i, m.group(1)))
    return sortie


def fautes(texte: str) -> list[str]:
    rendu = []
    for i, cle in cles(texte):
        cle = cle.split("@")[0]   # « Poser@repere » : après @, un CONTEXTE (`trSelon`), jamais affiché
        mots = {w.lower() for w in re.findall(r"[A-Za-zÀ-ÿ]+", cle)}
        trouve = sorted(mots & FAUTIVES)
        trouve += [t for t in TOURNURES if re.search(t, cle.lower())]
        if trouve:
            rendu.append(f"Langue.cpp:{i}: {', '.join(trouve)} — « {cle[:110]} »")
    return rendu


def main() -> int:
    chemin = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        Path(__file__).resolve().parent.parent / "app/Source/ui/Langue.cpp"
    texte = chemin.read_text(encoding="utf-8")
    n = len(cles(texte))
    if n < 100:   # une garde qui ne lit rien ne garde rien (D263)
        print(f"accents-francais : {n} clé(s) lue(s) dans {chemin} — trop peu, lecture ratée ?")
        return 2
    trouvees = fautes(texte)
    for f in trouvees:
        print(f)
    print(f"accents-francais : {len(trouvees)} clé(s) sans accent sur {n} lues")
    return 1 if trouvees else 0


if __name__ == "__main__":
    sys.exit(main())
