from pathlib import Path
import torch

from demucs.api import Separator
from demucs.audio import save_audio


def separate_audio(
    audio_path: Path,
    output_dir: Path,
    model_name: str = "htdemucs",
):
    output_dir.mkdir(parents=True, exist_ok=True)

    device = "cuda" if torch.cuda.is_available() else "cpu"

    print()
    print("[DEMUCS]")
    print(f"  modèle : {model_name}")
    print(f"  device : {device}")

    separator = Separator(
        model=model_name,
        device=device,
        progress=True,
        # DÉTERMINISME. Le défaut de demucs est shifts=1 : un décalage
        # temporel TIRÉ AU SORT avant la séparation (le « shift trick »),
        # non seedé. Deux exécutions sur le même fichier rendaient donc des
        # stems différents -- mesuré : 101 notes transcrites contre 156 sur
        # le même morceau, et toute comparaison AVANT/APRÈS devenait
        # incomparable. Le gain de qualité du shift trick (~0,1 dB SDR) ne
        # vaut pas la perte de reproductibilité pour un outil dont le métier
        # est de MESURER des écarts.
        shifts=0,
    )

    print("[DEMUCS] Séparation...")

    _origin, separated = separator.separate_audio_file(
        Path(audio_path)
    )

    stems = {}

    for source, audio in separated.items():
        output = output_dir / f"{source}.wav"

        save_audio(
            audio,
            output,
            samplerate=separator.samplerate,
        )

        stems[source] = output

        print(f"  {source:10s} -> {output}")

    return stems
