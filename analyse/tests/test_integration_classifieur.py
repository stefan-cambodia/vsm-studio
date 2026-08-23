"""A1.3 — le classifieur dans la chaîne : consigné, jamais suivi par défaut.

La mesure a tranché avant que le code ne soit écrit : sur *Clair de Lune*, le
classifieur place `vsm.piano` — la machine que l'arbitrage retient réellement —
au rang médian 16 sur 20. S'en servir pour dégrossir aurait éliminé la
gagnante. Ce que ces tests verrouillent est donc la RETENUE du dispositif :

  - avec un classifieur, le verdict est le MÊME que sans ;
  - son avis est tout de même enregistré, pour qu'on puisse un jour juger sur
    pièces s'il mérite qu'on le suive ;
  - la présélection apprise existe, mais il faut la demander.
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

from analyzer.vsm_classifier import charge_corpus, entraine  # noqa: E402
from analyzer.vsm_corpus_build import (GrilleDeNotes, genere_lot, machine_fingerprint,
                                        nouveau_manifeste)  # noqa: E402
from analyzer.vsm_engine import Note, VsmEngine  # noqa: E402
from analyzer.vsm_patch_optimizer import search_space_for_machine  # noqa: E402
from analyzer.vsm_reconstruct import StemNote, reconstruct_stem  # noqa: E402

SR = 44100
MACHINES = ("vsm.minimoog", "vsm.juno106", "vsm.dx7")
GRILLE = GrilleDeNotes(hauteurs=(48, 64), durees=(0.5,), velocites=(60, 105))
_CACHE: dict = {}


def modele_et_cible():
    """Un petit classifieur et un stem d'essai, engendrés une fois."""
    if "modele" in _CACHE:
        return _CACHE["modele"], _CACHE["audio"], _CACHE["notes"]

    dossier = Path(tempfile.mkdtemp(prefix="vsm-a13-"))
    with VsmEngine(sample_rate=SR) as moteur:
        manifeste = nouveau_manifeste(SR, 5, 10, GRILLE, [], "2026-08-23T00:00:00+00:00")
        for machine in MACHINES:
            manifeste.empreintes[machine] = machine_fingerprint(moteur, machine, SR)
            (dossier / machine).mkdir(parents=True, exist_ok=True)
            lot = genere_lot(machine, search_space_for_machine(machine, moteur), moteur,
                             patchs=10, grille=GRILLE, graine=5, sample_rate=SR)
            lot.enregistre(dossier / machine / "lot-000.npz")
            manifeste.exemples[machine] = len(lot.X)
        manifeste.enregistre(dossier / "manifeste.json")

        # La CIBLE est un rendu du Juno : un son que le parc sait produire, donc
        # un cas où le classifieur a toutes ses chances. S'il devait dévier le
        # verdict quelque part, ce serait ici.
        audio = moteur.render("vsm.juno106", {}, [Note(60, 100, 0.0, 0.8)], 1.2)

    modele, _ = entraine(charge_corpus(dossier), graine=5, part_epreuve=0.3)
    notes = [StemNote(note=60, velocity=100, start=0.0, duration=0.8)]
    _CACHE.update(modele=modele, audio=audio, notes=notes)
    return modele, audio, notes


@test
def integration_le_classifieur_ne_change_PAS_le_verdict():
    """Le point entier d'A1.3 : par défaut, il conseille et ne décide pas."""
    modele, audio, notes = modele_et_cible()
    with VsmEngine(sample_rate=SR) as moteur:
        sans = reconstruct_stem("essai", audio, notes, moteur, sample_rate=SR,
                                machines=list(MACHINES), max_iterations=4, shortlist=0)
        avec = reconstruct_stem("essai", audio, notes, moteur, sample_rate=SR,
                                machines=list(MACHINES), max_iterations=4, shortlist=0,
                                classifieur=modele)
    assert_true(sans is not None and avec is not None, "les deux reconstructions aboutissent")
    assert_equal(avec.machine, sans.machine, "MÊME machine retenue")
    assert_true(abs(avec.distance - sans.distance) < 1e-9, "MÊME distance")
    assert_equal(sorted(m for m, _ in avec.considered), sorted(m for m, _ in sans.considered),
                 "MÊMES candidates mesurées : rien n'a été écarté")


