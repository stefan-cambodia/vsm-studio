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
| **Saxophone, hautbois** | `vsm.cone` (modélisé), profils GM (échantillonnés) | **couvert depuis le 01/09/2026** — anche sur perce conique, rangs pairs mesurés (h2/h1 0,37, § 14) ; la sixième mesure a trouvé le couplage fautif là où cinq topologies avaient échoué |
| **Flûte** | profils multisample (FR3/GU/MS-Flute) | **couvert par échantillons** — le modèle à jet reste hors build, résultat négatif mesuré (ARCHITECTURE § 44 : sa boucle non inversante offre au continu le plus fort gain) |
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
> frottée. ~~Les cuivres et les bois restent seuls non couverts~~ — les
> cylindriques par `vsm.wind`, puis, le 01/09/2026, les CONIQUES par
> `vsm.cone` (§ 14) : **plus aucune ligne du tableau n'est vide.** La
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
| Tuyau à anche BATTANTE | `vsm.wind` (cylindre), `vsm.cone` (perce conique) | **fait** — § 11 |
| **Anche LIBRE** | `vsm.reed` (harmonium, accordéon) | **fait le 02/09** — § 13 ; seule machine dont la pression change la HAUTEUR |
| Objet frappé à rapports libres (1D) | `vsm.modal` (corde ↔ barre) | **fait** |
| **Objet frappé à DEUX dimensions** | `vsm.membrane` (timbale ↔ tabla chargé) | **fait le 02/09** — rapports de Bessel, hors de portée de `vsm.modal` |
| **Cordes SYMPATHIQUES** | `vsm.sitar` (onze cordes jamais pincées, chevalet plat) | **fait le 02/09** — comble un trou que trois documents constataient |
| **Clavier PINCÉ, sans vélocité, à REGISTRES** | `vsm.harpsichord` (sautereau, 8'/4', jeu de luth, frôlement au relâchement) | **fait le 03/09** — § 22 ; la seule machine qui refuse la vélocité au bit près et qui sonne AU relâchement |
| **Archet SANS FIN, bourdons sans clavier, chevalet qui claque** | `vsm.hurdygurdy` (roue, inertie, bourdons, chien) | **fait le 03/09** — § 23 ; la vélocité n'y fait pas la force mais le rythme |
| **Corde dont la table est une PEAU** | `vsm.banjo` (corde de `vsm.string` sur une banque de modes de Bessel) | **fait le 03/09** — § 24 ; la peau chante ses modes quelle que soit la note, et mange la corde |
| Waveshaping (spectre commandé) | `vsm.chebyshev` | **fait** |
| **Synthèse balayée** | `vsm.scanned` (chaîne de masses, timbre en temps réel) | **fait le 02/09** |
| **Lecture de BANDE** | `vsm.mellotron` (la bande finit, une par touche) | **fait le 02/09** — un COMPORTEMENT, pas un timbre |
| Granulaire, vectoriel, stochastique | `vsm.granular`, `vsm.vector`, `vsm.stochastic` | **fait** |
| Formants (voix) | `vsm.vocal` | **fait** |
| Double couche + modulation par voix | `vsm.cs80` | **fait** |

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
- ~~**Le CS-80.** Célèbre, magnifique — et très coûteux à faire honnêtement
  (double couche complète, sensibilité polyphonique à la pression, rubans).
  À garder pour plus tard, en le faisant bien plutôt qu'à moitié.~~
  **FAIT LE 02/09/2026, et « plus tard » avait une raison précise qui a
  cessé d'être vraie** : deux des trois obstacles sont tombés le jour même.
  Le moteur livre désormais tout le MIDI non-note aux machines (D0.5),
  pression polyphonique comprise, et le § 10 du CDC nouvelle-machine a fixé
  la doctrine des contrôleurs. Le troisième — les rubans — est un
  contrôleur PHYSIQUE : il n'entre pas dans une machine logicielle sans
  matériel pour le porter, et `vsm.cs80` le dit plutôt que de le simuler
  par un potentiomètre de plus. La machine apporte au parc ce qu'aucune
  autre n'avait : **deux couches complètes par voix**, et **une modulation
  PAR VOIX** — la pression sur une touche ouvre le filtre de cette voix et
  d'elle seule, ce que le test mesure en tenant deux notes et en n'en
  pressant qu'une.
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

### Deux familles de plus, sur instruction, et au titre que le § 7 autorise (02/09/2026)

L'utilisateur demande de nouvelles machines. La règle du § 7 s'applique telle
qu'écrite : élargir le catalogue est légitime « pour le plaisir de jouer,
jamais en s'en réclamant pour la reconstruction » — et le critère du § 9
reste la FAMILLE de synthèse absente, pas le nom célèbre. Deux familles
manquent réellement au parc, et ce sont elles qu'on ajoute :

| Machine | Famille ouverte | Ce qu'aucune machine du parc ne sait faire |
|---|---|---|
| `vsm.vector` | **synthèse vectorielle** (type Prophet VS / SY22) | quatre timbres aux coins d'un carré, un point qui s'y déplace : le TRAJET est le timbre — la couleur bouge sans qu'aucun filtre ne bouge |
| `vsm.granular` | **synthèse granulaire** | le son comme NUAGE de grains fenêtrés — densité, taille, dispersion de hauteur et de temps ; entre la note et la texture, un continuum qu'aucune machine n'offre |

Ce qui n'est PAS promis : un gain de reconstruction. Si l'une d'elles gagne
un jour un arbitrage, tant mieux, et le rapport le dira ; aucune case de
couverture n'en dépend.

**CE QUI EST PROMIS, EN REVANCHE : QU'ELLES NE COÛTENT RIEN.** Le § 7 permet
d'élargir le catalogue pour le jeu, mais une machine de plus est une
candidate de plus à chaque arbitrage — donc du temps, et une chance de plus
qu'un mauvais choix soit fait. La règle que ces ajouts se donnent, écrite
avant la mesure : **la distance globale d'un morceau étalon ne doit pas
augmenter quand le parc s'élargit**, à conditions par ailleurs identiques.
Premier verdict, déjà rendu : `vsm.cone`, `vsm.vector` et `vsm.granular`
portées de 33 à 37 machines n'ont rien changé sur *Us and Them* (0,1907 →
0,1910, du bruit — v5 contre v7 au § 5 duodecies de la feuille de route
fusion). Le CDC nouvelle-machine s'applique en entier
(tests, trait distinctif mesuré, identités, façade, empreinte).

**Second verdict, et il dépasse la règle : le parc à 39 machines GAGNE**
(0,1910 → **0,1822**, −4,6 % sur *Us and Them*). `vsm.cs80`, ajoutée pour
le jeu et sans rien promettre, **gagne la piste de basse dès sa première
course** — et elle la gagne au MÉLANGE alors qu'elle est **DEUX FOIS PLUS
LOIN au stem** : 0,3623 contre 0,1851 pour `vsm.multisample`, qui avait
gagné l'arbitrage avant d'être écartée.

