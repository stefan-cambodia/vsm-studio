# Le banc synthétique — cahier des charges

Chantier ouvert le 04/09/2026. Il répond à une question que la chaîne ne
savait pas poser : **où perd-elle ?**

## 0. La question, et la réponse courte

**La question.** La chaîne publie une distance globale (`rapport.json`,
`globalDistance`) et une distance par piste. Elle ne sait pas dire à quel
ÉTAGE l'écart se creuse : la séparation (le stem n'est pas la partie), la
transcription (les notes ne sont pas celles jouées), la parité (deux parties
fondues, une partie coupée en deux), l'arbitrage (la mauvaise machine), le
réglage (le mauvais patch). Sur un disque, on ne le saura jamais : on n'a ni
la session, ni le MIDI, ni les instruments — faute de vérité, chaque étage
est jugé contre la sortie de l'étage d'avant, et une erreur amont se
déguise en erreur aval.

**La réponse courte.** Un morceau que le MOTEUR a fabriqué a une vérité
complète par construction : les parties, la machine de chacune, son patch,
chaque note avec sa vélocité et sa durée, les niveaux, les panoramiques, et
les stems VRAIS de chaque partie. Sur un tel morceau, chaque étage se
mesure contre CE qu'il devait produire, et non contre ce que l'étage d'avant
lui a donné. Le banc est cet ensemble : un **générateur** de morceaux
entiers à vérité connue (`analyse/morceaux.py`), et un **tableau de bord
par étage** (`analyse/banc_synthetique.py`) qui fait tourner la chaîne
d'aujourd'hui, sans la modifier, et publie la perte imputable à chaque
étage.

Le banc MESURE la chaîne. Il ne la remplace pas, il ne l'entraîne pas, et il
ne dispense d'aucune validation sur disque (§ 3).

## 1. Ce que la mesure a déjà établi, et que le banc ne rouvre pas

Ce chantier part de quatre résultats écrits, tous chiffrés. Ils bornent ce
qu'on peut attendre d'un morceau généré.

- **ROADMAP-fusion § 7 (l'estimateur de paramètres, A3).** Un corpus de
  rendus moteur apprend à inverser ce que le moteur produit (distance
  médiane 0,1798 contre 0,5208 au hasard) et ne lit pas un disque : sur 9
  cibles réelles, le régime prudent rend 0,1974 contre 0,1974 pour la
  recherche ordinaire — identique, parce que le garde-fou refuse de
  resserrer 8 fois sur 9. **Un stem séparé est un son qu'aucune machine ne
  produit.**
- **ROADMAP-apprentissage A1.3 (le classifieur de machine).** 99,9 % de bonne
  machine dans le top 3 sur ce que le moteur produit ; sur un piano réel il
  annonce `vsm.sh101` à 1,00, et sur un morceau de SYNTHÉS séparé il
  s'abstient 96 à 100 % du temps. Les augmentations synthétiques, mesurées
  puis renforcées (fuite ×4, compression ×3), n'ont pas bougé le rang
  médian d'un cran ; le corpus passé par demucs (`corpus_separe.py`, 7 990
  exemples) apprend le résidu de la séparation, pas le disque (somme des
  rangs médians 45 pour le sec, 54 pour le séparé).
- **ROADMAP-apprentissage A3.4.** Le critère « médiane ≤ chaîne actuelle
  avec ≤ la moitié des évaluations » n'est pas atteint ; A3 est close par
  rejet mesuré, le code reste désactivé et documenté.
- **ROADMAP-fusion H25 (le timbre pour dire un ou deux instruments).**
  Réfutée à la première mesure : un seul piano donne la plus grande
  distance harmonique de la table (0,48), trois instruments distincts la
  plus petite (0,08). Le profil harmonique dépend du registre bien plus que
  de l'instrument. La limite est écrite au CDC multipiste § 6 bis : **un
  instrument dont les registres se séparent par un vide compte pour deux.**

Et une cinquième, qui fonde la méthode : **ROADMAP-fusion § 5 sexies**
écrit la condition de réouverture du dossier `vsm.multisample` — « une cible
dont on CONNAÎT le MIDI exact ; la transcription sortirait alors de
l'équation, et l'on saurait si la machine perd sur le timbre ou sur les
notes ». C'est exactement l'objet que ce banc fabrique, pour toutes les
machines et pas seulement pour celle-là.

**Conséquence.** Le corpus généré a montré trois fois qu'il n'apprend pas ce
qu'un disque contient. Ce banc n'en fait donc pas un corpus d'apprentissage
(sauf le cas circonscrit du § 8, sur une question STRUCTURELLE et non
timbrale). Il s'en sert pour ce qu'un tel corpus sait faire : dire, étage
par étage, ce que la chaîne perd **quand la vérité est connue** — ce qui est
une borne, pas une prédiction de ce qu'elle perd sur un disque.

## 2. Objets

### 2.1 Un morceau généré

Un dossier `morceau-NNNN-gGGG/` (numéro dans le lot, graine) :

```
morceau.wav            le mélange stéréo, float32, 44,1 kHz
verite.json            tout ce qu'on sait par construction (ci-dessous)
stems-vrais/           un WAV stéréo float32 par partie, dans l'ordre de verite.json
   01-basse.wav
   02-batterie.wav
   ...
```

