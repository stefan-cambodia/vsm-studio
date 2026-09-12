"""Combien de TIMBRES se sont installés dans un stem (H26, § 12.8 du CDC multipiste).

POURQUOI CETTE MESURE EXISTE. La porte du fourre-tout demandait polyphonie
moyenne >= 3 ET ambitus >= 36 demi-tons, toutes deux lues sur les NOTES
transcrites. Sur *Children*, le stem `other` porte quatre parties a 2,58 de
polyphonie moyenne : la porte ne s'ouvre pas, et le decoupage en voix n'est
JAMAIS ESSAYE (§ 12.4). La raison tient a la musique : dans ce morceau les
parties sont SUCCESSIVES autant que superposees — le piano entre a 28 s, le lead
vers 189 s —, et deux parties qui ne sonnent pas ensemble ne font pas monter la
polyphonie moyenne.

CE QUI A ETE REFUTE AVANT D'ARRIVER ICI, et qui explique la forme de ce module.
Une premiere statistique mesurait la DISPERSION du profil de bandes autour de sa
mediane (§ 12.6). Elle est morte deux fois (§ 12.7) : les stems de FUITE de
*Children* — des residus de separation a 0,2 et 0,6 % de l'energie — la
dominaient, parce qu'un son rare et erratique n'a pas de timbre stable, et une
porte batie dessus aurait decoupe en quatre voix un residu, c'est-a-dire
fabrique des pistes.

CE QUI SEPARE LES DEUX CAS EST LA PERSISTANCE. Une partie qui entre fait une
MARCHE : un profil neuf qui s'installe et DURE. Un residu fait une oscillation :
chaque fenetre differe de la suivante et rien ne tient. On compte donc des
PALIERS, pas de l'ecart.

LES DEUX SEUILS SONT ECRITS AU § 12.8, AVANT LA MESURE, et ils n'ont pas bouge
apres elle : distance L1 de 0,30 entre deux profils voisins, palier compte a
partir de 4 fenetres (20 s). Mesure publiee au § 12.9 : `other` 4 timbres,
`bass` 3, `piano` 1, `guitar` 0, `vocals` 0.
"""

from __future__ import annotations

from typing import List, Tuple

import numpy as np

FENETRE_SECONDES = 5.0
PLANCHER_DB = -50.0
SEUIL_L1 = 0.30
DUREE_MINIMALE = 4          # fenetres, soit 20 s
NOMBRE_DE_BANDES = 8
BANDES = np.geomspace(60.0, 16000.0, NOMBRE_DE_BANDES + 1)


def profil_de_bandes(bloc: np.ndarray, sample_rate: int) -> np.ndarray:
    """Le TIMBRE d'une fenetre : huit bandes log, normalisees a somme 1.

    Normalise, donc insensible au NIVEAU : une partie qui joue plus fort ne doit
    pas passer pour une autre partie.
    """
    spectre = np.abs(np.fft.rfft(bloc * np.hanning(len(bloc))))
    freqs = np.fft.rfftfreq(len(bloc), 1.0 / sample_rate)
    bandes = np.array(
        [float(spectre[(freqs >= BANDES[i]) & (freqs < BANDES[i + 1])].sum()) ** 2
         for i in range(NOMBRE_DE_BANDES)], dtype=np.float64)
    total = bandes.sum()
    return bandes / total if total > 0 else bandes


def timbres_installes(audio: np.ndarray, sample_rate: int) -> Tuple[int, List[Tuple[float, float]]]:
    """Rend (nombre de timbres installes, plages [(debut, duree) en secondes]).

    Un TROU DE SILENCE ROMPT LE PALIER : ce qui reprend apres n'est pas la meme
    tenue, et coller les deux ferait passer deux entrees d'une meme partie pour
    une seule. C'est aussi ce qui met les residus de separation a zero.
    """
    n = int(FENETRE_SECONDES * sample_rate)
    if n <= 0 or audio.size < n:
        return 0, []
    fenetres: List[Tuple[int, np.ndarray]] = []
    for k, debut in enumerate(range(0, audio.size - n + 1, n)):
        bloc = audio[debut:debut + n]
        rms = float(np.sqrt(np.mean(np.square(bloc, dtype=np.float64))))
        if rms <= 0.0 or 20.0 * np.log10(rms) < PLANCHER_DB:
            continue
        fenetres.append((k, profil_de_bandes(bloc, sample_rate)))
    if not fenetres:
        return 0, []

    groupes: List[List[Tuple[int, np.ndarray]]] = []
    courant = [fenetres[0]]
    for precedent, suivant in zip(fenetres, fenetres[1:], strict=False):
        rupture = (suivant[0] != precedent[0] + 1
                   or float(np.abs(suivant[1] - precedent[1]).sum()) >= SEUIL_L1)
        if rupture:
            groupes.append(courant)
            courant = []
        courant.append(suivant)
    groupes.append(courant)

    longs = [g for g in groupes if len(g) >= DUREE_MINIMALE]
    distincts: List[np.ndarray] = []
    plages: List[Tuple[float, float]] = []
    for groupe in longs:
        median = np.median(np.vstack([p for _, p in groupe]), axis=0)
        if all(float(np.abs(median - d).sum()) >= SEUIL_L1 for d in distincts):
            distincts.append(median)
        plages.append((groupe[0][0] * FENETRE_SECONDES, len(groupe) * FENETRE_SECONDES))
    return len(distincts), plages


def plainte_de_paliers(nombre: int, plages: List[Tuple[float, float]]) -> str:
    """La phrase du journal, ou "" si le stem ne porte qu'un timbre.

    DITE ET NON DEVINEE : le § 8.3 du cahier interdit qu'une porte s'ouvre sans
    qu'on sache pourquoi, et le compte seul ne dit pas OU les parties entrent.
    """
    if nombre < 2:
        return ""
    ou = ", ".join(f"{debut:.0f}-{debut + duree:.0f} s" for debut, duree in plages[:5])
    return (f"ATTENTION : ce stem porte {nombre} timbres qui s'INSTALLENT — des parties "
            f"y entrent et en sortent ({ou}"
            f"{', …' if len(plages) > 5 else ''}). La polyphonie ne les voit pas : "
            f"deux parties qui ne sonnent pas ensemble ne la font pas monter.")
