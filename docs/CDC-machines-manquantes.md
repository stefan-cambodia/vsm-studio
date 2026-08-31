# Cahier des charges — machines manquantes pour la reconstruction

**Question posée.** Pour aller d'un fichier **WAV vers MIDI + CLAP** — c'est-à-dire
reconstruire un morceau existant sous forme de notes et de patchs rejouables —
quelles machines manquent au parc actuel ?

**Réponse courte.** Il n'en manque pas *une* : il manque une **catégorie**. Les
douze machines actuelles sont des personnages — elles imposent leur couleur,
c'est leur raison d'être. Pour reproduire un son quelconque, il faut au
contraire des machines **neutres et bien conditionnées pour la recherche**,
plus un lecteur d'échantillons pour tout ce qui n'est pas synthétique.

---

## 1. Ce que le parc couvre réellement

Le projet d'analyse (`analyse/`) sépare l'entrée en stems (batterie, basse,
« autres », voix), transcrit les notes, puis cherche un patch par optimisation.
Il classe ce qu'il entend en : `bass`, `synth_bass`, `synth_lead`, `synth_pad`,
`piano_or_keys`, `strings`, `guitar_or_bright_keys`, `synth`, `unknown`.

| Ce que l'analyse trouve | Machine capable | Verdict |
|---|---|---|
| `synth_bass`, `synth_lead` | Minimoog, TB-303, SH-101, MS-20 | **couvert**, et bien |
| `synth_pad`, `synth` poly | Juno-106, Jupiter-8, Prophet, ARP | **couvert** |
| Timbres FM (cloches, e-piano FM, basses métalliques) | DX7 | **couvert** |
| Batterie électronique | TR-808, TR-909 | **couvert** si le morceau utilise ces machines |
| Batterie acoustique | `vsm.drums` | **couvert** — peaux inharmoniques, métal, pièce (§ 11) |
| Basse électrique, guitare | `vsm.string` | **couvert** — corde pincée, guide d'ondes (§ 10) |
| Piano acoustique | `vsm.piano` (modélisé), `vsm.multisample` (échantillonné) | **couvert deux fois, et il a fallu les deux** — voir l'encadré ci-dessous |
| Orgue, claviers électromécaniques | `vsm.epiano`, `vsm.tonewheel` | **couvert** |
| Cordes | `vsm.string` | **couvert** — corde frottée, même machine (§ 10) |
| Cuivres, bois à perce CYLINDRIQUE | `vsm.wind` | **couvert** — anche et lèvres (§ 11) |
| **Saxophone, hautbois, flûte** | — | **non couvert** — perce conique et jet d'air. Plus « hors de portée » depuis les mesures du prototype conique : ce qui manque est la SÉLECTIVITÉ, pas la possibilité (ARCHITECTURE.md § 33) |
| **Voix** | `vsm.sampler` | **reportée, jamais reconstruite** — et c'est le seul emploi du sampler dans la version finale |

> **Pourquoi le piano acoustique a DEUX machines, et ce que ça a coûté de
> l'apprendre.** *Clair de Lune* est le premier morceau de piano SEUL passé dans
> la chaîne (23/08/2026). `vsm.piano`, la machine modélisée faite pour cette
> case, **n'a pas survécu à la présélection** : les huit finalistes étaient tous
> des soustractifs et des tables d'ondes, entre 0,2590 et 0,3217 — six pour cent
> d'écart, c'est-à-dire aucune information. La case était cochée depuis des mois
> sur la foi d'un raisonnement (« des cordes frappées, donc un guide d'ondes »)
> et non d'une mesure sur un morceau réel. Elle l'est maintenant, et la réponse
> est que la modélisation donne l'EXPRESSIVITÉ tandis que le report
> d'échantillons donne la COUVERTURE : ce sont deux besoins, pas deux candidats
> pour le même. Détail et suite dans
> [`CDC-multisample.md`](CDC-multisample.md).

Or un morceau réel donne presque toujours un stem `drums` acoustique et un
stem `bass` joué sur un instrument, pas sur un TB-303. **Les trois quarts des
stems d'un enregistrement courant n'ont aujourd'hui aucune machine cible.**

> **État : le tableau ci-dessus a été remis à jour, et il ne reste qu'une case
> vide.** `vsm.sampler` a couvert le percussif, `vsm.epiano` et
> `vsm.tonewheel` l'électromécanique, `vsm.string` la corde — pincée comme
> frottée. Les cuivres et les bois restent seuls non couverts : ni une corde ni
> une lame ne les produit, il leur faudrait un modèle à anche ou à lèvre. La
> phrase « les trois quarts des stems n'ont aucune machine cible » n'est plus
> vraie, et c'est ce document qui la portait : elle est conservée telle quelle
> ci-dessus parce qu'elle dit ce qu'ON CROYAIT au départ, et l'encadré dit ce
> que la suite a donné.

## 2. Le second problème, moins visible : les machines de caractère résistent à la recherche

Même sur un stem franchement synthétique, une machine de caractère est un
mauvais candidat pour l'optimisation :

- **Elle colore de force.** Le drive du ladder Moog, la dérive analogique, le
  chorus BBD du Juno : autant de traits présents *quoi qu'on règle*. Si le son
  cible ne les a pas, aucun jeu de paramètres ne l'atteindra.
- **Son espace est étroit.** Le TB-303 a neuf paramètres, dont deux de
  configuration. On ne reconstruit pas un pad avec ça, et l'optimiseur passera
  son temps à le prouver.
- **Ses paramètres sont couplés.** L'accent du 303 agit sur le filtre, le
  niveau ET le decay à la fois : la recherche ne peut pas isoler une dimension.

Mesuré sur une cible pourtant rendue par le Minimoog lui-même : la recherche
converge vers 1125 Hz de coupure pour une cible à 900 Hz, et 1,18 de résonance
pour 2,2. Le pipeline fonctionne — c'est l'espace qui est difficile.

**Conclusion à en tirer** : les machines de caractère servent à **jouer** et à
**identifier** (« ce morceau ressemble à un Juno »), pas à reconstruire au plus
près. Il faut leur adjoindre des machines faites pour être ajustées.

