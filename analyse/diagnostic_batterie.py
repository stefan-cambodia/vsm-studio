#!/usr/bin/env python3
"""Où passe l'erreur de la batterie ?

Le budget d'erreur (`budget_erreur.py`) dit que la batterie pèse **63,5 %** de
la distance globale de *Sky and Sand* : c'est de loin le premier poste, et le
seul qui vaille d'être travaillé. Il ne dit pas POURQUOI.

Trois causes possibles, et une seule façon de les départager : rejouer la même
rythmique par des moyens différents, et mesurer.

  1. LA DÉTECTION  — les coups sont-ils au bon endroit, et n'en manque-t-il pas ?
     Mesurée par la part de l'énergie du stem qui tombe hors de tout coup
     détecté : si elle est forte, aucune machine ne rattrapera ce qui n'a pas
     été entendu.
  2. LE TIMBRE     — la machine peut-elle faire ce son ? Mesuré en rejouant les
     MÊMES instants avec les VRAIS coups découpés dans l'enregistrement
     (le sampler). C'est le plafond atteignable à détection constante.
  3. L'ATTRIBUTION — chaque famille tombe-t-elle sur la bonne voix ? Mesurée en
     comptant les coups qui atterrissent sur une note dont la machine n'a
     aucune voix, ou sur une voix déjà occupée par une autre famille.

Toutes les distances sont prises avec la MÊME métrique et contre le MÊME stem :
sans cela elles ne se compareraient pas (§ 10.3 de ROADMAP-fusion.md).
"""
from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path
from typing import Dict, List, Optional

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_distance_cache import cached_distance_for  # noqa: E402
from analyzer.vsm_drumkit import (                         # noqa: E402
    DRUM_MACHINE_NOTES, build_drum_kit, drum_kit_track, drum_machine_track,
    modelled_drum_track,
)
from analyzer.vsm_offline_render import render_track_offline  # noqa: E402
from analyzer.vsm_project_export import ExportNote, ExportTrack  # noqa: E402

SAMPLE_RATE = 44100


def charger_audio(chemin: Path) -> np.ndarray:
    """Mono, à SAMPLE_RATE -- la même lecture que la chaîne."""
    import librosa
    audio, _ = librosa.load(str(chemin), sr=SAMPLE_RATE, mono=True)
    return np.asarray(audio, dtype=np.float32)

# Les notes auxquelles chaque boîte du parc répond RÉELLEMENT, lues dans le
# C++ (TR808Synth.h et TR909Synth.h). Une note absente de cette liste ne
# déclenche aucune voix : le coup est silencieux, sans que rien ne le dise.
VOIX_REELLES: Dict[str, Dict[int, str]] = {
    "vsm.tr808": {36: "kick", 38: "snare", 39: "clap", 42: "closedHat",
                   46: "openHat", 56: "cowbell"},
    "vsm.tr909": {36: "kick", 38: "snare", 39: "clap", 42: "closedHat",
                   46: "openHat", 49: "crash", 45: "lowTom", 47: "midTom", 50: "hiTom"},
}


def couverture_des_coups(audio: np.ndarray, instants: List[float], fenetre: float) -> float:
    """Part de l'énergie du stem qui tombe DANS une fenêtre après un coup détecté.

    Ce qui reste dehors est ce que la détection n'a pas entendu : aucune machine,
    aucun réglage, aucun échantillon ne le rattrapera.
    """
    carre = np.square(audio, dtype=np.float64)
    total = float(carre.sum())
    if total <= 0.0:
        return 1.0
    masque = np.zeros(audio.size, dtype=bool)
    largeur = int(fenetre * SAMPLE_RATE)
    for instant in instants:
        debut = int(instant * SAMPLE_RATE)
        masque[max(0, debut):min(audio.size, debut + largeur)] = True
    return float(carre[masque].sum() / total)


def attribution(piste: ExportTrack) -> Dict[str, object]:
    """Ce que deviennent les coups une fois posés sur les notes de la machine."""
    voix = VOIX_REELLES.get(piste.machine)
    par_note: Dict[int, int] = {}
    for note in piste.notes:
        par_note[note.note] = par_note.get(note.note, 0) + 1
    if voix is None:
        return {"muets": 0, "parNote": par_note, "detail": "machine hors table"}
    muets = sum(n for note, n in par_note.items() if note not in voix)
    partages = {voix[note]: n for note, n in sorted(par_note.items()) if note in voix}
    return {"muets": muets, "parVoix": partages,
            "notesSansVoix": sorted(note for note in par_note if note not in voix)}


