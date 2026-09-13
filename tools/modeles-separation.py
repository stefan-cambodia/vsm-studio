#!/usr/bin/env python3
"""D273 : quel modèle de séparation garde l'AIGU de la basse ?

    analyse/.venv/bin/python tools/modeles-separation.py /home/stefan/vsm-d273

POURQUOI (D269 → D272). La chaîne sépare avec `htdemucs_6s`, et le stem de basse
qu'elle obtient a perdu 94 % de l'énergie que la partie jouée porte au-dessus de
300 Hz — exactement les partielles qui permettent de trancher une octave. Quatre
remèdes en aval ont échoué (D257, D258, D270, D272) : l'information n'y est plus.
Reste à savoir si c'est CE modèle qui la retire, ou la famille entière.

CE QUI EST MESURÉ, pour le stem de basse de chaque modèle, contre la partie de
basse VRAIE du corpus (connue exactement) :

  * la part de l'énergie au-dessus de 300 Hz — 25,5 % dans la partie jouée ;
  * la corrélation de forme d'onde, bande aiguë et bande grave (le contrôle : la
    bande grave doit être bien corrélée, sinon la mesure ne mesure rien) ;
  * le SDR, avec le facteur d'échelle optimal — deux signaux au même contenu mais
    à des niveaux différents ne doivent pas être comptés différents.

Le témoin est le stem que la chaîne a RÉELLEMENT écrit (`r1f-13sep`), c'est-à-dire
`htdemucs_6s` dans les conditions de la course, et non une reséparation.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

RACINE = Path(__file__).resolve().parent.parent
LOT = RACINE / "reconstruction/travail/r1f-13sep"
SRC = RACINE / "reconstruction/travail/s1-sec"
COUPURE = 300.0


def bande(x: np.ndarray, sr: float, bas: float | None = None, haut: float | None = None) -> np.ndarray:
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    m = np.ones_like(f, dtype=bool)
    if bas is not None:
        m &= f >= bas
    if haut is not None:
        m &= f < haut
    return np.fft.irfft(X * m, n=len(x))


def part_aigue(x: np.ndarray, sr: float) -> float:
    h = bande(x, sr, bas=COUPURE)
    total = float(np.sum(x * x))
    return float(np.sum(h * h)) / total if total > 0 else 0.0


def correlation(a: np.ndarray, b: np.ndarray) -> float:
    a = a - a.mean()
    b = b - b.mean()
    d = float(np.sqrt(float((a * a).sum()) * float((b * b).sum())))
    return float((a * b).sum() / d) if d > 0 else 0.0


def sdr(vrai: np.ndarray, estime: np.ndarray) -> float:
    """SDR avec le facteur d'échelle OPTIMAL : le niveau n'est pas une erreur."""
    d = float((estime * estime).sum())
    if d <= 0:
        return float("-inf")
    alpha = float((vrai * estime).sum()) / d
    bruit = vrai - alpha * estime
    p_bruit = float((bruit * bruit).sum())
    p_signal = float((vrai * vrai).sum())
    if p_bruit <= 0 or p_signal <= 0:
        return float("inf")
    return 10.0 * np.log10(p_signal / p_bruit)


def basse_vraie(morceau: str) -> tuple[np.ndarray, float] | None:
    f = SRC / morceau / "verite.json"
    if not f.is_file():
        return None
    v = json.loads(f.read_text(encoding="utf-8"))
    partie = next((p for p in v.get("parties", []) if p.get("role") == "basse"), None)
    if partie is None:
        return None
    x, sr = sf.read(str(SRC / morceau / partie["fichier"]), always_2d=True)
    return x.mean(axis=1), float(sr)


def stems_des_modeles(racine: Path, morceau: str) -> dict[str, Path]:
    """Le stem de basse de chaque modèle : le témoin du lot, puis les reséparations."""
    trouves: dict[str, Path] = {}
    temoin = LOT / morceau / "stems-separes" / "stems" / "bass.wav"
    if temoin.is_file():
        trouves["htdemucs_6s (témoin)"] = temoin
    for modele in sorted(p.name for p in racine.iterdir() if p.is_dir()):
        for f in (racine / modele / morceau).rglob("bass.wav"):
            trouves[modele] = f
            break
    return trouves


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 2
    racine = Path(argv[1])
    if not racine.is_dir():
        print(f"REFUS : {racine} n'existe pas — aucune reséparation à comparer")
        return 2

    par_modele: dict[str, list[tuple[float, float, float, float]]] = {}
    for dossier in sorted(SRC.glob("morceau-*")):
        vrai = basse_vraie(dossier.name)
        if vrai is None:
            continue
        v, sr = vrai
        vh, vb = bande(v, sr, bas=COUPURE), bande(v, sr, haut=COUPURE)
        for modele, chemin in stems_des_modeles(racine, dossier.name).items():
            x, sr2 = sf.read(str(chemin), always_2d=True)
            x = x.mean(axis=1)
            n = min(len(x), len(v))
            par_modele.setdefault(modele, []).append((
                part_aigue(x[:n], float(sr2)),
                correlation(vh[:n], bande(x[:n], float(sr2), bas=COUPURE)),
                correlation(vb[:n], bande(x[:n], float(sr2), haut=COUPURE)),
                sdr(v[:n], x[:n]),
            ))

    if not par_modele:
        print("aucun stem comparable")
        return 1
    n_ref = len(next(iter(par_modele.values())))
    print(f"stem de basse contre la partie de basse VRAIE, coupure {COUPURE:.0f} Hz")
    print(f"{'modèle':24s} {'morceaux':>9} {'aigu > 300 Hz':>14} {'corr. aigu':>11} "
          f"{'corr. grave':>12} {'SDR':>8}")
    print(f"{'(la partie jouée)':24s} {n_ref:9d} {25.5:13.1f}% {'—':>11} {'—':>12} {'—':>8}")
    for modele, lignes in par_modele.items():
        a = np.median([x[0] for x in lignes])
        ch = np.median([x[1] for x in lignes])
        cb = np.median([x[2] for x in lignes])
        s = np.median([x[3] for x in lignes])
        print(f"{modele:24s} {len(lignes):9d} {100 * a:13.1f}% {ch:11.3f} {cb:12.3f} {s:7.2f}dB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
