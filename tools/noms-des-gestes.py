#!/usr/bin/env python3
"""Un geste a plusieurs PORTES ; il ne doit pas avoir plusieurs NOMS.

    python3 tools/noms-des-gestes.py
    python3 tools/noms-des-gestes.py --tout    (montre aussi les gestes d'accord)

LA RÈGLE GARDÉE (18/09/2026, D355). Le même geste du piano roll s'atteint par
trois portes : la **fenêtre des raccourcis** (`ShortcutTable.cpp`, que
`ShortcutsWindow` affiche à l'utilisateur), l'**entrée de menu** (le menu du clic
droit, qui EST le menu Édition — `MainComponent.cpp:3347`) et parfois un **bouton
de la barre d'outils**. Chacune porte un libellé, et ces libellés sont ce que
l'utilisateur LIT. **Deux portes du même geste doivent se reconnaître l'une
l'autre** : le même texte, ou l'un commence par l'autre (« Quantifier » et
« Quantifier (100 %) » se reconnaissent ; « Joindre » et « Fusionner », non).

UN BOUTON A DEUX NOMS, ET C'EST LÉGITIME. Sa face peut être un symbole — « + »,
« - », « Tout » —, parce qu'une barre d'outils serrée ne tient pas des phrases ;
c'est alors son **infobulle** qui nomme la commande. Un bouton satisfait donc la
règle si sa face OU son infobulle reconnaît les autres portes. Sans cette
exception, « + » pour « Zoom avant » serait compté comme un défaut, ce qu'il
n'est pas.

**ET LA RÈGLE VAUT DANS LES DEUX LANGUES.** L'interface est bilingue (D78, D80) :
un geste peut s'accorder en français et diverger en anglais, ou l'inverse — et
c'est arrivé aux deux sens le 18/09. « Joindre » et « Fusionner » se traduisent
tous deux par *Join* : l'anglais était juste quand le français mentait. À
l'inverse, « Tout désélectionner » donne *Select none* et « Ne rien
sélectionner » *Select nothing* : les deux langues divergeaient, chacune à sa
façon. Le contrôle est donc joué deux fois, sur le français des sources et sur
l'anglais de `Langue.cpp`.

POURQUOI CETTE GARDE. Un utilisateur qui lit « Joindre — Ctrl+J » dans la fenêtre
des raccourcis cherche « Joindre » dans le menu et n'y trouve rien : le menu dit
« Fusionner ». Il en conclut que la commande n'existe pas là, ou qu'il s'est
trompé. Aucun test ne voit ce défaut — les deux libellés sont justes, chacun chez
soi —, et il ne se remarque qu'en se servant du logiciel dans les deux sens.

L'APPARIEMENT SE FAIT PAR L'APPEL, JAMAIS PAR LE NOM. Deux portes sont le même
geste quand elles appellent la MÊME fonction avec les MÊMES arguments :
`quantizeSelection(1.0f, false)` d'un côté et de l'autre. Apparier par le nom
supposerait résolu ce que l'on mesure, et `quantizeSelection(0.5f, false)` —
« Quantifier (50 %) », un geste voisin sans raccourci — ne doit surtout pas être
apparié au premier.

CE QU'ELLE NE VOIT PAS, et le dit plutôt que de compter zéro : les entrées de
menu dont le libellé est CALCULÉ (les accords, bâtis sur `chordTypeName`) n'ont
pas de texte littéral à lire ; elles sont comptées à part, et ne sont d'ailleurs
atteintes par aucun raccourci.

Rend 0 si tous les gestes à plusieurs portes portent un nom reconnaissable,
1 sinon, 2 si un fichier attendu manque ou si une forme de code n'est plus lue.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
PIANO = RACINE / "app/Source/ui/PianoRollComponent.cpp"
TABLE = RACINE / "interchange/src/ShortcutTable.cpp"
BARRE_CPP = RACINE / "app/Source/ui/PianoRollToolbar.cpp"
BARRE_H = RACINE / "app/Source/ui/PianoRollToolbar.h"
LANGUE = RACINE / "app/Source/ui/Langue.cpp"


def bloc(source: str, debut: str) -> str:
    """Le corps d'une fonction, de sa signature à l'accolade de fin en colonne 0."""
    i = source.index(debut)
    return source[i : source.index("\n}", i)]


def premier_tr(texte: str) -> str | None:
    """Le premier `tr("…")` d'une expression — une entrée de menu écrite avec une
    condition (« Annuler » / « Annuler : <geste> ») en porte deux, et c'est le
    début du libellé qui compte pour se reconnaître."""
    m = re.search(r'tr\(\s*(?:u8)?"((?:[^"\\]|\\.)*)"', texte)
    return m.group(1) if m else None


def lire_anglais() -> dict[str, str]:
    """La table de `Langue.cpp` : français exact -> anglais. Une clé absente veut
    dire « pas traduit », et `tr()` rend alors le français : c'est ce que la
    lecture rend aussi, pour que le contrôle anglais voie ce que l'écran voit."""
    texte = LANGUE.read_text(encoding="utf-8")
    debut = texte.index("const Paire kAnglais[]")
    fin = texte.index("\n};", debut)
    paires = re.findall(
        r'\{\s*(?:u8)?"((?:[^"\\]|\\.)*)"\s*,\s*(?:u8)?"((?:[^"\\]|\\.)*)"\s*\}',
        texte[debut:fin],
    )
    return {fr: en for fr, en in paires}


