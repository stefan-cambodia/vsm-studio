#!/usr/bin/env python3
"""D272, D280, D281 : le stem de basse séparé transcrit-il à la bonne octave, et qu'est-ce qui la rend ?

Trois modes, UN par lancement — ils s'excluent, et le témoin est toujours le
stem tel quel (0 dB, aucune coupure), transcrit par le MÊME code :

    analyse/.venv/bin/python tools/basse-aigu-releve.py 0 6 12 18 24          # D272 : relever l'aigu
    analyse/.venv/bin/python tools/basse-aigu-releve.py --passe-haut 0 40 60 80  # D280 : coupure FIXE
    analyse/.venv/bin/python tools/basse-aigu-releve.py --adaptee               # D281 : coupure ADAPTÉE

    VSM_MORCEAUX=morceau-0001-g1,morceau-0002-g2,... restreint à une moitié du
    corpus (régler sur l'une, valider sur l'autre) ; VSM_LOT choisit le lot.

POURQUOI (D269, D271). La séparation rend une basse dont l'aigu est ATTÉNUÉ, pas
retiré : au-dessus de 300 Hz elle garde 1,7 % de l'énergie de la partie jouée
(contre 25,5 %) mais en conserve la FORME, corrélée à 0,31. Or ce sont ces
partielles qui permettent de trancher une octave, et privé d'elles le
transcripteur choisit bas — 6,9 fois plus souvent que haut, là où sur le stem VRAI
il choisit haut (0,3×).

CE QUI EST MESURÉ. Le stem `bass` est transcrit tel quel (témoin), puis modifié
d'UNE façon dite en ligne de commande — jamais une constante éditée entre deux
passes. Les notes obtenues sont comparées à la vérité du morceau : bonne hauteur,
octave trop bas, octave trop haut, et la part INVENTÉE, qui est le contrôle.
Relever l'aigu relève aussi les fuites des autres instruments : sans ce contrôle,
on gagnerait des octaves en perdant tout le reste, et le chiffre de bonne hauteur
ne le dirait pas.

En mode adapté, la coupure calculée pour CHAQUE morceau est imprimée après le
tableau — c'est la seule variable entre les deux lignes, elle va dans la
provenance — avec le nombre de notes VRAIES qu'elle retire (ce qu'un correcteur
casse compte autant que ce qu'il répare).
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf

RACINE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(RACINE / "analyse"))

from analyzer.coupure_basse import (CENTILE as CENTILE_ADAPTE,  # noqa: E402
                                    FACTEUR as FACTEUR_ADAPTE, SONDE_MINIMUM,
                                    coupure_adaptee, passe_haut)
from analyzer.synth_engine import midi_to_hz  # noqa: E402

LOT = Path(os.environ.get("VSM_LOT", str(RACINE / "reconstruction/travail/r1f-13sep")))
SRC = RACINE / "reconstruction/travail/s1-sec"
TOLERANCE = 0.06
COUPURE = 300.0
# D281-D282 : la règle de la coupure adaptée — centile, facteur, plancher de la
# sonde, mur spectral — vit dans `analyzer/coupure_basse.py`, où la CHAÎNE la
# lit aussi (`--coupure-basse-adaptee`). Une règle, un endroit : ce que cet outil
# valide sur stem est, à l'octet près, ce que la chaîne applique en course.


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


@dataclass(frozen=True)
class Reglage:
    """UN point de mesure, et son nom dans le tableau — décidé une fois, à l'analyse
    de la ligne de commande, jamais deviné d'après le signe d'un nombre."""
    gain_db: float = 0.0
    coupure_hz: float = 0.0
    adaptee: bool = False
    libelle: str = "témoin"


@dataclass
class Morceau:
    """Ce que le mode adapté a décidé pour un morceau : sa provenance."""
    nom: str
    sonde: int                 # notes trouvées par la première transcription
    note: int | None           # la note MIDI au 20ᵉ centile, None si sonde trop courte
    coupure_hz: float
    vraies_sous: int           # notes VRAIES dont la fondamentale est sous la coupure
    vraie_grave: int | None    # note vraie la plus grave (None : pas de basse dans la vérité)


class Transcripteur:
    """basic-pitch chargé UNE fois ; les événements du stem NON modifié (0 dB,
    sans coupure) sont gardés par morceau : c'est le témoin, et c'est aussi la
    sonde du mode adapté — la même transcription, pas une troisième."""

    def __init__(self) -> None:
        from basic_pitch import ICASSP_2022_MODEL_PATH
        from basic_pitch.inference import Model, predict
        self._predict = predict
        self._modele = Model(ICASSP_2022_MODEL_PATH)
        self._brut: dict[str, list] = {}

    def transcrire(self, y: np.ndarray, sr: int) -> list:
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=True) as f:
            sf.write(f.name, y, sr)
            _, _, evts = self._predict(f.name, model_or_model_path=self._modele)
        return list(evts)

    def brut(self, nom: str, y: np.ndarray, sr: int) -> list:
        if nom not in self._brut:
            self._brut[nom] = self.transcrire(y, sr)
        return self._brut[nom]


def morceaux_choisis() -> list[Path]:
    # VSM_MORCEAUX : restreindre à une MOITIÉ du corpus, pour régler sur l'une et
    # valider sur l'autre.
    #
    # POURQUOI IL LE FAUT (13/09/2026). Il n'existe pas de « lot témoin » pour
    # cette mesure : `r1-sec-banc`, `r1-prod-banc` et `r1f-13sep` séparent tous
    # les MÊMES dix morceaux de `s1-sec`, et demucs est déterministe — leurs stems
    # de basse sont IDENTIQUES au md5. Valider un réglage sur un autre lot, c'est
    # le valider sur les mêmes données ; j'ai failli publier exactement cela.
    choisis = [m.strip() for m in os.environ.get("VSM_MORCEAUX", "").split(",") if m.strip()]
    retenus, ecartes = [], []
    for d in sorted(LOT.glob("morceau-*")):
        if choisis and d.name not in choisis:
            continue
        stem = d / "stems-separes" / "stems" / "bass.wav"
        verite = SRC / d.name / "verite.json"
        if stem.is_file() and verite.is_file():
            retenus.append(d)
        else:
            ecartes.append(d.name)
    for nom in choisis:
        if not (LOT / nom).is_dir():
            ecartes.append(f"{nom} (absent de {LOT.name})")
    if ecartes:
        print(f"écartés (stem ou vérité manquants) : {', '.join(ecartes)}")
    return retenus


def mesurer(r: Reglage, tr: Transcripteur, morceaux: list[Path]) -> tuple[dict[str, int], list[Morceau]]:
    c = {"juste": 0, "bas": 0, "haut": 0, "autre": 0, "inventee": 0, "total": 0}
    details: list[Morceau] = []
    for d in morceaux:
        stem = d / "stems-separes" / "stems" / "bass.wav"
        v = json.loads((SRC / d.name / "verite.json").read_text(encoding="utf-8"))
        vraies = sorted((float(n[2]), int(n[0])) for p in v.get("parties", [])
                        if p.get("role") != "batterie" for n in p.get("notes", []))
        x, sr = sf.read(str(stem), always_2d=True)
        sr = int(sr)
        brut = releve(x.mean(axis=1), float(sr), r.gain_db)
        coupure = r.coupure_hz
        if r.adaptee:
            # LA PREMIÈRE TRANSCRIPTION SANS FILTRE dit où est la basse de ce
            # morceau ; c'est celle du témoin, réemployée. La coupure s'en déduit,
            # puis on transcrit pour de bon.
            sonde = tr.brut(d.name, brut, sr) if r.gain_db == 0.0 else tr.transcrire(brut, sr)
            decision = coupure_adaptee(int(e[2]) for e in sonde)
            note, coupure = decision.note, decision.hz
            details.append(Morceau(
                nom=d.name, sonde=len(sonde), note=note, coupure_hz=coupure,
                vraies_sous=sum(1 for _, hv in vraies if midi_to_hz(hv) < coupure),
                vraie_grave=min((hv for _, hv in vraies), default=None)))
        if r.gain_db == 0.0 and coupure <= 0.0:
            evts = tr.brut(d.name, brut, sr)
        else:
            evts = tr.transcrire(passe_haut(brut, float(sr), coupure), sr)
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
    return c, details


def nom_note(midi: int | None) -> str:
    if midi is None:
        return "—"
    noms = ["do", "do#", "ré", "ré#", "mi", "fa", "fa#", "sol", "sol#", "la", "la#", "si"]
    return f"{noms[midi % 12]}{midi // 12 - 1}"


def reglages_depuis(a: argparse.Namespace, p: argparse.ArgumentParser) -> tuple[str, list[Reglage]]:
    """Le mode et ses points, tranchés ICI et une fois. Ce qui ne s'applique pas au
    mode choisi est une ERREUR, pas un argument ignoré en silence."""
    if a.adaptee:
        if a.gains:
            p.error("--adaptee ne prend pas de gains : le témoin est le stem tel quel")
        return "adaptee", [Reglage(), Reglage(adaptee=True, libelle="adaptée")]
    if a.passe_haut is not None:
        if a.gains:
            p.error("--passe-haut ne prend pas de gains : le témoin est la coupure 0")
        if any(hz < 0.0 for hz in a.passe_haut):
            p.error("une coupure est un nombre de hertz positif ou nul (0 = témoin)")
        return "passe-haut", [Reglage(coupure_hz=hz, libelle="témoin" if hz == 0.0 else f"{hz:.0f} Hz")
                              for hz in a.passe_haut]
    if not a.gains:
        p.error("donner les gains en dB (le premier est le témoin, 0), ou --passe-haut, ou --adaptee")
    return "gain", [Reglage(gain_db=g, libelle="témoin" if g == 0.0 else f"{g:+.0f} dB") for g in a.gains]


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("gains", nargs="*", type=float,
                   help="D272 : les relevés en dB de la bande au-dessus de 300 Hz ; "
                        "le PREMIER est le témoin (0 = la chaîne d'aujourd'hui)")
    modes = p.add_mutually_exclusive_group()
    modes.add_argument("--adaptee", action="store_true",
                       help="D281 : coupure ADAPTÉE au morceau — la transcription sans filtre "
                            f"dit où joue sa basse, et l'on coupe sous le {CENTILE_ADAPTE:.0f}e "
                            f"centile de ce qu'elle trouve, à {FACTEUR_ADAPTE} fois sa fréquence. "
                            "Une coupure FIXE a été réfutée : réglée sur la moitié A du corpus, "
                            "elle fait perdre sur la moitié B")
    modes.add_argument("--passe-haut", nargs="+", type=float, default=None, metavar="HZ",
                       help="D280 : au lieu de relever l'aigu, RETIRER le grave sous ces coupures "
                            "(0 = le témoin). La séparation empile de l'énergie sous la fondamentale "
                            "— 49,8 %% contre 5,2 %% dans la partie jouée — et D278 a montré qu'une "
                            "composante grave forte fait descendre le transcripteur d'une octave")
    a = p.parse_args()
    mode, reglages = reglages_depuis(a, p)

    morceaux = morceaux_choisis()
    if not morceaux:
        print(f"aucun morceau : lot {LOT}, VSM_MORCEAUX={os.environ.get('VSM_MORCEAUX', '')!r}")
        return 1
    tr = Transcripteur()

    intro = {
        "gain": f"relevé au-dessus de {COUPURE:.0f} Hz",
        "passe-haut": "GRAVE RETIRÉ sous une coupure FIXE",
        "adaptee": f"GRAVE RETIRÉ sous une coupure ADAPTÉE ({FACTEUR_ADAPTE} × "
                   f"{CENTILE_ADAPTE:.0f}e centile de la sonde, sonde ≥ {SONDE_MINIMUM} notes)",
    }[mode]
    print(f"stem « bass » séparé, {intro}, tolérance {TOLERANCE * 1000:.0f} ms, "
          f"lot {LOT.name}, {len(morceaux)} morceau(x) : {', '.join(d.name for d in morceaux)}")
    colonne = "gain" if mode == "gain" else "coupure"
    print(f"{colonne:>10} {'écrites':>8} {'justes':>8} {'8ve bas':>8} {'8ve haut':>9} "
          f"{'bas/haut':>9} {'bonne h.':>9} {'inventées':>10}")
    provenance: list[Morceau] = []
    for r in reglages:
        c, details = mesurer(r, tr, morceaux)
        provenance += details
        apparie = c["juste"] + c["bas"] + c["haut"] + c["autre"]
        rapport = f"{c['bas'] / c['haut']:.1f}x" if c["haut"] else "—"
        bonne = f"{100 * c['juste'] / apparie:.1f}%" if apparie else "—"
        inv = f"{100 * c['inventee'] / c['total']:.1f}%" if c["total"] else "—"
        print(f"{r.libelle:>10} {c['total']:8d} {c['juste']:8d} {c['bas']:8d} {c['haut']:9d} "
              f"{rapport:>9} {bonne:>9} {inv:>10}")
    if provenance:
        print("\ncoupure adaptée par morceau — la variable entre les deux lignes, et ce qu'elle casse :")
        print(f"{'morceau':>16} {'sonde':>6} {'centile':>8} {'coupure':>9} {'vraie grave':>12} {'vraies sous':>12}")
        for m in provenance:
            coupure = f"{m.coupure_hz:7.1f} Hz" if m.note is not None else "AUCUNE"
            print(f"{m.nom:>16} {m.sonde:6d} {nom_note(m.note):>8} {coupure:>10} "
                  f"{nom_note(m.vraie_grave):>12} {m.vraies_sous:12d}"
                  + ("   sonde trop courte, morceau NON filtré" if m.note is None else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