---

## 3. Machine 1 — `vsm.generic` : synthé neutre, conçu pour la recherche

**Intention.** Couvrir le plus large espace timbral possible par paramètre, sans
signature sonore propre. Ce qui est réglé à zéro doit vraiment disparaître : pas
de saturation résiduelle, pas de dérive, pas de coloration « pour le grain ».

**Architecture proposée**

```
OSC 1 (forme morphable) ─┐
OSC 2 (forme morphable) ─┼─> MÉLANGEUR ─> FILTRE multimode ─> VCA ─> DRIVE
SUB (carré/sinus)        │                    ^                ^
BRUIT (blanc/rose)      ─┘                    │                │
                                          ENV 2            ENV 1
                                        + LFO 1..2
```

**Paramètres et identités sémantiques**

| Bloc | Paramètres | Identités |
|---|---|---|
| Oscillateurs | forme morphable (sinus→triangle→scie→carré, **continue**), niveau, désaccord, largeur d'impulsion, octave | `oscillator.N.{shape,level,detune,pulseWidth,octave}` |
| Sources | sub (niveau, type), bruit (niveau, couleur) | `oscillator.sub.*`, `oscillator.noise.*` |
| Filtre | type **continu** (LP→BP→HP), coupure, résonance, pente (2/4 pôles), suivi clavier, quantité d'enveloppe | `filter.1.{type,cutoff,resonance,slope,keyTrack,envAmount}` |
| Enveloppes | ADSR ×2 (amplitude, filtre), courbe (linéaire↔exponentielle) | `envelope.{1,2}.{attack,decay,sustain,release,curve}` |
| LFO | ×2 : forme, fréquence, vers pitch / filtre / amplitude / PWM | `lfo.{1,2}.*` |
| Sortie | drive, niveau | `output.{drive,level}` |

