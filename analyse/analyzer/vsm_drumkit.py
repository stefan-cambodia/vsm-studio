"""
Construction d'un KIT DE BATTERIE pour le sampler, à partir d'un stem.

C'est l'étape 9.5 de la feuille de route. Jusqu'ici le stem de batterie était
explicitement ignoré : aucune machine du parc ne reproduit une batterie
enregistrée par synthèse, et produire une piste fausse aurait été pire que de
le dire.

LE PRINCIPE, et pourquoi il fonctionne là où la synthèse échoue : on ne cherche
pas à *fabriquer* le son d'une caisse claire, on RÉUTILISE celle de
l'enregistrement. Le stem est découpé en coups, les coups sont regroupés par
famille, et un représentant de chaque famille devient un échantillon du kit.
La reconstruction du percussif devient alors quasi exacte -- c'est le seul
endroit de la chaîne où l'on peut viser cela.

CE QU'ON NE FAIT PAS, et pourquoi :

  - On ne moyenne PAS les coups d'une même famille. Deux coups ne sont jamais
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


# Bandes de détection. Une batterie se lit par bandes, pas par spectre global :
# la grosse caisse vit sous 150 Hz, la caisse claire dans le médium, les
# cymbales au-dessus de 5 kHz. Chacune reçoit sa propre détection d'attaques.
#
# POURQUOI PAS UN SPECTRE GLOBAL PAR COUP -- c'est la première version, et elle
# échouait franchement. Quand la grosse caisse et la charleston frappent
# ENSEMBLE, il n'y a qu'une attaque, et son spectre mêlé bascule vers l'aigu :
# sur un motif de quatre mesures contenant 8 grosses caisses, 8 caisses claires
# et 32 charlestons, la détection globale ne trouvait que 30 coups, dont AUCUNE
# grosse caisse et AUCUNE caisse claire. Un spectre mélangé ne se classe pas.
DETECTION_BANDS: List[Tuple[str, float, float, int]] = [
    # (famille, fréquence basse, fréquence haute, note MIDI)
    ("kick", 20.0, 150.0, 36),
    ("snare", 180.0, 2500.0, 38),
    ("hihat", 5000.0, 16000.0, 42),
]

# Écart, dans la distribution des niveaux, au-delà duquel on considère qu'il
# sépare « la pièce est frappée » de « la pièce résonne encore ». Voir
# `_hits_from_levels` : c'est le cœur de la détection, et le chiffre est
# mesuré, pas choisi.
LEVEL_GAP_THRESHOLD = 0.15

# Plancher de niveau : en dessous, l'énergie de la bande à cette attaque est
# une fuite d'une autre pièce, pas une frappe.
LEVEL_FLOOR = 0.25

# Une bande dont la crête n'atteint pas cette part de la bande la plus forte
# n'est pas jouée du tout -- et on ne l'invente pas.
BAND_PRESENCE_RATIO = 0.02

# Deux attaques plus proches que cela sont considérées SIMULTANÉES : c'est le
# seuil en deçà duquel l'oreille n'entend plus deux frappes mais une seule.
SIMULTANEITY_SECONDS = 0.03

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


def _band_envelopes(
    audio: np.ndarray, sample_rate: int, bandes: Sequence[Tuple[float, float]]
) -> List[np.ndarray]:
    """
    Enveloppe d'énergie de chaque bande, dans le temps.

    POURQUOI UN SPECTROGRAMME ET PAS UN FILTRE. La première version filtrait
    chaque bande par transformée sur le FICHIER ENTIER, en mettant à zéro les
    fréquences hors bande. C'est un mur de briques en fréquence -- donc un
    étalement dans le temps : chaque transitoire se répand sur des centaines de
    millisecondes.

    L'effet était mesurable et il a coûté deux itérations : dans la bande
    aiguë, l'énergie devenait CONTINUE, si bien que le rapport « après/avant »
    valait 1 à chaque frappe et que la charleston n'était détectée que 12 fois
    sur 32. Un spectrogramme, lui, mesure l'énergie là où elle est.
    """
    spectre = np.abs(
        np.fft.rfft(
            np.lib.stride_tricks.sliding_window_view(audio, STFT_WINDOW)[::STFT_HOP]
            * np.hanning(STFT_WINDOW),
            axis=1,
        )
    )
    frequences = np.fft.rfftfreq(STFT_WINDOW, 1.0 / sample_rate)
    enveloppes = []
    for basse, haute in bandes:
        masque = (frequences >= basse) & (frequences < haute)
        enveloppes.append(np.sqrt(np.sum(spectre[:, masque] ** 2, axis=1)))
    return enveloppes


def _hits_from_levels(niveaux: np.ndarray) -> np.ndarray:
    """
    Décide, pour une bande, à quelles attaques la pièce est FRAPPÉE.

    `niveaux` est la PART de cette bande dans l'énergie de chaque attaque.

    Le critère qui marche, et pourquoi les autres échouent. On a essayé, dans
    l'ordre, de classer le spectre de chaque coup, puis de détecter les
    attaques bande par bande, puis de mesurer une MONTÉE d'énergie à chaque
    attaque. Les trois butent sur le même écueil : une pièce jouée EN CONTINU
    ne monte pas, elle ne s'arrête jamais. Sur un motif de charleston à la
    double-croche, le test de montée n'en voyait que 12 sur 32.

    Ce qui distingue vraiment, c'est la FORME DE LA DISTRIBUTION des niveaux de
    la bande, relevés à chaque attaque :

      - une pièce jouée par intermittence donne une distribution BIMODALE, avec
        un écart franc entre « frappée » et « résonne encore » ;
      - une pièce jouée à chaque temps donne une distribution CONTINUE.

    On coupe donc au plus grand écart -- s'il est franc. S'il ne l'est pas,
    c'est que la pièce joue partout, et on garde tout ce qui dépasse le
    plancher.

    CE QUE CETTE RÈGLE NE SAIT PAS FAIRE, et il faut le dire précisément.
    Mesuré sur deux motifs de quatre mesures joués par une boîte à rythmes :

        motif                      grosse caisse   caisse claire   charleston
        charleston à la double     8 / 8           8 / 8            8 / 32
        charleston aux contretemps 8 / 8           8 / 8            8 / 16

    La grosse caisse et la caisse claire sont exactes dans les deux cas ; la
    charleston est SOUS-détectée. C'est le compromis retenu, et il est
    délibéré : une frappe manquante s'entend comme un motif plus clairsemé,
    une frappe inventée s'entend comme une faute. Une version antérieure,
    fondée sur le niveau des bandes plutôt que sur leur part, trouvait les 32
    charlestons du premier motif mais en inventait 16 dans le second.

    La cause est physique et non réglable : la caisse claire d'une boîte à
    rythmes est faite de bruit, et son énergie entre 5 et 16 kHz atteint celle
    d'une charleston. Les séparer demande des gabarits spectraux -- une
    décomposition en familles apprises --, c'est-à-dire une autre technique,
    pas un seuil mieux choisi.
    """
    if niveaux.size == 0:
        return np.zeros(0, dtype=bool)
    maximum = float(np.max(niveaux))
    if maximum <= 0.0:
        return np.zeros(niveaux.size, dtype=bool)

    relatifs = niveaux / maximum
    ordre = np.argsort(relatifs)[::-1]
    tries = relatifs[ordre]

    # Plus grand écart entre deux niveaux consécutifs, en ignorant la position 0
    # (couper là voudrait dire « une seule frappe », ce qui n'arrive pas sur un
    # stem de batterie).
    ecarts = tries[:-1] - tries[1:]
    coupe = int(np.argmax(ecarts)) if ecarts.size else -1
    plus_grand = float(ecarts[coupe]) if coupe >= 0 else 0.0

    garde = np.zeros(niveaux.size, dtype=bool)
    if plus_grand > LEVEL_GAP_THRESHOLD:
        garde[ordre[: coupe + 1]] = True
    else:
        garde[relatifs > LEVEL_FLOOR] = True
    return garde


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

    enveloppes = _band_envelopes(
        audio, sample_rate, [(basse, haute) for _, basse, haute, _ in DETECTION_BANDS]
    )

    def trame_de(echantillon: int) -> int:
        # Une trame COMMENCE à l'échantillon qu'elle indexe et couvre la
        # fenêtre entière : son contenu est donc centré une demi-fenêtre plus
        # loin. Oublier ce décalage donnait 23 ms de regard EN AVANT, si bien
        # que la fenêtre précédant l'attaque contenait déjà l'attaque.
        return (echantillon - STFT_WINDOW // 2) // STFT_HOP

    def energie(enveloppe: np.ndarray, centre: int, bornes: Tuple[float, float]) -> float:
        debut = max(0, trame_de(centre + int(bornes[0] * sample_rate)))
        fin = min(enveloppe.size, trame_de(centre + int(bornes[1] * sample_rate)) + 1)
        if fin <= debut:
            return 0.0
        return float(np.max(enveloppe[debut:fin]))

    cretes = [float(np.max(e)) for e in enveloppes]
    crete_maximale = max(cretes) if cretes else 0.0

    # PART DE CHAQUE BANDE dans l'énergie de l'attaque, et non son niveau brut.
    #
    # C'est la mesure qui distingue réellement les pièces, et le niveau ne le
    # fait pas : une caisse claire de boîte à rythmes est faite de bruit, et
    # son énergie entre 5 et 16 kHz atteint celle d'une charleston. Mesuré sur
    # un motif où la charleston ne joue QUE les contretemps, aucun seuil de
    # niveau ne séparait les deux -- la distribution allait de 1,00 à 0,29 sans
    # le moindre palier. Rapportée à l'énergie totale de l'attaque, en
    # revanche, la grosse caisse et la caisse claire se détachent exactement,
    # dans ce motif comme dans celui où la charleston joue partout.
    niveaux_par_bande = np.array(
        [[energie(enveloppes[i], instant, AFTER_WINDOW) for i in range(len(DETECTION_BANDS))]
         for instant in instants]
    )
    totaux = np.maximum(niveaux_par_bande.sum(axis=1, keepdims=True), 1e-12)
    parts = niveaux_par_bande / totaux

    detections: Dict[str, Tuple[int, np.ndarray, List[int]]] = {}
    for index, (famille, basse, haute, note) in enumerate(DETECTION_BANDS):
        if crete_maximale <= 0.0 or cretes[index] < crete_maximale * BAND_PRESENCE_RATIO:
            continue  # bande muette : cette pièce n'est pas jouée, et on ne l'invente pas
        enveloppe = enveloppes[index]
        garde = _hits_from_levels(parts[:, index])
        debuts = [instant for instant, retenu in zip(instants, garde) if retenu]
        if debuts:
            detections[famille] = (note, enveloppe, debuts)

    if not detections:
        return None

    # --- coups isolés ---------------------------------------------------------
    # L'échantillon d'une famille doit venir d'une frappe où AUCUNE autre pièce
    # ne sonne : prélever une grosse caisse pendant une charleston mettrait la
    # charleston dans l'échantillon de grosse caisse, et on l'entendrait à
    # chaque frappe du kit reconstruit.
    tous_les_debuts = {f: set(d) for f, (_, _, d) in detections.items()}
    fenetre = int(SIMULTANEITY_SECONDS * sample_rate)

    def est_isole(famille: str, debut: int) -> bool:
        for autre, debuts in tous_les_debuts.items():
            if autre == famille:
                continue
            if any(abs(d - debut) < fenetre for d in debuts):
                return False
        return True

    crete_globale = float(np.max(np.abs(audio))) or 1.0
    # Facteur commun à tout le kit : la frappe la plus forte du stem sort à
    # 0,9, les autres gardent leur niveau relatif.
    # 0,7 et non 0,9 : plusieurs pièces frappent souvent ensemble et leurs
    # échantillons s'additionnent. Viser 0,9 pour la frappe la plus forte
    # faisait écrêter le kit reconstruit dès que deux pièces coïncidaient.
    gain_du_kit = 0.7 / crete_globale
    emplacements: List[DrumSlot] = []
    avertissements: List[str] = []

    ordre = {nom: rang for rang, (nom, *_) in enumerate(DETECTION_BANDS)}
    familles = sorted(detections.items(),
                      key=lambda item: (-len(item[1][2]), ordre.get(item[0], 99)))[:max_slots]

    for position, (famille, (note, bande, debuts)) in enumerate(familles):
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
