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
from .vsm_levels import recaler_avec_son_groupe
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
    # B11 : LE MÊME TÉMOIN, REJOUÉ SUR LE PROJET FINAL. Celui du dessus est
    # mesuré au moment où la piste est jugée, contre un projet que les pistes
    # SUIVANTES vont encore changer : sur « B4 Wuz Then », les deux témoins
    # comparaient 0,1679 et 0,1691 à 0,1889 — la distance d'alors — quand le
    # projet livré valait 0,1688. L'avis « le morceau est meilleur sans cette
    # piste » était donc rendu contre un état dépassé, et pouvait avoir cessé
    # d'être vrai. Ces deux champs-ci portent le verdict FINAL.
    muted_distance_final: Optional[float] = None
    reference_final: Optional[float] = None


# CE QUI N'A PAS CHANGÉ DEPUIS LE TOUR PRÉCÉDENT N'EST PAS REDIT. Le verdict
# rejoue sa passe jusqu'au point fixe (`settle_verdict`), et chaque passe
# mesurait ET imprimait « le morceau est MEILLEUR sans cette piste » : sur
# sky-parite, la même phrase avec les mêmes chiffres sortait deux fois de
# suite (CDC multipiste § 8, « une fois de trop »). Le chiffre est toujours
# mesuré et toujours publié dans le rapport ; au journal, il n'est redit que
# s'il a bougé.
_deja_dit: Dict[str, Tuple[float, float]] = {}


TrackState = Tuple[str, Dict[str, float], float, str, List[ExportNote]]


def piste_jouante(track: ExportTrack) -> bool:
    """Cette piste met-elle du son dans le melange par elle-meme ?

    H27 (CDC multipiste § 12.4). Le temoin de coupure etait pose DANS la boucle
    des alternatives, si bien qu'une piste sans machine suivante -- une piste
    AUDIO n'a pas de machine, donc pas de suivante -- sortait du verdict tout
    entier. Mesure de l'epreuve Children, course 3 : deux pistes audio (la voix
    reportee, tete et choeurs) sont entrees au melange, l'ont degrade de 2,39 %
    (0,1935 -> 0,1982), et AUCUNE ligne ne l'a dit.

    UN BUS N'EST PAS UNE PISTE JOUANTE : le couper couperait ses membres, dont
    chacun a deja son propre temoin -- le chiffre compterait deux fois la meme
    chose et ne designerait rien a couper.
    """
    if track.is_group:
        return False
    if track.audio_path:
        return True
    return bool(track.machine) and bool(track.notes)


def _temoin_de_coupure(track: ExportTrack, distance_du_projet) -> float:
    """LE TEMOIN DE COUPURE, MESURE ET PUBLIE, JAMAIS JOUE.

    Ce que vaut le morceau sans cette piste du tout. Il n'entre pas en
    concurrence avec les autres, et c'est delibere : une chaine autorisee a
    supprimer une piste optimiserait la metrique en abandonnant le morceau --
    elle rendrait un *Sky and Sand* sans basse, qu'aucune oreille n'accepterait.
    La decision de couper reste humaine ; ce qui ne peut pas rester tu, c'est le
    CHIFFRE.

    Il ne coute rien a etablir et il manquait cruellement : mesure apres coup
    (§ 5 decies), la basse publiee de *Sky and Sand* valait 0,2933 quand le
    morceau SANS elle valait 0,2781. La chaine ajoutait un instrument qui la
    degradait de 5,5 %, et aucune piece n'etait en mesure de le remarquer, faute
    de ce repere-la.

    H27 : ICI plutot que dans la boucle des alternatives, pour que les deux
    chemins -- avec et sans machine suivante -- mesurent la MEME chose.
    """
    volume_retenu = float(track.volume)
    track.volume = 0.0
    muet = distance_du_projet()
    track.volume = volume_retenu
    return muet


