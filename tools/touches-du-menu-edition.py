#!/usr/bin/env python3
"""Une entrée du menu Édition qui fait ce que fait une touche AFFICHE cette touche.

    python3 tools/touches-du-menu-edition.py
    python3 tools/touches-du-menu-edition.py --tout   (liste aussi les entrées tenues)

LA RÈGLE GARDÉE (28/09/2026, D471). Le menu Édition EST le menu contextuel du
piano roll (D80), construit dans `PianoRollComponent::buildContextMenu`. Quand
une de ses entrées appelle EXACTEMENT ce qu'appelle une commande de la table des
raccourcis, elle doit passer par `ajouterAvecRaccourci(…, ShortcutId::…)` : JUCE
dessine alors la touche EFFECTIVE de l'utilisateur à droite de l'entrée — « Annuler
… Ctrl+Z », comme chez Cubase et Live. Jusqu'à D471, seize entrées l'appelaient
par `addItem` nu et taisaient Ctrl+Z, Ctrl+C, Ctrl+V… (D155 avait posé la règle
en ne lisant que `MainComponent`).

L'APPARIEMENT SE FAIT PAR L'ACTION, JAMAIS PAR LE LIBELLÉ (leçon de D155 : le
libellé range « Copier la chaîne d'inserts » sous *Copier*). La garde lit les deux
aiguillages du piano roll :

  * `performShortcut`        — `case Id::EditCopy: copySelection(); return true;`
  * `performContextMenuAction` — `case kCtxCopy: copySelection(); break;`

et apparie une commande et une entrée quand leurs corps sont IDENTIQUES (espaces
retirés). « Quantifier (50 %) » (`quantizeSelection(0.5f, false)`) n'est donc pas
appariée à Ctrl+Q (`quantizeSelection(1.0f, false)`), et c'est juste.

CE QU'ELLE NE VOIT PAS, DIT PLUTÔT QUE TU : les touches FIXES (les flèches, qui
transposent) ne passent pas par `performShortcut` ; leurs entrées (« Transposer
+1 demi-ton », « Octave + »…) sont tenues à la main dans `buildContextMenu`.

Rend 0 si chaque entrée appariée passe par la table avec SA commande, 1 sinon,
2 si un aiguillage ne se lit plus (la garde ne mesure alors rien, et le dit).
"""
import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
SOURCE = RACINE / "app/Source/ui/PianoRollComponent.cpp"


def corps_de_fonction(texte: str, signature: str) -> str:
    debut = texte.find(signature)
    if debut < 0:
        return ""
    ouvrante = texte.find("{", debut)
    profondeur, i = 0, ouvrante
    while i < len(texte):
        if texte[i] == "{":
            profondeur += 1
        elif texte[i] == "}":
            profondeur -= 1
            if profondeur == 0:
                return texte[ouvrante:i + 1]
        i += 1
    return ""


def sans_commentaires(code: str) -> str:
    return re.sub(r"//[^\n]*", "", code)


def normaliser(corps: str) -> str:
    return re.sub(r"\s+", "", corps)


def instruction_autour(code: str, position: int) -> str:
    """L'instruction qui contient `position` : du dernier `;`, `{` ou `}` hors
    chaîne jusqu'au `;` qui ferme, parenthèses équilibrées."""
    debut = max(code.rfind(";", 0, position), code.rfind("{", 0, position), code.rfind("}", 0, position)) + 1
    profondeur, i, dans_chaine = 0, debut, False
    while i < len(code):
        c = code[i]
        if c == '"' and code[i - 1] != "\\":
            dans_chaine = not dans_chaine
        elif not dans_chaine:
            if c == "(":
                profondeur += 1
            elif c == ")":
                profondeur -= 1
            elif c == ";" and profondeur == 0:
                return code[debut:i + 1]
        i += 1
    return code[debut:]


def main() -> int:
    tout = "--tout" in sys.argv
    texte = SOURCE.read_text(encoding="utf-8")
    raccourcis = sans_commentaires(corps_de_fonction(texte, "bool PianoRollComponent::performShortcut("))
    actions = sans_commentaires(corps_de_fonction(texte, "void PianoRollComponent::performContextMenuAction("))
    menu = sans_commentaires(corps_de_fonction(texte, "juce::PopupMenu PianoRollComponent::buildContextMenu() const"))
    if not raccourcis or not actions or not menu:
        print("REFUS : un des trois corps de fonction ne se lit plus (signature changée ?)")
        return 2

    # Les deux formes de `case`, chacune vérifiée sur son compte (une regex qui
    # trie du code se valide sur chaque forme avant de servir de mesure — D149).
    commandes = {m.group(1): normaliser(m.group(2))
                 for m in re.finditer(r"case\s+Id::(\w+)\s*:\s*(.*?)\s*return\s+true\s*;", raccourcis, re.S)}
    entrees = {m.group(1): normaliser(m.group(2))
               for m in re.finditer(r"case\s+(kCtx\w+)\s*:\s*(.*?)\s*break\s*;", actions, re.S)}
    if len(commandes) < 20 or len(entrees) < 40:
        print(f"REFUS : {len(commandes)} commande(s) et {len(entrees)} entrée(s) lues — trop peu, "
              "la forme des `case` a changé")
        return 2

    paires = sorted((entree, commande)
                    for entree, corps in entrees.items()
                    for commande, corps_touche in commandes.items()
                    if corps and corps == corps_touche)
    print("=== D471 : le menu Édition affiche la touche de ce qu'il fait ===")
    print(f"  {len(commandes)} commande(s) du piano roll, {len(entrees)} entrée(s) de menu, "
          f"{len(paires)} paire(s) par la fonction appelée")
    muettes = 0
    for entree, commande in paires:
        position = re.search(rf"\b{entree}\b", menu)
        if position is None:
            print(f"  ABSENTE  {entree} ({commande}) : l'aiguillage la connaît, le menu ne la pose pas")
            muettes += 1
            continue
        instruction = instruction_autour(menu, position.start())
        # Les deux graphies : `ShortcutId::EditCopy`, ou l'alias `Id::EditCopy`
        # que `buildContextMenu` déclare (la première version n'acceptait que la
        # première, et déclarait muettes seize entrées qui ne l'étaient plus).
        tenue = ("ajouterAvecRaccourci" in instruction
                 and re.search(rf"\b(?:ShortcutId|Id)::{commande}\b", instruction) is not None)
        if not tenue:
            muettes += 1
            print(f"  MUETTE   {entree} ↔ {commande} : « {' '.join(instruction.split())[:90]} »")
        elif tout:
            print(f"  tenue    {entree} ↔ {commande}")
    print(f"--- {muettes} entrée(s) muette(s) sur {len(paires)}")
    return 1 if muettes else 0


if __name__ == "__main__":
    sys.exit(main())
