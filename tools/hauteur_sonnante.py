#!/usr/bin/env python3
"""À quelle hauteur une note du corpus SONNE-t-elle vraiment ?

POURQUOI CE MODULE EXISTE (13/09/2026, D267-D268). `verite.json` donne les notes
ÉCRITES par le générateur de corpus. Le transcripteur, lui, entend la hauteur
SONNANTE — et les deux diffèrent quand le patch tiré au hasard désaccorde un
oscillateur, ce qui arrive pour **9,9 % des notes mélodiques** du corpus `s1-sec`.

ET L'UNITÉ SE LIT, ELLE NE SE SUPPOSE PAS. Tout paramètre nommé `…detune` n'est
pas en demi-tons :

  * `vsm.psg` · `oscillator.2.detune`          → **cents** (37,15 vaut 0,37 st)
  * `vsm.obx` · `voice.unisonDetune`           → normalisé 0-1, ne déplace RIEN
  * `vsm.supersaw` · `oscillator.supersaw.detune` → normalisé 0-1, idem

Supposer les demi-tons a fait publier « 12,7 % des notes » là où il faut lire
9,9 %. Ce module est donc la SEULE implémentation de la règle : deux copies
divergent, et c'est ce qui était arrivé — `corpus-hauteurs.py` lisait l'unité
pendant que `confiance-contre-verite.py` la supposait encore.

L'unité se trouve en joignant deux fichiers du dépôt : `ParameterDescriptor.cpp`
donne le nom d'affichage d'un identifiant sémantique, et la table de la machine
(`audio/plugins/<machine>/*.cpp`) donne l'unité de ce nom.
"""
from __future__ import annotations

import re
from collections import defaultdict
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
DESCRIPTEURS = RACINE / "interchange/src/ParameterDescriptor.cpp"
PLUGINS = RACINE / "audio/plugins"

_noms_par_id: dict[str, set[str]] | None = None
_unites: dict[tuple[str, str], str] = {}


def _noms(clef: str) -> set[str]:
    global _noms_par_id
    if _noms_par_id is None:
        _noms_par_id = defaultdict(set)
        texte = DESCRIPTEURS.read_text(encoding="utf-8", errors="replace")
        for nom, sid in re.findall(r'\{"([^"]+)",\s*"([^"]+)"\}', texte):
            _noms_par_id[sid].add(nom)
    return _noms_par_id.get(clef, set())


def unite_du_parametre(machine: str, clef: str) -> str:
    """« st », « cents », ou chaîne vide si la machine n'en déclare pas."""
    if (machine, clef) in _unites:
        return _unites[(machine, clef)]
    court = machine.split(".")[-1]
    unite = ""
    for fichier in sorted(PLUGINS.glob(f"{court}/*.cpp")):
        contenu = fichier.read_text(encoding="utf-8", errors="replace")
        for nom in _noms(clef):
            m = re.search(r'\{\s*k\w+,\s*"' + re.escape(nom) + r'"\s*,[^}]*?"([^"]*)"\s*\}', contenu)
            if m:
                unite = m.group(1)
                break
        if unite:
            break
    _unites[(machine, clef)] = unite
    return unite


def en_demi_tons(machine: str, clef: str, valeur: float) -> float | None:
    """La valeur en DEMI-TONS, ou None si ce paramètre ne déplace pas la hauteur."""
    unite = unite_du_parametre(machine, clef)
    if unite == "st":
        return float(valeur)
    if unite == "cents":
        return float(valeur) / 100.0
    return None


def desaccords(partie: dict, seuil: float = 0.25) -> list[tuple[str, float]]:
    """(nom du paramètre, désaccord en demi-tons) — TOUS, du plus grand au plus petit.

    Tous, et non « le plus grand » : une machine HYBRIDE a deux couches à deux
    hauteurs (`sample.1.tune` et `oscillator.1.detune` sur `vsm.pcmhybrid`), et le
    transcripteur en suit l'une ou l'autre.
    """
    machine = str(partie.get("machine", ""))
    trouves = []
    for clef, valeur in (partie.get("patch") or {}).items():
        c = clef.lower()
        if "detune" not in c and not c.endswith(".tune"):
            continue
        demi = en_demi_tons(machine, clef, float(valeur))
        if demi is not None and abs(demi) > seuil:
            trouves.append((clef, demi))
    return sorted(trouves, key=lambda kv: -abs(kv[1]))


def hauteurs_sonnantes(partie: dict, hauteur: int) -> list[int]:
    """La hauteur écrite ET chaque hauteur où le patch la fait sonner."""
    valeurs = {hauteur}
    for _, demi in desaccords(partie):
        valeurs.add(hauteur + int(round(demi)))
    return sorted(valeurs)
