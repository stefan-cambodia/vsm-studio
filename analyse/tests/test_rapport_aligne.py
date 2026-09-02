"""Le rapport doit décrire le projet qu'on ÉCRIT, champ par champ.

Ces tests verrouillent une panne muette trouvée le 02/09/2026, et qui avait
fait écrire une conclusion fausse dans trois documents du dépôt.

Quand le verdict du mélange remplace la machine d'une piste,
`aligner_rapport_sur_projet` mettait à jour `machine`, `parameters` et
`trackDistance` — mais NI la distance au stem NI le profil. Le rapport
publiait donc le score de la machine ÉCARTÉE sous le nom de la nouvelle.
Mesuré sur `usandthem-v9` : « vsm.cs80, distance 0,185085 » alors que la
vraie distance de `vsm.cs80` au stem de basse est 0,362272 — la valeur
publiée était celle de `vsm.multisample`, qui avait gagné l'arbitrage avant
d'être écartée.

On en avait conclu que les deux machines étaient « à égalité au dix-millième »
et que le mélange départageait des ex æquo. La vérité est plus forte : le
mélange RENVERSE un écart de un à deux.
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer.vsm_project_export import ExportTrack  # noqa: E402
from analyzer.vsm_reconstruct import StemReconstruction  # noqa: E402
from reconstruire import Chantier, aligner_rapport_sur_projet  # noqa: E402


def _stem_apres_arbitrage():
    """Le stem tel que l'arbitrage de piste le laisse : multisample gagne.

    Les chiffres sont ceux de `usandthem-v9`, pris dans son `rapport.json`.
    """
    stem = StemReconstruction(
        name="bass",
        machine="vsm.multisample",
        parameters={"output.level": 0.9},
        distance=0.18508491705766025,
        notes=[],
        considered=[],
    )
    stem.profile = "FR3-Saw-Lead"
    stem.track_distance = 0.16594635634640428
    # Le classement AU STEM de toutes les candidates, dont la remplaçante.
    stem.track_considered = [
        ("vsm.multisample", "patch d'usine", 0.18508491705766025),
        ("vsm.cs80", "patch d'usine", 0.362272),
        ("vsm.psg", "patch d'usine", 0.41),
    ]
    return stem


def _chantier(stem):
    chantier = Chantier.__new__(Chantier)
    chantier.reconstruits = [stem]
    chantier.rapport_batterie = None
    return chantier


@test
def le_rapport_reprend_la_distance_de_la_machine_RETENUE():
    """Le cœur de la panne : cs80 doit publier SA distance, pas celle de l'autre."""
    stem = _stem_apres_arbitrage()
    retenue = ExportTrack(name="bass", machine="vsm.cs80",
                          parameters={"output.level": 0.8})
    aligner_rapport_sur_projet(_chantier(stem), [retenue], {"bass": 0.2337})

    assert_equal(stem.machine, "vsm.cs80")
    # 0,362272 et non 0,185085 -- presque le double, et c'est tout l'intérêt.
    assert_true(abs(stem.distance - 0.362272) < 1e-9,
                f"distance publiée {stem.distance}, attendue 0.362272")
    assert_true(stem.distance > 0.30,
                "le rapport publie encore le score de la machine écartée")


@test
def le_rapport_lache_le_profil_de_la_machine_ECARTEE():
    """Un profil de multisample publié sous le nom d'une machine qui n'en a pas."""
    stem = _stem_apres_arbitrage()
    retenue = ExportTrack(name="bass", machine="vsm.cs80", parameters={})
    aligner_rapport_sur_projet(_chantier(stem), [retenue], {})
    assert_equal(stem.profile, "")


@test
def le_profil_survit_quand_la_machine_ne_change_pas():
    """Le contrôle : sans substitution, rien ne doit bouger."""
    stem = _stem_apres_arbitrage()
    retenue = ExportTrack(name="bass", machine="vsm.multisample",
                          parameters={"output.level": 0.9}, profile="FR3-Saw-Lead")
    aligner_rapport_sur_projet(_chantier(stem), [retenue], {})
    assert_equal(stem.profile, "FR3-Saw-Lead")
    assert_true(abs(stem.distance - 0.18508491705766025) < 1e-12,
                "la distance de la gagnante ne doit pas bouger")


@test
def une_machine_qui_n_a_pas_concouru_laisse_le_champ_tel_quel():
    """Mieux vaut un chiffre ancien SIGNALÉ qu'un chiffre inventé.

    Si la machine finale n'apparaît nulle part dans l'arbitrage, on ne peut
    pas connaître sa distance au stem : on ne la fabrique pas.
    """
    stem = _stem_apres_arbitrage()
    avant = stem.distance
    retenue = ExportTrack(name="bass", machine="vsm.jamais.vue", parameters={})
    aligner_rapport_sur_projet(_chantier(stem), [retenue], {})
    assert_true(abs(stem.distance - avant) < 1e-12,
                "aucune distance ne doit être inventée")