> **Correction du 02/09/2026.** Ce paragraphe a d'abord annoncé que les deux
> machines étaient « identiques au dix-millième » au stem (0,1851 contre
> 0,1851). C'était FAUX, et le chiffre venait d'une panne muette de
> `rapport.json` : le champ `distance` d'un stem n'était pas recalculé quand
> le verdict du mélange remplaçait la machine, si bien que le rapport publiait
> le score de la machine ÉCARTÉE sous le nom de la nouvelle. Les deux valeurs
> étaient identiques parce que c'était **le même nombre**, pas parce que les
> machines étaient à égalité. Le défaut est corrigé dans
> `aligner_rapport_sur_projet`, et la conclusion en sort renforcée : le
> mélange ne fait pas que départager deux ex æquo, il **renverse** un écart
> de un à deux. `vsm.modal`, écartée au verdict, se place tout de même
devant `vsm.string` au stem comme au morceau. Ce que ces chiffres disent,
et qui vaut mieux que la règle qu'ils vérifient : **un vivier plus large
ne dilue pas l'arbitrage, il lui donne des candidates que le mélange
saura départager** — à condition que le verdict ait le droit d'y revenir
(le point fixe H5, dont c'est ici le premier gain global mesuré).

**TROISIÈME VERDICT, ET IL EST NÉGATIF : LE VIVIER ÉLARGI COÛTE
(02/09/2026).** La règle de ce paragraphe — « la distance globale d'un morceau
étalon ne doit pas augmenter quand le parc s'élargit » — vient d'être
VIOLÉE, et il faut le dire avant tout le reste, parce que c'est la règle que
ces ajouts s'étaient donnée.

La course **v12** reprend v9 en tout point : mêmes stems, mêmes budgets, même
métrique, trois tours de verdict, aucun modèle appris — la comparaison des
deux provenances ne montre **aucune option différente**. Seul le vivier change,
34 candidates contre 36, `vsm.chebyshev` et `vsm.scanned` étant entrées.
Résultat : **0,2112 contre 0,1822, soit +15,9 %.**

**Ce que la mesure dit, et ce qu'elle ne dit pas.** Elle ne dit pas que les
deux machines ajoutées sont mauvaises : sur la piste de basse, v12 retient
`vsm.multisample` à 0,185 au stem, c'est-à-dire aussi bien que le `vsm.cs80`
retenu par v9 (0,1851). Elle dit que **le CHEMIN a changé**. Les candidates
supplémentaires modifient le classement des « machines suivantes remises en
jeu au verdict du mélange » — v12 y présente `psg`, `wind` et `cs80` pour la
basse là où v9 présentait d'autres —, si bien que le point fixe explore une
autre suite d'états et s'arrête sur un optimum local moins bon. C'est le
défaut connu de tout algorithme glouton : **plus de candidates, ce n'est pas
un meilleur résultat, c'est un chemin différent.**

**Conséquence pour la règle du § 7, et elle est franche.** « Une machine de
plus ne coûte rien » est FAUX, mesuré à +15,9 % sur *Us and Them*. La règle
correcte est plus modeste : *une machine de plus ne coûte rien tant qu'elle ne
déplace pas l'arbitrage ; dès qu'elle entre dans les finalistes, elle change
le chemin, et le chemin peut être pire.* Un vivier qui s'élargit doit donc
être mesuré à CHAQUE ajout, et non pas une fois pour toutes — et les six
machines livrées le 02/09 devront passer cette épreuve (course v13, 41
candidates) avant qu'on puisse dire quoi que ce soit de leur valeur.

**Ce que cela ne remet PAS en cause** : le point fixe (H9, −9,5 % à vivier
constant) et les traits mesurés de chaque machine, qui sont des propriétés du
son et non de l'arbitrage. Une machine peut être juste, utile au musicien, et
nuire à une recherche automatique — ce sont deux mérites différents, et le
§ 7 les confondait.

**Et le témoin a couru : c'est bien le POINT FIXE qui porte le gain.** H9,
tranchée le 02/09, compare v9 à un v10 identique sauf `--tours-verdict 1` :
**0,1822 contre 0,2013**, soit −9,5 % pour le seul droit de revenir sur un
verdict. La conclusion du paragraphe précédent doit donc se lire avec sa
condition, et non sans elle : *un vivier plus large ne dilue pas
l'arbitrage — à condition que le verdict ait le droit d'y revenir.* Sans
ce droit, les candidates supplémentaires sont là mais ne sont jamais
choisies, `vsm.cs80` étant à égalité parfaite au stem avec la machine
qu'elle finit par battre au mélange. ~~Savoir si le vivier élargi COÛTE
quand le verdict ne repasse pas reste ouvert et demande un témoin v11~~
**TRANCHÉ le 02/09/2026 : le témoin v11 a couru, et le vivier GAGNE.**
À un seul tour de verdict et six finalistes, le vivier de v10
(34 machines) rend 0,1910 contre 0,1970 pour celui de v7 (32 machines,
sans `vsm.cs80` ni `vsm.modal`) — les deux machines de plus rapportent
3,0 %. La règle de ce § 7 tient donc sans réserve au régime actuel du
goulot ; le coût que v10 avait montré (0,2013) venait du goulot de trois
finalistes, pas du vivier (ROADMAP-fusion, « LE TÉMOIN v11 A COURU »).

**Et deux autres ont suivi le 02/09.** `vsm.cs80` sort le CS-80 de la
réserve du § 9, parce que sa condition d'attente — la pression
polyphonique, qu'aucun moteur ne livrait — a été levée le jour même : elle
ouvre l'architecture **à deux couches par voix** et la **modulation
par-voix**, la première du parc. `vsm.modal` ouvre la **synthèse modale
d'objets frappés à rapports libres** : `vsm.additive`, la plus proche,
plafonne à 2,003·f0 pour son second rang (loi de la corde raide) là où une
barre a le sien à 2,76·f0 — la frontière entre les deux machines est donc
mesurée, pas affirmée.

**Puis deux autres encore, et elles ouvrent des familles entières.**
`vsm.chebyshev` apporte le **waveshaping par polynômes de Tchebychev**
(Arfib et Le Brun, 1979), dont la propriété n'a pas d'équivalent dans le
parc : `T_n(cos θ) = cos(n·θ)` **exactement**, donc on écrit le spectre
qu'on veut et on l'obtient — poids sur le seul rang 3, et le rendu ne
contient que l'harmonique 3. Le trait musical en découle sans qu'on ait
rien à simuler : sous l'amplitude 1 les rangs élevés s'effondrent en `A^n`,
si bien qu'une note qui décroît perd ses aigus d'elle-même, **sans
enveloppe de filtre**. Elle a coûté un défaut instructif, consigné dans le
code : `T_n(0)` vaut ±1 pour n pair, et la machine sortait 0,30 de continu
AU REPOS — le silence n'était pas silencieux, et aucune mesure de niveau ne
l'aurait montré. Elle a coûté aussi une identité sémantique inventée
(`additive.partial.N.level`, annoncée comme un réemploi alors que
`vsm.additive` n'a aucune amplitude par rang) : une identité inventée en
croyant réemployer est pire qu'une identité neuve assumée, parce qu'elle
fait croire à un preset qu'il voyagera.

`vsm.scanned` apporte la **synthèse balayée** (Verplank, Mathews et Shaw,
2000), et son trait distinctif dit à lui seul pourquoi elle méritait
d'exister : **la vitesse d'évolution du timbre ne dépend pas de la note
jouée.** La forme d'onde y est la photographie d'une chaîne de 32 masses
qu'on a pincée et qui continue d'osciller pour son compte, à quelques
hertz ; la note ne décide que de la VITESSE À LAQUELLE ON LA LIT. Deux
machines du parc font bouger leur timbre toutes seules, et aucune de cette
façon : `vsm.wavetable` promène un pointeur entre des tables FIGÉES,
`vsm.stochastic` déplace ses points de brisure par une marche aléatoire —
du bruit borné, sans mémoire ni inertie, et qui change à chaque PÉRIODE.
Le test mesure exactement cela, et il a fallu deux essais pour le mesurer
honnêtement : deux notes distantes de deux octaves voient leur timbre
monter et descendre **aux mêmes instants** (corrélation des calendriers de
centroïde : 0,82).

Elle a coûté trois leçons de banc, toutes payées à la mesure :
- **La chaîne donne la FORME, pas l'amplitude.** Laissée à elle-même elle
  se vide de son énergie : rms 0,038 → 0,00000 en deux secondes et demie,
  la note tenue mourait toute seule alors que le sustain tenait. L'état
  est donc RENORMALISÉ à chaque pas (positions et vitesses ensemble, pour
  ne pas fausser la dynamique) — un entretien, au sens de l'archet.
- **Une mauvaise métrique a failli faire condamner la machine.** Le test
  mesurait « la brillance » comme l'énergie de la dérivée du signal : elle
  restait plate à 0,0004 d'un bout à l'autre, et concluait que rien
  n'évoluait. La dérivée d'un signal périodique est dominée par sa
  fréquence de LECTURE, pas par sa forme ; le contenu harmonique réel,
  lui, voyageait de 0,07 à 1,18. Mesurer un timbre demande de regarder les
  rangs, pas la pente.
- **L'hypothèse simple était fausse, et la mesure l'a dit.** On attendait
  que le timbre soit IDENTIQUE à instant égal pour deux notes, puisque la
  chaîne est unique et partagée. Il ne l'est pas : la note grave est
  franchement plus brillante (centroïde 2,4 contre 1,6), parce que sa
  période de lecture étant quatre fois plus longue, la forme a le temps de
  bouger PENDANT une lecture. Le niveau de brillance dépend donc de la
  note ; son CALENDRIER, non — et c'est cela, et cela seul, le trait de la
  famille. Le test dit désormais ce qui est vrai.

Le parc passe à **41 machines** (965 paramètres nommés, 1 302 tests
verts) — le compteur du dépôt recense toutes les entrées du registre, y
compris `vsm.testtone`, qui n'est pas un instrument mais sert de mire.

**Et une septième, qui n'apporte pas un timbre mais un COMPORTEMENT.**
`vsm.mellotron` ouvre la **lecture de bande**. Le parc lit déjà des
échantillons de trois façons (`vsm.sampler`, `vsm.multisample`,
`vsm.pcmhybrid`), et toutes les trois se comportent comme un ordinateur :
la note tenue dure aussi longtemps qu'on la tient, et transposer relit
l'enregistrement plus vite. Un Mellotron ne fait ni l'un ni l'autre, et
c'est de là que vient sa manière de jouer :

1. **La bande FINIT.** Huit secondes sous chaque touche, puis plus rien —
   l'enveloppe a beau tenir son sustain, il n'y a plus de bande. Aucune
   autre machine du parc ne s'interrompt d'elle-même, et le fait se lit
   jusque dans son empreinte de non-régression, dont la sixième fenêtre
   de RMS vaut exactement 0,000000.
2. **Une bande par touche, donc pas de transposition** : jouer deux
   octaves plus haut ne raccourcit pas la durée disponible, là où un
   échantillonneur qui transpose passerait de huit secondes à deux.
3. **Chaque brin a son défaut d'entraînement** : deux touches tenues
   ensemble ne pleurent pas en mesure — le contraire exact d'un LFO de
   vibrato, qui les ferait onduler à l'unisson.
4. **Le retour de la tête prend du temps** : rejouer avant la fin du
   rembobinage reprend la lecture où elle en était, et la seconde note
   dure moins que la première.

**L'APPROXIMATION EST GROSSE ET ELLE EST ASSUMÉE** (§ 8, statut
« inspiré ») : le CONTENU de la bande n'est pas un enregistrement mais une
petite banque de partiels avec son souffle. Ce que cette machine apporte
au parc est le comportement du TRANSPORT, non le timbre d'un orchestre de
1963 ; qui veut le timbre passe par `vsm.multisample` et ses 141 profils,
qui veut le comportement vient ici. Le dire est la condition pour que la
machine ne mente pas sur ce qu'elle est — et c'est aussi pourquoi il ne
faut rien attendre d'elle à l'arbitrage tant qu'un profil ne lui donne pas
un vrai contenu.

**Elle a coûté une décision de nommage, et pas une coquetterie.** Elle
s'est d'abord appelée `vsm.tape`, jusqu'à ce que la suite de tests fasse
apparaître, côte à côte, `tape_mix_zero_is_passthrough` — qui appartient à
l'EFFET d'insert « Tape », une saturation de bande. Deux objets presque
homonymes qui font des choses opposées (l'un lit une bande, l'autre la
sature) : le musicien qui ouvre son menu ne pouvait pas les distinguer.
La machine porte donc le nom de l'instrument, comme `vsm.minimoog` ou
`vsm.cs80`, et ses identités sémantiques gardent le préfixe `tape.` pour
le transport, distinct de `effect.tape.` pour la saturation.

Le parc passe à **42 machines** (980 paramètres nommés, 1 314 tests
verts).

**Et une huitième, qui comble un trou que TROIS documents constataient sans
le combler.** « Aucune résonance sympathique entre notes » est une
approximation assumée de `vsm.piano`, écrite dans son en-tête ; le § 28
d'ARCHITECTURE range la question du côté de la modélisation en la renvoyant
à cette machine-là ; le CDC du multisample l'écarte à son tour. Résultat :
**aucune machine du parc ne produisait de son sur une corde qu'on n'a pas
touchée**, alors que c'est la définition d'une famille entière — sitar,
tampura, viole d'amour, hardanger. `vsm.sitar` la comble, avec deux traits
mesurés :

1. **Les cordes sympathiques survivent à la note.** Tout relâché, la corde
   jouée étouffée depuis deux secondes, l'instrument sonne encore. Et la
   réponse est SÉLECTIVE, ce qui est le phénomène même et non une réverbe :
   une note accordée sur une corde la met en branle, une note à un demi-ton
   de là ne la trouve pas (le test exige un facteur trois entre les deux).
2. **Le jawari fait dépendre le timbre de l'AMPLITUDE, pas du temps.** Le
   chevalet du sitar est plat : la corde qui vibre fort vient le toucher une
   fois par cycle, et chaque contact est un choc. Mesuré sur la bande
   5–12 kHz, du plus doux au plus fort : **1,00 · 1,67 · 22,7 · 34,8 ·
   29,9**. À vélocité 6, le même réglage ne fait RIEN — pas « presque
   rien » : exactement 1,00. Aucune enveloppe de filtre ne produit cela,
   puisqu'une enveloppe suit le temps et ferait le même écart à toutes les
   vélocités.

**ELLE A COÛTÉ TROIS MÉTRIQUES FAUSSES, ET C'EST LA LEÇON À GARDER.** Le
modèle du chevalet a été refait deux fois parce que la mesure disait qu'il
assombrissait le son : une compression douce (centroïde 1495 → 1448 Hz),
puis un écrêtage dur (0,971 à quatre secondes de décroissance, en retirant
un cinquième de l'énergie). Ces deux-là étaient bien de mauvais modèles —
arrondir ou raboter une crête ENLÈVE des harmoniques. Mais le troisième,
lui, était JUSTE, et la métrique continuait de le condamner : le centroïde
spectral, tronqué à 8 kHz, DESCENDAIT (1941 → 1597 Hz). Le spectre par
bandes a montré pourquoi — le contact multiplie par 35 l'énergie de 5 à
12 kHz, mais épaissit aussi le grave d'un cinquième, et c'est ce grave qui
tirait le centroïde vers le bas.

C'est la troisième fois en deux jours qu'un instrument de mesure fait
accuser un modèle correct — après le saxophone (§ 44 : l'estimateur qui
lisait les jupes à la fréquence DEMANDÉE) et la synthèse balayée (l'énergie
de la dérivée, aveugle à l'évolution du timbre). **La règle qui s'en dégage
mérite d'être écrite une fois pour toutes : avant de conclure qu'un modèle
est faux, vérifier que la mesure regarde là où l'effet est censé se
produire.** Un chiffre agrégé — un centroïde, une énergie totale — cache
par construction ce qui bouge dans une bande étroite.

Le parc passe à **43 machines** (993 paramètres nommés, 1 326 tests
verts).

**Et une neuvième, dont l'existence a été DÉMONTRÉE avant d'être écrite.**
`vsm.membrane` ouvre l'objet à DEUX dimensions — le parc n'en avait aucun.
La question qui se posait était légitime : pourquoi ne pas ajouter
« membrane » au réglage `Material` de `vsm.modal`, qui est déjà une banque de
résonateurs à rapports libres ? **Le calcul, fait sur le code de `vsm.modal`
et non sur une intuition, dit que c'est impossible.** Son `ratioOf` interpole
entre la corde (rapport `n`) et la barre libre-libre (`((2n+1)/3)²`), puis
multiplie par `spread^((n-1)/10)` avec `spread` dans [0,5 ; 2] : pour le
second partiel, cela couvre exactement **[1,866 ; 2,978]**. Une membrane
circulaire a le sien à **1,593**, le rapport des deux premiers zéros de
Bessel. Hors de portée, quel que soit le réglage — et de même pour le
troisième (2,136 contre un intervalle qui commence à 2,61).

La raison est structurelle et vaut d'être écrite : une corde et une barre
sont des objets à UNE dimension, dont les modes s'indexent par un seul
entier ; une membrane est à deux, et ses modes s'indexent par un couple
`(m, n)`. Ce n'est pas un point de plus sur le segment corde↔barre, c'est un
autre espace.

Trois traits, et le deuxième est un fait acoustique remarquable :
- **Les rapports de Bessel** : second mode à 1,593·f0, ce qui fait qu'une
  timbale sonne « sans note » franche.
- **LA CHARGE REND LA PEAU ACCORDABLE — le miracle du tabla.** Le disque de
  pâte collé au centre (le *syahi*) alourdit la membrane et déplace ses modes
  vers des ENTIERS ; C. V. Raman l'a montré en 1934, et c'est pour cela qu'un
  tabla joue des notes là où une timbale joue des bruits accordés. Mesuré aux
  deux bouts du réglage `Loading` : **1,59 à charge nulle, 2,00 à charge
  pleine**, et le trajet entre les deux est continu. Aucune autre machine du
  parc ne rend harmonique un objet qui ne l'était pas.
- **Frapper au centre n'est pas frapper au bord** : un mode à `m ≥ 1`
  diamètres nodaux a un NŒUD au centre et ne s'excite pas là. Le second
  partiel disparaît donc quand on frappe au milieu (mesuré à moins de 5 % de
  son niveau au bord) — c'est la différence entre le *na* sourd et le *tin*
  chantant d'un tabla, et elle est binaire.

**Un paramètre mort retiré avant livraison.** La machine exposait d'abord un
neuvième réglage, `Velocity Sensitivity`, que `process` ne lisait nulle part :
la vélocité agissait déjà sur le niveau et sur la dureté du maillet. Un
réglage qui ne fait rien est pire qu'un réglage absent — il ment au musicien
et coûte une dimension à la recherche —, exactement ce que le § 33
d'ARCHITECTURE avait déjà conclu pour l'évasement de `vsm.wind`.

Le parc passe à **44 machines** (1 001 paramètres nommés, 1 337 tests
verts).

## 13. H11 — l'ANCHE LIBRE, et pourquoi ce n'est pas la sixième tentative de vent (écrite avant sa mesure, 02/09/2026)

Le § 11 conclut, après cinq échecs, qu'« une sixième topologie ne serait pas
une tentative de plus, ce serait la même erreur pour la sixième fois », et il
dit précisément où est l'erreur : **« ce n'est ni la forme de la perce ni la
nature de l'excitateur qui bloque, c'est la formulation du COUPLAGE entre
l'excitateur et la colonne d'air. »** Cette phrase est le critère qui autorise
ou interdit toute nouvelle tentative, et il faut donc y répondre AVANT
d'écrire une ligne.

**Une anche LIBRE n'a pas de colonne d'air.** C'est toute la différence, et
elle est structurelle. Dans une clarinette ou un saxophone, l'anche bat contre
une table et sa fréquence est imposée par le TUYAU ; c'est ce couplage
excitateur↔colonne qui a divergé ou refusé de s'amorcer cinq fois. Dans un
harmonium, un accordéon ou un harmonica, la lame bat DANS son cadre, sans le
toucher, et **sa fréquence est la sienne propre** — celle d'une lame encastrée.
Le corps de l'instrument ne fait que rayonner. Il n'y a donc pas de guide
d'ondes du tout : un seul résonateur mécanique entretenu par un flux, ce qui
est une boucle LOCALE, bornable, sans les deux cents échantillons de retard
qui rendaient les précédentes intraitables. Le critère du § 11 est respecté :
ce n'est pas une sixième formulation du même couplage, c'est l'absence du
couplage en cause.

**LE TRAIT À MESURER, et il est un CONTRASTE avec une machine existante.**
Fletcher et Rossing le donnent : sous pression croissante, une anche BATTANTE
monte en fréquence (elle se raidit contre sa table) tandis qu'une anche LIBRE
**descend** légèrement. Le parc a déjà la première (`vsm.wind`) ; la mesure qui
tranche est donc la dose-réponse de la hauteur à la pression, prise sur les
deux machines avec le même protocole.

- **Succès de H11** : la machine s'amorce sur toute la tessiture (hauteur à
  ±20 cents de la note demandée), et sa hauteur BAISSE quand la pression monte,
  là où celle de `vsm.wind` monte. La famille de l'anche libre entre au parc.
- **Échec de H11** : pas d'amorçage, divergence, ou hauteur qui ne suit pas le
  sens annoncé. Alors le code part **hors du `CMakeLists`**, comme
  `audio/plugins/flute/`, et le résultat négatif est écrit ici avec son
  chiffre. Un échec conservé et daté vaut mieux qu'un échec oublié : c'est ce
  qui a évité la sixième tentative de vent.

### H11 EST TRANCHÉE : SUCCÈS — la famille du vent entre enfin (02/09/2026)

**Ce qui avait échoué cinq fois réussit du premier coup, et l'explication
était dans le diagnostic du § 11.** `vsm.reed` s'amorce sur toute la tessiture
et sonne JUSTE : mesuré sur les notes 45, 52, 57, 64 et 69, l'écart à la note
demandée reste **sous 20 cents** (relevé : +1 à −6). Rien à voir avec
`vsm.flute`, dont « la fréquence se fige à 1 412 Hz quelle que soit la note ».
La raison est celle qu'on avait écrite avant de coder : il n'y a **pas de
colonne d'air** à coupler, donc pas de boucle longue à borner — la fréquence
est celle de la lame, que rien ne dispute.

**Le trait est mesuré, et la dose-réponse est monotone.** Note 57, sur la
course du soufflet : **+3,1 · 0,0 · −3,1 · −6,8 · −8,9 cents**. Souffler plus
fort fait DESCENDRE la note, ce qu'aucune autre machine du parc ne fait.

**MAIS L'HYPOTHÈSE ÉTAIT À MOITIÉ FAUSSE, ET C'EST ÉCRIT PLUTÔT QUE CORRIGÉ
EN SILENCE.** H11 annonçait un contraste de SENS : l'anche battante devait
MONTER en se raidissant contre sa table. Mesurée au même protocole,
`vsm.wind` ne monte pas — elle **ne bouge pas du tout** (+0,5 à +1,0 cent,
c'est-à-dire le bruit de la mesure), sa hauteur étant imposée par la longueur
du tuyau et non par son anche. Le contraste existe donc, mais il oppose une
machine SENSIBLE à une machine INSENSIBLE. Une hypothèse à demi vérifiée
qu'on reformulerait après coup ne vaudrait plus rien ; celle-ci est donc
consignée avec ce qu'elle a prédit juste et ce qu'elle a prédit faux.

**Trois erreurs de physique payées à la mesure**, toutes attrapées par la
sonde et aucune par l'oreille :
- **Le continu, encore, et c'est le piège du § 44 repayé.** Un débit redressé
  est positif par construction ; le premier essai retirait sa composante
  continue par une CONSTANTE, ce qui marche au régime établi et ment partout
  ailleurs. À faible pression, la lame n'oscillait pas du tout et la sortie
  valait −0,25 constant, soit un rms de 0,25 qu'on aurait pris pour du son.
  Mot pour mot la faute de `vsm.flute`. Un bloqueur du premier ordre, lui, ne
  peut pas se tromper.
- **Redressement DOUBLE au lieu de simple.** Le passage s'ouvrait des deux
  côtés (`|y|`), en croyant décrire une lame qui traverse son cadre. Mesure
  au spectre : toute l'énergie à 2·f0 et **rien au fondamental** — la machine
  jouait une octave au-dessus. Une anche libre laisse passer l'air d'un seul
  côté.
- **La charge d'air dix fois trop forte** : −165 cents sur la course, soit un
  ton et demi. Ce n'est pas une machine expressive, c'est une machine fausse.
  Ramenée à 1,2 % de raideur et CENTRÉE sur une pression de référence, pour
  que la note demandée sorte juste au réglage normal.

**Et une leçon d'instrument de mesure, la quatrième de la journée.** Sur le
même signal, l'autocorrélation annonçait −700 cents quand le spectre disait
+1200. Les deux ne pouvaient pas avoir raison ; c'est le spectre qui l'avait,
l'autocorrélation accrochant un mauvais pic. Plus tard, la même
autocorrélation à décalage ENTIER rapportait « aucune variation » sur la note
69 : à 440 Hz, un décalage d'un échantillon vaut 158 cents, et l'effet en fait
dix. Il a fallu un balayage fin du pic de magnitude pour voir quoi que ce soit.

Le parc passe à **45 machines** (1 012 paramètres nommés, 1 349 tests verts).

## 14. H12 — la PLAQUE, dont la brillance MONTE après la frappe (écrite avant sa mesure, 02/09/2026)

**Ce qui existe déjà, et pourquoi cela ne suffit pas.** Le parc sait faire une
cymbale : `vsm.drums` la rend « par un cluster de partiels aux rapports
irrationnels plus du bruit filtré », et c'est écrit dans son en-tête. Cette
approximation est bonne pour un kit de batterie, où la cymbale dure une
seconde et sert de ponctuation. Elle est STATIQUE : chaque partiel décroît
pour son compte, donc le son ne peut que s'assombrir avec le temps, comme
tout ce que contient le parc.

**Or un gong fait le contraire, et c'est son trait le plus reconnaissable.**
Frappé fort, un tam-tam est d'abord sourd, puis sa brillance MONTE pendant
plusieurs secondes avant de retomber. Le mécanisme est un couplage NON
LINÉAIRE entre modes : les grandes amplitudes de flexion convertissent
l'énergie des modes bas vers les modes hauts, d'autant plus vite que la
frappe est forte. Rossing et Fletcher le décrivent, et c'est ce qui distingue
un tam-tam d'une plaque idéale — qui, elle, s'assombrirait.

**Aucune machine du parc ne peut produire cela**, et ce n'est pas une question
de réglage : `vsm.modal`, `vsm.membrane`, `vsm.perc` et `vsm.drums` ont tous
des modes INDÉPENDANTS, dont les amplitudes ne font que décroître. Il n'y a
aucun chemin par lequel l'énergie d'un mode grave pourrait alimenter un mode
aigu. C'est une différence de STRUCTURE, pas de paramétrage.

**La mesure qui tranche**, et elle a deux moitiés parce que le phénomène est
non linéaire :
- **La montée existe** : le rapport de l'énergie aiguë à l'énergie grave est
  plus élevé une seconde après la frappe qu'immédiatement après, pour une
  frappe FORTE. Partout ailleurs dans le parc, ce rapport ne peut que baisser.
- **Elle dépend de la FORCE** : à frappe faible, le transfert est négligeable
  et le son s'assombrit normalement, comme n'importe quelle plaque. Sans cette
  seconde moitié, un simple filtre qui s'ouvrirait avec le temps passerait le
  premier test.

- **Succès de H12** : les deux moitiés sont mesurées. La famille entre au parc,
  et `vsm.drums` garde sa cymbale de kit — les deux ne visent pas le même
  usage.
- **Échec de H12** : le transfert ne s'entend pas, ou il diverge. Le code part
  hors du `CMakeLists` comme `audio/plugins/flute/`, avec son chiffre.

### H12 EST TRANCHÉE : SUCCÈS, les deux moitiés mesurées (02/09/2026)

`vsm.plate` rend le tam-tam, et le transfert s'entend franchement :

| couplage | frappe | brillance à 0,2 s | à 1,5 s | rapport |
|---|---|---|---|---|
| 0,0 (témoin) | forte | 0,00030 | 0,00026 | **0,88** |
| 0,6 | forte | 0,00096 | 0,12307 | **128,7** |
| 0,6 | douce | 0,00044 | 0,00102 | **2,3** |

**Première moitié** : à couplage nul, la machine s'assombrit (0,88) comme tout
le reste du parc ; dès qu'on couple, elle s'éclaircit. **Seconde moitié** : le
même réglage donne 128,7 sur une frappe forte et 2,3 sur une frappe douce, le
transfert étant quadratique. Un filtre qui s'ouvrirait avec le temps aurait
passé le premier test et échoué au second, ce qui est exactement pourquoi il
en fallait deux.

**Le témoin est du MÊME CODE** — le couplage à zéro est une valeur de la
course, pas une constante éditée entre deux passes.

**La course entière a été vérifiée bornée**, parce qu'un transfert non
linéaire est le genre de mécanisme qui diverge et que ce dépôt en a déjà payé
cinq : de 0,0 à 1,0, le pic reste entre 0,50 et 0,57 et aucun échantillon
n'est non fini. Le § 33 exige qu'une machine faite pour être CHERCHÉE n'ait
pas de zone inutilisable sur la course d'un de ses réglages ; c'est un test à
part entière.

Le parc passe à **46 machines** (1 019 paramètres nommés, 1 361 tests verts).

### DÉCISION : le vivier continue de s'élargir, et la mesure se poursuit à côté (02/09/2026)

Six familles sont entrées au parc le 02/09 — synthèse balayée, lecture de
bande, cordes sympathiques, membrane, anche libre, plaque. Le même jour, la
course v12 a mesuré que **le vivier élargi coûte** (+15,9 %), et l'hypothèse
H13 (feuille de route fusion) propose une cause : le goulot des finalistes,
fixé à trois quel que soit le nombre de candidates, alors que le classement au
stem peut se tromper d'un facteur deux.

**Ce chiffre n'arrête pas l'élargissement, et il ne le doit pas.** La question
avait d'abord été tranchée dans l'autre sens — « on arrête jusqu'au verdict de
H13 » —, et c'était une erreur de raisonnement qu'il vaut mieux écrire que
taire : elle confondait les deux motifs que ce même § 7 distingue depuis le
début. Une machine s'ajoute pour la COUVERTURE (combler un trou de
reconstruction) **ou pour le JEU** (donner un son que le musicien veut jouer),
et la distance globale ne mesure que le premier. `vsm.mellotron` ne prétend
pas améliorer une reconstruction : elle donne au parc un comportement de
transport que rien n'avait. Juger une telle machine à la distance d'*Us and
Them* revient à juger un violon au poids.

**ET v13 A TRANCHÉ, DANS L'AUTRE SENS : cinq machines de plus ne coûtent
RIEN (02/09/2026).** La course v13 reprend v12 en tout point, avec les cinq
machines livrées après elle (mellotron, sitar, membrane, reed, plate) : **41
candidates contre 36**, une seule variable. Résultat :

| course | candidates | distance globale |
|---|---|---|
| v9 | 34 | 0,1821509342551415 |
| v12 | 36 | **0,21123303926053802** |
| v13 | **41** | **0,21123303926053802** |

v12 et v13 sont identiques **à dix-sept décimales**. Cinq machines de plus
n'ont pas déplacé d'un bit ce que la chaîne décide.

**Les deux mesures ensemble disent la règle, et elles la disent mieux que ne
le faisait la formule d'origine.** Ce n'est pas le NOMBRE de candidates qui
coûte, c'est qu'une candidate ENTRE dans les finalistes remises en jeu au
mélange. `vsm.chebyshev` y est entrée en v12 (le journal la montre à 67,7 %
sur `other`) et le chemin a changé, pour le pire ; aucune des cinq de v13 n'y
entre, et la course est rigoureusement la même. La règle du § 7 tient donc
sous sa forme corrigée, désormais vérifiée dans les deux sens : *une machine
de plus ne coûte rien tant qu'elle ne déplace pas l'arbitrage.*

**Et le § 7 gagne sa seconde phrase, elle aussi mesurée : _un vivier qui
grandit demande un goulot qui grandit avec lui._** Le témoin v14 (H13 de la
feuille de route fusion) porte les finalistes de trois à six sur le même
vivier de 41 candidates : **0,1910 contre 0,2112**, soit −9,6 %, et la course
est plus RAPIDE (les réglages au mélange passent de 1199 à 525 secondes).
`MACHINES_AU_MELANGE` a donc été porté à 6. Le coût du vivier élargi n'était
donc pas dû aux machines ajoutées mais au tri qui les précédait.

**Conséquence pratique, et elle est rassurante** : une machine ajoutée pour le
JEU — un mellotron, une boîte à musique, une guimbarde — ne peut pas dégrader
une reconstruction tant qu'elle ne gagne aucun arbitrage. Élargir le vivier
pour jouer est gratuit ; ce qui se paie, c'est qu'une machine soit choisie à
tort, et c'est un problème de CRITÈRE, pas de catalogue. H13 (le goulot des
finalistes) reste donc la bonne piste.

**Ce qui reste vrai** : l'élargissement doit continuer d'être mesuré à chaque
ajout. La règle du § 7 garde donc sa forme corrigée — *une machine de
plus ne coûte rien tant qu'elle ne déplace pas l'arbitrage* — et le travail
sur H13 se poursuit en parallèle, parce que si le goulot est bien la cause, le
coût disparaîtra et les deux motifs cesseront de s'opposer.

Les courses en cours : v13 (41 candidates, trois finalistes) et v14 (41
candidates, six finalistes).

## 15. H15 — le CLAVICORDE : appuyer plus fort MONTE la note (écrite avant sa mesure, 02/09/2026)

**Le trait, et il est unique au clavier.** Sur un clavicorde, la tangente de
laiton ne rebondit pas comme un marteau de piano : elle frappe la corde ET
**reste en contact**, définissant elle-même la longueur vibrante. Appuyer plus
fort sur une touche déjà enfoncée tend donc la corde et **fait monter la
note** — c'est le *Bebung*, et c'est la seule façon de faire un vibrato sur un
instrument à clavier.

**Ce que le parc a, et ce qui lui manque.** `vsm.cs80` reçoit bien une
pression PAR VOIX, mais elle ouvre un filtre : le timbre change, la hauteur
non. `vsm.reed` déplace la hauteur sous la pression, mais **vers le bas**
(mesuré : +3,1 à −8,9 cents), la charge d'air alourdissant la lame. Aucune
machine ne la fait monter, et aucune ne relie la pression à la TENSION d'une
corde.

**Second trait, et il est aussi net** : relâcher la touche **étouffe
immédiatement**. La tangente quitte la corde, dont l'autre extrémité est
tressée de feutre : il n'y a ni résonance ni traîne. C'est le contraire du
piano (pédale, cordes sympathiques) et du sitar. Sur les autres machines à
corde du parc, le relâchement ouvre une décroissance ; ici il coupe.

**La mesure qui tranche :**
- **Bebung** : note tenue, pression de zéro à un, hauteur mesurée finement
  (balayage du pic de magnitude, pas d'autocorrélation à décalage entier —
  la leçon du banc de `vsm.reed`). La hauteur doit MONTER d'au moins dix
  cents, et de façon monotone.
- **Étouffement** : après le relâchement, l'énergie doit tomber sous le
  centième de son niveau en moins de 150 ms, quel que soit le réglage de
  décroissance de la corde — parce que ce n'est pas la corde qui décide, c'est
  le feutre.

- **Succès de H15** : les deux sont mesurés. Le parc gagne le seul clavier
  expressif en hauteur, et le contraste avec `vsm.reed` (qui descend) est
  mesuré au même protocole.
- **Échec de H15** : le code part hors du `CMakeLists`, avec son chiffre.

### H15 EST TRANCHÉE : SUCCÈS, les deux traits mesurés (02/09/2026)

**Le Bebung, sur la course de la pression, note 57 :**

| pression | 0,00 | 0,25 | 0,50 | 0,75 | 1,00 |
|---|---|---|---|---|---|
| écart | **0,0** | +7,3 | +14,5 | +22,0 | **+29,2 cents** |

Monotone, et d'une ampleur juste : une trentaine de cents colore une note
tenue sans la transposer, ce qui est exactement ce que le geste fait sur
l'instrument. Sans pression, la note est juste au cent près.

**Le contraste avec `vsm.reed` est désormais mesuré au même protocole**, et il
oppose deux mécaniques : une corde qu'on TEND monte (+29,2), une lame qu'on
ALOURDIT descend (−8,9). Le parc a les deux sens, et aucune autre machine ne
déplace la hauteur sous la pression — `vsm.cs80`, qui reçoit pourtant une
pression par voix, l'envoie à son filtre.

**L'étouffement** : 0,0074 avant le relâchement, **0,000000 cinquante
millisecondes après**, avec une corde réglée sur huit secondes de
décroissance. Ce n'est pas la corde qui décide, c'est le feutre — et c'est ce
qui sépare ce clavier de tous les autres instruments à corde du parc, où
relâcher OUVRE une décroissance.

Le parc passe à **47 machines** (1 027 paramètres nommés, 1 372 tests verts).

## 16. H16 — le VERRE FROTTÉ : un son qui met des SECONDES à naître (écrite avant sa mesure, 02/09/2026)

**Ce que le parc a déjà, et pourquoi cela ne suffit pas.** `vsm.string` sait
frotter : son archet applique la friction de Helmholtz à un guide d'ondes, et
le test mesure le cycle adhérence-décrochement. Mais une corde frottée
s'établit en quelques dizaines de millisecondes. Un verre frotté du doigt met
**une à trois secondes** à parler, et c'est ce que tout le monde reconnaît de
l'harmonica de verre : le son semble venir de nulle part, sans attaque.

**Le mécanisme est différent, pas seulement le réglage.** Un bol de verre est
un résonateur à Q très élevé — quelques modes seulement, très peu amortis —
et non une ligne à retard. L'énergie que le doigt lui donne à chaque
décrochement est minuscule devant celle qu'il faut accumuler ; le temps
d'établissement EST la conséquence du Q, et il dépend de la pression du doigt.
Il n'y a pas de guide d'ondes du tout, donc pas de boucle longue à borner —
la même raison qui a fait réussir `vsm.reed` là où cinq tentatives de vent
avaient échoué.

**Second trait, qui suit du même Q** : lâcher le verre ne l'arrête pas. Il
continue plusieurs secondes. C'est le contraire exact de `vsm.clavichord`,
livré le même jour, dont le feutre coupe le son en cinquante millisecondes —
et le parc aura mesuré les deux extrêmes au même protocole.

**La mesure qui tranche :**
- **L'établissement est LENT** : après l'attaque, l'énergie doit encore
  croître nettement entre 0,3 s et 1,5 s. Sur toute autre machine entretenue
  du parc, elle a atteint son régime bien avant.
- **Il dépend de la PRESSION** : plus le doigt appuie, plus le verre parle
  vite. Une dose-réponse, sans quoi on aurait seulement écrit une enveloppe
  d'attaque lente — qui, elle, mettrait le même temps à toutes les nuances.
- **Le relâchement ne coupe pas** : une seconde après, il reste de l'énergie.

- **Succès de H16** : les trois sont mesurés. Le parc gagne la friction sur
  résonateur, distincte de la friction sur corde.
- **Échec de H16** : pas d'auto-oscillation, ou établissement instantané. Le
  code part hors du `CMakeLists`, avec son chiffre.

### H16 EST TRANCHÉE : SUCCÈS, les trois traits mesurés (02/09/2026)

**L'établissement et sa dose-réponse**, note 69, rms mesuré :

| pression | à 0,3 s | à 1,5 s | rapport |
|---|---|---|---|
| 0,2 | 0,0067 | 0,2124 | **31,7** |
| 0,5 | 0,1186 | 0,2435 | 2,05 |
| 0,8 | 0,2403 | 0,2443 | 1,02 |
| 1,0 | 0,2443 | 0,2450 | **1,00** |

Pressé doucement, le verre met des secondes à parler ; pressé à fond, il est
établi dès la première demi-seconde. **Une enveloppe d'attaque lente aurait
donné la même colonne à toutes les pressions** — c'est cette dose-réponse, et
elle seule, qui distingue un instrument d'un déclencheur.

**Le relâchement ne coupe pas** : 0,2435 avant, 0,2242 une demi-seconde après,
0,1977 une seconde et demie après. Le parc a désormais les deux extrêmes
mesurés au même protocole — `vsm.clavichord`, livré le même jour, tombe à
0,000000 en cinquante millisecondes.

**Un défaut de niveau corrigé avant livraison** : le cycle limite de la
friction s'établit vers ±2, si bien que la machine saturait tout projet où on
l'ajoutait (pic 2,04). Ramenée à 0,38 en monophonie, elle est dans la plage du
reste du parc — et un test le vérifie sur toute la course de la pression,
plutôt que de s'en remettre au réglage.

Le parc passe à **48 machines** (1 035 paramètres nommés, 1 384 tests verts).

## 17. H17 — la GUIMBARDE : la seule machine qui refuse de suivre le clavier (écrite avant sa mesure, 02/09/2026)

**Le trait, et il est binaire.** Une guimbarde a une lame d'acier dont la
fréquence est FIXE : elle ne change pas, quoi que fasse le joueur. Ce qui
change, c'est la cavité buccale, qui filtre ce bourdon et fait ressortir tel ou
tel harmonique. **Le musicien ne joue pas des notes, il joue des FORMANTS sur
une note unique.**

**C'est le miroir exact de `vsm.vocal`**, et le parc aura les deux faces d'une
même idée. Sur la voix, la hauteur chantée bouge et les formants restent où ils
sont — c'est ce qui fait reconnaître une voyelle indépendamment de la note. Ici,
c'est l'inverse terme à terme : la hauteur reste, et c'est le formant qui
bouge. Aucune machine du parc ne fait cela, et pour une raison simple :
**toutes suivent le clavier.** Celle-ci sera la seule à le refuser, et à le
refuser par fidélité.

**La mesure qui tranche :**
- **La hauteur ne suit pas le clavier** : le fondamental mesuré sur les notes
  40, 55 et 70 doit être le MÊME à quelques cents près. Sur n'importe quelle
  autre machine, il suivrait exactement.
- **Mais la note fait quelque chose** : le pic spectral (le formant) doit
  monter avec la note, et de façon monotone. Sans cette moitié, on aurait
  seulement écrit un bourdon qui ignore le clavier — c'est-à-dire une machine
  cassée, pas une guimbarde.

- **Succès de H17** : les deux sont mesurés, et le § 10 du CDC nouvelle-machine
  gagne un cas nouveau : une machine qui REFUSE la hauteur MIDI, et qui doit le
  dire pour ne pas passer pour un défaut.
- **Échec de H17** : le code part hors du `CMakeLists`, avec son chiffre.

### H17 EST TRANCHÉE : SUCCÈS — et une machine qui refuse la hauteur (02/09/2026)

| note MIDI | 40 | 48 | 55 | 62 | 70 | 78 |
|---|---|---|---|---|---|---|
| fondamental | **82,00** | **82,00** | **82,00** | **82,00** | **82,00** | **82,00** Hz |
| centroïde | 705 | 849 | 994 | 1160 | 1362 | **1554 Hz** |

**Les deux moitiés sont là.** La hauteur ne bouge pas d'un cent sur trois
octaves et demie de clavier — sur n'importe quelle autre machine du parc, ce
tableau serait celui d'un défaut. Et pourtant la note fait quelque chose : le
formant monte de 705 à 1554 Hz, monotone, et c'est lui qu'on entend jouer.

**Un défaut trouvé par la mesure, et il portait sur le bourdon.** L'impulsion
qui excite la lame était large de neuf pour cent de la période, soit
cinquante-quatre échantillons à 82 Hz : le bourdon n'avait plus rien au-dessus
du kilohertz (0,000000 au douzième harmonique), si bien que le formant, même
placé à 2 240 Hz, **n'avait rien à cueillir** — le trait de la machine ne
s'entendait pas du tout. La largeur se compte désormais en ÉCHANTILLONS, de
sorte que la richesse du bourdon ne dépende plus de la lame choisie.

**Et une leçon de métrique, la sixième de la journée.** La première mesure du
formant cherchait le PIC dominant du spectre : il restait obstinément sur le
troisième harmonique du bourdon (246 Hz) quelle que soit la note. Un formant
déplace l'ENVELOPPE du spectre, pas son maximum ; c'est le centroïde qu'il
fallait regarder.

**Ce que le § 10 du CDC nouvelle-machine y gagne** : il traitait des machines
qui refusent un CONTRÔLEUR en le disant ; en voici une qui refuse la HAUTEUR
DE NOTE elle-même. Elle ne peut pas l'ignorer en silence — un musicien croirait
la machine cassée —, donc elle en fait son geste principal, et la façade
(section « REED », et non « tune ») comme le mode d'emploi le disent avant
qu'on s'en étonne. Elle refuse aussi la molette de hauteur, par cohérence : une
lame d'acier ne se plie pas en jouant.

Le parc passe à **49 machines** (1 046 paramètres nommés, 1 395 tests verts).

## 18. H18 — le THÉRÉMINE : un instrument sans touches, donc sans sauts (écrite avant sa mesure, 02/09/2026)

**Le trait vient de ce qu'il n'y a rien à toucher.** Un thérémine se joue dans
l'air, entre deux antennes : la main droite fait la hauteur, la gauche le
volume. Il n'y a **pas de touches**, donc pas de discontinuité possible — pour
aller d'une note à l'autre, la main traverse toutes celles du milieu, et on les
entend. Le glissando n'est pas un effet qu'on ajoute, c'est la seule façon dont
l'instrument sait changer de note.

**Deux conséquences, et le parc n'a ni l'une ni l'autre :**
- **Aucune machine du parc ne refuse les sauts de hauteur.** Toutes ont un
  portamento réglable — c'est-à-dire optionnel, et à zéro par défaut. Ici il ne
  peut pas être nul : ce serait un autre instrument.
- **Le volume ne vient pas de la frappe.** Il n'y a pas de frappe. La vélocité
  MIDI ne dit rien d'un thérémine ; c'est un contrôleur CONTINU qui fait le
  niveau, et il en fait tout, y compris l'attaque et l'extinction. `vsm.juno106`
  ignore déjà la vélocité (et un test le verrouille), mais il la remplace par
  une valeur fixe ; ici elle est remplacée par un GESTE.

**La mesure qui tranche :**
- **Le glissando est obligatoire** : deux notes enchaînées, la fréquence
  mesurée à mi-chemin doit se trouver STRICTEMENT entre les deux — et pas à
  moins d'un demi-ton de l'une ou de l'autre. Sur toute autre machine du parc
  réglée par défaut, elle serait déjà arrivée.
- **La vélocité ne fait rien au niveau** : deux rendus à vélocités opposées
  donnent le même RMS à 1 % près.
- **Mais la main gauche fait tout** : le même contrôleur, à deux valeurs,
  change le niveau d'un facteur franc.

- **Succès de H18** : les trois sont mesurés, et le parc gagne le seul
  instrument qui n'a pas de notes discrètes.
- **Échec de H18** : le code part hors du `CMakeLists`, avec son chiffre.

### H18 EST TRANCHÉE : SUCCÈS, les trois traits mesurés (02/09/2026)

**Le glissando est obligatoire.** De la note 57 (220 Hz) à la note 69
(440 Hz), avec `Glide` à 0,4 s :

| t | 0,90 s | 1,05 s | 1,20 s | 1,40 s | 1,80 s | 2,50 s |
|---|---|---|---|---|---|---|
| hauteur | 221,6 | **283,9** | 332,4 | 374,7 | 415,8 | 436,2 Hz |

À mi-chemin, la main est à 284 Hz — strictement entre les deux notes et à
plus d'un demi-ton de chacune. Sur toute autre machine du parc réglée par
défaut, elle serait déjà arrivée. Et la course du réglage l'interdit de
descendre à zéro : sa borne basse est à vingt millisecondes, parce qu'un
thérémine à portamento nul serait un oscillateur ordinaire.

**La vélocité ne fait RIEN** : vélocités 10 et 127 donnent 0,306018 toutes
les deux — un rapport de 1,0000 exactement. Il n'y a pas de frappe sur cet
instrument, donc la vélocité MIDI n'a rien à dire. **Mais la main gauche fait
tout** : la même pression de canal, à 0,2 et 0,9, change le niveau d'un
facteur **4,50**. `vsm.juno106` ignore déjà la vélocité, mais il la remplace
par une constante ; ici elle est remplacée par un GESTE.

**Et une identité inventée, rattrapée avant livraison.** Le premier jet
écrivait `lfo.1.pitchAmount` pour la profondeur de vibrato, en croyant
réemployer le vocabulaire du parc. Vérification faite dans le fichier :
**cette identité n'existe nulle part** ; la bonne est `lfo.1.toPitch`. C'est
la faute que `vsm.chebyshev` avait déjà payée, et la seule parade est de
regarder au lieu de supposer.

Le parc passe à **50 machines** (1 053 paramètres nommés, 1 407 tests verts).

## 19. H19 — la BOÎTE À MUSIQUE : une note qu'on redemande trop tôt ne sonne pas (écrite avant sa mesure, 02/09/2026)

**Le trait est mécanique, et il n'a rien d'un réglage.** Dans une boîte à
musique, chaque note est une LAME d'acier qu'une goupille du cylindre soulève
puis lâche. Une fois pincée, la lame met un temps à revenir sous la goupille :
si le cylindre la redemande avant, **il n'y a rien à pincer, et la note ne
sonne pas du tout.** C'est pour cela qu'un mécanisme de boîte à musique
comporte souvent deux lames accordées à l'unisson pour les notes répétées —
faute de quoi le trille est impossible.

**Aucune machine du parc ne refuse une note.** Toutes acceptent n'importe quel
débit : au pire, une nouvelle note vole une voix, mais elle sonne. Ici, une
note trop rapprochée doit être MUETTE, et le dire (le moteur compte déjà les
événements ignorés).

**Second trait, qui suit du même objet** : une lame d'acier libre à une
extrémité a des partiels très écartés et INHARMONIQUES — le premier au-dessus
du fondamental est vers 6,27 fois celui-ci, loin de l'octave. C'est ce qui
donne à la boîte à musique son timbre de verre, qu'aucune corde ne fait.
`vsm.modal` sait déjà faire une barre libre-libre (rapport 2,76 au second
rang) ; une lame ENCASTRÉE d'un côté est une autre loi, et le calcul sur son
code dira si elle est atteignable.

**La mesure qui tranche :**
- **La note redemandée trop tôt est muette** : deux frappes séparées de
  30 ms, la seconde ne doit rien ajouter — l'énergie mesurée juste après doit
  être celle de la décroissance de la première, à quelques pour cent près.
- **Mais elle sonne dès que la lame est revenue** : à 400 ms, la seconde
  frappe relance franchement le niveau.
- **Les partiels sont ceux d'une lame encastrée** : le second est à 6,27·f0 à
  quelques pour cent près.

- **Succès de H19** : les trois sont mesurés, et le § 10 du CDC gagne un
  second cas de refus — après la hauteur de `vsm.jewsharp`, une machine qui
  refuse une NOTE.
- **Échec de H19** : le code part hors du `CMakeLists`, avec son chiffre.

### H19 EST TRANCHÉE : SUCCÈS, et le parc a sa machine qui refuse (02/09/2026)

**La lame doit revenir**, temps de retour réglé à 0,18 s, deux frappes de la
même note :

| écart | notes refusées | niveau juste après / juste avant |
|---|---|---|
| 0,10 s | **1** | 0,96 — la première décroît, rien ne s'ajoute |
| 0,30 s | **0** | **2,01** — la seconde frappe relance |

Et c'est bien le TEMPS DE RETOUR qui décide, pas une limite arbitraire : le
même écart de 0,10 s passe si la lame revient en 0,05 s. Deux touches
différentes ne se bloquent jamais — chacune a sa lame, comme le peigne réel.

**Le refus est COMPTÉ** (`refusedNotes()`), parce qu'une panne muette reste
interdite même quand le silence est le comportement juste : l'interface
pourra dire un jour pourquoi un trille ne s'entend pas.

**Le second partiel est à 6,27·f0**, la loi d'une lame encastrée d'un côté —
et le calcul sur le `ratioOf` de `vsm.modal` confirme qu'il est hors de sa
portée ([1,866 ; 2,978] sur ce rang). Une barre libre aux deux bouts et une
lame encastrée sont deux lois, pas deux points d'un même segment.

**Le § 10 du CDC nouvelle-machine gagne son second cas de refus** : après
`vsm.jewsharp` qui refuse la HAUTEUR, voici une machine qui refuse la NOTE.

Le parc passe à **51 machines** (1 059 paramètres nommés, 1 419 tests verts).

## 20. H20 — le TERRAIN D'ONDES, et en quoi il diffère de `vsm.vector` (écrite avant sa mesure, 02/09/2026)

**La famille.** La synthèse par terrain d'ondes (Mitsuhashi, Borgonovo et
Haus, années 1980) définit une SURFACE `z = f(x, y)` et la parcourt par une
trajectoire — le plus souvent une orbite. L'onde de sortie est l'altitude
rencontrée le long du chemin. Le timbre ne vient donc ni d'une table ni d'un
filtre : **il vient de la FORME DU CHEMIN sur le relief.**

