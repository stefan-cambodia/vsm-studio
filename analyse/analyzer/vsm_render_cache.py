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
from typing import Optional, Tuple

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
