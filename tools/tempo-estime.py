#!/usr/bin/env python3
"""L'estimateur de tempo de la chaîne, jugé contre la vérité du banc synthétique.

    analyse/.venv/bin/python tools/tempo-estime.py reconstruction/travail/s1-sec

RÈGLE QUE CET OUTIL FAIT RESPECTER (docs/ROADMAP-fusion.md, F-tempo, 15/09/2026).
Une reconstruction s'ouvre au tempo du morceau, estimé par `analyzer.tempo` sur
le mélange ; l'attendu, écrit AVANT la mesure : sur les morceaux du lot dont
`verite.json` porte un tempo, l'estimation tombe à ±2 BPM de la vérité sur
QUATRE MORCEAUX SUR CINQ au moins (80 %), les erreurs d'OCTAVE (×2, ×½, ×3, ×⅔)
comptées À PART et publiées ; le témoin est 120 BPM fixe, ce que la chaîne
écrivait avant. Le balayage est publié ENTIER, jamais son meilleur point.

Rend 0 si l'attendu tient, 1 sinon, 2 si le lot est introuvable ou vide.

H46 (docs/CDC-reload-indifferenciable.md § 8, 30/09/2026) — `--affine [--part 0.9]`.
± 2 BPM suffisait à ne plus ouvrir un morceau à 120 ; pas à une GRILLE : un BPM
d'écart, c'est un temps entier de dérive par minute. Avec `--affine`, l'outil juge
l'estimation AFFINÉE (`estimer_tempo(..., affiner=True)`) dans l'unité qui compte,
la DÉRIVE de la grille à la fin du morceau — `|Δbpm| / bpm vrai × durée` —, contre
le témoin du même code (le tempo suivi seul) :

    analyse/.venv/bin/python tools/tempo-estime.py reconstruction/travail/s2 --affine

Attendu : dérive ≤ 20 ms sur `--part` des morceaux (0,9 par défaut). Les affinages
NON CONCLUANTS sont nommés, et les morceaux que l'affinage EMPIRE (dérive affinée
au-delà de celle du témoin) sont comptés à part : un correcteur se juge sur ce qu'il
casse autant que sur ce qu'il répare.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE / "analyse"))

from analyzer.tempo import estimer_tempo  # noqa: E402

TOLERANCE_BPM = 2.0
PART_ATTENDUE = 0.8


def lire_wav_mono(chemin: Path) -> tuple[np.ndarray, int]:
    """Le mélange en mono flottant (les morceaux du banc sont en 32 bits flottants, que `wave` refuse)."""
    import soundfile as sf

    x, sr = sf.read(str(chemin), dtype="float32", always_2d=True)
    return x.mean(axis=1), int(sr)


def rapport_d_octave(estime: float, vrai: float) -> str:
    for facteur, nom in ((2.0, "×2"), (0.5, "×½"), (3.0, "×3"), (2.0 / 3.0, "×⅔"), (1.5, "×1,5")):
        if abs(estime - vrai * facteur) <= TOLERANCE_BPM * facteur:
            return nom
    return ""


DERIVE_MAX_MS = 20.0


def juger_affinage(lot: Path, part_attendue: float) -> int:
    """H46 : la dérive de grille en fin de morceau, affinée contre suivie."""
    morceaux = sorted(d for d in lot.glob("morceau-*") if (d / "verite.json").is_file() and (d / "morceau.wav").is_file())
    if not morceaux:
        print(f"REFUS : aucun morceau avec verite.json et morceau.wav sous {lot}")
        return 2
    print(f"{'morceau':20s} {'vrai':>5s} {'durée':>6s} {'suivi':>6s} {'affiné':>8s} {'max.':>8s} {'cohér.':>6s} {'grille':>6s} "
          f"{'dérive suivi':>12s} {'dérive affiné':>13s}  verdict")
    tenus = empires = non_concluants = 0
    derives: list[float] = []
    derives_temoin: list[float] = []
    for d in morceaux:
        verite = json.load(open(d / "verite.json"))
        vrai = float(verite["tempo"])
        audio, sr = lire_wav_mono(d / "morceau.wav")
        duree = float(verite.get("duree") or len(audio) / sr)
        t = estimer_tempo(audio, sr, affiner=True)
        af = t.affinage
        assert af is not None
        derive_temoin = abs(af.depart - vrai) / vrai * duree * 1000.0
        derive = abs(t.bpm - vrai) / vrai * duree * 1000.0
        derives.append(derive)
        derives_temoin.append(derive_temoin)
        tenu = derive <= DERIVE_MAX_MS
        tenus += tenu
        empire = derive > derive_temoin + 1e-9
        empires += empire
        non_concluants += not af.concluant
        notes = []
        if not af.concluant:
            notes.append(f"NON CONCLUANT ({af.raison}, seuil {af.seuil:.2f})")
        if af.entier:
            notes.append("entier retenu")
        if empire:
            notes.append("EMPIRÉ")
        print(f"{d.name:20s} {vrai:5.0f} {duree:5.0f}s {af.depart:6.1f} {t.bpm:8.3f} {af.maximum:8.3f} {af.coherence:6.2f} "
              f"{af.subdivision:6d} {derive_temoin:9.0f} ms {derive:10.1f} ms  {'OK' if tenu else 'RATÉ'}"
              f"{'  ' + ' ; '.join(notes) if notes else ''}")
    n = len(morceaux)
    print()
    print(f"dérive ≤ {DERIVE_MAX_MS:.0f} ms : {tenus}/{n} ; médiane {np.median(derives):.1f} ms "
          f"(témoin, tempo suivi seul : {np.median(derives_temoin):.0f} ms) ; "
          f"non concluants : {non_concluants} ; EMPIRÉS par l'affinage : {empires}")
    verdict = tenus / n >= part_attendue
    print(f"VERDICT : {'TENU' if verdict else 'TOMBE'} (attendu ≥ {100 * part_attendue:.0f} % à ≤ {DERIVE_MAX_MS:.0f} ms de dérive)")
    return 0 if verdict else 1


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    lot = Path(argv[1])
    if "--affine" in argv[2:]:
        part = float(argv[argv.index("--part") + 1]) if "--part" in argv else 0.9
        return juger_affinage(lot, part)
    morceaux = sorted(d for d in lot.glob("morceau-*") if (d / "verite.json").is_file() and (d / "morceau.wav").is_file())
    if not morceaux:
        print(f"REFUS : aucun morceau avec verite.json et morceau.wav sous {lot}")
        return 2
    print(f"{'morceau':22s} {'vrai':>6s} {'estimé':>7s} {'écart':>7s} {'témoin 120':>10s}  octave  verdict")
    tenus = 0
    octaves = 0
    ecarts: list[float] = []
    ecarts_temoin: list[float] = []
    for d in morceaux:
        vrai = float(json.load(open(d / "verite.json"))["tempo"])
        audio, sr = lire_wav_mono(d / "morceau.wav")
        t = estimer_tempo(audio, sr)
        ecart = t.bpm - vrai
        ecarts.append(abs(ecart))
        ecarts_temoin.append(abs(120.0 - vrai))
        octave = "" if abs(ecart) <= TOLERANCE_BPM else rapport_d_octave(t.bpm, vrai)
        tenu = abs(ecart) <= TOLERANCE_BPM
        tenus += tenu
        octaves += bool(octave)
        print(f"{d.name:22s} {vrai:6.1f} {t.bpm:7.1f} {ecart:+7.1f} {120.0 - vrai:+10.1f}  {octave:6s}  {'OK' if tenu else 'RATÉ'}")
    n = len(morceaux)
    part = tenus / n
    print()
    print(f"à ±{TOLERANCE_BPM:.0f} BPM : {tenus}/{n} ({100 * part:.0f} %), erreurs d'octave : {octaves} ; "
          f"écart médian {np.median(ecarts):.1f} BPM (témoin 120 fixe : {np.median(ecarts_temoin):.1f})")
    verdict = part >= PART_ATTENDUE
    print(f"VERDICT : {'TENU' if verdict else 'TOMBE'} (attendu ≥ {100 * PART_ATTENDUE:.0f} % à ±{TOLERANCE_BPM:.0f} BPM)")
    return 0 if verdict else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
