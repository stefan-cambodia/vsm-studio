#!/usr/bin/env python3
# RÈGLE (D325) : les notes doivent SE VOIR dans un clip MIDI de l'arrangement. Sur une
# photo (VSM_CAPTURE) de l'arrangement, ce script balaie une colonne, trouve les bandes
# de clip, et rend pour chacune la part d'encre (pixels plus sombres que le fond) et
# le contraste WCAG fond / encre médiane. Usage : encre-clips.py photo.png [x_sonde]
# (défaut 1200 ; la bande mesurée va de x=470 à 1720, à 2117 px de large).
import sys

import numpy as np
from PIL import Image
from collections import Counter
im = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(int)
H, W, _ = im.shape
x_sonde = int(sys.argv[2]) if len(sys.argv) > 2 else 1200
x0, x1 = 470, 1720
def lum(c):
    r, g, b = [ (v/255)/12.92 if v/255 <= 0.03928 else ((v/255+0.055)/1.055)**2.4 for v in c ]
    return 0.2126*r + 0.7152*g + 0.0722*b
def ratio(a, b):
    la, lb = lum(a), lum(b)
    la, lb = max(la, lb), min(la, lb)
    return (la+0.05)/(lb+0.05)
col = im[:, x_sonde, :]
# segmenter la colonne en runs de couleur saturée (bande de clip) : distance au gris fond > 60.
# Un trait de note sombre (2 px depuis D325) casse la saturation : les trous de
# moins de 5 px sont refermés (l'interligne des pistes fait 7 px), sans quoi la bande mesurée dépend de l'encre.
masque = [ (max(c) - min(c)) > 60 for c in col ]
y = 0
while y < H:
    if not masque[y]:
        d = y
        while y < H and not masque[y]:
            y += 1
        if d > 0 and y < H and y - d < 5:
            for k in range(d, y):
                masque[k] = True
    else:
        y += 1
runs = []
debut = None
for y in range(H):
    if masque[y]:
        if debut is None:
            debut = y
    else:
        if debut is not None and y - debut >= 30:
            runs.append((debut, y))
        debut = None
for (a, b) in runs:
    bande = im[a+17:b-3, x0:x1, :].reshape(-1, 3)
    mode = Counter(map(tuple, bande)).most_common(1)[0][0]
    lm = lum(mode)
    lums = np.array([lum(tuple(p)) for p in bande[::7]])   # sous-échantillon
    encre = lums < lm - 0.04
    part = 100.0 * encre.mean()
    if encre.any():
        med = float(np.median(lums[encre]))
        r = (lm+0.05)/(med+0.05)
    else:
        r = 1.0
    print(f"y={a:4d}-{b:4d} fond=#{mode[0]:02x}{mode[1]:02x}{mode[2]:02x} encre={part:5.2f} %  contraste={r:4.2f}")
