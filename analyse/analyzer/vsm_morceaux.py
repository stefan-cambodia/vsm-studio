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
from typing import Callable, Dict, List, Optional, Sequence, Tuple

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

ROLES_MELODIQUES = ("basse", "accompagnement", "melodie", "nappe")
ROLE_DEUX_MAINS = "piano-deux-mains"
ROLE_BATTERIE = "batterie"
CAS = ("aucun", "memes-machine-disjoints", "chevauchement", "deux-mains")

# Registres plausibles par rôle (MIDI, bornes incluses).
REGISTRES: Dict[str, Tuple[int, int]] = {
    "basse": (28, 48),
    "accompagnement": (48, 72),
    "melodie": (60, 84),
    "nappe": (48, 76),
}
REGISTRES_DEUX_MAINS = ((36, 52), (60, 84))
# RMS cible par rôle (linéaire), tiré à ±3 dB autour.
NIVEAUX: Dict[str, float] = {
    "basse": 0.10, "batterie": 0.12, "accompagnement": 0.05, "melodie": 0.06,
    "nappe": 0.04, ROLE_DEUX_MAINS: 0.06,
}
GATES: Dict[str, float] = {
    "basse": 0.85, "accompagnement": 0.8, "melodie": 0.85, "nappe": 0.98,
    ROLE_DEUX_MAINS: 0.9,
}

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
            if m not in _NON_MELODIC and m not in _MACHINES_A_PROFIL]


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
        for i, (d, longueur) in enumerate(zip(degres, rythme)):
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

def _tirer_roles(rng: np.random.Generator, nombre: int, cas: str) -> List[Dict[str, object]]:
    """La liste des rôles, avec le cas de parité posé sur une ou deux parties."""
    roles: List[Dict[str, object]] = []
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
    while len(roles) < nombre:
        role = str(rng.choice(["accompagnement", "melodie", "nappe"]))
        roles.append({"role": role, "registre": [list(REGISTRES[role])], "cas": None})
    return roles[:max(nombre, len(roles))]


def _panoramique(rng: np.random.Generator, role: str) -> float:
    if role == "basse":
        return 0.0
    if role == ROLE_BATTERIE:
        return float(rng.uniform(-0.1, 0.1))
    return float(rng.uniform(-0.7, 0.7))


