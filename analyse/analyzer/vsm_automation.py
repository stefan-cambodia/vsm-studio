"""
Extraction d'une AUTOMATION de coupure depuis un stem, et sa mise à l'épreuve.

POURQUOI CE MODULE EXISTE. La recherche de patch règle UN patch sur UN extrait
représentatif, puis l'applique à toutes les notes du stem. Or le caractère
d'un morceau vit souvent dans le MOUVEMENT : la coupure qui balaye est l'âme
d'une ligne acide, et un patch figé n'en garde qu'une moyenne. Ce module
extrait la trajectoire de brillance du stem et la traduit en courbe
d'automation sur `filter.1.cutoff`.

LA RÈGLE QUI GOUVERNE TOUT : l'automation n'est gardée QUE si la distance
mesurée baisse. L'extraction est une heuristique (le centroïde spectral n'est
pas la coupure, il ne fait que la suivre) ; la seule chose qui la légitime est
un chiffre, stem par stem. Le verdict passe par le VRAI chemin -- deux
mini-projets rendus par `vsm-render`, avec et sans la courbe -- et jamais par
une approximation du rendu.

APPROXIMATION ASSUMÉE, écrite plutôt que découverte : la coupure est modulée
MULTIPLICATIVEMENT autour de la valeur trouvée par la recherche, au rythme du
centroïde du stem rapporté à sa médiane. L'exposant vaut 1 -- le centroïde
d'un passe-bas suit sa coupure à peu près linéairement dans la zone utile --
et si cette hypothèse est fausse pour un stem, la mesure le dira et la courbe
sera rejetée.
"""

from __future__ import annotations

import json
import subprocess
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

from .audio_distance import audio_distance
from .vsm_engine import VsmEngine, find_vsm_render
from .vsm_project_export import ExportNote, ExportTrack, write_project_bundle

# Points par seconde de la courbe écrite. Deux suffisent : un balayage de
# filtre musical se joue à l'échelle du temps, pas de la double-croche, et le
# moteur interpole linéairement entre les points.
POINTS_PER_SECOND = 2.0

# Fenêtre du lissage médian, en secondes. UNE SECONDE, et c'est le cœur de la
# méthode : le centroïde par trame suit AUSSI l'enveloppe interne de chaque
# note -- que la machine reproduit déjà d'elle-même. Extraire ce mouvement-là
# et l'écrire en automation l'appliquerait DEUX FOIS ; mesuré sur une cible
# balayée connue, la courbe par trame AGGRAVAIT la distance (5,25 -> 6,12)
# quand la courbe idéale la ramenait à zéro. Une fenêtre d'une seconde ne
# laisse passer que la tendance lente : le geste du musicien sur le filtre,
# pas le travail de l'enveloppe.
TREND_WINDOW_SECONDS = 1.0

# En deçà de ce rapport d'énergie à la trame la plus forte, la trame est du
# silence : son centroïde ne veut rien dire (il mesurerait le bruit de fond),
# on tient la dernière valeur entendue.
SILENCE_RATIO = 1e-3


