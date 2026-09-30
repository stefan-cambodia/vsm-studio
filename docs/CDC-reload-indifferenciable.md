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
- **Tempo 138,00 BPM** — *corrigé le 30/09 à 17 h 30 : ce portrait disait « 136 »,
  et la chaîne estime 139,7 ; les deux sont faux, § 3 point 4 et § 8.* **Diapason
  +12,3 cents** au-dessus de 440 Hz sur le morceau
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

### 2.1 La course est morte à 16 h 14, avec l'extinction du poste — relancée à 17 h 01

**CE QUI S'EST PASSÉ.** Le poste a été **éteint** le 30/09 à 16 h 14 min 30 s
(`journalctl -b -1` : « poweroff requested from client … plasma-shutdown » — une
extinction demandée depuis le bureau, pas une panne), puis rallumé à 16 h 52. La
course en était à l'étape 4/5 : le verdict du mélange fait (deux tours, 6 142 s),
le réglage au mélange des voix 1 à 3 fait (1 724, 2 520 et 2 528 s), dernière ligne
du journal à 15 h 53. **Elle n'avait encore rien écrit** : ni `project.json`, ni
`rapport.json` — la chaîne n'écrit le projet qu'après le réglage au mélange.
**5 h 11 de calcul** (07 h 48-08 h 16, 10 h 21-13 h 35, 14 h 45-16 h 14, deux
veilles de batterie déduites) sans résultat. Une veille, la course la traverse ;
une extinction, non. La campagne S2, gelée à `g7` depuis 07 h 48, est morte du même
coup (`CDC-recensement-des-sources.md` § 14.3). L'enchaînement prévu après la course
(A/B de H42, course 2) attendait un PID qui n'existe plus : rien de lui n'a couru.

**LA RELANCE (17 h 01).** La même chaîne — l'arbre principal, dont `analyse/` n'a
pas bougé depuis le départ du matin (dernier commit `d03cdf5`) —, les mêmes options,
et **le même binaire** `build/tools/vsm-render` (compilé à 07 h 15, md5
`93e6587c…`), délibérément NON recompilé : son empreinte entre dans la clé du cache
de mesures (`cache/mesures/`, H2 du § 5 duodecies de `ROADMAP-fusion.md`), où la
course morte a laissé **996 mesures** de candidates. Un binaire neuf les aurait
toutes invalidées. Les stems sont ceux de la première course, copiés à 10 h 54 dans
`reconstruction/travail/reload-peschi-stems/` (`--stems`, la séparation sautée).

Ce que la relance change, dit :
- la séparation n'est pas rejouée (mêmes fichiers, donc mêmes empreintes de cible) ;
- la chaîne avertit que le moteur (07 h 15) est plus vieux que
  `interchange/src/PatchRenderService.cpp` (12 h 37, H42) — c'est voulu : le moteur
  du matin est celui de la référence, et l'identité au bit du moteur neuf à 440 Hz
  est contrôlée par `cmp` à l'étape suivante (§ 4, A/B) ;
- le poste porte une autre session de travail (un émulateur Android, 2,8 Go) : les
  durées de cette course ne se comparent pas à celles du matin.

Ce qui est identique, vérifié sur le journal : tempo estimé (139,7), partage du
morceau (drums 69,0 %, other 29,2 %…), et les premières décisions — l'arbitrage de
batterie rend **0,339 / 0,398 / 0,424** (`tr808`, `tr909`, `drums`) comme le matin,
en 17 s au lieu de 46.

**LA SUITE NE DÉPEND PLUS D'UN PID QUI PEUT MOURIR.**
`reconstruction/travail/reload-suite.sh` enchaîne référence → A/B de H42 → course 2
→ mesure → reprise de S2, et **saute chaque étape dont le résultat est là** en le
disant à son journal (`reload-suite.journal`). Après une extinction, on la relance
telle quelle : elle reprend où l'on en était. Une course à la fois.

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
4. **Le tempo est faux de 1,7 BPM, et la grille du projet quitte la musique**
   (relevé le 30/09 à 17 h 30, en relisant le journal de la course). La chaîne
   estime **139,7 BPM** ; ce portrait disait 136. Mesuré par la cohérence de phase
   des attaques sur une grille de doubles croches, trois fois et indépendamment :
   **138,006** (grave du stem de batterie), **137,998** (grave du mélange),
   **138,002** (aigus du mélange, cohérence 0,80 ; à 136,0 : 0,009 ; à 139,7 :
   0,012), la phase constante à ± 10 ms d'un bout à l'autre du morceau — un tempo
   fixe. Les notes restent à leur place EN SECONDES (les ticks sont calculés au
   tempo écrit), donc le SON n'en souffre pas ; mais à 139,7 la grille du DAW
   prend une double croche d'avance toutes les 9 secondes, et un temps entier en
   35 : mesures, aimant et quantification n'y veulent plus rien dire — le défaut
   que le § 5 quindecies de `ROADMAP-fusion.md` croyait fermé, son attendu
   (± 2 BPM) étant trop large pour une grille. C'est H46 (§ 8). *Une première
   mesure d'aujourd'hui, par la cohérence sur une grille de NOIRES, rendait 0,06
   partout et « aucun tempo fixe » : le kick et la basse à contretemps s'y
   annulent. Une grille se cherche à la subdivision que le morceau joue.*

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

