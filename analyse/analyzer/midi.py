from pathlib import Path

from basic_pitch.inference import predict_and_save
from basic_pitch import ICASSP_2022_MODEL_PATH


def generate_midi(
    audio_path: Path,
    output_dir: Path,
):

    output_dir.mkdir(
        parents=True,
        exist_ok=True,
    )

    print()
    print("[MIDI] Transcription...")

    predict_and_save(
        [str(audio_path)],
        str(output_dir),

        save_midi=True,
        sonify_midi=False,
        save_model_outputs=False,
        save_notes=True,

        model_or_model_path=ICASSP_2022_MODEL_PATH,
    )

    midi_files = list(
        output_dir.glob("*.mid")
    )

    csv_files = list(
        output_dir.glob("*.csv")
    )

    return {
        "midi": str(
            midi_files[0]
        ) if midi_files else None,

        "notes_csv": str(
            csv_files[0]
        ) if csv_files else None,
    }
