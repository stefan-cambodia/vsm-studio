"""
Corpus engendré par le moteur, et estimateur audio -> paramètres.

C'EST LA PISTE GARDÉE EN RÉSERVE au §6 de la feuille de route : « il faudrait
un corpus étiqueté que personne n'a ; et le moteur, lui, sait déjà produire des
paires (paramètres -> audio) à volonté ». Elle a été menée jusqu'au bout et
MESURÉE. Verdict : **elle ne sert pas la reconstruction**, et le présent module
existe pour que le chemin ne soit pas refait sans le savoir.

Ce qui marche, et qui n'était pas acquis
----------------------------------------
Le corpus se fabrique effectivement pour rien : 9 755 exemples utilisables en
4 minutes (tirage uniforme dans l'espace déclaré par la machine, rendu par le
moteur réel, descripteurs calculés). L'estimateur appris dessus est très
au-dessus du hasard -- sur 120 cibles d'épreuve rendues par la machine
elle-même, distance médiane du patch prédit :

    tirage au hasard   0,5208
    ridge              0,1798      (mieux que le hasard 91 % du temps)
    forêt aléatoire    0,1825      (97 %)
    perceptron         0,1867      (95 %)

Ce qui ne marche pas, et c'était le but
---------------------------------------
La prédiction ne remplace pas la recherche, et ne l'accélère pas de façon
fiable. Sur les mêmes 14 cibles, à 10 axes :

    A  prédiction seule                 0,2223     0,0 s     bat B 7 % du temps
    B  recherche 20 itérations          0,0515    31,0 s     (référence)
    C  boîte ±0,15 autour, 5 it.        0,0597     8,7 s     bat B 29 %
    D  boîte ±0,15 autour, 20 it.       0,0433    30,6 s     bat B 50 %

L'idée de la BOÎTE était le seul angle que l'étape 10.2 n'avait pas pu
essayer : 10.2 disposait d'un point, qui se dilue dans une population de
trente-six ; ici on tient une région, ce qui réduit le problème au lieu de
déplacer son départ. La médiane s'améliore bien (-16 %), mais D ne gagne que
la moitié du temps et régresse jusqu'à **5,1x** quand la prédiction est
mauvaise : elle enferme alors la recherche loin de la solution.

Le garde-fou, qui marche, et qui ne suffit pas
----------------------------------------------
La prédiction sait dire si on peut lui faire confiance : sa PROPRE distance à
la cible (un rendu, ~12 ms) est corrélée à -0,66 au gain qu'elle apportera. En
ne resserrant que sous un seuil de 0,25 :

    D prudent    0,0406   (-21 % contre B, à coût égal)   pire régression 1,4x
    C prudent    0,0514   (identique à B, en 18,3 s)      soit 1,7x plus vite

Ces deux points de fonctionnement seraient utilisables... sur des cibles que la
machine sait produire. Or ce n'est jamais le cas en production.

Pourquoi c'est refusé : le fossé de domaine
-------------------------------------------
Sur 9 cibles RÉELLES -- extraits des stems séparés de House Of God, avec leurs
artefacts de séparation et leurs fuites d'autres instruments -- l'estimateur
s'effondre :

    A  prédiction seule                 0,2708   (+37 % contre B, pire 5,9x)
    D  boîte sans garde-fou             0,2032   (+3 %)
    D  prudent (garde-fou actif)        0,1974   (identique à B)

Le garde-fou fait son travail : il refuse de resserrer sur 8 cibles réelles
sur 9, donc la méthode ne NUIT pas -- elle se ramène simplement à la recherche
ordinaire. La cause est structurelle et non un défaut de modèle : le corpus ne
contient que des sons que `vsm.generic` sait produire, alors qu'un stem séparé
est un son que AUCUNE machine ne produit. On demande à l'estimateur d'inverser
une application en dehors de son image.

À quelle condition rouvrir le dossier
-------------------------------------
Il faudrait un corpus qui contienne la DÉGRADATION, pas seulement le son
propre : rendre le patch, le mélanger à d'autres stems, faire repasser le tout
par demucs, et étiqueter le résultat avec le patch d'origine. C'est faisable et
c'est cher (une séparation par exemple, contre 25 ms aujourd'hui). Tant que ce
corpus-là n'existe pas, l'estimateur restera un bon inverseur de ce que le
moteur produit, et un mauvais lecteur de ce qu'un disque contient.
"""

from __future__ import annotations

import time
from typing import Callable, List, Optional, Sequence, Tuple

import numpy as np

from .audio_distance import envelope, spectral_features
from .audio_distance_v2 import spectral_contrast
from .vsm_engine import VsmEngine
from .vsm_patch_optimizer import SearchParameter, _vector_to_parameters

# Plages de tirage des CONDITIONS DE JEU. Elles couvrent ce que la chaîne
# produit réellement : `_representative_note` rend des extraits de 0,4 à 1,5 s,
# tenus de 30 à 100 %, sur des notes de basse comme de nappe aiguë.
NOTES = (24, 96)
GATES = (0.30, 1.00)
DUREES = (0.40, 1.50)

# En deçà, un rendu est considéré inaudible : ses descripteurs décrivent du
# bruit numérique et non le patch, et l'apprendre associerait un vecteur de
# paramètres à des grandeurs qui n'en disent rien.
RMS_MINIMAL = 1e-4

# Seuil de confiance du garde-fou : au-delà de cette distance, la prédiction
# n'est pas assez sûre pour qu'on resserre l'espace autour d'elle. Mesuré, pas
# choisi -- voir l'en-tête.
SEUIL_CONFIANCE = 0.25


