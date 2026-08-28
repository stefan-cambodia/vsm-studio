
import numpy as np
from scipy.optimize import differential_evolution

from .synth_engine import (
    SynthParams,
    synthesize_note,
)

from .audio_distance import (
    audio_distance,
)


WAVEFORMS = [
    "sine",
    "triangle",
    "square",
    "saw",
    "pulse",
]


PARAMETER_BOUNDS = [
    (0.1, 1.0),       # osc_level
    (-30, 30),        # detune
    (0.0, 0.8),       # sub

    (100, 12000),     # cutoff
    (0.0, 1.0),       # resonance

    (0.001, 0.5),     # attack
    (0.02, 1.0),       # decay
    (0.1, 1.0),       # sustain
    (0.02, 2.0),      # release

    (0.0, 0.7),       # distortion
    (0.0, 0.3),       # noise
]


def vector_to_params(
    waveform,
    x,
):
    return SynthParams(
        waveform=waveform,

        osc_level=float(x[0]),
        osc_detune_cents=float(x[1]),
        sub_level=float(x[2]),

        filter_cutoff=float(x[3]),
        filter_resonance=float(x[4]),

        attack=float(x[5]),
        decay=float(x[6]),
        sustain=float(x[7]),
        release=float(x[8]),

        distortion=float(x[9]),
        noise=float(x[10]),
    )


def optimize_patch(
    target_audio,
    midi_note,
    sr=44100,
    duration=None,
    max_iterations=40,
):
    """
    Recherche automatiquement le patch
    dont le rendu ressemble le plus au signal cible.
    """

    if duration is None:
        duration = len(target_audio) / sr

    target_audio = target_audio.astype(
        np.float32
    )

    results = []

    for waveform in WAVEFORMS:

        print(
            f"[PATCH] optimisation waveform={waveform}"
        )

        def objective(x, waveform=waveform):

            params = vector_to_params(
                waveform,
                x,
            )

            candidate = synthesize_note(
                midi_note,
                duration,
                params,
                sr,
            )

            return audio_distance(
                target_audio,
                candidate,
                sr,
            )

        result = differential_evolution(
            objective,
            PARAMETER_BOUNDS,

            maxiter=max_iterations,

            popsize=6,

            mutation=(0.5, 1.0),

            recombination=0.7,

            polish=True,

            workers=1,

            updating="immediate",

            seed=42,
        )

        params = vector_to_params(
            waveform,
            result.x,
        )

        results.append({
            "waveform": waveform,
            "loss": float(result.fun),
            "params": params,
        })

    results.sort(
        key=lambda x: x["loss"]
    )

    best = results[0]

    return {
        "best": {
            "waveform": best["waveform"],
            "loss": best["loss"],
            "parameters": best[
                "params"
            ].to_dict(),
        },

        "alternatives": [
            {
                "waveform": r["waveform"],
                "loss": r["loss"],
                "parameters": r[
                    "params"
                ].to_dict(),
            }

            for r in results[1:]
        ],
    }
