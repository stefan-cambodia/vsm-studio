# Feuille de route du DAW — faire de l'atelier un logiciel de studio

**Nature du document.** Troisième axe, à côté de
[`ROADMAP-fusion.md`](ROADMAP-fusion.md) (reconstruire un enregistrement) et de
[`ROADMAP-apprentissage.md`](ROADMAP-apprentissage.md) (apprendre à reconnaître
le parc). Ces deux-là parlent de ce que le programme **entend** et de ce qu'il
**produit**. Celui-ci parle de ce qu'on peut y **faire** — c'est-à-dire du
logiciel lui-même, mesuré à l'aune de Cubase, d'Ableton Live et de FL Studio.

**Pourquoi il existe.** Les feuilles de route existantes déclarent toutes leurs
phases terminées, et elles ont raison : le moteur, les 34 machines,
l'interopérabilité et la chaîne de reconstruction sont faits, testés et mesurés.
Mais aucune n'a jamais posé la question « **peut-on travailler là-dedans ?** ».
Personne ne l'ayant posée, personne n'y a répondu, et l'écart s'est creusé sans
que rien ne le signale — ce qui est exactement le mode de défaillance que le
§ 0 de `CDC-nouvelle-machine.md` désigne comme le pire cas : incomplet **en
silence**.

---

## 0. Ce que ce document change, et ce qu'il ne renverse pas

Le § 0 de `ROADMAP-fusion.md` partage les rôles entre `analyse/`, `core/ +
audio/` et `interchange/`. **Ce partage ne bouge pas.** Ce qui change est le
critère par lequel on juge le DAW : jusqu'ici il était jugé comme **la référence
du rendu** — ce que Python optimise doit être exactement ce que le DAW joue.
C'est vrai et ça reste vrai. Mais un moteur de rendu juste n'est pas un lieu de
travail, et le second critère n'avait jamais été écrit.

**Les règles qui survivent intactes**, et qu'aucune phase de ce document n'a le
droit d'entamer :

1. **Le moteur est la source de vérité du rendu.** Tout ce qui s'ajoute ici
   passe par `ProcessGraph` et `OfflineRenderer`, jamais à côté.
2. **Le DAW reste autonome** : il se compile, se teste et s'utilise sans
   Python, sans réseau, sans CLAP. Une piste audio, un enregistrement, un
   export ne doivent pas créer de dépendance nouvelle à télécharger.
3. **Ajouter une machine ne modifie ni le moteur ni l'interface** (§ 0 de
   `CDC-nouvelle-machine.md`). Les phases ci-dessous élargissent les deux ;
   elles ne doivent pas rendre cette garantie plus difficile à tenir, et chaque
   phase qui touche `ProcessGraph` doit dire ce qu'elle exige d'une machine —
   la réponse attendue étant « rien ».
4. **Le chemin `process()` reste sans allocation, sans verrou, sans I/O.** Lire
   un fichier audio depuis le disque est précisément le genre de chose qui viole
   cette règle si on la fait naïvement : cela se fait sur un thread de
   préchargement, jamais dans le rappel audio.

**Et une règle nouvelle, propre à cet axe.** Le parc est figé par **34
empreintes de non-régression audio** (`audio/tests/audio_fingerprints.inc`).
**Aucune phase de ce document n'a le droit d'en changer une seule.** Si une
refonte du graphe modifie un échantillon, ce n'est pas l'empreinte qu'on met à
jour : c'est la refonte qui est fausse. On restructure l'atelier autour des
machines ; on ne retouche pas les machines pour que l'atelier tombe juste.

---

## 1. La mesure de départ

### 1.1 Le parc a dépassé l'atelier

**Le déséquilibre se voit au compteur de lignes**, et il n'a rien d'un détail de
présentation :

| Partie | Lignes | Ce que c'est |
|---|---|---|
| `analyse/` | 95 837 | entendre et décider (Python) |
| `audio/` | 35 168 | 34 machines, 9 effets, moteur temps réel |
| `interchange/` | 8 832 | formats, identités sémantiques, service de rendu |
| **`app/`** | **7 303** | **le logiciel lui-même** |

Sur 75 commits, **sept** touchent `app/`. Le parc a été nourri sans relâche ;
l'atelier qui l'accueille est resté un démonstrateur — de bonne facture, mais un
démonstrateur.

### 1.2 Le fait qui résume tout : la voix passe par une case de boîte à rythmes

Dans la reconstruction de *Sky and Sand* (`reconstruction/travail/sky-v4/`), la
piste « Voix » est une machine `vsm.sampler`, et le MIDI de l'arrangement
contient, pour cette piste :

| piste | notes |
|---|---|
| bass | 1 128 |
| other | 4 280 |
| Batterie | 4 215 |
| **Voix** | **1** |

Une note. Elle déclenche `samples/voix.wav` — **8 min 52, 47 Mo, le morceau
entier** — dans l'emplacement d'un sampler de percussions. Ce n'est pas un choix
de production : c'est le seul moyen qu'a trouvé la chaîne de faire entrer de
l'audio dans un DAW **qui n'a pas de piste audio**. Le § 6 de
`ROADMAP-fusion.md` écrit noir sur blanc que la voix restera de l'audio, « c'est
le mieux qu'on puisse en faire honnêtement » — et le logiciel qui doit la
rejouer n'a pas l'objet pour la porter.

