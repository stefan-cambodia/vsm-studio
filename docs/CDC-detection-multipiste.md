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
4. **La batterie éclatée par pièce** — le chemin le plus mûr vers la parité
   après les § 4.1-4.2 : la chaîne SAIT déjà séparer les frappes par pièce
   (sur *Us and Them* : kick 925, kick2 943, hihat 414) et n'en rend qu'UNE
   piste. Un batteur mixe huit pistes ; rendre une piste par pièce détectée
   est un découpage sans invention — les frappes sont déjà classées.
5. **La voix** : `vocals` (22,7 %) part d'un bloc au sampler — un chœur et
   une voix de tête restent une piste. Non couvert, dit, pas nié ; c'est le
   morceau le plus dur (séparer des voix DANS un stem de voix demande un
   modèle qu'on n'a pas).
6. **Le DAW à l'échelle de la parité** : l'application est vérifiée à huit
   pistes ; l'objectif en promet soixante-quatre. La liste de pistes, la
   console, l'arrangement et le rendu doivent être REGARDÉS et mesurés à 64
   pistes (fluidité, défilement, lisibilité à l'échelle 150 %) — un projet
   d'essai à 64 pistes se fabrique par l'import FLP, sans attendre la chaîne.
7. **Un deuxième morceau** : tout ce chantier est mesuré sur *Us and Them*.
   Avant de changer un défaut de la chaîne, le vérifier sur un second
   original (les MIDI de référence de ~/Téléchargements peuvent servir de
   cibles à contenu connu — voir CDC-multisample § 1, la question ouverte de
   la cible à MIDI exact).

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
[ ] Vérifié sur un deuxième morceau avant de changer un défaut
[ ] Sur un original dont les parties sont connues, le compte de pistes
    reconstruites atteint le compte de parties (l'objectif de parité du § 0)
```
