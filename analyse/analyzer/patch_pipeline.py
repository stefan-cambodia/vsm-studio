from pathlib import Path

import librosa

from .note_extraction import (
    extract_notes,
)

from .segments import (
    load_audio,
    extract_note_audio,
)

from .patch_optimizer import (
    optimize_patch,
)


def analyze_synth_stem(
    audio_path: Path,
    max_notes=8,
    max_iterations=25,
):
    """
    Analyse les premières notes suffisamment
    propres du stem et cherche un patch.
    """

    notes = extract_notes(
        audio_path
    )

    if not notes:
        return {
            "error": "Aucune note détectée"
        }

    y, sr = load_audio(
        audio_path,
        sr=44100,
    )

    results = []

    used = 0

    for note in notes:

        if note["confidence"] < 0.75:
            continue

        duration = (
            note["end"]
            -
            note["start"]
        )

        if duration < 0.08:
            continue

        segment = extract_note_audio(
            y,
            sr,
            note["start"],
            note["end"],
        )

        if len(segment) < sr * 0.08:
            continue

        print(
            f"[PATCH] "
            f"MIDI={note['midi']} "
            f"{note['start']:.2f}s"
        )

        fit = optimize_patch(
            segment,
            midi_note=note["midi"],
            sr=sr,
            duration=len(segment) / sr,
            max_iterations=max_iterations,
        )

        results.append({
            "note": note,
            "fit": fit,
        })

        used += 1

        if used >= max_notes:
            break

    return {
        "notes_analyzed": used,
        "fits": results,
    }
