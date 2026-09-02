"""La séparation en voix : des pistes JOUABLES à partir d'un fourre-tout.

C'est le mécanisme de H23 (ROADMAP-fusion § 5 quaterdecies). Le stem `other`
d'*Us and Them* porte 4,83 notes simultanées en moyenne sur 66 demi-tons, joué
par UNE machine. `separer_en_voix` partage un tel stem par REGISTRES — un
choix d'algorithme fait PAR LA MESURE : la séparation par continuité de
hauteur, essayée d'abord, rendait quatre voix balayant chacune 65-66
demi-tons (des parts de gâteau) ; le partage par registres rend des
intervalles disjoints d'ambitus 9 à 28.

Le contrat que ces tests fixent :
  - ce qui n'est PAS un fourre-tout ne se découpe JAMAIS (le garde-fou) ;
  - un fourre-tout se partage en registres DISJOINTS, sans perdre une note ;
  - le résultat est borné, replié de ses déchets, et déterministe.
"""

from __future__ import annotations

import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RACINE))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from framework import assert_equal, assert_true, run, test  # noqa: E402

from analyzer.vsm_reconstruct import (StemNote, densite_du_stem,  # noqa: E402
                                      separer_en_voix, stem_fourre_tout)


def note(hauteur, debut, duree=0.4):
    return StemNote(note=hauteur, velocity=100, start=debut, duration=duree)


def fourre_tout_a_cinq_lignes():
    """Cinq lignes superposées sur cinq octaves et demie — la silhouette du
    stem `other` d'Us and Them (polyphonie 4,83, ambitus 66)."""
    lignes = []
    for base in (31, 48, 60, 72, 90):
        lignes += [note(base + (i % 5), i * 0.5, 0.45) for i in range(64)]
    return lignes


@test
def ce_qui_n_est_pas_un_fourre_tout_ne_se_decoupe_jamais():
    """LE GARDE-FOU LE PLUS IMPORTANT : ne pas inventer de pistes.

    Trois cas qui sont UNE partie chacun, et que le partage par registres
    découperait volontiers si la porte n'était pas fermée :
      - une mélodie qui saute d'octave (large mais monophonique) ;
      - une suite d'accords serrés (polyphonique mais étroite) ;
      - une basse et une mélodie clairsemées (ni dense ni large à la fois).
    """
    melodie = [note(60 + (7 * i) % 24, i * 0.5) for i in range(24)]
    assert_equal(len(separer_en_voix(melodie, maximum=4)), 1,
                 "une mélodie qui saute reste une voix")

    accords = [note(h, i * 1.0, 0.9) for i in range(8) for h in (60, 64, 67)]
    assert_equal(len(separer_en_voix(accords, maximum=4)), 1,
                 "un accompagnement d'accords reste une voix — c'est UN instrument")

    duo = ([note(36 + (i % 3), i * 1.0, 0.9) for i in range(16)]
           + [note(72 + (i % 5), i * 0.5, 0.4) for i in range(32)])
    assert_equal(len(separer_en_voix(duo, maximum=4)), 1,
                 "deux lignes clairsemées ne franchissent pas le seuil du fourre-tout")


@test
def un_fourre_tout_se_partage_en_registres_disjoints():
    """Le cœur du contrat : les voix sont des INTERVALLES de hauteur qui ne se
    recouvrent pas — c'est ce qui les rend nommables (l'aigu, le médium, la
    basse) et ce que la continuité de hauteur ne garantissait pas."""
    voix = separer_en_voix(fourre_tout_a_cinq_lignes(), maximum=5)
    assert_equal(len(voix), 5, "cinq registres, cinq voix")
    assert_equal(sum(len(v) for v in voix), 5 * 64, "aucune note perdue")

    bornes = [(min(n.note for n in v), max(n.note for n in v)) for v in voix]
    # de l'aiguë à la grave, et disjointes
    for (bas_haut, _), (_, haut_bas) in zip(bornes, bornes[1:]):
        assert_true(bas_haut > haut_bas,
                    "registres disjoints attendus : " + str(bornes))


@test
def chaque_voix_produite_repasse_sous_le_seuil():
    """LE CRITÈRE DE H23, tel que la feuille de route l'a écrit : le succès ne
    se juge pas à la distance mais au fait que chaque piste produite cesse
    d'être un fourre-tout."""
    lignes = fourre_tout_a_cinq_lignes()
    assert_true(stem_fourre_tout(densite_du_stem(lignes)) != "",
                "le tout est bien un fourre-tout")
    for k, v in enumerate(separer_en_voix(lignes, maximum=5), 1):
        d = densite_du_stem(v)
        assert_equal(stem_fourre_tout(d), "",
                     f"la voix {k} est redevenue une partie : {d}")


@test
def le_maximum_borne_le_nombre_de_voix():
    voix = separer_en_voix(fourre_tout_a_cinq_lignes(), maximum=3)
    assert_equal(len(voix), 3, "le maximum est respecté")
    assert_equal(sum(len(v) for v in voix), 5 * 64, "aucune note perdue")


@test
def une_note_orpheline_ne_fait_pas_une_piste():
    """Un fourre-tout dont une note isolée traîne très haut : la voix d'une
    note serait un déchet de découpage exporté comme une partie réelle.

    L'initialisation aux quantiles PONDÉRÉS PAR LA DURÉE est ce qui l'empêche :
    aucun centre ne se pose sur une poignée de notes, donc l'orpheline rejoint
    le registre voisin dès l'affectation — pas de piste à replier après coup.
    Le test le vérifie avec maximum=5 : cinq centres pour cinq vraies lignes,
    l'orpheline n'en détourne aucun."""
    lignes = fourre_tout_a_cinq_lignes() + [note(127, 3.0, 0.2)]
    voix = separer_en_voix(lignes, maximum=5)
    assert_equal(len(voix), 5, "cinq registres, pas un de plus")
    assert_equal(sum(len(v) for v in voix), 5 * 64 + 1, "l'orpheline n'est pas perdue")
    assert_true(any(n.note == 127 for n in voix[0]),
                "elle a rejoint le registre le plus proche : l'aigu")
    assert_true(all(len(v) >= 60 for v in voix),
                "aucune voix squelettique : " + str([len(v) for v in voix]))


@test
def la_separation_est_deterministe():
    """Mêmes notes, mêmes voix, jusqu'à l'ordre près : une chaîne de MESURE ne
    peut pas rendre deux découpages selon l'ordre d'arrivée des notes."""
    import random
    rng = random.Random(7)
    notes = [note(rng.choice((40, 41, 43, 64, 65, 67, 88, 89)),
                  round(rng.uniform(0.0, 30.0), 3),
                  round(rng.uniform(0.6, 2.0), 3)) for _ in range(400)]
    a = separer_en_voix(list(notes), maximum=4)
    b = separer_en_voix(list(reversed(notes)), maximum=4)
    cle = lambda voix: [[(n.note, n.start, n.duration) for n in v] for v in voix]
    assert_equal(cle(a), cle(b), "l'ordre d'arrivée ne change rien")


if __name__ == "__main__":
    raise SystemExit(run())