### 1.3 Quatre absences structurelles

1. **Le clip n'existe pas.** Ni dans le modèle (`core/.../Track.h:69` : les
   notes sont un `std::vector<Note>` **à plat**, en ticks absolus depuis le
   début du morceau), ni dans le format (`ProjectDocument.h:75` : une piste
   **est** un canal de l'unique fichier `midi/arrangement.mid`), ni dans
   l'interface (le piano roll montre **une seule piste à la fois** ; les autres
   n'apparaissent qu'en notes fantômes non cliquables). Il n'y a donc ni
   région, ni motif réutilisable, ni copier-coller de section, ni boucle de
   clip, ni vue d'arrangement. **Ce n'est pas un manque d'interface : c'est
   l'absence du concept.**
2. **Le logiciel ne peut rien entendre.** `app/Source/audio/AudioEngine.cpp:11` :
   `deviceManager_.initialise(0, 2, nullptr, true)` — **zéro entrée**. Le rappel
   audio ignore explicitement ses paramètres d'entrée (`AudioEngine.cpp:120`).
   Pas de capture, donc pas d'enregistrement, ni MIDI ni audio.
3. **Aucune piste audio.** Le seul audio possible est la piste de référence A/B
   (`ReferenceTrack.h`) : un tampon stéréo **global**, mélangé après le master,
   non sérialisé, et exclu du rendu hors ligne par conception.
4. **Aucun plugin tiers.** `app/CMakeLists.txt` ne lie pas
   `juce_audio_processors` : pas de VST3, pas de scan. L'hôte CLAP **est écrit
   et testé** (`clap/host/ClapPluginHost.cpp`) mais rien dans `app/` ne le
   référence, et il ne charge que des instruments — `audio_inputs = nullptr`
   (`ClapPluginHost.cpp:192-193`) interdit par construction d'héberger un effet.

### 1.4 Huit choses qui existent et qui mentent

