"""D282 : la coupure adaptée du grave de la basse, dans la chaîne comme dans l'outil.

CE QUE CES TESTS GARDENT.
  1. La règle (analyzer/coupure_basse.py) : sonde courte → aucune coupure, dite ;
     sinon 0,75 × la fréquence du 20ᵉ centile ; et le mur spectral retire ce qui
     est sous la coupure sans toucher ce qui est au-dessus.
  2. La chaîne (reconstruire.py) : SANS l'option, la transcription est celle
     d'aujourd'hui — un seul appel, sur le stem tel quel ; AVEC l'option, seul le
     stem « bass » est transcrit deux fois (la sonde, puis le stem coupé), la
     décision est DITE et gardée pour le rapport, et une sonde trop courte laisse
     le stem tel quel.
  3. Le rapport porte la décision (« coupureBasse ») quand elle existe, et rien
     sinon ; la provenance dit si l'option était là.
  4. Le banc (banc_synthetique.py --stems-de) reprend les stems d'un lot, sous
     `stems/` seulement, et refuse un morceau qui n'en a pas.

POURQUOI L'OBJET DU CHEMIN RÉEL. Les tests de la chaîne passent par
`reconstruire_stem_melodique`, la fonction que la course appelle, avec un
`Contexte` réel — pas par un double qui aurait les mêmes champs à un près
(le piège du 13/09, `SearchParameter` contre `SearchDimension`).
"""
from __future__ import annotations

import io
import json
import subprocess
import sys
import tempfile
import types
from contextlib import redirect_stdout
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, test  # noqa: E402

import banc_synthetique  # noqa: E402
import reconstruire  # noqa: E402
from analyzer.coupure_basse import (CENTILE, FACTEUR, SONDE_MINIMUM,  # noqa: E402
                                    coupure_adaptee, passe_haut)
from analyzer.synth_engine import midi_to_hz  # noqa: E402
from analyzer.vsm_reconstruct import StemNote  # noqa: E402


# ---------------------------------------------------------------------------
# 1. La règle
# ---------------------------------------------------------------------------

@test
def une_sonde_trop_courte_ne_coupe_rien_et_le_dit():
    c = coupure_adaptee([40, 41, 42, 43][: SONDE_MINIMUM - 1])
    assert_true(not c.active, "moins de SONDE_MINIMUM notes : aucune coupure")
    assert_equal(c.hz, 0.0)
    assert_true(c.note is None)
    assert_true("NON filtré" in c.dire(), "la décision de ne pas filtrer est dite")
    assert_equal(c.json()["coupureHz"], 0.0)


@test
def la_coupure_est_trois_quarts_de_la_frequence_du_vingtieme_centile():
    # Dix notes : le 20ᵉ centile est la troisième (index int(10 × 0,20) = 2).
    hauteurs = [48, 40, 45, 43, 50, 52, 47, 41, 49, 46]
    c = coupure_adaptee(hauteurs)
    assert_true(c.active)
    assert_equal(c.note, sorted(hauteurs)[int(len(hauteurs) * CENTILE / 100.0)])
    assert_near(c.hz, FACTEUR * midi_to_hz(c.note), 1e-9)
    assert_equal(c.sonde, 10)
    assert_true("coupure à" in c.dire() and "Hz" in c.dire(), "la coupure est dite en hertz")


@test
def le_centile_ne_deborde_jamais_de_la_liste():
    # Exactement SONDE_MINIMUM notes, centile à 100 % : l'index est borné.
    c = coupure_adaptee([40] * SONDE_MINIMUM, centile=100.0)
    assert_equal(c.note, 40)


