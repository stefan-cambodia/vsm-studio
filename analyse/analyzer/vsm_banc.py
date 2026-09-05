"""
Le TABLEAU DE BORD PAR ÉTAGE du banc synthétique : où la chaîne perd.

Sur un morceau généré (`vsm_morceaux.py`), la vérité est complète. Ce module
lit une course de `reconstruire.py` faite sur ce morceau et mesure chaque
étage contre ce qu'il DEVAIT produire (docs/CDC-banc-synthetique.md § 2.4) :

  1. séparation    : chaque stem séparé contre la somme des stems vrais qu'il
                     devait porter (corrélation, SDR), énergie hallucinée ;
  2. transcription : notes vraies contre notes transcrites (précision, rappel,
                     F1 à ±1 demi-ton et ±50 ms, puis à hauteur exacte),
                     erreur de vélocité, erreur de durée ; frappes à part ;
  3. parité        : parties vraies contre pistes obtenues — fondues,
                     inventées, et le cas deux-mains (H25) compté à part ;
  4. arbitrage     : rang de la vraie machine dans le classement de chaque
                     piste, et la BORNE de piste (vraie machine, vrai patch,
                     notes transcrites, contre la cible que la chaîne a jugée) ;
  5. global        : la distance de la chaîne, la borne de transcription (les
                     pistes de la chaîne rendues par leurs vraies machines,
                     calées aux vrais niveaux) et la borne de production.

La perte imputable à chaque étage se lit dans les différences. Rien de
silencieux : un étage non mesurable le dit avec sa raison, une piste sans
partie est nommée, un rendu refusé est écrit.

Ce module ne touche pas à la chaîne : il la lit. Sans banc, la chaîne est la
chaîne d'aujourd'hui, à l'octet près.
"""

from __future__ import annotations

import json
import math
import re
from collections import Counter
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple

import numpy as np

from .vsm_engine import Note, VsmEngine, VsmEngineError
from .vsm_morceaux import ROLE_BATTERIE, ROLE_DEUX_MAINS, SR, lire_wav_float, stems_attendus

TOLERANCE_ATTAQUE_S = 0.05
TOLERANCE_HAUTEUR = 1
STEM_ATTENDU_PAR_ROLE = {"basse": "bass", ROLE_BATTERIE: "drums"}
STEMS_SANS_PARTIE = ("vocals", "guitar", "piano")  # à six sources, rien ne leur est destiné


# ---------------------------------------------------------------------------
# Lecture
# ---------------------------------------------------------------------------

def _mono(audio: np.ndarray) -> np.ndarray:
    audio = np.asarray(audio, dtype=np.float64)
    return audio.mean(axis=1) if audio.ndim == 2 else audio


def lire_wav_mono(chemin: Path) -> np.ndarray:
    import soundfile as sf

    audio, taux = sf.read(str(chemin), dtype="float32", always_2d=True)
    if taux != SR:
        raise ValueError(f"{chemin} : {taux} Hz, attendu {SR}")
    return _mono(audio)


def nom_normalise(nom: str) -> str:
    """Le MIDI écrit « other - 84-96 », le projet « other · 84-96 » : même piste."""
    return nom.replace(" · ", " - ").strip()


def stem_de_la_piste(nom: str) -> str:
    """Le stem qu'une piste a été jugée contre : le préfixe de son nom."""
    nom = nom_normalise(nom)
    if nom.startswith("Batterie"):
        return "drums"
    if nom.startswith("Voix"):
        return "vocals"
    return nom.split(" - ")[0].strip()


def iteration_de_la_piste(nom: str) -> int:
    """L'itération de la boucle résiduelle qui a produit la piste : « other -
    r1 » → 1 ; sans suffixe, 0 (la chaîne d'aujourd'hui)."""
    correspondance = re.search(r" - r(\d+)$", nom_normalise(nom))
    return int(correspondance.group(1)) if correspondance else 0


def lire_pistes_midi(chemin: Path) -> List[Dict[str, object]]:
    """Les pistes du MIDI écrit par la chaîne, notes en secondes.

    [{"nom", "notes": [[note, vélocité, début, durée], ...]}, ...] dans l'ordre
    du fichier — celui des pistes de project.json hors bus.
    """
    import mido

    fichier = mido.MidiFile(str(chemin))
    tempo = 500000
    for message in fichier.tracks[0]:
        if message.type == "set_tempo":
            tempo = message.tempo
            break
    tpq = fichier.ticks_per_beat
    pistes: List[Dict[str, object]] = []
    for piste in fichier.tracks:
        nom = ""
        tick = 0
        ouvertes: Dict[int, Tuple[int, int]] = {}
        notes: List[List[float]] = []
        for message in piste:
            tick += message.time
            if message.type == "track_name":
                nom = message.name
            elif message.type == "note_on" and message.velocity > 0:
                ouvertes[message.note] = (tick, message.velocity)
            elif message.type in ("note_off", "note_on"):
                if message.note in ouvertes:
                    debut, velocite = ouvertes.pop(message.note)
                    notes.append([message.note, velocite, mido.tick2second(debut, tpq, tempo),
                                  mido.tick2second(tick - debut, tpq, tempo)])
        if nom or notes:
            notes.sort(key=lambda n: (n[2], n[0]))
            pistes.append({"nom": nom, "notes": notes})
    return pistes


# ---------------------------------------------------------------------------
# 1. Séparation
# ---------------------------------------------------------------------------

def _sdr(reference: np.ndarray, estimation: np.ndarray) -> float:
    n = min(reference.size, estimation.size)
    ref, est = reference[:n], estimation[:n]
    energie = float(np.sum(ref ** 2))
    residu = float(np.sum((ref - est) ** 2))
    if energie <= 0:
        return float("nan")
    if residu <= 0:
        return float("inf")
    return 10 * math.log10(energie / residu)


def _correlation(a: np.ndarray, b: np.ndarray) -> float:
    n = min(a.size, b.size)
    a, b = a[:n], b[:n]
    da, db = float(np.sqrt(np.sum(a ** 2))), float(np.sqrt(np.sum(b ** 2)))
    if da <= 0 or db <= 0:
        return float("nan")
    return float(np.dot(a, b) / (da * db))


def _fichiers_de_stems(dossier: Path) -> Dict[str, Path]:
    """Les stems d'UNE séparation : le sous-dossier `stems/` s'il existe (le
    dossier de travail porte aussi arbitrage, réglage, verdict, et les résidus
    de la boucle), sinon les WAV du dossier même."""
    dossier = Path(dossier)
    if (dossier / "stems").is_dir():
        return {f.stem: f for f in sorted((dossier / "stems").glob("*.wav"))}
    return {f.stem: f for f in sorted(dossier.glob("*.wav"))}


