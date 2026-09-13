#!/usr/bin/env python3
"""Le F1 du banc contre le F1 corrigé — et surtout, l'écart est-il STABLE ?

    analyse/.venv/bin/python tools/f1-sonnant.py reconstruction/travail/r1f-13sep

POURQUOI (13/09/2026, § 9.6 de ROADMAP-fusion). Le F1 publié par le banc porte
deux biais, tous deux établis le même jour :

  * il compte la BATTERIE dans la vérité, alors que `stems[].noteConfidence` ne
    porte aucune percussion — la chaîne la compte ailleurs (`drums.hits`) ;
  * il compare la hauteur ÉCRITE, alors que 9,9 % des notes mélodiques du corpus
    SONNENT ailleurs, le patch tiré au hasard désaccordant un oscillateur.

Les deux vont dans le même sens : ils sous-estiment la chaîne.

CE QUE CE BANC MESURE, et pourquoi il ne remplace pas le F1 du banc. La MÊME règle
d'appariement est appliquée des deux côtés (hauteur + début, 50 ms par défaut) :
une seule variable change, la vérité comparée. Les valeurs absolues ne
reproduisent donc PAS le F1 du banc, qui a sa propre règle — c'est l'ÉCART qui se
lit, et lui seul.

ET C'EST L'ÉCART PAR MORCEAU QUI DÉCIDE, pas sa moyenne. S'il est stable, l'ancien
F1 peut être retiré et le biais documenté une fois pour toutes. S'il ne l'est pas,
la correction dépend du corpus — et sur `r1f-13sep` elle en dépend entièrement :
l'écart va de 0,0 à 54,0 points, parce que `morceau-0001-g1` n'a pas un F1 bas
mais un F1 QUI NE VEUT RIEN DIRE (ses deux parties mélodiques sont désaccordées de
+4,75 et −8,25 demi-tons, et aucune de ses 161 notes ne sonne où sa vérité
l'écrit).
"""
from __future__ import annotations

import json
import os
import statistics
import sys
from bisect import bisect_left
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hauteur_sonnante import hauteurs_sonnantes  # noqa: E402

TOLERANCE = float(os.environ.get("VSM_TOLERANCE", "0.05"))
SOURCE_DEFAUT = Path(__file__).resolve().parents[1] / "reconstruction/travail/s1-sec"


def _proche(liste: list[float], t: float) -> bool:
    i = bisect_left(liste, t)
    return any(0 <= j < len(liste) and abs(liste[j] - t) <= TOLERANCE for j in (i - 1, i))


def comptes_du_morceau(dossier: Path, source: Path, sonnant: bool) -> tuple[int, int, int, int]:
    """(vraies retrouvées, vraies totales, écrites justes, écrites totales)."""
    verite = json.loads((source / dossier.name / "verite.json").read_text(encoding="utf-8"))
    rapport = json.loads((dossier / "course" / "rapport.json").read_text(encoding="utf-8"))

    vraies: dict[int, list[float]] = defaultdict(list)
    for partie in verite.get("parties", []):
        percussive = partie.get("role") == "batterie"
        if sonnant and percussive:
            continue
        for note in partie.get("notes", []):
            hauteurs = ([int(note[0])] if (not sonnant or percussive)
                        else hauteurs_sonnantes(partie, int(note[0])))
            for h in hauteurs:
                vraies[h].append(float(note[2]))
    for liste in vraies.values():
        liste.sort()

    ecrites: dict[int, list[float]] = defaultdict(list)
    for stem in rapport.get("stems", []):
        for n in stem.get("noteConfidence", []) or []:
            ecrites[int(n["note"])].append(float(n["start"]))
    for liste in ecrites.values():
        liste.sort()

    v_tot = v_ok = 0
    for partie in verite.get("parties", []):
        percussive = partie.get("role") == "batterie"
        if sonnant and percussive:
            continue
        for note in partie.get("notes", []):
            hauteurs = ([int(note[0])] if (not sonnant or percussive)
                        else hauteurs_sonnantes(partie, int(note[0])))
            v_tot += 1
            if any(_proche(ecrites.get(h, []), float(note[2])) for h in hauteurs):
                v_ok += 1

    e_tot = e_ok = 0
    for h, liste in ecrites.items():
        for t in liste:
            e_tot += 1
            if _proche(vraies.get(h, []), t):
                e_ok += 1

    return v_ok, v_tot, e_ok, e_tot


def f1_des_comptes(v_ok: int, v_tot: int, e_ok: int, e_tot: int) -> tuple[float, float, float]:
    """(précision, rappel, F1) à partir des comptes bruts.

    L'agrégat se calcule sur les COMPTES du lot entier, jamais comme moyenne des
    F1 par morceau : une moyenne de F1 n'est pas un F1, et un morceau de cent
    notes y pèserait autant qu'un morceau de mille.
    """
    rappel = v_ok / v_tot if v_tot else 0.0
    precision = e_ok / e_tot if e_tot else 0.0
    f1 = 2 * precision * rappel / (precision + rappel) if precision + rappel else 0.0
    return precision, rappel, f1


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__.splitlines()[2].strip(), file=sys.stderr)
        return 2
    lot = Path(argv[1])
    source = Path(argv[2]) if len(argv) > 2 else SOURCE_DEFAUT
    morceaux = [d for d in sorted(lot.glob("morceau-*"))
                if (d / "course" / "rapport.json").is_file()]
    if not morceaux:
        print(f"aucun morceau mesurable sous {lot}")
        return 1

    print(f"{len(morceaux)} morceau(x), tolérance {TOLERANCE * 1000:.0f} ms, "
          f"MÊME règle d'appariement des deux côtés")
    print(f"\n{'morceau':18s} {'F1 actuel':>10} {'F1 sonnant':>11} {'écart':>10}")
    ecarts = []
    cumul = {False: [0, 0, 0, 0], True: [0, 0, 0, 0]}
    for d in morceaux:
        valeurs = {}
        for sonnant in (False, True):
            c = comptes_du_morceau(d, source, sonnant)
            for i in range(4):
                cumul[sonnant][i] += c[i]
            valeurs[sonnant] = f1_des_comptes(*c)[2]
        ecarts.append(valeurs[True] - valeurs[False])
        print(f"{d.name:18s} {valeurs[False]:10.4f} {valeurs[True]:11.4f} "
              f"{100 * ecarts[-1]:+7.1f} pt")

    print()
    print(f"{'':18s} {'précision':>10} {'rappel':>11} {'F1':>10}")
    for sonnant, nom in ((False, "actuel"), (True, "sonnant")):
        p_, r_, f_ = f1_des_comptes(*cumul[sonnant])
        print(f"{nom:18s} {p_:10.4f} {r_:11.4f} {f_:10.4f}")
    ecart_agrege = f1_des_comptes(*cumul[True])[2] - f1_des_comptes(*cumul[False])[2]
    print(f"{'écart agrégé':18s} {'':10} {'':11} {100 * ecart_agrege:+9.1f} pt")

    mediane = statistics.median(ecarts)
    print(f"\nécart : médiane {100 * mediane:+.1f} pt, de {100 * min(ecarts):+.1f} "
          f"à {100 * max(ecarts):+.1f}, écart-type {100 * statistics.pstdev(ecarts):.1f} pt")
    stable = (max(ecarts) - min(ecarts)) < 0.10
    print("ÉCART STABLE : l'ancien F1 peut être retiré, le biais documenté une fois"
          if stable else
          "ÉCART INSTABLE : la correction dépend du corpus — l'ancien F1 se garde à côté")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
