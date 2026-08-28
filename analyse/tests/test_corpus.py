"""Tests de la phase A0 — corpus et infrastructure.

Les deux exigences que le § 3 du cahier des charges marque « (testé) » sont
ici : le DÉTERMINISME (A0.2) et la PÉREMPTION par empreinte (A0.3). Les autres
tests couvrent ce dont ces deux-là dépendent — les augmentations doivent être
seedées, le manifeste doit faire l'aller-retour, la grille doit être ce qu'elle
annonce.

Ces tests font tourner le VRAI moteur : c'est le seul moyen de vérifier qu'un
corpus est regénérable, puisque c'est le moteur qui produit les données.
"""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, test  # noqa: E402

from analyzer.vsm_corpus_build import (AUGMENTATIONS, GrilleDeNotes,  # noqa: E402
                                        LotDeCorpus, Manifeste, NOM_FUITE, applique_fuite,
                                        genere_lot, graine_de_machine, machine_fingerprint,
                                        machines_de_recherche, nouveau_manifeste,
                                        verifie_fraicheur)
from analyzer.vsm_engine import VsmEngine  # noqa: E402
from analyzer.vsm_patch_optimizer import search_space_for_machine  # noqa: E402

MACHINE = "vsm.minimoog"
SR = 44100

# Grille RÉDUITE pour les tests : deux hauteurs, une durée, deux vélocités.
# Les tests doivent rester rapides ; ce qu'ils vérifient — reproductibilité,
# péremption — ne dépend pas de la taille de la grille.
GRILLE_ESSAI = GrilleDeNotes(hauteurs=(48, 72), durees=(0.5,), velocites=(60, 105))


def _moteur() -> VsmEngine:
    return VsmEngine(sample_rate=SR)


# --- A0.2 : déterminisme ---------------------------------------------------

@test
def corpus_deux_generations_meme_graine_sont_identiques_au_bit_pres():
    """L'exigence n° 1 du § 3. Sans elle, aucune mesure faite sur le corpus
    n'est rejouable, et tout ce que les phases A1 à A3 publieront sera invérifiable."""
    with _moteur() as moteur:
        espace = search_space_for_machine(MACHINE, moteur)
        premier = genere_lot(MACHINE, espace, moteur, patchs=4, grille=GRILLE_ESSAI,
                             graine=7, sample_rate=SR,
                             augmentations=[a.nom for a in AUGMENTATIONS])
        second = genere_lot(MACHINE, espace, moteur, patchs=4, grille=GRILLE_ESSAI,
                            graine=7, sample_rate=SR,
                            augmentations=[a.nom for a in AUGMENTATIONS])
    assert_true(premier.X.size > 0, "le lot ne doit pas être vide")
    assert_equal(premier.X.shape, second.X.shape, "forme des descripteurs")
    assert_true(np.array_equal(premier.X, second.X), "descripteurs identiques au bit près")
    assert_true(np.array_equal(premier.Y, second.Y), "patchs identiques au bit près")
    assert_equal(premier.augmentations, second.augmentations, "mêmes augmentations tirées")


@test
def corpus_deux_graines_differentes_donnent_des_donnees_differentes():
    """Le pendant du test précédent : une graine qui ne change rien serait un
    déterminisme obtenu par accident, pas par construction."""
    with _moteur() as moteur:
        espace = search_space_for_machine(MACHINE, moteur)
        premier = genere_lot(MACHINE, espace, moteur, patchs=3, grille=GRILLE_ESSAI,
                             graine=1, sample_rate=SR)
        second = genere_lot(MACHINE, espace, moteur, patchs=3, grille=GRILLE_ESSAI,
                            graine=2, sample_rate=SR)
    assert_true(not np.array_equal(premier.Y, second.Y), "des graines distinctes, des patchs distincts")


@test
def corpus_la_graine_par_machine_ne_depend_pas_de_pythonhashseed():
    """`hash()` d'une chaîne est randomisé à chaque démarrage de Python. S'en
    servir donnerait un corpus qu'on CROIT regénérable et qui ne l'est pas —
    le pire des deux mondes, puisque rien ne le signalerait."""
    assert_equal(graine_de_machine("vsm.minimoog"), 74649, "graine stable")
    assert_true(graine_de_machine("vsm.minimoog") != graine_de_machine("vsm.juno106"),
                "deux machines, deux graines")


# --- A0.3 : péremption -----------------------------------------------------

@test
def corpus_une_empreinte_est_stable_et_propre_a_la_machine():
    with _moteur() as moteur:
        premiere = machine_fingerprint(moteur, MACHINE, SR)
        seconde = machine_fingerprint(moteur, MACHINE, SR)
        autre = machine_fingerprint(moteur, "vsm.juno106", SR)
        inconnue = machine_fingerprint(moteur, "vsm.inexistante", SR)
    assert_true(len(premiere) == 64, "SHA-256 en hexadécimal")
    assert_equal(premiere, seconde, "deux mesures de la même machine")
    assert_true(premiere != autre, "deux machines, deux empreintes")
    assert_equal(inconnue, "", "une machine injouable n'a pas d'empreinte")


