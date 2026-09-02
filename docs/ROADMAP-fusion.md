# Feuille de route globale — après la fusion des deux projets

**Situation.** Le projet d'analyse (`analyse/`) et le DAW ne sont plus deux
dépôts qui se parlent par fichiers : ils vivent ensemble. L'objectif commun est
désormais explicite :

```
fichier WAV  ->  MIDI + patchs (CLAP / presets)  ->  rendu  ->  comparaison
```

Reconstruire un enregistrement sous forme de **notes rejouables** et de
**patchs chargeables**, puis mesurer l'écart avec l'original.

Ce document remplace, pour la suite, le découpage en phases d'`ARCHITECTURE.md`
(phases 1 à 7, toutes terminées) et l'addendum
[`ROADMAP-interop.md`](ROADMAP-interop.md) (P2 à P9, tous faits).

**Un troisième axe s'est ouvert depuis**, et il ne concerne ni l'analyse ni le
rendu : [`ROADMAP-daw.md`](ROADMAP-daw.md) traite du **logiciel lui-même** —
piste audio, clip, enregistrement, sauvegarde, console, hébergement de plugins.
Ce document-ci juge le DAW comme la **référence du rendu** ; l'autre le juge
comme un **lieu de travail**, et ce second critère n'avait jamais été écrit.

---

## 0. Partage des rôles — à ne pas brouiller

| | Rôle | Ne fait jamais |
|---|---|---|
| **`analyse/` (Python)** | entendre, décider : séparation, transcription, classification, recherche de patch | produire du son destiné à être écouté |
| **`core/` + `audio/` (C++)** | produire le son, et c'est la **référence** | connaître Python, JSON, le réseau |
| **`interchange/`** | traduire entre les deux : formats, identités sémantiques, service de rendu | entrer dans le chemin temps réel |

**La règle qui prime** : le moteur est la source de vérité du rendu. Ce que
Python optimise doit être exactement ce que le DAW joue — sinon on optimise un
son que personne n'entendra. C'est déjà le cas depuis le pont
(`analyse/PONT-VSM.md`) ; toute évolution doit le préserver.

Deuxième règle, héritée de l'interop : **le DAW reste autonome**. Il se compile,
se teste et s'utilise sans Python, sans réseau, sans CLAP. `analyse/` est un
client, jamais une dépendance.

---

## 1. Où on en est

**Acquis, mesuré :**

