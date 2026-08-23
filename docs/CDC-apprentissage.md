# Cahier des charges — Apprentissage : connaître les sonorités du parc

**Question posée.** La reconstruction s'améliorerait-elle davantage en ajoutant
des machines, ou en « s'entraînant » à reconnaître celles qui existent ?

**Réponse courte.** En s'entraînant — et le professeur est déjà dans le dépôt.
Le moteur sait produire des paires (paramètres → audio) à volonté, en ~10 ms
par note, déterministes au bit près. C'est un corpus étiqueté illimité, gratuit
et juridiquement propre. Ce document décrit ce qu'on en fait : un **corpus
généré**, un **classifieur de machine**, des **gabarits de batterie appris**,
et un **estimateur de paramètres**. Les modèles appris sont des _conseillers_ :
ils classent, présélectionnent, resserrent — **ils ne produisent jamais une
seconde d'audio**. Le moteur reste l'unique source de vérité du rendu.

Référence : [`ROADMAP-apprentissage.md`](ROADMAP-apprentissage.md) pour le
découpage en phases, [`ROADMAP-fusion.md`](ROADMAP-fusion.md) pour l'état de la
chaîne que ce CDC prolonge.

---

## 1. Ce que la mesure a déjà établi — et que ce CDC doit respecter

Quatre résultats chiffrés de la phase 10 et du §9.5 bornent ce projet. Les
ignorer serait refaire des expériences déjà payées.

1. **Un bon point de départ ne sert à rien à l'optimiseur actuel** (étape
   10.2) : un point d'amorce meilleur que 90 % des tirages gagnait 3 fois sur
   8, puis 4 sur 8 — pile ou face. La force de l'évolution différentielle est
   la diversité de sa population ; un bon point s'y dilue. **Interdit donc de
   justifier l'estimateur comme « warm start »** ; ses deux usages légitimes
   sont au §6.
2. **Le choix de machine est limité par le budget, pas par la description des
   machines** (phase 9) : le Juno-106 bat le pcmhybrid à budget suffisant et
   perd au budget par défaut. C'est exactement le trou qu'un classifieur
   comble : classer sans payer une recherche.
3. **Séparer caisse claire et charleston demande des gabarits appris, pas un
   seuil mieux choisi** (§9.5, mesuré : la caisse claire d'une boîte à rythmes
   est du bruit dont l'énergie au-dessus de 5 kHz atteint celle d'une
   charleston). Le classement par nouveauté spectrale actuel plafonne à trois
   familles sûres.
4. **Certains timbres sont réellement indistinguables** : sur une nappe tenue,
   Minimoog et Juno-106 finissent quasi ex æquo en v2 — parce que plusieurs
   soustractifs produisent ce son à l'identique. Aucun classifieur ne fera
   mieux que le signal. Le banc d'essai doit contenir de tels cas et l'objectif
   de précision doit les **exclure du dénominateur**, sinon on se fixera une
   cible impossible et on tordra le modèle pour l'atteindre.

---

## 2. Principe fondateur — le moteur est le professeur

Tout apprentissage de ce CDC se fait sur des données **générées par le moteur**
(plus les augmentations du §7). Conséquences :

- **Étiquettes parfaites** : machine, paramètres, note, vélocité sont connus
  par construction, jamais annotés à la main.
- **Déterminisme** : corpus regénérable à l'identique (graines fixées), donc
  toute mesure est rejouable — la règle du dépôt s'étend aux données.
- **Licences** : aucun enregistrement commercial dans l'entraînement. Le
  corpus est au moteur ce que les empreintes de non-régression sont déjà :
  du son de la maison.
- **Péremption liée aux empreintes** : si l'empreinte audio d'une machine
  change (son DSP a bougé), son corpus est périmé et les modèles qui en
  dérivent aussi. Le manifeste du corpus (§3) enregistre les empreintes ; un
  modèle entraîné sur un corpus périmé est **refusé au chargement**, pas
  appliqué en silence.

**Le risque principal a un nom : l'écart de domaine.** Le corpus est du rendu
moteur sec ; un stem réel a traversé des effets, une pièce, un mastering, et
une séparation qui laisse des fuites. Un modèle parfait sur le corpus peut
être mauvais sur un stem. La parade est double : **augmentation** à
l'entraînement (§7) et **validation finale uniquement sur les vérités terrain
et les morceaux réels** — jamais sur le corpus lui-même.

---

## 3. Objet 1 — le corpus

Un générateur (`analyse/analyzer/vsm_corpus.py`, nom indicatif) qui produit,
machine par machine :