def _stems_attendus_sauf(verite: dict, morceau: Path, exclues: Set[int]) -> Dict[str, np.ndarray]:
    """Les stems que la séparation d'un RÉSIDU devrait rendre : bass, drums,
    other — sommés depuis la vérité, SANS les parties déjà retenues."""
    groupes: Dict[str, np.ndarray] = {}
    for indice, partie in enumerate(verite["parties"]):
        if indice in exclues:
            continue
        nom = STEM_ATTENDU_PAR_ROLE.get(partie["role"], "other")
        stem = lire_wav_float(morceau / partie["fichier"]).astype(np.float64)
        groupes[nom] = groupes[nom] + stem if nom in groupes else stem
    return {nom: audio.astype(np.float32) for nom, audio in groupes.items()}


def mesurer_separation(verite: dict, morceau: Path, stems_separes: Optional[Path]) -> dict:
    if stems_separes is None or not stems_separes.exists():
        return {"mesure": False, "raison": "stems vrais fournis à la chaîne : la séparation n'a pas tourné"}
    fichiers = _fichiers_de_stems(stems_separes)
    if not fichiers:
        return {"mesure": False, "raison": f"aucun stem séparé dans {stems_separes}"}
    attendus = {nom: _mono(audio) for nom, audio in stems_attendus(verite, morceau).items()}
    return _separation_contre(attendus, fichiers)


def _separation_contre(attendus: Dict[str, np.ndarray], fichiers: Dict[str, Path]) -> dict:
    """Une séparation (nom → fichier) jugée contre ce qu'elle devait rendre."""
    separes = {nom: lire_wav_mono(f) for nom, f in fichiers.items()}
    energie_totale = sum(float(np.sum(s ** 2)) for s in separes.values()) or 1e-12
    par_stem: Dict[str, dict] = {}
    hallucinee = 0.0
    for nom, audio in separes.items():
        energie = float(np.sum(audio ** 2))
        if nom in attendus:
            par_stem[nom] = {"attendu": True, "correlation": _correlation(attendus[nom], audio),
                             "sdr_db": _sdr(attendus[nom], audio), "part_energie": energie / energie_totale}
        else:
            hallucinee += energie
            par_stem[nom] = {"attendu": False, "part_energie": energie / energie_totale,
                             "note": "aucune partie vraie ne devait y aller : énergie hallucinée"}
    for nom in attendus:
        if nom not in separes:
            par_stem[nom] = {"attendu": True, "absent": True, "note": "attendu et non rendu par la séparation"}
    elargi = None
    if "other" in attendus:
        somme = np.zeros_like(attendus["other"])
        membres = [n for n in ("other", "guitar", "piano") if n in separes]
        for n in membres:
            m = min(somme.size, separes[n].size)
            somme[:m] += separes[n][:m]
        elargi = {"membres": membres, "correlation": _correlation(attendus["other"], somme),
                  "sdr_db": _sdr(attendus["other"], somme)}
    return {"mesure": True, "stems": par_stem, "part_energie_hallucinee": hallucinee / energie_totale,
            "other_elargi": elargi}


# ---------------------------------------------------------------------------
# 2. Transcription
# ---------------------------------------------------------------------------

def apparier(vraies: Sequence[Sequence[float]], transcrites: Sequence[Sequence[float]],
             tolerance_hauteur: int = TOLERANCE_HAUTEUR, tolerance_attaque: float = TOLERANCE_ATTAQUE_S,
             sans_hauteur: bool = False) -> List[Tuple[int, int]]:
    """Paires (indice vraie, indice transcrite), glouton par attaque croissante.

    Chaque note transcrite prend la note vraie libre la plus proche en
    attaque, à ±tolérance d'attaque et ±tolérance de hauteur.
    """
    ordre_vraies = sorted(range(len(vraies)), key=lambda i: vraies[i][2])
    debuts = np.asarray([vraies[i][2] for i in ordre_vraies], dtype=float)
    libres = np.ones(len(vraies), dtype=bool)
    paires: List[Tuple[int, int]] = []
    for j in sorted(range(len(transcrites)), key=lambda k: transcrites[k][2]):
        note, _, debut, _ = transcrites[j][:4]
        bas = int(np.searchsorted(debuts, debut - tolerance_attaque, side="left"))
        haut = int(np.searchsorted(debuts, debut + tolerance_attaque, side="right"))
        meilleur, ecart = -1, float("inf")
        for k in range(bas, haut):
            i = ordre_vraies[k]
            if not libres[i]:
                continue
            if not sans_hauteur and abs(int(vraies[i][0]) - int(note)) > tolerance_hauteur:
                continue
            e = abs(float(vraies[i][2]) - float(debut))
            if e < ecart:
                meilleur, ecart = i, e
        if meilleur >= 0:
            libres[meilleur] = False
            paires.append((meilleur, j))
    return paires


def _scores(nb_vraies: int, nb_transcrites: int, nb_paires: int) -> dict:
    precision = nb_paires / nb_transcrites if nb_transcrites else 0.0
    rappel = nb_paires / nb_vraies if nb_vraies else 0.0
    f1 = 2 * precision * rappel / (precision + rappel) if precision + rappel else 0.0
    return {"vraies": nb_vraies, "transcrites": nb_transcrites, "appariees": nb_paires,
            "precision": precision, "rappel": rappel, "f1": f1}


def mesurer_transcription(verite: dict, pistes: Sequence[dict]) -> dict:
    parties = verite["parties"]
    vraies: List[List[float]] = []
    role_de_la_note: List[str] = []
    for partie in parties:
        if partie["role"] == ROLE_BATTERIE:
            continue
        for note in partie["notes"]:
            vraies.append(list(note))
            role_de_la_note.append(partie["role"])
    transcrites: List[List[float]] = []
    for piste in pistes:
        if stem_de_la_piste(str(piste["nom"])) in ("drums", "vocals"):
            continue
        transcrites.extend(list(n) for n in piste["notes"])
    paires = apparier(vraies, transcrites)
    exactes = apparier(vraies, transcrites, tolerance_hauteur=0)
    resultat = {"melodique": _scores(len(vraies), len(transcrites), len(paires)),
                "melodique_hauteur_exacte": _scores(len(vraies), len(transcrites), len(exactes))}
    if paires:
        dv = [abs(vraies[i][1] - transcrites[j][1]) for i, j in paires]
        dd = [abs(vraies[i][3] - transcrites[j][3]) for i, j in paires]
        rel = [abs(vraies[i][3] - transcrites[j][3]) / max(vraies[i][3], 1e-3) for i, j in paires]
        resultat["velocite_erreur_absolue_moyenne"] = float(np.mean(dv))
        resultat["duree_erreur_mediane_s"] = float(np.median(dd))
        resultat["duree_erreur_relative_mediane"] = float(np.median(rel))
    else:
        resultat["velocite_erreur_absolue_moyenne"] = None
        resultat["duree_erreur_mediane_s"] = None
        resultat["duree_erreur_relative_mediane"] = None
    par_role: Dict[str, dict] = {}
    appariees_par_role: Dict[str, int] = {}
    for i, _ in paires:
        appariees_par_role[role_de_la_note[i]] = appariees_par_role.get(role_de_la_note[i], 0) + 1
    for role in sorted(set(role_de_la_note)):
        total = role_de_la_note.count(role)
        par_role[role] = {"vraies": total, "rappel": appariees_par_role.get(role, 0) / total if total else 0.0}
    resultat["rappel_par_role"] = par_role
    # Frappes : attaque seule, ±50 ms, sans hauteur.
    frappes_vraies = [list(n) for p in parties if p["role"] == ROLE_BATTERIE for n in p["notes"]]
    frappes_transcrites = [list(n) for piste in pistes if stem_de_la_piste(str(piste["nom"])) == "drums"
                           for n in piste["notes"]]
    paires_frappes = apparier(frappes_vraies, frappes_transcrites, sans_hauteur=True)
    resultat["frappes"] = _scores(len(frappes_vraies), len(frappes_transcrites), len(paires_frappes))
    return resultat, paires, vraies, transcrites


