"""
Le GÉNÉRATEUR du banc synthétique : des morceaux entiers à vérité connue.

POURQUOI. La chaîne publie une distance globale et ne sait pas dire à quel
étage elle perd — séparation, transcription, parité, arbitrage, réglage. Sur
un disque, on ne le saura jamais : il n'y a pas de vérité. Sur un morceau que
le moteur a fabriqué, on connaît tout par construction : les parties, la
machine et le patch de chacune, chaque note avec sa vélocité et sa durée,
les niveaux, les panoramiques, et les stems VRAIS. Ce module fabrique ces
morceaux ; `banc_synthetique.py` y fait tourner la chaîne et mesure chaque
étage contre CE qu'il devait produire (docs/CDC-banc-synthetique.md).

CE QUE CE MODULE N'EST PAS. Un corpus d'apprentissage : trois mesures
(ROADMAP-fusion § 7, ROADMAP-apprentissage A1.3 et A3.4) ont montré qu'un
rendu moteur, si dégradé soit-il, n'enseigne pas ce qu'un disque contient.
Le banc MESURE ; il ne remplace pas la validation sur disque.

SEEDÉ DE BOUT EN BOUT. Une graine → un morceau, au bit près : le tirage
(numpy `default_rng`), les patchs (tirés dans le SearchProfile déclaré par
le moteur, comme le corpus), les notes, les niveaux, la production. Le
moteur lui-même est déterministe (invariant de ROADMAP-fusion § 8 : deux
rendus identiques donnent le même audio). Testé sur deux générations.

LA VÉRITÉ ET LE RENDU SE RECOUPENT. Le mélange est la somme des stems vrais
écrits, dans l'ordre du fichier, en float64 puis float32 : la somme des WAV
redonne `morceau.wav` au bit près hors `--production` (testé). Avec la
production, la vérité le dit et le banc le rappelle.

RIEN DE SILENCIEUX. Un patch qui rend l'inaudible est rejeté, retiré et
compté dans la vérité ; un gain de crête commun est dit ; le coût de chaque
rendu est publié.
"""

from __future__ import annotations

import hashlib
import json
import math
import os
import subprocess
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple

import numpy as np

from .vsm_engine import Note, VsmEngine, VsmEngineError, identite_du_moteur
from .vsm_patch_optimizer import _vector_to_parameters, search_space_for_machine
from .vsm_reconstruct import _MACHINES_A_PROFIL, _NON_MELODIC

FORMAT = "vsm-morceau-synthetique"
VERSION = 1
SR = 44100

# Le silence du corpus (`vsm_corpus.RMS_MINIMAL`) juge un extrait d'une
# seconde ; ici c'est une note-sonde d'une seconde aussi, puis le stem entier
# de trente secondes, dont le RMS moyen est plus bas (les silences entre les
# notes comptent). Deux seuils, tous deux dits dans la vérité.
RMS_SONDE_MINIMAL = 1e-3
RMS_STEM_MINIMAL = 1e-4
TIRAGES_DE_PATCH = 8
CRETE_MAXIMALE = 0.95

ROLE_VOIX = "voix"
ROLES_MELODIQUES = ("basse", "accompagnement", "melodie", "nappe", ROLE_VOIX)
ROLE_DEUX_MAINS = "piano-deux-mains"
ROLE_BATTERIE = "batterie"
CAS = ("aucun", "memes-machine-disjoints", "chevauchement", "deux-mains")

# Registres plausibles par rôle (MIDI, bornes incluses).
REGISTRES: Dict[str, Tuple[int, int]] = {
    "basse": (28, 48),
    "accompagnement": (48, 72),
    "melodie": (60, 84),
    "nappe": (48, 76),
    # Un chant d'alto à soprano : deux octaves, ce qu'une voix couvre vraiment.
    ROLE_VOIX: (55, 76),
}
REGISTRES_DEUX_MAINS = ((36, 52), (60, 84))
# RMS cible par rôle (linéaire), tiré à ±3 dB autour.
NIVEAUX: Dict[str, float] = {
    "basse": 0.10, "batterie": 0.12, "accompagnement": 0.05, "melodie": 0.06,
    "nappe": 0.04, ROLE_DEUX_MAINS: 0.06, ROLE_VOIX: 0.08,
}
GATES: Dict[str, float] = {
    "basse": 0.85, "accompagnement": 0.8, "melodie": 0.85, "nappe": 0.98,
    ROLE_DEUX_MAINS: 0.9, ROLE_VOIX: 0.95,
}

# B5, EXIGENCE 1 — LA SECTION, unité d'entrée et de sortie des parties.
#
# Le banc mesurait trente secondes d'une texture stable ; la chaîne travaille
# sur des disques de quatre minutes où les parties ENTRENT et SORTENT. Huit
# mesures est la longueur de section la plus courante de la musique populaire
# (à 120 bpm, seize secondes), et c'est la plus petite qui laisse le temps
# d'entendre qu'une partie est partie.
MESURES_PAR_SECTION = 8
SECTIONS_MAXIMUM = 16

# B5, EXIGENCE 2 — LA NOTE BRÈVE, et pourquoi elle se mesure en SECONDES.
#
# Le critère du cahier des charges est « sous 120 ms », une durée absolue : une
# double croche dure 107 ms à 140 bpm et 179 ms à 84 bpm, si bien qu'un phrasé
# écrit en valeurs rythmiques ne tiendrait le critère qu'aux tempos rapides. La
# durée tirée est donc absolue, et la borne haute (110 ms) laisse dix
# millisecondes de marge sous le critère.
BREVE_DUREE_S = (0.055, 0.110)
BREVE_PROPORTION = 0.40   # des parties mélodiques éligibles, en plus de la garantie

# B5, EXIGENCE 3 — LES PROFILS D'ÉCHANTILLONS PAR RÔLE.
#
# `vsm.multisample` ne joue que ce qu'on lui installe : les noms ci-dessous sont
# des FRAGMENTS cherchés dans les profils réellement installés sur le poste
# (`vsm-render` les déclare), jamais une liste de fichiers supposés présents.
# Un rôle dont aucun profil n'est installé est DIT et retombe sur la synthèse —
# une partie échantillonnée qui manque ne doit pas disparaître en silence.
PROFILS_PAR_ROLE: Dict[str, Tuple[str, ...]] = {
    "basse": ("Acoustic-Bass", "Finger-Bass", "Fretless-Bass", "Pick-Bass", "Synth-Bass"),
    "accompagnement": ("Grand-Piano", "E-Piano", "Nylon-Guitar", "Clean-Guitar",
                       "Jazz-Guitar", "Harp", "Drawbar-Organ"),
    "melodie": ("Violin", "Trumpet", "Alto-Sax", "Tenor-Sax", "Flute", "Oboe",
                "Clarinet", "Saw-Lead", "Square-Lead"),
    "nappe": ("Slow-Strings", "Strings", "Warm-Pad", "Halo-Pad", "New-Age-Pad",
              "Sweep-Pad", "Synth-Strings"),
    ROLE_DEUX_MAINS: ("Grand-Piano", "E-Piano"),
    ROLE_VOIX: ("Choir-Aahs", "Voice-Oohs", "Concert-Choir", "Choir-Pad"),
}
# B5, EXIGENCE 4 (second volet) — CE QUE LA BORNE EN DEMI-TONS NE VOIT PAS.
#
# `borner_les_hauteurs` ne peut brider que les dimensions que le moteur déclare
# en `st` ou en `cents`. Le lot `s2` a montré qu'il en existe d'autres, et
# `tools/hauteur-des-patchs.py` les mesure. Deux familles, qui ne se soignent
# pas pareil :
#
#   * une MACHINE dont le patch d'usine sonne déjà ailleurs que la note jouée :
#     elle n'a rien à faire dans le vivier MÉLODIQUE du banc, comme les boîtes à
#     rythmes n'y sont pas. Elle reste au parc et au DAW — c'est le vivier du
#     BANC qu'on restreint, pas le logiciel ;
#   * un PARAMÈTRE qui déplace la hauteur sans l'annoncer. Sa fenêtre utile est
#     MESURÉE (l'intervalle, en 0-1, où l'écart reste sous deux demi-tons) et le
#     tirage y est ramené, exactement comme pour un désaccord déclaré.
#
# Les deux tables sont remplies PAR LA MESURE, jamais à la main : l'outil rend
# un code non nul dès qu'une machine du vivier en sort sans y être déclarée.
# MESURÉ le 19/09/2026 par `tools/hauteur-des-patchs.py` (note 60, patch
# d'usine, hauteur lue par autocorrélation, écart ramené dans l'octave) : ces
# machines-là ne sonnent PAS à la note qu'on leur joue, et aucun réglage n'y
# change rien puisque c'est déjà vrai au patch d'usine. Trois sont des
# résonateurs inharmoniques — une membrane de tambour, une plaque, une guimbarde
# — dont la hauteur perçue n'est pas le numéro de note ; la quatrième, un
# carillon, n'a pas de hauteur lisible du tout.
#
# ELLES RESTENT AU PARC ET DANS LE DAW. C'est le vivier MÉLODIQUE DU BANC qu'on
# restreint, et pour une raison qui ne vaut que là : la vérité du banc compare la
# hauteur ÉCRITE à la hauteur ENTENDUE, et une partie dont les deux diffèrent de
# quatre demi-tons fausse toute statistique qui la compte (D267). Deux parties de
# `vsm.membrane` ont contredit leur vérité dans le lot `s2` avant cette mesure.
MACHINES_SANS_HAUTEUR_JUSTE: Dict[str, str] = {
    "vsm.membrane": "−3,83 demi-tons au patch d'usine (membrane inharmonique)",
    "vsm.plate": "−4,29 demi-tons au patch d'usine (plaque inharmonique)",
    "vsm.jewsharp": "+3,91 demi-tons au patch d'usine (guimbarde inharmonique)",
    "vsm.carillon": "aucune hauteur lisible au patch d'usine (cloche)",
}

# Bornes de recherche de la hauteur sonnante, LARGES, et la butée est dite.
# Une première version cherchait entre 40 et 2 000 Hz : six machines rendaient
# exactement +35,25 demi-tons, c'est-à-dire 2 000 Hz — la mesure lisait sa
# propre borne et l'écrivait comme un résultat.
FMIN_HAUTEUR, FMAX_HAUTEUR = 25.0, 5000.0

