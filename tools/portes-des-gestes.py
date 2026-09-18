#!/usr/bin/env python3
"""Trois portes pour un geste : font-elles la MÊME chose ?

    analyse/.venv/bin/python tools/portes-des-gestes.py
    analyse/.venv/bin/python tools/portes-des-gestes.py --geste Quantifier

LA RÈGLE GARDÉE (18/09/2026, D356). Un geste d'édition du piano roll s'atteint
par le **menu** du clic droit (qui est aussi le menu Édition), par un
**raccourci** clavier, et parfois par un **bouton** de la barre d'outils. D355 a
mis les trois d'accord sur le NOM ; celle-ci les met d'accord sur l'EFFET. Chaque
porte est jouée sur le même projet, le `.mid` exporté est relu, et les trois
doivent rendre le **même multiensemble de notes**.

POURQUOI PAR MULTIENSEMBLE ET NON PAR LISTE TRIÉE. C'est la leçon payée trois
fois le 13/09 : un geste musical ne préserve pas l'ordre (le miroir INVERSE
l'ordre d'un accord), et comparer deux listes triées à l'aveugle fabrique des
écarts qui n'existent pas. Chaque note est réduite à `(hauteur, début, durée,
vélocité)` et l'on compte les exemplaires (`collections.Counter`).

**ET CHAQUE GESTE PORTE SON TÉMOIN.** Deux portes qui ne font RIEN sont d'accord
— parfaitement, et cela ne prouve rien. Une course sans geste donne le fichier de
référence, et un geste dont aucune porte ne change le fichier est signalé « SANS
EFFET VISIBLE » plutôt que compté comme une réussite : c'est la leçon de D145, et
celle de D265 (une mesure qui ne peut pas voir une chose doit le DIRE, jamais
compter zéro). Certains gestes ne se voient pas dans un `.mid` — rendre une note
muette, par exemple, relève du projet et non du fichier exporté ; ils sont nommés
ici, pas passés sous silence.

L'EXPORT DU BOUTON EST UN GESTE DIFFÉRÉ (D356). Le bouton exige une sélection
posée avant lui, donc un clic joué APRÈS le démarrage — or tous les verbes
d'export agissent AU démarrage, et D354 n'avait pu qu'en AVERTIR. D356 fait de
l'export un geste : `VSM_GESTE_APRES=<ms>:exporter-midi:<fichier>` prend son rang
dans la file, après le clic.

Rend 0 si toutes les portes s'accordent, 1 sinon, 2 si l'application ou mido
manquent.
"""
from __future__ import annotations

import collections
import json
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
BINAIRE = RACINE / "build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"

# LES GESTES ET LEURS PORTES. Le libellé du menu, la touche telle que
# `juce::KeyPress::createFromDescription` la lit (ou None : tous les gestes n'ont
# pas de raccourci), le texte du bouton (ou None).
#
# « HUMANISER » EN FAIT PARTIE, ET LE PREMIER JET L'EXCLUAIT À TORT. Il portait
# écrit ici que le geste « décale au hasard, et deux courses n'en donneraient
# jamais le même fichier » -- affirmé sans être vérifié, et faux : la graine est
# FIXE (`settings.seed = 0x5EED1234u`, PianoRollComponent.cpp:812), ce que son
# infobulle disait déjà (« de façon reproductible »). Un geste écarté sur une
# hypothèse qu'on n'a pas mesurée est un trou dans la garde, et celui-ci se
# refermait en lisant vingt lignes de code.
GESTES = [
    ("Quantifier",      "Quantifier (100 %)",           "ctrl + Q", "Quantifier"),
    ("Legato",          "Legato",                       "ctrl + L", "Legato"),
    ("Humaniser",       "Humaniser",                    None,       "Humaniser"),
    ("Supprimer",       "Supprimer",                    "delete",   None),
    ("Dupliquer",       "Dupliquer",                    "ctrl + D", None),
    ("Fusionner",       "Fusionner",                    "ctrl + J", None),
    ("Couper",          "Couper",                       "ctrl + X", None),
    ("Muet",            "Rendre muet / audible",        "ctrl + M", None),
    ("CouperTete",      "Couper à la tête de lecture",  "ctrl + E", None),
]