**La question à trancher avant d'écrire une ligne : en quoi est-ce autre chose
que `vsm.vector` ?** Les deux ont une orbite dans un plan, et la ressemblance
s'arrête là. `vsm.vector` mélange BILINÉAIREMENT quatre formes d'onde : sa
sortie est une combinaison LINÉAIRE des quatre coins, donc agrandir l'orbite
change les proportions du mélange mais **ne peut créer aucun contenu qui
n'était pas déjà dans les coins**. Un terrain est une fonction NON LINÉAIRE
des coordonnées : agrandir l'orbite fait franchir des reliefs, et le spectre
change de nature, pas seulement de dosage.

**La mesure qui tranche est donc un CONTRASTE, et il faut le faire sur les
deux machines au même protocole :**
- Sur `vsm.terrain`, agrandir l'orbite (`Orbit Radius`) doit changer le
  contenu harmonique RELATIF — le rapport h3/h1 doit bouger d'un facteur
  franc, à hauteur constante.
- Sur `vsm.vector`, la même manœuvre (agrandir le déplacement dans le plan)
  ne peut pas produire cela : elle redose des formes fixes.
- **Et la hauteur ne doit pas bouger** : c'est un changement de TIMBRE, pas
  de note.

- **Succès de H20** : le rapport bouge franchement sur le terrain, et la
  hauteur ne bouge pas. La famille entre au parc, et le contraste avec
  `vsm.vector` est mesuré plutôt qu'affirmé.