**Exigences propres à cette machine** (elles n'ont pas de sens pour les autres) :

1. **Continuité.** Tout paramètre qui peut être continu l'est — forme d'onde et
   type de filtre compris. Un sélecteur discret crée des falaises dans la
   fonction de coût, où l'optimisation se bloque.
2. **Neutralité au repos.** À réglages neutres, la machine produit une forme
   d'onde propre : pas de dérive, pas de saturation, pas de bruit ajouté. Un
   test le vérifie (spectre sans partiels parasites au-dessus d'un seuil).
3. **Monotonie.** Augmenter la coupure augmente le contenu aigu ; augmenter la
   résonance accentue la bande ; augmenter le drive augmente les harmoniques.
   Testé, parce qu'un paramètre non monotone piège toute recherche par descente.
4. **Découplage.** Un paramètre agit sur une dimension et une seule, autant que
   la physique le permet. Là où le couplage est inévitable (résonance ↔ niveau),
   il est **compensé** en interne et documenté.
5. **Profil de recherche déclaré** : bornes utiles, échelle (linéaire ou
   logarithmique) et importance relative de chaque paramètre, exposés au projet
   d'analyse pour qu'il n'ait pas à les deviner (voir §6).

> **État au 19/08/2026 : fait.** La machine existe (`vsm.generic`), 40
> paramètres et 8 voix, avec sa façade, ses identités sémantiques, son profil
> de recherche et son empreinte de non-régression. Les quatre exigences
> ci-dessus ont chacune leurs tests dans `audio/tests/test_generic_synth.cpp`.
> Une brique partagée a dû s'ouvrir pour la servir :
> `StateVariableFilter::processMulti()` rend les trois sorties du filtre en un
> seul pas d'état, ce sans quoi le fondu continu entre types de filtre ferait
> tourner le filtre à triple fréquence. Voir la section 31 d'`ARCHITECTURE.md`.
>
> **Non retenu, et écrit plutôt que découvert plus tard** : la courbe
> d'enveloppe (linéaire ↔ exponentielle) du tableau ci-dessus n'est pas
> exposée ; les deux ADSR emploient la courbe partagée du parc. C'est une
> dimension de moins pour la recherche.

## 4. Machine 2 — `vsm.sampler` : lecteur d'échantillons

**Intention.** La seule façon honnête de reproduire une source acoustique.
Aucune synthèse soustractive ne fera une caisse claire enregistrée ; un
échantillon, si.

**Pourquoi c'est la pièce la plus rentable du lot** : le projet d'analyse
dispose déjà du matériau. Il isole le stem `drums`, détecte les frappes,
découpe les coups : ces extraits **sont** les échantillons. La reconstruction
devient alors quasi exacte pour tout le percussif — et la batterie est
justement ce que la synthèse reproduit le plus mal.

> **État : fait, en version 16 emplacements.** La machine existe
> (`vsm.sampler`), avec sa façade, ses identités sémantiques, son empreinte et
> son accès depuis le service de rendu. Les seize emplacements prévus ci-dessous
> y sont : le blocage n'était pas la mémoire mais l'AFFICHAGE — seize
> emplacements à sept réglages font cent douze commandes, illisibles alignées.
> Il a été levé en ne montrant que les quatre réglages qui se JOUENT (niveau,
> accord, décroissance, panoramique) ; les trois autres (note de déclenchement,
> point de départ, groupe de coupure) sont des réglages de configuration, posés
> une fois, déclarés omis avec leur raison. Le filtre passe-bas par emplacement
> n'a pas été retenu — pour rejouer un coup découpé, il n'apporte rien et
> triplerait le panneau.

**Architecture** : 16 emplacements, un échantillon par emplacement (mono ou
stéréo, chargé depuis un WAV), et par emplacement : note de déclenchement,
accord (± demi-tons), point de départ, enveloppe d'amplitude, filtre passe-bas
simple, niveau, panoramique, groupe de coupure (charleston ouvert/fermé).

**Contraintes techniques**

- Le chargement des fichiers se fait **hors du thread audio** — publication par
  échange atomique de pointeur, comme les chaînes d'effets du `ProcessGraph`.
- Rééchantillonnage de qualité (interpolation cubique au minimum), avec
  l'approximation documentée.
- L'état sauvegardé référence les fichiers par **chemin relatif** au dossier de
  projet : un projet doit rester transportable (règle déjà établie pour
  `project.json`).
- Un échantillon manquant est **signalé**, jamais remplacé par un autre son.

## 5. Machine 3 — `vsm.drumkit` : boîte à rythmes générique

Peut n'être qu'un profil du sampler (mêmes moteurs, façade et mapping
différents) : 8 à 16 pièces, une colonne de réglages par pièce, grille de 16
pas — la façade des TR, sans leur synthèse spécifique.

À trancher au moment de l'écrire : **profil du sampler** (moins de code, moins
de tests) ou **machine distincte** (façade plus juste). Le CDC de la §7 de
[`CDC-nouvelle-machine.md`](CDC-nouvelle-machine.md) s'applique dans les deux cas.

> **Tranché : aucune des deux. `vsm.drumkit` ne sera pas écrit, parce que
> `vsm.sampler` EST déjà cette machine.** En reprenant point par point ce que
> le paragraphe ci-dessus demande, le sampler livrait déjà tout sauf un
> détail :
>
> | Ce que le §5 demande | État dans `vsm.sampler` |
> |---|---|
> | 8 à 16 pièces | seize emplacements |
> | une colonne de réglages par pièce | seize sections de quatre réglages |
> | grille de 16 pas | `SequencerKind::DrumGrid`, seize pas |
> | mapping de boîte à rythmes | la note SÉLECTIONNE la pièce, elle ne transpose pas |
> | notes par défaut | la convention General MIDI de la batterie |
> | charleston ouverte/fermée | groupes de coupure |
> | « sans leur synthèse spécifique » | des échantillons, justement |
>
> Il manquait le NOM DES PIÈCES : la façade disait « SLOT 3 » là où une boîte à
> rythmes dit « HH CL ». C'est ce qui a été corrigé, et c'est tout ce qu'il
> fallait.
>
> **Pourquoi ne pas l'écrire quand même.** Une seconde machine aurait partagé
> le moteur, les paramètres, les identités sémantiques et la façade de la
> première : elle aurait coûté une empreinte de plus, une table sémantique
> identique de plus, une batterie de tests de plus, et une entrée de plus dans
> chaque liste de choix — pour un changement d'étiquettes. Le §7 de ce même
> document met en garde contre exactement cela : « elles élargissent le
> catalogue, pas la couverture ». Le §0 de
> [`CDC-nouvelle-machine.md`](CDC-nouvelle-machine.md) dit la même chose depuis
> l'autre bout : une machine se déclare, elle ne se duplique pas.
>
> **Ce que la décision coûte, et qui est écrit plutôt que caché.** Les noms de
> pièces affichés sont ceux que la convention General MIDI met à ces notes PAR
> DÉFAUT ; un emplacement dont on change la note de déclenchement porte alors
> une étiquette inexacte. C'est la limite assumée d'un kit dont les pièces
> restent réassignables — le numéro d'emplacement, lui, reste en tête du titre
> et ne ment jamais. Par ailleurs la grille de pas ne programme que les huit
> premières pièces ; les seize se déclenchent, mais les toms et percussions se
> jouent depuis le piano roll, faute de quoi seize lignes de seize pas seraient
> illisibles.

---

## 6. Exigence transverse : déclarer un « profil de recherche »

Aujourd'hui, `analyse/analyzer/vsm_patch_optimizer.py` code en dur les bornes de
recherche. C'est fragile : elles vieillissent, et elles ne peuvent pas s'adapter
à une machine que le script ne connaît pas.

**Proposition** : chaque machine déclare, à côté de sa `ParameterList`, un
profil optionnel par paramètre — bornes *utiles* (souvent plus étroites que les
bornes absolues), échelle (linéaire / logarithmique), et importance (`primary`,
`secondary`, `fixed`). Exposé dans la couche `interchange/`, lisible par le
service de rendu, donc utilisable par Python sans rien coder en dur.

Bénéfice concret : la recherche ne gaspille plus ses évaluations dans des
régions inaudibles, et une machine nouvelle devient optimisable **sans toucher
au code Python**.

## 7. Ordre recommandé, et pourquoi

1. **`vsm.sampler`** — débloque batterie, basse, piano, guitare : la majorité
   des stems d'un morceau réel. Plus gros gain, et le matériau (les coups
   découpés) existe déjà côté analyse.
2. **Profil de recherche** (§6) — petit travail, il rend les deux machines
   suivantes exploitables tout de suite.
3. **`vsm.generic`** — rend la reconstruction des stems synthétiques réellement
   convergente, au lieu d'approcher.
4. **`vsm.drumkit`** — confort et fidélité de façade, une fois le sampler en
   place.

Ce qui n'est **pas** recommandé, et pourquoi : ajouter d'autres machines de
caractère (Oberheim, Korg Poly, Roland D-50…) est plaisant mais n'améliore
presque pas la reconstruction. Elles élargissent le catalogue, pas la couverture.

## 8. Critères d'acceptation propres à ces machines

En plus du CDC général :

```
[x] vsm.generic : à réglages neutres, spectre propre (test)
[x] vsm.generic : monotonie vérifiée sur coupure, résonance, drive (tests)
[x] vsm.generic : formes d'onde et type de filtre CONTINUS, sans palier
[x] vsm.sampler : chargement hors thread audio, publication atomique
[x] vsm.sampler : échantillon manquant signalé, jamais substitué
[x] vsm.sampler : chemins relatifs dans l'état sauvegardé
[x] Profil de recherche déclaré et lisible depuis interchange/
[x] Preuve de bout en bout : un stem réel reconstruit, distance mesurée AVANT
    et APRÈS l'ajout, chiffres publiés dans ARCHITECTURE.md (§31)
[x] vsm.string : justesse du guide d'ondes vérifiée sur cinq octaves (test)
[x] vsm.string : le point de pincement annule les harmoniques qu'il doit (test)
[x] vsm.string : l'archet ENTRETIENT là où le pincement s'éteint (test)
[x] vsm.string : distance à une cible réelle mesurée AVANT et APRÈS, publiée
    dans ARCHITECTURE.md (§32)
```

