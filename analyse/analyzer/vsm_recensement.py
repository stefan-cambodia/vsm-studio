"""Le RECENSEMENT DES SOURCES : combien de parties, lesquelles, jouées par quoi (H37).

Cahier des charges : `docs/CDC-recensement-des-sources.md`. L'hypothèse et ses
quatorze attendus y ont été commités AVANT la première ligne de ce module
(commit `a110fd8`) ; ce qui suit les met en œuvre et ne les discute pas.

CE QUE LE MODULE FAIT. Il reçoit des stems (ou un mélange) et PUBLIE un
recensement : des grappes de segments au timbre voisin, chacune avec un rôle,
une activité et un classement de machines. Il ne coupe, ne fusionne, ne crée
aucune piste — la parité reste la décision de `--parite` (§ 1 du CDC).

LES RÉGLAGES SONT CEUX DU § 0.4, figés avant la mesure. Ils vivent ici en
constantes NOMMÉES, et un test vérifie qu'ils valent ce que le document écrit :
un seuil ne bouge pas sans que le test et le document bougent avec lui.

TROIS LECTURES du § 0.4 que le texte laissait ouvertes, tranchées en écrivant
ce module (24/09/2026, avant toute mesure) :

1. « Segments bornés à [0,10 s ; 1,00 s] » : un segment va d'une attaque à la
   suivante, tronqué à 1,00 s ; un reste plus court que 0,10 s n'est PAS un
   segment — il est compté (`trop_courts`), jamais fusionné en douce. Entre une
   attaque et la suivante, ce qui dépasse 1 s est découpé en fenêtres d'1 s
   (la nappe tenue du CDC), la dernière gardée si elle fait 0,10 s au moins.
2. Un stem SOUS le seuil de la chaîne (0,5 % de l'énergie des stems) est
   recensé et publié, mais n'entre PAS dans le compte K — c'est la règle de la
   chaîne elle-même, et le recensement se compare à la parité à règles égales.
3. Les conditions de jeu du descripteur complet (N3) sont celles du corpus A6 :
   gate 0,75, et la durée de la grille (0,5 ou 1,2 s) la plus proche de la durée
   médiane des segments de la grappe ; la note est la hauteur médiane des notes
   transcrites de la grappe, 60 s'il n'y en a pas.
"""

from __future__ import annotations

import math
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List, Mapping, Optional, Sequence, Tuple

import numpy as np

FORMAT = "vsm-recensement"
VERSION = 1

# --- § 0.4, approche A-grp -------------------------------------------------
SEGMENT_MIN_S = 0.10
SEGMENT_MAX_S = 1.00
SILENCE_DBFS = -50.0
HDBSCAN_PART_MIN = 0.02        # min_cluster_size = max(5, ⌈2 % des segments⌉)
HDBSCAN_TAILLE_MIN = 5
HDBSCAN_MIN_SAMPLES = 3
DUREE_MIN_GRAPPE_S = 4.0
# H39 (§ 12 du CDC) : autoriser HDBSCAN à rendre UNE grappe. Faux = H37 à
# l'octet ; posé par l'option `--hdbscan-une-grappe` du banc, jamais édité.
HDBSCAN_UNE_GRAPPE = False
# H40 (§ 13 du CDC) : choisir, par stem, entre H37 et H39 selon la SIMULTANÉITÉ
# des notes. Faux = inactif ; posé par `--regroupement-par-simultaneite`.
REGROUPEMENT_SIMULTANEITE = False
SEUIL_SIMULTANEITE = 0.5        # fixé AVANT la mesure (§ 13)
NOTES_MIN_TEMOIN = 3            # une grappe de moins de 3 notes ne témoigne de rien
DIMENSIONS_DE_TIMBRE = 40      # les 43 descripteurs A6, moins note, gate, durée
# --- § 0.4, approche B -------------------------------------------------------
PART_PERCUSSIVE = 0.6
# --- § 0.4, approche A-voix --------------------------------------------------
TRAME_VOIX_S = 0.05
CENTILE_VOIX = 90
# --- § 0.4, rôles (règles, jamais apprises) --------------------------------
NAPPE_DUREE_S = 1.0
NAPPE_POLYPHONIE = 2.0
PIANO_POLYPHONIE = 2.0
PIANO_AMBITUS = 24
BASSE_HAUTEUR = 48
MONO_POLYPHONIE = 1.5
LEAD_HAUTEUR = 60
# --- § 0.3 : étiquette d'un segment -----------------------------------------
PART_DOMINANTE = 0.60
# --- le seuil de stem de la chaîne ------------------------------------------
SEUIL_STEM = 0.005
# --- conditions de jeu du corpus A6 (vsm_corpus_build.GrilleCorpus) --------
GATE_CORPUS = 0.75
DUREES_CORPUS = (0.5, 1.2)

