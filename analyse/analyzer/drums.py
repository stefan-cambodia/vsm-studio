from pathlib import Path

import librosa
import numpy as np


def detect_drums(audio_path: Path):

    y, sr = librosa.load(
        str(audio_path),
        sr=None,
        mono=True,
    )

    onset_env = librosa.onset.onset_strength(
        y=y,
        sr=sr,
    )

    peaks = librosa.util.peak_pick(
        onset_env,
        pre_max=3,
        post_max=3,
        pre_avg=3,
        post_avg=5,
        delta=0.2,
        wait=5,
    )

    stft = np.abs(
        librosa.stft(
            y,
            n_fft=2048,
            hop_length=512,
        )
    )

    freqs = librosa.fft_frequencies(
        sr=sr,
        n_fft=2048,
    )

    result = {
        "kick": [],
        "snare": [],
        "hihat": [],
        "other_percussion": [],
    }

    for frame in peaks:

        spectrum = stft[:, frame]

        low = np.mean(
            spectrum[
                (freqs >= 40) &
                (freqs < 180)
            ]
        )

        mid = np.mean(
            spectrum[
                (freqs >= 180) &
                (freqs < 4000)
            ]
        )

        high = np.mean(
            spectrum[
                (freqs >= 5000) &
                (freqs < 14000)
            ]
        )

        total = low + mid + high + 1e-9

        time = float(
            librosa.frames_to_time(
                frame,
                sr=sr,
            )
        )

        low_ratio = low / total
        mid_ratio = mid / total
        high_ratio = high / total

        if low_ratio > 0.55:

            drum_type = "kick"

        elif high_ratio > 0.45:

            drum_type = "hihat"

        elif mid_ratio > 0.50:

            drum_type = "snare"

        else:

            drum_type = "other_percussion"

        result[drum_type].append(
            round(time, 3)
        )

    return result
