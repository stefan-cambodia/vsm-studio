"""La boucle résiduelle DANS la chaîne, sur le morceau minuscule commis
(docs/CDC-separation-par-synthese.md § 4) :

  - identité sans option : deux courses, l'une sans --residuel et l'autre avec
    --residuel 0, rendent project.json, le MIDI et rapport.json identiques
    octet pour octet, et la boucle n'est jamais appelée ; et la course sans
    option redonne la course commise le 04/09 (d'avant le chantier) — la
    chaîne d'aujourd'hui est la chaîne d'hier, à ceci près que les nombres
    qui SORTENT DU MOTEUR ne se comparent octet pour octet que si le moteur
    est le même binaire (voir `TOLERANCE_MOTEUR`) ;
  - la boucle de bout en bout (--residuel 1, séparateur du résidu injecté) :
    une soustraction dite avec ses chiffres, un résidu écrit, les doublons
    refusés avant l'arbitrage avec leurs trois nombres, une piste « · r1 »
    sur les notes nouvelles, le bloc `residuel` au rapport avec son motif
    d'arrêt, la provenance ;
  - déterminisme : deux exécutions rendent le même bloc à la 4e décimale ;
  - le banc lit la boucle : SDR du résidu vrai avant/après, séparation du
    résidu, parité à k, distance à k.

Ces tests font tourner la chaîne entière EN PROCESSUS, avec le vrai moteur
et Basic Pitch, sur trois secondes : une dizaine de secondes par course.
"""
from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, run, test  # noqa: E402

import reconstruire  # noqa: E402
from analyzer import vsm_banc  # noqa: E402
from analyzer.vsm_morceaux import ecrire_wav_float, stems_attendus  # noqa: E402

FIXTURE = Path(__file__).resolve().parent / "donnees" / "banc-minuscule"
# CE QUE LA COURSE COMMISE PEUT PROUVER, ET CE QU'ELLE NE PEUT PAS.
#
# `course/project.json` porte des nombres qui sortent du MOTEUR — un binaire
# qui ne vit pas dans le dépôt et que chaque `cmake --build` refait. Les exiger
# octet pour octet, c'est demander au test de tomber à la première compilation
# du moteur, et de dire « la chaîne a changé » quand ce qui a changé est le
# moteur. C'est arrivé : la référence a été enregistrée avec le moteur du
# 04/09/2026 15:37:58 (162 513 544 octets) ; celui du 09/09 17:05:01
# (166 864 936 octets, mêmes 64 machines) rejoue la MÊME course et donne
#
#   - le même arrangement.mid, octet pour octet ;
#   - les mêmes patchs : dans rapport.json, aucun champ des deux stems ne bouge ;
#   - la même automation, au dernier chiffre ;
#   - les mêmes volumes au dernier bit pour bass et les deux pièces de batterie ;
#   - et DEUX nombres qui bougent : le volume de « other », la seule piste
#     automatisée (2,597493237337678 contre 2,5974931425397476, soit 3,65e-08
#     en relatif) et la distance globale (7,41e-08).
#
# Les deux tiennent sous la résolution du float32 dans lequel le moteur rend
# son audio (eps = 1,19e-07) : c'est le dernier bit d'un rendu, pas une
# décision. Aucun verdict, aucune machine, aucune note ne change.
#
# Le test garde donc deux exigences SÉPARÉES plutôt qu'une seule affaiblie :
#   - ce qui ne passe pas par le moteur — structure, machines, presets, ticks,
#     et le MIDI entier — se compare à l'identique, sans tolérance ;
#   - les FLOTTANTS se comparent au millionième relatif, et l'écart absorbé est
#     IMPRIMÉ, jamais avalé (une tolérance muette est une panne muette).
# Et si le moteur qui tourne EST celui de la référence (même date, même
# taille), l'exigence redevient l'égalité octet pour octet : la relaxation ne
# vaut que là où elle est nécessaire.
#
# POURQUOI 1e-06 : quinze fois le plus grand écart observé, et cent fois plus
# serré que la 4e décimale à laquelle la chaîne publie et compare ses
# distances. Un écart qui changerait une décision est très au-dessus.
TOLERANCE_MOTEUR = 1e-6
SEPARATEUR_FACTICE = Path(__file__).resolve().parent / "donnees" / "separateur_factice.py"
# Les options de la course commise (rapport.json de la fixture, provenance).
OPTIONS_DE_LA_COURSE = ["--machines", "vsm.juno106,vsm.tb303,vsm.minimoog", "--budget-piste", "4",
                        "--axes-piste", "2", "--rendus-paralleles", "2", "--machines-au-melange", "2",
                        "--tours-verdict", "1", "--second-verdict", "0"]


