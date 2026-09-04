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
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterator, List, Optional, Sequence, Tuple

import math

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_automation import try_cutoff_automation  # noqa: E402
from analyzer.vsm_drumkit import (build_drum_kit, drum_kit_track, drum_machine_track,  # noqa: E402
                                  eclater_par_piece,
                                  modelled_drum_track, vocal_audio_track,
                                  vocal_sampler_track)
from analyzer.vsm_engine import (VsmEngine, find_vsm_render, identite_du_moteur,  # noqa: E402
                                 moteur_perime)
from analyzer.vsm_levels import VOLUME_MAX, match_track_levels  # noqa: E402
from analyzer.vsm_mix_verdict import (MixAlternative, install_alternative,  # noqa: E402
                                      keep_what_helps_the_mix, project_mix_distance,
                                      restore_track_state, settle_verdict, track_state)
from analyzer.vsm_project_export import (DEFAULT_TRACK_VOLUME, ExportNote, ExportTrack,  # noqa: E402
                                          write_project_bundle)
from analyzer.vsm_reconstruct import (StemNote, StemReconstruction, densite_du_stem,  # noqa: E402
                                      melodic_machines, reconstruct_stem,
                                      nom_de_note, reconstruction_distance, registres_par_vides,
                                      separer_en_voix, stem_fourre_tout,
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
# ÉLARGIR A ÉTÉ MESURÉ DEUX FOIS, ET LA SECONDE A DÉMENTI LA PREMIÈRE.
#
# Le premier verdict disait : « aller à cinq coûterait une minute de plus par
# piste pour des candidates dont aucune ne l'emporte », avec la réserve « à
# remesurer au premier morceau qui démentira ça ». Le démenti est arrivé le
# 02/09/2026, et il vient du VIVIER qui a grandi : à trente-quatre candidates
# la gagnante du mélange était dans les trois premières du stem, à quarante et
# une elle n'y est plus. Le goulot n'était pas trop étroit dans l'absolu, il
# l'est DEVENU.
#
# L'hypothèse H13 (ROADMAP-fusion) et son témoin v14, une seule variable :
#
#   v13 : 41 candidates, 3 finalistes -> 0,2112
#   v14 : 41 candidates, 6 finalistes -> 0,1910   (-9,6 %)
#
# ET CELA VA PLUS VITE, ce que personne n'attendait : le verdict passe de 670 à
# 582 secondes et les réglages au mélange de 1199 à 525. Trouver la bonne
# machine dès le verdict laisse moins de chemin à parcourir au réglage, et ce
# gain dépasse le coût des trois rendus supplémentaires. Doubler les finalistes
# ne se paie donc pas : sur ce morceau, cela rapporte des deux côtés.
MACHINES_AU_MELANGE = 6

# CE NOMBRE EST UNE OPTION (`--machines-au-melange`), ET PAS SEULEMENT UNE
# CONSTANTE, parce qu'un A/B a besoin d'un témoin REPRODUCTIBLE. Le comparer à
# l'ancien comportement demandait jusqu'ici de modifier cette ligne, c'est-à-dire
# de mesurer deux fois un code différent sans que le rapport en garde trace --
# exactement ce que la provenance d'A4.2 existe pour empêcher. `0` rend la chaîne
# d'avant le § 5 decies : la gagnante du stem part seule au verdict du mélange.

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


def separer(chemin: Path, dossier: Path, modele: str,
            commande: Optional[List[str]] = None) -> Dict[str, Path]:
    """Sépare en stems dans un SOUS-PROCESSUS qui meurt, et lit le dossier.

    POURQUOI PAS DANS CE PROCESSUS. Torch et demucs restent résidents (~7 Go)
    une fois importés, et Basic Pitch charge ensuite son propre modèle :
    l'addition a fait abattre deux courses par l'OOM killer le 02/09/2026
    (codes 137, journal du noyau) sur la machine à 15 Go. En sous-processus,
    demucs vit, écrit, MEURT — la mémoire revient avant la suite. C'est la
    condition technique du modèle six sources par défaut (§ 4.2 du CDC
    multipiste) : plus de pistes ne doit pas vouloir dire plus d'OOM.

    `commande` s'injecte pour les tests ; par défaut, le module
    `analyzer.separation` du même interpréteur.
    """
    if commande is None:
        commande = [sys.executable, "-m", "analyzer.separation"]
    complet = commande + [str(chemin), str(dossier), modele]
    resultat = subprocess.run(complet, cwd=str(Path(__file__).resolve().parent))
    if resultat.returncode != 0:
        raise RuntimeError(f"séparation en sous-processus : code {resultat.returncode}")
    stems = {p.stem: p for p in sorted(Path(dossier).glob("*.wav"))}
    if not stems:
        raise RuntimeError(f"séparation terminée mais aucun stem dans {dossier}")
    return stems


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


def profils_pour_arbitrage(moteur, machines: Sequence[str]) -> Dict[str, object]:
    """Comme `profils_de`, mais `vsm.multisample` part avec TOUS ses profils.

    Le premier morceau à saxophone (*Us and Them*, 31/08/2026) a montré le
    manque : dix-neuf profils installés, un seul essayé — « les autres ne
    seront pas essayés », disait le journal — et le ténor n'a jamais concouru.
    L'arbitrage de piste est l'endroit exact où plusieurs profils se
    départagent, au même tarif qu'une machine de plus (~15 s par candidate).
    """
    d: Dict[str, object] = dict(profils_de(moteur, machines))
    if "vsm.multisample" in d:
        noms = [str(p.get("name") or "") for p in moteur.profiles()]
        noms = [n for n in noms if n]
        if len(noms) > 1:
            d["vsm.multisample"] = noms
    return d


def profils_de(moteur, machines: Sequence[str]) -> Dict[str, str]:
    """Machine -> nom de profil, pour celles qui en ont un.

    Le PROFIL suit la machine dans tous les rendus hors ligne. L'oublier ne se
    voyait pas : la piste sortait muette, le garde-fou de niveau écartait la
    candidate, et elle disparaissait du tableau sans un mot.
    """
    return {m: nom for m in machines if (nom := profil_de(moteur, m))}


def provenance(args: argparse.Namespace, classifieur, frappes,
               identite_moteur: Optional[dict] = None) -> dict:
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
        # L'ORIGINAL, en chemin absolu : le DAW qui ouvre ce projet à la main
        # peut charger l'écoute A/B sans le redemander — la chaîne sait de quel
        # fichier elle est partie, et c'est au moment de l'ouverture que la
        # comparaison compte.
        "source": str(Path(args.entree).resolve()) if getattr(args, "entree", None) else None,
        "options": {
            "separation": not args.sans_separation,
            "sampler": not args.sans_sampler,
            "arbitrage": not args.sans_arbitrage,
            "arbitrageBatterie": not args.sans_arbitrage_batterie,
            "reglagePiste": not args.sans_reglage_piste,
            "rechercheNotes": not args.sans_recherche,
            "machinesAuMelange": args.machines_au_melange,
            "reglageMelange": not args.sans_reglage_melange,
            "budgetMelange": args.budget_melange,
            "toursVerdict": args.tours_verdict,
            "secondVerdict": args.second_verdict,
            "verdictAvecAudio": not args.verdict_sans_audio,
            "piecesNonIsolees": args.garder_pieces_non_isolees,
            "rendusParalleles": args.rendus_paralleles,
            "cacheRendus": not args.sans_cache_rendus,
            "budgetPiste": args.budget_piste,
            "axesPiste": args.axes_piste,
            "finalistes": args.finalistes,
            "preselectionApprise": args.preselection_apprise,
            # Le vivier conditionne le résultat : il va dans la provenance,
            # sans quoi deux rapports ne seraient pas comparables (§ « Mesure »
            # du cahier des charges).
            "machines": args.machines or None,
            "machinesExclues": args.machines_exclues or None,
            # LE MODÈLE DE SÉPARATION, qui manquait — et c'est l'option qui
            # conditionne le plus lourdement le résultat, puisqu'elle décide du
            # NOMBRE DE PISTES. `htdemucs` en rend quatre, `htdemucs_6s` six.
            # Deux rapports séparés par des modèles différents ne décrivent pas
            # le même morceau : l'un met le piano et la guitare dans `other`,
            # l'autre leur donne une piste. Ce champ vaut `null` quand les
            # stems sont repris d'un dossier (`--stems`), ce qui est une
            # information et non une absence : la séparation n'a pas eu lieu.
            "modeleSeparation": (None if (args.stems or args.sans_separation)
                                 else args.modele),
            "stemsRepris": args.stems or None,
            # Le découpage en voix change le NOMBRE DE PISTES du résultat :
            # deux rapports qui n'ont pas le même réglage ne se comparent pas.
            "voixParStem": args.voix_par_stem,
            "voixParVides": args.voix_par_vides,
            "reverbMelange": args.reverb_melange,
            "batterieParPiece": args.batterie_par_piece,
            "voixTeteChoeurs": args.voix_tete_choeurs,
            "seuilStem": args.seuil_stem,
            # `parite` est un RACCOURCI : les trois options qu'il allume sont
            # inscrites au-dessus, chacune à sa valeur effective. On garde
            # quand même la trace de la façon dont la course a été demandée.
            "parite": args.parite,
        },
        # Les modèles CONSULTÉS, avec leur date d'entraînement -- ou « aucun »,
        # qui est une information et non une absence d'information.
        "modeles": {
            "classifieurMachine": (classifieur.date if classifieur is not None else "aucun"),
            "classifieurFrappes": (frappes.date if frappes is not None else "aucun"),
        },
        "profilMultisample": os.environ.get("VSM_PROFIL", "") or "(premier installé)",
        # LE MOTEUR QUI A RENDU L'AUDIO, et non le commit du dépôt : voir
        # `identite_du_moteur` (analyzer/vsm_engine.py). L'identité est
        # CAPTURÉE pendant que le moteur est vivant et passée ici toute faite :
        # la première version interrogeait le moteur au moment d'écrire le
        # rapport, donc APRÈS sa fermeture, et retombait sur « je ne sais
        # pas » à chaque course — en silence (vu sur v11a).
        "moteur": identite_moteur,
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
    parseur.add_argument("--modele", default="htdemucs_6s",
                         help="modèle de séparation. Le défaut est passé de htdemucs (4 stems) "
                              "à htdemucs_6s (6 stems : +guitar, +piano) le 03/09/2026, sur "
                              "mesure : -10,4 %% de distance ET deux pistes de plus sur le "
                              "témoin H22 (docs/CDC-detection-multipiste.md § 4.2). Mesuré sur "
                              "UN morceau ; contre-épreuve due à la levée de la pause des "
                              "campagnes")
    parseur.add_argument("--machines", default="",
                         help="liste de machines candidates, séparées par des virgules "
                              "(défaut : toutes les mélodiques du moteur)")
    parseur.add_argument("--parite", action="store_true", default=True,
                         help="VISER LA PARITÉ DES PISTES : autant de pistes que le morceau "
                              "a de parties (docs/CDC-detection-multipiste.md § 0). C'EST LE "
                              "DÉFAUT depuis le 04/09/2026 : allume d'un coup les quatre "
                              "découpages structurels — --voix-par-stem 4, --voix-par-vides, "
                              "--batterie-par-piece, --voix-tete-choeurs — parce qu'il faut "
                              "les quatre pour approcher la parité et que personne ne devrait "
                              "avoir à les retenir. CE QUE CELA COÛTE, ET C'EST MESURÉ SUR "
                              "DEUX MORCEAUX (CDC multipiste § 8) : −0,1 %% et NEUF pistes au "
                              "lieu de quatre sur *Us and Them* (parite-v3), +3,1 %% et SEPT "
                              "pistes sur *Sky and Sand* (sky-parite) — le prix de +9,1 %% "
                              "mesuré par H23 était celui du calage voix par voix contre le "
                              "stem entier, corrigé depuis (§ 7). La règle du § 0 "
                              "s'applique — quand structure et ressemblance s'opposent, la "
                              "structure gagne et l'écart se publie. Chaque option reste "
                              "réglable à part, et une option explicite l'emporte sur ce "
                              "raccourci")
    parseur.add_argument("--sans-parite", dest="parite", action="store_false",
                         help="LE TÉMOIN : une piste par stem, rien de découpé — la chaîne "
                              "d'avant le 04/09/2026. Sert à mesurer ce que la parité coûte "
                              "sur un morceau nouveau (§ 5 : un A/B, une variable, le même "
                              "code), et à rejouer les témoins H22a-v2, sky-t6 et leurs "
                              "suites, qui ont couru sans parité")
    parseur.add_argument("--seuil-stem", type=float, default=0.5,
                         help="part d'énergie (en %%) sous laquelle un stem n'est PAS "
                              "reconstruit : c'est un résidu de séparation, pas une partie. "
                              "Mesuré sur *Clair de Lune* (piano SEUL) : le modèle à six "
                              "sources rend piano 99,5 %% et cinq stems à 0,0-0,4 %% — en "
                              "faire des pistes fabriquerait cinq parties là où il y en a "
                              "une, l'exact contraire de l'objectif de parité. Le stem "
                              "écarté est DIT avec son chiffre, jamais tu. 0 pour ne rien "
                              "écarter")
    parseur.add_argument("--voix-tete-choeurs", action="store_true",
                         help="séparer la voix de TÊTE (le centre du champ stéréo) des CHŒURS "
                              "(le large) en deux pistes audio dont la somme redonne "
                              "exactement le stem (§ 4.5 du CDC détection-multipiste). Le "
                              "séparateur ne reconnaît pas des voix : il sépare le centre du "
                              "large, convention de mixage presque universelle, et refuse EN "
                              "LE DISANT un stem mono ou sans largeur. L'effet sur la "
                              "distance n'est pas mesuré (campagnes en pause)")
    parseur.add_argument("--batterie-par-piece", action="store_true",
                         help="une piste PAR PIÈCE détectée (kick, hihat, caisse…) au lieu "
                              "d'une piste de kit unique — la parité des pistes pour la "
                              "batterie (§ 4.4 du CDC détection-multipiste). Même machine et "
                              "même patch pour toutes : le kit reste un instrument réglé une "
                              "fois, seules les frappes se répartissent. Les machines "
                              "suivantes ne sont alors plus remises en jeu au verdict du "
                              "mélange (la piste unique qu'elles remplaceraient n'existe "
                              "plus), et le volume par pièce n'est pas calé sur le stem — "
                              "les deux sont dits au journal")
    parseur.add_argument("--voix-par-stem", type=int, default=0,
                         help="découper un stem FOURRE-TOUT (au moins 3 notes simultanées en "
                              "moyenne ET 3 octaves) en au plus N voix, une piste par voix, "
                              "partagées par registres de hauteur (H23, ROADMAP-fusion "
                              "§ 5 quaterdecies). 0 (défaut) : ne rien découper. Chaque voix "
                              "est arbitrée et réglée sur le MÊME stem audio — le rapport et "
                              "le journal disent le découpage, qui reste une approximation : "
                              "un registre n'est pas un instrument, mais une piste par "
                              "registre se retravaille, un fourre-tout non")
    parseur.add_argument("--reverb-melange", action="store_true",
                         help="EN FIN DE CHAÎNE, chercher une réverbération commune aux pistes "
                              "mélodiques en re-rendant le projet entier sur une petite grille "
                              "(pièce 0,9 et 1,0 × mélange 4 et 8 %%, les seuls points où H24 "
                              "a mesuré un gain, ROADMAP-fusion § 5 quaterdecies) et garder "
                              "le point qui rapproche le plus de l'original — ou aucun, en le "
                              "disant. Quatre rendus complets. La voix (report d'audio) et la "
                              "batterie ne sont pas touchées. Option : le gain mesuré est petit "
                              "(−2,45 %% au mieux) et l'oreille n'a pas jugé une traîne de "
                              "plusieurs secondes à 4 %%")
    parseur.add_argument("--voix-par-vides", action="store_true",
                         help="AVANT le partage en N voix, découper un stem fourre-tout là "
                              "où sa transcription laisse des VIDES (au moins deux demi-tons "
                              "que personne ne joue entre deux registres qui pèsent chacun "
                              "au moins 5 %% de la durée). Le nombre de parties est LU dans "
                              "les notes au lieu d'être imposé : l'épreuve de parité "
                              "(analyse/epreuve_parite.py) a montré que --voix-par-stem 4 "
                              "coupe en quatre un stem qui porte trois registres disjoints. "
                              "Un registre encore fourre-tout après ce découpage est ensuite "
                              "partagé par --voix-par-stem. Allumé par --parite ; inerte sur les "
                              "transcriptions denses des vrais morceaux essayés (aucun creux)")
    parseur.add_argument("--machines-exclues", default="",
                         help="machines à RETIRER du vivier, séparées par des virgules. "
                              "C'est le complément de --machines, et il existe pour une "
                              "raison de méthode : mesurer ce qu'apporte une machine "
                              "demande un témoin qui soit LE MÊME CODE, options mises à "
                              "part. Sans cette option, retirer deux machines d'un parc "
                              "de trente-six obligeait soit à en lister trente-quatre à "
                              "la main (fragile, et la liste ment dès qu'une machine "
                              "arrive), soit à éditer une constante entre deux passes — "
                              "ce que le cahier des charges interdit. Un nom inconnu est "
                              "REFUSÉ et non ignoré : une exclusion qui ne s'applique "
                              "pas fausserait la course en silence.")
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
    parseur.add_argument("--sans-recherche", action="store_true",
                         help="sauter la recherche de patch note à note. C'est le "
                              "DÉFAUT depuis le 31/08/2026 : l'A/B du § 5 undecies "
                              "(ROADMAP-fusion) a rendu, sur trois morceaux, deux "
                              "reconstructions identiques à la sixième décimale et "
                              "une MEILLEURE de 2,1 %%, en trois à quatre fois moins "
                              "de temps. L'option est conservée pour les scripts qui "
                              "la passaient déjà.")
    parseur.add_argument("--avec-recherche", action="store_true",
                         help="rétablir la recherche de patch note à note -- l'ancienne "
                              "chaîne, conservée comme TÉMOIN d'A/B. Mesurée : elle "
                              "coûte 200 à 900 s par stem et ses patchs, battus six "
                              "fois sur huit par l'usine, OCCUPAIENT des places de "
                              "machines suivantes au verdict du mélange (Jaguar : "
                              "0,2913 avec, 0,2853 sans).")
    parseur.add_argument("--sans-reglage-piste", action="store_true",
                         help="ne pas RÉGLER le patch de la machine gagnante sur la piste "
                              "entière après l'arbitrage. Par défaut, une descente par "
                              "coordonnées balaye les axes déclarés par la machine et ne "
                              "garde une valeur que si elle rapproche le rendu complet du "
                              "stem : elle ne peut donc pas dégrader le patch d'où elle part.")
    parseur.add_argument("--sans-reglage-melange", action="store_true",
                         help="ne pas affiner la gagnante de chaque piste contre le "
                              "MÉLANGE après le verdict (H1 du § 5 duodecies). C'est "
                              "le témoin de l'A/B ; le défaut fait la passe, parce "
                              "que quatre morceaux sur quatre ont montré que le stem "
                              "est un mandataire qui se paie.")
    parseur.add_argument("--budget-melange", type=int, default=30,
                         help="évaluations du réglage jugé au mélange, PAR piste "
                              "(défaut 30). Une évaluation = un rendu de projet "
                              "entier + une distance (~10 à 15 s) : c'est cher, et "
                              "c'est le seul critère qui ne soit pas un mandataire.")
    parseur.add_argument("--garder-pieces-non-isolees", action="store_true",
                         help="jouer les pièces de batterie dont AUCUNE frappe n'est "
                              "isolée (H8 du § 5 duodecies). Par défaut elles sont "
                              "écartées, en le disant : leur échantillon contient le "
                              "reste du kit, et les jouer superpose au mélange une "
                              "copie sale de ce qui sonne déjà. Mesuré : les deux "
                              "pièces de cette sorte que le classifieur de frappes "
                              "ajoutait coûtaient 10,4 %% au morceau. C'est le témoin "
                              "de l'A/B.")
    parseur.add_argument("--tours-verdict", type=int, default=3,
                         help="nombre MAXIMAL de passes du verdict du mélange (défaut 3 ; "
                              "H5 du § 5 duodecies). Le verdict est glouton et chaque "
                              "décision fait le contexte des suivantes : on le rejoue "
                              "jusqu'à ce qu'aucune piste ne change (point fixe), borné "
                              "par ce nombre. 1 = un seul tour, l'ancien comportement — "
                              "c'est le témoin de l'A/B.")
    parseur.add_argument("--verdict-sans-audio", action="store_true",
                         help="TÉMOIN de la campagne 8 (CDC multipiste § 12) : juger le "
                              "mélange SANS ses pistes audio, comme la chaîne le faisait "
                              "sans le dire depuis que la voix est une piste audio. Ne "
                              "sert qu'à mesurer ce que la correction change.")
    parseur.add_argument("--second-verdict", type=int, default=0,
                         help="CAMPAGNE 7 (CDC multipiste § 11) : après le réglage au "
                              "mélange de la gagnante de chaque piste mélodique, remettre "
                              "en jeu ses N meilleures écartées qui changent de machine, "
                              "chacune RÉGLÉE au mélange avec le même budget, et garder la "
                              "meilleure des réglées. Le verdict jugeait des candidates "
                              "AVANT réglage, et le réglage peut renverser son ordre. "
                              "Défaut 0 : le témoin, l'ancien comportement. Coût : un "
                              "réglage au mélange par candidate.")
    parseur.add_argument("--rendus-paralleles", type=int, default=3,
                         help="nombre de rendus de candidates menés de front à "
                              "l'arbitrage de piste (défaut 3 ; H3 du § 5 duodecies). "
                              "1 = série, l'ancien comportement. Le classement est "
                              "déterministe quel que soit ce nombre.")
    parseur.add_argument("--sans-cache-rendus", action="store_true",
                         help="ne pas relire ni écrire le cache de rendus de piste "
                              "(cache/rendus, H2 du § 5 duodecies). Le cache est sûr "
                              "par construction -- sa clé porte l'empreinte du moteur "
                              "-- mais un A/B doit pouvoir prouver qu'il ne change "
                              "rien : voilà son témoin.")
    parseur.add_argument("--machines-au-melange", type=int, default=MACHINES_AU_MELANGE,
                         help=f"nombre de machines SUIVANTES du classement de piste "
                              f"remises en jeu au verdict du mélange (défaut "
                              f"{MACHINES_AU_MELANGE}). Mesuré sur *Sky and Sand* "
                              "(ROADMAP-fusion § 5 decies) : les classements au stem "
                              "et au mélange sont à peu près inverses, et la machine "
                              "que le mélange retient était TROISIÈME au stem. "
                              "0 rend la chaîne d'avant cette mesure -- la gagnante "
                              "part seule -- et c'est le témoin des A/B.")
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
    if args.avec_recherche and args.sans_recherche:
        raise SystemExit("--avec-recherche et --sans-recherche se contredisent : "
                         "choisir.")
    # LA PARITÉ EST UN RACCOURCI, PAS UN MODE — ET C'EST LE DÉFAUT depuis le
    # 04/09/2026 (§ 8 : deux morceaux mesurés, −0,1 % et +3,1 %, la règle du
    # § 5 est satisfaite). Elle allume les quatre découpages structurels et le
    # DIT, poste par poste ; une option écrite à la main l'emporte, pour qu'un
    # A/B sur un seul découpage reste possible sans démonter le raccourci ;
    # --sans-parite rend la chaîne d'avant, pour les témoins.
    if args.parite:
        allumes = []
        if args.voix_par_stem == 0:
            args.voix_par_stem = 4
            allumes.append("--voix-par-stem 4")
        if not args.batterie_par_piece:
            args.batterie_par_piece = True
            allumes.append("--batterie-par-piece")
        if not args.voix_tete_choeurs:
            args.voix_tete_choeurs = True
            allumes.append("--voix-tete-choeurs")
        # LE QUATRIÈME : les registres lus dans les vides de la transcription.
        # Inerte sur les huit pistes réelles des trois courses où on l'a
        # essayé (aucun creux dans une transcription dense), décisif sur
        # l'épreuve à vérité connue (trois registres disjoints).
        if not args.voix_par_vides:
            args.voix_par_vides = True
            allumes.append("--voix-par-vides")
        print("      --parite : " + (", ".join(allumes) if allumes
                                     else "rien à allumer, tout était déjà demandé"))
        print("      la parité prime sur la ressemblance quand les deux s'opposent "
              "(§ 0 du CDC) ; mesurée à −0,1 % sur *Us and Them* (9 pistes) et "
              "+3,1 % sur *Sky and Sand* (7 pistes) — --sans-parite pour le témoin")
    # LE DÉFAUT EST « SANS RECHERCHE » (§ 5 undecies, A/B du 31/08/2026).
    # Une exception : --sans-arbitrage rejoue l'ancienne chaîne d'avant
    # l'arbitrage de piste, qui n'a QUE la recherche pour choisir une machine ;
    # il implique donc la recherche, et le dit.
    if args.sans_arbitrage and not args.sans_recherche:
        if not args.avec_recherche:
            print("      --sans-arbitrage implique --avec-recherche : sans arbitrage, "
                  "seule la recherche choisit une machine")
        args.avec_recherche = True
    if not args.avec_recherche:
        args.sans_recherche = True
    if args.sans_recherche and args.sans_arbitrage:
        raise SystemExit("--sans-recherche exige l'arbitrage de piste : sans "
                         "recherche NI arbitrage, plus rien ne choisit de machine.")
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
    # La part d'énergie de chaque stem, mesurée avant toute reconstruction :
    # c'est elle qui distingue une partie d'un résidu de séparation.
    parts: Dict[str, float] = field(default_factory=dict)

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
    # LES PISTES QUI PARTAGENT UN STEM : nom de piste -> nom du groupe. Les
    # voix d'un stem découpé par registres, les pièces d'une batterie éclatée.
    # Elles se calent ENSEMBLE (voir `_caler_un_groupe`) : chacune comparée au
    # stem entier recevrait le gain qu'il faudrait pour le remplacer à elle
    # seule, et leur somme sortirait N fois trop fort.
    pistes_groupees: Dict[str, str] = field(default_factory=dict)
    rapport_batterie: Optional[Dict[str, object]] = None


# ---------------------------------------------------------------------------
# [2/5] Les stems
# ---------------------------------------------------------------------------

def partage_du_morceau(pistes: Dict[str, Path]) -> List[Dict[str, object]]:
    """QUELLE PART DU MORCEAU CHAQUE STEM PORTE-T-IL ?

    POURQUOI CE CHIFFRE MANQUAIT ET CE QU'IL A RÉVÉLÉ. Le rapport donnait
    quatre distances côte à côte, ce qui laisse croire à quatre pistes
    comparables. Elles ne le sont pas : sur *Us and Them*, `other` porte
    **62,1 %** de l'énergie du morceau, `vocals` 20,8 %, `drums` 13,0 % et
    `bass` 4,1 %. Les deux tiers du morceau sont sur une seule piste, jouée par
    une seule machine — et rien ne le disait.

    La part est mesurée sur les stems D'ORIGINE, jamais sur le rendu. C'est le
    partage du MORCEAU qu'on décrit, pas celui de notre copie : une piste
    ratée, donc silencieuse, pèserait zéro dans le rendu et disparaîtrait
    justement du tableau où il faut la voir.

    L'énergie plutôt que la durée ou le nombre de notes : c'est elle qui dit ce
    qu'on entend. Une nappe tenue tout le morceau ne fait que quelques notes.
    """
    energies: Dict[str, float] = {}
    for nom, chemin in pistes.items():
        try:
            echantillons = lire_wav(Path(chemin))
            energies[nom] = float(np.sum(np.square(echantillons, dtype=np.float64)))
        except Exception as erreur:  # noqa: BLE001 — un stem illisible se DIT
            print(f"      {nom:8s} : part d'énergie non mesurée ({erreur})")
            energies[nom] = 0.0
    total = sum(energies.values())
    if total <= 0.0:
        return []
    partage = [
        {"stem": nom, "partEnergie": round(100.0 * valeur / total, 1)}
        for nom, valeur in sorted(energies.items(), key=lambda kv: -kv[1])
    ]
    detail = ", ".join(f"{p['stem']} {p['partEnergie']} %" for p in partage)
    print(f"      partage du morceau : {detail}")
    # LA PHRASE QUI COMPTE, quand une piste porte le morceau à elle seule. Le
    # seuil est à la moitié : au-delà, parler de « quatre pistes » est une
    # description trompeuse de ce qu'on a produit.
    if partage[0]["partEnergie"] >= 50.0:
        # LE CONSEIL NE CONSEILLE QUE CE QUI RESTE À FAIRE. La première version
        # renvoyait à `--modele htdemucs_6s` même quand la course tournait DÉJÀ
        # dessus — vu sur *Sky and Sand*, où `drums` porte 78 % avec les six
        # sources : un conseil qu'on a déjà suivi discrédite les autres.
        conseil = (" Voir --modele htdemucs_6s pour séparer davantage."
                   if len(partage) <= 4 else
                   " Le modèle à six sources ne l'a pas partagé : c'est un stem "
                   "dense par nature (--voix-par-stem le découpe par registres).")
        print(f"      ATTENTION : « {partage[0]['stem']} » porte "
              f"{partage[0]['partEnergie']} % du morceau à lui seul. Une seule machine "
              f"jouera cette part-là.{conseil}")
    return partage


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
                   par_sampler: bool = False,
                   tete_et_choeurs: bool = False) -> List[Tuple[ExportTrack, np.ndarray]]:
    """Le stem vocal, REPORTÉ tel quel — sur une piste audio (ou deux).

    La voix ne se synthétise pas — le § 6 de la feuille de route le dit depuis
    le début, et chercher un patch dessus produisait un chiffre (obx, d=0,196
    sur Children) qui ne voulait rien dire : ce n'est pas parce qu'un OB-X
    approche le spectre d'une voix qu'il chante. C'est dit pour ce que c'est.

    `tete_et_choeurs` (--voix-tete-choeurs, § 4.5 du CDC multipiste) sépare la
    voix de TÊTE (le centre du champ stéréo) des CHŒURS (le large), en deux
    pistes audio dont la somme redonne EXACTEMENT le stem. La séparation lit
    le fichier STÉRÉO elle-même : le chargeur mono de la chaîne replie
    précisément ce qu'elle exploite. Trois refus possibles, tous DITS : stem
    mono, largeur sous le seuil, ou mode sampler (une note par échantillon —
    deux échantillons feraient deux notes, pas deux voix).
    """
    audio = charger_audio(chemin)
    if par_sampler:
        if tete_et_choeurs:
            print(f"      {nom:8s} : tête/chœurs IGNORÉ avec --voix-sampler — "
                  f"le report par sampler ne porte qu'un échantillon")
        piste = vocal_sampler_track(audio, SAMPLE_RATE, sortie / "samples", name="Voix")
        moyen = "sampler"
    else:
        if tete_et_choeurs:
            from analyzer.vsm_voix import lire_wav_stereo, separer_tete_et_choeurs
            stereo = lire_wav_stereo(chemin)
            separation = None
            if stereo is None:
                print(f"      {nom:8s} : tête/chœurs impossible — le stem vocal est MONO, "
                      f"il n'y a pas de champ stéréo à séparer")
            else:
                gauche, droite, _taux = stereo
                separation = separer_tete_et_choeurs(gauche, droite)
                if separation is None:
                    print(f"      {nom:8s} : tête/chœurs refusé — largeur stéréo sous le "
                          f"seuil, une piste « chœurs » quasi vide passerait pour une partie")
            if separation is not None:
                from analyzer.vsm_voix import ecrire_wav_stereo
                dossier = sortie / "samples"
                dossier.mkdir(parents=True, exist_ok=True)
                ecrire_wav_stereo(dossier / "voix-tete.wav", separation.tete, SAMPLE_RATE)
                ecrire_wav_stereo(dossier / "voix-choeurs.wav", separation.choeurs, SAMPLE_RATE)
                et = float(np.sum(separation.tete ** 2))
                ec = float(np.sum(separation.choeurs ** 2))
                total = max(1e-12, et + ec)
                print(f"      {nom:8s} : voix SÉPARÉE tête/chœurs par le champ stéréo — "
                      f"tête {100 * et / total:.0f} %, chœurs {100 * ec / total:.0f} % "
                      f"(part latérale {separation.part_laterale:.2f}) ; "
                      f"tête + chœurs = stem, exactement")
                pistes = []
                for etiquette, fichier, canal in (("Voix · tête", "voix-tete.wav", separation.tete),
                                                   ("Voix · chœurs", "voix-choeurs.wav",
                                                    separation.choeurs)):
                    mono = canal.mean(axis=1)
                    piste_voix = vocal_audio_track(mono, SAMPLE_RATE, dossier,
                                                    name=etiquette, nom_fichier=fichier)
                    if piste_voix is not None:
                        pistes.append((piste_voix, mono))
                if len(pistes) == 2:
                    return pistes
                print(f"      {nom:8s} : une des deux voix est vide après séparation — "
                      f"report intégral à la place")
        piste = vocal_audio_track(audio, SAMPLE_RATE, sortie / "samples", name="Voix")
        moyen = "piste audio"
    if piste is None:
        print(f"      {nom:8s} : stem vocal vide, piste ignorée")
        return []
    duree = audio.size / SAMPLE_RATE
    print(f"      {nom:8s} : {moyen}, report intégral ({duree:.0f} s) "
          f"— la voix n'est pas reconstruite, elle est reportée")
    return [(piste, audio)]


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
    # Le kit détecté : c'est lui qui sait quelles frappes appartiennent à
    # quelle pièce, et l'éclatement par pièce (--batterie-par-piece) en vit.
    kit: Optional[object] = None
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
                         write_samples=not args.sans_sampler, hit_classifier=ctx.frappes,
                         drop_unisolated=not args.garder_pieces_non_isolees)
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
    # LA PARITÉ POUR LA BATTERIE, DITE QUAND ELLE MANQUE. Le seuil de
    # fourre-tout des stems mélodiques (polyphonie et ambitus) n'a aucun sens
    # pour un kit : il ne voit donc PAS le cas de *Sky and Sand*, où la
    # batterie porte 78 % du morceau et contient CINQ pièces distinctes, sur
    # une seule piste. Ici, on ne devine rien — les pièces sont classées, on
    # sait exactement combien de parties la piste porte.
    part_batterie = ctx.parts.get(nom)
    # Le nombre de PISTES que ferait le découpage est celui des VOIX jouées,
    # pas celui des gabarits : deux gabarits d'un même kick (le premier coup
    # d'un morceau n'a pas de queue) tombent sur la même voix et restent
    # ensemble. L'épreuve de parité annonçait « 4 » pour 3 pistes rendues.
    voix_jouees = len({int(note.note) for note in piste.notes})
    if (not args.batterie_par_piece and voix_jouees >= 2
            and part_batterie is not None and part_batterie >= 25.0):
        print(f"      {nom:8s} : cette batterie porte {part_batterie} % du morceau sur UNE "
              f"piste et contient {len(kit.slots)} pièces distinctes sur {voix_jouees} voix — "
              f"--batterie-par-piece en ferait {voix_jouees}")
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
    resultat = ResultatBatterie(piste=piste, audio=audio, rapport=rapport, kit=kit)

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
                                    profils_pour_arbitrage(ctx.moteur, ctx.candidates)),
        workdir=ctx.travail / "arbitrage" / nom,
        parallel_renders=ctx.args.rendus_paralleles,
        render_cache=not ctx.args.sans_cache_rendus,
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
    stem.profile = gagnant.profile
    classement = ", ".join(
        f"{v.machine.split('.')[-1]}"
        + (f"[{v.profile}]" if v.profile else "")
        + f"={v.distance:.3f}"
        + ("*" if v.origin == "patch d'usine" else "")
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
    suivantes = runners_up(verdicts, count=ctx.args.machines_au_melange)
    if not suivantes:
        return []
    detail = ", ".join(
        f"{v.machine.split('.')[-1]} à {(v.distance - gagnant.distance) / max(1e-9, gagnant.distance) * 100:.1f} %"
        for v in suivantes)
    print(f"      {nom:8s} : {len(suivantes)} machine(s) suivante(s) remises en jeu "
          f"au verdict du mélange — {detail}")
    return [MixAlternative(parameters=dict(v.parameters),
                           label=f"machine suivante ({v.machine})"
                                 + (f" · {v.profile}" if v.profile else ""),
                           machine=v.machine, track_distance=v.distance,
                           profile=v.profile)
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
        profile=stem.profile or profil_de(ctx.moteur, stem.machine),
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


def reconstruire_stem_melodique(ctx: Contexte, nom: str,
                                chemin: Path) -> List[ResultatMelodique]:
    """Transcription, découpe en voix s'il y a lieu, puis recherche par voix.

    Rend une LISTE : un stem ordinaire donne une piste, un stem fourre-tout
    découpé par `--voix-par-stem` en donne plusieurs. La liste vide signifie
    « rien d'exploitable », et cela a été dit au journal.
    """
    args = ctx.args
    notes = extraire_notes(chemin)
    if not notes:
        print(f"      {nom:8s} : aucune note détectée, piste ignorée")
        return []
    audio = charger_audio(chemin)

    # LA PLAINTE DU FOURRE-TOUT, sur le stem ENTIER et dans les deux chemins
    # (recherche ou non) : elle vivait après la recherche note à note, qui est
    # SAUTÉE par défaut depuis le § 5 undecies — elle ne s'imprimait donc
    # jamais en course réelle.
    plainte = stem_fourre_tout(densite_du_stem(notes))
    if plainte:
        print(f"      {nom:8s} : {plainte}")

    if args.voix_par_vides and plainte:
        registres = registres_par_vides(notes)
        if len(registres) > 1:
            # LE NOMBRE DE PARTIES EST LU, PAS IMPOSÉ : chaque registre que
            # les vides délimitent devient une piste ; un registre encore
            # fourre-tout est ensuite partagé par --voix-par-stem, s'il est là.
            bornes = ", ".join(
                f"MIDI {min(n.note for n in r)}-{max(n.note for n in r)} ({len(r)} notes)"
                for r in registres)
            print(f"      {nom:8s} : DÉCOUPÉ en {len(registres)} registres par les VIDES de la "
                  f"transcription — {bornes}")
            resultats = []
            for registre in registres:
                lo, hi = min(n.note for n in registre), max(n.note for n in registre)
                sous_nom = f"{nom} · {nom_de_note(lo)}-{nom_de_note(hi)}"
                sous_voix = ([list(registre)] if args.voix_par_stem <= 1
                             else separer_en_voix(list(registre), args.voix_par_stem))
                if len(sous_voix) > 1:
                    print(f"      {sous_nom:8s} : encore un fourre-tout, partagé en "
                          f"{len(sous_voix)} voix par registres")
                for k, notes_voix in enumerate(sous_voix, 1):
                    nom_voix = sous_nom if len(sous_voix) == 1 else f"{sous_nom} · voix {k}"
                    resultat = _reconstruire_notes(ctx, nom_voix, list(notes_voix), audio)
                    if resultat is not None:
                        resultats.append(resultat)
            return resultats
        print(f"      {nom:8s} : aucun vide dans la transcription, rien à découper par les vides")

    if args.voix_par_stem > 1 and plainte:
        voix = separer_en_voix(notes, args.voix_par_stem)
        if len(voix) > 1:
            # LE DÉCOUPAGE EST DIT, registre par registre : c'est une décision
            # qui change le nombre de pistes du résultat, pas un détail.
            registres = ", ".join(
                f"voix {k} = MIDI {min(n.note for n in v)}-{max(n.note for n in v)}"
                f" ({len(v)} notes)" for k, v in enumerate(voix, 1))
            print(f"      {nom:8s} : DÉCOUPÉ en {len(voix)} voix par registres — {registres}")
            resultats = []
            for k, notes_voix in enumerate(voix, 1):
                sous_nom = f"{nom} · voix {k}"
                resultat = _reconstruire_notes(ctx, sous_nom, list(notes_voix), audio)
                if resultat is not None:
                    resultats.append(resultat)
            return resultats

    resultat = _reconstruire_notes(ctx, nom, notes, audio)
    return [resultat] if resultat is not None else []


def _reconstruire_notes(ctx: Contexte, nom: str, notes: List[StemNote],
                        audio: np.ndarray) -> Optional[ResultatMelodique]:
    """Recherche de patch, arbitrage et réglage d'UNE piste : le stem entier
    d'ordinaire, une voix quand le fourre-tout a été découpé. Chaque voix est
    jugée contre le MÊME audio de stem — il n'existe pas d'audio par voix, et
    en fabriquer un par filtrage amputerait les timbres (H23)."""
    args = ctx.args
    if args.sans_recherche:
        # LE § 5 UNDECIES EN ACTE : pas de recherche note à note. L'arbitrage
        # de piste juge déjà le patch d'usine de TOUTES les machines
        # (`build_candidates`) ; la recherche n'ajoutait que ses patchs
        # cherchés, battus six fois sur huit. On lui donne un porte-drapeau
        # neutre que l'arbitrage remplacera, et rien d'autre.
        stem = StemReconstruction(
            name=nom, machine=ctx.candidates[0], parameters={},
            distance=0.0, notes=list(notes), considered=[])
        print(f"      {nom:8s} : recherche note à note SAUTÉE (--sans-recherche) — "
              f"les patchs d'usine des {len(ctx.candidates)} machines partent "
              f"à l'arbitrage de piste")
        resultat = ResultatMelodique(stem=stem, audio=audio)
        resultat.secondes = arbitrer_sur_piste(ctx, nom, stem, audio)
        if stem.track_distance is None:
            # Sans recherche, l'arbitrage était le seul juge ; s'il n'a rien
            # retenu (filtre de niveau), il n'y a pas de repli honnête.
            print(f"      {nom:8s} : aucun patch d'usine retenu au niveau, piste ignorée")
            return None
        stem.distance = stem.track_distance
        if not args.sans_reglage_piste:
            regler_sur_piste(ctx, nom, stem, audio)
        return resultat

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
        # UN RÉSIDU DE SÉPARATION N'EST PAS UNE PARTIE, et le reconstruire
        # fabriquerait une piste là où il n'y a rien. Mesuré sur *Clair de
        # Lune* (piano SEUL) : le modèle à six sources rend `piano` 99,5 % et
        # CINQ stems entre 0,0 et 0,4 % — six pistes pour une partie, l'exact
        # contraire de l'objectif de parité.
        #
        # CE N'EST PAS COUPER UNE PISTE : couper reste une décision humaine,
        # et la règle vaut pour ce qu'on ENTEND. Ici on refuse de FABRIQUER,
        # et le refus est dit avec son chiffre — le stem reste sur le disque,
        # `--seuil-stem 0` le reconstruit.
        part = ctx.parts.get(nom)
        if part is not None and args.seuil_stem > 0.0 and part < args.seuil_stem:
            print(f"      {nom:8s} : NON reconstruit — {part} % de l'énergie du morceau, "
                  f"sous le seuil de {args.seuil_stem} % (résidu de séparation, pas une "
                  f"partie ; --seuil-stem 0 pour le reconstruire quand même)")
            continue
        if nom == "vocals" and not args.sans_sampler:
            for piste, audio in reporter_voix(nom, chemin, ctx.sortie,
                                              par_sampler=ctx.args.voix_sampler,
                                              tete_et_choeurs=args.voix_tete_choeurs):
                chantier.pistes_directes.append(piste)
                chantier.audio_par_stem[piste.name] = audio
            continue
        if nom == "drums" or args.batterie:
            batterie = reconstruire_batterie(ctx, nom, chemin)
            if batterie is not None:
                pieces: List[ExportTrack] = []
                if args.batterie_par_piece and batterie.kit is not None \
                        and batterie.piste.machine != "vsm.sampler":
                    pieces = eclater_par_piece(batterie.piste, batterie.kit)
                if len(pieces) > 1:
                    detail = ", ".join(f"{p.name.split(chr(183))[-1].strip()} "
                                       f"({len(p.notes)})" for p in pieces)
                    print(f"      {nom:8s} : batterie ÉCLATÉE en {len(pieces)} piste(s) "
                          f"par pièce — {detail}")
                    # DEUX RENONCEMENTS, DITS : les boîtes suivantes ne sont
                    # plus remises en jeu au verdict (la piste unique qu'elles
                    # remplaceraient n'existe plus), et le volume par pièce
                    # n'est pas calé sur le stem (le stem est le kit ENTIER,
                    # caler chaque pièce dessus la gonflerait).
                    if batterie.secondes:
                        print(f"      {nom:8s} : {len(batterie.secondes)} boîte(s) "
                              f"suivante(s) NON remises en jeu au verdict — "
                              f"conséquence du découpage par pièce")
                    batterie.rapport["splitByPiece"] = [
                        {"track": p.name, "hits": len(p.notes)} for p in pieces]
                    for sous_piste in pieces:
                        chantier.pistes_directes.append(sous_piste)
                        # Le stem de la batterie ENTIÈRE sert de référence à
                        # chaque pièce, et le groupe dit qu'elles se partagent.
                        chantier.audio_par_stem[sous_piste.name] = batterie.audio
                        chantier.pistes_groupees[sous_piste.name] = PISTE_BATTERIE
                        # LE PATCH D'AVANT RÉGLAGE, pour chaque pièce. Sur le
                        # témoin H22a, le verdict du mélange a PRÉFÉRÉ le patch
                        # d'usine de la batterie à celui réglé sur la piste
                        # (preset final = défauts) ; les pièces, qui n'avaient
                        # pas cette alternative, gardaient le patch réglé :
                        # +28 % mesurés (batterie-v2). Même patch pour toutes,
                        # le kit reste un instrument réglé une fois.
                        if batterie.patch_avant_reglage is not None:
                            chantier.patchs_avant_reglage[sous_piste.name] = dict(batterie.patch_avant_reglage)
                    chantier.audio_par_stem[PISTE_BATTERIE] = batterie.audio
                    chantier.rapport_batterie = batterie.rapport
                else:
                    chantier.pistes_directes.append(batterie.piste)
                    chantier.audio_par_stem[PISTE_BATTERIE] = batterie.audio
                    chantier.rapport_batterie = batterie.rapport
                    if batterie.patch_avant_reglage is not None:
                        chantier.patchs_avant_reglage[batterie.piste.name] = batterie.patch_avant_reglage
                    if batterie.secondes:
                        chantier.machines_secondes.setdefault(PISTE_BATTERIE, []).extend(batterie.secondes)
            continue
        for melodique in reconstruire_stem_melodique(ctx, nom, chemin):
            chantier.reconstruits.append(melodique.stem)
            chantier.audio_par_stem[melodique.stem.name] = melodique.audio
            # Une VOIX d'un stem découpé porte le nom du stem suivi du sien :
            # elle partage donc son stem avec ses sœurs, et le calage doit les
            # prendre ensemble.
            if melodique.stem.name != nom:
                chantier.pistes_groupees[melodique.stem.name] = nom
            if melodique.secondes:
                # Par le nom de la PISTE (la voix, quand il y a découpe), pas
                # celui du stem : deux voix du même stem ont chacune leurs
                # machines remises en jeu au verdict.
                chantier.machines_secondes.setdefault(
                    melodique.stem.name, []).extend(melodique.secondes)
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
                                    SAMPLE_RATE,
                                    groupes=chantier.pistes_groupees):
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

    # H5 (§ 5 duodecies) : LE VERDICT SE STABILISE EN POINT FIXE. La passe est
    # gloutonne, piste par piste dans un ordre fixe, et chaque décision fait le
    # contexte des suivantes — le fan-out des profils a montré sur *Us and
    # Them* que deux trajectoires gloutonnes peuvent finir à des morceaux
    # différents. On rejoue donc la passe jusqu'à ce qu'aucune piste ne change
    # de machine, de patch ou de profil, borné par --tours-verdict ; un tour
    # qui ne change rien est le point fixe, et il est DIT. Le témoin de l'A/B
    # est --tours-verdict 1, l'ancien comportement.
    depart_verdict = time.perf_counter()

    def une_passe():
        return keep_what_helps_the_mix(
            pistes_export, alternatives, melange, chantier.audio_par_stem, ctx.sortie,
            workdir=ctx.travail / "verdict",
            sample_rate=SAMPLE_RATE, metric=ctx.args.metrique,
            tempo=ctx.args.tempo, binary=ctx.args.moteur,
            profiles=profils_de(ctx.moteur, melodic_machines(ctx.moteur)),
            groupes=chantier.pistes_groupees)

    decisions, tours_joues, changees_par_tour = settle_verdict(
        pistes_export, une_passe, ctx.args.tours_verdict)
    recit = " ; ".join(f"tour {i}: {', '.join(noms) or 'rien'}"
                       for i, noms in enumerate(changees_par_tour, start=1))
    borne_atteinte = changees_par_tour and changees_par_tour[-1]
    print(f"      verdict du mélange : {tours_joues} tour(s) "
          f"({recit}) en {time.perf_counter() - depart_verdict:.0f} s"
          + (" — borne atteinte avant le point fixe" if borne_atteinte else ""))
    distances_retenues = {d.track: d.kept_track_distance for d in decisions
                          if d.kept_track_distance is not None}

    # H1 (§ 5 duodecies) : LA DERNIÈRE PASSE SE JUGE AU MORCEAU. Le réglage de
    # piste optimise le stem, et le stem est un mandataire qui se paie —
    # quatre morceaux sur quatre. Ici, la gagnante de chaque piste mélodique
    # est affinée avec la distance du MÉLANGE pour objectif ; chaque valeur
    # n'est gardée que si elle rapproche, la passe ne peut pas dégrader ce que
    # le verdict a rendu. Même ordre de pistes que le verdict : les décisions
    # déjà prises font le contexte des suivantes.
    reglages_melange: List[Dict[str, object]] = []
    if not ctx.args.sans_reglage_melange and ctx.args.budget_melange > 0:
        from analyzer.vsm_mix_refine import refine_against_mix

        noms_melodiques = [d.track for d in decisions
                           if any(st.name == d.track for st in chantier.reconstruits)]
        for nom_piste in noms_melodiques:
            depart = time.perf_counter()
            resultat = refine_against_mix(
                pistes_export, nom_piste, melange, chantier.audio_par_stem,
                ctx.sortie, workdir=ctx.travail / "verdict",
                sample_rate=SAMPLE_RATE, engine=ctx.moteur,
                budget=ctx.args.budget_melange,
                metric=ctx.args.metrique, tempo=ctx.args.tempo,
                binary=ctx.args.moteur, groupes=chantier.pistes_groupees)
            if resultat is None:
                print(f"      {nom_piste:8s} : réglage au mélange non tenté "
                      f"(machine sans axe, ou rendu de départ muet)")
                continue
            gain = resultat.start_distance - resultat.distance
            print(f"      {nom_piste:8s} : réglage au MÉLANGE "
                  f"{resultat.start_distance:.4f} -> {resultat.distance:.4f} "
                  f"({'-' if gain > 0 else ''}{abs(gain):.4f}, "
                  f"{resultat.evaluations} évaluations, "
                  f"{time.perf_counter() - depart:.0f} s)"
                  + (" — aucune valeur n'a rapproché, patch du verdict conservé"
                     if not resultat.improvements else ""))
            reglages_melange.append({
                "track": nom_piste,
                "mixDistanceBefore": resultat.start_distance,
                "mixDistanceAfter": resultat.distance,
                "evaluations": resultat.evaluations,
                "parameters": resultat.parameters if resultat.improvements else None})
            # Le stem du rapport suit ce que le projet joue, la règle
            # d'`aligner_rapport_sur_projet`.
            for st in chantier.reconstruits:
                if st.name == nom_piste and resultat.improvements:
                    st.parameters = dict(resultat.parameters)

    # CAMPAGNE 7 (§ 11) : LE SECOND VERDICT, ENTRE CANDIDATES RÉGLÉES. Le
    # premier verdict a jugé des machines AVANT réglage, et sky-parite-m9 a
    # montré qu'une gagnante au verdict (string, 0,2486 contre 0,2523 pour
    # vector) arrive au même point que sa rivale une fois réglée (0,2346
    # contre 0,2345) : l'ordre du verdict n'est pas celui d'après réglage.
    # Ici, les N meilleures écartées qui CHANGENT de machine sont installées,
    # réglées au mélange avec le même budget, et la meilleure des réglées est
    # gardée -- chaque chiffre est publié, y compris ceux des perdantes.
    seconds_verdicts: List[Dict[str, object]] = []
    if (ctx.args.second_verdict > 0 and not ctx.args.sans_reglage_melange
            and ctx.args.budget_melange > 0):
        from analyzer.vsm_mix_refine import refine_against_mix
        profils = profils_de(ctx.moteur, melodic_machines(ctx.moteur))
        noms_melodiques = [d.track for d in decisions
                           if any(st.name == d.track for st in chantier.reconstruits)]
        for nom_piste in noms_melodiques:
            decision = next((d for d in decisions if d.track == nom_piste), None)
            piste = next((t for t in pistes_export if t.name == nom_piste), None)
            if decision is None or piste is None:
                continue
            ecartees = sorted(((lib, d) for lib, d in decision.rejected
                               if lib.startswith("machine suivante")), key=lambda x: x[1])
            candidates = ecartees[:ctx.args.second_verdict]
            if not candidates:
                print(f"      {nom_piste:8s} : second verdict sans objet (aucune machine écartée)")
                continue
            depart = time.perf_counter()
            mesure = lambda: project_mix_distance(  # noqa: E731
                pistes_export, melange, ctx.sortie, ctx.travail / "verdict",
                SAMPLE_RATE, ctx.args.metrique, ctx.args.tempo, ctx.args.moteur)
            etat_gagnante = track_state(piste)
            volumes_gagnante = {t.name: float(t.volume) for t in pistes_export}
            machine_gagnante = piste.machine
            d_gagnante = mesure()
            meilleure = ("gagnante réglée", d_gagnante, etat_gagnante, volumes_gagnante)
            bilan: List[Dict[str, object]] = []
            for libelle, d_verdict in candidates:
                proposition = next((a for a in alternatives.get(nom_piste, ())
                                    if a.label == libelle), None)
                if proposition is None:
                    continue
                install_alternative(piste, pistes_export, proposition, etat_gagnante,
                                    chantier.audio_par_stem, ctx.sortie, SAMPLE_RATE,
                                    profils, chantier.pistes_groupees)
                d_installee = mesure()
                resultat = refine_against_mix(
                    pistes_export, nom_piste, melange, chantier.audio_par_stem,
                    ctx.sortie, workdir=ctx.travail / "verdict",
                    sample_rate=SAMPLE_RATE, engine=ctx.moteur,
                    budget=ctx.args.budget_melange,
                    metric=ctx.args.metrique, tempo=ctx.args.tempo,
                    binary=ctx.args.moteur, groupes=chantier.pistes_groupees)
                d_reglee = resultat.distance if resultat is not None else d_installee
                bilan.append({"label": libelle, "mixDistanceAtVerdict": d_verdict,
                              "mixDistanceInstalled": d_installee, "mixDistanceRefined": d_reglee,
                              "evaluations": resultat.evaluations if resultat else 0})
                print(f"      {nom_piste:8s} : second verdict, {libelle} : au verdict {d_verdict:.4f}, "
                      f"installée {d_installee:.4f}, réglée {d_reglee:.4f}"
                      f" (gagnante réglée {d_gagnante:.4f})")
                if d_reglee < meilleure[1] - 1e-6:
                    meilleure = (libelle, d_reglee, track_state(piste),
                                 {t.name: float(t.volume) for t in pistes_export})
                # On remet la gagnante avant d'essayer la suivante : chaque
                # candidate est jugée dans le même contexte.
                restore_track_state(piste, etat_gagnante, piste.machine)
                for t in pistes_export:
                    if t.name in volumes_gagnante:
                        t.volume = volumes_gagnante[t.name]
            restore_track_state(piste, meilleure[2], piste.machine)
            for t in pistes_export:
                if t.name in meilleure[3]:
                    t.volume = meilleure[3][t.name]
            if piste.machine != machine_gagnante:
                piste.machine_display_name = ""
            print(f"      {nom_piste:8s} : second verdict -> {meilleure[0]} ({meilleure[1]:.4f}) "
                  f"en {time.perf_counter() - depart:.0f} s"
                  + ("" if meilleure[0] == "gagnante réglée"
                     else f" — la machine change : {machine_gagnante} -> {piste.machine}"))
            seconds_verdicts.append({
                "track": nom_piste, "kept": meilleure[0], "mixDistance": meilleure[1],
                "winnerRefined": d_gagnante, "candidates": bilan,
                "machineBefore": machine_gagnante, "machineAfter": piste.machine})
            for st in chantier.reconstruits:
                if st.name == nom_piste:
                    st.parameters = dict(piste.parameters)

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
    for r in reglages_melange:
        verdict.append({"track": r["track"], "mixRefine": r})
    for sv in seconds_verdicts:
        verdict.append({"track": sv["track"], "secondVerdict": sv})
    # H5 : le nombre de tours joués et ce que chaque tour a changé sont PUBLIÉS
    # — un point fixe atteint d'office (un seul tour, rien de changé au-delà)
    # est une information, pas une absence d'information.
    verdict.append({"verdictRounds": tours_joues,
                    "changedByRound": changees_par_tour})
    return distances_retenues, verdict


def _distance_au_stem(stem, machine: str) -> Optional[float]:
    """La meilleure distance AU STEM relevée pour `machine` pendant l'arbitrage.

    Renvoie `None` si la machine n'a pas concouru — auquel cas on préfère
    laisser le champ tel quel plutôt que d'inventer un chiffre : une valeur
    absente est une information, une valeur fausse n'en est pas une.
    """
    meilleures = [d for m, _origine, d in getattr(stem, "track_considered", []) if m == machine]
    if meilleures:
        return min(meilleures)
    candidates = [d for m, d in getattr(stem, "considered", []) if m == machine]
    return min(candidates) if candidates else None


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
        # ET LA DISTANCE AU STEM AVEC, POUR LA MÊME RAISON — le mensonge avait
        # simplement reculé d'un champ de plus.
        #
        # `stem.distance` est le score de la machine sur le STEM. Quand le
        # verdict du mélange remplace la machine, ce champ restait celui de la
        # MACHINE ÉCARTÉE, publié sous le nom de la nouvelle. Ce n'est pas une
        # imprécision : mesuré sur `usandthem-v9`, le rapport annonçait
        # « vsm.cs80, distance 0,185085 » alors que la vraie distance de
        # `vsm.cs80` au stem de basse est **0,362272** — la valeur publiée
        # était celle de `vsm.multisample`, qui avait gagné l'arbitrage avant
        # d'être écartée. Sur `other`, « vsm.tb303 » publiait 0,173624 pour une
        # distance réelle de 0,299406.
        #
        # Cette panne a fait écrire une conclusion FAUSSE dans deux documents
        # (« sa distance au stem étant identique au dix-millième à celle de la
        # machine qu'elle remplace »). La vérité est plus intéressante que
        # l'erreur : une machine DEUX FOIS plus loin au stem gagne au mélange.
        vraie = _distance_au_stem(stem, piste_finale.machine)
        if vraie is not None:
            stem.distance = vraie
        # ET LE PROFIL, pour la même raison encore : `vsm.multisample`
        # remplacée par une machine sans profil laisserait « FR3-Saw-Lead »
        # publié sous le nom de la nouvelle. Le rapport doit décrire le projet
        # qu'on écrit, champ par champ, sans exception.
        stem.profile = piste_finale.profile
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

GRILLE_REVERB: Tuple[Tuple[float, float], ...] = ((0.9, 0.04), (0.9, 0.08), (1.0, 0.04), (1.0, 0.08))


def piste_melodique(piste: ExportTrack) -> bool:
    """Ni batterie, ni report d'audio : les seules pistes que H24 a réverbérées."""
    return bool(piste.machine) and not piste.is_drums and not piste.audio_path


def effet_reverb(taille: float, dosage: float) -> Dict[str, object]:
    return {"type": "reverb", "parameters": {
        "effect.reverb.mix": float(dosage), "effect.reverb.size": float(taille),
        "effect.reverb.damping": 0.5, "effect.reverb.width": 1.0}}


def chercher_reverb_au_melange(args: argparse.Namespace, sortie: Path,
                               pistes_export: List[ExportTrack], melange: np.ndarray,
                               mesurer=None) -> Dict[str, object]:
    """H24 EN ACTE, comme OPTION : la réverbération cherchée au mélange.

    Le projet est déjà écrit dans `sortie`. On le re-rend tel quel (le
    témoin), puis avec le même insert sur toutes les pistes mélodiques à
    chaque point de GRILLE_REVERB, et l'on garde le point qui rapproche le
    plus — s'il rapproche. Le refus est DIT avec son chiffre : une grille qui
    ne gagne rien laisse les pistes sèches, et le rapport le porte.

    `mesurer(pistes) -> distance` s'injecte pour les tests ; par défaut, un
    rendu complet par vsm-render dans un sous-dossier de travail, mesuré avec
    la métrique de la course. Les rendus tournent de front.
    """
    from concurrent.futures import ThreadPoolExecutor

    if mesurer is None:
        moteur_chemin = str(find_vsm_render(args.moteur))
        travail = sortie / "travail-reverb"

        def mesurer(pistes: List[ExportTrack], etiquette: str = "point") -> float:
            dossier = travail / etiquette
            write_project_bundle(pistes, dossier, title="reverb", tempo=args.tempo)
            # LES ÉCHANTILLONS NE SUIVENT PAS LE PROJET : la voix et les
            # frappes vivent dans `sortie/samples`, en chemins relatifs. Sans
            # ce lien, le premier essai a rendu la grille SANS la voix — et
            # `--quiet` taisait l'avertissement du moteur : la recherche
            # choisissait sur un mélange amputé, sans que rien ne le dise.
            lien = dossier / "samples"
            if (sortie / "samples").is_dir() and not lien.exists():
                os.symlink((sortie / "samples").resolve(), lien)
            rendu = dossier / "rendu.wav"
            resultat = subprocess.run([moteur_chemin, str(dossier), str(rendu), "--sample-rate",
                                       str(SAMPLE_RATE)], check=True, capture_output=True, text=True)
            plaintes = [ligne for ligne in (resultat.stdout + resultat.stderr).splitlines()
                        if "avertissement" in ligne or "illisible" in ligne]
            if plaintes:
                raise RuntimeError("le moteur s'est plaint pendant la recherche de réverb — "
                                   "la grille serait mesurée sur un projet amputé : " + plaintes[0])
            return reconstruction_distance(melange, lire_wav(rendu), SAMPLE_RATE, metric=args.metrique)

    touchees = [p.name for p in pistes_export if piste_melodique(p)]
    if not touchees:
        print("      réverb au mélange : aucune piste mélodique, rien à chercher")
        return {"pistes": [], "temoin": None, "grille": [], "retenu": None}

    def variante(taille: float, dosage: float) -> List[ExportTrack]:
        copie = []
        for piste in pistes_export:
            double = ExportTrack(**{k: v for k, v in piste.__dict__.items()})
            if piste_melodique(piste):
                double.effects = list(piste.effects) + [effet_reverb(taille, dosage)]
            copie.append(double)
        return copie

    points = [("temoin", None)] + [(f"p{int(t * 10)}-m{int(round(d * 100)):02d}", (t, d))
                                   for t, d in GRILLE_REVERB]
    with ThreadPoolExecutor(max_workers=max(1, args.rendus_paralleles)) as pool:
        futurs = {nom: pool.submit(mesurer, pistes_export if pt is None else variante(*pt), nom)
                  for nom, pt in points}
        distances = {nom: float(f.result()) for nom, f in futurs.items()}
    temoin = distances["temoin"]
    grille = [{"taille": t, "dosage": d, "distance": distances[nom],
               "ecartPourcent": (distances[nom] / temoin - 1.0) * 100.0 if temoin else None}
              for nom, (t, d) in [(n, p) for n, p in points if p is not None]]
    meilleur = min(grille, key=lambda g: (g["distance"], g["dosage"], g["taille"]))
    lignes = ", ".join(f"pièce {g['taille']:.1f} à {100 * g['dosage']:.0f} % → {g['distance']:.4f} "
                       f"({g['ecartPourcent']:+.2f} %)" for g in grille)
    if meilleur["distance"] < temoin:
        for piste in pistes_export:
            if piste_melodique(piste):
                piste.effects.append(effet_reverb(meilleur["taille"], meilleur["dosage"]))
        print(f"      réverb au mélange : témoin {temoin:.4f} ; {lignes}")
        print(f"      réverb au mélange : RETENUE pièce {meilleur['taille']:.1f} à "
              f"{100 * meilleur['dosage']:.0f} % sur {', '.join(touchees)} "
              f"({meilleur['ecartPourcent']:+.2f} %)")
        retenu = {"taille": meilleur["taille"], "dosage": meilleur["dosage"]}
    else:
        print(f"      réverb au mélange : témoin {temoin:.4f} ; {lignes}")
        print(f"      réverb au mélange : AUCUN point ne rapproche — les pistes restent sèches")
        retenu = None
    return {"pistes": touchees, "temoin": temoin, "grille": grille, "retenu": retenu}


def ajouter_groupes(pistes_export: List[ExportTrack], groupes: Dict[str, str]) -> List[str]:
    """LES PISTES D'UN GROUPE ARRIVENT DANS LE DAW SOUS UN BUS DE GROUPE.

    Le chantier tient le registre `pistes_groupees` (nom de piste -> nom du
    groupe) pour caler ensemble les pièces d'une batterie éclatée et les voix
    d'un stem découpé. Ce que la chaîne sait, le DAW doit le montrer : une
    piste de groupe par groupe (« Batterie », « other »), les membres routés
    vers elle. Un seul fader règle alors tout le kit, comme dans n'importe
    quelle console -- sans cela, neuf pistes de parité se réglaient une par
    une. Le groupe est au volume 1 et sans effet : il ne change pas le son,
    seulement la prise en main. Les groupes sont AJOUTÉS EN FIN de liste pour
    ne décaler aucun index de piste existant. Rend les noms des groupes créés.
    """
    if not groupes:
        return []
    index_par_nom = {piste.name: i for i, piste in enumerate(pistes_export)}
    crees: List[str] = []
    for nom_groupe in sorted(set(groupes.values()), key=lambda n: min(
            index_par_nom.get(m, len(pistes_export)) for m, g in groupes.items() if g == n)):
        membres = [m for m, g in groupes.items() if g == nom_groupe and m in index_par_nom]
        if len(membres) < 2:
            continue
        pistes_export.append(ExportTrack(name=nom_groupe, is_group=True, volume=1.0))
        index_groupe = len(pistes_export) - 1
        for membre in membres:
            pistes_export[index_par_nom[membre]].output_group = index_groupe
        crees.append(nom_groupe)
    return crees


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

def charger_tous_les_modules() -> None:
    """UNE COURSE EST UNE PHOTOGRAPHIE DU CODE À SON DÉPART, pas un film.

    Plusieurs modules de la chaîne étaient importés À LA DEMANDE, au moment de
    servir : `vsm_mix_refine` au réglage du mélange, `vsm_voix` à la voix,
    `note_extraction` à la première transcription. Un module importé cinq
    heures après le départ est lu SUR LE DISQUE à cet instant -- dans l'état où
    il est ALORS, pas dans celui du départ. Le 03/09/2026, la course
    usandthem-parite-v2 (5 h 24) est morte au réglage final : `vsm_mix_refine`
    fraîchement réécrit demandait à `vsm_levels`, chargé en mémoire dans sa
    version d'avant, une fonction qu'il n'avait pas encore. Tout le verdict
    était fait ; rien n'a été écrit.

    On importe donc TOUT au départ, ici et en une fois. Les importations
    paresseuses restent en place là où elles sont (elles servent les tests et
    `--help`, qui n'ont pas à payer Basic Pitch) : elles retombent sur
    `sys.modules`, et plus jamais sur le disque.
    """
    import importlib
    for nom in ("librosa", "analyzer.note_extraction", "analyzer.vsm_classifier",
                "analyzer.vsm_drum_corpus", "analyzer.vsm_voix", "analyzer.vsm_drumkit",
                "analyzer.vsm_mix_refine", "analyzer.vsm_mix_verdict", "analyzer.vsm_distance_cache",
                "analyzer.vsm_corpus", "analyzer.vsm_automation", "analyzer.vsm_track_refine",
                "analyzer.vsm_track_arbitration", "analyzer.vsm_offline_render",
                "analyzer.vsm_render_cache", "analyzer.vsm_project_export"):
        importlib.import_module(nom)


def chaine(args: argparse.Namespace) -> None:
    charger_tous_les_modules()
    if args.verdict_sans_audio:
        import analyzer.vsm_mix_verdict as _verdict
        _verdict.COPIER_LES_PISTES_AUDIO = False
        print("      TÉMOIN : le verdict et le réglage au mélange jugeront SANS les pistes audio")
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
        # LE PARTAGE, MESURÉ TOUT DE SUITE ET SUR LES STEMS D'ORIGINE : c'est
        # le morceau qu'on décrit, pas le rendu (voir `partage_du_morceau`).
        partage = partage_du_morceau(pistes)

        try:
            moteur = VsmEngine(binary=args.moteur, sample_rate=SAMPLE_RATE)
        except Exception as erreur:
            raise Abandon(2, f"moteur de rendu introuvable : {erreur}") from erreur

        # LE MOTEUR VIT JUSQU'À L'EXPORT : l'automation, le verdict du mélange
        # et les presets l'interrogent encore. Le rendu final, lui, passe par
        # le binaire seul.
        with moteur:
            # LE MOTEUR SE PRÉSENTE, ET SE PLAINT S'IL EST PÉRIMÉ. Un binaire
            # plus vieux que ses sources rend un vivier plus petit sans rien
            # dire : c'est ce qui est arrivé à v13 et v14 le 02/09/2026.
            plainte = moteur_perime(moteur)
            if plainte:
                print("      " + plainte)
            # L'IDENTITÉ SE CAPTURE ICI, moteur vivant : `machines()` parle au
            # processus. La provenance, elle, s'écrit après sa fermeture.
            identite_moteur = identite_du_moteur(moteur)
            classifieur = charger_classifieur(args, moteur)
            if args.sans_apprentissage:
                print("      --sans-apprentissage : aucun modèle appris n'est consulté")
            frappes = charger_classifieur_frappes(args, moteur)
            candidates = ([m.strip() for m in args.machines.split(",") if m.strip()]
                          or melodic_machines(moteur))
            exclues = [m.strip() for m in args.machines_exclues.split(",") if m.strip()]
            if exclues:
                # PANNE MUETTE INTERDITE : une exclusion qui ne correspond à
                # aucune machine (faute de frappe, machine renommée) laisserait
                # la course tourner avec un vivier qu'on croit réduit et qui ne
                # l'est pas. Le témoin serait alors identique à ce qu'il est
                # censé mesurer, et le verdict dirait « aucun effet ».
                inconnues = [m for m in exclues if m not in candidates]
                if inconnues:
                    raise SystemExit(
                        "[ERREUR] --machines-exclues nomme des machines qui ne sont pas "
                        "dans le vivier : " + ", ".join(inconnues) + "\n"
                        "          vivier : " + ", ".join(sorted(candidates)))
                candidates = [m for m in candidates if m not in exclues]
                print(f"      {len(exclues)} machine(s) EXCLUE(S) du vivier "
                      f"(--machines-exclues) : {', '.join(exclues)}")
            ctx = Contexte(args=args, moteur=moteur, sortie=sortie, travail=travail,
                           parts={p["stem"]: p["partEnergie"] for p in partage},
                           candidates=candidates, classifieur=classifieur, frappes=frappes)

            chantier = reconstruire_les_stems(ctx, pistes)

            print(f"[4/5] Écriture du projet dans {sortie}")
            pistes_export = assembler_pistes(ctx, chantier)
            distances_retenues, verdict = verdict_du_melange(ctx, chantier, pistes_export, melange)
            aligner_rapport_sur_projet(chantier, pistes_export, distances_retenues)
            figer_presets(moteur, pistes_export)
            groupes_crees = ajouter_groupes(pistes_export, chantier.pistes_groupees)
            if groupes_crees:
                print(f"      groupes du DAW : {', '.join(groupes_crees)} — les pistes d'un même "
                      f"stem partagent un fader")

        rapport = write_project_bundle(pistes_export, sortie, title=entree.stem, tempo=args.tempo)
        reverb = None
        if args.reverb_melange:
            reverb = chercher_reverb_au_melange(args, sortie, pistes_export, melange)
            if reverb.get("retenu"):
                rapport = write_project_bundle(pistes_export, sortie, title=entree.stem, tempo=args.tempo)
        # TOUT CE QUE LE RAPPORT PORTE EN PLUS DES STEMS, réuni UNE fois et
        # passé aux DEUX écritures. La première version ne passait la
        # provenance qu'à la première : la seconde, celle qui ajoute la
        # distance globale, écrasait le fichier sans elle, et le rapport final
        # -- le seul qu'on lit -- ne disait ni commit, ni options, ni modèles.
        # A4.2 était « fait » et son résultat n'existait pas sur disque.
        complements: Dict[str, object] = dict(
            provenance=provenance(args, classifieur, frappes, identite_moteur),
            drums=chantier.rapport_batterie,
            mix_verdict=verdict or None,
            partage=partage,
            reverb=reverb,
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
