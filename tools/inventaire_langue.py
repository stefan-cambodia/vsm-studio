#!/usr/bin/env python3
"""Inventaire A9 : les littéraux français d'app/Source qui atteignent l'écran sans tr().

    analyse/.venv/bin/python tools/inventaire_langue.py              # les comptes
    analyse/.venv/bin/python tools/inventaire_langue.py ECRAN        # et la liste
    analyse/.venv/bin/python tools/inventaire_langue.py --entetes    # les en-têtes
    analyse/.venv/bin/python tools/inventaire_langue.py --regle=large  # D101
    analyse/.venv/bin/python tools/inventaire_langue.py --machines [MACHINES]  # D103
    analyse/.venv/bin/python tools/inventaire_langue.py --sans-suivi # D106 : l'ancienne règle
    analyse/.venv/bin/python tools/inventaire_langue.py --doublons   # D216 : une clé écrite deux fois
    analyse/.venv/bin/python tools/inventaire_langue.py --garde      # D375 : .cpp ET .h, code 1 si un texte fuit
    analyse/.venv/bin/python tools/inventaire_langue.py --sans-d375  # D375 : le témoin, l'ancienne classification

LES EN-TÊTES À PART (D94). Le compte d'A9 ne lit que les `.cpp`, et c'est ce
compte-là que les phases comparent. Mais un `.h` écrit aussi à l'écran (une
infobulle posée dans une fonction en ligne, un nom de pièce de batterie) :
`--entetes` applique les mêmes règles aux en-têtes, en chiffre séparé, pour que
l'angle mort se mesure au lieu de se deviner.

POURQUOI UN INVENTAIRE DU CODE, EN PLUS DE CE QUI S'AFFICHE. `VSM_TEXTES_LISTE`
lit les textes que la fenêtre montre ; elle ne voit ni une boîte qu'on n'a pas
ouverte, ni une infobulle d'un panneau caché, ni ce que `paint()` dessine. Le
code, lui, contient tout. Les deux se complètent : l'inventaire dit ce qui
RESTE, la liste dit ce qui SE VOIT.

LES RÈGLES, écrites avant de compter (D94, ROADMAP-daw.md) :
  - des littéraux ADJACENTS (« u8"a" u8"b" ») sont une seule chaîne ;
  - une chaîne passée à tr(), trSelon(), trPhrase(), trGeste() ne compte pas...
  - ...SAUF si la table n'a pas sa clé : SANS_PAIRE, elle reste française en
    anglais aussi sûrement qu'une chaîne écrite sans tr() (trouvé par D94 :
    « Écoute A/B : … » et « Annuler (Ctrl+Z) » étaient dans ce cas) ;
  - TABLE : la chaîne est une clé de kAnglais ou un modèle de kModeles --
    traduite ailleurs, par une variable ;
  - COMMANDE : un mot ASCII minuscule, avec tirets, deux-points ou POINTS, sans
    espace (« monter-piste », « pistes.muet ») -- un jeton de banc, pas un
    texte. Le point est venu avec D150 : les noms de composant que les bancs
    désignent (D135, D145, D147) tombaient dans ÉCRAN, c'est-à-dire dans le
    chiffre même qui sert à juger la traduction ;
  - LIBELLÉ DE PAS (D150) : une chaîne confiée à `debutEdition()`,
    `onEditStarted()` ou `beginProjectEdit()` est le nom d'un pas d'annulation.
    La fenêtre d'historique le traduit BIEN PLUS LOIN, à la peinture, par
    `trGeste` : il n'est donc voisin d'aucun `tr(`. TABLE s'il a sa clé,
    SANS_PAIRE sinon. Testé AVANT le filtre « a l'air français », qui écarte un
    mot seul et capitalisé -- « Armement » n'était dans AUCUNE catégorie, et
    D149 l'a mesuré : la paire retirée, les cinq comptes ne bougeaient pas ;
  - TERMINAL : l'instruction écrit sur stderr ou stdout ;
  - ÉCRAN : tout le reste. C'est le chiffre d'A9.
`Langue.cpp` et `app/Source/tools/` sont hors du compte.

L'ANGLE MORT (D101). Une chaîne n'entre au compte que si elle a l'air
française : un accent ou un mot de FRANCAIS -- c'est la règle `stricte`, le
défaut. « Navigateur » ou « Preset illisible » n'en ont pas. Deux règles de
plus, en option tant que la mesure n'a pas tranché (`--regle=`) :
  - `mots` : les mots-outils français que l'anglais n'emploie pas, et les
    élisions (MOTS) -- aucun mot de contenu choisi d'après les ratés connus ;
  - `position` : une chaîne qui a des lettres, sans allure d'identifiant,
    passée dans une instruction qui affiche (AFFICHAGE) ;
  - `large` : l'une ou l'autre.

LES NOMS DES MACHINES (D103). Ils ne sont pas dans `app/Source` : le moteur les
donne (`audio/plugins/`), par deux voies -- le nom ENREGISTRÉ
(`VSM_REGISTER_SYNTH_PLUGIN`), que montrent la liste des pistes et le
navigateur, et `machineName()`, que montre le rack. `--machines` les lit tous
deux et compte ceux qui portent une description entre parenthèses sans avoir de
clé -- une description comme « (batterie acoustique) » n'a ni accent ni
mot-outil : la parenthèse est la règle, hors les noms IDENTIQUES dans les deux
langues, nommés un par un.

LE MESSAGE ASSEMBLÉ PUIS ÉCRIT AU TERMINAL (D106). La règle TERMINAL ne lit que
l'instruction de la chaîne ; un compte rendu assemblé dans une variable, puis
écrit par `fputs` plus loin, était compté ÉCRAN. `--suivi` suit la variable
LOCALE dans sa fonction (`va_seulement_au_terminal`) : si tous ses usages
suivants sont des ajouts ou des écritures au terminal, la chaîne est TERMINAL.
Mesurée sur le code de D105 (les 16 connues passées, une seule autre, juste),
c'est le DÉFAUT depuis D106 ; `--sans-suivi` rend l'ancienne règle, pour qu'un
témoin reste possible.

LES DIX QUI N'ÉTAIENT PAS À L'ÉCRAN (D375). ECRAN est resté à 10 pendant vingt
phases sans que personne en lise la liste ; lus un par un, AUCUN n'atteignait
l'écran. Trois règles de plus, `--sans-d375` pour le témoin :
  - le suivi de D106 admet `else v += …` comme `if (…) v += …` ;
  - COMMANDE admet la barre oblique : « gel/piste- » est un chemin relatif ;
  - HORS_ECRAN, compté et listé, jamais jeté : l'argument d'une recherche
    (`contains`, `startsWith`, `endsWith`, `indexOf`), le nom d'un fil
    (`juce::Thread(`), et ce qu'écrivent les fonctions de banc d'une liste
    FERMÉE (FONCTIONS_DE_BANC) -- une fonction neuve n'y entre qu'écrite ici.
D393 : le suivi de D106 compte aussi `v.add(…)` -- une liste qu'on remplit,
puis qu'on écrit au terminal -- comme un ajout ; une liste AFFICHÉE reste ECRAN
(vu rouge sur un `StringArray` injecté puis passé à `setText`).
Avec ECRAN à 0, l'inventaire devient une GARDE (`--garde`) : les `.cpp` ET les
en-têtes, code 1 au premier texte en ECRAN, NU ou SANS_PAIRE, chacun nommé. Vue
rouge sur une infobulle injectée, et sur l'en-tête qu'elle a trouvé :
`MixerComponent.h`, « Rendu muet par son dossier » posé sans `tr()`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

RACINE = Path(__file__).resolve().parents[1] / "app" / "Source"
MACHINES = Path(__file__).resolve().parents[1] / "audio" / "plugins"
FRANCAIS = re.compile(
    r"[éèêàâçùûôîïëœÉÈÊÀÇ]|\b(le|la|les|des|une|un|aucun|aucune|piste|pistes|réglage|"
    r"fichier|projet|ouvrir|enregistrer|lecture|arrêt|départ|touche|choisir|dossier|"
    r"réserve|sans|avec|pour|dans|sur)\b", re.I)
MOTS = re.compile(
    r"\b(de|du|et|est|pas|ne|au|aux|ce|cette|ces|qui|que|son|sa|ses|leur|leurs|puis|rien|"
    r"tout|tous|toute|toutes|vers|votre|vos)\b|\b(?:[ldnscj]|qu)['’]\w", re.I)
AFFICHAGE = re.compile(
    r"\b(montrerBoite|showMessageBoxAsync|setButtonText|setText|setTooltip|addItem|addSectionHeader|"
    r"addTab|drawText|drawFittedText|setTitle|addTextEditor|PanelWindow)\b")
REGLES = ("stricte", "mots", "position", "large")
IDENTIQUES = {"Test Tone (reference)", "Test Tone (reference Phase 2)"}
LITTERAL = re.compile(r'(?:u8)?"((?:[^"\\]|\\.)*)"')
TRADUCTION = re.compile(r"\b(tr|trSelon|trPhrase|trGeste|translate|TRANS)\s*\(\s*(u8)?\s*$")
# D150 : les fonctions qui reçoivent le NOM D'UN PAS d'annulation. Traduit à la
# peinture de la fenêtre d'historique (`trGeste`), donc jamais accolé à `tr(`.
LIBELLE_DE_PAS = re.compile(r"\b(debutEdition|onEditStarted|beginProjectEdit)\s*\(\s*(u8)?\s*$")
SORTIE = re.compile(r"fputs|stderr|stdout|std::cout|std::cerr|DBG\s*\(|printf")
CATEGORIES = ("ECRAN", "NU", "SANS_PAIRE", "TERMINAL", "TABLE", "COMMANDE",   # NU : D217
              "HORS_ECRAN")                                                 # D375
# D375 : l'argument d'une RECHERCHE dans un texte (jamais affiché : il le trie).
RECHERCHE = re.compile(r"\.(contains|containsIgnoreCase|startsWith|endsWith|indexOf)\s*\(\s*"
                       r"(?:juce::String::fromUTF8\s*\(\s*)?(?:u8)?\s*$")
# D375 : le NOM d'un fil, vu d'un débogueur, jamais d'un écran.
FIL = re.compile(r"\bjuce::Thread\s*\(\s*(?:u8)?\s*$")
# D375 : les fonctions de BANC, liste FERMÉE -- une fonction neuve n'y entre pas
# sans qu'on l'écrive ici. Tout ce qu'elles écrivent va au relevé d'un banc.
FONCTIONS_DE_BANC = ("parcourirLesTextes", "menusPourCapture")
EN_TETE_DE_FONCTION = re.compile(r"^[A-Za-z][^;{}\n]*?\b(\w+)\s*\(", re.M)


def sans_commentaires(texte: str) -> str:
    """Retire les commentaires en gardant les numéros de ligne."""
    texte = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), texte, flags=re.S)
    return "\n".join(re.sub(r'//(?=(?:[^"]*"[^"]*")*[^"]*$).*', "", ligne)
                     for ligne in texte.split("\n"))


def decode(litteral: str) -> str:
    """Le texte d'un littéral qui écrit ses accents en \\u00e9 ou \\xc3\\xa9."""
    if "\\u" in litteral:
        return re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), litteral)
    if "\\x" in litteral:
        octets = re.sub(r"\\x([0-9a-fA-F]{2})", lambda m: chr(int(m.group(1), 16)), litteral)
        return octets.encode("latin-1", "ignore").decode("utf-8", "ignore")
    return litteral


