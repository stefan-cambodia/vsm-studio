"""H37 : le recensement des sources (`docs/CDC-recensement-des-sources.md`).

Deux familles de tests, et la seconde passe AVANT toute mesure :

1. le module (`analyzer/vsm_recensement.py`) sur des signaux fabriqués — deux
   timbres alternés, un silence, un stem vide, et ses réglages, qui doivent
   valoir ce que le § 0.4 a écrit avant la mesure ;
2. LA GARDE DES MÉTRIQUES du banc (`banc_recensement.py`) sur des étiquettes
   construites à la main : un banc se vérifie avant sa cible (leçon de D266,
   où six bancs successifs ont accusé des machines justes).
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

from framework import assert_equal, assert_near, assert_true, test  # noqa: E402

import banc_recensement as B  # noqa: E402
from analyzer import vsm_recensement as R  # noqa: E402
from analyzer.vsm_reconstruct import StemNote  # noqa: E402

SR = 22050


def _deux_timbres(duree: float = 30.0) -> np.ndarray:
    """Une sinusoïde grave et un bruit aigu qui alternent toutes les 0,5 s."""
    rng = np.random.default_rng(7)
    t = np.arange(int(duree * SR)) / SR
    sinus = 0.3 * np.sin(2 * np.pi * 220.0 * t)
    bruit = 0.3 * np.diff(rng.standard_normal(t.size + 1))
    audio = np.where((t // 0.5) % 2 == 0, sinus, bruit)
    # une attaque nette au début de chaque demi-seconde
    enveloppe = np.exp(-((t % 0.5) / 0.35))
    return (audio * enveloppe).astype(np.float32)


def _grappes(audio: np.ndarray) -> R.Regroupement:
    seg = R.segmenter(audio, SR)
    d = R.descripteurs_complets(audio, SR, seg)
    moyenne = d.mean(axis=0) if d.size else np.zeros(43)
    echelle = d.std(axis=0) if d.size else np.ones(43)
    return R.grouper(R.centrer(d, moyenne, echelle), seg)


@test
def recensement_les_reglages_valent_ce_que_le_cdc_a_ecrit_avant_la_mesure():
    """§ 0.4 du CDC, commit a110fd8 : un seuil ne bouge pas sans ce test."""
    assert_equal(R.SEGMENT_MIN_S, 0.10)
    assert_equal(R.SEGMENT_MAX_S, 1.00)
    assert_equal(R.SILENCE_DBFS, -50.0)
    assert_equal(R.HDBSCAN_PART_MIN, 0.02)
    assert_equal(R.HDBSCAN_TAILLE_MIN, 5)
    assert_equal(R.HDBSCAN_MIN_SAMPLES, 3)
    assert_equal(R.DUREE_MIN_GRAPPE_S, 4.0)
    assert_equal(R.DIMENSIONS_DE_TIMBRE, 40)
    assert_equal(R.PART_PERCUSSIVE, 0.6)
    assert_equal(R.CENTILE_VOIX, 90)
    assert_equal(R.TRAME_VOIX_S, 0.05)
    assert_equal(R.PART_DOMINANTE, 0.60)
    assert_equal((R.NAPPE_DUREE_S, R.NAPPE_POLYPHONIE), (1.0, 2.0))
    assert_equal((R.PIANO_POLYPHONIE, R.PIANO_AMBITUS), (2.0, 24))
    assert_equal((R.BASSE_HAUTEUR, R.MONO_POLYPHONIE, R.LEAD_HAUTEUR), (48, 1.5, 60))
    assert_equal(R.taille_minimale(100), 5)
    assert_equal(R.taille_minimale(1000), 20)


@test
def recensement_decouper_borne_les_segments_et_compte_les_trop_courts():
    segments, trop_courts = R.decouper([0.0, 0.05, 0.5, 3.0], 3.5)
    bornes = [(round(s.debut, 3), round(s.fin, 3)) for s in segments]
    assert_equal(bornes, [(0.05, 0.5), (0.5, 1.5), (1.5, 2.5), (2.5, 3.0), (3.0, 3.5)])
    assert_equal(trop_courts, 1, "le segment de 0,05 s est COMPTÉ, pas fusionné")


def _part_sinus(s: R.Segment) -> float:
    t = np.arange(int(s.debut * SR), int(s.fin * SR)) / SR
    return float(np.mean((t // 0.5) % 2 == 0))


@test
def recensement_deux_timbres_les_segments_purs_tombent_chacun_dans_une_grappe():
    """Le REGROUPEMENT sépare les deux timbres sans une erreur.

    Mesuré le 24/09 en écrivant ce test : 19 segments purs de sinus → une
    grappe, 19 de bruit → une autre, aucun croisement.
    """
    audio = _deux_timbres()
    seg = R.segmenter(audio, SR)
    reg = _grappes(audio)
    sinus = {int(e) for s, e in zip(seg.segments, reg.etiquettes, strict=True) if _part_sinus(s) > 0.8}
    bruit = {int(e) for s, e in zip(seg.segments, reg.etiquettes, strict=True) if _part_sinus(s) < 0.2}
    assert_equal(len(sinus), 1, f"sinus : {sinus}")
    assert_equal(len(bruit), 1, f"bruit : {bruit}")
    assert_true(sinus.isdisjoint(bruit), "les deux timbres ne partagent aucune grappe")


@test
def recensement_une_attaque_manquee_fabrique_une_grappe_de_melange():
    """LA LIMITE CONNUE, écrite plutôt que réglée (24/09, avant toute mesure).

    Sur ce signal, l'attaque « bruit → sinus » n'est pas toujours détectée :
    11 segments d'1 s contiennent les deux timbres, et ils forment leurs PROPRES
    grappes (4 au total au lieu de 2). Ce n'est pas le regroupement qui se
    trompe — les segments purs sont parfaitement séparés (test précédent) —,
    c'est la segmentation qui lui donne des mélanges. Le niveau L1 du banc
    mesure exactement ce coût sur les vraies parties ; ce test garde le constat
    pour qu'on ne le « corrige » pas en douce par un seuil.
    """
    audio = _deux_timbres()
    seg = R.segmenter(audio, SR)
    mixtes = sum(1 for s in seg.segments if 0.2 <= _part_sinus(s) <= 0.8)
    assert_true(mixtes > 0, "des segments contiennent les deux timbres")
    assert_equal(len(_grappes(audio).grappes), 4)


@test
def recensement_le_silence_ne_donne_rien_et_le_dit():
    seg = R.segmenter(np.zeros(SR * 10, dtype=np.float32) + 1e-5, SR)
    assert_equal(len(seg.segments), 0)
    assert_true(seg.silencieux > 0, "les segments silencieux sont COMPTÉS")


@test
def recensement_un_stem_vide_rend_un_recensement_vide_sans_exception():
    seg = R.segmenter(np.zeros(10, dtype=np.float32), SR)
    assert_equal(len(seg.segments), 0)
    reg = R.grouper(np.zeros((0, 40)), seg)
    assert_equal(len(reg.grappes), 0)


@test
def recensement_deterministe_deux_passes_memes_etiquettes():
    audio = _deux_timbres()
    assert_equal(_grappes(audio).etiquettes.tolist(), _grappes(audio).etiquettes.tolist())


@test
def recensement_roles_par_regles():
    nappe = [StemNote(60 + i, 80, 0.0 + 4 * k, 3.0) for k in range(3) for i in (0, 4, 7)]
    assert_equal(R.role_par_regles("other", nappe), "nappe")
    lead = [StemNote(72 + (k % 5), 80, 0.25 * k, 0.2) for k in range(20)]
    assert_equal(R.role_par_regles("other", lead), "lead")
    grave = [StemNote(36 + (k % 3), 80, 0.25 * k, 0.2) for k in range(20)]
    assert_equal(R.role_par_regles("other", grave), "basse")
    assert_equal(R.role_par_regles("bass", lead), "basse", "le stem bass décide")
    assert_equal(R.role_par_regles("vocals", []), "voix")
    assert_equal(R.role_par_regles("other", []), "autre")


@test
def recensement_voix_par_registre_compte_un_accord_tenu():
    accord = [StemNote(n, 80, 0.0, 2.0) for n in (60, 64, 67)]
    voix, _ = R.voix_par_registre(accord)
    assert_equal(voix, 3)
    assert_equal(R.voix_par_registre([])[0], 0)


# --- la garde des métriques, AVANT la cible --------------------------------

@test
def recensement_garde_ari_nmi_sur_etiquettes_connues():
    vrais = [0] * 20 + [1] * 20 + [2] * 20
    ari, nmi, mixte = B.ari_nmi(vrais, vrais)
    assert_near(ari, 1.0, 1e-9)
    assert_near(nmi, 1.0, 1e-9)
    assert_equal(mixte, 0.0)
    permutes = [{0: 7, 1: 3, 2: -1}[v] for v in vrais]
    ari, nmi, _ = B.ari_nmi(vrais, permutes)
    assert_near(ari, 1.0, 1e-9, "une permutation des noms ne change rien")
    rng = np.random.default_rng(0)
    ari, _, _ = B.ari_nmi(vrais * 10, rng.integers(0, 3, 600).tolist())
    assert_true(abs(ari) < 0.05, f"le hasard donne un ARI proche de 0 : {ari}")
    ari, nmi, _ = B.ari_nmi([3] * 10, [0] * 10)
    assert_equal((ari, nmi), (None, None), "une seule classe vraie : non défini, jamais 1,0")
    ari, _, mixte = B.ari_nmi([0, 0, B.MIXTE, 1, 1], [5, 5, 9, 6, 6])
    assert_near(ari, 1.0, 1e-9, "les segments mixtes sont exclus")
    assert_near(mixte, 0.2, 1e-9)


@test
def recensement_garde_etiquette_dominante():
    assert_equal(B.etiquette_dominante(np.array([0.7, 0.3]), 0.6), 0)
    assert_equal(B.etiquette_dominante(np.array([0.5, 0.5]), 0.6), B.MIXTE)
    assert_equal(B.etiquette_dominante(np.array([0.0, 0.0]), 0.6), B.MIXTE)


@test
def recensement_garde_appariement_et_statuts():
    # trois parties ; grappe 0 = partie 0, grappe 1 = partie 1, grappe 2 = encore
    # partie 0 (dédoublement) ; la partie 2 n'a aucune grappe (fondue).
    labels = [0] * 10 + [1] * 10 + [0] * 4 + [2] * 3
    grappes = [list(range(0, 10)), list(range(10, 20)), list(range(20, 24))]
    app, m = B.apparier(grappes, labels, 3)
    assert_equal(app, {0: 0, 1: 1})
    assert_equal(B.statuts(app, m, 3), ["dedoublee", "ok", "fondue"])
    app, m = B.apparier([], labels, 3)
    assert_equal(app, {})


@test
def recensement_garde_roles_et_comptes():
    prf = B.roles_prf([("lead", "lead"), ("nappe", "lead")], ["nappe"], ["piano"],
                      ["lead", "nappe", "piano"])
    assert_equal((prf["lead"]["precision"], prf["lead"]["rappel"]), (1.0, 0.5))
    assert_equal((prf["nappe"]["precision"], prf["nappe"]["rappel"]), (0.0, None))
    assert_equal(prf["piano"]["rappel"], 0.0)
    assert_near(B.f1_macro(prf, ["lead", "nappe", "piano"]), (2 / 3 + 0.0) / 2, 1e-9,
                "les rôles sans partie vraie n'entrent pas dans la moyenne")
    c = B.erreurs_de_compte([6, 15, 5], [7, 7, 5])
    assert_equal((c["mae"], c["exacts"], c["aUnPres"]), (3.0, 1, 2))