# LES GESTES DE SÉLECTION NE LAISSENT RIEN DANS LE `.mid` : ils ne changent pas
# une note, seulement ce qui est choisi. Leurs portes se comparent donc sur le
# RELEVÉ (`VSM_SELECTION : N note(s) choisie(s)`), que D357 a déplacé là où la
# sélection change -- il n'était écrit que par le menu, si bien que ces gestes-là
# étaient les seuls du piano roll qu'aucun banc ne pouvait mesurer.
# Chacun est joué APRÈS un « tout sélectionner », sans quoi « Tout
# désélectionner » serait grisé (D355) et « Inverser » partirait de rien.
SELECTIONS = [
    ("ToutSelectionner",   "Tout sélectionner",     "ctrl + A"),
    ("ToutDeselectionner", "Tout désélectionner",   "escape"),
    ("Inverser",           "Inverser la sélection", "ctrl + I"),
]


# LES GESTES DE L'ARRANGEMENT, dont les portes n'étaient comparées à RIEN avant
# D359 : son clavier était hors d'atteinte de tout banc (`getTextCharacter()` vaut
# zéro sur une touche fabriquée depuis sa description, et l'arrangement ne
# reconnaissait ses sept raccourcis que par ce caractère). Depuis qu'il consulte
# la table, les deux portes se comparent comme celles du piano roll.
# La sélection vient du menu Édition, qui passe AVANT les deux (D222).
# « Joindre » EXIGE DEUX CLIPS, et le projet n'en a qu'un : le geste est donc
# précédé d'une COUPE, dans les deux portes. Sans cela il rendrait « sans effet
# visible » — un projet incapable d'exercer un geste, et non un geste mort,
# la même leçon qu'à D356 pour « Fusionner » et « Couper à la tête ».
# Le quatrième champ dit CE QU'ON ATTEND du fichier : « change » (le geste laisse
# une trace) ou « revient » (la paire s'annule et le fichier doit redevenir celui
# du témoin). « Couper » puis « Joindre » sont des gestes INVERSES — le code le
# dit depuis D16.3 : « une paire de raccourcis inverses qui ne s'annulent pas est
# une paire cassée » —, et pour cette paire-là « rien n'a changé » est la
# RÉUSSITE, pas une absence de mesure.
ARRANGEMENT = [
    ("CouperClip",  "clip-midi:Couper à la tête de lecture", "ctrl + E", "change"),
    ("JoindreClip", "clip-midi:Couper à la tête de lecture;clip-midi:Joindre les clips choisis",
                     "ctrl + E;arrangement:ctrl + J", "revient"),
]


def ecrire_projet(dossier: Path) -> None:
    """Huit notes aux départs et aux durées tous différents — un geste qui ne
    toucherait qu'au temps, ou qu'aux hauteurs, se distingue alors des autres
    dans le fichier relu.

    ET DEUX PAIRES DE MÊME HAUTEUR, parce que le premier jet du banc n'en avait
    aucune : `joinNotes` ne fusionne que des notes de MÊME hauteur (« ce serait
    une seule note à deux hauteurs », `core/src/sequencer/NoteEdit.cpp:276`), si
    bien que « Fusionner » rendait « sans effet visible » — un projet incapable
    d'exercer le geste, et non un geste mort. Les hauteurs sont donc 60, 62, 62,
    64, 66, 66, 68, 70."""
    (dossier / "midi").mkdir(parents=True, exist_ok=True)

    def vlq(n: int) -> bytes:
        octets = [n & 0x7F]
        n >>= 7
        while n:
            octets.append((n & 0x7F) | 0x80)
            n >>= 7
        return bytes(reversed(octets))

    # (départ, durée, hauteur). La NOTE À CHEVAL sur la tête de lecture (posée à
    # la mesure 2 par le banc, soit 1 920 ticks) est la dernière : sans elle,
    # « Couper à la tête » n'aurait rien à couper.
    notes = [(17, 100, 60), (217, 160, 62), (491, 90, 62), (749, 200, 64),
              (946, 120, 66), (1207, 80, 66), (1462, 150, 68), (1800, 400, 70)]
    evenements = [(0, b"\xff\x03\x04Lead"), (0, b"\xff\x51\x03\x07\xa1\x20"),
                   (0, b"\xff\x58\x04\x04\x02\x18\x08")]
    horloge = 0
    for i, (debut, duree, hauteur) in enumerate(notes):
        evenements.append((max(0, debut - horloge), bytes([0x90, hauteur, 100 - i])))
        evenements.append((duree, bytes([0x80, hauteur, 0])))
        horloge = debut + duree
    corps = b"".join(vlq(max(0, dt)) + octets for dt, octets in evenements) + b"\x00\xff\x2f\x00"
    (dossier / "midi/arrangement.mid").write_bytes(
        b"MThd" + struct.pack(">IHHH", 6, 1, 1, 480)
        + b"MTrk" + struct.pack(">I", len(corps)) + corps)
    (dossier / "project.json").write_text(json.dumps({
        "format": "vsm-project", "version": 1, "title": "portes",
        "midi": {"file": "midi/arrangement.mid"},
        "transport": {"loop": {"enabled": False, "endTick": 0, "startTick": 0},
                       "tempoChanges": [{"bpm": 120.0, "tick": 0}],
                       "ticksPerQuarterNote": 480,
                       "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}]},
        "tracks": [{"channel": 0, "color": "#FF6B9BFF", "effects": [],
                     "instrument": {"preferredPlugin": "vsm.minimoog"},
                     "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0],
                              "solo": False, "volume": 1.0},
                     "name": "Lead"}]}, indent=1), encoding="utf-8")