def extract_centroid_trend(
    audio: np.ndarray,
    sample_rate: int,
    until_seconds: Optional[float] = None,
) -> List[Tuple[float, float]]:
    """
    La TENDANCE du centroïde spectral du stem : [(seconde, hertz)].

    Tendance, et rien d'autre : le lissage médian d'une seconde (voir
    TREND_WINDOW_SECONDS) efface le mouvement interne des notes -- l'enveloppe
    de filtre que la machine reproduit déjà -- et ne garde que le geste lent.
    `until_seconds` coupe la courbe à la fin de la dernière note : au-delà il
    n'y a que la queue du son, dont le centroïde plonge et tirerait le dernier
    point vers le grave.
    """
    import librosa

    hop = 2048
    centroide = librosa.feature.spectral_centroid(
        y=audio.astype(np.float32), sr=sample_rate, hop_length=hop
    )[0]
    energie = librosa.feature.rms(y=audio.astype(np.float32), hop_length=hop)[0]
    if centroide.size < 8:
        return []

    plancher = float(np.max(energie)) * SILENCE_RATIO
    for i in range(1, centroide.size):
        if energie[i] < plancher:
            centroide[i] = centroide[i - 1]

    noyau = max(5, int(round(TREND_WINDOW_SECONDS * sample_rate / hop)))
    lisse = np.array([
        float(np.median(centroide[max(0, i - noyau // 2):i + noyau // 2 + 1]))
        for i in range(centroide.size)
    ])

    pas = max(1, int(round(sample_rate / hop / POINTS_PER_SECOND)))
    points: List[Tuple[float, float]] = []
    for i in range(0, lisse.size, pas):
        seconde = i * hop / sample_rate
        if until_seconds is not None and seconde > until_seconds:
            break
        points.append((seconde, float(lisse[i])))
    return points


def _median_centroid(audio: np.ndarray, sample_rate: int) -> float:
    """Centroïde médian des trames non silencieuses d'un rendu court."""
    import librosa

    hop = 1024
    centroide = librosa.feature.spectral_centroid(
        y=audio.astype(np.float32), sr=sample_rate, hop_length=hop
    )[0]
    energie = librosa.feature.rms(y=audio.astype(np.float32), hop_length=hop)[0]
    masque = energie >= float(np.max(energie)) * 0.05
    if not np.any(masque):
        return 0.0
    return float(np.median(centroide[masque]))


def calibrate_centroid_to_cutoff(
    engine: VsmEngine,
    machine: str,
    parameters: Dict[str, float],
    midi_note: int,
    cutoff_low: float,
    cutoff_high: float,
    sample_rate: int,
) -> Optional[Tuple[float, float, float, float]]:
    """
    Apprend la relation centroïde -> coupure SUR LA MACHINE ELLE-MÊME.

    Une première version supposait la relation 1:1 (coupure = base x
    centroïde/médiane) : mesuré sur une cible balayée connue, la courbe
    extraite était trois fois trop plate -- la fondamentale de la note ancre
    le bas du spectre, et le centroïde suit la coupure de loin. Plutôt que de
    choisir un exposant au jugé, on rend le patch du candidat à coupure basse
    puis haute, on mesure SES centroïdes, et on inverse cette relation-là par
    interpolation log-log. Deux rendus d'une seconde, et plus aucune
    hypothèse sur la pente.
    """
    # Calibrer AUTOUR DU POINT DE FONCTIONNEMENT, jamais aux bornes : à la
    # borne basse d'un filtre grand ouvert (20 Hz), le rendu est quasi muet et
    # le centroïde mesuré est celui du bruit numérique -- large bande, donc
    # HAUT, et la calibration concluait « la coupure ne pilote rien » sur une
    # machine où elle pilote tout. Un facteur quatre de part et d'autre de la
    # coupure trouvée couvre la zone où la courbe vivra, et l'interpolation
    # log-log extrapole le reste ; les valeurs écrites restent bornées au réel.
    base = float(parameters.get("filter.1.cutoff", 0.0)) or (cutoff_low * cutoff_high) ** 0.5
    cal_bas = float(np.clip(base / 4.0, cutoff_low, cutoff_high))
    cal_haut = float(np.clip(base * 4.0, cutoff_low, cutoff_high))
    if cal_haut < cal_bas * 2.0:
        return None
    bas = dict(parameters); bas["filter.1.cutoff"] = cal_bas
    haut = dict(parameters); haut["filter.1.cutoff"] = cal_haut
    try:
        rendu_bas = engine.render_note(machine, bas, midi_note=midi_note,
                                        duration=1.0, sample_rate=sample_rate)
        rendu_haut = engine.render_note(machine, haut, midi_note=midi_note,
                                         duration=1.0, sample_rate=sample_rate)
    except Exception:
        return None
    c_bas = _median_centroid(rendu_bas, sample_rate)
    c_haut = _median_centroid(rendu_haut, sample_rate)
    if c_bas <= 0.0 or c_haut <= 0.0:
        return None
    # La pente peut être NÉGATIVE, et c'est une découverte de la mesure : sur
    # un type de filtre continu réglé vers le passe-bande, monter la coupure
    # ASSOMBRIT la note (mesuré sur un patch trouvé par la recherche :
    # 424 Hz -> centroïde 2672, 6776 Hz -> centroïde 1242). La relation
    # inversée reste une relation ; l'interpolation log-log la suit telle
    # quelle, et l'épreuve A/B tranche comme pour tout le monde. On ne refuse
    # que l'ABSENCE de relation : moins de la moitié d'une octave de centroïde
    # entre les deux bornes de calibration.
    rapport = c_haut / c_bas
    if 1.0 / 1.4 < rapport < 1.4:
        return None  # la coupure ne pilote pas le centroïde sous ce patch
    return c_bas, c_haut, cal_bas, cal_haut


def map_trend_to_cutoff(
    trend: Sequence[Tuple[float, float]],
    calibration: Tuple[float, float, float, float],
    limites: Optional[Tuple[float, float]] = None,
) -> List[Tuple[float, float]]:
    """
    Traduit la tendance de centroïde en coupure, par la relation apprise.

    `limites` : les bornes RÉELLES du paramètre. La calibration est locale
    (autour du point de fonctionnement) ; la droite log-log extrapole au-delà,
    et l'écrêtage final se fait sur ce que la machine accepte vraiment.
    """
    c_bas, c_haut, cut_bas, cut_haut = calibration
    borne_bas, borne_haut = limites if limites else (cut_bas, cut_haut)
    log_c = (np.log(c_bas), np.log(c_haut))
    log_cut = (np.log(max(cut_bas, 1.0)), np.log(cut_haut))
    pente = (log_cut[1] - log_cut[0]) / (log_c[1] - log_c[0])
    points = []
    for seconde, centroide in trend:
        if centroide <= 0.0:
            continue
        log_valeur = log_cut[0] + pente * (np.log(centroide) - log_c[0])
        coupure = float(np.clip(np.exp(log_valeur), borne_bas, borne_haut))
        points.append((seconde, coupure))
    return points


def cutoff_bounds(engine: VsmEngine, machine: str) -> Optional[Tuple[float, float]]:
    """Bornes RÉELLES de `filter.1.cutoff`, ou None si la machine n'en a pas."""
    try:
        for parametre in engine.parameters(machine):
            if parametre.get("id") == "filter.1.cutoff":
                return float(parametre["min"]), float(parametre["max"])
    except Exception:
        return None
    return None


def _render_track(track: ExportTrack, folder: Path, duration: float,
                   sample_rate: int) -> Optional[np.ndarray]:
    """Rend une piste seule par le VRAI moteur hors ligne (`vsm-render`)."""
    write_project_bundle([track], folder, title="ab-automation")
    sortie = folder / "rendu.wav"
    try:
        subprocess.run(
            [str(find_vsm_render(None)), str(folder), str(sortie),
             "--sample-rate", str(sample_rate),
             "--duration", str(duration), "--quiet"],
            check=True, capture_output=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    donnees = sortie.read_bytes()
    position, data = 12, None
    while position < len(donnees) - 8:
        bloc = donnees[position:position + 4]
        taille = int.from_bytes(donnees[position + 4:position + 8], "little")
        if bloc == b"data":
            data = donnees[position + 8:position + 8 + taille]
            break
        position += 8 + taille + (taille & 1)
    if data is None:
        return None
    stereo = np.frombuffer(data, dtype="<f4")
    return stereo.reshape(-1, 2).mean(axis=1).astype(np.float32)


def try_cutoff_automation(
    stem_audio: np.ndarray,
    sample_rate: int,
    track: ExportTrack,
    engine: VsmEngine,
) -> Tuple[Optional[List[Tuple[float, float]]], Optional[float], Optional[float]]:
    """
    Met la courbe à l'épreuve : (courbe, distance sans, distance avec, motif).

    La courbe n'est renvoyée que si elle RAPPROCHE le rendu du stem, mesuré
    par la même distance que tout le reste de la chaîne, sur le rendu complet
    de la piste (toutes ses notes, pas un extrait). Quand elle ne l'est pas,
    `motif` dit POURQUOI -- la chaîne ne saute rien en silence.
    """
    bornes = cutoff_bounds(engine, track.machine)
    if bornes is None:
        return None, None, None, "la machine n'a pas de coupure"
    if track.parameters.get("filter.1.cutoff") is None:
        return None, None, None, "le patch ne règle pas la coupure"
    if not track.notes:
        return None, None, None, "aucune note"

    fin_des_notes = max(n.start + n.duration for n in track.notes)
    tendance = extract_centroid_trend(stem_audio, sample_rate, until_seconds=fin_des_notes)
    if len(tendance) < 4:
        return None, None, None, "stem trop court"
    valeurs = [c for _, c in tendance]
    if max(valeurs) < min(valeurs) * 1.1:
        return None, None, None, "le stem ne bouge pas"

    notes_medianes = sorted(n.note for n in track.notes)
    calibration = calibrate_centroid_to_cutoff(
        engine, track.machine, track.parameters,
        notes_medianes[len(notes_medianes) // 2], bornes[0], bornes[1], sample_rate)
    if calibration is None:
        return None, None, None, "la coupure ne pilote pas le timbre sous ce patch"
    courbe = map_trend_to_cutoff(tendance, calibration, limites=bornes)
    if not courbe:
        return None, None, None, "courbe vide"

    duree = stem_audio.size / float(sample_rate)
    with tempfile.TemporaryDirectory(prefix="vsm-ab-") as dossier:
        sans = _render_track(track, Path(dossier) / "sans", duree, sample_rate)
        avec_track = ExportTrack(**{**track.__dict__})
        avec_track.automation = dict(track.automation)
        avec_track.automation["filter.1.cutoff"] = courbe
        avec = _render_track(avec_track, Path(dossier) / "avec", duree, sample_rate)
    if sans is None or avec is None:
        return None, None, None, "rendu A/B impossible"

    n = min(stem_audio.size, sans.size, avec.size)
    if n == 0:
        return None, None, None, "rendu vide"
    distance_sans = float(audio_distance(stem_audio[:n], sans[:n], sample_rate))
    distance_avec = float(audio_distance(stem_audio[:n], avec[:n], sample_rate))
    if distance_avec < distance_sans:
        return courbe, distance_sans, distance_avec, "gardée"
    return None, distance_sans, distance_avec, "elle n'aide pas"
