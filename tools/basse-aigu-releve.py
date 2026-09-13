#!/usr/bin/env python3
"""D272 : relever l'aigu du stem de basse rend-il l'octave au transcripteur ?

    analyse/.venv/bin/python tools/basse-aigu-releve.py 0 6 12 18 24

POURQUOI (D269, D271). La séparation rend une basse dont l'aigu est ATTÉNUÉ, pas
retiré : au-dessus de 300 Hz elle garde 1,7 % de l'énergie de la partie jouée
(contre 25,5 %) mais en conserve la FORME, corrélée à 0,31. Or ce sont ces
partielles qui permettent de trancher une octave, et privé d'elles le
transcripteur choisit bas — 6,9 fois plus souvent que haut, là où sur le stem VRAI
il choisit haut (0,3×).

CE QUI EST MESURÉ. Le stem `bass` est transcrit tel quel (témoin, 0 dB), puis avec
la bande au-dessus de 300 Hz relevée de N décibels. UNE variable, le même code, le
gain en ligne de commande — jamais une constante éditée entre deux passes. Les
notes obtenues sont comparées à la vérité du morceau : bonne hauteur, octave trop
bas, octave trop haut, et la part INVENTÉE, qui est le contrôle. Relever l'aigu
relève aussi les fuites des autres instruments : sans ce contrôle, on gagnerait des
octaves en perdant tout le reste, et le chiffre de bonne hauteur ne le dirait pas.
"""
from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path

import numpy as np
import soundfile as sf

RACINE = Path(__file__).resolve().parent.parent
LOT = RACINE / "reconstruction/travail/r1f-13sep"
SRC = RACINE / "reconstruction/travail/s1-sec"
TOLERANCE = 0.06
COUPURE = 300.0


def releve(x: np.ndarray, sr: float, gain_db: float) -> np.ndarray:
    """Relève la bande au-dessus de COUPURE de `gain_db`, laisse le grave intact."""
    if gain_db == 0.0:
        return x
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    facteur = np.where(f >= COUPURE, 10.0 ** (gain_db / 20.0), 1.0)
    y = np.fft.irfft(X * facteur, n=len(x))
    crete = float(np.max(np.abs(y)))
    # NORMALISÉ SI ÇA DÉBORDE, et c'est dit : un signal écrêté ne transcrit pas ce
    # qu'on croit, et l'écrêtage se confondrait avec l'effet du relevé.
    return y / crete * 0.99 if crete > 0.99 else y


def mesurer(gain_db: float) -> dict[str, int]:
    from basic_pitch import ICASSP_2022_MODEL_PATH
    from basic_pitch.inference import predict

    c = {"juste": 0, "bas": 0, "haut": 0, "autre": 0, "inventee": 0, "total": 0}
    for d in sorted(LOT.glob("morceau-*")):
        stem = d / "stems-separes" / "stems" / "bass.wav"
        verite = SRC / d.name / "verite.json"
        if not (stem.is_file() and verite.is_file()):
            continue
        v = json.loads(verite.read_text(encoding="utf-8"))
        vraies = sorted((float(n[2]), int(n[0])) for p in v.get("parties", [])
                        if p.get("role") != "batterie" for n in p.get("notes", []))
        x, sr = sf.read(str(stem), always_2d=True)
        y = releve(x.mean(axis=1), float(sr), gain_db)
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=True) as f:
            sf.write(f.name, y, int(sr))
            _, _, evts = predict(f.name, model_or_model_path=ICASSP_2022_MODEL_PATH)
        for e in evts:
            t, h = float(e[0]), int(e[2])
            c["total"] += 1
            proches = [hv for tv, hv in vraies if abs(tv - t) <= TOLERANCE]
            if not proches:
                c["inventee"] += 1
            elif h in proches:
                c["juste"] += 1
            elif any(hv - h in (12, 24) for hv in proches):
                c["bas"] += 1
            elif any(hv - h in (-12, -24) for hv in proches):
                c["haut"] += 1
            else:
                c["autre"] += 1
    return c


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("gains", nargs="+", type=float,
                   help="les relevés en dB ; le PREMIER est le témoin (0 = la chaîne d'aujourd'hui)")
    a = p.parse_args()
    print(f"stem « bass » séparé, relevé au-dessus de {COUPURE:.0f} Hz, "
          f"tolérance {TOLERANCE * 1000:.0f} ms")
    print(f"{'gain':>7} {'écrites':>8} {'justes':>8} {'8ve bas':>8} {'8ve haut':>9} "
          f"{'bas/haut':>9} {'bonne h.':>9} {'inventées':>10}")
    for i, g in enumerate(a.gains):
        c = mesurer(g)
        apparie = c["juste"] + c["bas"] + c["haut"] + c["autre"]
        rapport = f"{c['bas'] / c['haut']:.1f}x" if c["haut"] else "—"
        bonne = f"{100 * c['juste'] / apparie:.1f}%" if apparie else "—"
        inv = f"{100 * c['inventee'] / c['total']:.1f}%" if c["total"] else "—"
        marque = "  (témoin)" if i == 0 else ""
        print(f"{g:6.0f}dB {c['total']:8d} {c['juste']:8d} {c['bas']:8d} {c['haut']:9d} "
              f"{rapport:>9} {bonne:>9} {inv:>10}{marque}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
