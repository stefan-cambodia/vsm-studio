#!/usr/bin/env python3
"""Le panneau d'effets écrit-il la valeur que l'effet TIENT ?

    python3 tools/effets-valeurs.py [chemin/du/binaire]

LA RÈGLE GARDÉE (28/09/2026, D474). Chaque réglage d'un effet d'insert, tout juste
posé sur une piste, s'affiche à SA valeur d'usine — celle que l'effet déclare
dans son en-tête (`parameterList_`) et qu'il joue. Avant D474, le panneau le
réglait sur une grille de `(max − min) / 1000` comptée depuis `min` et y posait
la valeur sans notification : « Cutoff 1998.02 Hz » pour 2 000 Hz, « Time
350.825 ms » pour 350, « Ratio 2.995 » pour 3.

COMMENT. Les seize effets de `EffectFactory::available()` sont posés chacun sur
la piste de démarrage (`VSM_MENU_CONTEXTE=ajout-effet:<nom>`), un HOME neuf par
course (D318) ; le relevé `VSM_VALEUR` donne le texte de chaque réglage. Les
valeurs d'usine se lisent dans les en-têtes (`audio/include/vsm/audio/effect/`),
l'appariement classe ↔ effet dans `EffectFactory::create`.

CE QUI FAIT UN ÉCART (une unité « % » est une part de −1 à 1 écrite × 100, D475) :
  * un SÉLECTEUR (D473) qui n'écrit pas le libellé de sa position d'usine ;
  * un nombre qui ne désigne pas la valeur d'usine À LA PRÉCISION DE SON ÉCRITURE
    (« 2000 Hz » tient 2000 à ±0,5 ; « 0.30 » tient 0,3 à ±0,005 ; « 0.34960 »
    ne tient PAS 0,35 : il en écrit cinq décimales et en rate la quatrième) ;
  * une unité déclarée et absente du texte ;
  * une case périmée (D460), ou un réglage déclaré et jamais relevé.

Rend 0 si aucun écart, 1 sinon, 2 si le binaire ou la lecture des sources manque
(la garde ne mesure alors rien, et le dit).
"""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
BINAIRE = RACINE / "build/app/VintageSynthMidiStudio_artefacts/RelWithDebInfo/Vintage Synth MIDI Studio"
USINE = RACINE / "audio/src/effect/EffectFactory.cpp"
ENTETES = RACINE / "audio/include/vsm/audio/effect"

# `{kMix, "Mix", 0.0f, 1.0f, 0.3f, ""}` et, depuis D473, un cinquième champ de
# positions `{"LP", "HP"}` éventuellement sur deux lignes. Le maximum peut être
# une constante nommée (`kMaxDelayMs`).
PARAMETRE = re.compile(
    r'\{\s*k\w+\s*,\s*"([^"]+)"\s*,\s*(-?[\d.]+)f?\s*,\s*(-?[\w.]+?)f?\s*,\s*(-?[\d.]+)f?\s*,\s*"([^"]*)"'
    r'(?:\s*,\s*//[^\n]*\n\s*)?(?:\s*,\s*\{([^}]*)\})?(?:\s*,\s*(true|false))?\s*\}', re.S)


def lire_usine() -> tuple[dict[str, str], dict[str, str]]:
    texte = USINE.read_text(encoding="utf-8")
    noms = dict(re.findall(r'\{"(\w+)",\s*"([^"]+)"\}', texte))
    classes = dict(re.findall(r'if \(id == "(\w+)"\) return std::make_unique<(\w+)>', texte))
    return noms, classes


def lire_parametres() -> dict[str, list[tuple[str, float, float, str, list[str]]]]:
    par_classe: dict[str, list[tuple[str, float, float, str, list[str]]]] = {}
    for entete in sorted(ENTETES.glob("*.h")):
        texte = re.sub(r"//[^\n]*", "", entete.read_text(encoding="utf-8"))
        for m in re.finditer(r"class (\w+) : public IAudioEffect", texte):
            debut = texte.find("parameterList_ = {", m.end())
            if debut < 0:
                continue
            fin = texte.find("};", debut)
            bloc = texte[debut:fin + 2]
            parametres = []
            for p in PARAMETRE.finditer(bloc):
                nom, mini, _maxi, defaut, unite, choix, _entier = p.groups()
                positions = re.findall(r'"([^"]*)"', choix) if choix else []
                parametres.append((nom, float(mini), float(defaut), unite, positions))
            par_classe[m.group(1)] = parametres
    return par_classe


