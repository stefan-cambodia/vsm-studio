#!/usr/bin/env python3
"""RÈGLE (D347) : l'application est SOMBRE, et aucune surface ne doit rester au gris
clair par défaut de JUCE.

Un panneau qu'on oublie de colorer ne casse rien, ne fait échouer aucun test et ne
se voit que sur une photo — l'en-tête du tableau d'événements est resté ainsi
pendant toute la vie du logiciel : 2 095 x 22 px de gris clair, la seule surface
claire de l'application, au milieu d'un thème sombre.

CE QUE LE SCRIPT CHERCHE, ET POURQUOI IL NE COMPTE PAS LE TEXTE. Une surface
claire est une SUITE CONTIGUË de pixels clairs et peu saturés ; un texte clair,
lui, donne des suites de quelques pixels seulement. On mesure donc, par rangée, la
PLUS LONGUE suite de pixels « gris clair » (luminance > 130, saturation < 30), et
l'on ne retient que les rangées où elle dépasse `--suite` pixels — **40 % de la
largeur de l'image, au moins 80 px**, et non un nombre fixe : 200 px font 9 % de
la fenêtre entière mais la MOITIÉ d'un panneau flottant de 400 px, et le seuil
fixe laissait donc passer sur un petit panneau ce qu'il attrapait sur la fenêtre.
Les rangées retenues sont groupées en bandes ; une bande d'au moins `--hauteur`
pixels (8 par défaut) est une SURFACE, et le script la signale.

Les couleurs vives (clips, accents ambre, barres de vumètre) sont saturées : elles
ne sont pas comptées, et c'est voulu — ce sont des marques, pas des fonds.

Usage : surfaces-claires.py photo.png [--suite N] [--hauteur N]
Rend 0 si aucune surface claire, 1 sinon, 2 si l'image est illisible.
"""
import sys

import numpy as np
from PIL import Image

args: list[str] = []
opts: dict[str, str] = {}
reste = sys.argv[1:]
while reste:
    jeton = reste.pop(0)
    if jeton.startswith("--"):
        opts[jeton] = reste.pop(0) if reste else ""
    else:
        args.append(jeton)
if not args:
    print(__doc__)
    raise SystemExit(2)

try:
    im = np.asarray(Image.open(args[0]).convert("RGB")).astype(int)
except OSError as erreur:
    print(f"image illisible : {erreur}")
    raise SystemExit(2) from erreur

# LE SEUIL EST RELATIF À LA LARGEUR DE CETTE IMAGE-CI, lue une fois l'image
# ouverte : 200 px font 9 % d'une fenêtre entière et la moitié d'un panneau.
suite_min = int(opts.get("--suite") or max(80, int(im.shape[1] * 0.4)))
hauteur_min = int(opts.get("--hauteur") or 8)

H, W, _ = im.shape
lum = 0.2126 * im[..., 0] + 0.7152 * im[..., 1] + 0.0722 * im[..., 2]
sat = im.max(axis=2) - im.min(axis=2)
gris = (lum > 130) & (sat < 30)


def plus_longue_suite(ligne):
    meilleure = courante = 0
    for v in ligne:
        courante = courante + 1 if v else 0
        if courante > meilleure:
            meilleure = courante
    return meilleure


rangees = [y for y in range(H) if plus_longue_suite(gris[y]) >= suite_min]
# LES RANGÉES RETENUES, GROUPÉES EN BANDES CONTIGUËS. Écrit sans sentinelle
# `None` : une borne qui vaut « rien » finit par être additionnée (mypy le dit,
# et c'est aussi la faute que D346 a payée à l'exécution avec `INT_MIN`).
bandes: list[tuple[int, int]] = []
for y in rangees:
    if bandes and y == bandes[-1][1] + 1:
        bandes[-1] = (bandes[-1][0], y)
    else:
        bandes.append((y, y))

surfaces = [(a, b) for a, b in bandes if b - a + 1 >= hauteur_min]
for a, b in surfaces:
    bande = im[a:b + 1]
    masque = gris[a:b + 1]
    moyenne = bande[masque].mean(axis=0) if masque.any() else np.zeros(3)
    print(f"SURFACE CLAIRE y={a}-{b} ({b - a + 1} px de haut), "
          f"couleur moyenne #{int(moyenne[0]):02x}{int(moyenne[1]):02x}{int(moyenne[2]):02x}, "
          f"luminance {lum[a:b + 1][masque].mean():.0f}")
print(f"SURFACES_CLAIRES {len(surfaces)}")
raise SystemExit(1 if surfaces else 0)
