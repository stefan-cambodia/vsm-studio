"""H72 — la longueur du charleston réglée sur SES frappes (docs/CDC-reload-indifferenciable.md § 33).

Le charleston n'est pas, comme le kick (H69), une forme d'onde répétée : celui du TR-808 laisse tourner ses six
oscillateurs d'une frappe à l'autre, et un charleston d'échantillon est surtout du bruit. Moyenner les ONDES les
annulerait. On moyenne donc leur ÉNERGIE : l'enveloppe de puissance au-dessus de `BANDE_HZ`, lissée sur `LISSAGE_S`,
chaque frappe recalée à ± `JEU_S` sur l'enveloppe moyenne par corrélation (la transcription date à quelques ms près).

- la FORME d'une enveloppe moyenne : en dB sous son maximum depuis son début (premier échantillon à `SEUIL_DEBUT`
  de son maximum), plancher `PLANCHER_DB` ; l'écart entre deux formes est l'écart-type de leur différence sur
  `FENETRE_S` (un décalage constant — le niveau — n'y compte pas : le défaut du § 31.2) ;
- la DÉCROISSANCE choisie : celle des rendus solo dont la forme est la plus proche de l'original ;
- le NIVEAU : `√(E_original / E_rendu)`, énergies des enveloppes moyennes sur `FENETRE_ENERGIE_S` depuis leur début ;
- la DENSITÉ d'une batterie contre son stem : les centiles des niveaux trame à trame (STFT de 1 024 points à
  22 050 Hz, pas de 256) par bande, dans les sections données.

    mesure_h72.py choisir --projet <dossier> --stem <drums.wav> --sections 56-84,... --solo 0.05=<wav> ... --sortie <json>
    mesure_h72.py densite --stem <drums.wav> --rendu <batterie.wav> --sections 56-84,... [--json <fichier>]
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

BANDE_HZ = 4000.0
LISSAGE_S = 0.002
AVANT_S = 0.010
DUREE_S = 0.120
JEU_S = 0.020
SEUIL_DEBUT = 0.1
PLANCHER_DB = -30.0
FENETRE_S = (0.005, 0.100)
FENETRE_ENERGIE_S = 0.100
CAISSE_AVANT_S = 0.150
CAISSE_APRES_S = 0.100
BANDES_DENSITE = [(30, 150), (150, 500), (500, 2000), (2000, 4000), (4000, 8000), (8000, 11000)]
CENTILES = (10, 50, 90, 99)


def retenir(charlestons: Sequence[float], caisses: Sequence[float], sections: Sequence[Tuple[float, float]]
            ) -> Tuple[List[float], Dict[str, int]]:
    """Les frappes de charleston dans les sections, sans caisse claire de CAISSE_AVANT_S avant à CAISSE_APRES_S après."""
    c = np.asarray(sorted(caisses), dtype=float)
    compte = {"charlestons": len(charlestons), "hors_sections": 0, "caisse_proche": 0, "retenues": 0}
    gardes: List[float] = []
    for t in sorted(charlestons):
        if not any(a <= t - AVANT_S and t + DUREE_S <= b for a, b in sections):
            compte["hors_sections"] += 1
            continue
        if len(c) and np.any((c > t - CAISSE_AVANT_S) & (c < t + CAISSE_APRES_S)):
            compte["caisse_proche"] += 1
            continue
        gardes.append(float(t))
    compte["retenues"] = len(gardes)
    return gardes, compte


def puissance(x: np.ndarray, sr: int) -> np.ndarray:
    """L'enveloppe de puissance au-dessus de BANDE_HZ, lissée par une moyenne glissante de LISSAGE_S."""
    y = h69.filtrer(x, sr, BANDE_HZ, None)
    n = max(1, int(round(LISSAGE_S * sr)))
    return np.convolve(y * y, np.ones(n) / n, mode="same")


