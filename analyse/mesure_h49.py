#!/usr/bin/env python3
"""H49 — DÉCRIRE LE PAD DE « RELOAD » SUR L'ORIGINAL (docs/CDC-reload-indifferenciable.md § 11)

H47 a demandé « quelle machine sonne comme le pad » sans savoir ce qu'est le pad : un
tiers de sa puissance est hors de ses dix raies étroites. Cet outil décrit chaque raie
— sa forme, ses composantes, son mouvement — dans l'ORIGINAL et dans le stem, et dit
si le mouvement a la forme d'un désaccord entre oscillateurs (mêmes écarts en CENTS
d'une raie à l'autre), d'une modulation à cadence fixe (mêmes écarts en HERTZ), ou
d'une bosse continue.

    analyse/.venv/bin/python analyse/mesure_h49.py mesurer ORIGINAL.wav STEM.wav \\
        --oracle reconstruction/travail/reload-h47/mesure.json --sortie reconstruction/travail/reload-h49
    analyse/.venv/bin/python analyse/mesure_h49.py verdict reconstruction/travail/reload-h49/mesure.json

Il ne rend rien par le moteur. Rend 0 quand la mesure est complète, 2 sur un refus.
L'outil ne choisit pas l'issue : il publie, et `verdict` recalcule depuis le fichier.

UNE RAIE NE SE JUGE QUE SI ELLE SE VOIT : son rapport au fond (la densité entre les
raies) doit atteindre 10 dB dans l'original. Les autres sont NOMMÉES, jamais comptées.

LE FOND est la médiane des cases entre 6 et 6,9 Hz de la raie, des deux côtés ; la
médiane d'un périodogramme sous-estime sa moyenne d'un facteur ln 2 (loi du χ² à deux
degrés), corrigé ici. Sans cela, le fond retiré serait trop petit de 1,6 dB.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Dict, List, Sequence, Tuple

import numpy as np

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))

DEBUT_S, FIN_S = 16.0, 40.0
DEMI_COEUR_HZ = 2.7                  # le cœur d'une raie : ce que H47 sommait
DEMI_BANDE_HZ = 6.0                  # au-delà, la raie voisine (la plus proche est à 13,8 Hz)
FOND_HZ = (6.0, 6.9)
VUE_DB = 10.0                        # rapport au fond en deçà duquel une raie ne se juge pas
SEGMENT_S = 8.0                      # Welch des composantes : 0,125 Hz par case
PROEMINENCE_DB = 8.0
SOUS_LA_PLUS_FORTE_DB = 15.0
TEMPS_HZ = 138.0 / 60.0              # 2,30 Hz : le kick (§ 8)
PARTIELS_HZ = (600.0, 6000.0)
PARTIELS_SOUS_DB = 40.0


# ------------------------------------------------------------------ les spectres

def spectre_entier(x: np.ndarray, sr: int) -> Tuple[np.ndarray, np.ndarray]:
    """Le spectre de puissance de l'extrait ENTIER (fenêtre de Hann) : fréquences, puissance par case."""
    x = np.asarray(x, dtype=np.float64)
    w = np.hanning(len(x))
    X = np.fft.rfft(x * w)
    P = 2.0 * np.abs(X) ** 2 / (w.sum() ** 2)
    return np.fft.rfftfreq(len(x), 1.0 / sr), P


def centre_de_raie(f: np.ndarray, P: np.ndarray, hz: float, tolerance_hz: float = 1.5) -> float:
    """La fréquence du maximum à ± `tolerance_hz` de la raie de l'oracle."""
    k = np.flatnonzero(np.abs(f - hz) <= tolerance_hz)
    return float(f[k[np.argmax(P[k])]])