- **Échec de H20** : le terrain se comporte comme un mélange, auquel cas ce
  serait `vsm.vector` sous un autre nom — le code partirait hors du
  `CMakeLists`, avec son chiffre.

### H20 EST TRANCHÉE : SUCCÈS — mais le critère annoncé ne séparait rien (02/09/2026)

**Ce que l'hypothèse demandait était insuffisant, et il faut le dire avant le
verdict.** Elle annonçait que sur `vsm.terrain`, agrandir l'orbite ferait
bouger h3/h1 « d'un facteur franc ». C'est vrai — mesuré ×2,4 — mais
`vsm.vector` le fait aussi (×2,36), parce qu'agrandir son orbite redose ses
quatre formes. **Le critère aurait été satisfait par les deux machines**, et
n'aurait donc rien démontré.

**Ce qui sépare vraiment se voit en regardant PLUSIEURS rangs ensemble :**

| | h3/h1 | h5/h1 | rapport des deux |
|---|---|---|---|
| `vsm.vector` (orbite 0 → 1) | ×2,36 | ×2,36 | **1,00** |
| `vsm.terrain` (rayon 0,15 → 1) | ×2,4 | **×18,3** | **7,6** |

`vsm.vector` est une combinaison LINÉAIRE de quatre formes fixes : tous ses
rangs suivent le même dosage et varient donc du même facteur — identiques à
trois décimales, et de façon parfaitement monotone (0,019 · 0,024 · 0,030 ·
0,037 · 0,044). Un terrain est une fonction NON LINÉAIRE des coordonnées : ses
rangs vont chacun leur chemin, et non monotonement. **C'est cela, et cela
seul, qui distingue un relief d'un mélange.**

