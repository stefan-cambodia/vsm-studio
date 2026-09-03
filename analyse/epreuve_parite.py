#!/usr/bin/env python3
"""L'ÉPREUVE DE PARITÉ : un morceau dont on CONNAÎT les parties.

Toute la mesure du chantier multipiste porte sur des originaux dont personne
ne sait la vérité — on compare des pistes à un nombre de parties supposé. Ce
script FABRIQUE un morceau court avec sa vérité écrite, fournit ses stems
directement (la variable est la CHAÎNE, pas la séparation), fait tourner la
chaîne avec `--parite`, et compte les pistes obtenues contre les parties
réelles, stem par stem.

Le morceau (32 s, 120 bpm, Am-F-C-G) :

  stem      parties réelles                      registres (MIDI)
  bass      1  basse                              29-36
  other     3  grave (dyades), médium (arpèges),  36-50 · 60-72 · 84-96
               aigu (mélodie) — registres DISJOINTS
  drums     3  kick, caisse claire, charleston
  vocals    2  voix de tête (centre), chœurs (larges, gauche/droite)

La première version de cette épreuve (03/09/2026) vivait dans un dossier
temporaire et a été perdue ; elle avait mesuré 6 pistes pour 9 parties. Cette
version est suivie par le dépôt pour que la mesure se rejoue.

Usage :
  analyse/.venv/bin/python analyse/epreuve_parite.py --sortie reconstruction/travail/epreuve
      [--fabriquer-seulement] [--budget-piste 8] [-- options supplémentaires de reconstruire.py]
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

SR = 44100
BPM = 120.0
BATTEMENT = 60.0 / BPM
MESURES = 16
DUREE = MESURES * 4 * BATTEMENT  # 32 s

# Am F C G, en fondamentales MIDI (octave 2)
ACCORDS = [(45, [0, 3, 7]), (41, [0, 4, 7]), (48, [0, 4, 7]), (43, [0, 4, 7])]

VERITE = {
    "bass": ["basse"],
    "other": ["grave", "médium", "aigu"],
    "drums": ["kick", "caisse", "charleston"],
    "vocals": ["tête", "chœurs"],
}

# LA VARIANTE « CHORALE » : l'excès inverse de la parité. `other` y est UN
# SEUL instrument (même timbre) qui tient quatre voix serrées sur trois
# octaves — un piano d'accompagnement. Au sens du seuil du fourre-tout
# (polyphonie ≥ 3 ET ambitus ≥ 36), c'est un fourre-tout ; pour l'oreille,
# c'est une partie. La parité, c'est UNE piste ici, pas quatre.
VERITE_CHORALE = {
    "bass": ["basse"],
    "other": ["chorale"],
    "drums": ["kick", "caisse", "charleston"],
    "vocals": ["tête", "chœurs"],
}


def _hz(midi: float) -> float:
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def _enveloppe(n: int, attaque: float, chute: float, tenue: float, relache: float) -> np.ndarray:
    t = np.arange(n) / SR
    duree = n / SR
    env = np.ones(n)
    a = max(1, int(attaque * SR))
    env[:a] = np.linspace(0.0, 1.0, a)
    d = int(chute * SR)
    if d > 0 and a + d < n:
        env[a:a + d] = np.linspace(1.0, tenue, d)
        env[a + d:] = tenue
    r = int(relache * SR)
    if r > 0:
        r = min(r, n)
        env[n - r:] *= np.linspace(1.0, 0.0, r)
    return env * (t < duree)


def _scie(freq: float, n: int, harmoniques: int = 24) -> np.ndarray:
    """Scie additive, limitée en bande : ni repliement ni surprise à l'aigu."""
    t = np.arange(n) / SR
    out = np.zeros(n)
    for k in range(1, harmoniques + 1):
        if freq * k >= SR / 2.2:
            break
        out += np.sin(2 * np.pi * freq * k * t) / k
    return out * (2 / np.pi)


def _passe_bas(x: np.ndarray, coupure: float) -> np.ndarray:
    from scipy.signal import lfilter
    a = np.exp(-2 * np.pi * coupure / SR)
    return lfilter([1 - a], [1, -a], x)