Le dernier point est le seul qui compte vraiment : ces machines existent pour
faire baisser une distance mesurable. Si elle ne baisse pas, elles n'ont pas
tenu leur promesse, et il faudra le dire.

> **Mesuré, et le voici dit (chiffres complets dans ARCHITECTURE.md §31).**
> Pour `vsm.sampler`, la promesse est tenue depuis sa livraison : un coup
> découpé rejoué corrèle à 1,0000 avec l'original. Pour `vsm.generic`, le
> verdict est en deux moitiés. Sur de l'audio PROPRE produit par une machine
> du parc, la distance ne baisse pas : la machine d'origine gagne toujours, et
> le generic ne fait ni mieux ni pire (il ne vole jamais l'identification —
> le risque symétrique, écarté par la mesure). Sur l'audio réellement à
> reconstruire — un stem passé par la séparation, teinté d'artefacts, sans
> machine d'origine évidente — le generic GAGNE le stem de basse (0,139 contre
> 0,149 au meilleur sans lui, la vraie machine hors podium). La neutralité
> paie exactement là où ce cahier des charges la destinait, et pas ailleurs.
> S'y ajoute une dépendance mesurée au budget de recherche : le generic est la
> seule machine à profiter d'un espace élargi (0,190 → 0,132 en passant de 6 à
> 20 axes sur la basse), pendant que les machines étroites s'y diluent — le
> plafond de six dimensions du budget par défaut le pénalise structurellement.

---

## 9. Suite du parc : quels synthés célèbres manquent encore

Le §1 traitait la couverture des **sources** (batterie acoustique, basse,
piano…). Cette section traite les **machines** elles-mêmes : lesquelles
ajouter, et dans quel ordre.

Deux critères, à ne pas confondre :

- **Notoriété** — ce qu'on veut jouer, et ce qu'un utilisateur cherchera dans
  la liste.
- **Famille de synthèse** — ce qui manque réellement au moteur. Ajouter un
  douzième soustractif à filtre en échelle n'ouvre presque rien ; une famille
  absente ouvre tout un pan de sons.

### Ce que le parc couvre déjà, par famille

| Famille | Machines | État |
|---|---|---|
| Soustractif, filtre en échelle | Minimoog, TB-303, SH-101, Juno-106, Jupiter-8, Prophet | **saturé** — six machines s'y partagent le même filtre |
| Soustractif, filtre à variable d'état | MS-20 (double HPF+LPF), ARP Odyssey | couvert |
| FM | DX7 (6 opérateurs) | couvert |
| Échantillons | Sampler | couvert (16 emplacements, façade de boîte à rythmes) |
| Percussions analogiques | TR-808, TR-909 | couvert |
| Table d'ondes | `vsm.wavetable` (4 tables × 8 formes, anti-repliement par niveaux) | **fait** |
| Hybride PCM + soustractif | `vsm.pcmhybrid` (5 transitoires engendrés + attaque chargeable) | **fait** |
| Supersaw / unisson massif | `vsm.supersaw` (7 scies, courbes de désaccord et de mélange) | **fait** |
| Électromécanique | `vsm.epiano` (lames), `vsm.tonewheel` (roues phoniques + rotatif) | **fait** |
| Filtre à 2 pôles, poly « brass » | `vsm.obx` | **fait** |
| **Modélisation physique, guide d'ondes** | `vsm.string` (corde pincée et frottée) | **fait** — voir § 10 |

### Proposition, par ordre de rendement

| Rang | Machine | Famille ouverte | Pourquoi elle, et pas une autre |
|---|---|---|---|
| 1 | **Piano électrique à lames** (type Rhodes / Wurlitzer) | électromécanique | Le classificateur d'analyse sort `piano_or_keys` sur quantité de morceaux, et **aucune** machine ne peut répondre. Modélisable (lame + marteau + pastille), donc plus léger qu'un piano acoustique échantillonné. Ouvre en plus la porte à l'orgue. |
| 2 | **Poly « brass » à filtre 2 pôles** (type Oberheim OB-X) | soustractif SVF poly | Le son de nappe et de cuivres le plus reconnaissable des années 80, et il sonne *différemment* de tout le parc actuel : deux pôles au lieu de quatre, ça s'entend immédiatement. Brique déjà présente (`StateVariableFilter`). |
| 3 | **Supersaw** (type JP-8000) | unisson massif | Omniprésent dans la musique électronique depuis 1997. Techniquement peu coûteux (7 scies désaccordées par voix), et sans lui la reconstruction d'un lead de dance n'a aucune chance. |
| 4 | **Table d'ondes** (type PPG / Waldorf) | table d'ondes | Vraie famille de synthèse absente : balayage d'une table, timbres mouvants qu'aucune machine actuelle n'atteint. Coûte surtout une infrastructure de tables. |
| 5 | **Hybride PCM + soustractif** (type D-50 / M1) | attaque échantillonnée + corps synthétique | Le M1 est le synthé le plus vendu de l'histoire ; le D-50 a défini la fin des années 80. **Le sampler rend ces machines abordables** : l'attaque PCM existe déjà, il reste à la faire passer dans un filtre et une enveloppe. |
| 6 | **Orgue à roues phoniques** (type Hammond) | électromécanique | Très présent dans les enregistrements réels ; tirettes harmoniques + rotatif. À faire après le piano électrique, dont il partage l'esprit. |

### Ce que je ne recommande pas, et pourquoi

- **Un septième soustractif à filtre en échelle** (Roland JX, Korg Polysix,
  Moog Voyager…). Ils sonnent bien, mais le parc en a déjà six : le gain est
  un nom sur une liste, pas une famille de sons.
- **Le CS-80.** Célèbre, magnifique — et très coûteux à faire honnêtement
  (double couche complète, sensibilité polyphonique à la pression, rubans).
  À garder pour plus tard, en le faisant bien plutôt qu'à moitié.