ROLES_MELODIQUES_N1 = ("lead", "nappe", "accompagnement", "piano")

# Familles du kit de la chaîne → pièce N1 (§ 0.3).
PIECE_N1 = {"kick": "grosse caisse", "kick2": "grosse caisse",
            "snare": "caisse claire", "snare2": "caisse claire", "clap": "caisse claire",
            "hihat": "charleston", "openhat": "charleston", "pedalhat": "charleston",
            "tom": "toms", "cymbal": "cymbales", "percussion": "autre"}


@dataclass
class Segment:
    debut: float
    fin: float

    @property
    def duree(self) -> float:
        return self.fin - self.debut


@dataclass
class Segmentation:
    segments: List[Segment]
    silencieux: int = 0
    trop_courts: int = 0


@dataclass
class Grappe:
    ident: int
    segments: List[int]                     # indices dans la segmentation
    duree: float
    role: str = "autre"
    notes: Dict[str, Any] = field(default_factory=dict)
    machines: List[Tuple[str, float]] = field(default_factory=list)
    abstention: str = ""
    distance_au_corpus: Optional[float] = None
    activite: List[Tuple[float, float]] = field(default_factory=list)


@dataclass
class Regroupement:
    etiquettes: np.ndarray                  # une par segment, -1 = bruit
    grappes: List[Grappe]                   # celles qui COMPTENT (≥ 4 s)
    courtes: List[Tuple[int, float]]        # (id, durée) des grappes écartées
    bruit: int
    # H40 (§ 13 du CDC) : la règle retenue pour ce stem, et la plus forte
    # simultanéité entre deux grappes de H37 -- vide hors de H40.
    choix: str = ""
    simultaneite: Optional[float] = None


# ---------------------------------------------------------------------------
# Segmentation
# ---------------------------------------------------------------------------

def _dbfs(bloc: np.ndarray) -> float:
    rms = float(np.sqrt(np.mean(np.square(bloc, dtype=np.float64)))) if bloc.size else 0.0
    return 20.0 * math.log10(rms) if rms > 0.0 else -math.inf


def decouper(debuts: Sequence[float], duree_totale: float) -> Tuple[List[Segment], int]:
    """Les segments entre attaques, bornés (lecture 1 de l'en-tête).

    Rend (segments, trop_courts). Fonction pure, testée sans audio.
    """
    bornes = sorted(float(d) for d in debuts if 0.0 <= float(d) < duree_totale)
    segments: List[Segment] = []
    trop_courts = 0
    for i, debut in enumerate(bornes):
        fin_libre = bornes[i + 1] if i + 1 < len(bornes) else duree_totale
        t = debut
        while fin_libre - t > 1e-9:
            fin = min(t + SEGMENT_MAX_S, fin_libre)
            if fin - t >= SEGMENT_MIN_S - 1e-9:
                segments.append(Segment(t, fin))
            else:
                trop_courts += 1
            t = fin
    return segments, trop_courts


def segmenter(audio: np.ndarray, sample_rate: int) -> Segmentation:
    """Attaques (librosa, retour à l'attaque), segments bornés, silences écartés."""
    import librosa

    duree = audio.size / float(sample_rate)
    if audio.size < int(SEGMENT_MIN_S * sample_rate):
        return Segmentation([])
    debuts = librosa.onset.onset_detect(y=audio.astype(np.float32), sr=sample_rate,
                                        backtrack=True, units="time")
    # Le début du signal est une attaque de fait : sans lui, une nappe qui
    # sonne dès 0 s n'aurait aucun segment avant la première attaque détectée.
    debuts = np.concatenate([[0.0], np.asarray(debuts, dtype=np.float64)])
    debuts = np.unique(np.round(debuts, 6))
    bruts, trop_courts = decouper(debuts.tolist(), duree)
    gardes: List[Segment] = []
    silencieux = 0
    for s in bruts:
        bloc = audio[int(s.debut * sample_rate):int(s.fin * sample_rate)]
        if _dbfs(bloc) < SILENCE_DBFS:
            silencieux += 1
        else:
            gardes.append(s)
    return Segmentation(gardes, silencieux=silencieux, trop_courts=trop_courts)


# ---------------------------------------------------------------------------
# Descripteurs
# ---------------------------------------------------------------------------

