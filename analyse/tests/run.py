#!/usr/bin/env python3
"""Lance toutes les suites de `analyse/tests/`.

    analyse/.venv/bin/python analyse/tests/run.py            # tout
    analyse/.venv/bin/python analyse/tests/run.py corpus     # les tests dont le nom contient « corpus »
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

ICI = Path(__file__).resolve().parent
sys.path.insert(0, str(ICI))
sys.path.insert(0, str(ICI.parent))

import framework  # noqa: E402


def main() -> int:
    filtre = sys.argv[1] if len(sys.argv) > 1 else ""
    for chemin in sorted(ICI.glob("test_*.py")):
        specification = importlib.util.spec_from_file_location(chemin.stem, chemin)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)
    return framework.run(filtre)


if __name__ == "__main__":
    raise SystemExit(main())
