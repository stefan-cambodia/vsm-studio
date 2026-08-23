"""
Cadre de test minimal, sans dépendance — le pendant Python de
`audio/tests/TestFramework.h`.

POURQUOI PAS PYTEST. Le § 9 du cahier des charges de l'apprentissage interdit
toute nouvelle dépendance lourde, et le dépôt a déjà tranché la même question
côté C++ : il y a un cadre maison de cinquante lignes plutôt qu'un framework.
La raison n'est pas l'économie, c'est que les tests doivent pouvoir tourner
HORS LIGNE et sans installation, exactement comme le reste du projet se compile
sans réseau. Un test qu'on ne peut pas lancer n'existe pas.

Usage :

    from framework import test, assert_true, assert_near, run

    @test
    def deux_et_deux_font_quatre():
        assert_true(2 + 2 == 4, "arithmétique")

    if __name__ == "__main__":
        raise SystemExit(run())
"""

from __future__ import annotations

import sys
import time
import traceback
from typing import Callable, List, Tuple

_TESTS: List[Tuple[str, Callable[[], None]]] = []


class EchecDeTest(AssertionError):
    pass


def test(fonction: Callable[[], None]) -> Callable[[], None]:
    """Enregistre la fonction comme test. L'ordre d'exécution est celui de
    déclaration : un test qui dépend d'un état construit par le précédent est
    une faute, mais un ordre stable rend au moins l'échec reproductible."""
    _TESTS.append((fonction.__name__, fonction))
    return fonction


def assert_true(condition: bool, message: str = "") -> None:
    if not condition:
        raise EchecDeTest(message or "condition fausse")


def assert_equal(obtenu, attendu, message: str = "") -> None:
    if obtenu != attendu:
        raise EchecDeTest(f"{message or 'égalité'} : obtenu {obtenu!r}, attendu {attendu!r}")


def assert_near(obtenu: float, attendu: float, tolerance: float, message: str = "") -> None:
    if abs(float(obtenu) - float(attendu)) > tolerance:
        raise EchecDeTest(
            f"{message or 'proximité'} : obtenu {obtenu!r}, attendu {attendu!r} "
            f"± {tolerance!r}")


def assert_raises(exception, fonction: Callable[[], object], message: str = "") -> None:
    try:
        fonction()
    except exception:
        return
    except Exception as autre:  # noqa: BLE001 - on veut nommer ce qui est arrivé
        raise EchecDeTest(f"{message or 'exception'} : attendu {exception.__name__}, "
                          f"obtenu {type(autre).__name__}") from autre
    raise EchecDeTest(f"{message or 'exception'} : {exception.__name__} n'a pas été levée")


def run(filtre: str = "") -> int:
    """Exécute les tests enregistrés. Rend le nombre d'échecs (code de sortie)."""
    reussis, echoues = 0, 0
    for nom, fonction in _TESTS:
        if filtre and filtre not in nom:
            continue
        depart = time.perf_counter()
        try:
            fonction()
        except Exception as erreur:  # noqa: BLE001 - un test peut échouer de toutes les façons
            echoues += 1
            print(f"[ÉCHEC] {nom} — {erreur}")
            if not isinstance(erreur, EchecDeTest):
                traceback.print_exc()
        else:
            reussis += 1
            duree = time.perf_counter() - depart
            print(f"[OK]    {nom}" + (f"  ({duree:.1f} s)" if duree > 0.5 else ""))
    print(f"\n{reussis} réussis, {echoues} échoués ({reussis + echoues} au total)")
    return echoues


def main(argv: List[str]) -> int:
    return run(argv[1] if len(argv) > 1 else "")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
