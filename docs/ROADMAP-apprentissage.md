# Feuille de route — Apprentissage (phases A0 à A5)

Prolonge [`ROADMAP-fusion.md`](ROADMAP-fusion.md) (phases 8-11, terminées).
Le cahier des charges est dans [`CDC-apprentissage.md`](CDC-apprentissage.md) ;
cette feuille ne répète pas ses exigences, elle les ordonne et fixe les
critères de clôture.

**L'ordre n'est pas chronologique par confort : il va du gain le plus sûr au
plus risqué.** A2 (batterie) est indépendante et son juge existe déjà ; A1
(classifieur) est bon marché et comble un trou mesuré ; A3 (estimateur) porte
le risque des pistes 10.1/10.2, déjà rejetées deux fois sous d'autres formes —
elle passe en dernier et peut être rejetée à son tour, chiffres à l'appui.

---

## Phase A0 — Corpus et infrastructure

Le socle de tout le reste : sans corpus regénérable, aucune mesure d'A1-A3
n'est rejouable.

| Étape | Contenu | Terminé quand |
|---|---|---|
| A0.1 | Générateur de corpus (`vsm_corpus.py`) : tirage dans `SearchProfile`, rendus par `VsmEngine`, caractéristiques v2, manifeste | une commande regénère le corpus des 15 machines de recherche ; coût mesuré et publié |
| A0.2 | Déterminisme des données | deux exécutions même graine → caractéristiques identiques au bit près (testé) |
| A0.3 | Péremption par empreinte | changer artificiellement une empreinte marque le corpus périmé et fait refuser le modèle dérivé (testé) |
| A0.4 | Augmentations seedées (EQ, réverbération courte, compression, bruit, fuite, désaccord) | chaque augmentation inscrite au manifeste ; A/B possible avec et sans |

**Critère de phase** : le corpus complet se regénère sans intervention en
moins d'une nuit, et sa taille/coût réels remplacent les ordres de grandeur
du CDC §3.

> **A0 EST FAITE, ET LE COÛT EST MESURÉ (23/08/2026).** Le socle vit dans
> `analyse/analyzer/vsm_corpus_build.py`, la commande est `analyse/corpus.py`,
> et douze tests (`analyse/tests/test_corpus.py`) tiennent les deux exigences
> marquées « testé ».
>
> | | mesuré |
> |---|---|
> | débit | **98 exemples/s** (31 972 exemples, 20 machines, 326 s) |
> | à 10 000 patchs par machine | **27 min par machine**, **9,1 h** pour les vingt |
> | estimation du CDC § 3 | « ≈ 20 min par machine » — juste |
>
> Le critère « moins d'une nuit » est donc tenu, mais de peu : neuf heures pour
> vingt machines, et le parc ne fera que grandir. Une anomalie est relevée au
> passage et attend son enquête : **`vsm.tonewheel` rend à 25/s** contre 98 en
> moyenne, quatre fois plus lent que tout le reste du parc.
>
> **Trois écarts au cahier, tranchés en écrivant :**
>
> 1. **Le corpus prend 20 machines, pas 15.** Le § 3 parlait de « 15 machines
>    de recherche » ; le moteur en déclare 25, dont 20 candidates mélodiques.
>    Ce sont celles-là que le corpus peuple : le synthé de test et les boîtes à
>    rythmes y ajouteraient des classes que personne n'a à reconnaître sur un
>    stem mélodique, et les percussions relèvent d'A2, qui a son propre corpus.
> 2. **L'empreinte est calculée EN FAISANT JOUER la machine**, pas en lisant
>    `audio/tests/audio_fingerprints.inc` — l'exigence n° 3 du § 3 interdit
>    toute ligne nouvelle côté C++. Bénéfice non prévu : elle capte aussi le
>    PROFIL installé. Retirer le profil de `vsm.multisample` fait passer son
>    corpus en « invérifiable », ce qu'une table du dépôt n'aurait pas su dire.
> 3. **La péremption a TROIS réponses, pas deux** : à jour, périmé, et
>    *invérifiable* (le moteur ne sait plus faire jouer la machine). Confondre
>    les deux dernières laisserait passer pour « à jour » un corpus qu'on ne
>    peut plus vérifier.
>
> **Un piège évité, et il valait la peine.** La graine par machine dérivait de
> `hash(nom)`. Python randomise le hachage des chaînes à chaque démarrage : deux
> générations « à la même graine » auraient tiré des patchs différents. Un
> corpus qu'on CROIT regénérable et qui ne l'est pas est pire qu'un corpus non
> regénérable — rien ne le signale, et toutes les mesures faites dessus
> deviennent incomparables en silence. Remplacé par un SHA-256 tronqué, et un
> test verrouille la valeur.

## Phase A1 — Le classifieur de machine

| Étape | Contenu | Terminé quand |
|---|---|---|
| A1.1 | Entraînement + banc tenu à l'écart | vraie machine top 3 ≥ 95 % (cas indistinguables comptés à part) ; deux entraînements → mêmes verdicts |
| A1.2 | Abstention | stems acoustiques (violon, piano) → « aucune machine », avec la mesure qui le motive ; jamais un score confiant hors du parc |
| A1.3 | Intégration en présélection dans `reconstruire.py` | sur le banc 10.3 (4 cibles) et les vérités terrain : même gagnant final que la recherche complète ; temps de présélection avant/après publié |
| A1.4 | Rejouer House Of God | verdicts finaux inchangés ou meilleurs ; le rapport porte version du modèle et décisions imprimées |

**Critère de phase** : la présélection deux passes (174 s) est remplacée ou
réduite par le classement quasi gratuit, **sans qu'aucun verdict du banc ne se
dégrade**. Si un verdict se dégrade, le classifieur nourrit la shortlist au
lieu de la remplacer, et c'est écrit.