def juger(texte: str, mini: float, defaut: float, unite: str, positions: list[str]) -> str | None:
    if positions:
        attendu = positions[round(defaut - mini)]
        return None if texte == attendu else f"« {texte} » au lieu de « {attendu} »"
    nombre = re.search(r"-?\d+(?:\.(\d+))?", texte)
    if nombre is None:
        return f"« {texte} » : aucun nombre"
    # L'UNITÉ D'ABORD : sans elle, on ne sait pas lire le nombre (« 0.30 » d'une
    # part non écrite en pour cent se jugerait comme 0,3 %, et la garde dirait
    # « ne désigne pas » là où la faute est l'unité absente).
    if unite and not texte.endswith(" " + unite):
        return f"« {texte} » sans son unité « {unite} »"
    decimales = len(nombre.group(1) or "")
    tolerance = 0.5 * 10 ** (-decimales) + 1e-9
    lu = float(nombre.group(0))
    # D475 : « % » est une part de −1 à 1 écrite × 100 — « 30 % » désigne 0,3.
    if unite == "%":
        lu, tolerance = lu / 100.0, tolerance / 100.0
    if abs(lu - defaut) > tolerance:
        return f"« {texte} » ne désigne pas {defaut:g} (précision de l'écriture : ±{tolerance:g})"
    return None


def main() -> int:
    binaire = Path(sys.argv[1]) if len(sys.argv) > 1 else BINAIRE
    if not binaire.exists():
        print(f"REFUS : {binaire} absent — compiler d'abord")
        return 2
    noms, classes = lire_usine()
    parametres = lire_parametres()
    effets = [(identifiant, noms[identifiant], parametres.get(classes.get(identifiant, ""), []))
              for identifiant in noms]
    vides = [nom for _, nom, p in effets if not p]
    if len(effets) < 16 or vides:
        print(f"REFUS : {len(effets)} effet(s) lus, sans paramètres lus : {vides}")
        return 2
    print("=== D474 : le panneau d'effets écrit la valeur que l'effet tient ===")
    ecarts = 0
    reglages = 0
    with tempfile.TemporaryDirectory(prefix="vsm-effets-valeurs-") as brouillon:
        for identifiant, nom, attendus in effets:
            maison = tempfile.mkdtemp(dir=brouillon)
            env = dict(os.environ, HOME=maison, VSM_VUE="effets,agrandir:bas",
                       VSM_MENU_CONTEXTE=f"ajout-effet:{nom}", VSM_TEXTES_LISTE="1",
                       VSM_TAILLE="1600x1000", VSM_DELAI="800",
                       VSM_CAPTURE=str(Path(brouillon) / f"{identifiant}.png"))
            sortie = subprocess.run([str(binaire)], env=env, capture_output=True, text=True,
                                    errors="replace", timeout=60).stderr
            if f"« {nom} » exécutée" not in sortie:
                print(f"  RATÉ {nom:<20} l'effet n'a pas été posé (journal : "
                      f"{[ligne for ligne in sortie.splitlines() if 'MENU_CONTEXTE' in ligne][:1]})")
                ecarts += 1
                continue
            lus = {}
            for ligne in sortie.splitlines():
                m = re.match(r"VSM_VALEUR : effet\.(.+?) : (.*?)(?: — CASE « .* » ≠ FONCTION.*)?$", ligne)
                if m:
                    lus[m.group(1)] = m.group(2)
            perimees = re.search(r"VSM_VALEUR : (\d+) case\(s\) périmée\(s\)", sortie)
            fautes = []
            if perimees is None or perimees.group(1) != "0":
                fautes.append(f"cases périmées : {perimees.group(1) if perimees else '?'}")
            for pnom, mini, defaut, unite, positions in attendus:
                reglages += 1
                if pnom not in lus:
                    fautes.append(f"{pnom} jamais relevé")
                    continue
                faute = juger(lus[pnom], mini, defaut, unite, positions)
                if faute:
                    fautes.append(f"{pnom} {faute}")
            if fautes:
                ecarts += len(fautes)
                print(f"  RATÉ {nom:<20} " + " ; ".join(fautes))
            else:
                print(f"  OK   {nom:<20} {len(attendus)} réglage(s) : "
                      + ", ".join(f"{p[0]} {lus[p[0]]}" for p in attendus))
    print(f"--- {ecarts} écart(s) sur {reglages} réglage(s) de {len(effets)} effets")
    return 1 if ecarts else 0


if __name__ == "__main__":
    sys.exit(main())