Le morceau : un tempo tiré (84 à 140 bpm), une grille de 4/4, une
progression d'accords tirée parmi des progressions plausibles, une durée
demandée (30 s par défaut), N parties (2 à 12, tiré). Chaque partie :

| champ | contenu |
|---|---|
| `role` | `basse`, `accompagnement`, `melodie`, `nappe`, `batterie`, `piano-deux-mains` |
| `machine` | une machine du parc de recherche (`machines_de_recherche`), mélodique pour les rôles mélodiques, `vsm.drums` / `vsm.tr808` / `vsm.tr909` pour la batterie |
| `patch` | un point tiré uniformément dans le `SearchProfile` déclaré par le moteur (`search_space_for_machine` sur TOUTES ses dimensions, `_vector_to_parameters`), rejeté et retiré s'il rend un son inaudible — le rejet est compté |
| `notes` | `[note, velocite, debut, duree]` en demi-tons MIDI, 1-127, secondes |
| `registre` | `[bas, haut]` MIDI ; deux registres pour `piano-deux-mains` |
| `niveau` | RMS cible du stem (tiré, dit en dB) et gain appliqué |
| `pan` | −1 à +1, loi à puissance constante |
| `pieces` | batterie seulement : les pièces frappées (`kick`, `snare`, `hihat`, `openhat`, `clap`, `tom`) |
| `cas` | le cas de parité que la partie incarne, s'il y en a un (§ 2.2) |

Les notes sont STRUCTURÉES, pas tirées au hasard : une basse suit la
fondamentale et la quinte des accords en croches ou en noires ; un
accompagnement arpège ou plaque les accords ; une mélodie marche par
degrés de la gamme avec des sauts rares, en motifs de deux mesures répétés
avec variation ; une nappe tient l'accord en rondes ; la batterie joue un
motif d'une mesure répété avec des variantes toutes les quatre mesures. Les
vélocités varient (temps forts plus forts, une dispersion tirée par
partie), les durées suivent un gate par rôle.

Le rendu passe par le pont existant (`VsmEngine`, `vsm-render --serve`),
UNE passe par partie, puis le mixage en numpy : chaque stem est calé à son
RMS tiré, panoramiqué, et **le mélange est la somme des stems vrais dans
l'ordre du fichier, en float64, convertie en float32** — la somme des stems
écrits redonne le mélange au bit près (testé). Si le mélange dépasse
0,95 de crête, un gain commun est appliqué à tous les stems AVANT
l'écriture, et il est dit dans la vérité.

`--production` ajoute au mélange, et seulement à lui, une réverbération
courte (réponse impulsionnelle synthétique seedée, 0,6 à 1,2 s, dosée) et
une compression légère (RMS, seuil, ratio 2:1 à 3:1, gain de rattrapage).
La vérité porte alors `production: {...}` avec chaque valeur, et le rapport
du banc rappelle que le mélange n'est PLUS la somme des stems.

`verite.json` porte aussi : la graine, la version du format, le commit, le
tempo, la progression, la durée réelle, l'identité du moteur
(`identite_du_moteur` : chemin, date de compilation, octets, nombre de
machines), les empreintes de rendu par partie (SHA-256 des échantillons),
les patchs rejetés pour inaudibilité, et le **coût** : secondes de rendu par
partie, de mixage, total.

### 2.2 Les cas de parité, inclus volontairement

Trois cas que le CDC multipiste nomme, tirés parmi les parties d'un
morceau et déclarés dans la vérité (`cas`) pour être comptés à part :

- **`memes-machine-disjoints`** : deux parties de la MÊME machine (patchs
  différents) dans des registres disjoints (au moins 8 demi-tons de vide).
  La parité doit en faire deux pistes (le découpage par les vides).
- **`chevauchement`** : deux parties de machines différentes dont les
  registres se recouvrent d'au moins une octave. Aucun découpage par
  hauteur ne peut les séparer ; c'est à la séparation ou aux voix de le
  faire, et l'attendu est qu'elles soient FONDUES.
- **`deux-mains`** : UNE partie `piano-deux-mains` — une machine, un patch,
  deux registres (main gauche 36-52, main droite 60-84) séparés par un
  vide. C'est le cas « chorale » d'H25 : la chaîne la coupe en deux pistes,
  et c'est une limite écrite. Le banc le compte SÉPARÉMENT des autres
  erreurs (§ 2.4, colonne H25).

