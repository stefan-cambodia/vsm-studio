#!/usr/bin/env python3
"""D278 : le SOUS-OSCILLATEUR explique-t-il l'octave trop basse de la basse ?

    analyse/.venv/bin/python tools/sous-oscillateur.py 0 0.2 0.4 0.6 0.8

POURQUOI (D269). Le transcripteur écrit les basses une octave trop bas 6,9 fois
plus souvent que trop haut, après séparation. Une explication tentante : une basse
de synthèse porte souvent un sous-oscillateur — une octave EN DESSOUS, mélangée au
signal — et le transcripteur, s'il l'entend, n'a pas tort : elle sonne.

L'hypothèse n'avait pas pu être tranchée sur le corpus : **une seule de ses neuf
parties de basse porte un sous-oscillateur**, et la corrélation qui en sortait
(+1,000) portait sur un point unique — une coïncidence écrite en décimales. Et
elle ne le sera pas davantage par un corpus neuf : **seules trois machines du parc
exposent `oscillator.sub.level`** (`vsm.generic`, `vsm.supersaw`, `vsm.sh101`).

CE BANC N'A PAS BESOIN DU CORPUS, IL A BESOIN D'UN TÉMOIN. La MÊME ligne de basse,
la MÊME machine, le MÊME patch : une seule variable, le niveau du sous-oscillateur,
donné en ligne de commande. Le premier niveau est le témoin.

CE QUI EST COMPTÉ : pour chaque note transcrite appariée à une note jouée (±60 ms),
elle est JUSTE, une octave TROP BAS, une octave TROP HAUT, ou autre. Et le nombre
de notes écrites est publié avec : un sous-oscillateur fort qui rendrait la ligne
inaudible ferait mesurer une disparition, pas un déplacement.
"""
from __future__ import annotations

import argparse
import sys
import tempfile
from collections import Counter
from pathlib import Path

import numpy as np
import soundfile as sf

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE / "analyse"))

TAUX = 44100
TOLERANCE = 0.06

# UNE LIGNE DE BASSE ORDINAIRE : une octave de va-et-vient, des noires à 110 BPM,
# dans le registre où une basse vit vraiment (mi1 à mi2). Écrite ici pour que le
# banc ne dépende d'aucun fichier voisin.
LIGNE = [40, 40, 47, 45, 40, 40, 43, 45, 38, 38, 45, 43, 38, 38, 41, 43]
DUREE_NOTE = 0.55
PAS = 0.60


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("niveaux", nargs="+", type=float,
                   help="les oscillator.sub.level à comparer ; le PREMIER est le témoin")
    p.add_argument("--machine", default="vsm.supersaw",
                   help="une des trois machines à sous-oscillateur "
                        "(vsm.supersaw, vsm.generic, vsm.sh101)")
    a = p.parse_args()

    from analyzer.vsm_engine import Note, VsmEngine
    from basic_pitch import ICASSP_2022_MODEL_PATH
    from basic_pitch.inference import predict

    notes = [Note(h, 100, i * PAS, DUREE_NOTE) for i, h in enumerate(LIGNE)]
    duree = len(LIGNE) * PAS + 1.0
    vraies = sorted((i * PAS, h) for i, h in enumerate(LIGNE))

    print(f"machine {a.machine}, {len(LIGNE)} notes de basse, "
          f"tolérance {TOLERANCE * 1000:.0f} ms")
    print(f"{'sub.level':>10} {'écrites':>8} {'justes':>7} {'8ve bas':>8} {'8ve haut':>9} "
          f"{'autre':>7} {'part 8ve bas':>13}")

    with VsmEngine(sample_rate=TAUX) as moteur:
        for i, niveau in enumerate(a.niveaux):
            patch = {"oscillator.sub.level": float(niveau)}
            son = moteur.render(a.machine, patch, notes, duration=duree, sample_rate=TAUX)
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=True) as f:
                sf.write(f.name, np.asarray(son, dtype="float32"), TAUX)
                _, _, evts = predict(f.name, model_or_model_path=ICASSP_2022_MODEL_PATH)
            c: Counter[str] = Counter()
            for e in evts:
                t, h = float(e[0]), int(e[2])
                proches = [hv for tv, hv in vraies if abs(tv - t) <= TOLERANCE]
                if not proches:
                    continue
                if h in proches:
                    c["juste"] += 1
                elif any(hv - h in (12, 24) for hv in proches):
                    c["bas"] += 1
                elif any(hv - h in (-12, -24) for hv in proches):
                    c["haut"] += 1
                else:
                    c["autre"] += 1
            apparie = sum(c.values())
            part = f"{100 * c['bas'] / apparie:.1f}%" if apparie else "—"
            marque = "  (témoin)" if i == 0 else ""
            print(f"{niveau:10.2f} {len(evts):8d} {c['juste']:7d} {c['bas']:8d} "
                  f"{c['haut']:9d} {c['autre']:7d} {part:>13}{marque}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
