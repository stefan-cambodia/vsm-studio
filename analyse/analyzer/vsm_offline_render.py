# -*- coding: utf-8 -*-
"""
Rendre UNE piste par le vrai moteur hors ligne, et la relire en mono.

Ce petit module existe parce que deux étapes en ont besoin et qu'elles doivent
rendre EXACTEMENT de la même façon : l'épreuve de l'automation de coupure
(`vsm_automation`) et l'arbitrage de machine sur la piste entière
(`vsm_track_arbitration`). Deux copies de ce code divergeraient un jour --
l'une avec la queue de réverbération, l'autre sans -- et les deux mesures
cesseraient silencieusement de se comparer.
"""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Optional

import numpy as np

from .vsm_engine import find_vsm_render
from .vsm_project_export import ExportTrack, write_project_bundle


def read_render_wav(path: Path) -> Optional[np.ndarray]:
    """Relit un WAV écrit par `vsm-render` (float32 stéréo), rendu en mono."""
    donnees = Path(path).read_bytes()
    position, data = 12, None
    while position < len(donnees) - 8:
        bloc = donnees[position:position + 4]
        taille = int.from_bytes(donnees[position + 4:position + 8], "little")
        if bloc == b"data":
            data = donnees[position + 8:position + 8 + taille]
            break
        position += 8 + taille + (taille & 1)
    if data is None:
        return None
    stereo = np.frombuffer(data, dtype="<f4")
    return stereo.reshape(-1, 2).mean(axis=1).astype(np.float32)


def render_track_offline(
    track: ExportTrack,
    folder: Path,
    sample_rate: int,
    duration: Optional[float] = None,
    tempo: float = 120.0,
    binary: Optional[str] = None,
    title: str = "rendu-piste",
) -> Optional[np.ndarray]:
    """
    Écrit un projet d'UNE piste, le rend, et renvoie le mono.

    Renvoie None si le rendu échoue -- ce qui est DIT par l'appelant plutôt que
    remplacé par du silence, qui passerait pour une machine muette et fausserait
    toute comparaison.
    """
    write_project_bundle([track], folder, title=title, tempo=tempo)
    sortie = Path(folder) / "rendu.wav"
    commande = [str(find_vsm_render(binary)), str(folder), str(sortie),
                "--sample-rate", str(sample_rate)]
    if duration is not None:
        commande += ["--duration", str(duration)]
    commande.append("--quiet")
    try:
        subprocess.run(commande, check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    return read_render_wav(sortie)
