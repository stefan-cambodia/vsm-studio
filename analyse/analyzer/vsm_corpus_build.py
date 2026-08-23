"""
Fabrique du CORPUS d'apprentissage (docs/ROADMAP-apprentissage.md, phase A0).

CE QUE CE MODULE AJOUTE À `vsm_corpus.py`. Celui-ci sait déjà tirer un patch
dans l'espace déclaré par la machine, le faire rendre par le moteur et en
calculer les descripteurs — c'est le socle, écrit et mesuré lors de l'étude de
l'estimateur. Ce qu'il ne sait pas faire, et que la phase A0 exige :

  - une GRILLE de notes par patch, au lieu d'une note tirée au hasard. Le § 3
    du cahier des charges demande au moins trois hauteurs × deux durées × deux
    vélocités, et la raison est mesurée ailleurs dans le dépôt : une machine ne
    se reconnaît pas sur une seule note grave ;
  - un MANIFESTE, qui dit de quoi le corpus est fait — empreintes des machines,
    commit du moteur, graines, versions, coût ;
  - la PÉREMPTION : une machine dont le son a bougé invalide son corpus, et
    tout modèle qui en dérive doit être refusé au chargement, pas appliqué en
    silence ;
  - des AUGMENTATIONS seedées, qui rapprochent le corpus (rendu sec) de ce que
    la chaîne rencontre (un stem séparé, avec sa pièce, son mastering et ses
    fuites) — c'est la parade nommée au § 7 contre l'écart de domaine, celui-là
    même qui a tué la première tentative d'estimateur.

CE QU'IL N'AJOUTE PAS : une seule ligne côté C++. Le § 3 l'exige (« le
générateur n'utilise que le pont existant »), et l'empreinte d'une machine est
donc calculée en la FAISANT JOUER plutôt qu'en lisant un fichier du dépôt — ce
qui vaut mieux, puisqu'on mesure alors ce que le moteur produit réellement.
"""

from __future__ import annotations

import hashlib
import json
import platform
import subprocess
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Tuple

import numpy as np

from .vsm_corpus import descriptors
from .vsm_engine import VsmEngine, VsmEngineError, Note
from .vsm_patch_optimizer import SearchParameter, _vector_to_parameters, search_space_for_machine

FORMAT_MANIFESTE = "vsm-corpus"
VERSION_MANIFESTE = 1

# ---------------------------------------------------------------------------
# Empreinte d'une machine
# ---------------------------------------------------------------------------

# Phrase d'empreinte : trois notes de tessitures différentes, deux vélocités,
# patch d'USINE. Volontairement courte — elle est rejouée à chaque vérification
# de fraîcheur — et volontairement fixe : la comparer à elle-même d'un jour à
# l'autre n'a de sens que si rien d'autre ne bouge.
PHRASE_EMPREINTE: Tuple[Tuple[int, int, float, float], ...] = (
    (40, 100, 0.0, 0.35),
    (60, 100, 0.40, 0.35),
    (72, 60, 0.80, 0.35),
)
DUREE_EMPREINTE = 1.4


def machine_fingerprint(engine: VsmEngine, machine: str, sample_rate: int = 44100) -> str:
    """SHA-256 du rendu d'une phrase fixe, patch d'usine.

    C'est l'équivalent Python de `audio/tests/audio_fingerprints.inc`, à ceci
    près qu'elle est calculée EN FAISANT JOUER la machine plutôt qu'en lisant
    une table. Elle capte donc tout ce qui change le son — le DSP, mais aussi
    le profil installé d'une machine à échantillons —, ce qu'une table du dépôt
    ne saurait pas faire.

    Rend une chaîne vide si le moteur refuse la machine : une machine qu'on ne
    sait pas faire jouer n'a pas d'empreinte, et prétendre le contraire ferait
    passer pour « à jour » un corpus qu'on ne peut plus vérifier.
    """
    notes = [Note(note, velocite, debut, duree)
             for note, velocite, debut, duree in PHRASE_EMPREINTE]
    try:
        audio = engine.render(machine, {}, notes, DUREE_EMPREINTE, sample_rate=sample_rate)
    except VsmEngineError:
        return ""
    if audio.size == 0:
        return ""
    return hashlib.sha256(np.asarray(audio, dtype="<f4").tobytes()).hexdigest()


