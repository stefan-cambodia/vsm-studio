"""H69 — le kick de l'original estimé par MOYENNE SYNCHRONE de ses frappes (docs/CDC-reload-indifferenciable.md § 30).

Un kick de production est, le plus souvent, le même échantillon à chaque frappe : moyennées après alignement, ses
frappes dans le stem « drums » rendent sa forme d'onde, et ce qui ne frappe pas avec lui s'y efface. Les frappes
retenues : la piste du kick, dans les sections données, sans caisse claire à moins de `GARDE_CAISSE_S`, sans autre
kick dans les `ISOLEMENT_S` qui suivent NI qui précèdent (la traîne d'un kick précédent, grave, baisserait la part
au-dessus de 150 Hz de l'original — un biais en faveur de l'hypothèse ; condition ajoutée avant toute mesure de
l'original, § 30.1). Alignées sur une référence par corrélation à ± `JEU_S`, filtrées sous `PASSE_BAS_HZ`.

Du kick moyen : sa fréquence instantanée (périodes entre passages par zéro, `BANDE_FI`), sa part d'énergie au-dessus de
`COUPURE_HZ` sur ses `DUREE_S` premières secondes, l'ajustement du modèle du TR-808 (`f(t) = f1 (1 + r e^(-t/tau))`,
amplitude en e^(-t/tau_a)), et la corrélation des moyennes des frappes paires et impaires. Le même instrument,
appliqué au rendu solo de la piste du kick, sert de contrôle, avec une réplique du code de `KickVoice` passée par le
même chemin.

    mesure_h69.py --projet <dossier> --piste-kick <nom> --piste-caisse <nom> --stem <drums.wav> --solo <kick.wav>
                  --accord 60 --decroissance 0.15 --sections 56-84,98-127,218-268 --sortie <fichier.json>
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

import numpy as np

GARDE_CAISSE_S = 0.150
ISOLEMENT_S = 0.300
JEU_S = 0.020
AVANT_S = 0.010
DUREE_S = 0.300
PASSE_BAS_HZ = 1000.0
COUPURE_HZ = 150.0
BANDE_FI = (30.0, 600.0)
INSTANTS_MS = (10, 25, 50, 100, 200)
SEUIL_DEBUT_DB = -20.0

# KickVoice (audio/plugins/tr808/TR808Synth.h) : départ à (1 + 5) fois l'accord, constante de 45 ms, tanh(sin × 1,8).
TR808_PROFONDEUR = 5.0
TR808_CONSTANTE_S = 0.045
TR808_SATURATION = 1.8


def replique_tr808(accord: float, decroissance: float, sr: int, duree: float = 0.6,
                   profondeur: float = TR808_PROFONDEUR, constante: float = TR808_CONSTANTE_S,
                   saturation: float = TR808_SATURATION) -> np.ndarray:
    """Une frappe de `KickVoice`, échantillon par échantillon comme le code (enveloppes multipliées par leur
    coefficient, phase cumulée)."""
    n = int(duree * sr)
    i = np.arange(n)
    env_hauteur = np.exp(-i / (constante * sr))
    env_amp = np.exp(-i / (decroissance * sr))
    f = accord * (1.0 + env_hauteur * profondeur)
    phase = np.cumsum(2.0 * np.pi * f / sr)
    return np.tanh(np.sin(phase) * saturation) * env_amp


def filtrer(x: np.ndarray, sr: int, bas: float | None, haut: float | None) -> np.ndarray:
    from scipy.signal import butter, sosfiltfilt

    if bas and haut:
        sos = butter(4, [bas, haut], "bandpass", fs=sr, output="sos")
    elif haut:
        sos = butter(4, haut, "lowpass", fs=sr, output="sos")
    else:
        sos = butter(4, bas, "highpass", fs=sr, output="sos")
    return sosfiltfilt(sos, x)


def retenir(kicks: Sequence[float], caisses: Sequence[float], sections: Sequence[Tuple[float, float]]
            ) -> Tuple[List[float], Dict[str, int]]:
    """Les frappes de kick retenues, et le compte de chaque exclusion (dit, pas tu)."""
    k = np.asarray(sorted(kicks), dtype=float)
    c = np.asarray(sorted(caisses), dtype=float)
    compte = {"kicks": int(len(k)), "hors_sections": 0, "caisse_proche": 0, "kick_voisin": 0, "retenues": 0}
    retenues: List[float] = []
    for j, t in enumerate(k):
        if not any(a <= t and t + DUREE_S <= b for a, b in sections):
            compte["hors_sections"] += 1
            continue
        if len(c) and float(np.min(np.abs(c - t))) < GARDE_CAISSE_S:
            compte["caisse_proche"] += 1
            continue
        avant = t - k[j - 1] if j > 0 else np.inf
        apres = k[j + 1] - t if j + 1 < len(k) else np.inf
        if avant < ISOLEMENT_S or apres < ISOLEMENT_S:
            compte["kick_voisin"] += 1
            continue
        retenues.append(float(t))
    compte["retenues"] = len(retenues)
    return retenues, compte


def moyenne_synchrone(x: np.ndarray, sr: int, instants: Sequence[float], iterations: int = 3
                      ) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """(kick moyen sur [-AVANT_S, DUREE_S), fenêtres alignées, décalages en échantillons).

    Chaque fenêtre est recalée à ± JEU_S sur la moyenne courante par corrélation, sur ses 100 premières ms."""
    jeu = int(round(JEU_S * sr))
    avant = int(round(AVANT_S * sr))
    n = int(round((AVANT_S + DUREE_S) * sr))
    larges = []
    for t in instants:
        d = int(round(t * sr)) - avant - jeu
        if d < 0 or d + n + 2 * jeu > len(x):
            continue
        larges.append(x[d:d + n + 2 * jeu])
    if not larges:
        raise ValueError("aucune fenêtre entière dans le signal")
    w = np.stack(larges)
    decal = np.zeros(len(w), dtype=int)
    ref = w[:, jeu:jeu + n].mean(axis=0)
    m = min(n, int(round((AVANT_S + 0.100) * sr)))
    for _ in range(iterations):
        for i in range(len(w)):
            meilleurs = [float(np.dot(w[i, jeu + L:jeu + L + m], ref[:m])) for L in range(-jeu, jeu + 1)]
            decal[i] = int(np.argmax(meilleurs)) - jeu
        alignees = np.stack([w[i, jeu + decal[i]:jeu + decal[i] + n] for i in range(len(w))])
        ref = alignees.mean(axis=0)
    return ref, alignees, decal


def debut(y: np.ndarray, sr: int) -> int:
    """Le premier échantillon où le signal REDRESSÉ passe SEUIL_DEBUT_DB sous son maximum.

    Pas l'enveloppe de Hilbert : son noyau (1/πt) l'étale AVANT l'attaque, et le test sur un kick connu l'a lue
    5 ms trop tôt (183 Hz lus à « 10 ms » pour 155)."""
    a = np.abs(y)
    return int(np.argmax(a >= a.max() * 10 ** (SEUIL_DEBUT_DB / 20)))


def frequence_instantanee(y: np.ndarray, sr: int) -> Tuple[np.ndarray, np.ndarray]:
    """(instants en échantillons, fréquences en Hz) : une PÉRIODE ENTIÈRE entre deux passages par zéro de même sens
    (montants et descendants), interpolés, dans BANDE_FI, datée à son milieu ; seulement là où le signal est à moins
    de 40 dB de son maximum.

    Pas la phase du signal analytique : sur un sinus SATURÉ (le tanh du TR-808), elle tourne inégalement dans la
    période, et un lissage plus court qu'une période a lu 60,3 Hz pour 68,5 au test. La saturation garde le signe,
    donc les passages par zéro du fondamental."""
    z = filtrer(y, sr, *BANDE_FI)
    seuil = np.abs(z).max() * 1e-2
    instants: List[float] = []
    frequences: List[float] = []
    for signe in (1, -1):
        s = signe * z
        i = np.nonzero((s[:-1] < 0) & (s[1:] >= 0))[0]
        x = i + s[i] / (s[i] - s[i + 1])          # passage interpolé
        for a, b in zip(x[:-1], x[1:], strict=True):
            if np.abs(z[int(a):int(b) + 1]).max() >= seuil:
                instants.append((a + b) / 2)
                frequences.append(sr / (b - a))
    ordre = np.argsort(instants)
    return np.asarray(instants)[ordre], np.asarray(frequences)[ordre]


def part_au_dessus(y: np.ndarray, sr: int, t0: int) -> float:
    n = int(round(DUREE_S * sr))
    bas = filtrer(y, sr, None, COUPURE_HZ)[t0:t0 + n]
    haut = filtrer(y, sr, COUPURE_HZ, None)[t0:t0 + n]
    eb, eh = float(np.sum(bas ** 2)), float(np.sum(haut ** 2))
    return eh / (eb + eh)


def ajuster(y: np.ndarray, sr: int, t0: int) -> Dict[str, float]:
    """f(t) = f1 (1 + r e^(-t/tau)) sur 5-200 ms, en log ; l'amplitude en e^(-t/tau_a) sur 20-250 ms."""
    from scipy.optimize import curve_fit
    from scipy.signal import hilbert

    ti, fi = frequence_instantanee(y, sr)
    tf = (ti - t0) / sr
    sel = (tf >= 0.005) & (tf <= 0.200)

    def modele(tt: np.ndarray, f1: float, r: float, tau: float) -> np.ndarray:
        return np.log(f1 * (1.0 + r * np.exp(-tt / tau)))

    (f1, r, tau), _ = curve_fit(modele, tf[sel], np.log(fi[sel]), p0=(60.0, 3.0, 0.03),
                                bounds=([10.0, 0.0, 0.001], [400.0, 50.0, 1.0]), maxfev=20000)
    t = (np.arange(len(y)) - t0) / sr
    env = np.abs(hilbert(filtrer(y, sr, *BANDE_FI)))
    sa = (t >= 0.020) & (t <= 0.250) & (env > 0)
    pente = np.polyfit(t[sa], np.log(env[sa]), 1)[0]
    return {"f1_hz": round(float(f1), 2), "r": round(float(r), 3), "tau_s": round(float(tau), 4),
            "tau_a_s": round(float(-1.0 / pente), 4) if pente < 0 else float("inf"),
            "periodes_ajustees": int(sel.sum()),
            "residu_rms_log": round(float(np.sqrt(np.mean((modele(tf[sel], f1, r, tau) - np.log(fi[sel])) ** 2))), 4)}


