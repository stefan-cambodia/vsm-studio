"""A4.1 pour le classifieur de frappes, et la batterie au verdict du mélange.

Deux choses que la chaîne promettait et ne tenait pas :

  - un modèle de batterie n'était JAMAIS vérifié au chargement ; un kick de
    909 qui change laissait en place un modèle qui nomme des kicks de 909 ;
  - une boîte à rythmes écartée sur la piste disparaissait de la chaîne, alors
    que le verdict du mélange sait défaire un choix de machine -- à condition
    que la candidate revienne avec SES notes, pas celles d'une autre.
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

from analyzer.vsm_drum_corpus import (NOTES_PAR_MACHINE, ClassifieurFrappes,  # noqa: E402
                                      empreinte_batterie)
from analyzer.vsm_engine import Note, VsmEngine  # noqa: E402
from analyzer.vsm_mix_verdict import MixAlternative, keep_what_helps_the_mix  # noqa: E402
from analyzer.vsm_project_export import ExportNote, ExportTrack  # noqa: E402

SR = 44100


def _modele_factice(empreintes):
    return ClassifieurFrappes(pieces=("kick",), moyenne=np.zeros(72), echelle=np.ones(72),
                              modeles={}, seuil=0.25, date="2026-08-24T00:00:00+00:00",
                              versions={}, empreintes=empreintes)


@test
def frappes_l_empreinte_d_une_boite_joue_son_kit_et_distingue_les_boites():
    with VsmEngine(sample_rate=SR) as moteur:
        e909 = empreinte_batterie(moteur, "vsm.tr909", SR)
        e909_bis = empreinte_batterie(moteur, "vsm.tr909", SR)
        e808 = empreinte_batterie(moteur, "vsm.tr808", SR)
        inconnue = empreinte_batterie(moteur, "vsm.nexistepas", SR)
    assert_true(len(e909) == 64, "une empreinte est un SHA-256")
    assert_equal(e909, e909_bis, "stable d'un rendu à l'autre")
    assert_true(e909 != e808, "la 909 et la 808 n'ont pas la même empreinte")
    assert_equal(inconnue, "", "une machine inconnue n'a PAS d'empreinte (vide, pas inventée)")


@test
def frappes_un_modele_perime_ou_sans_empreinte_est_refuse():
    with VsmEngine(sample_rate=SR) as moteur:
        vraies = {m: empreinte_batterie(moteur, m, SR) for m in NOTES_PAR_MACHINE}
        a_jour = _modele_factice(dict(vraies)).verifie_fraicheur(moteur, SR)
        alterees = dict(vraies)
        alterees["vsm.tr909"] = "0" * 64
        perime = _modele_factice(alterees).verifie_fraicheur(moteur, SR)
        sans = _modele_factice({}).verifie_fraicheur(moteur, SR)
    assert_true(a_jour.frais, f"les vraies empreintes : à jour ({a_jour.resume()})")
    assert_true(not perime.frais and perime.perimees == ("vsm.tr909",),
                f"une empreinte qui change : PÉRIMÉ pour cette boîte ({perime.resume()})")
    assert_true(not sans.frais and not sans.perimees and len(sans.invérifiables) == 3,
                f"sans empreinte : INVÉRIFIABLE, ce qui n'est pas à jour ({sans.resume()})")


@test
def frappes_le_modele_enregistre_porte_ses_empreintes():
    import joblib  # noqa: F401 — la dépendance du modèle, présente dans le venv
    dossier = Path(tempfile.mkdtemp(prefix="vsm-frappes-"))
    modele = _modele_factice({"vsm.tr909": "a" * 64})
    modele.enregistre(dossier / "m.joblib")
    relu = ClassifieurFrappes.relit(dossier / "m.joblib")
    assert_equal(relu.empreintes, {"vsm.tr909": "a" * 64}, "les empreintes font l'aller-retour")


@test
def melange_une_alternative_de_batterie_revient_avec_SES_notes():
    """Le cas de Children : la 909 a pris la piste, `vsm.drums` revient au
    verdict du mélange. Si elle revenait avec les notes de la 909, ses pièces
    absentes de la 909 se tairaient et le verdict jugerait un kit amputé."""
    notes_909 = [ExportNote(36, 110, 0.0, 0.05), ExportNote(39, 100, 0.5, 0.05),
                 ExportNote(42, 90, 0.25, 0.05), ExportNote(42, 90, 0.75, 0.05)]
    notes_drums = [ExportNote(36, 110, 0.0, 0.05), ExportNote(49, 100, 0.5, 0.05),
                   ExportNote(42, 90, 0.25, 0.05), ExportNote(42, 90, 0.75, 0.05)]
    with VsmEngine(sample_rate=SR) as moteur:
        # La CIBLE est vsm.drums elle-même : la bonne réponse est connue.
        cible = moteur.render("vsm.drums", {}, [Note(n.note, n.velocity, n.start, n.duration)
                                                for n in notes_drums], 1.2)
    piste = ExportTrack(name="Batterie", machine="vsm.tr909", parameters={},
                        notes=list(notes_909), is_drums=True)
    dossier = Path(tempfile.mkdtemp(prefix="vsm-melange-"))
    decisions = keep_what_helps_the_mix(
        [piste],
        {"Batterie": [MixAlternative(parameters={}, label="batterie modélisée (vsm.drums)",
                                     machine="vsm.drums", notes=list(notes_drums))]},
        cible, {"Batterie": cible}, dossier, workdir=dossier / "verdict", sample_rate=SR)
    assert_equal(len(decisions), 1, "une décision pour la batterie")
    assert_equal(decisions[0].kept, "batterie modélisée (vsm.drums)",
                 f"la cible est vsm.drums, le mélange la retrouve ({decisions[0].kept}, "
                 f"{decisions[0].distance_kept:.3f} contre {decisions[0].rejected})")
    assert_equal(piste.machine, "vsm.drums", "la piste a changé de machine")
    assert_equal([n.note for n in piste.notes], [n.note for n in notes_drums],
                 "et ses notes sont celles de vsm.drums, pas celles de la 909")


@test
def sans_apprentissage_reproduit_la_chaine_sans_modele():
    """A4.3 : le témoin. Avec un modèle SUR LA LIGNE DE COMMANDE et
    `--sans-apprentissage`, le rapport est celui d'une chaîne sans modèle --
    au chiffre près, provenance mise à part (elle dit que rien n'a été
    consulté, et c'est une information)."""
    import json
    import subprocess
    import wave

    import analyzer.vsm_drum_corpus as mod
    from analyzer.vsm_drum_bench import motif_contretemps, rend_motif
    from analyzer.vsm_drum_corpus import engendre_corpus_frappes, entraine_frappes

    dossier = Path(tempfile.mkdtemp(prefix="vsm-a43-"))
    sauvegarde = dict(mod.NOTES_PAR_MACHINE)
    mod.NOTES_PAR_MACHINE = {"vsm.tr808": sauvegarde["vsm.tr808"]}   # corpus réduit, test court
    try:
        with VsmEngine(sample_rate=SR) as moteur:
            corpus = engendre_corpus_frappes(moteur, graine=3)
            audio = rend_motif(motif_contretemps(), moteur)
    finally:
        mod.NOTES_PAR_MACHINE = sauvegarde
    modele, _ = entraine_frappes(corpus, graine=3)
    modele.enregistre(dossier / "frappes.joblib")
    a = np.clip(np.asarray(audio, dtype=np.float64), -1, 1)
    with wave.open(str(dossier / "motif.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((a * 32767).astype("<i2").tobytes())

    def chaine(*options):
        sortie = dossier / ("sortie-" + "-".join(o.strip("-") for o in options or ("nu",)))
        commande = [sys.executable, str(RACINE / "reconstruire.py"), str(dossier / "motif.wav"),
                    "--sans-separation", "--batterie", "--sans-sampler", "--sortie", str(sortie),
                    "--budget-piste", "4", "--axes-piste", "2", "--iterations", "2", *options]
        resultat = subprocess.run(commande, capture_output=True, text=True, timeout=600)
        assert_equal(resultat.returncode, 0, f"la chaîne aboutit : {resultat.stderr[-400:]}")
        return json.loads((sortie / "rapport.json").read_text(encoding="utf-8"))

    nu = chaine()
    temoin = chaine("--classifieur-batterie", str(dossier / "frappes.joblib"), "--sans-apprentissage")
    assert_equal(temoin["provenance"]["modeles"]["classifieurFrappes"], "aucun",
                 "le témoin dit qu'aucun modèle n'a été consulté")
    for r in (nu, temoin):
        r.pop("provenance")
    assert_equal(json.dumps(temoin, sort_keys=True), json.dumps(nu, sort_keys=True),
                 "hors provenance, le rapport du témoin EST celui de la chaîne sans modèle")
