#!/usr/bin/env python3
"""LE CRITÈRE DE PHASE DE D12 (docs/CDC-etirement-temporel.md, § 5) : la voix
reportée d'une reconstruction reste en place quand on change le tempo du
projet de +10 %, à condition que son clip SUIVE le tempo.

Protocole, tel qu'il a été exécuté le 04/09/2026 sur sky-parite :
  1. trois projets sont écrits à partir de project.json : `ref` (le tempo
     d'origine, la voix en temps réel), `sansWarp` (tempo × 1,1, la voix en
     temps réel — l'état d'avant D12) et `avecWarp` (tempo × 1,1, la voix
     avec la paire neutre de marqueurs, mode « hauteur conservée ») ;
  2. `vsm-render --stems` rend les trois sur une fenêtre où la voix chante ;
  3. l'enveloppe (rms par 2 ms) de la voix de `ref` est comprimée de 1,1 —
     c'est l'enveloppe ATTENDUE au nouveau tempo — et comparée par
     corrélation croisée à celle des deux autres rendus, sur huit mesures au
     nouveau tempo, puis mesure par mesure : le décalage du pic de
     corrélation est l'erreur d'alignement, en millisecondes.

Pourquoi une corrélation d'enveloppes et non un comptage d'attaques : une
voix chantée a des attaques molles, et un détecteur d'attaques à 5 ms y
fabrique à lui seul des dizaines de millisecondes d'écart (mesuré : 124 ms
« d'erreur » là où la corrélation en donne 10). La corrélation mesure ce que
le critère demande — la voix est-elle LÀ où elle doit être — et rien d'autre.

    analyse/.venv/bin/python analyse/mesure_d12_critere_de_phase.py \\
        reconstruction/travail/sky-parite build/tools/vsm-render dossier-de-travail
"""
import copy
import json
import os
import subprocess
import sys

import numpy as np
import soundfile as sf

PAS = 0.002          # la résolution de la mesure : 2 ms
FACTEUR = 1.1        # +10 % de tempo


def ecrire_projets(source, travail):
    d = json.load(open(os.path.join(source, "project.json"), encoding="utf-8"))
    voix = next(t for t in d["tracks"] if (t.get("audio") or {}).get("file"))
    a = voix["audio"]
    duree = a["frames"] / a["sampleRate"]
    ppq = d["transport"]["ticksPerQuarterNote"]
    bpm0 = d["transport"]["tempoChanges"][0]["bpm"]
    longueur = int(round(duree * ppq * bpm0 / 60.0))
    for nom, bpm, warp in (("ref", bpm0, False), ("sansWarp", bpm0 * FACTEUR, False),
                           ("avecWarp", bpm0 * FACTEUR, True)):
        dossier = os.path.join(travail, nom)
        os.makedirs(dossier, exist_ok=True)
        for lien in ("samples", "midi", "instruments"):
            cible = os.path.join(source, lien)
            if os.path.exists(cible) and not os.path.exists(os.path.join(dossier, lien)):
                os.symlink(cible, os.path.join(dossier, lien))
        p = copy.deepcopy(d)
        p["transport"]["tempoChanges"] = [{"tick": 0, "bpm": bpm}]
        for t in p["tracks"]:
            if (t.get("audio") or {}).get("file"):
                clip = {"sourceStart": 0, "sourceLength": longueur, "start": 0,
                        "length": longueur, "color": "#FF4BB3A6", "name": "Voix"}
                if warp:
                    clip["warp"] = "keepPitch"
                    clip["warpMarkers"] = [{"seconds": 0.0, "tick": 0},
                                           {"seconds": duree, "tick": longueur}]
                t["clips"] = [clip]
        if warp:
            p["version"] = 3
        json.dump(p, open(os.path.join(dossier, "project.json"), "w", encoding="utf-8"),
                  ensure_ascii=False, indent=1)
    return bpm0


def rendre(vsm_render, travail, debut, duree):
    for nom in ("ref", "sansWarp", "avecWarp"):
        dossier = os.path.join(travail, nom)
        subprocess.run([vsm_render, dossier, os.path.join(dossier, "stems"), "--stems",
                        os.path.join(dossier, "stems"), "--start", str(debut),
                        "--duration", str(duree), "--tail", "0", "--quiet"], check=True)


def voix(travail, nom):
    stems = os.path.join(travail, nom, "stems")
    fichier = next(f for f in sorted(os.listdir(stems)) if "Voix" in f or "voix" in f)
    x, sr = sf.read(os.path.join(stems, fichier), always_2d=True)
    return x.mean(axis=1), sr


def enveloppe(v, sr):
    n = int(PAS * sr)
    return np.sqrt(np.array([np.mean(v[i * n:(i + 1) * n] ** 2) for i in range(len(v) // n)]) + 1e-20)


def decalage(mesuree, attendue, debut_s, duree_s, max_ms):
    i0, i1 = int(debut_s / PAS), int((debut_s + duree_s) / PAS)
    x = mesuree[i0:i1] - mesuree[i0:i1].mean()
    n = int(max_ms / 1000.0 / PAS)
    meilleur = None
    for d in range(-n, n + 1):
        j0, j1 = i0 + d, i1 + d
        if j0 < 0 or j1 > len(attendue):
            continue
        y = attendue[j0:j1] - attendue[j0:j1].mean()
        r = float(np.dot(x, y) / (np.linalg.norm(x) * np.linalg.norm(y) + 1e-12))
        if meilleur is None or r > meilleur[1]:
            meilleur = (d * PAS * 1000.0, r)
    return meilleur


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 1
    source, vsm_render, travail = sys.argv[1:4]
    debut = float(sys.argv[4]) if len(sys.argv) > 4 else 130.0     # sky-parite : la voix entre à 151,8 s
    duree = float(sys.argv[5]) if len(sys.argv) > 5 else 60.0
    os.makedirs(travail, exist_ok=True)
    bpm0 = ecrire_projets(source, travail)
    rendre(vsm_render, travail, debut, duree)

    a, sr = voix(travail, "ref")
    b, _ = voix(travail, "sansWarp")
    c, _ = voix(travail, "avecWarp")
    ea, eb, ec = enveloppe(a, sr), enveloppe(b, sr), enveloppe(c, sr)
    t = np.arange(len(ea)) * PAS
    tc = np.arange(len(ec)) * PAS
    attendue = np.interp((debut + tc) * FACTEUR - debut, t, ea, left=0.0, right=0.0)

    # L'entrée de la voix : la première tranche de 2 ms au-dessus du dixième du maximum.
    entree = float(np.argmax(ea > 0.1 * ea.max())) * PAS
    debut_sortie = (debut + entree) / FACTEUR - debut
    huit = 8 * 4 * 60.0 / (bpm0 * FACTEUR)
    print(f"la voix entre à {debut + entree:.2f} s du morceau ; huit mesures à {bpm0 * FACTEUR:.0f} BPM = {huit:.2f} s")
    for nom, sig in (("AVEC suivi du tempo", ec), ("SANS suivi du tempo", eb)):
        d, r = decalage(sig, attendue, debut_sortie, huit, 1500)
        print(f"  {nom} : décalage {d:+.1f} ms sur huit mesures, corrélation {r:.3f}")
    pire = 0.0
    for m in range(8):
        d, r = decalage(ec, attendue, debut_sortie + m * huit / 8, huit / 8, 400)
        pire = max(pire, abs(d))
        print(f"    mesure {m + 1} : {d:+6.1f} ms  (r = {r:.3f})")
    print(f"  pire mesure, avec suivi : {pire:.1f} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
