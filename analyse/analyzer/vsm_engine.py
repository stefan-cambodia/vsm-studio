"""
Pont vers le moteur audio du DAW (VSM Studio).

POURQUOI CE MODULE EXISTE : `synth_engine.py` synthétise les notes candidates
en Python. C'est parfait pour chercher vite, mais ce qu'il produit n'est PAS ce
que jouera le DAW -- les filtres, les enveloppes et la saturation n'y sont pas
les mêmes. Un patch optimisé contre l'approximation Python sonne donc
différemment une fois chargé dans le vrai instrument, et l'écart n'apparaît
qu'à la fin, quand il est trop tard pour le corriger.

Ici, les notes candidates sont rendues par le MOTEUR RÉEL, celui de
l'application et des plugins CLAP. Le patch trouvé est directement jouable :
il n'y a plus de traduction entre ce qu'on optimise et ce qu'on entend.

COMMENT : le binaire `vsm-render --serve` reste vivant et répond à des requêtes
JSON, une par ligne. Les machines sont instanciées une seule fois. Un rendu
coûte ~7 ms contre ~24 ms si l'on relançait un processus à chaque évaluation --
et l'optimiseur en fait des milliers par note.

Les paramètres se désignent par leur identifiant SÉMANTIQUE
(`filter.1.cutoff`, `envelope.1.attack`...), jamais par un numéro interne :
c'est ce qui permet d'écrire un patch sans rien connaître du code du DAW, et de
viser une autre machine sans réécrire l'appel.
"""

from __future__ import annotations

import base64
import json
import os
import subprocess
import shutil
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import numpy as np


def identite_du_moteur(moteur) -> Dict[str, object]:
    """QUI A RENDU L'AUDIO — le binaire, pas le commit.

    POURQUOI CE BLOC EXISTE, ET IL A COÛTÉ DEUX COURSES. La provenance de
    `rapport.json` nommait le commit du dépôt et rien d'autre. Or le rendu ne
    sort pas du dépôt : il sort de `build/tools/vsm-render`, qui peut dater
    d'AVANT le commit annoncé. Le 02/09/2026, les courses v13 et v14 (terminées
    à 10:12 et 11:05) ont tourné avec un moteur compilé à 08:48, donc sans les
    sept machines écrites entre 09:13 et 10:17. Leur rapport annonce un commit
    dont le vivier compte quarante-sept machines mélodiques ; la course en a vu
    quarante et une, et rien ne le disait.

    À appeler PENDANT que le moteur est vivant : `machines()` interroge le
    processus. La première version vivait dans `reconstruire.py` et était
    appelée après la fermeture du moteur — elle retombait sur le repli « je ne
    sais pas » à chaque course, en silence, ce que la course v11a a montré.

    La fonction vit ICI, à côté de `find_vsm_render`, parce que tous les
    programmes qui créent un moteur (corpus, banc de batterie, classifieur…)
    doivent pouvoir s'en servir : la panne de v13/v14 peut se reproduire par
    chacune de ces portes, et un corpus bâti sur un moteur périmé empoisonne
    les modèles plus durablement qu'une course.
    """
    try:
        chemin = Path(str(moteur.binary)).resolve()
        stat = chemin.stat()
        return {
            "chemin": str(chemin),
            "compile": datetime.fromtimestamp(stat.st_mtime).isoformat(timespec="seconds"),
            "octets": stat.st_size,
            "machines": len(moteur.machines()),
        }
    except Exception:  # noqa: BLE001 — un moteur qu'on ne sait pas décrire se dit
        return {"chemin": str(getattr(moteur, "binary", "")), "compile": "", "octets": 0,
                "machines": 0}


