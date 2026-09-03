"""Le recalage après un changement de patch suit le GROUPE (§ 7 du CDC
multipiste, revenu par le verdict et le réglage au mélange).

Le calage des pistes groupées avait été corrigé au § 7 pour le calage initial,
mais le verdict du mélange et le réglage au mélange recalaient encore la
piste SEULE contre le stem entier après chaque essai de patch : une pièce de
batterie ou une voix y recevait le gain qu'il faudrait pour remplacer tout le
stem. Mesuré : batterie-v2, +28 % contre le témoin. Ces tests fixent que le
recalage d'un membre recale tout son groupe, ensemble, et qu'une piste
ordinaire reste calée seule.
"""
from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer import vsm_levels  # noqa: E402
from analyzer.vsm_project_export import ExportTrack  # noqa: E402


def pistes():
    return [ExportTrack(name="bass", machine="m"),
            ExportTrack(name="Batterie · kick", machine="m"),
            ExportTrack(name="Batterie · hihat", machine="m"),
            ExportTrack(name="other · voix 1", machine="m"),
            ExportTrack(name="other · voix 2", machine="m")]


GROUPES = {"Batterie · kick": "Batterie", "Batterie · hihat": "Batterie",
           "other · voix 1": "other", "other · voix 2": "other"}


def espion():
    appels = []

    def faux(tracks, stems_audio, samples_root, sample_rate, groupes=None):
        appels.append(([t.name for t in tracks], dict(groupes or {})))
        return []
    return appels, faux


@test
def un_membre_recale_tout_son_groupe_ensemble():
    appels, faux = espion()
    original = vsm_levels.match_track_levels
    vsm_levels.match_track_levels = faux
    try:
        p = pistes()
        vsm_levels.recaler_avec_son_groupe(p[1], p, {}, Path("."), 44100, GROUPES)
    finally:
        vsm_levels.match_track_levels = original
    assert_equal(len(appels), 1, "un seul calage")
    assert_equal(appels[0][0], ["Batterie · kick", "Batterie · hihat"],
                 "les deux pièces, pas la seule qu'on a touchée — et pas les voix d'other")
    assert_equal(appels[0][1], GROUPES, "avec le registre des groupes, donc le calage PAR GROUPE")


@test
def une_piste_ordinaire_reste_calee_seule():
    appels, faux = espion()
    original = vsm_levels.match_track_levels
    vsm_levels.match_track_levels = faux
    try:
        p = pistes()
        vsm_levels.recaler_avec_son_groupe(p[0], p, {}, Path("."), 44100, GROUPES)
        vsm_levels.recaler_avec_son_groupe(p[0], p, {}, Path("."), 44100, None)
    finally:
        vsm_levels.match_track_levels = original
    assert_equal([a[0] for a in appels], [["bass"], ["bass"]], "la basse seule, avec ou sans registre")
    assert_true(all(a[1] == {} for a in appels), "sans groupe : le chemin d'origine, inchangé")


if __name__ == "__main__":
    raise SystemExit(run())
