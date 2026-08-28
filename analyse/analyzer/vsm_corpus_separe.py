"""
Le corpus PASSÉ PAR LA SÉPARATION — la condition de réouverture du § 7.

CE QUE DEUX MESURES INDÉPENDANTES ONT DIT. L'estimateur de paramètres
(`ROADMAP-fusion.md` § 7) et le classifieur de machine (`ROADMAP-apprentissage`
A1) sont excellents sur ce que le MOTEUR produit et mauvais sur ce qu'un DISQUE
contient ; et dégrader synthétiquement un rendu (égaliseur, réverbération,
compression, bruit, fuite, désaccord — A0.4) n'y change rien, quelle que soit
la dose. La seule piste que le § 7 laisse debout : un corpus qui contient la
DÉGRADATION RÉELLE — « rendre le patch, le mélanger à d'autres stems, faire
repasser le tout par demucs, et étiqueter le résultat avec le patch
d'origine ». C'est ce que fait ce module, et il le fait pour MESURER si le
fossé se referme, avant qu'une seule ligne de la phase A3 ne soit écrite.

COMMENT C'EST PAYÉ. Une séparation par exemple coûterait des heures ; tous les
exemples sont donc CONCATÉNÉS, séparés par des silences, en UN seul fichier
que demucs traite une fois, et chaque stem « other » est redécoupé aux instants
connus. Le fond de mélange vient de VRAIS stems séparés (batterie, basse,
voix) de morceaux déjà passés dans la chaîne, tirés au sort et seedés.

CE QUE LA MESURE COMPARE. Trois classifieurs, même modèle, même coupure par
patch : entraîné sur le SEC, sur le SÉPARÉ, sur les deux. Jugés (1) sur le
séparé tenu à l'écart, (2) sur des SONDES réelles — un enregistrement et la
machine que l'arbitrage y retient : le rang médian de cette machine est le seul
chiffre qui dise si le modèle lit un disque.
"""

from __future__ import annotations

import json
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence, Tuple

import numpy as np

from .vsm_corpus import descriptors
from .vsm_corpus_build import graine_de_machine, machines_de_recherche
from .vsm_engine import VsmEngine, VsmEngineError
from .vsm_patch_optimizer import _vector_to_parameters, search_space_for_machine

FORMAT = "vsm-corpus-separe"
VERSION = 1

# La grille est plus courte que celle du corpus sec : une séparation coûte
# ~0,4 s par seconde d'audio, et ce corpus est un BANC, pas un entraînement.
HAUTEURS: Tuple[int, ...] = (52, 64)
VELOCITES: Tuple[int, ...] = (60, 105)
DUREE = 1.0
GATE = 0.75
SILENCE = 0.3            # entre deux exemples dans le fichier concaténé
# Rapport fond/synthé, en dB, tiré uniformément : le synthé est tantôt devant,
# tantôt derrière le reste du morceau, comme dans un mélange réel.
FOND_DB: Tuple[float, float] = (-6.0, 6.0)


