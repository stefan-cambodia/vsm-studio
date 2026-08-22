# -*- coding: utf-8 -*-
"""
RÉGLAGE DU PATCH SUR LA PISTE ENTIÈRE, après que la piste a choisi la machine.

L'arbitrage (`vsm_track_arbitration`) a montré que le critère « une note » et le
critère « la piste » ne classent pas les machines dans le même ordre. La même
remarque vaut pour les RÉGLAGES : le patch retenu a été optimisé contre une
note, et rien ne garantit qu'il soit le meilleur de sa machine sur la piste --
d'autant qu'à l'arbitrage, un patch d'USINE l'emporte parfois sur un patch
cherché. Cette étape reprend donc les réglages là où l'arbitrage les laisse, et
les juge sur ce qu'on entendra.

CE QU'ELLE FAIT, ET PAS PLUS. Une descente par coordonnées : chaque axe de
recherche déclaré par la machine est balayé à son tour, quelques valeurs par
axe, dans l'ordre d'importance que la machine elle-même annonce ; une valeur
n'est retenue que si elle RAPPROCHE le rendu complet de la piste de son stem.
Ce n'est pas un optimiseur global, et ce n'est pas une prétention modeste : à
cinq secondes l'évaluation, un budget global n'est pas payable, alors qu'une
descente par coordonnées améliore de façon monotone -- elle ne peut pas rendre
le patch pire que celui d'où elle part, ce qui est exactement la garantie qu'on
veut d'une étape ajoutée en bout de chaîne.

CE QUE ÇA COÛTE, MESURÉ. Un rendu de piste (1,4 s sur quatre minutes) plus une
distance (3,7 s) par évaluation : **c'est la distance qui domine, pas le
rendu**. Le budget est donc compté en évaluations et il est exposé sur la ligne
de commande, comme celui de la recherche -- deux réglages obtenus à des budgets
différents ne se comparent pas (ARCHITECTURE.md § 32).

CE QU'ELLE NE FAIT PAS. Elle ne règle que la machine GAGNANTE. Régler les trois
premières et les redépartager coûterait trois fois plus pour un gain non
mesuré ; le jour où on le mesurera, ce sera écrit ici.

UN PATCH DOIT POUVOIR ATTEINDRE LE NIVEAU DE SON STEM, et cette contrainte a été
APPRISE, pas prévue. La distance est insensible au niveau -- c'est voulu, une
machine ne doit pas gagner parce qu'elle sort plus fort. Mais la descente n'a
alors aucune raison de préserver le volume : sur la basse de *Children*, elle a
trouvé un patch meilleur de 27 % en timbre (0,282 -> 0,206) et deux fois plus
FAIBLE (niveau efficace 0,0117 -> 0,0060). Le calage des volumes demandait
ensuite un facteur 13, butait sur son plafond, et le mélange y perdait ce que
la piste y avait gagné : 0,2507 contre 0,2246 pour le patch non affiné. Un
candidat dont le niveau ne peut plus être rattrapé est donc REFUSÉ, avec le même
esprit que partout ailleurs -- ce qui est jugé doit être ce qu'on entendra.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

from .vsm_engine import VsmEngine, VsmEngineError
from .vsm_offline_render import render_track_offline
from .vsm_project_export import ExportNote, ExportTrack


@dataclass
class RefineOutcome:
    parameters: Dict[str, float]
    distance: float
    start_distance: float
    evaluations: int
    # (axe, valeur retenue, distance obtenue) pour chaque amélioration gardée.
    # Un réglage sans son historique ne se relit pas : on saurait que le patch
    # a bougé, pas ce qui l'a fait bouger.
    improvements: List[Tuple[str, float, float]] = field(default_factory=list)


def _probe_values(low: float, high: float, logarithmic: bool, count: int,
                  around: Optional[float] = None, span: float = 1.0) -> List[float]:
    """
    Les valeurs à essayer sur un axe.

    `span` resserre le balayage autour de `around` : la première passe explore
    tout l'intervalle, les suivantes affinent autour de ce qui a été retenu.
    """
    if logarithmic:
        lo, hi = np.log(max(low, 1e-9)), np.log(max(high, 1e-9))
    else:
        lo, hi = low, high
    if around is not None and span < 1.0:
        centre = np.log(max(around, 1e-9)) if logarithmic else around
        demi = (hi - lo) * span / 2.0
        lo, hi = max(lo, centre - demi), min(hi, centre + demi)
    points = np.linspace(lo, hi, count)
    return [float(np.exp(p)) if logarithmic else float(p) for p in points]


def refine_patch_on_track(
    machine: str,
    parameters: Dict[str, float],
    notes: Sequence[ExportNote],
    stem_audio: np.ndarray,
    engine: VsmEngine,
    workdir: Path,
    sample_rate: int,
    budget: int = 40,
    axes: int = 8,
    probes: int = 4,
    metric: str = "v2",
    tempo: float = 120.0,
    binary: Optional[str] = None,
    name: str = "piste",
    stem_rms: Optional[float] = None,
    base_volume: float = 0.9,
    max_volume: float = 10.0,
) -> Optional[RefineOutcome]:
    """
    Affine `parameters` pour `machine` en jugeant sur la piste entière.

    Renvoie None si la machine ne déclare aucun axe ou si le rendu de départ
    échoue -- ce qui est dit par l'appelant, jamais compensé par un patch
    inventé.
    """
    from .vsm_distance_cache import CachedTargetDistance, CachedTargetDistanceV2

    # Cible mise en cache : quarante évaluations contre le même stem, c'est
    # trente-neuf fois le même calcul évité (voir `vsm_track_arbitration`).
    fabrique = CachedTargetDistanceV2 if metric == "v2" else CachedTargetDistance
    mesurer = fabrique(np.asarray(stem_audio), sample_rate)

    try:
        profil = engine.search_profile(machine)
    except VsmEngineError:
        return None
    if not profil:
        return None

    # Valeurs de départ : le patch reçu, complété par les DÉFAUTS DE LA MACHINE
    # pour les axes qu'il ne fixe pas. Sans ça, on balayerait un axe depuis une
    # valeur qu'on croit connaître et qui n'est pas celle qui joue.
    try:
        defauts = {str(p["id"]): float(p["default"]) for p in engine.parameters(machine)}
    except VsmEngineError:
        defauts = {}

    courant: Dict[str, float] = dict(parameters)
    workdir = Path(workdir)
    dossier = workdir / "reglage"
    dossier.mkdir(parents=True, exist_ok=True)

    cache: Dict[tuple, float] = {}
    compteur = {"n": 0}

    def evaluer(valeurs: Dict[str, float]) -> Optional[float]:
        cle = tuple(sorted((k, round(v, 6)) for k, v in valeurs.items()))
        if cle in cache:
            return cache[cle]
        if compteur["n"] >= budget:
            return None
        piste = ExportTrack(name=name, machine=machine, parameters=dict(valeurs),
                            notes=list(notes))
        rendu = render_track_offline(piste, dossier, sample_rate, tempo=tempo, binary=binary,
                                     title=f"reglage-{name}")
        (dossier / "rendu.wav").unlink(missing_ok=True)
        compteur["n"] += 1
        if rendu is None or rendu.size == 0:
            cache[cle] = float("inf")
            return cache[cle]
        # NIVEAU ATTEIGNABLE : voir « un patch doit pouvoir atteindre le niveau
        # de son stem » en tête de module. Le calage des volumes fera
        # `base_volume * rms_stem / rms_rendu` et sera borné à `max_volume` ;
        # un candidat qui exige davantage est refusé ici, où l'on sait encore
        # pourquoi, plutôt que d'être rattrapé à moitié au mixage.
        if stem_rms is not None and stem_rms > 0.0:
            rms_rendu = float(np.sqrt(np.mean(np.square(rendu.astype(np.float64)))))
            if rms_rendu <= 0.0 or base_volume * stem_rms / rms_rendu > max_volume:
                cache[cle] = float("inf")
                return cache[cle]

        distance = float(mesurer(rendu))
        cache[cle] = distance
        return distance

    depart = evaluer(courant)
    if depart is None or not np.isfinite(depart):
        return None

    meilleure = depart
    ameliorations: List[Tuple[str, float, float]] = []

    # Deux passes : la première balaye tout l'intervalle de chaque axe, la
    # seconde resserre autour de ce qui a été retenu. Une seule passe large
    # rate les réglages fins ; une seule passe fine part d'un point qu'on n'a
    # pas de raison de croire bon.
    for passe, span in enumerate((1.0, 0.35)):
        for dimension in profil[:axes]:
            if compteur["n"] >= budget:
                break
            actuelle = courant.get(dimension.semantic_id, defauts.get(dimension.semantic_id))
            essais = _probe_values(dimension.low, dimension.high, dimension.logarithmic,
                                   probes, around=actuelle, span=span)
            for valeur in essais:
                if actuelle is not None and abs(valeur - actuelle) <= 1e-9:
                    continue
                candidat = dict(courant)
                candidat[dimension.semantic_id] = valeur
                distance = evaluer(candidat)
                if distance is None:
                    break
                if distance < meilleure - 1e-6:
                    meilleure = distance
                    courant = candidat
                    ameliorations.append((dimension.semantic_id, valeur, distance))
        if compteur["n"] >= budget:
            break

    return RefineOutcome(parameters=courant, distance=meilleure, start_distance=depart,
                         evaluations=compteur["n"], improvements=ameliorations)
