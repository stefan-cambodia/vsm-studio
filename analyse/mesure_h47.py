#!/usr/bin/env python3
"""H47 — LE PARC CONTIENT-IL LE PAD DE « RELOAD » ? (docs/CDC-reload-indifferenciable.md § 9)

La question des machines neuves ne dépend pas de la transcription : elle se pose avec
des notes PARFAITES. Cet outil prend l'extrait du stem où le pad est seul, écrit ses
notes par une règle (l'oracle), fabrique deux bornes par sinus, puis fait jouer ces
notes à chaque machine mélodique du parc — patch d'usine, le multi-échantillons une
fois par profil — au diapason du morceau, et mesure chaque rendu contre l'extrait.

    analyse/.venv/bin/python -u analyse/mesure_h47.py mesurer STEM.wav \\
        --moteur build-h42/tools/vsm-render --sortie reconstruction/travail/reload-h47
    analyse/.venv/bin/python analyse/mesure_h47.py verdict reconstruction/travail/reload-h47/mesure.json

`mesurer` écrit `mesure.json` (tout ce qui conditionne le résultat est dans sa
provenance) et imprime le verdict ; `verdict` le RECALCULE depuis le fichier, sans
rien rendre. Rend 0 quand la mesure est complète, 2 sur un refus (fichier absent,
extrait trop court, aucun pic). L'outil ne choisit pas l'issue : il publie.

CE QUE LE § 9 LAISSAIT À L'INSTRUMENT, ÉCRIT ICI AVANT LA MESURE (§ 9.1) :

  - LE SPECTRE de l'oracle est le Welch de l'estimateur de diapason (fenêtre de
    65 536 points, recouvrement de moitié), sur l'extrait entier ; mêmes pics
    (`find_peaks`, proéminence 12 dB, écart de 6 cases), interpolés à la parabole.
    L'AMPLITUDE d'un pic, pour `B_mesuré`, est la puissance sommée sur ± 4 cases
    (± 2,7 Hz : le pic du pad fait 2 Hz de large à cause de ses battements),
    divisée par la largeur de bruit de la fenêtre de Hann (1,5 case).
  - L'ÉQUILIBRE se lit par tranches de 4 mesures à 138,00 BPM (6,957 s — trois
    tranches pleines dans l'extrait), écart MÉDIAN par bande, comme au § 0 ; une
    bande « porte » dans une tranche si elle y est à plus de −40 dB du total dans
    l'extrait OU dans le rendu. C'est le code de `tools/ecart-a-l-original.py`
    (`bandes_par_tranche`, `logmel`), chargé tel quel — pas une copie.
  - LE RENDU est calé au niveau efficace de l'extrait AVANT l'équilibre et le
    log-mel ; `D` est insensible au niveau et reçoit le rendu brut, comme dans la
    chaîne.
  - LE CLASSEMENT `D` est celui de la chaîne : une candidate que son garde-fou de
    niveau écarterait (il faudrait plus de ×10 au fader pour atteindre le niveau de
    l'extrait) est MESURÉE et PUBLIÉE, mais hors classement — la chaîne ne la
    choisirait jamais. Un rendu vide est nommé « non mesuré », jamais compté zéro.
    Le Spearman est publié deux fois : sur le classement (celui qui juge
    l'attendu 5) et sur toutes les candidates mesurées.
  - « LES CINQ PREMIÈRES » sont cinq CANDIDATES (machine + profil), pas cinq
    machines distinctes : si cinq profils du multi-échantillons sont en tête, ce
    sont eux qui sont réglés, et c'est dit.
  - LE PROJET rendu porte le tempo mesuré (138,0 — il ne sert qu'aux machines qui
    calent un arpège ou un retard dessus) et UNE note par hauteur de l'oracle, de
    0 à la fin de l'extrait, vélocité 100.
  - UN COEFFICIENT ne se lit pas sans son nombre de points DISTINCTS : publié.

CE QUE L'INSTRUMENT NE VOIT PAS, ET QU'IL DIT. L'extrait commence à 16 s, le pad
sonnant déjà (il entre à 14 s) ; le rendu, lui, commence par l'ATTAQUE de la machine.
Une machine à attaque lente y perd sur les premières trames sans être un mauvais
pad. Et le stem porte un résidu de séparation (la batterie qui fuit) qu'aucune
machine ne joue : c'est ce que les deux bornes chiffrent — leurs écarts de bande
sont publiés à côté de ceux des machines, et c'est à eux qu'une machine se compare.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import subprocess
import sys
import time
from pathlib import Path
from types import ModuleType
from typing import Any, Dict, List, Optional, Sequence, Tuple

import numpy as np

ICI = Path(__file__).resolve().parent
RACINE = ICI.parent
sys.path.insert(0, str(ICI))

DEBUT_S, FIN_S = 16.0, 40.0          # le pad seul : il entre à 14 s, la basse à 42 s (§ 1)
LA4_HZ = 443.1372                    # +12,3 cents (§ 1, § 4)
TEMPO_BPM = 138.0                    # § 8
PROEMINENCE_DB = 12.0
F_MIN_HZ, F_MAX_HZ = 100.0, 2000.0
SOUS_LE_PLUS_FORT_DB = 20.0
DEMI_LOBE = 4                        # cases sommées de part et d'autre d'un pic
HANN_CASES = 1.5                     # largeur de bruit de la fenêtre de Hann, en cases
VELOCITE = 100
PORTE_DB = -40.0
CLASSES_ATTENDUES = frozenset({11, 1, 4, 6, 10})   # si, do♯, mi, fa♯, la♯ (§ 1)
NOMS_DE_CLASSE = ("do", "do♯", "ré", "ré♯", "mi", "fa", "fa♯", "sol", "sol♯", "la", "la♯", "si")
BUDGET = 40                          # celui du réglage de piste de la chaîne
PREMIERES = 5
VOLUME_DE_BASE, VOLUME_MAX = 0.9, 10.0   # le garde-fou de niveau de l'arbitrage
TENUE_MIN_DB = -6.0
PLANCHER_DB = -120.0


def outil_ecart() -> ModuleType:
    """`tools/ecart-a-l-original.py`, chargé tel quel : les bandes et le log-mel du § 0."""
    chemin = RACINE / "tools" / "ecart-a-l-original.py"
    spec = importlib.util.spec_from_file_location("ecart_a_l_original", chemin)
    if spec is None or spec.loader is None:
        raise SystemExit(f"REFUS : {chemin} illisible — l'instrument du § 0 manque")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def rms(y: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(np.asarray(y, dtype=np.float64))))) if len(y) else 0.0


# ------------------------------------------------------------------ l'oracle

def pics_tenus(mono: np.ndarray, sr: int) -> List[Dict[str, float]]:
    """Les pics tenus de l'extrait : fréquence interpolée, niveau du pic, amplitude du sinus équivalent."""
    from scipy.signal import find_peaks, welch

    if len(mono) < 2 * 65536:
        raise ValueError(f"extrait trop court pour le Welch de l'oracle : {len(mono)} points")
    f, P = welch(np.asarray(mono, dtype=np.float64), fs=sr, nperseg=65536, noverlap=32768, scaling="spectrum")
    k = (f > F_MIN_HZ) & (f < F_MAX_HZ)
    fk, Pk = f[k], P[k]
    Pdb = 10 * np.log10(Pk + 1e-20)
    indices, _ = find_peaks(Pdb, prominence=PROEMINENCE_DB, distance=6)
    if len(indices) == 0:
        return []
    plafond = float(max(Pdb[i] for i in indices))
    pas = float(f[1] - f[0])
    sortie: List[Dict[str, float]] = []
    for i in indices:
        if Pdb[i] < plafond - SOUS_LE_PLUS_FORT_DB:
            continue
        hz = float(fk[i])
        if 0 < i < len(Pdb) - 1:
            a, b, c = Pdb[i - 1], Pdb[i], Pdb[i + 1]
            if (a - 2 * b + c) != 0:
                hz += float(0.5 * (a - c) / (a - 2 * b + c) * pas)
        lo, hi = max(0, i - DEMI_LOBE), min(len(Pk), i + DEMI_LOBE + 1)
        puissance = float(Pk[lo:hi].sum() / HANN_CASES)
        sortie.append({"hz": hz, "db": float(Pdb[i]), "amplitude": math.sqrt(2.0 * puissance)})
    return sortie


