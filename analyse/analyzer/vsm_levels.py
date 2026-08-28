"""
Calage des VOLUMES de pistes sur l'équilibre de l'enregistrement d'origine.

POURQUOI CE MODULE EXISTE. Chaque piste exportée partait avec un volume fixe
de 0,9 -- un choix d'amorçage, jamais une mesure. Tant que la reconstruction
était pauvre (une batterie d'une seule pièce), le déséquilibre passait
inaperçu ; dès qu'elle est devenue dense, il est devenu LE premier écart
audible : chaque stem était plus proche de son original, et le MÉLANGE plus
loin (mesuré sur House Of God : distances par stem toutes en baisse, distance
globale en hausse).

LA MÉTHODE, la même que partout ailleurs : rendre chaque piste SEULE par le
vrai moteur (`vsm-render` sur un mini-projet), mesurer son niveau efficace,
le comparer à celui de SON stem -- les stems séparés portent l'équilibre réel
du morceau -- et corriger le volume du rapport des deux. Aucun réglage à
l'oreille, aucune constante au jugé : le volume devient une mesure.
"""

from __future__ import annotations

import shutil
import tempfile
from pathlib import Path
from typing import Dict, List, Sequence

import numpy as np

from .vsm_automation import _render_track
from .vsm_project_export import ExportTrack

# Bornes du volume corrigé. Le mixeur applique un gain linéaire sans borne,
# mais un rapport extrême dit parfois « le rendu est quasi muet » (échec
# silencieux quelque part) plutôt que « il faut amplifier sept fois » -- on
# borne, et on le DIT dans le rapport.
#
# LE PLAFOND EST PASSÉ DE 2,5 À 10, ET LA MESURE L'A IMPOSÉ. Sur *Children
# (Dream Version)*, la basse demande un facteur 6,7 : la machine qui la joue
# (`vsm.piano`, retenue par l'arbitrage sur la piste) est simplement une
# machine peu bruyante dans le grave, pas un rendu raté -- le vrai cas
# « rendu muet » est déjà attrapé par SILENT_RMS, plus bas, qui est le garde
# dont on a besoin. Bornée à 2,5, la piste restait 2,7 fois trop faible :
#
# | volume de la basse | distance globale (v2) |
# |---|---|
# | 2,5 (le plafond d'avant) | 0,2608 |
# | 4 | 0,2443 |
# | 5 | 0,2355 |
# | 6,7 (le facteur demandé) | 0,2246 |
#
# Le plafond coûtait donc 14 % de distance sur le morceau entier. Et il faut
# dire l'envers : avec la basse PRÉCÉDENTE (`vsm.generic`, timbre faux), le
# même facteur DÉGRADAIT le résultat (0,2672 -> 0,2792). Le plafond compensait
# un mauvais patch au lieu de le corriger -- une piste juste veut son niveau,
# une piste fausse veut être refaite.
#
# CE QUE ÇA COÛTE : 840 échantillons écrêtés sur 10,25 millions (0,008 % de la
# piste) contre 22 auparavant. C'est écrit ici plutôt que découvert plus tard ;
# la parade, si cela devient audible, est de baisser le master, pas de remettre
# les pistes au mauvais niveau les unes par rapport aux autres.
VOLUME_MIN = 0.02
VOLUME_MAX = 10.0

# En deçà de ce niveau efficace, un rendu est considéré muet : caler un volume
# sur du silence amplifierait du bruit, pas de la musique.
SILENT_RMS = 1e-5


def _rms(audio: np.ndarray) -> float:
    if audio.size == 0:
        return 0.0
    return float(np.sqrt(np.mean(np.square(audio.astype(np.float64)))))


def match_track_levels(
    tracks: Sequence[ExportTrack],
    stems_audio: Dict[str, np.ndarray],
    samples_root: Path,
    sample_rate: int,
) -> List[str]:
    """
    Cale le volume de chaque piste sur le niveau de son stem. EN PLACE.

    Renvoie une ligne de rapport par piste -- calée, bornée ou sautée, avec
    son motif : un calage silencieux serait invérifiable.
    """
    rapports: List[str] = []
    for track in tracks:
        stem = stems_audio.get(track.name)
        if stem is None:
            rapports.append(f"{track.name} : volume non calé (pas de stem de référence)")
            continue
        if not track.machine or not track.notes:
            rapports.append(f"{track.name} : volume non calé (piste sans machine ou sans note)")
            continue

        duree = stem.size / float(sample_rate)
        with tempfile.TemporaryDirectory(prefix="vsm-niveau-") as dossier:
            dossier = Path(dossier)
            # Les échantillons du kit vivent dans le dossier de sortie ; le
            # mini-projet les référence par chemin relatif, il lui faut donc
            # sa propre copie.
            for chemin_relatif in track.samples.values():
                source = samples_root / chemin_relatif
                if source.is_file():
                    destination = dossier / "solo" / chemin_relatif
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, destination)
            rendu = _render_track(track, dossier / "solo", duree, sample_rate)
        if rendu is None:
            rapports.append(f"{track.name} : volume non calé (rendu solo impossible)")
            continue

        n = min(stem.size, rendu.size)
        rms_stem = _rms(stem[:n])
        rms_rendu = _rms(rendu[:n])
        if rms_rendu < SILENT_RMS or rms_stem < SILENT_RMS:
            rapports.append(f"{track.name} : volume non calé (rendu ou stem muet)")
            continue

        # Le rendu solo inclut déjà le volume actuel de la piste : la
        # correction est donc un facteur SUR ce volume, pas une valeur absolue.
        facteur = rms_stem / rms_rendu
        vise = track.volume * facteur
        cale = float(np.clip(vise, VOLUME_MIN, VOLUME_MAX))
        borne = " (borné)" if abs(cale - vise) > 1e-9 else ""
        rapports.append(
            f"{track.name} : volume {track.volume:.2f} -> {cale:.2f}{borne} "
            f"(rms stem {rms_stem:.4f}, rendu {rms_rendu:.4f})"
        )
        track.volume = cale
    return rapports
