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


@test
def le_desaccord_de_hauteur_est_ecrit_dans_la_verite():
    """D277 : le corpus DIT de combien ses parties sonnent à côté de leurs notes.

    Le patch est tiré au hasard dans l'espace déclaré par la machine, où
    plusieurs exposent un désaccord d'oscillateur en demi-tons (`vsm.pcmhybrid`
    ±24, `vsm.obx` ±12). Une partie ainsi tirée SONNE ailleurs que ce que sa
    liste de notes annonce, et toute mesure de hauteur qui compare l'une à
    l'autre compte ces notes fausses à tort — 9,9 % des notes de `s1-sec`, et
    100 % de celles de `morceau-0001-g1`.

    LE POINT DÉLICAT EST L'UNITÉ, et c'est lui que ce test garde : « st » compte
    tel quel, « cents » se divise par cent, et tout le reste ne déplace RIEN.
    Avoir pris les réglages normalisés de 0 à 1 pour des demi-tons a fait publier
    « 12,7 % des notes » là où il faut lire 9,9 %.
    """
    import types

    from analyzer.vsm_morceaux import Generateur
    from analyzer.vsm_patch_optimizer import SearchParameter

    # LE TYPE QUI CIRCULE VRAIMENT. Ces tests ont d'abord été écrits avec
    # `SearchDimension`, qui porte aussi un `unit` — ils passaient, et les six
    # tests de génération du corpus tombaient : `self.espace()` rend des
    # `SearchParameter`, et ce type-là ne portait PAS l'unité. Une garde qui
    # n'emploie pas l'objet du chemin réel garde autre chose.
    espace = [
        SearchParameter(semantic_id="oscillator.2.detune", low=-12.0, high=12.0, unit="st"),
        SearchParameter(semantic_id="oscillator.autre.detune", low=0.0, high=50.0, unit="cents"),
        SearchParameter(semantic_id="voice.unisonDetune", low=0.0, high=1.0, unit=""),
        SearchParameter(semantic_id="filter.1.cutoff", low=20.0, high=20000.0, unit="Hz"),
    ]
    faux = types.SimpleNamespace(_espaces={"vsm.essai": espace})
    faux.espace = lambda machine: faux._espaces[machine]

    patch = {
        "oscillator.2.detune": -8.25,        # demi-tons : compte tel quel
        "oscillator.autre.detune": 10.0,     # cents : 0,10 demi-ton, sous le seuil
        "voice.unisonDetune": 0.84,          # normalisé : ne déplace RIEN
        "filter.1.cutoff": 8000.0,           # pas une hauteur du tout
    }
    trouves = Generateur.desaccords_de_hauteur(faux, "vsm.essai", patch)

    assert_equal(sorted(trouves), ["oscillator.2.detune"],
                 "ni le réglage normalisé, ni la fréquence, ni dix cents ne sont retenus")
    assert_near(trouves["oscillator.2.detune"], -8.25, 1e-9, "la valeur en demi-tons est gardée telle quelle")

    # LES CENTS COMPTENT DÈS QU'ILS DÉPASSENT LE SEUIL, et le cas réel n'est pas
    # loin : `vsm.psg` tire jusqu'à 50 cents, et le corpus en a produit 37,15 —
    # soit 0,37 demi-ton, au-dessus du quart de ton. Ce n'est pas assez pour
    # changer la note la plus proche (0,37 s'arrondit à 0), mais c'est assez pour
    # s'entendre, et le corpus doit le DIRE plutôt que de le taire.
    patch["oscillator.autre.detune"] = 37.15
    trouves = Generateur.desaccords_de_hauteur(faux, "vsm.essai", patch)
    assert_near(trouves["oscillator.autre.detune"], 0.3715, 1e-9,
                "37,15 cents valent 0,3715 demi-ton, et sont retenus")


@test
def borner_la_hauteur_ramene_les_desaccords_sous_la_borne():
    """B5 / § 7 bis : un patch tiré ne doit plus sonner huit demi-tons à côté.

    ON BORNE LE VECTEUR, PAS LE PATCH : `verite.json` garde le vecteur tiré et le
    patch s'en déduit. Écrêter le patch après coup les ferait mentir l'un sur
    l'autre, et un corpus reproductible ne l'est plus si son vecteur ne rend pas
    son patch. Le test vérifie donc les DEUX : la borne tenue, et l'accord entre
    le vecteur et le patch qu'il engendre.
    """
    import types

    import numpy as np

    from analyzer.vsm_morceaux import Generateur
    from analyzer.vsm_patch_optimizer import SearchParameter, _vector_to_parameters

    espace = [
        SearchParameter(semantic_id="oscillator.2.detune", low=-12.0, high=12.0, unit="st"),
        SearchParameter(semantic_id="sample.1.tune", low=-24.0, high=24.0, unit="st"),
        SearchParameter(semantic_id="osc.cents", low=0.0, high=50.0, unit="cents"),
        SearchParameter(semantic_id="filter.1.cutoff", low=20.0, high=20000.0,
                        logarithmic=True, unit="Hz"),
    ]
    faux = types.SimpleNamespace(_espaces={"vsm.essai": espace}, borne_hauteur=2.0)
    faux.espace = lambda machine: faux._espaces[machine]

    # Les deux extrêmes et le milieu : une borne qui ne tiendrait qu'au centre ne
    # servirait à rien, puisque c'est aux bords que le tirage désaccorde.
    for brut in (0.0, 0.5, 1.0):
        vecteur = Generateur.borner_les_hauteurs(faux, "vsm.essai", np.full(len(espace), brut))
        patch = _vector_to_parameters(espace, vecteur)
        assert_true(abs(patch["oscillator.2.detune"]) <= 2.0 + 1e-9,
                    f"detune borné à 2 demi-tons (obtenu {patch['oscillator.2.detune']})")
        assert_true(abs(patch["sample.1.tune"]) <= 2.0 + 1e-9,
                    f"tune borné à 2 demi-tons (obtenu {patch['sample.1.tune']})")
        assert_true(abs(patch["osc.cents"]) <= 200.0 + 1e-9,
                    f"cents bornés à 2 demi-tons = 200 cents (obtenu {patch['osc.cents']})")

    # LE TÉMOIN : sans borne, rien ne bouge — le corpus d'avant, au bit près.
    faux.borne_hauteur = 0.0
    vecteur = np.full(len(espace), 1.0)
    assert_true(np.array_equal(Generateur.borner_les_hauteurs(faux, "vsm.essai", vecteur), vecteur),
                "borne nulle : le vecteur tiré n'est pas touché")

    # ET LA DIMENSION QUI N'EST PAS UNE HAUTEUR n'est jamais remappée : borner le
    # filtre changerait le timbre du corpus sans que personne ne l'ait demandé.
    faux.borne_hauteur = 2.0
    vecteur = Generateur.borner_les_hauteurs(faux, "vsm.essai", np.full(len(espace), 1.0))
    assert_near(float(vecteur[3]), 1.0, 1e-12, "la fréquence de coupure n'est pas bornée")
