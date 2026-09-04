"""H25 APPRIS : « un instrument ou deux ? », décidé sur la STRUCTURE.

Le cahier des charges de ce module est écrit d'avance dans
`docs/CDC-banc-synthetique.md` § 8, et il est repris ici parce que c'est ici
qu'on le tiendra ou pas.

LE PROBLÈME. `registres_par_vides` découpe un stem fourre-tout là où sa
transcription laisse un vide de hauteur. C'est juste quand le vide sépare deux
PARTIES (une basse et une mélodie jouées par deux instruments) et faux quand il
sépare les deux MAINS d'un même instrument -- un piano joue la fondamentale en
bas et l'accord en haut, et le milieu reste vide. Le découpage dit alors
« deux » là où il n'y en a qu'un, et la reconstruction fabrique une piste de
trop. C'est H25, et la mesure l'a montré : sur les dix morceaux de S1, les
registres disjoints ont été FONDUS trois fois sur trois, et le cas deux-mains
coupé en deux une fois sur deux.

CE QUE CE MODULE N'A PAS LE DROIT DE REGARDER. Le TIMBRE. H25 a établi que le
timbre ment : deux mains d'un même piano sonnent différemment (registres,
étouffoirs, dynamique), et deux instruments différents peuvent sonner pareil
après séparation. Les descripteurs sont donc STRUCTURELS, tirés des seules
notes -- quand elles tombent, combien il y en a, sur quelle étendue.

CE QU'IL DÉCIDE, ET CE QU'IL NE DÉCIDE PAS. Il répond à UNE question, sur UNE
paire de registres voisins : « ces deux-là sont-ils une seule main gauche et
une seule main droite ? ». Il ne choisit pas le nombre de parties, il ne
nomme rien, il ne touche à aucun réglage. Le reste de la chaîne est inchangé.

DÉSACTIVÉ PAR DÉFAUT. Le § 8 l'exige : il n'entre dans la chaîne que derrière
`--deux-mains-appris`, et l'option va dans la provenance du rapport.
"""
from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

# La fenêtre de synchronie, en secondes. Trente millisecondes : c'est l'écart
# sous lequel deux attaques s'entendent comme une seule (le seuil d'antériorité
# de la perception), et c'est le chiffre écrit dans le § 8 avant la mesure.
FENETRE_SYNCHRONIE = 0.030

# Les noms des descripteurs, dans l'ordre du vecteur. Écrits une fois : le
# modèle enregistré les porte, et un modèle relu avec d'autres noms est refusé
# plutôt que d'être appliqué à des colonnes qui ne sont plus les mêmes.
DESCRIPTEURS = (
    "synchronie",          # part des attaques du haut à ±30 ms d'une attaque du bas
    "synchronie_inverse",  # ... et réciproquement : une main droite bavarde n'est pas
                           #     la même chose qu'une main gauche bavarde
    "cooccurrence",        # part du temps où les deux registres sonnent ensemble
    "correlation_attaques",# corrélation des densités d'attaques, fenêtre d'un temps
    "rapport_ambitus",     # ambitus du haut / ambitus du bas
    "rapport_densites",    # notes par seconde du haut / du bas
)


@dataclass(frozen=True)
class Descripteurs:
    valeurs: Tuple[float, ...]

    def vecteur(self) -> np.ndarray:
        return np.asarray(self.valeurs, dtype=float)

    def as_dict(self) -> Dict[str, float]:
        return {nom: float(v) for nom, v in zip(DESCRIPTEURS, self.valeurs)}


def _attaques(notes: Sequence[Sequence[float]]) -> np.ndarray:
    """Les instants d'attaque, triés. `notes` est (hauteur, vélocité, début, durée)."""
    if not notes:
        return np.zeros(0)
    return np.sort(np.asarray([float(n[2]) for n in notes], dtype=float))


def _part_synchrone(a: np.ndarray, b: np.ndarray, fenetre: float) -> float:
    """Part des attaques de `a` qui ont une attaque de `b` à moins de `fenetre`."""
    if a.size == 0 or b.size == 0:
        return 0.0
    indices = np.searchsorted(b, a)
    proche = np.full(a.shape, np.inf)
    gauche = np.clip(indices - 1, 0, b.size - 1)
    droite = np.clip(indices, 0, b.size - 1)
    proche = np.minimum(np.abs(a - b[gauche]), np.abs(a - b[droite]))
    return float(np.mean(proche <= fenetre))


def _densite_attaques(attaques: np.ndarray, duree: float, pas: float) -> np.ndarray:
    if duree <= 0.0 or pas <= 0.0:
        return np.zeros(1)
    cases = max(1, int(np.ceil(duree / pas)))
    histo, _ = np.histogram(attaques, bins=cases, range=(0.0, cases * pas))
    return histo.astype(float)


def _correlation(a: np.ndarray, b: np.ndarray) -> float:
    if a.size < 2 or b.size < 2 or a.size != b.size:
        return 0.0
    if float(np.std(a)) < 1e-12 or float(np.std(b)) < 1e-12:
        return 0.0
    return float(np.corrcoef(a, b)[0, 1])


