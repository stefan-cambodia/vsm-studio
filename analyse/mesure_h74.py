"""H74 — toutes les pièces de batterie à leurs frappes, résolues ENSEMBLE (docs/CDC-reload-indifferenciable.md § 35).

Pour chaque pièce *i* : sa BANDE PRINCIPALE *bᵢ* (celle des six bandes de `mesure_h72.BANDES_DENSITE` où son rendu
solo a le plus d'énergie sur le morceau), et, sur ses frappes (fenêtres de `AVANT_S` avant à `APRES_S` après),
l'énergie moyenne dans *bᵢ* du stem, `S(i)`, et de chaque rendu solo *j*, `Eⱼ(i)`. Le système `S(i) = Σⱼ xⱼ Eⱼ(i)`,
chaque équation divisée par `S(i)` (erreur relative), se résout en `xⱼ ≥ 0` par moindres carrés positifs ; le facteur
de volume de la pièce *j* est `√xⱼ`, borné à `BORNES` — toute borne atteinte est DITE. Hypothèse de l'instrument,
nommée : les énergies des pièces s'additionnent (elles ne sont pas cohérentes entre elles).

Les énergies se lisent sur une STFT de 1 024 points à 22 050 Hz, pas de 256 (11,6 ms), comme la densité de H72.

    mesure_h74.py --projet <dossier> --stem <drums.wav> --solo "Batterie - hihat=<wav>" ... --sortie <json>
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Dict, List, Mapping, Sequence, Tuple

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mesure_h69 as h69  # noqa: E402
import mesure_h72 as h72  # noqa: E402

AVANT_S = 0.010
APRES_S = 0.100
BORNES = (0.25, 4.0)
SR_ANALYSE = 22050
PAS = 256


def bandes_par_trame(x: np.ndarray, sr: int) -> np.ndarray:
    """Énergie par bande (lignes : `BANDES_DENSITE`) et par trame."""
    import librosa

    y = librosa.resample(x, orig_sr=sr, target_sr=SR_ANALYSE) if sr != SR_ANALYSE else x
    s = np.abs(librosa.stft(y, n_fft=1024, hop_length=PAS)) ** 2
    f = librosa.fft_frequencies(sr=SR_ANALYSE, n_fft=1024)
    return np.stack([s[(f >= a) & (f < b)].sum(axis=0) for a, b in h72.BANDES_DENSITE])


def bande_principale(b: np.ndarray) -> int:
    return int(np.argmax(b.sum(axis=1)))


def energie_aux_frappes(b: np.ndarray, instants: Sequence[float], bande: int) -> float:
    """L'énergie moyenne, par frappe, de la bande sur ses fenêtres [t − AVANT_S, t + APRES_S]."""
    trames = b.shape[1]
    total, n = 0.0, 0
    for t in instants:
        i0 = int(math.floor((t - AVANT_S) * SR_ANALYSE / PAS))
        i1 = int(math.ceil((t + APRES_S) * SR_ANALYSE / PAS))
        if i0 < 0 or i1 > trames:
            continue
        total += float(b[bande, i0:i1].sum())
        n += 1
    if n == 0:
        raise ValueError("aucune fenêtre entière")
    return total / n