### 4.2 Un défaut trouvé AVANT la course 2 : le cache de mesures ne portait pas le diapason (30/09/2026, 17 h 10)

La clé d'une mesure de candidate (`vsm_render_cache.cle_de_rendu`) scelle machine,
patch, notes, tempo et empreinte du moteur — « tout ce qui peut changer le rendu,
rien d'autre ». Le diapason de la course, que H42 fait porter à chaque requête de
rendu, **n'y était pas**. Une course à 443,14 Hz lancée après une course à 440 avec
le même `vsm-render` aurait relu les mesures de la première, candidate par
candidate, et la comparaison des deux aurait conclu « le diapason ne change aucun
arbitrage » — la panne muette que la règle du dépôt interdit, et exactement la
course complète que le § 4 annonce. Elle n'a pas eu lieu : ce matin les deux courses
devaient tourner sur deux binaires (donc deux empreintes), et c'est en relisant la
chaîne avant de relancer, pas par une mesure fausse, que le trou s'est vu.

Corrigé sur `reload-chaine` (`efc0a27`) : le diapason entre dans la clé **seulement
s'il diffère de 440** — à 440 la clé est celle d'avant (le test la recalcule par
l'ancienne formule), et les mesures déjà payées restent valables. La garde a été vue
rouge : la condition retirée, le test tombe sur « deux diapasons partagent une
clé ». `analyzer.diapason` et `analyzer.tenues` rejoignent
`charger_tous_les_modules`.

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

---

## 6. H44 — un seuil d'attaque plus exigeant rend sa tenue au pad sans rien casser ailleurs (écrite AVANT la mesure, 30/09/2026)

**D'où elle vient.** § 5.2 : réunir après coup ne distingue pas un fragment d'une
note rejouée ; la tenue se perd EN AMONT. Basic Pitch crée une note neuve à chaque
pic de son canal d'attaque au-dessus de `onset_threshold` (0,5 par défaut, jamais
réglé par la chaîne) ; les battements lents du pad produisent de tels pics.

**L'hypothèse.** Relever `onset_threshold` fait tomber les attaques parasites d'un son
tenu plus vite que les vraies attaques d'une partie rejouée, parce qu'une vraie
attaque est franche et un battement ne l'est pas.

**La mesure — un balayage publié ENTIER**, le seuil à 0,5 (témoin), 0,6, 0,7, 0,8 et
0,9, la sortie du modèle calculée UNE fois par stem et les notes redérivées à chaque
seuil (`basic_pitch.note_creation`), les autres réglages d'usine inchangés :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | « Reload », stem « other », pad (MIDI 58-73) : notes et durée médiane | à un seuil au moins : −50 % de notes ET durée ×2 | à aucun seuil −25 % |
| 2 | **le contrôle** — S2 (`g1`, `g2`, stems vrais, parties mélodiques) : F1 note à note (même hauteur, attaque à ± 50 ms), par rôle, au MÊME seuil que 1 | aucun rôle ne perd plus d'1 point de F1 sur le témoin | un rôle perd plus de 3 points |
| 3 | un seuil qui tient 1 ET 2 existe | oui : il devient la proposition pour la chaîne (une option, mesurée ensuite de bout en bout) | non : l'hypothèse est réfutée, et la tenue demandera un traceur de hauteur tenue plutôt qu'un réglage |

### 6.1 Verdict de H44 (30/09/2026, 12 h 20) : RÉFUTÉE comme réglage GLOBAL — et le balayage montre où elle vaut

`analyse/verdict_h44.py` (branche `reload-chaine`), sortie du modèle calculée une
fois par stem :

| seuil d'attaque | pad de « Reload » : notes | durée médiane |
|---|---|---|
| 0,5 (témoin) | 2 238 | 0,279 s |
| 0,6 | 1 366 (−39 %) | 0,372 s (×1,33) |
| 0,7 | 865 (**−61 %**) | 0,418 s (**×1,50**) |
| 0,8 | 757 (−66 %) | 0,383 s (×1,38) |
| 0,9 | 754 (−66 %) | 0,383 s (×1,38) |