def descripteurs_complets(audio: np.ndarray, sample_rate: int,
                          segmentation: Segmentation) -> np.ndarray:
    """Les 43 descripteurs A6 de chaque segment (note 60, gate et durée du corpus).

    Les trois dernières colonnes sont des conditions de jeu FACTICES ici : le
    regroupement ne les lit pas (il ne prend que les 40 premières), et
    l'identification les remplace grappe par grappe (lecture 3 de l'en-tête).
    """
    from analyzer.vsm_corpus import descriptors

    lignes = []
    for s in segmentation.segments:
        bloc = audio[int(s.debut * sample_rate):int(s.fin * sample_rate)].astype(np.float32)
        lignes.append(descriptors(bloc, sample_rate, 60, GATE_CORPUS, DUREES_CORPUS[0]))
    if not lignes:
        return np.zeros((0, 43))
    return np.vstack(lignes).astype(np.float64)


def centrer(descripteurs: np.ndarray, moyenne: np.ndarray, echelle: np.ndarray) -> np.ndarray:
    """Les 40 dimensions de timbre, centrées-réduites par le corpus A6."""
    d = DIMENSIONS_DE_TIMBRE
    if descripteurs.size == 0:
        return np.zeros((0, d))
    echelle_sure = np.where(np.asarray(echelle[:d]) > 0, echelle[:d], 1.0)
    x = (descripteurs[:, :d] - np.asarray(moyenne[:d])) / echelle_sure
    return np.nan_to_num(x, nan=0.0, posinf=0.0, neginf=0.0)


# ---------------------------------------------------------------------------
# H38 : un embedding pré-entraîné À LA PLACE des 40 grandeurs A6
# (§ 11 du CDC). Une seule variable : ce qui décrit un segment. `a6` reste le
# défaut, et son chemin ne change pas d'un octet.
# ---------------------------------------------------------------------------

EMBEDDINGS = {
    "clap": ("laion/clap-htsat-unfused", "8fa0f1c6d0433df6e97c127f64b2a1d6c0dcda8a", 48000),
    "ast": ("MIT/ast-finetuned-audioset-10-10-0.4593", "f826b80d28226b62986cc218e5cec390b1096902", 16000),
}
_MODELES: Dict[str, Any] = {}


def _sans_torchvision() -> None:
    """`transformers` importe `torchvision` dès qu'il le croit présent — et le
    `torchvision` 0.28 installé dans `analyse/.venv` ne s'importe pas avec le
    torch 2.13 du poste (« operator torchvision::nms does not exist », constaté
    le 24/09). Rien dans le dépôt ne s'en sert, et les deux modèles de H38 ne
    touchent pas l'image : on le DÉCLARE absent à `transformers`, sans rien
    désinstaller de l'environnement de la chaîne."""
    import transformers.utils as tu
    import transformers.utils.import_utils as iu

    def absent() -> bool:
        return False

    for module in (iu, tu):
        if hasattr(module, "is_torchvision_available"):
            module.is_torchvision_available = absent  # type: ignore[attr-defined]


def _modele(nom: str):
    """Le modèle et son processeur, chargés UNE fois, révision épinglée, sur CPU."""
    if nom not in _MODELES:
        import torch

        _sans_torchvision()

        depot, revision, _ = EMBEDDINGS[nom]
        modele: Any
        processeur: Any
        if nom == "clap":
            from transformers import ClapModel, ClapProcessor
            modele = ClapModel.from_pretrained(depot, revision=revision)
            processeur = ClapProcessor.from_pretrained(depot, revision=revision)
        else:
            from transformers import ASTFeatureExtractor, ASTModel
            modele = ASTModel.from_pretrained(depot, revision=revision)
            processeur = ASTFeatureExtractor.from_pretrained(depot, revision=revision)
        modele.eval()
        torch.set_grad_enabled(False)
        _MODELES[nom] = (modele, processeur)
    return _MODELES[nom]


