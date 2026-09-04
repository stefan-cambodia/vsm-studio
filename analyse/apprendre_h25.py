#!/usr/bin/env python3
"""H25 APPRIS : construire le jeu, apprendre, et MESURER contre le découpage
par les vides (CDC-banc-synthetique § 8).

Les attendus sont écrits dans le § 8, AVANT cette mesure. Ce script ne fait que
les trancher, et il publie ses chiffres qu'ils soient bons ou mauvais : « s'il
n'y arrive pas, le code reste désactivé, le chiffre se publie ».

CE QU'IL LIT : les `verite.json` de deux lots générés (`--cas deux-mains` et
`--cas memes-machine-disjoints`), où la réponse est connue PAR CONSTRUCTION.

CE QU'IL DÉCOUPE : les paires de registres sont obtenues par
`registres_par_vides`, c'est-à-dire par le MÊME découpage que la chaîne
emploie. Entraîner sur des paires obtenues autrement donnerait un modèle qui
n'a jamais vu ce qu'on lui demandera de juger.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyzer.vsm_deux_mains import (DESCRIPTEURS, Modele, descripteurs,  # noqa: E402
                                      empreinte_des_exemples)
from analyzer.vsm_reconstruct import StemNote, registres_par_vides  # noqa: E402

GRAINE = 20260905


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jeu", type=Path, required=True,
                    help="le jeu construit par jeu_h25.py depuis les COURSES (.npz)")
    ap.add_argument("--modele", type=Path, default=None, help="où écrire le modèle s'il tient")
    args = ap.parse_args()

    donnees = np.load(args.jeu)
    X, y = donnees["X"], donnees["y"].astype(int)
    n_un, n_deux = int(np.sum(y == 1)), int(np.sum(y == 0))
    print(f"jeu : {X.shape[0]} paires ({n_un} « un seul », {n_deux} « deux »), "
          f"{X.shape[1]} descripteurs")
    # DEUX CLASSES, ET ASSEZ DE CHACUNE : en dessous, la validation croisée
    # stratifiée n'a plus de sens et un score n'est plus une mesure.
    if n_un < 5 or n_deux < 5:
        print("PAS ASSEZ D'EXEMPLES D'UNE DES DEUX CLASSES pour conclure : "
              "le modèle reste désactivé, et c'est le chiffre qui se publie.")
        return 1

    # LE DÉPART À BATTRE : le découpage par les vides dit TOUJOURS « deux ».
    # Sur les paires posées, il a donc raison sur les disjoints et tort sur
    # toutes les deux-mains.
    base_mains = 0.0
    base_disjoints = 1.0
    print(f"\ndépart (les vides seuls) : « un seul » sur {base_mains:.0%} des deux-mains, "
          f"« deux » sur {base_disjoints:.0%} des disjoints")

    print("\ndescripteurs, moyenne par classe :")
    for i, nom in enumerate(DESCRIPTEURS):
        print(f"  {nom:22s} un seul {np.mean(X[y == 1, i]):+.3f}   deux {np.mean(X[y == 0, i]):+.3f}")

    from sklearn.linear_model import LogisticRegression
    from sklearn.model_selection import StratifiedKFold, cross_val_predict
    from sklearn.pipeline import make_pipeline
    from sklearn.preprocessing import StandardScaler

    modele = make_pipeline(StandardScaler(),
                            LogisticRegression(C=1.0, max_iter=2000, random_state=GRAINE))
    plis = StratifiedKFold(n_splits=5, shuffle=True, random_state=GRAINE)
    # VALIDATION CROISÉE, jamais le score sur ce qu'on a appris : avec six
    # descripteurs et quatre-vingts exemples, apprendre par cœur est facile.
    predit = cross_val_predict(modele, X, y, cv=plis)

    juste_mains = float(np.mean(predit[y == 1] == 1))
    juste_disjoints = float(np.mean(predit[y == 0] == 0))
    print(f"\nvalidation croisée (5 plis, graine {GRAINE}) :")
    print(f"  « un seul » sur les deux-mains      : {juste_mains:.0%}  "
          f"({int(np.sum(predit[y == 1] == 1))}/{int(np.sum(y == 1))})")
    print(f"  « deux » sur les registres disjoints : {juste_disjoints:.0%}  "
          f"({int(np.sum(predit[y == 0] == 0))}/{int(np.sum(y == 0))})")

    # LES DEUX CONDITIONS DU § 8, ET ELLES SE LISENT ENSEMBLE : dire « un seul »
    # plus souvent que le départ NE VAUT RIEN si l'on se met à fondre les
    # registres disjoints, que S1 fond déjà trois fois sur trois.
    mieux = juste_mains > base_mains
    sans_degat = juste_disjoints >= base_disjoints
    print(f"\n  bat le départ sur les deux-mains   : {'OUI' if mieux else 'NON'}")
    print(f"  sans dégrader les disjoints        : {'OUI' if sans_degat else 'NON'}")

    modele.fit(X, y)
    lr = modele.named_steps["logisticregression"]
    echelle = modele.named_steps["standardscaler"]
    coefs = (lr.coef_[0] / echelle.scale_).tolist()
    biais = float(lr.intercept_[0] - float(np.dot(lr.coef_[0] / echelle.scale_, echelle.mean_)))
    final = Modele(coefs, biais, 0.5, list(DESCRIPTEURS),
                    empreinte_des_exemples(X, y), int(X.shape[0]), GRAINE)
    print(f"\n  empreinte du jeu : {final.empreinte}  ({final.exemples} exemples)")

    if args.modele is not None and mieux and sans_degat:
        args.modele.parent.mkdir(parents=True, exist_ok=True)
        args.modele.write_text(json.dumps(final.to_json(), indent=2, ensure_ascii=False),
                                encoding="utf-8")
        print(f"  modèle écrit : {args.modele}")
    elif args.modele is not None:
        print("  modèle NON écrit : il ne tient pas les deux conditions du § 8.")
    return 0 if (mieux and sans_degat) else 2


if __name__ == "__main__":
    raise SystemExit(main())