| F1 note à note, S2 (`g1`, `g2`, stems vrais) | 0,5 | 0,6 | 0,7 | 0,8 | 0,9 |
|---|---|---|---|---|---|
| accompagnement | 69,7 | 68,6 | 66,2 | 66,3 | 66,7 |
| basse | 44,7 | **12,0** | 12,0 | 12,0 | 12,0 |
| mélodie | 99,9 | 99,9 | 99,9 | 99,8 | 99,7 |
| nappe | 51,3 | 63,7 | **69,8** | 71,6 | 69,5 |
| voix | 49,0 | 92,0 | **98,5** | 83,1 | 73,5 |

**Attendu 1 : partiel** (−61 % de notes à 0,7, mais la durée ne double à aucun
seuil). **Attendu 2 : échec à tout seuil au-dessus de 0,5** — la basse perd 32,7
points dès 0,6, l'accompagnement 3,5 à 0,7. **Attendu 3 : aucun seuil global ne tient
les deux** — H44 est réfutée telle qu'écrite.

**Ce que le balayage apprend, et qui n'était pas demandé** : sur les parties TENUES,
le gain est énorme — la voix passe de 49,0 à **98,5** de F1, la nappe de 51,3 à
**69,8** — pendant que la basse s'effondre. Le seuil d'attaque est donc un réglage
PAR STEM, pas un réglage de chaîne ; d'où l'hypothèse suivante (§ 7).

**Un défaut de la mesure, attrapé avant d'être publié** : la première course rendait
un F1 de **0,0 pour tous les rôles à tous les seuils** — la vérité était lue
(attaque, hauteur) et dépaquetée (hauteur, attaque). Un zéro partout ne mesurait que
le dépaquetage ; corrigé, rejoué, les chiffres ci-dessus sont ceux de la seconde
course.

---

## 7. H45 — le seuil d'attaque choisi PAR STEM, sur un indice que la transcription donne d'elle-même (écrite AVANT la mesure, 30/09/2026)

**L'hypothèse.** Un stem TENU se reconnaît sans vérité : transcrit au seuil d'usine
(0,5), ses notes se suivent à la même hauteur sans intervalle (le pad de « Reload » :
72 à 76 % sur ses hauteurs principales). Soit l'**indice de hachure** d'un stem : la
part de ses notes suivies d'une note de même hauteur à moins de 30 ms. Transcrire à
0,7 les stems dont l'indice dépasse un seuil `X`, et à 0,5 les autres, garde le gain
de H44 sur les parties tenues sans rien coûter à la basse.

**CALIBRATION ET VALIDATION SÉPARÉES — pour ne pas régler la règle sur ce qui la
juge.** `g1` et `g2` ont déjà été regardés (§ 5, § 6) : ils servent à CALIBRER `X` —
l'indice est relevé par rôle, et `X` est posé entre les nappes/voix et les
basses/accompagnements, la valeur et sa raison écrites avant la validation. La
validation se fait sur des morceaux que rien n'a encore regardés : **`g3`, `g4`,
`g5`**.

**ATTENDUS (sur `g3`-`g5`, par rôle, F1 note à note comme au § 6)** :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | aucun rôle ne perd sur le témoin (tout à 0,5) | perte ≤ 1 point partout | un rôle perd plus de 3 points |
| 2 | nappe et voix | gain ≥ 10 points chacune | gain < 3 points |
| 3 | le pad de « Reload » (stem « other ») est classé tenu | oui | non |
| 4 | les stems de basse de `g3`-`g5` sont classés non tenus | tous | un seul classé tenu |

### 7.1 Calibration sur `g1`-`g2` (30/09/2026, 12 h 25) — `X` = 0,4, posé AVANT la validation

