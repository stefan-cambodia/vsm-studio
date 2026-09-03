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
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

from .vsm_engine import find_vsm_render
from .vsm_levels import match_track_levels, recaler_avec_son_groupe
from .vsm_offline_render import read_render_wav
from .vsm_project_export import ExportNote, ExportTrack, write_project_bundle


@dataclass
class MixAlternative:
    """
    Une proposition concurrente pour une piste, telle que le mélange la jugera.

    `machine` vide veut dire « garder la machine en place et ne changer que le
    patch » -- le cas du patch d'avant réglage. Non vide, c'est une AUTRE
    machine, et c'est le seul endroit de la chaîne où un choix de machine peut
    encore être défait.
    """
    parameters: Dict[str, float]
    label: str
    machine: str = ""
    # NOTES propres à la proposition, quand la machine en a d'autres. Pour un
    # stem mélodique, toutes les machines jouent les mêmes notes et ce champ
    # reste None. Pour la BATTERIE, la correspondance famille -> note diffère
    # d'une boîte à l'autre (la 909 a un clap en 39, vsm.drums une percussion
    # en 49) : remettre vsm.drums en jeu avec les notes de la 909 ferait taire
    # des pièces, et le verdict jugerait un kit amputé.
    notes: Optional[List[ExportNote]] = None
    # NOM du profil multi-échantillons de la proposition, quand elle en porte
    # un. Vide : le profil se déduit (celui de la machine visée si la machine
    # change, celui DÉJÀ en place sinon -- une proposition « paramètres seuls »
    # ne doit pas écraser le profil que l'arbitrage a choisi).
    profile: str = ""
    # La distance de PISTE de cette proposition, quand on la connaît. Elle
    # voyage avec elle pour que le rapport puisse suivre la décision : c'est la
    # TROISIÈME fois qu'un champ du rapport reste sur le patch écarté (voir
    # § 5 bis et § 5 quater), et à chaque fois parce qu'un chiffre était rangé
    # ailleurs que la décision qu'il décrit.
    track_distance: Optional[float] = None


@dataclass
class MixDecision:
    track: str
    kept: str                              # libellé de la proposition retenue
    distance_kept: float                   # distance du MÉLANGE
    rejected: List[Tuple[str, float]]      # (libellé, distance) des écartées
    # Distance de PISTE de ce qui a été retenu, ou None si l'état courant l'a
    # emporté (l'appelant sait alors qu'il n'a rien à changer).
    kept_track_distance: Optional[float] = None
    # LE TÉMOIN DE COUPURE : ce que vaut le morceau SANS cette piste du tout.
    # Ce n'est PAS une candidate, et le corps de `keep_what_helps_the_mix` dit
    # pourquoi. C'est le repère sans lequel on ne sait pas si la piste rapporte
    # quoi que ce soit.
    muted_distance: Optional[float] = None


