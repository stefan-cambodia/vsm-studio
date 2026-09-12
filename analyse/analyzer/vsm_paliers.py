"""Combien de TIMBRES se sont installés dans un stem (H26, § 12.8 du CDC multipiste).

POURQUOI CETTE MESURE EXISTE. La porte du fourre-tout demandait polyphonie
moyenne >= 3 ET ambitus >= 36 demi-tons, toutes deux lues sur les NOTES
transcrites. Sur *Children*, le stem `other` porte quatre parties a 2,58 de
polyphonie moyenne : la porte ne s'ouvre pas, et le decoupage en voix n'est
JAMAIS ESSAYE (§ 12.4). La raison tient a la musique : dans ce morceau les
parties sont SUCCESSIVES autant que superposees — le piano entre a 28 s, le lead
vers 189 s —, et deux parties qui ne sonnent pas ensemble ne font pas monter la
polyphonie moyenne.

CE QUI A ETE REFUTE AVANT D'ARRIVER ICI, et qui explique la forme de ce module.
Une premiere statistique mesurait la DISPERSION du profil de bandes autour de sa
mediane (§ 12.6). Elle est morte deux fois (§ 12.7) : les stems de FUITE de
*Children* — des residus de separation a 0,2 et 0,6 % de l'energie — la
dominaient, parce qu'un son rare et erratique n'a pas de timbre stable, et une
porte batie dessus aurait decoupe en quatre voix un residu, c'est-a-dire
fabrique des pistes.

CE QUI SEPARE LES DEUX CAS EST LA PERSISTANCE. Une partie qui entre fait une
MARCHE : un profil neuf qui s'installe et DURE. Un residu fait une oscillation :
chaque fenetre differe de la suivante et rien ne tient. On compte donc des
PALIERS, pas de l'ecart.

LES DEUX SEUILS SONT ECRITS AU § 12.8, AVANT LA MESURE, et ils n'ont pas bouge
apres elle : distance L1 de 0,30 entre deux profils voisins, palier compte a
partir de 4 fenetres (20 s). Mesure publiee au § 12.9 : `other` 4 timbres,
`bass` 3, `piano` 1, `guitar` 0, `vocals` 0.
"""

from __future__ import annotations

from typing import List, Tuple

import numpy as np

FENETRE_SECONDES = 5.0
PLANCHER_DB = -50.0
SEUIL_L1 = 0.30
DUREE_MINIMALE = 4          # fenetres, soit 20 s
NOMBRE_DE_BANDES = 8
BANDES = np.geomspace(60.0, 16000.0, NOMBRE_DE_BANDES + 1)


def profil_de_bandes(bloc: np.ndarray, sample_rate: int) -> np.ndarray:
    """Le TIMBRE d'une fenetre : huit bandes log, normalisees a somme 1.

    Normalise, donc insensible au NIVEAU : une partie qui joue plus fort ne doit
    pas passer pour une autre partie.
    """
    spectre = np.abs(np.fft.rfft(bloc * np.hanning(len(bloc))))
    freqs = np.fft.rfftfreq(len(bloc), 1.0 / sample_rate)
    bandes = np.array(
        [float(spectre[(freqs >= BANDES[i]) & (freqs < BANDES[i + 1])].sum()) ** 2
         for i in range(NOMBRE_DE_BANDES)], dtype=np.float64)
    total = bandes.sum()
    return bandes / total if total > 0 else bandes


