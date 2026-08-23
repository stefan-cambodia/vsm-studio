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


@test
def batterie_les_boites_a_rythmes_jouent_le_kit_detecte():
    """Le kit découvert doit se rejouer sur la TR-909 et la TR-808 avec les
    MÊMES instants et vélocités que sur vsm.drums : seule la machine change,
    et c'est la condition pour que l'arbitrage compare des machines et non des
    transcriptions."""
    from analyzer.vsm_drumkit import (DRUM_MACHINE_NOTES, DrumKit, DrumSlot,
                                      drum_machine_track, modelled_drum_track)
    kit = DrumKit(slots=[
        DrumSlot("kick", 0, 36, "k.wav", onsets=[0.0, 0.5, 1.0], velocities=[110, 100, 120], hit_count=3),
        DrumSlot("hihat", 1, 42, "h.wav", onsets=[0.25, 0.75], velocities=[90, 80], hit_count=2),
        DrumSlot("pedalhat", 2, 44, "p.wav", onsets=[1.25], velocities=[70], hit_count=1),
        DrumSlot("tom", 3, 45, "t.wav", onsets=[1.5], velocities=[100], hit_count=1),
    ], sample_rate=44100, total_hits=7)
    reference = modelled_drum_track(kit)
    for machine in ("vsm.tr909", "vsm.tr808"):
        piste = drum_machine_track(kit, machine)
        assert_equal(piste.machine, machine, "la machine est celle demandée")
        assert_equal(len(piste.notes), len(reference.notes), f"{machine} : même nombre de frappes")
        assert_equal([n.start for n in piste.notes], [n.start for n in reference.notes],
                     f"{machine} : mêmes instants")
        assert_equal([n.velocity for n in piste.notes], [n.velocity for n in reference.notes],
                     f"{machine} : mêmes vélocités")
        assert_true(all(n.note in DRUM_MACHINE_NOTES[machine].values() for n in piste.notes),
                    f"{machine} : chaque note est une voix de la machine")
    # La pédale n'existe sur aucune des deux : elle devient charleston fermée.
    p909 = drum_machine_track(kit, "vsm.tr909")
    assert_true(42 in [n.note for n in p909.notes if abs(n.start - 1.25) < 1e-9],
                "pedalhat -> charleston fermée (42)")
    # La 808 n'a pas de toms : rabattu ET dit.
    kit.warnings.clear()
    drum_machine_track(kit, "vsm.tr808")
    assert_true(any("toms" in w for w in kit.warnings), "le rabattement des toms est DIT")
