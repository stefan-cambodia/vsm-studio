"""H59 — LA BATTERIE CALÉE PISTE PAR PISTE SUR LES BANDES DE SON STEM (docs/CDC-reload-indifferenciable.md § 20).

Le projet de la course de référence ; ses pistes de batterie (celles qui sortent dans le groupe « Batterie »)
rendues SOLO par le moteur (les autres pistes qui sonnent rendues muettes, les groupes jamais — la règle de
l'export des stems) ; l'énergie de sept bandes de chacune et du stem « drums » ; les volumes résolus aux moindres
carrés NON NÉGATIFS, chaque bande pesée par l'inverse de son énergie au stem (une erreur relative) ; le projet
réécrit avec ces volumes et RIEN d'autre ; rendu, puis mesuré par l'outil du § 0 avec les options du témoin.

    mesure_h59.py mesurer --projet <dossier> --stem <drums.wav> --moteur <vsm-render> --sortie <dossier>
                          --temoin <ecart-temoin.json>
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Dict, List, Tuple

import numpy as np

RACINE = Path(__file__).resolve().parent.parent
BANDES: Tuple[Tuple[float, float], ...] = ((20, 60), (60, 150), (150, 500), (500, 2000), (2000, 6000),
                                           (6000, 10000), (10000, 16000))
DUREE_S = 312.19
TEMPO_OUTIL = "136"   # celui de la mesure du témoin (reload-suite.sh), pour que les deux se comparent


def energies(chemin: Path) -> np.ndarray:
    """L'énergie de chaque bande (FFT de Hann par blocs de 2 s, un bloc sur trois) du mélange mono."""
    import soundfile as sf

    x, sr = sf.read(str(chemin), dtype="float32", always_2d=True)
    m = x.mean(axis=1).astype(np.float64)
    n = int(sr) * 2
    f = np.fft.rfftfreq(n, 1.0 / sr)
    fenetre = np.hanning(n)
    acc = np.zeros(len(BANDES))
    for i in range(0, len(m) - n, n * 3):
        p = np.abs(np.fft.rfft(m[i:i + n] * fenetre)) ** 2
        for j, (a, b) in enumerate(BANDES):
            acc[j] += float(p[(f >= a) & (f < b)].sum())
    return acc


def rendre(moteur: Path, dossier: Path, wav: Path) -> int:
    r = subprocess.run([str(moteur), str(dossier), str(wav), "--sample-rate", "44100", "--duration", str(DUREE_S),
                        "--format", "float32", "--quiet"], capture_output=True, text=True)
    if r.returncode != 0 or not wav.is_file() or wav.stat().st_size == 0:
        print(f"  RENDU RATÉ ({dossier.name}) rc={r.returncode} : {(r.stderr or r.stdout)[-300:]}")
        return 1
    return 0


def copier(projet: Path, vers: Path) -> Dict[str, Any]:
    import shutil

    if vers.exists():
        shutil.rmtree(vers)
    shutil.copytree(projet, vers)
    return json.loads((vers / "project.json").read_text(encoding="utf-8"))


def ecrire(dossier: Path, p: Dict[str, Any]) -> None:
    (dossier / "project.json").write_text(json.dumps(p, indent=2, ensure_ascii=False, sort_keys=True), encoding="utf-8")


def pistes_de_batterie(p: Dict[str, Any]) -> List[int]:
    """Les pistes qui sortent dans le groupe nommé « Batterie »."""
    groupes = {i for i, t in enumerate(p["tracks"]) if t.get("kind") == "group" and t.get("name") == "Batterie"}
    return [i for i, t in enumerate(p["tracks"]) if t.get("kind") != "group" and t.get("output") in groupes]