MACHINE_ECHANTILLONS = "vsm.multisample"
# La voix se chante par la machine à formants OU par un chœur échantillonné :
# les deux existent au parc, et un corpus qui n'en éprouverait qu'une mesurerait
# la moitié de ce que la chaîne rencontre.
MACHINE_VOIX_SYNTHESE = "vsm.vocal"

MACHINES_BATTERIE = ("vsm.drums", "vsm.tr808", "vsm.tr909")
# Les voix que chaque boîte possède réellement (vsm_drumkit.MACHINE_VOICES,
# MODELLED_DRUM_NOTES) : une note absente ne déclenche rien, en silence.
PIECES_PAR_MACHINE: Dict[str, Dict[str, int]] = {
    "vsm.drums": {"kick": 36, "snare": 38, "hihat": 42, "openhat": 46, "tom": 45},
    "vsm.tr808": {"kick": 36, "snare": 38, "hihat": 42, "openhat": 46, "clap": 39},
    "vsm.tr909": {"kick": 36, "snare": 38, "hihat": 42, "openhat": 46, "clap": 39, "tom": 45},
}

GAMME_MAJEURE = (0, 2, 4, 5, 7, 9, 11)
GAMME_MINEURE = (0, 2, 3, 5, 7, 8, 10)
# Progressions en DEGRÉS de la gamme (0 = tonique), une par mesure.
PROGRESSIONS_MAJEURES = ((0, 4, 5, 3), (0, 3, 4, 3), (5, 3, 0, 4), (1, 4, 0, 0), (0, 5, 3, 4))
PROGRESSIONS_MINEURES = ((0, 5, 2, 6), (0, 3, 4, 0), (0, 6, 5, 6), (0, 5, 0, 6), (0, 3, 6, 4))


@dataclass
class Partie:
    role: str
    machine: str
    patch: Dict[str, float]
    vecteur: List[float]
    notes: List[List[float]]          # [note, vélocité, début (s), durée (s)]
    registre: List[List[int]]         # un ou deux [bas, haut]
    niveau_rms: float
    niveau_db: float
    gain: float
    pan: float
    gate: float
    pieces: List[str] = field(default_factory=list)
    cas: Optional[str] = None
    patchs_rejetes: int = 0
    origine_patch: str = "tiré dans le SearchProfile"
    empreinte: str = ""
    cout_rendu_s: float = 0.0
    fichier: str = ""
    # D277 : DE COMBIEN CETTE PARTIE SONNE-T-ELLE À CÔTÉ DE SES NOTES ÉCRITES ?
    #
    # Le patch est tiré au hasard dans l'espace déclaré par la machine, et
    # plusieurs machines y exposent un désaccord d'oscillateur EN DEMI-TONS :
    # `vsm.pcmhybrid` ±24, `vsm.obx` et `vsm.arpodyssey` ±12. Une partie ainsi
    # tirée SONNE ailleurs que ce que sa liste de notes annonce — et toute mesure
    # de hauteur qui compare l'une à l'autre compte ces notes fausses à tort.
    #
    # Mesuré le 13/09/2026 sur `s1-sec` : 9,9 % des notes mélodiques du corpus
    # sont dans ce cas, et `morceau-0001-g1` l'est ENTIÈREMENT — son F1 passe de
    # 0,027 à 0,567 selon la hauteur qu'on compare, de dernier des dix à premier.
    # Le chiffre se DÉDUISAIT alors du patch et des unités déclarées par le C++
    # (`tools/hauteur_sonnante.py`) ; il s'écrit désormais ici, à la source, où
    # l'unité est connue sans devinette.
    desaccords_demi_tons: Dict[str, float] = field(default_factory=dict)
    # B5, EXIGENCE 1 : les SECTIONS où cette partie sonne, ou None quand elle
    # sonne d'un bout à l'autre (le corpus d'avant : aucune entrée, aucune
    # sortie). Les notes de `notes` sont DÉJÀ filtrées ; la vérité reste donc
    # exacte, et ce champ dit ce que le filtre a gardé.
    sections: Optional[List[int]] = None
    # B5, EXIGENCE 2 : le phrasé bref, ou None. `{duree_s, notes, sous_120ms}`,
    # et `abandonnee` avec sa raison quand la machine ne rend rien d'audible sur
    # des notes de cette durée — un abandon muet ferait mentir le corpus sur ce
    # qu'il contient.
    phrase_breve: Optional[Dict[str, Any]] = None
    # B5, EXIGENCE 3 : le profil d'échantillons de CETTE partie, son nom, son
    # chemin et l'empreinte du fichier. Sans l'empreinte, deux postes aux
    # banques différentes rendraient deux corpus différents sous la même graine
    # sans que rien ne le dise.
    profil: str = ""
    profil_chemin: str = ""
    profil_empreinte: str = ""
    # B5, EXIGENCE 4 (second volet) : DE COMBIEN LA SONDE DE CETTE PARTIE
    # SONNAIT-ELLE À CÔTÉ DE SA NOTE ? Mesuré sur le rendu, pas déduit du patch
    # comme `desaccords_demi_tons` — ce qui couvre les chemins que le moteur ne
    # déclare pas (tension de corde, ratio d'opérateur FM, bourdon).
    # `null` quand la hauteur n'est pas lisible : jamais zéro par défaut.
    desaccord_mesure_demi_tons: Optional[float] = None
    note_sonde: int = 0


@dataclass
class Production:
    reverb_duree_s: float
    reverb_mix: float
    reverb_coupure_hz: float
    compresseur_seuil_db: float
    compresseur_ratio: float
    compresseur_attaque_s: float
    compresseur_relache_s: float
    compresseur_rattrapage: float


# ---------------------------------------------------------------------------
# Tirage de la structure : tempo, tonalité, progression, rôles, cas
# ---------------------------------------------------------------------------

def commit_du_depot() -> str:
    try:
        racine = Path(__file__).resolve().parents[2]
        court = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True,
                               text=True, cwd=racine, check=True).stdout.strip()
        sale = subprocess.run(["git", "status", "--porcelain"], capture_output=True, text=True,
                              cwd=racine, check=True).stdout.strip()
        return court + ("+" if sale else "")
    except Exception:
        return "inconnu"


def machines_melodiques_du_banc(engine: VsmEngine) -> List[str]:
    """Le parc de recherche, moins ce que le banc ne tire pas.

    `machines_de_recherche` = les machines pour lesquelles le moteur déclare
    un espace de recherche. On en retire les boîtes à rythmes et le sampler
    (rôles à part), la tonalité d'essai, et les machines à profil
    (`vsm.multisample`) : sans données installées elles sont refusées par le
    moteur, avec elles le morceau dépendrait d'un fichier hors dépôt.
    """
    from .vsm_corpus_build import machines_de_recherche

    return [m for m in machines_de_recherche(engine)
            if m not in _NON_MELODIC and m not in _MACHINES_A_PROFIL
            and m not in MACHINES_SANS_HAUTEUR_JUSTE]


def hauteur_sonnante(x: np.ndarray, sample_rate: int = SR) -> Optional[float]:
    """Hauteur MIDI de la partie tenue d'un son, par autocorrélation.

    Rend None quand aucune période ne ressort ET quand la période trouvée est en
    BUTÉE de la fenêtre de recherche : ce qui ne peut pas être vu n'est pas
    compté juste. C'est la même fonction pour le banc et pour
    `tools/hauteur-des-patchs.py` — deux mesures de hauteur qui ne diraient pas
    la même chose ne mesureraient rien.
    """
    y = np.asarray(x, dtype=np.float64)
    y = y[int(0.05 * sample_rate): int(0.65 * sample_rate)]
    if y.size < 1024 or float(np.abs(y).max()) < 1e-5:
        return None
    y = y - y.mean()
    n = 1 << int(np.ceil(np.log2(2 * y.size)))
    spectre = np.fft.rfft(y, n)
    ac = np.fft.irfft(spectre * np.conj(spectre), n)[: y.size]
    if ac[0] <= 0:
        return None
    ac /= ac[0]
    lo = int(sample_rate / FMAX_HAUTEUR)
    hi = min(int(sample_rate / FMIN_HAUTEUR), ac.size - 1)
    if hi <= lo:
        return None
    k = lo + int(np.argmax(ac[lo:hi]))
    if ac[k] < 0.25 or k <= lo + 1 or k >= hi - 1:
        return None
    return 69.0 + 12.0 * float(np.log2((sample_rate / k) / 440.0))


def hors_octave(demi_tons: float) -> float:
    """L'écart RAMENÉ DANS L'OCTAVE, dans [-6, +6].

    L'ambiguïté d'octave n'est pas un défaut du corpus, et le cahier des charges
    le dit depuis D267 : `tools/corpus-hauteurs.py` écarte explicitement ±12 et
    ±24 comme étant l'ambiguïté du transcripteur. La même convention vaut ici,
    sans quoi le banc rejetterait des patchs que la garde qui juge le corpus, elle,
    laisse passer.
    """
    return (float(demi_tons) + 6.0) % 12.0 - 6.0


def _hz(midi: float) -> float:
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def _dans_registre(midi: int, bas: int, haut: int) -> int:
    while midi < bas:
        midi += 12
    while midi > haut:
        midi -= 12
    return midi


