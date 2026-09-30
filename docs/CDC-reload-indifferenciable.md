# « Reload » (Peschi, Original Mix) — une reconstruction qu'on ne distingue pas de l'original

*Chantier ouvert le 30/09/2026 à 07 h 45, à la demande de l'utilisateur :*

> « analyse ce fichier et reconstruit le si necessaire creer de nouveaux instruments
> machine, pour le faire, la reconstruction doit etre indifférenciable que l'original
> — /home/stefan/Téléchargements/Reload - Peschi Original Mix.mp4 »

*Ce document est écrit AVANT la mesure de la course de référence (§ 2) : les seuils
du § 0 ne bougeront plus après avoir vu ses chiffres. Il passe devant l'audit du DAW
(D525, écrite et mise en attente) et devant la campagne S2 (gelée pendant les courses
de ce chantier, `CDC-recensement-des-sources.md` § 14.2).*

---

## 0. Ce que « indifférenciable » veut dire ici — et ce qui ne se mesure pas

**La seule preuve est une écoute à l'aveugle** (ABX) par l'utilisateur. La chaîne
n'écoute pas. Ce qu'elle peut faire, c'est tenir des **conditions nécessaires** :
les rater prouve qu'on entendra la différence ; les tenir ne prouve pas qu'on ne
l'entendra pas. Ce document dit donc, à chaque étape, lesquelles sont tenues — et ne
dira jamais « indifférenciable » sur la foi d'un chiffre.

**L'instrument : `tools/ecart-a-l-original.py`**, écrit pour ce chantier et **validé
avant usage** sur des perturbations connues d'un extrait de 60 s :

| perturbation | ce que l'outil lit |
|---|---|
| aucune (l'extrait contre lui-même) | 0,0 cent, 0,00 dB partout, 0,0 ms |
| −13 cents (rééchantillonné, sans vocodeur) | −13,5 cents |
| +20 cents | +20,5 cents |
| retard de 10 ms | +10,2 ms (attaques), +9,98 ms (corrélation) |
| −3 dB | décalage −3,00 dB, équilibre 0,00 dB |

Sa première version a **échoué deux fois** à cette validation, et c'est écrit : le
diapason par `librosa.estimate_tuning` sur le mélange lisait +7 cents là où les pics
du pad disent +13, et une transposition de −13 cents ne le faisait bouger que de 5
(la batterie noie les partiels) — remplacé par une moyenne circulaire des écarts des
pics tenus ; et un pas d'analyse de 5,8 ms lisait un retard de 10 ms comme 11,6 —
ramené à 1,45 ms.

**LE PLANCHER** : l'original contre son propre réencodage AAC à 128 kb/s (la source
est elle-même un AAC à ~130 kb/s) — deux fichiers qu'une oreille ne distingue
généralement pas. Mesuré le 30/09 sur le morceau entier (43 tranches de 4 mesures) :

