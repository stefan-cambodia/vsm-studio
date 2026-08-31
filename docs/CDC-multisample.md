# Cahier des charges — `vsm.multisample` : l'acoustique par échantillons

**Question posée.** Le parc n'a aucune route honnête pour un instrument
acoustique mélodique — un piano, des cordes, tout un orchestre. Faut-il des
machines par instrument ?

**Réponse courte.** Non : **une** machine, un lecteur multi-échantillons, et
les banques libres font le reste. Le piano classique est son premier profil ;
l'import SoundFont (`.sf2`) apporte ensuite l'orchestre General MIDI entier
sans une ligne de DSP nouvelle. La modélisation physique (cordes frottées,
vents) reste possible plus tard, machine par machine, pour l'expressivité —
mais la **couverture**, celle qui manque à la reconstruction, se règle ici en
un seul chantier.

Ce CDC suit [`CDC-nouvelle-machine.md`](CDC-nouvelle-machine.md) (obligations
générales) et [`GUIDE-ajout-machine.md`](GUIDE-ajout-machine.md) (intégration).
Il prolonge [`CDC-apprentissage.md`](CDC-apprentissage.md) : la route
d'abstention « aucune machine du parc » y gagne enfin une destination.

---

## 1. Ce que le banc a mesuré, et qui justifie la machine

Clair de Lune (piano seul, 311 s, 2219 notes), reconstruit le 23/08/2026 par
la chaîne complète (`--sans-separation`, métrique v2) :

- les **huit** premières machines finissent entre 0,259 et 0,320 — une
  photo-finish qui dit « aucune ne ressemble à un piano, toutes s'en
  approchent aussi mal » ;
- gagnant : `vsm.supersaw`, d=0,259 ; distance globale **1,639** (silence :
  62,3), dont l'essentiel récupéré par l'automation de coupure (8,34 → 1,64,
  606 points) — la preuve que le parc *compense* au lieu de *reproduire* ;
- l'écoute est sans appel : les notes et le geste y sont, le timbre non.

**Le critère de clôture de ce CDC est le même banc rejoué** : Clair de Lune
avec le profil piano doit battre nettement 1,639, et l'écoute doit dire
« piano ». Le chiffre exact attendu n'est pas fixé d'avance — il sera publié.

## 2. Positionnement — pourquoi pas `vsm.sampler`, pourquoi pas la synthèse

- `vsm.sampler` est percussif par construction : un échantillon par
  emplacement, déclenché par note fixe, sans zones de clavier ni couches de
  vélocité. L'étendre le dénaturerait (sa façade et son CDC §30
  d'ARCHITECTURE.md sont pensés batterie). `vsm.multisample` est une machine
  distincte qui **réutilise** ses briques éprouvées : chargement hors thread
  audio par `ISampleLoader`, publication par échange atomique, chemins
  relatifs obligatoires, échantillon manquant signalé jamais substitué.
- La synthèse d'un piano (marteau-corde, résonance sympathique, table
  d'harmonie) est un sujet de recherche entier. Le dépôt a déjà tranché ce
  dilemme pour les cymbales des TR : synthétiser quand c'est possible,
  **documenter l'approximation quand ça ne l'est pas**. Un piano relève du
  second cas ; le prétendre soluble en soustractif serait exactement le genre
  d'affirmation sans mesure que la section 27 interdit.

## 3. Architecture

```
NOTE MIDI ──> choix de ZONE (étendue de notes × étendue de vélocités)
                │  échantillon, note racine, accord fin, points de boucle
                v
         lecture repitchée (interpolation cubique minimum)
                │  boucle de tenue si déclarée
                v
         enveloppe d'amplitude (attaque courte, release réglable)
                v
         niveau / panoramique / (filtre passe-bas doux optionnel)
```