def _dire_si_meilleur_sans(nom: str, muet: float, reference: float) -> None:
    """La phrase du journal, ecrite en UN seul endroit (H27).

    Deux chemins l'emettent desormais ; deux copies auraient fini par ne plus
    dire la meme chose.
    """
    if muet >= reference - 1e-6:
        return
    chiffres = (round(muet, 4), round(reference, 4))
    if _deja_dit.get(nom) == chiffres:
        return
    _deja_dit[nom] = chiffres
    print(f"      {nom:8s} : ATTENTION — le morceau est MEILLEUR "
          f"sans cette piste ({muet:.4f} contre {reference:.4f}). "
          f"Elle est conservée : couper est une décision humaine.")

# Le meilleur en lice : libellé, distance du MÉLANGE, état à remettre en place
# si on l'abandonne, distance de PISTE (inconnue pour le réglage courant, qui
# n'a pas concouru sur la piste) et volumes de toutes les pistes.
Meilleur = Tuple[str, float, TrackState, Optional[float], Dict[str, float]]


def track_state(track: ExportTrack) -> TrackState:
    """L'état d'une piste tel que le verdict le photographie : machine, patch,
    volume, profil, notes -- ce qu'il faut pour la remettre comme elle était."""
    return (track.machine, dict(track.parameters), float(track.volume),
            str(track.profile), list(track.notes))


def restore_track_state(track: ExportTrack, state: TrackState, machine_avant: str) -> None:
    track.machine, track.parameters, track.volume, track.profile = (
        state[0], dict(state[1]), state[2], state[3])
    track.notes = list(state[4])
    if track.machine != machine_avant:
        track.machine_display_name = ""


def install_alternative(track: ExportTrack, tracks: Sequence[ExportTrack],
                        proposition: MixAlternative, etat_courant: TrackState,
                        stems_audio: Dict[str, np.ndarray], samples_root: Path,
                        sample_rate: int, profiles: Optional[Dict[str, str]],
                        groupes: Optional[Dict[str, str]]) -> None:
    """Pose une proposition sur la piste, EN PLACE, volume recalé avec son
    groupe. Factorisé hors de `keep_what_helps_the_mix` pour que le second
    verdict (campagne 7) installe une candidate exactement comme le premier."""
    machine_visee = proposition.machine or etat_courant[0]
    track.machine = machine_visee
    track.parameters = dict(proposition.parameters)
    track.notes = (list(proposition.notes) if proposition.notes is not None
                   else list(etat_courant[4]))
    # Le PROFIL suit la proposition d'abord (l'arbitrage par profil en met un
    # par candidate), la machine ensuite, l'état courant enfin : une
    # proposition « paramètres seuls » qui retomberait sur le premier profil
    # installé écraserait celui que l'arbitrage a choisi. Et une piste qui
    # deviendrait `vsm.multisample` avec un profil vide rendrait du silence,
    # que le verdict compterait comme un résultat.
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
    # Le VOLUME est recalé pour chaque variante : deux patchs de niveaux
    # différents comparés au même volume ne compareraient pas les patchs.
    recaler_avec_son_groupe(track, tracks, stems_audio, samples_root, sample_rate, groupes)


