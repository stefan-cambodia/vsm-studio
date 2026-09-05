#!/usr/bin/env python3
"""Compare deux rapports du banc synthétique, MORCEAU PAR MORCEAU.

Une médiane sur vingt cache huit morceaux qui bougent dans un sens et douze
dans l'autre : le seul témoin recevable d'une campagne contre S1 est la ligne
à ligne, mêmes morceaux, mêmes graines (docs/CDC-separation-par-synthese.md
§ 2.7). Pour chaque morceau : distance globale, écart de parité, parties
fondues et inventées, SDR de la basse — A contre B — et, quand B a tourné
avec la boucle résiduelle, ce qu'elle a soustrait et ce que le résidu a
donné.

    analyse/.venv/bin/python analyse/comparer_bancs.py s1-sec-banc/rapport.json r1-sec-banc/rapport.json
"""
from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Optional

import numpy as np


def _f(x, fmt="{:.3f}") -> str:
    if x is None:
        return "—"
    try:
        if isinstance(x, float) and not np.isfinite(x):
            return "inf" if x > 0 else "-inf"
        return fmt.format(x)
    except (TypeError, ValueError):
        return str(x)


def _bass(m: dict) -> Optional[float]:
    return (((m.get("separation") or {}).get("stems") or {}).get("bass") or {}).get("sdr_db")


def _mediane(valeurs):
    v = [float(x) for x in valeurs if x is not None and np.isfinite(x)]
    return float(np.median(v)) if v else None


