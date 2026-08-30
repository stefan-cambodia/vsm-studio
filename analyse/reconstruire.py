#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
WAV/MP3 -> projet VSM rejouable (MIDI + patchs), et distance publiée.

    .venv/bin/python reconstruire.py morceau.mp3
    .venv/bin/python reconstruire.py morceau.wav --sortie ./ma-reconstruction
    .venv/bin/python reconstruire.py morceau.wav --sans-separation --machines vsm.sh101,vsm.juno106
    .venv/bin/python reconstruire.py morceau.mp3 --sans-sampler   # que des synthés

Ce que la commande produit :

    sortie/
      project.json  midi/  instruments/   <- le projet, ouvrable dans le DAW
      rapport.json                        <- distance par stem, machines écartées
      reconstruit.wav                     <- le rendu du projet
      comparaison.wav                     <- original à gauche, reconstruction à droite

CE QU'ELLE NE PROMET PAS : reconstruire n'est pas reproduire. Les machines du
parc sont des synthétiseurs ; une guitare acoustique n'a pas de machine cible.
La distance publiée le dira, et c'est pour cela qu'elle est publiée.

COMMENT LE FICHIER EST ORGANISÉ. La chaîne est une suite d'étapes, et chacune
est une fonction qui porte le nom de l'étape, dans l'ordre où `main` les
appelle :

    obtenir_stems          [2/5]  séparation, ou stems repris, ou mélange entier
    charger_classifieur(s)        les modèles appris, vérifiés ou refusés
    reporter_voix                 le stem vocal, reporté tel quel sur une piste audio
    reconstruire_batterie         détection des coups, arbitrage des boîtes, réglage
    reconstruire_stem_melodique   transcription, recherche de patch, arbitrage, réglage
    assembler_pistes       [4/5]  pistes d'export, automation de coupure, niveaux
    verdict_du_melange            ce qui rapproche le morceau, et ce qui est écarté
    aligner_rapport_sur_projet    le rapport décrit le projet ÉCRIT
    figer_presets                 les valeurs d'usine inscrites, pas héritées
    rendre_et_mesurer      [5/5]  rendu, distance globale, écoute A/B

Ce que les étapes lisent sans le modifier est dans `Contexte` ; ce qu'elles
accumulent pour l'export est dans `Chantier`. Une étape qui doit arrêter la
chaîne lève `Abandon` avec le code de sortie, et `main` l'imprime.
"""

from __future__ import annotations

import argparse
import contextlib
import os
import struct
import subprocess
import sys
import tempfile
import time
import wave
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterator, List, Optional, Sequence, Tuple

import math

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_automation import try_cutoff_automation  # noqa: E402
from analyzer.vsm_drumkit import (build_drum_kit, drum_kit_track, drum_machine_track,  # noqa: E402
                                  modelled_drum_track, vocal_audio_track,
                                  vocal_sampler_track)
from analyzer.vsm_engine import VsmEngine, find_vsm_render  # noqa: E402
from analyzer.vsm_levels import VOLUME_MAX, match_track_levels  # noqa: E402
from analyzer.vsm_mix_verdict import MixAlternative, keep_what_helps_the_mix  # noqa: E402
from analyzer.vsm_project_export import (DEFAULT_TRACK_VOLUME, ExportNote, ExportTrack,  # noqa: E402
                                          write_project_bundle)
from analyzer.vsm_reconstruct import (StemNote, StemReconstruction, melodic_machines,  # noqa: E402
                                      reconstruct_stem, reconstruction_distance,
                                      write_reconstruction_report)
from analyzer.vsm_track_arbitration import (ORIGINE_USINE, TrackCandidate, arbitrate_on_track,  # noqa: E402
                                             build_candidates, runners_up)
from analyzer.vsm_track_refine import refine_patch_on_track  # noqa: E402

SAMPLE_RATE = 44100

# Marge de l'égalité serrée pour la BATTERIE. Plus large que les 2 % des stems
# mélodiques, et la raison est mesurée : les patchs d'usine des boîtes à rythmes
# sont loin des réglages d'un morceau (la 808 d'usine à 0,251 descend à 0,209
# réglée, soit 17 %), donc un écart d'usine de 40 % peut se refermer au réglage.
# Le coût est borné : une seconde boîte réglée, c'est 40 évaluations de plus.
CLOSE_MARGIN_BATTERIE = 0.50

# Les boîtes à rythmes du parc qui concourent contre la batterie modélisée.
BOITES_A_RYTHMES = ("vsm.tr909", "vsm.tr808")

# COMBIEN DE MACHINES L'ARBITRAGE REMET EN JEU AU VERDICT DU MÉLANGE.
#
# Trois, et le chiffre vient d'une mesure et non d'un goût. Sur *Sky and Sand*
# (§ 5 decies), la machine que le mélange retient est TROISIÈME au stem pour la
# basse et DEUXIÈME pour `other` : deux suffiraient de justesse, trois couvrent
# les deux cas observés.
#
# ÉLARGIR A ÉTÉ MESURÉ, ET N'AURAIT RIEN RAPPORTÉ. L'objection est réelle -- sur
# `other`, `vsm.tb303` est DERNIÈRE au stem, à plus de 50 %, et bat pourtant au
# mélange la machine publiée. Mais elle ne GAGNE pas : sur les deux pistes
# mesurées, la gagnante du mélange est dans les trois premières du stem. Aller à
# cinq coûterait une minute de plus par piste (un rendu de projet et une
# distance chacune, une quinzaine de secondes) pour des candidates dont aucune
# ne l'emporte. À remesurer au premier morceau qui démentira ça.
MACHINES_AU_MELANGE = 3

# Nom de la piste de batterie dans le projet écrit, sous lequel le verdict du
# mélange, le rapport et les niveaux la retrouvent.
PISTE_BATTERIE = "Batterie"


class Abandon(Exception):
    """La chaîne s'arrête ici ; `code` est le code de sortie du programme."""

    def __init__(self, code: int, message: str) -> None:
        super().__init__(message)
        self.code = code


# ---------------------------------------------------------------------------
# Audio : lecture, écriture, mesure
# ---------------------------------------------------------------------------

def charger_audio(chemin: Path) -> np.ndarray:
    """Charge un fichier audio en mono, à SAMPLE_RATE."""
    import librosa

    audio, _ = librosa.load(str(chemin), sr=SAMPLE_RATE, mono=True)
    return np.asarray(audio, dtype=np.float32)


def lire_wav(chemin: Path) -> np.ndarray:
    """Lit un WAV écrit par le moteur (float32 ou entier), rendu en mono."""
    octets = Path(chemin).read_bytes()
    entete = octets.find(b"fmt ")
    format_code, canaux = struct.unpack("<HH", octets[entete + 8 : entete + 12])
    debut = octets.find(b"data")
    taille = struct.unpack("<I", octets[debut + 4 : debut + 8])[0]
    brut = octets[debut + 8 : debut + 8 + taille]
    if format_code == 3:
        valeurs = np.frombuffer(brut, dtype=np.float32)
    else:
        valeurs = np.frombuffer(brut, dtype="<i2").astype(np.float32) / 32768.0
    if canaux > 1:
        valeurs = valeurs.reshape(-1, canaux).mean(axis=1)
    return valeurs


def ecrire_wav(chemin: Path, canaux: Sequence[np.ndarray]) -> None:
    longueur = max(c.size for c in canaux)
    empile = np.zeros((longueur, len(canaux)), dtype=np.float32)
    for index, canal in enumerate(canaux):
        empile[: canal.size, index] = canal
    empile = np.clip(empile, -1.0, 1.0)
    with wave.open(str(chemin), "wb") as sortie:
        sortie.setnchannels(len(canaux))
        sortie.setsampwidth(2)
        sortie.setframerate(SAMPLE_RATE)
        sortie.writeframes((empile * 32767).astype("<i2").tobytes())


def niveau_efficace(audio: np.ndarray) -> Optional[float]:
    """Niveau efficace (RMS) du stem, ou None s'il est vide.

    C'est la référence du garde-fou de niveau des rendus hors ligne : une
    candidate qui ne peut pas atteindre ce niveau est écartée, pas comparée.
    """
    if not audio.size:
        return None
    return float(np.sqrt(np.mean(np.square(audio.astype(np.float64)))))


# ---------------------------------------------------------------------------
# Transcription et séparation
# ---------------------------------------------------------------------------

def velocite_locale(audio: np.ndarray, debut: float, fenetre: float = 0.20) -> float:
    """Niveau efficace du stem au moment où la note commence, sur `fenetre`.

    C'est l'ATTAQUE qu'on mesure, pas la note entière : ce qui règle la
    vélocité d'un instrument est la force du geste au départ, et une note
    tenue qui s'éteint ne doit pas en être pénalisée.
    """
    i = int(debut * SAMPLE_RATE)
    j = min(i + int(fenetre * SAMPLE_RATE), audio.size)
    if i >= j:
        return 0.0
    return float(np.sqrt(np.mean(np.square(audio[i:j], dtype=np.float64))))


