# -*- coding: utf-8 -*-
"""
LA BOUCLE RÉSIDUELLE : soustraire ce qu'on sait rendre, reséparer ce qui reste.

POURQUOI. Le nombre et le contenu des pistes sont décidés par un séparateur
entraîné sur de la pop, et S1 (docs/CDC-banc-synthetique.md § 6) a chiffré ce
qu'il rend sur vingt morceaux à vérité connue : `bass` à 0,21 dB de SDR et
0,26 de corrélation avec la vraie basse. Tout l'aval est plafonné par l'amont.
Le projet a ce qu'aucun séparateur n'a : un moteur déterministe qui sait
RENDRE une piste une fois trouvée. La boucle en tire parti — la partie la plus
sûre est rendue seule, alignée sur le mélange, SOUSTRAITE ; le résidu est
reséparé, et la chaîne relancée dessus, sur un mélange plus simple. Le
cahier des charges : docs/CDC-separation-par-synthese.md.

CE QUE CE MODULE FAIT, ET CE QU'IL NE FAIT PAS. Il porte l'arithmétique
(alignement, gain, corrélation, soustraction, notes déjà portées) et la
BOUCLE elle-même, écrite sur des collaborateurs injectés : rendre une unité,
séparer un résidu, reconstruire des stems, mesurer la distance du projet.
`reconstruire.py` fournit les vrais ; les tests en donnent de factices, et
c'est ainsi que chaque motif d'arrêt se vérifie sans payer une course. Le
module n'importe ni le moteur, ni demucs, ni la chaîne.

RIEN DE SILENCIEUX. Chaque candidate rendue est publiée avec sa corrélation,
soustraite ou non ; chaque soustraction avec son décalage, son gain et les
deux énergies ; chaque refus avec son motif ; chaque arrêt avec le sien.
Le résidu est un OBJET DE MESURE : il s'écrit dans le dossier de travail,
aucune piste ne le référence, et rien de ce que le DAW joue n'en dépend.
"""

from __future__ import annotations

import bisect
import struct
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Tuple

import numpy as np

# L'alignement se cherche dans ±50 ms : au-delà, un rendu n'est plus « la
# même partie un peu décalée », c'est une autre note. À 44,1 kHz.
FENETRE_ALIGNEMENT = 2205

# Les tolérances du banc (docs/CDC-banc-synthetique.md § 2.4), reprises telles
# quelles pour dire qu'une note transcrite dans le résidu est DÉJÀ portée par
# une piste retenue : même hauteur à un demi-ton, même attaque à 50 ms. Une
# frappe de batterie, à 30 ms, quelle que soit la pièce.
TOLERANCE_HAUTEUR = 1
TOLERANCE_ATTAQUE_S = 0.05
TOLERANCE_FRAPPE_S = 0.03

MOTIFS_D_ARRET = ("aucune-piste-sure", "residu-sous-le-seuil", "rien-de-discernable",
                  "distance-sans-gain", "iterations-atteintes")


# ---------------------------------------------------------------------------
# Objets
# ---------------------------------------------------------------------------

@dataclass
class Unite:
    """Ce qui se soustrait : une piste, ou le groupe des pistes d'un même stem.

    On soustrait le groupe entier, jamais une voix seule : une voix sur
    quatre comparée au stem entier serait toujours « peu corrélée », non
    parce qu'elle est fausse mais parce qu'elle est un quart (CDC § 2.1).
    """
    nom: str
    membres: List[object]           # les ExportTrack, tels que le rendu final les joue
    stem: np.ndarray                # le stem séparé contre lequel les membres ont été jugés (mono)
    part: float                     # part d'énergie de ce stem dans le mélange D'ORIGINE, en %
    distance: Optional[float]       # distance de piste (moyenne des membres) ; None = inconnue
    iteration: int = 0              # 0 : la chaîne d'aujourd'hui ; k : issue du résidu k

    @property
    def score(self) -> Optional[float]:
        """part / distance : la piste la plus proche de son stem, pondérée par
        ce qu'elle pèse (CDC § 2.3). Sans distance, pas de score — et pas de
        soustraction : on ne soustrait pas ce qu'on n'a pas su juger."""
        if self.distance is None or self.distance <= 0.0:
            return None
        return float(self.part) / float(self.distance)


