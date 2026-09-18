#!/usr/bin/env python3
"""Un raccourci écrit DANS un libellé ment dès qu'on le change.

    python3 tools/raccourcis-affiches.py
    python3 tools/raccourcis-affiches.py --tout   (liste aussi ce qui est toléré)

LA RÈGLE GARDÉE (18/09/2026, D358). La table des raccourcis est **modifiable par
l'utilisateur et persistée** dans ses préférences (`MainComponent::loadShortcuts`,
clé « raccourcis »). Un libellé qui écrit sa touche en toutes lettres —
« Couper à la tête de lecture (Ctrl+E) » — devient donc **faux** dès le premier
remaniement : il nomme une touche qui ne fait plus rien, pendant que la fenêtre
des raccourcis, elle, dit la vérité. Mesuré le 18/09 : Ctrl+E rebindé en
Ctrl+Maj+E, la fenêtre disait « Ctrl+Maj+E » et le menu du clip « (Ctrl+E) ».

D155 avait posé la règle — « la touche d'une entrée de menu est DESSINÉE par
JUCE, pas écrite dans le libellé » — et l'avait appliquée à la barre de menus.
Elle n'en était jamais sortie. Cette garde-ci la fait valoir partout : c'est la
leçon de D332 (« une règle posée dans l'application seule ne vaut pas pour
l'export »), appliquée aux libellés.

CE QU'ELLE CHERCHE, ET COMMENT. Elle lit les touches PAR DÉFAUT de la table
(`ShortcutTable.cpp`), les écrit comme l'interface les écrit (`toucheLisible` :
« ctrl + Q » devient « Ctrl+Q »), puis cherche chacune, ENTRE PARENTHÈSES, dans
les chaînes confiées à `tr()` des sources de `app/Source/`. Chercher la touche
plutôt qu'un motif de parenthèses évite d'accuser « (aucune note) » ou
« (les deux vues) », et de manquer « (F11) ».

CE QU'ELLE TOLÈRE, ET POURQUOI C'EST ÉCRIT ICI PLUTÔT QUE DEVINÉ :
  * les MODIFICATEURS DE SOURIS (« Ctrl+clic sur Solo », « Ctrl+clic ou
    double-clic : rampe ») ne sont pas des raccourcis : la table ne les porte
    pas, personne ne peut les changer, et les taire priverait l'utilisateur du
    seul endroit où ils sont écrits ;
  * `ShortcutTable.cpp` lui-même, qui EST la table, et `Langue.cpp`, qui traduit
    des libellés que la table fournit ;
  * `ShortcutsWindow.cpp`, dont la boîte de capture écrit « (Échap : annuler) » :
    cet Échap-là n'est pas la commande `edit.selectNone` mais le geste qui annule
    la capture en cours, et personne ne peut le changer. C'est une phrase, donc
    la règle des deux formes l'écarte déjà -- écrit ici pour qu'on ne le
    « répare » pas un jour par mégarde.

Rend 0 si aucun libellé n'écrit une touche configurable, 1 sinon, 2 si un
fichier attendu manque.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
TABLE = RACINE / "interchange/src/ShortcutTable.cpp"
SOURCES = RACINE / "app/Source"
# Les fichiers qui ont le DROIT de nommer une touche : la table, et la table des
# traductions qui en reprend les libellés.
EXEMPTS = {"ShortcutTable.cpp", "Langue.cpp"}
# Un modificateur suivi d'un mot de SOURIS n'est pas un raccourci de clavier.
SOURIS = re.compile(r"\+\s*(clic|glisser|clique|molette)", re.IGNORECASE)


def touches_par_defaut() -> dict[str, str]:
    """{touche affichée: nom de la commande}, d'après les défauts de la table."""
    texte = TABLE.read_text(encoding="utf-8")
    trouvees: dict[str, str] = {}
    for cle, libelle, touche in re.findall(
        r'\{ShortcutId::\w+,\s*"([^"]*)",\s*"[^"]*",\s*"((?:[^"\\]|\\.)*)",\s*"([^"]*)"', texte
    ):
        if touche:
            trouvees.setdefault(affichee(touche), cle or libelle)
    return trouvees


def affichee(description: str) -> str:
    """« ctrl + shift + E » écrit comme l'interface l'écrit : « Ctrl+Maj+E ».

    C'est la règle de `toucheLisible` (Langue.cpp), refaite ici parce qu'une
    garde qui appellerait le C++ ne tournerait plus sans l'avoir compilé."""
    mots = {"ctrl": "Ctrl", "shift": "Maj", "alt": "Alt", "command": "Ctrl",
            "escape": "Échap", "spacebar": "Espace", "delete": "Suppr",
            "backspace": "Retour arrière", "home": "Début", "end": "Fin"}
    morceaux = [m.strip() for m in description.split("+")]
    return "+".join(mots.get(m.lower(), m.upper() if len(m) == 1 else m) for m in morceaux)


def libelles(fichier: Path) -> list[tuple[int, str]]:
    """Les chaînes confiées à `tr(`, avec leur numéro de ligne."""
    texte = fichier.read_text(encoding="utf-8", errors="replace")
    trouvees = []
    for m in re.finditer(r'\btr\(\s*(?:u8)?"((?:[^"\\]|\\.)*)"', texte):
        trouvees.append((texte.count("\n", 0, m.start()) + 1, m.group(1)))
    return trouvees


def main() -> int:
    tout = "--tout" in sys.argv
    if not TABLE.is_file() or not SOURCES.is_dir():
        print("REFUS : les sources attendues sont absentes")
        return 2
    touches = touches_par_defaut()
    if len(touches) < 20:
        print(f"REFUS : {len(touches)} touches lues dans la table — le motif ne la lit plus")
        return 2

    print("=== D358 : un raccourci écrit dans un libellé ment dès qu'on le change ===")
    fichiers = sorted(p for p in SOURCES.rglob("*.cpp") if p.name not in EXEMPTS)
    fichiers += sorted(p for p in SOURCES.rglob("*.h") if p.name not in EXEMPTS)
    print(f"       {len(touches)} touches configurables, {len(fichiers)} fichiers lus")
    fautes, tolerees = 0, 0
    for fichier in fichiers:
        for ligne, texte in libelles(fichier):
            for touche, commande in touches.items():
                # DEUX FORMES, ET DEUX SEULEMENT. Le premier jet acceptait aussi
                # « ({touche} … » et accusait « Canal MIDI (1 à 16) » -- la
                # touche « 1 » de l'outil Sélection, dans une PHRASE. Une
                # parenthèse qui continue par d'autres mots est une phrase, pas
                # l'étiquette d'une touche. « (touche R) » est ajoutée parce que
                # la barre de transport l'écrivait ainsi et que le premier jet ne
                # la voyait pas : une garde qui ne connaît qu'une orthographe du
                # défaut rend zéro sur les autres.
                if f"({touche})" not in texte and f"(touche {touche})" not in texte:
                    continue
                if SOURIS.search(texte):
                    tolerees += 1
                    if tout:
                        print(f"  ---- {fichier.relative_to(RACINE)}:{ligne} « {texte[:60]} » "
                              f"— modificateur de souris, toléré")
                    continue
                fautes += 1
                print(f"  RATÉ {fichier.relative_to(RACINE)}:{ligne} écrit « ({touche}) » "
                      f"pour « {commande} » : {texte[:70]}")
    if tolerees and not tout:
        print(f"       {tolerees} mention(s) tolérée(s) (modificateurs de souris) — --tout pour les voir")
    print(f"--- {fautes} libellé(s) qui nomment une touche configurable")
    return 0 if fautes == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