def extraire_notes(chemin: Path) -> List[StemNote]:
    """
    Transcrit un fichier en notes.

    LA VÉLOCITÉ VIENT DE L'ÉNERGIE DU SON, PAS DE LA CONFIANCE DU
    TRANSCRIPTEUR, et c'est un correctif mesuré. La version précédente posait
    `velocity = 40 + 87 x confiance` avec ce raisonnement : « une note détectée
    de justesse ne doit pas sonner aussi fort qu'une note franche ». C'est
    plausible et c'est faux -- savoir QU'UNE note existe n'est pas savoir si
    elle a été jouée fort. Le résultat était une dynamique écrasée : sur la
    piste « other » de *Sky and Sand*, 4 280 notes entre 58 et 103 d'écart-type
    7,9, quand le stem varie du simple au double. Le morceau reconstruit avait
    donc les bonnes notes et pas le bon GESTE, ce qui s'entend immédiatement --
    et ce que la métrique v2 ne voyait pas.

    Mesuré sur cette piste, à notes, machine et durées identiques : l'accord
    entre l'enveloppe du rendu et celle du stem passe de **0,361 à 0,508**.

    La confiance garde son VRAI rôle, qui est le seul qu'elle puisse tenir :
    signaler le doute à l'oreille dans le piano roll (§ 11.3 de
    ROADMAP-fusion.md). Elle n'est plus confondue avec une nuance.
    """
    from analyzer.note_extraction import extract_notes

    audio = charger_audio(chemin)
    brutes = [b for b in extract_notes(chemin) if float(b["end"]) > float(b["start"])]
    if not brutes:
        return []

    # RÉFÉRENCE AU 90e CENTILE, ET NON AU MAXIMUM : un seul transitoire -- un
    # claquement, une saturation -- écraserait toutes les autres notes s'il
    # servait d'étalon. Le 90e centile est la nuance forte du morceau.
    niveaux = [velocite_locale(audio, float(b["start"])) for b in brutes]
    positifs = [n for n in niveaux if n > 0.0]
    reference = float(np.percentile(positifs, 90)) if positifs else 1.0

    notes = []
    for brut, niveau in zip(brutes, niveaux):
        confiance = float(brut.get("confidence", 0.8))
        # RACINE CARRÉE : l'oreille entend le niveau en gros comme sa racine,
        # et une échelle linéaire tasserait toutes les nuances moyennes vers le
        # bas. Le plancher à 8 garde une note audible plutôt que muette : une
        # note transcrite est une note qui a sonné.
        part = min(niveau / reference, 1.0) if reference > 0.0 else 0.0
        notes.append(
            StemNote(
                note=int(brut["midi"]),
                velocity=max(8, min(127, int(round(127.0 * math.sqrt(part))))),
                start=float(brut["start"]),
                duration=float(brut["end"]) - float(brut["start"]),
                # La confiance ne règle plus la vélocité : elle signale le doute
                # dans le piano roll, et c'est tout ce qu'elle sait faire.
                confidence=confiance,
            )
        )
    return notes


def notes_export(notes: Sequence[StemNote]) -> List[ExportNote]:
    """Les notes d'un stem telles que le projet et les rendus hors ligne les lisent."""
    return [ExportNote(n.note, n.velocity, n.start, n.duration) for n in notes]


def separer(chemin: Path, dossier: Path, modele: str) -> Dict[str, Path]:
    from analyzer.separation import separate_audio

    return {nom: Path(p) for nom, p in separate_audio(chemin, dossier, modele).items()}


# ---------------------------------------------------------------------------
# Profils multi-échantillons et provenance
# ---------------------------------------------------------------------------

def profil_de(moteur, machine: str) -> str:
    """Nom du profil multi-échantillons de `machine`, ou chaîne vide.

    C'est le NOM déclaré par le profil, pas son chemin : c'est lui que porte un
    projet exporté, et c'est ce qui permet de l'ouvrir sur un autre poste pourvu
    que la banque y soit installée.
    """
    if moteur is None:
        return ""
    try:
        chemin = moteur.profile_for(machine)
    except Exception:
        return ""
    if not chemin:
        return ""
    for profil in moteur.profiles():
        if profil.get("path") == chemin:
            return str(profil.get("name") or "")
    return ""


def profils_de(moteur, machines: Sequence[str]) -> Dict[str, str]:
    """Machine -> nom de profil, pour celles qui en ont un.

    Le PROFIL suit la machine dans tous les rendus hors ligne. L'oublier ne se
    voyait pas : la piste sortait muette, le garde-fou de niveau écartait la
    candidate, et elle disparaissait du tableau sans un mot.
    """
    return {m: nom for m in machines if (nom := profil_de(moteur, m))}


def provenance(args: argparse.Namespace, classifieur, frappes) -> dict:
    """Ce qu'il faut savoir pour REJOUER ce rapport (phase A4.2)."""
    try:
        racine = str(Path(__file__).resolve().parent.parent)
        commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True,
                                text=True, timeout=5, check=True, cwd=racine).stdout.strip()
        # UN ARBRE MODIFIÉ N'EST PAS CE COMMIT. Un rapport produit avec des
        # changements non commités qui annoncerait le commit nu se rejouerait
        # sur un autre code sans que rien ne le dise ; le « + » le dit.
        modifie = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no", "--",
                                  "analyse"], capture_output=True, text=True, timeout=5,
                                 check=True, cwd=racine).stdout.strip()
        if modifie:
            commit += "+"
    except Exception:  # noqa: BLE001 - hors dépôt, ou git absent : on le dit
        commit = ""
    return {
        "commit": commit,
        "options": {
            "separation": not args.sans_separation,
            "sampler": not args.sans_sampler,
            "arbitrage": not args.sans_arbitrage,
            "arbitrageBatterie": not args.sans_arbitrage_batterie,
            "reglagePiste": not args.sans_reglage_piste,
            "budgetPiste": args.budget_piste,
            "axesPiste": args.axes_piste,
            "finalistes": args.finalistes,
            "preselectionApprise": args.preselection_apprise,
        },
        # Les modèles CONSULTÉS, avec leur date d'entraînement -- ou « aucun »,
        # qui est une information et non une absence d'information.
        "modeles": {
            "classifieurMachine": (classifieur.date if classifieur is not None else "aucun"),
            "classifieurFrappes": (frappes.date if frappes is not None else "aucun"),
        },
        "profilMultisample": os.environ.get("VSM_PROFIL", "") or "(premier installé)",
    }


# ---------------------------------------------------------------------------
# Ligne de commande
# ---------------------------------------------------------------------------

