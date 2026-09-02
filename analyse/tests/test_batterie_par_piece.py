"""La batterie éclatée par pièce : la parité pour le kit (§ 4.4 du CDC).

La chaîne classe les frappes par pièce depuis longtemps et n'en rendait
qu'UNE piste. `eclater_par_piece` répartit les notes de la piste FINALE —
après arbitrage et réglage — en une piste par pièce, en appariant par les
INSTANTS de frappe : la note d'une pièce est celle qui sonne à ses instants,
et l'on ne rejoue jamais la logique de repli (qui écrit des avertissements et
tient un état d'occupation des voix).
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_drumkit import DrumKit, DrumSlot, eclater_par_piece  # noqa: E402
from analyzer.vsm_project_export import ExportNote, ExportTrack  # noqa: E402


def frappe(note, start):
    return ExportNote(note=note, velocity=100, start=start, duration=0.05)


def piece(famille, onsets, midi_note=36):
    return DrumSlot(family=famille, slot=0, midi_note=midi_note, sample_path="",
                    onsets=list(onsets), velocities=[100] * len(onsets),
                    hit_count=len(onsets))


def kit_de(*pieces):
    return DrumKit(slots=list(pieces), sample_rate=48000,
                   total_hits=sum(p.hit_count for p in pieces))


@test
def chaque_piece_recoit_sa_piste_et_rien_ne_se_perd():
    kit = kit_de(piece("kick", [0.0, 1.0, 2.0]), piece("hihat", [0.5, 1.5]))
    piste = ExportTrack(name="Batterie", machine="vsm.tr909",
                        parameters={"kick.decay": 0.3}, is_drums=True,
                        notes=[frappe(36, 0.0), frappe(36, 1.0), frappe(36, 2.0),
                               frappe(42, 0.5), frappe(42, 1.5)])
    pistes = eclater_par_piece(piste, kit)
    assert_equal([p.name for p in pistes],
                 ["Batterie · kick", "Batterie · hihat"],
                 "une piste par pièce, dans l'ordre du kit")
    assert_equal(sum(len(p.notes) for p in pistes), 5, "aucune frappe perdue")
    for p in pistes:
        assert_equal(p.machine, "vsm.tr909", "même machine pour tout le kit")
        assert_equal(p.parameters, {"kick.decay": 0.3},
                     "même patch : le kit est réglé UNE fois")
        assert_true(p.is_drums, "les sous-pistes restent des pistes de batterie")


@test
def deux_pieces_rabattues_sur_la_meme_voix_restent_ensemble():
    """Le tom rabattu sur la voix du kick (le repli mesuré du § 5 nonies) :
    les séparer mentirait sur ce que la machine joue. Ils partent ensemble,
    sous un nom composé qui DIT le rabattement."""
    kit = kit_de(piece("kick", [0.0, 1.0]), piece("tom", [0.25]),
                 piece("hihat", [0.5]))
    piste = ExportTrack(name="Batterie", machine="vsm.tr808", is_drums=True,
                        notes=[frappe(36, 0.0), frappe(36, 1.0),
                               frappe(36, 0.25),           # le tom, sur la voix du kick
                               frappe(42, 0.5)])
    pistes = eclater_par_piece(piste, kit)
    assert_equal([p.name for p in pistes],
                 ["Batterie · kick+tom", "Batterie · hihat"],
                 "le rabattement se lit dans le nom")
    assert_equal(len(pistes[0].notes), 3, "kick et tom ensemble")


@test
def une_note_hors_detection_part_dans_autres_et_ne_disparait_pas():
    kit = kit_de(piece("kick", [0.0]))
    piste = ExportTrack(name="Batterie", machine="vsm.drums", is_drums=True,
                        notes=[frappe(36, 0.0), frappe(49, 9.9)])  # 49 : d'où ?
    pistes = eclater_par_piece(piste, kit)
    assert_equal([p.name for p in pistes], ["Batterie · kick", "Batterie · autres"],
                 "l'inexpliqué est une piste, pas une disparition")
    assert_equal(len(pistes[1].notes), 1, "la note orpheline est là")


@test
def l_eclatement_est_deterministe():
    kit = kit_de(piece("kick", [0.0, 1.0]), piece("snare", [0.5]),
                 piece("hihat", [0.25, 0.75]))
    notes = [frappe(36, 0.0), frappe(36, 1.0), frappe(38, 0.5),
             frappe(42, 0.25), frappe(42, 0.75)]
    piste = ExportTrack(name="Batterie", machine="vsm.tr909", is_drums=True, notes=notes)
    a = eclater_par_piece(piste, kit)
    piste2 = ExportTrack(name="Batterie", machine="vsm.tr909", is_drums=True,
                         notes=list(reversed(notes)))
    b = eclater_par_piece(piste2, kit)
    cle = lambda ps: [(p.name, [(n.note, n.start) for n in p.notes]) for p in ps]
    assert_equal(cle(a), cle(b), "l'ordre d'arrivée des notes ne change rien")


if __name__ == "__main__":
    raise SystemExit(run())