@dataclass
class DejaPortees:
    """Les notes et les frappes que les pistes retenues jouent déjà."""
    notes: List[Tuple[int, float]] = field(default_factory=list)   # (hauteur MIDI, attaque en s)
    frappes: List[float] = field(default_factory=list)              # attaques en s


@dataclass
class Options:
    iterations: int
    correlation: float = 0.5     # garde-fou : corrélation minimale du rendu à son stem
    energie: float = 5.0         # arrêt : part du mélange d'origine sous laquelle le résidu est vide
    notes_min: int = 8           # une piste du résidu se fait d'au moins autant de notes NOUVELLES


@dataclass
class Passe:
    """Ce que la reconstruction d'un résidu rend à la boucle."""
    unites: List[Unite] = field(default_factory=list)
    pistes_ajoutees: List[dict] = field(default_factory=list)
    stems_refuses: List[dict] = field(default_factory=list)
    partage: List[dict] = field(default_factory=list)
    drums: Optional[dict] = None
    secondes: float = 0.0


@dataclass
class Collaborateurs:
    """Les quatre gestes que la boucle ne sait pas faire elle-même."""
    # (unité, longueur en échantillons) -> rendu MONO des membres ensemble, ou None
    rendre: Callable[[Unite, int], Optional[np.ndarray]]
    # (résidu.wav, dossier des stems) -> nom de stem -> fichier
    separer: Callable[[Path, Path], Dict[str, Path]]
    # (stems du résidu, itération, déjà portées) -> la passe
    reconstruire: Callable[[Dict[str, Path], int, DejaPortees], Passe]
    # () -> distance du projet en l'état (toutes les pistes retenues, avant verdict)
    distance_projet: Callable[[], float]
    # () -> ce que les pistes retenues jouent déjà
    deja_portees: Callable[[], DejaPortees]
    journal: Callable[[str], None] = print


# ---------------------------------------------------------------------------
# Arithmétique
# ---------------------------------------------------------------------------

def energie(x: np.ndarray) -> float:
    return float(np.sum(np.square(np.asarray(x, dtype=np.float64))))


def correlation(a: np.ndarray, b: np.ndarray) -> float:
    """Cosinus non centré, la définition du banc (`vsm_banc._correlation`) :
    deux chiffres qui portent le même nom se calculent de la même façon."""
    n = min(a.size, b.size)
    if n == 0:
        return float("nan")
    a64 = np.asarray(a[:n], dtype=np.float64)
    b64 = np.asarray(b[:n], dtype=np.float64)
    na, nb = float(np.sqrt(np.sum(a64 * a64))), float(np.sqrt(np.sum(b64 * b64)))
    if na <= 0.0 or nb <= 0.0:
        return float("nan")
    return float(np.dot(a64, b64) / (na * nb))


def decaler(rendu: np.ndarray, decalage: int, longueur: int) -> np.ndarray:
    """r_d[i] = r[i − d], sur `longueur` échantillons, zéro hors du rendu.
    Un décalage positif retarde le rendu (il arrive plus tard dans le mélange)."""
    sortie = np.zeros(longueur, dtype=np.float64)
    source = np.asarray(rendu, dtype=np.float64)
    debut = max(0, decalage)
    fin = min(longueur, source.size + decalage)
    if fin > debut:
        sortie[debut:fin] = source[debut - decalage:fin - decalage]
    return sortie


