#!/usr/bin/env python3
"""LA GARDE DE `docs/INDEX.md` : CE QU'IL AVANCE DE VÉRIFIABLE DOIT ÊTRE VRAI.

RÈGLE GARDÉE (20/09/2026) : les chiffres que l'INDEX écrit sur les documents
qu'il cite — leur longueur, et le compte de phases de `ROADMAP-daw.md` — sont
RECALCULÉS ici et doivent correspondre. Ils ne se recopient pas de mémoire.

POURQUOI CETTE GARDE EXISTE. L'ordre de marche du dépôt le dit déjà : « cela
vaut d'abord pour `docs/INDEX.md`, qu'on lit en PREMIER : ses lignes dérivent
derrière les cahiers des charges qu'elles citent. » Le 12/09, TROIS de ses
éléments étaient déjà faits. Le 20/09, en le relisant, deux lignes de plus
mentaient depuis des jours :

  * `ROADMAP-apprentissage.md` — « **A6 en cours** : le corpus des 59
    candidates mélodiques » — alors que A6 est close depuis le 12/09 (§ A6.8,
    le modèle à 58 machines adopté), et que le compte juste est 58 et non 59 ;
  * `CDC-detection-multipiste.md` — « le § 4.3 se croit ouvert » — alors que ses
    NEUF cases sont cochées, § 4.3 compris, depuis le 03/09. L'INDEX reprochait
    au document une péremption qui était devenue la sienne.

Ces deux-là demandent de lire ; on ne les automatise pas. Mais elles se sont
trahies par un chiffre qui, lui, se vérifie en une seconde : la même ligne
donnait `CDC-detection-multipiste.md` pour **889 lignes** quand le fichier en
compte **2 017**. UNE LONGUEUR FAUSSE EST LA TRACE D'UNE LIGNE QU'ON N'A PAS
ROUVERTE — c'est ce signal-là que cette garde rend impossible à manquer, et
c'est pourquoi elle vérifie des chiffres plutôt que des phrases.

ELLE REFUSE PLUTÔT QUE DE RENDRE ZÉRO. Si elle ne reconnaît plus la forme des
revendications de l'INDEX, elle rend le code 2 au lieu de dire « tout va bien » :
une garde qui ne trouve rien à vérifier et se tait est pire que pas de garde
(leçon de D355).

    tools/index-a-jour.py            # vérifie
    tools/index-a-jour.py --corriger # réécrit les chiffres, et DIT lesquels

Rend 0 si tout concorde, 1 s'il reste un écart, 2 si elle ne reconnaît plus
l'INDEX.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
INDEX = RACINE / "docs" / "INDEX.md"
# LA PAGE CONSULTABLE EST UNE SECONDE COPIE DU MÊME CONTENU, et l'INDEX pose
# lui-même la règle : « Qui modifie l'un refait l'autre. » Deux copies divergent
# toujours — mesuré le 20/09, elle donnait `ROADMAP-daw` pour 27 889 lignes
# quand le fichier en comptait 30 173, et `CDC-detection-multipiste` pour 843
# quand il en a 2 017. Elle est donc gardée par la même passe.
PAGE = RACINE / "docs" / "ordre-de-marche.html"

# « <td class="doc">ROADMAP-daw<br><span …>30 173 l.</span> »
REVENDICATION_HTML = re.compile(
    r'class="doc">(?P<nom>[A-Za-z0-9-]+)<br><span class="mono"[^>]*>'
    r"(?P<lignes>[\d\u00a0\u202f ]+)\s*l\."
)

# « [`nom.md`](chemin) (30 173 l.) » — l'espace insécable ou fine est tolérée,
# les milliers pouvant être séparés par une espace ordinaire ou une fine.
REVENDICATION = re.compile(
    r"\[`(?P<nom>[^`]+)`\]\((?P<cible>[^)]+)\)\s*\((?P<lignes>[\d   ]+)\s*l\.\)"
)

# « **361 titres de phase** écrits (D0 → D368) »
PHASES = re.compile(
    r"\*\*(?P<compte>\d+) titres de phase\*\* écrits \(D0 → D(?P<dernier>\d+)\)"
)


def entier(texte: str) -> int:
    """« 30 173 » -> 30173, quelle que soit l'espace qui sépare les milliers."""
    return int(re.sub(r"[  \s]", "", texte))


def formate(n: int) -> str:
    """30173 -> « 30 173 », comme l'INDEX les écrit."""
    return f"{n:,}".replace(",", " ")


