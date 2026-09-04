"""Le banc synthétique tient ses promesses (docs/CDC-banc-synthetique.md § 4) :
même graine → même morceau au bit près ; les stems vrais sommés = le mélange
hors production ; les notes de la vérité sont celles des rôles ; les cas de
parité se déclarent ; un patch inaudible est rejeté et compté ; et le tableau
de bord se calcule sur le morceau minuscule commis, sans moteur pour les
étages 1 à 3, avec le moteur pour les bornes.

Ces tests rendent avec le moteur réel (vsm-render --serve), comme
test_batterie_melange : des morceaux d'une mesure, quelques dixièmes de
seconde chacun."""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, run, test  # noqa: E402

from analyzer import vsm_banc, vsm_morceaux  # noqa: E402
from analyzer.vsm_engine import VsmEngine  # noqa: E402
from analyzer.vsm_morceaux import Generateur, ecrire_morceau, lire_wav_float, morceau_complet  # noqa: E402

MACHINES = ["vsm.juno106", "vsm.tb303", "vsm.minimoog"]
FIXTURE = Path(__file__).resolve().parent / "donnees" / "banc-minuscule"


def _generateur(moteur: VsmEngine, **kw) -> Generateur:
    return Generateur(moteur, machines=MACHINES, journal=lambda ligne: None, **kw)


@test
def meme_graine_meme_morceau_au_bit_pres():
    with VsmEngine(sample_rate=44100) as moteur, tempfile.TemporaryDirectory() as d:
        g = _generateur(moteur)
        a = g.fabriquer(21, duree=1.0, nombre_de_parties=2, cas="aucun")
        b = g.fabriquer(21, duree=1.0, nombre_de_parties=2, cas="aucun")
        va, vb = dict(a[0]), dict(b[0])
        va.pop("cout"), vb.pop("cout")
        for p in va["parties"] + vb["parties"]:
            p.pop("cout_rendu_s")
        assert_equal(json.dumps(va, sort_keys=True), json.dumps(vb, sort_keys=True), "même vérité")
        assert_true(np.array_equal(a[2], b[2]), "même mélange au bit près")
        for sa, sb in zip(a[1], b[1]):
            assert_true(np.array_equal(sa, sb), "mêmes stems au bit près")
        # et les FICHIERS aussi : l'écrivain WAV n'horodate rien
        ecrire_morceau(Path(d) / "x", a[0], a[1], a[2])
        ecrire_morceau(Path(d) / "y", b[0], b[1], b[2])
        assert_equal((Path(d) / "x" / "morceau.wav").read_bytes(), (Path(d) / "y" / "morceau.wav").read_bytes(),
                     "mêmes octets de fichier")
        c = g.fabriquer(22, duree=1.0, nombre_de_parties=2, cas="aucun")
        assert_true(not np.array_equal(a[2], c[2]), "une autre graine donne un autre morceau")


@test
def les_stems_vrais_sommes_redonnent_le_melange_hors_production():
    with VsmEngine(sample_rate=44100) as moteur, tempfile.TemporaryDirectory() as d:
        g = _generateur(moteur)
        verite, stems, melange = g.fabriquer(31, duree=1.0, nombre_de_parties=3, cas="aucun")
        dossier = Path(d) / "m"
        ecrire_morceau(dossier, verite, stems, melange)
        assert_true(morceau_complet(dossier), "le morceau écrit est complet")
        mix = lire_wav_float(dossier / "morceau.wav")
        somme = np.zeros(mix.shape, dtype=np.float64)
        for partie in verite["parties"]:
            somme += lire_wav_float(dossier / partie["fichier"])
        assert_true(np.array_equal(somme.astype(np.float32), mix), "somme des stems = mélange, au bit près")
        assert_true(verite["melange_est_la_somme_des_stems"], "et la vérité le dit")
        assert_true(float(np.abs(mix).max()) <= vsm_morceaux.CRETE_MAXIMALE + 1e-6, "pas de crête au-delà du plafond")
        assert_equal(len(verite["parties"][0]["empreinte"]), 64, "empreinte SHA-256 par stem")


@test
def la_production_change_le_melange_et_la_verite_le_dit():
    with VsmEngine(sample_rate=44100) as moteur:
        g = _generateur(moteur)
        sec = g.fabriquer(31, duree=1.0, nombre_de_parties=3, cas="aucun", production=False)
        prod = g.fabriquer(31, duree=1.0, nombre_de_parties=3, cas="aucun", production=True)
        for a, b in zip(sec[1], prod[1]):
            assert_true(np.array_equal(a, b), "les stems vrais sont les mêmes : la production ne touche que le mélange")
        assert_true(not np.array_equal(sec[2], prod[2]), "le mélange produit diffère")
        assert_true(prod[0]["production"] is not None and not prod[0]["melange_est_la_somme_des_stems"],
                    "la vérité porte la production et dit que le mélange n'est plus la somme")
        assert_true(0.6 <= prod[0]["production"]["reverb_duree_s"] <= 1.2, "réverbération courte")
        assert_near(np.sqrt(np.mean(prod[2] ** 2)), np.sqrt(np.mean(sec[2] ** 2)), 1e-3, "gain de rattrapage : même RMS")