class Structure:
    """Le squelette harmonique et rythmique d'un morceau, tiré d'une graine."""

    def __init__(self, rng: np.random.Generator, duree: float):
        self.tempo = int(rng.integers(84, 141))
        self.battement = 60.0 / self.tempo
        self.mesures = max(1, int(math.ceil(duree / (4 * self.battement))))
        self.duree = self.mesures * 4 * self.battement
        self.tonique = int(rng.integers(0, 12))
        self.mineur = bool(rng.random() < 0.5)
        self.gamme = GAMME_MINEURE if self.mineur else GAMME_MAJEURE
        progressions = PROGRESSIONS_MINEURES if self.mineur else PROGRESSIONS_MAJEURES
        self.progression = list(progressions[int(rng.integers(0, len(progressions)))])

    def degre(self, indice: int) -> int:
        """Classe de hauteur (0-11, relative à la tonique) du degré `indice`."""
        return self.gamme[indice % 7] + 12 * (indice // 7)

    def accord(self, mesure: int) -> Tuple[int, List[int]]:
        """(fondamentale MIDI en octave 3, intervalles de la triade sur ce degré)."""
        d = self.progression[mesure % len(self.progression)]
        fond = 48 + self.tonique + self.gamme[d]
        tierce = (self.degre(d + 2) - self.degre(d)) % 12
        quinte = (self.degre(d + 4) - self.degre(d)) % 12
        return fond, [0, tierce, quinte]

    def note_de_gamme(self, indice: int) -> int:
        """Hauteur MIDI absolue du degré `indice` (peut dépasser 7 : octaves)."""
        return 48 + self.tonique + self.degre(indice)


# ---------------------------------------------------------------------------
# Les notes, rôle par rôle : structurées, répétées, variées
# ---------------------------------------------------------------------------

def _velocite(rng: np.random.Generator, base: int, dispersion: float, fort: bool) -> int:
    v = base + (12 if fort else 0) + rng.normal(0.0, dispersion)
    return int(max(1, min(127, round(v))))


def notes_basse(rng: np.random.Generator, s: Structure, registre: Tuple[int, int], gate: float) -> List[List[float]]:
    motif = int(rng.integers(0, 3))  # 0 croches fond/quinte, 1 noires, 2 syncopé
    disp = float(rng.uniform(3.0, 10.0))
    notes: List[List[float]] = []
    for mesure in range(s.mesures):
        t0 = mesure * 4 * s.battement
        fond, iv = s.accord(mesure)
        if motif == 0:
            pas = s.battement / 2
            for i in range(8):
                midi = _dans_registre(fond + (iv[2] if i in (3, 7) else 0), *registre)
                notes.append([midi, _velocite(rng, 92, disp, i % 4 == 0), t0 + i * pas, pas * gate])
        elif motif == 1:
            for i in range(4):
                midi = _dans_registre(fond + (iv[2] if i == 2 else 0), *registre)
                notes.append([midi, _velocite(rng, 96, disp, i == 0), t0 + i * s.battement,
                              s.battement * gate])
        else:
            for debut, longueur, iv_i in ((0.0, 1.5, 0), (1.5, 1.0, 0), (2.5, 1.5, 2)):
                midi = _dans_registre(fond + iv[iv_i], *registre)
                notes.append([midi, _velocite(rng, 94, disp, debut == 0.0), t0 + debut * s.battement,
                              longueur * s.battement * gate])
    return notes


def notes_accompagnement(rng: np.random.Generator, s: Structure, registre: Tuple[int, int],
                         gate: float) -> List[List[float]]:
    arpege = bool(rng.random() < 0.6)
    disp = float(rng.uniform(3.0, 9.0))
    ordre = [0, 1, 2, 1] if rng.random() < 0.5 else [0, 2, 1, 2]
    notes: List[List[float]] = []
    for mesure in range(s.mesures):
        t0 = mesure * 4 * s.battement
        fond, iv = s.accord(mesure)
        if arpege:
            pas = s.battement / 2
            for i in range(8):
                midi = _dans_registre(fond + iv[ordre[i % 4]] + (12 if i >= 4 else 0), *registre)
                notes.append([midi, _velocite(rng, 84, disp, i % 4 == 0), t0 + i * pas, pas * gate])
        else:
            temps = (0.0, 2.0) if mesure % 2 == 0 else (1.0, 3.0)
            for t in temps:
                for k in iv:
                    midi = _dans_registre(fond + k, *registre)
                    notes.append([midi, _velocite(rng, 80, disp, t == 0.0), t0 + t * s.battement,
                                  s.battement * gate])
    return notes


def notes_melodie(rng: np.random.Generator, s: Structure, registre: Tuple[int, int],
                  gate: float) -> List[List[float]]:
    """Un motif de deux mesures, marche par degrés, répété avec variation."""
    disp = float(rng.uniform(4.0, 10.0))
    # rythme du motif : 8 temps répartis en noires et croches
    rythme: List[float] = []
    reste = 8.0
    while reste > 0:
        d = 0.5 if rng.random() < 0.45 else 1.0
        d = min(d, reste)
        rythme.append(d)
        reste -= d
    bas, haut = registre
    centre = (bas + haut) // 2
    # degré de départ : le plus proche du centre du registre
    indice = min(range(0, 22), key=lambda i: abs(s.note_de_gamme(i) - centre))
    degres: List[int] = []
    for _ in rythme:
        saut = int(rng.choice([-2, -1, -1, 1, 1, 2])) if rng.random() < 0.85 else int(rng.choice([-4, 3, 4]))
        indice = max(0, min(21, indice + saut))
        while s.note_de_gamme(indice) > haut and indice > 0:
            indice -= 1
        while s.note_de_gamme(indice) < bas and indice < 21:
            indice += 1
        degres.append(indice)
    notes: List[List[float]] = []
    for bloc in range(0, s.mesures, 2):
        t = bloc * 4 * s.battement
        variation = (bloc // 2) % 2 == 1
        for i, (d, longueur) in enumerate(zip(degres, rythme, strict=True)):
            indice = d
            if variation and i in (2, len(rythme) - 1):
                indice = max(0, min(21, d + int(rng.choice([-1, 1]))))
            midi = _dans_registre(s.note_de_gamme(indice), bas, haut)
            if t < s.duree:
                notes.append([midi, _velocite(rng, 96, disp, i == 0), t, longueur * s.battement * gate])
            t += longueur * s.battement
    return notes


def notes_nappe(rng: np.random.Generator, s: Structure, registre: Tuple[int, int],
                gate: float) -> List[List[float]]:
    disp = float(rng.uniform(2.0, 6.0))
    voix = 3 if rng.random() < 0.5 else 4
    tenue = 2 if rng.random() < 0.3 else 1  # mesures par accord tenu
    notes: List[List[float]] = []
    for mesure in range(0, s.mesures, tenue):
        t0 = mesure * 4 * s.battement
        fond, iv = s.accord(mesure)
        hauteurs = [fond + k for k in iv] + ([fond + 12] if voix == 4 else [])
        for midi in hauteurs:
            notes.append([_dans_registre(midi, *registre), _velocite(rng, 70, disp, False), t0,
                          tenue * 4 * s.battement * gate])
    return notes


def notes_voix(rng: np.random.Generator, s: Structure, registre: Tuple[int, int],
               gate: float) -> List[List[float]]:
    """Une ligne CHANTÉE : des phrases de deux mesures, séparées par un souffle.

    B5, exigence 3 du § 7 bis : « au moins un morceau sur quatre porte un rôle
    chanté ». Ce qui distingue cette ligne de `notes_melodie`, et qui compte
    pour une chaîne d'analyse : des valeurs LONGUES (un à deux temps), un
    ambitus resserré, et un SILENCE d'un temps à la fin de chaque phrase. Une
    voix reprend son souffle, et ce silence régulier la fait reconnaître autant
    que son timbre — une mélodie de synthétiseur, elle, enchaîne sans respirer.
    """
    disp = float(rng.uniform(3.0, 8.0))
    bas, haut = registre
    centre = (bas + haut) // 2
    indice = min(range(0, 22), key=lambda i: abs(s.note_de_gamme(i) - centre))
    notes: List[List[float]] = []
    for bloc in range(0, s.mesures, 2):
        depart = bloc * 4 * s.battement
        # la phrase s'arrête un temps avant la fin du bloc : c'est le souffle
        fin = min(s.duree, (bloc + 2) * 4 * s.battement) - s.battement
        t = depart
        premiere = True
        while t < fin - 1e-9:
            longueur = float(rng.choice([1.0, 1.0, 1.5, 2.0]))
            longueur = min(longueur, (fin - t) / s.battement)
            if longueur < 0.5:
                break
            saut = int(rng.choice([-2, -1, -1, 0, 1, 1, 2]))
            indice = max(0, min(21, indice + saut))
            while s.note_de_gamme(indice) > haut and indice > 0:
                indice -= 1
            while s.note_de_gamme(indice) < bas and indice < 21:
                indice += 1
            midi = _dans_registre(s.note_de_gamme(indice), bas, haut)
            notes.append([midi, _velocite(rng, 92, disp, premiere), t, longueur * s.battement * gate])
            t += longueur * s.battement
            premiere = False
    return notes


def raccourcir_phrase(rng: np.random.Generator, s: Structure, notes: List[List[float]],
                      duree_breve: float) -> List[List[float]]:
    """Le même phrasé, joué BREF : des frappes courtes sur la grille de double croche.

    B5, exigence 2 du § 7 bis. Le corpus d'avant ne portait AUCUNE note
    mélodique brève — ses 1 865 notes sous 150 ms étaient 1 865 frappes de
    batterie, et « la chaîne rate 96,7 % des notes courtes » a tenu trois phases
    sur cette seule population (D264-D265). Une note longue devient ici une à
    quatre frappes de `duree_breve` secondes, ce qui est un geste de musicien
    (une note réarticulée en doubles croches) et non un raccourcissement
    arbitraire : la hauteur et la place fortes du phrasé sont conservées.
    """
    pas = s.battement / 4
    sortie: List[List[float]] = []
    for midi, velocite, debut, duree in notes:
        frappes = max(1, min(4, int(duree / pas + 1e-6)))
        for i in range(frappes):
            t = debut + i * pas
            if t + duree_breve > s.duree:
                break
            baisse = 0 if i == 0 else int(rng.integers(4, 14))
            sortie.append([int(midi), int(max(1, min(127, int(velocite) - baisse))), t, duree_breve])
    return sortie


# ---------------------------------------------------------------------------
# B5, exigence 1 : l'arrangement — qui joue dans quelle section
# ---------------------------------------------------------------------------

def sections_du_morceau(s: Structure) -> List[Dict[str, float]]:
    """Le découpage en sections, en mesures ET en secondes.

    La longueur de section s'agrandit plutôt que de se multiplier au-delà de
    `SECTIONS_MAXIMUM` : un morceau de cinq minutes à 140 bpm ferait dix-huit
    sections de huit mesures, et dix-huit entrées et sorties ne sont plus un
    arrangement mais un clignotement.
    """
    par_section = max(MESURES_PAR_SECTION, int(math.ceil(s.mesures / SECTIONS_MAXIMUM)))
    sections: List[Dict[str, float]] = []
    for debut in range(0, s.mesures, par_section):
        fin = min(s.mesures, debut + par_section)
        sections.append({"mesure": debut, "mesures": fin - debut,
                         "debut": debut * 4 * s.battement, "fin": fin * 4 * s.battement})
    return sections


def _presence(rng: np.random.Generator, role: str, nb: int) -> List[int]:
    """Les sections où un rôle sonne : une plage continue, parfois trouée.

    Les archétypes viennent de ce qu'on entend sur un disque, et non d'un
    tirage uniforme : la basse et la batterie tiennent le morceau et entrent
    tôt ; la mélodie et le chant entrent après une introduction et se taisent
    parfois avant la fin ; tout le monde peut sauter une section (le « break »).
    """
    if nb <= 1:
        return [0]
    if role in ("basse", ROLE_BATTERIE):
        entree = 0 if rng.random() < 0.45 else 1
        sortie = nb - 1 if rng.random() < 0.85 else nb - 2
    elif role in ("melodie", ROLE_VOIX):
        entree = int(rng.integers(1, max(2, nb // 2 + 1)))
        sortie = nb - 1 if rng.random() < 0.5 else nb - 2
    else:
        entree = int(rng.integers(0, 2))
        sortie = nb - 1 if rng.random() < 0.7 else nb - 2
    sortie = max(entree, min(nb - 1, sortie))
    sections = list(range(entree, sortie + 1))
    if len(sections) >= 4 and rng.random() < 0.35:
        trou = int(rng.choice(sections[1:-1]))
        sections = [k for k in sections if k != trou]
    return sections


def _partielles(presences: List[set], nb: int) -> List[int]:
    return [i for i, pres in enumerate(presences) if len(pres) < nb]


def arranger(rng: np.random.Generator, s: Structure, parties: List[Partie],
             journal: Callable[[str], None]) -> Optional[Dict[str, Any]]:
    """Pose les entrées et les sorties, FILTRE les notes, et rend l'arrangement.

    Deux garanties, parce que le critère du cahier des charges les demande et
    qu'aucune des deux ne sort d'un tirage : **aucune section muette** (un
    morceau qui s'interrompt n'est pas un morceau) et **au moins deux parties
    qui ne sonnent pas d'un bout à l'autre**. Quand la seconde est impossible —
    deux parties pour deux sections —, elle est DITE et non tue.
    """
    sections = sections_du_morceau(s)
    nb = len(sections)
    if nb < 2 or not parties:
        journal(f"    arrangement impossible : {s.mesures} mesures font {nb} section(s)")
        return None
    presences: List[set] = []
    for partie in parties:
        pres = set(_presence(rng, partie.role, nb))
        presences.append(pres or set(range(nb)))
    for k in range(nb):
        if not any(k in pres for pres in presences):
            i = max(range(len(presences)), key=lambda j: (len(presences[j]), -j))
            presences[i].add(k)
    for _ in range(len(parties)):
        if len(_partielles(presences, nb)) >= 2:
            break
        pleines = [i for i in range(len(parties)) if len(presences[i]) == nb]
        pose = False
        for i in sorted(pleines, reverse=True):
            for k in (0, nb - 1):
                if k in presences[i] and sum(1 for pres in presences if k in pres) >= 2:
                    presences[i].discard(k)
                    pose = True
                    break
            if pose:
                break
        if not pose:
            break
    manquantes = 2 - len(_partielles(presences, nb))
    if manquantes > 0:
        journal(f"    arrangement : {manquantes} partie(s) de moins que les deux "
                f"demandées entrent ou sortent ({len(parties)} parties, {nb} sections)")
    for partie, pres in zip(parties, presences, strict=True):
        garde = sorted(pres)
        partie.sections = garde
        if len(garde) < nb:
            fenetres = [(sections[k]["debut"], sections[k]["fin"]) for k in garde]
            partie.notes = [n for n in partie.notes
                            if any(a - 1e-9 <= n[2] < b for a, b in fenetres)]
    journal(f"    arrangement : {nb} sections de {sections[0]['mesures']} mesures, "
            + ", ".join(f"{p.role}={len(p.sections or [])}/{nb}" for p in parties))
    return {"mesures_par_section": int(sections[0]["mesures"]), "sections": sections,
            "parties_partielles": len(_partielles(presences, nb))}


def _fenetres_de_partie(partie: Partie, arrangement: Optional[Dict[str, Any]]) -> List[Tuple[float, float]]:
    if arrangement is None or partie.sections is None:
        return []
    sections = arrangement["sections"]
    if len(partie.sections) >= len(sections):
        return []
    return [(float(sections[k]["debut"]), float(sections[k]["fin"])) for k in partie.sections]


def notes_deux_mains(rng: np.random.Generator, s: Structure, gate: float) -> List[List[float]]:
    """UNE partie, deux registres séparés par un vide : le cas chorale d'H25."""
    (gb, gh), (db, dh) = REGISTRES_DEUX_MAINS
    disp = float(rng.uniform(4.0, 9.0))
    notes: List[List[float]] = []
    for mesure in range(s.mesures):
        t0 = mesure * 4 * s.battement
        fond, iv = s.accord(mesure)
        # main gauche : fondamentale sur 1, quinte sur 3 (ou octave)
        for t, k in ((0.0, 0), (2.0, iv[2] if mesure % 2 == 0 else 12)):
            notes.append([_dans_registre(fond + k - 12, gb, gh), _velocite(rng, 88, disp, t == 0.0),
                          t0 + t * s.battement, 2 * s.battement * gate])
        # main droite : accord plaqué sur 1 et 3, arpège en croches sur 2 et 4
        for t in (0.0, 2.0):
            for k in iv:
                notes.append([_dans_registre(fond + k + 12, db, dh), _velocite(rng, 84, disp, t == 0.0),
                              t0 + t * s.battement, s.battement * gate])
        for i, t in enumerate((1.0, 1.5, 3.0, 3.5)):
            k = iv[(i + 1) % 3] + (12 if i % 2 else 0)
            notes.append([_dans_registre(fond + k + 12, db, dh), _velocite(rng, 78, disp, False),
                          t0 + t * s.battement, 0.5 * s.battement * gate])
    return notes


def notes_batterie(rng: np.random.Generator, s: Structure, machine: str) -> Tuple[List[List[float]], List[str]]:
    voix = PIECES_PAR_MACHINE[machine]
    pieces = ["kick", "snare", "hihat"]
    for extra in ("openhat", "clap", "tom"):
        if extra in voix and rng.random() < 0.5:
            pieces.append(extra)
    disp = float(rng.uniform(3.0, 8.0))
    doubles = bool(rng.random() < 0.4)      # charleston en doubles croches
    kick_et = bool(rng.random() < 0.5)      # kick sur le « et » de 4
    notes: List[List[float]] = []
    duree_frappe = 0.1

    def frappe(piece: str, t: float, base: int, fort: bool) -> None:
        notes.append([voix[piece], _velocite(rng, base, disp, fort), t, duree_frappe])

    for mesure in range(s.mesures):
        t0 = mesure * 4 * s.battement
        fin_de_bloc = mesure % 4 == 3
        for temps in (0, 2):
            frappe("kick", t0 + temps * s.battement, 110, temps == 0)
        if kick_et and mesure % 2 == 1:
            frappe("kick", t0 + 3.5 * s.battement, 96, False)
        for temps in (1, 3):
            frappe("snare", t0 + temps * s.battement, 104, False)
            if "clap" in pieces:
                frappe("clap", t0 + temps * s.battement, 90, False)
        pas = s.battement / (4 if doubles else 2)
        for i in range(int(4 * s.battement / pas + 0.5)):
            t = t0 + i * pas
            if "openhat" in pieces and (i * pas) % (2 * s.battement) > 2 * s.battement - pas - 1e-9:
                frappe("openhat", t, 90, False)
            else:
                frappe("hihat", t, 84, i % 2 == 0)
        if fin_de_bloc and "tom" in pieces:
            for i in range(4):
                frappe("tom", t0 + (3.0 + i * 0.25) * s.battement, 100, i == 0)
        elif fin_de_bloc:
            for i in range(2):
                frappe("snare", t0 + (3.5 + i * 0.25) * s.battement, 96, False)
    return notes, pieces


# ---------------------------------------------------------------------------
# Le tirage d'un morceau
# ---------------------------------------------------------------------------

def _tirer_roles(rng: np.random.Generator, nombre: int, cas: str,
                 voix: bool = False, echantillons: bool = False) -> List[Dict[str, Any]]:
    """La liste des rôles, avec le cas de parité posé sur une ou deux parties.

    `voix` et `echantillons` viennent de la GRAINE et non d'un tirage — c'est la
    règle déjà suivie par le cas de parité, et pour la même raison : un lot de
    dix graines doit voir chaque chose assez souvent pour qu'un attendu se
    mesure, et le premier lot tiré au hasard avait donné 0 « aucun » sur dix.
    Le cahier des charges demande un morceau sur quatre chanté et un sur trois
    échantillonné ; la graine en donne un sur deux et deux sur trois.
    """
    roles: List[Dict[str, Any]] = []
    if cas == "deux-mains":
        roles.append({"role": ROLE_DEUX_MAINS, "registre": [list(r) for r in REGISTRES_DEUX_MAINS], "cas": cas})
    elif cas == "memes-machine-disjoints":
        roles.append({"role": "accompagnement", "registre": [[48, 58]], "cas": cas, "paire": True})
        roles.append({"role": "melodie", "registre": [[68, 84]], "cas": cas, "paire": True})
    elif cas == "chevauchement":
        roles.append({"role": "accompagnement", "registre": [[52, 72]], "cas": cas})
        roles.append({"role": "melodie", "registre": [[60, 80]], "cas": cas})
    if len(roles) < nombre and rng.random() < 0.9:
        roles.insert(0, {"role": "basse", "registre": [list(REGISTRES["basse"])], "cas": None})
    if len(roles) < nombre and rng.random() < 0.85:
        roles.append({"role": ROLE_BATTERIE, "registre": [], "cas": None})
    if voix and len(roles) < nombre:
        roles.append({"role": ROLE_VOIX, "registre": [list(REGISTRES[ROLE_VOIX])], "cas": None})
    while len(roles) < nombre:
        role = str(rng.choice(["accompagnement", "melodie", "nappe"]))
        roles.append({"role": role, "registre": [list(REGISTRES[role])], "cas": None})
    roles = roles[:max(nombre, len(roles))]
    if voix and not any(r["role"] == ROLE_VOIX for r in roles):
        # Le nombre de parties ne laissait pas la place : le chant PREND celle
        # d'une partie mélodique plutôt que d'être abandonné en silence.
        for r in roles:
            if r["role"] in ("melodie", "accompagnement", "nappe"):
                r["role"], r["registre"] = ROLE_VOIX, [list(REGISTRES[ROLE_VOIX])]
                break
    if echantillons:
        candidats = [r for r in roles if r["role"] in PROFILS_PAR_ROLE and not r.get("paire")]
        if candidats:
            candidats[int(rng.integers(0, len(candidats)))]["echantillons"] = True
    return roles


def _panoramique(rng: np.random.Generator, role: str) -> float:
    if role == "basse":
        return 0.0
    if role == ROLE_BATTERIE:
        return float(rng.uniform(-0.1, 0.1))
    return float(rng.uniform(-0.7, 0.7))


def _panner(mono: np.ndarray, pan: float) -> np.ndarray:
    theta = (pan + 1.0) * math.pi / 4.0
    return np.stack([mono * math.cos(theta), mono * math.sin(theta)], axis=1).astype(np.float32)


def _rms_fenetres(x: np.ndarray, fenetres: Sequence[Tuple[float, float]]) -> float:
    """Le RMS des seuls passages où la partie SONNE.

    Sans cela, une partie qui ne joue que la moitié du morceau verrait son RMS
    divisé par deux par ses propres silences, et le calage la rendrait deux fois
    trop forte quand elle joue : l'arrangement changerait le MIXAGE, qui n'est
    pas ce qu'il est censé changer.
    """
    parts = []
    for debut, fin in fenetres:
        a, b = int(round(debut * SR)), int(round(fin * SR))
        if b > a:
            parts.append(x[max(0, a):min(x.shape[0], b)])
    utile = np.concatenate(parts) if parts else x
    return _rms(utile) if utile.size else 0.0


def _rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.asarray(x, dtype=np.float64) ** 2))) if x.size else 0.0


class MachineMuette(VsmEngineError):
    """La machine ne rend rien d'audible sur la note du rôle : elle est écartée, et dite."""


class Generateur:
    """Fabrique un morceau à partir d'une graine, avec un moteur déjà ouvert.

    `rendre` peut être remplacé (tests) : il reçoit (machine, patch, notes,
    durée) et rend un mono float32, comme `VsmEngine.render`.
    """

    def __init__(self, engine: VsmEngine, machines: Optional[Sequence[str]] = None,
                 borne_hauteur: float = 0.0,
                 arrangement: bool = False, notes_breves: bool = False, echantillons: bool = False,
                 rendre: Optional[Callable[..., np.ndarray]] = None,
                 journal: Optional[Callable[[str], None]] = None):
        self.engine = engine
        # B5 / § 7 bis : LES TROIS EXIGENCES DU CORPUS SUIVANT, chacune sous son
        # option, et TOUTES À FAUX PAR DÉFAUT. Le défaut est le corpus d'avant
        # au bit près, et ce n'est pas une prudence de style : `s1-sec` est
        # l'étalon de tous les chiffres publiés — S1, `r1`, `r1-prod`, `r1f`,
        # `r1f-13sep` — et un générateur qui changerait en douce les rendrait
        # tous incomparables sans que personne s'en aperçoive.
        self.arrangement = bool(arrangement)     # exigence 1 : entrées et sorties
        self.notes_breves = bool(notes_breves)   # exigence 2 : des notes sous 120 ms
        self.echantillons = bool(echantillons)   # exigence 3 : échantillons et chant
        # D277 / B5 : DE COMBIEN UN PATCH TIRÉ PEUT-IL DÉSACCORDER LA HAUTEUR ?
        #
        # 0 (le défaut) : sans borne, c'est-à-dire le corpus d'avant, au bit près.
        # Une valeur en demi-tons borne les paramètres de hauteur du patch, ce que
        # le § 7 bis du cahier des charges demande pour le corpus SUIVANT : sans
        # borne, une partie peut sonner huit demi-tons à côté de ses notes, et
        # `morceau-0001-g1` l'est ENTIÈREMENT — son F1 vaut 0,027 ou 0,567 selon
        # la hauteur qu'on compare, dernier des dix ou premier.
        #
        # BORNER CHANGE CE QUE LE CORPUS ÉPROUVE, et c'est assumé : un musicien
        # désaccorde ses oscillateurs, parfois beaucoup. Mais le corpus sert
        # d'ÉTALON, pas d'épreuve — sa vérité doit être fiable avant d'être
        # difficile, et un morceau dont le F1 dépend de la convention de lecture
        # ne mesure rien, si réaliste soit-il.
        self.borne_hauteur = float(borne_hauteur)
        self.machines = list(machines) if machines else machines_melodiques_du_banc(engine)
        if not self.machines:
            raise VsmEngineError("aucune machine mélodique cherchable : le moteur est-il vivant ?")
        self.rendre = rendre or (lambda machine, patch, notes, duree, profil=None: engine.render(
            machine, patch, notes, duration=duree, sample_rate=SR, profile=profil))
        self.journal = journal or (lambda ligne: None)
        self._espaces: Dict[str, list] = {}
        self._profils: Optional[List[Dict[str, Any]]] = None
        self._profils_refuses: Dict[str, str] = {}
        self._derive_sonde: Optional[float] = None

    def _rendre(self, machine: str, patch: Dict[str, float], notes: Sequence[Note],
                duree: float, profil: str = "") -> np.ndarray:
        """Le rendu, avec son profil quand il y en a un.

        Le profil n'est passé QUE s'il existe : les doublures de rendu des tests
        prennent quatre arguments, et leur en imposer un cinquième ferait
        échouer des tests qui n'ont rien à voir avec les échantillons.
        """
        if profil:
            return self.rendre(machine, patch, notes, duree, profil=profil)
        return self.rendre(machine, patch, notes, duree)

    def profils_installes(self) -> List[Dict[str, Any]]:
        if self._profils is None:
            try:
                self._profils = [p for p in self.engine.profiles() if not p.get("error")]
            except Exception as erreur:          # moteur sans consultation de profils
                self.journal(f"    profils illisibles ({erreur})")
                self._profils = []
        return self._profils

    def profil_pour(self, rng: np.random.Generator, role: str) -> Dict[str, str]:
        """Un profil d'échantillons installé qui convienne au rôle, ou rien.

        Rend `{}` quand aucun profil du poste ne correspond — l'appelant le DIT
        et retombe sur la synthèse. Le corpus dépend ainsi d'une banque installée
        hors du dépôt, ce qui est assumé et écrit au § 7 bis : la vérité porte le
        nom, le chemin et l'EMPREINTE du profil, sans quoi deux postes aux
        banques différentes rendraient deux corpus différents sous la même
        graine sans que rien ne le dise.
        """
        fragments = PROFILS_PAR_ROLE.get(role, ())
        candidats = [p for p in self.profils_installes()
                     if any(f in str(p.get("name", "")) for f in fragments)]
        if not candidats:
            return {}
        # L'ORDRE EST TIRÉ, PUIS CHAQUE CANDIDAT EST ÉPROUVÉ. Un profil installé
        # n'est pas un profil jouable : `vsm.multisample` refuse tout profil
        # au-delà de 256 Mo, et les banques générales en comptent (288 zones
        # pour un saxophone, 324 pour un violoncelle). Sans cette épreuve, le
        # tirage rendait un profil que le moteur refusait huit fois de suite,
        # et la partie était perdue.
        ordre = list(rng.permutation(len(candidats)))
        refuses: List[str] = []
        for i in ordre:
            choisi = candidats[int(i)]
            chemin = str(choisi.get("path", ""))
            refus = self.profil_refuse(chemin)
            if refus:
                refuses.append(f"{choisi.get('name')} ({refus})")
                continue
            empreinte = ""
            try:
                empreinte = hashlib.sha256(Path(chemin).read_bytes()).hexdigest()
            except OSError as erreur:
                self.journal(f"    profil {choisi.get('name')} : empreinte illisible ({erreur})")
            if refuses:
                self.journal(f"    {role} : {len(refuses)} profil(s) refusé(s) par le moteur — "
                             + "; ".join(refuses[:2]) + ("…" if len(refuses) > 2 else ""))
            return {"nom": str(choisi.get("name", "")), "chemin": chemin, "empreinte": empreinte}
        self.journal(f"    {role} : les {len(candidats)} profils qui conviennent sont refusés par "
                     f"le moteur — " + "; ".join(refuses[:2]) + ("…" if len(refuses) > 2 else ""))
        return {}

    def profil_refuse(self, chemin: str) -> str:
        """La raison pour laquelle le moteur refuse ce profil, ou une chaîne vide.

        Le verdict est MÉMORISÉ : un profil de trois cents zones est éprouvé une
        fois par lot, pas une fois par partie. Le refus vient du moteur lui-même
        (budget mémoire, fichier d'échantillon manquant), jamais d'une règle
        écrite ici qui dériverait de ce que la machine accepte vraiment.
        """
        if chemin in self._profils_refuses:
            return self._profils_refuses[chemin]
        raison = ""
        try:
            sonde = self._rendre(MACHINE_ECHANTILLONS, {}, [Note(60, 100, 0.0, 0.5)], 0.75, chemin)
            if not (sonde.size and np.isfinite(sonde).all() and _rms(sonde) >= RMS_SONDE_MINIMAL):
                raison = "muet sur la note 60"
        except VsmEngineError as erreur:
            raison = str(erreur).replace("profil refusé : ", "")
        self._profils_refuses[chemin] = raison
        return raison

    def espace(self, machine: str):
        if machine not in self._espaces:
            self._espaces[machine] = search_space_for_machine(machine, self.engine, max_dimensions=10 ** 6)
        return self._espaces[machine]

    def borner_les_hauteurs(self, machine: str, vecteur: np.ndarray) -> np.ndarray:
        """Ramène les composantes de HAUTEUR du vecteur dans la borne demandée.

        ON BORNE LE VECTEUR, PAS LE PATCH, et c'est ce qui fait tenir l'ensemble :
        `verite.json` garde le vecteur tiré, et le patch s'en déduit. Écrêter le
        patch après coup les ferait mentir l'un sur l'autre, et un corpus
        reproductible ne l'est plus si son vecteur ne rend pas son patch.

        Les dimensions LOGARITHMIQUES sont laissées telles quelles : aucun
        paramètre de hauteur du parc n'en est (un désaccord se lit en demi-tons
        ou en cents, jamais en décades), et remapper une échelle log sans cas
        d'essai serait deviner.
        """
        if self.borne_hauteur <= 0.0:
            return vecteur
        borne = self.borne_hauteur
        sortie = np.array(vecteur, dtype=float, copy=True)
        for i, dimension in enumerate(self.espace(machine)):
            if dimension.unit not in ("st", "cents") or dimension.logarithmic:
                continue
            en_st = 1.0 if dimension.unit == "st" else 0.01
            bas, haut = dimension.low * en_st, dimension.high * en_st
            if haut <= bas:
                continue
            # La fenêtre de tirage qui respecte la borne, exprimée en 0-1.
            t_bas = max(0.0, (-borne - bas) / (haut - bas))
            t_haut = min(1.0, (borne - bas) / (haut - bas))
            if t_haut <= t_bas:
                # La dimension entière est hors borne (elle ne peut pas être
                # accordée) : on prend le point le plus proche de zéro.
                sortie[i] = 0.0 if abs(bas) < abs(haut) else 1.0
                continue
            sortie[i] = t_bas + float(sortie[i]) * (t_haut - t_bas)
        return sortie

    def desaccords_de_hauteur(self, machine: str, patch: Dict[str, float]) -> Dict[str, float]:
        """Les paramètres du patch qui DÉPLACENT la hauteur, en demi-tons.

        L'unité vient de la dimension de recherche, que le moteur déclare : « st »
        compte tel quel, « cents » se divise par cent, et tout le reste ne déplace
        rien — `voice.unisonDetune` ou `oscillator.supersaw.detune` sont des
        réglages normalisés de 0 à 1, qu'on a d'abord pris pour des demi-tons et
        qui ont fait publier « 12,7 % des notes » là où il faut lire 9,9 %.
        """
        trouves: Dict[str, float] = {}
        for dimension in self.espace(machine):
            clef = dimension.semantic_id
            if clef not in patch:
                continue
            valeur = float(patch[clef])
            if dimension.unit == "st":
                demi = valeur
            elif dimension.unit == "cents":
                demi = valeur / 100.0
            else:
                continue
            if abs(demi) > 0.25:
                trouves[clef] = demi
        return trouves

    def tirer_patch(self, rng: np.random.Generator, machine: str, note_sonde: int,
                    profil: str = "") -> Tuple[Dict[str, float], List[float], int, str]:
        """Un patch audible, ou le patch d'usine après TIRAGES_DE_PATCH rejets."""
        espace = self.espace(machine)
        rejets = 0
        for _ in range(TIRAGES_DE_PATCH):
            vecteur = self.borner_les_hauteurs(machine, rng.random(len(espace)))
            patch = _vector_to_parameters(espace, vecteur)
            try:
                sonde = self._rendre(machine, patch, [Note(note_sonde, 100, 0.0, 0.75)], 1.0, profil)
            except VsmEngineError as erreur:
                self.journal(f"    {machine} : rendu refusé ({erreur}) — patch retiré")
                rejets += 1
                continue
            if sonde.size and np.isfinite(sonde).all() and _rms(sonde) >= RMS_SONDE_MINIMAL:
                # LA SONDE EST DÉJÀ RENDUE : on y LIT la hauteur, au lieu de la
                # déduire du patch. C'est le second volet de l'exigence 4, et il
                # ne coûte rien. `borner_les_hauteurs` ne bride que ce que le
                # moteur DÉCLARE en demi-tons ou en cents ; le lot `s2` a montré
                # qu'il existe d'autres chemins — `scanned.tension` est la tension
                # d'une corde, les ratios d'opérateur d'un DX7 transposent, un
                # cornemuse porte un bourdon à hauteur fixe. Aucun ne s'annonce.
                # Mesurée sur la sonde, la dérive se voit quelle qu'en soit la
                # cause, et le patch fautif est RETIRÉ comme un patch muet l'est.
                self._derive_sonde = self.derive_de_hauteur(sonde, note_sonde)
                if (self.borne_hauteur > 0.0 and self._derive_sonde is not None
                        and abs(self._derive_sonde) > self.borne_hauteur):
                    self.journal(f"    {machine} : sonne à {self._derive_sonde:+.2f} demi-ton de la "
                                 f"note {note_sonde} — patch retiré (borne {self.borne_hauteur:g})")
                    rejets += 1
                    continue
                return patch, [float(v) for v in vecteur], rejets, "tiré dans le SearchProfile"
            rejets += 1
        # Huit patchs muets de suite : c'est peut-être la MACHINE qui ne
        # sonne pas sur cette note (une boîte FM ne répond qu'à ses voix). On
        # éprouve le patch d'usine ; s'il est muet aussi, la machine est
        # écartée POUR CE RÔLE, en le disant, et l'appelant en tire une autre.
        try:
            sonde = self._rendre(machine, {}, [Note(note_sonde, 100, 0.0, 0.75)], 1.0, profil)
        except VsmEngineError as erreur:
            raise MachineMuette(f"{machine} : rendu refusé au patch d'usine ({erreur})") from erreur
        if not (sonde.size and np.isfinite(sonde).all() and _rms(sonde) >= RMS_SONDE_MINIMAL):
            raise MachineMuette(f"{machine} : muette sur la note {note_sonde}, même au patch d'usine")
        self._derive_sonde = self.derive_de_hauteur(sonde, note_sonde)
        if (self.borne_hauteur > 0.0 and self._derive_sonde is not None
                and abs(self._derive_sonde) > self.borne_hauteur):
            # Le patch d'usine lui-même sonne ailleurs : la machine n'est pas
            # mélodique au sens du banc, et l'on en tire une autre en le disant.
            # `MACHINES_SANS_HAUTEUR_JUSTE` évite d'en arriver là pour celles que
            # la mesure connaît ; ce chemin attrape celles qui viendront.
            raise MachineMuette(f"{machine} : sonne à {self._derive_sonde:+.2f} demi-ton de la note "
                                f"{note_sonde} même au patch d'usine")
        self.journal(f"    {machine} : {rejets} patchs inaudibles de suite — patch d'usine")
        return {}, [], rejets, f"patch d'usine après {rejets} rejets"

    def derive_de_hauteur(self, sonde: np.ndarray, note: int) -> Optional[float]:
        """De combien la sonde sonne-t-elle à côté de la note jouée, dans l'octave.

        None quand la hauteur n'est pas lisible : ce qui ne peut pas être vu
        n'est pas compté nul, et la vérité l'écrira `null` plutôt que zéro.
        """
        entendue = hauteur_sonnante(sonde)
        return None if entendue is None else hors_octave(entendue - float(note))

    def fabriquer(self, graine: int, duree: float = 30.0, production: bool = False,
                  cas: Optional[str] = None, nombre_de_parties: Optional[int] = None) -> Tuple[dict, List[np.ndarray], np.ndarray]:
        """Rend (vérité, stems stéréo float32 dans l'ordre, mélange stéréo float32)."""
        depart_total = time.perf_counter()
        rng = np.random.default_rng(int(graine))
        s = Structure(rng, duree)
        nombre = int(nombre_de_parties) if nombre_de_parties else int(rng.integers(2, 13))
        # Le cas TOURNE avec la graine plutôt que d'être tiré : un lot de dix
        # graines consécutives voit chaque cas deux ou trois fois. Le premier
        # lot tiré au hasard (04/09/2026) avait donné 0 « aucun », 1
        # « mêmes machine », 4 « deux mains » et 5 « chevauchement » — un
        # attendu par cas ne se mesure pas sur une occurrence.
        cas_choisi = cas if cas else CAS[int(graine) % len(CAS)]
        if cas_choisi not in CAS:
            raise ValueError(f"cas inconnu : {cas_choisi} (attendu {', '.join(CAS)})")
        # B5, exigence 3 : LE CHANT UN MORCEAU SUR DEUX, LES ÉCHANTILLONS DEUX
        # SUR TROIS — par la graine, comme le cas de parité, et pour la même
        # raison (un tirage ne garantit rien sur dix morceaux). Le cahier des
        # charges demande un sur quatre et un sur trois : la marge est prise
        # exprès, une partie échantillonnée pouvant être refusée faute de profil
        # installé, et un critère se tient avec ce qui SURVIT au tirage.
        veut_voix = self.echantillons and int(graine) % 2 == 0
        veut_echantillons = self.echantillons and int(graine) % 3 != 2
        roles = _tirer_roles(rng, nombre, cas_choisi, voix=veut_voix, echantillons=veut_echantillons)
        self.journal(f"  graine {graine} : {s.tempo} bpm, {'mineur' if s.mineur else 'majeur'} sur "
                     f"{s.tonique}, {s.mesures} mesures ({s.duree:.1f} s), {len(roles)} parties, cas {cas_choisi}")

        parties: List[Partie] = []
        machines_ecartees: List[Dict[str, str]] = []
        machine_de_paire: Optional[str] = None
        for description in roles:
            role = str(description["role"])
            registres = [list(map(int, r)) for r in description["registre"]]
            if role == ROLE_BATTERIE:
                machine = str(rng.choice(list(MACHINES_BATTERIE)))
                notes, pieces = notes_batterie(rng, s, machine)
                note_sonde = 36
                gate = 1.0
            else:
                pieces = []
                gate = float(np.clip(GATES[role] + rng.uniform(-0.1, 0.05), 0.3, 1.0))
                if role == ROLE_DEUX_MAINS:
                    notes = notes_deux_mains(rng, s, gate)
                    note_sonde = 64
                else:
                    r = (registres[0][0], registres[0][1])
                    fabrique = {"basse": notes_basse, "accompagnement": notes_accompagnement,
                                "melodie": notes_melodie, "nappe": notes_nappe,
                                ROLE_VOIX: notes_voix}[role]
                    notes = fabrique(rng, s, r, gate)
                    note_sonde = (r[0] + r[1]) // 2
            # B5, exigence 3 : LA MACHINE À ÉCHANTILLONS, quand le rôle la
            # demande et qu'un profil du poste lui convient. Sinon, c'est DIT et
            # le rôle retombe sur la synthèse : une partie échantillonnée
            # absente en silence ferait croire le corpus conforme.
            profil: Dict[str, str] = {}
            if role != ROLE_BATTERIE and (description.get("echantillons") or role == ROLE_VOIX):
                # Le chant se partage entre la machine à formants et un chœur
                # échantillonné ; une partie d'échantillons explicite, elle, ne
                # se joue que par la machine à échantillons.
                par_synthese = role == ROLE_VOIX and not description.get("echantillons") and rng.random() < 0.5
                if not par_synthese:
                    profil = self.profil_pour(rng, role)
                    if not profil:
                        raison = (f"aucun profil installé parmi "
                                  f"{', '.join(PROFILS_PAR_ROLE.get(role, ())) or 'aucun nom'}")
                        self.journal(f"    {role} : {MACHINE_ECHANTILLONS} écartée — {raison}")
                        machines_ecartees.append({"role": role, "machine": MACHINE_ECHANTILLONS,
                                                  "raison": raison})
            # La machine, puis son patch ; une machine muette sur la note du
            # rôle est écartée et dite, et une autre est tirée.
            for _ in range(len(self.machines) + 1):
                if role == ROLE_BATTERIE:
                    pass
                elif profil:
                    machine = MACHINE_ECHANTILLONS
                elif role == ROLE_VOIX:
                    machine = MACHINE_VOIX_SYNTHESE
                elif description.get("paire") and machine_de_paire:
                    machine = machine_de_paire
                else:
                    machine = str(rng.choice(self.machines))
                try:
                    patch, vecteur, rejets, origine = self.tirer_patch(
                        rng, machine, note_sonde, profil.get("chemin", ""))
                    break
                except MachineMuette as erreur:
                    self.journal(f"    écartée : {erreur}")
                    machines_ecartees.append({"role": role, "machine": machine, "raison": str(erreur)})
                    if role == ROLE_BATTERIE:
                        raise
                    # Le profil vient d'être refusé : le retirer, sans quoi le
                    # tour suivant redemanderait LE MÊME et la boucle
                    # s'épuiserait sur une machine déjà connue pour muette.
                    profil = {}
            else:
                raise VsmEngineError(f"aucune machine audible pour le rôle {role}")
            if description.get("paire"):
                machine_de_paire = machine
            niveau_db = float(20 * math.log10(NIVEAUX[role]) + rng.uniform(-3.0, 3.0))
            parties.append(Partie(role=role, machine=machine, patch=patch, vecteur=vecteur, notes=notes,
                                  registre=registres, niveau_rms=10 ** (niveau_db / 20), niveau_db=niveau_db,
                                  gain=1.0, pan=_panoramique(rng, role), gate=gate, pieces=pieces,
                                  cas=description.get("cas"), patchs_rejetes=rejets, origine_patch=origine,
                                  desaccords_demi_tons=self.desaccords_de_hauteur(machine, patch),
                                  profil=profil.get("nom", ""), profil_chemin=profil.get("chemin", ""),
                                  profil_empreinte=profil.get("empreinte", ""),
                                  desaccord_mesure_demi_tons=self._derive_sonde,
                                  note_sonde=int(note_sonde)))

        # B5, exigence 2 : LE PHRASÉ BREF, posé après le tirage des parties pour
        # que le flux de tirages du défaut reste intact quand l'option est à faux.
        notes_avant_bref: Dict[int, List[List[float]]] = {}
        if self.notes_breves:
            notes_avant_bref = self._phraser_bref(rng, s, parties)

        # B5, exigence 1 : LES ENTRÉES ET LES SORTIES, après le phrasé bref —
        # une frappe ajoutée par le phrasé doit être filtrée comme les autres.
        arrangement = arranger(rng, s, parties, self.journal) if self.arrangement else None
        for partie in parties:
            if partie.phrase_breve is not None:
                partie.phrase_breve["notes"] = len(partie.notes)
                partie.phrase_breve["sous_120ms"] = sum(1 for note in partie.notes if note[3] < 0.120)

        # Les tirages de PRODUCTION se font avant les rendus : un patch rejeté
        # de plus ou de moins ne doit pas déplacer la réverbération.
        prod: Optional[Production] = None
        if production:
            prod = Production(
                reverb_duree_s=float(rng.uniform(0.6, 1.2)), reverb_mix=float(rng.uniform(0.12, 0.25)),
                reverb_coupure_hz=float(rng.uniform(3000.0, 7000.0)),
                compresseur_seuil_db=float(rng.uniform(-18.0, -12.0)), compresseur_ratio=float(rng.uniform(2.0, 3.0)),
                compresseur_attaque_s=0.01, compresseur_relache_s=0.1, compresseur_rattrapage=1.0)
            graine_reverb = int(rng.integers(0, 2 ** 31 - 1))

        # Rendu, une passe par partie ; le stem est calé à son RMS tiré.
        duree_rendu = s.duree + 1.0
        n = int(round(duree_rendu * SR))
        stems: List[np.ndarray] = []
        for indice, partie in enumerate(parties):
            depart = time.perf_counter()
            try:
                audio = self._rendre_partie(partie, duree_rendu, rng)
            except VsmEngineError as erreur:
                if indice not in notes_avant_bref:
                    raise
                # La machine ne rend rien d'audible sur des notes de cette
                # DURÉE — une enveloppe qui ne s'ouvre pas en soixante
                # millisecondes. On revient au phrasé long, et la vérité le DIT :
                # un corpus qui annoncerait des notes brèves qu'il ne porte pas
                # ferait mentir toute mesure bâtie dessus.
                brève = dict(partie.phrase_breve or {})
                self.journal(f"    {partie.machine} : phrasé bref abandonné ({erreur})")
                partie.notes = notes_avant_bref[indice]
                fenetres_bref = _fenetres_de_partie(partie, arrangement)
                if fenetres_bref:
                    partie.notes = [note for note in partie.notes
                                    if any(a - 1e-9 <= note[2] < b for a, b in fenetres_bref)]
                partie.phrase_breve = {**brève, "abandonnee": True, "raison": str(erreur),
                                       "notes": len(partie.notes), "sous_120ms": 0}
                audio = self._rendre_partie(partie, duree_rendu, rng)
            mono = np.zeros(n, dtype=np.float32)
            mono[:min(n, audio.size)] = audio[:n]
            fenetres = _fenetres_de_partie(partie, arrangement)
            rms = _rms_fenetres(mono, fenetres) if fenetres else _rms(mono)
            partie.gain = float(partie.niveau_rms / rms) if rms > 0 else 0.0
            stems.append(_panner(mono * np.float32(partie.gain), partie.pan))
            partie.cout_rendu_s = time.perf_counter() - depart
            partie.fichier = f"stems-vrais/{indice + 1:02d}-{partie.role}.wav"
            self.journal(f"    {indice + 1:2d}. {partie.role:16s} {partie.machine:18s} {len(partie.notes):4d} notes"
                         f"  {partie.niveau_db:6.1f} dB  pan {partie.pan:+.2f}  {partie.cout_rendu_s:.2f} s"
                         + (f"  ({partie.origine_patch})" if partie.patchs_rejetes else ""))

        # Crête : un gain COMMUN, appliqué aux stems avant l'écriture, et dit.
        somme = np.zeros((n, 2), dtype=np.float64)
        for stem in stems:
            somme += stem
        crete = float(np.abs(somme).max()) if somme.size else 0.0
        gain_crete = CRETE_MAXIMALE / crete if crete > CRETE_MAXIMALE else 1.0
        if gain_crete != 1.0:
            stems = [(stem * np.float32(gain_crete)).astype(np.float32) for stem in stems]
            for partie in parties:
                partie.gain *= gain_crete
            self.journal(f"    crête {crete:.2f} : gain commun {gain_crete:.3f} sur tous les stems")
        depart_mix = time.perf_counter()
        melange = np.zeros((n, 2), dtype=np.float64)
        for stem in stems:
            melange += stem
        melange32 = melange.astype(np.float32)
        if prod is not None:
            melange32 = appliquer_production(melange32, prod, graine_reverb)
        cout_mix = time.perf_counter() - depart_mix

        for partie, stem in zip(parties, stems, strict=True):
            partie.empreinte = hashlib.sha256(np.ascontiguousarray(stem).tobytes()).hexdigest()
        cout_total = time.perf_counter() - depart_total
        verite = {
            "format": FORMAT, "version": VERSION, "graine": int(graine), "commit": commit_du_depot(),
            "tempo": s.tempo, "mineur": s.mineur, "tonique": s.tonique, "progression": s.progression,
            "mesures": s.mesures, "duree": s.duree, "duree_rendu": duree_rendu, "sample_rate": SR,
            "cas": cas_choisi, "nombre_de_parties": len(parties),
            # B5 : LES OPTIONS QUI CONDITIONNENT LE RÉSULTAT VONT DANS LA
            # PROVENANCE. Deux lots ne se comparent que si elles sont les mêmes.
            "exigences": {"arrangement": self.arrangement, "notes_breves": self.notes_breves,
                          "echantillons": self.echantillons, "borne_hauteur": self.borne_hauteur},
            "arrangement": arrangement,
            "gain_crete": gain_crete, "crete_avant_gain": crete,
            "production": asdict(prod) if prod else None,
            "melange_est_la_somme_des_stems": prod is None,
            "seuils": {"rms_sonde_minimal": RMS_SONDE_MINIMAL, "rms_stem_minimal": RMS_STEM_MINIMAL,
                       "tirages_de_patch": TIRAGES_DE_PATCH},
            "machines_tirables": list(self.machines),
            "machines_ecartees": machines_ecartees,
            "moteur": identite_du_moteur(self.engine),
            "parties": [asdict(p) for p in parties],
            "empreinte_melange": hashlib.sha256(np.ascontiguousarray(melange32).tobytes()).hexdigest(),
            "cout": {"rendu_s": sum(p.cout_rendu_s for p in parties), "mixage_s": cout_mix, "total_s": cout_total},
        }
        return verite, stems, melange32

    def _phraser_bref(self, rng: np.random.Generator, s: Structure,
                      parties: List[Partie]) -> Dict[int, List[List[float]]]:
        """B5, exigence 2 : quelles parties jouent bref, et le phrasé d'avant.

        LE CHANT ET LA NAPPE EN SONT EXCLUS, et c'est une décision, pas un
        oubli : une voix qui articule des doubles croches détachées de soixante
        millisecondes n'est plus une voix, et une nappe dont c'est la définition
        de tenir n'en serait plus une. L'exigence porte sur les notes brèves du
        corpus, pas sur leur présence dans chaque rôle.

        Rend le phrasé LONG de chaque partie touchée, par indice : une machine
        peut ne rien rendre d'audible sur des notes de cette durée, et il faut
        alors pouvoir y revenir plutôt que de perdre la partie.
        """
        avant: Dict[int, List[List[float]]] = {}
        eligibles = [i for i, partie in enumerate(parties)
                     if partie.role in ("melodie", "accompagnement")]
        if not eligibles:
            self.journal("    notes brèves : aucune partie mélodique éligible dans ce morceau")
            return avant
        # LA PREMIÈRE ÉLIGIBLE JOUE BREF, TOUJOURS ; les autres au tirage. Le
        # critère du cahier des charges porte sur le CORPUS — un cinquième des
        # parties mélodiques —, et un tirage seul ne le garantit pas : mesuré, le
        # même lot est passé de 19 à 14 parties brèves sur 74 (le seuil est 15)
        # parce que le rejet des patchs mal accordés avait décalé le flux de
        # tirages. Un critère de corpus qui dépend du hasard d'un autre mécanisme
        # n'est pas tenu, il est eu. Avec la garantie, le plancher est d'une
        # partie par morceau et le tirage n'ajoute que de la variété.
        choisies = [eligibles[0]] + [i for i in eligibles[1:] if rng.random() < BREVE_PROPORTION]
        for indice in choisies:
            partie = parties[indice]
            duree_breve = float(rng.uniform(*BREVE_DUREE_S))
            avant[indice] = list(partie.notes)
            partie.notes = raccourcir_phrase(rng, s, partie.notes, duree_breve)
            partie.phrase_breve = {"duree_s": duree_breve, "notes": len(partie.notes),
                                   "sous_120ms": sum(1 for note in partie.notes if note[3] < 0.120)}
            self.journal(f"    {partie.role} joue bref : {len(avant[indice])} notes → "
                         f"{len(partie.notes)} frappes de {duree_breve * 1000:.0f} ms")
        return avant

    def _rendre_partie(self, partie: Partie, duree: float, rng: np.random.Generator) -> np.ndarray:
        notes = [Note(int(n[0]), int(n[1]), float(n[2]), float(n[3])) for n in partie.notes]
        for _essai in range(TIRAGES_DE_PATCH + 1):
            audio = self._rendre(partie.machine, partie.patch, notes, duree, partie.profil_chemin)
            if audio.size and np.isfinite(audio).all() and _rms(audio) >= RMS_STEM_MINIMAL:
                return np.asarray(audio, dtype=np.float32)
            # La sonde était audible et le stem ne l'est pas (une enveloppe
            # qui ne s'ouvre pas sur des notes courtes, par exemple) : on le
            # DIT, et on retire.
            partie.patchs_rejetes += 1
            self.journal(f"    {partie.machine} : stem inaudible (RMS < {RMS_STEM_MINIMAL}) — patch retiré")
            if not partie.patch:
                break  # c'était déjà le patch d'usine : retirer ne changera rien
            note_sonde = int(partie.notes[0][0]) if partie.notes else 60
            partie.patch, partie.vecteur, rejets, partie.origine_patch = self.tirer_patch(
                rng, partie.machine, note_sonde, partie.profil_chemin)
            partie.patchs_rejetes += rejets
            # Le patch a changé : la dérive mesurée de la partie aussi. Sans
            # cette ligne la vérité porterait celle du patch REMPLACÉ, ce qui est
            # la pire sorte de mensonge — un chiffre juste sur autre chose.
            partie.desaccord_mesure_demi_tons = self._derive_sonde
            partie.note_sonde = int(note_sonde)
        raise VsmEngineError(f"{partie.machine} ne rend rien d'audible sur ses notes, même au patch d'usine")


# ---------------------------------------------------------------------------
# Production : réverbération courte et compression légère, seedées
# ---------------------------------------------------------------------------

def appliquer_production(melange: np.ndarray, prod: Production, graine: int) -> np.ndarray:
    from scipy.signal import fftconvolve, lfilter

    rng = np.random.default_rng(graine)
    n_ir = int(prod.reverb_duree_s * SR)
    t = np.arange(n_ir) / SR
    tau = prod.reverb_duree_s / 6.9  # −60 dB au bout de la durée
    a = math.exp(-2 * math.pi * prod.reverb_coupure_hz / SR)
    sortie = np.zeros_like(melange, dtype=np.float64)
    for canal in range(melange.shape[1]):
        bruit = rng.standard_normal(n_ir) * np.exp(-t / tau)
        bruit[:int(0.005 * SR)] = 0.0  # pré-délai de 5 ms
        ir = lfilter([1 - a], [1, -a], bruit)
        ir /= math.sqrt(float(np.sum(ir ** 2))) + 1e-12
        sec = melange[:, canal].astype(np.float64)
        humide = fftconvolve(sec, ir)[:sec.size]
        sortie[:, canal] = sec + prod.reverb_mix * humide
    # Compression RMS, feed-forward, gain lissé attaque/relâche, sur les deux canaux liés.
    puissance = np.mean(sortie ** 2, axis=1)
    fenetre = int(0.03 * SR)
    noyau = np.ones(fenetre) / fenetre
    rms = np.sqrt(np.convolve(puissance, noyau, mode="same") + 1e-12)
    niveau_db = 20 * np.log10(rms + 1e-9)
    exces = np.maximum(0.0, niveau_db - prod.compresseur_seuil_db)
    gain_db = -exces * (1.0 - 1.0 / prod.compresseur_ratio)
    gain = 10 ** (gain_db / 20)
    lisse = np.empty_like(gain)
    ca = math.exp(-1.0 / (prod.compresseur_attaque_s * SR))
    cr = math.exp(-1.0 / (prod.compresseur_relache_s * SR))
    g = 1.0
    for i in range(gain.size):
        c = ca if gain[i] < g else cr
        g = c * g + (1 - c) * gain[i]
        lisse[i] = g
    sortie *= lisse[:, None]
    rms_avant = _rms(melange)
    rms_apres = _rms(sortie)
    rattrapage = rms_avant / rms_apres if rms_apres > 0 else 1.0
    sortie *= rattrapage
    prod.compresseur_rattrapage = float(rattrapage)
    crete = float(np.abs(sortie).max()) if sortie.size else 0.0
    if crete > 0.99:
        sortie *= 0.99 / crete
    return sortie.astype(np.float32)


# ---------------------------------------------------------------------------
# Écriture : un dossier par morceau, la vérité en dernier
# ---------------------------------------------------------------------------

def ecrire_wav_float(chemin: Path, audio: np.ndarray) -> None:
    """WAV float32 MINIMAL : RIFF, fmt, data — et rien d'autre.

    libsndfile ajoute aux WAV flottants un bloc PEAK qui porte l'HEURE
    d'écriture : deux générations aux mêmes échantillons donnaient deux
    fichiers différents à l'octet 61, et « même graine → même morceau au bit
    près » ne se vérifiait plus sur le fichier. Ici, mêmes échantillons →
    mêmes octets. Le lecteur de la chaîne (`lire_wav`) et soundfile lisent
    ce format sans rien de plus.
    """
    import struct

    donnees = np.ascontiguousarray(audio, dtype=np.float32)
    if donnees.ndim == 1:
        donnees = donnees[:, None]
    canaux = int(donnees.shape[1])
    brut = donnees.astype("<f4").tobytes()
    entete = b"RIFF" + struct.pack("<I", 36 + len(brut)) + b"WAVE"
    entete += b"fmt " + struct.pack("<IHHIIHH", 16, 3, canaux, SR, SR * canaux * 4, canaux * 4, 32)
    entete += b"data" + struct.pack("<I", len(brut))
    Path(chemin).write_bytes(entete + brut)


def lire_wav_float(chemin: Path) -> np.ndarray:
    import soundfile as sf

    audio, taux = sf.read(str(chemin), dtype="float32", always_2d=True)
    if taux != SR:
        raise ValueError(f"{chemin} : {taux} Hz, attendu {SR}")
    return audio


def ecrire_morceau(dossier: Path, verite: dict, stems: Sequence[np.ndarray], melange: np.ndarray) -> None:
    dossier.mkdir(parents=True, exist_ok=True)
    (dossier / "stems-vrais").mkdir(exist_ok=True)
    for partie, stem in zip(verite["parties"], stems, strict=True):
        ecrire_wav_float(dossier / partie["fichier"], stem)
    ecrire_wav_float(dossier / "morceau.wav", melange)
    provisoire = dossier / "verite.json.tmp"
    provisoire.write_text(json.dumps(verite, indent=1, ensure_ascii=False), encoding="utf-8")
    os.replace(provisoire, dossier / "verite.json")


def morceau_complet(dossier: Path) -> bool:
    verite = dossier / "verite.json"
    if not verite.exists():
        return False
    try:
        contenu = json.loads(verite.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return False
    return (contenu.get("format") == FORMAT and (dossier / "morceau.wav").exists()
            and all((dossier / p["fichier"]).exists() for p in contenu.get("parties", [])))


def stems_attendus(verite: dict, dossier: Path) -> Dict[str, np.ndarray]:
    """Les stems que la SÉPARATION devrait rendre : bass, drums, other — sommés depuis la vérité."""
    groupes: Dict[str, np.ndarray] = {}
    for partie in verite["parties"]:
        nom = {"basse": "bass", ROLE_BATTERIE: "drums"}.get(partie["role"], "other")
        stem = lire_wav_float(dossier / partie["fichier"]).astype(np.float64)
        groupes[nom] = groupes[nom] + stem if nom in groupes else stem
    return {nom: audio.astype(np.float32) for nom, audio in groupes.items()}