def aligner(cible: np.ndarray, rendu: np.ndarray,
            fenetre: int = FENETRE_ALIGNEMENT) -> Tuple[int, float]:
    """Le décalage entier et le gain qui rapprochent le rendu de la cible.

    Le décalage est le maximum de la corrélation croisée dans ±fenetre,
    calculée par FFT (une transformée sur 24 millions d'échantillons vaut
    quelques secondes ; 4 411 produits scalaires en vaudraient une minute).
    Le gain est celui des moindres carrés au décalage retenu :
    g = <cible, r_d> / <r_d, r_d>. Pas de filtrage, pas d'égalisation : un
    scalaire et un entier, publiés (CDC § 2.4).
    """
    c = np.asarray(cible, dtype=np.float64)
    r = np.asarray(rendu, dtype=np.float64)
    n = c.size
    if n == 0 or r.size == 0 or not np.any(r):
        return 0, 0.0
    taille = 1
    while taille < n + r.size + 2 * fenetre:
        taille *= 2
    spectre_c = np.fft.rfft(c, taille)
    spectre_r = np.fft.rfft(r, taille)
    # corr[d] = Σ_i c[i] · r[i − d] : la corrélation croisée, d de −fenetre à
    # +fenetre, lue dans une transformée circulaire par le tour de l'indice.
    corr = np.fft.irfft(spectre_c * np.conj(spectre_r), taille)
    lags = np.arange(-fenetre, fenetre + 1)
    valeurs = corr[lags % taille]
    meilleur = int(lags[int(np.argmax(valeurs))])
    r_d = decaler(r, meilleur, n)
    denominateur = float(np.dot(r_d, r_d))
    if denominateur <= 0.0:
        return meilleur, 0.0
    return meilleur, float(np.dot(c, r_d) / denominateur)


def soustraire(cible: np.ndarray, rendu: np.ndarray, decalage: int, gain: float) -> np.ndarray:
    """cible − gain · r_d, en float32 comme tout ce que la chaîne lit.

    Le calcul se fait en float64 puis se replie : un mélange qui EST le rendu
    d'une piste, moins ce rendu au gain 1 et au décalage 0, donne un résidu
    NUL au bit près (testé) — c'est la propriété qui dit que la soustraction
    ne fabrique rien.
    """
    c = np.asarray(cible, dtype=np.float64)
    return (c - float(gain) * decaler(rendu, decalage, c.size)).astype(np.float32)


# ---------------------------------------------------------------------------
# Les notes déjà portées (CDC § 2.5)
# ---------------------------------------------------------------------------

def indices_nouveaux(paires: Sequence[Tuple[int, float]], deja: Sequence[Tuple[int, float]],
                     tolerance_hauteur: int = TOLERANCE_HAUTEUR,
                     tolerance_attaque: float = TOLERANCE_ATTAQUE_S) -> List[int]:
    """Les indices des (hauteur, attaque) qu'aucune note déjà portée ne couvre."""
    if not deja:
        return list(range(len(paires)))
    ordonnees = sorted((float(t), int(n)) for n, t in deja)
    instants = [t for t, _ in ordonnees]
    nouveaux = []
    for indice, (hauteur, attaque) in enumerate(paires):
        gauche = bisect.bisect_left(instants, float(attaque) - tolerance_attaque)
        droite = bisect.bisect_right(instants, float(attaque) + tolerance_attaque)
        portee = any(abs(int(hauteur) - ordonnees[i][1]) <= tolerance_hauteur
                     for i in range(gauche, droite))
        if not portee:
            nouveaux.append(indice)
    return nouveaux


def indices_frappes_nouvelles(attaques: Sequence[float], deja: Sequence[float],
                              tolerance: float = TOLERANCE_FRAPPE_S) -> List[int]:
    """Les indices des attaques qu'aucune frappe déjà portée ne couvre, quelle
    que soit la pièce : un coup au même instant est le même coup."""
    if not deja:
        return list(range(len(attaques)))
    instants = sorted(float(t) for t in deja)
    nouveaux = []
    for indice, attaque in enumerate(attaques):
        gauche = bisect.bisect_left(instants, float(attaque) - tolerance)
        if gauche >= len(instants) or instants[gauche] > float(attaque) + tolerance:
            nouveaux.append(indice)
    return nouveaux