# ---------------------------------------------------------------------------
# 3. Parité
# ---------------------------------------------------------------------------

def attribuer_pistes(verite: dict, pistes: Sequence[dict]) -> Dict[str, Optional[int]]:
    """Piste (nom normalisé) → indice de la partie vraie dont elle porte le plus de notes appariées."""
    parties = verite["parties"]
    vraies: List[List[float]] = []
    partie_de_la_note: List[int] = []
    for indice, partie in enumerate(parties):
        if partie["role"] == ROLE_BATTERIE:
            continue
        for note in partie["notes"]:
            vraies.append(list(note))
            partie_de_la_note.append(indice)
    attribution: Dict[str, Optional[int]] = {}
    for piste in pistes:
        nom = nom_normalise(str(piste["nom"]))
        if stem_de_la_piste(nom) in ("drums", "vocals"):
            continue
        comptes: Dict[int, int] = {}
        for i, _ in apparier(vraies, list(piste["notes"])):
            comptes[partie_de_la_note[i]] = comptes.get(partie_de_la_note[i], 0) + 1
        attribution[nom] = max(comptes, key=lambda k: (comptes[k], -k)) if comptes else None
    return attribution


def mesurer_parite(verite: dict, pistes: Sequence[dict], projet: dict) -> dict:
    parties = verite["parties"]
    attribution = attribuer_pistes(verite, pistes)
    pistes_de: Dict[int, List[str]] = {i: [] for i in range(len(parties))}
    sans_partie: List[str] = []
    for nom, indice in attribution.items():
        if indice is None:
            sans_partie.append(nom)
        else:
            pistes_de[indice].append(nom)
    fondues: List[str] = []
    inventees = 0
    h25 = 0
    detail: List[dict] = []
    for indice, partie in enumerate(parties):
        if partie["role"] == ROLE_BATTERIE:
            continue
        obtenues = pistes_de[indice]
        if not obtenues:
            fondues.append(f"{indice + 1:02d}-{partie['role']} ({partie['machine']})")
        elif partie["role"] == ROLE_DEUX_MAINS:
            h25 += len(obtenues) - 1
        else:
            inventees += len(obtenues) - 1
        detail.append({"partie": f"{indice + 1:02d}-{partie['role']}", "machine": partie["machine"],
                       "cas": partie.get("cas"), "pistes": obtenues})
    # Les pistes qui jouent, hors bus ; les pistes audio (voix) comptent — ce sont des pistes.
    jouees = [t for t in projet.get("tracks", []) if t.get("kind") != "group"]
    noms_batterie = [nom_normalise(t["name"]) for t in jouees if stem_de_la_piste(t["name"]) == "drums"]
    noms_voix = [nom_normalise(t["name"]) for t in jouees if stem_de_la_piste(t["name"]) == "vocals"]
    pieces_vraies = sum(len(p.get("pieces", [])) for p in parties if p["role"] == ROLE_BATTERIE)
    parties_melodiques = sum(1 for p in parties if p["role"] != ROLE_BATTERIE)
    inventees += len(sans_partie) + len(noms_voix)
    # La batterie : autant de pistes que de pièces frappées. Ce qui manque est
    # fondu (deux pièces sur une piste), ce qui dépasse est inventé.
    fondues_batterie = max(0, pieces_vraies - len(noms_batterie)) if pieces_vraies else 0
    inventees_batterie = max(0, len(noms_batterie) - pieces_vraies)
    if fondues_batterie:
        fondues.append(f"batterie : {fondues_batterie} pièce(s) sur {pieces_vraies} sans piste propre")
    inventees += inventees_batterie
    # Le cas déclaré, jugé par son nom.
    cas = verite.get("cas", "aucun")
    verdict_cas = None
    concernees = [i for i, p in enumerate(parties) if p.get("cas") == cas and cas != "aucun"]
    if cas == "deux-mains" and concernees:
        n = len(pistes_de[concernees[0]])
        verdict_cas = "absent" if n == 0 else ("entier" if n == 1 else f"coupé en {n}")
    elif cas in ("memes-machine-disjoints", "chevauchement") and len(concernees) == 2:
        a, b = (set(pistes_de[i]) for i in concernees)
        if not a or not b:
            verdict_cas = "fondu (une partie sans piste)"
        elif a & b:
            verdict_cas = "fondu (même piste)"
        else:
            verdict_cas = "séparé"
    return {
        "parties_melodiques": parties_melodiques, "pieces_de_batterie": pieces_vraies,
        "parties_vraies": parties_melodiques + pieces_vraies,
        "pistes_obtenues": len(jouees), "pistes_batterie": len(noms_batterie), "pistes_voix": noms_voix,
        "ecart": len(jouees) - (parties_melodiques + pieces_vraies),
        "fondues": fondues, "inventees": inventees, "h25": h25,
        "fondues_batterie": fondues_batterie, "inventees_batterie": inventees_batterie,
        "pistes_sans_partie": sans_partie, "cas": cas, "verdict_cas": verdict_cas,
        "attribution": attribution, "detail": detail,
    }


# ---------------------------------------------------------------------------
# 4 et 5. Arbitrage et bornes : il faut le moteur
# ---------------------------------------------------------------------------

def _rendre(engine: VsmEngine, machine: str, patch: Dict[str, float], notes: Sequence[Sequence[float]],
            duree: float) -> np.ndarray:
    return engine.render(machine, patch, [Note(int(n[0]), int(n[1]), float(n[2]), float(n[3])) for n in notes],
                         duration=duree, sample_rate=SR).astype(np.float64)


def _panner(mono: np.ndarray, pan: float) -> np.ndarray:
    theta = (pan + 1.0) * math.pi / 4.0
    return np.stack([mono * math.cos(theta), mono * math.sin(theta)], axis=1)


