"""Le verdict du mélange rend AUSSI les pistes audio (CDC multipiste § 12).

La leçon : le rendu de variante ne trouvait que les échantillons des pistes de
SAMPLER ; la voix, devenue piste AUDIO avec la parité, restait muette dans
chaque rendu du verdict et du réglage au mélange, et rien ne le disait. Ce
test verrouille la recopie, et son témoin.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402
import analyzer.vsm_mix_verdict as verdict  # noqa: E402
from analyzer.vsm_project_export import ExportTrack  # noqa: E402


@test
def la_piste_audio_est_recopiee_dans_le_dossier_de_variante():
    with tempfile.TemporaryDirectory(prefix="vsm-verdict-audio-") as racine:
        racine = Path(racine)
        (racine / "samples").mkdir()
        (racine / "samples" / "voix.wav").write_bytes(b"RIFF" + bytes(40))
        (racine / "samples" / "kick.wav").write_bytes(b"RIFF" + bytes(12))
        pistes = [ExportTrack(name="Voix", audio_path="samples/voix.wav"),
                  ExportTrack(name="Batterie", machine="vsm.multisample",
                              samples={36: "samples/kick.wav"})]
        variante = racine / "variante"
        verdict._copy_samples(pistes, racine, variante)
        assert_true((variante / "samples" / "voix.wav").is_file(), "la voix est recopiée")
        assert_true((variante / "samples" / "kick.wav").is_file(), "le sampler l'est toujours")
        assert_equal((variante / "samples" / "voix.wav").stat().st_size, 44)


@test
def le_temoin_laisse_la_piste_audio_muette_et_le_dit_par_son_drapeau():
    with tempfile.TemporaryDirectory(prefix="vsm-verdict-audio-") as racine:
        racine = Path(racine)
        (racine / "samples").mkdir()
        (racine / "samples" / "voix.wav").write_bytes(b"RIFF" + bytes(40))
        pistes = [ExportTrack(name="Voix", audio_path="samples/voix.wav")]
        verdict.COPIER_LES_PISTES_AUDIO = False
        try:
            verdict._copy_samples(pistes, racine, racine / "temoin")
        finally:
            verdict.COPIER_LES_PISTES_AUDIO = True
        assert_true(not (racine / "temoin" / "samples" / "voix.wav").exists(),
                    "le témoin reproduit l'ancien comportement")