> **A1.1 EST ATTEINTE. A1.2 NE L'EST PAS. ET A1.3 EST TRANCHÉE PAR LA MESURE,
> AVANT MÊME D'ÊTRE ÉCRITE (23/08/2026).**
>
> Corpus de 95 894 exemples (20 machines, 300 patchs, 16 notes par patch, moitié
> augmentés). Modèle : gradient boosting par histogrammes, CPU, 25 s
> d'entraînement. Épreuve sur des **patchs jamais vus**.
>
> **A1.1 — sur ce que le moteur produit : atteinte, et largement.**
>
> | | top 1 | top 3 | top 5 |
> |---|---|---|---|
> | tous les exemples | 94,8 % | 99,2 % | 99,7 % |
> | hors indistinguables | **98,5 %** | **99,9 %** | 100,0 % |
>
> 24,5 % des exemples sont déclarés indistinguables — le son le plus proche du
> corpus vient d'une AUTRE machine —, et les confusions sont exactement celles
> que le § 1.4 annonçait : `minimoog ↔ prophet`, `juno106 ↔ arpodyssey`,
> `jupiter8 → juno106`. Plusieurs soustractifs produisent le même son ; le
> modèle ne fera pas mieux que le signal, et c'était écrit avant de mesurer.
>
> **A1.2 — sur un piano réel : le mécanisme du § 4 est insuffisant, mesuré.**
>
> Le § 4 propose deux garde-fous : un seuil sur le score, un rayon de nouveauté.
> Éprouvés sur *Clair de Lune* (20 extraits d'une seconde) :
>
> - **le seuil de score ne sert à rien.** Un classifieur à ensemble fermé
>   choisit toujours l'une de ses vingt classes, et il est confiant : sur un
>   piano, il a annoncé `vsm.sh101` avec un score de **1,00**. Aucun seuil
>   raisonnable n'attrape cela ;
> - **le rayon marche, mais son calibrage d'origine était faux.** Posé au
>   quantile 99,5 % du corpus (6,95), il n'écartait que **1,7 %** du réel. Les
>   distributions se séparent pourtant nettement — médiane 2,22 pour le corpus,
>   5,17 pour le réel : l'erreur était de calibrer sur le corpus SEUL, ce qui
>   répond à « où finit le corpus » quand la question est « où finit le parc ».
>
> Le rayon est passé au quantile 90 % (3,72), et le choix se justifie par une
> **asymétrie** : un refus abusif coûte du TEMPS — la chaîne retombe sur la
> présélection d'aujourd'hui —, une désignation abusive coûte un VERDICT, et
> celui-là se propage sans que personne le voie.
>
> | rayon | abstentions sur le piano réel | refus abusifs sur le corpus |
> |---|---|---|
> | 6,95 (quantile 99,5 %) | 1,7 % | 0,7 % |
> | **3,72 (quantile 90 %)** | **75 %** | 10 % |
>
> Il reste **quatre désignations confiantes sur vingt** (jusqu'à `vsm.sh101` à
> 0,96 et `vsm.pcmhybrid` à 0,99) sur une source acoustique. **A1.2 n'est donc
> pas atteinte**, et le § 4 ne se contente pas d'un progrès : il dit que ce cas
> serait « le pire résultat possible de ce projet ».
>
> **A1.3 — le classifieur ne doit PAS remplacer la présélection, et la mesure
> le dit sans ambiguïté.** Sur *Clair de Lune*, l'arbitrage sur la piste retient
> `vsm.piano`. Le classifieur, lui, la place au **rang médian 16 sur 20**, et
> dans son top 5 seulement **12 %** du temps :
>
> | machine | rang médian | dans le top 5 |
> |---|---|---|
> | `vsm.sh101` (que le classifieur préfère) | 2 | 52 % |
> | `vsm.multisample` | 11 | 28 % |
> | **`vsm.piano` (la gagnante réelle)** | **16** | **12 %** |
>
> Un dégrossissage à cinq machines aurait donc **éliminé la gagnante**. Le
> critère de phase prévoyait ce cas et sa conséquence : *« le classifieur
> nourrit la shortlist au lieu de la remplacer, et c'est écrit »*. C'est écrit.
>
> **Ce que tout cela dit, et c'est la même chose qu'au § 7 de
> `ROADMAP-fusion.md`.** Le classifieur est excellent sur ce que le MOTEUR
> produit et mauvais sur ce qu'un DISQUE contient. C'est mot pour mot le fossé
> de domaine qui avait tué l'estimateur de paramètres.

### A0.4 — l'A/B des augmentations : elles marchent, et elles ne comblent pas le fossé

Le § 7 annonçait l'augmentation comme la parade au fossé de domaine, et le
§ 7 lui-même demandait l'A/B. Il est fait, et il donne deux réponses opposées
qu'il faut énoncer toutes les deux.

**Le protocole d'abord**, parce qu'il a fallu corriger le code pour qu'il ait un
sens : le tirage des augmentations puisait dans le même flux aléatoire que celui
des patchs, si bien qu'un corpus sec et un corpus augmenté « à la même graine »
ne contenaient pas les mêmes sons. Les comparer aurait mesuré deux choses à la
fois. Deux flux séparés depuis, et un test vérifie la propriété : **95 870
exemples de part et d'autre, mêmes patchs, mêmes notes, seuls les descripteurs
diffèrent.**

**Première réponse : en domaine, l'augmentation fait exactement ce qu'on lui
demande, et gratuitement.**

| entraîné sur | épreuve sèche | épreuve augmentée |
|---|---|---|
| corpus sec | **99,7 %** | 93,6 % |
| corpus augmenté | 99,6 % | **99,0 %** |

Le modèle sec perd six points dès qu'on dégrade ; le modèle augmenté n'en perd
presque aucun, et **ne paie rien** sur le son propre (99,6 contre 99,7). C'est un
gain réel, et il faut le porter au crédit de la méthode.

**Seconde réponse : sur un disque, cela ne change RIEN.**

| entraîné sur | abstentions (40 extraits) | confiantes à tort | rang médian de `vsm.piano` |
|---|---|---|---|
| corpus sec | 31/40 | 6 | 15 |
| corpus augmenté | 30/40 | 7 | 15 |

À un extrait près, dans le mauvais sens. **Les augmentations rendent le modèle
robuste aux dégradations qu'elles contiennent, pas à un enregistrement réel.**

**Et une mesure explique une bonne part du résultat.** Combien chaque
augmentation déplace-t-elle réellement le descripteur, en écarts-types du
corpus ?

| augmentation | déplacement |
|---|---|
| bruit | **0,359 σ** |
| réverbération | 0,093 σ |
| désaccord | 0,074 σ |
| égaliseur | 0,060 σ |
| **fuite** | **0,054 σ** |
| **compression** | **0,018 σ** |

Cinq des six ne déplacent presque rien. Le « corpus augmenté » est, pour
l'essentiel, un corpus bruité. La conclusion honnête n'est donc pas *« les
augmentations ne servent pas »* mais **« celles-ci ne dégradent presque rien, et
même ainsi le fossé ne se comble pas »** — ce ne sont pas les mêmes phrases, et
la première fermerait à tort une piste que la seconde laisse ouverte.

**Ce qu'il fallait avant de conclure : les deux augmentations molles corrigées,
et remesurer.** C'est fait, et la réponse est nette.

`fuite` mélangeait le rendu PRÉCÉDENT DE LA MÊME MACHINE, souvent du même patch :
un écho, pas une fuite. Elle prend désormais le rendu d'une autre machine.
`compression` était une courbe sans mémoire ; elle a maintenant un détecteur
d'enveloppe à attaque et relâchement. Les deux mordent nettement plus :

| augmentation | v1 | **v2** |
|---|---|---|
| fuite | 0,054 σ | **0,215 σ** (×4) |
| compression | 0,018 σ | **0,053 σ** (×3) |
| bruit | 0,359 σ | 0,362 σ |
| réverbération | 0,093 σ | 0,102 σ |
| désaccord | 0,074 σ | 0,083 σ |
| égaliseur | 0,060 σ | 0,070 σ |

*(La ligne « (sec) » du tableau brut affiche 0,012 σ : 95 % des exemples non
dégradés de v2 sont STRICTEMENT identiques à ceux du corpus sec, et les 5 %
restants sont un résidu d'alignement du tirage — pas une dégradation.)*

**Et sur la colonne qui décide, rien ne bouge :**

| entraîné sur | abstentions (40 extraits) | confiantes à tort | rang médian de `vsm.piano` |
|---|---|---|---|
| corpus sec | 31/40 | 6 | **15** |
| augmenté v1 (molles) | 30/40 | 7 | **15** |
| augmenté v2 (mordantes) | 31/40 | 8 | **15** |

Le rang médian de la machine que l'arbitrage retient réellement est **identique
aux trois** : la vue que le modèle a d'un piano réel ne bouge pas d'un cran quand
on quadruple la force des dégradations.

### Le contrôle qui manquait : un morceau de SYNTHÉS

Toutes les mesures ci-dessus portaient sur un piano — un son hors du parc. Une
objection restait donc ouverte : le classifieur échouerait-il parce que la
cible est acoustique ? Un morceau de synthés (*B4 Wuz Then*, 354 s, quatre
stems) y répond, stem par stem, après séparation :

| stem | extraits sonores | abstentions | distance médiane au corpus |
|---|---|---|---|
| `bass` | 70 | **96 %** | 5,21 |
| `other` (nappes, leads) | 117 | **100 %** | 6,47 |
| *Clair de Lune, piano, pour situer* | *40* | *78 %* | *5,17* |

Un stem de synthés — des sons que le parc sait produire — est **aussi loin du
corpus qu'un piano acoustique**. L'objection tombe : le classifieur n'échoue pas
parce que le piano est hors parc, il échoue parce qu'un **stem séparé** est hors
domaine, quel que soit l'instrument. C'est mot pour mot la phrase du § 7 de
`ROADMAP-fusion.md` : *un stem séparé est un son qu'aucune machine ne produit*.

Et les trois fois sur soixante-dix où il s'est prononcé sur la basse, il a dit
`vsm.sh101` à 0,99 ; l'arbitrage sur la piste a retenu `vsm.obx` à 0,206. Faux
les trois fois, avec un score de 0,99.

**Un incident au passage, et il est rassurant.** La chaîne a REFUSÉ le
classifieur : installer une seconde banque de piano avait changé en silence le
profil que `vsm.multisample` charge (ordre alphabétique), donc son empreinte, et
le modèle entraîné sur l'ancien son a été écarté — la chaîne continuant sans lui,
en le disant. La péremption (A0.3) a fait son travail en conditions réelles, sans
qu'on l'ait provoquée. Le choix silencieux du profil, lui, était un défaut :
corrigé (`VSM_PROFIL`, et le choix est imprimé quand il y en a plusieurs).

### Verdict sur la parade du § 7

**Elle est mesurée insuffisante, et pas parce que la dose était trop faible** —
c'était l'hypothèse de repli, elle est éprouvée et écartée. Un corpus de rendus
moteur, si dégradé soit-il SYNTHÉTIQUEMENT, n'enseigne pas ce qu'un disque
contient.

C'est le même mur, mesuré une seconde fois par un chemin indépendant, que celui
du § 7 de [`ROADMAP-fusion.md`](ROADMAP-fusion.md) sur l'estimateur de
paramètres. Et cela renforce la condition de réouverture qui y est écrite :
**il faudrait un corpus qui contienne la dégradation RÉELLE** — rendre le patch,
le mélanger à d'autres stems, faire repasser le tout par demucs, et étiqueter le
résultat avec le patch d'origine. Ce n'est plus une intuition : deux objets
d'apprentissage différents, entraînés sur deux corpus différents, butent au même
endroit pour la même raison.

Ce que l'augmentation apporte reste vrai et n'est pas rien : **une robustesse
gratuite aux dégradations qu'elle contient** (93,6 % → 99,0 % sur l'épreuve
dégradée, sans rien perdre sur le son propre). Elle est donc conservée. Elle ne
doit simplement plus être présentée comme la parade au fossé de domaine.

> **LA CONDITION DE RÉOUVERTURE EST MESURÉE (28/08/2026) : LE CORPUS SÉPARÉ
> APPREND LA SÉPARATION, PAS LE DISQUE.** `analyse/corpus_separe.py` engendre
> le corpus que le § 7 réclame — des rendus du moteur mélangés à de VRAIS stems
> séparés (batterie, basse, voix de *House Of God* ; batterie et voix SEULES de
> *Children* et *B4 Wuz Then*), repassés par demucs, redécoupés et étiquetés
> avec le patch d'origine — puis mesure trois classifieurs à coupure égale
> (sec, séparé, les deux) sur le séparé tenu à l'écart et sur cinq SONDES
> réelles : un enregistrement, les notes que la chaîne y a transcrites (lues
> dans le MIDI du projet), et la machine que l'arbitrage y a retenue. Le rang
> médian de cette machine, sur vingt extraits, est le seul chiffre qui dise si
> le modèle lit un disque.
>
> **Un incident d'abord, et il a coûté une nuit.** La première passe (20
> machines × 25 patchs × 4 conditions, 43 min d'audio concaténé) a été tuée
> par l'OOM-killer pendant la séparation : `separate_tensor` tient en mémoire
> l'entrée, sa copie normalisée, les quatre sources et leur copie — plus de
> 10 Go sur 15, sans GPU. La séparation se fait désormais par tranches de
> quatre minutes bordées d'un silence entre deux exemples, avec dix secondes
> de marge séparées puis jetées de chaque côté (sans marge, deux ou trois
> exemples par bord tombaient à 0,7-0,85 de corrélation avec la passe unique ;
> avec, ≥ 0,93 partout) ; 1,8 Go de RSS au plus, testé sans demucs. Corpus :
> **1 992 exemples en 864 s** (8 rendus inaudibles rejetés), énergie du synthé
> retrouvée dans « other » : médiane 0,97, quart inférieur 0,89.
>
> **Première réponse, en corpus : le fossé est mesuré, et le corpus séparé le
> comble en partie — mais la séparation elle-même a un plafond.** Coupure par
> patch, 396 exemples tenus à l'écart, mêmes patchs d'un côté et de l'autre :
>
> | entraîné sur | épreuve sèche top 1 / 3 | épreuve séparée top 1 / 3 |
> |---|---|---|
> | sec | **82 % / 94 %** | 25 % / 49 % |
> | séparé | 56 % / 76 % | **56 % / 73 %** |
> | sec + séparé | 80 % / 91 % | 55 % / 74 % |
>
> Le modèle sec perd la moitié de son top 3 dès que les MÊMES sons passent par
> demucs (94 → 49) : c'est le fossé de domaine, chiffré pour la première fois
> à patchs égaux. Entraîner sur le séparé en rattrape la moitié (49 → 73), et
> « sec + séparé » garde les deux (91 sur le sec, 74 sur le séparé) sans rien
> payer. Mais 56 % de top 1 est un PLAFOND DU DOMAINE, pas du corpus : par
> machine, `vsm.pcmhybrid` tombe à 0 %, `vsm.obx` à 17 %, `vsm.prophet` à
> 19 %, `vsm.ms20` à 21 % — même pour un modèle qui n'a vu que du séparé —
> quand `vsm.piano` tient 94 %, `vsm.tonewheel` 95 %, `vsm.dx7` 93 %. **Un
> stem séparé n'a plus l'identité de plusieurs soustractifs** ; ce n'est pas
> une question d'apprentissage.
>
> Et un cas dit ce que le modèle apprend réellement : `vsm.sh101` ne laisse
> que **23 %** de son énergie dans « other » (c'est une basse, demucs l'envoie
> dans `bass`), et le modèle séparé la reconnaît pourtant à **100 %** — il a
> appris le RÉSIDU que demucs laisse d'un synthé de basse, et c'est une
> empreinte plus nette que le son lui-même.
>
> **Seconde réponse, sur les disques : non.** Rang médian de la machine que
> l'arbitrage retient (1 = le modèle la met première), et part des extraits où
> elle est dans le top 5 :
>
> | sonde (machine retenue) | sec | séparé | sec + séparé |
> |---|---|---|---|
> | *Clair de Lune* (`vsm.piano`) | **12** / 5 % | 16 / 15 % | 16 / 5 % |
> | *B4* other (`vsm.jupiter8`) | **4** / 55 % | 12 / 25 % | 6 / 40 % |
> | *B4* bass (`vsm.supersaw`) | 14 / 0 % | 4 / 85 % | **2** / 55 % |
> | *Children* other (`vsm.string`) | **3** / 80 % | 8 / 25 % | 12 / 5 % |
> | *Children* bass (`vsm.piano`) | 15 / 0 % | 17 / 0 % | 18 / 0 % |
> | *somme des rangs* | **48** | 57 | 54 |
>
> Une sonde progresse spectaculairement — la basse de *B4* passe du rang 14 au
> rang 4 (rang 2 avec les deux corpus), et de 0 à 85 % dans le top 5 — ; trois
> reculent (*B4* other 4 → 12, *Children* other 3 → 8, *Clair de Lune*
> 12 → 16) ; la cinquième reste au fond pour les trois modèles. Le total est
> moins bon avec le corpus séparé qu'avec le sec.
>
> **Et la ligne qui explique le tableau : ce que le modèle séparé met en
> PREMIER sur un disque.** Sur *B4* bass, *B4* other et *Children* other, sa
> réponse la plus fréquente est **`vsm.sh101`** (7, 9 et 9 extraits sur 20) —
> la machine du résidu. Il a appris qu'un stem où il manque du grave est un
> synthé de basse passé par demucs, et il en voit un dans chaque stem réel.
> Sur la basse de *B4*, où c'est à peu près vrai, il gagne ; ailleurs, il
> perd. **Le corpus séparé enseigne les artefacts de la séparation, et rien
> d'autre** — c'est exactement ce qu'il contient.
>
> **Ce que la « dégradation réelle » du § 7 est, alors.** Deux choses, et ce
> corpus n'en porte qu'une. Les artefacts de demucs, d'abord : ils sont dedans,
> et là où ils dominent (un stem de basse, le plus filtré des quatre), le gain
> est net et mesuré. Puis la distance entre un instrument RÉEL dans un mixage
> réel et ce que le parc en rend : elle n'y est pas, et un corpus de rendus ne
> peut pas la contenir, parce que la machine « retenue » sur un disque n'est
> jamais celle qui l'a fait — c'est la plus PROCHE que l'arbitrage ait
> trouvée. Un piano Yamaha enregistré n'est pas `vsm.piano` ; la nappe de
> *Children* n'est pas `vsm.string`. Le classifieur d'A1 apprend « quelle
> machine a produit ce son », et sur un disque, la réponse vraie est
> « aucune » — ce que l'abstention d'A1.2 dit déjà, dans 75 à 100 % des cas.
>
> **Verdict, à cette taille : la condition du § 7 est éprouvée et ne rouvre
> pas A3.** Un modèle entraîné sur le séparé ne lit pas mieux un disque qu'un
> modèle entraîné sur le sec ; il lit mieux un STEM DE BASSE, et c'est un
> gain réel mais étroit, qui vaudrait pour la présélection d'une piste de
> basse — pas pour un estimateur de paramètres, qui a besoin de l'identité
> fine que la séparation détruit (0 % à 21 % pour quatre soustractifs).
>
> **L'hypothèse de repli — « 25 patchs par machine, c'est trop peu » — est
> éprouvée, et le verdict est définitif.** Même protocole, mêmes sondes, à
> **100 patchs** par machine : 7 990 exemples, 173 min d'audio séparé en
> 3 708 s (énergie retrouvée : médiane 0,97, quart inférieur 0,83). En
> corpus, quatre fois plus d'exemples font ce qu'on attend d'eux — le modèle
> séparé passe de 56 à **69 %** de top 1 sur le séparé tenu à l'écart (86 %
> en top 3), et « sec + séparé » tient 94 % sur le sec et 66 % sur le séparé.
> Sur les disques, rien ne bouge :
>
> | sonde (machine retenue) | sec | séparé | sec + séparé |
> |---|---|---|---|
> | *Clair de Lune* (`vsm.piano`) | **12** / 0 % | **12** / 5 % | 16 / 0 % |
> | *B4* other (`vsm.jupiter8`) | **3** / 70 % | 8 / 30 % | 7 / 30 % |
> | *B4* bass (`vsm.supersaw`) | 16 / 0 % | 5 / 55 % | **4** / 60 % |
> | *Children* other (`vsm.string`) | **2** / 95 % | 13 / 5 % | 16 / 0 % |
> | *Children* bass (`vsm.piano`) | **12** / 0 % | 16 / 0 % | 16 / 0 % |
> | *somme des rangs* | **45** | 54 | 59 |
>
> La somme des rangs, à 25 patchs : 48 / 57 / 54 ; à 100 : 45 / 54 / 59. Le
> corpus séparé reste derrière le sec, de la même marge, et le seul gain
> reste le même — un stem de basse (16 → 5). Le modèle sec, lui, profite des
> 100 patchs sur les deux stems « other » (3 et 2), ce qui dit où la marge
> restait : dans la couverture des patchs, pas dans le domaine. **A3 n'a pas
> de corpus.** La « dégradation réelle » du § 7 qu'un corpus de rendus peut
> contenir — les artefacts de demucs — est apprise, et elle n'est pas ce qui
> sépare un rendu d'un disque. Ce qui l'en sépare, c'est que le disque n'a
> pas été fait par le parc ; aucun étiquetage par le patch d'origine ne
> contient cette information, parce qu'il n'y a pas de patch d'origine.
> L'estimateur de paramètres est clos, avec deux mesures indépendantes
> (ROADMAP-fusion § 7, ici) et l'épreuve de sa condition de réouverture.
> Le code de la mesure reste (`corpus_separe.py`, 4 tests) : c'est lui qui
> devra être rejoué si un jour un corpus d'une autre nature — des disques
> dont on connaît la machine — existe.
>
> **UNE SIXIÈME ET UNE SEPTIÈME SONDE, SUR UN MORCEAU QUE LE VERDICT N'AVAIT
> JAMAIS VU (29/08/2026).** Le verdict ci-dessus contenait une prédiction sans
> le dire : *le corpus séparé n'aide que sur un stem de BASSE, et nuit
> ailleurs.* Elle reposait sur cinq sondes tirées de trois morceaux. *Sky and
> Sand* (Fritz Kalkbrenner) est passé dans la chaîne pour d'autres raisons ;
> ses deux stems mélodiques ont été versés au protocole, avec la machine que
> l'arbitrage y retient (`other` → `vsm.multisample`, `bass` →
> `vsm.wavetable`) et les notes du MIDI reconstruit. Mêmes trois modèles,
> même corpus à 100 patchs, aucun réentraînement particulier :
>
> | sonde (machine retenue) | sec | séparé | sec + séparé |
> |---|---|---|---|
> | *Sky* other (`vsm.multisample`) | **5** / 55 % | 8 / 30 % | 14 / 0 % |
> | *Sky* bass (`vsm.wavetable`) | 14 / 0 % | **5** / 55 % | **5** / 55 % |
>
> **La prédiction tient, et à l'unité près.** Le stem de basse passe du rang
> 14 au rang 5 et de 0 à 55 % dans le top 5 — exactement ce qu'avait fait la
> basse de *B4* (16 → 5, 0 → 55 %) ; le stem « other » recule (5 → 8). Somme
> des rangs sur les sept sondes : **64** pour le sec, 67 pour le séparé, 78
> pour les deux. Rien ne bouge, sur un morceau électronique de 2010 qui n'a
> rien à voir avec les trois précédents.
>
> Ce que cela ajoute au verdict n'est pas un chiffre de plus : c'est qu'il a
> **prédit** le résultat d'une mesure qui n'était pas faite. Le corpus séparé
> apprend le résidu que demucs laisse d'un synthé de basse ; là où le stem est
> une basse, il gagne, ailleurs il perd, et cela se reproduit sur commande. La
> clôture d'A3 ne repose donc plus sur une constatation mais sur une régularité
> vérifiée hors de son échantillon.