def mesurer(piste: ExportTrack, stem: np.ndarray, dossier: Path, metrique: str,
             moteur: Optional[str]) -> Optional[float]:
    duree = float(stem.size) / SAMPLE_RATE
    rendu = render_track_offline(piste, dossier, SAMPLE_RATE, duration=duree,
                                  binary=moteur, title="diagnostic-batterie")
    (dossier / "rendu.wav").unlink(missing_ok=True)
    if rendu is None or rendu.size == 0:
        return None
    return float(cached_distance_for(metrique)(stem, SAMPLE_RATE)(rendu))


# Les voix réellement disponibles sur chaque boîte, par RÔLE et non par nom de
# famille. C'est la table qu'une attribution par le spectre a besoin de lire.
ROLES: Dict[str, Dict[str, int]] = {
    "vsm.tr808": {"kick": 36, "snare": 38, "clap": 39, "closedHat": 42,
                   "openHat": 46, "cowbell": 56},
    "vsm.tr909": {"kick": 36, "snare": 38, "clap": 39, "closedHat": 42,
                   "openHat": 46, "crash": 49, "lowTom": 45, "midTom": 47, "hiTom": 50},
}


def role_par_profil(parts: List[float]) -> str:
    """Le RÔLE d'une famille, déduit de son spectre et non de son nom.

    Les seuils portent sur deux grandeurs seulement, parce que ce sont les deux
    que l'oreille utilise pour ranger une pièce de batterie : la part de
    l'énergie sous 200 Hz (une peau grave) et celle au-dessus de 2 kHz (du
    métal ou du bruit). Le reste est du médium, et c'est là que vivent les
    claps et les caisses claires.

    Une famille SANS profil mesuré ne reçoit pas de rôle : on rend une chaîne
    vide, et l'appelant retombe sur le nom. Deviner à partir de rien serait
    exactement ce que ce travail cherche à supprimer.
    """
    if len(parts) < 6:
        return ""
    grave = parts[0] + parts[1]
    aigu = parts[4] + parts[5]
    if grave >= 0.50 and aigu < 0.20:
        return "kick"
    if aigu >= 0.40:
        return "closedHat"
    if aigu >= 0.20:
        return "snare"
    return "clap"


def piste_par_profil(kit, machine: str, nom: str) -> ExportTrack:
    """La même rythmique, chaque famille posée sur la voix que son SPECTRE désigne.

    Quand deux familles réclament le même rôle, la seconde va sur la voix
    voisine que la machine propose (une deuxième peau grave sur un tom, un
    second bruit sur la charleston ouverte) plutôt que de s'empiler : deux
    pièces différentes jouées par la même voix ne se distinguent plus, et le
    motif perd sa texture.
    """
    voix = ROLES.get(machine, {})
    voisins = {
        "kick": ["kick", "lowTom", "midTom", "hiTom", "clap"],
        "snare": ["snare", "clap", "midTom"],
        "clap": ["clap", "snare", "cowbell"],
        "closedHat": ["closedHat", "openHat", "crash"],
    }
    pris: set = set()
    notes: List[ExportNote] = []
    # Les familles les plus fournies choisissent en premier : c'est celle qui
    # porte le motif qui doit avoir la voix juste.
    for emplacement in sorted(kit.slots, key=lambda s: -s.hit_count):
        role = role_par_profil(list(emplacement.band_shares))
        note = None
        for candidat in voisins.get(role, []):
            if candidat in voix and candidat not in pris:
                pris.add(candidat)
                note = voix[candidat]
                break
        if note is None:
            note = voix.get(role, int(emplacement.midi_note))
        for instant, velocite in zip(emplacement.onsets, emplacement.velocities, strict=True):
            notes.append(ExportNote(note=note, velocity=velocite, start=instant, duration=0.05))
    notes.sort(key=lambda n: n.start)
    return ExportTrack(name=nom, machine=machine, parameters={}, notes=notes, is_drums=True)


def piste_avec_table(kit, machine: str, table: Dict[str, int], nom: str) -> ExportTrack:
    """La même rythmique, posée sur une table de correspondance choisie."""
    notes: List[ExportNote] = []
    for emplacement in kit.slots:
        note = table.get(emplacement.family, int(emplacement.midi_note))
        for instant, velocite in zip(emplacement.onsets, emplacement.velocities, strict=True):
            notes.append(ExportNote(note=note, velocity=velocite, start=instant, duration=0.05))
    notes.sort(key=lambda n: n.start)
    return ExportTrack(name=nom, machine=machine, parameters={}, notes=notes, is_drums=True)


