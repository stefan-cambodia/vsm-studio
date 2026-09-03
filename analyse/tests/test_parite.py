"""`--parite` : viser autant de pistes que le morceau a de parties.

L'objectif de l'utilisateur (§ 0 du CDC détection-multipiste) est la parité :
« si un original comporte 15 postes, la reconstruction doit comporter 15
pistes également ; si l'original comporte 64 pistes, la reconstruction doit en
comporter 64 ». Trois découpages y mènent — les voix par registres, la
batterie par pièce, la tête et les chœurs — et il faut les TROIS. Personne ne
devrait avoir à les retenir : `--parite` les allume, en le disant.

Ce que ces tests fixent : le raccourci allume bien les trois, il n'écrase PAS
une option écrite à la main (un A/B sur un seul découpage doit rester
possible), et il ne s'allume pas tout seul.
"""

from __future__ import annotations

import contextlib
import io as flux
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from reconstruire import construire_parseur, valider_entree  # noqa: E402


def options(*arguments):
    """Les options APRÈS validation — c'est là que le raccourci s'applique.

    `valider_entree` finit par vérifier que le fichier existe et lève
    `Abandon` sinon ; on lui donne donc un vrai fichier vide, ce qui laisse
    tourner toute la partie qui nous intéresse.
    """
    import tempfile
    from reconstruire import Abandon
    with tempfile.NamedTemporaryFile(suffix=".wav") as fichier:
        args = construire_parseur().parse_args([fichier.name, *arguments])
        tampon = flux.StringIO()
        with contextlib.redirect_stdout(tampon):
            try:
                valider_entree(args)
            except (SystemExit, Abandon):
                pass
    return args, tampon.getvalue()


@test
def sans_parite_rien_n_est_allume():
    """Le défaut ne découpe rien : chaque découpage a un coût, et aucun ne
    s'impose sans être demandé."""
    args, _ = options()
    assert_equal(args.voix_par_stem, 0, "aucune voix")
    assert_equal(args.batterie_par_piece, False, "batterie entière")
    assert_equal(args.voix_tete_choeurs, False, "voix entière")
    assert_equal(args.voix_par_vides, False, "registres entiers")
    assert_equal(args.parite, False, "le raccourci ne s'allume pas seul")


@test
def parite_allume_les_quatre_decoupages_et_le_dit():
    args, journal = options("--parite")
    assert_equal(args.voix_par_stem, 4, "voix par registres")
    assert_equal(args.batterie_par_piece, True, "batterie par pièce")
    assert_equal(args.voix_tete_choeurs, True, "tête et chœurs")
    assert_equal(args.voix_par_vides, True, "registres par les vides")
    for attendu in ("--voix-par-stem 4", "--batterie-par-piece", "--voix-tete-choeurs",
                    "--voix-par-vides"):
        assert_true(attendu in journal, f"« {attendu} » doit être dit : {journal}")
    assert_true("+9,1 %" in journal,
                "le coût mesuré du découpage en voix est rappelé : " + journal)


@test
def une_option_ecrite_a_la_main_l_emporte():
    """Un A/B sur UN seul découpage doit rester possible sans démonter le
    raccourci : `--parite --voix-par-stem 8` doit donner huit voix, pas
    quatre."""
    args, journal = options("--parite", "--voix-par-stem", "8")
    assert_equal(args.voix_par_stem, 8, "la valeur explicite l'emporte")
    assert_true("--voix-par-stem" not in journal.split("la parité")[0],
                "le raccourci ne s'attribue pas ce qu'il n'a pas allumé : " + journal)
    assert_equal(args.batterie_par_piece, True, "les autres sont allumés quand même")


@test
def parite_va_dans_la_provenance_avec_ses_consequences():
    """Deux rapports dont le découpage diffère ne se comparent pas. Le
    raccourci est inscrit, ET les trois options à leur valeur effective —
    l'un dit comment la course a été demandée, les autres ce qu'elle a fait."""
    from reconstruire import provenance
    args, _ = options("--parite")
    p = provenance(args, None, None)
    assert_equal(p["options"]["parite"], True, "le raccourci est inscrit")
    assert_equal(p["options"]["voixParStem"], 4, "et sa conséquence aussi")
    assert_equal(p["options"]["batterieParPiece"], True, "et celle-ci")
    assert_equal(p["options"]["voixTeteChoeurs"], True, "et celle-là")
    assert_equal(p["options"]["voixParVides"], True, "et la quatrième")


if __name__ == "__main__":
    raise SystemExit(run())
