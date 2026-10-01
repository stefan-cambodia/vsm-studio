# -*- coding: utf-8 -*-
"""Cache de MESURES de candidates, sur disque, entre exécutions (hypothèse H2).

CE QUE LE § 5 DUODECIES A CONSTATÉ : entre deux exécutions comparées, les
candidates d'usine sont rendues À L'IDENTIQUE — moteur déterministe, graine
fixe — et repayées à chaque fois.

CE QUE LA PREMIÈRE VERSION A APPRIS EN UNE COURSE : cacher l'AUDIO coûtait
9,4 Go pour un seul morceau (83 Mo par candidate de huit minutes), et ne
faisait rien gagner sur la mesure de distance, restée en série. Ce qu'un
arbitrage consomme d'une candidate tient en DEUX NOMBRES : son niveau
efficace (le filtre « peut-elle atteindre le stem ») et sa distance à la
cible. C'est cela qu'on cache — quelques octets — et un hit économise le
rendu ET la distance.

LA CLÉ DIT TOUT CE QUI PEUT CHANGER CES DEUX NOMBRES, ET RIEN D'AUTRE :
côté candidate — machine, profil, patch, notes, durée, fréquence, tempo, et
l'EMPREINTE DU MOTEUR (un cache qui survivrait à un nouveau `vsm-render`
servirait les mesures d'hier avec l'autorité d'aujourd'hui, la panne que la
fraîcheur d'A4.1 attrape pour les modèles) ; côté mesure — la MÉTRIQUE et
l'empreinte de la CIBLE, puisque la distance dépend des deux.

Le cache vit dans `cache/mesures/` à la racine du dépôt, comme `corpus/` et
`modeles/` : regénérable, ignoré par git.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Optional, Sequence, Tuple

import numpy as np

from .vsm_engine import find_vsm_render


def _empreinte_moteur(binary: Optional[str]) -> str:
    """Empreinte du binaire de rendu, calculée UNE fois par exécution."""
    chemin = str(find_vsm_render(binary))
    if chemin not in _empreintes:
        h = hashlib.sha256()
        with open(chemin, "rb") as f:
            for bloc in iter(lambda: f.read(1 << 20), b""):
                h.update(bloc)
        _empreintes[chemin] = h.hexdigest()[:16]
    return _empreintes[chemin]


_empreintes: dict = {}


def dossier_du_cache() -> Path:
    racine = Path(__file__).resolve().parent.parent.parent
    return racine / "cache" / "mesures"


def cle_de_rendu(track, sample_rate: int, duration, tempo: float,
                 binary: Optional[str]) -> str:
    """La clé d'un rendu de piste : tout ce qui peut le changer, rien d'autre."""
    notes = [(n.note, n.velocity, round(n.start, 9), round(n.duration, 9))
             for n in track.notes]
    descripteur = {
        "machine": track.machine,
        "profile": track.profile,
        "parameters": sorted(track.parameters.items()),
        "notes": notes,
        "samples": (sorted((int(k), str(v)) for k, v in track.samples.items())
                     if getattr(track, "samples", None) else []),
        "channel": getattr(track, "channel", 0),
        "isDrums": bool(getattr(track, "is_drums", False)),
        "duration": duration,
        "sampleRate": sample_rate,
        "tempo": tempo,
        "moteur": _empreinte_moteur(binary),
    }
    # H42 : LE DIAPASON DE LA COURSE change chaque rendu mélodique, et il n'était
    # pas dans la clé — deux courses du même binaire, l'une à 440 et l'autre à
    # 443, se seraient servi les mesures l'une de l'autre, et l'A/B aurait lu
    # « aucun effet ». Inscrit SEULEMENT s'il diffère de 440 : les clés d'avant
    # restent celles d'aujourd'hui, et le cache déjà payé reste valable.
    from . import diapason
    if diapason.valeur() != 440.0:
        descripteur["diapason"] = diapason.valeur()
    texte = json.dumps(descripteur, sort_keys=True, ensure_ascii=True)
    return hashlib.sha256(texte.encode("ascii")).hexdigest()


# Le rendu qu'un dossier de projet reçoit du moteur : une SORTIE, jamais une entrée.
_SORTIES_DE_RENDU = frozenset({"rendu.wav"})

# Ce que la course a fait de ses mesures de PROJET (H48) : dit au journal et au
# rapport, parce qu'une mesure relue n'est pas une mesure payée, et qu'une course
# qui a repris après une mort doit pouvoir le montrer.
COMPTE_PROJET = {"relues": 0, "payees": 0}

# H58 : ce que la course a fait des niveaux de ses rendus SOLO — ceux du calage de
# niveau, que le verdict et le réglage au mélange rejouent à chaque évaluation (97 % de
# ce que coûtait encore un réglage tout relu, § 10.3 du cahier des charges de « Reload »).
COMPTE_NIVEAU = {"relues": 0, "payees": 0}

# H58 : LA COURSE POSE UNE FOIS si elle range ses niveaux solo, comme elle pose son
# moteur (H48). Le calage est appelé de quatre endroits dans trois modules ; un drapeau
# passé de main en main en oublierait un, et l'oubli rendrait en silence.
_NIVEAUX_DE_COURSE = {"actif": False}


def poser_cache_des_niveaux(actif: bool) -> None:
    _NIVEAUX_DE_COURSE["actif"] = bool(actif)


def cache_des_niveaux_actif() -> bool:
    return bool(_NIVEAUX_DE_COURSE["actif"])


def cle_de_niveau(cles_de_projet: Sequence[str], duree: float, echantillons_stem: int) -> str:
    """La clé du niveau d'un rendu solo (une clé de projet) ou de la SOMME d'un groupe
    (les clés de ses membres, DANS L'ORDRE où elles s'additionnent), pour une durée
    rendue et un stem de `echantillons_stem` échantillons — le niveau se prend sur
    `min(stem, rendu)`. Le CONTENU du stem n'y entre pas : le niveau du rendu n'en
    dépend pas."""
    descripteur = {"nature": "niveau-solo", "projets": list(cles_de_projet),
                   "duree": repr(float(duree)), "stem": int(echantillons_stem)}
    texte = json.dumps(descripteur, sort_keys=True, ensure_ascii=True)
    return hashlib.sha256(texte.encode("ascii")).hexdigest()


def niveau_en_cache(cle: str) -> Optional[Tuple[float, int]]:
    """(niveau efficace, nombre d'échantillons sur lequel il est pris), ou None."""
    chemin = dossier_du_cache() / f"{cle}.json"
    if not chemin.is_file():
        return None
    try:
        d = json.loads(chemin.read_text(encoding="ascii"))
        return float(d["rms"]), int(d["n"])
    except Exception:  # noqa: BLE001 - un fichier corrompu vaut une absence
        chemin.unlink(missing_ok=True)
        return None


def stocker_niveau(cle: str, rms: float, n: int) -> None:
    dossier = dossier_du_cache()
    dossier.mkdir(parents=True, exist_ok=True)
    temporaire = dossier / f".{cle}.tmp"
    temporaire.write_text(json.dumps({"rms": rms, "n": int(n)}), encoding="ascii")
    temporaire.replace(dossier / f"{cle}.json")

_empreintes_de_fichier: dict = {}


def _empreinte_de_fichier(chemin: Path) -> str:
    """Empreinte du CONTENU d'un fichier. Un gros fichier (le report vocal, des
    dizaines de mégaoctets, relu à chaque évaluation) n'est haché qu'une fois tant
    que sa taille et sa date ne bougent pas ; un petit l'est à chaque fois."""
    etat = chemin.stat()
    gros = etat.st_size >= (1 << 20)
    repere = (str(chemin), etat.st_size, etat.st_mtime_ns)
    if gros and repere in _empreintes_de_fichier:
        return str(_empreintes_de_fichier[repere])
    h = hashlib.sha256()
    with open(chemin, "rb") as f:
        for bloc in iter(lambda: f.read(1 << 20), b""):
            h.update(bloc)
    empreinte = h.hexdigest()
    if gros:
        _empreintes_de_fichier[repere] = empreinte
    return empreinte


def cle_de_projet(folder: Path, sample_rate: int, binary: Optional[str]) -> str:
    """La clé d'un rendu de PROJET (H48) : tout ce que le moteur va LIRE dans le
    dossier — chaque fichier, par son chemin relatif et son CONTENU —, la fréquence
    demandée et l'empreinte du moteur.

    POURQUOI LE DOSSIER ET NON LES PISTES. La clé d'un rendu de piste
    (`cle_de_rendu`) énumère les champs d'une piste, et elle en a déjà oublié un :
    le diapason (H42, § 4.2 du cahier des charges de « Reload »). Un projet en
    porte bien davantage — volumes, panoramiques, effets, automation, routage des
    groupes, échantillons — et la liste s'allongera. Le dossier écrit par
    `write_project_bundle` EST ce que le moteur lit : le hacher ne peut rien
    oublier de ce qui s'y trouve, aujourd'hui ni demain.

    CE QUE LA CLÉ NE VOIT PAS, comme celle d'une piste : le CONTENU d'un profil
    multi-échantillons installé, que le projet désigne par son nom. Réinstaller
    un profil sous le même nom demande de vider `cache/mesures/`.

    Un fichier de trop dans le dossier (le preset d'une variante précédente) ne
    peut que faire MANQUER un hit, jamais en servir un faux.
    """
    folder = Path(folder)
    h = hashlib.sha256()
    for chemin in sorted(p for p in folder.rglob("*") if p.is_file()):
        relatif = chemin.relative_to(folder).as_posix()
        if relatif in _SORTIES_DE_RENDU:
            continue
        h.update(relatif.encode("utf-8") + b"\0" + _empreinte_de_fichier(chemin).encode("ascii") + b"\0")
    descripteur = {
        "nature": "projet",
        "dossier": h.hexdigest(),
        "sampleRate": sample_rate,
        "moteur": _empreinte_moteur(binary),
    }
    texte = json.dumps(descripteur, sort_keys=True, ensure_ascii=True)
    return hashlib.sha256(texte.encode("ascii")).hexdigest()


def empreinte_de_cible(stem_audio: np.ndarray) -> str:
    """Empreinte du stem cible : la distance dépend de lui autant que du rendu."""
    x = np.ascontiguousarray(np.asarray(stem_audio, dtype=np.float32))
    return hashlib.sha256(x.tobytes()).hexdigest()[:24]


def cle_de_mesure(cle_rendu: str, metric: str, empreinte_cible: str) -> str:
    return hashlib.sha256(f"{cle_rendu}|{metric}|{empreinte_cible}"
                          .encode("ascii")).hexdigest()


def mesure_en_cache(cle: str) -> Optional[Tuple[float, float]]:
    """(niveau efficace, distance) d'une candidate déjà mesurée, ou None."""
    chemin = dossier_du_cache() / f"{cle}.json"
    if not chemin.is_file():
        return None
    try:
        d = json.loads(chemin.read_text(encoding="ascii"))
        return float(d["rms"]), float(d["distance"])
    except Exception:  # noqa: BLE001 - un fichier corrompu vaut une absence
        chemin.unlink(missing_ok=True)
        return None


def stocker_mesure(cle: str, rms: float, distance: float) -> None:
    dossier = dossier_du_cache()
    dossier.mkdir(parents=True, exist_ok=True)
    # Écrit à côté puis bascule : la règle de la sauvegarde automatique
    # (D10.4), pour qu'un processus interrompu ne laisse pas une mesure
    # tronquée qu'un autre prendrait pour bonne.
    temporaire = dossier / f".{cle}.tmp"
    temporaire.write_text(json.dumps({"rms": rms, "distance": distance}),
                          encoding="ascii")
    temporaire.replace(dossier / f"{cle}.json")
