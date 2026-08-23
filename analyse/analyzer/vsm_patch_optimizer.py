"""
Recherche de patch DANS l'espace de paramètres du DAW.

Différence avec `patch_optimizer.py` : celui-ci optimise les réglages d'un
synthé Python approximatif, puis il faut les traduire vers une machine réelle
-- et la traduction dégrade ce qu'on vient d'optimiser. Ici, chaque candidat
est rendu par la MACHINE VISÉE elle-même. Le résultat est un patch directement
chargeable dans le DAW ou dans un hôte CLAP : ce qu'on a optimisé est
exactement ce qu'on entendra.

Le prix : un rendu coûte ~10 ms (contre ~1 ms en Python pur). D'où le mode
service, qui garde les machines chargées, et un budget d'évaluations explicite
plutôt que subi.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np
from scipy.optimize import differential_evolution

from .audio_distance import audio_distance
from .vsm_distance_cache import CachedTargetDistance, CachedTargetDistanceV2
from .vsm_engine import SearchDimension, VsmEngine, VsmEngineError
from .vsm_search_seed import guided_population, measure_target


@dataclass
class SearchParameter:
    """Un paramètre à chercher, dans les unités RÉELLES de la machine."""
    semantic_id: str
    low: float
    high: float
    logarithmic: bool = False  # fréquences : chercher en log, l'oreille aussi


# ESPACE DE SECOURS, employé seulement si le moteur ne sait pas déclarer de
# profil (binaire ancien). Il n'est plus la source de vérité : depuis l'étape
# 8.2 de la feuille de route, l'espace vient de la MACHINE, via
# `VsmEngine.search_profile`. Le garder ici évite qu'une version dépareillée
# fasse échouer toute la chaîne, mais il ne vaut que pour un soustractif --
# c'est précisément le défaut qu'on a corrigé.
FALLBACK_SUBTRACTIVE_SPACE: List[SearchParameter] = [
    SearchParameter("filter.1.cutoff", 80.0, 12000.0, logarithmic=True),
    SearchParameter("filter.1.resonance", 0.0, 3.5),
    SearchParameter("filter.1.envAmount", 0.0, 1.0),
    SearchParameter("envelope.1.attack", 0.001, 0.6, logarithmic=True),
    SearchParameter("envelope.1.decay", 0.02, 1.5, logarithmic=True),
    SearchParameter("envelope.1.sustain", 0.0, 1.0),
    SearchParameter("envelope.1.release", 0.02, 2.0, logarithmic=True),
]


# Nombre de dimensions exploré par défaut. La valeur est MESURÉE, pas choisie
# au jugé : sur une cible produite par le Minimoog (coupure 900 Hz, résonance
# 2,2), à budget constant de 40 itérations et population 6, trois graines --
#
#     4 dimensions   distance médiane 0,220   coupure 1089 / 1087 / 1086
#     5 dimensions   distance médiane 0,029   coupure  884 /  899 /  908
#     6 dimensions   distance médiane 0,043   coupure  905 /  889 /  893
#     8 dimensions   distance médiane 0,331   coupure 5997 /  711 / 1198
#
# À quatre, la résonance sort de l'espace et la coupure se fige 20 % trop haut.
# À huit, la recherche devient erratique : le budget ne suffit plus à couvrir
# l'espace et les résultats varient d'un facteur huit d'une graine à l'autre.
#
# CE CHIFFRE VA DE PAIR AVEC LE BUDGET : l'augmenter n'a de sens qu'en
# augmentant `max_iterations` du même mouvement.
DEFAULT_MAX_DIMENSIONS = 6

# Nombre de dimensions pour la PASSE FINALE de `choose_machine` -- celle qui
# règle les finalistes, pas celle qui les classe. Mesuré (deux graines par
# point, 20 itérations fixes, trois cibles-vérité, quatre machines de largeurs
# différentes ; la population de scipy croissant avec la dimension, ouvrir 10
# axes coûte ~1,6x le temps de 6) :
#
#                          6 axes      10 axes     14 axes
#     dx7    / cloche      0,112       0,075       0,081     <- -33 %
#     dx7    / nappe       0,341       0,209       0,219     <- -39 %
#     ms20   / basse       0,098       0,074       0,078     <- -24 %
#     ms20   / nappe       0,248       0,215       0,180
#     ms20   / cloche      0,182       0,154       0,152
#     generic/ basse       0,180       0,169       0,184
#     generic/ nappe       0,180       0,176       0,188
#     generic/ cloche      0,223       0,248       0,241     <- régression
#     dx7    / basse       0,110       0,133       0,142     <- régression
#     pcm    / basse       0,181       0,185       0,152
#     pcm    / nappe       0,133       0,130       0,133
#     pcm    / cloche      0,420       0,420       0,424
#
# Lecture : 10 axes gagnent dans 8 cellules sur 12, et les gains majeurs vont
# aux machines cherchées DANS leur famille -- c'est-à-dire aux futures
# gagnantes, celles dont la distance décide du verdict. Les trois régressions
# touchent des machines hors de leur famille, qui ne gagnaient jamais : sur
# ces mesures, aucun verdict de `choose_machine` ne s'inverse. Quatorze axes
# n'apportent plus rien et coûtent 2,3x.
#
# D'où la règle en deux étages : le DÉGROSSISSAGE garde 6 axes (classer
# demande moins de précision que régler, et c'est la passe multipliée par
# toutes les candidates), la PASSE FINALE en ouvre 10 (payé sur les seuls
# finalistes). Un appelant qui passe `max_dimensions` explicitement garde la
# même valeur pour les deux passes, comme avant.
FINALIST_MAX_DIMENSIONS = 10


def search_space_for_machine(
    machine: str,
    engine: VsmEngine,
    max_dimensions: int = DEFAULT_MAX_DIMENSIONS,
) -> List[SearchParameter]:
    """
    Espace de recherche de `machine`, DÉCLARÉ PAR LE MOTEUR.

    `max_dimensions` borne le nombre de paramètres explorés, et ce n'est pas
    une commodité : le nombre d'évaluations nécessaires croît vite avec la
    dimension. Le DX7 déclare cinquante paramètres cherchables ; les explorer
    tous demanderait un budget hors de portée, et le résultat collerait au
    bruit plutôt qu'au son. On prend donc les plus importants, dans l'ordre que
    la machine elle-même a déclaré.
    """
    try:
        dimensions: List[SearchDimension] = engine.search_profile(machine)
    except VsmEngineError:
        # Moteur muet ou périmé : on le DIT, plutôt que de chercher dans le
        # vide en silence.
        import warnings

        warnings.warn(
            f"aucun profil de recherche pour « {machine} » ; repli sur l'espace "
            f"soustractif générique, qui ne convient pas à toutes les machines",
            RuntimeWarning,
            stacklevel=2,
        )
        return list(FALLBACK_SUBTRACTIVE_SPACE)

    return [
        SearchParameter(
            semantic_id=dimension.semantic_id,
            low=dimension.low,
            high=dimension.high,
            logarithmic=dimension.logarithmic,
        )
        for dimension in dimensions[:max_dimensions]
    ]


def _vector_to_parameters(
    space: Sequence[SearchParameter],
    vector: np.ndarray,
) -> Dict[str, float]:
    values: Dict[str, float] = {}
    for parameter, raw in zip(space, vector):
        if parameter.logarithmic:
            # Recherche en log : entre 80 Hz et 12 kHz, une recherche linéaire
            # passerait 99 % de son temps dans les aigus, où l'oreille entend
            # le moins de différence.
            values[parameter.semantic_id] = float(
                np.exp(np.log(parameter.low) + raw * (np.log(parameter.high) - np.log(parameter.low)))
            )
        else:
            values[parameter.semantic_id] = float(parameter.low + raw * (parameter.high - parameter.low))
    return values


@dataclass
class PatchSearchResult:
    machine: str
    parameters: Dict[str, float]
    distance: float
    evaluations: int
    audio: np.ndarray
    # Évaluations rejetées par la borne de niveau. Publié pour qu'on sache
    # combien de l'espace est inutilisable -- si c'est la moitié, la recherche
    # paie la moitié de son budget à découvrir ce qu'une borne sur le paramètre
    # de sortie lui aurait dit d'avance.
    rejected_for_level: int = 0


def optimize_patch_for_machine(
    target_audio: np.ndarray,
    midi_note: int,
    machine: str,
    engine: VsmEngine,
    sample_rate: int = 44100,
    space: Optional[Sequence[SearchParameter]] = None,
    max_iterations: int = 12,
    population: int = 6,
    fixed_parameters: Optional[Dict[str, float]] = None,
    seed: int = 1234,
    max_dimensions: int = DEFAULT_MAX_DIMENSIONS,
    metric: str = "v2",
    gate: float = 0.75,
    guided: bool = False,
    guided_fraction: float = 0.0,
    max_gain: Optional[float] = None,
) -> PatchSearchResult:
    """
    Cherche les réglages de `machine` qui reproduisent `target_audio`.

    `space` à None -- le cas normal -- fait demander son espace de recherche à
    LA MACHINE. Le passer explicitement reste possible pour forcer une
    exploration particulière, mais ce n'est plus le cas courant : une liste
    écrite ici serait forcément fausse pour au moins une machine du parc.

    `gate` est la proportion de l'extrait pendant laquelle la touche est tenue.
    Il DOIT correspondre à celui de la cible, et le défaut de 0,75 ne vaut que
    pour une cible produite avec ce même défaut.

    Pourquoi c'est important : une cible extraite d'un morceau tient souvent sa
    note 90 à 95 % de l'extrait, alors que les candidats étaient rendus à 75 %.
    L'optimiseur comparait donc une note relâchée trop tôt à une note encore
    tenue, et compensait ce décalage en faussant les enveloppes -- une erreur
    qu'il s'infligeait lui-même et qu'aucun réglage ne pouvait rattraper.

    `seed` est fixé par défaut : deux recherches identiques doivent donner le
    même patch, sans quoi comparer deux machines ne voudrait rien dire.

    `max_gain` : BORNE DE NIVEAU. Un candidat dont le niveau efficace est si bas
    qu'il faudrait l'amplifier de plus de `max_gain` pour rejoindre la cible
    est REJETÉ, comme l'est déjà une candidate sur la piste entière
    (`vsm_track_arbitration`, `vsm_track_refine`). La règle est la même, elle
    est simplement appliquée là où le défaut naît.

    POURQUOI ELLE MANQUAIT, ET CE QUE ÇA COÛTAIT. La distance est insensible au
    niveau — à raison : une machine ne doit pas gagner parce qu'elle sort plus
    fort. Mais sans borne, la recherche est libre de retenir un patch quasi
    muet dont le TIMBRE colle. Mesuré sur *B4 Wuz Then*, deux stems sur deux :
    le gagnant sur une note était « trop faible, il faudrait ×42 » sur la piste,
    et tombait à l'arbitrage — la recherche avait dépensé tout son budget sur
    un patch inutilisable. `None` conserve l'ancien comportement, pour l'A/B.
    """
    if space is None:
        space = search_space_for_machine(machine, engine, max_dimensions=max_dimensions)
    if not space:
        raise VsmEngineError(f"espace de recherche vide pour « {machine} »")
    duration = len(target_audio) / float(sample_rate)
    evaluations = 0
    # Les caractéristiques de la cible ne changent jamais : les recalculer à
    # chaque évaluation doublait le coût de la boucle (mesuré : 66 ms de
    # distance pour 10 ms de rendu).
    # MÉTRIQUE. « v2 » est celle de l'étape 10.3 : mêmes caractéristiques, mais
    # des termes rendus comparables entre eux, plus un terme de contraste
    # spectral. « v1 » reste accessible pour rejouer les mesures antérieures --
    # les deux ne donnent pas les mêmes chiffres et ne se comparent pas.
    fabrique = CachedTargetDistanceV2 if metric == "v2" else CachedTargetDistance
    distance_to_target = fabrique(target_audio, sample_rate)
    target_rms = float(np.sqrt(np.mean(np.asarray(target_audio, dtype=np.float64) ** 2)))
    rejected_for_level = 0

    def cost(vector: np.ndarray) -> float:
        nonlocal evaluations, rejected_for_level
        evaluations += 1
        parameters = _vector_to_parameters(space, vector)
        if fixed_parameters:
            parameters.update(fixed_parameters)
        try:
            candidate = engine.render_note(
                machine, parameters, midi_note=midi_note, duration=duration,
                gate=gate, sample_rate=sample_rate
            )
        except VsmEngineError:
            return 1e6  # une requête refusée ne doit pas interrompre la recherche
        if candidate.size == 0:
            return 1e6
        if max_gain is not None and target_rms > 0.0:
            candidate_rms = float(np.sqrt(np.mean(candidate.astype(np.float64) ** 2)))
            needed = target_rms / max(candidate_rms, 1e-12)
            if needed > max_gain:
                # PÉNALITÉ CROISSANTE plutôt qu'une falaise : l'évolution
                # différentielle a besoin d'une pente pour sortir d'une région,
                # et une falaise à 1e6 sur la moitié de l'espace la laisserait
                # errer sans information. Le rejet reste franc (toujours au-delà
                # de toute distance réelle), mais il dit DANS QUELLE DIRECTION.
                rejected_for_level += 1
                return 10.0 + float(np.log(needed / max_gain))
        return float(distance_to_target(candidate))

    # AMORCE GUIDÉE (étape 10.2 de la feuille de route) : ESSAYÉE, MESURÉE,
    # REJETÉE -- d'où le défaut à False.
    #
    # L'idée était de partir des caractéristiques mesurées de la cible plutôt
    # que d'un tirage. Le point d'amorce est bel et bien bon : sur une cible
    # Minimoog, il vaut 3,79 là où le tirage donne 10,39 en médiane, soit
    # mieux que 90 % des points au hasard.
    #
    # Il ne change pourtant RIEN au résultat. Comparé sur huit populations
    # initiales IDENTIQUES, en ne remplaçant qu'un membre par l'amorce :
    #
    #     budget  4 : sans 0,755  avec 0,767  -- l'amorce gagne 3 fois sur 8
    #     budget 10 : sans 0,405  avec 0,412  -- l'amorce gagne 4 fois sur 8
    #
    # C'est-à-dire pas mieux que pile ou face. L'explication tient à
    # l'algorithme : la force de l'évolution différentielle est la DIVERSITÉ de
    # sa population, et un bon point parmi trente-six s'y dilue. Le code reste,
    # documenté et désactivé, pour qu'on ne refasse pas le trajet.
    #
    # Une mesure trompeuse au passage, à retenir : fournir sa propre population
    # fait perdre l'hypercube latin que scipy emploie par défaut. Une première
    # version, uniforme, dégradait le résultat de 0,53 à 1,02 -- l'amorce
    # semblait nuisible alors que c'était la perte de stratification.
    initiale = None
    if guided:
        taille = max(5, population * len(space))
        initiale = guided_population(
            space, measure_target(target_audio, sample_rate), taille,
            np.random.default_rng(seed), neighbour_fraction=guided_fraction,
        )

    result = differential_evolution(
        cost,
        bounds=[(0.0, 1.0)] * len(space),
        maxiter=max_iterations,
        popsize=population,
        seed=seed,
        polish=False,
        tol=0.01,
        **({"init": initiale} if initiale is not None else {}),
    )

    parameters = _vector_to_parameters(space, result.x)
    if fixed_parameters:
        parameters.update(fixed_parameters)
    audio = engine.render_note(
        machine, parameters, midi_note=midi_note, duration=duration,
        gate=gate, sample_rate=sample_rate
    )
    return PatchSearchResult(
        machine=machine,
        parameters=parameters,
        distance=float(result.fun),
        evaluations=evaluations,
        audio=audio,
        rejected_for_level=rejected_for_level,
    )


def choose_machine(
    target_audio: np.ndarray,
    midi_note: int,
    engine: VsmEngine,
    machines: Sequence[str],
    sample_rate: int = 44100,
    metric: str = "v2",
    gate: float = 0.75,
    shortlist: Optional[int] = None,
    draft_seconds: float = 0.4,
    draft_iterations: int = 4,
    **kwargs,
) -> Tuple[PatchSearchResult, List[PatchSearchResult]]:
    """
    Cherche un patch sur PLUSIEURS machines et retient la plus proche.

    C'est le vrai problème de reconstruction : on ne sait pas d'avance quelle
    machine a servi. Chercher sur une seule et annoncer le résultat comme "le"
    patch serait présenter un choix arbitraire comme une conclusion.

    DEUX PASSES (étape 10 de la feuille de route), et voici pourquoi elles
    gagnent là où les autres pistes ont échoué. Une passe complète coûte ~9 s
    par machine ; sur quinze machines, un seul extrait demande plus de quatre
    minutes. Or la première passe n'a pas à RÉGLER les machines, seulement à
    les CLASSER -- et classer demande beaucoup moins de précision que régler.

    Mesuré sur une cible connue, en jugeant tous les patchs à pleine
    résolution (médiane sur huit graines) :

        1,0 s à 44,1 kHz    qualité 0,371    8,5 s par machine
        0,5 s à 44,1 kHz    qualité 0,542    4,3 s
        1,0 s à 22,05 kHz   qualité 1,250    4,3 s
        0,5 s à 22,05 kHz   qualité 0,773    2,6 s

    La qualité se dégrade, mais la coupure trouvée reste entre 871 et 995 Hz
    pour 900 visés dans TOUS les cas : l'ordre des machines, lui, résiste.
    On dégrossit donc sur un extrait court, on garde les `shortlist`
    meilleures, et on ne paie la passe complète que sur celles-là.

    COMBIEN DE FINALISTES : la moitié, et c'est mesuré. Sur quinze machines,
    en jugeant le patch final :

        sans présélection   343 s   distance 0,289
        7 finalistes        174 s   distance 0,289   <- même verdict, 2 fois moins cher
        5 finalistes        132 s   distance 0,438
        3 finalistes         84 s   distance 0,439

    Retenir la moitié reproduit exactement le verdict de la recherche
    complète ; descendre plus bas écarte parfois la machine qui aurait gagné.
    Le facteur quatre reste disponible pour qui préfère la vitesse -- il suffit
    de passer `shortlist=3`, et le compromis est chiffré ci-dessus.

    `shortlist=0` désactive le dégrossissage et retrouve le comportement
    d'origine -- utile pour vérifier que la présélection ne change pas le
    verdict.
    """
    candidates = list(machines)
    degrossies: Dict[str, float] = {}
    if shortlist is None:
        shortlist = max(3, (len(candidates) + 1) // 2)

    if shortlist and 0 < shortlist < len(candidates):
        # Extrait court, pris au DÉBUT : c'est là que se trouvent l'attaque et
        # le début du timbre, c'est-à-dire ce qui distingue le plus deux
        # machines. Couper à la fin garderait surtout de l'extinction.
        court = target_audio[: max(1, int(draft_seconds * sample_rate))]
        # Budget de dégrossissage RÉDUIT, en plus de l'extrait court. C'est le
        # point : on ne cherche pas le bon réglage, on cherche le bon ORDRE.
        # Donner à cette passe le budget d'une passe complète ne gagnerait que
        # le temps de l'extrait, soit un facteur deux au mieux.
        brouillon_kwargs = dict(kwargs)
        brouillon_kwargs["max_iterations"] = draft_iterations
        # Classer demande moins de précision que régler : le dégrossissage
        # reste à 6 axes même quand la passe finale en ouvre 10 (voir
        # FINALIST_MAX_DIMENSIONS). C'est la passe payée par TOUTES les
        # candidates ; l'élargir coûterait 1,6x sur tout le monde pour
        # améliorer un classement qui résiste déjà à bien pire (mesuré à
        # l'étape 10 : l'ordre tient même sur un extrait à moitié plus court).
        brouillon_kwargs.setdefault("max_dimensions", DEFAULT_MAX_DIMENSIONS)
        brouillon = []
        for machine in candidates:
            try:
                resultat = optimize_patch_for_machine(
                    court, midi_note, machine, engine, sample_rate=sample_rate,
                    metric=metric, gate=gate, **brouillon_kwargs
                )
            except VsmEngineError:
                continue
            brouillon.append((resultat.distance, machine))
            degrossies[machine] = resultat.distance
        if brouillon:
            brouillon.sort()
            candidates = [machine for _, machine in brouillon[:shortlist]]

    finale_kwargs = dict(kwargs)
    # La passe finale ouvre 10 axes (mesuré : voir FINALIST_MAX_DIMENSIONS).
    # Un appelant qui a fixé `max_dimensions` garde sa valeur pour les deux
    # passes -- une expérience qui compare des budgets doit pouvoir tout figer.
    finale_kwargs.setdefault("max_dimensions", FINALIST_MAX_DIMENSIONS)
    results = [
        optimize_patch_for_machine(
            target_audio, midi_note, machine, engine, sample_rate=sample_rate,
            metric=metric, gate=gate, **finale_kwargs
        )
        for machine in candidates
    ]
    results.sort(key=lambda r: r.distance)
    return results[0], results
