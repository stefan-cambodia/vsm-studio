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

from dataclasses import dataclass
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

# ÉCART EN DEÇÀ DUQUEL DEUX MACHINES NE SONT PAS DÉPARTAGÉES.
#
# Mesuré, et c'est ce qui a rendu ce seuil nécessaire : sur le stem `other` de
# *Children*, l'arbitrage sépare `vsm.ms20` de `vsm.string` par UN MILLIÈME
# (0,260 contre 0,261). À cette marge, un simple changement de protocole -- le
# passage au rendu à durée imposée -- suffit à les intervertir, et le mauvais
# choix coûtait 0,016 sur le morceau entier sans que rien en aval puisse le
# rattraper.
#
# 2 % en RELATIF, et non une valeur absolue : les distances ne vivent pas dans
# la même plage d'un stem à l'autre, et un seuil absolu serait tantôt muet
# tantôt bavard. Ce n'est pas un réglage fin -- c'est une déclaration
# d'ignorance : « sous cet écart, je ne sais pas laquelle est la meilleure, que
# le mélange tranche ».
CLOSE_MARGIN = 0.02


def close_runner_up(verdicts: Sequence["TrackVerdict"],
                    margin: float = CLOSE_MARGIN) -> Optional["TrackVerdict"]:
    """
    La meilleure candidate d'une AUTRE machine que la gagnante, si elle est à
    portée de `margin` (relatif). Renvoie None s'il n'y en a pas, ce qui est le
    cas courant : la plupart du temps l'arbitrage tranche nettement.

    Une autre MACHINE, pas un autre patch : un second patch de la gagnante ne
    répare pas un mauvais choix de machine, et c'est ce choix-là que le verdict
    du mélange ne savait pas défaire.
    """
    if len(verdicts) < 2:
        return None
    gagnante = verdicts[0]
    seuil = gagnante.distance * (1.0 + margin)
    for v in verdicts[1:]:
        if v.machine == gagnante.machine:
            continue
        return v if v.distance <= seuil else None
    return None


@dataclass
class TrackCandidate:
    machine: str
    parameters: Dict[str, float]
    origin: str
    # NOM du profil multi-échantillons, pour les machines qui en exigent un.
    # Sans lui, le rendu hors ligne de la piste est MUET -- la machine n'a
    # aucune zone -- et la candidate se fait écarter pour une raison qui n'a
    # rien à voir avec son timbre.
    profile: str = ""


@dataclass
class TrackVerdict:
    machine: str
    parameters: Dict[str, float]
    origin: str
    distance: float