# ---------------------------------------------------------------------------
# Augmentations
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class Augmentation:
    """Une dégradation appliquée au rendu sec avant d'en tirer les descripteurs.

    `applique(audio, sample_rate, rng)` doit être PURE et seedée : deux appels
    avec le même générateur aléatoire rendent le même résultat, sans quoi le
    corpus cesse d'être regénérable — et c'est l'exigence n° 1 du § 3.
    """
    nom: str
    raison: str
    applique: Callable[[np.ndarray, int, np.random.Generator], np.ndarray]


def _egaliseur(audio: np.ndarray, sample_rate: int, rng: np.random.Generator) -> np.ndarray:
    """Bascule grave/aigu à un pôle. Ce qu'un mastering fait de plus visible."""
    pente = float(rng.uniform(-0.6, 0.6))
    coupure = float(rng.uniform(400.0, 3000.0))
    coefficient = float(np.exp(-2.0 * np.pi * coupure / sample_rate))
    grave = np.zeros_like(audio)
    etat = 0.0
    for i, echantillon in enumerate(audio):
        etat = coefficient * etat + (1.0 - coefficient) * float(echantillon)
        grave[i] = etat
    aigu = audio - grave
    return (grave * (1.0 + pente) + aigu * (1.0 - pente)).astype(np.float32)


def _reverberation(audio: np.ndarray, sample_rate: int, rng: np.random.Generator) -> np.ndarray:
    """Pièce COURTE. Une grande salle noierait la note et l'étiquette avec."""
    from scipy.signal import fftconvolve

    t60 = float(rng.uniform(0.15, 0.60))
    melange = float(rng.uniform(0.05, 0.30))
    longueur = max(8, int(t60 * sample_rate))
    reponse = (rng.standard_normal(longueur)
               * np.exp(-6.9 * np.arange(longueur) / longueur)).astype(np.float32)
    norme = float(np.sqrt(np.sum(reponse ** 2))) or 1.0
    humide = fftconvolve(audio, reponse / norme)[:audio.size].astype(np.float32)
    return ((1.0 - melange) * audio + melange * humide).astype(np.float32)


def _compression(audio: np.ndarray, sample_rate: int, rng: np.random.Generator) -> np.ndarray:
    """Compression avec DÉTECTEUR — et le détecteur est tout le sujet.

    La première version était statique : une courbe appliquée échantillon par
    échantillon. Mesurée, elle déplaçait le descripteur de **0,018 écart-type**,
    c'est-à-dire rien. La raison est simple une fois écrite : une courbe sans
    mémoire ne touche que les crêtes, alors que ce qu'un compresseur fait
    réellement — et ce que la métrique regarde, à travers seize des
    quarante-trois descripteurs — c'est écraser l'ENVELOPPE dans le temps.

    Détecteur d'enveloppe à attaque et relâchement, donc, avec un `makeup` qui
    remet le niveau : sans lui on mesurerait une baisse de volume, pas une
    compression.

    LE CORRECTIF N'A PAS SUFFI, ET C'EST ÉCRIT ICI PLUTÔT QUE TU : le
    déplacement passe de 0,018 à **0,039 écart-type**. Deux fois mieux, et
    toujours presque rien — à comparer aux 0,32 du bruit. Cette augmentation
    reste donc la plus faible des six, et si l'on veut un jour qu'elle compte,
    ce n'est pas le détecteur qu'il faudra retoucher mais la question de savoir
    si ces descripteurs-là voient la dynamique du tout.
    """
    seuil = float(rng.uniform(0.05, 0.25))
    rapport = float(rng.uniform(3.0, 12.0))
    attaque = float(rng.uniform(0.001, 0.020))
    relachement = float(rng.uniform(0.05, 0.40))
    coefficient_attaque = float(np.exp(-1.0 / max(1.0, attaque * sample_rate)))
    coefficient_relachement = float(np.exp(-1.0 / max(1.0, relachement * sample_rate)))

    amplitude = np.abs(audio.astype(np.float64))
    enveloppe = np.empty_like(amplitude)
    suivi = 0.0
    for i, valeur in enumerate(amplitude):
        coefficient = coefficient_attaque if valeur > suivi else coefficient_relachement
        suivi = coefficient * suivi + (1.0 - coefficient) * valeur
        enveloppe[i] = suivi

    au_dessus = np.maximum(enveloppe - seuil, 0.0)
    cible = seuil + au_dessus / rapport
    gain = np.divide(cible, np.maximum(enveloppe, 1e-9),
                     out=np.ones_like(enveloppe), where=enveloppe > seuil)
    comprime = audio.astype(np.float64) * gain

    rms_avant = float(np.sqrt(np.mean(audio.astype(np.float64) ** 2))) or 1e-9
    rms_apres = float(np.sqrt(np.mean(comprime ** 2))) or 1e-9
    return (comprime * (rms_avant / rms_apres)).astype(np.float32)


