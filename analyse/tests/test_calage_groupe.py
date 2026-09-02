"""Des pistes qui partagent un stem se calent ENSEMBLE, sur leur somme.

MESURÉ, PAS SUPPOSÉ. Découper un stem en plusieurs pistes casse l'hypothèse
du calage piste par piste : chacune est comparée au stem ENTIER et reçoit
donc le gain qu'il faudrait à elle seule pour le remplacer. Deux dégâts :

  - les quatre voix d'`other` sortaient chacune au niveau du stem — leur
    somme, quatre fois trop fort ;
  - les pièces d'une batterie éclatée n'étaient pas calées du tout, faute
    d'un stem à leur nom : « volume non calé (pas de stem de référence) ».
    Sur *Us and Them*, `--batterie-par-piece` coûtait **+35,1 %** de distance
    (0,2580 contre 0,1910 au témoin) pour cette seule raison — un découpage
    qui ne change pas une note payait un tiers de dégradation.

Les pistes d'un groupe PARTITIONNENT leur stem : c'est leur somme qui doit
l'égaler. Ces tests fixent les deux moitiés du contrat — la somme atteint le
niveau du stem, et l'équilibre INTERNE du groupe ne bouge pas.
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

from framework import assert_equal, assert_near, assert_true, run, test  # noqa: E402

from analyzer import vsm_levels  # noqa: E402
from analyzer.vsm_project_export import ExportNote, ExportTrack  # noqa: E402

TAUX = 48000


def piste(nom, volume=0.9):
    return ExportTrack(name=nom, machine="vsm.drums", volume=volume,
                       notes=[ExportNote(note=36, velocity=100, start=0.0, duration=0.1)])


def avec_rendus(rendus, fonction):
    """Remplace le rendu solo par une table nom -> signal, sans moteur."""
    vrai = vsm_levels._render_track
    vsm_levels._render_track = lambda track, dossier, duree, taux: rendus.get(track.name)
    try:
        return fonction()
    finally:
        vsm_levels._render_track = vrai


@test
def la_somme_du_groupe_atteint_le_niveau_du_stem():
    """Deux pièces qui rendent 0,1 de RMS chacune : leur somme doit égaler un
    stem à 0,4. Chacune calée SEULE recevrait un facteur 4 — la somme
    sortirait alors deux fois trop fort."""
    n = TAUX
    # Deux signaux décorrélés (l'un pair, l'autre impair) : leur somme a
    # exactement l'énergie des deux, comme deux pièces de batterie disjointes.
    a = np.zeros(n); a[0::2] = 0.1 * np.sqrt(2)
    b = np.zeros(n); b[1::2] = 0.1 * np.sqrt(2)
    stem = np.zeros(n); stem[:] = 0.0
    stem[0::2] = 0.4 * np.sqrt(2)

    pistes = [piste("Batterie · kick"), piste("Batterie · hihat")]
    stems = {p.name: stem for p in pistes}
    groupes = {p.name: "Batterie" for p in pistes}
    rendus = {"Batterie · kick": a, "Batterie · hihat": b}

    lignes = avec_rendus(rendus, lambda: vsm_levels.match_track_levels(
        pistes, stems, Path("."), TAUX, groupes=groupes))

    rms_somme = vsm_levels._rms(a + b)
    attendu = 0.9 * vsm_levels._rms(stem) / rms_somme
    for p in pistes:
        assert_near(p.volume, attendu, 1e-6, f"{p.name} reçoit le facteur du GROUPE")
    assert_true(any("groupe « Batterie »" in l for l in lignes),
                "le rapport dit que le calage est groupé : " + str(lignes))


@test
def l_equilibre_interne_du_groupe_ne_bouge_pas():
    """La détection a trouvé qu'une pièce est deux fois plus forte qu'une
    autre : le calage change le poids du GROUPE dans le mélange, jamais le
    rapport entre ses membres."""
    n = TAUX
    a = np.zeros(n); a[0::2] = 0.2
    b = np.zeros(n); b[1::2] = 0.1
    stem = np.zeros(n); stem[0::2] = 0.8

    pistes = [piste("Batterie · kick", volume=0.8), piste("Batterie · hihat", volume=0.4)]
    stems = {p.name: stem for p in pistes}
    groupes = {p.name: "Batterie" for p in pistes}
    avant = pistes[0].volume / pistes[1].volume

    avec_rendus({"Batterie · kick": a, "Batterie · hihat": b},
                lambda: vsm_levels.match_track_levels(pistes, stems, Path("."),
                                                      TAUX, groupes=groupes))
    apres = pistes[0].volume / pistes[1].volume
    assert_near(apres, avant, 1e-9, "le rapport entre les membres est intact")


@test
def une_piste_hors_groupe_garde_le_calage_individuel():
    """Le chemin d'origine ne doit pas bouger : une piste ordinaire se cale
    seule, contre son propre stem."""
    n = TAUX
    rendu = np.zeros(n); rendu[0::2] = 0.1
    stem = np.zeros(n); stem[0::2] = 0.3

    seule = piste("bass", volume=0.9)
    avec_rendus({"bass": rendu},
                lambda: vsm_levels.match_track_levels([seule], {"bass": stem},
                                                      Path("."), TAUX, groupes={}))
    assert_near(seule.volume, 0.9 * 3.0, 1e-6, "facteur 3, comme avant le groupe")


@test
def un_groupe_sans_stem_le_dit_au_lieu_de_deviner():
    pistes = [piste("Batterie · kick"), piste("Batterie · hihat")]
    lignes = avec_rendus({}, lambda: vsm_levels.match_track_levels(
        pistes, {}, Path("."), TAUX, groupes={p.name: "Batterie" for p in pistes}))
    assert_true(any("pas de stem de référence" in l for l in lignes),
                "le refus est dit : " + str(lignes))
    assert_equal(pistes[0].volume, 0.9, "et rien n'est touché au hasard")


if __name__ == "__main__":
    raise SystemExit(run())