| mesure | plancher (AAC contre original) |
|---|---|
| diapason | +12,3 / +12,2 cents — écart −0,1 |
| niveau | décalage −0,05 dB, pire tranche 0,09 dB |
| équilibre par bande (écart médian) | ≤ 0,07 dB sur les six bandes ; tranches au-delà de 1 dB : 0 à 1 sur 43, sauf la basse (6/43) |
| calage du kick | médian 0,0 ms, \|médian\| 1,4 ms, p90 4,3 ms (le bruit du détecteur d'attaques) |
| largeur stéréo (side/mid) | 0,0478 / 0,0486 |
| log-mel | écart moyen **0,66 dB**, médian 0,40 dB |

**LES SEUILS, posés ici avant toute course** :

| condition | seuil | pourquoi |
|---|---|---|
| diapason | \|écart\| ≤ 5 cents | la plus petite différence de hauteur perçue sur un son tenu est de l'ordre de 5 cents |
| niveau | \|décalage\| ≤ 0,5 dB, pire tranche ≤ 1 dB autour du décalage | 1 dB en large bande se perçoit |
| équilibre | par bande : \|écart médian\| ≤ 1 dB, et ≥ 90 % des tranches à ≤ 3 dB | une coloration de 1 dB sur une octave s'entend en A/B ; 3 dB se remarquent |
| calage du kick | \|médian\| ≤ 2 ms, p90 ≤ 6 ms | le plancher du détecteur est 1,4 / 4,3 ms |
| largeur stéréo | à ± 30 % de l'original | le morceau est presque mono (0,048) |
| log-mel | écart moyen ≤ **1,3 dB** (deux fois le plancher) | c'est le seul chiffre qui voit le timbre trame à trame ; son seuil est RELATIF, faute d'un seuil d'audibilité publié |

La distance globale de la chaîne (`rapport.json`) est publiée à côté, à métrique,
budget et stems identiques, comme partout dans le dépôt.

---

## 1. Portrait de l'original (mesuré le 30/09/2026)

- **Source** : `.mp4` (vidéo h264 640 × 480, audio AAC 44,1 kHz stéréo, ~130 kb/s),
  312,2 s. Converti en `reconstruction/sources/reload-peschi.wav` (PCM 16 bits) —
  `soundfile` ne lit pas le `.mp4` (payé le 24/09, H37).
- **Tempo 136 BPM** ; **diapason +12,3 cents** au-dessus de 440 Hz sur le morceau
  entier (302 pics, concentration 0,94 ; +13 sur le pad seul, 15-34 s) — la
  reconstruction, accordée à 440 Hz, jouerait chaque note ~12 cents trop bas.
- **Presque mono** (énergie side/mid 0,048) ; RMS −16,3 dBFS, crête 0,94.
- **Un accord tenu** — si, do♯, mi, fa♯, la♯ (de mi3 à do♯5) — **presque
  sinusoïdal** : le deuxième partiel du fa♯4 est 34 dB sous le fondamental, rien
  entre 880 et 1 760 Hz ; des battements lents (largeur à −6 dB d'un pic : 2 Hz,
  pour une résolution de 0,67), sans porte rythmique régulière (autocorrélation de
  l'enveloppe ≤ 0,49, périodes erratiques).
- **Un kick sur chaque temps**, fondamental vers 86 Hz, longue décroissance — c'est
  lui qui domine la bande 60-150 Hz des sections pleines, pas une basse séparée ;
  une **basse** vers fa♯2 dans les sections pleines (elle est dans le stem « other »,
  voix 4) ; **charleston** (1 692 frappes), **caisse claire / clap** (192).
- **Structure** (énergie par bande, tranches de 4 mesures) : intro percussions
  seules (0-14 s) ; le pad entre (14 s) ; la basse (42 s) ; pleines 56-84 s,
  98-127 s, 176-211 s, 218-268 s ; ponts 84-92 s, 127-162 s, 211-218 s ; sortie
  268-305 s.
- **Séparation** (htdemucs_6s) : drums 69,0 %, other 29,2 %, piano 0,9 % (le même
  pad une octave plus haut, 84-112 s et 225-253 s), vocals 0,5 %, guitar 0,3 %,
  bass 0,2 % — les deux derniers sous le seuil de 0,5 %, non reconstruits.

---

## 2. La course de référence — la chaîne telle qu'elle est

Lancée le 30/09 à 07 h 48 avec les options par défaut (celles de *B4 Wuz Then* :
séparation `htdemucs_6s`, parité, sampler, arbitrages, 6 rendus parallèles),
`reconstruction/travail/reload-peschi/`, la campagne S2 gelée pendant la course. Le
poste s'est mis en veille au seuil de batterie (≈ 08 h 15 → 10 h 20) ; la course a
repris au réveil.

*Résultats : à écrire à la fin de la course, par l'outil du § 0 et le rapport.*

---

## 3. Ce qui s'entend déjà, avant tout chiffre — et qui fera les hypothèses

Relevé dans le journal de la course et dans l'analyse du § 1, pendant qu'elle
tournait. Rien de ceci n'est encore mesuré sur la reconstruction :

1. **Le diapason n'est estimé nulle part dans la chaîne** (cherché : aucun module
   d'`analyse/` ne l'estime ; les 59 machines calculent chacune
   `440 · 2^((n−69)/12)`). Toute la partie mélodique sera ~12 cents trop basse.
2. **Le pad tenu devient une pluie de notes brèves** : la voix 1 compte 972 notes
   sur MIDI 70-73, ~4 par seconde, pour un accord qui tient des mesures entières.
   Les machines qui gagnent l'arbitrage sont des machines FRAPPÉES ou PINCÉES
   (`vsm.scanned` 0,316 puis 0,278 ; derrière : perc, harpe, carillon, kalimba) —
   ce qui est cohérent avec des notes rejouées, pas avec un pad. À vérifier sur le
   MIDI écrit.
3. **Un seul instrument joué par quatre machines** : la parité découpe « other » en
   quatre voix par registre (70-73, 64-68, 58-61, 27-54). Les trois premières sont
   le MÊME accord ; chacune reçoit sa machine. La quatrième est la basse, une vraie
   partie distincte.

**La décision sur les machines neuves se prend APRÈS ces trois-là**, sur la mesure :
une machine ne se crée pas pour compenser une transcription qui rejoue un accord
tenu (un pad parfait rejoué à 4 notes par seconde reste faux). Si, les notes tenues
et le diapason juste, aucun pad du parc n'approche le timbre du § 1 (seuils du § 0,
bandes et log-mel), la machine manquante sera décrite ici — ce qu'elle doit savoir
faire, mesuré sur l'original — et construite selon `CDC-nouvelle-machine.md`.

