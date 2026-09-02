"""Le rapport doit dire quelle part du morceau chaque piste porte.

Suite du défaut de conception signalé le 02/09/2026 (« les originaux
contiennent bien plus que 4 pistes ») : le rapport donnait quatre distances
côte à côte, ce qui laisse croire à quatre pistes comparables. Sur *Us and
Them*, `other` porte **62,1 %** de l'énergie du morceau — les deux tiers sur
une piste, jouée par une machine — et aucun champ ne le disait.

`partage_du_morceau` mesure la part d'énergie de chaque stem D'ORIGINE et la
publie ; au-delà de 50 % sur un seul stem, elle le crie au journal.
"""

from __future__ import annotations

import struct
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import contextlib  # noqa: E402
import io  # noqa: E402

from framework import assert_equal, assert_near, assert_true, run, test  # noqa: E402

from reconstruire import partage_du_morceau  # noqa: E402


def ecrire_wav_mono(chemin: Path, amplitude: float, echantillons: int = 4800) -> None:
    """Un WAV 16 bits mono d'amplitude constante : son énergie est connue
    EXACTEMENT (n·a²), donc les parts attendues se calculent de tête."""
    valeur = int(amplitude * 32767)
    donnees = struct.pack("<" + "h" * echantillons, *([valeur] * echantillons))
    entete = (b"RIFF" + struct.pack("<I", 36 + len(donnees)) + b"WAVE"
              + b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, 48000, 96000, 2, 16)
              + b"data" + struct.pack("<I", len(donnees)))
    chemin.write_bytes(entete + donnees)


def capturer(pistes):
    """Le partage ET ce qu'il imprime : la plainte fait partie du contrat."""
    tampon = io.StringIO()
    with contextlib.redirect_stdout(tampon):
        partage = partage_du_morceau(pistes)
    return partage, tampon.getvalue()


@test
def les_parts_tombent_juste_et_dans_l_ordre():
    """Amplitudes 0,1 et 0,2 : énergies 1 à 4, donc 20 % et 80 % — et le plus
    lourd d'abord, parce que c'est lui qu'on ouvre le rapport pour trouver."""
    with tempfile.TemporaryDirectory() as d:
        dossier = Path(d)
        ecrire_wav_mono(dossier / "bass.wav", 0.1)
        ecrire_wav_mono(dossier / "other.wav", 0.2)
        partage, journal = capturer({"bass": dossier / "bass.wav",
                                     "other": dossier / "other.wav"})
    assert_equal(partage[0]["stem"], "other", "le plus lourd d'abord")
    assert_near(partage[0]["partEnergie"], 80.0, 0.2, "part de other")
    assert_near(partage[1]["partEnergie"], 20.0, 0.2, "part de bass")
    assert_true("ATTENTION" in journal,
                "80 % sur un stem doit se crier : " + journal)


@test
def un_partage_equilibre_ne_crie_pas():
    """Le seuil est à la moitié. Trois stems égaux : personne ne porte le
    morceau, le journal informe sans crier — un avertissement permanent est un
    avertissement qu'on apprend à ignorer."""
    with tempfile.TemporaryDirectory() as d:
        dossier = Path(d)
        for nom in ("bass", "drums", "other"):
            ecrire_wav_mono(dossier / f"{nom}.wav", 0.1)
        partage, journal = capturer({nom: dossier / f"{nom}.wav"
                                     for nom in ("bass", "drums", "other")})
    assert_near(partage[0]["partEnergie"], 33.3, 0.2, "trois parts égales")
    assert_true("ATTENTION" not in journal, "pas de cri sans raison : " + journal)
    assert_true("partage du morceau" in journal, "le partage s'imprime toujours")


@test
def un_stem_illisible_se_dit_et_ne_fait_pas_tomber_la_course():
    """La mesure est un compte rendu, pas une condition d'exécution : un stem
    corrompu vaut zéro AVEC une ligne au journal, jamais une exception au
    milieu d'une reconstruction de deux heures."""
    with tempfile.TemporaryDirectory() as d:
        dossier = Path(d)
        ecrire_wav_mono(dossier / "bass.wav", 0.1)
        (dossier / "casse.wav").write_bytes(b"pas un wav")
        partage, journal = capturer({"bass": dossier / "bass.wav",
                                     "casse": dossier / "casse.wav"})
    assert_equal(len(partage), 2, "le stem cassé reste au tableau")
    assert_near(partage[0]["partEnergie"], 100.0, 0.01, "toute l'énergie mesurable")
    assert_true("non mesurée" in journal, "le stem illisible est DIT : " + journal)


@test
def aucun_stem_mesurable_rend_un_partage_vide():
    """Tout illisible (ou tout silencieux) : un tableau de parts de zéro ne
    veut rien dire, on n'en publie pas."""
    with tempfile.TemporaryDirectory() as d:
        dossier = Path(d)
        (dossier / "a.wav").write_bytes(b"rien")
        partage, _ = capturer({"a": dossier / "a.wav"})
    assert_equal(partage, [], "pas de tableau sans mesure")


if __name__ == "__main__":
    raise SystemExit(run())
