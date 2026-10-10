"""H64 — CHAQUE PIÈCE D'UNE BATTERIE CALÉE SUR LA BANDE QU'ELLE DOMINE, SOUS UNE GARDE
(docs/CDC-reload-indifferenciable.md § 25).

Le calage par groupe (`vsm_levels._caler_un_groupe`) donne à toutes les pièces d'une batterie le MÊME facteur :
l'équilibre interne n'est mesuré par rien. Sur « Reload », il coûtait 10,9 dB au charleston (H63).

La règle, pièce par pièce, sur les énergies par bande des rendus SOLO et du stem :
  - les bandes que la pièce DOMINE : sa part de la somme des rendus y est ≥ `PART_DOMINANTE` ;
  - le facteur d'énergie `g²` qui rend la somme égale au stem SUR CES BANDES-LÀ, les autres pièces gardées ;
  - LA GARDE : sur toute autre bande où la pièce porte ≥ `PART_GARDEE` de la somme, l'écart au stem après le facteur
    ne doit pas dépasser l'écart d'avant de plus de `GARDE_DB` — sinon le facteur est REFUSÉ, et la raison dite. Le
    kick de « Reload » est ce cas : son sub le monterait de 7,8 dB, et le bas-médium, qu'il porte, s'éloignerait.
Une pièce qui ne domine aucune bande garde son niveau, et c'est dit aussi.

Fonctions pures, sans rendu : la mesure qui les emploie fournit les énergies.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import List, Mapping, Optional, Tuple

import numpy as np
import numpy.typing as npt

BANDES: Tuple[Tuple[float, float], ...] = ((20, 60), (60, 150), (150, 500), (500, 2000), (2000, 6000),
                                           (6000, 10000), (10000, 16000))
PART_DOMINANTE = 0.60
PART_GARDEE = 0.25
GARDE_DB = 3.0
FACTEUR_MIN, FACTEUR_MAX = 0.25, 4.0   # en amplitude : de −12 à +12 dB


@dataclass
class FacteurDePiece:
    nom: str
    facteur: float                 # en AMPLITUDE (le volume se multiplie par lui) ; 1.0 si refusé ou sans bande
    bandes: Tuple[int, ...]        # les bandes dominées (indices de BANDES)
    raison: str


def energies_par_bande(mono: np.ndarray, sr: int, bloc_s: float = 2.0, saut: int = 3) -> np.ndarray:
    """L'énergie de chaque bande de `BANDES` : FFT de Hann par blocs de `bloc_s`, un bloc sur `saut`."""
    x = np.asarray(mono, dtype=np.float64)
    n = int(sr * bloc_s)
    acc = np.zeros(len(BANDES))
    if n <= 0 or len(x) < n:
        return acc
    f = np.fft.rfftfreq(n, 1.0 / sr)
    fenetre = np.hanning(n)
    masques = [(f >= a) & (f < b) for a, b in BANDES]
    for i in range(0, len(x) - n + 1, n * saut):
        p = np.abs(np.fft.rfft(x[i:i + n] * fenetre)) ** 2
        for j, m in enumerate(masques):
            acc[j] += float(p[m].sum())
    return acc


def _db(rapport: float) -> float:
    return 10.0 * math.log10(max(rapport, 1e-30))


def facteurs_par_bande(stem: npt.ArrayLike, pieces: Mapping[str, npt.ArrayLike],
                       part_dominante: float = PART_DOMINANTE, part_gardee: float = PART_GARDEE,
                       garde_db: float = GARDE_DB) -> List[FacteurDePiece]:
    """Le facteur de chaque pièce (voir l'en-tête). `stem` et chaque pièce : une énergie par bande de `BANDES`."""
    e_stem = np.asarray(stem, dtype=np.float64)
    e = {nom: np.asarray(v, dtype=np.float64) for nom, v in pieces.items()}
    somme = np.sum(list(e.values()), axis=0) if e else np.zeros_like(e_stem)
    sortie: List[FacteurDePiece] = []
    for nom, ep in e.items():
        parts = np.divide(ep, somme, out=np.zeros_like(ep), where=somme > 0)
        dominees = tuple(int(b) for b in np.nonzero(parts >= part_dominante)[0] if e_stem[b] > 0)
        if not dominees:
            sortie.append(FacteurDePiece(nom, 1.0, (), f"ne domine aucune bande (part maximale {parts.max():.0%})"))
            continue
        idx = list(dominees)
        autres = float((somme[idx] - ep[idx]).sum())
        propre = float(ep[idx].sum())
        visee = float(e_stem[idx].sum()) - autres
        if propre <= 0 or visee <= 0:
            sortie.append(FacteurDePiece(nom, 1.0, dominees, "le stem est déjà couvert par les autres pièces sur ces bandes"))
            continue
        g2 = visee / propre
        facteur = float(np.clip(math.sqrt(g2), FACTEUR_MIN, FACTEUR_MAX))
        borne = " (borné)" if abs(facteur - math.sqrt(g2)) > 1e-9 else ""
        refus: Optional[str] = None
        for c in range(len(BANDES)):
            if c in dominees or parts[c] < part_gardee or e_stem[c] <= 0 or somme[c] <= 0:
                continue
            avant = abs(_db(somme[c] / e_stem[c]))
            apres = abs(_db((somme[c] - ep[c] + facteur ** 2 * ep[c]) / e_stem[c]))
            if apres > avant + garde_db:
                refus = (f"REFUSÉ : × {facteur:.2f} éloignerait la bande {BANDES[c][0]:.0f}-{BANDES[c][1]:.0f} Hz "
                         f"(part {parts[c]:.0%}) de {avant:.1f} à {apres:.1f} dB du stem")
                break
        if refus:
            sortie.append(FacteurDePiece(nom, 1.0, dominees, refus))
        else:
            bandes = ", ".join(f"{BANDES[b][0]:.0f}-{BANDES[b][1]:.0f}" for b in dominees)
            sortie.append(FacteurDePiece(nom, facteur, dominees, f"× {facteur:.2f}{borne} sur {bandes} Hz"))
    return sortie