def notes_de_l_oracle(pics: Sequence[Dict[str, float]], la4: float) -> List[int]:
    """UNE note par hauteur MIDI la plus proche au diapason `la4` ; deux pics sur la même n'en font qu'une."""
    return sorted({int(round(69 + 12 * math.log2(p["hz"] / la4))) for p in pics})


def hz_de(note: int, la4: float) -> float:
    return la4 * 2.0 ** ((note - 69) / 12.0)


# ------------------------------------------------------------------ les bornes

def borne_egale(notes: Sequence[int], la4: float, n: int, sr: int) -> np.ndarray:
    """Des sinus purs aux fréquences des NOTES, amplitudes égales."""
    t = np.arange(n) / sr
    y = np.zeros(n)
    for note in notes:
        y += np.sin(2 * np.pi * hz_de(note, la4) * t)
    return y


def borne_mesuree(pics: Sequence[Dict[str, float]], n: int, sr: int) -> np.ndarray:
    """Des sinus aux fréquences et amplitudes MESURÉES des pics."""
    t = np.arange(n) / sr
    y = np.zeros(n)
    for p in pics:
        y += p["amplitude"] * np.sin(2 * np.pi * p["hz"] * t)
    return y


# ------------------------------------------------------------------ les mesures

def caler(rendu: np.ndarray, extrait: np.ndarray) -> np.ndarray:
    """Le rendu à la longueur et au niveau efficace de l'extrait (un silence reste un silence)."""
    n = len(extrait)
    y = np.zeros(n)
    m = min(n, len(rendu))
    y[:m] = np.asarray(rendu[:m], dtype=np.float64)
    r = rms(y)
    return y if r <= 0.0 else y * (rms(extrait) / r)


