#!/usr/bin/env python3
"""L'AUDIT DE D506 : UNE ENTRÉE DE MENU QUI MODIFIE LE PROJET EMPILE UN PAS D'ANNULATION.

    tools/annulation-des-menus.py [chemin/du/binaire] [--seulement LIBELLÉ] [--etat N]

`--etat 2` ne JUGE que le second état (le premier est relevé, pas joué : il faut
savoir ce qu'il aurait jugé) — pour relire une entrée neuve sans rejouer les 166.

LA RÈGLE GARDÉE (29/09/2026). D36.4 l'a posée pour les widgets de la ligne de
piste (`vsm-edit-audit`) : un geste qui change le projet sans `beginProjectEdit`
ne supprime pas le Ctrl+Z, il le DÉCALE — le Ctrl+Z suivant remonte à
l'instantané d'avant et annule deux gestes en un, sans le dire. Aucun banc ne
l'éprouvait sur les ENTRÉES DE MENU.

COMMENT. Un projet de trois pistes est engendré. Une course « liste » choisit
toutes les notes du piano roll (`touche:pianoroll:ctrl + A`) puis relève les
menus (`lister-menus`) : les entrées ACTIVES des menus qui modifient le morceau
— Édition, Piste, Transport, Enregistrement, Mixage — sont retenues, sauf celles
qui ouvrent une fenêtre (libellé en « … » ou « ... »). Puis, pour chaque entrée
— jouée par son CHEMIN (`menu:Mixage > Delay > Gate`, D510 : les sous-menus de
chaque bus répètent les mêmes libellés) —, une course
sous un HOME neuf (D318) : tout choisir, l'entrée (`menu:`), enregistrer
(`enregistrer:`), relever l'historique (`relever-historique`) — les quatre en
différé, dans cet ordre. Une course témoin fait la même chose SANS l'entrée.

LE VERDICT, PAR ENTRÉE :
  * le morceau a changé (project.json sans son bloc `view`, et le .mid) ET un pas
    de plus → juste ;
  * le morceau a changé SANS pas → SUSPECT, et c'est ce que la garde attrape ;
  * un pas SANS changement → « pas pour rien » (un Ctrl+Z qui n'annule rien) :
    dit depuis D506, ROUGE depuis D512 (D511 l'a mis à zéro) ;
  * ni l'un ni l'autre → l'entrée ne touche pas au morceau (vue, écoute…).
Les exceptions — un changement légitimement sans pas — sont écrites ci-dessous
avec leur raison, jamais tacites.

DEUX ÉTATS (D512) : les notes du piano roll choisies, puis tous les clips de
l'arrangement choisis et des locateurs posés — le second ne rejoue que les
entrées que le premier laissait grisées.

Rend 0 si aucune entrée n'est suspecte, non jugée ni « pour rien », 1 sinon, 2 si
le binaire manque.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
BIN = RACINE / "build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
MENUS = ("Édition", "Piste", "Transport", "Enregistrement", "Mixage")

# D514 : LES ENTRÉES LÉGITIMEMENT MUETTES — sans effet sur le morceau et sans trace
# relevable —, chacune avec sa raison. Vide avant la mesure ; rempli entrée par
# entrée après l'avoir lue, jamais par famille.
MUETTES_VOULUES: dict[str, str] = {
    # AU BORD : l'état d'essai a la tête à 0, la première piste choisie, toutes les
    # notes choisies et aucun repère. Une commande de navigation qui n'a nulle part où
    # aller laisse les choses où elles devaient aller — et le dit en ne bougeant pas.
    "Édition > Sélection > Tout sélectionner": "toutes les notes sont déjà choisies (le Ctrl+A de l'état d'essai)",
    "Édition > Sélection > Toutes les notes de même hauteur": "toutes les notes sont déjà choisies : leurs "
                                                               "hauteurs rendent la même sélection",
    "Piste > Étendre le choix vers le haut": "la piste choisie est la première : rien au-dessus",
    "Transport > Retour au début": "la tête est déjà au début (tick 0)",
    "Transport > Marqueur précédent": "le projet d'essai n'a aucun repère, et la tête est à 0",
    "Transport > Marqueur suivant": "le projet d'essai n'a aucun repère : rien à atteindre",
    "Transport > Tête au début de la sélection": "la sélection commence au tick 0, où la tête est déjà",
    "Transport > Tête : temps précédent": "la tête est au début : rien avant",
    "Transport > Tête : mesure précédente": "la tête est au début : rien avant",
    "Transport > Fin de boucle à la tête": "la tête est à 0 : une fin de boucle à 0 ne délimite rien, et le geste "
                                          "ne pose rien (ni pas d'annulation)",
    "Enregistrement > Couper toutes les notes (panic)": "une commande d'urgence du moteur : aucune note ne sonne, "
                                                        "il n'y a rien à couper, et rien ne s'en écrit",
    # D516 : les menus contextuels des clips.
    "clip-midi-tous:Couleur de la piste": "le clip est implicite : il porte déjà la couleur de sa piste",
    "clip-audio-tous:Couleur de la piste": "le clip importé porte déjà la couleur de sa piste",
    "clip-midi-tous:Zoom : tout voir": "l'arrangement est déjà cadré sur tout le morceau (cadrage d'ouverture)",
    "clip-audio-tous:Zoom : tout voir": "l'arrangement est déjà cadré sur tout le morceau (cadrage d'ouverture)",
}

# LES CHANGEMENTS LÉGITIMEMENT SANS PAS, avec leur raison. Une exception tacite
# serait la panne muette que cette garde traque.
EXCEPTIONS = {
    "Boucle (marche / arrêt)": "la boucle marche/arrêt est une bascule de transport, comme dans Cubase et Live : "
                               "elle s'enregistre avec le morceau mais ne fait pas de pas (D0, D503)",
    "Métronome (marche / arrêt)": "le clic, même règle que la boucle (D503)",
}


def vlq(n: int) -> bytes:
    b = [n & 0x7F]
    n >>= 7
    while n:
        b.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(b))


def engendrer(dossier: Path, locateurs: tuple[int, int] = (0, 0), courbe: bool = False) -> None:
    (dossier / "midi").mkdir(parents=True)
    def piste(nom: str, base: int, tempo: bytes = b"") -> bytes:
        evs = ([(0, tempo)] if tempo else []) + [(0, b"\xff\x03" + bytes([len(nom)]) + nom.encode())]
        for i in range(8):
            h = base + (i * 5) % 12
            evs += [(0 if i == 0 else 240, bytes([0x90, h, 60 + i * 8])), (240 + i * 30, bytes([0x80, h, 0]))]
        corps = b"".join(vlq(dt) + o for dt, o in evs) + b"\x00\xff\x2f\x00"
        return b"MTrk" + struct.pack(">I", len(corps)) + corps
    (dossier / "midi/arrangement.mid").write_bytes(
        b"MThd" + struct.pack(">IHHH", 6, 1, 3, 480)
        + piste("une", 48, b"\xff\x51\x03\x07\xa1\x20") + piste("deux", 60) + piste("trois", 72))
    def t(nom: str) -> dict:
        piste = {"channel": 0, "color": "#FF6B9BFF", "effects": [],
                 "instrument": {"preferredPlugin": "vsm.minimoog"},
                 "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                 "name": nom}
        if courbe and nom == "une":   # D512 : sans courbe, les formes d'automation sont grisées
            piste["automation"] = [{"parameter": "mix.volume", "points": [{"tick": 0, "value": 1.0}]}]
        return piste
    (dossier / "project.json").write_text(json.dumps({
        "format": "vsm-project", "version": 1, "title": "annulation",
        "midi": {"file": "midi/arrangement.mid"},
        "transport": {"loop": {"enabled": False, "endTick": locateurs[1], "startTick": locateurs[0]},
                      "tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                      "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
        "tracks": [t("une"), t("deux"), t("trois")]}, indent=1))


def lancer(binaire: Path, brouillon: Path, nom: str, projet: Path, gestes: str, delai: int,
           extra: dict[str, str] | None = None) -> tuple[str, Path]:
    maison = Path(tempfile.mkdtemp(dir=brouillon, prefix="home."))
    # D514 : LES QUESTIONS SONT RÉPONDUES « OUI » (`VSM_CONFIRMER`, D235). « Reporter la
    # piste en audio (définitif) » demandait « Reporter | Annuler » ; sans réponse, elle
    # passait pour « sans effet » et n'avait jamais été jouée jusqu'au bout.
    env = dict(os.environ, HOME=str(maison), VSM_TAILLE="1280x800", VSM_PROJET=str(projet),
               VSM_VUE="sans-rapport", VSM_DELAI=str(delai), VSM_GESTE_APRES=gestes,
               VSM_CAPTURE=str(brouillon / f"{nom}.png"), VSM_CONFIRMER="oui")
    env.update(extra or {})
    try:
        r = subprocess.run([str(binaire)], env=env, capture_output=True, text=True,
                           errors="replace", timeout=60)
        return r.stdout + r.stderr, maison
    except subprocess.TimeoutExpired as e:
        return ((e.stdout or "") + (e.stderr or "") if isinstance(e.stdout, str) else "TIMEOUT"), maison


def vue(dossier: Path) -> str:
    """Le bloc `view` écrit — ce que l'empreinte du morceau écarte."""
    fichier = dossier / "project.json"
    if not fichier.exists():
        return "absent"
    return json.dumps(json.loads(fichier.read_text(encoding="utf-8")).get("view"), sort_keys=True)


