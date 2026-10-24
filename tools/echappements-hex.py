#!/usr/bin/env python3
"""UN « \\xNN » N'A QUE DEUX CHIFFRES : aucun échappement hexadécimal d'une chaîne C++ n'avale la lettre qui le suit.

    python3 tools/echappements-hex.py            # la garde, sur les sources C++ du dépôt
    python3 tools/echappements-hex.py --essai    # la garde jugée sur un cas de chaque forme

LA RÈGLE GARDÉE (24/10/2026, D586). En C++, `\\x` prend TOUS les chiffres hexadécimaux qui le suivent : dans
`"d\\xc3\\xa9filement"`, l'échappement est `\\xa9f` (0xa9f, hors intervalle pour un `char`), le « f » disparaît, et le
relevé écrit « dÃilement ». Le dépôt écrit ses accents en octets là où `u8` est ambigu (CLAUDE.md, le piège de
`char8_t`), et la parade est de couper la chaîne : `"d\\xc3\\xa9" "filement"`. Payé à D333 (« d\\xc3\\xa9but »), repayé
au mixeur (le commentaire « REPAYÉ ICI » de `MixerComponent.cpp`), puis deux fois sans le voir : D581 (« d\\xc3\\xa9calé »
dans `VSM_CONTROLEURS_SUIVENT`) et D586 (« d\\xc3\\xa9filement » dans `VSM_SUIVI`). Le compilateur le dit — « séquence
d'échappement hexadécimale hors intervalle » —, noyé dans quelque 380 avertissements que personne ne lit.

CE QU'ELLE LIT : les fichiers `.cpp`, `.h`, `.hpp`, `.mm` d'`app/`, `audio/`, `core/`, `interchange/`, `panels/`,
`clap/`, `vst3/` et `tools/`, hors dossiers de construction. Un petit lexique en extrait les littéraux de chaîne et de
caractère — les COMMENTAIRES sont sautés (trois des cinq motifs que `grep` trouve y sont, et racontent justement ce
piège), les chaînes BRUTES `R"(…)"` aussi (elles n'interprètent rien), et l'apostrophe séparatrice de chiffres (`1'000`)
n'ouvre pas de littéral. Dans chaque littéral, les échappements se lisent de gauche à droite, si bien que `"\\\\xa9c"`
(une barre échappée, puis du texte) n'est pas un `\\x`. Elle REFUSE (code 2) si elle ne lit aucun littéral.

Rend 0 si aucun échappement ne dépasse deux chiffres, 1 sinon, 2 si elle ne lit plus rien.
"""

from __future__ import annotations

import re
import sys
from collections.abc import Iterator
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
DOSSIERS = ("app", "audio", "core", "interchange", "panels", "clap", "vst3", "tools")
SUFFIXES = {".cpp", ".h", ".hpp", ".mm"}
BRUT = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\v\f\n]{0,16})\(')
ECHAPPEMENT = re.compile(r"\\(?:x([0-9a-fA-F]+)|.)", re.S)


def litteraux(texte: str) -> Iterator[tuple[int, str]]:
    """(ligne, contenu) de chaque littéral de chaîne ou de caractère NON brut, hors commentaires."""
    i, n, ligne = 0, len(texte), 1
    while i < n:
        c = texte[i]
        if c == "\n":
            ligne += 1
            i += 1
            continue
        if texte.startswith("//", i):
            j = texte.find("\n", i)
            i = n if j < 0 else j
            continue
        if texte.startswith("/*", i):
            j = texte.find("*/", i + 2)
            fin = n if j < 0 else j + 2
            ligne += texte.count("\n", i, fin)
            i = fin
            continue
        avant = texte[i - 1] if i > 0 else " "
        m = BRUT.match(texte, i)
        if m and not (avant.isalnum() or avant == "_"):
            fermeture = ")" + m.group(1) + '"'
            j = texte.find(fermeture, m.end())
            fin = n if j < 0 else j + len(fermeture)
            ligne += texte.count("\n", i, fin)
            i = fin
            continue
        if c == "'" and avant.isdigit() and texte[i - 2:i] != "u8":
            i += 1   # 1'000 : un séparateur de chiffres, pas un littéral
            continue
        if c in "\"'":
            j = i + 1
            while j < n and texte[j] != c and texte[j] != "\n":
                j += 2 if texte[j] == "\\" else 1
            yield ligne, texte[i + 1:j]
            i = j + 1
            continue
        i += 1


def fautes(texte: str) -> Iterator[tuple[int, str]]:
    for ligne, contenu in litteraux(texte):
        for m in ECHAPPEMENT.finditer(contenu):
            if m.group(1) is not None and len(m.group(1)) > 2:
                yield ligne, m.group(0)


ESSAIS = [   # (source, nombre de fautes attendu)
    ('const char* a = "d\\xc3\\xa9filement";', 1),            # le défaut : « \xa9f »
    ('const char* a = "d\\xc3\\xa9" "filement";', 0),         # la parade : la chaîne coupée
    ('auto b = u8"\\xa9c";', 1),                              # un préfixe ne change rien
    ('// « \\xa9c » mange le « c »\nint x;', 0),             # un commentaire ne compte pas
    ('/* \\xa9b */ int y;', 0),                               # ni un commentaire de bloc
    ('auto r = R"(\\xa9c)";', 0),                             # une chaîne brute n'interprète rien
    ('auto r = R"d(" \\xa9c )d"; auto s = "\\xa9c";', 1),     # … et sa fin est trouvée
    ('auto e = "\\\\xa9c";', 0),                              # une barre échappée, puis du texte
    ('int m = 1\'000; auto s = "\\xa9c";', 1),               # le séparateur n'ouvre pas de littéral
    ("char c = '\\x41'; char d = '\\x41a';", 1),             # les littéraux de caractère aussi
    ('auto s = "\\x1b[0m";', 0),                              # un échappement suivi d'autre chose
]


def essai() -> int:
    rates = 0
    for source, attendu in ESSAIS:
        obtenu = len(list(fautes(source)))
        if obtenu != attendu:
            rates += 1
            print(f"  RATÉ {source!r} : {obtenu} faute(s), {attendu} attendue(s)")
    print(f"ÉCHAPPEMENTS (essai) : {len(ESSAIS) - rates} / {len(ESSAIS)} cas jugés comme attendu")
    return 1 if rates else 0


def main() -> int:
    if "--essai" in sys.argv[1:]:
        return essai()
    lus = fichiers = 0
    trouvees: list[str] = []
    for dossier in DOSSIERS:
        base = RACINE / dossier
        if not base.is_dir():
            continue
        for chemin in sorted(base.rglob("*")):
            if chemin.suffix not in SUFFIXES or any(p.startswith("build") for p in chemin.relative_to(RACINE).parts):
                continue
            texte = chemin.read_text(encoding="utf-8", errors="replace")
            fichiers += 1
            lus += sum(1 for _ in litteraux(texte))
            for ligne, echappement in fautes(texte):
                trouvees.append(f"{chemin.relative_to(RACINE)}:{ligne} « {echappement} »")
    if lus == 0:
        print("ÉCHAPPEMENTS : aucun littéral lu — la garde ne lit plus les sources (refus)")
        return 2
    for t in trouvees:
        print("  FAUTE " + t + " — plus de deux chiffres : la lettre qui suit est avalée")
    print(f"ÉCHAPPEMENTS : {lus} littéral(aux) lu(s) dans {fichiers} fichier(s), {len(trouvees)} échappement(s) qui avalent")
    return 1 if trouvees else 0


if __name__ == "__main__":
    sys.exit(main())