def construire_parseur() -> argparse.ArgumentParser:
    parseur = argparse.ArgumentParser(
        description="Reconstruit un morceau avec les machines du DAW VSM.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parseur.add_argument("entree", help="fichier audio (wav, mp3, flac...)")
    parseur.add_argument("--sortie", default="reconstruction", help="dossier de sortie")
    parseur.add_argument("--batterie", action="store_true",
                         help="traiter l'entrée comme un stem de batterie : découpe en coups "
                              "et rejeu par la batterie modélisée, sans recherche de patch")
    parseur.add_argument("--voix-sampler", action="store_true",
                         help="reporter la voix par le SAMPLER (une note déclenchant le "
                              "fichier entier) au lieu d'une piste audio. C'est ce que la "
                              "chaîne faisait faute de piste audio : sur Sky and Sand, "
                              "8 min 52 et 47 Mo dans un emplacement de boîte à rythmes. "
                              "Conservé pour rejouer un projet ancien à l'identique.")
    parseur.add_argument("--batterie-echantillonnee", action="store_true",
                         help="rejouer la batterie par le SAMPLER, avec les coups découpés "
                              "dans l'enregistrement, au lieu de la batterie modélisée. "
                              "C'est l'ancien comportement ; il reste accessible parce qu'il "
                              "est plus fidèle au coup enregistré, et moins réglable.")
    parseur.add_argument("--sans-sampler", action="store_true",
                         help="interdire le sampler sur TOUT le morceau : le stem vocal "
                              "passe par la recherche de patch comme les autres, au lieu "
                              "d'être reporté tel quel, et la batterie modélisée n'écrit "
                              "plus d'échantillons. Le projet ne contient alors que des "
                              "machines de synthèse. À employer quand on veut un projet "
                              "entièrement rejouable sans le disque d'origine ; le prix "
                              "est une voix synthétisée, c'est-à-dire pas une voix.")
    parseur.add_argument("--classifieur", default=None,
                         help="chemin d'un modèle de classification de machine (phase A1). "
                              "Son avis est CONSIGNÉ dans le rapport ; par défaut il ne change "
                              "rien au verdict — mesuré, il place la gagnante réelle au rang "
                              "médian 16 sur 20 sur un piano")
    parseur.add_argument("--classifieur-batterie", default=None,
                         help="modèle de classification des FRAPPES (phase A2). Il décide, "
                              "attaque par attaque, quelles pièces frappent — plusieurs à la "
                              "fois s'il le faut. Mesuré au banc : charleston seule 16/16 au "
                              "lieu de 8/16, zéro kick inventé au lieu de 8")
    parseur.add_argument("--preselection-apprise", type=int, default=0,
                         help="ne chercher que les N premières machines du classifieur. "
                              "0 = désactivé, et c'est le défaut : la mesure ne le recommande "
                              "PAS (voir docs/ROADMAP-apprentissage.md, A1.3)")
    parseur.add_argument("--sans-apprentissage", action="store_true",
                         help="ignorer tout modèle appris — le témoin A/B exigé par le § 8.2 "
                              "du cahier des charges de l'apprentissage")
    parseur.add_argument("--sans-separation", action="store_true",
                         help="ne pas séparer en stems : traiter le fichier comme une seule piste")
    parseur.add_argument("--modele", default="htdemucs", help="modèle de séparation")
    parseur.add_argument("--machines", default="",
                         help="liste de machines candidates, séparées par des virgules "
                              "(défaut : toutes les mélodiques du moteur)")
    parseur.add_argument("--sans-arbitrage-batterie", action="store_true",
                         help="ne pas faire concourir les boîtes à rythmes du parc (TR-909, "
                              "TR-808) contre la batterie modélisée sur le stem de batterie. "
                              "C'est l'ancien comportement ; il reste accessible pour comparer")
    parseur.add_argument("--sans-arbitrage", action="store_true",
                         help="ne pas rejuger les candidates sur la PISTE ENTIÈRE. "
                              "Par défaut, la chaîne rend la piste complète avec le "
                              "patch trouvé de chaque machine ET avec le patch d'usine "
                              "de chaque machine, puis retient la meilleure : mesuré, "
                              "le critère « une note » ne classe pas dans le même ordre "
                              "que la piste (ARCHITECTURE.md § 34). Cette option rend "
                              "l'ancien comportement, pour comparer.")
    parseur.add_argument("--sans-reglage-piste", action="store_true",
                         help="ne pas RÉGLER le patch de la machine gagnante sur la piste "
                              "entière après l'arbitrage. Par défaut, une descente par "
                              "coordonnées balaye les axes déclarés par la machine et ne "
                              "garde une valeur que si elle rapproche le rendu complet du "
                              "stem : elle ne peut donc pas dégrader le patch d'où elle part.")
    parseur.add_argument("--budget-piste", type=int, default=40,
                         help="nombre d'évaluations du réglage sur la piste (défaut 40). "
                              "Une évaluation = un rendu de piste + une distance, mesuré "
                              "à ~5 s sur un morceau de quatre minutes. Comme le budget "
                              "de la recherche, il conditionne le résultat : deux réglages "
                              "obtenus à des budgets différents ne se comparent pas.")
    parseur.add_argument("--axes-piste", type=int, default=8,
                         help="nombre d'axes explorés par le réglage sur la piste "
                              "(défaut 8, dans l'ordre d'importance déclaré par la "
                              "machine). Certaines machines en déclarent bien plus -- "
                              "`vsm.drums` en a 21, un par pièce et par extinction -- "
                              "et n'en voir que huit laisse les autres à leur valeur "
                              "d'usine sans que personne l'ait jugé.")
    parseur.add_argument("--iterations", type=int, default=20,
                         help="budget de recherche par machine (défaut 20). "
                              "Mesuré : le doubler change souvent la machine retenue.")
    parseur.add_argument("--tempo", type=float, default=120.0, help="tempo du projet écrit")
    parseur.add_argument("--metrique", default="v2", choices=("v1", "v2", "v3", "v4"),
                         help="métrique de comparaison (défaut v2 ; v1 pour rejouer "
                              "d'anciennes mesures, v3 ajoute la hauteur des graves, "
                              "v4 ajoute la DYNAMIQUE — mesuré : v2 récompense une "
                              "batterie qui bourdonne. Deux métriques ne se comparent "
                              "jamais entre elles)")
    parseur.add_argument("--finalistes", type=int, default=None,
                         help="nombre de machines retenues après le dégrossissage "
                              "(défaut : la moitié). 0 DÉSACTIVE la présélection : "
                              "chaque candidate reçoit le budget complet. Plus lent, "
                              "mais c'est le seul réglage sous lequel les distances de "
                              "toutes les machines se comparent -- une candidate écartée "
                              "au dégrossissage n'a pas de score comparable aux autres.")
    parseur.add_argument("--stems", default=None,
                         help="dossier de stems DÉJÀ séparés (bass.wav, drums.wav, "
                              "other.wav, vocals.wav) : la séparation est sautée. "
                              "C'est ce qui rend une mesure rejouable -- refaire quatre "
                              "minutes de séparation pour comparer deux réglages de la "
                              "suite de la chaîne ne mesure rien de plus, et rend les "
                              "deux passes moins comparables si le modèle change.")
    parseur.add_argument("--garder-stems", default=None,
                         help="dossier où conserver les stems séparés, pour rejouer une "
                              "mesure sans repayer la séparation")
    parseur.add_argument("--moteur", default=None, help="chemin de vsm-render")
    return parseur


def valider_entree(args: argparse.Namespace) -> Path:
    """Vérifie ce qui peut l'être avant de charger quoi que ce soit."""
    if args.sans_sampler and args.batterie_echantillonnee:
        raise Abandon(1, "--sans-sampler et --batterie-echantillonnee se contredisent : "
                         "la batterie échantillonnée EST le sampler.")
    entree = Path(args.entree).expanduser()
    if not entree.exists():
        raise Abandon(1, f"fichier introuvable : {entree}")
    return entree


@contextlib.contextmanager
def dossier_de_travail(args: argparse.Namespace) -> Iterator[Path]:
    """Où vont les stems séparés et les rendus intermédiaires.

    Un dossier temporaire, effacé à la fin -- sauf si `--garder-stems` en
    désigne un : conserver les stems permet de rejouer une mesure sans repayer
    la séparation, qui coûte quatre minutes sur un morceau de quatre.
    """
    if args.garder_stems:
        dossier = Path(args.garder_stems).expanduser()
        dossier.mkdir(parents=True, exist_ok=True)
        yield dossier
        return
    with tempfile.TemporaryDirectory() as temporaire:
        yield Path(temporaire)


# ---------------------------------------------------------------------------
# Ce que les étapes partagent
# ---------------------------------------------------------------------------

@dataclass
class Contexte:
    """Ce que toutes les étapes lisent, et qu'aucune ne modifie."""
    args: argparse.Namespace
    moteur: VsmEngine
    sortie: Path
    travail: Path
    candidates: List[str]
    classifieur: Optional[object] = None
    frappes: Optional[object] = None

    def options_de_rendu(self, nom: str, audio: np.ndarray) -> Dict[str, object]:
        """Les réglages communs à tous les rendus hors ligne d'une piste :
        arbitrage et réglage jugent avec la même mesure et la même règle de
        niveau, sans quoi leurs chiffres ne se compareraient pas."""
        return dict(sample_rate=SAMPLE_RATE, metric=self.args.metrique, tempo=self.args.tempo,
                    binary=self.args.moteur, name=nom, stem_rms=niveau_efficace(audio),
                    base_volume=DEFAULT_TRACK_VOLUME, max_volume=VOLUME_MAX)


@dataclass
class Chantier:
    """Ce que la boucle sur les stems accumule, et que l'export lit."""
    # Les stems mélodiques, passés par la recherche de patch.
    reconstruits: List[StemReconstruction] = field(default_factory=list)
    # Les pistes écrites directement, sans recherche de patch : la voix
    # reportée au sampler, la batterie.
    pistes_directes: List[ExportTrack] = field(default_factory=list)
    audio_par_stem: Dict[str, np.ndarray] = field(default_factory=dict)
    # Patch d'AVANT le réglage, par nom de piste : c'est l'alternative que le
    # verdict du mélange remettra en concurrence. Les stems mélodiques la
    # portent dans leur `StemReconstruction` ; la batterie, qui n'en a pas,
    # passe par ce dictionnaire.
    patchs_avant_reglage: Dict[str, Dict[str, float]] = field(default_factory=dict)
    # La machine SECONDE de l'arbitrage, quand elle est à portée. C'est la
    # seule façon qu'une égalité mal tranchée cesse d'être définitive : le
    # verdict du mélange ne savait défaire qu'un réglage, jamais une machine.
    machines_secondes: Dict[str, List[MixAlternative]] = field(default_factory=dict)
    rapport_batterie: Optional[Dict[str, object]] = None


# ---------------------------------------------------------------------------
# [2/5] Les stems
# ---------------------------------------------------------------------------

def obtenir_stems(args: argparse.Namespace, entree: Path, travail: Path) -> Dict[str, Path]:
    """Nom de stem -> fichier : repris d'un dossier, séparés, ou le mélange seul."""
    if args.stems:
        dossier_stems = Path(args.stems).expanduser()
        trouves = {chemin.stem: chemin for chemin in sorted(dossier_stems.rglob("*.wav"))}
        if not trouves:
            raise Abandon(1, f"aucun stem dans {dossier_stems}")
        print(f"[2/5] Stems repris de {dossier_stems} : {', '.join(sorted(trouves))}")
        return trouves
    if args.sans_separation:
        print("[2/5] Séparation désactivée : une seule piste")
        return {"melange": entree}
    print(f"[2/5] Séparation en stems ({args.modele})")
    try:
        return separer(entree, travail / "stems", args.modele)
    except Exception as erreur:
        # La séparation est lourde et peut manquer. On le DIT et on continue
        # sur le mélange, plutôt que d'abandonner : une reconstruction
        # imparfaite reste plus utile qu'aucune.
        print(f"      échec ({erreur}) — repli sur le mélange entier")
        return {"melange": entree}


# ---------------------------------------------------------------------------
# Les modèles appris : chargés, VÉRIFIÉS, ou refusés -- jamais appliqués au
# doute. Un modèle entraîné sur le son d'hier ne se trompe pas bruyamment :
# il classe plausiblement et faux.
# ---------------------------------------------------------------------------

def charger_classifieur(args: argparse.Namespace, moteur: VsmEngine):
    """Le classifieur de MACHINE (phase A1), ou None."""
    if not args.classifieur or args.sans_apprentissage:
        return None
    from analyzer.vsm_classifier import Classifieur

    try:
        modele = Classifieur.relit(Path(args.classifieur))
        verdict = modele.verifie_fraicheur(moteur, SAMPLE_RATE)
        if not verdict.frais:
            print(f"      classifieur REFUSÉ — {verdict.resume()}")
            print("      la chaîne continue SANS lui, exactement comme avant")
            return None
        print(f"      classifieur du {modele.date}, "
              f"{len(modele.noms)} machines, empreintes vérifiées")
        return modele
    except Exception as erreur:  # noqa: BLE001
        print(f"      classifieur illisible ({type(erreur).__name__}) — ignoré")
        return None


def charger_classifieur_frappes(args: argparse.Namespace, moteur: VsmEngine):
    """Le classifieur de FRAPPES (phase A2), ou None."""
    if not args.classifieur_batterie or args.sans_apprentissage:
        return None
    from analyzer.vsm_drum_corpus import ClassifieurFrappes

    try:
        modele = ClassifieurFrappes.relit(args.classifieur_batterie)
        # VÉRIFIÉ comme l'autre (A4.1) : un kick de 909 qui change rend
        # périmé un modèle qui nomme des kicks de 909.
        verdict = modele.verifie_fraicheur(moteur, SAMPLE_RATE)
        if not verdict.frais:
            print(f"      classifieur de frappes du {modele.date} REFUSÉ — {verdict.resume()}")
            print("      la batterie est nommée SANS lui, exactement comme avant")
            return None
        print(f"      classifieur de frappes du {modele.date}, "
              f"pièces : {', '.join(modele.pieces)}, empreintes vérifiées")
        return modele
    except Exception as erreur:  # noqa: BLE001
        print(f"      classifieur de frappes illisible ({type(erreur).__name__}) — ignoré")
        return None


# ---------------------------------------------------------------------------
# [3/5] Un stem à la fois. RÉPARTITION : le sampler n'est QUE pour la voix ;
# la batterie a sa propre machine ; tout le reste passe par la recherche de
# patch.
# ---------------------------------------------------------------------------

def reporter_voix(nom: str, chemin: Path, sortie: Path,
                   par_sampler: bool = False) -> Optional[Tuple[ExportTrack, np.ndarray]]:
    """Le stem vocal, REPORTÉ tel quel — sur une piste audio.

    La voix ne se synthétise pas — le § 6 de la feuille de route le dit depuis
    le début, et chercher un patch dessus produisait un chiffre (obx, d=0,196
    sur Children) qui ne voulait rien dire : ce n'est pas parce qu'un OB-X
    approche le spectre d'une voix qu'il chante. C'est dit pour ce que c'est.
    """
    audio = charger_audio(chemin)
    if par_sampler:
        piste = vocal_sampler_track(audio, SAMPLE_RATE, sortie / "samples", name="Voix")
        moyen = "sampler"
    else:
        piste = vocal_audio_track(audio, SAMPLE_RATE, sortie / "samples", name="Voix")
        moyen = "piste audio"
    if piste is None:
        print(f"      {nom:8s} : stem vocal vide, piste ignorée")
        return None
    duree = audio.size / SAMPLE_RATE
    print(f"      {nom:8s} : {moyen}, report intégral ({duree:.0f} s) "
          f"— la voix n'est pas reconstruite, elle est reportée")
    return piste, audio


def resume_des_axes(affine) -> str:
    """Les derniers axes que le réglage a bougés, pour la console."""
    return ", ".join(
        f"{axe.split('.')[-1]}={valeur:.3g}" for axe, valeur, _ in affine.improvements[-4:]
    ) or "aucun axe retenu"


@dataclass
class ResultatBatterie:
    piste: ExportTrack
    audio: np.ndarray
    rapport: Dict[str, object]
    # Le patch d'usine de la boîte retenue, quand elle a été réglée : le
    # verdict du mélange le remet en concurrence.
    patch_avant_reglage: Optional[Dict[str, float]] = None
    # Les autres boîtes RÉGLÉES, encore en jeu au verdict du mélange.
    secondes: List[MixAlternative] = field(default_factory=list)


def arbitrer_batterie(ctx: Contexte, nom: str, kit, piste: ExportTrack, audio: np.ndarray,
                      rapport: Dict[str, object]
                      ) -> Tuple[ExportTrack, Dict[str, ExportTrack], List[str], list]:
    """Les boîtes à rythmes du parc concourent contre la batterie modélisée.

    Jusqu'ici cette piste était la SEULE à échapper à la règle « toutes les
    machines en lice, l'arbitrage tranche », au motif que `vsm.drums` n'avait
    « pas de concurrente crédible ». Sur un morceau de techno de 1993, la
    concurrente crédible est la TR-909 -- et c'est une oreille qui l'a dit,
    parce qu'aucun chiffre ne peut désigner une 909 tant qu'elle n'est pas
    dans la course. Les instants et les vélocités sont les mêmes pour toutes ;
    seule la machine change, et la piste entière juge.

    Rend la piste gagnante, les candidates EN LICE par machine, celles à
    RÉGLER, et les verdicts classés.
    """
    depart = time.perf_counter()
    # Les candidates EN LICE, par machine, chacune avec SES notes : la
    # correspondance famille -> note diffère d'une boîte à l'autre (la 909 a
    # un clap en 39, `vsm.drums` une percussion en 49), et une candidate jouée
    # avec les notes d'une autre serait un kit amputé.
    en_lice: Dict[str, ExportTrack] = {piste.machine: piste}
    for m in BOITES_A_RYTHMES:
        en_lice[m] = drum_machine_track(kit, m, name=PISTE_BATTERIE)
    # L'arbitrage générique partage les notes entre candidates ; on le fait
    # donc ici machine par machine, avec la même mesure et la même règle de
    # niveau.
    verdicts = []
    for m, candidate in en_lice.items():
        verdicts.extend(arbitrate_on_track(
            notes=list(candidate.notes), stem_audio=audio,
            candidates=[TrackCandidate(m, dict(candidate.parameters), ORIGINE_USINE)],
            workdir=ctx.travail / "arbitrage" / "batterie" / m,
            **ctx.options_de_rendu(PISTE_BATTERIE, audio)))
    verdicts.sort(key=lambda v: v.distance)
    rapport["trackArbitration"] = [
        {"machine": v.machine, "origin": v.origin, "distance": v.distance} for v in verdicts]
    podium = ", ".join(f"{v.machine.split('.')[-1]}={v.distance:.3f}" for v in verdicts[:3])
    if verdicts and verdicts[0].machine != piste.machine:
        gagnante = verdicts[0].machine
        piste = en_lice[gagnante]
        print(f"      {nom:8s} : arbitrage batterie CHANGE {gagnante} "
              f"D={verdicts[0].distance:.3f} "
              f"[{time.perf_counter()-depart:.0f} s] — {podium}")
    else:
        print(f"      {nom:8s} : arbitrage batterie garde vsm.drums "
              f"[{time.perf_counter()-depart:.0f} s] — {podium}")
    # TOUTE BOÎTE À PORTÉE EST RÉGLÉE AUSSI -- la même règle que les stems
    # mélodiques (§ 5 quinquies), parce que comparer une machine réglée à une
    # machine d'usine n'est pas une comparaison. Mesuré sur B4 Wuz Then : la
    # 808 d'usine battait la 909 d'usine (0,251 contre 0,356), et une oreille
    # disait 909 ; seul un réglage des deux tranche. Et sur Children, la
    # batterie MODÉLISÉE : écartée sur la piste (0,426 contre 0,301), elle
    # descendait réglée à 0,211 -- à portée de la 909 réglée (0,164) -- et le
    # mélange la préférait (0,28 contre 0,34) sans que personne ne puisse
    # comparer, puisqu'elle n'était plus nulle part.
    a_regler = [piste.machine]
    if verdicts:
        seuil_serre = verdicts[0].distance * (1.0 + CLOSE_MARGIN_BATTERIE)
        for v in verdicts[1:]:
            if v.machine in a_regler or v.distance > seuil_serre:
                continue
            a_regler.append(v.machine)
            print(f"      {nom:8s} : arbitrage batterie SERRÉ — {v.machine} à "
                  f"{100*(v.distance/verdicts[0].distance-1):.0f} % "
                  f"({v.distance:.3f}), réglée elle aussi")
    return piste, en_lice, a_regler, verdicts


def regler_batterie(ctx: Contexte, nom: str, piste: ExportTrack, en_lice: Dict[str, ExportTrack],
                    a_regler: List[str], audio: np.ndarray, rapport: Dict[str, object]
                    ) -> Dict[str, Tuple[ExportTrack, float, Dict[str, float]]]:
    """Règle sur la piste entière chaque boîte à régler.

    LA BATTERIE SE RÈGLE AUSSI, et c'était le dernier endroit de la chaîne où
    un patch restait celui d'usine sans que personne l'ait jugé. Elle pèse
    pourtant le plus lourd dans le mélange (niveau efficace 0,156 sur
    Children, contre 0,087 pour la basse) : la laisser hors du réglage
    revenait à soigner les pistes qu'on entend le moins.

    Rend, par machine réglée : la piste (son patch est remplacé), sa distance
    de piste, et son patch d'usine.
    """
    reglees: Dict[str, Tuple[ExportTrack, float, Dict[str, float]]] = {}
    for m in a_regler:
        candidate = en_lice[m]
        depart = time.perf_counter()
        patch_usine = dict(candidate.parameters)
        affine = refine_patch_on_track(
            machine=m, parameters=candidate.parameters, notes=candidate.notes,
            stem_audio=audio, engine=ctx.moteur,
            workdir=ctx.travail / "reglage" / (nom if m == piste.machine else f"{nom}-{m}"),
            budget=ctx.args.budget_piste, axes=ctx.args.axes_piste,
            **ctx.options_de_rendu(candidate.name, audio))
        if affine is None:
            print(f"      {nom:8s} : réglage piste de {m} non tenté "
                  f"(la machine ne déclare aucun axe)")
            continue
        candidate.parameters = dict(affine.parameters)
        reglees[m] = (candidate, float(affine.distance), patch_usine)
        rapport["refinements"].append({
            "machine": m, "before": affine.start_distance,
            "after": affine.distance, "evaluations": affine.evaluations})
        qui = "" if m == piste.machine else f"{m} "
        print(f"      {nom:8s} : réglage piste {qui}"
              f"{affine.start_distance:.3f} -> {affine.distance:.3f} "
              f"({affine.evaluations} évaluations, "
              f"{time.perf_counter()-depart:.0f} s) — {resume_des_axes(affine)}")
    return reglees


def _nom_courant(machine: str) -> str:
    """Le nom sous lequel un avertissement peut désigner une machine.

    Ils ne parlent pas tous d'`vsm.tr808` : certains disent « la TR-808 », qui
    est ce qu'un musicien lit. Le filtre doit reconnaître les deux, sans quoi
    il écarterait précisément les avertissements écrits pour être lus.
    """
    return {"vsm.tr808": "TR-808", "vsm.tr909": "TR-909",
            "vsm.drums": "batterie modélisée"}.get(machine, machine)


def reconstruire_batterie(ctx: Contexte, nom: str, chemin: Path) -> Optional[ResultatBatterie]:
    """Le stem de batterie : coups détectés et classés, puis rejoués.

    Les frappes pilotent `vsm.drums`, qui MODÉLISE peaux et métal, au lieu de
    charger des coups découpés. On y gagne un kit réglable, on y perd la
    fidélité littérale au coup enregistré ; le compromis est mesuré, pas
    supposé. `--batterie-echantillonnee` rend l'ancien comportement.
    """
    args = ctx.args
    audio = charger_audio(chemin)
    kit = build_drum_kit(audio, SAMPLE_RATE, ctx.sortie / "samples",
                         write_samples=not args.sans_sampler, hit_classifier=ctx.frappes)
    if kit is None:
        print(f"      {nom:8s} : aucun coup détecté, piste ignorée")
        return None
    detail = " ".join(f"{s.family}={s.hit_count}" for s in kit.slots)
    if args.batterie_echantillonnee:
        piste = drum_kit_track(kit, name=PISTE_BATTERIE)
        moyen = "sampler"
    else:
        piste = modelled_drum_track(kit, name=PISTE_BATTERIE)
        moyen = "vsm.drums"
    print(f"      {nom:8s} : {moyen}, {len(kit.slots)} pièce(s), "
          f"{kit.total_hits} frappe(s) — {detail}")
    # OÙ SE TROUVE L'ÉNERGIE DE CHAQUE FAMILLE. Un nom de famille est une
    # étiquette -- elle vient d'un modèle appris, ou d'une liste de réserve
    # quand le modèle n'a rien dit -- et rien ne la confrontait à ce qu'on
    # entend. Sur ce morceau, une famille de 811 frappes nommée « tom » avait
    # 69 % de son énergie sous 200 Hz : une grosse caisse, que la TR-808 jouait
    # sur le clap faute d'avoir des toms. Le profil rend la contradiction
    # visible en une ligne, au lieu de demander une enquête.
    from analyzer.vsm_drumkit import describe_band_shares
    for emplacement in kit.slots:
        if emplacement.band_shares:
            print(f"                 {emplacement.family:11s} "
                  f"{describe_band_shares(emplacement.band_shares)}")
    for avertissement in kit.warnings:
        print(f"                 ! {avertissement}")
    # CE QUI EST DIT ICI NE CONCERNE QUE LA DÉTECTION. Les avertissements des
    # MACHINES -- une famille sans voix, des toms rabattus sur le clap --
    # naissent plus bas, quand `drum_machine_track` pose les coups sur les
    # notes de chaque boîte, c'est-à-dire APRÈS ce point. Ils n'étaient donc ni
    # imprimés ni enregistrés : le rapport de *Sky and Sand* nomme `vsm.tr808`
    # comme machine retenue et ne porte que les avertissements de `vsm.drums`,
    # qui a perdu. On retient l'index pour dire, à la fin, ce que la machine
    # RETENUE a dû concéder.
    deja_dits = len(kit.warnings)
    # CE QUE LE RAPPORT DIRA DE LA BATTERIE. Longtemps elle n'y figurait pas :
    # `rapport.json` ne listait que les stems mélodiques, et la piste la plus
    # lourde du mélange -- arbitrée, réglée, départagée -- n'y laissait aucune
    # trace. Un rapport qui tait la décision la plus coûteuse n'est pas un
    # rapport.
    rapport: Dict[str, object] = {
        "machine": piste.machine,
        "means": moyen,
        "hits": int(kit.total_hits),
        "pieces": [{"family": s.family, "hits": int(s.hit_count),
                     "bandShares": [round(part, 4) for part in s.band_shares]}
                    for s in kit.slots],
        "warnings": list(kit.warnings),
        "trackArbitration": [],
        "refinements": [],
    }
    resultat = ResultatBatterie(piste=piste, audio=audio, rapport=rapport)

    en_lice: Dict[str, ExportTrack] = {piste.machine: piste}
    a_regler: List[str] = [piste.machine]
    verdicts: list = []
    if not args.sans_arbitrage and moyen == "vsm.drums" and not args.sans_arbitrage_batterie:
        piste, en_lice, a_regler, verdicts = arbitrer_batterie(ctx, nom, kit, piste, audio, rapport)
        moyen = piste.machine

    reglees: Dict[str, Tuple[ExportTrack, float, Dict[str, float]]] = {}
    if not args.sans_reglage_piste and piste.machine != "vsm.sampler":
        reglees = regler_batterie(ctx, nom, piste, en_lice, a_regler, audio, rapport)

    # LA MEILLEURE DES RÉGLÉES PREND LA PISTE, et les autres RÉGLÉES restent
    # en jeu au verdict du mélange : la piste a tranché entre elles, mais la
    # piste ne juge pas ce qu'on écoute.
    if reglees:
        meilleure = min(reglees, key=lambda m: reglees[m][1])
        if meilleure != piste.machine:
            contre = (f" ({reglees[meilleure][1]:.3f} contre {reglees[piste.machine][1]:.3f})"
                      if piste.machine in reglees else "")
            print(f"      {nom:8s} : {meilleure} réglée PASSE DEVANT{contre}")
            piste = reglees[meilleure][0]
            moyen = meilleure
        resultat.patch_avant_reglage = dict(reglees[meilleure][2])
        rapport["trackDistance"] = reglees[meilleure][1]
        for m, (candidate, d, _) in reglees.items():
            if m == meilleure:
                continue
            resultat.secondes.append(MixAlternative(
                parameters=dict(candidate.parameters),
                label=f"{'batterie modélisée' if m == 'vsm.drums' else 'seconde boîte'} "
                      f"({m}) réglée",
                machine=m, notes=list(candidate.notes), track_distance=d))
    elif verdicts:
        rapport["trackDistance"] = verdicts[0].distance
    rapport["machine"] = piste.machine
    rapport["means"] = moyen

    # LES CONCESSIONS DE LA MACHINE RETENUE, dites et enregistrées. Elles sont
    # filtrées sur son nom : le kit a été posé sur trois boîtes pendant
    # l'arbitrage, et lui rapporter les compromis des perdantes serait aussi
    # trompeur que de n'en rapporter aucun.
    nouveaux = kit.warnings[deja_dits:]
    siennes = [a for a in nouveaux if piste.machine in a or _nom_courant(piste.machine) in a]
    for avertissement in siennes:
        print(f"                 ! {avertissement}")
    rapport["warnings"] = list(kit.warnings[:deja_dits]) + siennes

    resultat.piste = piste
    return resultat


@dataclass
class ResultatMelodique:
    stem: StemReconstruction
    audio: np.ndarray
    # Les machines SUIVANTES de l'arbitrage, que le mélange rejugera.
    secondes: List[MixAlternative] = field(default_factory=list)


def arbitrer_sur_piste(ctx: Contexte, nom: str, stem: StemReconstruction, audio: np.ndarray
                       ) -> List[MixAlternative]:
    """Rejuge toutes les candidates sur la PISTE ENTIÈRE.

    La recherche a choisi une machine d'après UNE note ; ceci la rejuge sur
    toutes. Les deux critères ne classent pas dans le même ordre, et c'est le
    second qu'on écoute. Modifie `stem` en place ; rend les machines SUIVANTES,
    que le verdict du mélange rejugera -- le classement contre le stem ne
    prédit pas le classement dans le mélange (§ 5 decies).
    """
    depart = time.perf_counter()
    verdicts = arbitrate_on_track(
        notes=notes_export(stem.notes),
        stem_audio=audio,
        candidates=build_candidates(list(stem.patches.items()), ctx.candidates,
                                    profils_de(ctx.moteur, ctx.candidates)),
        workdir=ctx.travail / "arbitrage" / nom,
        **ctx.options_de_rendu(nom, audio),
    )
    if not verdicts:
        # « aucune retenue » et non « aucun rendu » : le cas le plus fréquent
        # n'est pas un moteur muet, c'est le filtre de niveau de
        # `arbitrate_on_track` qui a écarté TOUTES les candidates -- un stem
        # si fort qu'aucune machine ne peut l'atteindre. Les deux causes
        # appellent des gestes opposés ; les confondre coûtait l'enquête.
        print(f"      {nom:8s} : arbitrage sans verdict (aucune candidate "
              f"rendue ni retenue au niveau) — la machine de la "
              f"recherche est conservée")
        return []
    gagnant = verdicts[0]
    stem.track_distance = gagnant.distance
    stem.track_considered = [(v.machine, v.origin, v.distance) for v in verdicts]
    classement = ", ".join(
        f"{v.machine.split('.')[-1]}={v.distance:.3f}"
        f"{'*' if v.origin == 'patch d\'usine' else ''}"
        for v in verdicts[:3]
    )
    avant = next((v.distance for v in verdicts
                  if v.machine == stem.machine and v.parameters == stem.parameters), None)
    change = gagnant.machine != stem.machine or gagnant.parameters != stem.parameters
    stem.machine = gagnant.machine
    stem.parameters = dict(gagnant.parameters)
    marque = "CHANGE" if change else "confirme"
    print(f"      {nom:8s} : arbitrage piste {marque} "
          f"{gagnant.machine} ({gagnant.origin}) D={gagnant.distance:.3f}"
          + (f" (la recherche donnait {avant:.3f})" if avant is not None else "")
          + f" [{time.perf_counter()-depart:.0f} s] — {classement}")
    print("                 (* = patch d'usine)")

    # LES SUIVANTES REPARTENT TOUTES AU MÉLANGE, ET PLUS SEULEMENT LES SERRÉES.
    # Le seuil de 2 % supposait qu'une machine loin derrière AU STEM est loin
    # derrière tout court. Mesuré sur *Sky and Sand* (§ 5 decies), c'est faux :
    # la machine que le mélange retient pour la basse est TROISIÈME au stem, à
    # 17,6 %, et celle de `other` deuxième à 16,4 %. Les deux classements sont
    # à peu près inverses. Une proposition de plus coûte un rendu de projet --
    # une quinzaine de secondes sur les ~5 900 s d'une reconstruction.
    suivantes = runners_up(verdicts, count=MACHINES_AU_MELANGE)
    if not suivantes:
        return []
    detail = ", ".join(
        f"{v.machine.split('.')[-1]} à {(v.distance - gagnant.distance) / max(1e-9, gagnant.distance) * 100:.1f} %"
        for v in suivantes)
    print(f"      {nom:8s} : {len(suivantes)} machine(s) suivante(s) remises en jeu "
          f"au verdict du mélange — {detail}")
    return [MixAlternative(parameters=dict(v.parameters),
                           label=f"machine suivante ({v.machine})",
                           machine=v.machine, track_distance=v.distance)
            for v in suivantes]


def regler_sur_piste(ctx: Contexte, nom: str, stem: StemReconstruction, audio: np.ndarray) -> None:
    """Règle le patch retenu sur la piste entière, par une descente qui ne
    peut qu'améliorer son point de départ. Modifie `stem` en place.

    CETTE ÉTAPE NE DÉPEND PAS DE L'ARBITRAGE. Elle a été écrite sous le `else`
    de l'arbitrage, et c'était un défaut : elle disparaissait avec lui, donc
    `--sans-arbitrage` désactivait DEUX étapes, et un stem dont l'arbitrage ne
    rendait aucun verdict n'était pas réglé non plus. Le README promet
    l'inverse — « chaque étape se désactive : c'est ainsi qu'on attribue un
    écart à une étape et non à un ensemble » — et c'est la promesse qui a
    raison : sans elle, aucune mesure ne peut dire laquelle des deux étapes a
    produit un gain.
    """
    depart = time.perf_counter()
    stem.arbitration_parameters = dict(stem.parameters)
    stem.arbitration_distance = stem.track_distance
    affine = refine_patch_on_track(
        machine=stem.machine,
        parameters=stem.parameters,
        notes=notes_export(stem.notes),
        stem_audio=audio,
        engine=ctx.moteur,
        workdir=ctx.travail / "reglage" / nom,
        budget=ctx.args.budget_piste,
        axes=ctx.args.axes_piste,
        profile=profil_de(ctx.moteur, stem.machine),
        **ctx.options_de_rendu(nom, audio),
    )
    if affine is None:
        print(f"      {nom:8s} : réglage piste non tenté (la machine ne déclare aucun axe)")
        return
    gain = affine.start_distance - affine.distance
    stem.parameters = dict(affine.parameters)
    stem.track_distance = affine.distance
    print(f"      {nom:8s} : réglage piste "
          f"{affine.start_distance:.3f} -> {affine.distance:.3f} "
          f"({'-' if gain > 0 else ''}{abs(gain):.3f}, "
          f"{affine.evaluations} évaluations, "
          f"{time.perf_counter()-depart:.0f} s) — {resume_des_axes(affine)}")


def reconstruire_stem_melodique(ctx: Contexte, nom: str, chemin: Path) -> Optional[ResultatMelodique]:
    """Transcription, recherche de patch, arbitrage et réglage d'un stem."""
    args = ctx.args
    notes = extraire_notes(chemin)
    if not notes:
        print(f"      {nom:8s} : aucune note détectée, piste ignorée")
        return None
    audio = charger_audio(chemin)
    depart = time.perf_counter()
    stem = reconstruct_stem(
        nom, audio, notes, ctx.moteur,
        sample_rate=SAMPLE_RATE,
        machines=ctx.candidates,
        max_iterations=args.iterations,
        metric=args.metrique,
        shortlist=args.finalistes,
        classifieur=ctx.classifieur,
        preselection_apprise=args.preselection_apprise,
    )
    if stem is None:
        print(f"      {nom:8s} : aucune note exploitable")
        return None
    podium = ", ".join(
        f"{m.split('.')[-1]}={d:.2f}" for m, d in sorted(stem.considered, key=lambda x: x[1])[:3]
    )
    print(f"      {nom:8s} : {stem.machine:14s} d={stem.distance:.3f} "
          f"({len(notes)} notes, {time.perf_counter()-depart:.0f} s) — {podium}")

    resultat = ResultatMelodique(stem=stem, audio=audio)
    if not args.sans_arbitrage:
        resultat.secondes = arbitrer_sur_piste(ctx, nom, stem, audio)
    if not args.sans_reglage_piste:
        regler_sur_piste(ctx, nom, stem, audio)
    return resultat


def reconstruire_les_stems(ctx: Contexte, pistes: Dict[str, Path]) -> Chantier:
    """[3/5] Chaque stem vers sa voie : voix, batterie, ou recherche de patch."""
    args = ctx.args
    print(f"[3/5] {len(ctx.candidates)} machine(s) candidate(s)")
    if args.sans_sampler:
        print("      sampler INTERDIT : la voix passe par la recherche de patch, "
              "la batterie modélisée n'écrit pas d'échantillons")
    chantier = Chantier()
    for nom, chemin in sorted(pistes.items()):
        if nom == "vocals" and not args.sans_sampler:
            voix = reporter_voix(nom, chemin, ctx.sortie, par_sampler=ctx.args.voix_sampler)
            if voix is not None:
                piste, audio = voix
                chantier.pistes_directes.append(piste)
                chantier.audio_par_stem[piste.name] = audio
            continue
        if nom == "drums" or args.batterie:
            batterie = reconstruire_batterie(ctx, nom, chemin)
            if batterie is not None:
                chantier.pistes_directes.append(batterie.piste)
                chantier.audio_par_stem[PISTE_BATTERIE] = batterie.audio
                chantier.rapport_batterie = batterie.rapport
                if batterie.patch_avant_reglage is not None:
                    chantier.patchs_avant_reglage[batterie.piste.name] = batterie.patch_avant_reglage
                if batterie.secondes:
                    chantier.machines_secondes.setdefault(PISTE_BATTERIE, []).extend(batterie.secondes)
            continue
        melodique = reconstruire_stem_melodique(ctx, nom, chemin)
        if melodique is None:
            continue
        chantier.reconstruits.append(melodique.stem)
        chantier.audio_par_stem[melodique.stem.name] = melodique.audio
        if melodique.secondes:
            chantier.machines_secondes.setdefault(nom, []).extend(melodique.secondes)
    if not chantier.reconstruits and not chantier.pistes_directes:
        raise Abandon(3, "aucune piste reconstruite")
    return chantier


# ---------------------------------------------------------------------------
# [4/5] Export : pistes, automation, niveaux, verdict du mélange, presets
# ---------------------------------------------------------------------------

def assembler_pistes(ctx: Contexte, chantier: Chantier) -> List[ExportTrack]:
    """Les pistes du projet, avec l'automation de coupure et les niveaux calés."""
    pistes_export = []
    for stem in chantier.reconstruits:
        piste = ExportTrack(
            name=stem.name,
            machine=stem.machine,
            parameters=stem.parameters,
            notes=notes_export(stem.notes),
            profile=stem.profile or profil_de(ctx.moteur, stem.machine),
        )
        # AUTOMATION DE COUPURE : la trajectoire de brillance du stem, gardée
        # seulement si elle RAPPROCHE le rendu complet du stem -- mesuré par
        # la même distance que tout le reste, jamais supposé.
        audio_stem = chantier.audio_par_stem.get(stem.name)
        if audio_stem is not None:
            courbe, d_sans, d_avec, motif = try_cutoff_automation(
                audio_stem, SAMPLE_RATE, piste, ctx.moteur)
            if courbe is not None:
                piste.automation["filter.1.cutoff"] = courbe
                print(f"      {stem.name:8s} : automation de coupure GARDÉE "
                      f"({len(courbe)} points, distance {d_sans:.3f} -> {d_avec:.3f})")
            elif d_sans is not None:
                print(f"      {stem.name:8s} : automation de coupure rejetée "
                      f"({d_sans:.3f} -> {d_avec:.3f}, {motif})")
            else:
                print(f"      {stem.name:8s} : automation de coupure non tentée ({motif})")
        pistes_export.append(piste)
    pistes_export += chantier.pistes_directes

    # VOLUMES calés sur l'équilibre du morceau : chaque piste est rendue seule
    # et ramenée au niveau efficace de SON stem. C'était le premier écart
    # audible une fois la batterie devenue dense : chaque stem se rapprochait
    # de son original, et le mélange s'en éloignait.
    for ligne in match_track_levels(pistes_export, chantier.audio_par_stem, ctx.sortie,
                                    SAMPLE_RATE):
        print(f"      {ligne}")
    return pistes_export


def verdict_du_melange(ctx: Contexte, chantier: Chantier, pistes_export: List[ExportTrack],
                       melange: np.ndarray) -> Tuple[Dict[str, float], List[Dict[str, object]]]:
    """LE MÉLANGE A LE DERNIER MOT.

    Un réglage qui rapproche une piste de son stem peut éloigner le morceau :
    les stems ne se rendorment pas exactement dans l'original. On ne garde
    donc que ce qui rapproche ce qu'on écoute, et on DIT ce qui a été écarté.
    Modifie les pistes en place ; rend la distance de piste retenue par piste,
    et le verdict tel que le rapport le publie.
    """
    alternatives: Dict[str, List[MixAlternative]] = {}
    for stem in chantier.reconstruits:
        if stem.arbitration_parameters is not None:
            alternatives.setdefault(stem.name, []).append(
                MixAlternative(parameters=dict(stem.arbitration_parameters),
                               label="arbitrage",
                               track_distance=stem.arbitration_distance))
    for nom_piste, patch in chantier.patchs_avant_reglage.items():
        alternatives.setdefault(nom_piste, []).append(
            MixAlternative(parameters=dict(patch), label="avant réglage"))
    for nom_piste, secondes in chantier.machines_secondes.items():
        alternatives.setdefault(nom_piste, []).extend(secondes)
    if not alternatives:
        return {}, []

    decisions = keep_what_helps_the_mix(
        pistes_export, alternatives, melange, chantier.audio_par_stem, ctx.sortie,
        workdir=ctx.travail / "verdict",
        sample_rate=SAMPLE_RATE, metric=ctx.args.metrique,
        tempo=ctx.args.tempo, binary=ctx.args.moteur,
        profiles=profils_de(ctx.moteur, melodic_machines(ctx.moteur)))
    distances_retenues = {d.track: d.kept_track_distance for d in decisions
                          if d.kept_track_distance is not None}
    verdict: List[Dict[str, object]] = []
    for decision in decisions:
        ecartees = ", ".join(f"{lib} {d:.4f}" for lib, d in decision.rejected)
        # LE TÉMOIN DE COUPURE EST DIT AVEC LE VERDICT, et pas seulement inscrit
        # au rapport : sans lui, « la meilleure des variantes » se lit comme
        # « une bonne piste », ce qui n'est pas la même chose (§ 5 decies).
        coupee = ("" if decision.muted_distance is None
                  else f" [sans la piste : {decision.muted_distance:.4f}]")
        print(f"      {decision.track:8s} : verdict du mélange -> "
              f"{decision.kept} ({decision.distance_kept:.4f}){coupee}"
              + (f" — écartées : {ecartees}" if ecartees else ""))
        verdict.append({
            "track": decision.track, "kept": decision.kept,
            "mixDistance": decision.distance_kept,
            "mixDistanceMuted": decision.muted_distance,
            "rejected": [{"label": lib, "mixDistance": d} for lib, d in decision.rejected]})
    return distances_retenues, verdict


def aligner_rapport_sur_projet(chantier: Chantier, pistes_export: List[ExportTrack],
                               distances_retenues: Dict[str, float]) -> None:
    """LE RAPPORT DOIT DÉCRIRE LE PROJET QU'ON ÉCRIT, et il ne le faisait plus.

    `keep_what_helps_the_mix` REMPLACE le dictionnaire de paramètres de la
    piste (`track.parameters = ...`) au lieu de le modifier ; le
    `StemReconstruction`, qui partageait l'objet au départ, gardait donc le
    patch d'AVANT le verdict. Quand le mélange revenait au patch de
    l'arbitrage, `rapport.json` publiait le patch affiné et sa
    `trackDistance` : des chiffres pour un réglage absent du projet,
    c'est-à-dire la pire sorte -- ceux qu'on croit vérifiés.

    ICI, ET NON APRÈS LA RÉSOLUTION DES DÉFAUTS qui suit : le rapport dit ce
    que la CHAÎNE a décidé, pas les vingt valeurs d'usine que l'écriture du
    preset y ajoutera ensuite pour le figer. Noyer trois réglages trouvés dans
    vingt réglages hérités rendrait le rapport illisible sans rien lui
    apprendre.
    """
    par_nom = {piste.name: piste for piste in pistes_export}
    for stem in chantier.reconstruits:
        piste_finale = par_nom.get(stem.name)
        if piste_finale is None:
            continue
        stem.machine = piste_finale.machine
        stem.parameters = dict(piste_finale.parameters)
        # ET LA DISTANCE DE PISTE AVEC, sans quoi le rapport publierait le
        # chiffre du patch ÉCARTÉ. Vérifié sur Children v10 : le verdict avait
        # ramené `bass` et `other` au patch de l'arbitrage, et `trackDistance`
        # annonçait encore 0,1986 et 0,2174 -- les scores du réglage que le
        # mélange venait de refuser. Corriger `parameters` sans corriger ce
        # chiffre ne faisait que déplacer le mensonge d'un champ.
        retenue = distances_retenues.get(stem.name)
        if retenue is not None:
            stem.track_distance = retenue
    # LA BATTERIE AUSSI : le verdict du mélange peut lui avoir rendu une autre
    # boîte, et le rapport doit décrire celle qu'on écrit.
    rapport_batterie = chantier.rapport_batterie
    if rapport_batterie is not None and PISTE_BATTERIE in par_nom:
        piste_finale = par_nom[PISTE_BATTERIE]
        rapport_batterie["machine"] = piste_finale.machine
        rapport_batterie["parameters"] = {k: float(v) for k, v
                                          in sorted(piste_finale.parameters.items())}
        retenue = distances_retenues.get(PISTE_BATTERIE)
        if retenue is not None:
            rapport_batterie["trackDistance"] = retenue


def figer_presets(moteur: VsmEngine, pistes_export: List[ExportTrack]) -> None:
    """UN PRESET NE DOIT DÉPENDRE DE RIEN.

    Quand l'arbitrage ou le verdict retiennent un patch d'USINE, le
    dictionnaire de paramètres est vide : le preset écrit ne dit alors rien,
    et le son du projet dépend des valeurs par défaut de la machine AU MOMENT
    OÙ ON L'OUVRE. Le jour où un défaut change, le morceau change sans que
    rien ne le signale -- exactement la divergence silencieuse que ce projet
    refuse partout ailleurs. On écrit donc les valeurs RÉSOLUES : mêmes
    réglages, mêmes sons, mais inscrits.
    """
    for piste in pistes_export:
        if not piste.machine:
            continue
        try:
            defauts = {str(d["id"]): float(d["default"]) for d in moteur.parameters(piste.machine)}
        except Exception as erreur:
            print(f"      {piste.name:8s} : paramètres par défaut illisibles "
                  f"({erreur}) — preset écrit tel quel")
            continue
        defauts.update(piste.parameters)
        piste.parameters = defauts


# ---------------------------------------------------------------------------
# [5/5] Rendu et mesure
# ---------------------------------------------------------------------------

def rendre_et_mesurer(args: argparse.Namespace, sortie: Path, melange: np.ndarray,
                      chantier: Chantier, complements: Dict[str, object]) -> float:
    """Rend le projet écrit, mesure sa distance au mélange, écrit l'écoute A/B."""
    print("[5/5] Rendu du projet et mesure")
    rendu = sortie / "reconstruit.wav"
    # Résolu par la MÊME recherche que le moteur de la boucle : la version
    # précédente tentait « vsm-render » par le PATH et échouait à la toute
    # dernière étape -- après plusieurs minutes de recherche de patch, la
    # chaîne rendait tout SAUF le chiffre qu'elle promettait.
    moteur_chemin = str(find_vsm_render(args.moteur))
    try:
        subprocess.run(
            [moteur_chemin, str(sortie), str(rendu), "--sample-rate", str(SAMPLE_RATE), "--quiet"],
            check=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as erreur:
        raise Abandon(4, f"rendu impossible : {erreur}") from erreur

    reconstruit = lire_wav(rendu)
    distance = reconstruction_distance(melange, reconstruit, SAMPLE_RATE, metric=args.metrique)
    silence = reconstruction_distance(melange, np.zeros_like(melange), SAMPLE_RATE,
                                      metric=args.metrique)
    write_reconstruction_report(chantier.reconstruits, sortie / "rapport.json",
                                global_distance=distance, metric=args.metrique,
                                iterations=args.iterations, **complements)

    # Comparaison : original à gauche, reconstruction à droite. C'est l'écoute
    # qui tranche, pas le chiffre -- le chiffre dit seulement où regarder.
    ecrire_wav(sortie / "comparaison.wav", [melange, reconstruit])

    print()
    print(f"  DISTANCE GLOBALE : {distance:.4f}  "
          f"(métrique {args.metrique}, budget {args.iterations} itérations)")
    print(f"  (pour situer : la distance de l'original au silence vaut {silence:.1f})")
    return distance


# ---------------------------------------------------------------------------
# La chaîne
# ---------------------------------------------------------------------------

def chaine(args: argparse.Namespace) -> None:
    """La chaîne entière, de la lecture à l'écoute A/B. Lève `Abandon` pour s'arrêter."""
    entree = valider_entree(args)
    sortie = Path(args.sortie).expanduser()
    sortie.mkdir(parents=True, exist_ok=True)
    depart = time.perf_counter()

    print(f"[1/5] Lecture de {entree.name}")
    melange = charger_audio(entree)
    print(f"      {melange.size / SAMPLE_RATE:.1f} s, {melange.size} échantillons")

    with dossier_de_travail(args) as travail:
        pistes = obtenir_stems(args, entree, travail)

        try:
            moteur = VsmEngine(binary=args.moteur, sample_rate=SAMPLE_RATE)
        except Exception as erreur:
            raise Abandon(2, f"moteur de rendu introuvable : {erreur}") from erreur

        # LE MOTEUR VIT JUSQU'À L'EXPORT : l'automation, le verdict du mélange
        # et les presets l'interrogent encore. Le rendu final, lui, passe par
        # le binaire seul.
        with moteur:
            classifieur = charger_classifieur(args, moteur)
            if args.sans_apprentissage:
                print("      --sans-apprentissage : aucun modèle appris n'est consulté")
            frappes = charger_classifieur_frappes(args, moteur)
            candidates = ([m.strip() for m in args.machines.split(",") if m.strip()]
                          or melodic_machines(moteur))
            ctx = Contexte(args=args, moteur=moteur, sortie=sortie, travail=travail,
                           candidates=candidates, classifieur=classifieur, frappes=frappes)

            chantier = reconstruire_les_stems(ctx, pistes)

            print(f"[4/5] Écriture du projet dans {sortie}")
            pistes_export = assembler_pistes(ctx, chantier)
            distances_retenues, verdict = verdict_du_melange(ctx, chantier, pistes_export, melange)
            aligner_rapport_sur_projet(chantier, pistes_export, distances_retenues)
            figer_presets(moteur, pistes_export)

        rapport = write_project_bundle(pistes_export, sortie, title=entree.stem, tempo=args.tempo)
        # TOUT CE QUE LE RAPPORT PORTE EN PLUS DES STEMS, réuni UNE fois et
        # passé aux DEUX écritures. La première version ne passait la
        # provenance qu'à la première : la seconde, celle qui ajoute la
        # distance globale, écrasait le fichier sans elle, et le rapport final
        # -- le seul qu'on lit -- ne disait ni commit, ni options, ni modèles.
        # A4.2 était « fait » et son résultat n'existait pas sur disque.
        complements: Dict[str, object] = dict(
            provenance=provenance(args, classifieur, frappes),
            drums=chantier.rapport_batterie,
            mix_verdict=verdict or None,
        )
        write_reconstruction_report(chantier.reconstruits, sortie / "rapport.json",
                                    metric=args.metrique, iterations=args.iterations,
                                    **complements)
        print(f"      {rapport['tracks']} piste(s), {rapport['notes']} note(s)")

        rendre_et_mesurer(args, sortie, melange, chantier, complements)
        print(f"  projet    : {sortie}/project.json")
        print(f"  rapport   : {sortie}/rapport.json")
        print(f"  écoute A/B: {sortie}/comparaison.wav (gauche = original, droite = reconstruction)")
        print(f"  total     : {time.perf_counter()-depart:.0f} s")


def main() -> int:
    args = construire_parseur().parse_args()
    try:
        chaine(args)
    except Abandon as arret:
        print(f"[ERREUR] {arret}")
        return arret.code
    return 0


if __name__ == "__main__":
    sys.exit(main())
