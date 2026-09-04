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


# LES AVERTISSEMENTS DU RENDU DE VARIANTE NE SONT PLUS AVALÉS (CDC multipiste § 12). Le
# moteur les écrit sur la sortie d'erreur même en mode silencieux, et
# `capture_output` les rangeait dans une variable que personne ne lisait :
# « fichier audio introuvable » y a dormi sept campagnes. Chaque message
# distinct est dit UNE fois -- un verdict rend des centaines de variantes, et
# la même phrase cent fois n'est plus lue.
_avertissements_dits: set = set()


def dire_les_avertissements_du_rendu(stderr: Optional[str]) -> None:
    for ligne in (stderr or "").splitlines():
        ligne = ligne.strip()
        if not ligne or ligne in _avertissements_dits:
            continue
        _avertissements_dits.add(ligne)
        print(f"      rendu de variante : {ligne}")


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
        termine = subprocess.run(commande, check=True, capture_output=True, text=True)
        dire_les_avertissements_du_rendu(termine.stderr)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    # Un moteur qui sort en 0 SANS écrire son fichier est un rendu qui a
    # échoué, pas une exception à faire remonter : l'appelant sait dire
    # « rendu vide », et c'est la voie qui nomme la candidate écartée.
    if not sortie.is_file():
        return None
    return read_render_wav(sortie)