def _stems_vrais_groupes(dossier: Path) -> Path:
    groupes = dossier / "stems-vrais-groupes"
    groupes.mkdir(parents=True, exist_ok=True)
    verite = json.loads((FIXTURE / "morceau" / "verite.json").read_text(encoding="utf-8"))
    for nom, audio in stems_attendus(verite, FIXTURE / "morceau").items():
        ecrire_wav_float(groupes / f"{nom}.wav", audio)
    return groupes


def _courir(dossier: Path, nom: str, options: list) -> tuple:
    """La chaîne en processus ; rend (sortie, travail, journal)."""
    sortie, travail = dossier / nom, dossier / f"{nom}-travail"
    args = reconstruire.construire_parseur().parse_args(
        [str(FIXTURE / "morceau" / "morceau.wav"), "--sortie", str(sortie),
         "--stems", str(_stems_vrais_groupes(dossier)), "--garder-stems", str(travail)]
        + OPTIONS_DE_LA_COURSE + options)
    tampon = io.StringIO()
    with contextlib.redirect_stdout(tampon):
        reconstruire.chaine(args)
    return sortie, travail, tampon.getvalue()


def _differences(obtenu, attendu, tolerance: float, chemin: str = "") -> tuple:
    """Compare deux documents JSON : tout à l'identique, sauf les FLOTTANTS,
    tolérés sous `tolerance` en écart RELATIF.

    Rend `(dures, absorbees)` : ce qui diffère vraiment, et les écarts
    flottants passés sous la tolérance, avec leur chemin. La seconde liste
    existe pour être IMPRIMÉE — un test qui tolère en silence ne dit plus ce
    qu'il a vu.
    """
    dures: list = []
    absorbees: list = []
    if isinstance(attendu, dict) and isinstance(obtenu, dict):
        if set(obtenu) != set(attendu):
            return [f"{chemin or '/'} : clés {sorted(obtenu)} contre {sorted(attendu)}"], absorbees
        for cle in attendu:
            d, a = _differences(obtenu[cle], attendu[cle], tolerance, f"{chemin}/{cle}")
            dures += d
            absorbees += a
    elif isinstance(attendu, list) and isinstance(obtenu, list):
        if len(obtenu) != len(attendu):
            return [f"{chemin} : {len(obtenu)} élément(s) contre {len(attendu)}"], absorbees
        for i, (o, att) in enumerate(zip(obtenu, attendu, strict=True)):   # les longueurs viennent d'être vérifiées
            d, a = _differences(o, att, tolerance, f"{chemin}[{i}]")
            dures += d
            absorbees += a
    elif isinstance(attendu, float) and isinstance(obtenu, (int, float)) and not isinstance(obtenu, bool):
        ecart = abs(float(obtenu) - attendu) / max(abs(attendu), 1e-12)
        if ecart > tolerance:
            dures.append(f"{chemin} : {obtenu!r} contre {attendu!r} ({ecart:.2e} en relatif)")
        elif ecart > 0.0:
            absorbees.append(f"{chemin} {ecart:.2e}")
    elif obtenu != attendu:
        dures.append(f"{chemin} : {obtenu!r} contre {attendu!r}")
    return dures, absorbees


def _sans_provenance(rapport: dict) -> dict:
    copie = dict(rapport)
    copie.pop("provenance", None)
    return copie