# D217 : LES APPELS QUI POSENT UN TEXTE À L'ÉCRAN TEL QUEL. Un littéral donné
# comme argument de l'un d'eux n'a aucune variable derrière lui : rien ne le
# traduira plus tard, et la règle TABLE -- « traduite ailleurs » -- est fausse
# pour lui. C'est le cas que D214 a trouvé à la main (le titre d'un FileChooser).
PUITS = re.compile(
    # `setName` N'EST PAS un puits : c'est l'identifiant d'un composant, que les
    # bancs désignent (D150) -- le nom d'un ColourSelector montré dans une
    # CallOutBox ne s'affiche nulle part. Première version de cette liste, il
    # faisait crier la garde sur « Couleur de la piste ».
    r"\b(FileChooser|AlertWindow|BoiteLisible|setButtonText|setTooltip|setTitle"
    r"|addComboBox|addTextEditor|addTextBlock|addButton|addItem|addSubMenu|addSectionHeader"
    r"|montrerBoite|montrerBoiteLisible|demanderOuiNon|showMessageBoxAsync|showYesNoCancelBox"
    r"|showOkCancelBox|setText|dialogTitle)\b")


def dans_un_appel_de_traduction(avant: str) -> bool:
    """La chaîne qui suit `avant` est-elle DANS un `tr(...)`, même loin de lui ?

    D217 : la règle de proximité (24 caractères) ne voit pas
    `tr(n > 1 ? u8"…" : u8"…")`, où le `tr(` est à quarante caractères et où la
    chaîne est pourtant traduite. On remonte donc au dernier appel de traduction et
    l'on compte les parenthèses : si elles sont encore ouvertes à l'endroit de la
    chaîne, elle est dedans. Sans cela, six ternaires honnêtes passaient pour des
    littéraux nus — et une garde qui crie faux six fois sur treize ne sert plus.
    """
    for m in reversed(list(re.finditer(r"\btr(?:Selon|Phrase|Geste)?\(", avant))):
        reste = avant[m.end():]
        if reste.count("(") - reste.count(")") >= 0:
            return True
    return False


