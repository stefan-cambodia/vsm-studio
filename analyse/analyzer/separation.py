from pathlib import Path
import torch

from demucs.api import Separator
from demucs.audio import save_audio


def choisir_device() -> str:
    """Le processeur de calcul, dans l'ordre de ce qui est disponible.

    XPU AVANT CUDA parce que la machine de développement n'a qu'un iGPU Intel,
    et que l'ordre n'a aucune importance ailleurs : les deux ne coexistent pas.
    MESURÉ sur les 5 min de Clair de Lune, même modèle, mêmes options, même
    environnement : séparation 89,6 s en CPU contre 31,7 s en XPU (2,8x), et
    les quatre stems sont les mêmes -- corrélation 1,000000, écart maximal
    4,2e-07, c'est-à-dire l'arrondi du float32. Un gain de temps payé par un
    autre résultat n'en serait pas un ; celui-ci ne l'est pas.

    Le repli est silencieux dans son choix mais pas dans son compte rendu :
    l'appelant IMPRIME le device retenu, et c'est ainsi qu'on sait, en relisant
    un journal, sur quoi la séparation a tourné.
    """
    if hasattr(torch, "xpu") and torch.xpu.is_available():
        return "xpu"
    if torch.cuda.is_available():
        return "cuda"
    return "cpu"


def separate_audio(
    audio_path: Path,
    output_dir: Path,
    model_name: str = "htdemucs",
):
    output_dir.mkdir(parents=True, exist_ok=True)

    device = choisir_device()

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


# ---------------------------------------------------------------------------
# Exécutable en module : `python -m analyzer.separation entrée dossier modèle`.
#
# POURQUOI. La séparation vivait dans le processus de la chaîne, et torch +
# demucs y restent résidents (~7 Go) pendant que Basic Pitch charge le sien :
# l'OOM killer a abattu deux courses le 02/09/2026 sur la machine à 15 Go.
# En sous-processus, demucs vit, écrit ses stems, MEURT — et la mémoire
# revient avant que la chaîne ne continue. C'est ce point d'entrée que
# `reconstruire.py` lance désormais (voir `separer`).
# ---------------------------------------------------------------------------

def main(arguments=None) -> int:
    import argparse

    parseur = argparse.ArgumentParser(
        description="Sépare un fichier audio en stems (demucs), seul dans son processus.")
    parseur.add_argument("entree", help="fichier audio à séparer")
    parseur.add_argument("dossier", help="dossier de sortie des stems")
    parseur.add_argument("modele", nargs="?", default="htdemucs",
                         help="modèle demucs (htdemucs, htdemucs_6s…)")
    args = parseur.parse_args(arguments)
    stems = separate_audio(Path(args.entree), Path(args.dossier), args.modele)
    return 0 if stems else 3


if __name__ == "__main__":
    raise SystemExit(main())
