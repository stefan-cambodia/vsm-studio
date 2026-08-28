#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Audio Analyzer / Synth Patch Reconstructor

Pipeline :

    Audio file / URL
            |
            v
        Input loader
            |
            +----------------------+
            |                      |
            v                      v
      Global analysis           Demucs
                                  |
                 +----------------+----------------+
                 |                |                |
               drums             bass            other
                 |                |                |
                 v                v                v
             Drum AI         Basic Pitch       Basic Pitch
                                  |                |
                                  +-------+--------+
                                          |
                                          v
                                  Synth Patch Fitting
                                          |
                                          v
                                   Final JSON report

Usage :

    python main.py song.mp3

    python main.py "https://example.com/song.mp3"

    python main.py song.mp3 --output results

    python main.py song.mp3 --max-notes 8

    python main.py song.mp3 --no-demucs

    python main.py song.mp3 --no-midi

    python main.py song.mp3 --device cpu
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
from pathlib import Path
from typing import Any, Dict


# ============================================================
# PROJECT IMPORTS
# ============================================================

from analyzer.input import resolve_audio
from analyzer.analysis import analyze_audio
from analyzer.separation import separate_audio
from analyzer.drums import detect_drums
from analyzer.instruments import classify_stems
from analyzer.midi import generate_midi
from analyzer.patch_pipeline import analyze_synth_stem


# ============================================================
# VERSION
# ============================================================

VERSION = "2.0.0"


# ============================================================
# UTILITIES
# ============================================================

def print_header():
    print()
    print("=" * 78)
    print(" AUDIO ANALYZER / SYNTH PATCH RECONSTRUCTOR")
    print(f" Version {VERSION}")
    print("=" * 78)
    print()


def print_section(title: str):
    print()
    print("-" * 78)
    print(f" {title}")
    print("-" * 78)


def safe_float(value, default=None):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def safe_int(value, default=None):
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def json_safe(value):
    """
    Convertit récursivement les objets numpy / Path
    vers des types compatibles JSON.
    """

    if value is None:
        return None

    if isinstance(value, Path):
        return str(value)

    if isinstance(value, (str, int, float, bool)):
        return value

    if isinstance(value, dict):
        return {
            str(k): json_safe(v)
            for k, v in value.items()
        }

    if isinstance(value, (list, tuple)):
        return [
            json_safe(v)
            for v in value
        ]

    # numpy scalar
    if hasattr(value, "item"):
        try:
            return value.item()
        except Exception:
            pass

    # numpy array
    if hasattr(value, "tolist"):
        try:
            return value.tolist()
        except Exception:
            pass

    return str(value)


def save_json(
    data: Dict[str, Any],
    path: Path,
):
    path.parent.mkdir(
        parents=True,
        exist_ok=True,
    )

    with open(
        path,
        "w",
        encoding="utf-8",
    ) as f:

        json.dump(
            json_safe(data),
            f,
            indent=2,
            ensure_ascii=False,
        )


def directory_size(path: Path) -> int:
    """
    Taille approximative d'un répertoire en octets.
    """

    if not path.exists():
        return 0

    total = 0

    for item in path.rglob("*"):

        if item.is_file():

            try:
                total += item.stat().st_size
            except OSError:
                pass

    return total


# ============================================================
# ARGUMENTS
# ============================================================

