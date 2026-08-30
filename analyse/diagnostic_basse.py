#!/usr/bin/env python3
"""Où passe l'erreur d'une piste mélodique ?

Le pendant de `diagnostic_batterie.py`, pour une piste jouée par une machine et
non par une boîte à rythmes. Le budget d'erreur (`budget_erreur.py`) désigne la
piste où travailler ; il ne dit pas POURQUOI elle coûte, et sur la basse de
*Sky and Sand* la réponse n'était dans aucune des cases attendues.

CE QUE CET OUTIL MESURE, ET POURQUOI CHACUNE DES DEUX COLONNES EST NÉCESSAIRE.

  - la distance de la piste à SON STEM. C'est la cible que la chaîne optimise,
    piste par piste, et la seule dont elle dispose à ce moment-là ;
  - la distance du MÉLANGE COMPLET à l'original, la piste remplacée par la
    variante. C'est ce qu'on écoute.

Les deux ne s'accordent pas, et `vsm_mix_verdict` existe précisément parce
qu'elles se contredisent. Un diagnostic qui ne rendrait que la première
choisirait une variante que le morceau refuse.

TROIS TÉMOINS ENCADRENT LA LECTURE, et sans eux les distances ne veulent rien
dire :

  1. le projet TEL QUEL — le chiffre publié, qu'on doit retrouver ;
  2. la piste COUPÉE — ce que vaut le morceau sans elle. Une reconstruction qui
     ne bat pas ce témoin-là est pire que rien, et c'est arrivé ;
  3. la piste remplacée par le STEM RÉEL — le plafond, ce qu'une reconstruction
     parfaite de cette piste rapporterait.

LE MÉLANGE N'EST PAS UNE SOMME. Composer un mélange en additionnant des rendus
de pistes donne un autre signal que le rendu du projet (le bus maître n'est pas
linéaire) : mesuré sur *Sky and Sand*, 0,2945 contre 0,2933, avec des écarts
d'échantillon jusqu'à 0,29. Chaque variante est donc RENDUE EN ENTIER, ce qui
coûte une quinzaine de secondes et supprime la question.
"""
from __future__ import annotations

import argparse
import copy
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_distance_cache import cached_distance_for      # noqa: E402
from analyzer.vsm_engine import find_vsm_render                  # noqa: E402
from analyzer.vsm_offline_render import read_render_wav          # noqa: E402
from analyzer.vsm_project_export import ExportNote, ExportTrack   # noqa: E402

SAMPLE_RATE = 44100


def charger_audio(chemin: Path) -> np.ndarray:
    """Mono, à SAMPLE_RATE -- la même lecture que la chaîne."""
    import librosa
    audio, _ = librosa.load(str(chemin), sr=SAMPLE_RATE, mono=True)
    return np.asarray(audio, dtype=np.float32)


def lire_notes(midi: Path, piste: str) -> List[ExportNote]:
    """Les notes d'une piste du projet, en SECONDES.

    Relues dans le MIDI exporté plutôt que recalculées : ce sont exactement
    celles que le projet joue, et refaire la transcription introduirait une
    différence qu'on prendrait ensuite pour un effet de la machine.
    """
    import mido
    fichier = mido.MidiFile(str(midi))
    tempo = 500000
    for tampon in fichier.tracks:
        for message in tampon:
            if message.type == "set_tempo":
                tempo = message.tempo
                break
    notes: List[ExportNote] = []
    for tampon in fichier.tracks:
        if tampon.name != piste:
            continue
        tick, ouvertes = 0, {}
        for message in tampon:
            tick += message.time
            if message.type == "note_on" and message.velocity > 0:
                ouvertes.setdefault(message.note, []).append((tick, message.velocity))
            elif message.type == "note_off" or (message.type == "note_on" and message.velocity == 0):
                if ouvertes.get(message.note):
                    debut, velocite = ouvertes[message.note].pop(0)
                    notes.append(ExportNote(
                        note=int(message.note), velocity=int(velocite),
                        start=mido.tick2second(debut, fichier.ticks_per_beat, tempo),
                        duration=mido.tick2second(tick - debut, fichier.ticks_per_beat, tempo)))
    notes.sort(key=lambda n: n.start)
    return notes