def _bruit(audio: np.ndarray, sample_rate: int, rng: np.random.Generator) -> np.ndarray:
    """Souffle, à un rapport signal/bruit tiré entre 20 et 45 dB."""
    del sample_rate
    rapport_db = float(rng.uniform(20.0, 45.0))
    rms = float(np.sqrt(np.mean(audio.astype(np.float64) ** 2))) or 1e-9
    niveau = rms * (10.0 ** (-rapport_db / 20.0))
    return (audio + rng.standard_normal(audio.size).astype(np.float32) * niveau).astype(np.float32)


def _desaccord(audio: np.ndarray, sample_rate: int, rng: np.random.Generator) -> np.ndarray:
    """Quelques cents d'écart, par rééchantillonnage linéaire. Une banque, une
    bande, un instrument réel : rien n'est jamais exactement au diapason."""
    del sample_rate
    cents = float(rng.uniform(-25.0, 25.0))
    facteur = 2.0 ** (cents / 1200.0)
    positions = np.arange(audio.size, dtype=np.float64) * facteur
    positions = positions[positions < audio.size - 1]
    sortie = np.interp(positions, np.arange(audio.size), audio).astype(np.float32)
    if sortie.size < audio.size:
        sortie = np.pad(sortie, (0, audio.size - sortie.size))
    return sortie[:audio.size]


AUGMENTATIONS: Tuple[Augmentation, ...] = (
    Augmentation("egaliseur", "un mastering incline le spectre ; le corpus est plat", _egaliseur),
    Augmentation("reverberation", "un stem a traversé une pièce ; un rendu moteur non", _reverberation),
    Augmentation("compression", "la dynamique d'un disque est écrasée, celle d'un rendu non", _compression),
    Augmentation("bruit", "souffle de bande, artefacts de séparation, fond de salle", _bruit),
    Augmentation("desaccord", "rien n'est exactement au diapason, ni une bande ni un piano", _desaccord),
)

AUGMENTATIONS_PAR_NOM: Dict[str, Augmentation] = {a.nom: a for a in AUGMENTATIONS}

# La FUITE est traitée à part : elle a besoin d'un autre son, et non d'une
# fonction pure de celui-ci. C'est la dégradation la plus caractéristique d'un
# stem séparé, et celle que le § 7 nomme en premier.
NOM_FUITE = "fuite"


def applique_fuite(audio: np.ndarray, fuite: np.ndarray, rng: np.random.Generator) -> np.ndarray:
    """Mélange le rendu d'une AUTRE machine à bas niveau : ce que demucs laisse
    passer.

    CE QUI COMPTE EST QUE LE SON MÊLÉ SOIT VRAIMENT AUTRE, et la mesure a
    corrigé au passage l'explication qu'on en donnait. La première version
    mélangeait le rendu PRÉCÉDENT — c'est-à-dire, dans la boucle des notes, une
    note voisine du MÊME patch. Ce n'était pas une fuite, c'était un écho, et
    son déplacement du descripteur valait **0,054 écart-type**, quasiment rien.

    On a d'abord cru que le remède était « prendre une autre MACHINE ». Mesuré :
    une autre machine déplace de **0,148 σ**… et un autre PATCH de la même
    machine de **0,152 σ**. La cause n'était donc pas l'identité de la machine
    mais la PROXIMITÉ du son mêlé. Prendre une autre machine reste le bon choix
    — c'est ce qu'une séparation laisse réellement passer — mais il fallait
    écrire la vraie raison plutôt que celle qui semblait évidente.

    Le niveau est tiré entre −30 et −14 dB, ce qui couvre ce qu'on observe sur
    les stems réels — assez pour salir les descripteurs, pas assez pour que
    l'étiquette devienne fausse.
    """
    if fuite.size == 0:
        return audio
    niveau_db = float(rng.uniform(-30.0, -14.0))
    gain = 10.0 ** (niveau_db / 20.0)
    rms_source = float(np.sqrt(np.mean(audio.astype(np.float64) ** 2))) or 1e-9
    rms_fuite = float(np.sqrt(np.mean(fuite.astype(np.float64) ** 2))) or 1e-9
    aligne = fuite[:audio.size]
    if aligne.size < audio.size:
        aligne = np.pad(aligne, (0, audio.size - aligne.size))
    return (audio + aligne * (gain * rms_source / rms_fuite)).astype(np.float32)