def equilibre(extrait: np.ndarray, rendu_cale: np.ndarray, sr: int, outil: ModuleType,
              tranche_s: float) -> Dict[str, Optional[float]]:
    """Écart médian par bande (rendu − extrait, chaque bande relative au total de sa tranche), en dB.

    `None` : la bande ne porte dans aucune tranche, ni dans l'extrait ni dans le rendu.
    """
    bo = outil.bandes_par_tranche(np.asarray(extrait, dtype=np.float64), sr, tranche_s)
    br = outil.bandes_par_tranche(np.asarray(rendu_cale, dtype=np.float64), sr, tranche_s)
    k = min(len(bo), len(br))
    bo, br = bo[:k], br[:k]
    tot_o = 10 * np.log10(np.sum(10 ** (bo / 10), axis=1))
    tot_r = 10 * np.log10(np.sum(10 ** (br / 10), axis=1))
    rel_o, rel_r = bo - tot_o[:, None], br - tot_r[:, None]
    porte = (rel_o > PORTE_DB) | (rel_r > PORTE_DB)
    sortie: Dict[str, Optional[float]] = {}
    for j, (_lo, _hi, nom) in enumerate(outil.BANDES):
        e = (rel_r - rel_o)[:, j][porte[:, j]]
        sortie[nom] = None if len(e) == 0 else round(float(np.median(e)), 2)
    return sortie


def logmel_porteur(extrait: np.ndarray, rendu_cale: np.ndarray, sr: int, outil: ModuleType) -> Tuple[float, int, int]:
    """Écart log-mel moyen sur les cases qui portent (à 40 dB du maximum de l'extrait) ; cases jugées, cases en tout."""
    lo = outil.logmel(np.asarray(extrait, dtype=np.float32), sr)
    lr = outil.logmel(np.asarray(rendu_cale, dtype=np.float32), sr)
    k = min(lo.shape[1], lr.shape[1])
    lo, lr = lo[:, :k], lr[:, :k]
    porte = lo > lo.max() - 40.0
    return float(np.abs(lo - lr)[porte].mean()), int(porte.sum()), int(porte.size)