def _poser(piste: np.ndarray, debut: float, son: np.ndarray, gain: float = 1.0) -> None:
    i = int(debut * SR)
    fin = min(len(piste), i + len(son))
    if fin > i:
        piste[i:fin] += gain * son[:fin - i]


def _note(freq: float, duree: float, timbre: str) -> np.ndarray:
    n = int(duree * SR)
    if timbre == "basse":
        son = _passe_bas(_scie(freq, n, 16), 600.0) * _enveloppe(n, 0.005, 0.15, 0.6, 0.03)
    elif timbre == "grave":
        son = _passe_bas(_scie(freq, n, 20), 1200.0) * _enveloppe(n, 0.08, 0.3, 0.8, 0.15)
    elif timbre == "medium":
        t = np.arange(n) / SR
        son = (np.sign(np.sin(2 * np.pi * freq * t)) * 0.3 + _scie(freq, n, 10) * 0.7)
        son = _passe_bas(son, 3000.0) * _enveloppe(n, 0.003, 0.12, 0.35, 0.02)
    elif timbre == "aigu":
        t = np.arange(n) / SR
        son = (np.sin(2 * np.pi * freq * t) + 0.35 * np.sin(4 * np.pi * freq * t)
               + 0.12 * np.sin(6 * np.pi * freq * t)) * _enveloppe(n, 0.01, 0.2, 0.5, 0.05)
    else:
        raise ValueError(timbre)
    return son


def _voix(freq: float, duree: float) -> np.ndarray:
    """Une voix de synthèse : scie vibrée, deux formants (résonances)."""
    n = int(duree * SR)
    t = np.arange(n) / SR
    vib = 1.0 + 0.006 * np.sin(2 * np.pi * 5.5 * t)
    phase = np.cumsum(2 * np.pi * freq * vib / SR)
    brut = np.zeros(n)
    for k in range(1, 30):
        if freq * k >= SR / 2.2:
            break
        brut += np.sin(k * phase) / k
    from scipy.signal import iirpeak, lfilter
    son = np.zeros(n)
    for formant, q in ((650.0, 6.0), (1150.0, 8.0), (2600.0, 10.0)):
        b, a = iirpeak(formant / (SR / 2), q)
        son += lfilter(b, a, brut)
    return son * _enveloppe(n, 0.04, 0.1, 0.85, 0.08)


def _frappe(sorte: str) -> np.ndarray:
    rng = np.random.default_rng({"kick": 1, "caisse": 2, "charleston": 3}[sorte])
    if sorte == "kick":
        n = int(0.22 * SR)
        t = np.arange(n) / SR
        f = 45.0 + 110.0 * np.exp(-t / 0.035)
        return np.sin(np.cumsum(2 * np.pi * f / SR)) * np.exp(-t / 0.08) * 1.0
    if sorte == "caisse":
        n = int(0.18 * SR)
        t = np.arange(n) / SR
        bruit = rng.standard_normal(n)
        from scipy.signal import butter, lfilter
        b, a = butter(2, [1500 / (SR / 2), 7000 / (SR / 2)], btype="band")
        bruit = lfilter(b, a, bruit) * np.exp(-t / 0.06)
        corps = np.sin(2 * np.pi * 190.0 * t) * np.exp(-t / 0.05)
        return 0.75 * bruit + 0.45 * corps
    if sorte == "charleston":
        n = int(0.05 * SR)
        t = np.arange(n) / SR
        bruit = rng.standard_normal(n)
        from scipy.signal import butter, lfilter
        b, a = butter(2, 6500 / (SR / 2), btype="high")
        return lfilter(b, a, bruit) * np.exp(-t / 0.012) * 0.6
    raise ValueError(sorte)


