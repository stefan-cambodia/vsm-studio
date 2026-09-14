#!/usr/bin/env python3
"""RÈGLE (D327) : une courbe d'automation doit occuper sa lane, pas son dixième du
bas. Sur une photo (VSM_CAPTURE) de l'onglet Automation, ce script trouve les
POINTS ambre de la courbe dans la bande de la lane et publie leur étalement
vertical : médiane et écart p10-p90 en % de la hauteur de la bande. Usage :
etalement-automation.py photo.png [y1,y2 de la bande] (défaut 1100,1310 à 2117 × 1317)."""
import sys, numpy as np
from PIL import Image
im = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(int)
y1, y2 = (int(v) for v in (sys.argv[2] if len(sys.argv) > 2 else "1100,1310").split(','))
bande = im[y1:y2, 460:, :]
r, g, b = bande[..., 0], bande[..., 1], bande[..., 2]
ambre = (r > 200) & (g > 140) & (g < 210) & (b < 140)     # les points de la courbe (Palette::accentAmber)
ys, xs = np.nonzero(ambre)
if len(ys) < 10:
    print("aucun point ambre trouvé"); sys.exit(2)
h = y2 - y1
haut = 100.0 * (h - ys) / h        # 0 % = bas de la lane, 100 % = haut
p10, p50, p90 = np.percentile(haut, [10, 50, 90])
print(f"points ambre : {len(ys)} px ; hauteur médiane {p50:5.1f} % ; p10-p90 {p10:5.1f}-{p90:5.1f} % (étalement {p90-p10:5.1f} %)")
