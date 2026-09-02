"""La voix de tête et les chœurs, séparés par le champ stéréo (§ 4.5).

Le séparateur ne prétend pas reconnaître des voix : il sépare CE QUI EST AU
CENTRE (la tête, convention de mixage presque universelle) de ce qui est
large (chœurs, doublages). Ces tests fixent ses trois promesses :

  - tête + chœurs == stem, EXACTEMENT (les chœurs sont le complément
    temporel : rien ne peut se perdre) ;
  - l'énergie va au bon endroit — un signal centré part dans la tête, un
    signal large part dans les chœurs ;
  - une voix sans largeur ne se découpe PAS (la porte) : livrer une piste
    « chœurs » quasi vide la ferait passer pour une partie réelle.
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_voix import (SEUIL_PART_LATERALE, ecrire_wav_stereo,  # noqa: E402
                               lire_wav_stereo, separer_tete_et_choeurs)

TAUX = 48000


def sinus(hz, secondes=1.0, gain=0.5):
    t = np.arange(int(TAUX * secondes)) / TAUX
    return gain * np.sin(2 * np.pi * hz * t)


@test
def tete_plus_choeurs_redonne_le_stem_exactement():
    """LA GARANTIE DU MODULE. Quoi que vaille le masque spectral, le stem se
    reconstruit à l'identique : les chœurs sont l'original moins la tête."""
    gauche = sinus(220) + sinus(330)
    droite = sinus(220) - sinus(330)          # 330 Hz en opposition : large
    s = separer_tete_et_choeurs(gauche, droite)
    assert_true(s is not None, "il y a de la largeur à séparer")
    somme = s.tete + s.choeurs
    assert_true(np.allclose(somme[:, 0], gauche, atol=1e-9)
                and np.allclose(somme[:, 1], droite, atol=1e-9),
                "tête + chœurs == stem, exactement")


@test
def le_centre_part_dans_la_tete_et_le_large_dans_les_choeurs():
    """220 Hz identique sur les deux canaux (la « tête ») ; 330 Hz à gauche
    seulement et 415 Hz à droite seulement (les « chœurs »). Le séparateur
    doit router l'énergie de chacun au bon endroit — pas parfaitement (les
    cases temps-fréquence se partagent), mais nettement."""
    tete_vraie = sinus(220)
    gauche = tete_vraie + sinus(330)
    droite = tete_vraie + sinus(415)
    s = separer_tete_et_choeurs(gauche, droite)
    assert_true(s is not None, "stem large")

    def energie_a(signal, hz):
        spectre = np.abs(np.fft.rfft(signal))
        raie = int(round(hz * signal.size / TAUX))
        return float(np.sum(spectre[raie - 2:raie + 3] ** 2))

    mono_tete = s.tete.mean(axis=1)
    mono_choeurs = s.choeurs.mean(axis=1)
    part_220 = energie_a(mono_tete, 220) / (energie_a(mono_tete, 220)
                                            + energie_a(mono_choeurs, 220) + 1e-12)
    part_330 = energie_a(mono_choeurs, 330) / (energie_a(mono_tete, 330)
                                               + energie_a(mono_choeurs, 330) + 1e-12)
    assert_true(part_220 > 0.8, f"le centre va dans la tête : {part_220:.2f}")
    assert_true(part_330 > 0.8, f"le large va dans les chœurs : {part_330:.2f}")


@test
def une_voix_sans_largeur_ne_se_decoupe_pas():
    """La porte : un stem mono replié en stéréo (G == D) n'a pas de chœurs à
    offrir — le séparateur rend None, et l'appelant le dira au lieu de livrer
    une piste vide déguisée en partie."""
    centre = sinus(220) + sinus(440, gain=0.3)
    assert_true(separer_tete_et_choeurs(centre, centre.copy()) is None,
                "rien à séparer dans un signal centré")
    silence = np.zeros(TAUX)
    assert_true(separer_tete_et_choeurs(silence, silence) is None,
                "le silence non plus")


@test
def la_porte_mesure_la_part_laterale():
    """Le seuil est une PART d'énergie, pas une impression : à peine au-dessus
    il sépare, à peine en dessous il refuse."""
    tete = sinus(220)
    # une largeur calibrée : lateral/total = gain²/(1+gain²)
    # part latérale = g² / (g² + 0,25) pour une tête de gain 0,5 :
    # g = sqrt(0,25 · part / (1 − part)). La première version de ce calcul
    # supposait des énergies unitaires et plaçait les deux gains DU MÊME côté
    # du seuil — le test se trompait, pas la porte.
    import math
    def gain_pour(part):
        return math.sqrt(0.25 * part / (1.0 - part))
    large = sinus(330, gain=gain_pour(1.25 * SEUIL_PART_LATERALE))
    s = separer_tete_et_choeurs(tete + large, tete - large)
    assert_true(s is not None and s.part_laterale > SEUIL_PART_LATERALE,
                "au-dessus du seuil : séparé")
    petit = sinus(330, gain=gain_pour(0.8 * SEUIL_PART_LATERALE))
    assert_true(separer_tete_et_choeurs(tete + petit, tete - petit) is None,
                "en dessous du seuil : refusé")


@test
def l_aller_retour_wav_stereo_conserve_les_canaux():
    import tempfile
    gauche, droite = sinus(220), sinus(330)
    with tempfile.TemporaryDirectory() as d:
        chemin = Path(d) / "stereo.wav"
        ecrire_wav_stereo(chemin, np.stack([gauche, droite], axis=1), TAUX)
        lu = lire_wav_stereo(chemin)
    assert_true(lu is not None, "stéréo lue")
    g, dr, taux = lu
    assert_equal(taux, TAUX, "le taux survit")
    assert_true(np.allclose(g, gauche, atol=1e-3) and np.allclose(dr, droite, atol=1e-3),
                "gauche et droite ne sont pas échangés ni mélangés")


if __name__ == "__main__":
    raise SystemExit(run())
