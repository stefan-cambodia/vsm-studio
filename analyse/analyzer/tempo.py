"""Le tempo du morceau, estimé sur le mélange — pour que la reconstruction ne s'ouvre plus à 120 BPM.

POURQUOI. Jusqu'au 15/09/2026, `reconstruire.py` écrivait `--tempo`, 120 par
défaut, dans TOUT projet reconstruit : les notes tombaient au bon endroit en
secondes (les ticks sont calculés à ce tempo), mais la grille du DAW — mesures,
aimant, quantification, boucle par mesures — ne voulait rien dire sur un morceau
à 128 ou à 96. Un DAW digne de Cubase ouvre un morceau à SON tempo.

CE QUE CE MODULE FAIT. `estimer_tempo` suit les temps par `librosa.beat.beat_track`
sur l'enveloppe d'attaques du mélange et rend le tempo en BPM, l'instant du
premier temps suivi et le nombre de temps ; `bpm` est arrondi au dixième. Le
suivi de temps peut se tromper d'une OCTAVE (deux fois trop vite ou trop lent) :
l'outil `tools/tempo-estime.py` compte ces cas à part, et c'est lui qui juge
l'estimateur contre la vérité du banc synthétique — jamais ce module lui-même.

CE QU'IL NE FAIT PAS. Ni le premier temps de la MESURE (le suivi ne distingue
pas un temps fort), ni les changements de tempo : un seul tempo, comme avant,
mais le bon.

H46 (docs/CDC-reload-indifferenciable.md § 8) — LE TEMPO À LA PRÉCISION D'UNE GRILLE.
`beat_track` est juste à ± 2 BPM (écart médian 1,0 sur le banc) : assez pour ne plus
ouvrir un morceau à 120, pas assez pour une grille — un BPM d'écart, c'est un temps
entier de dérive par minute. « Reload » est à 138,00 ; la chaîne écrivait 139,7.
`affiner_tempo` cherche, à ± 4 % du tempo suivi, celui qui rend les attaques du
mélange le plus COHÉRENTES en phase sur une grille de subdivisions (4 ou 6 par
temps). Éteint par défaut (`estimer_tempo(..., affiner=False)` est la chaîne
d'avant au bit près) ; un affinage non concluant GARDE le tempo suivi et le dit.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

import numpy as np


FENETRE = 0.04          # ± 4 % autour du tempo suivi : en deçà des rapports métriques (16/15 = 6,7 %)
PAS_BPM = 0.002
SUBDIVISIONS = (4, 6)   # doubles croches, sextolets — jamais la noire seule : un kick sur le
                        # temps et une basse à contretemps s'y annulent (« Reload », 30/09/2026)
COHERENCE_MIN = 0.2     # et trois fois le niveau du hasard, 3/√N, si c'est plus
PART_ENTIER = 0.95      # l'entier le plus proche est retenu s'il garde 95 % de la cohérence


@dataclass(frozen=True)
class Affinage:
    """Ce que l'affinage a vu — tout ce qu'il faut pour le rejuger sans le rejouer."""
    depart: float        # le tempo de `beat_track`, d'où part la fenêtre
    maximum: float       # le tempo du maximum de cohérence, au millième
    coherence: float     # 1 = toutes les attaques sur la grille ; ~1/√N au hasard
    subdivision: int     # subdivisions par temps de la grille la plus cohérente
    attaques: int
    seuil: float         # max(0,2 ; 3/√N)
    concluant: bool      # faux : le tempo de départ est GARDÉ
    entier: bool         # l'entier le plus proche a été retenu
    raison: str = ""     # pourquoi l'affinage n'est pas concluant, s'il ne l'est pas

    def json(self) -> dict:
        d = {"depart": self.depart, "maximum": self.maximum, "coherence": self.coherence,
             "subdivision": self.subdivision, "attaques": self.attaques, "seuil": self.seuil,
             "concluant": self.concluant, "entier": self.entier}
        if self.raison:
            d["raison"] = self.raison
        return d


@dataclass(frozen=True)
class Tempo:
    bpm: float
    premier_temps_secondes: float
    temps: int
    affinage: Optional[Affinage] = None

    def json(self) -> dict:
        d = {"bpm": self.bpm, "premierTempsSecondes": self.premier_temps_secondes,
             "temps": self.temps, "source": "estime"}
        if self.affinage is not None:
            d["affinage"] = self.affinage.json()
        return d


def _instants_d_attaque(enveloppe: np.ndarray, sample_rate: int, saut: int) -> tuple[np.ndarray, np.ndarray]:
    """Instants (s) et poids des attaques : les pics de l'enveloppe, affinés sous la trame."""
    import librosa

    trames = librosa.onset.onset_detect(onset_envelope=enveloppe, sr=sample_rate, hop_length=saut,
                                        units="frames", backtrack=False)
    instants, poids = [], []
    for k in np.asarray(trames, dtype=int):
        decalage = 0.0
        if 0 < k < len(enveloppe) - 1:
            a, b, c = float(enveloppe[k - 1]), float(enveloppe[k]), float(enveloppe[k + 1])
            courbure = a - 2.0 * b + c
            if courbure < 0.0:   # un vrai sommet : interpolation parabolique, bornée à la demi-trame
                decalage = float(np.clip(0.5 * (a - c) / courbure, -0.5, 0.5))
        instants.append((k + decalage) * saut / sample_rate)
        poids.append(float(enveloppe[k]))
    return np.asarray(instants, dtype=np.float64), np.asarray(poids, dtype=np.float64)