def embeddings_de_segments(audio: np.ndarray, sample_rate: int, segmentation: Segmentation,
                           nom: str, lot: int = 16) -> np.ndarray:
    """Un embedding normé à 1 par segment (géométrie du cosinus, § 11)."""
    import librosa
    import torch

    if not segmentation.segments:
        return np.zeros((0, 1))
    modele, processeur = _modele(nom)
    sr_modele = EMBEDDINGS[nom][2]
    signal = librosa.resample(audio.astype(np.float32), orig_sr=sample_rate, target_sr=sr_modele)
    morceaux = [signal[int(s.debut * sr_modele):max(int(s.fin * sr_modele), int(s.debut * sr_modele) + 1)]
                for s in segmentation.segments]
    sorties = []
    for i in range(0, len(morceaux), lot):
        paquet = [m.astype(np.float32) for m in morceaux[i:i + lot]]
        if nom == "clap":
            entree = processeur(audio=paquet, sampling_rate=sr_modele, return_tensors="pt")
            vecteurs = modele.get_audio_features(**entree)
            if not isinstance(vecteurs, torch.Tensor):
                vecteurs = vecteurs.pooler_output
        else:
            entree = processeur(paquet, sampling_rate=sr_modele, return_tensors="pt")
            vecteurs = modele(**entree).pooler_output
        sorties.append(vecteurs.detach().cpu().numpy().astype(np.float64))
    x = np.vstack(sorties)
    normes = np.linalg.norm(x, axis=1, keepdims=True)
    return x / np.where(normes > 0, normes, 1.0)


# ---------------------------------------------------------------------------
# Regroupement
# ---------------------------------------------------------------------------

def taille_minimale(n_segments: int) -> int:
    return max(HDBSCAN_TAILLE_MIN, math.ceil(HDBSCAN_PART_MIN * n_segments))


def simultaneite(notes_a: Sequence[Any], notes_b: Sequence[Any]) -> Optional[float]:
    """H40 : la part des attaques d'une grappe qui tombent pendant qu'une note de
    l'autre sonne -- le plus grand des DEUX sens. None si l'une a moins de 3 notes.

    Les deux sens, et c'est une correction écrite AVANT la mesure (§ 13) : « les
    attaques de la plus petite grappe » donnait 0 pour une nappe de trois
    longues notes sous une mélodie, alors que c'est le cas même que la règle
    veut voir -- une note TENUE d'une grappe qui sonne quand l'autre attaque."""
    if len(notes_a) < NOTES_MIN_TEMOIN or len(notes_b) < NOTES_MIN_TEMOIN:
        return None

    def dans(attaques: Sequence[Any], tenues: Sequence[Any]) -> float:
        plages = sorted((float(n.start), float(n.start) + float(n.duration)) for n in tenues)
        debuts = np.array([p[0] for p in plages])
        fins_max = np.maximum.accumulate(np.array([p[1] for p in plages]))
        compte = 0
        for n in attaques:
            t = float(n.start)
            i = int(np.searchsorted(debuts, t, side="left")) - 1   # notes COMMENCÉES strictement avant
            if i >= 0 and fins_max[i] > t:
                compte += 1
        return compte / len(attaques)

    return max(dans(notes_a, notes_b), dans(notes_b, notes_a))


def grouper(x: np.ndarray, segmentation: Segmentation,
            notes: Optional[Sequence[Any]] = None) -> Regroupement:
    """HDBSCAN aux réglages figés ; une grappe COMPTE si elle cumule ≥ 4 s.

    H40 : avec `REGROUPEMENT_SIMULTANEITE` et des notes, les deux regroupements
    (sans et avec grappe unique) sont calculés, et la règle du § 13 choisit."""
    if REGROUPEMENT_SIMULTANEITE and notes is not None:
        sans = _grouper(x, segmentation, False)
        avec = _grouper(x, segmentation, True)
        if len(avec.grappes) >= len(sans.grappes):
            avec.choix = "grappe unique : rien de fusionné"
            return avec
        pire: Optional[float] = None
        par_grappe = [notes_de_la_grappe(notes, [segmentation.segments[i] for i in g.segments])
                      for g in sans.grappes]
        for a in range(len(par_grappe)):
            for b in range(a + 1, len(par_grappe)):
                v = simultaneite(par_grappe[a], par_grappe[b])
                if v is not None and (pire is None or v > pire):
                    pire = v
        if pire is not None and pire >= SEUIL_SIMULTANEITE:
            sans.choix, sans.simultaneite = "séparation gardée : sources simultanées", pire
            return sans
        avec.choix = ("fusion : aucune preuve de simultanéité" if pire is None
                      else "fusion : grappes non simultanées")
        avec.simultaneite = pire
        return avec
    return _grouper(x, segmentation, HDBSCAN_UNE_GRAPPE)