def preferences(maison: Path) -> str:
    """Les préférences écrites, SANS `projetsRecents` : l'enregistrement du banc y
    inscrit un chemin différent à chaque course, et toute course aurait « changé »."""
    fichier = maison / "VintageSynthMidiStudio" / "VintageSynthMidiStudio.settings"
    if not fichier.exists():
        return ""
    return "\n".join(x for x in fichier.read_text(encoding="utf-8", errors="replace").splitlines()
                     if 'name="projetsRecents"' not in x)


def boites(journal: str) -> set[str]:
    return {x for x in journal.splitlines() if x.startswith("VSM_BOITE : ")}


def selection(journal: str) -> str | None:
    """Le compte de notes choisies, écrit par le piano roll quand il CHANGE (D357)."""
    m = re.findall(r"VSM_SELECTION : (\d+)", journal)
    return m[-1] if m else None


def pistes_choisies(journal: str) -> str | None:
    """Le compte de pistes choisies, écrit par la liste quand il CHANGE (D514)."""
    m = re.findall(r"VSM_PISTES_CHOISIES : (\d+)", journal)
    return m[-1] if m else None


def tete(journal: str) -> str | None:
    m = re.findall(r"VSM_TETE : tick (-?\d+)", journal)
    return m[-1] if m else None


