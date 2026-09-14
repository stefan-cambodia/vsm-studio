"""D281-D282 : la coupure ADAPTÉE du grave d'un stem de basse — UNE règle, UN endroit.

POURQUOI CE MODULE. La règle est née dans `tools/basse-aigu-releve.py` (D281),
où elle a été validée sur une moitié du corpus qui ne l'avait pas réglée. Pour
la mesurer EN BOUT DE CHAÎNE (D282), la chaîne doit appliquer EXACTEMENT la
même règle que l'outil : deux copies divergent toujours, et l'on mesurerait
alors autre chose que ce qui a validé. L'outil et `reconstruire.py` importent
donc tous deux d'ici.

LA RÈGLE (D281, écrite d'un coup, sans balayage) :
  1. transcrire le stem tel quel — c'est la SONDE ;
  2. prendre le 20ᵉ centile des hauteurs MIDI écrites ;
  3. couper le grave sous 0,75 × sa fréquence, par un mur dans le spectre.

POURQUOI CES DEUX NOMBRES, et pourquoi ils ne se règlent pas :
  * le 20ᵉ centile plutôt que le minimum — la note la plus grave écrite est
    justement celle qu'on soupçonne d'être une octave trop bas ; s'en servir
    pour placer la coupure la protégerait. Un centile bas résiste à quelques
    fausses notes ; PAS à plus de 20 % d'octaves basses sur un morceau, et
    cela se produit (D281, relu le 14/09 : sur quatre morceaux de A sur cinq,
    le centile tombe SOUS le registre vrai et la coupure protège ce qu'elle
    devait ôter). C'est une faiblesse connue, nommée, non corrigée : la
    corriger serait régler la règle APRÈS sa validation ;
  * 0,75 × f0 tombe entre la fondamentale (1,0) et son octave inférieure
    (0,5) : le seul point qui retire l'une sans toucher l'autre.

CE QUE LA SÉPARATION FAIT, et qui justifie de couper (D280) : la partie de
basse jouée met 5,2 % de son énergie sous 80 Hz (médiane sur neuf morceaux),
le stem séparé en met 49,8 %, et jusqu'à 99 % sur un morceau dont la partie
vraie n'en portait que 0,2 %. Un filtre ne peut pas faire cela ; le modèle
reconstruit sa sortie en y plaçant du grave que la source n'a pas — et une
composante grave forte fait descendre le transcripteur d'une octave (D278 :
0,0 → 75,0 % d'octaves basses quand un sous-oscillateur monte).
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Optional

import numpy as np

from analyzer.synth_engine import midi_to_hz

CENTILE = 20.0
FACTEUR = 0.75
# Sous cinq notes, int(n × 0,20) vaut 0 : le « centile » serait le minimum, la
# note même qu'on soupçonne. On ne filtre pas, et on le dit.
SONDE_MINIMUM = 5


@dataclass(frozen=True)
class Coupure:
    """Ce que la règle a décidé pour UN stem : sa provenance, à publier."""
    sonde: int                  # notes trouvées par la première transcription
    note: Optional[int]         # la note MIDI au centile, None si la sonde est trop courte
    hz: float                   # 0.0 quand rien n'est coupé

    @property
    def active(self) -> bool:
        return self.note is not None and self.hz > 0.0

    def dire(self) -> str:
        note = self.note
        if note is None or not self.active:
            return (f"sonde de {self.sonde} note(s), moins que {SONDE_MINIMUM} : "
                    f"le centile serait le minimum, stem NON filtré")
        return (f"sonde de {self.sonde} notes, {CENTILE:.0f}e centile = MIDI {note} "
                f"({midi_to_hz(note):.1f} Hz), coupure à {self.hz:.1f} Hz "
                f"({FACTEUR} × f0)")

    def json(self) -> dict:
        return {"sonde": self.sonde, "centileMidi": self.note, "coupureHz": self.hz,
                "centile": CENTILE, "facteur": FACTEUR, "sondeMinimum": SONDE_MINIMUM}


def coupure_adaptee(hauteurs_midi: Iterable[int], facteur: float = FACTEUR,
                    centile: float = CENTILE) -> Coupure:
    """Où couper le grave de CE stem, d'après ce que sa transcription y trouve.

    Rend une `Coupure` inactive (note None, 0 Hz) quand la sonde est trop courte
    pour qu'un centile veuille dire quelque chose : l'appelant le DIT, il ne
    filtre pas en silence.
    """
    hauteurs = sorted(int(h) for h in hauteurs_midi)
    if len(hauteurs) < SONDE_MINIMUM:
        return Coupure(sonde=len(hauteurs), note=None, hz=0.0)
    note = hauteurs[min(len(hauteurs) - 1, int(len(hauteurs) * centile / 100.0))]
    return Coupure(sonde=len(hauteurs), note=note, hz=facteur * midi_to_hz(note))


def passe_haut(x: np.ndarray, sr: float, coupure_hz: float) -> np.ndarray:
    """Retire au signal tout ce qui est sous `coupure_hz` — un mur dans le
    spectre, pas une pente : c'est ce qui a été validé, et un filtre à pente
    laisserait passer une part du grave empilé. 0 Hz (ou moins) rend x tel quel."""
    if coupure_hz <= 0.0:
        return x
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1.0 / sr)
    return np.fft.irfft(np.where(f >= coupure_hz, X, 0.0), n=len(x))
