"""L'EMPREINTE DE TIMBRE d'un registre, lue dans l'audio du stem à ses notes.

POURQUOI. Le découpage d'un stem fourre-tout en registres (par les vides,
par k-moyennes) ne sait rien des instruments : il partage des HAUTEURS. Deux
registres disjoints peuvent être deux instruments (H23, l'épreuve de parité
à trois couches) ou les deux mains d'un même piano (la variante « chorale »
de l'épreuve : une partie, deux registres rendus). Seul le TIMBRE les
distingue, et le timbre est dans l'audio, pas dans les notes.

CE QU'ON MESURE. À chaque note transcrite, une tranche de spectre prise
après l'attaque ; on y lit l'amplitude aux harmoniques 1..N de la hauteur de
la note (une bande étroite autour de chaque multiple de f0), normalisée pour
ne pas dépendre du volume. La moyenne de ces profils sur toutes les notes du
registre est son empreinte. Deux empreintes se comparent par la distance
cosinus, insensible au niveau. La polyphonie salit chaque profil (les
harmoniques des autres notes tombent dans les bandes), mais pas de la même
façon d'une note à l'autre : la moyenne les lave en partie.

CE QUE ÇA NE SAIT PAS FAIRE. Un même instrument dont le timbre change avec
le registre (un piano est plus riche dans le grave) aura deux empreintes
différentes ; le seuil entre « même timbre » et « autre timbre » se choisit
donc par la MESURE — voir H25 dans ROADMAP-fusion — et non par principe.

MESURÉ LE 03/09/2026, ET C'EST UN RÉSULTAT NÉGATIF (ROADMAP-fusion, H25) :
sur l'épreuve à trois timbres DIFFÉRENTS, les registres sont à 0,08-0,32
l'un de l'autre ; sur la chorale d'UN SEUL timbre, ses deux registres sont à
0,48. Le profil harmonique dépend du registre plus que de l'instrument :
cette empreinte ne distingue PAS « même instrument » de « autre instrument ».
Le module reste comme outil de mesure ; il n'entre pas dans la chaîne.
"""
from __future__ import annotations

from typing import List, Optional, Sequence

import numpy as np

HARMONIQUES = 10
FENETRE = 4096
RETARD_ATTAQUE = 0.03        # s après l'attaque : la tenue, pas le transitoire
LARGEUR_BANDE = 0.03         # ± 3 % autour de chaque harmonique


def _hz(midi: float) -> float:
    return 440.0 * 2.0 ** ((float(midi) - 69.0) / 12.0)


def profil_d_une_note(audio: np.ndarray, sample_rate: int, note: int, debut: float,
                      duree: float) -> Optional[np.ndarray]:
    """Le profil harmonique d'UNE note : amplitudes aux harmoniques 1..N de
    sa hauteur, normalisées (somme 1). None si la tranche sort du signal ou
    est muette."""
    f0 = _hz(note)
    if f0 * 2.0 >= sample_rate / 2.0:
        return None
    position = debut + min(RETARD_ATTAQUE, max(0.0, duree * 0.5))
    i = int(position * sample_rate)
    if i < 0 or i + FENETRE > audio.size:
        return None
    tranche = audio[i:i + FENETRE].astype(np.float64) * np.hanning(FENETRE)
    if float(np.max(np.abs(tranche))) < 1e-5:
        return None
    spectre = np.abs(np.fft.rfft(tranche))
    frequences = np.fft.rfftfreq(FENETRE, 1.0 / sample_rate)
    profil = np.zeros(HARMONIQUES)
    for h in range(1, HARMONIQUES + 1):
        centre = f0 * h
        if centre >= sample_rate / 2.0:
            break
        masque = (frequences >= centre * (1.0 - LARGEUR_BANDE)) & (frequences <= centre * (1.0 + LARGEUR_BANDE))
        if masque.any():
            profil[h - 1] = float(spectre[masque].max())
    total = float(profil.sum())
    if total <= 0.0:
        return None
    return profil / total


def empreinte(audio: np.ndarray, sample_rate: int, notes: Sequence, maximum_notes: int = 400) -> Optional[np.ndarray]:
    """L'empreinte d'un registre : profil moyen de ses notes (au plus
    `maximum_notes`, réparties sur toute la durée). None si aucune note ne
    donne de profil."""
    notes = list(notes)
    if len(notes) > maximum_notes:
        pas = len(notes) / maximum_notes
        notes = [notes[int(k * pas)] for k in range(maximum_notes)]
    profils = [p for p in (profil_d_une_note(audio, sample_rate, int(n.note), float(n.start), float(n.duration))
                           for n in notes) if p is not None]
    if not profils:
        return None
    return np.mean(np.stack(profils), axis=0)


def distance_de_timbre(a: Optional[np.ndarray], b: Optional[np.ndarray]) -> Optional[float]:
    """1 − cosinus entre deux empreintes : 0 = même profil, 1 = orthogonaux."""
    if a is None or b is None:
        return None
    na, nb = float(np.linalg.norm(a)), float(np.linalg.norm(b))
    if na <= 0.0 or nb <= 0.0:
        return None
    return float(1.0 - np.dot(a, b) / (na * nb))


def distances_entre_registres(audio: np.ndarray, sample_rate: int,
                              registres: Sequence[Sequence]) -> List[List[Optional[float]]]:
    """La matrice des distances de timbre entre registres (voix) d'un stem."""
    empreintes = [empreinte(audio, sample_rate, r) for r in registres]
    return [[distance_de_timbre(a, b) for b in empreintes] for a in empreintes]