def settle_verdict(tracks: Sequence[ExportTrack], run_pass, max_rounds: int):
    """H5 (§ 5 duodecies) : rejoue la passe de verdict jusqu'au POINT FIXE.

    La passe est gloutonne, piste par piste dans un ordre fixe, et chaque
    décision fait le contexte des suivantes — deux viviers de candidates ont
    mené sur *Us and Them* à deux trajectoires dont la moins bonne au global
    contenait pourtant les meilleures pistes au stem. On rejoue donc
    `run_pass()` (qui MODIFIE les pistes en place et rend les décisions)
    jusqu'à ce qu'un tour ne change ni machine, ni patch, ni profil d'aucune
    piste, borné par `max_rounds`. Un tour qui ne change rien EST le point
    fixe : on s'arrête là, le chiffre d'un tour de plus ne dirait rien.

    Rend (décisions du dernier tour, tours joués, pistes changées par tour).
    """
    max_rounds = max(1, int(max_rounds))
    decisions: List[MixDecision] = []
    changed_by_round: List[List[str]] = []
    rounds = 0
    for _ in range(max_rounds):
        before = {t.name: (t.machine, dict(t.parameters), str(t.profile))
                  for t in tracks}
        decisions = run_pass()
        rounds += 1
        changed = [t.name for t in tracks
                   if t.name in before
                   and before[t.name] != (t.machine, dict(t.parameters), str(t.profile))]
        changed_by_round.append(changed)
        if not changed:
            break
    return decisions, rounds, changed_by_round


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
    alternatives: Dict[str, Sequence[MixAlternative]],
    mixture: np.ndarray,
    stems_audio: Dict[str, np.ndarray],
    samples_root: Path,
    workdir: Path,
    sample_rate: int,
    metric: str = "v2",
    tempo: float = 120.0,
    binary: Optional[str] = None,
    profiles: Optional[Dict[str, str]] = None,
    groupes: Optional[Dict[str, str]] = None,
) -> List[MixDecision]:
    """
    Tranche piste par piste entre l'état courant et ses concurrentes.

    `tracks` est modifié EN PLACE : à la sortie, chaque piste porte la machine,
    le patch et le volume de la variante retenue. `alternatives` donne, par nom
    de piste, les propositions à mettre en concurrence.

    UNE ALTERNATIVE PEUT CHANGER LA MACHINE, et c'est nouveau. Le verdict ne
    savait défaire qu'un RÉGLAGE : sa seule concurrente était le patch d'avant
    réglage de la même machine. Mesuré sur Children v11, cette limite coûte
    cher -- l'arbitrage y a départagé `vsm.ms20` et `vsm.string` à un MILLIÈME
    (0,260 contre 0,261), s'est trompé, et plus rien en aval ne pouvait le
    rattraper : 0,2976 au lieu de 0,2815. Une égalité mal tranchée était
    définitive. Elle ne l'est plus.

    Renvoie une décision par piste examinée, avec ce qui a été gardé ET tout ce
    qui a été écarté, chiffres à l'appui.
    """
    from .vsm_distance_cache import cached_distance_for

    # Le MORCEAU est la cible, et il ne change pas : mis en cache comme partout
    # ailleurs dans la chaîne.
    fabrique = cached_distance_for(metric)
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
        propositions = list(alternatives.get(track.name) or ())
        if not propositions:
            continue

        etat_courant = (track.machine, dict(track.parameters), float(track.volume),
                        str(track.profile), list(track.notes))
        # Les VOLUMES de toutes les pistes, parce qu'un recalage de groupe
        # touche les sœurs de la piste jugée : une variante écartée doit
        # rendre leurs volumes aussi.
        volumes_courants = {t.name: float(t.volume) for t in tracks}
        d_courant = distance_du_projet()
        meilleur = ("réglage", d_courant, etat_courant, None, volumes_courants)
        ecartees: List[Tuple[str, float]] = []

        for proposition in propositions:
            machine_visee = proposition.machine or etat_courant[0]
            if (machine_visee == etat_courant[0]
                    and dict(proposition.parameters) == etat_courant[1]):
                continue                       # rien à départager

            track.machine = machine_visee
            track.parameters = dict(proposition.parameters)
            track.notes = (list(proposition.notes) if proposition.notes is not None
                           else list(etat_courant[4]))
            # Le PROFIL suit la proposition d'abord (l'arbitrage par profil en
            # met un par candidate), la machine ensuite, l'état courant enfin :
            # une proposition « paramètres seuls » qui retomberait sur le
            # premier profil installé écraserait celui que l'arbitrage a
            # choisi. Et une piste qui deviendrait `vsm.multisample` avec un
            # profil vide rendrait du silence, que le verdict compterait comme
            # un résultat.
            if proposition.profile:
                track.profile = proposition.profile
            elif proposition.machine and machine_visee != etat_courant[0]:
                track.profile = (profiles or {}).get(machine_visee, "")
            else:
                track.profile = etat_courant[3]
            # Le nom d'affichage suit la machine, sinon le projet annoncerait
            # l'ancienne dans son interface.
            if machine_visee != etat_courant[0]:
                track.machine_display_name = ""
            # Le VOLUME est recalé pour chaque variante : deux patchs de
            # niveaux différents comparés au même volume ne compareraient pas
            # les patchs.
            recaler_avec_son_groupe(track, tracks, stems_audio, samples_root, sample_rate, groupes)
            distance = distance_du_projet()

            if distance < meilleur[1] - 1e-6:
                ecartees.append((meilleur[0], meilleur[1]))
                meilleur = (proposition.label, distance,
                            (track.machine, dict(track.parameters), float(track.volume),
                             str(track.profile), list(track.notes)),
                            proposition.track_distance,
                            {t.name: float(t.volume) for t in tracks})
            else:
                ecartees.append((proposition.label, distance))

        track.machine, track.parameters, track.volume, track.profile = (
            meilleur[2][0], dict(meilleur[2][1]), meilleur[2][2], meilleur[2][3])
        for t in tracks:
            if t.name in meilleur[4]:
                t.volume = meilleur[4][t.name]
        track.notes = list(meilleur[2][4])
        if track.machine != etat_courant[0]:
            track.machine_display_name = ""

        # LE TÉMOIN DE COUPURE, MESURÉ ET PUBLIÉ, JAMAIS JOUÉ.
        #
        # Ce que vaut le morceau sans cette piste du tout. Il n'entre pas en
        # concurrence avec les autres, et c'est délibéré : une chaîne autorisée
        # à supprimer une piste optimiserait la métrique en abandonnant le
        # morceau -- elle rendrait un *Sky and Sand* sans basse, qu'aucune
        # oreille n'accepterait. La décision de couper reste humaine ; ce qui
        # ne peut pas rester tu, c'est le CHIFFRE.
        #
        # Il ne coûte rien à établir et il manquait cruellement : mesuré après
        # coup (§ 5 decies), la basse publiée de *Sky and Sand* valait 0,2933
        # quand le morceau SANS elle valait 0,2781. La chaîne ajoutait un
        # instrument qui la dégradait de 5,5 %, et aucune pièce n'était en
        # mesure de le remarquer, faute de ce repère-là.
        volume_retenu = float(track.volume)
        track.volume = 0.0
        muet = distance_du_projet()
        track.volume = volume_retenu
        if muet < meilleur[1] - 1e-6:
            print(f"      {track.name:8s} : ATTENTION — le morceau est MEILLEUR "
                  f"sans cette piste ({muet:.4f} contre {meilleur[1]:.4f}). "
                  f"Elle est conservée : couper est une décision humaine.")

        decisions.append(MixDecision(track.name, meilleur[0], meilleur[1], ecartees,
                                     meilleur[3], muet))

    return decisions
