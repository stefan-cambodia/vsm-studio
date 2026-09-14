#!/usr/bin/env python3
"""D286 : la tête de lecture d'une lane du bas tombe-t-elle sur la MÊME colonne que celle de l'arrangement ?

    analyse/.venv/bin/python tools/alignement-lanes.py capture.png [--haut Y1] [--bas Y2]

RÈGLE. L'attendu de D286 est un alignement au pixel : le même tick, la même
colonne d'écran, dans l'arrangement et dans la lane (automation, MIDI CC ou
tempo) affichée sous lui. Cet outil le MESURE sur un autoportrait de la fenêtre
(`VSM_CAPTURE`) plutôt que de le lire à l'œil : il cherche, dans la bande de
l'arrangement puis dans celle de la lane, la colonne qui porte le plus de pixels
de la couleur de la tête (ambre), et publie les deux abscisses et leur écart.

POURQUOI PAR LA COULEUR ET NON PAR UN RELEVÉ DE TEXTE : la tête est PEINTE
(`g.fillRect`), invisible à `VSM_TEXTES_LISTE` (la leçon de D149). La photo est la
seule source, et une mesure sur la photo vaut mieux qu'un regard.

Les bandes se donnent en pixels de l'image : `--haut` (y1,y2 de l'arrangement)
et `--bas` (y1,y2 de la lane). Les défauts sont ceux d'une fenêtre 2117 × 1317
à 150 %, arrangement au-dessus, onglet du bas ouvert.

Rend 0 si l'écart est d'au plus 1 pixel, 1 sinon, 2 si une des bandes n'a pas
de colonne ambre (la tête n'y est pas : c'est un résultat, pas un succès).
"""
from __future__ import annotations

import argparse
import sys

import numpy as np
from PIL import Image

AMBRE = np.array([230, 170, 60])   # Palette::accentAmber, à la tolérance près
TOLERANCE = 60


def colonne_ambre(image: np.ndarray, y1: int, y2: int) -> tuple[int | None, int]:
    bande = image[y1:y2, :, :3].astype(int)
    proche = np.all(np.abs(bande - AMBRE) <= TOLERANCE, axis=2)
    par_colonne = proche.sum(axis=0)
    if par_colonne.max() < max(4, (y2 - y1) // 8):
        return None, int(par_colonne.max())
    # la colonne médiane des colonnes au maximum (un trait de deux pixels)
    colonnes = np.flatnonzero(par_colonne >= par_colonne.max() - 1)
    return int(np.median(colonnes)), int(par_colonne.max())


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("capture")
    p.add_argument("--haut", default="110,1000", help="y1,y2 de la bande de l'arrangement")
    p.add_argument("--bas", default="1090,1310", help="y1,y2 de la bande de la lane")
    a = p.parse_args(argv[1:])
    image = np.asarray(Image.open(a.capture).convert("RGB"))
    h1, h2 = (int(v) for v in a.haut.split(","))
    b1, b2 = (int(v) for v in a.bas.split(","))
    xh, nh = colonne_ambre(image, h1, h2)
    xb, nb = colonne_ambre(image, b1, b2)
    print(f"arrangement (y {h1}-{h2}) : colonne ambre {xh} ({nh} px)")
    print(f"lane        (y {b1}-{b2}) : colonne ambre {xb} ({nb} px)")
    if xh is None or xb is None:
        print("VERDICT : une des deux bandes n'a pas de tête — pas d'alignement à mesurer")
        return 2
    ecart = abs(xh - xb)
    print(f"écart : {ecart} px → {'ALIGNÉ' if ecart <= 1 else 'DÉCALÉ'}")
    return 0 if ecart <= 1 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
