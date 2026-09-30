"""H48 — le cache des mesures de PROJET : une course morte reprend ce qu'elle avait payé.

docs/CDC-reload-indifferenciable.md § 10. Le verdict du mélange et le réglage au
mélange ne laissaient rien sur disque ; chaque évaluation y est désormais rangée sous
une clé qui hache le DOSSIER que le moteur lit. Ces tests ne lancent pas le moteur :
le rendu est remplacé par un faux qui COMPTE ses appels, et le cache vit dans un
dossier de brouillon — jamais dans `cache/mesures/`.

  - la clé change avec tout ce que le moteur lit (volume, patch, note, tempo,
    diapason, échantillon, fréquence, moteur) et NE change PAS avec ce qu'il écrit
    (`rendu.wav`) ;
  - une mesure payée se relit AU BIT près, sans rendu ; un état différent se repaie ;
  - un rendu en échec n'est jamais rangé ;
  - sans l'option, rien n'est lu ni écrit (le chemin d'avant) ;
  - le verdict du mélange et le réglage au mélange, rejoués, ne rendent plus rien et
    décident pareil.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path
from typing import Dict, List

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer import diapason  # noqa: E402
from analyzer import vsm_mix_refine as mr  # noqa: E402
from analyzer import vsm_mix_verdict as mv  # noqa: E402
from analyzer import vsm_render_cache as rc  # noqa: E402
from analyzer.vsm_engine import SearchDimension  # noqa: E402
from analyzer.vsm_project_export import ExportNote, ExportTrack, write_project_bundle  # noqa: E402

TAUX = 44100
DUREE = 22050


def _pistes() -> List[ExportTrack]:
    return [
        ExportTrack(name="bass", machine="vsm.tb303", parameters={"filter.1.cutoff": 700.0},
                    notes=[ExportNote(note=48, velocity=100, start=0.0, duration=0.4)]),
        ExportTrack(name="other", machine="vsm.string", parameters={"env.attack": 0.2},
                    notes=[ExportNote(note=64, velocity=90, start=0.1, duration=0.3)]),
    ]


class _Brouillon:
    """Un cache de brouillon et un faux moteur, le temps d'un test."""

    def __init__(self) -> None:
        self._temporaire = tempfile.TemporaryDirectory(prefix="vsm-test-cache-projet-")
        self.racine = Path(self._temporaire.name)
        self.cache = self.racine / "cache"
        self.moteur = self.racine / "vsm-render"
        self.moteur.write_bytes(b"moteur A")
        self._vrai_dossier = rc.dossier_du_cache
        self._compte = dict(rc.COMPTE_PROJET)

    def __enter__(self) -> "_Brouillon":
        rc.dossier_du_cache = lambda: self.cache  # type: ignore[assignment]
        rc.COMPTE_PROJET.update(relues=0, payees=0)
        return self

    def __exit__(self, *exc_info) -> None:
        rc.dossier_du_cache = self._vrai_dossier  # type: ignore[assignment]
        rc.COMPTE_PROJET.update(self._compte)
        self._temporaire.cleanup()

    def rangees(self) -> int:
        return len(list(self.cache.glob("*.json"))) if self.cache.is_dir() else 0


def _cle(b: _Brouillon, pistes: List[ExportTrack], nom: str = "projet", tempo: float = 120.0,
         taux: int = TAUX) -> str:
    dossier = b.racine / nom
    write_project_bundle(pistes, dossier, title="verdict-mélange", tempo=tempo)
    return rc.cle_de_projet(dossier, taux, str(b.moteur))


@test
def cache_projet_la_cle_dit_tout_ce_que_le_moteur_lit():
    with _Brouillon() as b:
        reference = _cle(b, _pistes())
        assert_equal(_cle(b, _pistes()), reference, "le même état, deux fois")
        assert_equal(_cle(b, _pistes(), nom="ailleurs"), reference, "le même état dans un autre dossier")

        variantes: Dict[str, str] = {}
        p = _pistes()
        p[0].volume = 0.5
        variantes["volume"] = _cle(b, p)
        p = _pistes()
        p[1].parameters["env.attack"] = 0.21
        variantes["patch"] = _cle(b, p)
        p = _pistes()
        p[0].notes[0].duration = 0.41
        variantes["note"] = _cle(b, p)
        p = _pistes()
        p[1].machine = "vsm.cs80"
        variantes["machine"] = _cle(b, p)
        p = _pistes()
        p[0].pan = 0.3
        variantes["panoramique"] = _cle(b, p)
        variantes["tempo"] = _cle(b, _pistes(), tempo=138.0)
        variantes["fréquence"] = _cle(b, _pistes(), taux=48000)
        diapason.poser(443.1372)
        try:
            variantes["diapason"] = _cle(b, _pistes())
        finally:
            diapason.poser(440.0)
        b.moteur.write_bytes(b"moteur B")
        rc._empreintes.clear()
        variantes["moteur"] = _cle(b, _pistes())
        b.moteur.write_bytes(b"moteur A")
        rc._empreintes.clear()

        for nom, cle in variantes.items():
            assert_true(cle != reference, f"un changement de {nom} doit changer la clé")
        assert_equal(len(set(variantes.values())), len(variantes), "et chacun la sienne")
        assert_equal(_cle(b, _pistes()), reference, "revenu à l'état de départ, la clé de départ")