La hauteur, elle, ne bouge pas : 220,0 Hz à tous les rayons. C'est bien un
changement de timbre, pas de note.

Le parc passe à **52 machines** (1 071 paramètres nommés, 1 430 tests verts).

## 21. H21 — la SYNTHÈSE SPECTRALE : construire le spectre au lieu de l'obtenir (écrite avant sa mesure, 02/09/2026)

**La dernière grande famille absente.** Toutes les machines du parc
fabriquent une forme d'onde, et leur spectre est ce qui en résulte : on
l'obtient, on ne le pose pas. La synthèse par transformée inverse fait
l'inverse — on écrit les amplitudes de chaque case fréquentielle et on
redescend dans le temps par une IFFT. `vsm.chebyshev` en approche l'idée
(« on écrit le spectre qu'on veut et on l'obtient exactement »), mais il ne
peut poser que huit rangs HARMONIQUES, parce qu'un polynôme de Tchebychev de
rang n rend l'harmonique n et rien d'autre.

**Ce que cela permet et que rien d'autre ne permet : un spectre DENSE et
INHARMONIQUE à coût constant.** `vsm.additive` pose des rangs entiers ;
`vsm.modal` a vingt-quatre modes et `vsm.plate` seize, chacun coûtant un
oscillateur. Une IFFT de mille vingt-quatre points rend cinq cent douze raies
pour le même prix, à des fréquences quelconques — c'est-à-dire un « bruit
accordé » qu'aucune de ces machines ne peut approcher.

