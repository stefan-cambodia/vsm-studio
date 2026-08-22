# -*- coding: utf-8 -*-
"""
LE MÉLANGE A LE DERNIER MOT : on ne garde d'une amélioration de piste que ce
qui rapproche le MORCEAU.

POURQUOI CETTE ÉTAPE EXISTE, ET CE QU'ELLE A COÛTÉ D'APPRENDRE. La chaîne juge
chaque piste contre SON stem, et c'est raisonnable : c'est la seule cible dont
on dispose piste par piste. Mais les stems d'une séparation ne se rendorment pas
exactement dans l'original -- ils se recouvrent, ils fuient l'un dans l'autre --
et rien ne garantit qu'une piste plus proche de son stem donne un mélange plus
proche du morceau. Mesuré sur *Children (Dream Version)*, avec le réglage de
patch sur la piste :

| | distance des pistes à leur stem | distance du MORCEAU |
|---|---|---|
| arbitrage seul | basse 0,282 · other 0,250 · voix 0,346 | **0,2246** |
| + réglage libre | basse 0,206 · other 0,246 · voix 0,168 | 0,2519 |
| + réglage contraint en niveau | basse 0,216 · other 0,246 · voix 0,168 | 0,2380 |

Les trois pistes s'améliorent, le morceau recule. Contraindre le niveau
récupère la moitié de l'écart, pas plus : ce n'était donc pas seulement une
affaire de volume. **Une piste jugée seule et une piste dans un mélange ne sont
pas le même objectif**, et c'est le second qu'on écoute.

CE QUE FAIT CETTE ÉTAPE. Pour chaque piste qui a deux propositions -- celle de
l'arbitrage et celle du réglage --, elle rend le PROJET COMPLET avec l'une puis
avec l'autre, et garde celle qui rapproche du morceau. Le volume est recalé pour
chaque variante, sans quoi on comparerait un patch à un autre au mauvais niveau.
C'est exactement la règle de l'automation de coupure (« gardée seulement si elle
RAPPROCHE le rendu »), appliquée un cran plus haut.

Le parcours est glouton, piste par piste, dans l'ordre : chaque décision est
prise avec les décisions déjà arrêtées. Explorer les huit combinaisons de trois
pistes coûterait huit rendus complets au lieu de six, pour un gain qui n'est pas
mesuré -- si on le mesure un jour, ce sera écrit ici.

CE QUE ÇA COÛTE, MESURÉ : un rendu de projet (~5 s sur quatre minutes) plus une
distance (3,7 s) par variante, soit environ dix secondes par proposition.
"""

from __future__ import annotations

import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import numpy as np

from .vsm_engine import find_vsm_render
from .vsm_levels import match_track_levels
from .vsm_offline_render import read_render_wav
from .vsm_project_export import ExportTrack, write_project_bundle


@dataclass
class MixDecision:
    track: str
    kept: str            # "réglage" ou "arbitrage"
    distance_kept: float
    distance_other: float


def _copy_samples(tracks: Sequence[ExportTrack], samples_root: Path, folder: Path) -> None:
    """
    Recopie les échantillons référencés par les pistes DANS le dossier de rendu.

    SANS CETTE ÉTAPE, LE VERDICT SE PRONONÇAIT SUR UN MÉLANGE SANS LA VOIX.
    Les pistes de sampler -- le report vocal, et le kit si `--batterie-
    echantillonnee` -- désignent leurs fichiers par chemin RELATIF au dossier de
    projet. Un mini-projet écrit ailleurs ne les trouve donc pas, et
    `vsm-render` ne s'en plaint que par un avertissement sur la sortie d'erreur,
    que `capture_output` avale : le rendu réussit, la piste est muette, et rien
    ne le dit. On comparait deux mélanges amputés du stem le plus présent.

    C'est exactement ce que fait déjà `match_track_levels` avant son rendu solo,
    et pour la même raison ; la règle est ici la même, écrite au même endroit
    que le rendu qu'elle sert.
    """
    for track in tracks:
        for chemin_relatif in track.samples.values():
            source = Path(samples_root) / chemin_relatif
            if not source.is_file():
                continue
            destination = Path(folder) / chemin_relatif
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists() and destination.stat().st_size == source.stat().st_size:
                continue
            shutil.copy2(source, destination)