def _rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(x ** 2))) if x.size else 0.0


def mesurer_arbitrage_et_bornes(verite: dict, morceau: Path, rapport: dict, pistes: Sequence[dict],
                                attribution: Dict[str, Optional[int]], cibles: Dict[str, np.ndarray],
                                engine: Optional[VsmEngine], journal=print) -> Tuple[dict, dict]:
    from .vsm_distance_cache import cached_distance_for
    from .vsm_reconstruct import reconstruction_distance

    metrique = str(rapport.get("metric", "v2"))
    parties = verite["parties"]
    original = lire_wav_mono(morceau / "morceau.wav")
    duree = original.size / SR
    # Borne de production : la somme des stems vrais, secs, contre le mélange.
    # Sommée comme le générateur l'a fait (float64, puis float32, puis mono)
    # pour que, sans production, elle SOIT le mélange et que la borne soit 0.
    somme_stereo = np.zeros((original.size, 2))
    for partie in parties:
        stem = lire_wav_float(morceau / partie["fichier"]).astype(np.float64)
        n = min(stem.shape[0], original.size)
        somme_stereo[:n] += stem[:n]
    somme_vraie = _mono(somme_stereo.astype(np.float32))
    borne_production = reconstruction_distance(original, somme_vraie, SR, metric=metrique)

    if engine is None:
        return ({"mesure": False, "raison": "aucun moteur : rangs seuls"},
                {"global": rapport.get("globalDistance"), "borne_production": borne_production,
                 "borne_transcription": None, "mesure": False, "raison": "aucun moteur"})

    par_nom = {nom_normalise(str(s["name"])): s for s in rapport.get("stems", [])}
    arbitrage: List[dict] = []
    rendus_par_partie: Dict[int, np.ndarray] = {}
    # La batterie est UNE partie vraie jouée sur plusieurs pistes (une par
    # pièce) : la chaîne l'a arbitrée en une fois, sur le stem entier. Ses
    # pistes sont donc sommées en une seule entrée, jugée contre le même stem.
    pistes_batterie = [p for p in pistes if stem_de_la_piste(str(p["nom"])) == "drums"]
    a_juger: List[dict] = [p for p in pistes if stem_de_la_piste(str(p["nom"])) not in ("drums", "vocals")]
    if pistes_batterie:
        a_juger.append({"nom": f"Batterie ({len(pistes_batterie)} pistes)",
                        "notes": [n for p in pistes_batterie for n in p["notes"]], "batterie": True})
    for piste in a_juger:
        nom = nom_normalise(str(piste["nom"]))
        stem = "drums" if piste.get("batterie") else stem_de_la_piste(nom)
        if stem == "drums":
            indices = [i for i, p in enumerate(parties) if p["role"] == ROLE_BATTERIE]
            if not indices:
                arbitrage.append({"piste": nom, "partie": None, "note": "pistes de batterie sans partie vraie : inventées"})
                continue
            indice = indices[0]
        else:
            indice = attribution.get(nom)
            if indice is None:
                arbitrage.append({"piste": nom, "partie": None, "note": "aucune partie vraie appariée : inventée"})
                continue
        partie = parties[indice]
        entree: dict = {"piste": nom, "partie": f"{indice + 1:02d}-{partie['role']}", "machine_vraie": partie["machine"]}
        classement = None
        if stem == "drums":
            classement = (rapport.get("drums") or {}).get("trackArbitration")
            entree["distance_chaine"] = (rapport.get("drums") or {}).get("trackDistance")
        elif nom in par_nom:
            classement = par_nom[nom].get("trackArbitration")
            entree["distance_chaine"] = par_nom[nom].get("trackDistance", par_nom[nom].get("distance"))
            entree["machine_chaine"] = par_nom[nom].get("machine")
        if classement:
            # Le rang se compte en MACHINES distinctes, dans l'ordre du classement
            # (une machine y figure plusieurs fois : patch cherché, patch
            # d'usine, un par profil). Sur Sky and Sand, 194 entrées pour 59
            # machines ; « top 6 » veut dire six machines, pas six lignes.
            machines: List[str] = []
            for c in classement:
                if c["machine"] not in machines:
                    machines.append(c["machine"])
            entree["rang"] = machines.index(partie["machine"]) + 1 if partie["machine"] in machines else None
            entree["candidates"] = len(machines)
            entree["entrees_du_classement"] = len(classement)
            if entree["rang"] is None:
                entree["note"] = "la vraie machine n'a pas concouru"
        else:
            entree["rang"] = None
            entree["candidates"] = 0
            entree["note"] = "aucun classement d'arbitrage dans le rapport pour cette piste"
        # La borne de piste : vraie machine, vrai patch, notes TRANSCRITES, contre la cible jugée.
        try:
            rendu = _rendre(engine, partie["machine"], partie["patch"], piste["notes"], duree)
            rendu = np.pad(rendu, (0, max(0, original.size - rendu.size)))[:original.size]
            # Une piste issue du résidu k a été jugée contre le stem du RÉSIDU
            # k, pas contre celui du départ : la cible porte l'itération.
            k = iteration_de_la_piste(nom)
            cible = cibles.get(f"{stem} - r{k}") if k else cibles.get(stem)
            if cible is not None and _rms(rendu) > 0:
                entree["borne_piste"] = float(cached_distance_for(metrique)(cible, SR)(rendu))
                if entree.get("distance_chaine") is not None:
                    entree["perte_arbitrage_reglage"] = float(entree["distance_chaine"]) - entree["borne_piste"]
            else:
                entree["borne_piste"] = None
                entree["note"] = (entree.get("note", "") + f" ; cible « {stem} » absente ou rendu muet").strip(" ;")
            rendus_par_partie[indice] = rendus_par_partie.get(indice, np.zeros(original.size)) + rendu
        except VsmEngineError as erreur:
            entree["borne_piste"] = None
            entree["note"] = f"rendu refusé : {erreur}"
            journal(f"    {nom} : rendu de la vraie machine refusé ({erreur})")
        arbitrage.append(entree)

    # Borne de transcription : les pistes de la chaîne, rendues par leurs vraies
    # machines, au GAIN vrai de leur partie (celui que le générateur a appliqué
    # au rendu brut, crête comprise), panoramiquées comme la vérité. Avec les
    # notes exactes, ce mélange EST la somme des stems vrais ; avec des notes
    # manquantes, il en manque la part — c'est ce qu'on veut lire. Caler au
    # RMS vrai aurait remonté un rendu incomplet au niveau du complet.
    melange = np.zeros((original.size, 2))
    for indice, rendu in rendus_par_partie.items():
        partie = parties[indice]
        melange += _panner(rendu * float(partie["gain"]), float(partie["pan"]))
    borne_transcription = reconstruction_distance(original, melange.mean(axis=1), SR, metric=metrique)
    global_chaine = rapport.get("globalDistance")
    rangs = [e["rang"] for e in arbitrage if e.get("rang")]
    bornes = [e for e in arbitrage if e.get("borne_piste") is not None and e.get("distance_chaine") is not None]
    resume = {
        "mesure": True, "pistes": arbitrage,
        "pistes_jugees": len(rangs), "rang_1": sum(1 for r in rangs if r == 1),
        "top_6": sum(1 for r in rangs if r <= 6), "rang_median": float(np.median(rangs)) if rangs else None,
        "borne_superieure_a_la_chaine": sum(1 for e in bornes if e["borne_piste"] > e["distance_chaine"]),
        "pistes_avec_borne": len(bornes),
    }
    global_ = {
        "mesure": True, "metrique": metrique, "global": global_chaine,
        "borne_transcription": borne_transcription, "borne_production": borne_production,
        "perte_transcription_parite": (borne_transcription - borne_production),
        "perte_arbitrage_reglage_calage": (float(global_chaine) - borne_transcription) if global_chaine is not None else None,
        "parties_rendues_dans_la_borne": len(rendus_par_partie),
    }
    return resume, global_


