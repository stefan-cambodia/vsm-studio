#!/usr/bin/env python3
"""LA GARDE DE D403 : UN TEXTE QUI CITE UNE ENTRÉE DE MENU LA CITE PAR SON VRAI NOM.

RÈGLE GARDÉE (26/09/2026) : dans les clés françaises du dictionnaire
(`app/Source/ui/Langue.cpp`), chaque chemin « A ▸ B ▸ C » doit finir par une
entrée C qui EXISTE comme clé du dictionnaire — tous les libellés de menu
passent par `tr`. Une entrée peut porter une suite (« … », « (le montage
reprend) ») : la citation en est un PRÉFIXE. Une phrase qui décrit un AUTRE
logiciel (Cubase, Live, FL Studio) n'est pas jugée.

POURQUOI CETTE GARDE EXISTE. D402 : la fenêtre des associations MIDI décrivait
un clic droit et un libellé qui n'existaient nulle part. D403 : le remède d'une
chaîne introuvable citait « Fichier ▸ Chaîne d'analyse... », une ligne au-dessus
de la vraie entrée « Indiquer le dossier de la chaîne... ». Un texte qui cite
une commande ment dès que la commande change de nom (la leçon de D358).

Rend 0 si toutes les citations tiennent, 1 sinon (chacune nommée).
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
LANGUE = RACINE / "app" / "Source" / "ui" / "Langue.cpp"
AUTRES_LOGICIELS = re.compile(r"\b(Cubase|Live|Ableton|FL Studio|Logic)\b")
# Une citation : des segments séparés par « ▸ » ; le dernier s'arrête à la
# ponctuation de la phrase ou à un mot de liaison.
CITATION = re.compile(r"((?:[^\s▸«»()]+ )*?[^\s▸«»()]+(?: ▸ [^▸«»\n]+)+)")
FIN = re.compile(r"(\.\.\.|…)|[.,;:«»()]|\s(pour|puis|et|ou|qui|—)\s|$")


def cles() -> list[str]:
    texte = LANGUE.read_text(encoding="utf-8")
    return re.findall(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,', texte)


def derniere_entree(citation: str) -> str:
    """Le dernier segment de « A ▸ B ▸ C », coupé à la fin de la phrase ; les
    points de suspension en font partie (« Indiquer le dossier de la chaîne... »)."""
    segment = citation.split("▸")[-1].strip()
    m = FIN.search(segment)
    if m and m.group(1):          # des points de suspension : ils appartiennent au libellé
        return segment[:m.end()].strip()
    return segment[:m.start()].strip() if m else segment


def cite_une_entree(entree: str, libelles: set[str]) -> bool:
    """Une citation terminée par « ... » est une entrée EXACTE (« Chaîne d'analyse »,
    titre de section, n'est pas « Chaîne d'analyse... ») ; sinon, citation et
    entrée coïncident sur une frontière de mot, dans un sens ou dans l'autre —
    « Déverrouiller la piste » cite « Déverrouiller la piste (le montage
    reprend) », et « Nouveau depuis le modèle l'ouvrira » cite « Nouveau
    depuis le modèle » suivi de la phrase. « … » et « ... » sont un seul signe."""
    def norme(t: str) -> str:
        return t.replace("…", "...")
    cite = norme(entree)
    entrees = {norme(x) for x in libelles}
    if cite.endswith("..."):
        return cite in entrees
    for libelle in entrees:
        if libelle == cite or libelle.startswith(cite + " "):
            return True
        if cite.startswith(libelle + " ") and len(libelle) >= 8:
            return True
    return False


def main() -> int:
    toutes = cles()
    libelles = set(toutes)
    fautes = []
    jugees = 0
    for cle in toutes:
        if "▸" not in cle or AUTRES_LOGICIELS.search(cle):
            continue
        for bout in re.split(r"\\n|(?<=[.!?])\s", cle):
            if "▸" not in bout:
                continue
            entree = derniere_entree(bout)
            if not entree:
                continue
            jugees += 1
            if not cite_une_entree(entree, libelles):
                fautes.append((entree, cle[:110]))
    for entree, cle in fautes:
        print(f"  RATÉ « {entree} » n'est l'entrée d'aucun menu — dans : {cle}")
    print(f"MENUS CITÉS : {jugees} citation(s) jugée(s), {len(fautes)} faute(s)")
    return 1 if fautes else 0


if __name__ == "__main__":
    sys.exit(main())
