"""
Construction d'un KIT DE BATTERIE pour le sampler, à partir d'un stem.

C'est l'étape 9.5 de la feuille de route. Jusqu'ici le stem de batterie était
explicitement ignoré : aucune machine du parc ne reproduit une batterie
enregistrée par synthèse, et produire une piste fausse aurait été pire que de
le dire.

LE PRINCIPE, et pourquoi il fonctionne là où la synthèse échoue : on ne cherche
pas à *fabriquer* le son d'une caisse claire, on RÉUTILISE celle de
l'enregistrement. Le stem est découpé en coups, les coups sont regroupés par
pièce, et un représentant de chaque pièce devient un échantillon du kit.

LE CLASSEMENT SE FAIT PAR GABARITS APPRIS, et c'est la troisième architecture
de ce module -- chacune tuée par une mesure. Le spectre global par coup
échouait sur les frappes simultanées (un spectre mélangé ne se classe pas).
La part d'énergie par bande de fréquences, qui l'a remplacé, s'est INVERSÉE
sur le premier morceau de club réel : la queue d'un kick de house couvre
toutes les frappes suivantes, si bien qu'à l'instant d'une charleston la
bande grave domine (classée kick) et qu'à l'instant d'un vrai kick le clic
d'attaque pousse le médium (classé caisse claire). Mesuré deux fois : 811
frappes sur 813 en « kick » sur House Of God (D.H.S., 1995), puis
l'inversion exacte sur un motif-vérité rendu par la TR-909 du parc.

L'architecture actuelle mesure la NOUVEAUTÉ de chaque attaque -- spectre
juste après moins spectre juste avant, borné à zéro : ce qui sonnait déjà
s'annule, ne reste que la pièce frappée --, regroupe ces empreintes en
gabarits (au meneur, déterministe), élague les gabarits qui ne sont que la
somme d'autres (des coïncidences apprises, pas des pièces), décompose chaque
attaque en parts de gabarits (moindres carrés positifs : une attaque peut
être kick ET charleston), et nomme chaque gabarit par la répartition mesurée
de son énergie. Sur les motifs-vérité TR-808/909, le passage de l'ancien au
nouveau classement donne :

    pièce      avant (A/909)          après (A/909)   avant (B/808)   après (B/808)
    kick       0/16, 48 inventées     16/16, 3 inv.   0/16, 19 inv.   16/16, 1 inv.
    hihat      62/64 (instants faux)  46/64, 0 inv.   16/16, 11 inv.  16/16, 0 inv.

CE QUE LE CLASSEMENT NE SAIT PAS FAIRE, mesuré aussi : une pièce qui ne
frappe JAMAIS seule -- la caisse claire d'un morceau de club vit sur un kick,
toujours -- est invisible dans les empreintes (similarité kick-seul contre
kick+caisse : 0,947, identique à la variabilité interne du kick, en linéaire
comme en logarithmique). Elle reste FUSIONNÉE avec sa porteuse, et c'est
moins grave qu'il n'y paraît : l'échantillon mixte prélevé sur ces frappes
contient LES DEUX pièces, et les rejoue aux bons instants. Seule l'étiquette
ment un peu ; le son, non. Même statut pour les gabarits « fantômes »
occasionnels (une charleston teintée d'une queue de caisse claire) : leur
échantillon vient de leurs propres frappes, le rendu reste juste.

CE QU'ON NE FAIT PAS, et pourquoi :

  - On ne moyenne PAS les coups d'une même pièce. Deux coups ne sont jamais
    alignés à l'échantillon près ; les moyenner effacerait précisément
    l'attaque, c'est-à-dire ce qui fait reconnaître une percussion.
  - On ne prend pas non plus le coup le plus FORT : c'est souvent un accent,
    donc un cas particulier. On prend le MÉDOÏDE -- le coup le plus proche de
    tous les autres --, c'est-à-dire le plus représentatif.
"""

from __future__ import annotations

import wave
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import numpy as np

from .vsm_project_export import ExportNote, ExportTrack

# Familles reconnues, avec la note MIDI qui les déclenchera. Ce sont les notes
# de la convention General MIDI : un projet exporté se relit donc dans
# n'importe quel séquenceur avec les bons noms de pièces.
DRUM_FAMILIES: List[Tuple[str, int]] = [
    ("kick", 36),
    ("snare", 38),
    ("hihat", 42),
    ("tom", 45),
    ("cymbal", 49),
    ("percussion", 39),
]

# Durée maximale d'un coup extrait. Au-delà, on capterait le coup suivant.
MAX_HIT_SECONDS = 1.2
# Durée minimale exploitable : en dessous, il n'y a pas de coup, il y a un clic.
MIN_HIT_SECONDS = 0.03


@dataclass
class DrumSlot:
    """Un emplacement du kit : une famille, son échantillon, ses frappes."""
    family: str
    # ATTENTION AUX DEUX NUMÉROTATIONS, et elles diffèrent d'une unité :
    #   `slot` est l'emplacement du CHARGEUR d'échantillons (0..7) ;
    #   les paramètres, eux, s'appellent `sampler.slot.1.*` à `sampler.slot.8.*`.
    # Confondre les deux charge le son dans un emplacement et règle un autre.
    slot: int
    midi_note: int
    sample_path: str          # relatif au dossier de projet
    onsets: List[float] = field(default_factory=list)
    velocities: List[int] = field(default_factory=list)
    hit_count: int = 0
    # Frappes où aucune autre pièce ne sonnait : c'est parmi elles que
    # l'échantillon a été prélevé. Zéro veut dire que l'échantillon contient
    # forcément les autres pièces, et l'appelant doit pouvoir le dire.
    isolated_hits: int = 0


@dataclass
class DrumKit:
    slots: List[DrumSlot]
    sample_rate: int
    total_hits: int
    warnings: List[str] = field(default_factory=list)


# Ordre d'affichage des familles dans le kit : les pièces porteuses du motif
# d'abord. Les bandes de fréquences qui vivaient ici ont eu deux vies : elles
# ont d'abord DÉTECTÉ (une part d'énergie par bande et par attaque), puis la
# mesure a montré que ce critère s'INVERSAIT sur un kick qui résonne -- voir
# l'en-tête du module -- et la détection est passée aux gabarits appris
# (_novelty_fingerprints et la suite). Les familles ne servent plus qu'à trier
# et à nommer.
FAMILY_ORDER: List[str] = ["kick", "kick2", "snare", "snare2", "hihat",
                            "openhat", "pedalhat", "percussion", "tom", "cymbal"]

