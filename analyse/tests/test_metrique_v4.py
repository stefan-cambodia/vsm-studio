"""La métrique v4 : v3 plus un terme de DYNAMIQUE.

Ce que ces tests verrouillent est ce qui a motivé v4 et ce qui ne doit pas
bouger en l'ajoutant : le facteur de crête est un rapport, donc insensible au
volume ; le terme se tait quand il n'a rien à dire (v4 = v3 exactement) ; et
sur le cas qui a tout déclenché — une batterie qui bourdonne au lieu de
frapper — v4 pénalise ce que v2 récompensait.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_near, assert_true, test  # noqa: E402

from analyzer.audio_distance_v4 import crest_factor, dynamics_term  # noqa: E402
from analyzer.vsm_distance_cache import cached_distance_for  # noqa: E402
from analyzer.vsm_engine import Note, VsmEngine  # noqa: E402

SR = 44100


def sinus(freq: float, secondes: float = 1.0, amplitude: float = 0.5) -> np.ndarray:
    t = np.arange(int(secondes * SR)) / SR
    return (amplitude * np.sin(2 * np.pi * freq * t)).astype(np.float32)


def frappes(periode: float, longueur: float, decroissance: float) -> np.ndarray:
    """Des impulsions régulières qui s'éteignent : une batterie de laboratoire.

    LE RÉGIME ÉTABLI EST SEUL RENDU, et ce n'est pas un détail : quand les
    queues se recouvrent, l'amplitude MONTE pendant les premières secondes
    avant de se stabiliser. Rendre cette montée donnerait au bourdon un
    facteur de crête ARTIFICIELLEMENT ÉLEVÉ -- son maximum est à la fin, sa
    valeur efficace inclut le début silencieux -- et le test mesurerait la
    rampe au lieu du relief. Mesuré en l'écrivant : 3,19 pour le bourdon
    contre 2,48 pour le détaché, c'est-à-dire l'inverse de ce qui est vrai.
    On engendre donc quatre fois la longueur demandée et on n'en garde que la
    fin, où le régime est atteint quelle que soit l'extinction.
    """
    amorce = max(longueur, 8.0 * decroissance)
    total = amorce + longueur
    n = int(total * SR)
    y = np.zeros(n, dtype=np.float32)
    t = np.arange(n) / SR
    porteuse = np.sin(2 * np.pi * 60.0 * t).astype(np.float32)
    for debut in np.arange(0.0, total, periode):
        i = int(debut * SR)
        reste = np.arange(n - i) / SR
        y[i:] += (np.exp(-reste / decroissance) * porteuse[i:]).astype(np.float32)
    return y[int(amorce * SR):]


@test
def metrique_v4_le_facteur_de_crete_ignore_le_volume():
    """C'est un RAPPORT : doubler le signal ne doit rien changer. La métrique
    entière est insensible au niveau, et ce terme ne doit pas faire exception --
    sans quoi une machine gagnerait en sortant plus fort."""
    y = frappes(periode=0.33, longueur=4.0, decroissance=0.1)
    fort = crest_factor(y)
    faible = crest_factor((y * 0.01).astype(np.float32))
    assert_near(fort, faible, 1e-6, "le facteur de crête ne dépend pas du volume")
    assert_true(fort > 1.0, "des frappes détachées ont une crête au-dessus de leur efficace")


@test
def metrique_v4_distingue_des_frappes_d_un_bourdon():
    """Le cas qui a motivé v4, en laboratoire : deux signaux de même énergie
    moyenne et de même contenu spectral, l'un qui frappe et se tait, l'autre
    dont les queues se recouvrent en un mur continu."""
    detache = frappes(periode=0.33, longueur=8.0, decroissance=0.04)
    bourdon = frappes(periode=0.33, longueur=8.0, decroissance=1.25)
    c_detache = crest_factor(detache)
    c_bourdon = crest_factor(bourdon)
    assert_true(c_detache > 1.5 * c_bourdon,
                f"le détaché a bien plus de relief ({c_detache:.2f} contre {c_bourdon:.2f})")
    # Jugés contre le détaché : le bourdon doit être pénalisé PAR CE TERME.
    assert_true(dynamics_term(c_detache, c_bourdon) > 0.5,
                "un bourdon face à des frappes coûte plus d'une demi-octave de crête")
    assert_near(dynamics_term(c_detache, c_detache), 0.0, 1e-9,
                "et la cible contre elle-même ne coûte rien")


@test
def metrique_v4_se_tait_quand_elle_n_a_rien_a_dire():
    """Sur un extrait trop court pour qu'une enveloppe veuille dire quelque
    chose, le terme est nul et v4 = v3 EXACTEMENT -- la même règle que le terme
    de hauteur de v3, qui se tait faute de grave."""
    court = sinus(440.0, secondes=0.05)
    assert_near(crest_factor(court), 0.0, 1e-9,
                "trop court pour qu'une enveloppe veuille dire quelque chose")
    autre = sinus(460.0, secondes=0.05)
    v3 = cached_distance_for("v3")(court, SR)(autre)
    v4 = cached_distance_for("v4")(court, SR)(autre)
    assert_near(v4, v3, 1e-9, "terme nul, donc v4 == v3 exactement")


@test
def metrique_v4_penalise_la_batterie_qui_bourdonne():
    """Le cas réel. Sur un motif de kicks, le réglage de piste avait retenu une
    extinction de 1,25 s -- chaque frappe recouvrant les trois suivantes --
    parce que v2 la préférait. v4 doit renverser cet ordre, et c'est le terme de
    dynamique qui doit le faire, pas un effet de bord des sept autres."""
    with VsmEngine(sample_rate=SR) as moteur:
        notes = [Note(36, 110, i * 0.33, 0.1) for i in range(12)]
        duree = 4.5
        cible = moteur.render("vsm.tr808", {"drum.kick.decay": 0.12}, notes, duree)
        court = moteur.render("vsm.tr808", {"drum.kick.decay": 0.15}, notes, duree)
        long = moteur.render("vsm.tr808", {"drum.kick.decay": 1.25}, notes, duree)

    m4 = cached_distance_for("v4")(cible, SR)
    assert_true(m4(court) < m4(long),
                "v4 préfère l'extinction proche de la cible à celle qui bourdonne")
    assert_true(m4.terms(long)["dynamics"] > m4.terms(court)["dynamics"],
                "et c'est le terme de dynamique qui fait la différence")
