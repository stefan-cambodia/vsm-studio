#!/usr/bin/env python3
"""Le BANC SYNTHÉTIQUE : où la chaîne perd, étage par étage.

Prend un dossier de morceaux fabriqués par `morceaux.py` (chacun avec sa
vérité et ses stems vrais), fait tourner `reconstruire.py` sur chaque
`morceau.wav` — la chaîne d'aujourd'hui, avec ses défauts : séparation
htdemucs_6s, --parite, tout le parc — et publie, par morceau et agrégé :

  1. séparation     2. transcription     3. parité
  4. arbitrage (rang de la vraie machine, borne de piste)
  5. distance globale et l'écart à la borne

Tout dans <sortie>/rapport.json (avec provenance) et <sortie>/tableau.txt.
Le détail des mesures : docs/CDC-banc-synthetique.md § 2.4.

Usage :
  analyse/.venv/bin/python -u analyse/banc_synthetique.py reconstruction/travail/s1-sec --sortie reconstruction/travail/s1-sec-banc
      [--stems-vrais] [--rendus-paralleles 6] [--sans-course] [-- options supplémentaires de reconstruire.py]

--stems-vrais donne à la chaîne les stems VRAIS regroupés (bass, drums, other)
au lieu de la séparer : la variable devient la chaîne seule, et l'étage 1
est marqué « non mesuré ». Reprenable : une course dont rapport.json existe
n'est pas rejouée.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from analyzer.vsm_banc import agreger, mesurer_morceau, tableau  # noqa: E402
from analyzer.vsm_engine import VsmEngine, identite_du_moteur  # noqa: E402
from analyzer.vsm_morceaux import (commit_du_depot, ecrire_wav_float,  # noqa: E402
                                   morceau_complet, stems_attendus)

ICI = Path(__file__).resolve().parent


def heure() -> str:
    return datetime.now().strftime("%H:%M:%S")


def courir(morceau: Path, dossier: Path, args: argparse.Namespace, reste: list) -> dict:
    """La chaîne sur un morceau ; sautée si déjà courue. Rend {code, secondes, commande}."""
    course = dossier / "course"
    stems_separes = dossier / "stems-separes"
    if (course / "rapport.json").exists():
        print(f"[{heure()}] SAUTÉ {morceau.name} (déjà couru : {course / 'rapport.json'})")
        return {"code": 0, "secondes": None, "saute": True, "commande": None}
    commande = [sys.executable, "-u", str(ICI / "reconstruire.py"), str(morceau / "morceau.wav"),
                "--sortie", str(course), "--rendus-paralleles", str(args.rendus_paralleles)]
    if args.stems_vrais:
        groupes = dossier / "stems-vrais-groupes"
        groupes.mkdir(parents=True, exist_ok=True)
        verite = json.loads((morceau / "verite.json").read_text(encoding="utf-8"))
        for nom, audio in stems_attendus(verite, morceau).items():
            ecrire_wav_float(groupes / f"{nom}.wav", audio)
        commande += ["--stems", str(groupes)]
    else:
        commande += ["--garder-stems", str(stems_separes)]
    if args.moteur:
        commande += ["--moteur", args.moteur]
    commande += reste
    journal = dossier / "course.log"
    print(f"[{heure()}] DÉBUT {morceau.name} → {journal}")
    depart = time.time()
    with journal.open("w", encoding="utf-8") as f:
        code = subprocess.run(commande, stdout=f, stderr=subprocess.STDOUT).returncode
    secondes = time.time() - depart
    print(f"[{heure()}] COURSE {morceau.name} : code {code} en {secondes:.0f} s")
    if code != 0:
        print(journal.read_text(encoding="utf-8")[-2000:])
    return {"code": code, "secondes": secondes, "saute": False, "commande": commande}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("morceaux", type=Path, help="dossier du lot (sous-dossiers morceau-*/ avec verite.json)")
    ap.add_argument("--sortie", type=Path, required=True, help="dossier du banc (courses, mesures, rapport)")
    ap.add_argument("--stems-vrais", action="store_true",
                    help="donner à la chaîne les stems vrais regroupés au lieu de séparer")
    ap.add_argument("--rendus-paralleles", type=int, default=6)
    ap.add_argument("--sans-course", action="store_true", help="ne mesurer que les courses déjà faites")
    ap.add_argument("--moteur", default=None, help="chemin de vsm-render")
    args, reste = ap.parse_known_args()
    if reste and reste[0] == "--":
        reste = reste[1:]

    morceaux = sorted(d for d in args.morceaux.iterdir() if d.is_dir() and morceau_complet(d))
    incomplets = sorted(d.name for d in args.morceaux.iterdir() if d.is_dir() and d.name.startswith("morceau-")
                        and not morceau_complet(d))
    if incomplets:
        print(f"IGNORÉS (vérité incomplète) : {', '.join(incomplets)}")
    if not morceaux:
        print(f"aucun morceau complet dans {args.morceaux}")
        return 1
    args.sortie.mkdir(parents=True, exist_ok=True)
    print(f"[{heure()}] banc : {len(morceaux)} morceaux, chaîne {'sur stems vrais' if args.stems_vrais else 'avec séparation'}"
          + (f", options {' '.join(reste)}" if reste else ""))

    courses: dict = {}
    mesures: list = []
    non_mesures: list = []
    depart = time.time()
    with VsmEngine(binary=args.moteur, sample_rate=44100) as moteur:
        moteur_identite = identite_du_moteur(moteur)
        for morceau in morceaux:
            dossier = args.sortie / morceau.name
            dossier.mkdir(parents=True, exist_ok=True)
            if args.sans_course:
                if not (dossier / "course" / "rapport.json").exists():
                    non_mesures.append({"morceau": morceau.name, "raison": "pas de course (--sans-course)"})
                    print(f"[{heure()}] NON MESURÉ {morceau.name} : pas de course")
                    continue
                courses[morceau.name] = {"code": 0, "secondes": None, "saute": True, "commande": None}
            else:
                courses[morceau.name] = courir(morceau, dossier, args, reste)
                if courses[morceau.name]["code"] != 0:
                    non_mesures.append({"morceau": morceau.name,
                                        "raison": f"la chaîne a rendu le code {courses[morceau.name]['code']}"})
                    continue
            try:
                mesure = mesurer_morceau(morceau, dossier / "course", dossier / "stems-separes", moteur,
                                         stems_vrais_fournis=args.stems_vrais)
            except Exception as erreur:  # noqa: BLE001 — on veut nommer ce qui est arrivé, et continuer
                non_mesures.append({"morceau": morceau.name, "raison": f"mesure impossible : {erreur!r}"})
                print(f"[{heure()}] NON MESURÉ {morceau.name} : {erreur!r}")
                continue
            mesure["course_secondes"] = courses[morceau.name]["secondes"]
            mesures.append(mesure)
            (dossier / "mesure.json").write_text(json.dumps(mesure, indent=1, ensure_ascii=False, default=str),
                                                  encoding="utf-8")
            g = mesure["global"]
            p = mesure["parite"]
            print(f"[{heure()}] MESURÉ {morceau.name} : {p['pistes_obtenues']} pistes / {p['parties_vraies']} parties, "
                  f"F1 {mesure['transcription']['melodique']['f1']:.2f}, global {g.get('global')}, "
                  f"borne transcription {g.get('borne_transcription')}")
            ecrire(args, reste, mesures, non_mesures, courses, moteur_identite, time.time() - depart)
    ecrire(args, reste, mesures, non_mesures, courses, moteur_identite, time.time() - depart, final=True)
    return 0 if mesures and not non_mesures else (2 if mesures else 1)


def ecrire(args, reste, mesures, non_mesures, courses, moteur_identite, secondes, final=False) -> None:
    agregat = agreger(mesures) if mesures else {}
    texte = tableau(mesures, agregat) if mesures else "aucune mesure"
    if non_mesures:
        texte += "\n\nNON MESURÉS :\n" + "\n".join(f"  {n['morceau']} : {n['raison']}" for n in non_mesures)
    rapport = {
        "format": "vsm-banc-synthetique", "version": 1,
        "provenance": {
            "commit": commit_du_depot(), "date": datetime.now().isoformat(timespec="seconds"),
            "morceaux": str(args.morceaux), "stemsVrais": bool(args.stems_vrais),
            "rendusParalleles": args.rendus_paralleles, "optionsChaine": list(reste),
            "chaineDefauts": "reconstruire.py sans autre option : htdemucs_6s, --parite, tout le parc",
            "moteur": moteur_identite, "secondes": secondes, "termine": final,
            "courses": {nom: {k: (v if k != "commande" or v is None else " ".join(map(str, v)))
                              for k, v in c.items()} for nom, c in courses.items()},
        },
        "agrege": agregat, "morceaux": mesures, "nonMesures": non_mesures,
    }
    (args.sortie / "rapport.json").write_text(json.dumps(rapport, indent=1, ensure_ascii=False, default=str),
                                               encoding="utf-8")
    (args.sortie / "tableau.txt").write_text(texte + "\n", encoding="utf-8")
    if final:
        print()
        print(texte)
        print(f"\nrapport : {args.sortie / 'rapport.json'} — {secondes:.0f} s")


if __name__ == "__main__":
    sys.exit(main())
