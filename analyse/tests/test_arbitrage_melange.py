"""Les machines que l'arbitrage remet en jeu au verdict du mélange.

Ce que ces tests verrouillent est une leçon, pas une commodité : le classement
d'une piste CONTRE SON STEM ne prédit pas son classement DANS LE MÉLANGE, et
sur *Sky and Sand* les deux sont à peu près inverses (§ 5 decies de
`ROADMAP-fusion.md`). La machine que le mélange retient pour la basse est
troisième au stem, à 17,6 % de la gagnante ; l'ancien seuil de 2 % ne pouvait
structurellement pas la voir.
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer.vsm_track_arbitration import (TrackCandidate, TrackVerdict,  # noqa: E402
                                             build_candidates, runners_up)


def _verdicts():
    """Le classement RÉEL de la basse de *Sky and Sand*, distances comprises."""
    return [
        TrackVerdict("vsm.wavetable", {}, "patch d'usine", 0.3692),
        TrackVerdict("vsm.wavetable", {"cutoff": 0.5}, "patch cherché", 0.4010),
        TrackVerdict("vsm.minimoog", {}, "patch d'usine", 0.4303),
        TrackVerdict("vsm.phasedist", {}, "patch d'usine", 0.4340),
        TrackVerdict("vsm.prophet", {}, "patch d'usine", 0.4449),
        TrackVerdict("vsm.multisample", {}, "patch d'usine", 0.4578),
    ]


@test
def arbitrage_les_suivantes_sont_des_machines_et_pas_des_patchs():
    """Un second patch de la gagnante ne répare pas un mauvais choix de machine.

    C'est le piège que la liste de verdicts tend : `vsm.wavetable` y figure deux
    fois, avec deux patchs. La seconde entrée est meilleure que `vsm.minimoog`
    et doit pourtant être sautée -- sans quoi la place d'une machine
    concurrente serait prise par un patch de la gagnante.
    """
    suivantes = runners_up(_verdicts(), count=3)
    assert_equal([v.machine for v in suivantes],
                 ["vsm.minimoog", "vsm.phasedist", "vsm.prophet"],
                 "trois machines DISTINCTES, la gagnante exclue")
    assert_true(all(v.machine != "vsm.wavetable" for v in suivantes),
                "aucun second patch de la gagnante")


@test
def arbitrage_la_troisieme_est_remise_en_jeu_malgre_l_ecart():
    """Le cas mesuré : la gagnante du mélange est à 17,6 % au stem.

    Sur la basse de *Sky and Sand*, `vsm.phasedist` est troisième au stem
    (0,4340 contre 0,3692) et PREMIÈRE au mélange (0,2557 contre 0,2933). Un
    seuil relatif de 2 % -- celui d'avant -- l'aurait écartée sans la mesurer.
    """
    verdicts = _verdicts()
    gagnante = verdicts[0].distance
    suivantes = runners_up(verdicts, count=3)
    phasedist = next(v for v in suivantes if v.machine == "vsm.phasedist")
    ecart = (phasedist.distance - gagnante) / gagnante
    assert_true(ecart > 0.15, f"l'écart au stem est franc ({ecart*100:.1f} %)")
    assert_true(ecart > 0.02, "et il dépasse largement l'ancien seuil de 2 %")


@test
def arbitrage_les_suivantes_ne_supposent_rien_quand_il_n_y_a_rien():
    """Aucun verdict, aucune candidate d'une autre machine : on rend une liste
    vide plutôt que d'inventer une proposition. Le verdict du mélange n'a alors
    que l'existant à juger, ce qui est la bonne réponse."""
    assert_equal(runners_up([]), [], "aucun verdict -> aucune suivante")
    seule = [TrackVerdict("vsm.wavetable", {}, "patch d'usine", 0.37)]
    assert_equal(runners_up(seule), [], "une seule machine -> aucune suivante")
    assert_equal(len(runners_up(_verdicts(), count=1)), 1, "le compte est respecté")


@test
def arbitrage_le_temoin_a_zero_rend_exactement_la_chaine_d_avant():
    """`--machines-au-melange 0` : la gagnante du stem part SEULE au verdict.

    C'est le témoin des A/B, et il doit être atteignable par la ligne de
    commande. Il ne l'était pas : le nombre vivait dans une constante du
    module, si bien que comparer au comportement d'avant le § 5 decies
    demandait de MODIFIER le code entre les deux mesures -- deux rapports
    produits par deux programmes différents, et la provenance d'A4.2 n'en
    disait rien. L'option existe pour que le témoin soit du même code que ce
    qu'il témoigne."""
    assert_equal(runners_up(_verdicts(), count=0), [], "0 -> aucune suivante")
    assert_equal(len(runners_up(_verdicts(), count=3)), 3, "3 -> trois suivantes")


