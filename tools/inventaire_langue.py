#!/usr/bin/env python3
"""Inventaire A9 : les littéraux français d'app/Source qui atteignent l'écran sans tr().

    analyse/.venv/bin/python tools/inventaire_langue.py              # les comptes
    analyse/.venv/bin/python tools/inventaire_langue.py ECRAN        # et la liste
    analyse/.venv/bin/python tools/inventaire_langue.py --entetes    # les en-têtes
    analyse/.venv/bin/python tools/inventaire_langue.py --regle=large  # D101
    analyse/.venv/bin/python tools/inventaire_langue.py --machines [MACHINES]  # D103

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

L'ANGLE MORT (D101). Une chaîne n'entre au compte que si elle a l'air
française : un accent ou un mot de FRANCAIS -- c'est la règle `stricte`, le
défaut. « Navigateur » ou « Preset illisible » n'en ont pas. Deux règles de
plus, en option tant que la mesure n'a pas tranché (`--regle=`) :
  - `mots` : les mots-outils français que l'anglais n'emploie pas, et les
    élisions (MOTS) -- aucun mot de contenu choisi d'après les ratés connus ;
  - `position` : une chaîne qui a des lettres, sans allure d'identifiant,
    passée dans une instruction qui affiche (AFFICHAGE) ;
  - `large` : l'une ou l'autre.

LES NOMS DES MACHINES (D103). Ils ne sont pas dans `app/Source` : le moteur les
donne (`audio/plugins/`), par deux voies -- le nom ENREGISTRÉ
(`VSM_REGISTER_SYNTH_PLUGIN`), que montrent la liste des pistes et le
navigateur, et `machineName()`, que montre le rack. `--machines` les lit tous
deux et compte ceux qui portent une description entre parenthèses sans avoir de
clé -- une description comme « (batterie acoustique) » n'a ni accent ni
mot-outil : la parenthèse est la règle, hors les noms IDENTIQUES dans les deux
langues, nommés un par un.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

RACINE = Path(__file__).resolve().parents[1] / "app" / "Source"
MACHINES = Path(__file__).resolve().parents[1] / "audio" / "plugins"
FRANCAIS = re.compile(
    r"[éèêàâçùûôîïëœÉÈÊÀÇ]|\b(le|la|les|des|une|un|aucun|aucune|piste|pistes|réglage|"
    r"fichier|projet|ouvrir|enregistrer|lecture|arrêt|départ|touche|choisir|dossier|"
    r"réserve|sans|avec|pour|dans|sur)\b", re.I)
MOTS = re.compile(
    r"\b(de|du|et|est|pas|ne|au|aux|ce|cette|ces|qui|que|son|sa|ses|leur|leurs|puis|rien|"
    r"tout|tous|toute|toutes|vers|votre|vos)\b|\b(?:[ldnscj]|qu)['’]\w", re.I)
AFFICHAGE = re.compile(
    r"\b(montrerBoite|showMessageBoxAsync|setButtonText|setText|setTooltip|addItem|addSectionHeader|"
    r"addTab|drawText|drawFittedText|setTitle|addTextEditor|PanelWindow)\b")
REGLES = ("stricte", "mots", "position", "large")
IDENTIQUES = {"Test Tone (reference)", "Test Tone (reference Phase 2)"}
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


def ressemble_a_du_texte(chaine: str) -> bool:
    """D101, règle `position` : des lettres, et pas l'allure d'un identifiant."""
    if not re.search(r"[A-Za-zÀ-ÿ]{2}", chaine):
        return False
    return " " in chaine or not re.search(r"[._/:]|^[a-z]", chaine)


def a_l_air_francaise(chaine: str, avant: str, regle: str) -> bool:
    """La chaîne entre-t-elle au compte ? `avant` : son instruction, jusqu'à elle."""
    if FRANCAIS.search(chaine):
        return True
    if regle in ("mots", "large") and MOTS.search(chaine):
        return True
    return regle in ("position", "large") and bool(AFFICHAGE.search(avant)) and ressemble_a_du_texte(chaine)


def inventaire(racine: Path = RACINE, motif: str = "*.cpp", regle: str = "stricte") -> Dict[str, List[str]]:
    assert regle in REGLES, regle
    cles = cles_de_la_table((racine / "ui" / "Langue.cpp").read_text(encoding="utf-8"))
    comptes: Dict[str, List[str]] = {c: [] for c in CATEGORIES}
    for fichier in sorted(racine.rglob(motif)):
        if fichier.name == "Langue.cpp" or "tools" in fichier.relative_to(racine).parts:
            continue
        texte = sans_commentaires(fichier.read_text(encoding="utf-8"))
        for debut, fin, brut in chaines(texte):
            chaine = decode(brut)
            avant = texte[max(texte.rfind(c, 0, debut) for c in ";{}") + 1:debut]
            if chaine.startswith("VSM_") or not a_l_air_francaise(chaine, avant, regle):
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


def noms_de_machines(plugins: Path = MACHINES) -> List[Tuple[str, str]]:
    """D103 : (dossier, nom) -- les noms enregistrés et les `machineName()` des machines."""
    noms: Set[Tuple[str, str]] = set()
    for fichier in sorted(plugins.rglob("*.[ch]*")):
        texte = fichier.read_text(encoding="utf-8", errors="replace")
        dossier = fichier.relative_to(plugins).parts[0]
        for motif in (r'VSM_REGISTER_SYNTH_PLUGIN\(\s*"[^"]+"\s*,\s*"((?:[^"\\]|\\.)*)"',
                      r'machineName\(\)\s*const[^{;]*\{\s*return\s*"((?:[^"\\]|\\.)*)"'):
            for m in re.finditer(motif, texte):
                noms.add((dossier, decode(m.group(1)).replace("\\'", "'")))
    return sorted(noms)


def machines_sans_cle(racine: Path = RACINE, plugins: Path = MACHINES
                      ) -> Tuple[List[Tuple[str, str]], List[Tuple[str, str]], List[Tuple[str, str]]]:
    """D103 : (tous les noms, ceux qui portent une description, ceux-là sans clé)."""
    cles = cles_de_la_table((racine / "ui" / "Langue.cpp").read_text(encoding="utf-8"))
    tous = noms_de_machines(plugins)
    decrits = [(d, n) for d, n in tous if "(" in n and n not in IDENTIQUES]
    return tous, decrits, [(d, n) for d, n in decrits if n not in cles]


def main() -> int:
    options = [a for a in sys.argv[1:] if a.startswith("--")]
    arguments = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--machines" in options:
        tous, decrits, sans = machines_sans_cle()
        print(f"MACHINES {len(tous)} noms   DECRITS {len(decrits)}   SANS_CLE {len(sans)}")
        if "MACHINES" in arguments:
            for dossier, nom in sans:
                print(f"  M {dossier}: {nom}")
        return 0
    regle = next((o.split("=", 1)[1] for o in options if o.startswith("--regle=")), "stricte")
    if regle not in REGLES:
        print(f"règle inconnue : {regle} (attendu : {', '.join(REGLES)})", file=sys.stderr)
        return 2
    comptes = inventaire(motif="*.h" if "--entetes" in options else "*.cpp", regle=regle)
    print("   ".join(f"{c} {len(v)}" for c, v in comptes.items()))
    for categorie in arguments:
        for entree in comptes.get(categorie.upper(), []):
            print(f"  {categorie[0].upper()} {entree}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
