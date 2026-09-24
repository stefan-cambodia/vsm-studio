#!/usr/bin/env python3
"""Le banc du RECENSEMENT DES SOURCES (H37, `docs/CDC-recensement-des-sources.md`).

Il ne fait AUCUN rendu et n'appelle pas `reconstruire.py` : il relit les stems
qu'un banc synthétique a déjà gardés, et fait tourner les approches du § 0.4 à
quatre niveaux d'ablation (§ 0.3) :

  L1  chaque partie vraie SEULE               → segmentation + embedding
  L2  les stems vrais sommés (bass/drums/other/vocals) → regroupement dans un mélange
  L3  les stems séparés par la chaîne         → séparation
  L4  le mélange seul (approche B)

    analyse/.venv/bin/python -u analyse/banc_recensement.py \\
        --lot reconstruction/travail/s1-sec --banc reconstruction/travail/s1-sec-banc \\
        --sortie reconstruction/travail/recensement-s1-sec

Pour un disque (pas de vérité) : `--reel morceau.wav --stems dossier --sortie …`
publie les comptes L3 et L4 seulement, sans les juger.

LES MÉTRIQUES SONT VÉRIFIÉES AVANT LA CIBLE (leçon de D266) : les fonctions
d'appariement, d'étiquetage et de score sont testées sur des cas construits à la
main dans `tests/test_recensement.py`.
"""

from __future__ import annotations

import argparse
import json
import math
import subprocess
import sys
import time
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

import numpy as np

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))

FORMAT_BANC = "vsm-banc-recensement"
VERSION_BANC = 1
SR = 44100
STEMS_SEPARES = ("bass", "drums", "other", "guitar", "piano", "vocals")
MELODIQUES_SEPARES = ("bass", "other", "guitar", "piano", "vocals")

# Rôle du banc → rôle N1 (§ 0.3 du CDC).
ROLE_N1 = {"basse": "basse", "melodie": "lead", "nappe": "nappe",
           "accompagnement": "accompagnement", "piano-deux-mains": "piano", "voix": "voix"}
# Pièce du banc → pièce N1.
PIECE_BANC_N1 = {"kick": "grosse caisse", "snare": "caisse claire", "clap": "caisse claire",
                 "hihat": "charleston", "openhat": "charleston", "tom": "toms"}
PIECES_N1 = ("grosse caisse", "caisse claire", "charleston", "toms", "cymbales")
MIXTE = -2
# La cible d'un stem séparé : ce qu'il devrait porter (§ 2.4.1 du banc).
TARGET = {"bass": "bass", "drums": "drums", "vocals": "vocals",
          "other": "other", "guitar": "other", "piano": "other"}


# ---------------------------------------------------------------------------
# Métriques — pures, testées à la main avant toute mesure
# ---------------------------------------------------------------------------

def etiquette_dominante(energies: np.ndarray, part: float) -> int:
    """Indice de la partie qui pèse ≥ `part` de l'énergie, sinon MIXTE."""
    total = float(np.sum(energies))
    if total <= 0.0:
        return MIXTE
    i = int(np.argmax(energies))
    return i if energies[i] / total >= part - 1e-12 else MIXTE


def apparier(grappes: Sequence[Sequence[int]], labels: Sequence[int], n_parties: int
             ) -> Tuple[Dict[int, int], np.ndarray]:
    """Hongrois sur le nombre de segments partagés.

    `grappes` : pour chaque grappe, les indices de ses segments (dans `labels`).
    Rend ({grappe → partie}, matrice). Une paire à 0 segment partagé n'est pas
    un appariement.
    """
    from scipy.optimize import linear_sum_assignment

    m = np.zeros((len(grappes), n_parties), dtype=np.int64)
    for g, segs in enumerate(grappes):
        for s in segs:
            p = labels[s]
            if 0 <= p < n_parties:
                m[g, p] += 1
    if m.size == 0:
        return {}, m
    lignes, colonnes = linear_sum_assignment(-m)
    return {int(g): int(p) for g, p in zip(lignes, colonnes, strict=True) if m[g, p] > 0}, m


def statuts(appariement: Dict[int, int], matrice: np.ndarray, n_parties: int) -> List[str]:
    """Pour chaque partie : « ok », « dedoublee » ou « fondue »."""
    sortie = ["fondue"] * n_parties
    for p in appariement.values():
        sortie[p] = "ok"
    for g in range(matrice.shape[0]):
        if g in appariement or matrice[g].sum() == 0:
            continue
        p = int(np.argmax(matrice[g]))
        if sortie[p] == "ok":
            sortie[p] = "dedoublee"
    return sortie