class Melangeur:
    """Rend le projet complet, une piste remplacée par ce qu'on lui donne.

    Le dossier de travail est une COPIE du projet : on n'écrit jamais dans la
    reconstruction qu'on mesure. Sans cela, un diagnostic laisserait le projet
    dans l'état de sa dernière variante, et le chiffre publié cesserait d'être
    reproductible.
    """

    def __init__(self, projet: Path, travail: Path, piste: str, duree: float,
                  moteur: Optional[str] = None) -> None:
        self.piste = piste
        self.duree = duree
        self.moteur = moteur
        self.dossier = travail / "projet"
        if self.dossier.exists():
            shutil.rmtree(self.dossier)
        shutil.copytree(projet, self.dossier,
                        ignore=shutil.ignore_patterns("*.wav", "rapport.json", "comparaison.wav"))
        # Les échantillons, eux, doivent suivre : une piste audio ou un sampler
        # dont le fichier manque rend du SILENCE, sans un mot.
        for source in (projet / "samples").glob("*.wav"):
            (self.dossier / "samples").mkdir(parents=True, exist_ok=True)
            shutil.copy(source, self.dossier / "samples" / source.name)
        self.modele = json.loads((self.dossier / "project.json").read_text())
        self.sortie = travail / "melange.wav"

    def _ecrire(self, modele: Dict) -> None:
        (self.dossier / "project.json").write_text(json.dumps(modele, ensure_ascii=False))

    def _rendre(self) -> Optional[np.ndarray]:
        commande = [str(find_vsm_render(self.moteur)), str(self.dossier), str(self.sortie),
                    "--sample-rate", str(SAMPLE_RATE), "--duration", str(self.duree), "--quiet"]
        try:
            subprocess.run(commande, check=True, capture_output=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            return None
        return read_render_wav(self.sortie)

    def tel_quel(self) -> Optional[np.ndarray]:
        self._ecrire(self.modele)
        return self._rendre()

    def sans_la_piste(self) -> Optional[np.ndarray]:
        modele = copy.deepcopy(self.modele)
        for track in modele["tracks"]:
            if track["name"] == self.piste:
                track["mix"]["muted"] = True
        self._ecrire(modele)
        return self._rendre()

    def piste_seule(self) -> Optional[np.ndarray]:
        """La piste telle que le projet la porte, les autres coupées."""
        modele = copy.deepcopy(self.modele)
        for track in modele["tracks"]:
            track["mix"]["muted"] = track["name"] != self.piste
        self._ecrire(modele)
        return self._rendre()

    def avec(self, machine: str, patch: Dict[str, float], volume: float,
              seule: bool = False) -> Optional[np.ndarray]:
        """Le projet, la piste jouée par `machine` avec `patch` à `volume`.

        `seule` coupe toutes les autres pistes : c'est la même variante, mesurée
        contre son stem au lieu du mélange. Les deux passent par ce chemin
        unique, sans quoi la colonne « stem » et la colonne « mélange »
        finiraient par décrire deux patchs différents.
        """
        modele = copy.deepcopy(self.modele)
        for track in modele["tracks"]:
            if seule:
                track["mix"]["muted"] = track["name"] != self.piste
            if track["name"] != self.piste:
                continue
            preset = self.dossier / track["instrument"]["preset"]
            contenu = json.loads(preset.read_text())
            contenu["machineName"] = machine
            contenu["pluginId"] = machine
            contenu["parameters"] = {k: float(v) for k, v in patch.items()}
            preset.write_text(json.dumps(contenu, ensure_ascii=False))
            track["instrument"]["preferredPlugin"] = machine
            track["mix"]["volume"] = float(volume)
        self._ecrire(modele)
        return self._rendre()

    def volume_cale(self, machine: str, patch: Dict[str, float], stem: np.ndarray) -> float:
        """Le volume qui met la piste au niveau efficace de son stem.

        Comparer deux patchs à des niveaux différents, c'est comparer un patch
        et un fader. La chaîne cale de la même façon (`vsm_levels`), et une
        variante mesurée sans ce calage se ferait écarter pour un volume.
        """
        # SEULE, et c'est le piège : mesurer le niveau efficace du MÉLANGE au
        # lieu de celui de la piste donne un rapport qui n'a aucun sens, et un
        # volume trois fois trop bas. Le calage se fait sur la piste, contre son
        # stem, comme dans `vsm_levels`.
        rendu = self.avec(machine, patch, 1.0, seule=True)
        if rendu is None:
            return 1.0
        efficace = float(np.sqrt(np.mean(np.square(rendu, dtype=np.float64))))
        vise = float(np.sqrt(np.mean(np.square(stem, dtype=np.float64))))
        return 1.0 if efficace <= 1e-9 else min(4.0, vise / efficace)


def main() -> int:
    parseur = argparse.ArgumentParser(description=__doc__,
                                       formatter_class=argparse.RawDescriptionHelpFormatter)
    parseur.add_argument("projet", type=Path, help="dossier de la reconstruction")
    parseur.add_argument("--original", type=Path, required=True, help="le morceau d'origine")
    parseur.add_argument("--stem", type=Path, required=True, help="le stem de la piste")
    parseur.add_argument("--piste", default="bass")
    parseur.add_argument("--travail", type=Path, default=Path("../reconstruction/travail/diag-basse"))
    parseur.add_argument("--metrique", default="v4")
    parseur.add_argument("--moteur", default=None)
    parseur.add_argument("--machines", nargs="*", default=None,
                          help="les machines à essayer ; par défaut, les premières de "
                               "l'arbitrage inscrit dans rapport.json")
    parseur.add_argument("--regler", nargs="*", default=None,
                          help="les machines à RÉGLER avant mesure, avec le budget de la chaîne")
    parseur.add_argument("--budget", type=int, default=120)
    parseur.add_argument("--axes", type=int, default=21)
    args = parseur.parse_args()

    args.travail.mkdir(parents=True, exist_ok=True)
    original = charger_audio(args.original)
    stem = charger_audio(args.stem)
    notes = lire_notes(args.projet / "midi" / "arrangement.mid", args.piste)
    print(f"morceau : {original.size / SAMPLE_RATE:.1f} s — piste « {args.piste} », "
          f"{len(notes)} note(s) — métrique {args.metrique}")

    du_melange = cached_distance_for(args.metrique)(original, SAMPLE_RATE)
    du_stem = cached_distance_for(args.metrique)(stem, SAMPLE_RATE)
    melangeur = Melangeur(args.projet, args.travail, args.piste,
                           original.size / SAMPLE_RATE, args.moteur)

    def d(mesure, audio) -> str:
        return "échec" if audio is None else f"{float(mesure(audio)):.4f}"

    print("\n  TÉMOINS")
    print(f"    projet tel quel                    mélange {d(du_melange, melangeur.tel_quel())}")
    sans = melangeur.sans_la_piste()
    print(f"    piste COUPÉE                       mélange {d(du_melange, sans)}"
          "   <- une reconstruction doit battre ce chiffre")
    seule = melangeur.piste_seule()
    print(f"    piste telle quelle, contre son stem   stem {d(du_stem, seule)}")

    machines: List[str] = args.machines or []
    if not machines:
        rapport = json.loads((args.projet / "rapport.json").read_text())
        for entree in rapport.get("stems", []):
            if entree.get("name") == args.piste:
                machines = [c["machine"] for c in entree.get("trackArbitration", [])][:5]
    print(f"\n  CANDIDATES : {', '.join(machines) if machines else 'aucune'}")

    a_regler = set(args.regler or [])
    moteur_service: Dict[str, object] = {}

    def service():
        if "m" not in moteur_service:
            from analyzer.vsm_engine import VsmEngine
            moteur_service["m"] = VsmEngine(binary=args.moteur, sample_rate=SAMPLE_RATE)
        return moteur_service["m"]

    resultats: List[Tuple[str, Optional[float], Optional[float]]] = []
    for machine in machines:
        for regle in (False, True):
            if regle and machine not in a_regler:
                continue
            patch: Dict[str, float] = {}
            etiquette = f"{machine} (usine)"
            if regle:
                from analyzer.vsm_track_refine import refine_patch_on_track
                affine = refine_patch_on_track(
                    machine=machine, parameters={}, notes=list(notes), stem_audio=stem,
                    engine=service(), workdir=args.travail / "reglage" / machine,
                    budget=args.budget, axes=args.axes, sample_rate=SAMPLE_RATE,
                    metric=args.metrique, binary=args.moteur, name=args.piste)
                if affine is None:
                    print(f"    {machine:24s} réglage impossible")
                    continue
                patch = dict(affine.parameters)
                etiquette = f"{machine} (réglée)"
                print(f"    {etiquette:24s} réglage {affine.start_distance:.4f} -> "
                      f"{affine.distance:.4f} ({affine.evaluations} évaluations)")
            volume = melangeur.volume_cale(machine, patch, stem)
            melange = melangeur.avec(machine, patch, volume)
            seule_ = melangeur.avec(machine, patch, volume, seule=True)
            ds = None if seule_ is None else float(du_stem(seule_))
            dm = None if melange is None else float(du_melange(melange))
            resultats.append((etiquette, ds, dm))
            print(f"    {etiquette:24s} stem {ds if ds is None else f'{ds:.4f}'}"
                  f"   mélange {dm if dm is None else f'{dm:.4f}'}   (volume {volume:.2f})")

    if resultats:
        valides = [(e, s, m) for e, s, m in resultats if m is not None]
        if valides:
            print(f"\n  meilleure AU MÉLANGE : {min(valides, key=lambda x: x[2])[0]}")
            meilleure_stem = [(e, s, m) for e, s, m in valides if s is not None]
            if meilleure_stem:
                print(f"  meilleure AU STEM    : {min(meilleure_stem, key=lambda x: x[1])[0]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
