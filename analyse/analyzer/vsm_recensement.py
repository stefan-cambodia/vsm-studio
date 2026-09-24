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
from typing import Any, Dict, List, Optional, Sequence, Tuple

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
# Regroupement
# ---------------------------------------------------------------------------

def taille_minimale(n_segments: int) -> int:
    return max(HDBSCAN_TAILLE_MIN, math.ceil(HDBSCAN_PART_MIN * n_segments))


def grouper(x: np.ndarray, segmentation: Segmentation) -> Regroupement:
    """HDBSCAN aux réglages figés ; une grappe COMPTE si elle cumule ≥ 4 s."""
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
                         min_samples=HDBSCAN_MIN_SAMPLES).fit_predict(x)
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
                  classifieur: Any, sous_seuil: bool = False) -> StemRecense:
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
    x = centrer(descripteurs, classifieur.moyenne, classifieur.echelle)
    regroupement = grouper(x, segmentation)
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