def menus_montres(journal: str) -> list[str]:
    """La barre relevée APRÈS le geste : libellés, coches, entrées grisées. Un outil
    choisi se coche, une sélection active « Couper » — l'état se voit au menu."""
    return [x for x in journal.splitlines() if x.startswith("VSM_MENU_LISTE : ")]


def ligne_d_etat(journal: str) -> str | None:
    m = [x.split(" : ", 1)[1] for x in journal.splitlines() if x.startswith("VSM_ETAT_PIANOROLL : ")]
    return m[-1] if m else None


def empreinte(dossier: Path) -> str:
    """Le MORCEAU écrit : project.json sans son bloc `view` (le cadrage n'est pas le
    morceau), et chaque fichier des sous-dossiers (le .mid, les presets…)."""
    h = hashlib.sha256()
    fichier = dossier / "project.json"
    if not fichier.exists():
        return "absent"
    doc = json.loads(fichier.read_text(encoding="utf-8"))
    doc.pop("view", None)
    h.update(json.dumps(doc, sort_keys=True).encode())
    for f in sorted(dossier.rglob("*")):
        if f.is_file() and f.name != "project.json":
            h.update(f.relative_to(dossier).as_posix().encode())
            h.update(f.read_bytes())
    return h.hexdigest()[:16]