Par défaut, le cas TOURNE avec la graine (`aucun`, `memes-machine-disjoints`,
`chevauchement`, `deux-mains`, dans l'ordre de graine mod 4) : un lot de
dix graines consécutives voit chaque cas deux ou trois fois. Le premier
lot, tiré au hasard, en avait donné 0, 1, 5 et 4 — un attendu « dans ≥ 2/3
des occurrences » ne se mesure pas sur une occurrence. `--cas` l'impose.

### 2.3 Le lot

`analyse/morceaux.py --sortie dossier --nombre 10 --graine 1 [--duree 30]
[--production] [--cas ...]` fabrique dix morceaux aux graines 1 à 10 (graine
du lot + indice). **Seedé de bout en bout** : même graine → même
`verite.json` (empreintes comprises) et mêmes octets de WAV (testé sur deux
générations). **Interruptible et reprenable** : un morceau dont
`verite.json` est complet est sauté en le disant ; `verite.json` est écrit
en dernier et de façon atomique. Le coût de chaque morceau et du lot est
publié dans `lot.json`.

### 2.4 Le tableau de bord par étage

`analyse/banc_synthetique.py dossier --sortie banc [options de
reconstruire.py]` fait tourner `reconstruire.py` sur chaque `morceau.wav`
(défauts de la chaîne : séparation `htdemucs_6s`, `--parite`, tout le
parc), en gardant les stems séparés (`--garder-stems`), puis calcule et
publie, par morceau et agrégé (médiane et moyenne sur le lot) :

1. **Séparation.** Pour chaque stem séparé, la cible est la somme des stems
   vrais qu'il DEVRAIT porter : `bass` ← les parties `basse` ; `drums` ← la
   `batterie` ; `other` ← tout le reste. Publiés : corrélation et SDR
   (10·log10 de l'énergie de la cible sur celle du résidu, en mono, sur la
   durée commune). Les stems à six sources (`guitar`, `piano`, `vocals`)
   n'ont AUCUNE partie attendue sur un morceau généré : leur énergie est
   publiée comme **hallucinée** (part de l'énergie totale séparée), et un
   second couple corrélation/SDR est donné pour `other + guitar + piano`
   contre la même cible, puisque c'est là que la chaîne retrouve les
   claviers. Quand le banc reçoit les stems vrais (`--stems-vrais` : la
   chaîne saute la séparation et reçoit `bass`, `drums`, `other` sommés
   depuis la vérité), l'étage est marqué « non mesuré ».
2. **Transcription.** Les notes vraies des parties mélodiques contre les
   notes transcrites de toutes les pistes mélodiques (lues dans
   `midi/arrangement.mid`, en secondes par le tempo du fichier).
   Appariement glouton : même hauteur à ±1 demi-ton et attaque à ±50 ms,
   au plus proche. Publiés : précision, rappel, F1 ; les mêmes à hauteur
   EXACTE ; erreur absolue moyenne de vélocité sur les paires ; erreur
   médiane de durée (absolue et relative). Les frappes de batterie sont
   comptées à part, par attaque à ±50 ms, sans hauteur. C'est le chiffre
   qui manque depuis A5.
3. **Parité.** Chaque piste obtenue (hors bus de groupe) est attribuée à la
   partie vraie dont elle porte le plus de notes appariées ; les parties
   `batterie` comptent pour autant de parties que de pièces frappées
   (convention du CDC multipiste § 6 bis, qui compte kick, caisse,
   charleston), et les pistes `Batterie · …` leur répondent. Puis :
   - **fondues** : parties qui n'ont AUCUNE piste (leurs notes sont sur la
     piste d'une autre) ; pour la batterie, les pièces au-delà du nombre de
     pistes `Batterie · …` ;
   - **inventées** : pistes au-delà de la première pour une même partie
     (une partie coupée en deux), ET pistes qui n'ont reçu aucune note
     appariée ;
   - **H25** : les pistes inventées d'une partie `deux-mains` sont comptées
     dans cette colonne et PAS dans « inventées » ;
   - les comptes bruts : parties vraies, pistes obtenues, écart.
   Le cas déclaré de chaque morceau reçoit son verdict nommé (séparé /
   fondu).
4. **Arbitrage.** Pour chaque piste mélodique attribuée à une partie : le
   RANG de la vraie machine dans `trackArbitration` (ou « absente » si elle
   n'a pas concouru), et la **borne de piste** — la distance, à la métrique
   de la course, du rendu de la VRAIE machine avec son VRAI patch sur les
   notes TRANSCRITES de cette piste, contre la cible que la chaîne a jugée
   (le stem séparé, entier). `trackDistance − borne` est la perte imputable
   à l'arbitrage et au réglage sur cette piste ; une borne PLUS GRANDE que
   la distance de la chaîne dit que le stem séparé ressemble davantage à
   une autre machine qu'à la vraie — c'est le fossé de domaine du § 1,
   mesuré piste par piste.
5. **Distance globale et l'écart à la borne.** Trois nombres à la métrique
   de la course, contre `morceau.wav` :
   - `globalDistance` de la chaîne ;
   - **borne de transcription** : le mélange des pistes de la chaîne,
     chacune rendue par la vraie machine et le vrai patch de sa partie, au
     GAIN et au panoramique vrais de cette partie (le gain que le
     générateur a appliqué au rendu brut : avec les notes exactes, ce
     mélange EST la somme des stems vrais ; avec des notes manquantes il en
     manque la part, et caler au RMS aurait masqué le manque). C'est ce que
     la chaîne atteindrait avec un arbitrage, un réglage et un calage
     parfaits, à transcription et parité égales. Les pistes de batterie
     sont sommées en une seule entrée, jugée contre le stem entier comme la
     chaîne l'a fait ;
   - **borne de production** : le mélange des stems vrais, secs, contre le
     mélange produit. Sans `--production`, ce nombre est 0 (le mélange EST
     cette somme). Avec, c'est ce que coûte de rendre sec contre un
     original réverbéré et compressé (H24).
   La perte de chaque étage se lit dans les différences : `borne de
   transcription − borne de production` = transcription + parité ;
   `globalDistance − borne de transcription` = arbitrage + réglage +
   calage ; la séparation se lit à l'étage 1 et dans les bornes de piste.

   **Ce que « borne » veut dire, et ne veut pas dire.** La borne de
   transcription est une borne À CALAGE VRAI : elle rend les notes
   transcrites au gain de la vérité, sans rien compenser. La chaîne, elle,
   cale ses niveaux et règle ses patchs CONTRE le stem, et peut donc passer
   SOUS cette borne en rattrapant par le volume ou le timbre ce que la
   transcription a perdu (une pièce de batterie manquante, remontée par le
   calage du kit). Un écart négatif n'est pas une erreur du banc : c'est la
   mesure de cette compensation, et il se publie tel quel — c'est le
   mécanisme que ROADMAP-fusion § 5 sexies décrivait sur *Clair de Lune*
   (« la reconstruction réussissait en corrigeant en continu un timbre
   qu'elle n'avait pas su choisir »). Sur le morceau minuscule du dépôt,
   c'est déjà le cas : F1 de transcription 1,00 mais 2 pièces de batterie
   sur 5, borne 0,336, chaîne 0,234.

Tout va dans `banc/rapport.json` (format `vsm-banc-synthetique`, version,
provenance : commit, options de la chaîne mot pour mot, identité du moteur,
dossier des morceaux, liste des morceaux avec leur graine) et dans un
tableau lisible `banc/tableau.txt`, imprimé aussi au terminal. Une piste ou
un morceau qui n'a pas pu être mesuré est DIT, avec sa raison, jamais omis.
Le banc est **reprenable** : une course dont `rapport.json` existe n'est
pas rejouée.

## 3. Anti-objectifs

1. **Le banc ne remplace pas la validation sur disques réels.** Un morceau
   généré est un son que le parc sait produire, et un disque n'en est pas
   un (§ 1). Un gain mesuré au banc est une hypothèse pour un disque, pas
   un résultat : toute modification de la chaîne motivée par le banc se
   mesure ENSUITE en A/B sur un morceau réel, aux règles de
   ROADMAP-fusion, avant d'être déclarée.
2. **Il ne rouvre ni A1 ni A3.** Le corpus généré a montré trois fois qu'il
   n'apprend pas ce qu'un disque contient. Le banc ne produit ni
   classifieur de machine ni estimateur de patch, et aucun modèle appris
   sur ses morceaux n'entre dans la chaîne — à la seule exception du § 8,
   qui porte sur une question structurelle, est écrite d'avance et reste
   désactivée si elle échoue.
3. **Pas une ligne hors de `analyse/`.** Ni `core/`, ni `audio/`, ni
   `interchange/`, ni `app/` : une campagne en cours crierait « moteur
   périmé » et deux mesures deviendraient incomparables.
4. **Le banc ne règle pas la chaîne.** Ses chiffres décrivent ; les seuils,
   budgets et défauts de `reconstruire.py` ne changent pas parce que le
   banc préférerait d'autres valeurs — ce serait apprendre le banc.
5. **La chaîne mesurée est la chaîne d'aujourd'hui.** Le banc appelle
   `reconstruire.py` par son interface publique, avec ses défauts, et ne
   lui passe que des options existantes ; sans banc, la chaîne est
   inchangée à l'octet près.

## 4. Critères d'acceptation

```
[x] Générateur seedé : même graine → même verite.json et mêmes octets de WAV,
    testé sur deux générations successives (test_banc_synthetique.py,
    04/09/2026 : 2 parties, 1 mesure, vérité, stems, mélange et FICHIERS
    identiques au bit près — après avoir remplacé l'écrivain WAV de
    libsndfile, dont le bloc PEAK horodate le fichier)
[x] Cohérence vérité/rendu : la somme des stems vrais, dans l'ordre du
    fichier, redonne morceau.wav au bit près hors --production (testé) ;
    avec --production la vérité le dit et le test vérifie que ce n'est PLUS
    le cas, et que les stems, eux, n'ont pas bougé
[x] Les notes de verite.json sont celles rendues : nombre, registre par rôle,
    vélocités qui varient, frappes sur des voix que la boîte possède (testé
    sur un morceau à 6 parties)
[x] Les trois cas de parité se tirent et se déclarent ; --cas les impose
    (testé : deux-mains → une partie à deux registres séparés d'un vide ;
    memes-machine-disjoints → deux parties de même machine, patchs différents,
    vide ≥ 8 ; chevauchement → deux registres qui se recouvrent d'une octave)
[x] Un patch inaudible est rejeté, retiré, et compté dans la vérité (testé
    par un rendu factice muet une fois : patchs_rejetes = 1) ; une MACHINE
    muette sur la note du rôle est écartée en le disant (vsm.fmdrums,
    vsm.perc, constaté au premier lot) et une autre est tirée
[x] Interruptible et reprenable : un morceau complet est sauté en le disant
    (morceaux.py ; morceau_complet testé sur un dossier sans vérité, sur une
    vérité sans fichiers, et sur le morceau minuscule)
[x] Coût mesuré et publié : secondes par partie et total dans verite.json,
    par morceau et total dans lot.json (mesuré : 0,2 à 1,8 s par morceau de
    8 s, 5 s pour un lot de quatre)
[x] Tableau de bord : sur le morceau minuscule COMMIS avec sa course
    (analyse/tests/donnees/banc-minuscule/, 2 s, basse TB-303, TR-909,
    mélodie TB-303), les cinq étages se calculent et les chiffres attendus
    sont vérifiés — F1 1,00, 9 notes vraies, 4 pistes pour 7 parties, 3
    pièces fondues, rangs 2/3 et 1/3, borne de production 0, borne de
    transcription > 0 (testé, sans moteur pour 1-3, avec moteur pour 4-5)
[x] Le tableau dit ce qu'il n'a pas pu mesurer (stems vrais fournis →
    « séparation non mesurée », testé ; piste sans partie → nommée)
[x] Zéro ligne hors de analyse/ et docs/ (git diff --stat des trois commits)
[x] Suites vertes, zéro warning : 150 tests d'analyse (04/09/2026)
[x] README, MODE-EMPLOI et ROADMAP-fusion nomment le chantier
[x] Campagne S1 écrite AVANT sa mesure (§ 5)
[ ] Verdict de S1 écrit APRÈS avec les chiffres (§ 6)
```

## 5. Campagne S1 — attendus écrits AVANT la mesure (04/09/2026, 18:05)

**Ce qui est mesuré.** Dix morceaux de 30 s sans production (graines 1 à
10, lot `s1-sec`), puis les MÊMES dix graines avec `--production` (lot
`s1-prod`) : la seule variable entre les deux lots est la production. La
chaîne aux défauts du jour (`htdemucs_6s`, `--parite`, tout le parc,
`--budget-piste 40 --axes-piste 8 --tours-verdict 3`), `--rendus-paralleles
6`. Le script `reconstruction/travail/banc-s1.sh` (hors dépôt, comme
campagne-7.sh) attend la fin de la campagne 7 dans le journal : une
seule course lourde à la fois.

**Les lots, fabriqués avant le départ (19:30).** Graines 1 à 10, cas en
rotation : `memes-machine-disjoints` ×3 (graines 1, 5, 9), `chevauchement`
×3 (2, 6, 10), `deux-mains` ×2 (3, 7), `aucun` ×2 (4, 8) ; 3 à 12 parties,
30,0 à 31,5 s, 108 à 137 bpm ; 34 s pour le lot sec, 41 s pour le lot
produit. Les stems vrais de `s1-prod` sont identiques à ceux de `s1-sec`
(mêmes graines) : seul le mélange diffère.

**Le défaut a changé avant le départ (19:54).** La campagne 7 (CDC
multipiste § 11, verdict) fait de `--second-verdict 1` le défaut de la
chaîne : S1, démarrée à 19:51 sur l'ancien défaut, a été arrêtée après
trois minutes et relancée à 19:54 sur le nouveau — c'est « la chaîne d'aujourd'hui »
que le banc mesure. Le coût attendu ci-dessous grandit d'un tiers ; les
attendus, eux, ne changent pas.

**Coût attendu.** Génération ≤ 30 s par morceau (≤ 10 min le lot des
vingt). Course ≤ 15 min par morceau de 30 s (l'épreuve de parité à 3
candidates dure 80 à 250 s ; à 58 candidates mélodiques et budget 40,
l'arbitrage domine) — soit ≤ 5 h pour les vingt. Si un morceau dépasse 30
min, le banc le dit et continue.

**Mes attendus, chiffrés.** Les médianes sont sur le lot sec ; la dernière
ligne dit ce que la production doit changer.

| étage | attendu (médiane, lot sec) | ce qui le réfuterait |
|---|---|---|
| 1. séparation | `bass` SDR ≥ 6 dB, corrélation ≥ 0,85 ; `drums` SDR ≥ 8 dB ; `other` seul SDR entre 3 et 8 dB ; `other+guitar+piano` ≥ 6 dB ; énergie hallucinée (guitar+piano+vocals) entre 5 et 25 % | `drums` sous 5 dB (la séparation ne reconnaît pas une boîte à rythmes de synthèse) ; hallucination > 40 % |
| 2. transcription | F1 à ±1 demi-ton, ±50 ms : entre 0,50 et 0,70 ; rappel > précision ; F1 à hauteur exacte inférieur d'au moins 0,10 ; vélocité : erreur absolue moyenne ≥ 20 (la chaîne n'écrit pas une vélocité mesurée) ; durée : erreur relative médiane ≥ 30 % ; par rôle : basse > mélodie > accompagnement > nappe | F1 > 0,80 (la transcription ne serait pas le premier poste) ; vélocité < 10 |
| 3. parité | écart ≤ 1 piste (H25 compté à part) dans ≥ 6/10 ; `memes-machine-disjoints` séparé (2 pistes) dans ≥ 2/3 des occurrences ; `chevauchement` FONDU dans ≥ 2/3 ; `deux-mains` coupé en deux (colonne H25) dans ≥ 2/3 ; fondues ≥ 1 par morceau dès 6 parties ; batterie : autant de pistes que de pièces dans ≥ 5/10 | les registres disjoints fondus plus d'une fois sur deux ; le deux-mains laissé entier plus d'une fois sur deux (H25 aurait tort d'être une limite) |
| 4. arbitrage | vraie machine dans le top 6 pour ≥ 50 % des pistes mélodiques attribuées (hasard à 58 machines : 10 %) ; rang 1 pour ≥ 20 % ; borne de piste PLUS GRANDE que `trackDistance` pour ≥ 50 % des pistes (le stem séparé ressemble à autre chose que la vérité) | top 6 < 25 % (l'arbitrage ne reconnaîtrait même pas un son du parc) ; borne toujours plus petite (le réglage perdrait contre la vérité partout) |
| 5. global | `globalDistance` v2 entre 0,15 et 0,30 ; borne de transcription entre 0,08 et 0,18 ; **transcription + parité (borne − 0) > arbitrage + réglage (global − borne) dans ≥ 6/10** — la transcription est le premier poste (A5) | l'écart arbitrage > transcription dans ≥ 6/10 |
| production | `globalDistance` +8 à +25 % ; borne de production entre 0,04 et 0,12 ; séparation : SDR −1 à −4 dB ; transcription : F1 −0,00 à −0,08 ; parité : même compte à ±1 sur ≥ 7/10 ; arbitrage : top 6 −0 à −15 points | global inchangé à ±3 % (la production ne coûterait rien, contre H24) ; F1 −0,20 (la réverbération casserait la transcription) |

**Ce que je ne sais pas prédire, et que je publie sans attendu** : la part
de l'énergie de `other` que `htdemucs_6s` range dans `piano` (une nappe de
synthé peut y passer entière), et l'erreur de durée de la batterie (une
frappe de boîte à rythmes n'a pas de durée transcrite).

**Ce que le résultat décidera.** Si la transcription est le premier poste,
le chantier suivant est A5-bis : mesurer une transcription de rechange sur
CE banc avant d'y toucher. Si l'arbitrage l'est, c'est le fossé de domaine
qui domine et le banc le dira piste par piste. Si la parité l'est, le § 8
s'ouvre. Le § 8 ne s'ouvre QUE si le cas `deux-mains` est coupé en deux dans
au moins 2/3 des occurrences ET si les registres disjoints sont bien
séparés : un modèle qui corrigerait un cas rare au prix d'un cas fréquent
n'a pas d'objet.

## 6. Campagne S1 — verdict

### Verdict complet (05/09/2026, 02:23) — vingt morceaux, aucun non mesuré

`s1-sec` 22:41 → 00:24, `s1-prod` 00:24 → 02:23. Dix morceaux chacun, zéro
non mesuré, code 0 aux deux courses. Les attendus du § 5 étaient écrits le
04/09 à 18:05, avant la fabrication des lots ; ils sont confrontés un par un
ci-dessous.

#### Ce que le lot SEC a tranché

**La chaîne est plus faible que prévu à tous les étages, et le banc a donc
fait exactement ce pour quoi il a été écrit.**

| étage | attendu (§ 5) | mesuré | verdict |
|---|---|---|---|
| séparation | `bass` SDR ≥ 6 dB, corrélation ≥ 0,85 | **0,21 dB**, corrélation **0,26** | RATÉ, et de très loin |
| | `drums` SDR ≥ 8 dB (< 5 dB réfuterait) | **5,07 dB** | raté ; à 0,07 dB de la réfutation |
| | `other` seul entre 3 et 8 dB | **2,75 dB** | raté de peu |
| | `other+guitar+piano` ≥ 6 dB | **3,69 dB** | raté |
| | énergie hallucinée entre 5 et 25 % | **19,8 %** | **tenu** |
| transcription | F1 (±1 demi-ton, ±50 ms) entre 0,50 et 0,70 | **0,367** | raté |
| | rappel > précision | rappel **0,345** < précision **0,426** | RATÉ, et dans l'autre sens |
| | F1 hauteur exacte inférieur d'au moins 0,10 | **0,352**, soit −0,015 | raté |
| | vélocité : erreur absolue ≥ 20 | **20,5** | tenu |
| | durée : erreur relative médiane ≥ 30 % | **30,5 %** | tenu |
| parité | écart ≤ 1 piste dans ≥ 6/10 | **2/10**, écart médian **−2** | raté |
| | `memes-machine-disjoints` séparé dans ≥ 2/3 | 0/3 (fondu les trois fois) | RATÉ, et c'est la réfutation nommée |
| | `chevauchement` fondu dans ≥ 2/3 | 3/3 | tenu |
| | `deux-mains` coupé en deux dans ≥ 2/3 | 1/2 | à la limite |
| arbitrage | vraie machine dans le top 6 pour ≥ 50 % | **25,6 %** (11 pistes sur 43) | raté ; à 0,6 point de la réfutation |
| | rang 1 pour ≥ 20 % | **14,0 %** (6 sur 43) | raté |
| | borne de piste plus grande que la chaîne pour ≥ 50 % | **42 sur 43** | tenu, et largement |
| global | `globalDistance` entre 0,15 et 0,30 | médiane **0,186** | **tenu** |
| | borne de transcription entre 0,08 et 0,18 | **0,307** | raté (deux fois trop grande) |
| | transcription + parité > arbitrage + réglage dans ≥ 6/10 | **10/10** | **tenu, et sans appel** |

Perte médiane imputable à la transcription et à la parité : **0,307**. À
l'arbitrage et au réglage : **−0,146**. Non seulement la transcription est le
premier poste dans 10 morceaux sur 10 (A5 confirmé sans ambiguïté), mais
l'arbitrage et le réglage font MIEUX que la vérité de référence — la borne est
plus grande que la distance obtenue dans 42 pistes sur 43. Régler une machine
du parc pour ressembler au stem SÉPARÉ marche mieux que viser le stem vrai,
parce que le stem séparé n'est pas le stem vrai. **C'est la séparation qui
plafonne tout le reste.**

Deux attendus étaient MAL CALIBRÉS plutôt que réfutés, et il faut le dire :
« rappel > précision » supposait une transcription qui invente plus qu'elle
n'oublie — c'est l'inverse ; et la borne de transcription attendue entre 0,08
et 0,18 était une prévision faite sur des morceaux réels, pas sur des
synthèses à douze parties.

#### Ce que la PRODUCTION a tranché, et c'est une surprise

**La production coûte beaucoup MOINS que prévu, et à deux endroits elle
AIDE.** C'était la seule ligne du § 5 qui comparait les deux lots ; elle est
la plus intéressante des vingt.

| attendu (§ 5) | mesuré | verdict |
|---|---|---|
| `globalDistance` **+8 à +25 %** | **+7,1 %** (médiane 0,1859 → 0,1991 ; moyenne +4,7 %) | raté de peu, et par le bas |
| borne de production entre 0,04 et 0,12 | **0,036** | raté de peu, par le bas |
| séparation : SDR **−1 à −4 dB** | `bass` **−0,16**, `drums` **−0,75**, `other` **−0,48**, `other` élargi **−0,83** | RATÉ : quatre fois moins de dégât qu'annoncé |
| transcription : F1 **−0,00 à −0,08** | **−0,043** (0,367 → 0,323) | **tenu** |
| parité : même compte à ±1 sur **≥ 7/10** | **8/10**, et le verdict de cas identique **10/10** | **tenu** |
| arbitrage : top 6 **−0 à −15 points** | **+2,3 points** (25,6 % → 27,9 %) | RATÉ : la production l'améliore |
| *(réfuterait)* global inchangé à ±3 % | +7,1 % | pas réfuté |
| *(réfuterait)* F1 −0,20 | −0,043 | pas réfuté |

**Pourquoi la production coûte si peu, et ce que cela dit.** Parce qu'il n'y
avait presque plus rien à perdre. `bass` passe de 0,21 dB à 0,05 dB : ce n'est
pas une chute, c'est un plancher qu'on rase de plus près. L'attendu « −1 à
−4 dB » supposait une séparation qui marche et que la réverbération dégrade ;
la mesure dit qu'elle ne marchait déjà pas. **La production n'est pas le
problème de cette chaîne, et H24 — qui la disait coûteuse — est vraie dans son
sens mais négligeable dans son ampleur, tant que la séparation reste où elle
est.**

Deux effets vont même dans l'autre sens, et ils sont écrits ici sans être
expliqués, parce que le banc mesure et ne décide pas :

- **L'énergie hallucinée BAISSE avec la production** : 19,8 % → 14,6 %.
- **L'arbitrage s'améliore** : top 6 de 25,6 % à 27,9 %, et l'écart
  arbitrage/réglage passe de −0,146 à −0,165 (il fait encore mieux que la
  vérité). Une hypothèse plausible, qu'il faudrait une autre campagne pour
  trancher : la production rapproche le stem séparé de ce qu'une machine du
  parc PEUT produire, ce qui est un compliment ambigu.

#### Ce que la campagne établit, en une phrase

Sur vingt morceaux à vérité connue, **le premier poste de perte est la
transcription et la parité (0,307 contre −0,146), dans 20 morceaux sur 20**,
et il est lui-même plafonné par une séparation qui ne rend presque rien
d'utilisable sur la basse. La production, qu'on soupçonnait d'être un coût
majeur, coûte **+7,1 %** — moins que la fourchette qu'on lui prêtait.

Le banc mesure et publie ; il ne décide pas. Ce qu'on en fait s'écrira
ailleurs, avec ses propres attendus écrits avant leur mesure.

## 7. Ce que le banc n'est pas encore, et qui reste à écrire

- Des morceaux LONGS (3 à 5 min) et des parties qui entrent et sortent :
  le banc mesure aujourd'hui une texture stable de 30 s. La chaîne est
  mesurée sur des disques de quatre minutes ; un lot long viendra après S1
  s'il change une conclusion.
- Des parties à échantillons (`vsm.sampler`, `vsm.multisample`) : le
  générateur ne les tire pas (elles demandent des données installées, et
  la chaîne les traite à part).
- La voix : aucun rôle chanté. La chaîne a une branche voix (tête/chœurs)
  que le banc ne mesure pas.

## 8. L'usage apprenant d'H25 — SEULEMENT après S1, et sous condition

> **S1 EST MESURÉE (05/09/2026, 02:23), la condition d'entrée est donc
> levée.** Le § 5 demandait de ne pas ouvrir ce chantier avant ; il l'est
> maintenant, avec ce que la campagne apporte comme matière : dix morceaux
> `deux-mains` et `memes-machine-disjoints` dont la réponse est connue par
> construction, et un chiffre de départ à battre — le découpage par les vides
> dit « deux » à chaque fois, et a fondu les registres disjoints 3 fois sur 3.
> Les attendus du paragraphe ci-dessous restent ceux qui ont été écrits
> d'avance, et ils ne changent pas maintenant que la campagne est finie.

### Ce que la première nuit de travail a établi (05/09/2026, 04:30) — le jeu, pas encore le modèle

Le module existe (`analyse/analyzer/vsm_deux_mains.py`), **désactivé** : rien
dans la chaîne ne l'importe, conformément à la décision ci-dessous. Ce qui est
tranché à ce stade est le JEU D'APPRENTISSAGE, et c'est une mesure, pas un
choix de confort.

**On ne peut pas apprendre sur les notes VRAIES, et voici le chiffre.** Le
premier jet construisait les paires de registres à partir de la vérité des
morceaux générés — c'est gratuit, c'est exact, et c'est faux. Sur **80
morceaux** (40 `deux-mains`, 40 `memes-machine-disjoints`, générés pour
l'occasion), `registres_par_vides` ne pose la question que **7 fois**. Le
garde-fou du fourre-tout, lui, passe 80 fois sur 100 (polyphonie médiane
**9,85** pour un seuil de 3 ; ambitus médian **38 demi-tons** pour un seuil de
36) : ce n'est donc pas lui qui bloque, ce sont les VIDES. Les parties d'un
morceau se recouvrent en hauteur, et la densité lissée n'a pas de creux assez
profond. Baisser le nombre de parties n'y change rien (**1 sur 14** à trois
parties).

**Sur la TRANSCRIPTION du même morceau, la question est posée du premier
coup** : trois registres (MIDI 53-71, 37-51, 29-30) là où la vérité n'en
donnait aucun. C'est le point : la chaîne ne juge pas des notes vraies, elle
juge une transcription de stem séparé, dont les erreurs d'octave et les notes
inventées creusent la densité là où la musique ne creusait pas. **Un modèle
appris sur la vérité aurait été appris sur des paires que la chaîne ne voit
jamais.**

Le jeu se construit donc depuis les courses (`analyse/jeu_h25.py`) : chaque
note transcrite est appariée à la note vraie la plus proche par la fonction du
banc (±1 demi-ton, ±50 ms), hérite de la PARTIE d'où elle vient, et un
registre appartient à la partie qui y pèse le plus. Deux registres voisins
dominés par la même partie sont un seul instrument — la réponse reste connue
par construction, elle est simplement lue à travers la transcription.

**Le prix, mesuré : 424 s par morceau** en front-end seul (`--sans-recherche
--sans-reglage-piste --sans-reglage-melange`, budget d'arbitrage au minimum).
Trente morceaux sont en cours depuis 04:20 ; le modèle et son verdict
s'écriront ici quand ils seront mesurés, et pas avant.

Les six descripteurs sont ceux du § 8 et rien d'autre — synchronie des
attaques dans les deux sens, co-occurrence temporelle, corrélation des
densités d'attaques, rapport des ambitus, rapport des densités. **Aucun n'est
timbral**, et le module ne reçoit que des notes : ni échantillon, ni spectre,
ni nom de machine. C'est la seule contrainte que H25 impose sur la forme de la
réponse.

Écrit d'avance, dans l'en-tête du module (`analyse/analyzer/vsm_deux_mains.py`)
le jour où il s'écrira, et repris ici :

*Attendu.* Sur des morceaux générés où la réponse « un instrument ou deux »
est connue par construction (`deux-mains` contre `memes-machine-disjoints`
et deux parties quelconques à registres disjoints), un petit modèle CPU
(scikit-learn, seedé, versionné par empreinte) sur des descripteurs
STRUCTURELS — jamais timbraux, H25 a montré que le timbre ment : co-occurrence
temporelle des deux registres, corrélation des enveloppes d'attaque,
synchronie des attaques (part des attaques du registre haut à ±30 ms d'une
attaque du bas), rapport des ambitus, rapport des densités — doit dire
« un seul instrument » sur le cas deux-mains PLUS SOUVENT que le découpage
par les vides (qui dit toujours « deux »), SANS dégrader le 9/9 de l'épreuve
de parité ni séparer moins souvent les registres disjoints de S1.

*Décision.* S'il n'y arrive pas, le code reste désactivé, le chiffre se
publie ici — le sort de A3. S'il y arrive, il n'entre dans la chaîne que
derrière une option (`--deux-mains-appris`, dans la provenance), et la
validation sur disque (anti-objectif n° 1) reste due : *Us and Them* et
*Sky and Sand* n'ont montré aucun creux, le modèle y doit être INERTE, et
c'est à mesurer.