- **Zones** : chaque zone déclare étendue de notes, étendue de vélocités,
  échantillon, note racine, accord (cents), points de boucle facultatifs,
  niveau. Le choix de zone est déterministe (la première qui contient
  note+vélocité ; l'ordre du preset fait foi).
- **Polyphonie** : `VoiceManager`, 32 voix minimum (un piano avec pédale tient
  des dizaines de notes), vol « oldest » standard.
- **Boucle de tenue** : croisement de boucle sans clic (testé : énergie
  continue au point de bouclage). Pas d'échantillons de relâchement en v1 —
  omis et documenté.
- **Rééchantillonnage** : cubique minimum, l'approximation documentée
  (règle déjà établie pour le sampler).
- **Mémoire** : préchargement complet en v1, pas de streaming disque. Budget
  déclaré et vérifié : un profil ne doit pas dépasser ~256 Mo en mémoire ;
  le profil piano se construit en conséquence (voir §4).

**Approximation assumée — la pédale.** Le contrat `ISynthPlugin` transporte
NoteOn/NoteOff, pas les CC : pas de CC64 en v1. C'est moins grave qu'il n'y
paraît pour la reconstruction — la transcription **inscrit déjà la pédale
dans la durée des notes** (les 2219 notes de Clair de Lune la portent). Pour
le jeu au clavier, la limite est réelle, elle est documentée dans le code, et
l'extension éventuelle du contrat (événements CC datés au sample) est une
décision de moteur à prendre séparément — pas en contrebande d'une machine.

## 4. Livrable 1 — le profil piano

- **Source** : une banque libre et attribuable — Salamander Grand Piano
  (CC-BY) en premier choix, University of Iowa en repli. **Aucun échantillon
  commis dans le dépôt** : un script (`tools/` ou `analyse/`) télécharge,
  vérifie par empreinte SHA-256, convertit au format du profil (WAV, zones
  déclarées dans un JSON), et écrit le fichier d'attribution. Section 28
  respectée à la lettre : licence inconnue → banque refusée.
- **Sélection** : la banque complète est trop grande pour le budget mémoire ;
  le profil retient un sous-ensemble mesuré (de l'ordre d'un échantillon
  tous les 2-3 demi-tons × 3-4 couches de vélocité) — le pas exact se choisit
  à l'oreille et au budget, et s'écrit dans le JSON du profil, pas dans le
  code.
- **Critères** : une gamme chromatique rendue ne présente ni saut de timbre
  audible aux frontières de zones (test spectral aux frontières), ni clic de
  boucle ; pianissimo et fortissimo tirent des couches différentes (testé).

## 5. Livrable 2 — l'import SoundFont

- Lecture du sous-ensemble **utile** de SF2 : samples, zones (keyRange,
  velRange, rootKey, boucle, accord), enveloppe de volume, sélection par
  programme. Les générateurs exotiques (modulateurs, chorus/réverbération
  intégrés…) sont **ignorés en le disant** — liste des générateurs non pris
  en charge imprimée au chargement, jamais d'à-peu-près silencieux.
- La conversion se fait **hors du moteur** (outil dans `analyse/` ou
  `tools/`) : le DAW ne charge que le format de profil de la machine. Le
  moteur reste sans dépendance ni format tiers — l'invariant d'autonomie
  tient.
- Banques visées : FluidR3, GeneralUser GS (licences libres, attribution
  écrite). Résultat : l'orchestre General MIDI — cordes, cuivres, bois,
  harpe, timbales — disponible comme **profils** d'une seule machine.
- **Paramètre `program`** (identité sémantique dédiée) : discret, exclu de
  l'espace de recherche continu — c'est le classifieur/l'utilisateur qui
  choisit l'instrument, pas l'évolution différentielle (leçon `vsm.generic` :
  les falaises discrètes piègent la recherche).

## 6. Paramètres et identités sémantiques

Peu de paramètres, en unités réelles, tous déclarés :

| Paramètre | Identité | Note |
|---|---|---|
| Niveau | `output.level` | |
| Accord global | `output.tune` | cents |
| Attaque / Release | `envelope.1.{attack,release}` | surcharge de l'enveloppe du profil |
| Coupure douce | `filter.1.cutoff` | passe-bas 1 pôle, neutre à fond |
| Programme | `sample.program` | discret, hors recherche |
| Vélocité → niveau | `response.velocity` | 0 = plat, 1 = pleine dynamique |

Profil de recherche déclaré : seuls niveau, accord, enveloppe et coupure sont
cherchables — l'espace est **petit et sage**, c'est voulu : pour cette machine,
le timbre vient des échantillons, la recherche n'a qu'à caler l'habillage.

## 7. Intégration à la chaîne d'analyse et d'apprentissage

- `melodic_machines()` inclut la machine dès qu'un profil est présent ;
  sans profil installé, elle est absente des candidates **en le disant**.
- La route d'abstention du classifieur (CDC-apprentissage §4) devient
  « acoustique → `vsm.multisample` », profil choisi par le classement.
- Le corpus d'apprentissage (phase A0) inclut ses rendus **par profil
  installé**, avec l'empreinte du profil dans le manifeste — un profil changé
  périme le corpus, même règle que les machines.
- Banc de clôture : Clair de Lune rejoué, chiffres publiés contre la ligne de
  base 1,639 / supersaw, et l'A/B écouté.

## 8. Tests (batterie du GUIDE §9, plus les spécifiques)

Outre les sept obligatoires (enregistrement, silence, son, déterminisme,
save/load, taille de la liste de paramètres) :

1. la note à la frontière de deux zones tire la bonne zone (des deux côtés) ;
2. deux vélocités de part et d'autre d'un seuil de couche tirent deux
   échantillons différents ;
3. la boucle de tenue ne claque pas (continuité d'énergie au bouclage) ;
4. le repitch est juste (fréquence fondamentale mesurée à ±5 cents sur une
   sinusoïde de référence) ;
5. profil manquant, échantillon manquant, chemin absolu : refusés en le
   disant ;
6. un SF2 minimal de test (généré par l'outil de conversion, quelques zones
   synthétiques, commis car minuscule) fait l'aller-retour complet.

L'empreinte de non-régression se prend sur le SF2 minimal de test — pas sur
une banque téléchargée, qui ne doit jamais conditionner les tests.

## 9. Anti-objectifs (v1)

- Pas de SFZ complet, pas de round-robin, pas d'échantillons de relâchement,
  pas de streaming disque, pas de résonance sympathique — chacun omis **avec
  sa raison** dans le code, réévaluable sur mesure.
- Pas de CC64 tant que le contrat ne transporte pas les CC (§3).
- Pas de banque embarquée dans le dépôt, quelle que soit sa licence : le
  dépôt reste léger et compilable hors ligne, les banques s'installent.

## 10. Phases

| Phase | Contenu | Terminé quand | État |
|---|---|---|---|
| M1 | Moteur multisample + format de profil + profil piano (Salamander) | la reconstruction AVEC la machine bat la même SANS, à conditions identiques ; A/B écouté | **livré et MESURÉ, non clos** : écart 0,0000 sur *Clair de Lune* — §11 |
| M2 | Outil de conversion SF2 + profils orchestre (FluidR3) | un stem de cordes réelles reconstruit passe par la machine et bat toute machine de caractère, mesuré | **outil livré**, banque orchestre non mesurée — §12 |
| M3 | Intégration analyse/apprentissage (candidates, corpus, classifieur) | la route « acoustique » du classifieur aboutit ici ; abstention réservée à l'inconnu véritable | **débloqué** : `CDC-apprentissage.md` et `ROADMAP-apprentissage.md` sont installés (23/08). M3 suit désormais les phases A0-A5 de cette feuille, et la route d'abstention est tranchée par l'amendement du § 4 de ce cahier |

**Sur le critère de clôture de M1, une précision qui a coûté une fausse
conclusion.** Le premier rejeu a donné 0,2159 contre 1,639, soit sept fois
mieux — et ce chiffre ne vaut rien. Le `rapport.json` de référence n'a ni
`trackArbitration`, ni `trackDistance`, ni `gate` : il a été produit **sans
l'arbitrage sur la piste**. Les deux mesures ne se comparent donc pas, et
`vsm.multisample` n'avait de surcroît pas participé à la seconde. C'est, sous
une quatrième forme, la règle du § 10.3 de la feuille de route — *une distance
n'est un chiffre que si l'on sait à quelles conditions elle a été obtenue* — et
elle vaut aussi contre soi-même, quand le chiffre va dans le sens qu'on
espérait. La seule mesure recevable est un COUPLE : le même morceau, le même
code, les mêmes options, avec et sans la machine.

---

*Règle du dépôt, rappelée pour cette machine plus que pour toute autre : ne
jamais prétendre qu'un profil « sonne comme » l'instrument sans mesure ni
écoute publiée. Un lecteur d'échantillons honnête vaut mieux qu'une
modélisation vantée.*

---

## 11. M1 — état, et les décisions que ce cahier laissait ouvertes

Cette section est ajoutée à l'écriture de M1. Elle ne redit pas ce
qu'ARCHITECTURE.md § 35 dit déjà (la chaîne, les trois pièges, les
approximations) : elle consigne les **choix** que le cahier des charges laissait
en suspens, avec leur raison, pour qu'on ne les rouvre pas par oubli.

### Ce qui est livré

Moteur, format, outil d'installation, tests, façade, empreinte. **756 tests
verts, zéro warning** (81 core + 543 audio + 110 interchange + 11 CLAP + 11
façades), contre 725 avant.

- `audio/plugins/multisample/` — la machine, sept paramètres, trente-deux voix ;
- `audio/include/vsm/audio/plugin/IMultisampleBank.h` — les zones, sans JSON ;
- `interchange/{include,src}/…/MultisampleProfile.*` — le format
  `*.profile.json`, la découverte des profils installés, l'installation dans une
  machine ;
- `tools/installer-profil-piano.py` — téléchargement, empreinte, sélection,
  conversion, attribution ;
- façade dans `panels/src/MachinePanels.cpp`, empreinte dans
  `audio/tests/audio_fingerprints.inc`.

### Décision 1 — `voice.velocitySensitivity`, pas `response.velocity`

Le § 6 proposait une identité neuve, `response.velocity`. Elle n'a pas été
créée : `voice.velocitySensitivity` existe déjà dans le vocabulaire (elle sert à
`vsm.wind`) et dit **exactement la même chose** — la vélocité agit-elle sur la
dynamique. En inventer une seconde pour la même notion est la faute que le § 8.4
de la feuille de route reproche au reste du projet : la même chose sous deux
noms, et deux tables à tenir d'accord. Une identité neuve a en revanche été
créée là où rien n'existait : `output.tune`, accord global en cents, cherchable
(importance 0,55) parce qu'une banque enregistrée instrument par instrument
n'est jamais parfaitement d'accord avec elle-même.

### Décision 2 — le format vit dans `interchange/`, la machine reçoit des zones

Le § 3 décrivait l'architecture DSP sans dire où le JSON serait lu. Il est lu
dans `interchange/`, et la machine reçoit des structures nues par
`IMultisampleBank`. L'invariant « `core/` et `audio/` n'incluent rien de
`interchange/`, ni de JSON » (feuille de route § 8) prime sur la commodité, et
le bénéfice est concret : les round-robin, les échantillons de relâchement et le
SFZ pourront s'ajouter au format **sans recompiler une seule machine**.

### Décision 3 — l'empreinte se prend sur un profil ENGENDRÉ, pas sur un SF2

Le § 8 prévoyait de figer l'empreinte sur « le SF2 minimal de test ». Elle est
prise sur un profil **engendré en mémoire** par
`test_audio_regression.cpp` — quatre zones, deux couches de vélocité de part et
d'autre de 100, une boucle calée sur un nombre entier de périodes. C'est plus
strict que ce que le cahier demandait : l'empreinte ne dépend d'**aucun
fichier**, pas même d'un fichier commis, donc aucune corruption de dépôt ni
aucun outil de conversion ne peut la faire dériver. Le SF2 minimal reste prévu
pour M2, où il éprouvera l'aller-retour de conversion — ce qui est son vrai
sujet.

### Décision 4 — sans profil, la machine est écartée ET le pont refuse

Le § 7 demandait qu'elle soit « absente des candidates en le disant ». C'est
fait (`melodic_machines()` l'écarte et l'annonce), et **une seconde barrière a
été ajoutée** : le service de rendu refuse la requête au lieu de rendre du
silence. La raison est celle qui a motivé tout ce cahier — une machine muette ne
perd pas la comparaison, elle la fausse en gagnant sur toutes les cibles douces.
Un refus se lit ; un zéro se mesure et se publie.

Le profil employé est choisi **une fois** pour la durée du moteur et injecté
dans chaque requête. Deux rendus d'une même exécution portent donc le même
profil : c'est la règle du § 10.3 de la feuille de route (« une distance n'est un
chiffre que si l'on sait à quelles conditions elle a été obtenue »), appliquée à
une condition de plus.

### Décision 5 — l'outil refuse une archive dont l'empreinte n'est pas épinglée

Le § 4 demandait une vérification SHA-256 sans dire ce qui se passe la première
fois, quand le dépôt ne connaît pas encore l'empreinte. L'outil **refuse**, et
n'installe que si un mainteneur passe `--epingler-empreinte` — geste explicite,
qui se relit ensuite dans le diff. Accepter par défaut « la première empreinte
vue » reviendrait à n'avoir aucune vérification tout en affichant qu'on en a
une, ce qui est pire que de ne rien vérifier.

**Archive retenue** : `SalamanderGrandPianoV3+20161209_44khz16bit.tar.xz`
(412 Mo) plutôt que la distribution SFZ+FLAC de 2020 (742 Mo). Le profil
sous-échantillonne de toute façon la banque ; payer 330 Mo de plus pour du
24 bits à 48 kHz qu'on tronque ensuite n'achète rien.

### Décision 6 — le passe-bas est neutre EXACTEMENT à fond

Détaillé dans ARCHITECTURE.md § 35 : bouton à fond veut dire chemin direct, et
le test le vérifie échantillon pour échantillon.

### Décision 7 — un cache d'échantillons, sinon la machine était incherchable

Ce point n'était dans aucun paragraphe du cahier des charges, et il a failli
tout annuler. Le service de rendu crée une **instance neuve par requête** —
c'est ce qui garantit que deux rendus identiques donnent le même son, et il ne
faut pas y toucher. Mais installer un profil de piano, c'est décoder cent vingt
fichiers et deux cent trente mégaoctets :

| | coût d'une évaluation |
|---|---|
| `vsm.minimoog` | 4,1 ms |
| `vsm.multisample`, profil rechargé à chaque rendu | **123,9 ms** |
| `vsm.multisample`, échantillons décodés mis en cache | **2,0 ms** |

Trente fois le prix d'un soustractif, c'est une machine qu'on ne cherche pas :
on la subit. Le service partage donc les échantillons DÉCODÉS entre les
instances successives (`MultisampleSampleCache`), et rien d'autre — jamais de
l'état de machine. L'invariant tient : un test vérifie que le rendu est
identique **au bit près** avec cache et sans. Une fois le cache en place, la
machine est même moins chère que le Minimoog, ce qui n'a rien d'étonnant : lire
un échantillon coûte moins qu'un filtre en échelle.

La leçon est générale et vaut pour toute machine future qui dépendrait d'une
donnée volumineuse : **une machine trop lente n'est pas une machine lente, c'est
une machine absente** — la présélection l'écarte au premier tour, et son absence
se lit comme un mauvais résultat.

### Le banc de clôture, et son résultat : la machine n'apporte RIEN sur ce morceau

Mesuré comme il fallait le mesurer — **le même morceau, le même code, les mêmes
options, avec et sans la machine**, seul le dossier de profils changeant :

| | distance globale | machine retenue |
|---|---|---|
| SANS `vsm.multisample` (19 candidates) | **0,2159** | `vsm.piano` |
| AVEC `vsm.multisample` (20 candidates) | **0,2159** | `vsm.piano` |
| écart | **0,0000** | — |

`vsm.multisample` finit **7e sur 30** au tableau d'arbitrage (0,3571, patch
d'usine) et **ne survit même pas à la présélection** de la recherche par note :
elle n'est pas dans les dix finalistes. Elle est mesurée, elle est visible, elle
perd.

**Le critère du § 1 était écrit contre une référence invalide, et il faut le
réécrire.** « Battre nettement 1,639 » n'a pas de sens : les 1,639 venaient d'un
rapport sans arbitrage de piste, et le code d'aujourd'hui descend à 0,2159 **sans
la machine**. Le critère de clôture de M1 devient donc, et c'est ce qui sera
mesuré à l'avenir :

> *M1 est clos quand, sur un morceau de piano, la reconstruction AVEC
> `vsm.multisample` bat la même reconstruction SANS, à conditions strictement
> identiques, et que l'écoute A/B confirme.*

**Il ne l'est pas.** Sur *Clair de Lune*, l'écart est nul. La machine est livrée,
testée, mesurée et honnête ; elle n'a pas encore montré son utilité sur la
reconstruction, et ce document ne prétendra pas le contraire.

---

## 12. M2 — l'import SoundFont, et la panne muette qu'il a fallu trouver d'abord

### Ce qui est livré

- `interchange/{include,src}/…/SoundFont.*` — lecture du sous-ensemble utile
  d'un `*.sf2` et conversion vers le format de profil ;
- `tools/vsm-sf2` — l'outil : `--lister`, `--convertir`, `--sf2-minimal` ;
- quatre tests, dont **l'aller-retour complet** du § 8.6.

### Décision 8 — la LECTURE vit dans `interchange/`, l'OUTIL dans `tools/`

Le § 5 demande que la conversion se fasse « hors du moteur, outil dans
`analyse/` ou `tools/` ». L'outil **est** dans `tools/` (`vsm-sf2`). Ce qui vit
dans `interchange/`, c'est l'analyseur de format, et pour une raison que le
dépôt applique partout ailleurs : **dans ce projet, tout format a des tests.**
Il n'existe aucune infrastructure de test Python ; un analyseur SF2 écrit en
Python n'aurait donc aucune couverture, et un analyseur de format sans tests est
exactement la pièce qui se met à mal interpréter un champ sans que personne ne
le voie — la confusion décibels / centibels ci-dessous en est la démonstration.

L'invariant qui comptait est intact : `core/` et `audio/` ignorent tout du SF2,
le DAW ne charge jamais qu'un profil, et le moteur reste sans format tiers.

> **Cette raison a cessé d'être vraie le jour même, et il faut le dire.** La
> phase A0 de [`ROADMAP-apprentissage.md`](ROADMAP-apprentissage.md) exige des
> tests Python (« testé » y figure deux fois), et un cadre maison sans
> dépendance a donc été écrit — `analyse/tests/`. L'argument « il n'existe pas
> d'infrastructure de test Python » ne tient donc plus tel quel. La décision est
> maintenue, pour une raison qui, elle, reste vraie : le test d'aller-retour
> exerce en UN SEUL processus la conversion, l'écriture du profil, sa relecture
> par le chargeur du DAW et le rendu par la machine. Écrit en Python, il
> s'arrêterait au fichier produit et ne dirait rien de ce que le moteur en
> fait. Si la question se rouvre un jour, c'est cet argument-là qu'il faudra
> réfuter, pas celui de l'outillage.

### Décision 9 — le SF2 minimal est ENGENDRÉ, pas commis

Le § 8.6 prévoyait de commettre un petit SF2. Il est **écrit par le code**
(`writeMinimalSoundFont`, exposé par `vsm-sf2 --sf2-minimal`). Un binaire commis
est opaque : personne ne relit ce qu'il contient, et le jour où un test échoue on
ne sait pas si la faute est au lecteur ou au fichier. Ici, le contenu attendu
**est du code relisible**, et le test compare la lecture à ce que l'écriture a
voulu dire. Le fichier engendré pose volontairement un générateur non pris en
charge (`initialFilterQ`), pour que le rapport « voici ce que j'ai ignoré » soit
lui aussi éprouvé.

### Décision 10 — l'enveloppe de volume va dans un `*.synth.json`, pas dans le profil

Le § 5 demande de lire l'enveloppe de volume du SF2. Le format de profil n'a pas
d'enveloppe par zone, et lui en ajouter une pour ce seul besoin aurait été un
élargissement du format au profit d'un cas particulier. L'outil écrit donc, à
côté du profil, un preset `*.synth.json` — un format qui existe déjà — portant
`envelope.1.attack` et `envelope.1.release` et **désignant le profil par son
nom**. Rien du SF2 n'est perdu en silence : ce qui n'entre pas dans le profil
entre dans le preset, et ce qui n'entre nulle part est imprimé.

### Décision 11 — les banques General MIDI s'installent par SCRIPT, en ensemble canonique (01/09/2026)

Les treize profils GeneralUser GS et les douze FluidR3 des premières mesures
avaient été convertis À LA MAIN — aucune trace rejouable, aucun manifeste. La
règle du § 9 (« les banques s'installent ») vaut pour l'orchestre comme pour
le piano : `tools/installer-banques-midi.py` porte désormais le manifeste —
URL, licence écrite, crédit, empreinte SHA-256 épinglée — et convertit par
`vsm-sf2` un ensemble CANONIQUE de 45 programmes General MIDI (claviers,
orgues, guitares, basses, cordes, chœurs, cuivres, anches, flûtes, nappes ;
les bruitages et percussions chromatiques rares restent dehors : chaque profil
est une candidate de plus à l'arbitrage, et ce coût se paie à chaque morceau).

Trois banques y figurent, toutes à licence écrite : FluidR3 GM/GS (MIT, Frank
Wen), GeneralUser GS 2.0.3 (licence GeneralUser v2.0, S. Christian Collins) et
MuseScore_General v0.2 (MIT, Wen/Cowgill/Collins) — cette dernière s'ajoute
aux deux « banques visées » du § 5 parce que sa licence est aussi claire
qu'elles et qu'elle échantillonne plus dense (492 zones pour son piano). Le
même programme porte le MÊME nom d'une banque à l'autre (`FR3-Grand-Piano`,
`GU-Grand-Piano`, `MS-Grand-Piano`) : l'arbitrage par profil compare des
timbres, pas des catalogues. Les profils installés avant ce script gardent
leur nom (le script les respecte au lieu de les doubler), et ce qu'une banque
n'a pas est DIT au bilan, jamais tu. Installé ce jour : 109 profils neufs,
141 en tout, 1,3 Go — chaque profil reste sous le budget mémoire du § 3, un
seul étant chargé à la fois.

En rejouant *Clair de Lune*, `vsm.multisample` **n'apparaissait nulle part** :
ni dans les dix finalistes, ni dans les vingt-neuf lignes du tableau
d'arbitrage. Elle n'avait pas perdu — elle n'avait pas été mesurée.

**La cause** : l'arbitrage sur la piste et le réglage sur la piste ne passent pas
par le service de rendu mais par un rendu de PROJET hors ligne, et le projet
qu'ils écrivaient ne portait pas le champ `profile`. La machine y était donc
sans zones, donc muette ; le garde-fou de niveau voyait un RMS nul et écartait la
candidate — **sans un mot**.

**Les deux correctifs, et le second compte davantage.** Le profil suit désormais
la machine dans tous les rendus hors ligne (arbitrage, réglage, verdict du
mélange, export), et le verdict du mélange le met à jour quand il CHANGE de
machine — c'est le seul endroit de la chaîne où un profil peut se retrouver
attaché à la mauvaise. Mais surtout : **une candidate écartée est maintenant
nommée, avec sa raison** (« rendu vide », « silence », « trop faible, il faudrait
×N »). Le § 5 bis de la feuille de route recensait déjà quatre pannes muettes de
la chaîne d'analyse ; celle-ci est la cinquième, et elle a la même forme que les
autres — un résultat qui manque au tableau ressemble en tout point à un résultat
qu'on n'a pas voulu.

### Une hypothèse éprouvée et REJETÉE : la troncature ne claque pas

Le profil tronque les échantillons à six secondes pour tenir dans le budget
mémoire. Une note de piano résonne plus longtemps : coupée net, elle s'arrête
sur une valeur non nulle, et le lecteur devrait produire un clic à chaque note
tenue. C'était l'explication la plus séduisante du résultat décevant, et elle
est FAUSSE — mesurée sur l'échantillon du do médian, couche 11 :

| | valeur |
|---|---|
| pic de l'échantillon | 0,598 |
| dernière valeur avant la coupure | 0,002 |
| discontinuité | **−49 dB** |

À six secondes, la note a déjà perdu cinquante décibels : il n'y a rien à
couper. Le fondu de trente millisecondes a tout de même été ajouté à l'outil,
mais **sans lui attribuer le moindre gain** — c'est un garde-fou pour une autre
banque ou une troncature plus courte, pas un correctif. L'écrire ici évite qu'on
le prenne un jour pour l'explication d'un progrès qu'il n'a pas produit.

### Une deuxième hypothèse éprouvée et rejetée : la vélocité ne compte pas double

L'hypothèse : sur une banque dont les couches encodent déjà la dynamique
(Salamander en enregistre seize), appliquer en plus un gain proportionnel à la
vélocité la compterait DEUX FOIS, et les notes douces sortiraient trop faibles.
Mesuré sur la piste entière de *Clair de Lune*, à patch identique par ailleurs :

| `voice.velocitySensitivity` | distance | RMS du rendu |
|---|---|---|
| 0,00 | 0,3568 | 0,0984 |
| 0,50 | 0,3567 | 0,0853 |
| 1,00 | 0,3571 | 0,0725 |

Un millième d'écart sur toute la course, pour un tiers de niveau en moins. La
raison est écrite dans `vsm_track_arbitration` depuis longtemps et vaut ici :
**la distance est insensible au niveau** — une machine ne gagne pas parce
qu'elle sort plus fort. Le réglage n'est donc pas un handicap, et il ne sera pas
un levier non plus.

Au passage, une mesure utile pour la suite : les 2219 notes transcrites ont des
vélocités comprises entre **61 et 116**, médiane 86. La transcription **écrase
la dynamique** — un piano réel en couvre bien davantage — si bien que la couche
la plus douce du profil n'est **jamais** atteinte. Ce n'est pas un défaut de la
machine, c'est une limite de l'étape d'avant, et elle plafonne ce que n'importe
quelle banque à couches peut rendre.

### La métrique, elle, n'est pas en cause — vérifié

Avant de conclure quoi que ce soit sur la machine, il fallait écarter la
possibilité que la mesure soit aveugle à ce qu'on lui demande de juger. Test :
prendre pour cible **le rendu de `vsm.multisample` lui-même**, et voir si la
métrique la désigne nettement.

| cible = rendu de `vsm.multisample` | distance |
|---|---|
| `vsm.multisample` | **0,0000** |
| `vsm.string` | 0,1648 |
| `vsm.piano` | 0,2404 |
| `vsm.tb303` | 0,3523 |

La bonne machine gagne, et de loin. **La métrique discrimine.** Le résultat qui
suit n'est donc pas un artefact de mesure.

### Troisième hypothèse rejetée : ce n'est pas le « sec contre produit »

La banque est enregistrée au micro rapproché, sèche ; la cible est une version
studio, avec sa salle et son mastering. L'écart pouvait tenir là. Test : la même
réverbération synthétique — du bruit décroissant, volontairement bête, on cherche
si un flou QUELCONQUE rapproche — appliquée aux deux rendus, à niveau égal.

| rendu | sec | + rev. 1,2 s | + rev. 2,5 s |
|---|---|---|---|
| `vsm.multisample` | **0,3571** | 0,4083 | 0,4145 |
| `vsm.piano` | **0,2571** | 0,2892 | 0,2928 |

Le flou **dégrade les deux**, et dans les mêmes proportions (+14 % et +12 %). Il
n'explique donc ni l'écart, ni son sens.

### Le résultat, et ce qu'il faut en dire

Trois causes plausibles ont été éprouvées et écartées — la troncature des
échantillons, le double comptage de la dynamique, l'absence de production — et
la métrique s'est montrée capable de désigner la bonne machine quand la cible
est son propre rendu. Ce qui reste tient donc :

**sur cet enregistrement, avec cette transcription, le piano ÉCHANTILLONNÉ est
mesurablement plus loin de l'original que le piano MODÉLISÉ** (0,3571 contre
0,2571 sur la piste entière, patch d'usine des deux côtés).

C'est l'inverse de ce que le § 1 de ce cahier des charges attendait, et il faut
le dire ainsi plutôt que de le noyer. Trois lectures restent ouvertes, et aucune
n'est tranchée :

1. **La transcription plafonne le résultat.** 2219 notes approchées, dynamique
   écrasée entre 61 et 116 : la reconstruction ne joue pas *Clair de Lune*, elle
   joue une approximation de *Clair de Lune*. Un timbre fidèle rend les erreurs
   de notes AUDIBLES, là où un timbre générique les fond. Une machine peut donc
   perdre pour avoir été trop reconnaissable.
2. ~~**La cible n'est pas le piano de la banque.**~~ — **ÉPROUVÉE ET REJETÉE**
   (23/08). Une seconde banque a été installée, par l'outil SF2 de M2 : YDP
   Grand Piano (Yamaha Disklavier Pro, Zenph Studios / FreePats, CC-BY 3.0),
   150 zones. Sur la même piste et la même mesure :

   | | distance |
   |---|---|
   | `vsm.piano` (modélisé) | **0,2571** |
   | `vsm.string` (corde frappée) | 0,3523 |
   | `vsm.multisample` + Salamander (Yamaha C5) | 0,3571 |
   | `vsm.multisample` + YDP (Disklavier Pro) | **0,4274** |

   Deux pianos réels, enregistrés dans deux studios, sur deux instruments
   différents — et le second fait ENCORE PIRE que le premier. Ce n'est donc pas
   « le mauvais piano » : c'est le report d'échantillon lui-même qui perd sur
   cette cible.
3. **`vsm.piano` est peut-être simplement bon.** C'est la lecture la plus simple,
   et rien ne l'exclut.

**Ce qui permettrait de trancher** : une cible dont on CONNAÎT le contenu — un
enregistrement de piano dont on possède le MIDI exact (une prise Disklavier, ou
un rendu de banque tierce à partir d'un MIDI publié). La transcription
disparaîtrait alors de l'équation, et l'on saurait si la machine perd sur le
timbre ou sur les notes. Tant que cette mesure n'existe pas, **le § 1 de ce
cahier des charges garde sa question ouverte**, et `vsm.multisample` se justifie
par la COUVERTURE qu'elle ouvre (l'orchestre General MIDI par M2) plutôt que par
une victoire sur le piano.

### Quatrième hypothèse rejetée, et un défaut trouvé en la testant

Installer une deuxième banque a coûté une conversion et rapporté deux choses.

**Le résultat** est au § 11 ci-dessus : le YDP Disklavier fait pire que le
Salamander, donc l'écart ne tient pas au choix de l'instrument.

**Le défaut** est plus utile encore, et il était INVISIBLE avec une seule
banque. `vsm-sf2` écrit `X.profile.json` et pose ses échantillons dans un
dossier `X`. La résolution d'un profil par son nom cherchait « existe-t-il
quelque chose qui s'appelle X ? » — et trouvait le DOSSIER. Elle tentait alors
de le lire comme un profil, échouait, et **la machine rendait du silence**. Le
profil Salamander n'avait pas la collision (« Salamander Grand Piano » contre le
dossier `piano-salamander`), si bien que tout marchait.

Corrigé — on exige un FICHIER, pas une entrée quelconque — et un test le
verrouille. La leçon est celle du § 5 bis de la feuille de route sous une forme
de plus : *un banc à un seul cas ne teste pas ce qui distingue les cas.* Deux
banques valent mieux qu'une, non pour la couverture, mais pour le défaut que la
seconde révèle.