@test
def corpus_une_empreinte_qui_change_marque_le_corpus_perime():
    """A0.3. Un modèle entraîné sur le son d'hier, appliqué au son
    d'aujourd'hui, ne produit pas d'erreur : il produit des verdicts plausibles
    et faux. C'est pourquoi la péremption se VÉRIFIE au lieu de se supposer."""
    with _moteur() as moteur:
        manifeste = nouveau_manifeste(SR, 1, 10, GRILLE_ESSAI, [], "2026-08-23T00:00:00+00:00")
        manifeste.empreintes[MACHINE] = machine_fingerprint(moteur, MACHINE, SR)
        manifeste.empreintes["vsm.juno106"] = machine_fingerprint(moteur, "vsm.juno106", SR)
        frais = verifie_fraicheur(manifeste, moteur)
        assert_true(frais.frais, f"corpus tout juste engendré : {frais.resume()}")

        # Le son du Juno a « bougé » : une empreinte falsifiée fait exactement
        # ce que ferait un vrai changement de DSP.
        manifeste.empreintes["vsm.juno106"] = "0" * 64
        perime = verifie_fraicheur(manifeste, moteur)
        assert_true(not perime.frais, "un corpus périmé doit être détecté")
        assert_equal(perime.perimees, ("vsm.juno106",), "la machine périmée est NOMMÉE")
        assert_true("vsm.juno106" in perime.resume(), "le résumé dit laquelle")

        # Une machine que le moteur ne sait plus jouer n'est pas « à jour » :
        # elle est INVÉRIFIABLE, et c'est une troisième réponse, pas une des deux.
        manifeste.empreintes = {"vsm.disparue": "0" * 64}
        introuvable = verifie_fraicheur(manifeste, moteur)
        assert_true(not introuvable.frais, "une machine disparue invalide la vérification")
        assert_equal(introuvable.invérifiables, ("vsm.disparue",), "elle est nommée comme invérifiable")


# --- A0.4 : augmentations --------------------------------------------------

@test
def corpus_les_augmentations_sont_seedees_et_changent_le_son():
    """Seedées, sinon le corpus n'est pas regénérable ; et elles doivent
    réellement dégrader, sinon elles ne servent à rien contre l'écart de domaine."""
    rng_source = np.random.default_rng(3)
    audio = (rng_source.standard_normal(4410) * 0.2).astype(np.float32)
    # Un signal utile plutôt que du bruit pur, pour que le désaccord ait un sens.
    t = np.arange(4410) / SR
    audio = (np.sin(2 * np.pi * 220 * t) * np.exp(-3 * t)).astype(np.float32)

    for augmentation in AUGMENTATIONS:
        premier = augmentation.applique(audio, SR, np.random.default_rng(11))
        second = augmentation.applique(audio, SR, np.random.default_rng(11))
        autre = augmentation.applique(audio, SR, np.random.default_rng(12))
        assert_equal(premier.shape, audio.shape, f"{augmentation.nom} : longueur conservée")
        assert_true(np.array_equal(premier, second),
                    f"{augmentation.nom} : même graine, même résultat")
        assert_true(np.all(np.isfinite(premier)), f"{augmentation.nom} : rien de non fini")
        assert_true(not np.array_equal(premier, audio),
                    f"{augmentation.nom} : doit changer quelque chose")
        assert_true(not np.array_equal(premier, autre),
                    f"{augmentation.nom} : une autre graine, un autre résultat")


@test
def corpus_la_fuite_ajoute_un_autre_son_a_bas_niveau():
    t = np.arange(4410) / SR
    source = (np.sin(2 * np.pi * 220 * t)).astype(np.float32)
    intrus = (np.sin(2 * np.pi * 660 * t)).astype(np.float32)
    melange = applique_fuite(source, intrus, np.random.default_rng(5))
    assert_equal(melange.shape, source.shape, "longueur conservée")
    assert_true(not np.array_equal(melange, source), "la fuite s'entend")
    ecart = float(np.sqrt(np.mean((melange - source) ** 2)))
    reference = float(np.sqrt(np.mean(source ** 2)))
    assert_true(0.01 * reference < ecart < 0.5 * reference,
                f"la fuite reste BASSE : {20 * np.log10(ecart / reference):.1f} dB")
    # Une fuite vide ne casse rien : c'est le cas du tout premier exemple d'un lot.
    assert_true(np.array_equal(applique_fuite(source, np.zeros(0, np.float32),
                                               np.random.default_rng(5)), source),
                "sans son à faire fuir, le signal passe tel quel")


# --- manifeste et lots -----------------------------------------------------