- **Les romplers à bibliothèque** (JV-1080 et suite). Sans les échantillons
  d'origine, il ne resterait qu'un lecteur — que le sampler fait déjà.

### Effet sur la reconstruction

### État : les six machines sont livrées

Les six rangs ci-dessus ont été implémentés, chacun avec l'ensemble exigé par
le présent cahier des charges : DSP, suite de tests propre, identités
sémantiques de paramètres, façade, et empreinte audio de non-régression.

| Rang | Identifiant | Tests | Ce qui fait sa singularité dans le parc |
|---|---|---|---|
| 1 | `vsm.epiano` | 12 | Partiels inharmoniques, bruit de marteau, saturation asymétrique de pastille |
| 2 | `vsm.obx` | 15 | Filtre 12 dB/oct (deux étages distincts pour 24), unisson comme mode de jeu |
| 3 | `vsm.supersaw` | 14 | Sept scies, désaccord NON linéaire, courbes de niveau opposées centre/côtés |
| 4 | `vsm.wavetable` | 12 (+ 8 sur la brique) | Le timbre change sans le filtre ; niveaux de repliement pour les aigus |
| 5 | `vsm.pcmhybrid` | 13 | Attaque échantillonnée + corps synthétique, modulation en anneau, attaque remplaçable |
| 6 | `vsm.tonewheel` | 15 | Roues PARTAGÉES, repliement de rangs, rotatif à deux rotors d'inerties différentes |

Deux briques d'infrastructure réutilisables sont nées de ce travail :

- `dsp/WaveTable.h` — banque de tables engendrées par spectre, avec niveaux de
  repliement. Le repliement est le vrai problème de la lecture de table, et il
  est traité une fois pour toutes, pas machine par machine.
- `plugins/pcmhybrid` — banque de transitoires engendrés, doublée d'un
  `ISampleLoader` à un seul emplacement. **C'est le point de contact avec la
  reconstruction** : la chaîne d'analyse peut y déposer l'attaque réelle
  découpée dans le fichier à reproduire, et laisser la synthèse faire le reste.

Deux défauts trouvés par la mesure au passage, et corrigés :

- `vsm.obx` écrêtait sur un accord de huit notes (crête 1,17). Le niveau de
  voix a été recalibré et un test de marge a été ajouté à sa suite.
- La normalisation des tables d'ondes par la somme des amplitudes écrasait les
  formes riches ; elle est désormais faite en ÉNERGIE, ce qui est la grandeur
  que l'oreille rapporte au volume.

Les rangs 1, 3 et 6 comblent des **trous de couverture** : sans eux, des stems
entiers n'ont aucune machine cible. Les rangs 2, 4 et 5 élargissent surtout la
palette de jeu — utiles, mais à ne pas confondre avec un gain de reconstruction.
La règle du §7 reste valable : mesurer la distance **avant et après**, et
publier le chiffre.

---

## 10. Machine 4 — `vsm.string` : la corde, pincée et frottée

**Pourquoi elle, et pourquoi maintenant.** Après les six machines du § 9, il
restait exactement une limite écrite au § 1 de
[`ROADMAP-fusion.md`](ROADMAP-fusion.md) : « basse, guitare et cordes réelles
passent toujours par le sampler faute de modèle dédié ». Trois des sources du
tableau du § 1 n'avaient aucune machine cible, et elles ont ceci de commun
qu'elles sont toutes **une corde**. Le § 7 met en garde contre l'ajout de
machines de caractère, « qui élargissent le catalogue, pas la couverture » :
celle-ci fait l'inverse. Elle ouvre une famille de synthèse absente du parc —
la modélisation physique par guide d'ondes — et c'est la seule qui puisse
répondre honnêtement à un stem de corde jouée.

**Le principe.** Une onde qui fait des aller-retour dans une ligne à retard de
longueur `SR/f0`, et qui perd un peu à chaque tour. Ce qu'elle perd fait la
décroissance ; ce qu'elle perd **dans l'aigu d'abord** fait le timbre ; la
vitesse à laquelle les aigus la parcourent fait l'inharmonicité d'une corde
raide. Rien de tout cela ne s'obtient avec un oscillateur et un filtre : une
corde n'a ni l'un ni l'autre, elle a une longueur, une raideur et un point de
contact.

**Deux excitations, un fondu continu.** Le pincement rend son énergie d'un
coup ; l'archet en fournit tant qu'il frotte, par un cycle d'adhérence et de
décrochement qu'aucune enveloppe n'imite. `Excitation` passe de l'un à l'autre
**sans palier** — exigence n° 1 du § 3, qui vaut ici pour la même raison : la
machine est faite pour être cherchée, et un sélecteur discret creuse une
falaise dans la fonction de coût.

**Exigences propres à cette machine** :

1. **Justesse.** La hauteur naît de la longueur de la boucle, qui n'est pas un
   nombre entier d'échantillons. Un retard fractionnaire est donc obligatoire :
   à 4 kHz, un échantillon de retard vaut plus d'un demi-ton. Testé de la note
   28 à la note 76 ; erreur mesurée sous 0,2 cent.
2. **Le réglage de raideur doit valoir la même chose d'un bout à l'autre du
   clavier.** À coefficient de dispersion fixe, l'inharmonicité suit la
   fréquence absolue et non le rang du partiel : elle disparaît sur les cordes
   graves, c'est-à-dire là où elle s'entend le plus. Le coefficient dépend donc
   de la note, et il est résolu pour que l'effet soit linéaire dans le réglage.
3. **Le pincement doit avoir la pente d'un triangle.** Le point d'injection
   produit le facteur `sin(n·pi·p)` de la corde idéale ; il manque le 1/n² du
   déplacement triangulaire. Sans lui, le second harmonique sort plus fort que
   le fondamental, ce qu'aucun instrument à cordes ne fait.
4. **Transparence de la caisse au repos.** Une basse électrique n'a pas de
   caisse : à `Body Level = 0`, la machine doit être exactement transparente.
5. **Profil de recherche déclaré**, comme au § 6.

