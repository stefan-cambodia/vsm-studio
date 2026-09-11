#!/usr/bin/env python3
"""Inventaire A9 : les littéraux français d'app/Source qui atteignent l'écran sans tr().

    analyse/.venv/bin/python tools/inventaire_langue.py              # les comptes
    analyse/.venv/bin/python tools/inventaire_langue.py ECRAN        # et la liste
    analyse/.venv/bin/python tools/inventaire_langue.py --entetes    # les en-têtes

LES EN-TÊTES À PART (D94). Le compte d'A9 ne lit que les `.cpp`, et c'est ce
compte-là que les phases comparent. Mais un `.h` écrit aussi à l'écran (une
infobulle posée dans une fonction en ligne, un nom de pièce de batterie) :
`--entetes` applique les mêmes règles aux en-têtes, en chiffre séparé, pour que
l'angle mort se mesure au lieu de se deviner.

POURQUOI UN INVENTAIRE DU CODE, EN PLUS DE CE QUI S'AFFICHE. `VSM_TEXTES_LISTE`
lit les textes que la fenêtre montre ; elle ne voit ni une boîte qu'on n'a pas
ouverte, ni une infobulle d'un panneau caché, ni ce que `paint()` dessine. Le
code, lui, contient tout. Les deux se complètent : l'inventaire dit ce qui
RESTE, la liste dit ce qui SE VOIT.

LES RÈGLES, écrites avant de compter (D94, ROADMAP-daw.md) :
  - des littéraux ADJACENTS (« u8"a" u8"b" ») sont une seule chaîne ;
  - une chaîne passée à tr(), trSelon(), trPhrase(), trGeste() ne compte pas...
  - ...SAUF si la table n'a pas sa clé : SANS_PAIRE, elle reste française en
    anglais aussi sûrement qu'une chaîne écrite sans tr() (trouvé par D94 :
    « Écoute A/B : … » et « Annuler (Ctrl+Z) » étaient dans ce cas) ;
  - TABLE : la chaîne est une clé de kAnglais ou un modèle de kModeles --
    traduite ailleurs, par une variable ;
  - COMMANDE : un mot ASCII minuscule, avec tirets ou deux-points, sans espace
    (« monter-piste ») -- un jeton de banc, pas un texte ;
  - TERMINAL : l'instruction écrit sur stderr ou stdout ;
  - ÉCRAN : tout le reste. C'est le chiffre d'A9.
`Langue.cpp` et `app/Source/tools/` sont hors du compte.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

RACINE = Path(__file__).resolve().parents[1] / "app" / "Source"
FRANCAIS = re.compile(
    r"[éèêàâçùûôîïëœÉÈÊÀÇ]|\b(le|la|les|des|une|un|aucun|aucune|piste|pistes|réglage|"
    r"fichier|projet|ouvrir|enregistrer|lecture|arrêt|départ|touche|choisir|dossier|"
    r"réserve|sans|avec|pour|dans|sur)\b", re.I)
LITTERAL = re.compile(r'(?:u8)?"((?:[^"\\]|\\.)*)"')
TRADUCTION = re.compile(r"\b(tr|trSelon|trPhrase|trGeste|translate|TRANS)\s*\(\s*(u8)?\s*$")
SORTIE = re.compile(r"fputs|stderr|stdout|std::cout|std::cerr|DBG\s*\(|printf")
CATEGORIES = ("ECRAN", "SANS_PAIRE", "TERMINAL", "TABLE", "COMMANDE")


def sans_commentaires(texte: str) -> str:
    """Retire les commentaires en gardant les numéros de ligne."""
    texte = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), texte, flags=re.S)
    return "\n".join(re.sub(r'//(?=(?:[^"]*"[^"]*")*[^"]*$).*', "", ligne)
                     for ligne in texte.split("\n"))


def decode(litteral: str) -> str:
    """Le texte d'un littéral qui écrit ses accents en \\u00e9 ou \\xc3\\xa9."""
    if "\\u" in litteral:
        return re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), litteral)
    if "\\x" in litteral:
        octets = re.sub(r"\\x([0-9a-fA-F]{2})", lambda m: chr(int(m.group(1), 16)), litteral)
        return octets.encode("latin-1", "ignore").decode("utf-8", "ignore")
    return litteral


def cles_de_la_table(langue: str) -> Set[str]:
    """Les clés françaises de kAnglais et de kModeles, littéraux adjacents recollés."""
    cles: Set[str] = set()
    for m in re.finditer(r'\{\s*((?:(?:u8)?"(?:[^"\\]|\\.)*"\s*)+),', langue):
        cles.add(decode("".join(LITTERAL.findall(m.group(1)))))
    return cles


def chaines(texte: str) -> List[Tuple[int, int, str]]:
    """(début, fin, texte) de chaque chaîne, les littéraux adjacents recollés."""
    groupes: List[Tuple[int, int, str]] = []
    for m in LITTERAL.finditer(texte):
        if groupes and texte[groupes[-1][1]:m.start()].strip() in ("", "u8"):
            debut, _, deja = groupes[-1]
            groupes[-1] = (debut, m.end(), deja + m.group(1))
        else:
            groupes.append((m.start(), m.end(), m.group(1)))
    return groupes


def inventaire(racine: Path = RACINE, motif: str = "*.cpp") -> Dict[str, List[str]]:
    cles = cles_de_la_table((racine / "ui" / "Langue.cpp").read_text(encoding="utf-8"))
    comptes: Dict[str, List[str]] = {c: [] for c in CATEGORIES}
    for fichier in sorted(racine.rglob(motif)):
        if fichier.name == "Langue.cpp" or "tools" in fichier.relative_to(racine).parts:
            continue
        texte = sans_commentaires(fichier.read_text(encoding="utf-8"))
        for debut, fin, brut in chaines(texte):
            chaine = decode(brut)
            if not FRANCAIS.search(chaine) or chaine.startswith("VSM_"):
                continue
            if TRADUCTION.search(texte[max(0, debut - 24):debut]):
                # trSelon(clé, contexte) et trPhrase (modèles) ont leurs propres
                # tables : seule la clé nue de tr() se vérifie ici.
                appel = TRADUCTION.search(texte[max(0, debut - 24):debut])
                if appel is not None and appel.group(1) == "tr" and chaine not in cles:
                    ligne = texte.count("\n", 0, debut) + 1
                    comptes["SANS_PAIRE"].append(
                        f"{fichier.relative_to(racine)}:{ligne}: {chaine[:100]}")
                continue
            instruction = texte[texte.rfind(";", 0, debut) + 1:texte.find(";", fin)]
            if re.fullmatch(r"[a-z0-9\-:]+", chaine):
                categorie = "COMMANDE"
            elif SORTIE.search(instruction):
                categorie = "TERMINAL"
            elif chaine in cles:
                categorie = "TABLE"
            else:
                categorie = "ECRAN"
            ligne = texte.count("\n", 0, debut) + 1
            comptes[categorie].append(f"{fichier.relative_to(racine)}:{ligne}: {chaine[:100]}")
    return comptes


def main() -> int:
    arguments = [a for a in sys.argv[1:] if a != "--entetes"]
    comptes = inventaire(motif="*.h" if "--entetes" in sys.argv[1:] else "*.cpp")
    print("   ".join(f"{c} {len(v)}" for c, v in comptes.items()))
    for categorie in arguments:
        for entree in comptes.get(categorie.upper(), []):
            print(f"  {categorie[0].upper()} {entree}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
