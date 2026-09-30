"""H43 — réunir les notes qu'une transcription a hachées dans un son TENU.

docs/CDC-reload-indifferenciable.md § 5. Le pad de « Reload » sort de Basic Pitch
en morceaux de 0,27 s qui se suivent SANS INTERVALLE (72 à 76 % des notes de ses
hauteurs) : un accord tenu devient quatre attaques par seconde, et l'arbitrage
choisit des machines frappées.

LA RÈGLE, ET CE QU'ELLE NE DOIT PAS CASSER. Deux notes de même hauteur se
réunissent quand (a) la seconde commence moins de `ecart_max` après la fin de la
première ET (b) le stem ne présente PAS de nouvelle attaque à la jonction :
l'énergie dans une bande d'un demi-ton autour de la hauteur ne monte pas de plus
de `seuil_db` entre les 30 ms qui précèdent et les 30 ms qui suivent. La
condition (b) existe parce que la vérité de S2 compte 7 020 paires vraies sur
49 267 (14,25 %) contiguës à moins de 30 ms : un réunisseur sur le seul écart
détruirait ces notes rejouées.

Rien ne disparaît en silence : chaque réunion est comptée, par hauteur.
"""

from __future__ import annotations

from collections import Counter, defaultdict
from dataclasses import replace
from typing import Any, Dict, List, Sequence, Tuple

import numpy as np
from scipy.signal import butter, sosfiltfilt

FENETRE_S = 0.030


def _bande(audio: np.ndarray, sr: int, midi: int) -> np.ndarray:
    """Le stem filtré sur un demi-ton autour de la hauteur (± un quart de ton)."""
    f = 440.0 * 2.0 ** ((midi - 69) / 12.0)
    bas, haut = f * 2.0 ** (-1.0 / 24.0), min(f * 2.0 ** (1.0 / 24.0), sr * 0.45)
    if bas <= 20.0 or haut <= bas:
        return audio
    sos = butter(2, [bas, haut], btype="band", fs=sr, output="sos")
    return sosfiltfilt(sos, audio)


def _energie_db(x: np.ndarray, i: int, j: int) -> float:
    i, j = max(0, i), min(len(x), j)
    if j <= i:
        return -120.0
    return float(10.0 * np.log10(np.mean(np.square(x[i:j], dtype=np.float64)) + 1e-14))


def reunir_tenues(notes: Sequence[Any], audio: np.ndarray, sr: int,
                  ecart_max: float = 0.030, seuil_db: float = 3.0) -> Tuple[List[Any], Dict[str, Any]]:
    """Rend (notes réunies, bilan). `notes` : objets à champs note/start/duration (StemNote)."""
    par_hauteur: Dict[int, List[Any]] = defaultdict(list)
    for n in notes:
        par_hauteur[int(n.note)].append(n)
    sortie: List[Any] = []
    reunies = Counter()
    refusees_attaque = 0
    fenetre = int(FENETRE_S * sr)
    for hauteur, liste in par_hauteur.items():
        liste = sorted(liste, key=lambda n: n.start)
        bande = None
        courante = liste[0]
        for suivante in liste[1:]:
            fin = courante.start + courante.duration
            if suivante.start - fin < ecart_max:
                if bande is None:
                    bande = _bande(audio, sr, hauteur)
                jonction = int(round(suivante.start * sr))
                hausse = _energie_db(bande, jonction, jonction + fenetre) - _energie_db(bande, jonction - fenetre, jonction)
                if hausse <= seuil_db:
                    nouvelle_fin = max(fin, suivante.start + suivante.duration)
                    courante = replace(courante, duration=nouvelle_fin - courante.start,
                                       confidence=min(float(courante.confidence), float(suivante.confidence)))
                    reunies[hauteur] += 1
                    continue
                refusees_attaque += 1
            sortie.append(courante)
            courante = suivante
        sortie.append(courante)
    sortie.sort(key=lambda n: (n.start, n.note))
    bilan = {"notesAvant": len(notes), "notesApres": len(sortie), "reunions": int(sum(reunies.values())),
             "refuseesParAttaque": refusees_attaque, "parHauteur": {str(k): v for k, v in sorted(reunies.items())},
             "ecartMaxS": ecart_max, "seuilDb": seuil_db}
    return sortie, bilan
