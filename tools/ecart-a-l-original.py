#!/usr/bin/env python3
"""ÉCART À L'ORIGINAL : ce qu'une oreille entendrait entre un morceau et sa reconstruction.

POURQUOI CET OUTIL (30/09/2026). L'utilisateur a demandé que la reconstruction de
« Reload » soit INDIFFÉRENCIABLE de l'original. La chaîne publie une distance
globale (`rapport.json`), un nombre sans unité qui ne dit pas CE qui s'entend. Une
oreille, elle, entend d'abord :

  1. le DIAPASON — un morceau accordé 13 cents trop haut, rejoué à 440 Hz, bat
     contre l'original sur chaque note tenue ;
  2. l'ÉQUILIBRE SPECTRAL, tranche par tranche — un écart de 1 dB sur une large
     bande se perçoit ; 3 dB se remarquent ;
  3. le NIVEAU de chaque section — une montée qui ne monte pas ;
  4. le CALAGE des frappes — un kick 10 ms en retard s'entend comme un flam ;
  5. la LARGEUR stéréo ;
  6. et, en dernier recours, la distance log-mel trame à trame.

Chaque mesure dit son UNITÉ et son seuil d'audibilité, tiré de la littérature
courante (JND de niveau ~1 dB en large bande, de hauteur ~5 cents sur son tenu,
de décalage ~5-10 ms sur des attaques franches). Ce sont des CONDITIONS
NÉCESSAIRES : les tenir ne prouve pas l'indifférenciabilité — seule une écoute à
l'aveugle (ABX) le peut —, mais en rater une prouve qu'on l'entendra.

UNE MESURE QUI NE PEUT PAS VOIR UNE CHOSE LE DIT (règle du dépôt) : un diapason
estimé sur un signal sans hauteur est marqué « non mesurable », pas « 0 cent ».

    analyse/.venv/bin/python tools/ecart-a-l-original.py ORIGINAL.wav RECONSTRUCTION.wav [--json sortie.json]

Rend 0 si les deux fichiers se lisent, 2 sinon. Ne juge pas : il publie.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

SEUILS = {"diapason_cents": 5.0, "bande_db": 1.0, "niveau_db": 1.0, "calage_ms": 5.0}
BANDES = [(20, 60, "sub"), (60, 150, "basse"), (150, 500, "bas-médium"), (500, 2000, "médium"),
          (2000, 6000, "haut-médium"), (6000, 16000, "aigus")]


def lire(chemin: Path) -> tuple[np.ndarray, int]:
    import soundfile as sf
    y, sr = sf.read(str(chemin), dtype="float32", always_2d=True)
    return y, sr


def diapason_cents(mono: np.ndarray, sr: int) -> dict:
    """Écart au diapason 440, en cents, lu sur les PICS SPECTRAUX TENUS.

    `librosa.estimate_tuning` sur le mélange entier a été ESSAYÉ ET ÉCARTÉ (30/09) :
    sur « Reload », il lit +7 cents là où les pics du pad disent +13, et une
    transposition de -13 cents ne le fait bouger que de 5 — la batterie, qui
    domine, noie les partiels. Ici : spectre de Welch à fenêtre longue (1,5 s)
    par tranche de 20 s, pics proéminents entre 100 et 2 000 Hz, écart de chacun
    au demi-ton le plus proche, moyenne CIRCULAIRE pondérée par la puissance (un
    écart de +49 et un de -49 cents sont voisins, pas opposés).
    """
    from scipy.signal import find_peaks, welch
    tranche = 20 * sr
    angles, poids = [], []
    for debut in range(0, max(1, len(mono) - sr), tranche):
        z = mono[debut:debut + tranche]
        if len(z) < 4 * 65536 // 2:
            continue
        f, P = welch(z, fs=sr, nperseg=65536, noverlap=32768)
        k = (f > 100) & (f < 2000)
        Pdb = 10 * np.log10(P[k] + 1e-20)
        pics, _ = find_peaks(Pdb, prominence=12, distance=6)
        for i in pics:
            fi = f[k][i]
            # interpolation parabolique du sommet : la résolution brute (0,67 Hz) vaut
            # 6 cents à 200 Hz
            if 0 < i < len(Pdb) - 1:
                a, b, c = Pdb[i - 1], Pdb[i], Pdb[i + 1]
                dec = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
                fi = fi + dec * (f[1] - f[0])
            midi = 69 + 12 * np.log2(fi / 440.0)
            ecart = midi - np.round(midi)
            angles.append(2 * np.pi * ecart)
            poids.append(10 ** (Pdb[i] / 10))
    if len(angles) < 20:
        return {"cents": None, "pics": len(angles)}
    w = np.array(poids)
    z = np.sum(w * np.exp(1j * np.array(angles))) / np.sum(w)
    return {"cents": round(float(np.angle(z) / (2 * np.pi) * 100), 1), "pics": len(angles),
            "concentration": round(float(np.abs(z)), 2)}


def bandes_par_tranche(y: np.ndarray, sr: int, tranche_s: float) -> np.ndarray:
    """dB par bande et par tranche (tranches × bandes), puissance de la STFT."""
    from scipy.signal import stft
    f, _, Z = stft(y, fs=sr, nperseg=4096, noverlap=2048)
    P = np.abs(Z) ** 2
    trames_par_tranche = max(1, int(round(tranche_s * sr / 2048)))
    n = P.shape[1] // trames_par_tranche
    sortie = np.zeros((n, len(BANDES)))
    for k in range(n):
        bloc = P[:, k * trames_par_tranche:(k + 1) * trames_par_tranche]
        for j, (lo, hi, _) in enumerate(BANDES):
            b = (f >= lo) & (f < hi)
            sortie[k, j] = 10 * np.log10(bloc[b].sum() + 1e-12)
    return sortie


def attaques_basses(mono: np.ndarray, sr: int) -> np.ndarray:
    """Instants (s) des attaques sous 150 Hz — le kick, l'ossature d'un morceau à danser."""
    import librosa
    from scipy.signal import butter, sosfilt
    y = sosfilt(butter(4, 150, btype="low", fs=sr, output="sos"), mono)
    y = librosa.resample(y, orig_sr=sr, target_sr=22050)
    # pas de 32 échantillons à 22 050 Hz : 1,45 ms de résolution, sous le seuil de 5 ms
    # (le premier état, à 128, lisait un retard de 10 ms comme 11,6 : deux pas de 5,8)
    return librosa.onset.onset_detect(y=y, sr=22050, units="time", hop_length=32, backtrack=False)


def decalage_global_ms(a: np.ndarray, b: np.ndarray, sr: int) -> float:
    """Le retard de `b` sur `a` (ms), par corrélation des enveloppes sous 150 Hz."""
    from scipy.signal import butter, correlate, sosfilt
    sos = butter(4, 150, btype="low", fs=sr, output="sos")
    ea, eb = np.abs(sosfilt(sos, a)), np.abs(sosfilt(sos, b))
    pas = max(1, sr // 4000)   # 0,25 ms
    ea, eb = ea[::pas] - ea[::pas].mean(), eb[::pas] - eb[::pas].mean()
    lim = int(0.1 * sr / pas)
    c = correlate(eb, ea, mode="full", method="fft")
    milieu = len(ea) - 1
    fen = c[milieu - lim:milieu + lim + 1]
    return round(float((np.argmax(fen) - lim) * pas / sr * 1000.0), 2)


def calage_ms(a: np.ndarray, b: np.ndarray) -> dict:
    if len(a) == 0 or len(b) == 0:
        return {"mesurable": False}
    idx = np.searchsorted(b, a)
    ecarts = []
    for t, i in zip(a, idx, strict=True):
        cands = [b[j] for j in (i - 1, i) if 0 <= j < len(b)]
        d = min((c - t for c in cands), key=abs)
        if abs(d) < 0.05:
            ecarts.append(d * 1000.0)
    if not ecarts:
        return {"mesurable": False, "apparies": 0, "attaques_original": int(len(a))}
    e = np.array(ecarts)
    return {"mesurable": True, "apparies": int(len(e)), "attaques_original": int(len(a)),
            "attaques_reconstruction": int(len(b)), "median_ms": round(float(np.median(e)), 2),
            "median_abs_ms": round(float(np.median(np.abs(e))), 2),
            "p90_abs_ms": round(float(np.percentile(np.abs(e), 90)), 2)}


def logmel(y: np.ndarray, sr: int) -> np.ndarray:
    import librosa
    m = librosa.feature.melspectrogram(y=librosa.resample(y, orig_sr=sr, target_sr=22050), sr=22050,
                                       n_fft=2048, hop_length=512, n_mels=64)
    return librosa.power_to_db(m + 1e-10)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("original", type=Path)
    ap.add_argument("reconstruction", type=Path)
    ap.add_argument("--tempo", type=float, default=None, help="BPM, pour des tranches de 4 mesures (sinon 7 s)")
    ap.add_argument("--json", type=Path, default=None)
    a = ap.parse_args()
    # UNE COMPARAISON DONT UN CÔTÉ MANQUE REND « RATÉ », PAS « DIFFÉRENT » (règle du dépôt).
    for p in (a.original, a.reconstruction):
        if not p.is_file() or p.stat().st_size == 0:
            print(f"REFUS : {p} absent ou vide")
            return 2
    yo, sro = lire(a.original)
    yr, srr = lire(a.reconstruction)
    if sro != srr:
        import librosa
        yr = librosa.resample(yr.T, orig_sr=srr, target_sr=sro).T
    n = min(len(yo), len(yr))
    dit = []
    if abs(len(yo) - len(yr)) > sro * 0.5:
        dit.append(f"durées différentes : {len(yo)/sro:.1f} s contre {len(yr)/sro:.1f} s — comparé sur {n/sro:.1f} s")
    yo, yr = yo[:n], yr[:n]
    mo, mr = yo.mean(axis=1), yr.mean(axis=1)

    res: dict = {"original": str(a.original), "reconstruction": str(a.reconstruction), "seuils": SEUILS, "dit": dit}

    # 1. DIAPASON
    do, dr = diapason_cents(mo, sro), diapason_cents(mr, sro)
    res["diapason"] = {"original": do, "reconstruction": dr,
                       "ecart_cents": None if do["cents"] is None or dr["cents"] is None
                       else round(((dr["cents"] - do["cents"] + 50) % 100) - 50, 1)}

    # 2-3. ÉQUILIBRE ET NIVEAU PAR TRANCHE
    tranche = 4 * 4 * 60.0 / a.tempo if a.tempo else 7.0
    bo, br = bandes_par_tranche(mo, sro, tranche), bandes_par_tranche(mr, sro, tranche)
    k = min(len(bo), len(br))
    bo, br = bo[:k], br[:k]
    # on ne juge que les tranches où l'original sonne (à 40 dB du maximum)
    tot_o = 10 * np.log10(np.sum(10 ** (bo / 10), axis=1))
    actives = tot_o > tot_o.max() - 40
    tot_r = 10 * np.log10(np.sum(10 ** (br / 10), axis=1))
    niveau = (tot_r - tot_o)[actives]
    # l'équilibre : chaque bande RELATIVE au total de sa tranche, pour séparer « plus
    # fort partout » (le niveau) de « plus brillant » (l'équilibre)
    rel_o = bo - tot_o[:, None]
    rel_r = br - tot_r[:, None]
    ecart_bandes = (rel_r - rel_o)[actives]
    # une bande qui ne porte presque rien dans l'original (< -40 dB du total) ne se juge pas
    porte = rel_o[actives] > -40
    res["tranches"] = {"duree_s": round(tranche, 3), "jugees": int(actives.sum()), "sur": int(k)}
    res["niveau_db"] = {"decalage_median": round(float(np.median(niveau)), 2),
                        "ecart_abs_median_apres_decalage": round(float(np.median(np.abs(niveau - np.median(niveau)))), 2),
                        "pire": round(float(np.max(np.abs(niveau - np.median(niveau)))), 2)}
    par_bande = {}
    for j, (_, _, nom) in enumerate(BANDES):
        e = ecart_bandes[:, j][porte[:, j]]
        par_bande[nom] = None if len(e) == 0 else {
            "median_db": round(float(np.median(e)), 2), "abs_median_db": round(float(np.median(np.abs(e))), 2),
            "tranches_hors_seuil": int(np.sum(np.abs(e) > SEUILS["bande_db"])), "tranches": int(len(e))}
    res["equilibre"] = par_bande

    # 4. CALAGE DES ATTAQUES BASSES
    res["calage_kick"] = calage_ms(attaques_basses(mo, sro), attaques_basses(mr, sro))
    res["calage_kick"]["decalage_global_ms"] = decalage_global_ms(mo, mr, sro)

    # 5. LARGEUR STÉRÉO
    def largeur(y: np.ndarray) -> float | None:
        if y.shape[1] < 2:
            return None
        s, m = (y[:, 0] - y[:, 1]) / 2, (y[:, 0] + y[:, 1]) / 2
        return round(float(np.sum(s ** 2) / max(np.sum(m ** 2), 1e-12)), 4)
    res["largeur_side_sur_mid"] = {"original": largeur(yo), "reconstruction": largeur(yr)}

    # 6. LOG-MEL
    lo, lr = logmel(mo, sro), logmel(mr, sro)
    k = min(lo.shape[1], lr.shape[1])
    d = np.abs(lo[:, :k] - lr[:, :k])
    res["logmel_db"] = {"ecart_abs_moyen": round(float(d.mean()), 2), "ecart_abs_median": round(float(np.median(d)), 2)}

    # --- le compte rendu, lisible ---
    dia = res["diapason"]
    def dire(d: dict) -> str:
        return ("non mesurable" if d["cents"] is None
                else f"{d['cents']:+.1f} cents ({d['pics']} pics, concentration {d['concentration']})")
    print(f"diapason        : original {dire(dia['original'])}, reconstruction {dire(dia['reconstruction'])} — "
          f"écart {dia['ecart_cents'] if dia['ecart_cents'] is not None else 'NON MESURABLE'} (seuil {SEUILS['diapason_cents']:.0f})")
    nv = res["niveau_db"]
    print(f"niveau          : décalage {nv['decalage_median']:+.2f} dB ; autour, écart médian {nv['ecart_abs_median_apres_decalage']:.2f} dB, "
          f"pire tranche {nv['pire']:.2f} dB ({res['tranches']['jugees']} tranches de {tranche:.2f} s)")
    for nom, v in par_bande.items():
        if v is None:
            print(f"  {nom:12s}: non jugée (l'original n'y porte rien)")
        else:
            print(f"  {nom:12s}: écart médian {v['median_db']:+6.2f} dB, |médian| {v['abs_median_db']:5.2f} dB, "
                  f"{v['tranches_hors_seuil']}/{v['tranches']} tranches au-delà de {SEUILS['bande_db']:.0f} dB")
    c = res["calage_kick"]
    if c.get("mesurable"):
        print(f"calage du kick  : {c['apparies']}/{c['attaques_original']} attaques appariées (reconstruction : "
              f"{c['attaques_reconstruction']}) ; médian {c['median_ms']:+.1f} ms, |médian| {c['median_abs_ms']:.1f} ms, p90 {c['p90_abs_ms']:.1f} ms ; "
              f"décalage global (corrélation) {c['decalage_global_ms']:+.2f} ms")
    else:
        print("calage du kick  : NON MESURABLE (aucune attaque appariée à 50 ms près)")
    lg = res["largeur_side_sur_mid"]
    print(f"largeur stéréo  : original {lg['original']}, reconstruction {lg['reconstruction']} (énergie side/mid)")
    print(f"log-mel         : écart moyen {res['logmel_db']['ecart_abs_moyen']:.2f} dB, médian {res['logmel_db']['ecart_abs_median']:.2f} dB")
    for ligne in dit:
        print("DIT :", ligne)
    if a.json:
        a.json.write_text(json.dumps(res, ensure_ascii=False, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