## Phase A2 — Les gabarits de batterie appris

> **CONTRE-MESURE, ET ELLE EST DURE (02/09/2026).** Ce modèle est meilleur
> à son banc — charleston 16/16 au lieu de 8/16, zéro kick inventé — et
> **il éloigne le MORCEAU de 10,4 %** : *Us and Them*, trois courses à une
> variable près, 0,1910 sans lui contre 0,2108 avec (détail et tableau au
> § 5 duodecies de [`ROADMAP-fusion.md`](ROADMAP-fusion.md), hypothèse H7).
> Le stem de batterie, lui, ne bouge que de 2,4 % : c'est le mélange qui
> paie. La cause probable est écrite là-bas (les deux pièces
> supplémentaires sont celles dont le journal dit que l'échantillon
> « contient les autres pièces ») et sa correction était l'hypothèse H8 —
> **mesurée le jour même, elle ne récupère qu'un quart de l'écart**
> (10,4 % → 7,4 %). Le reste vient de la NOMINATION elle-même : à pièces
> égales, le classifieur déplace 285 frappes d'une famille à l'autre, et le
> morceau s'en éloigne. La question que la phase A2 doit désormais porter
> n'est plus « nomme-t-on mieux » mais **« une frappe mieux nommée est-elle
> mieux jouée »** — et la réponse mesurée, pour l'instant, est non.
>
> **Ce que cela ne dit PAS** : que le modèle est mauvais. Il fait ce qu'on
> lui demande, mieux que le repli par bandes. Ce que cela dit, c'est que
> son critère de banc — nommer les pièces — n'est pas le critère du
> projet, qui est la distance au morceau. Un banc qui n'est pas le juge
> final doit porter cette mention, sans quoi « meilleur » se lit comme
> « meilleur pour ce qui compte ».