def project_mix_distance(tracks: Sequence[ExportTrack], mixture: np.ndarray, samples_root: Path,
                         workdir: Path, sample_rate: int, metric: str = "v2",
                         tempo: float = 120.0, binary: Optional[str] = None) -> float:
    """La distance du PROJET rendu au morceau, telle que le verdict la mesure
    -- même dossier de variante, mêmes échantillons recopiés une fois."""
    from .vsm_distance_cache import cached_distance_for
    mesurer = cached_distance_for(metric)(np.asarray(mixture), sample_rate)
    dossier = Path(workdir) / "variante"
    dossier.mkdir(parents=True, exist_ok=True)
    _copy_samples(tracks, samples_root, dossier)
    rendu = _render_project(tracks, dossier, sample_rate, tempo, binary)
    if rendu is None or rendu.size == 0:
        return float("inf")
    return float(mesurer(rendu))


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
    _deja_dit.clear()
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

    # H27 : la distance du projet TELLE QU'IL EST à la fin de la derniere
    # iteration. Elle sert de reference aux pistes sans alternative, qui n'ont
    # rien a departager : sans elle, il faudrait un rendu de plus par piste pour
    # remesurer un chiffre que l'on vient d'etablir.
    derniere_distance: Optional[float] = None
    for track in tracks:
        propositions = list(alternatives.get(track.name) or ())
        if not propositions:
            # H27 : PAS DE MACHINE SUIVANTE N'EST PAS PAS DE VERDICT. Une piste
            # qui met du son dans le melange recoit son temoin de coupure meme
            # si rien ne lui est propose -- c'est le chiffre, pas le choix, qui
            # manquait au musicien.
            if not piste_jouante(track):
                continue
            reference = derniere_distance if derniere_distance is not None else distance_du_projet()
            derniere_distance = reference
            muet = _temoin_de_coupure(track, distance_du_projet)
            _dire_si_meilleur_sans(track.name, muet, reference)
            decisions.append(MixDecision(track.name, "inchangée (aucune machine suivante)",
                                          reference, [], None, muet))
            continue

        etat_courant = (track.machine, dict(track.parameters), float(track.volume),
                        str(track.profile), list(track.notes))
        # Les VOLUMES de toutes les pistes, parce qu'un recalage de groupe
        # touche les sœurs de la piste jugée : une variante écartée doit
        # rendre leurs volumes aussi.
        volumes_courants = {t.name: float(t.volume) for t in tracks}
        d_courant = distance_du_projet()
        meilleur: Meilleur = ("réglage", d_courant, etat_courant, None, volumes_courants)
        ecartees: List[Tuple[str, float]] = []

        for proposition in propositions:
            machine_visee = proposition.machine or etat_courant[0]
            if (machine_visee == etat_courant[0]
                    and dict(proposition.parameters) == etat_courant[1]):
                continue                       # rien à départager

            install_alternative(track, tracks, proposition, etat_courant,
                                stems_audio, samples_root, sample_rate, profiles, groupes)
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

        muet = _temoin_de_coupure(track, distance_du_projet)
        _dire_si_meilleur_sans(track.name, muet, meilleur[1])

        decisions.append(MixDecision(track.name, meilleur[0], meilleur[1], ecartees,
                                     meilleur[3], muet))
        derniere_distance = meilleur[1]

    _rejouer_les_temoins(tracks, decisions, distance_du_projet)
    return decisions


