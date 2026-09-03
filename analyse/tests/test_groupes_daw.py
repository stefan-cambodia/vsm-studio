"""Les pistes d'un groupe arrivent dans le DAW sous un bus de groupe.

Ce que ces tests fixent : une piste de groupe par groupe d'au moins deux
membres, ajoutée EN FIN de liste (aucun index existant ne bouge), les
membres routés vers elle ; le projet écrit porte `kind: group` et `output` ;
un projet sans groupe n'écrit rien de nouveau.
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_project_export import ExportNote, ExportTrack, write_project_bundle  # noqa: E402
from reconstruire import ajouter_groupes  # noqa: E402


def pistes():
    note = [ExportNote(60, 100, 0.0, 0.5)]
    return [ExportTrack(name="bass", machine="vsm.tb303", notes=note),
            ExportTrack(name="other · voix 1", machine="vsm.tb303", notes=note),
            ExportTrack(name="other · voix 2", machine="vsm.tb303", notes=note),
            ExportTrack(name="Batterie · kick", machine="vsm.tr808", notes=note, is_drums=True),
            ExportTrack(name="Batterie · hihat", machine="vsm.tr808", notes=note, is_drums=True),
            ExportTrack(name="Voix", audio_path="samples/voix.wav", audio_sample_rate=44100.0,
                        audio_frames=10, audio_channels=1)]


GROUPES = {"other · voix 1": "other", "other · voix 2": "other",
           "Batterie · kick": "Batterie", "Batterie · hihat": "Batterie"}


@test
def un_groupe_par_stem_partage_ajoute_en_fin_de_liste():
    p = pistes()
    crees = ajouter_groupes(p, GROUPES)
    assert_equal(crees, ["other", "Batterie"], "deux groupes, dans l'ordre de leurs premières pistes")
    assert_equal([t.name for t in p[:6]], [t.name for t in pistes()], "aucun index existant ne bouge")
    assert_equal([t.name for t in p[6:]], ["other", "Batterie"], "les groupes sont en fin de liste")
    assert_true(p[6].is_group and p[7].is_group, "ce sont des pistes de groupe")
    assert_equal([t.output_group for t in p[:6]], [-1, 6, 6, 7, 7, -1], "les membres sont routés, les autres non")


@test
def un_groupe_d_un_seul_membre_n_existe_pas():
    p = pistes()
    crees = ajouter_groupes(p, {"Batterie · kick": "Batterie"})
    assert_equal(crees, [], "un membre seul n'est pas un groupe")
    assert_equal(len(p), 6, "rien d'ajouté")


@test
def le_projet_ecrit_porte_le_groupe_et_le_routage():
    p = pistes()
    ajouter_groupes(p, GROUPES)
    with tempfile.TemporaryDirectory() as d:
        write_project_bundle(p, Path(d))
        projet = json.loads((Path(d) / "project.json").read_text(encoding="utf-8"))
    pistes_json = projet["tracks"]
    assert_equal(pistes_json[6]["kind"], "group", "kind: group")
    assert_equal(pistes_json[6]["name"], "other", "nommé comme le stem")
    assert_equal(pistes_json[1]["output"], 6, "la voix 1 va au groupe other")
    assert_equal(pistes_json[4]["output"], 7, "le charleston va au groupe Batterie")
    assert_true("output" not in pistes_json[0], "la basse va au master, rien d'écrit")
    assert_true("instrument" not in pistes_json[6], "un groupe n'a pas d'instrument")
    assert_equal(projet["version"], 2, "un projet à groupes est en version 2")


@test
def sans_groupe_le_projet_ne_change_pas():
    with tempfile.TemporaryDirectory() as d:
        write_project_bundle(pistes()[:1], Path(d))
        projet = json.loads((Path(d) / "project.json").read_text(encoding="utf-8"))
    assert_true("output" not in projet["tracks"][0] and "kind" not in projet["tracks"][0],
                "ni routage ni kind sur une piste ordinaire")
    assert_equal(projet["version"], 1, "version 1 conservée")


@test
def les_couleurs_de_piste_sont_opaques_en_argb():
    """Le DAW lit « #AARRGGBB » : une couleur écrite en RGBA lui donne un alpha
    quelconque, et « #06D6A0FF » rendait ses notes transparentes."""
    with tempfile.TemporaryDirectory() as d:
        write_project_bundle(pistes(), Path(d))
        projet = json.loads((Path(d) / "project.json").read_text(encoding="utf-8"))
    for piste in projet["tracks"]:
        couleur = piste["color"]
        assert_true(len(couleur) == 9 and couleur.startswith("#FF"),
                    f"{piste['name']} : {couleur} doit être opaque en ARGB")


if __name__ == "__main__":
    raise SystemExit(run())
