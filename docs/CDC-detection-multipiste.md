# Détection multipiste — cahier des charges

Chantier ouvert le 02/09/2026 sur un constat de l'utilisateur : « les
originaux contiennent bien plus que 4 pistes, or notre analyse n'en fait
jamais plus de 4 ». C'est exact, c'est un défaut de conception, et onze
versions de mesure ne l'avaient pas vu parce qu'aucun chiffre ne le
regardait. Depuis la même date, ce chantier et le DAW sont **les deux seuls
sujets de travail** ; les campagnes de mesure générales sont en pause
(directive utilisateur), seules courent les mesures de CE chantier.

## 0. La règle qui prime : la PARITÉ des pistes

L'objectif est fixé par l'utilisateur en toutes lettres (02/09/2026), et il
est SANS PLAFOND : **« si un original comporte 15 postes, la reconstruction
doit comporter 15 pistes également ; si l'original comporte 64 pistes, la
reconstruction doit en comporter 64 »**. Le critère n'est donc pas « plus de
quatre pistes » mais la parité, à toute échelle — le compte de pistes de la reconstruction vise le compte de parties
réelles de l'original. On ne sait pas toujours compter les parties réelles
(on n'a pas la session d'origine) ; ce qu'on sait faire, c'est ne jamais
FONDRE deux parties discernables en une, et publier ce qu'on n'a pas su
discerner.

Une reconstruction n'est pas jugée seulement à sa distance. Elle est jugée à
ce qu'elle rend **jouable et retravaillable** : un projet qui met quatre
instruments sur une piste ne se retravaille pas, quelle que soit sa distance.
La distance globale ne PEUT PAS voir ce défaut — quatre instruments fondus en
un sonnent « à peu près », et un spectre additionné ressemble à un spectre
additionné. Une version qui sépare mieux peut mesurer PLUS LOIN en rendant un
projet enfin utilisable ; quand ce cas se présente, **c'est la structure qui
gagne**, et l'écart de distance se publie à côté, sans être caché ni pardonné.

## 1. L'état des lieux, mesuré (02/09/2026, *Us and Them*)

La séparation par défaut (`htdemucs`) rend quatre stems, la chaîne une piste
par stem. Résultat :

| Stem | Part d'énergie (pli mono) | Polyphonie moy. (max) | Ambitus | Machine |
|---|---|---|---|---|
| `other` | **57,7 %** | **4,83** (11) | **66 demi-tons** | `vsm.tb303` |
| `vocals` | 22,7 % | — (sampler) | — | report |
| `drums` | 15,0 % | 0,24 (2) | 6 | `vsm.drums` |
| `bass` | 4,6 % | 0,50 (3) | 47 | `vsm.vocal` |