Indépendante d'A1/A3 ; peut commencer dès A0.1 (les frappes générées suffisent).

| Étape | Contenu | Terminé quand |
|---|---|---|
| A2.1 | Corpus de frappes : TR-808/909 + kits sampler, variations, superpositions construites | frappes étiquetées par construction, cas mêlés inclus (les tueurs des architectures 1-3) |
| A2.2 | Nommage des gabarits par modèle appris dans `vsm_drumkit.py` | motif-vérité « double-croche » : charleston > 24/32 (contre 8/32), kick et snare toujours 8/8, zéro frappe inventée |
| A2.3 | Contretemps et familles supplémentaires | motif « contretemps » > 14/16 ; toms/percussions/claps nommables (au-delà de 3 familles) |
| A2.4 | Rejouer House Of God | co-frappes toujours 0 ; ≥ 6 pièces ; l'écoute ne révèle pas de régression |

**Critère de phase** : la limite écrite au §9.5 (« gabarits appris, pas un
seuil mieux choisi ») est levée, motifs-vérité à l'appui. En cas d'échec, le
nommage actuel reste en place et l'échec est documenté avec ses chiffres.

> **A2.0 — LE JUGE EXISTE ENFIN COMME SCRIPT (23/08/2026).** Les motifs-vérité
> du § 9.5 avaient été mesurés à la main, à trois architectures d'intervalle, et
> les chiffres se contredisaient déjà : ce cahier écrit « 8/32 aujourd'hui » là
> où l'en-tête de `vsm_drumkit.py` mesure 46/64. `analyse/banc_batterie.py`
> (module `vsm_drum_bench.py`, quatre tests) rejoue les deux motifs en une
> commande, et c'est ce tableau — et lui seul — qui dit désormais où en est le
> détecteur. **Point de départ mesuré, tolérance 30 ms, 140 BPM :**
>
> | motif | pièce | retrouvées | inventées | confondues avec |
> |---|---|---|---|---|
> | double-croche (TR-909) | kick | 16/16 | **15** | — |
> | | snare | **0/8** | 0 | kick ×8 |
> | | hihat | 33/64 | 0 | kick ×31 |
> | contretemps (TR-808) | kick | 16/16 | **8** | — |
> | | snare | **0/8** | 0 | kick ×8 |
> | | hihat | 8/16 | 0 | kick ×8 |
>
> **La lecture qui oriente A2** : les INSTANTS sont justes — 64 et 32 frappes
> détectées, exactement le compte — et c'est le NOMMAGE qui défaille. Sur le
> motif B, huit charlestons qui frappent SEULES, sans aucune autre pièce
> dessous, sont nommées « kick ». Ce n'est pas une frappe manquante ni une
> frappe inventée, c'est une frappe mal nommée, et le banc la compte à part
> (« confondues avec »), parce que ce n'est pas le même défaut. La caisse
> claire, qui ne frappe jamais seule sur ces motifs, reste fusionnée à sa
> porteuse — la limite documentée du module, et elle est hors de portée d'un
> nommage appris tant qu'elle n'a pas d'empreinte propre.
>
> Les critères A2.2 et A2.3 se reformulent donc contre ce banc : *charleston
> seule nommée charleston sur B : 16/16 (aujourd'hui 8/16) ; kicks inventés sur
> A : 0 (aujourd'hui 15)*, sans qu'aucune frappe ne soit perdue.
>
> **Le mécanisme exact, trouvé avec le banc, et il confirme le § 1.3 du cahier
> au lieu de le contredire.** Sur le motif B, les charlestons mal nommées sont
> exactement **une sur deux : celles qui suivent un temps avec caisse claire**.
> Leur empreinte de nouveauté est GRAVE (part ≥ 3,5 kHz : 0,00-0,06) là où les
> autres sont aiguës (0,78-0,83). La nouveauté est « après moins avant, borné à
> zéro » ; or la queue de bruit de la caisse claire 808 est encore PLUS FORTE
> dans l'aigu quarante millisecondes avant la charleston qu'au moment où elle
> frappe. La soustraction annule donc tout l'aigu de la charleston — il ne reste
> qu'un résidu grave, rangé avec les kicks. Ce n'est pas le nommage qui se
> trompe : le gabarit « kick2 » EST grave. C'est l'empreinte qui est muette.
>
> **Quatre estimations du fond éprouvées, aucune ne sauve la charleston** :
>
> | fond | B, charleston | A, kicks inventés |
> |---|---|---|
> | moyenne (actuel) | 8/16 | 15 |
> | minimum | 9/16 | 16 |
> | dernière trame | 0/16 | 25 |
> | tendance extrapolée | 0/16 | 16 |
>
> C'est donc bien, comme le § 1.3 l'écrivait avant toute mesure, « une autre
> technique et non un seuil mieux choisi » : une charleston qui suit une caisse
> claire n'a PAS de nouveauté, et seul un gabarit appris sur des SUPERPOSITIONS
> CONSTRUITES — « queue de caisse claire + charleston » contre « queue de caisse
> claire seule », à décalages connus — peut la reconnaître dans le spectre brut.
> C'est l'objet A2.1, et le banc lui donnera son verdict.

### A2.1 et A2.2 — le classifieur de frappes, et le banc dit oui

`analyse/analyzer/vsm_drum_corpus.py`, commande `analyse/classifieur_batterie.py`.

**Le corpus** : frappes engendrées par les trois boîtes du parc (TR-909,
TR-808, `vsm.drums`), cinq variantes de réglage, deux vélocités — chaque pièce
SEULE, puis chaque paire SUPERPOSÉE à six décalages connus (94 à 273 ms, de la
double-croche à 160 BPM à la croche à 110), puis chaque paire EN CO-FRAPPE.
**4 710 exemples en 28 secondes**, 196 situations, étiquetés par construction :
à l'instant de la seconde frappe, quelle pièce est nouvelle ? C'est ce que le
§ 5 demandait, et pour la première fois on sait précisément quels cas mettre
dedans.

**Le descripteur** n'est pas la nouveauté : c'est le COUPLE (moyenne d'avant,
pic d'après) plus leur RAPPORT bande par bande. Une charleston sur une queue de
caisse claire ne fait pas monter l'aigu au-dessus de la moyenne d'avant, mais
elle l'empêche de descendre — et ça se lit dans le rapport, pas dans la
soustraction.

**Le modèle** : un classifieur binaire par pièce, gradient boosting CPU, coupé
par SITUATION (une situation entière d'un seul côté, sinon le score mentirait
vers le haut). Sur les situations jamais vues : kick 100 %, clap 97 %, tom
100 %, charleston 76 % de rappel.

**Le banc, qui seul compte :**

| motif | pièce | sans modèle | **avec modèle** |
|---|---|---|---|
| double-croche (909) | kick | 16/16, **15** inventés | 16/16, **4** inventés |
| | hihat | 33/64 | **44/64** |
| contretemps (808) | kick | 16/16, **8** inventés | 16/16, **0** inventé |
| | hihat | **8/16** | **16/16** |

**Le critère A2.2 reformulé est atteint sur le motif B** : charleston seule
16/16, zéro kick inventé, aucune frappe perdue. Sur A, les kicks inventés
tombent de 15 à 4 et la charleston monte de 33 à 44 — mieux, pas fini : les
vingt charlestons restantes sont celles qui tombent SUR un kick, et le modèle
les voit parfois comme le kick seul.

**La caisse claire sur le kick : une faute de protocole, puis un demi-résultat.**
Elle restait à 0/8 sur les deux motifs, et le modèle la voyait pourtant — à
0,47, 0,21 — sous le seuil. La raison était dans MA coupure : « kick+snare
ensemble » n'était qu'UNE situation, donc entière d'un seul côté, et le tirage
l'avait mise dans l'épreuve. Le modèle n'avait jamais vu une caisse claire sur
un kick à l'entraînement, et la reconnaissait à 0,47 quand même. C'était
méritoire, ce n'était pas le test voulu.

Les co-frappes sont désormais déclinées à trois ÉQUILIBRES (égal, l'une en
retrait, l'autre en retrait — dans un morceau de club la caisse claire est le
plus souvent SOUS le kick), et un garde-fou interdit qu'une paire soit entière
à l'écart. Le seuil de décision a été BALAYÉ au banc plutôt que choisi : les
sorties sont presque toujours proches de 0 ou 1, il ne pèse que sur les
co-frappes, et **aucune frappe n'est jamais perdue** quel que soit le seuil.
0,25 est la valeur mesurée (0,3, essayé « pour la marge », tombait du mauvais
côté d'une frappe). Le banc, après :

| motif | pièce | sans modèle | **avec modèle** |
|---|---|---|---|
| double-croche (909) | kick | 16/16, 15 inventés | 16/16, **8** inventés |
| | snare | 0/8 | **3/8** |
| | hihat | 33/64 | **40/64** |
| contretemps (808) | kick | 16/16, 8 inventés | 16/16, **0** inventé |
| | snare | 0/8 | **2/8** |
| | hihat | 8/16 | **16/16** |

La caisse claire sort de zéro ; la charleston sur le motif A recule de 44 à
40 ; rien n'est perdu. C'est un demi-résultat et il est écrit comme tel : la
similarité kick-seul / kick+caisse de 0,947 documentée dans `vsm_drumkit.py`
n'est pas levée, elle est entamée. Pour une boîte à rythmes qui rejoue le kit,
l'étiquette devient le son, et 2/8 de caisses claires, c'est un motif qui boite.

**Dans la chaîne** : `reconstruire.py --classifieur-batterie MODÈLE`. Sans
modèle, rien ne change — le nommage actuel reste le repli.

### A2.3 — un troisième motif, et le modèle doit voir ce que le détecteur voit

Le critère « toms/percussions/claps nommables » n'avait pas de juge : les deux
motifs du § 9.5 n'ont que trois familles. Le banc en a un troisième,
**C — « familles »** (TR-909) : kick sur 1 et 3, CLAP seul sur 2 et 4,
charleston aux contretemps, deux TOMS seuls sur le « e » et le « a » du
quatrième temps. Chaque clap et chaque tom frappe seul : ce motif ne juge pas
la superposition, il juge le nommage au-delà des trois familles. Et
`banc_batterie.py --classifieur-batterie MODÈLE` juge enfin AVEC le modèle —
jusqu'ici seule la commande d'entraînement le faisait.

**Premier passage : clap 8/8, tom 0/8 — et le modèle avait raison.** Sans
modèle, les claps sont nommés snare et les toms kick (le gabarit ne connaît
pas ces familles). Avec le modèle, clap 8/8 ; mais tom 0/8, rabattus en
« kick2 ». Interrogé à l'instant EXACT de chaque tom, le modèle répondait
pourtant « tom » à 0,98-0,99. Le mécanisme : le détecteur place l'attaque
d'un tom de 909 — attaque lente — **11 à 16 ms après la note**, et à cet
instant-là le modèle ne reconnaît plus rien (aucune pièce au seuil), donc le
gabarit reprend la main. La fenêtre « avant » du descripteur s'arrête 8 ms
avant l'instant lu : lue 16 ms en retard, elle contient l'attaque, et le
rapport après/avant s'effondre. Le modèle était entraîné sur la partition ;
il doit l'être sur ce que le détecteur lui donne.

**Le corpus décrit donc chaque frappe à son instant ET en retard**
(`RETARDS`), et le retard a été BALAYÉ au banc plutôt que choisi — parce que
la réponse n'est pas monotone :

| retards (ms) | A snare | A hihat | A kicks inv. | B snare | B hihat | B kicks inv. | C tom | C hihat | C kicks inv. |
|---|---|---|---|---|---|---|---|---|---|
| 0 (avant) | 3/8 | 40/64 | 8 | 2/8 | **16/16** | **0** | 0/8 | 15/16 | 7 |
| 0, 8 | 7/8 | 34/64 | 14 | 8/8 | 8/16 | 8 | 1/8 | 11/16 | 6 |
| 0, 12 | 8/8 | 35/64 | 13 | 8/8 | 8/16 | 8 | 7/8 | 12/16 | 0 |
| **0, 16** | **8/8** | **40/64** | **8** | **8/8** | 15/16 | 1 | **7/8** | 13/16 | **0** |
| 0, 20 | 8/8 | 40/64 | 8 | 8/8 | 11/16 | 5 | 7/8 | 12/16 | 0 |
| 0, 8, 16 | 8/8 | 39/64 | 9 | 8/8 | 8/16 | 8 | 6/8 | 13/16 | 1 |
| 0, 16, 24 | 8/8 | 40/64 | 8 | 8/8 | 12/16 | 4 | 7/8 | 14/16 | 0 |

**Ce que le tableau dit, et ce qu'il ne dit pas.** La lecture en retard
n'a pas seulement sauvé les toms : elle a donné la CAISSE CLAIRE SOUS LE KICK,
8/8 sur les deux motifs là où elle plafonnait à 3/8 et 2/8 — la similarité
0,947 kick-seul / kick+caisse de `vsm_drumkit.py`, « entamée » au paragraphe
précédent, est LEVÉE : la caisse claire est plus lisible 16 ms après l'attaque
du kick qu'à l'attaque même. Mais 16 ms est une **crête, pas un plateau** :
à 12 et à 20, la charleston du motif B retombe (8/16, 11/16) et des kicks
s'inventent. Ce qui tient à toute valeur (kick, snare, clap, tom : probabilité
≥ 0,98 aux instants détectés, mesurée) n'est pas ce qui bascule (la charleston
après une caisse claire : médiane 0,85, premier quartile 0,57, une à 0,04).
La charleston reste la pièce fragile du nommage appris ; le reste ne l'est
plus. Le modèle est au format v2 (empreintes, voir A4.1), 45 s à refaire.

**Le banc, après (modèle `RETARDS = (0, 16 ms)`) :**

| motif | pièce | sans modèle | **avec modèle** |
|---|---|---|---|
| double-croche (909) | kick | 16/16, 15 inventés | 16/16, **8** inventés |
| | snare | 0/8 | **8/8** |
| | hihat | 33/64 | **40/64** |
| contretemps (808) | kick | 16/16, 8 inventés | 16/16, **1** inventé |
| | snare | 0/8 | **8/8** |
| | hihat | 8/16 | **15/16** |
| familles (909) | kick | 8/8, 7 inventés | 8/8, **0** inventé |
| | clap | 0/8 (nommés snare) | **8/8** |
| | hihat | 9/16 | **13/16**, 1 inventé |
| | tom | 0/8 (nommés kick) | **7/8** |

A2.3 est atteinte : contretemps 15/16 (> 14/16), claps et toms nommés. Les
percussions au sens large (cymbales, rimshot, cowbell) ne sont pas dans le
corpus, donc pas nommables : le modèle ne nomme que ce qu'il a entendu, et
c'est écrit dans `PIECES`.

## Phase A3 — L'estimateur de paramètres

La plus risquée ; n'entre en chaîne que si son A/B global est positif.

| Étape | Contenu | Terminé quand |
|---|---|---|
| A3.1 | Estimateur avec incertitude par axe | couverture mesurée : vraie valeur dans [estimation ± incertitude] ≥ 98 % par axe sur données tenues à l'écart ; les axes sous le seuil gardent leur plage déclarée |
| A3.2 | Resserrement de l'espace cherché | A/B à budget égal sur le banc : distances ≤ chaîne actuelle ; chaque resserrement imprimé |
| A3.3 | Candidat bon marché (une évaluation) | sur les cibles « faciles » : recherche raccourcie, verdict imprimé avec les deux distances |
| A3.4 | A/B global | médiane des distances finales ≤ chaîne actuelle avec ≤ la moitié des évaluations ; aucun verdict de vérité terrain dégradé |

**Critère de phase** : A3.4, sans appel. S'il n'est pas atteint, le code est
conservé désactivé et documenté — le sort exact des étapes 10.1 et 10.2, et
c'est un résultat, pas une honte.

> **A3 EST CLOSE PAR REJET, ET CE DOCUMENT NE LE DISAIT PAS (constaté le
> 02/09/2026).** La phase a bien été menée jusqu'au bout, et ses mesures
> vivent au § 7 de [`ROADMAP-fusion.md`](ROADMAP-fusion.md) — mais rien ici
> ne renvoyait vers elles, si bien que la seule phase encore ouverte du
> tableau était en réalité terminée depuis longtemps. Un plan qui garde
> ouverte une case déjà tranchée fait chercher deux fois le même travail ;
> le renvoi est donc écrit ici, avec les chiffres qui comptent :
>
> - **A3.1 — le garde-fou EXISTE et fonctionne.** La distance de la
>   prédiction à la cible (un rendu, ~12 ms) est corrélée à **−0,66** au
>   gain qu'elle apportera : l'estimateur sait dire quand ne pas se croire.
> - **A3.2 — le resserrement marche… sur ce que la machine sait produire.**
>   Boîte ±0,15 autour de la prédiction, 20 itérations, garde-fou actif :
>   **−21 %** de distance médiane contre la recherche complète, à budget
>   égal, sur 14 cibles rendues par le moteur lui-même.
> - **A3.3 — le candidat bon marché tient aussi** : boîte 5 itérations,
>   distance identique à la recherche complète pour **1,7×** moins de temps.
> - **A3.4 — ET C'EST LUI QUI TRANCHE : NON.** Sur 9 cibles RÉELLES (stems
>   séparés, avec leurs artefacts et leurs fuites), le régime prudent rend
>   0,1974 contre 0,1974 pour la recherche ordinaire — **identique**, parce
>   que le garde-fou refuse de resserrer sur 8 cibles sur 9. La méthode ne
>   nuit pas ; elle ne sert pas non plus. Le critère exigeait « médiane ≤
>   avec ≤ la moitié des évaluations » : il n'est pas atteint.
>
> **La cause n'est pas un défaut de modèle mais une impossibilité de
> principe**, et deux chemins indépendants l'ont confirmée (l'estimateur ici,
> le classifieur en A1) : le corpus ne contient que des sons que la machine
> sait produire, alors qu'un stem séparé est un son qu'AUCUNE machine ne
> produit. La condition de réouverture — un corpus qui contienne la
> DÉGRADATION — a été construite (`analyse/corpus_separe.py`) et **n'a pas
> rouvert le dossier** (28/08). Le code reste, désactivé et documenté, dans
> `analyse/analyzer/vsm_corpus.py`.
>
> **Toutes les phases A0–A5 sont donc traitées**, A3 par rejet mesuré, A5.3
> restant une écoute humaine qui ne peut pas se déléguer.

