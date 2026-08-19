import librosa
import numpy as np


def extract_note_audio(
    y,
    sr,
    start,
    end,
    padding=0.03,
):
    start_sample = max(
        0,
        int(
            (start - padding)
            * sr
        ),
    )

    end_sample = min(
        len(y),
        int(
            (end + padding)
            * sr
        ),
    )

    segment = y[
        start_sample:end_sample
    ]

    return segment


def load_audio(
    path,
    sr=None,
):
    return librosa.load(
        str(path),
        sr=sr,
        mono=True,
    )