def tenue_db(rendu: np.ndarray) -> Optional[float]:
    """Niveau du dernier tiers rapporté au premier, en dB ; `None` si le premier tiers est muet."""
    tiers = len(rendu) // 3
    if tiers == 0:
        return None
    a, b = rms(rendu[:tiers]), rms(rendu[len(rendu) - tiers:])
    if a <= 0.0:
        return None
    return round(max(PLANCHER_DB, 20 * math.log10(max(b, 1e-12) / a)), 2)


def mesures_de(extrait: np.ndarray, rendu: np.ndarray, sr: int, outil: ModuleType, distance: Any,
               tranche_s: float) -> Dict[str, Any]:
    """Les quatre mesures du § 9 pour un rendu, contre l'extrait."""
    cale = caler(rendu, extrait)
    ecart, cases, sur = logmel_porteur(extrait, cale, sr, outil)
    niveau = rms(rendu)
    return {
        "D": round(float(distance(np.asarray(rendu, dtype=np.float32))), 6),
        "logmel": round(ecart, 3),
        "cases": cases, "cases_sur": sur,
        "bandes": equilibre(extrait, cale, sr, outil, tranche_s),
        "tenue_db": tenue_db(np.asarray(rendu)[:len(extrait)]),
        "rms": niveau,
    }


def ecartee_par_le_niveau(niveau_rendu: float, niveau_extrait: float) -> Optional[str]:
    """La raison pour laquelle l'arbitrage de la chaîne écarterait ce rendu, ou `None`."""
    if niveau_rendu <= 0.0:
        return "silence"
    facteur = VOLUME_DE_BASE * niveau_extrait / niveau_rendu
    return f"trop faible, il faudrait ×{facteur:.1f}" if facteur > VOLUME_MAX else None


# ------------------------------------------------------------------ le verdict

def pire_bande(bandes: Dict[str, Optional[float]]) -> Optional[Tuple[str, float]]:
    portees = [(nom, e) for nom, e in bandes.items() if e is not None]
    return max(portees, key=lambda c: abs(c[1])) if portees else None


def juger_timbre(m: Dict[str, Any], borne: float) -> Tuple[str, str]:
    """Attendus 3 et 4 : chaque bande qui porte à ≤ 1 dB ET log-mel ≤ `B_égal` + 1 dB ; échec au-delà de 3."""
    pire = pire_bande(m["bandes"])
    if pire is None:
        return "NON MESURABLE", "aucune bande ne porte"
    surcout = m["logmel"] - borne
    detail = (f"pire bande {pire[0]} {pire[1]:+.2f} dB ; log-mel {m['logmel']:.2f} dB, "
              f"soit B_égal {surcout:+.2f} dB")
    if abs(pire[1]) <= 1.0 and surcout <= 1.0:
        return "TENU", detail
    if abs(pire[1]) > 3.0 or surcout > 3.0:
        return "ÉCHEC", detail
    return "ENTRE LES DEUX", detail


def nom_de(c: Dict[str, Any]) -> str:
    return c["machine"] + (f"[{c['profil']}]" if c.get("profil") else "")