def decrire(y: np.ndarray, sr: int) -> Dict[str, object]:
    """Tout ce que l'hypothèse lit d'un kick moyen."""
    t0 = debut(y, sr)
    ti, fi = frequence_instantanee(y, sr)
    lue = {}
    for ms in INSTANTS_MS:
        i = t0 + ms / 1000 * sr
        lue[str(ms)] = round(float(np.interp(i, ti, fi)), 1) if len(ti) and ti[0] <= i <= ti[-1] else None
    # L'instant où la fréquence passe sous COUPURE_HZ se lit sur le MODÈLE ajusté : la fréquence instantanée brute
    # n'a pas de sens là où l'amplitude est faible, et le test l'a vue « sous 150 Hz » à 2,5 ms, dans le bruit.
    aj = ajuster(y, sr, t0)
    f1, r, tau = aj["f1_hz"], aj["r"], aj["tau_s"]
    if f1 >= COUPURE_HZ:
        sous_150 = None
    elif f1 * (1 + r) <= COUPURE_HZ:
        sous_150 = 0.0
    else:
        sous_150 = round(tau * float(np.log(r * f1 / (COUPURE_HZ - f1))) * 1000, 1)
    part = part_au_dessus(y, sr, t0)
    return {"debut_ms": round(t0 / sr * 1000 - AVANT_S * 1000, 2), "frequence_hz": lue,
            "sous_150_hz_ms": sous_150, "part_au_dessus": round(part, 4),
            "part_au_dessus_db": round(10 * float(np.log10(part)), 2), "ajustement": aj}


