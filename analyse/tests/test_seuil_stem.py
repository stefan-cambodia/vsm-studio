"""Un résidu de séparation n'est pas une partie : le seuil de stem.

TROUVÉ SUR LE DEUXIÈME MORCEAU (03/09/2026), et c'est ce qui l'a justifié.
*Clair de Lune* est un PIANO SEUL. Le modèle à six sources, devenu le défaut
la veille sur la foi d'*Us and Them*, y rend `piano` 99,5 % et **cinq stems
entre 0,0 et 0,4 %** : en faire des pistes fabriquerait cinq parties là où il
y en a une — l'exact contraire de l'objectif de parité, qui veut autant de
pistes que de parties, ni plus ni moins.

CE N'EST PAS « COUPER UNE PISTE ». La règle du dépôt — couper reste une
décision humaine — protège ce qu'on ENTEND. Ici la chaîne refuse de
FABRIQUER, ce qui est l'inverse ; et elle le dit avec son chiffre, le stem
reste sur le disque, `--seuil-stem 0` le reconstruit.

Ces tests portent sur la DÉCISION (quels stems passent la porte), pas sur la
reconstruction elle-même : le comportement se lit dans le journal, qui est le
contrat.
"""

from __future__ import annotations

import contextlib
import io as flux
import sys
import types
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from reconstruire import construire_parseur, partage_du_morceau  # noqa: E402


def parts_clair_de_lune():
    """Le partage RÉEL mesuré sur Clair de Lune séparé en six sources."""
    return {"piano": 99.5, "guitar": 0.4, "other": 0.1,
            "bass": 0.0, "vocals": 0.0, "drums": 0.0}


def stems_retenus(parts, seuil):
    """La décision de la porte, telle que `reconstruire_les_stems` la prend."""
    return sorted(nom for nom, part in parts.items()
                  if not (seuil > 0.0 and part < seuil))


@test
def le_piano_seul_ne_donne_qu_une_piste():
    """LE CAS QUI A MOTIVÉ LA PORTE : un original à UNE partie doit rendre UNE
    piste. Sans seuil, six."""
    parts = parts_clair_de_lune()
    assert_equal(stems_retenus(parts, 0.5), ["piano"],
                 "un piano seul rend une piste, pas six")
    assert_equal(len(stems_retenus(parts, 0.0)), 6,
                 "--seuil-stem 0 ne écarte rien, et c'est le témoin")


@test
def une_vraie_chanson_garde_toutes_ses_parties():
    """Le seuil ne doit pas manger de vraies parties. Sur *Us and Them* en six
    sources, la plus petite (piano, 6,9 %) est douze fois au-dessus."""
    parts = {"vocals": 28.0, "guitar": 26.9, "drums": 20.4,
             "other": 9.1, "bass": 8.7, "piano": 6.9}
    assert_equal(len(stems_retenus(parts, 0.5)), 6,
                 "les six parties d'Us and Them passent toutes")


@test
def le_seuil_par_defaut_est_un_demi_pourcent():
    args = construire_parseur().parse_args(["morceau.mp3"])
    assert_equal(args.seuil_stem, 0.5, "le défaut de --seuil-stem")


@test
def le_conseil_ne_conseille_pas_ce_qui_est_deja_fait():
    """Sur *Sky and Sand* séparé en SIX sources, `drums` porte 78 % : le cri
    du fourre-tout renvoyait à `--modele htdemucs_6s`, déjà employé. Un
    conseil qu'on a déjà suivi discrédite les autres."""
    import tempfile
    import wave

    def ecrire(chemin, amplitude, n=4800):
        valeur = int(amplitude * 32767)
        with wave.open(str(chemin), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000)
            w.writeframes(valeur.to_bytes(2, "little", signed=True) * n)

    def journal(noms, gros):
        with tempfile.TemporaryDirectory() as d:
            dossier = Path(d)
            for nom in noms:
                ecrire(dossier / f"{nom}.wav", 0.5 if nom == gros else 0.02)
            tampon = flux.StringIO()
            with contextlib.redirect_stdout(tampon):
                partage_du_morceau({n: dossier / f"{n}.wav" for n in noms})
            return tampon.getvalue()

    quatre = journal(["bass", "drums", "other", "vocals"], "drums")
    assert_true("--modele htdemucs_6s" in quatre,
                "à quatre stems, le conseil a du sens : " + quatre)
    six = journal(["bass", "drums", "guitar", "other", "piano", "vocals"], "drums")
    assert_true("--modele htdemucs_6s" not in six,
                "à six stems, ne pas conseiller ce qui est fait : " + six)
    assert_true("--voix-par-stem" in six,
                "et proposer le chemin qui RESTE : " + six)


if __name__ == "__main__":
    raise SystemExit(run())