def fabriquer(dossier: Path, variante: str = "registres") -> dict:
    dossier.mkdir(parents=True, exist_ok=True)
    n = int(DUREE * SR) + int(1.0 * SR)  # une seconde de queue
    bass = np.zeros(n)
    grave = np.zeros(n)
    medium = np.zeros(n)
    aigu = np.zeros(n)
    drums = np.zeros(n)
    voix_tete = np.zeros(n)
    voix_g = np.zeros(n)
    voix_d = np.zeros(n)
    notes_verite = {"bass": [], "grave": [], "medium": [], "aigu": [], "tete": [], "choeurs": []}

    melodie_aigu = [0, 2, 4, 2, 7, 4, 2, 0]      # degrés sur la gamme de la mineur, octave 6
    gamme = [0, 2, 3, 5, 7, 8, 10, 12]
    phrase_tete = [(0, 1.0), (2, 0.5), (3, 0.5), (4, 1.0), (2, 1.0), (0, 2.0)]

    for mesure in range(MESURES):
        t0 = mesure * 4 * BATTEMENT
        fond, intervalles = ACCORDS[mesure % 4]

        # basse : croches, fondamentale puis quinte, à l'octave 1
        for croche in range(8):
            midi = fond - 12 + (7 if croche in (3, 7) else 0)
            _poser(bass, t0 + croche * BATTEMENT / 2, _note(_hz(midi), BATTEMENT / 2 * 0.95, "basse"))
            notes_verite["bass"].append((midi, t0 + croche * BATTEMENT / 2))

        if variante == "chorale":
            # UN instrument, quatre voix serrées (basse, ténor, alto, soprano)
            # en blanches, même timbre partout, registres qui se TOUCHENT :
            # aucun vide, trois octaves, polyphonie 4.
            for blanche in range(2):
                t = t0 + blanche * 2 * BATTEMENT
                voix = [fond - 9, fond + 3 + intervalles[1], fond + 12 + 7, fond + 24 + intervalles[1]]
                if blanche == 1:
                    voix = [fond - 9 + 7 - 12 if fond - 9 + 7 - 12 >= 36 else fond - 9,
                            fond + 3 + 7, fond + 12 + intervalles[1] + 12 - 12, fond + 24 + 7]
                for midi in voix:
                    _poser(grave, t, _note(_hz(midi), 2 * BATTEMENT * 0.95, "grave"), 0.5)
                    notes_verite["grave"].append((midi, t))
        else:
            # grave : une dyade tenue par mesure (fondamentale + quinte), registre 36-50
            for iv in (0, 7):
                midi = fond - 9 + iv if fond - 9 + iv >= 36 else fond + 3 + iv
                _poser(grave, t0, _note(_hz(midi), 4 * BATTEMENT * 0.98, "grave"), 0.6)
                notes_verite["grave"].append((midi, t0))

            # médium : arpèges en croches sur les notes de l'accord, registre 60-72
            for croche in range(8):
                midi = fond + 12 + intervalles[croche % 3] + (12 if croche >= 6 else 0)
                while midi < 60:
                    midi += 12
                while midi > 72:
                    midi -= 12
                _poser(medium, t0 + croche * BATTEMENT / 2, _note(_hz(midi), BATTEMENT / 2 * 0.9, "medium"), 0.7)
                notes_verite["medium"].append((midi, t0 + croche * BATTEMENT / 2))

            # aigu : mélodie en noires, registre 84-96
            for noire in range(4):
                degre = melodie_aigu[(mesure * 4 + noire) % 8]
                midi = 84 + gamme[degre % 8]
                _poser(aigu, t0 + noire * BATTEMENT, _note(_hz(midi), BATTEMENT * 0.9, "aigu"), 0.5)
                notes_verite["aigu"].append((midi, t0 + noire * BATTEMENT))

        # batterie : kick 1 et 3 (et le « et » de 4 une mesure sur deux), caisse 2 et 4,
        # charleston à chaque croche
        for temps in (0, 2):
            _poser(drums, t0 + temps * BATTEMENT, _frappe("kick"))
        if mesure % 2 == 1:
            _poser(drums, t0 + 3.5 * BATTEMENT, _frappe("kick"), 0.8)
        for temps in (1, 3):
            _poser(drums, t0 + temps * BATTEMENT, _frappe("caisse"))
        for croche in range(8):
            _poser(drums, t0 + croche * BATTEMENT / 2, _frappe("charleston"), 1.0 if croche % 2 == 0 else 0.6)

        # voix : une phrase toutes les deux mesures (tête au centre, chœurs larges)
        if mesure % 2 == 0:
            t = t0
            for degre, longueur in phrase_tete:
                midi = 62 + gamme[degre]
                duree = longueur * BATTEMENT
                _poser(voix_tete, t, _voix(_hz(midi), duree * 0.95))
                notes_verite["tete"].append((midi, t))
                # chœurs : tierce et quinte au-dessus, l'une à gauche, l'autre à droite,
                # avec un léger décalage — c'est la LARGEUR qui les distingue
                _poser(voix_g, t + 0.012, _voix(_hz(midi + 3), duree * 0.95), 0.55)
                _poser(voix_d, t + 0.018, _voix(_hz(midi + 7), duree * 0.95), 0.55)
                notes_verite["choeurs"].append((midi + 3, t))
                notes_verite["choeurs"].append((midi + 7, t))
                t += duree

    def normaliser(x, cible):
        return x * (cible / (np.sqrt(np.mean(x ** 2)) + 1e-12))

    bass = normaliser(bass, 0.10)
    if variante == "chorale":
        other = normaliser(grave, 0.09)
    else:
        other = normaliser(grave, 0.06) + normaliser(medium, 0.05) + normaliser(aigu, 0.035)
    drums = normaliser(drums, 0.12)
    tete = normaliser(voix_tete, 0.07)
    g = normaliser(voix_g, 0.04)
    d = normaliser(voix_d, 0.04)
    vocals = np.stack([tete + g, tete + d], axis=1)  # tête au centre, chœurs à gauche/droite

    melange = np.stack([bass + other + drums, bass + other + drums], axis=1) + vocals
    pic = float(np.abs(melange).max())
    gain = 0.9 / pic if pic > 0.9 else 1.0
    import soundfile as sf
    stems = dossier / "stems"
    stems.mkdir(exist_ok=True)
    sf.write(str(stems / "bass.wav"), (bass * gain).astype(np.float32), SR, subtype="FLOAT")
    sf.write(str(stems / "other.wav"), (other * gain).astype(np.float32), SR, subtype="FLOAT")
    sf.write(str(stems / "drums.wav"), (drums * gain).astype(np.float32), SR, subtype="FLOAT")
    sf.write(str(stems / "vocals.wav"), (vocals * gain).astype(np.float32), SR, subtype="FLOAT")
    sf.write(str(dossier / "original.wav"), (melange * gain).astype(np.float32), SR, subtype="FLOAT")
    # les couches de `other` à part, pour vérifier la transcription couche par couche
    for nom, couche in (("other-grave", grave), ("other-medium", medium), ("other-aigu", aigu)):
        sf.write(str(dossier / f"{nom}.wav"), (normaliser(couche, 0.05) * gain).astype(np.float32), SR,
                 subtype="FLOAT")

    parties = VERITE_CHORALE if variante == "chorale" else VERITE
    verite = {
        "format": "vsm-epreuve-parite", "version": 1,
        "variante": variante,
        "duree": DUREE, "bpm": BPM,
        "parties": parties,
        "total": sum(len(v) for v in parties.values()),
        "registres": {"bass": [29, 36], "grave": [36, 50], "medium": [60, 72], "aigu": [84, 96],
                      "tete": [62, 74], "choeurs": [65, 81]},
        "notes": {k: len(v) for k, v in notes_verite.items()},
        "parts_energie": {
            "bass": float(np.sum(bass ** 2)), "other": float(np.sum(other ** 2)),
            "drums": float(np.sum(drums ** 2)), "vocals": float(np.sum(vocals ** 2)) / 2,
        },
    }
    (dossier / "verite.json").write_text(json.dumps(verite, indent=1, ensure_ascii=False), encoding="utf-8")
    return verite