def cles_de_la_table(langue: str) -> Set[str]:
    """Les clés françaises de kAnglais et de kModeles, littéraux adjacents recollés."""
    cles: Set[str] = set()
    for m in re.finditer(r'\{\s*((?:(?:u8)?"(?:[^"\\]|\\.)*"\s*)+),', langue):
        cles.add(decode("".join(LITTERAL.findall(m.group(1)))))
    return cles


def doublons_de_la_table(langue: str) -> List[Tuple[str, str, List[int]]]:
    """Les clés écrites DEUX FOIS dans la MÊME table de Langue.cpp.

    Pourquoi cette garde (D216). Le 13/09, D214 a ajouté « Importer un MIDI dans
    le projet... » à `kAnglais` alors que la clé y était déjà six cents lignes plus
    haut : deux entrées pour une clé, dont une seule est lue. Tant que les deux
    traductions sont identiques, rien ne se voit ; le jour où l'on corrige la
    MAUVAISE, la correction n'a aucun effet et l'on cherche ailleurs. Une clé
    présente dans DEUX tables différentes (l'interface et les phrases du moteur),
    en revanche, est légitime : `Impossible d'écrire %1` vient des deux, et ce
    n'est pas un doublon.
    """
    doubles: List[Tuple[str, str, List[int]]] = []
    for table in re.finditer(r"const\s+\w+\s+(k\w+)\s*\[\s*\]\s*=\s*\{(.*?)\n\};", langue, re.S):
        nom, corps = table.group(1), table.group(2)
        debut = table.start(2)
        vues: Dict[str, List[int]] = {}
        for m in re.finditer(r'\{\s*((?:(?:u8)?"(?:[^"\\]|\\.)*"\s*)+),', corps):
            cle = decode("".join(LITTERAL.findall(m.group(1))))
            ligne = langue[:debut + m.start()].count("\n") + 1
            vues.setdefault(cle, []).append(ligne)
        for cle, lignes in vues.items():
            if len(lignes) > 1:
                doubles.append((nom, cle, lignes))
    return doubles