@test
def cache_projet_la_cle_lit_le_contenu_des_echantillons_pas_la_sortie():
    with _Brouillon() as b:
        dossier = b.racine / "projet"
        (dossier / "samples").mkdir(parents=True)
        (dossier / "samples" / "kick.wav").write_bytes(b"kick 1")
        reference = _cle(b, _pistes())
        (dossier / "rendu.wav").write_bytes(b"un rendu d'avant")
        assert_equal(_cle(b, _pistes()), reference, "la sortie du moteur n'entre pas dans la clé")
        (dossier / "samples" / "kick.wav").write_bytes(b"kick 2")
        assert_true(_cle(b, _pistes()) != reference, "un échantillon changé de même taille change la clé")


@test
def cache_projet_une_mesure_payee_se_relit_au_bit_pres_sans_rendu():
    with _Brouillon() as b:
        appels = {"n": 0}
        signal = np.sin(np.arange(DUREE) * 0.05).astype(np.float32)

        def rendre(tracks, dossier, sample_rate, tempo, binary):
            appels["n"] += 1
            return signal * float(tracks[0].volume)

        def mesurer(rendu):
            return float(np.abs(rendu).mean()) / 3.0   # un flottant sans écriture décimale courte

        pistes = _pistes()
        dossier = b.racine / "variante"
        args = (dossier, TAUX, 120.0, str(b.moteur), mesurer, "v2", "cible-a", rendre)
        payee = mv.distance_de_projet(pistes, *args)
        relue = mv.distance_de_projet(pistes, *args)
        assert_equal(appels["n"], 1, "la seconde mesure ne rend rien")
        assert_equal(relue, payee, "relue au bit près")
        assert_equal(dict(rc.COMPTE_PROJET), {"relues": 1, "payees": 1}, "le compte dit ce qui a été relu")

        pistes[0].volume = 0.4
        autre = mv.distance_de_projet(pistes, *args)
        assert_equal(appels["n"], 2, "un autre état se repaie")
        assert_true(autre != payee, "et il a sa propre mesure")

        mv.distance_de_projet(pistes, dossier, TAUX, 120.0, str(b.moteur), mesurer, "v2", "cible-b", rendre)
        assert_equal(appels["n"], 3, "une autre cible se repaie")
        mv.distance_de_projet(pistes, dossier, TAUX, 120.0, str(b.moteur), mesurer, "v3", "cible-b", rendre)
        assert_equal(appels["n"], 4, "une autre métrique se repaie")


@test
def cache_projet_un_rendu_en_echec_n_est_jamais_range():
    with _Brouillon() as b:
        appels = {"n": 0}

        def rendre(tracks, dossier, sample_rate, tempo, binary):
            appels["n"] += 1
            return None

        args = (b.racine / "variante", TAUX, 120.0, str(b.moteur), lambda rendu: 0.0, "v2", "cible", rendre)
        assert_equal(mv.distance_de_projet(_pistes(), *args), float("inf"))
        assert_equal(mv.distance_de_projet(_pistes(), *args), float("inf"))
        assert_equal(appels["n"], 2, "l'échec est retenté, pas relu")
        assert_equal(b.rangees(), 0, "rien n'est rangé")


@test
def cache_projet_sans_l_option_rien_n_est_lu_ni_ecrit():
    with _Brouillon() as b:
        appels = {"n": 0}

        def rendre(tracks, dossier, sample_rate, tempo, binary):
            appels["n"] += 1
            return np.ones(DUREE, dtype=np.float32)

        args = (b.racine / "variante", TAUX, 120.0, str(b.moteur), lambda rendu: 0.25, "v2", "", rendre)
        mv.distance_de_projet(_pistes(), *args)
        mv.distance_de_projet(_pistes(), *args)
        assert_equal(appels["n"], 2, "cible vide : chaque mesure est rendue, comme avant")
        assert_equal(b.rangees(), 0, "et rien n'est rangé")
        assert_equal(dict(rc.COMPTE_PROJET), {"relues": 0, "payees": 0})


