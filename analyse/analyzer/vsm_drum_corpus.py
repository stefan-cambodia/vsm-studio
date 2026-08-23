"""
Corpus de FRAPPES et classifieur de pièces — phase A2.1 et A2.2.

CE QUE LE BANC A MONTRÉ, ET QUI DICTE LA FORME DE CE MODULE. Sur le motif aux
contretemps, une charleston qui suit une caisse claire n'a PAS de nouveauté :
la queue de bruit de la caisse claire est encore plus forte dans l'aigu
quarante millisecondes avant la charleston qu'au moment où elle frappe, et la
soustraction « après moins avant » efface tout son aigu. Aucune estimation du
fond ne la sauve (quatre éprouvées). L'information existe pourtant dans le
signal — l'aigu REMONTE par rapport à une queue qui décroissait — mais elle
n'est pas dans la nouveauté : elle est dans le COUPLE (avant, après).

Ce module apprend donc à lire ce couple. Son corpus est fait de frappes
engendrées par les boîtes à rythmes du parc, SEULES et SUPERPOSÉES à décalages
connus — « caisse claire puis charleston 214 ms plus tard », « kick puis
charleston sur sa queue » — étiquetées par construction : à l'instant de la
seconde frappe, quelle pièce est NOUVELLE ? C'est exactement ce que le § 5 du
cahier des charges demandait (« les cas qui ont fait tomber les architectures
précédentes, étiquetés par construction »), et c'est la première fois qu'on
sait précisément LESQUELS.

CE QUE LE MODÈLE FAIT ET NE FAIT PAS. Il répond, pour une attaque détectée :
« quelles pièces frappent ICI ? » — plusieurs à la fois s'il le faut. Il ne
détecte pas les attaques (le détecteur du module existant reste l'ossature, il
a survécu à trois architectures), et il ne produit aucun son. Le repli est
toujours le nommage actuel : sans modèle, rien ne change.
"""

from __future__ import annotations

import platform
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

from .vsm_drumkit import AFTER_WINDOW, BEFORE_WINDOW, STFT_HOP, STFT_WINDOW, _log_band_spectrogram
from .vsm_engine import Note, VsmEngine

FORMAT_MODELE = "vsm-classifieur-batterie"
VERSION_MODELE = 1

# Les pièces que le modèle sait nommer, et leur note sur chaque boîte. Le
# pedalhat (44) n'existe que sur vsm.drums ; il est une charleston fermée pour
# le classement, comme dans le banc.
PIECES: Tuple[str, ...] = ("kick", "snare", "hihat", "openhat", "clap", "tom")
NOTES_PAR_MACHINE: Dict[str, Dict[str, int]] = {
    "vsm.tr909": {"kick": 36, "snare": 38, "hihat": 42, "openhat": 46, "clap": 39, "tom": 45},
    "vsm.tr808": {"kick": 36, "snare": 38, "hihat": 42, "openhat": 46, "clap": 39},
    "vsm.drums": {"kick": 36, "snare": 38, "hihat": 42, "openhat": 46, "tom": 45},
}

# Décalages entre la première frappe et la seconde, en secondes. Ils couvrent
# la double-croche à 160 BPM (94 ms) jusqu'à la croche à 110 (273 ms) — la
# plage où la queue de la première frappe recouvre encore la seconde.
DECALAGES: Tuple[float, ...] = (0.094, 0.125, 0.150, 0.187, 0.214, 0.273)