## Phase A4 — Intégration, rapport, repli

| Étape | Contenu | Terminé quand |
|---|---|---|
| A4.1 | Modèles versionnés, chargés et vérifiés (empreintes) au démarrage | modèle absent ou périmé → repli chaîne actuelle, dit et daté |
| A4.2 | `rapport.json` : versions de corpus/modèles, abstentions et resserrements avec motifs | deux exécutions complètes → rapports identiques à la 4e décimale (le déterminisme de House Of God, préservé) |

> **A4.2 — fait (23/08).** Le rapport porte une clé `provenance` : le commit du
> code, les options qui conditionnent le résultat (séparation, sampler,
> arbitrages, réglage et son budget, finalistes, présélection apprise), les
> modèles consultés avec leur date d'entraînement — ou « aucun », qui est une
> information et non une absence d'information — et le profil multi-échantillons
> désigné. Les abstentions et classements du classifieur de machine étaient déjà
> par stem (`classifierRanking`, `classifierAbstention`). Le déterminisme à la
> 4e décimale n'est pas remesuré ici ; il l'a été sur B4 Wuz Then, dont les
> trois passages ont rendu des recherches par note identiques à la quatrième
> décimale à code égal.
| A4.3 | `--sans-apprentissage` | reproduit exactement la chaîne d'aujourd'hui ; sert de témoin dans tous les A/B |