def enveloppe_moyenne(x: np.ndarray, sr: int, instants: Sequence[float], iterations: int = 3
                      ) -> Tuple[np.ndarray, np.ndarray]:
    """(enveloppe de puissance moyenne sur [-AVANT_S, DUREE_S), décalages en échantillons)."""
    p = puissance(x, sr)
    jeu, avant = int(round(JEU_S * sr)), int(round(AVANT_S * sr))
    n = int(round((AVANT_S + DUREE_S) * sr))
    w = []
    for t in instants:
        d = int(round(t * sr)) - avant - jeu
        if d >= 0 and d + n + 2 * jeu <= len(p):
            w.append(p[d:d + n + 2 * jeu])
    if not w:
        raise ValueError("aucune fenêtre entière dans le signal")
    fen = np.stack(w)
    decal = np.zeros(len(fen), dtype=int)
    ref = fen[:, jeu:jeu + n].mean(axis=0)
    m = min(n, int(round((AVANT_S + 0.050) * sr)))
    for _ in range(iterations):
        for i in range(len(fen)):
            scores = [float(np.dot(fen[i, jeu + L:jeu + L + m], ref[:m])) for L in range(-jeu, jeu + 1)]
            decal[i] = int(np.argmax(scores)) - jeu
        ref = np.stack([fen[i, jeu + decal[i]:jeu + decal[i] + n] for i in range(len(fen))]).mean(axis=0)
    return ref, decal


def debut(env: np.ndarray) -> int:
    return int(np.argmax(env >= env.max() * SEUIL_DEBUT))


def forme_db(env: np.ndarray) -> np.ndarray:
    t0 = debut(env)
    return np.maximum(10.0 * np.log10(np.maximum(env[t0:], 1e-30) / env.max()), PLANCHER_DB)


def ecart_forme(a: np.ndarray, b: np.ndarray, sr: int) -> float:
    fa, fb = forme_db(a), forme_db(b)
    i0, i1 = (int(round(t * sr)) for t in FENETRE_S)
    if min(len(fa), len(fb)) < i1:
        raise ValueError("enveloppe plus courte que la fenêtre de forme")
    return float(np.std(fa[i0:i1] - fb[i0:i1]))


def energie(env: np.ndarray, sr: int) -> float:
    t0 = debut(env)
    return float(np.sum(env[t0:t0 + int(round(FENETRE_ENERGIE_S * sr))]))


def constante_db(env: np.ndarray, sr: int) -> float:
    """La pente de la forme sur 10-60 ms, rendue en constante de temps d'AMPLITUDE (s) — un relevé."""
    f = forme_db(env)
    i0, i1 = int(0.010 * sr), int(0.060 * sr)
    pente = np.polyfit(np.arange(i0, i1) / sr, f[i0:i1], 1)[0]   # dB de puissance par seconde
    return float(-20.0 / (math.log(10) * pente)) if pente < 0 else float("inf")


def choisir(original: np.ndarray, rendus: Dict[float, np.ndarray], sr: int) -> Dict[str, object]:
    ecarts = {k: ecart_forme(original, v, sr) for k, v in rendus.items()}
    choix = min(ecarts, key=lambda k: (ecarts[k], k))
    g = math.sqrt(energie(original, sr) / energie(rendus[choix], sr))
    return {"ecarts_forme_db": {f"{k:g}": round(v, 3) for k, v in sorted(ecarts.items())}, "decroissance_s": choix,
            "facteur_niveau": round(g, 4), "facteur_niveau_db": round(20 * math.log10(g), 2)}


def densite(stem: np.ndarray, rendu: np.ndarray, sr: int, sections: Sequence[Tuple[float, float]]
            ) -> Dict[str, Dict[str, object]]:
    import librosa

    n = min(len(stem), len(rendu))
    spectres = []
    for x in (stem[:n], rendu[:n]):
        y = librosa.resample(x, orig_sr=sr, target_sr=22050)
        spectres.append(np.abs(librosa.stft(y, n_fft=1024, hop_length=256)) ** 2)
    f = librosa.fft_frequencies(sr=22050, n_fft=1024)
    k = min(s.shape[1] for s in spectres)
    t = np.arange(k) * 256 / 22050
    sel = np.zeros(k, dtype=bool)
    for a, b in sections:
        sel |= (t >= a) & (t < b)
    res: Dict[str, Dict[str, object]] = {}
    for g0, g1 in BANDES_DENSITE:
        m = (f >= g0) & (f < g1)
        es, er = (10 * np.log10(s[m][:, :k][:, sel].sum(axis=0) + 1e-12) for s in spectres)
        cs, cr = np.percentile(es, CENTILES), np.percentile(er, CENTILES)
        res[f"{g0}-{g1}"] = {
            "stem": [round(float(v), 1) for v in cs], "rendu": [round(float(v), 1) for v in cr],
            "ecart": [round(float(b - a), 1) for a, b in zip(cs, cr, strict=True)],
            "energie_rendu_moins_stem_db": round(float(10 * np.log10((10 ** (er / 10)).sum() / (10 ** (es / 10)).sum())), 1)}
    return res