# ---------------------------------------------------------------------------
# Le résidu sur disque : un WAV float32 minimal, mêmes échantillons → mêmes
# octets (pas de bloc PEAK horodaté), lisible par la chaîne et par demucs.
# ---------------------------------------------------------------------------

def ecrire_residu(chemin: Path, audio: np.ndarray, sample_rate: int) -> None:
    donnees = np.ascontiguousarray(audio, dtype=np.float32)
    brut = donnees.astype("<f4").tobytes()
    entete = b"RIFF" + struct.pack("<I", 36 + len(brut)) + b"WAVE"
    entete += b"fmt " + struct.pack("<IHHIIHH", 16, 3, 1, sample_rate, sample_rate * 4, 4, 32)
    entete += b"data" + struct.pack("<I", len(brut))
    Path(chemin).parent.mkdir(parents=True, exist_ok=True)
    Path(chemin).write_bytes(entete + brut)


# ---------------------------------------------------------------------------
# La boucle
# ---------------------------------------------------------------------------

def _ms(echantillons: int, sample_rate: int) -> float:
    return 1000.0 * echantillons / float(sample_rate)


def _candidate(unite: Unite, residu: np.ndarray, rendu: Optional[np.ndarray],
               options: Options, sample_rate: int) -> dict:
    """Une candidate, mesurée : alignée sur le résidu, corrélée à son stem et
    au reste, et jugée par le garde-fou. Tout est publié, retenue ou non."""
    fiche: Dict[str, object] = {
        "unite": unite.nom, "membres": [getattr(m, "name", str(m)) for m in unite.membres],
        "iteration": unite.iteration, "part": unite.part, "distance": unite.distance,
        "score": unite.score, "retenue": False,
    }
    if rendu is None or rendu.size == 0 or not np.any(rendu):
        fiche["motif"] = "rendu impossible ou muet"
        return fiche
    decalage, gain = aligner(residu, rendu)
    r_d = decaler(rendu, decalage, residu.size)
    n = min(residu.size, unite.stem.size)
    reste = np.asarray(residu[:n], dtype=np.float64) - np.asarray(unite.stem[:n], dtype=np.float64)
    fiche.update({
        "decalageEchantillons": int(decalage), "decalageMs": round(_ms(decalage, sample_rate), 3),
        "gain": gain, "correlationStem": correlation(unite.stem, r_d),
        "correlationReste": correlation(reste, r_d[:n]),
    })
    fiche["_rendu"] = rendu
    if unite.score is None:
        fiche["motif"] = "sans distance de piste : pas de score, pas de soustraction"
    elif gain <= 0.0:
        fiche["motif"] = "gain non positif : rendu sans rapport avec le résidu, ou en opposition"
    elif not (fiche["correlationStem"] >= options.correlation):
        fiche["motif"] = (f"corrélation au stem {fiche['correlationStem']:.3f} sous le seuil "
                          f"de {options.correlation:.2f}")
    else:
        fiche["motif"] = "sûre"
        fiche["retenue"] = True
    return fiche


def _publique(fiche: dict) -> dict:
    return {k: v for k, v in fiche.items() if not k.startswith("_")}


