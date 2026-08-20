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
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import numpy as np

from .audio_distance import audio_distance
from .vsm_engine import VsmEngine, VsmEngineError, available_machines
from .vsm_patch_optimizer import choose_machine, optimize_patch_for_machine
from .vsm_project_export import ExportNote, ExportTrack, write_project_bundle

# Machines proposées par défaut pour un stem MÉLODIQUE. La liste exclut les
# boîtes à rythmes et le sampler : on ne cherche pas le timbre d'un kit par
# optimisation, on lui fournit des échantillons (c'est le rôle du stem de
# batterie, traité à part).
#
# Elle n'est PAS écrite en dur au sens où on l'entend d'habitude : elle est
# filtrée à partir de ce que le moteur déclare savoir instancier, si bien
# qu'une machine ajoutée au DAW entre automatiquement dans le choix.
_NON_MELODIC = {"vsm.testtone", "vsm.tr808", "vsm.tr909", "vsm.sampler"}


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


def melodic_machines(engine: VsmEngine) -> List[str]:
    """Machines mélodiques que le moteur sait réellement instancier."""
    return [m for m in available_machines(engine) if m not in _NON_MELODIC]


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
    return chosen, excerpt, float(chosen.duration) / max(1e-6, duree)


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
    best, everyone = choose_machine(
        excerpt,
        reference.note,
        engine,
        candidates,
        sample_rate=sample_rate,
        metric=metric,
        gate=gate,
        max_iterations=max_iterations,
        # None laisse `choose_machine` appliquer sa règle à deux étages :
        # 6 axes pour classer, 10 pour régler les finalistes (mesuré).
        **({"max_dimensions": max_dimensions} if max_dimensions is not None else {}),
    )
    return StemReconstruction(
        name=name,
        machine=best.machine,
        parameters=best.parameters,
        distance=best.distance,
        notes=list(notes),
        considered=[(r.machine, r.distance) for r in everyone],
    )


def reconstruction_distance(
    original: np.ndarray,
    reconstructed: np.ndarray,
    sample_rate: int,
) -> float:
    """
    Distance entre l'original et la reconstruction, sur la même durée.

    Les deux signaux sont tronqués à la plus courte longueur : comparer des
    durées différentes ferait passer un simple décalage pour une erreur de
    timbre.
    """
    length = min(original.size, reconstructed.size)
    if length == 0:
        return float("inf")
    return float(audio_distance(original[:length], reconstructed[:length], sample_rate))


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
                "consideredMachines": [
                    {"machine": machine, "distance": distance}
                    for machine, distance in sorted(stem.considered, key=lambda x: x[1])
                ],
                "parameters": {k: float(v) for k, v in sorted(stem.parameters.items())},
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
        )
        for stem in stems
    ]
    return write_project_bundle(tracks, folder, title=title, tempo=tempo)
