"""
Banc des MOTIFS-VÉRITÉ de batterie — le juge de la phase A2.

CE QUE C'EST. Un motif de quatre mesures dont on CONNAÎT chaque frappe, rendu
par une boîte à rythmes du parc, puis passé dans `build_drum_kit` exactement
comme le serait un stem séparé. On compare ce que le détecteur a trouvé à ce
qu'on a joué : pièce par pièce, frappes retrouvées et frappes INVENTÉES.

POURQUOI IL EXISTE COMME SCRIPT, ET PAS SEULEMENT COMME CHIFFRE. Les tableaux de
l'en-tête de `vsm_drumkit.py` et du § 9.5 de la feuille de route ont été
mesurés à la main, à trois architectures d'intervalle, et ils se contredisent
déjà : le cahier des charges de l'apprentissage écrit « 8/32 aujourd'hui » là
où le module mesure 46/64. Sans juge reproductible, la phase A2 ne saurait ni
d'où elle part ni si elle a gagné. Un banc qui se rejoue en une commande est
donc la première livraison de la phase, avant tout modèle.

LES DEUX MOTIFS sont ceux du § 9.5, pour que les chiffres se comparent :

  A — « double-croche » : grosse caisse sur chaque temps (16), caisse claire
      sur les temps 2 et 4 (8), charleston sur chaque double-croche (64).
      C'est le motif qui a tué les trois premières architectures, parce que
      presque chaque charleston tombe SUR une autre pièce.
  B — « contretemps » : grosse caisse sur chaque temps (16), caisse claire sur
      2 et 4 (8), charleston sur les contretemps seulement (16). Aucune
      charleston ne coïncide avec une autre pièce : le cas facile, qui sert de
      témoin.
  C — « familles » (A2.3) : grosse caisse sur 1 et 3 (8), CLAP seul sur 2 et
      4 (8), charleston aux contretemps (16), deux TOMS sur le « e » et le
      « a » du quatrième temps de chaque mesure (8). Chaque clap et chaque tom
      frappe SEUL -- sur la queue de la pièce précédente, comme dans un
      morceau, mais sans personne dessous : ce motif ne juge pas la
      superposition, il juge si le détecteur sait NOMMER au-delà des trois
      familles historiques.

UNE FRAPPE EST « RETROUVÉE » si le détecteur en a placé une de la bonne famille
à moins de `TOLERANCE` secondes. Une frappe détectée sans contrepartie dans
la vérité est « INVENTÉE ». La règle du § 9.5 tient : une frappe manquante
s'entend comme un motif plus clairsemé, une frappe inventée comme une faute.
"""

from __future__ import annotations

import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

from .vsm_drumkit import build_drum_kit
from .vsm_engine import Note, VsmEngine

TOLERANCE = 0.030   # 30 ms : la moitié d'une double-croche à 250 BPM, large à 140
BPM = 140.0
BARS = 4

GM = {"kick": 36, "snare": 38, "hihat": 42, "openhat": 46, "clap": 39, "tom": 45}


@dataclass(frozen=True)
class Motif:
    nom: str
    machine: str
    frappes: Dict[str, Tuple[float, ...]]   # famille -> instants en secondes

    def duree(self) -> float:
        return BARS * 4 * 60.0 / BPM + 1.0


def _temps(bars: int = BARS, subdivision: int = 4) -> List[float]:
    """Instants de toutes les subdivisions d'un temps sur `bars` mesures."""
    pas = 60.0 / BPM / subdivision
    return [i * pas for i in range(bars * 4 * subdivision)]


def motif_double_croche(machine: str = "vsm.tr909") -> Motif:
    seizieme = _temps(subdivision=4)
    temps = _temps(subdivision=1)
    return Motif(
        nom="double-croche",
        machine=machine,
        frappes={
            "kick": tuple(temps),
            "snare": tuple(t for i, t in enumerate(temps) if i % 4 in (1, 3)),
            "hihat": tuple(seizieme),
        },
    )


def motif_contretemps(machine: str = "vsm.tr808") -> Motif:
    temps = _temps(subdivision=1)
    croches = _temps(subdivision=2)
    return Motif(
        nom="contretemps",
        machine=machine,
        frappes={
            "kick": tuple(temps),
            "snare": tuple(t for i, t in enumerate(temps) if i % 4 in (1, 3)),
            "hihat": tuple(t for i, t in enumerate(croches) if i % 2 == 1),
        },
    )


def motif_familles(machine: str = "vsm.tr909") -> Motif:
    temps = _temps(subdivision=1)
    croches = _temps(subdivision=2)
    seizieme = _temps(subdivision=4)
    return Motif(
        nom="familles",
        machine=machine,
        frappes={
            "kick": tuple(t for i, t in enumerate(temps) if i % 4 in (0, 2)),
            "clap": tuple(t for i, t in enumerate(temps) if i % 4 in (1, 3)),
            "hihat": tuple(t for i, t in enumerate(croches) if i % 2 == 1),
            "tom": tuple(t for i, t in enumerate(seizieme) if i % 16 in (13, 15)),
        },
    )