> **A4 EST FAITE, ET A4.2 NE L'ÉTAIT PAS (24/08/2026).**
>
> **A4.2 — la provenance n'existait pas sur disque.** `reconstruire.py` écrit
> le rapport DEUX fois : une première avant le rendu, une seconde après, pour y
> ajouter la distance globale. Seule la première recevait la provenance ; la
> seconde écrasait le fichier sans elle. Le rapport final — le seul qu'on lit
> — ne disait ni commit, ni options, ni modèles, et la ligne « A4.2 — fait »
> ci-dessus décrivait un résultat qui n'existait pas. Un test le vérifiait sur
> la fonction d'écriture, pas sur le fichier que la chaîne laisse ; c'est le
> fichier qui est maintenant relu (test `sans_apprentissage_reproduit_…`).
> Au passage, le commit est suivi d'un `+` quand l'arbre de travail d'`analyse/`
> est modifié : un rapport qui annoncerait le commit nu sur un code différent
> ne se rejouerait sur rien.
>
> **Et le rapport ne parlait pas de la batterie.** `stems` ne listait que les
> stems mélodiques : la piste la plus lourde du mélange — arbitrée entre trois
> boîtes, réglée, départagée au verdict du mélange — n'y laissait aucune
> trace, et le verdict du mélange lui-même n'était qu'imprimé. Le rapport porte
> désormais `drums` (machine retenue, pièces, arbitrage, réglages, distance de
> piste) et `mixVerdict` (par piste : ce qui est gardé, ce qui est écarté, avec
> la distance du MÉLANGE pour chacun). L'invariant « toute décision … versionnée
> dans rapport.json » n'était pas tenu pour la décision la plus coûteuse.
>
> **A4.1 — le classifieur de frappes n'était pas vérifié.** Le classifieur de
> machine rejouait ses empreintes au chargement ; celui de frappes était chargé
> tel quel. Or il est entraîné sur les kicks de trois boîtes : qu'un kick de
> 909 change, et il nomme des kicks de 909 d'hier. L'empreinte mélodique
> (`machine_fingerprint`, notes 60 et 72) ne convenait pas — sur une boîte à
> rythmes ces notes tombent sur un emplacement vide ou une seule pièce — ; une
> empreinte de KIT (`empreinte_batterie`, chaque pièce jouée, patch d'usine)
> est inscrite au modèle (format v2), rejouée au chargement, et le modèle est
> REFUSÉ s'il est périmé ou invérifiable — un modèle v1, sans empreinte, est
> invérifiable, donc refusé ; il se refait en 45 s. Trois tests.
>
> **A4.3 — le témoin est testé.** Avec un modèle SUR la ligne de commande et
> `--sans-apprentissage`, le rapport est identique au chiffre près à celui
> d'une chaîne sans modèle, provenance mise à part (elle dit « aucun », ce qui
> est l'information attendue). Le même test vérifie au passage que deux
> exécutions de la chaîne rendent le même rapport — le déterminisme d'A4.2,
> mesuré cette fois.
>
> **Et les modèles ont un domicile.** Ils vivaient dans un dossier temporaire
> de session (tmpfs) : un redémarrage les aurait effacés, avec les trois corpus
> de l'A/B. `modeles/` et `corpus/` à la racine du dépôt, ignorés par git
> (regénérables, graines fixées), sont désormais ce que le README annonce.

## Phase A5 — Validation : un morceau de musique classique

Le banc final, choisi parce qu'il est le plus hostile au parc (couverture,
séparation, polyphonie). Les attentes sont écrites **avant** la mesure au
CDC §10 — cette phase vérifie qu'elles se réalisent, pas qu'on les contourne.