def timbres_installes(audio: np.ndarray, sample_rate: int) -> Tuple[int, List[Tuple[float, float]]]:
    """Rend (nombre de timbres installes, plages [(debut, duree) en secondes]).

    Un TROU DE SILENCE ROMPT LE PALIER : ce qui reprend apres n'est pas la meme
    tenue, et coller les deux ferait passer deux entrees d'une meme partie pour
    une seule. C'est aussi ce qui met les residus de separation a zero.
    """
    n = int(FENETRE_SECONDES * sample_rate)
    if n <= 0 or audio.size < n:
        return 0, []
    fenetres: List[Tuple[int, np.ndarray]] = []
    for k, debut in enumerate(range(0, audio.size - n + 1, n)):
        bloc = audio[debut:debut + n]
        rms = float(np.sqrt(np.mean(np.square(bloc, dtype=np.float64))))
        if rms <= 0.0 or 20.0 * np.log10(rms) < PLANCHER_DB:
            continue
        fenetres.append((k, profil_de_bandes(bloc, sample_rate)))
    if not fenetres:
        return 0, []

    groupes: List[List[Tuple[int, np.ndarray]]] = []
    courant = [fenetres[0]]
    for precedent, suivant in zip(fenetres, fenetres[1:], strict=False):
        rupture = (suivant[0] != precedent[0] + 1
                   or float(np.abs(suivant[1] - precedent[1]).sum()) >= SEUIL_L1)
        if rupture:
            groupes.append(courant)
            courant = []
        courant.append(suivant)
    groupes.append(courant)

    longs = [g for g in groupes if len(g) >= DUREE_MINIMALE]
    distincts: List[np.ndarray] = []
    plages: List[Tuple[float, float]] = []
    for groupe in longs:
        median = np.median(np.vstack([p for _, p in groupe]), axis=0)
        if all(float(np.abs(median - d).sum()) >= SEUIL_L1 for d in distincts):
            distincts.append(median)
        plages.append((groupe[0][0] * FENETRE_SECONDES, len(groupe) * FENETRE_SECONDES))
    return len(distincts), plages


def plainte_de_paliers(nombre: int, plages: List[Tuple[float, float]]) -> str:
    """La phrase du journal, ou "" si le stem ne porte qu'un timbre.

    DITE ET NON DEVINEE : le § 8.3 du cahier interdit qu'une porte s'ouvre sans
    qu'on sache pourquoi, et le compte seul ne dit pas OU les parties entrent.
    """
    if nombre < 2:
        return ""
    ou = ", ".join(f"{debut:.0f}-{debut + duree:.0f} s" for debut, duree in plages[:5])
    return (f"ATTENTION : ce stem porte {nombre} timbres qui s'INSTALLENT — des parties "
            f"y entrent et en sortent ({ou}"
            f"{', …' if len(plages) > 5 else ''}). La polyphonie ne les voit pas : "
            f"deux parties qui ne sonnent pas ensemble ne la font pas monter.")


def timbres_et_medians(audio: np.ndarray, sample_rate: int):
    """Les timbres installes ET leurs profils medians, pour classer ensuite.

    Meme calcul que `timbres_installes`, dont c'est la version qui rend de quoi
    travailler. Les deux partagent leur code pour qu'un seuil ne puisse pas
    diverger entre la porte et le decoupeur.
    """
    n = int(FENETRE_SECONDES * sample_rate)
    if n <= 0 or audio.size < n:
        return [], []
    fenetres: List[Tuple[int, np.ndarray]] = []
    for k, debut in enumerate(range(0, audio.size - n + 1, n)):
        bloc = audio[debut:debut + n]
        rms = float(np.sqrt(np.mean(np.square(bloc, dtype=np.float64))))
        if rms <= 0.0 or 20.0 * np.log10(rms) < PLANCHER_DB:
            continue
        fenetres.append((k, profil_de_bandes(bloc, sample_rate)))
    if not fenetres:
        return [], []
    groupes: List[List[Tuple[int, np.ndarray]]] = []
    courant = [fenetres[0]]
    for precedent, suivant in zip(fenetres, fenetres[1:], strict=False):
        if (suivant[0] != precedent[0] + 1
                or float(np.abs(suivant[1] - precedent[1]).sum()) >= SEUIL_L1):
            groupes.append(courant)
            courant = []
        courant.append(suivant)
    groupes.append(courant)
    medians: List[np.ndarray] = []
    for groupe in groupes:
        if len(groupe) < DUREE_MINIMALE:
            continue
        median = np.median(np.vstack([p for _, p in groupe]), axis=0)
        if all(float(np.abs(median - d).sum()) >= SEUIL_L1 for d in medians):
            medians.append(median)
    return fenetres, medians