**Le risque est réel et il faut le nommer** : une IFFT travaille par BLOCS, ce
qui introduit une latence, demande des tampons pré-alloués (aucune allocation
dans `process`), et impose un recouvrement soigné sous peine de clics à chaque
trame. Le § 1 du CDC nouvelle-machine ne souffre aucune exception là-dessus.

**La mesure qui tranche :**
- **Densité inharmonique** : à écartement de partiels non entier, l'énergie
  doit se trouver à des fréquences que ni `vsm.additive` ni `vsm.modal` ne
  peuvent produire — on vérifie qu'au moins cinquante raies distinctes portent
  de l'énergie, et qu'elles ne sont pas multiples d'un fondamental.
- **Pas de clic au recouvrement** : aucune discontinuité entre deux trames —
  la dérivée maximale du signal doit rester du même ordre partout, sans pic
  périodique à la fréquence des blocs.
- **Aucune allocation dans `process`** : le test du parc qui parcourt toutes
  les machines doit passer.

- **Succès de H21** : les trois sont mesurés. Le parc gagne sa dernière
  famille classique.
- **Échec de H21** : clics, allocation, ou spectre indiscernable d'une
  additive. Le code part hors du `CMakeLists`, avec son chiffre.

### H21 EST TRANCHÉE : SUCCÈS, la dernière grande famille entre (02/09/2026)

**Les partiels tombent où aucune série harmonique ne peut aller.** À
`Stretch` 1,3, le partiel k est à `k^1,3·f0` :

| k | fréquence entière | mesuré | fréquence étirée | mesuré |
|---|---|---|---|---|
| 2 | 220,0 Hz | **0,00000** | 270,9 Hz | 0,00025 |
| 3 | 330,0 Hz | **0,00000** | 458,8 Hz | 0,00015 |
| 5 | 550,0 Hz | **0,00000** | 891,4 Hz | 0,00013 |

Zéro exact aux rangs entiers : ni `vsm.additive` ni `vsm.chebyshev` ne
peuvent produire cela. Et à `Stretch` 1,0 — le contrôle, sur la course du même
réglage — les partiels y retombent.

**Le coût est constant, et c'est l'argument de la famille.** Huit partiels ou
deux cent cinquante-six : le signal reste du même ordre (pic 0,44 contre 0,72
avant mise à l'échelle) et une seule transformée les rend tous. **La
polyphonie est gratuite** au même titre : il n'y a pas de voix, toutes les
notes déposent dans le même spectre, et six notes coûtent une transformée
comme une.

**Les deux pièges de la famille sont traités et vérifiés** : aucun clic au
raccord des trames (le plus grand saut d'échantillon reste sous six fois le
saut moyen, la fenêtre de Hann à saut de moitié sommant à une constante), et
**aucune allocation dans `process`** — le test du parc qui parcourt toutes les
machines passe.

**Une brique nouvelle et vérifiée** : `dsp/RealFft.h`, une IFFT radix-2 de
cinquante lignes, sans dépendance et sans allocation. Son premier test ne
mesure pas la machine mais la BRIQUE : une raie unique doit rendre exactement
un cosinus (erreur mesurée 2,8·10⁻⁸). Sans cette garantie, aucun des chiffres
ci-dessus ne voudrait rien dire.

Le parc passe à **53 machines** (1 081 paramètres nommés, 1 444 tests verts).

## 22. H26 — le CLAVECIN : un clavier qui refuse la vélocité et qui sonne au relâchement (écrite avant sa mesure, 03/09/2026)

**Ce que le parc n'avait pas.** Toutes ses cordes écoutent la vélocité, et
aucune machine ne produit de son AU relâchement : le clavicorde coupe net, le
piano laisse mourir, les synthés relâchent une enveloppe. Un clavecin fait
les deux choses à l'envers. Son SAUTEREAU monte avec la touche et son bec
pince la corde en passant — toujours de la même façon, vite ou lentement :
c'est le trait qui a fait inventer le piano-forte. En retombant, le bec
frôle la corde une seconde fois avant que l'étouffoir se pose, et ce petit
pincement s'entend sur tout clavecin. Enfin le son ne se règle pas, il se
REGISTRE : 8', 4' à l'octave, jeu de luth (une peau sur les cordes du 8').

