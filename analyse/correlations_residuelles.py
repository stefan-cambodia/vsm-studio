#!/usr/bin/env python3
"""Les corrélations de la boucle résiduelle, lues sur un PROJET DÉJÀ RECONSTRUIT.

La boucle ne soustrait une unité que si son rendu, aligné, est corrélé à son
stem au-delà d'un seuil (docs/CDC-separation-par-synthese.md § 2.3). Sur un
disque, rejouer la chaîne entière pour connaître ces corrélations coûte deux
à trois heures ; le projet témoin les porte déjà. Ce script rend chaque unité
du projet (`vsm-render --stems --stems-par groupe` : les pièces d'une
batterie ensemble, les voix d'un stem ensemble), l'aligne sur le mélange par
le MÊME code que la boucle (`analyzer.vsm_residu`), et publie la corrélation
au stem séparé, celle au reste, le décalage, le gain — ce que le garde-fou
lirait. C'est la contre-mesure des deux disques de la campagne R1.

    analyse/.venv/bin/python analyse/correlations_residuelles.py \\
        reconstruction/travail/sky-parite-m9-sv1 reconstruction/travail/sky-6s \\
        reconstruction/travail/sources/sky-and-sand.wav --sortie sky-correlations.json
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_engine import find_vsm_render  # noqa: E402
from analyzer.vsm_residu import aligner, correlation, decaler  # noqa: E402
from reconstruire import SAMPLE_RATE, charger_audio, lire_wav, stem_de_la_piste  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("projet", type=Path, help="dossier du projet reconstruit (project.json, rapport.json)")
    ap.add_argument("stems", type=Path, help="dossier des stems séparés que la chaîne a jugés")
    ap.add_argument("source", type=Path, help="le morceau d'origine (wav, mp3…)")
    ap.add_argument("--moteur", default=None, help="chemin de vsm-render")
    ap.add_argument("--sortie", type=Path, default=None, help="rapport JSON")
    args = ap.parse_args()

    depart = time.time()
    rapport = json.loads((args.projet / "rapport.json").read_text(encoding="utf-8"))
    parts = {p["stem"]: float(p["partEnergie"]) for p in rapport.get("partage", [])}
    distances = {s["name"]: s.get("trackDistance") for s in rapport.get("stems", [])}
    distance_batterie = (rapport.get("drums") or {}).get("trackDistance")

    print(f"lecture de {args.source.name}")
    melange = charger_audio(args.source)
    print(f"  {melange.size / SAMPLE_RATE:.1f} s")
    with tempfile.TemporaryDirectory(prefix="vsm-correlations-") as temporaire:
        dossier = Path(temporaire) / "unites"
        # `--stems` fait du SECOND argument un dossier : un WAV par unité
        # (« 05 - Batterie.wav » pour un groupe, « 01 - bass.wav » pour une
        # piste seule), tout le reste muet.
        commande = [str(find_vsm_render(args.moteur)), str(args.projet), str(dossier),
                    "--stems", str(dossier), "--stems-par", "groupe", "--sample-rate", str(SAMPLE_RATE),
                    "--quiet"]
        print(f"rendu des unités : {' '.join(commande)}")
        subprocess.run(commande, check=True)
        fichiers = sorted(dossier.glob("*.wav"))
        resultats = []
        for fichier in fichiers:
            nom = re.sub(r"^\d+ - ", "", fichier.stem)
            stem = stem_de_la_piste(nom)
            if stem == "vocals" or nom.startswith("Voix"):
                print(f"  {nom:24s} piste audio : pas une unité")
                continue
            chemin_stem = args.stems / f"{stem}.wav"
            if not chemin_stem.exists():
                print(f"  {nom:24s} stem « {stem} » introuvable dans {args.stems}")
                continue
            rendu = lire_wav(fichier)
            reference = lire_wav(chemin_stem)
            if not np.any(rendu):
                print(f"  {nom:24s} rendu MUET")
                continue
            decalage, gain = aligner(melange, rendu)
            rendu_d = decaler(rendu, decalage, melange.size)
            n = min(melange.size, reference.size)
            reste = melange[:n].astype(np.float64) - reference[:n].astype(np.float64)
            membres = [t for t in distances if t == nom or t.startswith(nom + " · ")]
            if stem == "drums":
                distance = distance_batterie
            else:
                connues = [distances[t] for t in membres if distances.get(t) is not None]
                distance = float(np.mean(connues)) if connues else None
            fiche = {
                "unite": nom, "stem": stem, "part": parts.get(stem), "distance": distance,
                "score": (parts.get(stem, 0.0) / distance) if distance else None,
                "decalageEchantillons": int(decalage), "decalageMs": round(1000.0 * decalage / SAMPLE_RATE, 2),
                "gain": gain, "correlationStem": correlation(reference, rendu_d),
                "correlationReste": correlation(reste, rendu_d[:n]),
            }
            resultats.append(fiche)
            print(f"  {nom:24s} part {fiche['part']!s:>5s} % distance {distance if distance is None else round(distance, 4)!s:>7s} "
                  f"corr. stem {fiche['correlationStem']:.3f} reste {fiche['correlationReste']:.3f} "
                  f"gain {gain:.3f} décalage {decalage:+d} éch. ({fiche['decalageMs']:+.2f} ms)")
    meilleure = max(resultats, key=lambda f: f["correlationStem"], default=None)
    print(f"meilleure corrélation au stem : "
          + (f"{meilleure['correlationStem']:.3f} ({meilleure['unite']})" if meilleure else "aucune unité")
          + f" — seuil publié 0,5 ; {time.time() - depart:.0f} s")
    if args.sortie:
        args.sortie.write_text(json.dumps({
            "projet": str(args.projet), "stems": str(args.stems), "source": str(args.source),
            "commit": rapport.get("provenance", {}).get("commit"), "unites": resultats,
            "seuilPublie": 0.5, "secondes": time.time() - depart,
        }, indent=1, ensure_ascii=False), encoding="utf-8")
        print(f"rapport : {args.sortie}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
