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
    )

    print("[DEMUCS] Séparation...")

    origin, separated = separator.separate_audio_file(
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
