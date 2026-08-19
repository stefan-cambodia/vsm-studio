from pathlib import Path

import librosa
import numpy as np


def estimate_envelope(y, sr):

    amplitude = np.abs(
        librosa.stft(
            y,
            n_fft=2048,
            hop_length=512,
        )
    )

    envelope = np.mean(
        amplitude,
        axis=0,
    )

    envelope = envelope / (
        np.max(envelope) + 1e-9
    )

    times = librosa.frames_to_time(
        np.arange(len(envelope)),
        sr=sr,
        hop_length=512,
    )

    peak_index = int(
        np.argmax(envelope)
    )

    peak_time = float(
        times[peak_index]
    )

    # Estimation grossière de l'attaque
    threshold_10 = 0.1
    threshold_90 = 0.9

    above_10 = np.where(
        envelope[:peak_index + 1] >= threshold_10
    )[0]

    above_90 = np.where(
        envelope[:peak_index + 1] >= threshold_90
    )[0]

    if len(above_10):
        t10 = times[above_10[0]]
    else:
        t10 = 0.0

    if len(above_90):
        t90 = times[above_90[0]]
    else:
        t90 = peak_time

    attack = max(
        0.001,
        float(t90 - t10)
    )

    return {
        "attack_seconds": attack,
        "peak_time_seconds": peak_time,
    }


def estimate_waveform(y, sr):

    harmonic, percussive = librosa.effects.hpss(y)

    harmonic_energy = np.mean(
        harmonic ** 2
    )

    percussive_energy = np.mean(
        percussive ** 2
    )

    ratio = harmonic_energy / (
        harmonic_energy +
        percussive_energy +
        1e-9
    )

    fft = np.abs(
        librosa.stft(
            harmonic,
            n_fft=4096,
        )
    )

    mean_spectrum = np.mean(
        fft,
        axis=1,
    )

    peaks, _ = librosa.util.find_peaks(
        mean_spectrum,
        distance=5,
    )

    if len(peaks) < 3:

        waveform = "unknown"

    else:

        harmonic_ratio = (
            np.mean(
                mean_spectrum[peaks]
            )
            /
            (np.mean(mean_spectrum) + 1e-9)
        )

        if harmonic_ratio > 3.0:
            waveform = "saw_or_square"

        elif ratio > 0.75:
            waveform = "harmonic_oscillator"

        else:
            waveform = "complex"

    return waveform


def analyze_synth(audio_path: Path):

    y, sr = librosa.load(
        str(audio_path),
        sr=None,
        mono=True,
    )

    envelope = estimate_envelope(
        y,
        sr,
    )

    waveform = estimate_waveform(
        y,
        sr,
    )

    centroid = np.mean(
        librosa.feature.spectral_centroid(
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

    # Estimation du cutoff à partir du centroïde.
    estimated_cutoff = float(
        np.clip(
            centroid * 1.5,
            100,
            18000,
        )
    )

    resonance = float(
        np.clip(
            bandwidth / 8000,
            0,
            1,
        )
    )

    if envelope["attack_seconds"] < 0.03:

        envelope_type = "pluck_or_percussive"

    elif envelope["attack_seconds"] < 0.2:

        envelope_type = "soft_attack"

    else:

        envelope_type = "pad_or_strings"

    return {
        "synth_detected": True,

        "estimated_patch": {
            "oscillator": {
                "waveform": waveform,
                "detune_cents": None,
                "octave": None,
            },

            "filter": {
                "type": "lowpass",
                "cutoff_hz": round(
                    estimated_cutoff,
                    1,
                ),
                "resonance": round(
                    resonance,
                    3,
                ),
            },

            "envelope": {
                "type": envelope_type,
                "attack_seconds": round(
                    envelope["attack_seconds"],
                    4,
                ),
                "decay_seconds": None,
                "sustain": None,
                "release_seconds": None,
            },

            "effects": {
                "distortion": None,
                "chorus": None,
                "delay": None,
                "reverb": None,
            },
        },

        "analysis": {
            "spectral_centroid_hz": float(
                centroid
            ),
            "spectral_bandwidth_hz": float(
                bandwidth
            ),
        },
    }