def _grouper(x: np.ndarray, segmentation: Segmentation, une_grappe: bool) -> Regroupement:
    n = x.shape[0]
    if n == 0:
        return Regroupement(np.zeros(0, dtype=int), [], [], 0)
    taille = taille_minimale(n)
    if n < taille:
        # Trop peu de segments pour qu'une grappe existe : TOUT est bruit, et
        # c'est dit par le compte de bruit, pas deviné.
        return Regroupement(np.full(n, -1, dtype=int), [], [], n)
    from sklearn.cluster import HDBSCAN

    etiquettes = HDBSCAN(min_cluster_size=taille, copy=True,
                         min_samples=HDBSCAN_MIN_SAMPLES,
                         allow_single_cluster=une_grappe).fit_predict(x)
    grappes: List[Grappe] = []
    courtes: List[Tuple[int, float]] = []
    for ident in sorted(set(int(e) for e in etiquettes if e >= 0)):
        indices = [i for i in range(n) if etiquettes[i] == ident]
        duree = float(sum(segmentation.segments[i].duree for i in indices))
        if duree >= DUREE_MIN_GRAPPE_S - 1e-9:
            g = Grappe(ident, indices, duree)
            g.activite = activite([segmentation.segments[i] for i in indices])
            grappes.append(g)
        else:
            courtes.append((ident, duree))
    return Regroupement(np.asarray(etiquettes, dtype=int), grappes, courtes,
                        int(np.sum(etiquettes < 0)))


def activite(segments: Sequence[Segment], trou: float = 2.0) -> List[Tuple[float, float]]:
    """Les plages où la grappe joue : segments recollés tant qu'aucun trou de 2 s."""
    plages: List[Tuple[float, float]] = []
    for s in sorted(segments, key=lambda s: s.debut):
        if plages and s.debut - plages[-1][1] <= trou:
            plages[-1] = (plages[-1][0], max(plages[-1][1], s.fin))
        else:
            plages.append((s.debut, s.fin))
    return [(round(a, 3), round(b, 3)) for a, b in plages]


# ---------------------------------------------------------------------------
# Rôles (§ 0.4 : des règles, pas un apprentissage)
# ---------------------------------------------------------------------------

def notes_de_la_grappe(notes: Sequence[Any], segments: Sequence[Segment]) -> List[Any]:
    """Les notes transcrites dont l'attaque tombe dans un segment de la grappe."""
    if not notes or not segments:
        return []
    bornes = sorted((s.debut, s.fin) for s in segments)
    debuts = np.array([b[0] for b in bornes])
    retenues = []
    for n in notes:
        i = int(np.searchsorted(debuts, float(n.start), side="right")) - 1
        if i >= 0 and float(n.start) < bornes[i][1]:
            retenues.append(n)
    return retenues


def polyphonie_moyenne(notes: Sequence[Any], trame: float = TRAME_VOIX_S) -> float:
    """Nombre moyen de notes simultanées, sur les trames où au moins une sonne."""
    if not notes:
        return 0.0
    fin = max(float(n.start) + float(n.duration) for n in notes)
    k = int(math.ceil(fin / trame)) + 1
    compte = np.zeros(k, dtype=np.int32)
    for n in notes:
        a = int(float(n.start) / trame)
        b = max(a + 1, int(math.ceil((float(n.start) + float(n.duration)) / trame)))
        compte[a:b] += 1
    actives = compte[compte > 0]
    return float(actives.mean()) if actives.size else 0.0


def resume_des_notes(notes: Sequence[Any]) -> Dict[str, Any]:
    if not notes:
        return {"n": 0}
    hauteurs = [int(n.note) for n in notes]
    return {"n": len(notes),
            "dureeMediane": round(float(np.median([float(n.duration) for n in notes])), 4),
            "polyphonie": round(polyphonie_moyenne(notes), 4),
            "hauteurMediane": float(np.median(hauteurs)),
            "ambitus": int(max(hauteurs) - min(hauteurs))}


def role_par_regles(stem: str, notes: Sequence[Any]) -> str:
    """Le rôle N1 d'une grappe, par les règles du § 0.4, dans l'ordre écrit."""
    if stem == "vocals":
        return "voix"
    if stem == "bass":
        return "basse"
    if not notes:
        return "autre"
    r = resume_des_notes(notes)
    d, p, h, a = r["dureeMediane"], r["polyphonie"], r["hauteurMediane"], r["ambitus"]
    if d >= NAPPE_DUREE_S and p >= NAPPE_POLYPHONIE:
        return "nappe"
    if p >= PIANO_POLYPHONIE and a >= PIANO_AMBITUS and _registres(notes) >= 2:
        return "piano"
    if h < BASSE_HAUTEUR and p < MONO_POLYPHONIE:
        return "basse"
    if p < MONO_POLYPHONIE and h >= LEAD_HAUTEUR:
        return "lead"
    return "accompagnement"


