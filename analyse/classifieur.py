#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Entraîne et éprouve le CLASSIFIEUR DE MACHINE — phase A1.

    .venv/bin/python classifieur.py --corpus corpus/ --sortie modeles/classifieur.joblib
    .venv/bin/python classifieur.py --corpus corpus/ --confusion
    .venv/bin/python classifieur.py --eprouver modeles/classifieur.joblib --audio stem.wav

Ce que la commande imprime est ce que le § 4 du cahier des charges demande de
mesurer, dans son ordre : le taux de bonne machine dans le top 3, les cas
INDISTINGUABLES comptés à part, et le rayon au-delà duquel le modèle s'abstient.

RIEN N'EST SILENCIEUX (§ 8.3) : chaque décision — classement retenu, abstention,
refus du modèle pour cause de péremption — s'imprime avec la mesure qui la
motive.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_classifier import (Classifieur, charge_corpus, coupe_par_patch,
                                      entraine, matrice_de_confusion)
from analyzer.vsm_engine import VsmEngine, VsmEngineError


def entrainer(arguments) -> int:
    depart = time.perf_counter()
    print(f"[1/3] Lecture du corpus {arguments.corpus}")
    corpus = charge_corpus(arguments.corpus)
    print(f"      {len(corpus.X)} exemples, {len(corpus.noms)} machines, "
          f"{corpus.X.shape[1]} descripteurs")
    secs = corpus.augmentations.count("")
    print(f"      {secs} secs, {len(corpus.augmentations) - secs} augmentés")

    print("[2/3] Entraînement")
    classifieur, mesures = entraine(corpus, graine=arguments.graine,
                                     part_epreuve=arguments.part_epreuve,
                                     seuil_abstention=arguments.seuil,
                                     progression=lambda m: print(f"      {m}"))

    print("[3/3] Épreuve, sur des patchs JAMAIS VUS à l'entraînement")
    print(f"      top 1 : {mesures['top1']:.1%}   top 3 : {mesures['top3']:.1%}"
          f"   top 5 : {mesures['top5']:.1%}")
    print(f"      dont indistinguables : {mesures['ambigus']} "
          f"({mesures['partAmbigus']:.1%}) — le son le plus proche du corpus vient "
          f"d'une AUTRE machine")
    print(f"      hors indistinguables : top 1 {mesures['top1SansAmbigus']:.1%}   "
          f"top 3 {mesures['top3SansAmbigus']:.1%}   "
          f"top 5 {mesures['top5SansAmbigus']:.1%}")
    print(f"      rayon de nouveauté : {mesures['rayonNouveaute']:.2f} "
          f"(quantile {mesures['quantileRayon']:.0%}, "
          f"{mesures['refusAbusifs']:.1%} de refus abusifs sur le corpus) ; "
          f"seuil de score {mesures['seuilAbstention']:.2f}")

    cible = 0.95
    obtenu = mesures["top3SansAmbigus"]
    verdict = "ATTEINT" if obtenu >= cible else "NON ATTEINT"
    print(f"\n      critère A1.1 (top 3 ≥ 95 % hors indistinguables) : {verdict} "
          f"({obtenu:.1%})")

    if arguments.confusion:
        _, indices_epreuve = coupe_par_patch(corpus, arguments.part_epreuve, arguments.graine)
        matrice = matrice_de_confusion(corpus, classifieur, indices_epreuve)
        print("\nconfusions les plus fréquentes (vraie -> prédite) :")
        paires = []
        for i, vraie in enumerate(corpus.noms):
            for j, predite in enumerate(corpus.noms):
                if i != j and matrice[i, j]:
                    paires.append((int(matrice[i, j]), vraie, predite, int(matrice[i].sum())))
        for compte, vraie, predite, total in sorted(paires, reverse=True)[:12]:
            print(f"  {vraie:18s} -> {predite:18s} {compte:5d} / {total} ({compte / total:.0%})")

    if arguments.sortie:
        arguments.sortie.parent.mkdir(parents=True, exist_ok=True)
        classifieur.enregistre(arguments.sortie)
        print(f"\nmodèle écrit : {arguments.sortie}")
        print(f"  il porte les empreintes de ses {len(classifieur.empreintes)} machines : "
              f"si le son de l'une d'elles bouge, il sera REFUSÉ, pas appliqué")
    print(f"  total : {time.perf_counter() - depart:.0f} s")
    return 0 if obtenu >= cible else 1


