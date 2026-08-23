#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Rejoue le banc des motifs-vérité de batterie — le juge de la phase A2.

    .venv/bin/python banc_batterie.py
    .venv/bin/python banc_batterie.py --machines vsm.tr909,vsm.tr808,vsm.drums

Imprime, motif par motif et pièce par pièce, les frappes retrouvées, inventées
et confondues. C'est ce tableau — et lui seul — qui dit où en est le détecteur
et si un changement l'a amélioré.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_drum_bench import MOTIFS, juge, tableau
from analyzer.vsm_engine import VsmEngine


def main() -> int:
    analyseur = argparse.ArgumentParser(description=__doc__,
                                         formatter_class=argparse.RawDescriptionHelpFormatter)
    analyseur.add_argument("--machines", default="",
                            help="rejouer chaque motif sur ces machines (défaut : celles du motif)")
    arguments = analyseur.parse_args()
    machines = [m.strip() for m in arguments.machines.split(",") if m.strip()]

    scores = []
    with VsmEngine(sample_rate=44100) as moteur:
        for fabrique in MOTIFS:
            for machine in (machines or [fabrique().machine]):
                motif = fabrique(machine)
                scores.append(juge(motif, moteur))
    print(tableau(scores))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
