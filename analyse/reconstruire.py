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
from analyzer.vsm_levels import VOLUME_MAX, match_track_levels
from analyzer.vsm_mix_verdict import MixAlternative, keep_what_helps_the_mix
from analyzer.vsm_engine import VsmEngine, VsmEngineError, find_vsm_render
from analyzer.vsm_drumkit import (build_drum_kit, drum_kit_track,
                                  modelled_drum_track, vocal_sampler_track)
from analyzer.vsm_project_export import (DEFAULT_TRACK_VOLUME, ExportNote, ExportTrack,
                                          write_project_bundle)
from analyzer.vsm_track_arbitration import (arbitrate_on_track, build_candidates,
                                             close_runner_up)
from analyzer.vsm_track_refine import refine_patch_on_track
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
    parseur.add_argument("--sans-sampler", action="store_true",
                         help="interdire le sampler sur TOUT le morceau : le stem vocal "
                              "passe par la recherche de patch comme les autres, au lieu "
                              "d'être reporté tel quel, et la batterie modélisée n'écrit "
                              "plus d'échantillons. Le projet ne contient alors que des "
                              "machines de synthèse. À employer quand on veut un projet "
                              "entièrement rejouable sans le disque d'origine ; le prix "
                              "est une voix synthétisée, c'est-à-dire pas une voix.")
    parseur.add_argument("--sans-separation", action="store_true",
                         help="ne pas séparer en stems : traiter le fichier comme une seule piste")
    parseur.add_argument("--modele", default="htdemucs", help="modèle de séparation")
    parseur.add_argument("--machines", default="",
                         help="liste de machines candidates, séparées par des virgules "
                              "(défaut : toutes les mélodiques du moteur)")
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
    args = parseur.parse_args()

    if args.sans_sampler and args.batterie_echantillonnee:
        print("[ERREUR] --sans-sampler et --batterie-echantillonnee se contredisent : "
              "la batterie échantillonnée EST le sampler.")
        return 1

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
        if args.stems:
            dossier_stems = Path(args.stems).expanduser()
            trouves = {chemin.stem: chemin for chemin in sorted(dossier_stems.rglob("*.wav"))}
            if not trouves:
                print(f"[ERREUR] aucun stem dans {dossier_stems}")
                return 1
            print(f"[2/5] Stems repris de {dossier_stems} : {', '.join(sorted(trouves))}")
            pistes = trouves
        elif args.sans_separation:
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
            if args.sans_sampler:
                print("      sampler INTERDIT : la voix passe par la recherche de patch, "
                      "la batterie modélisée n'écrit pas d'échantillons")

            reconstruits = []
            # Patch d'AVANT le réglage, par nom de piste : c'est l'alternative
            # que le verdict du mélange remettra en concurrence. Les stems
            # mélodiques la portent dans leur `StemReconstruction` ; la
            # batterie, qui n'en a pas, passe par ce dictionnaire.
            patchs_avant_reglage: Dict[str, Dict[str, float]] = {}
            # La machine SECONDE de l'arbitrage, quand elle est à portée. C'est
            # la seule façon qu'une égalité mal tranchée cesse d'être
            # définitive : le verdict du mélange ne savait défaire qu'un
            # réglage, jamais une machine.
            machines_secondes: Dict[str, MixAlternative] = {}
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
                if nom == "vocals" and not args.sans_sampler:
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
                    kit = build_drum_kit(audio_batterie, SAMPLE_RATE, sortie / "samples",
                                         write_samples=not args.sans_sampler)
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
                    # LA BATTERIE SE RÈGLE AUSSI, et c'est le dernier endroit
                    # de la chaîne où un patch restait celui d'usine sans que
                    # personne l'ait jugé. Elle pèse pourtant le plus lourd dans
                    # le mélange (niveau efficace 0,156 sur Children, contre
                    # 0,087 pour la basse) : la laisser hors du réglage revenait
                    # à soigner les pistes qu'on entend le moins.
                    #
                    # Pas d'ARBITRAGE en revanche : `vsm.drums` n'a pas de
                    # concurrente crédible ici. Les deux boîtes à rythmes du
                    # parc n'ont ni la même correspondance de notes ni les mêmes
                    # pièces, et les mettre en lice demanderait une traduction
                    # dont l'effet n'est pas mesuré.
                    if not args.sans_reglage_piste and piste.machine != "vsm.sampler":
                        depart_reglage = time.perf_counter()
                        patchs_avant_reglage[piste.name] = dict(piste.parameters)
                        affine = refine_patch_on_track(
                            machine=piste.machine,
                            parameters=piste.parameters,
                            notes=piste.notes,
                            stem_audio=audio_batterie,
                            engine=moteur,
                            workdir=Path(temporaire) / "reglage" / nom,
                            sample_rate=SAMPLE_RATE,
                            budget=args.budget_piste,
                            axes=args.axes_piste,
                            metric=args.metrique,
                            tempo=args.tempo,
                            binary=args.moteur,
                            name=piste.name,
                            stem_rms=float(np.sqrt(np.mean(np.square(
                                audio_batterie.astype(np.float64))))) if audio_batterie.size else None,
                            base_volume=DEFAULT_TRACK_VOLUME,
                            max_volume=VOLUME_MAX,
                        )
                        if affine is None:
                            print(f"      {nom:8s} : réglage piste non tenté "
                                  f"(la machine ne déclare aucun axe)")
                        else:
                            piste.parameters = dict(affine.parameters)
                            bouges = ", ".join(
                                f"{axe.split('.')[-1]}={valeur:.3g}"
                                for axe, valeur, _ in affine.improvements[-4:]
                            ) or "aucun axe retenu"
                            print(f"      {nom:8s} : réglage piste "
                                  f"{affine.start_distance:.3f} -> {affine.distance:.3f} "
                                  f"({affine.evaluations} évaluations, "
                                  f"{time.perf_counter()-depart_reglage:.0f} s) — {bouges}")

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

                # ARBITRAGE SUR LA PISTE ENTIÈRE. Ce qui précède a choisi une
                # machine d'après UNE note ; ce qui suit la rejuge sur toutes.
                # Les deux critères ne classent pas dans le même ordre, et
                # c'est le second qu'on écoute.
                if not args.sans_arbitrage:
                    depart_arbitrage = time.perf_counter()
                    verdicts = arbitrate_on_track(
                        notes=[ExportNote(n.note, n.velocity, n.start, n.duration)
                               for n in resultat.notes],
                        stem_audio=audio,
                        candidates=build_candidates(list(resultat.patches.items()), candidates),
                        workdir=Path(temporaire) / "arbitrage" / nom,
                        sample_rate=SAMPLE_RATE,
                        metric=args.metrique,
                        tempo=args.tempo,
                        binary=args.moteur,
                        name=nom,
                        stem_rms=float(np.sqrt(np.mean(np.square(
                            audio.astype(np.float64))))) if audio.size else None,
                        base_volume=DEFAULT_TRACK_VOLUME,
                        max_volume=VOLUME_MAX,
                    )
                    if not verdicts:
                        # « aucune retenue » et non « aucun rendu » : le cas le
                        # plus fréquent n'est pas un moteur muet, c'est le
                        # filtre de niveau de `arbitrate_on_track` qui a écarté
                        # TOUTES les candidates -- un stem si fort qu'aucune
                        # machine ne peut l'atteindre. Les deux causes appellent
                        # des gestes opposés ; les confondre coûtait l'enquête.
                        print(f"      {nom:8s} : arbitrage sans verdict (aucune candidate "
                              f"rendue ni retenue au niveau) — la machine de la "
                              f"recherche est conservée")
                    else:
                        gagnant = verdicts[0]
                        resultat.track_distance = gagnant.distance
                        resultat.track_considered = [(v.machine, v.origin, v.distance)
                                                     for v in verdicts]
                        classement = ", ".join(
                            f"{v.machine.split('.')[-1]}={v.distance:.3f}"
                            f"{'*' if v.origin == 'patch d\'usine' else ''}"
                            for v in verdicts[:3]
                        )
                        avant = next((v.distance for v in verdicts
                                      if v.machine == resultat.machine
                                      and v.parameters == resultat.parameters), None)
                        change = (gagnant.machine != resultat.machine
                                  or gagnant.parameters != resultat.parameters)
                        resultat.machine = gagnant.machine
                        resultat.parameters = dict(gagnant.parameters)
                        marque = "CHANGE" if change else "confirme"
                        print(f"      {nom:8s} : arbitrage piste {marque} "
                              f"{gagnant.machine} ({gagnant.origin}) D={gagnant.distance:.3f}"
                              + (f" (la recherche donnait {avant:.3f})" if avant is not None else "")
                              + f" [{time.perf_counter()-depart_arbitrage:.0f} s] — {classement}")
                        print(f"                 (* = patch d'usine)")

                        seconde = close_runner_up(verdicts)
                        if seconde is not None:
                            machines_secondes[nom] = MixAlternative(
                                parameters=dict(seconde.parameters),
                                label=f"machine seconde ({seconde.machine})",
                                machine=seconde.machine,
                                track_distance=seconde.distance)
                            ecart = (seconde.distance - gagnant.distance) / max(1e-9, gagnant.distance)
                            print(f"      {nom:8s} : arbitrage SERRÉ — {seconde.machine} "
                                  f"à {ecart*100:.1f} % ({seconde.distance:.3f}), remise en jeu "
                                  f"au verdict du mélange")

                # RÉGLAGE SUR LA PISTE, ET IL NE DÉPEND PLUS DE L'ARBITRAGE.
                # Les réglages retenus viennent encore d'UNE note (ou de
                # l'usine) ; on les rejuge sur la piste entière, par une
                # descente qui ne peut qu'améliorer son point de départ.
                #
                # CETTE ÉTAPE ÉTAIT IMBRIQUÉE DANS LA PRÉCÉDENTE, et c'était un
                # défaut : écrite sous le `else` de l'arbitrage, elle
                # disparaissait avec lui. `--sans-arbitrage` désactivait donc
                # DEUX étapes, et un stem dont l'arbitrage ne rendait aucun
                # verdict n'était pas réglé non plus. Le README promet
                # l'inverse — « chaque étape se désactive : c'est ainsi qu'on
                # attribue un écart à une étape et non à un ensemble » — et
                # c'est la promesse qui a raison : sans elle, aucune mesure ne
                # peut dire laquelle des deux étapes a produit un gain.
                if not args.sans_reglage_piste:
                    depart_reglage = time.perf_counter()
                    resultat.arbitration_parameters = dict(resultat.parameters)
                    resultat.arbitration_distance = resultat.track_distance
                    affine = refine_patch_on_track(
                        machine=resultat.machine,
                        parameters=resultat.parameters,
                        notes=[ExportNote(n.note, n.velocity, n.start, n.duration)
                               for n in resultat.notes],
                        stem_audio=audio,
                        engine=moteur,
                        workdir=Path(temporaire) / "reglage" / nom,
                        sample_rate=SAMPLE_RATE,
                        budget=args.budget_piste,
                        axes=args.axes_piste,
                        metric=args.metrique,
                        tempo=args.tempo,
                        binary=args.moteur,
                        name=nom,
                        stem_rms=float(np.sqrt(np.mean(np.square(
                            audio.astype(np.float64))))) if audio.size else None,
                        base_volume=DEFAULT_TRACK_VOLUME,
                        max_volume=VOLUME_MAX,
                    )
                    if affine is None:
                        print(f"      {nom:8s} : réglage piste non tenté "
                              f"(la machine ne déclare aucun axe)")
                    else:
                        gain = affine.start_distance - affine.distance
                        resultat.parameters = dict(affine.parameters)
                        resultat.track_distance = affine.distance
                        bouges = ", ".join(
                            f"{axe.split('.')[-1]}={valeur:.3g}"
                            for axe, valeur, _ in affine.improvements[-4:]
                        ) or "aucun axe retenu"
                        print(f"      {nom:8s} : réglage piste "
                              f"{affine.start_distance:.3f} -> {affine.distance:.3f} "
                              f"({'-' if gain > 0 else ''}{abs(gain):.3f}, "
                              f"{affine.evaluations} évaluations, "
                              f"{time.perf_counter()-depart_reglage:.0f} s) — {bouges}")
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

            # LE MÉLANGE A LE DERNIER MOT. Un réglage qui rapproche une piste de
            # son stem peut éloigner le morceau : les stems ne se rendorment pas
            # exactement dans l'original. On ne garde donc que ce qui rapproche
            # ce qu'on écoute, et on DIT ce qui a été écarté.
            distances_retenues: Dict[str, float] = {}
            alternatives: Dict[str, List[MixAlternative]] = {}
            for stem in reconstruits:
                if stem.arbitration_parameters is not None:
                    alternatives.setdefault(stem.name, []).append(
                        MixAlternative(parameters=dict(stem.arbitration_parameters),
                                       label="arbitrage",
                                       track_distance=stem.arbitration_distance))
            for nom_piste, patch in patchs_avant_reglage.items():
                alternatives.setdefault(nom_piste, []).append(
                    MixAlternative(parameters=dict(patch), label="avant réglage"))
            for nom_piste, seconde in machines_secondes.items():
                alternatives.setdefault(nom_piste, []).append(seconde)
            if alternatives:
                decisions = keep_what_helps_the_mix(
                    pistes_export, alternatives, melange, audio_par_stem, sortie,
                    workdir=Path(temporaire) / "verdict",
                    sample_rate=SAMPLE_RATE, metric=args.metrique,
                    tempo=args.tempo, binary=args.moteur)
                distances_retenues = {d.track: d.kept_track_distance
                                      for d in decisions
                                      if d.kept_track_distance is not None}
                for decision in decisions:
                    ecartees = ", ".join(f"{lib} {d:.4f}" for lib, d in decision.rejected)
                    print(f"      {decision.track:8s} : verdict du mélange -> "
                          f"{decision.kept} ({decision.distance_kept:.4f})"
                          + (f" — écartées : {ecartees}" if ecartees else ""))
            # LE RAPPORT DOIT DÉCRIRE LE PROJET QU'ON ÉCRIT, et il ne le
            # faisait plus. `keep_what_helps_the_mix` REMPLACE le dictionnaire
            # de paramètres de la piste (`track.parameters = ...`) au lieu de le
            # modifier ; le `StemReconstruction`, qui partageait l'objet au
            # départ, gardait donc le patch d'AVANT le verdict. Quand le mélange
            # revenait au patch de l'arbitrage, `rapport.json` publiait le patch
            # affiné et sa `trackDistance` : des chiffres pour un réglage absent
            # du projet, c'est-à-dire la pire sorte -- ceux qu'on croit vérifiés.
            #
            # ICI, ET NON APRÈS LA RÉSOLUTION DES DÉFAUTS qui suit : le rapport
            # dit ce que la CHAÎNE a décidé, pas les vingt valeurs d'usine que
            # l'écriture du preset y ajoutera ensuite pour le figer. Noyer trois
            # réglages trouvés dans vingt réglages hérités rendrait le rapport
            # illisible sans rien lui apprendre.
            par_nom = {piste.name: piste for piste in pistes_export}
            for stem in reconstruits:
                piste_finale = par_nom.get(stem.name)
                if piste_finale is None:
                    continue
                stem.machine = piste_finale.machine
                stem.parameters = dict(piste_finale.parameters)
                # ET LA DISTANCE DE PISTE AVEC, sans quoi le rapport publierait
                # le chiffre du patch ÉCARTÉ. Vérifié sur Children v10 : le
                # verdict avait ramené `bass` et `other` au patch de
                # l'arbitrage, et `trackDistance` annonçait encore 0,1986 et
                # 0,2174 -- les scores du réglage que le mélange venait de
                # refuser. Corriger `parameters` sans corriger ce chiffre ne
                # faisait que déplacer le mensonge d'un champ.
                retenue = distances_retenues.get(stem.name)
                if retenue is not None:
                    stem.track_distance = retenue

            # UN PRESET NE DOIT DÉPENDRE DE RIEN. Quand l'arbitrage ou le
            # verdict retiennent un patch d'USINE, le dictionnaire de paramètres
            # est vide : le preset écrit ne dit alors rien, et le son du projet
            # dépend des valeurs par défaut de la machine AU MOMENT OÙ ON
            # L'OUVRE. Le jour où un défaut change, le morceau change sans que
            # rien ne le signale -- exactement la divergence silencieuse que ce
            # projet refuse partout ailleurs. On écrit donc les valeurs
            # RÉSOLUES : mêmes réglages, mêmes sons, mais inscrits.
            for piste in pistes_export:
                if not piste.machine:
                    continue
                try:
                    defauts = {str(d["id"]): float(d["default"])
                               for d in moteur.parameters(piste.machine)}
                except Exception as erreur:
                    print(f"      {piste.name:8s} : paramètres par défaut illisibles "
                          f"({erreur}) — preset écrit tel quel")
                    continue
                defauts.update(piste.parameters)
                piste.parameters = defauts

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