@dataclass
class Vivier:
    """Des tranches de VRAIS stems, pour faire le fond des mélanges."""
    tranches: List[np.ndarray]      # mono, float32, longueur = DUREE
    origines: List[str]

    @staticmethod
    def depuis(dossiers: Sequence[Path], sample_rate: int, rng: np.random.Generator,
               par_stem: int = 60) -> "Vivier":
        import soundfile as sf

        tranches, origines = [], []
        n = int(DUREE * sample_rate)
        for dossier in dossiers:
            for nom in ("drums", "bass", "vocals"):
                chemin = next(iter(sorted(Path(dossier).rglob(f"{nom}.wav"))), None)
                if chemin is None:
                    continue
                audio, sr = sf.read(str(chemin), always_2d=True, dtype="float32")
                if sr != sample_rate:
                    raise ValueError(f"{chemin} : {sr} Hz, attendu {sample_rate}")
                mono = audio.mean(axis=1)
                essais = 0
                while sum(1 for o in origines if o == f"{dossier.name}/{nom}") < par_stem and essais < par_stem * 10:
                    essais += 1
                    debut = int(rng.integers(0, max(1, mono.size - n)))
                    t = mono[debut:debut + n]
                    if t.size < n or float(np.sqrt(np.mean(t ** 2))) < 0.01:
                        continue      # un silence ne fait pas un fond
                    tranches.append(t.copy()); origines.append(f"{dossier.name}/{nom}")
        if not tranches:
            raise ValueError("aucune tranche de fond : les dossiers de stems sont vides")
        return Vivier(tranches, origines)

    def fond(self, rng: np.random.Generator, rms_synthe: float) -> np.ndarray:
        """Une somme de deux ou trois tranches, au niveau demandé par rapport au synthé."""
        k = int(rng.integers(2, 4))
        idx = rng.choice(len(self.tranches), size=k, replace=False)
        somme = np.sum([self.tranches[i] for i in idx], axis=0)
        rms = float(np.sqrt(np.mean(somme ** 2))) or 1e-9
        db = float(rng.uniform(*FOND_DB))
        return (somme / rms * rms_synthe * (10.0 ** (db / 20.0))).astype(np.float32)


@dataclass
class CorpusSepare:
    machines: List[str]
    X_sec: np.ndarray            # descripteurs du rendu propre
    X_sep: np.ndarray            # descripteurs du stem « other » après séparation
    y: np.ndarray                # index de machine
    patchs: np.ndarray           # numéro de patch (unique sur tout le corpus)
    conditions: np.ndarray       # (note, vélocité, fond en dB)
    # ÉNERGIE RETROUVÉE : part du synthé que la séparation a laissée dans
    # « other ». Un synthé parti dans « bass » ou « vocals » n'est pas dégradé,
    # il est ABSENT, et ses descripteurs décrivent le fond.
    retrouve: np.ndarray
    secondes: float = 0.0
    rejets: Dict[str, int] = field(default_factory=dict)

    def enregistre(self, chemin: Path) -> None:
        np.savez_compressed(chemin, format=FORMAT, version=VERSION,
                            machines=np.asarray(self.machines, dtype=object),
                            X_sec=self.X_sec, X_sep=self.X_sep, y=self.y, patchs=self.patchs,
                            conditions=self.conditions, retrouve=self.retrouve,
                            secondes=self.secondes,
                            rejets=np.asarray(json.dumps(self.rejets, ensure_ascii=False)))

    @staticmethod
    def relit(chemin: Path) -> "CorpusSepare":
        d = np.load(chemin, allow_pickle=True)
        if str(d["format"]) != FORMAT:
            raise ValueError(f"format inattendu : {d['format']}")
        return CorpusSepare(list(d["machines"]), d["X_sec"], d["X_sep"], d["y"], d["patchs"],
                            d["conditions"], d["retrouve"], float(d["secondes"]),
                            json.loads(str(d["rejets"])))


def restreint(corpus: CorpusSepare, machines: Sequence[str]) -> CorpusSepare:
    """Le même corpus, limité à `machines` (ré-indexées dans cet ordre)."""
    garde = [m for m in corpus.machines if m in set(machines)]
    index = {corpus.machines.index(m): i for i, m in enumerate(garde)}
    masque = np.asarray([int(k) in index for k in corpus.y])
    y = np.asarray([index[int(k)] for k in corpus.y[masque]], dtype=np.int32)
    return CorpusSepare(garde, corpus.X_sec[masque], corpus.X_sep[masque], y, corpus.patchs[masque],
                        corpus.conditions[masque], corpus.retrouve[masque], corpus.secondes,
                        dict(corpus.rejets))


