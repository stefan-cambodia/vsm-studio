"""Le tempo du morceau, estimé sur le mélange — pour que la reconstruction ne s'ouvre plus à 120 BPM.

POURQUOI. Jusqu'au 15/09/2026, `reconstruire.py` écrivait `--tempo`, 120 par
défaut, dans TOUT projet reconstruit : les notes tombaient au bon endroit en
secondes (les ticks sont calculés à ce tempo), mais la grille du DAW — mesures,
aimant, quantification, boucle par mesures — ne voulait rien dire sur un morceau
à 128 ou à 96. Un DAW digne de Cubase ouvre un morceau à SON tempo.

CE QUE CE MODULE FAIT. `estimer_tempo` suit les temps par `librosa.beat.beat_track`
sur l'enveloppe d'attaques du mélange et rend le tempo en BPM, l'instant du
premier temps suivi et le nombre de temps ; `bpm` est arrondi au dixième. Le
suivi de temps peut se tromper d'une OCTAVE (deux fois trop vite ou trop lent) :
l'outil `tools/tempo-estime.py` compte ces cas à part, et c'est lui qui juge
l'estimateur contre la vérité du banc synthétique — jamais ce module lui-même.

CE QU'IL NE FAIT PAS. Ni le premier temps de la MESURE (le suivi ne distingue
pas un temps fort), ni les changements de tempo : un seul tempo, comme avant,
mais le bon.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np


@dataclass(frozen=True)
class Tempo:
    bpm: float
    premier_temps_secondes: float
    temps: int

    def json(self) -> dict:
        return {"bpm": self.bpm, "premierTempsSecondes": self.premier_temps_secondes,
                "temps": self.temps, "source": "estime"}


def estimer_tempo(audio: np.ndarray, sample_rate: int) -> Tempo:
    """Le tempo du mélange (mono, flottant), en BPM au dixième."""
    import librosa

    y = np.asarray(audio, dtype=np.float32)
    if y.ndim > 1:
        y = y.mean(axis=1)
    if y.size < sample_rate:   # moins d'une seconde : rien à suivre
        return Tempo(120.0, 0.0, 0)
    attaques = librosa.onset.onset_strength(y=y, sr=sample_rate)
    tempo, temps = librosa.beat.beat_track(onset_envelope=attaques, sr=sample_rate, units="time")
    bpm = float(np.asarray(tempo).flat[0]) if np.ndim(tempo) > 0 else float(tempo)
    if not np.isfinite(bpm) or bpm <= 0.0:
        return Tempo(120.0, 0.0, 0)
    premier = float(temps[0]) if len(temps) else 0.0
    return Tempo(round(bpm, 1), round(premier, 3), int(len(temps)))