# Fenêtres de la mesure de montée, autour de l'instant d'attaque.
#
# La fenêtre « après » va jusqu'à 40 ms, et ce n'est pas de la marge : une
# grosse caisse tourne autour de 50 Hz, soit une période de 20 ms. Mesurer son
# énergie sur 30 ms, c'est la mesurer sur une période et demie -- le résultat
# dépend alors de la phase plus que de l'amplitude.
BEFORE_WINDOW = (-0.045, -0.008)
AFTER_WINDOW = (0.000, 0.040)

# Silence ajouté en tête avant la détection. Sans lui, une frappe à l'instant
# zéro est INVISIBLE : un détecteur d'attaques compare à ce qui précède, et
# rien ne précède le premier échantillon. Sur un motif commençant par une
# grosse caisse -- c'est-à-dire la quasi-totalité des motifs -- la première
# frappe manquait.
DETECTION_PADDING_SECONDS = 0.1


# Résolution de l'analyse par bandes. 128 échantillons de saut = 2,9 ms à
# 44,1 kHz : assez fin pour distinguer deux frappes que l'oreille sépare.
STFT_WINDOW = 1024
STFT_HOP = 128


def _log_band_spectrogram(audio: np.ndarray, sample_rate: int) -> Tuple[np.ndarray, int]:
    """
    Spectrogramme en 24 bandes LOGARITHMIQUES (40 Hz - 16 kHz).

    C'est la matière première des empreintes de nouveauté : assez de bandes
    pour séparer un clap d'une caisse claire, échelle log parce que l'oreille
    compare des rapports de fréquence.
    """
    fenetres = np.lib.stride_tricks.sliding_window_view(audio, STFT_WINDOW)[::STFT_HOP]
    spectre = np.abs(np.fft.rfft(fenetres * np.hanning(STFT_WINDOW), axis=1))
    frequences = np.fft.rfftfreq(STFT_WINDOW, 1.0 / sample_rate)
    bornes = np.geomspace(40.0, min(16000.0, sample_rate / 2.0 - 1.0), 25)
    bandes = np.stack([
        np.sqrt(np.sum(spectre[:, (frequences >= bornes[i]) & (frequences < bornes[i + 1])] ** 2, axis=1))
        for i in range(len(bornes) - 1)
    ], axis=1)
    return bandes, STFT_HOP