# ---------------------------------------------------------------------------
# Grille de notes
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class GrilleDeNotes:
    """Ce qui est joué pour CHAQUE patch.

    Le § 3 exige au moins trois hauteurs, deux durées et deux vélocités. Les
    valeurs par défaut couvrent ce que la chaîne rencontre : `_representative_note`
    rend des extraits de 0,4 à 1,5 s, et les vélocités transcrites vont
    rarement au-delà de 61-116 (mesuré sur Clair de Lune).
    """
    hauteurs: Tuple[int, ...] = (40, 52, 64, 76)
    durees: Tuple[float, ...] = (0.5, 1.2)
    velocites: Tuple[int, ...] = (60, 105)
    gate: float = 0.75

    def rendus_par_patch(self) -> int:
        return len(self.hauteurs) * len(self.durees) * len(self.velocites)

    def points(self) -> List[Tuple[int, float, int]]:
        return [(hauteur, duree, velocite)
                for hauteur in self.hauteurs
                for duree in self.durees
                for velocite in self.velocites]


# ---------------------------------------------------------------------------
# Génération
# ---------------------------------------------------------------------------

@dataclass
class LotDeCorpus:
    """Un lot AUTONOME : il se relit seul, et une génération interrompue en
    conserve tout ce qui a déjà été écrit. Le § 3 l'exige (« interruptible et
    reprenable »), et la raison est prosaïque : un corpus complet prend des
    heures, et personne ne relance des heures pour une coupure de courant."""
    machine: str
    X: np.ndarray                     # descripteurs
    Y: np.ndarray                     # vecteurs de paramètres normalisés
    conditions: np.ndarray            # (note, durée, vélocité) par exemple
    augmentations: List[str]          # nom appliqué à chaque exemple, "" si sec
    # NUMÉRO DE PATCH, unique dans tout le corpus d'une machine. Sans lui, une
    # validation croisée découperait au hasard et laisserait les seize notes
    # d'un même patch de part et d'autre de la coupure : le modèle verrait à
    # l'entraînement exactement le son qu'on lui demande de reconnaître à
    # l'épreuve, et son score serait faux vers le haut sans que rien ne le
    # montre. On découpe PAR PATCH, donc il faut le savoir.
    patchs: np.ndarray = field(default_factory=lambda: np.zeros(0, dtype=np.int32))
    # POURQUOI DES EXEMPLES MANQUENT, quand il en manque. Un lot de 300 patchs
    # × 16 notes devrait rendre 4 800 exemples ; s'il en rend 4 758, il faut
    # pouvoir dire lesquels sont tombés et pourquoi, sans avoir à le deviner.
    # C'est la règle « rien de silencieux » (§ 8.3) appliquée aux données.
    rejets: Dict[str, int] = field(default_factory=dict)
    seconds: float = 0.0

    def enregistre(self, chemin: Path) -> None:
        np.savez_compressed(
            chemin, machine=self.machine, X=self.X, Y=self.Y,
            conditions=self.conditions, patchs=self.patchs,
            augmentations=np.asarray(self.augmentations, dtype=object),
            rejets=np.asarray(json.dumps(self.rejets, ensure_ascii=False)),
            seconds=self.seconds)

    @staticmethod
    def relit(chemin: Path) -> "LotDeCorpus":
        donnees = np.load(chemin, allow_pickle=True)
        return LotDeCorpus(
            machine=str(donnees["machine"]), X=donnees["X"], Y=donnees["Y"],
            conditions=donnees["conditions"],
            patchs=donnees["patchs"] if "patchs" in donnees.files
                   else np.zeros(len(donnees["X"]), dtype=np.int32),
            augmentations=list(donnees["augmentations"]),
            rejets=(json.loads(str(donnees["rejets"])) if "rejets" in donnees.files else {}),
            seconds=float(donnees["seconds"]))