- **33 machines (+ tonalité d'essai), 13 effets**, moteur temps réel,
  **1 216 tests moteur + 60 tests d'analyse** verts, zéro warning.
  *(Ces trois chiffres disaient « 24 machines, 9 effets, 760 tests » jusqu'au
  31/08/2026 : ils avaient été écrits une fois et jamais repris, pendant que le
  parc et la suite grossissaient sous eux. Recomptés à la source —
  `BuiltInPlugins.cpp`, `EffectFactory.cpp`, les six suites — et non recopiés.
  `vsm.cone` et `vsm.flute` n'en font pas partie : elles sont dans l'arbre,
  hors build, comme résultats négatifs documentés.)*
- Piano roll complet, façades « façon hardware » pour les machines,
  séquenceurs à pas pour celles qui en ont un.
- Interop : identités sémantiques (**835 paramètres**), presets `*.synth.json`,
  projets `project.json`, rendu hors ligne `vsm-render`, adaptateur et hôte
  CLAP.
- Pont Python : rendu d'une note par le moteur réel en ~10 ms, déterministe au
  bit près ; recherche de patch fonctionnelle ; choix de machine par distance.

**Limites connues, chiffrées :**

- ~~La recherche approche sans retrouver~~ — **levé par l'étape 8.2**. Sur la
  même cible Minimoog, à budget égal, l'espace déclaré par la machine remplace
  l'espace écrit en dur : distance médiane **0,625 → 0,368**, et **0,029** en
  quadruplant le budget, avec la coupure retrouvée à 884 / 899 / 908 Hz pour
  900 visés (contre 864 / 827 / 1228 avec l'ancien espace).
- ~~La couverture instrumentale s'est comblée côté synthèse, mais basse,
  guitare et cordes réelles passent toujours par le sampler faute de modèle
  — **CETTE PHRASE EST DATÉE, corrigée le 02/09/2026 par l'hypothèse H10 du
  CDC machines-manquantes** : `vsm.string` couvre la guitare, y compris
  ÉLECTRIQUE, et la mesure le dit (h2 = 1,363 contre 1,387 relevé sur 36 zones
  de « Clean Guitar » réelle, h3 = 1,710 contre 1,967). Ce qui manque encore
  n'est pas une machine mais deux effets d'insert : la saturation d'ampli et
  la bande étroite du haut-parleur
  dédié~~ — **levé par `vsm.string`** (§ 10 de
  [`CDC-machines-manquantes.md`](CDC-machines-manquantes.md), chiffres dans
  ARCHITECTURE.md § 32). La corde — pincée ET frottée — est modélisée par
  guide d'ondes. Sur un violoncelle à l'archet réel, cherchée parmi les
  dix-sept machines mélodiques sans présélection, elle arrive **première au
  budget par défaut** (0,1225 contre 0,1310, sur les trois graines) et **perd à
  60 itérations** (0,1190 contre 0,0865 au SH-101, sur les trois graines
  aussi) : ses dix axes sont contraints par la physique, elle les épuise vite
  et bute sur un plafond que la recherche ne peut pas franchir, là où un
  soustractif plie son enveloppe spectrale aussi longtemps qu'on lui paie des
  évaluations. La couverture est acquise ; la victoire est conditionnelle, et
  la condition est écrite.
- ~~Il ne reste qu'une case vide au tableau de couverture des sources : les
  cuivres et les bois~~ — **traité par `vsm.wind`**, mais à MOITIÉ, et la
  moitié manquante est mesurée : les perces cylindriques (clarinette, et en
  première approximation les cuivres) sont couvertes ; les perces coniques
  (saxophone, hautbois) et les flûtes ne le sont pas. La raison est
  structurelle et vérifiée sur quatre topologies de boucle : une réflexion
  inversante à demi-longueur impose la symétrie demi-onde, qui interdit
  mathématiquement les harmoniques paires. Détail dans ARCHITECTURE.md § 33.
  **CETTE LIGNE ÉTAIT FAUSSE, ET LA MESURE L'A DIT (29/08/2026).** Elle
  annonçait qu'un prototype conique (`audio/plugins/cone/`, hors build)
  « sonne juste, tient son niveau et porte la série harmonique complète ».
  Le prototype a été entré dans le build, doté de ses identités sémantiques et
  MESURÉ par le pont Python, à neuf couples (souffle, raideur d'anche) : il
  produit une **sinusoïde pure**. Rapportés au fondamental, ses rangs 2 à 6
  valent 0,001 à 0,009 — c'est-à-dire rien —, quand `vsm.wind` porte les siens
  (h3 = 0,156, h5 = 0,753) comme sa physique l'exige. Il n'auto-oscille pas :
  c'est un résonateur qui sonne à `f0`, pas une anche. Sept de ses quinze tests
  échouent, dont celui de son trait distinctif, et deux d'entre eux réclament un
  réglage « Cone Taper » que l'en-tête de la machine a délibérément écarté — le
  prototype a été laissé au milieu d'un changement de conception.
  Il est donc RESSORTI du build, qui est redevenu vert (543 tests), et la case
  du tableau de couverture reste **vide**. Ce qui manque n'est pas la
  sélectivité d'un résonateur : c'est l'auto-oscillation, que l'en-tête de
  `ConeSynth.h` documente déjà comme ayant échoué par trois routes (guide
  d'ondes inversant, non inversant, banc modal). Cette mesure est la quatrième.
  **La leçon vaut plus que la machine : une affirmation de gain écrite sans son
  chiffre finit par être crue.** Celle-ci l'a été pendant une semaine.
- ~~**Le sampler n'est plus un repli universel : il est réservé à la voix.**~~
  — **la case est remplie (29/08/2026)** : `vsm.vocal` modélise le conduit
  vocal (source glottique, trois formants, cinq voyelles en continu), et son
  trait distinctif est la définition même d'une voix — les formants ne suivent
  pas la note chantée, ce qu'aucun filtre du parc ne sait faire. Détail dans
  ARCHITECTURE.md § 40. Le report par le sampler reste le bon choix pour
  reconstruire un couplet, parce qu'une voix humaine n'est pas synthétisable à
  l'identique ; ce que le parc gagne, c'est le TIMBRE vocal — un chœur, une
  nappe, une voyelle tenue —, qu'il ne pouvait produire d'aucune façon.
  Le paragraphe d'origine, qui reste vrai pour la reconstruction :
  C'est ce qui a rendu nécessaires `vsm.piano` et `vsm.drums`, et ce qui donne
  au parc sa forme finale — chaque source a une machine qui la MODÉLISE, sauf
  la voix, qui est reportée telle quelle et présentée comme telle.
- ~~Une recherche coûte ~13 s par note et par machine~~ — **la question est
  close autrement (31/08/2026)** : la recherche note à note n'est plus dans la
  chaîne par défaut. Le § 5 undecies a mesuré que ses patchs perdaient contre
  l'usine six fois sur huit, et l'A/B sur trois morceaux a rendu deux
  reconstructions identiques à la sixième décimale et une meilleure de 2,1 %
  sans elle. **Un morceau entier se reconstruit en 3 300 à 4 500 s** (55 à
  75 min) au budget élevé — v4, 60 itérations, 120 évaluations de réglage,
  classifieur de frappes — contre 12 770 s avec la recherche, chaîne contre
  chaîne sur *Knight of the Jaguar*. L'ancienne chaîne reste entière derrière
  `--avec-recherche`, en témoin d'A/B. **Et le GPU n'y change presque rien** :
  la séparation est la seule étape accélérable (31,7 s pour cinq minutes
  d'audio sur l'iGPU Intel — README, « la séparation tourne sur l'iGPU ») ;
  le temps est dans l'arbitrage et le réglage de piste, qui rendent l'audio
  par `vsm-render` et ne quitteront pas le CPU.
- **Le budget de recherche change les distances d'un facteur deux** (basse :
  0,103 à 20 itérations, 0,053 à 60) : deux mesures ne se comparent que si
  elles ont le même budget. Il est pour cela inscrit dans chaque
  `rapport.json`, au même titre que la métrique.

- ~~Aucune route honnête pour un instrument ACOUSTIQUE mélodique~~ — **ouverte
  par `vsm.multisample`** (§ 5 sexies ci-dessous, détail dans ARCHITECTURE.md
  § 35). Le constat qui l'a rendue nécessaire est une photo-finish, pas un
  raisonnement : sur *Clair de Lune* (piano seul, 2219 notes), les huit
  premières machines du parc finissent entre **0,2590 et 0,3217** — six pour
  cent d'écart, c'est-à-dire aucune information. La distance globale (1,639)
  était rattrapée non par le patch mais par l'automation de coupure, 8,34 →
  1,64 en six cent six points : **le parc compensait au lieu de reproduire.**
  La machine est livrée, testée et mesurée — **et le banc de clôture dit qu'elle
  n'apporte rien sur ce morceau** : 0,2159 avec elle, 0,2159 sans, à conditions
  strictement identiques. La couverture est ouverte (l'orchestre General MIDI
  arrive par l'import SoundFont) ; la victoire sur le piano n'est pas acquise,
  et le § 5 sexies dit pourquoi elle ne le sera peut-être jamais par ce
  chemin-là.

**Où en est la feuille de route :** les phases 8 à 11 sont TOUTES closes — la
couverture des sources, la reconstruction d'un morceau entier, l'efficacité de
la recherche et le bouclage dans le DAW. Ce qui reste ouvert n'est plus une
étape planifiée mais les limites énumérées ci-dessus et le §6, « ce qui n'est
pas au programme, et pourquoi ».

**Une leçon de méthode s'ajoute aux précédentes, et elle a failli coûter une
fausse conclusion.** La mesure d'acceptation de `vsm.string` a d'abord classé
la machine 14e sur 17 sur une cible qui est LITTÉRALEMENT une corde. Trois
défauts réels du modèle sont sortis de l'enquête ; mais le gros de l'écart
venait du protocole : le `gate` — la proportion de l'extrait pendant laquelle
la note est tenue — valait 0,95 face à une cible qui se tait à 0,24. Chaque
candidate jouait donc pendant 0,95 s contre une cible éteinte depuis 0,24 s,
ce qui pénalise spécifiquement tout modèle **entretenu** (l'archet, l'orgue),
incapable de s'arrêter tant que la touche est tenue, là où un soustractif
referme simplement son enveloppe. Le classement mesurait la résistance à une
erreur de protocole, pas les machines. Corrigé, la même machine au même budget
et à la même graine passe de 0,2456 à 0,1225 et prend la première place.
C'est la même règle qu'au § 10.3 et à la septième passe de House Of God, sous
une troisième forme : **une distance n'est un chiffre que si l'on sait à
quelles conditions elle a été obtenue** — et le `gate` en fait partie au même
titre que la métrique et le budget. Le correctif est donc le même que les deux
fois précédentes : le `gate` de chaque stem est désormais **inscrit dans
`rapport.json`**, à côté de sa distance. Il y est par stem et non en tête du
document, parce qu'il dépend de la note de référence choisie et diffère donc
d'un stem à l'autre.

---

## 2. Phase 8 — Couvrir les sources réelles

**But** : qu'un stem quelconque ait une machine cible plausible.

| Étape | Contenu | Terminé quand |
|---|---|---|
| ~~8.1~~ | ~~`vsm.sampler`~~ **fait, seize emplacements** | un stem de batterie découpé par `analyse/` se rejoue et la distance à l'original est mesurée |
| ~~8.2~~ | ~~Profil de recherche déclaré par machine~~ **fait** — `interchange/SearchProfile`, exposé par `vsm-render --serve` (`query: searchProfile`), consommé par `VsmEngine.search_profile` | l'optimiseur ne code plus aucune borne ; mesure A/B publiée ci-dessus |
| ~~8.3~~ | ~~`vsm.generic`~~ **fait** — DSP, tests, identités sémantiques, façade, empreinte | critère d'origine **NON tenu tel qu'écrit**, et c'est mesuré : voir ci-dessous |
| ~~8.4~~ | ~~`vsm.drumkit`~~ **ne sera pas écrit** — `vsm.sampler` EST déjà cette machine | décision tranchée et motivée dans [`CDC-machines-manquantes.md`](CDC-machines-manquantes.md) §5 |

**Phase 8 close.** Les quatre étapes sont réglées, deux par du code livré, une
par une mesure qui contredit son propre critère, une par un refus argumenté.

### 8.3 — le critère demandait la mauvaise chose, et la mesure l'a dit

Le critère écrit était : « sur cinq stems synthétiques, distance INFÉRIEURE à
la meilleure machine de caractère ». Il n'est pas tenu, et il ne DEVAIT pas
l'être — chiffres complets dans ARCHITECTURE.md §31 :

- sur de l'audio **propre** sorti d'une machine du parc, la machine d'origine
  gagne toujours ; le generic ne monte sur aucun podium. C'est attendu : la
  cible EST la signature de cette machine-là ;
- sur l'audio **réellement à reconstruire** — un stem passé par la séparation,
  teinté d'artefacts, sans machine d'origine évidente — le generic **gagne** le
  stem de basse : 0,139 contre 0,149 au meilleur sans lui, la vraie machine
  (sh101) même pas sur le podium.

La machine tient donc sa promesse exactement là où le cahier des charges la
destinait, et le critère se trompait de terrain : il mesurait sur du son propre
une machine faite pour du son sale. Il est remplacé par celui-ci, qui est celui
qu'on vérifie désormais : **sur un stem séparé, battre la meilleure machine de
caractère.**

Deux effets de bord mesurés, à ne pas oublier : le generic est la seule machine
qui PROFITE d'un espace élargi (0,190 → 0,132 sur la basse en passant de 6 à
20 axes), donc le plafond de six dimensions du budget par défaut le pénalise
structurellement ; et ajouter une candidate ne vole jamais l'identification,
mais peut coûter une place de finaliste à la machine qui aurait gagné
(+0,003 sur la nappe) — `shortlist=0` le supprime quand l'exactitude prime.

### 8.4 — la machine existait déjà sous un autre nom

`vsm.drumkit` devait être « 8 à 16 pièces, une colonne de réglages par pièce,
grille de 16 pas ». En allant l'écrire, on a constaté que `vsm.sampler` livrait
déjà chacun de ces points : seize emplacements, seize sections de quatre
réglages, grille de seize pas, mapping où la note SÉLECTIONNE la pièce au lieu
de transposer, notes par défaut de la convention General MIDI, groupes de
coupure pour la charleston. Il manquait le NOM DES PIÈCES — la façade disait
« SLOT 3 » là où une boîte à rythmes dit « HH CL ». C'est ce qui a été corrigé,
et c'est tout ce qu'il fallait.

Une seconde machine aurait partagé moteur, paramètres, identités sémantiques et
façade avec la première, pour un changement d'étiquettes : une empreinte de
plus, une table sémantique identique de plus, une suite de tests de plus, une
entrée de plus dans chaque liste de choix. C'est précisément ce contre quoi le
§7 du cahier des charges met en garde — « elles élargissent le catalogue, pas
la couverture ». Limite assumée et écrite : les noms affichés sont ceux que la
convention General MIDI met à ces notes PAR DÉFAUT, donc un emplacement dont on
change la note de déclenchement porte une étiquette inexacte ; le numéro
d'emplacement, lui, ne ment jamais.

Détail des exigences : [`CDC-machines-manquantes.md`](CDC-machines-manquantes.md).
Obligations générales : [`CDC-nouvelle-machine.md`](CDC-nouvelle-machine.md).

### Ce que l'étape 8.2 a appris, et qui vaut pour la suite

**Une mauvaise borne est pire que pas de dimension du tout.** En bornant la
résonance à une fenêtre « utile » fixe de 0..0,8, on rendait inatteignable la
valeur 2,2 d'une cible Minimoog -- dont le paramètre va en réalité jusqu'à 4,2,
là où celui de la TB-303 s'arrête à 1. La recherche compensait en faussant la
coupure (1190 Hz pour 900 visés) et la distance finale doublait. Règle retenue :
une fenêtre fixe n'est légitime que pour une grandeur à **unité absolue**
(hertz, secondes) ; pour tout le reste, c'est la plage déclarée qui fait foi.

**Le nombre de dimensions se mesure, il ne se choisit pas.** À budget constant,
sur la même cible : 4 dimensions donnent 0,220 (la résonance sort de l'espace),
5 donnent 0,029, 6 donnent 0,043, et 8 donnent 0,331 avec des résultats variant
d'un facteur huit selon la graine. Le défaut est donc à 6, et il est
explicitement lié au budget d'itérations.

**L'instance 1 est la principale.** Sans décote sur les instances suivantes,
`envelope.2.attack` (enveloppe de filtre) évinçait `filter.1.resonance` de
l'espace cherché, alors que la résonance pèse bien davantage sur le timbre.

## 3. Phase 9 — Reconstruire un morceau, pas une note

Aujourd'hui la chaîne s'arrête à la note. Il manque l'assemblage.

| Étape | Contenu | Terminé quand |
|---|---|---|
| ~~9.1~~ | ~~Export en dossier de projet complet~~ **fait** — `analyzer/vsm_project_export.py` | `vsm-render` rejoue le projet exporté sans intervention |
| ~~9.2~~ | ~~Une piste par stem, machine retenue et patch~~ **fait** — `analyzer/vsm_reconstruct.py`, via le moteur RÉEL et non le synthé Python approximatif | appariement piste↔machine prouvé par corrélation (1,0000 contre 0,0098 pour l'hypothèse inverse) |
| ~~9.3~~ | ~~Rapport de reconstruction~~ **fait** — `rapport.json` : distance par stem, **et toutes les machines écartées avec leur score** | le rapport dit où la reconstruction échoue |
| ~~9.4~~ | ~~Rendu comparatif~~ **fait** — `comparaison.wav`, original à gauche, reconstruction à droite | produit à chaque exécution |
| ~~9.5~~ | ~~Batterie : brancher le sampler~~ **fait, seize emplacements** — `analyzer/vsm_drumkit.py`, plus le transport des échantillons dans le format de projet | le stem `drums` est découpé, rejoué par le sampler, et mesuré |

**Critère de réussite de la phase** — celui du §9 de `ROADMAP-interop.md`, enfin
mesurable de bout en bout :

```
original.wav -> analyse -> (MIDI + presets + project.json)
             -> vsm-render -> reconstructed.wav
             -> distance publiée
```

### État : la chaîne tourne de bout en bout

Commande : `analyse/reconstruire.py fichier.mp3 --sortie dossier`.

Vérifiée sur une vérité terrain (deux stems produits par le moteur, machines et
patchs connus du seul auteur du test, puis reconstruits à l'aveugle parmi
quinze machines candidates) :

| Stem | Machine réelle | Machine retenue | Distance |
|---|---|---|---|
| Basse | `vsm.sh101` | **`vsm.sh101`** ✔ | 0,291 |
| Nappe | `vsm.juno106` | `vsm.pcmhybrid` ✘ | 0,708 |

Distance globale original ↔ reconstruction : **0,670**, contre 59,65 pour du
silence.

**Pourquoi la nappe est ratée, et ce que ça dit.** Ce n'est PAS l'espace de
recherche : à budget suffisant, le Juno-106 descend à 0,374 et bat le
`pcmhybrid` (0,478). Au budget par défaut (20 itérations) il reste à 0,826 et
perd. Le choix de machine est donc **limité par le budget**, pas par la
description des machines -- ce qui place le correctif en phase 10 et le chiffre
noir sur blanc.

**Un défaut du vocabulaire sémantique, révélé au passage — corrigé depuis.**
`filter.N.cutoff` ne disait pas le TYPE du filtre. Sur le Juno-106, le
coupe-bas (`filter.2.cutoff`, une commande mineure) recevait donc presque la
même importance que le passe-bas principal et occupait le rang 3 de l'espace
cherché.

La correction distingue les types là où c'est le type qui compte : les
coupe-bas CORRECTEURS (Juno-106, Jupiter-8, ARP Odyssey, supersaw) s'écrivent
désormais `filter.hp.cutoff` et descendent au rang qui est le leur (13e sur le
Juno, importance 0,52) ; la résonance et l'enveloppe du filtre principal
rentrent du même coup dans l'espace réellement cherché. Le MS-20 est l'unique
exception, voulue : son HPF est RÉSONANT, second filtre à part entière, et
garde `filter.2.*` avec son rang de vrai filtre (3e). Un test verrouille les
deux côtés de la distinction.

Le renommage est un changement CASSANT, fait sciemment : les `clap_id` de ces
quatre paramètres changent, et un preset ancien qui portait `filter.2.cutoff`
pour l'une de ces machines est désormais signalé « non pris en charge » --
signalé, pas ignoré : le rapport d'application le nomme.

**La séparation en stems n'est pas un confort.** Le même mélange traité en une
seule piste (`--sans-separation`) donne une distance de 12,3 au lieu de 0,67 :
une machine ne reproduit pas deux instruments à la fois.

**Depuis : la vérité terrain a été rejouée avec `vsm.generic` dans le parc**
(chiffres complets dans ARCHITECTURE.md §31), et la mesure a corrigé deux
défauts de la chaîne elle-même : l'étape finale cherchait `vsm-render` par le
PATH et échouait après des minutes de calcul (résolution par
`find_vsm_render`) ; et la séparation demucs gardait son défaut `shifts=1`, un
décalage aléatoire non seedé qui rendait deux exécutions incomparables — 101
notes transcrites contre 156 sur le même fichier. Elle est désormais
déterministe (`shifts=0`), ce qui est la condition de toute comparaison
avant/après.

### Premier morceau réel de bout en bout : House Of God (D.H.S., 1995)

La chaîne a été confrontée à un enregistrement du commerce — de l'acid house,
le terrain rêvé du parc — et chaque faiblesse trouvée a été corrigée puis
re-mesurée, en six passes. Bilan chiffré :

| Stem | Première passe | Sixième passe |
|---|---|---|
| basse | generic, d=0,131 | generic, **d=0,053** |
| batterie | 813 frappes dans UNE pièce | **6 pièces**, 1001 frappes, échantillons isolés |
| nappes | generic, d=0,221 | epiano, d=0,180 |
| voix | ms20, d=0,143 | ms20, d=0,113 |

Ces deux colonnes ne sont PAS au même budget de recherche : la première passe
tournait au défaut (20 itérations), la sixième à 60. Le rapport ne l'écrivait
pas alors ; il l'écrit désormais, et la septième passe raconte pourquoi.

Les volumes de pistes sont désormais MESURÉS (rendu solo contre RMS du stem :
1,31 / 0,66 / 1,06 / 0,96 au lieu de 0,9 partout), et l'automation de coupure
a été mise à l'épreuve stem par stem — et DÉCLINÉE partout sur ce morceau,
chaque refus avec son motif mesuré : le patch de basse trouvé ne répond pas à
la coupure (quasi passe-bande), l'e-piano n'en a pas, et sur la voix l'A/B a
tranché (4,324 → 4,358). Une reconstruction identique à la 4e décimale sur
deux exécutions complètes confirme le déterminisme de toute la chaîne.

Ce que ce morceau a rapporté au projet dépasse ses propres chiffres : le
classement de batterie par gabarits (l'inversion kick/charleston n'était
visible que sur un vrai kick de club), la règle de recherche à deux étages,
le calage des volumes, cinq défauts de chaîne corrigés (chemin du moteur à
l'étape finale, séparation non seedée, export hors de la vie du moteur,
exceptions muettes, budget de recherche absent du rapport) — chacun committé
avec sa mesure.

### Septième passe : la batterie joue au bon endroit, et un défaut de méthode

L'oreille de l'utilisateur avait entendu deux choses sur la sixième passe :
« les pièces ne jouent pas au bon endroit » et « un son bizarre qui n'a rien à
faire là ». Chacune avait une cause mesurable ; voici ce que le correctif a
donné, à **budget de recherche égal** (60 itérations, comme la sixième passe) :

| | v6 | v7 |
|---|---|---|
| frappes de batterie | 1 001 | **809** |
| instants distincts | 812 | 809 |
| **co-frappes** (deux pièces au même instant) | **188** | **0** |
| `snare.wav` | 1 235 ms | **348 ms** |
| `percussion.wav` | 1 223 ms | **784 ms** |
| `kick.wav` / `hihat.wav` | 488 / 464 ms | inchangés à la ms |
| basse, nappes, voix | 0,053 / 0,180 / 0,113 | **identiques à la 4e décimale** |
| **DISTANCE GLOBALE** | 5,827 | **2,974** (silence : 54,7) |

Les 188 co-frappes tombaient toutes au même instant qu'une frappe voisine :
l'affectation multi-étiquettes faisait tirer ENSEMBLE les gabarits-variantes
d'une même pièce. La règle est devenue « une frappe, une pièce » — celle qui
explique le mieux la nouveauté de l'attaque. Les trois instants perdus sont des
onsets jumeaux (< 35 ms) fusionnés, qui produisaient des fla.

**Le gain est bien celui de la batterie, et rien d'autre.** Les trois stems
mélodiques sont reproduits au chiffre près, volumes et refus d'automation
compris ; l'écart global vient donc entièrement de la piste corrigée. Mesuré
directement, cette piste rendue SEULE contre son propre stem, à niveau efficace
identique (0,0980 contre 0,0985) :

| | distance à son stem | référence |
|---|---|---|
| v6 | 6,065 | silence : 53,63 |
| **v7** | **2,877** | — |

#### Le défaut de méthode : un budget qu'aucun rapport n'écrivait

Cette septième passe avait d'abord été lancée au budget PAR DÉFAUT (20
itérations), et ses stems mélodiques semblaient tous s'être DÉGRADÉS alors que
le correctif ne touchait que la batterie : basse 0,053 → 0,103, nappes 0,180 →
0,210, voix 0,113 → 0,135. Le soupçon était sérieux — une chaîne non
déterministe rendrait toute comparaison avant/après sans valeur, et le
déterminisme est un invariant déclaré du projet.

Chaque étage a donc été vérifié séparément, et chacun a tenu :

| étage | vérification | résultat |
|---|---|---|
| séparation | deux passes demucs sur le même fichier | 4 stems identiques **au bit près** |
| transcription | deux passes basic-pitch | 217 notes, même signature |
| note de référence | l'extrait de cible choisi | identique au bit près |
| moteur | deux rendus du même patch | identiques |
| recherche | deux fois le même optimiseur | même patch, mêmes 756 évaluations |
| stem complet | deux `reconstruct_stem` | même verdict, même podium |

La cause était dans la COMMANDE, pas dans le code :

| budget de recherche | distance de la basse |
|---|---|
| 20 itérations (le défaut) | 0,1031 |
| 40 | 0,0577 |
| **60** | **0,0532** ← la valeur de la sixième passe, à la 4e décimale |
| 80 | 0,0516 |

La sixième passe avait tourné à `--iterations 60`, la septième au défaut de 20.
Rien dans les deux `rapport.json` ne le disait, et deux chiffres incomparables
ont été mis côte à côte pendant une demi-heure d'enquête.

Le correctif est celui qui existait déjà pour la métrique : **le budget est
inscrit dans le rapport** (`"iterations"`) et affiché dans le résumé. La leçon
généralise ce que l'étape 10.3 avait appris — une distance n'est un chiffre que
si l'on sait à quelles conditions elle a été obtenue, et le budget en fait
partie au même titre que la métrique.

Une remarque qui vaut pour la suite : au budget par défaut, le rendu de la
basse est si faible que le calage de volume BUTE SUR SA BORNE (0,90 → 2,50
« borné », pour un rapport visé de 3,59). C'est un signal, et il est écrit dans
le journal : quand un volume est borné, c'est presque toujours que la
reconstruction en amont a échoué, pas qu'il faut amplifier davantage.

### Étape 9.5 — la batterie passe par le sampler

Le format de projet ne savait pas transporter d'échantillons : un projet
utilisant le sampler se chargeait **sans le moindre avertissement** et rendait
du silence. C'était le vrai trou. Trois ajouts, côté C++ :

- `SynthPreset::samples` (`emplacement -> chemin`), champ facultatif : les
  presets existants restent lisibles et le numéro de version ne bouge pas.
- `applyPresetSamples()`, qui charge par `ISampleLoader` et **rapporte chaque
  échec** — manquant, hors bornes, ou machine incapable d'en lire.
- Chemins **relatifs obligatoires** : un chemin absolu est refusé, car il
  s'ouvrirait sur la machine qui a produit le projet et nulle part ailleurs.

Huit tests verrouillent l'ensemble, dont le décisif : un projet au sampler rend
du son, plus du silence.

Côté analyse, le stem est découpé en coups, regroupé par famille, et un
représentant de chaque famille devient un échantillon du kit. Résultat mesuré
sur une batterie de vérité connue :

| | distance | référence |
|---|---|---|
| batterie originale ↔ reconstruite | **3,52** | silence : 39,2 |
| la même via la recherche de patch (avant) | 12,15 | — |

**Quatre approches essayées, trois abandonnées, et pourquoi.** Le détail est
dans le code, parce qu'il empêchera d'y revenir :

1. *Classer le spectre de chaque coup* — 30 coups trouvés sur 48, dont aucune
   grosse caisse. Quand deux pièces frappent ensemble, leur spectre mêlé ne se
   classe pas.
2. *Détecter les attaques bande par bande* — la résonance grave produisait ses
   propres fausses attaques : 63 pour 8 réelles.
3. *Mesurer une montée d'énergie par bande* — une charleston jouée en continu
   ne monte jamais : 12 détections sur 32.
4. *Comparer la PART de chaque bande dans l'énergie de l'attaque* — retenu.

Deux défauts de mise en œuvre trouvés par la mesure au passage : un filtrage en
mur de briques sur le fichier entier, qui étale chaque transitoire dans le
temps ; et un décalage d'indice de spectrogramme donnant 23 ms de regard **en
avant**, si bien que la fenêtre précédant une attaque contenait déjà l'attaque.

**Limite mesurée, et assumée.** Sur deux motifs de quatre mesures :

| motif | grosse caisse | caisse claire | charleston |
|---|---|---|---|
| charleston à la double-croche | 8 / 8 | 8 / 8 | 8 / 32 |
| charleston aux contretemps | 8 / 8 | 8 / 8 | 8 / 16 |

La charleston est **sous-détectée**. C'est un compromis délibéré : une frappe
manquante s'entend comme un motif plus clairsemé, une frappe inventée s'entend
comme une faute. La cause est physique — la caisse claire d'une boîte à rythmes
est du bruit, et son énergie au-dessus de 5 kHz atteint celle d'une charleston.
Les séparer demande des gabarits spectraux appris, c'est-à-dire une autre
technique et non un seuil mieux choisi.

## 4. Phase 10 — Rendre la recherche efficace — **FAITE**

La phase est close. Elle a surtout appris **où ne pas optimiser** : sur les
trois pistes inscrites au départ, deux ne rapportent rien, et le gain est venu
d'ailleurs.

### Ce que coûte réellement une évaluation

Mesuré avant toute optimisation, par évaluation :

| poste | coût | part |
|---|---|---|
| rendu dans le moteur | 7,6 ms | 36 % |
| aller-retour de tube | 2,2 ms | 11 % |
| **calcul de la distance** | **~11 ms** | **53 %** |

La feuille de route estimait la distance à 8 ms et en concluait qu'il fallait
réduire le nombre d'évaluations. La mesure confirme la conclusion mais pas le
raisonnement : comparer coûte plus cher que produire.

### Les trois pistes inscrites, et leur sort

| Étape | Idée | Résultat mesuré |
|---|---|---|
| 10.1 | Rendu par lot | **Fait, sans gain.** Bit-à-bit identique aux requêtes isolées, mais **0,92×** : une réponse JSON de 8,5 Mo coûte plus que les 36 allers-retours qu'elle évite. La capacité reste, elle servira si le moteur passe un jour sur une liaison lente. |
| 10.2 | Amorce guidée par les caractéristiques mesurées | **Rejetée.** Le point d'amorce est pourtant bon — 3,79 contre 10,39 en médiane pour un tirage, soit mieux que 90 % des points au hasard. Comparé sur huit populations initiales **identiques**, il gagne 3 fois sur 8 puis 4 fois sur 8 : pas mieux que pile ou face. La force de l'évolution différentielle est la diversité de sa population, et un bon point parmi trente-six s'y dilue. Code conservé, désactivé, documenté. |
| 10.3 | Métrique plus discriminante | **Faite** — `audio_distance_v2`. La vraie machine passe de perdante à gagnante ; détail plus bas. |

### Une quatrième piste, essayée puisque la distance dominait — et rejetée aussi

Réécriture des caractéristiques en numpy pur : une seule découpe en trames
partagée entre spectre et enveloppe, et une seule transformée en cosinus sur la
moyenne des trames au lieu d'une par trame (la transformée étant linéaire, le
résultat est identique). Équivalence vérifiée au millionième près sur quatre
natures de signaux.

À travail égal — le côté candidat seul, l'autre étant en cache : **librosa
7,99 ms, numpy 8,46 ms**. La bibliothèque n'était pas le problème ; le coût est
celui de la transformée elle-même, 87 fenêtres de 2048 points. Code retiré.

*Piège de mesure à retenir* : comparer `audio_distance` (qui calcule **les
deux** côtés) au cache (qui n'en calcule **qu'un**) donnait un trompeur
« 1,7 fois plus rapide ».

### Ce qui a marché : deux passes pour choisir la machine

Le vrai coût n'est pas l'évaluation, c'est leur **nombre × le nombre de
machines** : quinze machines à ~23 s font près de six minutes pour un seul
extrait. Or la première passe n'a pas à *régler* les machines, seulement à les
*classer* — et classer demande beaucoup moins de précision.

On dégrossit donc sur 0,4 s d'extrait avec un budget réduit, on garde les
meilleures, et on ne paie la passe complète que sur celles-là :

| finalistes retenus | temps | distance finale |
|---|---|---|
| sans présélection | 343 s | 0,289 |
| **7 sur 15 (défaut)** | **174 s** | **0,289** — même verdict, deux fois moins cher |
| 5 sur 15 | 132 s | 0,438 |
| 3 sur 15 | 84 s | 0,439 — **4,1×**, mais la machine gagnante est parfois écartée |

Le défaut retient la moitié des candidates. Le facteur quatre reste accessible
en passant `shortlist=3`, avec le compromis chiffré ci-dessus.

### Seize emplacements : le blocage n'était pas où il était écrit

Le sampler s'arrêtait à huit emplacements, et la raison inscrite dans son code
était honnête mais devenue fausse : « la façade afficherait cent douze commandes
illisibles ». C'était vrai en montrant les SEPT paramètres de chaque
emplacement ; ça ne l'est plus en ne montrant que les QUATRE qui se jouent
(niveau, accord, décroissance, panoramique). Les trois autres -- note de
déclenchement, point de départ, groupe de coupure -- sont des réglages de
configuration, posés une fois par l'analyse, et sont déclarés omis avec leur
raison.

Seize emplacements en deux rangées de huit, couleurs par famille de pièces,
grille de pas sur les huit premiers (ceux que porte la convention General MIDI).
La limite s'est donc déplacée : ce n'est plus le moteur ni la façade qui borne
le kit, c'est **le détecteur**, qui ne distingue que trois familles. Aller plus
loin demande des gabarits spectraux appris, pas des bandes supplémentaires --
la mesure du § 9.5 l'établit.

### Étape 10.3 — la métrique était le plafond

**Le symptôme.** Sur une cible produite par un SH-101, la recherche retenait un
Jupiter-8. Ce n'était pas un échec de la recherche : elle trouvait bien le
patch le plus proche **au sens de la métrique**. C'est la métrique qui se
trompait de gagnant.

**Le diagnostic.** Le paysage était pourtant sain : en ne faisant varier qu'un
réglage, la distance dessine un V net dont le minimum tombe sur la vérité. Mais
le SH-101 rejoué avec ses VRAIS réglages donne exactement 0, alors que sa
propre recherche plafonnait à 0,158 avec 2 916 évaluations. Le point parfait
existait dans son espace et elle ne l'atteignait pas.

La décomposition terme par terme a montré pourquoi. En ne faisant varier que la
résonance :

    terme cepstral   1,714
    TOUS les autres  0,003

Et en ne faisant varier que la coupure : 8,395 contre 0,017. **La distance
était le terme cepstral, à 0,2 % près.** Les cinq autres termes étaient
calculés, pondérés, additionnés — et sans effet. Les poids affichés
(0,20 · 0,15 · 0,15 · 0,10 · 0,25 · 0,15) décrivaient une intention, pas un
comportement, parce que les termes n'étaient jamais rendus comparables entre
eux : une erreur de centroïde divisée par 4 000 vaut 0,01 quand une erreur
cepstrale en décibels vaut 5.

Conséquence la plus grave : l'enveloppe d'amplitude, **seule information
temporelle de la mesure**, pesait 0,002 sur environ 5. La distance était
quasiment aveugle à la forme dans le temps.

**Le correctif.** Chaque écart est désormais rapporté à la grandeur
correspondante de la cible — erreur relative, sans dimension, d'ordre un — et
les poids retrouvent le sens qu'ils annoncent. Deux détails appris en chemin :

- la planéité spectrale vit **déjà** entre 0 et 1 ; la rapporter à la valeur de
  la cible (souvent 1e-4) produisait des erreurs de 12,7 qui écrasaient tout ;
- un terme de **contraste spectral** a été ajouté, dans l'espoir de mieux servir
  la résonance. Mesuré : il n'y change rien, la résonance garde exactement la
  même part relative (0,20 fois la coupure, avant comme après). Les bandes
  d'octave sont trop larges pour voir un pic sur un spectre harmonique. Le
  terme est conservé parce qu'il apporte par ailleurs, mais **il ne résout pas
  le problème pour lequel il a été introduit**, et il ne faut pas le croire.

**Le résultat.** Sur une cible SH-101, meilleure distance atteignable par
machine :

| métrique | SH-101 (la vraie) | Jupiter-8 | Juno-106 | Minimoog |
|---|---|---|---|---|
| v1 | 0,2812 | **0,2135** ← gagne à tort | 0,2842 | 0,8884 |
| v2 | **0,0179** | 0,1305 | 0,1298 | 0,1365 |

Validé sur quatre cibles de familles différentes, chacune cherchée parmi cinq
machines :

| cible | rang de la vraie, v1 | rang, v2 | d(vraie) v1 | d(vraie) v2 |
|---|---|---|---|---|
| SH-101 | 1/5 | 1/5 | 0,792 | **0,037** |
| Juno-106 | 1/5 | 1/5 | 0,280 | **0,017** |
| **DX7** | **4/5** — un Juno gagne | **1/5** | 1,799 | **0,058** |
| Orgue à roues | 1/5 | 1/5 | 0,202 | **0,021** |

La v2 identifie la bonne machine dans les quatre cas et converge cinq à trente
fois plus près. Coût : **+0,9 ms par évaluation** (4,70 contre 3,81), soit
environ +4 % du temps total.

**Ce qui reste imparfait, et qu'il faut dire.** Sur une nappe tenue sans trait
distinctif, la v2 retient encore un Minimoog (0,083) au lieu du Juno-106 visé
(0,130) — mais le Juno passe de 1,09 et troisième loin derrière, à quasi ex
æquo. Pour un son que plusieurs soustractifs produisent réellement à
l'identique, attendre de la métrique qu'elle désigne l'original revient à lui
demander de distinguer des sons qui ne le sont pas.

**Attention aux chiffres antérieurs.** Les distances v1 et v2 ne sont pas du
même ordre et **ne se comparent pas**. La métrique employée est inscrite dans
chaque `rapport.json`, et `reconstruire.py --metrique v1` permet de rejouer les
mesures anciennes.

## 5. Phase 11 — Boucler et rendre utilisable

| Étape | Contenu | État |
|---|---|---|
| ~~11.1~~ | ~~Import du projet reconstruit, en un geste~~ | **fait** — menu *Fichier ▸ Ouvrir un projet VSM...* |
| ~~11.2~~ | ~~Écoute A/B : original en piste de référence~~ | **fait** — `ReferenceTrack`, menu *Fichier* |
| ~~11.3~~ | ~~Correction assistée : les notes douteuses mises en évidence~~ | **fait** — confiance par note, du transcripteur au piano roll |
| ~~11.4~~ | ~~Export en projet CLAP autonome~~ | **fait** — et sans nouveau format, voir ci-dessous |

C'est ici que la fusion prend tout son sens : la reconstruction n'est pas un
rapport à lire, c'est un morceau à ouvrir, écouter et corriger.

### 11.1 — ouvrir un dossier, pas un fichier

On choisit un DOSSIER et non un fichier : un projet VSM est un ensemble
(`project.json`, le MIDI, les presets, les échantillons), et pointer vers l'un
de ses fichiers laisserait croire qu'il s'ouvre seul. L'application charge le
tout, assigne les machines, applique les presets **puis** les échantillons --
dans cet ordre, car les machines n'existent qu'après l'assignation.

Un projet incomplet **s'ouvre et dit ce qui lui manque** : machine
indisponible, paramètre non pris en charge, échantillon introuvable. Un preset
déclaré pour une piste absente du MIDI est ignoré en le disant, plutôt que lu
hors des bornes.

Vérifié sur un projet réellement produit par la chaîne d'analyse : une piste,
sampler, 24 notes, 9 paramètres appliqués, **3 échantillons chargés**, aucun
avertissement. Le chemin exact que suit l'interface est couvert par un test
(`opening_a_bundle_applies_presets_and_samples_like_the_application_does`), là
où il est vérifiable sans fenêtre.

### 11.2 — entendre l'écart, pas seulement le lire

Une distance publiée dit de COMBIEN on s'écarte ; elle ne dit pas EN QUOI. Pour
corriger une reconstruction, il faut entendre les deux versions au même endroit
du morceau et passer de l'une à l'autre sans quitter le transport. Le DAW n'avait
aucune source audio : c'est un séquenceur MIDI et des machines.

`ReferenceTrack` en ajoute une, avec trois règles dont chacune répond à un piège :

1. **Elle ne part jamais dans l'export.** Le rendu hors ligne partage le même
   `processBlock` que la lecture -- c'est voulu, il ne doit exister qu'un seul
   chemin de calcul. Mais exporter la reconstruction avec l'original mélangé
   dedans produirait un fichier qui n'est ni l'un ni l'autre, et personne ne
   s'en apercevrait avant de l'écouter. L'export coupe donc explicitement la
   référence, **puis restitue le mode de l'utilisateur** -- exporter ne doit pas
   éteindre en douce son écoute comparative.
2. **Elle passe après le bus master.** La tranche master appartient à la
   reconstruction ; la faire agir sur l'original reviendrait à comparer deux
   sons également traités au lieu de comparer une copie à son modèle.
3. **La lecture du fichier a lieu ailleurs.** Le tampon est publié par échange
   atomique, comme les échantillons du sampler ; le thread audio ne fait que
   lire un pointeur déjà valide.

Deux détails qui comptent à l'usage : l'enregistrement est **rééchantillonné**
(un fichier à 44,1 kHz lu tel quel à 48 le transposerait d'un demi-ton, et l'on
comparerait une erreur qu'on aurait introduite soi-même), et un **décalage**
réglable aligne un original qui commence par du silence.

En mode « original seul », la reconstruction est tue *après* rendu et non en
amont : les instruments continuent de tourner, donc revenir à la reconstruction
ne produit ni silence ni claquement le temps qu'ils se remettent en marche.

> **Savoir ce qu'on entend (28/08/2026).** Le mode d'écoute ne se lisait que
> dans le menu *Fichier*, qui se referme : pendant une comparaison, rien à
> l'écran ne disait si l'on jugeait la reconstruction ou l'original, et l'on
> peut très bien corriger une note en écoutant la mauvaise version. La barre
> de transport porte désormais un bouton qui DIT le mode (*Écoute :
> reconstruction / les deux / original*, coloré dès que l'original est
> audible, grisé tant qu'aucun original n'est chargé) et le fait tourner au
> clic. La touche **R** fait de même depuis n'importe quelle fenêtre -- les
> panneaux sont des fenêtres séparées, donc `MainComponent` s'inscrit comme
> écouteur clavier sur chacune et reçoit ce que le composant focalisé n'a pas
> consommé. C'est le geste qu'A5.3 répétera le plus.

Huit tests couvrent l'ensemble. Un piège de test à retenir : rendre deux fois
le même projet sur LE MÊME graphe ne donne pas le même son -- les instruments
gardent leur état d'un rendu à l'autre. La comparaison doit se faire entre deux
graphes neufs, et c'est vérifié pour soi-même.

#### Le fichier d'origine n'est pas toujours un WAV

Le chargement n'acceptait que le WAV, parce que `WavFileReader` est le seul
lecteur du moteur -- et il est le seul parce que `audio/` n'a **aucune
dépendance**, règle qui ne se négocie pas. Or la chaîne d'analyse part de
fichiers du commerce, c'est-à-dire de **MP3** : comparer un morceau à sa
reconstruction obligeait donc à le convertir à la main d'abord, pour une
fonction dont tout l'intérêt est l'immédiateté.

Le décodage a été ajouté **dans `app/`**, pas dans `audio/`. C'est le seul
endroit où il peut vivre sans rien coûter : JUCE est déjà là, son
`AudioFormatManager` lit WAV, AIFF, FLAC, Ogg Vorbis et MP3, et le moteur reçoit
exactement ce qu'il recevait avant -- un `SampleBuffer` en float, déjà décodé.
Conséquence assumée : `vsm-render` et les tests, qui ignorent JUCE, restent au
WAV. C'est cohérent, ce sont des chemins de calcul et non des points d'entrée
d'utilisateur.

Trois décisions valent d'être écrites :

- **Le MP3 se demande explicitement.** JUCE laisse `JUCE_USE_MP3AUDIOFORMAT` à
  0 et l'assortit d'un avertissement sur d'éventuels brevets tiers. C'est un
  avertissement de 2012 ; les derniers brevets MP3 ont expiré en 2017, et le
  décodeur est du code source livré avec JUCE. Le drapeau est activé dans
  `app/CMakeLists.txt`, avec cette raison écrite à côté.
- **La liste des formats n'est écrite nulle part.** Le filtre du sélecteur de
  fichiers et le message d'erreur interrogent le gestionnaire de formats.
  Recopier la liste à la main la ferait mentir le jour où une option de
  compilation changerait : on proposerait un format qu'on refuserait ensuite.
- **Un WAV passe d'abord par le lecteur du moteur.** Il est strict par choix et
  ses refus sont explicites ; ce chemin-là est déjà testé et ne change pas. S'il
  refuse, JUCE est essayé à son tour, et en cas d'échec des deux **le message
  porte les deux refus** -- celui qui explique vraiment est parfois le premier.

**Un décalage aurait tout faussé, et il a été mesuré.** Un MP3 porte un silence
d'amorce que les décodeurs ne retirent pas tous de la même façon ; la piste de
référence, elle, joue à partir de zéro. Un original décalé de quelques dizaines
de millisecondes ferait paraître fausse une reconstruction juste -- exactement
le genre de panne silencieuse que ce projet refuse. Vérifié sur *Children
(Dream Version)*, en comparant le tampon décodé par JUCE à celui du décodeur de
la chaîne d'analyse (`librosa`) : **décalage nul, au sample près**, premier
échantillon audible au même index (47679) chez les deux. Les 14 976
échantillons de plus que rend JUCE (0,34 s) sont en QUEUE, pas en tête.

Cette mesure a demandé un outil, qui reste :

```bash
cmake --build build --target vsm-audio-import-check
./build/app/vsm-audio-import-check_artefacts/RelWithDebInfo/vsm-audio-import-check morceau.mp3
#   OK  morceau.mp3
#       decodeur MP3 file, 44.1 kHz, stereo, 3:52, 10250496 echantillons, crete 0.970
```

Il appelle **le code du menu Fichier**, sans écran ni carte son, et `--wav` écrit
le tampon décodé pour qu'on puisse le comparer ailleurs. Même raison d'être que
`vsm-panel-preview` : ce qu'on ne peut pas regarder, on ne peut pas le juger --
et le chargement d'un fichier est du code d'interface, qu'aucun test unitaire du
moteur n'atteindra jamais.

### 11.3 — voir où la transcription a hésité

L'information existait déjà et s'arrêtait au bord du fichier : le transcripteur
rend une **confiance par note**, qui ne servait qu'à régler la vélocité. La
faire remonter jusqu'au piano roll change la nature du travail de correction --
au lieu de réécouter le morceau entier en cherchant ce qui cloche, on voit
d'emblée où le doute se trouve.

Le trajet : `rapport.json` porte désormais un `noteConfidence` par stem →
`ReconstructionReport` le lit → `applyNoteConfidences` le reporte sur les notes
du projet → le piano roll marque celles qui passent sous le seuil.

**L'appariement se fait par hauteur et instant, jamais par position dans la
liste.** Un index serait plus simple et faux : il suffirait qu'une note soit
ajoutée, supprimée ou déplacée dans l'éditeur pour que toutes les confiances
suivantes désignent la mauvaise note -- et rien ne le signalerait. Sur une note
répétée rapidement, c'est le candidat le **plus proche** qui gagne, et non le
premier dans la tolérance.

**Une note dont le rapport ne dit rien reste franche.** Ce qu'on a saisi
soi-même n'est pas douteux ; rendre suspect par défaut ce qui n'est pas décrit
noierait le signal.

**La confiance ne change ni le rendu ni l'export.** Une note peu sûre se joue
comme les autres. Décider à la place de l'utilisateur -- la taire, la
supprimer -- serait pire que de ne rien signaler.

Huit tests couvrent la lecture et l'appariement. Le marquage lui-même, lui, a
demandé un **aperçu hors écran du piano roll** (`vsm-pianoroll-preview`), sur
le modèle de celui des façades -- et il a payé immédiatement : le marqueur
était d'abord dessiné AVANT le contour ordinaire, qui le recouvrait
intégralement ; puis, une fois déplacé, il était trop transparent pour se voir.
Deux défauts qu'aucun test ne pouvait attraper, dans du code qui compilait et
paraissait juste.

> **Y ALLER, pas seulement les voir (28/08/2026).** Marquer les notes
> douteuses suffit sur huit notes de démonstration ; sur un morceau transcrit
> -- 2 219 notes pour *Clair de Lune*, 4 230 sur la piste « other » de
> *Children* -- il faut les parcourir sans les chercher à l'œil, et c'est ce
> que la validation A5.3 demandera. La règle de parcours vit dans `core/`
> (`nextDoubtfulNote`, `countDoubtfulNotes`, `selectDoubtfulNotes`, trois
> tests) : ordre du morceau (début, hauteur, identifiant, pour que l'ordre
> soit total), départ de la sélection ou sinon de la tête de lecture, et le
> tour du morceau au bout plutôt que rien. Le seuil de doute a suivi : il est
> dans le cœur, en un seul endroit, et le piano roll, la barre d'état et le
> rapport d'ouverture lisent le même. Dans l'éditeur : **D** / **Maj+D**,
> *Sélection ▸ Note douteuse suivante / précédente / Toutes*, et le compte des
> douteuses restantes en barre d'état. La vue défile jusqu'à la note SANS
> changer le zoom -- zoomer sur une seule note ferait perdre les voisines,
> qui sont précisément ce qu'on compare pour la juger.
>
> L'aperçu hors écran a payé une troisième fois : avec `douteuse` en
> argument, il sélectionne la première note douteuse avant le rendu, et elle
> était **indiscernable** de la même note non sélectionnée -- contour de
> sélection et marqueur de doute étaient tous deux ambre, et « D » produisait
> donc une sélection invisible. La sélection porte désormais un halo clair à
> l'EXTÉRIEUR du contour, d'une autre teinte, qui ne recouvre ni le liseré ni
> la barre de doute.

### 11.4 — il n'y avait pas de second format à écrire

La feuille de route prévoyait un dossier `states/track_NN.clapstate` à côté des
`instruments/track_NN.synth.json`. En allant l'implémenter, on a constaté que
**l'adaptateur CLAP écrit déjà son état natif en preset sémantique** -- un choix
délibéré, documenté dans son code : un projet d'hôte enregistré aujourd'hui
reste lisible même si les identifiants internes d'une machine changent.

Les deux fichiers auraient donc contenu les mêmes octets sous deux noms. Deux
copies d'une même vérité finissent toujours par diverger ; le dossier `states/`
est abandonné, et c'est un gain, pas un renoncement.

Ce qui manquait vraiment, c'était de pouvoir **utiliser** cet état : l'hôte
récupérait l'extension CLAP correspondante sans jamais s'en servir. Elle est
maintenant exposée (`saveClapState` / `loadClapState`), et trois tests
établissent la promesse :

- un preset écrit par la chaîne d'analyse, chargé **par la voie CLAP**, produit
  un son **identique au millionième** à celui de la machine native réglée par
  la voie sémantique ;
- l'état relu est bien un preset sémantique, avec des identités comme
  `filter.1.cutoff` et non des numéros internes ;
- un état illisible est **refusé**, pas appliqué à moitié.

---

## 5 bis. Six pannes muettes de la chaîne d'analyse

> **Une cinquième s'est ajoutée en août 2026, et elle a la même forme que les
> quatre autres** — voir la fin de cette section. Le fait qu'elle soit apparue
> APRÈS cette relecture complète est en soi le résultat : ce n'est pas un type
> de faute qu'on élimine une fois, c'est un type de faute contre lequel il faut
> une DISCIPLINE — ici, « une candidate écartée est nommée avec sa raison ».

Les trois verdicts (note, piste, mélange) étaient en place et mesurés ; une
relecture complète du diff a montré que **trois d'entre eux ne jugeaient pas ce
qu'ils croyaient juger**. Aucune de ces pannes ne produit d'erreur : chacune
rend un chiffre plausible, et c'est ce qui les rendait coûteuses. Elles sont
corrigées, et écrites ici parce qu'elles disent toutes la même chose — *une
étape qui échoue à moitié est pire qu'une étape qui échoue*.

**1. Le verdict du mélange se prononçait sur un mélange sans la voix.**
`vsm_mix_verdict._render_project` écrivait le mini-projet dans un dossier de
travail sans y recopier les échantillons, que les pistes de sampler désignent
par chemin RELATIF au projet. Le report vocal — la piste la plus présente du
mélange — était donc absent des deux variantes comparées. `vsm-render` ne s'en
plaint que par un avertissement sur la sortie d'erreur, et `capture_output`
l'avalait : rendu réussi, code de retour 0, piste muette. `match_track_levels`
faisait déjà cette recopie avant son rendu solo ; la règle manquait ici seule.
Au passage, les variantes partagent désormais UN dossier au lieu d'un par
essai : recopier le stem vocal entier à chaque rendu coûtait des dizaines de
mégaoctets pour un fichier identique — la leçon que `vsm_track_arbitration`
avait déjà payée d'un « No space left on device ».

**REPRODUIT ET CHIFFRÉ, et le chiffre modère la conclusion.** Un projet de deux
pistes (la voix au sampler, la batterie modélisée) monté sur les stems de
*Children*, rendu deux fois et comparé à la somme des stems :

| ce que le verdict rendait | distance au mélange | niveau efficace |
|---|---|---|
| sans les échantillons (avant) | 0,4815 | 0,07941 |
| avec les échantillons (après) | **0,4641** | 0,07945 |

Le mécanisme est confirmé exactement tel qu'il était soupçonné : `vsm-render`
rend **code de retour 0** et une seule ligne sur la sortie d'erreur — « Piste 0 :
0 échantillon(s) chargé(s), 1 en échec » — que `capture_output` avalait. La
cible du verdict était donc fausse de 0,0174, soit 3,6 %.

**Ce que cela n'établit PAS, et il faut le dire avant que quelqu'un le déduise.**
Le verdict compare DEUX variantes rendues de la même façon : un biais commun aux
deux se soustrait en grande partie, et rien ici ne montre qu'une décision ait
été renversée. Ce qui est corrigé, c'est que la comparaison se fait désormais
sur le mélange qu'on écoutera au lieu d'un mélange amputé — la conséquence sur
le VERDICT reste non mesurée, et le sera le jour où la chaîne tournera de bout
en bout sur la version 3:52 qui a servi aux mesures du § 34 et qui n'est pas sur
cette machine.

Deux remarques à ne pas perdre : le niveau ne bouge que de 0,1 %, parce que le
stem vocal de *Children* porte peu d'énergie — sur un morceau chanté, l'écart
serait tout autre ; et les distances sont hautes (0,46) parce que deux pistes
sur quatre sont comparées au mélange entier, ce qui est sans effet sur la
COMPARAISON mais interdit de rapprocher ces chiffres de ceux du § 34.

**2. Des frappes de batterie pouvaient disparaître du projet.**
`_name_templates` puise les noms des gabarits excédentaires dans un vivier
(`percussion`, `tom`, `cymbal`, `tom2`, `tom3`) ; `MODELLED_DRUM_NOTES` n'en
connaissait que trois, et `modelled_drum_track` sautait les autres **en
silence**. Le chemin sampler, lui, les jouait toutes — la batterie modélisée
par défaut pouvait donc perdre de la musique que la détection avait trouvée.

**CE QU'IL FAUT DIRE SUR L'AMPLEUR, parce que la première version de ce
paragraphe l'a exagérée.** « Un quart des frappes » venait d'un kit de huit
gabarits construit À LA MAIN pour le test (12 notes sur 16), pas d'un morceau.
Vérification faite sur le stem de batterie de *Children* : **cinq gabarits, 1
092 frappes, aucune perdue** — le vivier n'est entamé que de deux noms, et
`tom2` n'est atteint qu'au quatrième débordement. Les réserves totalisent sept
noms pour `MAX_TEMPLATES = 8` : il faut donc une classification très
déséquilibrée (six gabarits tombant dans la même famille) pour perdre quoi que
ce soit. Le défaut est réel et le correctif reste juste ; sa portée est
CONDITIONNELLE, et un chiffre mesuré sur un kit fabriqué n'est pas un chiffre
mesuré sur un morceau. C'est exactement la confusion que ce document reproche
ailleurs aux distances obtenues à des budgets différents.

Le gain vérifié sur *Children* est donc ailleurs, et plus modeste : les dix
voix de `vsm.drums` deviennent atteignables (le crash et le tom grave ne
l'étaient d'aucune façon), et la famille `percussion` passe de la note 48 au
49 — un timbre qui change, pas une frappe qui revient.
Le vivier ne porte aucun sens timbral (un quatrième gabarit de charleston
s'appelle `percussion`), et le seul choix qui vaille est de ne pas faire tomber
deux gabarits distincts sur la même voix : les cinq noms reçoivent les cinq
voix que les réserves ne prennent pas, une pour une. Et une famille inconnue
est désormais **jouée et dite**, jamais sautée — c'est cette partie-là du
correctif qui garantit qu'aucun vivier futur ne pourra rejouer la panne.

**3. `--sans-arbitrage` désactivait deux étapes.** Le réglage sur la piste était
écrit sous le `else` de l'arbitrage : il disparaissait avec lui, et un stem dont
l'arbitrage ne rendait aucun verdict n'était pas réglé non plus. Le README
promet l'inverse — « chaque étape se désactive : c'est ainsi qu'on attribue un
écart à une étape et non à un ensemble » — et c'est la promesse qui a raison,
parce que sans elle **aucune mesure ne peut dire laquelle des deux étapes a
produit un gain**. Les deux options sont maintenant indépendantes, ce qui rend
mesurable tout le travail d'optimisation à venir sur cette partie de la chaîne.

**4. `rapport.json` publiait des patchs absents du projet.** Le verdict du
mélange REMPLACE le dictionnaire de paramètres de la piste au lieu de le
modifier ; le `StemReconstruction`, qui partageait l'objet au départ, gardait
donc le patch d'avant. Quand le mélange revenait au patch de l'arbitrage, le
rapport publiait le patch affiné et sa `trackDistance` : des chiffres pour un
réglage que le projet ne contient pas, c'est-à-dire la pire sorte — ceux qu'on
croit vérifiés. La resynchronisation a lieu avant l'écriture, et **avant** la
résolution des défauts : le rapport dit ce que la chaîne a DÉCIDÉ, pas les
vingt valeurs d'usine que l'écriture du preset y ajoute ensuite pour le figer.

**Ce qui reste ouvert, et n'est pas corrigé ici.** Le garde-fou de niveau de
`vsm_track_arbitration` et `vsm_track_refine` compare le niveau efficace du stem
ENTIER à celui d'un rendu qui s'arrête à la dernière note plus deux secondes,
alors que `match_track_levels` rend sur toute la durée du stem. Sur une piste
qui se tait avant la fin, le garde sous-estime le facteur qui sera réellement
demandé et laisse donc passer des candidates qui buteront sur `VOLUME_MAX` —
exactement le cas qu'il a été écrit pour attraper. Le corriger demande de rendre
à durée imposée, donc de refaire les mesures du § 34 : à faire d'un bloc, avec
les chiffres, plutôt qu'à moitié.

### 5. Une candidate écartée disparaissait du tableau sans un mot

Trouvée en cherchant pourquoi `vsm.multisample` n'apparaissait **nulle part**
dans la reconstruction de *Clair de Lune* : ni dans les dix finalistes, ni dans
les vingt-neuf lignes du tableau d'arbitrage. Elle n'avait pas perdu — elle
n'avait pas été mesurée.

**La cause.** L'arbitrage sur la piste, le réglage sur la piste et le verdict du
mélange ne passent pas par le service de rendu mais par un rendu de PROJET hors
ligne. Le projet qu'ils écrivaient ne portait pas le champ `profile` ; la
machine y était donc sans zones, donc muette ; le garde-fou de niveau voyait un
RMS nul et écartait la candidate. C'est exactement la panne n° 1 sous une autre
forme — une donnée externe désignée par référence, oubliée à la recopie — et
elle est réapparue parce que le correctif d'alors visait les ÉCHANTILLONS du
sampler, pas la catégorie « donnée externe d'une piste ».

**Le correctif qui compte n'est pas le premier.** Le profil suit désormais la
machine dans tous les rendus hors ligne, verdict du mélange compris — c'est le
seul endroit de la chaîne où une piste CHANGE de machine, donc le seul où un
profil peut se retrouver attaché à la mauvaise. Mais la vraie leçon est
ailleurs : **une candidate écartée est maintenant NOMMÉE, avec sa raison**
(« rendu vide », « silence », « trop faible, il faudrait ×N »). Sans cette
ligne, la prochaine donnée externe oubliée produira le même silence, et il
faudra la même enquête.

> *Un résultat qui manque à un tableau ressemble en tout point à un résultat
> qu'on n'a pas voulu.* C'est la forme que prennent toutes les pannes de cette
> section, et la seule défense est de rendre l'absence bruyante.

### 6. La note de référence tombait dans un silence, et vingt machines ont été jugées sur rien

Trouvée en cherchant pourquoi le gagnant d'une note (§ 5 septies) était
quarante-deux fois trop faible sur la piste. La borne de niveau ajoutée à la
recherche ne changeait rien — et pour cause : **la cible était muette**.

`_representative_note` prend la note la plus LONGUE du stem, parce qu'elle
montre le mieux l'entretien et l'extinction. Sur la basse de *B4 Wuz Then*, la
plus longue était une « note » de 3,68 s à MIDI 34 dans les premières secondes
du morceau — là où le stem séparé ne contient qu'un souffle : RMS 0,00008,
**onze cents fois sous le niveau du stem**. Un artefact de Basic Pitch, qui a
entendu un grave tenu dans un résidu de séparation. Quarante des notes du stem
étaient dans ce cas.

Toute la recherche par note — vingt machines, vingt fois vingt itérations — s'est
donc faite contre du silence. Un patch quasi muet l'a gagnée (`vsm.wind`,
0,2455, **le meilleur des vingt**), et il était inutilisable sur la piste. Le
chiffre était plausible, la chaîne a continué, et l'arbitrage a rattrapé le
verdict final sans que personne sache que toute la première étape avait été
jetée.

**Le correctif** : la note de référence doit SONNER — au moins un dixième du
niveau efficace du stem pendant sa durée. Le seuil est large, il écarte le
silence et pas une note douce. Quand la plus longue est écartée, la chaîne le
DIT. Sur cette basse, la référence passe de la note fantôme à une vraie note de
0,99 s à 0,1265 de RMS.

**La leçon dépasse ce stem.** Jusqu'ici, « la note la plus longue » était tenue
pour une propriété du MORCEAU. C'est une propriété de la TRANSCRIPTION, et une
transcription invente. Toute étape qui choisit une note d'après ses seuls
attributs MIDI — durée, hauteur, vélocité — sans vérifier dans l'audio qu'elle
existe, refera cette erreur.

---

## 5 ter. Children v10 : la chaîne entière, sampler compris

Première exécution de bout en bout avec le SAMPLER AUTORISÉ — donc avec la voix
reportée telle quelle, ce qu'aucune des passes v7 à v9 n'avait fait (toutes
tournaient `--sans-sampler`). C'est aussi la première fois que le verdict du
mélange rend un projet contenant réellement une piste de sampler, c'est-à-dire
la première fois qu'il juge sur le mélange complet.

**Protocole.** Face A du vinyle (`A. Robert Miles - Children.mp3`, 455,7 s),
séparation faite par la chaîne elle-même — et non des stems repris, pour que la
source et les stems soient du même enregistrement. Métrique v2, 20 itérations,
`--budget-piste 40`, `--axes-piste 8`. Durée totale : **2 925 s** (49 min).

| étape | bass | other |
|---|---|---|
| recherche (UNE note) | `vsm.string` 0,196 | `vsm.generic` 0,241 |
| la même sur la PISTE entière | 0,698 | — |
| arbitrage sur la piste | **`vsm.piano`, patch d'USINE, 0,275** | **`vsm.string`, patch cherché, 0,257** |
| réglage sur la piste | 0,199 | 0,217 |
| verdict du mélange | **revient à l'arbitrage** | **revient à l'arbitrage** |

**L'arbitrage a changé la machine sur les deux stems mélodiques**, et sur la
basse il l'a fait de la façon la plus nette qui soit : la gagnante de la
recherche, jugée sur la piste entière, fait 0,698 quand une machine que la
recherche n'avait pas retenue fait 0,275 AVEC SON PATCH D'USINE. C'est le même
résultat que celui qui a motivé l'étape, retrouvé sur une autre version du
morceau — 0,275 ici contre 0,282 sur l'édition 3:52 du § 34.

**Le verdict du mélange a défait deux réglages sur trois**, et le chiffre dit
pourquoi il existe :

| | distance du MORCEAU |
|---|---|
| après réglage des pistes | 0,3390 |
| basse ramenée à l'arbitrage | 0,3074 |
| `other` ramené à l'arbitrage | **0,2815** |
| batterie : réglage CONSERVÉ | 0,2815 (contre 0,4926 sans) |

Les deux pistes mélodiques s'étaient rapprochées de LEUR stem en éloignant le
morceau ; la batterie, elle, gagne 0,21 et son réglage est gardé. Une piste
jugée seule et une piste dans un mélange ne sont pas le même objectif, et c'est
mesuré pour la troisième fois.

**DISTANCE GLOBALE : 0,2815** (v2, 20 itérations ; l'original au silence vaut
1,0). Le projet compte 4 pistes et 7 655 notes : `vsm.piano` (basse),
`vsm.string` (other), `vsm.drums` (batterie), `vsm.sampler` (voix).

**Le plafond de volume à 10 a servi ici**, et pas qu'un peu : la basse demande
un facteur **8,74**. À l'ancien plafond de 2,5 elle serait restée trois fois et
demie trop faible dans le mélange.

**Ce que cette mesure ne permet PAS de dire.** Elle ne se compare ni aux passes
v7–v9 (qui n'avaient pas le sampler, ni les mêmes stems), ni aux chiffres du
§ 34 (édition 3:52, absente de cette machine). C'est un point de départ propre
pour la configuration « sampler autorisé », et rien de plus.

**Un défaut trouvé en la vérifiant.** Le rapport publiait encore un
`trackDistance` de 0,1986 pour la basse et 0,2174 pour `other` — les scores du
réglage que le verdict venait justement d'écarter. La resynchronisation du
§ 5 bis corrigeait `parameters` sans corriger ce chiffre : elle ne faisait que
déplacer le mensonge d'un champ. La distance de piste suit désormais le patch
retenu.

## 5 quater. Le garde-fou de niveau corrigé, et ce que la correction a révélé

Le § 5 bis laissait ouvert un défaut : le garde-fou « ce patch peut-il encore
atteindre le niveau de son stem ? » comparait le niveau efficace du stem ENTIER
à celui d'un rendu qui s'arrêtait à la dernière note plus deux secondes de
queue, alors que `match_track_levels` rend, lui, toute la durée du stem. Deux
mesures sur des terrains différents. Corrigé : l'arbitrage et le réglage rendent
désormais à DURÉE IMPOSÉE, comme le calage des volumes.

**Mesuré en A/B strict** — mêmes stems (repris de v10), même source, mêmes
réglages, la correction pour seule différence :

| | v10 (avant) | v11 (après) |
|---|---|---|
| arbitrage `other` | `vsm.string` 0,257 | `vsm.ms20` 0,260 (string à 0,261) |
| réglage `other` | 0,257 → **0,217** | 0,260 → 0,252 |
| arbitrage `bass` | `vsm.piano` 0,275 | `vsm.piano` 0,273 |
| réglage `bass` | 0,275 → 0,199 | 0,273 → 0,198 |
| **distance globale** | **0,2815** | 0,2976 |

**La correction coûte 0,0161, et il faut dire exactement d'où ça vient.** Elle
n'a rien mal jugé : sur la basse elle reproduit v10 au millième près. Tout
l'écart tient à `other`, où elle a fait basculer une ÉGALITÉ — `ms20` à 0,260
contre `string` à 0,261, un millième — et où `ms20`, une fois retenu, se règle
beaucoup moins bien (0,252 contre 0,217).

**Ce que ça apprend, et qui vaut plus que le garde-fou.**

1. **Le verdict de l'arbitrage sur `other` n'est pas robuste.** Un millième
   d'écart entre les deux premières, et un changement de protocole de cette
   taille suffit à les intervertir. Aucune décision de machine ne devrait être
   lue comme acquise à cette marge-là.
2. **Le verdict du mélange peut défaire un RÉGLAGE, jamais une MACHINE.** Sa
   seule alternative est le patch d'avant réglage de la MÊME machine. Quand
   l'arbitrage se trompe de machine à un millième près, plus rien en aval ne
   peut le rattraper : c'est exactement ce qui s'est produit ici, et c'est la
   faiblesse structurelle que cette mesure met au jour.

**Décision : la correction est GARDÉE malgré le chiffre.** L'ancien garde-fou
comparait deux grandeurs qui ne se comparent pas ; le remettre serait remettre
un défaut connu au motif qu'un tirage à pile ou face est tombé du bon côté une
fois. La durée imposée rend de plus la distance d'arbitrage plus fidèle : elle
couvre tout le stem au lieu de s'arrêter à la dernière note. Le prix est écrit
ici, il est d'une seule mesure sur un seul morceau, et il est ATTRIBUÉ.

**À faire ensuite, et c'est la vraie leçon** : donner au verdict du mélange la
machine SECONDE de l'arbitrage comme alternative, au moins quand l'écart entre
les deux premières est sous un seuil (ici 0,001). C'est le seul moyen qu'une
égalité mal tranchée cesse d'être définitive, et cela se mesure exactement comme
le reste — sur ce morceau, il devrait ramener `other` à `vsm.string` et la
distance globale vers 0,2815.

## 5 quinquies. Une égalité n'est plus définitive : le verdict peut changer de machine

> **LE SEUIL DE 2 % DÉCRIT ICI A ÉTÉ RETIRÉ AU § 5 DECIES.** Ce que cette
> section établit reste vrai — le verdict du mélange sait changer de MACHINE, et
> il le fallait. Ce qui est tombé, c'est la condition d'entrée : mesuré sur la
> basse de *Sky and Sand*, le classement contre le stem et le classement dans le
> mélange sont à peu près inverses, et la machine que le mélange retient était à
> 17,5 % au stem — hors de portée d'un seuil serré. Les trois meilleures
> machines suivantes repartent maintenant toutes au mélange, sans condition.

Le § 5 quater se terminait sur une prédiction falsifiable — « donner au verdict
du mélange la machine SECONDE de l'arbitrage comme alternative devrait ramener
`other` à `vsm.string` et la distance vers 0,2815 ». C'est fait, et c'est
mesuré.

**Ce qui change.** L'arbitrage signale désormais une ÉGALITÉ quand la deuxième
machine est à moins de 2 % de la première (`CLOSE_MARGIN`), et cette seconde est
remise en jeu au verdict du mélange. Le verdict, lui, sait maintenant changer de
MACHINE et plus seulement de patch : c'était sa limite, et elle était invisible
tant que l'arbitrage tombait juste.

Le seuil est RELATIF et non absolu, parce que les distances ne vivent pas dans
la même plage d'un stem à l'autre. Ce n'est pas un réglage fin, c'est une
déclaration d'ignorance : « sous cet écart, je ne sais pas laquelle est la
meilleure, que le mélange tranche ».

**Trois passes, mêmes stems, même source, une seule différence à chaque fois :**

| | garde-fou de niveau | égalité rattrapable | distance globale |
|---|---|---|---|
| v10 | faux | non | 0,2815 |
| v11 | corrigé | non | 0,2976 |
| **v12** | **corrigé** | **oui** | **0,2817** |

Le journal de v12 dit la mécanique en trois lignes :

```
other : arbitrage piste CHANGE vsm.ms20 (patch d'usine) D=0,260 — ms20=0,260*, string=0,261
other : arbitrage SERRÉ — vsm.string à 0,1 % (0,261), remise en jeu au verdict du mélange
other : verdict du mélange -> machine seconde (vsm.string) (0,2817)
        — écartées : réglage 0,3045, arbitrage 0,2976
```

**Ce que ça établit.** La correction du garde-fou ne coûte plus rien : les
0,0161 qu'elle avait coûtés en v11 sont récupérés, et cette fois SANS s'appuyer
sur un défaut. La chaîne n'est plus l'otage d'un millième : quand l'arbitrage
hésite, il le DIT et le mélange tranche. Sur la basse, où l'écart à la seconde
est de 45 %, rien ne se déclenche — le mécanisme ne coûte un rendu de plus que
là où il y a vraiment un doute.

**Et un troisième champ qui ne suivait pas la décision.** `trackDistance`
annonçait 0,2523 pour `other` — le score du réglage de `vsm.ms20` — alors que
le projet porte `vsm.string`. C'est la troisième fois que ce défaut se
présente (§ 5 bis pour `parameters`, § 5 quater pour la distance après revenue
à l'arbitrage), et toujours pour la même raison : un chiffre rangé ailleurs que
la décision qu'il décrit. Il voyage désormais AVEC la proposition
(`MixAlternative.track_distance`), et le verdict renvoie celui qu'il a retenu.
Le `rapport.json` de v12 sur le disque porte encore l'ancienne valeur : il a été
écrit avant ce correctif.

## 5 sexies. Le parc compensait au lieu de reproduire : `vsm.multisample`

*Clair de Lune* est le premier morceau de piano SEUL passé dans la chaîne, et
c'est ce qui a rendu le trou visible. Sur les morceaux précédents — *House Of
God*, *Children* — le piano n'était jamais isolé, et une approximation de timbre
se noyait dans le mélange. Seul, il ne se noie plus.

**Le chiffre qui a décidé.** Huit machines entre 0,2590 (`vsm.supersaw`) et
0,3217 (`vsm.tonewheel`). Un classement dont l'écart total vaut six pour cent
n'est pas un classement : c'est la mesure disant « aucune de ces machines n'est
la bonne réponse ». Et la distance globale de 1,639 se lisait mal tant qu'on ne
regardait pas d'où elle venait : l'automation de coupure la faisait tomber de
8,34 à 1,64 avec six cent six points. Autrement dit, la reconstruction
réussissait en **corrigeant en continu** un timbre qu'elle n'avait pas su
choisir.

**La réponse est une machine, pas une par instrument.** Un lecteur
multi-échantillons couvre le piano maintenant et, par l'import SoundFont,
l'orchestre General MIDI entier ensuite — sans une ligne de DSP nouvelle. C'est
le choix inverse de `vsm.string`, `vsm.piano` et `vsm.wind`, et les deux
coexistent sans se contredire : la modélisation physique donne l'EXPRESSIVITÉ,
le report d'échantillon donne la COUVERTURE. Le § 6 ci-dessous met en garde
contre les machines qui « élargissent le catalogue, pas la couverture » ;
celle-ci fait l'inverse, et c'est mesurable.

**La leçon de méthode, et elle est nouvelle.** Les leçons précédentes portaient
toutes sur les conditions d'une mesure — la métrique (§ 10.3), le budget, le
`gate`. Celle-ci porte sur ce qu'une mesure SERRÉE veut dire. Huit machines à
six pour cent les unes des autres se lisent spontanément comme « le supersaw
gagne » ; elles disent en réalité « la question n'a pas de réponse dans ce
parc ». **Un classement dont l'étendue est du même ordre que son bruit n'est pas
un classement, c'est un constat d'absence** — et le bon geste n'est pas de
choisir le premier, c'est d'aller chercher ce qui manque.

Un second garde-fou en est sorti, et il vaut pour toute machine future qui
dépendrait d'une donnée installée : **sans profil, la machine ne rend pas du
silence, elle est refusée.** Une machine muette ne perd pas la comparaison, elle
la fausse — sur une cible douce, elle gagne. La chaîne l'écarte de ses
candidates en le disant, et le pont refuse la requête. Un refus se lit ; un zéro
se mesure, et se publie.

**Le banc a été rejoué, et il dit NON.** Le même morceau, le même code, les
mêmes options, seul le dossier de profils changeant :

| | distance globale | machine retenue |
|---|---|---|
| sans `vsm.multisample` | **0,2159** | `vsm.piano` |
| avec `vsm.multisample` | **0,2159** | `vsm.piano` |

Écart nul. La machine finit 7e sur 30 à l'arbitrage (0,3571) et ne survit pas à
la présélection de la recherche par note. **Sur ce morceau, elle n'apporte
rien**, et le § 1 du cahier des charges garde donc sa question ouverte.

**Deux leçons, et la seconde vaut pour tout le projet.**

*La première* : le premier rejeu avait donné 0,2159 contre 1,639 — sept fois
mieux — et ce chiffre ne valait rien. Le rapport de référence avait été produit
SANS l'arbitrage de piste, et la machine n'avait même pas participé à la
mesure. C'est la quatrième forme de la règle du § 10.3 : *une distance n'est un
chiffre que si l'on sait à quelles conditions elle a été obtenue* — et elle vaut
**aussi contre soi-même**, quand le chiffre va dans le sens qu'on espérait. Le
seul protocole recevable pour juger un ajout est un COUPLE à conditions
identiques, avec et sans.

*La seconde* : trois causes mécaniques ont été éprouvées et écartées — la
troncature des échantillons (discontinuité à −49 dB), le double comptage de la
dynamique (0,3568 → 0,3571 sur toute la course du réglage), l'absence de
production (la réverbération dégrade les deux rendus de +14 % et +12 %) — et la
métrique s'est montrée capable de désigner la bonne machine quand la cible est
son propre rendu (0,0000 contre 0,1648 au second). Le résultat n'est donc pas un
artefact. Ce qui reste est plus intéressant : **la transcription plafonne tout.**
Les 2219 notes ont des vélocités comprises entre 61 et 116 — la dynamique est
écrasée, la couche la plus douce du profil n'est jamais atteinte — et les notes
elles-mêmes sont approchées. Un timbre FIDÈLE rend ces erreurs audibles, là où un
timbre générique les fond. *Une machine peut perdre pour avoir été trop
reconnaissable*, et aucune amélioration du parc ne le corrigera : c'est l'étape
d'avant qu'il faudrait améliorer.

**À quelle condition rouvrir le dossier** : une cible dont on CONNAÎT le MIDI
exact — une prise Disklavier, ou un rendu de banque tierce à partir d'un MIDI
publié. La transcription sortirait alors de l'équation, et l'on saurait si la
machine perd sur le timbre ou sur les notes. C'est peu coûteux à remplir, et
c'est ce paragraphe qu'il faudra venir corriger ce jour-là.

---

## 5 septies. Le gagnant d'une note ne tient pas la piste : mesuré deux fois sur un morceau

*B4 Wuz Then* (synthés, 354 s, quatre stems) est le premier morceau passé dans la
chaîne depuis que l'arbitrage NOMME les candidates qu'il écarte (§ 5 bis, panne
n° 5). Ce que la ligne nouvelle a montré, sur les DEUX stems mélodiques :

| stem | gagnant sur une note | sur la piste entière | retenu à l'arbitrage |
|---|---|---|---|
| `bass` | `vsm.wind` 0,247 | **« trop faible, il faudrait ×42 »** | `vsm.obx` 0,206 |
| `other` | `vsm.supersaw` 0,329 | **« trop faible, il faudrait ×42,5 »** ; `ms20` ×413 | `vsm.piano` 0,254 |

Le patch qui gagne la recherche sur une note **ne peut pas atteindre le niveau de
son stem** sur la piste entière — quarante fois trop faible, deux fois sur deux.
La cause est structurelle, et connue : la distance est insensible au niveau (une
machine ne doit pas gagner parce qu'elle sort plus fort), donc la recherche sur
une note est libre de retenir un patch quasi muet dont le TIMBRE colle. Sur la
piste, le calage des volumes bute sur son plafond (×10), et la candidate tombe.

Jusqu'ici cette chute était SILENCIEUSE — la candidate disparaissait du tableau
sans un mot — si bien qu'on ne savait pas qu'elle se produisait à chaque fois. Ce
n'est pas un cas marginal : c'est le comportement normal de la recherche par
note, et le § 34 d'ARCHITECTURE.md l'avait vu sous un autre angle (« le critère
une note ne classe pas dans le même ordre que la piste »). Le chiffre ×42 donne
la forme exacte du désaccord : ce n'est pas un ordre différent, c'est un gagnant
INUTILISABLE.

**Borner le niveau DÈS la recherche par note — fait, et mesuré.** La même règle
que la piste (un patch qu'il faudrait amplifier au-delà de `VOLUME_MAX` n'est
pas un candidat), appliquée là où le défaut naît, avec une pénalité croissante
plutôt qu'une falaise pour que l'évolution différentielle garde une pente. A/B
sur la basse de *B4 Wuz Then*, cible saine (voir la panne n° 6 du § 5 bis),
mêmes huit machines, même graine :

| | gagnant sur une note | gain nécessaire | |
|---|---|---|---|
| sans borne | `vsm.ms20`, 0,2277 | **×2 110** | inutilisable |
| **avec borne** | **`vsm.obx`, 0,2691** | **×2,1** | tient |

La distance sur une note MONTE — on interdit des patchs, c'est attendu — mais le
gagnant avec borne est exactement celui que l'arbitrage sur la piste avait fini
par retenir après avoir jeté le gagnant muet. La recherche donne d'entrée la
réponse que la piste aurait imposée, au lieu de dépenser son budget sur un patch
qu'elle rejettera. La borne est active par défaut ; `level_bound=False` rend
l'ancien comportement pour les mesures.

Un point à ne pas confondre : cette borne et la panne n° 6 sont DEUX défauts
distincts. La note fantôme rendait la cible muette ; la borne protège d'un
candidat muet face à une cible qui sonne. Le ×2 110 ci-dessus a été mesuré
APRÈS la correction de la note fantôme — la borne était nécessaire même sur une
cible saine.

**Et une surprise à écouter avant de la croire** : sur le stem `other` — des
nappes de synthé —, l'arbitrage retient `vsm.piano`, la machine MODÉLISÉE de
cordes frappées, devant tous les soustractifs. Sur un piano il battait le piano
échantillonné ; sur des nappes il bat les synthés. Soit cette machine est
remarquablement bonne, soit la métrique a un faible pour elle, et les deux
lectures demandent la même chose : l'écoute.

### L'écoute est venue, et elle dit trois choses que le rapport ne montrait pas

Une écoute INFORMÉE de *B4 Wuz Then* par l'utilisateur (23/08/2026) — la seule
vérité terrain que ce morceau aura jamais, puisque ses stems originaux
n'existent pas. Ce qu'elle entend, et ce que la chaîne avait répondu :

| stem | l'oreille | la chaîne (premier passage) |
|---|---|---|
| batterie | **TR-909**, très probable — kick sec, hats filtrés, open hat court, swing | `vsm.drums` (modélisée acoustique), **sans arbitrage** |
| basse | synthé analogique, **SH-101** en premier, Juno-106 possible ; *pas* une 303 | `vsm.obx` ; SH-101 **26e sur 27**, Juno **éliminé « trop faible ×10,6 »** |
| nappes | Juno-106 en premier, Alpha Juno, SH-101 — froid, métallique, Detroit/Chicago | `vsm.piano` ; Juno **3e** avec son patch d'usine (0,2787 contre 0,2544), mais son patch *cherché* fait 0,5028 |
| production | sample-based en partie (piano et string synthesizer crédités sur l'album) ; compacte, rugueuse, **dynamique limitée** | — |

**1. La batterie n'a jamais eu sa chance, et c'est écrit dans le code.** Le
commentaire de `reconstruire.py` dit : *« pas d'arbitrage : `vsm.drums` n'a pas
de concurrente crédible ici »*. Sur un morceau de techno de 1993, la concurrente
crédible est la **TR-909 du parc**, et la chaîne ne l'essaie jamais — ni elle ni
la TR-808. La batterie est le seul stem qui échappe à la règle « toutes les
machines en concurrence, l'arbitrage tranche ». C'est un trou de COUVERTURE, pas
un défaut de mesure, et seule une oreille pouvait le nommer : aucun chiffre ne
dit « cette batterie est une 909 » tant que la 909 n'est pas dans la course.
Ce qu'il faudrait : faire concourir `vsm.tr909` et `vsm.tr808` sur le stem de
batterie, avec les frappes du kit découvert traduites vers leurs notes — la
traduction que le commentaire disait non mesurée.

**2. Sur la basse et les nappes, les candidates de l'oreille ont été écartées
AVANT de concourir — par les deux défauts corrigés au § 5 bis (panne n° 6) et
ci-dessus (borne de niveau).** La SH-101 n'a jamais eu de patch cherché (pas
finaliste d'une recherche faite contre une note fantôme) ; le patch cherché du
Juno a été tué pour niveau sur la basse et rendu PIRE que l'usine sur les nappes
(0,5028 contre 0,2787). L'oreille ne dit donc pas que la chaîne s'est trompée de
machine : elle dit que la chaîne n'a pas MESURÉ les bonnes. Le rejeu avec les
deux correctifs est la suite directe.

**3. La « surprise » `vsm.piano` sur les nappes n'en est peut-être pas une.**
L'album crédite un piano et un *string synthesizer*. La chaîne entend peut-être
du piano parce qu'il y en a. Ce n'est pas tranché — l'oreille entend surtout du
Juno —, mais la lecture « la métrique a un faible pour `vsm.piano` » n'est plus
la seule.

**Le rejeu avec les deux correctifs (note qui sonne, borne de niveau) :
0,4088 contre 0,4290**, et le changement qui compte n'est pas le chiffre :

| stem | premier passage | avec les correctifs | l'oreille |
|---|---|---|---|
| nappes | `vsm.piano` 0,254 | **`jupiter8` 0,278 / `juno106` 0,279**, égalité à 0,2 % | Juno-106 |
| basse | `obx` 0,184 | `supersaw` 0,212 | SH-101, Juno |

Sur les nappes, le piano a disparu du podium : une fois la recherche saine, la
chaîne arrive sur deux polysynthés Roland à égalité — la famille que l'oreille
entend. L'arbitrage a déclaré l'égalité serrée (§ 5 quinquies) et remis le Juno
en jeu au verdict du mélange ; le Jupiter l'a emporté de 0,4 %. **L'oreille et
la mesure convergent.** Sur la basse, non : `supersaw`, moins bon que l'OB-X du
premier passage, et la SH-101 nulle part au sommet — le stem où elles restent
en désaccord, et la preuve que les correctifs rendent la recherche par note
SAINE sans rendre son verdict MEILLEUR. (Ce rejeu jouait encore `vsm.drums` :
il a été lancé avant l'arbitrage batterie ci-dessous.)

**Ce que la 909 a donné une fois dans la course (23/08, soir).** Les boîtes à
rythmes du parc concourent désormais sur le stem de batterie
(`drum_machine_track`, arbitrage dans `reconstruire.py`,
`--sans-arbitrage-batterie` pour l'ancien comportement). La « traduction » que
le commentaire disait non mesurée était triviale : les deux machines suivent
General MIDI, exactement les notes que la détection attribue déjà. Sur le stem
de batterie de *B4 Wuz Then*, patchs d'usine, piste entière :

| machine | distance |
|---|---|
| `vsm.drums` (modélisée acoustique) | 0,5728 |
| `vsm.tr909` | 0,3556 |
| **`vsm.tr808`** | **0,2507** |

Les deux boîtes écrasent la batterie acoustique d'un facteur deux : le trou de
couverture nommé par l'oreille est confirmé par le chiffre. Entre les deux, la
mesure préfère la **808** là où l'oreille dit « clairement 909 ». Deux lectures,
et aucune n'est tranchée : les patchs d'usine ne sont pas les réglages du
morceau (l'oreille décrit un kick 909 à decay moyen et des hats filtrés, et le
réglage sur la piste peut renverser l'ordre) ; ou la mesure entend autre chose
que l'oreille. Le réglage de `vsm.drums` sur la piste, lui, passe de 0,573 à
0,357 — il rattrape la 909 d'usine, pas la 808 — en poussant une pièce à
`level=0` : il fait taire ce qui gêne, ce qui dit assez où il plafonne.

**La chaîne complète avec les boîtes en lice : 0,2490.** Trois passages du même
morceau, mêmes options, seuls les correctifs changeant :

| passage | global | batterie | basse | nappes |
|---|---|---|---|---|
| premier | 0,4290 | `vsm.drums` | `obx` | `piano` |
| note qui sonne + borne | 0,4088 | `vsm.drums` | `supersaw` | `jupiter8` |
| **+ boîtes à rythmes en lice** | **0,2490** | **`tr808`**, réglée 0,251 → 0,209 | `supersaw` | `jupiter8` |

La batterie est le stem le plus lourd du mélange, et c'est là que le gain est
venu : **−39 % en un seul changement**, le plus gros écart mesuré sur ce projet
depuis l'arbitrage sur la piste lui-même. Le commentaire « pas de concurrente
crédible » avait coûté 0,16 de distance globale sur un morceau de techno — et
il a fallu une oreille pour le voir, parce qu'aucune mesure ne pouvait
désigner une machine absente de la course.

**808 ou 909, à armes égales.** L'oreille disait « clairement 909 » ; la mesure
d'usine disait 808. Les deux ont été RÉGLÉES sur la piste, même kit, mêmes
frappes, même budget (40 évaluations) :

| machine | usine | réglée | ce que le réglage a fait |
|---|---|---|---|
| `vsm.tr909` | 0,3556 | 0,2913 | kick tune 30, kick decay 1,2 (le max), snare decay 0,05 |
| **`vsm.tr808`** | 0,2507 | **0,2086** | kick tune 30, level 1, decay 0,196, hat decay 0,08 |

Le réglage gagne 17-18 % sur chacune et ne renverse pas l'ordre : la 808 reste
devant de 28 %. Ce n'était donc pas un effet des patchs d'usine. La chaîne
règle désormais la seconde boîte quand l'écart d'usine est sous 50 %
(`CLOSE_MARGIN_BATTERIE`) — une machine réglée contre une machine d'usine
n'est pas une comparaison.

**Mais un fait dans ces patchs met la MESURE en cause, pas les machines.** Les
deux réglages ont poussé `drum.kick.tune` à **30 Hz, la borne basse de
l'espace**. Or le kick réel du stem culmine à **59 Hz**, avec 73 % de son énergie
grave entre 55 et 120 Hz et moins de 2 % sous 40. Le réglage sur la piste
préfère donc un kick PLUS BAS que l'original — et une butée atteinte par deux
machines différentes n'est pas une coïncidence. Deux lectures : la métrique v2
récompense le sub au-delà de ce qu'il pèse à l'oreille ; ou `tune` sur un kick
à glissando ne désigne pas le fondamental tenu mais le point d'arrivée, et la
mesure aligne autre chose que le pic. L'oreille qui entend une 909 là où la
mesure entend une 808 pourrait tenir là. Non tranché ; c'est la mesure à faire
avant de croire la 808.

**La mesure est faite, en deux temps, et elle tranche contre la métrique.**

*Premier temps — cible contrôlée.* Une TR-808 dont le kick est à 60 Hz, sur
deux mesures ; des candidats identiques dont seul `kick.tune` varie :

| tune | 30 | 40 | 50 | 55 | **60** | 65 | 70 | 80 | 90 |
|---|---|---|---|---|---|---|---|---|---|
| 808 | 0,043 | 0,034 | 0,026 | 0,019 | **0,000** | 0,017 | 0,016 | 0,027 | 0,028 |
| 909 | 0,044 | 0,030 | 0,024 | 0,016 | **0,000** | 0,017 | 0,012 | 0,021 | 0,025 |

Minimum exact à 60, et 30 est LE PIRE. Quand tout le reste est égal, la
métrique lit l'accord du kick. L'hypothèse « biaisée vers le sub » est rejetée.

*Second temps — le stem réel.* Le patch 808 réglé, seul `kick.tune` varie, contre
la piste entière puis contre UNE frappe de kick réel isolée (pic mesuré à 59 Hz) :

| tune | 30 | 40 | 50 | 60 | 70 | 90 |
|---|---|---|---|---|---|---|
| piste entière | **0,2086** | 0,2163 | 0,2153 | 0,2219 | 0,2251 | 0,2256 |
| une frappe isolée | **0,5807** | 0,6017 | 0,5866 | **0,6212** | 0,6110 | 0,6068 |

Même sur une seule frappe dont le fondamental est à 59 Hz, la métrique préfère
30 Hz et trouve 60 Hz **le pire**. Le réglage n'a pas fait d'erreur : il a suivi
la mesure. C'est la mesure qui ne lit pas le grave d'un VRAI kick.

**Pourquoi, et c'est structurel.** Les sept termes de v2 — centroïde, largeur,
roll-off, planéité, MFCC, enveloppe, contraste — sont des statistiques de FORME
spectrale ; aucun ne mesure une HAUTEUR. Quand tout le reste est égal (808
contre 808), la forme suit l'accord et le minimum tombe juste. Sur un kick réel
— compressé, cliqué, 28 % de son énergie entre 90 et 120 Hz —, un 808 à 30 Hz
avec sa longue queue ressemble davantage *en silhouette* qu'un 808 à 60 Hz, même
si sa hauteur est fausse. L'oreille entend une hauteur ; la métrique entend une
silhouette.

**Ce que ça change.** Le verdict « 808 devant 909 » sur ce morceau n'est plus
une mesure de laquelle des deux est la bonne : c'est une mesure de laquelle a la
silhouette la plus proche, ce qui n'est pas la même question — et l'oreille
qui dit 909 n'est contredite par rien de solide. Ce que la métrique devrait
avoir pour trancher est un terme de hauteur pour les sons percussifs graves (la
position du pic sous 150 Hz, en rapport de fréquences). C'est un changement de
MÉTRIQUE, donc une v3, et les règles du § 10.3 valent : deux métriques ne se
comparent pas, et toute mesure publiée porte la sienne.

### La métrique v3 : v2 plus un terme de hauteur, et rien d'autre

`analyse/analyzer/audio_distance_v3.py`. Un terme de plus — la position du pic
d'énergie sous 150 Hz, comparée en OCTAVES entre cible et candidat, poids 0,20
comme l'enveloppe ou le contraste — et les sept termes de v2 inchangés, avec
leurs poids. Le terme est NUL quand la cible n'a pas de grave (moins de 10 % de
son énergie sous 150 Hz) : une nappe aiguë ne doit pas être jugée sur un bruit
de fond, et dans ce cas **v3 = v2 exactement**, testé.

Au passage, un défaut de structure corrigé : cinq modules choisissaient la
métrique chacun par un `if metric == "v2"`, et une troisième version aurait été
oubliée dans l'un d'eux sans que rien ne le dise. Une seule fabrique désormais
(`cached_distance_for`), qui REFUSE une métrique inconnue plutôt que de se
rabattre en silence.

**Les deux tests qui ont condamné v2, rejoués en v3 :**

| | v2 | **v3** |
|---|---|---|
| cible contrôlée (808 à 60 Hz), candidat à 30 Hz | 0,043 (le pire) | **0,218** (le pire, et plus nettement) |
| cible contrôlée, candidat à 60 Hz | 0,000 | **0,000** |
| frappe RÉELLE à 59 Hz, candidat à 30 Hz | **0,581 (le meilleur)** | **0,756 (le pire)** |
| frappe réelle, candidat à 50 Hz (pic mesuré 54) | 0,587 | **0,614 (le meilleur)** |
| frappe réelle, candidat à 60 Hz (pic mesuré 65) | 0,621 (le pire) | 0,646 |

Sur la cible contrôlée, rien ne casse ; sur la frappe réelle, l'ordre se
renverse exactement comme il le fallait. La métrique entend une hauteur.

**Et sur la question qui a tout déclenché — 808 ou 909 — v3 répond « égalité ».**
Les deux boîtes réglées sur la piste, même kit, même budget, jugées en v3 :

| | v2 réglée | **v3 réglée** |
|---|---|---|
| `vsm.tr909` | 0,2913 | **0,2426** — `kick.tune` d'usine, intouché |
| `vsm.tr808` | 0,2086 | **0,2399** — `kick.tune` passé de 30 à 50 |
| écart | 28 % pour la 808 | **1,1 %** |

Le terme de hauteur a ramené le kick de la 808 de sa butée à 30 Hz vers 50 (pic
rendu 54 Hz, pour 59 réels) et a laissé celui de la 909 à l'usine, qui tombait
déjà juste. Ce qui reste est une égalité à 1 %, sous la marge de 2 % de
l'arbitrage serré. **La préférence nette de v2 pour la 808 était un artefact de
silhouette** ; une fois la hauteur entendue, la mesure ne sait plus départager,
et c'est la réponse honnête. L'oreille qui disait 909 n'est contredite par
rien — elle a vu, avant la mesure, un défaut de la mesure.

La chaîne reste en v2 par défaut (`--metrique v3` l'active) tant que v3 n'a pas
été rejouée sur les bancs existants — *Children*, *House Of God*, les quatre
cibles de l'étape 10.3 — et la règle du § 10.3 vaut sans exception : une
distance v3 ne se compare qu'à une distance v3.

**Premier banc rejoué — les quatre cibles de l'étape 10.3 : v3 ne dégrade
rien.** Même graine, même budget (12 itérations), cinq machines en lice :

| cible | rang v2 | rang v3 | d(vraie) v2 | d(vraie) v3 |
|---|---|---|---|---|
| SH-101 | 1/5 | 1/5 | 0,054 | 0,045 |
| Juno-106 | 2/5 | 2/5 | 0,112 | 0,112 |
| DX7 | 1/5 | 1/5 | 0,046 | 0,046 |
| orgue à roues | 1/5 | 1/5 | 0,018 | 0,018 |

Trois distances sur quatre sont STRICTEMENT identiques : à la note 52
(≈ 165 Hz) ces cibles n'ont pas de grave, le terme de hauteur est nul par
construction, et v3 = v2. C'est ce que le banc devait d'abord vérifier — que le
terme se tait là où il n'a rien à dire. Seule la SH-101, qui a du sub, bouge,
et dans le bon sens. Le Juno à 2/5 est le cas documenté de la nappe sans trait
distinctif (un Jupiter-8 produit la même), inchangé. *Children* en v2 et v3 à
code identique est en cours ; c'est lui qui dira si v3 peut devenir le défaut.

**Et une phrase de l'écoute vaut pour tout le projet** : *« ne mets pas trop de
hi-fi : le caractère du morceau vient du côté compact, rugueux, limité en
dynamique de la production de l'époque. »* C'est, dit par une oreille, le fossé
de domaine que deux mesures indépendantes ont trouvé (§ 7 et
`ROADMAP-apprentissage.md`) : le parc produit des sons propres, un disque
contient une PRODUCTION. Une reconstruction qui vise le disque doit un jour
modéliser la production — pas seulement les machines.

---

## 5 octies. On réglait au hasard : le budget d'erreur, piste par piste

*Sky and Sand* (Fritz Kalkbrenner, 8 min 52, quatre stems) est reconstruit à
**0,2854**. La consigne était de rapprocher la sortie de l'entrée, et trois
leviers ont été essayés dans cet ordre — puis mesurés :

| levier | ce qu'il a donné sur le MÉLANGE |
|---|---|
| **budget ×3** (60 itérations, 120 évaluations de piste, 21 axes) | **0,2854 → 0,2521, −11,7 %** — le seul qui gagne |
| **batterie échantillonnée** (coups découpés dans l'enregistrement) | **0,2521 → 0,3560, +41 %** à budget égal — nettement pire |
| profil de nappe pour la piste `other` | **écarté** : 0,52 contre 0,32 au profil de piano, sur quatre nappes |
| suivi du niveau de l'original | −4,8 % au mieux (fenêtre d'une seconde), sur le rendu existant |

**Une erreur de lecture à ne pas refaire, et elle est de moi.** Les gains par
PISTE du budget triplé sont minuscules — basse 0,242 → 0,228, batterie 0,220 →
0,212 — et j'en avais conclu « le budget ne sert à rien ». Sur le MÉLANGE il
vaut 11,7 %, soit dix fois plus que la somme apparente : le verdict du mélange
recompose les pistes, et deux pistes un peu meilleures ne s'additionnent pas,
elles se combinent. **Un gain de piste ne se lit pas comme un gain de morceau**,
dans un sens comme dans l'autre.

**La batterie échantillonnée a été essayée parce que le budget d'erreur
ci-dessous la désignait, et elle a échoué — pour une raison qui n'était pas dans
mon modèle.** `--batterie-echantillonnee` ne rejoue pas « la vraie batterie » :
il découpe **UN échantillon par famille** (sept fichiers de 40 à 100 Ko) et le
rejoue à chaque frappe — 1 255 kicks strictement identiques, avec la fuite des
autres pièces que le détecteur signale lui-même (« aucune frappe isolée,
l'échantillon contient les autres pièces »). Le stem réel, lui, a 4 215 frappes
toutes différentes. Mesuré par le budget d'erreur : la piste de batterie
échantillonnée coûte **−72,3 %** (contre −63,5 % à la modélisée), c'est-à-dire
qu'elle est PLUS loin du stem réel. La route littérale n'est pas la route
fidèle.

**Aucun des autres ne pouvait marcher, et il était possible de le savoir en
quatre rendus.** `analyse/budget_erreur.py` remplace chaque piste reconstruite
par le stem RÉEL correspondant et mesure ce qu'une reconstruction PARFAITE de
cette piste-là rapporterait :

| piste rendue parfaite | distance | gain |
|---|---|---|
| **Batterie** | **0,1041** | **−63,5 %** |
| basse | 0,2854 | −0,0 % |
| voix | 0,2854 | −0,0 % |
| `other` | 0,2927 | +2,5 % (pire) |

La somme des pistes rendues séparément redonne exactement 0,2854, le chiffre du
rapport : la décomposition décrit bien ce morceau-là, et c'est imprimé par la
commande plutôt que supposé.

**Presque toute l'erreur est dans une seule piste.** Une basse parfaite ne
rapporterait rien ; une voix parfaite non plus, ce qui est normal puisqu'elle
est reportée telle quelle. Et le stem `other` RÉEL ferait légèrement pire que sa
reconstruction — signe que le réglage de cette piste compense déjà une partie de
ce que la batterie rate. Les trois leviers travaillaient tous sur les 36 % qui
ne bougent pas.

**Le plancher, aussi, était inconnu.** La somme des quatre stems séparés est à
**0,0551** de l'original, contre 0,9544 pour le silence. La séparation ne perd
donc presque rien : le plafond de la chaîne n'est pas la séparation, et il reste
les cinq sixièmes du chemin entre 0,2854 et 0,0551.

**En quoi la batterie modélisée diffère de la vraie, et ce n'est pas d'abord le
timbre.** Comparée au stem réel, à niveau efficace égal :

| | vraie | TR-808 réglée |
|---|---|---|
| crête d'enveloppe | 0,587 | **0,288** |
| part du temps au-dessus de 10 % de la crête | **0,52** | **1,00** |
| corrélation des enveloppes (20 ms) | — | 0,618 |

La vraie batterie se tait la moitié du temps ; la reconstruite jamais. Elle ne
frappe pas, elle bourdonne : 4 215 frappes en 532 secondes, avec des extinctions
trop longues qui se recouvrent en un mur continu, et une crête deux fois trop
basse — donc plus de transitoire. Le spectre dit la même chose : +6,6 dB à
120-250 Hz (les queues de kick et de toms qui s'accumulent), −3,1 dB à 4-8 kHz
(les charlestons et cymbales qui manquent).

> **CETTE CONCLUSION A ÉTÉ DÉMENTIE, ET IL FAUT LIRE LE § 5 NONIES AVANT LE
> PARAGRAPHE QUI SUIT.** La batterie n'était pas au bout de ce que la chaîne
> sait faire : elle était mal ATTRIBUÉE. Une famille de 811 frappes dont 69 %
> de l'énergie est sous 200 Hz — une grosse caisse — était jouée sur le clap de
> la TR-808, parce que le repli des toms était écrit d'avance. La replacer par
> la mesure vaut 0,3038 → 0,2454 en v4, à budget de réglage égal, sur la piste
> qui porte 63,5 % de l'erreur. Ce qui suit reste vrai de la ROUTE
> ÉCHANTILLONNÉE et du désaccord entre la métrique et l'oreille ; c'est le
> « au bout » qui était prématuré, et il l'était faute d'avoir regardé où les
> coups tombaient.

**La batterie est au bout de ce que la chaîne sait faire, et la limite n'est pas
le réglage.** Le budget d'erreur désignait la batterie ; deux routes ont été
éprouvées, et toutes deux butent sur une limite de conception, pas de recherche.

*La route échantillonnée ne peut pas reproduire la variation.* Le kit découpe
**un** représentant par famille — le médoïde des frappes isolées — et le sampler
n'a que **huit emplacements**. 1 602 kicks rejouent donc le même fichier. Le
stem réel a 4 215 frappes toutes différentes : la route est structurellement
incapable de s'en approcher, et c'est pourquoi elle mesure +41 %.

*La route modélisée est à son optimum, et c'est la MÉTRIQUE qui le place là.*
Le réglage a retenu `drum.kick.decay = 1,246 s` — pour un kick toutes les
330 ms, chaque frappe recouvre les trois suivantes — et `drum.snare.level = 0`,
c'est-à-dire une caisse claire muette. Ce ne sont pas des accidents de descente
par coordonnées : rendus à la main, tous les patchs plus courts sont MOINS bons
au sens de la métrique, et rallumer la caisse claire aussi.

| `kick.decay` | `snare.level` | distance v2 | crête | au-dessus de 10 % de la crête |
|---|---|---|---|---|
| **1,246** (retenu) | 0,0 | **0,2117** | 0,260 | 100 % |
| 0,600 | 0,0 | 0,2116 | 0,258 | 99 % |
| 0,300 | 0,0 | 0,2553 | 0,254 | 96 % |
| 0,150 | 0,0 | 0,3667 | 0,246 | 81 % |
| 0,300 | 0,6 | 0,2721 | 0,265 | 96 % |
| *stem réel* | | *0* | **0,587** | **52 %** |

**La métrique v2 récompense un bourdon continu et pénalise des frappes
détachées.** Elle compare des spectres moyennés ; le silence entre deux coups
n'y pèse rien, et la crête d'un transitoire non plus — la reconstruction plafonne
à 0,26 quand le réel monte à 0,587, sans que ça coûte quoi que ce soit au
chiffre. Raccourcir les extinctions rapproche l'enveloppe du réel (100 % → 81 %
du temps au-dessus du dixième de crête, contre 52 % pour le vrai) et ÉLOIGNE la
mesure. Les deux juges ne désignent pas le même patch.

**Ce qui est donc à trancher, et ce qui ne l'est pas.** Ce n'est pas un défaut
de la boîte à rythmes ni du réglage : c'est que le critère optimisé n'est pas
celui qu'on écoute. Deux sorties possibles, et elles ne se choisissent pas au
raisonnement : (1) un terme d'ENVELOPPE dans la métrique — crête et silence,
pas seulement spectre moyen —, qui changerait tous les verdicts du projet et
rendrait incomparables toutes les distances publiées ; (2) une route de batterie
qui rejoue plusieurs échantillons par famille, ce qui demande un sampler à plus
de huit emplacements. La première est peu coûteuse à écrire et coûteuse en
conséquences ; la seconde l'inverse. **`sky-hd/ecoute-batterie.wav` met les
trois versions bout à bout au même niveau efficace** — vraie, modélisée que la
métrique préfère, modélisée à extinctions courtes — parce que la seule question
qui décide est : laquelle ressemble à l'original ? Une oreille tranche cela en
deux minutes, aucune mesure disponible ici ne le fait.

### La métrique v4 : v3 plus un terme de dynamique, et rien d'autre

`analyse/analyzer/audio_distance_v4.py`. Un terme de plus — le FACTEUR DE CRÊTE
de l'enveloppe, son maximum rapporté à sa valeur efficace, comparé en octaves
entre cible et candidat, poids 0,20 comme l'enveloppe, le contraste ou la
hauteur — et les huit termes de v3 inchangés, avec leurs poids.

**Pourquoi v2 ne pouvait pas le voir, alors qu'elle a un terme « envelope ».**
Ce terme compare des enveloppes NORMALISÉES (`normalize(rms)`, centrées
réduites). La normalisation détruit exactement ce qui distingue une suite de
frappes d'un bourdon de même énergie moyenne : le rapport entre les crêtes et le
niveau courant. **v2 compare la FORME de l'enveloppe et jamais son RELIEF.**

**Le balayage d'extinctions rejoué en v4**, sur le stem de batterie réel :

| patch | v2 | **v4** | terme de dynamique | facteur de crête |
|---|---|---|---|---|
| kick 1,246 s (*retenu par v2*) | **0,2117** | 0,5194 | 1,053 | 1,371 |
| kick 0,600 s | 0,2116 | 0,4882 | 0,898 | 1,528 |
| **kick 0,300 s** | 0,2553 | **0,4878** | 0,677 | 1,779 |
| kick 0,150 s | 0,3667 | 0,5417 | 0,390 | 2,172 |
| kick 0,300 s + caisse claire | 0,2721 | 0,4952 | 0,630 | 1,839 |
| *stem réel* | *0* | *0* | *0* | **2,846** |

**L'ordre se renverse, et pas jusqu'à l'absurde.** v4 retient 0,300 s — une
extinction quatre fois plus courte que celle de v2 — et REFUSE 0,150 s, où les
termes spectraux se dégradent plus vite que le terme de dynamique ne s'améliore.
C'est un compromis, pas une bascule : le terme tire dans un sens, les huit
autres dans l'autre, et c'est ce qu'on demandait à une métrique.

**Un défaut trouvé en l'écrivant, et c'est un test qui l'a attrapé.** La
première version du terme réutilisait `audio_distance.envelope`, donc une
enveloppe centrée réduite. Sur un bourdon — enveloppe presque plate — il ne
reste qu'une ondulation minuscule que la division par l'écart-type ramène à
l'unité : le « facteur de crête » obtenu valait **3,9 contre 2,5** pour de
vraies frappes, l'inverse de la vérité. Le module calcule donc sa propre
enveloppe, non normalisée. Quatre tests verrouillent le terme, dont deux qui
n'ont besoin d'aucun moteur.

**Ce que v4 coûte, et pourquoi elle n'est pas le défaut.** Elle change les
verdicts partout où la dynamique compte, donc sur toutes les batteries. La règle
du § 10.3 vaut sans exception : **toutes les distances publiées de ce projet
sont en v2**, et une distance v4 ne se compare qu'à une distance v4. `--metrique
v4` l'active ; elle ne deviendra le défaut qu'une fois rejouée sur les bancs
existants, comme v3 avant elle.

**La leçon de méthode, et c'est la sixième forme de celle du § 10.3.** Les
précédentes disaient qu'une distance n'est un chiffre que si l'on sait à quelles
conditions elle a été obtenue — la métrique, le budget, le `gate`, le taux
d'échantillonnage. Celle-ci porte sur ce qu'on fait d'un chiffre unique :
**une distance globale ne dit pas OÙ est l'erreur, et sans ce partage on règle
au hasard, longtemps.** La mesure coûte quatre rendus. Elle passe désormais
avant tout réglage.

---

## 5 nonies. Une famille de 811 frappes jouée sur la mauvaise pièce

Le budget d'erreur du § 5 octies désignait la batterie — 63,5 % de l'erreur du
morceau — et concluait que le réglage n'y pouvait plus rien. Il ne disait pas
POURQUOI. `analyse/diagnostic_batterie.py` rejoue la même rythmique par des
moyens différents et mesure : c'est la seule façon de départager les trois
causes possibles, la détection, le timbre, et l'ATTRIBUTION des voix.

Toutes les distances ci-dessous sont en **métrique v4**, contre le stem de
batterie de *Sky and Sand* (532,2 s, kit de 7 pièces, 4 215 frappes,
classifieur `frappes.joblib`). La règle du § 10.3 vaut sans exception : elles ne
se comparent qu'entre elles. Les témoins donnent l'échelle — le stem contre
lui-même vaut 0,0000, contre le silence 1,2933, contre lui-même à −6 dB 0,0000,
ce qui vérifie au passage que la métrique est bien insensible au niveau.

**LA DÉTECTION N'EST PAS EN CAUSE, ET C'EST LA PREMIÈRE CHOSE À ÉCARTER.**
99,8 % de l'énergie du stem tombe à moins de 300 ms d'un coup détecté (90,8 % à
120 ms, 52,4 % à 50 ms). Ce qu'aucune machine ne rattraperait — ce qui n'a pas
été entendu du tout — est de deux pour mille.

**LE DÉFAUT TIENT EN UNE LIGNE.** La famille nommée « tom », 811 frappes, a
**69 % de son énergie sous 200 Hz** : c'est une grosse caisse. La TR-808 n'a pas
de toms, et la table de correspondance la rabattait sur le **CLAP** — un grave
posé sur une salve de bruit, pour un cinquième du morceau. Ce n'était pas un
accident : c'était un repli ÉCRIT D'AVANCE, « la pièce la plus proche en
fonction ». Le compte des voix disait le reste : trois familles empilées sur le
clap, **1 618 frappes sur 4 215 — 38 % du morceau — sur une seule voix**, pendant
que la vache et la charleston ouverte ne jouaient pas.

**CE QUE VALENT LES ATTRIBUTIONS, À BUDGET DE RÉGLAGE ÉGAL** (120 évaluations,
21 axes — la descente de la chaîne, partant du patch d'usine) :

| attribution | patch d'usine | **réglée** |
|---|---|---|
| **TR-808, repli MESURÉ au spectre** (adopté) | 0,7047 | **0,2454** |
| TR-808, le même sans la préférence pour une voix libre | 0,7822 | 0,2604 |
| TR-808, voix décidées ENTIÈREMENT au spectre | 1,0060 | 0,2760 |
| *TR-808, ancienne table — le chiffre publié de sky-v5* | *0,8710* | *0,3038* |
| TR-808, table corrigée à la main (clap déclaré, percussion → vache) | 0,8325 | 0,3114 |
| **TR-909, repli MESURÉ au spectre** (adopté) | 0,8625 | **0,3392** |
| TR-909, ancienne table | 0,8264 | 0,3432 |
| TR-909, voix décidées entièrement au spectre | 1,4704 | 0,3935 |
| *sampler, VRAIS coups découpés dans le stem* | — | *0,4187* |
| *`vsm.drums`, patch d'usine* | *1,8377* | — |

**Le gain est de 19 % sur la piste qui porte 63,5 % de l'erreur du morceau**, et
il ne coûte pas une évaluation de plus : c'est la même recherche, partie d'un
meilleur point.

**ET IL SE RETROUVE SUR LE MORCEAU ENTIER.** La chaîne complète rejouée sur
*Sky and Sand*, mêmes stems, même budget, même métrique, une seule décision
changée :

| | sky-v5 | **sky-v6** |
|---|---|---|
| distance globale (v4) | 0,3327 | **0,2933** |
| | | **−11,8 %** |

C'est l'ordre de grandeur qu'annonçait le budget d'erreur du § 5 octies, et il
le confirme d'un second chemin : une piste qui vaut 63,5 % de l'erreur, gagnant
19 %, rapporte une douzaine de pour cent sur le mélange.

**UN EFFET DE BORD QUI VAUT AUTANT QUE LE CHIFFRE.** L'arbitrage de batterie
désigne maintenant `vsm.tr808` du premier coup, à 0,705 contre 0,863 pour la
909. Avant, il désignait la **909** (0,826 contre 0,871) et ne retrouvait la 808
que par la règle de l'arbitrage SERRÉ, qui règle les deux quand elles sont à
moins de 5 %. La marge est passée de 5 % à 22 % : la bonne machine n'est plus
rattrapée de justesse, elle est choisie. Une attribution fausse ne dégradait pas
seulement le réglage — elle brouillait le verdict de machine, en amont.

**TROIS CHOSES QUE CE TABLEAU DIT, ET QU'IL FALLAIT MESURER POUR LES SAVOIR.**

*Le timbre n'était pas le plafond.* Les VRAIS coups, découpés dans
l'enregistrement et rejoués aux VRAIS instants, valent 0,4187 — nettement plus
que la boîte modélisée et réglée à 0,2454. Ce que la route échantillonnée perd
en variation (un représentant par famille, § 5 octies), aucune fidélité de
timbre ne le rachète.

*`vsm.drums` au patch d'usine est plus loin du stem que LE SILENCE* — 1,8377
contre 1,2933. Une batterie acoustique modélisée jouant un motif de techno n'est
pas une candidate faible, c'est une candidate nuisible ; l'arbitrage a raison de
lui préférer une boîte, et il le fait déjà.

*Un verdict au patch d'usine ne survit pas au réglage*, et c'est la septième
forme de la leçon du § 5 septies. L'ordre des attributions à l'usine n'est pas
leur ordre après réglage : la variante « tout au spectre », **dernière des 808 à
l'usine (1,0060), passe deuxième une fois réglée (0,2760)**. Une attribution ne
se juge donc pas sur un rendu d'usine, pas plus qu'une machine.

**CE QUI EST ADOPTÉ, ET CE QUI EST REFUSÉ.** L'idée large — décider TOUTES les
voix au spectre, en donnant à chaque famille une voix à elle — a été écrite,
mesurée, et elle perd (0,2760 contre 0,2454). Elle déloge des familles de la
voix qui porte leur propre nom, pour éviter un empilement qui n'est pas le
problème. Ce qui rapporte est plus étroit, et se dit en une phrase :

> **Une famille dont le nom désigne une voix que la machine possède la garde.
> Une famille dont le nom ne désigne AUCUNE voix de cette machine est placée par
> son SPECTRE, sur la première voix LIBRE de son rôle.**

C'est `_voix_de_la_famille`, dans `analyse/analyzer/vsm_drumkit.py`. Le rôle se
lit sur deux grandeurs seulement — la part de l'énergie sous 200 Hz et celle
au-dessus de 2 kHz —, parce que ce sont les deux dont l'oreille se sert pour
ranger une pièce de batterie. La préférence pour une voix LIBRE vaut à elle
seule 0,2604 → 0,2454 : c'est la famille `percussion` (248 frappes, un médium)
qui cesse de s'empiler sur la caisse claire et va sur la vache. Et elle ne cède
la place qu'à une famille NOMMÉE — céder aussi aux autres replis reviendrait à
l'exclusivité générale, qui a été mesurée et qui perd.

La table `DRUM_MACHINE_NOTES` n'est plus la règle : elle est le **dernier
recours**, pour un kit dont aucun profil n'a pu être mesuré. Ses replis écrits
d'avance sont exactement ce que la mesure a condamné.

**UNE ERREUR COMMISE EN L'ÉCRIVANT, ET C'EST LA MESURE QUI L'A ATTRAPÉE.** La
liste des voix acceptables par rôle est une DÉCLARATION sur les machines, et la
première version mettait les toms parmi les voisines du rôle « caisse claire »,
au motif que ce sont des peaux. Sur la TR-909 — qui, elle, a des toms — la
famille `percussion` allait alors sur un tom moyen, et la piste passait de
0,3392 à **0,3688** : pire que l'ancienne table. Un tom est une peau GRAVE, sa
place est dans le rôle « grosse caisse » et nulle part ailleurs. Une déclaration
se corrige comme une mesure : en la mesurant.

### Le budget d'erreur rejoué : la basse est le second front

Le § 5 octies a établi qu'on ne règle rien sans savoir OÙ est l'erreur. Le
budget a donc été rejoué sur la reconstruction corrigée — et sur l'ancienne, en
**v4** elle aussi, parce que le chiffre de 63,5 % qui a lancé tout ce travail
était en v2 et que la règle du § 10.3 interdit de les comparer.

| piste rendue PARFAITE | sky-v5 | **sky-v6** |
|---|---|---|
| Batterie | 0,1741 — −47,7 % | 0,1720 — **−41,6 %** |
| basse | 0,3200 — −3,8 % | 0,2451 — **−16,7 %** |
| `other` | 0,3456 — +3,9 % | 0,2999 — +1,8 % |
| voix | 0,3326 — −0,0 % | 0,2979 — +1,2 % |
| *distance du morceau* | *0,3327* | *0,2933* |
| *plancher (somme des stems)* | *0,0644* | *0,0644* |

**Le chiffre à lire n'est pas le pourcentage, c'est la colonne de gauche.** Une
batterie PARFAITE menait à 0,1741 avant la correction et mène à 0,1720 après :
**le plafond n'a pas bougé**. Ce que la correction a fait, c'est rapprocher le
morceau de ce plafond-là — d'où une part qui tombe de 47,7 % à 41,6 % sans
qu'aucune limite ait reculé. Un budget d'erreur mesure une DISTANCE AU PLAFOND,
et sa part relative se déplace dès que le reste bouge.

**Et la basse a changé de statut.** Elle valait 3,8 % et en vaut 16,7 % : elle
ne s'est pas dégradée, c'est le total qui a fondu autour d'elle. Elle est
maintenant le second front, là où le § 5 octies pouvait écrire « une basse
parfaite ne rapporterait rien ». La batterie reste le premier — 41,6 %, et la
route de l'attribution est désormais épuisée : la détection est hors de cause,
le timbre n'est pas le plafond, les voix sont justes. Ce qui reste d'elle est ce
que le § 5 octies avait déjà nommé, le désaccord entre la métrique et l'oreille,
et l'unique échantillon par famille.

**Ce qui reste ouvert, et il est nommé.** Le rôle n'a que quatre valeurs — peau
grave, médium, bruit clair, métal — et rien n'y distingue une vache d'une caisse
claire : sur *Sky and Sand*, c'est la préférence pour une voix libre, et non le
spectre, qui a mis la percussion au bon endroit. Un cinquième rôle demanderait
un discriminant de « métal accordé » ; l'écrire sur une seule famille d'un seul
morceau serait le sur-ajuster. Il attend une seconde occurrence.

---

## 5 decies. Le classement au stem ne prédit pas le classement au mélange

Le § 5 nonies a fait de la basse le second front — 16,7 % de l'erreur de *Sky
and Sand*. Il ne disait pas pourquoi elle coûtait, et
`analyse/diagnostic_basse.py`, pendant mélodique de `diagnostic_batterie.py`,
applique la même méthode : rejouer la même piste par des moyens différents, et
mesurer. La réponse n'était dans aucune des cases attendues.

**L'OUTIL REND DEUX COLONNES, ET IL FAUT LES DEUX.** La distance de la piste à
SON STEM est la cible que la chaîne optimise, piste par piste, et la seule dont
elle dispose à ce moment-là. La distance du MÉLANGE COMPLET à l'original, la
piste remplacée par la variante, est ce qu'on écoute. `vsm_mix_verdict` existe
parce que les deux se contredisent ; personne n'avait mesuré de combien.

Toutes les distances sont en **métrique v4**, sur *Sky and Sand* (532,2 s,
piste `bass`, 1 128 notes), et la règle du § 10.3 vaut : elles ne se comparent
qu'entre elles. Trois témoins donnent l'échelle — le projet **tel quel** vaut
0,2933, la piste **coupée** 0,2781, la piste telle quelle **contre son stem**
0,3692.

**LE TABLEAU, À PATCH D'USINE, CHAQUE VARIANTE RENDUE EN PROJET ENTIER** (le bus
maître n'est pas linéaire : additionner des rendus de pistes donne 0,2945 là où
le rendu du projet donne 0,2933, avec des écarts d'échantillon jusqu'à 0,29 —
d'où un rendu complet par variante, une quinzaine de secondes) :

| machine, patch d'usine | au stem | **au mélange** |
|---|---|---|
| **`vsm.wavetable`** — celle que l'arbitrage retient | **0,3692** | 0,2933 |
| `vsm.minimoog` | 0,4303 | 0,2845 |
| **`vsm.phasedist`** | 0,4340 | **0,2557** |
| `vsm.prophet` | 0,4449 | 0,2764 |
| `vsm.multisample` | 1,1370 | 0,2781 |
| *témoin : la piste COUPÉE* | — | *0,2781* |

**LES DEUX CLASSEMENTS SONT À PEU PRÈS INVERSES.** La première au stem est la
DERNIÈRE au mélange ; la troisième au stem est la première. Ce n'est pas un
écart de mesure qu'on pourrait absorber par un seuil un peu plus large : c'est
un désaccord d'ordre.

**ET LA BASSE RECONSTRUITE COÛTAIT PLUS QU'ELLE NE RAPPORTAIT.** Le morceau
publié vaut 0,2933 ; le même morceau **sans basse du tout** vaut 0,2781. La
chaîne ajoutait un instrument qui dégradait le mélange de 5,5 %, et rien ne
pouvait le voir : le verdict du mélange ne jugeait que les variantes qu'on lui
présentait, et on ne lui en présentait aucune. `vsm.phasedist` à 0,2557 fait
12,8 % de mieux que ce qui est publié, et 8,1 % de mieux que le silence.

**LE RÉGLAGE DE PISTE GAGNE SUR LE STEM ET PERD SUR LE MÉLANGE — QUATRE FOIS SUR
QUATRE** (budget de la chaîne : 120 évaluations, 21 axes) :

| machine | stem : usine → réglée | mélange : usine → réglée |
|---|---|---|
| `vsm.wavetable` | 0,3692 → **0,3043** (−17,6 %) | 0,2933 → 0,3010 (+2,6 %) |
| `vsm.minimoog` | 0,4303 → **0,3334** (−22,5 %) | 0,2845 → 0,3148 (+10,6 %) |
| `vsm.phasedist` | 0,4340 → **0,3092** (−28,8 %) | 0,2557 → 0,2623 (+2,6 %) |
| `vsm.prophet` | 0,4449 → **0,3051** (−31,4 %) | 0,2764 → 0,3006 (+8,8 %) |

Aucune exception. Le réglage fait exactement ce qu'on lui demande — il se
rapproche du stem — et le mélange le refuse à chaque fois. Sur cette piste, le
stem n'est pas un bon mandataire du morceau, et l'optimiser plus fort éloigne
davantage. C'est la forme la plus dure d'une leçon déjà écrite deux fois.
Le § 5 septies avait montré qu'un gagnant sur UNE NOTE ne tient pas la PISTE ;
on montre ici qu'un gagnant sur la PISTE ne tient pas le MÉLANGE. Chaque
critère intermédiaire est un mandataire, et chacun se paie. Ce qui est neuf,
c'est que le désaccord ne porte plus seulement sur QUELLE machine on choisit,
mais sur COMBIEN on la règle : mieux coller au mandataire peut nuire.

*Et la chaîne le rattrapait déjà, ce qui mérite d'être dit.* Le réglage n'est
pas retiré, parce que le verdict du mélange remet systématiquement le patch
d'AVANT réglage en concurrence — et sur sky-v6 il l'a effectivement préféré pour
`bass` (0,2901 contre 0,2974) comme pour `other` (0,2785 contre 0,2901). Le
garde-fou existait et il a fonctionné. Ce que le tableau ci-dessus ajoute, ce
n'est donc pas une panne à corriger : c'est la mesure de ce que ce garde-fou
évite, sur une piste où il travaille à chaque fois.

**LE MÊME DÉSACCORD SUR `other`, ET IL N'A RIEN DE PARTICULIER À LA BASSE.**
Mêmes témoins, même protocole, piste `other` (4 280 notes) : projet tel quel
0,2933, piste coupée 0,3208, piste telle quelle contre son stem 0,3089.

| machine, patch d'usine | au stem | **au mélange** |
|---|---|---|
| **`vsm.string`** — celle que l'arbitrage retient | **0,3089** | 0,2933 |
| **`vsm.stochastic`** | 0,3595 *(+16,4 %)* | **0,2834** |
| `vsm.piano` | 0,3817 *(+23,6 %)* | 0,2883 |
| `vsm.divider` | 0,4317 *(+39,8 %)* | 0,2971 |
| `vsm.tb303` | 0,4684 *(+51,6 %)* | 0,2853 |
| *témoin : la piste COUPÉE* | — | *0,3208* |

Encore une fois la première au stem est l'avant-dernière au mélange. Deux
différences avec la basse, et elles comptent toutes les deux. D'abord `other`
bat franchement son témoin de coupure (0,2933 contre 0,3208) : cette piste-là
rapporte vraiment, elle est seulement mal jouée. Ensuite `vsm.tb303`, **dernière
au stem à plus de 50 %**, est troisième au mélange et meilleure que ce qui est
publié — un rappel que le désordre entre les deux classements n'est pas un
décalage d'un ou deux rangs qu'on pourrait borner.

**CE QUE LE SEUIL DE 2 % NE POUVAIT STRUCTURELLEMENT PAS VOIR.** L'arbitrage ne
remettait en jeu au mélange qu'une seconde machine « à portée », `CLOSE_MARGIN =
0,02`. Les écarts réels au stem : `vsm.minimoog` à 16,5 %, `vsm.phasedist` à
17,5 %, `vsm.prophet` à 20,5 % — huit à dix fois la marge. Le seuil supposait
qu'une machine loin derrière AU STEM est loin derrière tout court, et c'est
précisément ce que le tableau ci-dessus réfute.

**DÉCISION : LE SEUIL DISPARAÎT, LES TROIS MEILLEURES MACHINES SUIVANTES
REPARTENT TOUTES AU MÉLANGE** (`runners_up`, `MACHINES_AU_MELANGE = 3`). Trois
machines DISTINCTES, jamais un second patch de la gagnante — un autre patch ne
répare pas un mauvais choix de machine, et c'est ce choix-là que le verdict du
mélange ne savait pas défaire.

*Ce que le seuil protégeait était le coût, et le coût est mesurable* : une
proposition de plus vaut un rendu de projet et une distance, une quinzaine de
secondes, contre les ~5 900 s d'une reconstruction. Le seuil économisait un
millième du temps et laissait passer douze pour cent de qualité.

*La raison d'être de l'ancien seuil reste vraie, et elle est absorbée.* Il avait
été écrit pour un cas mesuré (§ 5 quinquies) : sur le stem `other` de
*Children*, l'arbitrage sépare `vsm.ms20` de `vsm.string` par UN MILLIÈME, et à
cette marge un simple changement de protocole les intervertit. Une égalité
pareille est évidemment dans les trois premières ; la remise en jeu systématique
la couvre, et couvre en plus les écarts francs que le seuil laissait passer.

*Pourquoi trois et pas cinq, et la question a été posée aux chiffres.* La
machine que le mélange retient est TROISIÈME au stem pour la basse,
`vsm.phasedist`, et DEUXIÈME pour `other`, `vsm.stochastic` : deux
suffiraient de justesse, trois couvrent les deux cas. L'objection était sérieuse
— sur `other`, `vsm.tb303` est DERNIÈRE au stem et troisième au mélange, ce qui
donne l'impression qu'il faudrait tout remettre en jeu. Les deux tableaux y
répondent : sur les deux pistes, la GAGNANTE du mélange est dans les trois
premières du stem, et `vsm.tb303` ne gagne pas — elle bat seulement le choix
publié. Élargir à cinq coûterait une minute de plus par piste pour ajouter des
candidates dont aucune, sur les deux cas mesurés, ne l'emporte. Le chiffre est
donc mesuré et non choisi, et il sera remesuré au premier morceau qui le
démentira.

*Le seuil de la BATTERIE reste, et ce n'est pas un oubli.*
`CLOSE_MARGIN_BATTERIE = 0,50` ne décide pas la même chose : il choisit les
boîtes à rythmes qu'on RÈGLE — au budget de piste entier, `--budget-piste`
évaluations chacune — et non les propositions qu'on soumet au mélange. Son coût
est donc d'un tout autre ordre que la quinzaine de secondes d'un rendu, et sa
largeur, 50 % sur un parc de deux boîtes, le rend de toute façon presque
toujours vrai. Il sera revu le jour où le parc de percussions s'élargira, pas
avant.

**LA DÉCISION A ÉTÉ REJOUÉE EN VRAI, DEUX FOIS, ET ELLE TIENT (31/08/2026).**
Tout ce qui précède reposait sur un diagnostic — des variantes rendues à la
main sur une chaîne arrêtée. L'A/B qui manquait a tourné : *B4 Wuz Then*,
chaîne complète, mêmes stems, même budget (v4, 60 itérations, 120 évaluations,
21 axes, classifieur de frappes), et UNE seule variable —
`--machines-au-melange`, l'option créée pour que le témoin soit du même code
que ce qu'il témoigne. (Sa création a d'ailleurs attrapé un défaut : à zéro,
`runners_up` rendait quand même une machine, et le témoin aurait comparé trois
remises en jeu à UNE en croyant les comparer à aucune. Un test le verrouille.)

| *B4 Wuz Then*, chaîne complète | distance globale |
|---|---|
| témoin — la gagnante du stem part seule | 0,3683 |
| **règle des trois machines suivantes** | **0,2521 — soit −31,5 %** |

*L'expérience est propre, et ça se vérifie dans les rapports* : en amont du
verdict, les deux moitiés sont identiques au chiffre près (basse D=0,1964,
`other` D=0,2591, arbitrage 0,3854 et réglage 0,3874 des deux côtés). La seule
variable explique tout l'écart.

**LES TROIS DÉCISIONS FINALES SONT DES CHOIX QUE L'ANCIENNE CHAÎNE NE POUVAIT
PAS FAIRE.** `bass` retient `vsm.piano` — la TROISIÈME machine suivante, à
20,5 % au stem : le choix de trois plutôt que deux paie une fois de plus.
`other` retient `vsm.multisample`. La batterie retient la SECONDE boîte
(`vsm.tr909` réglée, 0,2499 contre 0,3270). Et sur `other`, la règle ne gagne
pas seulement des points : dans le témoin, la piste retenue DÉGRADE le mélange
(0,3751 contre 0,3688 sans elle — le silence gagne) ; avec la règle,
`vsm.multisample` la rend utile (0,3270 contre 0,3378 sans elle). Une piste
nuisible est devenue une piste qui rapporte.

**LA DEUXIÈME MESURE, ET POURQUOI ON PUBLIE UNE FOURCHETTE ET PAS LE MEILLEUR
CHIFFRE.** Une première paire A/B avait tourné le même jour SANS le
classifieur de frappes (voir plus bas) : mêmes conditions internes, batterie
dégradée des deux côtés, écart **−10,2 %** (0,2737 contre 0,3049). Deux
mesures indépendantes, même signe, même mécanisme — et une ampleur qui varie
du simple au triple selon le contexte. Ce que la règle vaut est donc
**« entre −10 et −31 % sur ce morceau »**, pas −31,5 %.

**ET SUR *KNIGHT OF THE JAGUAR*, TROISIÈME MORCEAU, LE MÊME MÉCANISME SANS
TÉMOIN DÉDIÉ** : dans les deux exécutions (avec et sans classifieur de
frappes), les DEUX stems mélodiques sont gagnés au mélange par une machine
suivante — `vsm.obx` sur la basse (l'arbitrage valait 0,3226, elle 0,2795,
et les deux AUTRES suivantes battaient aussi l'arbitrage), `vsm.juno106` puis
`vsm.minimoog` sur `other`. Sur trois morceaux mesurés, il ne s'est pas
trouvé UNE piste mélodique où la remise en jeu n'ait rien changé.

**ET *SKY AND SAND* LUI-MÊME, LE MORCEAU QUI A TOUT DÉCLENCHÉ, CONFIRME LA
PRÉDICTION MACHINE POUR MACHINE.** Chaîne complète rejouée aux conditions de
sky-v6 (v4, 60 itérations, 120 évaluations, 21 axes, classifieur de frappes,
mêmes stems — 7 pièces et 4 215 frappes détectées à l'identique), seule la
remise en jeu en plus : **0,2467 contre 0,2933 publié, −15,9 %.** Le verdict
du mélange a retenu `vsm.phasedist` pour la basse et `vsm.stochastic` pour
`other` — précisément les deux machines que les tableaux ci-dessus
désignaient. Et le constat qui a ouvert cette section est refermé : la basse
qui DÉGRADAIT le morceau (0,2933 contre 0,2781 sans elle) le sert désormais
(0,2510 contre 0,2771 sans elle). Le § 5 octies donnait la basse comme second
front du budget d'erreur ; ce front-là est pris.

**UNE ERREUR DE LANCEMENT EN CHEMIN, ET C'EST LA PROVENANCE QUI L'A DITE.**
Les quatre premières exécutions de cette campagne sont parties SANS
`--classifieur-batterie` — une section `modeles` oubliée en recopiant les
options d'une provenance. Les distances globales de cette série ne se
comparent à rien de publié, et une comparaison sky-v7/sky-v6 annoncée sur leur
foi a été RETIRÉE. Ce qui a permis de s'en apercevoir avant publication :
chaque rapport portait `classifieurFrappes: "aucun"` — A4.2 a fait exactement
le travail pour lequel il existe. La paire A/B de cette série, elle, reste
valide (ses deux moitiés étaient identiquement dégradées) : c'est le −10,2 %
de la fourchette ci-dessus.

**LE VERDICT DU MÉLANGE NE SAIT PAS COUPER — IL SAIT DÉSORMAIS LE DIRE.** Il
choisit parmi les variantes qu'on lui soumet, et « pas de piste du tout » n'en
est pas une. Sur la basse, le témoin de coupure valait 0,2781 quand le morceau
publié valait 0,2933 : le silence battait la reconstruction, et aucune pièce de
la chaîne n'était en mesure de le remarquer. Le remède technique est d'une ligne
— soumettre le silence comme candidate — et c'est justement pourquoi il ne
fallait pas l'écrire sans y penser. Une chaîne autorisée à supprimer une piste
optimise la métrique en abandonnant le morceau : elle rendrait un *Sky and Sand*
sans basse, ce qu'aucune oreille n'accepterait. La mesure et l'intention
divergent ici pour de bon, comme au § 5 octies.

**Ce qui a été tranché, et écrit.** `keep_what_helps_the_mix` mesure maintenant,
pour CHAQUE piste qu'il examine, le morceau rendu sans elle ; le chiffre est dit
au journal (`[sans la piste : 0,2781]`), publié au rapport
(`mixDistanceMuted`), et quand il bat ce qui a été retenu la chaîne l'annonce en
toutes lettres. Ce qu'elle ne fait pas, c'est couper : la piste est conservée
audible, et la décision reste humaine. Un test le verrouille dans le pire cas
possible — cible silencieuse, où couper est optimal au sens de la métrique — et
exige que la piste survive.

**UNE PANNE DE MÉTHODE, ATTRAPÉE PAR UN TÉMOIN, ET C'EST POURQUOI LE TÉMOIN
EXISTE.** La première passe du diagnostic donnait un tout autre tableau —
`vsm.wavetable` à 0,2729 au mélange, tous les volumes à 0,34. Le calage de
niveau mesurait le niveau efficace du MÉLANGE au lieu de celui de la PISTE
SEULE, d'où un volume trois fois trop bas pour tout le monde. Rien dans les
chiffres ne criait l'erreur ; ce qui l'a dite, c'est le témoin de cohérence :
la candidate identique à ce que le projet joue DOIT reproduire le chiffre
publié, et 0,2729 n'est pas 0,2933. Sans ce témoin, le mauvais tableau serait
ci-dessus.


## 5 undecies. L'étape la plus chère de la chaîne perd contre le patch d'usine

Le § 5 septies a établi qu'un gagnant sur UNE NOTE ne tient pas la PISTE. Le
§ 5 decies a montré que le classement au stem ne prédit pas celui du mélange.
Cette section-ci ne mesure pas une machine ni un seuil : elle mesure **ce que
rapporte la recherche de patch elle-même**, et la réponse tient en une ligne.

**LE RELEVÉ EST GRATUIT, ET PERSONNE NE L'AVAIT FAIT.** Chaque arbitrage de
piste imprime déjà les deux chiffres : `arbitrage piste CHANGE vsm.wavetable
(patch d'usine) D=0.369 (la recherche donnait 0.649)`. À gauche ce que vaut le
patch retenu sur la piste entière, à droite ce que valait le patch trouvé par
la recherche note à note. Il suffisait de les agréger sur tous les journaux
conservés.

**HUIT COUPLES (MORCEAU, STEM) INDÉPENDANTS**, sur quatre morceaux — *Sky and
Sand*, *B4 Wuz Then*, *Knight of the Jaguar*, *Clair de Lune*. Les quatorze
lignes brutes comptaient *Sky and Sand* quatre fois ; ce tableau ne le compte
qu'une, et c'est la seule lecture honnête.

| morceau | stem | qui gagne l'arbitrage | retard de la recherche |
|---|---|---|---|
| *Clair de Lune* | `other` | **usine** | **+254 %** |
| *Sky and Sand* | `bass` | **usine** | +72 % |
| *Knight of the Jaguar* | `other` | **usine** | +44 % |
| *B4 Wuz Then* | `other` | **usine** | +33 % |
| *Sky and Sand* | `other` | **usine** | +25 % |
| *B4 Wuz Then* | `bass` | **usine** | +21 % |
| *Knight of the Jaguar* | `bass` | cherché | — |
| *Clair de Lune* | `bass` | cherché | — |

**LE PATCH D'USINE GAGNE SIX FOIS SUR HUIT.** Et quand il gagne, le patch
cherché n'est pas légèrement derrière : il est en retard de 21 à 254 %, médiane
38,5 % (la médiane de {21, 25, 33, 44, 72, 254}).

**CE QUE ÇA COÛTE, PUISQUE C'EST LÀ QUE PASSE LE TEMPS.** La recherche note à
note demande 200 à 900 s par stem selon le budget et le nombre de candidates —
c'est, avec le réglage de piste, l'un des deux postes principaux d'une
reconstruction de plusieurs heures. Elle produit donc, six fois sur huit, un
patch qui perd contre **ne rien faire**.

**CE QU'IL NE FAUT PAS EN CONCLURE, ET C'EST IMPORTANT.** La recherche ne sert
pas qu'à trouver un patch : sa distance note à note est ce qui CLASSE les
machines, donc ce qui alimente la présélection (`--finalistes`) et l'ordre des
candidates de l'arbitrage. La supprimer sans rien mettre à la place retirerait
au parc son seul moyen de dégrossir. Deux des huit cas, par ailleurs, sont
gagnés par elle — sur les deux basses les plus « instrumentales » du lot, un
piano et une basse de guide d'ondes.

**L'HYPOTHÈSE À MESURER, ÉCRITE AVANT LA MESURE POUR NE PAS LA TORDRE APRÈS.**
Si l'arbitrage juge de toute façon chaque machine sur la piste entière avec son
patch d'usine, et si ce patch gagne six fois sur huit, alors le budget de la
recherche serait peut-être mieux dépensé au RÉGLAGE DE PISTE, qui lui est
mesuré contre la vraie cible. Le rendu d'un projet entier coûte une quinzaine
de secondes ; vingt-neuf patchs d'usine à juger coûtent donc ~435 s, soit
l'ordre de grandeur de la recherche qu'ils remplaceraient.

~~Ce n'est **pas** un résultat, c'est une hypothèse chiffrée. Elle se tranche
par un A/B [...] Tant que cet A/B n'a pas tourné, la chaîne ne change pas.~~

**L'A/B A TOURNÉ LE JOUR MÊME, SUR LES TROIS MORCEAUX, ET IL EST UNANIME
(31/08/2026).** Mêmes stems, même métrique, même budget, une seule variable
(`--sans-recherche`) :

| morceau | avec recherche | sans | verdict |
|---|---|---|---|
| *B4 Wuz Then* | 0,2521 | 0,2521 | **identique à la 6e décimale**, décision par décision |
| *Sky and Sand* | 0,2467 | 0,2467 | **identique à la 6e décimale**, décision par décision |
| *Knight of the Jaguar* | 0,2913 | **0,2853** | **meilleur de 2,1 %** |

**LE CAS DE JAGUAR EST CELUI QUI INSTRUIT.** C'était l'un des deux morceaux sur
huit où le patch cherché GAGNAIT l'arbitrage (`vsm.string`, D=0,188 contre
0,370 à l'usine) — le cas où sauter la recherche devait coûter. Il a rapporté :
sans les patchs cherchés dans le vivier, les machines SUIVANTES remises au
mélange changent, et le mélange préfère les nouvelles (`vsm.phasedist` 0,2817
contre 0,2883 ; l'arbitrage direct 0,2739 contre 0,2786). La recherche ne
faisait pas qu'apporter peu : **ses patchs occupaient des places de suivantes
que de meilleures candidates d'usine auraient prises.** Gagner le stem pour
perdre le mélange, une fois de plus — c'est le mandataire du § 5 septies, au
carré.

**LE TEMPS, DIT AVEC SA RÉSERVE.** Les moitiés « sans » ont coûté 3 306 à
4 500 s par morceau, en tournant à TROIS courses simultanées ; les références
« avec » ont été mesurées sous des charges diverses (jusqu'à la contention
sévère de ce jour-là) et leurs totaux ne sont pas des étalons propres. Ce qui
se dit sans réserve : la recherche retirée coûtait 200 à 900 s par stem, plus
ses patchs à rendre à l'arbitrage — sur *Jaguar*, chaîne contre chaîne dans des
conditions comparables, 12 770 s sont devenues 3 619 s.

**DÉCISION : « SANS RECHERCHE » EST LE DÉFAUT DE LA CHAÎNE.** L'ancienne
chaîne reste accessible en entier par `--avec-recherche`, conservée comme
témoin d'A/B — c'est la même politique que `--machines-au-melange 0` et
`--sans-apprentissage` : le témoin est du même code que ce qu'il témoigne.
`--sans-arbitrage` implique la recherche (sans arbitrage, elle seule choisit
une machine), et le dit. Le rapport porte `rechercheNotes` en provenance.

**CE QUE LA RECHERCHE RESTE SEULE À SAVOIR FAIRE, ET QUI N'EST PAS PERDU.**
Sa distance note à note alimentait le CLASSEMENT préalable des machines — la
présélection (`--finalistes`) et l'avis consigné du classifieur s'y adossaient.
Le défaut actuel s'en passe parce que l'arbitrage de piste juge TOUTES les
candidates d'usine au budget réel ; si un jour le parc devient trop grand pour
cela, c'est un dégrossissage qu'il faudra réinventer — pas la recherche de
patch qu'il faudra regretter.


## 5 duodecies. Quatre hypothèses chiffrées, écrites avant leurs mesures

La règle du § 5 undecies vaut pour la suite : une hypothèse s'écrit AVANT
l'A/B qui la tranche, avec son critère de succès, pour ne pas être tordue
après. En voici quatre, par ordre de rendement attendu. Aucune n'est un
résultat.

**H1 — le réglage final se juge au MÉLANGE, pas au stem.** Le fait qui la
motive est le plus répété du § 5 : le réglage gagne au stem et perd au mélange
quatre fois sur quatre, et le verdict garde souvent « avant réglage ». La
proposition : après le verdict du mélange, une passe courte de réglage de la
GAGNANTE dont l'objectif est la distance du mélange — 20 à 30 évaluations à
~15 s (rendu de projet) au lieu de 120 à ~5 s (rendu de piste), budget total
comparable. Témoin : la chaîne actuelle, par option. Succès : distance globale
≤ sur les trois morceaux étalons, aucune décision de verdict dégradée.

**H2 — un cache de rendus sur disque.** Entre deux exécutions comparées, les
candidates d'usine sont RENDUES À L'IDENTIQUE (moteur déterministe, graine
fixe) et repayées à chaque fois — et le fan-out des profils vient de porter
`vsm.multisample` seul à trente et une candidates par stem. Clé proposée :
(machine, profil, patch, empreinte des notes, durée, fréquence). Succès :
verdicts STRICTEMENT identiques avec et sans cache, et une reprise
d'arbitrage à chaud qui coûte ~0 s de rendu.

**H3 — un pool de moteurs de rendu.** L'arbitrage rend ses candidates en
SÉRIE dans un seul `vsm-render --serve` ; elles sont indépendantes. Trois ou
quatre serveurs en parallèle. Succès : verdicts identiques au bit près, étape
d'arbitrage divisée par ≥ 2,5. (La leçon de la contention du 31/08 borne le
pool : pas plus de cœurs qu'il n'y en a de libres.)

**H4 — des stems `htdemucs_ft`.** La qualité des stems borne toute la chaîne
(§ 7 : le fossé de domaine, les fuites). Le modèle fin coûte ~4x la
séparation — soit ~2 min sur l'iGPU, qui ne fait rien d'autre. Succès :
chaîne complète identique par ailleurs, distance globale meilleure sur au
moins deux des trois morceaux étalons ; les stems vivent dans un dossier
séparé et le rapport dit le modèle.

**H5 — le verdict du mélange se stabilise en point fixe.** Le fait qui la
motive est sorti du fan-out des profils : le verdict est GLOUTON, piste par
piste dans un ordre fixe, et chaque décision fait le contexte des suivantes —
sur *Us and Them*, deux viviers de candidates différents ont mené à deux
trajectoires dont la moins bonne au global contenait pourtant les meilleures
pistes au stem. La proposition : rejouer la passe de verdict jusqu'à ce
qu'aucune piste ne change de décision (point fixe), borné à deux ou trois
tours — chaque tour coûte ~un rendu de projet par piste à départager. Témoin :
un seul tour, l'actuel, par option. Succès : distance globale ≤ sur les trois
morceaux étalons, et AU MOINS un cas mesuré où le second tour change une
décision — sans quoi le point fixe est atteint d'office et l'hypothèse est
close à zéro coût.

L'ordre d'exécution proposé : H2 et H3 d'abord (elles accélèrent TOUTES les
mesures suivantes, H1 et H4 comprises), puis H1, puis H5, puis H4.

**H2 ET H3, PREMIÈRE FORME : LE CRITÈRE DE VITESSE N'EST PAS ATTEINT, ET LES
CHIFFRES DISENT POURQUOI (01/09/2026).** Rejouées sur *Us and Them* à chaîne
complète : verdicts identiques à la SIXIÈME décimale (0,270791 des deux
côtés) — le déterminisme du pool tient — mais arbitrages ×1,3 à 1,6 seulement
(277→272 s, 450→359 s, avec 60 candidates au lieu de 47), loin du ÷2,5 écrit.
Cause : seule la partie RENDU était parallélisée ; la DISTANCE (~3,7 s par
candidate) restait en série dans le fil principal, et Amdahl fait le reste.
Et le cache d'audio a coûté **9,4 Go pour un seul morceau** — 83 Mo par
candidate de huit minutes.

**DEUXIÈME FORME, CELLE QUI RESTE.** L'évaluation ENTIÈRE d'une candidate —
rendu, niveau, distance — part dans les travailleurs (la cible est décrite une
fois avant le bassin) ; et le cache ne stocke plus l'audio mais LES DEUX
NOMBRES que l'arbitrage consomme : niveau efficace et distance, clé scellée
par l'empreinte du moteur ET celle de la cible — quelques octets par
candidate, et un hit économise le rendu ET la distance. Le test d'intégration
tient la forme neuve : pool == série aux mêmes distances, et une reprise à
chaud rend le même classement avec la fonction de rendu SABOTÉE. Le gain de
bout en bout se remesure à la prochaine course.

**ET CETTE DEUXIÈME FORME PORTAIT UNE COLLISION DE THREADS, TROUVÉE PAR LA
CAMPAGNE DU 01/09/2026.** Les dossiers de rendu étaient partagés entre
candidates par `indice % pool`, en supposant qu'un thread du bassin garde sa
classe modulo — ce que `ThreadPoolExecutor.map` ne promet pas. Dès qu'une
machine rapide en double une lente, deux candidates écrivent au même endroit :
l'une efface le `rendu.wav` que l'autre va lire — c'est le plantage qui a fait
tomber *Sky and Sand* à la candidate 0, deux fois, reproductible — ou écrase
son `project.json` avant que le moteur ne l'ouvre, et la mesure de l'une
devient celle de l'autre, SILENCIEUSEMENT, cache compris. Le test « pool ==
série » n'y voyait rien : ses rendus courts finissent en cadence, sans
doublement. Correctif : un dossier PAR candidate, effacé sitôt la mesure
prise — la collision devient impossible par construction, l'empreinte disque
simultanée reste celle du bassin. Le cache des mesures a été VIDÉ (4,3 Mo,
entrées possiblement empoisonnées), et les courses du matin (usandthem-v4 et
ses témoins : 0,1907 / 0,1953 / 0,1907 / 0,1865) sont REJOUÉES en v5 : leurs
distances finales étaient réelles — mesurées sur le rendu du projet écrit —
mais leurs arbitrages ont pu élire sur des mesures échangées.

**LA CAMPAGNE DU 01/09/2026 A RENDU TOUS SES VERDICTS.** Seize courses sur
code sain (la collision réparée) : pour chaque morceau, la chaîne du jour
(vivier 141 profils, H1 et H5 actifs), son témoin H1, son témoin H5, et la
même chaîne sur stems `htdemucs_ft`. Mêmes budgets partout
(`--budget-piste 120 --axes-piste 21`), verdicts et distances au rapport.

| morceau | référence (chaîne d'hier) | chaîne du jour | témoin H1 | témoin H5 | stems ft |
|---|---|---|---|---|---|
| Us and Them | 0,2708 | **0,1907** (−29,6 %) | 0,1953 | 0,1907 | 0,1865 |
| Sky and Sand | 0,2467 | **0,2307** (−6,5 %) | 0,2479 | 0,2307 | 0,2345 |
| Jaguar | 0,2853 | **0,2117** (−25,8 %) | 0,2167 | 0,2117 | **0,1836** |
| B4 Wuz Then | 0,2521 | **0,2167** (−14,0 %) | 0,2492 | 0,2167 | 0,2286 |

- **H1 CONFIRMÉE, quatre sur quatre.** Le réglage jugé au mélange gagne
  partout — −2,4 % (usandthem), −6,9 % (sky), −2,3 % (jaguar), −13,0 %
  (b4) — sans changer aucune décision de verdict (mêmes machines retenues
  course et témoin). Le fait le plus répété du § 5 est refermé : la
  dernière passe se juge au morceau, et ça se chiffre.
- **H5 TIENT SON CRITÈRE À LA LETTRE, ET C'EST TOUT.** Les témoins à un
  tour sont identiques au dix-millième sur les quatre morceaux — le point
  fixe arrive d'office au tour 2. Le cas exigé existe (usandthem sur stems
  fins : trois tours, le tour 2 change la décision de la BASSE), donc
  l'hypothèse n'est pas close à zéro coût ; mais son gain mesuré est NUL
  sur ces huit courses, pour ~2 à 6 min de tour de vérification. Le défaut
  reste à 3 tours : le mécanisme a agi une fois, son témoin ne perd rien,
  et son coût est borné.
- **H4 REJETÉE, un étalon sur trois.** Les stems `htdemucs_ft` gagnent
  fort sur Jaguar (−13,3 %) mais perdent sur Sky (+1,6 %) et B4 (+5,5 %) ;
  le critère exigeait deux sur trois. (Hors étalons, usandthem y gagnait
  −2,2 % et l'arbitrage batterie y CHANGEAIT de machine — la TR-808.)
  `htdemucs` reste le modèle par défaut ; les stems fins restent
  disponibles par dossier séparé, modèle dit au rapport. La question
  qu'HÉRITE H6 : des stems meilleurs au banc public ne donnent pas un
  morceau plus proche deux fois sur trois — comprendre pourquoi AVANT de
  payer un cran de SDR de plus (la piste : la chaîne juge la
  reconstruction contre le MÉLANGE d'origine, et des stems plus propres
  déplacent la cible de l'arbitrage sans déplacer celle du verdict).
- **La performance de bout en bout, enfin chiffrée.** Us and Them à cache
  froid : 59,7 min pour ~170 candidates par stem mélodique, là où la
  chaîne d'hier mettait 53 min pour 60 candidates — le vivier TRIPLE pour
  +13 % de temps (la deuxième forme H2/H3 fait exactement ce qu'elle
  promettait). Témoins à cache chaud : 37,5 et 44,6 min, arbitrages à
  ~5 s par stem. Le bassin est passé à 8 rendus de front à mi-campagne
  (jaguar et b4) — les verdicts sont invariants par construction (test
  pool == série), aucun A/B de temps n'a donc été sacrifié.
- **Et les gains de la journée s'additionnent** : chaîne d'hier → chaîne
  du jour, −6,5 % à −29,6 % selon le morceau, moyenne −19 % — le vivier
  GM complet, H1, et le point fixe H5 pour ce qu'il garde.

**UNE DEUXIÈME VARIABLE S'ÉTAIT GLISSÉE DANS LA COMPARAISON DE TÊTE, ET
C'EST LA PROVENANCE QUI L'A DÉNONCÉE.** Les références (v3, v9, v6…)
couraient AVEC le classifieur de frappes (`--classifieur-batterie`,
provenance : `2026-08-23`) ; la campagne courait SANS (provenance :
`aucun`) — l'option ne se charge que si on la passe, et le script de
campagne ne la passait pas. C'est ce qui expliquait l'énigme de la découpe
batterie (5 pièces/2319 frappes hier, 3/2282 aujourd'hui, reproductible :
la campagne retombait sur la découpe par bandes). Conséquences dites :
les verdicts H1/H4/H5 tiennent — leurs témoins partagent le même
`aucun` — mais la ligne « chaîne d'hier → chaîne du jour » compare aussi
ce choix de modèle, et les batteries de la campagne sont celles du repli
par bandes, pas du classifieur mesuré meilleur au banc (A2 : charleston
16/16 au lieu de 8/16). La prochaine course le repasse explicitement, et
la leçon vaut d'être écrite : LIRE la provenance des deux rapports avant
de les comparer — elle est faite pour ça, et elle disait la vérité.

*Ce qui suit décrit la première implémentation, conservée pour l'histoire de
la décision :*

**H2 ET H3 SONT IMPLÉMENTÉES ET TENUES PAR UN TEST (01/09/2026)** —
`--rendus-paralleles` (défaut 3) et le cache `cache/rendus` (clé : machine,
profil, patch, notes, durée, fréquence, tempo, EMPREINTE DU MOTEUR ; témoin
`--sans-cache-rendus` ; les deux en provenance). Le test d'intégration mesure
leurs critères : pool == série aux mêmes machines et distances, et une
reprise à chaud rend le même classement avec la fonction de rendu SABOTÉE —
seules des lectures de cache peuvent produire ce tableau. (Premier jet du
test instructif : casser le BINAIRE ne prouve rien, l'empreinte moteur change
la clé et le cache refuse à bon droit.) Le gain de bout en bout reste à
chiffrer sur un morceau ; les verdicts, eux, sont déjà prouvés identiques.

**ET UNE MESURE IMPRÉVUE S'INTERCALE : LE FAN-OUT DES PROFILS (§ 11 du CDC
machines) A RENDU SES CHIFFRES.** Stems : −35 % sur `other` d'*Us and Them*
(0,350 → 0,227, guitare nylon). Morceau : **+2,7 %** (0,2638 → 0,2708). Un
timbre plus vrai colle mieux au stem fuites comprises — la basse gagnée par
un chœur, qui épouse la fuite vocale du stem — et le trajet glouton du
verdict finit ailleurs. C'est le plus fort argument mesuré EN FAVEUR de H1 et
d'une passe de stabilisation du verdict : détail au CDC § 11.

**H5 EST IMPLÉMENTÉE ET TENUE PAR UN TEST (01/09/2026), SA MESURE RESTE À
FAIRE.** `--tours-verdict` (défaut 3) rejoue la passe du verdict jusqu'à ce
qu'un tour ne change ni machine, ni patch, ni profil d'aucune piste — ce
tour-là est le point fixe et n'est pas payé deux fois ; le témoin de l'A/B est
`--tours-verdict 1`, l'ancien comportement. Le nombre de tours joués et ce que
chaque tour a changé sont PUBLIÉS (journal, `mixVerdict`, provenance) : un
point fixe atteint d'office est une information, pas une absence
d'information. La boucle vit dans `settle_verdict`
(`analyse/analyzer/vsm_mix_verdict.py`) et son test verrouille les trois
conduites : le point fixe arrête, la borne s'impose, le témoin ne joue qu'un
tour. Les critères chiffrés du § ci-dessus (globale ≤ sur les étalons, au
moins une décision changée par un second tour) se tranchent à la prochaine
course.

**LE PARC ÉLARGI NE COÛTE PAS : IL GAGNE (02/09/2026), ET C'EST H5 QUI
ENCAISSE.** La règle écrite au CDC machines avant la mesure — « une machine
ajoutée pour le jeu n'a rien à promettre, mais ne doit rien coûter » — est
non seulement tenue, elle est dépassée :

| course | machines | verdict | basse retenue | globale |
|---|---|---|---|---|
| v7 | 37 | 2 tours | `vsm.vocal` (D stem 0,1851) | 0,1910 |
| **v9** | **39** | **3 tours** | **`vsm.cs80`** (D stem **0,1851**) | **0,1822** |

- **`vsm.cs80` gagne une piste dès sa première course**, et la gagne au
  MÉLANGE : sa distance au stem est identique à celle de `vsm.vocal` au
  dix-millième près (0,1851 = 0,1851). Le stem ne les départage pas ; le
  morceau, oui, et de 4,6 %. C'est la leçon du mandataire (§ 5 septies)
  prise par le bon bout, pour une fois : la machine que le stem déclarait
  ex æquo est celle qui rapproche le morceau.
- **ET C'EST LE TOUR 2 DU POINT FIXE QUI L'A TROUVÉE.** Le verdict a pris
  trois tours (`tour 1 : other, Batterie ; tour 2 : bass ; tour 3 : rien`) :
  au premier passage, la basse gardait son choix ; c'est une fois les deux
  autres pistes arrêtées que `vsm.cs80` est devenue le meilleur choix pour
  elle. H5, gardée jusqu'ici « pour un coût borné et un gain nul mesuré »,
  vient de rendre son premier gain GLOBAL — et le second cas où son tour 2
  change une décision (le premier était `usandthem-v5-ft`).
- **`vsm.modal` confirme son classement au mélange** : écartée à 0,2739,
  mais devant `vsm.string` (0,2829) — au stem comme au morceau. Une machine
  ajoutée pour le jeu qui se place devant une machine de couverture sur un
  stem réel n'est pas décorative.

**H9 — le gain vient du POINT FIXE, pas du seul vivier ; écrite avant sa
mesure.** Les deux explications sont enchevêtrées : `vsm.cs80` est entrée
au tour 2, donc sans tour 2 elle ne serait pas entrée. Le témoin qui
sépare : **v10 = v9 avec `--tours-verdict 1`**. Succès : v10 ≈ v7
(0,1910), c'est-à-dire que le vivier seul ne suffit pas et que le point
fixe porte tout le gain. Échec : v10 ≈ v9, et c'est le vivier qui gagne —
auquel cas H5 reste sans gain propre, et il faudra le dire.

**H9 EST TRANCHÉE : LE POINT FIXE PORTE LE GAIN (02/09/2026).** Le témoin
v10 — v9 en tout point, `--tours-verdict 1` pour seule différence — rend
**0,2013 contre 0,1822**. Le point fixe du verdict vaut donc **−9,5 %** sur
*Us and Them*, à vivier, stems, métrique et budget identiques. C'est la
première fois que H5 rend un gain propre et chiffré : jusqu'ici elle était
gardée « pour un coût borné et un gain nul mesuré », et ce n'est plus vrai.

Ce que la mesure dit exactement, et il faut le dire dans cet ordre : ce
n'est pas le vivier qui gagne, c'est **le droit de revenir sur un verdict**.
Les deux machines ajoutées ne servent à rien tant que l'arbitrage ne
repasse pas : `vsm.cs80` n'entre qu'au tour 2, une fois les deux autres
pistes arrêtées, parce que c'est seulement à ce moment-là que le MÉLANGE
peut la préférer à sa rivale — et le mélange la préfère alors qu'elle est
**deux fois plus loin au stem** (0,3623 contre 0,1851). Un vivier plus large
donne des candidates ; seul le point fixe les fait choisir.

*(Ce passage disait d'abord « les deux sont à égalité au dix-millième
(0,1851 contre 0,1851) ». Le chiffre venait d'un champ de `rapport.json` qui
n'était pas recalculé après substitution de machine : les deux valeurs
étaient le MÊME nombre, celui de la machine écartée. Corrigé le 02/09/2026 ;
la mesure réelle rend la conclusion plus forte, pas plus faible.)*

**Ce que ce témoin NE dit PAS, et qui reste ouvert.** v10 (0,2013) est plus
loin que v7 (0,1910), et il serait tentant d'en conclure qu'un vivier
élargi éloigne le morceau quand le verdict ne repasse pas. **Cette lecture
est interdite** : v7 et v10 diffèrent par DEUX variables (37 machines et
trois tours contre 39 machines et un tour), et le § 5 undecies l'exclut.
Le témoin qui trancherait est **v11 = 37 machines, `--tours-verdict 1`** :
si v11 ≈ 0,2013, le vivier n'y est pour rien et c'est le tour unique qui
coûte ; si v11 ≈ 0,1910, alors le vivier élargi coûte VRAIMENT quand le
verdict ne repasse pas, et la règle du § 7 du CDC machines-manquantes
devra porter cette réserve : *une machine de plus ne coûte rien à
condition que le verdict ait le droit d'y revenir.* Tant que v11 n'a pas
couru, cette phrase reste une hypothèse et non un acquis.

**v13 APPORTE UNE PIÈCE AU DOSSIER DE H13 (02/09/2026).** La course v13 (41
candidates) rend **exactement** la distance de v12 (36 candidates) —
0,21123303926053802 dans les deux cas, à dix-sept décimales. Cinq machines de
plus n'ont rien déplacé, parce qu'aucune n'est entrée dans les trois
finalistes remises en jeu au mélange.

Cela ne tranche pas H13, mais cela en resserre l'énoncé : le vivier n'est pas
coûteux **par sa taille**, il l'est par ce qui franchit le goulot. Un vivier
de mille machines dont aucune n'atteint le top 3 coûterait du TEMPS et rien
d'autre. La question devient donc exactement celle de H13 : le classement au
stem, qui décide qui franchit, est-il assez fiable pour ce tri — sachant qu'il
peut se tromper d'un facteur deux (mesuré sur `vsm.cs80`) ?

## § 5 quaterdecies. LE PLAFOND DE QUATRE PISTES — un défaut de conception, signalé par l'utilisateur (02/09/2026)

**LE CONSTAT, ET IL NE VIENT PAS D'UNE MESURE MAIS D'UNE ÉCOUTE.** « Les
originaux contiennent bien plus que 4 pistes, or notre analyse n'en fait jamais
plus de 4. » C'est exact, et aucun des chiffres que cette feuille accumule
depuis onze versions ne le disait — parce qu'aucun ne le regardait.

### Les nombres, mesurés sur *Us and Them* le 02/09/2026

| Stem | Part de l'énergie | Notes | Polyphonie moyenne | Max | Ambitus | Machine retenue |
|---|---|---|---|---|---|---|
| `other` | **62,1 %** | 4 642 | **4,83** | 11 | **66 demi-tons** | `vsm.tb303` |
| `vocals` | 20,8 % | — | — | — | — | sampler |
| `drums` | 13,0 % | 2 282 | 0,24 | 2 | 6 | `vsm.drums` |
| `bass` | 4,1 % | 481 | 0,50 | 3 | 47 | `vsm.vocal` |

Autrement dit : **les deux tiers du morceau sont sur une seule piste, jouée par
une seule machine, qui doit tenir cinq notes simultanées sur cinq octaves et
demie.**

*(Deux chiffres circulent pour la part de `other`, et les deux sont justes :
**62,1 %** en sommant l'énergie des deux canaux stéréo — la mesure faite à la
main le jour du constat — et **57,7 %** après pli mono `(G+D)/2`, qui est ce
que `partage_du_morceau` publie désormais au rapport. L'écart vient de la
largeur stéréo : les composantes hors phase de `other` (nappes, réverbération)
s'annulent partiellement au pli, celles des voix centrées non. Le chiffre du
rapport est le pli mono, parce que c'est le signal que la chaîne traite
réellement ; la conclusion est la même des deux côtés du seuil de 50 %.)* Sur ce morceau-là, cette machine est un TB-303 — un synthétiseur
monophonique de ligne de basse acide. Le piano électrique, l'orgue, le
saxophone et les guitares de Pink Floyd passent tous par lui.

### Pourquoi ce n'était pas visible

La chaîne rend **une piste par stem**, et `htdemucs` rend quatre stems : `bass`,
`drums`, `other`, `vocals`. Le quatre n'est écrit nulle part dans notre code —
`reconstruire_les_stems` itère sur ce qu'on lui donne et ne traite spécialement
que `vocals` et `drums` — il est **hérité du modèle de séparation**, et il n'a
jamais été interrogé.

Surtout : **la distance globale ne pouvait pas le voir.** Quatre instruments
fondus en un sonnent « à peu près » ; la métrique compare des spectres, et un
spectre additionné ressemble à un spectre additionné. Une version qui
séparerait mieux pourrait même mesurer PLUS LOIN tout en rendant un projet
enfin retravaillable. Onze versions ont donc optimisé un nombre qui ne
regardait pas la chose la plus importante.

C'est une panne muette d'un genre nouveau : elle ne porte pas sur ce que la
chaîne mesure, mais sur **ce qu'elle produit**.

### Ce qui est fait tout de suite : la rendre visible

Trois champs par stem dans `rapport.json` — `polyphonieMoyenne` (pondérée par
le temps), `polyphonieMax`, `ambitusDemiTons` — et une plainte au journal quand
un stem dépasse **3 notes simultanées en moyenne ET 3 octaves** à la fois. Les
deux seuils sont exigés ensemble et non l'un ou l'autre : un accord de piano
est dense sans être un fourre-tout, un solo est large sans l'être non plus. Ce
qu'on cherche à nommer, c'est « plusieurs instruments additionnés ».

Cela ne corrige rien. Cela empêche que le défaut se reproduise sans témoin, et
c'est la première chose que ce dépôt exige. Cinq tests
(`analyse/tests/test_densite_stem.py`), vérifiés contre une moyenne non
pondérée par le temps — laquelle rend 2,5 là où la vraie valeur est 5.

### H22 — plus de stems à la séparation ; écrite AVANT sa mesure

**`htdemucs_6s` sépare six sources au lieu de quatre** : il ajoute `guitar` et
`piano`. L'option existe déjà (`--modele`), donc le témoin est une option en
ligne de commande et non une constante éditée, comme le § « Mesure » l'exige.

*Ce que j'attends, écrit d'avance* : le nombre de pistes passe de 4 à 6, et
`other` perd de sa densité — c'est le point. Sur la **distance**, je m'attends à
un résultat **neutre ou légèrement défavorable**, pour deux raisons : les
modèles à six sources sont réputés un peu moins bons sur les quatre sources
communes, et deux pistes de plus font deux arbitrages de plus, donc deux
occasions de choisir mal. **Si la distance se dégrade, cela ne condamne pas
H22** : il faudra alors dire clairement que la chaîne arbitre entre deux
qualités — la ressemblance et la jouabilité — et que la seconde n'a jamais été
mesurée jusqu'ici. C'est cette phrase-là, et non le chiffre, qui manquait.

*Le témoin* : même morceau, même budget, même métrique, un seul changement —
le jeu de stems (htdemucs contre htdemucs_6s).

**LA PREMIÈRE FORME DU TÉMOIN EST MORTE DEUX FOIS, ET LA CAUSE EST LA
MÉMOIRE (02/09/2026).** Le dispositif initial faisait re-séparer chaque moitié
dans le processus de la chaîne (« mêmes stems interdits, il faut re-séparer »),
pour que les deux moitiés soient traitées à l'identique. La machine a 15 Go :
torch et demucs restent résidents (~7 Go) pendant que Basic Pitch charge son
propre modèle, et l'OOM killer a abattu H22a à 15:28 puis H22b à 15:30 —
code 137, la trace est au journal du noyau. Pire : la file d'attente
enchaînait après le cadavre, et H22b est partie pendant que H22a gisait ;
la file s'arrête désormais au premier échec.

Le dispositif corrigé sépare À PART — un processus qui ne fait que demucs,
écrit ses stems, meurt — puis les deux moitiés repartent de `--stems`. Elles
restent traitées à l'identique (aucune ne sépare), la variable reste unique
(le dossier de stems, donc le modèle), et la provenance porte le dossier.

Ce qui permet de réutiliser les stems déjà en place pour la moitié témoin :
avant de mourir, la première H22a avait séparé et mesuré le partage —
57,7/22,7/15,0/4,6, **exactement** celui des stems stockés. `shifts=0` rend
demucs déterministe, et cette égalité l'encaisse : `usandthem/` EST une
séparation htdemucs du jour.

**H22 EST TRANCHÉE, ET MON ATTENDU AVAIT TORT DANS LE BON SENS (02/09/2026).**
J'avais écrit « distance neutre ou légèrement défavorable ». Mesuré, contre le
témoin H22a-v2 (mêmes 48 candidates, mêmes 3 tours, mêmes budgets, même
binaire, seule variable le jeu de stems) :

| Course | Stems | Pistes | Distance |
|---|---|---|---|
| H22a-v2 | htdemucs (4) | 4 | 0,191036 |
| H22b-v2 | htdemucs_6s (6) | 6 | **0,171142** |

**Six stems rapportent −10,4 % ET deux pistes de plus.** La crainte des
modèles à six sources (« un peu moins bons sur les quatre sources communes »)
ne s'est pas matérialisée sur ce morceau : donner à `guitar` et `piano` leur
piste — donc leur machine, leur arbitrage, leur réglage — vaut mieux que de
les fondre dans un `other` que le TB-303 approximait. Le § 0 du CDC
multipiste n'a même pas à trancher : structure ET distance vont du même côté.

Trois observations à côté du chiffre : le verdict à six pistes n'atteint pas
le point fixe en trois tours (`bass` et `piano` changent d'avis à chaque
tour — la borne est dite au journal) ; le réglage au mélange rapporte encore
−0,009 après le verdict ; et `guitar` (polyphonie 3,4, ambitus 74) comme
`other` résiduel (3,8, 71) restent des FOURRE-TOUT au sens du seuil — la
séparation à six sources réduit le problème, elle ne le ferme pas. C'est
exactement l'espace de H23.

### H23 — diviser un stem polyphonique en VOIX ; écrite AVANT sa mesure

Six stems ne suffiront pas : `other` restera un fourre-tout, plus maigre. La
question suivante est de savoir si l'on peut **découper une piste polyphonique
en plusieurs voix jouables**.

*Ce qu'il ne faut PAS faire, et la raison est physique* : découper l'AUDIO par
bandes de fréquence. Une note de saxophone a son fondamental dans une bande et
ses harmoniques dans les suivantes ; un découpage spectral ne sépare pas des
instruments, il ampute des timbres. Ce serait une séparation en apparence et un
massacre en fait.

*Ce qui se tient* : découper les NOTES déjà transcrites en voix, et donner à
chaque voix sa propre piste, chacune arbitrée sur le même stem. Le gain visé
n'est pas la distance : c'est **un projet qu'on peut retravailler**, où la
nappe et la ligne mélodique ne sont plus la même piste.

**LE MÉCANISME A ÉTÉ CHOISI PAR LA MESURE (02/09/2026), et le pressenti a
perdu.** Trois candidats, essayés à sec sur les 4 642 vraies notes du stem
`other` de v14 (polyphonie 4,83, ambitus 66 demi-tons) :

| Candidat | Voix (max=4) | Ambitus par voix | Verdict |
|---|---|---|---|
| Continuité de hauteur (le pressenti ci-dessus) | 4 × ~1 160 notes | **65-66 chacune** | des parts de gâteau, pas des parties |
| Continuité à ancre de registre (moyenne glissante, inertie 0,9) | 4 | 47-62 (médianes 66/57/50/50) | mieux, pas assez |
| **Partage par REGISTRES** (k-moyennes 1-D sur la hauteur, pondérées par la durée, init aux quantiles) | 4 | **28 / 9 / 9 / 16**, intervalles DISJOINTS (69-97, 59-68, 48-57, 31-47), polyphonie ≤ 1,7 | **retenu** |

Sur un nuage dense, toute affectation note à note perd son registre ; le
partage par registres garantit PAR CONSTRUCTION des intervalles de hauteur
disjoints — l'aigu, les médiums, la basse-nappe : des parties nommables. Et le
garde-fou vit dans la fonction même : **ce qui n'est pas un fourre-tout ne se
découpe jamais** — une mélodie qui saute d'octave, un accompagnement d'accords
serrés restent UNE piste, parce que les découper fabriquerait de fausses
parties, pires que le mal soigné. Six tests
(`analyse/tests/test_separation_voix.py`) fixent ce contrat ; l'option est
`--voix-par-stem N`, dans la provenance comme tout ce qui change le résultat.

*(Le premier jet portait aussi un repli des « voix squelettiques » après
partage ; aucun cas construit n'a su le déclencher — l'initialisation aux
quantiles pondérés ne donne jamais un centre à une poignée de notes — et il a
été retiré plutôt que de garder un filet que rien ne peut toucher.)*

*Ce que j'attends, écrit d'avance* : aucun gain de distance, et peut-être une
légère perte (N machines valent N fois plus d'occasions de se tromper). Le
critère de succès de H23 n'est donc **pas** la distance : c'est que chaque
piste produite passe sous le seuil du fourre-tout — moins de 3 notes
simultanées en moyenne ou moins de 3 octaves. Si la distance se dégrade de plus
de 5 %, le compromis se dit et se laisse à l'utilisateur par une option, il ne
se décide pas ici.

**H23 EST TRANCHÉE : LE CRITÈRE STRUCTUREL EST TENU, LE PRIX EST DIT
(03/09/2026).** Contre le même témoin H22a-v2, seule variable le découpage :

| Course | Pistes | Distance | Fourre-tout restants |
|---|---|---|---|
| H22a-v2 (témoin) | 4 | 0,191036 | other (poly 4,83 · 66 demi-tons) |
| H23 (`--voix-par-stem 4`) | **7** | 0,208457 (+9,1 %) | **aucun** |

Le succès promis est là : les quatre voix passent sous le seuil du
fourre-tout (polyphonie 0,64 / 1,68 / 1,36 / 1,14 sur des ambitus de 28 / 9 /
9 / 16 demi-tons), et l'arbitrage donne à chaque registre SA machine —
`vsm.divider` à l'aigu, `vsm.tb303`, deux `vsm.multisample`. Le verdict à
sept pistes atteint son point fixe en deux tours. Le déterminisme du
découpage s'est vérifié en vrai : la reprise après l'OOM a rendu les mêmes
quatre voix au bit près.

Le prix aussi est là : +9,1 % de distance, au-delà des 5 % que la règle
écrite d'avance fixait — le découpage reste donc une OPTION et ne devient pas
le défaut. La cause est visible au journal : chaque voix est jugée SEULE
contre le stem ENTIER, et la chaîne a signalé deux fois que le morceau serait
meilleur sans la voix 1 (l'aigu isolé ressemble peu au tout). Si le découpage
doit un jour gagner en distance, c'est cette cible-là qu'il faudra repenser —
juger les voix ENSEMBLE, pas chacune contre le tout.

**Ordre : H22 d'abord**, parce qu'elle est une option déjà câblée et qu'elle
tranche la question « une vraie séparation supplémentaire aide-t-elle ? » avant
qu'on écrive quoi que ce soit. H23 ensuite — son mécanisme est écrit et testé,
sa course est en file (`course-h23.sh`) derrière la paire H22, avec **H22a pour
témoin commun** : même modèle, même budget, même moteur, une seule variable
chacune (le modèle pour H22b, le découpage pour H23). Les courses attendent la
fin du témoin v11 : deux reconstructions de front mettent la machine au
surplace.

---

**LE MOTEUR N'ÉTAIT PAS DANS LA PROVENANCE, ET DEUX COURSES L'ONT PAYÉ
(02/09/2026).** En préparant v11 il a fallu établir le vivier de v10 et celui
de v7. C'est en les comptant qu'un écart est apparu : au commit de v13
(`fe39a4d`) le dépôt porte **quarante-sept** machines mélodiques, et la course
v13 en annonce **quarante et une**. La raison tient en deux horodatages :
`build/tools/vsm-render` avait été compilé à **08:48**, v13 s'est terminée à
**10:12** et v14 à **11:05**, et sept machines ont été écrites entre 09:13 et
10:17. **Les deux courses ont donc tourné avec un moteur qui ignorait sept
machines de leur propre commit, et rien ne le disait.**

*Ce que cela n'invalide pas* : le verdict de **H13**. v13 et v14 partagent ce
même binaire ; la seule variable entre elles reste `machinesAuMelange` (3 puis
6), et le gain de −9,6 % tient. *Ce que cela invalide* : le NOMBRE écrit à côté.
« 41 candidates » n'est pas « le vivier au commit fe39a4d », c'est « le vivier
du binaire de 08:48 ». Les deux phrases se ressemblent et une seule est vraie.

*Pourquoi cela pouvait arriver.* `rapport.json` inscrivait le commit du dépôt —
et même son `+` quand l'arbre est modifié —, mais **seulement pour `analyse/`**.
Or l'audio ne sort pas de `analyse/` : il sort d'un binaire C++ qui peut dater
de n'importe quand. Le § « Mesure » du cahier des charges veut que *toute option
qui conditionne le résultat aille dans la provenance* ; le moteur n'est pas une
option, c'est l'instrument lui-même, et il n'y était pas.

*La parade, livrée le même jour.* La provenance reçoit un bloc **`moteur`** —
chemin, date de compilation, taille, nombre de machines déclarées : quatre
champs qui suffisent à voir que deux rapports n'ont pas été rendus par le même
moteur. Et la chaîne **se plaint au démarrage** dès que le binaire est plus
vieux qu'un fichier de `audio/`, `core/` ou `interchange/`, en nommant le
fichier fautif ; un changement dans `app/` ne déclenche rien, sans quoi
l'avertissement deviendrait un bruit de fond qu'on apprend à ignorer. Cinq
tests dans `analyse/tests/test_provenance_moteur.py`, vérifiés contre une
implémentation cassée.

*La leçon, et elle était déjà écrite ailleurs.* Le § 9 de
`docs/ROADMAP-interop.md` dit depuis longtemps qu'**un binaire périmé ne se
signale pas comme périmé**. Elle y était consignée comme une consigne
d'exploitation — « penser à recompiler » —, ce qui ne suffit jamais : une
consigne qu'on doit se rappeler est une consigne qu'on oublie sous charge. Elle
devient ici une **mesure**, imprimée et publiée, et c'est la seule forme qui
tienne.

**LE TÉMOIN v11 EST DÉSORMAIS POSSIBLE (02/09/2026).** Il ne l'était pas :
réduire le vivier demandait soit de lister à la main les trente-quatre
machines qu'on garde — une liste qui ment dès qu'une machine arrive —, soit
d'éditer une constante entre deux passes, ce que le § « Mesure » du cahier
des charges interdit expressément. `reconstruire.py` reçoit donc
**`--machines-exclues`**, complément de `--machines`, et l'option va dans la
provenance de `rapport.json` comme toute option qui conditionne le résultat.
Un nom inconnu est REFUSÉ, avec le vivier affiché : une exclusion qui ne
s'applique pas rendrait le témoin identique à ce qu'il mesure, et le verdict
dirait « aucun effet » en toute bonne foi.

La paire à courir, une seule variable entre les deux — le vivier — et le
MÊME binaire pour les deux, ce qui évite de dépendre du moteur de v10 :

```
# 34 candidates, un tour  (reproduit v10 avec le moteur du jour)
--machines-exclues vsm.chebyshev,vsm.scanned --tours-verdict 1
# 32 candidates, un tour  (le vivier de v7)
--machines-exclues vsm.chebyshev,vsm.scanned,vsm.cs80,vsm.modal --tours-verdict 1
```

**CES DEUX LISTES ONT VIEILLI EN UNE JOURNÉE, et c'est instructif.** Elles
étaient justes le 02/09 au matin, quand le vivier comptait trente-six machines
mélodiques : trente-six moins deux font bien trente-quatre. Le soir même il en
compte **quarante-huit**. Une liste d'exclusions énoncée en extension est un
nombre déguisé en noms : elle dit « toutes sauf celles-ci » à un moment où l'on
sait ce que « toutes » veut dire, et elle ment dès que le parc bouge — le même
défaut, exactement, que la liste en compréhension qu'on avait refusé d'écrire.

Les listes réellement courues sont donc **recalculées** à partir des commits :
le vivier de v10 est l'ensemble des machines mélodiques d'aujourd'hui qui
existaient déjà au commit `883d5e6`, plus `vsm.chebyshev` qui y avait un dossier
sans être candidate ; celui de v7, les mêmes moins `vsm.cs80` et `vsm.modal`.
Quatorze exclusions d'un côté, seize de l'autre, et la chaîne affiche le compte
obtenu — 34 et 32 — de sorte que le témoin se vérifie lui-même. Les scripts sont
`reconstruction/travail/course-v11a.sh` et `course-v11b.sh`, lancés en série par
`paire-v11.sh`.



Si les deux rendent la même distance, le vivier élargi ne coûte rien même
sans point fixe, et la règle du § 7 du CDC machines-manquantes tient sans
réserve. Si la course à 34 est plus loin, alors les candidates
supplémentaires nuisent tant que le verdict ne repasse pas, et la règle doit
porter sa condition.

**LE TÉMOIN v11 A COURU, ET LE VIVIER GAGNE (02/09/2026).** La paire, une
seule variable — le vivier, deux machines d'écart — et le même binaire :

| Course | Vivier | Tours | Finalistes | Distance |
|---|---|---|---|---|
| v11a | celui de v10 (34 machines) | 1 | 6 | **0,191036** |
| v11b | celui de v7 (32 machines : sans `vsm.cs80` ni `vsm.modal`) | 1 | 6 | 0,196979 |

**Les deux machines de plus rapportent 3,0 %**, même quand le verdict ne
repasse pas. La règle du § 7 du CDC machines-manquantes tient donc **sans la
réserve** que ce paragraphe envisageait — au régime actuel du goulot (six
finalistes), un vivier plus large ne coûte pas, il paie.

Deux honnêtetés à poser à côté du chiffre :

1. **La paire ne rejoue pas v10 à l'identique.** Elle a couru avec
   `machinesAuMelange = 6`, le défaut depuis H13, quand v10 avait 3. La phrase
   « si v11 ≈ 0,2013, c'est le tour unique qui coûte » ne s'applique donc plus
   telle quelle. Ce qu'elle voulait démêler est démêlé autrement, et mieux :
   **v11a (34 machines, UN tour) rend 0,191036, soit le chiffre de v14 (41
   machines, TROIS tours) à la septième décimale** — 0,191036 contre 0,191036.
   Une fois le goulot élargi à six, le tour unique ne coûte plus rien et les
   sept machines suivantes ne déplacent rien : le coût que v10 montrait
   (0,2013) était bien celui du goulot étroit, pas du vivier. H13 s'en trouve
   confirmée une seconde fois, par un chemin indépendant.
2. **Le code des deux moitiés diffère** (commits `968c8bd` et `ae98283`, et la
   provenance le montre) : entre elles ont été livrés des ajouts de pure
   mesure — densité des stems, partage d'énergie, provenance du moteur, le
   mécanisme H23 derrière une option éteinte par défaut. Aucun ne touche une
   décision ni un calcul de distance ; la paire garde sa variable unique. Et
   leur bloc `moteur` porte encore le repli « je ne sais pas » : la correction
   de la capture moteur vivant est arrivée pendant leur course. H22 et H23
   porteront l'identité pleine.

**H13 — le vivier coûte parce que le GOULOT des finalistes est trop
étroit ; écrite avant sa mesure (02/09/2026).** Deux faits mesurés le même
jour la motivent, et il faut les lire ensemble :

1. **Le vivier élargi coûte** : v12 rend 0,2112 contre 0,1822 pour v9,
   +15,9 %, pour une seule variable — 36 candidates au lieu de 34.
2. **Le stem est un très mauvais prédicteur du mélange**, et bien pire qu'on
   ne le croyait : sur la basse de v9, `vsm.cs80` mesure **0,3623** au stem
   contre 0,1851 pour la machine qu'elle remplace, et elle fait pourtant un
   MEILLEUR morceau. Un facteur DEUX renversé. (Ce chiffre n'était pas connu
   avant le 02/09 : `rapport.json` publiait le score de la machine écartée,
   panne muette corrigée le même jour.)

**Le mécanisme que ces deux faits suggèrent.** Le verdict du mélange ne
rejuge que les `machinesAuMelange` premières du classement AU STEM — trois
par défaut. Si le stem peut se tromper d'un facteur deux, ce classement est
peu fiable ; et plus le vivier grandit, plus la vraie gagnante a de chances
de tomber en quatrième position ou au-delà, donc de n'être jamais essayée au
mélange. **Le vivier ne nuit pas parce qu'il contient de mauvaises machines,
il nuit parce que le goulot par lequel il passe est trop étroit pour lui.**

Si c'est vrai, la conséquence est agréable : le coût n'est pas une fatalité
de l'élargissement, c'est un réglage mal dimensionné.

**Le témoin : v14 = v13 avec `--machines-au-melange 6`**, une seule variable.

- **Succès de H13** : v14 rattrape ou dépasse v9 (≈ 0,18) là où v13 reste
  autour de 0,21. Le goulot était bien la cause, et il faut redimensionner
  `machinesAuMelange` en fonction de la TAILLE du vivier plutôt que de le
  laisser à trois quel que soit le nombre de candidates.
- **Échec de H13** : v14 ≈ v13. Alors le mal est ailleurs — probablement dans
  l'ordre d'exploration du point fixe lui-même — et il faudra chercher du
  côté de l'algorithme et non de ses paramètres.

Le coût à prévoir est connu d'avance et doit être dit : doubler le nombre de
finalistes double le nombre de rendus du verdict, donc allonge la course. Si
H13 réussit, le § 7 du CDC machines-manquantes gagne une seconde phrase :
*un vivier qui grandit demande un goulot qui grandit avec lui.*

### H13 EST TRANCHÉE : SUCCÈS — et le coût annoncé n'existe pas (02/09/2026)

**Le témoin v14 est v13 à une seule option près** (`machinesAuMelange` 3 → 6,
vérifié sur les deux provenances) :

| course | candidates | finalistes | distance |
|---|---|---|---|
| v9 | 34 | 3 | 0,1822 |
| v12 | 36 | 3 | 0,2112 |
| v13 | 41 | 3 | 0,2112 |
| **v14** | **41** | **6** | **0,1910** |

**Élargir le goulot récupère les deux tiers du coût du vivier** : −9,6 % par
rapport à v13. Le mécanisme supposé était le bon — à trente-quatre candidates
la gagnante du mélange était dans les trois premières du stem, à quarante et
une elle n'y est plus, et le goulot qui n'était pas trop étroit l'est DEVENU.
Le journal de v14 le montre en clair : sur `bass`, les finalistes vont
désormais jusqu'à `generic` à 108,9 % de la gagnante du stem, et sur `other`
jusqu'à `arpodyssey` à 98,6 % — des machines que trois places ne pouvaient pas
atteindre.

**LE COÛT ANNONCÉ N'EXISTE PAS, ET C'EST LE CONTRAIRE QUI SE PRODUIT.**
L'hypothèse prévenait honnêtement que « doubler le nombre de finalistes double
le nombre de rendus du verdict, donc allonge la course ». Mesuré, la course est
plus RAPIDE : le verdict passe de 670 à 582 secondes, et les réglages au
mélange de **1199 à 525 secondes**. Trouver la bonne machine dès le verdict
laisse beaucoup moins de chemin à parcourir au réglage qui suit, et ce gain
dépasse largement le coût des trois rendus supplémentaires. C'est une leçon
qu'aucun raisonnement n'aurait donnée : le prix d'un meilleur choix se
récupère en aval.

**Conséquence, appliquée** : `MACHINES_AU_MELANGE` passe de 3 à **6** dans
`reconstruire.py`, avec les chiffres inscrits à côté de la constante. Et le § 7
du CDC machines-manquantes gagne sa seconde phrase : *un vivier qui grandit
demande un goulot qui grandit avec lui.*

**Ce qui reste ouvert, et qu'il ne faut pas taire** : v14 (0,1910) ne rattrape
pas v9 (0,1822). Le goulot explique les deux tiers de l'écart, pas la totalité.
Le tiers restant vient d'ailleurs — peut-être de l'ordre dans lequel le point
fixe visite les pistes, peut-être d'un optimum local que le vivier élargi rend
plus probable. Un témoin v15 à neuf finalistes dirait si le reste s'obtient en
élargissant encore, ou si le mal est ailleurs.

**LE TÉMOIN v15 EST ÉCRIT ET MIS EN FILE (02/09/2026), hypothèse d'abord.**
Sa forme a changé depuis le paragraphe ci-dessus, pour garder UNE variable :
comparer v15 à v14 mêlerait deux choses, le goulot ET le moteur (v14 a couru
sur le binaire de 08:48, à 41 candidates ; le moteur du jour en donne 48). Le
témoin de v15 est donc **H22a-v2** — mêmes stems, même budget, mêmes trois
tours, même binaire, 48 candidates — et la seule variable est
`--machines-au-melange` : 6 contre 9.

*Ce que j'attends, écrit d'avance* : un gain nul ou marginal. H13 a montré que
passer de 3 à 6 finalistes rendait 9,6 % ; mais v11a a montré que le vivier et
les tours n'expliquent plus rien une fois le goulot à 6 (v11a = v14 à la
septième décimale). Si le reste de l'écart v9/v14 (0,1822 contre 0,1910)
venait du goulot, le passage à 9 devrait en reprendre une part visible ; je
crois plutôt que cet écart vient d'ailleurs (v9 et v14 diffèrent par le
moteur ET par le hasard des arbitrages serrés), et j'attends v15 ≈ H22a-v2.
Un gain net me donnerait tort, et ce serait une bonne nouvelle facile à
encaisser : le goulot est une constante, pas une architecture.

**H7 — le classifieur de frappes améliore le STEM et éloigne le MORCEAU,
écrite avant sa mesure (02/09/2026).** Le fait qui la motive : la course v6
(*Us and Them*, moteur du jour, classifieur de frappes RÉACTIVÉ après la
correction de provenance) rend 0,2108 contre 0,1907 pour v5 — **+10,5 % au
morceau** — alors que les distances de STEM sont identiques à un millième
près (bass 0,1844 → 0,1851, other 0,1737 → 0,1736). Seule la batterie
bouge, et peu (0,2181 → 0,2235). Un écart de dix pour cent au global pour
des pistes inchangées ne peut venir que de ce que le mélange fait de la
BATTERIE : le classifieur découpe cinq pièces (kick, hihat, kick2, tom,
openhat) là où le repli par bandes en trouve trois, et les deux pièces
supplémentaires ajoutent de l'énergie qui aide au stem et nuit au morceau.
Ce serait la leçon du mandataire (§ 5 septies) dans sa forme la plus nette :
un modèle mesuré MEILLEUR à son banc (A2 : charleston 16/16 au lieu de
8/16, zéro kick inventé) qui éloigne ce qu'on écoute.

Mais **deux variables ont changé** entre v5 et v6 — le classifieur ET le
moteur (35 machines, `vsm.cone` en lice, empreinte moteur neuve donc cache
froid) — et la règle du § 5 undecies l'interdit. Le témoin qui isole :
**v7 = moteur du jour, SANS classifieur**. v7 contre v5 mesure le moteur ;
v6 contre v7 mesure le classifieur. Succès de H7 : v7 ≈ v5 (le moteur ne
change rien, ce qu'on attend d'un ajout de machine qui perd son arbitrage)
et v6 > v7 d'au moins 5 % (le classifieur porte l'écart). Échec : le moteur
porte l'écart, et il faudra comprendre pourquoi un parc élargi éloigne le
morceau à stems identiques.

**H7 EST TRANCHÉE, ET C'EST LA LEÇON DU MANDATAIRE SOUS SA FORME LA PLUS
NETTE (02/09/2026).** Trois courses, une variable à la fois, mêmes stems,
mêmes budgets :

| course | moteur | classifieur de frappes | pièces | globale | batterie au stem |
|---|---|---|---|---|---|
| v5 | 35 machines | aucun | 3 | 0,1907 | 0,2181 |
| **v7** (témoin) | **37 machines** | aucun | 3 | **0,1910** | 0,2182 |
| **v6** | 37 machines | **2026-08-23** | **5** | **0,2108** | 0,2235 |

- **Le parc élargi ne change RIEN** : v7 contre v5, +0,16 % — du bruit
  (extraction de notes, cache froid). C'est ce qu'on attend d'un ajout de
  machines qui perdent leur arbitrage : `vsm.cone` a bien concouru sur
  `other` et a été écartée au verdict du mélange (0,3687 contre 0,2348),
  `vsm.vector` et `vsm.granular` de même. **Ajouter une machine ne dégrade
  pas la reconstruction**, et c'est mesuré plutôt que supposé.
- **Le classifieur porte TOUT l'écart** : v6 contre v7, **+10,4 %**. Un
  modèle mesuré MEILLEUR à son propre banc (A2 : charleston 16/16 au lieu
  de 8/16, zéro kick inventé) rend le MORCEAU plus lointain d'un dixième.
  Et il ne le fait presque pas au stem de batterie (+2,4 % seulement,
  0,2182 → 0,2235) : c'est bien le MÉLANGE qui paie.
- **Le garde-fou avait déjà nommé le coupable, et personne ne l'écoutait.**
  Les deux pièces que le classifieur ajoute sont exactement celles dont le
  journal dit, à chaque course : « ! tom : aucune frappe isolée,
  l'échantillon contient les autres pièces » et « ! openhat : aucune frappe
  isolée ». Leur échantillon contient le reste du kit ; les jouer superpose
  au mélange une copie sale de ce qui sonne déjà.

**H8 — ne pas jouer une pièce dont l'échantillon n'est pas isolé, écrite
avant sa mesure.** La chaîne DIT depuis longtemps qu'une pièce n'a aucune
frappe isolée ; elle la joue quand même. Proposition : une pièce ainsi
marquée est ÉCARTÉE du kit (dite au journal, comme toute candidate écartée
— § 5 bis), et **ses frappes sont perdues** : les détections sont par
famille, écarter la famille écarte ses instants (sur *Us and Them*, 321
frappes de « tom »). Une première rédaction de cette hypothèse annonçait
qu'elles « reviendraient à la pièce dont elles portent l'énergie » — c'était
faux, et le code le disait aussi ; le journal, lui, imprimait déjà le compte
exact. Ce qu'on abandonne est donc chiffré, et la mesure dira si le morceau
y gagne malgré tout. Témoin : le comportement actuel, par option.
Succès : avec classifieur, distance globale ≤ celle SANS classifieur sur
*Us and Them* — c'est-à-dire un classifieur qui cesse de coûter — et aucune
régression sur les trois autres étalons. Échec : l'écart persiste, et il
faudra chercher ailleurs que dans la propreté des échantillons.

**En attendant, le défaut reste le bon** : le classifieur de frappes n'est
utilisé que si on le passe explicitement (`--classifieur-batterie`), et la
campagne du 01/09 a couru sans lui. Ce qui change, c'est qu'on sait
maintenant que le passer COÛTE, et le chiffre est écrit.

**H8 EST MESURÉE LE JOUR MÊME : ELLE ÉCHOUE À SON CRITÈRE ET RÉCUPÈRE UN
QUART DE L'ÉCART.** *Us and Them*, quatrième course de la série :

| course | classifieur | pièces jouées | globale | contre v7 |
|---|---|---|---|---|
| v7 | aucun | 3 (par bandes) | 0,1910 | — |
| v6 | actif | 5 | 0,2108 | +10,4 % |
| **v8** | actif **+ H8** | **3** (tom et openhat écartés) | **0,2051** | **+7,4 %** |

- **Ce que H8 gagne** : 0,2108 → 0,2051, soit **−2,7 %** — un quart du coût
  du classifieur, pour 322 frappes abandonnées (321 « tom », 1 « openhat »).
  Écarter une pièce dont l'échantillon contient le reste du kit est donc
  bien un gain, et il est chiffré.
- **Ce que H8 ne gagne pas, et c'était son critère** : le morceau reste à
  +7,4 % de la course sans classifieur. **L'hypothèse est donc REJETÉE
  telle qu'écrite** — la propreté des échantillons n'explique qu'une part
  du coût.
- **Ce qui reste inexpliqué, et où chercher.** Les deux courses jouent
  alors les MÊMES trois familles, mais pas les mêmes frappes : sans
  classifieur `kick2=943 kick=925 hihat=414` (2 282), avec classifieur et
  H8 `kick=791 hihat=687 kick2=519` (1 997). La nomination du classifieur
  — meilleure à son banc — déplace 285 frappes d'une famille à l'autre et
  en abandonne 322, et c'est CELA qui coûte les 7,4 % restants. La question
  ouverte n'est plus « les échantillons sont-ils propres » mais « une
  frappe mieux NOMMÉE est-elle mieux JOUÉE » — et la réponse mesurée, pour
  l'instant, est non.

**H8 RESTE POURTANT LE DÉFAUT, et pour une raison vérifiée plutôt que
supposée** : dans la configuration par défaut — sans classifieur — **aucune
pièce non isolée n'apparaît jamais**. Vérifié sur les quatre morceaux de la
campagne (*Us and Them*, *Sky and Sand*, *Jaguar*, *B4 Wuz Then*) : zéro
avertissement. H8 est donc gratuite là où le garde-fou se tait, et
bénéfique là où il parle. Son témoin `--garder-pieces-non-isolees` reste,
et l'option est en provenance.

**H6 — des stems par un modèle RoFormer, écrite avant sa mesure
(01/09/2026).** La demande « de meilleurs stems » a désigné Spleeter
(Deezer) ; la piste est examinée et REFUSÉE sans course : sur MUSDB18,
Spleeter 4-stems vaut ~5,9 dB de SDR contre ~9,0 pour `htdemucs` (notre
défaut) et ~9,2 pour `htdemucs_ft` (H4) — et `spleeter` 2.4.2 exige
Python < 3.12 quand la chaîne tourne en 3.14. Mesurer un modèle que le banc
public place trois décibels SOUS le défaut ne trancherait rien. La forme qui
reste de cette demande : les modèles **BS/Mel-RoFormer** (MDX23/SDX,
~11-12 dB sur les voix, poids ouverts, par exemple via `audio-separator`)
séparent mieux que `htdemucs_ft` sur le banc public. Hypothèse : mêmes
critères que H4 — chaîne identique par ailleurs, stems dans un dossier
séparé, modèle dit au rapport, distance globale meilleure sur au moins deux
des trois étalons. À mesurer APRÈS H4 : si H4 échoue déjà à améliorer le
morceau alors que ses stems sont meilleurs au banc, un troisième cran de SDR
ne se justifiera qu'en comprenant d'abord pourquoi.

> **LA CONDITION S'EST RÉALISÉE LE SOIR MÊME : H4 est rejetée un étalon sur
> trois (bilan de campagne au § ci-dessous). H6 est donc SUSPENDUE à la
> compréhension, pas à la mesure** — tant qu'on ne sait pas pourquoi des
> stems meilleurs au banc public font un morceau plus lointain deux fois
> sur trois, un modèle encore meilleur au banc n'a aucune raison de faire
> mieux dans la chaîne.

**ET LE VIVIER DE PROFILS A CHANGÉ D'ÉCHELLE LE MÊME JOUR (01/09/2026).** Les
banques General MIDI libres s'installent désormais par
`tools/installer-banques-midi.py` (manifeste, empreintes épinglées,
attribution — décision 11 du CDC multisample) : 141 profils installés contre
31 la veille. Toute mesure de H1/H4/H5 postérieure à cette date court donc sur
ce vivier-là, et ne se compare aux chiffres antérieurs qu'en le disant.

---

## 6. Ce qui n'est pas au programme, et pourquoi

- **Reconstruire la voix.** Hors de portée d'une synthèse par machine ; la
  séparation la rend déjà disponible en audio, c'est le mieux qu'on puisse en
  faire honnêtement.
- **Ajouter des machines de caractère** (Oberheim, D-50, Poly-800…) : plaisant,
  mais cela élargit le catalogue sans améliorer la couverture. À faire pour le
  plaisir de jouer, jamais en s'en réclamant pour la reconstruction.
- **Une API réseau.** Le tube JSON suffit et supprime tout un pan de problèmes
  (port, authentification, cycle de vie). À rouvrir seulement si les deux
  programmes doivent un jour tourner sur deux machines.
- ~~**Un modèle appris de bout en bout (audio → paramètres).**~~ — **piste
  menée jusqu'au bout, MESURÉE, et refusée.** Détail et chiffres ci-dessous ;
  code conservé et documenté dans `analyse/analyzer/vsm_corpus.py`.

---

## 7. Le modèle appris : ce que la mesure a répondu

Le corpus étiqueté « que personne n'a », le moteur le fabrique pour rien :
**9 755 exemples utilisables en quatre minutes** (tirage uniforme dans l'espace
que la machine déclare, rendu par le moteur réel, descripteurs calculés). Cette
moitié de la promesse est tenue sans réserve.

### L'estimateur apprend réellement quelque chose

Sur 120 cibles d'épreuve rendues par `vsm.generic` lui-même, distance médiane
du patch prédit — sans aucune recherche :

| | distance médiane | mieux que le hasard |
|---|---|---|
| tirage au hasard | 0,5208 | — |
| **ridge** | **0,1798** | 91 % du temps |
| forêt aléatoire | 0,1825 | 97 % |
| perceptron | 0,1867 | 95 % |

### Mais il ne remplace pas la recherche, et ne l'accélère pas seul

Sur 14 cibles, à 10 axes. La colonne qui compte est la dernière : une médiane
qui s'améliore alors qu'on ne gagne qu'une fois sur deux n'est pas un gain,
c'est du bruit — la leçon exacte de l'étape 10.2.

| régime | distance médiane | temps | bat la recherche |
|---|---|---|---|
| A — prédiction seule | 0,2223 | **0,0 s** | 7 % |
| B — recherche 20 it. (référence) | 0,0515 | 31,0 s | — |
| C — boîte ±0,15, 5 it. | 0,0597 | 8,7 s | 29 % |
| D — boîte ±0,15, 20 it. | 0,0433 | 30,6 s | 50 % |

La **boîte** est le seul angle que 10.2 ne pouvait pas essayer : elle disposait
d'un point, qui se dilue dans une population de trente-six ; on tient ici une
région, ce qui réduit le problème au lieu d'en déplacer le départ. D régresse
pourtant jusqu'à **5,1×** quand la prédiction est mauvaise : elle enferme alors
la recherche loin de la solution.

### Le garde-fou marche : la prédiction sait dire si on peut la croire

Sa propre distance à la cible — un rendu, ~12 ms — est corrélée à **−0,66** au
gain qu'elle apportera. En ne resserrant que sous un seuil de 0,25 :

| régime prudent | distance médiane | contre B | pire régression | temps |
|---|---|---|---|---|
| boîte 20 it. si sûr | **0,0406** | **−21 %** | 1,4× | 31,0 s |
| boîte 5 it. si sûr | 0,0514 | identique | 1,7× | **18,3 s** (1,7× plus vite) |

Deux points de fonctionnement parfaitement utilisables… sur des cibles que la
machine sait produire.

### Ce qui tue la piste : le fossé de domaine

Sur 9 cibles **réelles** — extraits des stems séparés de House Of God, avec
leurs artefacts et leurs fuites d'autres instruments :

| régime | distance médiane | contre B | pire |
|---|---|---|---|
| A — prédiction seule | 0,2708 | **+37 %** | 5,9× |
| D — boîte sans garde-fou | 0,2032 | +3 % | 1,3× |
| **D — prudent** | **0,1974** | **identique à B** | 1,0× |

Le garde-fou refuse de resserrer sur **8 cibles réelles sur 9** : la méthode ne
nuit pas, elle se ramène simplement à la recherche ordinaire. Et la cause n'est
pas un défaut de modèle mais une impossibilité de principe : le corpus ne
contient que des sons que la machine sait produire, alors qu'un stem séparé est
un son qu'AUCUNE machine ne produit. On demande à l'estimateur d'inverser une
application en dehors de son image.

### À quelle condition rouvrir le dossier

Il faudrait un corpus qui contienne la **dégradation** et pas seulement le son
propre : rendre le patch, le mélanger à d'autres stems, faire repasser le tout
par demucs, et étiqueter le résultat avec le patch d'origine. C'est faisable, et
c'est cher — une séparation par exemple, contre 25 ms aujourd'hui. Tant que ce
corpus-là n'existe pas, l'estimateur restera un bon inverseur de ce que le
moteur produit et un mauvais lecteur de ce qu'un disque contient.

> **CONFIRMÉ PAR UN SECOND CHEMIN, INDÉPENDANT (23/08/2026).** La phase A1 de
> [`ROADMAP-apprentissage.md`](ROADMAP-apprentissage.md) a entraîné un
> CLASSIFIEUR — un autre objet, une autre tâche, un autre corpus — et il bute au
> même endroit : 99,9 % de bonne machine dans le top 3 sur ce que le moteur
> produit, et sur un piano réel il annonce `vsm.sh101` avec un score de 1,00.
>
> Surtout, l'objection évidente a été éprouvée et écartée. On pouvait croire que
> l'augmentation synthétique du corpus comblerait le fossé ; elle a été mesurée,
> puis RENFORCÉE (la fuite ×4, la compression ×3), et le résultat sur un
> enregistrement réel n'a pas bougé d'un cran — rang médian identique de la
> machine que l'arbitrage retient. **Dégrader synthétiquement un rendu moteur
> n'en fait pas un disque**, quelle que soit la dose.
>
> La condition de réouverture écrite ci-dessus n'est donc plus une intuition :
> c'est la seule piste que deux mesures indépendantes laissent debout.

> **LA CONDITION A ÉTÉ REMPLIE, ET ELLE NE ROUVRE PAS LE DOSSIER (28/08/2026).**
> Le corpus décrit ci-dessus existe — `analyse/corpus_separe.py` : rendus du
> parc mélangés à de vrais stems, repassés par demucs par tranches, étiquetés
> avec le patch d'origine ; 7 990 exemples à 100 patchs par machine, en une
> heure — et il a été mesuré contre cinq stems réels dont la chaîne a retenu
> la machine. Un modèle entraîné dessus rattrape la moitié du fossé SUR CE
> CORPUS (top 3 à patchs égaux : 39 % → 86 %) et ne lit pas mieux un disque
> qu'un modèle entraîné sur le sec (somme des rangs médians de la machine
> retenue : 45 pour le sec, 54 pour le séparé, à 100 patchs comme à 25). Ce
> qu'il apprend, c'est le résidu que demucs laisse d'un synthé — et il en voit
> un dans chaque stem réel. Le détail, les tableaux et le seul gain (un stem
> de basse) sont dans [`ROADMAP-apprentissage.md`](ROADMAP-apprentissage.md),
> phase A1. L'estimateur reste ce que ce paragraphe disait : un bon inverseur
> de ce que le moteur produit. Il n'y a plus de condition de réouverture
> écrite, parce qu'il n'y a plus de corpus fabricable qui la remplirait.

---

## 8. Invariants à vérifier à chaque étape

Ils ont tenu jusqu'ici ; ils doivent continuer.

```
[ ] Le MOTEUR se compile et passe ses tests SANS Python, SANS réseau, SANS CLAP
    (l'application, elle, exige JUCE : hors ligne, lui en désigner une copie)
[ ] core/ et audio/ n'incluent rien de interchange/, ni de JSON
[ ] Le chemin temps réel reste sans allocation, sans verrou, sans I/O
[ ] Deux rendus identiques donnent le même audio, au bit près
[ ] Aucune approximation silencieuse : ce qui n'est pas reproductible est DIT
[ ] Les empreintes audio des machines existantes restent inchangées
[ ] Ajouter une machine ne touche ni le moteur ni l'interface
```

**CES CASES SONT DES CHOSES À REVÉRIFIER, PAS DES ACQUIS À COCHER** — c'est le
sens de « ils doivent continuer », et une case cochée une fois pour toutes
serait justement la garantie qu'on perd sans s'en apercevoir. Ce qui suit date
les mesures ; il ne dispense pas de les refaire.

**LE PREMIER INVARIANT A ÉTÉ EXÉCUTÉ POUR LA PREMIÈRE FOIS (31/08/2026), ET IL
ÉTAIT FAUX D'UN QUART.** Ces cases n'avaient jamais été cochées par une mesure ;
celle-là l'est maintenant, avec `FETCHCONTENT_FULLY_DISCONNECTED=ON`, qui
INTERDIT à CMake de télécharger quoi que ce soit — c'est la seule façon de
vérifier « sans réseau » sans débrancher la machine.

| ce qui a été éprouvé | verdict |
|---|---|
| moteur seul (défauts), configuration hors ligne | **code 0** |
| moteur seul, compilation hors ligne | **code 0** |
| ses tests : `vsm_core` 158, `vsm_audio` 801, `vsm_interchange` 206 | **1 165 verts** |
| « Python » dans le cache CMake du moteur | **0 occurrence** |

*(Rejoué après l'ajout des tests d'interposition `dlsym`/`pthread` du même
jour — la partie de la suite la plus plausiblement sensible à un
environnement minimal — : mêmes verdicts, comptes à jour.)*
| **application (`VSM_BUILD_APP=ON`), hors ligne** | **ÉCHEC** |
| application hors ligne + `FETCHCONTENT_SOURCE_DIR_JUCE` | **code 0** |

**CE QUE L'INVARIANT DISAIT DE TROP.** `CMakeLists.txt` récupère JUCE 8.0.4
depuis GitHub par `FetchContent` dès que `VSM_BUILD_APP` ou `VSM_BUILD_VST3`
est actif. Sur une machine neuve, **l'application ne se compile pas sans
réseau** ; elle ne le faisait ici que parce que `build/_deps/juce-src` était
déjà peuplé. L'invariant était donc vérifié par un CACHE et non par la
construction — ce qui est la définition d'une garantie qu'on perd sans s'en
apercevoir.

La distinction existait déjà pour CLAP, dont l'option annonce en toutes lettres
« nécessite de télécharger le SDK CLAP », et elle manquait pour JUCE. Elle est
maintenant écrite aux trois endroits qui la portaient à faux : l'option
`VSM_BUILD_APP` de `CMakeLists.txt`, l'invariant n° 4 de
[`ROADMAP-daw.md`](ROADMAP-daw.md) § 6, et cette liste-ci.

**CE QUI RESTE VRAI, ET C'EST L'ESSENTIEL.** Le moteur — `core/`, `audio/`,
`interchange/`, `tools/` — se construit et se prouve **entièrement hors ligne,
sans Python, sans CLAP**. C'est la partie dont dépendent toutes les mesures
publiées, et elle ne demande rien à personne. Pour l'application, la
construction hors ligne est possible et la commande est :

```
cmake -S . -B build -DVSM_BUILD_APP=ON \
      -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
      -DFETCHCONTENT_SOURCE_DIR_JUCE=/chemin/vers/JUCE
```

Le dernier invariant est celui qui a rendu tout le reste possible : c'est parce
qu'une machine ne coûte que son propre dossier qu'on peut envisager d'en ajouter
trois de plus sans crainte.