def engendre(engine: VsmEngine, vivier: Vivier, patchs_par_machine: int, sample_rate: int,
             graine: int, dossier_travail: Path, machines: Optional[Sequence[str]] = None,
             progression: Optional[Callable[[str], None]] = None) -> CorpusSepare:
    """Rend, mélange, sépare EN UNE FOIS, redécoupe, décrit."""
    depart = time.perf_counter()
    # Les mêmes machines que le classifieur d'A1 : celles qui ont un espace de
    # recherche ET qui sont mélodiques. Le synthé de test et les boîtes à
    # rythmes n'ont rien à faire sur un stem mélodique.
    from .vsm_reconstruct import melodic_machines
    melodiques = set(melodic_machines(engine))
    machines = list(machines or [m for m in machines_de_recherche(engine) if m in melodiques])
    rng_fond = np.random.default_rng(graine + 7)
    n = int(DUREE * sample_rate)
    gap = int(SILENCE * sample_rate)
    rejets: Dict[str, int] = {}

    secs: List[np.ndarray] = []
    fonds: List[np.ndarray] = []
    etiquettes: List[Tuple[int, int, int, int, float]] = []   # (machine, patch, note, vel, dB)
    numero_patch = 0
    for im, machine in enumerate(machines):
        rng = np.random.default_rng(graine_de_machine(machine) ^ graine)
        espace = search_space_for_machine(machine, engine)
        for _ in range(patchs_par_machine):
            parametres = _vector_to_parameters(espace, rng.random(len(espace)))
            numero_patch += 1
            for note in HAUTEURS:
                for vel in VELOCITES:
                    try:
                        audio = engine.render_note(machine, parametres, midi_note=note,
                                                   duration=DUREE, velocity=vel, gate=GATE,
                                                   sample_rate=sample_rate)
                    except VsmEngineError as erreur:
                        rejets["moteur : " + str(erreur)[:60]] = rejets.get("moteur : " + str(erreur)[:60], 0) + 1
                        continue
                    audio = np.asarray(audio, dtype=np.float32)[:n]
                    if audio.size < n:
                        audio = np.pad(audio, (0, n - audio.size))
                    rms = float(np.sqrt(np.mean(audio.astype(np.float64) ** 2)))
                    if rms < 1e-4:
                        rejets["inaudible (RMS < 1e-4)"] = rejets.get("inaudible (RMS < 1e-4)", 0) + 1
                        continue
                    fond = vivier.fond(rng_fond, rms)
                    db = 20.0 * np.log10(float(np.sqrt(np.mean(fond ** 2))) / rms)
                    secs.append(audio); fonds.append(fond)
                    etiquettes.append((im, numero_patch, note, vel, db))
        if progression:
            progression(f"{machine} : {len(secs)} exemples rendus")

    # UN SEUL FICHIER, une seule séparation.
    total = len(secs) * (n + gap)
    melange = np.zeros(total, dtype=np.float32)
    for i, (s, f) in enumerate(zip(secs, fonds)):
        melange[i * (n + gap):i * (n + gap) + n] = s + f
    crete = float(np.abs(melange).max()) or 1.0
    gain = min(1.0, 0.9 / crete)          # pas d'écrêtage ; le même gain pour tout
    melange *= gain
    if progression:
        progression(f"séparation de {total / sample_rate / 60:.1f} min d'audio en une passe")
    other = _separe_other(melange, sample_rate, dossier_travail)

    X_sec, X_sep, y, patchs, conditions, retrouve = [], [], [], [], [], []
    for i, (s, f) in enumerate(zip(secs, fonds)):
        seg = np.asarray(other[i * (n + gap):i * (n + gap) + n], dtype=np.float32) / gain
        im, numero, note, vel, db = etiquettes[i]
        e_sec = float(np.sum(s.astype(np.float64) ** 2)) or 1e-12
        # Part de l'énergie du synthé que « other » a conservée : projection du
        # stem séparé sur le rendu propre, bornée à [0, 1].
        part = float(np.dot(seg.astype(np.float64), s.astype(np.float64)) / e_sec)
        X_sec.append(descriptors(s, sample_rate, note, GATE, DUREE))
        X_sep.append(descriptors(seg, sample_rate, note, GATE, DUREE))
        y.append(im); patchs.append(numero); conditions.append((note, vel, db))
        retrouve.append(min(1.0, max(0.0, part)))
    return CorpusSepare(machines, np.asarray(X_sec), np.asarray(X_sep), np.asarray(y, dtype=np.int32),
                        np.asarray(patchs, dtype=np.int32), np.asarray(conditions, dtype=np.float32),
                        np.asarray(retrouve, dtype=np.float32), time.perf_counter() - depart, rejets)


