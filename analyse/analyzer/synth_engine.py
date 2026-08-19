from dataclasses import dataclass, asdict
from typing import Dict
import math

import numpy as np
from scipy import signal


@dataclass
class SynthParams:
    waveform: str = "saw"

    osc_level: float = 0.8
    osc_detune_cents: float = 0.0
    sub_level: float = 0.0

    filter_cutoff: float = 4000.0
    filter_resonance: float = 0.15

    attack: float = 0.01
    decay: float = 0.2
    sustain: float = 0.7
    release: float = 0.2

    distortion: float = 0.0
    noise: float = 0.0

    chorus_mix: float = 0.0
    delay_mix: float = 0.0
    reverb_mix: float = 0.0

    def to_dict(self):
        return asdict(self)


def midi_to_hz(midi):
    return 440.0 * 2.0 ** ((midi - 69.0) / 12.0)


def oscillator(
    waveform,
    phase,
):
    """
    phase : radians
    """

    cycles = phase / (2.0 * np.pi)

    if waveform == "sine":
        return np.sin(phase)

    if waveform == "square":
        return signal.square(phase)

    if waveform == "triangle":
        return signal.sawtooth(
            phase,
            width=0.5,
        )

    if waveform == "saw":
        return signal.sawtooth(
            phase,
            width=1.0,
        )

    if waveform == "pulse":
        return signal.square(
            phase,
            duty=0.25,
        )

    return np.sin(phase)


def adsr(
    length,
    sr,
    attack,
    decay,
    sustain,
    release,
):
    """
    Génère une enveloppe ADSR.
    """

    total_time = length / sr

    attack = max(0.001, attack)
    decay = max(0.001, decay)
    release = max(0.001, release)

    attack_n = int(attack * sr)
    decay_n = int(decay * sr)
    release_n = int(release * sr)

    if attack_n + decay_n + release_n >= length:
        scale = length / (
            attack_n +
            decay_n +
            release_n +
            1
        )

        attack_n = int(attack_n * scale)
        decay_n = int(decay_n * scale)
        release_n = int(release_n * scale)

    sustain_n = max(
        0,
        length -
        attack_n -
        decay_n -
        release_n,
    )

    a = np.linspace(
        0.0,
        1.0,
        max(1, attack_n),
        endpoint=False,
    )

    d = np.linspace(
        1.0,
        sustain,
        max(1, decay_n),
        endpoint=False,
    )

    s = np.full(
        max(1, sustain_n),
        sustain,
    )

    r = np.linspace(
        sustain,
        0.0,
        max(1, release_n),
    )

    envelope = np.concatenate(
        [a, d, s, r]
    )

    return envelope[:length]


def lowpass(
    audio,
    cutoff,
    sr,
    resonance,
):
    """
    Low-pass Butterworth + résonance approximative.
    """

    cutoff = float(
        np.clip(
            cutoff,
            30.0,
            sr * 0.45,
        )
    )

    q = 0.5 + resonance * 12.0

    b, a = signal.iirfilter(
        N=2,
        Wn=cutoff,
        rs=None,
        btype="lowpass",
        ftype="butter",
        fs=sr,
    )

    return signal.lfilter(
        b,
        a,
        audio,
    )


def soft_clip(audio, amount):
    """
    Distorsion douce.
    """

    if amount <= 0:
        return audio

    drive = 1.0 + amount * 12.0

    return np.tanh(
        audio * drive
    ) / np.tanh(drive)


def synthesize_note(
    midi_note: int,
    duration: float,
    params: SynthParams,
    sr: int = 44100,
):
    """
    Génère une note synthétique à partir
    d'un patch interprétable.
    """

    n = max(
        1,
        int(duration * sr),
    )

    t = np.arange(n) / sr

    f = midi_to_hz(
        midi_note
    )

    detune_ratio = (
        2.0 **
        (params.osc_detune_cents / 1200.0)
    )

    f2 = f * detune_ratio

    phase1 = (
        2.0 *
        np.pi *
        f *
        t
    )

    phase2 = (
        2.0 *
        np.pi *
        f2 *
        t
    )

    osc1 = oscillator(
        params.waveform,
        phase1,
    )

    osc2 = oscillator(
        params.waveform,
        phase2,
    )

    audio = (
        osc1 * params.osc_level
        +
        osc2 * params.osc_level * 0.5
    )

    if params.sub_level > 0:

        sub = np.sin(
            2.0 *
            np.pi *
            f *
            0.5 *
            t
        )

        audio += (
            sub *
            params.sub_level
        )

    if params.noise > 0:

        noise = np.random.randn(n)

        audio += (
            noise *
            params.noise
        )

    envelope = adsr(
        n,
        sr,
        params.attack,
        params.decay,
        params.sustain,
        params.release,
    )

    audio *= envelope

    audio = lowpass(
        audio,
        params.filter_cutoff,
        sr,
        params.filter_resonance,
    )

    audio = soft_clip(
        audio,
        params.distortion,
    )

    peak = np.max(
        np.abs(audio)
    )

    if peak > 1e-8:
        audio /= peak

    return audio.astype(
        np.float32
    )