def verdict(mesure: Dict[str, Any]) -> List[Tuple[int, str, str]]:
    """Les six attendus du § 9, recalculés depuis `mesure.json` : (numéro, verdict, détail)."""
    sortie: List[Tuple[int, str, str]] = []
    classes = set(mesure["oracle"]["classes"])
    manque = sorted(CLASSES_ATTENDUES - classes)
    en_trop = sorted(classes - CLASSES_ATTENDUES)
    dit = ", ".join(NOMS_DE_CLASSE[c] for c in sorted(classes))
    if not manque and not en_trop:
        sortie.append((1, "TENU", f"classes {dit}"))
    else:
        sortie.append((1, "ÉCHEC", f"classes {dit} — manque : {', '.join(NOMS_DE_CLASSE[c] for c in manque) or 'rien'} ; "
                                   f"en trop : {', '.join(NOMS_DE_CLASSE[c] for c in en_trop) or 'rien'}"))

    b = mesure["bornes"]
    soi_nul = b["soi"]["logmel"] == 0.0 and abs(b["soi"]["D"]) < 1e-9
    ordre = b["mesuree"]["logmel"] <= b["egale"]["logmel"]
    sortie.append((2, "TENU" if soi_nul and ordre else "ÉCHEC",
                   f"l'extrait contre lui-même : log-mel {b['soi']['logmel']:.3f} dB, D {b['soi']['D']:.6f} ; "
                   f"B_mesuré {b['mesuree']['logmel']:.2f} dB, B_égal {b['egale']['logmel']:.2f} dB"))
    borne = float(b["egale"]["logmel"])

    classement = [mesure["candidates"][i] for i in mesure["classement"]]
    if classement:
        v, d = juger_timbre(classement[0], borne)
        sortie.append((3, v, f"{nom_de(classement[0])} (D {classement[0]['D']:.3f}) : {d}"))
    else:
        sortie.append((3, "NON MESURABLE", "aucune candidate au classement"))

    regles = [r for r in mesure.get("reglage", []) if r.get("D") is not None]
    if regles:
        meilleur = min(regles, key=lambda r: r["D"])
        v, d = juger_timbre(meilleur, borne)
        sortie.append((4, v, f"{nom_de(meilleur)} réglée (D {meilleur['D_depart']:.3f} → {meilleur['D']:.3f}, "
                             f"{meilleur['evaluations']} évaluations) : {d}"))
    else:
        sortie.append((4, "NON MESURABLE", "aucun réglage de piste n'a abouti"))

    s = mesure["spearman"]
    if s.get("rho") is None:
        sortie.append((5, "NON MESURABLE", f"{s.get('n', 0)} candidate(s) au classement"))
    else:
        v = "TENU" if s["rho"] >= 0.7 else ("ÉCHEC" if s["rho"] < 0.4 else "ENTRE LES DEUX")
        sortie.append((5, v, f"Spearman {s['rho']:+.3f} sur {s['n']} candidates ({s['distincts_D']} D distinctes, "
                             f"{s['distincts_logmel']} log-mel distincts) ; toutes mesurées : "
                             f"{s['rho_toutes']:+.3f} sur {s['n_toutes']}"))

    cinq = classement[:PREMIERES]
    tiennent = [c for c in cinq if c["tenue_db"] is not None and c["tenue_db"] >= TENUE_MIN_DB]
    v = "TENU" if len(tiennent) >= 3 else ("ÉCHEC" if len(tiennent) == 0 else "ENTRE LES DEUX")
    lues = ", ".join(f"{nom_de(c)} {c['tenue_db'] if c['tenue_db'] is not None else 'non mesurable'}" for c in cinq)
    sortie.append((6, v, f"{len(tiennent)} sur {len(cinq)} tiennent la note (≥ {TENUE_MIN_DB:.0f} dB) : {lues}"))
    return sortie


def imprimer_verdict(mesure: Dict[str, Any]) -> None:
    print("VERDICT (recalculé depuis la mesure) :")
    for numero, v, detail in verdict(mesure):
        print(f"  attendu {numero} : {v} — {detail}")


# ------------------------------------------------------------------ la course

def lire_extrait(stem: Path, debut_s: float, fin_s: float) -> Tuple[np.ndarray, int]:
    import soundfile as sf

    info = sf.info(str(stem))
    sr = int(info.samplerate)
    a, b = int(round(debut_s * sr)), int(round(fin_s * sr))
    if b > info.frames:
        raise ValueError(f"{stem} : {info.frames / sr:.1f} s, l'extrait demande {fin_s:.1f} s")
    x, _ = sf.read(str(stem), start=a, stop=b, dtype="float64", always_2d=True)
    return x.mean(axis=1), sr