def compter(sortie: Path, verite: dict) -> dict:
    projet = json.loads((sortie / "project.json").read_text(encoding="utf-8"))
    rapport = json.loads((sortie / "rapport.json").read_text(encoding="utf-8"))
    par_stem = {"bass": [], "other": [], "drums": [], "vocals": []}
    for piste in projet["tracks"]:
        nom = piste["name"]
        if nom.startswith("bass"):
            par_stem["bass"].append(nom)
        elif nom.startswith("other"):
            par_stem["other"].append(nom)
        elif nom.startswith("Batterie"):
            par_stem["drums"].append(nom)
        elif nom.startswith("Voix"):
            par_stem["vocals"].append(nom)
    lignes = []
    for stem, parties in verite["parties"].items():
        lignes.append({"stem": stem, "attendu": len(parties), "obtenu": len(par_stem[stem]),
                       "parties": parties, "pistes": par_stem[stem]})
    return {"lignes": lignes, "pistes": len(projet["tracks"]), "parties": verite["total"],
            "distance": rapport.get("globalDistance"), "metrique": rapport.get("metric")}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sortie", type=Path, required=True, help="dossier de l'épreuve (morceau + course)")
    ap.add_argument("--fabriquer-seulement", action="store_true")
    ap.add_argument("--budget-piste", type=int, default=8)
    ap.add_argument("--machines", default="vsm.minimoog,vsm.juno106,vsm.tb303",
                    help="candidates mélodiques : l'épreuve mesure la parité, pas le choix de machine")
    ap.add_argument("--rendus-paralleles", type=int, default=4)
    ap.add_argument("--sans-parite", action="store_true", help="le témoin : sans --parite")
    ap.add_argument("--nom", default=None, help="nom de la course (défaut : parite ou temoin)")
    ap.add_argument("--variante", default="registres", choices=("registres", "chorale"),
                    help="registres : trois parties disjointes dans other (9 parties) ; "
                         "chorale : UN instrument à quatre voix serrées dans other (7 parties)")
    args, reste = ap.parse_known_args()  # le reste va tel quel à reconstruire.py

    morceau = args.sortie / ("morceau" if args.variante == "registres" else f"morceau-{args.variante}")
    verite = fabriquer(morceau, args.variante)
    print(f"morceau fabriqué : {morceau} — {verite['total']} parties, "
          + ", ".join(f"{k} {v}" for k, v in verite["notes"].items()) + " notes")
    if args.fabriquer_seulement:
        return 0

    course = args.sortie / (args.nom or ("temoin" if args.sans_parite else "parite"))
    commande = [sys.executable, "-u", str(Path(__file__).resolve().parent / "reconstruire.py"),
                str(morceau / "original.wav"), "--sortie", str(course),
                "--stems", str(morceau / "stems"),
                "--budget-piste", str(args.budget_piste), "--axes-piste", "4",
                "--tours-verdict", "1", "--machines-au-melange", "2",
                "--rendus-paralleles", str(args.rendus_paralleles),
                "--machines", args.machines] + ([] if args.sans_parite else ["--parite"]) + reste
    debut = time.time()
    journal = args.sortie / (course.name + ".log")
    with journal.open("w", encoding="utf-8") as f:
        code = subprocess.run(commande, stdout=f, stderr=subprocess.STDOUT).returncode
    print(f"chaîne : code {code} en {time.time() - debut:.0f} s — journal {journal}")
    if code != 0:
        print(journal.read_text(encoding="utf-8")[-3000:])
        return code

    bilan = compter(course, verite)
    print()
    print(f"{'stem':8s} {'attendu':>7s} {'obtenu':>7s}  pistes")
    for l in bilan["lignes"]:
        print(f"{l['stem']:8s} {l['attendu']:7d} {l['obtenu']:7d}  {', '.join(l['pistes'])}")
    print(f"{'TOTAL':8s} {bilan['parties']:7d} {bilan['pistes']:7d}  distance {bilan['distance']:.4f} "
          f"({bilan['metrique']})")
    (args.sortie / (course.name + "-bilan.json")).write_text(json.dumps(bilan, indent=1, ensure_ascii=False),
                                                              encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
