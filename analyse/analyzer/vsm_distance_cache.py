"""
Distance audio à cible pré-calculée.

CONSTAT MESURÉ : dans une boucle d'optimisation, `audio_distance(target,
candidate, sr)` recalcule les caractéristiques de la CIBLE à chaque appel --
alors qu'elles ne changent jamais. Sur une note d'une seconde, une évaluation
coûte 66 ms de distance pour 10 ms de rendu : la métrique coûte six fois le
moteur audio, et la moitié de ce temps est du travail refait à l'identique des
milliers de fois.

Ce module ne remplace pas `audio_distance` : il en reprend EXACTEMENT la
formule et les pondérations (voir audio_distance.py), en mémorisant la partie
constante. Un test compare les deux sur des signaux réels pour garantir qu'on
n'a pas changé le critère en cours de route -- ce qui rendrait incomparables
deux recherches faites avant et après.
"""

from __future__ import annotations

import numpy as np

from .audio_distance import envelope, spectral_features


class CachedTargetDistance:
    """
    Distance vers une cible fixe. Même formule que `audio_distance`, avec les
    caractéristiques de la cible calculées une seule fois.

        distance = CachedTargetDistance(target_audio, sr)
        d = distance(candidate_audio)
    """

    def __init__(self, target: np.ndarray, sr: int):
        self.sr = sr
        self.target = np.asarray(target)
        # RÉÉCRITURE EN NUMPY PUR : ESSAYÉE, MESURÉE, ABANDONNÉE.
        #
        # La mesure a montré que comparer coûte autant que produire (≈8 ms de
        # distance pour 7,6 ms de rendu), ce qui désignait ce calcul comme le
        # premier poste à traiter. Une implémentation numpy équivalente au
        # millionième près a donc été écrite -- une seule découpe en trames
        # partagée, et une seule transformée en cosinus sur la moyenne des
        # trames au lieu d'une par trame.
        # À travail égal (le côté CANDIDAT seul, l'autre étant en cache) :
        # librosa 7,99 ms, numpy 8,46 ms. La bibliothèque n'était pas le
        # problème : le coût est celui de la transformée elle-même, 87 fenêtres
        # de 2048 points, et il ne se contourne pas en changeant d'outil.
        #
        # Un piège de mesure à retenir : comparer `audio_distance` (qui calcule
        # LES DEUX côtés) à ce cache (qui n'en calcule qu'un) donnait un
        # trompeur « 1,7 fois plus rapide ».
        self._features = spectral_features(self.target, sr)
        self._envelope = envelope(self.target)

    def __call__(self, candidate: np.ndarray) -> float:
        candidate = np.asarray(candidate)
        length = min(len(self.target), len(candidate))
        if length == 0:
            return 1e6

        # La troncature doit porter sur les DEUX signaux, comme dans
        # audio_distance : comparer une cible entière à un candidat plus court
        # avantagerait mécaniquement les sons qui s'arrêtent tôt.
        target = self.target[:length]
        candidate = candidate[:length]

        target_features = (
            self._features if length == len(self.target) else spectral_features(target, self.sr)
        )
        candidate_features = spectral_features(candidate, self.sr)

        centroid_error = abs(target_features["centroid"] - candidate_features["centroid"]) / 4000.0
        bandwidth_error = abs(target_features["bandwidth"] - candidate_features["bandwidth"]) / 4000.0
        rolloff_error = abs(target_features["rolloff"] - candidate_features["rolloff"]) / 8000.0
        flatness_error = abs(target_features["flatness"] - candidate_features["flatness"])
        mfcc_error = float(np.mean(np.abs(target_features["mfcc"] - candidate_features["mfcc"])))

        target_envelope = self._envelope if length == len(self.target) else envelope(target)
        candidate_envelope = envelope(candidate)
        common = min(len(target_envelope), len(candidate_envelope))
        envelope_error = float(np.mean(np.abs(target_envelope[:common] - candidate_envelope[:common])))

        return float(
            0.20 * centroid_error
            + 0.15 * bandwidth_error
            + 0.15 * rolloff_error
            + 0.10 * flatness_error
            + 0.25 * mfcc_error
            + 0.15 * envelope_error
        )


