"""H52 — le verdict du mélange entend la piste audio (docs/CDC-reload-indifferenciable.md § 14).

Depuis que la voix est une piste AUDIO, son fichier n'était plus recopié dans le
dossier que le verdict rend : le moteur ne le trouvait pas, la piste sortait muette,
et le verdict jugeait un mélange sans la voix. Lu dans douze rapports sur douze : la
distance « sans la piste » de voix égalait celle « avec », au seizième chiffre.

Le faux rendu fait ici ce que fait le moteur : une piste audio dont le FICHIER manque
dans le dossier rendu ne sonne pas. Le test est rouge tant que le fichier n'est pas
recopié, et c'est ce rouge-là qui a été vu avant la correction.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer import vsm_mix_verdict as mv  # noqa: E402
from analyzer.vsm_project_export import ExportNote, ExportTrack  # noqa: E402

TAUX, DUREE = 44100, 22050


def _pistes():
    return [ExportTrack(name="bass", machine="vsm.tb303",
                        notes=[ExportNote(note=48, velocity=100, start=0.0, duration=0.4)]),
            ExportTrack(name="Voix", audio_path="samples/voix.wav", audio_sample_rate=float(TAUX),
                        audio_frames=DUREE, audio_channels=1)]


def _rendu_comme_le_moteur(tracks, dossier, sample_rate, tempo, binary):
    """Une machine sonne ; une piste audio ne sonne que si SON FICHIER est dans le dossier."""
    temps = np.arange(DUREE, dtype=np.float32) / TAUX
    somme = np.zeros(DUREE, dtype=np.float32)
    for t in tracks:
        if t.volume <= 0.0:
            continue
        if t.machine:
            somme = somme + np.sin(2 * np.pi * 220 * temps).astype(np.float32) * float(t.volume)
        elif t.audio_path and (Path(dossier) / t.audio_path).is_file():
            somme = somme + np.sin(2 * np.pi * 660 * temps).astype(np.float32) * float(t.volume)
    return somme


@test
def h52_le_verdict_entend_la_piste_audio():
    temps = np.arange(DUREE, dtype=np.float32) / TAUX
    melange = (np.sin(2 * np.pi * 220 * temps) + np.sin(2 * np.pi * 660 * temps)).astype(np.float32) * 0.9
    with tempfile.TemporaryDirectory(prefix="vsm-test-h52-") as brouillon:
        racine = Path(brouillon)
        (racine / "sortie" / "samples").mkdir(parents=True)
        (racine / "sortie" / "samples" / "voix.wav").write_bytes(b"la voix")
        vrai = mv._render_project
        mv._render_project = _rendu_comme_le_moteur
        mv._deja_dit.clear()
        try:
            decisions = mv.keep_what_helps_the_mix(
                tracks=_pistes(), alternatives={}, mixture=melange, stems_audio={},
                samples_root=racine / "sortie", workdir=racine / "verdict", sample_rate=TAUX)
        finally:
            mv._render_project = vrai
        assert_true((racine / "verdict" / "variante" / "samples" / "voix.wav").is_file(),
                    "le fichier de la piste audio est recopié dans le dossier rendu")
    voix = next(d for d in decisions if d.track == "Voix")
    assert_true(voix.muted_distance is not None, "la piste audio a son témoin de coupure")
    assert_true(voix.muted_distance != voix.distance_kept,
                f"« sans la piste » ({voix.muted_distance}) ne peut pas égaler « avec » ({voix.distance_kept})")
    assert_true(voix.muted_distance > voix.distance_kept, "et le morceau est meilleur AVEC sa voix")


@test
def h52_sans_piste_audio_rien_de_plus_n_est_recopie():
    with tempfile.TemporaryDirectory(prefix="vsm-test-h52-") as brouillon:
        racine = Path(brouillon)
        (racine / "sortie" / "samples").mkdir(parents=True)
        (racine / "sortie" / "samples" / "kick.wav").write_bytes(b"kick")
        (racine / "sortie" / "samples" / "autre.wav").write_bytes(b"pas cite")
        pistes = _pistes()[:1]
        pistes[0].samples = {36: "samples/kick.wav"}
        mv._copy_samples(pistes, racine / "sortie", racine / "variante")
        copies = sorted(p.name for p in (racine / "variante" / "samples").iterdir())
        assert_equal(copies, ["kick.wav"], "seuls les fichiers que les pistes désignent")