| Étape | Contenu | Terminé quand |
|---|---|---|
| A5.1 | Reconstruction de bout en bout (`reconstruire.py`) sur l'enregistrement choisi | la chaîne va au bout ; `rapport.json` et `comparaison.wav` produits |
| A5.2 | Vérification des abstentions | les sources acoustiques passent par abstention → sampler, chacune avec sa mesure ; aucune machine de caractère « confiante » sur un instrument d'orchestre |
| A5.3 | Écoute et correction dans le DAW | projet ouvert (11.1), A/B contre l'original (11.2), notes douteuses marquées (11.3) ; ce que l'oreille trouve que le rapport ne disait pas devient une entrée de ce tableau — **préparée, l'écoute reste à faire** |
| A5.4 | Bilan | distances par stem publiées, passes documentées comme pour House Of God ; les leçons remontent dans les CDC — **fait** |

**Critère de phase** : un rapport qui dit vrai et une écoute qui ne surprend
pas son lecteur. Une distance élevée honnêtement expliquée vaut mieux qu'une
petite distance obtenue en trichant sur ce qu'on mesure.

> **A5.1 EST FAITE, ET A5.2 EST BLOQUÉE PAR UN REFUS QUI EST UNE BONNE
> NOUVELLE (29/08/2026).** *Clair de Lune* passé de bout en bout,
> séparation comprise : `reconstruire.py ../sources/clairdelune.wav
> --classifieur modeles/classifieur.joblib --classifieur-batterie
> modeles/frappes.joblib`, commit `85a6420+`, métrique v2, 20 itérations,
> budget de piste 40 sur 8 axes, arbitrage et réglage actifs, présélection
> apprise désactivée. **1 382 s**, sortie dans
> `reconstruction/travail/cdl-a5/` (projet, MIDI, rapport, `comparaison.wav`).
> **Distance globale 0,3746.**
>
> | piste | recherche | arbitrage de piste | réglage | verdict du mélange |
> |---|---|---|---|---|
> | bass | `vsm.wind` 0,101 (736 notes) | confirme `vsm.wind` 0,335 | 0,335 → **0,151** | garde le réglage (0,4266) |
> | other | `vsm.string` 0,240 (2 225 notes) | **CHANGE** pour `vsm.piano` d'usine 0,270 | 0,270 → 0,244 | garde l'arbitrage (0,3821) |
> | Batterie | — | garde `vsm.drums` 0,253 (contre TR-909 0,399, TR-808 0,483) | 0,253 → 0,224 | garde **l'avant-réglage** (0,3746) |
> | Voix | — | — | — | sampler, report intégral |
>
> **Le refus, d'abord, parce qu'il change le sort de la phase.** Le
> classifieur de machine a été **REFUSÉ au chargement** : « périmé pour
> `vsm.multisample` (leur son a changé depuis la génération) ». C'est le
> mécanisme d'A4.1 qui se déclenche pour la première fois sur une exécution
> réelle et non dans un test, et il fait exactement ce qui était écrit — la
> chaîne continue sans lui, et le rapport porte `classifieurMachine:
> "aucun"`, ce qui est une information et non une absence d'information.
> Conséquence directe : **A5.2 n'a pas pu être mesurée**, puisqu'elle juge
> les abstentions d'un modèle qui n'a pas parlé. Le corpus doit être
> réengendré et le modèle réentraîné avant de reprendre A5.2 ; c'est le coût
> normal d'une empreinte qui fait son travail, et il est préférable à un
> modèle qui aurait répondu sur un parc qu'il ne connaît plus.
>
> **Le risque « séparation pop sur un orchestre » s'est réalisé, en pire que
> prévu.** Le cahier annonçait « un orchestre sortira essentiellement en
> autres, peu séparé ». Sur un piano SEUL, demucs ne se contente pas de mal
> séparer : il **invente deux instruments**. Le stem `drums` contient
> 1 852 frappes que la chaîne nomme en quatre pièces, et le stem `bass`
> reçoit `vsm.wind` à 0,101 — la meilleure distance de note du morceau,
> obtenue sur ce qui n'est pas un instrument. Le verdict du mélange, lui, ne
> s'y trompe pas à moitié : il **garde** la batterie, parce que le résidu
> perçussif d'un piano rapproche effectivement le mélange (0,3746 contre
> 0,3821 sans). C'est le cas que le § 10 du cahier voulait voir écrit plutôt
> que maquillé : **la chaîne reconstruit fidèlement les artefacts de la
> séparation, et elle le dit dans son rapport.**
>
> **Et l'arbitrage de piste a de nouveau détrôné la recherche par note** :
> sur `other`, `vsm.string` gagne la note (0,240) et perd la piste
> (0,955 avec son patch cherché), où `vsm.piano` d'usine l'emporte à 0,270.
> C'est la cinquième occurrence du § 5 septies de `ROADMAP-fusion.md`, et la
> première sur un piano seul.
>
> Comparer 0,3746 aux 0,2159 et 1,639 des passes antérieures n'aurait aucun
> sens : celles-ci ne passaient pas par la séparation, et la règle du § 10.3
> vaut ici comme ailleurs — une distance n'est un chiffre que si l'on sait à
> quelles conditions elle a été obtenue. Le rapport porte les siennes.

