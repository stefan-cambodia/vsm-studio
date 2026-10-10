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