def graine_de_machine(machine: str) -> int:
    """Graine STABLE tirée du nom de la machine.

    `hash()` ne convient pas : Python randomise le hachage des chaînes à chaque
    démarrage (PYTHONHASHSEED), donc deux générations avec la même graine
    déclarée tireraient des patchs différents. Un corpus qu'on croit
    regénérable et qui ne l'est pas est pire qu'un corpus non regénérable :
    toutes les mesures faites dessus deviennent incomparables sans que rien ne
    le signale.
    """
    return int.from_bytes(hashlib.sha256(machine.encode("utf-8")).digest()[:4], "little") % 100003


def machines_de_recherche(engine: VsmEngine) -> List[str]:
    """Machines pour lesquelles le moteur déclare un espace de recherche.

    C'est la bonne définition de « les machines du corpus » : une machine sans
    profil de recherche n'a pas d'espace à peupler, et l'inclure produirait des
    exemples tous identiques.
    """
    retenues = []
    for machine in engine.machines():
        try:
            if engine.search_profile(machine):
                retenues.append(machine)
        except VsmEngineError:
            continue
    return retenues


def genere_lot(
    machine: str,
    space: Sequence[SearchParameter],
    engine: VsmEngine,
    patchs: int,
    grille: GrilleDeNotes,
    graine: int,
    sample_rate: int = 44100,
    augmentations: Sequence[str] = (),
    proportion_augmentee: float = 0.5,
    fuite_precedente: Optional[np.ndarray] = None,
    vivier_de_fuite: Sequence[np.ndarray] = (),
    decalage_patch: int = 0,
    progression: Optional[Callable[[str], None]] = None,
) -> LotDeCorpus:
    """Un lot : `patchs` patchs × la grille de notes, plus les augmentations.

    `proportion_augmentee` : part des exemples qui reçoivent une dégradation.
    Elle n'est pas de 100 % — le modèle doit voir le son PROPRE aussi, sans quoi
    il apprendrait à reconnaître la dégradation autant que la machine.
    """
    # DEUX FLUX ALÉATOIRES SÉPARÉS, et c'est indispensable à l'A/B du § 7.
    #
    # Avec un flux unique, le tirage des augmentations consomme des nombres, si
    # bien qu'un corpus SEC et un corpus AUGMENTÉ engendrés « à la même graine »
    # ne contiennent pas les mêmes patchs. Les comparer reviendrait alors à
    # mesurer deux choses à la fois — l'effet des augmentations ET celui d'un
    # échantillonnage différent de l'espace — sans pouvoir les démêler.
    #
    # Séparés, les patchs et les notes sont IDENTIQUES des deux côtés, et la
    # seule différence entre les deux corpus est celle qu'on veut mesurer.
    rng = np.random.default_rng(graine)
    rng_augmentation = np.random.default_rng(graine + 1_000_003)
    choisies = [AUGMENTATIONS_PAR_NOM[nom] for nom in augmentations
                if nom in AUGMENTATIONS_PAR_NOM]
    fuite_active = NOM_FUITE in augmentations

    X: List[np.ndarray] = []
    Y: List[np.ndarray] = []
    conditions: List[Tuple[float, float, float]] = []
    numeros: List[int] = []
    noms: List[str] = []
    rejets: Dict[str, int] = {}
    dernier_rendu = fuite_precedente
    depart = time.perf_counter()

    for index in range(patchs):
        vecteur = rng.random(len(space))
        parametres = _vector_to_parameters(space, vecteur)
        for hauteur, duree, velocite in grille.points():
            try:
                audio = engine.render_note(machine, parametres, midi_note=hauteur,
                                            duration=duree, velocity=velocite,
                                            gate=grille.gate, sample_rate=sample_rate)
            except VsmEngineError as erreur:
                # Une requête refusée ne doit pas interrompre des heures de
                # génération. L'exemple est absent, et la RAISON est comptée :
                # un corpus amputé sans explication ne se distingue pas d'un
                # corpus complet plus petit.
                rejets["moteur : " + str(erreur)[:60]] = rejets.get(
                    "moteur : " + str(erreur)[:60], 0) + 1
                continue
            except Exception as erreur:  # noqa: BLE001 — une génération dure des heures
                rejets[f"exception {type(erreur).__name__}"] = rejets.get(
                    f"exception {type(erreur).__name__}", 0) + 1
                continue
            if audio.size == 0:
                rejets["rendu vide"] = rejets.get("rendu vide", 0) + 1
                continue
            if not np.isfinite(audio).all():
                rejets["rendu non fini (NaN ou inf)"] = rejets.get("rendu non fini (NaN ou inf)", 0) + 1
                continue
            if float(np.sqrt(np.mean(audio.astype(np.float64) ** 2))) < 1e-4:
                # Inaudible : ses descripteurs décrivent du bruit numérique, pas
                # le patch. L'apprendre associerait un vecteur de paramètres à
                # des grandeurs qui n'en disent rien.
                rejets["inaudible (RMS < 1e-4)"] = rejets.get("inaudible (RMS < 1e-4)", 0) + 1
                continue

            nom_augmentation = ""
            travail = audio
            # Les deux tirages sont TOUJOURS consommés, même sans augmentation :
            # c'est ce qui garde les deux flux alignés d'un corpus à l'autre.
            doit_degrader = float(rng_augmentation.random()) < proportion_augmentee
            choix = float(rng_augmentation.random())
            if (choisies or fuite_active) and doit_degrader:
                possibles = list(choisies)
                # La fuite n'est proposée QUE si l'on dispose du son d'une autre
                # machine. À défaut, on ne la remplace pas par un écho du rendu
                # précédent : une augmentation qui ne dégrade pas ce qu'elle
                # prétend dégrader vaut moins que son absence, parce qu'elle
                # occupe la place dans le tirage ET dans le compte.
                if fuite_active and vivier_de_fuite:
                    possibles.append(None)  # marqueur de la fuite
                if possibles:
                    tirage = possibles[min(int(choix * len(possibles)), len(possibles) - 1)]
                    if tirage is None:
                        intrus = vivier_de_fuite[int(choix * len(vivier_de_fuite))
                                                 % len(vivier_de_fuite)]
                        travail = applique_fuite(audio, intrus, rng_augmentation)
                        nom_augmentation = NOM_FUITE
                    else:
                        travail = tirage.applique(audio, sample_rate, rng_augmentation)
                        nom_augmentation = tirage.nom

            X.append(descriptors(travail, sample_rate, hauteur, grille.gate, duree))
            Y.append(vecteur)
            conditions.append((float(hauteur), float(duree), float(velocite)))
            numeros.append(decalage_patch + index)
            noms.append(nom_augmentation)
            dernier_rendu = audio

        if progression is not None and (index + 1) % 25 == 0:
            progression(f"{machine} : {index + 1}/{patchs} patchs, {len(X)} exemples, "
                        f"{time.perf_counter() - depart:.0f} s")

    return LotDeCorpus(machine=machine,
                       X=np.asarray(X, dtype=np.float32),
                       Y=np.asarray(Y, dtype=np.float32),
                       conditions=np.asarray(conditions, dtype=np.float32),
                       patchs=np.asarray(numeros, dtype=np.int32),
                       augmentations=noms,
                       rejets=rejets,
                       seconds=time.perf_counter() - depart)