def _registres(notes: Sequence[Any]) -> int:
    from analyzer.vsm_reconstruct import registres_par_vides

    return len(registres_par_vides(list(notes)))


# ---------------------------------------------------------------------------
# A-voix : voix par registre, sur les notes
# ---------------------------------------------------------------------------

def voix_par_registre(notes: Sequence[Any]) -> Tuple[int, List[Dict[str, Any]]]:
    """K_mél d'un stem par les notes seules (§ 0.4, A-voix)."""
    if not notes:
        return 0, []
    from analyzer.vsm_reconstruct import registres_par_vides

    detail = []
    total = 0.0
    for registre in registres_par_vides(list(notes)):
        if not registre:
            continue
        fin = max(float(n.start) + float(n.duration) for n in registre)
        k = int(math.ceil(fin / TRAME_VOIX_S)) + 1
        compte = np.zeros(k, dtype=np.int32)
        for n in registre:
            a = int(float(n.start) / TRAME_VOIX_S)
            b = max(a + 1, int(math.ceil((float(n.start) + float(n.duration)) / TRAME_VOIX_S)))
            compte[a:b] += 1
        actives = compte[compte > 0]
        voix = float(np.percentile(actives, CENTILE_VOIX)) if actives.size else 0.0
        total += voix
        detail.append({"bas": int(min(n.note for n in registre)),
                       "haut": int(max(n.note for n in registre)), "voix": voix})
    return int(round(total)), detail


# ---------------------------------------------------------------------------
# Identification (N3)
# ---------------------------------------------------------------------------

def vecteur_de_grappe(descripteurs: np.ndarray, grappe: Grappe,
                      segmentation: Segmentation, notes: Sequence[Any]) -> np.ndarray:
    """Le descripteur complet d'une grappe (lecture 3 de l'en-tête)."""
    timbre = np.median(descripteurs[grappe.segments, :DIMENSIONS_DE_TIMBRE], axis=0)
    hauteur = float(np.median([int(n.note) for n in notes])) if notes else 60.0
    duree_med = float(np.median([segmentation.segments[i].duree for i in grappe.segments]))
    duree = min(DUREES_CORPUS, key=lambda d: abs(d - duree_med))
    return np.concatenate([timbre, [hauteur / 127.0, GATE_CORPUS, duree]])


def identifier(classifieur: Any, vecteur: np.ndarray, k: int = 5
               ) -> Tuple[List[Tuple[str, float]], str, float, np.ndarray]:
    """Classement de k machines, motif d'abstention, distance au corpus, probabilités."""
    classement, motif = classifieur.classe(vecteur, k=k)
    normalise = (np.atleast_2d(vecteur) - classifieur.moyenne) / classifieur.echelle
    distance = float(np.linalg.norm(classifieur.reference_nouveaute - normalise, axis=1).min())
    probas = classifieur.modele.predict_proba(normalise)[0]
    return classement, motif, distance, probas


def rang_de(classifieur: Any, probas: np.ndarray, machine: str) -> Optional[int]:
    """Rang (1 = premier) de `machine` ; None si le modèle ne la connaît pas."""
    if machine not in classifieur.noms:
        return None
    ordre = list(np.argsort(probas)[::-1])
    return ordre.index(classifieur.noms.index(machine)) + 1


# ---------------------------------------------------------------------------
# Un stem mélodique, de bout en bout
# ---------------------------------------------------------------------------

@dataclass
class StemRecense:
    stem: str
    sous_seuil: bool
    segmentation: Segmentation
    descripteurs: np.ndarray
    regroupement: Regroupement
    secondes: Dict[str, float]
    paliers: int = 0
    voix: int = 0
    voix_detail: List[Dict[str, Any]] = field(default_factory=list)


