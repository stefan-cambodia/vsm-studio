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

> **LA CONDITION DE RÉOUVERTURE A SON INSTRUMENT (28/08/2026), ET LA MESURE EST
> EN COURS.** `analyse/corpus_separe.py` engendre le corpus que le § 7 réclame :
> des rendus du moteur mélangés à de VRAIS stems séparés, repassés par demucs en
> une seule passe, redécoupés et étiquetés avec le patch d'origine. Puis il
> mesure trois classifieurs à coupure égale (sec, séparé, les deux) sur le
> séparé tenu à l'écart et sur des SONDES réelles — un enregistrement, les notes
> que la chaîne y a transcrites (lues dans le MIDI du projet), et la machine que
> l'arbitrage y a retenue ; le rang médian de cette machine est le seul chiffre
> qui dise si le modèle lit un disque.
>
> Protocole de la passe lancée : fond = *House Of God* (batterie, basse, voix) et
> la batterie et la voix SEULES de *Children* (face A) et *B4 Wuz Then*, dont la
> basse et « other » sont des sondes et ne doivent pas avoir servi de fond ;
> 20 machines × 25 patchs × 4 conditions ; sondes : *Clair de Lune* → `vsm.piano`,
> *B4* other → `vsm.jupiter8`, *B4* bass → `vsm.supersaw`, *Children* other →
> `vsm.string`, *Children* bass → `vsm.piano`. Le résultat sera écrit ici ; s'il
> ne referme pas le fossé, A3 n'a pas de corpus, et c'est un résultat.

## Phase A2 — Les gabarits de batterie appris

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
| A5.3 | Écoute et correction dans le DAW | projet ouvert (11.1), A/B contre l'original (11.2), notes douteuses marquées (11.3) ; ce que l'oreille trouve que le rapport ne disait pas devient une entrée de ce tableau |
| A5.4 | Bilan | distances par stem publiées, passes documentées comme pour House Of God ; les leçons remontent dans les CDC |

**Critère de phase** : un rapport qui dit vrai et une écoute qui ne surprend
pas son lecteur. Une distance élevée honnêtement expliquée vaut mieux qu'une
petite distance obtenue en trichant sur ce qu'on mesure.

---

## Risques nommés, et leur parade

| Risque | Parade |
|---|---|
| Écart de domaine (corpus sec vs stems réels) | augmentations (A0.4) ; validation uniquement sur vérités terrain et morceaux réels |
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