- des patchs tirés dans le **profil de recherche déclaré**
  (`SearchProfile`) : bornes réelles, échelles log respectées, tirage pondéré
  par importance — le corpus doit peupler l'espace que la recherche parcourt,
  pas l'espace naïf des bornes brutes ;
- plusieurs notes par patch (au moins 3 hauteurs × 2 durées × 2 vélocités),
  car une machine ne se reconnaît pas sur une seule note grave ;
- pour chaque rendu : les **caractéristiques de la métrique v2** (pas l'audio
  brut par défaut — 40 000 rendus en WAV pèseraient des gigaoctets pour rien ;
  une option `--garder-audio` existe pour l'audit) ;
- un **manifeste** JSON : machine, empreinte audio, commit du moteur, sample
  rate, graines, versions des bibliothèques, date, coût mesuré.

**Ordres de grandeur à vérifier en A0** : 10 000 patchs × 12 notes × ~10 ms de
rendu ≈ 20 min de rendu par machine sur un cœur — le calcul des
caractéristiques (~11 ms pièce, mesuré en phase 10) coûtera davantage que le
rendu, comme toujours. Le générateur doit être interruptible et reprenable
(le corpus se construit par lots, chaque lot est autonome).

**Exigences** :

1. Deux exécutions avec la même graine produisent des caractéristiques
   **identiques au bit près** (testé).
2. Une machine dont l'empreinte a changé est détectée et son corpus marqué
   périmé (testé).
3. Le générateur n'utilise que le pont existant (`VsmEngine`) — zéro ligne
   nouvelle côté C++.

---

## 4. Objet 2 — le classifieur de machine

**Entrée** : les caractéristiques v2 d'un segment (une note ou un agrégat de
notes d'un stem). **Sortie** : un **classement** des machines avec score, plus
une sortie d'**abstention** : « aucune machine du parc » (→ route sampler).

- Modèle **petit et CPU** : gradient boosting ou MLP modeste (scikit-learn,
  version épinglée, graine fixée). Pas de GPU, pas de dépendance lourde.
  L'entraînement complet doit tenir en minutes.
- **Usage dans la chaîne** : remplacer ou nourrir la présélection deux passes.
  Aujourd'hui : 15 machines dégrossies sur 0,4 s, 7 finalistes, 174 s. Demain :
  le classement est quasi gratuit et la passe complète ne paie que les 3 à 5
  machines qu'il désigne — le facteur 4 de la phase 10 (84 s) sans le risque
  mesuré d'écarter la gagnante.
- **Abstention obligatoire** : score maximal sous un seuil, ou distance aux
  caractéristiques du corpus au-delà d'un rayon → « aucune machine ne
  convient », imprimé avec sa mesure. C'est le chemin normal pour une source
  acoustique — un violon classé « MS-20 avec assurance » serait le pire
  résultat possible de ce projet.

> **AMENDEMENT (23/08/2026) — où mène l'abstention, maintenant que
> `vsm.multisample` existe.** Ce paragraphe et le § 10 disent « route
> sampler », et le critère 4 ci-dessous range le piano « hors parc ». Les deux
> ont été écrits sans connaître `vsm.multisample`, livrée le même jour, dont le
> § 7 du cahier des charges annonce précisément que « la route d'abstention y
> gagne enfin une destination ». Les deux documents se contredisent donc, et
> voici la lecture retenue :
>
> **L'abstention garde son sens — « aucune machine de SYNTHÈSE du parc ne
> convient » — et sa destination devient la ROUTE ACOUSTIQUE**, dans cet
> ordre : `vsm.multisample` si un profil installé couvre l'instrument ; le
> sampler pour la voix (son seul emploi, cf. `CDC-machines-manquantes.md` § 4) ;
> et, si ni l'un ni l'autre, une piste nommée SANS instrument — parce
> qu'effacer la piste ferait disparaître l'information « il y avait quelque
> chose ici que nous n'avons pas su reproduire ».
>
> **Le critère 4 est réécrit en conséquence, et il ne se contente plus
> d'exiger l'abstention** : sur un stem de piano, l'exigence est qu'**aucune
> machine de caractère ne soit désignée avec assurance**. Ce qui est ensuite
> proposé dépend de l'installation, et le test doit dire laquelle : profil
> piano installé → `vsm.multisample` parmi les candidates ; aucun profil →
> abstention. Un violon reste hors parc tant qu'aucun profil de cordes n'est
> installé.
>
> **Un chiffre tempère cette route, et il faut le connaître avant de s'y
> fier.** Sur *Clair de Lune*, mesuré à conditions identiques,
> `vsm.multisample` finit **7e sur 30** à l'arbitrage de piste (0,3571) et la
> reconstruction est **exactement la même avec et sans elle** (0,2159 des deux
> côtés) : c'est `vsm.piano`, la machine MODÉLISÉE, qui gagne. « Acoustique →
> multisample » n'est donc pas une règle d'aiguillage mais une **mise en
> concurrence** : le classifieur NOMME des candidates, l'arbitrage sur la piste
> tranche. Détail dans [`CDC-multisample.md`](CDC-multisample.md) § 11.
- **Critères de réussite** (mesurés, dans cet ordre) :
  1. sur rendus moteur tenus à l'écart : vraie machine dans le top 3 ≥ 95 %,
     cas indistinguables exclus du dénominateur et comptés à part ;
  2. sur le banc à quatre cibles de l'étape 10.3 (SH-101, Juno-106, DX7,
     orgue) : même gagnant final que la recherche complète ;
  3. sur les vérités terrain existantes et House Of God : verdicts finaux
     inchangés ou meilleurs, temps de présélection mesuré avant/après ;
  4. sur des stems acoustiques (violon, piano — hors parc) : abstention.

