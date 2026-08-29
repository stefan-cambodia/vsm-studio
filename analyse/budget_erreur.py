#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Où est l'erreur d'une reconstruction ? Le budget, piste par piste.

    .venv/bin/python budget_erreur.py ../reconstruction/travail/sky
    .venv/bin/python budget_erreur.py DOSSIER --original ../sources/morceau.wav

Une distance globale dit qu'on est à 0,2854 de l'original. Elle ne dit pas OÙ,
et sans ça on règle au hasard. Cette commande répond en deux mesures :

1. CE QUE CHAQUE STEM PÈSE — chaque stem séparé est remplacé par du silence, et
   la distance obtenue dit ce que ce stem représente dans le morceau. Elle donne
   au passage le PLANCHER de la chaîne : la distance entre l'original et la
   somme des stems, c'est-à-dire ce qu'aucune reconstruction ne pourra battre.

2. CE QUE CHAQUE PISTE COÛTE — chaque piste du projet est rendue SEULE (avec son
   volume de mixage), puis remplacée par le stem RÉEL correspondant. La distance
   obtenue dit ce qu'une reconstruction PARFAITE de cette piste-là, et d'elle
   seule, rapporterait.

CE QUE ÇA A DONNÉ LA PREMIÈRE FOIS, et pourquoi cette commande existe. Sur
*Sky and Sand* : batterie parfaite −63,5 %, basse −0,0 %, voix −0,0 %, `other`
+2,5 % (PIRE). Autrement dit, une basse parfaite ne rapporterait rien, et deux
des quatre pistes sont hors de cause — ce qu'aucune distance globale ne dit.

ET CE QUE ÇA NE DIT PAS. Le budget d'erreur désigne la piste où travailler ; il
ne dit pas COMMENT. Sur ce morceau il a désigné la batterie, et la route qui
semblait évidente — rejouer les coups découpés dans l'enregistrement plutôt
qu'une boîte modélisée — s'est révélée PIRE (+41 % sur le mélange), parce que
cette route découpe un seul échantillon par famille et le rejoue des centaines
de fois. Une piste bien désignée peut être mal réparée.

CONTRÔLE INTÉGRÉ : la somme des pistes rendues séparément doit redonner la
distance globale du rapport. Si les deux chiffres diffèrent, la décomposition
ne décrit pas le morceau qu'on croit, et le tableau ne veut rien dire — c'est
imprimé, pas supposé.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Dict, Optional

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_engine import find_vsm_render
from analyzer.vsm_reconstruct import reconstruction_distance

SAMPLE_RATE = 44100

# Le stem séparé qui correspond à chaque piste écrite par `reconstruire.py`.
# Les noms de pistes viennent de la chaîne ; les noms de stems, de demucs.
PISTE_VERS_STEM = {"bass": "bass", "other": "other",
                   "Batterie": "drums", "Voix": "vocals"}


def lire_mono(chemin: Path) -> np.ndarray:
    import soundfile

    audio, taux = soundfile.read(str(chemin), always_2d=True)
    if taux != SAMPLE_RATE:
        raise SystemExit(f"{chemin.name} est à {taux} Hz, attendu {SAMPLE_RATE} : "
                         f"deux rendus de taux différents ne se comparent pas")
    return audio.mean(axis=1).astype(np.float32)


def rendre_piste_seule(projet: Dict, dossier_projet: Path, nom: str,
                       travail: Path, duree: float, moteur: Path) -> np.ndarray:
    """La piste `nom` seule, à son volume de mixage, sur toute la durée du morceau.

    Les autres pistes sont MUETTES plutôt que retirées : le projet reste le même
    objet, avec les mêmes réglages, et seule la sortie change. La durée est
    imposée pour que toutes les pistes couvrent le même terrain -- une piste qui
    se tait avant la fin donnerait sinon un tableau plus court que les autres.
    """
    solo = dict(projet)
    solo["tracks"] = [dict(piste, mix=dict(piste["mix"], muted=(piste["name"] != nom)))
                      for piste in projet["tracks"]]
    dossier = travail / f"solo-{nom}"
    dossier.mkdir(parents=True, exist_ok=True)
    (dossier / "project.json").write_text(json.dumps(solo), encoding="utf-8")
    for annexe in ("midi", "instruments", "samples"):
        lien = dossier / annexe
        source = (dossier_projet / annexe).resolve()
        if not lien.exists() and source.exists():
            lien.symlink_to(source)
    sortie = dossier / "rendu.wav"
    subprocess.run([str(moteur), str(dossier), str(sortie),
                    "--sample-rate", str(SAMPLE_RATE),
                    "--duration", str(duree), "--quiet"], check=True)
    return lire_mono(sortie)


def aligner(x: np.ndarray, n: int) -> np.ndarray:
    return x[:n] if len(x) >= n else np.pad(x, (0, n - len(x)))