def moteur_perime(moteur, racine: Optional[Path] = None) -> Optional[str]:
    """Le binaire est-il plus vieux que les sources qu'il prétend porter ?

    UN BINAIRE PÉRIMÉ NE SE SIGNALE PAS COMME PÉRIMÉ (leçon du § 9 de
    docs/ROADMAP-interop.md). Il ne plante pas, il ne se plaint pas : il rend
    un vivier plus petit, et la course mesure autre chose que ce qu'on croit.
    Cette fonction rend la phrase à imprimer, ou None si tout va bien — elle
    n'imprime rien elle-même : chaque point d'entrée décide de son journal.

    On ne regarde que les dossiers dont le moteur est FAIT : `audio/` porte les
    machines, `core/` et `interchange/` ce qu'elles traversent. Un changement
    dans `app/` ne périme pas le rendu — sans cette borne, toute retouche
    d'interface ferait crier la chaîne, et l'avertissement deviendrait un bruit
    de fond qu'on apprend à ignorer.

    `racine` s'injecte pour les tests ; par défaut, la racine du dépôt est
    déduite de ce fichier.
    """
    try:
        if racine is None:
            racine = Path(__file__).resolve().parent.parent.parent
        binaire = Path(str(moteur.binary)).resolve()
        compile_le = binaire.stat().st_mtime
        plus_recent, nom = compile_le, None
        for dossier in ("audio", "core", "interchange"):
            for fichier in (racine / dossier).rglob("*"):
                if fichier.suffix not in (".h", ".cpp", ".inc"):
                    continue
                horodatage = fichier.stat().st_mtime
                if horodatage > plus_recent:
                    plus_recent, nom = horodatage, fichier
        if nom is None:
            return None
        return (f"ATTENTION : le moteur date du "
                f"{datetime.fromtimestamp(compile_le).isoformat(timespec='seconds')} et "
                f"{nom.relative_to(racine)} du "
                f"{datetime.fromtimestamp(plus_recent).isoformat(timespec='seconds')}. "
                f"Ce rendu N'EST PAS celui du code présent — recompiler vsm-render, "
                f"ou lire ce rapport en le sachant.")
    except Exception:  # noqa: BLE001
        return None


DEFAULT_BINARY_CANDIDATES = (
    "build/tools/vsm-render",
    "../build/tools/vsm-render",
    "../../build/tools/vsm-render",
)


class VsmEngineError(RuntimeError):
    pass


@dataclass(frozen=True)
class SearchDimension:
    """
    Une dimension de recherche, telle que LE MOTEUR la déclare.

    `importance` sert à choisir les N dimensions à explorer quand le budget
    d'évaluations est court. Ce n'est PAS un poids dans la distance : la
    distance se mesure sur le son, jamais sur les paramètres.
    """
    semantic_id: str
    low: float
    high: float
    logarithmic: bool = False
    importance: float = 0.5
    unit: str = ""


def find_vsm_render(explicit: Optional[str] = None) -> Path:
    """
    Localise le binaire de rendu. Cherché dans le dépôt puis dans le PATH ;
    une erreur explicite vaut mieux qu'un pont qui échoue silencieusement à la
    première requête.
    """
    if explicit:
        path = Path(explicit)
        if path.is_file():
            return path
        raise VsmEngineError(f"binaire introuvable : {explicit}")

    here = Path(__file__).resolve()
    for parent in (here.parent, *here.parents):
        for candidate in DEFAULT_BINARY_CANDIDATES:
            path = (parent / candidate).resolve()
            if path.is_file():
                return path

    found = shutil.which("vsm-render")
    if found:
        return Path(found)

    raise VsmEngineError(
        "vsm-render introuvable. Compilez-le :\n"
        "    cmake --build build --target vsm-render"
    )


@dataclass
class Note:
    note: int
    velocity: int = 100
    start: float = 0.0
    duration: float = 1.0


