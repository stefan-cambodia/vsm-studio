"""H27 : le témoin de coupure couvre les pistes qu'aucune machine ne joue.

Ce que ce test verrouille est une panne muette MESURÉE, pas une hypothèse. À la
course 3 de l'épreuve Children (CDC multipiste § 12.3), deux pistes AUDIO — la
voix reportée, tête et chœurs — sont entrées au mélange et l'ont dégradé de
2,39 % (0,1935 → 0,1982). Aucune ligne du journal ne l'a dit : le témoin de
coupure était posé DANS la boucle des alternatives, et une piste audio n'a pas
de machine, donc pas de machine suivante, donc pas de verdict du tout.

Le rendu est REMPLACÉ ici par une fonction : ce qui est mesuré est la DÉCISION
de mesurer, pas le moteur.
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402

from framework import assert_equal, assert_true, test  # noqa: E402

from analyzer import vsm_mix_verdict as mv  # noqa: E402
from analyzer.vsm_project_export import ExportNote, ExportTrack  # noqa: E402


def _piste_machine(nom: str = "bass") -> ExportTrack:
    return ExportTrack(name=nom, machine="vsm.tb303",
                       notes=[ExportNote(note=48, velocity=100, start=0.0, duration=0.5)])


def _piste_audio(nom: str = "Voix · tête") -> ExportTrack:
    return ExportTrack(name=nom, audio_path="samples/voix-tete.wav",
                       audio_sample_rate=44100.0, audio_frames=44100, audio_channels=1)


@test
def temoin_une_piste_audio_est_jouante():
    assert_true(mv.piste_jouante(_piste_audio()), "une piste audio met du son")
    assert_true(mv.piste_jouante(_piste_machine()), "une machine avec des notes aussi")


@test
def temoin_un_bus_n_est_pas_une_piste_jouante():
    """Couper un bus couperait ses membres, qui ont déjà chacun leur témoin."""
    assert_true(not mv.piste_jouante(ExportTrack(name="Batterie", is_group=True)),
                "un groupe ne reçoit pas de témoin")
    assert_true(not mv.piste_jouante(ExportTrack(name="vide")),
                "une piste sans machine, sans note et sans audio ne joue rien")


@test
def temoin_la_piste_audio_recoit_son_verdict_sans_machine_suivante():
    """LE CAS DE LA COURSE 3 : deux pistes audio, aucune alternative.

    Le faux rendu fabrique un mélange où la piste audio DÉGRADE : le morceau
    visé est la seule piste de machine, et l'audio ajoute un signal qui n'y est
    pas. Le témoin doit donc sortir, et dire que le morceau est meilleur sans.
    """
    # 44 100 Hz : la métrique v2 découpe des bandes jusqu'à Nyquist, et un taux
    # de banc trop bas la fait échouer avant de mesurer quoi que ce soit.
    taux, duree = 44100, 22050
    temps = np.arange(duree, dtype=np.float32) / taux
    machine = np.sin(2 * np.pi * 220 * temps).astype(np.float32)
    parasite = np.sin(2 * np.pi * 3000 * temps).astype(np.float32) * 0.3

    pistes = [_piste_machine(), _piste_audio()]
    rendus = {"bass": machine, "Voix · tête": parasite}

    def faux_rendu(tracks, dossier, sample_rate, tempo, binary):
        somme = np.zeros(duree, dtype=np.float32)
        for t in tracks:
            if t.volume > 0.0:
                somme = somme + rendus[t.name] * float(t.volume)
        return somme

    vrai_rendu = mv._render_project
    mv._render_project = faux_rendu
    mv._deja_dit.clear()
    try:
        decisions = mv.keep_what_helps_the_mix(
            tracks=pistes, alternatives={}, mixture=machine * 0.9, stems_audio={},
            samples_root=Path("."), workdir=Path("."), sample_rate=taux)
    finally:
        mv._render_project = vrai_rendu

    noms = [d.track for d in decisions]
    assert_equal(noms, ["bass", "Voix · tête"], "les deux pistes jouantes sont jugées")
    audio = next(d for d in decisions if d.track == "Voix · tête")
    assert_true(audio.muted_distance is not None, "la piste audio a son témoin de coupure")
    assert_true(audio.muted_distance < audio.distance_kept,
                "le morceau est meilleur sans le parasite, et le chiffre le dit")
    assert_equal(audio.rejected, [], "aucune alternative n'a été inventée pour une piste audio")


@test
def temoin_un_bus_ne_recoit_pas_de_verdict():
    """Le bus est écarté par la boucle elle-même, pas seulement par la fonction."""
    taux = 44100
    signal = np.ones(22050, dtype=np.float32)
    pistes = [_piste_machine(), ExportTrack(name="Batterie", is_group=True)]

    def faux_rendu(tracks, dossier, sample_rate, tempo, binary):
        return signal * sum(float(t.volume) for t in tracks if not t.is_group)

    vrai_rendu = mv._render_project
    mv._render_project = faux_rendu
    mv._deja_dit.clear()
    try:
        decisions = mv.keep_what_helps_the_mix(
            tracks=pistes, alternatives={}, mixture=signal, stems_audio={},
            samples_root=Path("."), workdir=Path("."), sample_rate=taux)
    finally:
        mv._render_project = vrai_rendu
    assert_equal([d.track for d in decisions], ["bass"], "le bus n'est pas jugé")


# ---------------------------------------------------------------------------
# B11 — LE TÉMOIN REJOUÉ SUR LE PROJET FINAL, ET PAR PAIRES.
#
# L'attendu, écrit avant la mesure : « le témoin publié se compare au projet
# LIVRÉ, pas à l'état qu'avait le projet au moment où la piste a été jugée ; un
# avis qui change entre les deux est DIT ; et quand deux pistes sont signalées,
# la coupe des deux est essayée et publiée, parce qu'elle n'est pas la somme des
# deux coupes. »
# ---------------------------------------------------------------------------


def _piste_muette_possible(nom: str, hauteur: int) -> ExportTrack:
    return ExportTrack(name=nom, machine="vsm.tb303",
                       notes=[ExportNote(note=hauteur, velocity=100, start=0.0, duration=0.5)])


@test
def temoin_le_verdict_final_se_compare_au_projet_livre():
    """Le défaut de B11 : la référence changeait après le jugement.

    Deux pistes parasites. La première est jugée quand la seconde est encore au
    mélange ; le projet s'améliore ensuite. Le témoin FINAL doit se comparer à
    la distance du projet livré, la même pour toutes les pistes — c'est
    précisément ce que la référence de la boucle ne garantissait pas.
    """
    taux, duree = 44100, 22050
    temps = np.arange(duree, dtype=np.float32) / taux
    machine = np.sin(2 * np.pi * 220 * temps).astype(np.float32)
    rendus = {"bass": machine,
              "para1": np.sin(2 * np.pi * 3000 * temps).astype(np.float32) * 0.3,
              "para2": np.sin(2 * np.pi * 5000 * temps).astype(np.float32) * 0.3}
    pistes = [_piste_machine(), _piste_muette_possible("para1", 72),
              _piste_muette_possible("para2", 79)]

    def faux_rendu(tracks, dossier, sample_rate, tempo, binary):
        somme = np.zeros(duree, dtype=np.float32)
        for t in tracks:
            if t.volume > 0.0:
                somme = somme + rendus[t.name] * float(t.volume)
        return somme

    vrai_rendu = mv._render_project
    mv._render_project = faux_rendu
    mv._deja_dit.clear()
    try:
        decisions = mv.keep_what_helps_the_mix(
            tracks=pistes, alternatives={}, mixture=machine * 0.9, stems_audio={},
            samples_root=Path("."), workdir=Path("."), sample_rate=taux)
    finally:
        mv._render_project = vrai_rendu

    finales = {d.track: d for d in decisions}
    for nom in ("bass", "para1", "para2"):
        assert_true(finales[nom].muted_distance_final is not None,
                    f"{nom} porte un témoin rejoué sur le projet final")
        assert_true(finales[nom].reference_final is not None,
                    f"{nom} porte la distance du projet livré")
    references = {round(float(d.reference_final), 9) for d in decisions}
    assert_equal(len(references), 1,
                 "toutes les pistes se comparent à LA MÊME distance, celle du projet livré")
    # Et les deux parasites restent signalés : le morceau est meilleur sans eux.
    for nom in ("para1", "para2"):
        assert_true(finales[nom].muted_distance_final < finales[nom].reference_final,
                    f"{nom} dégrade le mélange, et le témoin final le dit")


@test
def temoin_les_paires_sont_essayees_quand_deux_pistes_sont_signalees():
    """B11, second défaut : couper deux pistes n'est pas la somme de deux coupes.

    On compte les rendus : la passe finale en demande un pour le projet, un par
    piste jouante, puis UN PAR PAIRE de pistes signalées. Avec trois pistes dont
    deux signalées, cela fait 1 + 3 + 1 rendus après la boucle.
    """
    taux, duree = 44100, 22050
    temps = np.arange(duree, dtype=np.float32) / taux
    machine = np.sin(2 * np.pi * 220 * temps).astype(np.float32)
    rendus = {"bass": machine,
              "para1": np.sin(2 * np.pi * 3000 * temps).astype(np.float32) * 0.3,
              "para2": np.sin(2 * np.pi * 5000 * temps).astype(np.float32) * 0.3}
    pistes = [_piste_machine(), _piste_muette_possible("para1", 72),
              _piste_muette_possible("para2", 79)]
    appels = {"n": 0}

    def faux_rendu(tracks, dossier, sample_rate, tempo, binary):
        appels["n"] += 1
        somme = np.zeros(duree, dtype=np.float32)
        for t in tracks:
            if t.volume > 0.0:
                somme = somme + rendus[t.name] * float(t.volume)
        return somme

    vrai_rendu = mv._render_project
    mv._render_project = faux_rendu
    mv._deja_dit.clear()
    try:
        avant_passe = {"n": 0}
        mv.keep_what_helps_the_mix(
            tracks=pistes, alternatives={}, mixture=machine * 0.9, stems_audio={},
            samples_root=Path("."), workdir=Path("."), sample_rate=taux)
        del avant_passe
    finally:
        mv._render_project = vrai_rendu

    # La boucle elle-même : une référence (mise en cache) + un témoin par piste
    # = 4 rendus. La passe finale : 1 projet + 3 témoins + 1 paire = 5. Total 9.
    assert_equal(appels["n"], 9,
                 "le coût est borné et connu : 4 rendus dans la boucle, 5 à la passe finale")


@test
def temoin_une_seule_piste_signalee_n_essaie_aucune_paire():
    """Une paire demande deux signalées : sinon la passe ne coûte rien de plus."""
    taux, duree = 44100, 22050
    temps = np.arange(duree, dtype=np.float32) / taux
    machine = np.sin(2 * np.pi * 220 * temps).astype(np.float32)
    rendus = {"bass": machine,
              "para1": np.sin(2 * np.pi * 3000 * temps).astype(np.float32) * 0.3}
    pistes = [_piste_machine(), _piste_muette_possible("para1", 72)]
    appels = {"n": 0}

    def faux_rendu(tracks, dossier, sample_rate, tempo, binary):
        appels["n"] += 1
        somme = np.zeros(duree, dtype=np.float32)
        for t in tracks:
            if t.volume > 0.0:
                somme = somme + rendus[t.name] * float(t.volume)
        return somme

    vrai_rendu = mv._render_project
    mv._render_project = faux_rendu
    mv._deja_dit.clear()
    try:
        mv.keep_what_helps_the_mix(
            tracks=pistes, alternatives={}, mixture=machine * 0.9, stems_audio={},
            samples_root=Path("."), workdir=Path("."), sample_rate=taux)
    finally:
        mv._render_project = vrai_rendu
    # Boucle : 1 référence + 2 témoins = 3. Passe finale : 1 projet + 2 témoins,
    # et AUCUNE paire = 3. Total 6.
    assert_equal(appels["n"], 6, "aucune paire n'est essayée avec une seule piste signalée")