@test
def les_notes_suivent_les_registres_de_leur_role():
    with VsmEngine(sample_rate=44100) as moteur:
        g = _generateur(moteur)
        verite, _, _ = g.fabriquer(41, duree=2.0, nombre_de_parties=6, cas="aucun")
        roles = [p["role"] for p in verite["parties"]]
        assert_true("basse" in roles or "batterie" in roles, "au moins une basse ou une batterie")
        for partie in verite["parties"]:
            assert_true(len(partie["notes"]) > 0, f"{partie['role']} a des notes")
            if partie["role"] == "batterie":
                voix = set(vsm_morceaux.PIECES_PAR_MACHINE[partie["machine"]].values())
                assert_true(all(int(n[0]) in voix for n in partie["notes"]), "frappes sur des voix que la boîte a")
                assert_true(set(partie["pieces"]) >= {"kick", "snare", "hihat"}, "kick, caisse, charleston au moins")
                continue
            bas, haut = partie["registre"][0]
            for note, velocite, debut, duree in partie["notes"]:
                assert_true(bas <= note <= haut, f"{partie['role']} : {note} hors de [{bas}, {haut}]")
                assert_true(1 <= velocite <= 127 and duree > 0 and 0 <= debut < verite["duree"], "note bien formée")
            velocites = {int(n[1]) for n in partie["notes"]}
            assert_true(len(velocites) > 1 or len(partie["notes"]) < 3, "les vélocités varient")
        assert_true(verite["cout"]["total_s"] > 0 and all(p["cout_rendu_s"] >= 0 for p in verite["parties"]),
                    "le coût est publié")


@test
def les_trois_cas_de_parite_se_declarent():
    with VsmEngine(sample_rate=44100) as moteur:
        g = _generateur(moteur)
        v, _, _ = g.fabriquer(51, duree=1.0, nombre_de_parties=3, cas="deux-mains")
        mains = [p for p in v["parties"] if p["role"] == "piano-deux-mains"]
        assert_equal(len(mains), 1, "UNE partie deux-mains")
        (gb, gh), (db, dh) = mains[0]["registre"]
        assert_true(gh + 8 <= db, "les deux mains sont séparées par un vide")
        graves = [n[0] for n in mains[0]["notes"] if n[0] <= gh]
        aigus = [n[0] for n in mains[0]["notes"] if n[0] >= db]
        assert_true(graves and aigus and len(graves) + len(aigus) == len(mains[0]["notes"]), "chaque note dans une main")
        assert_equal(mains[0]["cas"], "deux-mains", "le cas est écrit sur la partie")

        v, _, _ = g.fabriquer(52, duree=1.0, nombre_de_parties=3, cas="memes-machine-disjoints")
        paire = [p for p in v["parties"] if p["cas"] == "memes-machine-disjoints"]
        assert_equal(len(paire), 2, "deux parties pour le cas")
        assert_equal(paire[0]["machine"], paire[1]["machine"], "même machine")
        assert_true(paire[0]["patch"] != paire[1]["patch"] or not paire[0]["patch"], "patchs différents")
        assert_true(paire[0]["registre"][0][1] + 8 <= paire[1]["registre"][0][0], "registres disjoints, vide ≥ 8")

        v, _, _ = g.fabriquer(53, duree=1.0, nombre_de_parties=3, cas="chevauchement")
        paire = [p for p in v["parties"] if p["cas"] == "chevauchement"]
        assert_equal(len(paire), 2, "deux parties pour le cas")
        (a0, a1), (b0, b1) = paire[0]["registre"][0], paire[1]["registre"][0]
        assert_true(min(a1, b1) - max(a0, b0) >= 12, "recouvrement d'au moins une octave")
        assert_equal(v["cas"], "chevauchement", "le cas est écrit sur le morceau")


@test
def un_patch_inaudible_est_rejete_retire_et_compte():
    with VsmEngine(sample_rate=44100) as moteur:
        appels = {"n": 0}

        def rendre_muet_une_fois(machine, patch, notes, duree):
            appels["n"] += 1
            if appels["n"] == 1:
                return np.zeros(int(duree * 44100), dtype=np.float32)
            return moteur.render(machine, patch, notes, duration=duree, sample_rate=44100)

        g = _generateur(moteur, rendre=rendre_muet_une_fois)
        verite, _, _ = g.fabriquer(61, duree=1.0, nombre_de_parties=2, cas="aucun")
        assert_equal(verite["parties"][0]["patchs_rejetes"], 1, "le premier patch, muet, est compté rejeté")
        assert_true(verite["parties"][0]["patch"] or "usine" in verite["parties"][0]["origine_patch"],
                    "un autre patch a été tiré")


