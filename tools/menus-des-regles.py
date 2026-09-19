#!/usr/bin/env python3
"""LES DEUX RÈGLES PORTENT LE MÊME MENU : LE MÊME LIBELLÉ DOIT Y FAIRE LA MÊME CHOSE.

    python3 tools/menus-des-regles.py
    python3 tools/menus-des-regles.py --montrer   (imprime les deux tables)

RÈGLE GARDÉE (20/09/2026, D370). L'application a DEUX règles — celle de
l'arrangement (`ArrangementComponent::menuDeLaRegle`) et celle du piano roll
(`PianoRollRulerComponent::construireMenuDeRepere`) — et toutes deux offrent le
même menu de repères : poser, renommer, retirer. Ce sont deux copies, écrites à
deux moments (D83 puis D218), et rien ne les appariait.

CE QUI A DÉCLENCHÉ CETTE GARDE, et pourquoi elle n'accuse personne. En relisant
les deux menus côte à côte pour le « reste nommé » de D361, on voit ceci :

    arrangement :  1 = Poser     2 = Renommer   3 = Retirer
    piano roll  :  1 = Poser     3 = Renommer   2 = Retirer

**Les numéros 2 et 3 sont ÉCHANGÉS entre les deux règles.** Mesuré avant
d'écrire quoi que ce soit : chaque gestionnaire est cohérent avec SON menu —
l'arrangement renomme sur 2 et retire sur 3, le piano roll renomme sur 3 et
retire sur 2 —, si bien que les deux se comportent JUSTE aujourd'hui. Ce n'est
pas un défaut, et cette garde ne le compte pas comme tel.

C'est un piège armé, et D349 l'a nommé : « un numéro en dur est un piège qui se
referme au premier onglet ajouté ». Ici il se refermerait au premier qui
factorise les deux menus, ou qui recopie un gestionnaire d'une règle vers
l'autre en croyant les numéros partagés — « Renommer ce repère… » EFFACERAIT le
repère, et aucun test ne le verrait : les deux menus resteraient justes chacun
chez soi, exactement comme les libellés divergents de D355.

CE QU'ELLE VÉRIFIE, et c'est ce que D361 avait nommé sans pouvoir l'affirmer :

  1. les deux règles offrent les MÊMES libellés ;
  2. un libellé donné y appelle le MÊME rappel (`onMarkerRenameRequested`…) —
     l'appariement se fait par le LIBELLÉ, et la comparaison porte sur l'ACTION,
     jamais sur le numéro, qui n'a de sens que dans son propre menu ;
  3. aucune de ces entrées n'a de raccourci dans la table — D361 l'affirmait
     (« ils n'ont aucun raccourci en commun avec la table, mais rien ne le
     VÉRIFIE »). Si l'une en reçoit un un jour, la garde le dira, et la règle de
     D355 s'appliquera alors : une entrée à deux portes doit porter un nom qui
     se reconnaît.

ELLE REFUSE PLUTÔT QUE DE RENDRE ZÉRO (code 2) si elle ne reconnaît plus l'une
des quatre formes qu'elle lit. Une garde qui ne trouve plus rien à comparer et
se tait est pire que pas de garde (leçon de D355, et de `gestes-vivants.py` qui
a d'abord échoué à échouer).

Rend 0 si les trois contrôles tiennent, 1 sinon, 2 si elle ne reconnaît plus le code.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
ARRANGEMENT = RACINE / "app/Source/ui/ArrangementComponent.cpp"
PIANO_REGLE = RACINE / "app/Source/ui/PianoRollRulerComponent.cpp"
TABLE = RACINE / "interchange/src/ShortcutTable.cpp"

# « menu.addItem(2, tr(u8"Renommer ce repère…"), survole >= 0); »
ENTREE = re.compile(r'menu\.addItem\(\s*(\d+)\s*,\s*tr\(\s*(?:u8)?"((?:[^"\\]|\\.)*)"')
# « if (choix == 3 && survole >= 0 && onMarkerRenameRequested) onMarkerRenameRequested(…) »
BRANCHE = re.compile(r"choix\s*==\s*(\d+)\b(?P<suite>[^;]*);", re.S)
RAPPEL = re.compile(r"\b(on[A-Z]\w*)\s*\(")


def bloc(source: str, signature: str) -> str:
    """Le corps d'une fonction, de sa signature à l'accolade de fin en colonne 0."""
    i = source.find(signature)
    if i < 0:
        raise LookupError(signature)
    fin = source.find("\n}", i)
    if fin < 0:
        raise LookupError(signature + " (fin)")
    return source[i:fin]


def decoder(texte: str) -> str:
    """Les échappements du C++ rendus tels que l'écran les montre (cf. noms-des-gestes.py)."""
    texte = re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), texte)

    def octets(m: re.Match[str]) -> str:
        brut = bytes(int(h, 16) for h in re.findall(r"\\x([0-9a-fA-F]{2})", m.group(0)))
        try:
            return brut.decode("utf-8")
        except UnicodeDecodeError:
            return m.group(0)

    return re.sub(r"(?:\\x[0-9a-fA-F]{2})+", octets, texte)