Plus de la moitié du morceau sur UNE piste, jouée par UN synthétiseur
monophonique de basse acide : le piano électrique, l'orgue, le saxophone et
les guitares passent tous par lui. (En somme stéréo, la part de `other` vaut
62,1 % ; le rapport publie le pli mono, qui est le signal que la chaîne
traite — les deux chiffres sont justes, ROADMAP-fusion § 5 quaterdecies
explique l'écart.)

## 2. Ce qui est déjà construit

1. **La visibilité** — aucun de ces chiffres n'existait. Désormais :
   `polyphonieMoyenne` (pondérée par le temps), `polyphonieMax`,
   `ambitusDemiTons` par stem et bloc `partage` (part d'énergie, mesurée sur
   les stems D'ORIGINE, jamais sur le rendu) dans `rapport.json` ; deux cris
   au journal — le stem fourre-tout (≥ 3 notes simultanées en moyenne ET
   ≥ 3 octaves, les deux ensemble : un accord de piano est dense sans être un
   fourre-tout, un solo est large sans l'être) et la piste qui porte plus de
   la moitié du morceau à elle seule.
2. **Plus de stems à la séparation** (H22) — `--modele htdemucs_6s` ajoute
   `guitar` et `piano`. La séparation est une étape À PART (un processus qui
   ne fait que demucs, écrit, meurt) : la faire dans le processus de la
   chaîne a fait tuer deux courses par l'OOM killer sur cette machine à
   15 Go. Troisième leçon de mémoire, payée le même jour : chaque
   travailleur de `--rendus-paralleles` tient EN PYTHON le rendu entier du
   morceau et ses intermédiaires de mesure (~0,5 Go), en plus de son
   vsm-render — à 12 travailleurs le python de course monte à 6,8 Go et
   l'OOM killer tranche (H23, 20:36). Sur cette machine, 8 est la borne
   mesurée ; le parallélisme est neutre pour le résultat, pas pour la
   mémoire. Partage six stems mesuré : vocals 28,0 · guitar 26,9 · drums 20,4 ·
   **other 9,1** · bass 8,7 · piano 6,9 — le fourre-tout tombe de 57,7 % à
   9,1 % avant tout découpage.
3. **Le découpage en voix** (H23) — `--voix-par-stem N` partage un stem
   fourre-tout par REGISTRES (k-moyennes 1-D sur la hauteur, pondérées par la
   durée). L'algorithme a été choisi PAR LA MESURE contre la séparation de
   voix par continuité : sur les 4 642 vraies notes de `other`, la continuité
   rend quatre voix qui balaient chacune 65-66 demi-tons (des parts de
   gâteau) ; les registres rendent des intervalles DISJOINTS d'ambitus
   28/9/9/16 (l'aigu 69-97, deux médiums 59-68 et 48-57, la basse-nappe
   31-47), polyphonie ≤ 1,7. Le garde-fou vit dans la fonction : ce qui n'est
   pas un fourre-tout ne se découpe JAMAIS — découper une mélodie qui saute
   ou un accompagnement d'accords fabriquerait de fausses pistes, pires que
   le mal soigné.

## 3. Les mesures en cours, hypothèses écrites avant les chiffres

Témoin commun : **H22a-v2** — stems `usandthem/` (htdemucs), 48 candidates,
3 tours, 6 finalistes. Rendu : **0,19103634445925913**, le chiffre de v14 au
bit près malgré un moteur recompilé à 53 machines — les machines ajoutées ne
déplacent rien, et la stabilité par empreintes est démontrée en course.

| Course | Variable unique | Attendu (écrit d'avance) | Résultat |
|---|---|---|---|
| H22b-v2 | stems `htdemucs_6s` (6) | 6 pistes ; distance neutre ou légèrement défavorable ; si elle se dégrade, la chaîne arbitre ressemblance contre jouabilité et le § 0 tranche | **0,17114 — −10,4 % ET six pistes.** L'attendu est contredit dans le bon sens : plus de pistes ET plus proche. Machines : bass→cs80, guitar→tb303, other→musicbox, piano→phasedist. Le verdict à six pistes n'atteint pas le point fixe en 3 tours (bass et piano oscillent), et le réglage au mélange rapporte encore −0,009. Restes : guitar (poly 3,4 · 74 demi-tons) et other (3,8 · 71) sont ENCORE des fourre-tout → H23 est complémentaire, pas concurrente |
| H23 | `--voix-par-stem 4` | pas de gain de distance ; succès = chaque piste sous le seuil du fourre-tout | **0,20846 (+9,1 %) et 7 pistes — le critère structurel est TENU** : les quatre voix passent sous le seuil (poly 0,64/1,68/1,36/1,14 ; ambitus 28/9/9/16), et chaque registre reçoit SA machine (divider, tb303, multisample ×2). La distance dépasse les 5 % de la règle écrite d'avance → le découpage reste une OPTION, il ne devient pas le défaut. Verdict au point fixe en 2 tours ; la chaîne signale 2× que le morceau serait meilleur sans la voix 1 (l'aigu jugé seul contre le stem entier) — conservée, couper est humain |
| `--voix-tete-choeurs` | tête et chœurs par le champ stéréo | *(aucun attendu écrit avant la course — faute dite ; la voix étant un report d'audio dont la somme est exacte, la distance ne pouvait bouger que par les autres pistes)* | **0,19112 (+0,05 %) et 5 pistes** contre H22a-v2 (0,19104) : neutre. Tête 74 % / chœurs 26 %, tête + chœurs = stem exactement. Le prix en distance est nul ; la qualité reste affaire d'oreille |
| `--batterie-par-piece` | une piste par pièce | pas de gain de distance ; les pièces ne se volent plus de voix | v1 0,25804 (+35,1 %, pièces non calées, § 7) ; v2 0,24481 (+28,2 %, pas d'alternative d'usine, § 8) ; **v3 0,191036 (+0,00 %, le témoin au millionième) et 5 pistes** : le découpage ne coûte rien |
| `--parite` | les trois découpages ensemble | ≈ H23 (+9 %), le prix des voix jugées seules | **0,21029 (+10,1 %) et 9 pistes** — même défaut de calage sur les pièces ET sur les voix (§ 7). Remesuré en campagne 2 (§ 8) |

## 4. Ce qui reste à faire (l'ordre de marche du chantier)

1. **Encaisser H22b et H23** : verdicts écrits ici et dans ROADMAP-fusion,
   projets ouverts DANS le DAW et regardés (capture), densités par piste
   vérifiées au rapport.
2. **Trancher le défaut — DÉCIDÉ (03/09/2026), les deux verdicts en main** :
   - **`htdemucs_6s` devient le modèle de séparation PAR DÉFAUT** : −10,4 %
     de distance ET deux pistes de plus, structure et ressemblance du même
     côté — il n'y a pas de compromis à arbitrer. Réserve dite : mesuré sur
     UN morceau ; la contre-épreuve sur un second original est la première
     mesure à faire quand la pause des campagnes sera levée (§ 4.7).
     Conséquence technique obligatoire : la séparation PAR DÉFAUT doit se
     faire en SOUS-PROCESSUS qui meurt (deux courses tuées par l'OOM avec
     demucs résident dans le processus de la chaîne — § 2.2).
   - ~~**`--voix-par-stem` reste une option, PAS le défaut**~~ — DÉPASSÉ le
     04/09/2026 : la règle écrite avant la mesure disait « si la distance se
     dégrade de plus de 5 %, le compromis se dit et se laisse à
     l'utilisateur » — mesuré +9,1 % par H23, mais ce prix était celui du
     calage voix par voix (§ 7) ; recalé, il vaut −0,1 % sur *Us and Them*
     et +3,1 % sur *Sky and Sand* (§ 8). **`--parite` est le défaut**, et
     `--sans-parite` le témoin.
3. **Le DAW montre ce que la chaîne sait — FAIT (D53, 08/09/2026).** La forme
   retenue est l'écran « Voir le rapport de reconstruction », et non une
   colonne de la liste de pistes : il porte le partage d'énergie stem par
   stem, les densités, le bloc batterie (machine, pièces, frappes, parité) et
   **la distance de chaque piste** — que l'écran taisait, alors que la pire
   était 26,7 % plus loin que la meilleure. La métrique et le `gate`
   l'accompagnent, parce qu'une distance sans ses conditions invite la
   comparaison que le projet interdit. Le `gate` ne s'écrit que lorsqu'il
   n'est pas à 1 : un « gate 1.00 » sur chaque ligne serait un meuble.
4. **La batterie éclatée par pièce — CÂBLÉE (03/09/2026)** :
   `--batterie-par-piece` rend une piste par pièce détectée, même machine et
   même patch pour toutes (le kit reste un instrument réglé une fois). Les
   pièces et les notes s'apparient par les INSTANTS de frappe — jamais en
   rejouant la logique de repli, qui écrit des avertissements et tient un
   état ; deux pièces rabattues sur la même voix restent ensemble sous un nom
   composé (« kick+tom »). Deux renoncements dits au journal : les boîtes
   suivantes ne sont plus remises en jeu au verdict, et le volume par pièce
   n'est pas calé sur le stem. 4 tests. Option, pas défaut : son effet sur la
   distance n'est PAS mesuré (campagnes en pause) — chaque pièce devient une
   instance séparée de la machine, les pièces ne se volent plus de voix, et
   ce changement de rendu attendra sa mesure.
> **CAMPAGNE P1 — L'EFFET DE `--batterie-par-piece` SUR LA DISTANCE, ATTENDUS
> ÉCRITS AVANT LA MESURE (10/09/2026, 10:30).**
>
> Le § 4.4 dit depuis le 03/09 que l'effet de cette option sur la distance n'est
> PAS mesuré. Vérifié le 10/09 : aucune course du dépôt ne fait témoin. Les huit
> `sky-*` existantes portent toutes `batterieParPiece` à `True` sauf `sky-hd`,
> qui diffère AUSSI par le nombre de tours de verdict (aucun contre trois) —
> deux variables, donc pas un A/B.
>
> **PROTOCOLE, UNE SEULE VARIABLE.** *Sky and Sand*, choisi parce que sa
> batterie porte **78 % de l'énergie du morceau** et compte **5 pièces pour
> 3 309 frappes** (§ 4.7) : c'est le morceau où l'option a le plus à changer.
> Les stems sont séparés UNE fois et repris par les deux courses (`--stems`),
> si bien que la séparation ne peut pas différer. Options identiques à
> `sky-parite` — métrique v2, budget de piste 120 sur 21 axes, 3 tours de
> verdict, 6 finalistes, parité — et un seul jeton change :
> `--batterie-par-piece` présent d'un côté, absent de l'autre. Même binaire de
> moteur pour les deux, même graine.
>
> **CE QUI EST CERTAIN PAR CONSTRUCTION, ET NE PROUVE DONC RIEN** : la course
> avec l'option rendra PLUS de pistes — une par pièce détectée au lieu d'une
> pour le kit. C'est ce que l'option fait ; le mesurer serait mesurer sa propre
> définition.
>
> **CE QUI EST EN JEU : LA DISTANCE, ET DEUX EFFETS TIRENT EN SENS CONTRAIRE.**
> (1) Une instance de machine par pièce supprime le vol de voix entre pièces —
> un charleston ne coupe plus la queue d'une caisse claire —, ce qui devrait
> RAPPROCHER. (2) Le volume par pièce n'est PAS calé sur le stem, renoncement
> dit au journal dès le câblage : cinq pistes calées chacune sur rien peuvent
> sommer faux, ce qui devrait ÉLOIGNER.
>
> **ATTENDU CHIFFRÉ, ÉCRIT MAINTENANT** : l'écart tient entre **−5 % et +10 %**.
> Au-delà de **±15 %**, l'attendu est réfuté et il faudra dire lequel des deux
> effets domine.
>
> **CE QUE CHAQUE ISSUE DÉCIDE.** Si l'option rapproche ou coûte moins de 5 % :
> elle peut devenir le défaut avec la parité, puisque la structure gagne et la
> ressemblance ne perd presque rien. Si elle coûte plus de 10 % : elle reste une
> option, et le § 4.4 portera son prix à côté de son gain — comme
> `--modele htdemucs_6s` porte le sien depuis le § 4.7. **Le prix se publie dans
> les deux cas** : une option dont on ignore le coût est une option qu'on
> recommande à l'aveugle.

5. **La voix — CÂBLÉE (03/09/2026)** : `--voix-tete-choeurs` sépare la voix
   de TÊTE des CHŒURS **par le champ stéréo** (extraction de centre par
   masque spectral, `analyzer/vsm_voix.py`) — le séparateur ne reconnaît pas
   des voix, il sépare le centre du large, convention de mixage presque
   universelle, et se nomme pour ça. Garanties : tête + chœurs = stem
   EXACTEMENT (le complément temporel — erreur max 2,8·10⁻¹⁷ mesurée sur le
   vrai stem) ; une voix mono ou sans largeur n'est PAS découpée, en le
   disant. Mesuré sur *Us and Them* (stem 6s) : part latérale 0,18, partage
   tête 74 % / chœurs 26 %, 22,7 s de calcul pour 470 s d'audio. 5 tests —
   dont un qui a coûté sa leçon : la resynthèse d'un spectre MASQUÉ explose
   au bord du signal là où l'identité est exacte (|r| = 2 132 sur 64
   échantillons), d'où le rembourrage d'une fenêtre de zéros. Option, pas
   défaut ; la QUALITÉ de la séparation (fuites de réverbération de la tête
   dans les chœurs, notamment) ne se juge qu'à l'OREILLE — les deux wav
   d'essai attendent une écoute. Distance mesurée le 03/09/2026 : +0,05 %, neutre (§ 3).
6. **Le DAW à l'échelle de la parité — REGARDÉ à 64 pistes (03/09/2026)** :
   un FLP d'essai à 64 canaux (16 familles × 4) importé et photographié.
   L'arrangement défile et reste lisible à 150 %, la console défile
   horizontalement (MASTER épinglé à droite, `MixerComponent` a son
   viewport), la liste de pistes défile, le rapport d'import à 64 lignes
   défile ; l'import et l'ouverture sont instantanés. VSM_VUE gagne le jeton
   `sans-rapport` pour photographier l'arrangement derrière l'écran de
   rapport. LA CHARGE AUDIO EST MESURÉE À SON TOUR (03/09/2026),
   sur un projet d'essai où 64 machines DIFFÉRENTES tiennent chacune trois
   notes par mesure — le cas défavorable, les voix se superposent :

   | Machines | Rendu hors ligne | DAW en LECTURE |
   |---|---|---|
   | 4 | 63× le temps réel | ~50 % d'un cœur |
   | 16 | 17× | ~67 % |
   | 64 | **5,2×** | **~127 %** (1,3 cœur sur 22) |

   Le DAW joue donc 64 machines avec une marge d'un facteur cinq, et la
   charge croît moins vite que le nombre de pistes (×16 de pistes pour ×2,6
   de CPU : le coût fixe domine à quatre pistes). Vérifié à l'écran, lecture
   en cours, les 64 vumètres bougeant. `VSM_VUE` gagne le jeton `jouer`
   pour photographier un projet EN TRAIN de jouer — sans lui, le compteur de
   charge ne dit rien.
7. **Le deuxième morceau — FAIT (03/09/2026), et il a corrigé une décision.**
   Deux seconds originaux, choisis pour s'opposer :

   | Original | 4 sources | 6 sources |
   |---|---|---|
   | *Sky and Sand* (électronique) | drums 77,2 · bass 11,3 · other 9,5 · vocals 2,0 | drums 78,0 · bass 13,8 · other 6,0 · vocals 2,0 · **piano 0,1 · guitar 0,0** |
   | *Clair de Lune* (PIANO SEUL) | other 100,0 | **piano 99,5** · guitar 0,4 · other 0,1 · reste 0,0 |

   **Ce que le deuxième morceau apprend, et qu'*Us and Them* ne pouvait pas
   dire** :

   - **le gain des six sources NE GÉNÉRALISE PAS.** Sur *Us and Them*, la
     guitare et le piano existent vraiment et valaient −10,4 %. Sur *Sky and
     Sand*, ils sont VIDES (0,0 et 0,1 %) : le modèle à six sources n'y
     ajoute que deux pistes de silence. Le défaut reste `htdemucs_6s` — il ne
     nuit pas, il nomme mieux (`piano 99,5` vaut mieux qu'`other 100,0` sur un
     piano seul) — mais la réserve du § 4.2 est LEVÉE dans un sens précis :
     six sources ne rapprochent que les morceaux qui ont réellement ces
     parties, et cela devait être dit ;
   - **un original à UNE partie donnait SIX pistes**, et c'est l'exact
     contraire de l'objectif de parité. D'où `--seuil-stem` (défaut 0,5 % de
     l'énergie) : un stem sous le seuil n'est pas reconstruit, et le refus est
     DIT avec son chiffre. Ce n'est pas « couper une piste » — la règle du
     dépôt protège ce qu'on entend ; ici la chaîne refuse de FABRIQUER, le
     stem reste sur le disque, `--seuil-stem 0` le reconstruit. La plus petite
     vraie partie d'*Us and Them* (piano, 6,9 %) est douze fois au-dessus ;
   - **le fourre-tout de *Sky and Sand* est la BATTERIE** (78 %), pas `other`.
     Le seuil de fourre-tout mesure polyphonie et ambitus, qui n'ont pas de
     sens pour un kit — il ne le voyait donc pas. **Comblé le même jour** :
     la batterie n'a pas besoin d'être devinée, ses pièces sont CLASSÉES, et
     l'on sait exactement combien de parties elle porte. Mesuré :
     *Sky and Sand* = **5 pièces, 3 309 frappes** (tom 1455, percussion 601,
     hihat 512, kick 434, kick2 307) ; *Us and Them* = 4 pièces, 2 227
     frappes. La chaîne le dit désormais quand la batterie porte au moins un
     quart du morceau sur une piste, et l'écran du rapport porte le bloc
     batterie — machine, pièces, frappes, concessions de la machine, et la
     ligne de parité. Reste non mesuré : l'effet de `--batterie-par-piece`
     sur la distance.

   **La DISTANCE sur ce second morceau — MESURÉE (03/09/2026), et elle borne
   le défaut.** Paire à une variable, le modèle de séparation, sur *Sky and
   Sand* (6 rendus de front, budget 120 × 21, 3 tours, 6 finalistes, même
   binaire) :

   | Course | Stems | Pistes | Distance | Verdict |
   |---|---|---|---|---|
   | sky-t4 (témoin) | 4 sources | 4 | **0,22466** | bass→vector, other→stochastic, batterie tr909 ; 3 tours |
   | sky-t6 | 6 sources | 4 — guitar (0,0 %) et piano (0,1 %) REFUSÉS par `--seuil-stem`, avec leurs chiffres | 0,23347 (**+3,9 %**) | bass→vector, other→musicbox, batterie tr909 ; 2 tours |

   Ce que cela dit : sur un morceau SANS guitare ni piano, les six sources
   ne donnent pas une piste de plus (le seuil fait son office, les deux stems
   vides sont refusés en le disant) et coûtent +3,9 % — non pas par les deux
   pistes en plus, il n'y en a pas, mais parce que le modèle à six sources
   découpe AUTREMENT la basse, l'accompagnement et la batterie (la basse y
   compte 1 704 notes contre 1 128 : le partage entre bass et other n'est pas
   le même). Le § 4.2 disait « il ne nuit pas » : c'était faux de 3,9 % sur
   ce morceau, et c'est écrit. **Le défaut RESTE `htdemucs_6s`** : −10,4 %
   sur un morceau qui a ces parties, +3,9 % sur un morceau qui ne les a pas ;
   la parité gagne dans un cas et ne perd rien dans l'autre, et
   `--modele htdemucs` reste à portée de main pour qui sait que son morceau
   n'a ni guitare ni piano. Choisir le modèle d'après le morceau lui-même
   (séparer aux deux modèles et garder celui dont les stems refusés sont
   nombreux) coûterait une séparation de plus ; ce n'est pas mesuré, et ce
   n'est pas fait.

   Vu aussi, et déjà connu du § 5 nonies de ROADMAP-fusion : sur sky-t4, le
   verdict du mélange trouve le morceau MEILLEUR sans la basse (0,2263 contre
   0,2319) — la basse de *Sky and Sand* reste le second front, quelle que soit
   la séparation.

## 6. `--parite` : le raccourci, et une épreuve de bout en bout

Trois découpages mènent à la parité — voix par registres, batterie par pièce,
tête et chœurs — et il faut **les trois**. *(Un quatrième les a rejoints le
03/09/2026, les registres lus dans les vides : voir le § 6 bis.)* Personne ne devrait avoir à les
retenir : `--parite` les allume, en disant lesquels et ce qu'ils coûtent. Une
option écrite à la main l'emporte, pour qu'un A/B sur un seul découpage reste
possible.

**L'ÉPREUVE DE BOUT EN BOUT, sur un morceau dont on CONNAÎT les parties.**
Toute la mesure de ce chantier porte sur des originaux dont personne ne sait
la vérité : on compare des pistes à un nombre de parties qu'on suppose. Un
morceau court est donc fabriqué avec sa vérité écrite (trois parties
mélodiques en registres disjoints, une batterie à trois pièces, une voix
centrée doublée large), et ses stems fournis directement — la variable est
la CHAÎNE, pas demucs. La chaîne complète y tourne en **25 secondes** :

| Course | Pistes | Distance |
|---|---|---|
| témoin | 4 — `bass`, `other`, `Batterie`, `Voix` | 0,3169 |
| `--parite` | **6** — `bass`, `other`, `Batterie · hihat`, `Batterie · kick+kick2`, `Voix · tête`, `Voix · chœurs` | 0,3163 |

La parité ne coûte rien ici (l'écart est du bruit), et deux mécanismes sur
trois se déclenchent. Le troisième — le découpage en voix — **ne se déclenche
pas, et c'est la garde qui fonctionne** : la transcription d'un morceau de
synthèse rend 32 notes de polyphonie moyenne 2,37, sous le seuil de 3. Le
découpage par registres reste prouvé sur des données réelles (H23, quatre
voix sur le vrai `other` d'*Us and Them*).

**Ce que cette épreuve a trouvé, et qu'aucune mesure n'aurait vu** : le format
MIDI écrit ses noms de piste en Latin-1, et « Voix · chœurs » contient un
« œ ». La chaîne TOMBAIT à l'écriture du projet — après tout le calcul. Le
défaut dormait depuis toujours ; il fallait un nom composé par la chaîne
elle-même pour le réveiller. Les noms sont désormais translittérés pour le
MIDI (le nom complet survit dans `project.json`, qui est de l'UTF-8), et
quatre tests le gardent.

## 6 bis. L'épreuve rejouable, et le quatrième découpage (03/09/2026)

**L'épreuve du § 6 avait été perdue** : son morceau et son script vivaient
dans un dossier temporaire, effacé par un redémarrage. Elle est maintenant un
outil du dépôt, `analyse/epreuve_parite.py` : il FABRIQUE le morceau (32 s à
120 bpm, Am-F-C-G) avec sa vérité écrite dans `verite.json`, fournit ses
stems, fait tourner la chaîne à petit budget (trois candidates, un tour) et
compte les pistes contre les parties, stem par stem. Neuf parties : une
basse (MIDI 29-36) ; dans `other`, trois couches en registres DISJOINTS —
dyades graves 36-50, arpèges médiums 60-72, mélodie aiguë 84-96 ; une
batterie à trois pièces (kick, caisse claire, charleston) ; une voix de tête
au centre doublée de chœurs larges. Chaque course dure de 80 à 250 s.

**Ce que l'épreuve a trouvé d'abord : un plantage.** Ses stems sont écrits
en float32, et le lecteur stéréo de la séparation tête/chœurs passait par le
module `wave` de Python, qui refuse ce format : la chaîne tombait sur le stem
vocal, après tout le travail sur les autres. Les stems de demucs sont en
entiers, personne n'avait rencontré le cas. Le lecteur lit désormais
l'en-tête lui-même (comme `lire_wav`), un test le garde.

**Puis le défaut nommé au § 5 — et il n'était pas où le § 5 le disait.** Le
§ 5 accusait la transcription (« 32 notes de polyphonie 2,37 »). Sur ce
morceau-ci, Basic Pitch rend 285 notes de polyphonie 3,97 sur 69 demi-tons —
un fourre-tout au sens du seuil, avec trois registres nettement séparés
(36-46, 56-79, 84-96). Le découpage se déclenche, et rend **quatre** voix
pour trois parties : `separer_en_voix` IMPOSE son maximum (quatre avec
`--parite`) et coupe en deux le registre le plus maigre pour faire le compte.
Le nombre de voix n'était pas lu, il était décidé d'avance.

**Le quatrième découpage : `--voix-par-vides`.** Avant le partage en N voix,
la densité de durée par hauteur, lissée sur deux demi-tons, est coupée à ses
**creux** — là où elle tombe sous un quart du plus petit des deux sommets
voisins. Un arpège serré (tierces, quartes) ne creuse pas : ses notes se
recouvrent après lissage — le premier jet coupait à chaque hauteur muette et
rendait douze registres pour trois ; dix demi-tons vides creusent jusqu'à
zéro. Un registre sous 5 % de la durée totale (une erreur d'octave de la
transcription, une fioriture) rejoint son voisin. Même garde-fou que les
voix : ce qui n'est pas un fourre-tout ne se découpe pas. Un registre encore
fourre-tout après ce découpage est partagé par `--voix-par-stem`. Quatre
tests (`analyse/tests/test_registres_par_vides.py`).

**Il rejoint `--parite`, et voici pourquoi c'est sans danger pour la campagne
2 qui tourne** : essayé sur les huit pistes mélodiques réelles des courses
H22a, H22b et sky-t6 (transcriptions de 481 à 4 642 notes), il ne coupe
RIEN — une transcription réelle est dense, sans creux. Il est donc inerte
sur les vrais morceaux mesurés jusqu'ici, et décisif sur le seul cas où la
vérité est connue. Il est dans la provenance (`voixParVides`).

**Mesuré, trois courses sur le même morceau** (mêmes candidates, même budget,
même moteur ; la seule variable est le découpage) :

| Course | other | Batterie | Voix | Pistes | Distance |
|---|---|---|---|---|---|
| témoin (aucun découpage) | 1 | 1 | 1 | 4 / 9 | 0,2197 |
| les trois découpages d'avant | **4** (77-96, 56-75, 43-46, 27-41) | 3 | 2 | 10 / 9 | 0,1793 |
| `--parite` (les quatre) | **3** (84-96, 56-79, 27-46 — nommés depuis en notes : C6-C7, G#3-G5, D#1-A#2) | 3 (hihat, kick+kick2, snare) | 2 (tête 77 %, chœurs 23 %) | **9 / 9** | **0,1776** |

La parité est atteinte, et elle ne coûte rien : la distance BAISSE de 19 %
par rapport au témoin (chaque partie reçoit sa machine et son volume). La
batterie rend ses trois pièces — le kick porte deux gabarits (le premier coup
d'un morceau n'a pas de queue), rabattus ensemble sous `kick+kick2`, ce qui
est le comportement voulu ; la caisse claire de synthèse est bien classée
caisse (la version perdue de l'épreuve l'avait vue en `kick2` : sa caisse
était trop sombre). La chaîne dit aussi, trois fois, que le morceau serait
meilleur sans telle piste (la basse, deux registres d'`other`) : chaque
registre est jugé seul contre le stem entier, et l'aveu reste ce qu'il est —
une information, pas une coupe.

**L'excès inverse, mesuré aussi (variante `--variante chorale`).** `other`
y est UN SEUL instrument — même timbre — qui tient quatre voix serrées sur
trois octaves : un piano d'accompagnement, 7 parties en tout. Au sens du
seuil c'est un fourre-tout (polyphonie 3,8, ambitus 47) ; pour l'oreille
c'est une partie. Résultat avec `--parite` : **8 pistes pour 7 parties** —
la voix de basse de la chorale (G#1-D#2) est séparée des trois autres
(C3-G5) par un creux, et le découpage par les vides en fait deux
instruments. Rien dans les notes ne peut dire le contraire, et le timbre lu
dans l'audio ne le peut pas non plus — mesuré et réfuté le jour même,
ROADMAP-fusion H25. **C'est une limite connue et chiffrée de la parité** :
un instrument dont les registres se séparent par un vide compte pour deux.

**Et les groupes arrivent dans le DAW (03/09/2026).** Le registre
`pistes_groupees` qui cale ensemble les pièces et les voix sert aussi le
projet écrit : une piste de groupe par stem partagé (« Batterie »,
« other »), les membres routés vers elle (`kind: group`, `output`), à 0 dB
sans effet, ajoutée en fin de liste pour ne décaler aucun index. Vérifié :
la distance de l'épreuve ne bouge pas (0,1828 avant comme après — le bus au
volume 1 est neutre pour le rendu), et le mixeur du DAW montre les deux bus.
Le compte de parité ignore les bus : ce sont des faders, pas des parties.
4 tests.

Ce que l'épreuve ne prouve pas : que les creux existent dans un vrai morceau.
Ils n'existent dans aucun des trois mesurés. Le jour où une transcription
réelle en montre, le journal le dira (« DÉCOUPÉ en N registres par les
VIDES »), et ce sera à mesurer.

## 7. Le calage des niveaux, cassé par le découpage — corrigé (03/09/2026)

**LA CAMPAGNE A TROUVÉ CE QU'ELLE DEVAIT TROUVER.** Mesuré sur *Us and Them*,
`--batterie-par-piece` coûtait **+35,1 %** de distance (0,2580 contre 0,1910)
— pour un découpage qui ne change pas une note. La cause était au journal :
`Batterie · kick2+kick : volume NON CALÉ (pas de stem de référence)`.

Le calage compare chaque piste rendue seule au stem qui porte son nom. Le
découpage casse cette hypothèse de deux façons :

- **les pièces d'une batterie éclatée n'ont aucun stem à leur nom** — elles
  restaient au volume d'amorçage 0,90 quand le kit entier, lui, était calé à
  0,82 : la batterie sortait environ deux fois trop fort ;
- **les voix d'un stem découpé ont TOUTES le stem entier pour référence** —
  chacune recevait donc le gain qu'il faudrait pour le remplacer à elle seule.
  Vu sur la course de parité : quatre voix montées à 1,44, 1,16, 1,69 et 0,73
  contre un même `rms stem 0,0700`. Leur somme sortait plusieurs fois trop
  fort, et personne ne l'avait dit.

**J'avais écrit le premier point comme un « renoncement » en câblant l'option,
sans le mesurer.** Il coûtait un tiers de la distance. L'avoir dit ne le
rendait pas acceptable : une concession non mesurée est une dette dont on
ignore le montant.

**Le correctif est de principe.** Les pistes d'un groupe PARTITIONNENT leur
stem : c'est leur **somme** qui doit l'égaler. On rend donc chacune seule, on
additionne, et l'on applique à toutes le **même** facteur — l'équilibre
interne que la détection ou le découpage ont trouvé n'est pas touché, seul le
poids du groupe dans le mélange l'est. Le chantier tient un registre
`pistes_groupees` (nom de piste → groupe), rempli par les deux découpages.
Quatre tests, dont celui qui garde l'équilibre interne et celui qui vérifie
que le chemin d'origine — une piste ordinaire, seule contre son stem — ne
bouge pas d'un iota.

**À remesurer** : `--batterie-par-piece`, `--voix-par-stem` et `--parite` sur
*Us and Them* portent tous ce défaut dans les chiffres du § 3. Les courses
sans découpage (le témoin, les six sources, la tête et les chœurs) n'y
touchent pas — le correctif est confiné au chemin des groupes — et restent
valables telles quelles.

## 8. Campagne 2 : la remesure après le calage des groupes — attendus écrits AVANT (03/09/2026)

Quatre courses en série, une variable chacune, mêmes réglages que la campagne
de la nuit (budget 120 × 21, 3 tours, 6 finalistes, même binaire du
02/09 15:23). La première campagne s'est arrêtée sur `sky-parite` par un
redémarrage de la machine ; les deux courses de *Us and Them* qui portaient le
défaut du § 7 sont rejouées dans de nouveaux dossiers, les anciens restent
pour la comparaison.

| Course | Témoin | Variable | Attendu, écrit d'avance |
|---|---|---|---|
| usandthem-batterie-v2 | H22a-v2 (0,19104) | `--batterie-par-piece` | le surcoût de +35,1 % DISPARAÎT : le découpage ne change pas une note, et les pièces sont désormais calées ensemble sur leur stem. J'attends la distance dans ±3 % du témoin. Si elle reste au-dessus de +5 %, le calage n'était pas la seule cause (les instances séparées ne se volent plus de voix, et c'est un changement de rendu) |
| usandthem-parite-v2 | H22a-v2 (0,19104) | `--parite` | ≈ H23 seule (0,2085, +9,1 %) : le prix des voix jugées seules contre le stem entier reste, celui du calage part. J'attends entre +7 et +11 %, et 9 pistes |
| sky-parite | sky-t6 (0,23347) | `--parite` | la batterie de *Sky and Sand* porte 78 % du morceau et 5 pièces : c'est le découpage qui compte ici. J'attends 5 pièces (ou moins, si des pièces se rabattent sur une même voix), la voix découpée si le stem a de la largeur, `other` NON découpé (il faut qu'il passe le seuil du fourre-tout, et la course t6 ne le criait pas). Distance : neutre à +5 % — les pièces sont calées en groupe, et rien d'autre ne change de note |
| usandthem-v15 | H22a-v2 (0,19104) | `--machines-au-melange 9` (avec `--sans-parite` explicite depuis que la parité est le défaut, 04/09) | ≈ témoin (hypothèse écrite le 02/09, ROADMAP-fusion § 5 quaterdecies). Relancée le 04/09 après l'extinction du poste |

**usandthem-batterie-v2 — MESURÉE (03/09/2026, 11:52) : 0,24481, +28,2 %
contre le témoin, −5,1 % seulement contre la v1.** L'attendu (±3 %) est
contredit : le calage par groupe a bien agi (les deux pièces calées ensemble
à 0,82, comme le kit entier du témoin), mais il n'expliquait qu'un cinquième
du surcoût. Le journal et le projet du témoin ont livré le reste :

- **le témoin garde le patch d'USINE de la batterie.** Au tour 1 du verdict du
  mélange, l'alternative « avant réglage » (le patch d'usine, contre celui
  réglé sur la piste) a été retenue — le preset final de H22a est identique
  aux défauts de `vsm.drums`, et son volume recalé à 0,51. Les pièces
  éclatées n'avaient JAMAIS cette alternative : le § 4.4 ne l'avait pas
  inscrite dans ses renoncements, parce que personne ne savait que le
  verdict la retenait. Elles gardaient donc le patch réglé sur la piste, à
  0,82 — le kit que le mélange refuse, plus fort d'un tiers ;
- **le verdict et le réglage au mélange recalaient la piste SEULE.** Après
  chaque essai de patch, `match_track_levels([piste])` — sans le registre
  des groupes : une pièce ou une voix y recevait le gain qu'il faudrait
  pour remplacer tout le stem. Le défaut du § 7 revenait par ces deux
  portes, et la course usandthem-parite-v2 (partie à 11:52, code d'avant)
  le porte dans ses chiffres.

Corrigé le jour même : chaque pièce reçoit le patch d'avant réglage comme
alternative (même patch pour toutes), et le recalage après un changement de
patch suit le groupe — `recaler_avec_son_groupe`, 2 tests. Sur l'épreuve à
vérité connue, les pièces reçoivent bien l'alternative (écartée : le patch
réglé y vaut mieux) ; la distance passe de 0,1776 à 0,1828 (+2,9 %) — les
décisions du verdict changent quand les voix sont recalées ensemble, et ce
morceau de synthèse n'est pas le juge. Le juge, écrit d'avance :

| Course | Témoin | Variable | Attendu, écrit d'avance |
|---|---|---|---|
| usandthem-batterie-v3 | H22a-v2 (0,19104) | `--batterie-par-piece`, code corrigé | le verdict retient le patch d'usine pour les pièces comme pour le kit, recalées ensemble : j'attends la distance dans ±5 % du témoin. Si elle reste au-dessus de +10 %, la cause restante est le rendu en instances séparées (les pièces ne se volent plus de voix) |
| usandthem-parite-v3 | H22a-v2 (0,19104) | `--parite`, code corrigé | les voix recalées ensemble au verdict et au réglage : j'attends mieux que parite-v2 et ≈ H23 (+9 %), soit entre +5 et +11 % |

**usandthem-batterie-v3 — MESURÉE (18:09) : 0,191036, le témoin AU MILLIONIÈME
(+0,00 %).** L'attendu (±5 %) est tenu au-delà de ce qu'il osait : le
découpage de la batterie par pièce ne coûte RIEN. Le journal le montre pas à
pas : les deux pièces calées ensemble à 0,82 comme le kit du témoin ; au tour
1 du verdict, chacune reçoit le patch d'usine (l'alternative qui manquait) et
son volume recalé à 0,51 — les valeurs mêmes du kit dans le témoin ; le
réglage au mélange fait ensuite le même chemin (bass 0,2262 → 0,2108, other →
0,2086). Deux instances de la même machine jouant chacune ses pièces au même
patch et au même volume rendent le même son qu'une seule : le surcoût de
+35 % (v1) puis +28 % (v2) était ENTIÈREMENT fait du calage et de
l'alternative absente, jamais du découpage. Le projet porte en plus son bus
« Batterie » (§ 6 bis). Le découpage par pièce peut donc rester dans
`--parite` sans réserve de distance.

**usandthem-parite-v3 — MESURÉE (21:11) : 0,19084, soit −0,10 % du témoin,
et NEUF pistes.** L'attendu (+5 à +11 %) est contredit dans le bon sens, et
de loin : le prix de la parité, mesuré à +9,1 % par H23 et à +28 % par
parite-v1, était ENTIÈREMENT fait du calage voix par voix contre le stem
entier. Recalées ensemble sur leur stem (§ 7), les quatre voix d'`other`,
les deux pièces de la batterie et les deux voix chantées rendent le même
morceau que quatre pistes, à 0,0002 près — et le projet se retravaille.
Machines retenues : bass → vocal ; other · voix 1 → divider, voix 2 →
tb303, voix 3 et 4 → multisample ; batterie kick2+kick et hihat → drums au
patch d'usine ; tête et chœurs reportées. Deux bus de groupe (« other »,
« Batterie »). Durée : 3 h 01 (10 885 s) à 8 rendus de front, contre 5 h 24
pour parite-v2 avant sa mort au réglage. Le verdict du mélange dit encore
« meilleur sans la basse » (0,2165 contre 0,2219), et elle est conservée.

Ce que cela rouvre : le § 4.2 laissait `--voix-par-stem` en option parce
que la parité coûtait +9,1 %. Elle ne coûte plus rien sur ce morceau.
**Faire de `--parite` le défaut est donc la question suivante — et elle
attend sky-parite (campagne 4, en cours depuis 21:11)**, parce que la règle
du § 5 exige un deuxième morceau avant de changer un défaut, et que *Sky
and Sand* met la parité à une autre épreuve : c'est la batterie (78 %) qui
s'y découpe, pas `other`. Attendu déjà écrit (§ 8, tableau de la campagne
2) : neutre à +5 %.

**sky-parite — MESURÉE (03/09 22:48, lue le 04/09) : 0,24073, soit +3,1 %
du témoin sky-t6 (0,23347), et SEPT pistes au lieu de quatre.** Dans
l'attendu (neutre à +5 %), et sur chaque point : la batterie de cinq pièces
s'est rabattue sur TROIS pistes (tom+kick+kick2 à 2 196 frappes, percussion
à 601, hihat à 512 — le tr808 n'a ni tom ni percussion, le spectre les
donne pour kick et snare), la voix n'est PAS découpée (largeur stéréo sous
le seuil : « une piste chœurs quasi vide passerait pour une partie », dit
au journal), `other` n'est PAS découpé (il ne passe pas le seuil du
fourre-tout). Le verdict du mélange garde tout ; le réglage au mélange
ramène 0,2523 à 0,2297, ce qui fait tout l'écart entre le prix au verdict
(+10 %) et le prix final (+3,1 %). Une piste est mesurée nuisible et
conservée : le morceau est meilleur SANS le hihat (0,2420 contre 0,2523) —
couper reste une décision humaine, la chaîne l'écrivait deux fois au
journal — une fois par tour du verdict, mêmes chiffres ; corrigé le
04/09 : ce qui n'a pas bougé depuis le tour précédent n'est pas redit, le
chiffre reste mesuré et publié dans le rapport. Durée : 1 h 37 (5 817 s) à 6 rendus de
front. Deux verrues dites au journal, différées (moteur) : le rendu final
avertit « Piste 6 (Batterie) : aucun instrument, elle restera silencieuse »
pour le BUS de groupe, qui n'a pas à avoir d'instrument.

**DÉCISION (04/09/2026) : `--parite` devient le DÉFAUT de `reconstruire.py`,
et `--sans-parite` rend la chaîne d'avant, pour les témoins.** La règle du
§ 5 (un deuxième morceau avant de changer un défaut) est satisfaite : deux
morceaux, deux structures différentes (`other` à 58 % découpé en quatre
voix sur *Us and Them* ; la batterie à 78 % découpée en trois sur *Sky and
Sand*), et le prix mesuré est −0,1 % et +3,1 % — pour neuf et sept pistes
au lieu de quatre. L'objectif du § 0 est la parité, pas la distance ; à ce
prix, la faire demander par une option revenait à livrer par défaut un
projet que personne ne peut retravailler. Ce que cela change pour les
mesures : tout témoin antérieur (H22a-v2, sky-t6 et leurs suites) a couru
sans parité, et une course qui doit leur être comparable s'écrit désormais
avec `--sans-parite` explicite — c'est le cas de usandthem-v15 (ci-dessous).
La provenance dit `parite` dans les deux cas. Le test `test_parite.py` fixe
le défaut, le témoin, et que le journal nomme `--sans-parite`.

**usandthem-parite-v2 — PERDUE (17:16), et la leçon vaut plus que la
course.** Après 5 h 24 et les trois tours du verdict, la chaîne est morte au
réglage du mélange : `vsm_mix_refine` était importé À LA DEMANDE à cet
instant, donc lu sur le disque dans sa version réécrite l'après-midi, qui
demandait à `vsm_levels` — chargé en mémoire dans sa version du matin — une
fonction qu'il n'avait pas. Rien n'a été écrit. L'information n'est pas
perdue (parite-v2 aurait porté le défaut du recalage que parite-v3 corrige),
le temps de machine l'est. Désormais `charger_tous_les_modules()` importe
toute la chaîne au départ : une course est une photographie du code à son
départ. La campagne 2 s'est arrêtée là ; sky-parite et v15 sont remises en
file derrière la campagne 3 (campagne 4).

## 9. Campagne 5 : la fusion des sept machines, et son prix — attendu écrit AVANT (03/09/2026)

La campagne 4 (sky-parite, v15) finit dans la nuit. Un script
(`campagne-parite-5.sh`, dans le dossier de travail) attend sa fin, puis
FUSIONNE la branche `machine-clavecin` dans `master` (sept machines, le
correctif du cône, la translittération), reconstruit tout, rejoue les six
suites, et NE POUSSE et NE LANCE la course suivante QUE si tout est vert ;
sinon il s'arrête et le journal le dit. La course :

| Course | Témoin | Variable unique | Attendu, écrit d'avance |
|---|---|---|---|
| usandthem-parite-parc60 | usandthem-parite-v3 (0,19084, 9 pistes) | le MOTEUR : 60 machines au lieu de 53, `vsm.cone` qui s'éteint enfin, mêmes stems, mêmes options (`--parite`, 120 × 21, 3 tours, 6 finalistes, 8 rendus) | la mémoire `elargir-le-vivier-de-machines` a mesuré une fois +15,9 % pour six familles (v12) ; depuis, le verdict au mélange arbitre entre finalistes et le parc n'est plus jugé qu'au stem. J'attends entre −3 % et +5 %, neuf pistes, et au moins UNE des sept nouvelles machines parmi les six finalistes d'une voix d'`other` (le clavinet ou le vibraphone sur *Us and Them* ne seraient pas absurdes). Si le prix dépasse +5 %, il se publie et ne décide de rien : une machine s'ajoute pour la couverture ET pour le jeu (CDC machines § 7), et la distance ne mesure que la première |

Le correctif du cône change son empreinte (pic −3,4 %) : toute course
lancée après la fusion porte un moteur différent de celui des campagnes 1
à 4, et la provenance le dit (`moteur.compile`, `moteur.machines` = 60).

### Reprise du 04/09/2026 : le poste s'est éteint à 22:50

Le poste a été éteint le 03/09 à 22:50 (journal système), deux minutes après
le départ de usandthem-v15 (22:48) : sky-parite était finie et écrite, v15
n'avait fait que l'arbitrage de la basse, et la campagne 5 n'a jamais
démarré (son journal est vide). Rien n'est perdu que deux minutes. La file
est relancée le 04/09 à 04:30 par `campagne-parite-4-reprise.sh`, qui
rejoue v15 sur l'ANCIEN moteur (53 machines, celui de son témoin H22a-v2),
avec `--sans-parite` EXPLICITE puisque la parité est devenue le défaut
entre-temps — même conditions que le témoin, une seule variable
(`--machines-au-melange 9`) — puis enchaîne `campagne-parite-5.sh` tel
quel (fusion, tout reconstruire, six suites, pousser, parc60). L'essai à
blanc de la fusion (`git merge-tree`) ne montre aucun conflit.

**usandthem-parite-parc60 — MESURÉE (10:08) : 0,19000, soit −0,4 % du
témoin parite-v3 (0,19084), NEUF pistes, moteur à 60 machines (provenance
`moteur.compile` 06:50:11, `moteur.machines` 60, commit 44fa8c4).** L'attendu
(−3 à +5 %, neuf pistes, au moins une des sept nouvelles machines parmi les
six finalistes d'une voix d'`other`) est tenu, et au-delà : **le clavecin
(`vsm.harpsichord`) est RETENU sur la voix 1 d'`other`** (D 0,4173 contre
0,4754 au diviseur de parite-v3), après avoir été finaliste sur les voix 1,
3 et 4 (à 11,5 %, 16,2 % et 31,8 % du premier à l'arbitrage) ; la vielle
est finaliste sur les voix 1 et 2, le clavinet sur les voix 1 et 3, la
cornemuse sur la basse (à 86,6 %). Les autres pistes ne changent pas de
machine (basse → vocal, voix 2 → tb303, voix 3 et 4 → multisample,
batterie → drums). Le verdict du mélange dit toujours « meilleur sans la
basse » (0,2202 contre 0,2174), et elle est conservée. Durée : 3 h 18
(11 886 s) contre 3 h 01 pour parite-v3 — **+9 % pour sept machines de
plus**, le prix du parc élargi en temps, pas en distance. Ce que cela dit :
le § 7 du CDC machines avait raison de séparer couverture et jeu — une
machine de plus ne coûte rien à la distance quand le verdict au mélange
arbitre, et peut la gagner ; le −0,4 % est dans le bruit de la mesure,
c'est le clavecin retenu qui est le fait. La campagne 5 clôt les campagnes
de la parité.

**usandthem-v15 — MESURÉE (06:49, lue à 10:15) : 0,18160, soit −5,0 % du
témoin H22a-v2 (0,19104), quatre pistes (sans parité, comme son témoin).**
L'hypothèse « ≈ témoin » (ROADMAP-fusion § 5 quaterdecies) est CONTREDITE
dans le bon sens : avec NEUF finalistes au lieu de six, la basse change de
machine — `vsm.sitar`, SEPTIÈME à l'arbitrage de piste (à 130,1 % du
premier), gagne au verdict du mélange, qui a pris un troisième tour (bass
seule au tour 2). Le sitar n'aurait jamais été entendu à six finalistes.
Durée 4 294 s (1 h 11). Ce que cela dit : l'arbitrage au stem se trompe
sur la basse d'*Us and Them* (elle sonne ailleurs, mêlée, que seule), et
le nombre de finalistes est le budget de cette seconde chance. La règle du
§ 5 s'applique : un second morceau avant de changer le défaut — c'est la
campagne 6.

## 10. Campagne 6 : le parc à 63 sur *Sky and Sand*, puis neuf finalistes — attendu écrit AVANT (04/09/2026, 10:20)

Deux courses, l'une après l'autre, sur le moteur à 63 machines (mandoline,
kalimba et séquençage d'ondes fusionnés à 10:20, commit de la fusion dans
la provenance). La première est le TÉMOIN de la seconde, et elle mesure
au passage le prix du parc sur le second morceau ; la seconde répond à
v15 sur *Sky and Sand*.

| Course | Témoin | Variable unique | Attendu, écrit d'avance |
|---|---|---|---|
| sky-parite-parc63 | sky-parite (0,24073, 7 pistes, moteur à 53 machines du 02/09) | le MOTEUR : 63 machines, le cône qui s'éteint, mêmes stems (sky-6s), mêmes options (parité, 120 × 21, 3 tours, 6 finalistes, 6 rendus) | comme parc60 sur *Us and Them* : entre −3 % et +5 %, sept pistes ; au moins une des dix nouvelles machines parmi les six finalistes de `bass` ou d'`other` — sur une piste électronique, le séquençage d'ondes ou le clavinet ne seraient pas absurdes sur `other` (musicbox à 0,2965 est battable) |
| sky-parite-m9 | sky-parite-parc63 | `--machines-au-melange 9` | v15 a donné −5,0 % sur *Us and Them* par un septième finaliste qui gagne au mélange. Sur *Sky and Sand*, `bass` (vector, 0,3314 au stem) et `other` (0,2965) ont chacun cinq machines suivantes écartées de peu (0,2334 à 0,2513 contre 0,2284 au verdict) : j'attends entre −6 % et +1 %, et qu'au moins UNE piste change de machine par un finaliste de rang 7 à 9. **Décision écrite d'avance** : si l'écart est ≤ −2 % (deux morceaux dans le même sens), `--machines-au-melange 9` devient le défaut ; entre −2 % et +1 %, il reste une option et le chiffre se publie ; au-delà de +1 %, l'hypothèse est réfutée sur ce morceau et v15 reste un fait d'*Us and Them* |

Durées attendues : 1 h 40 (sky-parite faisait 1 h 37 à 6 rendus) puis
2 h 10 (le troisième tour de verdict et trois finalistes de plus). Le
script `campagne-parite-6.sh` enchaîne les deux et s'arrête à la première
qui échoue.

**sky-parite-parc63 — MESURÉE (12:05) : 0,240683, IDENTIQUE au témoin
sky-parite (0,240683) à la neuvième décimale, sept pistes, moteur à 63
machines (provenance `moteur.compile` 10:18:16, commit 55e881b).** Mêmes
machines retenues (bass → vector, other → musicbox, batterie → tr808 par
pièce) : le parc élargi et le correctif du cône ne changent pas un
échantillon des machines que ce morceau choisit. L'attendu (−3 à +5 %, une
nouvelle machine parmi les finalistes) est tenu par le clavecin, finaliste
sur la basse à 7,3 % du premier, et la vielle sur `other` à 38 % — sans
qu'aucune ne gagne. Durée 6 366 s contre 5 817 (+9 %, le même prix que sur
*Us and Them*). Le fait : sur *Sky and Sand*, dix machines de plus ne
coûtent rien et n'apportent rien ; sur *Us and Them*, le clavecin gagnait
une voix.

