# -*- coding: utf-8 -*-
"""Cache de rendus de piste, sur disque, entre exécutions (hypothèse H2).

CE QUE LE § 5 DUODECIES A CONSTATÉ : entre deux exécutions comparées, les
candidates d'usine sont rendues À L'IDENTIQUE — moteur déterministe, graine
fixe — et repayées à chaque fois. Le fan-out des profils porte
`vsm.multisample` seul à trente et une candidates par stem : c'est ~15 s
pièce de rendu strictement rejoué.

LA CLÉ DIT TOUT CE QUI PEUT CHANGER LE RENDU, ET RIEN D'AUTRE : machine,
profil, patch, notes (chaque champ qui traverse `write_project_bundle`),
durée imposée, fréquence, tempo — et l'EMPREINTE DU MOTEUR : un cache qui
survivrait à un nouveau `vsm-render` servirait les rendus d'hier avec
l'autorité d'aujourd'hui, exactement la panne que la vérification de
fraîcheur d'A4.1 attrape pour les modèles. La distance n'est PAS mise en
cache ici : elle dépend de la cible et de la métrique, qui ont leurs
propres caches.

Le cache vit dans `cache/rendus/` à la racine du dépôt, comme `corpus/` et
`modeles/` : regénérable, ignoré par git.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Optional, Sequence

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
    return racine / "cache" / "rendus"


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


def rendu_en_cache(cle: str) -> Optional[np.ndarray]:
    chemin = dossier_du_cache() / f"{cle}.npy"
    if not chemin.is_file():
        return None
    try:
        return np.load(chemin)
    except Exception:  # noqa: BLE001 - un fichier corrompu vaut une absence
        chemin.unlink(missing_ok=True)
        return None


def stocker_rendu(cle: str, audio: np.ndarray) -> None:
    dossier = dossier_du_cache()
    dossier.mkdir(parents=True, exist_ok=True)
    # Écrit à côté puis bascule : la règle de la sauvegarde automatique
    # (D10.4), pour qu'un processus interrompu ne laisse pas un rendu tronqué
    # qu'un autre prendrait pour bon.
    temporaire = dossier / f".{cle}.tmp.npy"
    np.save(temporaire, audio.astype(np.float32))
    temporaire.replace(dossier / f"{cle}.npy")
