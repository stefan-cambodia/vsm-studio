
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


# H45 (docs/CDC-reload-indifferenciable.md § 7) : le seuil d'attaque choisi PAR STEM.
SEUIL_USINE = 0.5
SEUIL_TENU = 0.7
INDICE_TENU = 0.4


def _evenements(sortie, seuil):
    import numpy as np
    import basic_pitch.note_creation as infer
    from basic_pitch.constants import AUDIO_SAMPLE_RATE, FFT_HOP
    min_len = int(np.round(127.7 / 1000 * (AUDIO_SAMPLE_RATE / FFT_HOP)))
    _, ev = infer.model_output_to_notes(sortie, onset_thresh=seuil, frame_thresh=0.3, min_note_len=min_len,
                                        min_freq=None, max_freq=None, multiple_pitch_bends=False,
                                        melodia_trick=True, midi_tempo=120)
    return ev


def indice_de_hachure(evenements, ecart=0.030):
    """Part des notes suivies d'une note de même hauteur à moins de `ecart` secondes."""
    from collections import defaultdict
    par_h = defaultdict(list)
    for e in evenements:
        par_h[int(e[2])].append((float(e[0]), float(e[1])))
    suivies = 0
    for liste in par_h.values():
        liste.sort()
        suivies += sum(1 for (_a0, a1), (b0, _b1) in zip(liste, liste[1:], strict=False) if b0 - a1 < ecart)
    return suivies / max(len(evenements), 1)


def extract_notes_seuil_par_stem(audio_path):
    """Comme `extract_notes`, le seuil d'attaque choisi sur l'indice de hachure du stem.

    La sortie du modèle est calculée UNE fois ; les notes au seuil d'usine donnent
    l'indice ; au-delà de `INDICE_TENU`, les notes sont redérivées à `SEUIL_TENU`.
    Rend (notes, choix) — `choix` dit l'indice et le seuil retenus.
    """
    from basic_pitch.inference import run_inference
    print("[NOTES] Analyse Basic Pitch (seuil d'attaque par stem)...")
    sortie = run_inference(str(audio_path), ICASSP_2022_MODEL_PATH)
    usine = _evenements(sortie, SEUIL_USINE)
    indice = indice_de_hachure(usine)
    seuil = SEUIL_TENU if indice > INDICE_TENU else SEUIL_USINE
    evenements = usine if seuil == SEUIL_USINE else _evenements(sortie, seuil)
    notes = [{"start": float(e[0]), "end": float(e[1]), "midi": int(e[2]),
              "frequency": float(440.0 * 2.0 ** ((int(e[2]) - 69) / 12)), "confidence": float(e[3])}
             for e in evenements]
    return notes, {"indice": round(indice, 4), "seuil": seuil, "notesUsine": len(usine),
                   "notes": len(notes), "indiceTenu": INDICE_TENU}
