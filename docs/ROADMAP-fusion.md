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

- 23 machines (+ tonalité d'essai), 9 effets, moteur temps réel, 725 tests
  verts, zéro warning.
- Piano roll complet, façades « façon hardware » pour les 23 machines,
  séquenceurs à pas pour celles qui en ont un.
- Interop : identités sémantiques (563 paramètres), presets `*.synth.json`,
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
- **Le sampler n'est plus un repli universel : il est réservé à la voix.**
  C'est ce qui a rendu nécessaires `vsm.piano` et `vsm.drums`, et ce qui donne
  au parc sa forme finale — chaque source a une machine qui la MODÉLISE, sauf
  la voix, qui est reportée telle quelle et présentée comme telle.
- ~~Une recherche coûte ~13 s par note et par machine~~ — **traité par la
  phase 10**, mais le coût reste réel et il faut le dire : la présélection à
  deux étages le divise par deux à verdict identique (343 s → 174 s sur quinze
  machines), et un stem entier demande aujourd'hui ~350 s au budget par défaut,
  ~960 s à 60 itérations. Un morceau de quatre stems se reconstruit donc en
  ~16 min au défaut et ~40 min au budget élevé.
- **Le budget de recherche change les distances d'un facteur deux** (basse :
  0,103 à 20 itérations, 0,053 à 60) : deux mesures ne se comparent que si
  elles ont le même budget. Il est pour cela inscrit dans chaque
  `rapport.json`, au même titre que la métrique.

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

## 5 bis. Quatre pannes muettes de la chaîne d'analyse

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

---

## 8. Invariants à vérifier à chaque étape

Ils ont tenu jusqu'ici ; ils doivent continuer.

```
[ ] Le DAW se compile et passe ses tests SANS Python, SANS réseau, SANS CLAP
[ ] core/ et audio/ n'incluent rien de interchange/, ni de JSON
[ ] Le chemin temps réel reste sans allocation, sans verrou, sans I/O
[ ] Deux rendus identiques donnent le même audio, au bit près
[ ] Aucune approximation silencieuse : ce qui n'est pas reproductible est DIT
[ ] Les empreintes audio des machines existantes restent inchangées
[ ] Ajouter une machine ne touche ni le moteur ni l'interface
```

Le dernier invariant est celui qui a rendu tout le reste possible : c'est parce
qu'une machine ne coûte que son propre dossier qu'on peut envisager d'en ajouter
trois de plus sans crainte.