---

## 4. H42 — le morceau est accordé au-dessus de 440 Hz : une reconstruction à 440 bat contre lui sur chaque note tenue (écrite AVANT la mesure, 30/09/2026)

**Numéro.** H42 est le premier libre après H41 (`CDC-recensement-des-sources.md`
§ 14) ; il se cite avec ce document.

**L'hypothèse.** Donner au projet un **diapason** — la fréquence du la4 — que toutes
les machines mélodiques respectent, et le faire estimer par la chaîne sur le
mélange, amène l'écart de diapason de la reconstruction sous le seuil du § 0
(5 cents) sans rien changer d'autre.

**Ce qui sera écrit (le mécanisme, tranché ici).**
- **Le moteur** : `ISynthPlugin` porte une référence de la4, 440 Hz par défaut ;
  chaque machine MÉLODIQUE l'emploie là où elle convertit une note en fréquence
  (59 fichiers calculent aujourd'hui `440 · 2^((n−69)/12)`, sous une vingtaine de
  formes) ; le multi-échantillons multiplie son avance de lecture par
  `la4 / 440`. Les **batteries ne suivent pas** : leurs réglages d'accord sont ceux
  d'une pièce, pas d'une note (un kick « accordé » sur le morceau l'est par son
  réglage, que la chaîne cherche déjà). À 440, chaque machine rend **au bit près**
  ce qu'elle rendait.
- **Le projet** : `transport.referenceA4Hz`, écrit **seulement s'il diffère de 440**
  (un fichier d'avant reste identique octet pour octet), appliqué au graphe par les
  DEUX chemins de rendu — l'application et `interchange` (la leçon de D332 : ce qui
  conditionne le son d'une piste se pose dans `interchange`).
- **Le DAW** : une entrée « Diapason du projet… » (menu Transport), un pas
  d'historique, les deux langues — c'est le « Master Tune » de Cubase, qui manquait.
- **La chaîne** : `--diapason auto|<Hz>`, **défaut 440** (la chaîne d'aujourd'hui,
  au bit près, pour que la campagne S2 reste comparable) ; `auto` est l'estimateur
  validé du § 0 (moyenne circulaire des pics tenus), écrit au projet et à la
  provenance du rapport.

**LA MESURE — UNE SEULE VARIABLE.** Le projet de la course de référence, rendu deux
fois par le MÊME `vsm-render` (le neuf) : témoin tel quel (440), essai avec
`referenceA4Hz` = le diapason estimé par `--diapason auto` sur l'original. Mêmes
notes, mêmes machines, mêmes réglages ; seul le diapason change.

**ATTENDUS** :

| # | mesure | témoin (440) | essai (diapason estimé) | échec si |
|---|---|---|---|---|
| 1 | écart de diapason (outil du § 0) | ≈ −12 cents | \|écart\| ≤ 5 cents | > 5 cents |
| 2 | les empreintes audio du parc, les suites | vertes, inchangées | — | une empreinte change |
| 3 | un la4 à 446 Hz, machine par machine (test neuf) | — | toutes les mélodiques à ± 2 cents, les fautives NOMMÉES | une seule hors tolérance non dite |
| 4 | log-mel moyen (outil du § 0) | publié | ≤ témoin | > témoin + 0,05 dB |
| 5 | pistes de batterie (le contrôle) | — | leur rendu identique au bit | un échantillon change |

L'attendu 4 n'a pas de seuil de GAIN : le diapason est une condition nécessaire,
pas le gros de l'écart (le pad rejoué en notes brèves du § 3 pèse sans doute
davantage). Il ne doit simplement pas empirer. La course complète avec
`--diapason auto` (dont les ARBITRAGES peuvent changer, un timbre accordé se
comparant autrement) viendra après, et se lira avec `comparer_rapports.py`.