def notes_du_midi(fichier: Path) -> collections.Counter | None:
    """Le MULTIENSEMBLE des notes d'un `.mid` : (hauteur, début, durée, vélocité)."""
    if not fichier.is_file() or fichier.stat().st_size == 0:
        return None
    import mido

    comptes: collections.Counter = collections.Counter()
    for piste in mido.MidiFile(str(fichier)).tracks:
        horloge = 0
        ouvertes: dict[int, tuple[int, int]] = {}
        for evenement in piste:
            horloge += evenement.time
            if evenement.type == "note_on" and evenement.velocity > 0:
                ouvertes[evenement.note] = (horloge, evenement.velocity)
            elif evenement.type in ("note_off", "note_on"):
                debut = ouvertes.pop(evenement.note, None)
                if debut is not None:
                    comptes[(evenement.note, debut[0], horloge - debut[0], debut[1])] += 1
    return comptes


def dernier_compte_de_selection(journal: Path) -> int | None:
    """Le DERNIER `VSM_SELECTION : N note(s) choisie(s)` du journal — le dernier,
    parce que la course en écrit un par changement et que c'est l'état final qui
    se compare."""
    if not journal.is_file():
        return None
    dernier = None
    for ligne in journal.read_text(encoding="utf-8", errors="replace").splitlines():
        if ligne.startswith("VSM_SELECTION : "):
            morceaux = ligne.split()
            if len(morceaux) > 2 and morceaux[2].isdigit():
                dernier = int(morceaux[2])
    return dernier


def course(brouillon: Path, nom: str, projet: Path, **variables: str) -> Path:
    """Une course de banc sous un HOME NEUF (D318 : un HOME réutilisé rouvre ses
    autosauvegardes avant le geste demandé)."""
    maison = Path(tempfile.mkdtemp(prefix=f"h-{nom}-", dir=brouillon))
    sortie = brouillon / f"{nom}.mid"
    env = dict(os.environ)
    # VSM_POSITION POUR TOUTES LES COURSES, TÉMOIN COMPRIS : la tête est un état
    # du projet dont plusieurs gestes dépendent, et une tête posée seulement pour
    # les gestes qui s'en servent ferait différer leur témoin du leur.
    env.update({"HOME": str(maison), "VSM_PROJET": str(projet), "VSM_TAILLE": "1280x742",
                 "VSM_VUE": "sans-rapport,pianoroll", "VSM_DELAI": "2600",
                 "VSM_POSITION": "2", "VSM_CAPTURE": str(brouillon / f"{nom}.png")})
    env.update({k: v for k, v in variables.items() if v})
    journal = brouillon / f"{nom}.txt"
    with journal.open("wb") as sortie_journal:
        subprocess.run([str(BINAIRE)], env=env, stdout=sortie_journal,
                        stderr=subprocess.STDOUT, timeout=120, check=False)
    return sortie