def forme_de_raie(f: np.ndarray, P: np.ndarray, hz: float) -> Dict[str, float]:
    """Cœur, jupe et fond d'une raie ; la part de jupe (fond retiré) et le rapport au fond."""
    ecart = np.abs(f - hz)
    coeur = ecart <= DEMI_COEUR_HZ
    jupe = (ecart > DEMI_COEUR_HZ) & (ecart <= DEMI_BANDE_HZ)
    fond = (ecart > FOND_HZ[0]) & (ecart <= FOND_HZ[1])
    densite = float(np.median(P[fond])) / math.log(2.0)
    p_coeur, p_jupe = float(P[coeur].sum()), float(P[jupe].sum())
    net_coeur = max(p_coeur - densite * int(coeur.sum()), 0.0)
    net_jupe = max(p_jupe - densite * int(jupe.sum()), 0.0)
    total = net_coeur + net_jupe
    return {
        "part_jupe": round(100.0 * net_jupe / total, 2) if total > 0 else float("nan"),
        "rapport_au_fond_db": round(10 * math.log10((p_coeur + p_jupe) / max(densite * int((coeur | jupe).sum()), 1e-30)), 2),
        "puissance": p_coeur + p_jupe,
    }


def composantes(x: np.ndarray, sr: int, hz: float) -> List[Dict[str, float]]:
    """Les pics à ± 6 Hz de la raie dans un Welch à segments de 8 s, du plus fort au plus faible."""
    from scipy.signal import find_peaks, welch

    n = int(round(SEGMENT_S * sr))
    if len(x) < n:
        raise ValueError(f"extrait trop court pour des segments de {SEGMENT_S:.0f} s")
    f, P = welch(np.asarray(x, dtype=np.float64), fs=sr, nperseg=n, noverlap=n // 2, scaling="spectrum")
    k = np.flatnonzero(np.abs(f - hz) <= DEMI_BANDE_HZ)
    fk, Pdb = f[k], 10 * np.log10(P[k] + 1e-30)
    indices, _ = find_peaks(Pdb, prominence=PROEMINENCE_DB, distance=2)
    if len(indices) == 0:
        return []
    plafond = float(max(Pdb[i] for i in indices))
    pas = float(f[1] - f[0])
    pics = []
    for i in indices:
        if Pdb[i] < plafond - SOUS_LA_PLUS_FORTE_DB:
            continue
        h = float(fk[i])
        if 0 < i < len(Pdb) - 1:
            a, b, c = Pdb[i - 1], Pdb[i], Pdb[i + 1]
            if (a - 2 * b + c) != 0:
                h += float(0.5 * (a - c) / (a - 2 * b + c) * pas)
        pics.append((float(Pdb[i]), h))
    pics.sort(reverse=True)
    tete = pics[0][1]
    return [{"hz": round(h, 3), "db": round(db - pics[0][0], 2), "ecart_hz": round(h - tete, 3),
             "ecart_cents": round(1200 * math.log2(h / tete), 2)} for db, h in pics]


def mouvement(x: np.ndarray, sr: int, hz: float) -> Dict[str, float]:
    """Le signal analytique de la bande `hz` ± 6 Hz : modulation d'amplitude et de fréquence."""
    x = np.asarray(x, dtype=np.float64)
    n = len(x)
    X = np.fft.fft(x)
    f = np.fft.fftfreq(n, 1.0 / sr)
    ecart = np.abs(f - hz)                       # fréquences POSITIVES seulement : un signal analytique
    masque = np.where(ecart <= DEMI_BANDE_HZ, 1.0, 0.0)
    bord = (ecart > DEMI_BANDE_HZ) & (ecart <= FOND_HZ[1])
    masque[bord] = 0.5 * (1 + np.cos(np.pi * (ecart[bord] - DEMI_BANDE_HZ) / (FOND_HZ[1] - DEMI_BANDE_HZ)))
    z = np.fft.ifft(X * masque)
    # RAMENÉ EN BANDE DE BASE avant d'être décimé : à 100 Hz, la phase d'une raie de
    # 186 Hz tourne de presque deux tours par échantillon, et la fréquence instantanée
    # lue sur le signal décimé serait un repliement (vu au premier essai de la suite de
    # tests : « nan cents » sur un sinus seul).
    z = z * np.exp(-2j * np.pi * hz * np.arange(n) / sr)
    pas = max(1, sr // 100)                      # la bande fait 14 Hz de large : 100 Hz suffisent
    z = z[sr:n - sr:pas]                         # une seconde de bord retirée de chaque côté
    taux = sr / pas
    a = np.abs(z)
    niveau = 20 * np.log10(np.maximum(a, 1e-12))
    # la cadence dominante de la modulation d'amplitude, entre 0,2 et 12 Hz
    ac = (a - a.mean()) * np.hanning(len(a))
    S = np.abs(np.fft.rfft(ac)) ** 2
    fs = np.fft.rfftfreq(len(ac), 1.0 / taux)
    bande = (fs >= 0.2) & (fs <= 12.0)
    cadence = float(fs[bande][np.argmax(S[bande])]) if S[bande].sum() > 0 else float("nan")
    autour = bande & (np.abs(fs - cadence) <= 0.1)
    part = float(S[autour].sum() / S[bande].sum()) if S[bande].sum() > 0 else float("nan")
    # la fréquence instantanée, pondérée par la puissance
    inst = hz + np.diff(np.unwrap(np.angle(z))) * taux / (2 * np.pi)
    poids = (a[1:] * a[:-1])
    moyenne = float(np.sum(inst * poids) / np.sum(poids))
    cents = 1200 * np.log2(np.maximum(inst, 1e-6) / moyenne)
    fm = float(np.sqrt(np.sum(poids * cents ** 2) / np.sum(poids)))
    return {
        "am_ecart_type_db": round(float(np.std(niveau)), 2),
        "am_etendue_db": round(float(np.percentile(niveau, 95) - np.percentile(niveau, 5)), 2),
        "am_cadence_hz": round(cadence, 3),
        "am_part_de_la_cadence": round(part, 3),
        "fm_ecart_type_cents": round(fm, 2),
    }


def partiels_tenus(x: np.ndarray, sr: int, reference_db: float) -> List[Dict[str, float]]:
    """Les pics entre 600 et 6 000 Hz, à moins de 40 dB de la raie la plus forte, qui tiennent
    leur fréquence sur les TROIS tiers de l'extrait (une frappe de batterie n'en tient aucune)."""
    from scipy.signal import find_peaks, welch

    def pics(y: np.ndarray) -> List[Tuple[float, float]]:
        # Quatre segments au moins : le spectre d'un seul segment de bruit est hérissé de
        # pics de 12 dB, et l'un d'eux tombe toujours à 1 Hz de n'importe quelle fréquence.
        segment = min(65536, len(y) // 4)
        f, P = welch(np.asarray(y, dtype=np.float64), fs=sr, nperseg=segment, noverlap=segment // 2, scaling="spectrum")
        k = (f >= PARTIELS_HZ[0]) & (f <= PARTIELS_HZ[1])
        Pdb = 10 * np.log10(P[k] + 1e-30)
        idx, _ = find_peaks(Pdb, prominence=12.0, distance=6)
        return [(float(f[k][i]), float(Pdb[i])) for i in idx]

    tiers = len(x) // 3
    par_tiers = [pics(x[i * tiers:(i + 1) * tiers]) for i in range(3)]
    sortie = []
    for hz, db in pics(x):
        if db < reference_db - PARTIELS_SOUS_DB:
            continue
        # « Tenir » : dans CHAQUE tiers, un pic à 1 Hz près ET à 10 dB près du niveau de
        # l'extrait entier — une frappe présente dans un seul tiers laisse, dans les deux
        # autres, au mieux un pic de bruit bien plus bas.
        if all(any(abs(h - hz) <= 1.0 and d >= db - 10.0 for h, d in liste) for liste in par_tiers):
            sortie.append({"hz": round(hz, 2), "db": round(db - reference_db, 2)})
    return sorted(sortie, key=lambda p: -p["db"])


def niveau_de_reference(x: np.ndarray, sr: int, hz: float) -> float:
    """Le niveau (dB) de la raie la plus forte dans le même Welch que `partiels_tenus`."""
    from scipy.signal import welch

    segment = min(65536, len(x) // 4)
    f, P = welch(np.asarray(x, dtype=np.float64), fs=sr, nperseg=segment, noverlap=segment // 2, scaling="spectrum")
    k = np.abs(f - hz) <= 1.5
    return float(10 * np.log10(P[k].max() + 1e-30))


# ------------------------------------------------------------------ la description

def decrire(x: np.ndarray, sr: int, raies: Sequence[float]) -> List[Dict[str, Any]]:
    """Forme, composantes et mouvement de chaque raie de l'oracle dans `x`."""
    f, P = spectre_entier(x, sr)
    sortie: List[Dict[str, Any]] = []
    for hz in raies:
        centre = centre_de_raie(f, P, hz)
        fiche: Dict[str, Any] = {"oracle_hz": round(float(hz), 2), "hz": round(centre, 3)}
        fiche.update(forme_de_raie(f, P, centre))
        fiche["vue"] = bool(fiche["rapport_au_fond_db"] >= VUE_DB)
        fiche["composantes"] = composantes(x, sr, centre)
        fiche.update(mouvement(x, sr, centre))
        sortie.append(fiche)
    plus_forte = max(r["puissance"] for r in sortie)
    for r in sortie:
        r["niveau_db"] = round(10 * math.log10(r.pop("puissance") / plus_forte), 2)
    return sortie


def forme_du_mouvement(raies: Sequence[Dict[str, Any]]) -> Dict[str, Any]:
    """Attendu 4 : l'écart de la composante secondaire la plus forte est-il le même en CENTS
    ou en HERTZ d'une raie à l'autre ? Moins de trois raies à deux composantes : non mesurable."""
    ecarts = [(abs(r["composantes"][1]["ecart_hz"]), abs(r["composantes"][1]["ecart_cents"]))
              for r in raies if len(r["composantes"]) >= 2]
    if len(ecarts) < 3:
        return {"forme": "non mesurable", "raies": len(ecarts)}
    hz = np.array([e[0] for e in ecarts])
    cents = np.array([e[1] for e in ecarts])
    d_hz = float(np.std(hz) / np.mean(hz)) if np.mean(hz) > 0 else float("inf")
    d_cents = float(np.std(cents) / np.mean(cents)) if np.mean(cents) > 0 else float("inf")
    if d_cents <= 0.20 and d_hz >= 2 * d_cents:
        forme = "désaccord"
    elif d_hz <= 0.20 and d_cents >= 2 * d_hz:
        forme = "cadence fixe"
    else:
        forme = "non conclu"
    return {"forme": forme, "raies": len(ecarts), "dispersion_cents": round(d_cents, 3), "dispersion_hz": round(d_hz, 3),
            "ecart_cents_moyen": round(float(np.mean(cents)), 2), "ecart_hz_moyen": round(float(np.mean(hz)), 3)}


def au_temps(cadence_hz: float) -> bool:
    return any(abs(cadence_hz - k * TEMPS_HZ) <= 0.1 for k in (1, 2))


# ------------------------------------------------------------------ le verdict

def verdict(mesure: Dict[str, Any]) -> List[Tuple[int, str, str]]:
    """Les attendus 2 à 6 du § 11, recalculés depuis `mesure.json` (l'attendu 1 est la suite de tests)."""
    original, stem = mesure["original"], mesure["stem"]
    vues = [i for i, r in enumerate(original) if r["vue"]]
    cachees = [f"{original[i]['hz']:.1f} Hz ({original[i]['rapport_au_fond_db']:.1f} dB)"
               for i in range(len(original)) if i not in vues]
    dit = f"{len(vues)} raie(s) vue(s) sur {len(original)}" + (f" ; non vues : {', '.join(cachees)}" if cachees else "")
    sortie: List[Tuple[int, str, str]] = []
    if not vues:
        return [(n, "NON MESURABLE", dit) for n in (2, 3, 4, 5, 6)]

    ecarts = [stem[i]["part_jupe"] - original[i]["part_jupe"] for i in vues]
    mediane = float(np.median(np.abs(ecarts)))
    v = "TENU" if mediane <= 5.0 else ("ÉCHEC" if mediane > 15.0 else "ENTRE LES DEUX")
    sortie.append((2, v, f"médiane des |écarts| de part de jupe (stem − original) : {mediane:.1f} points "
                         f"(écarts : {', '.join(f'{e:+.1f}' for e in ecarts)}) ; {dit}"))

    resolues = [i for i in vues if len(original[i]["composantes"]) >= 2]
    part = len(resolues) / len(vues)
    v = "TENU" if part >= 0.7 else ("ÉCHEC" if part <= 0.3 else "ENTRE LES DEUX")
    sortie.append((3, v, f"{len(resolues)} raie(s) sur {len(vues)} vues se résolvent en ≥ 2 composantes "
                         f"(composantes par raie : {', '.join(str(len(original[i]['composantes'])) for i in vues)})"))

    fm = forme_du_mouvement([original[i] for i in vues])
    if fm["forme"] == "non mesurable":
        sortie.append((4, "NON MESURABLE", f"{fm['raies']} raie(s) à deux composantes : un coefficient ne se lit pas"))
    else:
        v = {"désaccord": "TENU (désaccord)", "cadence fixe": "ÉCHEC (cadence fixe)", "non conclu": "NON CONCLU"}[fm["forme"]]
        sortie.append((4, v, f"sur {fm['raies']} raies : dispersion {100 * fm['dispersion_cents']:.0f} % en cents "
                             f"(moyenne {fm['ecart_cents_moyen']:.1f} cents), {100 * fm['dispersion_hz']:.0f} % en hertz "
                             f"(moyenne {fm['ecart_hz_moyen']:.2f} Hz)"))

    partiels = mesure["partiels_original"]
    if not partiels:
        sortie.append((5, "TENU", f"aucun partiel tenu entre 600 et 6 000 Hz à moins de {PARTIELS_SOUS_DB:.0f} dB de la raie la plus forte"))
    else:
        fort = partiels[0]
        v = "TENU" if fort["db"] <= -30.0 else ("ÉCHEC" if fort["db"] > -20.0 else "ENTRE LES DEUX")
        sortie.append((5, v, f"le plus fort partiel tenu : {fort['hz']:.1f} Hz à {fort['db']:+.1f} dB ({len(partiels)} en tout)"))

    pompees = [i for i in vues if au_temps(original[i]["am_cadence_hz"])]
    v = "TENU" if len(pompees) < 3 else ("ÉCHEC" if len(pompees) >= 7 else "ENTRE LES DEUX")
    sortie.append((6, v, f"{len(pompees)} raie(s) sur {len(vues)} ont leur cadence dominante au temps ({TEMPS_HZ:.2f} ou "
                         f"{2 * TEMPS_HZ:.2f} Hz) ; cadences : {', '.join(format(original[i]['am_cadence_hz'], '.2f') for i in vues)} Hz"))
    return sortie


def imprimer(mesure: Dict[str, Any]) -> None:
    for nom in ("original", "stem"):
        print(f"{nom.upper()} :")
        for r in mesure[nom]:
            comps = " ; ".join(f"{c['ecart_hz']:+.2f} Hz ({c['ecart_cents']:+.1f} c) {c['db']:+.1f} dB" for c in r["composantes"][1:]) or "—"
            print(f"  {r['hz']:8.2f} Hz  niveau {r['niveau_db']:6.1f} dB  fond {r['rapport_au_fond_db']:5.1f} dB{'' if r['vue'] else ' (NON VUE)'}  "
                  f"jupe {r['part_jupe']:5.1f} %  composantes {len(r['composantes'])} [{comps}]  "
                  f"AM σ {r['am_ecart_type_db']:.1f} dB, étendue {r['am_etendue_db']:.1f} dB, cadence {r['am_cadence_hz']:.2f} Hz "
                  f"({100 * r['am_part_de_la_cadence']:.0f} %)  FM σ {r['fm_ecart_type_cents']:.1f} c")
    print("PARTIELS TENUS de l'original (600-6 000 Hz) : "
          + (", ".join(f"{p['hz']:.1f} Hz {p['db']:+.1f} dB" for p in mesure["partiels_original"]) or "aucun"))
    print("VERDICT (recalculé depuis la mesure ; l'attendu 1 est la suite de tests) :")
    for numero, v, detail in verdict(mesure):
        print(f"  attendu {numero} : {v} — {detail}")


# ------------------------------------------------------------------ la course

def lire_extrait(chemin: Path, debut_s: float, fin_s: float) -> Tuple[np.ndarray, int]:
    import soundfile as sf

    info = sf.info(str(chemin))
    sr = int(info.samplerate)
    a, b = int(round(debut_s * sr)), int(round(fin_s * sr))
    if b > info.frames:
        raise ValueError(f"{chemin} : {info.frames / sr:.1f} s, l'extrait demande {fin_s:.1f} s")
    x, _ = sf.read(str(chemin), start=a, stop=b, dtype="float64", always_2d=True)
    return x.mean(axis=1), sr


def mesurer(a: argparse.Namespace) -> int:
    for p in (a.original, a.stem, a.oracle):
        if not p.is_file() or p.stat().st_size == 0:
            print(f"REFUS : {p} absent ou vide")
            return 2
    raies = [float(p["hz"]) for p in json.loads(a.oracle.read_text(encoding="utf-8"))["oracle"]["pics"]]
    if not raies:
        print("REFUS : l'oracle ne porte aucune raie")
        return 2
    try:
        original, sr = lire_extrait(a.original, a.debut, a.fin)
        stem, sr_stem = lire_extrait(a.stem, a.debut, a.fin)
    except ValueError as e:
        print(f"REFUS : {e}")
        return 2
    if sr != sr_stem:
        print(f"REFUS : l'original est à {sr} Hz et le stem à {sr_stem} Hz")
        return 2
    d_original = decrire(original, sr, raies)
    forte = max(d_original, key=lambda r: r["niveau_db"])
    mesure: Dict[str, Any] = {
        "provenance": {"original": str(a.original), "stem": str(a.stem), "oracle": str(a.oracle),
                       "debut_s": a.debut, "fin_s": a.fin, "sr": sr},
        "original": d_original,
        "stem": decrire(stem, sr, raies),
        "partiels_original": partiels_tenus(original, sr, niveau_de_reference(original, sr, forte["hz"])),
    }
    a.sortie.mkdir(parents=True, exist_ok=True)
    (a.sortie / "mesure.json").write_text(json.dumps(mesure, indent=1, ensure_ascii=False, sort_keys=True) + "\n",
                                          encoding="utf-8")
    print(f"EXTRAIT : {a.debut:.0f} à {a.fin:.0f} s, {len(raies)} raies de l'oracle ; ÉCRIT : {a.sortie / 'mesure.json'}")
    imprimer(mesure)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sous = ap.add_subparsers(dest="mode", required=True)
    m = sous.add_parser("mesurer")
    m.add_argument("original", type=Path)
    m.add_argument("stem", type=Path)
    m.add_argument("--oracle", type=Path, required=True)
    m.add_argument("--sortie", type=Path, required=True)
    m.add_argument("--debut", type=float, default=DEBUT_S)
    m.add_argument("--fin", type=float, default=FIN_S)
    v = sous.add_parser("verdict")
    v.add_argument("mesure", type=Path)
    a = ap.parse_args()
    if a.mode == "verdict":
        if not a.mesure.is_file() or a.mesure.stat().st_size == 0:
            print(f"REFUS : {a.mesure} absent ou vide")
            return 2
        imprimer(json.loads(a.mesure.read_text(encoding="utf-8")))
        return 0
    return mesurer(a)


if __name__ == "__main__":
    sys.exit(main())
