
from basic_pitch.inference import predict
from basic_pitch import ICASSP_2022_MODEL_PATH


def extract_notes(
    audio_path,
):
    """
    Retourne les notes détectées
    par Basic Pitch.
    """

    print(
        "[NOTES] Analyse Basic Pitch..."
    )

    _, _, note_events = (
        predict(
            str(audio_path),
            model_or_model_path=(
                ICASSP_2022_MODEL_PATH
            ),
        )
    )

    notes = []

    for event in note_events:

        start = float(event[0])
        end = float(event[1])
        midi = int(event[2])
        confidence = float(event[3])

        notes.append({
            "start": start,
            "end": end,
            "midi": midi,
            "frequency": float(
                440.0 *
                2.0 **
                ((midi - 69) / 12)
            ),
            "confidence": confidence,
        })

    return notes
