# -*- coding: utf-8 -*-
"""Réglage FINAL de la gagnante, jugé sur le MÉLANGE (hypothèse H1, § 5 duodecies).

CE QUE LA MESURE A DIT, ET REDIT. Le réglage de piste optimise la distance au
stem ; le mélange le refuse quatre fois sur quatre (§ 5 decies), et le fan-out
des profils a montré la forme extrême du biais : un timbre vrai qui épouse le
stem FUITES COMPRISES gagne au stem et fait reculer le morceau. Le seul
critère qui ne soit pas un mandataire est la distance du MORCEAU rendu — c'est
elle qu'on optimise ici, en dernière passe, une fois le verdict rendu.

CE QUE ÇA COÛTE, ET POURQUOI LE BUDGET EST COURT. Une évaluation = un rendu de
PROJET (~10 à 15 s) + une distance, contre ~5 s pour un rendu de piste. Le
budget par défaut est donc de l'ordre de la trentaine d'évaluations, sur les
premiers axes déclarés par la machine — et chaque valeur n'est gardée que si
elle RAPPROCHE : la passe ne peut pas dégrader ce qu'elle reçoit, la règle de
l'automation de coupure et du réglage de piste.

LE VOLUME N'EST PAS UN AXE : il est recalé par `match_track_levels` avant
chaque évaluation, sans quoi on comparerait des niveaux, pas des timbres —
la leçon du § 5 decies (calage sur la piste SEULE, jamais sur le mélange).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

from .vsm_engine import VsmEngine, VsmEngineError
from .vsm_levels import match_track_levels
from .vsm_mix_verdict import _copy_samples, _render_project
from .vsm_track_refine import _probe_values


@dataclass
class MixRefineOutcome:
    track: str
    start_distance: float          # distance du MÉLANGE au départ
    distance: float                # ... et à l'arrivée
    parameters: Dict[str, float]
    evaluations: int
    improvements: List[Tuple[str, float, float]] = field(default_factory=list)


def refine_against_mix(
    tracks,                        # List[ExportTrack], modifiée EN PLACE
    track_name: str,
    mixture: np.ndarray,
    stems_audio: Dict[str, np.ndarray],
    samples_root: Path,
    workdir: Path,
    sample_rate: int,
    engine: VsmEngine,
    budget: int = 30,
    axes: int = 6,
    probes: int = 3,
    metric: str = "v2",
    tempo: float = 120.0,
    binary: Optional[str] = None,
) -> Optional[MixRefineOutcome]:
    """Affine les paramètres de `track_name` en jugeant le PROJET rendu.

    Modifie la piste en place si — et seulement si — le mélange se rapproche.
    Renvoie None si la machine ne déclare aucun axe ou si le rendu de départ
    échoue, ce que l'appelant DIT.
    """
    from .vsm_distance_cache import cached_distance_for

    piste = next((t for t in tracks if t.name == track_name), None)
    if piste is None or not piste.machine:
        return None
    try:
        profil = engine.search_profile(piste.machine)
    except VsmEngineError:
        return None
    if not profil:
        return None
    try:
        defauts = {str(p["id"]): float(p["default"]) for p in engine.parameters(piste.machine)}
    except VsmEngineError:
        defauts = {}

    fabrique = cached_distance_for(metric)
    mesurer = fabrique(np.asarray(mixture), sample_rate)

    workdir = Path(workdir)
    dossier = workdir / "reglage-melange" / track_name
    dossier.mkdir(parents=True, exist_ok=True)
    _copy_samples(tracks, samples_root, dossier)

    compteur = {"n": 0}
    cache: Dict[tuple, float] = {}

    def evaluer(valeurs: Dict[str, float]) -> Optional[float]:
        cle = tuple(sorted((k, round(v, 6)) for k, v in valeurs.items()))
        if cle in cache:
            return cache[cle]
        if compteur["n"] >= budget:
            return None
        piste.parameters = dict(valeurs)
        # Recalé sur la piste SEULE avant chaque mesure : deux patchs de
        # niveaux différents comparés au même volume compareraient des
        # niveaux. C'est la règle du verdict du mélange, reprise telle quelle.
        match_track_levels([piste], stems_audio, samples_root, sample_rate)
        rendu = _render_project(tracks, dossier, sample_rate, tempo, binary)
        compteur["n"] += 1
        if rendu is None or rendu.size == 0:
            cache[cle] = float("inf")
            return cache[cle]
        cache[cle] = float(mesurer(rendu))
        return cache[cle]

    courant: Dict[str, float] = dict(piste.parameters)
    depart = evaluer(courant)
    if depart is None or not np.isfinite(depart):
        return None

    meilleure = depart
    ameliorations: List[Tuple[str, float, float]] = []
    # Une passe large puis une resserrée, comme le réglage de piste — mais sur
    # moins d'axes et moins de sondes : chaque évaluation vaut trois des siennes.
    for span in (1.0, 0.35):
        for dimension in profil[:axes]:
            if compteur["n"] >= budget:
                break
            axe = dimension.semantic_id
            actuelle = courant.get(axe, defauts.get(axe))
            for valeur in _probe_values(dimension.low, dimension.high,
                                        dimension.logarithmic, probes,
                                        around=actuelle, span=span):
                essai = dict(courant)
                essai[axe] = float(valeur)
                distance = evaluer(essai)
                if distance is None:
                    break
                if distance < meilleure - 1e-6:
                    ameliorations.append((axe, meilleure, distance))
                    meilleure = distance
                    courant = essai

    # L'état FINAL de la piste est le meilleur trouvé — y compris si c'est le
    # point de départ : la passe ne dégrade jamais ce qu'elle reçoit.
    piste.parameters = dict(courant)
    match_track_levels([piste], stems_audio, samples_root, sample_rate)
    return MixRefineOutcome(track=track_name, start_distance=depart,
                            distance=meilleure, parameters=dict(courant),
                            evaluations=compteur["n"], improvements=ameliorations)