Celles-ci sont plus graves que les absences, et c'est le cœur de ce document.
Une fonction absente est honnête : on la cherche, on ne la trouve pas, on sait
où on en est. Une fonction **présente qui produit un résultat faux sans le
dire** détruit la confiance dans tout le reste — et ce projet a déjà payé ce
prix trois fois (l'empreinte silencieuse d'une machine muette, les six pannes
muettes de la chaîne d'analyse, la comparaison à deux fréquences
d'échantillonnage différentes).

| # | Ce qui ment | Où | Conséquence |
|---|---|---|---|
| 1 | **Rien ne se sauvegarde.** Le menu Fichier n'a ni « Enregistrer » ni « Enregistrer sous ». `saveProjectBundle()` existe et n'est appelée **nulle part** dans `app/` | `ProjectBundle.h:68` ; `MainComponent.cpp:314-343` | mixage, effets, automation, boucle, MIDI learn, paramètres : **tout est perdu à la fermeture** |
| 2 | **L'export WAV ne contient pas ce qu'on entend.** Le graphe d'export reçoit instruments, projet, automation et master — mais **jamais** `setTrackEffectChain()` ni `setSendEffect()` | `MainComponent.cpp:466-515` | le fichier rendu n'a ni les inserts, ni la reverb, ni le delay qu'on vient d'écouter |
| 3 | **Les chaînes d'effets vivent dans un composant d'interface**, dans une `std::map<int, Chain>` **indexée par numéro de piste** | `EffectChainComponent.h:46` | supprimer une piste **réaffecte silencieusement** les effets aux mauvaises pistes ; et rien n'est jamais sérialisé |
| 4 | **Les effets sont préparés à 48 kHz en dur**, sans être re-préparés si la carte tourne à 44,1 kHz | `MainComponent.cpp:124` | tous les temps de delay et de reverb sont faux hors 48 kHz — **le même piège** que le § 10.3 de `ROADMAP-fusion.md`, qui avait donné une corrélation de 0,0002 entre deux rendus identiques |
| 5 | **Le moteur jette tout le MIDI qui n'est pas une note.** 14 types d'événements sont modélisés, planifiés et exportés en SMF ; `MidiNoteEvent` n'a que `NoteOn`/`NoteOff`, et le reste retourne `false` | `MidiEvent.h:17-61` → `ProcessGraph.cpp:419-436` ; `ParameterTypes.h:34-41` | un pitch bend, une molette de modulation, un CC, un aftertouch sont **lus, stockés, sauvegardés, exportés — et ne s'entendent jamais**. Le projet contient des données qui ne sonnent pas |
| 6 | **Les notes se perdent en silence sous charge.** `kMaxEventsPerBlock = 256` par piste et par bloc, dépassement traité par un `break` sans signalement | `ProcessGraph.h:243` ; `ProcessGraph.cpp:412` | une piste de batterie reconstruite porte **4 215 notes** : le plafond est atteignable, et rien ne le dira |
| 7 | **Les éditions de vélocité contournent l'historique** : elles modifient `note.velocity` sans passer par `beginEdit()` | `VelocityLaneComponent.cpp:93-131` | non annulables, **et** la pile d'annulation devient incohérente pour les gestes suivants |
| 8 | **Trois commandes sont des décors** : le bouton Rec n'a aucun gestionnaire, le bouton Loop a un `onClick` **vide**, et `Track::armed` est écrit par le bouton « R » sans que personne ne le lise | `TransportBarComponent.cpp:10, 27-31` ; `TrackListComponent.cpp:70` | l'interface promet trois fonctions qui n'existent pas |

### 1.5 Ce qui, à l'inverse, est déjà au niveau

Il serait faux de tout peindre en noir, et ce document ne servirait à rien s'il
proposait de refaire ce qui est fait :

- **Le piano roll** n'a pas à rougir devant les trois logiciels cités : six
  outils, annuler/rétablir sur 128 pas, quantification à force partielle et
  swing, humanisation déterministe, legato, 14 gammes, 13 types d'accords,
  arpèges, ligne de vélocité peinte à la souris, notes fantômes, écoute au clic,
  duplication à l'Alt-glissé, et une trentaine de raccourcis.
- **Le timing** : `TempoMap` **et** `TimeSignatureMap` gèrent déjà les
  changements multiples ; la boucle du moteur est **échantillon-exacte**, avec
  relâchement des notes tenues au bouclage (`ProcessGraph.cpp:256-275`).
- **Le moteur audio** : graphe sans allocation ni verrou, publication par
  `std::atomic<std::shared_ptr<>>`, files sans verrou pour les notes live,
  **un seul chemin de rendu** partagé entre temps réel et export, déterminisme
  vérifié par test.
- **Le mixeur** existe vraiment : tranches avec VU-mètres à maintien de crête,
  deux départs auxiliaires, et une tranche master complète — **égaliseur trois
  bandes, compresseur, saturation, largeur stéréo, limiteur, mesure LUFS**
  (`MasterBus.h:36-51`).
- **L'interopérabilité** est en avance sur tout le reste : identités
  sémantiques, presets en unités physiques, adaptateur **et** hôte CLAP, rendu
  hors ligne déterministe, format de projet versionné qui refuse ce qu'il ne
  comprend pas et signale ce qui manque au lieu de le remplacer.
- **Les façades** des 34 machines, que très peu de DAW offrent à leurs
  instruments d'usine.

**Le diagnostic tient donc en une phrase** : l'écart avec Cubase, Live et FL
Studio n'est ni dans le son, ni dans l'édition des notes, ni dans le format.
Il est dans **tout ce qui entoure une note** — la garder, la faire cohabiter
avec de l'audio, l'arranger, l'enregistrer, la mixer, la sortir.

---

## 2. Ce qu'on entend par « digne de Cubase, Live, FL Studio »

Le mot est vague ; il faut le rendre mesurable, sans quoi ce document ne sera
jamais fini. **La barre n'est pas la ressemblance** : ni le nombre de plugins
d'usine, ni la copie d'une disposition d'écran. C'est cinq choses, et un
logiciel qui les tient est un logiciel de studio même s'il ne ressemble à aucun
des trois :

| | Le critère | Aujourd'hui |
|---|---|---|
| **a** | **Ce qu'on fait ne se perd pas** : le projet se sauvegarde et se rouvre à l'identique | **non** — rien au-delà des notes n'est écrit |
| **b** | **Le son peut entrer** : importer un fichier audio, enregistrer une entrée, jouer un clavier en temps réel | **non** — zéro entrée, pas de piste audio |
| **c** | **On peut arranger** : des clips déplaçables sur une ligne de temps, plusieurs pistes visibles à la fois, boucles, marqueurs | **non** — pas de clip dans le modèle |
| **d** | **On peut mixer** : bus de groupe, départs libres, égaliseur et dynamique **par piste**, chaîne latérale, compensation de latence | **partiel** — 2 départs figés, dynamique sur le master seulement, aucune PDC |
| **e** | **Le son peut sortir** : mixage, stems, plage choisie, formats et résolutions | **partiel** — `vsm-render` le fait, l'application exporte un WAV faux (§ 1.4 n° 2) |

**Un sixième critère, propre à ce projet.** Aucun des trois logiciels cités ne
sait prendre un enregistrement et rendre les notes **et** les patchs qui le
rejouent. Ce projet le sait, et c'est du Python appelé en ligne de commande. Le
jour où cela se fait depuis l'application, le DAW a quelque chose qu'aucun des
trois n'a — la seule case où il peut espérer être **devant**, ce qui en fait une
phase à part entière (D9) plutôt qu'un agrément.

---

## 3. Les phases, dans l'ordre

L'ordre obéit à deux règles, dans cet ordre :

1. **Ce qui ment passe avant ce qui manque.** Une fonction fausse coûte plus
   cher qu'une fonction absente, parce qu'elle se propage : on règle un mixage
   sur un export qui ne le contient pas, on cherche une erreur de synthèse
   là où c'est la fréquence d'échantillonnage qui est fausse.
2. **Ce qui devrait être réécrit passe avant ce qui s'y appuie.** Une piste
   audio posée avant que le modèle connaisse le clip serait à refaire
   intégralement ; un export écrit avant les bus exporterait une console qui
   n'existe pas encore.

### Phase D0 — Réparer ce qui ment

Aucune fonction nouvelle. Les huit points du § 1.4, dans l'ordre où ils font le
plus de dégâts. C'est la phase la moins spectaculaire et la plus rentable :
l'essentiel est du câblage de code déjà écrit.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D0.1 | **Enregistrer un projet** : brancher `saveProjectBundle()`, menu Fichier, Ctrl+S, « Enregistrer sous », état réel des presets par piste | un projet ouvert, modifié, sauvegardé et rouvert est **identique** ; un test compare les deux `project.json` |
| D0.2 | **Les chaînes d'effets et l'automation entrent dans le `Project`** et en sortent | elles survivent à un aller-retour disque ; supprimer une piste n'en déplace aucune (test de régression sur le décalage d'index) |
| D0.3 | **L'export contient ce qu'on entend** : inserts et départs posés sur le graphe d'export | l'export de l'application et `vsm-render` sur le même projet donnent le **même fichier, octet pour octet** |
| D0.4 | **Une seule fréquence d'échantillonnage** : les effets se re-préparent quand le périphérique change | à 44,1 kHz, un delay réglé sur 500 ms dure 500 ms ; test à trois fréquences |
| D0.5 | **Le MIDI non-note atteint les machines** : `MidiNoteEvent` s'élargit (pitch bend, CC, aftertouch, pression), `ISynthPlugin` reçoit ce que le projet contient | un pitch bend écrit dans le projet **s'entend** ; les 34 empreintes restent inchangées (une machine qui ignore un CC continue de l'ignorer) |
| D0.6 | **La perte d'événements cesse d'être silencieuse** : plafond relevé, dépassement compté et remonté | une piste de 4 215 notes joue toutes ses notes ; un test provoque le dépassement et vérifie qu'il est **signalé**, jamais avalé |
| D0.7 | **La vélocité passe par l'historique** ; l'annulation couvre le mixage, les effets, l'ajout/suppression de piste, et ne se vide plus au changement de piste | annuler après avoir peint une nuance la défait |
| D0.8 | **Les décors deviennent des commandes** : Rec, Loop, `armed`, et la **barre d'espace** qui ne lance rien aujourd'hui | aucun contrôle affiché n'est sans effet ; test d'interface sur les gestionnaires |

**Critère de phase** : après D0, **tout ce que l'application affiche est vrai**.
Aucune commande morte, aucun réglage perdu, aucun export qui diffère de l'écoute,
aucune donnée du projet qui ne sonne pas. C'est la condition pour que les mesures
des phases suivantes veuillent dire quelque chose.

> **D0.1 à D0.4 SONT FAITES, ET D0.8 POUR MOITIÉ (29/08/2026).** 883 tests
> moteur verts, zéro avertissement, les 34 empreintes audio inchangées.
>
> **Ce qui a changé de place, et c'est le cœur de l'affaire.** Les effets et
> l'automation vivaient dans des composants d'interface ; ils vivent désormais
> dans la **piste** (`core/Track.h` : `TrackEffect`, `AutomationCurve`), et la
> tranche master ainsi que la région de boucle dans le **projet**
> (`core/Project.h`). Ce déplacement règle quatre défauts d'un coup, sans qu'il
> reste de code pour les faire revenir :
>
> - le format savait écrire tout cela **depuis la Phase 7** — les tests
>   `effects_are_described_semantically_on_a_track` et
>   `automation_round_trips_and_stays_optional` le prouvaient déjà. Ce qui
>   manquait était le maillon d'avant : `documentFromProject()` écrivait des
>   tableaux **vides** parce que le modèle ne portait rien ;
> - le décalage d'index disparaît **par construction** : une chaîne rangée dans
>   la piste suit la piste, et supprimer une piste n'a plus rien à recalculer ;
> - la fréquence d'échantillonnage réelle est appliquée aux inserts et aux bus
>   (`AudioEngine::currentBlockSize()` a été ajouté pour préparer les effets à
>   la bonne taille de bloc, pas seulement à la bonne fréquence) ;
> - et l'export **emprunte le chemin de `vsm-render`** au lieu de monter son
>   propre graphe.
>
> **Le rendu hors ligne mentait aussi, et personne ne l'avait vu.**
> `renderBundleToWav()` — donc `vsm-render`, donc toute la chaîne de
> reconstruction — appliquait les presets et l'automation, mais **ni les
> inserts ni la tranche master**. Un projet portant une réverbération se rendait
> sans elle, sans un avertissement. Corrigé à la racine : l'export de
> l'application ne peut plus diverger du rendu hors ligne, puisque c'est le
> même code.
>
> **Un test qui ne prouvait rien, attrapé au passage.** La première version du
> test « un insert décrit est bien rendu » poussait tous les paramètres de
> l'effet à leur **minimum** — ce qui met aussi le mélange sec/traité à zéro.
> L'effet était branché, appliqué, et parfaitement inaudible : le test passait
> avec ou sans le correctif. Remplacé par un passe-bas à 20 Hz entièrement
> traité, réglé paramètre par paramètre et nommément.
>
> **Ce que D0.8 a rendu vrai** : la barre d'espace lance et arrête (elle ne
> faisait rien, nulle part), et le bouton Loop boucle réellement — sans région
> définie, sur tout le morceau, plutôt que d'exiger un geste que rien
> n'indiquait. **Ce qu'elle a rendu honnête** : le bouton Rec et l'armement de
> piste sont désactivés et disent pourquoi, en renvoyant à D3. Une commande qui
> promet une fonction absente est pire que la fonction absente, parce qu'elle se
> découvre en la cherchant.
>
> **D0.5 ET D0.6 SONT FAITES, D0.7 À MOITIÉ (29/08/2026).** 887 tests moteur
> verts, 34 empreintes inchangées.
>
> **D0.5 — pourquoi un second flux plutôt qu'une énumération élargie.** Le
> premier réflexe était d'ajouter des valeurs à `MidiNoteEvent::Kind`. Il aurait
> été faux : **vingt-deux des trente-quatre machines** dispatchent leurs
> événements en `if (kind == NoteOn && velocity > 0) ... else ...`, si bien que
> chaque contrôleur reçu aurait **relâché une note**, dans vingt-deux machines à
> la fois. Un type séparé (`MidiControlEvent`) et une méthode non pure dont le
> défaut est « je ne sais pas faire » ne peuvent pas être mal interprétés : une
> machine qui ne l'implémente pas se comporte exactement comme avant, au bit
> près — ce que les 34 empreintes vérifient.
>
> L'unité livrée est **celle du musicien** : des demi-tons pour la molette, une
> fraction de 0 à 1 pour le reste. Une machine n'a pas à connaître les 14 bits
> signés du MIDI pour transposer, et la conversion se fait une seule fois, dans
> le graphe.
>
> **Cinq machines répondent, et ce n'est pas un échantillon arbitraire** : les
> quatre monophoniques du parc (Minimoog, TB-303, SH-101, MS-20) et la
> duophonique (ARP Odyssey) — exactement celles dont le jeu dépend de la molette.
> Les polyphoniques suivront machine par machine : une case a été ajoutée à la
> liste de contrôle du § 10 de `CDC-nouvelle-machine.md`, qui est l'endroit où
> ce projet range les obligations par machine.
>
> **La distinction qui fait tout** : une machine qui **ignore** un contrôleur
> exerce un droit ; un moteur qui le **jette** cachait un défaut. Le moteur
> compte désormais les deux (`ignoredControlEvents()`), et un test vérifie que
> la TR-808 dit non plutôt que de faire semblant.
>
> **D0.6** : le plafond passe de 256 à 1024 événements par piste et par
> sous-segment, et surtout le `break` muet devient un compteur
> (`droppedNoteEvents()`). Le chiffre doit rester à zéro ; toute autre valeur
> est un morceau qu'on n'entend pas en entier.
>
> **D0.7, la moitié faite** : la lane de vélocité passe par l'historique, une
> fois par geste. Le défaut n'était pas seulement « ces éditions ne s'annulent
> pas » : l'annulation travaillant par instantanés, annuler le geste **suivant**
> restaurait un vecteur capturé avant les nuances peintes, qui disparaissaient
> donc avec une action sans rapport. **Reste** l'annulation du mixage, des
> effets et de l'ajout/suppression de piste, ainsi que la pile qui se vide au
> changement de piste : c'est un historique transactionnel et global, et c'est
> D1.5 qui le porte.

