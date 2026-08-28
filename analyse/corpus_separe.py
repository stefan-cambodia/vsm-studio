#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Le corpus PASSÉ PAR LA SÉPARATION, et la mesure qui dit s'il referme le fossé.

    .venv/bin/python corpus_separe.py --stems DOSSIER[,DOSSIER...] --sortie corpus/separe.npz
    .venv/bin/python corpus_separe.py --corpus corpus/separe.npz \\
        --sonde "Clair de Lune:cdl.wav:projet/midi/arrangement.mid#melange:vsm.piano"

Première forme : engendre le corpus (rendus du moteur mélangés à de VRAIS
stems, séparés par demucs en une passe, redécoupés, décrits). Seconde forme :
mesure. Les deux s'enchaînent si `--sortie` et `--sonde` sont donnés.

C'est la condition de réouverture écrite au § 7 de ROADMAP-fusion.md, éprouvée
AVANT d'écrire la phase A3 : si un modèle entraîné sur le séparé ne lit pas
mieux un disque qu'un modèle entraîné sur le sec, A3 n'a pas de corpus, et
c'est un résultat.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_corpus_separe import (CorpusSepare, Sonde, Vivier, coupe_par_patch,  # noqa: E402
                                        engendre, entraine_simple, restreint, top_k)
from analyzer.vsm_engine import VsmEngine  # noqa: E402

SR = 44100


def main() -> int:
    analyseur = argparse.ArgumentParser(description=__doc__,
                                         formatter_class=argparse.RawDescriptionHelpFormatter)
    analyseur.add_argument("--stems", default="", help="dossiers de stems réels, séparés par des virgules")
    analyseur.add_argument("--sortie", type=Path, default=None, help="où écrire le corpus (.npz)")
    analyseur.add_argument("--corpus", type=Path, default=None, help="corpus déjà engendré (.npz)")
    analyseur.add_argument("--patchs", type=int, default=25, help="patchs par machine")
    analyseur.add_argument("--graine", type=int, default=20260824)
    analyseur.add_argument("--machines", default="", help="sous-ensemble de machines (virgules)")
    analyseur.add_argument("--sonde", action="append", default=[],
                            help="NOM:AUDIO:NOTES:MACHINE — un enregistrement réel et la machine que "
                                 "l'arbitrage y retient ; NOTES est un JSON de notes ou le MIDI du "
                                 "projet reconstruit suivi de sa piste (arrangement.mid#other)")
    arguments = analyseur.parse_args()

    corpus = None
    if arguments.corpus and arguments.corpus.exists():
        corpus = CorpusSepare.relit(arguments.corpus)
        print(f"corpus relu : {len(corpus.y)} exemples, {len(corpus.machines)} machines")
        if arguments.machines:
            corpus = restreint(corpus, [m.strip() for m in arguments.machines.split(",") if m.strip()])
            print(f"      restreint à {len(corpus.machines)} machines : {len(corpus.y)} exemples")
    elif arguments.stems and arguments.sortie:
        dossiers = [Path(d).expanduser() for d in arguments.stems.split(",") if d.strip()]
        rng = np.random.default_rng(arguments.graine)
        vivier = Vivier.depuis(dossiers, SR, rng)
        print(f"[1/3] vivier de fond : {len(vivier.tranches)} tranches "
              f"({', '.join(sorted(set(vivier.origines)))})")
        machines = [m.strip() for m in arguments.machines.split(",") if m.strip()] or None
        with VsmEngine(sample_rate=SR) as moteur:
            print("[2/3] rendus, mélanges, séparation en une passe")
            corpus = engendre(moteur, vivier, arguments.patchs, SR, arguments.graine,
                              arguments.sortie.parent / (arguments.sortie.stem + "-travail"),
                              machines=machines,
                              progression=lambda m: print(f"      {m}", flush=True))
        arguments.sortie.parent.mkdir(parents=True, exist_ok=True)
        corpus.enregistre(arguments.sortie)
        print(f"      {len(corpus.y)} exemples en {corpus.secondes:.0f} s, "
              f"énergie retrouvée dans « other » : médiane {np.median(corpus.retrouve):.2f}, "
              f"quart inférieur {np.quantile(corpus.retrouve, 0.25):.2f}"
              + (f", rejets {corpus.rejets}" if corpus.rejets else ""))
        print(f"      corpus écrit : {arguments.sortie}")
    else:
        print("donner --corpus, ou --stems et --sortie", file=sys.stderr)
        return 2

    print("[3/3] la mesure : trois modèles, même coupure par patch")
    epreuve = coupe_par_patch(corpus.patchs, 0.2, arguments.graine)
    ent = ~epreuve
    modeles = {
        "sec": entraine_simple(corpus.X_sec[ent], corpus.y[ent], corpus.machines, arguments.graine),
        "séparé": entraine_simple(corpus.X_sep[ent], corpus.y[ent], corpus.machines, arguments.graine),
        "sec + séparé": entraine_simple(np.concatenate([corpus.X_sec[ent], corpus.X_sep[ent]]),
                                        np.concatenate([corpus.y[ent], corpus.y[ent]]),
                                        corpus.machines, arguments.graine),
    }
    sondes = []
    for spec in arguments.sonde:
        nom, audio, notes, machine = spec.split(":", 3)
        if machine not in corpus.machines:
            print(f"      sonde « {nom} » ignorée : {machine} n'est pas dans le corpus")
            continue
        sondes.append(Sonde.charge(nom, Path(audio).expanduser(), notes, machine))

    entete = f"{'entraîné sur':<14} {'sec top1/3':>12} {'séparé top1/3':>14}"
    for s in sondes:
        entete += f"  {s.nom + ' (' + s.machine.split('.')[-1] + ') rang méd./top5':>34}"
    print("      " + entete)
    for nom, modele in modeles.items():
        ligne = (f"{nom:<14} "
                 f"{top_k(modele, corpus.X_sec[epreuve], corpus.y[epreuve], 1):>5.0%}/{top_k(modele, corpus.X_sec[epreuve], corpus.y[epreuve], 3):<5.0%} "
                 f"{top_k(modele, corpus.X_sep[epreuve], corpus.y[epreuve], 1):>7.0%}/{top_k(modele, corpus.X_sep[epreuve], corpus.y[epreuve], 3):<5.0%}")
        for s in sondes:
            med, t5 = s.rang_median(modele)
            ligne += f"  {med:>25.0f} / {t5:<6.0%}"
        print("      " + ligne)
    # Le séparé, par tranche d'énergie retrouvée : un synthé parti dans un
    # autre stem n'est pas un exemple dégradé, c'est un exemple absent.
    for bas, haut in ((0.0, 0.3), (0.3, 0.7), (0.7, 1.01)):
        masque = epreuve & (corpus.retrouve >= bas) & (corpus.retrouve < haut)
        if masque.sum() == 0:
            continue
        print(f"      séparé, énergie retrouvée [{bas:.1f}, {haut:.1f}) : {masque.sum()} ex., "
              + ", ".join(f"{nom} top3 {top_k(m, corpus.X_sep[masque], corpus.y[masque], 3):.0%}"
                          for nom, m in modeles.items()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
