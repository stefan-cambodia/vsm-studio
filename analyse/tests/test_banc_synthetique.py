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
        for sa, sb in zip(a[1], b[1], strict=True):
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
        for a, b in zip(sec[1], prod[1], strict=True):
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
        (_gb, gh), (db, _dh) = mains[0]["registre"]
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


# ---------------------------------------------------------------------------
# B5 / § 7 bis : les trois exigences du corpus suivant
# ---------------------------------------------------------------------------

def _partie_d_essai(role: str, notes) -> vsm_morceaux.Partie:
    return vsm_morceaux.Partie(role=role, machine="vsm.juno106", patch={}, vecteur=[], notes=notes,
                               registre=[[48, 72]], niveau_rms=0.05, niveau_db=-26.0, gain=1.0,
                               pan=0.0, gate=0.85)


@test
def l_arrangement_fait_entrer_et_sortir_les_parties():
    """Exigence 1 : deux parties au moins ne sonnent pas d'un bout à l'autre,
    aucune section n'est muette, et les notes sont VRAIMENT filtrées."""
    rng = np.random.default_rng(7)
    s = vsm_morceaux.Structure(rng, 240.0)
    sections = vsm_morceaux.sections_du_morceau(s)
    assert_true(len(sections) >= 4, f"un morceau de {s.duree:.0f} s fait {len(sections)} sections")
    parties = [_partie_d_essai(role, vsm_morceaux.notes_nappe(rng, s, (48, 72), 0.9))
               for role in ("basse", "batterie", "melodie", "nappe", "accompagnement")]
    avant = [len(p.notes) for p in parties]
    arrangement = vsm_morceaux.arranger(rng, s, parties, lambda ligne: None)
    assert_true(arrangement is not None, "l'arrangement est posé")
    nb = len(arrangement["sections"])
    partielles = [p for p in parties if p.sections is not None and len(p.sections) < nb]
    assert_true(len(partielles) >= 2, f"au moins deux parties partielles, il y en a {len(partielles)}")
    for k in range(nb):
        assert_true(any(k in (p.sections or []) for p in parties), f"la section {k} n'est pas muette")
    for partie, combien in zip(parties, avant, strict=True):
        assert_true(len(partie.notes) > 0, f"{partie.role} garde des notes")
        if len(partie.sections or []) < nb:
            assert_true(len(partie.notes) < combien, f"{partie.role} : les notes hors sections sont parties")
            fenetres = [(arrangement["sections"][k]["debut"], arrangement["sections"][k]["fin"])
                        for k in (partie.sections or [])]
            assert_true(all(any(a - 1e-9 <= n[2] < b for a, b in fenetres) for n in partie.notes),
                        f"{partie.role} : aucune note hors de ses sections")
        else:
            assert_equal(len(partie.notes), combien, f"{partie.role} sonne partout : rien n'est filtré")


@test
def un_morceau_trop_court_dit_que_l_arrangement_est_impossible():
    rng = np.random.default_rng(3)
    s = vsm_morceaux.Structure(rng, 2.0)
    dit: list[str] = []
    parties = [_partie_d_essai("melodie", [[60, 100, 0.0, 0.5]])]
    assert_true(vsm_morceaux.arranger(rng, s, parties, dit.append) is None, "pas d'arrangement")
    assert_true(any("impossible" in ligne for ligne in dit), "et il est DIT, pas tu")
    assert_true(parties[0].sections is None, "la partie n'a pas de sections")


@test
def le_phrase_bref_donne_des_notes_sous_120_ms():
    """Exigence 2 : des frappes courtes sur la grille de double croche, à la
    hauteur et à la place du phrasé d'origine."""
    rng = np.random.default_rng(11)
    s = vsm_morceaux.Structure(rng, 30.0)
    notes = vsm_morceaux.notes_melodie(rng, s, (60, 84), 0.85)
    brefs = vsm_morceaux.raccourcir_phrase(rng, s, notes, 0.070)
    assert_true(len(brefs) >= len(notes), "une note longue donne une à quatre frappes")
    assert_true(all(abs(n[3] - 0.070) < 1e-9 for n in brefs), "toutes les frappes durent 70 ms")
    assert_true(all(n[3] < 0.120 for n in brefs), "toutes sous le seuil du cahier des charges")
    assert_true(set(int(n[0]) for n in brefs) == set(int(n[0]) for n in notes), "mêmes hauteurs")
    assert_true(all(1 <= n[1] <= 127 for n in brefs), "vélocités bien formées")
    pas = s.battement / 4
    assert_true(all(min(abs((n[2] - m[2]) % pas), pas - abs((n[2] - m[2]) % pas)) < 1e-6
                    for n, m in zip(brefs, brefs[1:], strict=False)), "sur la grille de double croche")


