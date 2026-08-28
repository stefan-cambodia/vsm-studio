"""Le corpus passé par la séparation : ce qui se vérifie sans payer demucs."""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer.vsm_corpus_separe import (DUREE, CorpusSepare, Vivier, coupe_par_patch)  # noqa: E402

SR = 44100


@test
def corpus_separe_le_vivier_refuse_les_silences_et_dose_le_fond():
    import soundfile as sf
    dossier = Path(tempfile.mkdtemp(prefix="vsm-vivier-")) / "stems"
    dossier.mkdir()
    rng = np.random.default_rng(0)
    bruit = (rng.standard_normal(SR * 6) * 0.1).astype(np.float32)
    bruit[: SR * 3] = 0.0                       # la première moitié est muette
    sf.write(str(dossier / "drums.wav"), np.stack([bruit, bruit], axis=1), SR)
    vivier = Vivier.depuis([dossier.parent], SR, np.random.default_rng(1), par_stem=8)
    assert_true(len(vivier.tranches) >= 4, f"{len(vivier.tranches)} tranches tirées")
    assert_true(all(float(np.sqrt(np.mean(t ** 2))) >= 0.01 for t in vivier.tranches),
                "aucune tranche muette")
    assert_true(all(t.size == int(DUREE * SR) for t in vivier.tranches), "toutes de la durée d'un exemple")
    fond = vivier.fond(np.random.default_rng(2), rms_synthe=0.05)
    rapport = 20 * np.log10(float(np.sqrt(np.mean(fond ** 2))) / 0.05)
    assert_true(-6.5 <= rapport <= 6.5, f"fond à {rapport:.1f} dB du synthé, dans la plage déclarée")


@test
def corpus_separe_la_coupure_garde_un_patch_entier_d_un_seul_cote():
    patchs = np.repeat(np.arange(50), 4)
    epreuve = coupe_par_patch(patchs, 0.2, graine=3)
    for p in np.unique(patchs):
        cotes = set(epreuve[patchs == p].tolist())
        assert_equal(len(cotes), 1, f"le patch {p} est entier d'un seul côté")
    assert_true(0.15 <= epreuve.mean() <= 0.25, f"{epreuve.mean():.0%} à l'épreuve")
    # Trop peu de patchs pour que 20 % en fassent un : il en met quand même un.
    minuscule = coupe_par_patch(np.repeat(np.arange(3), 2), 0.2, graine=4)
    assert_true(minuscule.any() and not minuscule.all(), "au moins un patch de chaque côté")


@test
def corpus_separe_fait_l_aller_retour():
    c = CorpusSepare(["vsm.a", "vsm.b"], np.ones((3, 43)), np.zeros((3, 43)),
                     np.array([0, 1, 1], dtype=np.int32), np.array([1, 2, 2], dtype=np.int32),
                     np.zeros((3, 3), dtype=np.float32), np.array([0.9, 0.2, 0.5], dtype=np.float32),
                     12.5, {"inaudible": 2})
    chemin = Path(tempfile.mkdtemp(prefix="vsm-separe-")) / "c.npz"
    c.enregistre(chemin)
    relu = CorpusSepare.relit(chemin)
    assert_equal(relu.machines, c.machines, "machines")
    assert_true(np.array_equal(relu.retrouve, c.retrouve), "énergie retrouvée")
    assert_equal(relu.rejets, {"inaudible": 2}, "les rejets voyagent avec le corpus")


@test
def corpus_separe_la_sonde_relit_les_notes_du_midi_du_projet():
    from analyzer.vsm_corpus_separe import notes_depuis_midi
    from analyzer.vsm_project_export import ExportNote, ExportTrack, _write_midi
    chemin = Path(tempfile.mkdtemp(prefix="vsm-sonde-")) / "arrangement.mid"
    pistes = [ExportTrack("bass", "vsm.sh101", notes=[ExportNote(36, 100, 0.0, 0.5)]),
              ExportTrack("other", "vsm.juno106", notes=[ExportNote(60, 90, 1.0, 0.25),
                                                         ExportNote(64, 80, 1.5, 2.0)])]
    _write_midi(pistes, chemin, tempo=140.0)      # un tempo qui n'est pas le défaut
    notes = notes_depuis_midi(chemin, "other")
    assert_equal([n["note"] for n in notes], [60, 64], "les notes de la piste demandée, dans l'ordre")
    assert_true(abs(notes[1]["start"] - 1.5) < 1e-3 and abs(notes[1]["duration"] - 2.0) < 1e-3,
                f"en secondes, tempo lu dans le fichier ({notes[1]})")
    try:
        notes_depuis_midi(chemin, "vocals")
        assert_true(False, "une piste absente est une erreur")
    except ValueError:
        pass