def lignes_du_fichier(chemin: Path) -> int | None:
    try:
        with chemin.open(encoding="utf-8") as f:
            return sum(1 for _ in f)
    except OSError:
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corriger", action="store_true",
                        help="réécrit les chiffres faux au lieu de seulement les dire")
    args = parser.parse_args()

    if not INDEX.is_file():
        print(f"REFUS : {INDEX} est introuvable", file=sys.stderr)
        return 2
    texte = INDEX.read_text(encoding="utf-8")

    revendications = list(REVENDICATION.finditer(texte))
    if not revendications:
        print("REFUS : aucune revendication « (N l.) » reconnue dans l'INDEX — "
              "sa forme a changé, et cette garde ne vérifie plus rien",
              file=sys.stderr)
        return 2

    ecarts: list[tuple[str, int, int]] = []
    introuvables: list[str] = []
    print(f"=== docs/INDEX.md : {len(revendications)} longueur(s) annoncée(s) ===")

    remplacements: list[tuple[str, str]] = []
    for m in revendications:
        cible = m.group("cible")
        if "#" in cible or cible.startswith(("http://", "https://")):
            continue
        chemin = (INDEX.parent / cible).resolve()
        reel = lignes_du_fichier(chemin)
        if reel is None:
            introuvables.append(f"{m.group('nom')} → {cible}")
            continue
        annonce = entier(m.group("lignes"))
        if annonce != reel:
            ecarts.append((m.group("nom"), annonce, reel))
            remplacements.append((m.group(0),
                                  m.group(0).replace(m.group("lignes").strip(),
                                                     formate(reel))))

    for nom, annonce, reel in ecarts:
        print(f"  ÉCART {nom} : l'INDEX dit {formate(annonce)} l., le fichier en a {formate(reel)}")
    for manquant in introuvables:
        print(f"  INTROUVABLE {manquant}")

    # LA PAGE CONSULTABLE, vérifiée par la même passe et pour la même raison.
    page_ecarts: list[tuple[str, int, int]] = []
    if PAGE.is_file():
        html = PAGE.read_text(encoding="utf-8")
        revendications_html = list(REVENDICATION_HTML.finditer(html))
        if not revendications_html:
            print("REFUS : aucune longueur reconnue dans ordre-de-marche.html",
                  file=sys.stderr)
            return 2
        print(f"=== docs/ordre-de-marche.html : {len(revendications_html)} "
              f"longueur(s) annoncée(s) ===")
        remplacements_html: list[tuple[str, str]] = []
        for m in revendications_html:
            nom = m.group("nom")
            candidats = [RACINE / "docs" / f"{nom}.md", RACINE / f"{nom}.md"]
            source: Path | None = next((c for c in candidats if c.is_file()), None)
            if source is None:
                page_ecarts.append((nom, entier(m.group("lignes")), -1))
                print(f"  INTROUVABLE {nom} : aucun fichier ne porte ce nom")
                continue
            reel = lignes_du_fichier(source)
            annonce = entier(m.group("lignes"))
            if reel is not None and annonce != reel:
                page_ecarts.append((nom, annonce, reel))
                print(f"  ÉCART {nom} : la page dit {formate(annonce)} l., "
                      f"le fichier en a {formate(reel)}")
                remplacements_html.append(
                    (m.group(0), m.group(0).replace(m.group("lignes").strip(),
                                                    formate(reel))))
        if args.corriger and remplacements_html:
            for avant, apres in remplacements_html:
                assert html.count(avant) >= 1, f"ancre perdue : {avant!r}"
                html = html.replace(avant, apres, 1)
            PAGE.write_text(html, encoding="utf-8")
            print(f"{len(remplacements_html)} chiffre(s) réécrit(s) dans "
                  f"docs/ordre-de-marche.html")

    # LE COMPTE DE PHASES, l'autre chiffre que l'INDEX recopie au lieu de compter.
    roadmap = RACINE / "docs" / "ROADMAP-daw.md"
    phases_ecart = False
    mp = PHASES.search(texte)
    if mp is None:
        print("REFUS : le compte de phases n'est plus reconnaissable dans l'INDEX",
              file=sys.stderr)
        return 2
    titres = re.findall(r"^### Phase D(\d+)", roadmap.read_text(encoding="utf-8"), re.M)
    if not titres:
        print("REFUS : aucun « ### Phase D… » dans ROADMAP-daw.md", file=sys.stderr)
        return 2
    compte_reel, dernier_reel = len(titres), max(int(n) for n in titres)
    compte_dit, dernier_dit = int(mp.group("compte")), int(mp.group("dernier"))
    if (compte_dit, dernier_dit) != (compte_reel, dernier_reel):
        phases_ecart = True
        print(f"  ÉCART phases : l'INDEX dit {compte_dit} titres jusqu'à D{dernier_dit}, "
              f"ROADMAP-daw.md en a {compte_reel} jusqu'à D{dernier_reel}")
        remplacements.append((mp.group(0),
                              f"**{compte_reel} titres de phase** écrits (D0 → D{dernier_reel})"))

    if args.corriger:
        # LA PAGE A DÉJÀ ÉTÉ RÉÉCRITE PLUS HAUT ; il reste l'INDEX. Et le verdict
        # est 0 dès qu'on a corrigé, même si seule la page l'était : rendre 1
        # après avoir réparé ferait échouer l'appelant sur un travail FAIT.
        if remplacements:
            for avant, apres in remplacements:
                assert texte.count(avant) >= 1, f"ancre perdue : {avant!r}"
                texte = texte.replace(avant, apres, 1)
            INDEX.write_text(texte, encoding="utf-8")
            print(f"\n{len(remplacements)} chiffre(s) réécrit(s) dans docs/INDEX.md")
        return 0

    if ecarts or introuvables or phases_ecart or page_ecarts:
        total = (len(ecarts) + len(introuvables) + len(page_ecarts)
                 + (1 if phases_ecart else 0))
        print(f"\nINDEX : {total} chiffre(s) à reprendre "
              f"(`tools/index-a-jour.py --corriger` les réécrit)")
        return 1
    print(f"\nINDEX : {len(revendications)} longueur(s) et le compte de phases "
          f"({compte_reel} jusqu'à D{dernier_reel}) concordent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