@test
def identite_sans_option_et_la_boucle_n_est_jamais_appelee():
    originale = reconstruire.boucle_residuelle_de_la_chaine

    def interdite(*_a, **_k):
        raise AssertionError("la boucle a été appelée sans --residuel")

    reconstruire.boucle_residuelle_de_la_chaine = interdite
    try:
        with tempfile.TemporaryDirectory() as d:
            a, _, journal_a = _courir(Path(d), "a", [])
            b, _, journal_b = _courir(Path(d), "b", ["--residuel", "0"])
            for fichier in ("project.json", "midi/arrangement.mid"):
                assert_equal((a / fichier).read_bytes(), (b / fichier).read_bytes(),
                             f"{fichier} identique octet pour octet")
            ra = json.loads((a / "rapport.json").read_text(encoding="utf-8"))
            rb = json.loads((b / "rapport.json").read_text(encoding="utf-8"))
            assert_equal(_sans_provenance(ra), _sans_provenance(rb), "rapport identique hors provenance")
            assert_equal(ra["provenance"]["options"]["residuel"], 0, "la provenance porte l'option à 0")
            assert_true("residuel" not in ra, "pas de bloc residuel sans l'option")
            assert_true("résiduel" not in journal_a and "résiduel" not in journal_b,
                        "et le journal n'en parle pas")
            # ET LA CHAÎNE D'HIER : la course commise le 04/09, d'avant le
            # chantier, aux mêmes options et sur les mêmes stems.
            commis = json.loads((FIXTURE / "course" / "rapport.json").read_text(encoding="utf-8"))
            # LE MIDI NE TRAVERSE PAS LE MOTEUR AUDIO : il se compare octet
            # pour octet quel que soit le binaire qui a rendu le son.
            assert_equal((a / "midi" / "arrangement.mid").read_bytes(),
                         (FIXTURE / "course" / "midi" / "arrangement.mid").read_bytes(),
                         "arrangement.mid de la course commise, octet pour octet")
            hier = commis["provenance"]["moteur"]
            aujourdhui = ra["provenance"]["moteur"]
            meme_moteur = (hier.get("compile") == aujourdhui.get("compile")
                           and hier.get("octets") == aujourdhui.get("octets"))
            if meme_moteur:
                # Le binaire de la référence est celui qui tourne : plus rien
                # ne justifie une tolérance.
                assert_equal((a / "project.json").read_bytes(),
                             (FIXTURE / "course" / "project.json").read_bytes(),
                             "project.json de la course commise, octet pour octet (même moteur)")
                assert_near(ra["globalDistance"], commis["globalDistance"], 1e-12,
                            "même distance globale (même moteur)")
            else:
                print(f"      moteur de la référence : {hier.get('compile')} "
                      f"({hier.get('octets')} octets) ; moteur d'aujourd'hui : "
                      f"{aujourdhui.get('compile')} ({aujourdhui.get('octets')} octets)")
                dures, absorbees = _differences(
                    json.loads((a / "project.json").read_text(encoding="utf-8")),
                    json.loads((FIXTURE / "course" / "project.json").read_text(encoding="utf-8")),
                    TOLERANCE_MOTEUR)
                print(f"      project.json : {len(absorbees)} nombre(s) sous "
                      f"{TOLERANCE_MOTEUR:.0e} — {', '.join(absorbees) if absorbees else 'aucun'}")
                assert_equal(dures, [], "project.json de la course commise, aux nombres du moteur près")
                ecart = abs(ra["globalDistance"] - commis["globalDistance"]) / abs(commis["globalDistance"])
                print(f"      distance globale : {ra['globalDistance']!r} contre "
                      f"{commis['globalDistance']!r} ({ecart:.2e} en relatif)")
                assert_true(ecart <= TOLERANCE_MOTEUR,
                            f"distance globale à {TOLERANCE_MOTEUR:.0e} près : écart {ecart:.2e}")
    finally:
        reconstruire.boucle_residuelle_de_la_chaine = originale


def _options_boucle(notes_min: int = 4) -> list:
    return ["--residuel", "1", "--residuel-correlation", "0", "--residuel-notes-min", str(notes_min),
            "--residuel-separateur", f"{sys.executable} {SEPARATEUR_FACTICE}"]