def ari_nmi(labels_vrais: Sequence[int], labels_predits: Sequence[int]
            ) -> Tuple[Optional[float], Optional[float], float]:
    """ARI et NMI sur les segments ÉTIQUETÉS (les mixtes exclus, leur part rendue).

    Le bruit d'HDBSCAN (-1) reste une étiquette : l'exclure gonflerait l'ARI.
    """
    from sklearn.metrics import adjusted_rand_score, normalized_mutual_info_score

    vrais = np.asarray(labels_vrais)
    predits = np.asarray(labels_predits)
    garde = vrais != MIXTE
    part_mixte = float(1.0 - garde.mean()) if vrais.size else 0.0
    # Une seule classe vraie : l'ARI vaut 1 par construction si tout tombe dans
    # une grappe, et ne mesure rien — « non défini », jamais 1,0 (vu au premier
    # essai, morceau 1 en L3 : ARI 1,0 sur 29 segments d'une même étiquette).
    if garde.sum() < 2 or len(set(vrais[garde].tolist())) < 2:
        return None, None, part_mixte
    return (float(adjusted_rand_score(vrais[garde], predits[garde])),
            float(normalized_mutual_info_score(vrais[garde], predits[garde])), part_mixte)


def roles_prf(paires: Sequence[Tuple[str, str]], predits_non_apparies: Sequence[str],
              vrais_non_apparies: Sequence[str], roles: Sequence[str]
              ) -> Dict[str, Dict[str, Optional[float]]]:
    """Précision, rappel, F1 par rôle. `paires` = (rôle prédit, rôle vrai)."""
    sortie: Dict[str, Dict[str, Optional[float]]] = {}
    for r in roles:
        vp = sum(1 for p, v in paires if p == r and v == r)
        n_pred = sum(1 for p, _ in paires if p == r) + sum(1 for p in predits_non_apparies if p == r)
        n_vrai = sum(1 for _, v in paires if v == r) + sum(1 for v in vrais_non_apparies if v == r)
        precision = vp / n_pred if n_pred else None
        rappel = vp / n_vrai if n_vrai else None
        f1 = (2 * precision * rappel / (precision + rappel)
              if precision and rappel else (0.0 if n_pred and n_vrai else None))
        sortie[r] = {"precision": precision, "rappel": rappel, "f1": f1,
                     "vp": vp, "predits": n_pred, "vrais": n_vrai}
    return sortie


def f1_macro(prf: Dict[str, Dict[str, Optional[float]]], roles: Sequence[str]) -> Optional[float]:
    """Moyenne des F1 des rôles qui ont au moins une partie vraie."""
    valeurs: List[float] = [float(prf[r]["f1"] or 0.0) for r in roles if prf[r]["vrais"]]
    return float(np.mean(valeurs)) if valeurs else None


def erreurs_de_compte(vrais: Sequence[int], predits: Sequence[int]) -> Dict[str, Any]:
    e = [p - v for v, p in zip(vrais, predits, strict=True)]
    a = [abs(x) for x in e]
    return {"mae": float(np.mean(a)) if a else None, "exacts": sum(1 for x in a if x == 0),
            "aUnPres": sum(1 for x in a if x <= 1), "n": len(a), "ecarts": e}


# ---------------------------------------------------------------------------
# Lecture
# ---------------------------------------------------------------------------

def lire_mono(chemin: Path) -> np.ndarray:
    import soundfile as sf

    audio, sr = sf.read(str(chemin), always_2d=True, dtype="float32")
    if sr != SR:
        import librosa
        return librosa.resample(audio.mean(axis=1), orig_sr=sr, target_sr=SR).astype(np.float32)
    return audio.mean(axis=1).astype(np.float32)


def ecrire_mono(chemin: Path, audio: np.ndarray) -> None:
    import soundfile as sf

    chemin.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(chemin), audio.astype(np.float32), SR, subtype="FLOAT")


_NOTES_CACHE: Dict[str, List[Any]] = {}


def notes_de(chemin: Path) -> List[Any]:
    """Les notes par la MÊME fonction que la chaîne (Basic Pitch, vélocités locales)."""
    cle = str(chemin.resolve())
    if cle not in _NOTES_CACHE:
        import contextlib
        import io

        import reconstruire
        with contextlib.redirect_stdout(io.StringIO()):
            _NOTES_CACHE[cle] = reconstruire.extraire_notes(chemin)
    return _NOTES_CACHE[cle]


def kit_de(audio: np.ndarray) -> List[Tuple[str, int]]:
    """Les pièces que la chaîne retient au défaut (sans classifieur de frappes,
    pièces non isolées écartées) : [(famille, frappes)]."""
    import contextlib
    import io
    import tempfile

    from analyzer.vsm_drumkit import build_drum_kit
    with tempfile.TemporaryDirectory() as d, contextlib.redirect_stdout(io.StringIO()):
        kit = build_drum_kit(audio, SR, Path(d), write_samples=False, hit_classifier=None,
                             drop_unisolated=True)
    return [] if kit is None else [(s.family, s.hit_count) for s in kit.slots]


# ---------------------------------------------------------------------------
# Un morceau
# ---------------------------------------------------------------------------

@dataclass
class Partie:
    indice: int
    role: str
    machine: str
    pieces: List[str]
    audio: np.ndarray
    chemin: Path

    @property
    def batterie(self) -> bool:
        return self.role == "batterie"

    @property
    def route(self) -> str:
        return {"basse": "bass", "batterie": "drums", "voix": "vocals"}.get(self.role, "other")


