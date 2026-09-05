"""Un « séparateur » de résidu pour les tests : le résidu entier devient other.wav (copié tel quel)."""
import shutil, sys, pathlib
entree, dossier, modele = sys.argv[1], pathlib.Path(sys.argv[2]), sys.argv[3]
dossier.mkdir(parents=True, exist_ok=True)
shutil.copyfile(entree, dossier / "other.wav")
print(f"[factice] {entree} -> {dossier}/other.wav (modèle demandé : {modele})")