@test
def un_morceau_incomplet_n_est_pas_complet():
    with tempfile.TemporaryDirectory() as d:
        dossier = Path(d) / "m"
        dossier.mkdir()
        assert_true(not morceau_complet(dossier), "sans vérité : incomplet")
        (dossier / "verite.json").write_text('{"format": "vsm-morceau-synthetique", "parties": [{"fichier": "x.wav"}]}')
        assert_true(not morceau_complet(dossier), "vérité sans ses fichiers : incomplet")
    assert_true(morceau_complet(FIXTURE / "morceau"), "le morceau minuscule du dépôt est complet")


@test
def l_appariement_respecte_ses_tolerances():
    vraies = [[60, 100, 0.0, 0.5], [64, 100, 0.5, 0.5], [67, 100, 1.0, 0.5]]
    transcrites = [[60, 90, 0.03, 0.4], [65, 90, 0.52, 0.4], [67, 90, 1.2, 0.4], [72, 90, 2.0, 0.4]]
    paires = vsm_banc.apparier(vraies, transcrites)
    assert_equal(sorted(paires), [(0, 0), (1, 1)], "±50 ms et ±1 demi-ton : deux paires, la troisième est à 200 ms")
    assert_equal(vsm_banc.apparier(vraies, transcrites, tolerance_hauteur=0), [(0, 0)], "hauteur exacte : une seule")
    assert_equal(len(vsm_banc.apparier(vraies, transcrites, sans_hauteur=True)), 2, "sans hauteur : l'attaque décide")


@test
def le_tableau_de_bord_se_calcule_sur_le_morceau_minuscule_sans_moteur():
    mesure = vsm_banc.mesurer_morceau(FIXTURE / "morceau", FIXTURE / "course", None, None,
                                      stems_vrais_fournis=True, journal=lambda ligne: None)
    assert_true(not mesure["separation"]["mesure"] and "stems vrais" in mesure["separation"]["raison"],
                "séparation non mesurée, et dit pourquoi")
    t = mesure["transcription"]
    assert_equal(t["melodique"]["vraies"], 9, "neuf notes mélodiques vraies (4 de basse, 5 de mélodie)")
    assert_near(t["melodique"]["f1"], 1.0, 1e-9, "la chaîne a transcrit les neuf notes à ±1 demi-ton")
    assert_true(t["velocite_erreur_absolue_moyenne"] > 5, "la vélocité écrite par la chaîne n'est pas celle jouée")
    assert_true(0 < t["frappes"]["f1"] < 1, "frappes : une partie seulement (deux pièces sur cinq)")
    p = mesure["parite"]
    assert_equal(p["parties_melodiques"], 2, "deux parties mélodiques")
    assert_equal(p["pieces_de_batterie"], 5, "cinq pièces frappées")
    assert_equal(p["pistes_obtenues"], 4, "quatre pistes jouées (bass, other, deux Batterie)")
    assert_equal(p["fondues_batterie"], 3, "trois pièces sans piste propre")
    assert_equal(p["inventees"], 0, "rien d'inventé")
    assert_equal(p["attribution"], {"bass": 0, "other": 2}, "bass → partie 1 (basse), other → partie 3 (mélodie)")
    assert_true(not mesure["arbitrage"]["mesure"], "sans moteur, pas de bornes")
    assert_near(mesure["global"]["borne_production"], 0.0, 1e-9, "sans production, la somme des stems EST le mélange")
    assert_near(mesure["global"]["global"], 0.2345, 1e-3, "la distance de la course commise")


@test
def le_tableau_de_bord_calcule_les_bornes_avec_le_moteur():
    with VsmEngine(sample_rate=44100) as moteur:
        mesure = vsm_banc.mesurer_morceau(FIXTURE / "morceau", FIXTURE / "course", None, moteur,
                                          stems_vrais_fournis=True, journal=lambda ligne: None)
    a = mesure["arbitrage"]
    assert_true(a["mesure"], "arbitrage mesuré")
    par_piste = {e["piste"]: e for e in a["pistes"]}
    assert_equal(par_piste["bass"]["rang"], 2, "la TB-303 vraie est deuxième sur la basse dans la course commise")
    assert_equal(par_piste["other"]["rang"], 1, "et première sur la mélodie")
    assert_true(par_piste["other"]["borne_piste"] < par_piste["other"]["distance_chaine"],
                "la vraie machine au vrai patch fait mieux que la chaîne sur other")
    assert_equal(len([e for e in a["pistes"] if e["piste"].startswith("Batterie")]), 1, "la batterie : UNE entrée")
    assert_equal(a["pistes_jugees"], 3, "trois pistes jugées")
    g = mesure["global"]
    assert_true(g["borne_transcription"] > 0.0, "borne de transcription strictement positive : il manque trois pièces")
    assert_true(g["perte_transcription_parite"] > 0 and g["perte_arbitrage_reglage_calage"] is not None,
                "les deux pertes sont publiées")
    agregat = vsm_banc.agreger([mesure])
    texte = vsm_banc.tableau([mesure], agregat)
    assert_true("fondues : batterie" in texte and "non mesuré" in texte, "le tableau dit ce qui manque")
    assert_equal(agregat["arbitrage"]["top_6"], 3, "top 6 agrégé")


if __name__ == "__main__":
    raise SystemExit(run())