### 4.1 Mesuré — le moteur (30/09/2026, 11 h 30) : attendus 2, 3 et 5 TENUS

**Ce qui est écrit.** `audio/include/vsm/audio/plugin/Diapason.h` : une valeur du
moteur (atomique, 440 par défaut), posée par `ProcessGraph::setProject` depuis
`Project::referenceA4Hz` — donc par les deux chemins de rendu, `interchange`
appelant `setProject` AVANT de monter les machines. **64 conversions** note →
fréquence dans **58 fichiers** de machines passent par elle ; les quatre `440`
restants ne sont pas des notes (deux valeurs initiales, un formant, l'opérateur à
fréquence FIXE du DX7, qui par définition ne suit pas). Le diviseur d'octaves et les
roues phoniques, qui tiennent une table calculée à l'`initialize`, se réaccordent au
bloc suivant quand le diapason change (sans toucher aux phases : pas de clic). Le
multi-échantillons multiplie son avance de lecture par `la4 / 440`. Le document :
`transport.referenceA4Hz`, écrit seulement s'il diffère de 440 ; hors de 400-480 Hz
à la lecture, écarté et DIT (une réserve : il changerait le son).

| # | attendu | mesuré |
|---|---|---|
| 2 | empreintes et suites inchangées | **tenu** — audio 1 311/1 311 (empreintes comprises : au bit près à 440), core 364, interchange 314 (+1 : le diapason voyage, une valeur absurde est écartée et dite), clap 25 |
| 3 | chaque machine mélodique suit à ± 2 cents | **tenu** — 56 machines mélodiques du registre à +23,45 ± 2 cents (la4 446 contre 440), le multi-échantillons dans son propre test (profil engendré) |
| 5 | les machines qui ne suivent pas rendent au bit près | **tenu** — les 7 (5 batteries, sampler, guimbarde), à 38 contre 38 |

**Deux défauts de la MESURE, attrapés avant d'accuser une machine.** La première
version du test nommait quatre fautives : `vsm.vector` (+27,60 cents), `vsm.carillon`
(+27,70), `vsm.bagpipe` (+26,48), `vsm.psg` (+21,15). Les hauteurs ABSOLUES lues
disaient le reste : 192, 233 et 211 Hz pour une note de 220 — l'autocorrélation ne
lisait PAS la hauteur d'une table qui se déforme, d'une cloche ou d'une cornemuse à
bourdons, et le rapport de deux lectures fausses ne dit rien. Une seconde mesure,
indépendante — le décalage du SPECTRE ENTIER sur un axe logarithmique, qui ne
demande pas de période — arbitre désormais : elle lit **+23,40, +23,29 et +23,48**
cents pour les trois. Elle a d'abord été vérifiée sur une machine franche (Minimoog :
+23,45 ± 1, et 0 contre elle-même). La puce sonore, elle, **suit et quantifie** :
sa hauteur est `horloge / (16 · entier)`, fidèle au SN76489 ; lue 220,19 et
222,90 Hz pour 220,22 et 222,83 prédits par sa formule — le test la juge contre
cette prédiction. `test.dummy`, une doublure muette des tests du registre, est
écartée et dite.

