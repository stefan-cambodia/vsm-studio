#!/usr/bin/env python3
"""H44 — le seuil d'attaque de Basic Pitch, balayé ENTIER (docs/CDC-reload-indifferenciable.md § 6).

La sortie du modèle est calculée UNE fois par stem ; les notes sont redérivées à
chaque seuil par `basic_pitch.note_creation.model_output_to_notes`, les autres
réglages étant ceux d'usine (ceux de `predict`, donc de la chaîne).

  1. « Reload », stem « other » : notes du pad (MIDI 58-73) et leur durée médiane ;
  2. le contrôle : S2, stems VRAIS des parties mélodiques des morceaux demandés, F1
     note à note (même hauteur, attaque à ± 50 ms, appariement un à un), par rôle.

    python analyse/verdict_h44.py STEM_OTHER_RELOAD CORPUS_S2 morceau-0001-g1 morceau-0002-g2
"""

from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

SEUILS = (0.5, 0.6, 0.7, 0.8, 0.9)


def notes_aux_seuils(chemin: Path) -> dict:
    import basic_pitch.note_creation as infer
    from basic_pitch import ICASSP_2022_MODEL_PATH
    from basic_pitch.constants import AUDIO_SAMPLE_RATE, FFT_HOP
    from basic_pitch.inference import run_inference
    sortie = run_inference(str(chemin), ICASSP_2022_MODEL_PATH)
    min_len = int(np.round(127.7 / 1000 * (AUDIO_SAMPLE_RATE / FFT_HOP)))
    rendu = {}
    for s in SEUILS:
        _, ev = infer.model_output_to_notes(sortie, onset_thresh=s, frame_thresh=0.3, min_note_len=min_len,
                                            min_freq=None, max_freq=None, multiple_pitch_bends=False,
                                            melodia_trick=True, midi_tempo=120)
        rendu[s] = [(float(e[0]), float(e[1]), int(e[2])) for e in ev if e[1] > e[0]]
    return rendu


def f1(vraies: list, trouvees: list, tol: float = 0.05) -> tuple:
    par_h = defaultdict(list)
    for d, _f, h in trouvees:
        par_h[h].append(d)
    for h in par_h:
        par_h[h].sort()
    utilisees = defaultdict(set)
    touches = 0
    # `vraies` : (attaque, hauteur). La première course lisait (hauteur, attaque) et
    # rendait un F1 de 0,0 PARTOUT — un zéro qui ne mesurait que le dépaquetage.
    for d, h in vraies:
        cands = par_h.get(h, [])
        meilleur, ecart = -1, tol
        for i, t in enumerate(cands):
            if i in utilisees[h]:
                continue
            if abs(t - d) <= ecart:
                meilleur, ecart = i, abs(t - d)
        if meilleur >= 0:
            utilisees[h].add(meilleur)
            touches += 1
    p = touches / max(len(trouvees), 1)
    r = touches / max(len(vraies), 1)
    return touches, len(vraies), len(trouvees), (2 * p * r / (p + r) if p + r else 0.0)


def main() -> int:
    stem_reload, corpus, morceaux = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3:]
    print("=== 1. « Reload », stem « other », pad MIDI 58-73")
    reload_notes = notes_aux_seuils(stem_reload)
    base = [n for n in reload_notes[0.5] if 58 <= n[2] <= 73]
    for s in SEUILS:
        pad = [n for n in reload_notes[s] if 58 <= n[2] <= 73]
        dur = np.median([f - d for d, f, _ in pad]) if pad else 0.0
        print(f"  seuil {s:.1f} : {len(pad):5d} notes ({100 * (len(pad) - len(base)) / max(len(base), 1):+5.0f} %), "
              f"durée médiane {dur:.3f} s (×{dur / np.median([f - d for d, f, _ in base]):.2f})")
    print("=== 2. S2, F1 note à note par rôle (même hauteur, attaque ± 50 ms)")
    par_role = defaultdict(lambda: defaultdict(lambda: [0, 0, 0]))
    for nom in morceaux:
        verite = json.loads((corpus / nom / "verite.json").read_text(encoding="utf-8"))
        for partie in verite["parties"]:
            if partie.get("pieces") or not partie.get("notes"):
                continue
            stem = corpus / nom / partie["fichier"]
            if not stem.is_file():
                print(f"  {nom} {partie['role']} : stem ABSENT — non mesurée")
                continue
            vraies = [(float(d), int(h)) for h, _v, d, _du in partie["notes"]]
            par_seuil = notes_aux_seuils(stem)
            for s in SEUILS:
                t, nv, nt, _ = f1(vraies, par_seuil[s])
                acc = par_role[partie["role"]][s]
                acc[0] += t
                acc[1] += nv
                acc[2] += nt
    print("  rôle".ljust(22) + "".join(f"  {s:.1f}".rjust(9) for s in SEUILS))
    for role, par in sorted(par_role.items()):
        ligne = f"  {role:20s}"
        for s in SEUILS:
            t, nv, nt = par[s]
            p, r = t / max(nt, 1), t / max(nv, 1)
            ligne += f"  {100 * (2 * p * r / (p + r) if p + r else 0.0):6.1f}"
        print(ligne)
    return 0


if __name__ == "__main__":
    sys.exit(main())
