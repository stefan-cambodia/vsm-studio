"""H70 — le kick du TR-808 réglé sur SES frappes (docs/CDC-reload-indifferenciable.md § 31).

Sur l'instrument de H69 (`mesure_h69.py` : les frappes retenues, la moyenne synchrone, le début du kick moyen) :

- l'ENVELOPPE d'un kick moyen : module du signal analytique dans `BANDE_FI`, depuis son début, en dB sous son
  maximum, plancher à `PLANCHER_DB` ; l'écart entre deux enveloppes est la moyenne quadratique de leur différence
  sur `FENETRE_ENVELOPPE_S` ;
- la TENUE choisie : celle des rendus solo dont l'enveloppe est la plus proche de celle de l'original ;
- le NIVEAU : `√(E_original / E_rendu)`, énergies des deux kicks moyens (sous 1 kHz) sur `FENETRE_ENERGIE_S` depuis
  leur début.

Rien ici ne regarde le mélange : le réglage est fixé sur les frappes, AVANT la mesure du mélange.

    mesure_h70.py --projet <dossier> --piste-kick <nom> --piste-caisse <nom> --stem <drums.wav>
                  --sections 56-84,98-127,218-268 --solo 0.04=<wav> --solo 0.06=<wav> ... --sortie <choix.json>
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mesure_h69 as h69  # noqa: E402

PLANCHER_DB = -20.0
FENETRE_ENVELOPPE_S = (0.005, 0.130)
FENETRE_ENERGIE_S = 0.150


def kick_moyen(x: np.ndarray, sr: int, instants: Sequence[float]) -> np.ndarray:
    """Le kick moyen sous `PASSE_BAS_HZ`, sur [-AVANT_S, DUREE_S) — le chemin de H69."""
    y = h69.filtrer(x, sr, None, h69.PASSE_BAS_HZ)
    moyen, _, _ = h69.moyenne_synchrone(y, sr, instants)
    return moyen


def enveloppe_db(y: np.ndarray, sr: int) -> np.ndarray:
    """L'enveloppe depuis le début du kick, en dB sous son maximum, plancher à PLANCHER_DB."""
    from scipy.signal import hilbert

    t0 = h69.debut(y, sr)
    env = np.abs(hilbert(h69.filtrer(y, sr, *h69.BANDE_FI)))[t0:]
    return np.maximum(20.0 * np.log10(np.maximum(env, 1e-12) / env.max()), PLANCHER_DB)


def ecart_enveloppes(a: np.ndarray, b: np.ndarray, sr: int) -> float:
    ea, eb = enveloppe_db(a, sr), enveloppe_db(b, sr)
    i0, i1 = (int(round(t * sr)) for t in FENETRE_ENVELOPPE_S)
    if min(len(ea), len(eb)) < i1:
        raise ValueError("kick moyen plus court que la fenêtre d'enveloppe")
    return float(np.sqrt(np.mean((ea[i0:i1] - eb[i0:i1]) ** 2)))


def energie(y: np.ndarray, sr: int) -> float:
    t0 = h69.debut(y, sr)
    return float(np.sum(y[t0:t0 + int(round(FENETRE_ENERGIE_S * sr))] ** 2))


def choisir(original: np.ndarray, rendus: Dict[float, np.ndarray], sr: int) -> Dict[str, object]:
    """La tenue dont l'enveloppe est la plus proche, et le facteur de niveau à cette tenue."""
    ecarts = {tenue: ecart_enveloppes(original, y, sr) for tenue, y in rendus.items()}
    tenue = min(ecarts, key=lambda k: (ecarts[k], k))
    g = math.sqrt(energie(original, sr) / energie(rendus[tenue], sr))
    return {"ecarts_db": {f"{k:g}": round(v, 3) for k, v in sorted(ecarts.items())},
            "tenue_s": tenue, "facteur_niveau": round(g, 4), "facteur_niveau_db": round(20 * math.log10(g), 2)}


def main() -> int:
    from mesure_h65 import frappes_par_piste

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--projet", type=Path, required=True)
    ap.add_argument("--piste-kick", required=True)
    ap.add_argument("--piste-caisse", required=True)
    ap.add_argument("--stem", type=Path, required=True)
    ap.add_argument("--sections", required=True)
    ap.add_argument("--solo", action="append", required=True, help="TENUE=fichier.wav (rendu solo de la piste du kick)")
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args()
    solos: List[Tuple[float, Path]] = []
    for s in a.solo:
        tenue, chemin = s.split("=", 1)
        solos.append((float(tenue), Path(chemin)))
    for chemin in [a.projet / "project.json", a.stem] + [c for _, c in solos]:
        if not chemin.is_file() or chemin.stat().st_size == 0:
            print(f"REFUS : {chemin} absent ou vide")
            return 2
    sections = [(float(m.split("-")[0]), float(m.split("-")[1])) for m in a.sections.split(",")]
    p = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    pistes = frappes_par_piste(a.projet, p)
    instants, compte = h69.retenir(pistes[a.piste_kick], pistes[a.piste_caisse], sections)
    print("FRAPPES : " + ", ".join(f"{k} {v}" for k, v in compte.items()))
    xo, sr = h69.lire_mono(a.stem)
    original = kick_moyen(xo, sr, instants)
    rendus: Dict[float, np.ndarray] = {}
    descriptions: Dict[str, object] = {}
    for tenue, chemin in solos:
        xs, srs = h69.lire_mono(chemin)
        if srs != sr:
            print(f"REFUS : {chemin} à {srs} Hz, le stem à {sr} Hz")
            return 2
        rendus[tenue] = kick_moyen(xs, sr, instants)
        descriptions[f"{tenue:g}"] = h69.decrire(rendus[tenue], sr)
    res = {"provenance": {k: str(v) for k, v in vars(a).items()}, "frappes": compte,
           "constantes": {"plancher_db": PLANCHER_DB, "fenetre_enveloppe_s": FENETRE_ENVELOPPE_S,
                          "fenetre_energie_s": FENETRE_ENERGIE_S},
           "original": h69.decrire(original, sr), "rendus": descriptions, "choix": choisir(original, rendus, sr)}
    a.sortie.parent.mkdir(parents=True, exist_ok=True)
    a.sortie.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    o = res["original"]
    assert isinstance(o, dict)
    print(f"original : part > 150 Hz {o['part_au_dessus_db']:+.2f} dB ; f {o['frequence_hz']}")
    for k, d in descriptions.items():
        assert isinstance(d, dict)
        print(f"tenue {k} s : écart d'enveloppe {res['choix']['ecarts_db'][k]} dB ; "  # type: ignore[index]
              f"part > 150 Hz {d['part_au_dessus_db']:+.2f} dB ; f {d['frequence_hz']}")
    print(f"CHOIX : {res['choix']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