def chaines(texte: str) -> List[Tuple[int, int, str]]:
    """(début, fin, texte) de chaque chaîne, les littéraux adjacents recollés."""
    groupes: List[Tuple[int, int, str]] = []
    for m in LITTERAL.finditer(texte):
        if groupes and texte[groupes[-1][1]:m.start()].strip() in ("", "u8"):
            debut, _, deja = groupes[-1]
            groupes[-1] = (debut, m.end(), deja + m.group(1))
        else:
            groupes.append((m.start(), m.end(), m.group(1)))
    return groupes


def ressemble_a_du_texte(chaine: str) -> bool:
    """D101, règle `position` : des lettres, et pas l'allure d'un identifiant."""
    if not re.search(r"[A-Za-zÀ-ÿ]{2}", chaine):
        return False
    return " " in chaine or not re.search(r"[._/:]|^[a-z]", chaine)


def a_l_air_francaise(chaine: str, avant: str, regle: str) -> bool:
    """La chaîne entre-t-elle au compte ? `avant` : son instruction, jusqu'à elle."""
    if FRANCAIS.search(chaine):
        return True
    if regle in ("mots", "large") and MOTS.search(chaine):
        return True
    return regle in ("position", "large") and bool(AFFICHAGE.search(avant)) and ressemble_a_du_texte(chaine)