def voix_par_paliers(notes, audio: np.ndarray, sample_rate: int):
    """H30 : DECOUPER PAR LE TEMPS, la ou le decoupeur de registres coupe la hauteur.

    POURQUOI CE DECOUPEUR EXISTE, MESURE A L'APPUI. Sur `other` de *Children*,
    le decoupage par registres rend quatre voix qui jouent chacune 81 a 99 % du
    morceau et touchent trois a cinq paliers sur cinq (§ 12.11) : elles sont
    toutes partout, alors que les parties ENTRENT ET SORTENT. Un decoupeur de
    hauteur ne peut pas separer des parties qui se succedent — ce n'est pas un
    reglage a trouver, c'est la mauvaise dimension.

    CE QU'IL REND EST UNE SECTION, PAS UNE PARTIE — ET C'EST POURQUOI LA CHAINE
    NE L'APPELLE PAS. Mesure du 12/09 sur `other` de *Children* (§ 12.12) : les
    quatre voix se concentrent bien dans le temps (support reel 15 %, 55 %, 12 %,
    18 %, contre 68 a 94 % pour le decoupage par registres) — le critere de H30
    est tenu. Mais chacune couvre MIDI 29-96, l'ambitus ENTIER : une voix
    temporelle contient TOUT ce qui joue pendant sa periode. Donner une machine a
    chacune ferait CHANGER DE MACHINE un meme instrument d'une section a l'autre,
    ce qui est pire que le fourre-tout qu'on soigne.

    CE MODULE RESTE DONC UNE BRIQUE, ET PAS UN DECOUPEUR DE PISTES. Ce qu'il
    apporte est la CARTE des entrees et des sorties ; ce qu'il faudrait est
    decouper par la HAUTEUR a l'interieur de chaque palier, puis APPARIER les
    voix d'un palier a l'autre par leur timbre pour en faire des parties
    continues (H31, § 12.12). Il est garde par des tests et publie ici pour cela.

    Chaque fenetre SONORE est rangee sous le timbre installe dont elle est la
    plus proche ; une note appartient a la fenetre ou elle COMMENCE. Les voix
    rendues suivent l'ordre des timbres, et une voix vide n'est pas rendue.
    """
    fenetres, medians = timbres_et_medians(audio, sample_rate)
    if len(medians) < 2:
        return [list(notes)]
    par_fenetre = {}
    for index, profil in fenetres:
        distances = [float(np.abs(profil - m).sum()) for m in medians]
        par_fenetre[index] = int(np.argmin(distances))
    groupes: List[List] = [[] for _ in medians]
    orphelines: List = []
    for note in notes:
        index = int(float(note.start) // FENETRE_SECONDES)
        timbre = par_fenetre.get(index)
        if timbre is None:
            orphelines.append(note)      # note tombee dans une fenetre SILENCIEUSE
            continue
        groupes[timbre].append(note)
    # LES ORPHELINES NE SE PERDENT PAS : une note qui commence dans une fenetre
    # jugee silencieuse (une attaque juste avant le seuil) rejoint le timbre de
    # la fenetre voisine la plus proche dans le temps. Les jeter serait une panne
    # muette ; leur ouvrir une voix serait inventer une partie.
    for note in orphelines:
        index = int(float(note.start) // FENETRE_SECONDES)
        voisines = sorted(par_fenetre, key=lambda k: abs(k - index))
        if voisines:
            groupes[par_fenetre[voisines[0]]].append(note)
    for groupe in groupes:
        groupe.sort(key=lambda n: float(n.start))
    return [g for g in groupes if g]


def voix_par_paliers_et_registres(notes, audio: np.ndarray, sample_rate: int,
                                  maximum: int = 4):
    """H31 : la HAUTEUR a l'interieur de chaque palier, puis l'APPARIEMENT.

    POURQUOI LES DEUX DIMENSIONS, MESURE A L'APPUI. Couper par la hauteur seule
    rend quatre voix qui jouent 68 a 94 % du morceau (§ 12.11) ; couper par le
    temps seul rend quatre SECTIONS qui couvrent l'ambitus entier (§ 12.12).
    Aucune ne suffit, parce qu'une partie est precisement ce qui a UNE hauteur
    ET UNE presence : un registre qui apparait, dure, et disparait.

    COMMENT. Chaque palier — une periode ou la texture est stable — est decoupe
    par registres : la, les registres sont ceux des parties qui jouent ALORS, et
    non une moyenne de tout le morceau. Les sous-voix de tous les paliers sont
    ensuite APPARIEES par la proximite de leur hauteur mediane : une partie est
    la chaine de ses apparitions.

    CE QUE CELA NE SAIT PAS FAIRE, ET QUI EST MESURE : separer deux parties qui
    partagent le MEME registre AU MEME MOMENT. C'est le cas de `other` sur
    *Children*, et le § 12.13 en porte les chiffres — trois voix d'ambitus 24, 17
    et **43** demi-tons, de support 94, 63 et 86 %. Le critere de H31 (ambitus
    sous 30) tombe sur la troisieme, et le decoupage ne vaut pas mieux que celui
    par registres seuls.

    CETTE FONCTION N'EST DONC PAS APPELEE PAR LA CHAINE. Elle est gardee, avec
    ses mesures, parce qu'un resultat negatif se publie : sur ce disque les
    quatre parties se recouvrent DANS LES DEUX DIMENSIONS, et aucun decoupage au
    niveau des NOTES ne peut les separer. Ce qui le pourrait est en amont — une
    meilleure separation de sources, chantier C1 de l'INDEX.
    """
    from .vsm_reconstruct import separer_en_voix

    fenetres, medians = timbres_et_medians(audio, sample_rate)
    if len(medians) < 2 or maximum <= 1:
        return [list(notes)]

    par_fenetre = {}
    for index, profil in fenetres:
        par_fenetre[index] = int(np.argmin([float(np.abs(profil - m).sum()) for m in medians]))

    # 1) les notes, rangees par palier (la fenetre ou elles COMMENCENT)
    segments: List[List] = [[] for _ in medians]
    for note in notes:
        index = int(float(note.start) // FENETRE_SECONDES)
        timbre = par_fenetre.get(index)
        if timbre is None:
            voisines = sorted(par_fenetre, key=lambda k: abs(k - index))
            timbre = par_fenetre[voisines[0]] if voisines else 0
        segments[timbre].append(note)

    # 2) la hauteur A L'INTERIEUR de chaque palier
    sous_voix: List[Tuple[float, List]] = []
    for segment in segments:
        if len(segment) < 2:
            if segment:
                sous_voix.append((float(segment[0].note), list(segment)))
            continue
        for groupe in separer_en_voix(segment, maximum, justifie=True):
            if groupe:
                mediane = float(np.median([float(n.note) for n in groupe]))
                sous_voix.append((mediane, list(groupe)))
    if not sous_voix:
        return [list(notes)]

    # 3) L'APPARIEMENT : les sous-voix se rassemblent par proximite de hauteur.
    #    Le nombre de parties est celui du palier le plus riche — il n'est pas
    #    impose : un morceau ou chaque palier porte deux registres n'en aura pas
    #    quatre parce qu'il y a quatre paliers.
    sous_voix.sort(key=lambda mv: mv[0])
    medianes = np.array([m for m, _ in sous_voix], dtype=np.float64)
    # Regroupement par SEUIL plutot que par k fixe : deux sous-voix appartiennent
    # a la meme partie si leurs medianes sont a moins d'une octave. Un seuil dit
    # ce qu'il fait ; un k impose le nombre de parties avant de les avoir vues.
    parties: List[List] = []
    courante: List = list(sous_voix[0][1])
    ancre = medianes[0]
    for (mediane, groupe) in sous_voix[1:]:
        if mediane - ancre <= 12.0:
            courante.extend(groupe)
        else:
            parties.append(courante)
            courante = list(groupe)
            ancre = mediane
    parties.append(courante)
    for partie in parties:
        partie.sort(key=lambda n: float(n.start))
    return [p for p in parties if p]