MOTIFS = (motif_double_croche, motif_contretemps, motif_familles)


def rend_motif(motif: Motif, engine: VsmEngine, sample_rate: int = 44100,
               velocite: int = 110) -> np.ndarray:
    notes = [Note(GM[famille], velocite, t, 0.1)
             for famille, instants in motif.frappes.items() for t in instants]
    return engine.render(motif.machine, {}, notes, motif.duree(), sample_rate=sample_rate)


@dataclass
class ScoreFamille:
    famille: str
    attendues: int
    retrouvees: int
    inventees: int
    # Familles sous lesquelles le détecteur a rangé les frappes attendues qu'il
    # a trouvées au bon instant mais nommées AUTREMENT. C'est l'information
    # qu'A2 doit lire : une charleston classée « snare » n'est pas manquante,
    # elle est mal nommée, et ce n'est pas le même défaut.
    confondues_avec: Dict[str, int] = field(default_factory=dict)

    @property
    def rappel(self) -> float:
        return self.retrouvees / self.attendues if self.attendues else float("nan")


@dataclass
class ScoreMotif:
    motif: str
    machine: str
    familles: List[ScoreFamille]
    pieces_detectees: int
    frappes_detectees: int

    def ligne(self, famille: str) -> Optional[ScoreFamille]:
        return next((f for f in self.familles if f.famille == famille), None)


def _famille_canonique(nom: str) -> str:
    """Le détecteur distingue kick/kick2, snare/snare2, hihat/openhat/pedalhat.
    Pour juger la présence d'une frappe, ces variantes sont la même pièce."""
    for base in ("kick", "snare", "hihat", "openhat", "pedalhat", "tom", "cymbal", "percussion"):
        if nom.startswith(base):
            return "hihat" if base in ("openhat", "pedalhat") else base
    return nom


def juge(motif: Motif, engine: VsmEngine, sample_rate: int = 44100,
         audio: Optional[np.ndarray] = None, hit_classifier=None) -> ScoreMotif:
    """Rend le motif, le passe dans `build_drum_kit`, et compte.

    `hit_classifier` : le classifieur de frappes (A2), s'il y en a un ; sans
    lui, c'est le nommage par gabarit qui est jugé."""
    if audio is None:
        audio = rend_motif(motif, engine, sample_rate)

    with tempfile.TemporaryDirectory(prefix="vsm-banc-batterie-") as dossier:
        kit = build_drum_kit(audio, sample_rate, Path(dossier), write_samples=False,
                             hit_classifier=hit_classifier)

    detectees: Dict[str, List[float]] = {}
    frappes_total = 0
    if kit is not None:
        for emplacement in kit.slots:
            famille = _famille_canonique(emplacement.family)
            detectees.setdefault(famille, []).extend(emplacement.onsets)
            frappes_total += len(emplacement.onsets)

    scores: List[ScoreFamille] = []
    for famille, attendues in motif.frappes.items():
        trouvees = sorted(detectees.get(famille, []))
        retrouvees = 0
        confondues: Dict[str, int] = {}
        for t in attendues:
            if any(abs(t - d) <= TOLERANCE for d in trouvees):
                retrouvees += 1
                continue
            # Pas trouvée sous son nom : l'a-t-on trouvée sous un AUTRE ?
            for autre, instants in detectees.items():
                if autre != famille and any(abs(t - d) <= TOLERANCE for d in instants):
                    confondues[autre] = confondues.get(autre, 0) + 1
                    break
        inventees = sum(1 for d in trouvees
                        if not any(abs(t - d) <= TOLERANCE for t in attendues))
        scores.append(ScoreFamille(famille, len(attendues), retrouvees, inventees, confondues))

    return ScoreMotif(motif.nom, motif.machine, scores,
                      pieces_detectees=len(kit.slots) if kit else 0,
                      frappes_detectees=frappes_total)


def tableau(scores: Sequence[ScoreMotif]) -> str:
    lignes = [f"{'motif':<14} {'machine':<10} {'pièce':<7} {'retrouvées':>11} {'inventées':>10}  confondues avec"]
    for s in scores:
        for f in s.familles:
            conf = ", ".join(f"{k}×{v}" for k, v in f.confondues_avec.items()) or "—"
            lignes.append(f"{s.motif:<14} {s.machine.split('.')[-1]:<10} {f.famille:<7} "
                          f"{f.retrouvees:>4} / {f.attendues:<4} {f.inventees:>10}  {conf}")
        lignes.append(f"{'':<14} {'':<10} {'(kit)':<7} {s.pieces_detectees} pièce(s), "
                      f"{s.frappes_detectees} frappes")
    return "\n".join(lignes)
