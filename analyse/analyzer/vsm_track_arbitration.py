# -*- coding: utf-8 -*-
"""
ARBITRAGE SUR LA PISTE ENTIÈRE : la note choisit le patch, la piste choisit
la machine.

POURQUOI CETTE ÉTAPE EXISTE. La recherche de patch travaille sur UNE note --
la plus longue du stem -- et c'est une hypothèse assumée depuis le début : les
notes d'un même instrument partagent leur timbre, et chercher sur chacune
coûterait des heures. L'hypothèse tient pour RÉGLER un patch. Elle ne tient pas
pour CHOISIR une machine, et la mesure l'a montré sans ambiguïté sur le stem de
basse de *Children (Dream Version)* :

| ce qui joue la piste de basse | distance à son stem, piste entière |
|---|---|
| `vsm.generic`, patch trouvé par la recherche sur une note | 0,4614 |
| `vsm.piano`, patch d'USINE, aucune recherche | **0,2820** |

Une machine que la recherche n'avait pas retenue, avec le patch qu'elle a en
sortant de boîte, faisait 39 % mieux sur la piste réelle que la gagnante réglée
sur mesure. Ce n'est pas un défaut de l'optimiseur : c'est que le critère « une
note » et le critère « la piste » ne classent pas dans le même ordre, et que
c'est le second qu'on écoute.

CE QUE FAIT L'ARBITRAGE. Il rend la piste ENTIÈRE, avec toutes ses notes, pour
chaque candidate -- le patch trouvé par la recherche, et aussi le patch d'usine
de chaque machine -- puis classe par distance au stem. Le patch d'usine est dans
la liste précisément parce que le cas ci-dessus s'est produit : l'exclure
reviendrait à décider d'avance qu'un patch réglé bat toujours un patch neuf, ce
qui est faux.

CE QUE ÇA COÛTE. Un rendu de piste par candidate. Mesuré sur un morceau de
quatre minutes : 1,4 s de rendu, plus la distance. Face aux minutes que dure la
recherche de patch, c'est marginal -- et c'est la seule étape de la chaîne qui
juge sur ce qu'on entendra vraiment.

CE QUE ÇA NE DOIT PAS COÛTER : DE LA PLACE. Un rendu de quatre minutes en
stéréo flottante pèse 82 Mo. Garder les trente-huit remplissait sept gigaoctets
de `/tmp` -- qui est un disque en MÉMOIRE sur beaucoup de systèmes -- et la
première exécution est morte dessus, « No space left on device », après avoir
mené l'arbitrage de la basse à bien. Chaque candidate est donc rendue dans LE
MÊME dossier, et son WAV est effacé sitôt mesuré : l'empreinte est celle d'un
seul rendu, quel que soit le nombre de candidates.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

from .vsm_offline_render import render_track_offline
from .vsm_project_export import ExportNote, ExportTrack

# Origine d'une candidate, écrite telle quelle dans le journal et le rapport :
# savoir qu'une machine l'emporte AVEC SON PATCH D'USINE est une information
# sur la recherche, pas seulement sur la machine.
ORIGINE_CHERCHE = "patch cherché"
ORIGINE_USINE = "patch d'usine"


@dataclass
class TrackCandidate:
    machine: str
    parameters: Dict[str, float]
    origin: str


@dataclass
class TrackVerdict:
    machine: str
    parameters: Dict[str, float]
    origin: str
    distance: float


def build_candidates(
    searched: Sequence[Tuple[str, Dict[str, float]]],
    factory_machines: Sequence[str],
) -> List[TrackCandidate]:
    """
    La liste des candidates : les patchs trouvés, plus les patchs d'usine.

    Une machine peut donc apparaître deux fois, avec deux patchs différents.
    C'est voulu : ce sont deux propositions distinctes, et les départager est
    exactement ce qu'on demande à cette étape.
    """
    candidates = [TrackCandidate(machine, dict(parameters), ORIGINE_CHERCHE)
                  for machine, parameters in searched]
    for machine in factory_machines:
        candidates.append(TrackCandidate(machine, {}, ORIGINE_USINE))
    return candidates


def arbitrate_on_track(
    notes: Sequence[ExportNote],
    stem_audio: np.ndarray,
    candidates: Sequence[TrackCandidate],
    workdir: Path,
    sample_rate: int,
    metric: str = "v2",
    tempo: float = 120.0,
    binary: Optional[str] = None,
    name: str = "piste",
    stem_rms: Optional[float] = None,
    base_volume: float = 0.9,
    max_volume: float = 10.0,
) -> List[TrackVerdict]:
    """
    Rend la piste entière pour chaque candidate et les classe.

    Renvoie la liste TRIÉE par distance croissante ; vide si aucune candidate
    n'a pu être rendue -- ce qui est dit par l'appelant, jamais compensé par un
    choix arbitraire.

    La distance est la MÊME que partout ailleurs dans la chaîne, et elle est
    insensible au niveau : une machine ne gagne pas parce qu'elle sort plus
    fort. Le calage des volumes vient plus tard, et sur le stem.
    """
    from .vsm_distance_cache import CachedTargetDistance, CachedTargetDistanceV2

    # LA CIBLE NE CHANGE PAS D'UNE CANDIDATE À L'AUTRE : ses descripteurs sont
    # calculés UNE fois. C'est la même leçon que `vsm_distance_cache` avait
    # tirée pour la recherche sur une note, et elle vaut d'autant plus ici que
    # la cible dure quatre minutes au lieu d'une seconde -- sur 38 candidates,
    # c'était 38 fois le même travail.
    fabrique = CachedTargetDistanceV2 if metric == "v2" else CachedTargetDistance
    mesurer = fabrique(np.asarray(stem_audio), sample_rate)

    verdicts: List[TrackVerdict] = []
    exportables = list(notes)
    workdir = Path(workdir)

    # UN SEUL dossier, réemployé : voir « ce que ça ne doit pas coûter » en tête
    # de module. Trente-huit rendus conservés, c'est trois gigaoctets par stem.
    dossier = workdir / "candidate"
    dossier.mkdir(parents=True, exist_ok=True)

    for candidate in candidates:
        piste = ExportTrack(name=name, machine=candidate.machine,
                            parameters=dict(candidate.parameters), notes=exportables)
        rendu = render_track_offline(piste, dossier, sample_rate, tempo=tempo, binary=binary,
                                     title=f"arbitrage-{name}")
        # Effacé tout de suite : il a déjà été lu en mémoire, et le garder ne
        # servirait qu'à saturer le disque au trente-huitième.
        (dossier / "rendu.wav").unlink(missing_ok=True)
        if rendu is None or rendu.size == 0:
            continue
        # Une candidate qui ne pourra pas atteindre le niveau de son stem n'est
        # pas une candidate : le calage des volumes buterait sur son plafond et
        # la piste resterait trop faible dans le mélange, quel que soit son
        # timbre. La même règle vaut au réglage (`vsm_track_refine`), où elle a
        # été apprise.
        if stem_rms is not None and stem_rms > 0.0:
            rms_rendu = float(np.sqrt(np.mean(np.square(rendu.astype(np.float64)))))
            if rms_rendu <= 0.0 or base_volume * stem_rms / rms_rendu > max_volume:
                continue
        distance = float(mesurer(rendu))
        verdicts.append(TrackVerdict(candidate.machine, dict(candidate.parameters),
                                     candidate.origin, distance))

    verdicts.sort(key=lambda v: v.distance)
    return verdicts