> **État : fait.** La machine existe (`vsm.string`), quinze paramètres et huit
> voix, avec ses seize tests, ses identités sémantiques, son profil de
> recherche, sa façade et son empreinte. Les exigences 1 à 4 ont chacune leur
> test dans `audio/tests/test_string_synth.cpp`.
>
> **La promesse est tenue AU BUDGET DE LA CHAÎNE, et elle ne l'est plus à
> budget triplé** (chiffres complets dans ARCHITECTURE.md § 32). Sur un
> violoncelle à l'archet RÉEL, cherchée parmi les dix-sept machines mélodiques
> sans présélection : au budget par défaut (20 itérations, celui que
> `reconstruire.py` emploie), `vsm.string` arrive **première** sur les trois
> graines — 0,1225 contre 0,1310 au meilleur du parc sans elle. À 60
> itérations, elle **perd** sur les trois : le SH-101 descend à 0,0865 quand
> elle plafonne à 0,1190.
>
> La raison n'est pas un défaut de convergence, c'est un PLAFOND PHYSIQUE : ses
> dix axes sont peu nombreux et fortement contraints — une corde ne peut pas
> produire n'importe quel spectre, c'est ce qui en fait un modèle — donc la
> recherche les épuise vite. De 20 à 60 itérations, elle gagne 10 % quand un
> soustractif en gagne 36. C'est le symétrique exact de ce que le § 3 disait de
> `vsm.generic` : la neutralité s'achète avec du budget, la fidélité physique se
> paie d'avance.
>
> Deux résultats à ne pas perdre de vue pour autant. Elle est de loin la plus
> **stable** des candidates (±7 % sur six recherches, contre un facteur deux
> pour le SH-101) : un classement de machines vaut mieux avec une candidate
> dont le verdict ne dépend pas de la graine. Et sur une cible à l'archet, les
> six recherches indépendantes ont toutes retenu `string.excitation` entre 0,956
> et 0,999 — c'est-à-dire **l'archet**, jamais le pincement. Aucune distance ne
> dit à l'optimiseur ce qu'est un archet : il l'a trouvé parce que le modèle en
> contient un.
>
> **Ce que la mesure n'établit pas**, et qu'il faut dire : une cible, un
> instrument. La chaîne complète (`reconstruire.py` de bout en bout, séparation
> et transcription comprises) n'a pas pu être rejouée, faute de la pile
> d'analyse lourde sur la machine où la mesure a été faite. C'est la moitié
> manquante de la preuve.
>
> **Ce que la mesure a coûté et rapporté.** Elle a d'abord donné le résultat
> INVERSE (14e sur 17), et l'enquête a trouvé trois vrais défauts du modèle —
> la dispersion à coefficient fixe, la salve de pincement trop courte pour
> exciter une corde grave, la pente en 1/n² absente — puis un quatrième défaut
> qui n'était pas dans la machine du tout : le `gate` du protocole valait 0,95
> pour une cible qui se tait à 0,24. Une distance n'est un chiffre que si l'on
> sait à quelles conditions elle a été obtenue ; la métrique et le budget
> étaient déjà inscrits dans les rapports, le `gate` ne l'était nulle part.
>
> **Non retenu, et écrit plutôt que découvert plus tard** : l'archet n'a pas de
> temps de montée réglable (une dimension de recherche économisée, un handicap
> réel sur les attaques lentes) ; le corps est une coloration en résonances
> série, pas une caisse qui rayonne ; et une seule ligne à retard porte
> l'aller-retour là où la physique en demande deux.

---

## 11. Machines 5, 6 et 7 — `vsm.piano`, `vsm.drums`, `vsm.wind`

Cette section existait comme RENVOI avant d'exister comme texte : le tableau du
§ 1 y pointait quatre fois pendant qu'elle manquait. Elle est écrite ici pour
que le renvoi cesse de mentir, et **elle ne redit pas ce qu'ARCHITECTURE.md
§ 33 dit déjà**. Le détail — la loi du marteau, les zéros de Bessel, les quatre
topologies de boucle — vit là-bas et nulle part ailleurs : deux exposés de la
même physique divergeraient, et c'est la faute que le § 8.4 reproche au reste
du projet.

