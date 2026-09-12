"""H26 : compter les timbres qui s'INSTALLENT dans un stem (§ 12.8 du CDC multipiste).

Ce que ces tests verrouillent est la DÉCISION qui a survécu à deux réfutations :
ce n'est pas l'écart de timbre qui distingue « plusieurs parties » d'un résidu de
séparation, c'est la PERSISTANCE. Les deux seuils (0,30 de distance L1, 4 fenêtres
de durée minimale) sont ceux du § 12.8, posés avant la mesure.
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer.vsm_paliers import (plainte_de_paliers,  # noqa: E402
                                   profil_de_bandes, timbres_installes)

TAUX = 22050


def _ton(hz: float, secondes: float, niveau: float = 0.3) -> np.ndarray:
    t = np.arange(int(secondes * TAUX), dtype=np.float64) / TAUX
    return (niveau * np.sin(2 * np.pi * hz * t)).astype(np.float32)


@test
def paliers_une_partie_tenue_donne_un_seul_timbre():
    """Un son stable de 60 s : un timbre, quoi qu'il dure."""
    nombre, plages = timbres_installes(_ton(220.0, 60.0), TAUX)
    assert_equal(nombre, 1, "un seul timbre installé")
    assert_true(len(plages) >= 1, "et au moins une plage nommée")


@test
def paliers_deux_parties_successives_donnent_deux_timbres():
    """LE CAS DE « other » SUR CHILDREN, en laboratoire : deux timbres qui se
    succèdent sans jamais se superposer. La polyphonie ne les voit pas (elles ne
    sonnent pas ensemble) ; les paliers, si."""
    audio = np.concatenate([_ton(150.0, 40.0), _ton(3000.0, 40.0)])
    nombre, _ = timbres_installes(audio, TAUX)
    assert_equal(nombre, 2, "deux timbres installés")


@test
def paliers_un_residu_erratique_n_installe_rien():
    """LE CAS DES STEMS DE FUITE, qui a tué la première forme (§ 12.7). Des
    éclats courts et différents, séparés de silence : rien ne tient 20 s, donc
    rien ne s'installe — et la porte ne s'ouvrira pas sur un résidu."""
    rng = np.random.default_rng(20260912)
    morceaux = []
    for _ in range(12):
        morceaux.append(_ton(float(rng.uniform(200.0, 6000.0)), 2.0))
        morceaux.append(np.zeros(int(6.0 * TAUX), dtype=np.float32))
    nombre, plages = timbres_installes(np.concatenate(morceaux), TAUX)
    assert_equal(nombre, 0, "aucun timbre installé")
    assert_equal(plages, [], "et aucune plage")


@test
def paliers_le_silence_rompt_le_palier():
    """Deux passages du MÊME timbre séparés par un silence ne font qu'UN timbre
    installé (le même revient), mais DEUX plages : le compte dit ce qui joue, les
    plages disent quand."""
    audio = np.concatenate([_ton(220.0, 30.0),
                            np.zeros(int(30.0 * TAUX), dtype=np.float32),
                            _ton(220.0, 30.0)])
    nombre, plages = timbres_installes(audio, TAUX)
    assert_equal(nombre, 1, "le même timbre, revenu")
    assert_equal(len(plages), 2, "deux plages distinctes")


@test
def paliers_le_profil_est_insensible_au_niveau():
    """Un profil NORMALISÉ : une partie qui joue plus fort n'est pas une autre
    partie. Sans cela, un crescendo ouvrirait la porte."""
    fort = profil_de_bandes(_ton(440.0, 5.0, niveau=0.9)[:int(5 * TAUX)], TAUX)
    faible = profil_de_bandes(_ton(440.0, 5.0, niveau=0.05)[:int(5 * TAUX)], TAUX)
    assert_true(float(np.abs(fort - faible).sum()) < 1e-6,
                "le même timbre à deux niveaux donne le même profil")


@test
def paliers_la_plainte_nomme_ou_les_parties_entrent():
    """Le § 8.3 interdit qu'une porte s'ouvre sans qu'on sache pourquoi."""
    assert_equal(plainte_de_paliers(1, [(0.0, 20.0)]), "", "un seul timbre ne se plaint pas")
    phrase = plainte_de_paliers(3, [(0.0, 40.0), (100.0, 30.0), (200.0, 25.0)])
    assert_true("3 timbres" in phrase, "le compte est dit")
    assert_true("0-40 s" in phrase and "100-130 s" in phrase, "les plages sont nommées")


@test
def paliers_le_decoupeur_de_temps_concentre_les_voix():
    """H30 : chaque voix se concentre dans SA periode, au lieu de couvrir tout.

    Deux timbres qui se succedent : le decoupeur doit rendre deux voix, dont
    chacune ne joue que pendant son palier. C'est ce que le decoupage par
    REGISTRES ne sait pas faire — mesure du § 12.11 : ses voix couvrent 68 a
    94 % du morceau.
    """
    from analyzer.vsm_paliers import voix_par_paliers

    class Note:
        def __init__(self, note, start):
            self.note, self.start, self.duration, self.velocity = note, start, 0.4, 100

    audio = np.concatenate([_ton(150.0, 40.0), _ton(3000.0, 40.0)])
    notes = ([Note(40, t) for t in np.arange(1.0, 39.0, 2.0)]
             + [Note(80, t) for t in np.arange(41.0, 79.0, 2.0)])
    voix = voix_par_paliers(notes, audio, TAUX)
    assert_equal(len(voix), 2, "deux voix, une par timbre installé")
    for groupe in voix:
        debuts = [n.start for n in groupe]
        assert_true(max(debuts) - min(debuts) < 45.0,
                    "chaque voix tient dans sa période, pas sur tout le morceau")


@test
def paliers_un_seul_timbre_ne_se_decoupe_pas():
    """Sans deux timbres installés, le découpeur rend le stem tel quel — il ne
    fabrique pas de voix là où rien ne s'est installé."""
    from analyzer.vsm_paliers import voix_par_paliers

    class Note:
        def __init__(self, note, start):
            self.note, self.start, self.duration, self.velocity = note, start, 0.4, 100

    audio = _ton(220.0, 60.0)
    notes = [Note(60, t) for t in np.arange(1.0, 59.0, 2.0)]
    assert_equal(len(voix_par_paliers(notes, audio, TAUX)), 1, "une seule voix")