**sky-parite-m9 — MESURÉE (13:43) : 0,244108, soit +1,4 % de son témoin
parc63.** La règle écrite d'avance tranche : au-delà de +1 %, l'hypothèse
est RÉFUTÉE sur ce morceau, et v15 (−5,0 % sur *Us and Them*) reste un
fait de ce morceau-là. `--machines-au-melange 9` reste une option, et le
défaut reste 6. Ce que la course montre, et qui vaut plus que le chiffre :
la basse a changé de machine — `vsm.string`, septième à neuvième finaliste,
GAGNE au verdict (0,2486 contre 0,2523 pour vector au même stade) — mais le
réglage au mélange qui suit rattrape moins bien string que vector : final
0,2441 contre 0,2407. **Le verdict juge un mélange AVANT réglage, et le
réglage peut renverser son ordre.** Sur *Us and Them* le sitar gagnant au
verdict gagnait aussi après réglage ; ici non. Un verdict qui jugerait des
finalistes RÉGLÉS coûterait un réglage par finaliste (mille secondes
chacun) ; un second verdict APRÈS réglage, entre le gagnant réglé et le
second non réglé, est la forme économe à essayer — attendu à écrire avant
sa mesure, campagne 7 si elle vient. Durée 5 877 s (le troisième tour n'a
pas eu lieu : deux tours, comme parc63).

