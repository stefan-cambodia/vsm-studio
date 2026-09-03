"""Une course est une photographie du code à son départ : tous les modules
de la chaîne sont importés AVANT la première étape, pour qu'aucune importation
à la demande ne lise plus tard sur le disque un fichier réécrit entre-temps
(usandthem-parite-v2, 03/09/2026 : 5 h 24 de course perdues au réglage final).
"""
from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_true, run, test  # noqa: E402

import reconstruire  # noqa: E402


@test
def tous_les_modules_de_la_chaine_sont_charges_au_depart():
    reconstruire.charger_tous_les_modules()
    for nom in ("analyzer.vsm_mix_refine", "analyzer.vsm_voix", "analyzer.vsm_levels",
                "analyzer.note_extraction", "analyzer.vsm_distance_cache", "analyzer.vsm_corpus"):
        assert_true(nom in sys.modules, nom + " doit être en mémoire dès le départ")


if __name__ == "__main__":
    raise SystemExit(run())