@test
def le_mur_spectral_retire_le_grave_et_garde_le_reste():
    sr = 8000.0
    t = np.arange(int(sr)) / sr
    grave = np.sin(2 * np.pi * 40.0 * t)
    aigu = np.sin(2 * np.pi * 200.0 * t)
    y = passe_haut(grave + aigu, sr, 60.0)
    spectre = np.abs(np.fft.rfft(y))
    f = np.fft.rfftfreq(len(y), 1.0 / sr)
    energie_40 = float(spectre[np.argmin(np.abs(f - 40.0))])
    energie_200 = float(spectre[np.argmin(np.abs(f - 200.0))])
    assert_true(energie_40 < 1e-6 * energie_200, f"le 40 Hz est retiré ({energie_40:.3g} vs {energie_200:.3g})")
    assert_near(energie_200, float(np.abs(np.fft.rfft(aigu))[np.argmin(np.abs(f - 200.0))]), 1e-6)
    # 0 Hz : le signal tel quel, au bit près.
    x = np.random.default_rng(1).standard_normal(1000)
    assert_true(passe_haut(x, sr, 0.0) is x, "sans coupure, l'objet même est rendu")


# ---------------------------------------------------------------------------
# 2. La chaîne
# ---------------------------------------------------------------------------

def _notes(hauteurs):
    return [StemNote(note=h, velocity=80, start=0.5 * i, duration=0.4, confidence=0.9)
            for i, h in enumerate(hauteurs)]


class _Chaine:
    """`reconstruire_stem_melodique` avec ses trois voisins remplacés : la
    transcription (on note ce qu'elle reçoit), le chargement audio (un signal
    connu) et `_reconstruire_notes` (arrêt juste après la transcription)."""

    def __init__(self, hauteurs_sonde, hauteurs_coupe, adaptee: bool):
        self.appels: list = []
        self.recues: list = []
        self.hauteurs_sonde = list(hauteurs_sonde)
        self.hauteurs_coupe = list(hauteurs_coupe)
        self.adaptee = adaptee

    def __enter__(self):
        self._sauve = (reconstruire.extraire_notes, reconstruire.charger_audio,
                       reconstruire._reconstruire_notes)

        def extraire(chemin):
            self.appels.append(Path(chemin))
            return _notes(self.hauteurs_coupe if "-coupe-" in Path(chemin).name else self.hauteurs_sonde)

        def charger(chemin):
            sr = reconstruire.SAMPLE_RATE
            t = np.arange(sr) / sr
            return (0.5 * np.sin(2 * np.pi * 40.0 * t) + 0.5 * np.sin(2 * np.pi * 220.0 * t)).astype(np.float32)

        def reconstruire_notes(ctx, nom, notes, audio):
            self.recues.append((nom, [n.note for n in notes]))
            return None

        reconstruire.extraire_notes = extraire
        reconstruire.charger_audio = charger
        reconstruire._reconstruire_notes = reconstruire_notes
        self.dossier = tempfile.TemporaryDirectory()
        travail = Path(self.dossier.name)
        args = types.SimpleNamespace(coupure_basse_adaptee=self.adaptee, voix_par_vides=False,
                                     voix_par_stem=0, porte_paliers=False)
        self.ctx = reconstruire.Contexte(args=args, moteur=None, sortie=travail, travail=travail,
                                         candidates=[])
        return self

    def __exit__(self, *_):
        (reconstruire.extraire_notes, reconstruire.charger_audio,
         reconstruire._reconstruire_notes) = self._sauve
        self.dossier.cleanup()

    def courir(self, nom: str) -> str:
        sortie = io.StringIO()
        with redirect_stdout(sortie):
            resultat = reconstruire.reconstruire_stem_melodique(self.ctx, nom, Path(f"/stems/{nom}.wav"))
        assert_equal(resultat, [], "le double de _reconstruire_notes rend None : liste vide")
        return sortie.getvalue()


SONDE = [40, 41, 42, 43, 44, 45, 46, 47, 48, 49]
COUPE = [52, 53, 54, 55, 56, 57]


@test
def sans_l_option_la_transcription_est_celle_d_aujourd_hui():
    with _Chaine(SONDE, COUPE, adaptee=False) as ch:
        journal = ch.courir("bass")
        assert_equal(len(ch.appels), 1, "UN appel de transcription")
        assert_equal(ch.appels[0], Path("/stems/bass.wav"), "sur le stem tel quel")
        assert_equal(ch.recues, [("bass", SONDE)], "les notes de la sonde partent à l'arbitrage")
        assert_true("coupure" not in journal, "rien n'est dit d'une coupure qui n'existe pas")
        assert_equal(ch.ctx.decisions, {}, "aucune décision au rapport")