def main() -> int:
    parseur = argparse.ArgumentParser(description=__doc__,
                                       formatter_class=argparse.RawDescriptionHelpFormatter)
    parseur.add_argument("stem", type=Path, help="le stem de batterie (drums.wav)")
    parseur.add_argument("--travail", type=Path, default=Path("../reconstruction/travail/diag-batterie"))
    parseur.add_argument("--metrique", default="v4")
    parseur.add_argument("--moteur", default=None, help="binaire vsm-render à utiliser")
    parseur.add_argument("--classifieur", type=Path, default=None)
    parseur.add_argument("--regler", nargs="*", default=None,
                          help="étiquettes (sous-chaînes) des candidates à RÉGLER "
                               "avant mesure, avec le budget de la chaîne")
    parseur.add_argument("--budget", type=int, default=120)
    parseur.add_argument("--axes", type=int, default=21)
    parseur.add_argument("--patch-regle", type=Path, default=None,
                          help="un *.synth.json dont le patch sera rejoué tel quel")
    args = parseur.parse_args()

    stem = charger_audio(args.stem)
    args.travail.mkdir(parents=True, exist_ok=True)
    print(f"stem : {stem.size / SAMPLE_RATE:.1f} s, métrique {args.metrique}")

    frappes = None
    if args.classifieur is not None:
        from analyzer.vsm_drum_corpus import ClassifieurFrappes
        frappes = ClassifieurFrappes.relit(args.classifieur)
        print(f"classifieur de frappes : {args.classifieur.name}")

    # LES ÉCHANTILLONS VONT DANS LE DOSSIER DU RENDU, et c'est indispensable :
    # `render_track_offline` écrit un projet d'une piste DANS ce dossier, et le
    # sampler résout ses chemins par rapport à lui. Écrits ailleurs, il rend du
    # silence -- et une candidate muette se fait écarter pour un timbre qui n'a
    # rien à voir. C'est exactement la panne muette du § 5 bis, et elle attend
    # au premier détour.
    rendu_dossier = args.travail / "rendu"
    rendu_dossier.mkdir(parents=True, exist_ok=True)
    kit = build_drum_kit(stem, SAMPLE_RATE, rendu_dossier / "samples",
                          write_samples=True, hit_classifier=frappes)
    if kit is None:
        print("aucun coup détecté")
        return 1
    detail = " ".join(f"{s.family}={s.hit_count}" for s in kit.slots)
    print(f"kit : {len(kit.slots)} pièce(s), {kit.total_hits} frappe(s) — {detail}\n")

    # TÉMOINS DE LA MESURE. Sans eux, on ne saurait pas lire les distances :
    # le stem contre lui-même donne le zéro, le stem contre le silence donne
    # l'échelle, et le stem contre lui-même à moitié moins fort dit si la
    # métrique est bien insensible au niveau -- ce que la chaîne suppose.
    mesure = cached_distance_for(args.metrique)(stem, SAMPLE_RATE)
    print(f"  témoin : stem contre lui-même        D = {float(mesure(stem)):.4f}")
    print(f"  témoin : stem contre le silence      D = {float(mesure(np.zeros_like(stem))):.4f}")
    print(f"  témoin : stem contre lui-même à -6dB D = {float(mesure(stem * 0.5)):.4f}")
    print()

    tous = [instant for s in kit.slots for instant in s.onsets]
    for fenetre in (0.05, 0.12, 0.30):
        part = couverture_des_coups(stem, tous, fenetre)
        print(f"  couverture de l'énergie à {fenetre*1000:.0f} ms : {part*100:5.1f} %")
    print()

    # CHAQUE FABRIQUE PART D'UNE COPIE FRAÎCHE DU KIT. Elles le MUTENT --
    # `drum_machine_track` y ajoute ses avertissements, et l'attribution des
    # notes n'est pas sans effet de bord. Les enchaîner sur le même objet fait
    # dépendre le résultat de l'ordre d'appel, ce qui rend la comparaison
    # fausse sans que rien ne le signale.
    def frais():
        return copy.deepcopy(kit)

    corrigee = dict(DRUM_MACHINE_NOTES["vsm.tr808"])
    corrigee["clap"] = 39                # la 808 A un clap (note 39), non mappé
    corrigee["percussion"] = 56          # la vache, pour ne plus l'empiler sur le clap

    # vsm.drums AVEC LE PATCH D'USINE : c'est cette candidate-là que
    # l'arbitrage mesure (il construit ses candidates avec des paramètres
    # VIDES), et non celle que `modelled_drum_track` propose avec les siens.
    drums_usine = modelled_drum_track(frais(), name="Batterie")
    drums_usine.parameters = {}

    # LE REPLIAGE DES TOMS, LES DEUX FAÇONS. La table actuelle envoie les toms
    # sur le CLAP (« la pièce la plus proche en fonction : un coup sec et
    # court »). Mais la mesure du spectre moyen des coups de cette famille dit
    # que ce sont des graves -- 69 % de leur énergie sous 200 Hz. Poser un
    # grave sur une salve de bruit est le pire choix possible ; la grosse
    # caisse est l'autre candidat évident, et seule la mesure tranche.
    vers_kick = dict(DRUM_MACHINE_NOTES["vsm.tr808"])
    vers_kick["clap"] = 39
    vers_kick["tom"] = vers_kick["tom2"] = vers_kick["tom3"] = 36     # la grosse caisse
    vers_kick["percussion"] = 56                                      # la vache

    candidates: List[tuple] = [
        ("sampler (vrais coups découpés)", drum_kit_track(frais(), name="Batterie")),
        ("vsm.tr808 (toms -> grosse caisse)", piste_avec_table(frais(), "vsm.tr808", vers_kick, "Batterie")),
        ("vsm.tr808 (voix par le SPECTRE)", piste_par_profil(frais(), "vsm.tr808", "Batterie")),
        ("vsm.tr909 (voix par le SPECTRE)", piste_par_profil(frais(), "vsm.tr909", "Batterie")),
        ("vsm.drums (patch VIDE, comme l'arbitrage)", drums_usine),
        ("vsm.drums (modélisée, usine)", modelled_drum_track(frais(), name="Batterie")),
        ("vsm.tr808 (usine, table actuelle)", drum_machine_track(frais(), "vsm.tr808", name="Batterie")),
        ("vsm.tr909 (usine, table actuelle)", drum_machine_track(frais(), "vsm.tr909", name="Batterie")),
        ("vsm.tr808 (table corrigée)", piste_avec_table(frais(), "vsm.tr808", corrigee, "Batterie")),
    ]

    # Le patch RÉGLÉ retenu par la dernière reconstruction, pour raccrocher le
    # diagnostic au chiffre que la chaîne a publié.
    if args.patch_regle is not None:
        valeurs = json.loads(args.patch_regle.read_text())["parameters"]
        reglee = drum_machine_track(frais(), "vsm.tr808", name="Batterie")
        reglee.parameters = {k: float(v) for k, v in valeurs.items()}
        candidates.append(("vsm.tr808 (patch réglé de sky-v5)", reglee))
        corrigee_reglee = piste_avec_table(frais(), "vsm.tr808", corrigee, "Batterie")
        corrigee_reglee.parameters = dict(reglee.parameters)
        candidates.append(("vsm.tr808 (table corrigée + patch réglé)", corrigee_reglee))
        kick_reglee = piste_avec_table(frais(), "vsm.tr808", vers_kick, "Batterie")
        kick_reglee.parameters = dict(reglee.parameters)
        candidates.append(("vsm.tr808 (toms -> kick + patch réglé)", kick_reglee))

    regler = args.regler if args.regler is not None else []

    # Le moteur en mode service, ouvert une seule fois et seulement si un
    # réglage est demandé : il déclare les axes explorables de chaque machine.
    _moteur = {}

    def moteur():
        if "m" not in _moteur:
            from analyzer.vsm_engine import VsmEngine
            _moteur["m"] = VsmEngine(binary=args.moteur, sample_rate=SAMPLE_RATE)
        return _moteur["m"]

    resultats = []
    for etiquette, piste in candidates:
        info = attribution(piste)
        if any(motif in etiquette for motif in regler) and piste.machine != "vsm.sampler":
            # LE MÊME RÉGLAGE QUE LA CHAÎNE, avec le même budget : comparer une
            # candidate réglée à une candidate d'usine ne dirait rien.
            from analyzer.vsm_track_refine import refine_patch_on_track
            affine = refine_patch_on_track(
                machine=piste.machine, parameters=dict(piste.parameters),
                notes=piste.notes, stem_audio=stem, engine=moteur(),
                workdir=args.travail / "reglage" / piste.machine,
                budget=args.budget, axes=args.axes,
                sample_rate=SAMPLE_RATE, metric=args.metrique, binary=args.moteur,
                name="Batterie")
            if affine is not None:
                piste.parameters = dict(affine.parameters)
                print(f"  {etiquette:36s} réglée {affine.start_distance:.4f} "
                      f"-> {affine.distance:.4f} ({affine.evaluations} évaluations)")
        distance = mesurer(piste, stem, args.travail / "rendu", args.metrique, args.moteur)
        resultats.append((etiquette, distance, info))
        d = "échec" if distance is None else f"{distance:.4f}"
        print(f"  {etiquette:36s} D = {d}")
        if info.get("muets"):
            print(f"      {info['muets']} frappe(s) sur des notes SANS VOIX "
                  f"{info.get('notesSansVoix')} — silencieuses")
        if "parVoix" in info:
            print(f"      voix : {info['parVoix']}")
    print()

    valides = [(e, d) for e, d, _ in resultats if d is not None]
    if valides:
        meilleur = min(valides, key=lambda x: x[1])
        print(f"  meilleure : {meilleur[0]} à {meilleur[1]:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