def build_parser():

    parser = argparse.ArgumentParser(
        prog="audio-analyzer",
        description=(
            "Analyse un morceau audio et tente de reconstruire "
            "les instruments, batteries et patches de synthétiseurs."
        ),
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    # --------------------------------------------------------
    # INPUT
    # --------------------------------------------------------

    parser.add_argument(
        "input",
        help=(
            "Fichier audio local ou URL HTTP/HTTPS "
            "(WAV, MP3, FLAC, OGG, M4A, etc.)"
        ),
    )

    # --------------------------------------------------------
    # OUTPUT
    # --------------------------------------------------------

    parser.add_argument(
        "--output",
        default="output",
        help="Répertoire de sortie.",
    )

    # --------------------------------------------------------
    # DEMUCS
    # --------------------------------------------------------

    parser.add_argument(
        "--model",
        default="htdemucs",
        help="Modèle Demucs à utiliser.",
    )

    parser.add_argument(
        "--no-demucs",
        action="store_true",
        help="Désactive la séparation Demucs.",
    )

    # --------------------------------------------------------
    # MIDI
    # --------------------------------------------------------

    parser.add_argument(
        "--no-midi",
        action="store_true",
        help="Désactive la transcription MIDI globale.",
    )

    # --------------------------------------------------------
    # SYNTH
    # --------------------------------------------------------

    parser.add_argument(
        "--no-synth",
        action="store_true",
        help="Désactive l'optimisation des patches de synthé.",
    )

    parser.add_argument(
        "--max-notes",
        type=int,
        default=8,
        help=(
            "Nombre maximum de notes utilisées par stem "
            "pour la recherche de patch."
        ),
    )

    parser.add_argument(
        "--max-iterations",
        type=int,
        default=25,
        help=(
            "Nombre d'itérations de l'optimiseur "
            "pour chaque waveform."
        ),
    )

    # --------------------------------------------------------
    # DEVICE
    # --------------------------------------------------------

    parser.add_argument(
        "--device",
        choices=[
            "auto",
            "cpu",
            "cuda",
        ],
        default="auto",
        help="Device utilisé lorsque supporté.",
    )

    # --------------------------------------------------------
    # CLEANUP
    # --------------------------------------------------------

    parser.add_argument(
        "--keep-temp",
        action="store_true",
        help="Conserve les fichiers temporaires.",
    )

    parser.add_argument(
        "--clean-output",
        action="store_true",
        help=(
            "Supprime le répertoire de sortie avant "
            "de commencer l'analyse."
        ),
    )

    return parser


# ============================================================
# DEVICE
# ============================================================

def configure_device(device_name: str):

    if device_name == "auto":
        return None

    try:
        import torch

        if device_name == "cuda":

            if not torch.cuda.is_available():
                raise RuntimeError(
                    "CUDA demandé mais aucun GPU CUDA disponible."
                )

            return "cuda"

        return "cpu"

    except ImportError as erreur:

        if device_name == "cuda":
            raise RuntimeError(
                "PyTorch n'est pas installé."
            ) from erreur

        return "cpu"


# ============================================================
# STEM ANALYSIS
# ============================================================

def analyze_drums_stem(
    drums_path: Path,
):

    print_section("DRUM ANALYSIS")

    print(
        f"[DRUMS] Source : {drums_path}"
    )

    try:

        drums = detect_drums(
            drums_path
        )

        for name, events in drums.items():

            print(
                f"  {name:22s} : "
                f"{len(events)} événements"
            )

        return drums

    except Exception as exc:

        print(
            f"[DRUMS] ERREUR : {exc}"
        )

        return {
            "error": str(exc)
        }


def analyze_instrument_stems(
    stems: Dict[str, Path],
):

    print_section("INSTRUMENT CLASSIFICATION")

    try:

        results = classify_stems(
            stems
        )

        for result in results:

            source = result.get(
                "source",
                "unknown",
            )

            candidates = result.get(
                "candidates",
                [],
            )

            if candidates:

                best = candidates[0]

                print(
                    f"  {source:12s} -> "
                    f"{best.get('instrument', 'unknown'):25s} "
                    f"confidence="
                    f"{best.get('confidence', 0):.3f}"
                )

        return results

    except Exception as exc:

        print(
            f"[INSTRUMENTS] ERREUR : {exc}"
        )

        return {
            "error": str(exc)
        }


# ============================================================
# SYNTH ANALYSIS
# ============================================================

def analyze_synth_source(
    name: str,
    path: Path,
    max_notes: int,
    max_iterations: int,
):

    print_section(
        f"SYNTH PATCH FITTING : {name.upper()}"
    )

    print(
        f"[SYNTH] Source : {path}"
    )

    print(
        f"[SYNTH] max notes      : {max_notes}"
    )

    print(
        f"[SYNTH] max iterations : {max_iterations}"
    )

    started = time.perf_counter()

    try:

        result = analyze_synth_stem(
            path,
            max_notes=max_notes,
            max_iterations=max_iterations,
        )

        elapsed = (
            time.perf_counter()
            -
            started
        )

        print(
            f"[SYNTH] terminé en "
            f"{elapsed:.1f}s"
        )

        # Résumé des patches
        fits = result.get(
            "fits",
            [],
        )

        print(
            f"[SYNTH] notes analysées : "
            f"{len(fits)}"
        )

        for index, fit in enumerate(
            fits,
            start=1,
        ):

            note = fit.get(
                "note",
                {},
            )

            patch = fit.get(
                "fit",
                {},
            )

            best = patch.get(
                "best",
                {},
            )

            waveform = best.get(
                "waveform",
                "unknown",
            )

            loss = safe_float(
                best.get("loss")
            )

            midi = note.get(
                "midi",
                "?",
            )

            print(
                f"    #{index:<2} "
                f"MIDI={midi:<3} "
                f"waveform={waveform:<10} "
                f"loss={loss:.5f}"
                if loss is not None
                else
                f"    #{index:<2} "
                f"MIDI={midi:<3} "
                f"waveform={waveform}"
            )

        return result

    except Exception as exc:

        print(
            f"[SYNTH] ERREUR : {exc}"
        )

        return {
            "error": str(exc),
            "source": str(path),
        }


# ============================================================
# PATCH AGGREGATION
# ============================================================

def aggregate_patch_results(
    synth_results,
):
    """
    Agrège les patches obtenus sur les différentes notes.

    Pour l'instant on calcule une moyenne simple des paramètres
    numériques. Une future version pourra utiliser une véritable
    optimisation globale.
    """

    aggregated = []

    for source_result in synth_results:

        source = source_result.get(
            "source",
            "unknown",
        )

        analysis = source_result.get(
            "analysis",
            {},
        )

        fits = analysis.get(
            "fits",
            [],
        )

        if not fits:
            continue

        candidates = []

        for fit in fits:

            best = (
                fit
                .get("fit", {})
                .get("best", {})
            )

            if best:
                candidates.append(best)

        if not candidates:
            continue

        # ----------------------------------------------------
        # Waveform majoritaire
        # ----------------------------------------------------

        waveform_counts = {}

        for candidate in candidates:

            waveform = candidate.get(
                "waveform",
                "unknown",
            )

            waveform_counts[waveform] = (
                waveform_counts.get(
                    waveform,
                    0,
                )
                + 1
            )

        waveform = max(
            waveform_counts,
            key=waveform_counts.get,
        )

        # ----------------------------------------------------
        # Paramètres numériques
        # ----------------------------------------------------

        parameter_names = [
            "osc_level",
            "osc_detune_cents",
            "sub_level",
            "filter_cutoff",
            "filter_resonance",
            "attack",
            "decay",
            "sustain",
            "release",
            "distortion",
            "noise",
            "chorus_mix",
            "delay_mix",
            "reverb_mix",
        ]

        averaged = {}

        for parameter in parameter_names:

            values = []

            for candidate in candidates:

                params = candidate.get(
                    "parameters",
                    {},
                )

                value = params.get(
                    parameter
                )

                if isinstance(
                    value,
                    (int, float),
                ):

                    values.append(
                        float(value)
                    )

            if values:

                averaged[parameter] = (
                    sum(values)
                    /
                    len(values)
                )

        # ----------------------------------------------------
        # Loss
        # ----------------------------------------------------

        losses = []

        for candidate in candidates:

            loss = candidate.get(
                "loss"
            )

            if isinstance(
                loss,
                (int, float),
            ):

                losses.append(
                    float(loss)
                )

        mean_loss = (
            sum(losses) / len(losses)
            if losses
            else None
        )

        aggregated.append({

            "source": source,

            "confidence": (
                max(
                    0.0,
                    min(
                        1.0,
                        1.0 - mean_loss,
                    ),
                )
                if mean_loss is not None
                else None
            ),

            "waveform": waveform,

            "waveform_votes": waveform_counts,

            "mean_reconstruction_loss": mean_loss,

            "parameters": averaged,

            "notes_used": len(
                candidates
            ),
        })

    return aggregated


# ============================================================
# FINAL SUMMARY
# ============================================================

def print_summary(
    report: Dict[str, Any],
):

    print_section("FINAL RESULT")

    track = report.get(
        "track",
        {},
    )

    duration = track.get(
        "duration_seconds"
    )

    bpm = track.get(
        "tempo_bpm"
    )

    key = track.get(
        "key",
        {},
    )

    print(
        f"Duration : "
        f"{duration:.2f} s"
        if isinstance(
            duration,
            (int, float),
        )
        else
        "Duration : unknown"
    )

    print(
        f"BPM      : "
        f"{bpm:.2f}"
        if isinstance(
            bpm,
            (int, float),
        )
        else
        "BPM      : unknown"
    )

    if key:

        print(
            f"Key      : "
            f"{key.get('key', '?')} "
            f"{key.get('mode', '')}"
        )

        confidence = key.get(
            "confidence"
        )

        if isinstance(
            confidence,
            (int, float),
        ):

            print(
                f"Key confidence : "
                f"{confidence:.3f}"
            )

    # --------------------------------------------------------
    # INSTRUMENTS
    # --------------------------------------------------------

    print()

    print(
        "Instrument sources:"
    )

    instruments = report.get(
        "instruments",
        [],
    )

    if isinstance(
        instruments,
        list,
    ):

        for item in instruments:

            source = item.get(
                "source",
                "?",
            )

            candidates = item.get(
                "candidates",
                [],
            )

            if candidates:

                best = candidates[0]

                print(
                    f"  {source:12s} "
                    f"-> "
                    f"{best.get('instrument', '?'):<25s} "
                    f"{best.get('confidence', 0):.3f}"
                )

    # --------------------------------------------------------
    # DRUMS
    # --------------------------------------------------------

    print()

    print(
        "Drums:"
    )

    drums = report.get(
        "drums",
        {},
    )

    if isinstance(
        drums,
        dict,
    ):

        for name, events in drums.items():

            if isinstance(
                events,
                list,
            ):

                print(
                    f"  {name:22s} "
                    f"{len(events)}"
                )

    # --------------------------------------------------------
    # SYNTHS
    # --------------------------------------------------------

    print()

    print(
        "Synth patches:"
    )

    patches = report.get(
        "aggregated_patches",
        [],
    )

    if patches:

        for patch in patches:

            source = patch.get(
                "source",
                "?",
            )

            waveform = patch.get(
                "waveform",
                "?",
            )

            loss = patch.get(
                "mean_reconstruction_loss"
            )

            params = patch.get(
                "parameters",
                {},
            )

            print()

            print(
                f"  [{source}]"
            )

            print(
                f"    waveform : "
                f"{waveform}"
            )

            if loss is not None:

                print(
                    f"    loss     : "
                    f"{loss:.5f}"
                )

            if "filter_cutoff" in params:

                print(
                    f"    cutoff   : "
                    f"{params['filter_cutoff']:.1f} Hz"
                )

            if "filter_resonance" in params:

                print(
                    f"    resonance: "
                    f"{params['filter_resonance']:.3f}"
                )

            if "attack" in params:

                print(
                    f"    attack   : "
                    f"{params['attack'] * 1000:.1f} ms"
                )

            if "decay" in params:

                print(
                    f"    decay    : "
                    f"{params['decay'] * 1000:.1f} ms"
                )

            if "sustain" in params:

                print(
                    f"    sustain  : "
                    f"{params['sustain']:.3f}"
                )

            if "release" in params:

                print(
                    f"    release  : "
                    f"{params['release'] * 1000:.1f} ms"
                )

    else:

        print(
            "  Aucun patch reconstruit."
        )


# ============================================================
# MAIN PIPELINE
# ============================================================

def run(args):

    print_header()

    started = time.perf_counter()

    # --------------------------------------------------------
    # OUTPUT
    # --------------------------------------------------------

    output_dir = Path(
        args.output
    ).expanduser().resolve()

    if args.clean_output:

        if output_dir.exists():

            print(
                f"[OUTPUT] Suppression : "
                f"{output_dir}"
            )

            shutil.rmtree(
                output_dir
            )

    output_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    temp_dir = (
        output_dir /
        "temp"
    )

    stems_dir = (
        output_dir /
        "stems"
    )

    midi_dir = (
        output_dir /
        "midi"
    )

    patches_dir = (
        output_dir /
        "patches"
    )

    patches_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    # --------------------------------------------------------
    # DEVICE
    # --------------------------------------------------------

    try:

        device = configure_device(
            args.device
        )

    except Exception as exc:

        print(
            f"[DEVICE] ERREUR : {exc}"
        )

        return 1

    if device:

        print(
            f"[DEVICE] {device}"
        )

    # --------------------------------------------------------
    # INPUT
    # --------------------------------------------------------

    print_section("INPUT")

    try:

        audio_path = resolve_audio(
            args.input,
            temp_dir,
        )

    except Exception as exc:

        print(
            f"[INPUT] ERREUR : {exc}"
        )

        return 1

    print(
        f"[INPUT] {audio_path}"
    )

    # --------------------------------------------------------
    # GLOBAL ANALYSIS
    # --------------------------------------------------------

    print_section("GLOBAL AUDIO ANALYSIS")

    try:

        global_analysis = analyze_audio(
            audio_path
        )

        print(
            f"[AUDIO] duration : "
            f"{global_analysis.get('duration_seconds', '?')} s"
        )

        print(
            f"[AUDIO] BPM      : "
            f"{global_analysis.get('tempo_bpm', '?')}"
        )

        key = global_analysis.get(
            "key",
            {},
        )

        print(
            f"[AUDIO] key      : "
            f"{key.get('key', '?')} "
            f"{key.get('mode', '')}"
        )

    except Exception as exc:

        print(
            f"[AUDIO] ERREUR : {exc}"
        )

        global_analysis = {
            "error": str(exc)
        }

    # --------------------------------------------------------
    # DEMUCS
    # --------------------------------------------------------

    stems = {}

    if args.no_demucs:

        print_section(
            "DEMUCS - DISABLED"
        )

    else:

        print_section(
            "SOURCE SEPARATION"
        )

        try:

            stems = separate_audio(
                audio_path,
                stems_dir,
                model_name=args.model,
            )

            print()

            print(
                "[DEMUCS] Stems disponibles :"
            )

            for name, path in stems.items():

                print(
                    f"  {name:12s} -> {path}"
                )

        except Exception as exc:

            print(
                f"[DEMUCS] ERREUR : {exc}"
            )

            print(
                "[DEMUCS] Le pipeline "
                "continue avec le morceau original."
            )

            stems = {}

    # --------------------------------------------------------
    # INSTRUMENT CLASSIFICATION
    # --------------------------------------------------------

    instrument_results = []

    if stems:

        instrument_results = (
            analyze_instrument_stems(
                stems
            )
        )

    # --------------------------------------------------------
    # DRUMS
    # --------------------------------------------------------

    if "drums" in stems:

        drum_results = (
            analyze_drums_stem(
                stems["drums"]
            )
        )

    else:

        drum_results = {}

        print_section(
            "DRUM ANALYSIS"
        )

        print(
            "[DRUMS] Aucun stem drums disponible."
        )

    # --------------------------------------------------------
    # MIDI GLOBAL
    # --------------------------------------------------------

    midi_results = None

    if args.no_midi:

        print_section(
            "GLOBAL MIDI - DISABLED"
        )

    else:

        print_section(
            "GLOBAL MIDI TRANSCRIPTION"
        )

        try:

            midi_results = (
                generate_midi(
                    audio_path,
                    midi_dir,
                )
            )

            print(
                f"[MIDI] "
                f"{midi_results}"
            )

        except Exception as exc:

            print(
                f"[MIDI] ERREUR : {exc}"
            )

            midi_results = {
                "error": str(exc)
            }

    # --------------------------------------------------------
    # SYNTH PATCH FITTING
    # --------------------------------------------------------

    synth_results = []

    if args.no_synth:

        print_section(
            "SYNTH PATCH FITTING - DISABLED"
        )

    else:

        synth_candidates = []

        # Bass
        if "bass" in stems:

            synth_candidates.append(
                (
                    "bass",
                    stems["bass"],
                )
            )

        # Other
        if "other" in stems:

            synth_candidates.append(
                (
                    "other",
                    stems["other"],
                )
            )

        if not synth_candidates:

            print_section(
                "SYNTH PATCH FITTING"
            )

            print(
                "[SYNTH] Aucun stem adapté."
            )

        else:

            for (
                source_name,
                source_path,
            ) in synth_candidates:

                result = analyze_synth_source(
                    source_name,
                    source_path,
                    max_notes=args.max_notes,
                    max_iterations=args.max_iterations,
                )

                synth_results.append({

                    "source": source_name,

                    "audio_path": str(
                        source_path
                    ),

                    "analysis": result,
                })

    # --------------------------------------------------------
    # AGGREGATE PATCHES
    # --------------------------------------------------------

    print_section(
        "PATCH AGGREGATION"
    )

    aggregated_patches = (
        aggregate_patch_results(
            synth_results
        )
    )

    print(
        f"[PATCH] "
        f"{len(aggregated_patches)} "
        f"patch(es) agrégé(s)."
    )

    # --------------------------------------------------------
    # FINAL REPORT
    # --------------------------------------------------------

    total_time = (
        time.perf_counter()
        -
        started
    )

    report = {

        "application": {
            "name": (
                "Audio Analyzer / "
                "Synth Patch Reconstructor"
            ),
            "version": VERSION,
        },

        "analysis": {
            "duration_seconds": round(
                total_time,
                3,
            ),
        },

        "input": {
            "source": args.input,
            "resolved_path": str(
                audio_path
            ),
        },

        "configuration": {
            "demucs": not args.no_demucs,
            "demucs_model": args.model,

            "midi": not args.no_midi,

            "synth_fitting": (
                not args.no_synth
            ),

            "max_notes": args.max_notes,

            "max_iterations": (
                args.max_iterations
            ),

            "device": args.device,
        },

        "track": global_analysis,

        "stems": {
            name: str(path)
            for name, path
            in stems.items()
        },

        "instruments": instrument_results,

        "drums": drum_results,

        "midi": midi_results,

        "synthesizers": synth_results,

        "aggregated_patches": (
            aggregated_patches
        ),
    }

    report_path = (
        output_dir /
        "analysis.json"
    )

    print_section(
        "SAVE REPORT"
    )

    try:

        save_json(
            report,
            report_path,
        )

        print(
            f"[REPORT] {report_path}"
        )

    except Exception as exc:

        print(
            f"[REPORT] ERREUR : {exc}"
        )

        return 1

    # --------------------------------------------------------
    # SUMMARY
    # --------------------------------------------------------

    print_summary(
        report
    )

    # --------------------------------------------------------
    # OUTPUT INFO
    # --------------------------------------------------------

    print_section(
        "OUTPUT"
    )

    print(
        f"Report : {report_path}"
    )

    if stems_dir.exists():

        print(
            f"Stems  : {stems_dir}"
        )

    if midi_dir.exists():

        print(
            f"MIDI   : {midi_dir}"
        )

    if patches_dir.exists():

        print(
            f"Patches: {patches_dir}"
        )

    size = directory_size(
        output_dir
    )

    print(
        f"Size   : "
        f"{size / (1024 * 1024):.2f} MB"
    )

    # --------------------------------------------------------
    # CLEAN TEMP
    # --------------------------------------------------------

    if not args.keep_temp:

        if temp_dir.exists():

            print()

            print(
                "[CLEANUP] Suppression "
                "des fichiers temporaires..."
            )

            try:

                shutil.rmtree(
                    temp_dir
                )

            except Exception as exc:

                print(
                    f"[CLEANUP] "
                    f"Impossible : {exc}"
                )

    # --------------------------------------------------------
    # END
    # --------------------------------------------------------

    print()
    print("=" * 78)

    print(
        f" Analyse terminée en "
        f"{total_time:.1f} secondes."
    )

    print(
        f" Résultat : {report_path}"
    )

    print("=" * 78)
    print()

    return 0


# ============================================================
# ENTRY POINT
# ============================================================

def main():

    parser = build_parser()

    args = parser.parse_args()

    try:

        return run(args)

    except KeyboardInterrupt:

        print()
        print(
            "\n[STOP] Analyse interrompue."
        )

        return 130

    except Exception as exc:

        print()
        print(
            "[FATAL] "
            f"{type(exc).__name__}: {exc}"
        )

        return 1


if __name__ == "__main__":

    sys.exit(
        main()
    )