**LA GARDE, VUE ROUGE.** `vsm.generic` remis à `440.0f` en dur, recompilé : le test
le nomme — « +0,00 cents par période, −0,00 par spectre, au lieu de +23,45 » —, 1
fautive sur 56 ; la source rétablie, 0.

*Les attendus 1 et 4 (le rendu du projet de référence aux deux diapasons) suivent,
avec un `vsm-render` construit à part (`build-h42/`) : la campagne S2, gelée, se
sert de `build/tools/vsm-render`, qu'une compilation ne doit pas remplacer.*

---

## 5. H43 — un son tenu que la transcription hache : réunir les notes qui se touchent SANS nouvelle attaque (écrite AVANT la mesure, 30/09/2026)

**Ce qui est vu (mesuré le 30/09 sur le stem « other » de la course de référence,
Basic Pitch réglages d'usine).** 2 740 notes, durée médiane **0,267 s** ; sur les
hauteurs du pad, les morceaux se suivent SANS INTERVALLE — la note suivante de même
hauteur commence là où la précédente finit (écart médian **0,000 s**) dans
**72 % à 76 %** des cas (MIDI 66 : 687 notes ; 70 : 241 ; 71 : 320 ; 73 : 411). Un
accord tenu des mesures entières devient ~4 attaques par seconde, et l'arbitrage
choisit en conséquence des machines FRAPPÉES (§ 3). Rien dans la chaîne ne réunit
deux notes contiguës (`analyse/analyzer/note_extraction.py` rend les événements de
Basic Pitch tels quels).

**CE QUE LA RÈGLE CASSERAIT, MESURÉ AVANT DE L'ÉCRIRE.** Dans la vérité du corpus
S2 (10 morceaux, parties mélodiques), **7 020 paires sur 49 267** (14,25 %) de notes
consécutives de même hauteur sont séparées de moins de 30 ms — surtout
l'accompagnement (4 368) et la mélodie (1 694). Réunir sur le seul écart
détruirait ces notes-là. La règle exige donc une seconde condition : **pas de
nouvelle attaque à la jonction** dans l'audio du stem.

**L'hypothèse.** Réunir deux notes de même hauteur quand (a) la seconde commence
moins de 30 ms après la fin de la première ET (b) l'enveloppe du stem, filtrée
autour de cette hauteur, ne remonte pas à la jonction (pas d'attaque) rend au pad
des notes tenues, sans casser les notes répétées d'une partie qui les rejoue.

**Le mécanisme (tranché ici).** Une option de la chaîne, `--reunir-tenues`, **éteinte
par défaut** (la chaîne d'aujourd'hui au bit près, et la campagne S2 inchangée) ;
la jonction se juge sur l'énergie du stem dans une bande d'un demi-ton autour de la
hauteur : attaque = hausse de plus de 3 dB entre les 30 ms qui précèdent et les
30 ms qui suivent la jonction. Chaque réunion est COMPTÉE au rapport (combien, sur
quelles hauteurs) — rien ne disparaît en silence.

**ATTENDUS** — témoin : la chaîne sans l'option ; essai : avec. Mêmes stems (ceux de
la course de référence, copiés dans `reconstruction/travail/reload-peschi-stems/`),
même budget, même métrique :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | notes du pad (stem « other », MIDI 58-73) | au moins **−50 %**, durée médiane au moins ×2 | moins de −25 % |
| 2 | **le contrôle — ce qu'elle casse** : paires VRAIES contiguës de S2 réunies à tort, sur la transcription des stems vrais de deux morceaux (`g1`, `g2`) | **≤ 5 %** | > 10 % |
| 3 | les voix du pad à l'arbitrage | une machine TENUE (pad, orgue, cordes, chœur) plutôt que frappée, sur au moins 2 des 3 voix | aucune |
| 4 | log-mel moyen (outil du § 0), mélange entier | ≤ témoin − 0,3 dB | > témoin |

Un balayage de seuil (3 dB), s'il est fait, se publie ENTIER.

### 5.1 Mesuré — attendu 1 (30/09/2026, 12 h) : PARTIEL au seuil écrit, et le balayage entier

La règle (`analyse/analyzer/tenues.py`, branche `reload-chaine` — la campagne S2
interdit de toucher `analyse/analyzer/` de l'arbre principal pendant qu'elle court)
appliquée aux 2 740 notes de Basic Pitch sur le stem « other » de la course de
référence ; le pad = MIDI 58-73 :

| seuil d'attaque | notes du pad | durée médiane | réunions | refusées (attaque) |
|---|---|---|---|---|
| 1,5 dB | 2 238 → 1 662 (**−26 %**) | 0,279 → 0,337 s | 680 | 898 |
| **3 dB (le seuil écrit)** | 2 238 → 1 261 (**−44 %**) | 0,279 → 0,360 s (×1,29) | 1 127 | 451 |
| 6 dB | 2 238 → 941 (**−58 %**) | 0,279 → 0,372 s (×1,33) | 1 466 | 112 |

**Au seuil écrit, l'attendu 1 est PARTIEL** : −44 % (réussite à −50 %, échec au-delà
de −25 %) et une durée ×1,29 (réussite à ×2). Le seuil ne bouge pas après coup. Ce
que le balayage dit : même à 6 dB, où presque plus rien n'est refusé, la durée
médiane ne passe pas ×1,33 — **la plupart des fragments ne se touchent pas à 30 ms
près** : Basic Pitch laisse entre eux des intervalles plus longs, et la règle ne les
voit pas. Et les battements lents du pad (§ 1) font monter l'énergie de plus de
3 dB à 451 jonctions sans qu'aucune note ne soit rejouée. Réunir après coup ne rend
donc au pad qu'une partie de sa tenue ; ce qui manque est en amont (la transcription
elle-même). Les attendus 2 à 4 restent à mesurer.