# ---------------------------------------------------------------------------
# Un morceau entier
# ---------------------------------------------------------------------------

def mesurer_morceau(morceau: Path, course: Path, stems_separes: Optional[Path],
                    engine: Optional[VsmEngine], stems_vrais_fournis: bool = False, journal=print) -> dict:
    verite = json.loads((morceau / "verite.json").read_text(encoding="utf-8"))
    rapport = json.loads((course / "rapport.json").read_text(encoding="utf-8"))
    projet = json.loads((course / "project.json").read_text(encoding="utf-8"))
    pistes = lire_pistes_midi(course / "midi" / "arrangement.mid")
    # Un BUS de groupe (« Batterie », « other ») figure dans le MIDI sans
    # notes : ce n'est pas une partie, c'est un fader. On l'écarte par son
    # nom, lu dans le projet.
    bus = {nom_normalise(t["name"]) for t in projet.get("tracks", []) if t.get("kind") == "group"}
    pistes = [p for p in pistes if nom_normalise(str(p["nom"])) not in bus]
    separation = mesurer_separation(verite, morceau, None if stems_vrais_fournis else stems_separes)
    transcription, _, _, _ = mesurer_transcription(verite, pistes)
    parite = mesurer_parite(verite, pistes, projet)
    # Les cibles que la chaîne a jugées : les stems qu'elle a reçus — ceux du
    # départ, et ceux de chaque résidu de la boucle sous « stem - rk ».
    cibles: Dict[str, np.ndarray] = {}
    if stems_vrais_fournis or stems_separes is None or not stems_separes.exists():
        cibles = {nom: _mono(audio) for nom, audio in stems_attendus(verite, morceau).items()}
    else:
        cibles = {nom: lire_wav_mono(f) for nom, f in _fichiers_de_stems(stems_separes).items()}
        for residu in sorted(stems_separes.glob("residu-r*")):
            k = residu.name[len("residu-r"):]
            for nom, f in _fichiers_de_stems(residu).items():
                cibles[f"{nom} - r{k}"] = lire_wav_mono(f)
    arbitrage, global_ = mesurer_arbitrage_et_bornes(verite, morceau, rapport, pistes, parite["attribution"],
                                                     cibles, engine, journal)
    residuel = mesurer_residuel(verite, morceau, rapport, pistes, projet, parite["attribution"],
                                None if stems_vrais_fournis else stems_separes)
    return {
        "morceau": morceau.name, "graine": verite.get("graine"), "cas": verite.get("cas"),
        "production": verite.get("production") is not None, "parties": verite.get("nombre_de_parties"),
        "melange_est_la_somme_des_stems": verite.get("melange_est_la_somme_des_stems"),
        "separation": separation, "transcription": transcription, "parite": parite,
        "arbitrage": arbitrage, "global": global_, "residuel": residuel,
    }


# ---------------------------------------------------------------------------
# 6. La boucle résiduelle (docs/CDC-separation-par-synthese.md § 2.7)
# ---------------------------------------------------------------------------

def _parite_a_l_iteration(verite: dict, pistes: Sequence[dict], projet: dict, k: int) -> dict:
    """La parité en ne comptant que les pistes d'itération ≤ k."""
    pistes_k = [p for p in pistes if iteration_de_la_piste(str(p["nom"])) <= k]
    projet_k = {"tracks": [t for t in projet.get("tracks", [])
                           if iteration_de_la_piste(str(t.get("name", ""))) <= k]}
    p = mesurer_parite(verite, pistes_k, projet_k)
    return {"pistes_obtenues": p["pistes_obtenues"], "parties_vraies": p["parties_vraies"],
            "ecart": p["ecart"], "fondues": len(p["fondues"]), "inventees": p["inventees"], "h25": p["h25"]}