def recenser_stem(stem: str, audio: np.ndarray, sample_rate: int, notes: Sequence[Any],
                  classifieur: Any, sous_seuil: bool = False,
                  embedding: str = "a6") -> StemRecense:
    """Segmenter, décrire, grouper, nommer les rôles, identifier — un stem."""
    from analyzer.vsm_paliers import timbres_installes

    temps: Dict[str, float] = {}
    t = time.perf_counter()
    segmentation = segmenter(audio, sample_rate)
    temps["segmentation"] = time.perf_counter() - t
    t = time.perf_counter()
    descripteurs = descripteurs_complets(audio, sample_rate, segmentation)
    temps["descripteurs"] = time.perf_counter() - t
    t = time.perf_counter()
    if embedding == "a6":
        x = centrer(descripteurs, classifieur.moyenne, classifieur.echelle)
    else:
        x = embeddings_de_segments(audio, sample_rate, segmentation, embedding)
        temps["embedding"] = time.perf_counter() - t
        t = time.perf_counter()
    regroupement = grouper(x, segmentation, notes)
    temps["grappes"] = time.perf_counter() - t
    t = time.perf_counter()
    for g in regroupement.grappes:
        segs = [segmentation.segments[i] for i in g.segments]
        ses_notes = notes_de_la_grappe(notes, segs)
        g.role = role_par_regles(stem, ses_notes)
        g.notes = resume_des_notes(ses_notes)
        if classifieur is not None and descripteurs.size:
            vecteur = vecteur_de_grappe(descripteurs, g, segmentation, ses_notes)
            g.machines, g.abstention, g.distance_au_corpus, _ = identifier(classifieur, vecteur)
    temps["roles_identification"] = time.perf_counter() - t
    t = time.perf_counter()
    paliers, _ = timbres_installes(audio, sample_rate)
    voix, voix_detail = voix_par_registre(notes)
    temps["paliers_voix"] = time.perf_counter() - t
    return StemRecense(stem, sous_seuil, segmentation, descripteurs, regroupement, temps,
                       paliers=paliers, voix=voix, voix_detail=voix_detail)


def part_percussive_par_segment(audio: np.ndarray, sample_rate: int,
                                segmentation: Segmentation) -> np.ndarray:
    """Approche B : part percussive (HPSS, marges par défaut) de chaque segment."""
    import librosa

    harmonique, percussif = librosa.effects.hpss(audio.astype(np.float32))
    parts = []
    for s in segmentation.segments:
        a, b = int(s.debut * sample_rate), int(s.fin * sample_rate)
        eh = float(np.sum(np.square(harmonique[a:b], dtype=np.float64)))
        ep = float(np.sum(np.square(percussif[a:b], dtype=np.float64)))
        parts.append(ep / (eh + ep) if eh + ep > 0 else 0.0)
    return np.asarray(parts)


def bloc_de_stem(r: StemRecense) -> Dict[str, Any]:
    """La forme publiée d'un stem recensé (§ 3.1 du CDC)."""
    return {
        "stem": r.stem, "sousSeuil": r.sous_seuil,
        "segments": {"retenus": len(r.segmentation.segments),
                     "silencieux": r.segmentation.silencieux,
                     "tropCourts": r.segmentation.trop_courts,
                     "bruit": r.regroupement.bruit},
        # H40 : la règle retenue et la simultanéité qui l'a décidée (vides hors H40).
        **({"choixH40": r.regroupement.choix,
            "simultaneite": (round(r.regroupement.simultaneite, 3)
                             if r.regroupement.simultaneite is not None else None)}
           if r.regroupement.choix else {}),
        "grappes": [{"id": g.ident, "role": g.role, "segments": len(g.segments),
                     "dureeS": round(g.duree, 3), "activite": g.activite, "notes": g.notes,
                     "machines": [[m, round(s, 4)] for m, s in g.machines],
                     "distanceAuCorpus": (round(g.distance_au_corpus, 4)
                                          if g.distance_au_corpus is not None else None),
                     "abstention": g.abstention}
                    for g in r.regroupement.grappes],
        "grappesCourtes": [{"id": i, "dureeS": round(d, 3)} for i, d in r.regroupement.courtes],
        "paliers": r.paliers, "voixParRegistre": r.voix,
        "secondes": {k: round(v, 3) for k, v in r.secondes.items()},
    }


def reglages() -> Dict[str, Any]:
    """Les réglages figés, pour la provenance."""
    return {"segmentMinS": SEGMENT_MIN_S, "segmentMaxS": SEGMENT_MAX_S,
            "silenceDbfs": SILENCE_DBFS, "hdbscanPartMin": HDBSCAN_PART_MIN,
            "hdbscanTailleMin": HDBSCAN_TAILLE_MIN, "hdbscanMinSamples": HDBSCAN_MIN_SAMPLES,
            "dureeMinGrappeS": DUREE_MIN_GRAPPE_S, "dimensionsDeTimbre": DIMENSIONS_DE_TIMBRE,
            "hdbscanUneGrappe": HDBSCAN_UNE_GRAPPE,   # H39
            "regroupementSimultaneite": REGROUPEMENT_SIMULTANEITE,   # H40
            "seuilSimultaneite": SEUIL_SIMULTANEITE, "notesMinTemoin": NOTES_MIN_TEMOIN,
            "partPercussive": PART_PERCUSSIVE, "trameVoixS": TRAME_VOIX_S,
            "centileVoix": CENTILE_VOIX, "partDominante": PART_DOMINANTE,
            "seuilStem": SEUIL_STEM, "gateCorpus": GATE_CORPUS, "dureesCorpus": list(DUREES_CORPUS)}


