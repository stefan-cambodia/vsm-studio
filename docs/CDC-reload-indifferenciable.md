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
- **Tempo 138,00 BPM** — *corrigé le 30/09 vers 17 h 10 : ce portrait disait « 136 »,
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

*Résultats : au § 2.4 (la course finie le 02/10 à 08 h 29 ; distance globale 0,2415 ; aucune des six conditions du § 0 tenue).*

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

### 2.2 Morte une deuxième fois à 18 h 28 — la même extinction — et relancée à 19 h 30 par la suite, telle quelle

**CE QUI S'EST PASSÉ.** La relance de 17 h 01 a retrouvé l'étape 4/5 en **54 minutes**
(« Écriture du projet » à 17 h 55, contre plus de cinq heures le matin : le cache de
mesures a servi, comme prévu), puis est entrée dans le réglage au mélange, qui n'écrit
rien au journal tant qu'une voix n'est pas finie. Le poste a été **éteint une
deuxième fois** à 18 h 28 min 00 s (`journalctl -b -2` : « poweroff requested from
client … plasma-shutdown », la même demande depuis le bureau ; capot fermé à 17 h 41,
sans veille — la garde tenait son blocage), rallumé à 18 h 58, puis **redémarré** à
19 h 26. Ni `project.json` ni `rapport.json` : 1 h 27 de course (17 h 01-18 h 28)
sans résultat, dont 33 minutes de réglage au mélange que le journal ne montre pas.
La suite (PID 23193) est morte à son étape 1.

**LA RELANCE (19 h 30 min 09 s), sans rien réécrire.** `uptime` d'abord (« up 0
min » : rien ne tournait, et les PID des fichiers `.pid` ne désignaient plus rien),
puis la commande écrite en tête de `reload-suite.sh`, inchangée : son étape 1 a lu
que la référence n'avait pas fini et l'a relancée — mêmes stems, même binaire
(`build/tools/vsm-render`, md5 `93e6587c…`, toujours celui de 07 h 15). Les deux
morts sont écrites dans `reload-peschi.journal` et `reload-suite.journal`. La garde
de batterie est reposée sur le PID de la suite (seuil 10 %, 92 % au départ).

**CE QUE CES DEUX MORTS DISENT DE LA CHAÎNE, ET QUI RESTE À FAIRE.** Le cache de
mesures rend le début rejouable à bas prix ; **le réglage au mélange, lui, ne laisse
rien** — 6 772 s le matin pour trois voix, perdues deux fois. Une course de
plusieurs heures sur un poste portable doit pouvoir reprendre à l'étape où elle est
morte : c'est un défaut de la chaîne, nommé ici, à traiter dans `ROADMAP-fusion.md`
quand aucune course ne tournera (`analyse/analyzer/` ne se touche pas pendant une
course).

### 2.3 Morte une troisième fois le 30/09 à 23 h 10 — et relancée le 01/10 à 22 h 49, le cache des mesures de projet dans la chaîne

**CE QUI S'EST PASSÉ.** La relance de 19 h 30 avait passé le verdict du mélange (deux
tours, **7 112 s**) et réglé la première voix au mélange (« other · voix 1 » 0,2546 →
0,2509, 27 évaluations, **2 298 s**) ; la deuxième était en cours, dernière ligne du
journal à 23 h 01. Le poste a été **éteint** à 23 h 10 min 34 s (`journalctl -b -2` :
`org.kde.Shutdown`, une extinction demandée depuis le bureau, la troisième de la
journée), rallumé à 23 h 29, éteint à 23 h 42, rallumé le 01/10 à 00 h 05. Ni
`project.json` ni `rapport.json` : **3 h 40 de course** sans résultat, dont au moins 2 h 37 de
mélange que rien ne rangeait. Personne ne l'a relancée avant la reprise du 01/10 à
20 h 55 (`uptime` : 20 h 49 ; rien ne tournait).

**CE QUI A CHANGÉ AVANT LA RELANCE, ET POURQUOI MAINTENANT.** Le verdict de H48 (§ 10.1)
était calculé depuis le 30/09 à 21 h 32 ; sa règle, écrite avant, faisait entrer le
cache des mesures de projet « dans l'arbre principal dès qu'aucune course n'y
tourne ». C'était le cas : `reload-h48` y a été fusionnée (`1259f93`), la suite Python
entière passée deux fois sur l'arbre fusionné (257 sur 257), ruff et mypy verts
(`5a37823`). La fusion apporte aussi `reload-chaine` — H42, H43, H45, **éteints par
défaut** — et, à 440 Hz, la clé d'un rendu de piste est celle d'avant : les mesures de
candidates payées par les trois courses mortes restent valables.

**LA RELANCE (01/10, 22 h 49 min 14 s)**, la commande écrite en tête de
`reload-suite.sh`, inchangée : mêmes stems, **même binaire** `build/tools/vsm-render`
(md5 `93e6587c…`, celui du 30/09 à 07 h 15 — D526, corrigé dans les sources, n'y est
délibérément PAS : le recompiler jetterait tout le cache). Suite PID 743158 ; la garde
de batterie bloque la veille tant qu'elle vit, et la déclenche à 10 % (95 % au départ).

**Ce que la relance montre déjà** (lu au journal avant d'écrire ces lignes, donc pas
« dit avant ») : les premières décisions sont celles des trois courses — tempo 139,7,
arbitrage de batterie 0,339 / 0,398 / 0,424 (en 23 s). **Ce qu'elle doit montrer, dit
avant** : à la fin, la ligne « mesures de projet » de son journal — toutes payées,
aucune relue, puisque rien n'était rangé —, qui donne le coût de la clé sur un
morceau de 312 s (la limite écrite au § 10). **Une quatrième extinction ne coûterait plus le
mélange** : c'est l'attendu 3 de H48, tenu à 30 % près.


### 2.4 Finie le 02/10 à 08 h 29 — les résultats (écrits à 08 h 39)

**LA COURSE.** Relancée le 01/10 à 22 h 49 min 14 s, `FIN reconstruction rc=0` le 02/10 à
08 h 29 min 18 s : **9 h 40 à l'horloge, 25 416 s (7 h 04) au compteur de la chaîne**. L'écart,
9 388 s, est EXACTEMENT celui des deux mises en veille de la nuit relevées par `journalctl`
(01 h 40 min 01 s → 02 h 21 min 28 s, puis 04 h 58 min 59 s → 06 h 54 min 00 s), que le
compteur ne voit pas ; les gels de compilation (SIGSTOP), eux, y sont comptés. Douze pistes,
5 918 notes, les groupes « other » et « Batterie » ; projet, rapport et `comparaison.wav`
écrits dans `reconstruction/travail/reload-peschi/`.

**DISTANCE GLOBALE : 0,2415** (métrique v2, budget 20 itérations, stems repris de
`reload-peschi-stems`). Le verdict du mélange : deux tours ; la voix 3 de « other » et le
piano changent de machine au second verdict (`vsm.tonewheel` → `vsm.stochastic`,
`vsm.multisample` → `vsm.minimoog`) ; les deux pistes de voix restent « inchangées (aucune
machine suivante) » — le chiffre « sans la piste » y vaut celui « avec », au dernier chiffre,
la signature de H52 (§ 14) : cette course est d'AVANT sa correction.

**LES CONDITIONS DU § 0** — l'outil `tools/ecart-a-l-original.py`, passé par l'étape 2 de
`reload-suite.sh` sur le projet rendu à 44,1 kHz par `build-h42` (le « témoin » de H42, égal
AU BIT au rendu de l'ancien moteur `build/` — contrôle de la même étape) :

| condition | seuil | référence | tenue |
|---|---|---|---|
| diapason | \|écart\| ≤ 5 cents | −12,7 cents (original +12,3, reconstruction −0,4) | non |
| niveau | \|décalage\| ≤ 0,5 dB, pire tranche ≤ 1 dB | −0,57 dB ; pire tranche 5,43 dB | non |
| équilibre | \|écart médian\| ≤ 1 dB par bande | sub −1,33, basse −3,15, bas-médium +2,72, médium +1,66, haut-médium −3,01, aigus **−6,94** dB ; 33 à 42 tranches sur 43 au-delà de 1 dB | non (6 bandes sur 6) |
| calage du kick | \|médian\| ≤ 2 ms, p90 ≤ 6 ms | \|médian\| 4,3 ms, p90 8,7 ms ; 408 attaques appariées sur 849 (618 côté reconstruction) | non |
| largeur stéréo | ± 30 % de 0,0478 | **0,0001** — la reconstruction est MONO | non |
| log-mel | écart moyen ≤ 1,3 dB | **10,09 dB** (médian 8,53) | non |

**Aucune des six conditions nécessaires n'est tenue** : c'est le point de départ chiffré
que le § 2 attendait, pas un verdict sur une hypothèse. Trois écarts dominent par leur
taille, et chacun a sa piste déjà écrite ou à écrire : les **aigus à −6,9 dB** sur 40
tranches sur 43 ; la **largeur stéréo nulle** (0,0001 contre 0,0478 — chaque piste est
rendue au centre, ce que le § 3 n'avait pas relevé) ; le **log-mel à 10,1 dB**, huit fois
le seuil. Le diapason, lui, a son hypothèse mesurée : H42 (§ 4.3).

**CE QUI AVAIT ÉTÉ DIT AVANT, ET QUI EST FAUX.** Le § 2.3 attendait, sur la ligne
« mesures de projet », « toutes payées, aucune relue, puisque rien n'était rangé ». Le
journal dit **368 payées, 57 relues** : 368 fichiers neufs exactement sont apparus dans
`cache/mesures` pendant la course, et les 57 relectures sont des mesures RÉPÉTÉES au sein
de la course elle-même — le même projet mesuré deux fois au mélange. La course A du § 10.1
l'avait déjà montré (« cache vide qu'elle remplit » : 242 payées, **34 relues**) ; la
prédiction ne l'avait pas lu. Le coût de la clé sur un morceau de 312 s, que cette ligne
devait donner, n'est pas isolable ici (rien ne chronomètre le hachage) : il reste « non
mesuré », comme au § 10.

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
   (relevé le 30/09 vers 17 h 10, en relisant le journal de la course). La chaîne
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

### 4.2 Un défaut trouvé AVANT la course 2 : le cache de mesures ne portait pas le diapason (30/09/2026, 17 h 05)

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

### 4.3 Verdict de H42 (mesuré le 02/10 de 08 h 29 à 08 h 32 par `reload-suite.sh`, écrit à 08 h 39) : attendu 1 TENU, attendu 4 en ÉCHEC de 0,01 dB au-delà de sa marge

La mesure écrite au § 4, sans rien changer : le projet de la course de référence (§ 2.4)
rendu deux fois par `build-h42`, témoin à 440, essai à `referenceA4Hz` = 443,1372 Hz
(+12,3 cents, l'estimateur du § 0 sur l'original) ; **une seule variable**. Contrôle : le
témoin du moteur neuf est égal AU BIT à celui de l'ancien moteur (`build/`).

| # | mesure | témoin (440) | essai (443,14 Hz) | verdict |
|---|---|---|---|---|
| 1 | écart de diapason | −12,7 cents | **−0,4 cents** | **tenu** (\|−0,4\| ≤ 5) |
| 4 | log-mel moyen | 10,09 dB | 10,15 dB | **échec** : +0,06 dB, la marge écrite était +0,05 |

Les autres lignes de l'outil, publiées pour qu'on ne les cherche pas : niveau −0,57 → −0,81 dB
(pire tranche 5,43 → 6,51 dB) ; bandes, l'essai rapproche le sub, la basse, le bas-médium, le
haut-médium et les aigus, et éloigne le médium (+1,66 → +2,20 dB) ; kick inchangé (\|médian\|
4,3 ms, p90 8,7 ms) ; largeur 0,0001 dans les deux.

**CE QUE LE VERDICT DIT, ET NE DIT PAS.** L'hypothèse tient sur ce qu'elle promettait — le
diapason passe sous le seuil, de −12,7 à −0,4 cents — et l'attendu 4, qui ne demandait que de
ne pas empirer, est en échec à **0,01 dB au-delà de sa marge, à la précision où l'outil publie
(0,01 dB)**. Ce n'est pas « dans le bruit » : la marge était écrite avant, elle est dépassée.
Un projet réglé à 440 (machines, patchs, niveaux choisis À 440) puis transposé de 12 cents
n'est plus le projet que la chaîne aurait réglé à ce diapason ; c'est exactement ce que la
course 2 (`--diapason auto`, en cours depuis 08 h 32) mesure, et c'est elle qui dira si le
diapason doit entrer par défaut. **H42 reste éteinte par défaut** jusque-là.

### 4.4 La course 2 morte le 02/10 à 19 h 05, à l'étape 4/5 — reprise le 10/10 à 17 h 30, telle quelle

**CE QUE LE DISQUE DIT, relu le 10/10 à 17 h 30.** `reload-course2.log` s'arrête au milieu du verdict
du mélange (« other · voix 2 : ATTENTION — le morceau est MEILLEUR sans cette piste… »), sans ligne
`FIN` ; le dossier de sortie n'a que ses `samples/` (aucun `project.json`, aucun `rapport.json`). La
course avait déjà été relancée une fois le 02/10 (deux « [1/5] » au journal). Du 02/10 au 10/10, le
travail est passé au DAW (D525 à D549) ; personne ne l'a relancée — et ce document ne le disait pas.

**LA REPRISE, rejouable et écrite avant de lancer** (la règle des extinctions, `CLAUDE.md`) : la MÊME
chaîne — l'arbre `vsm-studio-reload`, branche `reload-chaine` à `f0136266` (H58), propre —, le MÊME
`build-h42/tools/vsm-render` (30/09, 12 h 45, inchangé : son empreinte est dans la clé de
`cache/mesures/`, 109 864 mesures rangées), les MÊMES stems, les mêmes options (`--diapason auto
--seuil-attaque-par-stem --rendus-paralleles 6`). Ce qu'elle avait payé se relit (§ 10.5 : un mélange
entier rejoué en 8 s) ; seul ce qu'elle n'avait pas atteint se paie. **La branche ne porte ni H52 (la
voix dans le verdict) ni H46** : c'est voulu — la course 2 est la seconde moitié d'un A/B contre la
course de RÉFÉRENCE, qui ne les portait pas non plus ; leur ajouter H52 ferait deux variables.
`reconstruction/travail/reload-course2-reprise.sh`, lancé par `setsid nohup`, la veille bloquée par
`tools/garder-batterie.sh --pid` (une campagne : la veille ne se bloque que pour elle, et le poste est
endormi à 10 % quoi qu'il arrive). Batterie à 54 % au départ, en décharge.

**CE QU'ELLE DOIT DIRE, écrit au § 4 et au § 7 avant la course, et rappelé ici sans y toucher** : si le
diapason estimé (H42) doit entrer par défaut — l'écart de diapason sous 5 cents SANS dégrader le
log-mel au-delà de sa marge, sur un projet RÉGLÉ à ce diapason et non transposé après coup —, et ce que
le seuil d'attaque par stem (H45) fait au morceau entier.

### 4.5 La course 2 finie (10/10 à 22 h 23, rc=0) et mesurée (22 h 24) : le diapason estimé N'ENTRE PAS par défaut — −6,1 cents, et le log-mel recule de 1,58 dB

Relancée à 17 h 30, finie à **22 h 23** — 4 h 53 à l'horloge, **11 099 s au compteur** : la différence est celle des
veilles de la soirée (`journalctl`), la batterie ayant été tenue par `garder-batterie --pid`. Ce qu'elle avait payé
le 02/10 s'est relu (elle a dépassé en vingt minutes l'endroit où elle était morte). La mesure, par
`reload-course2-mesure.sh` (l'étape 4 de `reload-suite.sh` à l'identique, `--tempo 136`), contre la référence :

| mesure (outil du § 0) | référence (§ 2.4) | course 2 |
|---|---|---|
| diapason (écart) | −12,7 cents | **−6,1 cents** (reconstruction +6,2 ; concentration 0,34) |
| sub · basse · bas-médium | −1,33 · −3,15 · +2,72 | −1,13 · −3,26 · +2,67 |
| médium · haut-médium · aigus | +1,66 · −3,01 · −6,94 | +0,96 · −2,82 · −7,54 |
| niveau : décalage (pire tranche) | −0,57 (5,43) | −0,83 (**8,23**) |
| kick \|médian\| · p90 (appariées) | 4,35 · 8,71 (408) | 4,35 · 10,16 (476) |
| largeur | 0,0001 | 0,0 |
| log-mel moyen (médian) | 10,09 (8,53) | **11,67 (9,90)** |
| distance globale (`rapport.json`) | 0,2415 | 0,2402 |

**Les attendus du § 4 que la course 2 devait trancher** : l'écart de diapason sous 5 cents — **échec** (−6,1) ; le
log-mel au plus au témoin — **échec** (+1,58 dB). **Le diapason estimé reste éteint par défaut**, comme le § 4.3
l'avait laissé. Ce que la course ne permet pas de dire, et qui était su en la lançant : elle porte DEUX variables
(H42 et H45) contre la référence, et les deux reculs ne s'attribuent pas ; la distance de la chaîne, elle, avance
de 0,5 %, et c'est la troisième fois dans ce chantier qu'elle et le log-mel disent deux choses différentes (§ 9.2).
**Relevé, sans conclusion** : le projet porte bien `referenceA4Hz` = 443,14 Hz, et le rendu ne se lit qu'à +6,2 cents
avec une concentration de 0,34 (0,89 pour la référence) — les machines retenues ont changé (un thérémine, un
générateur stochastique, un diviseur, deux multi-échantillons), et des machines qui glissent ou bougent leur hauteur
dispersent l'estimateur ; ce n'est pas mesuré ici.

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

## 8. H46 — le tempo à la précision d'une GRILLE : l'estimation affinée par la cohérence de phase des attaques (écrite AVANT la mesure, 30/09/2026, 17 h 15)

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

### 8.1 Verdict de H46 (30/09/2026, 17 h 25) : TENUE — les six attendus, et ce que trente morceaux synthétiques ne prouvent pas

`tools/tempo-estime.py <lot> --affine` (branche `reload-h46`, `b2607ca`, commitée
AVANT la mesure), `s1-sec` d'abord, puis `s2` et `s1-prod` sans rien changer entre :

| lot | dérive ≤ 20 ms | dérive médiane, affiné | dérive médiane, témoin (tempo suivi) | non concluants | empirés |
|---|---|---|---|---|---|
| `s1-sec` (10 extraits de 30 s) | **10 / 10** | 0,0 ms | 246 ms | 0 | 0 |
| `s2` (10 morceaux de 186 à 269 s) | **10 / 10** | 0,0 ms | **1 671 ms** | 0 | 0 |
| `s1-prod` (30 s, avec production) | **10 / 10** | 0,0 ms | 246 ms | 0 | 0 |

| # | attendu | verdict |
|---|---|---|
| 1 | `s1-sec` : ≤ 20 ms sur 9 / 10 | **tenu** (10 / 10) |
| 2 | le contrôle : aucun morceau empiré, sur les trois lots | **tenu** (0 sur 30 — dont `g1`, que le suivi lisait déjà juste à 110,0) |
| 3 | `s2` : ≤ 20 ms sur 9 / 10 | **tenu** (10 / 10) |
| 4 | `s1-prod` : ≤ 20 ms sur 8 / 10 | **tenu** (10 / 10) |
| 5 | les non concluants comptés et nommés | **tenu** (0 sur 30) |
| 6 | sans l'option, rien ne change | **tenu** (test : `affinage` absent, les quatre champs d'avant, tempo au dixième) |

**CE QUE LE « 0,0 ms » CACHE, ET QU'IL FAUT DIRE.** Les trente vérités sont des tempos
ENTIERS, et la règle de l'entier a été retenue trente fois sur trente : la dérive
tombe à zéro par construction du corpus. Le chiffre qui juge le MÉCANISME est celui du
maximum de cohérence brut (colonne « max. » de l'outil), sans la règle : le pire écart
est de **0,053 BPM** (`g8` de `s1-sec`, 13,0 ms de dérive) ; sur `s2`, **0,011 BPM**
(`g8` encore, 16,4 ms) ; sur `s1-prod`, 0,050 BPM (12,3 ms). **Trente sur trente sous
20 ms SANS la règle de l'entier** — les attendus tiennent sans elle. Ce que la règle
risque, elle — arrondir un tempo qui n'est PAS entier —, aucun de ces lots ne le
mesure : seuls un test de construction (127,4 rendu à 0,01 près, non arrondi) et « B4 Wuz
Then » ci-dessous le touchent.

**`g8` est le moins cohérent des trois lots, et ce n'est pas expliqué** : 0,31 à 0,35
quand les vingt-sept autres mesures sont à 0,91 et plus. Il conclut (seuil 0,20, 255 à
1 226 attaques), avec une marge de 1,5 ; pourquoi sa grille s'ajuste moins bien n'a pas
été cherché.

**LA GARDE, VUE ROUGE.** La fenêtre de ± 4 % réduite à rien (l'affinage ne peut plus
rien affiner) : **1 / 10** sur `s1-sec`, dix « non concluant (maximum au bord de la
fenêtre) » nommés, dérive médiane 245,8 ms — celle du témoin —, verdict TOMBE, code 1.

**Les deux enregistrements réels — publiés, sans vérité, ils ne valident rien :**

| morceau | tempo suivi | affiné | cohérence | attaques | entier |
|---|---|---|---|---|---|
| « Reload » (312 s) | 139,7 | **138,000** (maximum à 138,001) | 0,93 | 2 098 | retenu |
| « B4 Wuz Then » (354 s) | 126,0 | **126,973** | 0,96 | 2 104 | NON retenu |

« B4 Wuz Then » n'est donc pas à 126 : à 126,0, la grille que la chaîne lui écrit
depuis le 15/09 dérive de **2,7 s** d'un bout à l'autre du morceau. Et l'entier voisin,
127, n'est pas retenu — il coûterait 75 ms de dérive : un enregistrement réel n'a pas à
tomber sur un entier, et la règle ne l'y force pas.

**CE QUE CES LOTS NE PROUVENT PAS.** Trente morceaux synthétiques ont leurs notes
EXACTEMENT sur la grille, un tempo fixe et entier, aucun rubato ; le ternaire n'y est
pas (un test de construction seulement : des triolets à 96 lus sur la grille de
sextolets). Et l'affinage GARDE le niveau métrique du suivi : vu en écrivant les
tests, une pulsation nue de croches à 138 est suivie à 92 par `beat_track`, et
l'affinage rend 92,0 — la même grille physique (six par temps à 92, quatre à 138), le
mauvais chiffre. L'erreur d'octave
reste l'affaire de `tools/tempo-estime.py` sans `--affine`, comme écrit.

**CE QUE LE VERDICT DÉCIDE.**
- `--tempo-affine` entre dans la prochaine course de « Reload » (la course 3, avec H42
  et H45) ; la course 2, déjà en file, part sans elle, comme elle a été écrite.
- **Elle deviendra le défaut de la chaîne, mais pas avant la fin de la campagne S2** :
  le tempo entre dans la clé du cache de mesures et dans chaque rendu (une machine
  peut caler un arpège ou un retard dessus), et `g7`-`g10` doivent courir la chaîne de
  `g1`-`g6`. Le basculement se fera dans un commit à lui, avec son A/B à une variable
  sur un morceau du banc — pour MESURER que le tempo écrit ne change pas le son, au
  lieu de le supposer.
- Le code reste sur `reload-h46` tant qu'une course tourne sur l'arbre principal
  (règle du dépôt : `analyse/analyzer/` ne se touche pas pendant une course).
- **Reste nommé : la PHASE de la grille.** À tempo juste, les notes tombent à un écart
  constant de la grille ; la mesure 1 commence toujours au tick 0 et non au premier
  temps fort. C'est la suite (le « reste nommé » du § 5 quindecies).

---

## 9. H47 — le parc contient-il le pad ? Des notes TENUES idéales, le diapason juste, et chaque machine mesurée sur le pad seul (écrite AVANT la mesure, 30/09/2026, 17 h 35)

**Pourquoi maintenant.** Le § 3 remet la décision sur les machines neuves à « après
ces trois-là » — le diapason, la transcription hachée, le même instrument joué par
quatre machines —, et la course 2 qui les réunit ne finira pas avant demain (la
référence a été relancée à 17 h 01, § 2.1). Or la question « existe-t-il, dans le
parc, une machine qui sonne comme ce pad ? » ne dépend pas de la transcription : elle
se pose avec des notes PARFAITES. La poser à part, sur vingt-quatre secondes, la
tranche ce soir — et dit tout de suite s'il faut décrire une machine.

**L'extrait.** Le stem « other » de la course de référence, de **16 à 40 s** : le pad
y est seul (il entre à 14 s, la basse à 42 s, § 1).

**L'ORACLE — des notes écrites par une règle, pas à la main.** Le spectre de Welch de
l'extrait (la fenêtre de l'estimateur de diapason) ; chaque pic de proéminence ≥ 12 dB
entre 100 et 2 000 Hz, à moins de 20 dB du plus fort, devient UNE note tenue sur tout
l'extrait, à la hauteur MIDI la plus proche au diapason de 443,14 Hz, vélocité 100.
Deux pics sur la même note n'en font qu'une.

**LES DEUX BORNES — ce que vaudrait un timbre parfait.** Avant toute machine :
- `B_égal` : des sinus purs aux fréquences des notes de l'oracle, **amplitudes
  égales** — ce que peut au mieux une machine parfaitement sinusoïdale jouant ces
  notes à vélocité égale ;
- `B_mesuré` : des sinus aux fréquences et **amplitudes mesurées** des pics — ce que
  vaudrait, en plus, un niveau juste par note.
L'écart entre les deux est ce que coûte l'absence de niveau par note, machine
quelconque. Ni l'une ni l'autre ne bat : ce qui reste sous `B_mesuré` est le
battement du pad et le résidu de séparation.

**LES MESURES** (chacune contre l'extrait, le rendu calé au niveau de l'extrait) :
- `D` — la distance de la chaîne (métrique v2), celle que l'arbitrage emploie : c'est
  elle qui CHOISIT ;
- l'**équilibre par bande** du § 0 (six bandes, chacune relative au total) sur les
  bandes qui portent à −40 dB du total **dans l'extrait ou dans le rendu** — une
  machine brillante là où l'extrait se tait doit se voir ;
- le **log-mel** du § 0, mais sur les seules cases qui portent (à 40 dB du maximum de
  l'extrait) : sur un stem, les cases vides opposent un plancher de séparation à un
  silence numérique, et la moyenne du § 0 ne mesurerait qu'elles. Ce n'est donc PAS
  le chiffre du § 0, et il ne se compare qu'aux bornes ;
- la **tenue** : niveau du dernier tiers de la note rapporté au premier (dB) — une
  machine frappée ne peut pas jouer une note de vingt-quatre secondes, quel que soit
  son timbre.

**Les candidates** : celles de la chaîne — les machines mélodiques du registre à leur
patch d'usine, le multi-échantillons une fois par profil —, rendues par le
`vsm-render` de `build-h42` au diapason de 443,14 Hz. Puis le **réglage de piste de
la chaîne** (`refine_patch_on_track`, son budget de 40 évaluations) sur les cinq
premières au classement `D`.

**ATTENDUS** :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | les notes de l'oracle | les cinq classes de hauteur du § 1 (si, do♯, mi, fa♯, la♯), aucune autre | une classe manque ou s'ajoute : c'est dit, la règle n'est pas retouchée |
| 2 | **l'instrument** : les bornes | `B_mesuré` ≤ `B_égal` sur le log-mel ; l'extrait contre lui-même = 0 | sinon l'instrument est faux et rien ne se lit |
| 3 | le meilleur patch d'USINE (premier au classement `D`) | chaque bande qui porte à ≤ 1 dB, et log-mel ≤ `B_égal` + 1 dB | une bande à plus de 3 dB, ou log-mel > `B_égal` + 3 dB |
| 4 | le meilleur après réglage de piste (les cinq premières) | comme 3 | comme 3 |
| 5 | **la chaîne sait-elle CHOISIR ?** corrélation de rang (Spearman) entre `D` et le log-mel, toutes candidates | ≥ 0,7 | < 0,4 — le pad existerait-il, la chaîne ne le désignerait pas |
| 6 | les cinq premières au classement `D` tiennent la note (tenue ≥ −6 dB) | trois au moins | aucune |

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) :
- 3 ou 4 tenu : **le parc a le pad**. Aucune machine neuve pour lui ; ce qui sépare
  encore la reconstruction de l'original est dans la transcription (H45), le niveau
  par note (l'écart entre les deux bornes le chiffre) et le mélange.
- 4 entre 1 et 3 dB : il manque un PATCH, pas une machine — un profil de pad
  sinusoïdal à écrire pour la machine la plus proche.
- 4 en échec : la machine manquante est décrite ici (ce qu'elle doit savoir faire,
  mesuré sur l'extrait) et construite selon `CDC-nouvelle-machine.md`.
- 5 en échec, quel que soit le reste : le défaut est dans la MÉTRIQUE de l'arbitrage,
  et c'est elle que l'hypothèse suivante vise.

### 9.1 Ce que le § 9 laissait à l'instrument — écrit avec le code, AVANT la mesure (30/09/2026, 19 h 40)

L'instrument est `analyse/mesure_h47.py` (branche `reload-h47`, tirée de
`reload-chaine` : il lui faut le diapason de H42, et `analyse/analyzer/` de l'arbre
principal ne se touche pas pendant la course de référence). Dix tests
(`analyse/tests/test_h47_pad.py`) le verrouillent sur des signaux à vérité connue et
sur des mesures fabriquées ; quatre défauts remis à la main les font tomber (la règle
des 20 dB portée à 40, la largeur de bruit de la fenêtre oubliée, le seuil « porte »
ouvert à tout, une borne jouée un demi-ton trop haut). Sept choses que le § 9
n'écrivait pas, tranchées ici et commitées avant toute mesure sur « Reload » :

1. **Le spectre de l'oracle** est le Welch de l'estimateur de diapason (65 536 points,
   recouvrement de moitié) sur l'extrait entier, mêmes pics, interpolés à la parabole.
   **L'amplitude d'un pic**, pour `B_mesuré` : la puissance sommée sur ± 4 cases
   (± 2,7 Hz — le pic du pad fait 2 Hz de large, § 1), divisée par la largeur de bruit
   de la fenêtre de Hann (1,5 case). Test : un sinus rendu à 3 % près.
2. **L'équilibre** se lit par tranches de 4 mesures à 138,00 BPM (6,957 s, trois
   tranches pleines dans l'extrait), écart MÉDIAN par bande comme au § 0 ; une bande
   porte dans une tranche si elle y dépasse −40 dB du total dans l'extrait OU dans le
   rendu. Le code est celui de `tools/ecart-a-l-original.py`, chargé tel quel.
3. **Le rendu est calé** au niveau efficace de l'extrait avant l'équilibre et le
   log-mel ; `D` reçoit le rendu brut, comme dans la chaîne (elle est insensible au
   niveau).
4. **Le classement `D` est celui de la chaîne** : une candidate que le garde-fou de
   niveau de l'arbitrage écarterait (plus de ×10 au fader pour atteindre l'extrait)
   est mesurée et publiée, mais HORS classement — la chaîne ne la choisirait jamais.
   Un rendu vide est « non mesuré », nommé, jamais compté zéro. Le Spearman de
   l'attendu 5 porte sur le classement ; celui de toutes les candidates mesurées est
   publié à côté, avec le nombre de valeurs DISTINCTES de chaque série.
5. **« Les cinq premières » sont cinq CANDIDATES** (machine + profil), pas cinq
   machines : si cinq profils du multi-échantillons sont en tête, ce sont eux qui
   sont réglés.
6. **Le projet rendu** porte le tempo mesuré (138,0 — il ne sert qu'à une machine
   qui calerait un arpège ou un retard dessus) et UNE note par hauteur de l'oracle,
   de 0 à la fin de l'extrait.
7. **Entre « réussite » et « échec »** (attendus 3 à 6), l'outil écrit « entre les
   deux » : c'est le cas que « ce que le verdict décidera » range sous « il manque un
   PATCH ».

**Ce que l'instrument ne voit pas, dit avant.** L'extrait commence à 16 s, le pad
sonnant déjà ; le rendu commence par l'ATTAQUE de la machine — une attaque lente y
perd sur les premières trames sans être un mauvais pad. Et le stem porte un résidu
de séparation qu'aucune machine ne joue : les écarts de bande des deux BORNES sont
publiés à côté de ceux des machines. Si `B_mesuré` elle-même rate « chaque bande à
≤ 1 dB », l'attendu 3 est intenable tel qu'écrit par quelque machine que ce soit, et
ce sera dit tel quel — sans retoucher le seuil.

### 9.2 Verdict de H47 (30/09/2026, mesuré de 19 h 41 à 19 h 50) : l'instrument ne voit pas le pad — deux attendus en échec qu'AUCUNE machine ne pouvait tenir, et une métrique qui range des harpes devant les sinus de l'oracle

`analyse/mesure_h47.py mesurer` (branche `reload-h47`, `652ce7d`, arbre propre), le
stem « other » de 16 à 40 s, `vsm-render` de `build-h42` (md5 `52532381…`), diapason
443,1372 Hz, 560,6 s de mesure à côté de la course de référence ;
`reconstruction/travail/reload-h47/mesure.json`, verdict recalculé par `verdict`.

**L'oracle** : 10 pics, 10 notes — mi3, fa♯3, la♯3, si3, do♯4, mi4, fa♯4, la♯4, si4,
do♯5 (MIDI 52 à 73), soit les cinq classes du § 1 sur deux octaves, aucune autre.

**Les candidates** : **198** (58 machines mélodiques, dont le multi-échantillons
une fois par profil : 141 profils). 190 au classement `D`, 8 hors classement par le
garde-fou de niveau (dont un profil muet), aucune non mesurée. 164 sur 190 tiennent
la note à −6 dB.

| | `D` | log-mel (cases qui portent) | bandes (écart médian, dB) |
|---|---|---|---|
| l'extrait contre lui-même | 0,000 | 0,00 | toutes à 0,00 |
| `B_égal` | 0,471 | 13,14 | sub −0,60 · basse +5,70 · bas-médium +0,84 · médium −3,43 |
| `B_mesuré` | 0,429 | 11,32 | sub −3,96 · basse +0,84 · bas-médium +0,55 · médium −1,81 |
| 1ʳᵉ à `D` : multi-échantillons « GU-Harp » | 0,353 | 9,37 | sub −14,89 · médium −7,54 · haut-médium +9,38 |
| la même, réglée (38 évaluations) | 0,295 | 9,18 | sub −14,93 |
| 5ᵉ à `D`, réglée : « FR3-Fretless-Bass » | 0,380 → 0,282 | 7,20 | haut-médium +25,75 |
| 1ʳᵉ au log-mel : « GU-Acoustic-Bass » (19ᵉ à `D`) | 0,468 | 6,18 | basse +9,27 · haut-médium +16,26 |

| # | attendu | verdict |
|---|---|---|
| 1 | les cinq classes, aucune autre | **tenu** |
| 2 | l'instrument : l'extrait contre lui-même = 0, `B_mesuré` ≤ `B_égal` | **tenu** (0,000 ; 11,32 ≤ 13,14) |
| 3 | le meilleur patch d'usine : chaque bande à ≤ 1 dB, log-mel ≤ `B_égal` + 1 dB | **échec** (« GU-Harp » : sub −14,89 dB ; log-mel 3,78 dB SOUS `B_égal`) |
| 4 | le meilleur après réglage | **échec** (« FR3-Fretless-Bass » 0,282 : haut-médium +25,75 dB) |
| 5 | Spearman entre `D` et le log-mel ≥ 0,7 | **échec** : **−0,018** sur 190 (167 `D` distinctes, 160 log-mel distincts) ; −0,057 sur les 198 |
| 6 | trois des cinq premières tiennent la note | **tenu** (5 sur 5 : −0,75 · −1,23 · −1,23 · 0,00 · −5,41 dB) |

**CE QUE CES ÉCHECS NE DISENT PAS — le § 9.1 l'avait écrit, et c'est le cas.**
`B_mesuré`, des sinus aux fréquences et amplitudes mesurées sur l'extrait, **rate
elle-même l'attendu 3** (sub −3,96 dB, médium −1,81 dB) ; `B_égal` aussi (basse
+5,70). « Chaque bande à ≤ 1 dB » n'était tenable par aucune machine : les échecs
3 et 4 ne disent donc RIEN du parc, et **aucune machine n'est décrite sur leur
foi**. Le seuil n'est pas retouché ; l'attendu est déclaré mal posé.

**ET LES BORNES NE BORNENT PAS.** Le § 9 tenait `B_égal` pour « ce que peut au mieux
une machine parfaitement sinusoïdale ». Mesuré : **154 candidates sur 190 font mieux
que `B_égal` au log-mel, 141 mieux que `B_mesuré`** ; à `D`, 19 et 11. Une harpe,
un piano électrique et une basse échantillonnés passent devant les sinus de
l'oracle, aux deux métriques. Cinq des « cinq premières » sont des profils du
multi-échantillons, dont un doublon (« FR3- » et « MS-E-Piano-Tine » sont la même
banque sous deux noms : mêmes chiffres au millième).

**Relevé APRÈS la mesure, pour comprendre — il ne change aucun verdict.** De quoi
l'extrait est fait (la règle du dépôt : décomposer avant d'expliquer) :
- **99,9 %** de sa puissance est entre 100 et 2 000 Hz ; le sub est à **−39,8 dB**
  du total et la basse à −41,1 : la « pire bande sub » de l'attendu 3 juge **0,01 %**
  de la puissance, une bande entrée au ras du seuil « porte » de −40 dB ;
- **les dix pics de l'oracle (± 2,7 Hz) portent 68,3 % de la puissance** ; le tiers
  restant est dans les mêmes bandes (bas-médium : 58,7 sur 74,4 % ; médium : 9,6 sur
  25,5 %), et ce n'est pas de la batterie qui fuit : la séparation
  harmonique/percussive rend **94,8 % d'harmonique, 0,3 % de percussif** ;
- aucun pic hors de l'oracle n'approche : le suivant est à −34 dB (150,1 Hz).

**Un tiers du pad n'est donc PAS dans ses dix raies étroites**, et il est tonal : des
raies plus larges que ± 2,7 Hz — une modulation (battement de plusieurs oscillateurs,
chorus, vibrato), ou une trace de la séparation, et rien ici ne dit lequel. C'est ce
tiers que des sinus fixes ne jouent pas, et que des timbres échantillonnés, riches et
mouvants, remplissent par accident : les deux métriques récompensent le remplissage.
Le § 1 disait « presque sinusoïdal » sur la foi d'un spectre MOYEN ; la moyenne
cachait le mouvement.

**CE QUE LE VERDICT DÉCIDE.**
- **Rien sur les machines neuves.** Ni « le parc a le pad », ni « il manque une
  machine » : l'instrument ne voyait pas ce qu'il devait juger. H47 est close comme
  **non concluante sur sa question**, ses deux attendus d'instrument tenus, deux mal
  posés, et son attendu 5 en échec franc.
- **L'attendu 5 tient ce qui était écrit avant** (« le défaut est dans la MÉTRIQUE »),
  avec une précision que la mesure impose : ce n'est pas `D` contre un log-mel qui
  aurait raison — AUCUNE des deux ne met `B_mesuré` en tête. Une métrique ne se juge
  que contre une description du pad qui ne dépende d'aucune des deux.
- **La suite est donc de DÉCRIRE le pad** — ses raies une à une, leur niveau, leur
  largeur, leur mouvement — **sur l'ORIGINAL et non sur le stem**, pour séparer ce
  qui est au morceau de ce qui est à la séparation : c'est H49 (§ 11). La machine, ou
  le patch, se décidera sur cette description, comme le § 3 le demandait (« ce
  qu'elle doit savoir faire, mesuré sur l'original »).

---

## 10. H48 — une course morte reprend ce qu'elle avait payé : les mesures de PROJET rangées sur disque (écrite AVANT la mesure, 30/09/2026, 19 h 56)

**Pourquoi maintenant, et pourquoi ici.** Le § 2.2 nommait le défaut et le remettait
« à quand aucune course ne tournera ». C'était mal lu : la règle interdit de toucher
l'arbre QUE LA COURSE IMPORTE, pas d'écrire dans un autre. Le code est donc écrit
dans un arbre à part (branche `reload-h48`, tirée de `reload-chaine`), pendant que
la référence tourne — elle ne tient que si personne n'éteint le poste d'ici cinq
heures, et la course 2 qui la suit sera aussi longue.

**Le constat, lu dans le code.** Le verdict du mélange (`keep_what_helps_the_mix`),
le réglage au mélange (`refine_against_mix`) et le second verdict
(`project_mix_distance`) font tous la même chose : écrire le projet, le rendre par
`vsm-render`, mesurer sa distance au morceau. Aucune de ces mesures n'est rangée —
le cache de mesures (H2, `ROADMAP-fusion.md` § 5 duodecies) ne connaît que les
candidates de PISTE. Sur « Reload » : 6 142 s de verdict et 6 772 s de réglage pour
trois voix, perdues deux fois.

**L'hypothèse.** Le moteur est déterministe et la suite des états d'une course aussi :
ranger chaque mesure de projet sous une clé qui dit TOUT ce que le moteur lit suffit
à ce qu'une course relancée rejoue la morte sans la repayer, et arrive au même
projet, au bit près.

**La clé** hache le DOSSIER écrit par `write_project_bundle` — chaque fichier, par
chemin relatif et contenu, hors `rendu.wav` qui est une sortie —, la fréquence,
l'empreinte du moteur, puis la métrique et l'empreinte de la cible. Le dossier plutôt
qu'une liste de champs : la clé d'un rendu de piste énumère les siens et en avait
oublié un, le diapason (§ 4.2) ; un projet en porte bien plus (volumes, effets,
automation, routage des groupes, échantillons), et hacher ce que le moteur lit ne
peut rien oublier de ce qui s'y trouve. Ce qu'elle ne voit pas, comme la clé d'une
piste : le CONTENU d'un profil installé, désigné par son nom.

**Ce qui n'est PAS rangé, dit avant** : les rendus solo du calage de niveau
(`recaler_avec_son_groupe`), rejoués à chaque évaluation. Déterministes, ils
redonnent les mêmes volumes, donc les mêmes clés ; leur coût reste dû à la reprise,
et l'attendu 2 le chiffre.

**L'option** est celle du cache existant (`--sans-cache-rendus` coupe les deux), déjà
dans la provenance ; le rapport gagne `options.mesuresDeProjet` (payées, relues) et
le journal une ligne — une mesure relue n'est pas une mesure payée, et cela se dit.

**LA MESURE.** `s1-sec/morceau-0001-g1` (30 s), ses stems déjà séparés
(`s1-sec-banc/…/stems-separes/stems`), la chaîne de `reload-h48`, le `vsm-render` de
`build-h42`, 2 rendus parallèles, un cache VIDE au départ (celui de l'arbre à part),
quatre courses l'une après l'autre, à côté de la course de référence :
- **T**, le témoin : `--sans-cache-rendus` ;
- **A** : avec le cache, qu'elle remplit ;
- **B** : la même, rejouée sur le cache de A ;
- **C** : cache vidé ; la course est TUÉE (`SIGKILL` sur tout son groupe, ce que fait
  une extinction) à la première ligne « réglage au MÉLANGE » de son journal, puis
  relancée telle quelle jusqu'au bout.

**ATTENDUS** :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | **l'identité** : `project.json` de A, B et C contre celui de T (`cmp`) ; dans `rapport.json`, la distance globale, le verdict du mélange et les distances par stem | identiques, à l'octet et au dernier chiffre | une seule différence : le cache n'entre pas dans la chaîne |
| 2 | **le rejeu** : durée des étapes au mélange de B (verdict + réglages + seconds verdicts, lues au journal) rapportée à A | ≤ 25 %, et 0 mesure payée dans B | > 60 % |
| 3 | **la mort** : dans C relancée, le verdict du mélange et le premier réglage rapportés aux mêmes étapes de C tuée | ≤ 25 % ; et C finit identique à T (attendu 1) | > 60 %, ou un projet différent |
| 4 | **le témoin n'écrit rien** : fichiers dans le cache après T, et ligne « mesures de projet » à son journal | 0 et aucune | sinon l'option ne coupe pas ce qu'elle dit couper |
| 5 | **ce que coûte la première passe** : étapes au mélange de A rapportées à T | ≤ 110 % | > 125 % |

Si T et A diffèrent, un second témoin T′ (même commande que T) dira si la chaîne
diffère d'elle-même d'une course à l'autre : l'attendu 1 ne se lit qu'avec lui.

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) :
- 1, 2 et 3 tenus : le code entre dans `reload-chaine` AVANT le départ de la course 2
  — une première course ne relit rien, elle fait exactement les calculs d'avant et
  les range ; le cache ne sert que si elle meurt. Et dans l'arbre principal dès
  qu'aucune course n'y tourne.
- 1 en échec : rien n'entre nulle part, et la différence est cherchée avant tout.
- 2 ou 3 entre les deux : le cache entre (il ne peut pas nuire si 1 tient), et le
  calage de niveau devient la suite.

**Ce que cette mesure ne prouve pas** : un morceau de 30 s n'a ni report vocal de
55 Mo à hacher à chaque évaluation, ni onze pistes ; le coût de la clé sur « Reload »
se lira sur la course 2 (ligne « mesures de projet » de son journal).

### 10.1 Verdict de H48 (mesuré le 30/09 de 20 h 09 à 21 h 32 ; écrit le 01/10 à 22 h 43) : 1, 2, 4 et 5 TENUS, 3 ENTRE LES DEUX — le cache entre, comme écrit

**Écrit un jour après sa mesure, et c'est dit.** `verdict_h48.py` a rendu son verdict à
21 h 32 le 30/09 (`reconstruction/travail/h48/verdict.txt`) ; le poste a été éteint à
23 h 10 (`journalctl -b -2` : `org.kde.Shutdown`, une extinction demandée depuis le
bureau) avant qu'il entre ici. Les chiffres ci-dessous sont ceux du fichier, relus, pas
recalculés.

**Deux départs.** Le premier (19 h 57, code `8906218`) a été arrêté à la main à 20 h 06 :
le témoin T était mort à l'étape 4/5 (« vsm-render introuvable » — le calage de niveau
ignorait `--moteur`) et avait laissé **3 fichiers** au cache (l'arbitrage de batterie
écrivait sous `--sans-cache-rendus`). Deux défauts de la chaîne trouvés par le témoin
avant toute mesure, corrigés dans `0baad56`. Le second départ (20 h 09, code `0baad56`,
arbre propre, moteur `build-h42` md5 `52532381`) est la mesure.

| course | étapes au mélange (verdict ; réglages ; seconds verdicts) | total | mesures de projet (payées, relues) |
|---|---|---|---|
| T, témoin `--sans-cache-rendus` | 206 ; 127 · 103 · 113 ; 59 · 112 · 110 | 830 s | — (aucune ligne) |
| A, cache vide qu'elle remplit | 158 ; 140 · 110 · 117 ; 65 · 130 · 150 | 870 s | 242, 34 |
| B, rejouée sur le cache de A | 28 ; 72 · 19 · 20 ; 13 · 13 · 11 | 176 s | 0, 276 |
| C tuée (`SIGKILL` du groupe à sa 1re ligne « réglage au MÉLANGE ») | 193 ; 176 ; — | 369 s | — (morte) |
| C relancée telle quelle | 37 ; 74 · 143 · 159 ; 73 · 151 · 152 | 789 s | 150, 126 |

| # | attendu | verdict |
|---|---|---|
| 1 | l'identité contre T | **tenu** : `project.json` de A, B et C identiques à l'octet ; rapport hors provenance, 0 différence ; distance globale 0,1959822425659897 partout. La provenance diffère de 2 ou 3 champs, ceux que H48 y ajoute (`cacheRendus`, `mesuresDeProjet.payees`, `.relues`) |
| 2 | le rejeu : B rapportée à A | **tenu** : 176 s pour 870, **20,2 %** ; 0 mesure payée dans B |
| 3 | la mort : C relancée rapportée à C tuée, verdict + 1er réglage | **entre les deux** : 111 s pour 369, **30,1 %** (seuil 25 %) ; C finit identique à T |
| 4 | le témoin n'écrit rien | **tenu** : 0 fichier au cache après T, pas de ligne « mesures de projet » |
| 5 | le coût de la première passe : A rapportée à T | **tenu** : 870 s pour 830, **104,8 %** |

**Ce qui manque à l'attendu 3, lu sur les chiffres.** Le verdict de C relancée descend
à 37 s (19 % des 193 de la course tuée) ; c'est le premier réglage qui retient : 74 s,
autant que dans B (72 s), où **rien** n'est payé. Ce reste n'est donc pas une mesure
que la reprise rate : c'est un coût que le cache ne range pas, et il est propre à la
basse (les deux autres réglages de B prennent 19 et 20 s). Le § 10 nommait d'avance
ce que le cache ne range pas — les rendus solo du calage de niveau ; que ce soient
EUX qui coûtent ces 72 s n'est pas mesuré ici, et ne s'écrit pas comme établi.

**CE QUE LE VERDICT DÉCIDE — la règle écrite avant, appliquée telle quelle** (« 2 ou 3
entre les deux : le cache entre, et le calage de niveau devient la suite ») :
- **le cache entre** : `reload-h48` fusionnée dans l'arbre principal le 01/10
  (`1259f93`), aucune course n'y tournant — la suite Python entière sur l'arbre
  fusionné, **257 réussis, 0 échoué** ; ruff et mypy verts après quatre annotations
  manquantes dans les verdicts de H44 et H45 (`5a37823`). `reload-chaine` avancée au
  même point : la course 2 partira avec lui. À 440 Hz la clé d'un rendu de PISTE est
  celle d'avant (`efc0a27`) : les mesures déjà payées par la course de référence restent
  valables, et la référence relancée (§ 2.3) range désormais ses mesures de projet ;
- **la suite** : ce que coûte le premier réglage de la basse quand tout est relu —
  décomposé AVANT d'être attribué au calage de niveau.

**Ce que la mesure ne prouve pas, toujours** : le coût de la clé (hacher le dossier à
chaque évaluation) sur un morceau de 312 s à report vocal de 55 Mo. Il se lira sur la
ligne « mesures de projet » du journal de la référence relancée.

### 10.2 La suite de H48 : où passent les 72 s du premier réglage de la basse, TOUT RELU ? (écrit AVANT le profil, 02/10/2026, 00 h 34)

**La question, laissée par le § 10.1.** Dans la course B (tout relu du cache, 0 mesure
payée), le premier réglage au mélange — la basse — prend encore **72 s**, quand les deux
suivants en prennent 19 et 20. Le § 10 nommait d'avance ce que le cache ne range pas :
les rendus SOLO du calage de niveau (`recaler_avec_son_groupe`), rejoués à chaque
évaluation. L'attribution n'a pas été mesurée ; elle se mesure ici.

**La mesure.** La course B rejouée telle quelle (même commande, même arbre `reload-h48`,
même cache de 839 fichiers, même moteur `build-h42`, 2 rendus parallèles, `nice 10`),
sous un pilote qui PROFILE le seul premier appel de `refine_against_mix` (`cProfile`,
temps cumulé par fonction appelée) ; `reconstruction/travail/h48/B-profil/`.

**ATTENDUS** :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | **le témoin** : le temps profilé du premier réglage, contre les 72 s du journal de B | à ± 25 % (54 à 90 s) | ailleurs : la course rejouée n'est pas celle de B, rien ne s'attribue |
| 2 | **l'attribution** : la part du temps cumulé sous `recaler_avec_son_groupe` | ≥ 60 % : le calage de niveau est bien le coût que le cache ne range pas | < 30 % : la prédiction du § 10 est fausse, et la fonction qui porte le plus est nommée |

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) : 2 tenu — ranger les rendus solo du
calage dans le cache de mesures (une hypothèse de plus, son A/B écrit avant) ; 2 en
échec — la fonction nommée devient la cible ; entre les deux — les deux premières
fonctions sont publiées, rien n'est entrepris sur cette foi.

### 10.3 Verdict du § 10.2 (02/10/2026, profilé de 00 h 35 à 00 h 41, écrit à 00 h 41) : 97 % du premier réglage, tout relu, sont les rendus SOLO du calage de niveau

La course B rejouée sous le pilote (`reconstruction/travail/h48/profil-B.py`) : mêmes
décisions au chiffre près (basse 0,2670 → 0,2310 en 30 évaluations ; guitare, autre
idem), **0 mesure de projet payée, 276 relues**, cache inchangé (839 fichiers).

| # | attendu | verdict |
|---|---|---|
| 1 | le témoin : le temps profilé contre les 72 s de B (54 à 90 s) | **tenu** : **56,9 s** profilées, 57 s au journal de la course rejouée |
| 2 | la part sous `recaler_avec_son_groupe` (≥ 60 %) | **tenu** : **55,1 s sur 56,9, 97 %** |

Le profil, fonction par fonction (temps cumulé) : `recaler_avec_son_groupe` →
`match_track_levels` → `_render_track` → `render_track_offline`, **31 rendus solo,
54,7 s** (1,8 s chacun) ; l'écriture des projets 1,4 s ; la distance relue 1,3 s ; la
CLÉ du cache de projet (hacher le dossier) **0,1 s** pour 30 appels — le coût que le
§ 10 craignait pour « Reload » ne se voit pas ici.

**CE QUE CELA DÉCIDE, comme écrit avant** : ranger les rendus solo du calage dans le
cache de mesures — une hypothèse de plus (H58 ; H57 est réservée au pad, § 18.2), son
A/B écrit avant sa mesure. Ce que le cache en rangerait n'est pas l'audio (un rendu
solo de « Reload » pèse 55 Mo) mais le seul nombre que le calage en tire, le niveau
efficace du rendu, sous une clé qui hache le dossier écrit pour lui — la règle de H48.

### 10.4 H58 — les rendus SOLO du calage de niveau rangés à leur tour : une course relancée ne repaie plus le calage (écrite AVANT la mesure, 02/10/2026, 00 h 43)

**Pourquoi.** § 10.3 : tout relu, 97 % de ce que coûte encore un réglage au mélange sont
les rendus solo du calage de niveau (`match_track_levels`, `_caler_un_groupe`), que
le cache de H48 ne range pas. Sur « Reload », chaque évaluation du réglage d'une voix
de l'« other » en rend QUATRE (le groupe entier), sur 312 s.

**L'hypothèse.** Le calage ne tire de ses rendus solo qu'UN nombre : le niveau efficace
du rendu — ou, pour un groupe, de la SOMME des rendus de ses membres — sur la longueur
du stem. Le ranger sous une clé qui hache ce que le moteur lit suffit à ce qu'une
course relancée ne rende plus rien pour caler, et arrive au même projet, au bit près.

**La clé** : pour une piste seule, la clé de projet de H48 (`cle_de_projet` : le dossier
écrit pour le rendu solo, échantillons recopiés compris, la fréquence, l'empreinte du
moteur) jointe à la DURÉE rendue et au nombre d'échantillons du stem (le niveau se
prend sur `min(stem, rendu)`) ; pour un groupe, les clés de ses membres DANS L'ORDRE,
jointes de même. Rien du CONTENU du stem n'y entre : le niveau du rendu n'en dépend
pas.

**L'option** est celle du cache existant (`--sans-cache-rendus` coupe les trois) ; le
rapport gagne `options.niveauxSolo` (payés, relus) et le journal une ligne.

**LA MESURE** — le banc de H48, trois courses dans un arbre à part (`reload-h58`, tiré
de `master`), cache VIDE au départ, `vsm-render` de `build-h42`, 2 rendus parallèles,
`nice 10`, à côté de la course de référence : **T** (`--sans-cache-rendus`), **A** (cache
vide, qu'elle remplit), **B** (rejouée sur le cache de A).

**ATTENDUS** :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | **l'identité** : `project.json` de A et B contre T (`cmp`) ; dans `rapport.json`, hors provenance, aucune différence | identiques, à l'octet et au dernier chiffre | une seule différence |
| 2 | **le rejeu** : les étapes au mélange de B, rapportées aux 176 s de la course B de H48 (même banc, sans ce cache) | ≤ 50 % | > 80 % |
| 3 | **ce que B paie** : niveaux solo et mesures de projet payés dans B | 0 et 0 | sinon le cache ne range pas ce qu'il dit |
| 4 | **le témoin n'écrit rien** : fichiers au cache après T, lignes « niveaux solo » et « mesures de projet » à son journal | 0 et aucune | sinon l'option ne coupe pas ce qu'elle dit couper |
| 5 | **ce que coûte la première passe** : étapes au mélange de A rapportées à T | ≤ 110 % | > 125 % |

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) : 1 à 4 tenus — le code entre dans
`reload-chaine` et dans l'arbre principal dès qu'aucune course n'y tourne (la course de
référence tourne : il attendra sa fin) ; 1 en échec — rien n'entre, la différence est
cherchée ; 2 entre les deux — le cache entre (il ne peut pas nuire si 1 tient), et le
nouveau premier poste est profilé comme au § 10.2.

### 10.5 Verdict de H58 (02/10/2026, mesuré de 00 h 52 à 01 h 37) : TENUE — les cinq attendus ; une course relancée rejoue tout le mélange en 8 s

`reconstruction/travail/h58/` (branche `reload-h58`, `f013626`, arbre propre, moteur
`build-h42` md5 `52532381`), `verdict_h58.py` :

| course | étapes au mélange (verdict ; réglages ; seconds verdicts) | total | mesures de projet (payées, relues) | niveaux solo (payés, relus) |
|---|---|---|---|---|
| T, témoin `--sans-cache-rendus` | 210 ; 128 · 98 · 111 ; 59 · 117 · 113 | 836 s | — | — |
| A, cache vide qu'elle remplit | 141 ; 129 · 104 · 112 ; 60 · 114 · 115 | 775 s | 242, 34 | 220, 13 |
| B, rejouée sur le cache de A | 2 ; 1 · 1 · 1 ; 1 · 1 · 1 | **8 s** | 0, 276 | **0, 233** |

| # | attendu | verdict |
|---|---|---|
| 1 | l'identité contre T | **tenu** : `project.json` de A et B identiques à l'octet, rapport hors provenance sans différence, distance globale 0,1959822425659897 partout |
| 2 | le rejeu, rapporté aux 176 s de la course B de H48 (≤ 50 %) | **tenu** : 8 s, **4,5 %** |
| 3 | ce que B paie (0 et 0) | **tenu** : 0 niveau solo, 0 mesure de projet |
| 4 | le témoin n'écrit rien | **tenu** : 0 fichier au cache après T, aucune ligne de compte |
| 5 | la première passe, A rapportée à T (≤ 110 %) | **tenu** : **92,7 %** — elle relit déjà 13 niveaux solo qu'elle avait payés plus tôt dans la même course |

**CE QUE LE VERDICT DÉCIDE, comme écrit avant** (1 à 4 tenus) : le code entre dans
`reload-chaine` — la course 2 de « Reload » partira avec lui — et dans l'arbre principal
quand aucune course n'y tournera ; la course de référence y tourne, il attend sa fin
(`analyse/analyzer/` ne se touche pas sous une course qui l'a importé).

**Ce que la mesure ne prouve pas** : la première passe à 92,7 % tient à un morceau de
30 s où le réglage repasse par des états déjà vus ; sur « Reload », dont les niveaux
solo se prennent sur 312 s et par groupes de quatre voix, la ligne « niveaux solo du
calage » du journal de la course 2 dira combien une première passe en relit.

---

## 11. H49 — décrire le pad sur l'ORIGINAL : ses raies une à une, et la forme de leur mouvement (écrite AVANT la mesure, 30/09/2026, 20 h 00)

**Pourquoi.** H47 a montré qu'on ne peut pas demander « quelle machine sonne comme
le pad » sans savoir ce qu'est le pad : un tiers de sa puissance est hors de ses dix
raies étroites, et les deux métriques en main récompensent ce qui le remplit par
accident (§ 9.2). Le § 3 demandait la description « mesurée sur l'original » ; elle
n'a jamais été faite — le § 1 s'est arrêté à un spectre moyen.

**Ce qui est connu avant de mesurer, et d'où.** Sur le STEM : dix raies (§ 9.2),
68,3 % de la puissance à ± 2,7 Hz d'elles, le reste tonal ; la plus forte (fa♯4,
372,66 Hz) fait 2 Hz de large à −6 dB (§ 1). Sur l'ORIGINAL, rien n'a été regardé à
cette résolution.

**L'hypothèse.** Le mouvement est AU MORCEAU, pas à la séparation, et il a la forme
d'un **désaccord entre oscillateurs** : chaque note est jouée par plusieurs
oscillateurs presque sinusoïdaux, écartés de quelques cents, qui battent. Si c'est
vrai, chaque raie se résout en composantes DISCRÈTES, et leurs écarts sont les mêmes
EN CENTS d'une raie à l'autre (un désaccord est proportionnel à la fréquence) ; une
modulation à cadence fixe (trémolo, chorus à LFO, pompage sur le kick) donnerait les
mêmes écarts EN HERTZ ; une modulation irrégulière, une bosse continue.

**L'extrait** : 16 à 40 s, le pad seul avec la batterie (§ 1) — de l'ORIGINAL
(`reconstruction/sources/reload-peschi.wav`, en mono) et du stem « other ».

**L'INSTRUMENT** (`analyse/mesure_h49.py`, branche `reload-h47`), par raie `k` de
l'oracle de H47, à la fréquence `f_k` :
- **la forme** : dans le spectre de l'extrait entier (fenêtre de Hann, 0,042 Hz par
  case), la puissance du **cœur** (à ± 2,7 Hz de `f_k`), de la **jupe** (de 2,7 à
  6 Hz) et la densité du **fond** (médiane entre 6 et 6,9 Hz, des deux côtés : à
  mi-chemin des deux raies les plus proches, 13,8 Hz) ; la **part de jupe** est la
  jupe rapportée au cœur plus la jupe, le fond retiré ; le **rapport au fond** de la
  raie (cœur + jupe sur le fond ramené à 12 Hz) dit si elle se mesure ;
- **les composantes** : dans un Welch à segments de 8 s (0,125 Hz par case, cinq
  segments), les pics à ± 6 Hz de `f_k`, de proéminence ≥ 8 dB et à moins de 15 dB du
  plus fort ; leur écart au plus fort, en hertz et en cents ;
- **le mouvement** : le signal analytique de la bande `f_k` ± 6 Hz — profondeur de
  modulation d'amplitude (écart-type du niveau en dB, et l'étendue du 5ᵉ au
  95ᵉ centile), cadence dominante de cette modulation, modulation de fréquence
  (écart-type en cents, pondéré par l'amplitude) ;
- **les partiels** : tout pic de l'original entre 600 et 6 000 Hz à moins de 40 dB
  de la raie la plus forte, hors batterie (il doit tenir sa fréquence sur les trois
  tiers de l'extrait).

**Une raie ne se juge que si elle se VOIT** : rapport au fond ≥ 10 dB dans
l'original. Celles qui ne passent pas sont nommées, jamais comptées.

**ATTENDUS** :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | **l'instrument**, sur des signaux fabriqués (tests écrits avant) : un sinus seul ; trois sinus à −7, 0, +7 cents, à deux hauteurs ; un sinus à trémolo de 5 Hz, à deux hauteurs ; le tout sous un bruit à −30 dB | 1 composante ; 3 composantes, mêmes écarts en cents (± 1) aux deux hauteurs ; bandes latérales à ± 5 Hz (± 0,2) aux deux hauteurs ; le classement « cents / hertz » de l'attendu 4 rend la bonne forme | une seule de ces lectures fausse : rien ne se lit |
| 2 | **au morceau ou à la séparation ?** écart de la part de jupe entre le stem et l'original, raie par raie (raies vues) | médiane des \|écarts\| ≤ 5 points : la séparation garde la forme, le mouvement est au morceau | médiane > 15 points : le stem ne décrit pas le pad, tout ce qui a été mesuré sur lui (H43 à H47) est à relire |
| 3 | **discret ou continu ?** raies vues qui se résolvent en ≥ 2 composantes dans l'original | ≥ 7 sur 10 (ou ≥ 70 % des raies vues) | ≤ 3 : une bosse continue, l'hypothèse des oscillateurs tombe |
| 4 | **cents ou hertz ?** dispersion (écart-type rapporté à la moyenne) de l'écart de la composante secondaire la plus forte, d'une raie à l'autre, exprimé en cents et en hertz | dispersion en cents ≤ 20 % ET en hertz ≥ 2 fois celle en cents : un désaccord | l'inverse : une modulation à cadence fixe ; ni l'un ni l'autre : non conclu, et c'est dit |
| 5 | **sinusoïdal ?** le plus fort partiel de l'original entre 600 et 6 000 Hz qui tienne sa fréquence | ≤ −30 dB sous la raie la plus forte | > −20 dB : le pad n'est pas « presque sinusoïdal », le § 1 est à corriger |
| 6 | **le kick pompe-t-il le pad ?** cadence dominante de la modulation d'amplitude, raies vues de l'original | sur moins de 3 raies, elle tombe à ± 0,1 Hz de 2,30 Hz (le temps) ou 4,60 Hz | sur 7 et plus : un pompage, qui se reconstruit par un effet et non par un timbre |

Moins de trois composantes secondaires mesurables sur l'ensemble des raies :
l'attendu 4 est « non mesurable », pas un échec (un coefficient sur deux points ne
se lit pas).

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) :
- 3 et 4 tenus (désaccord) : le pad est N oscillateurs sinus désaccordés de `c`
  cents, et `N` et `c` sont MESURÉS. La question de H47 se repose alors juste : le
  parc a-t-il une machine qui joue cela ? — cherchée par sa déclaration de
  paramètres (unisson, désaccord, forme d'onde), puis jugée sur CES descripteurs.
  Sinon la machine est décrite et construite (`CDC-nouvelle-machine.md`).
- 4 en « cadence fixe » ou 6 en échec : le mouvement est un EFFET (trémolo, chorus,
  pompage) posé sur un timbre simple ; c'est la chaîne d'effets du projet qui est
  visée, pas le parc.
- 3 en échec (continu) : une modulation irrégulière ; sa largeur et sa cadence sont
  publiées, et l'hypothèse suivante cherche lequel des seize effets la produit.
- 2 en échec : avant tout le reste, les mesures prises sur le stem sont relues.
- 5 en échec : le § 1 est corrigé et la liste des partiels devient la description.

### 11.1 Verdict de H49 (30/09/2026, 20 h 04) : le mouvement est au morceau et il est DISCRET — mais l'attendu « cents ou hertz » ne conclut pas tel qu'il était écrit

`analyse/mesure_h49.py mesurer` (branche `reload-h47`, `cd575ba`, arbre propre),
l'original et le stem « other » de 16 à 40 s ;
`reconstruction/travail/reload-h49/mesure.json`.

**Les dix raies se voient toutes dans l'original** (rapport au fond de 11,1 à
23,9 dB). Raie par raie, dans l'ORIGINAL :

| raie (Hz) | niveau (dB) | part de jupe | composantes | modulation d'amplitude (σ, étendue) | cadence | modulation de fréquence (σ) |
|---|---|---|---|---|---|---|
| 166,00 | −17,6 | 2,5 % | 3 | 9,9 dB, 30,5 dB | 3,50 Hz | 11,4 cents |
| 186,33 | −15,4 | 4,1 % | 5 | 4,9 dB, 16,2 dB | 3,50 Hz | 12,2 cents |
| 234,79 | −15,3 | 2,3 % | 3 | 11,8 dB, 37,0 dB | 0,27 Hz | 8,2 cents |
| 248,67 | −10,4 | 1,7 % | 3 | 10,4 dB, 31,3 dB | 1,73 Hz | 6,8 cents |
| 279,12 | −13,7 | 7,0 % | 5 | 10,8 dB, 33,2 dB | 1,77 Hz | 7,2 cents |
| 333,71 | −3,6 | 31,1 % | 7 | 10,0 dB, 30,3 dB | 3,50 Hz | 7,9 cents |
| 372,67 | 0,0 | 16,0 % | 7 | 4,8 dB, 15,4 dB | 1,77 Hz | 7,3 cents |
| 469,58 | −3,6 | 28,7 % | 6 | 14,1 dB, 41,0 dB | 0,27 Hz | 7,2 cents |
| 497,33 | −2,2 | 36,1 % | 6 | 10,3 dB, 31,8 dB | 3,50 Hz | 7,7 cents |
| 554,79 | −1,8 | 45,6 % | 5 | 10,8 dB, 32,6 dB | 3,50 Hz | 5,5 cents |

| # | attendu | verdict |
|---|---|---|
| 1 | l'instrument sur signaux fabriqués | **tenu** (6 tests sur 6) — après DEUX défauts trouvés par ces tests et corrigés avant la mesure : une fréquence instantanée lue sur un signal décimé sans le ramener en bande de base, et un partiel « tenu » par un pic de bruit |
| 2 | au morceau ou à la séparation ? | **tenu** : médiane des écarts de part de jupe **0,3 point** (de −1,9 à +1,3) ; les composantes du stem sont celles de l'original à 0,01 Hz près |
| 3 | discret ou continu ? | **tenu** : **10 raies sur 10** se résolvent (3 à 7 composantes) |
| 4 | cents ou hertz ? | **non conclu**, tel qu'écrit : dispersion de 34 % en cents (moyenne 11,7), de 37 % en hertz (moyenne 2,04 Hz) |
| 5 | sinusoïdal ? | **entre les deux**, tel qu'écrit : le plus fort partiel « tenu » est à 5 966,7 Hz, −22,3 dB |
| 6 | le kick pompe-t-il le pad ? | **tenu** : 0 raie sur 10 à 2,30 ou 4,60 Hz (cadences : 3,50 Hz cinq fois, 1,73 à 1,77 trois fois, 0,27 deux fois) |

**CE QUE CELA ÉTABLIT.**
- **Le stem dit vrai sur le pad** (attendu 2) : ce qui a été mesuré sur lui de H43 à
  H47 n'est pas à relire pour cause de séparation. Le « tiers hors des raies » de
  H47 est AU MORCEAU.
- **Ce tiers est fait de raies DISCRÈTES** (attendu 3), pas d'un souffle : la part
  de jupe monte avec la hauteur, de 2 % à 166 Hz à 46 % à 555 Hz.
- **Chaque raie s'éteint presque** : 15 à 41 dB d'étendue de niveau. Un timbre à
  amplitude fixe — tout ce que H47 a fait jouer — ne peut pas lui ressembler.
- **Pas de pompage au kick** (attendu 6).

**CE QUE L'ATTENDU 4 N'A PAS SU LIRE — et c'est un défaut du critère, dit.** Il
comparait, d'une raie à l'autre, l'écart de LA composante secondaire la plus forte.
Or la plus forte change de famille avec la hauteur (± 1,75 Hz de 166 à 373 Hz,
± 3,5 Hz à 470 et 497 Hz, + 1,13 Hz à 555 Hz) : le critère compare des choses
différentes et rend « non conclu ». Le verdict reste tel quel.

**L'attendu 5 est confondu, et c'est mesuré** : les dix partiels « tenus » entre
2,3 et 6 kHz sont dans le stem de BATTERIE (dix sur dix, aux mêmes fréquences) — un
charleston métallique tient ses raies d'un bout à l'autre, et « tenir sa fréquence
sur les trois tiers » ne l'écarte pas. Dans le stem « other », un seul partiel passe
le seuil : **835,1 Hz à −34,4 dB**. Le pad reste sinusoïdal à −34 dB près ; le
critère du § 11 ne savait pas le dire sur l'original.

**Relevé APRÈS la mesure — une lecture, pas un verdict.** En rapportant chaque
composante non plus à la plus forte mais à la hauteur TEMPÉRÉE de sa note (la4 =
443,1372 Hz) :
- **la composante la plus proche du tempéré en est à ± 0,4 cent sur les dix raies**
  (± 0,09 Hz) — le diapason de H42, retrouvé note par note ;
- **les 40 composantes secondaires tombent toutes sur une famille à deux cadences**,
  `f1` = 1,75 Hz et `f2` = 2,37 Hz : 23 sur le peigne `k·f1` (58 %, contre 8 % au
  hasard), 39 sur {`k·f1`, `f2`, `f1`+`f2`, `f2`−`f1`, 2`f1`−`f2`, 2`f2`} à ± 0,08 Hz
  (98 %, contre 21 %), la quarantième à 2`f1`+`f2`. Les mêmes écarts EN HERTZ sur
  toutes les raies : une modulation périodique, pas un désaccord ;
- `f1` lu sur 23 composantes : **1,7512 ± 0,0022 Hz** — ce n'est pas un triolet de
  blanches à 138 BPM (1,725 Hz) : un oscillateur libre ;
- **la signature d'un RETARD modulé** : la bande `f1` domine sous 300 Hz et
  disparaît au-dessus de 450 Hz, où c'est 2·`f1` qui domine — ce que fait une
  modulation de phase dont l'indice croît avec la fréquence (la première bande
  latérale s'annule vers un indice de 3,8), c'est-à-dire un chorus : un retard qui
  oscille d'environ 1 ms à 1,75 Hz, mélangé au son direct.

**CE QUE LE VERDICT DÉCIDE.**
- Le § 11 rangeait ce cas sous « le mouvement est un EFFET posé sur un timbre
  simple ; c'est la chaîne d'effets du projet qui est visée, pas le parc ». L'attendu
  4 ne l'a pas établi ; la lecture ci-dessus le dit, mais elle a été faite APRÈS
  avoir vu les chiffres : elle devient une hypothèse, **H50 (§ 12)**, jugée sur des
  extraits que personne n'a regardés.
- **Aucune machine neuve n'est décrite** : dix sinus et un chorus ne demandent pas
  de machine, si la lecture tient.

---

## 12. H50 — le pad est un sinus par note sous un chorus à deux cadences : la lecture du § 11.1, jugée sur deux extraits jamais regardés (écrite AVANT la mesure, 30/09/2026, 20 h 13)

**L'hypothèse**, entière et chiffrée, telle que le § 11.1 la lit sur 16-40 s :
chaque note du pad est une raie à sa hauteur tempérée (la4 = 443,1372 Hz), entourée
de bandes latérales aux écarts `k·f1`, `f2` et leurs combinaisons, avec
**`f1` = 1,751 Hz** et **`f2` = 2,37 Hz**, les mêmes EN HERTZ à toutes les hauteurs ;
et la bande `f1` cède la place à 2·`f1` quand la hauteur monte (un retard modulé).

**Les extraits**, choisis sur la structure du § 1 et jamais analysés à cette
résolution : **130 à 154 s** (le long pont) et **272 à 296 s** (la sortie) — 24 s
chacun, comme le premier.

**L'instrument** (`analyse/mesure_h50.py`, branche `reload-h47`) : l'oracle de H47
sur le stem « other » de l'extrait (les notes peuvent avoir changé) ; les
composantes de H49 dans l'ORIGINAL ; pour chaque raie vue, la PORTEUSE est la
composante la plus proche de la hauteur tempérée de sa note, et chaque autre
composante est rapportée à elle. La famille est fixée ici, avant la mesure :
{`f1`, 2`f1`, 3`f1`, `f2`, 2`f2`, `f1`+`f2`, `f2`−`f1`, 2`f1`−`f2`, 2`f1`+`f2`}, à
± 0,08 Hz — neuf membres, soit **24 %** de la bande de ± 6 Hz : c'est le taux du
hasard, et il est publié à côté.

**ATTENDUS**, sur chaque extrait :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | **l'instrument** (tests écrits avant) : des sinus sous un retard modulé à 1,751 Hz (± 1,2 ms, moitié direct) à cinq hauteurs ; le même à 1,80 Hz ; les mêmes sous un trémolo de 5 Hz | le retard modulé : porteuses au tempéré, ≥ 90 % des composantes sur la famille, `f1` relu à ± 0,01 Hz, la signature « `f1` en bas, 2·`f1` en haut » lue ; à 1,80 Hz, `f1` tombe ; le trémolo : moins de 40 % sur la famille | une lecture fausse : rien ne se lit |
| 2 | **les porteuses** : écart de la composante la plus proche à la hauteur tempérée | ≤ 1 cent sur ≥ 80 % des raies vues | < 50 % |
| 3 | **la famille** : part des composantes secondaires qui tombent sur elle | ≥ 70 % (hasard 24 %) | < 40 % |
| 4 | **`f1`** relu sur les composantes du peigne `k·f1` (moyenne de l'écart divisé par `k`) | 1,751 ± 0,010 Hz | hors de ± 0,030 Hz, ou moins de cinq composantes pour le lire (non mesurable) |
| 5 | **la signature du retard** : sous 260 Hz, la plus forte bande `f1` dépasse la plus forte 2·`f1` d'au moins 3 dB (ou 2·`f1` est absente) ; au-dessus de 450 Hz, l'inverse | vrai sur ≥ 80 % des raies concernées | < 50 % |

H50 est **tenue** si 2, 3, 4 et 5 tiennent sur les DEUX extraits ; **réfutée** si 3
échoue sur l'un des deux ; entre les deux, chaque attendu est dit tel quel. Un
extrait où moins de quatre raies se voient ne juge rien : il est dit « muet ».

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) :
- tenue : le pad se reconstruit par **un timbre sinusoïdal et un chorus**, et la
  suite (H51) est de le FABRIQUER avec ce que le dépôt a — une machine qui sache
  jouer un sinus, l'effet chorus du rack s'il sait porter deux cadences et 1 ms de
  profondeur — puis de juger le rendu AVEC L'INSTRUMENT DE H49, raie par raie,
  contre l'original. Ce qui manquerait (une seconde cadence, une profondeur) est
  alors un manque de l'EFFET, chiffré, et non une machine à inventer.
- réfutée : la lecture du § 11.1 valait pour 24 secondes ; le mouvement change au
  fil du morceau, et c'est sa trajectoire qu'il faut décrire avant tout.

### 12.1 Verdict de H50 (30/09/2026, 20 h 14) : TENUE sur les deux extraits — le pad est un sinus par note sous une modulation de retard à deux cadences

`analyse/mesure_h50.py mesurer` (branche `reload-h47`, `472e58e`, arbre propre) ;
`reconstruction/travail/reload-h50/mesure.json`.

| attendu | 130-154 s | 272-296 s |
|---|---|---|
| raies vues | 10 sur 11 | 10 sur 11 |
| 2 — porteuses à ≤ 1 cent du tempéré | **tenu** : 9 sur 10 | **tenu** : 9 sur 10 |
| 3 — composantes sur la famille (hasard 24 %) | **tenu** : 36 sur 38, **95 %** | **tenu** : 36 sur 39, **92 %** |
| 4 — `f1` relu (attendu 1,751 ± 0,010) | **tenu** : 1,7495 Hz (± 0,0015, 22 composantes) | **tenu** : 1,7501 Hz (± 0,0021, 22 composantes) |
| 5 — la signature du retard | **tenu** : 6 raies sur 6 | **tenu** : 6 raies sur 6 |

L'attendu 1 (l'instrument) est tenu par 5 tests sur 5, dont deux témoins qui doivent
tomber et tombent : le même retard modulé à 1,80 Hz fait échouer `f1`, un trémolo de
5 Hz fait échouer la famille.

**Ce que les deux extraits ajoutent, dit :**
- **une onzième note**, sol♯4 (418,27 Hz au tempéré), que l'oracle lit sur le stem
  dans le pont et dans la sortie : sa raie est à **+7,6 cents** du tempéré et ses
  composantes sont hors famille (+1,42 et +2,69 Hz ; +0,70 Hz). C'est la porteuse
  manquée des deux attendus 2, et ce n'est pas une raie du pad : une autre source,
  vers 420,1 Hz, que le stem « other » porte à ces endroits — nommée, pas expliquée ;
- **si4 (497,4 Hz) ne se voit plus** dans l'original (fond à 7,4 et 7,3 dB, sous le
  seuil de 10) : autre chose occupe sa bande dans le pont et la sortie ;
- **les niveaux sont les mêmes d'un extrait à l'autre** à quelques dixièmes de
  décibel (mi3 : −1,1 / −3,7 dB à 130 s, −1,0 / −3,8 à 272 s, −1,2 / −3,8 à 16 s) :
  la modulation est stationnaire sur tout le morceau.

**CE QUE LE VERDICT DÉCIDE** (écrit au § 12) : le pad se reconstruit par **un timbre
sinusoïdal et une modulation de retard** ; aucune machine neuve. La suite est de le
FABRIQUER avec ce que le dépôt a, et de chiffrer ce qui manque à l'EFFET — H51.

---

## 13. H51 — un chorus à deux cadences REND-il les niveaux des raies ? Un modèle fidèle à l'effet du rack, réglé sur cinq raies, jugé sur cinq autres (écrite AVANT la mesure, 30/09/2026, 20 h 18)

**Ce que le dépôt a, lu dans le code.** `audio/include/vsm/audio/dsp/Chorus.h` : une
ligne à retard lue à deux positions par deux LFO sinusoïdaux de MÊME cadence, en
quadrature (gauche et droite) ; le retard va de la base à la base plus la profondeur.
L'insert du rack (`ChorusEffect.h`) en expose trois réglages — cadence de 0,05 à
8 Hz, profondeur de 0,5 à 8 ms, dosage de 0 à 1 — et fixe la base à 8 ms. **Une
seule cadence, une quadrature imposée.**

**Ce que H50 demande** : deux cadences (1,750 et 2,37 Hz) et leurs combinaisons
(`f1`+`f2`, 2`f1`−`f2`…), c'est-à-dire deux modulations qui se composent.

**L'hypothèse.** Un modèle fait des mêmes pièces que l'effet du rack — un retard
modulé par un sinus, mélangé au son direct — appliqué DEUX fois à un sinus par note,
rend les niveaux relatifs des composantes de chaque raie de l'original, à toutes les
hauteurs, avec les MÊMES réglages.

**Les trois topologies**, toutes publiées :
- **S** — deux étages en série (la sortie du premier entre dans le second) ;
- **M** — un seul retard, modulé par la SOMME de deux LFO ;
- **P** — deux retards en parallèle, chacun son LFO. P ne peut pas produire
  `f1`+`f2` : c'est le témoin de topologie.

**L'instrument** (`analyse/mesure_h51.py`, branche `reload-h47`). Le modèle est
calculé en numpy, exactement (la source est un sinus : `x(t − τ(t))` s'écrit sans
interpolation). Chaque raie synthétisée est lue par l'instrument de H49
(`composantes`) et rapportée à sa porteuse comme en H50. **La distance** d'une raie :
moyenne des écarts absolus de niveau (dB, relatifs à la plus forte composante de la
raie), sur la porteuse et les membres de la famille présents à moins de 15 dB dans
l'original OU dans le modèle — un membre absent vaut −15 dB. Les réglages (profondeur,
dosage et base de chaque étage ; `f1` = 1,750 et `f2` = 2,37 Hz fixés) sont cherchés
par évolution différentielle à graine fixe, bornés (profondeur 0,1 à 8 ms, base 1 à
20 ms, dosage 0 à 1).

**Réglage et validation séparés.** L'original de 16 à 40 s (§ 11.1). Réglé sur
CINQ raies — mi3, la♯3, do♯4, fa♯4, si4 — et jugé sur les CINQ autres — fa♯3, si3,
mi4, la♯4, do♯5 —, qu'il n'a jamais vues.

**ATTENDUS** :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | **le modèle décrit l'effet du rack** : trois notes (mi3, fa♯4, do♯5) rendues par `vsm-render` (`build-h42`) à travers l'insert chorus (1,751 Hz, 2,4 ms, dosage 0,5), somme mono, contre le modèle à deux lectures en quadrature et base de 8 ms ; la machine est la première de la liste {orgue à tuyaux, additive, roue phonique, générique} dont la raie SANS effet n'a qu'une composante | chaque composante à ≤ 1 dB du modèle, sur les trois raies | > 3 dB quelque part : le modèle ne décrit pas l'effet, rien d'autre ne se lit |
| 2 | **le réglage** : distance moyenne sur les cinq raies de réglage, meilleure topologie | ≤ 2 dB | > 4 dB |
| 3 | **la validation** : distance moyenne sur les cinq raies jamais vues, mêmes réglages | ≤ 3 dB | > 6 dB |
| 4 | **le témoin** : un sinus nu (aucun effet), et le meilleur chorus à UNE cadence, sur les raies de validation | tous deux à plus de 2 dB au-dessus de la meilleure topologie | sinon deux cadences ne sont pas nécessaires, et c'est dit |
| 5 | **la topologie** : P (sans combinaisons) contre la meilleure de S et M, en validation | P pire d'au moins 1 dB | sinon la topologie n'est pas tranchée |
| 6 | **ce qui manque au rack**, chiffré : chaque réglage trouvé hors de ce que l'insert expose | la liste, avec ses valeurs | — (un relevé, pas un seuil) |

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) :
- 1, 2 et 3 tenus : l'effet du rack reçoit EXACTEMENT ce que l'attendu 6 liste, dans
  une phase à lui (`ROADMAP-daw.md`) — défauts inchangés au bit près, mesurée par un
  EXPORT — puis le pad est rendu par le vrai moteur et jugé par l'instrument de H49
  contre l'original, raie par raie.
- 1 en échec : le modèle est corrigé contre l'effet avant toute autre lecture.
- 3 en échec avec 2 tenu : sur-ajustement ; le modèle est trop libre ou la
  modulation n'est pas un retard, et le résidu par raie est publié.

---

## 14. H52 — le verdict du mélange jugeait un morceau SANS sa voix (constaté, corrigé dans une branche, et ce qui reste à mesurer est écrit avant ; 30/09/2026, 20 h 25)

**Ce n'est pas une hypothèse écrite avant sa mesure, et ce paragraphe ne la déguise
pas en cela** : c'est un défaut de la chaîne trouvé en lisant `_copy_samples` pour
H48, vérifié dans les rapports existants AVANT d'y toucher, puis corrigé. Ce qui
reste à mesurer — son effet sur les DÉCISIONS — est, lui, écrit ci-dessous avant.

**Le défaut.** Le verdict du mélange, le réglage au mélange et la boucle résiduelle
rendent le projet dans un dossier à part, où `_copy_samples` recopie les fichiers
que les pistes désignent. Elle ne recopiait que `track.samples` — les échantillons
des samplers. Depuis que la voix est une piste AUDIO (D2 de `ROADMAP-daw.md`), elle
désigne son fichier par `audio_path` et `samples` est vide : le moteur ne trouve pas
le fichier, n'en dit rien que sur sa sortie d'erreur (avalée), et la piste sort
muette. **Le verdict choisissait donc les machines contre un mélange sans la
voix** — le défaut même que la docstring de `_copy_samples` dit avoir fermé pour le
sampler (« le verdict se prononçait sur un mélange sans la voix »), revenu par une
autre porte.

**La preuve était publiée, et personne ne la lisait.** Le témoin de coupure (H27)
écrit au rapport ce que vaut le morceau SANS chaque piste. Relevé sur les 311
rapports du dossier de travail : **12 pistes « Voix » au verdict, 12 sur 12 avec
« sans la piste » ÉGAL à « avec », au seizième chiffre** (`s2-banc` g2, g4, g6 ;
`d282-temoin`, `d282-coupure`, `r1f-13sep` g3, g5, g6 — par exemple 0,3137599871497436
des deux côtés). Une valeur qui revient à son point de départ : la piste n'avait
jamais sonné.

**Par le vrai moteur** (`build-h42`), une machine et une piste audio, le mélange
visé étant leur rendu COMPLET — le même script lancé dans les deux arbres :

| arbre | la voix « avec » | la voix « sans la piste » | le fichier dans le dossier rendu |
|---|---|---|---|
| `reload-h48` (sans la correction) | 0,158163 | 0,158163 | absent |
| `reload-h52` (avec) | **0,0** | 0,158163 | présent |

Sans la correction, le projet est à 0,158 de SON PROPRE rendu : la voix manque. Avec,
il est à 0.

**La correction** (branche `reload-h52`, `58ae509`) : `_copy_samples` recopie aussi
`audio_path`. Deux tests, le premier vu ROUGE la correction retirée.

**ELLE N'ENTRE NI DANS `reload-chaine` NI DANS L'ARBRE PRINCIPAL AUJOURD'HUI.** La
course 2 de « Reload » doit rester à une variable de sa référence, et `g7`-`g10` de
S2 doivent courir la chaîne de `g1`-`g6` (§ 8.1). Sur « Reload », la voix pèse 0,5 %
du morceau (§ 1) : l'effet y est attendu nul, et ce sera mesuré, pas supposé.

**CE QUI RESTE À MESURER, écrit avant** — l'effet sur les décisions, sur un morceau
CHANTÉ du banc (`s2`, `g2` : ses stems séparés existent, `s2-banc`), témoin = la
chaîne de `reload-h48`, essai = celle de `reload-h52`, une seule variable :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | la piste « Voix » au rapport de l'essai | « sans la piste » > « avec » | égales : la correction ne porte pas |
| 2 | le verdict du mélange : pistes dont la machine retenue change entre témoin et essai | publié, piste par piste | — (un relevé) |
| 3 | la distance globale du morceau (`rapport.json`, mêmes métrique, budget et stems) | essai ≤ témoin + 1 % | essai > témoin + 3 % : juger avec la voix coûte, et il faut comprendre pourquoi avant d'entrer |
| 4 | les morceaux SANS voix (`g1` de `s1-sec`) : projet et rapport | identiques à l'octet, provenance à part | une différence : la correction touche ce qu'elle ne devait pas |

Elle entrera dans l'arbre principal après S2, dans un commit à elle, avec ces quatre
chiffres.

### 13.1 Verdict de H51 (30/09/2026, mesuré de 20 h 20 à 20 h 44) : PARTIELLE — un retard modulé explique l'essentiel, deux cadences n'ajoutent que 0,7 dB, les raies du haut résistent ; et l'attendu 1 a trouvé un défaut du MOTEUR

`analyse/mesure_h51.py mesurer` (branche `reload-h47`, `108f662`, arbre propre),
l'original de 16 à 40 s, `vsm-render` de `build-h42` ; 1 440 s ;
`reconstruction/travail/reload-h51/mesure.json`. Le balayage ENTIER :

| topologie | réglage (5 raies) | validation (5 autres) | réglages trouvés |
|---|---|---|---|
| N — sinus nu | 6,75 dB | 7,39 dB | — |
| U1 — une cadence, `f1` | 1,98 dB | 3,56 dB | profondeur 1,74 ms, dosage 0,45, base 1,83 ms |
| U2 — une cadence, `f2` | 4,50 dB | 5,79 dB | profondeur 1,44 ms, dosage 0,35, base 3,60 ms |
| **S — deux étages en série** | **1,49 dB** | **2,85 dB** | `f1` : 1,68 ms, 0,47, base 1,81 ms ; `f2` : 1,10 ms, 0,41, base 6,81 ms |
| M — un retard, deux LFO | 1,58 dB | 4,16 dB | 1,76 et 0,67 ms, dosage 0,48, base 14,41 ms |
| P — deux retards en parallèle | 1,64 dB | 4,12 dB | 2,11 ms, 0,65, 9,25 ms ; 0,41 ms, 0,81, 19,67 ms |

| # | attendu | verdict |
|---|---|---|
| 1 | le modèle décrit l'effet du rack (par le vrai moteur) | **échec — et ce n'est pas le modèle** : le rendu à travers l'insert chorus contient UN échantillon à **3,9e28** (voir ci-dessous) ; les trois tables du moteur sont vides |
| 2 | le réglage ≤ 2 dB | **tenu** : S, 1,49 dB |
| 3 | la validation ≤ 3 dB | **tenu** : S, 2,85 dB — par raie 2,42 · 0,96 · 3,23 · 3,07 · **4,59** (do♯5) |
| 4 | le témoin : une cadence à plus de 2 dB au-dessus | **échec** : U1 à 3,56 dB, S à 2,85 — **marge de 0,70 dB** ; le sinus nu, lui, est à 7,39 |
| 5 | la topologie : P pire d'au moins 1 dB | **tenu** : P 4,12 dB, S 2,85 (+1,27) |
| 6 | ce qui manque au rack | bases de 1,81 et 6,81 ms (le rack : 8 ms, fixe) ; deux cadences composées (une seule) ; une lecture unique en mono (deux lectures en quadrature, dont la somme mono éteint 2·`f1` — vérifié par un test du modèle) |

**CE QUE CELA ÉTABLIT, ET PAS PLUS.**
- **Un retard modulé à `f1` fait l'essentiel du chemin** : 7,39 → 3,56 dB avec un
  seul étage. Et ses réglages sont les MÊMES qu'on le cherche seul (1,74 ms, 0,45,
  1,83 ms) ou en premier étage de S (1,68 ms, 0,47, 1,81 ms) : ce n'est pas un
  accident de l'optimiseur.
- **C'est un retard COURT** — de 1,8 à 3,5 ms —, la plage d'un FLANGER sans
  réinjection plutôt que celle d'un chorus (le rack : base de 8 ms pour le chorus,
  de 1 ms pour le flanger). Relevé après la mesure, à reprendre.
- **La seconde cadence n'est PAS établie comme nécessaire par cette distance**
  (attendu 4 en échec, 0,70 dB). H50 l'a vue — ses composantes existent, sur la
  famille à 95 % —, mais elles sont à −6 / −12 dB, et une moyenne d'écarts de niveau
  les pèse peu. Les deux mesures ne se contredisent pas ; elles ne pèsent pas la
  même chose, et c'est dit.
- **Le modèle sous-module les raies du haut** : à do♯5, l'original a ses bandes
  latérales à −0,6 / −2,2 dB de la plus forte et sa porteuse à −0,9 ; le modèle les
  met à −9. Aucun réglage commun ne rend à la fois le bas (mi3 : 0,72 dB) et le haut
  (do♯5 : 4,59 dB). « Un son direct et UNE lecture retardée » n'est donc pas toute
  la structure.

**H51 est PARTIELLE** : 2, 3 et 5 tenus, 4 en échec, 1 en échec pour une raison
étrangère au modèle. La règle écrite au § 13 — « 1 en échec : avant toute autre
lecture » — s'applique à la lettre, et ce qu'elle trouve est plus grave que prévu.

**L'ATTENDU 1 A TROUVÉ UN DÉFAUT DU MOTEUR : le chorus lit hors de son tampon.** Trois
notes tenues 26 s par l'orgue à tuyaux à travers l'insert chorus (1,751 Hz, 2,4 ms) :
à **16,6906 s**, l'échantillon 736 055 du canal GAUCHE vaut **3,9478528e28**, et les
75 suivants décroissent d'un facteur 0,425 — le coefficient du passe-bas de l'effet.
Le canal droit est sain (crête 0,426) ; hors de cette salve, le niveau efficace est
0,1228. La cause, retrouvée par le calcul : à cet échantillon l'écriture est à la
case 458 et le retard vaut 457,99998 échantillons ; la position de lecture,
`écriture − retard`, sort négative d'une fraction infime, et `position + taille`
s'ARRONDIT à `taille` (2 209) en simple précision — la lecture se fait une case
après la fin du tampon, et rend ce que le tas contient là. Le même calcul est dans
le flanger, où la valeur lue repart dans la ligne par la réinjection ; le chorus est
aussi celui du Juno-106 et du Jupiter-8. **C'est la phase D526 de `ROADMAP-daw.md`**,
ouverte et corrigée le jour même (deux tests vus rouges, 1 314 tests audio verts,
l'export identique au bit hors de 98 échantillons).

**L'attendu 1, REJOUÉ tel qu'écrit avec le moteur corrigé (`build-h51`, 21 h 06) :
TENU.** Le chorus du rack est à **0,21 dB au pire** de son modèle (mi3 0,06 dB,
fa♯4 0,04, do♯5 0,21) : le modèle décrit bien l'effet — et l'effet, en somme mono,
éteint bien 2·`f1` (aucune composante à ± 3,5 Hz dans le rendu du moteur). Le rack
tel qu'il est ne peut donc pas jouer ce pad, dont les raies du haut sont dominées
par 2·`f1`.

**CE QUE LE VERDICT DÉCIDE.**
- **D526 d'abord** (le moteur), puis l'attendu 1 rejoué — fait, ci-dessus.
- **Le rack n'est PAS étendu sur la foi de ce modèle** : il laisse 4,6 dB à do♯5, et
  étendre un effet pour le rapprocher d'un modèle qui ne rend pas l'original serait
  régler le septième banc (règle du dépôt). L'attendu 6 reste un relevé.
- **La suite (H53) interroge la STRUCTURE au lieu de régler un modèle** : si le pad
  est « un son direct plus une lecture retardée », l'enveloppe complexe de chaque
  raie décrit un CERCLE dans le plan, et le retard `τ(t)` s'y LIT — sa forme, sa
  profondeur, ses deux cadences — sans optimiseur. Si elle n'en décrit pas un, il y
  a plusieurs lectures, et leur nombre se compte.

---

## 15. H53 — l'enveloppe complexe de chaque raie décrit-elle un CERCLE ? La structure du mouvement, lue sans optimiseur (écrite AVANT la mesure, 30/09/2026, 20 h 56)

**Pourquoi.** H51 a RÉGLÉ un modèle et s'est heurtée aux raies du haut : on ne sait
pas si c'est le modèle qui est faux ou l'optimiseur qui a transigé. Une structure se
lit plus directement qu'elle ne se règle.

**Ce que la structure « un son direct plus UNE lecture retardée » impose.** Une note
de pulsation `ω` y devient `A·e^{jωt}·E(t)`, avec `E(t) = (1 − m) + m·e^{−jω·τ(t)}` :
**`E(t)` parcourt un CERCLE** du plan complexe — centre `1 − m`, rayon `m` — quelle
que soit la forme du retard `τ(t)`. Et l'angle autour du centre EST le retard :
`τ(t) = −angle / ω`. Deux lectures (en série ou en parallèle) ne donnent pas un
cercle. La structure se lit donc sur la FORME de la trajectoire, et si c'est un
cercle, le retard s'y lit en millisecondes, raie par raie, sans rien régler.

**L'instrument** (`analyse/mesure_h53.py`, branche `reload-h47`), sur l'original de
16 à 40 s, par raie de l'oracle :
- la bande analytique de H49, mais de demi-largeur ADAPTÉE à la raie — la moitié de
  l'écart à la raie voisine la plus proche, bornée à 15 Hz (6,9 Hz pour la♯3 et
  si3, 10 pour mi3 et fa♯3, 13,9 pour la♯4 et si4, 15 ailleurs) : une modulation
  d'indice 3 à 4 a des bandes jusqu'à 5 fois `f1`, que ± 6 Hz coupaient ;
- ramenée en bande de base par la fréquence de la PORTEUSE (la composante la plus
  proche du tempéré, H50) : c'est `E(t)`, à une constante complexe près ;
- **le cercle** ajusté aux points de `E(t)` par moindres carrés (algébrique) ; la
  **circularité** est l'écart-type des distances au centre, rapporté au rayon ;
- si c'est un cercle : le **dosage** `m` (rayon sur rayon plus distance du centre à
  l'origine), et le **retard** `τ(t)` déroulé, en millisecondes, dont on publie
  l'amplitude aux cadences `f1` et `f2` et à leurs doubles.

**Ce que la bande fait au cercle, dit avant.** Le bruit de la batterie dans la bande
épaissit le trait ; le rapport au fond de H49 est publié par raie, et une raie à
moins de 10 dB ne se juge pas.

**ATTENDUS** :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | **l'instrument** (tests écrits avant), à cinq hauteurs, bruit à −30 dB : un direct plus une lecture (dosage 0,47, retard de 1,8 ms ± 0,85 à `f1` ± 0,3 à `f2`) ; deux étages en série ; un trémolo | une lecture : circularité ≤ 5 %, dosage relu à ± 0,05, amplitude du retard à `f1` relue à ± 10 % ; la série : circularité > 15 % sur les raies du haut ; le trémolo : pas un cercle (circularité > 15 %, ou arc parcouru de moins de 60° — une droite s'ajuste par un cercle immense dont elle est un arc infime) | une lecture fausse : rien ne se lit |
| 2 | **un cercle ?** circularité de chaque raie vue de l'original | ≤ 10 % ET un arc d'au moins 60° sur au moins 8 raies : la structure est « un direct plus une lecture » | > 25 %, ou un arc de moins de 60°, sur 5 raies ou plus : ce n'est pas elle |
| 3 | **le même retard pour toutes les notes ?** amplitude de `τ(t)` à `f1`, en ms, d'une raie à l'autre (raies à cercle) | dispersion ≤ 15 % : un seul retard, donc un effet sur le BUS du pad | > 40 % : la modulation est par note |
| 4 | **le même dosage ?** `m` d'une raie à l'autre | dispersion ≤ 15 % | > 40 % |
| 5 | **la forme du LFO** : dans `τ(t)`, l'amplitude à 2·`f1` rapportée à celle à `f1` | ≤ 10 % : un sinus | > 30 % : une autre forme (triangle, ou deux lectures) |

Entre « réussite » et « échec », chaque attendu est dit « entre les deux ». Si
l'attendu 2 échoue, 3 à 5 ne se lisent pas et sont dits « sans objet ».

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) :
- 2, 3 et 4 tenus : l'effet est UN retard modulé sur le bus, de forme, profondeur,
  base et dosage MESURÉS ; c'est lui qu'on donne au rack (une phase de
  `ROADMAP-daw.md`), puis le pad est rendu par le moteur et jugé raie par raie.
- 2 en échec : plusieurs lectures. Les trajectoires sont publiées, et l'hypothèse
  suivante en compte les lectures (la réponse d'un effet à retards, prise à dix
  fréquences au même instant, est une somme d'exponentielles dont le nombre se lit).
- 2 tenu, 3 ou 4 en échec : un cercle par note, mais pas le même — une modulation
  PAR NOTE (un vibrato de la machine plus un mélange), et c'est alors la machine
  qui est visée, pas l'effet.

### 15.1 Ce que les tests de l'instrument ont changé — AVANT toute mesure de l'original (01/10/2026, 22 h 52)

L'attendu 1 est la suite de tests (`analyse/tests/test_h53_cercle.py`, branche
`reload-h47`, `8ea9970`) : six tests, cinq hauteurs, bruit à −30 dB. **Ils ont trouvé
deux défauts de l'instrument**, corrigés avant qu'il ait vu l'original — vérifié :
aucun dossier `reload-h53`, aucun script ne l'appelle.

1. **L'amplitude du retard était lue à une case de FFT.** Sur 22 s, 1,75 Hz tombe à
   mi-case (38,5 cases), où la fenêtre de Hann perd 1,42 dB : **0,7224 ms relus pour
   0,85** — sur toutes les raies, et un sinus pur placé sur une case était relu juste.
   La transformée est désormais évaluée sur une grille de 1 mHz : 0,8498 à 0,8508 ms.
2. **Une corde passait pour un arc.** Un trémolo à la cadence `f1` — celle qui compte
   ici ; le premier test le prenait à 5 Hz, où rien ne se voyait — a fait juger la♯4
   « CERCLE » : circularité 9,9 %, arc 83°. Un segment parcouru en sinus passe ses
   instants à ses DEUX BOUTS, qui tombent sur le cercle dont il est la corde ;
   l'ajustement, trompé, ne peut pas trancher. Critère ajouté, qui ne passe pas par
   lui : l'**aplatissement** du nuage de points (petit axe sur grand axe). Un arc de
   demi-angle α parcouru en sinus en a ≈ α/4, soit **0,13 pour les 60° déjà exigés** ;
   le seuil est **0,10**, tiré de ce calcul et non des tests. Vu rouge le seuil remis
   à 0.

Relevé sur les quatre structures, par raie (mi3, si3, mi4, la♯4, do♯5) — circularité /
arc / aplatissement, `*` = jugée cercle :

| structure | mi3 | si3 | mi4 | la♯4 | do♯5 |
|---|---|---|---|---|---|
| une lecture | 0,4 % / 138° / 0,31 * | 0,7 % / 207° / 0,48 * | 0,9 % / 277° / 0,66 * | 0,4 % / 389° / 0,94 * | 0,5 % / 461° / 0,99 * |
| deux étages en série | 13,7 % / 241° / 0,64 | 25,3 % / 1 259° / 0,86 | 24,4 % / 1 584° / 0,82 | 36,6 % / 6 279° / 0,87 | 36,0 % / 6 444° / 0,99 |
| trémolo à 5 Hz | 43,3 % / 179° / 0,009 | 40,0 % / 166° / 0,013 | 40,5 % / 168° / 0,017 | 24,9 % / 128° / 0,006 | 29,1 % / 138° / 0,009 |
| trémolo à `f1` | 23,7 % / 125° / 0,009 | 40,5 % / 168° / 0,013 | 39,6 % / 165° / 0,017 | 9,9 % / 83° / 0,006 | 31,2 % / 143° / 0,009 |

**L'attendu 1 est TENU**, après ces deux corrections : une lecture, circularité 0,4 à
0,9 % (≤ 5 %), dosage 0,470 à 0,4702 (± 0,05), retard à `f1` 0,8498 à 0,8508 ms
(± 10 %), et la même chose avec la porteuse décalée de 0,05 Hz (vu rouge l'affinage
retiré : mi3 à 8,7 %) ; la série à 36,6 et 36,0 % sur les deux raies du haut (> 15 %) ;
aucun des deux trémolos n'est un cercle. **Ce que les tests ne gardent pas, et c'est
dit** : l'arc de 60° ne décide d'aucun des six cas (le trémolo tombe par la
circularité) ; il décide sur une lecture de ± 0,05 ms seulement — un vrai cercle à
arcs de 29 à 36°, circularité 0,5 à 1,2 % —, cas relevé, non gardé par un test.

**L'ATTENDU 2 EST AMENDÉ EN CONSÉQUENCE, avant la mesure** : réussite si circularité
≤ 10 % ET arc d'au moins 60° **ET aplatissement d'au moins 0,10**, sur au moins 8
raies ; échec si circularité > 25 %, ou arc de moins de 60°, **ou aplatissement sous
0,10**, sur 5 raies ou plus. Le reste du § 15 est inchangé.

### 15.2 Verdict de H53 tel qu'écrit (01/10/2026, mesuré à 22 h 54) : attendu 2 en ÉCHEC — et un contrôle de l'instrument, écrit AVANT d'être lancé (22 h 55)

`analyse/mesure_h53.py mesurer` (branche `reload-h47`, `8ea9970`), l'original de 16 à
40 s, les dix raies de l'oracle ; 6 s ; `reconstruction/travail/reload-h53/mesure.json`.

| raie | Hz | rapport au fond | circularité | arc | aplatissement |
|---|---|---|---|---|---|
| mi3 | 165,99 | 20,6 dB | 39,9 % | 4 326° | 0,544 |
| fa♯3 | 186,32 | 20,0 dB | 44,5 % | 2 376° | 0,421 |
| la♯3 | 234,74 | 18,0 dB | 49,9 % | 743° | 0,662 |
| si3 | 248,70 | 24,3 dB | 46,5 % | 1 605° | 0,723 |
| do♯4 | 279,16 | 18,6 dB | 31,7 % | 1 357° | 0,644 |
| mi4 | 331,98 | 18,3 dB | 50,0 % | 1 607° | 0,617 |
| fa♯4 | 372,63 | 16,5 dB | 42,7 % | 1 252° | 0,781 |
| la♯4 | 469,49 | 16,4 dB | 40,0 % | 1 586° | 0,878 |
| si4 | 497,40 | 10,4 dB | 50,3 % | 742° | 0,947 |
| do♯5 | 558,32 | 10,2 dB | 44,2 % | 1 949° | 0,970 |

| # | attendu | verdict |
|---|---|---|
| 2 | un cercle ? | **ÉCHEC** : 0 raie en cercle, 10 franchement hors (circularité de 32 à 50 %), 10 vues sur 10 |
| 3, 4, 5 | le même retard, le même dosage, la forme du LFO | **sans objet**, comme écrit |

**Ce que les chiffres montrent, et pas plus.** Aucune trajectoire n'est un trait : ce
sont des NUAGES (aplatissement de 0,42 à 0,97, circularité voisine de celle d'une
tache gaussienne, ≈ 52 %), et des arcs de deux à douze tours — le retard « lu » y
ferait de 4 à 72 ms crête à crête, ce qui n'a pas de sens pour un chorus : l'angle,
déroulé autour d'un centre que la trajectoire frôle sans cesse, accumule des tours.

**POURQUOI LA RÈGLE DU § 15 NE S'APPLIQUE PAS ENCORE.** Elle dit « 2 en échec :
plusieurs lectures ». Mais l'instrument n'a été éprouvé qu'à un bruit de −30 dB, et
les raies de l'original sont à **10 à 24 dB** de leur fond — dans une bande de ± 7 à
± 15 Hz, plus large que celle de H49 (± 6 Hz). Un calcul d'ordre de grandeur : à
20 dB sur ± 6 Hz, le bruit vaut ≈ 13 à 16 % de l'amplitude efficace de la raie dans
la bande de H53 ; pour un dosage de 0,5, cette amplitude vaut 0,71 fois celle de la
note et le rayon 0,5 fois — le bruit fait ≈ 20 % du RAYON, et sa part radiale une
circularité de 13 à 16 % à lui seul. À 10 dB, trois fois plus : de 40 à 50 %. Le § 15 le prévoyait en mots (« le bruit de la batterie
épaissit le trait ») sans l'avoir chiffré, et le seuil de 10 dB n'a jamais été
éprouvé. Règle du dépôt : quand un banc accuse, vérifier le banc avant la cible.

**LE CONTRÔLE, écrit avant d'être lancé.** La structure « un direct plus une lecture »
(dosage 0,5 ; retard 1,8 ms ± 0,85 à `f1` ± 0,3 à `f2`), aux DIX hauteurs de l'oracle
et à 443,14 Hz, chaque raie noyée dans un bruit blanc réglé pour que son rapport au
fond — mesuré par la MÊME fonction que sur l'original (`mesure_h49.forme_de_raie`) —
soit celui de la même raie dans l'original, à ± 1 dB ; trois tirages de bruit.
- **Si l'instrument y relit au moins 8 cercles sur 10** (dans au moins deux tirages
  sur trois) : il voit un cercle à ce bruit, et l'échec de l'attendu 2 dit la
  STRUCTURE — ce n'est pas une lecture. La suite est celle que le § 15 écrivait.
- **S'il en relit 5 ou moins** : il est aveugle à ce bruit ; **H53 est NON
  CONCLUANTE**, et rien ne s'écrit sur la structure du pad. La suite est alors un
  instrument qui supporte le bruit de l'original (moyenner par note sur des cycles
  du LFO, ou lire la phase relative de deux raies voisines) — écrit avant d'être
  mesuré, comme le reste.
- Entre les deux : non concluante aussi, et dit.

Le contrôle est un script d'analyse (`analyse/controle_h53.py`, branche
`reload-h47`), sans rendu : il tourne à côté de la course de référence.

### 15.3 Verdict du contrôle (01/10/2026, 22 h 56) : l'instrument est AVEUGLE au bruit de l'original — H53 NON CONCLUANTE

`analyse/controle_h53.py` (branche `reload-h47`), la règle du § 15.2 telle qu'écrite :
une lecture parfaite (dosage 0,5 ; 1,8 ms ± 0,85 à `f1` ± 0,3 à `f2`) aux dix hauteurs,
chaque raie à son rapport au fond de l'original (atteint à ± 1 dB, 30 sur 30), bruit
blanc, trois tirages : **2, 2 et 2 cercles sur 10** — fa♯3 ou la♯3, et si3, les raies
à 18-24 dB. Aveugle : **H53 est NON CONCLUANTE**, et rien ne s'écrit sur la structure
du pad. `reconstruction/travail/reload-h53/controle.json`.

**Ce que le contrôle montre en passant, et qui sert la suite.** Là où le jugement
« cercle » échoue, les RELECTURES restent justes : retard à `f1` de 0,74 à 1,18 ms pour
0,85, dosage de 0,45 à 0,55. C'est le seuil de circularité qui est aveugle au bruit,
pas la lecture du retard.

**Relevé après coup, qui n'est PAS une conclusion.** Sur les dix raies, l'original est
plus épais que le pire des trois tirages : si3 46,5 % pour 5,7 % au plus, la♯3 49,9
pour 10,0, fa♯3 44,5 pour 12,6, mi4 50,0 pour 17,9 … do♯5 44,2 pour 33,3 (× 1,3 à × 8).
Deux raisons de ne pas l'écrire comme établi : c'est une lecture d'après coup, et le
bruit du contrôle est BLANC et stationnaire quand celui de l'original est une
batterie. Elle fait la question de H54, jugée autrement.

---

## 16. H54 — sur le stem « other », où la batterie est retirée, les raies du pad décrivent-elles un CERCLE ? (écrite AVANT la mesure, 01/10/2026, 22 h 58)

**Pourquoi.** H53 n'a rien pu dire : à 10-24 dB de leur fond, les raies de l'original
sont trop bruitées pour que l'instrument y voie même une lecture parfaite. Le bruit,
c'est surtout la batterie — et la séparation l'a retirée : le stem « other » de la
course de référence porte le pad avec un fond bien plus bas. H49 a mesuré que le
mouvement des raies y est celui du morceau (stem et original à 0,3 point).

**Une seule variable change par rapport à H53 : la SOURCE** — le stem au lieu de
l'original. L'instrument est celui du § 15.1 (`8ea9970`), inchangé ; ses critères
sont ceux de l'attendu 2 amendé (circularité ≤ 10 %, arc ≥ 60°, aplatissement ≥ 0,10).

**Les extraits** : 16-40 s (l'oracle de H47, dix notes) et les deux extraits de H50,
130-154 s et 272-296 s (onze notes, sol♯4 en plus) — aucun des trois n'a été passé à
l'instrument du cercle sur le stem. Sur onze raies, les seuils restent ceux du § 15
(au moins 8 cercles ; 5 raies franchement hors).

**Le contrôle fait partie de la mesure** : pour chaque extrait, la même lecture parfaite
qu'au § 15.2, chaque raie réglée au rapport au fond qu'elle a DANS LE STEM, trois
tirages ; c'est l'attendu 1, et un extrait dont l'instrument est aveugle ne juge rien.

**Ce que la séparation peut faire, dit avant.** Demucs travaille par masques et par
forme d'onde : il pourrait lisser ou épaissir une trajectoire. H49 dit que le
mouvement survit (0,3 point) ; il ne dit pas que la FORME de la trajectoire survit.
Un cercle sur le stem ne serait donc pas une preuve que l'original en décrit un — mais
une structure à plusieurs lectures ne naît pas d'un masque.

**ATTENDUS**, par extrait :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | **l'instrument voit au bruit du stem** : la lecture parfaite, chaque raie à son rapport au fond dans le stem (± 1 dB), trois tirages | au moins 8 cercles dans au moins deux tirages | 5 ou moins dans au moins deux tirages : l'extrait ne juge rien |
| 2 | **un cercle ?** sur le stem | au moins 8 raies en cercle : « un direct plus une lecture » | 5 raies ou plus franchement hors (circularité > 25 %, arc < 60° ou aplatissement < 0,10) : ce n'est pas elle |
| 3 | **les trois extraits disent-ils la même chose ?** | le même verdict de l'attendu 2 sur les extraits où l'attendu 1 tient | des verdicts opposés |
| 4 | si 2 tient : **le même retard, le même dosage, un sinus** — les attendus 3, 4 et 5 du § 15, tels qu'écrits | | |

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) :
- **2 tenu sur au moins deux extraits où 1 tient**, et 3 tenu : le pad est un direct
  plus UNE lecture ; les attendus 3 à 5 donnent sa forme, sa profondeur et son dosage,
  et c'est cet effet — mesuré, pas réglé — que le rack reçoit (une phase de
  `ROADMAP-daw.md`) si 3 et 4 tiennent.
- **2 en échec sur au moins deux extraits où 1 tient** : ce n'est pas une lecture,
  même la batterie retirée ; la suite COMPTE les lectures (la réponse d'un effet à
  retards, prise à dix fréquences au même instant, est une somme d'exponentielles).
- **1 en échec sur deux extraits ou plus** : même le stem est trop bruité pour cet
  instrument ; H54 non concluante, et la suite est un instrument qui moyenne par note
  sur les cycles du LFO.
- Tout autre cas : non concluant, et dit.

### 16.1 Verdict de H54 (01/10/2026, mesuré de 23 h 00 à 23 h 01) : NON CONCLUANTE comme écrit — et la mesure DÉFAIT la prémisse des §§ 15.2 et 16 : le fond des raies n'est pas la batterie

`analyse/mesure_h54.py` (branche `reload-h47`, instrument de `8ea9970` inchangé), le stem
« other » de la course de référence ; 43 s ; `reconstruction/travail/reload-h54/`.

| extrait | contrôle : cercles de la lecture parfaite par tirage | attendu 1 | sur le stem (pour mémoire) |
|---|---|---|---|
| 16-40 s | 2, 2, 2 sur 10 | **échec** | 0 cercle, 10 franchement hors |
| 130-154 s | 3, 3, 3 sur 10 | **échec** | 0 cercle, 9 hors (si4 non vue, 9,5 dB) |
| 272-296 s | 2, 3, 2 sur 10 | **échec** | 0 cercle, 10 hors (si4 non vue, 5,3 dB) |

Attendu 1 en échec sur les trois extraits : la règle écrite dit « H54 non concluante »,
et rien ne s'écrit sur la structure du pad.

**CE QUE LA MESURE DÉFAIT.** Les deux hypothèses reposaient sur une prémisse écrite au
§ 15.2 — « le bruit, c'est surtout la batterie » — et H54 la mettait à l'épreuve sans
le dire : retirer la batterie devait relever le rapport au fond. Il n'a pas bougé :
sur 16-40 s, le stem et l'original sont à **1,1 dB près sur dix raies sur dix** (mi3
20,7 / 20,6 dB ; si3 23,7 / 24,3 ; do♯5 10,0 / 10,2). Relevé direct, source par source,
de la densité dans la bande du fond (6 à 6,9 Hz de chaque raie), rapportée à celle de
l'original :

| raie | other | drums | bass | guitar | piano | vocals |
|---|---|---|---|---|---|---|
| mi3 | −0,6 | −11,9 | −46,7 | −47,0 | −48,5 | −48,6 |
| fa♯3 | +0,1 | −14,7 | −52,4 | −48,0 | −54,1 | −48,4 |
| la♯3 | −0,6 | −17,7 | −52,3 | −47,2 | −49,6 | −37,7 |
| si3 | +0,4 | −17,9 | −53,8 | −49,7 | −52,5 | −27,6 |
| do♯4 | −0,6 | −17,4 | −55,9 | −52,4 | −51,4 | −46,7 |
| mi4 | −0,5 | −26,2 | −64,9 | −63,5 | −62,0 | −58,6 |
| fa♯4 | −0,2 | −35,4 | −66,8 | −67,0 | −69,5 | −67,4 |
| la♯4 | −0,7 | −24,0 | −64,7 | −66,4 | −59,5 | −56,7 |
| si4 | −1,2 | −36,6 | −67,3 | −68,6 | −68,4 | −67,6 |
| do♯5 | −0,1 | −31,5 | −65,3 | −75,2 | −68,0 | −62,2 |

**Le fond des raies EST le stem « other »** ; la batterie est de 12 à 37 dB dessous.
Conséquences, dites sans les arrondir :
- **le contrôle du § 15.2 a modélisé le mauvais bruit** : il a réglé un bruit BLANC pour
  atteindre un rapport au fond que, dans l'original, fait le pad lui-même (ou ce que le
  stem « other » porte avec lui). Son verdict « aveugle » ne dit donc pas que
  l'instrument est aveugle au bruit de l'original ; il dit qu'il l'est à un bruit blanc
  de ce niveau. **H53 reste non concluante**, mais plus pour la raison écrite au § 15.3 ;
- le relevé d'après coup du § 15.3 (« l'original plus épais que le contrôle ») perd son
  témoin pour la même raison ;
- **la question change** : ce fond est-il du BRUIT (un ensemble dense, des voix
  désaccordées, une réverbération) ou une STRUCTURE déterministe — les composantes
  d'une modulation que H50 a trouvées sur une famille à deux cadences, et qui tombent
  aussi dans la bande du fond (`f1` + 2·`f2` = 6,49 Hz) ? Un fond déterministe ne
  brouille pas une trajectoire : il la DESSINE.

**CE QUI SUIT, écrit avant sa mesure : H55 (§ 17)** — la suite que la règle du § 16
nommait pour ce cas (« un instrument qui moyenne par note sur les cycles du LFO »),
précisée par ce que la mesure vient d'apprendre.

---

## 17. H55 — le fond des raies est-il du BRUIT ou une STRUCTURE ? La trajectoire repliée sur les deux cadences (écrite AVANT la mesure, 01/10/2026, 23 h 03)

**Pourquoi.** § 16.1 : le fond des raies est le stem « other » lui-même. Si le pad est
un son modulé par deux LFO libres (H50 : `f1` ≈ 1,75 Hz, `f2` ≈ 2,37 Hz), alors à chaque
instant l'enveloppe complexe `E(t)` d'une raie n'est fonction QUE des deux phases
`(φ1, φ2)` : la trajectoire, aussi embrouillée qu'elle paraisse dans le temps, se
REPLIE sur le tore des deux phases en une surface nette. Un bruit (un ensemble dense,
une réverbération) ne se replie pas. Et la structure repliée se juge ensuite comme au
§ 15 : une seule lecture donne un cercle pour TOUT couple de phases.

**L'instrument** (`analyse/mesure_h55.py`, branche `reload-h47`), par raie, sur `E(t)`
tel que le § 15 le construit (bande adaptée, porteuse affinée), à 200 Hz :
- le **repli** : chaque instant rangé dans une case de 8 × 8 selon `(f1·t mod 1, f2·t
  mod 1)` ; la **moyenne par case** `M` est la structure, l'écart à elle le résidu ;
- la **part déterministe** `D = 1 − Σ|E − M|² / Σ|E − Ē|²` ; un bruit pur donne `D`
  voisin de 64 cases sur 4 400 instants, ≈ 0,015 ;
- les **cadences** cherchées, communes à toutes les raies vues d'un extrait, sur une
  grille de 0,5 mHz (`f1` de 1,744 à 1,756 Hz, `f2` de 2,355 à 2,385 Hz), au maximum de
  `D` — une recherche, donc ses témoins la subissent aussi ;
- la **forme repliée** : les cases de `M` jugées comme au § 15.1 — circularité,
  aplatissement, et l'ARC COUVERT (360° moins le plus grand vide angulaire autour du
  centre : les cases n'ont pas d'ordre temporel, l'arc déroulé n'a pas de sens).

**ATTENDUS** :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | **l'instrument** (tests écrits avant), cinq hauteurs, bruit à −30 dB : (a) une lecture ; (b) deux étages en série (S de H51) ; (c) une modulation ALÉATOIRE (bruit complexe passe-bas à 8 Hz) | (a) `D` ≥ 0,9, `M` en cercle (≤ 5 %), cadences relues à ± 2 mHz ; (b) `D` ≥ 0,9, `M` hors cercle (> 15 %) sur les raies du haut ; (c) `D` ≤ 0,3 | une seule faute : rien ne se lit |
| 2 | **structure ou bruit ?** `D` de chaque raie vue de l'ORIGINAL | `D` ≥ 0,7 sur au moins 8 raies : une STRUCTURE à deux cadences | `D` ≤ 0,3 sur 5 raies ou plus : un BRUIT |
| 3 | si 2 tient : **la structure est-elle UNE lecture ?** `M` de chaque raie | en cercle (§ 15.1) sur au moins 8 raies | circularité > 25 % ou aplatissement < 0,10 sur 5 raies ou plus : plusieurs lectures |
| 4 | **les cadences** trouvées sur chaque extrait | à ± 3 mHz de celles de H50 (1,7495-1,7501 ; 2,37) | ailleurs : ce ne sont pas les cadences de H50 qui commandent |

**Les extraits** : les trois de H54 (16-40, 130-154, 272-296 s), sur l'ORIGINAL ; aucun
n'a été replié. Chaque extrait est jugé ; un verdict qui diffère d'un extrait à l'autre
est dit.

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) :
- **2 et 3 tenus** : le pad est un direct plus UNE lecture sous deux LFO ; `M` donne le
  retard en fonction des deux phases, donc la forme et la profondeur de chaque LFO —
  c'est cet effet que le rack reçoit (une phase de `ROADMAP-daw.md`).
- **2 tenu, 3 en échec** : une structure déterministe à PLUSIEURS lectures ; la suite
  les compte sur `M` (une somme de cercles par couple de phases).
- **2 en échec** : le fond est un bruit — un ensemble ou une réverbération, pas un
  effet à deux LFO ; la suite mesure sa largeur et son temps de décorrélation.
- **2 entre les deux** : non concluant, et dit.

### 17.1 Ce que les tests de l'instrument ont changé — AVANT toute mesure de l'original (01/10/2026, 23 h 41)

L'attendu 1 est la suite `analyse/tests/test_h55_repli.py` (branche `reload-h47`,
`bbcc49a`). **Ses trois tests ont défait trois choix du § 17**, chacun corrigé avant
que l'instrument ait vu l'original :

1. **Le repli en cases ne tient pas une modulation profonde.** Sous une lecture dont
   l'indice atteint 3 radians (do♯5, 0,85 ms à `f1`), la phase de la raie tourne de
   plus de deux radians à l'intérieur d'une seule case de 8 × 8 : une lecture PARFAITE
   n'y donnait que D = 0,78 (la série 0,51). Remplacé par ce que le repli approchait :
   **la famille elle-même**, les composantes `k·f1 + l·f2` (|k| ≤ 8, |l| ≤ 6) qui
   tombent dans la bande — 165 dans ± 15 Hz, écart minimal 0,11 Hz —, ajustées par
   moindres carrés. Une fonction du tore EST une somme de ces composantes.
2. **Un ajustement non validé explique un bruit.** Sur la modulation aléatoire, la
   famille ajustée sur l'extrait entier « expliquait » **0,28 à 0,34** de la variance
   (trois raies sur cinq au-delà du seuil de 0,3) : 165 composantes complexes contre
   un bruit de 16 Hz sur 22 s. **D est donc VALIDÉE PAR MOITIÉS** : la famille ajustée
   sur une moitié prédit l'autre, dans les deux sens. Un bruit ne se prédit pas : −0,41
   à −0,75. Vu rouge la validation retirée.
3. **La recherche des cadences par projection se trompait** : sur une lecture parfaite
   à 1,750 / 2,370 Hz, le périodogramme de la famille choisissait 1,752 / 2,3575 (des
   membres voisins, non orthogonaux sur 22 s, y comptent deux fois le même pic).
   Moindres carrés exacts, sur une grille de 1 mHz puis de 0,25 mHz autour du meilleur
   point ; `E(t)` décimée à 50 Hz pour que cela tienne à côté de la course.

La forme repliée devient la famille ajustée sur l'extrait entier, ÉVALUÉE sur 24 × 24
couples de phases, puis jugée comme au § 15.1 avec l'arc couvert.

| structure (cinq raies) | cadences relues | D validée | forme repliée : circularité |
|---|---|---|---|
| une lecture | 1,750 / 2,370 | 0,999 à 1,000 | 0,2 à 0,3 % — cinq cercles |
| deux étages en série | 1,750 / 2,370 | 0,999 à 1,000 | 13,5 à 36,8 % (36,4 et 36,8 sur les deux du haut) |
| modulation aléatoire | (sans objet) | −0,75 à −0,41 | 43 à 46 % |

**L'attendu 1 est TENU.** Les attendus 2 à 4 et les décisions du § 17 sont inchangés ;
les seuils de D (0,7 et 0,3) s'appliquent à la D validée.

### 17.2 Verdict de H55 (01/10/2026, mesuré de 23 h 42 à 23 h 53) : attendus 2 et 4 en ÉCHEC tels qu'écrits — NON CONCLUANTE, parce que la prémisse commune à H53, H54 et H55 est fausse : les raies ne sont pas des notes TENUES

`analyse/mesure_h55.py` (branche `reload-h47`, `bbcc49a`), l'original, trois extraits ;
11 min ; `reconstruction/travail/reload-h55/mesure.json`.

| extrait | cadences trouvées | D validée ≥ 0,7 | D validée ≤ 0,3 | D par raie |
|---|---|---|---|---|
| 16-40 s | 1,7542 / 2,3635 Hz | 0 | 8 sur 10 | de −2,94 à 0,53 |
| 130-154 s | 1,7552 / 2,3640 Hz | 0 | 9 sur 10 | de −1,44 à 0,36 |
| 272-296 s | 1,7547 / 2,3635 Hz | 0 | 7 sur 10 | de −1,14 à 0,45 |

Attendu 2 en ÉCHEC sur les trois extraits ; attendu 3 sans objet ; **attendu 4 en
ÉCHEC** d'une façon qui mérite d'être dite : les trois extraits donnent les MÊMES
cadences entre eux à 1 mHz près — 1,7547 ± 0,0005 et 2,3637 ± 0,0003 Hz —, à 4 à 6 mHz
de celles de H50. Trois extraits indépendants qui s'accordent si bien ne lisent pas un
bruit ; ce relevé n'est pas un attendu, il est publié.

**La règle écrite dirait « le fond est un bruit ». Elle ne s'applique pas, et voici
pourquoi — un relevé de l'hypothèse de l'instrument, fait AVANT d'écrire le verdict.**
Les trois instruments (§§ 15, 16, 17) supposent qu'une raie de l'oracle SONNE pendant
tout l'extrait ; leurs tests le supposaient aussi. Le niveau efficace de chaque raie,
par tranches de 2 s des 22 s utiles, le dément :

| extrait | raies qui s'éteignent ensemble (creux en dB sous la tranche la plus forte) |
|---|---|
| 16-40 s | mi3, si3, mi4, si4 : −16,0 à −17,4 ; −9,8 à −10,6 ; −16,8 à −17,6, aux tranches 2, 5 et 9 · la♯3 et la♯4 : −26 à −29, −17 à −18, −23 à −27 · do♯4 et do♯5 : −14 à −15, −11 à −12 · fa♯3 et fa♯4 : jamais sous −5,5 |
| 130-154 s | sol♯4 ne sonne que de la 8e à la 14e seconde du segment utile (−25 à −28 dB ailleurs) ; la♯3 et la♯4 : −25 à −26 dB deux fois |
| 272-296 s | sol♯4 absent les 4 premières secondes (−28 dB) ; la♯3 : −23 dB |

**Les raies vont et viennent par PAIRES D'OCTAVES, aux mêmes instants** : le pad change
d'accord toutes les quelques mesures, et les « dix notes de l'oracle » sont la RÉUNION
d'une suite d'accords, pas dix notes tenues. Ce que cela fait aux trois mesures :
- une raie qui s'éteint ramène sa trajectoire vers l'origine — un NUAGE de circularité
  30 à 50 %, avec des tours que l'angle accumule en frôlant le centre, quelle que soit
  la structure : c'est ce que H53 et H54 ont lu ;
- une enveloppe d'accords (de l'ordre de 0,1 Hz) n'est pas dans la famille des deux
  cadences et ne se PRÉDIT pas d'une moitié à l'autre : c'est ce que la D validée de
  H55 a lu ;
- les contrôles et les tests synthétiques tenaient leurs notes 24 s : ils ne pouvaient
  pas le voir.

**H55 est NON CONCLUANTE**, et H53 et H54 le restent pour la même raison, désormais
connue. Rien ne s'écrit sur la structure du pad. La leçon est celle de l'ordre de
marche (13/09, D265) : avant d'expliquer un chiffre, décomposer sa population — ici,
le temps : une raie de 24 s est une suite de présences et d'absences.

**CE QUI SUIT, à écrire avant sa mesure (H56)** : la même question, ACCORD PAR ACCORD —
sur les seuls segments où l'ensemble des raies qui sonnent ne change pas, chaque raie
jugée sur son segment, avec un contrôle synthétique soumis aux MÊMES segments.

---

## 18. H56 — accord par accord : là où une raie SONNE, décrit-elle un cercle ? (écrite AVANT la mesure, 01/10/2026, 23 h 56)

**Pourquoi.** § 17.2 : les trois instruments précédents jugeaient des raies qui
s'éteignaient de 10 à 29 dB au fil des accords. La question du § 15 — « un direct plus
UNE lecture » — reste entière ; elle se pose maintenant là seulement où la note sonne.

**L'instrument** (`analyse/mesure_h56.py`, branche `reload-h47`), par raie, sur `E(t)`
construit comme au § 15 (bande adaptée, porteuse affinée), à 200 Hz :
- l'**enveloppe lente** `env(t)` : la racine de la moyenne glissante de `|E|²` sur
  1,5 s — deux cycles et demi de `f1`, trois et demi de `f2` : le mouvement d'une
  lecture s'y moyenne, un changement d'accord non ;
- les **segments où la note sonne** : `env` à moins de 10 dB de son maximum sur
  l'extrait, d'un seul tenant pendant au moins 3 s, rognés de 0,25 s à chaque bout ;
- sur chaque segment, la trajectoire NORMALISÉE `E / env`, jugée par l'instrument du
  § 15.1 tel quel (circularité ≤ 10 %, arc ≥ 60°, aplatissement ≥ 0,10) ; le retard à
  `f1` relu sur le segment.

**ATTENDUS** :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | **l'instrument** (tests écrits avant), cinq hauteurs, bruit à −30 dB, des notes qui s'allument et s'éteignent par segments de 4 à 7 s (fondus de 50 ms) : (a) une lecture ; (b) deux étages en série ; (c) une lecture sous des notes tenues 24 s | (a) au moins 90 % des segments en cercle, retard à `f1` relu à ± 15 % ; (b) au plus 20 % des segments des raies du haut en cercle ; (c) toutes les raies en cercle | une seule faute : rien ne se lit |
| 2 | **le contrôle** : la même lecture synthétique, chaque raie portant l'enveloppe lente MESURÉE sur l'original (la même `env(t)`), jugée sur les mêmes segments | au moins 80 % des segments en cercle | moins : l'extrait ne juge rien (l'instrument est aveugle à CES enveloppes) |
| 3 | **l'original**, sur les extraits où 2 tient | au moins 80 % des segments en cercle : « un direct plus une lecture » | au plus 20 % : ce n'est pas elle |
| 4 | si 3 tient : **un seul retard ?** l'amplitude du retard à `f1` d'un segment à l'autre, toutes raies | dispersion ≤ 15 % : un effet sur le bus | > 40 % : par note |

**Les extraits** : les trois de H55, sur l'original. Les segments sont publiés raie par
raie (début, fin, niveau).

**CE QUE LE VERDICT DÉCIDERA** (écrit avant) :
- **3 et 4 tenus** : le pad est un direct plus UNE lecture sur le bus ; son retard, sa
  profondeur et son dosage sont lus segment par segment, et c'est cet effet — mesuré —
  que le rack reçoit (une phase de `ROADMAP-daw.md`).
- **3 tenu, 4 en échec** : une lecture par note — la modulation est dans la machine,
  pas dans un effet.
- **3 en échec** (2 tenu) : plusieurs lectures ; la suite les compte.
- **2 en échec sur deux extraits ou plus** : non concluante — et l'on cesse d'empiler
  des instruments sur ce pad sans regarder autre chose : la suite reviendrait à rendre
  le pad par le moteur avec les effets du rack, et à juger à l'oreille et au § 0.

### 18.1 Ce que les tests de l'instrument ont changé — AVANT toute mesure de l'original (02/10/2026, 00 h 00)

L'attendu 1 est la suite `analyse/tests/test_h56_accords.py` (branche `reload-h47`).
Sur une lecture parfaite à notes intermittentes (portes de 4 à 7 s), **trois choix du
§ 18 sont tombés**, chacun corrigé avant que l'instrument ait vu l'original :

1. **La porteuse affinée sur l'extrait entier se trompe sous les portes** : de 0,03 à
   0,10 Hz (si3 : 248,618 Hz pour 248,702) — le filtre de ± 0,3 Hz voit les bandes
   latérales de la porte. Sur 5 s, le cercle devient un anneau : **0 segment en cercle
   sur 11**. Elle est désormais affinée SUR CHAQUE SEGMENT : le décalage, cherché à
   ± 0,15 Hz par pas de 2 mHz, qui rend la trajectoire la plus circulaire. **Cette
   recherche favorise le cercle, et c'est dit** : la série la subit aussi, et n'en
   devient pas un (0 segment sur 6, circularités de 13 à 25 %).
2. **Normaliser par l'enveloppe déformait le rayon** : une fenêtre de 1,5 s ne moyenne
   pas un nombre entier de cycles des LFO, et `E / env` gardait leurs ondulations —
   9 segments sur 11, circularités de 6 à 13 %. L'enveloppe ne sert plus qu'à TROUVER
   les segments ; la trajectoire est jugée telle quelle.
3. **Le rognage de 0,25 s laissait la transition dans le segment** : il vaut une
   demi-fenêtre d'enveloppe, 0,75 s.

| cas (cinq raies) | segments en cercle | circularité | retard à `f1` relu (0,85 ms) |
|---|---|---|---|
| une lecture, portes graine 11 | 11 sur 11 | 0 à 1 % | 0,845 à 0,892 ms |
| une lecture, portes graine 12 | 11 sur 11 | 0 à 9 % | 0,794 à 0,853 ms |
| deux étages en série, portes graine 11 | 0 sur 6 | 13 à 25 % | — |
| une lecture, notes tenues 24 s | 5 sur 5 | 0 à 1 % | 0,850 à 0,852 ms |

**Une précision sur l'attendu 1 (b)** : intermittentes, les deux raies du haut de la
série tombent sous 10 dB de leur fond (7,3 et 8,8 dB) et n'ont plus de segment ; (b) se
juge donc sur TOUS les segments de la série. **L'attendu 1 est TENU.** Les attendus 2 à
4 et les décisions du § 18 sont inchangés ; le contrôle (attendu 2) passe par le même
instrument corrigé.

### 18.2 Verdict de H56 (02/10/2026, mesuré de 00 h 01 à 00 h 02) : NON CONCLUANTE — et la règle d'arrêt écrite avant s'applique

`analyse/mesure_h56.py` (branche `reload-h47`), l'original, trois extraits, l'original
et son contrôle jugés sur les mêmes segments ; 35 s ;
`reconstruction/travail/reload-h56/mesure.json`.

| extrait | contrôle : segments en cercle | attendu 2 | original : segments en cercle (pour mémoire) |
|---|---|---|---|
| 16-40 s | 15 sur 26 (58 %) | **échec** | 0 sur 26 |
| 130-154 s | 13 sur 22 (59 %) | **échec** | 0 sur 22 |
| 272-296 s | 14 sur 25 (56 %) | **échec** | 0 sur 25 |

L'attendu 2 échoue sur les trois extraits : une lecture parfaite, portant les enveloppes
mesurées sur l'original, n'est relue cercle que sur 56 à 59 % des segments — l'instrument
reste aveugle à CES enveloppes. **H56 est NON CONCLUANTE**, et rien ne s'écrit sur la
structure du pad.

**Ce que le contrôle montre de son aveuglement** : il voit sur les raies du bas et du
milieu (mi3 à mi4 : circularités de 3 à 14 %, la plupart des segments en cercle), et ne
voit pas sur les deux fa♯, tenus d'un seul segment de 19 à 20 s (12 à 21 % : leur niveau y
descend jusqu'à 5 dB sous son maximum) ni sur les raies du haut (sol♯4, la♯4 : 15 à 26 %).

**Relevé, et PAS une conclusion** : sur les raies où le contrôle voit, l'original n'est
jamais un cercle — si3 25 à 34 % (contrôle 3 à 8 %), la♯3 27 à 36 % (3 à 9 %), do♯4 28 à
35 % (4 à 9 %). La règle écrite ne lit pas l'attendu 3 raie par raie ; ce relevé ne la
remplace pas.

**LA RÈGLE D'ARRÊT, écrite au § 18 avant la mesure** : « 2 en échec sur deux extraits
ou plus : non concluante — et l'on cesse d'empiler des instruments sur ce pad sans
regarder autre chose : la suite reviendrait à rendre le pad par le moteur avec les
effets du rack, et à juger à l'oreille et au § 0. » **Elle s'applique.** Le bilan de la
lignée, dit sans l'arrondir : de H47 à H56, huit hypothèses sur le pad (H48 et H52
portaient sur la chaîne) — H47 non concluante, H49 (un mouvement au morceau, discret),
H50 tenue (une modulation de retard à deux cadences, sur des sinus), H51 partielle (deux
étages en série à 2,85 dB en validation), et quatre instruments de STRUCTURE (H53 à H56) non concluants, chacun pour une raison que son
successeur a trouvée — le bruit du contrôle, la batterie qui n'était pas le fond, des
notes qui n'étaient pas tenues, des enveloppes qui aveuglent encore le dernier. Aucune
de ces quatre n'a donné un chiffre sur lequel le rack puisse être réglé.

**La suite (H57, à écrire avant sa mesure)** : rendre le pad par le moteur — des sinus,
sous l'effet que H51 a trouvé (deux étages en série, réglages S) et sous le chorus du
rack tel qu'il est — et juger contre l'original au § 0, sur les extraits jamais
entendus par cette lignée, et à l'oreille.

---

## 19. H57 — le pad rendu par le MOTEUR : des sinus sous les effets du rack, jugés sur des extraits jamais entendus (écrite AVANT la mesure, 10/10/2026, 17 h 34)

**Pourquoi maintenant, et ce que le § 18.2 avait décidé.** La règle d'arrêt du § 18 s'est appliquée :
on cesse d'empiler des instruments de STRUCTURE sur ce pad, et l'on rend le pad par le moteur — des
sinus, sous l'effet que H51 a trouvé et sous le rack tel qu'il est —, jugé contre l'original. La course 2
(§ 4.4) tourne : ce qui suit RENDRE peu (quelques secondes de son, quelques rendus), et passe sous
`nice` ; ce qui rendrait beaucoup attend sa fin.

**CE QUE LE RACK PEUT, lu dans le code avant d'écrire l'hypothèse.** H51 a trouvé deux étages de retard
modulé en série : `f1` (base 1,81 ms, profondeur 1,68 ms, dosage 0,47) puis `f2` (base 6,81 ms,
profondeur 1,10 ms, dosage 0,41), deux lectures en quadrature. Dans le modèle de H51, la base est le
retard MINIMAL (`base + profondeur · (½ + ½ sin)`, `retard_s`) : `f1` va de 1,81 à 3,49 ms, `f2` de 6,81
à 7,91 ms. Le **flanger** du rack retarde de `1 ms + lfo · 6 ms · profondeur`, `lfo` ∈ [0, 1], gauche et
droite en QUADRATURE — la forme de `f1`, mais son minimum est fixé à 1 ms : celui de `f1`, 1,81 ms, ne
s'y atteint pas. **Réglage, aux moindres carrés sur la trajectoire du retard** : profondeur 0,46, de 1 à
3,76 ms. Le **chorus** a sa base fixée à 8 ms : de 8 à 9,1 ms pour une profondeur de 1,10, là où `f2` va
de 6,81 à 7,91. *(Corrigé le 10/10 à 17 h 36, avant le code : la première écriture prenait la base pour
un centre — « de 0,13 à 3,49 ms » — en lisant mal `retard_s` ; la conclusion — le rack n'atteint pas
`f1` — ne change pas, le réglage si.)* **H57 se mesure donc en
deux temps** : H57a avec le rack TEL QU'IL EST, à ses réglages les plus proches ; H57b — un retard de
base réglable dans le moteur — seulement si H57a échoue POUR CETTE RAISON (son attendu 4).

**L'HYPOTHÈSE H57a.** Des notes tenues (l'oracle de H47), jouées par une machine SINUSOÏDALE du parc
(`vsm.additive`, un seul partiel — mesurée « propre » par H51), sous le flanger du rack (réinjection 0,
cadence `f1`, profondeur et dosage au plus près de l'étage `f1`) puis sous son chorus (cadence `f2`,
profondeur 1,10 ms, dosage 0,41), approchent le stem « other » de l'original plus près que les mêmes
sinus NUS, et plus près que la meilleure machine du parc de H47 — sur des extraits que la lignée n'a
JAMAIS mesurés : **84 à 92 s, 154 à 162 s, 211 à 218 s** (deux ponts et la fin du long pont ; 7 à 8 s
chacun, un accord, la basse absente — vérifié sur l'énergie sous 120 Hz avant la mesure, et dit).

**LES MESURES**, celles de H47 (§ 9) sans en changer une, contre l'extrait du stem « other », chaque
rendu calé à son niveau : le log-mel des cases qui portent, l'équilibre par bande, la tenue ; PLUS la
**largeur** (side/mid) — le § 2.4 a trouvé la reconstruction MONO (0,0001) là où l'original est à
0,048 et son stem « other » à 0,052 : le flanger en quadrature est la première pièce du parc qui donne
une largeur à une note tenue. Bornes : `B_égal`, `B_mesuré` (§ 9).

**ATTENDUS, sur chacun des trois extraits :**

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | l'instrument : `B_mesuré` ≤ `B_égal` au log-mel ; l'extrait contre lui-même = 0 | tenu | l'instrument est faux : rien ne se lit |
| 2 | log-mel : sinus + rack contre sinus NUS | au moins 1,0 dB de mieux | pas mieux : le rack n'apporte rien au timbre |
| 3 | log-mel : sinus + rack contre la meilleure machine de H47 sur le MÊME extrait | mieux | pire de plus de 1 dB |
| 4 | log-mel : sinus + rack contre `B_mesuré` | à 2 dB ou moins | à plus de 3 dB — et si le flanger plafonne à sa base de 1 ms (son réglage optimal collé à la borne), c'est H57b |
| 5 | la largeur du rendu (side/mid) | à ± 30 % de celle de l'extrait | sous 0,01 : le rack ne donne pas de largeur |
| 6 | la tenue | ≥ −1 dB (des sinus tenus) | — (un contrôle) |

**La règle de décision, écrite avant** : 2, 3 et 5 tenus sur deux extraits au moins → le pad se rend par
« sinus + rack », et la chaîne apprend à le choisir (une hypothèse à elle) ; 2 tenu sans 3 → le rack aide,
une machine du parc fait mieux, rien n'entre ; 2 en échec partout → l'effet de H51 n'est pas celui du
rack, et H57b ne s'ouvre que si l'attendu 4 accuse la base de 1 ms. **Et l'oreille** : les trois extraits,
original, sinus nus, sinus + rack, écrits en `.wav` côte à côte pour l'utilisateur — la chaîne n'écoute pas
(§ 0).

### 19.1 Verdict de H57a (10/10/2026, mesuré de 17 h 38 à 17 h 42, écrit à 17 h 43) : la règle écrite dit « rien n'entre » — et deux de ses attendus étaient jugés par une mesure que H47 avait déjà prise en défaut

`analyse/mesure_h57.py` (branche `reload-h47`, `3ea6ff3`), le stem « other » de la course de référence,
`vsm-render` de `build-h51`, sous `nice` à côté de la course 2 ; 229 s ;
`reconstruction/travail/reload-h57/mesure.json`. Les trois extraits portent le MÊME accord à l'oracle (onze
notes : mi, fa♯, la♯, si, do♯ sur deux octaves, et **sol♯4**, absent du § 1) — d'où des rendus de sinus
identiques d'un extrait à l'autre, et c'est vérifié, pas une panne. Énergie sous 120 Hz : −25,6 / −27,4 /
−26,3 dB du total ; la basse n'y est donc pas tout à fait absente. Le contrôle : les effets changent le son
(écart efficace 0,75 du niveau des sinus nus).

| extrait | 1 instrument | 2 rack contre nus | 3 contre le parc (1re à `D`) | 4 contre `B_mesuré` | 5 largeur | 6 tenue |
|---|---|---|---|---|---|---|
| 84-92 s | tenu | **tenu** (23,40 contre 25,17, +1,77) | échec (E-Piano 10,33 ; +13,07) | échec (+4,50) | **échec** (0,416 contre 0,070) | tenu |
| 154-162 s | tenu | **tenu** (26,30 contre 28,41, +2,10) | échec (Harp 11,19 ; +15,11) | échec (+3,16) | **échec** (0,416 contre 0,074) | tenu |
| 211-218 s | tenu | **tenu** (20,64 contre 22,01, +1,37) | échec (E-Piano 8,92 ; +11,72) | échec (+4,01) | **échec** (0,413 contre 0,056) | tenu |

**LA RÈGLE ÉCRITE, appliquée telle quelle** : « 2 tenu sans 3 → le rack aide, une machine du parc fait mieux,
rien n'entre ». **Rien n'entre dans la chaîne.**

**CE QUI ÉTAIT FAUX DANS L'ÉCRITURE DE H57, et que le verdict ne doit pas cacher.** Les attendus 3 et 4
jugent au log-mel des cases qui portent ; or H47 (§ 9.2) avait trouvé que **ses bornes ne bornent pas** :
sur 16-40 s, 141 candidates sur 190 passaient sous `B_mesuré`, une harpe et un piano électrique devant les
sinus de l'oracle, et le classement `D` de la chaîne ne corrélait pas à ce log-mel (Spearman −0,018). Le
même défaut est ici, plus fort : la meilleure guitare nylon échantillonnée est à **5,3 à 6,5 dB**, sous des
bornes de sinus parfaits à 16,6 à 23,1 dB. Écrire « les mesures de H47 sans en changer une » reprenait donc
une mesure DÉJÀ prise en défaut : les attendus 3 et 4 ne disent pas si le rack approche le pad — ils disent
que ce log-mel préfère des sons attaqués. Ce n'est pas un échec du rack, c'est l'instrument, et c'était
écrit avant H57 ; je ne l'ai pas relu.

**CE QUI TIENT, et n'en dépend pas.**
- **L'attendu 2, trois fois sur trois** : à notes et diapason égaux, le flanger puis le chorus du rack
  rapprochent les sinus de l'original de 1,4 à 2,1 dB au log-mel — le même instrument des deux côtés, une
  seule variable : la comparaison se lit, même si l'échelle absolue est suspecte.
- **L'attendu 5, trois fois sur trois, et c'est le résultat le plus net** : le rack rend le pad **six fois
  trop large** (side/mid 0,41 contre 0,056 à 0,074). Le flanger du rack met ses deux lectures en quadrature
  sur la GAUCHE et la DROITE ; dans l'original, la modulation que H50-H51 ont trouvée **ne se répartit pas
  ainsi** — le pad est presque mono. Les « deux lectures en quadrature » de H51 étaient une propriété de la
  somme MONO (elles éteignent 2·`f1`), pas une image stéréo. Le pad se modulera donc des DEUX côtés à la
  fois, ou ne se modulera pas par ce flanger.

**À L'OREILLE** — la chaîne n'écoute pas (§ 0) : les trois extraits, l'original, les sinus nus et les
sinus + rack, calés au même niveau efficace, sont dans `reconstruction/travail/reload-h57/ecoute/`.

**La suite, sans l'écrire ici comme hypothèse** : H57b (une base réglable) ne s'ouvre pas — sa condition
était « l'attendu 4 accuse la base de 1 ms », et l'attendu 4 n'est pas lisible. Ce que ce verdict laisse
de mesurable : (1) un jugement du timbre qui ne préfère pas les sons attaqués — la question que H47 a
posée et que personne n'a tranchée ; (2) un rack dont la modulation est la même à gauche et à droite.

---

## 20. H59 — les extrêmes manquent parce que la batterie est calée EN BLOC : le niveau de chaque piste de batterie, résolu sur les bandes de son stem (écrite AVANT la mesure, 10/10/2026, 17 h 45)

**Numéro.** H57 est la dernière écrite ; H58 est prise (§ 10.4). H59.

**CE QUI A ÉTÉ LU AVANT, et ne tranche rien** (10/10, 17 h 44) — la part de chaque bande dans le total,
original contre le rendu de la référence (`reload-h42/temoin.wav`, `build-h42`), et dans les stems :

| bande | original | référence | écart | stem drums | stem other |
|---|---|---|---|---|---|
| 20-60 Hz | −18,6 | −25,8 | **−7,2** | −16,5 | −35,3 |
| 60-150 Hz | −4,2 | −8,7 | **−4,5** | −2,4 | −16,7 |
| 150-500 Hz | −4,4 | −1,9 | +2,5 | −7,4 | −1,3 |
| 500-2 000 Hz | −9,4 | −8,4 | +1,0 | −20,4 | −6,2 |
| 2-6 kHz | −13,3 | −13,8 | −0,5 | −11,3 | −45,4 |
| 6-10 kHz | −13,2 | −17,5 | **−4,3** | −11,0 | −63,2 |
| 10-16 kHz | −14,9 | −20,9 | **−6,0** | −12,6 | −68,6 |

Les deux extrémités manquent, et **toutes deux viennent de la batterie** (le stem « other » est à −45 dB et
plus au-dessus de 2 kHz, à −35 dB sous 60 Hz). Or la chaîne cale la batterie EN BLOC : ses trois pistes
(charleston, kick, caisse claire) reçoivent le MÊME volume, 1,823 (« groupe Batterie : rms stem 0,1203, somme
des 3 pistes 0,0594 », journal de la course) — un niveau d'ensemble juste, un partage entre pistes que rien ne
mesure. Un charleston trop discret et un kick sans grave donnent exactement ce tableau.

**L'HYPOTHÈSE H59.** Résoudre le volume de CHAQUE piste de batterie aux moindres carrés non négatifs sur
l'énergie par bande du stem « drums » — `E_stem(b) ≈ Σ_t g_t² · E_t(b)`, `E_t` mesurée sur le rendu SOLO de la
piste `t`, sept bandes —, à la place du volume commun, rapproche l'équilibre spectral de la reconstruction de
l'original sur les bandes où la batterie domine, sans rien coûter ailleurs. **Chaque bande pesée par l'inverse de son
énergie au stem** — une erreur RELATIVE : sur les énergies brutes, la bande de 60 à 150 Hz, la plus forte,
déciderait seule. Le nouveau volume d'une piste est son volume commun × √`g²`.

**LA MESURE, une seule variable** : le projet de la course de référence, rendu par `build-h42` (le moteur de son
témoin), ses trois pistes de batterie aux volumes résolus, TOUT le reste identique ; le TÉMOIN est
`reload-h42/temoin.wav` (même moteur, même projet, volumes communs), déjà mesuré par l'outil du § 0
(`ecart-temoin.json`, `--tempo 136`) ; l'essai mesuré par le MÊME outil, mêmes options. Les rendus solo des
trois pistes, la résolution et les volumes trouvés sont publiés.

**ATTENDUS** (outil du § 0, écart médian par bande) :

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | aigus | \|écart\| ≤ 2 dB (témoin −6,94) | pas de gain de 3 dB : les aigus manquants ne sont pas un partage de niveaux — le timbre du charleston |
| 2 | sub et basse | chacune gagne ≥ 1,5 dB vers 0 (témoin −1,33 et −3,15) | l'une s'éloigne |
| 3 | les quatre autres bandes | aucune ne s'éloigne de plus de 1 dB | une s'éloigne de plus de 1 dB |
| 4 | log-mel moyen | ≤ témoin (10,09 dB) | > témoin + 0,1 dB |
| 5 | le calage du kick (ms) | inchangé à 0,5 ms près | — (un contrôle : on ne touche à aucune note) |

**La règle de décision** : 1, 2 et 3 tenus → le calage par bande des pistes d'un groupe de batterie entre dans
la chaîne, par une hypothèse à elle, mesurée sur un morceau du banc à une variable ; 2 et 3 tenus sans 1 →
le grave est un partage, l'aigu est un timbre — le charleston du parc est à mesurer sur le stem seul.
**Quand** : après la course 2 — trois rendus solo et un rendu entier de 312 s ne se lancent pas à côté d'elle.

---

## 21. Le log-mel décomposé — 10,1 dB qui ne sont pas un timbre mais une DISPOSITION — et H60, la caisse claire qui est un clap (relevé à 17 h 50, H60 écrite AVANT sa mesure, 10/10/2026, 17 h 57)

**LE RELEVÉ, sans hypothèse** (la règle du dépôt : décomposer un chiffre avant de l'expliquer). Le log-mel du
§ 0 (10,09 dB) de la référence (`reload-h42/temoin.wav`), signé (reconstruction − original), par section du § 1
et par bande :

| section | sub | basse | bas-médium | médium | haut-médium | aigus | niveau efficace |
|---|---|---|---|---|---|---|---|
| intro 0-14 s (percussions seules) | **+30,8** | **+24,7** | +18,1 | +10,6 | +8,8 | +3,7 | +4,7 |
| pad 14-42 s | +12,6 | −2,5 | −8,5 | +1,3 | −3,7 | −8,2 | −1,4 |
| pleine 56-84 s | −16,3 | −12,2 | −5,3 | −3,4 | −12,3 | −12,9 | −1,3 |
| pont 84-92 s | **+24,0** | +8,6 | −6,6 | +4,5 | +7,3 | +2,9 | −0,6 |
| pleine 98-127 s | −12,3 | −8,5 | −5,7 | −7,1 | −13,1 | −14,8 | −0,7 |
| long pont 127-162 s | +11,2 | −3,7 | −9,2 | −0,1 | −4,0 | −7,8 | −1,4 |
| pleine 218-268 s | −7,7 | −6,9 | −6,6 | −5,4 | −9,3 | −11,2 | −0,9 |
| sortie 268-305 s | +15,2 | +0,9 | −5,4 | +3,1 | +1,3 | −3,0 | −0,6 |

Le niveau large bande de chaque section est juste à 1,4 dB près (sauf l'intro, +4,7) : **l'écart n'est pas un
niveau, c'est une disposition** — du grave là où l'original n'en a presque pas (intro, ponts : +24 à +31 dB),
pas assez de grave ni d'aigu là où il en a (les pleines : −7 à −16). La condition « niveau » du § 0, dominée par
le kick, ne pouvait pas le voir.

**QUI JOUE LE GRAVE DE L'INTRO** — chaque piste rendue SEULE sur 0-14 s (`build-h42`, au volume du projet),
énergie par bande contre l'original (même échelle, dB) :

| | sub | basse | bas-médium | médium | haut-médium | aigus |
|---|---|---|---|---|---|---|
| ORIGINAL 0-14 s | 53,2 | 49,7 | 55,5 | 67,7 | 77,0 | 80,3 |
| Batterie · **snare** | 49,7 | **80,6** | **80,8** | 73,5 | 81,2 | 78,0 |
| other · voix 4 (orgue) | 60,8 | 60,2 | 52,9 | 44,3 | 16,9 | 16,4 |
| piano (Minimoog) | 62,9 | 58,5 | 55,8 | 52,0 | 61,7 | 48,3 |
| Batterie · hihat | 13,5 | 17,0 | 31,9 | 49,5 | 64,5 | 69,0 |
| Batterie · kick | −4,9 | −0,2 | −8,3 | −26,8 | −64,0 | −75,9 |

Le kick ne joue pas dans l'intro (0,1 note par seconde au MIDI) ni dans les ponts (0) ; la « snare » y joue (2,4
par seconde, et presque rien dans les pleines) — exactement là où le grave est en trop. Elle a l'aigu de l'original
(78 contre 80 dB) et **trente et un décibels de trop entre 60 et 150 Hz** : 192 frappes que la transcription range
en caisse claire (note 38), jouées par la caisse claire du TR-808 (accord 120 Hz, timbre « snappy » 0,6), quand le
§ 1 nommait « caisse claire / clap » sans trancher. L'orgue de la voix 4 et le Minimoog du piano ajoutent 8 à 11 dB
de sub et de grave dans une intro où l'original n'a que des percussions — des notes de résidu de séparation, une
autre affaire, nommée ici et non mesurée.

**L'HYPOTHÈSE H60** : la frappe que la chaîne appelle « caisse claire » est un CLAP — sans corps grave —, et la même
partition jouée par le clap du TR-808 (note 39, la machine déjà choisie par l'arbitrage, ses réglages d'usine de
clap) rapproche l'intro et les ponts de l'original, sans rien coûter ailleurs.

**LA MESURE, une seule variable** : le projet de la référence, la piste « Batterie · snare » réécrite note 38 → 39
(et rien d'autre : ni volume, ni machine, ni autre piste), rendue par `build-h42` ; témoin : le même projet tel
quel. Sur l'intro (0-14 s) et les deux ponts (84-92 s, 211-218 s), le MÉLANGE entier, énergie par bande contre
l'original ; puis, la course 2 finie, le morceau entier par l'outil du § 0.

**ATTENDUS :**

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | intro, ponts : écart (mélange − original) en basse et bas-médium | réduit d'au moins 10 dB | réduit de moins de 5 dB : la caisse claire n'est pas la cause |
| 2 | intro, ponts : haut-médium et aigus | à ± 3 dB de l'original | à plus de 6 dB : le clap ne porte pas l'aigu de la frappe |
| 3 | morceau entier, § 0 : log-mel moyen | ≤ témoin (10,09) | > témoin + 0,1 dB |
| 4 | morceau entier, § 0 : les six bandes | aucune ne s'éloigne de plus de 1 dB | une s'éloigne de plus de 1 dB |

**La règle de décision** : 1, 2, 3 tenus → l'arbitrage de batterie de la chaîne doit choisir, frappe par frappe de
classe, entre caisse claire et clap (une hypothèse à elle, mesurée sur un morceau du banc à une variable) ; 1 tenu
sans 2 → le grave est la caisse claire, l'aigu est un autre timbre ; 1 en échec → la cause est ailleurs, et ce
relevé le dit.

### 21.1 Verdict de H60 (10/10/2026, mesuré de 17 h 58 à 17 h 59) : le grave EST la caisse claire, mais le clap ne porte pas son aigu — rien n'entre

Le projet de la référence, la piste « Batterie · snare » réécrite 38 → 39 (les 192 frappes, 384 événements ;
les autres pistes et `project.json` identiques, vérifié), rendu par `build-h42` (19 s) ; témoin :
`reload-h42/temoin.wav`, le même projet et le même moteur. **Écart au plan écrit** : le rendu entier et l'outil
du § 0 ont été passés PENDANT la course 2, sous `nice`, au lieu d'après — un rendu de 19 s et une mesure d'une
minute ; le plan écartait la charge, pas une autre valeur.

**Attendus 1 et 2** — le MÉLANGE, écart à l'original par bande (dB), témoin → essai :

| section | basse | bas-médium | médium | haut-médium | aigus |
|---|---|---|---|---|---|
| intro 0-14 s | **+31,1 → +14,2** | **+25,3 → +12,4** | +5,8 → +12,6 | +4,3 → +0,9 | −1,8 → **−7,2** |
| pont 84-92 s | **+19,6 → −4,3** | −2,2 → −3,9 | −1,8 → −0,7 | +3,7 → +0,3 | −0,4 → **−5,4** |
| pont 211-218 s | **+23,0 → −0,3** | −0,9 → −2,7 | −4,8 → −2,2 | +3,1 → −0,5 | −0,4 → **−6,1** |
| pleine 56-84 s (contrôle) | −7,4 → −7,4 | +2,7 → +2,7 | +1,3 → +1,3 | −10,9 → −10,9 | −9,2 → −9,2 |

**Attendus 3 et 4** — le morceau entier, l'outil du § 0, `--tempo 136` comme le témoin :

| mesure | témoin | essai |
|---|---|---|
| log-mel moyen (médian) | 10,09 (8,53) | **10,85 (9,47)** |
| sub · basse · bas-médium | −1,33 · −3,15 · +2,72 | −1,11 · −4,27 · +2,90 |
| médium · haut-médium · aigus | +1,66 · −3,01 · −6,94 | +2,47 · **−6,27** · −8,21 |
| niveau (pire tranche) | −0,57 (5,43) | −1,16 (4,68) |
| kick \|médian\| · p90 | 4,35 · 8,71 ms | 4,35 · 8,71 ms |

| # | attendu | verdict |
|---|---|---|
| 1 | basse et bas-médium de l'intro et des ponts réduits de 10 dB | **tenu en basse** (−16,9, −15,3, −22,7 dB d'écart), tenu en bas-médium dans l'intro (−12,9) ; dans les ponts, le bas-médium était déjà à 2,2 dB et s'éloigne de 1,7 |
| 2 | haut-médium et aigus à ± 3 dB | **échec** : le haut-médium tient (0,9 · 0,3 · 0,5) ; les aigus tombent à −7,2 · −5,4 · −6,1 |
| 3 | log-mel ≤ témoin | **échec** : 10,85 contre 10,09 |
| 4 | aucune bande ne s'éloigne de plus de 1 dB | **échec** : haut-médium +3,26, aigus +1,27, basse +1,12 |

**La règle écrite, appliquée** : « 1 tenu sans 2 → le grave est la caisse claire, l'aigu est un autre timbre ».
**Rien n'entre.** Ce que la mesure établit : les **trente et un décibels de grave en trop** dans l'intro et
les ponts viennent du CORPS de la caisse claire du TR-808 (son ton accordé), et le clap les retire ; mais le
SOUFFLE de la caisse claire (« snappy ») portait l'aigu et le haut-médium de la frappe originale, ce que le
clap ne fait pas — et sur le morceau entier, les tranches où la frappe porte le haut du spectre pèsent plus que
l'intro. La frappe de l'original est donc une caisse claire SANS corps grave, ou un clap AVEC un souffle :
c'est une question de réglage de la frappe, pas de choix entre deux voix. La suite nommée, sans hypothèse
écrite : la caisse claire du TR-808 son ton retiré ou monté (`drum.snare.tune`), son souffle gardé — une
variable — que l'arbitrage de batterie de la chaîne ne règle pas aujourd'hui, la recherche de piste ayant
trouvé « decay » et « tune » sur le kick, rien sur ce qui fait le grave de la caisse.

## 22. H61 — la caisse claire du TR-808 sans son ton : le souffle seul (écrite AVANT la mesure, 10/10/2026, 18 h 00)

**Ce que le code dit, lu avant d'écrire.** La caisse claire du TR-808 rend `ton · (1 − snappy) + souffle · snappy`
(`TR808Synth.h`, `SnareVoice`). Le projet de la référence l'a à **`tune` = 120 Hz — le BAS de sa plage**
(120-300 Hz, défaut 180) — et `snappy` = 0,6 : le réglage de piste de la batterie a cherché ses paramètres sur le
stem « drums » ENTIER, que le kick domine (« réglage piste … tune=50, tune=120, level=1, decay=0.449 »), et un
ton de caisse claire à 120 Hz tombe en plein dans la bande de 60 à 150 Hz où l'intro a 31 dB de trop (§ 21).

**L'HYPOTHÈSE H61** : la frappe de l'original est le SOUFFLE de la caisse claire sans son ton — `snappy` = 1, le
reste du réglage gardé — et elle tient ce que le clap de H60 tenait (le grave de l'intro et des ponts) sans perdre
ce qu'il perdait (l'aigu). **Une seule variable, et ce qu'elle fait d'autre est dit** : `drum.snare.snappy` 0,6 →
1,0 dans le préréglage de la piste « Batterie · snare » ; le ton disparaît, et le souffle monte de 20·log₁₀(1/0,6)
= **+4,4 dB** — la piste n'est pas recalée, pour que le geste reste un.

**LA MESURE** : celle de H60, à l'identique — le mélange par bande sur l'intro et les deux ponts, puis l'outil du
§ 0 sur le morceau entier, `build-h42`, contre le même témoin.

**ATTENDUS :**

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | intro, ponts : basse | écart réduit d'au moins 10 dB (comme le clap) | moins de 5 dB |
| 2 | intro, ponts : haut-médium et aigus | à ± 3 dB de l'original | à plus de 6 dB |
| 3 | morceau entier : log-mel moyen | ≤ témoin (10,09) | > témoin + 0,1 dB |
| 4 | morceau entier : les six bandes | aucune ne s'éloigne de plus de 1 dB | une s'éloigne de plus de 1 dB |

**La règle de décision** : 1 à 4 tenus → le réglage de la caisse claire (son ton, son souffle) entre dans ce que la
recherche de piste de batterie explore SUR LES FRAPPES DE CAISSE CLAIRE, et non sur le stem entier — une hypothèse à
elle, mesurée sur un morceau du banc ; 1 et 2 tenus, 3 ou 4 en échec → le souffle à +4,4 dB pèse trop ailleurs, et
la variable suivante est son niveau.

### 22.1 Verdict de H61 (10/10/2026, mesuré de 18 h 00 à 18 h 01) : le grave part, mais le souffle monté de 4,4 dB passe l'aigu de la frappe — 1 tenu, 2, 3 et 4 en échec

`drum.snare.snappy` 0,6 → 1,0 dans `instruments/track_07.synth.json`, seul fichier changé (vérifié par `cmp` sur
le projet, le MIDI et les deux autres préréglages de batterie) ; `build-h42`, témoin `reload-h42/temoin.wav`.

| section | basse | bas-médium | médium | haut-médium | aigus |
|---|---|---|---|---|---|
| intro | +31,1 → **+13,9** | +25,3 → +6,1 | +5,8 → +9,6 | +4,3 → **+8,7** | −1,8 → +2,3 |
| pont 84-92 s | +19,6 → **−5,7** | −2,2 → −4,0 | −1,8 → −1,4 | +3,7 → **+8,0** | −0,4 → +3,5 |
| pont 211-218 s | +23,0 → **−1,4** | −0,9 → −2,7 | −4,8 → −3,6 | +3,1 → **+7,5** | −0,4 → +3,7 |

Morceau entier (outil du § 0) : log-mel **10,09 → 10,53** ; sub −1,33 → −1,58 ; **basse −3,15 → −4,79** ; bas-médium
+2,72 → +2,18 ; médium +1,66 → +1,77 ; **haut-médium −3,01 → +0,99** ; **aigus −6,94 → −4,43** ; niveau −0,57 →
−0,39 (pire tranche 5,43 → 6,31) ; kick inchangé.

| # | verdict |
|---|---|
| 1 | **tenu** : la basse de l'intro et des ponts se rapproche de 17,2 · 13,9 · 21,6 dB |
| 2 | **échec** : le haut-médium passe à +7,5 à +8,7 dB — le souffle monté de 4,4 dB, l'effet secondaire que H61 disait |
| 3 | **échec** : log-mel +0,44 dB |
| 4 | **échec** : la basse du morceau s'éloigne de 1,64 dB — le ton de la caisse claire remplissait un peu le grave des pleines, où le vrai manque est ailleurs (kick, basse : § 21) |

**La règle écrite ne couvrait pas ce cas** (1 tenu, 2 en échec) : elle le disait pour « 1 et 2 tenus ». **Rien
n'entre.** Ce que la mesure montre : retirer le ton est juste dans l'intro et les ponts, et le haut du morceau
entier s'en rapproche de 2 à 2,5 dB ; ce qui fait échouer, c'est la part du geste que H61 nommait sans la
vouloir — le souffle plus fort. D'où H62.

## 23. H62 — le ton retiré, le souffle GARDÉ tel quel (écrite AVANT la mesure, 10/10/2026, 18 h 02)

**L'hypothèse** : `snappy` = 1 et le niveau de la caisse claire × 0,6 (`drum.snare.level` 0,8 → 0,48), de sorte
que `souffle · snappy · level` reste ce qu'il était (1 × 0,48 = 0,6 × 0,8) et que seul le ton disparaisse. C'est UN
geste — retirer le ton — écrit avec les deux paramètres qui le font sans rien changer d'autre ; H61 le faisait
avec un seul et changeait le souffle aussi.

**LA MESURE** : celle de H60 et H61. **ATTENDUS** : les quatre de H61, aux mêmes seuils ; et, écrit en plus, **la
basse du morceau entier** — H61 a montré qu'elle perd 1,64 dB quand le ton part : si H62 la perd aussi, l'attendu
4 échoue pour une raison connue, et la suite est le grave des pleines (le kick et la basse), pas la caisse claire.

**La règle de décision** : 1, 2 et 3 tenus → le TON de la caisse claire est retiré du réglage, et la recherche de
piste de batterie doit pouvoir le faire d'elle-même (une hypothèse à elle) ; 4 en échec par la seule basse, à
moins de 2 dB → noté, non bloquant (le grave des pleines est une autre affaire, § 21) ; 2 en échec → le souffle
de l'original n'est pas celui du TR-808 au même niveau, et c'est un timbre.

### 23.1 Verdict de H62 (10/10/2026, mesuré de 18 h 02 à 18 h 03) : le ton retiré seul — le grave de l'intro et des ponts part, rien d'autre ne bouge, et le log-mel gagne 0,09 dB

`drum.snare.snappy` 0,6 → 1,0 et `drum.snare.level` 0,8 → 0,48 (souffle · snappy · niveau : 0,48 avant et après),
`build-h42`, témoin `reload-h42/temoin.wav`.

| section | basse | bas-médium | médium | haut-médium | aigus |
|---|---|---|---|---|---|
| intro | +31,1 → **+13,9** | +25,3 → **+3,9** | +5,8 → +5,2 | +4,3 → +4,3 | −1,8 → −1,8 |
| pont 84-92 s | +19,6 → **−5,9** | −2,2 → −4,0 | −1,8 → −1,8 | +3,7 → +3,7 | −0,4 → −0,5 |
| pont 211-218 s | +23,0 → **−1,6** | −0,9 → −2,7 | −4,8 → −4,9 | +3,1 → +3,1 | −0,4 → −0,5 |

Morceau entier (outil du § 0) : **log-mel 10,09 → 10,00** (médian 8,53 → 8,48) ; sub −1,33 → −1,17 ; basse −3,15
→ −4,34 ; bas-médium +2,72 → +2,83 ; médium +1,66 → +2,03 ; haut-médium −3,01 → −2,71 ; aigus −6,94 → −6,73 ;
niveau −0,57 → −1,06, **pire tranche 5,43 → 4,77** ; kick inchangé.

| # | verdict |
|---|---|
| 1 | **tenu** : 17,2 · 13,7 · 21,4 dB de rapprochés |
| 2 | **entre les deux** : les aigus tiennent (−1,8 · −0,5 · −0,5) ; le haut-médium reste à +4,3 · +3,7 · +3,1, au-delà de ± 3 — EXACTEMENT les valeurs du témoin : H62 n'y touche pas, et le seuil, écrit pour H61 où le souffle bougeait, juge ici un écart qu'elle n'a pas fait |
| 3 | **tenu** : 10,00 contre 10,09 |
| 4 | **échec par la seule basse** (+1,19 dB d'éloignement, sous 2 dB : « noté, non bloquant » selon la règle écrite) |

**La règle, à la lettre** : « 1, 2 et 3 tenus » ne l'est pas — 2 est entre les deux, sur une bande que le geste ne
touche pas. **Ce que la mesure établit, sans l'arrondir** : le ton de la caisse claire du TR-808, réglé à 120 Hz
par une recherche menée sur le stem entier, fait les 31 dB de grave en trop de l'intro et des ponts ; le retirer
les ramène à 2-14 dB, sans changer une seule autre bande de ces sections ni des pleines (contrôle identique au
dixième), et le morceau gagne 0,09 dB de log-mel et 0,66 dB sur sa pire tranche. **C'est petit sur le morceau
entier**, parce que l'intro et les ponts n'en sont que 29 s sur 312. **Rien n'entre dans la chaîne aujourd'hui** :
retirer un ton à la main sur UN morceau n'est pas une règle. La suite que cela nomme est une hypothèse à elle — la
recherche de piste de batterie réglant CHAQUE voix sur SES frappes (la caisse claire sur les instants de la piste
« snare », pas sur le stem entier), mesurée sur un morceau du banc à une variable — et le grave des PLEINES (−7 à
−16 dB, § 21), la vraie part du log-mel, reste à attaquer : le kick et la basse.

### 20.1 Verdict de H59 (10/10/2026, mesuré de 18 h 04 à 18 h 05) : le plus grand gain du chantier au log-mel (10,09 → 8,49) — et deux attendus en échec, parce que le KICK n'a pas le timbre que son niveau voudrait

`analyse/mesure_h59.py` (`91450436`), `build-h42`, 66 s. **Écart au plan écrit** : passé pendant la course 2 sous
`nice` (quatre rendus de 19 s), comme H60 — le plan écartait une charge qui s'est révélée nulle. Volumes résolus :
**charleston 1,823 → 6,409** (× 3,5), **kick 1,823 → 0,939** (× 0,5), caisse claire 1,823 → 1,253. L'écart au stem
par bande, volume commun → résolu : 20-60 Hz −7,8 → **−13,5** ; 60-150 −4,6 → **−9,7** ; 150-500 +5,8 → +0,3 ;
500-2 000 +4,3 → +0,3 ; 2-6 kHz −0,9 → −0,2 ; 6-10 kHz −4,9 → +0,1 ; 10-16 kHz −6,8 → 0,0. Le kick du TR-808 a trop
de bas-médium et de médium pour son sub : les moindres carrés le baissent pour ces bandes-là, et son sub part avec.

| # | attendu | mesure | verdict |
|---|---|---|---|
| 1 | aigus \|écart\| ≤ 2 dB | −6,94 → **+2,50** (un gain de 4,4 dB) | **entre les deux** (au-delà de 2, au-delà du gain de 3 qui définissait l'échec) |
| 2 | sub et basse se rapprochent de 1,5 dB | sub −1,33 → **−3,80**, basse −3,15 → **−5,73** | **échec** : les deux s'éloignent |
| 3 | les quatre autres bandes : aucune ne s'éloigne de plus de 1 dB | bas-médium +2,72 → +1,66, médium +1,66 → +1,70, haut-médium −3,01 → +1,82 | **tenu** |
| 4 | log-mel ≤ témoin | **10,09 → 8,49** (médian 8,53 → 6,25) | **tenu** — le plus grand gain de ce chantier |
| 5 | contrôle : le calage du kick inchangé à 0,5 ms | \|médian\| 4,35 → 5,08, p90 8,71 → 10,16 | **échec du contrôle** : aucune note n'a bougé ; le détecteur d'attaques basses de l'outil lit autre chose quand le kick baisse de 6 dB — c'est l'outil qui bouge, et c'est dit |

Niveau : décalage −0,57 → −2,04 dB, pire tranche 5,43 → 7,60. **La règle écrite ne couvre pas le cas** (2 en échec) :
**rien n'entre**. Ce que la mesure établit : (a) **le charleston est trois fois et demie trop bas**, et c'est la
plus grande part des aigus et du haut-médium manquants — un partage de niveaux, pas un timbre ; (b) **le kick ne peut
pas être calé par son niveau** : son timbre (trop de 150-2 000 Hz pour son sub) fait que tout niveau qui rend l'un
fausse l'autre. D'où H63, le charleston seul.

## 24. H63 — le charleston seul à son niveau (écrite AVANT la mesure, 10/10/2026, 18 h 05)

**L'hypothèse** : le volume de la piste « Batterie · hihat » porté à celui que H59 a résolu (1,823 → 6,409), le kick
et la caisse claire laissés au volume commun, rapproche les aigus et le haut-médium de l'original sans rien coûter au
grave. **Une variable**, la mesure de H59 à l'identique (même témoin, même outil, mêmes options).

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | aigus | \|écart\| ≤ 3 dB (témoin 6,94) | gain de moins de 3 dB |
| 2 | sub, basse, bas-médium | aucune ne s'éloigne de plus de 0,5 dB | une s'éloigne de plus de 1 dB |
| 3 | log-mel | ≤ témoin − 0,5 dB | > témoin |
| 4 | contrôle : le calage du kick | inchangé à 0,5 ms près | — |

**La règle de décision** : 1, 2 et 3 tenus → le niveau de chaque piste d'un groupe de batterie se résout sur les
bandes du stem où CETTE piste domine (le charleston sur l'aigu), et l'entrée dans la chaîne est une hypothèse à elle,
mesurée sur un morceau du banc à une variable ; 3 tenu sans 2 → le charleston porte aussi du grave, à mesurer.

### 24.1 Verdict de H63 (10/10/2026, mesuré à 18 h 06) : TENUE — le charleston trois fois et demie plus fort ramène l'aigu et le haut-médium dans le seuil du § 0, sans toucher au grave, et le log-mel gagne 1,51 dB

Une variable, vérifiée : le volume de la piste 5 (« Batterie · hihat ») 1,823 → 6,409, le reste du projet identique ;
`build-h42`, même témoin, même outil, mêmes options.

| mesure (outil du § 0) | témoin | H63 |
|---|---|---|
| sub · basse · bas-médium | −1,33 · −3,15 · +2,72 | −1,45 · −3,21 · +2,50 |
| médium · haut-médium · **aigus** | +1,66 · −3,01 · **−6,94** | +0,87 · **+0,03** · **+0,66** |
| log-mel moyen (médian) | 10,09 (8,53) | **8,58 (6,55)** |
| niveau : décalage (pire tranche) | −0,57 (5,43) | −0,09 (6,01) |
| kick \|médian\| · p90 | 4,35 · 8,71 ms | 4,35 · 8,71 ms |

| # | attendu | verdict |
|---|---|---|
| 1 | aigus \|écart\| ≤ 3 dB | **tenu** : 0,66 — sous le seuil du § 0 lui-même (1 dB) |
| 2 | sub, basse, bas-médium : aucune ne s'éloigne de plus de 0,5 dB | **tenu** : +0,12, +0,06, −0,22 |
| 3 | log-mel ≤ témoin − 0,5 dB | **tenu** : −1,51 dB |
| 4 | contrôle : le calage du kick | **tenu** : identique au centième |

**H63 EST TENUE.** Pour la première fois dans ce chantier, **trois des six bandes tiennent le seuil du § 0** (médium
0,87, haut-médium 0,03, aigus 0,66, toutes ≤ 1 dB) ; le témoin n'en tenait aucune. Seule la pire tranche de niveau
recule (5,43 → 6,01 dB). **Le charleston était 3,5 fois trop bas** : le calage EN BLOC de la chaîne donnait aux trois
pistes de batterie le même volume, et la plus aiguë, la plus faible des trois en énergie, y perdait 10,9 dB.

**La règle écrite, appliquée** : « 1, 2 et 3 tenus → le niveau de chaque piste d'un groupe de batterie se résout sur
les bandes du stem où CETTE piste domine, et l'entrée dans la chaîne est une hypothèse à elle, mesurée sur un morceau
du banc à une variable ». **Ce qu'H59 a appris borne cette hypothèse-là** : résoudre TOUTES les pistes sur toutes les
bandes baisse le kick pour son médium et lui retire son sub ; la règle à écrire doit caler chaque piste sur SA bande
(le charleston sur l'aigu) et laisser une piste dont le timbre n'est pas celui du stem (le kick ici) à son niveau,
le dire, et ne pas la forcer. Cette hypothèse s'écrit avant son code ; elle n'entre pas aujourd'hui dans la chaîne.

## 25. H64 — dans la chaîne : chaque pièce d'une batterie calée sur SA bande, sous une garde (écrite AVANT son code, 10/10/2026, 18 h 08)

**Ce que la chaîne fait, lu dans `analyzer/vsm_levels.py`.** `_caler_un_groupe` rend chaque piste du groupe seule,
additionne, et applique à toutes le MÊME facteur (rms du stem / rms de la somme) : « l'équilibre interne — celui que
la détection ou le découpage ont trouvé — n'est pas touché ». Rien ne le mesure. Sur « Reload », il coûtait 10,9 dB
au charleston (H63).

**L'HYPOTHÈSE H64.** Après le facteur de groupe, pour un groupe de BATTERIE, chaque pièce reçoit un facteur propre :
celui qui égale l'énergie de la bande où elle DOMINE la somme des rendus (sa part ≥ 60 % de cette bande) à celle du
stem dans la même bande — **sous une garde** : le facteur est refusé, et le refus DIT, s'il éloignerait du stem de
plus de 3 dB une autre bande où la pièce porte au moins 25 % de la somme. Sur « Reload », la règle doit rendre H63 :
le charleston monté de son aigu, le kick laissé (son sub le monterait de 7,8 dB et éloignerait le bas-médium, qu'il
porte, de 5,8 à 13,6 dB — refusé, et dit).

**LES MESURES, dans l'ordre :**
1. **Reproduire ce qui l'a fait naître, sans en faire une preuve** : la règle, appliquée par une fonction pure aux
   rendus solo de la référence (ceux de H59), rend les facteurs — charleston ≈ × 3,5, kick et caisse claire refusés
   ou proches de 1 — et le projet qui en sort, mesuré au § 0, tient ce que H63 tenait. C'est un contrôle de la
   fonction, pas de l'idée : la règle a été écrite en regardant ce morceau.
2. **La preuve, ailleurs** : un morceau du banc à stems VRAIS (S2), la chaîne avec et sans l'option, une seule
   variable, mêmes stems, même moteur, métrique et budget identiques ; la distance globale et l'outil du § 0.

**ATTENDUS (mesure 2)** : la distance globale ≤ témoin (aucune dégradation de plus de 1 %) ; l'équilibre par bande
du groupe batterie plus proche du stem sur au moins la moitié des bandes, aucune ne s'éloignant de plus de 1 dB ; les
refus de la garde publiés pièce par pièce. **Échec** : une dégradation de plus de 3 % — la règle de « Reload » ne
vaut pas ailleurs. **Le code vit dans un arbre à part** tant que des courses tournent, et n'entre dans la chaîne que
par la mesure 2.

### 25.1 La mesure 1 de H64 (10/10/2026, 18 h 10) : le CONTRÔLE de la règle échoue sur le morceau qui l'a fait naître — dominer la somme des rendus n'est pas porter la bande dans l'original

`analyse/mesure_h64.py` (`b0474e3d` ; ses annotations corrigées après coup : mypy y voyait deux erreurs, passées au
premier commit sans que la ligne soit lue), les rendus solo de H59, `build-h42`, l'outil du § 0 :

| pièce | ce que la règle a fait |
|---|---|
| Batterie · hihat | « ne domine aucune bande (part maximale 36 %) » — **laissé à son niveau** |
| Batterie · kick+kick2 | × 0,91 sur 20 à 2 000 Hz |
| Batterie · snare | **× 1,52** sur 2 à 16 kHz |

Mesuré : sub −2,18 · basse −2,78 · bas-médium +2,10 · médium +1,24 · haut-médium −0,40 · aigus −5,15 ; **log-mel
10,59** (témoin 10,09, H63 8,58) ; kick 4,35 / 9,87 ms.

**La règle ne rend pas H63, et la raison est dans la règle, pas dans le code** : la domination se calcule sur la
somme des RENDUS. Dans la reconstruction, c'est le SOUFFLE de la caisse claire du TR-808 qui porte l'aigu (le
charleston, réglé court et à 0,7 dans le kit, n'en porte que 36 % au plus) ; la règle monte donc la caisse claire —
et son ton avec, d'où le grave — là où l'original demandait le charleston. Dire QUELLE PIÈCE porte une bande de
l'ORIGINAL demanderait de décomposer le stem par pièce, ce que la chaîne ne sait pas faire (un seul stem « drums »).
**H64 ne va pas au banc telle qu'écrite.** La fonction et ses tests restent, sans appel par la chaîne. Ce qui reste
établi est H63 lui-même — sur « Reload », le charleston était 3,5 fois trop bas — et la question qu'il pose est plus
précise que ma règle : « à quel niveau une pièce de batterie doit-elle sonner pour que ses FRAPPES ressemblent aux
frappes du stem au même instant ? » — une mesure aux instants de chaque pièce, pas sur des bandes du morceau entier.

## 26. H65 — le niveau d'une pièce de batterie mesuré à SES frappes isolées (écrite AVANT la mesure, 10/10/2026, 18 h 12)

**L'hypothèse** : le niveau juste d'une pièce de batterie se lit aux instants où elle frappe SEULE — aucune autre
pièce dans ± 60 ms, d'après le MIDI de la transcription —, en comparant, sur une fenêtre de 80 ms après chaque
frappe, l'énergie du stem « drums » à celle du rendu solo de la pièce (H59) ; le facteur d'amplitude est la
racine de la médiane des rapports d'énergie. Appliqué aux trois pièces de « Reload », il rapproche le morceau de
l'original autant que H63 au moins.

**Ce qui la distingue de H64** : elle n'a pas besoin de savoir qui porte une bande — elle compare des frappes à des
frappes, là où une seule pièce sonne dans l'original comme dans la reconstruction. **Ce qui peut la fausser, dit
avant** : le pad et la voix sont dans le mélange, pas dans le stem « drums » (le stem seul est comparé) ; une
frappe isolée DANS LA TRANSCRIPTION peut ne pas l'être dans l'original (une pièce que la transcription a ratée) —
la médiane s'en défend, sans le prouver ; et le kick, dont le timbre n'est pas celui du stem (H59), recevra un
facteur qui égale l'ÉNERGIE de ses frappes, pas leur forme.

**LA MESURE, une variable — les trois volumes de batterie, comme H59** : rendu `build-h42`, outil du § 0, même
témoin. Publiés : le nombre de frappes isolées par pièce, les rapports (médiane, quartiles), les facteurs.

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | le facteur du charleston | à ± 3 dB de celui que H63 a trouvé (× 3,5, soit +10,9 dB) | à plus de 6 dB : la mesure aux frappes ne voit pas ce que H63 a vu |
| 2 | log-mel | ≤ celui de H63 (8,58) + 0,3 dB | > témoin (10,09) |
| 3 | sub et basse | aucune ne s'éloigne de plus de 1 dB du témoin | une s'éloigne de plus de 2 dB |
| 4 | les frappes isolées | ≥ 30 par pièce | moins : la pièce n'est pas mesurable ainsi, et c'est dit |

**La règle de décision** : 1, 2 et 3 tenus → la mesure aux frappes isolées est la règle candidate pour la chaîne (le
calage interne d'un groupe de batterie), et elle va au banc par une hypothèse à elle ; 1 tenu, 2 ou 3 en échec → le
niveau de la pièce est juste, son timbre ne l'est pas (le kick) ; 1 en échec → l'idée ne voit pas le charleston.

### 26.1 Verdict de H65 (10/10/2026, mesuré à 18 h 13) : le meilleur résultat du chantier — log-mel 8,17, aigus à 0,38 dB — mais le filtre d'isolement n'a rien filtré, et la basse dépasse sa marge de 0,02 dB

`analyse/mesure_h65.py` (`6e1c5003`), rendus solo de H59, `build-h42`, outil du § 0, même témoin.

| pièce | frappes | isolées | rapport stem/rendu (médiane ; quartiles) | facteur |
|---|---|---|---|---|
| charleston | 1 692 | **1 692** | 9,596 ; 6,745 · 17,718 | **× 3,10 (+9,8 dB)** |
| kick | 555 | **555** | 0,970 ; 0,120 · 1,141 | × 0,985 (−0,1 dB) |
| caisse claire | 192 | **192** | 0,076 ; 0,009 · 0,193 | **× 0,276 (−11,2 dB)** |

| mesure (outil du § 0) | témoin | H63 | **H65** |
|---|---|---|---|
| sub · basse · bas-médium | −1,33 · −3,15 · +2,72 | −1,45 · −3,21 · +2,50 | −1,28 · −4,17 · +2,79 |
| médium · haut-médium · aigus | +1,66 · −3,01 · −6,94 | +0,87 · +0,03 · +0,66 | +1,63 · −1,61 · **−0,38** |
| log-mel moyen (médian) | 10,09 (8,53) | 8,58 (6,55) | **8,17 (6,03)** |
| niveau : décalage (pire tranche) | −0,57 (5,43) | −0,09 (6,01) | −0,81 (5,66) |
| kick \|médian\| · p90 | 4,35 · 8,71 | 4,35 · 8,71 | 4,35 · 8,71 |

| # | attendu | verdict |
|---|---|---|
| 1 | le charleston à ± 3 dB de H63 (+10,9 dB) | **tenu** : +9,8 dB |
| 2 | log-mel ≤ H63 + 0,3 | **tenu** : 8,17 contre 8,58 — le meilleur chiffre de ce chantier |
| 3 | sub et basse : aucune ne s'éloigne de plus de 1 dB | **entre les deux** : le sub se rapproche (−1,33 → −1,28), la basse s'éloigne de **1,02 dB** — 0,02 au-delà de la marge, sous les 2 dB de l'échec. C'est l'effet déjà mesuré par H62 (−1,19) : le ton de la caisse claire remplissait un peu de grave, et H65 la baisse de 11 dB |
| 4 | ≥ 30 frappes isolées par pièce | **tenu, et c'est un défaut de la mesure** : TOUTES les frappes sont « isolées » (1 692 sur 1 692, 555 sur 555, 192 sur 192) — la transcription ne met JAMAIS deux pièces à moins de 60 ms, elle donne un seul nom par attaque. Le filtre écrit pour se défendre des frappes simultanées n'a donc rien écarté : H65 a mesuré à CHAQUE frappe, et un kick frappé avec un charleston compte l'énergie du charleston dans le stem |

**La règle, à la lettre** : « 1, 2 et 3 tenus » ne l'est pas — 3 est entre les deux, de 0,02 dB, comme l'attendu 4 de
H42 dépassait sa marge de 0,01 dB et a été déclaré en échec. **H65 ne va pas au banc sur ce verdict.** Ce qu'elle
établit, chiffré : à leurs propres frappes, **le charleston est trois fois trop bas (+9,8 dB) et la caisse claire près
de quatre fois trop forte (−11,2 dB)** dans la reconstruction de référence ; corrigés ensemble, le morceau gagne
**1,92 dB de log-mel** et ses aigus passent de −6,94 à −0,38 dB. Ce qui reste à faire avant le banc : une mesure qui
sache ce qu'une frappe contient — la transcription ne le dit pas —, et le grave des pleines, que ni H59 ni H65 ne
touchent (le kick et la basse, § 21).

## 27. Le grave des pleines est la BASSE, calée en bloc avec le pad — et H66, la voix de basse à son niveau (relevé à 18 h 15, H66 écrite AVANT la mesure, 10/10/2026, 18 h 16)

**LE RELEVÉ.** Le kick n'en est pas la cause : sur les 77 kicks de la pleine 56-84 s, le stem a son fondamental à
86,1 Hz et le kick reconstruit à 80,7 Hz, et leurs enveloppes sous 150 Hz décroissent de même sur les 0,45 s entre
deux temps. La basse, elle (« other · voix 4 », un orgue à roue phonique, rendue SEULE par `build-h42`), contre le
stem « other », par section, sous 500 Hz (dB, même échelle) :

| section | stem other : sub · basse · bas-médium | voix 4 rendue : sub · basse · bas-médium |
|---|---|---|
| intro 0-14 s | −0,3 · −4,0 · 2,0 (silence) | **60,8 · 60,2** · 52,9 |
| pad 14-42 s | 54,4 · 52,9 · 93,4 | 46,4 · **61,8** · 70,3 |
| pleine 56-84 s | 56,8 · **71,2** · 89,8 | 43,5 · **59,2** · 66,8 |
| pleine 98-127 s | 63,6 · **76,6** · 87,4 | 56,8 · **64,6** · 68,4 |
| long pont 127-162 s | 57,3 · 62,4 · 95,1 | 48,3 · 62,9 · 71,7 |
| pleine 218-268 s | 67,2 · **78,6** · 88,3 | 63,8 · **70,4** · 72,9 |

Dans les pleines, la voix de basse est **8 à 12 dB sous le stem** entre 60 et 150 Hz. Ailleurs, elle joue là où le stem
n'a pas de basse (l'intro : soixante décibels sur un silence ; le pad : 9 dB au-dessus) — des notes de résidu, une
autre affaire. Les quatre voix d'« other » ont le MÊME volume (0,104) : le calage en bloc, encore — trois voix de pad
au-dessus de 233 Hz (MIDI 58 et plus) et une basse en dessous, qui y perd.

**L'HYPOTHÈSE H66** : le volume de « other · voix 4 » porté au rapport des énergies, sur le morceau entier, du stem
« other » et de son rendu solo SOUS 150 Hz — la bande où, dans le stem, elle est seule (les voix de pad commencent à
233 Hz) — rapproche le grave du morceau de l'original. **Ce qu'elle ne fait pas, dit avant** : les notes de résidu
de l'intro et du pad montent avec elle — un niveau unique ne peut pas corriger une voix qui joue où elle ne devrait
pas ; si elles l'emportent, c'est elles qu'il faudra attaquer.

**LA MESURE, une variable** (le volume de la piste 3), `build-h42`, l'outil du § 0, le témoin de toujours ; et, sur
les sections, la basse du mélange contre l'original.

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | basse (60-150 Hz) des trois pleines, mélange contre original | se rapproche d'au moins 3 dB | moins de 1 dB |
| 2 | sub et basse du § 0 (médianes) | se rapprochent toutes deux | l'une s'éloigne de plus de 1 dB |
| 3 | log-mel | ≤ témoin | > témoin + 0,3 dB |
| 4 | basse de l'intro et du pad, mélange contre original | — (un relevé : de combien les notes de résidu montent) | — |

### 27.1 Verdict de H66 (10/10/2026, mesuré à 18 h 17) : la basse montée de 12 dB ne bouge le grave des pleines que de 0,1 à 0,2 dB — ce grave est celui de la BATTERIE

Énergie sous 150 Hz, stem « other » / voix 4 rendue = 16,8 → **× 4,10 (+12,3 dB)**, volume 0,104 → 0,428 ; une
variable ; `build-h42` ; outil du § 0.

| section | sub, témoin → essai | basse, témoin → essai |
|---|---|---|
| pleine 56-84 s | −9,7 → −9,6 | −7,4 → −7,3 |
| pleine 98-127 s | −9,7 → −8,7 | −6,4 → −6,2 |
| pleine 218-268 s | −9,6 → −8,2 | −6,5 → −6,3 |
| intro (relevé) | +10,2 → **+19,3** | +31,1 → +31,7 |
| pad (relevé) | +0,4 → +3,2 | +24,4 → +25,0 |

Morceau entier : **sub −1,33 → −0,48**, basse −3,15 → −3,16, les autres bandes à 0,2 dB près ; **log-mel 10,09 → 9,62**
(médian 8,53 → 8,08) ; niveau −0,57 → −0,39 (pire tranche 5,43 → 5,67) ; le calage du kick lu 5,8 / 11,6 ms (l'outil,
comme sous H59, lit autrement ses attaques basses quand le grave change ; aucune note n'a bougé).

| # | verdict |
|---|---|
| 1 | **échec** : 0,1 à 0,2 dB de rapprochement en basse dans les pleines |
| 2 | **entre les deux** : le sub se rapproche de 0,85 dB, la basse ne bouge pas (+0,01) |
| 3 | **tenu** : −0,47 dB |
| 4 | relevé : les notes de résidu de l'intro montent avec elle — +9,1 dB de sub |

**CE QUE LE CHIFFRE DIT, et que le relevé du § 27 n'avait pas lu** : dans les pleines, l'original a **98 dB** entre 60
et 150 Hz au MÉLANGE, et le stem « other » n'en a que 71 à 79 — **le grave des pleines est dans le stem « drums »**, la
basse n'en est qu'une petite part, et la monter de 12 dB ne pouvait rien y faire. La part qui manque est donc celle
du kick — dont H65 a trouvé le NIVEAU juste à ses frappes (× 0,985) et H59 le TIMBRE faux (trop de 150-2 000 Hz pour
son sub). **La suite est le timbre du kick**, mesuré sur ses frappes contre celles du stem : un kick du TR-808 à son
accord et à sa décroissance actuels (`tune` 50, `decay` 0,05, choisis par une recherche sur le stem entier) contre
le fondamental de 86 Hz et la longue décroissance du § 1. **Rien n'entre de H66** : sa règle (« 1 en échec ») le dit.

## 28. H67 — le kick du TR-808 à l'accord et à la longueur du kick original (écrite AVANT la mesure, 10/10/2026, 18 h 18)

**Ce que le code et le projet disent.** Le kick du TR-808 (`KickVoice`) part à six fois son accord et y chute en
45 ms, puis s'éteint en `decay` secondes. Le projet de la référence l'a à **`tune` 50 Hz** (plage 30-90) et
**`decay` 0,05 s — le plus court possible** (plage 0,05-1,5) : réglés par une recherche sur le stem « drums » entier.
Un kick aussi bref est presque tout entier dans son balayage d'attaque (de 300 Hz vers 50) — le « trop de 150-2 000 Hz
pour son sub » de H59. L'original (§ 1, § 25.1) : un fondamental à **86 Hz** et une longue décroissance.

**L'HYPOTHÈSE H67** : le kick à l'accord de l'original (86 Hz) et à une décroissance plus longue rapproche le grave des
pleines de l'original. **Deux variables, mesurées UNE À UNE** : (a) `drum.kick.tune` 50 → 86, `decay` gardé à 0,05 ;
(b) à `tune` 86, `drum.kick.decay` balayé — 0,15 · 0,30 · 0,45 · 0,80 s — et **publié ENTIER**, pas à son meilleur
point. Seul le préréglage de la piste « Batterie · kick+kick2 » change ; `build-h42` ; l'outil du § 0 et le grave des
trois pleines, contre le même témoin.

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | (a) l'accord seul : sub et basse des pleines | se rapprochent | s'éloignent de plus de 1 dB |
| 2 | (b) le meilleur point du balayage : basse des trois pleines | se rapproche d'au moins 3 dB du témoin | moins de 1 dB |
| 3 | log-mel du meilleur point | ≤ témoin − 0,3 dB | > témoin |
| 4 | bas-médium du § 0 au meilleur point | ne s'éloigne pas de plus de 1 dB | s'éloigne de plus de 2 dB |

**La règle de décision** : 2, 3 et 4 tenus → la recherche de piste de batterie doit régler le kick sur SES frappes
(le timbre aux instants du kick, pas sur le stem entier), une hypothèse à elle ; 2 tenu sans 3 → le grave revient,
le timbre reste loin ; 2 en échec → ni l'accord ni la longueur du TR-808 ne font ce kick, et c'est une autre machine
(ou une nouvelle) qui est à mesurer.

### 28.1 Verdict de H67 (10/10/2026, mesuré de 18 h 19 à 18 h 22) : la longueur ramène le grave des pleines, l'accord à 86 Hz vide le sub — « le grave revient, le timbre reste loin »

Le balayage ENTIER (préréglage du kick seul, `build-h42`, outil du § 0) :

| réglage | basse des trois pleines (mélange − original) | sub · basse · bas-médium (§ 0) | log-mel |
|---|---|---|---|
| témoin (50 Hz, 0,05 s) | −7,4 · −6,4 · −6,5 | −1,33 · −3,15 · +2,72 | 10,09 |
| (a) 86 Hz, 0,05 s | −14,8 · −14,1 · −14,1 | −7,68 · −7,41 · +3,10 | 10,15 |
| (b) 86 Hz, 0,15 s | **−0,7 · −0,4 · −0,4** | −6,36 · −2,08 · +2,77 | **9,96** |
| (b) 86 Hz, 0,30 s | +4,2 · +4,3 · +4,4 | −5,45 · +0,43 · +2,10 | 9,99 |
| (b) 86 Hz, 0,45 s | +6,1 · +6,1 · +6,3 | −5,07 · +1,30 · +1,44 | 10,00 |
| (b) 86 Hz, 0,80 s | +7,9 · +7,8 · +8,0 | −4,70 · +2,06 · +0,84 | 10,01 |

| # | verdict |
|---|---|
| 1 | **échec** : l'accord seul éloigne la basse des pleines de 7 dB (le kick bref de 86 Hz a moins d'énergie encore sous 150 Hz) |
| 2 | **tenu** : à 0,15 s, la basse des pleines passe de −7,4 / −6,4 / −6,5 à −0,7 / −0,4 / −0,4 dB |
| 3 | **entre les deux** : log-mel 9,96, sous le témoin (10,09) mais au-dessus de la marge (9,79) |
| 4 | **tenu** : bas-médium +2,77 (+0,05) |

**La règle** : « 2 tenu sans 3 → le grave revient, le timbre reste loin ». Rien n'entre. **Ce que le balayage montre** :
la longueur du kick règle la basse des pleines de façon monotone (de −7 à +8 dB de 0,05 à 0,80 s), et 0,15 s la met
à moins d'un décibel ; mais l'accord à 86 Hz retire le SUB (20-60 Hz : −1,33 → −4,7 à −7,7 dB) que l'accord de 50 Hz
fournissait — le kick de l'original a donc du sub SOUS son fondamental de 86 Hz, ce qu'un TR-808 accordé à 86 Hz
n'a pas. La suite nommée : un balayage de l'accord à longueur 0,15 s, et le kick « kick+kick2 » qui est peut-être
DEUX sons superposés (le nom de la piste le dit) — à mesurer avant d'en faire une hypothèse.

## 29. H68 — l'accord du kick, à la longueur que H67 a trouvée (écrite AVANT la mesure, 10/10/2026, 23 h 26)

**L'hypothèse** : à `decay` 0,15 s (où la basse des pleines est à moins d'un décibel, H67), un accord plus bas que
86 Hz rend le sub que 86 Hz retirait, sans perdre la basse : le kick du TR-808 trouve un réglage où sub ET basse sont
proches de l'original. **Une variable** : `drum.kick.tune` balayé — 40 · 50 · 60 · 70 · 86 Hz —, `decay` 0,15 s,
**publié ENTIER** ; le préréglage du kick seul ; `build-h42` ; outil du § 0 et basse des trois pleines, même témoin.

| # | mesure (au meilleur point du balayage, choisi par le log-mel) | réussite | échec |
|---|---|---|---|
| 1 | sub du § 0 | \|écart\| ≤ celui du témoin (1,33) | au-delà de 3 dB |
| 2 | basse des trois pleines | à ± 2 dB de l'original | au-delà de 4 dB |
| 3 | log-mel | ≤ témoin − 0,3 dB (9,79) | > témoin |

**La règle de décision** : 1, 2, 3 tenus → le kick se règle sur SES frappes (l'accord et la longueur), et la recherche
de piste de batterie doit le faire d'elle-même — une hypothèse à elle, au banc ; 2 tenu sans 1 → le TR-808 n'a pas
le sub de ce kick à cette longueur, et le « kick+kick2 » (deux sons superposés ?) est à mesurer.


### 29.1 Verdict de H68 (11/10/2026, rendus le 10/10 de 23 h 27 à 23 h 30, rejoués à 00 h 33 au bit près, mesurés de 02 h 15 à 02 h 20, écrit à 02 h 22) : à la lettre « 2 tenu sans 1 » — mais le balayage entier dément la conclusion que la règle en tire : à 60 Hz, sub ET basse sont à l'original

**L'instrument d'abord.** Le calcul « basse des trois pleines » de H66 et H67, fait à la volée, ne se retrouvait pas à
0,1-0,5 dB près ; il vit désormais dans l'outil du § 0 (`--sections 56-84,98-127,218-268` : la médiane, sur les
tranches de 4 mesures dont le CENTRE tombe dans la section — 4, 4 et 7 —, de l'écart absolu par bande et de l'écart
relatif au total de la tranche). Contrôlé sur une perturbation connue (40 s de l'original contre lui-même à +6 dB :
absolu +6,0, relatif 0,0 sur les six bandes ; une section sans tranche est DITE, pas comptée zéro). Le témoin et tout
le balayage de H67 sont remesurés sous ce même instrument : le § 0 se retrouve au centième (sub −1,33, log-mel 10,09),
la basse des pleines à 0,5 dB près de ce que H67 publiait (témoin −7,5 · −6,4 · −6,5 contre −7,4 · −6,4 · −6,5 ;
(a) −15,3 · −14,6 · −14,6 contre −14,8 · −14,1 · −14,1) — aucun verdict de H67 ne change. Le point 86 Hz de H68 est
**identique au bit près** au point (b) 0,15 s de H67 : le script de H68 reproduit son prédécesseur.

Le balayage ENTIER, `build-h42` (moteur `52532381`), préréglage du kick seul, décroissance 0,15 s (sections : mélange
− original, dB absolus ; § 0 : médianes relatives) :

| réglage | basse des pleines | sub des pleines | bas-médium des pleines | § 0 sub · basse · bas-médium | niveau (pire) | log-mel |
|---|---|---|---|---|---|---|
| témoin (50 Hz, 0,05 s) | −7,5 · −6,4 · −6,5 | −9,7 · −9,0 · −8,9 | +2,7 · +3,4 · +2,9 | −1,33 · −3,15 · +2,72 | −0,57 (5,43) | 10,09 |
| 40 Hz | +1,2 · +2,4 · +2,3 | +11,2 · +11,4 · +11,4 | +3,4 · +4,1 · +3,6 | +8,49 · +1,51 · +0,39 | +2,55 (6,46) | 9,90 |
| **50 Hz** | **+1,0 · +1,9 · +1,9** | +8,4 · +8,4 · +8,4 | +4,6 · +5,4 · +5,0 | **+5,57** · +1,01 · +1,15 | +2,54 (6,45) | **9,86** |
| 60 Hz | +0,9 · +1,7 · +1,7 | +2,2 · +2,3 · +2,3 | +5,4 · +6,2 · +5,7 | +0,15 · +0,56 · +1,86 | +2,54 (6,44) | 9,89 |
| 70 Hz | +0,4 · +1,0 · +1,0 | −11,3 · −9,2 · −9,3 | +5,9 · +6,7 · +6,3 | −2,80 · −0,24 · +2,34 | +2,54 (6,44) | 9,88 |
| 86 Hz (= H67 b, 0,15 s) | −0,9 · −0,5 · −0,4 | −17,1 · −15,2 · −15,5 | +6,4 · +7,2 · +6,8 | −6,36 · −2,08 · +2,77 | +2,57 (6,47) | 9,96 |

Le meilleur point par le log-mel, comme la règle le veut : **50 Hz** (9,86).

| # | verdict à 50 Hz |
|---|---|
| 1 | **échec** : sub du § 0 à +5,57 dB, au-delà des 3 dB |
| 2 | **tenu** : basse des pleines +1,0 · +1,9 · +1,9, dans les ± 2 dB — de 0,1 dB |
| 3 | **entre les deux** : 9,86, sous le témoin (10,09) mais au-dessus de la marge (9,79) |

**La règle, à la lettre** : « 2 tenu sans 1 → le TR-808 n'a pas le sub de ce kick à cette longueur, et le
“kick+kick2” est à mesurer ». Rien n'entre (3 n'est tenu nulle part : 9,86 au mieux).

**CE QUE LE BALAYAGE ENTIER DIT, ET QUI DÉMENT LA CONCLUSION DE LA RÈGLE.** À **60 Hz**, le sub du § 0 est à
**+0,15 dB** et la basse des pleines à +0,9 · +1,7 · +1,7 : les attendus 1 et 2 y sont tenus ENSEMBLE. Le TR-808 A
donc le sub de ce kick à cette longueur. C'est le CHOIX du point qui l'a écarté : **le log-mel ne départage pas
l'accord** — 9,86 à 9,90 de 40 à 70 Hz, 0,04 dB d'étendue, quand le sub du § 0 y parcourt 11,3 dB. Un « meilleur
point » choisi par une mesure plate est choisi au hasard ; la leçon, pour toute règle de ce chantier : avant de
désigner un point par une mesure, montrer que cette mesure VARIE sur le balayage plus que d'un écart qu'on sache
interpréter. La seconde moitié de la branche est faite aussi, et ne trouve rien : la piste « kick+kick2 » ne porte
qu'**une** hauteur (MIDI 36, 555 frappes) ; son nom est celui de deux classes de la transcription fondues en une
piste, pas deux sons superposés dans le projet. Si le kick de l'ORIGINAL est fait de deux couches, le projet ne
permet pas de le lire.

**Ce que le balayage montre de plus.** (a) Le sub se règle par l'accord, avec un bord franc : dans les pleines, de
+2,2 dB à 60 Hz à −11,3 à 70 Hz — le fondamental du kick quitte la bande 20-60 Hz. (b) À 0,15 s, quel que soit
l'accord, le mélange est **3,1 dB plus fort** au total que le témoin (niveau −0,57 → +2,54 ; pire tranche 5,43 →
6,45), et le surplus est dans le **bas-médium** des pleines (+2,7-3,4 → +5,4-6,2 dB à 60 Hz) ; les aigus relatifs
du § 0 en perdent 1,8 dB (−6,94 → −8,72) sans que rien n'ait changé au-dessus de 6 kHz (−8,9 en absolu, avant comme
après) — la mesure relative suit le total. (c) Le code du kick (`audio/plugins/tr808/TR808Synth.h`, `KickVoice`)
dit d'où peut venir ce bas-médium, et aucune de ces sources n'est un paramètre : une SATURATION fixe
(`tanh(sinus × 1,8)`, qui ajoute les harmoniques impaires — 180 et 300 Hz pour un kick à 60 Hz) et un BALAYAGE
d'attaque fixe (départ à six fois l'accord, chute en 45 ms — de 360 Hz vers 60). La suite nommée : dire LAQUELLE
des deux porte le surplus de 150-500 Hz, ce qui demande d'abord de pouvoir les régler.

## 30. H69 — le kick de l'ORIGINAL, estimé par moyenne synchrone de ses frappes : balaye-t-il moins haut ou plus vite que celui du TR-808 ? (écrite AVANT la mesure, 11/10/2026, 02 h 26)

**Ce que le code dit, relu après H68.** Dans `KickVoice`, la fréquence vaut `accord × (1 + 5 e^(−t/0,045))` : 45 ms
est une CONSTANTE DE TEMPS (`DecayEnvelope` : e⁻¹ au bout de la durée), pas la durée du balayage. Accordé à 60 Hz, le
kick passe 300 Hz à 10 ms, 232 Hz à 25 ms, 159 Hz à 50 ms et 92 Hz à 100 ms ; il reste au-dessus de 150 Hz pendant
**54 ms**, et, sous une amplitude qui décroît en 0,15 s, y dépense **54 %** de son énergie sur ses 300 premières ms
(à 0,05 s : 91 %) — chiffres d'une réplique du code en numpy, coupure à 150 Hz par un Butterworth d'ordre 4. C'est le
premier suspect du surplus de bas-médium que H68 a mesuré ; le second est la saturation fixe (`tanh(sinus × 1,8)`,
harmoniques 3 et 5 — 180 et 300 Hz à 60 Hz), mais la même réplique la dit faible : sans elle, la part au-dessus de
150 Hz passe de 54 à 52 %. Aucun des deux n'est un paramètre.

**L'instrument.** Un kick de production est, le plus souvent, le MÊME échantillon à chaque frappe. Moyennées après
alignement, ses frappes dans le stem « drums » rendent sa forme d'onde, et ce qui ne frappe pas avec lui s'y efface
(√N : 200 frappes, 23 dB). Les frappes retenues : la piste « kick+kick2 » de la référence, dans les trois pleines,
sans caisse claire à moins de 150 ms, sans autre kick dans les 300 ms qui suivent (le compte est publié) ; alignées
sur une référence par corrélation à ± 20 ms (la transcription en est à 4 ms près au médian, 9 à 12 ms au p90),
filtrées sous 1 kHz. Du kick moyen : sa fréquence instantanée (phase du signal analytique, 30-600 Hz) à 10, 25, 50,
100 et 200 ms ; sa part d'énergie au-dessus de 150 Hz sur ses 300 premières ms ; et un ajustement du modèle du code
(`f(t) = f₁ (1 + r e^(−t/τ))`, amplitude en e^(−t/τₐ)). **Le même instrument, appliqué au rendu solo de la piste du
kick** (60 Hz, 0,15 s, `build-h42`), sert de contrôle : sa réponse est connue d'avance par le code.

**L'HYPOTHÈSE H69** : le kick de l'original balaye moins haut ou plus vite que celui du TR-808 réglé comme H68 l'a
trouvé (60 Hz, 0,15 s) — sa part d'énergie au-dessus de 150 Hz est plus basse, et sa fréquence passe sous 150 Hz
plus tôt.

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | CONTRÔLE : l'instrument sur le rendu solo du TR-808 | fréquence à 10, 25, 50, 100 ms à ± 10 % de la formule du code (300 · 232 · 159 · 92 Hz) ; part au-dessus de 150 Hz à ± 1 dB de celle d'une réplique du code passée par le même instrument | l'un ou l'autre hors marge : l'instrument ne lit pas un balayage connu, rien ne se conclut de l'original |
| 2 | STABILITÉ : les frappes paires et impaires moyennées à part | corrélation ≥ 0,9 sous 1 kHz sur les 300 ms | < 0,7 : ce kick n'est pas un échantillon répété (ou l'alignement échoue), sa moyenne ne le représente pas |
| 3 | part d'énergie au-dessus de 150 Hz, original contre TR-808 (60 Hz, 0,15 s) | au moins 3 dB plus basse dans l'original | à moins de 1 dB : le balayage n'est pas la cause, la saturation l'est peut-être |
| 4 | instant où la fréquence passe sous 150 Hz | original < 30 ms (TR-808 : 54 ms) | original ≥ 54 ms |
| 5 | relevé : `f₁`, `r`, `τ`, `τₐ` ajustés sur l'original | — (ce que H70 donnerait au TR-808) | — |

**La règle de décision** : 1 et 2 tenus, 3 et 4 tenus → le TR-808 reçoit deux paramètres, la profondeur (`r`) et la
constante (`τ`) de son balayage, leurs défauts aux valeurs d'aujourd'hui (5 et 0,045 s — un rendu de la référence
identique au bit près le prouve), et H70 les règle sur l'original, mesurée au mélange ; 1 et 2 tenus, 3 en échec →
la saturation est le suspect suivant, et c'est elle qui devient un paramètre ; 1 ou 2 en échec → rien ne se conclut
de cet instrument.

### 30.1 Ce que les tests de l'instrument ont changé — AVANT toute mesure de l'original (11/10/2026, 02 h 30)

`analyse/mesure_h69.py`, `analyse/tests/test_mesure_h69.py` (4 tests) : un kick de forme CONNUE (la réplique du
TR-808 à 55 Hz, r = 3, τ = 20 ms, τₐ = 0,2 s — d'autres réglages que ceux de « Reload »), posé 95 fois à des
instants connus dans un fond de bruit et d'une note de basse non synchrone, les instants donnés à l'instrument faux
de ± 8 ms. Trois changements, chacun vu ROUGE avant :

1. **Le début du kick se lit sur le signal redressé, pas sur l'enveloppe de Hilbert** : le noyau de Hilbert (1/πt)
   étale l'enveloppe AVANT l'attaque, et le début était lu 5 ms trop tôt (183 Hz lus à « 10 ms » pour 155).
2. **La fréquence se lit par périodes entre passages par zéro, pas par la phase du signal analytique** : sur un
   sinus SATURÉ (le `tanh` du TR-808), la phase tourne inégalement dans la période, et un lissage plus court qu'une
   période lisait 60,3 Hz pour 68,5 à 50 ms. La saturation garde le signe, donc les passages du fondamental.
3. **L'instant « sous 150 Hz » se lit sur le modèle ajusté** : la fréquence brute, lue dans le bruit d'avant
   l'attaque, le donnait à 2,5 ms.

Et une condition ajoutée à la sélection, sans test qui l'ait demandée : **aucun autre kick dans les 300 ms
QUI PRÉCÈDENT** (le § 30 ne gardait que celles qui suivent). La traîne d'un kick précédent est grave ; dans la
moyenne de l'original, elle baisserait la part au-dessus de 150 Hz — un biais EN FAVEUR de l'hypothèse — alors que
le TR-808, monophonique, coupe sa traîne à chaque frappe. Frappes retenues : 138 au lieu de 211.

Sur la scène connue, l'instrument rend : f(10, 25, 50, 100, 200 ms) = 156,3 · 103,2 · 69,1 · 56,3 · 55,0 Hz pour
155,1 · 102,3 · 68,5 · 56,1 · 55,0 ; f₁ 55,01 Hz, r 3,014, τ 20,1 ms, τₐ 0,200 s ; corrélation paires/impaires
0,9995 ; sur du bruit seul, sans kick, elle tombe sous 0,7 (le contrôle de l'attendu 2) ; et deux balayages
différents (r 5, τ 45 ms contre r 2, τ 10 ms) sont lus à plus de 3 dB l'un de l'autre au-dessus de 150 Hz (le
contrôle de l'attendu 3).

### 30.2 Verdict de H69 (11/10/2026, rendu et mesuré à 02 h 31, écrit à 02 h 33) : 1 à 4 tenus — et la règle prescrit des paramètres inutiles : le balayage de l'original EST celui du TR-808, vers 37 Hz, sous une amplitude tenue puis coupée

`reconstruction/travail/reload-h69.sh` (rejouable) : le rendu solo de la piste du kick à 60 Hz / 0,15 s par
`build-h42` (moteur `52532381`), puis `analyse/mesure_h69.py`. Frappes : 555 ; 241 hors des pleines, 2 près d'une
caisse claire, 175 avec un kick voisin à moins de 300 ms ; **137 retenues** (138 au § 30.1 : le décompte d'alors ne
demandait pas que les 300 ms de la fenêtre tiennent dans la section).

| | fréquence à 10 · 25 · 50 · 100 · 200 ms (Hz) | sous 150 Hz | part > 150 Hz | paires/impaires |
|---|---|---|---|---|
| formule du code (60 Hz) | 300,2 · 232,1 · 158,8 · 92,5 · 63,5 | 54,2 ms | — | — |
| réplique du code | 300,9 · 232,6 · 159,1 · 92,7 · 63,6 | 54,3 ms | 0,531 (−2,75 dB) | — |
| rendu solo du TR-808 | 301,0 · 232,7 · 159,1 · 92,7 · 63,6 | 54,3 ms | 0,530 (−2,75 dB) | 1,0 |
| **original (stem « drums »)** | 185,4 · 139,6 · 91,7 · 60,0 · *164,6* | 16,3 ms (modèle) ; 15-18 ms (lu) | **0,167 (−7,78 dB)** | **0,9996** |

| # | verdict |
|---|---|
| 1 | **tenu** : le rendu solo à 0,3 % de la formule aux quatre instants, sa part au-dessus de 150 Hz à 0,01 dB de la réplique ; décalages d'alignement nuls (médiane −0,02 ms) |
| 2 | **tenu** : 0,9996 — le kick de l'original est un échantillon répété |
| 3 | **tenu** : −7,78 contre −2,75 dB, **5,0 dB plus bas** dans l'original |
| 4 | **tenu** : 145,5 Hz à 17,8 ms (169,9 à 14,9), 16,3 ms par le modèle ; le TR-808 : 54 ms |
| 5 | **relevé tel qu'écrit, INUTILISABLE** : sur 5-200 ms, f₁ 104,9 Hz, r 1,52, τ 12,9 ms, τₐ 0,099 s, résidu 0,39 en log (le rendu : 0,0005). La fenêtre de 200 ms prend des périodes où le kick n'est plus : la valeur à 200 ms (164,6 Hz) en est une |

**CE QUE LA TRAJECTOIRE DIT, LUE PÉRIODE PAR PÉRIODE.** De 4 à 15 ms, une attaque irrégulière de 160 à 210 Hz ; puis
un glissement régulier — 145,5 Hz à 18 ms, 127,0 à 29, 112,1 à 37, 95,6 à 47, 82,8 à 58, 72,8 à 71, 61,3 à 86,
57,7 à 94 ms — sous une amplitude **PLATE** (−4,5 dB en moyenne de 5 à 80 ms, écart-type 1,5 dB), qui tombe
d'un coup : −6 dB à 81 ms, −10 à 90, −20 à 127 ms. Après 100 ms, ce que l'instrument lit n'est plus le kick (13 à
20 dB plus bas, des fréquences de 70 à 180 Hz sans suite) ; 9,6 % seulement de l'énergie des 300 ms est au-delà de
100 ms.

**Un ajustement A POSTERIORI — décidé après avoir vu la trajectoire, donc un relevé et non une preuve —, borné à la
zone où le kick domine** (l'enveloppe à moins de 10 dB de son maximum) :

| fenêtre | périodes | f₁ | r | départ | τ | résidu (log) |
|---|---|---|---|---|---|---|
| 5-86 ms | 18 | 36,0 Hz | 4,89 | 212 Hz | 44,3 ms | 0,064 |
| 15-86 ms | 14 | 39,3 Hz | 4,41 | 213 Hz | 42,2 ms | 0,021 |
| 5-95 ms | 19 | 37,1 Hz | 4,73 | 213 Hz | 43,6 ms | 0,062 |

**Le balayage de l'original a la forme de celui du TR-808** — r 4,4 à 4,9 pour 5, τ 42 à 44 ms pour 45 —, **vers
36 à 39 Hz**, pas vers 60. **La règle, à la lettre** (« 1 et 2 tenus, 3 et 4 tenus → le TR-808 reçoit la profondeur et
la constante de son balayage ») donnerait au TR-808 deux paramètres que l'original règle à leurs valeurs d'aujourd'hui :
**elle ne s'applique pas**, et c'est écrit plutôt que fait. Sa prémisse — « l'original balaye moins haut ou plus
vite » — est vraie dans le chiffre (3 et 4 tenus), fausse dans la cause : la part au-dessus de 150 Hz est plus basse
parce que le balayage descend vers 37 Hz et passe 150 Hz à 19 ms au lieu de 54, pas parce qu'il a une autre forme.

**Ce que cela explique de H68.** À 40 Hz et 0,15 s, H68 trouvait 8,5 dB de sub en trop : le TR-808 accordé bas
continue de sonner à 40 Hz des centaines de millisecondes, quand le kick de l'original est COUPÉ à 85-90 ms, à 58 Hz,
avant d'avoir atteint le sub. Ce qui sépare les deux kicks n'est pas le balayage, c'est **l'enveloppe d'amplitude** :
tenue ~80 ms puis coupée, là où `KickVoice` ne sait que décroître en exponentielle depuis l'attaque.

**La suite, nommée et pas encore écrite en hypothèse** : un paramètre de TENUE de l'amplitude du kick (défaut 0, le
son d'aujourd'hui au bit près), le kick réglé sur le relevé (≈ 37 Hz, tenue ≈ 80 ms, la décroissance la plus courte),
mesuré au mélange contre le témoin — H70.

## 31. H70 — le kick du TR-808 réglé sur SES frappes : l'accord relevé, une TENUE d'amplitude, le niveau de la frappe moyenne (écrite AVANT son code et sa mesure, 11/10/2026, 02 h 36)

**Ce qui manque au TR-808, d'après H69** : l'amplitude de `KickVoice` décroît en exponentielle dès l'attaque ; celle
du kick de l'original est TENUE ~80 ms puis coupée. **Le code, dans un arbre à part** (`reload-h70`, worktree
`../vsm-studio-h70`, son propre `vsm-render` construit sans l'application) : un paramètre « Kick Hold »
(`drum.kick.hold`, 0 à 0,3 s, **défaut 0**) pendant lequel l'enveloppe d'amplitude reste à 1 avant de décroître ;
à 0, le chemin de calcul est celui d'aujourd'hui. **Sans règle au profil de recherche** : la chaîne ne le cherche
pas (`buildSearchProfile` ne garde que les paramètres qui ont une règle) — il ne change aucune reconstruction tant
qu'une hypothèse ne l'y fait pas entrer. Le panneau du TR-808 reçoit son bouton (la garde
`no_parameter_is_silently_unreachable` l'exige) ; **il n'entre dans `master` — photo de l'interface et bancs à
l'appui — que si la règle ci-dessous le dit.**

**Le contrôle d'abord** : le témoin (le projet de référence tel quel) rendu par le nouveau `vsm-render` doit être
**identique au bit près** à `reload-h42/temoin.wav` (rendu par `build-h42`). Identique → le paramètre à 0 ne change
rien, et les rendus de H67 et H68 restent valables ; différent → le témoin de H70 est le rendu du nouveau moteur, et
c'est dit, avec l'écart.

**L'HYPOTHÈSE H70** : un kick réglé sur ses frappes par l'instrument de H69, SANS regarder le mélange, rapproche le
mélange de l'original — grave ET bas-médium. Le réglage, entièrement fixé avant toute mesure du mélange :
- **accord 37 Hz** (le relevé du § 30.2 : f₁ 36,0 à 39,3 Hz), balayage inchangé (r 5, τ 45 ms : ceux de l'original
  à 0,5 et 3 ms près), **décroissance 0,05 s** (le minimum de sa plage : la coupure de l'original est plus vive) ;
- **tenue** : celle de {0,04 · 0,06 · 0,08 · 0,10 s} dont le kick moyen du rendu solo a l'enveloppe la plus proche
  de celle de l'original (enveloppe de Hilbert 30-600 Hz depuis le début de chacun, en dB sous son maximum, plancher
  à −20 dB, écart quadratique moyen sur 5-130 ms) ;
- **niveau** : le volume de la piste multiplié par `√(E_original / E_rendu)`, énergies des deux kicks moyens sous
  1 kHz sur leurs 150 premières ms — la frappe moyenne à l'énergie de celle du stem (la méthode de H65, sur la frappe
  moyenne au lieu de la médiane des frappes).

Mesuré au mélange : ce point « K » contre le témoin ; en relevés, pour décomposer, K sans le niveau (« K₀ »), et
37 Hz / 0,05 s sans tenue (« A ») ; le point 60 Hz / 0,15 s de H68 sert de repère pour le bas-médium.

| # | mesure (au point K) | réussite | échec |
|---|---|---|---|
| 1 | le kick, sur ses frappes (rendu solo, instrument de H69) | part au-dessus de 150 Hz à ± 1,5 dB de celle de l'original (−7,78 dB) ; fréquence à 25, 50, 100 ms à ± 10 % de l'original (139,6 · 91,7 · 60,0 Hz) | part à plus de 3 dB : le TR-808, même tenu, ne fait pas ce kick |
| 2 | le grave et le bas-médium du mélange | sub du § 0 \|écart\| ≤ 1,33 dB (le témoin) ; basse des trois pleines à ± 2 dB ; bas-médium des trois pleines au moins 2 dB plus près de l'original qu'au point 60 Hz / 0,15 s de H68 (+5,4 · +6,2 · +5,7) | sub au-delà de 3 dB, ou basse au-delà de 4, ou bas-médium pas plus près que H68 |
| 3 | log-mel | ≤ témoin − 0,3 dB (9,79) | > témoin (10,09) |
| 4 | relevés : niveau du § 0, les six bandes, K₀ et A | — | — |

**La règle de décision** : 1, 2 et 3 tenus → « Kick Hold » entre dans `master` (son bouton photographié, les bancs
rejoués), et la chaîne reçoit une hypothèse à elle : régler le kick d'une batterie sur ses frappes par moyenne
synchrone ; 1 tenu, 2 ou 3 en échec → le kick est juste sur ses frappes et le mélange ne suit pas : ce qui l'éloigne
est ailleurs (les autres pièces, leurs niveaux — H65) ; 1 en échec → le TR-808 tenu ne fait pas ce kick, et c'est
une autre machine (le kick de l'original est un échantillon répété : un échantillonneur le jouerait tel quel).

### 31.1 L'instrument de H70 et le code du paramètre, AVANT toute mesure (11/10/2026, 02 h 39)

`analyse/mesure_h70.py` (sur `mesure_h69.py`) et `analyse/tests/test_mesure_h70.py` : un « original » connu (la
réplique du TR-808 à 37 Hz, tenue 70 ms) contre des rendus à 0 · 40 · 70 · 100 ms de tenue et à moitié du niveau →
la tenue choisie est 70 ms (écart d'enveloppe < 0,5 dB, sans tenue > 3 dB), le facteur de niveau ×2,00. **Vu rouge
en mutant l'instrument** : une fenêtre d'enveloppe arrêtée à 30 ms (aveugle à toute tenue de 40 ms ou plus) fait
tomber le test (écart sans tenue 2,7 dB). **Ce que ces tests ne gardent PAS** : le recalage du début de chaque kick
moyen — un instrument qui part du début de la fenêtre reste vert, parce que le test pose l'original et les rendus au
même instant ; dans « Reload », le début de l'original est lu 5,2 ms avant celui du rendu (§ 30.2), et le recalage
y compte.

Dans l'arbre `reload-h70` : « Kick Hold » (`kKickHold`, ajouté APRÈS `kAccent` pour que les identifiants des
réglages enregistrés ne bougent pas), sa ligne au descripteur (`drum.kick.hold`), son bouton au panneau (« HOLD »,
sous « TONE »), et deux tests C++ : la tenue garde l'amplitude à moins de 0,5 dB jusqu'à 75 ms puis la décroissance
reprend (< −15 dB 100 ms après), sans tenue −11 dB à 65 ms ; et à 0 — le défaut — le rendu est identique au bit près
à celui d'un TR-808 où l'on n'a rien posé.

### 31.2 Verdict de H70 (11/10/2026, rendu et mesuré de 02 h 43 à 02 h 47, écrit à 02 h 49) : rien n'est tenu en entier, rien n'entre — mais les trois bandes graves des pleines sont, pour la première fois, à moins de 1,5 dB de l'original, et le log-mel ne le voit pas

`reconstruction/travail/reload-h70.sh` (rejouable), moteur de `reload-h70` (`1eff3b6d`, branche poussée).

**Le contrôle : tenu.** Le témoin rendu par le nouveau moteur est **identique au bit près** à celui de `build-h42` :
« Kick Hold » à 0 ne change rien, et les rendus de H67 et H68 restent comparables à ceux-ci.

**Le choix sur les frappes, à la lettre** : écart d'enveloppe 3,09 · 4,44 · 6,33 · 8,00 dB pour 0,04 · 0,06 · 0,08 ·
0,10 s → **tenue 0,04 s**, au BORD du balayage ; niveau `√(E_o/E_r)` = **×0,7075 (−3,01 dB)**. **Un défaut de
l'instrument, vu sur les frappes AVANT de lire le mélange** : l'enveloppe est rapportée à son MAXIMUM, et celui de
l'original est le clic de l'attaque, **4,3 dB au-dessus de son plateau** — le rendu tenu, plat à son maximum, est
pénalisé d'autant plus longtemps qu'il est tenu. Rapportées à leur forme seule (décalage constant retiré), les
mêmes enveloppes s'écartent de 2,64 · **2,48** · 3,46 · 4,82 dB : le choix aurait été 0,06 s, voisin. Et aucune
tenue ne colle bien, pour une raison de la machine : la décroissance du TR-808 ne descend pas sous 0,05 s, quand la
coupure de l'original (−6 dB à 81 ms, −20 à 127) a une constante d'environ 25 ms. Le point K a été rendu à la lettre
(0,04 s) ; 0,06 s n'est pas rendu.

| point | basse des pleines | sub des pleines | bas-médium des pleines | § 0, \|médian\| sub · basse · bas-méd. · médium | haut-méd. · aigus (médian) | niveau (pire) | log-mel (médian) |
|---|---|---|---|---|---|---|---|
| témoin | −7,5 · −6,4 · −6,5 | −9,7 · −9,0 · −8,9 | +2,7 · +3,4 · +2,9 | 7,63 · 6,12 · 2,80 · 1,89 | −3,01 · −6,94 | −0,57 (5,43) | 10,09 (8,53) |
| H68, 60 Hz / 0,15 s | +0,9 · +1,7 · +1,7 | +2,2 · +2,3 · +2,3 | +5,4 · +6,2 · +5,7 | 1,23 · 1,86 · 2,09 · 1,39 | −4,05 · −8,72 | +2,54 (6,44) | 9,89 (8,39) |
| **K** (37 Hz, 0,05 s, tenue 0,04 s, ×0,7075) | **−1,3 · 0,0 · −0,1** | **+0,6 · +1,3 · +1,2** | **+0,9 · +1,5 · +1,0** | **1,51 · 1,47 · 1,31 · 1,21** | −3,46 · −7,26 | **−0,23 (5,17)** | 10,09 (8,44) |
| K₀ (sans le niveau) | +1,7 · +3,0 · +2,9 | +3,6 · +4,3 · +4,2 | +3,5 · +4,2 · +3,7 | 1,62 · 2,18 · 1,22 · 1,48 | −3,93 · −8,61 | +2,19 (6,09) | 10,10 (8,45) |
| A (37 Hz, 0,05 s, sans tenue) | −4,5 · −3,2 · −3,3 | −2,8 · −2,2 · −2,2 | +1,6 · +2,3 · +1,8 | 1,57 · 3,03 · 2,05 · 1,98 | −3,01 · −6,95 | −0,57 (5,43) | 10,07 (8,43) |

| # | verdict au point K |
|---|---|
| 1 | **entre les deux** : fréquence à 25 · 50 · 100 ms = 143,4 · 98,2 · 57,3 Hz, à 2,7 · 7,1 · 4,5 % de l'original (tenu) ; part au-dessus de 150 Hz −5,04 dB contre −7,78 — **2,74 dB**, au-delà des ± 1,5 dB, en deçà des 3 dB de l'échec (à 0,08 s, elle aurait été à −7,37 : la tenue choisie par l'enveloppe et celle que demande la part au-dessus de 150 Hz ne sont pas les mêmes, et c'est la décroissance trop lente qui les sépare) |
| 2 | **entre les deux** : basse des pleines dans les ± 2 dB (tenu) ; bas-médium des pleines **4,5 à 4,7 dB** plus près que H68 (tenu) ; sub du § 0 à **+1,51**, 0,18 dB au-delà du témoin (\|−1,33\|) — mais son écart absolu médian, tranche à tranche, passe de 7,63 à 1,51 dB |
| 3 | **entre les deux** : 10,09, égal au témoin au centième publié — ni 0,3 dB dessous, ni au-dessus |

**La règle** : rien n'est tenu en entier, **« Kick Hold » n'entre pas dans `master`** ; il reste dans sa branche.

**CE QUE LA MESURE MONTRE.** (a) **La moitié basse du spectre est réparée** : sur le morceau entier, l'écart absolu
médian tranche à tranche des bandes sub, basse, bas-médium et médium passe de 7,63 · 6,12 · 2,80 · 1,89 dB à
**1,51 · 1,47 · 1,31 · 1,21** ; dans les pleines, les trois bandes graves sont à moins de 1,5 dB ; le niveau se
rapproche (−0,57 → −0,23 dB) et la pire tranche aussi (5,43 → 5,17). La décomposition : l'accord seul (A) divise
par deux les écarts du sub et de la basse sans toucher au niveau ; la tenue (K₀) amène la basse et le bas-médium
mais rend le morceau 2,2 dB trop fort ; le niveau de la frappe moyenne (K) le ramène — sans que le mélange ait
servi à le choisir. (b) **Le log-mel ne voit pas le kick** : 10,09 au témoin comme en K, et de 9,86 à 10,15 pour
TOUS les réglages de kick essayés depuis H67. L'attendu 3 a été écrit sur une mesure que la variable ne pouvait pas
faire bouger — H68 l'avait montré, et je l'ai recopié. La leçon, pour toute hypothèse qui touche une seule pièce :
son critère doit être de ceux que cette pièce peut faire bouger (ici, les bandes qu'elle porte), et le log-mel se
garde comme CONTRÔLE (« ne recule pas »), pas comme critère de réussite. (c) **Ce qui reste au log-mel est dans la
moitié haute** — haut-médium −3,46 et aigus −7,26 en K, inchangés : le territoire de H63 et H65 (le charleston trois
fois trop bas, la caisse claire près de quatre fois trop forte), où H65 avait mené le log-mel à 8,17.

## 32. H71 — les deux moitiés du spectre se corrigent-elles l'une sans l'autre ? Le kick de H70 et les niveaux de H65, ensemble (écrite AVANT la mesure, 11/10/2026, 02 h 50)

**Ce que H65 et H70 ont fait, chacun de son côté.** H65 (les niveaux du charleston et de la caisse claire, lus à
leurs frappes) a réparé la moitié HAUTE — aigus −6,94 → −0,38, log-mel 10,09 → 8,17 — sans toucher au grave ; H70
(le kick réglé sur ses frappes) a réparé la moitié BASSE — \|médian\| sub · basse · bas-médium · médium 7,63 · 6,12 ·
2,80 · 1,89 → 1,51 · 1,47 · 1,31 · 1,21 — sans toucher au haut ni au log-mel. Ni l'une ni l'autre n'est entrée.

**L'HYPOTHÈSE H71** : les deux corrections sont INDÉPENDANTES — appliquées ensemble, chacune garde ce qu'elle a fait
seule. **Le point mesuré** : le projet de référence, la piste du kick réglée comme le point K de H70 (37 Hz, 0,05 s,
tenue 0,04 s, volume ×0,7075), celles du charleston et de la caisse claire à leurs facteurs de H65 — **repris en
rejouant `mesure_h65.py` tel quel** (`build-h42`, mêmes solos de H59, même stem), parce que H65 ne les a publiés qu'à
trois chiffres ; le rejeu doit rendre un essai **identique au bit près** à celui de H65 (sinon c'est dit, et les
facteurs du rejeu servent). Le facteur du kick de H65 (×0,985) n'est PAS appliqué : celui de K le remplace. Rendu
par le moteur de `reload-h70` ; l'outil du § 0, ses sections.

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | le CONTRÔLE : le log-mel | ≤ celui de H65 + 0,3 dB (8,47) — le kick ne défait pas ce que H65 a gagné | > H65 + 1,0 dB (9,17) |
| 2 | la moitié basse : \|médian\| sub, basse, bas-médium, médium | chacune à moins de 0,5 dB de celle de K (1,51 · 1,47 · 1,31 · 1,21), ou mieux | l'une pire de plus de 1,5 dB |
| 3 | la moitié haute : médianes du haut-médium et des aigus | à moins de 0,5 dB de celles de H65 (−1,61 · −0,38), ou plus près de 0 | l'une plus loin de plus de 1,5 dB |
| 4 | relevés : niveau, pire tranche, les pleines par bande | — | — |

**La règle de décision** : 1, 2 et 3 tenus → les deux corrections sont indépendantes ; ce point devient le **repère**
du chantier (« la référence H71 »), contre lequel se mesure ce qui reste — et chacune va à la chaîne par son propre
chemin (le niveau d'une pièce à ses frappes, une fois l'instrument de H65 capable de dire ce qu'une frappe contient ;
le kick réglé sur ses frappes, une fois la tenue entrée dans `master`) ; 2 ou 3 en échec → elles interagissent, et
la bande qui recule dit laquelle gêne l'autre.

### 32.1 Verdict de H71 (11/10/2026, mesuré de 02 h 50 à 02 h 52, écrit à 02 h 53) : 1 et 2 tenus, 3 à 0,05 dB de sa marge — et en absolu, la moitié haute n'a pas bougé d'un dixième : les deux corrections sont indépendantes

`reconstruction/travail/reload-h71.sh` (rejouable). **Le rejeu de H65** (`build-h42`) rend un essai **identique au
bit près** au sien ; ses facteurs exacts : charleston ×3,097806, caisse claire ×0,276083 (le kick, ×0,985134, non
appliqué). Le point H71 rendu par le moteur de `reload-h70` (`1eff3b6d`).

| mesure (outil du § 0) | témoin | H65 | K (H70) | **H71** |
|---|---|---|---|---|
| \|médian\| sub · basse · bas-médium · médium | 7,63 · 6,12 · 2,80 · 1,89 | — | 1,51 · 1,47 · 1,31 · 1,21 | **1,30 · 1,44 · 1,13 · 1,54** |
| médianes haut-médium · aigus | −3,01 · −6,94 | −1,61 · −0,38 | −3,46 · −7,26 | −2,16 · −0,68 |
| log-mel moyen (médian) | 10,09 (8,53) | 8,17 (6,03) | 10,09 (8,44) | **8,16 (5,98)** |
| niveau : décalage (pire tranche) | −0,57 (5,43) | −0,81 (5,66) | −0,23 (5,17) | −0,24 (**5,09**) |

| # | verdict |
|---|---|
| 1 | **tenu** : log-mel 8,16, sous H65 + 0,3 (8,47) — et 0,01 dB sous H65 lui-même |
| 2 | **tenu** : 1,30 · 1,44 · 1,13 · 1,54 contre 1,51 · 1,47 · 1,31 · 1,21 — trois bandes mieux, le médium pire de 0,33 dB, dans la marge |
| 3 | **entre les deux, à la lettre** : aigus −0,68 (0,30 plus loin que H65, dans la marge) ; haut-médium **−2,16 contre −1,61 : 0,55 dB, 0,05 au-delà de la marge** |

**La règle, à la lettre** : « 1, 2 et 3 tenus » ne l'est pas, de 0,05 dB — comme H65 l'a manqué de 0,02 dB. Le point
n'est pas déclaré « référence H71 ».

**CE QUE L'ABSOLU DIT DE L'ATTENDU 3.** L'essai de H65, mesuré après coup par sections (`h65-essai-sections.json`),
contre H71, écart absolu mélange − original dans les trois pleines :

| bande | H65 | H71 |
|---|---|---|
| haut-médium | −1,9 · −3,1 · −2,9 | −1,9 · −3,1 · −2,9 |
| aigus | +0,3 · −0,5 · −0,2 | +0,3 · −0,5 · −0,2 |
| médium | +1,8 · −0,8 · −2,6 | +1,4 · −1,5 · −3,3 |
| basse | −7,7 · −6,5 · −6,6 | −1,3 · −0,0 · −0,2 |
| sub | −9,8 · −9,2 · −9,1 | +0,6 · +1,3 · +1,2 |

**La moitié haute est identique au dixième.** Le recul du haut-médium relatif est celui du TOTAL : le kick rend au
morceau son grave, le total monte de 0,57 dB (niveau −0,81 → −0,24), et toute bande relative au total descend
d'autant. C'est l'effet que le § 29.1 avait nommé (« la mesure relative suit le total ») — et que j'ai oublié en
écrivant l'attendu 3 sur une médiane relative pour une hypothèse qui change le total. **La leçon, la troisième de
la nuit sur le même thème** : le critère d'une hypothèse se choisit parmi les mesures que sa variable peut faire
bouger (§ 31.2) ET que les autres variables ne font pas bouger ; une bande qu'on veut « inchangée » se juge en
absolu. **Les deux corrections sont indépendantes** dans les mesures qui le disent (le haut absolu, le bas relatif),
et H71 est, chiffré, le meilleur état mesuré du chantier : log-mel 8,16, pire tranche 5,09 dB, les quatre bandes
basses à moins de 1,6 dB d'écart médian. Ce qui reste, dans les pleines : le haut-médium (−1,9 à −3,1 dB absolus)
et le médium des deux dernières (−1,5 et −3,3).

## 33. Le log-mel de H71 décomposé : la batterie a l'ÉNERGIE du stem et pas sa DENSITÉ — et H72, la longueur du charleston réglée sur ses frappes (relevé de 02 h 54 à 02 h 57, H72 écrite AVANT sa mesure, 11/10/2026, 02 h 58)

**LE RELEVÉ, sans hypothèse** (la règle : décomposer un chiffre avant de l'expliquer). Le log-mel de H71 (8,16 dB),
signé (reconstruction − original), par groupe de bandes mel (64 bandes jusqu'à 11 kHz : **2 sous 150 Hz, 19 au-dessus
de 2 kHz** — d'où la cécité au kick des §§ 31.2 et 32.1) :

| bandes | 0-150 Hz | 150-500 | 500-2 000 | 2-4 kHz | 4-8 kHz | 8-11 kHz |
|---|---|---|---|---|---|---|
| bandes mel | 2 | 7 | 23 | 13 | 13 | 6 |
| \|écart\| moyen (dB) | 10,62 | 12,06 | 6,94 | 7,84 | 7,91 | 8,65 |
| signé | −0,88 | −7,18 | −2,25 | −4,02 | −4,44 | −4,78 |
| part du total | 4,1 % | 16,2 % | 30,6 % | 19,5 % | 19,7 % | 9,9 % |

Par section, l'écart signé est de **−5 à −8 dB dans les trois pleines et dans 162-211 s** (dans toutes les bandes),
quand l'intro et la fin sont au-dessus (+4). Or le niveau du § 0 est juste (−0,24 dB) : l'énergie y est, trame à trame
elle n'y est pas. Dans les pleines, l'original n'est que batterie (stem à −15 dB) et « other » (−23 dB) ; voix, guitare
et piano y sont à −52 dB. Rendues chacune SEULE (`reload-h71/parties/`, moteur de `reload-h70`) contre leur stem, dans
les pleines : « other » a 10 à 11 dB de trop peu sous 500 Hz et 2 à 6 dB de trop au-dessus ; **la batterie est 9 à
11 dB sous son stem en log-mel dans cinq groupes sur six — sur les trames fortes comme sur les faibles — avec la même
énergie à 1 dB près** (sauf 2-4 kHz, −5,3 dB, et 150-500 Hz, +2,8). La distribution des niveaux trame à trame (STFT
de 46 ms, pleines) dit pourquoi :

| bande | énergie, rendu − stem | stem : centiles 10 · 50 · 90 · 99 (dB) | rendu : centiles 10 · 50 · 90 · 99 |
|---|---|---|---|
| 30-150 Hz | −0,2 | 17,2 · 28,7 · 42,3 · 43,9 | −8,2 · 19,8 · 41,1 · 44,3 |
| 150-500 Hz | +2,8 | 4,2 · 18,5 · 30,1 · 43,4 | −19,8 · 9,2 · 39,4 · 44,5 |
| 500-2 000 Hz | −0,7 | 2,5 · 11,6 · 20,5 · 27,1 | −9,1 · 6,6 · 19,9 · 27,6 |
| 2-4 kHz | −5,3 | 11,1 · 16,7 · 24,8 · 27,2 | −20,3 · 8,3 · 19,6 · 23,6 |
| 4-8 kHz | −0,7 | **13,2 · 23,4 · 25,9 · 27,3** | **−14,1 · 15,3 · 27,4 · 31,6** |
| 8-11 kHz | −0,4 | 7,5 · 19,6 · 23,4 · 24,4 | −18,3 · 12,2 · 24,9 · 29,1 |

**La batterie de l'original est DENSE et celle du rendu est CREUSE** : entre les frappes, le rendu retombe 20 à 30 dB
sous le stem (10ᵉ centile), et compense par des crêtes plus hautes (99ᵉ). Entre 4 et 8 kHz, le stem est presque
continu — 14 dB du 10ᵉ au 99ᵉ centile, contre 46 dans le rendu.

**Le premier suspect, lu dans le code et le projet** : le charleston du TR-808 (`HatVoice` : six carrés inharmoniques
passés au-dessus de 7 kHz) décroît avec une constante de **0,05 s** (`drum.closedHat.decay`, plage 0,02-0,2 s) ; il
frappe sur la double croche (intervalle médian 128 ms, 105 ms au 10ᵉ centile ; 107 ms à 139,7 BPM) et retombe de
18,6 dB avant la suivante.

**L'HYPOTHÈSE H72** : le charleston de l'original est plus LONG ; réglé sur ses frappes — sa décroissance choisie sur
l'enveloppe de sa frappe moyenne, son niveau sur l'énergie de celle-ci, dans SA bande —, le charleston du TR-808
rend à la batterie sa densité, et le log-mel — qui, lui, voit les aigus — le dit. **Le réglage, fixé sur les frappes
avant toute mesure du mélange** : l'instrument de H69-H70 rapporté au charleston — frappes de la piste « hihat »
dans les pleines, sans caisse claire de 150 ms avant à 100 ms après ; moyenne synchrone au-dessus de 4 kHz (le
charleston du TR-808 est passé à 7 kHz) ; l'enveloppe comparée en FORME (décalage constant retiré — le défaut du
§ 31.2 corrigé), sur 5-100 ms (avant la double croche suivante), aux rendus solo de la piste à
`closedHat.decay` ∈ {0,05 · 0,08 · 0,12 · 0,16 · 0,20 s} ; le volume de la piste au rapport des énergies des deux
frappes moyennes sur leurs 100 premières ms. **Le point mesuré** : H71 (§ 32) avec ce charleston — une variable,
la piste du charleston (sa décroissance et son volume) ; même moteur, même outil, mêmes sections.

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | la densité : 10ᵉ centile de la batterie seule contre son stem, pleines, 4-8 kHz et 8-11 kHz | l'écart (−27,3 et −25,8 dB en H71) réduit de moitié au moins | réduit de moins de 5 dB : la longueur du charleston n'est pas ce qui manque |
| 2 | log-mel du mélange | ≤ H71 − 0,5 dB (7,66) | > H71 (8,16) |
| 3 | le CONTRÔLE de la moitié basse : \|médian\| sub, basse, bas-médium | chacune à moins de 0,5 dB de H71 (1,30 · 1,44 · 1,13), ou mieux | l'une pire de plus de 1,5 dB |
| 4 | relevés : haut-médium et aigus en ABSOLU dans les pleines, niveau, la décroissance et le facteur choisis | — | — |

**La règle de décision** : 1, 2, 3 tenus → le charleston se règle sur ses frappes comme le kick, et la chaîne reçoit
une hypothèse à elle (la longueur et le niveau des pièces de batterie à leurs frappes moyennes) ; 1 tenu sans 2 → la
densité revient sans que le log-mel suive : ce qu'il voit est ailleurs (le 2-4 kHz, la caisse claire ?) ; 1 en échec
→ le charleston n'est pas ce qui manque — un son continu de l'original (une boucle, une réverbération) que la
transcription ne donne pas, à chercher sur la frappe moyenne et entre les frappes.

### 33.1 L'instrument de H72, AVANT toute mesure de l'original (11/10/2026, 03 h 01)

`analyse/mesure_h72.py` et `analyse/tests/test_mesure_h72.py` (4 tests). **Ce que H69 ne pouvait pas faire ici** : le
charleston du TR-808 laisse tourner ses six oscillateurs d'une frappe à l'autre (`HatVoice` ne remet pas leurs
phases à zéro), et un charleston d'échantillon est surtout du bruit : moyennées en ONDE, ses frappes s'annuleraient.
L'instrument moyenne donc leur ÉNERGIE — l'enveloppe de puissance au-dessus de 4 kHz, lissée sur 2 ms, chaque frappe
recalée à ± 20 ms sur la moyenne par corrélation —, compare les formes en écart-type de leur différence (un
décalage constant, c'est-à-dire un niveau, n'y compte plus : le défaut du § 31.2), et relève la DENSITÉ par les
centiles des niveaux trame à trame. Sur une scène connue (des charlestons de bruit, une graine par frappe, à 0,12 s,
sur une grille de double croche, instants faux de ± 6 ms, un fond à 80 Hz) : la décroissance choisie est 0,12 s parmi
cinq, le facteur de niveau ×2 à 0,1 près, la constante relevée sur la forme 0,12 s à 10 % près ; un charleston bref
contre un long creuse le 10ᵉ centile de plus de 15 dB. **Vu rouge** : la bande descendue à 20 Hz (le fond grave entre
dans l'enveloppe) fait choisir 0,20 s. **Resté vert, et pourquoi** : une fenêtre de forme réduite à 10 ms — sur un
signal sans bruit, dix millisecondes de pente suffisent à séparer les cinq décroissances ; ce n'est pas un défaut,
mais ce test ne garde pas la longueur de la fenêtre.

### 33.2 Verdict de H72 (11/10/2026, mesuré de 03 h 03 à 03 h 04, écrit à 03 h 06) : TENUE — la batterie retrouve sa densité, le log-mel passe de 8,16 à 7,08 ; et le charleston de l'original est plus long que tout ce que le charleston fermé du TR-808 sait faire

`reconstruction/travail/reload-h72.sh` (rejouable), moteur de `reload-h70` (`1eff3b6d`). Frappes : 1 692 ; 1 201 hors
des pleines, 25 près d'une caisse claire ; **466 retenues** (11 recalées en butée de ± 20 ms).

**Le choix sur les frappes** : écart de forme 3,53 · 1,97 · 1,14 · 1,45 · **0,94** dB pour une décroissance de 0,05 ·
0,08 · 0,12 · 0,16 · **0,20 s** → 0,20 s, **le bord de la plage** du charleston fermé (0,02-0,2 s) ; facteur de niveau
×0,489 (−6,21 dB : un charleston plus long porte plus d'énergie par frappe), volume de la piste 5,6474 → 2,7616.
Constante relevée sur la forme (pente de 10 à 60 ms) : **0,436 s pour l'original**, 0,050 · 0,080 · 0,120 · 0,164 ·
0,207 s pour les rendus — l'instrument relit les leurs à 4 % près ; celle de l'original est deux fois plus longue
que le plus long charleston fermé (et la moyenne d'une grille de double croche y mêle la traîne de la frappe
précédente : c'est une borne haute).

| densité de la batterie seule contre son stem, pleines : écart des centiles 10 · 50 · 90 · 99 (dB) | H71 | **H72** |
|---|---|---|
| 500-2 000 Hz | −11,5 · −4,9 · −0,6 · +0,5 | −1,1 · −5,7 · −0,6 · +0,5 |
| 2-4 kHz | −31,4 · −8,4 · −5,2 · −3,6 | −4,8 · −3,6 · −7,5 · −8,3 |
| 4-8 kHz | −27,3 · −8,1 · +1,5 · +4,3 | **+1,0 · −2,6 · −0,6 · −0,4** |
| 8-11 kHz | −25,8 · −7,4 · +1,5 · +4,7 | **+3,8 · −1,5 · −0,9 · +0,1** |
| énergie de la bande 4-8 · 8-11 kHz, rendu − stem | −0,7 · −0,4 | −1,5 · −1,3 |

| mesure (outil du § 0) | H71 | **H72** |
|---|---|---|
| log-mel moyen (médian) | 8,16 (5,98) | **7,08 (5,35)** |
| \|médian\| sub · basse · bas-médium · médium | 1,30 · 1,44 · 1,13 · 1,54 | 1,35 · 1,39 · 1,09 · 1,62 |
| médianes haut-médium · aigus | −2,16 · −0,68 | −3,09 · −1,67 |
| haut-médium des pleines, absolu | −1,9 · −3,1 · −2,9 | −2,9 · −3,8 · −3,5 |
| aigus des pleines, absolu | +0,3 · −0,5 · −0,2 | −0,8 · −1,2 · −1,0 |
| niveau : décalage (pire tranche) | −0,24 (5,09) | −0,38 (**4,71**) |

| # | verdict |
|---|---|
| 1 | **tenu** : le 10ᵉ centile à 4-8 kHz passe de −27,3 à +1,0 dB, à 8-11 kHz de −25,8 à +3,8 — l'écart fermé, pas seulement réduit de moitié |
| 2 | **tenu** : log-mel 7,08, sous H71 − 0,5 (7,66) — le plus grand gain d'une pièce seule depuis H65 |
| 3 | **tenu** : 1,35 · 1,39 · 1,09, à 0,05 dB au plus de H71 |

**La règle** : « 1, 2, 3 tenus → le charleston se règle sur ses frappes comme le kick, et la chaîne reçoit une
hypothèse à elle (la longueur et le niveau des pièces de batterie à leurs frappes moyennes) ». **H72 est TENUE.**

**Ce qu'elle ne règle pas, et qui se voit dans la même mesure.** (a) **Le charleston est trop COURT pour la machine** :
choisi au bord de sa plage, il reste deux fois plus bref que celui de l'original ; le charleston OUVERT du TR-808
(0,1-1 s, passé à 5 kHz) en aurait la longueur. (b) **Le haut du spectre a perdu 0,6 à 1,1 dB** dans les pleines, en
absolu : le niveau choisi sur les 100 premières ms de la frappe moyenne laisse la bande du charleston 1,3 à 1,5 dB
sous le stem, et le 2-4 kHz reste à −6,1 dB d'énergie — le charleston du TR-808, passé à 7 kHz, n'y joue pas, quand
celui de l'original y a de la matière (son 2-4 kHz est aussi dense que son 4-8). (c) **Un défaut de l'instrument
trouvé à la lecture** : le champ « énergie » de la densité rendait +inf (`10 ** (er / 10).sum()` élevait 10 à la
SOMME des niveaux) ; corrigé, un test le garde (vu rouge sur l'ancien code), les deux densités rejouées — les
centiles, qui jugent l'attendu 1, n'en dépendaient pas.

## 34. H73 — la méthode de H72 sur un SECOND morceau : le charleston de « B4 Wuz Then » réglé sur ses frappes (écrite AVANT toute mesure de ce morceau, 11/10/2026, 03 h 08)

**Pourquoi avant la chaîne.** La règle de H72 envoie la méthode à la chaîne. Une course complète coûte de deux à huit
heures par morceau (campagne S2) ; or l'étape visée agit APRÈS la batterie, sur un projet déjà construit : elle se
juge sur un projet que la chaîne a produit, avec et sans elle, contre l'original — sans rejouer la chaîne. Et un
réglage trouvé sur UN morceau, regardé pendant douze hypothèses, n'est pas une règle : il faut le voir valoir
ailleurs AVANT d'en faire du code de chaîne.

**Le morceau** : « B4 Wuz Then », le projet de la chaîne `reconstruction/travail/b4wuzthen/` (commit `021f9cb`,
batterie éclatée par pièce, un TR-808 : charleston 892 frappes à `closedHat.decay` 0,05 s, kick 763, caisse claire
555, clap 474 — tous au même volume, 1,545), l'original `reconstruction/sources/b4-wuz-then.wav` (354,08 s). Le stem
« drums » de l'instrument : `reconstruction/travail/b4/drums.wav` (htdemucs, la famille du modèle de la chaîne),
choisi ici avant de le regarder. Rien de ce morceau n'a servi à écrire H69-H72.

**La méthode, celle de H72 sans rien y changer** (instrument `mesure_h72.py`, mêmes constantes) : frappes de la piste
« hihat » sur le morceau entier, sans caisse claire NI clap de 150 ms avant à 100 ms après (ce morceau a un clap, que
« Reload » n'avait pas : la seule adaptation, écrite ici) ; décroissance du charleston fermé choisie parmi
{0,05 · 0,08 · 0,12 · 0,16 · 0,20 s} sur la forme de la frappe moyenne au-dessus de 4 kHz ; volume de la piste au
rapport des énergies. **Une variable** : la piste du charleston (sa décroissance et son volume). Témoin et essai
rendus par le MÊME moteur (`reload-h70`, « Kick Hold » à 0 partout : celui de `master` au bit près), à 354,08 s ; outil
du § 0 sans `--tempo` (tranches de 7 s) pour les deux ; densité de la batterie seule (les quatre pistes) contre le stem
sur le morceau entier.

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | la densité : écart du 10ᵉ centile à 4-8 kHz, batterie seule contre stem | réduit de moitié au moins — ou, s'il est déjà sous 6 dB au témoin, la prémisse ne tient pas et c'est dit | réduit de moins de 3 dB alors qu'il dépassait 6 |
| 2 | log-mel du mélange | ≤ témoin − 0,3 dB | > témoin |
| 3 | le CONTRÔLE : \|médian\| sub, basse, bas-médium | chacune à moins de 0,5 dB du témoin | l'une pire de plus de 1,5 dB |

**La règle de décision** : 1, 2, 3 tenus → la méthode vaut sur un morceau qu'elle n'a pas vu : elle devient une étape
de la chaîne (une option, éteinte par défaut, mesurée par une course) ; 2 en échec → elle ne valait que pour
« Reload », et c'est écrit ; prémisse absente (1) → ce morceau n'a pas le défaut, et rien ne se conclut de lui.

### 34.1 Verdict de H73 (11/10/2026, mesuré de 03 h 09 à 03 h 11, écrit à 03 h 12) : PARTIELLE — la méthode choisit juste sur un morceau qu'elle n'a pas vu, mais ses gains restent sous les seuils écrits ; rien n'entre

`reconstruction/travail/reload-h73.sh` (rejouable), moteur de `reload-h70` (`1eff3b6d`). Frappes du charleston : 892,
dont **471 près d'une caisse claire ou d'un clap**, **421 retenues** (22 recalées en butée).

**Le choix** : écart de forme **0,93** · 1,57 · 2,47 · 2,94 · 3,59 dB pour 0,05 · 0,08 · 0,12 · 0,16 · 0,20 s →
**0,05 s, la décroissance que le projet avait déjà** ; constante relevée sur l'original **0,056 s** (0,436 sur
« Reload »). L'instrument ne tire pas vers le long par habitude : il lit un charleston bref et le garde bref. Le
niveau : **×1,809 (+5,15 dB)** — le charleston de ce projet était trop bas, comme celui de « Reload » avant H65.
L'essai ne change donc que le volume de la piste.

| mesure | témoin | essai |
|---|---|---|
| densité, 10ᵉ centile rendu − stem : 2-4 · 4-8 · 8-11 kHz | −15,4 · −15,2 · −13,0 | −11,7 · −11,2 · −9,8 |
| énergie de la bande 4-8 · 8-11 kHz, rendu − stem | −0,3 · −0,9 | +0,4 · +0,6 |
| log-mel moyen (médian) | 8,57 (6,88) | **8,37 (6,62)** |
| \|médian\| sub · basse · bas-médium | 3,25 · 1,30 · 1,02 | 3,23 · 1,31 · 1,02 |
| aigus : médiane (\|médian\|) | −1,26 (3,47) | **−0,38 (1,57)** |
| haut-médium : médiane (\|médian\|) | −1,50 (3,33) | −1,40 (3,28) |
| niveau : décalage (pire tranche) | −0,49 (10,65) | −0,47 (10,64) |

| # | verdict |
|---|---|
| 1 | **entre les deux** : la prémisse tient (−15,2 dB au témoin, au-delà de 6) ; l'écart se réduit de 4,0 dB — pas de moitié (il fallait −7,6), plus que les 3 dB de l'échec |
| 2 | **entre les deux** : −0,20 dB, quand la réussite en demandait 0,3 ; le log-mel ne recule pas |
| 3 | **tenu** : à 0,02 dB près |

**La règle, à la lettre** : ni « 1, 2, 3 tenus », ni « 2 en échec » — **rien n'entre dans la chaîne sur ce verdict**.

**CE QUE LE SECOND MORCEAU DIT.** (a) La méthode est **sûre** sur un morceau qu'elle n'a pas vu : elle y choisit la
décroissance que l'original demande (bref ici, long sur « Reload »), et son niveau rapproche les aigus de moitié
(\|médian\| 3,47 → 1,57) sans toucher au grave. (b) Son **gain dépend du défaut** que le morceau porte : sur « Reload »,
le charleston manquait de LONGUEUR (H72 : −1,08 dB de log-mel) ; ici, seulement de NIVEAU (−0,20). (c) Ce qui reste
de creux entre les frappes de « B4 » n'est pas le charleston : le 10ᵉ centile reste à −10 à −12 dB dans les trois
bandes hautes après lui, et le 2-4 kHz y est aussi creux que le 4-8 — d'autres pièces (la caisse claire et le clap,
1 029 frappes) ont leur part. **La suite nommée** : la même méthode étendue à TOUTES les pièces (chacune dans sa
bande, son niveau et sa longueur à ses frappes moyennes), jugée sur ces deux morceaux — une hypothèse à elle, avec
des seuils écrits pour un gain qui dépend du défaut du morceau.

## 35. H74 — toutes les pièces de batterie à leurs frappes, résolues ENSEMBLE ; le charleston à sa longueur (écrite AVANT son instrument et sa mesure, 11/10/2026, 03 h 13)

**Ce que H59, H64 et H65 n'ont pas su faire, et ce que la moyenne permet.** H59 résolvait les volumes sur les bandes
du morceau ENTIER (le kick y prenait le grave de tout le monde) ; H64 donnait chaque bande à la pièce qui DOMINE les
rendus (et la caisse claire du TR-808 volait l'aigu du charleston) ; H65 comparait chaque frappe au stem en large
bande (et un kick frappé avec un charleston comptait le charleston). Aux INSTANTS d'une pièce et dans SA bande, le
stem contient cette pièce ET ses voisines habituelles — et le rendu de la batterie entière aussi, aux mêmes
instants. D'où un système : pour chaque pièce *i*, sur ses frappes (fenêtres de −10 à +100 ms) et dans sa bande
principale *bᵢ* (celle des six du § 33 où son rendu solo a le plus d'énergie), l'énergie moyenne du stem vaut
`Σⱼ xⱼ · Eⱼ(i)`, où `Eⱼ(i)` est celle du rendu solo de la pièce *j* dans la même bande aux mêmes fenêtres et
`xⱼ = gⱼ²` le carré de son facteur de volume. *N* pièces, *N* équations, *N* inconnues positives : **moindres carrés
positifs** (en erreur relative, chaque équation divisée par son terme de stem), facteurs bornés à ×¼-×4 et toute
borne atteinte DITE. Les énergies s'additionnent si les pièces ne sont pas cohérentes entre elles — une hypothèse de
l'instrument, nommée.

**L'HYPOTHÈSE H74** : sur « Reload » et « B4 Wuz Then », la batterie de la chaîne réglée ainsi — le charleston d'abord
à sa longueur (la méthode de H72, inchangée), puis les volumes de toutes ses pièces résolus ensemble — rapproche
chaque mélange de son original plus que chacune des corrections partielles déjà mesurées. **Le point mesuré**, sur
chaque morceau : le projet de la chaîne tel quel (le témoin) contre le même projet aux pistes de batterie ainsi
réglées ; le kick garde son préréglage (la tenue de H70 n'est pas dans `master`) — seul son volume est résolu. Même
moteur pour tous (`reload-h70`, tenue à 0) ; outil du § 0 comme aux §§ 33-34 (« Reload » : `--tempo 136` et ses
sections ; « B4 » : tranches de 7 s).

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | log-mel, « Reload » | ≤ celui de H65 (8,17), qui ne réglait que les niveaux | > témoin (10,09) |
| 2 | log-mel, « B4 Wuz Then » | ≤ celui de H73 (8,37), qui ne réglait que le charleston | > témoin (8,57) |
| 3 | aigus : \|médian\| sur les deux morceaux | plus près que leur témoin | plus loin sur l'un |
| 4 | le CONTRÔLE : \|médian\| sub, basse, bas-médium sur les deux | aucune pire de plus de 1 dB | l'une pire de plus de 2 dB |
| 5 | relevés : les bandes principales, les facteurs, les bornes atteintes, le résidu du système | — | — |

**La règle de décision** : 1 à 4 tenus → l'étape entre dans la chaîne comme OPTION, éteinte par défaut, mesurée par
une course complète de « Reload » avant de devenir un défaut ; 3 et 4 tenus, 1 ou 2 non → l'instrument rapproche le
spectre sans battre les corrections partielles : la résolution conjointe n'apporte rien de plus que H65 et H72
chacun de son côté, et c'est dit ; 4 en échec → la résolution conjointe déplace de l'énergie entre pièces au prix du
grave : l'hypothèse d'additivité ne tient pas.

### 35.1 L'instrument de H74, AVANT toute mesure (11/10/2026, 04 h 39)

`analyse/mesure_h74.py` et `analyse/tests/test_mesure_h74.py` (3 tests). Trois pièces synthétiques de gains connus
(×1 · ×2 · ×0,5) qui frappent souvent ensemble, rendues chacune au gain 1 avec d'autres graines de bruit : les gains
retrouvés à 1 % près (0,998 · 1,984 · 0,498), résidu < 0,05, aucune borne. **Deux corrections du TEST, dites.**
(1) L'attendu des bandes principales, écrit de tête (« 30-150 · 500-2 000 · 4-8 kHz »), était faux — la réplique du
kick dépense plus de la moitié de son énergie au-dessus de 150 Hz (H69), et un bruit blanc remplit d'abord la bande
la plus large en hertz : 150-500 · 2-4 kHz · 8-11 kHz, que l'instrument lisait. (2) **Le test ne gardait pas ce qui
fait la méthode** : sur sa scène, un solveur SANS termes croisés (chaque pièce seule contre le stem, H65 en somme)
passait aussi. Une scène où la caisse est LARGE BANDE et frappe avec le charleston le sépare : pièce par pièce, le
charleston est lu ×1,48 pour ×0,5 ; résolu ensemble, ×0,52. Ce test-là est vu rouge sur le mutant. Une borne
atteinte (×8 demandé) est ramenée à ×4 et dite.

### 35.2 Verdict de H74 (11/10/2026, mesuré de 04 h 39 à 04 h 42, écrit à 04 h 43) : 3 et 4 en ÉCHEC — caler une pièce sur UNE bande déplace toutes les autres là où son timbre n'est pas celui de l'original

`reconstruction/travail/reload-h74.sh` (rejouable), moteur de `reload-h70` (`1eff3b6d`) ; la décroissance du charleston
reprise de H72 (0,20 s) et H73 (0,05 s). Les systèmes, résidu nul sur les deux morceaux :

| morceau | pièce | bande principale (de son rendu) | rendu / stem à ses frappes, avant | facteur |
|---|---|---|---|---|
| Reload | charleston | 4-8 kHz | 0,53 | ×1,926 (+5,7 dB) |
| Reload | kick | **150-500 Hz** | 3,54 | **×0,533 (−5,5 dB)** |
| Reload | caisse claire | 150-500 Hz | 94,4 | ×0,25 — **borne** (×0,091 demandé), dit |
| B4 | charleston | 4-8 kHz | 0,27 | ×2,035 (+6,2 dB) |
| B4 | kick | 30-150 Hz | 1,49 | ×0,819 (−1,7 dB) |
| B4 | caisse claire | **150-500 Hz** | 0,35 | **×1,781 (+5,0 dB)** |
| B4 | clap | 500-2 000 Hz | 15,8 | ×0,25 — **borne** (×0,232 demandé), dit |

| mesure | Reload : témoin → H74 (H65 ; H72) | B4 : témoin → H74 (H73) |
|---|---|---|
| log-mel moyen | 10,09 → **7,10** (8,17 ; 7,08) | 8,57 → 8,43 (8,37) |
| aigus : médiane (\|médian\|) | −6,94 (6,94) → +2,23 (3,06) | −1,26 (3,47) → **+4,13 (4,25)** |
| \|médian\| sub · basse · bas-médium | 7,63 · 6,12 · 2,80 → 9,14 · **8,17** · 2,40 | 3,25 · 1,30 · 1,02 → 2,47 · 1,68 · 1,21 |
| niveau : décalage (pire tranche) | −0,57 (5,43) → −2,65 (7,65) | −0,49 (10,65) → −1,23 (10,80) |
| pleines, sub · basse absolus | −9,7 · −7,5 (1ʳᵉ) → −15,1 · −12,9 | — |

| # | verdict |
|---|---|
| 1 | **tenu** : 7,10 ≤ 8,17 |
| 2 | **entre les deux** : 8,43, sous le témoin, au-dessus de H73 |
| 3 | **échec** : les aigus de « B4 » s'éloignent (\|médian\| 3,47 → 4,25) |
| 4 | **échec** : la basse de « Reload » recule de 2,05 dB (6,12 → 8,17), au-delà des 2 dB |

**La règle** écrit « 4 en échec → l'hypothèse d'additivité ne tient pas ». **Ce n'est pas ce que la mesure montre**, et
c'est écrit plutôt que suivi : le système est résolu (résidu nul), chaque pièce rend à ses frappes, DANS SA BANDE,
l'énergie du stem (rapports après réglage 1,00 à 1,01, sauf les deux bornes). Ce qui casse, c'est le CHOIX de la
bande : elle est lue sur le RENDU, et quand le timbre du rendu n'est pas celui de l'original, la pièce est calée sur
une bande que l'original ne lui donne pas. Le kick du TR-808 de « Reload » (50 Hz, 0,05 s) dépense l'essentiel de son
énergie dans son balayage (H69) : calé sur 150-500 Hz, il descend de 5,5 dB, et le grave des pleines avec lui (sub
−15, basse −13 dB). La caisse claire de « B4 », calée sur 150-500 Hz, monte de 5 dB et son souffle aigu avec elle
(aigus +4,1). **La leçon** : un niveau unique ne corrige un timbre faux dans aucune bande — il choisit laquelle
sacrifier. Le réglage d'une pièce à ses frappes doit lire TOUTES ses bandes (un système de 6 × *N* équations, où un
timbre faux se voit dans le résidu au lieu de se cacher dans un facteur), et ne caler son niveau qu'une fois son
timbre approché (le kick de H70, le charleston de H72). Rien n'entre.

## 36. H75 — les pièces résolues ensemble sur TOUTES leurs bandes, une fois leur timbre approché (écrite AVANT son instrument et sa mesure, 11/10/2026, 05 h 36)

**Ce que H74 a appris** (§ 35.2) : caler une pièce sur UNE bande — celle de son rendu — sacrifie les autres là où son
timbre n'est pas celui de l'original. **L'instrument, étendu** : pour chaque pièce *i* et chacune des six bandes *k*,
aux frappes de *i*, `S_k(i) = Σⱼ xⱼ E_jk(i)` — 6 × *N* équations pour *N* inconnues, chacune en erreur relative et
pondérée par la part de la bande dans l'énergie du stem à ces frappes (une bande où le stem n'a presque rien ne doit
pas tirer le niveau) ; moindres carrés positifs ; facteurs bornés à ×¼-×4, bornes dites. Le RÉSIDU par pièce et par
bande est publié : c'est là qu'un timbre faux se voit, au lieu de se cacher dans un facteur.

**L'HYPOTHÈSE H75** : sur des pièces dont le timbre est déjà approché — le kick de H70 (37 Hz, 0,05 s, tenue 0,04 s :
moteur de `reload-h70`) et le charleston de H72 (0,20 s) sur « Reload » ; le charleston de H73 (0,05 s) sur « B4 » —,
les niveaux résolus sur toutes les bandes rapprochent le mélange de l'original AU MOINS autant que les meilleurs
niveaux partiels déjà mesurés (H65 et le niveau de la frappe moyenne de H70-H72 sur « Reload » ; H73 sur « B4 »),
sans casser le grave ni l'aigu. **Les points mesurés** : « Reload » — le projet de référence, kick et charleston à
leurs réglages de H70 et H72, tous les volumes de batterie résolus par H75 (et rien d'autre) ; « B4 » — le projet de la
chaîne, charleston à 0,05 s, tous les volumes de batterie résolus. Même moteur, même outil, mêmes sections que
§§ 33-35.

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | log-mel, « Reload » | ≤ celui de H72 (7,08), dont les niveaux venaient de H65 et de la frappe moyenne | > H72 + 0,5 dB |
| 2 | log-mel, « B4 » | ≤ celui de H73 (8,37) | > témoin (8,57) |
| 3 | le grave : \|médian\| sub, basse, bas-médium | « Reload » : chacune à moins de 0,5 dB de H72 (1,35 · 1,39 · 1,09) ; « B4 » : à moins de 0,5 dB du témoin (3,25 · 1,30 · 1,02) | l'une pire de plus de 1,5 dB |
| 4 | l'aigu : \|médian\| des aigus | « Reload » ≤ H72 (1,67) ; « B4 » ≤ H73 (1,57) | plus loin de plus de 1 dB que la référence, sur l'un |
| 5 | relevés : facteurs, bornes, résidu par pièce et par bande | — | — |

**La règle de décision** : 1 à 4 tenus → la résolution sur toutes les bandes est la règle candidate pour la chaîne (le
calage interne d'un groupe de batterie, D-phase et banc à elle) ; 3 et 4 tenus sans 1 ou 2 → elle ne casse rien mais
n'apporte pas plus que les corrections partielles ; 3 ou 4 en échec → même sur toutes les bandes, un niveau déplace
de l'énergie entre pièces au prix d'une bande, et le résidu dira laquelle et chez qui.

### 36.1 L'instrument de H75, AVANT toute mesure (11/10/2026, 05 h 38)

`mesure_h74.py --toutes-bandes` (`resoudre_toutes_bandes`), un test de plus dans `test_mesure_h74.py`. Sur la scène
de H74 (gains ×1 · ×2 · ×0,5), les gains retrouvés à 10 % près sur les six bandes. **Le timbre faux** : l'original a
une caisse large bande (dès 200 Hz), le rendu une caisse sans rien sous 2 kHz — le résidu de la caisse le dit à
500-2 000 Hz (sous −6 dB) et reste sous 2 dB à 4-8 kHz, où son timbre est juste. **Vu rouge** sur un mutant qui
calcule le résidu sans les facteurs (−7,8 dB à 4-8 kHz).

### 36.2 Verdict de H75 (11/10/2026, mesuré de 06 h 27 à 06 h 29, écrit à 06 h 30) : 4 en ÉCHEC sur « B4 » — et le résidu nomme les timbres faux, ce que H74 cachait

`reconstruction/travail/reload-h75.sh` (rejouable), moteur de `reload-h70`. Les facteurs : « Reload » charleston +6,47 ·
kick −4,70 · caisse claire −12,04 dB (borne ; ×0,143 demandé, dit), résidu pondéré 0,91 ; « B4 » charleston +6,94 ·
kick −5,28 · caisse claire −0,51 · clap −10,43 dB, résidu 1,52.

| mesure | Reload : H72 → **H75** | B4 : témoin · H73 → **H75** |
|---|---|---|
| log-mel moyen (médian) | 7,08 (5,35) → **6,89 (5,04)** | 8,57 · 8,37 → 8,42 (6,38) |
| \|médian\| sub · basse · bas-médium | 1,35 · 1,39 · 1,09 → 0,91 · 1,75 · 1,07 | 3,25 · 1,30 · 1,02 → 1,48 · 2,29 · 1,73 |
| aigus : médiane (\|médian\|) | −1,67 (1,67) → +1,68 (1,97) | −0,38 (1,57) [H73] → **+3,83 (3,89)** |
| haut-médium : \|médian\| | 3,09 → **0,97** | 3,33 → 2,25 |
| niveau : décalage (pire tranche) | −0,38 (4,71) → −0,83 (6,14) | −0,49 (10,65) → −3,80 (10,61) |

| # | verdict |
|---|---|
| 1 | **tenu** : 6,89 ≤ 7,08 — le meilleur log-mel du chantier |
| 2 | **entre les deux** : 8,42, sous le témoin, au-dessus de H73 |
| 3 | « Reload » **tenu** (la basse à +0,36 dB) ; « B4 » **entre les deux** (basse +0,99, bas-médium +0,71) |
| 4 | « Reload » **entre les deux** (+0,30 dB) ; **« B4 » en ÉCHEC** : les aigus 2,32 dB plus loin que H73 |

**La règle** : « 3 ou 4 en échec → un niveau déplace de l'énergie entre pièces au prix d'une bande, et le résidu dira
laquelle et chez qui ». **Il le dit** (rendu / stem aux frappes de la pièce, après réglage, en dB) :

| pièce | 30-150 | 150-500 | 500-2 k | 2-4 k | 4-8 k | 8-11 k |
|---|---|---|---|---|---|---|
| Reload · charleston | −4,3 | −3,9 | −4,2 | −2,9 | +0,8 | +0,5 |
| Reload · kick | −1,0 | +2,0 | −0,6 | **−9,1** | −2,6 | −1,2 |
| Reload · caisse claire (à sa borne) | −0,2 | **+7,9** | −2,4 | −6,0 | −1,2 | +2,3 |
| B4 · charleston | +2,6 | −3,1 | **−10,7** | −7,3 | −0,5 | +1,6 |
| B4 · kick | −3,5 | −7,4 | **−16,3** | **−13,3** | −7,9 | −6,0 |
| B4 · caisse claire | +1,1 | −5,4 | **−12,5** | −1,2 | +3,7 | +2,3 |
| B4 · clap | −9,1 | −8,7 | +1,7 | −2,1 | −4,5 | −4,6 |

**CE QUE LE RÉSIDU NOMME.** Sur « Reload » : la caisse claire du TR-808 a 7,9 dB de trop à 150-500 Hz MÊME divisée par
quatre — son corps tonal, que H60-H62 avaient déjà désigné (le « ton » de 120 Hz) ; le kick manque de 9 dB à 2-4 kHz
— le clic d'attaque de l'original, que le TR-808 n'a pas. Sur « B4 » : **toutes les pièces manquent de 500-2 000 Hz à
leurs frappes** (−10 à −16 dB), le kick de partout au-dessus de 150 Hz : ce kit n'est pas un TR-808, et aucun niveau
ne le fera ressembler. Les aigus de « B4 » montent parce que le système, pour combler ce que le charleston et la caisse
n'ont pas au médium, les monte en entier. **Rien n'entre.** La leçon de H74, confirmée et précisée : le niveau d'une
pièce ne se règle qu'une fois son TIMBRE juste, et le résidu de H75 est l'instrument qui dit, pièce par pièce, quel
timbre est faux et où — le point de départ d'un choix de machine (ou d'un échantillon) par pièce.

## 37. H76 — la MACHINE de chaque pièce de batterie, choisie par le résidu de H75 (écrite AVANT son instrument et sa mesure, 11/11/2026, 02 h 07)

**Ce que H75 a appris** (§ 36.2) : « le niveau d'une pièce ne se règle qu'une fois son TIMBRE juste, et le résidu de H75
est l'instrument qui dit, pièce par pièce, quel timbre est faux et où — le point de départ d'un choix de machine (ou
d'un échantillon) par pièce ». Toutes les pièces des deux morceaux sont jouées par le TR-808 ; le parc porte trois
autres boîtes à rythmes qui répondent aux mêmes notes General MIDI (36, 38, 39, 42 — relevées dans les deux projets) :
`vsm.tr909`, `vsm.drums` (batterie acoustique), `vsm.fmdrums`.

**L'instrument, à écrire** (`analyse/mesure_h76.py`, son test, `reconstruction/travail/reload-h76.sh` rejouable) :
- **Les candidats** d'une pièce : le TR-808 de H75 (le TÉMOIN — réglé : kick de H70, charleston de H72 / H73) et les
  trois autres machines **à leurs réglages d'usine** (aucun réglage n'est cherché : une variable, la machine).
- **Le choix se fait sur le STEM, jamais sur la mesure du mélange** : pour chaque pièce *i* et chaque candidat *M*, le
  solo de *i* est remplacé par celui de *M*, les autres pièces restant celles de H75 ; le système de H75
  (`mesure_h74.py --toutes-bandes`) est résolu et son **résidu pondéré** `R(i, M)` relevé. La pièce prend la machine
  de plus petit résidu **si elle le baisse d'au moins 5 %** par rapport à H75 ; sinon elle garde le TR-808.
- **Un solo MUET** (une machine qui ne joue pas la note : niveau à plus de 60 dB sous le solo du TR-808) est écarté ET
  DIT — il ne compte pas pour un candidat à résidu.
- **La combinaison** des machines choisies est résolue une fois de plus (les choix ne sont pas indépendants : une
  seule passe, et le résidu combiné est publié à côté des résidus un à un), rendue, et mesurée par le même outil
  (`tools/ecart-a-l-original.py`), le même moteur (`reload-h70`), les mêmes sections que §§ 33-36.
- **La table entière est publiée** : `R(i, M)` pour chaque pièce et chaque machine, pas seulement le choix.

**L'HYPOTHÈSE H76** : le résidu de H75 désigne une machine plus proche — au moins une pièce par morceau change de
machine, le résidu combiné baisse, et le MÉLANGE suit. Attendu plus fort sur « B4 », dont H75 dit que « ce kit n'est
pas un TR-808 » (toutes ses pièces manquent de 500-2 000 Hz), que sur « Reload », dont le TR-808 est déjà réglé.

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | le résidu pondéré combiné | « Reload » ≤ 0,82 et « B4 » ≤ 1,37 (dix pour cent sous H75 : 0,91 · 1,52) | il ne baisse sur AUCUN des deux (aucune pièce ne change, ou la combinaison remonte) |
| 2 | log-mel, « Reload » | ≤ H75 (6,89) | > H72 (7,08) |
| 3 | log-mel, « B4 » | ≤ H73 (8,37) | > témoin (8,57) |
| 4 | le grave : \|médian\| sub, basse, bas-médium | chacune à moins de 0,5 dB de H75 (« Reload » 0,91 · 1,75 · 1,07 ; « B4 » 1,48 · 2,29 · 1,73) | l'une pire de plus de 1,5 dB |
| 5 | l'aigu : \|médian\| des aigus | « Reload » ≤ H75 (1,97) ; « B4 » ≤ H73 (1,57) | plus loin de plus de 1 dB que la référence, sur l'un |
| 6 | ce que le choix CASSE : pour chaque pièce qui change, ses bandes à moins de 3 dB du stem en H75 | aucune ne passe au-delà de 6 dB | une au moins |
| 7 | relevés : la table `R(i, M)`, les solos muets, les facteurs, les bornes, le résidu par pièce et par bande | — | — |

**La règle de décision** : 1 à 6 tenus → le choix de la machine par le résidu est la règle candidate pour la chaîne
(le verdict par pièce d'un groupe de batterie) ; 1 tenu sans 2 ou 3 → le résidu au STEM ne prédit pas le mélange — le
stem est lui-même une estimation de la séparation, et c'est lui qu'il faudra juger ; 1 en échec → aucune machine du
parc, à ses réglages d'usine, n'est plus près que le TR-808 réglé : le besoin est un réglage par pièce ou un
échantillon, pas un changement de boîte ; 6 en échec → le résidu pondéré cache ce qu'il casse, et le critère de choix
est à refaire avant toute règle.

**Ce que la mesure ne pourra PAS dire**, écrit d'avance : une machine écartée à ses réglages d'usine n'est pas une
machine écartée (son réglage n'est pas cherché) ; deux morceaux ne font pas une règle ; et le choix, fait sur le même
stem que le résidu qui le juge, se juge au MÉLANGE (2 à 5), pas au critère 1 seul.

### 37.1 L'instrument de H76, AVANT toute mesure (11/11/2026, 02 h 11)

`analyse/mesure_h76.py` (`choisir`, `bandes_cassees`, `evaluer`) et `analyse/tests/test_mesure_h76.py`, sur la scène de
H74-H75 : l'original a une caisse large bande, le témoin la rend sans rien sous 2 kHz. Trois candidats — une caisse
large (choisie : son résidu est sous la moitié de celui du témoin), une caisse muette (écartée ET dite, sans résidu),
un charleston de même timbre que le témoin (laissé : moins de 5 % de gain). **Vu rouge sur deux mutants** : sans le
seuil, le charleston « pareil » et une caisse PIRE sont choisis (3 tests rouges) ; sans le contrôle du muet, la muette
reçoit un résidu (1 rouge). `ruff` et `mypy` propres. `reconstruction/travail/reload-h76.sh` : les solos témoins sont
ceux de H75 (même moteur, `reload-h70`), les candidats rendus un à un, la machine posée APRÈS les réglages du TR-808
qu'elle remplace (preset sans paramètre : ses réglages d'usine).

**Une limite, dite avant la mesure** : le résidu pondéré est celui du système AVANT le bornage des facteurs (comme en
H75, dont les 0,91 et 1,52 servent de référence) ; un candidat dont un facteur touche la borne a un résidu optimiste.
Chaque borne atteinte est relevée dans la table à côté de son résidu.

### 37.2 Verdict de H76 (11/11/2026, candidats rendus de 02 h 11 à 02 h 14, mélange rendu à 06 h 38, mesuré à 09 h 49, écrit à 09 h 51) : 3 et 6 en ÉCHEC — une seule pièce change, son résidu baisse de 8,8 %, et le mélange s'ÉLOIGNE de 0,92 dB

`reconstruction/travail/reload-h76.sh` (rejouable), moteur de `reload-h70` (`1eff3b6d`). **La table entière** — résidu
pondéré du système avec ce seul solo remplacé ; « b » : un facteur à sa borne (résidu optimiste, § 37.1) :

| pièce | TR-808 réglé (H75, témoin) | `vsm.tr909` | `vsm.drums` | `vsm.fmdrums` | choix |
|---|---|---|---|---|---|
| Reload · charleston | 0,9058 | 1,1411 b | 1,1093 b | 1,1502 b | garde |
| Reload · kick | 0,9058 | 1,0697 b | 1,0838 b | 1,1275 b | garde |
| Reload · caisse claire | 0,9058 | 0,8978 (−0,9 %) | 0,9499 b | 0,9135 b | garde (sous 5 %) |
| B4 · charleston | 1,5193 | 1,5637 | 1,5312 | 1,5633 | garde |
| B4 · kick | 1,5193 | 1,6353 | 1,6638 | 1,6572 | garde |
| B4 · caisse claire | 1,5193 | 1,5320 | 1,4670 (−3,4 %) | **1,3849 (−8,8 %)** | **`vsm.fmdrums`** |
| B4 · clap | 1,5193 | 1,4977 (−1,4 %) | MUET (écarté, dit) | 1,5028 b | garde (sous 5 %) |

Sur 21 candidats, UN passe le seuil ; `vsm.drums` ne joue pas la note 39 (solo à −∞ dB). « Reload » : aucune pièce ne
change, la combinaison EST celle de H75, rien n'est rendu. « B4 », combinaison (charleston +7,24 · kick −5,41 · caisse
claire `vsm.fmdrums` −5,16 · clap −10,71 dB), résidu 1,3849 :

| mesure, « B4 » | H75 → **H76** |
|---|---|
| log-mel moyen (médian) | 8,42 (6,38) → **9,34 (6,98)** |
| \|médian\| sub · basse · bas-médium | 1,48 · 2,29 · 1,73 → 1,18 · **3,08** · 1,71 |
| aigus : médiane (\|médian\|) | +3,83 (3,89) → +1,25 (1,34) |
| médium · haut-médium : \|médian\| | 3,03 · 2,25 → 2,65 · 2,21 |
| niveau : décalage (pire tranche) | −3,80 (10,61) → −3,62 (10,80) |
| attaques du kick appariées | 1 002 / 1 339 → **742 / 1 339** |

| # | verdict |
|---|---|
| 1 | **entre les deux** : « Reload » 0,9058, inchangé (> 0,82) ; « B4 » 1,3849, en baisse de 8,8 % mais au-dessus de 1,37 |
| 2 | **tenu, à vide** : « Reload » est le projet de H75 (6,89) — aucune pièce n'y change, ce n'est pas un gain |
| 3 | **en ÉCHEC** : 9,34 > témoin (8,57) — le pire log-mel de « B4 » depuis H73 |
| 4 | « B4 » **entre les deux** : la basse à +0,79 dB de H75 (sub −0,30, bas-médium −0,02) ; « Reload » à vide |
| 5 | « B4 » **tenu** : 1,34 ≤ 1,57 ; « Reload » à vide |
| 6 | **en ÉCHEC** : la caisse claire de « B4 », à +2,3 dB du stem à 8-11 kHz en H75, y passe à **−14,5 dB** |

La caisse claire de « B4 », rendu / stem à ses frappes (dB), H75 → H76 : 30-150 +1,1 → −3,3 · 150-500 −5,4 → −3,6 ·
500-2 k **−12,5 → −2,1** · 2-4 k −1,2 → +2,1 · 4-8 k +3,7 → −2,2 · 8-11 k **+2,3 → −14,5**. Le résidu PONDÉRÉ a acheté
10 dB de médium au prix de 17 dB d'extrême aigu, parce que le stem a peu d'énergie à 8-11 kHz à ces frappes : la
pondération qui empêche une bande vide de tirer un niveau (§ 36) laisse aussi un timbre s'y vider.

**La règle** : « 6 en échec → le résidu pondéré cache ce qu'il casse, et le critère de choix est à refaire avant toute
règle ». Et 3 en échec le confirme par l'autre bout : le résidu au stem baisse, le mélange s'éloigne. **Le même choix
relu avec la garde de 6 DANS le critère** (un candidat qui casse une bande est refusé — calcul sur la table, aucun
rendu) : `vsm.fmdrums` refusée, `vsm.drums` sous le seuil (−3,4 %) — plus AUCUNE pièce ne change, sur aucun des deux
morceaux, et c'est alors la branche « 1 en échec » qui parle : aucune machine du parc, à ses réglages d'usine, n'est
plus près que le TR-808 réglé. **Rien n'entre.**

**Où le mélange perd, relevé et non expliqué** (densité de la batterie seule contre le stem, `densite.txt`) : le 10e
centile du rendu tombe de −16,7 à −32,3 dB à 2-4 kHz, de −13,9 à −31,4 à 4-8 kHz, de −18,4 à −45,5 à 8-11 kHz, quand
celui du stem est entre −6,6 et −0,3 dB — ENTRE les frappes, la batterie de H76 est vide là où celle de H75 ne l'était
pas (la médiane suit : −3,1 → −7,6 · −2,0 → −9,3 · +0,2 → −9,1). Le résidu, mesuré AUX frappes d'une pièce (10 ms
avant, 100 ms après), ne voit pas ce qui sonne entre elles — c'est la leçon de H72 (la densité), que le critère de H76
ne portait pas. Quelle pièce remplissait ces creux (la queue de souffle de la caisse du TR-808, à ×0,94 en H75 ?) n'est
PAS mesuré : c'est une piste.

**Ce que la mesure ne dit pas** (écrit d'avance au § 37, rappelé) : les machines écartées le sont à leurs réglages
d'usine ; deux morceaux ne font pas une règle.

**Un incident, dit** : le mélange de « B4 », rendu à 06 h 38, n'a été mesuré qu'à 09 h 49. `tools/ecart-a-l-original.py`
demande **~6,2 Go** sur ce morceau de 354 s (relevé : sommet de RSS à 6 235 912 Ko ; tué à 6 Go de plafond, passé à 9 ;
la pile au moment de la mort : `attaques_basses` → `onset_detect` au pas de 32 échantillons) ; trois courses du script
(06 h 38, 07 h 41, 08 h 31) y sont restées sans rien écrire, `systemd-oomd` a tué un onglet de terminal à 09 h 26
(mémoire et échange au-delà de 90 %), le poste a redémarré à 09 h 46. Que les trois courses se soient EMPILÉES n'est
pas prouvé (leurs PID sont perdus). **Le contrôle de l'outil** : rejoué sur le mélange de H75, il rend son texte du
11/10 à l'octet près (`cmp`) — la mesure de 09 h 49 est du même instrument que sa référence.

## 38. H77 — le critère refait : le log-mel de la batterie rendue contre le STEM classe-t-il les points comme le log-mel du mélange ? (écrite AVANT son instrument et sa mesure, 11/11/2026, 09 h 52)

**Ce que H76 a appris** (§ 37.2) : le résidu pondéré de H75, mesuré aux frappes, baisse de 8,8 % quand le mélange
s'éloigne de 0,92 dB — il ne voit ni une bande qui se vide là où le stem pèse peu, ni ce qui sonne ENTRE les frappes.
« Le critère de choix est à refaire avant toute règle. » Avant de choisir quoi que ce soit avec un critère neuf, on
vérifie qu'il PRÉDIT le juge — sur des points déjà rendus et déjà jugés, sans un rendu de plus.

**Le critère candidat** : l'écart log-mel moyen (celui de `tools/ecart-a-l-original.py`, la MÊME fonction `logmel`,
importée, sur le morceau entier) entre le **stem de batterie** de la séparation et la **batterie rendue seule**. Il
voit toutes les bandes sans pondération par l'énergie, et tout le temps, frappes ou non.

**Les points** (tous sur disque, mélange déjà mesuré par l'outil du § 0, seule la batterie change d'un point à l'autre
d'un même morceau) :

| morceau | point | batterie rendue | log-mel du MÉLANGE (publié) |
|---|---|---|---|
| B4 | témoin | `reload-h73/temoin-batterie.wav` | 8,57 |
| B4 | H73 | `reload-h73/essai-batterie.wav` | 8,37 |
| B4 | H74 | `reload-h74/b4/essai-batterie.wav` | 8,43 |
| B4 | H75 | `reload-h75/b4/essai-batterie.wav` | 8,42 |
| B4 | H76 | `reload-h76/b4/essai-batterie.wav` | 9,34 |
| Reload | H71 | `reload-h71/parties/batterie.wav` | 8,16 |
| Reload | H74 | `reload-h74/reload/essai-batterie.wav` | 7,10 |
| Reload | H75 | `reload-h75/reload/essai-batterie.wav` | 6,89 |

Treize paires de points d'un même morceau (10 + 3). Une paire est **tranchée** quand ses deux mélanges diffèrent d'au
moins 0,10 dB : dix le sont (B4 : le témoin contre H73, H74 et H75 — 0,20 · 0,14 · 0,15 —, H76 contre les quatre
autres — 0,77 à 0,97 ; Reload : les trois — 0,21 · 1,06 · 1,27) ; trois ne le sont pas (H73, H74 et H75 de « B4 »,
dans 0,06 dB) et ne comptent pas. Une paire tranchée est **concordante** quand le critère au stem range ses deux
points dans le même ordre que le mélange.

**L'HYPOTHÈSE H77** : le log-mel au stem range les dix paires tranchées comme le mélange.

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | paires tranchées concordantes | 10 sur 10 | 8 ou moins, OU une discordante parmi les six de plus de 0,5 dB (H76 × 4, H71 × 2) |
| 2 | le TÉMOIN du critère : le résidu pondéré de H75 sur la paire H75 / H76 de « B4 » (1,5193 · 1,3849, déjà publié) | — (discordant, connu : c'est ce que le critère neuf doit corriger) | le critère neuf est discordant sur cette même paire |
| 3 | relevés : la table entière — log-mel au stem moyen ET médian de chaque point, l'ordre de chaque paire, les paires non tranchées aussi | — | — |

**La règle de décision** : 1 tenu → H78 rejoue le choix de H76 avec ce critère (la batterie d'un candidat = la somme
des solos déjà rendus, sans un rendu de plus), sous la garde des bandes cassées ; 9 sur 10 sans discordance au-delà de
0,5 dB → le critère range les grands écarts et pas les petits : utilisable avec un seuil de gain qui écarte les petits,
à écrire dans H78 ; 1 en échec → le stem de batterie n'est pas un témoin du mélange à cette métrique, et le choix d'une
machine ne peut se juger qu'au mélange rendu (un rendu par candidat).

**Ce que la mesure ne pourra PAS dire**, écrit d'avance : huit points, dont six doivent leur écart à UNE variable
chacun, ne font pas une règle ; les trois paires de « Reload » et les sept de « B4 » ne sont pas indépendantes (cinq et
trois points) ; un critère qui range des points EXISTANTS n'a pas encore CHOISI — H78 le jugera au mélange.

### 38.1 L'instrument de H77, AVANT toute mesure (11/11/2026, 09 h 54)

`analyse/mesure_h77.py` (`logmel_au_stem`, `paires`, `bilan`) et `analyse/tests/test_mesure_h77.py`, cinq tests. La
fonction `logmel` est celle de `tools/ecart-a-l-original.py`, chargée de son fichier — pas une copie. Sur des cas
connus : une paire à 0,05 dB n'est pas tranchée, une paire à 0,10 dB l'est ; un ordre inversé au stem est discordant,
et « grand » au-delà de 0,5 dB ; un signal contre lui-même rend 0,00 dB, le même à −1 dB rend 1 dB, et un bruit vidé
de son aigu (passe-bas à 4 kHz) plus de cinq fois cela — ce que le résidu pondéré de H76 ne voyait pas. **Une décision
prise en écrivant** : une ÉGALITÉ au stem sur une paire tranchée compte DISCORDANTE (un critère qui ne départage pas ne
prédit pas). **Vu rouge sur trois mutants**, un test chacun : l'ordre non regardé ; toute paire tranchée ; l'égalité
admise. `ruff` et `mypy` propres. `reconstruction/travail/reload-h77.sh` : aucun rendu, huit lectures.

### 38.2 Verdict de H77 (11/11/2026, mesuré à 09 h 54, écrit à 09 h 55) : TENUE — dix paires tranchées sur dix rangées comme le mélange, et les trois paires non tranchées disent le seuil qu'il faudra au critère

`reconstruction/travail/reload-h77.sh` (aucun rendu ; `reload-h77/h77.json`). **La table entière** :

| morceau | point | log-mel du mélange | log-mel au stem : moyen (médian) |
|---|---|---|---|
| B4 | témoin | 8,57 | 11,75 (9,42) |
| B4 | H73 | 8,37 | 11,45 (8,98) |
| B4 | H74 | 8,43 | 11,08 (8,36) |
| B4 | H75 | 8,42 | 11,52 (8,78) |
| B4 | H76 | 9,34 | **13,80** (10,50) |
| Reload | H71 | 8,16 | 10,56 (7,23) |
| Reload | H74 | 7,10 | 9,05 (6,29) |
| Reload | H75 | 6,89 | 8,75 (5,83) |

| paire | Δ mélange | Δ au stem | |
|---|---|---|---|
| B4 · témoin → H73 | −0,20 | −0,30 | concordante |
| B4 · témoin → H74 | −0,14 | −0,67 | concordante |
| B4 · témoin → H75 | −0,15 | −0,23 | concordante |
| B4 · témoin → H76 | +0,77 | +2,05 | concordante |
| B4 · H73 → H76 | +0,97 | +2,35 | concordante |
| B4 · H74 → H76 | +0,91 | +2,72 | concordante |
| B4 · H75 → H76 | +0,92 | +2,28 | concordante |
| Reload · H71 → H74 | −1,06 | −1,51 | concordante |
| Reload · H71 → H75 | −1,27 | −1,81 | concordante |
| Reload · H74 → H75 | −0,21 | −0,30 | concordante |
| B4 · H73 → H74 | +0,06 | −0,37 | non tranchée |
| B4 · H73 → H75 | +0,05 | +0,07 | non tranchée |
| B4 · H74 → H75 | −0,01 | **+0,44** | non tranchée |

| # | verdict |
|---|---|
| 1 | **tenu** : 10 concordantes sur 10 tranchées, aucune discordante |
| 2 | **tenu** : sur la paire H75 → H76 de « B4 », où le résidu pondéré baissait (1,5193 → 1,3849), le log-mel au stem MONTE de 2,28 dB, comme le mélange (+0,92) |

**Ce que les paires non tranchées apprennent** : entre H73, H74 et H75 de « B4 », que le mélange ne départage pas
(0,06 dB), le critère au stem bouge de 0,07 à 0,44 dB, dans un sens ou dans l'autre. Un écart au stem de moins d'un
demi-décibel ne prédit donc rien ; et sur les dix paires tranchées l'écart au stem vaut de 1,4 à 4,8 fois celui du
mélange (le stem ne porte que la batterie : l'écart n'y est pas dilué). **La règle** : « 1 tenu → H78 rejoue le choix
de H76 avec ce critère, sous la garde des bandes cassées » — avec un seuil de gain d'au moins 0,5 dB au stem, que cette
table demande. **Rien n'entre** : un critère qui range des points existants n'a encore rien choisi.

**Ce que la mesure ne dit pas** (écrit d'avance, rappelé) : huit points de deux morceaux ; les paires ne sont pas
indépendantes — quatre des dix tiennent au seul point H76.

## 39. H78 — le choix de H76 rejoué avec le critère de H77 : la machine d'une pièce choisie par le log-mel AU STEM, sous la garde des bandes cassées (écrite AVANT son instrument et sa mesure, 11/11/2026, 09 h 55)

**Ce que H77 a appris** (§ 38.2) : le log-mel de la batterie rendue contre le stem range dix paires sur dix comme le
mélange ; un écart au stem de moins de 0,5 dB ne prédit rien. **Une variable : le critère.** Les candidats (les 21 solos
de H76, déjà rendus), les témoins (les solos de H75), le moteur (`reload-h70`), les niveaux (le système de H75,
`resoudre_toutes_bandes`, résolu pour chaque candidat comme en H76) et le juge (l'outil du § 0) ne changent pas.

**L'instrument, à écrire** (`analyse/mesure_h78.py`, son test, `reconstruction/travail/reload-h78.sh` rejouable) :
- **La batterie d'un candidat sans la rendre** : la somme des solos, chacun à son facteur — `B(i, M) = Σⱼ fⱼ · soloⱼ`,
  le solo de *i* remplacé par celui de *M*. Son log-mel au stem `L(i, M)` par `mesure_h77.logmel_au_stem`.
- **Le CONTRÔLE de la somme, avant tout choix** : la batterie témoin ainsi sommée (solos et facteurs de H75) doit rendre
  le log-mel au stem de la batterie de H75 RENDUE (§ 38.2 : « B4 » 11,52 · « Reload » 8,75) à 0,05 dB près. Sinon la
  somme n'est pas le rendu (un volume qui n'est pas un gain linéaire, un traitement de groupe) : REFUS, pas de table.
- **Le choix** : un candidat MUET est écarté et dit (H76) ; un candidat qui CASSE une bande de sa pièce (à moins de 3 dB
  du stem en H75, à plus de 6 dB avec lui — `mesure_h76.bandes_cassees`) est REFUSÉ et dit ; parmi les autres, la pièce
  prend celui de plus petit `L` **s'il le baisse d'au moins 0,5 dB** sous celui du témoin ; sinon elle garde le TR-808.
- **La combinaison** des choix : résolue, sommée, son `L` publié, ses bandes cassées relevées ; puis RENDUE (mélange et
  batterie seule) et jugée par l'outil du § 0. La batterie rendue repasse par le critère : la somme et le rendu doivent
  se répondre à 0,05 dB.
- **La table entière est publiée** : `L(i, M)`, facteurs, bornes, muets, refusés.

**L'HYPOTHÈSE H78** : avec un critère qui prédit le mélange, au moins une pièce d'au moins un morceau trouve dans le
parc une machine plus proche que le TR-808 réglé, et le mélange SUIT. **Ce que je crois moins**, dit d'avance : H76
relu sous la seule garde des bandes cassées ne changeait déjà plus rien (§ 37.2), et la caisse `vsm.fmdrums` de « B4 »
sera refusée deux fois — par la garde, et par le critère (13,80 contre 11,52).

| # | mesure | réussite | échec |
|---|---|---|---|
| 0 | le contrôle de la somme (témoin sommé contre batterie de H75 rendue, au stem) | à 0,05 dB sur les deux morceaux | au-delà : REFUS, rien d'autre n'est lu |
| 1 | pièces qui changent de machine | au moins une, sur au moins un morceau | aucune, sur aucun des deux |
| 2 | log-mel du MÉLANGE, sur chaque morceau où une pièce change | en baisse d'au moins 0,10 dB sous H75 (« Reload » 6,89 · « B4 » 8,42) | en hausse d'au moins 0,10 dB |
| 3 | grave et aigu, mêmes morceaux : \|médian\| sub, basse, bas-médium, aigus | chacune à moins de 0,5 dB de H75 (« Reload » 0,91 · 1,75 · 1,07 · 1,97 ; « B4 » 1,48 · 2,29 · 1,73 · 3,89) | l'une pire de plus de 1,5 dB |
| 4 | bandes cassées par la COMBINAISON | aucune | une au moins |
| 5 | la batterie de la combinaison : sommée contre rendue, au stem | à 0,05 dB | au-delà |
| 6 | relevés : la table `L(i, M)`, facteurs, bornes, muets, refusés | — | — |

**La règle de décision** : 1 à 5 tenus → le choix par le log-mel au stem, sous la garde, est la règle candidate pour le
verdict par pièce d'un groupe de batterie (à écrire en D-phase, avec son banc, sur un troisième morceau) ; 1 en échec →
c'est la branche que H76 n'avait pas pu lire proprement : avec un critère qui prédit le mélange, AUCUNE machine du parc
à ses réglages d'usine n'est plus près que le TR-808 réglé — le besoin est un réglage par pièce (ce que H70 et H72 ont
fait à la main pour le kick et le charleston) ou un échantillon, et la suite le dira ; 1 tenu et 2 en échec → la
concordance de H77 ne tient pas sur un point neuf : huit points ne faisaient pas une règle, et le choix d'une machine
ne se juge qu'au mélange rendu.

**Ce que la mesure ne pourra PAS dire**, écrit d'avance : les machines écartées le sont à leurs réglages d'usine ; les
niveaux restent ceux du système de H75, dont H76 a montré qu'il achète une bande au prix d'une autre — un candidat peut
perdre par son NIVEAU et non par son timbre (les facteurs à leur borne sont relevés pour cela) ; deux morceaux.

### 39.1 L'instrument de H78, AVANT toute mesure (11/11/2026, 09 h 58)

`analyse/mesure_h78.py` (`sommer`, `choisir`, `evaluer`, `controle_de_la_somme`) et
`analyse/tests/test_mesure_h78.py`, cinq tests, sur la scène de H74-H76 et avec le critère RÉEL (la fonction `logmel` de
l'outil, par `mesure_h77`) : la caisse large est choisie, la muette écartée et dite, le charleston « pareil » laissé ;
un candidat meilleur au critère mais qui casse une bande est refusé, et le suivant ne passe que s'il gagne 0,5 dB ; une
pièce sans facteur n'est pas sommée à ×1, elle fait un refus. **Vu rouge sur quatre mutants**, un test chacun : sans le
seuil de gain ; la garde ôtée du choix ; le muet non contrôlé ; la somme sans ses facteurs. `ruff` et `mypy` propres.
`reconstruction/travail/reload-h78.sh` : dérivé de celui de H76 (mêmes fonctions `projet` et `rendre`), aucun candidat
rendu, la combinaison seule si une pièce change ; un contrôle de somme en échec arrête la course (code 3) sans table.

**Une limite, dite avant la mesure** : le critère lit le morceau ENTIER, le juge du mélange aussi — mais le stem de
batterie est une estimation de la séparation, et ce qu'elle a laissé fuir des autres instruments compte dans le critère
comme de la batterie à atteindre.

### 39.2 Verdict de H78 (11/11/2026, mesuré de 09 h 58 à 10 h 01, écrit à 10 h 02) : le meilleur log-mel de « B4 » (8,42 → 7,37) et 3 en ÉCHEC — trois pièces passent au TR-909, la densité revient, le grave et l'aigu s'éloignent ; la garde a un trou

`reconstruction/travail/reload-h78.sh` (rejouable), moteur `1eff3b6d`. **La table entière** — log-mel au stem de la
batterie sommée, moyen ; « b » : un facteur à sa borne ; « C » : casse une bande de sa pièce, refusé :

| pièce | TR-808 réglé (témoin) | `vsm.tr909` | `vsm.drums` | `vsm.fmdrums` | choix |
|---|---|---|---|---|---|
| Reload · charleston | 8,75 | 11,86 b C | 13,36 b C | 12,88 b C | garde |
| Reload · kick | 8,75 | 8,54 b C | 9,06 b C | 9,07 b C | garde (le seul meilleur gagne 0,21 dB, et casse 30-150) |
| Reload · caisse claire | 8,75 | 8,77 C | 8,92 b C | 9,51 b C | garde |
| B4 · charleston | 11,52 | 11,82 | 12,19 | 12,06 | garde |
| B4 · kick | 11,52 | **10,87** (−0,65) | 12,45 | 11,29 | **`vsm.tr909`** |
| B4 · caisse claire | 11,52 | **11,02** (−0,50) | 11,78 C | 13,80 C | **`vsm.tr909`** (au seuil même) |
| B4 · clap | 11,52 | **10,34** (−1,18) | MUET | 12,08 b | **`vsm.tr909`** |

« Reload » : les neuf candidats cassent une bande, huit à une borne — aucune pièce ne change, rien n'est rendu. « B4 » :
la caisse `vsm.fmdrums` de H76 est refusée deux fois, comme annoncé (garde, et 13,80). Combinaison de « B4 » (charleston
TR-808 +6,27 · kick 909 −11,23 · caisse 909 −1,00 · clap 909 −8,75 dB) : **10,29** au stem, aucune bande cassée.

| mesure, « B4 » | H75 → **H78** |
|---|---|
| log-mel moyen (médian) | 8,42 (6,38) → **7,37 (5,94)** |
| \|médian\| sub · basse · bas-médium | 1,48 · 2,29 · 1,73 → 2,07 · **4,39** · 2,10 |
| aigus : médiane (\|médian\|) | +3,83 (3,89) → **+6,67 (6,67)** |
| médium · haut-médium : \|médian\| | 3,03 · 2,25 → 3,54 · 3,44 |
| niveau : décalage (pire tranche) | −3,80 (10,61) → −5,06 (10,20) |
| attaques du kick appariées | 1 002 / 1 339 → 792 / 1 339 |

| # | verdict |
|---|---|
| 0 | **tenu** : témoin sommé contre batterie rendue, 8,75 · 8,75 (« Reload ») et 11,52 · 11,52 (« B4 ») — la somme EST le rendu |
| 1 | **tenu** sur « B4 » (trois pièces sur quatre) ; aucune sur « Reload » |
| 2 | **tenu** : 7,37, soit −1,05 dB sous H75 — et sous tous les points de « B4 » mesurés depuis H73 |
| 3 | **en ÉCHEC** : la basse 2,10 dB plus loin, les aigus 2,78 dB plus loin (sub +0,59, bas-médium +0,37) |
| 4 | **tenu à la lettre** : aucune bande « cassée » — voir le trou ci-dessous |
| 5 | **tenu** : batterie de la combinaison sommée 10,29, rendue 10,29 |

**La règle** ne donnait de branche qu'à « 1 à 5 tenus », « 1 en échec » et « 1 tenu, 2 en échec » ; 3 en échec avec 2
tenu n'y était pas — **je la tranche ici, par la lecture la plus sévère** : le choix n'est PAS une règle candidate. Deux
juges du même outil se contredisent sur le même rendu, et un gain au log-mel qui s'achète en équilibre n'est pas un
gain tant qu'on ne sait pas lequel des deux l'oreille suivrait. **Rien n'entre.**

**Ce que H78 établit quand même**, chiffres à l'appui :
- **Le critère de H77 a prédit un point NEUF** : −1,23 dB au stem, −1,05 dB au mélange (rapport 1,2 — SOUS la fourchette
  de 1,4 à 4,8 du § 38.2 : le stem a prédit le sens, pas l'ampleur). La branche « la concordance ne tient pas sur un point neuf » est écartée.
- **Le résidu de H75 nommait juste, mais ne savait pas choisir** : il disait que les pièces de « B4 » manquaient de
  500-2 000 Hz (§ 36.2). Avec le TR-909, rendu / stem aux frappes à 500-2 000 Hz : charleston −10,7 → −3,4 (sans avoir
  changé de machine : ce sont les autres pièces qui y sonnent), kick −16,3 → −10,6, caisse −12,5 → −10,8.
- **La densité revient** (`densite.txt`) : le 10e centile de la batterie rendue, de −13,9 (H75) à **−0,6 dB** à 4-8 kHz
  pour un stem à −0,3 ; de −18,4 à −6,7 à 8-11 kHz pour un stem à −6,6. C'est par là que le log-mel gagne.

**LE TROU DE LA GARDE**, trouvé en lisant le résidu de la combinaison : le kick du TR-909, à ses frappes, passe à
30-150 Hz de **−3,5 à −11,2 dB** du stem — et n'est pas « cassé », parce que la garde ne protège que les bandes à moins
de 3 dB AVANT le choix. Une bande déjà loin de 3,5 dB peut s'éloigner de 7,7 dB de plus sans un mot. C'est le grave de
l'attendu 3 : le système de H75 donne au kick du 909 un facteur de 0,2745 (la borne est à 0,25), la batterie rendue a
9,9 dB d'énergie de moins que le stem à 30-150 Hz (5,0 en H75), et son 99e centile y est 12,9 dB sous celui du stem.
Écrit d'avance au § 39 : « un candidat peut perdre par son NIVEAU et non par son timbre » — ici il GAGNE au critère
malgré un niveau faux, parce que le log-mel moyen compte autant les trames creuses que les frappes.

**La suite que ces chiffres désignent** (à écrire en H79, AVANT sa mesure) : la machine choisie par H78 gardée, UNE
variable — le niveau du kick, que le système de H75 fixe mal dès que le timbre change ; et la garde refaite pour voir
une bande qui s'ÉLOIGNE, pas seulement une bande qui était proche.

**Ce que la mesure ne dit pas** (écrit d'avance, rappelé) : machines à leurs réglages d'usine ; un seul morceau a
changé ; le stem porte les fuites de la séparation.

## 40. H79 — les machines de H78 gardées, UNE variable : le niveau du kick, calé sur son grave à ses frappes ; et la garde refaite (écrite AVANT son instrument et sa mesure, 11/11/2026, 10 h 04)

**Ce que H78 a appris** (§ 39.2) : sur « B4 », kick, caisse claire et clap au TR-909 donnent le meilleur log-mel du
morceau (7,37) et un grave faux — le système de H75 donne au kick un facteur de 0,2745, et à ses frappes la batterie
est à −11,2 dB du stem à 30-150 Hz. **Une variable : le facteur du kick.** Machines (H78), autres facteurs (charleston
2,0593 · caisse 0,891 · clap 0,365), moteur (`reload-h70`), juge (l'outil du § 0) : inchangés. « Reload » n'est pas
mesuré : aucune pièce n'y a changé.

**L'instrument, à écrire** (`analyse/mesure_h79.py`, son test, `reconstruction/travail/reload-h79.sh` rejouable) :
- **Le facteur calé** `f*` : celui qui porte la batterie, aux frappes du kick et à 30-150 Hz (la bande principale du
  kick), à l'énergie du stem, les autres pièces à leur facteur de H78 —
  `f*² = (S − Σ_{j≠kick} fⱼ² Eⱼ) / E_kick`. Un numérateur négatif ou nul est un REFUS ; une borne (×¼-×4) atteinte est
  dite. C'est le calage de H74 (une pièce, sa bande), pour la seule pièce dont H78 a montré le niveau faux.
- **Le balayage ENTIER**, au stem ET au mélange : le facteur de H78 (0,2745) et +3, +6, +9, +12 dB au-dessus, plus
  `f*` — six points, chacun sommé (critère de H77, résidu du kick par bande à ses frappes) puis RENDU et jugé. Le
  balayage dit si `f*` est un bon point ou un point quelconque d'une pente.
- **La garde refaite** (`bandes_eloignees`) : une bande d'une pièce changée est ÉLOIGNÉE si elle finit à plus de 6 dB du
  stem ET à plus de 3 dB plus loin qu'en H75 — qu'elle ait été proche ou non. Relevée pour chaque point, et relue sur
  le choix de H78 (où elle doit nommer le 30-150 Hz du kick : c'est le trou qu'elle ferme).

**L'HYPOTHÈSE H79** : le grave faux de H78 est un NIVEAU, pas un timbre — au facteur `f*`, le log-mel garde le gain de
H78 et le grave revient au moins où H75 l'avait.

| # | mesure, « B4 », au point `f*` | réussite | échec |
|---|---|---|---|
| 1 | log-mel du mélange | ≤ 7,47 (H78 + 0,10) | > 8,32 (le gain sur H75 perdu à 0,10 près) |
| 2 | \|médian\| sub · basse · bas-médium | chacune à moins de 0,5 dB de H75 (1,48 · 2,29 · 1,73), ou meilleure | l'une pire que H75 de plus de 1,5 dB |
| 3 | \|médian\| des aigus | ≤ 4,39 (H75 + 0,5) | > 5,39 (H75 + 1,5) |
| 4 | bandes ÉLOIGNÉES (garde refaite) des trois pièces changées | aucune | une au moins |
| 5 | le critère au stem range-t-il les six points comme le mélange ? (paires tranchées, règle de § 38) | toutes concordantes | une discordante de plus de 0,5 dB au mélange |
| 6 | relevés : `f*`, le balayage entier (stem, mélange, bandes), la garde refaite relue sur H78 | — | — |

**La règle de décision** : 1 à 4 tenus → sur « B4 », machine PUIS niveau donnent un point meilleur que H75 sur les deux
juges — la méthode en deux temps (machine par le log-mel au stem, niveau de la pièce changée par sa bande) est la
candidate, à rejouer sur un troisième morceau avant toute D-phase ; 1 tenu, 2 ou 3 en échec → le grave ou l'aigu faux
tient au TIMBRE du 909 et non à son niveau, et le log-mel seul ne suffit pas à choisir une machine : le critère doit
porter l'équilibre ; 1 en échec → le gain de H78 était celui d'un kick trop bas (un mélange creusé qui ressemble mieux
au log-mel), et H78 est à relire comme un artefact du juge ; 5 en échec → le critère de H77 ne range pas des points
qui ne diffèrent que d'un niveau, et ne peut servir à en régler un.

**Ce que la mesure ne pourra PAS dire**, écrit d'avance : un morceau ; un calage sur UNE bande est ce que H74 a montré
fragile quand le timbre est faux (c'est pourquoi le balayage entier est publié) ; les facteurs des trois autres pièces
restent ceux d'un système résolu avec le kick à 0,2745 — s'ils en dépendaient, ce n'est pas mesuré ici.

### 40.1 L'instrument de H79, AVANT toute mesure (11/11/2026, 10 h 07)

`analyse/mesure_h79.py` (`facteur_cale`, `rendu_sur_stem`, `bandes_eloignees`) et `analyse/tests/test_mesure_h79.py`,
cinq tests sur la scène de H74 (gains ×1 · ×2 · ×0,5) : le kick calé, les autres pièces à leur vrai gain, retrouve ×1
à 5 % près ; un solo deux fois trop fort se cale à la moitié ; un solo dix fois trop bas touche la borne ×4, dite ; des
autres pièces qui dépassent déjà le stem font un REFUS, une pièce sans facteur aussi ; le résidu à facteurs donnés est
nul aux vrais gains. **La garde refaite, sur les chiffres mêmes de H78** : l'ancienne (`bandes_cassees`) se tait devant
−3,54 → −11,19 dB, la neuve nomme « 30-150 ». **Vu rouge sur quatre mutants**, un test chacun : les autres pièces non
retranchées ; le numérateur négatif non refusé ; la garde redevenue l'ancienne ; la borne non appliquée. `ruff` et
`mypy` propres. `reconstruction/travail/reload-h79.sh` : les facteurs de H78 relus dans son `choix.json`, six mélanges
rendus et jugés (le point « +0 dB » REJOUE H78 : il doit rendre 7,37, c'est le témoin de la course), la concordance
par `mesure_h77.paires`.

### 40.2 Verdict de H79 (11/11/2026, mesuré de 10 h 07 à 10 h 12, garde relue à 10 h 14, écrit à 10 h 14) : 1, 2, 3 et 5 TENUS — log-mel 7,10, grave et aigu mieux qu'en H75 — et 4 en ÉCHEC : la garde refaite montre le grave du kick qui déborde sur les frappes des autres

`reconstruction/travail/reload-h79.sh` (rejouable), moteur `1eff3b6d`. `f*` = **0,9952** (−0,04 dB ; aucune borne) : le
kick du TR-909 à son niveau d'usine, à très peu près — le système de H75 le divisait par 3,6. **Le balayage entier** :

| point | facteur du kick | au stem | mélange : log-mel (médian) | \|médian\| sub · basse · bas-médium | \|médian\| aigus | niveau | kick à 30-150 Hz, à ses frappes | bandes ÉLOIGNÉES (toutes pièces) |
|---|---|---|---|---|---|---|---|---|
| +0 dB (H78 rejoué) | 0,2745 | 10,29 | 7,37 (5,94) | 2,07 · 4,39 · 2,10 | 6,67 | −5,06 | −11,19 | kick : 30-150 |
| +3 dB | 0,3877 | 10,07 | 7,25 (5,82) | 1,16 · 3,43 · 2,03 | 5,44 | −4,22 | −8,19 | kick : 30-150 |
| +6 dB | 0,5477 | 9,92 | 7,15 (5,72) | **0,72** · 2,45 · 1,86 | 4,24 | −2,99 | −5,19 | charleston : 30-150 |
| +9 dB | 0,7736 | **9,87** | **7,10** (5,65) | 1,48 · 1,75 · 1,68 | 2,21 | −0,97 | −2,19 | charleston : 30-150 ; caisse : 30-150 |
| **f\*** | 0,9952 | 9,90 | **7,10** (5,67) | 1,89 · 1,39 · 1,56 | 1,52 | +0,65 | 0,00 | charleston : 30-150, 150-500 ; caisse : 30-150 |
| +12 dB | 1,0928 | 9,93 | 7,12 (5,69) | 2,02 · **1,35** · **1,54** | **1,44** | +1,24 | +0,81 | les mêmes qu'à f\* |

Le témoin de la course : « +0 dB » rend 7,37 et les bandes de H78 au centième. Références : H75 8,42 · 1,48 · 2,29 ·
1,73 · aigus 3,89 ; H73 aigus 1,57.

| # | verdict, au point f\* |
|---|---|
| 1 | **tenu** : 7,10 ≤ 7,47 — le meilleur log-mel de « B4 », 1,32 dB sous H75 |
| 2 | **tenu** : sub 1,89 (à +0,41 de H75), basse 1,39 et bas-médium 1,56 MEILLEURES que H75 |
| 3 | **tenu** : aigus 1,52 — sous H75 (3,89) et sous H73 (1,57), la meilleure valeur de « B4 » avec le point +12 dB |
| 4 | **en ÉCHEC** : la caisse claire (changée) est à **+8,7 dB** du stem à 30-150 Hz à ses frappes (+1,1 en H75) ; kick et clap : aucune |
| 5 | **tenu** : 9 paires tranchées sur 9 concordantes (15 paires ; les six non tranchées sont celles du plateau) |

**La règle** : « 1 à 4 tenus → la méthode en deux temps est la candidate ». 4 ne l'est pas ; les autres branches
supposent 1, 2, 3 ou 5 en échec. **Tranché au plus sévère, comme en H78** : pas encore une candidate. Mais l'hypothèse
elle-même — « le grave faux de H78 est un NIVEAU » — est tenue AU MÉLANGE sur ses trois attendus, et la branche « le
gain de H78 était celui d'un kick trop bas » est écartée : le log-mel BAISSE encore quand le kick remonte (7,37 → 7,10).

**Ce que la garde refaite montre, et que le juge du mélange ne voit pas** : aux frappes du kick, le grave est au stem
(0,0 dB) ; aux frappes de la caisse il est 8,7 dB au-dessus, à celles du charleston **13,3 dB** au-dessus (2,6 en H75,
2,1 en H78). Le stem a donc bien moins de grave ENTRE les frappes du kick que la batterie rendue : le kick du TR-909, à
ses réglages d'usine, sonne encore quand celui de l'original s'est tu. L'outil du § 0 juge l'équilibre par tranches de
7 s et ne peut pas le voir ; la garde, qui lit pièce par pièce à leurs frappes, le nomme. C'est la leçon de H67-H70 sur
« Reload » (la LONGUEUR du kick), retrouvée sur un second morceau par un autre instrument. Le sub le dit aussi à sa
façon : son meilleur point est à +6 dB (0,72), il remonte ensuite (1,89 à f\*) pendant que la basse continue de
s'améliorer — un seul facteur ne peut pas servir les deux.

**f\* est sur un plateau, pas sur un sommet** : de +9 dB à +12 dB, le mélange tient dans 0,02 dB de log-mel et le stem
dans 0,06. Le calage sur une bande donne un point du plateau ; il ne le choisit pas mieux qu'un autre.

**La garde relue sur H78** (attendu 6) : elle nomme « kick : 30-150 » au point +0 dB — le trou est fermé. Avec elle,
H78 aurait refusé le kick du 909 AU NIVEAU DU SYSTÈME DE H75, alors qu'à f\* ce même kick donne le meilleur mélange :
une garde qui lit des niveaux faux refuse de bons timbres. L'ordre des opérations compte — niveau d'abord, garde
ensuite.

**Rien n'entre.** La suite que ces chiffres désignent (H80, à écrire AVANT sa mesure) : UNE variable, la décroissance
du kick du TR-909 — la méthode de H72 (le charleston réglé sur ses frappes), sur le kick, jugée par la garde aux
frappes des AUTRES pièces et par le mélange.

**Un défaut de l'instrument, dit** : la première course (10 h 07) est morte avant toute mesure sur une erreur de
format de la ligne de commande (`{d:g}` sur une chaîne) — les cinq tests passent par les fonctions, aucun par
`main()`. Corrigé (`float(d)`), rejoué. Et la garde n'était calculée que pour la pièce calée : étendue à toutes les
pièces à 10 h 14 (`eloignees_par_piece`), l'instrument rejoué, les six mélanges gardés ; les six points sont identiques
à ceux de la course précédente (facteur, log-mel au stem, résidu du kick). **Ce que la mesure ne dit pas** (écrit
d'avance, rappelé) : un morceau ; les trois autres facteurs sont ceux d'un système résolu avec le kick à 0,2745.

## 41. H80 — UNE variable : la décroissance du kick du TR-909, choisie par le grave aux frappes des AUTRES pièces (écrite AVANT son instrument et sa mesure, 11/11/2026, 10 h 16)

**Ce que H79 a appris** (§ 40.2) : sur « B4 », le kick du TR-909 à son niveau (f\* = 0,9952) donne le meilleur mélange
(7,10), et la garde refaite le dit trop LONG — le grave est au stem à ses frappes, 8,7 dB au-dessus à celles de la
caisse, 13,3 à celles du charleston. Sa décroissance est à 0,32 s (réglage d'usine ; plage 0,05-1,2 s,
`drum.kick.decay`) ; la chaîne avait donné 0,196 s à celle du TR-808 qu'il remplace.

**Une variable : cette décroissance.** Machines (H78), facteurs du charleston, de la caisse et du clap (H78), moteur,
juge : inchangés. Le niveau du kick n'est pas une seconde variable mais une CONSÉQUENCE, par la règle de H79 : à chaque
décroissance, `f*` est recalé sur 30-150 Hz à ses frappes (`mesure_h79.facteur_cale`) — sans quoi un kick plus court,
donc moins énergique, serait jugé sur un niveau faux (la leçon de § 40.2 : niveau d'abord, garde ensuite).

**L'instrument, à écrire** (`analyse/mesure_h80.py`, son test, `reconstruction/travail/reload-h80.sh` rejouable) :
- **Le balayage ENTIER** : 0,05 · 0,08 · 0,12 · 0,16 · 0,20 · 0,25 · 0,32 s. Pour chacune : le solo du kick RENDU, `f*`,
  la batterie sommée (critère au stem de H77), le résidu de CHAQUE pièce à ses frappes, les bandes éloignées ; puis le
  MÉLANGE rendu et jugé — les sept, pas le seul point choisi.
- **Le TÉMOIN que le réglage atteint le moteur** : la QUEUE du solo du kick à 30-150 Hz (énergie de 100 à 300 ms après
  la frappe, rapportée à celle des 100 premières ms) doit décroître strictement avec la décroissance sur les sept
  points. Sinon REFUS : la clé n'est pas lue, et sept rendus identiques ne mesurent rien. Et le point 0,32 s rejoue
  H79 : il doit rendre f\* = 0,9952 et 7,10.
- **Le choix**, sur le stem : la décroissance qui minimise le **débord** — la moyenne des \|rendu / stem\| à 30-150 Hz
  aux frappes du charleston et à celles de la caisse claire (les deux pièces où H79 l'a relevé : 13,3 et 8,7 dB). À
  débord égal au dixième, la plus longue (la plus proche du réglage d'usine).

**L'HYPOTHÈSE H80** : il existe une décroissance plus courte que 0,32 s où le grave ne déborde plus (aucune bande
éloignée sur aucune pièce), et le mélange garde ce que H79 a gagné.

| # | mesure, « B4 », à la décroissance choisie | réussite | échec |
|---|---|---|---|
| 0 | le témoin : queue du solo strictement décroissante ; 0,32 s rejoue H79 | les deux | l'un manque : REFUS, rien d'autre n'est lu |
| 1 | bandes ÉLOIGNÉES, les quatre pièces | aucune | une au moins |
| 2 | log-mel du mélange | ≤ 7,20 (H79 + 0,10) | > 7,37 (H78 : le gain de H79 perdu) |
| 3 | \|médian\| sub · basse · bas-médium | chacune à moins de 0,5 dB de H79 (1,89 · 1,39 · 1,56), ou meilleure | l'une pire de plus de 1,5 dB |
| 4 | \|médian\| des aigus | ≤ 2,02 (H79 + 0,5) | > 3,02 |
| 5 | le critère au stem range-t-il les sept points comme le mélange ? (paires tranchées, § 38) | toutes concordantes | une discordante de plus de 0,5 dB au mélange |
| 6 | relevés : le balayage entier (queue, f\*, débord, stem, mélange, bandes, garde) | — | — |

**La règle de décision** : 1 à 4 tenus → sur « B4 », machine, niveau puis longueur donnent un point propre aux deux juges
ET à la garde : la méthode en trois temps est la candidate, à rejouer telle quelle sur un TROISIÈME morceau avant toute
D-phase ; 1 en échec, 2 à 4 tenus → le grave entre les frappes n'est pas (seulement) la longueur du kick — et il faudra
demander au STEM ce qu'il vaut là, avant d'accuser la machine ; 2 en échec → un kick plus court rend au mélange ce que
le 909 lui avait donné, et le débord était une part du gain ; 5 en échec → le critère de H77 ne voit pas une longueur.
Toute autre combinaison se tranche au plus sévère, comme en H78 et H79.

**Ce que la mesure ne pourra PAS dire**, écrit d'avance : la garde lit le STEM, et un séparateur peut avoir rendu à la
basse la queue d'un kick — un « débord » peut être un défaut de la cible et non du rendu ; le juge du mélange, par
tranches de 7 s, ne départagera pas ; un morceau ; l'accord du kick (55 Hz d'usine) n'est pas touché, alors que le sub
de H79 (1,89, son meilleur point à +6 dB) dit qu'il pourrait compter.

### 41.1 L'instrument de H80, et ce que ses tests ont changé — AVANT toute mesure (11/11/2026, 10 h 21)

`analyse/mesure_h80.py` (`queue_db`, `queue_croissante`, `frappes_hors`, `debord`, `choisir`, `evaluer`) et
`analyse/tests/test_mesure_h80.py`, huit tests. La scène : celle de H74, le kick de l'« original » à 0,12 s, trois
candidats à 0,06 · 0,12 · 0,30 s.

**CE QUE LA SCÈNE A CHANGÉ au § 41.** Le débord y était écrit « aux frappes du charleston et à celles de la caisse ».
Sur la scène, un charleston sur deux tombe SUR un kick : toutes frappes confondues, le kick de 0,30 s ne « déborde »
que de **1,8 dB** — la tête des kicks noie leur queue —, là où les seuls charlestons d'ENTRE les kicks disent
**+8,7 dB** (et −13,9 pour le kick trop court). C'est la population de la mesure qui était mal faite (« la population
d'une mesure, c'est aussi le temps »). **Décision, écrite avant toute mesure du morceau** : le débord se lit aux
frappes des autres pièces situées à 100 ms au moins de TOUTE frappe du kick (`frappes_hors`, `HORS_S`) ; le nombre de
frappes gardées est publié ; une pièce qui n'en a aucune est écartée ET DITE ; s'il n'en reste aucune, REFUS. La garde
de l'attendu 1 (bandes éloignées, toutes frappes, comme en H79) ne change pas.

Sur la scène : 0,12 s est choisie, son niveau retrouvé à 5 % près, son débord sous 1 dB ; la caisse (toujours sur un
kick) est écartée et dite ; trois solos identiques font un REFUS. **Vu rouge sur quatre mutants** : toutes les frappes
gardées (3 rouges) ; à égalité la plus courte (1) ; la queue non contrôlée (2) ; le débord sans valeur absolue (2).
`ruff` et `mypy` propres.

**La ligne de commande passée À BLANC** (le défaut de H79 : aucun test ne passait par `main()`) : deux « points »
donnés avec le MÊME solo, celui du kick d'usine de H76 — REFUS attendu, REFUS obtenu (code 3, pas de table écrite).
**Ce que cet essai a laissé voir, et qu'il faut donc dire** : le point d'usine, déjà connu de H79 (f\* 0,9952, 9,90 au
stem), a une queue de +2,92 dB (plus d'énergie de 100 à 300 ms qu'à la frappe) et un débord de **12,1 dB** (charleston
+15,0 sur 855 frappes gardées de 892 ; caisse +9,2 sur 538 de 555). Aucun autre point n'a été rendu ni lu.

`reconstruction/travail/reload-h80.sh` : la fonction `projet` de H76-H79, plus UN réglage de la machine posée
(`PARAM="4:drum.kick.decay=…"`, refusé si la piste n'est pas un TR-909 à ses réglages d'usine) ; sept solos, l'instrument,
sept mélanges rendus et jugés, la concordance.

### 41.2 Verdict de H80 (11/11/2026, mesuré de 10 h 21 à 10 h 27, écrit à 10 h 28) : NON TENUE — aucune décroissance ne contente la garde, celle que le stem choisit abîme le mélange, et le stem et le mélange se contredisent sur le grave d'entre les kicks

`reconstruction/travail/reload-h80.sh` (rejouable), moteur `1eff3b6d`. Frappes gardées pour le débord : charleston 855
sur 892, caisse 538 sur 555. **Le balayage entier** (f\* recalé à chaque point ; « entre » : rendu / stem à 30-150 Hz
aux frappes d'entre les kicks) :

| décroissance | queue du solo | f\* | entre : charleston · caisse | débord | au stem | mélange : log-mel (médian) | \|médian\| sub · basse · bas-médium | \|médian\| aigus | bandes ÉLOIGNÉES |
|---|---|---|---|---|---|---|---|---|---|
| 0,05 s | −3,86 | 3,0527 | −1,9 · −15,6 | 8,78 | 10,09 | 7,49 (6,08) | 10,95 · 4,88 · 5,21 | 2,29 | charleston : 150-500 ; kick : 8-11 k |
| 0,08 s | −1,94 | 1,9152 | +3,2 · −6,5 | 4,85 | **9,74** | 7,21 (5,85) | 6,58 · 2,84 · 4,37 | 2,00 | charleston : 150-500 |
| **0,12 s (choisie)** | −0,20 | 1,4531 | +7,5 · −0,1 | **3,77** | 9,76 | 7,13 (5,73) | 3,15 · 1,91 · 3,53 | 1,62 | charleston : 30-150, 150-500 |
| 0,16 s | +0,91 | 1,2579 | +10,2 · +3,5 | 6,83 | 9,79 | **7,10** (5,69) | 1,23 · 1,55 · 2,98 | 1,50 | charleston : 30-150, 150-500 |
| 0,20 s | +1,67 | 1,1496 | +12,0 · +5,7 | 8,86 | 9,83 | **7,10** (5,68) | **0,22** · 1,42 · 2,50 | 1,32 | charleston : 30-150, 150-500 |
| 0,25 s | +2,32 | 1,0666 | +13,6 · +7,5 | 10,54 | 9,86 | **7,10** (5,67) | 1,03 · **1,39** · 2,05 | **1,31** | charleston : 30-150, 150-500 ; caisse : 30-150 |
| 0,32 s (usine, H79) | +2,92 | 0,9952 | +15,0 · +9,2 | 12,08 | 9,90 | **7,10** (5,67) | 1,89 · **1,39** · **1,56** | 1,52 | charleston : 30-150, 150-500 ; caisse : 30-150 |

| # | verdict, à 0,12 s |
|---|---|
| 0 | **tenu** : la queue croît strictement sur les sept points (−3,86 → +2,92 dB) ; 0,32 s rend f\* 0,9952 et 7,10, H79 au centième |
| 1 | **en ÉCHEC** : le charleston reste éloigné à 30-150 et 150-500 Hz — et il l'est à 150-500 Hz aux SEPT points, même à 0,05 s |
| 2 | **tenu** : 7,13 ≤ 7,20 |
| 3 | **en ÉCHEC** : bas-médium 3,53 (+1,97 sur H79) ; sub 3,15 (+1,26) et basse 1,91 (+0,52), entre les deux |
| 4 | **tenu** : 1,62 ≤ 2,02 |
| 5 | **entre les deux** : 6 paires concordantes sur 10 tranchées, 4 discordantes — toutes de 0,11 dB au mélange, aucune au-delà de 0,5 |

**La règle** : 1 et 3 en échec n'est pas une de ses branches — « toute autre combinaison se tranche au plus sévère » :
H80 n'est pas tenue, et la décroissance du kick n'est PAS la troisième étape d'une méthode. **Rien n'entre.** Le point
de H79 (0,32 s, usine) reste le meilleur de « B4 » aux attendus écrits ; H80 n'a rien trouvé de mieux.

**Ce que le balayage établit** :
- **Le mélange ne voit pas la longueur** : de 0,16 à 0,32 s, le log-mel tient à 7,10 au centième. Ce qui bouge, c'est
  la répartition du grave : le sub est à son meilleur à 0,20 s (0,22 dB — le meilleur sub du chantier sur ce morceau),
  le bas-médium à 0,32 s ; un kick plus court vide le sub (10,95 à 0,05 s) malgré son niveau recalé.
- **Le stem et le mélange se CONTREDISENT.** Aux charlestons d'entre les kicks — une pièce sans grave, donc la lecture
  la plus propre de la queue du kick —, le stem veut un kick de 0,05 à 0,08 s (−1,9 · +3,2 dB) ; le mélange, lui, perd
  alors 6 à 11 dB de sub. Aux caisses, le stem veut 0,12 s (−0,1 dB) : mais la caisse de l'original porte son propre
  grave, que celle du 909 n'a pas (§ 39.2), et c'est la queue du kick qui le remplace — une compensation, pas un accord.
- **Le critère de H77 ne voit pas une longueur** : sur ces sept points les écarts au stem tiennent dans 0,35 dB — sous
  le demi-décibel que § 38.2 disait muet — et quatre paires sur dix s'y rangent à l'envers. Il a rangé juste les
  machines (H78) et les niveaux (H79) ; il ne règle pas une enveloppe.
- **Non expliqué** : le 150-500 Hz éloigné aux frappes du charleston à TOUTES les décroissances, y compris quand le
  kick est éteint depuis longtemps (0,05 s). Ce n'est pas sa queue. Aucune mesure ici ne dit ce que c'est.

**Ce que la mesure ne dit pas, et qu'elle désigne** (écrit d'avance au § 41 : « un débord peut être un défaut de la
cible et non du rendu ») : le grave que le mélange original porte entre les kicks, et que le stem de batterie n'a pas,
est quelque part — dans le stem de BASSE de la séparation, vraisemblablement — et dans le rendu c'est la queue du kick
qui en tient lieu. Si c'est cela, le kick long de H79 ne ressemble pas au kick de l'original : il COMPENSE une basse
trop faible (basse à −1,3 dB, sub en défaut dès que le kick raccourcit). **La suite** (H81, à écrire AVANT sa mesure) :
où est le grave d'entre les kicks dans l'original — stem par stem — et qui le porte dans le rendu, piste par piste ;
aucune machine ne se règle avant.

## 42. H81 — le grave d'entre les kicks : où il est dans l'original, stem par stem, et qui le porte dans le rendu, piste par piste (écrite AVANT son instrument et sa mesure, 11/11/2026, 10 h 30)

**Ce que H80 a appris** (§ 41.2) : aux charlestons d'entre les kicks, le stem de batterie veut un kick de 0,05 à
0,08 s ; le mélange, lui, perd alors 6 à 11 dB de sub. Les deux juges ne peuvent avoir raison ensemble que si le grave
d'entre les kicks de l'original est AILLEURS que dans sa batterie — et que, dans le rendu, c'est la queue du kick qui en
tient lieu. Aucun rendu de machine ici : une mesure de répartition.

**L'instrument, à écrire** (`analyse/mesure_h81.py`, son test, `reconstruction/travail/reload-h81.sh` rejouable) :
l'énergie moyenne par fenêtre (10 ms avant, 100 ms après — celles de H74) à 30-150 Hz et à 150-500 Hz, sur DEUX
populations dites et comptées — les frappes du kick (le témoin : là, c'est le kick qui doit porter le grave) et les
frappes du charleston à 100 ms au moins de tout kick (`mesure_h80.frappes_hors`, 855 sur 892) —, pour :
- **l'original** : le mélange, et ses quatre stems (`drums`, `bass`, `other`, `vocals`) ;
- **le rendu au point de H79** (kick du 909 à 0,32 s, f\* 0,9952 : `reload-h79/b4/essai-0.9952.wav`) : le mélange, la
  batterie (la somme des solos à leurs facteurs, contrôlée en H78), et les pistes `bass`, `guitar`, `other`, rendues
  seules (trois rendus, moteur `reload-h70`).
- **Deux contrôles avant toute lecture** : la somme des quatre stems doit rendre le mélange original, et la somme de la
  batterie et des trois pistes le mélange rendu, à 1 dB près dans chaque bande et chaque population. Sinon REFUS — les
  parts ne s'additionnent pas et aucune ne se lit.

**L'HYPOTHÈSE H81** : entre les kicks, le grave de l'original est porté par sa BASSE ; le rendu a la basse trop faible
à cet endroit, et sa batterie y tient lieu de basse.

| # | mesure, à 30-150 Hz, aux charlestons d'entre les kicks | réussite | échec |
|---|---|---|---|
| 0 | les deux contrôles de somme | à 1 dB | au-delà : REFUS |
| 1 | original : stem `bass` rapporté au stem `drums` | ≥ +6 dB (la basse porte) | ≤ 0 dB (la batterie porte autant ou plus) |
| 2 | rendu : batterie rapportée à la piste `bass` | ≥ 0 dB (la batterie y vaut au moins la basse) | ≤ −6 dB (la basse porte, comme dans l'original) |
| 3 | la piste `bass` rendue rapportée au stem `bass` | ≤ −3 dB (la basse rendue manque) | ≥ −1 dB (elle ne manque pas) |
| 4 | le TÉMOIN, aux frappes du kick : stem `drums` rapporté au stem `bass` | ≥ 0 dB (là, le kick porte) | < −6 dB (la batterie ne porte le grave nulle part : la mesure ne distingue rien) |
| 5 | relevés : la table entière (deux bandes, deux populations, dix signaux), le mélange rendu rapporté au mélange original | — | — |

**La règle de décision** : 1, 2 et 3 tenus (4 tenu) → le kick long de H79 COMPENSE une basse trop faible entre les
frappes ; aucune décroissance de kick ne se choisit avant que la basse soit à son niveau, et la suite (H82) porte sur
la piste `bass` — jouée par `vsm.clavinet` dans ce projet —, pas sur la batterie ; 1 en échec → le grave d'entre les
kicks est dans le stem de batterie lui-même, le débord de H80 n'en est pas un, et c'est le stem contre le mélange qu'il
faut expliquer autrement ; 1 tenu, 3 en échec → la basse rendue ne manque pas, et la contradiction de H80 n'est pas une
affaire de niveau de basse (timbre de la basse, ou du kick à 55 Hz) ; 4 en échec → REFUS de conclure.

**Ce que la mesure ne pourra PAS dire**, écrit d'avance : les stems sont des estimations — un séparateur peut donner à
`bass` la queue d'un kick, et l'attendu 1 serait alors tenu pour une mauvaise raison ; seul le contrôle par les
MÉLANGES (relevé 5 : rendu contre original, sans séparation) n'en dépend pas ; un morceau.

### 42.1 L'instrument de H81, AVANT toute mesure (11/11/2026, 10 h 32)

`analyse/mesure_h81.py` (`energies`, `rapport_db`, `controle_de_somme`) et `analyse/tests/test_mesure_h81.py`, quatre
tests : un kick court toutes les 0,5 s et une « basse » tenue (60 Hz) — aux frappes du kick le kick porte le grave
(plus de +6 dB), entre elles c'est la basse (moins de −6 dB) ; la somme des parties rend le mélange, et la même somme
sans la basse est refusée, avec le manque lu entre les kicks et dans le grave seulement ; un signal muet rend « rien »
(pas 0 dB) et fait échouer le contrôle ; une population vide est un refus. **Vu rouge sur quatre mutants** : une seule
population pour tous (2 rouges) ; le contrôle de somme toujours tenu (2) ; un signal nul rendu à 0 dB (1) ; la
population vide admise (1). `ruff` et `mypy` propres.

**`main()` passée à blanc**, sur UN fichier neutre donné pour tous les signaux (le mélange de H75) : la table ne rend
que des zéros — rien du morceau n'est lu — et le passage a montré un défaut : deux contrôles de somme du même nom
s'effaçaient l'un l'autre. Désormais un REFUS. Populations relevées au passage : 763 frappes de kick, 855 charlestons
d'entre les kicks sur 892.

`reconstruction/travail/reload-h81.sh` : les trois pistes hors batterie rendues seules dans le projet de H79 (mêmes
machines, f\* relu dans `h79.json`), puis l'instrument.

### 42.2 Verdict de H81 (11/11/2026, mesuré de 10 h 32 à 10 h 33, écrit à 10 h 34) : TENUE — entre les kicks le grave de l'original est sa basse, la basse rendue en manque de 7 dB, et la queue du kick du 909 la remplace, avec 4 dB de trop

`reconstruction/travail/reload-h81.sh` (rejouable), moteur `1eff3b6d`. Populations : 763 frappes de kick ; 855
charlestons d'entre les kicks sur 892. **La table entière**, en dB rapportés au mélange ORIGINAL de la même population
et de la même bande :

| signal | aux kicks · 30-150 | aux kicks · 150-500 | entre les kicks · 30-150 | entre les kicks · 150-500 |
|---|---|---|---|---|
| original : stem `drums` | −0,5 | −0,5 | **−11,0** | −7,7 |
| original : stem `bass` | −11,3 | −20,0 | **−1,0** | −8,6 |
| original : stem `other` | −23,4 | −15,5 | −14,6 | −3,4 |
| original : stem `vocals` | −76,2 | −60,5 | −69,0 | −56,0 |
| **rendu de H79** (mélange) | −0,4 | +2,3 | **+4,3** | +2,5 |
| rendu : batterie (sommée) | −0,5 | +2,0 | **+4,0** | −5,7 |
| rendu : piste `bass` | −17,1 | −11,8 | **−8,2** | −1,0 |
| rendu : piste `guitar` | −32,7 | −26,0 | −25,7 | −17,5 |
| rendu : piste `other` | −21,1 | −12,3 | −12,0 | −1,4 |

| # | verdict, à 30-150 Hz |
|---|---|
| 0 | **tenu** : la somme des quatre stems rend l'original à 0,26 dB au pire ; celle de la batterie et des trois pistes rend le mélange rendu à 0,00 dB |
| 1 | **tenu** : entre les kicks, stem `bass` à **+10,0 dB** du stem `drums` — la basse porte |
| 2 | **tenu** : dans le rendu, la batterie à **+12,2 dB** de la piste `bass` — c'est la batterie qui porte |
| 3 | **tenu** : la piste `bass` rendue à **−7,2 dB** du stem `bass` |
| 4 | **tenu** (le témoin) : aux kicks, stem `drums` à +10,8 dB du stem `bass` — là, le kick porte, dans l'original comme dans le rendu (−0,5 · −0,5) |

**La règle** : « 1, 2 et 3 tenus (4 tenu) → le kick long de H79 COMPENSE une basse trop faible entre les frappes ;
aucune décroissance de kick ne se choisit avant que la basse soit à son niveau, et la suite porte sur la piste `bass`,
pas sur la batterie ». **Elle s'applique.**

**Ce que la table dit de plus** :
- **Par les MÉLANGES seuls, sans séparation** (le relevé qui ne dépend d'aucun stem) : aux kicks, le rendu a le grave
  de l'original à 0,4 dB ; entre les kicks il en a **4,3 dB de TROP**. La compensation dépasse : la queue du 909 (0,32 s)
  met entre les frappes plus de grave que la basse de l'original n'en avait. C'est ce que le sub de H80 disait à sa
  façon (meilleur à 0,20 s qu'à 0,32).
- **La contradiction de H80 est levée** : la batterie rendue est à +15,0 dB du stem `drums` entre les kicks — le
  débord de H80, retrouvé au centième par un autre instrument — et le stem avait raison de vouloir un kick court ; le
  mélange avait raison de vouloir du grave à cet endroit. Ils ne parlaient pas du même instrument.
- **La basse rendue n'est pas seulement trop basse : son spectre est déplacé.** À 30-150 Hz elle manque de 7,2 dB
  (5,8 aux kicks) ; à 150-500 Hz elle a **7,6 dB de TROP** (8,2 aux kicks). La piste `bass` de ce projet est jouée par
  `vsm.clavinet` : trop peu de fondamentale, trop d'harmoniques — un timbre, qu'un volume ne corrigera pas (la leçon de
  H74, sur un autre instrument). Le 150-500 Hz en excès se lit aussi au mélange (+2,3 · +2,5 dB).
- **Les chiffres de la batterie valident H79 là où il fallait** : aux kicks, batterie rendue et stem `drums` sont à
  0,0 dB l'un de l'autre à 30-150 Hz — le calage f\* fait ce qu'il dit.

**Rien n'entre** — H81 ne règle rien, elle désigne. **La suite** (H82, à écrire AVANT sa mesure) : la MACHINE de la
piste `bass` de « B4 », choisie contre le stem `bass` par la méthode qui a marché pour la batterie — le critère de
H77 (à vérifier sur ce stem avant de s'en servir, comme il l'a été sur l'autre), le niveau calé ensuite (H79) — puis,
la basse à sa place, la décroissance du kick rejouée (H80), qui aura alors un stem et un mélange d'accord.

**Ce que la mesure ne dit pas** (écrit d'avance, rappelé) : les parts par stem sont celles d'une séparation — mais le
relevé par les mélanges (+4,3 dB entre les kicks) n'en dépend pas, et il va dans le même sens ; un morceau.

## 43. H82 — la MACHINE de la piste `bass` de « B4 », choisie contre le stem `bass` (écrite AVANT son instrument et sa mesure, 11/11/2026, 11 h 09)

**Ce que H81 a appris** (§ 42.2) : la basse rendue (`vsm.clavinet`) manque de 7,2 dB à 30-150 Hz et en a 7,6 de trop à
150-500 Hz. **Une variable : la machine de la piste `bass`.** Le reste est le projet de H79 (TR-909, f\* 0,9952).

**L'instrument** (`analyse/mesure_h82.py`, son test, `reconstruction/travail/reload-h82.py` rejouable) :
- **Candidats**, à leurs réglages d'usine : `vsm.tb303`, `vsm.minimoog`, `vsm.sh101`, `vsm.juno106`, `vsm.ms20`,
  `vsm.arpodyssey`, `vsm.dx7`, `vsm.prophet` ; le TÉMOIN : le clavinet du projet, ses réglages gardés.
- **Le niveau, conséquence et non variable** : chaque solo est ramené à l'ÉNERGIE du stem `bass` sur le morceau entier
  (un gain, publié ; le témoin aussi, pour que tous soient jugés à niveau égal). Un solo muet (−60 dB) est écarté et dit.
- **Le critère** : le log-mel du solo ainsi calé contre le stem `bass` (`mesure_h77.logmel_au_stem`). Choix : le plus
  petit, s'il gagne au moins 0,5 dB sur le témoin calé.
- **Le critère n'a pas été vérifié sur ce stem** : les NEUF mélanges sont donc rendus et jugés, et la concordance
  stem / mélange (§ 38) est lue sur eux avant de croire le choix.

**L'HYPOTHÈSE H82** : une machine du parc est plus près du stem `bass` que le clavinet, et le mélange suit.

| # | mesure, « B4 » | réussite | échec |
|---|---|---|---|
| 0 | le témoin de la course : clavinet à son volume du projet | rend H79 (7,10) au centième | sinon REFUS |
| 1 | concordance stem / mélange sur les paires tranchées des neuf points calés | toutes | une discordante de plus de 0,5 dB au mélange |
| 2 | log-mel du mélange, machine choisie | ≤ 7,00 (H79 − 0,10) | ≥ 7,10 |
| 3 | \|médian\| sub · basse · bas-médium | chacune à moins de 0,5 dB de H79 (1,89 · 1,39 · 1,56), ou meilleure | l'une pire de plus de 1,5 dB |
| 4 | piste `bass` contre stem `bass`, entre les kicks (H81) : 30-150 et 150-500 Hz | toutes deux à moins de 3 dB (−7,2 et +7,6 en H81) | l'une au-delà de 6 dB |
| 5 | relevés : la table entière (gain, log-mel au stem, mélange, bandes) | — | — |

**La règle** : 1 à 4 tenus → la basse est à sa place, H80 (la décroissance du kick) se rejoue sur ce projet ; aucune
machine ne gagne 0,5 dB → aucune machine du parc, à ses réglages d'usine, n'est plus près : le besoin est un réglage
(la coupure du clavinet est à 500 Hz) ou un échantillon ; 1 en échec → le critère ne vaut pas pour ce stem, le choix se
lit au mélange seul ; toute autre combinaison, au plus sévère. **Ce que la mesure ne dira pas** : réglages d'usine ; un
calage sur l'énergie totale ne dit rien du niveau par note ; un morceau ; le stem `bass` est une estimation.

### 43.1 L'instrument de H82, AVANT toute mesure (11/11/2026, 11 h 09)

`analyse/mesure_h82.py` (`gain_d_energie`, `choisir`), deux tests : un solo à ×0,25 reçoit ×4, un solo à −80 dB est
MUET et non multiplié par 10 000, un stem muet est un refus ; le choix veut 0,5 dB. **Vu rouge sur trois mutants** (le
muet non contrôlé ; sans seuil ; le gain en énergie au lieu d'amplitude), un rouge chacun. `ruff`, `mypy` propres. Le
reste est réemployé tel quel : critère de H77, populations de H80, énergies de H81.
`reconstruction/travail/reload-h82.py` : la course s'arrête si son témoin ne rejoue pas H79.

### 43.2 Verdict de H82 (11/11/2026, écrit à 11 h 22) : le stem choisit `vsm.tb303` et le grave de la basse revient à 0,3 dB — mais 2 en ÉCHEC : le log-mel du mélange ne voit pas la basse

`reconstruction/travail/reload-h82.py` (rejouable ; `reload-h82/h82.json`). Aucun solo muet. **La table entière** :

| machine de la piste `bass` | gain | au stem | mélange | \|médian\| sub · basse · bas-médium | aigus | entre les kicks, piste / stem : 30-150 · 150-500 |
|---|---|---|---|---|---|---|
| `vsm.clavinet` (témoin, calé) | 1,179 | 15,95 | 7,13 | 1,85 · 1,41 · 1,61 | 1,52 | −5,7 · +9,0 |
| **`vsm.tb303`** (choisie) | 0,422 | **13,76** | 7,10 | 2,12 · 1,23 · 1,15 | 1,65 | **+0,3** · +4,8 |
| `vsm.minimoog` | 0,264 | 14,66 | 7,10 | 2,13 · 1,25 · 1,24 | 1,65 | −0,4 · +4,9 |
| `vsm.sh101` | 0,117 | 17,86 | 7,10 | 2,37 · 1,44 · 1,15 | 1,45 | −1,6 · +4,5 |
| `vsm.juno106` | 0,293 | 18,85 | 7,15 | 1,89 · 1,14 · 1,25 | 1,49 | −1,3 · +5,6 |
| `vsm.ms20` | 0,148 | 22,43 | 7,16 | 2,04 · 1,30 · 1,26 | 1,33 | −1,8 · +5,9 |
| `vsm.arpodyssey` | 0,154 | 18,71 | 7,14 | 2,04 · 1,26 · 1,21 | 1,41 | −1,7 · +5,5 |
| `vsm.dx7` | 0,145 | 15,33 | 7,39 | 1,69 · 1,43 · 1,52 | 1,56 | −7,0 · +8,4 |
| `vsm.prophet` | 0,248 | 16,37 | **7,08** | 2,13 · 1,24 · 1,14 | 1,55 | −0,6 · +4,2 |

| # | verdict |
|---|---|
| 0 | **tenu** : le témoin de la course rend 7,10 |
| 1 | **entre les deux** : 8 paires tranchées sur 36, 2 concordantes, 6 discordantes — toutes tiennent au seul `vsm.dx7`, aucune au-delà de 0,5 dB au mélange |
| 2 | **en ÉCHEC** : 7,10 avec `vsm.tb303` — pas un centième de gagné sur H79 |
| 3 | **tenu** : sub 2,12 (+0,23), basse 1,23 et bas-médium 1,15 meilleurs |
| 4 | **entre les deux** : 30-150 Hz à +0,3 dB (−7,2 en H81) ; 150-500 Hz à +4,8 dB (+7,6) |

**La règle**, au plus sévère : H82 n'est pas tenue comme écrite, rien n'entre. **Ce qu'elle établit** : huit machines
sur neuf tiennent dans 0,08 dB de log-mel au mélange pendant que le critère au stem varie de 8,7 dB — le juge du
mélange est AVEUGLE à la machine de la basse, et un attendu au log-mel (le 2) ne pouvait pas la départager. Ce qui la
départage, ce sont les bandes : avec le TB-303 (ou le Minimoog, ou le Prophet) le grave de la basse entre les kicks
revient au stem à moins de 1 dB, et le bas-médium du mélange passe de 1,56 à 1,15. Le 150-500 Hz reste 4 à 6 dB trop
fort avec toutes les machines : non expliqué (les notes transcrites ? une octave ? — pas mesuré). Le sub du mélange ne
s'améliore pas (2,12) : le kick long de H79 est toujours là, et il n'a plus de basse à remplacer.

**La suite** (H83, à écrire AVANT sa mesure) : la basse au TB-303, la décroissance du kick rejouée (H80), jugée cette
fois par ce qui voit le défaut — le grave du mélange entre les kicks, rendu contre original (le relevé de H81 : +4,3 dB)
— et non par le log-mel, que H80 et H82 montrent plat sur ces deux variables.

## 44. H83 — la basse au TB-303, la décroissance du kick rejouée, jugée par le grave du MÉLANGE entre les kicks (écrite AVANT sa mesure, 11/11/2026, 12 h 00)

**Ce que H80 à H82 ont appris** : le log-mel est plat sur la longueur du kick et sur la machine de la basse ; ce qui
voit le défaut est le grave du mélange entre les kicks, rendu contre original, sans séparation (H81 : +4,3 dB à
30-150 Hz ; −0,4 aux kicks). **Une variable : `drum.kick.decay` du TR-909**, sur le projet de H82 (`bass` au
`vsm.tb303`, gain 0,422). Les sept points de H80, chacun à son f\* de H80 (relu dans `reload-h80/b4/h80.json` : il se
cale sur le stem `drums`, que la basse ne touche pas).

**L'instrument** : aucun code neuf — `mesure_h81.energies` sur les deux MÉLANGES (populations de H81 : 763 kicks, 855
charlestons d'entre les kicks), `mesure_h80.choisir` (le plus petit écart absolu au dixième ; à égalité la plus longue),
l'outil du § 0. `reconstruction/travail/reload-h83.py`, rejouable ; son témoin : le point 0,32 s doit rejouer le
mélange TB-303 de H82 (7,10 · 2,12 · 1,23 · 1,15).

**L'HYPOTHÈSE H83** : la basse à sa place, une décroissance plus courte que 0,32 s ramène le grave d'entre les kicks à
l'original SANS vider le sub, ce que H80 ne pouvait pas.

| # | mesure, à la décroissance choisie | réussite | échec |
|---|---|---|---|
| 0 | le témoin : 0,32 s rejoue H82 | au centième | sinon REFUS |
| 1 | mélange rendu / original, 30-150 Hz, entre les kicks | à 1,5 dB (H79 : +4,3) | au-delà de 3 dB |
| 2 | le même, aux kicks | à 1,5 dB | au-delà de 3 dB |
| 3 | \|médian\| sub · basse · bas-médium | chacune à moins de 0,5 dB de H82 (2,12 · 1,23 · 1,15), ou meilleure | l'une pire de plus de 1,5 dB |
| 4 | log-mel | ≤ 7,20 | > 7,37 |
| 5 | relevés : le balayage entier ; la décroissance choisie contre celle que le stem voulait en H80 (0,05 à 0,12 s) | — | — |

**La règle** : 1 à 4 tenus → ce projet est le meilleur point de « B4 », et la suite est de rejouer la méthode entière
(machines de batterie, niveau, basse, longueur) sur un TROISIÈME morceau avant toute D-phase ; sinon, au plus sévère.
**Dit d'avance** : l'attendu 1 est le critère du choix lui-même — il sera tenu ou non par construction du balayage ; ce
sont 2, 3 et 4 qui jugent. Un morceau ; le 150-500 Hz de la basse, trop fort de 4,8 dB, n'est pas touché.

### 44.1 Verdict de H83 (11/11/2026, écrit à 12 h 06) : NON TENUE — le kick court remet le grave d'entre les kicks à l'original (−0,2 dB) et défait le sub, la basse et le bas-médium : c'est le TIMBRE du kick, pas sa longueur

`reconstruction/travail/reload-h83.py` (rejouable ; `reload-h83/h83.json`). **Le balayage entier** :

| décroissance | f\* | rendu / original, 30-150 Hz : aux kicks · entre | le même, 150-500 Hz : aux kicks · entre | log-mel | \|médian\| sub · basse · bas-médium | aigus |
|---|---|---|---|---|---|---|
| 0,32 s (H82) | 0,9952 | −0,2 · **+5,4** | +2,2 · +1,5 | 7,10 | 2,12 · 1,23 · **1,15** | 1,65 |
| 0,25 s | 1,0666 | −0,2 · +4,4 | +2,6 · +1,3 | **7,09** | 1,35 · **1,22** · 1,59 | 1,54 |
| 0,20 s | 1,1496 | −0,2 · +3,4 | +3,1 · +1,2 | **7,09** | **0,52** · 1,26 · 2,05 | **1,34** |
| 0,16 s | 1,2579 | −0,2 · +2,4 | +3,6 · +1,1 | 7,10 | 0,66 · 1,35 · 2,51 | 1,51 |
| 0,12 s | 1,4531 | −0,2 · +1,3 | +4,4 · +1,1 | 7,12 | 2,29 · 1,66 · 3,17 | 1,62 |
| 0,08 s | 1,9152 | −0,2 · +0,3 | +6,1 · +1,2 | 7,20 | 4,57 · 2,40 · 4,09 | 2,01 |
| **0,05 s (choisie)** | 3,0527 | −0,2 · **−0,2** | **+8,9** · +1,7 | 7,46 | 6,70 · 4,29 · 5,07 | 2,27 |

| # | verdict, à 0,05 s |
|---|---|
| 0 | **tenu** : 0,32 s rejoue H82 au centième |
| 1 | **tenu** (par construction) : −0,2 dB |
| 2 | **tenu** : −0,2 dB |
| 3 | **en ÉCHEC** : sub +4,58, basse +3,06, bas-médium +3,92 sur H82 |
| 4 | **en ÉCHEC** : 7,46 > 7,37 |

**Rien n'entre.** Ce que le balayage établit :
- **Le mélange et le stem sont maintenant D'ACCORD sur la longueur** : jugé au mélange, sans séparation, le grave
  d'entre les kicks demande 0,05 à 0,08 s — ce que le stem de batterie demandait en H80. La contradiction de H80 venait
  bien de la basse (H81), et elle est levée.
- **Mais ce kick-là, raccourci, n'est plus le bon kick** : pour garder son grave à ses frappes (f\* le tient à −0,2 dB
  aux sept points), il faut le monter de 9,7 dB, et son corps monte avec — +8,9 dB de trop à 150-500 Hz aux kicks —
  tandis que le sub se vide (6,70). Le kick du TR-909 à 55 Hz ne peut pas être à la fois court, grave et sans corps :
  l'original l'est. C'est un TIMBRE (accord, balayage, attaque), comme H67-H70 l'avaient trouvé sur « Reload ».
- **Le meilleur compromis publié** : 0,20 s — sub 0,52, log-mel 7,09, aigus 1,34 — au prix du bas-médium (2,05) et de
  3,4 dB de grave en trop entre les kicks. Il n'est PAS adopté : il n'était pas le choix écrit.
- Avec la basse au TB-303, le point 0,32 s a 5,4 dB de trop entre les kicks (4,3 en H81) : la basse revenue s'AJOUTE
  à la queue du kick — attendu, et c'est pourquoi H82 seule ne pouvait pas améliorer le sub.

**La suite** : le kick de « B4 » demande ce que H69-H70 ont fait pour celui de « Reload » — le relever sur ses frappes
(accord, balayage, tenue : `mesure_h69`) et régler la machine dessus, ou conclure à un échantillon
(`CDC-multisample.md`). H84 s'écrira là-dessus, AVANT sa mesure. **Ce que la mesure ne dit pas** : un morceau ; un seul
réglage du 909 a bougé (son accord et son attaque sont à l'usine).

## 45. H84 — le kick de « B4 » relevé sur ses frappes, et le TR-909 réglé dessus : accord, décroissance, attaque (écrite AVANT son instrument et sa mesure, 11/11/2026, 12 h 16)

**Ce que H83 a appris** (§ 44.1) : la longueur seule ne suffit pas — le kick du 909 raccourci perd son sub et gonfle son
corps. Le projet reste celui de H82 (basse au TB-303) ; ce qui bouge : les trois réglages du kick du TR-909.

**L'instrument** (`analyse/mesure_h84.py`, son test, `reconstruction/travail/reload-h84.py` rejouable) :
- **Le kick de l'original** : la moyenne synchrone de ses frappes isolées dans le stem `drums` (`mesure_h69.retenir`,
  `mesure_h70.kick_moyen` — le chemin de H69-H70, morceau entier ; le nombre de frappes retenues est publié, moins de
  vingt est un refus).
- **La grille ENTIÈRE** : accord 35 · 45 · 55 · 65 · 80 Hz × décroissance 0,05 · 0,08 · 0,12 · 0,20 · 0,32 s × attaque
  0 · 0,5 — cinquante solos rendus, le kick moyen de chacun pris aux MÊMES frappes.
- **Le critère** (`ecart_tf`) : l'écart moyen, en dB, des deux kicks moyens en temps-fréquence — 30 à 500 Hz, 0 à 300 ms,
  chacun normalisé à son énergie (le niveau n'est pas jugé : il se cale ensuite, f\* de H79), plancher à −40 dB sous le
  maximum. Il voit l'accord, la longueur et le corps ensemble. Le choix : le plus petit.
- **Le jugement** : le réglage choisi et les deux suivants, chacun à son f\*, mélange rendu et jugé (outil du § 0 ;
  grave du mélange rendu / original aux kicks et entre les kicks, H81). Le point d'usine (55 Hz · 0,32 s · 0,5) est
  dans la grille : son mélange est celui de H82, le témoin.

**L'HYPOTHÈSE H84** : un réglage du kick du TR-909 est à la fois court, grave et sans excès de corps — ce que la seule
décroissance ne donnait pas.

| # | mesure, au réglage choisi | réussite | échec |
|---|---|---|---|
| 1 | `ecart_tf` | au moins 1 dB sous celui du point d'usine | à moins de 0,3 dB |
| 2 | mélange rendu / original, 30-150 Hz : aux kicks ET entre les kicks | tous deux à 1,5 dB (H82 : −0,2 · +5,4) | l'un au-delà de 3 dB |
| 3 | le même à 150-500 Hz, aux kicks | ≤ +3,0 dB (H82 +2,2 ; H83 à 0,05 s +8,9) | > +5 dB |
| 4 | \|médian\| sub · basse · bas-médium | chacune à moins de 0,5 dB de H82 (2,12 · 1,23 · 1,15), ou meilleure | l'une pire de plus de 1,5 dB |
| 5 | log-mel | ≤ 7,20 | > 7,37 |
| 6 | relevés : la grille entière, les frappes retenues, les trois mélanges | — | — |

**La règle** : 1 à 5 tenus → le kick de « B4 » est réglé, et la méthode entière se rejoue sur un troisième morceau ;
1 tenu, 2 à 5 non → le critère sur le kick moyen ne prédit pas le mélange, à dire ; 1 en échec → aucun réglage du 909
n'approche ce kick : c'est un échantillon (`CDC-multisample.md`). **Ce que la mesure ne dira pas** : le kick moyen
vient du stem, sous 1 kHz ; trois réglages sur quatre du kick (son niveau interne reste à l'usine) ; un morceau.

### 45.1 L'instrument de H84, AVANT toute mesure (11/11/2026, 12 h 17)

`analyse/mesure_h84.py` (`carte_db`, `ecart_tf`), trois tests sur la réplique du kick du TR-808 de H69 : un kick contre
lui-même rend 0, un gain ne change rien, 0,05 s pour 0,12 et 80 Hz pour 45 rendent plus de 2 dB, un voisin (47 Hz ·
0,13 s) moins de la moitié ; le bon réglage est retrouvé dans une grille de douze ; un signal sans grave est un refus.
**Vu rouge sur trois mutants** (sans normalisation ; le refus ôté ; 9 ms lus au lieu de 300), un rouge chacun. `ruff`,
`mypy` propres. `reconstruction/travail/reload-h84.py` : le solo du kick est rendu au facteur de H79, et ramené à ×1
avant le calcul de f\*.

### 45.2 Verdict de H84 (11/11/2026, mesuré de 12 h 16 à 12 h 31, écrit à 12 h 32) : NON TENUE — le kick moyen désigne bien un kick court et grave (45 Hz · 0,08 s), le grave du mélange est à l'original aux kicks ET entre eux, mais le bas-médium casse ; et le critère ne voit pas l'attaque

`reconstruction/travail/reload-h84.py` (rejouable ; `reload-h84/h84.json`). Frappes retenues : 264 sur 763 (169 près
d'une caisse, 330 près d'un autre kick). **La grille entière** (`ecart_tf`, dB ; l'attaque 0 et 0,5 rendent le MÊME
chiffre au centième à 49 points sur 50 — une seule colonne) :

| accord | 0,05 s | 0,08 s | 0,12 s | 0,20 s | 0,32 s |
|---|---|---|---|---|---|
| 35 Hz | 5,44 | 4,47 | 4,51 | 5,01 | 5,44 |
| 45 Hz | 4,75 | **4,38** | 4,98 | 5,63 | 6,01 |
| 55 Hz | 5,11 | 5,55 | 6,18 | 6,82 | **7,20** (usine) |
| 65 Hz | 6,01 | 7,03 | 7,75 | 8,37 | 8,73 |
| 80 Hz | 7,61 | 8,91 | 9,56 | 10,19 | 10,55 |

| réglage (accord · décroissance · attaque) | tf | f\* | log-mel | \|médian\| sub · basse · bas-médium | aigus | rendu / original 30-150 Hz : aux kicks · entre | 150-500 Hz aux kicks |
|---|---|---|---|---|---|---|---|
| usine 55 · 0,32 · 0,5 (H82) | 7,20 | 0,9952 | 7,10 | 2,12 · 1,23 · 1,15 | 1,65 | −0,2 · +5,4 | +2,2 |
| **45 · 0,08 · 0 (choisi)** | 4,38 | 1,6292 | 7,32 | 1,98 · 2,14 · 3,44 | 1,92 | −0,2 · **+0,1** | +4,1 |
| 45 · 0,08 · 0,5 (à égalité) | 4,38 | 1,6292 | 7,13 | 2,02 · 2,19 · 3,40 | 2,64 | −0,2 · +0,1 | +4,1 |
| 35 · 0,08 · 0 (troisième) | 4,47 | 1,3749 | 7,35 | **0,50** · 1,64 · 2,21 | 2,10 | −0,2 · −0,1 | **+1,6** |

| # | verdict, au réglage choisi |
|---|---|
| 1 | **tenu** : 4,38, soit 2,82 dB sous le point d'usine |
| 2 | **tenu** : −0,2 dB aux kicks, +0,1 entre eux (H82 : +5,4) |
| 3 | **entre les deux** : +4,1 dB |
| 4 | **en ÉCHEC** : bas-médium 3,44 (+2,29 sur H82) ; basse 2,14 (+0,91) |
| 5 | **entre les deux** : 7,32 |

**La règle**, au plus sévère : non tenue, rien n'entre. **Ce que la mesure établit** : (a) le relevé du kick va dans le
sens de H83 et de H69-H70 — court, et plus grave que l'usine ; (b) **le critère est AVEUGLE à l'attaque** : le kick
moyen est lu sous 1 kHz, l'attaque du 909 est un clic au-dessus — et elle vaut 0,19 dB de log-mel au mélange (7,32 →
7,13) ; l'égalité entre 0 et 0,5 a été tranchée par l'ordre des noms, pas par une mesure ; (c) 0,1 dB d'écart au
critère (45 contre 35 Hz) sépare deux mélanges très différents : à 35 Hz le sub est à 0,50, le corps aux kicks à +1,6 dB
— le critère range les régions de la grille, pas deux voisins. **Le troisième point est le plus prometteur et n'a été
rendu qu'à l'attaque 0.**

## 46. H85 — UNE variable : l'attaque du kick, au point 35 Hz · 0,08 s (écrite AVANT sa mesure, 11/11/2026, 12 h 32)

Projet de H84, kick du TR-909 à 35 Hz · 0,08 s, f\* 1,3749 (celui de H84 : l'attaque ne touche pas le 30-150 Hz — à
vérifier par l'attendu 2). Points : attaque 0 (H84, le témoin : doit rejouer 7,35), **0,5 (le réglage d'usine, le
point JUGÉ)** et 1,0 (relevé). Aucun code neuf (`reconstruction/travail/reload-h85.py`).

| # | mesure, à l'attaque 0,5 | réussite | échec |
|---|---|---|---|
| 0 | le témoin : l'attaque 0 rejoue H84 | au centième | sinon REFUS |
| 1 | log-mel | ≤ 7,20 | > 7,37 |
| 2 | mélange rendu / original, 30-150 Hz, aux kicks et entre | tous deux à 1,5 dB | l'un au-delà de 3 dB |
| 3 | le même à 150-500 Hz, aux kicks | ≤ +3,0 dB | > +5 dB |
| 4 | \|médian\| sub · basse · bas-médium | chacune à moins de 0,5 dB de H82 (2,12 · 1,23 · 1,15), ou meilleure | l'une pire de plus de 1,5 dB |

**La règle** : 1 à 4 tenus → le kick de « B4 » est réglé (35 Hz · 0,08 s · 0,5), la méthode se rejoue sur un troisième
morceau ; sinon au plus sévère, et le point est publié à côté de H79 et de H82. **Dit d'avance** : ce point a été
choisi en REGARDANT les mélanges de H84 — c'est un réglage à la main, pas un choix d'instrument ; il ne vaut que comme
point, et une règle devra le retrouver seule.

### 46.1 Verdict de H85 (11/11/2026, écrit à 12 h 37) : 1, 2 et 3 TENUS, 4 entre les deux — le meilleur grave de « B4 », pas encore un kick réglé

`reconstruction/travail/reload-h85.py` (rejouable ; `reload-h85/h85.json`). Le témoin (attaque 0) rejoue H84 au centième.

| point de « B4 » | log-mel | \|médian\| sub · basse · bas-médium | aigus | rendu / original 30-150 Hz : aux kicks · entre | 150-500 Hz aux kicks |
|---|---|---|---|---|---|
| H75 (TR-808, niveaux résolus) | 8,42 | 1,48 · 2,29 · 1,73 | 3,89 | — | — |
| H79 (TR-909, kick à f\*) | 7,10 | 1,89 · 1,39 · 1,56 | 1,52 | −0,4 · +4,3 | +2,3 |
| H82 (+ basse au TB-303) | 7,10 | 2,12 · 1,23 · 1,15 | 1,65 | −0,2 · +5,4 | +2,2 |
| H85 · attaque 0 | 7,35 | 0,50 · 1,64 · 2,21 | 2,10 | −0,2 · −0,1 | +1,6 |
| **H85 · attaque 0,5 (jugé)** | **7,12** | **0,49** · 1,66 · 2,17 | 3,20 | −0,2 · **−0,1** | **+1,6** |
| H85 · attaque 1,0 | 7,19 | 0,43 · 1,74 · 2,08 | 6,05 | −0,2 · −0,1 | +1,6 |

| # | verdict, à l'attaque 0,5 |
|---|---|
| 1 | **tenu** : 7,12 ≤ 7,20 |
| 2 | **tenu** : −0,2 dB aux kicks, −0,1 dB entre eux — l'attaque ne touche pas le grave, f\* de H84 reste juste |
| 3 | **tenu** : +1,6 dB |
| 4 | **entre les deux** : sub 0,49 (meilleur, de 1,63 dB) ; basse 1,66 (+0,43) ; bas-médium 2,17 (+1,02) |

**La règle**, au plus sévère : le kick n'est PAS déclaré réglé, rien n'entre. **Ce que le point vaut** : le grave du
mélange est à l'original à 0,2 dB près aux kicks ET entre eux (+5,4 en H82), le sub est le meilleur relevé sur ce
morceau, le log-mel tient à 0,02 dB de H79. **Ce qu'il coûte**, hors attendus et dit : les aigus passent de 1,65 à
3,20 avec l'attaque d'usine (2,10 à l'attaque 0, 6,05 à 1,0) — le clic du 909 est trop brillant pour ce morceau, et le
log-mel et les aigus tirent l'attaque en sens contraires ; le bas-médium reste 1 dB plus loin qu'en H82.

**Où en est « B4 »** après H76-H85 : machines de batterie (H78), niveau du kick (H79), machine de la basse (H82),
kick court et grave (H84-H85) — chaque étape tenue sur ce qu'elle visait, aucune sur tous ses attendus. Trois choses
restent nommées et non expliquées : le 150-500 Hz de la basse, 4 à 6 dB trop fort avec toutes les machines (H82) ; le
bas-médium du mélange ; l'attaque du kick. **La suite** : avant d'affiner davantage un seul morceau — huit réglages
choisis sur « B4 » ne font pas une méthode —, rejouer la chaîne des choix (H78 → H79 → H82 → H84) telle quelle sur
« Reload » et sur un troisième morceau, écrite AVANT sa mesure, pour savoir ce qui s'y retrouve.

## 47. H86 — la chaîne des choix rejouée sur « Reload » : d'abord la question de H81 — qui porte le grave d'entre les kicks ? (écrite AVANT sa mesure, 11/11/2026, 12 h 51)

**Un constat fait en ouvrant le projet, avant toute mesure** : le projet de « Reload » (`reload-h42/temoin`) n'a PAS de
piste de basse — quatre voix `other`, un `piano` (`vsm.minimoog`), la batterie, deux pistes de voix audio — alors que
la séparation a rendu un stem `bass`. H78 rejouée telle quelle est déjà mesurée (§ 39.2 : aucune pièce ne change) ; H82
n'a pas de piste où s'appliquer. Reste la mesure de H81, qui dit si cela compte.

**La mesure** (aucun rendu, aucun code neuf : `mesure_h81.py`) : à 30-150 et 150-500 Hz, aux 555 frappes du kick et aux
charlestons d'entre les kicks (1 613 sur 1 692), le mélange original, ses six stems, et le mélange rendu de H75
(`reload-h75/reload/essai.wav`, log-mel 6,89). Contrôle : la somme des six stems rend l'original à 1 dB.

**L'HYPOTHÈSE H86** : sur « Reload » aussi le grave d'entre les kicks est porté par la basse de l'original — et, faute
de piste, le rendu en MANQUE (là où « B4 » en avait trop).

| # | mesure, à 30-150 Hz | réussite | échec |
|---|---|---|---|
| 0 | contrôle de somme des six stems | à 1 dB | REFUS |
| 1 | entre les kicks : stem `bass` rapporté au stem `drums` | ≥ +6 dB | ≤ 0 dB |
| 2 | entre les kicks : mélange rendu rapporté à l'original | ≤ −3 dB (le rendu manque) | à moins de 1,5 dB (il ne manque pas) |
| 3 | aux kicks : mélange rendu rapporté à l'original | à 1,5 dB (le kick de H70 tient) | au-delà de 3 dB |
| 4 | relevés : la table entière | — | — |

**La règle** : 1 et 2 tenus → « Reload » a une PISTE MANQUANTE, la basse ; c'est un défaut de la chaîne (le stem existe,
aucune piste n'en sort) à instruire avant tout réglage — pourquoi ce stem n'a pas donné de piste ; 1 tenu, 2 en échec
avec un rendu à plus de +3 dB → comme sur « B4 », autre chose tient lieu de basse ; 1 en échec → sur ce morceau la
batterie porte son grave, et l'absence de piste de basse ne coûte rien ici. **Dit d'avance** : parts par stem = parts
d'une séparation ; les attendus 2 et 3, par les mélanges, n'en dépendent pas.

### 47.1 Verdict de H86 (11/11/2026, écrit à 12 h 52) : 1 en ÉCHEC — sur « Reload » la batterie porte son grave, il n'y a pas de basse manquante ; mais le rendu manque de 5,7 dB entre les kicks

`reload-h86/h86.json`. Contrôle de somme tenu (0,49 dB au pire). En dB rapportés au mélange original :

| signal | aux kicks · 30-150 | aux kicks · 150-500 | entre les kicks · 30-150 | entre les kicks · 150-500 |
|---|---|---|---|---|
| stem `drums` | −0,2 | −1,1 | **−1,5** | −11,9 |
| stem `bass` | −38,6 | −46,3 | **−16,1** | −31,6 |
| stem `other` | −20,9 | −7,7 | −16,6 | −1,2 |
| stem `piano` | −43,1 | −21,7 | −34,0 | −20,3 |
| stems `guitar` · `vocals` | −71,4 · −80,0 | −56,4 · −37,5 | −53,9 · −75,0 | −24,2 · −20,5 |
| **mélange rendu (H75)** | −1,2 | +1,4 | **−5,7** | −2,8 |

1 **en ÉCHEC** : entre les kicks le stem `bass` est à −14,6 dB du stem `drums`. 2 : le rendu manque de 5,7 dB entre les
kicks. 3 **tenu** : −1,2 dB aux kicks. **La règle** : « 1 en échec → la batterie porte son grave, l'absence de piste de
basse ne coûte rien ici » — la chaîne a eu raison de ne pas en faire une. **Ce que H81 ne disait pas et que H86 ajoute** :
les deux morceaux ont le MÊME symptôme de signe opposé — « B4 » 4 à 5 dB de trop entre les kicks, « Reload » 5,7 de
moins — et pas la même cause : là une basse faible que le kick compensait, ici un grave qui vit dans la batterie même,
après la frappe, et que le kick de H70 (0,05 s, tenue 0,04 s) ne porte pas.

## 48. H87 — H84 rejouée sur « Reload » : le kick relevé sur ses frappes, le TR-808 réglé dessus (écrite AVANT sa mesure, 11/11/2026, 12 h 52)

**Le même instrument, le même protocole que § 45**, sur l'autre morceau et l'autre machine : le kick moyen du stem
`drums` (`retenir`, `kick_moyen`, morceau entier) contre celui de chaque réglage du TR-808 (moteur `reload-h70`), par
`mesure_h84.ecart_tf` ; la grille ENTIÈRE : accord 30 · 37 · 45 · 55 Hz × décroissance 0,05 · 0,10 · 0,20 · 0,35 s ×
tenue 0 · 0,04 · 0,08 s (48 solos ; le point de H70 — 37 · 0,05 · 0,04 — y est) ; les trois meilleurs à leur f\*
(`facteur_cale`, charleston 2,1063 et caisse 0,25 de H75 gardés), mélange rendu et jugé comme en H75 (`--tempo 136`,
mêmes sections) et par le grave rendu / original (H86). `reconstruction/travail/reload-h87.py`, rejouable ; son témoin :
le point de H70 au facteur de H75 (0,5822) doit rejouer 6,89.

**L'HYPOTHÈSE H87** : le relevé désigne un kick plus LONG que celui de H70, et le grave d'entre les kicks revient.

| # | mesure, au réglage choisi | réussite | échec |
|---|---|---|---|
| 0 | le témoin de la course | 6,89 au centième | REFUS |
| 1 | `ecart_tf` | au moins 1 dB sous celui du point de H70 | à moins de 0,3 dB |
| 2 | mélange rendu / original, 30-150 Hz, aux kicks ET entre | tous deux à 1,5 dB (H75 : −1,2 · −5,7) | l'un au-delà de 3 dB |
| 3 | \|médian\| sub · basse · bas-médium | chacune à moins de 0,5 dB de H75 (0,91 · 1,75 · 1,07), ou meilleure | l'une pire de plus de 1,5 dB |
| 4 | log-mel | ≤ 6,89 | > 7,08 (H72) |
| 5 | relevés : la grille entière, les frappes retenues, les trois mélanges | — | — |

**La règle** : 1 à 4 tenus → le relevé du kick par `ecart_tf` a réglé deux kicks sur deux morceaux et deux machines :
c'est la candidate pour une D-phase (le réglage d'un kick dans la chaîne) ; 1 en échec → H70 avait déjà le bon kick à ce
critère, et le grave manquant est ailleurs dans la batterie ; sinon au plus sévère. **Dit d'avance** : H84 a montré que
0,1 dB au critère ne départage pas deux voisins (d'où les trois mélanges) ; le critère ne voit rien au-dessus de
500 Hz ; `ecart_tf` lit 300 ms — un grave qui dure plus longtemps lui échappe.

### 48.1 Verdict de H87 (11/11/2026, écrit à 13 h 13) : NON TENUE — le relevé désigne bien un kick plus long, le grave d'entre les kicks revient (−5,7 → −1,9 dB), le log-mel est le meilleur de « Reload » (6,84), et le SUB casse de 6,7 dB : l'instrument de H84 ne se transporte pas

`reconstruction/travail/reload-h87.py` (rejouable ; `reload-h87/h87.json`). Le témoin rend 6,89. Frappes retenues : 204
sur 555 (23 près d'une caisse, 328 près d'un autre kick). **La grille entière** (`ecart_tf`, dB ; tenue 0 · 0,04 · 0,08 s) :

| accord | 0,05 s | 0,10 s | 0,20 s | 0,35 s |
|---|---|---|---|---|
| 30 Hz | 6,01 · 5,49 · 5,41 | **4,83** · 4,92 · 5,30 | 5,15 · 5,27 · 5,38 | 5,47 · 5,49 · 5,50 |
| 37 Hz | 6,57 · **5,78** (H70) · 5,98 | 5,21 · 5,67 · 5,99 | 6,06 · 6,07 · 6,20 | 6,39 · 6,34 · 6,37 |
| 45 Hz | 6,92 · 6,27 · 6,83 | 6,33 · 6,73 · 6,95 | 7,38 · 7,30 · 7,29 | 7,60 · 7,55 · 7,51 |
| 55 Hz | 7,19 · 7,19 · 7,48 | 7,27 · 7,42 · 7,60 | 8,09 · 8,00 · 8,00 | 8,40 · 8,36 · 8,32 |

| réglage (accord · décroissance · tenue) | tf | f\* | log-mel | \|médian\| sub · basse · bas-médium | aigus | rendu / original 30-150 Hz : aux kicks · entre |
|---|---|---|---|---|---|---|
| H70 : 37 · 0,05 · 0,04 (H75, témoin) | 5,78 | 0,5822 | 6,89 | 0,91 · 1,75 · 1,07 | 1,97 | −1,2 · −5,7 |
| **30 · 0,10 · 0 (choisi)** | 4,83 | 0,6885 | 6,84 | **7,63** · 1,71 · 1,66 | 1,86 | −0,2 · −1,9 |
| 30 · 0,10 · 0,04 | 4,92 | 0,4975 | 6,88 | 8,47 · 1,70 · 2,12 | 2,03 | −0,2 · −1,3 |
| 30 · 0,20 · 0 | 5,15 | 0,5431 | **6,83** | 10,70 · 2,32 · 2,45 | 1,70 | −0,2 · +1,9 |

| # | verdict, au réglage choisi |
|---|---|
| 1 | **entre les deux** : 0,95 dB sous le point de H70 |
| 2 | **entre les deux** : −0,2 dB aux kicks (tenu), −1,9 entre eux (à 0,4 dB du seuil) |
| 3 | **en ÉCHEC** : sub 7,63, soit 6,72 dB plus loin que H75 |
| 4 | **tenu** : 6,84 — le meilleur log-mel de « Reload » |

**Rien n'entre, et la règle est claire** : pas de candidate. **Ce que deux morceaux disent ensemble** (H84-H85 et H87) :
- **`ecart_tf` choisit le bon SENS de longueur** sur les deux (plus court sur « B4 », plus long sur « Reload ») — et le
  grave d'entre les kicks suit, sur les deux.
- **Il ne sait pas choisir un ACCORD.** Sur les deux morceaux il tire vers le bas de la grille ; sur « B4 » 35 Hz
  donnait le meilleur sub (0,49), sur « Reload » 30 Hz le pire (7,63, contre 0,91 à 37 Hz). Sa résolution est de 21 Hz
  (fenêtre de 46 ms) : 30 et 37 Hz tombent dans la même case. H68 avait déjà dit que l'accord se lit au sub, pas ailleurs.
- **Le log-mel s'améliore pendant que le sub casse** (6,89 → 6,84 pour +6,7 dB de sub) : quatrième fois dans ce chantier
  (H78, H80, H82, H87) que le log-mel moyen ne voit pas le grave.
- **Ce qui se transporte, c'est la MESURE et non le réglage** : le grave du mélange aux kicks et entre les kicks, rendu
  contre original, sans séparation. Elle a nommé juste sur les deux morceaux (+4,3 dB de trop sur « B4 », −5,7 de moins
  sur « Reload ») ce que l'outil du § 0, par tranches de 7 s, ne distingue pas.

**La suite**, que ces chiffres commandent avant tout réglage de plus : (a) faire entrer cette mesure dans
`tools/ecart-a-l-original.py` (une ligne « grave aux frappes / entre les frappes », validée sur perturbations connues
comme le reste de l'outil — § 0), pour que tout essai à venir la porte ; (b) rejouer H87 avec UNE variable, l'accord, à
0,10 s sans tenue, jugé par le sub — l'accord relevé par H69 (37 Hz) étant le candidat écrit d'avance.

## 49. H88 — UNE variable sur « Reload » : l'accord du kick, à 0,10 s sans tenue, jugé par le sub (écrite AVANT sa mesure, 11/11/2026, 13 h 15)

Projet et moteur de H87 ; kick du TR-808 à 0,10 s, tenue 0 (la longueur que H87 a choisie) ; accord 30 · 33 · 37 · 41 ·
45 Hz, chacun à son f\* (`facteur_cale`), les cinq mélanges rendus et jugés. **Le point JUGÉ est 37 Hz** — l'accord
relevé par H69 (§ 30.2), écrit d'avance au § 48.1 ; le balayage dit s'il est un sommet. Aucun code neuf
(`reconstruction/travail/reload-h88.py` ; son témoin : 30 Hz doit rejouer H87, 6,84 · sub 7,63).

| # | mesure, à 37 Hz | réussite | échec |
|---|---|---|---|
| 0 | le témoin : 30 Hz rejoue H87 | au centième | REFUS |
| 1 | \|médian\| sub | à moins de 0,5 dB de H75 (0,91), ou meilleur | pire de plus de 1,5 dB |
| 2 | \|médian\| basse · bas-médium | chacune à moins de 0,5 dB de H75 (1,75 · 1,07), ou meilleure | l'une pire de plus de 1,5 dB |
| 3 | mélange rendu / original, 30-150 Hz : aux kicks · entre | à 1,5 dB · à 3 dB (H75 : −1,2 · −5,7) | au-delà de 3 dB · au-delà de 4,5 dB |
| 4 | log-mel | ≤ 6,89 | > 7,08 |
| 5 | relevés : le balayage entier ; l'accord du meilleur sub | — | — |

**La règle** : 1 à 4 tenus → le kick de « Reload » est 37 Hz · 0,10 s · sans tenue, et la méthode candidate tient en deux
relevés — la longueur par `ecart_tf`, l'accord par H69 — à rejouer sur « B4 » puis un troisième morceau ; sinon au plus
sévère. **Dit d'avance** : si le meilleur sub n'est pas à 37 Hz, H69 ne donne pas l'accord non plus, et il se lira au
balayage du sub lui-même.

### 49.1 Verdict de H88 (11/11/2026, écrit à 13 h 23) : NON TENUE — à 37 Hz le grave d'entre les kicks est à l'original (+0,3 dB) et le log-mel le meilleur de « Reload » (6,75), mais le sub reste 5 dB trop loin ; il n'a pas de sommet dans le balayage

`reconstruction/travail/reload-h88.py` (rejouable ; `reload-h88/h88.json`). Le témoin (30 Hz) rejoue H87 au centième.

| accord (0,10 s, sans tenue) | f\* | log-mel | \|médian\| sub · basse · bas-médium | aigus | rendu / original 30-150 Hz : aux kicks · entre | 150-500 Hz aux kicks |
|---|---|---|---|---|---|---|
| H75 : 37 Hz · 0,05 s · tenue 0,04 | 0,5822 | 6,89 | **0,91** · 1,75 · 1,07 | 1,97 | −1,2 · −5,7 | +1,4 |
| 30 Hz | 0,6885 | 6,84 | 7,63 · 1,71 · 1,66 | 1,86 | −0,2 · −1,9 | −0,6 |
| 33 Hz | 0,7482 | 6,78 | 6,87 · 1,83 · **0,71** | 1,70 | −0,2 · −0,8 | +1,4 |
| **37 Hz (jugé)** | 0,8133 | 6,75 | 5,86 · 2,12 · 1,22 | 1,24 | −0,2 · **+0,3** | +2,9 |
| 41 Hz | 0,8826 | **6,74** | 4,79 · 2,30 · 1,61 | 1,11 | −0,2 · +1,2 | +4,2 |
| 45 Hz | 0,9602 | **6,74** | 3,59 · 2,54 · 2,13 | **0,93** | −0,2 · +2,1 | +5,4 |

| # | verdict, à 37 Hz |
|---|---|
| 0 | **tenu** |
| 1 | **en ÉCHEC** : sub 5,86, soit 4,95 dB plus loin que H75 |
| 2 | **tenu** : basse +0,37, bas-médium +0,15 |
| 3 | **tenu** : −0,2 dB aux kicks, +0,3 entre eux |
| 4 | **tenu** : 6,75 |

**Rien n'entre.** Le kick de « Reload » n'est pas réglé, et H69 ne donne pas l'accord au sens du sub : celui-ci décroît
d'un bout à l'autre du balayage (7,63 → 3,59) sans sommet, et reste loin de H75 à tous les points. **Deux juges du même
grave se contredisent** : la mesure par fenêtres dit le 30-150 Hz à l'original aux kicks et entre eux (à 37 Hz), le sub
de l'outil du § 0 le dit 5 à 6 dB faux. **Une piste, non mesurée** : la bande 30-150 Hz est trop large — la queue d'un
kick de TR-808 est un sinus pur à son accord (tout dans le sub), et le grave que l'original porte entre les kicks peut
être plus haut dans la bande ; à énergie égale sur 30-150 Hz, la répartition serait fausse. C'est exactement ce qu'une
mesure en DEUX bandes (30-60 · 60-150 Hz) dirait.

**La suite** (inchangée, précisée) : la mesure du grave aux frappes et entre elles entre dans l'outil du § 0, en deux
bandes graves et non une — puis H88 se relit avec elle avant tout autre réglage de kick.

## 50. L'outil du § 0 gagne une mesure : le grave AUX frappes et ENTRE elles, en deux bandes — et H89, la piste de H88 (écrit AVANT le code et la mesure, 11/11/2026, 16 h 17)

**Pourquoi** (§§ 42-49) : la mesure qui a nommé juste sur deux morceaux se faisait à la main, avec les notes du projet ;
l'outil du § 0 ne lit que deux fichiers. **Ce qui entre dans `tools/ecart-a-l-original.py`** (`grave_aux_frappes`) :
- les instants sont les attaques graves de l'ORIGINAL, que l'outil détecte déjà (`attaques_basses`, calculées une fois
  au lieu de deux) ; « aux frappes » : de 10 ms avant à 100 ms après chacune ; « entre » : le reste, entre deux attaques
  distantes de 2 s au plus (un break sans kick n'est ni l'un ni l'autre, et sa durée est dite) ;
- trois bandes — celles de l'outil : sub 20-60, basse 60-150, bas-médium 150-500 Hz — par filtres passe-bande, énergie
  échantillon par échantillon ; le chiffre : reconstruction / original en dB, par bande et par population ;
- **ce qu'elle ne compense pas, et dit** : un décalage global entre les deux fichiers déplace les fenêtres de la
  reconstruction ; au-delà de 10 ms d'écart médian au calage du kick, une ligne le rappelle.

**La validation, écrite d'avance** (`analyse/tests/test_ecart_grave.py`, sur signaux connus : un kick court toutes les
0,5 s et une note tenue à 100 Hz) : identique → 0,0 dB partout ; ×2 → +6,0 dB partout ; un kick allongé → le sub ENTRE
les frappes monte de plus de 6 dB, celui AUX frappes de moins de 2 ; la note tenue ôtée → la basse entre les frappes
tombe de plus de 10 dB, le sub aux frappes ne bouge pas de 1 dB ; les attaques détectées sur ce signal sont ses kicks
à 10 % près en nombre. **Et le recoupement avec ce qui est déjà mesuré** (30-150 Hz, notes du projet) : la mesure doit
retrouver, dans au moins une de ses deux bandes graves, entre les frappes — « B4 » H82 à plus de +3 dB, « B4 » H85 à
moins de 2 dB, « Reload » H75 à moins de −3 dB. Un seul de ces trois manqué : la mesure n'entre pas.

**H89 — la piste de H88, relue en deux bandes.** Sur « Reload » à 37 Hz · 0,10 s (H88), **hypothèse** : entre les
frappes le sub (20-60 Hz) est en EXCÈS d'au moins 3 dB et la basse (60-150 Hz) en DÉFAUT d'au moins 3 dB — la bande
30-150 Hz les additionnait à +0,3. **Échec** : les deux bandes sont du même signe, ou l'une à moins de 1,5 dB. Si H89
tient, le grave d'entre les kicks de « Reload » n'est pas une queue de kick à 37 Hz : c'est autre chose, plus haut.

### 50.1 Le code et sa validation, AVANT le recoupement et H89 (11/11/2026, 16 h 18)

`grave_aux_frappes` est dans `tools/ecart-a-l-original.py` (étape « 4 bis », une ligne au compte rendu, une clé au
JSON ; `attaques_basses` de l'original n'est plus calculée qu'une fois). `analyse/tests/test_ecart_grave.py`, cinq tests.
**Deux attendus de validation écrits au § 50 étaient FAUX sur la scène, et sont corrigés ici avant toute mesure d'un
morceau** : (1) « un kick allongé monte le sub aux frappes de moins de 2 dB » — non : un kick de 0,06 s s'éteint avant
la fin de sa fenêtre de 100 ms, celui de 0,25 s y met 5,3 dB de plus ; ce qui le signe est l'ÉCART des deux
populations (+19,9 entre, +5,3 aux frappes), et l'attendu devient « entre dépasse aux frappes de plus de 10 dB » ;
(2) la note tenue de la scène, vingt fois plus faible que le kick, ne portait pas la bande 60-150 Hz entre les frappes
(sa suppression n'y ôtait que 4,9 dB) : elle est montée à la moitié du kick, et l'attendu (« plus de 10 dB ») tient.
Le reste tient tel qu'écrit : identique 0,0 dB, ×2 → +6,02 dB, un break est hors mesure et dit, sans attaque rien
n'est mesuré, les attaques détectées sont les kicks. **Vu rouge sur quatre mutants** (les deux populations confondues ;
le break compté ; une seule bande pour les trois ; le non-mesurable ôté). `ruff`, `mypy` propres.

### 50.2 Verdict (11/11/2026, écrit à 16 h 21) : le recoupement est MANQUÉ sur « Reload » — la mesure n'entre pas dans l'outil, elle en est retirée ; et ce qu'elle a fait voir vaut plus qu'elle : la moitié des attaques graves de l'original n'ont pas de kick dans le projet

`reconstruction/travail/reload-h89.py` (aucun rendu ; `reload-h89.json`). Reconstruction / original, dB, aux frappes ·
entre elles :

| mélange | attaques de l'original | sub 20-60 | basse 60-150 | bas-médium 150-500 |
|---|---|---|---|---|
| B4 · H82 | 1 339 | +2,5 · **+3,8** | −0,4 · −1,3 | +2,2 · +3,3 |
| B4 · H85 | 1 339 | +3,1 · **−1,4** | −1,7 · −4,0 | +1,2 · +3,0 |
| Reload · H75 | 849 | −3,7 · **+8,2** | −1,9 · −1,2 | +0,1 · −2,6 |
| Reload · H87 (30 Hz) | 849 | +5,0 · +15,5 | −1,6 · −0,7 | −3,0 · −3,8 |
| Reload · H88 (37 Hz) | 849 | +1,6 · +15,8 | −1,1 · −0,3 | +1,8 · −1,6 |
| Reload · H88 (45 Hz) | 849 | −2,0 · +14,8 | −0,7 · +0,9 | +4,7 · +0,3 |

**Le recoupement écrit au § 50** : « B4 » H82 à plus de +3 dB entre les frappes — tenu (+3,8) ; « B4 » H85 à moins de
2 dB — tenu (−1,4) ; **« Reload » H75 à moins de −3 dB — MANQUÉ** : +8,2 au sub, −1,2 à la basse, là où la mesure par
les notes du projet disait −5,7. « Un seul manqué : la mesure n'entre pas. » **Appliqué** : `grave_aux_frappes` et son
test sont retirés de l'outil (le fichier est revenu à son état d'avant le § 50.1 ; le code reste lisible dans le commit
qui l'avait posé). **H89** ne se lit donc pas — et lue quand même, elle serait en échec (basse à −0,3 dB).

**Pourquoi les deux mesures se contredisent, relevé à la suite** — elles ne lisent pas les mêmes frappes :

| morceau | attaques graves de l'original (`attaques_basses`) | kicks du projet | attaques à plus de 50 ms de tout kick du projet | kicks du projet sans attaque à 50 ms |
|---|---|---|---|---|
| Reload | 849 | 555 | **415** | 121 |
| B4 | 1 339 | 763 | **643** | 42 |

Sur « Reload », **415 attaques graves de l'original sur 849 n'ont aucun kick du projet à 50 ms** — et ce morceau n'a
pas de basse (H86 : le stem `bass` est à −14,6 dB de la batterie entre les kicks). Les « charlestons d'entre les
kicks » de H86 tombaient donc, pour beaucoup, SUR des frappes graves que le projet n'a pas : le « grave qui manque
entre les kicks » (−5,7 dB) n'était pas une queue de kick trop courte, et H87-H88 ont allongé un kick pour boucher des
frappes ABSENTES — d'où un sub qui explose entre les vraies attaques (+15 dB) pendant que la mesure par les notes se
disait contente (+0,3). **L'outil du § 0 le publiait depuis le début** : « calage du kick : 396/849 attaques appariées
(reconstruction : 530) » sur le mélange de H75 — un témoin publié qui ne s'est pas lu (la règle du dépôt, repayée).
Sur « B4 », les 643 attaques sans kick peuvent être des notes de basse (ce morceau en a une) : non départagé ici.

**Ce qui tombe** : la lecture de H86 (« un grave qui vit dans la batterie après la frappe »), et avec elle le sens de
H87 et H88 — leurs chiffres restent, leur explication non. **Ce qui tient** : H81-H85 sur « B4 » ne sont pas démenties
par cette mesure (H82 +3,8 → H85 −1,4), mais se relisent sous la même question.

**La suite** (H90, à écrire AVANT sa mesure) : sur « Reload », que sont les 415 attaques graves sans kick — dans quel
stem, à quel niveau par rapport aux kicks transcrits, sur quels temps de la mesure — et la transcription de la batterie
les a-t-elle manquées ? Si oui, c'est un défaut de la CHAÎNE (le rappel des kicks), en amont de tout réglage de machine.

## 51. H90 — sur « Reload », les 415 attaques graves sans kick : des kicks manqués par la transcription, ou des fausses détections ? (écrite AVANT son instrument et sa mesure, 11/11/2026, 16 h 23)

Aucun rendu. `analyse/mesure_h90.py` (`apparier`, `position_16e`), son test, `reconstruction/travail/reload-h90.py`.
Les 849 attaques de l'original (`attaques_basses`, l'outil du § 0) sont partagées en « avec kick » (un kick du projet à
50 ms) et « sans kick ». Pour chaque groupe : la part qui coïncide (50 ms) avec une attaque grave détectée dans le STEM
`drums` seul ; l'énergie à 30-150 Hz du stem `drums` dans la fenêtre de la frappe (10 ms avant, 100 ms après), médiane
du groupe ; la part qui suit de moins de 150 ms une autre attaque (une double détection possible) ; la position dans le
temps (double-croche du temps, tempo 136, grille calée sur les kicks du projet) ; la part à 50 ms d'une caisse claire.

**L'HYPOTHÈSE H90** : ce sont des frappes réelles de la batterie que la transcription n'a pas écrites.

| # | mesure, sur les attaques « sans kick » | réussite | échec |
|---|---|---|---|
| 1 | part retrouvée dans les attaques du stem `drums` | ≥ 70 % | < 40 % |
| 2 | énergie médiane à 30-150 Hz (stem `drums`), rapportée à celle des « avec kick » | à moins de 6 dB | plus de 12 dB dessous |
| 3 | part qui suit une autre attaque de moins de 150 ms | ≤ 20 % | ≥ 50 % (des doubles détections) |
| 4 | relevés : les comptes, les positions par double-croche, la part sur une caisse, les 121 kicks du projet sans attaque | — | — |

**La règle** : 1 à 3 tenus → la chaîne a manqué des kicks sur « Reload » : un défaut de RAPPEL à instruire dans la
transcription de la batterie (où, pourquoi), avant tout réglage — et H87-H88 sont à refaire une fois les notes là ;
1 ou 2 en échec → ces attaques ne sont pas des frappes de batterie (autre instrument grave, ou bruit du détecteur) ;
3 en échec → c'est `attaques_basses` qui compte double, et le « 396/849 » de l'outil est à corriger, pas la chaîne.
**Dit d'avance** : le stem `drums` est une estimation ; une frappe « réelle » au sens de ces trois mesures peut être un
tom ou une caisse grave et non un kick — la position et la part sur une caisse sont relevées pour cela.

### 51.1 Verdict de H90 (11/11/2026, écrit à 16 h 25) : RÉFUTÉE — ces attaques ne sont pas des frappes de batterie, la chaîne n'a pas manqué de kicks ; et l'explication écrite au § 50.2 est RETIRÉE

`reconstruction/travail/reload-h90.py` (aucun rendu ; `reload-h90.json`). 849 attaques dans l'original, 888 dans le stem
`drums`, 555 kicks au projet.

| groupe | nombre | retrouvées dans le stem `drums` | énergie médiane 30-150 Hz du stem, à la frappe | suivent une autre de moins de 150 ms | sur une caisse |
|---|---|---|---|---|---|
| avec kick du projet | 434 | 94,7 % | 50,9 dB | 24,2 % | 0 % |
| **sans kick** | 415 | 52,5 % | **3,1 dB** | 28,7 % | 8,4 % |

| # | verdict |
|---|---|
| 1 | **entre les deux** : 52,5 % |
| 2 | **en ÉCHEC** : 47,8 dB SOUS les kicks transcrits — il n'y a pas de frappe grave de batterie à ces instants |
| 3 | **entre les deux** : 28,7 % |

**La règle** : « 1 ou 2 en échec → ces attaques ne sont pas des frappes de batterie ». **Où elles sont** (par tranches
de 30 s, sans · avec) : 0-30 s **125 · 0** ; 30-60 s 64 · 75 ; 60-120 s 23 · 110 ; 120-150 s **49 · 13** ; 150-180 s
64 · 58 ; 180-270 s 51 · 178 ; 270-300 s **39 · 0**. Elles s'entassent là où le kick ne joue pas — l'intro, le pont, la
fin : `attaques_basses` y relève les maxima d'un grave faible, faute de mieux. La transcription de la batterie n'est
PAS en cause. (Les 121 kicks du projet sans attaque à 50 ms ne sont retrouvés dans le stem qu'à 7,4 % : relevé, non
instruit. Les positions par double-croche ne se lisent pas : l'écart médian à ma grille est de 27 à 29 ms, un quart de
pas — la grille est mal calée, je ne publie pas ces comptes comme une mesure.)

**RÉTRACTATION du § 50.2.** J'y ai écrit que « les charlestons d'entre les kicks tombaient, pour beaucoup, sur des
frappes graves que le projet n'a pas », et que « H87-H88 ont allongé un kick pour boucher des frappes ABSENTES ».
**C'est faux** : écrit sur un compte (415 sur 849) dont je n'avais pas décomposé la population — le piège du taux sans
son dénominateur, repayé le jour même. Ce qui reste vrai du § 50.2 : la mesure portée dans l'outil a manqué son
recoupement, et son retrait tient — mais pour une autre raison que celle écrite : sa population « entre les frappes »
avalait les sections SANS kick (43,5 s hors mesure seulement, sur un morceau dont l'intro, le pont et la fin n'en ont
pas), où des attaques parasites espacées de moins de 2 s font compter comme « entre deux frappes » ce qui est un break.
La lecture de H86 (−5,7 dB entre les kicks, aux charlestons du PROJET) n'est donc pas démentie ; H87 et H88 gardent leur
sens premier ; et la contradiction de H88 — le grave juste par fenêtres, le sub faux à l'outil — reste ENTIÈRE et
inexpliquée.

**Ce que cela apprend sur l'outil du § 0** : son « N/849 attaques appariées » compte des attaques qui ne sont pas des
kicks dès que le morceau a des sections sans kick ; le chiffre n'est pas un rappel. À corriger ou à dire dans l'outil —
noté, non fait.

**La suite** (H91, à écrire AVANT sa mesure) : ce que H89 voulait lire, lu proprement — les deux bandes graves (20-60 ·
60-150 Hz) aux kicks et aux charlestons d'entre les kicks **du projet** (les populations de H81, qui ont tenu leurs
contrôles), mélange rendu contre original, sur les points de H75, H87 et H88.

## 52. H91 — le grave de « Reload » en deux bandes, aux frappes DU PROJET (écrite AVANT son instrument et sa mesure, 11/11/2026, 16 h 38)

Aucun rendu. `analyse/mesure_h91.py` (`energie_fenetres` : passe-bande, énergie dans les fenêtres de 10 ms avant à
100 ms après chaque instant), son test, `reconstruction/travail/reload-h91.py`. Populations de H86 : les 555 kicks, les
1 613 charlestons d'entre les kicks. Bandes : sub 20-60, basse 60-150, et leur réunion 20-150 Hz. Mélange rendu contre
original, sur H75 et les cinq accords de H88 (30 à 45 Hz, 0,10 s sans tenue).

**Le recoupement, avant toute lecture** : la bande 20-150 Hz doit retrouver, à 1,5 dB, ce que `mesure_h81` a publié à
30-150 Hz entre les kicks — H75 −5,7 dB, H88 à 37 Hz +0,3 dB. L'un des deux manqué : rien ne se lit.

**L'HYPOTHÈSE H91** (celle de H89) : à 37 Hz, entre les kicks, le sub est en EXCÈS d'au moins 3 dB et la basse en
DÉFAUT d'au moins 3 dB — la bande large les additionnait. **Échec** : même signe, ou l'une à moins de 1,5 dB.
**La règle** : tenue → le grave d'entre les kicks de « Reload » est plus HAUT que la queue d'un kick à 37 Hz, et aucun
allongement ne le rendra : chercher ce que c'est (un tom, une seconde grosse caisse accordée plus haut) ; échec → la
bande large ne cachait rien, et le sub faux de l'outil vient d'ailleurs que d'entre les kicks (aux kicks mêmes, ou des
sections sans charleston) — le balayage le dira.

### 52.1 Le recoupement de H91 est MANQUÉ de 0,16 dB — et H91 bis, écrite avant d'être rejouée (11/11/2026, 16 h 40)

Première course : à 20-150 Hz entre les kicks, H75 rend **−7,36 dB** (attendu −5,7 ± 1,5 : manqué de 0,16) ; 37 Hz rend
−0,06 (attendu +0,3 : tenu). « L'un des deux manqué : rien ne se lit » — **les chiffres des deux bandes de cette course
ne sont donc PAS lus comme un verdict.** Le recoupement comparait une bande de 20 à 150 Hz à une mesure faite de 30 à
150 Hz : ce n'était pas la même bande, et c'est mon attendu qui était mal écrit, pas forcément l'instrument.
**H91 bis** — une seule chose change, la bande du recoupement : 30-150 Hz, celle de `mesure_h81`, même tolérance
(1,5 dB), mêmes deux points. Tenu → les deux bandes se lisent, avec l'hypothèse et la règle du § 52 inchangées ; manqué
encore → l'instrument par filtres ne reproduit pas celui par spectrogramme, et c'est lui qu'il faut instruire.
**Dit** : j'écris ceci après avoir VU les chiffres de la première course (sub +16,4 · basse −11,5 dB à 37 Hz) ; le
lecteur doit le savoir.

### 52.2 H91 bis (11/11/2026, écrit à 16 h 41) : le recoupement est MANQUÉ encore, de 0,18 dB — pas de verdict ; les deux bandes sont publiées comme un RELEVÉ, pas comme une lecture

`reload-h91.json`. Mélange rendu / original, dB :

| point | aux kicks : sub · basse · 30-150 | entre les kicks : sub · basse · 30-150 | `mesure_h81`, 30-150 : aux kicks · entre |
|---|---|---|---|
| H75 | −4,0 · −0,9 · −1,2 | +8,5 · −14,8 · **−7,4** | −1,2 · **−5,7** |
| 30 Hz | +5,2 · −0,6 · −0,2 | +15,7 · −14,7 · −2,4 | −0,2 · −1,9 |
| 33 Hz | +3,7 · −0,4 · −0,1 | +16,4 · −13,7 · −1,0 | −0,2 · −0,8 |
| 37 Hz | +0,3 · −0,2 · −0,2 | +16,4 · −11,5 · −0,2 | −0,2 · +0,3 |
| 41 Hz | −1,9 · +0,1 · −0,2 | +16,2 · −8,1 · +0,7 | −0,2 · +1,2 |
| 45 Hz | −5,2 · +0,2 · −0,3 | +15,5 · −4,5 · +1,4 | −0,2 · +2,1 |

**Le recoupement** : à 37 Hz, −0,2 pour +0,3 — tenu ; sur H75, −7,4 pour −5,7 — **manqué de 0,18 dB**. Aux kicks les
deux instruments s'accordent à 0,05 dB sur les six points ; entre les kicks ils s'écartent de 0,5 à 1,7 dB, toujours
dans le même sens (le filtre lit plus bas). **La règle, écrite avant** : « l'instrument par filtres ne reproduit pas
celui par spectrogramme, et c'est lui qu'il faut instruire ». **Je ne déclare donc PAS H91 tenue**, alors que le relevé
va très loin dans son sens (sub +16,4 dB, basse −11,5 dB à 37 Hz, là où elle demandait ±3) : deux instruments qui
diffèrent de 1,7 dB sur le même fichier ne me permettent pas de dire lequel a raison là où le signal est faible — et
c'est justement là, entre les frappes, que tout se joue.

**La suite** (H92, à écrire AVANT sa mesure) : sur une scène de VÉRITÉ CONNUE — un kick et, entre ses frappes, une
note de niveau connu dans chaque bande —, lequel des deux instruments rend le rapport écrit, et à quelle erreur. Aucun
morceau, aucun rendu. Ensuite seulement le relevé ci-dessus se lira, ou non.

## 53. H92 — lequel des deux instruments lit juste entre les frappes ? Une scène de vérité connue (écrite AVANT sa mesure, 11/11/2026, 17 h 00)

Aucun morceau, aucun rendu, aucun code neuf (`reconstruction/travail/reload-h92.py`). **La scène** : un kick (la
réplique du TR-808 de H69, 37 Hz, 0,05 s) toutes les 0,44 s, COUPÉ net à 100 ms — après quoi il vaut zéro, exactement ;
entre deux kicks, de 150 à 350 ms après chacun, une note pure (40 Hz pour le sub, 100 Hz pour la basse) d'amplitude
connue sous la crête du kick : −20, −40, −60 dB. L'« original » porte la note ; le « rendu » la porte multipliée par un
gain connu, −6 ou −15 dB ; le kick est le même des deux côtés. Fenêtres de mesure : de 10 ms avant à 100 ms après un
instant situé 220 ms après chaque kick — entièrement dans la note. **La vérité** est donc le gain écrit, sans calcul :
tout écart est du kick que l'instrument a laissé baver dans la fenêtre.

Instrument A : le spectrogramme de `mesure_h74` (30-150 Hz ; `mesure_h81`). Instrument B : le filtre de `mesure_h91`
(30-150, 20-60 et 60-150 Hz).

**L'HYPOTHÈSE H92** : B rend la vérité, A tire vers 0 dB (ce que les morceaux montrent : A −5,7, B −7,4).

| # | mesure, note à −40 dB | réussite | échec |
|---|---|---|---|
| 1 | B, 30-150 Hz, note de 100 Hz : erreur aux deux gains | ≤ 0,5 dB | > 1 dB |
| 2 | B dans chaque bande propre (40 Hz lu à 20-60, 100 Hz lu à 60-150) | ≤ 0,5 dB | > 1 dB |
| 3 | A, 30-150 Hz : erreur, et son signe | relevé — attendu : vers 0 dB | — |
| 4 | relevés : la table entière (trois niveaux, deux gains, deux notes, deux instruments) | — | — |

**La règle** : 1 et 2 tenus → l'instrument par filtres est juste là où le signal est faible, le recoupement manqué de
H91 était la faute de l'instrument de RÉFÉRENCE, et le relevé du § 52.2 se lit avec l'hypothèse et la règle du § 52 ;
1 ou 2 en échec → B bave aussi, les deux bandes de H91 ne se lisent pas, et il faut un troisième instrument. **Dit
d'avance** : une scène à kick coupé net est plus facile que le réel (un vrai kick a une queue) — elle juge la bavure de
l'instrument, pas la séparation d'un kick et d'une basse qui se recouvrent.

### 53.1 Verdict de H92 (11/11/2026, écrit à 17 h 01) : 1 et 2 TENUS — l'instrument par filtres est juste ; mais celui de référence l'est AUSSI sur la scène : leur écart sur le morceau reste inexpliqué. H91 se lit, avec cette marge

`reload-h92.json`. Erreur (lu − vérité), dB :

| note | niveau sous la crête du kick | gain vrai | A, spectrogramme 30-150 | B, filtre 30-150 | B, bande propre |
|---|---|---|---|---|---|
| 100 Hz | −20 · −40 · −60 | −6 | 0,00 · 0,00 · 0,00 | 0,00 · 0,00 · 0,01 | 0,00 · 0,00 · 0,01 |
| 100 Hz | −20 · −40 · −60 | −15 | 0,00 · 0,00 · 0,00 | 0,00 · 0,00 · 0,08 | 0,00 · 0,01 · 0,07 |
| 40 Hz | −20 · −40 · −60 | −6 | 0,00 · 0,00 · 0,00 | 0,00 · 0,00 · 0,01 | 0,00 · 0,01 · **1,10** |
| 40 Hz | −20 · −40 · −60 | −15 | 0,00 · 0,00 · 0,00 | 0,00 · 0,00 · 0,10 | 0,00 · 0,14 · **5,98** |

1 **tenu** (0,00 dB) ; 2 **tenu** (0,14 dB au pire à −40 dB). 3, relevé : **mon attendu était faux** — l'instrument A ne
tire pas vers 0 dB, il est exact partout sur cette scène. **La limite de B, trouvée** : à 20-60 Hz, une note 60 dB sous
le kick est lue avec 1,1 à 6,0 dB d'erreur (le filtre du sub résonne). **Le régime réel, relevé à la suite** pour savoir
de quel côté de cette limite on est (énergie entre les kicks rapportée à celle aux kicks, instrument B) :

| « Reload » | sub 20-60 Hz | basse 60-150 Hz |
|---|---|---|
| original | −14,7 dB | −12,0 dB |
| rendu H75 | −2,2 dB | −25,9 dB |
| rendu H88 à 37 Hz | **+1,4 dB** | −23,4 dB |

L'original est à −12 et −15 dB : loin au-dessus du régime où B se trompe. **La règle** s'applique sur sa première
moitié (B est juste) et PAS sur sa seconde (« la faute de l'instrument de référence ») : la scène, plus facile que le
réel comme écrit d'avance, n'explique pas l'écart de 0,5 à 1,7 dB des deux instruments sur le morceau. **Je lis donc le
relevé du § 52.2 avec cette marge de 1,7 dB** — et il la dépasse de loin.

**H91, lue** : à 37 Hz, entre les kicks, sub **+16,4 dB**, basse **−11,5 dB** (demandé : ±3). **TENUE.** Et le relevé
dit plus que l'hypothèse : l'excès de sub entre les kicks est déjà là en H75 (+8,5 dB), avec un kick de 0,05 s qui est
éteint depuis longtemps ; dans le rendu, le sub d'entre les kicks vaut presque celui des kicks (−2,2 dB ; +1,4 à
37 Hz) quand il en est à −14,7 dans l'original. **Ce n'est pas une queue de kick : quelque chose TIENT du sub dans le
rendu de « Reload »**, et le grave que l'original a entre les kicks — plus haut, à 60-150 Hz — y manque de 11 à 15 dB à
tous les points. H87 et H88 réglaient un kick pour un défaut qui n'est pas le sien ; le sub faux de l'outil (0,91 →
5,86) était vrai.

**La suite** (H93, à écrire AVANT sa mesure) : la question de H81, côté rendu, sur « Reload » — quelle PISTE porte le
20-60 Hz entre les kicks (les quatre voix `other`, le `piano` au `vsm.minimoog`, la batterie), piste par piste, sous
contrôle de somme ; et dans l'original, quel stem porte le 60-150 Hz qui manque.

## 54. H93 — sur « Reload », quelle PISTE du rendu tient le sub entre les kicks, et quel stem de l'original porte le 60-150 Hz qui manque (écrite AVANT sa mesure, 11/11/2026, 17 h 06)

Aucun code neuf (`mesure_h91.energie_fenetres`, populations de H86 ; `reconstruction/travail/reload-h93.py`). Le projet
de H75, ses dix pistes rendues SEULES (quatre voix `other`, `piano`, charleston, kick, caisse claire, deux voix audio) ;
l'original et ses six stems. Deux bandes (20-60, 60-150 Hz), deux populations (555 kicks, 1 613 charlestons d'entre les
kicks). Chaque part est donnée en dB sous SON mélange.

| # | mesure | réussite | échec |
|---|---|---|---|
| 0 | contrôles de somme : dix pistes → mélange rendu, six stems → original, dans les deux bandes et les deux populations | à 1 dB | REFUS |
| 1 | rendu, sub entre les kicks : les trois pièces de batterie ensemble | à −6 dB ou moins du mélange, ET une piste hors batterie à −3 dB ou plus | la batterie à −3 dB ou plus (c'est elle, malgré H92) |
| 2 | relevés : la table entière ; la piste qui porte le sub ; le stem qui porte le 60-150 Hz de l'original entre les kicks | — | — |

**L'HYPOTHÈSE H93** : une piste hors batterie — je crois le `piano`, joué par `vsm.minimoog` au volume le plus fort du
projet — tient un sub que l'original n'a pas. **La règle** : 1 tenu → cette piste est à instruire (ses notes : une
octave ? sa machine : un sous-oscillateur ?) avant tout autre réglage de « Reload » ; 1 en échec → c'est une pièce de
batterie qui tient ce sub, et les solos diront laquelle. **Dit d'avance** : les parts de l'original sont celles d'une
séparation.

### 54.1 Verdict de H93 (11/11/2026, écrit à 17 h 14) : 1 en ÉCHEC — c'est la piste du KICK qui porte le sub entre les kicks ; la phrase du § 53.1 (« ce n'est pas une queue de kick ») est RETIRÉE

`reconstruction/travail/reload-h93.py` (rejouable ; `reload-h93/h93.json`). Contrôles de somme tenus (rendu 0,0 dB ;
original 0,3 dB au pire). Chaque part en dB sous son mélange :

| | aux kicks : sub · basse | entre les kicks : sub · basse |
|---|---|---|
| rendu · **kick** | 0,0 · 0,0 | **−0,1** · −2,2 |
| rendu · caisse claire | −58,7 · −35,2 | −42,5 · −6,2 |
| rendu · charleston | −63,4 · −78,2 | −54,8 · −44,5 |
| rendu · `piano` (`vsm.minimoog`) | −35,2 · −36,0 | −21,6 · −11,4 |
| rendu · `other` voix 4 | −26,6 · −36,5 | −24,0 · −11,1 |
| rendu · `other` voix 1 · 2 · 3 | −41,9 · −31,7 · −33,9 (sub) | −38,0 · −28,0 · −28,4 (sub) ; −27,8 · −25,8 · −25,2 (basse) |
| rendu · deux voix audio | sous −69 | sous −68 |
| original · stem `drums` | −0,1 · −0,2 | **−1,1 · −1,6** |
| original · stems `bass` · `other` | −38,1 · −30,9 (sub) | −16,3 · −14,9 (sub) ; −15,8 · −16,7 (basse) |
| original · stems `piano` · `guitar` · `vocals` | sous −43 | sous −32 |

**1 en ÉCHEC** : entre les kicks, le sub du rendu est la piste du kick à 0,1 dB près ; le `piano` est à −21,6 dB. **Mon
hypothèse était fausse, et la phrase du § 53.1 aussi** — « ce n'est pas une queue de kick : quelque chose TIENT du sub »,
« un kick de 0,05 s éteint depuis longtemps ». **RETIRÉE.** Je l'avais déduite sans regarder OÙ tombent les fenêtres : à
136 à la noire une double-croche dure 110 ms, et les « charlestons d'entre les kicks » sont, pour beaucoup, la
double-croche qui SUIT un kick — de 100 à 210 ms après lui, là où un kick de TR-808 à 0,05 s de décroissance après
0,04 s de tenue sonne encore. C'est la seconde explication de la journée écrite au-delà de ce que son attendu mesurait,
et retirée (§ 50.2, § 53.1).

**Ce que la table établit, sans plus** : (a) dans le rendu comme dans l'original, entre les kicks, le grave est dans la
batterie (kick −0,1 · −2,2 dB ; stem `drums` −1,1 · −1,6 dB) — H86 le disait, c'est confirmé en deux bandes ; (b) ce
grave n'a pas la même place : rapporté aux kicks, l'original en a −14,7 dB au sub et −12,0 à la basse, le rendu −2,2
et −25,9 (§ 53.1) — la batterie de l'original, de 100 à 210 ms après ses kicks, sonne à 60-150 Hz ; le kick du TR-808,
lui, y sonne à son accord ; (c) la décroissance du kick du rendu est à 0,05 s, le MINIMUM de la machine : elle est à sa
borne, et ce kick ne peut pas être plus court.

**Ce que la mesure ne dit PAS** : si le 60-150 Hz de l'original entre les kicks est la queue de son kick, une autre
pièce (la caisse du rendu y pèse −6,2 dB), ou un reste de séparation. **La suite** (H94, à écrire AVANT sa mesure) : le
kick MOYEN de l'original (la moyenne synchrone de H69, qui efface ce qui ne frappe pas avec lui) contre celui du rendu,
bande par bande et par tranches de 50 ms jusqu'à 300 ms — son grave de 100 à 210 ms est-il À LUI ?

## 55. H94 — le grave que la batterie de « Reload » a de 100 à 200 ms après ses kicks est-il celui du KICK ? (écrite AVANT son instrument et sa mesure, 11/11/2026, 17 h 16)

Aucun rendu. Le kick MOYEN (moyenne synchrone de H69 sur les frappes isolées de H87 : 204 sur 555, aucun autre kick à
300 ms) du stem `drums`, du mélange original, du solo du kick rendu (H93) et du mélange rendu de H75. Ce qui ne frappe
pas AVEC le kick s'efface dans la moyenne ; ce qui reste est à lui. `analyse/mesure_h94.py` (`profil` : énergie par
bande — 20-60 et 60-150 Hz — et par tranche de 100 ms, de 0 à 300 ms), son test, `reconstruction/travail/reload-h94.py`.

**L'HYPOTHÈSE H94** : ce grave est à lui — le kick de l'original a, après sa frappe, une queue plus HAUTE que son sub.

| # | mesure, kick moyen du stem `drums`, tranche 100-200 ms | réussite | échec |
|---|---|---|---|
| 1 | basse (60-150 Hz), rapportée à la basse de la tranche 0-100 ms | ≥ −18 dB (à 6 dB de ce que les fenêtres lisaient : −12,0) | ≤ −24 dB (la moyenne l'efface : ce n'est pas le kick) |
| 2 | basse rapportée au sub, même tranche | ≥ +3 dB | ≤ 0 dB |
| 3 | cohérence : corrélation des moyennes des frappes paires et impaires, 100-300 ms, 60-150 Hz | ≥ 0,7 | < 0,3 |
| 4 | relevés : les quatre profils entiers ; le même sur le mélange original (sans séparation) | — | — |

**La règle** : 1 à 3 tenus → le kick de « Reload » a une queue à 60-150 Hz que le TR-808 n'a pas : c'est un timbre de
kick, un échantillon ou une autre machine, et plus un réglage ; 1 ou 3 en échec → ce grave ne frappe pas avec le kick :
une autre pièce ou un autre instrument, à chercher par ses propres instants. **Dit d'avance** : 204 frappes ; des
tranches de 100 ms et un filtre de 20 à 60 Hz se recouvrent d'une tranche sur l'autre (une période de 20 Hz dure 50 ms) —
le test dit de combien ; la comparaison à −12,0 dB rapproche deux fenêtres qui ne sont pas les mêmes.

### 55.1 L'instrument de H94, AVANT toute mesure (11/11/2026, 17 h 16)

`analyse/mesure_h94.py` (`filtrer_bande`, `profil`), trois tests sur une frappe connue (40 Hz de 0 à 100 ms, puis
100 Hz 12 dB dessous) : la tête au sub, la queue à la basse, son niveau retrouvé à 1,5 dB. **La bavure, chiffrée** : sans
queue du tout, la tête laisse dans la tranche 100-200 ms un sub à −20,4 dB d'elle, et une basse à −26,6 dB ; à
200-300 ms, −50 dB. Un attendu ne se lit donc qu'au-dessus de ces planchers. **Vu rouge sur trois mutants** (tranches mal
placées ; une seule bande ; le refus ôté). `ruff`, `mypy` propres.

### 55.2 Verdict de H94 (11/11/2026, écrit à 17 h 17) : TENUE — le grave d'après les kicks de « Reload » est celui du KICK, à 60-150 Hz, et le TR-808 fait l'inverse

`reconstruction/travail/reload-h94.py` (aucun rendu ; `reload-h94.json`). 203 fenêtres moyennées. Énergie du kick
moyen, en dB rapportés à SA tranche 0-100 ms, par tranche (0-100 · 100-200 · 200-300 ms) :

| kick moyen | sub 20-60 Hz | basse 60-150 Hz | basse − sub, par tranche | cohérence paires / impaires (100-300 ms, basse) |
|---|---|---|---|---|
| stem `drums` | 0 · −11,1 · −13,2 | 0 · **−10,2** · −13,0 | +14,2 · **+15,0** · +14,3 | 0,994 |
| mélange original (sans séparation) | 0 · −10,8 · −12,5 | 0 · −9,7 · −10,8 | +14,2 · +15,2 · +15,8 | 0,992 |
| rendu · kick seul (TR-808 de H70) | 0 · **+5,4** · −10,8 | 0 · **−21,4** · −46,0 | +18,3 · **−8,4** · −16,9 | 1,000 |
| rendu · mélange de H75 | 0 · +5,4 · −10,8 | 0 · −21,3 · −46,3 | +18,3 · −8,4 · −17,2 | 0,999 |

| # | verdict, stem `drums`, 100-200 ms |
|---|---|
| 1 | **tenu** : −10,2 dB (les fenêtres lisaient −12,0) — la moyenne synchrone ne l'efface pas |
| 2 | **tenu** : basse à +15,0 dB du sub |
| 3 | **tenu** : 0,994 |

Les chiffres sont loin au-dessus des planchers de bavure du § 55.1 (−20,4 et −26,6 dB), et le mélange original, sans
séparation, rend le même profil à 0,5 dB près sur la tranche jugée. **La règle** : « le kick de "Reload" a une queue à
60-150 Hz que le TR-808 n'a pas : c'est un timbre de kick, un échantillon ou une autre machine, et plus un réglage ».
**Elle s'applique.** Ce que la table mesure, et rien de plus : le kick de l'original garde sa basse 14 à 15 dB au-dessus
de son sub sur ses trois tranches, et la tient (−10 puis −13 dB) ; celui du TR-808 part pareil (+18,3) puis s'inverse —
de 100 à 200 ms son sub MONTE de 5,4 dB pendant que sa basse tombe de 21,4. Les relevés de H91 (sub +8,5 à +16,4 dB,
basse −11,5 à −14,8 dB entre les kicks) et le sub faux de H87-H88 en sont la lecture au mélange.

**Rien n'entre** — H94 désigne. **La suite** (H95, à écrire AVANT sa mesure) : le kick de « Reload » joué par un
ÉCHANTILLON — le kick moyen lui-même, tiré du stem (ce que `CDC-multisample.md` et les machines `vsm.sampler` /
`vsm.multisample` du parc permettent) —, à la place du TR-808, jugé par l'outil du § 0 et par les deux bandes aux
kicks et entre eux. « Machines neuves si la mesure les demande » : elle le demande ici pour un timbre, pas pour une
machine.

## 56. H95 — le kick de « Reload » joué par un ÉCHANTILLON (écrite AVANT sa mesure, 11/11/2026, 17 h 19)

**Un constat fait en ouvrant le projet** : la chaîne a DÉJÀ tiré un échantillon de kick du stem —
`reload-h42/temoin/samples/kick.wav` (0,464 s, mono), avec `kick2.wav`, `snare.wav`, `hihat.wav` — et son projet joue
quand même le TR-808. La piste du kick n'a qu'une note (36, 555 fois).

**Une variable : la machine de la piste du kick** — `vsm.sampler`, emplacement 1 sur la note 36, réglages du report
intégral de la chaîne (`vsm_drumkit.py` : niveau 1/0,7071, décroissance 0, accord 0). Le reste est le projet de H75.
Le niveau, conséquence : f\* (`facteur_cale`, 30-150 Hz à ses frappes), comme depuis H79. Deux points : **A, le point
JUGÉ — `samples/kick.wav`, l'échantillon de la chaîne** ; B, relevé — le kick moyen de H69 sur les 204 frappes
isolées, large bande. Aucun code neuf (`reconstruction/travail/reload-h95.py` ; témoin : le projet de H75 rejoue 6,89).

| # | mesure, au point A | réussite | échec |
|---|---|---|---|
| 0 | le témoin de la course | 6,89 au centième | REFUS |
| 1 | mélange rendu / original ENTRE les kicks : sub ET basse (`mesure_h91`) | tous deux à 3 dB (H75 : +8,5 · −14,8) | l'un au-delà de 6 dB |
| 2 | le même AUX kicks | tous deux à 3 dB (H75 : −4,0 · −0,9) | l'un au-delà de 6 dB |
| 3 | \|médian\| sub · basse · bas-médium (outil du § 0) | chacune à moins de 0,5 dB de H75 (0,91 · 1,75 · 1,07), ou meilleure | l'une pire de plus de 1,5 dB |
| 4 | log-mel | ≤ 6,89 | > 7,08 |
| 5 | relevés : le profil du kick rendu (H94), le point B, f\* | — | — |

**La règle** : 1 à 4 tenus → sur « Reload » le kick est un échantillon, que la chaîne sait déjà tirer et n'a pas
choisi : la question devient sa DÉCISION de kit (pourquoi le TR-808 l'a emporté sur son propre échantillon), à instruire
en D-phase ; sinon au plus sévère. **Dit d'avance** : un échantillon tiré du stem porte les fuites de la séparation ; un
seul échantillon pour 555 frappes ne porte aucune variation ; un morceau.

### 56.1 Verdict de H95 (11/11/2026, écrit à 17 h 27) : 2, 3 et 4 TENUS, 1 en ÉCHEC de 0,6 dB — l'échantillon de la chaîne donne le meilleur log-mel de « Reload » (6,89 → 6,13) et garde trop de sub entre les kicks

`reconstruction/travail/reload-h95.py` (rejouable ; `reload-h95/h95.json`). Le témoin rend 6,89.

| kick de « Reload » | f\* | log-mel | \|médian\| sub · basse · bas-médium | aigus | rendu / original AUX kicks : sub · basse | ENTRE : sub · basse | profil du solo, 100-200 ms : sub · basse (dB sous sa tête) |
|---|---|---|---|---|---|---|---|
| TR-808 de H70 (H75, témoin) | 0,5822 | 6,89 | 0,91 · 1,75 · 1,07 | 1,97 | −4,0 · −0,9 | +8,5 · −14,8 | +5,4 · −21,4 |
| **A · `samples/kick.wav` (jugé)** | 0,9656 | **6,13** | 1,08 · 1,44 · **0,72** | 2,69 | −2,6 · 0,0 | **+6,6** · −1,6 | +2,2 · −9,4 |
| B · kick moyen de H69 (relevé) | 0,7647 | 6,59 | 1,57 · 1,89 · 1,73 | 2,06 | +0,7 · −0,2 | **+0,8** · −3,6 | −11,8 · −10,2 |
| (original, kick moyen du stem — § 55.2) | | | | | | | −11,1 · −10,2 |

| # | verdict, au point A |
|---|---|
| 0 | **tenu** |
| 1 | **en ÉCHEC** : entre les kicks, basse à −1,6 dB (H75 : −14,8) mais sub à +6,6 dB — 0,6 dB au-delà du seuil d'échec |
| 2 | **tenu** : −2,6 · 0,0 dB |
| 3 | **tenu** : sub +0,17, basse et bas-médium meilleurs |
| 4 | **tenu** : 6,13 — 0,76 dB sous H75, le plus grand gain d'un seul geste sur ce morceau depuis H72 |

**La règle**, au plus sévère : pas « 1 à 4 tenus », rien n'entre. **Ce que la table mesure** : l'échantillon de la chaîne
rend au mélange la basse d'entre les kicks (−14,8 → −1,6 dB) et le log-mel suit pour une fois (−0,76) ; il garde un sub
qui dure — son propre profil a, de 100 à 200 ms, un sub 2,2 dB AU-DESSUS de sa tête, là où le kick moyen de l'original
est 11,1 dB dessous. Le point B, qui est ce kick moyen, a le sub d'entre les kicks à +0,8 dB et le profil de l'original
à 1 dB près — et un log-mel moins bon (6,59), des bandes de l'outil moins bonnes que A : il n'était pas le point jugé, et
A et B ne se départagent pas ici (aigus : A 2,69, B 2,06 ; un échantillon large bande contre un kick moyen qui a perdu
ce qui ne se répète pas).

**Deux pistes, non mesurées** : pourquoi `samples/kick.wav` porte ce sub tenu (une frappe unique prise avec ce qui
sonnait dessous ? — à lire dans `vsm_drumkit.py` et sur le fichier) ; pourquoi la chaîne, qui avait cet échantillon, a
rendu le TR-808 (sa décision de kit). **La suite** (H96, à écrire AVANT sa mesure) : la seconde — rejouer la décision de
kit de la chaîne sur ce morceau, avec ses propres distances, pour savoir sur quel chiffre le TR-808 l'a emporté.

## 57. H96 — la question de H95 était déjà tranchée ailleurs ; ce qui reste à mesurer : la distance DE LA CHAÎNE voit-elle le gain de l'échantillon ? (écrite AVANT sa mesure, 11/11/2026, 17 h 35)

**Ce que j'aurais dû lire avant H95** (`reconstruire.py`, `ROADMAP-fusion.md` § 5 octies) : la chaîne n'arbitre PAS
entre la boîte à rythmes et ses échantillons — la batterie modélisée est son défaut, choisi et écrit (« on y gagne un
kit réglable, on y perd la fidélité littérale au coup enregistré »), et `--batterie-echantillonnee` rend l'autre
route. Elle a été mesurée sur *Sky and Sand* : +41 % sur le mélange (0,2521 → 0,3560), un échantillon par famille
portant « la fuite des autres pièces ». La question du § 56.1 (« sur quel chiffre le TR-808 l'a emporté ») n'a donc pas
d'objet sur « Reload » : sur aucun, c'est une règle. Et H95 ne la contredit pas d'emblée : autre morceau, une SEULE
pièce échantillonnée (le kick) et non le kit, autre juge (le log-mel de l'outil du § 0, pas la distance de la chaîne).

**Ce qui se mesure** (aucun rendu : `analyzer.vsm_reconstruct.reconstruction_distance`, la distance même de la chaîne,
métrique du rapport de la course et v2) : original contre le mélange de H75, celui de H95-A (kick par
`samples/kick.wav`) et celui de H95-B (kick moyen), et contre le silence (l'échelle).

**L'HYPOTHÈSE H96** : la distance de la chaîne voit le gain de H95-A comme le log-mel le voit (6,89 → 6,13, −11 %).

| # | mesure | réussite | échec |
|---|---|---|---|
| 1 | distance de la chaîne, H95-A rapportée à H75 | en baisse d'au moins 2 % | égale ou en hausse |
| 2 | relevés : les trois distances dans chaque métrique, le silence, H95-B | — | — |

**La règle** : 1 tenu → sur ce morceau les deux juges s'accordent, un kick échantillonné SEUL vaut mieux que le TR-808,
et la règle du § 5 octies (mesurée sur un kit entier, un autre morceau) mérite d'être rejouée pièce par pièce — à
proposer, pas à décider ici : c'est un choix de conduite du projet (réglable contre fidèle) ; 1 en échec → les deux
juges se contredisent, et c'est le juge qu'il faut instruire avant de toucher à la règle.

### 57.1 Verdict de H96 (11/11/2026, écrit à 17 h 36) : TENUE — la distance de la chaîne voit aussi le gain du kick échantillonné (−4,5 %)

`reload-h96.json`. Distance de la chaîne, métrique v2 (la clé de métrique n'a pas été trouvée dans la provenance du
rapport de la course : une seule métrique est donc mesurée, et c'est dit) :

| mélange de « Reload » | distance v2 | rapportée à H75 | log-mel (outil du § 0) |
|---|---|---|---|
| silence | 0,9236 | | |
| H75 (kick au TR-808) | 0,3458 | | 6,89 |
| **H95-A (kick par `samples/kick.wav`)** | **0,3302** | **−4,5 %** | 6,13 |
| H95-B (kick moyen de H69) | 0,3387 | −2,1 % | 6,59 |

1 **tenu** : −4,5 %. Les deux juges s'accordent, et dans le même ordre (A, puis B, puis le TR-808). **La règle** : « un
kick échantillonné SEUL vaut mieux que le TR-808 sur ce morceau, et la règle du § 5 octies (mesurée sur un kit entier,
un autre morceau) mérite d'être rejouée pièce par pièce — à proposer, pas à décider ici ».

**Ce qui est proposé, et laissé à la décision de l'utilisateur** (les documents sont clairs : la batterie modélisée
est un choix — « réglable » contre « fidèle au coup enregistré » —, pas un résultat de mesure sur chaque pièce) :
rejouer `ROADMAP-fusion` § 5 octies PIÈCE PAR PIÈCE — le kick échantillonné, le reste du kit modélisé — sur *Sky and
Sand*, « Reload » et « B4 », avec la distance de la chaîne. Ce que H87-H96 apportent à ce choix : sur « Reload », huit
réglages du kick du TR-808 n'ont pas approché ce que l'échantillon fait d'un geste, parce que le défaut est un timbre
(§ 55.2) ; et ce que l'échantillon coûte est mesuré aussi (sub d'entre les kicks +6,6 dB, § 56.1). **Rien n'entre** tant
que ce choix n'est pas fait.

## 58. H97 — décision de l'utilisateur : la batterie reste MODÉLISÉE ; chercher dans le parc une machine dont le kick a ce timbre (écrite AVANT son instrument et sa mesure, 11/11/2026, 17 h 40)

**La décision** (11/11, en réponse au § 57.1) : « cherche une machine dont le kick a ce timbre » — pas d'échantillon.
**Ce timbre**, mesuré au § 55.2 sur le kick moyen de l'original : sa basse (60-150 Hz) 14 à 15 dB au-dessus de son sub
(20-60 Hz) sur ses trois tranches de 100 ms, et tenue (sub 0 · −11,1 · −13,2 dB ; basse 0 · −10,2 · −13,0 dB sous leur
tête). **Ce que H87-H88 n'avaient pas balayé** : l'accord n'y dépassait pas 55 Hz — or un kick dont la queue reste à
60-150 Hz est accordé plus HAUT. Les quatre boîtes à rythmes du parc, chacune sur toute la moitié haute de sa plage :

| machine | accord (Hz) | décroissance (s) |
|---|---|---|
| `vsm.tr808` (30-90 Hz ; tenue 0) | 55 · 70 · 90 | 0,10 · 0,20 · 0,35 |
| `vsm.tr909` (30-90 Hz) | 55 · 70 · 90 | 0,10 · 0,20 · 0,32 |
| `vsm.fmdrums` (30-120 Hz) | 55 · 80 · 110 | 0,10 · 0,20 · 0,35 |
| `vsm.drums` (35-90 Hz) | 52 · 70 · 90 | 0,10 · 0,20 · 0,34 |

**L'instrument** (`analyse/mesure_h97.py`, son test, `reconstruction/travail/reload-h97.py`) : pour chacun des 36
solos, le profil de H94 du kick moyen aux 204 frappes isolées, et son **écart au profil de l'original** — la moyenne
des écarts absolus, en dB, sur sept chiffres : sub et basse à 100-200 et 200-300 ms (sous leur tête), et basse − sub
aux trois tranches. Le choix : le plus petit. Les TROIS meilleurs sont rendus au mélange à leur f\* et jugés comme H95.

**L'HYPOTHÈSE H97** : une machine du parc, accordée haut, a ce profil — et le mélange suit.

| # | mesure, au réglage choisi | réussite | échec |
|---|---|---|---|
| 0 | le témoin : le projet de H75 rejoue 6,89 ; le kick de H70 a l'écart de profil le plus grand de la table ou presque | relevé | REFUS si 6,89 manque |
| 1 | écart de profil | ≤ 3 dB (le kick de H70 : à relever) | > 6 dB (aucune machine n'a ce profil) |
| 2 | mélange rendu / original ENTRE les kicks : sub ET basse | tous deux à 3 dB (H75 : +8,5 · −14,8 ; H95-A : +6,6 · −1,6) | l'un au-delà de 6 dB |
| 3 | le même AUX kicks | tous deux à 3 dB | l'un au-delà de 6 dB |
| 4 | \|médian\| sub · basse · bas-médium (outil du § 0) | chacune à moins de 0,5 dB de H75 (0,91 · 1,75 · 1,07), ou meilleure | l'une pire de plus de 1,5 dB |
| 5 | log-mel | ≤ 6,89 (H75) ; relevé à côté de H95-A (6,13) | > 7,08 |
| 6 | relevés : la table entière des 36, les trois mélanges, la distance v2 de la chaîne | — | — |

**La règle** : 1 à 5 tenus → le kick de « Reload » se règle sur une machine du parc, par son PROFIL — la candidate à
rejouer sur « B4 » ; 1 en échec → aucune des quatre n'a ce timbre sur ces plages : la mesure demande une machine neuve
ou un réglage de plus (`CDC-nouvelle-machine.md`), et dit lequel par les chiffres du profil ; 1 tenu, 2 à 5 non → le
profil ne suffit pas à prédire le mélange, à dire. **Dit d'avance** : le profil ne lit rien au-dessus de 150 Hz (le
corps, le clic) ; les autres réglages de chaque machine restent à l'usine ; un morceau.

### 58.1 L'instrument de H97, AVANT toute mesure (11/11/2026, 17 h 41)

`analyse/mesure_h97.py` (`resume`, `ecart_profil`), deux tests sur les profils publiés au § 55.2 : un profil contre
lui-même, ou 12 dB plus fort, rend 0 ; le kick de H70 est à plus de 10 dB de l'original ; chacun des chiffres du résumé
pèse (7 dB sur une tranche → 2,0 dB d'écart). **Vu rouge sur trois mutants** (le niveau absolu compté ; une tranche
lue deux fois ; les écarts sans valeur absolue). `ruff`, `mypy` propres. `reconstruction/travail/reload-h97.py` : la
fonction `rendre` de H87, plus la pose d'une autre machine sur la piste du kick (accord et décroissance seuls, le
reste à l'usine).

### 58.2 Verdict de H97 (11/11/2026, écrit à 18 h 08) : NON TENUE — `vsm.drums` à 70 Hz · 0,10 s a presque le profil (3,9 dB, le kick de H70 : 17,4) et remet le grave d'entre les kicks à 3 dB de l'original, mais le bas-médium casse et la distance de la chaîne monte

`reconstruction/travail/reload-h97.py` (rejouable ; `reload-h97/h97.json`). Le témoin rend 6,89. **La table entière**,
écart au profil de l'original, dB (décroissances dans l'ordre du § 58) :

| machine | accord bas | accord moyen | accord haut |
|---|---|---|---|
| `vsm.tr808` (55 · 70 · 90 Hz) | 14,7 · 18,1 · 21,2 | 9,8 · 14,3 · 14,0 | 16,8 · 19,2 · 21,3 |
| `vsm.tr909` (55 · 70 · 90 Hz) | 15,5 · 15,8 · 15,9 | 9,9 · 12,9 · 13,0 | 13,9 · 16,2 · 17,1 |
| `vsm.fmdrums` (55 · 80 · 110 Hz) | 9,9 · 11,0 · 13,4 | 6,4 · 6,8 · 9,4 | 10,7 · 8,3 · 10,2 |
| `vsm.drums` (52 · 70 · 90 Hz) | 16,4 · 15,4 · 14,1 | **3,9** · 5,2 · 6,5 | 14,0 · 12,7 · 12,5 |

Le kick de H70 (TR-808, 37 Hz · 0,05 s · tenue 0,04) : 17,4.

| kick de « Reload » | profil | f\* | log-mel | distance v2 | \|médian\| sub · basse · bas-médium | aigus | rendu / original AUX kicks : sub · basse | ENTRE : sub · basse |
|---|---|---|---|---|---|---|---|---|
| TR-808 de H70 (H75, témoin) | 17,4 | 0,5822 | 6,89 | 0,3458 | 0,91 · 1,75 · 1,07 | 1,97 | −4,0 · −0,9 | +8,5 · −14,8 |
| **`vsm.drums` 70 Hz · 0,10 s (choisi)** | 3,9 | 1,0993 | 6,81 | 0,3581 | 1,66 · 2,08 · **2,64** | 2,28 | −1,4 · 0,0 | **−3,0 · −2,5** |
| `vsm.drums` 70 Hz · 0,20 s | 5,2 | 0,8934 | 6,82 | 0,3687 | 2,06 · 2,35 · 3,18 | 1,90 | −2,1 · −0,1 | −0,6 · +2,4 |
| `vsm.fmdrums` 80 Hz · 0,10 s | 6,4 | 1,3643 | 7,24 | **0,3319** | 6,16 · 1,93 · 0,64 | 1,05 | −3,4 · +0,1 | −5,4 · +1,8 |
| (échantillon de la chaîne, H95-A — non retenu) | | 0,9656 | 6,13 | 0,3302 | 1,08 · 1,44 · 0,72 | 2,69 | −2,6 · 0,0 | +6,6 · −1,6 |

| # | verdict, au réglage choisi |
|---|---|
| 0 | **tenu** ; le kick de H70 est bien parmi les plus loin de la table (17,4) |
| 1 | **entre les deux** : 3,9 dB (réussite à 3, échec à 6) |
| 2 | **tenu, à la limite** : −3,0 · −2,5 dB entre les kicks — le premier kick MODÉLISÉ qui y tient les deux bandes |
| 3 | **tenu** : −1,4 · 0,0 dB |
| 4 | **en ÉCHEC** : bas-médium 2,64 (+1,57 sur H75) ; sub 1,66 (+0,75) |
| 5 | **tenu** : 6,81 |

**Rien n'entre.** Ce que la table mesure, sans plus : (a) accordé haut, un kick du parc approche le profil de
l'original à 3,9 dB là où celui qu'on réglait depuis H67 en était à 17,4 — l'accord était la variable que H87-H88
n'avaient pas balayée assez haut ; (b) c'est la batterie ACOUSTIQUE (`vsm.drums`), pas une boîte à rythmes, qui s'en
approche le plus ; (c) le bas-médium se dégrade avec elle (150-500 Hz : le profil n'y lit rien, c'était dit d'avance) ;
(d) **les juges se contredisent sur l'ordre** : au log-mel `vsm.drums` gagne (6,81) et `vsm.fmdrums` perd (7,24) ; à
la distance v2 de la chaîne c'est l'inverse (0,3581, PIRE que H75, contre 0,3319, meilleur que H75 et à 0,5 % de
l'échantillon). Aucun des trois points n'est meilleur que H75 aux deux.

**La suite** (H98, à écrire AVANT sa mesure) : le profil étendu au bas-médium (une troisième bande, 150-500 Hz — ce
qui a cassé), et un balayage FIN autour des deux régions trouvées — `vsm.drums` de 60 à 80 Hz, `vsm.fmdrums` de 70 à
90 Hz —, avec le réglage propre à chacune qui touche le corps (le batteur de l'une, le rapport et l'indice FM de
l'autre) ; jugé au log-mel ET à la distance de la chaîne, un point n'étant retenu que s'il bat H75 aux deux.

## 59. H98 — le profil étendu au bas-médium, et un balayage fin autour des deux régions de H97 (écrite AVANT son instrument et sa mesure, 11/11/2026, 18 h 15)

**L'instrument** : `mesure_h94` gagne une troisième bande (150-500 Hz) ; `mesure_h97.ecart_profil3` compare douze
chiffres — les sept de H97, plus le bas-médium à 100-200 et 200-300 ms sous sa tête, et bas-médium − basse aux trois
tranches. `reconstruction/travail/reload-h98.py`, rejouable.
**La grille ENTIÈRE** (72 solos, le reste de chaque machine à l'usine) :
- `vsm.drums` : accord 60 · 65 · 70 · 75 · 80 Hz × décroissance 0,08 · 0,10 · 0,15 s × batteur 0 · 0,5 · 1 ;
- `vsm.fmdrums` : accord 70 · 80 · 90 Hz × décroissance 0,10 · 0,15 · 0,20 s × indice FM 0 · 1,5 · 3.
Les TROIS plus petits écarts sont rendus au mélange à leur f\*, et jugés au log-mel ET à la distance v2 de la chaîne.

**L'HYPOTHÈSE H98** : avec le bas-médium dans le critère, un réglage bat le TR-808 de H75 aux DEUX juges.

| # | mesure, au réglage choisi (le plus petit écart) | réussite | échec |
|---|---|---|---|
| 0 | le témoin : H75 rejoue 6,89 | | REFUS |
| 1 | log-mel ET distance v2 | ≤ 6,89 ET ≤ 0,3458 | pires tous les deux |
| 2 | mélange rendu / original, sub et basse, AUX kicks et ENTRE | les quatre à 3 dB | l'un au-delà de 6 dB |
| 3 | \|médian\| sub · basse · bas-médium | chacune à moins de 0,5 dB de H75 (0,91 · 1,75 · 1,07), ou meilleure | l'une pire de plus de 1,5 dB |
| 4 | relevés : la grille entière, l'écart du kick de H70, les trois mélanges | — | — |

**La règle** : 1 à 3 tenus → le kick de « Reload » est réglé sur une machine du parc, par son profil : à rejouer sur
« B4 » ; aucun des TROIS points ne bat H75 aux deux juges → sur ces plages le parc n'a pas ce timbre, et les chiffres
du profil du meilleur diront ce qui lui manque (`CDC-nouvelle-machine.md`) ; sinon au plus sévère. **Dit d'avance** :
douze chiffres dont cinq neufs — le critère change, et un point de H97 ne se compare pas à un point de H98 par son
écart ; le rapport FM de `vsm.fmdrums` et les autres réglages restent à l'usine.

### 59.1 L'instrument de H98, AVANT toute mesure (11/11/2026, 18 h 15)

`mesure_h94.BANDES` porte une troisième bande (150-500 Hz) ; `mesure_h97.resume3` et `ecart_profil3`, un test de
plus : un corps 10 dB plus fort, de même forme, que l'ancien critère ne voit pas (0,0) et que le neuf compte (2,5 dB :
trois chiffres sur douze) ; un profil sans bas-médium est un refus. **Vu rouge sur deux mutants** (les trois chiffres
bas-médium − basse annulés ; le refus ôté). Onze tests H9x verts, `ruff` et `mypy` propres.

### 59.2 Verdict de H98 (11/11/2026, mesuré de 18 h 28 à 18 h 47, écrit à 18 h 56) : NON TENUE — aucun des trois points ne bat H75 aux deux juges ; le meilleur (`vsm.fmdrums` 70 Hz · 0,20 s · indice 1,5) l'égale à 0,03 dB et 0,4 % près

`reconstruction/travail/reload-h98.py` (rejouable ; `reload-h98/h98.json`). Le témoin rend 6,89. **La grille entière**,
écart de profil à trois bandes, dB — par accord, trois décroissances, et pour chacune les trois valeurs du troisième
réglage (batteur 0 · 0,5 · 1, ou indice FM 0 · 1,5 · 3) :

| machine · accord | décroissance courte | moyenne | longue |
|---|---|---|---|
| `vsm.drums` 60 Hz (0,08 · 0,10 · 0,15 s) | 10,3 · 10,3 · 10,3 | 9,9 · 9,9 · 9,9 | 9,5 · 9,5 · 9,5 |
| `vsm.drums` 65 Hz | 9,0 · 9,0 · 9,0 | 8,2 · 8,2 · 8,2 | 8,2 · 8,2 · 8,2 |
| `vsm.drums` 70 Hz | 10,9 · 10,9 · 10,9 | 9,6 · 9,6 · 9,6 | 8,9 · 8,9 · 8,9 |
| `vsm.drums` 75 Hz | 13,3 · 13,3 · 13,3 | 11,8 · 11,8 · 11,8 | 9,8 · 9,8 · 9,8 |
| `vsm.drums` 80 Hz | 13,4 · 13,4 · 13,4 | 11,8 · 11,8 · 11,9 | 9,5 · 9,5 · 9,5 |
| `vsm.fmdrums` 70 Hz (0,10 · 0,15 · 0,20 s) | 16,9 · 10,3 · 8,4 | 16,9 · 7,2 · 5,9 | 16,9 · **5,9** · 6,4 |
| `vsm.fmdrums` 80 Hz | 16,9 · 12,2 · 9,6 | 16,2 · 8,6 · 6,8 | 15,6 · 6,8 · 6,8 |
| `vsm.fmdrums` 90 Hz | 17,8 · 12,7 · 10,7 | 17,6 · 9,7 · 7,2 | 17,4 · 7,6 · 6,7 |

Le kick de H70 : 15,2.

| kick de « Reload » | profil (3 bandes) | f\* | log-mel | distance v2 | \|médian\| sub · basse · bas-médium | aigus | AUX kicks : sub · basse | ENTRE : sub · basse |
|---|---|---|---|---|---|---|---|---|
| TR-808 de H70 (H75, témoin) | 15,2 | 0,5822 | 6,89 | 0,3458 | 0,91 · 1,75 · 1,07 | 1,97 | −4,0 · −0,9 | +8,5 · −14,8 |
| **`vsm.fmdrums` 70 · 0,20 · 1,5 (choisi)** | 5,9 | 0,9519 | 6,92 | 0,3472 | 1,24 · 1,79 · **0,65** | 1,03 | +2,1 · −0,3 | +3,7 · +5,3 |
| `vsm.fmdrums` 70 · 0,15 · 3,0 | 5,9 | 1,0497 | 7,15 | 0,3390 | 1,91 · 1,20 · 1,27 | 1,09 | +4,2 · −0,5 | +3,0 · +3,8 |
| `vsm.fmdrums` 70 · 0,20 · 3,0 | 6,4 | 0,9978 | 7,17 | 0,3411 | 4,09 · 1,27 · 1,27 | 0,89 | +6,7 · −1,0 | +8,6 · +5,5 |

| # | verdict, au réglage choisi |
|---|---|
| 1 | **en ÉCHEC, de peu** : log-mel 6,92 (+0,03) ET distance v2 0,3472 (+0,4 %) — pires tous les deux, dans l'épaisseur du trait |
| 2 | **entre les deux** : aux kicks +2,1 · −0,3 ; entre les kicks +3,7 · +5,3 (H75 : +8,5 · −14,8) |
| 3 | **tenu** : sub +0,33, basse +0,04, bas-médium meilleur (0,65) |

**La règle** : « aucun des TROIS points ne bat H75 aux deux juges → sur ces plages le parc n'a pas ce timbre, et les
chiffres du profil du meilleur diront ce qui lui manque ». **Ils le disent** (douze chiffres : original → choisi) :
sub sous sa tête −11,1 · −13,2 → −6,5 · −11,7 ; **basse sous sa tête −10,2 · −13,0 → −0,9 · −5,4** (elle ne décroît
presque pas : 9 dB de trop à 100-200 ms) ; basse − sub 14,2 · 15,0 · 14,3 → 12,5 · 18,1 · 18,9 ; bas-médium sous sa
tête −17,3 · −17,8 → −18,2 · **−30,8** ; bas-médium − basse −7,6 · −14,8 · −12,4 → −1,5 · −18,8 · **−27,0**. Le kick de
l'original a une frappe nette puis une queue qui tombe de 10 dB et se TIENT, sur ses trois bandes à la fois ; celui de
`vsm.fmdrums` tient sa basse sans la faire tomber et perd son bas-médium. Ce qui manque est une ENVELOPPE à deux
temps — une chute rapide puis un palier — commune aux bandes : aucune des quatre machines n'a ce réglage (une seule
décroissance chacune).

**Ce que la grille dit de l'instrument, et qui n'est pas vérifié** : le batteur de `vsm.drums` ne change RIEN au
critère (trois chiffres identiques au dixième sur quinze cases). Soit il agit au-dessus de 500 Hz, soit le réglage
n'atteint pas le moteur — je n'ai pas posé de témoin comme en H80, et je ne tranche pas. Et `vsm.drums` ressort moins
bien qu'en H97 (8,2 au mieux contre 3,9 sur sept chiffres) : le bas-médium, que H97 ne lisait pas, est ce qui lui manque.

**Rien n'entre.** Bilan de la recherche demandée par l'utilisateur (H97-H98, 108 réglages de quatre machines) : accordé
à 70 Hz, un kick modélisé rapproche « Reload » de l'original entre les kicks (+8,5 · −14,8 → +3,7 · +5,3 dB) et sur le
bas-médium, à log-mel et distance égaux à ceux de H75 — sans les battre. **La suite, à décider** : la mesure désigne un
réglage qui manque — une tenue après la chute (le « hold » ajouté au TR-808 en H70 tient AVANT la chute ; il en faudrait
un palier APRÈS) — et c'est un chantier de machine (`CDC-nouvelle-machine.md`, `GUIDE-ajout-machine.md`), à écrire en
H99 avec son attendu : le profil de l'original à 3 dB.

## 60. H99 — décision de l'utilisateur : le palier s'ajoute au TR-808 ; « Kick Sustain », écrit AVANT son code et sa mesure (11/11/2026, 19 h 00)

**La décision** (11/11, en réponse au § 59.2) : « ajout au tr 808 ». **Ce que le profil demande** (§ 55.2, § 59.2) : une
frappe, une chute rapide d'une dizaine de décibels, puis un PALIER qui se tient et décroît lentement (−10 dB à
100-200 ms, −13 à 200-300 ms), la basse restant au-dessus du sub.

**Le code, à écrire** (arbre `reload-h70`, là où vit déjà « Kick Hold » — le moteur de toutes les mesures depuis H70 ;
mêmes fichiers que H70 : la voix, la liste des réglages, le descripteur, le panneau, les tests du cœur) :
- **Kick Sustain** (`drum.kick.sustain`, 0 à 1, défaut 0) : le niveau du palier, en amplitude sous la crête ;
- **Kick Sustain Decay** (`drum.kick.sustainDecay`, 0,05 à 2 s, défaut 0,5) : sa décroissance propre ;
- l'amplitude devient `max(enveloppe d'avant, sustain × enveloppe lente)` : la chute est celle de « Kick Decay »
  jusqu'à croiser le palier, qui prend la suite. **À 0, rien ne change, AU BIT PRÈS** — aucune reconstruction n'est à
  rejouer ; les deux réglages sont ajoutés en fin de liste (les identifiants enregistrés ne bougent pas), sans règle
  de recherche (la chaîne ne les explore pas tant qu'aucune mesure ne le demande).

**Les tests du cœur, écrits avec le code** : à 0 le rendu est identique au bit près ; avec un palier à 0,3 et une chute
de 0,05 s, la crête à 150-160 ms est à −10,5 dB ± 1,5 de la crête d'attaque (sans palier : sous −25 dB), et à
250-260 ms elle a encore décru de moins de 4 dB pour une décroissance lente de 0,5 s ; la voix reste active pendant le
palier.

**La mesure** (`reconstruction/travail/reload-h99.py`, sur « Reload », même protocole que H98) : accord 60 · 70 · 80 Hz
× chute 0,05 · 0,10 s × palier 0,2 · 0,3 · 0,45 × décroissance lente 0,2 · 0,3 · 0,5 s — 54 solos, profil à trois
bandes, les trois meilleurs rendus au mélange à leur f\*.

| # | mesure | réussite | échec |
|---|---|---|---|
| 0 | le témoin : avec le moteur NEUF, le projet de H75 rend un fichier IDENTIQUE à celui du moteur d'avant (`cmp`), et 6,89 | les deux | REFUS |
| 1 | écart de profil (trois bandes), réglage choisi | ≤ 3 dB (H70 : 15,2 ; meilleur du parc, H98 : 5,9) | > 5,9 dB (le palier n'apporte rien) |
| 2 | log-mel ET distance v2 | ≤ 6,89 ET ≤ 0,3458 | pires tous les deux |
| 3 | mélange rendu / original, sub et basse, AUX kicks et ENTRE | les quatre à 3 dB | l'un au-delà de 6 dB |
| 4 | \|médian\| sub · basse · bas-médium | chacune à moins de 0,5 dB de H75 (0,91 · 1,75 · 1,07), ou meilleure | l'une pire de plus de 1,5 dB |

**La règle** : 1 à 4 tenus → le kick de « Reload » est réglé sur le TR-808, et le réglage neuf a sa raison d'être : à
porter dans `master` par sa D-phase (`CDC-nouvelle-machine.md` : panneau, bancs, règle de recherche) et à rejouer sur
« B4 » ; 1 en échec → un palier ne suffit pas à faire ce profil — le réglage reste dans l'arbre à part et n'entre pas ;
sinon au plus sévère. **Dit d'avance** : le palier sonne à l'accord du kick, une sinusoïde écrêtée — le bas-médium de
l'original (−17 dB sous sa tête, tenu) n'est pas dans ce modèle ; un morceau.

### 60.1 Le code de H99 et ses tests, AVANT toute mesure d'un morceau (11/11/2026, 19 h 20)

Branche `reload-h70`, un commit, poussé (« Kick Sustain » et « Kick Sustain Decay » au TR-808 : la voix, la liste des
réglages, le descripteur — `drum.kick.sustain`, `drum.kick.sustainDecay` —, le panneau, les tests). Moteur de mesure
rebâti : `8163073d` (l'ancien, `1eff3b6d`, est gardé à côté). **Un attendu du § 60 était FAUX, et c'est mon calcul** :
j'y écrivais le palier « à −10,5 dB ± 1,5 à 150-160 ms » pour un réglage de 0,3 — j'avais oublié que le palier décroît
lui aussi (0,3 × e^(−0,15/0,5) = 0,222, soit **−13,1 dB**), et le premier test, écrit sur une autre erreur (l'écrêtage
porte sur le sinus, pas sur l'enveloppe), est tombé rouge sur un code juste : crêtes relevées 0,468 · 0,105 · 0,086, soit
−13,0 dB puis −1,7 dB. Le test attend désormais −13,1 ± 1,5 dB, moins de 4 dB de chute sur les 100 ms suivantes, et
moins de −25 dB sans palier ; à 0, le rendu est identique au bit près, quelle que soit la décroissance lente. **Vu
rouge sur un mutant** (le palier ôté du rendu). Cœur : 1 342 verts ; panneaux : 11 verts. Le témoin au bit près sur un
projet entier (attendu 0) est la première chose que fait `reconstruction/travail/reload-h99.py`.

### 60.2 Verdict de H99 (11/11/2026, mesuré de 20 h 16 à 20 h 33, écrit à 20 h 38) : 1 en ÉCHEC — le palier fait ce que ses tests disent, et il n'apporte RIEN au profil ; le réglage reste dans l'arbre à part

`reconstruction/travail/reload-h99.py` (rejouable ; `reload-h99/h99.json`). Moteur `8163073d`. **Attendu 0 tenu** : le
projet de H75 rendu par le moteur neuf est IDENTIQUE (`cmp`) à celui du moteur d'avant, et rejoue 6,89.

**La grille entière**, écart de profil à trois bandes, dB — palier 0,2 · 0,3 · 0,45, et pour chacun la décroissance lente
0,2 · 0,3 · 0,5 s :

| accord · chute | palier 0,2 | palier 0,3 | palier 0,45 |
|---|---|---|---|
| 60 Hz · 0,05 s | **8,6** · 9,0 · 9,2 | 9,1 · 9,3 · 9,3 | 9,2 · 9,2 · 9,1 |
| 60 Hz · 0,10 s | 9,1 · 9,1 · 9,0 | 9,1 · 9,1 · 9,1 | 9,1 · 9,2 · 9,2 |
| 70 Hz · 0,05 s | 9,5 · 9,6 · 10,1 | 9,5 · 10,0 · 10,2 | 9,7 · 10,1 · 9,9 |
| 70 Hz · 0,10 s | 9,3 · 9,5 · 9,8 | 9,4 · 9,8 · 10,1 | 9,7 · 10,2 · 10,1 |
| 80 Hz · 0,05 s | 12,1 · 12,1 · 12,0 | 12,0 · 11,8 · 11,5 | 11,5 · 11,2 · 10,9 |
| 80 Hz · 0,10 s | 11,5 · 11,5 · 11,5 | 11,5 · 11,6 · 11,4 | 11,5 · 11,4 · 11,2 |

Références : kick de H70 15,2 ; meilleur du parc sans palier (H98, `vsm.fmdrums`) 5,9.

| kick de « Reload » | profil | f\* | log-mel | distance v2 | \|médian\| sub · basse · bas-médium | AUX kicks : sub · basse | ENTRE : sub · basse |
|---|---|---|---|---|---|---|---|
| TR-808 de H70 (H75, témoin) | 15,2 | 0,5822 | 6,89 | 0,3458 | 0,91 · 1,75 · 1,07 | −4,0 · −0,9 | +8,5 · −14,8 |
| **60 Hz · 0,05 · palier 0,2 · lente 0,2 (choisi)** | 8,6 | 2,7231 | 7,13 | 0,3245 | 5,46 · 6,76 · 3,91 | −2,5 · +0,3 | +7,9 · +2,3 |
| 60 Hz · 0,05 · palier 0,2 · lente 0,3 | 9,0 | 2,6728 | 7,11 | 0,3205 | 2,81 · 6,04 · 3,89 | −1,7 · +0,2 | +11,3 · +4,3 |
| 60 Hz · 0,10 · palier 0,2 · lente 0,5 | 9,0 | 1,2934 | 6,85 | 0,3227 | 2,68 · 3,24 · 3,26 | −5,1 · +0,2 | +8,2 · +3,7 |

| # | verdict, au réglage choisi |
|---|---|
| 0 | **tenu** |
| 1 | **en ÉCHEC** : 8,6 dB — au-delà de 5,9, le meilleur du parc SANS palier ; la grille est plate (8,6 à 12,1 sur 54 cases) |
| 2 | ni l'un ni l'autre : log-mel 7,13 (pire), distance v2 0,3245 (meilleure) |
| 3 | **en ÉCHEC** : sub d'entre les kicks +7,9 dB |
| 4 | **en ÉCHEC** : sub +4,55, basse +5,01, bas-médium +2,84 |

**La règle** : « 1 en échec → un palier ne suffit pas à faire ce profil — le réglage reste dans l'arbre à part et
n'entre pas ». **Appliquée** : « Kick Sustain » reste sur `reload-h70`, il ne va pas dans `master`. Ce que le résumé
du meilleur dit (original → choisi) : sub sous sa tête −11,1 · −13,2 → −0,7 · **+5,1** (il MONTE) ; basse − sub
14,2 · 15,0 · 14,3 → 20,6 · 17,4 · **3,5** ; bas-médium sous sa tête −17,3 · −17,8 → −29,8 · −34,4. Le palier d'un sinus
accordé à 60 Hz tombe à cheval sur la frontière des deux bandes graves et n'a rien au-dessus — c'était écrit d'avance
(« le bas-médium de l'original n'est pas dans ce modèle »), et c'est la moitié du profil. Mon hypothèse du § 59.2 —
« ce qui manque est une enveloppe à deux temps » — était une lecture, pas une mesure : l'enveloppe est là, le profil
n'y est pas.

**Relevé, non interprété** : ces trois points ont la MEILLEURE distance v2 mesurée sur « Reload » (0,3205 à 0,3245 ;
l'échantillon de H95 : 0,3302 ; H75 : 0,3458) avec les pires bandes de l'outil du § 0. Depuis H97, sur neuf mélanges, la
distance de la chaîne et le log-mel rangent les kicks dans des ordres différents.

**Rien n'entre. Bilan de H97-H99** (162 réglages, un réglage neuf) : aucun kick modélisé — du parc ou avec palier —
n'a le profil de celui de « Reload » à moins de 5,9 dB, et aucun ne bat le TR-808 de H70 aux deux juges. **Ce qui
bloque maintenant n'est plus une machine mais les juges** : tant qu'ils se contredisent, « meilleur » n'a pas de sens,
et chaque réglage de plus choisit entre eux sans le dire. **La suite proposée** (H100, à écrire AVANT sa mesure) :
instruire ce désaccord sur les mélanges déjà rendus — ce que la distance v2 récompense et que le log-mel et les bandes
punissent —, sans un rendu de plus.

## 61. H100 — le désaccord des juges : quel TERME de la distance v2 porte ses gains sur « Reload » ? (écrite AVANT sa mesure, 11/11/2026, 21 h 09)

**Ce que le code dit** (`analyzer/audio_distance_v2.py`) : la distance v2 est une somme pondérée de sept termes
calculés sur le morceau ENTIER — centroïde 0,15, largeur de bande 0,10, roll-off 0,10, planéité 0,05, cepstre (MFCC
moyen) 0,20, enveloppe d'amplitude 0,20, contraste spectral 0,20. Le log-mel de l'outil du § 0, lui, compare trame à
trame. Aucun rendu, aucun code neuf (`reconstruction/travail/reload-h100.py`) : les sept termes de chaque mélange de
« Reload » déjà rendu depuis H95 (H75, H95-A et B, les trois de H97, de H98, de H99), à côté de leur log-mel.

**Le contrôle, avant toute lecture** : la somme pondérée des termes recalculés doit rendre la distance v2 publiée de
chaque mélange à 0,0005 près. Sinon REFUS — je ne décompose pas ce que je ne sais pas recomposer.

**L'HYPOTHÈSE H100** : sur les points où v2 gagne et le log-mel perd (les trois de H99, `vsm.fmdrums` de H97), UN seul
terme porte plus de 60 % du gain de v2 sur H75 — je crois l'ENVELOPPE (un kick plus fort creuse l'enveloppe comme
l'original), dit d'avance pour être démenti. **Échec** : aucun terme ne porte 40 %.

**La règle** : tenue → sur ces mélanges le verdict de v2 est celui de ce terme, et le désaccord des juges se ramène à
une question nommée (ce terme-là contre le log-mel) ; échec → le gain est diffus, et il faudra comparer les juges
autrement (par l'écoute, ou par des perturbations connues comme au § 0). **Rien ne se décide ici sur les juges** : la
métrique de la chaîne est un choix de `ROADMAP-fusion`, pas de ce cahier. **Dit d'avance** : douze mélanges d'un seul
morceau, qui ne diffèrent que par leur kick.
