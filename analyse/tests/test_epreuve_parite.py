"""L'épreuve de parité se FABRIQUE et sa vérité tient : neuf parties, des
stems qui somment à l'original, une voix stéréo avec de la largeur, des
registres disjoints dans `other`. Ce test ne fait pas tourner la chaîne
(c'est l'outil qui mesure) ; il garde ce que l'outil promet à la chaîne."""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

import epreuve_parite  # noqa: E402


@test
def le_morceau_a_neuf_parties_et_ses_stems_somment_a_l_original():
    import soundfile as sf
    with tempfile.TemporaryDirectory() as d:
        dossier = Path(d)
        verite = epreuve_parite.fabriquer(dossier)
        assert_equal(verite["total"], 9, "neuf parties écrites")
        assert_equal(json.loads((dossier / "verite.json").read_text())["total"], 9, "la vérité est sur le disque")
        original, taux = sf.read(str(dossier / "original.wav"), always_2d=True)
        somme = np.zeros_like(original)
        for nom in ("bass", "other", "drums", "vocals"):
            stem, _ = sf.read(str(dossier / "stems" / f"{nom}.wav"), always_2d=True)
            somme += stem if stem.shape[1] == 2 else np.repeat(stem, 2, axis=1)
        assert_equal(taux, epreuve_parite.SR, "44,1 kHz")
        assert_true(np.abs(somme - original).max() < 1e-6, "les stems somment EXACTEMENT à l'original")
        voix, _ = sf.read(str(dossier / "stems" / "vocals.wav"), always_2d=True)
        cote = voix[:, 0] - voix[:, 1]
        milieu = voix[:, 0] + voix[:, 1]
        assert_true(np.sum(cote ** 2) / np.sum(milieu ** 2) > 0.05, "la voix a de la largeur à séparer")
        r = verite["registres"]
        assert_true(r["grave"][1] < r["medium"][0] and r["medium"][1] < r["aigu"][0],
                    "les trois registres d'other sont disjoints")


if __name__ == "__main__":
    raise SystemExit(run())