def lire_portes() -> tuple[dict[str, dict[str, list[str]]], list[str], list[str]]:
    """Rend {appel: {porte: [libellés]}}, les entrées au libellé calculé, et les
    anomalies de lecture (une forme de code que les motifs ne lisent plus).

    Une porte peut porter PLUSIEURS noms : un bouton a sa face et son infobulle."""
    piano = PIANO.read_text(encoding="utf-8")
    table = TABLE.read_text(encoding="utf-8")
    anomalies: list[str] = []
    calcules: list[str] = []

    # (1) identifiant de raccourci -> appel, et -> libellé de la fenêtre des raccourcis.
    raccourci_appel = dict(
        re.findall(r"case Id::(\w+):\s*([^;]+);\s*return true;", bloc(piano, "bool PianoRollComponent::performShortcut"))
    )
    libelle_raccourci = {
        m[0]: m[1]
        for m in re.findall(r'\{ShortcutId::(\w+),\s*"[^"]*",\s*"[^"]*",\s*"((?:[^"\\]|\\.)*)"', table)
    }
    if not raccourci_appel or not libelle_raccourci:
        anomalies.append("aucun raccourci lu : les motifs ne correspondent plus au code")

    # (2) identifiant d'entrée de menu -> appel.
    corps_menu = bloc(piano, "void PianoRollComponent::performContextMenuAction")
    menu_appel = dict(re.findall(r"case (kCtx\w+):\s*([^;]+);\s*break;", corps_menu))

    # (3) identifiant d'entrée de menu -> libellé. Le motif prend l'identifiant,
    # puis le PREMIER `tr("…")` qui suit dans le même appel à `addItem` : une
    # entrée conditionnelle (Annuler, Replier) en porte deux, une entrée calculée
    # (les accords) aucun -- et celle-là est comptée à part, pas oubliée.
    construction = bloc(piano, "juce::PopupMenu PianoRollComponent::buildContextMenu")
    libelle_menu: dict[str, str] = {}
    for m in re.finditer(r"addItem\(\s*(kCtx\w+)\s*,", construction):
        ident = m.group(1)
        if ident in libelle_menu:
            continue
        lib = premier_tr(construction[m.end() : m.end() + 400])
        if lib is None:
            calcules.append(ident)
        else:
            libelle_menu[ident] = lib
    manquants = sorted(set(menu_appel) - set(libelle_menu) - set(calcules))
    if manquants:
        anomalies.append("entrées de menu sans libellé lu : " + ", ".join(manquants))

    # (4) bouton de la barre d'outils -> appel, -> face du bouton, -> infobulle.
    barre = BARRE_CPP.read_text(encoding="utf-8")
    entete = BARRE_H.read_text(encoding="utf-8")
    texte_bouton = {
        m[0]: m[1]
        for m in re.findall(r'(\w+Button_)\s*\{\s*(?:u8)?"((?:[^"\\]|\\.)*)"', entete)
    }
    # D358 : DEUX FORMES D'INFOBULLE, parce que la moitié d'entre elles composent
    # désormais leur touche (`avecTouche(tr("…"), …)`) au lieu de l'écrire. Le
    # motif d'origine ne lisait que la forme directe : les trois zooms ont
    # aussitôt paru « à plusieurs noms », leur infobulle étant devenue invisible
    # à la garde. Une garde qui ne connaît qu'une forme du code rend un défaut
    # là où il n'y en a pas -- le pendant exact du « zéro » de D354.
    infobulle = {
        m[0]: m[1]
        for m in re.findall(
            r'(\w+Button_)\.setTooltip\((?:\s*(?:vsm::app::ui::)?(?:avecTouche|libelleAvecTouche)\()?'
            r'\s*(?:vsm::app::ui::)?tr\(\s*(?:u8)?"((?:[^"\\]|\\.)*)"',
            barre)
    }
    bouton_appel = dict(re.findall(r"(\w+Button_)\.onClick\s*=\s*\[this\]\s*\{\s*pianoRoll_\.([^;]+);", barre))
    if not bouton_appel:
        anomalies.append("aucun bouton de barre d'outils lu : le motif ne correspond plus au code")

    portes: dict[str, dict[str, list[str]]] = {}
    for ident, appel in raccourci_appel.items():
        if ident in libelle_raccourci:
            portes.setdefault(appel.strip(), {})["raccourci"] = [libelle_raccourci[ident]]
    for ident, appel in menu_appel.items():
        if ident in libelle_menu:
            portes.setdefault(appel.strip(), {})["menu"] = [libelle_menu[ident]]
    for ident, appel in bouton_appel.items():
        noms = [texte_bouton[ident]] if ident in texte_bouton else []
        if ident in infobulle:
            noms.append(infobulle[ident])
        if noms:
            portes.setdefault(appel.strip(), {})["bouton"] = noms
    return portes, calcules, anomalies