def correlation(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.dot(a - a.mean(), b - b.mean()) / (np.linalg.norm(a - a.mean()) * np.linalg.norm(b - b.mean())))


def mesurer_un(x: np.ndarray, sr: int, instants: Sequence[float]) -> Dict[str, object]:
    y = filtrer(x, sr, None, PASSE_BAS_HZ)
    moyen, alignees, decal = moyenne_synchrone(y, sr, instants)
    paires, impaires = alignees[0::2].mean(axis=0), alignees[1::2].mean(axis=0)
    r = decrire(moyen, sr)
    r["frappes_moyennees"] = int(len(alignees))
    r["correlation_paires_impaires"] = round(correlation(paires, impaires), 4)
    r["decalages_ms"] = {"median": round(float(np.median(decal)) / sr * 1000, 2),
                         "p10": round(float(np.percentile(decal, 10)) / sr * 1000, 2),
                         "p90": round(float(np.percentile(decal, 90)) / sr * 1000, 2),
                         "en_butee": int(np.sum(np.abs(decal) >= int(round(JEU_S * sr))))}
    return r


def lire_mono(chemin: Path) -> Tuple[np.ndarray, int]:
    import soundfile as sf

    x, sr = sf.read(str(chemin), dtype="float64", always_2d=True)
    return x.mean(axis=1), int(sr)