Indice de hachure (seuil d'usine 0,5), par stem :

| rôle | indices relevés |
|---|---|
| basse | 0,003 |
| mélodie | 0,136 · 0,262 |
| accompagnement | 0,000 · 0,254 · **0,644** (`g1` : ses notes de 66 ms, que la transcription rend contiguës) |
| nappe | 0,000 · 0,000 · 0,247 · 0,464 |
| voix | 0,473 |
| « Reload », stem « other » | **0,576** |

**L'indice sépare la BASSE (0,003) de tout le reste** — c'est la condition dont H44
manquait. **Il ne sépare PAS un accompagnement haché des parties tenues** :
l'accompagnement de `g1` (0,644) dépasse toutes les nappes et la voix. Aucun `X` ne
range les rôles sans erreur.

**`X` = 0,4, et pourquoi** : c'est la valeur qui classe tenues la voix (0,473), la
nappe la plus hachée (0,464) et le pad de « Reload » (0,576), et jamais la basse ;
elle classe aussi, à tort, l'accompagnement de `g1` — un coût connu d'avance, que la
validation chiffrera sur des morceaux jamais regardés. `X` ne bougera plus.

### 7.2 Verdict de H45 (30/09/2026, 12 h 35) : PARTIELLE au sens écrit — et aucun rôle n'y perd

Validation sur `g3`, `g4`, `g5` (jamais regardés), `X` = 0,4 :

| rôle (F1 note à note) | témoin (tout à 0,5) | règle par stem | écart |
|---|---|---|---|
| accompagnement | 40,4 | 41,6 | +1,2 |
| basse | 26,8 | 31,3 | **+4,5** |
| mélodie | 46,8 | 46,5 | −0,3 |
| nappe | 35,8 | 48,3 | **+12,4** |
| voix | 97,0 | 97,0 | 0,0 |
| piano deux mains | 0,0 | 0,0 | *non mesurable* |

| # | attendu | verdict |
|---|---|---|
| 1 | aucun rôle ne perd plus d'1 point | **tenu** (la pire perte : −0,3, la mélodie) |
| 2 | nappe et voix gagnent 10 points chacune | **partiel** : la nappe +12,4 ; la voix 0,0 — celle de `g4` n'était pas hachée (indice 0,224) et lisait déjà 97,0 au témoin : rien à gagner, mais l'attendu écrit dit échec pour elle |
| 3 | le pad de « Reload » est classé tenu | **tenu** (indice 0,576) |
| 4 | aucune basse de `g3`-`g5` classée tenue | **échec** : la basse de `g3` (indice 0,599) l'a été — et c'est la basse qui GAGNE 4,5 points : une basse hachée profite du seuil haut, la basse franche de `g2` (0,003) s'y effondrait |

**Le piano deux mains n'est pas mesuré, et c'est dit** : sa partie vraie joue deux
oscillateurs désaccordés de 1,46 demi-ton, sans hauteur sonnante mesurée ; aucune note
transcrite n'y tombe à la même hauteur que la vérité, des deux côtés. Un 0,0 qui ne
mesure que l'aveuglement de l'appariement, pas la règle.

**CE QUE LE VERDICT DÉCIDE.** Au sens écrit, H45 est partielle (deux attendus tenus,
un partiel, un en échec). Sur le fond, la règle ne coûte rien à aucun rôle et donne
+12,4 points aux nappes : elle devient une **option de la chaîne**
(`--seuil-attaque-par-stem`, éteinte par défaut, l'indice et le seuil choisis inscrits
au rapport pour chaque stem), et sa mesure de bout en bout sur « Reload » — avec le
diapason de H42 — dira si le son s'en rapproche (outil du § 0). L'attendu 4 était mal
posé : il supposait qu'une basse ne doit jamais changer de seuil, et la mesure a
montré que c'est la basse FRANCHE qui ne le doit pas.

---

## 8. H46 — le tempo à la précision d'une GRILLE : l'estimation affinée par la cohérence de phase des attaques (écrite AVANT la mesure, 30/09/2026, 17 h 45)

**Ce qui est vu (§ 3, point 4).** « Reload » est à **138,00 BPM** ; la chaîne écrit
139,7 au projet. Et ce n'est pas un accident de ce morceau : l'attendu du § 5
quindecies de `ROADMAP-fusion.md` était « ± 2 BPM », tenu 10 fois sur 10 sur
`s1-sec` avec un écart médian de **1,0 BPM**. Un BPM d'écart, c'est une grille qui
prend un temps entier d'avance en une minute. L'attendu était écrit pour ne plus
ouvrir un morceau à 120 ; il ne l'était pas pour une grille.

**L'unité juste est la DÉRIVE, pas le BPM** : de combien la grille du projet
s'écarte de la musique à la fin du morceau — `|Δbpm| / bpm × durée`. Une grille qui
sert (aimant, quantification, boucle par mesures) ne doit pas dériver de plus de
quelques dizaines de millisecondes sur le morceau entier.

**L'hypothèse.** Partant du tempo de `librosa.beat.beat_track` (juste à ± 2 BPM),
chercher dans une fenêtre de ± 4 % le tempo qui rend les attaques du mélange le plus
COHÉRENTES en phase sur une grille de subdivisions amène la dérive sous 20 ms en fin
de morceau — sans rien casser là où `beat_track` était déjà juste.

**Le mécanisme (tranché ici).**
- Les attaques : `librosa.onset.onset_detect` sur l'enveloppe d'attaques du mélange,
  l'instant de chacune affiné par interpolation parabolique de l'enveloppe, son poids
  = la force de l'attaque.
- La cohérence d'un tempo `b` sur une subdivision `d` par temps :
  `C = |Σ w·exp(2πi·d·t·b/60)| / Σ w` — 1 pour des attaques toutes sur la grille,
  ~`1/√N` pour `N` attaques au hasard.
- **Deux subdivisions, 4 (doubles croches) et 6 (sextolets, qui portent le ternaire)**,
  la plus cohérente gagne. Pas la noire ni la croche seules : un kick sur le temps et
  une basse à contretemps s'y annulent (payé aujourd'hui, § 3).