@test
def la_boucle_tourne_de_bout_en_bout_et_le_banc_la_lit():
    with tempfile.TemporaryDirectory() as d:
        sortie, travail, journal = _courir(Path(d), "r1", _options_boucle(notes_min=4))
        rapport = json.loads((sortie / "rapport.json").read_text(encoding="utf-8"))
        projet = json.loads((sortie / "project.json").read_text(encoding="utf-8"))
        assert_equal(rapport["provenance"]["options"]["residuel"], 1, "provenance : residuel 1")
        assert_equal(rapport["provenance"]["options"]["residuelNotesMin"], 4, "provenance : le seuil de notes")
        assert_true(rapport["provenance"]["options"]["residuelSeparateur"], "provenance : le séparateur injecté")
        bloc = rapport["residuel"]
        assert_equal(bloc["demande"], 1, "une itération demandée")
        assert_equal(len(bloc["iterations"]), 1, "une itération faite")
        it = bloc["iterations"][0]
        assert_equal(len(it["candidats"]), 3, "trois unités rendues et publiées : Batterie, bass, other")
        for c in it["candidats"]:
            assert_true("correlationStem" in c and "gain" in c and "decalageEchantillons" in c,
                        "chaque candidate porte ses chiffres : " + str(c))
        assert_equal(it["soustraction"]["unite"], "Batterie", "la plus sûre : 67 % / 0,27, le plus grand score")
        assert_equal(sorted(it["soustraction"]["membres"]), ["Batterie · hihat", "Batterie · kick"],
                     "le groupe entier, jamais une pièce seule")
        assert_true(0 < it["energie"]["partApres"] < it["energie"]["partAvant"] <= 100.0, "le résidu a maigri : " + str(it["energie"]))
        assert_true(Path(it["residu"]).exists(), "le résidu est sur disque")
        assert_true(Path(it["residu"]).is_relative_to(travail), "dans le dossier de travail, pas dans le projet")
        assert_true(not (sortie / "residu-r1").exists() and not list(sortie.glob("residu*")),
                    "rien du résidu dans le dossier du projet")
        assert_true("other" in it["stems"], "le séparateur factice a rendu other")
        assert_equal([p["piste"] for p in it["pistesAjoutees"]], ["other · r1"],
                     "une piste sur les notes nouvelles, nommée par son itération")
        ajoutee = it["pistesAjoutees"][0]
        assert_true(ajoutee["nouvelles"] >= 4 and ajoutee["dejaPortees"] > 0
                    and ajoutee["transcrites"] == ajoutee["nouvelles"] + ajoutee["dejaPortees"],
                    "les trois nombres du filtre : " + str(ajoutee))
        assert_true(bloc["arret"]["motif"] in ("distance-sans-gain", "iterations-atteintes"),
                    "un motif d'arrêt nommé : " + str(bloc["arret"]))
        assert_true("distanceProjet" in it and it["distanceProjet"]["avant"] > 0, "la distance en l'état, avant et après")
        noms = [t["name"] for t in projet["tracks"]]
        assert_true("other · r1" in noms, "la piste du résidu est dans le projet : " + str(noms))
        assert_true(all(n in noms for n in ("bass", "other", "Batterie · hihat", "Batterie · kick")),
                    "et les pistes d'avant sont toutes là, gelées")
        assert_true(any(s["name"] == "other · r1" for s in rapport["stems"]), "et dans les stems du rapport")
        # Le journal dit tout : candidates, soustraction, filtre, arrêt.
        assert_true("SOUSTRAIT « Batterie »" in journal, "la soustraction est imprimée avec ses chiffres")
        assert_true("corr. stem" in journal and "décalage" in journal, "chaque candidate avec sa corrélation")
        assert_true("NOUVELLES gardées" in journal, "le filtre dit ce qu'il garde")
        assert_true("ARRÊT" in journal, "l'arrêt est imprimé")

        # LE BANC LIT LA BOUCLE.
        mesure = vsm_banc.mesurer_morceau(FIXTURE / "morceau", sortie, travail, None,
                                          stems_vrais_fournis=False, journal=lambda ligne: None)
        res = mesure["residuel"]
        assert_true(res["mesure"], "mesuré : " + str(res.get("raison")))
        assert_equal(res["soustractions"], 1, "une soustraction")
        f = res["iterations"][0]
        assert_equal(f["unite"], "Batterie", "l'unité")
        assert_equal(f["parties_retenues"], ["02-batterie"], "la batterie soustraite retient la partie batterie")
        assert_true(f["sdr_residu_vrai_avant_db"] is not None and f["sdr_residu_vrai_apres_db"] is not None,
                    "SDR du résidu vrai avant et après")
        assert_true(f["montee_db"] is not None, "et la montée")
        assert_true(f["separation"]["mesure"] and "other" in f["separation"]["stems"],
                    "la séparation du résidu est jugée contre les parties non retenues")
        assert_true("bass" in f["separation"]["stems"] and f["separation"]["stems"]["bass"].get("absent"),
                    "et dit que bass, attendu, n'a pas été rendu par le séparateur factice")
        assert_equal(f["parite"]["pistes_obtenues"], 5, "parité à k = 1 : les quatre pistes d'avant et other · r1")
        assert_equal(res["parite_initiale"]["pistes_obtenues"], 4, "parité initiale : quatre")
        assert_equal(f["pistes_ajoutees"], ["other · r1"], "la piste ajoutée")
        assert_equal(f["distance_projet"], it["distanceProjet"], "la distance en l'état, reprise du rapport")
        texte = vsm_banc.tableau([mesure], vsm_banc.agreger([mesure]))
        assert_true("résiduel r1 : Batterie SOUSTRAITE" in texte and "arrêt" in texte, "le tableau dit la boucle :\n" + texte)
        agregat = vsm_banc.agreger([mesure])["residuel"]
        assert_equal(agregat["unites_soustraites"], {"Batterie": 1}, "agrégé : l'unité soustraite")
        assert_equal(agregat["pistes_ajoutees_total"], 1, "agrégé : une piste ajoutée")