*Ce que j'attends, écrit avant la mesure (dans le banc, avant de le
lancer)* : (1) deux vélocités extrêmes donnent une sortie IDENTIQUE AU BIT
PRÈS ; (2) une corde qu'on laisse mourir sous la touche puis qu'on relâche
produit un niveau au moins trois fois supérieur juste après le relâchement à
celui d'avant, puis retombe sous le dixième dans les 200 ms ; le frôlement
doit rester un petit pincement, une quinzaine de dB sous l'attaque ; (3)
tirer le 4' augmente l'énergie à 2·f0 d'au moins moitié ; (4) le jeu de luth
divise par deux le niveau à une seconde. Comme la boîte à musique (H19) et
le clavicorde, la machine refuse la molette en connaissance de cause.

### H26 EST TRANCHÉE : SUCCÈS, mesuré au banc (03/09/2026)

| Trait | Attendu | Mesuré |
|---|---|---|
| vélocité 20 contre 120 | sortie identique au bit près | **identique** (comparaison des tampons) |
| frôlement au relâchement | ≥ 3× le niveau d'avant, −15 dB sous l'attaque environ | corde éteinte avant (0,000000), frôlement 0,00635 = **−13,7 dB** sous l'attaque (réglage à fond ; −20 dB au défaut), **0,000000** 200 ms plus tard : l'étouffoir a coupé |
| registre 4' | ×1,5 à 2·f0 | **×1,6** |
| jeu de luth | ≤ ×0,5 à 1 s | **×0,05** |

Le premier banc a trouvé le piège de la machine : une corde tenue longtemps
s'endort sous le seuil d'activité, le gestionnaire de voix la croit libre et
ne lui transmet plus le relâchement — le frôlement n'avait pas lieu. Une
touche ENFONCÉE tient donc la voix éveillée même silencieuse. Douze tests,
dont les quatre traits ; empreinte de non-régression committée ; façade
REGISTERS · PLECTRUM · STRINGS (sans réglage de vélocité : la façade ne
promet pas ce que la machine refuse) ; identités sémantiques neuves pour ce
qui est propre au clavecin (registres, frôlement, étouffoir), réemployées
pour le point de pincement et la corde. Le parc passe à **54 machines**.

Développé dans un worktree pendant les campagnes de parité, pour ne pas
périmer le moteur des courses en cours (CDC multipiste § 8, « en attente ») ;
fusionné à leur fin.

## 23. H27 — la VIELLE À ROUE : un archet sans fin, des bourdons sans clavier, et la vélocité comme RYTHME (écrite avant sa mesure, 03/09/2026)

**Ce que le parc n'avait pas.** `vsm.string` frotte une corde à l'archet,
et l'archet finit avec la note. Une roue enduite de colophane ne finit pas :
tournée à la manivelle, elle frotte toutes les cordes à la fois, la
CHANTERELLE que les touches raccourcissent comme les BOURDONS, qui n'ont pas
de clavier et sonnent tant que la roue tourne — même sans note, et encore
un instant après la dernière, le temps que la roue s'arrête. Et la TROMPETTE
porte un chevalet mobile, le CHIEN, qui claque quand la roue accélère : le
rythme d'une vielle vient du coup de poignet, pas des touches, qui n'ont
aucune force à donner. Aucune machine du parc n'avait de son sans note, ni
d'inertie, ni de vélocité qui soit un rythme plutôt qu'une force.

*Ce que j'attends, écrit avant la mesure (dans le banc)* : (1) une note
tenue garde son niveau à 3 s comme à 1 s (à 3 dB près) — un archet, pas un
pincement ; (2) chien levé, deux vélocités extrêmes donnent la même
chanterelle AU BIT PRÈS ; (3) chien posé, la vélocité forte porte au moins
deux fois l'énergie de claquement de la faible (bandes latérales de la
trompette à ± la fréquence du chien) ; (4) les bourdons sonnent encore
50 ms après le relâchement (au moins 30 % du niveau tenu) et sont éteints
2 s après (moins de 5 %) ; (5) bourdons levés, plus rien ne sonne à leur
hauteur. Molette et pression refusées en connaissance de cause.

### H27 EST TRANCHÉE : SUCCÈS — et la roue a une force de seuil (03/09/2026)

| Trait | Attendu | Mesuré |
|---|---|---|
| chanterelle tenue | ±3 dB entre 1 s et 3 s | **+2,0 dB** (0,108 → 0,136) : elle ne meurt pas |
| vélocité 15 contre 127, chien levé | identique au bit près | **identique** |
| chien | ≥ ×2 d'énergie de claquement | **×8,2** (0,000043 → 0,000354) |
| bourdons après le relâchement | ≥ 30 % à 50 ms, ≤ 5 % à 2 s | **×2,3 à 50 ms** (0,0106 → 0,0244), **0,000000 à 2 s** |
| bourdons levés | < 10 % | tenu |

**Ce que le banc a appris, et qui n'était pas prévu : la friction a un
SEUIL.** Le gros bourdon, d'abord accordé une octave sous la tonique
(32 Hz), ne donnait pas sa fondamentale sous la roue — 3·f0 dominait de
soixante-dix fois. Remonté à la tonique (65 Hz, l'accord des vielles en
sol/do), il chante ; mais la force de la roue sur lui a dû être CHOISIE
par la mesure, pas devinée : à 0,3 sa fondamentale vaut 0,00002, à 0,45
0,00004, à 0,6 0,0022, à 0,8 0,0106. En dessous d'un seuil, l'archet
n'entretient pas l'oscillation ; la roue pousse donc les bourdons à 0,8 de
la force de la chanterelle, et le nombre est écrit dans le code avec sa
raison. Second effet vu au banc : au ralenti de la roue, le bourdon sonne
plus FORT à sa fondamentale que sous la pleine force (×2,3 à 50 ms) — la
friction passe d'un régime à glissements multiples au cycle de Helmholtz
propre, comme un archet qu'on allège. C'est un trait de l'instrument, pas
un défaut, et il n'est pas corrigé.

Douze tests, empreinte, façade WHEEL · DRONES · CHIEN (rendue et regardée) ;
identités neuves pour ce qui n'existe nulle part ailleurs (bourdons, tonique,
inertie, chien), celles de l'archet de `vsm.string` pour la roue. Le parc
passe à **55 machines**.

## 24. H28 — le BANJO : la corde dont la table est une PEAU (écrite avant sa mesure, 03/09/2026)

**Ce que le parc n'avait pas.** Toutes ses cordes rayonnent par une table
que personne ne modélise : le chevalet y est une simple perte. Le banjo
tend ses cordes sur une peau de tambour, et c'est la peau qui rayonne. Deux
traits en découlent qu'aucune corde du parc n'a : la peau chante SES modes
— ceux d'une membrane circulaire, les zéros de Bessel de `vsm.membrane` —
à des fréquences qui ne dépendent pas de la note ; et la peau MANGE la
corde, prend son énergie pour la rayonner, si bien que la note est brève et
claquante là où une guitare tiendrait.