def mesurer_residuel(verite: dict, morceau: Path, rapport: dict, pistes: Sequence[dict], projet: dict,
                     attribution: Dict[str, Optional[int]], stems_separes: Optional[Path]) -> dict:
    """Ce que la boucle a fait au résidu, jugé contre le résidu VRAI.

    Le résidu vrai à l'itération k est le mélange moins les stems vrais des
    parties déjà retenues — une partie est retenue quand une piste soustraite
    lui est attribuée (celle dont elle porte le plus de notes appariées), la
    batterie soustraite retient la partie batterie. Deux SDR par itération,
    avant et après la soustraction, pour que la montée se lise ; la séparation
    du résidu jugée contre les parties NON retenues ; la parité à k ; la
    distance en l'état que la chaîne a publiée.
    """
    bloc = rapport.get("residuel")
    if not bloc:
        return {"mesure": False, "raison": "la course n'a pas tourné avec --residuel"}
    if stems_separes is None or not Path(stems_separes).exists():
        return {"mesure": False, "raison": "pas de dossier de travail conservé : le résidu n'est pas sur disque"}
    parties = verite["parties"]
    indices_batterie = [i for i, p in enumerate(parties) if p["role"] == ROLE_BATTERIE]
    melange = _mono(lire_wav_float(morceau / "morceau.wav"))
    stems_vrais = {i: _mono(lire_wav_float(morceau / p["fichier"])) for i, p in enumerate(parties)}
    retenues: Set[int] = set()
    precedent = melange
    iterations: List[dict] = []
    for it in bloc.get("iterations", []):
        k = int(it.get("iteration", len(iterations) + 1))
        fiche: Dict[str, object] = {"iteration": k}
        sous = it.get("soustraction")
        if sous is None:
            fiche["note"] = "aucune soustraction : la boucle s'est arrêtée avant"
            fiche["candidats"] = [{"unite": c.get("unite"), "correlation_stem": c.get("correlationStem"),
                                   "motif": c.get("motif")} for c in it.get("candidats", [])]
            iterations.append(fiche)
            continue
        sans_partie: List[str] = []
        for membre in sous.get("membres", []):
            nom = nom_normalise(str(membre))
            if stem_de_la_piste(nom) == "drums":
                if indices_batterie:
                    retenues.add(indices_batterie[0])
                else:
                    sans_partie.append(nom)
            else:
                indice = attribution.get(nom)
                if indice is None:
                    sans_partie.append(nom)
                else:
                    retenues.add(indice)
        fiche.update({
            "unite": sous.get("unite"), "membres": sous.get("membres"),
            "correlation_stem": sous.get("correlationStem"), "correlation_reste": sous.get("correlationReste"),
            "gain": sous.get("gain"), "decalage_echantillons": sous.get("decalageEchantillons"),
            "parties_retenues": [f"{i + 1:02d}-{parties[i]['role']}" for i in sorted(retenues)],
            "membres_sans_partie": sans_partie,
            "energie_residu_part": (it.get("energie") or {}).get("partApres"),
        })
        residu_vrai = melange.copy()
        for i in retenues:
            n = min(residu_vrai.size, stems_vrais[i].size)
            residu_vrai[:n] -= stems_vrais[i][:n]
        chemin_residu = Path(stems_separes) / f"residu-r{k}" / "residu.wav"
        if not chemin_residu.exists():
            fiche["note"] = f"résidu absent du disque : {chemin_residu}"
            iterations.append(fiche)
            continue
        residu = lire_wav_mono(chemin_residu)
        if retenues:
            avant = _sdr(residu_vrai, precedent)
            apres = _sdr(residu_vrai, residu)
            fiche["sdr_residu_vrai_avant_db"] = avant
            fiche["sdr_residu_vrai_apres_db"] = apres
            fiche["montee_db"] = (apres - avant) if (np.isfinite(avant) and np.isfinite(apres)) else None
        else:
            fiche["note"] = "aucune partie vraie retenue : le résidu vrai est le mélange, SDR sans objet"
        dossier_stems = Path(stems_separes) / f"residu-r{k}" / "stems"
        fichiers = _fichiers_de_stems(dossier_stems) if dossier_stems.exists() else {}
        attendus = {nom: _mono(audio) for nom, audio in _stems_attendus_sauf(verite, morceau, retenues).items()}
        if fichiers and attendus:
            fiche["separation"] = _separation_contre(attendus, fichiers)
        elif not fichiers:
            fiche["separation"] = {"mesure": False, "raison": "le résidu n'a pas été séparé (arrêt avant)"}
        else:
            fiche["separation"] = {"mesure": False, "raison": "toutes les parties vraies sont retenues"}
        fiche["parite"] = _parite_a_l_iteration(verite, pistes, projet, k)
        fiche["pistes_ajoutees"] = [p.get("piste") for p in it.get("pistesAjoutees", [])]
        fiche["stems_refuses"] = len(it.get("stemsRefuses", []))
        fiche["distance_projet"] = it.get("distanceProjet")
        fiche["secondes"] = it.get("secondes")
        precedent = residu
        iterations.append(fiche)
    return {
        "mesure": True, "demande": bloc.get("demande"), "arret": bloc.get("arret"),
        "distance_initiale": bloc.get("distanceProjetInitiale"),
        "parite_initiale": _parite_a_l_iteration(verite, pistes, projet, 0),
        "soustractions": sum(1 for f in iterations if f.get("unite")),
        "iterations": iterations,
    }


# ---------------------------------------------------------------------------
# Agrégation et tableau
# ---------------------------------------------------------------------------

def _mediane(valeurs: Sequence[Optional[float]]) -> Optional[float]:
    v = [float(x) for x in valeurs if x is not None and np.isfinite(x)]
    return float(np.median(v)) if v else None


def _moyenne(valeurs: Sequence[Optional[float]]) -> Optional[float]:
    v = [float(x) for x in valeurs if x is not None and np.isfinite(x)]
    return float(np.mean(v)) if v else None