@test
def deux_executions_donnent_le_meme_rapport_a_la_quatrieme_decimale():
    def arrondi(objet):
        if isinstance(objet, float):
            return round(objet, 4)
        if isinstance(objet, dict):
            return {k: arrondi(v) for k, v in objet.items() if k not in ("secondes",)}
        if isinstance(objet, list):
            return [arrondi(v) for v in objet]
        return objet

    with tempfile.TemporaryDirectory() as d:
        a, _, _ = _courir(Path(d), "x", _options_boucle())
        b, _, _ = _courir(Path(d), "x", _options_boucle())   # mêmes dossiers : mêmes chemins publiés
        ra = json.loads((a / "rapport.json").read_text(encoding="utf-8"))
        rb = json.loads((b / "rapport.json").read_text(encoding="utf-8"))
        assert_equal(arrondi(ra["residuel"]), arrondi(rb["residuel"]), "même bloc residuel à la 4e décimale")
        assert_near(ra["globalDistance"], rb["globalDistance"], 1e-4, "même distance globale")


@test
def le_doublon_est_refuse_avant_l_arbitrage_avec_ses_trois_nombres():
    """Au seuil de 8 notes nouvelles (le défaut), le résidu du morceau
    minuscule n'a rien de nouveau : 13 notes transcrites, 7 déjà portées, 6
    nouvelles — refusé, dit, et la boucle s'arrête sur rien-de-discernable."""
    with tempfile.TemporaryDirectory() as d:
        sortie, _, journal = _courir(Path(d), "r1", _options_boucle(notes_min=8))
        rapport = json.loads((sortie / "rapport.json").read_text(encoding="utf-8"))
        it = rapport["residuel"]["iterations"][0]
        assert_equal(it["pistesAjoutees"], [], "aucune piste ajoutée")
        assert_equal(len(it["stemsRefuses"]), 1, "un stem refusé")
        refus = it["stemsRefuses"][0]
        assert_equal(refus["stem"], "other", "other")
        assert_true(refus["transcrites"] == refus["dejaPortees"] + refus["nouvelles"] and refus["nouvelles"] < 8,
                    "les trois nombres : " + str(refus))
        assert_equal(rapport["residuel"]["arret"]["motif"], "rien-de-discernable", "le motif")
        assert_true("REFUSÉ (résidu r1)" in journal, "le refus est imprimé")
        assert_true("arbitrage piste" not in journal.split("partage du résidu")[1],
                    "et l'arbitrage n'a pas tourné sur le résidu")
        noms = [t["name"] for t in json.loads((sortie / "project.json").read_text(encoding="utf-8"))["tracks"]]
        assert_true(not any("r1" in n for n in noms), "rien du résidu dans le projet : " + str(noms))


if __name__ == "__main__":
    raise SystemExit(run())