def _cooccurrence(bas: Sequence[Sequence[float]], haut: Sequence[Sequence[float]],
                  duree: float, pas: float = 0.05) -> float:
    """Part du temps où les DEUX registres tiennent une note."""
    if duree <= 0.0 or not bas or not haut:
        return 0.0
    cases = max(1, int(np.ceil(duree / pas)))
    def occupe(notes: Sequence[Sequence[float]]) -> np.ndarray:
        grille = np.zeros(cases, dtype=bool)
        for n in notes:
            debut = max(0, int(float(n[2]) / pas))
            fin = min(cases, int(np.ceil((float(n[2]) + max(1e-3, float(n[3]))) / pas)))
            if fin > debut:
                grille[debut:fin] = True
        return grille
    ga, gb = occupe(bas), occupe(haut)
    union = int(np.count_nonzero(ga | gb))
    if union == 0:
        return 0.0
    return float(np.count_nonzero(ga & gb) / union)


def descripteurs(bas: Sequence[Sequence[float]], haut: Sequence[Sequence[float]],
                 battement: float = 0.5) -> Descripteurs:
    """Les six descripteurs STRUCTURELS d'une paire de registres voisins.

    `bas` et `haut` sont deux listes de notes (hauteur, vélocité, début, durée),
    et rien d'autre : aucun échantillon, aucun spectre, aucun nom de machine.
    """
    ab, ah = _attaques(bas), _attaques(haut)
    duree = 0.0
    for notes in (bas, haut):
        for n in notes:
            duree = max(duree, float(n[2]) + max(0.0, float(n[3])))

    ambitus_bas = (max(float(n[0]) for n in bas) - min(float(n[0]) for n in bas)) if bas else 0.0
    ambitus_haut = (max(float(n[0]) for n in haut) - min(float(n[0]) for n in haut)) if haut else 0.0
    dens_bas = len(bas) / duree if duree > 0 else 0.0
    dens_haut = len(haut) / duree if duree > 0 else 0.0

    pas = max(0.05, battement)
    return Descripteurs((
        _part_synchrone(ah, ab, FENETRE_SYNCHRONIE),
        _part_synchrone(ab, ah, FENETRE_SYNCHRONIE),
        _cooccurrence(bas, haut, duree),
        _correlation(_densite_attaques(ab, duree, pas), _densite_attaques(ah, duree, pas)),
        (ambitus_haut / ambitus_bas) if ambitus_bas > 0 else 0.0,
        (dens_haut / dens_bas) if dens_bas > 0 else 0.0,
    ))


# ---------------------------------------------------------------------------
# Le modèle
# ---------------------------------------------------------------------------

@dataclass
class Modele:
    """Un modèle appris, avec de quoi le refuser s'il n'est pas celui qu'on croit.

    L'EMPREINTE couvre les descripteurs ET les données d'entraînement : deux
    modèles qui portent le même nom et n'ont pas vu les mêmes exemples ne sont
    pas le même modèle, et le rapport de reconstruction doit pouvoir dire
    lequel a servi.
    """
    coefficients: List[float]
    biais: float
    seuil: float
    descripteurs: List[str]
    empreinte: str
    exemples: int
    graine: int

    def predit_un_seul(self, d: Descripteurs) -> bool:
        z = float(np.dot(np.asarray(self.coefficients), d.vecteur()) + self.biais)
        return (1.0 / (1.0 + np.exp(-z))) >= self.seuil

    def to_json(self) -> dict:
        return {
            "format": "vsm-h25-appris", "version": 1,
            "descripteurs": list(self.descripteurs), "coefficients": list(self.coefficients),
            "biais": self.biais, "seuil": self.seuil, "empreinte": self.empreinte,
            "exemples": self.exemples, "graine": self.graine,
        }

    @staticmethod
    def from_json(data: dict) -> "Modele":
        if data.get("format") != "vsm-h25-appris":
            raise ValueError(f"ce fichier n'est pas un modèle H25 (format « {data.get('format')} »)")
        if int(data.get("version", 0)) != 1:
            raise ValueError(f"version de modèle H25 inconnue : {data.get('version')}")
        noms = list(data.get("descripteurs", []))
        if tuple(noms) != DESCRIPTEURS:
            # REFUSÉ, JAMAIS RÉORDONNÉ : appliquer des coefficients à d'autres
            # colonnes que celles qui les ont produits donnerait un modèle qui
            # répond, et qui répond n'importe quoi.
            raise ValueError("les descripteurs du modèle ne sont pas ceux de ce module")
        return Modele(list(data["coefficients"]), float(data["biais"]), float(data["seuil"]),
                      noms, str(data.get("empreinte", "")), int(data.get("exemples", 0)),
                      int(data.get("graine", 0)))


def empreinte_des_exemples(X: np.ndarray, y: np.ndarray) -> str:
    h = hashlib.sha256()
    h.update(np.ascontiguousarray(np.round(X, 6)).tobytes())
    h.update(np.ascontiguousarray(y.astype(np.int8)).tobytes())
    h.update(",".join(DESCRIPTEURS).encode("utf-8"))
    return h.hexdigest()[:16]


def charger(chemin: Path) -> Optional[Modele]:
    if not chemin.is_file():
        return None
    return Modele.from_json(json.loads(chemin.read_text(encoding="utf-8")))
