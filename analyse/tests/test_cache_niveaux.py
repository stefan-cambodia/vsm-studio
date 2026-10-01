"""H58 — les niveaux des rendus SOLO du calage rangés à leur tour (docs/CDC-reload-indifferenciable.md § 10.4).

Tout relu par le cache de H48, 97 % de ce que coûtait encore un réglage au mélange
étaient les rendus solo du calage de niveau (§ 10.3). Ces tests ne lancent pas le
moteur : le rendu solo est remplacé par un faux qui COMPTE ses appels, le cache vit
dans un dossier de brouillon, et le moteur de la course est un fichier factice.

  - un niveau payé se relit AU BIT près, sans rendu, et le volume calé est le même ;
  - un patch changé se repaie ;
  - sans l'option, rien n'est lu ni écrit, et le calage est celui d'avant, au bit ;
  - un groupe se range par les clés de ses membres, DANS L'ORDRE, et se relit entier ;
  - un rendu en échec n'est jamais rangé, ni la somme d'un groupe dont un membre a échoué ;
  - la clé suit la durée rendue et la longueur du stem.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer import vsm_engine  # noqa: E402
from analyzer import vsm_levels as lv  # noqa: E402
from analyzer import vsm_render_cache as rc  # noqa: E402
from analyzer.vsm_project_export import ExportNote, ExportTrack  # noqa: E402

TAUX = 8000
N_STEM = 4000


def _piste(nom: str = "bass", coupure: float = 700.0) -> ExportTrack:
    return ExportTrack(name=nom, machine="vsm.tb303", parameters={"filter.1.cutoff": coupure},
                       notes=[ExportNote(note=48, velocity=100, start=0.0, duration=0.4)])


def _stem(amplitude: float = 0.2) -> np.ndarray:
    t = np.arange(N_STEM) / TAUX
    return (amplitude * np.sin(2 * np.pi * 110.0 * t)).astype(np.float32)


class _Brouillon:
    """Un cache de brouillon, un faux moteur et un faux rendu solo, le temps d'un test."""

    def __init__(self, ranger: bool = True, echec: Optional[set] = None) -> None:
        self._temporaire = tempfile.TemporaryDirectory(prefix="vsm-test-cache-niveaux-")
        self.racine = Path(self._temporaire.name)
        self.cache = self.racine / "cache"
        self.moteur = self.racine / "vsm-render"
        self.moteur.write_bytes(b"moteur A")
        self.ranger, self.echec = ranger, set(echec or ())
        self.rendus: List[str] = []
        self._vrai_dossier = rc.dossier_du_cache
        self._vrai_rendu = lv._render_track
        self._compte = dict(rc.COMPTE_NIVEAU)
        self._actif = rc.cache_des_niveaux_actif()

    def _rendre(self, track: ExportTrack, folder: Path, duration: float, sample_rate: int) -> Optional[np.ndarray]:
        self.rendus.append(track.name)
        if track.name in self.echec:
            return None
        # Un rendu qui DÉPEND de ce que la piste porte : la coupure fait l'amplitude,
        # le volume la multiplie — comme le vrai, qui inclut le volume de la piste.
        t = np.arange(int(round(duration * sample_rate))) / sample_rate
        amplitude = track.parameters.get("filter.1.cutoff", 500.0) / 1000.0 * track.volume
        return (amplitude * np.sin(2 * np.pi * 220.0 * t)).astype(np.float32)

    def __enter__(self) -> "_Brouillon":
        rc.dossier_du_cache = lambda: self.cache  # type: ignore[assignment]
        lv._render_track = self._rendre  # type: ignore[assignment]
        vsm_engine.poser_moteur_de_course(str(self.moteur))
        rc.poser_cache_des_niveaux(self.ranger)
        rc.COMPTE_NIVEAU.update(relues=0, payees=0)
        return self

    def __exit__(self, *exc_info) -> None:
        rc.dossier_du_cache = self._vrai_dossier  # type: ignore[assignment]
        lv._render_track = self._vrai_rendu  # type: ignore[assignment]
        vsm_engine.poser_moteur_de_course(None)
        rc.poser_cache_des_niveaux(self._actif)
        rc.COMPTE_NIVEAU.update(self._compte)
        self._temporaire.cleanup()

    def rangees(self) -> int:
        return len(list(self.cache.glob("*.json"))) if self.cache.is_dir() else 0


def _caler(b: _Brouillon, pistes: List[ExportTrack], groupes: Optional[Dict[str, str]] = None,
           stems: Optional[Dict[str, np.ndarray]] = None) -> List[float]:
    lv.match_track_levels(pistes, stems or {"bass": _stem()}, b.racine, TAUX, groupes=groupes)
    return [p.volume for p in pistes]


