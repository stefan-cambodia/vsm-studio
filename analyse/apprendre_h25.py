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


def _stem_notes(notes: Sequence[Sequence[float]]) -> List[StemNote]:
    """Les notes de la vérité, dans le type que `registres_par_vides` attend."""
    return [StemNote(note=int(n[0]), velocity=int(n[1]), start=float(n[2]), duration=float(n[3]))
            for n in notes]


def _pistes_du_stem_other(data: dict) -> List[Tuple[int, list]]:
    """Les parties qui atterrissent dans le stem « other », avec leur indice.

    C'EST LA CLÉ DE CE JEU DE DONNÉES, et le premier essai s'était trompé. La
    chaîne ne pose JAMAIS la question sur une partie isolée : elle la pose sur
    le stem `other` ENTIER, qui porte plusieurs parties additionnées — c'est
    même la seule raison pour laquelle `registres_par_vides` existe. Une partie
    seule ne passe pas le garde-fou du fourre-tout (trois notes simultanées ET
    trois octaves), et le premier jet a donc produit ZÉRO paire sur
    quatre-vingts morceaux. Mesure faite, cause comprise, jeu refait.
    """
    pistes: List[Tuple[int, list]] = []
    for i, partie in enumerate(data.get("parties", [])):
        role = str(partie.get("role", ""))
        # `basse` et `batterie` ont leurs propres stems : ce qui reste est
        # `other`, et c'est là que le fourre-tout se forme.
        if role in ("basse", "batterie"):
            continue
        notes = partie.get("notes") or []
        if notes:
            pistes.append((i, list(notes)))
    return pistes


def _paires_du_morceau(data: dict) -> List[Tuple[np.ndarray, int]]:
    """Les paires de registres voisins que la chaîne poserait, étiquetées.

    L'étiquette est connue PAR CONSTRUCTION : chaque note sait de quelle partie
    elle vient. Un registre appartient à la partie qui y pèse le plus (en
    durée) ; deux registres voisins dominés par la MÊME partie sont un seul
    instrument, et c'est exactement la question d'H25.
    """
    pistes = _pistes_du_stem_other(data)
    if len(pistes) < 1:
        return []
    origine: Dict[Tuple[int, int, int], int] = {}
    toutes: List[list] = []
    for indice, notes in pistes:
        for n in notes:
            cle = (int(n[0]), int(round(float(n[2]) * 1000)), int(round(float(n[3]) * 1000)))
            origine.setdefault(cle, indice)
            toutes.append(list(n))
    registres = registres_par_vides(_stem_notes(toutes))
    if len(registres) < 2:
        return []

    def dominante(registre) -> Optional[int]:
        poids: Dict[int, float] = {}
        for n in registre:
            cle = (int(n.note), int(round(float(n.start) * 1000)), int(round(float(n.duration) * 1000)))
            src = origine.get(cle)
            if src is None:
                continue
            poids[src] = poids.get(src, 0.0) + max(1e-6, float(n.duration))
        if not poids:
            return None
        return max(poids, key=lambda k: (poids[k], -k))

    conv = lambda r: [[n.note, n.velocity, n.start, n.duration] for n in r]
    exemples: List[Tuple[np.ndarray, int]] = []
    # Les registres sont rendus de l'aigu au grave.
    for k in range(len(registres) - 1):
        haut, bas = registres[k], registres[k + 1]
        da, db = dominante(haut), dominante(bas)
        if da is None or db is None:
            continue
        etiquette = 1 if da == db else 0
        exemples.append((descripteurs(conv(bas), conv(haut)).vecteur(), etiquette))
    return exemples


def _exemples_du_lot(dossier: Path, cas: Optional[str]) -> Tuple[List[np.ndarray], List[int], int, int]:
    """Rend (vecteurs, étiquettes, morceaux lus, morceaux que les vides ne
    découpent pas)."""
    X: List[np.ndarray] = []
    y: List[int] = []
    lus = 0
    non_decoupes = 0
    for verite in sorted(dossier.glob("*/verite.json")):
        data = json.loads(verite.read_text(encoding="utf-8"))
        if cas is not None and data.get("cas") != cas:
            continue
        lus += 1
        paires = _paires_du_morceau(data)
        if not paires:
            non_decoupes += 1
            continue
        for vecteur, etiquette in paires:
            X.append(vecteur)
            y.append(etiquette)
    return X, y, lus, non_decoupes


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mains", type=Path, required=True, help="lot --cas deux-mains")
    ap.add_argument("--disjoints", type=Path, required=True, help="lot --cas memes-machine-disjoints")
    ap.add_argument("--modele", type=Path, default=None, help="où écrire le modèle s'il tient")
    args = ap.parse_args()

    Xm, ym, lus_m, sans_m = _exemples_du_lot(args.mains, None)
    Xd, yd, lus_d, sans_d = _exemples_du_lot(args.disjoints, None)

    print(f"lot deux-mains          : {lus_m} morceaux, {len(Xm)} paires posées, "
          f"{sans_m} non découpés par les vides")
    print(f"lot registres disjoints : {lus_d} morceaux, {len(Xd)} paires posées, "
          f"{sans_d} non découpés par les vides")
    if len(Xm) < 5 or len(Xd) < 5:
        print("PAS ASSEZ D'EXEMPLES pour conclure : le modèle reste désactivé.")
        return 1

    X = np.vstack(Xm + Xd)
    y = np.asarray(ym + yd, dtype=int)

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