### Phase D1 — Le clip, dans le modèle, sans toucher un échantillon

Le modèle doit apprendre qu'une piste est faite de **morceaux placés** et non
d'un flot de notes accroché à un canal MIDI. Fondation de D2, D3 et D5, et seule
phase du document qui ne se voie pas.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D1.1 | `Clip` dans `core/` : début, durée, décalage interne, boucle, muet, nom, couleur | les notes d'une piste vivent dans des clips ; `PlaybackScheduler` lit des clips |
| D1.2 | Clip MIDI **par référence** : un même clip placé deux fois ne duplique pas ses notes | éditer l'un modifie l'autre ; test |
| D1.3 | `project.json` **version 2** | un projet v1 se charge, se convertit, se réécrit en v2 ; migration testée dans les deux sens |
| D1.4 | Marqueurs et régions nommées, promus au rang d'entités | les Marker/CuePoint SMF conservés opaques aujourd'hui (`MidiEvent.h:36-39`) deviennent visibles dans le transport et écrits dans `project.json` |
| D1.5 | L'historique d'annulation devient **transactionnel et global** (suite de D0.7) | une opération sur trois pistes s'annule d'un seul geste |

**Critère de phase, et il est sévère** : après D1, le rendu de
`docs/examples/demo-project/` et celui des projets reconstruits sont
**identiques au bit près** à ce qu'ils étaient avant. On restructure le modèle
sans changer une seule valeur d'échantillon. Si un octet bouge, la phase est
fausse — c'est le principe des empreintes de machines, appliqué au projet.