def pas(journal: str) -> int:
    m = re.findall(r"VSM_HISTORIQUE_PAS : (\d+)", journal)
    return int(m[-1]) if m else -1


def libelles_des_pas(journal: str) -> str:
    """D517 : les LIBELLÉS de l'historique relevé. Un « pas pour rien » compté dans la
    série complète (« Tous les canaux », 29/09) n'a pas reparu rejoué seul, trois
    fois — et son journal était effacé avec le brouillon : on ne savait pas QUEL pas
    avait été compté. Le verdict le dit désormais, avec ceux du témoin."""
    m = re.findall(r"VSM_HISTORIQUE_PAS : \d+ : ?(.*)", journal)
    return m[-1].strip() if m else "?"


def main() -> int:
    args = [a for a in sys.argv[1:]]
    seulement = None
    if "--seulement" in args:
        i = args.index("--seulement")
        seulement = args[i + 1]
        del args[i:i + 2]
    seul_etat = None
    if "--etat" in args:
        i = args.index("--etat")
        seul_etat = int(args[i + 1])
        del args[i:i + 2]
    binaire = Path(args[0]) if args else BIN
    if not binaire.exists():
        print(f"REFUS : {binaire} absent — compiler d'abord")
        return 2
    brouillon = Path(tempfile.mkdtemp(dir=os.environ.get("TMPDIR", "/tmp"), prefix="vsm-annulation-menus."))
    # LE BROUILLON EST UN tmpfs (CLAUDE.md) : une course y laissait 36 Mo de projets
    # écrits — dix courses, 360 Mo de RAM. CE brouillon, et lui seul, est effacé.
    try:
        return auditer(binaire, brouillon, seulement, seul_etat)
    finally:
        shutil.rmtree(brouillon, ignore_errors=True)


# D512 : DEUX ÉTATS DU MORCEAU. Dans le premier (les notes du piano roll choisies,
# pas de locateurs), seize entrées restaient GRISÉES — tout ce qui agit sur la
# sélection de l'ARRANGEMENT ou entre les LOCATEURS (répéter, découper aux
# transitoires, dessiner l'automation, insérer ou supprimer du temps…) — et
# l'audit ne les avait jamais jouées. Le second état choisit tous les clips,
# pose des locateurs et donne une courbe à la piste « une » ; il ne rejoue que
# ce que le premier n'a pas pu jouer (le reste l'a été, et le dire suffit : deux
# fois 166 courses doubleraient la série). Joué la première fois, il a trouvé
# les cinq formes d'automation actives sur une piste sans courbe, qui ne
# traçaient rien et ne le disaient qu'au journal.
ETATS = (
    # (nom, geste qui choisit, locateurs posés dans le projet, une courbe sur la piste « une »,
    #  les entrées qui PROUVENT l'état : elles doivent être actives, sinon l'état n'a pas pris)
    ("notes choisies", "300:touche:pianoroll:ctrl + A;", (0, 0), False, ()),
    ("clips choisis, locateurs posés, une courbe", "300:menu:Édition > Tout sélectionner dans l'arrangement;",
     (960, 2880), True,
     ("Édition > Répéter la sélection (à la suite) > 2 fois",            # les clips choisis
      "Édition > Insérer du silence entre les locateurs",                  # les locateurs
      "Édition > Dessiner l'automation sur la sélection > Rampe montante")),   # la courbe
)


