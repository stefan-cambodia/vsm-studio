"""Tests de la phase A1 — le classifieur de machine.

Ce que ces tests VÉRIFIENT, et qui n'est pas le score du modèle : le score se
mesure sur un vrai corpus et se publie, il n'a pas sa place dans une suite de
tests (il changerait à chaque régénération). Ce qui se teste, c'est ce qui doit
être vrai QUEL QUE SOIT le corpus :

  - la coupure entraînement/épreuve ne laisse aucun patch des deux côtés,
    faute de quoi tout score publié serait faux vers le haut ;
  - deux entraînements sur le même corpus donnent les MÊMES VERDICTS (§ 8.4) ;
  - un son hors du parc déclenche l'abstention plutôt qu'un score confiant ;
  - un modèle dont les empreintes ne correspondent plus est REFUSÉ.

Le corpus d'essai est engendré ici, minuscule et par le vrai moteur : trois
machines, quelques patchs. Aucun test ne dépend d'un corpus installé.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer.vsm_classifier import (Classifieur, charge_corpus, coupe_par_patch,
                                      entraine, matrice_de_confusion)  # noqa: E402
from analyzer.vsm_corpus_build import (GrilleDeNotes, genere_lot, machine_fingerprint,
                                        nouveau_manifeste)  # noqa: E402
from analyzer.vsm_engine import VsmEngine  # noqa: E402
from analyzer.vsm_patch_optimizer import search_space_for_machine  # noqa: E402

SR = 44100
MACHINES = ("vsm.minimoog", "vsm.juno106", "vsm.dx7")
GRILLE = GrilleDeNotes(hauteurs=(48, 64), durees=(0.5,), velocites=(60, 105))

_CORPUS_ESSAI: dict = {}


def corpus_d_essai() -> Path:
    """Engendre une fois un corpus minuscule, réemployé par tous les tests.

    Un corpus par test coûterait une minute chacun ; ils ne le modifient pas,
    et le partager ne crée donc pas de dépendance entre eux — seulement une
    dépendance à ce corpus-ci, qui est engendré, pas installé.
    """
    if "chemin" in _CORPUS_ESSAI:
        return _CORPUS_ESSAI["chemin"]

    dossier = Path(tempfile.mkdtemp(prefix="vsm-classifieur-"))
    with VsmEngine(sample_rate=SR) as moteur:
        manifeste = nouveau_manifeste(SR, 5, 12, GRILLE, [], "2026-08-23T00:00:00+00:00")
        for machine in MACHINES:
            manifeste.empreintes[machine] = machine_fingerprint(moteur, machine, SR)
            espace = search_space_for_machine(machine, moteur)
            (dossier / machine).mkdir(parents=True, exist_ok=True)
            lot = genere_lot(machine, espace, moteur, patchs=12, grille=GRILLE,
                             graine=5, sample_rate=SR)
            lot.enregistre(dossier / machine / "lot-000.npz")
            manifeste.exemples[machine] = len(lot.X)
        manifeste.enregistre(dossier / "manifeste.json")
    _CORPUS_ESSAI["chemin"] = dossier
    return dossier


@test
def classifieur_le_corpus_se_relit_avec_ses_patchs():
    corpus = charge_corpus(corpus_d_essai())
    assert_equal(sorted(corpus.noms), sorted(MACHINES), "machines du manifeste")
    assert_true(len(corpus.X) > 50, f"{len(corpus.X)} exemples seulement")
    assert_equal(len(corpus.patchs), len(corpus.X), "un numéro de patch par exemple")
    assert_true(len(np.unique(corpus.patchs)) > 1,
                "les numéros de patch doivent varier — sinon la coupure par patch "
                "ne peut rien couper")


@test
def classifieur_la_coupure_ne_laisse_aucun_patch_des_deux_cotes():
    """LE test de méthode. Les seize notes d'un patch sont seize vues du même
    son ; les répartir au hasard mettrait à l'entraînement exactement ce qu'on
    demande de reconnaître à l'épreuve, et le score serait faux vers le haut
    sans que rien ne le montre."""
    corpus = charge_corpus(corpus_d_essai())
    entrainement, epreuve = coupe_par_patch(corpus, part_epreuve=0.25, graine=1)
    assert_true(len(entrainement) > 0 and len(epreuve) > 0, "les deux côtés sont peuplés")
    assert_equal(len(set(entrainement.tolist()) & set(epreuve.tolist())), 0,
                 "aucun exemple des deux côtés")
    groupes = corpus.groupes()
    communs = set(groupes[entrainement].tolist()) & set(groupes[epreuve].tolist())
    assert_equal(len(communs), 0, f"{len(communs)} patch(s) des deux côtés de la coupure")
    # Chaque machine doit être représentée à l'épreuve, sinon son score n'existe pas.
    assert_equal(len(np.unique(corpus.machines[epreuve])), len(corpus.noms),
                 "toutes les machines sont éprouvées")


@test
def classifieur_deux_entrainements_donnent_les_memes_verdicts():
    """Exigence n° 4 du § 8 : l'identité bit à bit des poids est souhaitée,
    celle des VERDICTS est exigée."""
    corpus = charge_corpus(corpus_d_essai())
    premier, mesures_a = entraine(corpus, graine=3, part_epreuve=0.25)
    second, mesures_b = entraine(corpus, graine=3, part_epreuve=0.25)
    _, epreuve = coupe_par_patch(corpus, 0.25, 3)
    matrice_a = matrice_de_confusion(corpus, premier, epreuve)
    matrice_b = matrice_de_confusion(corpus, second, epreuve)
    assert_true(np.array_equal(matrice_a, matrice_b), "mêmes prédictions sur l'épreuve")
    assert_equal(mesures_a["top1"], mesures_b["top1"], "même score")
    assert_equal(mesures_a["ambigus"], mesures_b["ambigus"], "mêmes cas ambigus")


@test
def classifieur_s_abstient_devant_ce_qu_il_n_a_jamais_vu():
    """Critère 4 du § 4. « Un violon classé MS-20 avec assurance serait le pire
    résultat possible de ce projet » — on éprouve ici avec un descripteur
    volontairement hors de tout ce que le corpus contient."""
    corpus = charge_corpus(corpus_d_essai())
    classifieur, _ = entraine(corpus, graine=4, part_epreuve=0.25)

    # Un son du parc : le classement doit être utilisable.
    _, epreuve = coupe_par_patch(corpus, 0.25, 4)
    classement, motif = classifieur.classe(corpus.X[epreuve[0]])
    assert_equal(len(classement), 3, "un score par machine (k borné par le parc)")
    assert_true(abs(sum(score for _, score in classement) - 1.0) < 1e-6,
                "les probabilités somment à 1")

    # Un vecteur ABERRANT : loin du corpus dans toutes les directions.
    aberrant = classifieur.moyenne + classifieur.echelle * 50.0
    _, motif_aberrant = classifieur.classe(aberrant)
    assert_true(motif_aberrant, "un son hors du parc doit déclencher l'abstention")
    assert_true("rayon" in motif_aberrant or "seuil" in motif_aberrant,
                f"le motif doit dire POURQUOI : « {motif_aberrant} »")


@test
def classifieur_fait_l_aller_retour_par_le_disque():
    corpus = charge_corpus(corpus_d_essai())
    classifieur, _ = entraine(corpus, graine=6, part_epreuve=0.25)
    _, epreuve = coupe_par_patch(corpus, 0.25, 6)
    attendu, motif_attendu = classifieur.classe(corpus.X[epreuve[0]])

    with tempfile.TemporaryDirectory(prefix="vsm-modele-") as dossier:
        chemin = Path(dossier) / "classifieur.joblib"
        classifieur.enregistre(chemin)
        relu = Classifieur.relit(chemin)
    obtenu, motif_obtenu = relu.classe(corpus.X[epreuve[0]])
    assert_equal([n for n, _ in obtenu], [n for n, _ in attendu], "même classement")
    assert_equal(motif_obtenu, motif_attendu, "même verdict d'abstention")
    assert_equal(relu.empreintes, classifieur.empreintes, "empreintes conservées")


@test
def classifieur_perime_est_refuse_et_non_applique():
    """La contrepartie de la péremption du corpus (A0.3). Un modèle entraîné
    sur le son d'hier ne se trompe pas bruyamment : il classe plausiblement et
    faux. Il doit donc être refusé."""
    corpus = charge_corpus(corpus_d_essai())
    classifieur, _ = entraine(corpus, graine=8, part_epreuve=0.25)
    with VsmEngine(sample_rate=SR) as moteur:
        frais = classifieur.verifie_fraicheur(moteur, SR)
        assert_true(frais.frais, f"tout juste entraîné : {frais.resume()}")

        classifieur.empreintes["vsm.dx7"] = "0" * 64
        perime = classifieur.verifie_fraicheur(moteur, SR)
    assert_true(not perime.frais, "une empreinte qui a bougé doit être détectée")
    assert_equal(perime.perimees, ("vsm.dx7",), "la machine périmée est nommée")