def _panner(mono: np.ndarray, pan: float) -> np.ndarray:
    theta = (pan + 1.0) * math.pi / 4.0
    return np.stack([mono * math.cos(theta), mono * math.sin(theta)], axis=1).astype(np.float32)


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
                 rendre: Optional[Callable[..., np.ndarray]] = None,
                 journal: Optional[Callable[[str], None]] = None):
        self.engine = engine
        self.machines = list(machines) if machines else machines_melodiques_du_banc(engine)
        if not self.machines:
            raise VsmEngineError("aucune machine mélodique cherchable : le moteur est-il vivant ?")
        self.rendre = rendre or (lambda machine, patch, notes, duree: engine.render(
            machine, patch, notes, duration=duree, sample_rate=SR))
        self.journal = journal or (lambda ligne: None)
        self._espaces: Dict[str, list] = {}

    def espace(self, machine: str):
        if machine not in self._espaces:
            self._espaces[machine] = search_space_for_machine(machine, self.engine, max_dimensions=10 ** 6)
        return self._espaces[machine]

    def tirer_patch(self, rng: np.random.Generator, machine: str, note_sonde: int) -> Tuple[Dict[str, float], List[float], int, str]:
        """Un patch audible, ou le patch d'usine après TIRAGES_DE_PATCH rejets."""
        espace = self.espace(machine)
        rejets = 0
        for _ in range(TIRAGES_DE_PATCH):
            vecteur = rng.random(len(espace))
            patch = _vector_to_parameters(espace, vecteur)
            try:
                sonde = self.rendre(machine, patch, [Note(note_sonde, 100, 0.0, 0.75)], 1.0)
            except VsmEngineError as erreur:
                self.journal(f"    {machine} : rendu refusé ({erreur}) — patch retiré")
                rejets += 1
                continue
            if sonde.size and np.isfinite(sonde).all() and _rms(sonde) >= RMS_SONDE_MINIMAL:
                return patch, [float(v) for v in vecteur], rejets, "tiré dans le SearchProfile"
            rejets += 1
        # Huit patchs muets de suite : c'est peut-être la MACHINE qui ne
        # sonne pas sur cette note (une boîte FM ne répond qu'à ses voix). On
        # éprouve le patch d'usine ; s'il est muet aussi, la machine est
        # écartée POUR CE RÔLE, en le disant, et l'appelant en tire une autre.
        try:
            sonde = self.rendre(machine, {}, [Note(note_sonde, 100, 0.0, 0.75)], 1.0)
        except VsmEngineError as erreur:
            raise MachineMuette(f"{machine} : rendu refusé au patch d'usine ({erreur})") from erreur
        if not (sonde.size and np.isfinite(sonde).all() and _rms(sonde) >= RMS_SONDE_MINIMAL):
            raise MachineMuette(f"{machine} : muette sur la note {note_sonde}, même au patch d'usine")
        self.journal(f"    {machine} : {rejets} patchs inaudibles de suite — patch d'usine")
        return {}, [], rejets, f"patch d'usine après {rejets} rejets"

    def fabriquer(self, graine: int, duree: float = 30.0, production: bool = False,
                  cas: Optional[str] = None, nombre_de_parties: Optional[int] = None) -> Tuple[dict, List[np.ndarray], np.ndarray]:
        """Rend (vérité, stems stéréo float32 dans l'ordre, mélange stéréo float32)."""
        depart_total = time.perf_counter()
        rng = np.random.default_rng(int(graine))
        s = Structure(rng, duree)
        nombre = int(nombre_de_parties) if nombre_de_parties else int(rng.integers(2, 13))
        cas_choisi = cas if cas else str(rng.choice(CAS))
        if cas_choisi not in CAS:
            raise ValueError(f"cas inconnu : {cas_choisi} (attendu {', '.join(CAS)})")
        roles = _tirer_roles(rng, nombre, cas_choisi)
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
                                "melodie": notes_melodie, "nappe": notes_nappe}[role]
                    notes = fabrique(rng, s, r, gate)
                    note_sonde = (r[0] + r[1]) // 2
            # La machine, puis son patch ; une machine muette sur la note du
            # rôle est écartée et dite, et une autre est tirée.
            for _ in range(len(self.machines) + 1):
                if role == ROLE_BATTERIE:
                    pass
                elif description.get("paire") and machine_de_paire:
                    machine = machine_de_paire
                else:
                    machine = str(rng.choice(self.machines))
                try:
                    patch, vecteur, rejets, origine = self.tirer_patch(rng, machine, note_sonde)
                    break
                except MachineMuette as erreur:
                    self.journal(f"    écartée : {erreur}")
                    machines_ecartees.append({"role": role, "machine": machine, "raison": str(erreur)})
                    if role == ROLE_BATTERIE:
                        raise
            else:
                raise VsmEngineError(f"aucune machine audible pour le rôle {role}")
            if description.get("paire"):
                machine_de_paire = machine
            niveau_db = float(20 * math.log10(NIVEAUX[role]) + rng.uniform(-3.0, 3.0))
            parties.append(Partie(role=role, machine=machine, patch=patch, vecteur=vecteur, notes=notes,
                                  registre=registres, niveau_rms=10 ** (niveau_db / 20), niveau_db=niveau_db,
                                  gain=1.0, pan=_panoramique(rng, role), gate=gate, pieces=pieces,
                                  cas=description.get("cas"), patchs_rejetes=rejets, origine_patch=origine))

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
            audio = self._rendre_partie(partie, duree_rendu, rng)
            mono = np.zeros(n, dtype=np.float32)
            mono[:min(n, audio.size)] = audio[:n]
            rms = _rms(mono)
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

        for partie, stem in zip(parties, stems):
            partie.empreinte = hashlib.sha256(np.ascontiguousarray(stem).tobytes()).hexdigest()
        cout_total = time.perf_counter() - depart_total
        verite = {
            "format": FORMAT, "version": VERSION, "graine": int(graine), "commit": commit_du_depot(),
            "tempo": s.tempo, "mineur": s.mineur, "tonique": s.tonique, "progression": s.progression,
            "mesures": s.mesures, "duree": s.duree, "duree_rendu": duree_rendu, "sample_rate": SR,
            "cas": cas_choisi, "nombre_de_parties": len(parties),
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

    def _rendre_partie(self, partie: Partie, duree: float, rng: np.random.Generator) -> np.ndarray:
        notes = [Note(int(n[0]), int(n[1]), float(n[2]), float(n[3])) for n in partie.notes]
        for essai in range(TIRAGES_DE_PATCH + 1):
            audio = self.rendre(partie.machine, partie.patch, notes, duree)
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
            partie.patch, partie.vecteur, rejets, partie.origine_patch = self.tirer_patch(rng, partie.machine, note_sonde)
            partie.patchs_rejetes += rejets
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
    for partie, stem in zip(verite["parties"], stems):
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
