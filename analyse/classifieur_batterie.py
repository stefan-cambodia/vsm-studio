#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Entraîne le CLASSIFIEUR DE FRAPPES — phase A2 — et le juge au banc.

    .venv/bin/python classifieur_batterie.py --sortie modeles/frappes.joblib

Le corpus est engendré à la volée (quelques milliers de frappes seules et
superposées, une trentaine de secondes) : il n'a pas à être conservé, il se
refait à l'identique. Le banc des motifs-vérité est rejoué avant et après,
parce que c'est lui -- et non le score sur corpus -- qui dit si le modèle sert.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer import vsm_drum_bench as bench
from analyzer.vsm_drum_corpus import engendre_corpus_frappes, entraine_frappes
from analyzer.vsm_engine import VsmEngine


def main() -> int:
    analyseur = argparse.ArgumentParser(description=__doc__,
                                         formatter_class=argparse.RawDescriptionHelpFormatter)
    analyseur.add_argument("--sortie", type=Path, default=Path("modeles/frappes.joblib"))
    analyseur.add_argument("--graine", type=int, default=20260823)
    analyseur.add_argument("--seuil", type=float, default=None,
                            help="seuil de décision (défaut : SEUIL_DECISION, balayé au banc)")
    arguments = analyseur.parse_args()

    with VsmEngine(sample_rate=44100) as moteur:
        print("[1/3] Corpus de frappes, seules et superposées")
        corpus = engendre_corpus_frappes(moteur, graine=arguments.graine,
                                         progression=lambda m: print(f"      {m}", end="\r", flush=True))
        print(f"\n      {len(corpus.X)} exemples, {len(set(corpus.situations))} situations")

        print("[2/3] Entraînement, épreuve sur des situations jamais vues")
        modele, mesures = entraine_frappes(corpus, graine=arguments.graine,
                                           **({'seuil': arguments.seuil} if arguments.seuil is not None else {}))
        for piece, m in mesures["parPiece"].items():
            print(f"      {piece:8s} rappel {m['rappel']:.1%}  précision {m['precision']:.1%}")

        print("[3/3] Le banc des motifs-vérité, sans puis avec le modèle")
        audios = [(fab(), bench.rend_motif(fab(), moteur)) for fab in bench.MOTIFS]
        sans = [bench.juge(m, moteur, audio=a) for m, a in audios]
        avec = [bench.juge(m, moteur, audio=a, hit_classifier=modele) for m, a in audios]

    print(f"\n      {'motif':<14} {'pièce':<7} {'sans modèle':>18} {'avec modèle':>18}")
    for s_sans, s_avec in zip(sans, avec, strict=True):
        for f0, f1 in zip(s_sans.familles, s_avec.familles, strict=True):
            print(f"      {s_sans.motif:<14} {f0.famille:<7} "
                  f"{f0.retrouvees:>3}/{f0.attendues:<3} (+{f0.inventees:>2} inv.)   "
                  f"{f1.retrouvees:>3}/{f1.attendues:<3} (+{f1.inventees:>2} inv.)")

    arguments.sortie.parent.mkdir(parents=True, exist_ok=True)
    modele.mesures["banc"] = {s.motif: {f.famille: {"retrouvees": f.retrouvees,
                                                     "attendues": f.attendues,
                                                     "inventees": f.inventees}
                                        for f in s.familles} for s in avec}
    modele.enregistre(arguments.sortie)
    print(f"\nmodèle écrit : {arguments.sortie}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
