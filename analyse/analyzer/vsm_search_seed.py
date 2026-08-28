"""
Amorce GUIDÉE de la recherche de patch (étape 10.2 de la feuille de route).

LE PROBLÈME. La recherche part aujourd'hui d'une population tirée au hasard :
elle passe donc ses premières générations à découvrir des choses que la cible
dit déjà. Si le son à reproduire a son centre de gravité spectral à 900 Hz, il
est inutile d'aller essayer une coupure à 12 kHz pour l'apprendre -- la mesure
la donne en une milliseconde.

L'IDÉE, et sa limite. On ne PRÉDIT pas le patch : personne ne sait déduire une
résonance d'un spectre moyen. On fournit un point de départ raisonnable pour
les quelques dimensions où la mesure dit vraiment quelque chose -- la coupure,
et la forme de l'enveloppe d'amplitude. Les autres partent au milieu de leur
intervalle, ce qui ne vaut ni mieux ni moins bien qu'un tirage.

POURQUOI C'EST SANS RISQUE. L'amorce est UN membre de la population initiale,
pas une contrainte. Si elle est mauvaise, l'optimiseur s'en écarte comme de
n'importe quel autre point ; s'il elle est bonne, il part de plus près. Le pire
cas est donc « aussi bon qu'avant », et c'est ce que la mesure doit confirmer.
"""

from __future__ import annotations

from typing import Dict, Optional, Sequence

import numpy as np


