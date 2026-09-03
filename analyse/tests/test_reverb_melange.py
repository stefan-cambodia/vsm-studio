"""H24 comme option : la réverbération cherchée au mélange.

Ce que ces tests fixent : les inserts d'une piste s'écrivent dans
project.json ; la recherche ne touche que les pistes mélodiques (ni batterie,
ni report d'audio) ; elle garde le point qui rapproche et REFUSE, en le
disant, quand aucun ne rapproche ; le rapport porte la grille entière.
"""
from __future__ import annotations

import contextlib
import io as flux
import json
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_project_export import ExportNote, ExportTrack, write_project_bundle  # noqa: E402
from reconstruire import GRILLE_REVERB, chercher_reverb_au_melange, effet_reverb  # noqa: E402


def pistes():
    note = [ExportNote(60, 100, 0.0, 0.5)]
    return [ExportTrack(name="bass", machine="vsm.tb303", notes=note),
            ExportTrack(name="Batterie", machine="vsm.tr808", notes=note, is_drums=True),
            ExportTrack(name="Voix", audio_path="samples/voix.wav", audio_sample_rate=44100.0,
                        audio_frames=10, audio_channels=1)]


def args():
    return SimpleNamespace(rendus_paralleles=2, tempo=120.0, metrique="v2", moteur=None)


@test
def les_inserts_d_une_piste_vont_dans_le_projet():
    piste = ExportTrack(name="bass", machine="vsm.tb303", notes=[ExportNote(60, 100, 0.0, 0.5)])
    piste.effects.append(effet_reverb(1.0, 0.04))
    with tempfile.TemporaryDirectory() as d:
        write_project_bundle([piste], Path(d))
        projet = json.loads((Path(d) / "project.json").read_text(encoding="utf-8"))
    effets = projet["tracks"][0]["effects"]
    assert_equal(len(effets), 1, "un insert écrit")
    assert_equal(effets[0]["type"], "reverb", "du type du DAW")
    assert_equal(effets[0]["parameters"]["effect.reverb.mix"], 0.04, "avec son dosage")


@test
def le_point_qui_rapproche_est_retenu_sur_les_seules_pistes_melodiques():
    export = pistes()
    vus = []

    def mesurer(p, etiquette="?"):
        vus.append(etiquette)
        dosage = next((e["parameters"]["effect.reverb.mix"] for t in p for e in t.effects), None)
        taille = next((e["parameters"]["effect.reverb.size"] for t in p for e in t.effects), None)
        assert_true(all(not t.effects for t in p if t.is_drums or t.audio_path),
                    "ni la batterie ni la voix ne reçoivent l'insert")
        if dosage is None:
            return 0.200
        return 0.190 if (taille, dosage) == (1.0, 0.04) else 0.205

    with contextlib.redirect_stdout(flux.StringIO()) as journal:
        bilan = chercher_reverb_au_melange(args(), Path("."), export, None, mesurer=mesurer)
    assert_equal(sorted(vus), sorted(["temoin"] + [f"p{int(t * 10)}-m{int(round(d * 100)):02d}"
                                                   for t, d in GRILLE_REVERB]),
                 "le témoin et chaque point de la grille sont rendus une fois")
    assert_equal(bilan["retenu"], {"taille": 1.0, "dosage": 0.04}, "le meilleur point est retenu")
    assert_equal(len(bilan["grille"]), len(GRILLE_REVERB), "le rapport porte la grille entière")
    assert_equal(export[0].effects[0]["parameters"]["effect.reverb.size"], 1.0, "posé sur la basse")
    assert_equal(export[1].effects, [], "pas sur la batterie")
    assert_equal(export[2].effects, [], "pas sur la voix")
    assert_true("RETENUE" in journal.getvalue(), "et c'est dit")


@test
def aucun_point_ne_rapproche_les_pistes_restent_seches_et_c_est_dit():
    export = pistes()

    def mesurer(p, etiquette="?"):
        return 0.200 if not any(t.effects for t in p) else 0.201

    with contextlib.redirect_stdout(flux.StringIO()) as journal:
        bilan = chercher_reverb_au_melange(args(), Path("."), export, None, mesurer=mesurer)
    assert_equal(bilan["retenu"], None, "rien de retenu")
    assert_true(all(not t.effects for t in export), "les pistes restent sèches")
    assert_true("AUCUN point ne rapproche" in journal.getvalue(), "le refus est dit")
    assert_equal(bilan["temoin"], 0.200, "avec son chiffre")


if __name__ == "__main__":
    raise SystemExit(run())