> **A5.3 — CE QUE LE RAPPORT DONNE À L'OREILLE, ET CE QU'IL NE PEUT PAS
> FAIRE À SA PLACE.** L'étape demande une écoute dans le DAW ; ce qui peut
> être préparé l'a été. Le projet s'ouvre (`reconstruction/travail/cdl-a5/`,
> quatre pistes : `bass`, `other`, `Batterie`, `Voix`), `comparaison.wav`
> porte l'original à gauche et la reconstruction à droite, et les
> **confiances par note** sont dans le rapport, une par note, prêtes pour le
> marquage du piano roll (§ 11.3 de `ROADMAP-fusion.md`) :
>
> | piste | notes | médiane | 1er quartile | sous 0,50 |
> |---|---|---|---|---|
> | `bass` (le stem halluciné) | 736 | 0,449 | 0,387 | **498 (68 %)** |
> | `other` (le piano) | 2 225 | 0,536 | 0,435 | 881 (40 %) |
>
> **Le transcripteur se méfie le plus de la piste qui n'existe pas.** Les deux
> tiers des notes du stem `bass` sont sous 0,50, contre 40 % sur le piano. La
> chaîne portait donc déjà, sans qu'on le lui demande, le signal qui permet à
> une oreille de trouver l'hallucination : il suffit d'aller aux notes
> douteuses, ce que le piano roll sait faire depuis le § 11.3. L'écoute
> elle-même reste à faire et demande une oreille ; c'est la seule chose de
> cette phase qu'aucune mesure ne remplace.
>
> **A5.4 — BILAN.** Les distances, à conditions écrites (métrique v2, 20
> itérations, budget de piste 40 sur 8 axes, séparation htdemucs `shifts=0`,
> arbitrage et réglage actifs, présélection apprise désactivée) :
>
> | piste | machine retenue | distance de piste | ce que le mélange en fait |
> |---|---|---|---|
> | `bass` | `vsm.wind` | 0,151 après réglage | gardé (0,4266) |
> | `other` | `vsm.piano` (patch d'usine) | 0,244 après réglage | **arbitrage** gardé (0,3821) |
> | `Batterie` | `vsm.drums` | 0,224 après réglage | **avant réglage** gardé (0,3746) |
> | `Voix` | sampler | — | report intégral |
> | **morceau** | | | **0,3746** |
>
> **Les quatre leçons, et elles sont remontées dans les cahiers des charges.**
> (1) La séparation ne se contente pas de mal séparer un enregistrement hors
> de son domaine : elle HALLUCINE des sources, et la chaîne les reconstruit
> consciencieusement — c'est l'attente à écrire pour le prochain banc, à la
> place de « un orchestre sortira essentiellement en autres ». (2) Le rayon
> d'abstention a arrêté une désignation à 0,96 sur un piano : il gagne sa
> place, et le modèle reste consultatif. (3) La péremption par empreinte dit
> QUE le modèle est à refaire, pas AVEC QUOI — vérifier les corpus
> disponibles avant d'en fabriquer un a économisé 27 minutes contre 28
> secondes. (4) Ce qui plafonne le résultat sur ce morceau n'est ni le parc ni
> la recherche mais la TRANSCRIPTION, comme le § 5 sexies de
> `ROADMAP-fusion.md` l'avait établi ; les confiances par note le disent
> maintenant chiffre en main.
>
> **La phase A5 est donc close pour tout ce qu'une mesure peut clore**, et il
> reste exactement une chose : écouter.

> **A5.2 — MESURÉE, ET LE CRITÈRE N'EST PAS ATTEINT : IL RESTE UNE
> DÉSIGNATION CONFIANTE (29/08/2026).** Le modèle a d'abord été réentraîné,
> et l'enquête sur son refus vaut d'être écrite : **le corpus n'était pas
> périmé, le modèle l'était.** Une seule empreinte différait — celle de
> `vsm.multisample` — entre le modèle (`a9b40e1d…`) et le moteur d'aujourd'hui
> (`1390126f…`), et `corpus/ab-augmente-v2` porte la seconde : il est déclaré
> « à jour » par `corpus.py --verifier`, quand `ab-sec` et `ab-augmente`, plus
> anciens, sont périmés. Le modèle en service avait donc été entraîné sur un
> corpus dépassé alors qu'un corpus valable existait à côté. **28 secondes de
> réentraînement**, pas les 27 minutes d'un corpus : top 1 93,8 %, top 3
> 98,9 %, et hors indistinguables (26,3 %) top 3 **99,8 %** — A1.1 reste
> atteint. La leçon est d'exploitation, pas de méthode : la péremption dit
> QUE le modèle est à refaire, elle ne dit pas AVEC QUOI, et le corpus le plus
> récent n'est pas forcément celui qui a servi.
>
> **La mesure**, vingt extraits d'une seconde également répartis sur
> l'enregistrement et sur chacun des quatre stems que la séparation a tirés
> d'un piano SEUL (les extraits silencieux ne sont pas classés, d'où les
> totaux inégaux) ; « confiante » = score ≥ 0,90 :
>
> | source | abstentions | retenues | confiantes | distance médiane au corpus |
> |---|---|---|---|---|
> | original (piano seul) | 18/20 | 2 (`vsm.sh101`) | **1 — `vsm.sh101` à 1,00** | 5,45 |
> | stem `other` (le piano) | 19/20 | 1 (`vsm.sh101`) | **1 — `vsm.sh101` à 0,94** | 5,56 |
> | stem `bass` (artefact) | 2/3 | 1 (`vsm.sh101`) | 0 | 6,05 |
> | stem `drums` (artefact) | **6/6** | 0 | 0 | 6,05 |
> | stem `vocals` (artefact) | 7/9 | 2 (`vsm.ms20`) | 0 | 5,34 |
>
> **Ce qui marche.** L'abstention est passée de 75 % (A1.2, 23/08) à **90 %**
> sur le même enregistrement, sans que le rayon change (3,77 contre 3,72 —
> l'écart vient du réentraînement, pas d'un réglage). Le rayon fait ce pour
> quoi il est là : la distance médiane du réel au corpus est de 5,3 à 6,1 pour
> un rayon de 3,77, c'est-à-dire que le piano est franchement hors du parc, et
> mesurément. Et les trois stems qui sont des ARTEFACTS de la séparation — la
> basse, la batterie et la voix inventées à partir d'un piano — ne reçoivent
> **aucune** désignation confiante ; la batterie fantôme s'abstient 6 fois sur
> 6. Le modèle ne prétend pas reconnaître ce qui n'existe pas.
>
> **Ce qui ne marche pas, et c'est le critère.** `vsm.sh101` est désigné à
> **1,00** sur un piano acoustique. C'est le même échec qu'en A1.2, à la même
> machine, réduit en fréquence (1 cas sur 20 au lieu de 4) mais pas en nature :
> un classifieur à ensemble fermé reste capable d'une certitude totale sur une
> source qu'aucune de ses classes ne produit. **A5.2 n'est donc pas atteinte**,
> et le § 4 du cahier ne se contente pas d'un progrès. Ce qui la sauverait
> n'est pas un rayon plus serré — à 10 % de refus abusifs sur le corpus, il est
> déjà au prix que l'asymétrie d'A1.2 justifie — mais un modèle qui ne soit pas
> à ensemble fermé, c'est-à-dire une classe « aucune », qu'aucun corpus de
> rendus ne peut peupler. C'est, une troisième fois et par une troisième
> porte, le fossé de domaine du § 7 de `ROADMAP-fusion.md`.
>
> **ET LA MÊME QUESTION POSÉE À LA CHAÎNE, QUI EST LE SEUL ENDROIT OÙ ELLE
> COMPTE, DONNE UNE AUTRE RÉPONSE.** La passe A5.1 avait tourné sans
> classifieur (refusé). Elle a été REJOUÉE avec le modèle réentraîné, en
> reprenant les stems déjà séparés (`--stems`) pour que rien d'autre ne
> change. Résultat, sur les deux stems mélodiques :
>
> ```
> bass  : classifieur — aucune machine du parc ne convient : ce son est à 5.28 du corpus, au-delà du rayon 3.77
> other : classifieur — aucune machine du parc ne convient : ce son est à 6.37 du corpus, au-delà du rayon 3.77
> ```
>
> **Abstention 2 sur 2**, et le rapport porte enfin la provenance du modèle
> (`classifieurMachine: 2026-08-28T17:50:18+00:00`) avec, pour chaque stem, le
> classement que le modèle AURAIT rendu. C'est là que la mesure devient
> intéressante : sur le stem `other` — le piano — ce classement commence par
> **`vsm.juno106` à 0,96**. Une désignation confiante, sur un piano
> acoustique, **arrêtée par le rayon**. Le garde-fou ne fait pas de la
> figuration : il attrape exactement le cas que le § 4 du cahier appelle le
> pire résultat possible du projet.
>
> **Les deux mesures ne se contredisent pas, elles ne portent pas sur la même
> chose**, et la différence est la fenêtre du descripteur. Par extraits d'une
> seconde tirés au fil du morceau, le modèle est confiant une fois sur vingt
> et le rayon laisse passer ce cas-là. À l'endroit où la chaîne l'interroge
> vraiment — la note de référence du stem, la plus longue —, il s'abstient
> deux fois sur deux. **A5.2 est donc atteinte pour la CHAÎNE et pas pour le
> MODÈLE** : aucune machine de caractère n'est désignée sur cet enregistrement
> par la chaîne, et le modèle reste capable d'une certitude à 1,00 si on
> l'interroge ailleurs. C'est écrit dans cet ordre parce que c'est le seul
> honnête : ce qui protège le verdict n'est pas la qualité du modèle, c'est
> qu'on ne le laisse pas décider.
>
> **Et le déterminisme d'A4.2 est remesuré au passage** : deux passes du même
> morceau, l'une sans modèle et l'autre avec, rendent la MÊME distance globale
> (0,374584 des deux côtés), les mêmes machines (`vsm.wind`, `vsm.piano`,
> `vsm.drums`) et la même distance de batterie (0,223792). L'invariant « sans
> modèle sur disque, la chaîne = la chaîne d'aujourd'hui » vaut donc aussi
> dans l'autre sens : **avec** modèle et sans présélection apprise, elle ne
> bouge pas d'un chiffre.

---

## Risques nommés, et leur parade

| Risque | Parade |
|---|---|
| Écart de domaine (corpus sec vs stems réels) | ~~augmentations (A0.4)~~ mesurées, elles ne le comblent pas ; ~~corpus passé par la séparation~~ mesuré, il ne le comble pas non plus (§ A1, 28/08) ; ce qui reste : l'abstention, et la validation uniquement sur vérités terrain et morceaux réels |
| Modèles périmés quand une machine évolue | péremption par empreinte (A0.3), refus au chargement |
| Classifieur confiant hors du parc | abstention obligatoire, testée sur sources acoustiques (A1.2) |
| Timbres indistinguables | exclus du dénominateur, comptés à part (CDC §1.4) |
| L'estimateur répète le sort de 10.1/10.2 | ordre des phases ; A3.4 sans appel ; rejet documenté = résultat |
| Dérive du déterminisme (bibliothèques ML) | versions épinglées, graines fixées, identité des verdicts testée (A0.2, A1.1) |
| Séparation pop sur un orchestre (A5) | attendu et écrit d'avance ; le rapport le dit au lieu de le maquiller |

## Invariants — ceux de la fusion, plus quatre

```
[ ] Tous les invariants de ROADMAP-fusion §7, mot pour mot
[ ] Aucun modèle appris ne produit ni ne modifie de l'audio entendu
[ ] Sans modèle sur disque, la chaîne = la chaîne d'aujourd'hui, à l'identique
[ ] Toute décision d'un modèle est imprimée avec sa mesure, et versionnée
    dans rapport.json
[ ] Corpus, entraînement, inférence : seedés, regénérables, rejouables
```
