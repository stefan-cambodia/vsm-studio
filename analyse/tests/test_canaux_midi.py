"""D385 : chaque partie mélodique d'un projet reconstruit a son canal MIDI.

Ce que ces tests fixent : les pistes mélodiques restées au canal 0 reçoivent
les canaux libres dans l'ordre (0, 1, 2… hors 9), dans `project.json` ET dans
le `.mid` du dossier ; la batterie garde le 9 ; un canal posé explicitement
n'est pas touché ; groupe et piste audio ne consomment pas de canal ; au-delà
de quinze parties, le compte rendu NOMME celles qui partagent le leur.
"""
from __future__ import annotations

import collections
import json
import sys
import tempfile
from pathlib import Path

import mido

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_project_export import ExportNote, ExportTrack, write_project_bundle  # noqa: E402

NOTE = [ExportNote(60, 100, 0.0, 0.5)]


def ecrire(pistes):
    dossier = Path(tempfile.mkdtemp())
    rapport = write_project_bundle(pistes, dossier)
    projet = json.loads((dossier / "project.json").read_text(encoding="utf-8"))
    midi = mido.MidiFile(dossier / "midi" / "arrangement.mid")
    canaux_midi = [sorted({m.channel for m in t if m.type == "note_on"}) for t in midi.tracks]
    return rapport, [t["channel"] for t in projet["tracks"]], canaux_midi


@test
def trois_parties_melodiques_trois_canaux():
    pistes = [ExportTrack(name="bass", machine="vsm.piano", notes=NOTE),
              ExportTrack(name="other", machine="vsm.string", notes=NOTE),
              ExportTrack(name="Batterie · kick", machine="vsm.tr909", notes=NOTE, is_drums=True),
              ExportTrack(name="piano", machine="vsm.spectral", notes=NOTE),
              ExportTrack(name="Batterie", is_group=True),
              ExportTrack(name="Voix", audio_path="samples/voix.wav", audio_sample_rate=44100.0,
                          audio_frames=10, audio_channels=1)]
    rapport, canaux, canaux_midi = ecrire(pistes)
    assert_equal(canaux[:4], [0, 1, 9, 2])
    assert_equal(canaux_midi[:4], [[0], [1], [9], [2]])
    assert_equal(rapport["shared_channel_tracks"], [])


@test
def un_canal_explicite_est_garde():
    pistes = [ExportTrack(name="a", machine="vsm.piano", notes=NOTE, channel=5),
              ExportTrack(name="b", machine="vsm.piano", notes=NOTE)]
    _, canaux, _ = ecrire(pistes)
    assert_equal(canaux, [5, 0])


@test
def au_dela_de_quinze_le_partage_est_dit():
    pistes = [ExportTrack(name=f"p{i}", machine="vsm.piano", notes=NOTE) for i in range(17)]
    rapport, canaux, _ = ecrire(pistes)
    assert_true(9 not in canaux, "le canal de la batterie reste libre")
    compte = collections.Counter(canaux)
    assert_equal(sorted(c for c, n in compte.items() if n > 1), [0, 1])
    assert_equal(sorted(rapport["shared_channel_tracks"]), sorted(["p0", "p1", "p15", "p16"]))


if __name__ == "__main__":
    raise SystemExit(run())