### Phase D2 — La piste audio

Ce qui débloque le fait du § 1.2 : la voix cesse d'être une note de sampler.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D2.1 | Type de piste `Audio`, clip audio référençant un fichier + un intervalle | un projet porte une piste audio ; chemins portables imposés comme pour les presets |
| D2.2 | Lecture disque **hors du thread audio** : préchargement, tampon circulaire, sous-alimentation comptée et signalée | un fichier de 47 Mo se lit sans une allocation dans `process()` ; un test compte les allocations |
| D2.3 | Rééchantillonnage si le fichier n'est pas à la fréquence de la session | un WAV 44,1 kHz joué à 48 kHz garde hauteur et durée, à la tolérance publiée |
| D2.4 | Gain, panoramique, fondus d'entrée/sortie, inversion de phase, par clip | réglables, sauvegardés, rendus |
| D2.5 | Forme d'onde dessinée, avec cache d'aperçu | 9 minutes s'affichent sans bloquer l'interface |
| D2.6 | `vsm-render` rend l'audio comme le temps réel | rendu hors ligne et temps réel identiques à l'échantillon près, comme c'est déjà testé pour CLAP |

**Critère de phase, et c'est le plus important du document** : la reconstruction
de *Sky and Sand* se charge dans l'application et **se joue entière, voix
comprise, sans qu'aucune note de sampler ne porte un fichier de 47 Mo**. Le MIDI
de la voix retombe à zéro note ; l'audio est sur une piste audio, à sa place.

