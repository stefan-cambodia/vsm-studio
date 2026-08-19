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

- 18 machines (+ tonalité d'essai), 9 effets, moteur temps réel, 588 tests
  verts, zéro warning.
- Piano roll complet, façades « façon hardware » pour les 18 machines,
  séquenceurs à pas pour celles qui en ont un.
- Interop : identités sémantiques (~470 paramètres), presets `*.synth.json`,
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
- La couverture instrumentale s'est comblée côté synthèse (six machines
  ajoutées, voir [`CDC-machines-manquantes.md`](CDC-machines-manquantes.md)),
  mais **basse, guitare et cordes réelles** passent toujours par le sampler
  faute de modèle dédié.
- Une recherche coûte ~13 s par note et par machine au budget par défaut, ~43 s
  au budget quadruplé. Un morceau en compte des centaines : c'est l'objet de
  la phase 10.

---

## 2. Phase 8 — Couvrir les sources réelles

**But** : qu'un stem quelconque ait une machine cible plausible.

| Étape | Contenu | Terminé quand |
|---|---|---|
| ~~8.1~~ | ~~`vsm.sampler`~~ **fait, seize emplacements** | un stem de batterie découpé par `analyse/` se rejoue et la distance à l'original est mesurée |
| ~~8.2~~ | ~~Profil de recherche déclaré par machine~~ **fait** — `interchange/SearchProfile`, exposé par `vsm-render --serve` (`query: searchProfile`), consommé par `VsmEngine.search_profile` | l'optimiseur ne code plus aucune borne ; mesure A/B publiée ci-dessus |
| 8.3 | `vsm.generic` (synthé neutre, paramètres continus et monotones) | sur cinq stems synthétiques, distance **inférieure** à la meilleure machine de caractère |
| 8.4 | `vsm.drumkit` (profil batterie du sampler) | façade à colonnes + grille 16 pas, sur le moteur du sampler |

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

**Un défaut du vocabulaire sémantique, révélé au passage.** `filter.N.cutoff`
ne dit pas le TYPE du filtre. Sur le Juno-106, le coupe-bas (`filter.2.cutoff`,
une commande mineure) reçoit donc presque la même importance que le passe-bas
principal et occupe le rang 3 de l'espace cherché. Corriger demandera de
distinguer les types dans la table sémantique, ce qui touche ses tests de
complétude : à faire, mais pas en passant.

**La séparation en stems n'est pas un confort.** Le même mélange traité en une
seule piste (`--sans-separation`) donne une distance de 12,3 au lieu de 0,67 :
une machine ne reproduit pas deux instruments à la fois.

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
- **Un modèle appris de bout en bout (audio → paramètres).** Séduisant, mais il
  faudrait un corpus étiqueté que personne n'a ; et le moteur, lui, sait déjà
  produire des paires (paramètres → audio) à volonté. C'est la piste à garder
  en réserve : générer un corpus **avec le moteur** pour entraîner un
  estimateur qui donnerait le point de départ de la recherche (10.2).

---

## 7. Invariants à vérifier à chaque étape

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
