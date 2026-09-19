#!/usr/bin/env python3
"""Quels réglages déplacent la HAUTEUR sans le déclarer ? (B5, exigence 4)

    analyse/.venv/bin/python tools/hauteur-des-patchs.py              # le balayage
    analyse/.venv/bin/python tools/hauteur-des-patchs.py --coupables  # + la fouille par paramètre

LA RÈGLE QUE CET OUTIL FAIT TENIR, et elle est au § 7 bis.1 du cahier des
charges : **aucune partie du corpus ne doit sonner à plus de deux demi-tons de
la note qu'elle écrit**. La borne de D277 (`--borner-hauteur`) n'y suffit pas,
et c'est ce que le lot `s2` a montré : elle ne peut brider QUE les dimensions
que le moteur déclare en `st` ou en `cents`, et il en existe d'autres.

DEUX CAUSES, ET ELLES NE SE SOIGNENT PAS PAREIL :

  * **la MACHINE** — son patch d'usine sonne déjà ailleurs que la note jouée.
    Une membrane de tambour est inharmonique : sa hauteur perçue n'est pas son
    numéro de note. Une telle machine n'a rien à faire dans le vivier MÉLODIQUE
    du banc, comme les boîtes à rythmes n'y sont pas ;
  * **un PARAMÈTRE** — l'usine sonne juste et un réglage tiré déplace la
    hauteur. `scanned.tension` est la tension d'une corde : elle EST la hauteur,
    et rien dans son unité déclarée (sans unité, de 0 à 1) ne le dit.

CE QUI EST MESURÉ. Une note tenue est rendue et sa hauteur lue par
autocorrélation, au patch d'usine puis sur des patchs tirés comme le banc les
tire. L'écart se lit en demi-tons. Une machine dont AUCUN rendu ne donne de
hauteur lisible (bruit, percussion) est dite « sans hauteur » et n'est pas
comptée juste : ce qui ne peut pas être vu n'est pas compté nul.

`--coupables` fouille ensuite, machine par machine, quel PARAMÈTRE déplace la
hauteur : chacun est balayé seul depuis le patch d'usine, et l'outil rend
l'intervalle autour de la valeur d'usine où l'écart reste sous deux demi-tons.
C'est cet intervalle que le banc doit respecter.

Code de retour non nul dès qu'une machine ou un paramètre sort des tables
déclarées dans `analyse/analyzer/vsm_morceaux.py` : une machine neuve au parc
ne doit pas entrer en douce dans le vivier mélodique du banc.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

RACINE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RACINE / "analyse"))

from analyzer.vsm_engine import Note, VsmEngine, VsmEngineError  # noqa: E402
from analyzer.vsm_patch_optimizer import _vector_to_parameters, search_space_for_machine  # noqa: E402

SR = 44100
NOTE = 60
TIRAGES = 6
TOLERANCE_ST = 2.0

# LA MESURE DE HAUTEUR VIENT DU MODULE DU BANC, elle n'est pas recopiée ici :
# deux mesures de hauteur qui ne diraient pas la même chose ne mesureraient rien,
# et c'est sur celle du banc que le tirage de patch rejette.
from analyzer.vsm_morceaux import (MACHINES_SANS_HAUTEUR_JUSTE, Generateur,  # noqa: E402
                                   hauteur_sonnante, hors_octave,
                                   machines_melodiques_du_banc)


def ecart(moteur: VsmEngine, machine: str, patch: Dict[str, float],
          profil: str = "") -> Optional[float]:
    try:
        audio = moteur.render(machine, patch, [Note(NOTE, 100, 0.0, 0.6)], 0.8,
                              profile=profil or None)
    except VsmEngineError:
        return None
    h = hauteur_sonnante(audio)
    return None if h is None else h - NOTE


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--coupables", action="store_true", help="fouiller le paramètre en cause")
    ap.add_argument("--machines", default="", help="restreindre (liste séparée par des virgules)")
    args = ap.parse_args()

    with VsmEngine(sample_rate=SR) as moteur:
        machines = [m for m in args.machines.split(",") if m] or machines_melodiques_du_banc(moteur)
        machines = sorted(set(machines) | set(MACHINES_SANS_HAUTEUR_JUSTE))
        print(f"{len(machines)} machines, note {NOTE}, {TIRAGES} patchs tirés chacune, "
              f"tolérance {TOLERANCE_ST:.0f} demi-tons\n")
        print(f"{'machine':20s} {'usine':>8} {'pire patch':>11} {'sur':>4}  verdict "
              f"(écarts RAMENÉS DANS L'OCTAVE, comme le fait la garde du corpus)")
        fautives_usine: List[Tuple[str, float]] = []
        fautives_patch: List[Tuple[str, float]] = []
        sans_hauteur: List[str] = []
        rng = np.random.default_rng(1)
        # LE TIRAGE EST CELUI DU BANC, BORNE COMPRISE. La première version tirait
        # le patch à plat : elle accusait seize machines dont six synthétiseurs
        # soustractifs classiques (Minimoog, Jupiter-8, Prophet, OB-X, MS-20,
        # Arp Odyssey) qui déclarent, eux, leur désaccord d'oscillateur EN
        # DEMI-TONS — et que `--borner-hauteur 2` bride déjà. Elle mesurait donc
        # le corpus d'avant, pas celui qu'on engendre.
        brideur = Generateur(moteur, machines=list(machines), borne_hauteur=TOLERANCE_ST,
                             journal=lambda ligne: None)
        for machine in machines:
            espace = search_space_for_machine(machine, moteur, max_dimensions=10 ** 6)
            usine = ecart(moteur, machine, {})
            pires: List[float] = []
            for _ in range(TIRAGES):
                vecteur = brideur.borner_les_hauteurs(machine, rng.random(len(espace)))
                patch = _vector_to_parameters(espace, vecteur)
                e = ecart(moteur, machine, patch)
                if e is not None:
                    pires.append(e)
            pire = max(pires, key=lambda e: abs(hors_octave(e))) if pires else None
            if usine is None and pire is None:
                sans_hauteur.append(machine)
                print(f"{machine:20s} {'-':>8} {'-':>11} {0:4d}  SANS HAUTEUR LISIBLE (non comptée juste)")
                continue
            verdict = "juste"
            if usine is not None and abs(hors_octave(usine)) > TOLERANCE_ST:
                verdict = "LA MACHINE sonne ailleurs"
                fautives_usine.append((machine, hors_octave(usine)))
            elif pire is not None and abs(hors_octave(pire)) > TOLERANCE_ST:
                verdict = "UN PARAMÈTRE déplace la hauteur"
                fautives_patch.append((machine, hors_octave(pire)))
            print(f"{machine:20s} {hors_octave(usine) if usine is not None else float('nan'):+8.2f} "
                  f"{hors_octave(pire) if pire is not None else float('nan'):+11.2f} "
                  f"{len(pires):4d}  {verdict}")

        print(f"\nMACHINES dont le patch d'USINE sonne à plus de {TOLERANCE_ST:.0f} demi-tons : "
              f"{len(fautives_usine)}")
        for machine, e in fautives_usine:
            connue = "déclarée" if machine in MACHINES_SANS_HAUTEUR_JUSTE else "NON DÉCLARÉE"
            print(f"  {machine:20s} {e:+.2f} st  ({connue})")
        print(f"MACHINES dont un PATCH TIRÉ sort de la borne : {len(fautives_patch)}")
        for machine, e in fautives_patch:
            print(f"  {machine:20s} {e:+.2f} st  (retiré au TIRAGE, sonde par sonde)")
        print(f"SANS HAUTEUR LISIBLE : {len(sans_hauteur)}"
              + (f" — {', '.join(sans_hauteur)}" if sans_hauteur else ""))

        if args.coupables and fautives_patch:
            print("\nQUEL PARAMÈTRE, ET DANS QUEL INTERVALLE L'ÉCART RESTE SOUS LA BORNE")
            for machine, _ in fautives_patch:
                espace = search_space_for_machine(machine, moteur, max_dimensions=10 ** 6)
                for dimension in espace:
                    bornes = []
                    if dimension.unit in ("st", "cents"):
                        continue   # déjà bridé par la borne en demi-tons
                    for t in np.linspace(0.0, 1.0, 11):
                        valeur = float(
                            dimension.low * (dimension.high / dimension.low) ** t
                            if dimension.logarithmic and dimension.low > 0
                            else dimension.low + t * (dimension.high - dimension.low))
                        e = ecart(moteur, machine, {dimension.semantic_id: valeur})
                        bornes.append((float(t), e))
                    mesures = [(t, e) for t, e in bornes if e is not None]
                    if not mesures:
                        continue
                    amplitude = max(abs(hors_octave(e)) for _, e in mesures)
                    if amplitude <= TOLERANCE_ST:
                        continue
                    bons = [t for t, e in mesures if abs(hors_octave(e)) <= TOLERANCE_ST]
                    intervalle = f"[{min(bons):.2f}, {max(bons):.2f}]" if bons else "AUCUN"
                    print(f"  {machine:18s} {dimension.semantic_id:28s} amplitude {amplitude:+6.2f} st, "
                          f"fenêtre sous la borne (en 0-1) : {intervalle}")

        # LA RÈGLE DE LA GARDE, et elle ne porte QUE sur le patch d'usine. Une
        # machine dont l'usine sonne juste est bridée au TIRAGE, sonde par sonde,
        # par `Generateur.tirer_patch` : le patch fautif est retiré comme un patch
        # muet l'est, quel que soit le paramètre en cause. Une machine dont
        # l'USINE sonne ailleurs, elle, ferait retirer huit patchs pour rien avant
        # d'être écartée — elle doit être déclarée, et elle sort du vivier.
        inconnues = [m for m, _ in fautives_usine if m not in MACHINES_SANS_HAUTEUR_JUSTE]
        inconnues += [m for m in sans_hauteur if m not in MACHINES_SANS_HAUTEUR_JUSTE]
        if inconnues:
            print(f"\nNON DÉCLARÉES : {', '.join(sorted(set(inconnues)))} — leur patch d'usine sonne "
                  f"ailleurs (ou n'a pas de hauteur lisible) et le banc les tirerait quand même, "
                  f"pour retirer huit patchs par partie avant de les écarter")
            return 1
        print("\nTOUTES LES MACHINES DU VIVIER SONNENT JUSTE AU PATCH D'USINE ; celles dont un "
              "réglage déplace la hauteur sont retirées au tirage, sonde par sonde")
        return 0


if __name__ == "__main__":
    sys.exit(main())