def agreger(mesures: Sequence[dict]) -> dict:
    def col(chemin: Sequence[str]):
        sortie = []
        for m in mesures:
            v = m
            for cle in chemin:
                v = v.get(cle) if isinstance(v, dict) else None
                if v is None:
                    break
            sortie.append(v)
        return sortie

    def sep(nom: str, cle: str):
        return [((m["separation"].get("stems") or {}).get(nom) or {}).get(cle) for m in mesures
                if m["separation"].get("mesure")]

    top6 = [m["arbitrage"].get("top_6") for m in mesures if m["arbitrage"].get("mesure")]
    jugees = [m["arbitrage"].get("pistes_jugees") for m in mesures if m["arbitrage"].get("mesure")]
    rang1 = [m["arbitrage"].get("rang_1") for m in mesures if m["arbitrage"].get("mesure")]
    bornes_sup = [m["arbitrage"].get("borne_superieure_a_la_chaine") for m in mesures if m["arbitrage"].get("mesure")]
    avec_borne = [m["arbitrage"].get("pistes_avec_borne") for m in mesures if m["arbitrage"].get("mesure")]
    pertes_t = col(["global", "perte_transcription_parite"])
    pertes_a = col(["global", "perte_arbitrage_reglage_calage"])
    premier_poste = sum(1 for t, a in zip(pertes_t, pertes_a) if t is not None and a is not None and t > a)
    residuels = [m["residuel"] for m in mesures if (m.get("residuel") or {}).get("mesure")]
    premieres = [f for r in residuels for f in r["iterations"] if f.get("unite") and f["iteration"] == 1]
    toutes = [f for r in residuels for f in r["iterations"] if f.get("unite")]
    residuel = {
        "morceaux_mesures": len(residuels),
        "morceaux_avec_soustraction": sum(1 for r in residuels if r["soustractions"]),
        "soustractions": len(toutes),
        "unites_soustraites": dict(Counter(f["unite"] for f in toutes)),
        "correlation_stem_mediane_r1": _mediane([f.get("correlation_stem") for f in premieres]),
        "montee_sdr_residu_vrai_mediane_r1": _mediane([f.get("montee_db") for f in premieres]),
        "sdr_residu_vrai_apres_mediane_r1": _mediane([f.get("sdr_residu_vrai_apres_db") for f in premieres]),
        "bass_sdr_au_residu_mediane_r1": _mediane([((f.get("separation") or {}).get("stems") or {}).get("bass", {}).get("sdr_db")
                                                   for f in premieres]),
        "pistes_ajoutees_total": sum(len(f.get("pistes_ajoutees") or []) for f in toutes),
        "stems_refuses_total": sum(f.get("stems_refuses") or 0 for f in toutes),
        "secondes_par_iteration_mediane": _mediane([(f.get("secondes") or {}).get("total") for f in toutes]),
        "arrets": dict(Counter((r.get("arret") or {}).get("motif", "?") for r in residuels)),
        "ecart_parite_initial_median": _mediane([r["parite_initiale"]["ecart"] for r in residuels]),
        "inventees_initiales_total": sum(r["parite_initiale"]["inventees"] for r in residuels),
    } if residuels else {"morceaux_mesures": 0}
    return {
        "morceaux": len(mesures),
        "residuel": residuel,
        "separation": {
            "mesures": sum(1 for m in mesures if m["separation"].get("mesure")),
            "bass_sdr_db": _mediane(sep("bass", "sdr_db")), "bass_correlation": _mediane(sep("bass", "correlation")),
            "drums_sdr_db": _mediane(sep("drums", "sdr_db")), "other_sdr_db": _mediane(sep("other", "sdr_db")),
            "other_elargi_sdr_db": _mediane([(m["separation"].get("other_elargi") or {}).get("sdr_db")
                                             for m in mesures if m["separation"].get("mesure")]),
            "part_energie_hallucinee": _mediane(col(["separation", "part_energie_hallucinee"])),
        },
        "transcription": {
            "f1": _mediane(col(["transcription", "melodique", "f1"])),
            "precision": _mediane(col(["transcription", "melodique", "precision"])),
            "rappel": _mediane(col(["transcription", "melodique", "rappel"])),
            "f1_hauteur_exacte": _mediane(col(["transcription", "melodique_hauteur_exacte", "f1"])),
            "velocite_erreur_absolue_moyenne": _mediane(col(["transcription", "velocite_erreur_absolue_moyenne"])),
            "duree_erreur_relative_mediane": _mediane(col(["transcription", "duree_erreur_relative_mediane"])),
            "frappes_f1": _mediane(col(["transcription", "frappes", "f1"])),
        },
        "parite": {
            "ecart_median": _mediane(col(["parite", "ecart"])),
            "ecart_au_plus_1": sum(1 for e in col(["parite", "ecart"]) if e is not None and abs(e) <= 1),
            "fondues_total": sum(len(m["parite"]["fondues"]) for m in mesures),
            "inventees_total": sum(m["parite"]["inventees"] for m in mesures),
            "h25_total": sum(m["parite"]["h25"] for m in mesures),
            "verdicts_des_cas": {m["morceau"]: (m["cas"], m["parite"]["verdict_cas"]) for m in mesures
                                 if m["cas"] != "aucun"},
        },
        "arbitrage": {
            "pistes_jugees": int(sum(j or 0 for j in jugees)),
            "top_6": int(sum(t or 0 for t in top6)),
            "rang_1": int(sum(r or 0 for r in rang1)),
            "taux_top_6": (sum(t or 0 for t in top6) / sum(j or 0 for j in jugees)) if sum(j or 0 for j in jugees) else None,
            "taux_rang_1": (sum(r or 0 for r in rang1) / sum(j or 0 for j in jugees)) if sum(j or 0 for j in jugees) else None,
            "borne_superieure_a_la_chaine": int(sum(b or 0 for b in bornes_sup)),
            "pistes_avec_borne": int(sum(b or 0 for b in avec_borne)),
        },
        "global": {
            "distance_mediane": _mediane(col(["global", "global"])), "distance_moyenne": _moyenne(col(["global", "global"])),
            "borne_transcription_mediane": _mediane(col(["global", "borne_transcription"])),
            "borne_production_mediane": _mediane(col(["global", "borne_production"])),
            "perte_transcription_parite_mediane": _mediane(pertes_t),
            "perte_arbitrage_reglage_calage_mediane": _mediane(pertes_a),
            "transcription_premier_poste": premier_poste,
            "morceaux_compares": sum(1 for t, a in zip(pertes_t, pertes_a) if t is not None and a is not None),
        },
    }


def _f(x, fmt: str = "{:.3f}") -> str:
    if x is None:
        return "—"
    try:
        if isinstance(x, float) and not np.isfinite(x):
            return "inf" if x > 0 else "-inf"
        return fmt.format(x)
    except (TypeError, ValueError):
        return str(x)