def _novelty_fingerprints(
    bandes: np.ndarray,
    instants: Sequence[int],
    sample_rate: int,
) -> Tuple[np.ndarray, np.ndarray]:
    """
    Empreinte de NOUVEAUTÉ de chaque attaque : ce qui APPARAÎT à cet instant.

    C'est la pièce maîtresse du classement, et elle répond à un échec mesuré
    deux fois -- sur House Of God (811 frappes sur 813 classées grosse caisse)
    puis sur un motif-vérité rendu par la TR-909, où le critère précédent
    s'INVERSAIT : les temps (vrais kicks) partaient en caisse claire, les
    doubles-croches de charleston en grosse caisse. La cause : la part
    d'énergie d'une bande mesure ce qui RÉSONNE -- la queue d'un kick de club
    couvre toutes les frappes suivantes -- et non ce qui FRAPPE.

    La nouveauté (spectre juste après MOINS spectre juste avant, borné à zéro)
    annule les queues par construction : ce qui sonnait déjà avant l'attaque
    disparaît de la soustraction. Ne reste que la pièce frappée.
    """
    def trame(echantillon: int) -> int:
        # même correction de centrage que le reste du module
        return max(0, (echantillon - STFT_WINDOW // 2) // STFT_HOP)

    empreintes, energies = [], []
    for instant in instants:
        avant = bandes[trame(instant + int(BEFORE_WINDOW[0] * sample_rate)):
                        trame(instant + int(BEFORE_WINDOW[1] * sample_rate)) + 1]
        apres = bandes[trame(instant + int(0.002 * sample_rate)):
                        trame(instant + int(AFTER_WINDOW[1] * sample_rate)) + 1]
        # Le fond est la MOYENNE d'avant, et le choix est mesuré dans les
        # deux sens. Le MAX d'avant supprimait bien le gabarit fantôme
        # « charleston + queue de caisse claire » (la queue bruiteuse d'une
        # caisse claire scintille, et son pic repassait pour de la
        # nouveauté)... mais il effaçait aussi les charlestons jouées en
        # continu : face au max de la charleston précédente, la nouvelle ne
        # dépassait plus (rappel 5/64 contre 46/64). C'est la leçon
        # historique du module -- une pièce jouée en continu ne monte pas --
        # retrouvée par la mesure. On garde la moyenne, et on assume le
        # fantôme occasionnel : son échantillon est prélevé sur SES propres
        # frappes, il rejoue donc le bon son aux bons instants, seule son
        # étiquette est de trop.
        fond = np.mean(avant, axis=0) if avant.size else np.zeros(bandes.shape[1])
        pic = np.max(apres, axis=0) if apres.size else np.zeros(bandes.shape[1])
        nouveaute = np.maximum(pic - fond, 0.0)
        energie = float(np.linalg.norm(nouveaute))
        empreintes.append(nouveaute / (energie + 1e-12))
        energies.append(energie)
    return np.stack(empreintes), np.asarray(energies)


# Similarité en deçà de laquelle deux attaques sont des pièces DIFFÉRENTES.
# Mesuré sur les motifs-vérité : à 0,90 la TR-909 éclatait en gabarits
# redondants ; à 0,60 kick et charleston fusionnaient. Entre 0,70 et 0,85 le
# banc rend les mêmes gabarits, le choix au centre de la zone stable.
TEMPLATE_SIMILARITY = 0.78

# Un gabarit doit expliquer au moins cette part de la nouveauté d'une attaque
# pour que la pièce soit dite FRAPPÉE à cet instant. Bas = des frappes
# inventées ; haut = la pièce discrète d'un coup simultané disparaît.
ASSIGN_SHARE = 0.30

# Part au-delà de laquelle une frappe est PURE : une seule pièce y sonne, on
# peut y prélever un échantillon sans emporter les autres.
PURE_SHARE = 0.85

# Nombre maximal de pièces apprises. Huit : ce que la façade du sampler
# programme au pas, et déjà plus qu'un kit de club courant.
MAX_TEMPLATES = 8


def _learn_templates(empreintes: np.ndarray, energies: np.ndarray) -> np.ndarray:
    """
    Apprend les gabarits de pièces par regroupement DÉTERMINISTE.

    Regroupement « au meneur » : les attaques sont visitées par nouveauté
    décroissante ; chacune rejoint le gabarit le plus proche si la similarité
    cosinus dépasse TEMPLATE_SIMILARITY, sinon elle en fonde un nouveau.
    Aucun tirage au sort nulle part -- deux exécutions rendent les mêmes
    gabarits, condition de toute comparaison avant/après.

    Deux passes de raffinage ré-affectent chaque attaque au gabarit recalculé :
    le meneur initial est un accent, pas forcément un centre.
    """
    ordre = np.argsort(-energies, kind="stable")
    centres: List[np.ndarray] = []
    for i in ordre:
        e = empreintes[i]
        if centres:
            similarites = [float(np.dot(e, c) / (np.linalg.norm(c) + 1e-12)) for c in centres]
            meilleur = int(np.argmax(similarites))
            if similarites[meilleur] >= TEMPLATE_SIMILARITY:
                centres[meilleur] = centres[meilleur] + e
                continue
        if len(centres) < MAX_TEMPLATES:
            centres.append(e.copy())
    for _ in range(2):
        normalises = [c / (np.linalg.norm(c) + 1e-12) for c in centres]
        sommes = [np.zeros_like(empreintes[0]) for _ in centres]
        comptes = [0] * len(centres)
        for i in ordre:
            similarites = [float(np.dot(empreintes[i], n)) for n in normalises]
            meilleur = int(np.argmax(similarites))
            sommes[meilleur] += empreintes[i]
            comptes[meilleur] += 1
        centres = [somme for somme, compte in zip(sommes, comptes) if compte > 0]
    return np.stack([c / (np.linalg.norm(c) + 1e-12) for c in centres])


# Résidu en deçà duquel un gabarit s'explique comme SOMME des autres : ce
# n'est alors pas une pièce, c'est une coïncidence apprise (kick et charleston
# frappés ensemble assez souvent pour fonder leur propre groupe). Mesuré sur
# le motif-vérité TR-909 : le gabarit fantôme « queue de kick + charleston »
# porte un résidu de 0,38, les vraies pièces restent au-dessus de 0,75.
MIXTURE_RESIDUAL = 0.55

def _prune_mixtures(gabarits: np.ndarray) -> np.ndarray:
    """
    Retire les gabarits qui ne sont que la SOMME d'autres gabarits.

    Kick et charleston frappés ensemble assez souvent fondent leur propre
    groupe -- une coïncidence apprise, pas une pièce. Mesuré sur le
    motif-vérité TR-909 : le gabarit fantôme « queue de kick + charleston »
    porte un résidu de 0,45 face aux vrais gabarits, les vraies pièces
    restent au-dessus de 0,60.
    """
    from scipy.optimize import nnls

    garde = list(range(len(gabarits)))
    change = True
    while change and len(garde) > 2:
        change = False
        for candidat in list(garde):
            autres = [i for i in garde if i != candidat]
            poids, residu = nnls(gabarits[autres].T, gabarits[candidat])
            if residu < MIXTURE_RESIDUAL and poids.sum() > 0.0:
                garde.remove(candidat)
                change = True
                break
    return gabarits[garde]


def _assign_hits(empreintes: np.ndarray, gabarits: np.ndarray) -> np.ndarray:
    """
    Parts de chaque gabarit dans chaque attaque, par moindres carrés POSITIFS.

    C'est la « décomposition en familles apprises » que la limite documentée
    du critère précédent appelait : une attaque où kick et charleston frappent
    ENSEMBLE s'explique comme somme des deux gabarits, et chacun reçoit sa
    part -- là où un classement à pièce unique devait choisir.
    """
    from scipy.optimize import nnls

    parts = np.zeros((empreintes.shape[0], gabarits.shape[0]))
    for i, e in enumerate(empreintes):
        poids, _ = nnls(gabarits.T, e)
        total = float(poids.sum())
        if total > 0.0:
            parts[i] = poids / total
    return parts


def _name_templates(gabarits: np.ndarray, sample_rate: int) -> List[Tuple[str, int]]:
    """
    Nomme chaque gabarit d'après la RÉPARTITION de son énergie de nouveauté.

    Les seuils sont MESURÉS sur les motifs-vérité TR-808/909, pas choisis :

        gabarit             part >= 3,5 kHz   part >= 1 kHz
        charlestons             0,72-0,95       0,84-0,95
        caisse claire           0,35            0,68
        kicks (et variantes)    0,00-0,06       0,00-0,16

    Le PIC seul trompait deux fois : le clic d'attaque du kick 808 culmine à
    203 Hz (sa fondamentale de 50 Hz s'annule avec la queue du kick précédent
    dans la nouveauté), et une somme par zones inégales en largeur faisait
    gagner le médium. Les parts d'énergie au-dessus de 1 et 3,5 kHz, elles,
    séparent les trois familles avec une marge d'un facteur deux.

    Un même timbre peut fonder PLUSIEURS gabarits (le premier kick d'un
    morceau n'a pas de queue à soustraire, il garde ses graves) : les doublons
    d'une famille reçoivent les notes voisines de la convention General MIDI,
    pour que le projet exporté garde des noms vrais.
    """
    familles: List[Tuple[str, int]] = []
    reserves: Dict[str, List[Tuple[str, int]]] = {
        "kick": [("kick", 36), ("kick2", 35)],
        "snare": [("snare", 38), ("snare2", 40)],
        "hihat": [("hihat", 42), ("openhat", 46), ("pedalhat", 44)],
    }
    divers = [("percussion", 39), ("tom", 45), ("cymbal", 49), ("tom2", 47), ("tom3", 50)]
    bornes = np.geomspace(40.0, min(16000.0, sample_rate / 2.0 - 1.0), 25)
    centres_bandes = np.sqrt(bornes[:-1] * bornes[1:])
    for gabarit in gabarits:
        energie = gabarit * gabarit
        total = float(energie.sum()) + 1e-12
        part_1k = float(energie[centres_bandes >= 1000.0].sum()) / total
        part_3k5 = float(energie[centres_bandes >= 3500.0].sum()) / total
        if part_3k5 >= 0.5:
            famille = "hihat"
        elif part_1k >= 0.35:
            famille = "snare"
        else:
            famille = "kick"
        pool = reserves[famille]
        familles.append(pool.pop(0) if pool else (divers.pop(0) if divers else (famille, 36)))
    return familles


def _hit_features(extrait: np.ndarray, sample_rate: int) -> np.ndarray:
    """
    Empreinte d'un coup, pour comparer deux coups d'une même famille.

    Bandes en échelle logarithmique : l'oreille compare des rapports de
    fréquence, pas des écarts. Normalisée en énergie, sinon la comparaison
    mesurerait surtout le volume -- et le médoïde serait le coup de force moyen
    plutôt que le coup de timbre moyen.
    """
    spectre = np.abs(np.fft.rfft(extrait * np.hanning(extrait.size)))
    frequences = np.fft.rfftfreq(extrait.size, 1.0 / sample_rate)
    bornes = np.geomspace(40.0, min(16000.0, sample_rate / 2.0 - 1.0), 17)
    bandes = np.array([
        float(np.sum(spectre[(frequences >= bornes[i]) & (frequences < bornes[i + 1])]))
        for i in range(len(bornes) - 1)
    ])
    return bandes / (float(np.linalg.norm(bandes)) + 1e-12)


def _medoid_index(empreintes: Sequence[np.ndarray]) -> int:
    """
    Indice du coup le plus REPRÉSENTATIF : celui dont la distance totale aux
    autres est la plus faible. Le plus fort serait un accent, le premier serait
    un hasard.
    """
    if len(empreintes) == 1:
        return 0
    matrice = np.stack(empreintes)
    distances = np.linalg.norm(matrice[:, None, :] - matrice[None, :, :], axis=2)
    return int(np.argmin(distances.sum(axis=1)))


def _write_wav(chemin: Path, audio: np.ndarray, sample_rate: int, gain: float = 1.0) -> None:
    """
    Écrit un échantillon du kit.

    `gain` est le MÊME pour tout le kit, et c'est essentiel : normaliser chaque
    échantillon à sa propre crête détruirait l'équilibre de la batterie -- une
    charleston se retrouverait aussi forte qu'une grosse caisse. On applique
    donc un facteur unique, calculé sur la frappe la plus forte du kit, ce qui
    conserve les proportions de l'enregistrement d'origine.
    """
    chemin.parent.mkdir(parents=True, exist_ok=True)
    # Fondu de sortie de 5 ms : un échantillon coupé net claque, et le clic
    # s'entendrait à chaque frappe.
    fondu = min(int(0.005 * sample_rate), audio.size // 4)
    sortie = np.array(audio, dtype=np.float32, copy=True) * float(gain)
    if fondu > 0:
        sortie[-fondu:] *= np.linspace(1.0, 0.0, fondu, dtype=np.float32)
    sortie = np.clip(sortie, -1.0, 1.0)
    with wave.open(str(chemin), "wb") as fichier:
        fichier.setnchannels(1)
        fichier.setsampwidth(2)
        fichier.setframerate(sample_rate)
        fichier.writeframes((sortie * 32767).astype("<i2").tobytes())


def _decay_end(extrait: np.ndarray, sample_rate: int) -> int:
    """
    Longueur utile d'un coup anormalement long : jusqu'à la fin de SA
    décroissance.

    N'agit que sur les tranches de plus de 0,6 s -- le cas « onset suivant
    manqué » : une fuite de voix dans le stem de batterie n'a pas d'attaque,
    et la tranche embarquait jusqu'à 1,2 s d'autre chose (mesuré : une
    « caisse claire » de 1 235 ms qui rejouait un bout du morceau à chaque
    frappe). Les tranches courtes, bien bornées par l'onset suivant, restent
    intactes -- elles sonnaient juste.

    La coupe se fait à la REMONTÉE : après la crête du coup, l'enveloppe
    décroît ; l'endroit où elle repasse 6 dB AU-DESSUS de son minimum courant
    est l'endroit où autre chose entre. Un seuil absolu (-30 dB de la crête)
    ne mordait pas : dans un stem dense, il n'y a jamais 50 ms de calme.
    """
    if extrait.size <= int(0.6 * sample_rate):
        return extrait.size
    fenetre = max(1, int(0.010 * sample_rate))
    enveloppe = np.sqrt(np.convolve(extrait.astype(np.float64) ** 2,
                                     np.ones(fenetre) / fenetre, mode="same"))
    pic = int(np.argmax(enveloppe))
    minimum = float(enveloppe[pic])
    garde = pic + int(0.080 * sample_rate)  # laisser vivre le corps du coup
    for i in range(pic, enveloppe.size):
        minimum = min(minimum, float(enveloppe[i]))
        if i > garde and enveloppe[i] > minimum * 2.0:
            return i
    # Ni remontée ni borne : l'enveloppe est un PLATEAU -- du contenu soutenu
    # (nappe de bruit, fuite), pas la queue d'un coup. Un vrai coup long (une
    # cymbale) décroît et passe sous -20 dB de sa crête : on coupe là. Un
    # plateau qui ne descend jamais est coupé à 0,6 s -- mesuré : la
    # « percussion » de House Of God restait à 1 223 ms parce que rien dans sa
    # tranche ne ressemblait à une extinction.
    seuil = float(np.max(enveloppe)) * 0.1
    sous = np.nonzero(enveloppe[garde:] < seuil)[0]
    if sous.size:
        return garde + int(sous[0])
    return int(0.6 * sample_rate)


def build_drum_kit(
    audio: np.ndarray,
    sample_rate: int,
    samples_folder: Path,
    relative_prefix: str = "samples",
    max_slots: int = 16,
    write_samples: bool = True,
) -> Optional[DrumKit]:
    """
    Découpe un stem de batterie en kit jouable par le sampler.

    La détection se fait BANDE PAR BANDE, chacune donnant sa propre famille :
    c'est la seule façon d'entendre une grosse caisse et une charleston jouées
    au même instant, ce qui est le cas le plus courant qui soit.

    Renvoie None si aucun coup n'est détecté -- ce qui est DIT par l'appelant,
    jamais compensé par un kit inventé.

    `write_samples=False` fait tout le travail SAUF écrire les WAV découpés :
    la détection, le classement, les instants et les vélocités sont identiques,
    et le kit reste jouable par `modelled_drum_track`. C'est ce qu'il faut quand
    le sampler est interdit -- écrire des échantillons que rien ne charge
    laisserait croire, en ouvrant le projet, qu'il en dépend.

    OÙ SE SITUE LA LIMITE AUJOURD'HUI. Le sampler accepte seize emplacements et
    sa façade sait les montrer ; ce n'est donc plus le moteur qui borne le kit,
    mais CE DÉTECTEUR : il ne distingue que trois familles (grosse caisse,
    caisse claire, cymbales), parce qu'il les sépare par bandes de fréquences.
    Aller plus loin -- séparer les toms entre eux, une charleston d'une
    cymbale -- demande des gabarits spectraux appris, et non des bandes
    supplémentaires : la mesure du § « ce que cette règle ne sait pas faire »
    l'a montré, l'énergie d'une caisse claire recouvre celle d'une charleston.
    """
    import librosa

    if audio.size < sample_rate // 10:
        return None

    # --- UNE SEULE détection d'attaques, sur le signal complet ---------------
    #
    # C'est le point d'architecture de ce module, et il vient de deux échecs
    # mesurés sur un motif connu (8 grosses caisses, 8 caisses claires,
    # 32 charlestons) :
    #
    #   - Classer chaque attaque d'après son SPECTRE GLOBAL : 30 coups trouvés
    #     sur 48, dont aucune grosse caisse et aucune caisse claire. Quand deux
    #     pièces frappent ensemble, leur spectre mêlé ne se classe pas.
    #   - Détecter les attaques SÉPARÉMENT DANS CHAQUE BANDE : la résonance
    #     grave de la grosse caisse produisait ses propres fausses attaques,
    #     63 pour 8 réelles.
    #
    # La détection sur le signal complet, elle, donne des instants justes : une
    # résonance n'a pas d'attaque. Il reste à demander à chaque bande si ELLE
    # monte à cet instant -- et une même attaque peut alors appartenir à
    # plusieurs pièces, ce qui est précisément le cas courant.
    # Deux jeux d'instants, et la distinction est nécessaire :
    #
    #   `attaques`  -- non recalées, elles tombent SUR la montée d'énergie.
    #                  C'est ce qu'il faut pour juger quelle bande monte.
    #   `decoupes`  -- recalées au creux précédent (« backtrack »), pour ne pas
    #                  couper le tout début de la frappe à l'extraction.
    #
    # Les confondre a coûté la grosse caisse entière : le recalage la ramenait
    # ~18 ms trop tôt, or sa période est de 20 ms -- la fenêtre « après »
    # tombait donc AVANT l'attaque, et le rapport de montée ne dépassait
    # jamais 2 là où on attendait 10.
    marge = int(DETECTION_PADDING_SECONDS * sample_rate)
    rembourre = np.concatenate([np.zeros(marge, dtype=np.float32), audio])
    # `delta` et `wait` abaissés par rapport aux valeurs par défaut, et
    # mesurés : sur un motif de 32 instants distincts, les réglages d'origine
    # en trouvaient 31, ceux-ci 32, et les rendre encore plus sensibles en
    # trouvait 78 -- c'est-à-dire du bruit.
    trames = librosa.onset.onset_detect(
        y=rembourre, sr=sample_rate, units="frames", delta=0.02, wait=2
    )
    attaques = [int(d) - marge for d in librosa.frames_to_samples(trames)]
    recalees = [int(d) - marge for d in
                librosa.frames_to_samples(librosa.onset.onset_backtrack(
                    trames, librosa.onset.onset_strength(y=rembourre, sr=sample_rate)))]
    paires = [(a, max(0, min(a, r))) for a, r in zip(attaques, recalees) if a >= 0]
    if not paires:
        return None
    # FUSION DES ONSETS JUMEAUX (< 35 ms). Le détecteur produit parfois deux
    # attaques à 25-30 ms sur un même coup ; la seconde, faite de la traîne de
    # la première, se classait dans un gabarit-variante et déclenchait un FLA
    # -- deux échantillons presque identiques à 30 ms d'écart, entendu comme
    # « les pièces ne jouent pas au bon endroit ». À 140 BPM la double-croche
    # fait 107 ms : rien de musical ne vit sous 35 ms, on garde le premier.
    fenetre_fla = int(0.035 * sample_rate)
    fusionnees = [paires[0]]
    for a, r in paires[1:]:
        if a - fusionnees[-1][0] < fenetre_fla:
            continue
        fusionnees.append((a, r))
    paires = fusionnees
    instants = [a for a, _ in paires]
    decoupe_de = {a: d for a, d in paires}

    # --- classement par GABARITS APPRIS --------------------------------------
    #
    # L'empreinte de nouveauté de chaque attaque (voir _novelty_fingerprints)
    # est regroupée en gabarits de pièces, puis chaque attaque est décomposée
    # en parts de gabarits. Une attaque peut appartenir à PLUSIEURS pièces --
    # kick et charleston frappent ensemble sur tous les temps d'un morceau de
    # club -- et c'est la part de chaque gabarit qui le dit, plus un seuil par
    # bande de fréquences.
    bandes_log, _ = _log_band_spectrogram(audio, sample_rate)
    empreintes, energies = _novelty_fingerprints(bandes_log, instants, sample_rate)
    if not np.any(energies > 0.0):
        return None
    gabarits = _learn_templates(empreintes, energies)
    gabarits = _prune_mixtures(gabarits)
    parts = _assign_hits(empreintes, gabarits)
    noms = _name_templates(gabarits, sample_rate)

    # UNE PIÈCE PAR FRAPPE : celle qui explique le mieux la nouveauté.
    #
    # L'histoire de cette règle est une paire d'échecs mesurés. La version
    # multi-étiquettes (toute pièce à part >= ASSIGN_SHARE) faisait tirer les
    # gabarits-variantes ensemble : 187 co-frappes sur House Of God, toutes au
    # même instant, entendues comme « les pièces ne jouent pas au bon
    # endroit ». La version « paire autorisée si gabarits dissemblables »
    # n'a rien filtré : les variantes SONT dissemblables en nouveauté --
    # c'est pour cela qu'elles ont formé leur propre gabarit. Et le cas
    # légitime que la paire devait servir -- kick et charleston frappés
    # ensemble -- ne se produit presque jamais en pratique (1 co-frappe sur
    # tout le morceau) : la nouveauté du kick écrase la part de la charleston.
    # Une frappe, une pièce ; la simultanéité vécue reste servie par
    # l'échantillon lui-même, qui contient ce qui sonnait à cet instant.
    detections: Dict[str, Tuple[int, List[int]]] = {}
    frappes_par_gabarit: Dict[int, List[int]] = {k: [] for k in range(len(gabarits))}
    for i, instant in enumerate(instants):
        meilleur = int(np.argmax(parts[i]))
        if parts[i, meilleur] >= ASSIGN_SHARE:
            frappes_par_gabarit[meilleur].append(instant)
    for index, (famille, note) in enumerate(noms):
        if frappes_par_gabarit[index]:
            detections.setdefault(famille, (note, frappes_par_gabarit[index]))

    if not detections:
        return None

    # --- coups isolés ---------------------------------------------------------
    # L'échantillon d'une pièce doit venir d'une frappe où elle sonne SEULE :
    # prélever une grosse caisse pendant une charleston mettrait la charleston
    # dans l'échantillon, et on l'entendrait à chaque frappe du kit. La pureté
    # se lit directement dans les parts de la décomposition.
    part_de = {(int(instant), famille): float(parts[i, index])
               for i, instant in enumerate(instants)
               for index, (famille, _) in enumerate(noms)}

    def est_isole(famille: str, debut: int) -> bool:
        return part_de.get((int(debut), famille), 0.0) >= PURE_SHARE

    crete_globale = float(np.max(np.abs(audio))) or 1.0
    # Facteur commun à tout le kit : la frappe la plus forte du stem sort à
    # 0,9, les autres gardent leur niveau relatif.
    # 0,7 et non 0,9 : plusieurs pièces frappent souvent ensemble et leurs
    # échantillons s'additionnent. Viser 0,9 pour la frappe la plus forte
    # faisait écrêter le kit reconstruit dès que deux pièces coïncidaient.
    gain_du_kit = 0.7 / crete_globale
    emplacements: List[DrumSlot] = []
    avertissements: List[str] = []

    ordre = {nom: rang for rang, nom in enumerate(FAMILY_ORDER)}
    familles = sorted(detections.items(),
                      key=lambda item: (-len(item[1][1]), ordre.get(item[0], 99)))[:max_slots]

    for position, (famille, (note, debuts)) in enumerate(familles):
        extraits, cretes, instants = [], [], []
        for index, debut in enumerate(debuts):
            fin = debuts[index + 1] if index + 1 < len(debuts) else audio.size
            fin = min(int(fin), int(debut) + int(MAX_HIT_SECONDS * sample_rate), audio.size)
            # COUPE À L'EXTINCTION, en plus du prochain onset : le prochain
            # onset DÉTECTÉ peut être loin (une fuite de voix dans le stem de
            # batterie n'a pas d'attaque), et la tranche embarquait alors
            # jusqu'à 1,2 s d'autre chose -- mesuré sur House Of God : la
            # « caisse claire » de 1 235 ms rejouait un bout du morceau à
            # chaque frappe. La règle exacte est dans `_decay_end` : la
            # remontée de l'enveloppe au-dessus de son minimum courant, et
            # non un seuil absolu -- qui ne mord pas dans un stem dense.
            fin = int(debut) + _decay_end(audio[int(debut):fin], sample_rate)
            # Découpe au point RECALÉ : partir de l'instant d'attaque
            # amputerait la montée, c'est-à-dire ce qui fait reconnaître la
            # frappe.
            extrait = audio[decoupe_de.get(int(debut), int(debut)):fin]
            if extrait.size < int(MIN_HIT_SECONDS * sample_rate):
                continue
            crete = float(np.max(np.abs(extrait)))
            if crete < 1e-4:
                continue
            extraits.append(extrait)
            cretes.append(crete)
            instants.append(int(debut))
        if not extraits:
            continue

        # Le représentant se choisit d'abord parmi les frappes ISOLÉES.
        isoles = [i for i, debut in enumerate(instants) if est_isole(famille, debut)]
        candidats = isoles if isoles else list(range(len(extraits)))
        if not isoles:
            avertissements.append(
                f"{famille} : aucune frappe isolée, l'échantillon contient les autres pièces"
            )
        empreintes = [_hit_features(extraits[i], sample_rate) for i in candidats]
        representatif = extraits[candidats[_medoid_index(empreintes)]]

        nom_fichier = f"{famille}.wav"
        if write_samples:
            _write_wav(samples_folder / nom_fichier, representatif, sample_rate, gain_du_kit)

        emplacements.append(
            DrumSlot(
                family=famille,
                slot=position,
                midi_note=note,
                sample_path=f"{relative_prefix}/{nom_fichier}",
                onsets=[debut / sample_rate for debut in instants],
                # La vélocité vient de la crête du coup, rapportée au coup le
                # plus fort du stem : c'est la dynamique réellement jouée, et
                # la reproduire compte autant que le timbre.
                velocities=[
                    max(1, min(127, int(round(20 + 107 * (crete / crete_globale)))))
                    for crete in cretes
                ],
                hit_count=len(extraits),
                isolated_hits=len(isoles),
            )
        )

    if not emplacements:
        return None
    return DrumKit(
        slots=emplacements,
        sample_rate=sample_rate,
        total_hits=sum(slot.hit_count for slot in emplacements),
        warnings=avertissements,
    )


def drum_kit_track(kit: DrumKit, name: str = "Batterie") -> ExportTrack:
    """Transforme un kit en piste de projet, prête à être exportée."""
    parametres: Dict[str, float] = {}
    notes: List[ExportNote] = []
    echantillons: Dict[int, str] = {}

    for emplacement in kit.slots:
        # Les paramètres sont numérotés à partir de 1, le chargeur à partir de
        # 0 (voir DrumSlot). Le décalage est ici, une fois, et nulle part
        # ailleurs.
        numero = emplacement.slot + 1
        parametres[f"sampler.slot.{numero}.note"] = float(emplacement.midi_note)
        parametres[f"sampler.slot.{numero}.level"] = 1.0
        # Décroissance à 0 = jouer l'échantillon ENTIER. Il a déjà été découpé
        # à la bonne longueur ; lui imposer une extinction le raccourcirait
        # une seconde fois.
        parametres[f"sampler.slot.{numero}.decay"] = 0.0
        echantillons[emplacement.slot] = emplacement.sample_path

        for instant, velocite in zip(emplacement.onsets, emplacement.velocities):
            notes.append(
                ExportNote(
                    note=emplacement.midi_note,
                    velocity=velocite,
                    start=instant,
                    # La durée ne commande rien sur un sampler percussif : le
                    # coup se joue en entier. On la garde courte pour que le
                    # piano roll reste lisible.
                    duration=0.05,
                )
            )

    notes.sort(key=lambda n: n.start)
    return ExportTrack(
        name=name,
        machine="vsm.sampler",
        parameters=parametres,
        notes=notes,
        is_drums=True,
        samples=echantillons,
        machine_display_name="Sampler (8 emplacements)",
    )


# ---------------------------------------------------------------------------
# Batterie MODÉLISÉE, et voix échantillonnée : la répartition décidée pour la
# version finale.
#
# Jusqu'ici le sampler servait de repli universel — batterie découpée, et tout
# ce qu'aucune machine ne savait faire. La règle est désormais tranchée : LE
# SAMPLER N'EST QUE POUR LA VOIX. Deux conséquences, et les deux fonctions
# ci-dessous les portent.
#
#   - La batterie passe à `vsm.drums`, qui la MODÉLISE (peaux inharmoniques,
#     métal, pièce). La détection de frappes et le classement par familles ne
#     changent pas d'un iota : c'est le même travail, mais son résultat pilote
#     des notes au lieu de charger des échantillons. On y gagne une batterie
#     RÉGLABLE — accorder la caisse claire, ouvrir la pièce — là où un
#     échantillon découpé était figé, et on y perd la fidélité littérale au
#     coup enregistré. Le compromis est assumé et il est mesuré (ARCHITECTURE.md
#     § 33).
#   - La voix, elle, ne se synthétise pas. Le § 6 de la feuille de route le dit
#     depuis longtemps : « hors de portée d'une synthèse par machine ; la
#     séparation la rend déjà disponible en audio, c'est le mieux qu'on puisse
#     en faire honnêtement ». Le stem vocal devient donc UN échantillon, joué
#     tel quel. Ce n'est pas une reconstruction, c'est un report — et l'appeler
#     autrement serait mentir.

# Note de déclenchement de `vsm.drums` pour chaque famille détectée, en
# convention General MIDI. `percussion` n'a pas d'équivalent dans un kit
# modélisé à sept pièces : elle tombe sur le tom aigu, ce qui est faux mais
# audible au bon endroit, plutôt que d'être perdue.
# Correspondance famille détectée -> note de `vsm.drums`.
#
# ELLE DOIT ÊTRE TOTALE, et elle ne l'était pas : `_name_templates` puise les
# noms des gabarits excédentaires dans son vivier `divers` -- `percussion`,
# `tom`, `cymbal`, `tom2`, `tom3` -- et les deux derniers ne figuraient pas
# ici. `modelled_drum_track` les ignorait alors en silence : sur un kit de huit
# gabarits, toutes les frappes d'un `tom2` disparaissaient du projet sans une
# ligne pour le dire. Le chemin sampler, lui, les jouait (il suit
# `emplacement.midi_note`) -- passer la batterie modélisée par défaut perdait
# donc de la musique, ce qui est exactement la panne muette que ce projet
# refuse partout ailleurs.
#
# LE VIVIER `divers` NE PORTE AUCUN SENS TIMBRAL : c'est une réserve de noms
# tirée dans l'ordre quand la famille d'un gabarit a épuisé la sienne. Un
# quatrième gabarit classé « charleston » s'appelle `percussion` sans être une
# percussion. Le seul choix qui vaille est donc de ne PAS faire tomber deux
# gabarits distincts sur la même voix : ils y additionneraient deux timbres que
# la détection avait justement séparés. Les cinq noms du vivier reçoivent les
# cinq voix que les réserves ne prennent pas -- correspondance un pour un, et
# les dix voix de la machine deviennent atteignables, alors que la caisse
# claire d'accompagnement (49) et le tom grave (41) ne l'étaient d'aucune
# façon.
#
# `kick2` et `snare2` restent REPLIÉS sur la voix de leur famille : ce sont
# deux gabarits du même instrument (le premier coup d'un morceau n'a pas de
# queue à soustraire et fonde son propre gabarit), pas deux pièces du kit.
MODELLED_DRUM_NOTES: Dict[str, int] = {
    "kick": 36, "kick2": 36,
    "snare": 38, "snare2": 38,
    "hihat": 42, "pedalhat": 44, "openhat": 46,
    # Le vivier `divers`, dans l'ordre où il est tiré, sur les cinq voix libres.
    "percussion": 49, "tom": 45, "cymbal": 51, "tom2": 48, "tom3": 41,
}


# Correspondance famille -> note pour les BOÎTES À RYTHMES du parc.
#
# Les deux machines suivent la convention General MIDI (TR909Synth.h l. 276,
# TR808Synth.h l. 247) : kick 36, caisse claire 38, clap 39, charleston fermée
# 42, ouverte 46. Ce sont EXACTEMENT les notes que la détection attribue déjà
# aux familles, si bien que la « traduction » que `reconstruire.py` disait non
# mesurée se réduit à quatre renvois : la pédale (44), que ni l'une ni l'autre
# n'a, devient charleston fermée ; les variantes kick2/snare2 rejoignent leur
# pièce ; et ce qu'une machine n'a pas (toms sur la 808, cloche sur la 909)
# est rabattu sur ce qu'elle a de plus proche -- en le DISANT.
DRUM_MACHINE_NOTES: Dict[str, Dict[str, int]] = {
    "vsm.tr909": {
        "kick": 36, "kick2": 36, "snare": 38, "snare2": 38,
        "hihat": 42, "pedalhat": 42, "openhat": 46,
        "percussion": 39, "tom": 45, "tom2": 47, "tom3": 50, "cymbal": 49,
    },
    "vsm.tr808": {
        "kick": 36, "kick2": 36, "snare": 38, "snare2": 38,
        "hihat": 42, "pedalhat": 42, "openhat": 46,
        "percussion": 39, "cymbal": 46,
        # La 808 n'a pas de toms dans cette implémentation : ils vont au clap,
        # la pièce la plus proche en fonction (un coup sec et court), et le kit
        # le dit dans ses avertissements.
        "tom": 39, "tom2": 39, "tom3": 39,
    },
}
DRUM_MACHINE_DISPLAY: Dict[str, str] = {
    "vsm.tr909": "TR-909-style Drum Machine",
    "vsm.tr808": "TR-808-style Drum Machine",
}


def drum_machine_track(kit: DrumKit, machine: str, name: str = "Batterie") -> ExportTrack:
    """
    Le kit détecté joué par une BOÎTE À RYTHMES du parc, patch d'usine.

    POURQUOI CETTE PISTE EXISTE. `reconstruire.py` ne faisait concourir que
    `vsm.drums`, au motif écrit qu'elle « n'avait pas de concurrente crédible ».
    Sur un morceau de techno de 1993, la concurrente crédible est la TR-909 du
    parc -- et c'est une oreille, pas une mesure, qui l'a dit (feuille de route
    § 5 septies) : aucun chiffre ne peut désigner une 909 tant qu'elle n'est
    pas dans la course. Cette fonction l'y met, avec la même règle que pour
    les stems mélodiques -- toutes les machines en concurrence, l'arbitrage sur
    la piste tranche.

    Les instants et les vélocités sont ceux de la détection ; seule la machine
    change. Une famille que la machine n'a pas est rabattue et DITE.
    """
    if machine not in DRUM_MACHINE_NOTES:
        raise ValueError(f"pas de correspondance de notes pour « {machine} »")
    table = DRUM_MACHINE_NOTES[machine]
    notes: List[ExportNote] = []
    for emplacement in kit.slots:
        note = table.get(emplacement.family)
        if note is None:
            note = int(emplacement.midi_note)
            kit.warnings.append(
                f"{emplacement.family} : famille sans voix sur {machine}, "
                f"jouée sur la note {note} ({emplacement.hit_count} frappe(s))"
            )
        elif emplacement.family.startswith("tom") and machine == "vsm.tr808":
            kit.warnings.append(
                f"{emplacement.family} : la TR-808 n'a pas de toms, rabattu sur le clap "
                f"({emplacement.hit_count} frappe(s))"
            )
        for instant, velocite in zip(emplacement.onsets, emplacement.velocities):
            notes.append(ExportNote(note=note, velocity=velocite, start=instant, duration=0.05))
    notes.sort(key=lambda n: n.start)
    return ExportTrack(
        name=name,
        machine=machine,
        parameters={},
        notes=notes,
        is_drums=True,
        machine_display_name=DRUM_MACHINE_DISPLAY.get(machine, machine),
    )


def modelled_drum_track(kit: DrumKit, name: str = "Batterie") -> ExportTrack:
    """
    Transforme un kit détecté en piste `vsm.drums` — modélisée, sans échantillon.

    Les instants et les vélocités sont ceux qu'a trouvés la détection ; seule
    la façon de les jouer change.
    """
    notes: List[ExportNote] = []
    for emplacement in kit.slots:
        note = MODELLED_DRUM_NOTES.get(emplacement.family)
        if note is None:
            # UNE FAMILLE INCONNUE SE JOUE QUAND MÊME, ET SE DIT. Sauter la
            # pièce ferait disparaître ses frappes du projet sans trace ; on la
            # rabat sur la note que la détection lui avait attribuée -- elle
            # vient de la convention General MIDI et vaut mieux que rien -- et
            # on l'inscrit dans les avertissements du kit, que l'appelant
            # imprime. Un kit incomplet est acceptable ; un kit incomplet en
            # silence ne l'est pas.
            note = int(emplacement.midi_note)
            kit.warnings.append(
                f"{emplacement.family} : famille sans voix déclarée dans vsm.drums, "
                f"jouée sur la note {note} ({emplacement.hit_count} frappe(s))"
            )
        for instant, velocite in zip(emplacement.onsets, emplacement.velocities):
            notes.append(ExportNote(note=note, velocity=velocite,
                                     start=instant, duration=0.05))
    notes.sort(key=lambda n: n.start)
    return ExportTrack(
        name=name,
        machine="vsm.drums",
        parameters={},
        notes=notes,
        is_drums=True,
        machine_display_name="Drums (batterie acoustique)",
    )


def vocal_sampler_track(
    audio: np.ndarray,
    sample_rate: int,
    dossier_samples: Path,
    name: str = "Voix",
) -> Optional[ExportTrack]:
    """
    Le stem vocal, reporté tel quel dans le sampler.

    UN échantillon, UNE note au début du morceau. Le découper en phrases
    donnerait un projet plus « musical » à regarder et introduirait des coutures
    audibles pour rien : ce qu'on veut ici, c'est que la voix soit exactement
    la voix.

    La décroissance est réglée à 0 — jouer l'échantillon entier — et l'accord à
    0 : toute transposition serait une altération de l'enregistrement.
    """
    if audio.size < sample_rate // 10:
        return None
    crete = float(np.max(np.abs(audio))) if audio.size else 0.0
    if crete < 1e-4:
        return None
    dossier_samples.mkdir(parents=True, exist_ok=True)
    chemin = dossier_samples / "voix.wav"
    # Gain 1 : on ne normalise pas. Le niveau de la piste est calé plus tard,
    # sur le stem lui-même, comme pour toutes les autres.
    _write_wav(chemin, audio, sample_rate, gain=1.0)
    # NIVEAU : le report doit sortir du sampler à l'identique, et deux facteurs
    # l'en empêchent si on n'y prend pas garde. Mesuré sur le stem vocal de
    # Children : écrit à 0,00334 de niveau efficace, il ressortait à 0,0012,
    # soit 2,8 fois trop faible -- au point que le calage automatique des
    # volumes butait sur sa borne et laissait la voix trop en retrait.
    #
    #   - la VÉLOCITÉ multiplie le niveau (100/127 = 0,79) : on joue donc à 127 ;
    #   - le panoramique est à PUISSANCE CONSTANTE, ce qui coûte 0,707 au
    #     centre : le niveau d'emplacement le compense exactement.
    #
    # Ce n'est pas un défaut du sampler -- les deux comportements sont justes
    # pour un kit, où l'on veut jouer sur la vélocité et placer les pièces. Ils
    # ne conviennent simplement pas à un report intégral, qui ne doit rien
    # ajouter ni retrancher.
    parametres = {
        "sampler.slot.1.note": 60.0,
        "sampler.slot.1.level": 1.0 / 0.70710678,
        "sampler.slot.1.decay": 0.0,
        "sampler.slot.1.tune": 0.0,
        "sampler.slot.1.start": 0.0,
        "sampler.slot.1.pan": 0.0,
    }
    return ExportTrack(
        name=name,
        machine="vsm.sampler",
        parameters=parametres,
        notes=[ExportNote(note=60, velocity=127, start=0.0,
                          duration=float(audio.size) / float(sample_rate))],
        is_drums=True,
        samples={0: f"samples/{chemin.name}"},
        machine_display_name="Sampler (16 emplacements)",
    )