def energies_par_segment(parties: Sequence[Partie], segments: Sequence[Any]) -> np.ndarray:
    e = np.zeros((len(segments), len(parties)))
    for j, p in enumerate(parties):
        carre = np.square(p.audio.astype(np.float64))
        cumul = np.concatenate([[0.0], np.cumsum(carre)])
        for i, s in enumerate(segments):
            a = min(int(s.debut * SR), carre.size)
            b = min(int(s.fin * SR), carre.size)
            e[i, j] = cumul[b] - cumul[a]
    return e


def recenser_niveau(nom: str, stems: Dict[str, np.ndarray], chemins: Dict[str, Path],
                    parties: Sequence[Partie], classifieur: Any) -> Dict[str, Any]:
    """L2 ou L3 : recenser chaque stem, compter, apparier, juger."""
    from analyzer import vsm_recensement as R

    t0 = time.perf_counter()
    energie = {k: float(np.sum(np.square(v.astype(np.float64)))) for k, v in stems.items()}
    totale = sum(energie.values()) or 1.0
    tous = list(parties)                        # la batterie est une étiquette aussi
    blocs, grappes_globales = [], []            # (stem, grappe, labels du stem)
    k_grp = k_pal = k_voix = 0
    ari = nmi = part_mixte = None
    for stem, audio in stems.items():
        if stem == "drums":
            continue
        sous = energie[stem] / totale < R.SEUIL_STEM
        notes = notes_de(chemins[stem])
        r = R.recenser_stem(stem, audio, SR, notes, classifieur, sous_seuil=sous)
        bloc = R.bloc_de_stem(r)
        # L'ÉTIQUETTE SE CHOISIT PARMI LES PARTIES QUE CE STEM DOIT PORTER (sa
        # cible, § 2.4.1 du banc : `other`, `guitar` et `piano` ← le reste). Le
        # premier jet prenait toutes les parties, mesurées sur leur stem VRAI
        # entier : la batterie, absente du stem `other` de L2 par construction,
        # y « dominait » 28 segments sur 57 (24/09, morceau 1, avant toute
        # mesure publiée). Limite dite : en L3, un segment où la séparation a
        # laissé fuir une autre partie reçoit quand même une étiquette de cible.
        cible = TARGET.get(stem, "other")
        indices = [i for i, p in enumerate(tous) if p.route == cible]
        e = energies_par_segment([tous[i] for i in indices], r.segmentation.segments)
        labels = [(indices[k] if k >= 0 else k)
                  for k in (etiquette_dominante(ligne, R.PART_DOMINANTE) for ligne in e)]
        bloc["labelsVrais"] = labels
        blocs.append(bloc)
        if not sous:
            k_grp += len(r.regroupement.grappes)
            k_pal += r.paliers
            k_voix += r.voix
            for g in r.regroupement.grappes:
                grappes_globales.append((stem, g, labels, r))
        if stem == "other":
            ari, nmi, part_mixte = ari_nmi(labels, r.regroupement.etiquettes.tolist())
    # --- batterie ---
    kit = kit_de(stems["drums"]) if "drums" in stems and energie["drums"] / totale >= R.SEUIL_STEM else []
    k_bat = len(kit)
    # --- appariement global grappes ↔ parties (toutes étiquettes) ---
    # Les labels se concatènent stem par stem, dans l'ordre des grappes.
    vus: Dict[int, int] = {}
    labels_concat: List[int] = []
    listes: List[List[int]] = []
    for _stem, g, labels, r in grappes_globales:
        cle = id(r)
        if cle not in vus:
            vus[cle] = len(labels_concat)
            labels_concat.extend(labels)
        listes.append([vus[cle] + i for i in g.segments])
    appariement, matrice = apparier(listes, labels_concat, len(tous))
    stat_tous = statuts(appariement, matrice, len(tous))
    # --- rôles N1 ---
    paires, pred_seuls = [], []
    for gi, (_stem, g, _l, _r) in enumerate(grappes_globales):
        if gi in appariement and not tous[appariement[gi]].batterie:
            paires.append((g.role, ROLE_N1.get(tous[appariement[gi]].role, "autre")))
        else:
            pred_seuls.append(g.role)
    apparies = set(appariement.values())
    vrais_seuls = [ROLE_N1.get(p.role, "autre") for i, p in enumerate(tous)
                   if not p.batterie and i not in apparies]
    roles = list(R.ROLES_MELODIQUES_N1) + ["basse", "voix"]
    prf = roles_prf(paires, pred_seuls, vrais_seuls, roles)
    # --- N3 ---
    rangs, distances = [], []
    for gi, (_stem, g, _l, r) in enumerate(grappes_globales):
        if gi in appariement and not tous[appariement[gi]].batterie:
            partie = tous[appariement[gi]]
            ses_notes = R.notes_de_la_grappe(notes_de(chemins[_stem]),
                                             [r.segmentation.segments[i] for i in g.segments])
            v = R.vecteur_de_grappe(r.descripteurs, g, r.segmentation, ses_notes)
            _c, _m, dist, probas = R.identifier(classifieur, v)
            rangs.append(R.rang_de(classifieur, probas, partie.machine))
            distances.append(dist)
    # --- pièces N1 ---
    pieces_pred = sorted({R.PIECE_N1.get(f, "autre") for f, _ in kit})
    batterie = [p for p in parties if p.batterie]
    pieces_vraies = sorted({PIECE_BANC_N1.get(x, x) for p in batterie for x in p.pieces})
    return {
        "niveau": nom, "stems": blocs, "kit": kit,
        "compte": {"A-grp": {"K_mel": k_grp, "K_bat": k_bat, "K": k_grp + k_bat},
                   "A-pal": {"K_mel": k_pal, "K_bat": k_bat, "K": k_pal + k_bat},
                   "A-voix": {"K_mel": k_voix, "K_bat": k_bat, "K": k_voix + k_bat}},
        "statuts": {tous[i].indice: s for i, s in enumerate(stat_tous) if not tous[i].batterie},
        "ari": ari, "nmi": nmi, "partMixte": part_mixte,
        "roles": prf, "f1Macro": f1_macro(prf, R.ROLES_MELODIQUES_N1),
        "n3": {"rangs": rangs, "distances": distances},
        "pieces": {"predites": pieces_pred, "vraies": pieces_vraies},
        "secondes": round(time.perf_counter() - t0, 2),
    }