---

## 5. Objet 3 — les gabarits de batterie appris

Le classement actuel (nouveauté spectrale, gabarits formés sur le morceau
lui-même) reste l'ossature — il a survécu à trois architectures et à un vrai
kick de club. Ce qui s'apprend, c'est **le nommage et la séparation des
familles proches** :

- **Corpus** : frappes générées par TR-808, TR-909 et kits du sampler, à
  accord/niveau/decay variés, **plus des superpositions construites**
  (kick+charleston à décalages connus, caisse claire sur queue de kick…) —
  précisément les cas qui ont fait tomber les architectures précédentes,
  étiquetés par construction.
- **Modèle** : gabarits spectraux par famille (ou petit classifieur sur les
  empreintes de nouveauté), consommé par `vsm_drumkit.py` au moment de nommer
  les gabarits découverts dans le morceau. La règle « une frappe, une pièce —
  celle qui explique le mieux la nouveauté » demeure.
- **Critères de réussite** (les motifs-vérité du §9.5 sont le juge) :
  1. charleston à la double-croche : détection > 24/32 (aujourd'hui 8/32),
     **sans frappe inventée** — kick et caisse claire restent à 8/8 ;
  2. charleston aux contretemps : > 14/16 (aujourd'hui 8/16) ;
  3. House Of God rejoué : co-frappes toujours à 0, familles au moins aussi
     bien séparées (6 pièces), et l'écoute ne révèle pas de régression ;
  4. plus de trois familles nommables (toms, percussions, claps) — les seize
     emplacements du sampler attendent déjà.

C'est l'objet **le plus sûr** du CDC : le gain est localisé, le juge existe,
et l'échec éventuel ne casse rien (le nommage actuel reste en repli).

---

## 6. Objet 4 — l'estimateur de paramètres

**Entrée** : caractéristiques v2 d'une cible + machine choisie. **Sortie** :
un patch estimé **avec incertitude par axe**. Ses deux usages autorisés — et
seulement eux (le warm start est interdit par la mesure, §1.1) :

- **a) Resserrer l'espace cherché.** La fenêtre de recherche d'un axe devient
  [estimation ± incertitude apprise] au lieu de la plage déclarée entière.
  Garde-fou absolu, leçon de l'étape 8.2 (« une mauvaise borne est pire que
  pas de dimension du tout ») : la **couverture** est mesurée sur données
  tenues à l'écart — la vraie valeur doit tomber dans la fenêtre ≥ 98 % des
  fois, par axe ; un axe qui n'y arrive pas garde sa plage déclarée. Le
  resserrement est imprimé (axe, fenêtre, couverture mesurée), jamais tacite.
- **b) Servir de candidat bon marché.** Le patch estimé est rendu et évalué
  **une fois** par le moteur. S'il fait déjà mieux que ce que la recherche au
  budget par défaut obtient d'ordinaire, la recherche est raccourcie ou
  sautée — verdict imprimé avec les deux distances. Cas typique visé : les
  cibles « faciles » qui consomment aujourd'hui le même budget que les
  difficiles.
- **Abstention** : incertitude trop large → la chaîne actuelle s'exécute telle
  quelle. L'estimateur ne doit jamais coûter plus qu'il ne rapporte : son
  A/B global (§ critères) l'établit ou le fait retirer, comme 10.1 et 10.2.
