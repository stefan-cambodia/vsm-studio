from pathlib import Path

import librosa
import numpy as np


NOTE_NAMES = [
    "C",
    "C#",
    "D",
    "D#",
    "E",
    "F",
    "F#",
    "G",
    "G#",
    "A",
    "A#",
    "B",
]


def hz_to_note_name(hz: float):

    if hz <= 0:
        return None

    midi = 69 + 12 * np.log2(hz / 440.0)

    midi = int(round(midi))

    octave = midi // 12 - 1

    note = NOTE_NAMES[midi % 12]

    return f"{note}{octave}"


def estimate_key(y, sr):

    chroma = librosa.feature.chroma_cqt(
        y=y,
        sr=sr,
    )

    profile = np.mean(chroma, axis=1)

    major_profile = np.array([
        6.35,
        2.23,
        3.48,
        2.33,
        4.38,
        4.09,
        2.52,
        5.19,
        2.39,
        3.66,
        2.29,
        2.88,
    ])

    minor_profile = np.array([
        6.33,
        2.68,
        3.52,
        5.38,
        2.60,
        3.53,
        2.54,
        4.75,
        3.98,
        2.69,
        3.34,
        3.17,
    ])

    scores_major = []
    scores_minor = []

    for i in range(12):

        scores_major.append(
            np.corrcoef(
                profile,
                np.roll(major_profile, i),
            )[0, 1]
        )

        scores_minor.append(
            np.corrcoef(
                profile,
                np.roll(minor_profile, i),
            )[0, 1]
        )

    major_idx = int(np.nanargmax(scores_major))
    minor_idx = int(np.nanargmax(scores_minor))

    if scores_major[major_idx] >= scores_minor[minor_idx]:

        return {
            "key": NOTE_NAMES[major_idx],
            "mode": "major",
            "confidence": float(scores_major[major_idx]),
        }

    return {
        "key": NOTE_NAMES[minor_idx],
        "mode": "minor",
        "confidence": float(scores_minor[minor_idx]),
    }


def analyze_audio(audio_path: Path):

    print()
    print("[ANALYSIS] Chargement...")

    y, sr = librosa.load(
        str(audio_path),
        sr=None,
        mono=True,
    )

    duration = len(y) / sr

    print(f"[ANALYSIS] durée : {duration:.2f}s")
    print(f"[ANALYSIS] sample rate : {sr}")

    rms = librosa.feature.rms(y=y)[0]

    onset = librosa.onset.onset_strength(
        y=y,
        sr=sr,
    )

    tempo, beats = librosa.beat.beat_track(
        onset_envelope=onset,
        sr=sr,
    )

    if np.ndim(tempo) > 0:
        tempo = float(np.asarray(tempo).flat[0])
    else:
        tempo = float(tempo)

    centroid = librosa.feature.spectral_centroid(
        y=y,
        sr=sr,
    )[0]

    bandwidth = librosa.feature.spectral_bandwidth(
        y=y,
        sr=sr,
    )[0]

    rolloff = librosa.feature.spectral_rolloff(
        y=y,
        sr=sr,
    )[0]

    flatness = librosa.feature.spectral_flatness(
        y=y,
    )[0]

    zcr = librosa.feature.zero_crossing_rate(y)[0]

    key = estimate_key(y, sr)

    return {
        "duration_seconds": round(duration, 3),
        "sample_rate": sr,

        "tempo_bpm": round(tempo, 2),

        "beats": [
            round(float(x), 3)
            for x in librosa.frames_to_time(
                beats,
                sr=sr,
            )
        ],

        "key": key,

        "audio_features": {
            "rms_mean": float(np.mean(rms)),
            "spectral_centroid_hz": float(np.mean(centroid)),
            "spectral_bandwidth_hz": float(np.mean(bandwidth)),
            "spectral_rolloff_hz": float(np.mean(rolloff)),
            "spectral_flatness": float(np.mean(flatness)),
            "zero_crossing_rate": float(np.mean(zcr)),
        },
    }
