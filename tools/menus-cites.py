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
    # LES DEUX FORMES DE CLÉ (02/10/2026, D532.3) : `{ "…",` dans la table des
    # libellés, `{ u8"…",` dans celle des modèles de phrase (`ModeleDePhrase`,
    # char8_t). Le motif ne lisait que la première — 1 633 clés sur 1 801 — et les
    # deux entrées du menu Piste nées avec D532.1 bis, rangées dans la seconde, y
    # passaient pour « l'entrée d'aucun menu ». `(?:u8)?"`, et non `u8?"`, qui
    # voudrait « un u, puis un 8 facultatif » (le piège que l'ordre de marche nomme).
    return re.findall(r'\{\s*(?:u8)?"((?:[^"\\]|\\.)*)"\s*,', texte)


def derniere_entree(citation: str) -> str:
    """Le dernier segment de « A ▸ B ▸ C », coupé à la fin de la phrase ; les
    points de suspension en font partie (« Indiquer le dossier de la chaîne... »)."""
    segment = citation.split("▸")[-1].strip()
    m = FIN.search(segment)
    if m and m.group(1):          # des points de suspension : ils appartiennent au libellé
        return segment[:m.end()].strip()
    return segment[:m.start()].strip() if m else segment


def cite_une_entree(entree: str, libelles: set[str], souple: bool = False) -> bool:
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
        # D454 : une citation SANS BORNE (sans emphase, prise dans sa phrase) peut
        # citer un libellé à parenthèse sans elle, la phrase continuant après :
        # « Clavier d'ordinateur fait jouer… ». Jamais une citation bornée : c'est
        # ainsi que « Voir le dernier rapport d'import » passait (essai en rouge).
        if souple:
            base = re.sub(r"\s*\([^()]*\)\s*(\.\.\.)?$", "", libelle)
            if base != libelle and len(base) >= 8 and (cite == base or cite.startswith(base + " ")):
                return True
    return False


MODE_EMPLOI = RACINE / "docs" / "MODE-EMPLOI.md"


def citations_du_mode_d_emploi(chemin: Path) -> list[tuple[str, str, bool]]:
    """D454 : LE MODE D'EMPLOI CITE AUSSI LES MENUS. D452 a renommé « Voir le
    dernier rapport d'import » ; le mode d'emploi gardait l'ancien nom, et cette
    garde ne lisait que le dictionnaire.

    LA CITATION EST BORNÉE PAR SON EMPHASE quand elle en a une (`*Fichier ▸
    Exporter audio (WAV)...*`) : l'emphase finit là où finit le libellé, ce que
    la phrase ne dit pas (« Clavier d'ordinateur fait jouer… » continue la
    phrase). Une première version prenait la phrase entière et, pour ne pas
    rater les libellés à parenthèse, acceptait un libellé privé de sa
    parenthèse : la citation périmée « Voir le dernier rapport d'import »
    passait alors, préfixée par « Voir le dernier rapport » -- vu à l'essai en
    rouge. Sans emphase, la citation se prend dans sa phrase, comme les clés."""
    texte = re.sub(r"`[^`]*`", "", chemin.read_text(encoding="utf-8"))
    sortie = []
    for paragraphe in re.split(r"\n\s*\n", texte):
        paragraphe = paragraphe.replace("\n", " ")
        # Le paragraphe est LIÉ à la fonction (ruff B023) : `re.sub` l'appelle dans
        # ce même tour de boucle, mais une fermeture sur une variable de boucle
        # lirait la dernière valeur si on la gardait pour plus tard.
        def prendre(m: re.Match[str], paragraphe: str = paragraphe) -> str:
            contenu = m.group(2)
            if "▸" in contenu and not AUTRES_LOGICIELS.search(paragraphe):
                entree = contenu.split("▸")[-1].strip().rstrip(".,;:")
                if contenu.split("▸")[-1].strip().endswith("..."):
                    entree = contenu.split("▸")[-1].strip()
                sortie.append((entree, contenu[:110], False))
                return " "
            return m.group(0)
        reste = re.sub(r"(\*\*|\*)([^*]+?)\1", prendre, paragraphe).replace("*", "")
        for phrase in re.split(r"(?<=[.!?])\s", reste):
            if "▸" not in phrase or AUTRES_LOGICIELS.search(phrase):
                continue
            for m in CITATION.finditer(phrase):
                entree = derniere_entree(m.group(1))
                if entree:
                    sortie.append((entree, phrase[:110], True))
    return sortie


def main() -> int:
    toutes = cles()
    libelles = set(toutes)
    fautes = []
    jugees = 0
    # D454 : `--mode-emploi <fichier>` juge un AUTRE texte (l'essai en rouge).
    manuel = Path(sys.argv[sys.argv.index("--mode-emploi") + 1]) if "--mode-emploi" in sys.argv else MODE_EMPLOI
    for entree, phrase, souple in citations_du_mode_d_emploi(manuel):
        jugees += 1
        if not cite_une_entree(entree, libelles, souple):
            fautes.append((entree, "MODE-EMPLOI : " + phrase))
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
