#!/usr/bin/env python3
"""Transcrire UNE PLAGE d'un fichier audio en notes MIDI, pour le DAW (D20.4).

La chaîne (`reconstruire.py`) transcrit un morceau ENTIER, séparé en stems ;
ici, c'est le geste de Live (« Convert to MIDI ») : le clip qu'on a sous la
souris devient des notes, sur une piste neuve, et c'est tout. Même
transcripteur (Basic Pitch), mêmes vélocités tirées de l'énergie du son que la
chaîne (`reconstruire.extraire_notes`, réemployée et non recopiée), même
confiance par note -- qui marque les notes douteuses dans le piano roll.

    analyse/.venv/bin/python analyse/transcrire_clip.py fichier.wav --debut 12.5 --fin 20.0 --sortie notes.json

Écrit un JSON : { "fichier", "debut", "fin", "notes": [ {"note", "velocity",
"start", "duration", "confidence"} ], "secondes" } -- `start` en secondes DANS
LE FICHIER (la plage est rétablie), ce que l'application replace sur sa ligne
de temps. Sans --fin, jusqu'au bout ; sans --debut, depuis le début.

CE MODULE N'EST PAS IMPORTÉ PAR LA CHAÎNE : il l'importe. Une course en cours
ne le voit pas, et il ne touche à rien de ce qu'elle a chargé.
"""
from __future__ import annotations

import argparse
import json
import sys
import tempfile
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))


def transcrire_plage(fichier: Path, debut: float = 0.0, fin: float | None = None) -> dict:
    """Les notes de `[debut, fin]` de `fichier`, en secondes dans le fichier."""
    import soundfile as sf
    from reconstruire import SAMPLE_RATE, charger_audio, extraire_notes

    depart = time.perf_counter()
    audio = charger_audio(fichier)
    duree = audio.size / SAMPLE_RATE
    debut = max(0.0, float(debut))
    fin = duree if fin is None else min(duree, float(fin))
    if fin <= debut:
        raise ValueError(f"plage vide : de {debut:.3f} à {fin:.3f} s (le fichier dure {duree:.3f} s)")
    extrait = audio[int(round(debut * SAMPLE_RATE)):int(round(fin * SAMPLE_RATE))]
    with tempfile.TemporaryDirectory(prefix="vsm-transcrire-") as temporaire:
        chemin = Path(temporaire) / "extrait.wav"
        sf.write(str(chemin), extrait.astype(np.float32), SAMPLE_RATE, subtype="FLOAT")
        notes = extraire_notes(chemin)
    return {
        "fichier": str(fichier), "debut": debut, "fin": fin,
        "notes": [
            {"note": int(n.note), "velocity": int(n.velocity),
             "start": round(float(n.start) + debut, 6), "duration": round(float(n.duration), 6),
             "confidence": round(float(n.confidence), 4)}
            for n in notes
        ],
        "secondes": round(time.perf_counter() - depart, 2),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fichier", type=Path, help="fichier audio (wav, mp3, flac…)")
    ap.add_argument("--debut", type=float, default=0.0, help="début de la plage, en secondes")
    ap.add_argument("--fin", type=float, default=None, help="fin de la plage, en secondes (défaut : la fin)")
    ap.add_argument("--sortie", type=Path, default=None, help="fichier JSON (défaut : la sortie standard)")
    args = ap.parse_args()
    if not args.fichier.exists():
        print(f"[ERREUR] fichier introuvable : {args.fichier}", file=sys.stderr)
        return 1
    try:
        resultat = transcrire_plage(args.fichier, args.debut, args.fin)
    except ValueError as erreur:
        print(f"[ERREUR] {erreur}", file=sys.stderr)
        return 1
    texte = json.dumps(resultat, indent=1, ensure_ascii=False)
    if args.sortie:
        args.sortie.write_text(texte + "\n", encoding="utf-8")
        print(f"{len(resultat['notes'])} note(s) de {resultat['debut']:.2f} à {resultat['fin']:.2f} s "
              f"en {resultat['secondes']} s -> {args.sortie}")
    else:
        print(texte)
    return 0


if __name__ == "__main__":
    sys.exit(main())