def _render_project(tracks: Sequence[ExportTrack], folder: Path, sample_rate: int,
                    tempo: float, binary: Optional[str]) -> Optional[np.ndarray]:
    write_project_bundle(list(tracks), folder, title="verdict-mélange", tempo=tempo)
    sortie = folder / "rendu.wav"
    try:
        subprocess.run([str(find_vsm_render(binary)), str(folder), str(sortie),
                        "--sample-rate", str(sample_rate), "--quiet"],
                       check=True, capture_output=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    audio = read_render_wav(sortie)
    sortie.unlink(missing_ok=True)
    return audio


def keep_what_helps_the_mix(
    tracks: List[ExportTrack],
    alternatives: Dict[str, Dict[str, float]],
    mixture: np.ndarray,
    stems_audio: Dict[str, np.ndarray],
    samples_root: Path,
    workdir: Path,
    sample_rate: int,
    metric: str = "v2",
    tempo: float = 120.0,
    binary: Optional[str] = None,
) -> List[MixDecision]:
    """
    Tranche piste par piste entre le patch courant et son alternative.

    `tracks` est modifié EN PLACE : à la sortie, chaque piste porte le patch et
    le volume de la variante retenue. `alternatives` donne, par nom de piste, le
    patch concurrent -- typiquement celui d'avant le réglage.

    Renvoie une décision par piste examinée, pour que le journal puisse dire ce
    qui a été gardé ET ce qui a été écarté, avec les deux chiffres.
    """
    from .vsm_distance_cache import CachedTargetDistance, CachedTargetDistanceV2

    # Le MORCEAU est la cible, et il ne change pas : mis en cache comme partout
    # ailleurs dans la chaîne.
    fabrique = CachedTargetDistanceV2 if metric == "v2" else CachedTargetDistance
    mesurer = fabrique(np.asarray(mixture), sample_rate)

    workdir = Path(workdir)
    # UN SEUL dossier, réemployé d'une variante à l'autre, et les échantillons
    # recopiés UNE fois : ce qui change d'un rendu au suivant, c'est un patch et
    # un volume, jamais un fichier. Garder un dossier par variante recopiait le
    # report vocal entier (plusieurs dizaines de mégaoctets) à chaque essai,
    # pour un résultat identique -- la leçon de `vsm_track_arbitration`, dont la
    # première exécution est morte d'un « No space left on device ».
    dossier = workdir / "variante"
    dossier.mkdir(parents=True, exist_ok=True)
    _copy_samples(tracks, samples_root, dossier)
    decisions: List[MixDecision] = []

    def distance_du_projet() -> float:
        rendu = _render_project(tracks, dossier, sample_rate, tempo, binary)
        if rendu is None or rendu.size == 0:
            return float("inf")
        return float(mesurer(rendu))

    for track in tracks:
        autre = alternatives.get(track.name)
        if autre is None or dict(autre) == dict(track.parameters):
            continue

        courant_params, courant_volume = dict(track.parameters), float(track.volume)
        d_courant = distance_du_projet()

        # Le VOLUME est recalé pour la variante : deux patchs de niveaux
        # différents comparés au même volume ne compareraient pas les patchs.
        track.parameters = dict(autre)
        match_track_levels([track], stems_audio, samples_root, sample_rate)
        d_autre = distance_du_projet()

        if d_autre < d_courant - 1e-6:
            decisions.append(MixDecision(track.name, "arbitrage", d_autre, d_courant))
        else:
            track.parameters, track.volume = courant_params, courant_volume
            decisions.append(MixDecision(track.name, "réglage", d_courant, d_autre))

    return decisions
