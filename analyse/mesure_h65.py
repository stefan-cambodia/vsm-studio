"""H65 — le niveau d'une pièce de batterie mesuré à SES frappes isolées (docs/CDC-reload-indifferenciable.md § 26).

Les frappes de chaque pièce sont lues au MIDI du projet de la référence ; une frappe est ISOLÉE si aucune autre pièce
ne frappe à moins de `ISOLEMENT_S`. Sur `FENETRE_S` après chaque frappe isolée, le rapport des énergies du stem
« drums » et du rendu solo de la pièce (H59, `reload-h59/solo-<i>.wav`) ; le facteur d'amplitude est la racine de la
médiane de ces rapports. Appliqué aux volumes, le projet est rendu et mesuré par l'outil du § 0, options du témoin.

    mesure_h65.py --projet <dossier> --solos <reload-h59> --stem <drums.wav> --moteur <vsm-render> --sortie <dossier>
"""
from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np

ICI = Path(__file__).resolve().parent
RACINE = ICI.parent
ISOLEMENT_S = 0.060
FENETRE_S = 0.080
FACTEUR_MIN, FACTEUR_MAX = 0.25, 4.0
FRAPPES_MIN = 30
DUREE_S = "312.19"


def lire_mono(chemin: Path) -> Tuple[np.ndarray, int]:
    import soundfile as sf

    x, sr = sf.read(str(chemin), dtype="float64", always_2d=True)
    return x.mean(axis=1), int(sr)


def frappes_par_piste(projet: Path, p: Dict) -> Dict[str, List[float]]:
    """Les instants (s) des notes de chaque piste MIDI, au tempo du projet (un tempo constant)."""
    import mido

    mid = mido.MidiFile(str(projet / p["midi"]["file"]))
    tempos = p["transport"]["tempoChanges"]
    if len(tempos) != 1:
        raise SystemExit(f"REFUS : {len(tempos)} changements de tempo — la mesure suppose un tempo constant")
    seconde_par_tick = 60.0 / (float(tempos[0]["bpm"]) * mid.ticks_per_beat)
    sortie: Dict[str, List[float]] = {}
    for tr in mid.tracks:
        nom = next((m.name for m in tr if m.type == "track_name"), "")
        t = 0
        instants = []
        for m in tr:
            t += m.time
            if m.type == "note_on" and m.velocity > 0:
                instants.append(t * seconde_par_tick)
        sortie[nom] = instants
    return sortie


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--projet", type=Path, required=True)
    ap.add_argument("--solos", type=Path, required=True)
    ap.add_argument("--stem", type=Path, required=True)
    ap.add_argument("--moteur", type=Path, required=True)
    ap.add_argument("--sortie", type=Path, required=True)
    ap.add_argument("--original", type=Path, default=RACINE / "reconstruction" / "sources" / "reload-peschi.wav")
    a = ap.parse_args()
    p = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    groupe = {i for i, t in enumerate(p["tracks"]) if t.get("kind") == "group" and t.get("name") == "Batterie"}
    pieces = [i for i, t in enumerate(p["tracks"]) if t.get("kind") != "group" and t.get("output") in groupe]
    frappes = frappes_par_piste(a.projet, p)
    # Le MIDI nomme les pistes « Batterie - hihat » là où le projet écrit « Batterie · hihat ».
    instants = {i: frappes.get(p["tracks"][i]["name"].replace(" · ", " - "), []) for i in pieces}
    stem, sr = lire_mono(a.stem)
    n_fen = int(FENETRE_S * sr)
    facteurs: Dict[int, float] = {}
    for i in pieces:
        nom = p["tracks"][i]["name"]
        autres = np.sort(np.concatenate([np.asarray(instants[j]) for j in pieces if j != i] or [np.zeros(0)]))
        isolees = [t for t in instants[i]
                   if autres.size == 0 or np.min(np.abs(autres - t)) > ISOLEMENT_S]
        solo, sr_solo = lire_mono(a.solos / f"solo-{i}.wav")
        if sr_solo != sr:
            print(f"REFUS : {nom} rendu à {sr_solo} Hz, le stem à {sr}")
            return 2
        rapports = []
        for t in isolees:
            k = int(round(t * sr))
            if k + n_fen > min(len(stem), len(solo)):
                continue
            es, er = float(np.sum(stem[k:k + n_fen] ** 2)), float(np.sum(solo[k:k + n_fen] ** 2))
            if es > 0 and er > 0:
                rapports.append(es / er)
        if len(rapports) < FRAPPES_MIN:
            print(f"  « {nom} » : {len(instants[i])} frappes, {len(isolees)} isolées, {len(rapports)} mesurables — "
                  f"MOINS DE {FRAPPES_MIN} : la pièce garde son niveau")
            facteurs[i] = 1.0
            continue
        q1, med, q3 = (float(np.percentile(rapports, q)) for q in (25, 50, 75))
        facteur = float(np.clip(math.sqrt(med), FACTEUR_MIN, FACTEUR_MAX))
        facteurs[i] = facteur
        print(f"  « {nom} » : {len(instants[i])} frappes, {len(isolees)} isolées, {len(rapports)} mesurables ; "
              f"rapport d'énergie stem/rendu médiane {med:.3f} (quartiles {q1:.3f} · {q3:.3f}) → "
              f"× {facteur:.3f} ({20 * math.log10(facteur):+.1f} dB)")
    if a.sortie.exists():
        shutil.rmtree(a.sortie)
    shutil.copytree(a.projet, a.sortie / "essai")
    q = json.loads((a.sortie / "essai" / "project.json").read_text(encoding="utf-8"))
    for i, f in facteurs.items():
        v0 = float(q["tracks"][i]["mix"]["volume"])
        q["tracks"][i]["mix"]["volume"] = v0 * f
        print(f"  volume « {q['tracks'][i]['name']} » {v0:.4f} -> {v0 * f:.4f}")
    (a.sortie / "essai" / "project.json").write_text(json.dumps(q, indent=2, ensure_ascii=False, sort_keys=True),
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
          f"niveau {e['niveau_db']['decalage_median']} (pire {e['niveau_db']['pire']}) ; "
          f"kick {e['calage_kick']['median_abs_ms']} / {e['calage_kick']['p90_abs_ms']} ms")
    return 0


if __name__ == "__main__":
    sys.exit(main())
