#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Engendre le CORPUS d'apprentissage — phase A0 de `docs/ROADMAP-apprentissage.md`.

    .venv/bin/python corpus.py --sortie corpus/
    .venv/bin/python corpus.py --sortie corpus/ --patchs 2000
    .venv/bin/python corpus.py --sortie corpus/ --machines vsm.minimoog,vsm.juno106
    .venv/bin/python corpus.py --verifier corpus/          # le corpus est-il encore valable ?

CE QUE ÇA PRODUIT :

    corpus/
      manifeste.json                 <- de quoi il est fait, et s'il est périmé
      vsm.minimoog/lot-000.npz       <- descripteurs + patchs, par lots autonomes
      vsm.minimoog/lot-001.npz
      ...

CE QUE ÇA COÛTE, ET POURQUOI C'EST ÉCRIT ICI. Le § 3 du cahier des charges
donnait un ordre de grandeur (« 10 000 patchs × 12 notes ≈ 20 min par machine »)
en demandant de le vérifier. La commande MESURE et publie le coût réel, machine
par machine, dans le manifeste : c'est ce chiffre-là qui compte, pas l'estimation.

INTERRUPTIBLE ET REPRENABLE. Chaque lot est autonome et écrit dès qu'il est
prêt ; relancer la commande saute les lots déjà présents. Un corpus complet
prend des heures, et personne ne relance des heures pour une coupure.
"""

from __future__ import annotations

import argparse
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import List

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_corpus_build import (AUGMENTATIONS, GrilleDeNotes, LotDeCorpus,
                                        Manifeste, NOM_FUITE, genere_lot,
                                        machine_fingerprint, machines_de_recherche,
                                        nouveau_manifeste, verifie_fraicheur)
from analyzer.vsm_engine import VsmEngine, VsmEngineError
from analyzer.vsm_patch_optimizer import search_space_for_machine
from analyzer.vsm_corpus_build import graine_de_machine
from analyzer.vsm_reconstruct import melodic_machines

# Machines du corpus par défaut : celles que le classifieur devra départager,
# c'est-à-dire les CANDIDATES de la chaîne. Y mettre le synthé de test ou les
# boîtes à rythmes peuplerait le corpus de classes que personne n'a à
# reconnaître sur un stem mélodique ; les percussions relèvent de la phase A2,
# qui a son propre corpus de frappes.
def machines_par_defaut(engine: VsmEngine) -> List[str]:
    melodiques = set(melodic_machines(engine))
    return [m for m in machines_de_recherche(engine) if m in melodiques]


def main() -> int:
    analyseur = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    analyseur.add_argument("--sortie", type=Path, default=Path("corpus"),
                            help="dossier du corpus (défaut : ./corpus)")
    analyseur.add_argument("--verifier", type=Path, default=None,
                            help="vérifier la fraîcheur d'un corpus existant et sortir")
    analyseur.add_argument("--machines", default="",
                            help="liste de machines, séparées par des virgules "
                                 "(défaut : les candidates mélodiques du moteur)")
    analyseur.add_argument("--patchs", type=int, default=2000,
                            help="patchs par machine (défaut 2000). Le § 3 du cahier des "
                                 "charges parle de 10 000 ; commencez petit, le manifeste "
                                 "publie le coût réel et vous saurez ce que 10 000 coûtent")
    analyseur.add_argument("--taille-lot", type=int, default=250,
                            help="patchs par lot autonome (défaut 250)")
    analyseur.add_argument("--graine", type=int, default=20260823)
    analyseur.add_argument("--sample-rate", type=int, default=44100)
    analyseur.add_argument("--sans-augmentation", action="store_true",
                            help="corpus SEC uniquement — le témoin A/B du § 7")
    analyseur.add_argument("--proportion-augmentee", type=float, default=0.5,
                            help="part des exemples dégradés (défaut 0,5). Pas 1,0 : le "
                                 "modèle doit voir le son propre aussi, sinon il apprend "
                                 "à reconnaître la dégradation autant que la machine")
    analyseur.add_argument("--moteur", default=None, help="chemin de vsm-render")
    arguments = analyseur.parse_args()

    if arguments.verifier is not None:
        return verifier(arguments.verifier, arguments.moteur)

    augmentations = ([] if arguments.sans_augmentation
                     else [a.nom for a in AUGMENTATIONS] + [NOM_FUITE])
    grille = GrilleDeNotes()
    sortie = arguments.sortie
    sortie.mkdir(parents=True, exist_ok=True)

    try:
        moteur = VsmEngine(binary=arguments.moteur, sample_rate=arguments.sample_rate)
    except VsmEngineError as erreur:
        print(f"moteur indisponible : {erreur}", file=sys.stderr)
        return 2

    with moteur:
        machines = ([m.strip() for m in arguments.machines.split(",") if m.strip()]
                    or machines_par_defaut(moteur))
        print(f"[1/3] {len(machines)} machine(s), {arguments.patchs} patchs chacune, "
              f"{grille.rendus_par_patch()} rendus par patch "
              f"= {len(machines) * arguments.patchs * grille.rendus_par_patch()} exemples visés")
        if augmentations:
            print(f"      augmentations : {', '.join(augmentations)} "
                  f"({arguments.proportion_augmentee:.0%} des exemples)")
        else:
            print("      corpus SEC (--sans-augmentation) : c'est le témoin A/B du § 7")

        horodatage = datetime.now(timezone.utc).isoformat(timespec="seconds")
        manifeste = nouveau_manifeste(arguments.sample_rate, arguments.graine,
                                       arguments.patchs, grille, augmentations, horodatage)

        print("[2/3] Empreintes des machines")
        for machine in machines:
            empreinte = machine_fingerprint(moteur, machine, arguments.sample_rate)
            if not empreinte:
                print(f"      {machine:20s} INJOUABLE — écartée du corpus")
                continue
            manifeste.empreintes[machine] = empreinte
            print(f"      {machine:20s} {empreinte[:16]}")
        machines = [m for m in machines if m in manifeste.empreintes]

        print("[3/3] Génération")
        depart_total = time.perf_counter()
        engendres_total = 0
        for machine in machines:
            dossier = sortie / machine
            dossier.mkdir(parents=True, exist_ok=True)
            espace = search_space_for_machine(machine, moteur)
            exemples, secondes = 0, 0.0
            engendres = 0
            fuite = None
            lots = max(1, (arguments.patchs + arguments.taille_lot - 1) // arguments.taille_lot)
            for numero in range(lots):
                chemin = dossier / f"lot-{numero:03d}.npz"
                if chemin.exists():
                    # REPRISE : le lot est autonome, on le relit pour le compte
                    # et on passe. C'est ce qui rend la commande relançable.
                    deja = LotDeCorpus.relit(chemin)
                    exemples += len(deja.X)
                    secondes += deja.seconds
                    continue
                patchs = min(arguments.taille_lot,
                             arguments.patchs - numero * arguments.taille_lot)
                lot = genere_lot(
                    machine, espace, moteur, patchs=patchs, grille=grille,
                    # La graine dépend de la machine ET du lot : deux lots
                    # partageant une graine tireraient les mêmes patchs, et le
                    # corpus contiendrait des doublons sans qu'on le voie.
                    #
                    # `hash()` d'une chaîne est INTERDIT ici : Python le
                    # randomise à chaque démarrage (PYTHONHASHSEED), si bien que
                    # deux générations avec la même graine tireraient des patchs
                    # différents. C'est exactement la non-reproductibilité
                    # silencieuse que l'exigence n° 1 du § 3 interdit.
                    graine=arguments.graine + numero * 977 + graine_de_machine(machine),
                    sample_rate=arguments.sample_rate,
                    augmentations=augmentations,
                    proportion_augmentee=arguments.proportion_augmentee,
                    fuite_precedente=fuite,
                    decalage_patch=numero * arguments.taille_lot,
                    progression=lambda message: print(f"      {message}", end="\r", flush=True))
                lot.enregistre(chemin)
                exemples += len(lot.X)
                secondes += lot.seconds
                engendres += len(lot.X)
            manifeste.exemples[machine] = exemples
            manifeste.secondes[machine] = round(secondes, 1)
            engendres_total += engendres
            repris = " (repris)" if engendres == 0 else ""
            print(f"      {machine:20s} {exemples:7d} exemples  {secondes:7.0f} s"
                  f"  ({exemples / max(secondes, 1e-9):5.0f}/s){repris}")

        manifeste.enregistre(sortie / "manifeste.json")
        total = time.perf_counter() - depart_total
        exemples_total = sum(manifeste.exemples.values())
        print(f"\ncorpus écrit dans {sortie}")
        print(f"  {exemples_total} exemples au total")
        # L'EXTRAPOLATION N'A DE SENS QUE SUR CE QUI A ÉTÉ RÉELLEMENT ENGENDRÉ.
        # Sur une reprise complète, tout est relu depuis le disque en une
        # fraction de seconde, et diviser par ce temps-là annoncerait deux cent
        # mille exemples par seconde -- un chiffre faux, du genre qu'on recopie
        # ensuite dans un document sans le revérifier.
        if engendres_total == 0:
            print("  rien de nouveau engendré (tous les lots étaient déjà là) : "
                  "pas de coût à publier")
        else:
            print(f"  {engendres_total} engendrés en {total:.0f} s "
                  f"({engendres_total / max(total, 1e-9):.0f}/s)")
            par_machine = total / max(len(machines), 1)
            print(f"  extrapolation à 10 000 patchs/machine : "
                  f"{par_machine * 10000 / arguments.patchs / 60:.0f} min par machine, "
                  f"{par_machine * 10000 / arguments.patchs * len(machines) / 3600:.1f} h "
                  f"pour ces {len(machines)} machines")
    return 0


def verifier(dossier: Path, binaire) -> int:
    """Le corpus est-il encore valable ? Le seul moyen de le savoir est de
    refaire jouer les machines et de comparer."""
    chemin = dossier / "manifeste.json" if dossier.is_dir() else dossier
    try:
        manifeste = Manifeste.relit(chemin)
    except (OSError, ValueError) as erreur:
        print(f"manifeste illisible : {erreur}", file=sys.stderr)
        return 2

    print(f"corpus du {manifeste.date}, commit {manifeste.commit[:12] or '(inconnu)'}")
    print(f"  {len(manifeste.empreintes)} machines, "
          f"{sum(manifeste.exemples.values())} exemples, "
          f"graine {manifeste.graine}, {manifeste.sample_rate} Hz")
    if manifeste.augmentations:
        print(f"  augmentations : {', '.join(manifeste.augmentations)}")

    with VsmEngine(binary=binaire, sample_rate=manifeste.sample_rate) as moteur:
        verdict = verifie_fraicheur(manifeste, moteur)
    print(f"\n{verdict.resume()}")
    return 0 if verdict.frais else 1


if __name__ == "__main__":
    raise SystemExit(main())