AFFECTATION = re.compile(r"(?:^|[\s(;{}])(?:(?:juce::String|std::string|auto)\s+)?([A-Za-z]\w*)\s*(\+=|=(?!=)|\.add\s*\()")


def masquer_les_chaines(texte: str) -> str:
    """Le texte, chaque littéral vidé (même longueur) : un « ; » ou un nom écrits
    DANS une chaîne ne sont pas du code (D106, mesure 1)."""
    return LITTERAL.sub(lambda m: m.group(0)[:m.start(1) - m.start(0)] + " " * len(m.group(1)) + '"', texte)


def va_seulement_au_terminal(texte: str, debut: int, masque: str = "", d375: bool = True) -> bool:
    """D106 : la chaîne à `debut` est-elle assemblée dans une variable LOCALE que
    la fonction n'envoie qu'au terminal ? Chaque usage de la variable après son
    instruction doit être un nouvel ajout (`v +=`, `v =`) ou une instruction qui
    écrit sur la sortie d'erreur ; un seul autre usage (une boîte, un retour, un
    libellé) et la chaîne reste à l'écran. Un membre (`nom_`) n'est jamais suivi :
    l'interface peut le montrer ailleurs. Bornes et usages se cherchent dans
    `masque`, le texte aux littéraux vidés."""
    code = masque or masquer_les_chaines(texte)
    depart = max(code.rfind(c, 0, debut) for c in ";{}") + 1
    affectations = list(AFFECTATION.finditer(code[depart:debut]))
    if not affectations:
        return False
    nom = affectations[-1].group(1)
    if nom.endswith("_") or nom in ("return", "const"):
        return False
    fin_de_fonction = code.find("\n}\n", debut)
    if fin_de_fonction < 0:
        return False
    fin_instruction = code.find(";", debut)
    usages = 0
    for m in re.finditer(rf"\b{re.escape(nom)}\b", code[fin_instruction:fin_de_fonction]):
        position = fin_instruction + m.start()
        instruction = code[max(code.rfind(c, 0, position) for c in ";{}") + 1:code.find(";", position)]
        # D375 : un ajout sous `else` est un ajout comme sous `if (…)`.
        prefixe = r"(?:if\s*\([^;]*\)\s*|else\s+)?" if d375 else r"(?:if\s*\([^;]*\)\s*)?"
        # D393 : `v.add(…)` (une liste qu'on remplit) est un ajout comme `v += …`.
        if re.match(rf"\s*{prefixe}{re.escape(nom)}\s*(\+=|=(?!=)|\.add\s*\()", instruction):
            continue
        if not SORTIE.search(instruction):
            return False
        usages += 1
    return usages > 0