def eprouver(arguments) -> int:
    """Classe un fichier audio réel — c'est le critère 4 du § 4 : sur une source
    acoustique, le modèle doit s'abstenir plutôt que d'être confiant à tort."""
    import soundfile

    classifieur = Classifieur.relit(arguments.eprouver)
    print(f"modèle du {classifieur.date}, {len(classifieur.noms)} machines, "
          f"graine {classifieur.graine}")

    if not arguments.sans_verification:
        try:
            with VsmEngine(sample_rate=44100) as moteur:
                verdict = classifieur.verifie_fraicheur(moteur)
            if not verdict.frais:
                # REFUSÉ, pas appliqué : un modèle entraîné sur le son d'hier ne
                # se trompe pas bruyamment, il classe plausiblement et faux.
                print(f"MODÈLE REFUSÉ — {verdict.resume()}", file=sys.stderr)
                return 3
            print("  empreintes vérifiées : le son des machines n'a pas bougé")
        except VsmEngineError as erreur:
            print(f"vérification impossible ({erreur}) — passez --sans-verification "
                  f"si vous acceptez de vous en dispenser", file=sys.stderr)
            return 3

    from analyzer.vsm_corpus import descriptors

    audio, sample_rate = soundfile.read(str(arguments.audio), always_2d=True)
    mono = audio.mean(axis=1).astype(np.float32)
    debut = int(arguments.depart * sample_rate)
    extrait = mono[debut:debut + int(arguments.duree * sample_rate)]
    if extrait.size == 0:
        print("extrait vide", file=sys.stderr)
        return 2

    vecteur = descriptors(extrait, sample_rate, arguments.note, 1.0, arguments.duree)
    classement, motif = classifieur.classe(vecteur)
    print(f"\nextrait {arguments.depart:.1f}–{arguments.depart + arguments.duree:.1f} s "
          f"de {Path(arguments.audio).name}")
    for nom, score in classement:
        print(f"  {nom:20s} {score:.3f}")
    if motif:
        print(f"\nABSTENTION — {motif}")
    else:
        print(f"\nretenu : {classement[0][0]} ({classement[0][1]:.2f})")
    return 0


def main() -> int:
    analyseur = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    analyseur.add_argument("--corpus", type=Path, default=Path("corpus"))
    analyseur.add_argument("--sortie", type=Path, default=None,
                            help="où écrire le modèle (défaut : ne pas l'écrire)")
    analyseur.add_argument("--graine", type=int, default=20260823)
    analyseur.add_argument("--part-epreuve", type=float, default=0.2)
    analyseur.add_argument("--seuil", type=float, default=0.20,
                            help="score maximal en deçà duquel le modèle s'abstient")
    analyseur.add_argument("--confusion", action="store_true",
                            help="imprimer les confusions les plus fréquentes")
    analyseur.add_argument("--eprouver", type=Path, default=None,
                            help="au lieu d'entraîner : classer un fichier avec ce modèle")
    analyseur.add_argument("--audio", type=Path, default=None)
    analyseur.add_argument("--depart", type=float, default=0.0)
    analyseur.add_argument("--duree", type=float, default=1.0)
    analyseur.add_argument("--note", type=int, default=60)
    analyseur.add_argument("--sans-verification", action="store_true",
                            help="ne pas vérifier les empreintes (déconseillé, et dit)")
    arguments = analyseur.parse_args()

    if arguments.eprouver is not None:
        if arguments.audio is None:
            print("--eprouver exige --audio", file=sys.stderr)
            return 2
        return eprouver(arguments)
    return entrainer(arguments)


if __name__ == "__main__":
    raise SystemExit(main())