def resoudre(stem: np.ndarray, solos: Mapping[str, np.ndarray], frappes: Mapping[str, Sequence[float]]
             ) -> Dict[str, object]:
    """Les facteurs de volume des pièces, résolus ensemble. `stem` et `solos` : sorties de `bandes_par_trame`."""
    from scipy.optimize import nnls

    noms = list(solos)
    bandes = {n: bande_principale(solos[n]) for n in noms}
    s = np.array([energie_aux_frappes(stem, frappes[i], bandes[i]) for i in noms])
    e = np.array([[energie_aux_frappes(solos[j], frappes[i], bandes[i]) for j in noms] for i in noms])
    a = e / s[:, None]
    x, residu = nnls(a, np.ones(len(noms)))
    facteurs: Dict[str, float] = {}
    dits: List[str] = []
    for n, xi in zip(noms, x, strict=True):
        g = math.sqrt(max(float(xi), 0.0))
        if g < BORNES[0] or g > BORNES[1]:
            dits.append(f"« {n} » : facteur {g:.3f} hors de {BORNES}, ramené à la borne")
            g = min(max(g, BORNES[0]), BORNES[1])
        facteurs[n] = round(g, 4)
    # ce que le stem a à ses frappes, rapporté à ce que la batterie RÉGLÉE y rend : 1 si le système est résolu
    x_borne = np.array([facteurs[n] ** 2 for n in noms])
    rapport = (e @ x_borne) / s
    return {"bandes": {n: f"{h72.BANDES_DENSITE[b][0]}-{h72.BANDES_DENSITE[b][1]}" for n, b in bandes.items()},
            "facteurs": facteurs, "facteurs_db": {n: round(20 * math.log10(g), 2) for n, g in facteurs.items()},
            "residu_relatif": round(float(residu), 4),
            "rendu_sur_stem_aux_frappes": {n: round(float(r), 3) for n, r in zip(noms, rapport, strict=True)},
            "avant_reglage_sur_stem": {n: round(float(r), 3) for n, r in zip(noms, e.sum(axis=1) / s, strict=True)},
            "dits": dits}


def main() -> int:
    from mesure_h65 import frappes_par_piste

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--projet", type=Path, required=True)
    ap.add_argument("--stem", type=Path, required=True)
    ap.add_argument("--solo", action="append", required=True, help="« nom de la piste MIDI=fichier.wav »")
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args()
    solos_chemins: List[Tuple[str, Path]] = [(s.split("=", 1)[0], Path(s.split("=", 1)[1])) for s in a.solo]
    for chemin in [a.projet / "project.json", a.stem] + [c for _, c in solos_chemins]:
        if not chemin.is_file() or chemin.stat().st_size == 0:
            print(f"REFUS : {chemin} absent ou vide")
            return 2
    p = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    pistes = frappes_par_piste(a.projet, p)
    for nom, _ in solos_chemins:
        if nom not in pistes or not pistes[nom]:
            print(f"REFUS : piste « {nom} » absente ou sans frappe ({', '.join(pistes)})")
            return 2
    xs, sr = h69.lire_mono(a.stem)
    stem = bandes_par_trame(xs, sr)
    solos: Dict[str, np.ndarray] = {}
    for nom, chemin in solos_chemins:
        x, srs = h69.lire_mono(chemin)
        if srs != sr:
            print(f"REFUS : {chemin} à {srs} Hz, le stem à {sr} Hz")
            return 2
        solos[nom] = bandes_par_trame(x, sr)
    frappes = {nom: pistes[nom] for nom, _ in solos_chemins}
    r = resoudre(stem, solos, frappes)
    res = {"provenance": {k: str(v) for k, v in vars(a).items()},
           "constantes": {"avant_s": AVANT_S, "apres_s": APRES_S, "bornes": BORNES, "bandes": h72.BANDES_DENSITE},
           "frappes": {n: len(v) for n, v in frappes.items()}, **r}
    a.sortie.parent.mkdir(parents=True, exist_ok=True)
    a.sortie.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    for n in frappes:
        print(f"« {n} » : {len(frappes[n])} frappes, bande {r['bandes'][n]} Hz, "  # type: ignore[index]
              f"avant {r['avant_reglage_sur_stem'][n]} du stem ; facteur {r['facteurs'][n]} "  # type: ignore[index]
              f"({r['facteurs_db'][n]:+.2f} dB) ; après {r['rendu_sur_stem_aux_frappes'][n]}")  # type: ignore[index]
    print(f"résidu relatif {r['residu_relatif']}")
    for ligne in r["dits"]:  # type: ignore[attr-defined]
        print("DIT :", ligne)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