def versions() -> Dict[str, str]:
    import importlib.metadata as md

    sortie = {"numpy": np.__version__}
    for paquet in ("scikit-learn", "librosa", "basic-pitch", "demucs", "torch"):
        try:
            sortie[paquet] = md.version(paquet)
        except md.PackageNotFoundError:
            sortie[paquet] = "absent"
    return sortie


# ---------------------------------------------------------------------------
# Un morceau entier : ce que la chaîne (--recensement) et le banc (--reel)
# appellent tous les deux — UN chemin, pour que les deux ne divergent pas.
# ---------------------------------------------------------------------------

def recenser_morceau(stems: Dict[str, np.ndarray], sample_rate: int,
                     notes_par_stem: Mapping[str, Sequence[Any]],
                     kit: Sequence[Tuple[str, int]], classifieur: Any,
                     classifieur_refuse: str = "", embedding: str = "a6") -> Dict[str, Any]:
    """Le bloc `recensement` du § 3.1 du CDC, pour des stems déjà en mémoire.

    `kit` : les (famille, frappes) que la chaîne retient pour la batterie.
    Un stem sous le seuil de la chaîne est recensé et publié, et n'entre pas au
    compte (lecture 2 de l'en-tête).
    """
    energie = {k: float(np.sum(np.square(v.astype(np.float64)))) for k, v in stems.items()}
    totale = sum(energie.values()) or 1.0
    blocs: List[Dict[str, Any]] = []
    k_grp = k_pal = k_voix = 0
    for stem, audio in stems.items():
        if stem == "drums":
            continue
        sous = energie[stem] / totale < SEUIL_STEM
        r = recenser_stem(stem, audio, sample_rate, notes_par_stem.get(stem, []), classifieur,
                          sous_seuil=sous, embedding=embedding)
        blocs.append(bloc_de_stem(r))
        if not sous:
            k_grp += len(r.regroupement.grappes)
            k_pal += r.paliers
            k_voix += r.voix
    drums_compte = "drums" in stems and energie["drums"] / totale >= SEUIL_STEM
    pieces = list(kit) if drums_compte else []
    k_bat = len(pieces)
    indecidable = ["cymbales : le kit de la chaîne ne les distingue pas des autres métaux"]
    if classifieur_refuse:
        indecidable.append(f"machines (N3) : {classifieur_refuse}")
    return {
        "format": FORMAT, "version": VERSION, "approche": "A-grp",
        "compte": {"K": k_grp + k_bat, "K_mel": k_grp, "K_bat": k_bat},
        "comptesParApproche": {"A-grp": {"K_mel": k_grp, "K_bat": k_bat},
                               "A-pal": {"K_mel": k_pal, "K_bat": k_bat},
                               "A-voix": {"K_mel": k_voix, "K_bat": k_bat}},
        "partEnergie": {k: round(v / totale, 5) for k, v in energie.items()},
        "stems": blocs,
        "batterie": [{"piece": f, "pieceN1": PIECE_N1.get(f, "autre"), "frappes": n} for f, n in pieces],
        "indecidable": indecidable,
        "provenance": {"reglages": reglages(), "versions": versions()},
    }


def resume_journal(bloc: Dict[str, Any], pistes_parite: Optional[int] = None) -> List[str]:
    """Les lignes du journal (§ 3.2) : une par stem, puis la synthèse."""
    lignes = []
    for s in bloc["stems"]:
        roles = ", ".join(g["role"] for g in s["grappes"]) or "aucune"
        lignes.append(f"recensement : {s['stem']} → {len(s['grappes'])} grappe(s) ({roles}), "
                      f"{s['segments']['bruit']} segments de bruit, "
                      f"{len(s['grappesCourtes'])} grappe(s) sous 4 s écartée(s)"
                      + (" — SOUS LE SEUIL, hors compte" if s["sousSeuil"] else ""))
    c = bloc["compte"]
    fin = f"recensement : K = {c['K']} ({c['K_mel']} mélodiques + {c['K_bat']} pièces)"
    if pistes_parite is not None:
        fin += f" — la parité a produit {pistes_parite} pistes"
    lignes.append(fin)
    return lignes
