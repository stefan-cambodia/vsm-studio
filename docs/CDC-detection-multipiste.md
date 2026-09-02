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
   - **`--voix-par-stem` reste une option, PAS le défaut** : la règle écrite
     avant la mesure disait « si la distance se dégrade de plus de 5 %, le
     compromis se dit et se laisse à l'utilisateur » — mesuré +9,1 %. Le
     § 0 ne s'y oppose pas : ici la structure est déjà servie par les six
     stems, et le découpage vaut pour qui veut la parité au prix dit.
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
   d'essai attendent une écoute.
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

   Reste due, à la levée de la pause : la mesure de DISTANCE sur ce second
   morceau (le partage seul ne dit pas si le morceau sonne plus près).

## 6. `--parite` : le raccourci, et une épreuve de bout en bout

Trois découpages mènent à la parité — voix par registres, batterie par pièce,
tête et chœurs — et il faut **les trois**. Personne ne devrait avoir à les
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

## 5. Critères d'acceptation

```
[x] La densité (polyphonie, ambitus) et le partage d'énergie de chaque stem
    sont publiés au rapport et criés au journal quand ils dépassent les seuils
[x] Le découpage en voix existe, gardé par le seuil du fourre-tout, choisi
    par une mesure sur données réelles, testé (6 tests d'algorithme)
[x] La séparation six sources est praticable sans tuer la machine (étape à
    part), et son partage est mesuré
[ ] H22b et H23 sont tranchées, verdicts écrits avec leurs chiffres
[x] La chaîne a un comportement PAR DÉFAUT décidé, écrit ET câblé (§ 4.2) :
    --modele vaut htdemucs_6s (gardé par un test), la séparation vit en
    sous-processus qui meurt (4 tests de plomberie), et le nouveau chemin a
    été éprouvé en vrai — les six stems rendus sont IDENTIQUES au bit près
    aux stems de référence (sha256, déterminisme shifts=0)
[x] Le DAW montre les densités et avertissements du rapport (§ 4.3) :
    Fichier ▸ Voir le rapport de reconstruction (grisé sans rapport), et
    VSM_RAPPORT=1 pour le photographier sans souris — vérifié sur le
    rapport réel de H22a
[x] Un projet multipiste reconstruit a été OUVERT et REGARDÉ dans le DAW :
    usandthem-h22b, six pistes, rapport à l'écran ET arrangement — lequel
    était VIDE : la chaîne n'écrit pas de clips, la vue ne dessine que les
    clips, et le scheduler seul connaissait la fenêtre implicite. Corrigé à
    l'ouverture (le clip « tout à zéro » est exactement le passage que le
    scheduler fabriquait), vu la voix dessiner sa forme d'onde
[x] Vérifié sur un deuxième morceau avant de changer un défaut : deux
    seconds originaux (*Sky and Sand*, *Clair de Lune*), qui ont borné le
    gain des six sources et fait naître `--seuil-stem`
[ ] Sur un original dont les parties sont connues, le compte de pistes
    reconstruites atteint le compte de parties (l'objectif de parité du § 0)
```