@test
def le_defaut_ne_porte_aucune_des_trois_exigences():
    """Le corpus d'avant : aucune section, aucun phrasé bref, aucun profil — et
    la vérité le DIT, pour que deux lots ne se comparent qu'à options égales."""
    with VsmEngine(sample_rate=44100) as moteur:
        verite, _, _ = _generateur(moteur).fabriquer(21, duree=1.0, nombre_de_parties=2, cas="aucun")
    assert_equal(verite["exigences"], {"arrangement": False, "notes_breves": False,
                                       "echantillons": False, "borne_hauteur": 0.0},
                 "les trois options sont dans la provenance, à faux")
    assert_true(verite["arrangement"] is None, "aucun arrangement")
    for partie in verite["parties"]:
        assert_true(partie["sections"] is None and partie["phrase_breve"] is None, "aucune exigence posée")
        assert_equal(partie["profil"], "", "aucun profil")


@test
def un_profil_refuse_par_le_moteur_est_dit_et_remplace():
    """Exigence 3 : `vsm.multisample` refuse un profil au-delà de son budget
    mémoire. Le tirage ne doit pas insister sur un profil refusé, et le refus
    ne doit pas être tu — c'est ce qui a coûté huit rendus par partie perdue."""
    class MoteurFactice:
        def profiles(self):
            return [{"name": "Essai-Grand-Piano", "path": "/tmp/gros.profile.json"},
                    {"name": "Essai-E-Piano", "path": "/tmp/petit.profile.json"}]

    refuses = []

    def rendre(machine, patch, notes, duree, profil=None):
        if profil == "/tmp/gros.profile.json":
            refuses.append(profil)
            raise vsm_morceaux.VsmEngineError("profil refusé : profil au-delà du budget mémoire de 256 Mo")
        return np.full(int(duree * vsm_morceaux.SR), 0.1, dtype=np.float32)

    generateur = Generateur(MoteurFactice(), machines=MACHINES, rendre=rendre,  # type: ignore[arg-type]
                            journal=lambda ligne: None)
    rng = np.random.default_rng(1)
    choisis = [generateur.profil_pour(rng, "accompagnement").get("nom") for _ in range(6)]
    assert_true(all(nom == "Essai-E-Piano" for nom in choisis),
                f"seul le profil accepté est retenu, six fois : {set(choisis)}")
    assert_equal(len(refuses), 1, "le gros profil n'est éprouvé QU'UNE fois : le verdict est mémorisé")
    assert_true("budget mémoire" in generateur.profil_refuse("/tmp/gros.profile.json"),
                "et la raison du moteur est gardée telle quelle")


@test
def les_trois_exigences_traversent_un_vrai_morceau():
    """De bout en bout, avec le moteur : les sections filtrent, le phrasé bref
    donne des notes sous 120 ms, et le CALAGE DE NIVEAU se fait sur ce qui
    sonne — sans quoi l'arrangement changerait le mixage."""
    with VsmEngine(sample_rate=44100) as moteur:
        generateur = _generateur(moteur, arrangement=True, notes_breves=True, borne_hauteur=2.0)
        verite, stems, _ = generateur.fabriquer(5, duree=90.0, nombre_de_parties=4, cas="aucun")
    assert_equal(verite["exigences"]["arrangement"], True, "la provenance dit l'arrangement")
    arrangement = verite["arrangement"]
    assert_true(arrangement is not None and len(arrangement["sections"]) >= 2, "au moins deux sections")
    nb = len(arrangement["sections"])
    partielles = [p for p in verite["parties"] if len(p["sections"]) < nb]
    assert_true(len(partielles) >= 2, f"deux parties au moins entrent ou sortent ({len(partielles)})")
    brefs = [p for p in verite["parties"]
             if p["role"] != "batterie" and any(n[3] < 0.120 for n in p["notes"])]
    assert_true(brefs, "au moins une partie mélodique porte des notes sous 120 ms")
    for partie in brefs:
        assert_true(partie["phrase_breve"] is not None, "et la vérité le dit")
    # Le niveau : mesuré sur les seules sections où la partie joue. Le mono se
    # REFAIT en inversant la loi de panoramique, qui est à puissance constante :
    # sommer les deux canaux ajouterait jusqu'à √2 au centre, et le premier
    # essai de ce test accusait le calage d'un facteur 1,41 qui était le sien.
    import math as _math
    for partie, stem in zip(verite["parties"], stems, strict=True):
        if len(partie["sections"]) >= nb:
            continue
        fenetres = [(arrangement["sections"][k]["debut"], arrangement["sections"][k]["fin"])
                    for k in partie["sections"]]
        theta = (float(partie["pan"]) + 1.0) * _math.pi / 4.0
        mono = stem[:, 0] * _math.cos(theta) + stem[:, 1] * _math.sin(theta)
        attendu = partie["niveau_rms"] * verite["gain_crete"]
        assert_near(vsm_morceaux._rms_fenetres(mono, fenetres), attendu, attendu * 0.05,
                    f"{partie['role']} : calée à son niveau LÀ OÙ ELLE JOUE")


