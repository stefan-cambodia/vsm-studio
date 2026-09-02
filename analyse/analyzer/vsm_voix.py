"""Séparer la voix de TÊTE des CHŒURS, par le champ stéréo.

LE POURQUOI (§ 4.5 de docs/CDC-detection-multipiste.md). Le stem `vocals`
porte 22,7 % du morceau et part d'un bloc sur une piste audio : une voix de
tête, ses doublages et ses chœurs restent UNE piste, là où l'objectif de
parité les veut séparés.

LE COMMENT, et pourquoi celui-là. Séparer des voix DANS un stem de voix par
apprentissage demanderait un modèle qu'on n'a pas et une dépendance de plus.
Mais un mixage pose la voix de tête AU CENTRE du champ stéréo et élargit les
chœurs — c'est une convention de production presque universelle, et elle se
mesure : sur *Us and Them*, le stem vocal a 18 % de son énergie dans le canal
latéral (G−D), corrélation gauche/droite 0,66. L'extraction de centre est
donc un séparateur honnête : il ne prétend pas reconnaître des voix, il
sépare CE QUI EST AU CENTRE de ce qui ne l'est pas, et se nomme pour ça.

L'algorithme : STFT des deux canaux ; par case temps-fréquence, un MASQUE de
centre — l'accord des deux canaux en niveau (2|G||D| / (|G|²+|D|²)) multiplié
par leur accord en phase (cos de l'écart, borné à zéro). La tête est la
resynthèse masquée ; les chœurs sont LE COMPLÉMENT TEMPOREL (original moins
tête), ce qui garantit EXACTEMENT tête + chœurs = stem : rien ne peut se
perdre, et rejouer les deux pistes à l'unité redonne le stem au bit près
(aux arrondis d'écriture près).

LA PORTE : une voix mono ou entièrement centrée n'a rien à séparer — part
latérale sous le seuil, la fonction rend None et l'appelant le DIT, plutôt
que de livrer une piste « chœurs » quasi vide qui passerait pour une partie.

Tout est déterministe : fenêtres de Hann, pas de tirage, pas de dépendance
au-delà de numpy.
"""

from __future__ import annotations

import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Tuple

import numpy as np

# Fenêtre et saut : 2048/512 à 48 kHz ≈ 43 ms d'analyse, 75 % de recouvrement.
# Hann à saut quart de fenêtre : la somme des fenêtres AU CARRÉ est constante
# (COLA), la resynthèse par addition-recouvrement est exacte.
_FENETRE = 2048
_SAUT = 512

# Sous ce seuil de part latérale, il n'y a pas de chœurs discernables : on ne
# découpe pas. 5 % : un stem réellement arrangé (Us and Them : 18 %) est très
# au-dessus, une voix mono repliée en fausse stéréo très en dessous.
SEUIL_PART_LATERALE = 0.05


@dataclass
class VoixSeparee:
    tete: np.ndarray      # stéréo, (n, 2)
    choeurs: np.ndarray   # stéréo, (n, 2) — le complément exact
    part_laterale: float  # part d'énergie du canal G−D dans le stem


def _stft(signal: np.ndarray) -> np.ndarray:
    fenetre = np.hanning(_FENETRE)
    reste = (-(signal.size - _FENETRE)) % _SAUT
    signal = np.concatenate([signal, np.zeros(reste)])
    trames = np.lib.stride_tricks.sliding_window_view(signal, _FENETRE)[::_SAUT]
    return np.fft.rfft(trames * fenetre, axis=1)


def _istft(spectre: np.ndarray, taille: int) -> np.ndarray:
    fenetre = np.hanning(_FENETRE)
    trames = np.fft.irfft(spectre, n=_FENETRE, axis=1) * fenetre
    sortie = np.zeros((spectre.shape[0] - 1) * _SAUT + _FENETRE)
    poids = np.zeros_like(sortie)
    for i, trame in enumerate(trames):
        debut = i * _SAUT
        sortie[debut:debut + _FENETRE] += trame
        poids[debut:debut + _FENETRE] += fenetre * fenetre
    poids[poids < 1e-12] = 1.0
    return (sortie / poids)[:taille]


