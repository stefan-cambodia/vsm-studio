#!/usr/bin/env python3
"""LA GARDE DE D444-D445 : UN NOMBRE DÉCIMAL TAPÉ SE LIT AVEC SA VIRGULE.

RÈGLE GARDÉE (27/09/2026) : dans `app/Source/`, tout rappel qui convertit un
texte TAPÉ par l'utilisateur en nombre décimal -- le corps d'un
`valueFromTextFunction` ou d'un `onTextChange` qui appelle `getDoubleValue` ou
`getFloatValue` -- doit passer par `lireNombreSaisi` (BulleDeValeur.h) ou traiter
la virgule lui-même (`replaceCharacter(',', '.')`).

POURQUOI CETTE GARDE EXISTE. D444 : « 120,5 » tapé dans le tempo devenait
« 1205 », refusé sans un mot. D445 : le trim « 3,5 » donnait 24 dB (le maximum),
le délai « 12,5 » donnait 125 ms, le fader « -6,5 » donnait -6. JUCE lit le
point ; un utilisateur français tape la virgule.

LIMITE, dite : la garde lit le texte des rappels posés sous la forme
`x.valueFromTextFunction = [..] { … }` ou `x.onTextChange = [..] { … }`. Les
entiers (`getIntValue`) ne sont pas jugés : une virgule y tronque, elle ne
multiplie pas.

  tools/virgule-saisie.py [fichier …]   (défaut : app/Source/**/*.cpp et *.h)

Rend 0 si aucun rappel en défaut, 1 sinon (chacun nommé).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
RAPPEL = re.compile(r"([\w\->\.\[\]\*]+?)(?:->|\.)(valueFromTextFunction|onTextChange)\s*=\s*\[[^\]]*\]\s*(?:\([^)]*\))?\s*\{")
DECIMAL = re.compile(r"get(Double|Float)Value\s*\(")
VIRGULE = re.compile(r"lireNombreSaisi|replaceCharacter\s*\(\s*','")


def corps(texte: str, debut: int) -> str:
    profondeur, fin = 1, debut
    while profondeur and fin < len(texte):
        profondeur += {"{": 1, "}": -1}.get(texte[fin], 0)
        fin += 1
    return texte[debut:fin]


def juger(fichier: Path) -> tuple[int, list[str]]:
    texte = fichier.read_text(encoding="utf-8", errors="replace")
    vus, fautes = 0, []
    for m in RAPPEL.finditer(texte):
        c = corps(texte, m.end())
        if not DECIMAL.search(c):
            continue
        vus += 1
        if not VIRGULE.search(c):
            nom = fichier.relative_to(RACINE) if fichier.is_relative_to(RACINE) else fichier
            ligne = texte.count("\n", 0, m.start()) + 1
            fautes.append(f"RATÉ {nom}:{ligne}: « {m.group(1)}.{m.group(2)} » lit un décimal sans la virgule")
    return vus, fautes


CASE_EDITABLE = re.compile(r"([\w\.\->]+?)(?:->|\.)setTextBoxStyle\s*\(\s*juce::Slider::TextBox(Below|Above|Left|Right)\s*,\s*false")


def cases_sans_lecture(fichier: Path) -> tuple[int, list[str]]:
    """SECONDE RÈGLE : une case ÉDITABLE sans `valueFromTextFunction` lit par JUCE,
    qui s'arrête à la virgule (le fader et les réglages d'effet de D445 -- la
    première règle ne les voyait pas : ils n'avaient pas de rappel du tout).
    Le rappel est cherché sur le MÊME objet (ou son alias `raw = x.get()`), de
    quinze lignes avant la case à quarante après. Une première version cherchait
    « valueFromTextFunction » tout court dans les quarante lignes suivantes : le
    swing passait grâce au rappel de la VÉLOCITÉ, sa voisine (vu en construisant
    la garde)."""
    lignes = fichier.read_text(encoding="utf-8", errors="replace").split("\n")
    vues, fautes = 0, []
    for i, ligne in enumerate(lignes):
        if not CASE_EDITABLE.search(ligne):
            continue
        vues += 1
        m = CASE_EDITABLE.search(ligne)
        objet = m.group(1)
        fenetre = "\n".join(lignes[max(0, i - 15):i + 40])
        noms = {objet} | set(re.findall(r"(\w+)\s*=\s*" + re.escape(objet) + r"\.get\(\)", fenetre))
        if not any(re.search(re.escape(n) + r"(?:->|\.)valueFromTextFunction", fenetre) for n in noms):
            nom = fichier.relative_to(RACINE) if fichier.is_relative_to(RACINE) else fichier
            fautes.append(f"RATÉ {nom}:{i + 1}: case éditable lue par JUCE (sans la virgule) : « {ligne.strip()[:70]} »")
    return vues, fautes


CHAMP_DE_FENETRE = re.compile(r"getTextEditorContents\s*\([^)]*\)\s*\.\s*get(Double|Float)Value\s*\(")


def champs_sans_virgule(fichier: Path) -> tuple[int, list[str]]:
    """TROISIÈME RÈGLE (D448) : le champ d'une FENÊTRE (`AlertWindow`) lu par
    `getDoubleValue` s'arrête à la virgule -- la queue des exports lisait « 1,5 »
    comme 1 s. Les deux premières règles ne voyaient que curseurs et libellés."""
    lignes = fichier.read_text(encoding="utf-8", errors="replace").split("\n")
    fautes = []
    for i, ligne in enumerate(lignes):
        if CHAMP_DE_FENETRE.search(ligne):
            nom = fichier.relative_to(RACINE) if fichier.is_relative_to(RACINE) else fichier
            fautes.append(f"RATÉ {nom}:{i + 1}: champ de fenêtre lu sans la virgule : « {ligne.strip()[:70]} »")
    return len(fautes), fautes


def main() -> int:
    fichiers = [Path(a).resolve() for a in sys.argv[1:]] or sorted(
        list((RACINE / "app" / "Source").rglob("*.cpp")) + list((RACINE / "app" / "Source").rglob("*.h")))
    vus, toutes = 0, []
    for f in fichiers:
        v, fautes = juger(f)
        c, fautes_cases = cases_sans_lecture(f)
        _, fautes_champs = champs_sans_virgule(f)
        vus += v + c
        toutes += fautes + fautes_cases + fautes_champs
    for faute in toutes:
        print(faute)
    print(f"VIRGULE SAISIE : {vus} lecture(s) et case(s) jugée(s), {len(toutes)} sans la virgule")
    return 1 if toutes else 0


if __name__ == "__main__":
    sys.exit(main())