def _separe_other(melange: np.ndarray, sample_rate: int, dossier: Path) -> np.ndarray:
    """Fait passer le mélange par demucs (shifts=0, comme la chaîne) et rend « other »."""
    import torch
    from demucs.api import Separator

    separator = Separator(model="htdemucs", device="cpu", progress=False, shifts=0)
    stereo = torch.from_numpy(np.stack([melange, melange]))
    _, sources = separator.separate_tensor(stereo, sr=sample_rate)
    other = sources["other"].mean(dim=0).cpu().numpy()
    # Le fichier est conservé : c'est lui qui permet d'ÉCOUTER ce que le modèle
    # apprend, et de vérifier qu'un exemple est bien un synthé dans un mélange.
    dossier.mkdir(parents=True, exist_ok=True)
    try:
        import soundfile as sf
        sf.write(str(dossier / "melange.wav"), melange, sample_rate)
        sf.write(str(dossier / "other.wav"), other, sample_rate)
    except Exception:  # noqa: BLE001 — l'écoute est un confort, pas la mesure
        pass
    return other


# ---------------------------------------------------------------------------
# La mesure
# ---------------------------------------------------------------------------

@dataclass
class ModeleSimple:
    """Le même classifieur que `vsm_classifier` (standardisation + gradient
    boosting), sans l'abstention : ici on mesure le CLASSEMENT."""
    noms: List[str]
    moyenne: np.ndarray
    echelle: np.ndarray
    modele: object

    def rangs(self, X: np.ndarray) -> np.ndarray:
        P = self.modele.predict_proba((np.atleast_2d(X) - self.moyenne) / self.echelle)
        return P

    def rang_de(self, x: np.ndarray, machine: str) -> int:
        p = self.rangs(x)[0]
        ordre = list(np.argsort(p)[::-1])
        return ordre.index(self.noms.index(machine)) + 1


def entraine_simple(X: np.ndarray, y: np.ndarray, noms: Sequence[str], graine: int) -> ModeleSimple:
    from sklearn.ensemble import HistGradientBoostingClassifier

    moyenne = X.mean(axis=0); echelle = X.std(axis=0); echelle[echelle < 1e-9] = 1.0
    m = HistGradientBoostingClassifier(random_state=graine, max_iter=200)
    m.fit((X - moyenne) / echelle, y)
    return ModeleSimple(list(noms), moyenne, echelle, m)


def coupe_par_patch(patchs: np.ndarray, part_epreuve: float, graine: int) -> np.ndarray:
    rng = np.random.default_rng(graine)
    uniques = np.unique(patchs)
    # Au moins un patch à l'épreuve : un banc vide ne mesure rien et plante tard.
    combien = max(1, int(len(uniques) * part_epreuve))
    epreuve = set(rng.choice(uniques, size=combien, replace=False).tolist())
    return np.asarray([p in epreuve for p in patchs])


def top_k(modele: ModeleSimple, X: np.ndarray, y: np.ndarray, k: int) -> float:
    P = modele.rangs(X)
    ordre = np.argsort(P, axis=1)[:, ::-1][:, :k]
    return float(np.mean([y[i] in ordre[i] for i in range(len(y))]))


