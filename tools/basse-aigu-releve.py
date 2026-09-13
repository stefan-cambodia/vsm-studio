#!/usr/bin/env python3
"""D272 : relever l'aigu du stem de basse rend-il l'octave au transcripteur ?

    analyse/.venv/bin/python tools/basse-aigu-releve.py 0 6 12 18 24

POURQUOI (D269, D271). La séparation rend une basse dont l'aigu est ATTÉNUÉ, pas
retiré : au-dessus de 300 Hz elle garde 1,7 % de l'énergie de la partie jouée
(contre 25,5 %) mais en conserve la FORME, corrélée à 0,31. Or ce sont ces
partielles qui permettent de trancher une octave, et privé d'elles le
transcripteur choisit bas — 6,9 fois plus souvent que haut, là où sur le stem VRAI
il choisit haut (0,3×).

CE QUI EST MESURÉ. Le stem `bass` est transcrit tel quel (témoin, 0 dB), puis avec
la bande au-dessus de 300 Hz relevée de N décibels. UNE variable, le même code, le
gain en ligne de commande — jamais une constante éditée entre deux passes. Les
notes obtenues sont comparées à la vérité du morceau : bonne hauteur, octave trop
bas, octave trop haut, et la part INVENTÉE, qui est le contrôle. Relever l'aigu
relève aussi les fuites des autres instruments : sans ce contrôle, on gagnerait des
octaves en perdant tout le reste, et le chiffre de bonne hauteur ne le dirait pas.
"""
from __future__ import annotations

import argparse
import json
import os
import tempfile
from pathlib import Path

import numpy as np
import soundfile as sf

RACINE = Path(__file__).resolve().parent.parent
LOT = Path(os.environ.get("VSM_LOT", str(RACINE / "reconstruction/travail/r1f-13sep")))
SRC = RACINE / "reconstruction/travail/s1-sec"
TOLERANCE = 0.06
COUPURE = 300.0


def passe_haut(x: np.ndarray, sr: float, coupure: float) -> np.ndarray:
    """D280 : retire au stem ce que la séparation y a AJOUTÉ sous la fondamentale.

    Mesuré le 13/09 sur neuf morceaux : la partie de basse jouée met 5,2 % de son
    énergie sous 80 Hz (médiane), le stem séparé en met **49,8 %** — et jusqu'à
    99 % sur un morceau dont la partie vraie n'en portait que 0,2 %. Un filtre ne
    peut pas faire cela ; le modèle reconstruit sa sortie en y plaçant du grave
    que la source n'a pas.
    """
    if coupure <= 0.0:
        return x
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    return np.fft.irfft(np.where(f >= coupure, X, 0.0), n=len(x))


def releve(x: np.ndarray, sr: float, gain_db: float) -> np.ndarray:
    """Relève la bande au-dessus de COUPURE de `gain_db`, laisse le grave intact."""
    if gain_db == 0.0:
        return x
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    facteur = np.where(f >= COUPURE, 10.0 ** (gain_db / 20.0), 1.0)
    y = np.fft.irfft(X * facteur, n=len(x))
    crete = float(np.max(np.abs(y)))
    # NORMALISÉ SI ÇA DÉBORDE, et c'est dit : un signal écrêté ne transcrit pas ce
    # qu'on croit, et l'écrêtage se confondrait avec l'effet du relevé.
    return y / crete * 0.99 if crete > 0.99 else y


def coupure_adaptee(evenements, facteur: float = 0.75, centile: float = 20.0) -> float:
    """Où couper le grave de CE morceau, d'après ce que la transcription y trouve.

    D281 : une coupure FIXE a été réfutée (D280) — réglée à 60 Hz sur la moitié A,
    elle fait perdre 2,3 points sur la moitié B, dont les basses jouent plus haut.
    La bonne coupure dépend du REGISTRE du morceau, et le morceau sait le dire :
    on transcrit une première fois sans filtre, et l'on coupe SOUS ce qu'on a
    trouvé.

    LE CENTILE PLUTÔT QUE LE MINIMUM, et le facteur 0,75 plutôt que 0,5 :
      * la note la plus grave écrite est justement celle qu'on soupçonne d'être
        une octave trop bas — s'en servir pour placer la coupure la protégerait ;
        le 20ᵉ centile résiste à quelques fausses notes sans monter trop haut ;
      * 0,75 × f0 tombe entre la fondamentale (1,0) et son octave inférieure
        (0,5), donc retire l'une sans toucher l'autre. C'est le seul point qui
        sépare les deux, et il n'a pas été choisi par balayage.
    """
    hauteurs = sorted(int(e[2]) for e in evenements)
    if not hauteurs:
        return 0.0
    note = hauteurs[min(len(hauteurs) - 1, int(len(hauteurs) * centile / 100.0))]
    return facteur * 440.0 * 2.0 ** ((note - 69) / 12.0)


