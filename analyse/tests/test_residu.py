"""La boucle résiduelle tient ses promesses (docs/CDC-separation-par-synthese.md § 4),
sans la chaîne : l'arithmétique et la boucle sur des collaborateurs factices.

  - l'alignement retrouve un décalage et un gain construits ;
  - un mélange qui EST le rendu d'une piste, moins ce rendu, donne un résidu
    NUL au bit près (avec le vrai moteur) ;
  - le garde-fou refuse un rendu décorrélé, et le refus porte sa corrélation ;
  - les notes déjà portées se reconnaissent aux tolérances du banc ;
  - chacun des cinq motifs d'arrêt se déclenche et se nomme ;
  - le résidu s'écrit sur disque, mêmes échantillons → mêmes octets.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, run, test  # noqa: E402

from analyzer import vsm_residu as vr  # noqa: E402
from analyzer.vsm_offline_render import render_track_offline  # noqa: E402
from analyzer.vsm_project_export import ExportNote, ExportTrack  # noqa: E402

SR = 44100


def _sinus(frequence: float, secondes: float, graine: int = 0) -> np.ndarray:
    t = np.arange(int(secondes * SR)) / SR
    return (0.5 * np.sin(2 * np.pi * frequence * t)).astype(np.float32)


def _bruit(secondes: float, graine: int) -> np.ndarray:
    return np.random.default_rng(graine).normal(0.0, 0.1, int(secondes * SR)).astype(np.float32)


# ---------------------------------------------------------------------------
# Arithmétique
# ---------------------------------------------------------------------------

@test
def l_alignement_retrouve_un_decalage_et_un_gain_construits():
    """Un rendu décalé de +37 échantillons et atténué de moitié dans du bruit
    est retrouvé à d = 37, g = 0,50 ± 0,02."""
    rendu = _bruit(1.0, 1) * 3.0
    melange = 0.5 * vr.decaler(rendu, 37, rendu.size) + _bruit(1.0, 2)
    decalage, gain = vr.aligner(melange, rendu)
    assert_equal(decalage, 37, "le décalage construit est retrouvé")
    assert_near(gain, 0.5, 0.02, "le gain construit est retrouvé")
    # Et dans l'autre sens : un rendu en avance.
    melange = 0.8 * vr.decaler(rendu, -12, rendu.size) + _bruit(1.0, 3)
    decalage, gain = vr.aligner(melange, rendu)
    assert_equal(decalage, -12, "un décalage négatif aussi")
    assert_near(gain, 0.8, 0.02, "et son gain")


@test
def la_soustraction_exacte_donne_un_residu_nul_au_bit_pres():
    """Un mélange qui EST le rendu d'une piste par le vrai moteur, moins ce
    rendu réaligné : décalage 0, gain 1, résidu nul — la soustraction ne
    fabrique rien."""
    piste = ExportTrack(name="bass", machine="vsm.tb303",
                        notes=[ExportNote(36, 100, 0.1, 0.3), ExportNote(43, 90, 0.5, 0.3),
                               ExportNote(38, 110, 0.9, 0.4)])
    with tempfile.TemporaryDirectory() as d:
        rendu = render_track_offline(piste, Path(d) / "solo", SR, duration=1.5, title="test-residu")
    assert_true(rendu is not None and rendu.size > 0 and np.any(rendu), "le moteur a rendu la piste")
    melange = rendu.astype(np.float32).copy()
    decalage, gain = vr.aligner(melange, rendu)
    assert_equal(decalage, 0, "décalage nul")
    assert_equal(gain, 1.0, "gain exactement 1")
    residu = vr.soustraire(melange, rendu, decalage, gain)
    assert_equal(residu.dtype, np.float32, "le résidu est en float32, comme tout ce que la chaîne lit")
    assert_true(not np.any(residu), "résidu NUL au bit près")
    assert_near(vr.correlation(melange, rendu), 1.0, 1e-9, "corrélation 1 avec lui-même")


@test
def le_garde_fou_refuse_un_rendu_decorrele_et_dit_sa_correlation():
    stem = _sinus(110.0, 1.0)
    residu = stem + _sinus(440.0, 1.0) * 0.3
    unite = vr.Unite(nom="bass", membres=[], stem=stem, part=40.0, distance=0.2)
    fiche = vr._candidate(unite, residu, _bruit(1.0, 7), vr.Options(iterations=1, correlation=0.5), SR)
    assert_true(not fiche["retenue"], "un bruit n'est pas la partie")
    assert_true("sous le seuil" in fiche["motif"] and "corrélation" in fiche["motif"], "le refus dit sa corrélation : " + fiche["motif"])
    assert_true(abs(fiche["correlationStem"]) < 0.1, "et elle est basse")
    # Le même rendu que le stem passe, avec le score attendu.
    fiche = vr._candidate(unite, residu, stem.copy(), vr.Options(iterations=1, correlation=0.5), SR)
    assert_true(fiche["retenue"] and fiche["motif"] == "sûre", "le stem lui-même est sûr")
    assert_near(fiche["correlationStem"], 1.0, 1e-6, "corrélation 1")
    assert_near(fiche["score"], 200.0, 1e-9, "score = part / distance = 40 / 0,2")
    assert_true(fiche["correlationReste"] < 0.05, "et il ne ressemble pas au reste (une autre fréquence)")
    # Sans distance : pas de score, pas de soustraction, et c'est dit.
    sans = vr.Unite(nom="x", membres=[], stem=stem, part=40.0, distance=None)
    fiche = vr._candidate(sans, residu, stem.copy(), vr.Options(iterations=1), SR)
    assert_true(not fiche["retenue"] and "sans distance" in fiche["motif"], fiche["motif"])
    # Un rendu en OPPOSITION à tous les décalages : gain négatif, refus nommé.
    # (Une sinusoïde inversée n'en est pas une : c'est la même, décalée d'une
    # demi-période, et l'alignement la retrouve — à raison.)
    constante = vr.Unite(nom="dc", membres=[], stem=np.ones(SR // 2, dtype=np.float32), part=40.0, distance=0.2)
    fiche = vr._candidate(constante, np.ones(SR // 2, dtype=np.float32), -np.ones(SR // 2, dtype=np.float32),
                          vr.Options(iterations=1), SR)
    assert_true(not fiche["retenue"] and "gain non positif" in fiche["motif"], fiche["motif"])


@test
def les_notes_deja_portees_se_reconnaissent_aux_tolerances_du_banc():
    deja = [(60, 1.000), (64, 2.000)]
    paires = [(60, 1.030), (61, 1.040), (62, 1.000), (64, 2.060), (64, 5.0)]
    nouveaux = vr.indices_nouveaux(paires, deja)
    assert_equal(nouveaux, [2, 3, 4], "±1 demi-ton et ±50 ms : la première et la deuxième sont déjà portées")
    assert_equal(vr.indices_nouveaux(paires, []), [0, 1, 2, 3, 4], "rien de porté : tout est nouveau")
    frappes = vr.indices_frappes_nouvelles([0.500, 0.520, 0.545, 1.0], [0.51])
    assert_equal(frappes, [2, 3], "±30 ms, quelle que soit la pièce")


@test
def le_residu_s_ecrit_en_float32_minimal_memes_octets():
    audio = _bruit(0.2, 11)
    with tempfile.TemporaryDirectory() as d:
        a, b = Path(d) / "a.wav", Path(d) / "b.wav"
        vr.ecrire_residu(a, audio, SR)
        vr.ecrire_residu(b, audio.copy(), SR)
        assert_equal(a.read_bytes(), b.read_bytes(), "mêmes échantillons → mêmes octets")
        import soundfile as sf
        relu, taux = sf.read(str(a), dtype="float32")
        assert_equal(taux, SR, "à la fréquence de la chaîne")
        assert_true(np.array_equal(relu, audio), "relu tel quel par soundfile")
        assert_equal(a.stat().st_size, 44 + audio.size * 4, "RIFF, fmt, data — et rien d'autre")


# ---------------------------------------------------------------------------
# La boucle, sur des collaborateurs factices : chaque motif d'arrêt
# ---------------------------------------------------------------------------

class _Piste:
    def __init__(self, nom):
        self.name = nom


def _unite(nom, stem, part, distance, iteration=0):
    return vr.Unite(nom=nom, membres=[_Piste(nom)], stem=stem, part=part, distance=distance,
                    iteration=iteration)


def _boucle(unites, melange, options, rendre, passe=None, distances=(0.30, 0.25, 0.20, 0.15),
            stems=None, journal=None):
    lignes = []
    jauge = iter(distances)

    def separer(residu_wav, dossier):
        assert Path(residu_wav).exists(), "le résidu est écrit avant la séparation"
        dossier.mkdir(parents=True, exist_ok=True)
        return dict(stems if stems is not None else {"other": dossier / "other.wav"})

    def reconstruire(stems_, k, deja):
        if passe is None:
            return vr.Passe()
        return passe(stems_, k, deja)

    collab = vr.Collaborateurs(rendre=rendre, separer=separer, reconstruire=reconstruire,
                               distance_projet=lambda: next(jauge),
                               deja_portees=lambda: vr.DejaPortees(),
                               journal=(journal or lignes.append))
    with tempfile.TemporaryDirectory() as d:
        rapport = vr.boucle_residuelle(melange, unites, options, collab, Path(d), SR)
    return rapport, lignes


@test
def arret_aucune_piste_sure():
    stem = _sinus(110.0, 0.5)
    melange = stem + _sinus(220.0, 0.5)
    unites = [_unite("bass", stem, 50.0, 0.2)]
    rapport, lignes = _boucle(unites, melange, vr.Options(iterations=2, correlation=0.5),
                              rendre=lambda u, n: _bruit(0.5, 3))
    assert_equal(rapport["arret"]["motif"], "aucune-piste-sure", "motif")
    assert_equal(rapport["arret"]["iteration"], 1, "dès la première itération")
    assert_true("meilleure corrélation" in rapport["arret"]["detail"], rapport["arret"]["detail"])
    assert_equal(len(rapport["iterations"]), 1, "l'itération est publiée avec ses candidates")
    assert_equal(len(rapport["iterations"][0]["candidats"]), 1, "une candidate, écartée")
    assert_true("soustraction" not in rapport["iterations"][0], "et rien de soustrait")
    assert_true(any("ARRÊT" in l and "aucune-piste-sure" in l for l in lignes), "l'arrêt est imprimé")


@test
def arret_residu_sous_le_seuil():
    stem = _sinus(110.0, 0.5)
    melange = stem.copy()
    unites = [_unite("bass", stem, 100.0, 0.2)]
    rapport, lignes = _boucle(unites, melange, vr.Options(iterations=3, energie=5.0),
                              rendre=lambda u, n: stem.copy())
    assert_equal(rapport["arret"]["motif"], "residu-sous-le-seuil", "motif")
    it = rapport["iterations"][0]
    assert_equal(it["soustraction"]["unite"], "bass", "la basse a été soustraite")
    assert_near(it["energie"]["partApres"], 0.0, 1e-6, "et il ne reste rien")
    assert_true(Path(it["residu"]).name == "residu.wav", "le résidu a été écrit")
    assert_true("stems" not in it, "pas de réséparation : rien à chercher")
    assert_true(any("SOUSTRAIT" in l for l in lignes), "la soustraction est imprimée")


@test
def arret_rien_de_discernable():
    stem = _sinus(110.0, 0.5)
    melange = stem + _sinus(330.0, 0.5)
    unites = [_unite("bass", stem, 50.0, 0.2)]
    rapport, _ = _boucle(unites, melange, vr.Options(iterations=3),
                         rendre=lambda u, n: stem.copy(), passe=None)
    assert_equal(rapport["arret"]["motif"], "rien-de-discernable", "motif")
    assert_equal(rapport["iterations"][0]["pistesAjoutees"], [], "aucune piste")
    # Une séparation qui ne rend rien est le même motif, avec son détail.
    rapport, _ = _boucle([_unite("bass", stem, 50.0, 0.2)], melange, vr.Options(iterations=3),
                         rendre=lambda u, n: stem.copy(), stems={})
    assert_equal(rapport["arret"]["motif"], "rien-de-discernable", "motif")
    assert_true("aucun stem" in rapport["arret"]["detail"], rapport["arret"]["detail"])


def _passe_qui_ajoute(nom_stem="other"):
    def passe(stems, k, deja):
        stem = _sinus(330.0, 0.5)
        return vr.Passe(unites=[_unite(f"{nom_stem} · r{k}", stem, 30.0, 0.3, iteration=k)],
                        pistes_ajoutees=[{"piste": f"{nom_stem} · r{k}", "machine": "vsm.x", "notes": 9}],
                        partage=[{"stem": nom_stem, "partEnergie": 30.0}])
    return passe


@test
def arret_distance_sans_gain_garde_les_pistes():
    stem = _sinus(110.0, 0.5)
    melange = stem + _sinus(330.0, 0.5)
    unites = [_unite("bass", stem, 50.0, 0.2)]
    rapport, lignes = _boucle(unites, melange, vr.Options(iterations=3), rendre=lambda u, n: stem.copy(),
                              passe=_passe_qui_ajoute(), distances=(0.30, 0.31))
    assert_equal(rapport["arret"]["motif"], "distance-sans-gain", "motif")
    assert_true("GARDÉES" in rapport["arret"]["detail"], "les pistes restent : " + rapport["arret"]["detail"])
    it = rapport["iterations"][0]
    assert_equal(it["distanceProjet"], {"avant": 0.30, "apres": 0.31}, "les deux chiffres")
    assert_equal([p["piste"] for p in it["pistesAjoutees"]], ["other · r1"], "la piste ajoutée est publiée")
    assert_equal(len(unites), 2, "et l'unité issue du résidu est entrée dans la liste")


@test
def arret_iterations_atteintes_et_l_unite_du_residu_concourt_ensuite():
    stem = _sinus(110.0, 0.5)
    # Une troisième composante que personne ne rend : après deux soustractions
    # exactes, le résidu la porte encore (11 % du mélange, au-dessus des 5 %).
    melange = stem + _sinus(330.0, 0.5) + 0.5 * _sinus(550.0, 0.5)
    unites = [_unite("bass", stem, 50.0, 0.2)]
    rendus = []

    def rendre(u, n):
        rendus.append(u.nom)
        return u.stem.copy()

    rapport, _ = _boucle(unites, melange, vr.Options(iterations=2), rendre=rendre,
                         passe=_passe_qui_ajoute(), distances=(0.30, 0.25, 0.20))
    assert_equal(rapport["arret"]["motif"], "iterations-atteintes", "motif")
    assert_equal([it["soustraction"]["unite"] for it in rapport["iterations"]], ["bass", "other · r1"],
                 "la seconde itération soustrait l'unité que la première a fait apparaître")
    assert_equal(rendus, ["bass", "other · r1"], "une unité soustraite n'est plus rendue")
    assert_true(rapport["iterations"][1]["energie"]["partApres"] < rapport["iterations"][0]["energie"]["partApres"],
                "le résidu maigrit à chaque itération")


@test
def la_plus_sure_est_celle_de_plus_grand_score_et_les_egalites_se_departagent_par_le_nom():
    a, b = _sinus(110.0, 0.5), _sinus(220.0, 0.5)
    melange = a + b
    unites = [_unite("zeta", a, 40.0, 0.2), _unite("alpha", b, 40.0, 0.2), _unite("beta", b, 60.0, 0.2)]
    rapport, _ = _boucle(unites, melange, vr.Options(iterations=1), rendre=lambda u, n: u.stem.copy(),
                         passe=_passe_qui_ajoute(), distances=(0.3, 0.2))
    assert_equal(rapport["iterations"][0]["soustraction"]["unite"], "beta", "60/0,2 bat 40/0,2")
    assert_equal(len(rapport["iterations"][0]["candidats"]), 3, "les trois sont publiées")
    unites = [_unite("zeta", a, 40.0, 0.2), _unite("alpha", b, 40.0, 0.2)]
    rapport, _ = _boucle(unites, melange, vr.Options(iterations=1), rendre=lambda u, n: u.stem.copy(),
                         passe=_passe_qui_ajoute(), distances=(0.3, 0.2))
    assert_equal(rapport["iterations"][0]["soustraction"]["unite"], "zeta", "à score égal, le nom le plus grand — stable")


if __name__ == "__main__":
    raise SystemExit(run())