def se_reconnaissent(a: str, b: str) -> bool:
    """Deux libellés du même geste : le même texte, ou l'un commence par l'autre."""
    x, y = a.strip(), b.strip()
    return x == y or x.startswith(y) or y.startswith(x)


def accorde(portes: dict[str, list[str]]) -> bool:
    """Le geste est bien nommé s'il existe UN nom que toutes ses portes
    reconnaissent — chaque porte par au moins un des noms qu'elle affiche."""
    candidats = [nom for noms in portes.values() for nom in noms]
    return any(
        all(any(se_reconnaissent(nom, autre) for autre in noms) for noms in portes.values())
        for nom in candidats
    )


def main() -> int:
    tout = "--tout" in sys.argv
    for fichier in (PIANO, TABLE, BARRE_CPP, BARRE_H):
        if not fichier.is_file():
            print(f"REFUS : {fichier} est absent")
            return 2

    if not LANGUE.is_file():
        print(f"REFUS : {LANGUE} est absent")
        return 2
    portes, calcules, anomalies = lire_portes()
    for mot in anomalies:
        print(f"REFUS : {mot}")
    if anomalies:
        return 2
    anglais = lire_anglais()
    if len(anglais) < 100:
        print(f"REFUS : {len(anglais)} paires lues dans Langue.cpp — le motif ne lit plus la table")
        return 2

    plusieurs = {appel: noms for appel, noms in portes.items() if len(noms) > 1}
    print("=== D355 : un geste, plusieurs portes, un seul nom ===")
    print(f"       {len(portes)} geste(s) atteignable(s), dont {len(plusieurs)} par plusieurs portes ;")
    print(f"       {len(calcules)} entrée(s) au libellé calculé, hors du contrôle : {', '.join(calcules) or 'aucune'} ;")
    print(f"       {len(anglais)} paires de traduction lues.")
    ecarts = 0
    for appel, noms in sorted(plusieurs.items()):
        # NON TRADUIT = LE FRANÇAIS, comme `tr()` : le contrôle anglais voit
        # alors exactement ce que l'écran montre, plutôt qu'un trou.
        en_noms = {porte: [anglais.get(n, n) for n in liste] for porte, liste in noms.items()}
        bon_fr, bon_en = accorde(noms), accorde(en_noms)
        if not (bon_fr and bon_en):
            ecarts += 1
        if not (bon_fr and bon_en) or tout:
            etat = "OK  " if bon_fr and bon_en else "RATÉ"
            langue = "" if bon_fr == bon_en else (
                " [l'anglais seul diverge]" if bon_fr else " [le français seul diverge]")
            details = " | ".join(f"{porte} = « {' / '.join(liste)} »" for porte, liste in sorted(noms.items()))
            print(f"  {etat} {appel:34s} {details}{langue}")
            if not bon_en:
                detail_en = " | ".join(f"{porte} = « {' / '.join(liste)} »" for porte, liste in sorted(en_noms.items()))
                print(f"       {'':34s} EN : {detail_en}")
    print(f"--- {ecarts} geste(s) à plusieurs noms")
    return 0 if ecarts == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