def main() -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from mesure_h65 import frappes_par_piste

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--projet", type=Path, required=True)
    ap.add_argument("--piste-kick", required=True)
    ap.add_argument("--piste-caisse", required=True)
    ap.add_argument("--stem", type=Path, required=True)
    ap.add_argument("--solo", type=Path, required=True)
    ap.add_argument("--accord", type=float, required=True, help="l'accord du kick du rendu solo (Hz)")
    ap.add_argument("--decroissance", type=float, required=True, help="sa décroissance (s)")
    ap.add_argument("--sections", required=True)
    ap.add_argument("--sortie", type=Path, required=True)
    a = ap.parse_args()
    for chemin in (a.projet / "project.json", a.stem, a.solo):
        if not chemin.is_file() or chemin.stat().st_size == 0:
            print(f"REFUS : {chemin} absent ou vide")
            return 2
    sections = [(float(m.split("-")[0]), float(m.split("-")[1])) for m in a.sections.split(",")]
    p = json.loads((a.projet / "project.json").read_text(encoding="utf-8"))
    pistes = frappes_par_piste(a.projet, p)
    for nom in (a.piste_kick, a.piste_caisse):
        if nom not in pistes:
            print(f"REFUS : piste « {nom} » absente du MIDI ({', '.join(pistes)})")
            return 2
    instants, compte = retenir(pistes[a.piste_kick], pistes[a.piste_caisse], sections)
    print("FRAPPES : " + ", ".join(f"{k} {v}" for k, v in compte.items()))
    res: Dict[str, object] = {"provenance": {k: str(v) for k, v in vars(a).items()},
                              "constantes": {"garde_caisse_s": GARDE_CAISSE_S, "isolement_s": ISOLEMENT_S,
                                             "jeu_s": JEU_S, "passe_bas_hz": PASSE_BAS_HZ, "coupure_hz": COUPURE_HZ,
                                             "bande_fi": BANDE_FI},
                              "frappes": compte}
    xs, sr = lire_mono(a.solo)
    res["solo"] = mesurer_un(xs, sr, instants)
    attendu = replique_tr808(a.accord, a.decroissance, sr)
    rep = np.concatenate([np.zeros(int(round(AVANT_S * sr))), attendu])[:int(round((AVANT_S + DUREE_S) * sr))]
    res["replique"] = decrire(filtrer(rep, sr, None, PASSE_BAS_HZ), sr)
    res["formule_hz"] = {str(ms): round(a.accord * (1 + TR808_PROFONDEUR * np.exp(-ms / 1000 / TR808_CONSTANTE_S)), 1)
                         for ms in INSTANTS_MS}
    xo, sro = lire_mono(a.stem)
    if sro != sr:
        print(f"REFUS : le stem est à {sro} Hz, le solo à {sr} Hz")
        return 2
    res["original"] = mesurer_un(xo, sr, instants)
    a.sortie.parent.mkdir(parents=True, exist_ok=True)
    a.sortie.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding="utf-8")
    for nom in ("formule_hz",):
        print(f"{nom:10s} : {res[nom]}")
    for nom in ("solo", "replique", "original"):
        r = res[nom]
        assert isinstance(r, dict)
        print(f"{nom:10s} : f(10,25,50,100,200 ms) {r['frequence_hz']} ; sous 150 Hz à {r['sous_150_hz_ms']} ms ; "
              f"part > 150 Hz {r['part_au_dessus']:.3f} ({r['part_au_dessus_db']:+.2f} dB) ; ajustement {r['ajustement']}"
              + (f" ; paires/impaires {r['correlation_paires_impaires']} ; décalages {r['decalages_ms']}"
                 if "correlation_paires_impaires" in r else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