def fonction_englobante(texte: str, debut: int) -> str:
    """D375 : le nom de la dernière fonction ouverte en colonne 0 avant `debut`."""
    noms = [m.group(1) for m in EN_TETE_DE_FONCTION.finditer(texte, 0, debut)
            if not m.group(0).startswith(("if", "for", "while", "switch", "return"))]
    return noms[-1] if noms else ""


def inventaire(racine: Path = RACINE, motif: str = "*.cpp", regle: str = "stricte",
               suivi: bool = True, d375: bool = True) -> Dict[str, List[str]]:
    assert regle in REGLES, regle
    cles = cles_de_la_table((racine / "ui" / "Langue.cpp").read_text(encoding="utf-8"))
    comptes: Dict[str, List[str]] = {c: [] for c in CATEGORIES}
    for fichier in sorted(racine.rglob(motif)):
        if fichier.name == "Langue.cpp" or "tools" in fichier.relative_to(racine).parts:
            continue
        texte = sans_commentaires(fichier.read_text(encoding="utf-8"))
        masque = masquer_les_chaines(texte) if suivi else ""
        for debut, fin, brut in chaines(texte):
            chaine = decode(brut)
            avant = texte[max(texte.rfind(c, 0, debut) for c in ";{}") + 1:debut]
            # D150 : LE NOM D'UN PAS, avant tout filtre. Voir l'en-tête.
            if LIBELLE_DE_PAS.search(texte[max(0, debut - 32):debut]):
                ligne = texte.count("\n", 0, debut) + 1
                comptes["TABLE" if chaine in cles else "SANS_PAIRE"].append(
                    f"{fichier.relative_to(racine)}:{ligne}: {chaine[:100]}")
                continue
            if chaine.startswith("VSM_") or not a_l_air_francaise(chaine, avant, regle):
                continue
            if TRADUCTION.search(texte[max(0, debut - 24):debut]):
                # trSelon(clé, contexte) et trPhrase (modèles) ont leurs propres
                # tables : seule la clé nue de tr() se vérifie ici.
                appel = TRADUCTION.search(texte[max(0, debut - 24):debut])
                if appel is not None and appel.group(1) == "tr" and chaine not in cles:
                    ligne = texte.count("\n", 0, debut) + 1
                    comptes["SANS_PAIRE"].append(
                        f"{fichier.relative_to(racine)}:{ligne}: {chaine[:100]}")
                continue
            instruction = texte[texte.rfind(";", 0, debut) + 1:texte.find(";", fin)]
            # D150 : le point, pour « pistes.muet » ; D375 : la barre, pour « gel/piste- »
            if re.fullmatch(r"[a-z0-9\-:./]+" if d375 else r"[a-z0-9\-:.]+", chaine):
                categorie = "COMMANDE"
            elif SORTIE.search(instruction):
                categorie = "TERMINAL"
            elif chaine in cles:
                # D217 : posé tel quel dans un appel qui AFFICHE -> personne ne le
                # traduira. Compté à part pour ne pas déplacer le chiffre d'A9.
                categorie = ("NU" if PUITS.search(avant) and not dans_un_appel_de_traduction(avant)
                             else "TABLE")
            elif suivi and va_seulement_au_terminal(texte, debut, masque, d375):
                categorie = "TERMINAL"   # D106 : assemblée, puis écrite au terminal
            elif d375 and (RECHERCHE.search(avant) or FIL.search(avant)
                           or fonction_englobante(texte, debut) in FONCTIONS_DE_BANC):
                categorie = "HORS_ECRAN"
            else:
                categorie = "ECRAN"
            ligne = texte.count("\n", 0, debut) + 1
            comptes[categorie].append(f"{fichier.relative_to(racine)}:{ligne}: {chaine[:100]}")
    return comptes