@test
def avec_l_option_la_basse_est_transcrite_deux_fois_et_la_decision_est_dite():
    with _Chaine(SONDE, COUPE, adaptee=True) as ch:
        journal = ch.courir("bass")
        assert_equal(len(ch.appels), 2, "la sonde, puis le stem coupé")
        assert_equal(ch.appels[0], Path("/stems/bass.wav"))
        coupe = ch.appels[1]
        attendu = coupure_adaptee(SONDE)
        assert_true(coupe.name == f"bass-coupe-{attendu.hz:.0f}hz.wav", f"fichier coupé nommé par sa coupure : {coupe.name}")
        assert_true(coupe.parent == ch.ctx.travail, "écrit dans le dossier de travail")
        assert_true(coupe.is_file() and coupe.stat().st_size > 44, "le fichier coupé existe et n'est pas vide")
        assert_equal(ch.recues, [("bass", COUPE)], "les notes du stem COUPÉ partent à l'arbitrage")
        decisions = ch.ctx.decisions["coupureBasse"]
        assert_equal(len(decisions), 1)
        d = decisions[0]
        assert_equal(d["stem"], "bass")
        assert_equal(d["centileMidi"], attendu.note)
        assert_near(d["coupureHz"], attendu.hz, 1e-9)
        assert_equal(d["notesSonde"], len(SONDE))
        assert_equal(d["notesApresCoupure"], len(COUPE))
        assert_true(f"{attendu.hz:.1f} Hz" in journal, f"la coupure est dite au journal : {journal!r}")
        assert_true(f"{len(SONDE)} notes avant, {len(COUPE)} après" in journal)


@test
def avec_l_option_les_autres_stems_ne_bougent_pas():
    with _Chaine(SONDE, COUPE, adaptee=True) as ch:
        ch.courir("other")
        assert_equal(len(ch.appels), 1, "un seul appel : « other » n'est pas coupé")
        assert_equal(ch.recues, [("other", SONDE)])
        assert_equal(ch.ctx.decisions, {})


@test
def une_sonde_trop_courte_laisse_le_stem_tel_quel_et_le_dit():
    courte = [40, 52, 53][: SONDE_MINIMUM - 1]
    with _Chaine(courte, COUPE, adaptee=True) as ch:
        journal = ch.courir("bass")
        assert_equal(len(ch.appels), 1, "pas de seconde transcription")
        assert_equal(ch.recues, [("bass", courte)], "les notes de la sonde servent telles quelles")
        assert_true("NON filtré" in journal, f"le refus de filtrer est dit : {journal!r}")
        d = ch.ctx.decisions["coupureBasse"][0]
        assert_equal(d["coupureHz"], 0.0)
        assert_true(d["centileMidi"] is None)


# ---------------------------------------------------------------------------
# 3. Le rapport et la provenance
# ---------------------------------------------------------------------------

@test
def le_rapport_porte_la_coupure_quand_elle_existe_et_rien_sinon():
    from analyzer.vsm_reconstruct import StemReconstruction, write_reconstruction_report
    stem = StemReconstruction(name="bass", machine="vsm.sh101", parameters={}, distance=0.3,
                              notes=_notes([40]), considered=[("vsm.sh101", 0.3)])
    with tempfile.TemporaryDirectory() as d:
        chemin = Path(d) / "rapport.json"
        write_reconstruction_report([stem], chemin, metric="v2",
                                    coupure_basse=[{"stem": "bass", "coupureHz": 38.9}])
        r = json.loads(chemin.read_text(encoding="utf-8"))
        assert_equal(r["coupureBasse"], [{"stem": "bass", "coupureHz": 38.9}])
        write_reconstruction_report([stem], chemin, metric="v2")
        r = json.loads(chemin.read_text(encoding="utf-8"))
        assert_true("coupureBasse" not in r, "sans coupure, le rapport est celui d'aujourd'hui")