# D516 : LES MENUS CONTEXTUELS. Celui du piano roll EST le menu Édition (une seule
# définition) : l'audit de la barre le couvre. Restent ceux des clips et des règles.
CONTEXTES = ("clip-midi-tous", "clip-audio-tous", "regle", "regle-pianoroll")
# Ce qu'il faut taper pour qu'une entrée aboutisse (comme `gestes-vivants.py`).
OPTIONS_CONTEXTE = {"Renommer…": "nom=Essai", "Poser un repère ici…": "nom=Essai"}
# Les entrées NON jouées, et pourquoi — dites à chaque course.
NON_JOUEES_CONTEXTE = {
    "Couleur…": "un sélecteur de couleur, qu'aucun verbe de banc ne remplit",
    "Transcrire en MIDI (Basic Pitch, Python)": "un processus Python de plusieurs secondes (mesuré par D20.4)",
}


def ecrire_un_son(chemin: Path) -> None:
    """Deux secondes de la 220 Hz, engendrées ici : une garde ne dépend pas d'un fichier voisin."""
    import math
    import wave
    with wave.open(str(chemin), "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(44100)
        f.writeframes(b"".join(struct.pack("<hh", v, v) for v in
                               (int(12000 * math.sin(2 * math.pi * 220.0 * i / 44100)) for i in range(88200))))


def relever_contextes(binaire: Path, brouillon: Path, projet: Path,
                      contexte: dict[str, str]) -> tuple[list[tuple[str, str, bool]], list[str]]:
    """Les entrées ACTIVES des menus contextuels (« quel:libellé »), et celles écartées."""
    liste, _ = lancer(binaire, brouillon, "liste-contextes", projet, "", 1500,
                      dict(contexte, VSM_MENU_CONTEXTE=";".join(f"{q}:?" for q in CONTEXTES)))
    retenues: list[tuple[str, str, bool]] = []
    ecartees: list[str] = []
    for x in liste.splitlines():
        if not x.startswith("VSM_MENU_CONTEXTE : ") or " = " not in x:
            continue
        quel, libelles = x.split("VSM_MENU_CONTEXTE : ", 1)[1].split(" = ", 1)
        for brut in libelles.split(" | "):
            if "[grisee]" in brut:
                continue
            libelle = re.sub(r" \{[^}]*\}", "", brut.replace(" [cochee]", "")).strip()
            if libelle in NON_JOUEES_CONTEXTE:
                ecartees.append(f"{quel}:{libelle}")
                continue
            retenues.append((f"{quel}:{libelle}", libelle, "[cochee]" in brut))
    return retenues, ecartees


def relever_entrees(binaire: Path, brouillon: Path, projet: Path, choisir: str,
                    nom: str) -> list[tuple[str, str, bool]]:
    liste, _ = lancer(binaire, brouillon, f"liste-{nom}", projet, f"{choisir}700:lister-menus", 1200)
    lignes = [x.split("VSM_MENU_LISTE : ", 1)[1] for x in liste.splitlines() if x.startswith("VSM_MENU_LISTE : ")]
    chemins = [x.split(" [")[0] for x in lignes]
    parents = {c.rsplit(" > ", 1)[0] for c in chemins}
    barre = ("Fichier", "Édition", "Piste", "Transport", "Enregistrement", "Mixage", "Affichage", "Aide")
    entrees = [(c, x) for x, c in zip(lignes, chemins, strict=True)
               if c.split(" > ")[0] in barre and c not in parents and "[titre]" not in x]
    retenues = []
    for c, ligne in entrees:
        menu = c.split(" > ")[0]
        texte = c.rsplit(" > ", 1)[-1]
        if menu not in MENUS or "[grisée]" in ligne:
            continue
        if texte.rstrip().endswith(("...", "…")) or "..." in texte or "…" in texte:
            continue
        # D510 : L'ENTRÉE EST JOUÉE PAR SON CHEMIN (`menu:Mixage > Delay > Gate`) : les
        # sous-menus de chaque bus répètent les mêmes libellés, et le premier venu
        # était toujours celui du premier bus. Plus aucune entrée n'est hors d'atteinte.
        retenues.append((c, texte, "[cochée]" in ligne))
    return retenues


def auditer(binaire: Path, brouillon: Path, seulement: str | None, seul_etat: int | None = None) -> int:
    total = {"suspects": 0, "incompletes": 0, "pour_rien": 0, "justes": 0, "muettes": 0, "traces": 0, "voulues": 0}
    deja: set[str] = set()
    juges = 0
    for k, (nom, choisir, locateurs, courbe, preuves) in enumerate(ETATS):
        projet = brouillon / f"projet-{k}"
        engendrer(projet, locateurs, courbe)
        retenues = relever_entrees(binaire, brouillon, projet, choisir, f"{k}")
        if seulement:
            retenues = [r for r in retenues if r[1] == seulement or r[1].startswith(seulement)]
        neuves = [r for r in retenues if r[0] not in deja]
        rejouees = len(retenues) - len(neuves)
        print(f"=== D506 — état « {nom} » : les entrées de menu qui modifient le morceau empilent un pas "
              f"({len(neuves)} entrées" + (f" ; {rejouees} déjà jugées dans l'état précédent, non rejouées" if k else "")
              + ") ===")
        # LA GARDE DE LA GARDE : si le geste qui choisit n'a pas pris, l'état
        # « clips choisis » serait à peu près le premier une seconde fois, et se
        # tairait. « Aucune entrée neuve » ne suffisait pas : sans le geste, les
        # locateurs et la courbe en ouvrent encore trois (vu rouge, D512). Chaque
        # ingrédient de l'état a son entrée-preuve.
        absentes = [c for c in preuves if c not in {r[0] for r in retenues}]
        if absentes and not seulement:
            for c in absentes:
                print(f"  RATÉ l'état « {nom} » n'a pas pris : « {c} » n'est pas active")
            total["incompletes"] += len(absentes)
            continue
        deja |= {r[0] for r in retenues}
        if seul_etat is not None and seul_etat != k + 1:
            print(f"    (état {k + 1} relevé, NON JUGÉ : --etat {seul_etat})")
            continue
        juges += 1
        for cle, n in juger(binaire, brouillon, projet, choisir, neuves, f"{k}").items():
            total[cle] += n
    if seul_etat is None or seul_etat == len(ETATS) + 1:
        juges += 1
        for cle, n in auditer_contextes(binaire, brouillon, seulement).items():
            total[cle] += n
    else:
        print(f"    (menus contextuels NON JUGÉS : --etat {seul_etat})")
    print(f"--- {total['suspects']} entrée(s) suspecte(s), {total['incompletes']} non jugée(s), "
          f"{total['pour_rien']} pas pour rien, {total['muettes']} muette(s), {total['justes']} juste(s), "
          f"{total['traces']} sans effet avec trace, {total['voulues']} muette(s) voulue(s), "
          f"{juges} famille(s) jugée(s) sur {len(ETATS) + 1}")
    return 1 if total["suspects"] or total["incompletes"] or total["pour_rien"] or total["muettes"] else 0


def auditer_contextes(binaire: Path, brouillon: Path, seulement: str | None) -> dict[str, int]:
    projet = brouillon / "projet-contextes"
    engendrer(projet)
    son = brouillon / "son.wav"
    ecrire_un_son(son)
    contexte = {"VSM_IMPORT_AUDIO": str(son)}
    retenues, ecartees = relever_contextes(binaire, brouillon, projet, contexte)
    if seulement:
        retenues = [r for r in retenues if r[1] == seulement or r[1].startswith(seulement)]
    print(f"=== D516 — les menus contextuels ({', '.join(CONTEXTES)}) : {len(retenues)} entrées ===")
    for c in ecartees:
        print(f"    NON JOUÉE, et dit : {c} — {NON_JOUEES_CONTEXTE[c.split(':', 1)[1]]}")
    # LA GARDE DE LA GARDE : chaque menu doit avoir été LU. Un clip audio que l'import
    # n'aurait pas posé rendrait son menu vide, et l'audit tout vert.
    lus = {r[0].split(":", 1)[0] for r in retenues} | {c.split(":", 1)[0] for c in ecartees}
    manquants = [q for q in CONTEXTES if q not in lus]
    if manquants and not seulement:
        for q in manquants:
            print(f"  RATÉ le menu « {q} » n'a rendu AUCUNE entrée active")
        return {"incompletes": len(manquants)}
    # UN TÉMOIN PAR MENU, QUI CHOISIT COMME L'ENTRÉE CHOISIT. Les verbes « -tous »
    # choisissent tous les clips avant d'agir ; un témoin qui ne choisit rien
    # donnerait à CHAQUE entrée une fausse trace « menu » (« Répéter » s'active), et
    # une muette s'y cacherait. Le témoin joue un libellé qui n'existe pas : même
    # sélection, aucune action.
    total: dict[str, int] = {}
    for quel in CONTEXTES:
        siennes = [r for r in retenues if r[0].startswith(quel + ":")]
        if not siennes:
            continue
        temoin = dict(contexte, VSM_MENU_CONTEXTE=f"{quel}:(témoin : aucune entrée)")
        for cle, n in juger(binaire, brouillon, projet, "", siennes, f"ctx-{quel}", temoin).items():
            total[cle] = total.get(cle, 0) + n
    return total


def juger(binaire: Path, brouillon: Path, projet: Path, choisir: str,
          retenues: list[tuple[str, str, bool]], etat: str,
          contexte: dict[str, str] | None = None) -> dict[str, int]:
    """`contexte` (D516) : l'environnement du témoin des menus contextuels ; chaque
    entrée « quel:libellé » est alors jouée par `VSM_MENU_CONTEXTE`, au démarrage."""
    # D510 : DE LA MARGE, ET UNE MESURE ABSENTE N'EST PAS UN ZÉRO. Dans la course
    # complète, quatre entrées (des accords) ont rendu « non jouée » ou « suspecte » :
    # rejouées seules, toutes justes. Sous la charge, un geste différé peut tomber
    # après la photo qui clôt la course ; le relevé d'historique manquait, et
    # l'ancien code le comptait « aucun pas » — un faux suspect fabriqué par le banc.
    def course(nom: str, gestes: str, extra: dict[str, str] | None = None) -> tuple[str, int, str, str, str]:
        sortie = brouillon / f"ecrit-{etat}-{nom}"
        j, maison = lancer(binaire, brouillon, f"{etat}-{nom}", projet,
                           # la ligne d'état AVANT l'enregistrement du banc, qui la réécrit
                           # (« 8 note(s) sélectionnée(s)… », une course sur deux)
                           f"{choisir}{gestes}1180:relever-tete;1200:lister-menus;1250:relever-etat;"
                           f"1300:enregistrer:{sortie};"
                           "1700:relever-historique", 2800, dict(contexte or {}, **(extra or {})))
        return empreinte(sortie), pas(j), j, vue(sortie), preferences(maison)

    def jouer(i: int, essai: int, chemin: str) -> tuple[str, int, str, str, str, bool]:
        if contexte is None:
            e, p, j, v, pr = course(f"e{i}-{essai}", f"700:menu:{chemin};")
            return e, p, j, v, pr, "exécutée" in j and f"« {chemin}" in j
        quel, libelle = chemin.split(":", 1)
        extra = {"VSM_MENU_CONTEXTE": chemin}
        if libelle in OPTIONS_CONTEXTE:
            extra["VSM_OPTIONS"] = OPTIONS_CONTEXTE[libelle]
        e, p, j, v, pr = course(f"e{i}-{essai}", "", extra)
        return e, p, j, v, pr, f"« {libelle} » exécutée ({quel})" in j

    e0, p0, j0, v0, pr0 = course("temoin", "")
    if e0 == "absent" or p0 < 0:
        print(f"  RATÉ témoin illisible (empreinte {e0}, pas {p0})")
        return {"incompletes": 1}
    suspects = 0
    justes = 0
    incompletes = []
    pour_rien = []
    neutres = []
    muettes = 0
    voulues = 0
    for i, (chemin, texte, cochee) in enumerate(retenues):
        for essai in (1, 2):   # une mesure incomplète est rejouée UNE fois
            e, p, j, v, pr, joue = jouer(i, essai, chemin)
            if joue and p >= 0 and e != "absent":
                break
        if not joue or p < 0 or e == "absent":
            manque = ("l'entrée n'a pas été jouée" if not joue
                      else "le relevé d'historique manque" if p < 0 else "le projet n'a pas été écrit")
            print(f"  ?    {chemin} — NON JUGÉE, deux fois : {manque} (une mesure absente n'est pas un zéro)")
            incompletes.append(chemin)
            continue
        change, empile = e != e0, p > p0
        if change and empile:
            print(f"  OK   {chemin}")
            justes += 1
        elif change and not empile:
            if texte in EXCEPTIONS:
                print(f"  OK   {chemin} — sans pas, et c'est voulu : {EXCEPTIONS[texte]}")
                justes += 1
            else:
                print(f"  SUSPECT {chemin} — le morceau change SANS pas d'annulation "
                      f"(pas : [{libelles_des_pas(j)}] ; témoin : [{libelles_des_pas(j0)}])")
                suspects += 1
        elif empile:
            # D512 : ROUGE, DÉSORMAIS. D511 a mis les « pas pour rien » à zéro ; un
            # Ctrl+Z qui n'annule rien (et qui vide la branche « rétablir ») ne
            # revient pas en silence.
            pour_rien.append(chemin)
            print(f"  PAS POUR RIEN {chemin} — un pas SANS changement du morceau "
                  f"(pas : [{libelles_des_pas(j)}] ; témoin : [{libelles_des_pas(j0)}])")
        else:
            # D514 : SANS EFFET SUR LE MORCEAU, MAIS PAS SANS TRACE. D512 a trouvé dans
            # cette case cinq entrées actives, inertes et muettes. Chaque trace est
            # relevée et nommée ; une entrée qui n'en laisse aucune est MUETTE.
            traces = []
            if v != v0:
                traces.append("vue")
            neuves = sorted(boites(j) - boites(j0))
            if neuves:
                traces.append("boîte « " + neuves[0].split(" : ", 1)[1][:70] + " »")
            ld = ligne_d_etat(j)
            if ld is not None and ld != ligne_d_etat(j0):
                traces.append(f"ligne d'état « {ld[:60]} »")
            if pr != pr0:
                traces.append("préférence")
            if selection(j) != selection(j0):
                traces.append(f"sélection {selection(j0)} → {selection(j)} note(s)")
            if pistes_choisies(j) != pistes_choisies(j0):
                traces.append(f"pistes choisies {pistes_choisies(j0)} → {pistes_choisies(j)}")
            if tete(j) is not None and tete(j) != tete(j0):
                traces.append(f"tête {tete(j0)} → {tete(j)}")
            m, m0 = menus_montres(j), menus_montres(j0)
            if m and m != m0:
                autre = sorted(set(m) - set(m0))
                traces.append("menu « " + (autre[0].split(" : ", 1)[1][-60:] if autre else "ordre") + " »")
            if cochee:
                traces.append("déjà cochée")
            neutres.append(chemin)
            if traces:
                print(f"  trace {chemin} — {' ; '.join(traces)}")
            elif chemin in MUETTES_VOULUES:
                print(f"  muette {chemin} — et c'est voulu : {MUETTES_VOULUES[chemin]}")
                voulues += 1
            else:
                print(f"  MUETTE {chemin} — ni vue, ni boîte, ni ligne d'état, ni préférence, ni tête, "
                      "ni menu, ni sélection, ni pistes choisies, ni cochée")
                muettes += 1
    print(f"    {len(neutres)} entrée(s) sans effet sur le morceau, dont {muettes} MUETTE(S)")
    return {"suspects": suspects, "incompletes": len(incompletes), "pour_rien": len(pour_rien), "justes": justes,
            "muettes": muettes, "voulues": voulues, "traces": len(neutres) - muettes - voulues}

if __name__ == "__main__":
    sys.exit(main())