def sections_de(texte: str) -> List[Tuple[float, float]]:
    return [(float(m.split("-")[0]), float(m.split("-")[1])) for m in texte.split(",")]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sous = ap.add_subparsers(dest="verbe", required=True)
    c = sous.add_parser("choisir")
    c.add_argument("--projet", type=Path, required=True)
    c.add_argument("--piste-charleston", default="Batterie - hihat")
    c.add_argument("--piste-caisse", action="append", default=None,
                   help="pistes dont les frappes écartent un charleston voisin (répétable ; « Batterie - snare » par défaut)")
    c.add_argument("--stem", type=Path, required=True)
    c.add_argument("--sections", required=True)
    c.add_argument("--solo", action="append", required=True, help="DÉCROISSANCE=fichier.wav")
    c.add_argument("--sortie", type=Path, required=True)
    d = sous.add_parser("densite")
    d.add_argument("--stem", type=Path, required=True)
    d.add_argument("--rendu", type=Path, required=True)
    d.add_argument("--sections", required=True)
    d.add_argument("--json", type=Path, default=None)
    a = ap.parse_args()
    if a.verbe == "densite":
        for chemin in (a.stem, a.rendu):
            if not chemin.is_file() or chemin.stat().st_size == 0:
                print(f"REFUS : {chemin} absent ou vide")
                return 2
        xs, sr = h69.lire_mono(a.stem)
        xr, srr = h69.lire_mono(a.rendu)
        if sr != srr:
            print(f"REFUS : {sr} Hz contre {srr} Hz")
            return 2
        r = densite(xs, xr, sr, sections_de(a.sections))
        for bande, v in r.items():
            print(f"{bande:>10} Hz : énergie {v['energie_rendu_moins_stem_db']:+.1f} dB ; centiles {CENTILES} "
                  f"stem {v['stem']} rendu {v['rendu']} écart {v['ecart']}")
        if a.json:
            a.json.write_text(json.dumps(r, indent=2, ensure_ascii=False), encoding="utf-8")
        return 0

    from mesure_h65 import frappes_par_piste

    solos: List[Tuple[float, Path]] = [(float(s.split("=", 1)[0]), Path(s.split("=", 1)[1])) for s in a.solo]
    for chemin in [a.projet / "project.json", a.stem] + [p for _, p in solos]:
        if not chemin.is_file() or chemin.stat().st_size == 0:
            print(f"REFUS : {chemin} absent ou vide")
            return 2
    p = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    pistes = frappes_par_piste(a.projet, p)
    caisses = [t for nom in (a.piste_caisse or ["Batterie - snare"]) for t in pistes[nom]]
    instants, compte = retenir(pistes[a.piste_charleston], caisses, sections_de(a.sections))
    print("FRAPPES : " + ", ".join(f"{k} {v}" for k, v in compte.items()))
    xo, sr = h69.lire_mono(a.stem)
    original, dec_o = enveloppe_moyenne(xo, sr, instants)
    rendus: Dict[float, np.ndarray] = {}
    for decr, chemin in solos:
        xs, srs = h69.lire_mono(chemin)
        if srs != sr:
            print(f"REFUS : {chemin} à {srs} Hz")
            return 2
        rendus[decr], _ = enveloppe_moyenne(xs, sr, instants)
    choix = choisir(original, rendus, sr)
    res = {"provenance": {k: str(v) for k, v in vars(a).items()}, "frappes": compte,
           "constantes": {"bande_hz": BANDE_HZ, "lissage_s": LISSAGE_S, "fenetre_s": FENETRE_S,
                          "plancher_db": PLANCHER_DB, "fenetre_energie_s": FENETRE_ENERGIE_S},
           "decalages_original_ms": {"median": round(float(np.median(dec_o)) / sr * 1000, 2),
                                     "en_butee": int(np.sum(np.abs(dec_o) >= int(round(JEU_S * sr))))},
           "constante_original_s": round(constante_db(original, sr), 4),
           "constantes_rendus_s": {f"{k:g}": round(constante_db(v, sr), 4) for k, v in sorted(rendus.items())},
           "choix": choix}
    a.sortie.parent.mkdir(parents=True, exist_ok=True)
    a.sortie.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"original : constante relevée {res['constante_original_s']} s ; décalages {res['decalages_original_ms']}")
    print(f"rendus : constantes relevées {res['constantes_rendus_s']}")
    print(f"CHOIX : {choix}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