# ---------------------------------------------------------------------------
# Manifeste et péremption
# ---------------------------------------------------------------------------

def _commit_du_depot() -> str:
    try:
        sortie = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True,
                                text=True, timeout=5, check=True,
                                cwd=str(Path(__file__).resolve().parents[2]))
        return sortie.stdout.strip()
    except Exception:
        return ""


def _versions() -> Dict[str, str]:
    versions = {"python": platform.python_version(), "numpy": np.__version__}
    for nom in ("scipy", "sklearn"):
        try:
            module = __import__(nom)
            versions[nom] = getattr(module, "__version__", "?")
        except ImportError:
            versions[nom] = "absent"
    return versions


@dataclass
class Manifeste:
    """Ce qui permet de dire, six mois plus tard, de quoi ce corpus est fait —
    et surtout s'il est encore valable."""
    date: str
    commit: str
    sample_rate: int
    graine: int
    patchs_par_machine: int
    grille: Dict[str, object]
    augmentations: List[str]
    versions: Dict[str, str]
    empreintes: Dict[str, str] = field(default_factory=dict)
    exemples: Dict[str, int] = field(default_factory=dict)
    secondes: Dict[str, float] = field(default_factory=dict)

    def en_json(self) -> Dict[str, object]:
        return {"format": FORMAT_MANIFESTE, "version": VERSION_MANIFESTE,
                "date": self.date, "commit": self.commit,
                "sampleRate": self.sample_rate, "graine": self.graine,
                "patchsParMachine": self.patchs_par_machine, "grille": self.grille,
                "augmentations": self.augmentations, "versions": self.versions,
                "empreintes": self.empreintes, "exemples": self.exemples,
                "secondes": self.secondes}

    def enregistre(self, chemin: Path) -> None:
        chemin.write_text(json.dumps(self.en_json(), indent=2, ensure_ascii=False) + "\n",
                          encoding="utf-8")

    @staticmethod
    def relit(chemin: Path) -> "Manifeste":
        donnees = json.loads(Path(chemin).read_text(encoding="utf-8"))
        if donnees.get("format") != FORMAT_MANIFESTE:
            raise ValueError(f"manifeste de format inattendu : {donnees.get('format')!r}")
        if donnees.get("version") != VERSION_MANIFESTE:
            raise ValueError(f"manifeste de version {donnees.get('version')} non prise en charge")
        return Manifeste(
            date=donnees["date"], commit=donnees.get("commit", ""),
            sample_rate=int(donnees["sampleRate"]), graine=int(donnees["graine"]),
            patchs_par_machine=int(donnees["patchsParMachine"]),
            grille=donnees.get("grille", {}), augmentations=donnees.get("augmentations", []),
            versions=donnees.get("versions", {}), empreintes=donnees.get("empreintes", {}),
            exemples=donnees.get("exemples", {}), secondes=donnees.get("secondes", {}))


