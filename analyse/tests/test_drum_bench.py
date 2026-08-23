"""Le banc de batterie doit être un juge fiable : déterministe, et honnête sur
ses propres conventions (tolérance, familles canoniques, frappes inventées)."""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer.vsm_drum_bench import (BPM, TOLERANCE, motif_contretemps, motif_double_croche,
                                      juge, rend_motif, _famille_canonique)  # noqa: E402
from analyzer.vsm_engine import VsmEngine  # noqa: E402


@test
def banc_batterie_les_motifs_sont_ceux_du_paragraphe_9_5():
    a = motif_double_croche()
    b = motif_contretemps()
    assert_equal(len(a.frappes["kick"]), 16, "A : seize grosses caisses")
    assert_equal(len(a.frappes["snare"]), 8, "A : huit caisses claires")
    assert_equal(len(a.frappes["hihat"]), 64, "A : soixante-quatre charlestons")
    assert_equal(len(b.frappes["hihat"]), 16, "B : seize charlestons aux contretemps")
    # Sur B, AUCUNE charleston ne coïncide avec une autre pièce : c'est le témoin.
    autres = set(b.frappes["kick"]) | set(b.frappes["snare"])
    assert_true(all(min(abs(t - o) for o in autres) > TOLERANCE for t in b.frappes["hihat"]),
                "B : les charlestons sont seules")
    # Sur A, chaque temps porte kick ET charleston : le cas qui a tué trois architectures.
    assert_true(all(t in a.frappes["hihat"] for t in a.frappes["kick"]),
                "A : chaque grosse caisse est doublée d'une charleston")


@test
def banc_batterie_le_juge_est_deterministe():
    with VsmEngine(sample_rate=44100) as moteur:
        motif = motif_contretemps()
        audio = rend_motif(motif, moteur)
        premier = juge(motif, moteur, audio=audio)
        second = juge(motif, moteur, audio=audio)
    for x, y in zip(premier.familles, second.familles):
        assert_equal((x.retrouvees, x.inventees), (y.retrouvees, y.inventees),
                     f"{x.famille} : même verdict deux fois")


@test
def banc_batterie_les_variantes_sont_la_meme_piece():
    assert_equal(_famille_canonique("kick2"), "kick", "kick2 est un kick")
    assert_equal(_famille_canonique("openhat"), "hihat", "openhat est une charleston")
    assert_equal(_famille_canonique("pedalhat"), "hihat", "pedalhat aussi")
    assert_equal(_famille_canonique("snare2"), "snare", "snare2 est une caisse claire")


@test
def banc_batterie_les_instants_sont_retrouves_meme_si_le_nom_est_faux():
    """La distinction centrale du banc : une frappe au bon instant sous le
    mauvais nom est CONFONDUE, pas manquante. C'est l'information que la phase
    A2 doit lire, puisque c'est le nommage qu'elle apprend."""
    with VsmEngine(sample_rate=44100) as moteur:
        score = juge(motif_contretemps(), moteur)
    for f in score.familles:
        trouvees_quelque_part = f.retrouvees + sum(f.confondues_avec.values())
        assert_true(trouvees_quelque_part >= 0.9 * f.attendues,
                    f"{f.famille} : les INSTANTS sont justes "
                    f"({trouvees_quelque_part}/{f.attendues} trouvées sous un nom ou un autre)")
