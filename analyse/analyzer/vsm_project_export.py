"""
Export d'un morceau analysé en DOSSIER DE PROJET VSM.

C'est l'étape 9.1 de la feuille de route globale, et le chaînon qui manquait :
jusqu'ici la chaîne s'arrêtait à la note. Elle savait, pour un extrait donné,
trouver la machine et le patch les plus proches -- mais rien ne rassemblait ces
résultats en quelque chose que le DAW puisse rejouer.

CE QUE CE MODULE ÉCRIT, et pourquoi ce format :

    projet/
      project.json                    <- pistes, machines, mixage, tempo
      midi/arrangement.mid            <- LES NOTES, une piste par stem
      instruments/track_NN.synth.json <- un patch par piste

Les notes vivent dans le `.mid` et NULLE PART AILLEURS. C'est une règle du
format, pas une commodité : dupliquer les notes dans le JSON créerait deux
vérités qui divergeraient au premier montage. `project.json` ne porte que ce
qu'un fichier MIDI ne sait pas dire -- quelle machine joue quelle piste, avec
quel patch, à quel niveau.

CE QUE CE MODULE NE FAIT PAS : il n'analyse rien et ne cherche aucun patch. Il
reçoit des résultats et les met en forme. Le partage des rôles de la feuille de
route est explicite là-dessus -- l'analyse décide, le DAW rend, et ce module
n'est que le pont entre les deux.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import mido

# Résolution du fichier MIDI. 480 est la valeur du format VSM ; la fixer ici
# garantit que les ticks du `project.json` (tempo, boucle) et ceux du `.mid`
# parlent de la même chose.
TICKS_PER_QUARTER_NOTE = 480

# Couleurs de piste, reprises de la palette de l'application. Une piste sans
# couleur serait grise, et douze pistes grises sont illisibles.
_TRACK_COLOURS = [
    "#FF6B9BFF", "#FFD166FF", "#06D6A0FF", "#4CC9F0FF",
    "#B57BFFFF", "#F4845FFF", "#8AC926FF", "#FF595EFF",
]


# Volume par défaut d'une piste écrite par la chaîne. Il est nommé parce que
# le réglage sur la piste doit savoir de quel niveau le calage partira pour
# juger si un patch pourra encore être rattrapé.
DEFAULT_TRACK_VOLUME = 0.9


@dataclass
class ExportNote:
    """Une note, en SECONDES -- l'unité que produit l'analyse."""
    note: int
    velocity: int
    start: float
    duration: float


@dataclass
class ExportTrack:
    """
    Une piste du projet reconstruit : un stem, sa machine et son patch.

    `machine` peut être vide : un stem qu'on n'a pas su attribuer donne alors
    une piste SANS instrument, présente et nommée. C'est délibéré -- effacer
    la piste ferait disparaître l'information « il y avait quelque chose ici
    que nous n'avons pas su reproduire ».
    """
    name: str
    machine: str = ""
    parameters: Dict[str, float] = field(default_factory=dict)
    notes: List[ExportNote] = field(default_factory=list)
    channel: int = 0
    volume: float = DEFAULT_TRACK_VOLUME
    pan: float = 0.0
    is_drums: bool = False
    # Échantillons à charger dans la machine (sampler) : « emplacement ->
    # chemin ». C'est par là que passent les coups de batterie découpés dans
    # l'enregistrement d'origine.
    samples: Dict[int, str] = field(default_factory=dict)
    distance: Optional[float] = None
    machine_display_name: str = ""
    # Courbes d'automation : « identité sémantique -> [(seconde, valeur)] »,
    # valeurs en UNITÉS RÉELLES (Hz, secondes). C'est par là que l'analyse
    # écrit « la coupure suit cette trajectoire » -- un patch figé ne dit
    # qu'une moyenne, et le caractère d'un morceau vit souvent dans le
    # mouvement. Converties en ticks à l'écriture, comme les notes.
    automation: Dict[str, List[Tuple[float, float]]] = field(default_factory=dict)


def _preset_document(track: ExportTrack, name: str) -> dict:
    document = {
        "format": "vsm-synth-preset",
        "version": 1,
        "name": name,
        "pluginId": track.machine,
        "machineName": track.machine_display_name or track.machine,
        # « derived » et jamais « measured » : ces réglages viennent d'une
        # recherche par optimisation sur un enregistrement, pas d'une mesure
        # sur du matériel. Le format impose de le dire.
        "fidelity": "derived",
        "parameters": {k: float(v) for k, v in sorted(track.parameters.items())},
    }
    # Échantillons : écrits seulement s'il y en a, et TOUJOURS en chemin
    # relatif au dossier de projet. Un chemin absolu s'ouvrirait sur la machine
    # qui a produit le projet et nulle part ailleurs -- le moteur les refuse
    # d'ailleurs explicitement.
    if track.samples:
        document["samples"] = {
            str(int(slot)): str(path) for slot, path in sorted(track.samples.items())
        }
    return document


def _write_midi(tracks: Sequence[ExportTrack], path: Path, tempo: float) -> None:
    """
    Écrit le MIDI avec EXACTEMENT une piste par piste de projet.

    Cette correspondance un pour un n'est pas cosmétique : le DAW apparie les
    pistes du `.mid` et celles du `project.json` PAR INDICE. Une piste de plus
    ou de moins ne produit pas une erreur, elle produit un décalage -- et donc
    un morceau où chaque partie est jouée par la machine de la suivante.
    Le cas s'est présenté : une bibliothèque MIDI courante insère d'office une
    piste de méta-données en tête, et la basse s'est retrouvée jouée par la
    boîte à rythmes, sans le moindre message.

    Le tempo est donc placé au début de la PREMIÈRE piste de notes, et non
    dans une piste dédiée.
    """
    midi = mido.MidiFile(type=1, ticks_per_beat=TICKS_PER_QUARTER_NOTE)
    ticks_per_second = TICKS_PER_QUARTER_NOTE * float(tempo) / 60.0

    for index, track in enumerate(tracks):
        midi_track = mido.MidiTrack()
        midi_track.append(mido.MetaMessage("track_name", name=track.name, time=0))
        if index == 0:
            midi_track.append(
                mido.MetaMessage("set_tempo", tempo=mido.bpm2tempo(float(tempo)), time=0)
            )

        # Canal 9 pour la batterie : c'est la convention MIDI, et le DAW s'en
        # sert pour reconnaître une piste percussive.
        channel = 9 if track.is_drums else int(track.channel) % 16

        events = []
        for note in track.notes:
            # Une note de durée nulle ou négative serait ignorée par certains
            # lecteurs et tenue indéfiniment par d'autres. On la refuse ici,
            # là où on peut encore dire pourquoi.
            if note.duration <= 0.0:
                continue
            pitch = max(0, min(127, int(note.note)))
            velocity = max(1, min(127, int(note.velocity)))
            start = int(round(max(0.0, float(note.start)) * ticks_per_second))
            end = max(start + 1, int(round((float(note.start) + float(note.duration)) * ticks_per_second)))
            # Le rang (0 pour une fin, 1 pour un début) départage deux
            # événements au même tick : une fin doit précéder un début, sinon
            # rejouer la même hauteur coupe la note qu'on vient d'ouvrir.
            events.append((start, 1, pitch, velocity))
            events.append((end, 0, pitch, 0))
        events.sort()

        previous = 0
        for tick, kind, pitch, velocity in events:
            delta = tick - previous
            previous = tick
            midi_track.append(
                mido.Message(
                    "note_on" if kind == 1 else "note_off",
                    note=pitch, velocity=velocity, channel=channel, time=delta,
                )
            )
        midi_track.append(mido.MetaMessage("end_of_track", time=0))
        midi.tracks.append(midi_track)

    midi.save(str(path))

    # Vérification APRÈS écriture : le fichier relu doit contenir exactement
    # autant de pistes que le projet. C'est le genre de garantie qu'on ne peut
    # pas déduire du code appelant, et dont l'absence a déjà coûté un morceau
    # entier joué par les mauvaises machines.
    written = mido.MidiFile(str(path))
    if len(written.tracks) != len(tracks):
        raise RuntimeError(
            f"MIDI écrit avec {len(written.tracks)} pistes pour {len(tracks)} pistes de projet : "
            f"l'appariement par indice serait décalé"
        )


def write_project_bundle(
    tracks: Sequence[ExportTrack],
    folder: Path,
    title: str = "Reconstruction",
    tempo: float = 120.0,
) -> Dict[str, object]:
    """
    Écrit le dossier de projet complet et renvoie un compte rendu.

    Le compte rendu n'est pas décoratif : il liste les fichiers écrits et les
    pistes laissées SANS machine. Un export qui échoue à moitié en silence est
    pire qu'un export qui échoue -- on rejouerait un morceau amputé sans
    savoir de quoi.
    """
    folder = Path(folder)
    (folder / "midi").mkdir(parents=True, exist_ok=True)
    (folder / "instruments").mkdir(parents=True, exist_ok=True)

    written: List[str] = []
    unassigned: List[str] = []

    document_tracks = []
    for index, track in enumerate(tracks):
        entry: Dict[str, object] = {
            "name": track.name,
            "channel": 9 if track.is_drums else int(track.channel),
            "color": _TRACK_COLOURS[index % len(_TRACK_COLOURS)],
            "effects": [],
            "mix": {
                "volume": float(track.volume),
                "pan": float(track.pan),
                "muted": False,
                "solo": False,
                "sends": [0.0, 0.0],
            },
        }

        if track.machine:
            preset_relative = f"instruments/track_{index:02d}.synth.json"
            preset_path = folder / preset_relative
            preset_path.write_text(
                json.dumps(_preset_document(track, track.name), indent=2, ensure_ascii=False,
                           sort_keys=True) + "\n",
                encoding="utf-8",
            )
            written.append(preset_relative)
            entry["instrument"] = {
                "preferredPlugin": track.machine,
                "preset": preset_relative,
            }
        else:
            unassigned.append(track.name)

        if track.automation and track.machine:
            ticks_par_seconde = TICKS_PER_QUARTER_NOTE * tempo / 60.0
            entry["automation"] = [
                {
                    "parameter": semantic_id,
                    "points": [
                        {"tick": int(round(seconde * ticks_par_seconde)), "value": float(valeur)}
                        for seconde, valeur in points
                    ],
                }
                for semantic_id, points in sorted(track.automation.items())
                if points
            ]

        document_tracks.append(entry)

    midi_relative = "midi/arrangement.mid"
    _write_midi(tracks, folder / midi_relative, tempo)
    written.append(midi_relative)

    document = {
        "format": "vsm-project",
        "version": 1,
        "title": title,
        "midi": {"file": midi_relative},
        "transport": {
            "ticksPerQuarterNote": TICKS_PER_QUARTER_NOTE,
            "tempoChanges": [{"tick": 0, "bpm": float(tempo)}],
            "timeSignatures": [{"tick": 0, "numerator": 4, "denominator": 4}],
            "loop": {"enabled": False, "startTick": 0, "endTick": 0},
        },
        "tracks": document_tracks,
    }
    (folder / "project.json").write_text(
        json.dumps(document, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    written.append("project.json")

    return {
        "folder": str(folder),
        "written": written,
        "tracks": len(tracks),
        "notes": sum(len(t.notes) for t in tracks),
        "unassigned_tracks": unassigned,
    }
