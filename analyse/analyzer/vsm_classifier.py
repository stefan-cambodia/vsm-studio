"""
Classifieur de MACHINE — phase A1 de `docs/ROADMAP-apprentissage.md`.

CE QU'IL FAIT : à partir des descripteurs v2 d'un extrait, il rend un
CLASSEMENT des machines du parc, plus une sortie d'ABSTENTION quand rien du
parc ne convient. Il ne produit pas une seconde d'audio, et il ne remplace
aucune mesure : il dit dans quel ordre essayer, la recherche et l'arbitrage
tranchent.

POURQUOI IL EST UTILE, ET LA MESURE QUI LE DIT. Le § 1.2 du cahier des charges
le tient d'une observation payée en phase 9 : le choix de machine est limité par
le BUDGET, pas par la description des machines — le Juno-106 bat le pcmhybrid
à budget suffisant et perd au budget par défaut. La présélection à deux étages
coûte 174 s ; un classement quasi gratuit peut lui désigner les trois à cinq
machines qui méritent le budget complet.

CE QU'IL NE DOIT SURTOUT PAS FAIRE, et c'est écrit avant de mesurer : classer un
violon en « MS-20 avec assurance ». Le § 4 appelle ça « le pire résultat
possible de ce projet », et l'abstention n'est donc pas une option de confort
mais une sortie obligatoire, testée.

DEUX PIÈGES DE MÉTHODE, TRAITÉS ICI PLUTÔT QUE DÉCOUVERTS PLUS TARD :

1. **La coupure se fait PAR PATCH, jamais par exemple.** Les seize notes d'un
   même patch sont seize vues du même son ; les répartir au hasard mettrait à
   l'entraînement exactement ce qu'on demande de reconnaître à l'épreuve. Le
   score serait faux vers le haut, et rien ne le montrerait.
2. **Les timbres réellement indistinguables sont comptés à part** (§ 1.4).
   Plusieurs soustractifs produisent la même nappe ; exiger de les départager
   serait se fixer une cible impossible, puis tordre le modèle pour l'atteindre.
   La définition retenue est mesurée, pas décrétée — voir `exemples_ambigus`.
"""

from __future__ import annotations

import json
import platform
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

from .vsm_corpus_build import LotDeCorpus, Manifeste, verifie_fraicheur
from .vsm_engine import VsmEngine

FORMAT_MODELE = "vsm-classifieur"
VERSION_MODELE = 1


# ---------------------------------------------------------------------------
# Lecture du corpus
# ---------------------------------------------------------------------------

@dataclass
class CorpusCharge:
    X: np.ndarray            # (n, 43) descripteurs
    machines: np.ndarray     # (n,) indices de classe
    patchs: np.ndarray       # (n,) numéro de patch, unique PAR MACHINE
    noms: List[str]          # noms des classes, dans l'ordre des indices
    augmentations: List[str]
    manifeste: Manifeste

    def groupes(self) -> np.ndarray:
        """Identifiant de groupe pour la coupure : (machine, patch)."""
        return self.machines.astype(np.int64) * 1_000_000 + self.patchs.astype(np.int64)


def charge_corpus(dossier: Path) -> CorpusCharge:
    dossier = Path(dossier)
    manifeste = Manifeste.relit(dossier / "manifeste.json")
    noms = sorted(manifeste.empreintes)
    if not noms:
        raise ValueError("le manifeste ne déclare aucune machine")

    X, machines, patchs, augmentations = [], [], [], []
    for index, machine in enumerate(noms):
        lots = sorted((dossier / machine).glob("lot-*.npz"))
        if not lots:
            raise ValueError(f"aucun lot pour « {machine} » : corpus incomplet")
        for chemin in lots:
            lot = LotDeCorpus.relit(chemin)
            X.append(lot.X)
            machines.append(np.full(len(lot.X), index, dtype=np.int32))
            patchs.append(lot.patchs)
            augmentations.extend(lot.augmentations)
    return CorpusCharge(np.concatenate(X), np.concatenate(machines),
                        np.concatenate(patchs), noms, augmentations, manifeste)


# ---------------------------------------------------------------------------
# Découpage
# ---------------------------------------------------------------------------

