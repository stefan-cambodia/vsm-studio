"""B10 : ``comparaison.wav`` n'écrête plus, et le rapport dit ce qui lui a été fait.

Le fichier d'écoute A/B est en 16 bits ; une reconstruction au-dessus de
0 dBFS y était rabotée (« B4 Wuz Then » : +2,7 dB, 1 176 échantillons à fond
d'échelle) et le musicien entendait une distorsion qui n'était pas celle de la
reconstruction. Le remède : descendre les DEUX canaux du même facteur, pour que
le rapport original/reconstruction — ce qu'on écoute — reste intact, et le DIRE.
"""

from __future__ import annotations

import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, test  # noqa: E402

import reconstruire  # noqa: E402


def _sinus(crete: float, n: int = 4800) -> np.ndarray:
    return (crete * np.sin(np.linspace(0.0, 40.0 * np.pi, n, endpoint=False))).astype(np.float32)


@test
def b10_une_reconstruction_au_dessus_de_zero_dbfs_descend_les_deux_canaux_du_meme_facteur() -> None:
    original = _sinus(0.5)
    reconstruit = _sinus(1.4741)   # la crête de « B4 Wuz Then »
    canaux, compte = reconstruire.preparer_comparaison([original, reconstruit])
    assert_near(compte["gainDb"], 20.0 * np.log10(reconstruire.CRETE_ECOUTE / 1.4741), 1e-6)
    assert_near(float(np.max(np.abs(canaux[1]))), reconstruire.CRETE_ECOUTE, 1e-5)
    # Le rapport entre les deux canaux est intact : c'est ce qu'on écoute.
    assert_near(float(np.max(np.abs(canaux[0]))) / float(np.max(np.abs(canaux[1]))), 0.5 / 1.4741, 1e-5)
    assert_true(compte["echantillonsEcretesEvites"] > 0, "les échantillons au-dessus de 1,0 sont comptés")
    assert_near(compte["cretes"][1], 1.4741, 1e-4)


@test
def b10_une_reconstruction_sous_la_crete_d_ecoute_ne_change_pas() -> None:
    original, reconstruit = _sinus(0.7), _sinus(0.8)
    canaux, compte = reconstruire.preparer_comparaison([original, reconstruit])
    assert_equal(compte["gainDb"], 0.0)
    assert_equal(compte["echantillonsEcretesEvites"], 0)
    assert_true(np.array_equal(canaux[1], reconstruit), "rien n'est touché sous -1 dBFS")


@test
def b10_le_fichier_grave_n_a_aucun_echantillon_a_fond_d_echelle_et_le_dit() -> None:
    with tempfile.TemporaryDirectory() as dossier:
        chemin = Path(dossier) / "comparaison.wav"
        compte = reconstruire.ecrire_wav(chemin, [_sinus(0.5), _sinus(1.4741)], ecoute=True)
        with wave.open(str(chemin), "rb") as f:
            assert_equal(f.getsampwidth(), 2)
            assert_equal(f.getnchannels(), 2)
            brut = np.frombuffer(f.readframes(f.getnframes()), dtype="<i2")
        assert_equal(int(np.count_nonzero(np.abs(brut.astype(np.int32)) >= 32767)), 0)
        assert_true(compte["gainDb"] < -4.0, f"gain {compte['gainDb']:.2f} dB attendu autour de -4,37")


@test
def b10_une_sonde_de_mesure_n_est_pas_descendue() -> None:
    # La sonde coupée d'un stem (D282) est une ENTRÉE de mesure : sans `ecoute`,
    # rien ne bouge -- sinon la mesure changerait avec le graveur.
    with tempfile.TemporaryDirectory() as dossier:
        chemin = Path(dossier) / "sonde.wav"
        compte = reconstruire.ecrire_wav(chemin, [_sinus(0.9)])
        assert_equal(compte["gainDb"], 0.0)
        with wave.open(str(chemin), "rb") as f:
            brut = np.frombuffer(f.readframes(f.getnframes()), dtype="<i2")
        assert_near(float(np.max(np.abs(brut.astype(np.int32)))) / 32767.0, 0.9, 1e-3)
