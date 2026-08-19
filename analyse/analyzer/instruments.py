from pathlib import Path

import librosa
import numpy as np


def analyze_instrument_characteristics(
    audio_path: Path,
):

    y, sr = librosa.load(
        str(audio_path),
        sr=None,
        mono=True,
    )

    duration = len(y) / sr

    centroid = np.mean(
        librosa.feature.spectral_centroid(
            y=y,
            sr=sr,
        )
    )

    flatness = np.mean(
        librosa.feature.spectral_flatness(
            y=y,
        )
    )

    rolloff = np.mean(
        librosa.feature.spectral_rolloff(
            y=y,
            sr=sr,
        )
    )

    bandwidth = np.mean(
        librosa.feature.spectral_bandwidth(
            y=y,
            sr=sr,
        )
    )

    rms = np.mean(
        librosa.feature.rms(y=y)
    )

    return {
        "duration": float(duration),
        "spectral_centroid": float(centroid),
        "spectral_bandwidth": float(bandwidth),
        "spectral_rolloff": float(rolloff),
        "spectral_flatness": float(flatness),
        "rms": float(rms),
    }


def classify_instrument(features):

    centroid = features["spectral_centroid"]
    flatness = features["spectral_flatness"]
    bandwidth = features["spectral_bandwidth"]

    candidates = []

    # Sons très harmoniques
    if flatness < 0.08:

        candidates.append(
            ("piano_or_keys", 0.55)
        )

        candidates.append(
            ("synth", 0.50)
        )

        candidates.append(
            ("strings", 0.35)
        )

    # Sons très brillants
    if centroid > 3500:

        candidates.append(
            ("synth_lead", 0.65)
        )

        candidates.append(
            ("guitar_or_bright_keys", 0.40)
        )

    # Sons graves
    if centroid < 1200:

        candidates.append(
            ("bass", 0.75)
        )

        candidates.append(
            ("synth_bass", 0.65)
        )

    # Large bande
    if bandwidth > 2500:

        candidates.append(
            ("synth_pad", 0.45)
        )

    if not candidates:

        candidates.append(
            ("unknown", 0.20)
        )

    candidates.sort(
        key=lambda x: x[1],
        reverse=True,
    )

    return [
        {
            "instrument": name,
            "confidence": round(score, 3),
        }
        for name, score in candidates[:5]
    ]


def classify_stems(stems):

    results = []

    for stem_name, path in stems.items():

        features = analyze_instrument_characteristics(
            path
        )

        candidates = classify_instrument(
            features
        )

        results.append({
            "source": stem_name,
            "candidates": candidates,
            "features": features,
        })

    return results