def coupe_par_patch(corpus: CorpusCharge, part_epreuve: float = 0.2,
                    graine: int = 20260823) -> Tuple[np.ndarray, np.ndarray]:
    """Indices (entraînement, épreuve), découpés PAR PATCH et par machine.

    Découper par machine séparément garantit que chaque classe est représentée
    des deux côtés dans la même proportion : sur vingt classes, un tirage global
    laisserait certaines classes presque absentes de l'épreuve, et leur score
    n'aurait aucun sens.
    """
    rng = np.random.default_rng(graine)
    entrainement, epreuve = [], []
    for classe in np.unique(corpus.machines):
        masque = corpus.machines == classe
        patchs = np.unique(corpus.patchs[masque])
        rng.shuffle(patchs)
        coupure = max(1, int(round(len(patchs) * (1.0 - part_epreuve))))
        pour_epreuve = set(patchs[coupure:].tolist())
        indices = np.flatnonzero(masque)
        pour_test = np.isin(corpus.patchs[indices], list(pour_epreuve))
        epreuve.append(indices[pour_test])
        entrainement.append(indices[~pour_test])
    return np.concatenate(entrainement), np.concatenate(epreuve)


# ---------------------------------------------------------------------------
# Ambiguïté : les timbres que personne ne peut départager
# ---------------------------------------------------------------------------