@test
def integration_l_avis_du_classifieur_est_bien_consigne():
    """Consigner sans suivre n'est pas une demi-mesure : c'est ce qui permettra
    de juger sur pièces, exécution après exécution."""
    modele, audio, notes = modele_et_cible()
    with VsmEngine(sample_rate=SR) as moteur:
        resultat = reconstruct_stem("essai", audio, notes, moteur, sample_rate=SR,
                                    machines=list(MACHINES), max_iterations=4,
                                    shortlist=0, classifieur=modele)
    assert_true(resultat.classifier_ranking or resultat.classifier_abstention,
                "l'avis doit être là, classement OU abstention motivée")
    if resultat.classifier_ranking:
        machines = [m for m, _ in resultat.classifier_ranking]
        assert_true(all(m in MACHINES for m in machines), "des machines du parc")
        scores = [s for _, s in resultat.classifier_ranking]
        assert_true(scores == sorted(scores, reverse=True), "classement décroissant")


@test
def integration_la_preselection_apprise_doit_etre_DEMANDEE():
    """Elle existe — il faut pouvoir l'essayer — mais elle est éteinte par
    défaut, et l'allumer se voit : le nombre de candidates mesurées tombe."""
    modele, audio, notes = modele_et_cible()
    with VsmEngine(sample_rate=SR) as moteur:
        complet = reconstruct_stem("essai", audio, notes, moteur, sample_rate=SR,
                                   machines=list(MACHINES), max_iterations=4,
                                   shortlist=0, classifieur=modele)
        restreint = reconstruct_stem("essai", audio, notes, moteur, sample_rate=SR,
                                     machines=list(MACHINES), max_iterations=4,
                                     shortlist=0, classifieur=modele,
                                     preselection_apprise=1)
    if restreint.classifier_abstention:
        # Le modèle s'est abstenu : la présélection ne s'applique pas, et c'est
        # le comportement voulu — on ne dégrossit pas sur un avis qu'on refuse.
        assert_equal(len(restreint.considered), len(complet.considered),
                     "une abstention ne doit RIEN écarter")
    else:
        assert_true(len(restreint.considered) < len(complet.considered),
                    "demandée, la présélection écarte réellement des candidates")


@test
def recherche_la_borne_de_niveau_rejette_les_patchs_inaudibles():
    """Le gagnant d'une note doit pouvoir tenir la piste. Sans borne, la
    recherche est libre de retenir un patch quasi muet dont le timbre colle ;
    mesuré sur B4 Wuz Then, c'est arrivé deux stems sur deux (« ×42 »)."""
    from analyzer.vsm_patch_optimizer import optimize_patch_for_machine
    with VsmEngine(sample_rate=SR) as moteur:
        # Cible FORTE : une note de Minimoog, oscillateur 1 à fond.
        #
        # `oscillator.1.level` et non `output.level` : le Minimoog n'a PAS de
        # niveau de sortie, et le service le dit (« paramètre ignoré, absent de
        # cette machine »). Une première version du test le passait quand même
        # et mesurait un rendu IDENTIQUE à tous les niveaux — le test échouait,
        # et c'est lui qui avait raison.
        muets = {"oscillator.2.level": 0.0, "oscillator.3.level": 0.0,
                 "oscillator.noise.level": 0.0}
        cible = moteur.render("vsm.minimoog", {"oscillator.1.level": 1.0, **muets},
                              [Note(48, 120, 0.0, 0.6)], 0.8)
        # On force la recherche sur ce seul axe de NIVEAU : le timbre est le bon
        # par construction, seul le niveau peut faire échouer.
        from analyzer.vsm_patch_optimizer import SearchParameter
        espace = [SearchParameter("oscillator.1.level", 0.0, 1.0)]
        sans = optimize_patch_for_machine(cible, 48, "vsm.minimoog", moteur, sample_rate=SR,
                                          space=espace, max_iterations=3, population=4,
                                          gate=0.75, fixed_parameters=muets, max_gain=None)
        avec = optimize_patch_for_machine(cible, 48, "vsm.minimoog", moteur, sample_rate=SR,
                                          space=espace, max_iterations=3, population=4,
                                          gate=0.75, fixed_parameters=muets,
                                          max_gain=VOLUME_MAX_POUR_TEST)
    assert_equal(sans.rejected_for_level, 0, "sans borne, rien n'est rejeté")
    assert_true(avec.rejected_for_level > 0,
                "avec borne, les niveaux quasi nuls de l'espace DOIVENT être rejetés")
    rms_cible = float(np.sqrt(np.mean(cible ** 2)))
    rms_avec = float(np.sqrt(np.mean(avec.audio ** 2)))
    assert_true(rms_cible / max(rms_avec, 1e-12) <= VOLUME_MAX_POUR_TEST,
                "le gagnant AVEC borne tient le niveau de la cible")