def measure_target(audio: np.ndarray, sample_rate: int) -> Dict[str, float]:
    """
    Caractéristiques de la cible qui se traduisent en réglages.

    Volontairement peu nombreuses : chaque grandeur retenue doit correspondre à
    un paramètre dont on sait dire dans quel sens il bouge. Ajouter des
    descripteurs qu'on ne sait pas traduire ne ferait que donner l'illusion
    d'une amorce mieux informée.
    """
    if audio.size == 0:
        return {}

    # --- centre de gravité spectral, pondéré par l'énergie -------------------
    # Pondéré : les trames silencieuses ont un centroïde arbitraire, et les
    # inclure à poids égal tirerait la mesure n'importe où.
    fenetre = 2048
    saut = 512
    if audio.size >= fenetre:
        trames = np.lib.stride_tricks.sliding_window_view(audio, fenetre)[::saut]
        spectre = np.abs(np.fft.rfft(trames * np.hanning(fenetre), axis=1))
        frequences = np.fft.rfftfreq(fenetre, 1.0 / sample_rate)
        energie = spectre.sum(axis=1)
        centroides = (spectre * frequences).sum(axis=1) / np.maximum(energie, 1e-12)
        poids = energie / max(float(energie.sum()), 1e-12)
        centroide = float(np.sum(centroides * poids))
    else:
        centroide = float(sample_rate) / 8.0

    # --- enveloppe d'amplitude ------------------------------------------------
    # ENVELOPPE EN VALEUR EFFICACE, sur des fenêtres de 25 ms, et c'est
    # nécessaire : une enveloppe prise sur |x| lissé à 5 ms oscille encore au
    # rythme de la note elle-même (une note à 130 Hz a une période de 7,7 ms).
    # Mesurée ainsi, la première version voyait un déclin de 2,8 ms là où il
    # était de 350 ms -- soit un facteur 125 -- et l'amorce partait à côté.
    fenetre_env = max(1, int(0.025 * sample_rate))
    saut_env = max(1, fenetre_env // 4)
    if audio.size < fenetre_env * 2:
        return {"centroid": centroide}
    blocs = np.lib.stride_tricks.sliding_window_view(audio, fenetre_env)[::saut_env]
    enveloppe = np.sqrt(np.mean(blocs.astype(np.float64) ** 2, axis=1))
    crete = float(np.max(enveloppe))
    if crete <= 0.0:
        return {"centroid": centroide}

    par_seconde = sample_rate / saut_env
    index_crete = int(np.argmax(enveloppe))
    attaque = float(index_crete) / par_seconde

    # FIN DE LA NOTE : premier instant, après la crête, où l'énergie retombe
    # durablement sous 10 % de la crête. Sans ce repère, la queue de
    # relâchement était comptée dans le maintien et le tirait vers zéro : sur
    # une cible de maintien 0,4, la mesure rendait 0,147.
    apres = enveloppe[index_crete:]
    faibles = np.nonzero(apres < crete * 0.10)[0]
    fin_note = int(faibles[0]) if faibles.size else apres.size

    # MAINTIEN : niveau du dernier tiers de la note tenue, pas de tout ce qui
    # suit la crête -- le début de ce segment appartient encore au déclin.
    debut_maintien = max(1, int(fin_note * 0.6))
    segment = apres[debut_maintien:fin_note] if fin_note > debut_maintien else apres[:fin_note]
    maintien = float(np.median(segment)) / crete if segment.size else 0.5

    # DÉCLIN : temps mis, après la crête, pour rejoindre le maintien à moitié.
    cible_declin = crete * (maintien + (1.0 - maintien) * 0.5)
    sous = np.nonzero(apres[:fin_note] <= cible_declin)[0]
    declin = float(sous[0]) / par_seconde if sous.size else float(fin_note) / par_seconde

    # RELÂCHEMENT : durée de la chute après la fin de la note. Vaut le déclin
    # par défaut, faute de mieux, quand la note n'est pas relâchée dans
    # l'extrait.
    queue = apres[fin_note:]
    if queue.size > 2:
        eteintes = np.nonzero(queue < crete * 0.02)[0]
        relachement = float(eteintes[0]) / par_seconde if eteintes.size else float(queue.size) / par_seconde
    else:
        relachement = declin

    return {
        "centroid": centroide,
        "attack": max(1e-4, attaque),
        "decay": max(1e-3, declin),
        "sustain": float(np.clip(maintien, 0.0, 1.0)),
        "release": max(1e-3, relachement),
    }


# Ce que chaque caractéristique mesurée renseigne. Une entrée par paramètre
# qu'on sait VRAIMENT déduire ; le reste part au milieu, ce qui est honnête.
_GUIDED: Dict[str, str] = {
    "filter.1.cutoff": "centroid",
    "filter.2.cutoff": "centroid",
    "envelope.1.attack": "attack",
    "envelope.1.decay": "decay",
    "envelope.1.sustain": "sustain",
    "envelope.1.release": "release",
}


def _position(valeur: float, low: float, high: float, logarithmic: bool) -> float:
    """Position normalisée (0..1) d'une valeur dans l'intervalle de recherche."""
    valeur = float(np.clip(valeur, low, high))
    if high <= low:
        return 0.5
    if logarithmic and low > 0.0:
        return float((np.log(valeur) - np.log(low)) / (np.log(high) - np.log(low)))
    return float((valeur - low) / (high - low))


def guided_vector(
    space: Sequence,
    measured: Dict[str, float],
) -> Optional[np.ndarray]:
    """
    Point de départ normalisé, dans l'espace de recherche donné.

    Renvoie None si la mesure n'apporte rien pour cet espace -- auquel cas
    l'appelant garde son tirage habituel plutôt que de partir d'un vecteur
    entièrement médian, qui serait un biais sans information.
    """
    if not measured:
        return None
    vecteur = np.full(len(space), 0.5, dtype=float)
    renseignes = 0
    for index, parametre in enumerate(space):
        source = _GUIDED.get(parametre.semantic_id)
        if source is None or source not in measured:
            continue
        vecteur[index] = _position(
            measured[source], parametre.low, parametre.high, parametre.logarithmic
        )
        renseignes += 1
    return vecteur if renseignes > 0 else None


def guided_population(
    space: Sequence,
    measured: Dict[str, float],
    size: int,
    rng: np.random.Generator,
    spread: float = 0.18,
    neighbour_fraction: float = 0.0,
) -> Optional[np.ndarray]:
    """
    Population initiale : un HYPERCUBE LATIN dont on remplace quelques membres
    par l'amorce et son voisinage.

    LE POINT À NE PAS MANQUER, et il a coûté une mesure trompeuse : la
    population par défaut de `differential_evolution` n'est pas un tirage
    uniforme, c'est un hypercube latin -- un tirage STRATIFIÉ, qui garantit que
    chaque dimension est explorée sur toute son étendue. Fournir sa propre
    population uniforme fait perdre cette stratification, et le remède est pire
    que le mal : à budget serré, une amorce parfaite posée dans une population
    uniforme donnait 1,02 là où le tirage stratifié seul donnait 0,53.

    On repart donc de l'hypercube et on n'y remplace que ce qu'il faut.
    """
    amorce = guided_vector(space, measured)
    if amorce is None:
        return None

    population = _latin_hypercube(size, len(space), rng)
    population[0] = amorce
    voisins = max(0, int(size * neighbour_fraction))
    for i in range(1, min(voisins, size)):
        population[i] = np.clip(amorce + rng.normal(0.0, spread, len(space)), 0.0, 1.0)
    return population


def _latin_hypercube(size: int, dimensions: int, rng: np.random.Generator) -> np.ndarray:
    """
    Tirage stratifié : chaque dimension est découpée en `size` tranches
    d'égale largeur, et chaque tranche reçoit exactement un point. C'est ce que
    fait `differential_evolution` par défaut, et le reproduire est nécessaire
    pour que l'amorce s'ajoute à cette qualité au lieu de s'y substituer.
    """
    tranches = (rng.random((size, dimensions)) + np.arange(size)[:, None]) / size
    for colonne in range(dimensions):
        rng.shuffle(tranches[:, colonne])
    return tranches
