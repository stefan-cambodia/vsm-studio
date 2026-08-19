import numpy as np
import librosa


def normalize(x):
    x = np.asarray(x)

    x = x - np.mean(x)

    std = np.std(x)

    if std < 1e-9:
        return x

    return x / std


def spectral_features(
    y,
    sr,
):
    stft = np.abs(
        librosa.stft(
            y,
            n_fft=2048,
            hop_length=512,
        )
    )

    log_spec = librosa.amplitude_to_db(
        stft + 1e-8,
        ref=np.max,
    )

    centroid = np.mean(
        librosa.feature.spectral_centroid(
            S=stft,
            sr=sr,
        )
    )

    bandwidth = np.mean(
        librosa.feature.spectral_bandwidth(
            S=stft,
            sr=sr,
        )
    )

    rolloff = np.mean(
        librosa.feature.spectral_rolloff(
            S=stft,
            sr=sr,
        )
    )

    flatness = np.mean(
        librosa.feature.spectral_flatness(
            S=stft,
        )
    )

    mfcc = librosa.feature.mfcc(
        S=log_spec,
        sr=sr,
        n_mfcc=13,
    )

    return {
        "centroid": centroid,
        "bandwidth": bandwidth,
        "rolloff": rolloff,
        "flatness": flatness,
        "mfcc": np.mean(
            mfcc,
            axis=1,
        ),
    }


def envelope(y):
    rms = librosa.feature.rms(
        y=y,
        frame_length=2048,
        hop_length=512,
    )[0]

    return normalize(rms)


def audio_distance(
    target,
    candidate,
    sr,
):
    """
    Distance perceptuelle approximative.
    Plus petit = meilleur.
    """

    min_len = min(
        len(target),
        len(candidate),
    )

    target = target[:min_len]
    candidate = candidate[:min_len]

    target_features = spectral_features(
        target,
        sr,
    )

    candidate_features = spectral_features(
        candidate,
        sr,
    )

    # Spectre
    centroid_error = abs(
        target_features["centroid"]
        -
        candidate_features["centroid"]
    )

    bandwidth_error = abs(
        target_features["bandwidth"]
        -
        candidate_features["bandwidth"]
    )

    rolloff_error = abs(
        target_features["rolloff"]
        -
        candidate_features["rolloff"]
    )

    flatness_error = abs(
        target_features["flatness"]
        -
        candidate_features["flatness"]
    )

    # MFCC
    mfcc_error = np.mean(
        np.abs(
            target_features["mfcc"]
            -
            candidate_features["mfcc"]
        )
    )

    # Enveloppe
    env_a = envelope(target)
    env_b = envelope(candidate)

    m = min(
        len(env_a),
        len(env_b),
    )

    envelope_error = np.mean(
        np.abs(
            env_a[:m]
            -
            env_b[:m]
        )
    )

    # Normalisation
    centroid_error /= 4000.0
    bandwidth_error /= 4000.0
    rolloff_error /= 8000.0

    distance = (
        0.20 * centroid_error
        +
        0.15 * bandwidth_error
        +
        0.15 * rolloff_error
        +
        0.10 * flatness_error
        +
        0.25 * mfcc_error
        +
        0.15 * envelope_error
    )

    return float(distance)
