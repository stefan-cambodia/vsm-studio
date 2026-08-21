#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
WAV/MP3 -> projet VSM rejouable (MIDI + patchs), et distance publiée.

    .venv/bin/python reconstruire.py morceau.mp3
    .venv/bin/python reconstruire.py morceau.wav --sortie ./ma-reconstruction
    .venv/bin/python reconstruire.py morceau.wav --sans-separation --machines vsm.sh101,vsm.juno106

Ce que la commande produit :

    sortie/
      project.json  midi/  instruments/   <- le projet, ouvrable dans le DAW
      rapport.json                        <- distance par stem, machines écartées
      reconstruit.wav                     <- le rendu du projet
      comparaison.wav                     <- original à gauche, reconstruction à droite

CE QU'ELLE NE PROMET PAS : reconstruire n'est pas reproduire. Les machines du
parc sont des synthétiseurs ; une guitare acoustique n'a pas de machine cible.
La distance publiée le dira, et c'est pour cela qu'elle est publiée.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_automation import try_cutoff_automation
from analyzer.vsm_levels import match_track_levels
from analyzer.vsm_engine import VsmEngine, VsmEngineError, find_vsm_render
from analyzer.vsm_drumkit import (build_drum_kit, drum_kit_track,
                                  modelled_drum_track, vocal_sampler_track)
from analyzer.vsm_project_export import ExportNote, ExportTrack, write_project_bundle
from analyzer.vsm_reconstruct import (
    StemNote,
    export_reconstruction,
    melodic_machines,
    reconstruct_stem,
    reconstruction_distance,
    write_reconstruction_report,
)

SAMPLE_RATE = 44100


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


def extraire_notes(chemin: Path) -> List[StemNote]:
    """
    Transcrit un fichier en notes.

    La confiance rendue par le transcripteur sert de vélocité : une note
    détectée de justesse ne doit pas sonner aussi fort qu'une note franche.
    """
    from analyzer.note_extraction import extract_notes

    notes = []
    for brut in extract_notes(chemin):
        duree = float(brut["end"]) - float(brut["start"])
        if duree <= 0.0:
            continue
        confiance = float(brut.get("confidence", 0.8))
        notes.append(
            StemNote(
                note=int(brut["midi"]),
                velocity=max(1, min(127, int(40 + 87 * confiance))),
                start=float(brut["start"]),
                duration=duree,
                # La même confiance sert deux fois, et ce n'est pas un
                # raccourci : elle règle la vélocité (une note à peine
                # détectée a été jouée doucement) ET signale le doute à
                # l'utilisateur dans le piano roll.
                confidence=confiance,
            )
        )
    return notes


def separer(chemin: Path, dossier: Path, modele: str) -> Dict[str, Path]:
    from analyzer.separation import separate_audio

    return {nom: Path(p) for nom, p in separate_audio(chemin, dossier, modele).items()}