### Phase D3 — Enregistrer

Un logiciel qui ne peut rien capter n'est pas un studio, c'est un lecteur.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D3.1 | Ouvrir le périphérique **avec des entrées**, choisir la source, écouter l'entrée, mémoriser le choix du périphérique | le niveau d'entrée s'affiche ; `AudioEngine.cpp:11` n'ouvre plus zéro entrée ; le réglage survit au redémarrage |
| D3.2 | Tempo **modifiable**, tap tempo, piste de tempo dessinée, **métronome** et décompte | on peut commencer un morceau à partir de rien, ce qui est impossible aujourd'hui |
| D3.3 | Enregistrement MIDI temps réel : armement (`Track::armed` enfin lu), superposition, quantification après coup | jouer trois mesures les inscrit dans un clip |
| D3.4 | Enregistrement audio en flux sur disque pendant la lecture | 10 minutes s'enregistrent sans décrochage ; le fichier est relu tel quel |
| D3.5 | Punch in/out, enregistrement en boucle avec prises empilées | les prises se conservent et se choisissent |
| D3.6 | Latence d'entrée **mesurée**, pas estimée, et compensée | une boucle physique enregistre à l'échantillon près ; le chiffre est publié |

**Critère de phase** : on peut jouer une partie au clavier par-dessus une
reconstruction et la garder — c'est-à-dire faire de la musique dans le logiciel,
pas seulement l'écouter.

### Phase D4 — La console