**Ce que la campagne 6 clôt.** Deux morceaux mesurés dans chaque sens :
le parc élargi ne coûte rien à la distance (−0,4 % et 0,0 %) et se paie en
temps (+9 %) ; neuf finalistes gagnent −5,0 % sur un morceau et perdent
+1,4 % sur l'autre — c'est une constante du morceau, et le défaut ne
change pas.

## 11. Campagne 7 : le second verdict, entre candidates RÉGLÉES — attendu écrit AVANT (04/09/2026, 15:35)

**Ce que la campagne 6 a laissé, relu aux chiffres.** Le § 10 disait que
« le réglage au mélange rattrape moins bien string que vector ». Les
rapports disent autre chose : au stade du réglage, la basse de m9 (string)
arrive à 0,2346 et celle de parc63 (vector) à 0,2345 — le même point ; et
`other` (musicbox dans les deux) à 0,2307 contre 0,2297. L'écart final
(0,2441 contre 0,2407, +1,4 %) naît donc pour un tiers au réglage d'`other`
et pour le reste EN AVAL du réglage, dans des étapes que ces deux rapports
ne détaillent pas. Ce qui reste vrai : le verdict juge des candidates
AVANT réglage (string 0,2486 contre vector 0,2523), et le réglage efface
cet ordre. Un verdict qui jugerait des candidates réglées est la forme
honnête ; la forme économe est un SECOND verdict, après le réglage de la
gagnante, contre ses meilleures écartées réglées à leur tour.

**L'option.** `--second-verdict N` (défaut 0 = le témoin, même code) :
après le réglage au mélange de la gagnante de chaque piste mélodique, ses
N meilleures écartées qui changent de machine sont installées comme au
premier verdict (`install_alternative`, factorisé), réglées au mélange
avec le même budget, et la meilleure des réglées est gardée. Chaque
candidate est jugée dans le même contexte (la gagnante est remise entre
deux). Tout est publié dans `rapport.json` sous `secondVerdict` : distance
au verdict, installée, réglée, gagnante réglée, et la machine avant/après.
Coût : un réglage au mélange par candidate — 950 à 1 030 s sur `bass`,
1 270 à 1 310 s sur `other` (campagne 6).

| Course | Témoin | Variable unique | Attendu, écrit d'avance |
|---|---|---|---|
| sky-parite-m9-v2 | sky-parite-m9 (0,244108, moteur à 63 machines du 04/09 10:18) | le MOTEUR : 64 machines (clavinet), D12 à D15 (dither à l'export, contournement, rampes — rien de tout cela ne joue dans une reconstruction sans insert ni rampe, sauf le dither si les rendus sont en entiers), mêmes stems, mêmes options que m9 | identique à m9 à la quatrième décimale (parc63 l'était à la neuvième face à sky-parite) : 0,2441 ± 0,0005, basse `vsm.string`, `other` musicbox. Un écart au-delà dirait que le dither entre dans la distance, et il faudrait alors le mesurer seul |
| sky-parite-m9-sv1 | sky-parite-m9-v2 | `--second-verdict 1` | sur `bass`, vector réglée contre string réglée : les deux valent 0,2345 à 0,2346 au § 10, donc l'écart attendu est sous 0,1 % et le sens est un pile ou face — j'attends que la basse RESTE string (la gagnante réglée garde l'avantage à égalité, par le seuil 1e-6) ou passe à vector pour moins de 0,0005. Sur `other`, la meilleure écartée est mellotron (0,2504 au verdict contre 0,2486) : j'attends qu'elle reste derrière musicbox une fois réglée (musicbox réglée 0,2307 ; mellotron devrait gagner moins de 0,015 au réglage, ce que ni bass ni other n'ont jamais gagné : −0,0140 et −0,0177 au mieux). Distance finale : entre −1 % et +0,5 % du témoin v2. Durée : +2 300 s (deux réglages) sur 5 877, soit 2 h 15 |

**Décision écrite d'avance.** Si le second verdict change une machine ET
que le final gagne au moins 1 % : `--second-verdict 1` devient le défaut,
et son coût (+40 %) s'accepte. Si aucune candidate réglée ne bat la
gagnante réglée sur aucune piste : l'hypothèse « le réglage renverse
l'ordre du verdict » est RÉFUTÉE sur ce morceau, l'option reste à 0, et
le +1,4 % de m9 se cherche EN AVAL du réglage — la prochaine campagne
publiera la distance après chaque étape qui suit (résolution des défauts,
rendu final), ce que les rapports ne font pas encore. Entre les deux (une
machine change, gain sous 1 %) : l'option reste une option, le chiffre se
publie.

Le script `campagne-7.sh` enchaîne les deux courses et s'arrête à la
première qui échoue. Départ à 15:38, fin prévue vers 19:40.

### Verdict de la campagne 7 (04/09/2026, 19:50)

| Course | Distance | Basse | `other` | Durée |
|---|---|---|---|---|
| sky-parite-m9 (témoin de v2) | 0,244108 | `vsm.string` | `vsm.musicbox` | — |
| sky-parite-m9-v2 (moteur à 64 machines) | **0,244108** | `vsm.string` | `vsm.musicbox` | 1 h 48 (15:38 → 17:26) |
| sky-parite-m9-sv1 (`--second-verdict 1`) | **0,228156** (**−6,5 %**) | **`vsm.vector`** | `vsm.musicbox` | 2 h 24 (17:26 → 19:50, **+2 160 s**, +33 %) |

**Le témoin est confirmé à la neuvième décimale.** v2 rend 0,244108
comme m9 : ni la 64e machine, ni D12 à D15 (le dither compris) n'entrent
dans la distance d'une reconstruction. Les distances de piste sont les
mêmes aussi (basse 0,2404, `other` 0,1968).

**Le second verdict est confirmé sur la basse, et mon attendu y est
RÉFUTÉ.** J'attendais un pile ou face sous 0,1 % entre string et vector
réglées (0,2346 contre 0,2345 au § 10). Mesuré, dans le contexte du
second verdict — après le réglage de TOUTES les gagnantes — : vector au
verdict 0,2523, installée 0,2490, **réglée 0,2170** ; string réglée,
remesurée dans le même contexte, 0,2307. L'écart est de 0,0137 (−5,9 %
sur la distance au mélange), pas de 0,0001. La raison est dans le contexte
: au § 10, les deux chiffres venaient de deux COURSES (m9, parc63), donc
de deux mélanges différents ; ici les deux candidates sont réglées contre
le même mélange, celui où `other` est déjà réglée, et l'une y trouve
davantage. Le § 10 avait raison de dire que le réglage efface l'ordre du
verdict ; il avait tort de conclure que les deux machines valaient la même
chose une fois réglées.

**Sur `other`, l'attendu tient.** Mellotron au verdict 0,2504, installée
0,2214, réglée 0,2190 ; musicbox réglée 0,2170 : la gagnante reste, pour
0,0020. Le second verdict coûte 1 646 s sur cette piste pour ne rien
changer — c'est le prix d'une question qu'on ne peut pas trancher sans la
poser.

**La durée est celle annoncée** : +2 160 s pour +2 300 attendus.

**Décision, celle qui était écrite d'avance** : une machine change ET le
final gagne 6,5 % (≥ 1 %) → **`--second-verdict 1` est le défaut** de
`reconstruire.py` depuis ce commit, `0` reste le témoin, et la provenance
le porte (`secondVerdict`). Le +1,4 % de m9 sur parc63 (§ 10) n'a plus à
se chercher en aval : sv1 passe SOUS parc63 (0,2407) de 5,2 %, avec la
machine que parc63 avait trouvée sur la basse.

**Conséquence sur la campagne S1 du banc synthétique** (CDC banc § 5) :
elle avait démarré à 19:51 sur l'ancien défaut ; arrêtée après trois
minutes de course, relancée à 19:54 sur le nouveau, pour mesurer la
chaîne telle qu'elle est. Son coût attendu grandit d'un tiers.

### En attente de la fin des campagnes (03/09/2026) — FAIT le 03/09 au soir (fusion `44fa8c4`, § 9)

Deux retouches sont différées parce qu'elles touchent `audio/` ou
`interchange/` et feraient crier « moteur périmé » toute course lancée après
elles — un avertissement vrai, mais qui doit rester rare pour rester lu :

- **les noms de fichiers des stems exportés par groupe** : `OfflineReconstruction.cpp`
  remplace chaque octet non ASCII par `_`, et « Voix · tête » devient
  `Voix __ t__te.wav`. Translittérer (é → e, œ → oe, « · » → « - ») —
  **FAIT sur la branche `machine-clavecin`** (03/09/2026, 1 test) : elle
  attend la même fusion que les machines ;
- **le vivier de machines** (mémoire permanente de l'utilisateur) : SEPT
  familles sont PRÊTES sur la branche `machine-clavecin`, développées dans
  un worktree séparé, suites vertes, façades rendues — `vsm.harpsichord`
  (le clavecin, § 22 du CDC machines), `vsm.hurdygurdy` (la vielle à roue,
  § 23), `vsm.banjo` (la corde sur la peau, § 24), `vsm.vibraphone` (la
  barre creusée, le tube à moteur, le feutre à pédale, § 25), `vsm.bagpipe`
  (la réserve d'air, § 26), `vsm.carillon` (la cloche accordée, § 27), `vsm.clavinet` (la corde qui sonne
  entière au relâchement, § 28) — et, sur la même branche, **la correction d'un
  défaut de `vsm.cone`** que la cornemuse a révélé : le saxophone ne
  s'éteignait jamais après le relâchement (rms 0,295 deux secondes après),
  la régénération de sa perce tenant la boucle à 1,4 sans souffle. Le
  correctif change l'empreinte du cône (pic −3,4 %, l'attaque) et attend
  donc lui aussi la fin des campagnes ; elles se fusionnent à
  la fin des campagnes, et le moteur se recompile alors pour les courses
  suivantes, provenance à l'appui.

## 12. Épreuve *Children* — une reconstruction depuis zéro, attendus écrits AVANT (11/09/2026, commit `baf905f` à 15:07:53 ; départ des courses à 15:08:20)

**UNE question** : la chaîne d'aujourd'hui rend-elle encore quatre pistes sur
un disque réel ; et si oui, quel étage les fond ?