def build_candidates(
    searched: Sequence[Tuple[str, Dict[str, float]]],
    factory_machines: Sequence[str],
    profiles: Optional[Dict[str, str]] = None,
) -> List[TrackCandidate]:
    """
    La liste des candidates : les patchs trouvés, plus les patchs d'usine.

    Une machine peut donc apparaître deux fois, avec deux patchs différents.
    C'est voulu : ce sont deux propositions distinctes, et les départager est
    exactement ce qu'on demande à cette étape.

    `profiles` associe une machine au NOM de son profil multi-échantillons.
    L'oublier ne se voyait PAS : le rendu hors ligne sortait du silence, le
    garde-fou de niveau écartait la candidate, et elle disparaissait du tableau
    d'arbitrage sans un mot -- exactement la panne muette que le § 5 bis de la
    feuille de route interdit.
    """
    profiles = profiles or {}
    candidates = [TrackCandidate(machine, dict(parameters), ORIGINE_CHERCHE,
                                 profiles.get(machine, ""))
                  for machine, parameters in searched]
    for machine in factory_machines:
        candidates.append(TrackCandidate(machine, {}, ORIGINE_USINE, profiles.get(machine, "")))
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
    from .vsm_distance_cache import cached_distance_for

    # LA CIBLE NE CHANGE PAS D'UNE CANDIDATE À L'AUTRE : ses descripteurs sont
    # calculés UNE fois. C'est la même leçon que `vsm_distance_cache` avait
    # tirée pour la recherche sur une note, et elle vaut d'autant plus ici que
    # la cible dure quatre minutes au lieu d'une seconde -- sur 38 candidates,
    # c'était 38 fois le même travail.
    fabrique = cached_distance_for(metric)
    mesurer = fabrique(np.asarray(stem_audio), sample_rate)

    # DURÉE IMPOSÉE, ET C'EST LE GARDE-FOU DE NIVEAU QUI L'EXIGE. Sans elle,
    # `vsm-render` s'arrête à la dernière note plus deux secondes de queue,
    # alors que `stem_rms` porte sur le stem ENTIER et que `match_track_levels`
    # rendra, lui, toute la durée du stem. Sur une piste qui se tait avant la
    # fin, le rendu court n'a pas la traîne de silence : son niveau efficace est
    # surestimé, le facteur de volume prévu est sous-estimé, et des candidates
    # qui buteront sur VOLUME_MAX passent le filtre -- le cas même qu'il devait
    # attraper. La durée imposée met les deux mesures sur le même terrain.
    #
    # Elle change aussi la DISTANCE, et en mieux : la comparaison couvre
    # désormais tout le stem au lieu de s'arrêter à la dernière note.
    duree = float(np.asarray(stem_audio).size) / float(sample_rate) if sample_rate else None

    verdicts: List[TrackVerdict] = []
    ecartees: List[Tuple[str, str, str]] = []
    exportables = list(notes)
    workdir = Path(workdir)

    # UN SEUL dossier, réemployé : voir « ce que ça ne doit pas coûter » en tête
    # de module. Trente-huit rendus conservés, c'est trois gigaoctets par stem.
    dossier = workdir / "candidate"
    dossier.mkdir(parents=True, exist_ok=True)

    for candidate in candidates:
        piste = ExportTrack(name=name, machine=candidate.machine,
                            parameters=dict(candidate.parameters), notes=exportables,
                            profile=candidate.profile)
        rendu = render_track_offline(piste, dossier, sample_rate, duration=duree,
                                     tempo=tempo, binary=binary, title=f"arbitrage-{name}")
        # Effacé tout de suite : il a déjà été lu en mémoire, et le garder ne
        # servirait qu'à saturer le disque au trente-huitième.
        (dossier / "rendu.wav").unlink(missing_ok=True)
        if rendu is None or rendu.size == 0:
            # ABANDON DIT, jamais tu. Un rendu vide vient soit d'un moteur qui a
            # refusé la requête, soit d'une machine sans sa donnée -- et dans les
            # deux cas la candidate disparaît du tableau pour une raison qui n'a
            # rien à voir avec son timbre. Le lecteur du tableau doit pouvoir
            # faire la différence entre « mauvaise » et « pas mesurée ».
            ecartees.append((candidate.machine, candidate.origin, "rendu vide"))
            continue
        # Une candidate qui ne pourra pas atteindre le niveau de son stem n'est
        # pas une candidate : le calage des volumes buterait sur son plafond et
        # la piste resterait trop faible dans le mélange, quel que soit son
        # timbre. La même règle vaut au réglage (`vsm_track_refine`), où elle a
        # été apprise.
        if stem_rms is not None and stem_rms > 0.0:
            rms_rendu = float(np.sqrt(np.mean(np.square(rendu.astype(np.float64)))))
            if rms_rendu <= 0.0 or base_volume * stem_rms / rms_rendu > max_volume:
                raison = ("silence" if rms_rendu <= 0.0
                          else f"trop faible, il faudrait ×{base_volume * stem_rms / rms_rendu:.1f}")
                ecartees.append((candidate.machine, candidate.origin, raison))
                continue
        distance = float(mesurer(rendu))
        verdicts.append(TrackVerdict(candidate.machine, dict(candidate.parameters),
                                     candidate.origin, distance))

    verdicts.sort(key=lambda v: v.distance)
    if ecartees:
        print(f"      {name}  : {len(ecartees)} candidate(s) non mesurée(s) — "
              + ", ".join(f"{machine} ({origine}, {raison})"
                          for machine, origine, raison in ecartees))
    return verdicts