def nouveau_manifeste(sample_rate: int, graine: int, patchs: int, grille: GrilleDeNotes,
                      augmentations: Sequence[str], horodatage: str) -> Manifeste:
    return Manifeste(
        date=horodatage, commit=_commit_du_depot(), sample_rate=sample_rate,
        graine=graine, patchs_par_machine=patchs,
        grille={"hauteurs": list(grille.hauteurs), "durees": list(grille.durees),
                "velocites": list(grille.velocites), "gate": grille.gate},
        augmentations=list(augmentations), versions=_versions())


@dataclass(frozen=True)
class Peremption:
    """Verdict de fraîcheur d'un corpus, machine par machine."""
    perimees: Tuple[str, ...]
    invérifiables: Tuple[str, ...]

    @property
    def frais(self) -> bool:
        return not self.perimees and not self.invérifiables

    def resume(self) -> str:
        if self.frais:
            return "corpus à jour"
        morceaux = []
        if self.perimees:
            morceaux.append("périmé pour " + ", ".join(self.perimees)
                            + " (leur son a changé depuis la génération)")
        if self.invérifiables:
            morceaux.append("invérifiable pour " + ", ".join(self.invérifiables)
                            + " (le moteur ne sait plus les faire jouer)")
        return " ; ".join(morceaux)


def verifie_fraicheur(manifeste: Manifeste, engine: VsmEngine) -> Peremption:
    """Rejoue l'empreinte de chaque machine et la compare au manifeste.

    C'EST LA PIÈCE QUI EMPÊCHE UN MODÈLE DE MENTIR. Un modèle entraîné sur le
    son d'hier appliqué au son d'aujourd'hui ne produit pas d'erreur : il
    produit des verdicts plausibles et faux, ce qui est le pire des échecs pour
    un outil de reconstruction. La péremption doit donc être VÉRIFIÉE, pas
    supposée, et un corpus périmé fait REFUSER le modèle qui en dérive.
    """
    perimees, invérifiables = [], []
    for machine, attendue in sorted(manifeste.empreintes.items()):
        obtenue = machine_fingerprint(engine, machine, manifeste.sample_rate)
        if not obtenue:
            invérifiables.append(machine)
        elif obtenue != attendue:
            perimees.append(machine)
    return Peremption(tuple(perimees), tuple(invérifiables))