def noms_de_machines(plugins: Path = MACHINES) -> List[Tuple[str, str]]:
    """D103 : (dossier, nom) -- les noms enregistrés et les `machineName()` des machines."""
    noms: Set[Tuple[str, str]] = set()
    for fichier in sorted(plugins.rglob("*.[ch]*")):
        texte = fichier.read_text(encoding="utf-8", errors="replace")
        dossier = fichier.relative_to(plugins).parts[0]
        for motif in (r'VSM_REGISTER_SYNTH_PLUGIN\(\s*"[^"]+"\s*,\s*"((?:[^"\\]|\\.)*)"',
                      r'machineName\(\)\s*const[^{;]*\{\s*return\s*"((?:[^"\\]|\\.)*)"'):
            for m in re.finditer(motif, texte):
                noms.add((dossier, decode(m.group(1)).replace("\\'", "'")))
    return sorted(noms)


def machines_sans_cle(racine: Path = RACINE, plugins: Path = MACHINES
                      ) -> Tuple[List[Tuple[str, str]], List[Tuple[str, str]], List[Tuple[str, str]]]:
    """D103 : (tous les noms, ceux qui portent une description, ceux-là sans clé)."""
    cles = cles_de_la_table((racine / "ui" / "Langue.cpp").read_text(encoding="utf-8"))
    tous = noms_de_machines(plugins)
    decrits = [(d, n) for d, n in tous if "(" in n and n not in IDENTIQUES]
    return tous, decrits, [(d, n) for d, n in decrits if n not in cles]


def main() -> int:
    options = [a for a in sys.argv[1:] if a.startswith("--")]
    arguments = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--machines" in options:
        tous, decrits, sans = machines_sans_cle()
        print(f"MACHINES {len(tous)} noms   DECRITS {len(decrits)}   SANS_CLE {len(sans)}")
        if "MACHINES" in arguments:
            for dossier, nom in sans:
                print(f"  M {dossier}: {nom}")
        return 0
    if "--doublons" in options:
        langue = (RACINE / "ui" / "Langue.cpp").read_text(encoding="utf-8")
        doubles = doublons_de_la_table(langue)
        print(f"DOUBLONS {len(doubles)}")
        for nom, cle, lignes in doubles:
            print(f"  D {nom} lignes {', '.join(str(ligne) for ligne in lignes)} : {cle[:90]}")
        return 1 if doubles else 0
    regle = next((o.split("=", 1)[1] for o in options if o.startswith("--regle=")), "stricte")
    if regle not in REGLES:
        print(f"règle inconnue : {regle} (attendu : {', '.join(REGLES)})", file=sys.stderr)
        return 2
    d375 = "--sans-d375" not in options
    if "--garde" in options:
        # D375 : la garde lit les .cpp ET les en-têtes, et nomme ce qu'elle trouve.
        fautes = 0
        for motif in ("*.cpp", "*.h"):
            comptes = inventaire(motif=motif, regle=regle, suivi="--sans-suivi" not in options, d375=d375)
            print(f"[{motif}] " + "   ".join(f"{c} {len(v)}" for c, v in comptes.items()))
            for categorie in ("ECRAN", "NU", "SANS_PAIRE"):
                for entree in comptes[categorie]:
                    print(f"  RATÉ {categorie} {entree}")
                    fautes += 1
        print(f"GARDE : {fautes} texte(s) qui atteindraient l'écran sans traduction")
        return 1 if fautes else 0
    comptes = inventaire(motif="*.h" if "--entetes" in options else "*.cpp", regle=regle,
                         suivi="--sans-suivi" not in options, d375=d375)
    print("   ".join(f"{c} {len(v)}" for c, v in comptes.items() if d375 or c != "HORS_ECRAN"))
    for categorie in arguments:
        for entree in comptes.get(categorie.upper(), []):
            print(f"  {categorie[0].upper()} {entree}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
