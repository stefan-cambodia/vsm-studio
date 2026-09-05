# La séparation par synthèse — la boucle résiduelle — cahier des charges

Chantier ouvert le 05/09/2026, au lendemain du verdict de S1. Il part d'un
constat que le banc a chiffré et que personne ne peut plus contourner : **le
nombre et le contenu des pistes sont décidés par un séparateur entraîné sur
de la pop, et il rend une basse inutilisable.**

## 0. La question, et la réponse courte

**La question.** La chaîne reconstruit une piste par stem séparé. Le
séparateur (`htdemucs_6s`) décide donc de tout ce qui vient après : combien
de parties on cherchera, et dans quel signal. S1 (CDC banc § 6) a mesuré ce
qu'il rend sur vingt morceaux à vérité connue : `bass` à **0,21 dB** de SDR
et **0,26** de corrélation avec la vraie basse — un stem qui n'est pas la
partie —, et l'arbitrage bat la vérité de référence sur **42 pistes sur 43**
parce que régler une machine pour ressembler au stem SÉPARÉ marche mieux que
viser le stem VRAI. Tout l'aval est plafonné par l'amont. Que peut-on faire
de mieux qu'un séparateur, sans en entraîner un ?

**La réponse courte.** Le projet a ce qu'aucun séparateur n'a : un moteur
déterministe qui sait RENDRE une piste une fois trouvée. La boucle
résiduelle en tire parti. Elle prend la partie la plus sûre parmi celles que
la chaîne a déjà reconstruites, la rend seule, l'aligne sur le mélange, la
SOUSTRAIT, resépare ce qui reste, et relance la chaîne sur ce résidu — qui
est un mélange plus simple que le précédent. Elle recommence jusqu'à ce que
le résidu ne contienne plus de partie discernable. **Le nombre de pistes
émerge du signal** — c'est la parité par un autre chemin —, et le banc
synthétique, qui connaît le résidu VRAI à chaque étape (le mélange moins les
stems vrais des parties déjà retenues), est le seul juge.

Ce que la boucle N'EST PAS est écrit au § 3 avant ce qu'elle est : elle ne
touche pas à ce qu'on entend, elle ne remplace pas la séparation, et elle
n'apprend rien.

## 1. Ce que S1 a établi, et que ce chantier ne rouvre pas

- **La séparation plafonne tout** (CDC banc § 6, lot sec) : `bass` 0,21 dB
  et corrélation 0,26 ; `drums` 5,07 dB ; `other` 2,75 dB ; énergie
  hallucinée 19,8 %. La production ne change presque rien (`bass` 0,05 dB
  avec elle) : ce n'est pas une chute, c'est un plancher rasé de plus près.
  Ce chantier ne mesure pas à nouveau la séparation : il la prend telle
  qu'elle est, et mesure ce qu'elle rend sur un mélange que la boucle a
  SIMPLIFIÉ.
- **L'arbitrage et le réglage font mieux que la vérité de référence**
  (42/43) — donc **le stem séparé n'est pas le stem vrai**. Une piste réglée
  sur un stem séparé est une copie d'une copie ; c'est cette copie que la
  boucle soustrait, et c'est pour cela que la soustraction est gardée par
  une corrélation MESURÉE (§ 2.3) et jamais supposée.
- **Le bon bout est le découpage amont, pas le juge** (verdict H25, CDC
  banc § 8) : un modèle appris pour dire « une ou deux parties » est inerte
  sur disque et ne fait pas mieux que « toujours deux ». On ne cherche donc
  pas un meilleur juge de ce que le séparateur a fondu ; on cherche à lui
  donner un mélange où il y a MOINS à fondre.
- **La parité est sans plafond, et la structure gagne** (CDC multipiste
  § 0) : « si un original comporte 15 postes, la reconstruction doit
  comporter 15 pistes ». Une version qui sépare mieux peut mesurer PLUS LOIN
  ; quand ce cas se présente, la structure gagne et l'écart se publie. Ce
  chantier hérite de la règle mot pour mot — et il hérite aussi de son
  envers, qui n'était pas écrit : une piste INVENTÉE n'est pas de la
  structure, c'est du bruit. Le § 2.5 dit comment la boucle s'en garde.
- **La transcription et la parité restent le premier poste de perte
  (0,307, dans 20 morceaux sur 20)**. La boucle ne touche pas au
  transcripteur ; elle lui donne, à chaque itération, un stem issu d'un
  mélange plus simple. Si le résidu n'est pas plus propre que le mélange,
  elle ne peut rien pour lui, et la campagne le dira.