*Ce que j'attends, écrit avant la mesure (dans le banc)* : (1) pour deux
notes éloignées (la2 et la4), la PART du mode fondamental de la peau
(tendue à 300 Hz) dans le son est au moins doublée par rapport au même
banjo sans peau ; (2) avec la peau, la tenue à une seconde (relative à
l'attaque) tombe sous 60 % de celle sans peau ; (3) la vélocité compte
(c'est un onglet, pas un sautereau) ; molette honorée, comme `vsm.string`.

### H28 EST TRANCHÉE : SUCCÈS (03/09/2026)

| Trait | Attendu | Mesuré |
|---|---|---|
| part du mode de peau à 300 Hz, la2 | ≥ ×2 | **×2,1** (0,0027 → 0,0056) |
| part du mode de peau à 300 Hz, la4 | ≥ ×2 | **×2,6** |
| tenue à 1 s, relative à l'attaque | ≤ 60 % de sans peau | **≈ 0** contre 0,0020 : la peau a tout pris |
| vélocité 30 contre 120 | ≥ ×1,5 | tenu |

La peau est une banque de six résonateurs à deux pôles aux six premiers
modes de Bessel, alimentée par le chevalet — et non frappée, ce qui est
toute la différence avec `vsm.membrane` ; le couplage est à sens unique
(la peau ne renvoie rien à la corde, sinon une perte). Douze tests,
empreinte, façade HEAD · PICK · STRING (rendue et regardée) ; identités
neuves pour la peau seule (tension, amortissement, part), celles de
`vsm.string` pour la corde. Le parc passe à **56 machines**.

## 12. H10 — la guitare ÉLECTRIQUE est-elle vraiment couverte ? (écrite avant sa mesure, 02/09/2026)

**Le fait qui la motive est une contradiction interne à ce dépôt.** Le tableau
du § 1 déclare « Basse électrique, guitare → `vsm.string` → **couvert** ». Le
§ 1 de `ROADMAP-fusion.md` écrit le contraire : « basse, guitare et cordes
réelles passent toujours par le sampler faute de modèle ». Les deux ne peuvent
pas être vrais, et la question n'est pas rhétorique : la séparation à six
sources (`htdemucs_6s`) produit un stem `guitar` à part entière, donc un stem
entier dépend de la réponse.

**L'ARGUMENT À ÉPROUVER.** Une guitare électrique n'est pas une corde pincée
qu'on écoute : c'est une corde pincée qu'on écoute EN UN POINT, par un micro
magnétique placé quelque part le long de la corde. Il y a donc **deux peignes
indépendants** — celui du point de PINCEMENT (que `vsm.string` a, via
`string.pickPosition`) et celui du point de CAPTATION (qu'aucune machine du
parc n'a). Un micro placé au quart de la corde ne peut pas entendre
l'harmonique 4, dont il occupe un nœud, quel que soit l'endroit où l'on pince.
S'y ajoutent la saturation d'ampli et la bande passante étroite du
haut-parleur, qui sont des effets d'insert et non de la machine.

**LA MESURE QUI TRANCHE, et c'est le protocole du saxophone (§ 11).** Cible
isolée : le preset « Clean Guitar » (programme 27) de GeneralUser GS, mesuré
zone par zone sur sa région tenue. Machine : `vsm.string`, balayée sur TOUTE la
course de `Pick Position` et de son amortissement. On compare les profils de
rangs h1..h8.

- **Succès de H10** : le profil réel montre un creux marqué à un rang que
  `vsm.string` ne parvient à creuser à AUCUN réglage de sa course — la marque
  du second peigne. Alors le tableau du § 1 est faux, il faut le corriger, et
  une machine `vsm.guitar` (corde + micro à position) a sa raison d'être.
- **Échec de H10** : `vsm.string` atteint le profil réel quelque part sur sa
  course. Alors le tableau a raison, `ROADMAP-fusion.md` § 1 est daté et doit
  être corrigé dans l'autre sens, et **il ne faut PAS écrire la machine** — ce
  serait un doublon, exactement ce que le § 9 refuse.

Dans les deux cas le dépôt y gagne : une contradiction de moins entre deux
documents qui se lisent l'un l'autre.

### H10 EST TRANCHÉE : ÉCHEC — et **la machine ne sera pas écrite** (02/09/2026)

**La cible réelle, 36 zones de « Clean Guitar » (GeneralUser GS) mesurées sur
leur région de boucle.** Médiane des rangs, rapportés au fondamental :

| | h2 | h3 | h4 | h5 |
|---|---|---|---|---|
| guitare électrique réelle (médiane de 36 zones) | 1,387 | 1,967 | 1,800 | 0,756 |
| `vsm.string`, meilleur point de sa course | **1,363** | **1,710** | 1,222 | 1,403 |

Le premier fait à retenir est déjà dans la colonne de gauche : **les rangs 2, 3
et 4 d'une guitare électrique sont PLUS FORTS que son fondamental.** C'est la
marque du micro magnétique, qui capte la VITESSE de la corde et non son
déplacement, donc accentue de six décibels par octave. Une machine qui ne
saurait pas produire cela serait hors sujet.

**`vsm.string` le produit.** Pincée près du chevalet (`Pick Position` 0,05),
elle rend h2 = 1,363 pour 1,387 visé — l'écart est de deux pour cent — et
h3 = 1,710 pour 1,967. Elle plafonne sur h4 (1,222 contre 1,800) et déborde sur
h5 (1,403 contre 0,756), mais **il n'existe aucun rang qu'elle serait
structurellement incapable d'atteindre** : l'écart est quantitatif, pas
mathématique. Rien à voir avec le fossé du saxophone, où la symétrie demi-onde
interdisait les rangs pairs et mesurait 0,000 contre 0,419 — un facteur infini,
et une impossibilité démontrable.

**Et la dispersion des zones réelles achève l'argument.** Ces 36 zones ne
décrivent pas UN profil mais un nuage : h4 va de 0,005 à 2,827 selon la corde
et la vélocité échantillonnées. « La » guitare électrique n'a pas de signature
spectrale unique dont on pourrait dire qu'une machine la rate ; `vsm.string`
tombe dans le nuage.

**Conséquences, et elles sont toutes des économies.**
- **Aucune machine `vsm.guitar` ne sera écrite.** Ce serait le septième
  soustractif du § 9 sous une autre forme : un nom de plus sur une liste, une
  candidate de plus à chaque arbitrage, et aucune famille ouverte.
- **Le tableau du § 1 a raison** et reste tel quel : la guitare est couverte
  par `vsm.string`.
- **Le § 1 de `ROADMAP-fusion.md` est DATÉ et doit être corrigé** : il écrit
  que « basse, guitare et cordes réelles passent toujours par le sampler faute
  de modèle », ce qui était vrai avant `vsm.string` et ne l'est plus. La
  contradiction entre les deux documents est levée dans ce sens-là.
- **Ce qui manque vraiment, s'il manque quelque chose, n'est PAS une machine**
  mais deux effets d'insert appliqués APRÈS elle : la saturation d'ampli et la
  bande étroite du haut-parleur. Ils appartiennent à la chaîne d'effets, pas au
  parc de machines, et le § 9 refuse expressément de confondre les deux.

**Une leçon de banc, et elle est du même genre que les trois précédentes.** Le
premier balayage a conclu que `vsm.string` plafonnait à h3 = 1,347, très loin
de la cible. Il réglait un paramètre nommé `Damping` — qui n'existe pas : la
machine expose `String Damping`. `setParameter` sur un identifiant inconnu ne
fait rien et **ne le dit pas**, si bien que quatre lignes du balayage étaient
identiques sans que cela alerte. C'est une panne muette, exactement ce que la
règle du dépôt interdit, et elle a failli faire écrire une machine inutile.

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

### ~~La case qui reste vide~~ — comblée le 01/09/2026, et pourquoi elle ne se comblait pas par un réglage

> **La case est remplie : `vsm.cone` est au parc** (§ 14, ARCHITECTURE § 33).
> Ce qui suit reste vrai mot pour mot — c'était bien la topologie, pas un
> réglage — et la sortie n'a pas été une topologie de plus : c'était le
> COUPLAGE, la symétrie du limiteur de boucle.

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

**LE DOSSIER EST ROUVERT SUR INSTRUCTION (01/09/2026), ET LE BANC A PARLÉ
AVANT TOUTE MODIFICATION.** L'utilisateur demande de couvrir « saxophone,
hautbois, flûte » ; la voie est celle que le § 44 d'ARCHITECTURE prescrit —
le COUPLAGE, pas une sixième topologie — et elle part du prototype
`audio/plugins/cone/` (hors build), qui a déjà la régulation. Un banc dédié
mesure justesse, niveau et énergie RANG PAR RANG (Goertzel sur n·f0, la
question du § 44 posée d'office) sur 45 configurations (5 notes de la
tessiture d'un ténor × 3 raideurs × 3 souffles, drift et bruit coupés).
L'état de départ, mesuré tel quel — et une leçon de banc AVANT le premier
chiffre : une première lecture donnait un « spectre de saxophone » à la note
46 (h2/h1 jusqu'à 1,10) qui fondait en montant. C'était un ARTEFACT : le
Goertzel visait n·f0 DEMANDÉ alors que la note sort jusqu'à +78 cents
au-dessus — sur une fenêtre de 1,5 s il lisait les jupes, pas les pics. La
leçon est la jumelle de celle du § 44 (« où est l'énergie ? ») : l'énergie
se mesure aux rangs de la note PRODUITE, pas de la note demandée. Corrigé,
le tableau devient :

| sur les 45 configurations | valeur |
|---|---|
| justesse | +36 à +78 cents, l'écart DÉCROÎT avec f0 |
| niveau efficace | 0,19-0,21 partout (la régulation tient) |
| h3/h1 · h5/h1 | 0,25-0,29 · 0,10-0,14 — les IMPAIRS sont là |
| h2/h1 · h4/h1 | 0,03-0,15 · 0,02-0,10 — les PAIRS manquent, partout |

Le spectre est UNIFORME sur la tessiture et il n'est pas vide : c'est la
signature d'une non-linéarité quasi IMPAIRE (le tanh de l'injection est
impair, la table d'anche sature symétriquement autour du repos) dans une
boucle qui, elle, porterait la série complète. Le problème n'est plus « la
sélectivité contre le timbre » : c'est la SOURCE qui ne produit pas de pair.

**Deux hypothèses, écrites avant leurs mesures (règle du § 5 duodecies de
la feuille de route fusion) :**

- **HC1 — la justesse est un retard de groupe.** L'écart décroît avec la
  note : signature d'une compensation faite au continu et non à f0.
  Compenser, dans `setTuning`, le retard de groupe À f0 des filtres de
  boucle — les deux pôles de perte, et l'AVANCE de phase de l'apex qui est
  aujourd'hui ignorée — ramène les 45 configurations sous ±30 cents sans
  toucher au spectre. Témoin : la compensation actuelle.
- **HC3 — l'anche asymétrique injecte le pair à la source.** Une anche qui
  BAT est asymétrique — fermeture en butée dure contre la table, ouverture
  douce — et un cycle asymétrique porte des rangs pairs même sous une
  boucle sélective. Rendre la table d'anche et la borne d'injection
  asymétriques (la butée de fermeture plus proche que la butée d'ouverture)
  doit porter h2/h1 à ≥ 0,3 sur la majorité des 45 configurations, sans
  perdre la justesse de HC1 ni la tenue du niveau. Une variable à la fois :
  HC1 d'abord, HC3 ensuite.

Critère d'acceptation final, celui du § 14 : sur les zones de boucle du
ténor GeneralUser (l'enregistrement isolé dont ce paragraphe se sert
depuis le 31/08), la machine doit porter des rangs pairs mesurables
(h2 ≥ 0,3 en médiane) en restant juste et bornée — et concourir à
l'arbitrage comme toute machine du parc, où le multisample reste son
témoin naturel.

**LES DEUX HYPOTHÈSES SONT TRANCHÉES LE JOUR MÊME, ET LA MACHINE EST AU
PARC (01/09/2026).**

- **HC1 CONFIRMÉE.** La phase de la cascade de boucle (deux pôles de perte,
  apex) évaluée sur H(e^jw) à w = 2π·f0/fs et retirée du trajet :
  **45/45 configurations justes** (−13 à +8 cents) contre 0/45 avant
  (+36 à +78). L'avance de phase de l'apex, ignorée par la compensation au
  continu, était bien le terme qui manquait.
- **HC3 CONFIRMÉE, SOUS UNE SECONDE FORME, et le chemin vaut la
  conclusion.** Première forme (courber la table d'anche) MESURÉE ET
  REJETÉE : h2/h1 recule (0,078 → 0,026) — l'anche BAT déjà (butée +1
  atteinte, dp de −2,1 à +0,9), le pair naît à la source mais un limiteur
  de boucle IMPAIR (le tanh) écrase l'onde vers un carré symétrique. Et le
  limiteur est structurel : sans lui, la boucle double sa période
  (−1200 cents). La forme qui reste : rendre le LIMITEUR asymétrique
  (terme en x², l'idée du micro de `vsm.epiano`), dose-réponse mesurée
  (asym 0 → h2 0,07 ; −1,2 → 0,42 ; falaise du sous-harmonique à −1,4).
  Résultat : **h2/h1 ≥ 0,3 sur 45/45 configurations, moyenne 0,365**
  (cible ténor 0,42), h4/h1 0,25, et `Brassiness` devient le bouton
  d'embouchure qui échange l'impair contre le pair (0,15 → h2 0,42/h3
  0,13 ; 0,7 → h2 0,23/h3 0,29).
- **Trois gardes mesurées au passage**, chacune avec son seuil : la course
  du souffle plafonne à 0,70 (au-delà, l'anche reste fermée plus d'un
  demi-cycle et la boucle sous-harmonise — le couac du vrai instrument,
  qu'une machine faite pour être cherchée ne doit pas avoir sur sa
  course) ; le mordant est asservi à un souffle ralenti de 150 ms (monté
  plus vite que la boucle n'établit f0, il verrouillait un mode haut,
  +2521 cents) ; et le limiteur garde un plancher de drive (brassiness à
  zéro le supprimait, et la boucle sous-harmonisait).
- **Et une leçon de banc** : le premier estimateur mesurait les rangs à
  n·f0 DEMANDÉ sur une note qui sortait +78 cents au-dessus — il lisait
  les jupes et inventait un « spectre de saxophone » au grave. L'énergie se
  mesure aux rangs de la note PRODUITE.

Livraison complète du § 10 : 9 tests dédiés (le trait distinctif —
`cone_bore_carries_even_harmonics`, miroir exact du test d'imparité de
`vsm.wind` — plus les gardes, la justesse, les molettes), empreinte de
non-régression, identités sémantiques RÉEMPLOYÉES de `vsm.wind` (mêmes
commandes, seule la perce change — le précédent du piano avec la corde,
et le profil de recherche suit d'office), façade en laiton verni —
regardée en aperçu PNG (vsm-panel-preview), disposition et sérigraphie
lisibles. Le parc
passe à 35 machines. Le critère d'arbitrage sur le stem de ténor réel se
mesurera à la première course qui suivra la recompilation du moteur.
