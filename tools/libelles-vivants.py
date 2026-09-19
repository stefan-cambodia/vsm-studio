#!/usr/bin/env python3
"""AUCUN LIBELLÉ AFFICHÉ NE NOMME UNE TOUCHE CONFIGURABLE — vérifié SUR L'ÉCRAN.

    tools/libelles-vivants.py [chemin/du/binaire]
    tools/libelles-vivants.py --tout        (montre chaque texte relevé)

RÈGLE GARDÉE (20/09/2026, D372) : c'est la règle de D358 — « la touche d'une
entrée de menu est DESSINÉE par JUCE, jamais écrite dans le libellé » —, mais
contrôlée là où `raccourcis-affiches.py` ne peut pas aller : sur les textes que
l'application AFFICHE vraiment.

ELLE REBIND, ET C'EST TOUTE L'IDÉE. Un premier jet cherchait simplement une
touche entre parenthèses dans les textes affichés : il a rendu **15 fautes**, et
les quinze étaient JUSTES — « Annuler (Ctrl+Z) », « Zoom avant (=) »… sont les
infobulles que D358 a écrites avec `libelleAvecTouche`, qui ajoute la touche
VIVANTE. À l'exécution, un libellé juste et un libellé en dur s'affichent
EXACTEMENT PAREIL ; les distinguer en lisant une seule course est impossible.

La garde joue donc DEUX courses : l'une avec la table d'usine, l'autre avec une
commande REBINDÉE (le fichier de réglages porte la table, clé « raccourcis »).
Un libellé qui suit la table change ; **un libellé qui montre encore l'ancienne
touche l'a écrite en dur**. C'est la leçon de D145 — une valeur qui revient à son
point de départ ne prouve rien sans le témoin qui montre qu'elle en était partie
—, appliquée à du texte.

ET LE REBIND PORTE SON PROPRE TÉMOIN : au moins un libellé doit montrer la
touche NEUVE. Sans ce contrôle, « plus aucune ancienne touche » serait vrai aussi
le jour où le rebind échoue en silence et où plus rien ne s'affiche du tout.

CE TÉMOIN TOMBE AUSSI QUAND LE DÉFAUT EST LÀ, et c'est attendu : si le SEUL
libellé de la commande rebindée est écrit en dur, rien ne peut montrer la touche
neuve. Les deux contrôles passent alors au rouge, et c'est le second qui nomme
la cause (« (Ctrl+Z) est encore affiché après le rebind »). Mesuré en posant le
défaut : 2 ratés, le second citant l'infobulle fautive mot pour mot.

POURQUOI ELLE EXISTE. D358 puis D371 lisent le CODE, et D371 a chiffré ce que
cette lecture ne peut pas atteindre : **36 libellés sont bâtis sur une variable**
(`tr(cle)`, `tr(juce::String::fromUTF8(nom))`) et aucune analyse statique ne les
rendra lisibles. D358 l'avait dit — « il faudrait un relevé à l'exécution pour le
voir » — et personne ne l'avait écrit. Or le relevé existait depuis D94 :
`VSM_TEXTES_LISTE` descend les composants et rend chaque texte visible, boutons
et infobulles compris. Il suffisait de le lui demander.

LES DEUX GARDES NE SE REMPLACENT PAS, ET C'EST VOULU :

  * `raccourcis-affiches.py` lit TOUT le code, y compris les vues qu'aucune
    course n'ouvre, mais ne voit que les littéraux ;
  * celle-ci ne voit que les vues qu'elle ouvre, mais elle voit les libellés
    ASSEMBLÉS, ceux que l'autre déclare ne pas pouvoir lire.

CE QU'ELLE NE VOIT PAS, et le dit plutôt que de compter zéro :

  * un texte PEINT (`g.drawText`) est invisible à un relevé qui descend les
    composants — c'est la leçon de D149 et D152, où « la phrase ne s'affiche
    pas » a failli être écrite d'une phrase affichée en grand au centre de
    l'écran ;
  * les vues non ouvertes, et les boîtes de dialogue, qui n'existent qu'après le
    geste qui les ouvre (D95).

Elle DIT donc combien de textes elle a lus. Un « 0 fautif » sur 3 textes lus ne
vaut pas un « 0 fautif » sur 600, et refuser de distinguer les deux serait
exactement la panne muette que ce dépôt refuse.

Rend 0 si aucun libellé affiché ne nomme une touche configurable, 1 sinon,
2 si le binaire manque ou si aucun texte n'a pu être relevé.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
BINAIRE = RACINE / "build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"

# LES VUES OUVERTES, et pourquoi celles-là : chacune porte des libellés que les
# autres n'ont pas. Une course par vue, avec sa capture — un lancement sans
# capture ne quitte pas (D318).
VUES = [
    ("arrangement", "sans-rapport,arrangement"),
    ("pianoroll", "sans-rapport,pianoroll"),
    ("mixer", "sans-rapport,arrangement,mixer"),
    ("liste", "sans-rapport,arrangement,liste"),
    ("automation", "sans-rapport,arrangement,automation"),
]

TEXTE = re.compile(r"^VSM_TEXTE : (?P<nature>[^:]+) : (?P<texte>.*)$")
SOURIS = re.compile(r"\+\s*(clic|glisser|clique|molette)", re.IGNORECASE)


def outils_de_d358() -> tuple[dict[str, str], object]:
    """Les touches configurables, lues par la garde statique — jamais recopiées.

    Deux listes de touches finiraient par ne plus dire la même chose, et c'est
    la garde statique qui porte déjà la lecture de `ShortcutTable.cpp` et ses
    refus (moins de vingt touches lues = le motif ne la lit plus).
    """
    chemin = RACINE / "tools/raccourcis-affiches.py"
    spec = importlib.util.spec_from_file_location("raccourcis_affiches", chemin)
    if spec is None or spec.loader is None:
        raise LookupError(f"{chemin} illisible")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.touches_par_defaut(), module


def poser_les_raccourcis(maison: Path, surcharges: dict[str, str]) -> None:
    """Écrit la table des raccourcis dans le fichier de réglages d'un HOME de banc.

    C'est le seul chemin : aucun verbe de banc ne rebinde une commande, et la
    table est lue au démarrage depuis `UiScale::properties()`, clé « raccourcis »
    (`MainComponent::loadShortcuts`). On écrit donc le fichier que JUCE lira.
    JAMAIS celui de l'utilisateur — `maison` est toujours un dossier de brouillon.
    """
    from xml.sax.saxutils import escape
    surcharges_json = ", ".join(f'"{cle}": "{val}"' for cle, val in surcharges.items())
    table = f'{{"format": "vsm.raccourcis.v1", "overrides": {{{surcharges_json}}}}}'
    dossier = maison / "VintageSynthMidiStudio"
    dossier.mkdir(parents=True, exist_ok=True)
    (dossier / "VintageSynthMidiStudio.settings").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n\n<PROPERTIES>\n'
        f'  <VALUE name="raccourcis" val="{escape(table, {chr(34): "&quot;"})}"/>\n'
        "</PROPERTIES>\n",
        encoding="utf-8")


def course(binaire: Path, brouillon: Path, nom: str, vue: str, projet: Path,
            surcharges: dict[str, str] | None = None) -> list[tuple[str, str]]:
    """Un lancement, et les textes qu'il a affichés."""
    # UN HOME NEUF PAR COURSE (D318) : un HOME réutilisé rouvre ses
    # autosauvegardes AVANT le geste demandé, et chaque course y laisse sa trace.
    maison = Path(tempfile.mkdtemp(prefix=f"home-{nom}-", dir=brouillon))
    if surcharges:
        poser_les_raccourcis(maison, surcharges)
    journal = brouillon / f"{nom}.txt"
    env = dict(os.environ)
    env.update({
        "HOME": str(maison),
        "VSM_TAILLE": "1600x1000",
        "VSM_PROJET": str(projet),
        "VSM_DELAI": "3500",
        "VSM_VUE": vue,
        "VSM_TEXTES_LISTE": "1",
        "VSM_CAPTURE": str(brouillon / f"{nom}.png"),
    })
    with journal.open("w", encoding="utf-8") as sortie:
        subprocess.run([str(binaire)], env=env, stdout=sortie, stderr=subprocess.STDOUT,
                        timeout=180, check=False)
    textes = []
    for ligne in journal.read_text(encoding="utf-8", errors="replace").splitlines():
        m = TEXTE.match(ligne.strip())
        if m:
            textes.append((m.group("nature").strip(), m.group("texte").strip()))
    return textes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binaire", nargs="?", default=str(BINAIRE))
    parser.add_argument("--tout", action="store_true", help="montre chaque texte relevé")
    args = parser.parse_args()

    binaire = Path(args.binaire)
    if not binaire.is_file() or not os.access(binaire, os.X_OK):
        print(f"REFUS : {binaire} absent — compiler d'abord", file=sys.stderr)
        return 2
    try:
        touches, _ = outils_de_d358()
    except Exception as e:                                    # noqa: BLE001
        print(f"REFUS : impossible de lire les touches — {e}", file=sys.stderr)
        return 2
    if len(touches) < 20:
        print(f"REFUS : {len(touches)} touches lues — le motif ne lit plus la table",
              file=sys.stderr)
        return 2

    projet = RACINE / "reconstruction/children-dream-v12"
    if not (projet / "project.json").is_file():
        print(f"REFUS : {projet} n'est pas un projet — la garde n'a rien à ouvrir",
              file=sys.stderr)
        return 2

    # LA COMMANDE REBINDÉE ET SA TOUCHE NEUVE. « edit.undo » parce que son
    # infobulle est affichée dans toutes les vues du piano roll, et « ctrl + F9 »
    # parce qu'aucune commande d'usine ne la porte : une touche déjà prise
    # partirait en conflit et le rebind serait refusé.
    # PLUSIEURS COMMANDES D'UN COUP, et cela ne coûte pas un lancement de plus :
    # la seconde course en porte autant qu'on veut. En rebinder UNE laissait 56
    # des 57 non éprouvées — c'était le « reste nommé » de la première version.
    # Les touches neuves sont des touches de fonction qu'aucune commande d'usine
    # ne porte : une touche déjà prise partirait en conflit et le rebind serait
    # refusé, ce qui se lirait comme un libellé en dur.
    REBINDS = {
        "edit.undo":      ("ctrl + F9",  "Ctrl+F9"),
        "edit.redo":      ("ctrl + F10", "Ctrl+F10"),
        "edit.quantize":  ("ctrl + F11", "Ctrl+F11"),
        "edit.legato":    ("ctrl + F12", "Ctrl+F12"),
        "view.zoomIn":    ("alt + F9",   "Alt+F9"),
        "view.zoomOut":   ("alt + F10",  "Alt+F10"),
        "view.zoomToFit": ("alt + F11",  "Alt+F11"),
        "tool.select":    ("alt + F12",  "Alt+F12"),
    }
    par_commande = {commande: touche for touche, commande in touches.items()}
    inconnues = sorted(c for c in REBINDS if c not in par_commande)
    if inconnues:
        print(f"REFUS : ces commandes ne sont plus dans la table — {', '.join(inconnues)}",
              file=sys.stderr)
        return 2
    surcharges = {c: REBINDS[c][0] for c in REBINDS}
    # {ancienne touche affichée: (commande, nouvelle touche affichée)}
    attendus = {par_commande[c]: (c, REBINDS[c][1]) for c in REBINDS}

    print("=== D372 : un libellé affiché suit-il la table des raccourcis ? ===")
    print(f"       {len(touches)} touches configurables, {len(VUES)} vue(s) × 2 courses")
    print(f"       {len(REBINDS)} commande(s) rebindée(s) : "
          + ", ".join(f"{a} -> {n}" for a, (_, n) in sorted(attendus.items())))

    avant: set[tuple[str, str]] = set()
    apres: set[tuple[str, str]] = set()
    with tempfile.TemporaryDirectory(prefix="vsm-libelles-") as tmp:
        brouillon = Path(tmp)
        for nom, vue in VUES:
            try:
                avant.update(course(binaire, brouillon, nom, vue, projet))
                apres.update(course(binaire, brouillon, nom + "-rebind", vue, projet,
                                     surcharges))
            except subprocess.TimeoutExpired:
                print(f"  RATÉ {nom} : une course n'a pas rendu la main", file=sys.stderr)
                return 2

    if not avant or not apres:
        print("REFUS : aucun texte relevé — l'écran est peut-être VERROUILLÉ "
              "(loginctl show-session <n> -p LockedHint), ou le relevé ne répond plus",
              file=sys.stderr)
        return 2
    print(f"       {len(avant)} texte(s) distincts avant, {len(apres)} après")

    rates = 0

    def verdict(quoi: str, ok: bool) -> None:
        nonlocal rates
        print(f"  {'OK  ' if ok else 'RATÉ'} {quoi}")
        if not ok:
            rates += 1

    # (1) LE TÉMOIN DU REBIND : la touche NEUVE doit apparaître quelque part.
    # Sans lui, « plus aucune ancienne touche » serait vrai aussi le jour où le
    # rebind échoue en silence — et la garde passerait au vert sur une panne.
    suivies = sorted({n for _, (_, n) in attendus.items()
                       if any(f"({n})" in t for _, t in apres)})
    for n in suivies:
        exemple = next(t for _, t in apres if f"({n})" in t)
        print(f"       ({n}) s'affiche : {exemple[:70]}")
    verdict(f"le rebind a pris : {len(suivies)}/{len(attendus)} touche(s) neuve(s) affichée(s)",
             bool(suivies))

    # (2) LA MESURE : après le rebind, plus AUCUN libellé ne doit montrer
    # l'ancienne touche pour cette commande. Celui qui la montre encore l'a
    # écrite en dur — c'est le défaut que D358 chasse, vu sur l'écran.
    restes = sorted({(a, t) for a in attendus for _, t in apres
                      if f"({a})" in t and not SOURIS.search(t)})
    for a, t in restes:
        print(f"  RATÉ « ({a}) » ({attendus[a][0]}) est encore affiché après le "
              f"rebind : {t[:64]}")
    verdict(f"aucun libellé ne montre encore l'une des {len(attendus)} anciennes touches",
             not restes)

    # CE QU'ELLE N'A PAS PU VOIR, dit à chaque course.
    print("       NON COUVERT : les textes PEINTS (invisibles à ce relevé, D149), "
          "les boîtes de dialogue (D95), les vues non ouvertes, et les commandes "
          "de la table qui ne sont pas rebindées par cette course")
    print()
    if rates:
        print(f"D372 : {rates} contrôle(s) raté(s)")
        return 1
    print("D372 : les libellés affichés suivent la table")
    return 0


if __name__ == "__main__":
    sys.exit(main())