def descriptors(audio: np.ndarray, sample_rate: int, midi_note: int,
                gate: float, duration: float) -> np.ndarray:
    """
    Vecteur d'entrée de l'estimateur : 43 grandeurs.

    Ce sont EXACTEMENT celles sur lesquelles la distance v2 juge, plus
    l'enveloppe rééchantillonnée et les trois conditions de jeu. Réutiliser les
    mêmes n'est pas de la paresse : si l'estimateur voyait autre chose que ce
    qui sert à le noter, un échec ne dirait pas s'il ignore la bonne
    information ou s'il ne sait pas s'en servir.

    Les conditions de jeu sont INDISPENSABLES : sans elles, deux sons
    identiques joués à des notes différentes réclameraient le même patch, ce
    qui est faux -- une même coupure ne colore pas de la même façon un la1 et
    un la5.
    """
    f = spectral_features(audio, sample_rate)
    env = envelope(audio)
    env16 = (np.interp(np.linspace(0, 1, 16), np.linspace(0, 1, len(env)), env)
             if len(env) > 1 else np.zeros(16))
    return np.concatenate([
        [np.log10(max(float(f["centroid"]), 1.0)),
         np.log10(max(float(f["bandwidth"]), 1.0)),
         np.log10(max(float(f["rolloff"]), 1.0)),
         np.log10(max(float(f["flatness"]), 1e-8))],
        np.asarray(f["mfcc"], dtype=float),
        np.asarray(spectral_contrast(audio, sample_rate), dtype=float),
        env16,
        [midi_note / 127.0, float(gate), float(duration)],
    ])


def generate_corpus(
    machine: str,
    space: Sequence[SearchParameter],
    engine: VsmEngine,
    count: int,
    sample_rate: int = 44100,
    seed: int = 11,
    progress: Optional[Callable[[str], None]] = None,
) -> Tuple[np.ndarray, np.ndarray]:
    """
    `count` paires (descripteurs, vecteur normalisé de paramètres).

    Rend environ 40 exemples par seconde sur une machine de bureau : le corpus
    étiqueté que « personne n'a » coûte quatre minutes.
    """
    rng = np.random.default_rng(seed)
    X: List[np.ndarray] = []
    Y: List[np.ndarray] = []
    depart = time.perf_counter()
    for index in range(count):
        vector = rng.random(len(space))
        note = int(rng.integers(NOTES[0], NOTES[1] + 1))
        gate = float(rng.uniform(*GATES))
        duree = float(rng.uniform(*DUREES))
        try:
            audio = engine.render_note(
                machine, _vector_to_parameters(space, vector), midi_note=note,
                duration=duree, gate=gate, sample_rate=sample_rate)
        except Exception:
            # Une requête refusée ne doit pas interrompre une génération de
            # plusieurs minutes ; l'exemple est simplement absent du corpus.
            continue
        if audio.size == 0 or not np.isfinite(audio).all():
            continue
        if float(np.sqrt(np.mean(audio.astype(np.float64) ** 2))) < RMS_MINIMAL:
            continue
        X.append(descriptors(audio, sample_rate, note, gate, duree))
        Y.append(vector)
        if progress is not None and (index + 1) % 1000 == 0:
            progress(f"{index + 1}/{count} tirés, {len(X)} retenus, "
                     f"{time.perf_counter() - depart:.0f} s")
    return np.asarray(X), np.asarray(Y)


def narrowed_space(space: Sequence[SearchParameter], centre: Sequence[float],
                   radius: float = 0.15) -> List[SearchParameter]:
    """
    Sous-espace de recherche centré sur une prédiction.

    `radius` est une fraction de l'intervalle NORMALISÉ de chaque axe. La boîte
    est ramenée dans les bornes déclarées par la machine : on ne sort jamais de
    ce qu'elle dit savoir faire. Un axe dont la boîte serait vide est rouvert
    en entier plutôt que réduit à un point.

    À N'EMPLOYER QU'AVEC LE GARDE-FOU (`is_prediction_trustworthy`) : sans lui,
    une mauvaise prédiction enferme la recherche et la distance régresse d'un
    facteur cinq.
    """
    resserre: List[SearchParameter] = []
    for parametre, valeur in zip(space, centre, strict=True):
        bas = float(np.clip(valeur - radius, 0.0, 1.0))
        haut = float(np.clip(valeur + radius, 0.0, 1.0))
        if haut - bas < 1e-6:
            bas, haut = 0.0, 1.0
        if parametre.logarithmic:
            rapport = parametre.high / parametre.low
            low, high = parametre.low * rapport ** bas, parametre.low * rapport ** haut
        else:
            etendue = parametre.high - parametre.low
            low, high = parametre.low + etendue * bas, parametre.low + etendue * haut
        resserre.append(SearchParameter(parametre.semantic_id, low, high,
                                        parametre.logarithmic))
    return resserre


def is_prediction_trustworthy(distance: float,
                              threshold: float = SEUIL_CONFIANCE) -> bool:
    """
    La prédiction mérite-t-elle qu'on resserre l'espace autour d'elle ?

    On ne le devine pas : on REND le patch prédit et on mesure sa distance à la
    cible, ce qui coûte un rendu (~12 ms) contre une recherche complète de
    trente secondes. C'est ce jugement qui empêche la boîte de nuire -- sur des
    cibles réelles, il refuse de resserrer huit fois sur neuf, et la méthode se
    ramène alors sans dommage à la recherche ordinaire.
    """
    return float(distance) <= threshold
