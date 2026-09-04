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
3. **Le DAW montre ce que la chaîne sait** : les densités, le partage et les
   avertissements de fourre-tout existent dans `rapport.json` et nulle part
   dans l'application. Un projet reconstruit qui s'ouvre devrait dire « cette
   piste porte plusieurs parties » là où on la voit — forme à concevoir
   (colonne de la liste de pistes ? panneau du rapport de reconstruction,
   comme le rapport d'import ?).
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

## 12. Le verdict jugeait un morceau SANS LA VOIX — trouvé le 04/09/2026 à 16:05, en cherchant « l'aval »

**Comment c'est apparu.** Le § 11 se demandait où naissait l'écart entre le
dernier réglage au mélange de m9 (0,2307) et sa distance finale (0,2441).
Une expérience sans course, sur les fichiers publiés de sky-parite-m9,
rendus par le moteur du jour (64 machines) :

| Rendu du projet final de m9 | Distance v2 |
|---|---|
| tel quel (moteur à 63, `reconstruit.wav` publié) | 0,244108 |
| tel quel, moteur à 64 machines | 0,244108 — le moteur ne change rien |
| sans le groupe « Batterie » | 0,244108 — le groupe ne change rien |
| **sans la piste audio « Voix »** | **0,230693 — le chiffre exact du dernier réglage au mélange** |

**La cause.** `_copy_samples` (verdict et réglage au mélange) recopie dans
le dossier de variante les fichiers des pistes de SAMPLER
(`track.samples`) ; son propre commentaire raconte la première fois où le
verdict s'est prononcé sur un mélange sans la voix, quand elle était un
report vocal. La parité (§ 6) a fait de la voix une piste AUDIO
(`audio_path`, `vocal_audio_track`), que cette boucle ne voit pas ; le
mini-projet écrit ailleurs ne trouve pas `samples/voix.wav`, `vsm-render`
ne s'en plaint que sur une sortie d'erreur que `capture_output` avale, et
la piste est muette dans CHAQUE rendu du verdict et du réglage. Le
calage des niveaux (`vsm_levels`) n'est pas touché : il saute les pistes
sans machine.

**Ce que cela invalide, et ce que cela ne change pas.** Toutes les
campagnes en parité (2 à 7) ont choisi les machines, les patchs et les
volumes des pistes mélodiques et de la batterie en jugeant un morceau
privé de son stem le plus présent : les comparaisons ENTRE candidates
restent des comparaisons (même absence des deux côtés), mais les volumes
et les réglages ont été poussés à remplir le vide de la voix, et le
témoin de coupure (« le morceau est MEILLEUR sans cette piste ») était
mesuré sans elle. Les distances FINALES publiées, elles, sont justes :
`rendre_et_mesurer` rend le projet écrit, voix comprise. La correction
recopie aussi `audio_path` ; `--verdict-sans-audio` en est le témoin, même
code, et la provenance l'inscrit (`verdictAvecAudio`). Un test verrouille
les deux (`test_verdict_piste_audio.py`).

**Campagne 8, attendu écrit AVANT (04/09/2026, 16:15).** Une course sur
*Sky and Sand*, `sky-parite-m9-voix`, mêmes options que sky-parite-m9-v2
(témoin de la campagne 7, même code à l'option près, qui reproduit
l'ancien chemin octet pour octet), avec la voix dans le verdict.

| Ce qu'on attend | Chiffre |
|---|---|
| distances au verdict et au réglage | de l'ordre de 0,244 et non 0,23 : elles sont désormais comparables à la finale — l'écart « en aval » du § 11 disparaît (moins de 0,5 % entre le dernier réglage et la finale, contre +5,8 % sur m9) |
| distance finale | entre −4 % et −1 % de m9-v2 : les volumes et réglages de `bass`, `other` et des trois pièces de batterie sont choisis contre le vrai morceau. Au-delà de −4 % je me serai trompé sur le mécanisme dans le bon sens ; entre −1 % et +0,5 %, la présence de la voix ne changeait pas les choix (à publier tel quel) ; au-delà de +0,5 %, la correction fait pire et il faudra comprendre pourquoi avant de la garder |
| le témoin de coupure du hihat | « meilleur sans » disparaît ou s'inverse : 0,2431 contre 0,2486 était mesuré sans la voix |
| durée | celle de m9-v2, ± 10 % : rien de plus à rendre, un fichier de plus à lire par rendu |

**Et un second morceau, le même jour (16:20).** `usandthem-parite-voix`,
mêmes options que `usandthem-parite-parc60` (0,190043, campagne 5, le
dernier *Us and Them* en parité ; six finalistes, huit rendus). Le v15
(−5,0 %, neuf finalistes) n'était PAS en parité : sa voix était un report
vocal, donc recopiée, donc entendue par son verdict — ce qui explique
peut-être qu'il ait gagné là où le m9 de *Sky* perdait. Attendu : entre
−4 % et −1 % de parc60, pour les mêmes raisons ; et le jalon du verdict
(nouveau, publié sous `verdictJalon`) à moins de 0,5 % de la finale sur
les deux morceaux. Le script `campagne-8.sh` enchaîne les deux courses,
*Sky* d'abord.

**Décision écrite d'avance.** La correction reste quoi qu'il arrive : un
verdict qui juge un autre morceau que celui qu'on rend n'est pas une
option, et le chiffre de la campagne dit seulement ce qu'elle valait. Si
le gain est ≥ 1 %, les campagnes 5 à 7 sont à relire à cette lumière avant
d'en tirer autre chose que ce qu'elles disent déjà (le parc ne coûte
rien ; neuf finalistes sont une constante du morceau — deux conclusions
qui reposent sur des comparaisons entre courses toutes privées de voix, et
qui tiennent donc).

### En attente de la fin des campagnes (03/09/2026)

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