def niveau_l1(parties: Sequence[Partie], classifieur: Any) -> Dict[str, Any]:
    """Chaque partie SEULE : combien de grappes, et la machine de la plus longue."""
    from analyzer import vsm_recensement as R

    t0 = time.perf_counter()
    sortie: Dict[str, Any] = {"parties": {}, "pieces": {}}
    for p in parties:
        if p.batterie:
            kit = kit_de(p.audio)
            sortie["pieces"] = {"predites": sorted({R.PIECE_N1.get(f, "autre") for f, _ in kit}),
                                "vraies": sorted({PIECE_BANC_N1.get(x, x) for x in p.pieces}),
                                "kit": kit}
            continue
        notes = notes_de(p.chemin)
        r = R.recenser_stem(p.route, p.audio, SR, notes, classifieur)
        n = len(r.regroupement.grappes)
        rang = dist = None
        if n:
            g = max(r.regroupement.grappes, key=lambda g: g.duree)
            ses = R.notes_de_la_grappe(notes, [r.segmentation.segments[i] for i in g.segments])
            v = R.vecteur_de_grappe(r.descripteurs, g, r.segmentation, ses)
            _c, _m, dist, probas = R.identifier(classifieur, v)
            rang = R.rang_de(classifieur, probas, p.machine)
        sortie["parties"][p.indice] = {
            "role": p.role, "machine": p.machine, "grappes": n,
            "statut": "ok" if n == 1 else ("fondue" if n == 0 else "dedoublee"),
            "rang": rang, "distance": dist, "bloc": R.bloc_de_stem(r)}
    sortie["secondes"] = round(time.perf_counter() - t0, 2)
    return sortie


def niveau_l4(melange: np.ndarray, classifieur: Any) -> Dict[str, Any]:
    """Approche B : le mélange seul."""
    from analyzer import vsm_recensement as R

    t0 = time.perf_counter()
    seg = R.segmenter(melange, SR)
    d = R.descripteurs_complets(melange, SR, seg)
    reg = R.grouper(R.centrer(d, classifieur.moyenne, classifieur.echelle), seg)
    parts = R.part_percussive_par_segment(melange, SR, seg)
    k_bat = sum(1 for g in reg.grappes if float(np.median(parts[g.segments])) >= R.PART_PERCUSSIVE)
    k_mel = len(reg.grappes) - k_bat
    return {"compte": {"B": {"K_mel": k_mel, "K_bat": k_bat, "K": k_mel + k_bat}},
            "segments": len(seg.segments), "bruit": reg.bruit,
            "grappes": [{"dureeS": round(g.duree, 2),
                         "partPercussive": round(float(np.median(parts[g.segments])), 3)}
                        for g in reg.grappes],
            "etiquettes": reg.etiquettes.tolist(),
            "segmentsBornes": [[s.debut, s.fin] for s in seg.segments],
            "secondes": round(time.perf_counter() - t0, 2)}