**Ce qui a rendu ces trois machines nécessaires** est une décision, pas un
manque : le sampler a cessé d'être le repli universel pour être **réservé à la
voix** (§ 4 et ARCHITECTURE.md § 33). Trois trous se sont ouverts d'un coup —
la batterie acoustique perdait sa seule réponse, le piano acoustique aussi (il
était déclaré « hors de portée sans bibliothèque d'échantillons »), et les
cuivres et les bois n'en avaient jamais eu.

| Machine | Famille ouverte | Source du tableau du § 1 |
|---|---|---|
| `vsm.piano` | cordes FRAPPÉES | `piano_or_keys` acoustique |
| `vsm.drums` | membranes inharmoniques et métal | `drums` sans échantillon |
| `vsm.wind` | anche et lèvres, perce cylindrique | clarinette, cuivres |

**Ce qu'elles doivent au § 7, qui les déconseillait.** Le § 7 met en garde
contre l'ajout de machines : « elles élargissent le catalogue, pas la
couverture ». Ces trois-là font l'inverse, pour la raison exacte que le § 10
donnait déjà pour `vsm.string` — elles ouvrent des FAMILLES DE SYNTHÈSE
absentes du parc (banc modal inharmonique, guide d'ondes à réflexion
inversante), pas des variantes d'un soustractif qu'on possède en huit
exemplaires. Le garde-fou du § 7 reste valable, et il vaut toujours contre une
neuvième machine de caractère.

**La brique partagée, et la preuve qu'elle l'est fidèlement.** `vsm.piano`
emploie EXACTEMENT la boucle de `vsm.string`, sortie dans
`dsp/StringWaveguide.h` plutôt que recopiée. La preuve que l'extraction n'a
rien changé n'est pas une relecture, c'est l'empreinte : `vsm.string` a gardé
la sienne AU BIT PRÈS à travers le refactoring. `vsm.wind`, lui, ne la partage
pas, et le dire compte autant — sa réflexion est inversante, il n'a pas de
dispersion, et sa perte est un rayonnement au pavillon.

### La case qui reste vide, et pourquoi elle ne se comble pas par un réglage

**Saxophone, hautbois, flûte.** C'est la dernière ligne sans machine du tableau
du § 1, et **ce n'est pas un défaut de réglage de `vsm.wind`** : c'est sa
topologie. Une réflexion inversante à demi-longueur impose la symétrie
demi-onde, qui **interdit mathématiquement les harmoniques paires** — or c'est
précisément d'elles qu'un saxophone tire son timbre. Vérifié sur quatre
topologies de boucle avec la même anche (tableau dans ARCHITECTURE.md § 33) ;
aucune ne donne à la fois l'oscillation et les paires.

**Ce qu'il faudrait, et ce que ça vaut.** Un résonateur CONIQUE, c'est-à-dire
une autre machine, pas une option de celle-ci : la perce conique se comporte
en tuyau ouvert (série harmonique complète) et la flûte n'a pas d'anche du tout
mais un jet d'air instable. Deux modèles, donc, ou un modèle à deux régimes.

**Décision, et elle est de ne pas le faire maintenant.** Le § 7 demande de
juger une machine sur la COUVERTURE qu'elle ajoute, pas sur le catalogue.

Il faut d'abord écarter un mauvais argument, parce qu'il se présente tout seul :
« aucune catégorie ne désignerait un bois conique ». C'est faux pour la chaîne
de reconstruction. Les neuf catégories du § 1 viennent de `classify_stems`, que
seul `main.py` emploie ; `reconstruire.py` **ne classe rien** — il met TOUTES
les machines mélodiques en concurrence sur chaque stem et laisse l'arbitrage sur
la piste trancher à la distance mesurée. Une machine conique serait donc
essayée d'office, sans qu'on ait à lui construire un aiguillage.

Le vrai motif est ailleurs, et c'est le motif habituel de ce document : **rien
ne l'a encore mesurée comme manquante.** Toutes les distances publiées portent
sur *Children* et *House Of God*, deux morceaux sans bois conique. Construire
le résonateur maintenant, ce serait payer une machine entière — DSP, identités
sémantiques, profil de recherche, façade, tests, empreinte (§ 10 de
`CDC-nouvelle-machine.md`) — sur la foi d'un raisonnement, quand tout le reste
du parc a été justifié par un chiffre. Le § 10 rappelle ce que ça coûte de
faire l'inverse : `vsm.string` a d'abord donné le résultat INVERSE de celui
qu'on attendait, et il a fallu la mesure pour trouver ses quatre vrais défauts.

~~**À rouvrir dès qu'un morceau à saxophone, hautbois ou flûte sera passé dans
la chaîne et que le stem correspondant sera mesuré comme mal servi** — la
condition est vérifiable, et elle est peu coûteuse à remplir : il suffit d'un
morceau et d'une exécution.~~

**CETTE CONDITION EST INAPPLICABLE TELLE QU'ELLE EST ÉCRITE, ET C'EST LA
TENTATIVE DE LA REMPLIR QUI L'A MONTRÉ (31/08/2026).** Elle parle du « stem
correspondant ». Il n'existe pas. `htdemucs` sépare en QUATRE sources —
`drums`, `bass`, `other`, `vocals` — et **aucun instrument à vent n'a de stem
à lui** : un saxophone tombe dans `other`, avec l'orgue, la guitare et le
piano. Une distance mesurée sur `other` ne peut donc pas être imputée au vent,
ce qui est précisément ce que la condition demandait de faire.

**Ce qui a été essayé, sur *Us and Them* (Pink Floyd, 1973), choisi parce que
le ténor de Dick Parry y joue longuement :**

- **Séparation à quatre sources, puis recherche d'un passage de vent à
  découvert dans `other`.** Le passage trouvé donnait `f0` = 366,2 Hz à 140 s,
  à 150 s ET à 200 s. Une hauteur tenue une minute est un PAD D'ORGUE, pas une
  phrase de saxophone : le critère « son tenu » sélectionnait exactement le
  mauvais instrument. Le profil harmonique qu'on en tirait décrivait l'orgue.
- **Suivi de hauteur au quart de seconde sur la section de solo**, pour
  remplacer « son tenu » par « ligne mélodique ». Résultat : 112 / 220 / 364 Hz
  qui alternent d'une fenêtre à l'autre — le détecteur de pic saute entre
  l'orgue et le reste, aucune ligne cohérente.
- **Séparation à SIX sources** (`htdemucs_6s` : `guitar` et `piano` en plus),
  pour vider `other` de ce qui masquait le vent. `other` y tombe à 0,018 de
  niveau efficace sur la section de solo, DERRIÈRE `guitar` (0,028) : ce n'est
  plus un instrument, c'est un résidu, et son spectrogramme ne montre aucune
  pile harmonique suivie. Le parc de modèles de séparation ne contient de toute
  façon aucune source « vent ».

**CE QUE LA CONDITION DOIT DIRE À LA PLACE.** La bonne épreuve n'est pas un
morceau, c'est **une cible isolée** — exactement le protocole qui a fait
accepter `vsm.string` : `cello.wav`, une note réelle d'instrument acoustique,
cherchée parmi les machines mélodiques sans présélection (§ 10, et
ARCHITECTURE § 33). Pour le vent conique, il faut donc un enregistrement où
l'instrument est **la seule source mélodique entretenue** — une pièce solo, ou
un échantillon isolé —, et la mesure qui tranche est la comparaison des
profils harmoniques : `vsm.wind` porte h3 = 0,156 et h5 = 0,753 et rien de
pair, une perce conique doit porter les rangs pairs.

**ET LA FLÛTE, ELLE, A ÉTÉ TENTÉE — CE DOCUMENT NE LE DISAIT PAS.** Le
paragraphe « ce qu'il faudrait » ci-dessus annonce « deux modèles, donc » et
s'arrête là, comme si aucun n'existait. Le second a été écrit :
`audio/plugins/flute/`, un modèle jet/biseau avec ses tests, HORS du
`CMakeLists` — un résultat négatif conservé, et non un oubli. Sa mesure est au
§ 44 d'ARCHITECTURE.md, et elle vaut d'être connue avant toute reprise : ce
qu'on avait d'abord pris pour une auto-oscillation était une composante
CONTINUE, chaque harmonique pesant 0,00004 pour un niveau global de 0,27 ; le
bloqueur de continu, qui est obligatoire, fait tomber le niveau à 0,005 et fige
la fréquence à 1 412 Hz quelle que soit la note.

Le compte exact est donc de **cinq tentatives** — quatre topologies à anche
(§ 33) et une à jet (§ 44) —, et le § 44 en tire la conclusion qui doit guider
la sixième : l'obstacle est le même à chaque fois, un gain de boucle sous le
seuil, atteint par des topologies entièrement différentes. **Ce n'est donc ni
la forme de la perce ni la nature de l'excitateur qui bloque, c'est la
formulation du COUPLAGE entre l'excitateur et la colonne d'air.** Une sixième
topologie ne serait pas une tentative de plus, ce serait la même erreur pour la
sixième fois.

**LA MESURE À CIBLE ISOLÉE A ÉTÉ FAITE (31/08/2026), ET ELLE CHIFFRE ENFIN LE
FOSSÉ.** La condition réécrite ci-dessus demandait un enregistrement où
l'instrument est seul ; les banques GeneralUser GS installées le fournissent —
145 zones de saxophone ténor échantillonné, mesurées zone par zone sur leur
région de boucle (la partie tenue du son) :

| | h2 (paire) | h3 | h4 (paire) |
|---|---|---|---|
| ténor réel (médiane de 6 zones, 165–350 Hz) | **0,419** | 0,355 | **0,442** |
| `vsm.wind` (ARCHITECTURE § 33) | **0,000** | 0,156 | **0,000** |

Les rangs pairs d'un saxophone ne sont pas un ornement : ils pèsent autant que
les impairs. La symétrie demi-onde de `vsm.wind` les interdit mathématiquement
— le fossé n'est plus un raisonnement, c'est un facteur mesurable à l'infini.

**ET LA CONSÉQUENCE N'EST PAS DE CONSTRUIRE LA SIXIÈME TOPOLOGIE.** Les cinq
tentatives documentées butent toutes sur le même seuil d'auto-oscillation, et
le § 44 d'ARCHITECTURE dit où chercher (le couplage), pas quand. En attendant,
**la case du tableau de couverture est remplie par la route honnête du parc** :
l'import SoundFont de `vsm.multisample`. Treize profils GeneralUser GS sont
installés — saxophones soprano/alto/ténor, hautbois, clarinette, flûte,
trompette, section de cuivres, cordes, chœur, violon, contrebasse, guitare
nylon — chacun avec son attribution. C'est un report d'échantillons et c'est
présenté comme tel : la modélisation physique reste fermée.

**MAIS LA COUVERTURE N'EST QU'À MOITIÉ OUVERTE, ET C'EST LE PREMIER MORCEAU
QUI L'A DIT (31/08/2026).** Une première rédaction de ce paragraphe affirmait
que le saxophone installé serait « joué d'office par l'arbitrage comme toute
machine du parc ». *Us and Them* (Pink Floyd — ténor de Dick Parry, premier
morceau à saxophone jamais passé dans la chaîne) a montré que c'est faux :
`vsm.multisample` ne porte qu'UN profil par exécution — le premier installé,
« GM-Warm-Pad », et le journal dit en toutes lettres que les dix-huit autres
« ne seront pas essayés » (`VSM_PROFIL=nom` pour en choisir un). Le ténor n'a
donc jamais concouru. La couverture est INSTALLÉE mais pas ARBITRÉE : pour
qu'elle le soit, il faudrait que l'arbitrage de piste présente une candidate
`vsm.multisample` PAR profil installé. ~~C'est la prochaine marche, et elle
est écrite ici pour ne pas être crue déjà franchie.~~

**LA MARCHE EST FRANCHIE ET MESURÉE (01/09/2026), ET LA MESURE ENSEIGNE
AUTRE CHOSE QUE PRÉVU.** L'arbitrage présente une candidate par profil
(trente et un aujourd'hui, GeneralUser GS et FluidR3), le gagnant emporte son
profil jusqu'au projet, et le classement l'affiche
(`multisample[GU-Nylon-Guitar]=0.227*`). Rejoué sur *Us and Them* aux mêmes
conditions : au STEM, les profils écrasent tout — `other` passe de 0,350
(`vsm.string`) à **0,227** (guitare nylon, −35 %), les suivantes à 54–95 %
derrière. Et le MORCEAU recule : 0,2708 contre 0,2638 (+2,7 %).

C'est la leçon du mandataire (§ 5 septies, decies) sous sa forme la plus
instructive : **un timbre plus vrai colle mieux au stem FUITES COMPRISES** —
la basse y est gagnée par un CHŒUR échantillonné, parce que le stem de basse
contient de la fuite vocale et qu'un chœur colle à la fuite. Le verdict du
mélange a contenu les dégâts (il a écarté le pire) sans les annuler : le
trajet glouton, parti d'autres candidates, finit ailleurs. Le fan-out RESTE —
il enrichit le vivier et le ténor concourt enfin — mais son bénéfice net
attend H1 (réglage jugé au mélange) et une passe de stabilisation du verdict,
inscrites au § 5 duodecies de la feuille de route fusion.

**La décision de ne pas construire la machine ne change pas** : elle reposait
sur « rien ne l'a encore mesurée comme manquante », et ce paragraphe ne mesure
toujours rien de tel. Ce qui change, c'est qu'on sait maintenant que la
mesure ne s'obtiendra pas en passant un morceau de plus dans la chaîne, et ce
qu'il faudrait à la place. Une condition de réouverture qu'on ne peut pas
remplir n'est pas une condition : c'est une porte peinte sur un mur.