**L'original.** *Children* (Robert Miles, 1995), face A du vinyle 12"
(`A. Robert Miles - Children.mp3`, 455,7 s, la version longue), lu en place
dans `/home/stefan/robert_miles-children-vinyl-1995/` et jamais copié dans le
dépôt. C'est le troisième original que réclame B3 (INDEX § 4) : il a un VRAI
piano, et pas de guitare. Il a déjà été reconstruit en août (ROADMAP-fusion
§ 5 ter, v10 à v12), par une chaîne à quatre stems, sans parité ni second
verdict : ces chiffres-là ne se comparent à aucun de ceux-ci.

**Depuis zéro, vérifié avant le départ.**

- **Ce qui tourne** (`pgrep`, 14:57) : RIEN — ni le corpus A6 (parc59), ni
  la campagne P1, dont la course `p1-sans` a un journal arrêté le 10/09 à
  12:08 sans ligne de fin (constat, pas un geste de cette épreuve). Rien à
  interrompre ; le parallélisme reste celui du défaut (3 rendus).
- **Le moteur était PÉRIMÉ** : `build/tools/vsm-render` (10/09 17:18) était
  plus ancien que `audio/plugins/sampler/SamplerSynth.cpp` (D103, 11/09
  10:42 — le nom « Sampler (16 emplacements) », rien de sonore). Recompilé,
  la cible seule (11/09 15:00:36, code 0), puis revérifié : aucune source
  d'`audio/`, `core/`, `tools/`, `interchange/` n'est plus récente. Le cache
  de rendus a pour clé l'empreinte du binaire : aucune entrée d'une course
  antérieure ne peut être relue.
- **Aucun stem repris** pour les courses 1 et 2 : chacune sépare elle-même,
  dans un dossier de travail neuf (`reconstruction/travail/children-*`).
- **Les défauts tels quels** : `htdemucs_6s` en sous-processus (shifts=0),
  parité (les quatre découpages), second verdict 1, six machines remises au
  mélange, trois tours de verdict, métrique v2, budget de piste 40 × 8 axes,
  seuil de stem 0,5 %, 3 rendus de front. Aucun seuil touché, aucune variable
  d'environnement (`VSM_PROFIL` compris : la route multisample joue le profil
  que l'utilisateur obtient, celui que le journal nommera).
- **Pas de graine** : `reconstruire.py` n'en a pas. La séparation est
  déterministe (shifts=0, sha256 identiques mesurés, § 5) et le classement ne
  dépend pas du nombre de rendus. « Même graine » est donc tenu par
  construction, et la course 3 le vérifie en passant (voir plus bas).

### 12.1 Les parties attendues

Lues de deux façons, toutes deux dites. (a) L'énergie de l'original par
bande, tranche de 8 mesures à 135 bpm (14,2 s ; tempo estimé 136), et la part
percussive (HPSS). (b) Une transcription MIDI d'amateur du même mix
(`nonstop2k`, 458,8 s à 135 bpm, 18 pistes) : c'est une REFAITE, pas la
session d'origine — elle propose une orchestration plausible et des dates
d'entrée, pas une vérité ; ses doublures (deux basses sur les mêmes 731
notes) ne comptent pas pour deux parties.

| Temps | Ce que l'énergie montre | Lecture (MIDI d'amateur) |
|---|---|---|
| 0 → 28 s | rien au-dessus de 1,5 kHz (−17 dB), grave-médium seul | la nappe seule (nappe dès 3,6 s) |
| 28 s | 400-1 500 Hz +13 dB, 1,5-5 kHz +28 dB | **le piano entre** (32,0 s) |
| 57 s | 150-400 Hz +5 dB, 5-11 kHz +16 dB | cordes (60 s), chœur (89 s) |
| 114 s | part percussive 0,09 → 0,30, sous 60 Hz +5 dB | grosse caisse (117,3 s) |
| 128 s | 60-150 Hz +6 dB, percussive 0,45 | basse (131,8 s) |
| 156 → 213 s | 1,5-5 kHz +3 à +4 dB | arpège aigu (160 s), lead en scie (189 s) |
| 213 → 270 s | médium −5 dB, 1,5-5 kHz −15 dB, 5-11 kHz à −2 dB puis remontée | **pont** : grosse caisse et basse seules, pas de charleston, puis la montée |
| 270 → 370 s | tout revient | le thème entier |
| 370 → 398 s | le même creux qu'à 213 s | second pont |
| 398 → 455 s | tout revient, décroît sur les 15 dernières secondes | final |