def mesurer_morceau(dossier: Path, stems_separes: Optional[Path], sortie: Path,
                    classifieur: Any, niveaux: Sequence[str]) -> Dict[str, Any]:
    verite = json.loads((dossier / "verite.json").read_text())
    parties = []
    for i, p in enumerate(verite["parties"]):
        chemin = dossier / p["fichier"]
        parties.append(Partie(i, p["role"], p["machine"], list(p.get("pieces") or []),
                              lire_mono(chemin), chemin))
    k_mel = sum(1 for p in parties if not p.batterie)
    k_bat = sum(len(p.pieces) for p in parties if p.batterie)
    mesure: Dict[str, Any] = {"morceau": dossier.name, "graine": verite.get("graine"),
                              "cas": verite.get("cas"), "duree": verite.get("duree"),
                              "vrai": {"K_mel": k_mel, "K_bat": k_bat, "K": k_mel + k_bat},
                              "parties": [{"indice": p.indice, "role": p.role, "machine": p.machine,
                                           "pieces": p.pieces} for p in parties]}
    if "L1" in niveaux:
        mesure["L1"] = niveau_l1(parties, classifieur)
    if "L2" in niveaux:
        n = max(p.audio.size for p in parties)
        sommes: Dict[str, np.ndarray] = {}
        for p in parties:
            base = sommes.setdefault(p.route, np.zeros(n, dtype=np.float64))
            base[:p.audio.size] += p.audio
        chemins = {}
        for k, v in sommes.items():
            chemins[k] = sortie / dossier.name / "L2" / f"{k}.wav"
            ecrire_mono(chemins[k], v.astype(np.float32))
        mesure["L2"] = recenser_niveau("L2", {k: v.astype(np.float32) for k, v in sommes.items()},
                                       chemins, parties, classifieur)
    if "L3" in niveaux and stems_separes is not None:
        chemins = {k: stems_separes / f"{k}.wav" for k in STEMS_SEPARES
                   if (stems_separes / f"{k}.wav").exists()}
        manquants = [k for k in STEMS_SEPARES if k not in chemins]
        stems = {k: lire_mono(c) for k, c in chemins.items()}
        mesure["L3"] = recenser_niveau("L3", stems, chemins, parties, classifieur)
        mesure["L3"]["stemsManquants"] = manquants
    elif "L3" in niveaux:
        mesure["L3"] = {"nonMesure": "aucun stem séparé pour ce morceau"}
    if "L4" in niveaux:
        mesure["L4"] = niveau_l4(lire_mono(dossier / "morceau.wav"), classifieur)
    return mesure


# ---------------------------------------------------------------------------
# Agrégation
# ---------------------------------------------------------------------------