def coherence_de_grille(instants: np.ndarray, poids: np.ndarray, bpms: np.ndarray, subdivision: int) -> np.ndarray:
    """|Σ w·exp(2πi·d·t·b/60)| / Σ w pour chaque tempo `b` — par tranches, pour la mémoire."""
    total = float(poids.sum())
    sortie = np.zeros(len(bpms), dtype=np.float64)
    if total <= 0.0 or instants.size == 0:
        return sortie
    for debut in range(0, len(bpms), 256):
        b = bpms[debut:debut + 256]
        phases = np.exp(2j * np.pi * subdivision * np.outer(b, instants) / 60.0)
        sortie[debut:debut + 256] = np.abs(phases @ poids) / total
    return sortie


def affiner_tempo(instants: np.ndarray, poids: np.ndarray, depart: float) -> Affinage:
    """Le tempo le plus cohérent à ± 4 % de `depart`, sur la meilleure des subdivisions."""
    n = int(instants.size)
    seuil = max(COHERENCE_MIN, 3.0 / np.sqrt(max(n, 1)))
    if n < 8:
        return Affinage(depart, depart, 0.0, 0, n, round(seuil, 4), False, False,
                        "moins de huit attaques")
    bpms = np.arange(depart * (1.0 - FENETRE), depart * (1.0 + FENETRE) + PAS_BPM / 2, PAS_BPM)
    meilleur: Optional[tuple[float, int, int, np.ndarray]] = None
    for d in SUBDIVISIONS:
        c = coherence_de_grille(instants, poids, bpms, d)
        i = int(np.argmax(c))
        if meilleur is None or c[i] > meilleur[0]:
            meilleur = (float(c[i]), d, i, c)
    assert meilleur is not None
    cmax, d, i, c = meilleur
    # UN MAXIMUM AU BORD DE LA FENÊTRE N'EST PAS UN SOMMET : le vrai est peut-être dehors.
    if i == 0 or i == len(bpms) - 1:
        return Affinage(depart, round(float(bpms[i]), 3), round(cmax, 4), d, n, round(seuil, 4), False, False,
                        "maximum au bord de la fenêtre")
    # le sommet entre trois points, au millième
    a, b, e = c[i - 1], c[i], c[i + 1]
    courbure = a - 2.0 * b + e
    sommet = float(bpms[i]) + (0.5 * (a - e) / courbure * PAS_BPM if courbure < 0.0 else 0.0)
    if cmax < seuil:
        return Affinage(depart, round(sommet, 3), round(cmax, 4), d, n, round(seuil, 4), False, False,
                        "cohérence sous le seuil")
    # L'ENTIER LE PLUS PROCHE, s'il explique les attaques à 95 % du maximum : entre deux
    # tempos que les attaques ne départagent pas, le plus simple est celui qu'on a écrit.
    entier = float(round(sommet))
    garde = float(coherence_de_grille(instants, poids, np.array([entier]), d)[0])
    if bpms[0] <= entier <= bpms[-1] and garde >= PART_ENTIER * cmax:
        return Affinage(depart, round(sommet, 3), round(cmax, 4), d, n, round(seuil, 4), True, True)
    return Affinage(depart, round(sommet, 3), round(cmax, 4), d, n, round(seuil, 4), True, False)


def estimer_tempo(audio: np.ndarray, sample_rate: int, affiner: bool = False) -> Tempo:
    """Le tempo du mélange (mono, flottant), en BPM au dixième — affiné au millième (H46) si demandé."""
    import librosa

    y = np.asarray(audio, dtype=np.float32)
    if y.ndim > 1:
        y = y.mean(axis=1)
    if y.size < sample_rate:   # moins d'une seconde : rien à suivre
        return Tempo(120.0, 0.0, 0)
    attaques = librosa.onset.onset_strength(y=y, sr=sample_rate)
    tempo, temps = librosa.beat.beat_track(onset_envelope=attaques, sr=sample_rate, units="time")
    bpm = float(np.asarray(tempo).flat[0]) if np.ndim(tempo) > 0 else float(tempo)
    if not np.isfinite(bpm) or bpm <= 0.0:
        return Tempo(120.0, 0.0, 0)
    premier = float(temps[0]) if len(temps) else 0.0
    if not affiner:
        return Tempo(round(bpm, 1), round(premier, 3), int(len(temps)))
    # H46 : les MÊMES attaques que le suivi (même enveloppe, saut de 512 par défaut).
    instants, poids = _instants_d_attaque(np.asarray(attaques), sample_rate, 512)
    affinage = affiner_tempo(instants, poids, round(bpm, 1))
    if not affinage.concluant:
        return Tempo(round(bpm, 1), round(premier, 3), int(len(temps)), affinage)
    retenu = float(round(affinage.maximum)) if affinage.entier else affinage.maximum
    return Tempo(retenu, round(premier, 3), int(len(temps)), affinage)