def comparer(a: dict, b: dict, nom_a: str, nom_b: str) -> str:
    par_nom_a = {m["morceau"]: m for m in a.get("morceaux", [])}
    par_nom_b = {m["morceau"]: m for m in b.get("morceaux", [])}
    communs = [n for n in par_nom_a if n in par_nom_b]
    lignes = [f"A = {nom_a} (commit {a.get('provenance', {}).get('commit')}, options {a.get('provenance', {}).get('optionsChaine')})",
              f"B = {nom_b} (commit {b.get('provenance', {}).get('commit')}, options {b.get('provenance', {}).get('optionsChaine')})",
              f"{len(communs)} morceau(x) en commun ; seulement dans A : {sorted(set(par_nom_a) - set(par_nom_b)) or 'aucun'} ; "
              f"seulement dans B : {sorted(set(par_nom_b) - set(par_nom_a)) or 'aucun'}", ""]
    entete = (f"{'morceau':22s} {'global A':>9s} {'global B':>9s} {'écart':>7s} {'par. A':>6s} {'par. B':>6s} "
              f"{'fond A/B':>9s} {'inv A/B':>8s} {'bass A':>7s} {'bass B':>7s} {'secondes A/B':>13s}")
    lignes += [entete, "-" * len(entete)]
    ecarts, bass_a, bass_b = [], [], []
    for nom in communs:
        ma, mb = par_nom_a[nom], par_nom_b[nom]
        ga, gb = (ma.get("global") or {}).get("global"), (mb.get("global") or {}).get("global")
        ecart = (gb - ga) / ga * 100.0 if ga and gb else None
        if ecart is not None:
            ecarts.append(ecart)
        pa, pb = ma.get("parite") or {}, mb.get("parite") or {}
        bass_a.append(_bass(ma))
        bass_b.append(_bass(mb))
        lignes.append(f"{nom[:22]:22s} {_f(ga, '{:.4f}'):>9s} {_f(gb, '{:.4f}'):>9s} {_f(ecart, '{:+.1f} %'):>7s} "
                      f"{_f(pa.get('ecart'), '{:+d}'):>6s} {_f(pb.get('ecart'), '{:+d}'):>6s} "
                      f"{len(pa.get('fondues') or []):>4d}/{len(pb.get('fondues') or []):<4d} "
                      f"{_f(pa.get('inventees'), '{}'):>3s}/{_f(pb.get('inventees'), '{}'):<4s} "
                      f"{_f(_bass(ma), '{:.1f}'):>7s} {_f(_bass(mb), '{:.1f}'):>7s} "
                      f"{_f(ma.get('course_secondes'), '{:.0f}'):>6s}/{_f(mb.get('course_secondes'), '{:.0f}'):<6s}")
        res = mb.get("residuel") or {}
        if res.get("mesure"):
            for it in res.get("iterations", []):
                if not it.get("unite"):
                    lignes.append(f"{'':22s}   r{it['iteration']} : rien de soustrait")
                    continue
                sep = it.get("separation") or {}
                bass_res = ((sep.get("stems") or {}).get("bass") or {}).get("sdr_db") if sep.get("mesure") else None
                dp = it.get("distance_projet") or {}
                lignes.append(f"{'':22s}   r{it['iteration']} : {it['unite']} (corr {_f(it.get('correlation_stem'), '{:.2f}')}) ; "
                              f"SDR résidu vrai {_f(it.get('sdr_residu_vrai_avant_db'), '{:.1f}')} → "
                              f"{_f(it.get('sdr_residu_vrai_apres_db'), '{:.1f}')} dB ; bass au résidu {_f(bass_res, '{:.1f}')} dB ; "
                              f"+{len(it.get('pistes_ajoutees') or [])} piste(s), {it.get('stems_refuses', 0)} refusé(s) ; "
                              f"distance {_f(dp.get('avant'))} → {_f(dp.get('apres'))}")
            arret = res.get("arret") or {}
            lignes.append(f"{'':22s}   arrêt : {arret.get('motif')}")
    lignes.append("")
    ga_med = _mediane([(par_nom_a[n].get("global") or {}).get("global") for n in communs])
    gb_med = _mediane([(par_nom_b[n].get("global") or {}).get("global") for n in communs])
    lignes.append(f"MÉDIANES sur {len(communs)} morceaux : global A {_f(ga_med, '{:.4f}')} B {_f(gb_med, '{:.4f}')} ; "
                  f"écart médian par morceau {_f(_mediane(ecarts), '{:+.1f} %')} "
                  f"({sum(1 for e in ecarts if e < 0)} baissent, {sum(1 for e in ecarts if e > 0)} montent, "
                  f"{sum(1 for e in ecarts if e == 0)} égaux) ; "
                  f"parité A {_f(_mediane([(par_nom_a[n].get('parite') or {}).get('ecart') for n in communs]), '{:+.0f}')} "
                  f"B {_f(_mediane([(par_nom_b[n].get('parite') or {}).get('ecart') for n in communs]), '{:+.0f}')} ; "
                  f"inventées A {sum((par_nom_a[n].get('parite') or {}).get('inventees', 0) for n in communs)} "
                  f"B {sum((par_nom_b[n].get('parite') or {}).get('inventees', 0) for n in communs)} ; "
                  f"bass SDR A {_f(_mediane(bass_a), '{:.2f}')} B {_f(_mediane(bass_b), '{:.2f}')} dB")
    rb = (b.get("agrege") or {}).get("residuel") or {}
    if rb.get("morceaux_mesures"):
        lignes.append(f"RÉSIDUEL (B) : soustractions dans {rb['morceaux_avec_soustraction']}/{rb['morceaux_mesures']} ; "
                      f"corr. médiane r1 {_f(rb.get('correlation_stem_mediane_r1'), '{:.2f}')} ; "
                      f"SDR résidu vrai {_f(rb.get('montee_sdr_residu_vrai_mediane_r1'), '{:+.1f}')} dB ; "
                      f"bass au résidu {_f(rb.get('bass_sdr_au_residu_mediane_r1'), '{:.2f}')} dB ; "
                      f"pistes ajoutées {rb.get('pistes_ajoutees_total')} ; stems refusés {rb.get('stems_refuses_total')} ; "
                      f"arrêts {rb.get('arrets')}")
    return "\n".join(lignes)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    a, b = Path(sys.argv[1]), Path(sys.argv[2])
    print(comparer(json.loads(a.read_text(encoding="utf-8")), json.loads(b.read_text(encoding="utf-8")),
                   a.parent.name, b.parent.name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