@test
def h58_un_niveau_paye_se_relit_au_bit_sans_rendu():
    with _Brouillon() as b:
        premier = _caler(b, [_piste()])
        assert_equal(len(b.rendus), 1)
        second = _caler(b, [_piste()])
        assert_equal(len(b.rendus), 1)             # rien de rendu la seconde fois
        assert_equal(second, premier)              # au bit près
        assert_equal(dict(rc.COMPTE_NIVEAU), {"relues": 1, "payees": 1})
        assert_equal(b.rangees(), 1)


@test
def h58_un_patch_change_se_repaie():
    with _Brouillon() as b:
        _caler(b, [_piste(coupure=700.0)])
        _caler(b, [_piste(coupure=900.0)])
        assert_equal(len(b.rendus), 2)
        assert_equal(dict(rc.COMPTE_NIVEAU), {"relues": 0, "payees": 2})


@test
def h58_sans_l_option_rien_n_est_lu_ni_ecrit_et_le_calage_est_celui_d_avant():
    with _Brouillon(ranger=False) as b:
        temoin = _caler(b, [_piste()])
        _caler(b, [_piste()])
        assert_equal(len(b.rendus), 2)
        assert_equal(b.rangees(), 0)
        assert_equal(dict(rc.COMPTE_NIVEAU), {"relues": 0, "payees": 0})
    with _Brouillon() as b:
        paye = _caler(b, [_piste()])
        relu = _caler(b, [_piste()])
    assert_equal(paye, temoin)
    assert_equal(relu, temoin)


@test
def h58_un_groupe_se_range_par_ses_membres_dans_l_ordre():
    groupes = {"voix 1": "other", "voix 2": "other"}
    stems = {"voix 1": _stem(0.3)}

    def membres(c1: float, c2: float) -> List[ExportTrack]:
        return [_piste("voix 1", c1), _piste("voix 2", c2)]

    with _Brouillon() as b:
        premier = _caler(b, membres(600.0, 800.0), groupes, stems)
        assert_equal(len(b.rendus), 2)
        second = _caler(b, membres(600.0, 800.0), groupes, stems)
        assert_equal(len(b.rendus), 2)             # le groupe entier relu
        assert_equal(second, premier)
        _caler(b, membres(600.0, 850.0), groupes, stems)
        assert_equal(len(b.rendus), 4)             # un membre changé : le groupe se repaie
    # L'ORDRE compte dans la clé : la somme est la même, mais la clé ne le sait pas, et
    # mieux vaut un hit manqué qu'un faux.
    a = rc.cle_de_niveau(["x", "y"], 0.5, N_STEM)
    assert_true(a != rc.cle_de_niveau(["y", "x"], 0.5, N_STEM), "l'ordre des membres n'entre pas dans la clé")


@test
def h58_un_rendu_en_echec_n_est_jamais_range():
    with _Brouillon(echec={"bass"}) as b:
        rapports = lv.match_track_levels([_piste()], {"bass": _stem()}, b.racine, TAUX)
        assert_true(any("rendu solo impossible" in r for r in rapports), str(rapports))
        assert_equal(b.rangees(), 0)
        assert_equal(dict(rc.COMPTE_NIVEAU), {"relues": 0, "payees": 0})


@test
def h58_un_groupe_dont_un_membre_a_echoue_n_est_pas_range():
    # La somme se calcule sans lui (le chemin d'avant), mais elle ne vaut que pour cet
    # instant : rangée, elle deviendrait la vérité des courses suivantes.
    groupes = {"voix 1": "other", "voix 2": "other"}
    with _Brouillon(echec={"voix 2"}) as b:
        _caler(b, [_piste("voix 1", 600.0), _piste("voix 2", 800.0)], groupes, {"voix 1": _stem(0.3)})
        assert_equal(b.rangees(), 0)
        assert_equal(dict(rc.COMPTE_NIVEAU), {"relues": 0, "payees": 0})


@test
def h58_la_cle_suit_la_duree_et_le_stem():
    base = rc.cle_de_niveau(["p"], 0.5, N_STEM)
    assert_equal(base, rc.cle_de_niveau(["p"], 0.5, N_STEM))
    assert_true(base != rc.cle_de_niveau(["p"], 0.6, N_STEM), "la durée n'entre pas dans la clé")
    assert_true(base != rc.cle_de_niveau(["p"], 0.5, N_STEM + 1), "le stem n'entre pas dans la clé")