def agreger(mesures: Sequence[Dict[str, Any]], parite: Dict[str, Dict[str, int]]) -> Dict[str, Any]:
    ag: Dict[str, Any] = {"n": len(mesures)}
    vrais_k = [m["vrai"]["K"] for m in mesures]
    vrais_km = [m["vrai"]["K_mel"] for m in mesures]
    comptes: Dict[str, Dict[str, Any]] = {}

    def ajouter(cle: str, predits_k: List[int], predits_km: List[int], vk: List[int], vkm: List[int]):
        comptes[cle] = {"K": erreurs_de_compte(vk, predits_k), "K_mel": erreurs_de_compte(vkm, predits_km)}

    for niveau in ("L2", "L3"):
        ok = [m for m in mesures if niveau in m and "compte" in m[niveau]]
        if not ok:
            continue
        for approche in ("A-grp", "A-pal", "A-voix"):
            ajouter(f"{approche}@{niveau}", [m[niveau]["compte"][approche]["K"] for m in ok],
                    [m[niveau]["compte"][approche]["K_mel"] for m in ok],
                    [m["vrai"]["K"] for m in ok], [m["vrai"]["K_mel"] for m in ok])
    ok = [m for m in mesures if "L4" in m]
    if ok:
        ajouter("B@L4", [m["L4"]["compte"]["B"]["K"] for m in ok],
                [m["L4"]["compte"]["B"]["K_mel"] for m in ok],
                [m["vrai"]["K"] for m in ok], [m["vrai"]["K_mel"] for m in ok])
    ok = [m for m in mesures if m["morceau"] in parite]
    if ok:
        ajouter("P@L3", [parite[m["morceau"]]["K"] for m in ok],
                [parite[m["morceau"]]["K_mel"] for m in ok],
                [m["vrai"]["K"] for m in ok], [m["vrai"]["K_mel"] for m in ok])
    ag["comptes"] = comptes
    ag["vrais"] = {"K": vrais_k, "K_mel": vrais_km}

    # Attendu 2 : part imputable à la séparation, meilleure approche A.
    meilleure, part = None, None
    candidats = [(comptes[f"{a}@L3"]["K"]["mae"], a) for a in ("A-grp", "A-pal", "A-voix")
                 if f"{a}@L3" in comptes]
    if candidats:
        _, meilleure = min(candidats)
        e3 = comptes[f"{meilleure}@L3"]["K"]["mae"]
        e2 = comptes.get(f"{meilleure}@L2", {}).get("K", {}).get("mae")
        part = None if (e2 is None or not e3) else (e3 - e2) / e3
    ag["meilleureA"] = meilleure
    ag["partSeparation"] = part

    # Attribution partie par partie (A-grp) : premier niveau en défaut.
    attribution: Counter[str] = Counter()
    for m in mesures:
        for p in m["parties"]:
            if p["role"] == "batterie":
                continue
            i = p["indice"]
            s1 = m.get("L1", {}).get("parties", {}).get(i, {}).get("statut")
            s2 = m.get("L2", {}).get("statuts", {}).get(i)
            s3 = m.get("L3", {}).get("statuts", {}).get(i)
            if s1 not in (None, "ok"):
                attribution["segmentation+embedding (L1)"] += 1
            elif s2 not in (None, "ok"):
                attribution["regroupement dans un mélange (L2)"] += 1
            elif s3 not in (None, "ok"):
                attribution["séparation (L3)"] += 1
            elif s3 == "ok":
                attribution["juste jusqu'en L3"] += 1
            else:
                attribution["non mesuré"] += 1
    ag["attribution"] = dict(attribution)
    perdues = sum(v for k, v in attribution.items() if "(L" in k)
    ag["partPerduesD_abordEnL3"] = (attribution["séparation (L3)"] / perdues) if perdues else None

    # L1
    l1 = [pp for m in mesures for pp in m.get("L1", {}).get("parties", {}).values()]
    if l1:
        une = [pp for pp in l1 if pp["grappes"] == 1]
        deux_mains = [pp for pp in l1 if pp["role"] == "piano-deux-mains"]
        rangs = [pp["rang"] for pp in l1 if pp["rang"] is not None]
        ag["L1"] = {"parties": len(l1), "uneGrappe": len(une), "partUneGrappe": len(une) / len(l1),
                    "distribution": dict(Counter(pp["grappes"] for pp in l1)),
                    "deuxMainsCoupees": sum(1 for pp in deux_mains if pp["grappes"] >= 2),
                    "deuxMains": len(deux_mains),
                    "n3": resume_rangs(rangs, len(l1)),
                    "distanceMediane": med([pp["distance"] for pp in l1 if pp["distance"] is not None])}
    for niveau in ("L2", "L3"):
        ok = [m[niveau] for m in mesures if niveau in m and "compte" in m[niveau]]
        if not ok:
            continue
        rangs = [r for x in ok for r in x["n3"]["rangs"] if r is not None]
        dist = [d for x in ok for d in x["n3"]["distances"]]
        aris = [x["ari"] for x in ok if x["ari"] is not None]
        nmis = [x["nmi"] for x in ok if x["nmi"] is not None]
        f1s = [x["f1Macro"] for x in ok if x["f1Macro"] is not None]
        # rôles agrégés : on somme les comptes
        roles_tot: Dict[str, Dict[str, float]] = defaultdict(lambda: {"vp": 0, "pred": 0, "vrais": 0})
        for x in ok:
            for r, v in x["roles"].items():
                roles_tot[r]["vp"] += v["vp"]
                roles_tot[r]["pred"] += v["predits"]
                roles_tot[r]["vrais"] += v["vrais"]
        prf = {}
        for r, t in roles_tot.items():
            pr = t["vp"] / t["pred"] if t["pred"] else None
            rp = t["vp"] / t["vrais"] if t["vrais"] else None
            prf[r] = {"precision": pr, "rappel": rp,
                      "f1": (2 * pr * rp / (pr + rp)) if pr and rp else (0.0 if t["pred"] and t["vrais"] else None),
                      "predits": t["pred"], "vrais": t["vrais"]}
        from analyzer.vsm_recensement import ROLES_MELODIQUES_N1
        pieces = {}
        for piece in PIECES_N1:
            avec = [x for x in ok if piece in x["pieces"]["vraies"]]
            pieces[piece] = ({"rappel": sum(1 for x in avec if piece in x["pieces"]["predites"]) / len(avec),
                              "morceaux": len(avec)} if avec else "INDÉCIDABLE : aucune partie vraie")
        ag[niveau] = {"ariMoyen": med_moy(aris), "nmiMoyen": med_moy(nmis), "f1MacroParMorceau": med_moy(f1s),
                      "roles": prf, "f1MacroGlobal": f1_macro(prf, ROLES_MELODIQUES_N1),
                      "n3": resume_rangs(rangs, len(rangs)), "distanceMediane": med(dist),
                      "pieces": pieces,
                      "secondesMoyennes": float(np.mean([x["secondes"] for x in ok]))}
    return ag


def med(v: Sequence[float]) -> Optional[float]:
    return float(np.median(v)) if len(v) else None


def med_moy(v: Sequence[float]) -> Optional[Dict[str, float]]:
    return {"moyenne": float(np.mean(v)), "mediane": float(np.median(v)), "n": len(v)} if v else None


def resume_rangs(rangs: Sequence[int], n: int) -> Dict[str, Any]:
    if not rangs:
        return {"n": 0}
    r = np.asarray(rangs)
    return {"n": int(r.size), "top1": float(np.mean(r <= 1)), "top5": float(np.mean(r <= 5)),
            "rangMedian": float(np.median(r))}


def parite_du_banc(banc: Optional[Path]) -> Dict[str, Dict[str, int]]:
    """Le témoin P : les pistes que la parité a produites, relues au rapport du banc."""
    if banc is None or not (banc / "rapport.json").exists():
        return {}
    r = json.loads((banc / "rapport.json").read_text())
    sortie = {}
    for m in r["morceaux"]:
        p = m["parite"]
        sortie[m["morceau"]] = {"K": p["pistes_obtenues"],
                                "K_mel": p["pistes_obtenues"] - p["pistes_batterie"]}
    return sortie


def commit() -> str:
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ICI, capture_output=True,
                              text=True, check=True).stdout.strip()
    except Exception:  # noqa: BLE001
        return "inconnu"