def _fenetres(bandes: np.ndarray, instant: int, sample_rate: int) -> Optional[np.ndarray]:
    """Le couple (avant, après) en bandes log, aplati. Même fenêtrage que la
    nouveauté du module existant — pour que ce qu'on apprend soit exactement ce
    que le détecteur voit."""
    def trame(x: int) -> int:
        return max(0, (x - STFT_WINDOW // 2) // STFT_HOP)

    avant = bandes[trame(instant + int(BEFORE_WINDOW[0] * sample_rate)):
                    trame(instant + int(BEFORE_WINDOW[1] * sample_rate)) + 1]
    apres = bandes[trame(instant + int(0.002 * sample_rate)):
                    trame(instant + int(AFTER_WINDOW[1] * sample_rate)) + 1]
    if not avant.size or not apres.size:
        return None
    moyenne_avant = np.mean(avant, axis=0)
    pic_apres = np.max(apres, axis=0)
    # Trois vues, et la troisième est celle qui manque à la nouveauté : le
    # RAPPORT après/avant bande par bande, en log. Une charleston sur une queue
    # de caisse claire ne fait pas monter l'aigu au-dessus de la moyenne
    # d'avant, mais elle l'empêche de DESCENDRE — et ça se lit dans le rapport
    # à la dernière trame d'avant.
    derniere_avant = avant[-1]
    rapport = np.log1p(pic_apres) - np.log1p(derniere_avant)
    return np.concatenate([np.log1p(moyenne_avant), np.log1p(pic_apres), rapport]).astype(np.float32)


def descripteurs_frappe(audio: np.ndarray, instant: int, sample_rate: int) -> Optional[np.ndarray]:
    bandes, _ = _log_band_spectrogram(audio, sample_rate)
    return _fenetres(bandes, instant, sample_rate)


@dataclass
class CorpusFrappes:
    X: np.ndarray                # (n, 72)
    Y: np.ndarray                # (n, len(PIECES)) — 1 si la pièce frappe à cet instant
    machines: List[str]
    situations: List[str]        # « seule », « après kick », « sur queue de snare »…
    pieces: Tuple[str, ...] = PIECES


def engendre_corpus_frappes(engine: VsmEngine, sample_rate: int = 44100,
                            graine: int = 20260823, progression=None) -> CorpusFrappes:
    """Frappes seules et superposées, étiquetées par construction."""
    rng = np.random.default_rng(graine)
    X, Y, machines, situations = [], [], [], []
    index = {p: i for i, p in enumerate(PIECES)}
    duree = 0.8

    def ajoute(audio: np.ndarray, instant_s: float, nouvelles: Sequence[str], machine: str, situation: str):
        v = descripteurs_frappe(audio, int(instant_s * sample_rate), sample_rate)
        if v is None:
            return
        y = np.zeros(len(PIECES), dtype=np.float32)
        for p in nouvelles:
            y[index[p]] = 1.0
        X.append(v); Y.append(y); machines.append(machine); situations.append(situation)

    for machine, notes in NOTES_PAR_MACHINE.items():
        pieces = list(notes)
        # Variations de réglage : quelques tirages de l'espace déclaré de la
        # machine, pour que le modèle ne connaisse pas qu'UN kick de 808.
        variantes = [{}]
        try:
            from .vsm_patch_optimizer import search_space_for_machine, _vector_to_parameters
            espace = search_space_for_machine(machine, engine)
            for _ in range(4):
                variantes.append(_vector_to_parameters(espace, rng.random(len(espace))))
        except Exception:  # noqa: BLE001 — une machine sans espace reste au patch d'usine
            pass

        for patch in variantes:
            for velocite in (70, 110):
                # 1. Chaque pièce SEULE, à t = 0,1 s.
                for p in pieces:
                    audio = engine.render(machine, patch, [Note(notes[p], velocite, 0.1, 0.05)],
                                          duree, sample_rate=sample_rate)
                    ajoute(audio, 0.1, [p], machine, "seule")
                # 2. SUPERPOSITIONS : A à t = 0,1, B à t = 0,1 + d. Étiquette à
                #    l'instant de B : B seule est nouvelle.
                for a in pieces:
                    for b in pieces:
                        if a == b:
                            continue
                        for d in DECALAGES:
                            audio = engine.render(machine, patch,
                                                  [Note(notes[a], velocite, 0.1, 0.05),
                                                   Note(notes[b], velocite, 0.1 + d, 0.05)],
                                                  duree, sample_rate=sample_rate)
                            ajoute(audio, 0.1 + d, [b], machine, f"{b} après {a} ({d*1000:.0f} ms)")
                # 3. CO-FRAPPES : A et B au même instant — les deux sont nouvelles.
                #
                # À PLUSIEURS ÉQUILIBRES, et la situation porte l'équilibre.
                # Une première version n'avait qu'un « kick+snare ensemble »,
                # à vélocités égales : une seule situation, donc entière d'un
                # seul côté de la coupure -- et elle est tombée dans l'épreuve.
                # Le modèle n'avait JAMAIS vu une caisse claire sur un kick à
                # l'entraînement, et la reconnaissait pourtant à 0,47 sur le
                # banc. C'était méritoire et ce n'était pas le test voulu. Les
                # équilibres déclinent la situation, et dans un morceau de
                # club la caisse claire est le plus souvent SOUS le kick.
                for i, a in enumerate(pieces):
                    for b in pieces[i + 1:]:
                        for vel_a, vel_b, equilibre in ((velocite, velocite, "égal"),
                                                        (velocite, max(40, velocite - 40), f"{b} en retrait"),
                                                        (max(40, velocite - 40), velocite, f"{a} en retrait")):
                            audio = engine.render(machine, patch,
                                                  [Note(notes[a], vel_a, 0.1, 0.05),
                                                   Note(notes[b], vel_b, 0.1, 0.05)],
                                                  duree, sample_rate=sample_rate)
                            ajoute(audio, 0.1, [a, b], machine, f"{a}+{b} ensemble ({equilibre})")
            if progression:
                progression(f"{machine} : {len(X)} exemples")
    return CorpusFrappes(np.stack(X), np.stack(Y), machines, situations)


@dataclass
class ClassifieurFrappes:
    pieces: Tuple[str, ...]
    moyenne: np.ndarray
    echelle: np.ndarray
    modeles: Dict[str, object]     # un modèle binaire par pièce
    seuil: float
    date: str
    versions: Dict[str, str]
    mesures: Dict[str, object] = field(default_factory=dict)

    def pieces_a(self, descripteur: np.ndarray) -> List[Tuple[str, float]]:
        """Pièces qui frappent à cet instant, avec leur probabilité. Vide si
        aucune ne passe le seuil — et c'est une réponse, pas un échec."""
        z = ((np.asarray(descripteur, dtype=np.float64) - self.moyenne) / self.echelle)[None, :]
        resultat = []
        for p in self.pieces:
            proba = float(self.modeles[p].predict_proba(z)[0, 1])
            if proba >= self.seuil:
                resultat.append((p, proba))
        return sorted(resultat, key=lambda x: -x[1])

    def enregistre(self, chemin) -> None:
        import joblib
        joblib.dump({"format": FORMAT_MODELE, "version": VERSION_MODELE,
                     "pieces": self.pieces, "moyenne": self.moyenne, "echelle": self.echelle,
                     "modeles": self.modeles, "seuil": self.seuil, "date": self.date,
                     "versions": self.versions, "mesures": self.mesures}, chemin)

    @staticmethod
    def relit(chemin) -> "ClassifieurFrappes":
        import joblib
        d = joblib.load(chemin)
        if d.get("format") != FORMAT_MODELE or d.get("version") != VERSION_MODELE:
            raise ValueError(f"modèle de batterie inattendu : {d.get('format')!r} v{d.get('version')}")
        return ClassifieurFrappes(tuple(d["pieces"]), d["moyenne"], d["echelle"], d["modeles"],
                                  float(d["seuil"]), str(d["date"]), dict(d["versions"]),
                                  dict(d.get("mesures", {})))


# SEUIL DE DÉCISION, balayé au banc plutôt que choisi. Les sorties du modèle
# sont presque toujours proches de 0 ou de 1, et le seuil ne pèse que sur les
# co-frappes à l'équilibre : à 0,25 le motif aux contretemps rend 16/16 et zéro
# kick inventé, à 0,35 il en perd une. Aucune frappe n'est JAMAIS perdue, quel
# que soit le seuil -- le modèle n'ajoute que des étiquettes --, donc le coût
# d'un seuil bas est borné. 0,25 est la valeur mesurée ; 0,3 a été essayé
# « pour la marge » et tombait du mauvais côté de la frappe qui sépare 16/16 de
# 15/16. Un réglage se prend là où la mesure le met, pas à côté.
SEUIL_DECISION = 0.25


def entraine_frappes(corpus: CorpusFrappes, graine: int = 20260823, seuil: float = SEUIL_DECISION,
                     part_epreuve: float = 0.25) -> Tuple[ClassifieurFrappes, Dict[str, object]]:
    """Un modèle binaire par pièce, éprouvé sur des SITUATIONS jamais vues.

    La coupure se fait par SITUATION (« hihat après snare (214 ms) » entier d'un
    côté ou de l'autre), pas par exemple : les exemples d'une même situation
    sont des vues du même cas, et les répartir au hasard ferait le même mensonge
    que la coupure par exemple du classifieur de machine."""
    import sklearn
    from sklearn.ensemble import HistGradientBoostingClassifier

    rng = np.random.default_rng(graine)
    situations = sorted(set(corpus.situations))
    rng.shuffle(situations)
    epreuve_situations = set(situations[:max(1, int(len(situations) * part_epreuve))])
    # GARDE-FOU : une PAIRE de pièces ne doit jamais être entièrement à l'écart.
    # Les co-frappes d'une paire sont trois situations (trois équilibres) ;
    # si le tirage les met toutes trois dans l'épreuve, le modèle n'apprend
    # jamais que ces deux pièces peuvent frapper ensemble. C'est arrivé, et
    # c'est écrit ci-dessus.
    def paire_de(situation: str) -> Optional[str]:
        return situation.split(" ensemble")[0] if " ensemble" in situation else None
    paires_epreuve: Dict[str, List[str]] = {}
    for sit in epreuve_situations:
        p = paire_de(sit)
        if p:
            paires_epreuve.setdefault(p, []).append(sit)
    toutes = {}
    for sit in situations:
        p = paire_de(sit)
        if p:
            toutes.setdefault(p, []).append(sit)
    for p, sits in paires_epreuve.items():
        if len(sits) == len(toutes.get(p, [])):
            epreuve_situations.discard(sorted(sits)[0])  # on en rend une à l'entraînement
    masque_epreuve = np.array([s in epreuve_situations for s in corpus.situations])

    X = corpus.X.astype(np.float64)
    moyenne = X[~masque_epreuve].mean(axis=0)
    echelle = X[~masque_epreuve].std(axis=0); echelle[echelle < 1e-9] = 1.0
    Z = (X - moyenne) / echelle

    modeles: Dict[str, object] = {}
    mesures: Dict[str, object] = {"parPiece": {}}
    for i, p in enumerate(corpus.pieces):
        y = corpus.Y[:, i]
        if y[~masque_epreuve].sum() == 0 or (1 - y[~masque_epreuve]).sum() == 0:
            continue
        m = HistGradientBoostingClassifier(random_state=graine, max_iter=150)
        m.fit(Z[~masque_epreuve], y[~masque_epreuve])
        modeles[p] = m
        if masque_epreuve.any():
            pred = (m.predict_proba(Z[masque_epreuve])[:, 1] >= seuil).astype(float)
            vrai = y[masque_epreuve]
            tp = float(((pred == 1) & (vrai == 1)).sum()); fp = float(((pred == 1) & (vrai == 0)).sum())
            fn = float(((pred == 0) & (vrai == 1)).sum())
            mesures["parPiece"][p] = {"rappel": tp / max(tp + fn, 1), "precision": tp / max(tp + fp, 1),
                                      "positifs": int(vrai.sum())}
    mesures["situationsEpreuve"] = sorted(epreuve_situations)
    mesures["exemples"] = {"entrainement": int((~masque_epreuve).sum()), "epreuve": int(masque_epreuve.sum())}
    clf = ClassifieurFrappes(tuple(p for p in corpus.pieces if p in modeles), moyenne, echelle, modeles,
                             seuil, datetime.now(timezone.utc).isoformat(timespec="seconds"),
                             {"python": platform.python_version(), "numpy": np.__version__,
                              "sklearn": sklearn.__version__}, mesures)
    return clf, mesures
