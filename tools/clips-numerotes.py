#!/usr/bin/env python3
"""D262 : tout clip ajouté à une piste doit porter un IDENTIFIANT.

    python3 tools/clips-numerotes.py

LA RÈGLE. `Project::assignClipIds()` numérote à partir de 1 et ne se rejoue
qu'au CHARGEMENT d'un projet (`ArrangementComponent::setProject`). Un clip
poussé à la main pendant la séance garde donc `id == 0`, qui est la valeur
« jamais numéroté » -- et le code qui VISE un clip lit ce 0 comme « aucun clip ».
Le 13/09/2026, « Découper aux transitoires » a trouvé quatre attaques sur un
fichier importé, écrit « 4 attaque(s) », puis « 0 coupe(s) », sans que rien ne
dise pourquoi : le clip couvrant ÉTAIT trouvé, son identifiant valait 0, et
`if (couvrant == 0)` le jetait.

CE QUI EST VÉRIFIÉ : tout `clips.push_back` / `clips.emplace_back` du code de
production porte, DANS LA MÊME FONCTION, l'une de ces trois choses : une
affectation d'identifiant (`clip.id = ...`, `.id = idCounter++`), un appel à
`Project::ajouterClip` qui numérote, ou un appel à `assignClipIds()` qui repasse
derrière. Un clip qui GARDE l'identifiant qu'il avait déjà (déplacé d'une piste à
l'autre) se marque par le commentaire « identifiant conservé » sur la ligne : une
exception écrite se relit, une exception devinée par l'outil ne se relit pas.

Les chemins de CHARGEMENT (interchange/, où `assignClipIds()` repasse) et les
tests sont exclus, et la liste des exclusions est écrite ici plutôt que devinée.
"""
from __future__ import annotations

import re
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent

# Les dossiers fouillés : le code qui CRÉE des clips pendant une séance.
DOSSIERS = ["core/src", "app/Source", "audio/src"]

# CE QUI EST EXCLU, ET POURQUOI -- écrit, pas deviné.
#  - interchange/ : chemins de lecture d'un projet, où `assignClipIds()` repasse.
#  - */tests/     : les tests posent leurs identifiants comme ils l'entendent.
#  - app/Source/tools/ : bancs de démonstration hors application, qui appellent
#    `assignClipIds()` eux-mêmes (ArrangementPreviewMain) ou ne persistent rien.
EXCLUS = re.compile(r"(^|/)(tests|interchange)/|app/Source/tools/")

POUSSE = re.compile(r"\bclips\.(push_back|emplace_back)\b")
NUMEROTE = re.compile(r"\.id\s*=|ajouterClip|assignClipIds")
CONSERVE = "identifiant conserv"   # sans l'accent final : « conservé » / « conservés »


def portees(lignes: list[str]) -> list[int]:
    """Les numéros de ligne qui OUVRENT une définition de premier niveau.

    La portée d'un identifiant, c'est la fonction — pas un nombre de lignes fixe
    choisi au jugé. Une fenêtre de six lignes manquait `copie.id = 0` posé huit
    lignes plus haut, et `assignClipIds()` appelé cinquante lignes plus bas.
    """
    return [i for i, ligne in enumerate(lignes)
            if ligne[:1] not in ("", " ", "\t", "#", "}", "/")]


def main() -> int:
    fautes: list[str] = []
    controles = 0
    for dossier in DOSSIERS:
        for fichier in sorted((RACINE / dossier).rglob("*")):
            if fichier.suffix not in (".cpp", ".h") or EXCLUS.search(str(fichier.relative_to(RACINE))):
                continue
            lignes = fichier.read_text(encoding="utf-8", errors="replace").splitlines()
            debuts = portees(lignes)
            for i, ligne in enumerate(lignes):
                if not POUSSE.search(ligne):
                    continue
                controles += 1
                if CONSERVE in ligne:
                    continue
                avant = max([d for d in debuts if d <= i], default=0)
                apres = min([d for d in debuts if d > i], default=len(lignes))
                if not NUMEROTE.search("\n".join(lignes[avant:apres])):
                    fautes.append(f"{fichier.relative_to(RACINE)}:{i + 1} : {ligne.strip()}")
    for f in fautes:
        print(f"CLIP SANS IDENTIFIANT  {f}")
    print(f"CLIPS_POUSSES {controles} SANS_IDENTIFIANT {len(fautes)}")
    return 1 if fautes else 0


if __name__ == "__main__":
    raise SystemExit(main())
