#!/usr/bin/env python3
"""H43, attendu 2 — CE QUE LA RÉUNION DES TENUES CASSE, compté sur la vérité de S2.

docs/CDC-reload-indifferenciable.md § 5. Pour chaque partie mélodique des morceaux
demandés : le stem VRAI est transcrit par la fonction de la chaîne, la réunion est
appliquée au seuil écrit (3 dB), et chaque jonction réunie est confrontée à la
vérité :

  - À TORT : la jonction tombe sur la frontière de deux notes VRAIES de même
    hauteur, contiguës à moins de 30 ms (± 40 ms de tolérance d'attaque) — une
    note rejouée que la règle a fondue avec la précédente ;
  - RÉPARATION : la jonction tombe à l'intérieur d'une note vraie (à plus de 40 ms
    de ses bords) — un fragment que la transcription avait coupé.

Le taux publié est celui de l'attendu : paires vraies contiguës réunies à tort /
paires vraies contiguës. Réussite ≤ 5 %, échec > 10 %.

    python analyse/verdict_h43.py reconstruction/travail/s2 morceau-0001-g1 morceau-0002-g2
"""

from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.note_extraction import extract_notes  # noqa: E402
from analyzer.tenues import reunir_tenues  # noqa: E402
from analyzer.vsm_reconstruct import StemNote  # noqa: E402

TOLERANCE = 0.040


def main() -> int:
    corpus = Path(sys.argv[1])
    morceaux = sys.argv[2:]
    total_paires = total_tort = total_rep = total_jonctions = 0
    for nom in morceaux:
        dossier = corpus / nom
        verite = json.loads((dossier / "verite.json").read_text(encoding="utf-8"))
        for partie in verite["parties"]:
            if partie.get("pieces") or not partie.get("notes"):
                continue
            stem = dossier / partie["fichier"]
            if not stem.is_file():
                print(f"  {nom} {partie['role']:18s} : stem ABSENT ({stem}) — non mesurée")
                continue
            y, sr = sf.read(str(stem), dtype="float32", always_2d=True)
            mono = y.mean(axis=1)
            brutes = extract_notes(stem)
            notes = [StemNote(note=int(b["midi"]), velocity=100, start=float(b["start"]),
                              duration=float(b["end"]) - float(b["start"]), confidence=float(b["confidence"]))
                     for b in brutes if b["end"] > b["start"]]
            _, bilan = reunir_tenues(notes, mono, sr, garder_jonctions=True)
            vraies = defaultdict(list)
            for h, _vel, debut, duree in partie["notes"]:
                vraies[int(h)].append((float(debut), float(debut) + float(duree)))
            frontieres = defaultdict(list)   # début de la seconde note d'une paire vraie contiguë
            paires = 0
            for h, liste in vraies.items():
                liste.sort()
                for (_a0, a1), (b0, _b1) in zip(liste, liste[1:], strict=False):
                    if b0 - a1 < 0.030:
                        frontieres[h].append(b0)
                        paires += 1
            tort = rep = 0
            touchees = set()
            for h, t in bilan["jonctions"]:
                proches = [b for b in frontieres.get(h, []) if abs(b - t) < TOLERANCE]
                if proches:
                    tort += 1
                    touchees.add((h, min(proches, key=lambda b: abs(b - t))))
                elif any(d + TOLERANCE < t < f - TOLERANCE for d, f in vraies.get(h, [])):
                    rep += 1
            print(f"  {nom} {partie['role']:18s} : paires vraies contiguës {paires:5d}, réunies à tort "
                  f"{len(touchees):4d} ; jonctions réunies {len(bilan['jonctions']):5d} dont réparations {rep}")
            total_paires += paires
            total_tort += len(touchees)
            total_rep += rep
            total_jonctions += len(bilan["jonctions"])
    taux = 100.0 * total_tort / max(total_paires, 1)
    print(f"TOTAL : {total_tort} paires vraies réunies à tort sur {total_paires} ({taux:.2f} %) ; "
          f"{total_jonctions} jonctions réunies, dont {total_rep} réparations")
    verdict = "RÉUSSITE" if taux <= 5.0 else "ÉCHEC" if taux > 10.0 else "ENTRE LES SEUILS"
    print(f"→ attendu 2 : {verdict} (réussite ≤ 5 %, échec > 10 %)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