def boucle_residuelle(melange: np.ndarray, unites: List[Unite], options: Options,
                      collab: Collaborateurs, travail: Path, sample_rate: int) -> dict:
    """Le pas de boucle du CDC § 2.2, N fois au plus, et son rapport.

    Modifie `unites` en place (les unités issues d'un résidu s'y ajoutent, et
    concourent aux itérations suivantes). Rend le bloc `residuel` de
    rapport.json ; l'appelant y ajoute ce qu'il sait (chemins, provenance).
    """
    journal = collab.journal
    m = np.asarray(melange, dtype=np.float32)
    e0 = energie(m)
    rapport: Dict[str, object] = {
        "demande": int(options.iterations),
        "options": {"correlation": options.correlation, "energie": options.energie,
                    "notesMin": options.notes_min},
        "energieMelange": e0,
        "iterations": [],
        "arret": None,
    }

    def arreter(motif: str, detail: str, iteration: int) -> None:
        rapport["arret"] = {"motif": motif, "detail": detail, "iteration": iteration}
        journal(f"      résiduel : ARRÊT à l'itération {iteration} — {motif} : {detail}")

    journal(f"      résiduel : {options.iterations} itération(s) demandée(s), "
            f"{len(unites)} unité(s) candidates, garde-fou corrélation ≥ {options.correlation:.2f}, "
            f"arrêt sous {options.energie:.1f} % d'énergie, {options.notes_min} notes nouvelles au moins")
    distance_avant = collab.distance_projet()
    rapport["distanceProjetInitiale"] = distance_avant
    journal(f"      résiduel : distance du projet en l'état, avant toute soustraction : {distance_avant:.4f}")

    soustraites: List[int] = []   # identités (id()) des unités déjà soustraites
    for k in range(1, options.iterations + 1):
        depart = time.perf_counter()
        it: Dict[str, object] = {"iteration": k, "candidats": [], "secondes": {}}
        rapport["iterations"].append(it)

        # a. Choisir la plus sûre : tout est rendu, tout est publié.
        debut_rendus = time.perf_counter()
        fiches = []
        for unite in unites:
            if id(unite) in soustraites:
                continue
            fiche = _candidate(unite, m, collab.rendre(unite, m.size), options, sample_rate)
            fiches.append(fiche)
            etat = "SÛRE" if fiche["retenue"] else "écartée"
            score_txt = "   —   " if fiche["score"] is None else f"{fiche['score']:7.2f}"
            if "correlationStem" in fiche:
                journal(f"      résiduel r{k} : {unite.nom:20s} {etat:8s} score {score_txt} "
                        f"corr. stem {fiche['correlationStem']:.3f} reste {fiche['correlationReste']:.3f} "
                        f"gain {fiche['gain']:.3f} décalage {fiche['decalageEchantillons']:+d} éch. "
                        f"({fiche['decalageMs']:+.2f} ms) — {fiche['motif']}")
            else:
                journal(f"      résiduel r{k} : {unite.nom:20s} {etat:8s} — {fiche['motif']}")
        it["candidats"] = [_publique(f) for f in fiches]
        it["secondes"]["rendus"] = round(time.perf_counter() - debut_rendus, 1)
        sures = [f for f in fiches if f["retenue"]]
        if not sures:
            meilleure = max((f for f in fiches if "correlationStem" in f),
                            key=lambda f: f["correlationStem"], default=None)
            detail = (f"aucune des {len(fiches)} candidate(s) ne passe le garde-fou"
                      + (f" ; la meilleure corrélation au stem est {meilleure['correlationStem']:.3f} "
                         f"({meilleure['unite']})" if meilleure else ""))
            it["secondes"]["total"] = round(time.perf_counter() - depart, 1)
            arreter("aucune-piste-sure", detail, k)
            break
        choisie = max(sures, key=lambda f: (f["score"], f["unite"]))
        unite = next(u for u in unites if u.nom == choisie["unite"] and id(u) not in soustraites)

        # b. Soustraire — UNE unité par itération, chaque itération a une variable.
        e_avant = energie(m)
        m = soustraire(m, choisie["_rendu"], int(choisie["decalageEchantillons"]), float(choisie["gain"]))
        e_apres = energie(m)
        soustraites.append(id(unite))
        it["soustraction"] = _publique(choisie)
        it["energie"] = {"avant": e_avant, "apres": e_apres,
                         "partAvant": 100.0 * e_avant / e0 if e0 > 0 else 0.0,
                         "partApres": 100.0 * e_apres / e0 if e0 > 0 else 0.0}
        journal(f"      résiduel r{k} : SOUSTRAIT « {unite.nom} » (itération {unite.iteration}, "
                f"{len(unite.membres)} piste(s)) — décalage {choisie['decalageEchantillons']:+d} éch., "
                f"gain {choisie['gain']:.3f}, corrélation au stem {choisie['correlationStem']:.3f} ; "
                f"énergie du résidu {it['energie']['partAvant']:.1f} % → {it['energie']['partApres']:.1f} % "
                f"du mélange")

        # c. Le résidu sur disque : un objet de mesure, jamais joué.
        dossier = Path(travail) / f"residu-r{k}"
        residu_wav = dossier / "residu.wav"
        ecrire_residu(residu_wav, m, sample_rate)
        it["residu"] = str(residu_wav)
        if it["energie"]["partApres"] < options.energie:
            arreter("residu-sous-le-seuil",
                    f"le résidu porte {it['energie']['partApres']:.2f} % du mélange, "
                    f"sous les {options.energie:.1f} % : rien à y chercher", k)
            it["secondes"]["total"] = round(time.perf_counter() - depart, 1)
            break

        # d. Reséparer, et relancer la chaîne sur les stems du résidu seulement.
        debut_separation = time.perf_counter()
        stems = collab.separer(residu_wav, dossier / "stems")
        it["secondes"]["separation"] = round(time.perf_counter() - debut_separation, 1)
        it["stems"] = {nom: str(chemin) for nom, chemin in stems.items()}
        if not stems:
            it["secondes"]["total"] = round(time.perf_counter() - depart, 1)
            arreter("rien-de-discernable", "la séparation du résidu n'a rendu aucun stem", k)
            break
        debut_passe = time.perf_counter()
        passe = collab.reconstruire(stems, k, collab.deja_portees())
        it["secondes"]["reconstruction"] = round(time.perf_counter() - debut_passe, 1)
        it["partage"] = passe.partage
        it["stemsRefuses"] = passe.stems_refuses
        it["pistesAjoutees"] = passe.pistes_ajoutees
        if passe.drums:
            it["drums"] = passe.drums
        unites.extend(passe.unites)
        it["secondes"]["total"] = round(time.perf_counter() - depart, 1)
        if not passe.pistes_ajoutees:
            arreter("rien-de-discernable",
                    f"le résidu séparé n'a donné aucune piste ({len(passe.stems_refuses)} stem(s) "
                    f"refusé(s), voir stemsRefuses)", k)
            break
        journal(f"      résiduel r{k} : {len(passe.pistes_ajoutees)} piste(s) ajoutée(s) — "
                + ", ".join(p["piste"] for p in passe.pistes_ajoutees)
                + (f" ; {len(passe.stems_refuses)} stem(s) refusé(s)" if passe.stems_refuses else ""))

        # e. La distance du projet en l'état : elle arrête, elle ne défait pas (CDC § 5).
        distance_apres = collab.distance_projet()
        it["distanceProjet"] = {"avant": distance_avant, "apres": distance_apres}
        journal(f"      résiduel r{k} : distance du projet en l'état {distance_avant:.4f} → {distance_apres:.4f}")
        if not (distance_apres < distance_avant):
            arreter("distance-sans-gain",
                    f"les pistes de l'itération ne baissent pas la distance du projet "
                    f"({distance_avant:.4f} → {distance_apres:.4f}) ; elles sont GARDÉES, "
                    f"l'écart se publie (CDC multipiste § 0)", k)
            break
        distance_avant = distance_apres
        if k == options.iterations:
            arreter("iterations-atteintes", f"{options.iterations} itération(s) demandée(s), faites", k)
    return rapport