### 5.2 Verdict de H43 (30/09/2026, 12 h) : RÉFUTÉE par son contrôle — elle casse une note rejouée sur six

`analyse/verdict_h43.py` (branche `reload-chaine`) : les stems VRAIS des parties
mélodiques de `g1` et `g2`, transcrits par la fonction de la chaîne, réunis au seuil
écrit (3 dB), chaque jonction confrontée à la vérité.

| partie | paires vraies contiguës | réunies à tort | jonctions réunies | dont réparations |
|---|---|---|---|---|
| g2 accompagnement | 1 500 | **250** | 250 | 0 |
| g2 nappe | 0 | 0 | 697 | **292** |
| g2 nappe (2) | 0 | 0 | 58 | 52 |
| g2 voix | 0 | 0 | 45 | 22 |
| g2 nappe (3) | 57 | 0 | 0 | 0 |
| g1 accompagnement | 0 | 0 | **1 233** | 0 |
| les cinq autres parties | 0 | 0 | 3 | 0 |
| **total** | **1 557** | **250 (16,06 %)** | 2 286 | 366 |

**Attendu 2 : ÉCHEC** (16,06 % contre un seuil d'échec de 10 %). Et le chiffre écrit
SOUS-ESTIME le dommage, ce qu'il faut dire : l'attendu ne comptait que les paires
vraies contiguës à moins de 30 ms, or l'accompagnement de `g1` — des notes vraies de
66 ms, séparées — a subi **1 233 réunions** dont aucune n'est une réparation : la
transcription y rend contiguës des notes qui ne le sont pas, et l'énergie d'une
note qui résonne encore masque l'attaque de la suivante à la même hauteur. Seules
les nappes montrent le bénéfice attendu (344 réparations sur 755 jonctions).

**CE QUE LE VERDICT DÉCIDE.** H43 n'est pas adoptée : `--reunir-tenues` reste éteinte
(elle demeure, publiée et comptée, pour mesurer une règle meilleure). Les attendus
3 et 4 ne sont PAS courus : un correcteur se juge sur ce qu'il casse autant que sur
ce qu'il répare (la leçon de D270), et courir une reconstruction pour chiffrer son
gain sur « Reload » reviendrait à chercher la mesure qui le sauverait. Le constat de
§ 5.1 tient : la tenue du pad se perd EN AMONT, dans la transcription, et c'est là
que l'hypothèse suivante devra agir — avec ce même contrôle sur S2.
