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
| Piano acoustique | `vsm.piano` | **couvert** — cordes frappées, même guide d'ondes (§ 11) |
| Orgue, claviers électromécaniques | `vsm.epiano`, `vsm.tonewheel` | **couvert** |
| Cordes | `vsm.string` | **couvert** — corde frottée, même machine (§ 10) |
| Cuivres, bois à perce CYLINDRIQUE | `vsm.wind` | **couvert** — anche et lèvres (§ 11) |
| **Saxophone, hautbois, flûte** | — | **non couvert** — perce conique et jet d'air, mesuré hors de portée du modèle actuel (§ 11) |
| **Voix** | `vsm.sampler` | **reportée, jamais reconstruite** — et c'est le seul emploi du sampler dans la version finale |

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