def mesurer(gain_db: float, passe_haut_hz: float = 0.0, adaptee: bool = False) -> dict[str, int]:
    from basic_pitch import ICASSP_2022_MODEL_PATH
    from basic_pitch.inference import predict

    c = {"juste": 0, "bas": 0, "haut": 0, "autre": 0, "inventee": 0, "total": 0}
    # VSM_MORCEAUX : restreindre à une MOITIÉ du corpus, pour régler sur l'une et
    # valider sur l'autre.
    #
    # POURQUOI IL LE FAUT (13/09/2026). Il n'existe pas de « lot témoin » pour
    # cette mesure : `r1-sec-banc`, `r1-prod-banc` et `r1f-13sep` séparent tous
    # les MÊMES dix morceaux de `s1-sec`, et demucs est déterministe — leurs stems
    # de basse sont IDENTIQUES au md5. Valider un réglage sur un autre lot, c'est
    # le valider sur les mêmes données ; j'ai failli publier exactement cela.
    choisis = [m.strip() for m in os.environ.get("VSM_MORCEAUX", "").split(",") if m.strip()]
    for d in sorted(LOT.glob("morceau-*")):
        if choisis and d.name not in choisis:
            continue
        stem = d / "stems-separes" / "stems" / "bass.wav"
        verite = SRC / d.name / "verite.json"
        if not (stem.is_file() and verite.is_file()):
            continue
        v = json.loads(verite.read_text(encoding="utf-8"))
        vraies = sorted((float(n[2]), int(n[0])) for p in v.get("parties", [])
                        if p.get("role") != "batterie" for n in p.get("notes", []))
        x, sr = sf.read(str(stem), always_2d=True)
        brut = releve(x.mean(axis=1), float(sr), gain_db)
        coupure = passe_haut_hz
        if adaptee:
            # UNE PREMIÈRE TRANSCRIPTION SANS FILTRE dit où est la basse de ce
            # morceau ; la coupure s'en déduit, puis on transcrit pour de bon.
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=True) as f0:
                sf.write(f0.name, brut, int(sr))
                _, _, sonde = predict(f0.name, model_or_model_path=ICASSP_2022_MODEL_PATH)
            coupure = coupure_adaptee(sonde)
        y = passe_haut(brut, float(sr), coupure)
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=True) as f:
            sf.write(f.name, y, int(sr))
            _, _, evts = predict(f.name, model_or_model_path=ICASSP_2022_MODEL_PATH)
        for e in evts:
            t, h = float(e[0]), int(e[2])
            c["total"] += 1
            proches = [hv for tv, hv in vraies if abs(tv - t) <= TOLERANCE]
            if not proches:
                c["inventee"] += 1
            elif h in proches:
                c["juste"] += 1
            elif any(hv - h in (12, 24) for hv in proches):
                c["bas"] += 1
            elif any(hv - h in (-12, -24) for hv in proches):
                c["haut"] += 1
            else:
                c["autre"] += 1
    return c


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("gains", nargs="+", type=float,
                   help="les relevés en dB ; le PREMIER est le témoin (0 = la chaîne d'aujourd'hui)")
    p.add_argument("--adaptee", action="store_true",
                   help="D281 : coupure ADAPTÉE au morceau — une première transcription sans "
                        "filtre dit où joue sa basse, et l'on coupe sous le 20e centile de ce "
                        "qu'elle trouve, à 0,75 fois sa fréquence. Une coupure FIXE a été réfutée : "
                        "réglée sur la moitié A du corpus, elle fait perdre sur la moitié B")
    p.add_argument("--passe-haut", nargs="+", type=float, default=None, metavar="HZ",
                   help="D280 : au lieu de relever l'aigu, RETIRER le grave sous ces coupures "
                        "(0 = le témoin). La séparation empile de l'énergie sous la fondamentale "
                        "— 49,8 %% contre 5,2 %% dans la partie jouée — et D278 a montré qu'une "
                        "composante grave forte fait descendre le transcripteur d'une octave")
    a = p.parse_args()
    if a.adaptee:
        reglages = [(0.0, 0.0), (0.0, -1.0)]   # témoin, puis coupure adaptée
    elif a.passe_haut:
        reglages = [(0.0, hz) for hz in a.passe_haut]
    else:
        reglages = [(g, 0.0) for g in a.gains]
    entete = "passe-haut" if a.passe_haut else "gain"
    print("stem « bass » séparé, "
          + ("GRAVE RETIRÉ sous la coupure" if a.passe_haut
             else f"relevé au-dessus de {COUPURE:.0f} Hz")
          + f", tolérance {TOLERANCE * 1000:.0f} ms")
    print(f"{entete:>10} {'écrites':>8} {'justes':>8} {'8ve bas':>8} {'8ve haut':>9} "
          f"{'bas/haut':>9} {'bonne h.':>9} {'inventées':>10}")
    for i, (g, hz) in enumerate(reglages):
        c = mesurer(g, max(0.0, hz), adaptee=(hz < 0.0))
        apparie = c["juste"] + c["bas"] + c["haut"] + c["autre"]
        rapport = f"{c['bas'] / c['haut']:.1f}x" if c["haut"] else "—"
        bonne = f"{100 * c['juste'] / apparie:.1f}%" if apparie else "—"
        inv = f"{100 * c['inventee'] / c['total']:.1f}%" if c["total"] else "—"
        marque = "  (témoin)" if i == 0 else ""
        valeur = ("adaptée" if hz < 0.0 else
                  f"{hz:8.0f}Hz" if (a.passe_haut or a.adaptee) else f"{g:6.0f}dB")
        print(f"{valeur:>10} {c['total']:8d} {c['juste']:8d} {c['bas']:8d} {c['haut']:9d} "
              f"{rapport:>9} {bonne:>9} {inv:>10}{marque}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