def table_des_libelles(source: str, sig_menu: str, sig_action: str) -> dict[str, str]:
    """{libellé: rappel appelé}, pour une règle.

    L'ACTION est lue dans le gestionnaire, pas devinée du libellé : c'est le seul
    moyen de voir qu'un numéro et son action se sont désaccordés.
    """
    corps_menu = bloc(source, sig_menu)
    par_numero = {int(n): decoder(lib) for n, lib in ENTREE.findall(corps_menu)}
    if not par_numero:
        raise LookupError(sig_menu + " : aucune entrée « menu.addItem(n, tr(…)) »")

    corps_action = bloc(source, sig_action)
    rappel_par_numero: dict[int, str] = {}
    for m in BRANCHE.finditer(corps_action):
        rappels = RAPPEL.findall(m.group("suite"))
        if rappels:
            # Le rappel APPELÉ est le dernier : le premier est le test
            # « && onMarkerRemoved » qui vérifie seulement qu'il est branché.
            rappel_par_numero[int(m.group(1))] = rappels[-1]
    if not rappel_par_numero:
        raise LookupError(sig_action + " : aucune branche « choix == n »")

    sans_action = sorted(n for n in par_numero if n not in rappel_par_numero)
    if sans_action:
        raise LookupError(
            f"{sig_menu} : les entrées {sans_action} n'ont aucune branche dans le gestionnaire"
        )
    return {par_numero[n]: rappel_par_numero[n] for n in sorted(par_numero)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--montrer", action="store_true", help="imprime les deux tables")
    args = parser.parse_args()

    for chemin in (ARRANGEMENT, PIANO_REGLE, TABLE):
        if not chemin.is_file():
            print(f"REFUS : {chemin} est introuvable", file=sys.stderr)
            return 2
    try:
        arrangement = table_des_libelles(
            ARRANGEMENT.read_text(encoding="utf-8"),
            "juce::PopupMenu ArrangementComponent::menuDeLaRegle",
            "void ArrangementComponent::regleMenuAction",
        )
        piano = table_des_libelles(
            PIANO_REGLE.read_text(encoding="utf-8"),
            "juce::PopupMenu PianoRollRulerComponent::construireMenuDeRepere",
            "void PianoRollRulerComponent::actionDeMenuDeRepere",
        )
    except LookupError as e:
        print(f"REFUS : forme de code non reconnue — {e}", file=sys.stderr)
        return 2

    print(f"=== les deux règles : {len(arrangement)} et {len(piano)} entrée(s) ===")
    if args.montrer:
        for nom, table in (("arrangement", arrangement), ("piano roll", piano)):
            for libelle, rappel in table.items():
                print(f"  {nom:12} {libelle!r} -> {rappel}")

    rates = 0

    def verdict(quoi: str, ok: bool) -> None:
        nonlocal rates
        print(f"  {'OK  ' if ok else 'RATÉ'} {quoi}")
        if not ok:
            rates += 1

    # (1) LES MÊMES LIBELLÉS
    seuls_arr = sorted(set(arrangement) - set(piano))
    seuls_pia = sorted(set(piano) - set(arrangement))
    if seuls_arr or seuls_pia:
        for libelle in seuls_arr:
            print(f"       seulement dans l'arrangement : {libelle!r}")
        for libelle in seuls_pia:
            print(f"       seulement dans le piano roll : {libelle!r}")
    verdict("les deux règles offrent les mêmes libellés", not seuls_arr and not seuls_pia)

    # (2) LE MÊME LIBELLÉ, LA MÊME ACTION — et c'est le contrôle qui compte.
    desaccords = [
        (libelle, arrangement[libelle], piano[libelle])
        for libelle in sorted(set(arrangement) & set(piano))
        if arrangement[libelle] != piano[libelle]
    ]
    for libelle, a, p in desaccords:
        print(f"       DÉSACCORD {libelle!r} : arrangement -> {a}, piano roll -> {p}")
    verdict("un libellé fait la même chose dans les deux règles", not desaccords)

    # (3) AUCUNE DE CES ENTRÉES N'A DE RACCOURCI — ce que D361 affirmait sans le
    # vérifier. On compare aux LIBELLÉS de la table, décodés comme les autres.
    libelles_table = {decoder(lib) for lib in re.findall(r'"((?:[^"\\]|\\.)*)"', TABLE.read_text(encoding="utf-8"))}
    if not libelles_table:
        print("REFUS : aucun libellé lu dans ShortcutTable.cpp", file=sys.stderr)
        return 2
    a_raccourci = sorted(set(arrangement) & libelles_table)
    for libelle in a_raccourci:
        print(f"       {libelle!r} a désormais une entrée dans la table : "
              f"la règle de D355 s'applique (un geste à deux portes porte un nom qui se reconnaît)")
    verdict("aucune entrée des règles n'a de raccourci (l'affirmation de D361)", not a_raccourci)

    print()
    if rates:
        print(f"MENUS DES RÈGLES : {rates} contrôle(s) raté(s)")
        return 1
    print("MENUS DES RÈGLES : les deux règles disent et font la même chose")
    return 0


if __name__ == "__main__":
    sys.exit(main())
