"""
Reconstruction complète : fichier audio -> projet VSM rejouable.

C'est le critère de réussite de la phase 9 de la feuille de route, enfin
mesurable de bout en bout :

    original.wav -> analyse -> (MIDI + presets + project.json)
                 -> vsm-render -> reconstruit.wav
                 -> distance publiée

DIFFÉRENCE AVEC `patch_pipeline` : celui-ci optimise les réglages d'un synthé
Python approximatif, qu'il faut ensuite TRADUIRE vers une machine réelle -- et
la traduction dégrade ce qu'on vient d'optimiser. Ici, chaque candidat est
rendu par la machine visée elle-même, via le moteur du DAW. Le patch obtenu
est directement chargeable : ce qu'on a optimisé est exactement ce qu'on
entendra.

CE QUE CE MODULE NE PRÉTEND PAS FAIRE. Il reconstruit un morceau avec les
machines du parc, ce qui n'est pas la même chose que le reproduire. Un
enregistrement de guitare acoustique n'a pas de machine cible ; la distance
publiée le dira, et c'est justement pour cela qu'elle est publiée.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import numpy as np

from .audio_distance import audio_distance
from .audio_distance_v2 import audio_distance_v2
from .vsm_engine import (
    _MACHINES_A_PROFIL,
    VsmEngine,
    VsmEngineError,
    available_machines,
)
from .vsm_levels import VOLUME_MAX
from .vsm_patch_optimizer import choose_machine, optimize_patch_for_machine
from .vsm_project_export import DEFAULT_TRACK_VOLUME
from .vsm_project_export import ExportNote, ExportTrack, write_project_bundle

# Machines proposées par défaut pour un stem MÉLODIQUE. La liste exclut les
# boîtes à rythmes et le sampler : on ne cherche pas le timbre d'un kit par
# optimisation, on lui fournit des échantillons (c'est le rôle du stem de
# batterie, traité à part).
#
# Elle n'est PAS écrite en dur au sens où on l'entend d'habitude : elle est
# filtrée à partir de ce que le moteur déclare savoir instancier, si bien
# qu'une machine ajoutée au DAW entre automatiquement dans le choix.
_NON_MELODIC = {"vsm.testtone", "vsm.tr808", "vsm.tr909", "vsm.sampler", "vsm.drums"}


@dataclass
class StemNote:
    """Une note détectée dans un stem, en secondes."""
    note: int
    velocity: int
    start: float
    duration: float
    # CONFIANCE de la transcription, entre 0 et 1. Une note franche vaut 1 ;
    # une note devinée au milieu d'un accord ou d'un bruit vaut moins. C'est
    # elle qui, remontée jusqu'au piano roll, dit à l'utilisateur OÙ la
    # transcription a hésité -- au lieu de le laisser réécouter tout le morceau
    # en cherchant ce qui cloche.
    confidence: float = 1.0


@dataclass
class StemReconstruction:
    name: str
    machine: str
    parameters: Dict[str, float]
    distance: float
    notes: List[StemNote]
    # (machine, distance) pour TOUTES les candidates, pas seulement la retenue :
    # un choix sans ses concurrents n'est pas un choix, c'est une affirmation.
    considered: List[tuple]
    is_drums: bool = False
    # NOM du profil multi-échantillons employé, pour les machines qui en
    # exigent un. Inscrit dans le projet exporté par son NOM et non par un
    # chemin : le profil pèse deux cents mégaoctets, on ne le recopie pas dans
    # chaque projet, et un chemin absolu ne s'ouvrirait que sur le poste qui
    # l'a écrit.
    profile: str = ""
    # AVIS du classifieur appris, consigné dans le rapport. Il ne change rien
    # au verdict par défaut : le publier à côté de la mesure est justement ce
    # qui permettra, un jour, de savoir s'il mérite qu'on le suive.
    classifier_ranking: List[tuple] = field(default_factory=list)
    classifier_abstention: str = ""

    # `gate` : la proportion de l'extrait pendant laquelle la note de référence
    # était TENUE. Conservé parce qu'il conditionne la distance au même titre
    # que la métrique et le budget -- mesuré sur un violoncelle à l'archet, le
    # faire passer de 0,95 à sa vraie valeur 0,24 change la distance d'un
    # facteur 1,6 et INVERSE le classement des machines, sans toucher une
    # ligne de DSP (ARCHITECTURE.md § 32). Deux distances obtenues à des
    # `gate` différents ne se comparent pas.
    gate: Optional[float] = None
    # Le patch trouvé pour CHAQUE candidate, et pas seulement pour la gagnante.
    # C'est ce qui permet à l'arbitrage sur la piste entière de rejuger toutes
    # les propositions au lieu de la seule qu'un critère « une note » avait
    # retenue -- et ce critère, mesuré, ne classe pas dans le même ordre que la
    # piste (voir `vsm_track_arbitration`).
    patches: Dict[str, Dict[str, float]] = field(default_factory=dict)
    # Ce qu'a donné l'arbitrage sur la piste entière, quand il a eu lieu.
    # `distance` reste la distance SUR UNE NOTE : les deux ne se comparent pas,
    # et les confondre serait exactement le défaut que le § 32 décrit.
    track_distance: Optional[float] = None
    track_considered: List[tuple] = field(default_factory=list)
    # Le patch d'AVANT le réglage sur la piste. Conservé parce que le verdict
    # du mélange doit pouvoir revenir dessus : un réglage qui rapproche la piste
    # peut éloigner le morceau.
    #
    # Il vient de l'arbitrage quand celui-ci a rendu un verdict -- d'où le nom
    # -- mais PLUS SEULEMENT : le réglage sur la piste ne dépend plus de
    # l'arbitrage, donc avec `--sans-arbitrage`, ou sur un stem que l'arbitrage
    # n'a pas su départager, ce champ porte le patch de la RECHERCHE. Dans les
    # deux cas il dit la même chose -- « l'état d'où le réglage est parti » --
    # et c'est ce que le verdict du mélange doit pouvoir reprendre.
    arbitration_parameters: Optional[Dict[str, float]] = None
    # La distance de piste qui va AVEC `arbitration_parameters`. Sans elle, un
    # patch repris par le verdict du mélange se retrouvait publié avec la
    # distance de celui qu'on venait d'écarter.
    arbitration_distance: Optional[float] = None


def melodic_machines(engine: VsmEngine) -> List[str]:
    """Machines mélodiques que le moteur sait réellement instancier.

    Une machine à PROFIL (`vsm.multisample`) n'entre dans la liste que si un
    profil est installé, et son absence est DITE. Sans profil, elle rendrait du
    silence : elle ne perdrait pas la comparaison, elle la fausserait, en
    gagnant sur toutes les cibles douces. Le § 7 du cahier des charges de la
    machine demande exactement cela -- « absente des candidates en le disant ».
    """
    machines = [m for m in available_machines(engine) if m not in _NON_MELODIC]

    utilisables = []
    for machine in machines:
        if machine in _MACHINES_A_PROFIL and engine is not None:
            if engine.profile_for(machine) is None:
                print(f"  {machine} écartée : aucun profil installé "
                      f"(voir tools/installer-profil-piano.py)")
                continue
        utilisables.append(machine)
    return utilisables


def _representative_note(
    audio: np.ndarray,
    notes: Sequence[StemNote],
    sample_rate: int,
) -> Optional[tuple]:
    """
    Choisit UNE note sur laquelle chercher le patch, et rend son extrait
    ainsi que la proportion de celui-ci pendant laquelle la note est tenue.

    Pourquoi une seule : chercher sur chaque note d'un morceau coûterait des
    heures, et les notes d'un même instrument partagent leur timbre -- c'est
    l'hypothèse de travail, et elle est explicite. On prend la note la plus
    LONGUE : c'est celle qui montre le mieux l'entretien et l'extinction, là
    où une note brève ne montre que l'attaque.
    """
    usable = [n for n in notes if n.duration > 0.15]
    if not usable:
        return None

    # LA NOTE DOIT SONNER, et il a fallu un morceau réel pour s'en apercevoir.
    #
    # Sur la basse de *B4 Wuz Then*, la note la plus longue était une « note »
    # de 3,68 s à MIDI 34 dans les premières secondes, là où le stem est
    # SILENCIEUX : RMS 0,00008, onze cents fois sous le niveau du stem. Un
    # artefact de transcription — Basic Pitch a entendu un grave tenu dans un
    # souffle de séparation. Toute la recherche par note, sur les vingt
    # machines, s'est donc faite contre du silence ; un patch muet l'a gagnée
    # (`vsm.wind`, 0,2455, le meilleur des vingt) ; et il était quarante-deux
    # fois trop faible sur la piste. La borne de niveau de la recherche ne peut
    # rien contre une cible muette : la cible était fausse, pas le candidat.
    #
    # On ne retient donc que les notes pendant lesquelles le stem SONNE — au
    # moins un dixième de son niveau efficace global. Le seuil est large : il
    # écarte le silence, pas une note douce. Et c'est la panne muette n° 6
    # (ROADMAP-fusion § 5 bis) : aucun message, un chiffre plausible, et un
    # stem entier jugé sur une erreur.
    stem_rms = float(np.sqrt(np.mean(np.asarray(audio, dtype=np.float64) ** 2)))
    if stem_rms > 0.0:
        def sonne(n: StemNote) -> bool:
            debut = int(n.start * sample_rate)
            fin = min(len(audio), int((n.start + n.duration) * sample_rate))
            if fin - debut < 8:
                return False
            segment = np.asarray(audio[debut:fin], dtype=np.float64)
            return float(np.sqrt(np.mean(segment ** 2))) >= 0.1 * stem_rms

        sonores = [n for n in usable if sonne(n)]
        if len(sonores) < len(usable):
            ecartees = len(usable) - len(sonores)
            plus_longue = max(usable, key=lambda n: n.duration)
            if plus_longue not in sonores:
                print(f"      note de référence : la plus longue ({plus_longue.duration:.2f} s, "
                      f"MIDI {plus_longue.note}) tombe dans un SILENCE du stem — écartée, "
                      f"avec {ecartees - 1} autre(s)")
        if sonores:
            usable = sonores
    chosen = max(usable, key=lambda n: n.duration)

    # L'EXTRAIT DÉBORDE APRÈS LE RELÂCHEMENT, et ce n'est pas un détail.
    #
    # La phase d'extinction est l'un des traits les plus caractéristiques d'une
    # machine : deux synthés peuvent tenir la même note de la même façon et
    # s'éteindre tout autrement. Couper l'extrait au relâchement supprimait
    # donc précisément ce qui les sépare -- et la recherche retenait un
    # Minimoog (0,083) là où le Juno visé faisait 0,130, alors que sur la même
    # cible AVEC son extinction le Juno l'emporte largement (0,011 contre
    # 0,062, confirmé par un juge indépendant : 0,21 dB d'écart de
    # spectrogramme contre 6,18).
    #
    # Le débordement s'arrête à la note SUIVANTE : au-delà, on comparerait
    # deux notes au lieu d'une.
    queue = 0.6
    suivantes = [n.start for n in notes if n.start > chosen.start + 1e-6]
    if suivantes:
        queue = max(0.0, min(queue, min(suivantes) - (chosen.start + chosen.duration)))

    start = int(chosen.start * sample_rate)
    # On prend au plus une seconde et demie : au-delà, la distance mesure
    # surtout du silence.
    duree = min(chosen.duration + queue, 1.5)
    length = int(duree * sample_rate)
    excerpt = audio[start:start + length]
    if excerpt.size < sample_rate // 10:
        return None
    # `gate` est une PROPORTION de l'extrait : elle ne peut pas dépasser 1.
    #
    # Sans cette borne elle le pouvait, et c'est le fait de l'INSCRIRE dans le
    # rapport qui l'a révélé -- première exécution, premier stem, `gate` à
    # 1,9978. La cause : l'extrait est plafonné à 1,5 s, mais pas la durée de
    # la note choisie ; une note de trois secondes donnait donc « tenue pendant
    # 200 % de l'extrait ». Le sens s'inverse : au-delà de 1, la note est tenue
    # PENDANT TOUT l'extrait, ce que 1 exprime déjà.
    gate = float(chosen.duration) / max(1e-6, duree)
    return chosen, excerpt, min(1.0, gate)


def reconstruct_stem(
    name: str,
    audio: np.ndarray,
    notes: Sequence[StemNote],
    engine: VsmEngine,
    sample_rate: int = 44100,
    machines: Optional[Sequence[str]] = None,
    max_iterations: int = 20,
    max_dimensions: Optional[int] = None,
    metric: str = "v2",
    shortlist: Optional[int] = None,
    classifieur: Optional[object] = None,
    preselection_apprise: int = 0,
    level_bound: bool = True,
) -> Optional[StemReconstruction]:
    """
    Trouve la machine et le patch qui approchent le mieux ce stem.

    Renvoie None si le stem ne contient aucune note exploitable -- ce qui est
    dit, jamais deviné : un stem muet ne doit pas produire une piste au hasard.
    """
    picked = _representative_note(audio, notes, sample_rate)
    if picked is None:
        return None
    # `gate` : la proportion de l'extrait pendant laquelle la note est tenue.
    # Il est TRANSMIS à la recherche, sans quoi les candidats seraient rendus
    # avec un relâchement à 75 % face à une cible qui tient sa note à 95 % --
    # et l'optimiseur fausserait les enveloppes pour rattraper un décalage
    # qu'il se serait infligé lui-même.
    reference, excerpt, gate = picked

    candidates = list(machines) if machines else melodic_machines(engine)

    # AVIS DU CLASSIFIEUR (phase A1). Par défaut il est CONSIGNÉ, pas suivi :
    # mesuré sur Clair de Lune, il place `vsm.piano` — la machine que
    # l'arbitrage retient réellement — au rang médian 16 sur 20. Un
    # dégrossissage sur son classement aurait éliminé la gagnante.
    #
    # Le publier quand même a un intérêt : chaque exécution accumule une
    # comparaison entre ce que le modèle croit et ce que la mesure trouve, et
    # c'est de ces comparaisons-là que sortira le jour où on pourra s'y fier.
    # Le suivre reste possible (`preselection_apprise`), à la main, et il est
    # dit que la mesure ne le recommande pas.
    avis_classement: List[tuple] = []
    avis_abstention = ""
    if classifieur is not None:
        try:
            from .vsm_corpus import descriptors

            vecteur = descriptors(excerpt, sample_rate, reference.note, gate,
                                   len(excerpt) / float(sample_rate))
            avis_classement, avis_abstention = classifieur.classe(vecteur, k=len(candidates))
        except Exception as erreur:  # noqa: BLE001 — un avis ne doit jamais faire échouer une reconstruction
            avis_abstention = f"classifieur inutilisable : {type(erreur).__name__}"

        if avis_abstention:
            print(f"      {name:8s} : classifieur — {avis_abstention}")
        else:
            podium = ", ".join(f"{m.split('.')[-1]}={s:.2f}" for m, s in avis_classement[:3])
            print(f"      {name:8s} : classifieur — {podium}")

        if preselection_apprise > 0 and not avis_abstention:
            retenues = [m for m, _ in avis_classement[:preselection_apprise]
                        if m in candidates]
            if retenues:
                ecartees = [m for m in candidates if m not in retenues]
                print(f"      {name:8s} : présélection APPRISE — {len(retenues)} retenue(s), "
                      f"{len(ecartees)} écartée(s) sans être mesurée(s)")
                candidates = retenues

    best, everyone = choose_machine(
        excerpt,
        reference.note,
        engine,
        candidates,
        sample_rate=sample_rate,
        metric=metric,
        gate=gate,
        max_iterations=max_iterations,
        # None laisse `choose_machine` appliquer sa règle par défaut : la
        # moitié des candidates en finale. La passer explicitement sert aux
        # MESURES, où une candidate écartée au dégrossissage n'aurait pas de
        # score comparable aux autres.
        **({"shortlist": shortlist} if shortlist is not None else {}),
        # None laisse `choose_machine` appliquer sa règle à deux étages :
        # 6 axes pour classer, 10 pour régler les finalistes (mesuré).
        **({"max_dimensions": max_dimensions} if max_dimensions is not None else {}),
        # BORNE DE NIVEAU, la même que sur la piste (VOLUME_MAX / volume de
        # base) : un patch qu'il faudrait amplifier davantage ne sera jamais
        # retenu à l'arbitrage, donc le chercher est du budget perdu. Mesuré sur
        # B4 Wuz Then AVANT cette borne : deux gagnants sur deux étaient « trop
        # faibles, ×42 ». `level_bound=False` rend l'ancien comportement, pour
        # l'A/B.
        **({"max_gain": VOLUME_MAX / DEFAULT_TRACK_VOLUME} if level_bound else {}),
    )
    return StemReconstruction(
        name=name,
        machine=best.machine,
        parameters=best.parameters,
        distance=best.distance,
        notes=list(notes),
        considered=[(r.machine, r.distance) for r in everyone],
        gate=gate,
        patches={r.machine: dict(r.parameters) for r in everyone},
        profile=_profile_name(engine, best.machine),
        classifier_ranking=[(m, float(s)) for m, s in avis_classement[:5]],
        classifier_abstention=avis_abstention,
    )


def _profile_name(engine: VsmEngine, machine: str) -> str:
    """Nom du profil employé par cette machine, ou chaîne vide.

    C'est le NOM déclaré par le profil, pas son chemin : c'est lui que le projet
    exporté inscrira, et c'est ce qui permet d'ouvrir le projet sur un autre
    poste pourvu que la banque y soit installée.
    """
    if machine not in _MACHINES_A_PROFIL or engine is None:
        return ""
    chemin = engine.profile_for(machine)
    if not chemin:
        return ""
    for profil in engine.profiles():
        if profil.get("path") == chemin:
            return str(profil.get("name") or "")
    return ""


def reconstruction_distance(
    original: np.ndarray,
    reconstructed: np.ndarray,
    sample_rate: int,
    metric: str = "v2",
) -> float:
    """
    Distance entre l'original et la reconstruction, sur la même durée.

    Les deux signaux sont tronqués à la plus courte longueur : comparer des
    durées différentes ferait passer un simple décalage pour une erreur de
    timbre.

    `metric` EST UN PARAMÈTRE, et il ne l'était pas. Cette fonction appelait
    `audio_distance` -- la v1 -- quoi qu'on ait demandé, pendant que le résumé
    imprimait « métrique v2 » et que le rapport l'inscrivait. Toutes les
    distances GLOBALES publiées jusqu'ici sont donc des v1 mal étiquetées ; les
    distances par stem, elles, ont toujours été celles qu'elles annonçaient.
    C'est exactement le défaut que le § 32 décrit -- un chiffre présenté sous
    des conditions qui ne sont pas les siennes -- et il était dans le code qui
    publie le chiffre le plus visible de toute la chaîne.
    """
    length = min(original.size, reconstructed.size)
    if length == 0:
        return float("inf")
    mesure = audio_distance_v2 if metric == "v2" else audio_distance
    return float(mesure(original[:length], reconstructed[:length], sample_rate))


def write_reconstruction_report(
    stems: Sequence[StemReconstruction],
    path: Path,
    global_distance: Optional[float] = None,
    metric: str = "v2",
    iterations: Optional[int] = None,
) -> None:
    """
    Écrit le rapport de reconstruction (étape 9.3).

    Il dit OÙ la reconstruction échoue, pas seulement de combien : la distance
    par stem, et les machines écartées avec leur score. Sans ce détail, un
    chiffre global ne permet aucune décision.
    """
    document = {
        "format": "vsm-reconstruction-report",
        "version": 1,
        # La métrique est INSCRITE dans le rapport, et ce n'est pas une
        # formalité : les distances de la v1 et de la v2 ne sont pas du même
        # ordre de grandeur et ne se comparent pas. Un rapport qui ne dirait
        # pas laquelle a servi inviterait à confronter des chiffres qui n'ont
        # rien à voir.
        "metric": metric,
        # LE BUDGET DE RECHERCHE, inscrit pour la même raison que la métrique,
        # et pour une raison APPRISE : deux passes sur House Of God ont été
        # comparées stem à stem alors qu'elles n'avaient pas tourné au même
        # budget (60 itérations contre 20). Rien dans les rapports ne le
        # disait, et l'écart -- basse 0,053 contre 0,103 -- s'est d'abord lu
        # comme un non-déterminisme de la chaîne. Il a fallu vérifier la
        # séparation, la transcription, le moteur et l'optimiseur un par un
        # pour retrouver la vraie cause. Le budget est un paramètre de la
        # mesure au même titre que la métrique : deux distances obtenues à
        # des budgets différents ne se comparent pas.
        "iterations": iterations,
        "globalDistance": global_distance,
        "stems": [
            {
                "name": stem.name,
                "machine": stem.machine,
                "distance": stem.distance,
                "notes": len(stem.notes),
                # Troisième condition de la mesure, après la métrique et le
                # budget : voir le commentaire du champ `gate`.
                "gate": None if stem.gate is None else round(float(stem.gate), 4),
                # Confiance NOTE PAR NOTE, appariée côté DAW par hauteur et
                # instant (jamais par position dans la liste : une note
                # ajoutée ou déplacée décalerait tout sans rien signaler).
                "noteConfidence": [
                    {
                        "note": int(note.note),
                        "start": round(float(note.start), 4),
                        "confidence": round(float(note.confidence), 4),
                    }
                    for note in stem.notes
                ],
                # Distance de la PISTE ENTIÈRE au stem, pour la machine
                # retenue -- la seule des deux qui dise ce qu'on entendra.
                # `distance` juste au-dessus porte sur une note.
                "trackDistance": stem.track_distance,
                "trackArbitration": [
                    {"machine": machine, "origin": origin, "distance": distance}
                    for machine, origin, distance in stem.track_considered
                ],
                "consideredMachines": [
                    {"machine": machine, "distance": distance}
                    for machine, distance in sorted(stem.considered, key=lambda x: x[1])
                ],
                "parameters": {k: float(v) for k, v in sorted(stem.parameters.items())},
                # AVIS du classifieur appris, inscrit À CÔTÉ de la mesure et
                # jamais à sa place. C'est l'accumulation de ces comparaisons —
                # ce que le modèle croyait, ce que la mesure a trouvé — qui dira
                # un jour s'il mérite qu'on le suive (§ 8.3 : rien de silencieux).
                **({"classifierRanking": [{"machine": m, "score": s}
                                           for m, s in stem.classifier_ranking]}
                   if stem.classifier_ranking else {}),
                **({"classifierAbstention": stem.classifier_abstention}
                   if stem.classifier_abstention else {}),
            }
            for stem in stems
        ],
    }
    Path(path).write_text(
        json.dumps(document, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )


def export_reconstruction(
    stems: Sequence[StemReconstruction],
    folder: Path,
    title: str = "Reconstruction",
    tempo: float = 120.0,
) -> Dict[str, object]:
    """Met les stems reconstruits en dossier de projet VSM."""
    tracks = [
        ExportTrack(
            name=stem.name,
            machine=stem.machine,
            parameters=stem.parameters,
            notes=[ExportNote(n.note, n.velocity, n.start, n.duration) for n in stem.notes],
            is_drums=stem.is_drums,
            profile=stem.profile,
        )
        for stem in stems
    ]
    return write_project_bundle(tracks, folder, title=title, tempo=tempo)
