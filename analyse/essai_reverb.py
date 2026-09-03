#!/usr/bin/env python3
"""H24 — la chaîne rend des pistes SÈCHES contre un disque réverbéré.

Prend le projet d'une course terminée, y insère une réverbération (l'effet
« reverb » du DAW, Freeverb) sur les pistes MÉLODIQUES à plusieurs dosages, et
mesure chaque rendu contre l'original avec la métrique et la cible de la
chaîne. UNE variable : le dosage. Patchs, volumes, notes, moteur sont ceux de
la course.

Le témoin (aucun effet) doit retrouver la distance inscrite dans rapport.json :
c'est la preuve que la mesure est bien la même. Le dosage 0 est inséré aussi,
comme épreuve de plomberie : l'effet à mélange nul doit rendre le témoin au bit
près.

Usage :
  analyse/.venv/bin/python analyse/essai_reverb.py \\
      reconstruction/travail/usandthem-h22b reconstruction/travail/sources/us-and-them.mp3 \\
      --sortie reconstruction/travail/h24 --dosages 0 8 15 25 40
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from reconstruire import SAMPLE_RATE, charger_audio, lire_wav  # noqa: E402
from analyzer.vsm_engine import find_vsm_render  # noqa: E402
from analyzer.vsm_reconstruct import reconstruction_distance  # noqa: E402

# La voix est EXCLUE : report intégral de l'enregistrement, elle porte déjà la
# réverbération du disque. La batterie aussi, par choix d'hypothèse : H24 parle
# des pistes mélodiques (§ 5 quaterdecies), et une seule variable à la fois.
PISTES_AUDIO = {"audio"}


def melodique(piste: dict) -> bool:
    return piste.get("kind", "instrument") not in PISTES_AUDIO and piste.get("channel", 0) != 9


def preparer(temoin: Path, sortie: Path, nom: str, dosage: float | None,
             taille: float, amortissement: float) -> Path:
    dossier = sortie / nom
    if dossier.exists():
        shutil.rmtree(dossier)
    dossier.mkdir(parents=True)
    for sous in ("instruments", "midi", "samples"):
        if (temoin / sous).exists():
            os.symlink((temoin / sous).resolve(), dossier / sous)
    projet = json.loads((temoin / "project.json").read_text(encoding="utf-8"))
    touchees = []
    for piste in projet["tracks"]:
        if dosage is None or not melodique(piste):
            continue
        piste["effects"] = [{
            "type": "reverb",
            "parameters": {
                "effect.reverb.mix": dosage,
                "effect.reverb.size": taille,
                "effect.reverb.damping": amortissement,
                "effect.reverb.width": 1.0,
            },
        }]
        touchees.append(piste["name"])
    (dossier / "project.json").write_text(json.dumps(projet, indent=1, ensure_ascii=False),
                                          encoding="utf-8")
    (dossier / "pistes-touchees.json").write_text(json.dumps(touchees, ensure_ascii=False))
    return dossier


def rendre(moteur: str, dossier: Path, duree: float | None = None) -> float:
    debut = time.time()
    commande = [moteur, str(dossier), str(dossier / "reconstruit.wav"),
                "--sample-rate", str(SAMPLE_RATE), "--quiet"]
    if duree is not None:
        commande += ["--duration", f"{duree:.6f}"]
    subprocess.run(commande, check=True)
    return time.time() - debut


def midi_vide(chemin: Path) -> None:
    """Un fichier MIDI sans une note : le moteur en exige un, la sonde n'en a
    pas besoin (sa seule piste est audio)."""
    piste = b"\x00\xff\x51\x03\x07\xa1\x20" + b"\x00\xff\x2f\x00"   # tempo 120, fin de piste
    chemin.write_bytes(b"MThd" + (6).to_bytes(4, "big") + (1).to_bytes(2, "big")
                       + (1).to_bytes(2, "big") + (480).to_bytes(2, "big")
                       + b"MTrk" + len(piste).to_bytes(4, "big") + piste)


def sonder_metrique(args, moteur: str, melange, metrique: str, dosages) -> list:
    """L'original lui-même, rendu par le moteur comme piste AUDIO avec la
    réverbération insérée. Sa distance à l'original nu mesure ce que la
    métrique fait d'une queue de réverbération SEULE, sans qu'aucune machine
    n'entre en jeu."""
    import soundfile as sf
    resultats = []
    for dosage in dosages:
        dossier = args.sortie / f"original-reverb-{int(round(dosage * 100)):02d}"
        if dossier.exists():
            shutil.rmtree(dossier)
        (dossier / "samples").mkdir(parents=True)
        (dossier / "midi").mkdir()
        midi_vide(dossier / "midi" / "arrangement.mid")
        sf.write(str(dossier / "samples" / "original.wav"), melange, SAMPLE_RATE, subtype="FLOAT")
        projet = {
            "format": "vsm-project", "version": 2, "title": "sonde",
            "midi": {"file": "midi/arrangement.mid"},
            "transport": {"tempoChanges": [{"bpm": 120.0, "tick": 0}], "ticksPerQuarterNote": 480,
                          "timeSignatures": [{"denominator": 4, "numerator": 4, "tick": 0}],
                          "loop": {"enabled": False, "endTick": 0, "startTick": 0}},
            "tracks": [{
                "name": "original", "kind": "audio", "channel": 0,
                "audio": {"file": "samples/original.wav", "channels": 1,
                          "sampleRate": float(SAMPLE_RATE), "frames": int(melange.size)},
                "mix": {"muted": False, "pan": 0.0, "sends": [0.0, 0.0], "solo": False, "volume": 1.0},
                "effects": [{"type": "reverb", "parameters": {
                    "effect.reverb.mix": dosage, "effect.reverb.size": args.taille,
                    "effect.reverb.damping": args.amortissement, "effect.reverb.width": 1.0}}],
            }],
        }
        (dossier / "project.json").write_text(json.dumps(projet, indent=1), encoding="utf-8")
        rendre(moteur, dossier, duree=melange.size / SAMPLE_RATE)
        rendu = lire_wav(dossier / "reconstruit.wav")
        resultats.append({"dosage": dosage,
                          "distance": reconstruction_distance(melange, rendu, SAMPLE_RATE, metric=metrique)})
    return resultats


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("projet", type=Path, help="dossier de la course témoin (project.json + rapport.json)")
    ap.add_argument("original", type=Path, help="l'original, cible de la mesure")
    ap.add_argument("--sortie", type=Path, required=True)
    ap.add_argument("--dosages", type=float, nargs="+", default=[0, 8, 15, 25, 40],
                    help="mélange de la réverbération, en pour cent")
    ap.add_argument("--taille", type=float, default=0.6, help="effect.reverb.size (défaut du DAW : 0,6)")
    ap.add_argument("--amortissement", type=float, default=0.5, help="effect.reverb.damping (défaut : 0,5)")
    ap.add_argument("--metrique", default=None, help="défaut : celle du rapport de la course")
    ap.add_argument("--rendus-paralleles", type=int, default=3)
    ap.add_argument("--moteur", default=None)
    ap.add_argument("--sonde-metrique", action="store_true",
                    help="applique aussi la même réverbération à l'ORIGINAL, rendu comme piste "
                         "audio par le même moteur, et mesure sa distance à lui-même : dit si "
                         "la métrique voit une queue de réverbération, et de combien")
    args = ap.parse_args()

    rapport = json.loads((args.projet / "rapport.json").read_text(encoding="utf-8"))
    metrique = args.metrique or rapport.get("metric", "v2")
    attendu = rapport.get("globalDistance")
    moteur = str(find_vsm_render(args.moteur))
    print(f"témoin : {args.projet}  (rapport : {attendu:.6f}, métrique {metrique})")
    print(f"moteur : {moteur}")

    variantes = [("temoin", None)] + [(f"reverb-{int(round(d)):02d}", d / 100.0) for d in args.dosages]
    dossiers = {nom: preparer(args.projet, args.sortie, nom, dosage, args.taille, args.amortissement)
                for nom, dosage in variantes}

    print(f"rendu de {len(dossiers)} variantes, {args.rendus_paralleles} de front…", flush=True)
    durees: dict[str, float] = {}
    with ThreadPoolExecutor(max_workers=args.rendus_paralleles) as pool:
        futurs = {nom: pool.submit(rendre, moteur, d) for nom, d in dossiers.items()}
        for nom, futur in futurs.items():
            durees[nom] = futur.result()
            print(f"  {nom:12s} rendu en {durees[nom]:5.1f} s", flush=True)

    melange = charger_audio(args.original)
    resultats = []
    temoin_rendu = lire_wav(dossiers["temoin"] / "reconstruit.wav")
    print()
    print(f"{'variante':12s} {'dosage':>7s} {'distance':>10s} {'écart':>9s}")
    for nom, dosage in variantes:
        rendu = lire_wav(dossiers[nom] / "reconstruit.wav")
        distance = reconstruction_distance(melange, rendu, SAMPLE_RATE, metric=metrique)
        identique = bool(rendu.size == temoin_rendu.size and (rendu == temoin_rendu).all())
        ecart = (distance / attendu - 1.0) * 100.0 if attendu else float("nan")
        resultats.append({"variante": nom, "dosage": dosage, "distance": distance,
                          "ecartPourcent": ecart, "identiqueAuTemoin": identique,
                          "dureeRendu": durees[nom]})
        note = "  (= témoin au bit près)" if identique and dosage is not None else ""
        print(f"{nom:12s} {'—' if dosage is None else f'{dosage*100:4.0f} %':>7s} "
              f"{distance:10.6f} {ecart:+8.2f} %{note}")

    sonde = []
    if args.sonde_metrique:
        print()
        print("sonde de la métrique : l'ORIGINAL passé dans la même réverbération, contre lui-même")
        sonde = sonder_metrique(args, moteur, melange, metrique, [d for _, d in variantes if d])
        for ligne in sonde:
            print(f"{'original':12s} {ligne['dosage']*100:4.0f} % {ligne['distance']:10.6f}")

    (args.sortie / "resultats.json").write_text(json.dumps({
        "sondeMetrique": sonde,
        "hypothese": "H24",
        "temoin": str(args.projet),
        "attenduRapport": attendu,
        "metrique": metrique,
        "moteur": moteur,
        "pistesTouchees": json.loads((dossiers[variantes[-1][0]] / "pistes-touchees.json").read_text()),
        "reverb": {"size": args.taille, "damping": args.amortissement, "width": 1.0},
        "resultats": resultats,
    }, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"\nécrit : {args.sortie / 'resultats.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
