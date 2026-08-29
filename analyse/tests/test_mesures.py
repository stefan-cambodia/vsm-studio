"""Les deux moitiés du projet mesurent-elles la même chose ?

C'est le critère de la phase D4 de ``docs/ROADMAP-daw.md`` : « le mixage fait
dans l'application et le mixage fait par ``analyse/`` sur les mêmes stems
donnent le même LUFS à 0,1 près. Les deux moitiés du projet mesureront enfin la
même chose. »

Ce test le VÉRIFIE au lieu de l'espérer : il fabrique des signaux, les écrit en
WAV, les fait mesurer par ``vsm-measure`` -- qui emploie le code C++ du mixeur --
et compare aux mesures de ``analyzer.mesures``.

Il se saute proprement quand l'outil n'est pas compilé : un test qui exige une
compilation préalable ne doit pas faire échouer la suite Python de quelqu'un qui
travaille sur l'analyse.
"""

from __future__ import annotations

import json
import math
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from framework import assert_near, assert_true, run, test  # noqa: E402
from analyzer.mesures import mesurer  # noqa: E402

RACINE = Path(__file__).resolve().parents[2]
TOLERANCE_LUFS = 0.1


def _outil() -> Path | None:
    """Le chemin de ``vsm-measure``, ou None s'il n'est pas compilé."""
    for motif in ("build/app/vsm-measure_artefacts/*/vsm-measure",
                  "build/app/vsm-measure_artefacts/vsm-measure"):
        for chemin in RACINE.glob(motif):
            if chemin.is_file() and os.access(chemin, os.X_OK):
                return chemin
    return None


def _ecrire_wav(chemin: Path, gauche: np.ndarray, droite: np.ndarray, sr: int) -> None:
    """Écrit un WAV 32 bits flottant, sans dépendance."""
    entrelace = np.empty(gauche.size * 2, dtype=np.float32)
    entrelace[0::2] = gauche.astype(np.float32)
    entrelace[1::2] = droite.astype(np.float32)
    donnees = entrelace.tobytes()
    with open(chemin, "wb") as f:
        f.write(b"RIFF")
        f.write(struct.pack("<I", 36 + len(donnees)))
        f.write(b"WAVEfmt ")
        f.write(struct.pack("<IHHIIHH", 16, 3, 2, sr, sr * 2 * 4, 2 * 4, 32))
        f.write(b"data")
        f.write(struct.pack("<I", len(donnees)))
        f.write(donnees)


def _mesurer_par_loutil(chemin: Path) -> dict:
    sortie = subprocess.run([str(_outil()), str(chemin)], capture_output=True,
                             text=True, cwd="/")
    ligne = sortie.stdout.strip().splitlines()[-1]
    return json.loads(ligne)


def _signaux():
    """Quatre signaux qui n'ont pas les mêmes réponses, et c'est le but.

    Un seul signal ne prouverait rien : deux mesures fausses de la même façon
    coïncideraient. On prend donc du grave et de l'aigu (la pondération K les
    traite différemment), du bruit (large bande) et un signal en opposition de
    phase (que la corrélation doit voir).
    """
    sr = 48000
    n = sr // 2
    t = np.arange(n) / sr
    yield "sinus 100 Hz", sr, 0.5 * np.sin(2 * np.pi * 100 * t), 0.5 * np.sin(2 * np.pi * 100 * t)
    yield "sinus 5 kHz", sr, 0.3 * np.sin(2 * np.pi * 5000 * t), 0.3 * np.sin(2 * np.pi * 5000 * t)
    rng = np.random.default_rng(20260830)
    bruit = rng.standard_normal(n) * 0.1
    yield "bruit", sr, bruit, rng.standard_normal(n) * 0.1
    porteuse = 0.4 * np.sin(2 * np.pi * 440 * t)
    yield "opposition de phase", sr, porteuse, -porteuse


@test
def les_deux_moities_donnent_le_meme_lufs():
    outil = _outil()
    assert_true(outil is not None,
                "vsm-measure n'est pas compile "
                "(cmake --build build --target vsm-measure) -- test saute")
    if outil is None:
        return

    with tempfile.TemporaryDirectory() as dossier:
        for nom, sr, gauche, droite in _signaux():
            chemin = Path(dossier) / (nom.replace(" ", "_") + ".wav")
            _ecrire_wav(chemin, gauche, droite, sr)

            cpp = _mesurer_par_loutil(chemin)
            py = mesurer(gauche, droite, sr)

            assert_near(cpp["lufs"], py.lufs, TOLERANCE_LUFS,
                        f"LUFS de « {nom} » : le moteur dit {cpp['lufs']:.3f}, "
                        f"l'analyse dit {py.lufs:.3f}")
            # La crête et la valeur efficace ne passent par aucun filtre : elles
            # doivent coincider bien plus finement que le LUFS.
            assert_near(cpp["peak"], py.peak, 1e-5, f"crete de « {nom} »")
            assert_near(cpp["rms"], py.rms, 1e-5, f"RMS de « {nom} »")
            assert_near(cpp["correlation"], py.correlation, 1e-4,
                        f"correlation de « {nom} »")


@test
def la_correlation_voit_lopposition_de_phase():
    """Le cas qui compte : ce mixage disparaît en mono, et les deux moitiés
    doivent le dire toutes les deux."""
    sr = 48000
    t = np.arange(sr // 4) / sr
    porteuse = 0.4 * np.sin(2 * np.pi * 440 * t)
    py = mesurer(porteuse, -porteuse, sr)
    assert_near(py.correlation, -1.0, 1e-6, "corrélation en opposition")
    py_mono = mesurer(porteuse, porteuse, sr)
    assert_near(py_mono.correlation, 1.0, 1e-6, "corrélation en phase")


@test
def le_silence_ne_donne_pas_moins_l_infini():
    """Une valeur finie se compare, se sérialise et s'affiche ; -inf demande un
    cas particulier partout."""
    sr = 48000
    vide = np.zeros(1024)
    m = mesurer(vide, vide, sr)
    assert_true(math.isfinite(m.lufs), "le LUFS du silence doit rester fini")
    assert_near(m.correlation, 1.0, 1e-9, "deux canaux vides sont identiques")


if __name__ == "__main__":
    raise SystemExit(run())