| # | Partie | Plage | Sûreté |
|---|---|---|---|
| 1 | **piano** (le motif) | 28 → ~370 s | sûre |
| 2 | **nappe synthé** | 0 → ~370 s | sûre (seule dans l'intro) |
| 3 | **nappe de cordes** | 57 → ~370 s | probable |
| 4 | **lead synthé** (le motif doublé en scie) | ~189 → 455 s, hors ponts | probable |
| 5 | **basse** | 128 → 455 s | sûre |
| 6 | **grosse caisse** | 114 → 455 s, ponts compris | sûre |
| 7 | **charleston** | ~117 → 455 s, absent à 213-256 et 370-398 | sûre |
| 8 | **caisse claire / clap** | ~117 → 455 s, hors ponts | probable |
| 9 | arpège aigu (C5-C#6) | 160 → 370 s | incertaine (MIDI, et +3 dB à 1,5-5 kHz) |
| 10 | chœur « aah » | 89 → 370 s | incertaine (MIDI seul) |

**Mon compte : 8 parties** (1 à 8), 10 au plus avec les deux incertaines.
Pas de voix (la version longue est instrumentale ; la « Vocal Mix » est sur
l'autre face), pas de guitare. Conséquence écrite d'avance : **un stem
`vocals` ou `guitar` qui passe le seuil de 0,5 % est un stem de FUITE, et la
piste qu'il donne est une piste INVENTÉE.**

### 12.2 Les courses, et ce que chacune prédit

Toutes : `analyse/.venv/bin/python -u reconstruire.py <original> --sortie
reconstruction/travail/children-cN-…`, par un script lancé sous `setsid
nohup`, qui s'arrête à la première course en échec.

**COURSE 1 — le défaut** (`children-c1-defaut`, et `--garder-stems` pour que
la course 3 reprenne ses stems ; ce dossier ne conditionne rien).

| Étage | Prédiction |
|---|---|
| séparation | six stems ; **`piano` non vide (5 à 15 %) et porteur du motif** (sa transcription dans F3-G#5, à partir de ~28 s) ; `drums` le plus gros (35 à 50 %) ; `other` 15 à 30 % ; **`guitar` et `vocals` au-dessus de 0,5 %** — des fuites du lead et des arpèges |
| `other` | FOURRE-TOUT (polyphonie ≥ 3 et ambitus ≥ 36 : nappe, cordes et lead couvrent F1-F5) ; **`registres_par_vides` ne coupe PAS** — comme sur les deux disques mesurés (H25) : pas de creux sous le quart du plus petit sommet voisin, parce que nappe et cordes remplissent l'ambitus sans trou ; `separer_en_voix` rend alors **4 voix** par k-moyennes |
| `piano` | pas un fourre-tout (ambitus ~27 demi-tons) : **1 piste** |
| `bass` | 1 piste |
| batterie | **3 pièces** (kick, hihat, snare/clap) ; une quatrième (percussion) possible |
| `vocals` | 1 piste reportée ; tête/chœurs découpé seulement si la fuite est large |
| `guitar` | 1 piste |
| arbitrage du `piano` | `vsm.piano` (le modèle physique) dans les trois premières ; **la route multisample n'est PAS une route piano au défaut** : sans `VSM_PROFIL`, la machine joue le PREMIER profil installé, et sur ce poste l'ordre alphabétique commence par un accordéon (`FR3-Accordion`) — le journal le dira |
| distance | ~0,25 à 0,32 ; **ne se compare à rien** (aucun témoin de même budget) |
| durée | 2 à 4 h |

**Compte prédit : 11 pistes (9 à 13)** — bass 1, piano 1, other 4,
batterie 3, guitar 1, vocals 1. **Fondues : au moins 2** (les registres
d'`other` coupent par hauteur, et nappe, cordes, lead et arpège se
recouvrent en hauteur : chaque registre en portera plusieurs). **Inventées :
au moins 2** (`guitar`, `vocals`). Ma prédiction est donc que le défaut de
la chaîne sur ce disque n'est PLUS le compte — c'est la COMPOSITION : trop
de pistes par invention, et des parties encore fondues à l'intérieur.

**Ce qui réfute** (écrit par l'utilisateur, repris tel quel) : **quatre
pistes ou moins** réfute la parité sur ce disque ; **plus de dix** réfute
l'inverse (la chaîne ne sous-découpe plus, elle fabrique). Ma prédiction
tombe du second côté : si elle se vérifie, le chantier suivant est
l'invention, pas la fusion.

**COURSE 2 — B3, une seule variable** (`children-c2-htdemucs`) : les mêmes
jetons, plus `--modele htdemucs`. Quatre stems ; `other` avale piano, nappe,
cordes et lead — **part d'`other` ≥ 30 %** (sur *Us and Them*, 57,7 %) ;
fourre-tout → 4 voix ; batterie 3 ; vocals 1. **Compte prédit : 9 (8 à 10).**
**Distance : la course 1 (six sources) plus proche de 2 à 8 %.** Ce n'est
pas la même mesure que le −10,4 % de H22b : la parité est allumée des deux
côtés (c'est le défaut depuis le 04/09), elle ne l'était d'aucun. Règle
écrite d'avance : course 2 plus loin d'au moins 2 % → **B3 confirme le
défaut sur un troisième original**, le seul des trois qui ait les deux
propriétés (un piano réel, pas de guitare) ; entre −2 et +2 % → neutre, le
défaut se garde pour ce qu'il nomme ; course 2 plus proche de plus de 2 % →
le gain des six sources NE SE RETROUVE PAS sur un morceau à piano, la
réserve du § 4.2 s'élargit et devient l'hypothèse du chantier suivant.
**Dans les trois cas le défaut ne change pas ici** : cette épreuve mesure,
elle ne corrige pas.

**COURSE 3 — le plafond de structure** (`children-c3-plafond`), stems de la
course 1 repris par `--stems` (copiés d'abord dans un dossier qui ne contient
QU'EUX : `--stems` ramasse tous les `.wav` d'un dossier, et le dossier de
travail de la course 1 contient aussi ses rendus intermédiaires). Les
options, chacune nommée :

| Option | Sous cette forme ? | Dans la course 3 |
|---|---|---|
| `--voix-par-stem` | oui | `4`, explicite |
| `--voix-par-vides` | oui | explicite |
| `--batterie-par-piece` | oui | explicite |
| tête/chœurs « si une voix est détectée » | **NON** : `--voix-tete-choeurs` ne DÉTECTE pas de voix, il découpe le stem nommé `vocals` s'il est large, voix ou fuite | explicite, sous sa vraie forme |
| `--seuil-stem 0` | oui — reconstruit les stems sous 0,5 % | oui |
| `--garder-pieces-non-isolees` | oui — joue les pièces de batterie sans frappe isolée | oui |
| `--voix-par-stem` > 4 | existe | **non** : c'est un MAXIMUM que les k-moyennes remplissent par construction sur un fourre-tout (§ 6 bis) — le monter mesure le paramètre, pas la chaîne |
| `--residuel N` | existe | **non** : ce n'est pas un découpage mais une boucle de reséparation, mesurée inerte (R1 : zéro soustraction sur vingt morceaux) |

Les quatre premières SONT le défaut (`--parite`) : à elles seules, la course
3 serait la course 1, au bit près. Ce qui la distingue, ce sont les deux
suivantes. **Prédiction : course 1 + les stems refusés par le seuil + les
pièces écartées faute de frappe isolée.** Si la course 1 n'en refuse ni n'en
écarte aucun, la course 3 doit rendre sa distance AU BIT PRÈS — et c'est la
preuve du déterminisme promise plus haut.

Le verdict (la table parties attendues × pistes obtenues pour les trois
courses, fondues et inventées à part, et la réponse) s'écrira ici, en
§ 12.3, sans toucher à ce qui précède. **Rien ne se corrige dans cette
épreuve** : un seuil qu'on baisse pour un morceau est un seuil qu'on n'a pas
mesuré ; ce que le verdict désigne devient une hypothèse écrite pour le
chantier suivant.

### 12.3 Le verdict — course 1 (12/09, écrit à la fin de la course 1 ; les courses 2 et 3 suivent)

**La course.** Code 0, le 12/09 à 00:17:55. **Distance 0,1935** (métrique v2,
budget 20). **9 pistes jouantes et 1 bus** (Batterie) — les « 10 piste(s) »
du journal comptent le bus —, 9 224 notes. Temps : 24 609 s de chaîne pour
32 975 s d'horloge, et l'écart est EXACTEMENT la somme des quatre veilles du
poste pendant la course, lues au journal du système (16:04-16:32,
20:17-21:44, 21:52-21:55, 22:02-22:23 : 8 366 s). La chaîne a donc travaillé
**6 h 50 min éveillée**, pour 2 à 4 h prédites.

**Ce que la séparation a fait** — part de chaque stem de `htdemucs_6s` dans
l'énergie de chaque section (%), mesurée sur les stems gardés par la course :

| stem | 0-28 | 28-57 | 57-114 | 114-128 | 128-213 | 213-270 | 270-370 | 370-398 | 398-456 | morceau |
|---|---|---|---|---|---|---|---|---|---|---|
| drums | 0,1 | 0,0 | 0,1 | 67,9 | 66,8 | 74,6 | 67,2 | 74,8 | 72,1 | 66,9 |
| bass | **92,9** | 54,3 | 32,9 | 22,6 | 19,6 | 24,8 | 20,6 | 24,9 | 19,9 | 22,2 |
| other | 6,9 | 15,7 | 40,2 | 6,7 | 13,2 | 0,4 | 11,4 | 0,2 | 8,0 | 9,5 |
| piano | 0,0 | **29,1** | 8,1 | 1,7 | **0,1** | 0,0 | 0,4 | 0,1 | 0,0 | 0,7 |
| guitar | 0,0 | 0,9 | **18,3** | 0,2 | 0,0 | 0,2 | 0,2 | 0,0 | 0,0 | 0,6 |
| vocals | 0,0 | 0,0 | 0,3 | 0,8 | 0,3 | 0,0 | 0,3 | 0,0 | 0,0 | 0,2 |

Lu : **le piano n'est dans le stem `piano` que tant qu'il est à nu** — de 28
à 57 s il y porte 29,1 % de l'énergie et 78,8 % de la bande 400 Hz-5 kHz ;
dès l'entrée des cordes (57 s) il passe à `other` et à `guitar`, et après
128 s le stem `piano` n'en garde rien (0,1 %), alors que le piano joue
jusqu'à ~370 s. **La nappe de l'intro, seule de 0 à 28 s, est dans `bass`**
(92,9 %). Les cordes et le chœur, à leur entrée, vont pour un quart à
`guitar` (25,7 % de la bande 400 Hz-5 kHz à 57-114 s).

**La confrontation, étage par étage** (les prédictions du § 12.2, telles
qu'écrites) :

| étage | prédit | obtenu | |
|---|---|---|---|
| séparation | `piano` 5 à 15 %, porteur du motif | 0,7 % ; porteur de 28 à 57 s seulement | réfuté |
| | `drums` 35 à 50 % | 66,9 % | réfuté (au-dessus) |
| | `other` 15 à 30 % | 9,5 % | réfuté |
| | `guitar` et `vocals` au-dessus de 0,5 % | 0,6 % et 0,2 % : `vocals` sous le seuil, aucune piste | à moitié |
| `other` | fourre-tout ; `registres_par_vides` ne coupe pas ; 4 voix | polyphonie moyenne **2,58** (< 3), ambitus 67 : PAS fourre-tout. La chaîne n'appelle `registres_par_vides` et `separer_en_voix` QUE sur un fourre-tout (`reconstruire.py`, `if args.voix_par_vides and plainte`) : **ni l'un ni l'autre n'a été essayé** ; 1 piste | réfuté |
| `piano` | 1 piste | 1 piste | tenu |
| `bass` | 1 piste | 1 piste (C1-F5, 53 demi-tons, polyphonie 1,26) | tenu |
| batterie | 3 pièces, une quatrième possible | 5 : kick+kick2, hihat, tom, percussion, snare | réfuté (au-dessus) |
| arbitrage du `piano` | `vsm.piano` dans les trois premiers ; multisample joue `FR3-Accordion` par défaut, le journal le dira | `vsm.piano` au **rang 163** (0,537) ; le journal nomme bien `FR3-Accordion` comme profil par défaut, et l'arbitrage met tous les profils en concurrence : `multisample[FR3-Saw-Lead]` 0,260 ; le verdict du mélange change ensuite la machine (tour 1) : **`vsm.tb303`**, D = 0,2278 | réfuté |
| distance | 0,25 à 0,32 | **0,1935** | hors de la fourchette (plus proche) ; ne se compare à rien |
| durée | 2 à 4 h | 6 h 50 min éveillée | réfuté |
| compte | 11 (9 à 13) | **9 pistes** (+ 1 bus) | tenu, à la borne basse |

**Parties attendues × pistes de la course 1** (§ 12.1 ; « porte » : la piste
en contient une part, lue sur l'énergie des stems par section — lecture, pas
preuve, pour les parties que rien d'autre ne sépare) :

| # | partie | piste(s) qui la portent |
|---|---|---|
| 1 | piano (le motif) | `piano` de 28 à 57 s seulement ; ensuite `other` |
| 2 | nappe synthé | `bass` (l'intro), puis `other` |
| 3 | nappe de cordes | `other` ; `guitar` à son entrée (57-114 s) |
| 4 | lead en scie | `other` (seul stem présent dans 1,5-5 kHz à 156-213 s, hors batterie) |
| 5 | basse | `bass` |
| 6 | grosse caisse | `Batterie · kick+kick2` (959 frappes) |
| 7 | charleston | `Batterie · hihat` (575) |
| 8 | caisse claire / clap | `Batterie · snare` (41 — maigre) |
| 9 | arpège aigu (incertaine) | `other` |
| 10 | chœur (incertaine) | `other`, `guitar` |

**Fondues** : `other` (`vsm.sitar`, 3 651 notes sur F1-C7) porte le piano
après 57 s, la nappe, les cordes, le lead — deux parties sûres et deux
probables sur UNE piste ; `bass` (`vsm.hurdygurdy`) porte la basse ET la
nappe de l'intro. **Inventées** : `Batterie · tom` (183 frappes ; l'original
n'a pas de tom, et le verdict du mélange dit le morceau MEILLEUR sans elle,
0,2027 contre 0,2103) ; `guitar` n'est pas une invention pure — elle porte
une part des cordes à leur entrée — mais l'original n'a pas de guitare, et
elle joue un clavicorde sur 0,6 % de l'énergie ; `Batterie · percussion`
(52 frappes), douteuse.

**Ce que la course 1 répond, en attendant les deux autres.** La chaîne ne
rend plus quatre pistes : neuf, et la parité n'est pas réfutée (ni quatre
ou moins, ni plus de dix). Mais le compte cache la composition : les
parties se fondent dans DEUX étages, dans cet ordre. **(1) La séparation** :
`htdemucs_6s` ne garde le piano dans son stem que tant qu'il est à nu, puis
le verse dans `other` ; il met la nappe de l'intro dans `bass`. **(2) La
porte du fourre-tout** (polyphonie moyenne ≥ 3 ET ambitus ≥ 36) : `other`
n'y atteint pas (2,58), et c'est elle qui décide si le découpage est même
essayé. Ma prédiction attendait l'inverse — trop de pistes par invention,
des parties fondues à l'intérieur des registres — ; elle tombe sur le premier
point (deux inventions, pas quatre) et se vérifie, en pire, sur le second :
les parties ne sont pas fondues DANS des registres, elles le sont dans un
stem qu'aucun registre n'a touché. L'hypothèse pour le chantier suivant
s'écrira au verdict final, avec les courses 2 et 3.

#### La course 2 — B3, `--modele htdemucs` (12/09, code 0 à 05:51:45)

**La course.** **Distance 0,2342**. **7 pistes jouantes et 1 bus**, 7 656
notes. Temps : 11 565 s de chaîne pour 20 029 s d'horloge — l'écart (8 464 s)
est la veille de la nuit, 01:19:27 → 03:40:28, lue au journal du système.

**Ce que la séparation a fait** — part de chaque stem de `htdemucs` (quatre
sources) dans l'énergie de chaque section (%), la bande 400 Hz-5 kHz entre
parenthèses là où elle change la lecture :

| stem | 0-28 | 28-57 | 57-114 | 114-128 | 128-213 | 213-270 | 270-370 | 370-398 | 398-456 | morceau |
|---|---|---|---|---|---|---|---|---|---|---|
| drums | 0,3 | 0,0 | 0,1 | 60,9 | 64,7 | 74,3 | 65,6 | 74,3 | 71,3 | 65,0 |
| bass | **96,3** | 44,8 | 21,7 | 19,4 | 19,4 | 25,0 | 19,6 | 25,3 | 19,5 | 21,4 |
| other | 3,4 | **55,2** (99,1) | **77,8** (99,4) | 19,4 | 15,9 | 0,7 | 14,8 | 0,3 | 9,2 | 13,6 |
| vocals | 0,0 | 0,0 | 0,4 | 0,4 | 0,0 | 0,0 | 0,0 | 0,0 | 0,0 | **0,0** |

À quatre stems, `other` prend tout le médium DÈS 28 s (99 % de la bande
400 Hz-5 kHz de 28 à 114 s) : le piano y est avec la nappe, les cordes et le
lead, sans le détour par un stem `piano` qui les tenait quelques secondes en
course 1. Et **la nappe de l'intro tombe encore dans `bass`** (96,3 %),
comme en course 1 : ce n'est donc pas le nombre de sources qui l'y met.

**Les pistes.** `bass` → **`vsm.piano`** (D = 0,2288 ; F1-F5, polyphonie
1,26) ; `other` → **`vsm.phasedist`** (D = 0,1735 ; 4 232 notes, F1-G#6,
polyphonie 2,89) ; cinq pièces de batterie sur `vsm.tr909` (kick+kick2 932
frappes, hihat 608, snare 178, percussion 58, **tom 10**).

**La confrontation aux prédictions du § 12.2** :

| prédit | obtenu | |
|---|---|---|
| `other` ≥ 30 % de l'énergie | **13,6 %** | réfuté — la batterie prend 65 % dès 114 s |
| `other` fourre-tout → 4 voix | polyphonie **2,89** (< 3) : PAS fourre-tout ; `registres_par_vides` et `separer_en_voix` jamais appelés ; **1 piste** | réfuté |
| batterie 3 pièces | 5 | réfuté (au-dessus) |
| `vocals` 1 piste | 0,0 % d'énergie, sous le seuil de 0,5 % : **aucune** | réfuté |
| compte 9 (8 à 10) | **7 pistes + 1 bus** | tenu, à la borne basse |
| course 1 plus proche de 2 à 8 % | course 1 **0,1935**, course 2 **0,2342** : la course 2 est **21,0 % plus loin** (la course 1 17,4 % plus près) | direction tenue, **écart bien plus grand que prédit** |

**Ce que B3 en dit.** La règle écrite d'avance : « course 2 plus loin d'au
moins 2 % → B3 CONFIRME le défaut sur un troisième original ». L'écart est
de 21 % : **`htdemucs_6s` par défaut est confirmé sur un troisième
original**, celui qui a un vrai piano et pas de guitare. À noter pour la
réserve du § 4.2 : le gain ne vient pas d'un stem `piano` qui tiendrait le
motif — il ne le tient qu'à nu, 28 à 57 s (course 1) — mais du partage
général, et il est ici trois fois plus grand que sur *Us and Them*.

**Fondues et inventées.** `other` (`vsm.phasedist`) porte piano, nappe,
cordes et lead : une piste pour quatre parties, une de plus qu'en course 1,
puisque le piano n'a plus de stem à lui. `bass` (`vsm.piano` — la machine
piano joue la BASSE et la nappe) en porte deux. Inventées : la batterie donne
cinq pièces là où l'original en a trois, et le verdict du mélange dit le
morceau MEILLEUR sans trois d'entre elles — hihat (0,2334 contre 0,2734),
percussion (0,2130), snare (0,2602) —, toutes conservées parce que couper
est une décision humaine. **Aucune piste `vocals` ni `guitar` : à quatre
stems, les deux inventions de la course 1 disparaissent.**

#### La course 3 — le plafond de structure (12/09, code 0 à 12:29:19)

**La course.** **Distance 0,1982** (0,19816). **11 pistes jouantes et 1 bus**,
9 224 notes. Temps : 19 695 s de chaîne pour 23 854 s d'horloge, et l'écart
(4 159 s) est la somme des DEUX veilles du poste — 08:10:10 → 09:14:30 et
09:15:00 → 09:20:00, soit 4 160 s au journal du système —, à la seconde près.
La séparation n'a pas eu lieu : les six stems de la course 1 ont été repris par
`--stems`, et le partage relu est le même au dixième (drums 66,9 %, bass
22,2 %, other 9,5 %, piano 0,7 %, guitar 0,6 %, vocals 0,2 %).

**Ce que chaque option a fait, séparément.**

| option | effet mesuré |
|---|---|
| `--voix-par-stem 4`, `--voix-par-vides`, `--batterie-par-piece`, `--voix-tete-choeurs` | aucun : elles SONT le défaut, et le journal le dit en tête — « `--parite` : rien à allumer, tout était déjà demandé » |
| `--garder-pieces-non-isolees` | **aucun** : la course 1 n'avait écarté AUCUNE pièce faute de frappe isolée. Les deux courses trouvent 6 pièces et 1 810 frappes (kick 816, hihat 575, tom 183, kick2 143, percussion 52, snare 41), et les éclatent en 5 pistes, à l'identique |
| `--seuil-stem 0` | **le seul qui agit** : le stem `vocals`, refusé en course 1 (0,2 % de l'énergie, sous le seuil de 0,5 %), est repris ; `--voix-tete-choeurs` le coupe par le champ stéréo en tête 64 % et chœurs 36 % (part latérale 0,26), somme égale au stem exactement → **deux pistes de plus** |

**Le déterminisme, prouvé plus fort que promis.** Le § 12.2 écrivait : « si la
course 1 n'en refuse ni n'en écarte aucun, la course 3 doit rendre sa distance
AU BIT PRÈS ». La condition n'est pas remplie — un stem a été refusé —, et ce
qui a été obtenu vaut mieux : **toutes les décisions des deux courses sont
identiques, au dernier chiffre**. Les quatorze lignes de décision du journal se
superposent (arbitrage de piste, réglage, arbitrage de batterie, verdict du
mélange en 2 tours sur les mêmes pistes, second verdict jusqu'à 0,1935), les
distances de stem du rapport sont égales à la quinzième décimale (`bass`
0,306785487606432 des deux côtés), et les machines finales sont les mêmes :
`bass` → `vsm.hurdygurdy`, `guitar` → `vsm.clavichord`, `other` → `vsm.sitar`,
`piano` → `vsm.tb303`, batterie → `vsm.tr909`. Douze heures d'intervalle, même
moteur, même résultat.

**L'intégrité de l'épreuve, vérifiée et non supposée.** Les trois courses
portent trois commits différents en provenance (073a133, 1153a05, 64ab0fe) :
entre le premier et le dernier, `git diff --name-only` ne montre que `app/`
(16 fichiers), `docs/`, `CLAUDE.md` et `tools/inventaire_langue.py` — **ni
`core/`, ni `audio/`, ni `interchange/`, ni `analyse/`**. Et le binaire de
rendu porte encore l'horodatage que le journal de l'épreuve a relevé à son
départ (`build/tools/vsm-render`, 2026-09-11 15:00:36,814600964). Les trois
courses ont donc tourné sur UN moteur ; la règle qui a bloqué D18.7b et A21
treize jours durant a tenu.

**Alors d'où vient l'écart de distance ?** 0,19353 en course 1, 0,19816 en
course 3 : **+2,39 %**, pour deux pistes dont le journal dit « volume non calé
(piste sans machine ou sans note) ». Cette phrase se lit comme « la piste est
vide » ; elle ne l'est pas. Mesuré, et non supposé : la différence des deux
rendus (`reconstruit.wav`, 20 028 612 trames chacun) régressée sur la somme des
deux pistes de voix.

| mesure | valeur |
|---|---|
| gain de moindres carrés | **0,6364** |
| `R²`, part de la différence expliquée | **1,000000** |
| résidu | **−118,7 dB** sous la différence — l'arrondi de `float32` |
| énergie ajoutée au rendu | **0,072 %** |

0,6364, c'est 0,9/√2 : le volume d'usine d'une piste, et la loi de panoramique
centrée du moteur. **L'écart entier tient donc aux deux pistes de voix, et à
rien d'autre** — le reste du rendu est identique à l'arrondi près, ce qui
achève la preuve de déterminisme ci-dessus.

**Ce que ces deux pistes sont.** `reporter_voix` (`analyse/reconstruire.py`)
pose le stem vocal sur une piste AUDIO : « la voix ne se synthétise pas »,
décision écrite de longue date et assumée pour ce qu'elle est. Ce sont donc
deux pistes qui REJOUENT le stem, pas deux pistes inventées par une machine.

**Ce qui est un défaut, en revanche, et qui est neuf.** Le calage des volumes
(`analyse/analyzer/vsm_levels.py:109`) ne sait caler qu'une piste qui a une
machine ET des notes : une piste audio sort de l'étage à son volume d'usine
0,9, que la loi de panoramique ramène à 0,636 — alors que tête + chœurs
redonnent le stem EXACTEMENT, c'est-à-dire qu'elles devraient entrer au gain
1,0 pour rendre au mélange la part que la séparation lui a retirée. La chaîne
pose la voix 3,9 dB trop bas, et l'annonce par une phrase qui laisse croire
qu'elle ne la pose pas.

**ATTENDU DE LA MESURE QUI SUIT, ÉCRIT AVANT ELLE (12/09, 13:56).** Deux
lectures restent possibles, une seule survivra. **(a)** Le stem `vocals` d'un
morceau INSTRUMENTAL est de la fuite — de l'énergie que les autres stems
portent déjà — et la rejouer la compte DEUX FOIS : la distance croît alors avec
le gain dès 0, son minimum est en g = 0, et c'est `--seuil-stem 0` qui
fabrique. **(b)** Le stem manque vraiment au mélange et la distance a son
minimum vers g = 1,0 : le défaut est alors le CALAGE, pas le seuil, et la
course 3 paie 2,39 % pour une piste posée 3,9 dB trop bas. **Je parie sur
(a)** : le § 12.1 écrit d'avance que ce disque est instrumental, et un stem à
0,2 % d'énergie sur un disque sans voix est un résidu de séparation. Ce qui
tranche : la distance v2 de l'original au rendu de la course 1 augmenté de
g·(tête + chœurs), pour g = 0, 0,3, 0,6364, 1,0 et 1,4142. **Les deux témoins
font partie de la mesure** : g = 0 doit retomber sur 0,19353 et g = 0,6364 sur
0,19816, sans quoi un chiffre qui bouge ne dira pas s'il mesure le mélange ou
mon outil.

**LA MESURE, ET CE QU'ELLE RÉFUTE (12/09, 14:05).** Les deux témoins retombent
EXACTEMENT sur les chiffres publiés — g = 0 rend 0,19353157517211156 et
g = 0,6364 rend 0,19816082615353953, écart 0,000e+00 des deux côtés : l'outil
mesure le mélange, et pas lui-même.

| g | distance v2 | contre la course 1 |
|---|---|---|
| **0,0** — la course 1 | 0,193532 | — |
| 0,1 | 0,195449 | +0,99 % |
| 0,2 | 0,196201 | +1,38 % |
| 0,3 | 0,196769 | +1,67 % |
| 0,4 | 0,197241 | +1,92 % |
| 0,5 | 0,197662 | +2,13 % |
| **0,6364** — la course 3 | 0,198161 | +2,39 % |
| 0,8 | 0,198697 | +2,67 % |
| **1,0** — le stem exact | 0,199270 | +2,96 % |
| 1,2 | 0,199776 | +3,23 % |
| 1,4142 | 0,200265 | +3,48 % |

**(a) survit, (b) est réfutée — et c'est MA lecture de l'heure précédente qui
tombe.** La distance croît sans minimum intérieur sur tout l'intervalle : le
minimum est en g = 0. Caler la piste audio à 1,0 — le « remède » que le manque
de `vsm_levels.py` semblait appeler — aurait coûté **+2,96 %** au lieu de
+2,39 %. Le manque est réel (une piste audio ne passe par aucun calage) et le
corriger AGGRAVERAIT ce morceau : il ne sera donc pas corrigé sur la foi d'un
raisonnement. Le seuil de 0,5 %, lui, est justifié par la mesure : à 0,2 %
d'énergie, le stem `vocals` de ce disque instrumental est bien le résidu de
séparation que le journal annonce, et la chaîne a eu raison de le refuser.

**La forme de la courbe dit où le défaut n'est pas.** 41 % du coût total (0,99
des 2,39 points) est payé au PREMIER dixième de gain : la métrique réagit à la
PRÉSENCE du résidu bien plus qu'à son niveau. Ce n'est pas une affaire de
dosage, et aucun calage ne la rattrapera.

**LE DÉFAUT QUE CETTE MESURE DÉSIGNE, ET IL EST NEUF.** Le témoin de coupure —
« le morceau est MEILLEUR sans cette piste », le chiffre que la chaîne doit dire
sans jamais couper elle-même — n'a jamais été établi pour ces deux pistes. Il
est posé DANS la boucle des alternatives (`analyse/analyzer/vsm_mix_verdict.py`,
`for track in tracks:` puis `if not propositions: continue`), si bien qu'une
piste sans alternative — une piste AUDIO n'a pas de machine, donc pas de machine
suivante — sort du verdict du mélange tout entier, témoin de coupure compris. La
phrase qui manque au journal de la course 3 est : « Voix · tête, Voix · chœurs :
le morceau est MEILLEUR sans ces pistes (0,1935 contre 0,1982) ». C'est la panne
muette que ce dépôt s'interdit — ce qui est ajouté au mélange sans être mesuré —
et c'est précisément celle que le témoin de coupure avait été écrit pour fermer
(§ 5 decies de `ROADMAP-fusion.md`, la basse de *Sky and Sand* à +5,5 %). Le
trou restait ouvert pour les pistes qu'aucune machine ne joue.

### 12.4 Le verdict de l'épreuve — les trois courses côte à côte (12/09)

|  | course 1 — le défaut | course 2 — B3 | course 3 — le plafond |
|---|---|---|---|
| séparation | `htdemucs_6s`, 6 stems | `htdemucs`, 4 stems | stems de la course 1, repris |
| distance v2 (budget 20) | **0,1935** | 0,2342 | 0,1982 |
| pistes jouantes | 9 | 7 | 11 |
| bus | 1 (Batterie) | 1 | 1 |
| notes | 9 224 | 7 656 | 9 224 |
| chaîne / horloge | 24 609 s / 32 975 s | 11 565 s / 20 029 s | 19 695 s / 23 854 s |
| **parties portées par une piste à elles seules** (sur 8 sûres ou probables) | **3** | **3** | **3** |
| pistes qui fondent plusieurs parties | `other` (4 parties), `bass` (2) | `other` (4), `bass` (2) | les mêmes |
| pistes que l'original n'a pas | `guitar`, `Batterie · tom`, `Batterie · percussion` | 2 pièces de batterie de trop | les mêmes + `Voix · tête`, `Voix · chœurs` |
| machine du piano | `vsm.tb303` (au verdict) | `vsm.piano` — sur la BASSE | `vsm.tb303` |

**La ligne qui porte le verdict est celle du milieu.** Le compte de pistes va de
7 à 11 d'une course à l'autre, la distance de 0,1935 à 0,2342 — et le nombre de
parties qu'une piste porte SEULE ne bouge pas : **trois, et les trois sont des
pièces de batterie** (grosse caisse, charleston, caisse claire), les seules que
la chaîne découpe par un chemin qui lui est propre. Les cinq parties mélodiques
(piano, nappe synthé, nappe de cordes, lead en scie, basse) se partagent deux
pistes dans les trois courses. **Le compte de pistes n'est donc pas une mesure
de la parité** : il monte quand la chaîne ajoute, pas quand elle sépare.

**La réponse aux règles de réfutation écrites par l'utilisateur** (§ 12.2) :
« quatre pistes ou moins réfute la parité sur ce disque ; plus de dix réfute
l'inverse — la chaîne ne sous-découpe plus, elle fabrique ». Les courses 1 (9) et
2 (7) ne franchissent ni l'une ni l'autre. **La course 3 franchit la seconde :
11 pistes jouantes**, et les deux qui font passer la barre sont les deux pistes
de voix d'un disque instrumental, dont la mesure ci-dessus montre qu'elles
coûtent 2,39 %. Sur ce disque, la chaîne ne sous-découpe plus ET elle fabrique ;
les deux défauts tiennent ensemble, et le second est le prix du premier — on
ajoute des pistes là où l'on ne sait pas séparer.

**Les trois étages où la composition se perd, dans l'ordre où ils agissent.**

1. **La séparation décide avant que la chaîne ne puisse rien.** `htdemucs_6s` ne
   garde le piano dans son stem que tant qu'il est à nu (28 à 57 s : 29,1 % de
   l'énergie de la section, 0,7 % du morceau), puis le verse dans `other` ; la
   nappe de l'intro part dans `bass` (92,9 % de 0 à 28 s), et cela ne dépend pas
   du nombre de sources — la course 2 à quatre stems l'y met aussi (96,3 %).
2. **La porte du fourre-tout ne s'ouvre jamais.** Le découpage en voix n'est
   ESSAYÉ que sur un stem déclaré fourre-tout (polyphonie moyenne ≥ 3 ET ambitus
   ≥ 36). `other` mesure 2,58 en course 1 et 2,89 en course 2 : dans les deux
   cas, `registres_par_vides` et `separer_en_voix` n'ont pas été appelés une
   seule fois. Les quatre parties d'`other` ne sont pas fondues DANS des
   registres mal choisis — elles le sont dans un stem qu'aucun découpage n'a
   touché.
3. **Ce que la chaîne ajoute sans machine échappe au verdict.** Démontré par la
   course 3 : deux pistes audio entrent au mélange, le dégradent de 2,39 %, et
   aucune ligne ne le dit.

**CE QUE L'ÉPREUVE DÉSIGNE POUR LA SUITE — deux hypothèses écrites ici, avec
leur critère de réfutation, avant tout travail.**

> **H26 — un fourre-tout se reconnaît dans le TEMPS autant que dans la hauteur.**
> `other` porte quatre parties à 2,58 de polyphonie moyenne parce que ces
> parties sont SUCCESSIVES autant que superposées : le piano entre à 28 s, le
> lead vers 189 s, les ponts les coupent. Une porte qui ne lit que la polyphonie
> et l'ambitus ne peut pas voir cela, et le tableau de partage par sections que
> la chaîne calcule DÉJÀ (§ 12.3) le voit à l'œil nu. Hypothèse : une porte qui
> compare le profil spectral du stem entre ses sections déclare `other`
> fourre-tout sur *Children*, et le découpage qui s'ensuit porte le nombre de
> parties tenues seules au-dessus de trois. **Réfutée si** le découpage par
> entrées et sorties ne fait pas passer ce nombre de 3 à 5 au moins sur ce
> disque — la distance, elle, est publiée dans les deux cas, la parité primant
> sur la ressemblance (§ 0).
>
> **H27 — le témoin de coupure doit couvrir les pistes sans machine.** Le
> déplacer hors de la boucle des alternatives, pour qu'il soit établi pour
> TOUTE piste jouante. **Attendu, chiffré d'avance et vérifiable sans nouvelle
> course** : rejoué sur le projet de la course 3, le verdict imprime « Voix ·
> tête », « Voix · chœurs » et « le morceau est MEILLEUR sans cette piste » avec
> 0,1935 contre 0,1982. **Réfutée si** la phrase ne sort pas, ou sort avec
> d'autres chiffres que ceux-là.

### 12.5 H27 tenue — le témoin de coupure couvre les pistes qu'aucune machine ne joue (12/09)

**CE QUI A CHANGÉ.** Le témoin de coupure était posé DANS la boucle des
alternatives (`vsm_mix_verdict.py`, `if not propositions: continue`) : une piste
sans machine suivante — une piste AUDIO n'a pas de machine, donc pas de
suivante — sortait du verdict du mélange tout entier. Il est désormais établi
pour toute **piste jouante** : `piste_jouante()` répond oui à une piste audio et
à une piste qui a machine ET notes, non à un BUS (le couper couperait ses
membres, dont chacun a déjà son témoin : le chiffre compterait deux fois la même
chose et ne désignerait rien à couper). La mesure et la phrase du journal vivent
maintenant dans deux fonctions uniques, lues par les deux chemins — deux copies
auraient fini par ne plus dire la même chose.

**CE QUE CELA COÛTE** : un rendu de plus par piste sans alternative. La
référence, elle, n'est pas remesurée — c'est la distance du projet telle que
l'itération précédente l'a laissée.

**LES CHIFFRES DE LA COURSE 3, MESURÉS DE BOUT EN BOUT** (projet final rendu par
`vsm-render`, chaque piste mise à 0 tour à tour, distance v2 contre l'original) :

| rendu | distance v2 |
|---|---|
| le projet tel quel | 0,1982 |
| sans `Voix · tête` | **0,1950** |
| sans `Voix · chœurs` | **0,1978** |
| sans les deux | **0,19353157517211156** — la distance publiée de la course 1, au dernier chiffre |

Les deux lignes qui manquaient au journal sont donc, mot pour mot :
« `Voix · tête` : ATTENTION — le morceau est MEILLEUR sans cette piste (0,1950
contre 0,1982) » et « `Voix · chœurs` … (0,1978 contre 0,1982) ».

**ET L'ATTENDU DE H27 ÉTAIT IMPRÉCIS SUR DEUX POINTS, que la mesure corrige.**
Il annonçait « 0,1935 contre 0,1982 » : c'est le chiffre des deux pistes
coupées ENSEMBLE, alors que le témoin coupe UNE piste à la fois — d'où 0,1950 et
0,1978. Et ces chiffres sont ceux du projet FINAL, le seul état que le musicien
ouvre ; la ligne imprimée PENDANT une course se mesure contre l'état du mélange
à cet instant (0,2103 au verdict de la course 3), ce qui dit le même fait sur une
autre référence. Que le témoin soit aussi établi sur le projet final est une
question ouverte, nommée ici et non tranchée.

**CE QUI LE GARDE** : quatre tests (`analyse/tests/test_temoin_de_coupure.py`),
dont le cas de la course 3 — deux pistes, aucune alternative, le rendu remplacé
par une fonction, parce que ce qui est mesuré est la DÉCISION de mesurer et non
le moteur. La suite Python entière est verte (179), ruff et mypy aussi.

### 12.6 H26, premier pas — la statistique qui doit reconnaître un fourre-tout dans le TEMPS (attendu écrit avant la mesure, 12/09 16:10)

**CE QUE LA PORTE ACTUELLE NE PEUT PAS VOIR.** `stem_fourre_tout()`
(`vsm_reconstruct.py`) demande polyphonie moyenne ≥ 3 **et** ambitus ≥ 36
demi-tons. Sur *Children*, `other` porte quatre parties à **2,58** de polyphonie
(course 1) et **2,89** (course 2) : la porte ne s'ouvre pas, et
`registres_par_vides` comme `separer_en_voix` ne sont jamais APPELÉS. Les
parties n'y sont pas fondues dans des registres mal choisis — elles le sont dans
un stem qu'aucun découpage n'a touché. La raison est écrite au § 12.4 : dans ce
morceau, les parties sont SUCCESSIVES autant que superposées (le piano entre à
28 s, le lead vers 189 s, les ponts les coupent), et deux parties qui ne sonnent
pas en même temps ne font pas monter la polyphonie moyenne.

**LA STATISTIQUE PROPOSÉE, ET ELLE SE MESURE SUR L'AUDIO, PAS SUR LES NOTES.**
La transcription est le maillon le plus faible de la chaîne (C2 à l'INDEX :
F1 = 0,367) ; une porte bâtie sur elle hériterait de ses erreurs. Le stem, lui,
est là.

1. Le stem est découpé en fenêtres de **5 s**.
2. Les fenêtres sous **−50 dBFS** (silence) sont écartées, et leur nombre est dit.
3. Chaque fenêtre donne un profil de **8 bandes** logarithmiques de 60 Hz à
   16 kHz, normalisé à somme 1 — un profil de TIMBRE, insensible au niveau.
4. La statistique est la **dispersion** : distance L1 moyenne entre le profil de
   chaque fenêtre et le profil MÉDIAN du stem, dans [0, 2].

Un stem qui porte UNE partie garde son timbre : dispersion basse. Un stem où des
parties entrent et sortent change de profil selon le passage : dispersion haute.

**CE QUI EST ATTENDU, ET CE QUI RÉFUTE.** Les cas dont la composition est ÉCRITE
(§ 12.1 et § 12.3) :

| stem | ce qu'il porte | attendu |
|---|---|---|
| `other` de *Children* | piano (après 57 s), nappe, cordes, lead — **4 parties** | le plus haut des stems mélodiques |
| `bass` de *Children* | la basse **et** la nappe de l'intro — 2 parties | haut |
| `piano` de *Children* | le piano, et seulement de 28 à 57 s — 1 partie | bas |
| `drums` | une famille de frappes, dense mais stable | bas — et s'il est haut, la statistique mesure la densité, pas la composition |

**RÉFUTÉE si** `other` ne dépasse pas `piano`, ou si `drums` se classe avec
`other` : dans le premier cas la statistique ne voit pas ce qu'elle prétend
voir ; dans le second elle mesure autre chose. Aucun seuil n'est posé avant ces
chiffres — le poser d'abord et mesurer ensuite serait le tordre.

### 12.7 H26, premier pas — RÉFUTÉ deux fois, et ce que les chiffres apprennent (12/09)

**LA MESURE DE LA FORME SIMPLE** (§ 12.6 : fenêtres de 5 s, silence sous
−50 dBFS écarté et compté, 8 bandes log 60 Hz-16 kHz normalisées, distance L1
moyenne au profil médian). Deux jeux de stems : *Children* séparé en six sources
(la course 1 de l'épreuve, dont la composition est ÉCRITE au § 12.1) et les six
stems d'un autre disque déjà sur le poste, dont la composition ne l'est pas.

| stem | *Children* | part sonore | autre disque |
|---|---|---|---|
| `bass` (2 parties : basse **et** nappe de l'intro) | 0,4294 | 99 % | 0,3319 |
| `drums` | 0,4575 | 75 % | 0,3028 |
| `guitar` (fuite : 0,6 % de l'énergie) | **0,6577** | 23 % | 0,7471 |
| `other` (**4 parties**) | 0,6176 | 90 % | 0,3827 |
| `piano` (1 partie, et à nu 29 s seulement) | 0,4946 | 35 % | 0,4197 |
| `vocals` (fuite : 0,2 %) | **0,6401** | 29 % | 0,6632 |

**Les deux critères écrits d'avance sont tenus** — `other` (0,6176) dépasse
`piano` (0,4946), et `drums` (0,4575) ne se classe pas avec `other`. **Et la
statistique est inutilisable quand même**, pour un cas que l'attendu n'avait pas
nommé : **les deux stems de FUITE la dominent tous les deux**. Une porte bâtie
là-dessus déclarerait fourre-tout un résidu de séparation à 0,2 % d'énergie et
le partagerait en quatre voix — c'est-à-dire qu'elle fabriquerait des pistes,
précisément le défaut que la course 3 a mesuré. Les parts sonores disent
pourquoi : 23 % et 29 % de fenêtres au-dessus du plancher, contre 90 % pour
`other`. Un stem rare et erratique n'a pas de timbre stable parce qu'il n'a pas
de timbre.

**LA FORME PONDÉRÉE (H26b), écrite après ce constat et avant sa mesure** : la
médiane ET la moyenne pondérées par l'énergie de chaque fenêtre, pour qu'une
fenêtre presque silencieuse ne pèse pas autant qu'un tutti ; la part sonore
publiée à côté. **Réfutée si** `other` ne repasse pas au-dessus des deux fuites,
ou si `drums` la rejoint.

| stem | *Children*, pondérée | part sonore | autre disque, pondérée |
|---|---|---|---|
| `bass` | 0,3213 | 99 % | 0,2662 |
| `drums` | **0,4457** | 75 % | 0,2859 |
| `guitar` | 0,3387 | 23 % | 0,6458 |
| `other` | 0,3770 | 90 % | 0,2796 |
| `piano` | 0,2048 | 35 % | 0,2966 |
| `vocals` | **0,5732** | 29 % | 0,5389 |

**RÉFUTÉE, sur ses deux critères à la fois.** La pondération remet `guitar`
(0,3387) sous `other` (0,3770) — et `vocals` reste au-dessus (0,5732), et
`drums` passe DEVANT (0,4457). Sur l'autre disque, `other` (0,2796) est sous
`drums` (0,2859) et à côté de `bass` (0,2662) : aucune séparation.

**CE QUE CES DEUX MESURES APPRENNENT, ET C'EST UTILE.** La dispersion d'un
profil de bandes mesure l'INSTABILITÉ DE TIMBRE, et l'instabilité de timbre a au
moins trois causes que ce chiffre ne sait pas distinguer : plusieurs parties qui
entrent et sortent, un résidu de séparation qui n'a pas de timbre, et une
batterie dont chaque frappe a le sien. Aucun seuil sur un tel chiffre ne peut
séparer la première des deux autres, et en poser un serait tordre la mesure
après coup. **La porte du fourre-tout reste donc celle d'aujourd'hui**, et H26
n'a pas trouvé sa statistique ici.

**LA PISTE QUE CES CHIFFRES DÉSIGNENT, NON MESURÉE ET DITE POUR CE QU'ELLE
EST.** Ce qui distingue `other` des deux fuites dans ces tableaux n'est pas la
dispersion — c'est la PART SONORE (90 % contre 23 % et 29 %) croisée avec la
part d'énergie du stem (9,5 % contre 0,6 % et 0,2 %). Et ce qui distinguerait
une partie qui ENTRE d'un timbre qui change, c'est un profil de bandes qui
apparaît et se MAINTIENT — une marche, pas une oscillation. Une statistique de
palier (segmentation en plages stables, puis compte des plages dont le profil
diffère durablement) reste à écrire et à mesurer ; elle n'est pas promise ici.

### 12.8 H26, deuxième forme — le PALIER plutôt que la dispersion (attendu écrit avant la mesure, 12/09)

**CE QUE LA RÉFUTATION DU § 12.7 A APPRIS.** La dispersion d'un profil de bandes
mesure l'INSTABILITÉ DE TIMBRE, et trois causes la produisent sans qu'elle sache
les distinguer : plusieurs parties qui entrent et sortent, un résidu de
séparation qui n'a pas de timbre, une batterie dont chaque frappe a le sien. Les
deux stems de FUITE de *Children* dominaient ainsi le vrai fourre-tout.

**CE QUI LES SÉPARE, ET QUI N'A PAS ENCORE ÉTÉ MESURÉ : LA PERSISTANCE.** Une
partie qui entre fait une MARCHE — un profil neuf qui s'installe et DURE. Un
résidu de séparation fait une oscillation : chaque fenêtre diffère de la
suivante, et rien ne s'installe. La statistique proposée compte donc des
PALIERS, pas de l'écart :

1. Fenêtres de 5 s, silence sous −50 dBFS écarté et compté (comme au § 12.6).
2. Profil de 8 bandes logarithmiques 60 Hz-16 kHz, normalisé à somme 1.
3. Deux fenêtres CONSÉCUTIVES appartiennent au même palier si leur distance L1
   est **sous 0,30** — le quart de l'écart maximal observé au § 12.7 entre deux
   stems différents, donc « le même timbre » au sens large.
4. Un palier COMPTE s'il dure au moins **4 fenêtres (20 s)** : ce qui ne dure pas
   n'est pas une partie, c'est un accident de séparation.
5. La statistique est le **nombre de paliers comptés dont les profils médians
   sont mutuellement distants de plus de 0,30** — c'est-à-dire le nombre de
   timbres différents qui se sont INSTALLÉS dans le stem.

**CE QUI EST ATTENDU, ET CE QUI RÉFUTE.** Les compositions écrites au § 12.1 et
mesurées au § 12.3 :

| stem de *Children* | ce qu'il porte | attendu |
|---|---|---|
| `other` | piano (après 57 s), nappe, cordes, lead — **4 parties**, qui entrent et sortent | **≥ 2 paliers** |
| `bass` | la basse ET la nappe de l'intro (seule de 0 à 28 s) | **≥ 2 paliers** |
| `drums` | une famille de frappes, présente et stable | **1 palier** |
| `piano` | le piano, à nu de 28 à 57 s seulement | **1 palier** (il ne joue qu'une fois) |
| `guitar`, `vocals` | résidus de séparation, 23 % et 29 % de fenêtres sonores | **0 ou 1 palier** — rien ne s'y installe |

**RÉFUTÉE si** un stem de fuite atteint le compte d'`other`, ou si `other` n'a
qu'un palier : dans le premier cas la statistique confond encore résidu et
composition ; dans le second elle ne voit pas ce qu'elle prétend voir. Aucun
seuil ne sera déplacé après la mesure — les deux (0,30 et 4 fenêtres) sont posés
ici, avant elle, et s'ils sont mauvais c'est l'hypothèse qui tombe.

### 12.9 H26, deuxième forme — elle SURVIT, et elle sépare ce que la première confondait (12/09)

**LA MESURE**, avec les seuils du § 12.8, posés avant elle et non retouchés
(0,30 de distance L1, 4 fenêtres de durée minimale) :

| stem de *Children* | ce qu'il porte | attendu | **timbres installés** | paliers longs |
|---|---|---|---|---|
| `other` | 4 parties qui entrent et sortent | ≥ 2 | **4** | 5 |
| `bass` | basse + nappe de l'intro | ≥ 2 | **3** | 9 |
| `piano` | une partie, à nu 29 s | 1 | **1** | 1 |
| `guitar` | résidu (0,6 % de l'énergie) | 0 ou 1 | **0** | 0 |
| `vocals` | résidu (0,2 %) | 0 ou 1 | **0** | 0 |
| `drums` | une famille de frappes | 1 | **2** | 7 |

**LES DEUX CRITÈRES DE RÉFUTATION SONT TENUS.** Aucun stem de fuite n'approche
le compte d'`other` — ils sont à **zéro**, là où la dispersion du § 12.7 les
mettait EN TÊTE —, et `other` en a quatre. La persistance fait donc ce que
l'écart ne savait pas : **un résidu de séparation oscille sans rien installer**
(23 % et 29 % de fenêtres sonores, aucune suite de quatre qui se ressemble),
**une partie qui entre s'installe**.

**ET LE COMPTE TOMBE SUR LA COMPOSITION ÉCRITE.** `other` : quatre timbres, pour
quatre parties (piano après 57 s, nappe, cordes, lead) — le § 12.1 les avait
nommées avant toute mesure. `bass` : trois, pour deux parties écrites, avec les
plages qui le disent (un palier de 0 à 40 s, la nappe de l'intro ; un autre de
150 à 210 s ; un troisième après). `piano` : un seul palier, à 60 s, et c'est
exactement la fenêtre où le § 12.3 a mesuré que le piano est à nu.

**CE QUI EST MANQUÉ, ET QUI SE DIT.** `drums` rend **2** là où l'attendu disait
1. Une batterie change bel et bien de timbre entre l'intro sans charleston et le
plein kit, et la statistique le voit ; ce n'est pas un défaut de la mesure, c'est
une limite de l'attendu, qui supposait un stem de frappes uniforme. Sans
conséquence pour la porte : la batterie a son propre chemin de découpage
(`--batterie-par-piece`) et ne passe pas par le fourre-tout.

**CE QUE CELA NE PROUVE PAS ENCORE.** Que la porte ainsi armée AMÉLIORE la
reconstruction. Le compte de timbres installés est une bonne mesure de la
composition d'un stem ; savoir si découper `other` en quatre sur ce disque
rapproche ou éloigne le morceau demande une course, avec son témoin. C'est le
chantier B9, et son attendu s'écrira avant elle.

### 12.10 H26 — la porte ouvre, mais SUR QUOI ? (attendu écrit avant la mesure, 12/09)

**LA QUESTION QUE LA PORTE NE RÉSOUT PAS.** `--porte-paliers` déclare `other`
fourre-tout (4 timbres installés) là où la polyphonie ne le voyait pas. Ce qui
suit dans la chaîne, en revanche, n'a pas changé : `registres_par_vides` coupe
par les CREUX DE HAUTEUR, et `separer_en_voix` par k-moyennes **sur la
hauteur**. Or ce que les paliers ont trouvé est une structure de TEMPS — le piano
entre à 28 s, le lead vers 189 s. Ouvrir la porte sur une structure temporelle
pour la donner à un découpeur de hauteur peut ne rien recouvrer du tout.

**LA MESURE, ET POURQUOI ELLE NE COÛTE PAS UNE COURSE.** La question ne demande
ni rendu ni arbitrage : elle se tranche sur les NOTES. On transcrit `other` une
fois, on lui applique les deux découpeurs dans l'ordre où la chaîne les applique,
et on regarde OÙ tombent les notes de chaque voix obtenue — dans quel palier, et
dans quelle proportion. Dix minutes au lieu de dix heures.

**CE QUE J'ATTENDS, ET CE QUI LE RÉFUTE.** Les parties étant successives, un
découpage par la hauteur doit produire des voix qui s'ÉTALENT sur tous les
paliers au lieu de s'y concentrer : j'attends **aucune voix concentrée** —
« concentrée » = 60 % ou plus de ses notes dans un seul palier. **Réfutée si**
une majorité des voix se concentre : le découpeur de hauteur recouvrerait alors
la structure de temps par accident, et il n'y aurait rien à écrire de plus.

### 12.11 H26 — la mesure a d'abord buté sur une SECONDE porte, et ce qu'elle dit ensuite (12/09)

**CE QUI A BLOQUÉ LA MESURE, ET QUI EST UN RÉSULTAT.** `--porte-paliers` déclare
`other` fourre-tout ; le découpage n'a quand même pas eu lieu. La cause est dans
`separer_en_voix` : sa première ligne **rejuge la densité** —
`if not stem_fourre_tout(densite_du_stem(notes)): return [list(notes)]` — et sa
documentation revendique ce garde-fou comme « le plus important », posé LÀ plutôt
que chez l'appelant. La porte du temps ouvrait donc sur une seconde porte fermée,
et **l'option était INERTE** : mesuré, `other` restait à UNE voix pour 3 651
notes.

**CE QUI A ÉTÉ CHANGÉ, ET CE QUI NE L'A PAS ÉTÉ.** `separer_en_voix` accepte
désormais un argument `justifie`, qui dit que l'appelant a établi le fourre-tout
AUTREMENT. Il ne relâche pas le garde-fou pour tout le monde : la seule
justification écrite à ce jour est le compte de timbres installés, qui rend **0
sur un résidu de séparation** et 4 sur ce stem — c'est-à-dire qu'il refuse
précisément les cas que le garde-fou protégeait (« une mélodie qui saute
d'octave, une nappe d'accords serrés »).

**LA MESURE, ENFIN ATTEIGNABLE.** `other` de *Children*, 3 651 notes, aucun vide
de hauteur (`registres_par_vides` rend 1 registre), puis quatre voix par
k-moyennes sur la hauteur :

| voix | notes | registre | joue de… à | part du morceau | paliers touchés |
|---|---|---|---|---|---|
| 1 | 1 032 | MIDI 72-96 | 56 → 452 s | 87 % | 4/5 |
| 2 | 1 300 | MIDI 60-70 | 2 → 452 s | **99 %** | 5/5 |
| 3 | 501 | MIDI 46-58 | 3 → 373 s | 81 % | 3/5 |
| 4 | 818 | MIDI 29-41 | 1 → 444 s | 97 % | 5/5 |

**LE DÉCOUPEUR DE HAUTEUR NE RECOUVRE PAS LES PARTIES.** Les quatre voix jouent
chacune sur 81 à 99 % du morceau et touchent trois à cinq paliers sur cinq :
elles sont toutes présentes presque partout, quand les parties, elles, ENTRENT ET
SORTENT (le piano à 28 s, le lead vers 189 s). Découper par la hauteur un stem
dont les parties se succèdent donne quatre tranches du même gâteau — exactement
ce que le § 5 quaterdecies reprochait à la séparation par continuité, sous une
autre forme.

**ET MON CRITÈRE ÉTAIT MAL CHOISI, ce qui se dit.** L'attendu du § 12.10
demandait « aucune voix concentrée à 60 % dans un seul palier ». Il est tenu
(18 %, 15 %, 14 %, 9 %) — mais il ne POUVAIT pas ne pas l'être : les cinq paliers
couvrent **25 % du morceau**, si bien qu'aucune voix jouant tout du long ne peut
y loger 60 % de ses notes. Ce sont les DURÉES et les paliers touchés, mesurés
après coup, qui répondent vraiment. Un critère qui ne peut pas échouer ne mesure
rien : celui-ci est remplacé par ceux-là, et l'erreur reste écrite.

> **H30 — IL FAUT UN DÉCOUPEUR DE TEMPS, pas seulement une porte (écrite ici,
> non mesurée).** Les paliers disent OÙ les timbres s'installent ; le découpage
> devrait suivre ces frontières au lieu de couper des registres. Attendu, quand
> il sera écrit : sur `other` de *Children*, des voix dont chacune se concentre
> dans SES paliers (part dominante ≥ 60 % de sa durée utile, mesurée sur la
> couverture réelle et non sur le morceau entier), et un compte de parties tenues
> seules qui passe de 3 à au moins 5 (§ 12.4). **Réfutée si** les voix obtenues
> couvrent encore le morceau entier, ou si la distance du morceau reconstruit
> s'en trouve dégradée de plus de 10 % — la parité prime sur la ressemblance
> (§ 0), mais pas à ce prix-là.

## 5. Critères d'acceptation

```
[x] La densité (polyphonie, ambitus) et le partage d'énergie de chaque stem
    sont publiés au rapport et criés au journal quand ils dépassent les seuils
[x] Le découpage en voix existe, gardé par le seuil du fourre-tout, choisi
    par une mesure sur données réelles, testé (6 tests d'algorithme)
[x] La séparation six sources est praticable sans tuer la machine (étape à
    part), et son partage est mesuré
[x] H22b et H23 sont tranchées, verdicts écrits avec leurs chiffres
    (§ 3, et ROADMAP-fusion) : six sources −10,4 % et deux pistes de plus ;
    voix par registres +9,1 % et zéro fourre-tout restant
[x] La chaîne a un comportement PAR DÉFAUT décidé, écrit ET câblé (§ 4.2) :
    --modele vaut htdemucs_6s (gardé par un test), la séparation vit en
    sous-processus qui meurt (4 tests de plomberie), et le nouveau chemin a
    été éprouvé en vrai — les six stems rendus sont IDENTIQUES au bit près
    aux stems de référence (sha256, déterminisme shifts=0)
[x] Le DAW montre les densités et avertissements du rapport (§ 4.3) :
    Fichier ▸ Voir le rapport de reconstruction (grisé sans rapport), et
    VSM_RAPPORT=1 pour le photographier sans souris — vérifié sur le
    rapport réel de H22a. Complété le 03/09/2026 : le verdict du mélange
    (« meilleur sans cette piste », machine gardée au mélange), la
    réverbération cherchée au mélange (retenue ou refusée), et l'original
    chargé avec le projet pour l'écoute A/B (provenance.source, sinon le
    canal gauche de comparaison.wav) — vus à l'écran sur l'épreuve et sur
    usandthem-parite
[x] Un projet multipiste reconstruit a été OUVERT et REGARDÉ dans le DAW :
    usandthem-h22b, six pistes, rapport à l'écran ET arrangement — lequel
    était VIDE : la chaîne n'écrit pas de clips, la vue ne dessine que les
    clips, et le scheduler seul connaissait la fenêtre implicite. Corrigé à
    l'ouverture (le clip « tout à zéro » est exactement le passage que le
    scheduler fabriquait), vu la voix dessiner sa forme d'onde
[x] Vérifié sur un deuxième morceau avant de changer un défaut : deux
    seconds originaux (*Sky and Sand*, *Clair de Lune*), qui ont borné le
    gain des six sources et fait naître `--seuil-stem`
[x] Sur un original dont les parties sont connues, le compte de pistes
    reconstruites atteint le compte de parties (l'objectif de parité du § 0).
    ATTEINT LE 03/09/2026 : sur le morceau à vérité écrite (§ 6 bis, outil
    `analyse/epreuve_parite.py`), **9 parties donnent 9 pistes**, à 0,1776
    contre 0,2197 sans découpage. La première mesure (6 pistes, § 6) accusait
    la transcription et le nombre de voix de la machine ; la cause réelle
    était le nombre de voix IMPOSÉ par le partage en registres — corrigé par
    les registres lus dans les vides (`--voix-par-vides`). Historique de la
    première mesure :

      | Partie réelle | Attendu | Obtenu (§ 6) | Obtenu (§ 6 bis) |
      |---|---|---|---|
      | basse | 1 | 1 | 1 |
      | `other` : grave, médium, aigu | 3 | 1 | **3** |
      | batterie : kick, caisse, charleston | 3 | 2 | **3** |
      | voix : tête, doublage | 2 | 2 | 2 |
```