def mesurer(a: argparse.Namespace) -> int:
    from scipy.optimize import nnls

    for chemin in (a.projet / "project.json", a.stem, a.moteur, a.temoin):
        if not chemin.is_file() or chemin.stat().st_size == 0:
            print(f"REFUS : {chemin} absent ou vide")
            return 2
    depart = time.time()
    a.sortie.mkdir(parents=True, exist_ok=True)
    base = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    batterie = pistes_de_batterie(base)
    if not batterie:
        print("REFUS : aucune piste ne sort dans un groupe « Batterie »")
        return 2
    print("PISTES DE BATTERIE : " + ", ".join(f"{i} « {base['tracks'][i]['name']} » volume {base['tracks'][i]['mix']['volume']:.4f}"
                                            for i in batterie))
    e_stem = energies(a.stem)
    colonnes: List[np.ndarray] = []
    for i in batterie:
        dossier = a.sortie / f"solo-{i}"
        p = copier(a.projet, dossier)
        for j, t in enumerate(p["tracks"]):
            if t.get("kind") != "group":
                t["mix"]["muted"] = j != i
        ecrire(dossier, p)
        wav = a.sortie / f"solo-{i}.wav"
        if rendre(a.moteur, dossier, wav):
            return 1
        colonnes.append(energies(wav))
    e_pistes = np.stack(colonnes, axis=1)   # bandes × pistes
    poids = 1.0 / np.maximum(e_stem, 1e-30)
    x, residu = nnls(e_pistes * poids[:, None], np.ones(len(BANDES)))
    gains = np.sqrt(x)
    print("BANDES (dB, part du total) — stem drums : "
          + "  ".join(f"{b[0]}-{b[1]}:{10 * np.log10(e_stem[k] / e_stem.sum()):.1f}" for k, b in enumerate(BANDES)))
    volumes: Dict[int, Tuple[float, float]] = {}
    for k, i in enumerate(batterie):
        v0 = float(base["tracks"][i]["mix"]["volume"])
        volumes[i] = (v0, v0 * float(gains[k]))
        print(f"  « {base['tracks'][i]['name']} » : g² = {x[k]:.4f}, volume {v0:.4f} -> {v0 * gains[k]:.4f}")
    avant = e_pistes.sum(axis=1)
    apres = (e_pistes * x[None, :]).sum(axis=1)
    print("  écart au stem par bande (dB), volume commun -> résolu : "
          + "  ".join(f"{b[0]}-{b[1]}:{10 * np.log10(avant[k] / e_stem[k]):+.1f}->{10 * np.log10(max(apres[k], 1e-30) / e_stem[k]):+.1f}"
                      for k, b in enumerate(BANDES)))

    essai = a.sortie / "essai"
    p = copier(a.projet, essai)
    for i, (_v0, v1) in volumes.items():
        p["tracks"][i]["mix"]["volume"] = v1
    ecrire(essai, p)
    wav = a.sortie / "essai.wav"
    if rendre(a.moteur, essai, wav):
        return 1
    ecart = a.sortie / "ecart-essai.json"
    r = subprocess.run([sys.executable, str(RACINE / "tools" / "ecart-a-l-original.py"), str(a.original), str(wav),
                        "--tempo", TEMPO_OUTIL, "--json", str(ecart)], capture_output=True, text=True)
    (a.sortie / "ecart-essai.txt").write_text(r.stdout + r.stderr, encoding="utf-8")
    if r.returncode != 0 or not ecart.is_file():
        print(f"REFUS : l'outil du § 0 a échoué (rc={r.returncode})")
        return 1
    t = json.loads(a.temoin.read_text(encoding="utf-8"))
    e = json.loads(ecart.read_text(encoding="utf-8"))
    mesure = {"batterie": batterie, "volumes": {str(i): v for i, v in volumes.items()}, "g2": x.tolist(),
              "residu": float(residu), "temoin": str(a.temoin), "essai": str(ecart),
              "duree_s": round(time.time() - depart, 1)}
    (a.sortie / "mesure.json").write_text(json.dumps(mesure, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    imprimer(t, e)
    print(f"ÉCRIT : {a.sortie / 'mesure.json'} ({mesure['duree_s']} s)")
    return 0


def imprimer(t: Dict[str, Any], e: Dict[str, Any]) -> None:
    print("\nTÉMOIN -> ESSAI (outil du § 0, écart médian par bande en dB) :")
    for nom in t["equilibre"]:
        a_, b_ = t["equilibre"][nom]["median_db"], e["equilibre"][nom]["median_db"]
        print(f"  {nom:12s} {a_:+6.2f} -> {b_:+6.2f}   (|écart| {abs(a_):.2f} -> {abs(b_):.2f})")
    print(f"  log-mel moyen {t['logmel_db']['ecart_abs_moyen']:.2f} -> {e['logmel_db']['ecart_abs_moyen']:.2f} dB")
    print(f"  niveau : décalage {t['niveau_db']['decalage_median']:+.2f} -> {e['niveau_db']['decalage_median']:+.2f} dB, "
          f"pire tranche {t['niveau_db']['pire']:.2f} -> {e['niveau_db']['pire']:.2f}")
    print(f"  kick : |médian| {t['calage_kick']['median_abs_ms']:.2f} -> {e['calage_kick']['median_abs_ms']:.2f} ms, "
          f"p90 {t['calage_kick']['p90_abs_ms']:.2f} -> {e['calage_kick']['p90_abs_ms']:.2f}")
    print(f"  largeur {t['largeur_side_sur_mid']['reconstruction']} -> {e['largeur_side_sur_mid']['reconstruction']}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sous = ap.add_subparsers(dest="mode", required=True)
    m = sous.add_parser("mesurer")
    m.add_argument("--projet", type=Path, required=True)
    m.add_argument("--stem", type=Path, required=True)
    m.add_argument("--moteur", type=Path, required=True)
    m.add_argument("--sortie", type=Path, required=True)
    m.add_argument("--temoin", type=Path, required=True)
    m.add_argument("--original", type=Path, default=RACINE / "reconstruction" / "sources" / "reload-peschi.wav")
    a = ap.parse_args()
    return mesurer(a)


if __name__ == "__main__":
    sys.exit(main())