@test
def melange_le_temoin_de_coupure_est_mesure_et_la_piste_est_QUAND_MEME_gardee():
    """Le morceau sans la piste : mesuré, publié, jamais joué.

    Ce que ce test verrouille est une DÉCISION et non une mécanique. Mesuré
    après coup (§ 5 decies), la basse publiée de *Sky and Sand* dégradait le
    mélange de 5,5 % par rapport au silence, et rien dans la chaîne n'était en
    mesure de le voir. Le chiffre est donc calculé à chaque verdict.

    Mais il ne CONCOURT PAS : une chaîne autorisée à supprimer une piste
    optimiserait la métrique en abandonnant le morceau. Le montage ci-dessous
    est le pire cas possible — la cible est le SILENCE, donc couper la piste
    est la réponse optimale au sens de la métrique — et la piste doit rester.
    """
    import tempfile

    import numpy as np

    from analyzer.vsm_engine import VsmEngine  # noqa: F401 — le moteur du rendu
    from analyzer.vsm_mix_verdict import MixAlternative, keep_what_helps_the_mix
    from analyzer.vsm_project_export import ExportNote, ExportTrack

    notes = [ExportNote(60, 100, 0.0, 0.4), ExportNote(64, 100, 0.5, 0.4)]
    piste = ExportTrack(name="bass", machine="vsm.minimoog", parameters={}, notes=list(notes))
    cible = np.zeros(int(1.2 * 44100), dtype=np.float32)   # le SILENCE : couper serait optimal
    dossier = Path(tempfile.mkdtemp(prefix="vsm-coupure-"))

    decisions = keep_what_helps_the_mix(
        [piste], {"bass": [MixAlternative(parameters={}, label="autre machine",
                                          machine="vsm.prophet")]},
        cible, {"bass": cible}, dossier, workdir=dossier / "verdict", sample_rate=44100)

    assert_equal(len(decisions), 1, "une décision pour la piste")
    assert_true(decisions[0].muted_distance is not None,
                "le témoin de coupure est MESURÉ, et pas laissé à None")
    assert_true(decisions[0].muted_distance < decisions[0].distance_kept,
                f"sur cible silencieuse, couper est meilleur "
                f"({decisions[0].muted_distance:.4f} contre {decisions[0].distance_kept:.4f})")
    assert_true(piste.machine in ("vsm.minimoog", "vsm.prophet"),
                f"et la piste est GARDÉE malgré tout ({piste.machine})")
    assert_true(piste.volume > 0.0, "gardée AUDIBLE : couper n'est pas une décision de la chaîne")


@test
def arbitrage_un_profil_multisample_est_une_candidate_a_part_entiere():
    """`vsm.multisample` part une fois PAR profil installé.

    Le premier morceau à saxophone l'a exigé : dix-neuf profils installés,
    un seul essayé, le ténor jamais concouru. Une liste de noms dans le
    dictionnaire des profils devient autant de candidates d'usine, chacune
    portant le sien -- et une machine à profil UNIQUE garde le comportement
    d'avant, à l'octet près."""
    candidates = build_candidates(
        [("vsm.minimoog", {"cutoff": 0.5})],
        ["vsm.minimoog", "vsm.multisample"],
        {"vsm.multisample": ["GU-Tenor-Sax", "GM-Warm-Pad", "YDP-Grand"]})
    usine = [c for c in candidates if c.origin == "patch d'usine"]
    multi = [c for c in usine if c.machine == "vsm.multisample"]
    assert_equal(len(multi), 3, "une candidate par profil")
    assert_equal(sorted(c.profile for c in multi),
                 ["GM-Warm-Pad", "GU-Tenor-Sax", "YDP-Grand"], "chacune le sien")
    mono = [c for c in usine if c.machine == "vsm.minimoog"]
    assert_equal(len(mono), 1, "machine sans profil : une seule candidate")


@test
def arbitrage_les_suivantes_gardent_une_machine_ET_son_profil():
    """`runners_up` déduplique par MACHINE : le meilleur profil représente la
    sienne, et il VOYAGE avec elle -- un « multisample » retenu sans son profil
    serait une décision inapplicable."""
    verdicts = [
        TrackVerdict("vsm.string", {}, "patch d'usine", 0.30),
        TrackVerdict("vsm.multisample", {}, "patch d'usine", 0.32, profile="GU-Tenor-Sax"),
        TrackVerdict("vsm.multisample", {}, "patch d'usine", 0.35, profile="GM-Warm-Pad"),
        TrackVerdict("vsm.piano", {}, "patch d'usine", 0.40),
    ]
    suivantes = runners_up(verdicts, count=3)
    assert_equal([v.machine for v in suivantes], ["vsm.multisample", "vsm.piano"],
                 "une seule entrée par machine, la meilleure d'abord")
    assert_equal(suivantes[0].profile, "GU-Tenor-Sax", "le profil gagnant voyage")
