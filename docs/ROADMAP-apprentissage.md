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
| A4.3 | `--sans-apprentissage` | reproduit exactement la chaîne d'aujourd'hui ; sert de témoin dans tous les A/B |

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
