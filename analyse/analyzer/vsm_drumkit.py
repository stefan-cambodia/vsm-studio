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


def build_drum_kit(
    audio: np.ndarray,
    sample_rate: int,
    samples_folder: Path,
    relative_prefix: str = "samples",
    max_slots: int = 16,
) -> Optional[DrumKit]:
    """
    Découpe un stem de batterie en kit jouable par le sampler.

    La détection se fait BANDE PAR BANDE, chacune donnant sa propre famille :
    c'est la seule façon d'entendre une grosse caisse et une charleston jouées
    au même instant, ce qui est le cas le plus courant qui soit.

    Renvoie None si aucun coup n'est détecté -- ce qui est DIT par l'appelant,
    jamais compensé par un kit inventé.

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

    # Frappes de chaque pièce : part suffisante de la nouveauté de l'attaque.
    detections: Dict[str, Tuple[int, List[int]]] = {}
    for index, (famille, note) in enumerate(noms):
        debuts = [instant for i, instant in enumerate(instants)
                  if parts[i, index] >= ASSIGN_SHARE]
        if debuts:
            detections.setdefault(famille, (note, debuts))

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