def empreinte(chemin: Path) -> str:
    import hashlib

    h = hashlib.sha256()
    with open(chemin, "rb") as f:
        for bloc in iter(lambda: f.read(1 << 20), b""):
            h.update(bloc)
    return "sha256:" + h.hexdigest()


# ---------------------------------------------------------------------------
# Tableau
# ---------------------------------------------------------------------------

def fmt(v: Any, n: int = 3) -> str:
    if v is None:
        return "—"
    if isinstance(v, float):
        return f"{v:.{n}f}"
    return str(v)


def tableau(ag: Dict[str, Any], lot: str) -> str:
    lignes = [f"RECENSEMENT — lot {lot}, {ag['n']} morceaux", ""]
    lignes.append(f"{'approche@niveau':16s} {'MAE K':>7s} {'exacts':>7s} {'±1':>5s} {'MAE Kmél':>9s} {'±1 mél':>7s}")
    for cle, c in sorted(ag["comptes"].items()):
        lignes.append(f"{cle:16s} {fmt(c['K']['mae'], 2):>7s} {c['K']['exacts']:>4d}/{c['K']['n']:<2d} "
                      f"{c['K']['aUnPres']:>2d}/{c['K']['n']:<2d} {fmt(c['K_mel']['mae'], 2):>9s} "
                      f"{c['K_mel']['aUnPres']:>4d}/{c['K_mel']['n']:<2d}")
    lignes.append("")
    lignes.append(f"meilleure approche A en L3 : {ag['meilleureA']} ; part imputable à la séparation "
                  f"(E_L3 − E_L2)/E_L3 = {fmt(ag['partSeparation'])}")
    lignes.append(f"attribution (A-grp, par partie mélodique) : {ag['attribution']} ; "
                  f"part des perdues d'abord en L3 = {fmt(ag['partPerduesD_abordEnL3'])}")
    if "L1" in ag:
        a = ag["L1"]
        lignes.append(f"L1 : {a['uneGrappe']}/{a['parties']} parties seules à 1 grappe "
                      f"({fmt(a['partUneGrappe'])}), distribution {a['distribution']}, "
                      f"deux-mains coupées {a['deuxMainsCoupees']}/{a['deuxMains']} ; "
                      f"N3 {a['n3']} ; distance médiane au corpus {fmt(a['distanceMediane'], 2)}")
    for niveau in ("L2", "L3"):
        if niveau in ag:
            a = ag[niveau]
            lignes.append(f"{niveau} : ARI {a['ariMoyen']} NMI {a['nmiMoyen']} ; F1 macro rôles "
                          f"(global) {fmt(a['f1MacroGlobal'])} ; N3 {a['n3']} ; distance médiane "
                          f"{fmt(a['distanceMediane'], 2)} ; {fmt(a['secondesMoyennes'], 1)} s/morceau")
            lignes.append("      rôles : " + " · ".join(
                f"{r} P={fmt(v['precision'], 2)} R={fmt(v['rappel'], 2)} ({v['vrais']} vrais, {v['predits']} prédits)"
                for r, v in a["roles"].items()))
            lignes.append(f"      pièces : {a['pieces']}")
    return "\n".join(lignes)