def main() -> int:
    if not BINAIRE.is_file():
        print("REFUS : l'application n'est pas compilée")
        return 2
    try:
        import mido  # noqa: F401
    except ImportError:
        print("REFUS : mido est requis pour relire les .mid")
        return 2
    voulu = None
    if "--geste" in sys.argv:
        voulu = sys.argv[sys.argv.index("--geste") + 1]

    brouillon = Path(tempfile.mkdtemp(prefix="vsm-portes-"))
    projet = brouillon / "projet"
    ecrire_projet(projet)

    print("=== D356 : trois portes pour un geste, un seul effet ===")
    # LE TÉMOIN D'ABORD : sans lui, « les portes sont d'accord » ne distingue pas
    # deux gestes identiques de deux gestes qui n'ont rien fait.
    temoin = notes_du_midi(course(brouillon, "temoin", projet,
                                   VSM_MENU_CONTEXTE="pianoroll:Tout sélectionner",
                                   VSM_EXPORT_MIDI=str(brouillon / "temoin.mid")))
    if not temoin:
        print("REFUS : le témoin n'a pas écrit de notes — la course elle-même a échoué")
        return 2
    print(f"       témoin : {sum(temoin.values())} notes, {len(temoin)} distinctes")

    rates, sans_effet = 0, []
    for nom, libelle, touche, bouton in GESTES:
        if voulu and voulu != nom:
            continue
        resultats: dict[str, collections.Counter | None] = {}
        resultats["menu"] = notes_du_midi(course(
            brouillon, f"{nom}-menu", projet,
            VSM_MENU_CONTEXTE=f"pianoroll:Tout sélectionner;pianoroll:{libelle}",
            VSM_EXPORT_MIDI=str(brouillon / f"{nom}-menu.mid")))
        if touche:
            resultats["raccourci"] = notes_du_midi(course(
                brouillon, f"{nom}-raccourci", projet,
                VSM_TOUCHE=f"pianoroll:ctrl + A;pianoroll:{touche}",
                VSM_EXPORT_MIDI=str(brouillon / f"{nom}-raccourci.mid")))
        if bouton:
            # LE BOUTON EXIGE UNE SÉLECTION POSÉE AVANT LUI, donc un clic DIFFÉRÉ
            # -- et un export différé derrière (D356), sans quoi le fichier
            # montrerait l'état d'avant le clic.
            resultats["bouton"] = notes_du_midi(course(
                brouillon, f"{nom}-bouton", projet,
                VSM_MENU_CONTEXTE="pianoroll:Tout sélectionner",
                VSM_GESTE_APRES=f"500:cliquer:{bouton};1100:exporter-midi:"
                                 + str(brouillon / f"{nom}-bouton.mid")))

        absentes = [porte for porte, valeur in resultats.items() if valeur is None]
        if absentes:
            print(f"  RATÉ {nom:12s} aucune note écrite par : {', '.join(absentes)}")
            rates += 1
            continue
        # Les portes sans fichier sont sorties ci-dessus : ce dictionnaire-ci n'a
        # plus de `None`, et le dire au typage évite de le supposer plus bas.
        lues: dict[str, collections.Counter] = {
            porte: valeur for porte, valeur in resultats.items() if valeur is not None
        }
        valeurs = list(lues.values())
        # UNE SEULE PORTE NE SE COMPARE À RIEN, et « 1 porte d'accord » serait une
        # réussite pour rien : le compte des portes est donc affiché, toujours.
        accord = all(v == valeurs[0] for v in valeurs[1:])
        change = valeurs[0] != temoin
        detail = " | ".join(f"{porte} {sum(v.values())} notes" for porte, v in lues.items())
        if not accord:
            # CE QUI DIFFÈRE, ET NON SEULEMENT QU'IL DIFFÈRE : deux portes qui se
            # séparent d'une note se cherchent longtemps sans la note en question.
            print(f"  RATÉ {nom:12s} les portes NE font pas la même chose — {detail}")
            reference = valeurs[0]
            for porte, v in list(lues.items())[1:]:
                if v != reference:
                    print(f"       {'':12s} {porte} : {sum((v - reference).values())} note(s) en plus, "
                          f"{sum((reference - v).values())} en moins")
            rates += 1
        elif not change:
            print(f"  ---- {nom:12s} SANS EFFET VISIBLE dans le .mid — {detail}")
            sans_effet.append(nom)
        else:
            print(f"  OK   {nom:12s} {len(lues)} porte(s) d'accord, et le fichier a changé — {detail}")

    # --- LES GESTES DE SÉLECTION, mesurés sur le relevé et non sur le fichier ---
    if not voulu or voulu in {nom for nom, _, _ in SELECTIONS}:
        print("    — gestes de sélection (relevé VSM_SELECTION, rien à lire dans le .mid) —")
    for nom, libelle, touche in SELECTIONS:
        if voulu and voulu != nom:
            continue
        comptes: dict[str, int | None] = {}
        course(brouillon, f"sel-{nom}-menu", projet,
                VSM_MENU_CONTEXTE=f"pianoroll:Tout sélectionner;pianoroll:{libelle}")
        comptes["menu"] = dernier_compte_de_selection(brouillon / f"sel-{nom}-menu.txt")
        course(brouillon, f"sel-{nom}-raccourci", projet,
                VSM_TOUCHE=f"pianoroll:ctrl + A;pianoroll:{touche}")
        comptes["raccourci"] = dernier_compte_de_selection(brouillon / f"sel-{nom}-raccourci.txt")
        detail = " | ".join(f"{porte} {valeur}" for porte, valeur in comptes.items())
        if any(valeur is None for valeur in comptes.values()):
            # UNE PORTE MUETTE N'EST PAS UNE PORTE D'ACCORD : c'est le défaut même
            # que D357 vient de réparer, et la garde doit le revoir s'il revient.
            print(f"  RATÉ {nom:12s} une porte n'a RIEN dit — {detail}")
            rates += 1
        elif len(set(comptes.values())) > 1:
            print(f"  RATÉ {nom:12s} les portes ne choisissent pas le même nombre — {detail}")
            rates += 1
        else:
            print(f"  OK   {nom:12s} 2 portes d'accord — {detail} note(s) choisie(s)")

    # --- LES GESTES DE L'ARRANGEMENT : menu du clip contre clavier (D359) ---
    if not voulu or voulu in {nom for nom, _, _, _ in ARRANGEMENT}:
        print("    — gestes de l'arrangement (le clavier y entre depuis D359) —")
    for nom, entree, touche, attendu in ARRANGEMENT:
        if voulu and voulu != nom:
            continue
        portes: dict[str, collections.Counter | None] = {}
        portes["menu"] = notes_du_midi(course(
            brouillon, f"arr-{nom}-menu", projet,
            VSM_VUE="sans-rapport,arrangement",
            VSM_MENU="Tout sélectionner dans l'arrangement",
            VSM_MENU_CONTEXTE=entree,
            VSM_EXPORT_MIDI=str(brouillon / f"arr-{nom}-menu.mid")))
        portes["clavier"] = notes_du_midi(course(
            brouillon, f"arr-{nom}-clavier", projet,
            VSM_VUE="sans-rapport,arrangement",
            VSM_MENU="Tout sélectionner dans l'arrangement",
            VSM_TOUCHE=f"arrangement:{touche}",
            VSM_EXPORT_MIDI=str(brouillon / f"arr-{nom}-clavier.mid")))
        if any(v is None for v in portes.values()):
            manquantes = [porte for porte, v in portes.items() if v is None]
            print(f"  RATÉ {nom:12s} aucune note écrite par : {', '.join(manquantes)}")
            rates += 1
            continue
        lues_arr = {porte: v for porte, v in portes.items() if v is not None}
        valeurs_arr = list(lues_arr.values())
        detail = " | ".join(f"{porte} {sum(v.values())} notes" for porte, v in lues_arr.items())
        revenu = valeurs_arr[0] == temoin
        if valeurs_arr[0] != valeurs_arr[1]:
            print(f"  RATÉ {nom:12s} les portes NE font pas la même chose — {detail}")
            rates += 1
        elif attendu == "revient" and revenu:
            print(f"  OK   {nom:12s} 2 portes d'accord, et la paire s'ANNULE "
                  f"(fichier identique au témoin, note pour note) — {detail}")
        elif attendu == "revient":
            print(f"  RATÉ {nom:12s} la paire ne s'annule pas : le fichier diffère du témoin — {detail}")
            rates += 1
        elif revenu:
            print(f"  ---- {nom:12s} SANS EFFET VISIBLE dans le .mid — {detail}")
            sans_effet.append(nom)
        else:
            print(f"  OK   {nom:12s} 2 portes d'accord, et le fichier a changé — {detail}")

    if sans_effet:
        print(f"--- {len(sans_effet)} geste(s) sans effet visible dans le .mid : {', '.join(sans_effet)}")
        print("    (ils ne sont NI réussis NI ratés : le fichier exporté ne les porte pas)")
    print(f"--- {rates} désaccord(s) entre portes")
    return 0 if rates == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