def main() -> int:
    analyseur = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    analyseur.add_argument("projet", type=Path,
                           help="dossier écrit par reconstruire.py (project.json, stems/, rapport.json)")
    analyseur.add_argument("--original", type=Path, default=None,
                           help="le morceau d'origine (défaut : déduit du rapport, sinon exigé)")
    analyseur.add_argument("--stems", type=Path, default=None,
                           help="dossier des stems séparés (défaut : PROJET/stems/stems)")
    analyseur.add_argument("--travail", type=Path, default=None,
                           help="où écrire les rendus intermédiaires")
    analyseur.add_argument("--metrique", default="v2", choices=("v1", "v2", "v3"))
    analyseur.add_argument("--moteur", default=None)
    arguments = analyseur.parse_args()

    # RÉSOLU EN ABSOLU, et ce n'est pas de la coquetterie : les rendus solo
    # sont écrits ailleurs et renvoient au projet par des liens symboliques.
    # Un chemin relatif s'y résout depuis le dossier du LIEN, pas depuis celui
    # d'où la commande a été lancée -- `vsm-render` sortait alors en échec 2,
    # sans dire lequel des trois liens était mort.
    projet_dossier: Path = arguments.projet.resolve()
    projet = json.loads((projet_dossier / "project.json").read_text(encoding="utf-8"))
    stems_dossier = arguments.stems or (projet_dossier / "stems" / "stems")
    if not stems_dossier.is_dir():
        raise SystemExit(f"stems introuvables : {stems_dossier} — relancez la chaîne "
                         f"avec --garder-stems, ou passez --stems")
    if arguments.original is None:
        raise SystemExit("--original est exigé : la cible ne se devine pas")

    cible = lire_mono(arguments.original)
    stems = {nom: lire_mono(stems_dossier / f"{nom}.wav")
             for nom in ("bass", "drums", "other", "vocals")
             if (stems_dossier / f"{nom}.wav").is_file()}
    n = min(len(cible), *(len(v) for v in stems.values()))
    cible = cible[:n]
    stems = {k: aligner(v, n) for k, v in stems.items()}

    mesurer = lambda x: reconstruction_distance(cible, aligner(x, n), SAMPLE_RATE,  # noqa: E731
                                                metric=arguments.metrique)

    print(f"[1/2] Ce que chaque stem pèse (métrique {arguments.metrique})\n")
    plancher = mesurer(sum(stems.values()))
    silence = mesurer(np.zeros(n, dtype=np.float32))
    print(f"      somme des stems séparés : {plancher:.4f}   <- LE PLANCHER, "
          f"aucune reconstruction ne fera mieux")
    print(f"      silence                 : {silence:.4f}   <- le plafond, pour situer\n")
    for nom in stems:
        sans = sum(v for k, v in stems.items() if k != nom)
        print(f"      sans {nom:8s}          : {mesurer(sans):.4f}")

    print("\n[2/2] Ce que chaque piste coûte : rendue seule, puis remplacée par le stem RÉEL\n")
    moteur = find_vsm_render(arguments.moteur)
    travail = arguments.travail or (projet_dossier / "budget-erreur")
    travail.mkdir(parents=True, exist_ok=True)
    duree = n / SAMPLE_RATE

    rendus: Dict[str, np.ndarray] = {}
    for piste in projet["tracks"]:
        nom = piste["name"]
        rendus[nom] = aligner(rendre_piste_seule(projet, projet_dossier, nom,
                                                 travail, duree, moteur), n)
        print(f"      {nom:9s} rendue seule, niveau efficace "
              f"{float(np.sqrt(np.mean(rendus[nom] ** 2))):.4f}")

    melange = sum(rendus.values())
    base = mesurer(melange)
    annonce: Optional[float] = None
    rapport = projet_dossier / "rapport.json"
    if rapport.is_file():
        annonce = json.loads(rapport.read_text(encoding="utf-8")).get("globalDistance")

    print(f"\n      somme des pistes rendues : {base:.4f}", end="")
    if annonce is not None:
        ecart = abs(base - annonce)
        print(f"   (le rapport annonce {annonce:.4f}, écart {ecart:.4f})")
        if ecart > 0.005:
            print("      ATTENTION : la décomposition ne redonne pas la distance annoncée.")
            print("      Le tableau ci-dessous ne décrit pas le morceau du rapport.")
    else:
        print()

    print(f"\n      {'piste rendue PARFAITE':24s} {'distance':>9s} {'gain':>8s}")
    for nom in rendus:
        stem = PISTE_VERS_STEM.get(nom)
        if stem is None or stem not in stems:
            print(f"      {nom:24s} {'—':>9s} {'(aucun stem correspondant)':>8s}")
            continue
        hybride = melange - rendus[nom] + stems[stem]
        distance = mesurer(hybride)
        print(f"      {nom:24s} {distance:9.4f} {100 * (distance - base) / base:+7.1f}%")

    print("\n      Lecture : le plus gros gain est le seul endroit où travailler.")
    print("      Un gain nul veut dire que cette piste est déjà hors de cause,")
    print("      et qu'aucun réglage ne l'améliorera de façon audible.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