- Fenêtre ± 4 % autour du départ, pas de 0,002 BPM. Au-delà de ± 4 % on entre dans
  les rapports métriques (16/15 est à 6,7 %) : l'erreur d'OCTAVE n'est pas l'affaire
  de cette hypothèse, et reste comptée à part par `tools/tempo-estime.py`.
- **Concluant** si la cohérence du maximum atteint `max(0,2 ; 3/√N)` (trois fois le
  niveau du hasard). Sinon le tempo de départ est GARDÉ, et c'est DIT au journal et
  au rapport — jamais un affinage sur du bruit. Deux autres cas ne concluent pas,
  ajoutés en écrivant le code et AVANT toute mesure : moins de huit attaques, et un
  maximum **au bord** de la fenêtre (ce n'est pas un sommet : le vrai est peut-être
  dehors).
- **Le tempo ENTIER le plus proche est retenu s'il explique les attaques à 95 % de
  la cohérence du maximum** — ce qui revient à une dérive de moins d'un cinquième de
  subdivision sur le morceau entier, quelle que soit sa durée. Entre deux tempos que
  les attaques ne départagent pas, le plus simple est celui que le morceau a été
  écrit avec. Sinon le tempo est rendu au millième.
- Une option de la chaîne, `--tempo-affine`, **éteinte par défaut** (la chaîne
  d'aujourd'hui au bit près, et la campagne S2 inchangée) ; `provenance.tempo` porte
  le départ, le tempo affiné, sa cohérence, la subdivision, et si l'entier a été
  retenu.

**CALIBRATION ET VALIDATION.** Rien ici n'a été réglé sur un corpus : les seuils
ci-dessus sont posés sur leur raison (le hasard, la dérive), avant toute mesure.
`s1-sec` (dix extraits de 30 s) est mesuré le premier ; `s2` (dix morceaux de 186 à
269 s, avec entrées et sorties de parties) et `s1-prod` (les mêmes graines, avec
production) le sont ensuite, sans rien changer entre. Les trois lots partagent
leurs dix tempos (108 à 137) : ce que `s2` ajoute est la LONGUEUR — là où la dérive
se voit —, pas de nouveaux tempos, et c'est dit.

**ATTENDUS** — témoin : `beat_track` seul (la chaîne d'aujourd'hui) ; essai :
affiné. Dérive = `|Δbpm| / bpm vrai × durée du morceau` :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | `s1-sec` : dérive en fin de morceau | ≤ 20 ms sur 9 morceaux sur 10 au moins | moins de 7 sur 10 |
| 2 | **le contrôle — ce qu'il casse** : morceaux dont la dérive affinée DÉPASSE celle du témoin, sur les trois lots | 0 | 2 ou plus |
| 3 | `s2` : dérive en fin de morceau | ≤ 20 ms sur 9 sur 10 au moins | moins de 7 sur 10 |
| 4 | `s1-prod` : dérive en fin de morceau | ≤ 20 ms sur 8 sur 10 au moins | moins de 6 sur 10 |
| 5 | les affinages « non concluants » | comptés et nommés ; ils comptent comme ratés aux attendus 1, 3 et 4 si leur dérive dépasse 20 ms | — |
| 6 | sans l'option | `provenance.tempo` et le tempo du projet identiques (test) | un champ change |

« Reload » (138,00, déjà vu) et « B4 Wuz Then » (sans vérité) sont PUBLIÉS, et ne
valident rien.

**Ce que H46 ne fait pas.** La grille n'est toujours pas PHASÉE sur le premier temps
fort (le « reste nommé » du § 5 quindecies) : à tempo juste, les notes tombent à un
écart CONSTANT de la grille au lieu d'un écart qui grandit. C'est la phase suivante,
et elle ne se mesure qu'une fois le tempo juste.