def _ligne(nom: str, m: Dict[str, Any]) -> str:
    pire = pire_bande(m["bandes"])
    tenue = "non mesurable" if m["tenue_db"] is None else f"{m['tenue_db']:+7.2f} dB"
    bande = "aucune bande" if pire is None else f"{pire[0]} {pire[1]:+7.2f} dB"
    return f"  {nom:44s} D {m['D']:.3f}  log-mel {m['logmel']:6.2f} dB  pire bande {bande:26s}  tenue {tenue}"


def spearman(candidates: Sequence[Dict[str, Any]]) -> Tuple[Optional[float], int, int, int]:
    from scipy.stats import spearmanr

    d = [c["D"] for c in candidates]
    lm = [c["logmel"] for c in candidates]
    if len(d) < 3 or len(set(d)) < 3 or len(set(lm)) < 3:
        return None, len(d), len(set(d)), len(set(lm))
    rho = float(spearmanr(d, lm)[0])
    return round(rho, 3), len(d), len(set(d)), len(set(lm))


def mesurer(a: argparse.Namespace) -> int:
    from analyzer import diapason
    from analyzer.vsm_distance_cache import cached_distance_for
    from analyzer.vsm_engine import VsmEngine
    from analyzer.vsm_offline_render import render_track_offline
    from analyzer.vsm_project_export import ExportNote, ExportTrack
    from analyzer.vsm_reconstruct import melodic_machines
    from analyzer.vsm_track_arbitration import build_candidates
    from analyzer.vsm_track_refine import refine_patch_on_track
    from reconstruire import profils_pour_arbitrage

    for p in (a.stem, a.moteur):
        if not p.is_file() or p.stat().st_size == 0:
            print(f"REFUS : {p} absent ou vide")
            return 2
    try:
        extrait, sr = lire_extrait(a.stem, a.debut, a.fin)
    except ValueError as e:
        print(f"REFUS : {e}")
        return 2
    n = len(extrait)
    duree = n / sr
    tranche_s = 4 * 4 * 60.0 / a.tempo
    outil = outil_ecart()
    distance = cached_distance_for("v2")(np.asarray(extrait, dtype=np.float32), sr)
    niveau_extrait = rms(extrait)

    try:
        pics = pics_tenus(extrait, sr)
    except ValueError as e:
        print(f"REFUS : {e}")
        return 2
    if not pics:
        print("REFUS : aucun pic tenu dans l'extrait — l'oracle n'a rien à écrire")
        return 2
    notes = notes_de_l_oracle(pics, a.la4)
    classes = sorted({note % 12 for note in notes})
    print(f"EXTRAIT : {a.stem} de {a.debut:.0f} à {a.fin:.0f} s ({duree:.2f} s à {sr} Hz), niveau efficace {niveau_extrait:.4f}")
    print(f"ORACLE : {len(pics)} pic(s), {len(notes)} note(s) — "
          + ", ".join(f"{note} ({NOMS_DE_CLASSE[note % 12]})" for note in notes))
    for p in pics:
        midi = 69 + 12 * math.log2(p["hz"] / a.la4)
        print(f"  pic {p['hz']:8.2f} Hz  {p['db']:7.1f} dB  amplitude {p['amplitude']:.5f}  → note {midi:6.2f}")

    bornes = {
        "soi": mesures_de(extrait, extrait, sr, outil, distance, tranche_s),
        "egale": mesures_de(extrait, borne_egale(notes, a.la4, n, sr), sr, outil, distance, tranche_s),
        "mesuree": mesures_de(extrait, borne_mesuree(pics, n, sr), sr, outil, distance, tranche_s),
    }
    print("BORNES :")
    for cle, nom in (("soi", "l'extrait contre lui-même"), ("egale", "B_égal"), ("mesuree", "B_mesuré")):
        print(_ligne(nom, bornes[cle]))
        print("      bandes : " + ", ".join(f"{k} {'—' if v is None else format(v, '+.2f')}" for k, v in bornes[cle]["bandes"].items()))

    diapason.poser(a.la4)
    travail = a.sortie / "travail"
    travail.mkdir(parents=True, exist_ok=True)
    export_notes = [ExportNote(note=note, velocity=VELOCITE, start=0.0, duration=duree) for note in notes]

    def rendre(machine: str, parametres: Dict[str, float], profil: str, dossier: Path) -> Optional[np.ndarray]:
        piste = ExportTrack(name="pad", machine=machine, parameters=dict(parametres),
                            notes=list(export_notes), profile=profil)
        dossier.mkdir(parents=True, exist_ok=True)
        rendu = render_track_offline(piste, dossier, sr, duration=duree, tempo=a.tempo,
                                     binary=str(a.moteur), title="h47-pad")
        (dossier / "rendu.wav").unlink(missing_ok=True)
        return rendu

    depart = time.time()
    resultats: List[Dict[str, Any]] = []
    non_mesurees: List[str] = []
    with VsmEngine(binary=str(a.moteur), sample_rate=sr) as moteur:
        machines = melodic_machines(moteur)
        candidates = build_candidates([], machines, profils_pour_arbitrage(moteur, machines))
        print(f"CANDIDATES : {len(candidates)} ({len(machines)} machines mélodiques, le multi-échantillons une fois par profil), "
              f"diapason {a.la4:.4f} Hz, moteur {a.moteur}")
        for i, candidate in enumerate(candidates):
            rendu = rendre(candidate.machine, {}, candidate.profile, travail / f"candidate-{i}")
            fiche: Dict[str, Any] = {"machine": candidate.machine, "profil": candidate.profile,
                                     "origine": candidate.origin}
            if rendu is None or rendu.size == 0:
                fiche["ecartee"] = "rendu vide"
                non_mesurees.append(nom_de(fiche))
                print(f"  {nom_de(fiche):44s} NON MESURÉE — rendu vide")
                resultats.append(fiche)
                continue
            fiche.update(mesures_de(extrait, rendu, sr, outil, distance, tranche_s))
            fiche["ecartee"] = ecartee_par_le_niveau(fiche["rms"], niveau_extrait)
            print(_ligne(nom_de(fiche), fiche) + (f"  HORS CLASSEMENT ({fiche['ecartee']})" if fiche["ecartee"] else ""))
            resultats.append(fiche)

        classement = sorted((i for i, c in enumerate(resultats) if c.get("ecartee") is None),
                            key=lambda i: resultats[i]["D"])
        print(f"CLASSEMENT D : {len(classement)} candidate(s) ; hors classement : "
              f"{sum(1 for c in resultats if c.get('ecartee') not in (None, 'rendu vide'))} par le niveau, "
              f"{len(non_mesurees)} non mesurée(s)")

        reglages: List[Dict[str, Any]] = []
        for rang, i in enumerate(classement[:a.premieres], start=1):
            c = resultats[i]
            issue = refine_patch_on_track(c["machine"], {}, export_notes, np.asarray(extrait, dtype=np.float32),
                                          moteur, travail / f"reglage-{rang}", sr, budget=a.budget, metric="v2",
                                          tempo=a.tempo, binary=str(a.moteur), name="pad",
                                          stem_rms=niveau_extrait, base_volume=VOLUME_DE_BASE,
                                          max_volume=VOLUME_MAX, profile=c["profil"])
            fiche = {"machine": c["machine"], "profil": c["profil"], "rang_usine": rang, "D_usine": c["D"]}
            if issue is None:
                fiche["D"] = None
                fiche["dit"] = "réglage non tenté (aucun axe déclaré, ou rendu de départ en échec)"
                print(f"  RÉGLAGE {rang} {nom_de(c):36s} NON TENTÉ — aucun axe déclaré, ou rendu de départ en échec")
                reglages.append(fiche)
                continue
            rendu = rendre(c["machine"], issue.parameters, c["profil"], travail / f"regle-{rang}")
            if rendu is None or rendu.size == 0:
                fiche["D"] = None
                fiche["dit"] = "rendu du patch réglé vide"
                print(f"  RÉGLAGE {rang} {nom_de(c):36s} NON MESURÉ — rendu du patch réglé vide")
                reglages.append(fiche)
                continue
            fiche.update(mesures_de(extrait, rendu, sr, outil, distance, tranche_s))
            fiche.update({"D_depart": round(issue.start_distance, 6), "D_reglage": round(issue.distance, 6),
                          "evaluations": issue.evaluations, "parametres": issue.parameters,
                          "ameliorations": [[axe, valeur, round(d, 6)] for axe, valeur, d in issue.improvements]})
            print(_ligne(f"RÉGLAGE {rang} {nom_de(c)}", fiche)
                  + f"  ({issue.start_distance:.3f} → {issue.distance:.3f}, {issue.evaluations} évaluations)")
            reglages.append(fiche)

    rho, nb, dd, dl = spearman([resultats[i] for i in classement])
    rho_t, nb_t, _, _ = spearman([c for c in resultats if c.get("ecartee") != "rendu vide"])
    tete = subprocess.run(["git", "-C", str(RACINE), "rev-parse", "--short", "HEAD"],
                          capture_output=True, text=True, check=False).stdout.strip()
    mesure: Dict[str, Any] = {
        "provenance": {
            "stem": str(a.stem), "debut_s": a.debut, "fin_s": a.fin, "sr": sr,
            "empreinte_extrait": hashlib.sha256(np.asarray(extrait, dtype=np.float64).tobytes()).hexdigest()[:16],
            "la4_hz": a.la4, "tempo": a.tempo, "tranche_s": round(tranche_s, 4),
            "moteur": str(a.moteur), "moteur_md5": hashlib.md5(a.moteur.read_bytes()).hexdigest(),
            "metrique": "v2", "budget": a.budget, "premieres": a.premieres, "code": tete,
            "duree_s": round(time.time() - depart, 1),
        },
        "oracle": {"pics": [{k: round(v, 5) for k, v in p.items()} for p in pics], "notes": notes, "classes": classes},
        "bornes": bornes,
        "candidates": resultats,
        "classement": classement,
        "non_mesurees": non_mesurees,
        "reglage": reglages,
        "spearman": {"rho": rho, "n": nb, "distincts_D": dd, "distincts_logmel": dl,
                     "rho_toutes": rho_t, "n_toutes": nb_t},
    }
    sortie = a.sortie / "mesure.json"
    sortie.write_text(json.dumps(mesure, indent=1, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8")
    print(f"ÉCRIT : {sortie} ({mesure['provenance']['duree_s']} s)")
    imprimer_verdict(mesure)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sous = ap.add_subparsers(dest="mode", required=True)
    m = sous.add_parser("mesurer")
    m.add_argument("stem", type=Path)
    m.add_argument("--moteur", type=Path, required=True)
    m.add_argument("--sortie", type=Path, required=True)
    m.add_argument("--debut", type=float, default=DEBUT_S)
    m.add_argument("--fin", type=float, default=FIN_S)
    m.add_argument("--la4", type=float, default=LA4_HZ)
    m.add_argument("--tempo", type=float, default=TEMPO_BPM)
    m.add_argument("--budget", type=int, default=BUDGET)
    m.add_argument("--premieres", type=int, default=PREMIERES)
    v = sous.add_parser("verdict")
    v.add_argument("mesure", type=Path)
    a = ap.parse_args()
    if a.mode == "verdict":
        if not a.mesure.is_file() or a.mesure.stat().st_size == 0:
            print(f"REFUS : {a.mesure} absent ou vide")
            return 2
        imprimer_verdict(json.loads(a.mesure.read_text(encoding="utf-8")))
        return 0
    return mesurer(a)


if __name__ == "__main__":
    sys.exit(main())