def _faux_rendu(appels: Dict[str, int]):
    temps = np.arange(DUREE, dtype=np.float32) / TAUX

    def rendre(tracks, dossier, sample_rate, tempo, binary):
        appels["n"] += 1
        somme = np.zeros(DUREE, dtype=np.float32)
        for t in tracks:
            if t.volume <= 0.0 or not t.machine:
                continue
            brillance = float(sum(t.parameters.values())) if t.parameters else 1.0
            hz = 220.0 if t.name == "bass" else 330.0
            somme = somme + np.sin(2 * np.pi * (hz + brillance) * temps).astype(np.float32) * float(t.volume)
        return somme
    return rendre


@test
def cache_projet_le_verdict_rejoue_ne_rend_rien_et_decide_pareil():
    with _Brouillon() as b:
        appels = {"n": 0}
        temps = np.arange(DUREE, dtype=np.float32) / TAUX
        melange = (np.sin(2 * np.pi * 225 * temps) + np.sin(2 * np.pi * 332 * temps)).astype(np.float32)
        alternatives = {"bass": [mv.MixAlternative(label="avant réglage", parameters={"filter.1.cutoff": 5.0})]}

        def passe(render_cache: bool):
            mv._deja_dit.clear()
            return mv.keep_what_helps_the_mix(
                tracks=_pistes(), alternatives=alternatives, mixture=melange, stems_audio={},
                samples_root=b.racine, workdir=b.racine / "verdict", sample_rate=TAUX,
                binary=str(b.moteur), render_cache=render_cache)

        vrai = mv._render_project
        mv._render_project = _faux_rendu(appels)
        try:
            temoin = passe(False)
            rendus_du_temoin = appels["n"]
            assert_equal(b.rangees(), 0, "sans l'option, le verdict ne range rien")
            premiere = passe(True)
            rendus_de_la_premiere = appels["n"] - rendus_du_temoin
            seconde = passe(True)
            rendus_de_la_seconde = appels["n"] - rendus_du_temoin - rendus_de_la_premiere
        finally:
            mv._render_project = vrai

        assert_true(rendus_du_temoin >= 4, f"le témoin rend chaque variante : {rendus_du_temoin}")
        assert_true(0 < rendus_de_la_premiere <= rendus_du_temoin, "la première passe paie ses mesures")
        assert_equal(rendus_de_la_seconde, 0, "la passe rejouée ne rend plus rien")
        assert_equal(premiere, temoin, "avec le cache, les mêmes décisions que sans")
        assert_equal(seconde, temoin, "rejouée, les mêmes décisions encore")
        assert_true(rc.COMPTE_PROJET["relues"] >= rendus_du_temoin, "et le compte dit ce qui a été relu")


class _FauxMoteur:
    def search_profile(self, machine: str):
        return [SearchDimension("filter.1.cutoff", 100.0, 1000.0), SearchDimension("filter.1.resonance", 0.0, 1.0)]

    def parameters(self, machine: str):
        return [{"id": "filter.1.cutoff", "default": 700.0}, {"id": "filter.1.resonance", "default": 0.2}]


@test
def cache_projet_le_reglage_au_melange_rejoue_ne_rend_rien_et_arrive_au_meme_patch():
    with _Brouillon() as b:
        appels = {"n": 0}
        temps = np.arange(DUREE, dtype=np.float32) / TAUX
        melange = (np.sin(2 * np.pi * 620 * temps) + np.sin(2 * np.pi * 330.2 * temps)).astype(np.float32)

        def passe(render_cache: bool):
            pistes = _pistes()
            issue = mr.refine_against_mix(
                pistes, "bass", melange, {}, b.racine, workdir=b.racine / "verdict", sample_rate=TAUX,
                engine=_FauxMoteur(), budget=12, binary=str(b.moteur), render_cache=render_cache)  # type: ignore[arg-type]
            return issue, dict(pistes[0].parameters)

        vrai = mr._render_project
        mr._render_project = _faux_rendu(appels)
        try:
            temoin, patch_temoin = passe(False)
            rendus_du_temoin = appels["n"]
            premiere, patch_premiere = passe(True)
            avant_la_seconde = appels["n"]
            seconde, patch_seconde = passe(True)
            rendus_de_la_seconde = appels["n"] - avant_la_seconde
        finally:
            mr._render_project = vrai

        assert_true(temoin is not None and premiere is not None and seconde is not None, "le réglage a eu lieu")
        assert_true(rendus_du_temoin >= 5, f"le témoin rend chaque évaluation : {rendus_du_temoin}")
        assert_equal(rendus_de_la_seconde, 0, "le réglage rejoué ne rend plus rien")
        assert_equal(premiere, temoin, "avec le cache, la même issue que sans")
        assert_equal(seconde, temoin, "rejoué, la même issue encore — évaluations comptées pareil")
        assert_equal((patch_premiere, patch_seconde), (patch_temoin, patch_temoin), "et le même patch sur la piste")