def separer_tete_et_choeurs(gauche: np.ndarray, droite: np.ndarray) -> Optional[VoixSeparee]:
    """Rend la séparation, ou None si le stem n'a pas de largeur à séparer."""
    gauche = np.asarray(gauche, dtype=np.float64)
    droite = np.asarray(droite, dtype=np.float64)
    lateral = (gauche - droite) / 2.0
    centre = (gauche + droite) / 2.0
    energie_totale = float(np.sum(centre**2) + np.sum(lateral**2))
    if energie_totale <= 0.0:
        return None
    part_laterale = float(np.sum(lateral**2)) / energie_totale
    if part_laterale < SEUIL_PART_LATERALE:
        return None

    # REMBOURRAGE D'UNE FENÊTRE DE ZÉROS AUX DEUX BOUTS, et il a été payé
    # pour l'apprendre : au bord du signal, la somme des fenêtres de Hann au
    # carré tend vers zéro, et la division de resynthèse y est exacte pour un
    # spectre INTACT mais explose pour un spectre MASQUÉ — la trame masquée ne
    # s'annule plus au bord, mesuré |r| = 2 132 sur les 64 premiers
    # échantillons d'un sinus d'amplitude 0,5. Avec le rembourrage, les bords
    # vivent dans du silence entièrement recouvert, et l'on rogne au retour.
    bourre = _FENETRE
    gauche_b = np.concatenate([np.zeros(bourre), gauche, np.zeros(bourre)])
    droite_b = np.concatenate([np.zeros(bourre), droite, np.zeros(bourre)])
    sg, sd = _stft(gauche_b), _stft(droite_b)
    ag, ad = np.abs(sg), np.abs(sd)
    # L'accord en niveau : 1 quand |G| = |D|, tombe vers 0 quand un canal
    # domine. L'accord en phase : cos de l'écart, borné à zéro — deux canaux
    # en opposition ne portent aucun centre.
    niveau = (2.0 * ag * ad) / (ag * ag + ad * ad + 1e-12)
    phase = np.cos(np.angle(sg) - np.angle(sd))
    masque = niveau * np.clip(phase, 0.0, None)

    n = gauche.size
    tete_g = _istft(masque * sg, bourre + n + bourre)[bourre:bourre + n]
    tete_d = _istft(masque * sd, bourre + n + bourre)[bourre:bourre + n]
    tete = np.stack([tete_g, tete_d], axis=1)
    original = np.stack([gauche, droite], axis=1)
    # LE COMPLÉMENT TEMPOREL, et c'est la garantie du module : quoi que vaille
    # le masque, tête + chœurs == stem, exactement.
    choeurs = original - tete
    return VoixSeparee(tete=tete, choeurs=choeurs, part_laterale=part_laterale)


def lire_wav_stereo(chemin: Path) -> Optional[Tuple[np.ndarray, np.ndarray, int]]:
    """(gauche, droite, taux), ou None si le fichier est mono.

    Le lecteur mono de la chaîne (`lire_wav`) replie les canaux — c'est son
    métier, la mesure travaille en mono — mais la séparation tête/chœurs vit
    précisément DANS ce que le pli efface. Elle lit donc le fichier
    elle-même, int16 ou float32.
    """
    with wave.open(str(chemin), "rb") as w:
        canaux = w.getnchannels()
        taux = w.getframerate()
        largeur = w.getsampwidth()
        brut = w.readframes(w.getnframes())
    if canaux != 2:
        return None
    if largeur == 2:
        valeurs = np.frombuffer(brut, dtype="<i2").astype(np.float64) / 32768.0
    elif largeur == 4:
        valeurs = np.frombuffer(brut, dtype="<f4").astype(np.float64)
    else:
        return None
    return valeurs[0::2], valeurs[1::2], taux


def ecrire_wav_stereo(chemin: Path, stereo: np.ndarray, taux: int) -> None:
    serre = np.clip(stereo, -1.0, 1.0)
    entiers = (serre * 32767.0).astype("<i2")
    with wave.open(str(chemin), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(taux)
        w.writeframes(entiers.reshape(-1).tobytes())