def main() -> int:
    parseur = argparse.ArgumentParser(
        description="Reconstruit un morceau avec les machines du DAW VSM.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parseur.add_argument("entree", help="fichier audio (wav, mp3, flac...)")
    parseur.add_argument("--sortie", default="reconstruction", help="dossier de sortie")
    parseur.add_argument("--batterie", action="store_true",
                         help="traiter l'entrée comme un stem de batterie : découpe en coups "
                              "et rejeu par la batterie modélisée, sans recherche de patch")
    parseur.add_argument("--batterie-echantillonnee", action="store_true",
                         help="rejouer la batterie par le SAMPLER, avec les coups découpés "
                              "dans l'enregistrement, au lieu de la batterie modélisée. "
                              "C'est l'ancien comportement ; il reste accessible parce qu'il "
                              "est plus fidèle au coup enregistré, et moins réglable.")
    parseur.add_argument("--sans-separation", action="store_true",
                         help="ne pas séparer en stems : traiter le fichier comme une seule piste")
    parseur.add_argument("--modele", default="htdemucs", help="modèle de séparation")
    parseur.add_argument("--machines", default="",
                         help="liste de machines candidates, séparées par des virgules "
                              "(défaut : toutes les mélodiques du moteur)")
    parseur.add_argument("--iterations", type=int, default=20,
                         help="budget de recherche par machine (défaut 20). "
                              "Mesuré : le doubler change souvent la machine retenue.")
    parseur.add_argument("--tempo", type=float, default=120.0, help="tempo du projet écrit")
    parseur.add_argument("--metrique", default="v2", choices=("v1", "v2"),
                         help="métrique de comparaison (défaut v2 ; v1 pour rejouer "
                              "d'anciennes mesures — les deux ne se comparent pas)")
    parseur.add_argument("--finalistes", type=int, default=None,
                         help="nombre de machines retenues après le dégrossissage "
                              "(défaut : la moitié). 0 DÉSACTIVE la présélection : "
                              "chaque candidate reçoit le budget complet. Plus lent, "
                              "mais c'est le seul réglage sous lequel les distances de "
                              "toutes les machines se comparent -- une candidate écartée "
                              "au dégrossissage n'a pas de score comparable aux autres.")
    parseur.add_argument("--garder-stems", default=None,
                         help="dossier où conserver les stems séparés, pour rejouer une "
                              "mesure sans repayer la séparation")
    parseur.add_argument("--moteur", default=None, help="chemin de vsm-render")
    args = parseur.parse_args()

    entree = Path(args.entree).expanduser()
    if not entree.exists():
        print(f"[ERREUR] fichier introuvable : {entree}")
        return 1

    sortie = Path(args.sortie).expanduser()
    sortie.mkdir(parents=True, exist_ok=True)
    depart = time.perf_counter()

    print(f"[1/5] Lecture de {entree.name}")
    melange = charger_audio(entree)
    duree = melange.size / SAMPLE_RATE
    print(f"      {duree:.1f} s, {melange.size} échantillons")

    with tempfile.TemporaryDirectory() as temporaire:
        if args.garder_stems:
            # Conserver les stems permet de rejouer une mesure sans repayer la
            # séparation, qui coûte quatre minutes sur un morceau de quatre.
            temporaire = str(Path(args.garder_stems).expanduser())
            Path(temporaire).mkdir(parents=True, exist_ok=True)
        # --- séparation -------------------------------------------------------
        if args.sans_separation:
            print("[2/5] Séparation désactivée : une seule piste")
            pistes = {"melange": entree}
        else:
            print(f"[2/5] Séparation en stems ({args.modele})")
            try:
                pistes = separer(entree, Path(temporaire) / "stems", args.modele)
            except Exception as erreur:
                # La séparation est lourde et peut manquer. On le DIT et on
                # continue sur le mélange, plutôt que d'abandonner : une
                # reconstruction imparfaite reste plus utile qu'aucune.
                print(f"      échec ({erreur}) — repli sur le mélange entier")
                pistes = {"melange": entree}

        # --- transcription + recherche de patch -------------------------------
        try:
            moteur = VsmEngine(binary=args.moteur, sample_rate=SAMPLE_RATE)
        except Exception as erreur:
            print(f"[ERREUR] moteur de rendu introuvable : {erreur}")
            return 2

        with moteur:
            candidates = ([m.strip() for m in args.machines.split(",") if m.strip()]
                          or melodic_machines(moteur))
            print(f"[3/5] {len(candidates)} machine(s) candidate(s)")

            reconstruits = []
            audio_par_stem: Dict[str, np.ndarray] = {}
            pistes_batterie: List[ExportTrack] = []
            for nom, chemin in sorted(pistes.items()):
                # RÉPARTITION DE LA VERSION FINALE : le sampler n'est QUE pour
                # la voix. La batterie, elle, a désormais sa propre machine.
                #
                # La voix ne se synthétise pas — le § 6 de la feuille de route
                # le dit depuis le début, et chercher un patch dessus produisait
                # un chiffre (obx, d=0,196 sur Children) qui ne voulait rien
                # dire : ce n'est pas parce qu'un OB-X approche le spectre d'une
                # voix qu'il chante. Le stem vocal est donc REPORTÉ tel quel
                # dans le sampler, et c'est dit pour ce que c'est.
                if nom == "vocals":
                    audio_voix = charger_audio(chemin)
                    piste_voix = vocal_sampler_track(
                        audio_voix, SAMPLE_RATE, sortie / "samples", name="Voix")
                    if piste_voix is None:
                        print(f"      {nom:8s} : stem vocal vide, piste ignorée")
                        continue
                    duree = piste_voix.notes[0].duration
                    print(f"      {nom:8s} : sampler, report intégral ({duree:.0f} s) "
                          f"— la voix n'est pas reconstruite, elle est reportée")
                    pistes_batterie.append(piste_voix)
                    audio_par_stem["Voix"] = audio_voix
                    continue

                # La batterie : les frappes sont détectées et classées comme
                # avant, mais elles pilotent `vsm.drums`, qui MODÉLISE peaux et
                # métal, au lieu de charger des coups découpés. On y gagne un
                # kit réglable, on y perd la fidélité littérale au coup
                # enregistré ; le compromis est mesuré, pas supposé.
                if nom == "drums" or args.batterie:
                    audio_batterie = charger_audio(chemin)
                    kit = build_drum_kit(audio_batterie, SAMPLE_RATE, sortie / "samples")
                    if kit is None:
                        print(f"      {nom:8s} : aucun coup détecté, piste ignorée")
                        continue
                    detail = " ".join(
                        f"{s.family}={s.hit_count}" for s in kit.slots
                    )
                    if args.batterie_echantillonnee:
                        piste = drum_kit_track(kit, name="Batterie")
                        moyen = "sampler"
                    else:
                        piste = modelled_drum_track(kit, name="Batterie")
                        moyen = "vsm.drums"
                    print(f"      {nom:8s} : {moyen}, {len(kit.slots)} pièce(s), "
                          f"{kit.total_hits} frappe(s) — {detail}")
                    for avertissement in kit.warnings:
                        print(f"                 ! {avertissement}")
                    pistes_batterie.append(piste)
                    audio_par_stem["Batterie"] = audio_batterie
                    continue

                notes = extraire_notes(chemin)
                if not notes:
                    print(f"      {nom:8s} : aucune note détectée, piste ignorée")
                    continue

                audio = charger_audio(chemin)
                debut = time.perf_counter()
                resultat = reconstruct_stem(
                    nom, audio, notes, moteur,
                    sample_rate=SAMPLE_RATE,
                    machines=candidates,
                    max_iterations=args.iterations,
                    metric=args.metrique,
                    shortlist=args.finalistes,
                )
                if resultat is None:
                    print(f"      {nom:8s} : aucune note exploitable")
                    continue
                podium = ", ".join(
                    f"{m.split('.')[-1]}={d:.2f}"
                    for m, d in sorted(resultat.considered, key=lambda x: x[1])[:3]
                )
                print(f"      {nom:8s} : {resultat.machine:14s} d={resultat.distance:.3f} "
                      f"({len(notes)} notes, {time.perf_counter()-debut:.0f} s) — {podium}")
                reconstruits.append(resultat)
                audio_par_stem[resultat.name] = audio

            if not reconstruits and not pistes_batterie:
                print("[ERREUR] aucune piste reconstruite")
                return 3

            # --- export ----------------------------------------------------------
            print(f"[4/5] Écriture du projet dans {sortie}")
            pistes_export = []
            for stem in reconstruits:
                piste = ExportTrack(
                    name=stem.name,
                    machine=stem.machine,
                    parameters=stem.parameters,
                    notes=[ExportNote(n.note, n.velocity, n.start, n.duration) for n in stem.notes],
                )
                # AUTOMATION DE COUPURE : la trajectoire de brillance du stem,
                # gardée seulement si elle RAPPROCHE le rendu complet du stem --
                # mesuré par la même distance que tout le reste, jamais supposé.
                audio_stem = audio_par_stem.get(stem.name)
                if audio_stem is not None:
                    courbe, d_sans, d_avec, motif = try_cutoff_automation(
                        audio_stem, SAMPLE_RATE, piste, moteur)
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
            pistes_export += pistes_batterie

            # VOLUMES calés sur l'équilibre du morceau : chaque piste est rendue
            # seule et ramenée au niveau efficace de SON stem. C'était le premier
            # écart audible une fois la batterie devenue dense : chaque stem se
            # rapprochait de son original, et le mélange s'en éloignait.
            for ligne in match_track_levels(pistes_export, audio_par_stem, sortie, SAMPLE_RATE):
                print(f"      {ligne}")
        rapport = write_project_bundle(pistes_export, sortie, title=entree.stem, tempo=args.tempo)
        write_reconstruction_report(reconstruits, sortie / "rapport.json",
                                    metric=args.metrique, iterations=args.iterations)
        print(f"      {rapport['tracks']} piste(s), {rapport['notes']} note(s)")

        # --- rendu et distance ------------------------------------------------
        print("[5/5] Rendu du projet et mesure")
        rendu = sortie / "reconstruit.wav"
        # Résolu par la MÊME recherche que le moteur de la boucle : la version
        # précédente tentait « vsm-render » par le PATH et échouait à la toute
        # dernière étape -- après plusieurs minutes de recherche de patch, la
        # chaîne rendait tout SAUF le chiffre qu'elle promettait.
        moteur_chemin = str(find_vsm_render(args.moteur))
        try:
            subprocess.run(
                [moteur_chemin, str(sortie), str(rendu),
                 "--sample-rate", str(SAMPLE_RATE), "--quiet"],
                check=True,
            )
        except (subprocess.CalledProcessError, FileNotFoundError) as erreur:
            print(f"      rendu impossible : {erreur}")
            return 4

        reconstruit = lire_wav(rendu)
        distance = reconstruction_distance(melange, reconstruit, SAMPLE_RATE,
                                           metric=args.metrique)
        silence = reconstruction_distance(melange, np.zeros_like(melange), SAMPLE_RATE,
                                          metric=args.metrique)
        write_reconstruction_report(reconstruits, sortie / "rapport.json",
                                    global_distance=distance, metric=args.metrique,
                                    iterations=args.iterations)

        # Comparaison : original à gauche, reconstruction à droite. C'est
        # l'écoute qui tranche, pas le chiffre -- le chiffre dit seulement où
        # regarder.
        ecrire_wav(sortie / "comparaison.wav", [melange, reconstruit])

        print()
        print(f"  DISTANCE GLOBALE : {distance:.4f}  "
              f"(métrique {args.metrique}, budget {args.iterations} itérations)")
        print(f"  (pour situer : la distance de l'original au silence vaut {silence:.1f})")
        print(f"  projet    : {sortie}/project.json")
        print(f"  rapport   : {sortie}/rapport.json")
        print(f"  écoute A/B: {sortie}/comparaison.wav (gauche = original, droite = reconstruction)")
        print(f"  total     : {time.perf_counter()-depart:.0f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