@test
def la_hauteur_sonnante_se_lit_et_dit_quand_elle_ne_lit_rien():
    """La mesure qui décide du rejet : juste sur un son périodique, None sur du
    bruit et EN BUTÉE de sa fenêtre — jamais un chiffre tiré de sa propre borne."""
    sr = vsm_morceaux.SR
    t = np.arange(int(0.8 * sr)) / sr
    la440 = np.sin(2 * np.pi * 440.0 * t).astype(np.float32)
    assert_near(vsm_morceaux.hauteur_sonnante(la440), 69.0, 0.1, "un la 440 est lu comme la note 69")
    do = np.sin(2 * np.pi * 261.63 * t).astype(np.float32)
    assert_near(vsm_morceaux.hauteur_sonnante(do), 60.0, 0.1, "un do 3 est lu comme la note 60")
    bruit = np.zeros(int(0.8 * sr), dtype=np.float32)
    assert_true(vsm_morceaux.hauteur_sonnante(bruit) is None, "le silence n'a pas de hauteur")
    # 20 Hz : sous la fenêtre de recherche. La corrélation y est quasi parfaite
    # dès le plus petit décalage, si bien que le maximum tombe EN BUTÉE — et
    # c'est ce cas-là qui faisait lire six machines à « +35,25 demi-tons »,
    # c'est-à-dire la borne elle-même écrite comme un résultat.
    grave = np.sin(2 * np.pi * 20.0 * t).astype(np.float32)
    assert_true(vsm_morceaux.hauteur_sonnante(grave) is None,
                "une hauteur sous la fenêtre est illisible, et non lue comme la borne")
    # Dans la fenêtre, en revanche, l'aigu se lit (4 kHz ≈ note 107).
    assert_near(vsm_morceaux.hauteur_sonnante(np.sin(2 * np.pi * 4000.0 * t).astype(np.float32)),
                107.2, 0.3, "un aigu dans la fenêtre se lit")
    assert_near(vsm_morceaux.hors_octave(12.3), 0.3, 1e-9, "l'octave est retirée")
    assert_near(vsm_morceaux.hors_octave(-24.5), -0.5, 1e-9, "deux octaves aussi")
    assert_near(vsm_morceaux.hors_octave(-3.8), -3.8, 1e-9, "et ce qui n'est pas une octave reste")


@test
def un_patch_qui_sonne_a_cote_est_retire_et_dit():
    """Le second volet de l'exigence 4 : la dérive est LUE sur la sonde, donc
    vue quelle qu'en soit la cause — y compris un réglage que le moteur ne
    déclare pas en demi-tons (tension de corde, ratio d'opérateur FM)."""
    sr = vsm_morceaux.SR

    def ton(frequence, duree):
        n = int(duree * sr)
        return (0.3 * np.sin(2 * np.pi * frequence * np.arange(n) / sr)).astype(np.float32)

    faux = {"n": 0}

    def rendre(machine, patch, notes, duree, profil=None):
        faux["n"] += 1
        # Les trois premiers patchs sonnent une quarte trop haut, puis juste.
        decalage = 5.0 if faux["n"] <= 3 else 0.0
        return ton(440.0 * 2 ** ((60 + decalage - 69) / 12.0), duree)

    class MoteurFactice:
        def profiles(self):
            return []

    with VsmEngine(sample_rate=sr) as moteur:
        espace = moteur.search_profile("vsm.juno106")
    generateur = Generateur(MoteurFactice(), machines=["vsm.juno106"], rendre=rendre,  # type: ignore[arg-type]
                            borne_hauteur=2.0, journal=lambda ligne: None)
    generateur._espaces["vsm.juno106"] = espace
    _patch, _vecteur, rejets, origine = generateur.tirer_patch(np.random.default_rng(2), "vsm.juno106", 60)
    assert_equal(rejets, 3, "les trois patchs faux sont retirés")
    assert_true("SearchProfile" in origine, "et le quatrième est gardé")
    assert_near(generateur._derive_sonde, 0.0, 0.1, "la dérive retenue est nulle")


@test
def les_machines_qui_ne_sonnent_pas_juste_sortent_du_vivier():
    """Mesurées le 19/09 : quatre machines ne sonnent pas à la note qu'on leur
    joue DÈS le patch d'usine. Elles restent au parc et au DAW ; c'est le vivier
    mélodique du BANC qui les écarte, parce que sa vérité compare la hauteur
    écrite à la hauteur entendue."""
    with VsmEngine(sample_rate=44100) as moteur:
        vivier = vsm_morceaux.machines_melodiques_du_banc(moteur)
    assert_true(len(vsm_morceaux.MACHINES_SANS_HAUTEUR_JUSTE) >= 4, "quatre au moins sont déclarées")
    for machine, raison in vsm_morceaux.MACHINES_SANS_HAUTEUR_JUSTE.items():
        assert_true(machine not in vivier, f"{machine} hors du vivier mélodique du banc")
        assert_true(len(raison) > 20, f"{machine} : la raison est écrite, pas sous-entendue")
    assert_true("vsm.piano" in vivier and "vsm.juno106" in vivier, "le vivier garde ce qui sonne juste")


if __name__ == "__main__":
    raise SystemExit(run())