def tableau(mesures: Sequence[dict], agregat: dict) -> str:
    lignes = []
    entete = (f"{'morceau':22s} {'cas':24s} {'part.':>5s} {'pistes':>6s} {'fond.':>5s} {'inv.':>4s} {'H25':>3s} "
              f"{'F1':>5s} {'vél.':>5s} {'SDR b/d/o':>14s} {'top6':>7s} {'global':>7s} {'borne T':>8s} {'borne P':>8s}")
    lignes.append(entete)
    lignes.append("-" * len(entete))
    for m in mesures:
        sep = m["separation"]
        if sep.get("mesure"):
            stems = sep.get("stems", {})
            sdr = "/".join(_f((stems.get(n) or {}).get("sdr_db"), "{:.1f}") for n in ("bass", "drums", "other"))
        else:
            sdr = "non mesuré"
        arb = m["arbitrage"]
        top6 = f"{arb.get('top_6')}/{arb.get('pistes_jugees')}" if arb.get("mesure") else "—"
        p = m["parite"]
        t = m["transcription"]
        g = m["global"]
        lignes.append(f"{m['morceau'][:22]:22s} {str(m['cas'])[:24]:24s} {p['parties_vraies']:5d} {p['pistes_obtenues']:6d} "
                      f"{len(p['fondues']):5d} {p['inventees']:4d} {p['h25']:3d} {_f(t['melodique']['f1'], '{:.2f}'):>5s} "
                      f"{_f(t.get('velocite_erreur_absolue_moyenne'), '{:.0f}'):>5s} {sdr:>14s} {top6:>7s} "
                      f"{_f(g.get('global')):>7s} {_f(g.get('borne_transcription')):>8s} {_f(g.get('borne_production')):>8s}")
        if p.get("verdict_cas"):
            lignes.append(f"{'':22s} cas {m['cas']} : {p['verdict_cas']}")
        if p["fondues"]:
            lignes.append(f"{'':22s} fondues : {', '.join(p['fondues'])}")
        if p["pistes_sans_partie"]:
            lignes.append(f"{'':22s} inventées sans partie : {', '.join(p['pistes_sans_partie'])}")
        if arb.get("mesure"):
            for e in arb["pistes"]:
                if e.get("partie") is None:
                    continue
                lignes.append(f"{'':22s}   {e['piste'][:28]:28s} vraie {e['machine_vraie']:16s} rang {_f(e.get('rang'), '{}'):>4s}"
                              f"/{e.get('candidates', 0):<3d} chaîne {_f(e.get('distance_chaine'))} borne {_f(e.get('borne_piste'))}"
                              + (f"  ({e['note']})" if e.get("note") else ""))
        res = m.get("residuel") or {}
        if res.get("mesure"):
            for f in res["iterations"]:
                if not f.get("unite"):
                    meilleure = max((c for c in f.get("candidats", []) if c.get("correlation_stem") is not None),
                                    key=lambda c: c["correlation_stem"], default=None)
                    lignes.append(f"{'':22s} résiduel r{f['iteration']} : rien de soustrait"
                                  + (f" — meilleure corrélation {meilleure['correlation_stem']:.2f} ({meilleure['unite']})"
                                     if meilleure else ""))
                    continue
                sep_res = (f.get("separation") or {})
                bass_res = ((sep_res.get("stems") or {}).get("bass") or {}).get("sdr_db") if sep_res.get("mesure") else None
                dp = f.get("distance_projet") or {}
                pk = f.get("parite") or {}
                lignes.append(
                    f"{'':22s} résiduel r{f['iteration']} : {f['unite']} SOUSTRAITE (corr {_f(f.get('correlation_stem'), '{:.2f}')}, "
                    f"gain {_f(f.get('gain'), '{:.2f}')}, {_f(f.get('decalage_echantillons'), '{:+d}')} éch.) ; "
                    f"résidu {_f(f.get('energie_residu_part'), '{:.0f}')} % ; SDR résidu vrai "
                    f"{_f(f.get('sdr_residu_vrai_avant_db'), '{:.1f}')} → {_f(f.get('sdr_residu_vrai_apres_db'), '{:.1f}')} dB ; "
                    f"bass au résidu {_f(bass_res, '{:.1f}')} dB ; +{len(f.get('pistes_ajoutees') or [])} piste(s)"
                    + (f" ({', '.join(f['pistes_ajoutees'])})" if f.get("pistes_ajoutees") else "")
                    + f", {f.get('stems_refuses', 0)} refusé(s) ; parité {pk.get('pistes_obtenues', '—')}/{pk.get('parties_vraies', '—')} "
                    f"inv. {pk.get('inventees', '—')} ; distance {_f(dp.get('avant'))} → {_f(dp.get('apres'))}"
                    + (f" ; {f['note']}" if f.get("note") else ""))
            arret = res.get("arret") or {}
            lignes.append(f"{'':22s} résiduel : arrêt {arret.get('motif', '?')} — {arret.get('detail', '')}")
        elif res.get("raison") and "--residuel" not in str(res.get("raison")):
            lignes.append(f"{'':22s} résiduel : non mesuré — {res['raison']}")
    a = agregat
    lignes.append("")
    lignes.append(f"AGRÉGÉ sur {a['morceaux']} morceaux (médianes)")
    s = a["separation"]
    lignes.append(f"  séparation   : {s['mesures']} mesurés — bass SDR {_f(s['bass_sdr_db'], '{:.1f}')} dB corr {_f(s['bass_correlation'], '{:.2f}')} ; "
                  f"drums SDR {_f(s['drums_sdr_db'], '{:.1f}')} dB ; other SDR {_f(s['other_sdr_db'], '{:.1f}')} dB "
                  f"(élargi {_f(s['other_elargi_sdr_db'], '{:.1f}')} dB) ; hallucinée {_f(s['part_energie_hallucinee'], '{:.1%}')}")
    t = a["transcription"]
    lignes.append(f"  transcription: F1 {_f(t['f1'], '{:.2f}')} (P {_f(t['precision'], '{:.2f}')} R {_f(t['rappel'], '{:.2f}')}) ; "
                  f"hauteur exacte F1 {_f(t['f1_hauteur_exacte'], '{:.2f}')} ; vélocité EAM {_f(t['velocite_erreur_absolue_moyenne'], '{:.0f}')} ; "
                  f"durée err. rel. {_f(t['duree_erreur_relative_mediane'], '{:.0%}')} ; frappes F1 {_f(t['frappes_f1'], '{:.2f}')}")
    p = a["parite"]
    lignes.append(f"  parité       : écart médian {_f(p['ecart_median'], '{:+.0f}')} ; écart ≤ 1 sur {p['ecart_au_plus_1']}/{a['morceaux']} ; "
                  f"fondues {p['fondues_total']} ; inventées {p['inventees_total']} ; H25 {p['h25_total']}")
    for morceau, (cas, verdict) in p["verdicts_des_cas"].items():
        lignes.append(f"      {morceau[:22]:22s} {cas:24s} → {verdict}")
    r = a["arbitrage"]
    lignes.append(f"  arbitrage    : vraie machine top 6 {r['top_6']}/{r['pistes_jugees']} ({_f(r['taux_top_6'], '{:.0%}')}) ; "
                  f"rang 1 {r['rang_1']} ({_f(r['taux_rang_1'], '{:.0%}')}) ; borne > chaîne {r['borne_superieure_a_la_chaine']}/{r['pistes_avec_borne']}")
    g = a["global"]
    lignes.append(f"  global       : distance {_f(g['distance_mediane'])} (moy. {_f(g['distance_moyenne'])}) ; borne transcription {_f(g['borne_transcription_mediane'])} ; "
                  f"borne production {_f(g['borne_production_mediane'])} ; perte transcription+parité {_f(g['perte_transcription_parite_mediane'])} ; "
                  f"perte arbitrage+réglage+calage {_f(g['perte_arbitrage_reglage_calage_mediane'])} ; "
                  f"transcription premier poste {g['transcription_premier_poste']}/{g['morceaux_compares']}")
    r = a.get("residuel") or {}
    if r.get("morceaux_mesures"):
        unites = ", ".join(f"{u} {n}" for u, n in sorted(r["unites_soustraites"].items(), key=lambda kv: -kv[1]))
        arrets = ", ".join(f"{motif} {n}" for motif, n in sorted(r["arrets"].items(), key=lambda kv: -kv[1]))
        lignes.append(f"  résiduel     : soustractions dans {r['morceaux_avec_soustraction']}/{r['morceaux_mesures']} morceaux "
                      f"({unites or 'aucune'}) ; corr. stem médiane r1 {_f(r['correlation_stem_mediane_r1'], '{:.2f}')} ; "
                      f"SDR résidu vrai {_f(r['montee_sdr_residu_vrai_mediane_r1'], '{:+.1f}')} dB (médiane r1, "
                      f"après {_f(r['sdr_residu_vrai_apres_mediane_r1'], '{:.1f}')} dB) ; "
                      f"bass au résidu {_f(r['bass_sdr_au_residu_mediane_r1'], '{:.1f}')} dB ; "
                      f"pistes ajoutées {r['pistes_ajoutees_total']}, stems refusés {r['stems_refuses_total']} ; "
                      f"parité initiale écart {_f(r['ecart_parite_initial_median'], '{:+.0f}')} inventées {r['inventees_initiales_total']} ; "
                      f"arrêts : {arrets} ; secondes/itération {_f(r['secondes_par_iteration_mediane'], '{:.0f}')}")
    return "\n".join(lignes)
