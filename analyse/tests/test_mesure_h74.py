"""L'instrument de H74 (CDC-reload § 35) sur des signaux CONNUS, avant toute mesure.

Trois « pièces » de bandes qui se chevauchent — un kick (sinus glissant, grave, avec un clic), une caisse claire (bruit
large de 200 Hz à 6 kHz), un charleston (bruit au-dessus de 5 kHz) —, sur une grille où elles frappent souvent
ensemble ; un « stem » qui les mélange à des gains connus. Rendues chacune au gain 1 (d'autres graines de bruit :
rien de cohérent entre stem et rendu), l'instrument doit retrouver les gains.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_near, assert_true, run, test  # noqa: E402

import mesure_h69 as h69  # noqa: E402
import mesure_h74 as h74  # noqa: E402

SR = 44100
DUREE = 40


def piece(nom, instants, graine, caisse_large=False):
    rng = np.random.default_rng(graine)
    x = np.zeros(DUREE * SR)
    for t in instants:
        i = int(round(t * SR))
        if nom == "kick":
            k = h69.replique_tr808(55.0, 0.12, SR, duree=0.4)
            k[:40] += rng.standard_normal(40) * 0.3            # un clic, qui déborde dans l'aigu
        elif nom == "caisse":
            n = int(0.25 * SR)
            bruit = h69.filtrer(rng.standard_normal(n + 400), SR, 200, None if caisse_large else 6000)
            k = bruit[200:200 + n] * np.exp(-np.arange(n) / (0.06 * SR))
        else:
            n = int(0.12 * SR)
            k = h69.filtrer(rng.standard_normal(n + 400), SR, 5000, None)[200:200 + n] * np.exp(-np.arange(n) / (0.03 * SR))
        x[i:i + len(k)] += k[:len(x) - i]
    return x


def grille():
    temps = np.arange(1.0, DUREE - 1.0, 0.5)
    return {"kick": list(temps), "caisse": list(temps[1::2] + 0.0), "charleston": list(np.arange(1.0, DUREE - 1.0, 0.25))}


@test
def test_h74_les_gains_connus_sont_retrouves():
    g = grille()
    gains = {"kick": 1.0, "caisse": 2.0, "charleston": 0.5}
    stem = sum(gains[n] * piece(n, g[n], graine=10 + k) for k, n in enumerate(gains))
    solos = {n: h74.bandes_par_trame(piece(n, g[n], graine=20 + k), SR) for k, n in enumerate(gains)}
    r = h74.resoudre(h74.bandes_par_trame(stem, SR), solos, g)
    for n, attendu in gains.items():
        assert_near(r["facteurs"][n], attendu, 0.1 * attendu, f"« {n} » : {r['facteurs']}")
    # Les bandes principales, telles que les signaux les font (et non telles qu'on les nomme) : la réplique du kick
    # balaye depuis 330 Hz et dépense plus de la moitié de son énergie au-dessus de 150 Hz (H69) ; un bruit blanc
    # porte le plus d'énergie dans la bande la plus large EN HERTZ — 2-4 kHz pour la caisse (200-6 000 Hz), 8-11 kHz
    # pour le charleston (au-dessus de 5 kHz). Écrit d'abord « 30-150 / 500-2000 / 4000-8000 » de tête : faux.
    assert_equal(r["bandes"], {"kick": "150-500", "caisse": "2000-4000", "charleston": "8000-11000"})
    assert_true(r["residu_relatif"] < 0.05, f"résidu {r['residu_relatif']}")
    assert_equal(r["dits"], [])


@test
def test_h74_la_resolution_est_conjointe():
    """Une caisse LARGE BANDE frappe avec le charleston et remplit sa bande : seul un système qui retire la part de la
    caisse aux frappes du charleston retrouve son gain. Pièce par pièce (sans termes croisés), on lit ×1,48 pour ×0,5
    (vu le 11/10, sur ce mutant) ; ce test l'exige à 10 % près."""
    g = grille()
    gains = {"kick": 1.0, "caisse": 2.0, "charleston": 0.5}
    stem = sum(gains[n] * piece(n, g[n], 10 + k, caisse_large=True) for k, n in enumerate(gains))
    solos = {n: h74.bandes_par_trame(piece(n, g[n], 20 + k, caisse_large=True), SR) for k, n in enumerate(gains)}
    r = h74.resoudre(h74.bandes_par_trame(stem, SR), solos, g)
    for n, attendu in gains.items():
        assert_near(r["facteurs"][n], attendu, 0.1 * attendu, f"« {n} » : {r['facteurs']}")


@test
def test_h74_une_borne_atteinte_est_dite():
    g = grille()
    stem = 8.0 * piece("charleston", g["charleston"], 1) + piece("kick", g["kick"], 2)
    solos = {"kick": h74.bandes_par_trame(piece("kick", g["kick"], 3), SR),
             "charleston": h74.bandes_par_trame(piece("charleston", g["charleston"], 4), SR)}
    r = h74.resoudre(h74.bandes_par_trame(stem, SR), solos, {"kick": g["kick"], "charleston": g["charleston"]})
    assert_near(r["facteurs"]["charleston"], 4.0, 1e-9, "ramené à la borne")
    assert_true(any("charleston" in d for d in r["dits"]), f"dits : {r['dits']}")


if __name__ == "__main__":
    raise SystemExit(run())
