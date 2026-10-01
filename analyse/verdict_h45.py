#!/usr/bin/env python3
"""H45 — le seuil d'attaque choisi par stem sur son INDICE DE HACHURE (CDC-reload-indifferenciable § 7).

L'indice d'un stem : la part de ses notes (transcrites au seuil d'usine, 0,5) suivies
d'une note de même hauteur à moins de 30 ms — un son tenu que la transcription hache.

    # calibration (g1, g2 : déjà regardés) — l'indice par stem et par rôle
    python analyse/verdict_h45.py calibrer CORPUS morceau-0001-g1 morceau-0002-g2 [--reload STEM]
    # validation (g3-g5 : jamais regardés) — la règle « indice > X → 0,7 » contre tout à 0,5
    python analyse/verdict_h45.py valider X CORPUS morceau-0003-g3 morceau-0004-g4 morceau-0005-g5 [--reload STEM]
"""

from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from verdict_h44 import f1, notes_aux_seuils  # noqa: E402

ECART = 0.030


def indice_de_hachure(notes: list) -> float:
    par_h = defaultdict(list)
    for d, f, h in notes:
        par_h[h].append((d, f))
    suivies = 0
    for liste in par_h.values():
        liste.sort()
        suivies += sum(1 for (_a0, a1), (b0, _b1) in zip(liste, liste[1:], strict=False) if b0 - a1 < ECART)
    return suivies / max(len(notes), 1)


def parties(corpus: Path, morceaux: list):
    for nom in morceaux:
        verite = json.loads((corpus / nom / "verite.json").read_text(encoding="utf-8"))
        for partie in verite["parties"]:
            if partie.get("pieces") or not partie.get("notes"):
                continue
            stem = corpus / nom / partie["fichier"]
            if not stem.is_file():
                print(f"  {nom} {partie['role']} : stem ABSENT — non mesurée")
                continue
            yield nom, partie, stem


def main() -> int:
    args = sys.argv[1:]
    reload_stem = None
    if "--reload" in args:
        i = args.index("--reload")
        reload_stem = Path(args[i + 1])
        del args[i:i + 2]
    mode = args[0]
    if mode == "calibrer":
        corpus, morceaux = Path(args[1]), args[2:]
        par_role = defaultdict(list)
        for nom, partie, stem in parties(corpus, morceaux):
            ix = indice_de_hachure(notes_aux_seuils(stem)[0.5])
            par_role[partie["role"]].append(ix)
            print(f"  {nom} {partie['role']:18s} indice {ix:.3f}")
        for role, v in sorted(par_role.items()):
            print(f"  RÔLE {role:18s} indices {', '.join(f'{x:.3f}' for x in sorted(v))}")
        if reload_stem is not None:
            print(f"  RELOAD « other » indice {indice_de_hachure(notes_aux_seuils(reload_stem)[0.5]):.3f}")
        return 0
    x = float(args[1])
    corpus, morceaux = Path(args[2]), args[3:]
    temoin: defaultdict[str, list[int]] = defaultdict(lambda: [0, 0, 0])
    essai: defaultdict[str, list[int]] = defaultdict(lambda: [0, 0, 0])
    classement = []
    for nom, partie, stem in parties(corpus, morceaux):
        vraies = [(float(d), int(h)) for h, _v, d, _du in partie["notes"]]
        par_seuil = notes_aux_seuils(stem)
        ix = indice_de_hachure(par_seuil[0.5])
        tenu = ix > x
        classement.append((nom, partie["role"], ix, tenu))
        for acc, notes in ((temoin, par_seuil[0.5]), (essai, par_seuil[0.7 if tenu else 0.5])):
            t, nv, nt, _ = f1(vraies, notes)
            a = acc[partie["role"]]
            a[0] += t
            a[1] += nv
            a[2] += nt
    for nom, role, ix, tenu in classement:
        print(f"  {nom} {role:18s} indice {ix:.3f} → {'TENU (0,7)' if tenu else 'non tenu (0,5)'}")

    def score(a):
        p, r = a[0] / max(a[2], 1), a[0] / max(a[1], 1)
        return 100 * (2 * p * r / (p + r) if p + r else 0.0)
    print("  rôle                 témoin    règle    écart")
    for role in sorted(temoin):
        st, se = score(temoin[role]), score(essai[role])
        print(f"  {role:20s} {st:6.1f}   {se:6.1f}   {se - st:+6.1f}")
    if reload_stem is not None:
        ix = indice_de_hachure(notes_aux_seuils(reload_stem)[0.5])
        print(f"  RELOAD « other » indice {ix:.3f} → {'TENU' if ix > x else 'non tenu'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