class CachedTargetDistanceV2:
    """
    Même service que `CachedTargetDistance`, pour la distance de deuxième
    version : les caractéristiques de la CIBLE sont calculées une fois.
    """

    def __init__(self, target: np.ndarray, sr: int):
        from .audio_distance_v2 import combine, distance_terms, spectral_contrast

        self.sr = sr
        self.target = np.asarray(target)
        self._features = spectral_features(self.target, sr)
        self._envelope = envelope(self.target)
        self._contrast = spectral_contrast(self.target, sr)
        self._combine = combine
        self._terms = distance_terms
        self._contrast_of = spectral_contrast

    def terms(self, candidate: np.ndarray) -> dict:
        candidate = np.asarray(candidate)
        length = min(len(self.target), len(candidate))
        if length == 0:
            return {}
        target = self.target[:length]
        candidate = candidate[:length]
        target_features = (
            self._features if length == len(self.target) else spectral_features(target, self.sr)
        )
        target_envelope = self._envelope if length == len(self.target) else envelope(target)
        target_contrast = (
            self._contrast if length == len(self.target) else self._contrast_of(target, self.sr)
        )
        return self._terms(
            target_features, spectral_features(candidate, self.sr),
            target_envelope, envelope(candidate),
            target_contrast, self._contrast_of(candidate, self.sr),
        )

    def __call__(self, candidate: np.ndarray) -> float:
        detail = self.terms(candidate)
        return self._combine(detail) if detail else 1e6


class CachedTargetDistanceV3:
    """Même service, pour la troisième version : les caractéristiques de la
    cible -- hauteur grave comprise -- sont calculées une fois."""

    def __init__(self, target: np.ndarray, sr: int):
        from .audio_distance_v2 import spectral_contrast
        from .audio_distance_v3 import combine, distance_terms, low_pitch

        self.sr = sr
        self.target = np.asarray(target)
        self._features = spectral_features(self.target, sr)
        self._envelope = envelope(self.target)
        self._contrast = spectral_contrast(self.target, sr)
        self._pitch = low_pitch(self.target, sr)
        self._combine = combine
        self._terms = distance_terms
        self._contrast_of = spectral_contrast
        self._pitch_of = low_pitch

    def terms(self, candidate: np.ndarray) -> dict:
        candidate = np.asarray(candidate)
        length = min(len(self.target), len(candidate))
        if length == 0:
            return {}
        target = self.target[:length]
        candidate = candidate[:length]
        entier = length == len(self.target)
        return self._terms(
            self._features if entier else spectral_features(target, self.sr),
            spectral_features(candidate, self.sr),
            self._envelope if entier else envelope(target), envelope(candidate),
            self._contrast if entier else self._contrast_of(target, self.sr),
            self._contrast_of(candidate, self.sr),
            self._pitch if entier else self._pitch_of(target, self.sr),
            self._pitch_of(candidate, self.sr),
        )

    def __call__(self, candidate: np.ndarray) -> float:
        detail = self.terms(candidate)
        return self._combine(detail) if detail else 1e6


METRIQUES = ("v1", "v2", "v3")


def cached_distance_for(metric: str):
    """LA fabrique, et la seule : cinq modules choisissaient la métrique chacun
    par un `if metric == "v2"`, si bien qu'une troisième version aurait été
    oubliée dans l'un d'eux sans que rien ne le dise. Une métrique inconnue
    est REFUSÉE, pas rabattue sur une autre -- une distance calculée avec la
    mauvaise métrique est un chiffre qui ment."""
    if metric == "v1":
        return CachedTargetDistance
    if metric == "v2":
        return CachedTargetDistanceV2
    if metric == "v3":
        return CachedTargetDistanceV3
    raise ValueError(f"métrique inconnue : « {metric} » (attendu : {', '.join(METRIQUES)})")