# ---------------------------------------------------------------------------
# Entrée
# ---------------------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lot", type=Path, help="dossier d'un lot du banc synthétique")
    ap.add_argument("--banc", type=Path, default=None,
                    help="dossier du banc qui a gardé les stems séparés (et le rapport de parité)")
    ap.add_argument("--stems-separes", type=Path, default=None,
                    help="dossier <morceau>/stems-separes/*.wav, prioritaire sur --banc")
    ap.add_argument("--sortie", type=Path, required=True)
    ap.add_argument("--niveaux", default="L1,L2,L3,L4")
    ap.add_argument("--morceaux", default="", help="filtre sur le nom des morceaux")
    ap.add_argument("--classifieur", type=Path, default=ICI.parent / "modeles" / "classifieur.joblib")
    ap.add_argument("--reel", type=Path, default=None, help="un disque : le mélange")
    ap.add_argument("--stems", type=Path, default=None, help="un disque : ses stems séparés")
    args = ap.parse_args()

    from analyzer import vsm_recensement as R
    from analyzer.vsm_classifier import Classifieur

    classifieur = Classifieur.relit(args.classifieur)
    args.sortie.mkdir(parents=True, exist_ok=True)
    provenance = {"commit": commit(), "commande": sys.argv, "reglages": R.reglages(),
                  "versions": R.versions(),
                  "empreintes": {"classifieur": empreinte(args.classifieur)},
                  "date": time.strftime("%Y-%m-%dT%H:%M:%S")}

    if args.reel is not None:
        return mesurer_reel(args, classifieur, provenance)

    niveaux = [n.strip() for n in args.niveaux.split(",") if n.strip()]
    morceaux = sorted(d for d in args.lot.iterdir() if (d / "verite.json").exists()
                      and args.morceaux in d.name)
    mesures, non_mesures = [], []
    for d in morceaux:
        fichier = args.sortie / d.name / "mesure.json"
        if fichier.exists():
            print(f"[{time.strftime('%T')}] DÉJÀ {d.name}", flush=True)
            mesures.append(json.loads(fichier.read_text()))
            continue
        separes = None
        if args.stems_separes and (args.stems_separes / d.name / "stems-separes").exists():
            separes = args.stems_separes / d.name / "stems-separes"
        elif args.banc and (args.banc / d.name / "stems-separes" / "stems").exists():
            separes = args.banc / d.name / "stems-separes" / "stems"
        print(f"[{time.strftime('%T')}] DÉBUT {d.name} (stems séparés : {separes or 'AUCUN'})", flush=True)
        t = time.perf_counter()
        try:
            m = mesurer_morceau(d, separes, args.sortie, classifieur, niveaux)
        except Exception as erreur:  # noqa: BLE001 — dit, jamais tu
            import traceback
            traceback.print_exc()
            non_mesures.append({"morceau": d.name, "raison": f"{type(erreur).__name__}: {erreur}"})
            print(f"[{time.strftime('%T')}] NON MESURÉ {d.name} : {erreur}", flush=True)
            continue
        m["secondes"] = round(time.perf_counter() - t, 1)
        fichier.parent.mkdir(parents=True, exist_ok=True)
        fichier.write_text(json.dumps(m, ensure_ascii=False, default=_json))
        mesures.append(m)
        print(f"[{time.strftime('%T')}] FIN {d.name} en {m['secondes']} s : vrai K={m['vrai']['K']} "
              + " ".join(f"{n}:{m[n]['compte']}" for n in ("L2", "L3", "L4")
                         if n in m and "compte" in m[n]), flush=True)
    ag = agreger(mesures, parite_du_banc(args.banc))
    rapport = {"format": FORMAT_BANC, "version": VERSION_BANC, "provenance": provenance,
               "lot": str(args.lot), "agrege": ag, "nonMesures": non_mesures,
               "morceaux": [m["morceau"] for m in mesures]}
    (args.sortie / "rapport.json").write_text(json.dumps(rapport, ensure_ascii=False, indent=1, default=_json))
    texte = tableau(ag, args.lot.name)
    if non_mesures:
        texte += "\n\nNON MESURÉS : " + "; ".join(f"{x['morceau']} ({x['raison']})" for x in non_mesures)
    (args.sortie / "tableau.txt").write_text(texte + "\n")
    print(texte)
    return 0 if not non_mesures else 1


def mesurer_reel(args, classifieur, provenance) -> int:
    """Un disque : comptes L3 et L4, publiés sans vérité."""
    from analyzer import vsm_recensement as R

    t = time.perf_counter()
    chemins = {k: args.stems / f"{k}.wav" for k in STEMS_SEPARES if (args.stems / f"{k}.wav").exists()}
    stems = {k: lire_mono(c) for k, c in chemins.items()}
    notes = {k: notes_de(c) for k, c in chemins.items() if k != "drums"}
    kit = kit_de(stems["drums"]) if "drums" in stems else []
    bloc = R.recenser_morceau(stems, SR, notes, kit, classifieur)
    blocs = bloc["stems"]
    cpa = bloc["comptesParApproche"]
    kit = [(b["piece"], b["frappes"]) for b in bloc["batterie"]]
    k_grp, k_pal, k_voix = cpa["A-grp"]["K_mel"], cpa["A-pal"]["K_mel"], cpa["A-voix"]["K_mel"]
    energie = bloc["partEnergie"]
    l4 = niveau_l4(lire_mono(args.reel), classifieur)
    sortie = {"format": FORMAT_BANC, "version": VERSION_BANC, "provenance": provenance,
              "reel": str(args.reel), "stems": str(args.stems),
              "partEnergie": energie,
              "L3": {"compte": {"A-grp": {"K_mel": k_grp, "K_bat": len(kit), "K": k_grp + len(kit)},
                                "A-pal": {"K_mel": k_pal, "K_bat": len(kit), "K": k_pal + len(kit)},
                                "A-voix": {"K_mel": k_voix, "K_bat": len(kit), "K": k_voix + len(kit)}},
                     "stems": blocs, "kit": kit},
              "L4": {k: v for k, v in l4.items() if k not in ("etiquettes", "segmentsBornes")},
              "secondes": round(time.perf_counter() - t, 1)}
    (args.sortie / "rapport.json").write_text(json.dumps(sortie, ensure_ascii=False, indent=1, default=_json))
    print(json.dumps({"L3": sortie["L3"]["compte"], "L4": sortie["L4"]["compte"],
                      "kit": kit, "partEnergie": sortie["partEnergie"],
                      "grappes": {b["stem"]: [(g["role"], g["dureeS"]) for g in b["grappes"]] for b in blocs},
                      "secondes": sortie["secondes"]}, ensure_ascii=False, indent=1))
    return 0


def _json(o: Any) -> Any:
    if isinstance(o, (np.integer,)):
        return int(o)
    if isinstance(o, (np.floating,)):
        return float(o)
    if isinstance(o, np.ndarray):
        return o.tolist()
    if isinstance(o, float) and math.isnan(o):
        return None
    raise TypeError(type(o))


if __name__ == "__main__":
    sys.exit(main())