def notes_depuis_midi(chemin: Path, piste: str) -> List[dict]:
    """Les notes d'une piste du MIDI écrit par `vsm_project_export`, en secondes.

    Le tempo est lu dans le fichier (il est posé au début de la première
    piste), et une piste absente est une erreur, pas une liste vide : une
    sonde sans notes mesurerait le silence."""
    import mido

    fichier = mido.MidiFile(str(chemin))
    tempo = 500000                     # 120 bpm, le défaut MIDI
    for message in fichier.tracks[0] if fichier.tracks else []:
        if message.type == "set_tempo":
            tempo = message.tempo
            break
    secondes_par_tick = tempo / 1e6 / fichier.ticks_per_beat
    for track in fichier.tracks:
        if track.name != piste:
            continue
        notes: List[dict] = []
        ouvertes: Dict[int, Tuple[float, int]] = {}
        tick = 0
        for message in track:
            tick += message.time
            if message.type == "note_on" and message.velocity > 0:
                ouvertes[message.note] = (tick * secondes_par_tick, message.velocity)
            elif message.type in ("note_off", "note_on") and message.note in ouvertes:
                debut, velocite = ouvertes.pop(message.note)
                notes.append({"note": int(message.note), "start": debut,
                              "duration": tick * secondes_par_tick - debut,
                              "velocity": int(velocite)})
        return notes
    raise ValueError(f"{chemin} : aucune piste « {piste} » "
                     f"(pistes : {', '.join(t.name for t in fichier.tracks)})")


@dataclass
class Sonde:
    """Un enregistrement réel, ses notes transcrites, et la machine que
    l'arbitrage y retient : le rang médian de cette machine est la mesure."""
    nom: str
    audio: np.ndarray
    sample_rate: int
    notes: List[dict]
    machine: str

    @staticmethod
    def charge(nom: str, chemin_audio: Path, notes: str, machine: str) -> "Sonde":
        """`notes` : un JSON de notes ({start, duration, note}), ou le MIDI d'un
        projet reconstruit suivi du nom de sa piste -- `arrangement.mid#other`.
        C'est la seconde forme qui rend la sonde REJOUABLE : les notes sont
        celles que la chaîne a transcrites et que l'arbitrage a jugées, pas
        une copie intermédiaire."""
        import soundfile as sf

        audio, sr = sf.read(str(chemin_audio), always_2d=True, dtype="float32")
        if "#" in notes:
            chemin, piste = notes.rsplit("#", 1)
            liste = notes_depuis_midi(Path(chemin).expanduser(), piste)
        else:
            liste = json.load(open(Path(notes).expanduser()))
        return Sonde(nom, audio.mean(axis=1), sr, liste, machine)

    def extraits(self, nombre: int = 20) -> List[Tuple[np.ndarray, int]]:
        """`nombre` extraits d'une seconde, à intervalle régulier, avec la note
        que la transcription dit sonner -- comme `eprouve_clairdelune` (A1.2)."""
        duree_totale = self.audio.size / self.sample_rate
        sortie = []
        for k in range(nombre):
            debut = duree_totale * (k + 0.5) / nombre
            candidates = [n for n in self.notes if n["start"] <= debut < n["start"] + n["duration"]]
            if not candidates:
                candidates = [min(self.notes, key=lambda n: abs(n["start"] - debut))]
            note = max(candidates, key=lambda n: n["duration"])["note"]
            extrait = self.audio[int(debut * self.sample_rate):int((debut + DUREE) * self.sample_rate)]
            if extrait.size >= int(0.5 * self.sample_rate):
                sortie.append((extrait, int(note)))
        return sortie

    def rang_median(self, modele: ModeleSimple) -> Tuple[float, float]:
        rangs = []
        for extrait, note in self.extraits():
            x = descriptors(extrait, self.sample_rate, note, GATE, DUREE)
            rangs.append(modele.rang_de(x, self.machine))
        r = np.asarray(rangs)
        return float(np.median(r)), float(np.mean(r <= 5))