VOLUME_MAX_POUR_TEST = 10.0 / 0.9


@test
def reference_une_note_transcrite_dans_le_silence_n_est_pas_retenue():
    """Panne muette n° 6. Sur la basse de B4 Wuz Then, la note la plus longue
    (3,68 s) tombait dans un silence du stem — un artefact de transcription — et
    toute la recherche s'est faite contre rien. La référence doit SONNER."""
    from analyzer.vsm_reconstruct import _representative_note
    sr = SR
    audio = np.zeros(int(6.0 * sr), dtype=np.float32)
    # Une vraie note de 0,8 s entre 2,0 et 2,8 s ; le reste est silence.
    debut, fin = int(2.0 * sr), int(2.8 * sr)
    t = np.arange(fin - debut) / sr
    audio[debut:fin] = (0.3 * np.sin(2 * np.pi * 110 * t)).astype(np.float32)
    # La « longue » ne doit pas CHEVAUCHER la vraie note, sinon son segment
    # sonne par procuration — une première version du test faisait exactement
    # cette erreur, et le test échouait pour une raison qui n'était pas celle
    # qu'il voulait éprouver.
    notes = [StemNote(note=33, velocity=100, start=3.0, duration=2.9),   # « longue », dans le silence
             StemNote(note=45, velocity=100, start=2.0, duration=0.8)]   # courte, mais elle sonne
    ref, excerpt, gate = _representative_note(audio, notes, sr)
    assert_equal(ref.note, 45, "la note qui SONNE est retenue, pas la plus longue")
    assert_true(float(np.sqrt(np.mean(excerpt ** 2))) > 0.05, "l'extrait cible n'est pas du silence")

    # Et si TOUTES les notes tombent dans le silence, on ne fabrique pas une
    # référence : on garde l'ancien comportement (la plus longue) plutôt que de
    # rendre None — le stem a des notes, la chaîne le DIRA par la distance.
    muet = np.zeros(int(6.0 * sr), dtype=np.float32)
    ref2, _, _ = _representative_note(muet, notes, sr)
    assert_equal(ref2.note, 33, "sans aucune note sonore, repli sur la plus longue")


@test
def rapport_porte_sa_provenance():
    """A4.2 : un rapport qui ne dit pas avec quels modèles, quelles options et
    quel code il a été produit ne se rejoue pas -- et ne se compare à rien."""
    import json, tempfile
    from analyzer.vsm_reconstruct import StemReconstruction, write_reconstruction_report
    stem = StemReconstruction(name="essai", machine="vsm.minimoog", parameters={}, distance=0.1,
                              notes=[StemNote(60, 100, 0.0, 0.5)], considered=[("vsm.minimoog", 0.1)])
    prov = {"commit": "abc123", "options": {"budgetPiste": 40},
            "modeles": {"classifieurMachine": "aucun", "classifieurFrappes": "2026-08-23"}}
    with tempfile.TemporaryDirectory() as d:
        chemin = Path(d) / "rapport.json"
        write_reconstruction_report([stem], chemin, metric="v3", iterations=20, provenance=prov)
        r = json.loads(chemin.read_text(encoding="utf-8"))
    assert_equal(r["metric"], "v3", "la métrique est inscrite")
    assert_equal(r["provenance"]["commit"], "abc123", "le commit est inscrit")
    assert_equal(r["provenance"]["modeles"]["classifieurMachine"], "aucun",
                 "« aucun » est une information, pas une absence")
    # Sans provenance, le rapport reste lisible et ne porte pas la clé.
    with tempfile.TemporaryDirectory() as d:
        chemin = Path(d) / "rapport.json"
        write_reconstruction_report([stem], chemin, metric="v2")
        r = json.loads(chemin.read_text(encoding="utf-8"))
    assert_true("provenance" not in r, "pas de provenance inventée")
