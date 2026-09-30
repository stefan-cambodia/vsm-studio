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