class VsmEngine:
    """
    Rend des notes avec le moteur du DAW.

    S'utilise en gestionnaire de contexte, ou en appelant `close()` : le
    processus enfant doit être arrêté, sinon il survit à la session Python.

        with VsmEngine() as engine:
            audio = engine.render_note(
                "vsm.tb303",
                {"filter.1.cutoff": 700.0, "filter.1.resonance": 0.8},
                midi_note=45,
                duration=1.0,
            )
    """

    def __init__(
        self,
        binary: Optional[str] = None,
        sample_rate: int = 44100,
        block_size: int = 256,
    ):
        self.binary = find_vsm_render(binary)
        self.sample_rate = sample_rate
        self.block_size = block_size
        self._request_id = 0
        # Profil multi-échantillons retenu pour toute la durée du moteur.
        # `None` = pas encore demandé, `""` = demandé et aucun installé.
        self._profile_choice: Optional[str] = None
        self._process = subprocess.Popen(
            [str(self.binary), "--serve"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )

    # -- cycle de vie ---------------------------------------------------

    def __enter__(self) -> "VsmEngine":
        return self

    def __exit__(self, *exc_info) -> None:
        self.close()

    def close(self) -> None:
        if self._process.poll() is not None:
            return
        try:
            self._process.stdin.close()
            self._process.wait(timeout=5)
        except Exception:
            self._process.kill()

    # -- rendu ----------------------------------------------------------

    def _query(self, payload: Dict[str, object]) -> Dict[str, object]:
        """
        Envoie une CONSULTATION au moteur et renvoie sa réponse.

        Les consultations partagent le tube des rendus, volontairement : le
        client interroge le profil d'une machine juste avant de lancer sa
        boucle sur cette même machine, dans le même processus. Un second canal
        (fichier annexe, autre exécutable) laisserait le client chercher selon
        des bornes qui ne seraient plus celles du moteur.
        """
        if self._process.poll() is not None:
            raise VsmEngineError("le moteur de rendu s'est arrêté")

        self._request_id += 1
        request = {k: v for k, v in payload.items() if not k.startswith("_")}
        request["id"] = self._request_id
        self._process.stdin.write(json.dumps(request) + "\n")
        self._process.stdin.flush()

        line = self._process.stdout.readline()
        if not line:
            stderr = self._process.stderr.read() if self._process.stderr else ""
            raise VsmEngineError(f"aucune réponse du moteur. {stderr}")
        response = json.loads(line)
        if not response.get("ok"):
            raise VsmEngineError(response.get("error", "consultation refusée"))

        # Un moteur ANTÉRIEUR au protocole de consultation ne connaît pas le
        # champ « query » : il traite la ligne comme une demande de rendu, rend
        # une note sur la machine par défaut, et répond « ok » -- sans la
        # charge utile attendue. Sans ce contrôle, l'appelant recevrait une
        # liste vide et chercherait dans le vide en croyant tout aller bien.
        expected = payload.get("_expect")
        if expected and expected not in response:
            raise VsmEngineError(
                f"le moteur n'a pas répondu à la consultation « {payload.get('query')} » : "
                f"champ « {expected} » absent. Binaire vsm-render probablement périmé "
                f"(reconstruire et recopier à la racine du projet)."
            )
        return response

    def machines(self) -> List[str]:
        """Machines que le moteur sait réellement instancier."""
        return list(self._query({"query": "machines", "_expect": "machines"})["machines"])

    # --- profils multi-échantillons -------------------------------------
    #
    # POURQUOI C'EST LE MOTEUR QU'ON INTERROGE, et pas un dossier lu ici : la
    # règle « le moteur est la source de vérité du rendu » (feuille de route
    # § 0) vaut aussi pour ce qu'il sait charger. Une découverte faite côté
    # Python pourrait retenir un profil que le moteur refuse -- et l'écart ne
    # se verrait qu'au moment où toutes les distances seraient déjà fausses.

    def profiles(self) -> List[Dict[str, object]]:
        """Profils multi-échantillons installés, tels que le moteur les voit."""
        try:
            return list(self._query({"query": "profiles", "_expect": "profiles"})["profiles"])
        except VsmEngineError:
            # Moteur plus ancien que cette consultation : pas de profil, dit
            # franchement plutôt que déguisé en liste vide silencieuse.
            return []

    def profile_for(self, machine: str) -> Optional[str]:
        """Chemin du profil à employer pour cette machine, ou None.

        Le choix est mémorisé pour la durée du moteur : deux rendus de la même
        exécution doivent porter le même profil, sans quoi deux distances de la
        même série ne se comparent plus -- c'est la leçon du § 10.3 de la
        feuille de route, appliquée à une condition de plus.

        QUEL PROFIL, ET POURQUOI C'EST DIT. La première version prenait « le
        premier profil valide », c'est-à-dire le premier par ORDRE ALPHABÉTIQUE
        de nom de fichier. Installer une seconde banque de piano (« YDP-Grand »,
        qui se classe avant « piano-salamander ») a donc changé en silence ce
        que `vsm.multisample` jouait -- et le classifieur entraîné la veille a
        été refusé pour péremption, à juste titre, sans que personne ait décidé
        quoi que ce soit. Le mécanisme de péremption a fait son travail ; le
        choix silencieux, lui, était le défaut.

        Désormais : `VSM_PROFIL` (variable d'environnement, nom déclaré ou nom
        de fichier) désigne le profil ; à défaut, le premier est pris ET LE
        CHOIX EST IMPRIMÉ s'il y en avait plusieurs, avec la liste des autres.
        """
        if machine not in _MACHINES_A_PROFIL:
            return None
        if self._profile_choice is None:
            valides = [p for p in self.profiles() if not p.get("error")]
            voulu = os.environ.get("VSM_PROFIL", "").strip()
            choisi = None
            if voulu:
                for p in valides:
                    if p.get("name") == voulu or str(p.get("path", "")).endswith(voulu):
                        choisi = p
                        break
                if choisi is None:
                    print(f"      profil « {voulu} » (VSM_PROFIL) introuvable parmi "
                          f"{', '.join(str(p.get('name')) for p in valides) or 'aucun'} "
                          f"-- repli sur le premier")
            if choisi is None and valides:
                choisi = valides[0]
                if len(valides) > 1:
                    print(f"      {len(valides)} profils installés — « {choisi.get('name')} » "
                          f"est le profil PAR DÉFAUT de la machine (VSM_PROFIL=nom "
                          f"pour en choisir un autre) ; l'arbitrage de piste, lui, "
                          f"les met TOUS en concurrence.")
            self._profile_choice = str(choisi["path"]) if choisi else ""
        return self._profile_choice or None

    def search_profile(self, machine: str) -> List["SearchDimension"]:
        """
        Espace de recherche DÉCLARÉ PAR LE MOTEUR pour cette machine.

        C'est le point de l'étape 8.2 de la feuille de route : les bornes, les
        échelles et l'ordre d'importance viennent de la machine, jamais d'une
        liste écrite ici. Trois raisons, toutes constatées avant de le faire :

          - une machine sans filtre (l'orgue à roues phoniques) était cherchée
            sur « filter.1.cutoff », c'est-à-dire sur rien ;
          - une machine dont le filtre n'est pas le sujet (table d'ondes,
            supersaw) l'était sur ses paramètres secondaires ;
          - ajouter une machine au DAW n'ajoutait rien ici, donc elle héritait
            d'un espace pensé pour une autre.
        """
        response = self._query(
            {"query": "searchProfile", "machine": machine, "_expect": "dimensions"}
        )
        return [
            SearchDimension(
                semantic_id=str(entry["id"]),
                low=float(entry["low"]),
                high=float(entry["high"]),
                logarithmic=(entry.get("scale") == "log"),
                importance=float(entry.get("importance", 0.5)),
                unit=str(entry.get("unit", "")),
            )
            for entry in response.get("dimensions", [])
        ]

    def parameters(self, machine: str) -> List[Dict[str, object]]:
        """Tous les paramètres sémantiques d'une machine, avec leurs bornes."""
        return list(
            self._query({"query": "parameters", "machine": machine, "_expect": "parameters"})["parameters"]
        )

    def render(
        self,
        machine: str,
        parameters: Dict[str, float],
        notes: Sequence[Note],
        duration: float,
        sample_rate: Optional[int] = None,
        samples: Optional[Dict[int, str]] = None,
    ) -> np.ndarray:
        """
        Rend `notes` sur `machine` et renvoie l'audio mono (float32).
        Lève VsmEngineError si le moteur refuse la requête.
        """
        if self._process.poll() is not None:
            raise VsmEngineError("le moteur de rendu s'est arrêté")

        self._request_id += 1
        request = {
            "id": self._request_id,
            "machine": machine,
            "sampleRate": sample_rate or self.sample_rate,
            "blockSize": self.block_size,
            "duration": float(duration),
            "parameters": {k: float(v) for k, v in parameters.items()},
            "notes": [
                {
                    "note": int(n.note),
                    "velocity": int(n.velocity),
                    "start": float(n.start),
                    "duration": float(n.duration),
                }
                for n in notes
            ],
            "returnAudio": "base64-f32-mono",
        }
        profile = self.profile_for(machine)
        if profile:
            request["profile"] = profile
        if samples:
            # Échantillons à charger dans la machine (sampler) : c'est ainsi
            # qu'un coup découpé d'un enregistrement se rejoue tel quel.
            request["samples"] = {str(int(slot)): str(path) for slot, path in samples.items()}

        self._process.stdin.write(json.dumps(request) + "\n")
        self._process.stdin.flush()

        line = self._process.stdout.readline()
        if not line:
            stderr = self._process.stderr.read() if self._process.stderr else ""
            raise VsmEngineError(f"aucune réponse du moteur. {stderr}")

        response = json.loads(line)
        if not response.get("ok"):
            raise VsmEngineError(response.get("error", "erreur inconnue"))

        audio = response.get("audio")
        if not audio:
            return np.zeros(0, dtype=np.float32)
        return np.frombuffer(base64.b64decode(audio), dtype=np.float32)

    def render_batch(
        self,
        machine: str,
        parameter_sets: Sequence[Dict[str, float]],
        notes: Sequence[Note],
        duration: float,
        sample_rate: Optional[int] = None,
        samples: Optional[Dict[int, str]] = None,
    ) -> List[np.ndarray]:
        """
        Rend PLUSIEURS jeux de paramètres en une seule requête.

        C'est le cas d'une génération d'optimiseur : même machine, mêmes notes,
        seuls les réglages changent. Un aller-retour de tube au lieu de N.

        Chaque jeu reçoit sa propre instance de machine côté moteur : deux lots
        identiques rendent donc exactement le même son, comme deux requêtes
        isolées.
        """
        if not parameter_sets:
            return []
        if self._process.poll() is not None:
            raise VsmEngineError("le moteur de rendu s'est arrêté")

        self._request_id += 1
        request = {
            "id": self._request_id,
            "machine": machine,
            "sampleRate": sample_rate or self.sample_rate,
            "blockSize": self.block_size,
            "duration": float(duration),
            "batch": [{k: float(v) for k, v in jeu.items()} for jeu in parameter_sets],
            "notes": [
                {"note": int(n.note), "velocity": int(n.velocity),
                 "start": float(n.start), "duration": float(n.duration)}
                for n in notes
            ],
            "returnAudio": "base64-f32-mono",
        }
        profile = self.profile_for(machine)
        if profile:
            request["profile"] = profile
        if samples:
            request["samples"] = {str(int(slot)): str(path) for slot, path in samples.items()}

        self._process.stdin.write(json.dumps(request) + "\n")
        self._process.stdin.flush()
        line = self._process.stdout.readline()
        if not line:
            stderr = self._process.stderr.read() if self._process.stderr else ""
            raise VsmEngineError(f"aucune réponse du moteur. {stderr}")
        response = json.loads(line)
        if not response.get("ok"):
            raise VsmEngineError(response.get("error", "erreur inconnue"))

        audios = response.get("batchAudio")
        if audios is None:
            # Moteur antérieur au rendu par lot : il a traité la requête comme
            # un rendu simple. On le DIT plutôt que de rendre une liste vide.
            raise VsmEngineError(
                "le moteur ne sait pas rendre par lot (champ « batchAudio » absent). "
                "Binaire vsm-render probablement périmé."
            )
        return [np.frombuffer(base64.b64decode(un), dtype=np.float32) for un in audios]

    def render_note(
        self,
        machine: str,
        parameters: Dict[str, float],
        midi_note: int,
        duration: float,
        velocity: int = 100,
        gate: float = 0.75,
        sample_rate: Optional[int] = None,
        samples: Optional[Dict[int, str]] = None,
    ) -> np.ndarray:
        """
        Une note seule -- le cas de l'optimiseur de patch.

        `gate` : proportion de la durée pendant laquelle la touche est tenue.
        Le reste laisse le release s'éteindre, sans quoi on comparerait des
        notes coupées net et l'optimiseur choisirait des release courts pour de
        mauvaises raisons.
        """
        held = max(0.01, duration * gate)
        return self.render(
            machine,
            parameters,
            [Note(note=midi_note, velocity=velocity, start=0.0, duration=held)],
            duration=duration,
            sample_rate=sample_rate,
            samples=samples,
        )


# ---------------------------------------------------------------------------
# Correspondance entre les paramètres de `synth_engine.SynthParams` et le
# vocabulaire sémantique du DAW.
#
# Tous les paramètres Python n'ont pas d'équivalent : `distortion`, `noise` et
# les mixages d'effets dépendent de la machine visée. Ce qui n'a pas de
# correspondance est OMIS -- jamais rapproché d'un paramètre "à peu près
# semblable", ce qui produirait un son faux sans le dire.
# ---------------------------------------------------------------------------

SYNTH_PARAM_TO_SEMANTIC = {
    "osc_level": "oscillator.1.level",
    "osc_detune_cents": None,          # en cents ici, en demi-tons côté DAW : converti plus bas
    "sub_level": "oscillator.1.subLevel",
    "filter_cutoff": "filter.1.cutoff",
    "filter_resonance": "filter.1.resonance",
    "attack": "envelope.1.attack",
    "decay": "envelope.1.decay",
    "sustain": "envelope.1.sustain",
    "release": "envelope.1.release",
    "noise": "oscillator.noise.level",
    "distortion": None,                # pas d'équivalent direct : ignoré sciemment
    "chorus_mix": None,
    "delay_mix": None,
    "reverb_mix": None,
    "waveform": "oscillator.1.waveform",
}

WAVEFORM_TO_INDEX = {
    "saw": 0.0,
    "square": 1.0,
    "pulse": 1.0,
    "triangle": 2.0,
    "sine": 3.0,
}


def synth_params_to_semantic(params) -> Dict[str, float]:
    """
    Convertit un `SynthParams` (synth_engine.py) en paramètres sémantiques.
    Les champs sans correspondance sont laissés de côté, pas devinés.
    """
    values: Dict[str, float] = {}
    for field, semantic_id in SYNTH_PARAM_TO_SEMANTIC.items():
        if semantic_id is None:
            continue
        if not hasattr(params, field):
            continue
        value = getattr(params, field)
        if field == "waveform":
            index = WAVEFORM_TO_INDEX.get(str(value))
            if index is not None:
                values[semantic_id] = index
            continue
        values[semantic_id] = float(value)

    # Le détune : cents côté Python, demi-tons côté DAW.
    detune = getattr(params, "osc_detune_cents", None)
    if detune:
        values["oscillator.2.detune"] = float(detune) / 100.0

    return values


def synthesize_note_vsm(
    midi_note: int,
    duration: float,
    params,
    sr: int = 44100,
    machine: str = "vsm.minimoog",
    engine: Optional[VsmEngine] = None,
) -> np.ndarray:
    """
    Remplaçant direct de `synth_engine.synthesize_note`, rendu par le moteur
    réel. Même signature, plus le choix de la machine.

    Fournir `engine` pour réutiliser un processus déjà lancé : sans lui, un
    processus est créé et détruit à chaque appel, ce qui ruine l'intérêt du
    mode service dans une boucle d'optimisation.
    """
    own_engine = engine is None
    active = engine or VsmEngine(sample_rate=sr)
    try:
        return active.render_note(
            machine,
            synth_params_to_semantic(params),
            midi_note=midi_note,
            duration=duration,
            sample_rate=sr,
        )
    finally:
        if own_engine:
            active.close()


# Liste de SECOURS, employée uniquement quand aucun moteur n'est joignable
# (tests hors ligne, documentation). Elle est forcément en retard sur le DAW --
# c'est pourquoi elle n'est plus la source de vérité : `available_machines`
# interroge le moteur dès qu'il y en a un.
# Machines qui n'ont AUCUN son tant qu'un profil n'est pas installé. Elles ne
# sont pas des candidates par défaut : sans profil elles rendent du silence, et
# une distance mesurée contre du silence est un chiffre faux -- sur une cible
# douce, la machine muette gagnerait.
_MACHINES_A_PROFIL = {"vsm.multisample"}

_FALLBACK_MACHINES = [
    "vsm.minimoog", "vsm.tb303", "vsm.juno106", "vsm.jupiter8", "vsm.prophet",
    "vsm.sh101", "vsm.ms20", "vsm.arpodyssey", "vsm.dx7", "vsm.tr808", "vsm.tr909",
    "vsm.sampler", "vsm.epiano", "vsm.obx", "vsm.supersaw", "vsm.wavetable",
    "vsm.pcmhybrid", "vsm.tonewheel", "vsm.generic",
]


def available_machines(engine: Optional[VsmEngine] = None) -> List[str]:
    """
    Machines que le DAW sait instancier.

    Interroge le moteur quand il y en a un : une liste écrite ici serait
    forcément en retard sur lui, et c'est exactement ce qui s'est produit --
    six machines ajoutées au DAW restaient invisibles à l'analyse.
    """
    if engine is not None:
        try:
            machines = engine.machines()
            if machines:
                return machines
        except VsmEngineError:
            pass  # moteur muet : on retombe sur la liste de secours, sans mentir
    return list(_FALLBACK_MACHINES)