def exemples_ambigus(X_entrainement: np.ndarray, y_entrainement: np.ndarray,
                     X_epreuve: np.ndarray, y_epreuve: np.ndarray,
                     moyenne: np.ndarray, echelle: np.ndarray,
                     reference_maximale: int = 20000,
                     graine: int = 20260823) -> np.ndarray:
    """Masque des exemples d'épreuve qu'AUCUN modèle ne pourrait départager.

    DÉFINITION, et elle est mesurée plutôt que décrétée : un exemple est
    ambigu si, dans l'espace normalisé des descripteurs, son plus proche voisin
    d'entraînement appartenant à une AUTRE machine est plus proche que son plus
    proche voisin de sa PROPRE machine. Autrement dit : le son qu'il faut
    reconnaître est, littéralement, produit de plus près par une autre machine
    du parc.

    CE QUE CETTE DÉFINITION NE DIT PAS. Elle ne prouve pas que les deux machines
    sont interchangeables en général — seulement que sur CE son, à ces
    descripteurs, elles le sont. C'est exactement ce dont le § 1.4 a besoin
    (« exclure du dénominateur »), et rien de plus : s'en servir pour déclarer
    deux machines équivalentes serait une extrapolation.
    """
    A = ((X_entrainement - moyenne) / echelle).astype(np.float32)
    B = ((X_epreuve - moyenne) / echelle).astype(np.float32)

    # SOUS-ÉCHANTILLONNAGE DE LA RÉFÉRENCE, et il est nécessaire : la recherche
    # du plus proche voisin est quadratique, et sur un corpus de cent mille
    # exemples la matrice complète des distances ferait des dizaines de
    # gigaoctets. On tire au sort un sous-ensemble STRATIFIÉ par machine — un
    # tirage global sous-représenterait les classes les moins peuplées, et
    # l'ambiguïté se mesurerait alors surtout contre les classes abondantes.
    if len(A) > reference_maximale:
        rng = np.random.default_rng(graine)
        garde = []
        par_classe = max(1, reference_maximale // max(1, len(np.unique(y_entrainement))))
        for classe in np.unique(y_entrainement):
            indices = np.flatnonzero(y_entrainement == classe)
            if len(indices) > par_classe:
                indices = rng.choice(indices, par_classe, replace=False)
            garde.append(indices)
        garde = np.concatenate(garde)
        A, y_entrainement = A[garde], y_entrainement[garde]

    # Distances par produit matriciel : ||a-b||² = ||a||² + ||b||² - 2·a·b.
    # C'est la même chose que `norm(a - b)`, en passant par BLAS au lieu d'un
    # tableau intermédiaire de taille (bloc × référence × 43).
    normes_A = np.einsum("ij,ij->i", A, A)
    ambigus = np.zeros(len(B), dtype=bool)
    classes = np.unique(y_entrainement)
    masques = {int(c): (y_entrainement == c) for c in classes}

    for debut in range(0, len(B), 1024):
        bloc = B[debut:debut + 1024]
        carres = (np.einsum("ij,ij->i", bloc, bloc)[:, None]
                  + normes_A[None, :] - 2.0 * (bloc @ A.T))
        for i in range(len(bloc)):
            vraie = int(y_epreuve[debut + i])
            memes = masques.get(vraie)
            if memes is None or not memes.any() or memes.all():
                continue
            ambigus[debut + i] = carres[i][~memes].min() < carres[i][memes].min()
    return ambigus


# ---------------------------------------------------------------------------
# Le modèle
# ---------------------------------------------------------------------------

@dataclass
class Classifieur:
    """Un classifieur entraîné, avec tout ce qui permet de le refuser."""
    noms: List[str]
    moyenne: np.ndarray
    echelle: np.ndarray
    modele: object                       # estimateur scikit-learn
    empreintes: Dict[str, str]           # celles du corpus d'entraînement
    seuil_abstention: float
    rayon_nouveaute: float
    reference_nouveaute: np.ndarray      # sous-échantillon normalisé du corpus
    graine: int
    date: str
    versions: Dict[str, str]
    mesures: Dict[str, object] = field(default_factory=dict)

    # -- inférence ----------------------------------------------------------

    def classe(self, descripteurs: np.ndarray, k: int = 5) -> Tuple[List[Tuple[str, float]], str]:
        """Rend (classement, motif d'abstention).

        Le motif est vide quand le classement est utilisable. Il est REMPLI, et
        donc imprimable, quand le classifieur refuse de se prononcer : le § 8.3
        interdit toute décision silencieuse.
        """
        vecteur = np.atleast_2d(np.asarray(descripteurs, dtype=np.float64))
        normalise = (vecteur - self.moyenne) / self.echelle
        probabilites = self.modele.predict_proba(normalise)[0]
        ordre = np.argsort(probabilites)[::-1][:k]
        classement = [(self.noms[i], float(probabilites[i])) for i in ordre]

        motif = ""
        if float(probabilites.max()) < self.seuil_abstention:
            motif = (f"aucune machine du parc ne convient : meilleur score "
                     f"{float(probabilites.max()):.2f} sous le seuil {self.seuil_abstention:.2f}")
        else:
            distance = float(np.linalg.norm(self.reference_nouveaute - normalise, axis=1).min())
            if distance > self.rayon_nouveaute:
                motif = (f"aucune machine du parc ne convient : ce son est à {distance:.2f} "
                         f"du corpus, au-delà du rayon {self.rayon_nouveaute:.2f}")
        return classement, motif

    # -- persistance --------------------------------------------------------

    def enregistre(self, chemin: Path) -> None:
        import joblib

        chemin = Path(chemin)
        joblib.dump({"format": FORMAT_MODELE, "version": VERSION_MODELE,
                     "noms": self.noms, "moyenne": self.moyenne, "echelle": self.echelle,
                     "modele": self.modele, "empreintes": self.empreintes,
                     "seuil_abstention": self.seuil_abstention,
                     "rayon_nouveaute": self.rayon_nouveaute,
                     "reference_nouveaute": self.reference_nouveaute,
                     "graine": self.graine, "date": self.date,
                     "versions": self.versions, "mesures": self.mesures}, chemin)

    @staticmethod
    def relit(chemin: Path) -> "Classifieur":
        import joblib

        donnees = joblib.load(Path(chemin))
        if donnees.get("format") != FORMAT_MODELE:
            raise ValueError(f"modèle de format inattendu : {donnees.get('format')!r}")
        if donnees.get("version") != VERSION_MODELE:
            raise ValueError(f"modèle de version {donnees.get('version')} non prise en charge")
        return Classifieur(
            noms=list(donnees["noms"]), moyenne=donnees["moyenne"], echelle=donnees["echelle"],
            modele=donnees["modele"], empreintes=dict(donnees["empreintes"]),
            seuil_abstention=float(donnees["seuil_abstention"]),
            rayon_nouveaute=float(donnees["rayon_nouveaute"]),
            reference_nouveaute=donnees["reference_nouveaute"],
            graine=int(donnees["graine"]), date=str(donnees["date"]),
            versions=dict(donnees["versions"]), mesures=dict(donnees.get("mesures", {})))

    def verifie_fraicheur(self, engine: VsmEngine, sample_rate: int = 44100):
        """Le son des machines a-t-il bougé depuis l'entraînement ?

        C'est la contrepartie de la péremption du corpus (A0.3) : un modèle
        entraîné sur le son d'hier ne se trompe pas bruyamment, il classe
        plausiblement et faux. Il doit donc être REFUSÉ, pas appliqué.
        """
        faux_manifeste = Manifeste(date=self.date, commit="", sample_rate=sample_rate,
                                    graine=self.graine, patchs_par_machine=0, grille={},
                                    augmentations=[], versions={}, empreintes=self.empreintes)
        return verifie_fraicheur(faux_manifeste, engine)


def _versions() -> Dict[str, str]:
    import sklearn

    return {"python": platform.python_version(), "numpy": np.__version__,
            "sklearn": sklearn.__version__}


# QUANTILE DU RAYON D'ABSTENTION, et le choix se justifie par une ASYMÉTRIE.
#
# Un rayon large laisse passer des sons hors du parc et le classifieur les
# nomme avec assurance : c'est une erreur de VERDICT, elle se propage dans toute
# la chaîne et personne ne la voit. Un rayon serré refuse des sons que le parc
# aurait pu servir : c'est une perte de TEMPS, la chaîne retombe simplement sur
# la présélection complète d'aujourd'hui, et le résultat final est le même.
#
# Les deux erreurs ne coûtent donc pas la même chose, et le réglage doit pencher
# du côté du refus. Mesuré : au quantile 99,5 % (rayon 6,95), le modèle
# n'écartait que 1,7 % d'un enregistrement de piano réel et le classait
# « vsm.sh101 » avec un score de 1,00. Au quantile 90 % (rayon ≈ 3,9), il en
# écarte 75 %, pour 9 % de refus abusifs sur le corpus lui-même.
QUANTILE_RAYON = 0.90


def entraine(corpus: CorpusCharge, graine: int = 20260823, part_epreuve: float = 0.2,
             seuil_abstention: float = 0.20, quantile_rayon: float = QUANTILE_RAYON,
             progression=None) -> Tuple[Classifieur, Dict[str, object]]:
    """Entraîne et MESURE. Rend le classifieur et le rapport de son épreuve."""
    from sklearn.ensemble import HistGradientBoostingClassifier

    indices_entrainement, indices_epreuve = coupe_par_patch(corpus, part_epreuve, graine)
    X_entrainement = corpus.X[indices_entrainement].astype(np.float64)
    y_entrainement = corpus.machines[indices_entrainement]
    X_epreuve = corpus.X[indices_epreuve].astype(np.float64)
    y_epreuve = corpus.machines[indices_epreuve]

    moyenne = X_entrainement.mean(axis=0)
    echelle = X_entrainement.std(axis=0)
    echelle[echelle < 1e-9] = 1.0

    if progression:
        progression(f"entraînement sur {len(X_entrainement)} exemples, "
                    f"épreuve sur {len(X_epreuve)}")

    # Gradient boosting par histogrammes : rapide sur CPU, sans réglage à
    # trouver, et il accepte des descripteurs d'échelles très différentes. Le
    # § 4 impose « petit et CPU » ; celui-ci s'entraîne en dizaines de secondes.
    modele = HistGradientBoostingClassifier(random_state=graine, max_iter=200,
                                             early_stopping=False)
    modele.fit((X_entrainement - moyenne) / echelle, y_entrainement)

    probabilites = modele.predict_proba((X_epreuve - moyenne) / echelle)
    classement = np.argsort(probabilites, axis=1)[:, ::-1]
    dans_top = {k: float(np.mean([y_epreuve[i] in classement[i, :k]
                                   for i in range(len(y_epreuve))]))
                for k in (1, 3, 5)}

    if progression:
        progression("recherche des exemples ambigus (plus proche voisin)")
    ambigus = exemples_ambigus(X_entrainement, y_entrainement, X_epreuve, y_epreuve,
                               moyenne, echelle, graine=graine)
    net = ~ambigus
    dans_top_net = {k: (float(np.mean([y_epreuve[i] in classement[i, :k]
                                        for i in np.flatnonzero(net)]))
                        if net.any() else float("nan"))
                    for k in (1, 3, 5)}

    # Rayon de nouveauté : la distance au corpus qu'un son du parc ne dépasse
    # presque jamais. Au-delà, on est hors de ce que le modèle a vu, et le §4
    # exige l'abstention plutôt qu'un score confiant.
    rng = np.random.default_rng(graine)
    reference = ((X_entrainement - moyenne) / echelle)
    if len(reference) > 4000:
        reference = reference[rng.choice(len(reference), 4000, replace=False)]
    # SOUS-ÉCHANTILLON TIRÉ AU SORT, et non les premiers venus. `coupe_par_patch`
    # rend ses indices RANGÉS PAR CLASSE : prendre les quatre mille premiers ne
    # calibrerait le rayon que sur les quatre ou cinq premières machines, et le
    # rayon publié vaudrait pour elles seules. C'est le genre d'erreur qui ne
    # produit pas de message : le chiffre sort, il est plausible, et il est faux.
    tous = ((X_epreuve - moyenne) / echelle).astype(np.float32)
    if len(tous) > 4000:
        tous = tous[rng.choice(len(tous), 4000, replace=False)]
    normalise_epreuve = tous
    reference32 = reference.astype(np.float32)
    normes_reference = np.einsum("ij,ij->i", reference32, reference32)
    plus_proches = []
    for debut in range(0, len(normalise_epreuve), 1024):
        bloc = normalise_epreuve[debut:debut + 1024]
        carres = (np.einsum("ij,ij->i", bloc, bloc)[:, None]
                  + normes_reference[None, :] - 2.0 * (bloc @ reference32.T))
        plus_proches.append(np.sqrt(np.maximum(carres.min(axis=1), 0.0)))
    distances = np.concatenate(plus_proches)
    rayon = float(np.quantile(distances, quantile_rayon))
    # Le taux de refus abusif est PUBLIÉ avec le rayon : sans lui, personne ne
    # peut juger si le réglage est prudent ou paranoïaque.
    refus_abusifs = float(np.mean(distances > rayon))

    mesures = {
        "exemples": {"entrainement": int(len(X_entrainement)), "epreuve": int(len(X_epreuve))},
        "machines": corpus.noms,
        "top1": dans_top[1], "top3": dans_top[3], "top5": dans_top[5],
        "ambigus": int(ambigus.sum()),
        "partAmbigus": float(ambigus.mean()),
        "top1SansAmbigus": dans_top_net[1],
        "top3SansAmbigus": dans_top_net[3],
        "top5SansAmbigus": dans_top_net[5],
        "rayonNouveaute": rayon,
        "quantileRayon": quantile_rayon,
        "refusAbusifs": refus_abusifs,
        "seuilAbstention": seuil_abstention,
    }

    classifieur = Classifieur(
        noms=list(corpus.noms), moyenne=moyenne, echelle=echelle, modele=modele,
        empreintes=dict(corpus.manifeste.empreintes), seuil_abstention=seuil_abstention,
        rayon_nouveaute=rayon, reference_nouveaute=reference, graine=graine,
        date=datetime.now(timezone.utc).isoformat(timespec="seconds"),
        versions=_versions(), mesures=mesures)
    return classifieur, mesures


def matrice_de_confusion(corpus: CorpusCharge, classifieur: Classifieur,
                          indices: np.ndarray) -> np.ndarray:
    X = (corpus.X[indices].astype(np.float64) - classifieur.moyenne) / classifieur.echelle
    predits = classifieur.modele.predict(X)
    n = len(classifieur.noms)
    matrice = np.zeros((n, n), dtype=np.int32)
    for vrai, predit in zip(corpus.machines[indices], predits):
        matrice[int(vrai), int(predit)] += 1
    return matrice