@test
def la_provenance_dit_si_l_option_etait_la():
    parseur = reconstruire.construire_parseur()
    sans = parseur.parse_args(["x.wav"])
    avec = parseur.parse_args(["x.wav", "--coupure-basse-adaptee"])
    assert_true(sans.coupure_basse_adaptee is False, "le défaut : la chaîne d'aujourd'hui")
    assert_true(avec.coupure_basse_adaptee is True)
    assert_true(reconstruire.provenance(sans, None, None)["options"]["coupureBasseAdaptee"] is False)
    assert_true(reconstruire.provenance(avec, None, None)["options"]["coupureBasseAdaptee"] is True)


# ---------------------------------------------------------------------------
# 4. Le banc
# ---------------------------------------------------------------------------

@test
def le_banc_reprend_les_stems_d_un_lot_sous_stems_seulement():
    with tempfile.TemporaryDirectory() as d:
        lot = Path(d) / "lot-source"
        stems = lot / "morceau-0001-g1" / "stems-separes" / "stems"
        stems.mkdir(parents=True)
        (stems / "bass.wav").write_bytes(b"RIFF")
        # Un résidu de la boucle, avec une basse HOMONYME : il ne doit pas être repris.
        residu = lot / "morceau-0001-g1" / "stems-separes" / "residu-r1" / "stems"
        residu.mkdir(parents=True)
        (residu / "bass.wav").write_bytes(b"RIFF")
        morceau = Path(d) / "src" / "morceau-0001-g1"
        morceau.mkdir(parents=True)
        sortie = Path(d) / "banc" / "morceau-0001-g1"
        sortie.mkdir(parents=True)
        vus: list = []

        class Resultat:
            returncode = 0

        vrai_run = subprocess.run
        subprocess.run = lambda argv, **k: (vus.append(list(argv)), Resultat())[1]
        try:
            args = types.SimpleNamespace(stems_de=str(lot), stems_vrais=False, rendus_paralleles=2, moteur=None)
            with redirect_stdout(io.StringIO()):
                course = banc_synthetique.courir(morceau, sortie, args, ["--coupure-basse-adaptee"])
        finally:
            subprocess.run = vrai_run
        assert_equal(course["code"], 0)
        argv = vus[0]
        i = argv.index("--stems")
        assert_equal(Path(argv[i + 1]), stems, "les stems repris sont ceux de stems/, pas ceux du résidu")
        assert_true("--garder-stems" not in argv, "rien à garder : rien n'a été séparé")
        assert_equal(argv[-1], "--coupure-basse-adaptee", "les options de la chaîne suivent")
        lien = sortie / "stems-separes"
        assert_true(lien.is_symlink() and lien.resolve() == stems.resolve(),
                    "le banc mesure la séparation sur les stems repris, par un lien")


@test
def le_banc_refuse_un_morceau_dont_le_lot_n_a_pas_les_stems():
    with tempfile.TemporaryDirectory() as d:
        lot = Path(d) / "lot-source"
        (lot / "morceau-0002-g2" / "stems-separes" / "stems").mkdir(parents=True)   # vide
        morceau = Path(d) / "src" / "morceau-0002-g2"
        morceau.mkdir(parents=True)
        sortie = Path(d) / "banc" / "morceau-0002-g2"
        sortie.mkdir(parents=True)
        vrai_run = subprocess.run
        subprocess.run = lambda *a, **k: (_ for _ in ()).throw(AssertionError("la chaîne ne doit pas partir"))
        try:
            args = types.SimpleNamespace(stems_de=str(lot), stems_vrais=False, rendus_paralleles=2, moteur=None)
            texte = io.StringIO()
            with redirect_stdout(texte):
                course = banc_synthetique.courir(morceau, sortie, args, [])
        finally:
            subprocess.run = vrai_run
        assert_true(course["code"] != 0, "le morceau n'est pas couru")
        assert_true("aucun stem repris" in course["raison"], course["raison"])
        assert_true("SANS STEMS" in texte.getvalue(), "et c'est dit au journal")
        assert_true(not (sortie / "stems-separes").exists(), "aucun lien vers un dossier vide")