def _rejouer_les_temoins(tracks: Sequence[ExportTrack], decisions: List[MixDecision],
                          distance_du_projet) -> None:
    """B11 -- LE TEMOIN DE COUPURE, REJOUE SUR LE PROJET FINAL, ET PAR PAIRES.

    DEUX DEFAUTS, NOMMES DANS `CDC-detection-multipiste.md` § 13 :

    1. **Le temoin etait mesure trop tot.** Il est pris pendant la boucle,
       contre le projet TEL QU'IL EST A CET INSTANT ; les pistes jugees apres
       le changent encore. Sur « B4 Wuz Then », deux pistes se comparaient a
       0,1889 quand le projet livre valait 0,1688 : un avis rendu contre un
       etat depasse, qui peut avoir cesse d'etre vrai -- et dans ce sens-la, il
       a cesse de l'etre (le projet s'est ameliore de 10 %, donc il est plus
       difficile de faire mieux sans une piste).
    2. **Couper deux pistes n'est pas la somme de deux coupes.** Les
       combinaisons n'etaient jamais essayees. Deux pistes qui se recouvrent
       peuvent chacune paraitre inutile sans que les couper toutes les deux
       aide -- ou l'inverse.

    CE QUE CETTE FONCTION NE FAIT PAS : couper. Elle mesure et publie, comme le
    temoin simple. La decision reste humaine, et la regle du depot est que la
    chaine ne coupe jamais elle-meme.

    LE COUT EST BORNE : un rendu pour le projet final, un par piste jouante,
    puis un par paire de pistes SIGNALEES (jamais toutes les paires -- sur dix
    pistes, ce serait quarante-cinq rendus pour une question que personne ne
    pose). Les signalees se comptent sur les doigts d'une main.
    """
    jouantes = [t for t in tracks if piste_jouante(t)]
    if not jouantes:
        return
    par_nom = {t.name: t for t in jouantes}
    reference = distance_du_projet()

    signalees: List[Tuple[str, float]] = []
    for decision in decisions:
        piste = par_nom.get(decision.track)
        if piste is None:
            continue
        muet = _temoin_de_coupure(piste, distance_du_projet)
        decision.muted_distance_final = muet
        decision.reference_final = reference
        # CE QUI A CHANGE D'AVIS EST DIT, et c'est la moitie de l'interet de
        # cette passe : un verdict provisoire qui ne tient plus est une panne
        # muette s'il reste au rapport sans etre corrige.
        avant = decision.muted_distance
        provisoire = avant is not None and avant < decision.distance_kept - 1e-6
        final = muet < reference - 1e-6
        if provisoire != final and avant is not None:
            print(f"      {decision.track:8s} : le témoin de coupure CHANGE D'AVIS sur le projet "
                  f"final — « meilleur sans » au jugement "
                  f"({avant:.4f} contre {decision.distance_kept:.4f}) : "
                  f"{'OUI' if provisoire else 'non'} ; sur le projet livré "
                  f"({muet:.4f} contre {reference:.4f}) : "
                  f"{'OUI' if final else 'non'}.")
        if final:
            signalees.append((decision.track, muet))
            # LE NOM NU, ET NON « … (final) » : `_dire_si_meilleur_sans` tait
            # ce qui n'a pas bougé depuis le dernier tour (la regle ecrite au
            # dessus de `_deja_dit`), et un suffixe en ferait une autre piste —
            # la meme phrase avec les memes chiffres sortirait deux fois. Ainsi,
            # la ligne finale ne parait que si les chiffres ont CHANGE.
            _dire_si_meilleur_sans(decision.track, muet, reference)

    if len(signalees) < 2:
        return
    # LES PAIRES, PARMI LES SEULES SIGNALEES. Le chiffre publie repond a la
    # question qu'un musicien se pose quand deux pistes sont montrees du doigt :
    # « et si je les coupais toutes les deux ? »
    print(f"      témoin de coupure : {len(signalees)} piste(s) signalée(s), "
          f"{len(signalees) * (len(signalees) - 1) // 2} paire(s) essayée(s)")
    for i in range(len(signalees)):
        for j in range(i + 1, len(signalees)):
            a_nom, a_muet = signalees[i]
            b_nom, b_muet = signalees[j]
            pa, pb = par_nom[a_nom], par_nom[b_nom]
            va, vb = float(pa.volume), float(pb.volume)
            pa.volume = 0.0
            pb.volume = 0.0
            ensemble = distance_du_projet()
            pa.volume, pb.volume = va, vb
            # LA SOMME DES DEUX COUPES N'EST PAS LA COUPE DES DEUX : on publie
            # l'ecart entre ce qu'on aurait predit (le meilleur des deux seuls)
            # et ce qu'on mesure, parce que c'est lui qui dit si la question
            # valait la peine d'etre posee.
            attendu = min(a_muet, b_muet)
            mieux = "la paire fait MIEUX" if ensemble < attendu - 1e-6 else "la paire ne fait pas mieux"
            print(f"      {a_nom} + {b_nom} : sans les deux {ensemble:.4f}, "
                  f"meilleur des deux seuls {attendu:.4f}, "
                  f"projet livré {reference:.4f} — {mieux} que la meilleure coupe seule.")
