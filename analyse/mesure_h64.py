"""H64, mesure 1 — la règle de `analyzer/vsm_calage_bandes.py` appliquée à « Reload » (CDC-reload § 25).

Un CONTRÔLE de la fonction sur le morceau qui l'a fait naître, pas une preuve de l'idée : les rendus solo des pièces
de batterie sont ceux de H59 (`reload-h59/solo-<i>.wav`, `build-h42`), le stem celui de la référence ; les facteurs
trouvés sont appliqués aux volumes du projet de la référence, le projet rendu et mesuré par l'outil du § 0 avec les
options du témoin.

    mesure_h64.py --projet <dossier> --solos <reload-h59> --stem <drums.wav> --moteur <vsm-render> --sortie <dossier>
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))

from analyzer.vsm_calage_bandes import energies_par_bande, facteurs_par_bande  # noqa: E402

RACINE = ICI.parent
DUREE_S = "312.19"


def lire_mono(chemin: Path):
    import soundfile as sf

    x, sr = sf.read(str(chemin), dtype="float32", always_2d=True)
    return x.mean(axis=1), int(sr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--projet", type=Path, required=True)
    ap.add_argument("--solos", type=Path, required=True)
    ap.add_argument("--stem", type=Path, required=True)
    ap.add_argument("--moteur", type=Path, required=True)
    ap.add_argument("--sortie", type=Path, required=True)
    ap.add_argument("--original", type=Path, default=RACINE / "reconstruction" / "sources" / "reload-peschi.wav")
    a = ap.parse_args()
    base = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    groupe = {i for i, t in enumerate(base["tracks"]) if t.get("kind") == "group" and t.get("name") == "Batterie"}
    pieces = [i for i, t in enumerate(base["tracks"]) if t.get("kind") != "group" and t.get("output") in groupe]
    stem, sr = lire_mono(a.stem)
    e_stem = energies_par_bande(stem, sr)
    energies = {}
    for i in pieces:
        solo = a.solos / f"solo-{i}.wav"
        if not solo.is_file() or solo.stat().st_size == 0:
            print(f"REFUS : {solo} absent — les rendus solo de H59 manquent")
            return 2
        y, sr_y = lire_mono(solo)
        energies[base["tracks"][i]["name"]] = energies_par_bande(y, sr_y)
    facteurs = facteurs_par_bande(e_stem, energies)
    print("FACTEURS (règle de H64) :")
    for f in facteurs:
        print(f"  « {f.nom} » : {f.raison}")
    if a.sortie.exists():
        shutil.rmtree(a.sortie)
    shutil.copytree(a.projet, a.sortie / "essai")
    p = json.loads((a.sortie / "essai" / "project.json").read_text(encoding="utf-8"))
    par_nom = {f.nom: f.facteur for f in facteurs}
    for i in pieces:
        t = p["tracks"][i]
        v0 = float(t["mix"]["volume"])
        t["mix"]["volume"] = v0 * par_nom[t["name"]]
        print(f"  volume « {t['name']} » {v0:.4f} -> {t['mix']['volume']:.4f}")
    (a.sortie / "essai" / "project.json").write_text(json.dumps(p, indent=2, ensure_ascii=False, sort_keys=True),
                                                     encoding="utf-8")
    wav = a.sortie / "essai.wav"
    r = subprocess.run([str(a.moteur), str(a.sortie / "essai"), str(wav), "--sample-rate", "44100", "--duration",
                        DUREE_S, "--format", "float32", "--quiet"], capture_output=True, text=True)
    if r.returncode != 0 or not wav.is_file():
        print(f"REFUS : rendu raté rc={r.returncode}")
        return 1
    ecart = a.sortie / "ecart-essai.json"
    r = subprocess.run([sys.executable, str(RACINE / "tools" / "ecart-a-l-original.py"), str(a.original), str(wav),
                        "--tempo", "136", "--json", str(ecart)], capture_output=True, text=True)
    (a.sortie / "ecart-essai.txt").write_text(r.stdout + r.stderr, encoding="utf-8")
    if r.returncode != 0:
        print(f"REFUS : outil du § 0 rc={r.returncode}")
        return 1
    e = json.loads(ecart.read_text(encoding="utf-8"))
    print("ESSAI (outil du § 0) : " + " · ".join(f"{n} {v['median_db']:+.2f}" for n, v in e["equilibre"].items())
          + f" ; log-mel {e['logmel_db']['ecart_abs_moyen']} (médian {e['logmel_db']['ecart_abs_median']}) ; "
          f"kick {e['calage_kick']['median_abs_ms']} / {e['calage_kick']['p90_abs_ms']} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