| Étape | Contenu | Terminé quand |
|---|---|---|
| D4.1 | **Égaliseur, compresseur, porte, limiteur enfichables par piste** — le DSP existe déjà dans `MasterBus`, il n'est pas exposé | quatre effets de plus dans `EffectFactory`, chacun conforme au CDC (identités sémantiques, façade, empreinte) |
| D4.2 | Bus de groupe et départs **libres** (aujourd'hui : deux, figés en dur sur Reverb et Delay dans le constructeur de `MainComponent`) | une réverbération se partage entre pistes ; le nombre de départs n'est plus une constante |
| D4.3 | Départs pré/post-fader au choix (post-fader est codé en dur, `ProcessGraph.cpp:465`) | commutable par départ |
| D4.4 | **Chaîne latérale** (*sidechain*) | le compresseur d'une piste écoute une autre piste — la signature même du genre que ce projet reconstruit |
| D4.5 | **Compensation de latence (PDC)** : `ISynthPlugin` et `IAudioEffect` déclarent leur latence, le graphe la compense | insérer un effet à latence connue ne décale plus la piste ; test avec une latence artificielle et un `Oversampler` |
| D4.6 | Automation de **tout** : volume, pan, départs, paramètres d'effets, master — aujourd'hui les paramètres d'instrument seulement (`ProcessGraph.cpp:325`) | un fondu écrit en automation s'entend |
| D4.7 | Mesure : crête **et** RMS par piste, LUFS, corrélation de phase | affichés, et cohérents avec ce que `analyse/` mesure du même signal |

**Critère de phase** : le mixage fait dans l'application et le mixage fait par
`analyse/` sur les mêmes stems donnent le même LUFS à 0,1 près. Les deux moitiés
du projet mesureront enfin la même chose.

### Phase D5 — La vue d'arrangement

D1 a mis les clips dans le modèle ; ici on les rend manipulables.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D5.1 | Ligne de temps multipiste : clips déplaçables, redimensionnables, coupables | à la souris, avec annulation |
| D5.2 | Copier/coller/dupliquer, aimantation à la grille, boucle de clip par étirement | mêmes gestes et mêmes raccourcis que le piano roll |
| D5.3 | Pliage des pistes, hauteurs réglables, réordonnancement, couleurs choisies | l'écran tient 16 pistes |
| D5.4 | Automation dessinée **sur** l'arrangement, avec zoom et courbes | plus une lane isolée dans un onglet |
| D5.5 | Gel et report (*freeze* / *bounce*) d'une piste en audio | une piste gelée sonne identique et coûte le prix d'une lecture audio |

**Critère de phase** : arranger une reconstruction — déplacer un refrain,
doubler une mesure, boucler quatre temps — se fait entièrement à la souris.

### Phase D6 — Exporter

D0.3 a rendu l'export **honnête** ; ici on le rend complet.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D6.1 | Plage au choix (morceau, boucle, sélection), fréquence, profondeur, queue | plus de 48 kHz / 24 bits en dur |
| D6.2 | Export **stems** : une piste ou un bus par fichier | la somme des stems égale le mixage, vérifié par test |
| D6.3 | Export MIDI complet (aujourd'hui : perd `muted` et `confidence`) | relu ailleurs sans perte de tempo ni de signature |
| D6.4 | Export d'un **projet autonome** (dossier complet, échantillons compris) | s'ouvre sur une autre machine sans rien de manquant |
| D6.5 | Rendu en temps réel, requis dès qu'un plugin tiers l'exige | option explicite, jamais le défaut |

**Critère de phase** : il n'y a toujours qu'**un seul rendu** dans ce projet.
L'application n'en invente pas un second ; elle expose celui de `vsm-render`.

### Phase D7 — Héberger les plugins des autres

L'hôte CLAP existe (`clap/host/`). Le marché, lui, est en VST3.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D7.1 | Lier l'hôte CLAP à l'application — il est écrit, testé, et non branché | un `.clap` tiers se charge sur une piste |
| D7.2 | Hôte VST3 pour les instruments, présenté comme `ISynthPlugin` | un instrument tiers joue et se sauvegarde dans le projet |
| D7.3 | **Entrées audio dans l'hôte** pour héberger des effets (CLAP comme VST3) | insérables au même titre que les natifs |
| D7.4 | Interface native du plugin dans une fenêtre ; transport transmis au plugin | affichée, redimensionnable, fermable sans perte d'état ; un delay synchronisé au tempo suit le tempo |
| D7.5 | Balayage des plugins installés en tâche de fond | plugin fautif isolé et signalé, jamais fatal |

**Critère de phase** : un projet contenant un plugin tiers se recharge à
l'identique, et son absence est **signalée sans être substituée** — exactement
la règle déjà tenue pour les instruments VSM manquants (P4/P7 de
`ROADMAP-interop.md`).

### Phase D8 — Tenir la charge

| Étape | Contenu | Terminé quand |
|---|---|---|
| D8.1 | Graphe audio multicœur | 32 pistes chargées tiennent sans décrochage ; gain mesuré et publié |
| D8.2 | Diffusion disque pour l'audio long | 20 pistes de 9 minutes n'occupent pas 1 Go |
| D8.3 | Un seul chemin de transport : `RealtimeTransport` et l'horloge du `ProcessGraph` sont aujourd'hui **deux notions de position** qui coexistent | une seule fait autorité ; l'autre disparaît ou en dérive |
| D8.4 | Banc de charge dans la suite de tests | le coût par piste est chiffré et suivi, comme le banc CPU de la Phase 6 |

**Critère de phase** : le chiffre existe. Aujourd'hui personne ne sait combien
de pistes l'application supporte, et une performance qu'on ne mesure pas est une
performance qu'on croit avoir.

### Phase D9 — Reconstruire depuis l'application

La case où ce logiciel peut être **devant** les trois autres.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D9.1 | Glisser un fichier audio lance la chaîne, si Python est présent | absence de Python = fonction grisée **avec sa raison**, jamais une erreur |
| D9.2 | Avancement visible et annulable | séparation, transcription, recherche : chaque étape s'affiche |
| D9.3 | Le résultat arrive comme un projet **ouvert**, pas comme un dossier à charger | pistes, patchs, notes douteuses marquées |
| D9.4 | Écoute A/B étendue à tout le flux de travail | déjà faite pour un projet chargé |

**Critère de phase, et il est double** : la chaîne se lance depuis l'interface,
**et** le DAW se compile et fonctionne sans Python (règle n° 2 du § 0). Si tenir
les deux demande de compliquer le code, c'est la seconde qui gagne.

### Phase D10 — Le confort qui fait qu'on reste

Regroupées parce qu'aucune n'est structurante, et qu'aucune ne se remarque tant
qu'elle est là.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D10.1 | Navigateur : machines, presets, profils, échantillons, recherche, glisser-déposer | trouver un preset ne demande plus d'ouvrir un dossier |
| D10.2 | MIDI learn **persistant**, liste des associations, moyen d'en défaire une (`clearMidiLearn()` n'est appelé de nulle part), et cartographie du transport et du mixeur | un potentiomètre physique s'en souvient d'une session à l'autre |
| D10.3 | Raccourcis configurables, table imprimable, fenêtre de préférences | une page les liste tous |
| D10.4 | Sauvegarde automatique et récupération après plantage | tuer l'application ne perd pas plus d'une minute |

---

## 4. Les choix tranchés ici, et pourquoi

Conformément à l'usage de ce dépôt, les questions ouvertes se referment en
écrivant. Sept l'étaient.

**1. La vue Session d'Ableton (clips lancés en scènes) : non, et voici la
condition qui rouvrirait le débat.** Elle n'est pas un raffinement de la vue
d'arrangement, c'est un **second modèle temporel** — des clips déclenchés hors
de la ligne de temps, quantifiés au lancement. La construire signifierait tenir
deux ordonnanceurs, alors que le § D8.3 constate qu'on peine déjà à en tenir
**un** proprement. Or ce projet a un but écrit : reconstruire un enregistrement
existant et mesurer l'écart. Un enregistrement n'a pas de scènes, il a une ligne
de temps. **Condition de réouverture** : le jour où le projet sert à jouer sur
scène plutôt qu'à reconstruire, ce qui n'est écrit nulle part aujourd'hui.

**2. VST3 avant LV2, et CLAP avant les deux.** CLAP passe en premier parce que
l'hôte est **déjà écrit et testé** : le brancher est une demi-journée, et c'est
le meilleur rapport de tout le document. VST3 ensuite, parce que c'est là que
sont les instruments que les gens ont installés. LV2 après, ou jamais.

**3. L'étirement temporel (*warp*) s'écrira dans le projet, et pas tout de
suite.** D2 se contente du rééchantillonnage. Suivre le tempo en gardant la
hauteur demande un algorithme (WSOLA, vocodeur de phase), et la règle n° 2 du
§ 0 interdit une dépendance à télécharger. Il sera donc **écrit ici**, comme le
lecteur JSON l'a été, et il aura sa propre phase quand D2 sera acquis. Le dire
maintenant évite qu'on choisisse une bibliothèque par facilité au milieu de D2.

**4. Le format de projet passe en version 2 et reste un dossier.** Les clips et
les pistes audio entrent dans `ProjectDocument` ; `kProjectVersion` passe à 2 ;
**un projet v1 se charge toujours** et se convertit, la conversion étant testée
dans les deux sens. Le dossier (project.json + MIDI + presets + échantillons)
est conservé plutôt qu'une archive unique : c'est ce qui permet à `analyse/`
d'écrire un projet sans connaître le code du DAW, et cette propriété vaut plus
que la commodité d'un fichier unique.

**5. `MidiNoteEvent` s'élargit, il n'est pas remplacé.** Le pont entre le modèle
d'édition (14 types) et le moteur (2 types) doit s'ouvrir, mais **le contrat
`ISynthPlugin` reste le même pour les 34 machines existantes** : une machine qui
ignore un pitch bend continue de l'ignorer, et son empreinte ne bouge pas.
L'alternative — passer le `std::variant` de `core/` jusqu'au DSP — ferait entrer
le modèle d'édition dans le chemin temps réel, ce que le § 0 de
`ROADMAP-interop.md` interdit.

**6. La reconstruction reste en Python.** D9 l'appelle, ne la réécrit pas. Les
95 837 lignes d'`analyse/` s'appuient sur PyTorch, Demucs et Basic Pitch : les
porter en C++ serait un second projet, et il n'apporterait rien qu'on puisse
mesurer.

**7. Pas de piste vidéo, pas de partition.** Deux fonctions que Cubase a et qui
sont des métiers entiers. Les nommer ici évite d'y revenir à chaque relecture.

---

## 5. Ce qui n'est pas au programme, et pourquoi

- **Copier une interface existante.** Les façades de ce projet imitent des
  machines, jamais des logiciels. La disposition de l'écran doit se justifier
  par l'usage, pas par la ressemblance.
- **MPE et MIDI 2.0.** D0.5 ouvre le chemin des événements non-note ; l'expression
  par note est un cran au-dessus et ne sert aucune machine du parc aujourd'hui.
  À rouvrir le jour où une machine la demande.
- **La synchronisation externe** (MIDI Clock, MTC, LTC, Ableton Link). Utile en
  studio partagé, sans objet pour un logiciel qui tourne seul.
- **Un moteur d'échantillonnage de bibliothèque géante** (à la Kontakt).
  `vsm.multisample` couvre le report d'échantillons dont la reconstruction a
  besoin, et aucune banque n'est commise dans le dépôt.
- **Le nuage, la collaboration, les comptes.** Rien ne les demande, et chacun
  apporte un pan de problèmes sans améliorer une seule mesure.
- **La compatibilité avec les projets de Cubase, Live ou FL.** Lire un `.flp` ou
  un `.als` est de la rétro-ingénierie sans fin. Le MIDI et les stems sont les
  formats d'échange, et ils sont ouverts.
- **Compiler `vsm.cone` et `vsm.flute`.** Elles sont dans l'arbre avec leurs
  tests et hors du `CMakeLists`. Ce n'est pas un oubli : ce sont deux **résultats
  négatifs** conservés et documentés (ARCHITECTURE.md § 44 pour la flûte, qui ne
  s'auto-oscille pas une fois le blocage de continu posé). Elles restent hors
  build.

---

## 6. Invariants à vérifier à chaque phase

Ceux de la fusion, plus cinq propres à cet axe :

1. **Les 34 empreintes audio restent vertes et inchangées.** Une phase qui en
   modifie une est fausse jusqu'à preuve du contraire.
2. **`process()` reste sans allocation, sans verrou, sans I/O** — y compris
   quand une piste audio lit 47 Mo depuis le disque.
3. **Rendu temps réel et rendu hors ligne restent identiques**, à l'échantillon
   près, sur tout ce qui s'ajoute. Le test existe pour CLAP ; il s'étend.
4. **Le DAW se compile et s'utilise sans Python, sans réseau, sans CLAP.**
   Chaque phase le revérifie : c'est le genre de garantie qu'on perd sans s'en
   apercevoir.
5. **Rien ne se perd et rien ne ment.** Toute fonction ajoutée est sauvegardée
   dans le projet, présente à l'export, et sans commande morte. C'est l'acquis
   de D0, et le reperdre serait pire que ne l'avoir jamais eu.