## 2. Objets

### 2.1 Une unité rendue

Ce qui se soustrait est une **unité** : une piste reconstruite, ou le
GROUPE des pistes qui partagent un stem (les pièces d'une batterie éclatée,
les voix d'un `other` découpé par registres). On soustrait le groupe entier,
jamais une voix seule : une voix sur quatre comparée au stem entier de
`other` serait toujours « peu corrélée », non parce qu'elle est fausse mais
parce qu'elle est un quart.

Une unité porte :

- ses membres (les pistes d'export, avec leur patch réglé, leur volume
  calé, leur automation — celles du rendu final, pas d'autres) ;
- son stem de référence (le stem séparé contre lequel ses membres ont été
  jugés) et la **part d'énergie** de ce stem dans le mélange d'origine ;
- sa **distance de piste** : celle de ses membres, moyennée quand ils sont
  plusieurs (la distance de `Batterie`, celle de chaque voix pour `other`) ;
- l'itération qui l'a produite (0 pour la chaîne d'aujourd'hui, k pour le
  résidu k).

Ne sont PAS des unités : les pistes audio (la voix reportée telle quelle
n'est pas rendue, donc pas soustraite — son stem est déjà le résidu de
lui-même), et les pistes sans machine ou sans note.

### 2.2 Le pas de boucle

Itération k (k = 1 … N, `--residuel N`), à partir du résidu m_{k-1} (m_0 est
le mélange lu, en mono, celui que toute la chaîne mesure) :

a. **Choisir la plus sûre.** Pour CHAQUE unité non encore soustraite, la
   rendre seule (`vsm-render`, même moteur, mêmes options que le rendu
   final), l'aligner sur m_{k-1} (§ 2.4), et mesurer sa corrélation à son
   stem. Tout est imprimé et publié, pour toutes les candidates, soustraites
   ou non — c'est ainsi que le seuil pourra être revu avec des chiffres.
   La plus sûre est celle de plus grand **score = part d'énergie / distance
   de piste** (§ 2.3) parmi celles qui passent le garde-fou. Aucune ne le
   passe → arrêt, motif `aucune-piste-sure`.
b. **Soustraire.** m_k = m_{k-1} − g · r décalé de d échantillons (gain g et
   décalage d publiés). UNE unité par itération : chaque itération a une
   variable, et l'effet d'une soustraction se lit seul. N borne donc le
   nombre de soustractions.
c. **Publier la qualité.** Énergie du résidu avant et après, en absolu et
   en part du mélange d'origine ; le résidu est écrit sur disque
   (`residu-r{k}/residu.wav` dans le dossier de travail, conservé par
   `--garder-stems`) pour que le banc le compare au résidu VRAI. Le résidu
   sous `--residuel-energie` (part du mélange d'origine, 5 % par défaut) →
   arrêt, motif `residu-sous-le-seuil`, sans reséparer : il n'y a plus rien
   à chercher.
d. **Reséparer et relancer.** `htdemucs_6s` en sous-processus, `shifts=0`,
   sur m_k ; puis transcription → parité → arbitrage → réglage sur les stems
   du résidu UNIQUEMENT, avec le budget ordinaire et les mêmes options que
   l'itération 0, sauf ce que le § 5 dit. Les parts d'énergie des stems du
   résidu se mesurent contre le mélange D'ORIGINE, pas contre le résidu :
   sinon un résidu de 5 % verrait tous ses stems passer le seuil de 0,5 %.
   Les pistes obtenues portent leur itération dans leur nom (« other · r1 »,
   « Batterie · kick · r1 », groupe « Batterie · r1 »).
e. **Arrêter, en disant pourquoi.** Après l'itération : aucune piste ajoutée
   → `rien-de-discernable` ; la distance du projet en l'état n'a pas baissé
   → `distance-sans-gain` (les pistes ajoutées sont GARDÉES, § 5) ; k = N →
   `iterations-atteintes`. Chaque arrêt imprime son motif et ses chiffres.

**Une piste retenue est GELÉE.** La boucle ajoute, elle ne rejuge pas : ni
la machine, ni le patch, ni les notes d'une piste d'une itération
précédente ne bougent. Rejuger viendra, avec sa propre campagne. Le verdict
du mélange, lui, tourne UNE fois à la fin sur toutes les pistes, comme
aujourd'hui — il n'est pas la boucle, c'est la fin ordinaire de la chaîne.

### 2.3 Le critère de sûreté, écrit d'avance

**Le score.** Une soustraction rapporte d'autant plus qu'elle enlève
d'énergie au mélange, et coûte d'autant moins que ce qu'elle enlève
ressemble à la partie. La part d'énergie du stem mesure la première ; la
distance de piste est le seul indicateur de la seconde qu'on ait AVANT de
rendre. D'où `score = part / distance`, décroissant : la piste la plus
proche de son stem, pondérée par ce qu'elle pèse. Les égalités se départagent
par le nom, pour que deux exécutions choisissent la même.

**Le garde-fou, et pourquoi il est mesuré et non déduit du score.** Une
piste douteuse soustraite abîme le mélange plus qu'elle ne l'éclaircit. On
ne soustrait donc que si le rendu, aligné et réglé au mélange, est CORRÉLÉ
à son stem au-delà de `--residuel-correlation` (**0,5** par défaut). Le
chiffre a une raison, et elle est arithmétique : avec le gain des moindres
carrés, la soustraction retire une fraction c² de l'énergie de la partie
(c = corrélation) et n'en ajoute jamais. À c = 0,5 elle en retire un quart
et en laisse trois quarts dans le résidu — au-dessous, la partie reste
presque entière, et la réséparation la retrouverait : un doublon plus
probable qu'une découverte. À c = 0,7 la moitié part ; à 0,9, les quatre
cinquièmes. Le seuil trace donc la limite entre « inutile » et « utile »,
pas entre « sain » et « nocif » : le nocif est le rendu qui ressemble AUX
AUTRES parties, et la corrélation du rendu au RESTE du mélange (le résidu
moins le stem) est publiée à côté, sans agir, pour que la campagne dise si
elle aurait dû.

Ce que la corrélation au niveau de l'échantillon mesure, et ce qu'elle
manque, est dit ici pour que la campagne ne soit pas lue de travers : un
rendu qui joue les bonnes notes avec la bonne machine mais un oscillateur
en phase différente, ou désaccordé d'un dixième de demi-ton, se décorrèle
en une seconde. La corrélation attendue d'une partie mélodique est donc
BASSE par nature sur disque, et sur le banc elle n'est haute que si la
machine, le patch, les notes et la phase sont retrouvés ensemble. La
batterie, faite de transitoires courts, y échappe en partie. C'est le
risque nommé du chantier, pas une surprise à découvrir ; l'hypothèse de
repli est écrite au § 7.

### 2.4 L'alignement et la soustraction

- **Décalage entier** : le maximum de la corrélation croisée entre le rendu
  et m_{k-1}, cherché dans ±50 ms (±2 205 échantillons), calculé par FFT.
  Publié en échantillons et en millisecondes.
- **Gain** : le scalaire des moindres carrés au décalage retenu,
  g = ⟨m_{k-1}, r_d⟩ / ⟨r_d, r_d⟩. Publié. Un gain nul ou négatif est un
  refus DIT (« rendu sans rapport, ou en opposition ») : on ne soustrait
  pas un signal pour l'ajouter.
- **Jamais de filtrage adaptatif silencieux** : ni Wiener, ni égalisation,
  ni gain par bande. Un scalaire, un décalage, publiés. Ce qu'un tel
  filtrage gagnerait est une hypothèse à écrire avant sa mesure, pas un
  réglage à glisser dans la boucle.
- Le rendu de l'unité est rendu en **mono** (le pli du moteur), comme tout
  ce que la chaîne mesure ; le résidu est mono. La séparation du résidu ne
  dispose donc plus du champ stéréo — une limite publiée (§ 5).

### 2.5 Les doublons : l'envers de la parité

Une soustraction à c = 0,6 laisse 64 % de la partie dans le résidu ; la
réséparation la retrouve, la transcription la retranscrit, et la chaîne en
ferait une seconde piste. Sans garde, chaque itération FABRIQUERAIT une
copie de chaque partie déjà retenue — l'exact contraire de la parité. La
règle, appliquée dans la passe du résidu et nulle part ailleurs :

- une note transcrite dans un stem du résidu qui est DÉJÀ PORTÉE par une
  piste retenue (même hauteur à ±1 demi-ton, même attaque à ±50 ms — les
  tolérances du banc, CDC banc § 2.4) n'est pas une note nouvelle ; une
  frappe de batterie déjà portée (même instant à ±30 ms, quelle que soit la
  pièce) non plus ;
- ce qui reste est le matériau NOUVEAU du stem. S'il compte moins de
  `--residuel-notes-min` notes (**8** par défaut : une phrase), le stem n'a
  rien de nouveau, il est refusé AVANT l'arbitrage (qui est le poste
  coûteux) et le refus est dit avec ses trois nombres — transcrites, déjà
  portées, nouvelles ;
- sinon, la piste se reconstruit sur les notes nouvelles SEULES, et le
  rapport porte les trois nombres.

Ce filtre agit sur les notes, jamais sur l'audio : une note déjà portée est
retirée d'une transcription, pas d'un signal. Et il ne rejuge rien : les
notes de l'itération 0 restent celles de l'itération 0.

### 2.6 Ce que le rapport porte

`rapport.json` reçoit un bloc `residuel`, absent sans l'option :

```
residuel:
  demande: N
  options: { correlation, energie, notesMin, separateur }
  iterations: [ pour chaque k :
    candidats: [ { unite, membres, iteration, part, distance, score,
                   decalageEchantillons, decalageMs, gain,
                   correlationStem, correlationReste, retenue: bool, motif } ]
    soustraction: { unite, ... les mêmes chiffres ... }
    energie: { avant, apres, partAvant, partApres }   (part du mélange d'origine)
    residu: chemin du WAV, stems: chemin, partage: [ {stem, partEnergie} ]
    stemsRefuses: [ { stem, motif, transcrites, dejaPortees, nouvelles } ]
    pistesAjoutees: [ { piste, machine, notes, nouvelles, dejaPortees } ]
    distanceProjet: { avant, apres }    (le projet en l'état, avant verdict)
    secondes: { rendus, separation, reconstruction, total }
  ]
  arret: { motif, detail, iteration }
```

et la provenance porte `residuel`, `residuelCorrelation`, `residuelEnergie`,
`residuelNotesMin` — des options qui conditionnent le résultat, donc des
options de la provenance (CLAUDE.md, § Mesure).

### 2.7 Ce que le banc lit

`banc_synthetique.py` publie par morceau et par itération, quand la course
porte un bloc `residuel` :

- **le SDR du résidu contre le résidu vrai** : résidu vrai_k = mélange −
  Σ stems vrais des parties déjà retenues à k (une partie retenue est celle
  dont une piste soustraite porte le plus de notes appariées — l'attribution
  du banc ; la batterie soustraite retient la partie batterie). Deux
  chiffres : avant la soustraction (m_{k-1}) et après (m_k), pour que la
  MONTÉE soit lisible ;
- **la séparation du résidu** : le SDR de chaque stem séparé du résidu
  contre le stem vrai des parties NON retenues, `bass` en tête — c'est le
  chiffre de S1 (0,21 dB) rejoué sur le résidu ;
- **la parité atteinte à k** : pistes, fondues, inventées, en ne comptant
  que les pistes d'itération ≤ k (le suffixe « · r{k} » les nomme) ;
- **la distance à k** : celle que la chaîne a publiée en l'état
  (`distanceProjet`), et la distance finale ;
- les pistes ajoutées, les stems refusés pour doublon, les secondes.

La comparaison ligne à ligne avec S1 (mêmes 20 morceaux, mêmes graines,
`analyse/comparer_bancs.py`) est le seul témoin recevable : une médiane sur
vingt cache huit morceaux qui bougent dans un sens et douze dans l'autre.

## 3. Anti-objectifs

1. **La boucle ne touche jamais à l'audio ENTENDU.** Le DAW joue les pistes
   reconstruites ; `comparaison.wav` reste l'original contre le projet
   rendu. Le résidu est un OBJET DE MESURE : il vit dans le dossier de
   travail, aucune piste du projet ne le référence, et il n'entre dans
   aucun rendu. Soustraire pour écouter serait une autre idée, et une
   mauvaise : on entendrait les défauts de la soustraction à la place des
   défauts de la reconstruction.
2. **Elle ne remplace pas la séparation, elle la raffine.** Sans option, la
   chaîne est la chaîne d'aujourd'hui : mêmes stems, mêmes pistes, mêmes
   notes, mêmes patchs, même projet, même audio, mêmes distances, au bit
   près ; `rapport.json` ne diffère que par les clés de provenance des
   nouvelles options, à leur valeur de repos. Un test le tient. La première
   itération part de ce que le séparateur a rendu ; la boucle ne
   fonctionne qu'avec lui.
3. **Pas de modèle appris dans cette phase.** Ni pour choisir l'unité, ni
   pour aligner, ni pour dire si le résidu contient encore quelque chose.
   Le critère est une formule publiée, le garde-fou une corrélation
   mesurée, l'arrêt un chiffre. Le corpus généré a montré trois fois qu'il
   n'apprend pas ce qu'un disque contient (CDC banc § 1).
4. **Pas une ligne hors de `analyse/`** (et `docs/`). Ni `core/`, ni
   `audio/`, ni `interchange/`, ni `app/` : une campagne en cours crierait
   « moteur périmé », et deux mesures deviendraient incomparables.
5. **La boucle ne rejuge pas.** Une piste retenue est gelée (§ 2.2). Ce
   qu'un second passage sur une piste déjà faite gagnerait est une autre
   campagne, avec ses propres attendus.

## 4. Critères d'acceptation

```
[ ] Identité sans option : deux courses sur le morceau minuscule, l'une sans
    --residuel et l'autre avec --residuel 0, rendent project.json, le MIDI
    et rapport.json identiques octet pour octet, et la boucle n'est jamais
    appelée (testé)
[ ] Alignement sur un cas construit : un rendu décalé de +37 échantillons et
    atténué de moitié dans du bruit est retrouvé à d = 37, g = 0,50 ± 0,02
    (testé)
[ ] Soustraction exacte : un mélange qui EST le rendu d'une piste, moins ce
    rendu réaligné, donne un résidu NUL au bit près, gain 1,0, décalage 0
    (testé avec le vrai moteur)
[ ] Le garde-fou refuse : un rendu décorrélé (bruit) n'est pas soustrait, le
    refus porte sa corrélation (testé)
[ ] Chaque motif d'arrêt se déclenche et se nomme : aucune-piste-sure,
    residu-sous-le-seuil, rien-de-discernable, distance-sans-gain,
    iterations-atteintes (testés un par un, sur une boucle à collaborateurs
    factices)
[ ] Les doublons sont refusés avant l'arbitrage, avec leurs trois nombres ;
    les notes nouvelles seules font une piste (testé)
[ ] La boucle tourne de bout en bout sur le morceau minuscule commis
    (--residuel 1, séparateur du résidu injecté) : une soustraction dite avec
    ses chiffres, un résidu écrit, le bloc `residuel` au rapport avec son
    motif d'arrêt, les pistes du résidu nommées « · r1 » (testé)
[ ] Déterminisme : deux exécutions --residuel 1 sur le morceau minuscule
    rendent le même rapport à la 4e décimale (testé)
[ ] Rien de silencieux : chaque rendu de candidate, chaque soustraction,
    chaque refus, chaque arrêt est imprimé avec sa mesure (lu au journal du
    test de bout en bout)
[ ] Le banc lit la boucle : SDR du résidu vrai avant/après, séparation du
    résidu, parité à k, distance à k, sur le morceau minuscule couru avec
    --residuel 1 (testé)
[ ] comparer_bancs.py met deux rapports de banc côte à côte, morceau par
    morceau (testé sur deux rapports factices)
[ ] Provenance : residuel et ses seuils dans rapport.json (testé)
[ ] Tous les modules de la boucle sont importés au départ
    (charger_tous_les_modules, test_modules_au_depart)
[ ] Zéro ligne hors de analyse/ et docs/ (git diff --stat de chaque commit)
[ ] Suites vertes, zéro warning
[ ] README, MODE-EMPLOI et ROADMAP-fusion nomment le chantier
[ ] Campagne R1 écrite AVANT sa mesure (§ 6)
[ ] Verdict de R1 écrit APRÈS, avec les chiffres et la décision annoncée (§ 7)
```

## 5. Décisions prises en écrivant

Les points que le texte de commande laissait ouverts, tranchés ici avec
leur raison, pour ne pas être retranchés après la mesure.

- **L'arrêt « aucune nouvelle piste ne baisse la distance » ne DÉFAIT
  rien.** Le § 0 du CDC multipiste dit que la distance peut monter en
  rendant le projet retravaillable, et que la structure gagne alors. Un
  arrêt qui retirerait les pistes de l'itération obéirait au critère que
  le § 0 interdit comme seul juge. Donc : l'itération dont les pistes ne
  baissent pas la distance du projet est la DERNIÈRE, ses pistes restent,
  l'écart se publie (`distance-sans-gain`, avec les deux chiffres). La
  distance mesurée pour ce motif est celle du projet EN L'ÉTAT — pistes de
  toutes les itérations, volumes calés, automation, mais AVANT le verdict
  du mélange, qui tourne une fois à la fin — et le rapport la nomme ainsi.
- **Une unité par itération, pas toutes les unités sûres.** Le texte dit
  « la partie la plus sûre » ; la tentation était de soustraire d'un coup
  tout ce qui passe le garde-fou, pour que le résidu maigrisse plus vite.
  On s'en tient à une : chaque itération a une variable, et `--residuel 3`
  mesure trois soustractions successives, chacune lisible seule. Si la
  campagne montre que les soustractions sûres sont nombreuses et que N les
  bride, ce sera un chiffre pour la prochaine.
- **Les doublons se refusent, ils ne se publient pas comme pistes** (§ 2.5).
  Le compte des parties inventées demandé « à part » est bien publié — par
  le banc, sur les pistes AJOUTÉES —, et le rapport de la chaîne porte en
  plus les stems refusés pour doublon avec leurs nombres. Deux comptes,
  deux questions : ce que la boucle a fabriqué, ce qu'elle a failli
  fabriquer.
- **La voix n'est pas reprise dans le résidu.** Elle est reportée telle
  quelle à l'itération 0 (piste audio, pas rendue, donc pas soustraite) ;
  le stem `vocals` du résidu est le même stem moins rien, et en refaire une
  piste serait un doublon audio. Il est ignoré en le disant, avec sa part.
- **La batterie du résidu est modélisée seulement.** `build_drum_kit`
  écrit ses échantillons sous `samples/<famille>.wav` ; une seconde
  détection écraserait ceux de l'itération 0. Dans la passe du résidu, les
  échantillons ne s'écrivent pas — la batterie modélisée (le défaut) n'en a
  pas besoin, et `--batterie-echantillonnee` ne s'applique donc pas aux
  batteries d'un résidu. Dit au journal quand le cas se présente.
- **Le résidu est mono.** Toute la chaîne mesure sur le pli mono (la
  distance, les niveaux, le verdict), et le moteur rend en stéréo un signal
  que `read_render_wav` replie. Soustraire en stéréo demanderait de rendre
  et d'aligner deux canaux dont la chaîne ne mesure aucun. Le coût est que
  la séparation du résidu ne voit plus le champ stéréo (la voix
  tête/chœurs, notamment, n'y a plus de sens — et elle n'y est pas reprise,
  voir plus haut). Si la boucle survit à R1, mesurer ce que la stéréo lui
  rendrait est une campagne à part.
- **Le seuil d'énergie d'arrêt vaut 5 % du mélange d'origine.** Au-dessous,
  le résidu est du même ordre que l'énergie hallucinée par la séparation
  (14,6 à 19,8 % à S1) divisée par trois : chercher une partie dedans,
  c'est reconstruire le bruit du séparateur. Le chiffre est une option, et
  il ira dans la provenance.
- **La campagne court dans l'ordre de la décision, pas dans l'ordre du
  texte.** R1 à `--residuel 1` sur les deux lots d'abord, puis les deux
  disques à `--residuel 1` (ce sont eux qui tranchent le défaut), puis
  `--residuel 3` sur les deux lots. Le poste s'est déjà éteint au milieu
  d'une campagne (CDC multipiste § 9) ; ce qui porte la décision passe en
  premier, et tout est reprenable (une course dont `rapport.json` existe
  n'est pas rejouée).

## 6. Campagne R1 — attendus écrits AVANT la mesure (05/09/2026, 09:05)

Sur les vingt morceaux de S1 (lots `s1-sec` et `s1-prod`, graines 1 à 10,
30 s), chaîne aux défauts du jour (commit 322cc03 : `htdemucs_6s`,
`--parite`, tout le parc, second verdict), 6 rendus parallèles, plus
`--residuel 1` ; puis `--residuel 3` ; puis les deux disques. Le témoin est
S1 elle-même : `reconstruire.py` et ses modules n'ont pas changé depuis ses
deux commits (`git diff --stat 7006939..HEAD -- analyse/` ne touche que les
fichiers d'H25), donc **un morceau où la boucle ne soustrait rien doit
rendre la distance de S1 à la 4e décimale** — c'est la première chose à
vérifier, et si elle manque, la mesure est cassée avant d'avoir commencé.

Les chiffres de S1 qu'il faut battre, lot sec / lot prod : `bass` SDR
médian **0,21 / 0,05 dB** ; écart de parité médian **−2 / −2**, écart ≤ 1
sur **2/10 / 4/10** ; parties inventées **24 / 31** (sommes) ; distance
globale médiane **0,186 / 0,199** ; secondes par course (médiane) **≈ 620 /
≈ 680**.

**Ce qui sera soustrait.** La batterie porte la plus grande part d'énergie
dans huit morceaux sur dix de chaque lot, et sa boîte vraie est au rang 1
dans six ; ses transitoires se corrèlent mieux qu'un oscillateur. J'attends
que l'unité soustraite à la première itération soit `Batterie` dans **au
moins 6 morceaux sur les 16 qui en ont**, avec une corrélation au stem
médiane entre **0,4 et 0,8** ; et que les unités mélodiques passent le
garde-fou dans **moins de 4 morceaux sur 20** (corrélations médianes sous
0,3 — la phase, § 2.3). Au total : entre **6 et 14 morceaux sur 20** avec
une soustraction, les autres s'arrêtant sur `aucune-piste-sure` en rendant
exactement S1. *Réfuterait le seuil, pas la boucle* : aucune soustraction
sur 20 morceaux, avec des corrélations de batterie toutes sous 0,5 — le
seuil serait alors à revoir avec les corrélations publiées, avant tout
autre verdict.

**Le SDR du résidu contre le résidu vrai.** Sur les morceaux où une unité
est soustraite, l'arithmétique du § 2.3 donne une montée de 10·log(1/(1−c²))
: +1,2 dB à c = 0,5, +2,9 dB à c = 0,7. J'attends une montée médiane entre
**+1 et +4 dB**. *Réfuterait* : moins de +0,5 dB médian — la soustraction
enlèverait autre chose que la partie.

**Le SDR de la basse dans le résidu** (le chiffre de l'utilisateur).
Après soustraction de la batterie, le séparateur voit un mélange sans
transitoires ; la basse synthétique reste ce qu'elle est (un `vsm.banjo` ou
un `vsm.wind` en guise de basse, ce qu'un modèle entraîné sur la pop ne
reconnaît pas). J'attends une montée MODESTE : médiane sur les morceaux
soustraits entre **0,5 et 2,0 dB** (contre 0,21), soit **+0,3 à +1,8 dB**.
*Réfuterait sur le banc* : moins de +0,3 dB médian — le résidu n'aiderait
pas le séparateur sur la basse, et la boucle ne peut rien pour le premier
poste de perte. L'utilisateur attend « nettement au-dessus de 0,21 » ; mon
chiffre est plus bas que le sien, et il est écrit pour être confronté.

**La parité.** Le résidu ne fera apparaître une partie fondue que si sa
transcription porte des notes NOUVELLES (§ 2.5) : celles que l'itération 0
n'a pas transcrites parce que la batterie les couvrait. J'attends **entre 0
et 2 pistes ajoutées par morceau**, médiane **1**, et **plus de stems
refusés pour doublon que de pistes ajoutées** (au moins 2 refus par
morceau soustrait). Sur la parité finale : écart médian **−2 → −1** au
mieux, inchangé au pire ; et le compte des parties inventées, publié à
part, **ne dépasse pas 24 + 8 (sec) et 31 + 8 (prod)** — au-delà, la
boucle invente plus qu'elle ne découvre, et c'est le chiffre qui la
condamne. Les trois cas de parité (`memes-machine-disjoints` 0/3 séparé à
S1) : j'attends qu'ils restent fondus 3 fois sur 3 — la boucle ne découpe
pas un stem, elle en resépare un résidu.

**La distance globale.** Une piste ajoutée est jugée au verdict du mélange
avec les autres ; les doublons sont refusés avant. J'attends la distance
finale médiane **entre −3 % et +3 %** de S1 (0,186 / 0,199), et qu'elle
MONTE dans au moins un morceau où une piste a été ajoutée — la structure
qui gagne sur la ressemblance, publiée. Le motif `distance-sans-gain` se
déclenchera dans **au moins 3 morceaux sur 20** à `--residuel 3`.

**Le coût.** Une itération rend chaque candidate une fois (secondes),
sépare un résidu (20 à 40 s), transcrit ses stems, et n'arbitre que les
stems qui ont du nouveau. J'attends **+20 à +50 %** de temps par course à
`--residuel 1` (≈ 750 à 930 s médian), **+40 à +120 %** à `--residuel 3`.
Au-delà de +150 %, le coût seul disqualifie le défaut.

**À `--residuel 3`.** J'attends que la deuxième soustraction ait lieu dans
**moins de la moitié** des morceaux soustraits une fois (les unités
mélodiques ne passent pas le garde-fou), et que l'arrêt majoritaire soit
`aucune-piste-sure` à k = 2. Le SDR du résidu vrai ne devrait plus monter
de plus de 1 dB après la première soustraction.

**Les deux disques**, mêmes options que leurs derniers témoins,
`--residuel 1` :

| disque | témoin | options du témoin | attendu |
|---|---|---|---|
| *Us and Them* | `usandthem-parite-parc60` : 0,19000, 11 pistes, 8 632 s (commit 44fa8c4 ; le second verdict n'était pas encore le défaut — on le laisse ÉTEINT ici par `--second-verdict 0`, pour que la boucle soit la seule variable face à ce témoin) | `--stems ../reconstruction/travail/usandthem --parite --budget-piste 120 --axes-piste 21 --tours-verdict 3 --machines-au-melange 6 --second-verdict 0 --rendus-paralleles 8` | la batterie de *Us and Them* est jouée par un vrai batteur et rendue par `vsm.drums` : corrélation au stem **sous 0,5** ; aucune unité sûre, `aucune-piste-sure`, **11 pistes**, distance **0,1900 à la 4e décimale** (rien n'a été soustrait, rien ne doit bouger), coût +5 % (les rendus des candidates) |
| *Sky and Sand* | `sky-parite-m9-sv1` : 0,228156, 7 pistes, 11 886 s (commit 6a79c69) | `--stems ../reconstruction/travail/sky-6s --parite --budget-piste 120 --axes-piste 21 --tours-verdict 3 --machines-au-melange 9 --second-verdict 1 --rendus-paralleles 6` | la batterie est une boîte (techno, 1993) et porte 78 % du morceau : c'est le cas le plus favorable de tout le chantier. Corrélation de `Batterie` au stem entre **0,4 et 0,7** ; soustraite dans la moitié des mondes ; si elle l'est, le résidu descend sous 40 % du mélange, `other` du résidu porte des notes nouvelles (**+1 à +3 pistes**, 8 à 10 au total), distance **entre −4 % et +4 %** de 0,2282 ; coût +30 à +60 % |

L'écoute de `comparaison.wav` des deux disques sera notée au § 7, en
quelques lignes, avant de lire les chiffres.

**Décision écrite d'avance.** Si le SDR du résidu vrai monte (≥ +1 dB
médian) ET que la parité se rapproche (écart médian −1 ou mieux, ou écart
≤ 1 sur ≥ 4/10 par lot) SANS parties inventées au-delà du plafond ci-dessus
: `--residuel 1` devient le défaut de `reconstruire.py`, son coût
s'accepte, et `0` reste le témoin. Si le résidu n'est pas plus propre
(< +0,5 dB), ou si la basse ne bouge pas (< +0,3 dB) ET que la parité ne
bouge pas : l'option reste à 0, le chiffre se publie — le sort d'A3 et
d'H25. Entre les deux (le résidu monte mais la parité ne suit pas) :
l'option reste à 0, et le § 7 dit lequel des deux chaînons manque, avec le
chiffre de chacun.

## 7. Campagne R1 — verdict

*(à écrire après la mesure, avec les chiffres en face de chaque attendu du
§ 6, l'écoute des deux disques, et la décision annoncée)*

**L'hypothèse de repli, écrite maintenant pour ne pas être inventée
après.** Si la boucle est réfutée par la CORRÉLATION (les rendus mélodiques
ne passent jamais le garde-fou, la batterie rarement), le chaînon qui
manque n'est pas l'idée mais la soustraction au niveau de l'échantillon,
aveugle à la phase (§ 2.3). L'hypothèse suivante serait une soustraction
sur le MODULE du spectre (le rendu retire son amplitude à celle du mélange,
bande par bande et trame par trame, la phase du mélange est gardée) —
publiée, bornée, avec ses propres attendus, et mesurée par le même banc
contre le même résidu vrai. Elle n'est pas dans cette phase, et elle ne
s'écrira que si R1 la désigne.