- **Critères de réussite** : sur un banc de cibles tenues à l'écart, médiane
  des distances finales ≤ celle de la chaîne actuelle avec **au plus la
  moitié des évaluations** ; et sur les vérités terrain réelles, aucun
  verdict dégradé.

C'est l'objet le **plus risqué** du CDC — deux idées voisines (10.1, 10.2) ont
déjà été mesurées puis rejetées. Il passe en dernier, et son rejet éventuel,
chiffré, sera un résultat au même titre que son adoption.

---

## 7. Augmentation — franchir l'écart de domaine

Entre le corpus (rendu sec) et un stem réel, l'entraînement intercale des
dégradations plausibles, appliquées **aux caractéristiques via l'audio** rendu
du lot en cours : égalisation douce aléatoire, réverbération courte,
compression, bruit de fond à −40/−60 dB, fuite d'une autre source à faible
niveau, léger désaccord. Chaque augmentation est seedée et inscrite au
manifeste. La mesure d'efficacité est unique : les critères des §4-6 sur
**stems réels** (vérités terrain, House Of God), pas sur le corpus.

---

## 8. Contraintes d'ingénierie

1. **Tout vit dans `analyse/`** (Python). Zéro ligne nouvelle dans `core/`,
   `audio/`, `interchange/`, `app/`. Les invariants de
   [`ROADMAP-fusion.md`](ROADMAP-fusion.md) §7 s'appliquent mot pour mot.
2. **Repli permanent** : sans modèle sur disque (ou modèle périmé), la chaîne
   se comporte **exactement** comme aujourd'hui. `reconstruire.py
   --sans-apprentissage` force ce repli et sert de témoin A/B.
3. **Rien de silencieux** : chaque décision d'un modèle (classement retenu,
   abstention, fenêtre resserrée, candidat accepté/refusé) est imprimée avec
   sa mesure, et `rapport.json` porte les versions de corpus et de modèles
   utilisées.
4. **Déterminisme de bout en bout** : corpus, entraînement et inférence
   seedés, versions épinglées. Deux entraînements sur le même corpus donnent
   les mêmes verdicts sur le banc (l'identité bit à bit des poids est
   souhaitée mais c'est l'identité des **verdicts** qui est exigée et testée).
5. **CPU seulement**, corpus complet + entraînements en moins d'une nuit sur
   la machine de développement. Ce projet n'a pas de ferme de calcul et n'en
   veut pas.

## 9. Anti-objectifs

- **Pas de modèle qui produit du son.** Ni synthèse neuronale, ni
  « correction » apprise du rendu. Le jour où un modèle touche à l'audio
  entendu, la promesse du projet (le DAW joue ce que l'analyse a optimisé)
  est morte.
- **Pas de reconstruction de la voix** (inchangé, ROADMAP-fusion §6).
- **Pas d'entraînement sur des enregistrements du commerce.**
- **Pas de course à la précision sur les timbres indistinguables** (§1.4).
- **Pas de nouvelle dépendance lourde** : scikit-learn et numpy suffisent ;
  toute proposition au-delà (torch…) exige une mesure prouvant que le petit
  modèle plafonne.

## 10. Le banc final — un morceau de musique classique

La validation de bout en bout se fera sur un enregistrement classique — le
terrain le plus **hostile** au parc, et c'est voulu. Ce qu'il faut en attendre,
écrit avant de mesurer :

- la séparation (demucs) vise des stems pop (batterie/basse/voix/autres) : un
  orchestre sortira essentiellement en « autres », peu séparé ;
- presque aucun stem n'a de machine cible : le résultat honnête est
  l'**abstention** du classifieur presque partout, la route sampler, et
  peut-être l'e-piano ou l'orgue sur ce qui s'en approche ;
- la polyphonie dense mettra le transcripteur à l'épreuve plus que la
  recherche de patch.

Le succès de ce banc n'est **pas** une petite distance : c'est un rapport qui
dit vrai — abstentions motivées, distances publiées, confiances par note dans
le piano roll — et une écoute A/B qui ne surprend pas celui qui a lu le
rapport. Si le classifieur y classe un violon en MS-20 avec assurance, le §4
a échoué, quel que soit son score sur corpus.

---

*Ce document suit les conventions de `CDC-nouvelle-machine.md` et
`CDC-machines-manquantes.md` : toute affirmation de gain doit arriver avec sa
mesure, et une piste rejetée se documente avec les chiffres qui l'ont rejetée.*