@test
def corpus_le_manifeste_fait_l_aller_retour():
    manifeste = nouveau_manifeste(SR, 42, 100, GRILLE_ESSAI, ["bruit", NOM_FUITE],
                                   "2026-08-23T12:00:00+00:00")
    manifeste.empreintes[MACHINE] = "a" * 64
    manifeste.exemples[MACHINE] = 1234
    manifeste.secondes[MACHINE] = 56.7
    with tempfile.TemporaryDirectory(prefix="vsm-corpus-") as dossier:
        chemin = Path(dossier) / "manifeste.json"
        manifeste.enregistre(chemin)
        relu = Manifeste.relit(chemin)
    assert_equal(relu.graine, 42, "graine")
    assert_equal(relu.patchs_par_machine, 100, "patchs")
    assert_equal(relu.empreintes[MACHINE], "a" * 64, "empreinte")
    assert_equal(relu.exemples[MACHINE], 1234, "compte d'exemples")
    assert_near(relu.secondes[MACHINE], 56.7, 1e-6, "coût mesuré")
    assert_equal(relu.augmentations, ["bruit", NOM_FUITE], "augmentations")
    assert_true(relu.versions.get("numpy"), "les versions sont inscrites")


@test
def corpus_un_manifeste_de_format_inconnu_est_refuse():
    """Refusé, pas lu au mieux : une clé mal interprétée ferait passer un
    corpus périmé pour frais."""
    with tempfile.TemporaryDirectory(prefix="vsm-corpus-") as dossier:
        chemin = Path(dossier) / "manifeste.json"
        chemin.write_text(json.dumps({"format": "autre-chose", "version": 1}), encoding="utf-8")
        try:
            Manifeste.relit(chemin)
        except ValueError as erreur:
            assert_true("format" in str(erreur), "le message dit ce qui cloche")
        else:
            assert_true(False, "un format inconnu doit être refusé")


@test
def corpus_un_lot_se_relit_seul():
    """« Interruptible et reprenable » (§ 3) : chaque lot doit se suffire."""
    with _moteur() as moteur:
        espace = search_space_for_machine(MACHINE, moteur)
        lot = genere_lot(MACHINE, espace, moteur, patchs=2, grille=GRILLE_ESSAI,
                         graine=13, sample_rate=SR, augmentations=["bruit"])
    with tempfile.TemporaryDirectory(prefix="vsm-corpus-") as dossier:
        chemin = Path(dossier) / "lot-000.npz"
        lot.enregistre(chemin)
        relu = LotDeCorpus.relit(chemin)
    assert_equal(relu.machine, MACHINE, "machine")
    assert_true(np.array_equal(relu.X, lot.X), "descripteurs")
    assert_true(np.array_equal(relu.Y, lot.Y), "patchs")
    assert_equal(relu.augmentations, lot.augmentations, "augmentations")


@test
def corpus_la_grille_joue_ce_qu_elle_annonce():
    """Le § 3 exige au moins 3 hauteurs × 2 durées × 2 vélocités."""
    grille = GrilleDeNotes()
    assert_true(len(grille.hauteurs) >= 3, "au moins trois hauteurs")
    assert_true(len(grille.durees) >= 2, "au moins deux durées")
    assert_true(len(grille.velocites) >= 2, "au moins deux vélocités")
    assert_equal(len(grille.points()), grille.rendus_par_patch(), "compte annoncé")


@test
def corpus_les_machines_de_recherche_ont_toutes_un_espace():
    with _moteur() as moteur:
        machines = machines_de_recherche(moteur)
        assert_true(len(machines) > 10, f"{len(machines)} machines seulement")
        for machine in machines[:4]:
            assert_true(len(search_space_for_machine(machine, moteur)) > 0,
                        f"{machine} déclare un espace")


@test
def corpus_sec_et_augmente_contiennent_les_MEMES_patchs():
    """La condition de l'A/B du § 7. Si le tirage des augmentations consommait
    le même flux aléatoire que celui des patchs, un corpus sec et un corpus
    augmenté « à la même graine » ne contiendraient pas les mêmes sons — et les
    comparer mesurerait deux choses à la fois, sans pouvoir les démêler."""
    with _moteur() as moteur:
        espace = search_space_for_machine(MACHINE, moteur)
        sec = genere_lot(MACHINE, espace, moteur, patchs=4, grille=GRILLE_ESSAI,
                         graine=17, sample_rate=SR, augmentations=[])
        augmente = genere_lot(MACHINE, espace, moteur, patchs=4, grille=GRILLE_ESSAI,
                              graine=17, sample_rate=SR,
                              augmentations=[a.nom for a in AUGMENTATIONS])
    assert_equal(sec.Y.shape, augmente.Y.shape, "même nombre d'exemples")
    assert_true(np.array_equal(sec.Y, augmente.Y), "MÊMES patchs des deux côtés")
    assert_true(np.array_equal(sec.conditions, augmente.conditions), "mêmes notes jouées")
    assert_true(np.array_equal(sec.patchs, augmente.patchs), "mêmes numéros de patch")
    assert_true(all(nom == "" for nom in sec.augmentations), "le corpus sec est sec")
    assert_true(any(nom for nom in augmente.augmentations), "l'autre est bien augmenté")
    assert_true(not np.array_equal(sec.X, augmente.X),
                "et les descripteurs, eux, DOIVENT différer")
