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

**État au 30/08/2026 : les onze phases sont faites, de D0 à D10.** Chacune porte
le compte rendu de ce qu'elle a coûté et de ce qu'elle a trouvé, à l'endroit où
elle est décrite — c'est là qu'il faut lire, pas ici. Ce qui reste ouvert est
nommé au § 5 (« ce qui n'est pas au programme, et pourquoi »), et les deux
reports assumés en cours de route ont été levés le 30/08/2026 : la façade
native des plugins CLAP (différée en D7.4 « faute d'un affichage pour l'ouvrir
au moins une fois » — l'affichage existe, la façade a été ouverte) et la pose
d'un échantillon depuis le navigateur (qui demandait une position, et l'a
trouvée dans l'arrangement).

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

> **CE TABLEAU EST DATÉ, ET C'EST SA RAISON D'ÊTRE.** Il mesure l'état du
> 31/08/2026, celui qui a fait écrire cette feuille de route ; il ne se met pas
> à jour, sans quoi la phrase qu'il justifie n'aurait plus de sens. Pour
> mémoire, au 10/09/2026 : `app/` fait **32 873 lignes**, soit 4,5 fois plus,
> et les cinq critères du § 2 sont tenus. L'état courant se lit dans
> `docs/INDEX.md`.

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

> **UNE NEUVIÈME, TROUVÉE LE 03/09/2026 EN PHOTOGRAPHIANT LES ONGLETS DU BAS.**
> L'onglet « MIDI CC » était un libellé — « vue dédiée (Phase 2 UI ; éditable
> dès maintenant via les lanes du piano roll) » — et sa promesse était fausse :
> aucune lane du piano roll n'édite les CC. Le modèle les porte, le
> séquenceur les joue (D0.5), l'import et l'export les conservent : une
> courbe de coupure importée d'un `.als` se JOUAIT sans pouvoir être vue ni
> corrigée. C'est exactement une chose « présente qui ment ». Corrigé le jour
> même : `MidiCcComponent` édite les contrôleurs (piste, contrôleur, points en
> paliers, historique, republication au séquenceur) ; les deux libellés
> « Phase 2 UI » du mixeur et de l'automation, morts depuis D4 et D5, sont
> retirés. Le même passage a corrigé trois choses moins graves, vues sur le
> projet de l'épreuve de parité : le piano roll s'ouvrait toujours sur C6 (une
> basse reconstruite montrait une fenêtre vide), une piste de batterie nommait
> ses touches par leur hauteur et non par leur pièce, une piste audio montrait
> une grille vide avec les notes fantômes d'une autre piste ; et un projet
> reconstruit ouvert à la main n'avait pas son original pour l'écoute A/B.
>
> **ET UNE DIXIÈME, LA MÊME APRÈS-MIDI, PAR ÉCHANTILLONNAGE DES PIXELS.** Un
> registre reconstruit de l'épreuve montrait sa bande de vélocité pleine et
> une grille vide : ses 164 notes étaient dessinées, mais quasi
> transparentes. La chaîne écrivait ses couleurs de piste en RGBA
> (« #06D6A0FF ») là où le format lit de l'ARGB : un alpha de 0x06 pour une
> piste sur huit, et une fausse teinte pour toutes les autres — la basse
> « rose » sortait bleue depuis toujours, et personne ne pouvait le savoir
> sans connaître la palette voulue. L'exportateur écrit désormais des
> couleurs opaques, et le DAW rend opaque à l'ouverture une couleur d'alpha
> trop bas, pour les projets déjà écrits.

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
> ~~Les polyphoniques suivront machine par machine~~ : une case a été ajoutée à
> la liste de contrôle du § 10 de `CDC-nouvelle-machine.md`, qui est l'endroit
> où ce projet range les obligations par machine. **Les polyphoniques SONT
> faites (01/09/2026)** : vingt-deux machines honorent la molette, celles qui
> refusent (résonateurs frappés, orgues, samplers, percussions) le font en
> connaissance de cause et le moteur compte leur refus — la doctrine, la liste
> complète et la leçon de l'estimateur de hauteur sont au § 10 du CDC.
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

> **BILAN DE LA PHASE D0 : LE CRITÈRE EST TENU.** Plus une commande morte —
> celles qui ne peuvent pas encore être vraies sont désactivées et disent
> pourquoi. Plus un réglage perdu : mixage, effets, automation, tranche master
> et région de boucle sont dans le projet et reviennent du disque. Plus un
> export qui diffère de l'écoute : il emprunte le chemin de `vsm-render`. Plus
> une donnée qui ne sonne pas : le MIDI non-note atteint les machines, et ce
> qu'une machine refuse est compté. **Deux restes, tous deux portés par une
> phase ultérieure** : l'historique global (D1.5) et l'enregistrement lui-même
> (D3).
>
> **Une leçon qui vaut pour la suite.** Trois des huit défauts venaient de la
> même cause : une donnée qui n'avait qu'**une seule copie vivante**, hors du
> projet — les chaînes d'effets dans un composant d'interface, l'automation
> dans un vecteur du `MainComponent`, la tranche master dans l'objet du moteur.
> Une donnée qui vit hors du projet n'est ni sauvegardée, ni exportée, ni
> rechargée, et personne ne s'en aperçoit puisqu'elle est correcte à l'écran.
> La règle qui en sort, et qui vaut pour toutes les phases suivantes : **si
> l'utilisateur peut le régler, le projet doit le porter, et l'interface ne doit
> en être qu'un reflet.**

---

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

> **D1.1 À D1.4 SONT FAITES, ET LE CRITÈRE EST VÉRIFIÉ POUR DE VRAI
> (29/08/2026).** 900 tests moteur verts.
>
> **La vérification n'est pas une promesse.** Le binaire `vsm-render` gelé au
> début de la session — construit **avant** la moindre modification de D0 et de
> D1 — a rendu les mêmes projets que le binaire d'aujourd'hui :
>
> | projet | résultat |
> |---|---|
> | `docs/examples/demo-project/` | **identique, octet pour octet** |
> | `reconstruction/travail/sky-v4` (4 pistes, sampler de 47 Mo, batterie) | **identique, octet pour octet** |
>
> **LE CHOIX DE CONCEPTION, ET CELUI QUI A ÉTÉ ÉCARTÉ.** Un clip pouvait être un
> **conteneur** qui emporte ses notes, comme chez Ableton : la piste ne serait
> plus qu'une liste de clips, chacun avec son vecteur de notes en ticks
> relatifs. On a retenu l'autre modèle, celui de la **région** — comme Pro Tools
> ou Logic : la piste garde son matériau sur une seule ligne de temps, et un
> clip est une **fenêtre** dessus, posée ailleurs. Trois raisons, dans cet
> ordre :
>
> 1. **D1.2 tombe tout seul.** « Un même clip placé deux fois ne duplique pas
>    ses notes ; éditer l'un modifie l'autre » : deux fenêtres sur le même
>    matériau lisent les mêmes notes, il n'y a jamais eu deux copies. Le modèle
>    conteneur exige une indirection explicite (un « clip source » partagé, des
>    instances qui le référencent) et tout le comptage de références qui va
>    avec.
> 2. **Aucune note ne change de place.** Le modèle conteneur obligeait à
>    réécrire tous les ticks en relatif, donc à toucher les **quatre-vingt-trois
>    endroits** qui manipulent `track.notes` — dont le piano roll entier — pour
>    un rendu qui doit rester identique au bit près. Beaucoup de risque, aucun
>    gain audible.
> 3. **Une piste sans clip garde exactement son comportement.** « Vide » ne veut
>    pas dire « cas particulier » : il veut dire *pas de découpe*, ce qui est le
>    sens littéral du mot. Et le planificateur n'a **pas** deux chemins : le cas
>    sans découpe est la fenêtre identité, si bien que l'absence de régression
>    est **démontrable** au lieu d'être promise — il n'y a pas de « chemin
>    historique » à côté qui pourrait diverger à la première correction.
>
> Ce que ce modèle coûte, et qui est assumé : éditer les notes « dans » un clip
> posé ailleurs édite le matériau **à sa position d'origine**. C'est le
> comportement d'un éditeur de régions, et c'est celui qu'on veut ici — un
> enregistrement reconstruit a UNE ligne de temps.
>
> **D1.3, la migration est vide, et c'est ce qui la rend sûre.** Une piste de la
> version 1 n'a pas de clip, c'est-à-dire qu'elle n'est pas découpée : un état
> parfaitement représentable en version 2. Rien à convertir, donc rien à rater.
> Un fichier version 2 est en revanche **refusé** par un logiciel qui ne lit que
> la 1 — refusé et non deviné, comme tout ce que ce format ne comprend pas. Et
> un projet sans clip ni repère **écrit exactement le même fichier qu'avant**,
> un test le vérifie octet par octet.
>
> **D1.4, les repères existaient sans exister.** Le format MIDI en porte depuis
> toujours (méta 0x06 et 0x07) et ce projet les conservait en octets opaques
> dans `Track::miscEvents` : lus, réexportés fidèlement, et **invisibles**. Ils
> deviennent des entités du projet, **globales** — « refrain » ne repère pas un
> endroit de la piste de basse, il repère un endroit du morceau. Un test vérifie
> qu'ils **ne se multiplient pas** par le nombre de pistes à chaque
> aller-retour, erreur qui ne se serait vue qu'au troisième enregistrement.
>
> **Et l'œil a servi, comme pour les façades.** L'outil d'aperçu hors écran rend
> désormais la règle en plus du piano roll. Premier rendu : « Refrain » et
> « Pont », trop proches au zoom courant, écrivaient **« Refrain Pont »** l'un
> par-dessus l'autre, et le premier mangeait le numéro de mesure du second. La
> place d'un nom s'arrête maintenant au repère suivant, avec un fond opaque
> derrière ; quand il n'y a pas la place d'écrire, **on n'écrit pas** — le
> fanion suffit à dire qu'il y a un repère là, un nom coupé à deux lettres ne
> dit rien du tout.
>
> **D1.5 EST FAITE À SON TOUR.** L'annulation porte désormais sur le PROJET :
> elle défait une note, un fader, un effet inséré, une piste ajoutée ou
> supprimée, un repère posé, un clip. Elle **survit au changement de piste**,
> alors que l'ancienne devait être vidée à chaque fois — regarder une autre
> piste effaçait tout ce qu'on pouvait annuler.
>
> **Une implémentation, deux portées.** `EditHistory` était déjà un historique
> par instantanés ; il est devenu `SnapshotHistory<T>`, et `EditHistory` comme
> `ProjectHistory` n'en sont que deux alias. Zéro code dupliqué, et les sept
> tests d'origine passent sans une ligne changée. **Le type de l'instantané dit
> ce que l'annulation couvre** : c'est toute la différence entre les deux.
>
> **Le coût est en mémoire, et il est chiffré.** Une `Note` pèse une trentaine
> d'octets ; *Sky and Sand* reconstruit en compte ~9 600 sur quatre pistes, soit
> quelques centaines de kilo-octets par instantané et quelques dizaines de
> méga-octets à la profondeur maximale. C'est le prix d'une annulation à
> laquelle on peut se fier, et il est assumé — l'alternative, un journal de
> commandes inversibles, demanderait d'écrire et de tester une inverse correcte
> pour chacune des vingt-et-quelques opérations d'édition, y compris les
> composées.
>
> **Deux pièges tendus par le geste lui-même, et évités.** Un glissé de fader
> émet son signal de changement à chaque échantillon de mouvement : s'en servir
> pour l'instantané aurait empilé trois cents pas d'annulation pour un seul
> mouvement. D'où un signal distinct, émis au DÉBUT du geste (`onMixEditStarted`,
> `onEditStarted`), comme pour la lane de vélocité. Et `setProject` vidait
> l'historique : or il est rappelé à chaque republication, **y compris après un
> annuler** — la pile aurait été effacée à l'instant même où l'on s'en sert. La
> remise à zéro est passée là où un vrai document change : nouveau projet,
> ouverture d'un MIDI, ouverture d'un dossier.
>
> **Et une régression d'usage attrapée au passage** : la republication remettait
> la vue sur la piste 0. Après un annuler, l'utilisateur se serait retrouvé au
> début du morceau à chaque geste. La piste regardée est conservée, et n'est
> ramenée à zéro que si elle n'existe plus.
>
> **LA PHASE D1 EST COMPLÈTE**, et son critère revérifié après D1.5 : les deux
> projets rendent toujours des fichiers identiques, octet pour octet, à ceux du
> binaire d'avant la session.

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

> **D2.1, D2.2, D2.3 ET D2.6 SONT FAITES, ET LE CRITÈRE EST MESURÉ
> (29/08/2026).** 917 tests moteur verts.
>
> **La preuve tient en deux chiffres.** Le même morceau rendu deux fois — la
> voix par l'ancien détour du sampler, puis la voix sur une vraie piste audio :
>
> | comparaison | écart maximal |
> |---|---|
> | piste audio contre report par sampler | **1,2 × 10⁻⁷** (l'epsilon du flottant) |
> | piste audio contre la même piste coupée | **0,476** |
>
> Le premier chiffre dit que la piste audio reproduit le détour **au bit de
> précision près** ; le second, qu'elle joue bel et bien — sans lui, le premier
> ne prouverait rien, puisque deux silences se ressemblent aussi.
>
> **UN CHOIX D'UNITÉS QUI ÉVITE UNE PROMESSE FAUSSE.** Tout le modèle est en
> ticks ; la fenêtre d'un clip audio est en **secondes**. Un tick est une
> position *musicale* : le convertir passe par la carte de tempo, et suppose
> donc que le matériau suit le tempo. Une note le fait, un enregistrement non —
> pas tant que l'étirement temporel n'est pas écrit (choix n° 3 du § 4). La
> position du clip reste musicale, son contenu est du temps réel.
>
> **DEUX PIÈGES ATTRAPÉS PAR L'ESSAI, ET AUCUN N'AURAIT FAIT DE BRUIT.**
> D'abord, le graphe écartait toute piste sans instrument : une piste audio n'en
> a pas et n'en aura jamais, et elle passait donc en silence. Ensuite, la
> longueur par défaut d'un clip venait du nombre de trames que le projet
> **déclare** ; sur un fichier dont l'en-tête avait été mal deviné, une voix de
> 532 s déclarée à 266 s se coupait au milieu du morceau. Quand la déclaration
> et le fichier divergent, c'est le fichier qui a raison.
>
> **Le préchargement en mémoire est un choix écrit** : 47 Mo sur le disque font
> 190 Mo décodés en flottants stéréo, ce qui est tenable pour la poignée de
> pistes que D2 doit débloquer. La diffusion depuis le disque — seule capable de
> vingt pistes de neuf minutes — est **D8.2**, et elle ne changera que la classe
> `AudioTrackSource`.
>
> **Le rééchantillonnage (D2.3) est une interpolation linéaire**, approximation
> assumée et documentée : sur le rapport courant 44,1 → 48 kHz, l'erreur reste
> sous le millième pour tout ce qui vit sous 10 kHz. Un noyau fenêtré fera mieux,
> et il sera écrit avec l'étirement temporel plutôt qu'emprunté.
>
> **Côté chaîne d'analyse**, la voix s'écrit désormais comme piste audio ;
> `--voix-sampler` rejoue l'ancien détour à l'identique, pour rouvrir un projet
> ancien. Le format ne passe en version 2 **que si le projet utilise une
> nouveauté de la version 2** : un projet sans piste audio reste en version 1 et
> s'ouvre partout.
>
> **D2.4 ET D2.5 SONT DÉPLACÉES DANS D5, ET VOICI POURQUOI.** Le gain, les
> fondus et l'inversion de phase par clip existent dans le modèle, se
> sérialisent et s'entendent au rendu ; ce qui manque est le moyen de les
> RÉGLER, c'est-à-dire un clip qu'on puisse saisir. De même, une forme d'onde se
> dessine sur une ligne de temps, et il n'y en a aucune : le piano roll montre
> des notes, la liste de pistes est une pile de tranches sans axe temporel. Les
> faire ici demanderait d'inventer une demi-vue d'arrangement qu'il faudrait
> jeter en arrivant à D5. Elles y sont donc rattachées, nommément (D5.6 et
> D5.7), plutôt que laissées cochées à moitié.
>
> **L'application charge et publie les pistes audio** au chargement d'un projet
> et à chaque changement de fréquence de la carte son — le matériau est décodé
> pour UNE fréquence, et le graphe n'en rééchantillonne pas en temps réel. Une
> piste qui ne charge pas est **nommée dans une boîte de dialogue** : à
> l'oreille, elle ne se distingue pas d'une piste dont on aurait baissé le
> volume.

### Phase D3 — Enregistrer

Un logiciel qui ne peut rien capter n'est pas un studio, c'est un lecteur.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D3.1 | Ouvrir le périphérique **avec des entrées**, choisir la source, écouter l'entrée, mémoriser le choix du périphérique | le niveau d'entrée s'affiche ; `AudioEngine.cpp:11` n'ouvre plus zéro entrée ; le réglage survit au redémarrage |
| D3.2 | Tempo **modifiable**, tap tempo, piste de tempo dessinée, **métronome** et décompte | on peut commencer un morceau à partir de rien, ce qui est impossible aujourd'hui |
| D3.3 | Enregistrement MIDI temps réel : armement (`Track::armed` enfin lu), superposition, quantification après coup | jouer trois mesures les inscrit dans un clip — **fait** |
| D3.4 | Enregistrement audio en flux sur disque pendant la lecture | 10 minutes s'enregistrent sans décrochage ; le fichier est relu tel quel — **fait** |
| D3.5 | Punch in/out, enregistrement en boucle avec prises empilées | les prises se conservent et se choisissent — **fait** |
| D3.6 | Latence d'entrée **mesurée**, pas estimée, et compensée | une boucle physique enregistre à l'échantillon près ; le chiffre est publié — **fait** |

**Critère de phase** : on peut jouer une partie au clavier par-dessus une
reconstruction et la garder — c'est-à-dire faire de la musique dans le logiciel,
pas seulement l'écouter.

> **LA PHASE D3 EST CLOSE (29/08/2026).** Ses six étapes sont faites, et le
> critère de phase est tenu : une piste s'arme, un décompte se lance, on joue,
> et ce qu'on a joué est là — au clavier comme au micro, en une passe ou en
> vingt dont on garde la bonne. Le détail de chaque étape est en dessous.

> **D3.1 EST FAITE (29/08/2026).** Le moteur demande **deux entrées** là où il
> en ouvrait zéro, le sélecteur de périphérique les laisse choisir (et donne
> enfin l'onglet MIDI), le choix du périphérique est **conservé** d'une
> exécution à l'autre — il ne l'était pas, et il fallait rechoisir sa carte, sa
> fréquence et sa taille de bloc à chaque lancement — et la barre de transport
> porte un **témoin d'entrée** à côté du bouton Rec.
>
> **Les entrées sont DEMANDÉES, pas exigées.** Une machine dont la carte n'a que
> des sorties doit rester utilisable pour éditer, mixer et exporter : le moteur
> retente alors sans entrée. Mais il ne le cache pas — le témoin est **barré**
> quand aucune entrée n'est ouverte, parce qu'un bargraphe vide dirait « rien
> n'arrive », ce qui n'est pas « rien ne peut arriver ».
>
> **Pourquoi un témoin avant même de savoir enregistrer.** Brancher un micro et
> ne rien voir est le premier échec possible d'un enregistrement, et il n'a rien
> à voir avec l'enregistrement : c'est la carte, le câble ou le canal. Un témoin
> permanent sépare les deux questions avant qu'on ne les confonde.
>
> **D3.2 EST FAITE, sauf le décompte** (qui n'a de sens qu'avec
> l'enregistrement, D3.3). *La « piste de tempo dessinée » du rang, elle,
> manquait encore à ce compte rendu et n'est arrivée que le 03/09/2026 : le
> moteur suivait déjà une carte de tempo à chaque bloc et l'import la
> remplissait, mais rien ne la montrait ni ne l'éditait, et le tempo de la
> barre de transport l'EFFAÇAIT à chaque changement. L'onglet Tempo l'édite
> en paliers (`TempoLaneComponent`), avec historique et republication ; la
> barre de transport ne change plus que le tempo de départ.* Le tempo **s'édite** — il n'était qu'un `juce::Label`
> jamais rendu éditable, dont la valeur venait du projet importé : on ne pouvait
> donc pas commencer un morceau à partir de rien, ce qui est le premier geste
> d'un studio. Il se frappe aussi au bouton **Tap** (moyenne des intervalles des
> quatre dernières frappes ; une pause d'une seconde et demie recommence le
> compte). Et le changer est une action **annulable**, comme le reste.
>
> **LE MÉTRONOME N'EST PAS UNE MACHINE DU PARC, ET C'EST RAISONNÉ.** Toutes les
> autres sources de son de ce projet sont des instruments : elles reçoivent des
> notes, se règlent, s'exportent en preset, figent leur rendu par une empreinte.
> Le métronome n'est rien de cela — il ne joue pas de musique, il compte. Le
> faire passer par une piste et un instrument obligerait à inventer des notes
> absentes du morceau, à les écarter de l'export, et à expliquer pourquoi une
> piste ne s'entend pas au rendu. Il vit donc dans le graphe, **après la tranche
> master** comme la piste de référence : le faire passer par le compresseur
> ferait plonger tout le mixage à chaque temps. Et il ne s'exporte **jamais** —
> le rendu hors ligne monte son propre graphe et ne l'allume pas.
>
> Cinq tests le couvrent, dont deux sur le seul piège de la chose : un intervalle
> qui commence entre deux temps ne doit pas en inventer un (sans quoi le
> métronome battrait la mesure du **bloc audio** et non celle du morceau), et
> deux blocs qui se suivent ne doivent pas compter deux fois le temps de leur
> frontière.

> **D3.3 EST FAITE, DÉCOMPTE COMPRIS (29/08/2026).** `Track::armed` était écrit
> par un bouton et **lu par personne** : on pouvait armer une piste, et rien
> n'arrivait. Le bouton R agit maintenant sur deux choses à la fois, et c'est
> voulu — la piste armée reçoit le clavier **à l'écoute** comme **à
> l'enregistrement**. Jouer sur une piste et enregistrer sur une autre n'aurait
> aucun sens, et le bouton Rec reste désactivé tant qu'aucune piste n'est armée
> plutôt que de rester rouge et inerte : sans piste armée, la prise n'a nulle
> part où aller.
>
> **LE DÉCOMPTE EST UN MORCEAU DE LIGNE DE TEMPS SITUÉ AVANT ZÉRO.** C'est le
> choix qui porte toute l'étape. `ProcessGraph::seekSeconds` rabotait la
> position à zéro ; elle accepte désormais les valeurs négatives, et compter
> deux mesures se réduit à sauter à `-2 mesures` et à jouer. Tout le reste suit
> sans une ligne de plus : le planning n'a aucun événement là, les clips audio
> et la piste de référence n'y rencontrent que du silence, le rebouclage ne se
> déclenche qu'une fois sa fin franchie, et le **dernier clic tombe exactement
> sur le premier temps du morceau** — ce qui est toute la raison d'être d'un
> décompte. L'alternative était un second ordonnanceur de pré-écoute, c'est-à-dire
> une deuxième horloge à tenir d'accord avec la première.
>
> Le clic bat **même métronome éteint** tant que la position est négative : un
> décompte qu'on n'entend pas ne compte rien.
>
> **DATER UNE NOTE JOUÉE : L'ANCRE.** Les messages d'un clavier arrivent sur le
> thread MIDI, datés par le pilote sur l'horloge du système ; le morceau, lui,
> est daté par l'horloge du transport, que seul le thread audio fait avancer.
> Lire `currentSeconds()` à l'arrivée du message donnerait à toutes les notes
> d'un même bloc la **même** date — une quantification involontaire à la taille
> de bloc, soit 10,7 ms à 512 échantillons, largement audible sur une double
> croche. Le thread audio publie donc à chaque bloc une **ancre** (heure
> système, position du transport) et le thread MIDI interpole. La publication
> passe par un compteur de version (*seqlock*) et non par un verrou, qui est
> interdit au thread audio ; la précision devient celle de l'horodatage du
> pilote.
>
> **La latence retranchée est DÉCLARÉE, pas mesurée**, et le code le dit à
> l'endroit où il la retranche. On joue en réaction à ce qu'on **entend**, et ce
> qu'on entend a déjà pris le retard de la sortie : sans correction, toute prise
> serait systématiquement en retard d'une dizaine de millisecondes. Le chiffre
> annoncé par le pilote vaut mieux que rien ; la mesure réelle par boucle
> physique reste l'objet de **D3.6** et le remplacera sans rien changer d'autre.
>
> **L'ARMEMENT NE SE SAUVEGARDE PAS, ET C'EST TRANCHÉ ICI.** C'est un état de
> session, pas une donnée de morceau. Rouvrir un projet avec une piste
> silencieusement armée ferait écrire la prise suivante à un endroit qu'on n'a
> pas désigné, et c'est le genre de surprise qu'un enregistrement ne pardonne
> pas. Le **décompte** (0, 1 ou 2 mesures) et le **mode** (superposer /
> remplacer) sont, eux, conservés d'une exécution à l'autre comme l'échelle
> d'interface : ce sont des façons de travailler, pas des propriétés du morceau.
>
> **LA QUANTIFICATION APRÈS COUP NE FAIT PAS DE CHEMIN À ELLE.** À la fin d'une
> prise, les notes enregistrées sont **sélectionnées** dans le piano roll ; la
> commande Quantifier existante porte alors exactement sur ce qu'on vient de
> jouer, avec la grille, le swing et la force que l'utilisateur a réglés. Le
> menu *Enregistrement* propose le geste en un coup. Écrire une seconde
> quantification aurait donné deux comportements à faire coïncider.
>
> **En mode `Replace`, on efface ce qui COMMENCE dans la prise**, pas ce qui la
> traverse : une note tenue commencée avant le point d'entrée appartient à ce
> qui précède, et l'effacer détruirait hors de la région qu'on a désignée.
>
> Treize tests couvrent le seul vrai travail de l'enregistrement — transformer
> des touches en notes — et tous les cas où l'appariement se casse : la touche
> encore tenue quand on arrête (conservée, fermée à l'arrêt), le relâchement
> dont l'enfoncement précède la prise (ignoré, parce qu'inventer une note serait
> écrire ce qu'on n'a pas joué), la même hauteur frappée deux fois avant d'être
> relâchée une fois (premier enfoncé, premier fermé), la note trop brève pour
> être mesurée (un tick de plancher), et la prise faite après un changement de
> tempo.
>
> **Deux défauts trouvés en chemin, corrigés ici.** Le métronome n'était
> **jamais préparé** : il gardait sa fréquence d'échantillonnage par défaut de
> 48 kHz quoi que fasse la carte, donc à 44,1 kHz — le régime le plus courant —
> son clic sortait un demi-ton trop bas et durait 9 % de trop. Personne ne s'en
> plaignait parce qu'un clic faux ressemble à un clic. Et le champ
> `Track::monitoring`, écrit par personne et lu par personne, a été **retiré** :
> l'armement dit déjà « c'est cette piste qui écoute mon clavier ».

> **D3.4 EST FAITE (29/08/2026).** L'entrée était MESURÉE depuis D3.1 — le
> témoin de la barre de transport le montrait — mais rien n'en était fait. Elle
> s'écrit désormais dans un fichier, pendant que ça joue.
>
> **LE PROBLÈME EST UN PROBLÈME DE THREADS, pas de format.** Le rappel audio
> reçoit les échantillons d'entrée et doit rendre la main en quelques
> millisecondes ; écrire un fichier depuis là — un appel système, une
> allocation, l'attente d'un disque — produirait des craquements à la première
> hésitation du système de fichiers. Un enregistrement de dix minutes ne peut
> pas dépendre de la bonne humeur du noyau. Le thread audio ne fait donc que
> **déposer** ses blocs dans une file d'une seconde, qu'un thread de fond
> écrit. C'est `juce::AudioFormatWriter::ThreadedWriter`, **employé plutôt que
> réécrit** : il fait partie de JUCE, il est éprouvé, et la règle n° 2 du § 0
> n'interdit que les dépendances à **télécharger**. Ce qui est à nous, en
> revanche, c'est l'accès : le canevas fourni par JUCE prend un verrou dans le
> rappel audio, ce que ce projet ne s'autorise nulle part ; le rédacteur est
> donc publié par un `std::atomic<std::shared_ptr<>>`, comme le graphe le fait
> pour ses instruments.
>
> **LE POINT D'ENTRÉE EST RESPECTÉ À L'ÉCHANTILLON.** Il tombe où il tombe, y
> compris au milieu d'un bloc : on n'écrit donc que la **queue** du bloc à
> partir de lui. Commencer au début du bloc qui le contient donnerait à chaque
> prise un décalage aléatoire allant jusqu'à 10,7 ms — le défaut même qu'on
> avait évité côté MIDI avec l'ancre. Le premier échantillon du fichier est
> celui du point d'entrée, ce qui permet de poser le clip **à** ce point, sans
> décalage à corriger.
>
> **UNE PISTE AUDIO PORTE UN SEUL FICHIER**, et c'est ce qui décide du reste :
> une nouvelle prise **remplace** le matériau de sa piste, quel que soit le mode
> d'enregistrement. Superposer deux prises audio demanderait plusieurs matériaux
> par piste, ce que le modèle n'a pas — c'est l'objet de **D3.5**, où les prises
> s'empilent et se choisissent. Le menu *Enregistrement* le dit, plutôt que de
> laisser croire que le réglage « superposer / remplacer » la concerne.
>
> **Trois refus, tous prononcés AVANT qu'on ait joué.** Découvrir après trois
> minutes que rien n'a été écrit serait la pire façon de l'apprendre. Une prise
> audio exige donc : une seule piste audio armée (une entrée, une prise), un
> **projet déjà enregistré** (le format range les fichiers par chemin relatif à
> son dossier, ce qui est la condition pour ouvrir le projet sur une autre
> machine), et une entrée ouverte. Le nom de fichier est toujours **libre** :
> écraser une prise parce qu'on a rearmé la même piste serait la faute la moins
> pardonnable d'un enregistreur.
>
> **On pouvait enregistrer, mais pas créer de piste où le faire.** `Kind::Audio`
> n'apparaissait que dans le chargement : les pistes audio ne pouvaient venir
> que d'un projet importé. *Piste ▸ Ajouter une piste audio* existe désormais,
> et une piste audio n'affiche plus de sélecteur d'instrument — lui promettre un
> choix de machine sans effet serait mentir — mais **son fichier**, ou le fait
> qu'elle n'en a pas encore, ce qui est exactement ce qu'on a besoin de savoir
> avant d'appuyer sur Rec.
>
> **LE CRITÈRE EST VÉRIFIÉ, PAS AFFIRMÉ.** « Dix minutes sans décrochage, le
> fichier relu tel quel » n'est pas une propriété qu'on lit dans le code : elle
> dépend du disque, du tampon et du thread d'écriture. Elle ne demande en
> revanche ni micro ni écran, puisqu'on peut fabriquer le signal d'entrée. D'où
> `vsm-disk-record-check`, qui pousse un signal dont **chaque échantillon est
> différent des autres** (une sinusoïde pure ne montrerait pas un décalage d'une
> trame), à **vingt fois le temps réel** — vingt fois plus dur que l'usage, et
> dix minutes se vérifient en trente secondes. Mesure du 29/08/2026 :
> **600 s à 44,1 kHz par blocs de 256, zéro bloc perdu, 26 460 000 trames
> relues, écart maximal 1,19 e-7** — soit le quantum de 24 bits, c'est-à-dire
> exactement ce qui a été écrit.
>
> Pousser *sans* frein, en revanche, fait déborder le tampon : c'est la première
> chose qu'a montrée l'outil, et ce n'est pas un défaut de l'enregistreur mais
> une boucle de test sans cadence. Aucun producteur réel n'est plus rapide que
> la carte son.

> **D3.5 EST FAITE (29/08/2026).** Le critère tient en cinq mots — « les prises
> se conservent et se choisissent » — et c'est un choix de modèle qui les rend
> possibles.
>
> **LE MODÈLE RETENU EST CELUI DU RANGEMENT, et celui qui a été écarté est celui
> des N matériaux simultanés.** Une piste pourrait porter toutes ses prises en
> permanence et n'en jouer qu'une : le planning, le piano roll, le rendu et
> l'export devraient alors tous savoir laquelle, c'est-à-dire que chacun des
> quatre-vingts endroits qui lisent `Track::notes` devrait poser la question.
> Ici, la piste garde **un seul matériau courant** — exactement celui qu'elle a
> toujours eu — et les prises inactives attendent à côté. Choisir une prise,
> c'est ranger le matériau courant dans la prise à laquelle il appartient et
> sortir celui de la prise voulue. Rien de ce qui lit une piste n'a eu à
> changer, et `takes` vide veut dire « aucune prise empilée » : une piste qui
> n'a jamais servi à un enregistrement empilé se comporte exactement comme
> avant. C'est la même règle qui avait fait choisir le modèle de la **région**
> pour les clips.
>
> **CE QUI ÉTAIT LÀ AVANT LA PREMIÈRE PRISE DEVIENT LA PRISE N° 0.** Sans cela,
> le premier enregistrement empilé effacerait le matériau existant —
> typiquement une partie reconstruite, c'est-à-dire ce qu'on avait de plus
> précieux. Le coût assumé du modèle, écrit dans `Take` : éditer des notes
> modifie la prise **active**, et seulement elle.
>
> **LE PIÈGE DE L'ENREGISTREMENT EN BOUCLE : deux passes occupent exactement les
> mêmes positions.** La date d'une note ne dit donc pas à quelle passe elle
> appartient, et sans rien de plus les passes formeraient une bouillie dont on
> ne pourrait plus rien extraire. Le moteur **compte ses rebouclages**, le
> compteur voyage dans l'ancre — avec la position, sous le même compteur de
> version, parce que les deux doivent être vues ensemble — et chaque note capté
> est estampillée de sa passe. Une note tenue par-dessus la couture se ferme à
> la fin de **sa** passe, et son relâchement, qui appartient à la suivante, y
> est ignoré faute d'enfoncement : chaque passe est un enregistrement complet en
> elle-même.
>
> **UNE PASSE EN BOUCLE N'OUVRE PAS UN NOUVEAU FICHIER AUDIO.** Toutes les
> passes partagent le fichier ouvert au début de la session, et chacune en est
> une **fenêtre** — le modèle de la région, encore. Le découper au passage exact
> de la boucle demanderait de fermer et rouvrir un fichier au seul endroit où il
> ne faut surtout pas faire de pause.
>
> **LES NOTES DES PRISES VONT DANS UN FICHIER SÉPARÉ**, `midi/prises.mid`. Elles
> ne peuvent pas aller dans `arrangement.mid` : celui-ci est ce qu'on **entend**,
> et c'est aussi ce qu'on exporte pour l'ouvrir ailleurs. Y verser les passes
> écartées en ferait une archive, et montrerait des pistes muettes à qui
> l'ouvrirait. Deux fichiers, deux rôles : l'arrangement et le tiroir. Un projet
> sans prise n'écrit pas le second, et un tiroir manquant **ouvre le morceau en
> le disant** plutôt que de refuser — mais perdre des prises en silence serait
> pire que ne pas les avoir gardées.
>
> **La région de punch est une donnée de MORCEAU**, pas un réglage de session :
> on refait le même passage vingt fois, et la redéfinir à chaque ouverture
> reviendrait à perdre l'endroit qu'on a mis dix minutes à cerner. Elle se
> dessine **Alt + glisser sur la règle**, comme la boucle avec Maj, et elle est
> peinte en rouge sur une bande basse — les deux régions se règlent souvent au
> même endroit, et superposées à l'identique on ne saurait plus laquelle on
> regarde. Les deux bornes sont respectées **à l'échantillon** des deux côtés :
> le MIDI par les bornes du `MidiRecorder`, l'audio en tronquant la queue du
> bloc comme il en tronquait déjà la tête.
>
> **Un défaut trouvé en écrivant, et corrigé : l'annulation ne couvrait que la
> dernière passe.** L'instantané était pris à l'arrêt, alors que les passes
> précédentes avaient déjà modifié le projet. Il est désormais pris à la
> **première passe qui produit quelque chose** : annuler un enregistrement le
> défait en entier, ce qui est la seule chose qu'on attende de cette commande.
>
> Vingt-quatre tests couvrent l'étape : sept sur le modèle de rangement (dont le
> coût assumé — une correction reste sur la prise où on l'a faite), trois sur le
> punch et les passes dans l'enregistreur, et six sur l'aller-retour disque,
> dont celui qui vérifie que l'arrangement ne porte **jamais** les prises mises
> de côté.

> **D3.6 EST FAITE (29/08/2026). LA PHASE D3 EST CLOSE.** La latence était
> **déclarée** par le pilote et retranchée telle quelle, et le code de D3.3 le
> disait à l'endroit où il la retranchait, en renvoyant ici. Elle se **mesure**
> désormais.
>
> **ON MESURE UNE DATE, PAS UNE AMPLITUDE.** Ce qui est émis est un **balayage
> de fréquence** de 30 ms, pas un clic. Un clic est plus simple à fabriquer,
> mais sa détection repose sur un seuil, donc sur le bruit ambiant : dans une
> pièce, ou sur une entrée à fort gain, on trouve son seuil avant son clic. Un
> balayage ne ressemble à rien d'autre — sa corrélation avec lui-même est un pic
> étroit, sa corrélation avec du bruit est plate. La corrélation est calculée en
> direct, sans transformée de Fourier : quelques millions de multiplications,
> une fois, sur le thread de l'interface, à la demande de l'utilisateur. Une FFT
> irait plus vite et demanderait cent lignes de plus qu'il faudrait tester.
>
> **UN CHIFFRE PEU NET EST REFUSÉ, PAS PUBLIÉ.** C'est le cas du câble non
> branché : la corrélation trouve toujours un maximum quelque part dans le
> bruit. L'adopter décalerait toutes les prises suivantes d'une valeur inventée
> qu'on ne remettrait jamais en question — bien pire que de ne pas compenser.
> La netteté du pic (le rapport entre le maximum et la moyenne) est le garde-fou
> et elle est affichée avec le résultat.
>
> **CE QU'ON COMPENSE, ET CE QU'ON NE PEUT PAS COMPENSER.** L'échantillon
> d'entrée qui arrive au bloc dont le transport est à P a été joué en réaction à
> ce qu'on entendait, c'est-à-dire à ce que le moteur avait émis un aller-retour
> plus tôt : son intention musicale se situe à P − R. Les prises **audio** sont
> donc avancées de R. Les prises **MIDI**, elles, continuent d'employer la
> latence de sortie annoncée par le pilote : un clavier n'est pas dans la
> boucle, et cette mesure ne peut rien en dire. Le dialogue le dit à
> l'utilisateur au moment où il publie le chiffre, plutôt que de laisser croire
> que tout est réglé.
>
> **LE CHIFFRE EST PUBLIÉ** — le critère l'exige, et ce n'est pas un détail :
> corriger sans pouvoir lire de combien ne permettrait même pas de savoir si la
> correction a eu lieu. Le menu *Enregistrement* affiche en permanence la valeur
> retenue en millisecondes et en échantillons, ou « jamais mesurée — les prises
> audio ne sont pas compensées ». La mesure est conservée d'une exécution à
> l'autre : elle décrit la machine et sa carte, pas le morceau.
>
> **LE CRITÈRE EST VÉRIFIÉ SANS CÂBLE, ET C'EST LE VRAI CHEMIN QUI EST
> ÉPROUVÉ.** La boucle demande un câble ; tout ce qui se trouve entre le câble
> et le résultat — l'émission dans le rappel audio, la capture, la corrélation —
> n'en demande aucun. `vsm-latency-check` branche donc un **faux périphérique**
> qui renvoie dans l'entrée ce que l'application vient d'écrire dans la sortie,
> avec le retard qu'on choisit. Seul le pilote est remplacé ; le rappel de
> `AudioEngine`, ses tampons et son automate de mesure sont ceux de
> l'application. Mesures du 29/08/2026 : **64, 256, 333, 512, 1234, 2049 et
> 4096 échantillons retrouvés exactement**, à 44,1, 48 et 96 kHz, par blocs de
> 64 à 512.
>
> L'outil a d'ailleurs commencé par échouer d'exactement une taille de bloc, et
> c'était la **simulation** qui avait tort : un rappel audio lit son entrée
> avant d'écrire sa sortie, donc l'aller-retour le plus court qu'on puisse
> imposer par simulation vaut un bloc. Du vrai matériel peut faire mieux, et
> l'origine de la mesure — l'entrée du bloc où le balayage part — est
> précisément ce qui permet de le voir.

### Phase D4 — La console

| Étape | Contenu | Terminé quand |
|---|---|---|
| D4.1 | **Égaliseur, compresseur, porte, limiteur enfichables par piste** — le DSP existe déjà dans `MasterBus`, il n'est pas exposé | quatre effets de plus dans `EffectFactory`, chacun conforme au CDC (identités sémantiques, façade, empreinte) — **fait** |
| D4.2 | Bus de groupe et départs **libres** (aujourd'hui : deux, figés en dur sur Reverb et Delay dans le constructeur de `MainComponent`) | une réverbération se partage entre pistes ; le nombre de départs n'est plus une constante — **fait** |
| D4.3 | Départs pré/post-fader au choix (post-fader est codé en dur, `ProcessGraph.cpp:465`) | commutable par départ — **fait** |
| D4.4 | **Chaîne latérale** (*sidechain*) | le compresseur d'une piste écoute une autre piste — la signature même du genre que ce projet reconstruit — **fait** |
| D4.5 | **Compensation de latence (PDC)** : `ISynthPlugin` et `IAudioEffect` déclarent leur latence, le graphe la compense | insérer un effet à latence connue ne décale plus la piste ; test avec une latence artificielle et un `Oversampler` — **fait** |
| D4.6 | Automation de **tout** : volume, pan, départs, paramètres d'effets, master — aujourd'hui les paramètres d'instrument seulement (`ProcessGraph.cpp:325`) | un fondu écrit en automation s'entend — **fait** |
| D4.7 | Mesure : crête **et** RMS par piste, LUFS, corrélation de phase | affichés, et cohérents avec ce que `analyse/` mesure du même signal — **fait** |

**Critère de phase** : le mixage fait dans l'application et le mixage fait par
`analyse/` sur les mêmes stems donnent le même LUFS à 0,1 près. Les deux moitiés
du projet mesureront enfin la même chose.

> **LA PHASE D4 EST CLOSE (30/08/2026).** Ses sept étapes sont faites, et le
> critère est tenu **avec cinq cents fois la marge demandée** : l'écart mesuré
> entre le moteur et `analyse/` est de **0,0002 LU** au pire, là où le critère
> en tolérait 0,1. Le détail de chaque étape est en dessous.

> **D4.1 EST FAITE (29/08/2026).** Le DSP existait, entier et testé, et n'était
> accessible QUE sur le master. Une console dont on ne peut pas égaliser une
> piste n'est pas une console — c'est un bus de sortie avec des réglages. Le
> mixage se fait piste par piste ; le master ne fait que terminer.
>
> **CE QUI A ÉTÉ ÉCRIT N'EST PAS DU TRAITEMENT, C'EST UN HABILLAGE.** `Biquad`,
> `Compressor` et `Limiter` sont les mêmes classes que celles du bus master, aux
> mêmes coefficients ; les rendre enfichables demandait une liste de paramètres,
> des atomiques et un `process`. Recopier le DSP aurait donné deux compresseurs
> à faire coïncider, et ils auraient fini par diverger. Seule la **porte
> n'existait pas** : le master a un égaliseur, un compresseur, une saturation et
> un limiteur, mais rien pour faire taire ce qui est sous un seuil.
>
> **QUATRE EFFETS ET NON UNE SEULE TRANCHE**, parce qu'une tranche unique
> imposerait l'ordre égaliseur → compresseur → porte → limiteur à qui n'en veut
> qu'un — et cet ordre n'est pas celui qu'on veut toujours : une porte se place
> **avant** le compresseur pour ne pas ouvrir sur le souffle qu'il vient de
> remonter. Quatre inserts se rangent dans l'ordre qu'on décide, comme les neuf
> autres.
>
> **LA FAÇADE EST GRATUITE, ET CE N'EST PAS UN RACCOURCI** : `EffectChainComponent`
> construit son interface depuis la `ParameterList` de l'effet. Un effet qui
> déclare ses réglages a son panneau, ses noms et ses unités sans une ligne
> d'interface — c'est la contrepartie du modèle de paramètres unique choisi en
> Phase 2, et elle se touche ici pour la première fois.
>
> **LES EMPREINTES : LES EFFETS N'EN AVAIENT AUCUNE.** Les trente-quatre machines
> étaient protégées de la dérive, les treize effets ne l'étaient pas — alors
> qu'ils partagent leurs briques (`Biquad`, `Dynamics`) avec elles. Une dérive
> dans l'une change le son de tout un mixage sans faire échouer un seul test de
> propriété : « le compresseur réduit au-dessus du seuil » reste vrai qu'il
> réduise de 3 ou de 6 dB. Le harnais existant a donc été étendu aux effets, et
> **les treize** ont désormais leur empreinte, pas seulement les quatre
> nouveaux.
>
> **DEUX FOIS OÙ LE HARNAIS A EU RAISON CONTRE CELUI QUI L'ÉCRIVAIT.** Le
> garde-fou « empreinte transparente » — le pendant, pour un effet, de
> l'empreinte muette d'une machine — a refusé la porte du premier coup : le
> signal d'épreuve portait sur son canal droit une sinusoïde d'amplitude
> **constante**, et les dynamiques de ce moteur étant stéréo-liées, cette petite
> constante maintenait la porte grande ouverte d'un bout à l'autre. Puis la
> calibration a montré qu'une dérive de +5 % sur la fréquence des biquads
> faisait échouer deux **machines** et pas l'égaliseur, dont le réglage
> d'épreuve était trop doux pour mordre. Les deux ont été corrigés, et les
> réglages retenus sont mesurés :
>
> | Dérive injectée | Ce qui échoue |
> |---|---|
> | temps des dynamiques +3 % | rien (marge de bruit voulue) |
> | temps des dynamiques +10 % | le compresseur |
> | fréquence des biquads +5 % | l'égaliseur |
>
> Aucune de ces dérives ne faisait échouer le moindre test de propriété des
> effets. C'est exactement le trou que ces empreintes comblent.

> **D4.2 EST FAITE (29/08/2026).** Deux moitiés, et la première réparait plus
> qu'une rigidité.
>
> **LES DÉPARTS ÉTAIENT DEUX, ET SURTOUT ILS ÉTAIENT INVISIBLES.** Le nombre
> était une constante du moteur, les deux effets étaient figés dans le
> constructeur de l'application sur une réverbération et un delay — et les
> niveaux d'envoi, eux, ÉTAIENT sauvegardés. Un projet portait donc des réglages
> qui pointaient vers deux effets que rien, ni le fichier ni l'interface, ne
> nommait. Le projet déclare désormais SES bus, avec leur nom, leur effet et le
> gain de leur retour ; le mixeur donne un bouton par bus nommé, et son
> infobulle dit lequel.
>
> **RETIRER UN BUS DÉCALE LES NIVEAUX DES PISTES AVEC LUI.** Sans cela, le
> départ qui visait le bus 2 viserait le bus 1 : la piste partirait dans le
> mauvais effet sans qu'aucun bouton n'ait bougé. Le plafond de huit reste, mais
> ce n'est plus le nombre : c'est la taille des tampons, que le chemin temps
> réel n'alloue pas — et le franchir est **compté**.
>
> **UN PROJET D'AVANT QUI A DES NIVEAUX SANS BUS DÉCLARÉ retrouve les deux
> siens**, parce que ces niveaux ont été écrits pour eux. Un projet sans bus ET
> sans niveau n'a rien perdu : on le laisse tranquille, sinon les deux départs
> reviendraient à chaque ouverture chez qui n'en veut aucun.
>
> **UN GROUPE EST UNE PISTE**, et ce n'est pas un raccourci : il a un nom, un
> volume, un panoramique, un muet, un solo, une chaîne d'inserts, des départs et
> une automation — exactement ce qu'a une piste. En faire un troisième objet
> aurait obligé le mixeur, l'éditeur d'effets, l'automation et le format à
> connaître deux choses là où une seule suffit. Le rendu se fait en **deux
> passes** plutôt qu'en triant les pistes : l'ordre des pistes appartient à
> l'utilisateur, pas au moteur.
>
> **UN GROUPE NE VA JAMAIS DANS UN GROUPE.** Les groupes imbriqués demanderaient
> un ordre topologique et une détection de cycle pour un besoin que rien n'a
> exprimé ; un seul niveau couvre l'usage réel (batterie, claviers, voix). Un
> routage de groupe vers groupe est **ignoré et part au master**, ce qui
> s'entend — là où une boucle ferait tourner le rendu en rond, littéralement.
>
> **UN TEST A TROUVÉ UNE FAUTE DE MIXAGE QUI SE SERAIT ENTENDUE.** Router deux
> pistes dans un groupe neutre les rendait **3 dB plus faibles** que sans
> groupe : la loi de panoramique à puissance constante vaut 0,707 sur les deux
> canaux au centre, et le groupe l'appliquait une seconde fois. Grouper serait
> devenu un choix qu'on paie. Un groupe reçoit un signal DÉJÀ stéréo : son
> réglage n'est pas un panoramique mais une **balance**, qui vaut un au centre
> et n'atténue que le canal opposé — ce que fait le potentiomètre de balance
> d'une tranche stéréo sur une console. Un test vérifie désormais qu'un groupe
> neutre est traversable **sans perte**.
>
> **LA SUPPRESSION D'UNE PISTE RÉPARE LES ROUTAGES**, et cette règle est dans
> `core/` et non dans l'interface : un routage qui visait la piste 5 viserait la
> 4, et le mixage partirait dans un autre groupe sans qu'aucun réglage n'ait
> bougé. C'est le défaut qui avait déjà fait ranger les chaînes d'effets DANS la
> piste ; il revient dès qu'une piste en référence une autre, et une règle qu'on
> ne peut pas tester n'est qu'une intention.
>
> **Deux pièges tendus par le passage du tableau de deux au vecteur**, tous deux
> attrapés par les tests existants : indexer un vecteur vide (segfault immédiat),
> et borner la boucle de LECTURE par la taille du vecteur — qui part vide —, ce
> qui faisait disparaître tous les niveaux d'envoi **en silence** au chargement.

> **D4.3 EST FAITE (29/08/2026).** Post-fader était le seul comportement
> possible, écrit en dur dans la boucle de mixage.
>
> **CE QUE CHACUN VEUT DIRE.** En **post-fader**, baisser une piste baisse aussi
> ce qu'elle envoie : la proportion d'effet reste constante, et c'est ce qu'on
> veut d'une réverbération — une piste qu'on retire du mixage ne doit pas
> laisser sa réverbération toute seule. En **pré-fader**, le départ ignore le
> fader : c'est ce qu'il faut pour un retour de casque, ou pour envoyer une
> piste dans un effet **sans l'entendre en direct** — on descend le fader à zéro
> et seul l'effet subsiste. Ce dernier cas justifie à lui seul l'existence du
> réglage, et il a son test.
>
> **C'EST UN RÉGLAGE DU BUS ET NON DE CHAQUE PISTE**, comme sur une console où
> un auxiliaire est câblé pré ou post pour tout le monde — et c'est ce que dit
> le critère, « commutable par départ ». Le rendre indépendant par piste
> multiplierait les commutateurs par le nombre de pistes pour un besoin que rien
> n'a exprimé ; le jour où il le sera, ce champ deviendra le **défaut** du bus.
>
> **LE MUET COUPE TOUT, PRÉ-FADER COMPRIS**, et c'est un choix écrit. Une
> console câble parfois les départs pré-fader avant le muet ; ici, « muet » veut
> dire « je ne veux plus l'entendre », et une piste muette dont la réverbération
> continue de sonner serait déroutante.
>
> Le mode voyage jusqu'au chemin audio par un **masque d'un bit par bus** plutôt
> que par une lecture du projet dans la boucle : le thread audio a le snapshot
> sous la main, mais un entier se lit une fois par bloc là où le vecteur se
> relirait par piste et par sous-segment. Les groupes obéissent au même masque —
> un groupe alimente les départs comme une piste.

> **D4.4 EST FAITE (29/08/2026).** Le compresseur d'une piste écoute une autre
> piste, et les deux étapes précédentes s'y révèlent nécessaires.
>
> **L'ÉCOUTE PASSE PAR UN BUS DE DÉPART, pas par une référence de piste à
> piste.** Router « la grosse caisse vers le bus 3 » et « ce compresseur écoute
> le bus 3 » emploie ce qui existe déjà — un bouton par tranche, un niveau
> sauvegardé, un routage nommé. Une référence directe aurait demandé un SECOND
> système de routage à tenir d'accord avec le premier, et à renuméroter à chaque
> suppression de piste comme les groupes.
>
> **DEUX MÉTHODES FACULTATIVES SUR `IAudioEffect`**, et non un `process`
> élargi : douze effets sur treize n'écoutent rien d'autre que ce qu'ils
> traitent, et leur imposer un paramètre de plus les obligerait tous à le
> documenter, le tester et l'ignorer. Le défaut « je n'écoute rien » les laisse
> rigoureusement inchangés. Côté DSP, `Compressor` gagne une seule fonction :
> le calcul ne change pas, seule la SOURCE de sa décision.
>
> **L'ORDRE DE RENDU, et le garde-fou qui va avec.** Pour que la grosse caisse
> fasse plonger la basse **dans le bloc courant**, elle doit avoir été calculée
> avant. Les pistes qui alimentent un bus écouté passent donc devant. Mais
> réordonner les additions changerait le dernier bit du mixage sans raison :
> tant qu'aucun effet n'écoute, **aucun ordre n'est publié** et le rendu emprunte
> exactement le chemin qu'il avait. Un test vérifie l'égalité **au bit près**.
>
> **D4.3 REND D4.4 UTILISABLE, et c'est un test qui l'a montré.** La première
> version coupait la source au MUET pour ne pas l'entendre — et le muet coupe
> aussi les départs, donc le bus d'écoute restait vide. Une source de chaîne
> latérale se retire du mixage par son **fader**, avec un départ **pré-fader** :
> elle commande sans s'entendre. Les deux étapes composent exactement comme sur
> une console.
>
> **Un second test s'est trompé avant d'être juste.** Il comparait « avec » et
> « sans » chaîne latérale, et les deux compressaient autant : avec un seuil
> bas, un compresseur écrase aussi bien son propre signal que celui qu'il
> écoute. Ce qui varie doit être le seul fil qu'on éprouve — le **niveau
> d'envoi** de la source vers le bus, tout le reste identique.
>
> **Le retour d'un bus s'éteint**, et ce n'est pas un raffinement : un bus qui ne
> sert qu'à faire écouter une piste ne doit pas s'entendre. Sans ce
> commutateur, il aurait fallu choisir entre une réverbération parasite et pas
> de chaîne latérale du tout.

> **D4.5 EST FAITE (29/08/2026).** Le défaut était là depuis que la distorsion
> suréchantillonne, et il ne s'annonçait pas.
>
> **CE QUI SE PASSAIT.** Un suréchantillonneur filtre, et un filtre à phase
> linéaire retarde : la distorsion du parc décalait sa piste de **seize
> échantillons**. Le son restait juste — c'est bien ce qui rend le défaut
> pénible : la piste n'était plus en place, et deux prises censées coïncider
> cessaient de coïncider **selon les effets qu'on leur avait mis**. On attribue
> cela à tout sauf à sa cause.
>
> **ON NE PEUT PAS AVANCER UNE PISTE, ALORS ON RETARDE LES AUTRES.** Le graphe
> calcule la latence de chaque chemin, prend le maximum, et donne à chacune la
> différence. `IAudioEffect` et `ISynthPlugin` déclarent la leur — zéro par
> défaut, donc les trente-quatre machines et douze effets sur treize sont
> rigoureusement inchangés. Le seul chiffre réel du parc, seize échantillons,
> est **calculé** par l'`Oversampler` à partir du nombre de coefficients de ses
> deux filtres, et non écrit à la main quelque part.
>
> **UN CHEMIN, PAS UNE PISTE.** Une piste groupée traverse DEUX chaînes avant le
> master : la sienne et celle de son groupe. Ne compter que la sienne la
> laisserait décalée du retard de son groupe — un décalage qui n'apparaîtrait
> qu'en groupant, c'est-à-dire au moment où l'on soupçonnerait le moins
> l'insert. Un test le couvre.
>
> **AUCUNE LATENCE, AUCUN PLAN**, comme pour l'ordre de rendu de D4.4 : pas une
> ligne à retard de longueur zéro à traverser pour rien, et le rendu reste
> exactement celui qu'il était.
>
> Le test qui compte compare deux rendus : l'effet à latence sur **une** piste
> (le graphe compense) et sur **les deux** (elles sont forcément alignées entre
> elles, donc c'est la référence). Les deux sortent identiques.
>
> **Une leçon en passant, écrite dans le test** : le graphe exige qu'un effet
> soit `prepare()`é avant d'être publié, et un test qui l'oubliait plantait dans
> une ligne à retard vide. Le pion de test dimensionne désormais ses tampons dès
> sa construction — un test doit échouer sur une assertion, pas sur un segment
> de mémoire.

> **D4.6 EST FAITE (30/08/2026).** Une courbe ne pouvait piloter qu'un réglage
> de machine. Le **fondu** — le geste d'automation le plus courant qui soit —
> était donc impossible à écrire, et tout le mixage échappait à l'automation
> **alors que le format savait déjà l'écrire** : les courbes étaient rangées
> dans la piste et sauvegardées, mais rien ne pouvait en viser autre chose
> qu'un instrument.
>
> **DES PRÉFIXES, ET NON UN CHAMP « GENRE ».** Une courbe se nomme
> `mix.volume`, `mix.pan`, `mix.send.2`, `insert.1.effect.reverb.mix` ou
> `master.Limiter Ceiling` ; sans préfixe connu, c'est un réglage de la machine
> de la piste — ce qui fait que **les projets d'avant se relisent inchangés**.
> Un champ de plus obligerait le format, le lecteur, l'écrivain et la chaîne
> d'analyse à s'accorder sur une énumération ; un préfixe se lit, s'écrit et se
> diagnostique à l'œil dans le fichier. C'est la raison qui avait déjà fait
> choisir des identités sémantiques plutôt que des numéros.
>
> **LE VOLUME ET LE PANORAMIQUE VIVENT DANS LE PROJET, que le thread audio ne
> lit qu'en lecture seule** — c'est tout l'intérêt du snapshot. Une courbe ne
> peut donc pas les modifier là où ils sont : elle écrit dans des **surcharges**
> que le mixage consulte à la place, et un **masque d'un entier par piste** dit
> lesquelles sont pilotées. Sans lui, le mixage devrait parcourir toutes les
> courbes pour chaque piste et chaque sous-segment.
>
> **UN FONDU EMPORTE LES DÉPARTS POST-FADER AVEC LUI**, comme le ferait la main
> sur le fader : le niveau employé pour les départs est celui de l'automation
> quand elle le pilote. D4.3 et D4.6 devaient se rencontrer, et c'est ici.
>
> **UNE COURBE QU'ON NE SAIT PAS RÉSOUDRE EST LAISSÉE DANS LE PROJET** et
> simplement pas jouée : elle vise une machine absente, un insert retiré, une
> version différente. La supprimer ferait perdre le travail de l'utilisateur à
> la première ouverture — et c'est exactement l'inverse de ce que ce projet
> cherche à garantir.
>
> Six tests, dont celui qui est le critère : le fader descend de 1 à 0 sur une
> seconde, et on mesure le début et la fin du rendu.

> **D4.7 EST FAITE (30/08/2026), ET AVEC ELLE LA PHASE D4.**
>
> **LA CRÊTE SEULE NE DIT PAS GRAND-CHOSE**, et c'est tout ce que le mixeur
> affichait. Elle dit si ça écrête ; elle ne dit pas si c'est FORT. Deux pistes
> de même crête peuvent être séparées de quinze décibels perçus selon qu'elles
> sont denses ou pleines de silences — et c'est la seconde information qu'on
> cherche quand on équilibre un mixage. Le mètre porte désormais les deux : la
> barre remplit au niveau EFFICACE, le trait marque la crête, et l'écart entre
> les deux se lit d'un coup d'œil.
>
> **LA CORRÉLATION DE PHASE répond à une question qu'aucune des deux autres ne
> posait** : « qu'est-ce qu'il reste de ceci en mono ? ». Négative, la piste
> DISPARAÎT dès qu'on somme — ce qui arrive à qui écoute sur un téléphone — et
> rien dans ce logiciel ne le signalait. Une bande en pied de mètre, rouge dès
> que le chiffre passe sous zéro, et la valeur en clair sur le master : une
> couleur dit qu'il y a un problème, elle ne dit pas s'il est de -0,1 ou de
> -0,9, et c'est ce qui décide si on va chercher.
>
> **LE CRITÈRE DE PHASE EST VÉRIFIÉ, PAS AFFIRMÉ.** Deux moitiés d'un projet qui
> mesurent différemment ne peuvent pas se comparer : la reconstruction
> paraîtrait meilleure ou pire qu'elle n'est, selon celle des deux qu'on croit.
> D'où deux pièces qui se répondent :
>
>  - **`vsm-measure`**, qui mesure un fichier avec LE code du mixeur — pas une
>    redite écrite pour l'occasion, qui ne prouverait que sa propre justesse ;
>  - **`analyse/analyzer/mesures.py`**, qui suit la même norme (ITU-R BS.1770)
>    avec les mêmes coefficients de biquad, et dont la récurrence est écrite à
>    la main plutôt qu'empruntée à `scipy` — c'est la seule façon d'être sûr
>    qu'un écart vienne du signal et non d'un détail d'implémentation.
>
> `analyse/tests/test_mesures.py` les compare sur quatre signaux choisis pour ne
> pas avoir les mêmes réponses (un grave, un aigu — la pondération K les traite
> différemment —, du bruit large bande, et une opposition de phase). **Un seul
> signal ne prouverait rien : deux mesures fausses de la même façon
> coïncideraient.**
>
> Mesures du 30/08/2026 :
>
> | Signal | LUFS moteur | LUFS `analyse/` | Écart |
> |---|---|---|---|
> | sinus 100 Hz | -7,9043 | -7,9046 | **0,0002** |
> | sinus 5 kHz | -7,3561 | -7,3561 | **0,000001** |
> | bruit | -13,9167 | -13,9167 | **0,000000** |
> | opposition de phase | -8,5886 | -8,5886 | **0,000015** |
>
> Le critère tolérait 0,1 LU. La crête, le RMS et la corrélation, qui ne passent
> par aucun filtre, coïncident à 1e-5 près.
>
> **`analyse/` N'AVAIT AUCUNE MESURE DE LOUDNESS** avant ce jour : le module est
> nouveau, et il ne dépend que de numpy — comme le reste de la chaîne, et pour
> que le test tourne hors ligne sans installation. Le test se saute proprement
> quand `vsm-measure` n'est pas compilé : un test qui exige une compilation
> préalable ne doit pas faire échouer la suite Python de quelqu'un qui travaille
> sur l'analyse.


### Phase D5 — La vue d'arrangement

D1 a mis les clips dans le modèle ; ici on les rend manipulables.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D5.1 | Ligne de temps multipiste : clips déplaçables, redimensionnables, coupables | à la souris, avec annulation — **fait** |
| D5.2 | Copier/coller/dupliquer, aimantation à la grille, boucle de clip par étirement | mêmes gestes et mêmes raccourcis que le piano roll — **fait** |
| D5.3 | Pliage des pistes, hauteurs réglables, réordonnancement, couleurs choisies | l'écran tient 16 pistes — **fait** |
| D5.4 | Automation dessinée **sur** l'arrangement, avec zoom et courbes | plus une lane isolée dans un onglet — **fait** |
| D5.5 | Gel et report (*freeze* / *bounce*) d'une piste en audio | une piste gelée sonne identique et coûte le prix d'une lecture audio — **fait** |
| D5.6 | Gain, fondus et inversion de phase **réglés à la souris** sur le clip (venus de D2.4 : le modèle et le moteur les portent déjà) | un fondu se tire sur le coin du clip — **fait** |
| D5.7 | Forme d'onde dessinée dans le clip audio, avec cache d'aperçu (venue de D2.5) | 9 minutes s'affichent sans bloquer l'interface — **fait** |

**Critère de phase** : arranger une reconstruction — déplacer un refrain,
doubler une mesure, boucler quatre temps — se fait entièrement à la souris.

> **D5.1 EST FAITE (30/08/2026).** D1 avait mis les clips dans le MODÈLE : ils
> s'y rangeaient, s'y sauvegardaient et s'y jouaient, mais **rien ne permettait
> de les toucher**. Déplacer un refrain demandait de déplacer chaque note qui le
> compose.
>
> **LE COMPOSANT NE CONTIENT AUCUNE LOGIQUE DE MONTAGE.** Déplacer,
> redimensionner et couper sont dans `vsm::sequencer::ClipEdit`, en fonctions
> pures, éprouvées sans serveur graphique — douze tests. Dans le composant, ils
> auraient été intestables : il aurait fallu un écran pour vérifier qu'un clip
> coupé en deux rejoue exactement le même son.
>
> **CE QUI REND CES GESTES PARTICULIERS**, et qui vient tout droit du modèle de
> la RÉGION choisi en D1 : un clip est une fenêtre sur le matériau, pas une
> boîte qui l'emporte.
>
>  - **Déplacer** ne déplace aucune note : la fenêtre bouge, le matériau reste.
>  - **Tirer le bord gauche** ne pousse pas le clip : cela rogne la tête, et ce
>    qui reste demeure **exactement là où il était** sur la ligne de temps.
>    C'est ce qu'on attend quand on rogne le début d'une prise, et c'est ce qui
>    distingue une région d'une boîte.
>  - **Couper** donne deux moitiés qui rejouent exactement ce que jouait
>    l'original : la seconde reprend la fenêtre là où la première l'a laissée.
>
> **UN CLIP AUDIO A SA FENÊTRE EN SECONDES** (voir `Clip::sourceStartSeconds`),
> et l'oublier serait un défaut discret : rogner le début d'une prise la
> **décalerait** au lieu de la rogner — le clip commencerait plus tard en jouant
> la même chose. Les opérations reçoivent donc la conversion tick → seconde, par
> le même chemin que `spansFromTrack`.
>
> **LES FONDUS NE SE DUPLIQUENT PAS À LA COUPE** : le fondu d'entrée appartient
> au début, celui de sortie à la fin. Les recopier sur les deux moitiés ferait
> apparaître un **trou** au point de coupe, la première s'éteignant pendant que
> la seconde monte.
>
> **UN CLIP GAGNE UN IDENTIFIANT**, comme les notes l'ont depuis toujours et
> pour la même raison : couper un clip en insère un, et une sélection par indice
> désignerait alors le voisin. Il n'est **pas sauvegardé** — rien d'autre ne
> référence un clip, il n'a besoin d'être unique que pendant la session, et
> l'écrire ferait grossir le format d'une donnée que personne ne relit.
>
> **ALT COUPE, plutôt qu'un OUTIL qu'on choisit et qu'on oublie de quitter.** Un
> mode se laisse allumé, et le geste suivant fait autre chose que ce qu'on
> croit ; un modificateur ne dure que le temps où on le tient. C'est la règle
> déjà retenue pour la boucle (Maj) et le punch (Alt) sur la règle du piano roll.
>
> **L'aimantation est à la MESURE et non à la noire** : on arrange par mesures,
> et un clip qui tomberait sur un temps quelconque ne serait presque jamais ce
> qu'on voulait.
>
> **La vue se regarde sans écran**, par `vsm-arrangement-preview`, pour la même
> raison que le piano roll et les façades : ce qu'on ne peut pas regarder, on ne
> peut pas le juger. L'aperçu porte exprès les cas qui se jugent à l'œil — deux
> clips bord à bord (le résultat d'une coupe) qui doivent se lire comme deux, un
> clip muet qui doit se remarquer **sans disparaître**, et une piste de groupe
> qui n'a pas de clip parce qu'elle n'a pas de matériau.

> **D5.2 EST FAITE (30/08/2026).**
>
> **LA BOUCLE NAÎT DU MÊME GESTE QUE LE REDIMENSIONNEMENT**, et c'est ce que dit
> « boucle de clip par ÉTIREMENT ». Tant qu'il reste du matériau, tirer le bord
> droit en révèle davantage ; une fois au bout, la fenêtre ne peut plus grandir
> et c'est la durée jouée qui continue — le clip répète alors sa fenêtre, **sans
> qu'une seule note soit copiée**. Un modificateur ou un second outil pour
> « boucler » demanderait de savoir à l'avance où finit le matériau, ce que
> personne ne sait en tirant.
>
> **UN DÉFAUT TROUVÉ EN CHEMIN, ET IL AURAIT ÉTÉ DIFFICILE À VOIR.** Le planning
> MIDI répétait déjà la fenêtre d'un clip trop long (`passagesOf`) ; le côté
> AUDIO, lui, lisait tout droit et continuait dans le fichier. Le même geste
> aurait donc **bouclé une batterie MIDI et révélé la suite d'une prise
> audio** — deux réponses pour un seul geste, et personne n'aurait soupçonné
> l'étirement. `spansFromTrack` répète désormais sa fenêtre comme le fait le
> planning, avec la même règle sur les fondus : ils appartiennent au CLIP et non
> à chaque tour, sinon un trou reviendrait à chaque boucle.
>
> **LES MÊMES RACCOURCIS QUE LE PIANO ROLL, à la lettre** : Ctrl+C, Ctrl+V,
> Ctrl+D, Ctrl+X. Deux vues du même morceau qui demanderaient deux gestes
> différents pour la même chose seraient deux logiciels. Dupliquer décale de la
> longueur de la sélection **arrondie à la grille** — dupliquer une mesure tombe
> pile sur la suivante —, exactement la règle du piano roll.
>
> **Le presse-papiers porte ses CLIPS, pas des identifiants** : coller doit
> marcher après avoir supprimé l'original, et un identifiant ne désignerait
> alors plus rien. On colle à la **tête de lecture** et sur la **piste
> courante**, ce qui permet aussi de recopier un motif d'une piste à l'autre.
>
> **LA GRILLE FINE EST CELLE DU PIANO ROLL, lue à l'usage plutôt que recopiée.**
> Deux réglages de grille dans deux vues du même morceau finiraient par se
> contredire, et l'utilisateur ne saurait plus lequel il vient de changer.
> L'aimantation est à la **mesure** par défaut — on arrange par mesures — et `G`
> bascule vers la grille fine, `S` la coupe.
>
> **UN RÉGLAGE QU'ON BASCULE AU CLAVIER DOIT SE VOIR** : l'état d'aimantation
> est écrit en petit dans le coin de la règle. Sans cela il se retournerait
> contre celui qui l'a basculé sans s'en souvenir.
>
> **UN CLIP QUI BOUCLE SE VOIT BOUCLER** : un trait fin marque chaque tour.
> Dessiné comme un simple rectangle plus long, il mentirait sur ce qu'il joue —
> et c'est vérifiable à l'œil dans `vsm-arrangement-preview`, dont l'aperçu
> porte maintenant un clip étiré quatre fois.

> **D5.3 EST FAITE (30/08/2026), ET LE CRITÈRE EST VÉRIFIÉ À L'ŒIL.** Seize
> pistes tiennent dans **486 pixels** — quatre dépliées et douze pliées, ce qui
> est la façon dont on travaille : on ouvre celles qu'on retouche et on referme
> le reste. L'aperçu le montre plutôt que de le promettre.
>
> **PLIER N'ÉCRASE PAS LA HAUTEUR RÉGLÉE, il la met de côté.** Sans quoi
> déplier rendrait une hauteur standard, et tout le travail de mise en page
> serait perdu au premier pli. Une piste pliée n'affiche que son nom — c'est ce
> qu'on est venu chercher — mais ses clips restent visibles en fine bande :
> replier une piste ne doit pas la faire disparaître du morceau.
>
> **LA HAUTEUR ET LE PLI SONT SAUVEGARDÉS AVEC LE MORCEAU.** Ce ne sont pas des
> propriétés du son mais de la façon dont on REGARDE ce morceau-là, et les
> perdre obligerait à refaire la mise en page à chaque ouverture. Champs
> facultatifs : une piste à la hauteur standard et dépliée garde exactement le
> fichier qu'elle avait.
>
> **RÉORDONNER : CE NE SONT PAS LES INDEX QUI SUIVENT, CE SONT LES PISTES.** Le
> même piège que la suppression, et il est pire ici — réordonner déplace
> potentiellement TOUTES les pistes, et un routage vers un groupe qui a changé
> de rang enverrait le mixage ailleurs sans qu'aucun réglage n'ait bougé.
> `moveTrack` note donc, pour chaque piste, **vers quelle piste** elle envoie,
> puis retrouve les nouveaux index après coup. Corriger les index au fil du
> déplacement demanderait de raisonner sur trois cas de figure, et le troisième
> serait faux. Quatre tests, dont un qui remonte un groupe tout en haut pour que
> **tous** les index changent.
>
> **TROIS GESTES DANS LA MÊME BANDE, séparés par l'endroit où l'on saisit** : le
> triangle plie, le bandeau de couleur ouvre le sélecteur, le bas de l'en-tête
> règle la hauteur, et le reste réordonne. C'est la règle déjà employée pour les
> clips, où le bord se distingue du milieu.
>
> **Traverser six pistes est UN geste, pas six** : l'instantané d'annulation
> n'est pris qu'au premier pas. De même, un glissé dans le sélecteur de couleur
> produit des dizaines de changements et n'ouvre qu'**un** pas d'annulation —
> mais rouvrir le sélecteur en commence un nouveau, sans quoi toutes les
> couleurs de la session n'en feraient qu'un seul et annuler les défairait
> toutes.
>
> **La couleur suit le sélecteur en direct** : on choisit une couleur en la
> voyant sur la piste, pas en la devinant dans un carré. Le sélecteur est ouvert
> par l'application et non par la vue : le composant d'arrangement ne connaît de
> JUCE que le dessin, et lui faire ouvrir une fenêtre le lierait à
> l'application.

> **D5.4 EST FAITE (30/08/2026).** Les courbes se dessinent **par-dessus les
> clips** et non à côté : une courbe se lit par rapport à ce qu'elle pilote, et
> une bande séparée — a fortiori un onglet — obligerait à faire l'aller-retour
> des yeux entre le fondu et le clip qu'il éteint. `A` les montre et les cache :
> « plus une lane isolée dans un onglet » ne veut pas dire des courbes en
> permanence par-dessus les clips quand on arrange.
>
> **UNE SEULE RÈGLE D'INTERPOLATION, ET ELLE EST VÉRIFIÉE.** La courbe est
> DESSINÉE par `core::automationValueAt` et JOUÉE par
> `audio::AutomationLane::valueAt` — deux structures différentes, l'une pour
> l'édition, l'autre pour le chemin temps réel. Deux interpolations qui
> divergeraient feraient **dessiner une courbe et en entendre une autre**, le
> genre d'écart qu'on met des heures à ne pas croire. Un test les compare sur
> les mêmes points, tous les sept ticks, **au-delà des deux bouts** — c'est là
> que le maintien hors plage doit coïncider, et c'est justement ce qu'on
> oublie. Même discipline qu'en D4.7 entre le moteur et `analyse/`.
>
> **HORS DE SA PLAGE, UNE COURBE MAINTIENT SA VALEUR** au lieu de tomber à
> zéro : une courbe qui ne couvre que le refrain ne doit pas éteindre le
> paramètre pendant les couplets.
>
> **POSER UN POINT DEMANDE DE SAVOIR SUR QUELLE ÉCHELLE.** Les bornes viennent
> des listes de paramètres des machines et des effets, que la vue n'a pas à
> connaître : elle demande, l'application répond. Quand le paramètre est inconnu
> — machine absente, insert retiré —, la courbe reste **visible mais non
> modifiable**, plutôt que modifiable sur une échelle inventée.
>
> **UN PALIER SE VOIT** : carré plein contre carré évidé. Sans cela, deux points
> identiques à l'œil se comporteraient différemment et rien ne dirait pourquoi.
>
> **Une courbe dessinée s'entend TOUT DE SUITE** : sans republication, elle
> serait sauvegardée et muette jusqu'à la prochaine ouverture du projet.
>
> **Un défaut d'affichage trouvé dans l'aperçu, et corrigé** : le nom du
> paramètre était écrit sur la piste et se superposait au nom du clip — les deux
> devenaient illisibles. Il est maintenant dans l'en-tête, où il appartient de
> toute façon, puisque c'est la piste qui décide quelle courbe elle montre.

> **D5.5 EST FAITE (30/08/2026). « SONNE IDENTIQUE » EST UNE ÉGALITÉ, PAS UNE
> FORMULE**, et elle est vérifiée **au bit près**.
>
> **CE QU'IL FALLAIT CAPTURER, ET LE PROBLÈME QUE ÇA POSE.** Un gel doit
> contenir le signal d'AVANT le fader : le volume, le panoramique, les départs,
> le muet et le solo restent vivants après le gel — sinon geler serait
> reporter, et une piste gelée à mi-volume ne pourrait plus être remontée. Or le
> mixage applique sa loi de panoramique en même temps que le volume : capturer
> le master les figerait dans le fichier, et la piste subirait la loi **deux
> fois**.
>
> **D'OÙ DEUX RENDUS, ET CE N'EST PAS UNE RUSE.** Aux extrémités, la loi de
> panoramique vaut *exactement* 1 et 0 — mesuré : `pan = -1` donne (1 ; 0) et
> `pan = +1` donne (-4,4 e-08 ; 1). Rendre la piste seule tournée à fond à
> gauche livre donc le canal gauche **inaltéré**, à fond à droite le canal
> droit. Aucune division, aucun arrondi, et le test compare avec `==` plutôt
> qu'avec une tolérance.
>
> **CE QUI EST GELÉ CESSE DE TOURNER** : le moteur saute l'instrument ET les
> inserts d'une piste gelée. Les repasser dessus les appliquerait deux fois,
> puisqu'ils sont déjà dans le fichier — et c'est là qu'est le gain promis par
> « coûte le prix d'une lecture audio ».
>
> **LE MATÉRIAU N'EST JAMAIS DÉTRUIT.** Notes, instrument, inserts et clips
> restent dans la piste et reviennent au dégel. Un gel qui effacerait ce qu'il
> remplace serait un report qui n'ose pas dire son nom. Regeler repart d'ailleurs
> **du matériau** et non du gel précédent, sans quoi on empilerait des rendus de
> rendus et le son dériverait à chaque gel sans que rien ne le dise.
>
> **TROIS PIÈGES ÉCARTÉS, chacun avec son test** : le fader, le muet et les
> départs n'entrent pas dans le fichier ; l'automation du **mixage** non plus
> (elle resterait vivante et s'appliquerait deux fois) alors que celle des
> machines et des inserts, elle, est bien ce qu'on gèle ; et les **clips** ne
> sont pas appliqués au gel, qui rend déjà la piste découpée.
>
> **UNE PISTE GELÉE LE DIT** — « midi · gelé » dans son en-tête, et ses clips
> estompés. Sans cela, on éditerait ses notes en se demandant pourquoi rien ne
> change : son instrument ne tourne plus, c'est son gel qu'on entend.
>
> **Geler exige un dossier de projet**, comme l'enregistrement audio et pour la
> même raison : le format range ses fichiers par chemin relatif. Le dégel
> **efface** le fichier — le garder laisserait dans le dossier un rendu que plus
> rien ne référence, et qu'on retrouverait des mois plus tard sans savoir ce
> qu'il est.
>
> Au passage, `renderBundleToWav` s'est scindé : rendre en mémoire et poser sur
> le disque sont deux gestes, et le gel avait besoin du premier sans le second.

> **LE REPORT (*bounce*) COMPLÈTE L'ÉTAPE.** C'est le **même rendu** que le gel
> — deux rendus différents finiraient par ne plus sonner pareil — suivi d'une
> décision : le matériau est remplacé, la piste devient audio, et notes,
> instrument, inserts et découpe s'en vont parce qu'ils sont désormais **dans le
> fichier**. Les garder les appliquerait une seconde fois, par-dessus leur
> propre rendu.
>
> **REPORTER SE DEMANDE, GELER NON.** Le report est annulable tant que la
> session est ouverte et définitif ensuite : c'est exactement ce que veut dire
> « définitif », et le dire vaut mieux que de le laisser découvrir. La demande
> renvoie d'ailleurs vers le gel pour qui cherchait seulement à alléger.
> **L'automation du mixage survit** au report, celle des machines part avec
> elles : la première pilote encore quelque chose, la seconde ne vise plus rien.
>
> **D5.6 EST FAITE (30/08/2026).** Le modèle et le moteur portaient déjà les
> fondus, le gain et l'inversion de phase depuis D2.4 ; rien ne permettait de
> les toucher.
>
> **LE HAUT ET LE BAS D'UN MÊME BORD FONT DEUX CHOSES** : le coin du haut tire
> un fondu, le bord redimensionne. C'est la convention de tous les séquenceurs,
> et elle tient parce qu'un fondu se dessine justement depuis le haut du clip.
> Chacun a son curseur — sans cela, rien ne distinguerait les huit pixels qui
> tirent un fondu de ceux qui redimensionnent, et on découvrirait la différence
> en la subissant.
>
> **UN FONDU SE MESURE EN SECONDES**, comme la fenêtre d'un clip audio et pour
> la même raison : il suit le SON, pas le tempo. Accélérer un morceau ne doit
> pas raccourcir ses fondus. Il ne dépasse jamais le clip — au-delà il mangerait
> ce qui vient après et ne s'entendrait plus comme un fondu.
>
> **UN GAIN DE CLIP N'EST JAMAIS NÉGATIF.** L'inversion de phase est un réglage
> à part, et la confondre avec un gain négatif rendrait le bouton illisible : on
> ne saurait plus si un clip est faible ou inversé. L'inversion **bascule** par
> clip plutôt que d'aligner la sélection — inverser une sélection dont la moitié
> l'est déjà doit rendre l'autre moitié.
>
> **LES TROIS SE VOIENT** : les fondus en triangles sombres aux coins, la phase
> inversée en liséré de tirets. Un réglage qui ne se dessinerait pas obligerait
> à écouter pour savoir s'il existe, et deux clips identiques dont l'un est
> inversé s'annulent en s'additionnant sans que rien d'autre ne le dise.


> **D5.7 EST FAITE (30/08/2026). « SANS BLOQUER L'INTERFACE » SE JOUE AU
> DESSIN, PAS AU CHARGEMENT.** La tentation était de lancer un thread de fond
> pour calculer l'aperçu. Mesuré, il n'y avait rien à y gagner : neuf minutes de
> stéréo coûtent **21 ms** de calcul de cache, une seule fois, par-dessus un
> décodage qui en coûte cent fois plus et que ce projet fait déjà sur le thread
> de l'interface. Un thread de plus n'aurait déplacé que ces 21 ms, contre une
> synchronisation à tenir juste. Le cache se construit donc là où le fichier est
> déjà décodé, dans `loadAudioTracks`.
>
> Ce qui aurait vraiment bloqué, c'est le **redessin** : il revient à chaque
> déplacement de souris, et parcourir neuf minutes d'échantillons à chaque fois
> aurait figé la vue. `peaksForRange` lit le cache, jamais le fichier : **0,08
> ms par rafraîchissement**, indépendamment de la durée. C'est cela, le critère.
>
> **UN APERÇU GARDE LE MINIMUM *ET* LE MAXIMUM**, pas une amplitude. Un son
> asymétrique — une caisse claire, une voix — n'est pas un ruban centré, et le
> réduire à une valeur ferait mentir le dessin sur ce qu'on entend. Le dernier
> paquet est gardé même incomplet : sinon la fin d'un fichier disparaîtrait.
>
> **CHAQUE COLONNE DE PIXEL A AU MOINS UN PAQUET.** Zoomé au maximum, plusieurs
> colonnes tombent dans le même paquet ; sans cette garantie, la forme d'onde se
> trouerait exactement là où l'on regarde de plus près.
>
> **LE DESSIN PART DE `sourceStartSeconds`.** Un clip rogné joue le milieu du
> fichier : dessiner son début montrerait une forme qui ne correspond pas au
> son, ce qui est pire que pas de forme du tout.


### Phase D6 — Exporter

D0.3 a rendu l'export **honnête** ; ici on le rend complet.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D6.1 | Plage au choix (morceau, boucle, sélection), fréquence, profondeur, queue | plus de 48 kHz / 24 bits en dur — **fait** |
| D6.2 | Export **stems** : une piste ou un bus par fichier | la somme des stems égale le mixage, vérifié par test — **fait** |
| D6.3 | Export MIDI complet (aujourd'hui : perd `muted` et `confidence`) | relu ailleurs sans perte de tempo ni de signature — **fait** |
| D6.4 | Export d'un **projet autonome** (dossier complet, échantillons compris) | s'ouvre sur une autre machine sans rien de manquant — **fait** |
| D6.5 | Rendu en temps réel, requis dès qu'un plugin tiers l'exige | option explicite, jamais le défaut — **fait** |

**Critère de phase** : il n'y a toujours qu'**un seul rendu** dans ce projet.
L'application n'en invente pas un second ; elle expose celui de `vsm-render`.

> **D6.1 EST FAITE (30/08/2026). UNE PLAGE SE CALCULE DEPUIS ZÉRO ET SE DÉCOUPE
> ENSUITE.** C'était le seul vrai choix de l'étape. Rendre à froid en se
> plaçant directement au début de la plage aurait été rapide et faux : la
> queue de réverbération, l'écho et le compresseur qu'installe ce qui précède
> n'existeraient pas, et l'extrait exporté ne serait pas celui qu'on entend à
> cet endroit — sans que rien dans le fichier ne le signale. Le rendu part donc
> toujours de zéro, et `startSeconds` découpe. Le surcoût est proportionnel au
> début de la plage, hors ligne, et payé une fois.
>
> **C'EST AUSSI CE QUE LE TEST VÉRIFIE**, et c'est pour cela qu'il est
> formulé ainsi : la plage exportée doit être **bit à bit** la portion
> correspondante du rendu complet. « Un fichier plus court sort » serait vrai
> d'un rendu à froid, donc ne prouverait rien.
>
> **LE DÉFAUT DE FRÉQUENCE EST CELLE DE LA SESSION**, pas 48 kHz. Un projet
> travaillé à 96 kHz s'exportait jusqu'ici rééchantillonné en silence.
> Rééchantillonner en exportant reste possible : c'est désormais un choix,
> plus un accident.
>
> **CE QUI N'EXISTE PAS NE SE PROPOSE PAS** : sans boucle posée et sans clip
> sélectionné, les deux plages correspondantes sont grisées. Les laisser
> actives donnerait un fichier vide sans rien expliquer.
>
> **LA QUEUE S'AJOUTE APRÈS LA PLAGE**, boucle et sélection comprises : une
> boucle exportée sans queue coupe net sa dernière résonance sur le dernier
> temps, ce qui s'entend immédiatement en la rejouant ailleurs.
>
> **UN SEUL RENDU, TOUJOURS** : `vsm-render` reçoit la même option, `--start`.
> L'application ne sait rien exporter que la ligne de commande ne sache faire.


> **D6.2 EST FAITE (30/08/2026). UN STEM EST UNE CONTRIBUTION AU MIXAGE, PAS
> UNE PISTE ISOLÉE.** Le stem de la voix porte son volume, son panoramique, ses
> inserts **et la réverbération qu'elle envoie** : c'est ce qui le rend
> utilisable seul chez quelqu'un d'autre. C'est exactement l'inverse du gel
> (D5.5), qui capture la piste AVANT son fader pour que le mixage reste
> vivant — les deux fonctions rendent la même piste et ne doivent surtout pas
> rendre la même chose.
>
> **LA TRANCHE MASTER N'EST PAS DANS LES STEMS.** Un compresseur de master
> réagit au mixage entier : il n'existe aucune façon de le répartir entre les
> pistes, et l'appliquer à chaque fichier le ferait agir autant de fois qu'il y
> a de stems. Ce qui est exporté est donc ce qui ARRIVE au master, et la somme
> vaut le mixage avant master. C'est la seule égalité qui puisse être vraie, et
> l'export le dit dans ses avertissements plutôt que de laisser croire à
> l'autre.
>
> **CE QUI ROMPRAIT L'ÉGALITÉ EST ANNONCÉ, PAS INTERDIT** : un insert non
> linéaire sur un bus de GROUPE réagit au groupe entier, donc ne se répartit pas
> non plus. Le rendu avertit ; il ne refuse pas, parce que des stems légèrement
> disjoints restent utiles et que c'est à l'utilisateur d'en décider.
>
> **UN BUS DE GROUPE N'EST JAMAIS UN STEM DE PLUS** : il est TRAVERSÉ par les
> pistes qu'il porte. Le compter en supplément doublerait le son. Le mode « un
> groupe par fichier » remplace ses pistes, il ne s'y ajoute pas — d'où la même
> somme, avec moins de fichiers.
>
> **UNE PANNE TROUVÉE EN ÉCRIVANT LE TEST, ET QUI N'AVAIT RIEN À VOIR AVEC LES
> STEMS.** Le stem d'une piste envoyant 40 % dans la réverbération sortait
> identique à celui d'une piste n'envoyant rien. Cause : le rendu hors ligne
> posait bien les NIVEAUX de départ de chaque piste et le masque pré-fader,
> mais **aucun effet sur les bus de départ**. Tout ce qui partait vers un
> départ tombait dans un bus vide. Autrement dit, depuis D4.2, **tout projet
> exporté depuis l'application perdait sa réverbération et son delay de
> départ**, en silence — la panne même que D0.3 avait corrigée pour les
> inserts, restée intacte à côté. Corrigée dans `renderBundleToBuffer`, avec un
> test qui compare deux rendus ne différant que par un niveau de départ.


> **D6.3 EST FAITE (30/08/2026). LE FICHIER JOUE TOUJOURS CE QU'ON ENTEND ; CE
> QUE LE SMF NE SAIT PAS DIRE EST ÉCRIT LÀ OÙ IL L'IGNORE.** Deux propriétés
> d'une note n'existent pas dans le format : `muted` (présente mais silencieuse)
> et `confidence` (le degré de certitude d'une transcription). Elles
> disparaissaient à l'export sans un mot — exporter puis réimporter son propre
> morceau démuselait les notes qu'on avait tues et effaçait le travail de
> vérification d'une transcription. Un aller-retour qui perd du travail est un
> piège.
>
> **CE QUI A ÉTÉ ÉCARTÉ** : écrire les notes muettes dans le flux de notes en
> les marquant à côté. Le fichier jouerait alors autre chose que ce qu'on
> entend, et tout autre logiciel les ferait sonner. La règle ne bouge pas, et
> un test la garde.
>
> **CE QUI A ÉTÉ RETENU** : un événement méta **0x7F** (*Sequencer Specific*),
> que la norme réserve exactement à cet usage et que tout autre logiciel ignore
> — il n'y a rien à y comprendre pour qui ne le connaît pas. Signature `0x7D
> "VS"` (0x7D est l'identifiant « non commercial » réservé aux usages privés),
> un numéro de version, puis les notes muettes **en entier** (elles ne sont
> nulle part ailleurs) et les confiances des notes qui ne valent pas 1. Un
> projet qui n'a rien à dire n'écrit **aucun** bloc.
>
> **UN BLOC 0x7F D'UN AUTRE LOGICIEL TRAVERSE INTACT.** Ne consommer que le
> nôtre est la même règle que partout ailleurs dans ce projet : ce qu'on ne
> comprend pas se réécrit tel quel, jamais ne s'efface.
>
> **LE TEMPO ET LA SIGNATURE**, la moitié du critère qui porte sur ce que le SMF
> sait dire, sont vérifiés sur des cartes à **plusieurs changements** — et pas
> seulement en comparant les valeurs : le test vérifie qu'un même tick retombe
> à la même seconde. Un tempo relu de travers ne se voit pas, il s'entend.


> **D6.4 EST FAITE (30/08/2026). LE FORMAT ÉTAIT PORTABLE ; L'ENREGISTREMENT NE
> L'ÉTAIT PAS.** Tous les chemins d'un projet sont relatifs à son dossier
> depuis toujours, et la lecture refuse même un chemin absolu — l'étape avait
> donc l'air à moitié faite. Elle ne l'était pas du tout :
> `saveProjectBundle` n'écrit que `project.json`, le MIDI et les presets, et ne
> **copie aucun média**. « Enregistrer sous » un autre dossier produisait un
> `project.json` désignant des fichiers restés dans l'ancien : illisible sur une
> autre machine, et silencieusement incomplet sur celle-ci.
>
> **LES PRISES ÉCARTÉES PARTENT AVEC.** Une prise qu'on n'a pas retenue reste
> du travail ; ne pas l'emporter reviendrait à décider à la place de
> l'utilisateur qu'il n'y reviendra pas.
>
> **CE QUI MANQUE EST NOMMÉ, ET LE DOSSIER EST ÉCRIT QUAND MÊME.** Refuser
> d'écrire parce qu'un fichier sur seize est introuvable ferait perdre les
> quinze autres. L'application le dit au moment où l'on enregistre, plutôt que
> de le laisser découvrir en rouvrant le projet ailleurs.
>
> **EXPORTER SUR PLACE NE DÉTRUIT RIEN.** `copy_file` d'un fichier sur
> lui-même le vide ; or enregistrer par-dessus son propre dossier est le geste
> le plus fréquent qui soit (Ctrl+S). La copie reconnaît ce cas et ne fait
> rien, et un test le garde — c'est celui où une erreur coûterait tout.
>
> **LE CRITÈRE EST VÉRIFIÉ COMME IL EST ÉCRIT** : le test ne se contente pas de
> constater les copies, il **recharge le dossier écrit** et vérifie qu'il ne
> manque plus rien. « S'ouvre ailleurs » ne se déduit pas d'une liste de
> fichiers.


> **D6.5 EST FAITE (30/08/2026). DEUX FAÇONS DE PASSER AU TEMPS RÉEL, ET UNE
> SEULE EST UN CHOIX.** L'utilisateur peut le demander — c'est l'option
> explicite, jamais cochée d'avance. Et un plugin peut l'EXIGER, en répondant
> vrai à `requiresRealtimeRender()` : le rendu l'honore alors **et le nomme**
> dans ses avertissements. Ce second cas n'est pas « le défaut » qui
> s'installerait en douce, c'est une exigence déclarée qu'ignorer ferait rendre
> autre chose que ce qu'on a entendu — la première moitié du critère demande
> exactement cela.
>
> **POURQUOI CE N'EST JAMAIS LE DÉFAUT** : les trente-quatre machines du parc
> sont déterministes. Un bloc calculé plus vite que le temps réel donne
> exactement les mêmes échantillons, et neuf minutes rendues en dix secondes
> sont une propriété du projet qu'on ne sacrifie pas par prudence. Un test
> vérifie qu'aucune machine du parc ne l'exige — si l'une s'y mettait, tous les
> rendus deviendraient cent fois plus lents sans que personne l'ait demandé.
>
> **CE QUE LES TESTS GARDENT** n'est pas « l'option existe » mais les deux
> propriétés qui la rendent défendable : elle est fausse par défaut, et quand
> elle est vraie elle change la **durée du calcul sans changer un seul
> échantillon**. Une option de vitesse qui modifierait le résultat ne serait pas
> une option de vitesse.
>
> **L'ATTENTE SE CALCULE DEPUIS LE DÉBUT**, jamais bloc par bloc : additionner
> des attentes courtes accumule l'erreur de chaque réveil, et un rendu de neuf
> minutes finirait sensiblement en retard sur ce qu'il prétend imiter.
>
> **LE CROCHET EST POSÉ POUR D7.** `requiresRealtimeRender()` existe sur
> `ISynthPlugin` et sur `IAudioEffect`, pour la même raison que
> `latencySamples()` : un plugin doit pouvoir DIRE ce qu'il lui faut au lieu de
> rendre faux en silence.


### Phase D7 — Héberger les plugins des autres

L'hôte CLAP existe (`clap/host/`). Le marché, lui, est en VST3.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D7.1 | Lier l'hôte CLAP à l'application — il est écrit, testé, et non branché | un `.clap` tiers se charge sur une piste — **fait** |
| D7.2 | Hôte VST3 pour les instruments, présenté comme `ISynthPlugin` | un instrument tiers joue et se sauvegarde dans le projet — **fait** |
| D7.3 | **Entrées audio dans l'hôte** pour héberger des effets (CLAP comme VST3) | insérables au même titre que les natifs — **fait** |
| D7.4 | Interface native du plugin dans une fenêtre ; transport transmis au plugin | affichée, redimensionnable, fermable sans perte d'état ; un delay synchronisé au tempo suit le tempo — **fait (VST3 et CLAP)** |
| D7.5 | Balayage des plugins installés en tâche de fond | plugin fautif isolé et signalé, jamais fatal — **fait** |

**Critère de phase** : un projet contenant un plugin tiers se recharge à
l'identique, et son absence est **signalée sans être substituée** — exactement
la règle déjà tenue pour les instruments VSM manquants (P4/P7 de
`ROADMAP-interop.md`).

> **D7.5 EST FAITE (30/08/2026). BALAYER UN PLUGIN, C'EST L'EXÉCUTER.** On ne
> peut pas savoir ce qu'un fichier `.vst3` contient sans ouvrir sa bibliothèque
> et l'interroger — donc sans faire tourner du code qu'on n'a pas écrit. Un seul
> plugin mal écrit, et il y en a, fait tomber le processus. Un balayage naïf
> transforme « l'utilisateur a installé un plugin douteux » en « le DAW ne
> démarre plus », sans le moindre message.
>
> **« JAMAIS FATAL » EXIGE DEUX PROCESSUS, ET RIEN D'AUTRE NE SUFFIT.** Un
> `try`/`catch` n'attrape pas une faute de segmentation ; une liste noire
> construite après coup ne protège que du DEUXIÈME lancement, le premier ayant
> déjà emporté l'application. Le balayage se fait donc dans un processus
> ENFANT, un fichier à la fois : s'il tombe, il tombe **seul**, le parent
> constate un code de sortie anormal, note le fichier comme fautif, et passe au
> suivant.
>
> **VÉRIFIÉ, PAS SUPPOSÉ.** Un `.clap` construit hors du dépôt pour l'occasion,
> qui déréférence un pointeur nul dès qu'on l'ouvre, a été passé au processus
> enfant : celui-ci sort en **139** (SIGSEGV), et le parent continue. C'est la
> seule façon de savoir que ce mécanisme fait ce qu'il promet.
>
> **L'APPLICATION SE RELANCE ELLE-MÊME** (`--scan-plugin <fichier>`) plutôt que
> de livrer un exécutable de balayage à part : l'enfant doit charger
> **exactement** le même code d'hôte que le parent, sinon le balayage validerait
> un chemin et la lecture en emprunterait un autre. Un second binaire aurait
> aussi à être trouvé, installé et tenu à jour. Le détournement se fait dans
> `main`, **avant** que JUCE ne démarre : ouvrir une fenêtre pour balayer un
> fichier serait absurde, et échouerait d'emblée sur une machine sans affichage.
>
> **UN DÉLAI PAR FICHIER, GÉNÉREUX MAIS BORNÉ** (20 s). Certains plugins lisent
> des gigaoctets d'échantillons à l'ouverture, et les couper les déclarerait
> fautifs à tort. Mais un plugin qui attend une clé de licence sur un serveur
> injoignable ne rendra **jamais** la main, et sans borne le balayage resterait
> bloqué sur lui pour toujours.
>
> **LES FAUTIFS SONT GARDÉS DANS LE CATALOGUE**, avec leur raison. Sans cela,
> chaque balayage retenterait le même plugin — donc referait payer la même
> chute — et l'utilisateur n'apprendrait jamais lequel des deux cents fichiers
> de son disque pose problème. Un second balayage ne rouvre que ce qu'il n'a
> jamais vu ; tout rouvrir reste possible, et c'est ce qu'on veut après avoir
> mis à jour un plugin.
>
> **LE CATALOGUE EST ÉCRIT AU FUR ET À MESURE**, pas à la fin : si
> l'application est fermée pendant un balayage de deux cents fichiers, le
> travail déjà fait est gardé.
>
> **CE QUI EST TESTÉ, ET POURQUOI CELA SUFFIT.** Lancer un processus et le voir
> tomber ne se teste pas dans une suite unitaire — cela se fait, et cela a été
> fait. Ce qui est testé est la partie où une erreur serait **silencieuse** : le
> protocole entre l'enfant et le parent (une ligne mal décodée met un plugin au
> mauvais endroit du menu ; un message de licence imprimé par un plugin pendant
> son chargement ne doit pas entrer au catalogue ; un nom contenant une
> tabulation ne doit pas couper la ligne en deux) et la relecture du catalogue.
>
> **ET LE BALAYAGE DÉBOUCHE SUR LA MÊME PORTE QUE LE RESTE** :
> `CataloguedPlugin::instrumentId()` rend exactement l'identifiant que
> `PluginRegistry` et `EffectFactory` savent lire (D7.1 à D7.3). Un catalogue
> qui aurait sa propre façon de désigner les plugins n'aurait servi à rien.


> **D7.1 EST FAITE (30/08/2026). « BRANCHER » VEUT DIRE UNE CHOSE PRÉCISE : QUE
> LE REGISTRE DE MACHINES SACHE RÉPONDRE.** Tout le reste du projet — le graphe,
> le format de projet, le rendu hors ligne, le Synth Rack — ne parle qu'à
> `PluginRegistry::create(id)`. Faire répondre le registre suffit donc, et
> aucune de ces couches n'a une ligne à changer pour accepter des instruments
> qu'on n'a pas écrits. C'est la garantie « ajouter une machine ne touche ni le
> moteur ni l'interface » tenue jusqu'au bout.
>
> **UN CROCHET, PAS UNE DÉPENDANCE.** `audio/` ne doit rien savoir de CLAP : la
> dépendance va dans l'autre sens, et le SDK CLAP est facultatif. Le registre
> accepte donc **un résolveur**, appelé quand l'identifiant demandé ne fait pas
> partie du parc ; c'est `installClapResolver()` qui se pose dedans, jamais le
> registre qui va chercher CLAP. Sans la couche CLAP compilée, l'application et
> `vsm-render` se construisent et fonctionnent à l'identique — l'entrée de menu
> n'apparaît simplement pas, plutôt que d'apparaître grisée pour une raison
> qu'on ne saurait pas expliquer.
>
> **UN PLUGIN TIERS EST UN IDENTIFIANT COMME UN AUTRE** :
> `clap:<chemin>#<identifiant>`, écrit tel quel dans `project.json` à côté de
> `vsm.tb303`. Le séparateur se cherche **par la fin** : un chemin de fichier
> peut contenir un « # », un identifiant CLAP est un nom pointé qui n'en
> contient pas — chercher par le début couperait le chemin au mauvais endroit
> sur la machine de quelqu'un d'autre.
>
> **LE CHEMIN EST ABSOLU, ET C'EST ASSUMÉ.** Un plugin n'est pas un média du
> morceau : c'est un logiciel installé sur la machine. Le copier dans le dossier
> de projet (D6.4) serait le redistribuer, ce qu'aucune licence ne permet en
> général. Un projet emporté ailleurs signale donc le plugin manquant **sans le
> remplacer** — le critère de la phase, tenu dès la première étape et gardé par
> un test.
>
> **`isRegistered` RÉPOND FAUX POUR UN PLUGIN TIERS**, délibérément : savoir
> s'il est là demande d'ouvrir un fichier, et cette question est posée partout,
> y compris dans des boucles d'interface. Ce qui décide reste `create()`, dont
> l'échec est déjà signalé et jamais substitué.
>
> **`vsm-render` REÇOIT LE MÊME BRANCHEMENT**, en une ligne. L'application et la
> ligne de commande doivent accepter les mêmes projets ; sans cela, exporter
> depuis l'une et depuis l'autre ne donnerait pas le même fichier — ce que le
> critère de la phase D6 interdit.


> **D7.2 EST FAITE (30/08/2026). LE MARCHÉ EST EN VST3, ET UN HÔTE QUI
> N'ACCEPTERAIT QUE LE FORMAT QU'IL PRÉFÈRE N'HÉBERGERAIT PERSONNE.** La couche
> a exactement la même forme que celle de CLAP : un plugin chargé est présenté
> comme un `ISynthPlugin`, et le registre apprend une forme d'identifiant de
> plus, `vst3:<chemin>#<identifiant>`. C'est la **deuxième** famille de machines
> qu'on n'a pas écrites à passer par cette architecture sans la faire bouger —
> et c'est la seule façon de savoir qu'elle tient.
>
> **AUCUN TÉLÉCHARGEMENT DE PLUS.** JUCE 8 embarque le SDK VST3 et
> `juce_audio_processors` sait déjà héberger ; l'option `VSM_BUILD_VST3` n'exige
> donc rien que l'application n'exige déjà. JUCE est désormais récupéré par le
> `CMakeLists` racine plutôt que par `app/`, parce que `tools/` et `app/` lient
> tous deux cette couche et qu'elle doit être définie avant eux.
>
> **`JUCE_PLUGINHOST_VST3=1` OU RIEN.** Sans cette définition, l'hôte de JUCE se
> compile et ne trouve **jamais** aucun plugin — la pire des pannes, celle qui
> ne dit rien. Un `#error` refuse la compilation plutôt que de livrer ça.
>
> **LES DEUX RÉSOLVEURS S'ENCHAÎNENT AU LIEU DE S'ÉCRASER.** Le registre n'en
> accepte qu'un ; celui qui se pose garde celui d'avant et lui passe la main.
> L'ordre des appels n'a donc aucune importance — une règle d'ordre serait
> exactement ce que personne ne se rappellerait et que rien ne signalerait.
>
> **« SE SAUVEGARDE DANS LE PROJET » A OBLIGÉ À AJOUTER QUELQUE CHOSE, et c'est
> le vrai sujet de l'étape.** L'état d'un plugin tiers ne se réduit pas à ses
> paramètres automatisables : il y a des échantillons chargés, des matrices de
> modulation, des tables dessinées à la main, que rien dans le vocabulaire
> sémantique ne désigne. Un hôte qui ne sauvegarderait que les paramètres
> rouvrirait le morceau avec un autre son, sans le dire. D'où
> `ISynthPlugin::saveNativeState()` / `loadNativeState()` — **vides pour les
> trente-quatre machines du parc**, dont le son EST leur table de paramètres, et
> c'est une propriété qu'on ne voulait pas perdre en chemin.
>
> **C'EST DU TEXTE, ET C'EST UNE DÉCISION.** Les états natifs sont binaires ;
> l'hôte qui les produit les encode (base64), parce que la couche
> d'interopérabilité qui les écrit ne connaît que du JSON et ne doit pas
> apprendre à manipuler des octets pour une famille de machines sur trente-cinq.
>
> **L'ÉTAT NATIF S'AJOUTE AUX VALEURS SÉMANTIQUES, IL NE LES REMPLACE PAS.** Le
> premier ne se relit que par la même machine ; les secondes restent lisibles
> par un humain et applicables à une autre. Perdre le premier rendrait le
> morceau faux, perdre les secondes rendrait le fichier opaque. À la relecture,
> l'état natif est reposé **d'abord** et les valeurs nommées par-dessus : ce
> sont elles qu'un humain ou un script a pu modifier exprès dans le fichier.
>
> **ET JAMAIS SUR UNE AUTRE MACHINE** : un état natif ne se transpose pas, et le
> refus est **dit** (`PresetApplyReport::nativeStateDetail`), jamais tu.
>
> **LE TEST EST FERMÉ SUR LUI-MÊME**, comme celui de CLAP : ce dépôt construit
> un petit instrument VST3 dont l'état porte une valeur **qu'aucun paramètre
> n'expose**. Sans cela, le test de sauvegarde passerait aussi pour un hôte qui
> ne sauvegarderait que ses paramètres — c'est-à-dire pour la panne même qu'on
> veut interdire. Aucun plugin tiers installé n'est nécessaire, et le jour où un
> plugin tiers posera problème, on saura que le défaut vient de lui.
>
> **UN EFFET N'EST PAS UN INSTRUMENT** : la distinction est lue dans le fichier,
> et l'application ne propose que les instruments. Poser un effet là où une
> piste attend un instrument donnerait une piste muette qu'il faudrait deviner à
> l'oreille. Les effets viendront en D7.3.
>
> **CE QUE L'HÔTE NE PRÉTEND PAS SAVOIR** : `activeVoiceCount()` rend zéro,
> parce qu'aucun format de plugin ne publie ce chiffre. Zéro se lit comme « pas
> d'information » ; un chiffre inventé ferait mentir l'affichage de charge, qui
> existe précisément pour dire quand une machine sature.


> **D7.3 EST FAITE (30/08/2026). LA DIFFÉRENCE ENTRE UN INSTRUMENT ET UN EFFET
> TIENT EN UN MOT : L'ENTRÉE.** Les deux hôtes passaient délibérément
> `audio_inputs = nullptr` — un instrument reçoit des notes et rend du son.
> Un effet reçoit du son. C'est tout ce que le titre de l'étape veut dire, et
> c'est ce qui manquait.
>
> **CE QU'UN HÔTE SANS ENTRÉES PRODUIT** : un effet qui se charge, s'affiche,
> expose ses paramètres et rend du silence. Rien là-dedans ne ressemble à une
> panne tant qu'on ne l'écoute pas. C'est pourquoi les deux effets d'essai
> construits par ce dépôt **inversent le signe** du signal, et pourquoi les
> tests comparent à une valeur **attendue** plutôt qu'à « quelque chose de non
> nul » : un effet qui rendrait du bruit, du silence, ou son entrée intacte
> échoue les trois fois, là où un test de non-silence n'en attraperait qu'un.
>
> **« INSÉRABLES AU MÊME TITRE QUE LES NATIFS » A DÉCIDÉ DU MÉCANISME.**
> `EffectFactory` reçoit le même crochet de résolveur que `PluginRegistry` en
> D7.1 : un identifiant `clap:` ou `vst3:` demandé comme insert charge le
> fichier. Un identifiant interne (« reverb ») et un identifiant de plugin
> entrent donc par **la même porte**, et l'interface les ajoute par le même
> chemin — un second bouton « ajouter un plugin » à côté de « ajouter un effet »
> aurait suggéré deux mécanismes à tenir d'accord l'un avec l'autre. Ils
> apparaissent dans le même menu, sous la même liste.
>
> **UN INSTRUMENT N'EST PAS UN EFFET, ET LE REFUS EST DIT DANS LES DEUX SENS.**
> Poser un effet sur une piste lui ferait attendre un signal que personne ne lui
> donne ; poser un instrument en insert lui ferait ignorer celui qu'on lui
> donne. Les deux rendent une piste muette, et les deux se découvriraient à
> l'oreille. Côté VST3 la distinction est lue dans la description du plugin ;
> côté CLAP, dans les « features » qu'il déclare — **jamais** devinée du nom ni
> du nombre de ports audio : une heuristique marcherait la plupart du temps, et
> c'est précisément ce qui la rend dangereuse.
>
> **L'ÉTAT NATIF SUIT, POUR LA MÊME RAISON QU'EN D7.2.** Un effet tiers porte
> des réponses impulsionnelles chargées, des courbes dessinées, des tables
> apprises. `IAudioEffect` gagne donc `saveNativeState()` / `loadNativeState()`
> — **vides pour les treize effets internes** — et `TrackEffect` un champ
> facultatif, écrit seulement quand il existe : un projet qui n'emploie que des
> effets internes garde **exactement** le fichier qu'il avait, ce qu'un test
> vérifie.
>
> **DEUX PLUGINS D'ESSAI DE PLUS, CONSTRUITS PAR CE DÉPÔT.** L'adaptateur CLAP
> n'expose que des instruments : l'hôte d'effets n'avait rien à héberger.
> Dépendre d'un effet installé sur la machine rendrait le test vert ou rouge
> selon l'ordinateur. Le circuit reste donc fermé des deux côtés.
>
> **UNE MÉCANIQUE ÉCRITE DEUX FOIS A ÉTÉ RAMENÉE À UNE.** Poser un lot de
> valeurs de paramètres sur un plugin CLAP demande une quinzaine de lignes de
> remplissage de structures C ; l'instrument et l'effet la partagent désormais,
> plutôt que d'en garder deux copies qui auraient fini par diverger sur un
> détail que rien n'aurait signalé.
>
> **UN PIÈGE DE CONSTRUCTION, ET SA RAISON.** Un module JUCE n'est pas une
> bibliothèque compilée à part : c'est une cible INTERFACE qui **ajoute ses
> sources** à chaque cible qui la lie. `vsm_vst3_host` en bibliothèque statique
> emportait donc une copie complète de `juce_audio_processors`, et
> l'application, qui lie `juce_audio_utils`, une seconde — « définitions
> multiples » sur des centaines de symboles. La couche est passée en INTERFACE :
> sa source est compilée **une fois**, par la cible finale, avec la seule copie
> de JUCE qu'elle possède déjà.


> **D7.4 EST FAITE (30/08/2026). LE TRANSPORT EST LA MOITIÉ QUI S'ENTEND.**
> Les trente-quatre machines du parc n'en ont aucun besoin : le graphe leur
> livre des notes déjà horodatées. Un plugin qu'on n'a pas écrit, lui, ne peut
> pas deviner le tempo — un delay synchronisé, un arpégiateur, un LFO réglés
> « 1/4 » resteraient sur leur valeur d'usine, et **rien dans le son ne dirait**
> qu'ils n'ont jamais rien su du morceau.
>
> **LIVRÉ AVANT `process`, PAS PASSÉ À `process`.** Élargir la signature aurait
> obligé trente-quatre machines et treize effets à déclarer, documenter et
> ignorer un paramètre de plus. Même forme, et même raison, que
> `setSidechainInput` (D4.4).
>
> **LA POSITION EST DONNÉE DEUX FOIS, EN SECONDES ET EN NOIRES**, et aucune ne
> se déduit de l'autre sans la carte des tempos : un morceau qui accélère fait
> diverger « à la troisième seconde » et « au troisième temps ». Et « beat »
> veut dire **la noire** dans tous les formats de plugin, y compris en 6/8 où le
> temps musical est la croche pointée — convertir en temps de mesure ferait
> sauter un delay synchronisé d'un facteur trois dès qu'on quitte le 4/4. Un
> test le garde.
>
> **LE CRITÈRE EST MESURÉ COMME IL EST ÉCRIT.** Les deux effets d'essai
> construits par ce dépôt sont devenus des **delays à la noire**, dont le retard
> n'est pas un réglage mais une lecture du transport. On leur envoie une
> impulsion et on cherche l'écho : à 90 BPM il tombe à 32 000 échantillons, à
> 180 BPM à 16 000, et le rapport est bien celui des tempos — ce qui distingue
> « le plugin a reçu un tempo » de « le plugin a reçu **le** tempo ». Un second
> test, **sans transport livré**, vérifie qu'ils retombent alors sur 120 BPM
> d'usine : sans lui, les deux premiers pourraient passer avec un hôte muet.
>
> **LA FAÇADE : CE QUI EST VÉRIFIABLE SANS ÉCRAN EST CE QUI COMPTE.** L'aspect
> d'une fenêtre ne se teste pas ici ; « fermable sans perte d'état », si —
> et c'est la moitié du critère qui coûterait cher à découvrir en la subissant.
> Elle est vraie **par construction** : l'état vit dans le plugin, la fenêtre
> n'en montre qu'un dessin, et un test ouvre puis détruit la façade avant de
> comparer l'état natif et le son. La fenêtre suit la taille que le plugin
> demande et le laisse la changer s'il le permet ; une fenêtre fixe rognerait un
> éditeur redimensionnable, ce qui est pire que pas de fenêtre du tout. Deux
> fenêtres sur le même plugin sont refusées — elles montreraient le même état à
> deux endroits.
>
> **LES MACHINES DU PARC NE SE VOIENT PAS PROPOSER DE FAÇADE NATIVE** : elles
> ont la leur, montrée par le Synth Rack. Deux chemins vers la même chose, dont
> l'un ne mène nulle part, valent moins qu'un seul.
>
> **LA FAÇADE CLAP AVAIT ÉTÉ DIFFÉRÉE, ET ELLE EST FAITE DEPUIS LE
> 30/08/2026.** Le motif du report était écrit ici : « ce n'est pas long à
> écrire ; c'est **impossible à exécuter une seule fois** dans l'environnement
> où ce travail se fait, qui n'a pas d'affichage. Livrer cent cinquante lignes
> d'incrustation de fenêtre que personne n'a jamais vues tourner, en les
> déclarant faites, est exactement ce que ce projet refuse ailleurs. » Cet
> environnement a un affichage. La condition posée est donc levée — et elle
> l'est de la façon qu'elle exigeait : la façade a été **ouverte**.
>
> **CE QUI MANQUAIT N'ÉTAIT PAS L'INCRUSTATION, C'ÉTAIT L'HÔTE.** Le plugin
> était instancié avec un `clap_host` **statique et partagé**, dont
> `get_extension` répondait toujours `nullptr` et dont `host_data` était nul.
> C'était suffisant pour faire jouer un plugin : le son ne demande rien à
> l'hôte. Une interface, si — et elle le demande *en retour* :
> `request_resize`, `closed`, `register_timer` sont des appels du plugin VERS
> l'hôte, et un `host_data` nul rendait la question « de quel plugin
> s'agit-il ? » sans réponse. D'où un pont par instance, qui expose quatre
> extensions : l'interface, **les minuteries** (beaucoup d'éditeurs ne
> dessinent rien sans elles et donnent une fenêtre figée qui ressemble à un
> plugin cassé), la vérification de thread (que beaucoup interrogent avant de
> s'initialiser) et le journal (un plugin qui se plaint dans le vide est un
> plugin dont on ne saura jamais pourquoi il refuse).
>
> **L'ORDRE DES APPELS N'EST PAS NÉGOCIABLE**, et c'est tout ce que
> l'incrustation a de délicat : `create`, `get_size`, `set_parent`, `show`.
> `set_parent` exige une fenêtre native qui **existe déjà**, donc un composant
> JUCE qui a un « peer ». L'incrustation se fait donc dans
> `parentHierarchyChanged` et non dans le constructeur : un composant pas encore
> ajouté à une fenêtre rendrait un identifiant nul, et le plugin s'incrusterait
> dans la racine de l'écran — c'est-à-dire nulle part et partout.
>
> **ET IL A FALLU FABRIQUER DE QUOI L'OUVRIR.** Aucun plugin CLAP tiers à
> interface n'est installé sur la machine de développement, et en exiger un
> rendrait la vérification dépendante de l'ordinateur — exactement ce que le
> plugin d'essai de D7.3 refusait déjà pour les effets. `vsm-test-gui.clap` est
> donc un instrument CLAP minimal **qui a une vraie interface X11** : il se
> laisse reparenter, il peint une barre qui se déplace (une fenêtre qui ne bouge
> pas ne prouverait pas que les minuteries arrivent), et il demande à grandir au
> bout de deux secondes, ce qui exerce `request_resize` sans qu'un humain tire
> sur un coin.
>
> **CE QUI SE VÉRIFIE SANS ÉCRAN EST DANS LA SUITE ; CE QUI N'Y ARRIVE PAS EST
> DANS UN OUTIL.** Savoir si un plugin *a* une interface ne demande aucun
> serveur graphique, et c'est cette moitié-là qui décide si le menu propose
> l'entrée : deux tests la gardent, dans les deux sens (l'adaptateur du dépôt
> répond non, le plugin d'essai répond oui — un prédicat toujours faux passerait
> le premier sans rien garantir). L'incrustation, elle, s'ouvre :
> `vsm-clap-gui-check <fichier.clap>` emprunte **exactement** le chemin du menu,
> et rapporte la taille demandée, la taille obtenue, et si le plugin a réclamé
> un redimensionnement. Mesuré : 360 × 220 demandés, 480 × 260 après deux
> secondes.
>
> Aperçu : [`docs/images/panneaux/facade-clap.png`].


### Phase D8 — Tenir la charge

| Étape | Contenu | Terminé quand |
|---|---|---|
| D8.1 | Graphe audio multicœur | 32 pistes chargées tiennent sans décrochage ; gain mesuré et publié — **fait** |
| D8.2 | Diffusion disque pour l'audio long | 20 pistes de 9 minutes n'occupent pas 1 Go — **fait** |
| D8.3 | Un seul chemin de transport : `RealtimeTransport` et l'horloge du `ProcessGraph` sont aujourd'hui **deux notions de position** qui coexistent | une seule fait autorité ; l'autre disparaît ou en dérive — **fait : elle a disparu** |
| D8.4 | Banc de charge dans la suite de tests | le coût par piste est chiffré et suivi, comme le banc CPU de la Phase 6 — **fait** |

**Critère de phase** : le chiffre existe. Aujourd'hui personne ne sait combien
de pistes l'application supporte, et une performance qu'on ne mesure pas est une
performance qu'on croit avoir.

> **LE CHIFFRE EXISTE (30/08/2026).** Sur la machine de développement (Core
> Ultra 7 155H), à 48 kHz par blocs de 512 échantillons — budget 10,667 ms :
>
> - **trente-deux pistes chargées** (huit voix tenues et trois inserts chacune,
>   dont une distorsion qui suréchantillonne) tiennent à **18,6 % du budget**
>   avec huit threads auxiliaires, contre **68,7 % et un pire bloc au-dessus du
>   budget** — donc un clic — sur un seul cœur ;
> - **une piste ordinaire** coûte **0,011 ms**, et ce coût est linéaire : la
>   trente-deuxième coûte ce que coûtait la seizième ;
> - **la taille du planning ne coûte plus rien** : quatre mille notes par piste
>   se paient comme zéro ;
> - **vingt pistes de neuf minutes d'audio** occupent **20 Mo** au lieu de
>   4,1 Go.
>
> Ces quatre chiffres sont mesurés, pas estimés, et les trois derniers sont
> vérifiés à chaque exécution de la suite de tests. Extrapoler « combien de
> pistes en tout » à partir d'eux serait retomber dans ce que ce critère
> reproche : ce qu'on sait, c'est ce qu'on a mesuré.

> **D8.1 EST FAITE (30/08/2026). LE CHIFFRE, D'ABORD.** Trente-deux pistes
> chargées — huit voix tenues et trois inserts chacune, dont la distorsion qui
> suréchantillonne — mesurées sur la machine de développement (Core Ultra 7
> 155H, 22 cœurs logiques), à 48 kHz par blocs de 512 échantillons, dont le
> budget est de 10,667 ms :
>
> | Threads auxiliaires | p99 | % du budget | pire bloc | gain (p99) |
> |---|---|---|---|---|
> | 0 (mono-cœur) | 7,32 ms | 68,7 % | **13,34 ms — au-dessus du budget** | — |
> | 1 | 4,98 ms | 46,7 % | 12,58 ms | x1,47 |
> | 2 | 3,97 ms | 37,2 % | 4,12 ms | x1,85 |
> | 4 | 2,45 ms | 22,9 % | 2,53 ms | x2,99 |
> | **8** | **1,98 ms** | **18,6 %** | **2,05 ms** | **x3,70** |
> | 12 | 4,06 ms | 38,0 % | 6,33 ms | x1,80 |
> | 16 | 4,25 ms | 39,9 % | 6,36 ms | x1,72 |
>
> Ces chiffres viennent d'une exécution ; la QUEUE de distribution bouge d'une
> exécution à l'autre (de x2,3 à x3,7 à huit threads sur trois passages), la
> médiane et le `min` beaucoup moins. Ce qu'il faut en retenir est la FORME --
> un sommet entre six et huit threads, un décrochage net au-delà --, pas la
> troisième décimale.
>
> **CE QUE CE TABLEAU DIT ET QU'UNE MOYENNE AURAIT CACHÉ** : mono-cœur, la
> moyenne est confortable (6,83 ms, 64 % du budget) et le pire bloc dépasse
> quand même le budget — c'est-à-dire qu'il y a un clic. C'est exactement la
> raison pour laquelle la colonne retenue est le p99 et non la moyenne : un
> décrochage ne se moyenne pas, il s'entend.
>
> **LE SOMMET EST À HUIT, ET LE RÉGLAGE « UN THREAD PAR CŒUR » EST UN PIÈGE.**
> Vingt threads sur cette machine donnent le meilleur `min` de tout le tableau
> (0,99 ms) et un p99 deux fois pire que huit. La raison est structurelle : une
> ronde ne finit qu'avec son dernier travailleur, et sur un processeur hybride
> un cœur E met deux à trois fois plus longtemps qu'un cœur P à rendre la même
> piste. Le défaut recommandé est donc plafonné à huit threads auxiliaires
> (`RenderThreadPool::kRecommendedCeiling`), un plafond qu'on relèvera le jour
> où une mesure le demandera — pas avant. L'utilisateur peut toujours en
> choisir davantage à la main (*Fichier ▸ Threads de rendu*).
>
> **LA PROPRIÉTÉ QUI REND TOUT LE RESTE ACCEPTABLE : LE MULTICŒUR NE CHANGE PAS
> UN SEUL ÉCHANTILLON.** Une piste ne dépend d'aucune autre tant qu'elle n'est
> pas MÉLANGÉE : son instrument, son matériau audio et ses inserts ne lisent
> qu'elle. C'est là, et seulement là, que le calcul se répartit. Le mixage vers
> le master, les groupes, les départs et les mètres reste séquentiel et dans
> l'ordre de rendu — additionner trente-deux tampons ne coûte rien à côté de les
> calculer, et le faire dans le désordre changerait le dernier bit d'un mixage
> pour rien. Un test compare le rendu à zéro thread et à quatre, **au bit près**
> et non à epsilon près, et vérifie au passage que le chemin parallèle a bien
> été emprunté : sans ce second contrôle, il pourrait mesurer deux fois le même
> chemin et ne rien prouver. Sans cette propriété, un export cesserait de
> reproduire ce qu'on a entendu dès qu'on changerait de machine, et la règle du
> § 5 d'`ARCHITECTURE.md` deviendrait fausse sans que rien ne le dise.
>
> **UNE CHAÎNE LATÉRALE INTERDIT LE PARALLÉLISME, ET C'EST LE SEUL CAS.** Un
> effet qui écoute un bus de départ lit ce que les pistes précédentes viennent
> d'y verser : le calcul d'une piste dépend alors du MÉLANGE d'une autre, et
> l'indépendance sur laquelle tout repose n'existe plus. Le graphe s'en aperçoit
> tout seul — `refreshRenderOrder` fait déjà exactement cette recherche pour
> ordonner les pistes — et retombe sur un seul cœur pour ce projet-là. Un test
> le vérifie : quatre threads existent, aucun segment ne passe par le chemin
> parallèle.
>
> **LE BANC DE THREADS NE FAIT QU'UNE CHOSE**, et n'a ni file de travaux, ni vol
> de tâches entre rondes, ni futurs : chacune de ces généralités coûterait des
> allocations sur le chemin le plus contraint du programme. Le thread audio ne
> prend jamais de verrou — il DONNE des jetons de sémaphore et attend la fin sur
> un entier atomique — parce qu'attendre un verrou que détient un thread moins
> prioritaire est précisément le clic qu'on cherche à éviter. Le thread appelant
> travaille comme les autres, ce qui rend le banc transparent à zéro thread : la
> boucle est alors littéralement celle d'avant.
>
> **LE BOGUE QUI NE SE SERAIT VU QU'EN PRODUCTION** mérite d'être nommé, parce
> qu'il est invisible à la lecture : un travailleur qui vient de finir la
> DERNIÈRE tâche est encore dans sa boucle et va tenter une prise de plus avant
> d'en sortir. Si l'appelant était déjà reparti préparer la ronde suivante,
> cette prise-là piocherait dans la nouvelle ronde et en exécuterait la première
> tâche deux fois — une piste doublée, une fois sur mille blocs. Le banc compte
> donc les travailleurs encore DANS la ronde, et non les tâches restantes.
> `ThreadSanitizer` passe la suite audio complète sans un seul avertissement.
>
> **CHANGER LE NOMBRE DE THREADS PENDANT QUE LE SON TOURNE** est une chose qu'un
> utilisateur fait ; détruire un thread en train de rendre un bloc en est une
> autre. Le thread d'interface ferme d'abord la porte (`parallelAllowed_`), puis
> attend que le bloc en cours soit sorti (`renderBusy_`) : les deux atomiques
> sont en `seq_cst`, la seule cohérence qui garantisse qu'au moins l'un des deux
> côtés voie l'autre.
>
> **ET LE RENDU HORS LIGNE EN PROFITE AUSSI**, sans qu'on ait rien à régler :
> puisque le résultat est identique au bit près, un export à huit threads est le
> même fichier qu'à un seul, simplement obtenu plus vite.

> **D8.2 EST FAITE (30/08/2026). LE CHIFFRE, D'ABORD.** Un matériau diffusé
> occupe **1,0 Mo** en mémoire — quatre fenêtres de 32 768 trames stéréo — plus
> le tampon de décodage quand il faut rééchantillonner. Vingt pistes en
> occupent vingt, contre **4,1 Go** avant : neuf minutes de stéréo à 48 kHz font
> 207 Mo une fois décodées en flottants, et vingt pistes de ce genre
> demandaient plus de mémoire que n'en a la machine.
>
> **LE CHIFFRE NE DÉPEND PAS DE LA DURÉE DU FICHIER, ET C'EST TOUTE LA
> DÉMONSTRATION.** Écrire vingt fichiers de neuf minutes pour vérifier le
> critère coûterait deux gigaoctets de disque et une minute à chaque exécution
> de la suite, pour mesurer une propriété qui se démontre exactement. Le test
> compare donc la mémoire d'un fichier d'une seconde et celle d'un fichier de
> quarante, exige qu'elles soient **égales**, puis fait l'arithmétique. C'est
> plus fort qu'un essai à vingt pistes, qui ne dirait rien de la vingt-et-unième.
>
> **LA COUTURE ÉTAIT ANNONCÉE, ET ELLE A TENU.** Le commentaire d'en-tête
> d'`AudioTrackSource` disait, depuis D2 : « la diffusion changera CETTE classe
> sans toucher au reste ». C'est ce qui s'est passé — `ProcessGraph` n'a pas
> bougé d'une ligne. Le matériau est désormais un `SampleStore`, dont il existe
> deux implémentations : `MemorySampleStore` (tout le fichier, décodé) et
> `StreamedSampleStore` (quatre fenêtres glissantes, le reste sur le disque).
> Le graphe ne sait pas laquelle il joue.
>
> **LE SEUIL EST À VINGT SECONDES, ET C'EST UNE DÉCISION.** Ce qui est court est
> lu cent fois et doit répondre à l'échantillon près : un coup de caisse claire
> de trois secondes n'a rien à faire sur le disque, et le diffuser
> n'économiserait rien tout en ajoutant une latence. Ce qui est long est lu une
> fois d'un bout à l'autre, ce qu'un cache glissant sert exactement. Au-dessus
> de vingt secondes, plus rien n'est un « échantillon ». Le choix se fait sur la
> durée RÉELLE du fichier, lue dans son en-tête avant de décoder quoi que ce
> soit — pas sur ce que le projet en déclare, qui peut mentir.
>
> **QUATRE FENÊTRES, ET PAS DEUX.** Deux suffiraient à la lecture linéaire et
> laisseraient un trou au premier montage un peu serré : deux clips superposés
> puisent à deux endroits du même fichier, et un bloc à cheval sur une frontière
> en touche deux d'un coup. Un trou qu'on entendrait sans savoir d'où il vient.
>
> **LE PIÈGE QUI A COÛTÉ UN SILENCE DÉFINITIF, ET QU'UN TEST A ATTRAPÉ.**
> L'anneau des demandes tenait huit entrées pour quatre fenêtres. Le
> remplissage refuse de recycler une fenêtre encore réclamée — c'est ce qui
> empêche deux besoins d'alterner en se chassant l'un l'autre — mais avec plus
> de demandes que de fenêtres, des demandes **périmées** suffisaient à toutes
> les épingler, et une fenêtre réellement nécessaire ne se chargeait plus
> jamais. L'anneau tient désormais exactement autant de demandes qu'il y a de
> fenêtres, ce qui rend la situation impossible plutôt qu'improbable.
>
> **LE THREAD DE DIFFUSION ATTEND LE THREAD AUDIO, ET JAMAIS L'INVERSE.** Avant
> de réécrire une fenêtre, il l'invalide, puis attend que plus personne n'y
> lise. C'est le seul endroit du moteur où un thread attend le thread audio, et
> c'est le bon sens de l'attente : celui qui n'a pas d'échéance attend celui qui
> en a une. Un test dédié fait sauter un lecteur d'un bout à l'autre du fichier
> — donc recycler les fenêtres en permanence — et vérifie **chaque échantillon
> servi** : le signal d'essai encode la position dans la fenêtre à gauche et le
> numéro de la fenêtre à droite, parce qu'une fenêtre servie à la place d'une
> autre porte les mêmes décalages internes et passerait inaperçue sinon.
> `ThreadSanitizer` passe la suite audio complète sans un avertissement.
>
> **CE QUE LE DISQUE N'A PAS LIVRÉ SE COMPTE** (`AudioTrackSource::cacheMisses`),
> comme les notes que le moteur n'a pas pu jouer. Un trou de diffusion ne se
> distingue pas, à l'oreille, d'un passage silencieux ; le compteur est la seule
> chose qui permette de dire lequel des deux on vient d'entendre. Et une
> position **hors du fichier** n'en est pas un : il n'y a rien à y livrer,
> jamais, et les confondre ferait sonner l'alarme sur chaque clip qui dépasse la
> fin de sa prise.
>
> **L'EXPORT DIFFUSE AUSSI, MAIS EN ATTENDANT.** Le rendu hors ligne applique le
> même seuil et va chercher lui-même ce qui manque au lieu de se taire : un
> export dans lequel il manquerait ce que le disque n'a pas eu le temps de
> livrer ne serait pas un export, ce serait une loterie. C'est aussi ce qui rend
> exportable un projet dont l'audio ne tiendrait pas en mémoire — exactement
> celui que cette phase débloque.
>
> **ET L'APERÇU AUSSI, SANS QUOI RIEN N'AURAIT ÉTÉ GAGNÉ.** Il ne servirait à
> rien de ne plus charger une prise de neuf minutes si dessiner sa forme d'onde
> exigeait quand même de la charger une fois. `computePeaksFromFile` relit le
> fichier par tranches de 262 144 trames et ne garde que les extrêmes ; un test
> vérifie que le dessin obtenu est celui du chemin résident, tranche par
> tranche.

> **D8.3 EST FAITE (30/08/2026). LE CHOIX ÉTAIT ENTRE « DISPARAÎT » ET « EN
> DÉRIVE » : C'EST DISPARAÎT.** `RealtimeTransport` est supprimé — en-tête,
> source, tests. Il ne restait de lui qu'une position redondante et un thread ;
> sa dernière justification écrite, « il pilote encore la sortie MIDI
> (`IMidiEventSink`) », était vide au sens propre : le seul récepteur du
> programme, `MainComponent::onMidiEvent`, ne contenait qu'un
> `juce::ignoreUnused`.
>
> **CE QUE LA COEXISTENCE COÛTAIT, ET CE N'ÉTAIENT PAS DES FAUTES D'ÉCRITURE
> MAIS DES CONSÉQUENCES DE LA STRUCTURE.** Trois défauts, dont deux que
> personne n'avait rattachés à leur cause :
>
> 1. **La position n'avançait qu'aux événements.** Le thread MIDI dormait
>    jusqu'à la note suivante et ne publiait sa position qu'en la jouant : entre
>    deux notes espacées le curseur ne bougeait pas d'un pixel, et sur une nappe
>    tenue il restait figé pendant des secondes.
> 2. **Un projet uniquement AUDIO ne pouvait pas jouer du tout.** Sans note, le
>    planning était vide, la passe se terminait « naturellement » dès le premier
>    tour, et le transport s'arrêtait avant d'avoir commencé. Le DAW savait
>    charger une prise de neuf minutes — c'est tout l'objet de D2 et de D8.2 —
>    et refusait de la lire.
> 3. **Démarrer la lecture repositionnait le moteur audio sur l'horloge du
>    thread MIDI**, c'est-à-dire sur la moins exacte des deux, et
>    l'interface recopiait l'état de l'un dans l'autre une fois par tour de
>    minuterie en comparant deux booléens.
>
> **CE QUI LE REMPLACE NE TIENT AUCUNE POSITION.** `engine::Transport` LIT celle
> du graphe et n'ajoute que ce que le graphe n'a pas à connaître : l'état
> (arrêté / en lecture / en pause — l'arrêt étant la pause qui revient à zéro),
> la conversion en ticks, et la fin du morceau. C'est la seule chose que le
> graphe ne peut pas décider seul : il sait rendre, il ne sait pas ce qu'est
> « la fin ».
>
> **ET « LA FIN » N'EST PLUS LA DERNIÈRE NOTE.** `Project::lastUsedTick()` ne
> connaît que le matériau MIDI — ce qui est exactement ce qu'il faut au
> planificateur, qui s'en sert pour décider où s'arrêtent les répétitions d'un
> clip. S'en servir pour dire « le morceau est fini » était l'erreur qui rendait
> un projet audio injouable. `Project::lastSoundingTick()` compte aussi les
> clips, et c'est elle que le transport **et l'export** emploient : l'export
> d'un projet uniquement audio produisait sinon un fichier de deux secondes —
> la seule queue de réverbération — pour neuf minutes de prise.
>
> **SANS CARTE SON, C'EST LA MÊME HORLOGE, SIMPLEMENT ALIMENTÉE AUTREMENT.**
> L'application doit rester utilisable pour éditer, faire défiler et exporter
> sur une machine sans audio — c'était la vraie raison de garder l'ancien
> transport. Un thread de secours appelle donc `processBlock` dans un tampon
> qu'on jette, au rythme du temps réel, et son échéance se calcule depuis
> l'origine et non bloc par bloc (même règle qu'en D6.5 : additionner des
> attentes courtes accumule l'erreur de chaque réveil). C'est **volontairement**
> le même chemin de calcul : une seconde façon de faire avancer le temps serait
> une seconde façon de se tromper, ce dont cette phase se débarrasse
> précisément. Il rend la main dès que la carte revient — deux moteurs qui
> avanceraient le même graphe le feraient avancer deux fois plus vite, et un
> test le vérifie dans les deux sens.

> **D8.4 EST FAITE (30/08/2026), ET ELLE A TROUVÉ CE QU'ELLE CHERCHAIT DÈS LE
> PREMIER JOUR.** Le banc CPU de la Phase 6 est un exécutable à part, qu'on
> lance à la main quand on y pense — c'est-à-dire une fois par optimisation.
> Entre deux, personne ne regarde : une régression de performance entre dans le
> dépôt et n'en ressort qu'au moment où quelqu'un se plaint d'un clic. Le banc
> de charge, lui, vit dans `audio/tests/test_banc_de_charge.cpp` et tourne à
> chaque fois.
>
> **CE QU'UN TEST DE PERFORMANCE PEUT AFFIRMER, ET CE QU'IL NE PEUT PAS.** Il ne
> peut pas dire « ce bloc coûte 0,42 ms » : la même ligne donne des chiffres
> différents selon le cœur, la fréquence et ce que fait le reste de la machine.
> Il peut dire deux choses, et ce sont les deux qui comptent :
>
> - **des RAPPORTS**, qui ne dépendent d'aucune de ces variables. « Doubler les
>   pistes double le coût » et « la densité du planning ne coûte rien » sont des
>   propriétés de l'algorithme, pas de la machine ;
> - **un CHIFFRE en étalons** — combien d'enveloppes ADSR coûte une piste —,
>   comparable d'une exécution à l'autre et **imprimé** à chaque passage de la
>   suite. C'est ce que le critère appelle « chiffré et suivi ».
>
> **LE CHIFFRE.** Une piste (une machine, quatre voix tenues, aucun insert)
> coûte **0,011 ms** par bloc, soit environ **26 000 étalons**, mesuré comme
> coût MARGINAL — la différence entre seize pistes et une, divisée par quinze —
> et non comme le total divisé par le nombre de pistes : un bloc porte des frais
> fixes (bus master, mètres, métronome) qui n'appartiennent à aucune piste.
>
> **LA RÉGRESSION QUE LE BANC A TROUVÉE, ET ELLE ÉTAIT ÉNORME.** Le planning
> était trié par TEMPS, et chaque piste le parcourait EN ENTIER, à chaque
> sous-segment d'automation, pour n'en garder que ce qui la concernait : le coût
> d'un bloc valait « pistes × événements ». Une quadratique, invisible sur les
> projets d'essai à quatre notes et écrasante sur un vrai. Mesuré : trente-deux
> pistes de quatre mille notes coûtaient **10,4 ms par bloc contre 3,9 ms à
> vide — 99,5 % du budget**, dont l'essentiel passé à ÉCARTER des notes situées
> à deux minutes de la tête de lecture. Les jouer ne coûte rien, puisqu'on ne
> les joue pas ; le seul coût légitime était celui de ne pas les regarder.
>
> Le snapshot range désormais le planning **par (piste, temps)** — un tri stable
> sur la piste conserve l'ordre temporel que le planificateur a établi — et
> chaque piste entre dans sa tranche par recherche dichotomique. Le coût est
> devenu **plat** : 3,97 ms à vide, 3,97 ms avec quatre mille notes par piste.
> Le test le garde avec un rapport, donc sur n'importe quelle machine ; remis
> l'ancien parcours en place pour vérifier, il échoue à **x10,0**.
>
> **CE QUE LA SUITE IMPRIME MAINTENANT, à chaque exécution** : le coût d'une
> piste et de seize, le coût marginal par piste, le rapport entre 1→16 et 16→32
> (la linéarité), le surcoût d'un planning dense, et le coût d'un bloc pris en
> fin de morceau contre un bloc pris au début (la dichotomie). Quatre rapports,
> quatre régressions structurelles qui ne peuvent plus passer.

### Phase D9 — Reconstruire depuis l'application

La case où ce logiciel peut être **devant** les trois autres.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D9.1 | Glisser un fichier audio lance la chaîne, si Python est présent | absence de Python = fonction grisée **avec sa raison**, jamais une erreur — **fait** |
| D9.2 | Avancement visible et annulable | séparation, transcription, recherche : chaque étape s'affiche — **fait** |
| D9.3 | Le résultat arrive comme un projet **ouvert**, pas comme un dossier à charger | pistes, patchs, notes douteuses marquées — **fait** |
| D9.4 | Écoute A/B étendue à tout le flux de travail | déjà faite pour un projet chargé — **fait** |

**Critère de phase, et il est double** : la chaîne se lance depuis l'interface,
**et** le DAW se compile et fonctionne sans Python (règle n° 2 du § 0). Si tenir
les deux demande de compliquer le code, c'est la seconde qui gagne.

> **LA PHASE D9 EST FAITE (30/08/2026), ET LES DEUX MOITIÉS DU CRITÈRE TIENNENT
> SANS SE CONTREDIRE.** Un morceau glissé sur la fenêtre — ou choisi dans
> *Fichier ▸ Reconstruire un morceau...* — part dans la chaîne, montre ce
> qu'elle fait, s'annule, et revient sous la forme d'un projet **ouvert** avec
> l'original prêt en regard. Et pas une ligne de Python n'est liée au binaire :
> le DAW se compile et passe ses 1180 tests sur une machine qui n'en a pas.
>
> **CE QUI REND LES DEUX COMPATIBLES TIENT EN UNE PHRASE : LA CHAÎNE EST UN
> PROCESSUS, PAS UNE BIBLIOTHÈQUE.** Embarquer un interpréteur aurait fait de
> Python une dépendance de compilation ; le lancer comme un enfant en fait une
> dépendance d'exécution *facultative*. Le même choix protège d'autre chose :
> la chaîne charge `torch` et `demucs`, alloue plusieurs gigaoctets, et peut
> s'effondrer sur un modèle absent ou une carte graphique qui refuse. Dans le
> processus du DAW, chacun de ces échecs emporterait le morceau ouvert. C'est
> exactement le raisonnement du balayage des plugins (D7.5).
>
> **D9.1 — « JAMAIS UNE ERREUR » A UNE CONSÉQUENCE PRÉCISE SUR LA DÉTECTION.**
> La tentation était d'exécuter `python -c "import demucs"` pour savoir si
> l'environnement est complet. C'est refusé : cela ferait dépendre l'ouverture
> d'un menu du démarrage d'un interpréteur, qui prend une seconde quand tout va
> bien et se **bloque** quand tout va mal. La détection ne regarde donc que des
> fichiers — quelques `stat`, qui ne peuvent ni échouer ni attendre — et elle
> distingue **trois** situations plutôt que deux, parce que la deuxième et la
> troisième n'appellent pas le même geste :
>
> | Ce qu'on trouve | Ce que l'application dit | Ce qu'elle propose |
> |---|---|---|
> | rien | « la chaîne d'analyse (le dossier `analyse/`) est introuvable » | *Indiquer le dossier de la chaîne...* |
> | `reconstruire.py`, pas de `.venv` | « l'environnement Python n'a pas été créé » | la commande exacte à taper |
> | les deux | — | l'entrée est active |
>
> Répondre « chaîne introuvable » dans le deuxième cas enverrait chercher un
> dossier que l'utilisateur a sous les yeux, et le vrai remède — trois mots de
> commande — ne serait dit nulle part. Et un chemin désigné à la main qui se
> révèle faux **n'est pas remplacé en silence** par celui que la recherche
> aurait trouvé : lui trouver quand même le bon lui ferait croire que son
> réglage est correct, et le jour où il le déplacerait, plus rien ne marcherait
> sans raison apparente.
>
> **CE QUE LA DÉTECTION NE PROMET PAS**, écrit plutôt que découvert : trouver
> l'interpréteur ne prouve pas que `torch` est installé. Une dépendance
> manquante se voit au lancement, dans la sortie de la chaîne — et c'est le bon
> endroit, puisque c'est là qu'elle est nommée.
>
> **LE GLISSER-DÉPOSER DEMANDE AVANT DE PARTIR POUR DIX MINUTES.** Un fichier
> lâché sur une fenêtre est un geste ambigu — on peut vouloir l'écouter, le
> poser sur une piste, ou le reconstruire — et lancer d'autorité l'opération la
> plus longue des trois serait le pire choix par défaut. Seul l'audio est
> accepté : un `.mid` glissé est un projet à importer, et les confondre
> lancerait une analyse de dix minutes sur un fichier qui n'attendait qu'à être
> lu.
>
> **D9.2 — PAS DE POURCENTAGE INVENTÉ.** Les cinq étapes durent de trois
> secondes (lecture) à dix minutes (séparation) ; une barre qui les traiterait
> comme égales passerait 80 % de son temps entre 20 et 40 %, ce qui est pire
> que pas de barre du tout. La fenêtre montre donc **l'étape que la chaîne
> annonce elle-même** — elle écrit `[2/5] Séparation en stems (htdemucs)` — et
> le compte est lu dans la ligne au lieu d'être supposé : le jour où la chaîne
> passera à six étapes, la fenêtre suivra sans qu'on y touche. Le journal
> défile en dessous, en lecture seule mais **sélectionnable** : quand la chaîne
> échoue, la ligne qui l'explique doit pouvoir être copiée, pas recopiée à la
> main.
>
> **ANNULER LAISSE LE DOSSIER INCOMPLET EN PLACE**, et c'est délibéré : il
> contient ce que la séparation a déjà produit, et l'effacer perdrait quatre
> minutes de calcul pour un projet qu'on relancera peut-être avec d'autres
> options.
>
> **D9.3 — UN SEUL CHEMIN D'OUVERTURE.** Le résultat n'arrive pas comme un
> dossier à retrouver : il s'ouvre. Et il s'ouvre par la fonction qui ouvre un
> projet désigné à la main, extraite pour l'occasion de la lambda du sélecteur
> de fichiers — presets appliqués, échantillons chargés, `rapport.json` lu et
> notes douteuses marquées dans le piano roll. Deux chemins d'ouverture
> finiraient par ne plus charger tout à fait la même chose, et c'est le second
> qui serait oublié.
>
> **D9.4 — L'ÉCOUTE A/B EST PRÊTE AVANT QU'ON LA DEMANDE.** Elle existait pour
> un projet qu'on ouvre à la main : on chargeait la reconstruction, puis on
> allait chercher l'original dans un menu. Or le moment où la comparaison
> compte le plus est celui où la reconstruction vient de finir — et c'est
> précisément le moment où l'application **sait** de quel fichier elle est
> partie. Le lui faire redemander était une question dont elle avait déjà la
> réponse. Si le décodeur du DAW ne sait pas relire l'original, on n'ouvre PAS
> de fenêtre d'erreur par-dessus le projet qui vient de s'ouvrir : la chaîne,
> elle, a su le lire, donc c'est une limite du décodeur et non un échec de la
> reconstruction.
>
> **ET LE DOSSIER COURANT DU PROCESSUS N'EST PAS TOUCHÉ.** Le réflexe est de
> faire un `chdir` vers le dossier de la chaîne avant de lancer l'enfant.
> `setAsCurrentWorkingDirectory` agit sur le processus ENTIER, depuis un thread
> de fond, pendant que l'utilisateur ouvre peut-être un sélecteur de fichiers :
> un effet de bord global payé par tout le reste de l'application. Rien ne
> l'exige — `reconstruire.py` ajoute lui-même son dossier au chemin d'import et
> le pont trouve `vsm-render` en remontant depuis `__file__`. Vérifié en
> lançant la chaîne depuis un autre dossier.

### Phase D10 — Le confort qui fait qu'on reste

Regroupées parce qu'aucune n'est structurante, et qu'aucune ne se remarque tant
qu'elle est là.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D10.1 | Navigateur : machines, presets, profils, échantillons, recherche, glisser-déposer | trouver un preset ne demande plus d'ouvrir un dossier — **fait** |
| D10.2 | MIDI learn **persistant**, liste des associations, moyen d'en défaire une (`clearMidiLearn()` n'est appelé de nulle part), et cartographie du transport et du mixeur | un potentiomètre physique s'en souvient d'une session à l'autre — **fait** |
| D10.3 | Raccourcis configurables, table imprimable, fenêtre de préférences | une page les liste tous — **fait** |
| D10.4 | Sauvegarde automatique et récupération après plantage | tuer l'application ne perd pas plus d'une minute — **fait** |

> **D10.2 EST FAITE (30/08/2026).** Ce qui se perdait à chaque lancement n'était
> pas une préférence de confort : c'était le câblage d'un studio, refait à la
> main à chaque démarrage. Les associations vivent maintenant dans le fichier de
> préférences, écrites **dès qu'elles changent** et non à la fermeture — une
> application qui se termine mal ne doit pas faire perdre ce travail-là.
>
> **CE QUI MANQUAIT N'ÉTAIT PAS UN OUBLI, C'ÉTAIT UNE FRONTIÈRE DE THREADS.**
> Le MIDI learn ne savait piloter qu'un paramètre de machine, et pour une
> raison précise : un paramètre de machine se règle par un `std::atomic`, que le
> thread MIDI peut écrire. Le volume, le panoramique, le muet, les départs et le
> transport vivent dans le PROJET, que seul le thread de l'interface a le droit
> de modifier. La réponse n'est pas de forcer la frontière mais de la traverser
> proprement : le thread MIDI DÉPOSE dans une file sans verrou, la minuterie de
> l'interface applique. Un potentiomètre physique envoie cent messages par
> seconde ; la republication du projet est donc coalescée, comme pour un geste
> de souris sur le mixeur.
>
> **UNE BASCULE S'APPUIE, UN FADER SE POSITIONNE.** Traiter l'un comme l'autre
> ferait démarrer la lecture au milieu d'une course de potentiomètre. Le seuil
> est celui du MIDI : 64.
>
> **PAS DE « VOLUME GÉNÉRAL », ET C'EST UN CONSTAT PLUTÔT QU'UN OUBLI.** Le
> modèle n'a pas de fader master : la tranche master est un correcteur, un
> compresseur et un limiteur. Lui ajouter un gain de sortie pour que le MIDI
> learn ait quelque chose à piloter mettrait dans le chemin audio un réglage
> qu'aucune interface ne montre, et qu'on retrouverait un jour à une valeur
> qu'on n'a jamais choisie. « Piloter le mixeur » veut donc dire ici : volume,
> panoramique, muet, solo et départs des PISTES.
>
> **LE LEARN NE PEUT PLUS SE FAIRE UNIQUEMENT EN TOUCHANT UN RÉGLAGE.** C'était
> le seul geste possible, et c'est exactement pourquoi le transport et le
> mixeur étaient inatteignables : ils n'ont pas de potentiomètre à toucher dans
> le Synth Rack. La fenêtre *Associations MIDI* propose donc les cibles, et
> **seulement celles qui existent** — un départ n'apparaît que si le projet le
> déclare, les réglages de piste que s'il y a une piste choisie. Promettre une
> association qui ne ferait rien serait pire que de ne pas la proposer.
>
> **ET LES ASSOCIATIONS SE VOIENT.** Le MIDI learn marchait et était
> **invisible** : retrouver qu'un potentiomètre pilotait la résonance de la
> piste 4 demandait de les tourner tous en regardant l'écran, et en défaire une
> demandait de la remplacer ou d'effacer les quinze. La liste dit `CC 74 →
> piste 4 · Resonance`, avec le nom que la MACHINE donne au paramètre — pas son
> numéro, qui n'aide personne.
>
> **CE QUE LA RELECTURE REFUSE.** Les genres sont écrits en toutes lettres,
> jamais par leur numéro : un `enum class` se réordonne un jour, et des fichiers
> à numéros se mettraient alors à piloter autre chose, en silence. Une
> association dont le genre est inconnu — fichier écrit par une version future —
> est **écartée et comptée**, jamais devinée : un potentiomètre qui pilote autre
> chose que ce qu'on croit est pire qu'un potentiomètre inerte, et le compte
> permet de le DIRE au lieu de laisser chercher.

> **D10.4 EST FAITE (30/08/2026). LA CADENCE EST DE TRENTE SECONDES**, là où le
> critère dit « pas plus d'une minute » : une marge de deux vaut mieux qu'une
> marge nulle sur un disque qui hésite. Une photo n'est prise que si le projet a
> changé — un studio ouvert sans qu'on y touche n'a aucune raison d'écrire.
>
> **CE QU'ELLE N'ÉCRIT PAS EST AUSSI IMPORTANT QUE CE QU'ELLE ÉCRIT.**
> Enregistrer un projet complet (`exportStandaloneProject`) COPIE tous les
> médias. Recopier une prise de deux cents mégaoctets toutes les trente secondes
> ferait de la sauvegarde automatique la panne dont elle devait protéger. Elle
> écrit donc `project.json`, le MIDI et les presets, et **retient le dossier
> d'origine** : les chemins de médias lui restent relatifs. Sans ce souvenir, un
> projet récupéré rouvrirait avec toutes ses pistes audio muettes, et rien
> n'expliquerait pourquoi.
>
> **ELLE N'ÉCRIT PAS SUR LE THREAD DE L'INTERFACE.** Le projet est copié là — un
> type valeur, copie rapide et cohérente — et écrit ailleurs. Un studio qui
> hoquette toutes les trente secondes est un studio dont on désactive la
> sauvegarde automatique, et la protection s'en va avec elle.
>
> **ELLE ÉCRIT À CÔTÉ PUIS BASCULE.** Une sauvegarde interrompue *en cours
> d'écriture* laisserait un `project.json` tronqué : un plantage pendant la
> sauvegarde détruirait la sauvegarde, c'est-à-dire exactement le scénario
> qu'elle couvre.
>
> **COMMENT ON SAIT QU'UNE SESSION S'EST INTERROMPUE, ET COMMENT ON ÉVITE LE
> FAUX POSITIF.** Chaque exécution a son dossier et l'efface en se terminant
> **normalement** ; un dossier qui subsiste est donc celui d'une session morte
> sans se fermer. Mais un dossier qui subsiste, c'est aussi celui d'une deuxième
> fenêtre **ouverte en ce moment** — et proposer de récupérer une session qui
> est en train de travailler serait pire que ne rien proposer. Chaque session
> tient donc un **verrou inter-processus** sur son dossier : s'il s'acquiert, le
> propriétaire n'existe plus. C'est le verrou, et lui seul, qui distingue « ça a
> planté » de « c'est ouvert ailleurs ».
>
> **ET L'EFFACEMENT N'EST PAS DANS LE DESTRUCTEUR.** Un destructeur s'exécute
> aussi bien à la fermeture normale qu'au démontage après une erreur ; effacer
> des deux côtés effacerait justement ce qu'on voulait garder. C'est la
> fermeture explicite qui le dit.
>
> **LA QUESTION POSÉE N'EST PAS « RÉCUPÉRER UNE SESSION ? »** — personne ne peut
> répondre à celle-là. Elle dit lequel, de quand, et ce qu'il contient : *« Sky
> and Sand — 12 piste(s), 4821 note(s), enregistré automatiquement il y a 3
> minutes »*. Et quand le projet **n'avait jamais été enregistré**, elle le dit
> explicitement : c'est le cas où l'on ne perd pas une minute mais tout, et
> c'est celui pour lequel cette étape existe.

> **D10.3 EST FAITE (30/08/2026). CE QUI EXISTAIT ÉTAIT INVISIBLE** : deux
> `switch` sur des codes de touches, l'un dans `MainComponent`, l'autre dans le
> piano roll, et rien qui les liste. La seule façon de savoir ce que faisait une
> touche était de l'essayer ; la seule façon de savoir quelles touches faisaient
> quelque chose était de lire deux fichiers de code.
>
> **LE CATALOGUE EST LA SOURCE, LES `switch` SONT DES CONSÉQUENCES.** Chaque
> commande est déclarée une fois dans `interchange/ShortcutTable.h`, avec son
> libellé, sa famille et sa touche par défaut. Une touche pressée désigne
> désormais une **commande**, et les deux gestionnaires consultent la même
> table. Un raccourci qu'on ajouterait dans le code sans le déclarer là
> n'apparaîtrait pas dans la page — et c'est précisément pour cela que le code
> ne doit plus les connaître autrement.
>
> **CE QUI NE SE RECONFIGURE PAS EST ÉCRIT PLUTÔT QU'OMIS.** Les flèches
> déplacent la sélection et `Maj` en quadruple le pas : leur sens EST leur
> direction, et les réassigner produirait une flèche gauche qui monte. Elles
> figurent donc dans la page, marquées comme fixes. Une page qui prétend tout
> lister et tait quatre touches ment davantage qu'une page qui dit « celles-ci
> ne bougent pas ».
>
> **TROIS PIÈGES, TROIS DÉCISIONS ÉCRITES.**
>
> - **`command` devient `ctrl`.** Sous macOS, JUCE écrit « command + S » ; le
>   logiciel accepte depuis toujours les deux indifféremment. Une table qui les
>   distinguerait obligerait l'utilisateur d'un Mac à tout reconfigurer.
> - **`Maj` est retiré à la seconde tentative, et seulement quand il est seul.**
>   Sur la plupart des dispositions, `+` s'obtient par `Maj` `=` : JUCE rend
>   alors « shift + = ». Le retirer toujours ferait répondre « Annuler » à
>   Ctrl+Maj+Z, qui est « Rétablir ».
> - **L'alias ne suit pas la personnalisation.** `Retour arrière` supprime et
>   `Ctrl+Y` rétablit parce que l'usage l'attend, pas parce que ce sont de
>   seconds raccourcis. Réassigner « Supprimer » à F1 doit rendre `Retour
>   arrière` inerte — sinon il effacerait encore, et on chercherait longtemps.
>
> **UN CONFLIT SE DIT AVANT D'ÊTRE CRÉÉ**, en nommant la commande qui tient
> déjà la touche : deux commandes sur la même touche, c'est une seule qui
> répond et rien qui dise laquelle. Une touche **vide désactive** une commande,
> et c'est un choix légitime — le taire obligerait à inventer une touche pour se
> débarrasser d'un raccourci gênant. Pendant une capture, la touche est une
> **donnée et non une commande** : sans ce détournement, appuyer sur `Espace`
> pour le réassigner lancerait la lecture, et l'on ne pourrait jamais changer
> une touche déjà prise — c'est-à-dire aucune de celles qu'on veut changer.
>
> **LA TABLE S'IMPRIME**, en texte : on l'imprime, on la colle au mur du studio,
> on la cherche avec Ctrl+F. Une capture d'écran ne ferait aucune des trois. Un
> test vérifie le critère comme il est écrit — chaque commande du catalogue,
> avec son libellé et sa touche, doit s'y trouver, les fixes comprises.
>
> **ET LES PRÉFÉRENCES SONT RASSEMBLÉES.** Elles existaient toutes, éparpillées :
> la taille de l'interface dans *Affichage*, les threads de rendu et le dossier
> de la chaîne d'analyse dans *Fichier*, les raccourcis et les associations MIDI
> dans deux fenêtres qu'il fallait connaître. Un réglage qu'on ne retrouve qu'en
> se souvenant du menu où il se cache est un réglage qu'on ne change pas. Le
> panneau ne détient rien : chaque contrôle appelle l'application, qui possède
> déjà le réglage — dupliquer l'état créerait une seconde vérité, et c'est
> toujours la seconde qui finit par mentir.

> **D10.1 EST FAITE (30/08/2026), ET LA PHASE D10 AVEC ELLE.** Ce que
> l'application savait faire, c'était **charger** un preset, un profil, un
> échantillon — chacun par un sélecteur de fichiers, c'est-à-dire à condition de
> savoir déjà où il était. Trente-quatre machines, autant de presets par projet,
> des profils multi-échantillons et des dossiers de samples : la matière
> existait, et le seul moyen d'y accéder était de s'en souvenir.
>
> **L'INVENTAIRE NE LIT AUCUN CONTENU**, et c'est ce qui le rend instantané : il
> lit des NOMS de fichiers et des extensions. Un dossier d'échantillons contient
> facilement des milliers de fichiers ; les ouvrir un par un pour savoir ce
> qu'ils sont ferait de l'ouverture du navigateur une attente. Ce qu'on lit
> vraiment — la machine d'un preset — l'est au moment où on le pose.
>
> **LA PROFONDEUR EST BORNÉE.** Un dossier de bibliothèque peut être n'importe
> quoi, y compris la racine d'un disque désignée par mégarde ; une exploration
> sans fond transformerait une erreur de clic en gel de plusieurs minutes.
>
> **LA RECHERCHE EST DÉLIBÉRÉMENT SIMPLE** : tous les mots, dans n'importe quel
> ordre, sans casse, dans le nom ou l'origine. « 303 acid » trouve « TB-303 Acid
> Lead » comme « acid lead (tb303) ». Une recherche floue rendrait des résultats
> qu'on ne saurait pas expliquer, et la seule chose qu'on demande à un
> navigateur est qu'on comprenne pourquoi ce qu'il montre est là. **Il s'ouvre
> plein, pas vide** : un navigateur qui exige une requête avant de montrer quoi
> que ce soit suppose qu'on sait ce qu'on cherche, alors qu'on l'ouvre justement
> pour voir ce qu'il y a.
>
> **L'ORIGINE DIT LE SOUS-DOSSIER, PAS SEULEMENT LA BIBLIOTHÈQUE** : deux
> « basse » rangées à deux endroits doivent se distinguer sans qu'on ait à les
> essayer.
>
> **DEUX GESTES, ET LE SECOND EXISTE PARCE QUE LE PREMIER MENT UN PEU.** Le
> double-clic applique à la piste sélectionnée : c'est le geste court, et il
> suppose qu'on a la bonne piste en tête. Le glisser dépose sur la piste qu'on
> VOIT — la liste souligne celle qu'on survole, sans quoi on lâche à l'aveugle
> et on découvre après coup qu'on vient de changer le son de la mauvaise.
>
> **UN PRESET EMPORTE SA MACHINE.** Appliquer un preset de TB-303 sur un DX7
> réglerait des paramètres qui n'ont pas le même sens, et rien ne dirait
> pourquoi ça ne sonne pas : le preset déclare sa machine, et la piste en
> change si nécessaire. Ce qui n'a pas pu être appliqué est **dit** — un preset
> à moitié posé qui se tait donne un son qu'on croit être celui du fichier.
>
> **POSER UN ÉCHANTILLON SE FAIT DANS L'ARRANGEMENT, ET C'EST LÀ QUE C'EST
> POSSIBLE.** Cela avait d'abord été laissé de côté avec ce motif : « cela
> demande de décider quelle piste il devient ET où il commence ». La première
> moitié de la réponse, la liste des pistes la donne ; la seconde n'existe que
> dans l'arrangement, qui sait convertir une abscisse en mesure. Un échantillon
> glissé sur l'arrangement tombe donc sur la piste survolée, **à la mesure
> aimantée** — un trait doré le montre pendant le glisser, parce que poser à
> trois millisecondes du premier temps est le genre de décalage qu'on ne voit
> pas et qu'on entend.
>
> **QUATRE REFUS, ET CHACUN NOMME SA RAISON.** Un projet jamais enregistré n'a
> nulle part où copier le fichier (tous les chemins d'un projet sont relatifs au
> sien, et la lecture refuse même un chemin absolu — D6.4). Une piste MIDI qui
> porte des notes ne devient pas audio en silence : c'est peut-être ce qu'on
> veut, ce n'est jamais ce qu'on veut sans le savoir. Une piste porte UN fichier,
> découpé en clips : un second le remplacerait partout. Et un échantillon
> illisible le dit avec le message du décodeur.
>
> **LE FICHIER EST COPIÉ DANS LE PROJET**, jamais désigné là où il se trouve :
> « enregistrer, c'est aussi emporter les médias » (D6.4), et un projet qui
> pointerait vers la bibliothèque de l'utilisateur serait illisible ailleurs et
> silencieusement incomplet ici. Même nom et même taille : on ne recopie pas.
> Même nom et contenu différent : les deux coexistent, avec un suffixe.
>
> **ET LA LONGUEUR DU CLIP EST LUE DANS LE FICHIER COPIÉ**, pas déduite de ce
> qu'on croit : c'est la même correction que `loadAudioTracks` applique déjà en
> relisant, et pour la même raison — quand la déclaration et le fichier
> divergent, c'est le fichier qui a raison.
>
> **UN PROFIL, LUI, RESTE À TROUVER PLUTÔT QU'À POSER**, et c'est un constat sur
> le modèle : un profil multi-échantillons appartient à une MACHINE
> (`vsm.multisample`), pas à une piste ni à une position. Le poser sur une piste
> qui n'a pas cette machine ne produirait rien, et rien n'expliquerait quoi.
>
> **L'INVENTAIRE EST REFAIT À L'OUVERTURE DE LA FENÊTRE**, jamais en continu :
> un dossier se parcourt en quelques dizaines de millisecondes, et le refaire à
> chaque tour de minuterie ferait travailler le disque pour rien pendant qu'on
> compose.

> **ET UNE PANNE QUE TOUTE LA PHASE D10 A CÔTOYÉE SANS LA VOIR (31/08/2026).**
> Elle n'a pas été trouvée en relisant du code : elle a été trouvée en lançant
> l'application pour vérifier que D10.1 démarrait encore. La fenêtre de
> récupération s'est ouverte — D10.4 faisait son travail —, et le
> `project.json` qu'elle proposait contenait ceci :
>
> ```
> "EQ Mid Q": 0,8,
> "Limiter Ceiling": -0,3,
> ```
>
> **Des virgules décimales. Ce n'est pas du JSON**, et le lecteur du projet
> refusait les trois sauvegardes automatiques présentes sur le disque. La
> promesse de D10.4 — « tuer l'application ne perd pas plus d'une minute » —
> était donc fausse dans les grandes largeurs : la sauvegarde écrivait
> fidèlement toutes les trente secondes un fichier que le bouton « Récupérer »
> n'était pas en mesure de relire. Le mécanisme entier fonctionnait ; ce qu'il
> produisait était inutilisable.
>
> **LA CAUSE N'EST PAS DANS CE QUE LE PROGRAMME FAIT, MAIS DANS CE QU'ON FAIT
> POUR LUI.** `snprintf("%g")` et `strtod` consultent `LC_NUMERIC`. Un
> programme C++ n'installe aucune locale de lui-même — mais JUCE en installe
> une : `juce_SystemStats_linux.cpp` appelle `setlocale(LC_ALL, "")`, garde ce
> que cet appel RENVOIE, et le « restaure ». Or `setlocale` renvoie la
> NOUVELLE locale. La restauration réinstalle donc ce qu'elle devait défaire,
> et le processus reste dans la locale de l'environnement pour le reste de sa
> vie. Sur une machine réglée en français, tout nombre fractionnaire écrit par
> l'application porte une virgule.
>
> **TROIS SYMPTÔMES, ET LE TROISIÈME EST LE SEUL QUI COMPTE VRAIMENT**, mesurés
> contre la vraie bibliothèque et non déduits :
>
> | | ce qu'il écrit dans le fichier | relit son propre fichier | lit un fichier VALIDE, écrit `0.8` |
> |---|---|---|---|
> | locale C — celle des tests | `0.8` | la bonne valeur | la bonne valeur |
> | après le `setlocale` de JUCE | `0,8` | **refusé** | **zéro, en silence** |
>
> La dernière colonne est celle qu'aucun message d'erreur n'aurait donnée. Les
> `project.json` de la chaîne d'analyse sont écrits en Python, donc avec des
> points, sur n'importe quelle machine : l'application les chargeait avec TOUS
> leurs paramètres fractionnaires à zéro, **sans un mot**. C'est le projet que
> D9.3 ouvre à la fin d'une reconstruction, et c'est aussi tout preset, tout
> profil multi-échantillons et tout fichier d'associations MIDI.
>
> **CE QUI A ÉTÉ CORRIGÉ, ET POURQUOI CE N'EST PAS UNE RUSTINE.** On aurait pu
> remplacer le séparateur après coup, ou forcer la locale au démarrage — deux
> corrections qui tiennent tant que personne n'ajoute un appel. `std::to_chars`
> et `std::from_chars` sont définis en locale C, toujours, par la norme : la
> question ne se pose plus. La forme produite est exactement celle de
> `printf("%.*g")` en locale C, donc **aucun fichier déjà écrit par une chaîne
> saine ne change d'un octet** — un test le verrouille, parce qu'une correction
> qui ferait bouger tous les fichiers du dépôt coûterait plus qu'elle ne
> rapporte.
>
> La règle est écrite UNE fois, dans `interchange/NumberText.h` : **un nombre
> qui traverse une frontière — un fichier, une ligne de commande, un tube — se
> lit en locale C.** Les sept autres points d'entrée qui l'ignoraient la
> suivent, dont `vsm-render` (où `--duration 0.5` devenait une erreur
> d'utilisation) et l'outil qui publie la latence mesurée de D3.6.
>
> **UNE EXCEPTION, ÉCRITE PLUTÔT QUE SUBIE** : `paramsTextToValue` de
> l'adaptateur CLAP garde la locale du processus. Ce texte-là ne traverse rien
> — il est affiché par l'hôte à un être humain et retapé par lui. Un
> utilisateur français qui tape « 0,5 » doit obtenir un demi.
>
> **POURQUOI 1 213 TESTS N'ONT RIEN VU, ET CE QUI CHANGE.** Ils tournent en
> locale C, où la panne n'existe pas. Les trois nouveaux en installent une
> exprès ; et sur une machine qui n'en a aucune d'installée, ils DISENT que le
> contrôle n'a pas eu lieu au lieu de se compter comme réussis — un test qui se
> tait quand il n'a rien pu vérifier est pire qu'un test absent, puisqu'il
> laisse croire que la vérification existe. Ils ont été éprouvés dans les deux
> sens : les trois échouent sans la correction.
>
> **CE QUE ÇA COÛTAIT VRAIMENT, MESURÉ SUR LES RECONSTRUCTIONS DÉJÀ RENDUES.**
> Le paragraphe ci-dessus dit que les nombres fractionnaires d'un fichier
> valide se lisaient comme zéro. Ce n'est pas une tournure : l'ancien lecteur a
> été recompilé tel quel (`git show bab0ad0^:interchange/src/Json.cpp`) et
> lâché sur les presets réellement écrits par la chaîne d'analyse.
>
> | reconstruction | paramètres de patch lus comme ZÉRO |
> |---|---|
> | `children-dream` | **60 sur 76** |
> | `children-dream-v10` | **120 sur 166** |
>
> Sur `track_01` de la v12 — une corde — `string.bowPressure` valait 0,5 et se
> lisait 0 : **l'archet appuie avec une force nulle**, la corde ne sonne pas.
> `string.bodySize` 0,956 → 0, `output.drive` 0,108 → 0. Douze des quinze
> paramètres de cette machine, et les trois survivants sont ceux qui valaient
> un entier.
>
> **Autrement dit, le projet que le DAW ouvrait n'était pas celui que la chaîne
> avait mesuré.** Les distances publiées sont, elles, intactes — elles sont
> calculées en Python à partir de rendus faits par `vsm-render`, qui tourne
> sans interface, donc en locale C, et n'a jamais été touché. Ce qui divergeait
> n'est pas la mesure : c'est ce qu'on entendait en ouvrant le résultat. Il
> **ET LA BASCULE EST DATÉE, CE QUI RETIRE LA DERNIÈRE RÉSERVE.** On pouvait
> espérer qu'elle survenait tard, et qu'un projet ouvert tôt se lisait juste.
> Le déclencheur est `juce_TextLayout.cpp` : la mise en page d'un
> `AttributedString` demande `SystemStats::getUserLanguage()`, donc appelle
> `getLocaleValue`, donc bascule la locale — **au premier texte que
> l'application dessine**, avant que qui que ce soit ait pu cliquer. Il n'y a
> pas de fenêtre de tir : dans une session graphique, tout se lit après.

> **LA LEÇON, ET C'EST LA MÊME QUE CELLE DE D7.5.** Une phase entière a été
> déclarée terminée pendant que la sauvegarde automatique écrivait des fichiers
> illisibles, et rien dans le code, les tests ou les rapports ne le disait.
> Ce qui l'a dit, c'est d'avoir lancé le binaire et regardé la fenêtre.

---

### Phase D11 — L'audit après les onze phases : ce qu'un musicien cherche encore (03/09/2026)

**Pourquoi une douzième phase.** Les onze phases ont répondu aux cinq
critères du § 2, et la question « peut-on travailler là-dedans ? » a une
réponse. Mais elle a été posée par la feuille de route, pas par un
utilisateur de Cubase ou de Live qui s'assoit devant l'écran ; celui-là
cherche des gestes précis, et quelques-uns manquent encore. Un audit
poste par poste, vérifié dans le code (pas d'après le manuel), en donne
la liste. Il a aussi dit faux deux fois — la chaîne latérale et la
compensation de latence existent (D4.4, D4.5, `ProcessGraph.cpp`) — et
ces deux-là sont rayés.

L'ordre suit le § 3 : le geste quotidien d'abord, le confort ensuite, le
chantier lourd en dernier.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D11.1 | **Le clip change de piste** : `mouseDrag` calcule la piste sous le curseur et l'ignore (`juce::ignoreUnused(piste)`) ; le déplacement ne touche que le temps | glisser un clip sur une autre piste l'y pose — avec les notes que sa fenêtre couvre, puisqu'un clip est une fenêtre sur le matériau de SA piste (D1) ; un clip audio ne change de piste que si elle porte le même fichier ou aucun, et le refus est dit ; annulable ; test — **fait** |
| D11.2 | **Sélection au lasso** dans l'arrangement, et « tout sélectionner » (Ctrl+A n'y est câblé que pour les notes) | un rectangle tiré sur le vide sélectionne les clips qu'il touche ; Ctrl+A prend tous les clips — **fait** |
| D11.3 | **Se repérer en musique** : la position du transport dit minutes:secondes et un tick brut, jamais « mesure 33, temps 2 » ; l'arrangement ne suit pas la tête de lecture (le piano roll, si) ; ni retour à zéro ni marqueur suivant/précédent au clavier | mesure · temps affichés à côté du temps ; l'arrangement défile derrière la tête quand elle sort de l'écran, bouton comme au piano roll ; Début, marqueur suivant/précédent dans la table des raccourcis — **fait** |
| D11.4 | **Renommer et colorer un clip** : `Clip::name` et `Clip::colorRgba` existent et aucune vue ne les édite (la couleur est toujours celle de la piste) | double-clic sur le nom, couleur au menu ; sauvegardés (déjà dans le format) — **fait** |
| D11.5 | **Dupliquer une piste** (seules les sélections se dupliquent) ; **canal MIDI éditable** (`t.channel = n % 16` à la création, étiquette non éditable) | une commande au menu de piste, tout copié (instrument, effets, notes, clips, automation, routage) ; le canal se saisit — **fait** |
| D11.6 | **Fichiers récents**, **plein écran**, **modèle de projet** (« Enregistrer comme modèle », « Nouveau depuis le modèle ») | menu Fichier ; F11 ; un modèle rouvert est un projet neuf sans chemin — **fait** |
| D11.7 | **S'entendre** : l'entrée audio n'est jamais recopiée vers la sortie pendant l'armement ; **le clavier d'ordinateur** ne joue pas de notes | écoute d'entrée commutable par piste armée (latence dite) ; une rangée de touches joue la piste choisie, octave réglable — **fait** (l'écoute est globale, pas par piste : voir la note) |
| D11.8 | **Étirement temporel d'un clip audio** — le choix n° 3 du § 4 le refusait ; `Clip::sourceStartSeconds` le dit en toutes lettres | à trancher au moment de l'écrire, chiffres à l'appui : un étirement de qualité (vocodeur de phase ou WSOLA) coûte un chantier entier, et la reconstruction n'en a pas besoin — c'est un besoin de production, pas de mesure. Dernier, et seulement après D11.1 à D11.7 |

> **D11.1 ET D11.2 SONT FAITES (03/09/2026).** Le geste existait à moitié :
> `mouseDrag` calculait la piste sous le pointeur et l'ignorait. Le
> changement de piste est RELATIF au dernier pas, comme le temps, et il
> passe par `moveClipsAcrossTracks` (core, 3 tests) : **les notes que la
> fenêtre couvre suivent le clip**, aux mêmes ticks de matériau — c'est la
> seule lecture cohérente du modèle de la région (D1), et son prix est dit
> dans l'en-tête : un autre clip de la piste cible qui couvre ces ticks les
> verra aussi. La figure garde sa forme aux bords de la liste (le décalage
> est réduit pour tous, règle de `moveClips`). Un clip audio ne va que vers
> une piste audio du même fichier, ou vide (qui l'adopte) ; le reste est
> compté pendant le geste et DIT au relâchement, en une fenêtre, jamais
> avalé. Le lasso part du vide (Maj l'ajoute), Ctrl+A prend tous les clips,
> et l'arrangement s'est ouvert sur les neuf pistes de *usandthem-parite-v3*
> avec ce code (capture).

> **D11.3 EST FAITE (03/09/2026).** La barre de transport demande « mes. 33 ·
> 2 » à l'application (un fournisseur ; elle ne connaît pas le projet), et
> l'affiche sous le temps. L'arrangement suit la tête PAR PAGES, la règle
> qu'avait fixée le piano roll (un fond qui glisse à chaque image fatigue) ;
> `F` bascule, et la règle écrit « suit » — un réglage qu'on bascule au
> clavier doit se voir (D5). Trois commandes de plus dans la table des
> raccourcis, donc dans la page imprimable : Début (`Début`), marqueur
> suivant (`Maj+N`), précédent (`Maj+B`, avec une noire de tolérance pour
> qu'un second appui remonte bien au marqueur d'avant). Vu à l'écran.
>
> **Et une faute de conduite, dite** : le commit de D11.1 annonçait une
> capture faite « avec ce code » alors que l'application ne compilait pas
> (une concaténation `juce::String + char8_t*` ambiguë dans le message de
> refus) et que la capture venait du binaire précédent — le filtre de la
> sortie de compilation ne montrait que les avertissements. Corrigé ici ;
> désormais la ligne « Built target » est exigée avant toute capture.

> **D11.4 EST FAITE (03/09/2026).** Double-clic : une fenêtre demande le
> nom. Clic droit : Renommer, Couleur (le sélecteur de JUCE, en direct sur
> le clip, un seul pas d'annulation par ouverture — la règle de la couleur
> de piste), Couleur de la piste (la reprendre), Rendre muet — sur toute
> la sélection, car six clips pris au lasso font un geste. Les fenêtres
> restent dans l'application, la vue demande par deux rappels ; le format
> portait déjà `name` et `color` (ProjectDocument.cpp), rien à migrer.

> **D11.5 EST FAITE (03/09/2026).** `duplicateTrack` vit dans `core/`
> (2 tests) parce que c'est une règle du modèle : tout est copié, les
> identifiants de notes et de clips sont NEUFS (deux notes du même
> identifiant sur deux pistes feraient agir un geste sur l'autre piste), et
> les routages vers un groupe situé après l'original sont réparés comme
> pour `moveTrack`. L'état VIVANT de l'instrument n'est pas dans le modèle
> (D0.1) : l'application le recopie de l'original à la copie après avoir
> reconstruit le graphe, état natif compris. Le canal MIDI se saisit dans
> la liste des pistes (1 à 16, tout autre texte rend l'ancien).

> **D11.6 EST FAITE (03/09/2026).** Dix projets récents dans le fichier de
> préférences (écrit dès l'ouverture ou l'enregistrement, comme l'échelle) ;
> un dossier disparu reste listé, grisé, marqué « introuvable » — le
> retirer en silence ferait chercher où il est passé. Le modèle est UN
> dossier de projet dans le dossier des préférences : « Enregistrer comme
> modèle » y écrit le projet courant sans changer son chemin, « Nouveau
> depuis le modèle » le charge et EFFACE le chemin, si bien que Ctrl+S
> demande où et que le modèle ne s'écrase que par la commande qui le
> nomme. Plein écran : Affichage et F11, coché quand il l'est.

> **D11.7 EST FAITE (03/09/2026), avec un écart dit.** L'écoute d'entrée
> est GLOBALE (Enregistrement ▸ Écouter l'entrée en direct), pas par piste
> armée : le moteur n'a qu'une entrée physique, et la recopier vers la
> sortie une fois ou une fois par piste armée rendrait le même son — sauf
> à passer par les inserts de la piste, ce qui ajouterait la latence d'un
> bloc au chemin et n'est pas ce qu'on demande quand on veut s'entendre.
> Elle s'ajoute à la sortie dans le rappel audio, sans allocation, jamais
> par défaut (entendre son micro dans ses enceintes sans l'avoir voulu,
> c'est un larsen), et l'intitulé nomme la latence du périphérique que
> « Mesurer la latence » chiffre. Le clavier d'ordinateur (Affichage) suit
> la disposition de Live — rangée du milieu pour les blanches, rangée du
> dessus pour les noires, Z/X pour l'octave — et passe par le MÊME chemin
> qu'un clavier MIDI (piste choisie ou armées, capture si l'enregistrement
> tourne). Actif, il emprunte les lettres aux raccourcis ; la répétition
> d'une touche tenue ne rejoue pas la note, et le relâchement l'éteint.

> **D11.8 EST TRANCHÉE (03/09/2026) : REPORTÉE, et voici les chiffres.**
> Un étirement temporel digne de Live (warp) ou de Cubase (élastique) est
> un vocodeur de phase ou un WSOLA avec détection de transitoires, un banc
> de mesure (hauteur conservée au cent près, durée exacte à l'échantillon,
> transitoires non doublés) et une interface de marqueurs de warp : de
> l'ordre de D2 entière, pas d'une étape. La reconstruction n'en a aucun
> besoin — ses pistes audio sont des reports d'un original qui a déjà le
> bon tempo — et aucune mesure du dépôt ne le demande. Il entrera comme
> phase D12, avec son cahier des charges, une fois les campagnes de la
> parité closes et les cinq machines de la branche fusionnées : c'est
> l'ordre du § 3 (ce qui s'appuie sur le modèle, après le modèle), et
> `Clip::sourceStartSeconds` continue de dire en toutes lettres pourquoi un
> clip audio ne suit pas le tempo.
>
> **Le cahier des charges de D12 est écrit (04/09/2026)** :
> `docs/CDC-etirement-temporel.md` (sur la branche `machine-mandoline`,
> fusionnée après la campagne 5) — trois modes par clip (éteint, hauteur
> conservée, rééchantillonné), des marqueurs entre le fichier et la grille,
> un WSOLA à verrouillage de transitoires écrit dans le dépôt (le vocodeur de
> phase attend un chiffre), un noyau de rééchantillonnage fenêtré qui
> remplace l'interpolation linéaire de D2.3, un banc de neuf mesures écrit
> avant la première, et un critère de phase sur *Sky and Sand* (+10 % de
> tempo, la voix à ≤ 10 ms sur huit mesures). Il s'écrit le jour où sa
> condition se réalise : la campagne 5 fusionne les machines et clôt les
> campagnes de la parité.
>
> **D12.1 À D12.7 SONT FAITES ET MESURÉES (04/09/2026), le jour même.** Le
> noyau sinc de Kaiser (64 points, choisis au banc : 10⁻⁵ à 20 kHz là où
> l'interpolation linéaire de D2.3 était à 10⁻¹ dès 10 kHz), le WSOLA à
> verrouillage de transitoires (0,0 cent, 0,0 % de flottement, seize
> attaques à 0,98 ms, blocs de 256 = blocs de 4 096 au bit près), le
> détecteur d'attaques, le modèle (mode et marqueurs relatifs, couper et
> rogner transportent la carte), le format (version 3 seulement si un clip
> s'en sert), le moteur (`ProcessGraph` intact, rapport un au bit près,
> ×2 à 220 Hz conservés, rééchantillonné à 110 Hz), l'interface (sous-menu
> du clip, « N mesures », marqueurs saisissables, forme d'onde en temps
> étiré, vu à l'écran) et le critère de phase sur la voix de *Sky and Sand*
> à +10 % de tempo : **−8 ms sur huit mesures, sans dérive**, contre une
> voix ailleurs (r = 0,33, 13,8 s de dérive) sans D12 ; 14 ms sur la pire
> mesure prise seule, chiffre publié et légué à D12.8. Les chiffres, les
> attendus réécrits et les deux pannes trouvées par la mesure (un ordre
> d'évaluation en C++, un rendu hors ligne qui exportait un clip calé sans
> son calage) sont dans `docs/CDC-etirement-temporel.md`.
>
> **D12.8 AUSSI, LE JOUR MÊME : le vocodeur de phase, et il est le défaut.**
> Écrit sur le chiffre que D12.7 lui léguait (14 ms de flottement de grain
> sur la pire mesure), avec verrouillage de pics et phases remises à zéro
> aux transitoires ; même banc que le WSOLA, tenu (0,0 cent, seize attaques
> à 0,98 ms, partiels à 0,0 %, blocs au bit près), et le critère de phase
> refait par le même script : **−2 ms sur huit mesures, 4 ms sur la pire**,
> contre −8 et 14 au WSOLA — qui reste dans le dépôt comme témoin, une
> option de clip écrite dans le projet.

Ce que l'audit a trouvé et qui n'entre PAS ici, avec la raison : la
sélection de notes par vélocité (le filtre existe sous forme d'opérations
sur la sélection, le tri par vélocité s'ajoutera au piano roll quand une
main le demandera) ; les lanes de pitch bend et d'aftertouch (le format
les porte, la vue CC les montrera avec le même composant — une extension
de `MidiCcComponent`, pas une phase — **faite le jour même** : deux
pseudo-contrôleurs, 128 et 129, le bend dessiné à 7 bits autour de son
centre, un bend enregistré gardant ses 14 bits tant qu'on ne touche pas la
lane) ; l'arpégiateur temps réel (celui du
piano roll écrit des notes, ce qui est le choix D0 : pas d'effet MIDI qui
mentirait sur ce qui est écrit) ; l'historique d'annulation visible — **fait le jour même** (Affichage ▸
Historique des modifications : chaque pas, l'état courant marqué, ce que
Rétablir rendrait ; un clic sur un pas y revient par le même chemin que
Ctrl+Z) — et la palette de commandes (agréable, sans geste quotidien
derrière).

### Phase D13 — Le second audit : ce qui manque encore une fois D12 posée (04/09/2026)

**Pourquoi.** D11 avait audité les gestes quotidiens ; D12 a posé le suivi
de tempo, et un clip qui suit le tempo appelle des gestes qui n'existaient
pas avant lui. Le même audit, refait dans le code, trouve six absences —
dont une qui MENT, et qui passe donc en premier (règle 1 du § 3).

| Étape | Contenu | Terminé quand |
|---|---|---|
| D13.1 | **Deux clips audio qui se chevauchent S'ADDITIONNENT** : `mixInto` somme toutes les portées, et une prise posée sur la fin d'une autre double le son sur le chevauchement — ce qu'aucun DAW ne fait. Cubase et Live y mettent un fondu enchaîné | sur le chevauchement, le premier clip s'éteint et le second monte, linéairement, la somme restant à un ; la région se voit hachurée dans l'arrangement ; test moteur : deux clips d'un même signal qui se chevauchent d'une seconde jouent à niveau constant |
| D13.2 | **Étirer un clip audio à la souris** : le geste de D12 manque — tirer le bord droit d'un clip étiré ne fait que le prolonger (la carte se prolonge, le matériau continue) | Ctrl tenu, tirer le bord droit ÉTIRE : le dernier marqueur suit le bord, le mode passe en « hauteur conservée » s'il était éteint ; la règle dit ce que fait le bord ; annulable ; test `core/` |
| D13.3 | **Insérer ou supprimer une plage de temps** sur tout le morceau (l'outil Plage de Cubase) : retirer une mesure d'un arrangement déplace aujourd'hui piste par piste | Édition ▸ Insérer du silence à la tête de lecture / Supprimer la sélection de temps : clips, notes, automation, marqueurs, tempo et mesures de TOUTES les pistes glissent ensemble ; ce qui est à cheval est coupé ; test `core/` |
| D13.4 | **Un clip audio à l'envers** (cymbale, traîne inversée) : `Clip` n'a pas de sens de lecture | menu du clip « À l'envers » ; le moteur lit le fichier à rebours sur la fenêtre du clip ; la forme d'onde se dessine à l'envers ; sauvegardé ; test moteur (la lecture inversée d'une rampe est une rampe descendante) |
| D13.5 | **La saisie pas à pas** dans le piano roll (Cubase) : un clavier — d'ordinateur ou MIDI — pose des notes à la position d'insertion, qui avance d'un pas de grille à chaque note, sans que le transport tourne | un bouton de la barre du piano roll l'arme ; chaque note reçue s'écrit à la position, de la longueur de la grille ; Entrée avance sans note (un silence), Retour arrière recule ; vu à l'écran |
| D13.6 | **Normaliser un clip** : le gain existe, personne ne le calcule | menu du clip « Normaliser » : le gain devient 1 / crête du matériau joué ; dit dans le gain du clip, annulable |
| D13.8 | **Trois effets d'insert qu'une tranche de Cubase ou Live a et que le parc n'avait pas** : la forme (transient shaper), le mouvement (trémolo / auto-pan), la hauteur en temps réel (pitch shift) — l'audit D11 s'était arrêté aux gestes, pas aux inserts | dans `EffectFactory`, l'onglet Effets les propose (l'interface est générique) ; chacun mesuré sur son trait, empreinte, identités |
| D13.7 | **Adopter le tempo du clip** : « N mesures » déduit le tempo d'origine d'une boucle et l'affiche — mais le projet reste à son tempo, et la boucle joue étirée. Le geste inverse manque : caler le PROJET sur la boucle (Live : « Set 1.1.1 here » et le tempo de la boucle ; Cubase : « Set Tempo from Event ») | la fenêtre du tempo déduit propose « Adopter ce tempo pour le projet » : le changement de tempo au tick 0 prend cette valeur, la boucle joue alors au rapport un (le court-circuit), les autres changements de tempo restent ; annulable |

L'ordre suit le § 3 : ce qui ment (D13.1) avant ce qui manque ; le geste
de D12 (D13.2) et l'arrangement global (D13.3) avant le confort.

> **D13.1 EST FAITE (04/09/2026).** La règle vit dans `spansFromTrack`, là
> où les clips deviennent des portées : sur un chevauchement, le premier
> reçoit un fondu de sortie et le second un fondu d'entrée de la longueur du
> chevauchement (le plus long des deux si un fondu réglé l'était déjà). Le
> test joue deux clips d'un fichier CONSTANT qui se chevauchent d'une
> seconde : le niveau reste à 0,5 partout, au bit près (pire écart 10⁻⁵),
> là où l'addition donnait 1,0 sur la seconde commune. Le moteur n'a pas
> changé d'une ligne — `mixInto` applique les fondus qu'il appliquait déjà.
> L'arrangement hachure la zone de chevauchement sur chaque clip concerné.
>
> **D13.2 EST FAITE (04/09/2026).** `stretchClipsEnd` vit dans `core/` : la
> durée jouée change, les marqueurs glissent en proportion (le calage
> relatif est gardé, le dernier suit le bord, deux marqueurs ne se
> confondent jamais, jamais sous un tick), et un clip qui ne suivait pas le
> tempo reçoit sa paire neutre et passe en « hauteur conservée ». Test : un
> clip de deux secondes tiré à 2 880 ticks joue les MÊMES deux secondes (le
> milieu de la carte est à 4,0 s pour un matériau de 3,0 à 5,0 s). Dans la
> vue, Ctrl sur le bord droit d'un clip audio étire — un modificateur, pas
> un outil, la même raison qu'Alt pour couper — et le curseur le dit avant
> le clic.
>
> **D13.3 EST FAITE (04/09/2026).** `insertTime` et `deleteTime` vivent
> dans `core/` (`TimeEdit.h`) : tout glisse ensemble — notes, clips (par
> `splitClips`, qui sait couper une fenêtre en secondes et une carte de
> tempo), contrôleurs MIDI, automation, repères, tempo, mesures, boucle et
> punch ; ce qui est à cheval est coupé à l'insertion et raccourci de ce
> qu'il avait dedans à la suppression ; l'entrée au tick 0 du tempo et de
> la mesure ne bouge jamais. Trois tests. Dans l'application, la plage est
> celle des LOCATEURS (la région de boucle), comme dans Cubase : Édition ▸
> Insérer du silence entre les locateurs (Ctrl+Maj+I) et Supprimer le
> temps entre les locateurs (Ctrl+Maj+K), dans la table des raccourcis donc
> dans la page imprimable ; annulable.
>
> **D13.4 EST FAITE (04/09/2026).** `Clip::reversed`, écrit dans le projet
> (`reversed`, et la version 3 comme le suivi de tempo : un lecteur ancien
> jouerait le clip à l'endroit sans un mot). Le moteur n'apprend rien de
> nouveau : la portée lit un MIROIR du magasin de la piste
> (`MirroredSampleStore`, la trame i est la trame N − 1 − i, la diffusion
> depuis le disque reste diffusée), sa fenêtre convertie une fois à la
> publication ; un clip étiré et à l'envers retourne sa carte sur ses deux
> axes. Test : une rampe lue à l'envers est une rampe qui descend, à la
> trame près (pire écart 10⁻⁷), étirée ×2 aussi. Menu du clip « À
> l'envers » (sur la sélection, chacun le sien), la forme d'onde se dessine
> à l'envers.
>
> **D13.6 EST FAITE (04/09/2026).** Menu du clip « Normaliser (gain =
> 1 / crête) » : la crête du matériau joué vient du cache d'aperçu, qui
> garde les extrêmes de chaque tranche de 256 trames — exactement ce qu'il
> faut, déjà là, sans relire le fichier — et le gain du clip devient son
> inverse ; le silence ne se normalise pas ; annulable, et le gain se voit
> dans la forme d'onde comme tout gain de clip.
>
> **D13.5 EST FAITE (04/09/2026).** Un bouton « Pas à pas » dans la barre
> du piano roll arme le moteur applicatif ; armé, chaque note reçue sur son
> entrée MIDI — un clavier MIDI comme le clavier d'ordinateur, qui passe
> par le même chemin — est POSTÉE au fil d'interface (on est sur le thread
> MIDI, le projet ne s'y touche pas) et le piano roll l'écrit à la tête de
> lecture, de la longueur de la grille, puis avance la tête d'un pas ;
> Entrée avance sans note, Retour arrière recule ; le son continue de
> passer, on s'entend en saisissant. La tête de lecture EST la position
> d'insertion : elle se voit, elle se déplace au clic sur la règle. Une note
> par pas — l'accord se pose par le bouton Accord — et c'est dit.
>
> **D13.7 EST FAITE (04/09/2026).** La fenêtre du tempo déduit par « N
> mesures » propose désormais « Adopter ce tempo pour le projet » : le
> changement de tempo au tick 0 prend la valeur déduite (les autres
> restent), le transport et les pistes audio se republient, et la boucle
> joue au rapport un — le court-circuit de l'étireur, pas un bit de
> différence avec le fichier. Annulable comme tout ce qui passe par
> `beginProjectEdit`.
>
> **D13.8 EST FAITE (04/09/2026), et deux de ses bancs ont eu une leçon à
> donner.** `TremoloEffect` (LFO sur le gain, sinus → carré par une tangente
> hyperbolique, phase stéréo 0 = trémolo, 180° = auto-pan : à 4 Hz et
> profondeur 1, la gauche va de 0,015 à 0,343 et seize fenêtres sur
> trente-deux sont en opposition gauche-droite). `TransientShaperEffect`
> (deux suiveurs, 1/20 ms et 30/200 ms, leur différence est l'attaque : Attack
> +1 fait ×4 sur les cinq premières ms d'une note et laisse la tenue à
> +1,5 % ; Sustain −1 la ramène à 0,36) — la première version faisait +12 %
> sur la tenue, parce que le suiveur lent ne montait que sous les crêtes de
> la sinusoïde redressée : les deux suiveurs lisent désormais une enveloppe
> lissée à 2 ms. `PitchShiftEffect` (deux têtes sur une ligne de retard,
> fenêtres en demi-sinus, la recette de l'H910 ; latence déclarée d'un
> demi-grain) : mesuré sur si♭3 transposé d'une octave, **466,1 Hz pour
> 466,2 attendus**, reste à 233 Hz 0,011, battement du grain 2,9 % — après
> deux leçons : à 220 Hz, un grain de 50 ms fait onze périodes tout rond et
> les deux têtes tombaient en opposition de phase exacte (la porteuse
> s'annulait : « la note du banc ne doit pas diviser le grain », comme pour
> la machine à séquence) ; et sans alignement, la tête qui redémarre
> reprenait la source 5,83 périodes plus tôt, un saut de 0,17 tour toutes
> les 25 ms que la transformée lisait comme +25 cents (473,1 Hz). La tête
> qui redémarre cherche donc, à ± 8 ms, le décalage qui la met en phase avec
> l'autre — la recherche du WSOLA, une fois par grain. Le parc passe à
> **16 effets d'insert**.
>
> **ET UNE CASE QUI S'ÉTAIT MISE À MENTIR, rattrapée le même jour.** Depuis
> que la parité est le défaut de la chaîne (CDC multipiste § 8, 04/09), la
> case « Reconstruire en visant la parité des pistes » n'ajoutait plus rien
> en étant cochée et ne retirait rien en étant décochée : la reconstruction
> visait la parité dans les deux cas. `ReconstructionChain::commandLine`
> passe désormais `--sans-parite` quand elle est vide et `--parite` quand
> elle est cochée — explicite même s'il est le défaut, pour que la ligne de
> commande se lise sans connaître la date — et l'intitulé dit « (le défaut
> de la chaîne) ». Deux tests corrigés dans le même sens.

### Phase D14 — Le troisième audit : les gestes de tous les jours qui manquaient encore (04/09/2026)

**Pourquoi.** Même méthode que D11 et D13 : un utilisateur de Cubase ou de
Live s'assoit, cherche ses gestes, et l'on vérifie dans le code ce qui
manque. Cinq absences, vérifiées ; l'ordre suit le § 3 — les gestes du
quotidien d'abord, l'import et l'export ensuite, la préférence en dernier.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D14.1 | **Les locateurs sur la sélection** (le `P` de Cubase, `Ctrl+L` de Live) : la boucle ne se pose que sur la règle, à la main | une commande (dans la table des raccourcis) pose la région de boucle sur l'étendue de la sélection — les clips de l'arrangement, ou à défaut les notes du piano roll — et l'active ; les deux vues et le moteur la voient |
| D14.2 | **Zoom sur tout / sur la sélection dans l'arrangement** : « Ajuster à la fenêtre » n'est entendu que par le piano roll, et l'arrangement n'a que +/− | le raccourci ajuste les deux vues ; le menu de l'arrangement offre « Zoom : tout voir » et « Zoom : la sélection », comme celui du piano roll |
| D14.3 | **Importer un fichier MIDI sur de nouvelles pistes** : « Ouvrir MIDI… » REMPLACE le projet, et un `.mid` lâché sur la fenêtre n'est pas reçu | Fichier ▸ Importer un MIDI dans le projet… ajoute ses pistes à la suite, à la tête de lecture ; un `.mid` lâché sur la fenêtre fait pareil ; annulable ; test `core/` sur la fusion des pistes |
| D14.4 | **Le dither à l'export 16 et 24 bits** : l'export tronque, et une queue de réverbération à −80 dB devient une distorsion de quantification — Cubase et Live dithérisent | un dither TPDF (± 1 LSB triangulaire) à l'écriture des formats entiers, actif par défaut, éteint par option ; test : un sinus à −90 dB exporté en 16 bits garde un spectre sans harmoniques de quantification (dit en chiffres) |
| D14.5 | **Retour au début à l'arrêt** (préférence de Cubase ; c'est le défaut de Live) : Stop laisse la tête où elle est, et il faut `Début` ensuite | une préférence, retenue, qui ramène la tête à la position de départ de la lecture quand on arrête |

> **D14.1 ET D14.2 SONT FAITES (04/09/2026).** « Locateurs sur la
> sélection » (`P`, table des raccourcis, menu Édition) pose la boucle sur
> l'étendue des clips choisis dans l'arrangement, toutes pistes confondues —
> à défaut sur les notes choisies du piano roll — et l'active ; la région
> est posée PARTOUT d'un coup (projet, transport, moteur, les deux vues, le
> bouton Loop) par une seule fonction, qui remplace six lignes recopiées.
> Annulable. Et l'arrangement zoome : « Zoom : tout voir » et « Zoom : la
> sélection » dans son menu, et le raccourci « Ajuster à la fenêtre » vaut
> désormais pour les deux vues — il n'était entendu que par le piano roll.
>
> **D14.3 EST FAITE (04/09/2026).** `appendTracksFrom` vit dans `core/`
> (`ProjectImport.h`) : les pistes de la source s'ajoutent à la suite,
> leurs ticks ramenés à la résolution du projet (un fichier à 960 ppq
> jouerait deux fois trop lentement dans un projet à 480), posées à la tête
> de lecture, avec des identifiants de notes et de clips neufs ; le tempo et
> les mesures du fichier sont IGNORÉS et comptés — le projet garde les
> siens, et l'application le dit. Deux tests. Fichier ▸ Importer un MIDI
> dans le projet…, et un `.mid` lâché sur la fenêtre fait pareil ; annulable.
>
> **D14.4 EST FAITE (04/09/2026), et son banc a dû réécrire sa forme.**
> `WavFileWriter` ajoute un bruit TPDF de ± 1 LSB (deux tirages uniformes
> d'un générateur déterministe à graine fixe : l'export reste reproductible
> octet pour octet) avant l'arrondi des formats entiers, sans effet sur le
> flottant ; `RenderOptions::dither` (vrai par défaut) et `vsm-render
> --sans-dither`. L'attendu disait « un sinus de 1 LSB tronqué est un carré,
> troisième harmonique à un tiers » : l'écrivain ARRONDIT, il ne tronque
> pas, et un sinus d'un LSB arrondi est un escalier à trois niveaux dont la
> troisième harmonique ne vaut que 0,022 — ce sont les cinquième et septième
> qui ressortent (0,186 et 0,151). Le banc juge donc le profil entier,
> harmoniques 2 à 12 : pire harmonique 0,186 sans dither, **0,012 avec**.
>
> **D14.5 EST FAITE (04/09/2026).** Préférences ▸ Audio ▸ « À l'arrêt :
> revenir au point de départ », retenue dans le fichier de préférences. La
> transition se voit sur l'horloge unique, dans le minuteur — quel que soit
> le chemin qui a arrêté le transport (le bouton, la barre d'espace, une
> commande MIDI apprise) : un seul endroit, pas quatre.

### Phase D15 — Le quatrième audit : ce qui manque encore une fois D14 posée (04/09/2026)

**Pourquoi.** Même méthode que D11, D13 et D14, et le même garde-fou : chaque
absence ci-dessous a été VÉRIFIÉE dans le code avant d'être écrite (un
relevé de trente gestes usuels de Cubase et de Live contre `app/Source/`,
`core/` et `interchange/` ; vingt-cinq existaient déjà). L'ordre suit le
§ 3 : le geste de tous les jours d'abord, l'outil de mesure ensuite, le
modèle temporel en dernier parce qu'il traverse tout.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D15.1 | **Contourner un insert** : `TrackEffect` n'a pas d'état « actif », et l'on ne peut comparer avec/sans qu'en supprimant l'effet (Cubase : bouton Bypass ; Live : l'interrupteur du device) | un champ `enabled` par insert, sauvegardé (absent = actif, les projets existants ne changent pas), un interrupteur par rangée dans la chaîne d'effets, et « tous les inserts » d'un coup par piste ; le graphe saute l'effet SANS changer la compensation de latence (un contournement ne doit pas déplacer la piste) ; test `audio/` : contourné = identique à l'absence de l'effet, à la latence près, et le retard reste le même |
| D15.2 | **Déplacer la sélection au clavier dans l'arrangement** : le piano roll a ses flèches, l'arrangement n'a que la souris (Cubase : `Ctrl+←/→` d'un pas de grille ; Live : `←/→`) | `Ctrl+←/→` déplace les clips choisis d'un pas de grille, `Ctrl+Shift+←/→` d'un pas fin, `Ctrl+↑/↓` vers la piste voisine du même genre ; dans la table des raccourcis ; annulable |
| D15.3 | **Un analyseur de spectre** sur le master (Live : Spectrum ; Cubase : SuperVision) : la console montre des niveaux, jamais une répartition | une fenêtre d'analyse, FFT 4096 sur le master, axe des fréquences logarithmique de 20 Hz à 20 kHz, axe des niveaux en dB, retenue en position et taille ; le calcul se fait hors du fil audio, sur une copie ; test : un sinus à 1 kHz sort dans la bonne case, à ± un demi-ton |
| D15.4 | **Des presets d'effet** : les machines ont leurs presets, les seize effets n'en ont aucun, et régler une réverbération se refait à chaque piste | enregistrer / charger un preset nommé par type d'effet, dans le dossier des préférences ; un menu dans la rangée de l'insert ; test `interchange/` : aller-retour disque exact |
| D15.5 | **Les rampes de tempo** : `TempoChange` ne connaît que le palier, et un ralentissement se fait en marches d'escalier (Cubase : courbe de tempo ; Live : automation du tempo) | un drapeau « rampe jusqu'au suivant » par changement de tempo ; `TempoMap` intègre la rampe exactement (forme close, pas de pas fixe) dans les deux sens ; l'export MIDI la rend en paliers d'une noire et le dit ; la voie de tempo la dessine ; tests `core/` : aller-retour ticks ↔ secondes à 1 µs près, et une rampe de 120 à 60 sur quatre mesures dure ce que la formule dit |

> **D15.1 EST FAITE (04/09/2026), avec le choix de Cubase plutôt que celui
> de Live.** Contourner n'éteint pas : l'insert continue de tourner (queue de
> réverbération, mémoire de delay, enveloppe de compresseur), déclare la
> même latence, et seule sa sortie est remplacée par le signal sec retardé
> d'exactement cette latence (`BypassableEffect`, un enrobage ; le drapeau
> est atomique, la chaîne n'est ni reconstruite ni republiée, donc aucun
> clic). Deux raisons : la compensation de latence de la piste ne bouge pas
> -- un « avec / sans » qui déplace la piste compare autre chose --, et le
> retour est sans transitoire. Le prix, un effet qui calcule pour rien, est
> celui que Cubase paie aussi. Le banc l'a mesuré sur les seize effets à 60 %
> de chaque réglage : contourné = sec retardé de la latence, écart 0 ; remis
> après un tiers de contournement = jamais contourné, écart 0 ; latence du
> graphe 1 648 échantillons dans les deux états. Le premier passage a trouvé
> un défaut réel : la latence du pitch shift est la moitié de son grain, un
> RÉGLAGE posé après `prepare`, et une ligne dimensionnée à `prepare` lisait
> 1 200 pour 1 632 (écart 0,93) -- la latence se lit donc à chaque bloc, et
> la ligne est dimensionnée large (un huitième de seconde au moins). Dans le
> fichier : `"enabled": false` seulement quand c'est le cas, les projets
> existants ne changent pas d'un octet (test d'aller-retour). Dans la vue :
> « On / Off » en tête de chaque rangée, le nom grisé quand contourné,
> « Contourner tout / Tout remettre » pour la piste ; annulable ; l'export
> hors ligne lit le même drapeau. Le garde-fou d'allocation monte désormais
> un insert sur deux contourné.

> **D15.2 EST FAITE (04/09/2026), et elle a pris la convention du piano
> roll plutôt que celle du tableau ci-dessus.** Le tableau disait
> `Ctrl+←/→`, « dans la table des raccourcis » ; le piano roll avait déjà
> tranché le contraire pour ses flèches (D10) : leur sens EST leur
> direction, elles ne se reconfigurent pas, et la page des raccourcis les
> liste comme fixes. Deux vues du même morceau ne demandent pas deux gestes
> pour la même chose -- c'est la règle écrite en tête de `keyPressed` de
> l'arrangement. Donc : `←`/`→` déplacent les clips choisis d'un pas
> d'aimantation (la mesure, ou la grille fine selon `G` -- le même pas qu'à
> la souris), `Maj` en fait quatre ; `↑`/`↓` les passent à la piste voisine
> par `moveClipsAcrossTracks`, notes comprises, refus comptés et dits ; sans
> sélection, `←`/`→` font défiler. Annulable. Le pas fin de `Ctrl+Maj` du
> tableau n'existe pas : `G` le donne déjà. Vérifié : le déplacement passe
> par les deux fonctions de `core/` déjà testées ; l'application s'ouvre en
> arrangement sur un projet à quatre pistes.

> **D15.3 EST FAITE (04/09/2026), et son banc a corrigé deux fois
> l'échelle.** Affichage ▸ Analyseur de spectre… : une fenêtre flottante,
> FFT de 4 096 points sur la somme mono du bus final, PRISE APRÈS la tranche
> master (ce que l'analyseur voit est ce qui sort), axe des fréquences
> logarithmique de 20 Hz à la moitié de la cadence, axe des niveaux où un
> sinus plein-échelle lit 0 dB, courbe vive et courbe tenue (un demi-dB par
> image), la crête nommée en hertz. Le fil audio ne fait que déposer les
> échantillons dans un anneau sans verrou (`SpectrumTap`, éteint tant que la
> fenêtre est fermée) ; tout le calcul est sur le fil de l'interface, à 25
> images par seconde. L'attendu disait « 1 kHz dans la bonne case à ± un
> demi-ton » : la case est à 996,1 Hz (une case fait 11,7 Hz), et la
> parabole sur trois cases affine à 1 000,18 Hz. Le NIVEAU, lui, a menti
> deux fois : la case seule lit -0,63 dB (la fenêtre de Hann creuse une raie
> qui tombe entre deux cases, jusqu'à -1,42 dB), et le sommet de la parabole
> en dB surcorrige (+0,15 dB à 1 kHz, +0,27 à 440 Hz : la parabole n'est
> pas la forme du lobe). La forme du lobe de Hann, elle, est connue --
> sinc(δ)/(1-δ²) -- et rend 0,06 dB pour le sinus plein-échelle et -59,96
> pour celui à -60. Trois tests. Et une règle de plus pour TOUTES les
> fenêtres flottantes, qui manquait (mémoire « disposition réglable ») :
> `PanelWindow::setDefaultSize` reprend la position et la taille qu'on avait
> réglées, ramenées dans l'écran, et chaque déplacement d'une fenêtre visible
> est retenu sous son titre. `VSM_VUE=spectre,jouer` ouvre la fenêtre et
> lance la lecture, pour la photographier ; vu à l'écran sur le projet à
> quatre pistes, crête à 167,8 Hz.

> **D15.4 EST FAITE (04/09/2026), et elle a choisi la bibliothèque plutôt
> que le dossier des préférences.** Le tableau disait « dans le dossier des
> préférences » ; les presets de MACHINES vivent dans la bibliothèque de
> l'utilisateur (`*.synth.json`, indexés par le navigateur), et deux
> dossiers de presets pour deux sortes de presets seraient deux logiciels.
> Donc : `*.effect.json` (`EffectPreset.h`, format `vsm-effect-preset`
> version 1 -- type de fabrique, réglages en unités réelles sous leur nom
> sémantique, état natif s'il existe ; le contournement n'en fait PAS
> partie, c'est une décision de mixage), écrits dans `<bibliothèque>/effets`
> si la bibliothèque est réglée, sinon `<projet>/effets`, sinon à côté des
> préférences ; lus dans la bibliothèque ET le projet. Chaque rangée de la
> chaîne porte un bouton « Preset » : enregistrer sous un nom, ou charger
> l'un des presets DU MÊME TYPE trouvés (un preset de réverbération n'est
> jamais proposé à un delay). Le navigateur les liste comme une sorte à
> part (« Effet »), et un preset déposé sur une piste y ajoute l'insert
> réglé ; un type que la fabrique ne construit pas est nommé, jamais
> remplacé. Annulable. Quatre tests : aller-retour exact (valeur pour
> valeur, état natif compris), refus par nom d'un preset de machine ou
> d'une version inconnue, et l'index qui distingue `Salle claire.effect.json`
> de `basse.synth.json`. Vu à l'écran : la rangée « On | Reverb | Preset |
> ^ v X ».

> **D15.5 EST FAITE (04/09/2026), en forme close.** `TempoChange` gagne
> `rampToNext` : le tempo glisse linéairement EN BPM contre la position
> musicale jusqu'au changement suivant (la courbe de tempo de Cubase). Sur
> un tronçon de L ticks de b0 à b1, une noire dure 60/b(x) secondes, donc
> s(x) = 60·L/(ppq·(b1-b0))·ln(b(x)/b0), et l'inverse est une exponentielle :
> aucun pas fixe, le milieu d'une rampe est aussi juste que son bout. Tout
> passe par `TempoMap` (le moteur, l'ordonnanceur, l'export, la voie), donc
> tout suit. Mesuré : une rampe de 120 à 60 sur quatre mesures dure
> 16·ln 2 = 11,0904 s (8 s en palier à 120, 16 à 60), 90 BPM au milieu,
> aller-retour exact au tick sur onze positions et à la microseconde en
> secondes. Le fichier MIDI ne connaît que le palier : l'export rend chaque
> rampe en paliers d'une noire dont chacun dure exactement ce que la rampe
> lui donne (durée totale conservée à l'arrondi de la microseconde, 16 pas
> = 17 événements pour l'exemple), et l'application le DIT au moment
> d'exporter, avec le nombre de paliers. Dans le fichier de projet, `"ramp":
> true` seulement quand c'est le cas. Dans la voie de tempo, Ctrl+clic ou
> double-clic sur un point bascule sa rampe, dessinée en pente ; annulable.
> Vu à l'écran : 120 → 60 en pente, puis 140 en palier. Quatre tests.

**Ce que l'audit a écarté, et pourquoi.** Le pré-roll (jouer les mesures
qui précèdent le punch-in) : le décompte existe, et un punch-in se prépare
en posant la tête avant. Le scrub audio : Live ne l'a pas, et la tête posée
à la souris avec la lecture en boucle rend le même service. Le choix de
l'entrée audio par piste : à revérifier avec une carte multi-entrées sous
la main, pas sur un relevé de code.


### Phase D16 — Le cinquième audit : ce qui manque encore une fois D15 posée (04/09/2026, 22:30) — **TERMINÉE (04/09/2026, 23:45)**

**Pourquoi.** Même méthode que D11, D13, D14 et D15, même garde-fou :
quarante-cinq gestes de Cubase et de Live relevés et VÉRIFIÉS un par un
dans `app/Source/`, `core/`, `interchange/` et `audio/` ; trente-sept
existent déjà (dupliquer une piste, geler, marqueurs au piano roll, swing,
humaniser, prises en boucle, fondus, historique, sauvegarde automatique,
export par piste, MIDI learn, automation des inserts…). Huit manquent.
L'ordre suit le § 3 : ce qui MENT d'abord (D16.1 et D16.2 rendent un
projet qui ne dit pas ce qu'il contient), le geste de tous les jours
ensuite, le modèle en dernier. Pendant la campagne S1 du banc synthétique
(04/09 au soir), la règle écrite ici était « seules les étapes qui ne
touchent que `app/` avancent » ; elle était plus stricte que le danger.
Vérifié : `vsm-render` lie `vsm_core` et `vsm_audio` STATIQUEMENT (`ldd`
n'y montre aucun `libvsm_*`), donc bâtir `vsm_core_tests` ou
l'application refabrique les bibliothèques sans remplacer d'un octet le
binaire que la course exécute. La vraie règle, celle du CLAUDE.md, est
donc la seule : jamais de build complet, jamais la cible `vsm-render`
pendant une course — des cibles nommées, et l'on vérifie l'horodatage de
`build/tools/vsm-render` après coup. `core/` et `audio/` sont donc
ouverts, et ce qu'une course mesure reste le moteur qu'elle a trouvé au
départ, ce qui est exactement ce qu'on lui demande.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D16.1 | **Créer un clip dans l'arrangement.** Sur une piste MIDI neuve, des notes écrites au piano roll ne produisent AUCUN clip visible tant qu'on n'a pas sauvegardé et rouvert : la matérialisation n'a lieu qu'à l'ouverture (`MainComponent.cpp:3181-3193`), et `mouseDoubleClick` de l'arrangement ne fait que renommer. Cubase : double-clic entre les locateurs ou crayon ; Live : double-clic sur la piste | `ClipEdit::createClip` (règle du chevauchement, compteur d'identifiants) ; double-clic sur le vide d'une piste crée un clip d'une mesure aimantée, article de menu de piste ; les notes déjà écrites sur la piste se matérialisent quand elles sont écrites, pas à la réouverture ; annulable ; test `core/` : créer un clip de 0 à une mesure sur une piste qui porte des notes → `PlaybackScheduler::build` rend exactement les notes de cette mesure ; sur une piste vide → clip présent, aucun événement |
| D16.2 | **Chasser les contrôleurs à la mise en lecture.** `PlaybackScheduler::build` (`:100-119`) n'émet un CC, un bend, un aftertouch ou un programme que si son tick tombe dans la fenêtre : démarrer au refrain perd la pédale (CC64), le balayage de filtre et le programme posés plus tôt. Cubase : Chase Events, actif par défaut ; Live idem | avant la boucle, `build` émet à `startTick` la dernière valeur ≤ `startTick` de chaque (canal, contrôleur), du bend, de l'aftertouch et du programme, passages compris ; test `core/` : CC74 = 20 au tick 0 et 100 au tick 1920, `build(p, 960, 2880)` commence par `ControlChange{74, 20}` au temps de 960 |
| D16.3 | **Joindre et couper les clips au clavier.** `Ctrl+J` et `Ctrl+E` sont dans la table (`ShortcutTable.cpp:30-31`) mais seul le piano roll les entend ; l'arrangement ne coupe qu'à l'Alt+clic sous le pointeur. Cubase : Colle, « Couper à la position du curseur » ; Live : Consolidate, Split | `ClipEdit::joinClips` (deux clips contigus de la même piste dont la fenêtre source se prolonge fusionnent, sinon refus dit) ; `Ctrl+E` coupe la sélection à la tête de lecture par `splitClips` ; menu du clip ; annulable ; test `core/` : [0,960[ + [960,1920[ contigus → un clip, rendu identique note pour note ; sources disjointes → refus, rien de modifié |
| D16.4 | **Les repères dans l'arrangement.** `Project::markers` n'est dessiné et posé que par la règle du piano roll ; `Maj+N/B` navigue à l'aveugle là où l'on arrange. Cubase : piste de marqueurs ; Live : locateurs de la zone de scrub | la règle de l'arrangement dessine les repères (nom, trait), double-clic sur la règle en pose un, double-clic sur un repère le renomme, clic droit le retire — sur les rappels `onMarkerRequested` / `onMarkerRemoved` déjà écrits ; capture `VSM_VUE=arrangement` d'un projet à trois repères |
| D16.5 | **Verrouiller une piste.** Aucun `locked` nulle part ; depuis D15.2 les flèches déplacent les clips, et une piste de référence finie part d'un coup de flèche. Cubase : cadenas par piste ; Live : verrou du clip figé | `Track::locked` (absent du fichier quand faux), refus CENTRALISÉ dans `ClipEdit` (`moveClips`, `resizeClips*`, `splitClips`, `moveClipsAcrossTracks`, `createClip`, `joinClips`) et dans l'édition de notes, pas dans la vue ; cadenas dans la liste des pistes, clips grisés ; test `core/` : `moveClips` sur une piste verrouillée rend 0 et ne touche pas un tick ; une sélection à cheval ne déplace que la libre, refus compté |
| D16.6 | **Le métronome réglable.** `setMetronomeLevel` existe et n'est appelé nulle part ; l'interface n'a qu'un interrupteur. Cubase : Metronome Setup ; Live : volume et Count-in | niveau retenu dans les préférences et poussé au graphe ; « seulement au décompte » et « seulement à l'enregistrement » branchés sur `clicAudible` ; test `audio/` : niveau 0,25 → crête du clic au quart de celle à 1,0 à 10⁻⁶ près ; le décompte reste audible métronome éteint |
| D16.7 | **Le décalage de piste.** `Track` n'a aucun `delayMs` ; la compensation de latence corrige, elle ne se règle pas. Cubase : Delay dans l'inspecteur ; Live : Track Delay | `Track::delayMs` (absent du fichier quand nul), lu par `ProcessGraph` et `PlaybackScheduler` (notes ET audio), saisi dans la console ; test `audio/` : une impulsion sur une piste à −10 ms sort 10 ms plus tôt à l'échantillon près, latence déclarée inchangée |
| D16.8 | **Écrire l'automation en jouant.** L'automation ne s'obtient qu'au dessin ; aucun mode Write/Touch/Latch, aucun armement. Cubase : W/R par tranche ; Live : armement d'automation, la main sur un fader écrit | `AutomationEdit::writeAutomationRange` (remplace les points de la plage, raccorde les bords), un mode par piste (`off / touch / latch`, absent du fichier quand off), W dans la console, capture des rappels de volume/pan/sends avec la position du transport ; test `core/` : écrire 0,5 de 0 à 960 dans une courbe à 1,0 → deux points de raccord, `automationValueAt(961)` = 1,0 |

> **D16.1 EST FAITE (04/09/2026), et la règle du chevauchement est le
> refus.** `ClipEdit::createClip` pose la fenêtre IDENTITÉ sur le matériau
> déjà là (`sourceStart == startTick`, même longueur) : ce qui sonnait à la
> mesure 5 continue d'y sonner. Un clip créé qui montrerait le début du
> matériau déplacerait le morceau à sa naissance -- c'est déjà le
> raisonnement de la matérialisation à l'ouverture, et il vaut ici.
>
> Le chevauchement, lui, était le choix à trancher. Deux clips d'une même
> piste dont les fenêtres se recouvrent lisent DEUX FOIS le même matériau :
> le passage se joue en double alors qu'aucune note n'est en double.
> Déplacer et dupliquer le laissent possible, et cela reste assumé -- on
> VOIT les deux clips qu'on empile. Créer, non : le geste vise ce qui a
> l'air d'être du vide. Donc le nouveau clip s'arrête au clip suivant (et
> l'application DIT qu'il fait une demi-mesure au lieu d'une), et si son
> début est déjà couvert, rien n'est créé et le refus est dit avec sa
> raison -- jamais une création discrète ailleurs. La durée JOUÉE fait foi,
> pas la fenêtre : un clip bouclé couvre toute sa répétition, et créer au
> milieu d'une boucle doublerait ce qu'elle répète.
>
> Le geste : double-clic sur le vide d'une piste, le point ramené sur la
> grille VERS L'ARRIÈRE (on vise une mesure, pas un tick) ; article
> « Créer un clip d'une mesure à la tête de lecture » du menu Piste ; les
> deux passent par la même fonction. Le clip prend le nom et la couleur de
> sa piste. Annulable, et l'instantané n'est pris QUE si quelque chose va
> changer -- un refus qui laisserait « Créer un clip » dans l'historique
> ferait annuler du vide. Un groupe est un bus, pas une piste de matériau :
> il refuse, en le disant.
>
> Et la moitié invisible de l'étape : la matérialisation de la fenêtre
> implicite n'avait lieu qu'à l'OUVERTURE d'un projet, si bien que des
> notes écrites au piano roll sur une piste neuve ne produisaient aucun
> clip tant qu'on n'avait pas sauvegardé et rouvert. La même fonction est
> désormais appelée après chaque écriture de notes.
>
> Trois tests `core/` : un clip d'une mesure sur une piste de deux mesures
> de noires rend EXACTEMENT les quatre notes de la première (0,0 / 0,5 /
> 1,0 / 1,5 s), pas la seconde ; sur une piste vide, le clip est là et
> l'ordonnanceur ne rend rien ; la règle du chevauchement dans ses trois
> cas (raccourci, refusé, boucle). Vu à l'écran : `VSM_VUE=clip:0:4,
> clip:1:6` -- le jeton d'autoportrait qui déclenche le MÊME code que le
> double-clic, parce qu'un geste de souris ne se photographie pas
> autrement -- pose « Acid Bass » en mesure 5 et « Drums » en mesure 7,
> d'une mesure chacun, au nom et à la couleur de leur piste ; `clip:0:0`
> sur une mesure occupée ne crée rien.

> **D16.2 EST FAITE (04/09/2026), et l'attendu écrit ici n'aurait pas suffi
> à la faire sonner.** Le tableau disait « avant la boucle, `build` émet à
> `startTick` la dernière valeur… ». C'est fait, et c'est juste — mais
> vérifié en cherchant les appelants : `PlaybackScheduler::build` n'est
> appelé qu'à UN endroit en production, `ProcessGraph::setProject`, et
> toujours avec `startTick = 0`. Le moteur construit son planning une fois,
> du début à la fin du morceau, et se déplace ensuite dedans par
> dichotomie : poser la tête au refrain ne « saute » aucun événement, il
> commence à les lire plus loin. La chasse dans `build` seule aurait donc
> été verte aux tests et muette à l'oreille — exactement la panne que ce
> dépôt s'interdit. La chasse appartient au DÉPLACEMENT DE LA TÊTE, et
> c'est là qu'elle a été posée en plus : `PlaybackScheduler::chaseAt` est
> publique, `ProcessGraph::seekSeconds` l'appelle SUR LE FIL DE
> L'INTERFACE et livre le résultat au fil audio par une file sans verrou,
> comme une note jouée au clavier. Faire remonter le planning à rebours au
> chemin audio aurait été un coût non borné là où il n'y en a pas le droit ;
> ce qui déborderait la file est compté (`droppedChasedControls`), jamais tu.
>
> STRICTEMENT AVANT le point de départ, et non « jusqu'à » : un événement
> posé exactement là est déjà rendu par la boucle ordinaire, et le chasser
> aussi le dédoublerait. Tous passages confondus, aussi : un clip bouclé
> rejoue la même valeur source à chaque répétition, et c'est la dernière
> passée sous la tête qui est en vigueur, pas celle de la ligne de temps du
> matériau.
>
> CE QUI EST CHASSÉ : les contrôleurs continus, le pitch bend, la pression
> de CANAL et le programme — des états du canal, qui valent tant qu'on ne
> les change pas. Pas la pression POLYPHONIQUE, et c'est la décision que le
> tableau laissait ouverte en écrivant « l'aftertouch » : elle s'adresse à
> une note nommée, et aucune note d'avant le point de départ ne sonne
> encore — la rendre enverrait une pression pour une note qui n'existe pas.
> Le programme part EN PREMIER : sur beaucoup d'instruments il remplace le
> son, et les contrôleurs rendus avant lui seraient effacés par lui. Une
> piste muette ou hors solo n'est pas chassée. La tête ramenée à zéro ou
> avant (le décompte) ne chasse rien : il n'y a rien avant le début.
> Chassé même transport à l'ARRÊT — sans quoi la première note jouée au
> clavier après un déplacement sonnerait avec les contrôleurs d'avant.
>
> Mesuré, avec le témoin du même code (la chasse coupée d'une ligne) : une
> molette poussée à fond à la mesure 1, une note à la mesure 3, la tête
> posée sur la note. Sans chasse, 440,4 Hz avec la molette comme sans —
> elle était perdue, et rien ne le disait. Avec, 494,8 Hz, soit les deux
> demi-tons attendus (493,9 Hz à la résolution de l'autocorrélation près).
> Sept tests : cinq `core/` (la valeur en vigueur rendue en tête au temps du
> départ ; pas de doublon sur le point de départ ; bend, pression et
> programme oui, polyphonique non, programme en premier ; la dernière
> répétition d'une boucle ; une piste muette n'est pas chassée) et deux
> `audio/` (le refrain garde la molette du couplet ; retour à zéro et
> décompte ne chassent rien). Le test de non-allocation du chemin audio
> reste vert.

> **D16.3 EST FAITE (04/09/2026), et joindre se définit par la coupe.**
> `ClipEdit::joinClips` : deux clips fusionnent quand le second est
> exactement ce qu'une coupe aurait produit du premier — ils se touchent sur
> la ligne de temps ET leur fenêtre se prolonge. Le second critère est celui
> qui compte : sans lui, le clip joint jouerait autre chose que les deux
> clips séparés, et le seul étalon qui vaille ici est que le son ne change
> pas d'une note. Sont refusés, comptés et dits : un clip qui BOUCLE (la
> fenêtre jointe ne serait plus celle qu'on répétait), un clip qui SUIT LE
> TEMPO (deux cartes de warp bout à bout ne font pas une carte, le
> prolongement des rapports aux bords se croiserait), et deux réglages de
> montage différents (gain, phase, sens, muet) — un clip joint ne peut pas
> porter les deux, et en perdre un en silence serait pire que refuser. Le
> clip joint garde le fondu d'ENTRÉE du premier et celui de SORTIE du
> dernier : les deux bords qui restent des bords.
>
> `audioTrack` est un paramètre EXPLICITE, et c'est une décision : sur une
> piste audio la fenêtre dans le FICHIER doit se prolonger aussi, en
> secondes, ce qui est la même exigence dans l'unité du matériau. Le déduire
> de `sourceStartSeconds` marcherait presque, et « presque » veut dire qu'une
> paire de clips MIDI se ferait refuser sur un critère qui ne la concerne
> pas — un clip est une fenêtre, il ne sait pas s'il montre des notes ou un
> fichier, c'est sa PISTE qui le sait.
>
> `Ctrl+J` et `Ctrl+E` sont écrits en clair dans le `keyPressed` de
> l'arrangement, comme les cinq qui y étaient déjà (Ctrl+A/C/V/D/X) et avec
> les mêmes lettres que la table. Faire consulter la table des raccourcis à
> l'arrangement est un autre chantier, et il devra déplacer les sept d'un
> coup plutôt qu'en laisser cinq en dur et deux non. Les deux gestes sont
> aussi au menu du clip. Annulables, et l'instantané n'est pris que si
> quelque chose va changer.
>
> UN DÉFAUT TROUVÉ EN REGARDANT L'ÉCRAN, et qu'aucun test de `core/` ne
> pouvait voir : après un Ctrl+E, seules les PREMIÈRES moitiés restaient
> choisies (la seconde reçoit un identifiant neuf), si bien que le Ctrl+J
> qui suivait ne trouvait qu'une moitié sur deux et ne recollait rien. Deux
> raccourcis inverses qui ne s'annulent pas sont une paire cassée. Les deux
> moitiés restent donc choisies — comme une duplication rend la sélection
> des copies —, et la coupe à l'Alt+clic fait désormais pareil : couper à la
> souris et couper au clavier sont le même geste et ne doivent pas laisser
> deux sélections différentes. Symétriquement, joindre retire de la
> sélection les identifiants absorbés, qui ne désignent plus rien.
>
> Sept tests `core/` : le clip joint rejoue événement pour événement ce que
> jouait le clip entier (témoin : le projet non découpé, comparé au
> planificateur, pas à la géométrie) ; fenêtres disjointes refusées et rien
> déplacé ; trou sur la ligne de temps refusé ; boucle, warp et gains
> différents refusés ; couper puis joindre rend le clip de départ, fondus
> compris ; trois d'affilée deviennent un, et deux paires séparées par une
> rupture donnent deux clips et un refus ; sur une piste audio les secondes
> doivent s'enchaîner, et la même paire sur une piste MIDI se joint. Vu à
> l'écran (`tout-choisir,tete:2,couper-clips[,joindre-clips]`) : un clip de
> quatre mesures coupé en deux à la mesure 3, puis rendu entier.

> **D16.5 EST FAITE (04/09/2026), et la frontière du refus est écrite dans
> les types.** `Track::locked`, absent du fichier quand il est faux (vérifié :
> un projet sans verrou ne gagne pas un octet). Verrouiller n'est PAS taire —
> la piste se joue, s'entend, se mixe et se règle comme avant, et un test le
> montre au planificateur, événement par événement. C'est le MONTAGE qui est
> refusé : déplacer, redimensionner, étirer, couper, joindre, dupliquer,
> créer, changer de piste, et l'édition des notes.
>
> OÙ VIT LE CADENAS. Les fonctions de `ClipEdit` qui prennent un
> `std::vector<Clip>&` sont la géométrie pure : elles ne savent pas à quelle
> piste appartiennent les clips, et ne peuvent donc rien vérifier. Des
> SURCHARGES qui prennent un `Track&` ont été ajoutées à côté : elles
> refusent tout sur une piste verrouillée et rendent ce qu'elles ont fait
> (zéro quand elles ont refusé). Ce sont elles que l'application appelle
> désormais, et le cadenas tient en un `if` par geste au lieu des quarante
> gestes des deux vues, où le quarante-et-unième l'aurait oublié.
> `moveClipsAcrossTracks` vérifie DES DEUX CÔTÉS : on ne prend rien à une
> piste verrouillée et on ne lui pose rien, sinon le verrou se contournerait
> en poussant depuis la voisine. Pour les notes, le point de passage unique
> était déjà là sans qu'on l'ait cherché : les trente et un gestes d'édition
> du piano roll appellent tous `beginEdit` avant de toucher au matériau ;
> il rend maintenant faux sur une piste verrouillée, et chaque geste s'arrête
> là. La vue ne teste jamais le cadenas — elle le RAPPORTE, et l'application
> le dit avec sa raison.
>
> À l'écran, le mot et non l'icône, dans la liste des pistes (« verrouillée »)
> et dans l'en-tête de l'arrangement (« midi · verrouillé »), en ambre : un
> dessin de cadenas laisserait croire que la piste est coupée, ce qu'elle
> n'est pas. Ses clips sont grisés à la même opacité qu'une piste gelée — ce
> qu'on ne peut pas saisir doit se voir avant qu'on essaie de le saisir.
> Annulable.
>
> UN DÉFAUT TROUVÉ PAR LE TEST D'ALLER-RETOUR, et qui aurait vidé l'étape de
> son sens : `"locked"` avait d'abord été écrit À L'INTÉRIEUR du bloc qui
> n'existe que pour une piste GELÉE. Le verrou aurait donc été perdu à la
> sauvegarde sur toute piste non gelée, c'est-à-dire presque toutes. Gel et
> verrou n'ont aucun rapport — l'un est une affaire de CPU, l'autre de
> montage — et ils s'écrivent maintenant côte à côte, indépendamment.
>
> Cinq tests : tous les gestes refusés sur une piste verrouillée sans qu'un
> tick bouge ni qu'un identifiant soit distribué, et le MÊME appel qui passe
> une fois déverrouillée (c'est le cadenas qu'on mesure, pas un geste
> impossible) ; une sélection à cheval ne déplace que la piste libre, et les
> clips verrouillés sont comptés pour être dits ; le changement de piste
> refusé dans les deux sens ; le rendu identique verrouillée ou non ;
> l'aller-retour disque, avec le fichier inchangé quand rien n'est
> verrouillé. Vu à l'écran (`verrouiller:0,tout-choisir,deplacer-clips`) :
> le clip d'Acid Bass grisé et immobile, celui de Drums déplacé d'une mesure.

> **D16.6 EST FAITE (04/09/2026), et le décompte reste hors de portée des
> réglages.** `setMetronomeLevel` existait depuis D3 et n'était appelé de
> nulle part : le niveau vivait dans le code, pas dans l'application. Il est
> désormais un curseur des préférences, retenu d'une exécution à l'autre et
> poussé au graphe au démarrage. Le niveau est un GAIN LINÉAIRE montré en
> clair : on veut un clic plus fort ou moins fort, pas des décibels, et la
> crête suit le chiffre exactement — 0,25 donne le quart de la crête de 1,0
> à 10⁻⁶ près, et 0 donne le silence exact (« éteint » et « à zéro » doivent
> se valoir à l'oreille).
>
> `Metronome::level_` est devenu ATOMIQUE au passage. C'était un `float` nu,
> écrit par le fil de l'interface et lu par le fil audio à chaque
> échantillon : une course de données bénigne en pratique et interdite en
> droit, que personne n'avait payée pour la seule raison que le curseur
> n'existait pas encore.
>
> Deux restrictions par-dessus l'interrupteur, branchées sur `clicAudible` :
> « seulement au décompte » (entrer en mesure et ne plus rien entendre
> ensuite) et « seulement à l'enregistrement » (c'est là qu'on en a besoin,
> et nulle part ailleurs). Elles se cumulent. AUCUNE des deux ne fait taire
> le décompte : un décompte qu'on n'entend pas ne compte rien, c'est sa
> seule raison d'être, et cette règle-là ne se règle pas — trois tests le
> vérifient, dont un qui montre que le clic se tait bien passé zéro en mode
> « décompte seul ». Le graphe apprend qu'on enregistre par
> `AudioEngine::setRecording`, qui le lui dit désormais en même temps qu'il
> le note : « seulement à l'enregistrement » ne peut pas se deviner depuis
> le fil audio.
>
> Six tests `audio/` (trois neufs et les trois du décompte, toujours verts).
> Vu à l'écran (`vsm-ui-preview`, la fenêtre rendue hors écran à 150 %) :
> « Niveau du clic » à 0,35 et « Le clic bat : seulement au décompte /
> seulement à l'enregistrement ». La fenêtre passe de 472 à 562 pixels de
> haut pour les trois rangées — « ça tient dans la case » ne l'emporte
> jamais sur « ça se lit ».

> **D16.7 EST FAITE (04/09/2026), en millisecondes et pas en ticks.**
> `Track::delayMs`, absent du fichier quand il est nul. Le réglage sert à
> corriger le temps de réaction d'un joueur, la latence d'un appareil, ou à
> poser une caisse claire trois millisecondes en retard pour qu'elle
> « traîne » : aucune de ces trois choses ne suit le tempo, et l'exprimer en
> ticks les ferait toutes changer au premier ritardando. Un test le montre —
> la même piste à 120 puis à 60 BPM garde ses dix millisecondes.
>
> DEUX SIGNES OPPOSÉS, ET CE N'EST PAS UNE FAUTE. Côté notes, le décalage
> s'ajoute au TEMPS de l'événement, à la toute fin de `build` et AVANT le
> tri : un événement décalé doit être trié là où il sonne, pas là où il était
> écrit. Côté audio, c'est la position de LECTURE qui recule de ce que la
> piste avance — à l'instant t on lit ce qui se trouvait à t + 10 ms. Là on
> déplace l'événement, ici on déplace la fenêtre par laquelle on regarde le
> fichier. La chasse aux contrôleurs (D16.2) est décalée elle aussi : une
> pédale doit arriver AVEC la piste qu'elle règle.
>
> IL NE TOUCHE PAS À LA COMPENSATION DE LATENCE. Celle-là remet les pistes
> ENSEMBLE, celui-ci les écarte exprès ; si le second changeait la première,
> régler un décalage déplacerait tout le reste du morceau. La latence
> déclarée du graphe est vérifiée inchangée à −37,5 ms.
>
> Dans la console, une case où l'on TAPE un nombre plutôt qu'un bouton qu'on
> tourne : c'est un réglage qu'on connaît (« la basse arrive trois
> millisecondes trop tard »), pas un réglage qu'on cherche à l'oreille, et un
> bouton de dix pixels ne saurait pas donner le dixième de milliseconde.
> Bornes ±200 ms — au-delà on ne corrige plus un temps de réaction, on
> déplace la partie, et cela se fait au clip, où l'on VOIT ce qu'on déplace.
>
> Cinq tests : l'impulsion audio à −10 ms sort 480 échantillons plus tôt à
> 48 kHz, À L'ÉCHANTILLON PRÈS (et 480 plus tard à +10 ms, le témoin à zéro
> tombant où le fichier le dit) ; la latence déclarée ne bouge pas ; les
> notes décalées en secondes et non en ticks, tempo changé compris ; la
> chasse suit ; l'aller-retour disque, le fichier inchangé quand le décalage
> est nul. Vu à l'écran (`VSM_VUE=mixer` sur un projet dont la première piste
> porte −12,5 ms) : « -12,5 ms » sous le panoramique d'Acid Bass, « 0,0 ms »
> sous celui de Drums.

> **D16.8 EST FAITE (04/09/2026), et Touch et Latch ne sont pas deux
> mécanismes.** `AutomationEdit::writeAutomationRange` remplace les points de
> la plage par ceux qu'on vient de jouer et RACCORDE les deux bords à ce que
> la courbe disait — sans le raccord, corriger deux mesures au milieu d'un
> fondu ferait sauter le paramètre à l'entrée et à la sortie : on aurait
> réparé deux mesures en cassant les deux voisines. Le raccord est posé à UN
> TICK de la plage et non sur ses bords ; posé dessus, il écraserait le
> premier et le dernier point de ce qu'on vient de jouer. Sur une courbe
> vide, aucun raccord n'est inventé : elle ne disait rien, et poser un zéro
> ferait plonger le paramètre hors de la plage.
>
> Un détail qui aurait rendu la courbe non déterministe : la fusion se fait
> par une carte indexée sur le tick, et non par un tri suivi d'un
> dédoublonnage. `std::unique` garde le PREMIER des ex æquo et `std::sort`
> n'est pas stable — la valeur retenue à un tick partagé aurait dépendu de
> l'implémentation de la bibliothèque standard. C'est le point JOUÉ qui
> gagne, toujours.
>
> `Track::automationMode` (`off / touch / latch`), absent du fichier quand il
> est éteint. Les deux modes armés ne se distinguent QUE par l'instant où
> l'enregistrement s'arrête — le lâcher pour Touch, l'arrêt du transport pour
> Latch —, et c'est pourquoi il n'y a qu'une `Passe` et qu'un `fermerPasse`
> dans la tranche. En Latch, la valeur tenue du lâcher jusqu'à l'arrêt s'écrit
> par UN point de plus à la fin : pas de minuterie qui échantillonnerait une
> valeur qui ne bouge plus. Une passe ne s'ouvre que si le transport ROULE —
> écrire à l'arrêt déposerait tout sur un seul tick, et transformerait un
> simple réglage de mixage en édition de courbe. La courbe reçoit le gain
> LINÉAIRE et non les décibels du curseur : `mix.volume` est en gain, et
> écrire des dB dessinerait une courbe que le moteur n'applique pas.
>
> L'ARRÊT EST DÉTECTÉ SUR LE FRONT DESCENDANT, dans la minuterie, et non dans
> les six endroits qui appellent `transport_.stop()` : un seul des six oublié
> laisserait une passe ouverte pour toujours, et elle se déposerait au
> prochain arrêt — longtemps après le geste, et sur la mauvaise plage. Fermée
> AVANT le retour au départ (D14.5), pour que la fin de la passe soit là où
> la lecture s'est arrêtée et non là où elle était partie. Désarmer clôt
> aussi ce qui courait.
>
> À l'écran, le bouton W a SA PROPRE RANGÉE dans la tranche. Mis en tiers
> avec M et S, les trois libellés étaient tronqués en « ... » sur 76 pixels à
> l'échelle 150 % — vu en regardant la capture, pas en le supposant. Il porte
> maintenant « W touch » (ambre) ou « W latch » (rouge), lisible d'un coup
> d'œil, et la hauteur par défaut du dock du bas passe de 260 à 282 pour que
> le fader garde sa poignée. Sur une machine qui a déjà réglé cette hauteur,
> c'est le réglage retenu qui l'emporte, comme il se doit.
>
> Six tests : cinq `core/` (la plage remplacée et le bord raccordé — écrire
> 0,5 de 0 à 960 dans une courbe à 1,0 laisse 1,0 au tick 961 ; écrire au
> milieu d'un fondu ne casse ni l'amont ni l'aval ; un passage sans point
> joué n'efface rien ; le point joué gagne sur celui qui était là ; une
> courbe vide ne reçoit pas de raccord inventé) et un `interchange/`
> (aller-retour des trois modes, fichier inchangé quand tout est éteint).

> **D16.4 EST FAITE (04/09/2026), et elle a pris le menu plutôt que le clic
> droit sec.** Le tableau disait « clic droit le retire » ; un repère qui
> disparaît sous un clic droit sans rien demander est une perte silencieuse,
> et le piano roll avait déjà tranché l'inverse (D10) : sa règle ouvre un
> menu. Deux vues du même morceau ne demandent pas deux gestes pour la même
> chose. Donc, sur la règle de l'arrangement comme sur celle du piano roll :
> clic droit ouvre « Poser un repère ici… / Renommer ce repère… / Retirer ce
> repère », les deux derniers grisés hors d'un repère ; double-clic sur un
> repère le renomme, double-clic sur le vide en pose un ; le repère visé est
> le plus proche à dix pixels près, jamais un tick exact qu'on ne saurait
> viser à la souris. Le RENOMMAGE n'existait nulle part avant : on ne
> pouvait que retirer et reposer, en perdant sa place. Les trois gestes sont
> désormais trois fonctions de `MainComponent` (`requestMarker`,
> `renameMarker`, `removeMarker`) que les deux règles appellent, et qui
> rafraîchissent les deux vues ensemble -- l'ancienne version ne repeignait
> que le piano roll, si bien qu'un repère posé n'apparaissait dans
> l'arrangement qu'au prochain redessin. Annulable, chaque geste sous son
> nom. Dans l'arrangement, le repère est un fanion et son nom dans la règle
> plus un trait sur toute la hauteur des pistes (on voit où tombe le refrain
> par rapport aux clips, ce qui est la raison d'être de la chose) ; le nom
> n'a que la place jusqu'au repère suivant, et sous vingt-six pixels on
> n'écrit pas -- le fanion suffit. Au passage, les libellés accentués : la
> règle du piano roll affichait « Poser un repere ici... » depuis D10, faute
> d'échappement UTF-8 dans la source. Vu à l'écran (`VSM_VUE=arrangement,
> sans-rack,sans-mixer` sur un projet à trois repères) : Intro en 1,
> Refrain en 3, Pont en 5, les trois traits traversant les deux pistes.

**Ce que l'audit a écarté, et pourquoi.** Le solo safe (les retours restent
audibles en solo, `ProcessGraph.cpp:1149`, le besoin est faible) ; le
panic MIDI (à revoir avec un clavier externe branché) ; le repliement d'un
groupe qui cacherait ses membres (le groupe est un bus, pas un dossier :
§ 4) ; la règle en secondes (le transport affiche déjà les deux) ; la
transposition d'un clip entier depuis l'arrangement (Cubase seul, le piano
roll transpose à la flèche) ; le note repeat (l'arpégiateur temps réel est
écarté depuis D11).

### Phase D17 — Le sixième audit : ce qui manque une fois D16 posée (04/09/2026, 23:55) — **TERMINÉE (05/09/2026, 01:05)**

**Pourquoi.** Même méthode que D11 à D16, même garde-fou : chaque absence
ci-dessous a été VÉRIFIÉE dans le code avant d'être écrite, jamais supposée.
Le relevé a d'abord écarté ce qui existe déjà — le sidechain (`ProcessGraph`
lit un bus de départ en entrée latérale), la quantification de la dernière
prise, le report en audio, le gel, l'analyseur de spectre, les presets
d'effet, les rampes de tempo, le suivi de tempo des clips, les repères, le
verrou, le décalage de piste, l'écriture d'automation en jouant. Restent
huit manques, et l'ordre suit le § 3 : ce qui MENT d'abord, le geste de tous
les jours ensuite, le modèle en dernier.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D17.1 | **Les fondus n'ont pas de forme.** `AudioTrackSource.cpp:18-22` : `gain *= position / fadeIn`, strictement LINÉAIRE. Deux clips corrélés en fondu enchaîné creusent donc ~3 dB au milieu — le raccord s'entend, et rien ne le dit. Cubase : sept formes par fondu ; Live : Constant Power / Constant Gain | une forme par fondu (`Linear`, `EqualPower`, et `Slow`/`Fast` pour l'attaque), absente du fichier quand linéaire ; le moteur applique la forme ; test `audio/` : deux copies du MÊME bruit en fondu enchaîné d'égale puissance gardent leur niveau à 0,5 dB près au point de croisement, contre −3 dB en linéaire (le témoin est la même passe, forme changée) |
| D17.2 | **L'automation ne suit pas les clips.** `ClipEdit.cpp` ne touche à `Track::automation` nulle part : déplacer un clip d'une mesure laisse sa courbe de volume où elle était, et le projet ne joue plus ce qu'il montre. Cubase : « l'automation suit les événements », actif par défaut | `AutomationEdit::shiftAutomationRange` et son usage par `moveClips`/`moveClipsAcrossTracks`/`splitClips` — les points de la plage couverte par le clip suivent, les autres non ; un interrupteur global (Cubase le rend débrayable, et le débrayer sert quand on remonte une prise sous une courbe qu'on veut garder) ; test `core/` : un clip de [0,960[ portant une courbe déplacé à 1920 → `automationValueAt` rend à 1920 ce qu'elle rendait à 0, et rien n'a changé hors de la plage |
| D17.3 | **Ce qu'on vient de jouer est perdu.** Aucune capture rétrospective : jouer une phrase sans avoir armé, la trouver bonne, et n'avoir aucun moyen de la garder. Cubase : Retrospective Record ; Live : Capture MIDI | un tampon circulaire des N dernières minutes d'entrée MIDI, alimenté DÈS que l'application tourne (pas seulement à l'enregistrement) ; « Enregistrement ▸ Récupérer ce qui vient d'être joué » pose les notes sur la piste choisie, à leur place réelle sur la ligne de temps ; annulable ; test `core/` : trois notes poussées au tampon puis récupérées rendent trois notes aux mêmes ticks |
| D17.4 | **On ne peut pas masquer une piste.** Rien dans `Track` ni dans les vues : une reconstruction à soixante pistes se parcourt en entier ou pas du tout. Cubase : Visibility ; Live : Fold/Unfold et les Track Groups | `Track::hidden` (absent du fichier quand faux), respecté par la liste des pistes, l'arrangement et la console — et par AUCUN calcul : une piste masquée sonne exactement comme avant (test), sans quoi « masquer » deviendrait « couper » à l'insu de tous |
| D17.5 | **Transposer une piste ou le morceau.** Le piano roll transpose une SÉLECTION de notes ; rien ne transpose une piste entière ni le morceau, et rien ne le fait sans réécrire les notes. Cubase : Transpose de l'inspecteur, piste de transposition | `Track::transposeSemitones` (absent du fichier quand nul), appliqué par `PlaybackScheduler` À LA LECTURE et non au matériau — c'est ce qui le rend annulable d'un chiffre et non d'un historique ; les notes hors 0..127 sont ÉCARTÉES et comptées, jamais repliées ; saisi dans la console ; test `core/` : +12 rend les mêmes notes une octave au-dessus, une note à 120 transposée de +12 disparaît et le compteur le dit |
| D17.6 | **Raccourcir un clip à ce qui sonne.** Aucune détection de silence : un stem reconstruit commence par 400 ms de rien, et il faut tirer le bord à l'œil. Cubase : Detect Silence ; Live : le même geste à la main | `audio::analysis::detectSilence` (seuil en dB, durée minimale, marge avant l'attaque), et « Clip ▸ Rogner au son » qui règle la fenêtre du clip sans toucher au fichier ; annulable ; test `audio/` : un bruit précédé de 500 ms de silence à −80 dB est rogné à 500 ms ± 1 ms, et un fichier sans silence n'est pas touché |
| D17.7 | **L'automation ne sait pas courber.** `AutomationPoint` n'a que `step` : un segment est droit ou en marche d'escalier, jamais courbe. Un fondu de volume droit en gain sonne comme une chute brutale à la fin. Cubase : poignée de courbure sur chaque segment ; Live : la même | un `curve` par point (−1 à +1, 0 = droit), écrit seulement quand il n'est pas nul ; la MÊME interpolation dans `AutomationEdit` et dans `AutomationLane` du moteur (deux formules qui divergeraient feraient dessiner une courbe et en entendre une autre — c'est déjà la règle du § 6) ; la poignée se tire au milieu du segment dans la voie d'automation ; tests `core/` ET `audio/` sur les mêmes points |
| D17.8 | **Le groove ne s'extrait ni ne s'applique.** La quantification ne connaît que la grille et le swing : on ne peut pas prendre le placement d'une batterie reconstruite et le donner à une basse écrite droite. Cubase : Groove Agent / Hitpoints → quantize ; Live : le Groove Pool | `sequencer::Groove` (une suite d'écarts en fraction de pas, plus une force), `extractGroove` depuis les notes d'une piste et `applyGroove` sur une sélection ; le groove s'enregistre dans la bibliothèque comme un preset (`*.groove.json`) ; test `core/` : extraire d'une piste puis appliquer à une copie DROITE de cette piste rend les ticks d'origine à un tick près |

> **D17.2 EST FAITE (05/09/2026), et le décalage suivi est celui qui s'est
> FAIT, pas celui qu'on a demandé.** `AutomationEdit::shiftAutomationRange`
> déplace les points d'une plage ; les surcharges `Track&` de `moveClips` et
> `moveClipsAcrossTracks` l'appellent pour chaque clip choisi. Le projet
> jouait autre chose que ce qu'il montrait dès qu'on déplaçait un clip d'une
> mesure ; il ne le fait plus.
>
> UN PIÈGE ÉVITÉ EN L'ÉCRIVANT AVANT DE CODER : `moveClips` RÉDUIT le
> décalage pour tous quand l'un des clips buterait sur zéro (c'est la règle
> qui garde à la sélection sa figure). Décaler les courbes de ce qu'on a
> demandé au lieu de ce qui s'est fait les aurait désaccordées des clips
> qu'elles suivent, et seulement dans ce cas-là — le genre de défaut qu'on ne
> voit qu'un mois plus tard. Le décalage appliqué est donc RELU sur un clip
> après coup. Un test l'exerce (−2000 demandé, −480 obtenu).
>
> AUCUN POINT N'EST CRÉÉ AUX BORDS, au contraire de `writeAutomationRange` qui
> en pose deux. Écrire une plage REMPLACE ce qu'elle contenait, donc il faut
> raccorder ; la déplacer TRANSPORTE ce qu'elle contenait, et poser des
> raccords ajouterait deux points à chaque déplacement — au bout de dix
> gestes, la courbe serait un peigne. Un point déplacé qui retombe sur un tick
> occupé gagne : c'est celui qu'on vient de tirer.
>
> Changer de PISTE n'est pas la même chose que changer de PLACE : le clip
> garde sa position, donc les points ne se déplacent pas dans le temps, ils
> DÉMÉNAGENT — ils quittent la courbe de même paramètre de la piste d'origine
> et entrent dans celle de la cible, créée si elle manque. C'est exactement la
> règle que D11.1 avait écrite pour les notes, et pour la même raison : ce que
> le clip montre doit le suivre.
>
> Le suivi est une PRÉFÉRENCE passée en paramètre à `ClipEdit` plutôt que lue
> quelque part — `core/` ne connaît aucune préférence —, et dans le paramètre
> plutôt qu'à côté de l'appel, pour qu'un appelant ne puisse pas déplacer un
> clip en oubliant sa courbe : le même raisonnement que pour le verrou de
> D16.5. Active par défaut comme chez Cubase, débrayable, retenue d'une
> exécution à l'autre. La débrayer sert : quand on remonte une prise SOUS une
> courbe qu'on veut garder, c'est la courbe qui a raison.
>
> `splitClips` n'a rien reçu, et c'est une décision : couper ne déplace rien
> sur la ligne de temps, donc aucune courbe n'a à bouger. Le tableau le
> nommait par prudence ; la vérification a montré qu'il n'y avait rien à y
> faire.
>
> Quatre tests `core/` : le clip déplacé emporte sa courbe et laisse le reste
> en place ; l'interrupteur éteint ne touche à rien ; le décalage suivi est
> celui qui s'est fait ; le changement de piste fait déménager les points de
> la plage et laisse les autres. Vu à l'écran : « En déplaçant ▸ L'automation
> suit les clips » dans les préférences.

> **D17.3 EST FAITE (05/09/2026), et elle a fait tomber un piège que le
> tableau n'avait pas vu.** `RetrospectiveBuffer` garde les quatre mille
> derniers événements MIDI — une capacité en ÉVÉNEMENTS et non en minutes,
> parce qu'elle est alors bornée en mémoire quoi qu'on joue —, alimenté dès
> que l'application tourne. La file de capture du moteur ne se remplissait
> que pendant l'enregistrement : elle se remplit maintenant toujours, et
> l'application la vide au tampon à chaque tour de minuterie. Le chemin
> d'enregistrement ne change pas d'une ligne, le point d'entrée de
> `MidiRecorder` écartant comme toujours ce qui le précède.
>
> LA RÉCUPÉRATION NE FAIT PAS L'APPARIEMENT ELLE-MÊME : elle reverse ses
> événements dans un `MidiRecorder` neuf et lui demande ses notes. Apparier
> des touches en notes — avec ses cas tordus, la touche encore tenue, le
> relâchement sans enfoncement — est déjà écrit et déjà testé ; l'écrire une
> seconde fois donnerait deux appariements qui finiraient par diverger.
>
> LE PIÈGE, trouvé en lisant `transportSecondsAtClock` avant de s'en servir :
> transport à l'ARRÊT, le temps du morceau ne passe pas, et l'ancre
> horloge→transport rend la MÊME position pour toutes les notes d'une
> phrase. On aurait récupéré un accord de douze notes là où l'on avait joué
> une mélodie — et c'est justement à l'arrêt qu'on pianote en cherchant une
> idée, donc le cas principal. Hors lecture, la position est donc construite
> sur le TEMPS RÉEL écoulé depuis la première note de la rafale, posée à la
> tête de lecture ; l'ancre de rafale se remet à zéro dès que le transport
> repart.
>
> Les notes sont posées en OVERDUB, jamais en substitution : on récupère
> par-dessus ce qui était là. Le tampon est VIDÉ après coup — sans cela, un
> second « récupérer » reposerait les mêmes notes en double sans que rien ne
> le dise. Le menu dit combien d'événements sont en mémoire, et l'article est
> grisé quand il n'y en a pas : « récupérer » sur un tampon vide ne doit pas
> se découvrir en cliquant. Une piste audio refuse, en le disant. Annulable.
>
> Quatre tests `core/` : trois notes récupérées aux ticks où elles ont été
> jouées (à leur place réelle, pas au début du morceau) ; le tampon plein
> oublie les plus anciennes et jamais les dernières ; un tampon vide ne rend
> rien plutôt qu'une note de durée nulle ; une touche encore tenue rend une
> note qui finit au dernier événement.

> **D17.4 EST FAITE (05/09/2026), et masquer se réduit à une hauteur nulle.**
> `Track::hidden`, absent du fichier quand il est faux. La règle qui compte
> tient en une phrase : MASQUER N'EST PAS COUPER. Une piste masquée sonne, se
> mixe et s'exporte exactement comme avant — vérifié au planificateur,
> événement par événement, et vérifié aussi qu'elle ne devient pas audible si
> elle était muette (les deux drapeaux sont indépendants, et c'est le muet qui
> décide du son). Un « masquer » qui ferait taire serait la pire des pannes
> muettes : on chercherait une heure pourquoi la basse a disparu du mixage.
> AUCUN calcul ne lit ce drapeau ; seules les trois vues qui dessinent des
> pistes le lisent.
>
> DANS L'ARRANGEMENT, IL A SUFFI DE FAIRE RENDRE ZÉRO À `trackHeight`.
> `trackTop`, `trackAtY`, les zones de pliage et le dessin somment tous cette
> fonction : ils sautent donc la piste sans savoir qu'elle existe. Filtrer
> dans chacun d'eux aurait été cinq endroits à ne pas oublier — et le sixième
> écrit demain aurait été le mauvais. Seul le DESSIN reçoit un `continue`
> explicite, parce que le trait de séparation se pose à `y + h` : à hauteur
> nulle, il se serait posé sur celui de la piste précédente, deux traits l'un
> sur l'autre à un endroit qui ne sépare rien.
>
> Dans la liste des pistes et dans la console, même idée (hauteur ou largeur
> nulle) mais les rangées et les tranches restent CONSTRUITES et indexées
> comme les pistes : ne pas les construire aurait décalé `rows_[idx]` et
> `strips_[i]`, dont la sélection, le glisser-déposer et la mesure de niveau
> se servent partout. La hauteur et la largeur totales ne comptent que les
> visibles, sans quoi un blanc resterait à leur place.
>
> Le menu Piste dit combien de pistes sont masquées et grise « Afficher toutes
> les pistes » quand il n'y en a aucune : on ne doit pas avoir à cliquer pour
> savoir s'il en reste. Annulable des deux côtés.
>
> Deux tests : le rendu identique masquée ou non (`core/`), l'aller-retour
> disque avec le fichier inchangé quand rien n'est masqué (`interchange/`).
> Vu à l'écran (`masquer:0`) : Acid Bass disparaît de la liste ET de
> l'arrangement, et Drums remonte à sa place sans laisser de blanc.

> **D17.5 EST FAITE (05/09/2026), à la lecture et jamais dans le matériau.**
> `Track::transposeSemitones`, absent du fichier quand il est nul, appliqué
> par `PlaybackScheduler` au moment de fabriquer les événements. C'est tout
> ce qui le distingue du « transposer la sélection » du piano roll, et c'est
> ce qui le rend utile : on l'annule en remettant zéro plutôt qu'en défaisant
> un historique, on l'essaie à l'oreille en tournant un chiffre, et deux
> transpositions successives ne s'accumulent en rien puisqu'il n'y a aucun
> arrondi. Un test vérifie que le matériau n'a pas bougé d'une note.
>
> LE PRIX, ASSUMÉ ET ÉCRIT : le piano roll montre le matériau, donc les notes
> ÉCRITES et non celles qu'on entend. C'est le comportement de Cubase, et il
> se comprend dès qu'on sait que le réglage existe — d'où le choix de le
> mettre dans la CONSOLE, à côté du fader, qui est le seul endroit où il se
> voit forcément.
>
> LES NOTES POUSSÉES HORS DE 0..127 SONT ÉCARTÉES, JAMAIS REPLIÉES à
> l'octave : les faire sonner à une hauteur que personne n'a demandée serait
> pire que de ne pas les jouer. Elles sont comptées
> (`PlaybackScheduler::transposeDroppedNotes`) et l'application le dit — une
> fois, au franchissement de zéro, et pas à chaque cran du curseur : une
> alerte par demi-ton rendrait le réglage inutilisable, et une alerte qui ne
> vient jamais laisserait chercher la note manquante. Le message rappelle que
> le matériau n'a pas bougé et qu'un zéro remet tout.
>
> Trois tests : +12 rend les mêmes notes une octave au-dessus sans toucher au
> matériau, et zéro rend exactement le témoin ; une note à 120 transposée de
> +12 disparaît, le compteur passe de 0 à 1, et la note restante sort à 72
> (et surtout pas repliée à 120) ; l'aller-retour disque avec le fichier
> inchangé à zéro. Vu à l'écran : « -7 dt » sous le décalage dans la tranche
> d'Acid Bass.

> **D17.6 EST FAITE (05/09/2026), et elle ne relit pas le cache d'aperçu.**
> `io::detectSound` cherche les bornes de ce qui dépasse un seuil de crête,
> avec une marge avant l'attaque et un silence minimal en deçà duquel on ne
> touche à rien.
>
> LE CACHE D'APERÇU A ÉTÉ ENVISAGÉ ET ÉCARTÉ, alors qu'il est déjà là et que
> « Normaliser » (D13.6) s'en sert justement pour ne pas relire le fichier :
> ses tranches font 256 échantillons, soit 5,3 ms à 48 kHz, et cette étape
> promet la milliseconde. Une attaque coupée cinq millisecondes trop tôt est
> un clic ; coupée cinq millisecondes trop tard, c'est le transitoire qu'on
> mange. On lit donc les échantillons, ce qu'une commande déclenchée à la
> main peut se permettre.
>
> LA MARGE AVANT L'ATTAQUE (5 ms par défaut) n'est pas de la prudence
> décorative : une attaque n'est jamais un mur, et couper à l'échantillon
> exact où le seuil est franchi rabote le début du transitoire. LE SILENCE
> MINIMAL (20 ms) est le garde-fou inverse : sans lui, la commande
> grignoterait trois millisecondes à chaque clip qu'on lui donne, et l'on ne
> saurait jamais si elle a fait quelque chose. Tout sous le seuil : on ne
> rogne RIEN — un clip entièrement silencieux réduit à zéro tick disparaîtrait,
> et personne n'a demandé de le supprimer.
>
> LE ROGNAGE PASSE PAR `resizeClipsStart` ET `resizeClipsEnd`, c'est-à-dire
> par les deux mêmes gestes qu'à la main, mesurés au lieu d'être visés. Écrire
> les champs du clip directement aurait été plus court et aurait perdu toutes
> leurs règles : le verrou de la piste, la longueur minimale d'un tick, la
> fenêtre en SECONDES d'un clip audio. Le fichier n'est pas touché — c'est la
> fenêtre qui bouge —, et l'application dit combien de millisecondes sont
> parties de chaque côté.
>
> Quatre tests `audio/` : 500 ms de plancher à −80 dB trouvées à la
> milliseconde près (48 échantillons) ; la marge coupe AVANT l'attaque et
> jamais dedans ; un fichier qui commence sur le son n'est pas touché ; les
> deux bouts sont rognés, et un fichier entièrement silencieux est laissé tel
> quel.

> **D17.7 EST FAITE (05/09/2026), et c'est l'écran qui a trouvé le défaut que
> quatre suites de tests laissaient passer.** `AutomationPoint::curve`, de −1
> à +1, zéro étant la droite et n'étant pas écrit. La forme est `x` élevé à la
> puissance `2^(−2·courbure)` : continue, strictement croissante, égale à
> l'identité à courbure nulle, et SYMÉTRIQUE — la courbure opposée donne la
> fonction réciproque, ce qu'un test vérifie. Une puissance plutôt qu'une
> Bézier parce qu'elle s'inverse et se compose sans rien résoudre, et que la
> poignée du milieu de segment suffit à la régler.
>
> POURQUOI IL EN FALLAIT UNE : un fondu de volume DROIT EN GAIN n'est pas un
> fondu droit à l'oreille. L'oreille entend des décibels, et une droite en
> gain passe la moitié de sa course dans les six derniers décibels — elle
> s'entend comme une chute brutale à la fin. C'est le geste d'automation le
> plus courant qui soit, et il ne se dessinait pas.
>
> LA FORMULE EST DANS LE MODÈLE (`sequencer::automationCurveEase`) et le
> MOTEUR L'APPELLE : c'est l'invariant du § 6, et un test `audio/` compare
> les deux implémentations sur sept courbures et quatre-vingt-deux positions,
> à 10⁻⁶ près. Le dessin, lui, n'a eu besoin d'aucune ligne : il échantillonne
> déjà `automationValueAt` tous les deux pixels — mettre la formule au bon
> endroit l'a fait suivre tout seul.
>
> **LE DÉFAUT, ET IL MÉRITE D'ÊTRE ÉCRIT EN ENTIER.** La courbe restait
> DROITE à l'écran alors que le fichier la disait courbe. Les tests de `core/`
> (la fonction), du moteur (l'accord des deux formules) et du format
> (l'aller-retour disque) étaient tous verts, et aucun ne pouvait le voir : le
> chemin fautif est dans `app/`, où une fonction refait les courbes du projet
> À PARTIR DES VOIES DU MOTEUR après chaque republication, et construisait ses
> points sans la courbure. Elle repartait donc à zéro à chaque republication —
> dessinée droite, et ÉCRASÉE à la sauvegarde suivante. Aucune suite ne
> traverse ce point de passage, et c'est précisément ce que la règle « lancer
> l'application et la regarder » existe pour attraper. Deux autres conversions
> avaient déjà failli tomber : les agrégats POSITIONNELS de `ProjectDocument`,
> où le champ ajouté en dernier prenait sa valeur par défaut sans que le
> compilateur ait un mot à dire.
>
> La poignée est un petit cercle ÉVIDÉ au milieu de chaque segment assez large
> (24 px), pâle quand la courbure est nulle et vive sinon — elle ne doit pas se
> confondre avec un point, qui est carré. On la tire verticalement, cent
> pixels pour toute la course, et le SENS SUIT LA PENTE : tirer vers la valeur
> d'arrivée accélère, tirer à l'opposé fait traîner. Sans cette symétrie, la
> poignée ferait l'inverse de ce qu'on croit sur un segment descendant. La
> courbure se calcule par rapport à celle du clic et non en s'accumulant : un
> glissement qui repasse par son point de départ rend la courbure de départ au
> bit près. Un palier ne se courbe pas, et n'a pas de poignée.
>
> Six tests : quatre `core/` (zéro est exactement la droite ; les deux sens
> écartent la mi-course de part et d'autre sans jamais déplacer les bornes ;
> les courbures opposées sont réciproques ; un palier tient sa valeur quoi
> que dise la courbure ; l'aisance est monotone et bornée), un `audio/`
> (l'accord des deux implémentations) et un `interchange/` (l'aller-retour, et
> le fichier inchangé à courbure nulle). Vu à l'écran : le premier segment
> monte vite et s'aplatit, le second traîne et se précipite, et les deux
> poignées disent laquelle est réglée.

> **D17.8 EST FAITE (05/09/2026), et un pas muet reste muet.** `Groove` est
> une suite d'écarts en FRACTION de pas, plus une vélocité moyenne par pas.
> `extractGroove` prend le placement réel d'une partie, `applyGroove` le donne
> à une autre.
>
> CE QUE LA QUANTIFICATION NE SAVAIT PAS FAIRE. `Quantizer` connaît la grille
> et le swing : il RAPPROCHE d'un idéal calculé. Un groove fait l'inverse — il
> prend le placement d'une partie qu'on trouve bonne et l'impose ailleurs. Sur
> ce projet-ci, c'est le geste qui manquait le plus : la chaîne d'analyse
> reconstruit une batterie avec son placement d'origine, et rien ne permettait
> de le donner à une basse écrite droite.
>
> DES ÉCARTS ET NON DES POSITIONS, en fraction de pas et non en ticks : le même
> groove s'applique à n'importe quel tempo et à n'importe quelle résolution, et
> le fichier `*.groove.json` ne contient aucun tick. Une note est rattachée au
> pas dont elle est la plus PROCHE et non à celui qui la précède — une note
> jouée deux millisecondes en avance appartient au pas qu'elle anticipe. Les
> écarts d'un même pas sont moyennés : une grosse caisse et un charley sur le
> même temps décrivent le même instant musical.
>
> UN PAS ABSENT EST LA DÉCISION DE L'ÉTAPE. Quand la partie d'origine ne jouait
> rien sur un pas, le groove ne dit RIEN de ce pas, et les notes qui y tombent
> ne bougent pas. Un écart nul aurait l'air pareil et ferait le contraire : il
> ramènerait ces notes sur la grille, c'est-à-dire qu'il les quantifierait sans
> qu'on l'ait demandé. Le fichier écrit donc `present` à part, et un test
> vérifie qu'un pas absent le reste après l'aller-retour.
>
> La FORCE est un paramètre de l'application et non du groove : le même groove
> sert à cent pour cent sur une basse et à trente sur un piano. À un demi, la
> note fait la moitié du chemin depuis là où elle est — c'est ce qui « teinte »
> sans déplacer. L'accentuation suit SÉPARÉMENT : on veut souvent le placement
> sans toucher aux nuances qu'on a écrites. Un groove ne change jamais la durée
> d'une note : il déplace, il n'étire pas.
>
> Le groove courant vit dans l'APPLICATION et non dans le projet : c'est un
> outil qu'on porte d'un morceau à l'autre, comme un preset. Le projet garde
> les notes telles que le groove les a laissées, et n'a pas à se souvenir d'où
> elles tiennent leur placement. Il s'enregistre dans la bibliothèque au même
> endroit que les presets d'effet (D15.4) ; un fichier qui n'est pas un groove
> est NOMMÉ, jamais lu comme un groove vide. Le menu Édition dit le nom du
> groove en mémoire — appliquer « quelque chose » qu'on ne nomme pas, c'est
> appliquer on ne sait quoi.
>
> Sept tests : cinq `core/` (extraire d'une partie qui balance puis appliquer à
> une copie DROITE rend les ticks d'origine À UN TICK PRÈS — le critère de
> l'étape ; la demi-force fait la moitié du chemin et la force nulle ne bouge
> rien ; un pas muet laisse ses notes tranquilles ; l'accentuation ne suit que
> si on le demande ; un groove vide n'applique rien) et deux `interchange/`
> (l'aller-retour avec écarts, accents et pas muets ; un fichier d'un autre
> format, une version future, un groove sans pas et un JSON cassé sont tous
> refusés en le disant).

> **D18.3 EST FAITE (05/09/2026), et aucun geste n'a eu à apprendre ce qu'est
> un groupe.** `Track::editGroup` (0 = aucun, absent du fichier), et UNE seule
> fonction : `expandSelectionToEditGroups`. Les six gestes de montage —
> couper, déplacer, joindre, redimensionner, étirer, à la souris comme au
> clavier — travaillaient déjà sur une sélection ; c'est la SÉLECTION qui a
> grandi, et ils en héritent sans une ligne de plus. Écrire « et fais la même
> chose sur les pistes du groupe » dans chacun des six aurait garanti que le
> septième l'oublie : c'est le raisonnement du verrou (D16.5), et il marche
> encore mieux ici.
>
> `selection_` reste EXACTEMENT ce que l'utilisateur a cliqué — un clic qui
> ferait grossir la sélection en silence rendrait impossible de savoir ce
> qu'on a pris. L'élargissement est calculé au moment de s'en servir, et il a
> deux consommateurs pour un seul résultat : les gestes de temps, et le
> DESSIN, qui montre les clips liés comme choisis. Sans ce second, on
> couperait trois pistes en croyant en couper une — exactement la surprise
> qu'un groupe d'édition doit éviter, pas produire.
>
> LE CRITÈRE DE RATTACHEMENT EST LE RECOUVREMENT, et non l'égalité des bornes :
> deux micros d'une même batterie sont découpés pareil, mais un clip a pu être
> rogné, et c'est encore le même passage. Rien à voir avec `outputGroup`, qui
> est un bus de MIXAGE : celui-ci ne touche à aucun signal, deux pistes peuvent
> être liées à l'édition et sortir sur des bus différents. Huit groupes, parce
> que le besoin réel est « les micros de la batterie » et « les doublages de
> la voix », et qu'au-delà on ne s'y retrouve plus.
>
> Cinq tests : la sélection grandit au bon groupe et pas aux autres ; couper
> une piste d'un groupe de trois coupe les trois AU MÊME TICK et laisse la
> quatrième entière ; le rattachement se fait par recouvrement ; sans groupe la
> sélection est rendue telle quelle, et une sélection vide reste vide
> (l'élargir donnerait tout) ; l'aller-retour disque. Vu à l'écran AVEC SON
> TÉMOIN, un seul clip choisi (`choisir-clip:0,tete:2,couper-clips`) : groupe
> posé, les deux pistes sont coupées à la mesure 3 ; groupe retiré, seule
> Acid Bass est coupée et Drums reste entière.
>
> **D18.2 (assembler les prises) a été prise APRÈS celle-ci, et c'était une
> décision — elle est faite depuis (voir sa note).** Le comping n'est utilisable qu'avec une vue des prises empilées
> où l'on dessine la plage qu'on garde : livrer le modèle sans la vue
> donnerait une fonction que personne ne peut appeler, et ce dépôt préfère une
> étape entière à deux moitiés.

> **D18.6 EST FAITE (05/09/2026).** `Project::notes`, un texte libre écrit
> dans `project.json` seulement s'il y a quelque chose à écrire. Affichage ▸
> Notes du projet, une fenêtre flottante qui écrit dans le projet À CHAQUE
> FRAPPE et marque le projet modifié : des notes qu'il faudrait penser à
> valider seraient des notes perdues. Le panneau ne détient rien — le texte
> vit dans le projet, et une seconde copie serait la première à mentir.
>
> POURQUOI CE PROJET-CI EN A PLUS BESOIN QU'UN AUTRE : une reconstruction est
> faite de DÉCISIONS (« la basse vient du stem `other` parce que la séparation
> l'y avait rangée », « la nappe est une hypothèse, le stem n'avait rien de
> net », « cette piste est coupée exprès »). Le rapport de reconstruction dit
> ce que la CHAÎNE a fait ; ceci dit ce que l'HUMAIN a décidé, et les deux ne
> se remplacent pas.
>
> UN CAS QUE LE TEST ATTRAPE : des notes VIDES venant du document n'effacent
> pas celles qui sont en mémoire. Un projet ouvert par une version qui ne
> connaît pas le champ ne doit pas les perdre au réenregistrement — c'est la
> même prudence que pour les presets d'un projet ancien. Le menu dit le nombre
> de caractères : un bloc-notes vide et un bloc-notes plein s'ouvrent pareil,
> et savoir qu'il y a quelque chose dedans est la moitié de son intérêt.
>
> Un test `interchange/` (aller-retour, fichier inchangé quand le texte est
> vide, et des notes vides qui n'écrasent rien). Vu à l'écran par une capture
> d'ÉCRAN et non un autoportrait — la fenêtre est flottante, donc absente du
> rendu du composant principal.

> **D18.5 A ÉTÉ ESSAYÉE PUIS REMISE (05/09/2026), et ce qu'elle a appris vaut
> d'être écrit.** L'idée paraissait tenir en deux lignes : un facteur sur
> l'horloge du transport, et tout ce qui lit le transport suit. Le facteur a
> été posé sur l'avance de BLOC (`blockEndSeconds = blockStart + durée ×
> vitesse`), avec la précaution qui allait bien — à 1,0 exactement,
> l'arithmétique de placement des événements restait celle d'avant, arrondi
> pour arrondi, pour ne déplacer aucun échantillon des rendus existants.
>
> **Le banc a dit non, et tout de suite : à 0,5, l'impulsion posée à une
> seconde sortait encore à une seconde.** La raison est que le bloc n'est pas
> l'unité du temps ici. `renderSpan` découpe chaque bloc en SOUS-SEGMENTS pour
> l'automation, et chacun calcule sa position en secondes à partir du décalage
> d'échantillon dans le bloc — à raison d'un échantillon pour un échantillon.
> Ralentir l'avance de bloc sans toucher à cette conversion revient à ralentir
> l'horloge de la pendule sans ralentir la trotteuse : entre deux blocs, tout
> continue à la vitesse normale.
>
> Le vrai travail est donc là, dans la conversion « décalage d'échantillon →
> secondes de morceau », qui est aussi le code que traversent l'accumulation
> en flottant, la marge d'un quart d'échantillon aux frontières et le
> rebouclage — trois choses qu'un test protège précisément parce qu'elles se
> cassent en silence (`process_graph_loop_renders_the_same_audio_every_turn`).
> Ce n'est pas une étape de deux lignes, c'est une étape qui touche le cœur du
> placement temporel, et elle mérite d'être écrite d'un seul tenant plutôt
> qu'ajoutée à la fin d'une longue session. **Le code a été retiré entièrement
> plutôt que laissé en place** : un varispeed qui ne change pas la vitesse est
> pire que pas de varispeed, parce qu'on le croit.
>
> **SECOND EXAMEN (05/09/2026, 05:30), et il approfondit le diagnostic — la
> conversion du temps n'est que la moitié du travail.** Trois endroits
> convertissent un décalage d'échantillon en secondes de morceau à raison
> d'un pour un : le début de chaque sous-segment d'automation
> (`renderSpan`, ~64 échantillons), la fin de segment de `renderTrackVoice`,
> et le placement des événements. Ceux-là se corrigent.
>
> Mais le quatrième n'est pas une conversion : `AudioTrackSource::mixInto`
> lit `sampleCount` trames CONSÉCUTIVES du fichier à partir d'une position.
> Ralentir l'horloge ne ralentit pas cette lecture — il faudrait lire à un
> autre PAS, c'est-à-dire RÉÉCHANTILLONNER. Un varispeed audio n'est donc pas
> un décalage de position, c'est un changement de vitesse de lecture, et le
> seul endroit du moteur qui sache faire cela est le chemin d'étirement de
> D12 (`ClipWarp`, dont le mode `Repitch` rééchantillonne justement).
>
> **Ce que la prochaine tentative doit savoir** : D18.5 n'est pas « poser un
> facteur sur l'horloge », c'est (a) corriger les trois conversions de temps,
> et (b) faire passer tout clip audio par le rééchantillonnage quand la
> vitesse n'est pas 1 — en préservant le chemin d'aujourd'hui au bit près à
> vitesse normale, puisque c'est lui que tous les rendus existants ont
> emprunté. C'est une étape de moteur, pas une étape de transport.

> **D18.5 EST FAITE À LA TROISIÈME TENTATIVE (05/09/2026, 09:40), et le plan
> que le second examen avait écrit était le bon.** Les deux moitiés ont été
> faites telles qu'annoncées : les conversions de temps, puis le
> rééchantillonnage.
>
> | | x1 | x0,5 | x2 |
> |---|---|---|---|
> | impulsion posée à 1 s | **1,0000 s** | **2,0000 s** | **0,5000 s** |
> | hauteur d'un instrument CALCULÉ | 440,4 Hz | **440,4 Hz** | — |
> | hauteur d'un FICHIER à 440 Hz | 440,4 Hz | **220,2 Hz** | 872,7 Hz |
> | fin d'un clip de 2 s | 2,000 s | **4,000 s** | — |
> | rendu à vitesse 1 contre le rendu d'avant | **écart 0,0 — au bit près** | | |
>
> **CE QUI REND L'ADDITION SANS RISQUE, et c'est une question d'écriture.**
> Chaque conversion est écrite `x * vitesse / sampleRate_` et jamais
> `x * (vitesse / sampleRate_)` : la multiplication par 1,0 est EXACTE en
> IEEE 754, donc à vitesse normale l'expression se réduit littéralement à
> celle d'avant, arrondi pour arrondi. La forme parenthésée aurait divisé puis
> multiplié, changé l'arrondi, et déplacé des échantillons sans rien annoncer.
> Un test l'exige (`speed_one_renders_bit_for_bit_what_it_rendered_before`), et
> les 1 243 tests audio le confirment par ailleurs — dont
> `process_graph_loop_renders_the_same_audio_every_turn`, celui-là même que la
> note d'échec désignait comme le gardien fragile.
>
> Les cinq endroits touchés : l'avance de bloc, la frontière de boucle, le
> sous-segment d'automation (**la moitié qui manquait à la première
> tentative**), la fin de segment de `renderTrackVoice`, et le placement des
> événements — ce dernier en divisant l'écart d'échantillons de MORCEAU par la
> vitesse, l'entier traversant le `double` sans bouger tant qu'elle vaut un.
>
> **LA SECONDE MOITIÉ, celle du fichier lu.** `AudioTrackSource::mixIntoAtSpeed`
> lit à la position `timelineStart + i × vitesse` par le noyau fenêtré de
> D12.1 — le même qui sert au mode « vinyle » de l'étirement, puisque lire à
> une position fractionnaire est le même problème. À `vitesse == 1.0` elle
> DÉLÈGUE à `mixInto` : c'est le même code, pas une approximation qui tombe
> juste. Le noyau appartient au GRAPHE et non à la source, parce que sa table
> dépend du rapport et que la construire alloue : elle est calculée dans
> `setPlaybackSpeed`, sur le fil de l'interface, et **avant** que la nouvelle
> vitesse soit publiée — l'ordre inverse laisserait un bloc lire une table
> calculée pour l'ancien rapport.
>
> **UN POINT OÙ LE RÉSULTAT DIVERGE DE CE QUI AVAIT ÉTÉ ÉCRIT, et la raison.**
> Le tableau annonçait « les clips qui suivent le tempo s'étirent, les autres
> changent de hauteur ». Les seconds font bien ce qui était promis ; les
> premiers, non : à vitesse ≠ 1, un clip étiré est lu par le même noyau et
> change donc de hauteur comme les autres. Ce n'est pas un oubli. La cadence
> d'un étireur est fixée par sa CARTE, pas par l'appel : lui faire rendre à
> mi-vitesse à hauteur constante demande de republier la carte de chaque clip
> en trames de SORTIE à chaque changement de vitesse, donc de modifier des
> objets que le fil audio est en train de lire, aux deux endroits qui les
> construisent (`MainComponent` et `OfflineReconstruction`). C'est une étape à
> part entière, avec son propre danger. Et entre les deux comportements, celui
> qui a été retenu est le plus cohérent : un varispeed qui rééchantillonne TOUT
> est une machine à bande, tandis qu'un varispeed qui change la hauteur de
> certains clips et pas d'autres ferait dépendre le résultat d'un drapeau que
> l'utilisateur avait coché pour une tout autre raison.
>
> **CE QUI NE PEUT PAS ARRIVER, et c'est structurel plutôt qu'heureux.** Un
> varispeed oublié ne se retrouve JAMAIS dans un fichier exporté : les cinq
> chemins de rendu hors ligne de l'application passent tous par
> `vsm::interchange::render*`, qui construit son PROPRE graphe, dont la vitesse
> est celle d'usine. Le varispeed est un outil d'écoute, et il le reste sans
> qu'on ait à y penser.
>
> **L'INTERFACE LE DIT**, comme le critère l'exigeait : un menu « x1 » dans la
> barre de transport, entre « Tap » et l'écoute A/B. Un menu et non un
> curseur — les vitesses utiles se nomment, et un curseur qu'on effleure
> laisserait le morceau à 0,97 sans que rien ne le signale. Toute valeur autre
> que x1 s'affiche en ROUGE : un varispeed qu'on oublie allumé fait chercher
> longtemps pourquoi le morceau ne sonne pas juste. La vitesse est bornée à
> [0,25 ; 4] et n'est écrite NULLE PART dans le projet — c'est un réglage de
> séance, comme l'armement d'une piste.
>
> Tests : 1 815 C++ (244 / 1 243 / 273 / 25 / 11 / 19) et 150 Python, tous
> verts.

> **D18.4 EST FAITE (05/09/2026), et une réduction l'a rendue petite.** UNE
> SECTION N'EST PAS UN OBJET DE PLUS : elle se DÉDUIT des repères — de
> celui-ci jusqu'au suivant —, parce que c'est déjà ainsi qu'on s'en sert. On
> pose « Refrain » au début du refrain, et la section refrain va de là au
> repère d'après. Ajouter un second modèle de « zone nommée » à côté des
> repères aurait donné deux vérités sur la même chose, et c'est toujours la
> seconde qui ment. Seul l'ORDRE est nouveau, et il tient dans une liste
> d'entiers.
>
> `flattenPlayOrder` transporte les notes, les clips, les courbes d'automation
> et les repères ; c'est le SEUL moment où l'ordre touche au matériau — tant
> qu'on n'aplatit pas, on n'a rien cassé et l'on peut essayer autre chose. Une
> note qui déborderait de sa section est COUPÉE à sa fin : laissée entière,
> elle empiéterait sur la section suivante, que personne n'a arrangée ainsi
> (la règle de `splitClips` au bord d'un clip). Chaque créneau reçoit un point
> d'automation à sa valeur d'entrée, sans quoi un créneau qui commence au
> milieu d'un fondu hériterait de la valeur du précédent et le paramètre
> sauterait au raccord. Les notes copiées reçoivent des identifiants NEUFS :
> une section jouée deux fois a produit deux fois les mêmes notes.
>
> CE QUI N'EST PAS TRANSPORTÉ, ET C'EST DIT AVANT D'APLATIR : la carte de
> tempo et celle des signatures. Elles décrivent la ligne de temps, pas les
> sections, et les réordonner demanderait de décider ce que devient un ralenti
> joué deux fois — la réponse n'est pas la même selon qu'on répète un refrain
> ou qu'on déplace une coda. Le panneau prévient quand le morceau a plus d'un
> tempo (`flattenChangesTempoMeaning`) ; sur une reconstruction à tempo
> constant, cela ne change rien.
>
> **LA LEÇON DE D8.3, REPAYÉE ICI.** La première écriture bornait la dernière
> section avec `lastUsedTick()`, qui ne connaît que le matériau MIDI. Vu à
> l'écran : sur un projet à trois repères, la section « C » n'existait pas et
> l'aplatissement en rendait deux au lieu de trois. Une reconstruction faite
> de clips AUDIO n'aurait eu aucune section au-delà de son dernier repère,
> c'est-à-dire, le plus souvent, aucune. C'est `lastSoundingTick()` qu'il
> fallait, exactement comme pour l'export en D8.3 — et un test le tient
> désormais sur un projet sans une seule note.
>
> L'ORDRE N'EST PAS ENREGISTRÉ DANS LE PROJET, et c'est une décision : il ne
> décrit rien du morceau, c'est un brouillon dont le résultat s'écrit dans le
> matériau dès qu'on aplatit. Un projet rouvert avec un ordre de jeu qu'on ne
> se rappelle pas avoir posé serait une surprise, pas un service.
>
> Huit tests `core/` : les sections lues des repères (la dernière court
> jusqu'au matériau, un repère posé après tout ne fait pas de section, sans
> repère il n'y en a aucune) ; aplatir [A, A, B] rend un projet dont le
> PLANNING est celui qu'on entendrait, avec des identifiants distincts ; les
> repères suivent ; une note à cheval est coupée ; un ordre vide ou invalide
> ne touche à rien ; l'automation reçoit sa valeur au raccord ; deux tempos
> sont signalés ; un projet fait uniquement de clips audio a bien ses
> sections. Vu à l'écran (`aplatir:2:0:1` sur A/B/C) : C occupe deux mesures,
> puis A, puis B, repères et clips réordonnés ensemble.

> **D18.2 EST FAITE (05/09/2026), et elle a une liste plutôt que des lanes —
> c'est une décision, pas un raccourci.** Un tronçon dit « de tel tick à tel
> tick, prends telle prise » ; la composite est la suite de ses tronçons et
> rien d'autre, donc elle se RECALCULE au lieu de se recopier, et corriger une
> frontière ne demande pas de tout refaire.
>
> POURQUOI PAS DES LANES SUPERPOSÉES : elles demandent une seconde vue du
> piano roll, avec son défilement, son zoom et sa sélection — c'est-à-dire un
> second éditeur. Une liste de tronçons dit exactement la même chose (« de la
> mesure 1 à 4, la prise 2 »), se lit d'un coup d'œil et se corrige sans viser
> au pixel. Ce qu'elle ne donne pas, et il faut le dire : VOIR les passes pour
> choisir. On les écoute en changeant de prise, ce que le menu Enregistrement
> fait déjà. Si l'usage montre que cela ne suffit pas, les lanes seront une
> étape à part entière — pas un ajout discret à celle-ci.
>
> **LE PIÈGE DU MODÈLE, ET IL EST ÉCRIT DANS `Track.h` DEPUIS D3.5 :** quand
> `activeTake` désigne une prise, le contenu de `takes[activeTake]` est
> PÉRIMÉ — la vérité est dans `notes`. Lire aveuglément `takes[i].notes`
> rendrait donc l'état d'AVANT pour la passe qu'on est en train d'écouter,
> c'est-à-dire précisément celle qu'on vient de juger bonne. Un test l'exerce :
> on choisit la prise 2, on l'édite, on en fait un tronçon, et ce sont les
> notes éditées qui sortent.
>
> Composer RANGE d'abord la passe courante dans sa prise (sans quoi elle
> serait perdue, et c'est souvent l'une de celles qu'on assemble), puis met
> `activeTake` à −1 : une composite n'appartient à aucune prise, et la dire
> active écraserait cette prise-là au prochain changement. Les passes sont
> conservées — on peut recommencer autrement. Une note qui déborderait de son
> tronçon est coupée à sa fin : laissée entière, elle sonnerait par-dessus le
> tronçon suivant, qui vient d'une AUTRE passe. Annulable, et l'instantané
> n'est pris que si la composition produit quelque chose.
>
> Quatre tests `core/` : trois tronçons pris dans trois prises rendent
> exactement les notes de chacune sur sa plage, avec des identifiants
> distincts ; la prise ACTIVE est lue dans la piste et non dans sa copie
> périmée ; composer range la passe qu'on écoutait et n'appartient à aucune
> prise ; une note à cheval est coupée, et un tronçon vide, à l'envers ou qui
> désigne une prise inexistante ne fabrique rien. Vu à l'écran : le panneau
> lit les trois prises de la piste, dit combien de mesures fait le morceau et
> que ce qu'aucun tronçon ne couvre ne sonnera pas.

> **D18.7 A ÉTÉ SCINDÉE EN DEUX, ET SA PREMIÈRE MOITIÉ EST FAITE
> (05/09/2026).** L'étape mêlait deux choses de nature différente : qu'une
> machine SACHE rendre ses voix séparément, et que le graphe les PUBLIE sur
> des pistes. La première est une capacité, close et vérifiable seule ; la
> seconde est un changement de modèle (une piste qui est « la sortie n° k de
> l'instrument de la piste j »), et la mêler à l'autre aurait donné une étape
> qu'on ne peut ni finir ni juger d'un coup. Elles sont donc deux, et c'est
> écrit plutôt que fait en silence.
>
> **D18.7a — la capacité — EST FAITE.** `ISynthPlugin` gagne `outputCount()`
> (une paire par défaut), `outputName(index)` et `processMultiOut(...)`. Le
> DÉFAUT EST L'ANCIEN CHEMIN : une machine qui n'implémente rien rend son
> mixage dans la sortie 0 et du SILENCE dans les autres — vérifié au bit près
> sur le Minimoog, y compris que les sorties qu'elle n'a pas sont mises à
> zéro et non laissées telles qu'on les lui a données. Aucune des soixante
> machines existantes ne change d'une ligne ni d'un échantillon.
>
> Le TR-808 l'implémente : six sorties nommées (grosse caisse, caisse claire,
> charley fermé, charley ouvert, clap, cloche). **La somme des six est ce que
> `process` rend, AU BIT PRÈS** — écart mesuré **0,000e+00** sur une mesure où
> cinq pièces sonnent ensemble (crête 0,876, donc la mesure porte sur du
> son). Ce n'est pas une chance : la marge de 0,5 est appliquée pièce par
> pièce plutôt qu'à la somme, et 0,5 étant une puissance de deux, la
> multiplication est EXACTE — la somme des six moitiés est la moitié de la
> somme, sans arrondi intermédiaire. L'invariant est ce qui rend l'addition
> sans risque : sans lui, un projet sonnerait différemment selon qu'on l'a
> éclaté en pistes ou non.
>
> **D18.7b — la publication sur des pistes — reste à faire**, et c'est elle
> qui sert l'objectif de PARITÉ : une reconstruction qui a séparé la grosse
> caisse de la caisse claire ne doit pas les recoller en les jouant (§ 2 de
> `CDC-detection-multipiste.md`). Elle demande de dire dans le modèle qu'une
> piste porte la sortie n° k d'une autre, et de le faire suivre au fichier, à
> la console et au rendu hors ligne.

> **D18.7b — CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (05/09/2026, 08:10).**
> Le critère de D18.7 disait « la somme des sorties séparées est identique AU
> BIT PRÈS au rendu stéréo d'avant ». Il a été tenu par D18.7a, **et il ne peut
> pas l'être au niveau du GRAPHE** : chaque piste traverse son propre fader et
> sa propre loi de panoramique, donc le mélange calcule `Σ(vₖ·g)` là où la
> piste unique calculait `(Σvₖ)·g`. La multiplication flottante n'est pas
> distributive sur l'addition, et les deux diffèrent du dernier bit. Prétendre
> le contraire serait un critère qu'on contourne au lieu d'un critère qu'on
> tient.
>
> Ce qui est donc attendu, et qui reste falsifiable :
>
> 1. **Ce qu'on entend ne change pas.** Un projet où les six sorties du TR-808
>    sont publiées sur six pistes à gain unité et panoramique centré doit
>    rendre le même master que le même projet sans publication, à l'erreur
>    d'accumulation flottante près — que je MESURE et publie, et dont j'attends
>    qu'elle reste **sous −120 dBFS** (≈ 1e-6 en relatif). Au-delà, ce n'est
>    plus de l'arrondi : c'est une erreur de routage.
> 2. **Rien ne change pour ce qui existe.** Tant qu'aucune piste ne publie, la
>    table de routage n'est même pas allouée et le rendu emprunte le chemin
>    d'avant — vérifié au bit près, pas seulement à l'oreille.
> 3. **Aucune panne muette.** Une piste qui réclame la sortie n° 4 d'une
>    machine qui n'en a que deux sort silencieuse ; ce silence est COMPTÉ
>    (`droppedInstrumentOutputs()`) et non subi.
> 4. **Le fichier reste compatible.** Un projet d'avant l'étape se relit et se
>    réécrit octet pour octet.

> **D18.7b EST FAITE (05/09/2026, 08:55), et les quatre attendus sont tenus.**
> `Track::outputSourceTrack` / `outputIndex` disent qu'une piste porte la
> sortie n° k d'une autre ; le graphe rend alors la machine par
> `processMultiOut`, la sortie 0 restant sur la piste qui porte l'instrument.
>
> | | mesuré | attendu |
> |---|---|---|
> | projet publié contre projet simple | **2,98e-08, soit −143,4 dBFS** | < −120 dBFS |
> | somme des six solos contre le rendu complet | **1,49e-08, soit −149,4 dBFS** | arrondi seul |
> | deux pistes sur la MÊME sortie | **rapport de crête 2,0000** | exactement 2 |
> | projet où personne ne publie | **écart 0,0 — au bit près** | inchangé |
>
> **CE QUE LA MESURE A OBLIGÉ À TRANCHER, ET QUI N'ÉTAIT PAS PRÉVU.** Trois
> tests sur sept sont tombés d'un coup, tous pour la même raison : le
> planificateur n'émet AUCUN événement pour une piste muette
> (`PlaybackScheduler`), si bien que couper la piste porteuse affamait la
> machine et TOUTES ses sorties avec elle. Couper la grosse caisse faisait
> taire la caisse claire. La décision, écrite dans le code : **le muet d'une
> piste coupe SA tranche de console, pas la machine qu'elle porte** — une piste
> dont une autre publie les sorties reste JOUÉE même muette, et son propre
> silence est assuré au mélange, là où il a toujours été. Le solo suit la même
> règle, sans quoi « écouter la caisse claire seule » n'écouterait rien.
>
> **DEUX PIÈGES ÉVITÉS PARCE QU'ILS ÉTAIENT DÉJÀ ÉCRITS AILLEURS.**
> `outputSourceTrack` est un INDEX, comme `outputGroup` : il suit désormais sa
> piste à travers `moveTrack`, `duplicateTrack` et `removeTrack` (supprimer la
> porteuse fait cesser de publier plutôt que pointer dans le vide), et un test
> le tient. Et la table de routage alloue les tampons **par source et tout ou
> rien** : à l'allouer à moitié, deux machines rendues en parallèle auraient
> partagé un même tampon de rebut, c'est-à-dire une course.
>
> **LE PARALLÉLISME N'EST PAS SACRIFIÉ.** Le calcul d'une piste dépend
> maintenant d'une autre, ce qui est exactement ce qui interdit le multicœur
> aux chaînes latérales. Ici le rendu se fait en DEUX VAGUES — les pistes qui ne
> lisent rien, puis celles qui lisent — chacune parallèle ; le MÉLANGE, lui,
> garde l'ordre d'origine, parce que réordonner des additions flottantes
> changerait le dernier bit sans raison.
>
> **CE QUE L'ÉCRAN A MONTRÉ, ET QUI A ÉTÉ CORRIGÉ.** La première capture
> donnait à une piste publiée un sélecteur d'instrument « (Aucun) » — un
> réglage que le graphe ignore délibérément, donc la pire espèce : celui qui se
> règle et ne fait rien. Elle affiche maintenant ce qu'elle porte (« sortie
> n° 1 de TR-808 »), et son bouton d'ARMEMENT a disparu pour la même raison :
> sans instrument, il n'y a pas de clavier à lui livrer. Le mélangeur, lui,
> n'a rien demandé — une piste publiée est une piste, et sa tranche existait
> déjà.
>
> Reste visible dans la capture : sur un nom de source long, la ligne se coupe
> (« sortie n° 1 de TR-808 (Grosse cais... »). C'est la largeur du dock qui
> borne, pas la police ; l'essentiel — le numéro de sortie et la machine — se
> lit.
>
> Tests : 1 808 C++ (244 / 1 236 / 273 / 25 / 11 / 19) et 150 Python, tous
> verts.

> **D18.1 EST FAITE (05/09/2026), et son critère a dû être réécrit par la
> mesure.** « Piste ▸ Reporter la sélection en audio » rend hors ligne les
> clips choisis — une piste neuve par piste source, posée À LA PLACE de la
> sélection, l'originale intacte. C'est ce qui distingue ce geste du report de
> PISTE (D5.5), qui remplace le matériau : ici on pose à côté, et l'on désactive
> l'original si l'on veut entendre le report seul. Le bundle rendu ne garde que
> les clips choisis ; les NOTES restent, puisqu'un clip est une fenêtre sur
> elles. Même rendu que le gel et que le report de piste — trois chemins
> différents finiraient par ne plus sonner pareil. Ce qui échoue est nommé
> piste par piste.
>
> **LE CRITÈRE ÉCRIT UNE HEURE PLUS TÔT DISAIT « identique au rendu du morceau
> sur cette plage ». La mesure l'a réfuté deux fois, et la seconde est la plus
> instructive.** Premier essai, clips voisins : écart **0,459** — c'est la
> QUEUE de la note du clip d'avant, et elle DOIT être absente, sans quoi elle
> s'entendrait deux fois une fois le report posé à côté de l'original. Second
> essai, clips écartés d'une seconde de silence : écart **0,426 encore**, alors
> que plus rien ne sonnait. Les deux rendus contiennent bien la note (crêtes
> 0,327 et 0,358) — elle n'est simplement pas au même endroit de son cycle.
> **Une machine a de la MÉMOIRE** (phase d'oscillateur, charge de filtre, état
> d'enveloppe), et une note précédée d'une autre ne sonne pas échantillon pour
> échantillon comme la même note jouée à froid.
>
> Ce n'est donc pas un défaut à corriger : c'est ce qu'EST un report de
> sélection, chez Cubase comme ici. Le rendu part de zéro (D6.1), ce qui met
> les EFFETS dans l'état où l'oreille les attend ; rien ne peut mettre la
> MACHINE dans l'état que lui aurait donné un matériau qu'on a justement exclu
> — et le voudrait-on qu'il faudrait le rendre, c'est-à-dire ne plus reporter
> une sélection. Le critère juste, et celui que les tests portent désormais :
> le report CONTIENT ce qu'on a choisi, il ne contient PAS ce qu'on n'a pas
> choisi, et l'écart au morceau est NOMMÉ plutôt que promis nul.
>
> Trois tests `interchange/` : le clip choisi sonne et le clip écarté est
> silencieux ; l'écart au morceau est non nul et sa raison est écrite — ce
> dernier test existe pour empêcher une fausse réparation, et s'il tombe un
> jour à zéro c'est que le report aura recommencé à rendre ce qu'on n'avait
> pas choisi.

> **D17.1 EST FAITE (05/09/2026), et le chiffre est celui du manuel.**
> `FadeShape` (`Linear`, `EqualPower`, `Slow`, `Fast`), absente du fichier
> quand c'est la droite — un projet d'avant D17.1 se réécrit octet pour
> octet et sonne au bit près comme avant, parce que la droite reste le
> défaut.
>
> Mesuré, avec le témoin de la même passe et une seule variable (deux bruits
> DÉCORRÉLÉS de graines différentes, une seconde de recouvrement, la forme
> seule change) : **la droite creuse −2,93 dB au point de croisement**, ce
> que la théorie annonce (0,5² + 0,5² = 0,5, soit −3,01 dB) ; **le quart de
> sinusoïde tient à +0,07 dB**. Le raccord s'entendait, et rien ne le disait.
> C'est aussi pourquoi la droite n'est pas fautive et reste proposée : entre
> deux clips CORRÉLÉS — deux prises du même passage, ou le même matériau
> coupé et recollé, c'est-à-dire le montage courant — la somme se fait en
> amplitude, 0,5 + 0,5 fait un, et c'est la droite qui est juste. Les deux
> cas existent, d'où les deux formes ; `Slow` et `Fast` complètent pour
> l'attaque d'un fondu simple.
>
> LA FORMULE VIT DANS LE MODÈLE (`sequencer::fadeShapeGain`) et pas dans le
> moteur, pour la raison du § 6 : le dessin du clip et le son qu'il rend
> doivent sortir de la même. L'arrangement dessinait un TRIANGLE, c'est-à-dire
> une droite quelle que soit la forme jouée — on aurait vu une droite en
> entendant un quart de sinusoïde. Il trace maintenant la vraie courbe, en
> douze segments (c'est un masque, pas un tracé de précision). Le choix est
> au menu du clip, sur toute la sélection comme le muet.
>
> UN PIÈGE PAYÉ DEUX FOIS DANS LA MÊME HEURE, et l'écrire ici est le seul
> moyen de ne pas le repayer : `Clip` ET `ProjectClip` se construisent par
> AGRÉGAT POSITIONNEL. Le champ, glissé la première fois entre
> `fadeOutSeconds` et `gain`, décalait tout ce qui suit ; le compilateur a
> rattrapé les deux (un `float` vers un `enum class`, puis vers un
> `std::string`), mais un champ du même type serait passé sans un mot. Les
> deux structures le disent déjà dans leur commentaire — « placé en dernier,
> volontairement » — et la règle vaut pour tout champ ajouté à l'une ou à
> l'autre. Un test de l'aller-retour vérifie désormais que les champs voisins
> n'ont pas glissé.
>
> Trois tests : le creux mesuré des deux formes (`audio/`), le défaut resté
> à la droite, et l'aller-retour disque avec le fichier inchangé en droite
> (`interchange/`). Vu à l'écran : quatre clips, une forme chacun, et les
> quatre courbes se distinguent à l'œil.


### Phase D18 — Le septième audit : ce qui manque une fois D17 posée (05/09/2026, 01:10)

**Pourquoi.** Même méthode que D11 à D17. L'ordre suit le § 3 : le geste de
tous les jours d'abord, l'outil de travail ensuite, le modèle en dernier
parce qu'il traverse l'interface des machines.

> **UN HUITIÈME MANQUE A ÉTÉ ÉCRIT PUIS RETIRÉ, ET C'EST LA LEÇON DE CET
> AUDIT.** « Exporter une PLAGE » figurait ici, vérifié par un `grep` sur
> `exportRange|renderRange|bounceRange` qui ne rendait rien. C'était FAUX :
> l'export d'une plage existe depuis D6.1, sous le nom
> `RenderOptions::startSeconds`, avec exactement le raisonnement que l'étape
> se proposait d'écrire (« le rendu part toujours de zéro et la plage est
> découpée ensuite… un rendu qui démarrerait à froid produirait un extrait
> que personne n'a jamais entendu »). J'en avais même commencé une seconde
> implémentation dans `OfflineRenderer` — c'est-à-dire deux copies de la
> même règle, ce que ce dépôt refuse partout ailleurs. Elle a été retirée.
>
> **La règle qui en sort, et qui vaut pour tous les audits à venir : on
> cherche le CONCEPT, pas l'identifiant, et on cherche AUSSI dans la feuille
> de route.** Un `grep` sur trois noms qu'on aurait choisis soi-même ne
> prouve rien : il prouve que l'auteur d'avant n'a pas eu les mêmes idées de
> nommage. Les sept manques ci-dessous ont été revérifiés de cette façon —
> sur le concept, dans le code ET dans ce document.

Le relevé a écarté ce qui existe : l'export d'une plage (D6.1), l'écoute
d'entrée, la force de quantification, le gel, le report d'une piste entière,
l'export par piste, les prises conservées, les repères, le suivi de tempo,
le dither à l'export (D14.4).

| Étape | Contenu | Terminé quand |
|---|---|---|
| D18.1 | **Reporter la SÉLECTION en audio.** Reporter une PISTE existe (`kMenuTrackBounce`) ; reporter les clips choisis sur une piste neuve, non — c'est pourtant le geste qui fige une idée sans figer la piste. Cubase : Render in Place ; Live : Freeze & Flatten sur une sélection | « Piste ▸ Reporter la sélection en audio » : les clips choisis sont rendus hors ligne et posés sur une piste audio neuve À LEUR PLACE, la piste d'origine intacte ; le rendu part de zéro et découpe, comme l'export (D6.1) ; annulable ; test : le report CONTIENT ce qui a été choisi et RIEN de ce qui ne l'a pas été |
| D18.2 | **Assembler les prises.** `Track::takes` conserve chaque passe depuis D3.5 et l'on ne peut que CHOISIR la meilleure : impossible de prendre le couplet de la deuxième et le refrain de la quatrième. Cubase : lanes ; Live : take lanes | une prise composite se décrit par une suite de tronçons (prise, début, fin) dans `core/` ; la vue des prises montre les passes empilées, on y dessine la plage qu'on garde ; le matériau courant est RECALCULÉ depuis les tronçons, jamais recopié à la main ; annulable ; test `core/` : trois tronçons pris dans trois prises rendent exactement les notes de chacune sur sa plage |
| D18.3 | **Éditer plusieurs pistes ensemble.** Rien ne lie deux pistes à l'édition : couper une reconstruction multipiste à la mesure 33 demande de couper douze fois, et un tick d'écart casse la phase entre deux micros. Cubase : Edit Groups | `Track::editGroup` (0 = aucun, absent du fichier), et les gestes de TEMPS de `ClipEdit` (couper, déplacer, joindre) s'appliquent à toutes les pistes du même groupe, au même tick ; test `core/` : couper une piste d'un groupe de trois coupe les trois au même tick, et une piste hors groupe n'est pas touchée |
| D18.4 | **L'ordre de jeu.** Les repères nomment des endroits (D16.4) mais rien ne nomme des SECTIONS ni ne les rejoue dans un autre ordre : essayer « couplet, couplet, refrain » demande de tout recopier. Cubase : piste d'Arrangement | des sections nommées (début, fin), déduites des repères ou dessinées ; une liste d'ordre de jeu ; « Aplatir » écrit le résultat comme du vrai matériau, et c'est le SEUL moment où le projet change ; test `core/` : aplatir [A, A, B] rend un projet dont le planning est celui qu'on entendrait |
| D18.5 | **La vitesse de lecture.** Aucun varispeed : on ne peut pas ralentir pour relever un passage. Cubase : Varispeed ; Live n'en a pas besoin parce que tout y suit le tempo, ce qui n'est pas notre cas (un clip audio ne suit le tempo que si on le lui demande, D12) | un facteur de vitesse appliqué à l'HORLOGE du transport, sans toucher au projet ni au tempo ; les clips qui suivent le tempo s'étirent, les autres changent de hauteur — c'est un varispeed, pas un étirement, et l'interface le dit ; test `audio/` : à 0,5, une impulsion posée à 1 s sort à 2 s |  ⟵ **FAITE À LA TROISIÈME TENTATIVE (05/09/2026) : voir les notes.**
| D18.6 | **Les notes du projet.** Rien pour écrire « la basse vient du stem `other`, la nappe est une hypothèse » : une reconstruction est pleine de décisions dont il ne reste aucune trace, et c'est précisément ce projet-ci qui en produit le plus | un texte libre par projet, écrit dans `project.json`, montré dans une fenêtre ; test `interchange/` : aller-retour, et fichier inchangé octet pour octet quand le texte est vide |
| D18.7 | **Une machine ne sort que sur DEUX canaux.** `ISynthPlugin::process` rend L/R : les huit voix d'un TR-808 arrivent mixées, et une reconstruction qui a séparé la grosse caisse de la caisse claire les recolle. C'est le § 2 de `CDC-detection-multipiste.md` qui le demande, et l'objectif de parité qui le paie | `ISynthPlugin` sait dire combien de sorties il a et les rendre séparément (défaut : une paire, aucune machine existante ne change) ; `ProcessGraph` publie chaque sortie sur une piste ; test `audio/` : la somme des sorties séparées est identique AU BIT PRÈS au rendu stéréo d'avant |  ⟵ **FAITE, EN DEUX MOITIÉS (05/09/2026) : voir les notes.**

**Ce que l'audit a écarté, et pourquoi.** Les zooms mémorisés (le zoom sur la
sélection et le zoom « tout voir » de D14.2 couvrent l'usage réel) ; le motif
d'accentuation du métronome (le clic accentue déjà le premier temps, et un
motif réglable est un séquenceur de plus) ; l'automation par Bézier (D17.7 a
tranché pour la puissance, qui s'inverse et se compose) ; le mode « ripple »
où tout ce qui suit se décale (l'insertion et la suppression de temps entre
locateurs, D13.3, font le même travail en le disant).

## 4. Les choix tranchés ici, et pourquoi

Conformément à l'usage de ce dépôt, les questions ouvertes se referment en
écrivant. Sept l'étaient.

**8 — ajouté le 31/08/2026 : la FENÊTRE UNIQUE devient le défaut, et ce n'est
pas la vue Session.** L'application vivait en six fenêtres — un socle réduit à
la barre de transport, et cinq flottantes (pistes, piano roll, rack, console,
arrangement) à repositionner à chaque session. Ce qu'on regarde ensemble doit
vivre ensemble : les cinq panneaux s'ancrent désormais DANS la fenêtre
principale — pistes à gauche, morceau au centre (arrangement ou piano roll, on
passe de l'un à l'autre comme avant), rack à droite, console en bas — chaque
volet masquable depuis Affichage, l'espace rendu au centre. « Fenêtres
flottantes » reste disponible au même menu, et le choix survit au redémarrage.
La décision n° 1 ci-dessus n'est PAS rouverte : rien ici ne touche au modèle
temporel — c'est la disposition qui change, justifiée par l'usage (la doctrine
du § 5 : jamais par la ressemblance).

Un piège d'implémentation vaut d'être écrit : une `ResizableWindow` qui garde
son pointeur de contenu le REPLAQUE à sa taille à chaque `resized()`, même
re-parenté — il faut `clearContentComponent()` avant d'ancrer, sans quoi un
panneau s'étale plein cadre par-dessus les autres. Et l'application sait
désormais se photographier (`VSM_CAPTURE=sortie.png` : rendu de la fenêtre en
PNG après deux secondes, puis sortie) — sous Wayland, aucun outil externe ne
sait ni viser cette fenêtre ni la passer devant un terminal, et une interface
qu'on ne peut pas regarder est une interface qu'on ne peut pas juger.

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
- ~~**La compatibilité avec les projets de Cubase, Live ou FL.**~~ **CE REFUS
  EST LEVÉ (02/09/2026), et il faut dire pourquoi il était mal posé.** Il tenait
  en une phrase — « lire un `.flp` ou un `.als` est de la rétro-ingénierie sans
  fin » — qui mélangeait trois formats très différents. Un `.als` est du **XML
  gzippé et lisible**, dont les balises se nomment elles-mêmes (`MidiTrack`,
  `MidiNoteEvent`) : il n'y a là aucune rétro-ingénierie, seulement un
  décompresseur à écrire. Un `.flp` est binaire, mais sa STRUCTURE se vérifie
  toute seule — un découpage d'événements faux n'atteint pas la fin du fichier
  exactement — et seul le SENS de ses identifiants est reconstitué, ce que le
  rapport d'import chiffre poste par poste. Le seul format où la phrase était
  juste est le `.cpr` de Cubase, et **c'est le seul qu'on ne lit pas** : on
  explique à la place, en nommant les deux chemins qui marchent.
  Voir `docs/CDC-import-daw.md`. Le refus valait donc pour un format sur trois,
  et le tenir pour les trois privait le musicien de ses projets Live et FL sans
  raison mesurée.
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
   quand une piste audio lit 47 Mo depuis le disque. **Et pour TOUTES les
   machines depuis le 02/09/2026** : le test ne montait que huit pistes de
   `vsm.minimoog` et ne disait donc rien des trente-huit autres — une
   machine qui aurait alloué dans `process()` traversait la suite entière
   sans être vue. Il parcourt désormais le registre et fait jouer chaque
   machine à son tour, en NOMMANT la fautive s'il y en a une. C'est
   exactement la forme de garde-fou que le § 6 décrit : il gardait, et il
   ne gardait qu'un trente-neuvième du parc. **Et depuis le 04/09/2026, il
   monte aussi les seize effets d'insert sur une piste et fait jouer un clip
   qui suit le tempo (vocodeur, WSOLA, rééchantillonné) et un clip à
   l'envers** : D12, D13 et D14 avaient ajouté des chemins dans `process()`
   que le garde-fou ne voyait pas. Vérifié : aucune allocation, aucun
   verrou, aucune entrée-sortie.
   Le même jour, l'invariant n° 3 s'est étendu aux effets : chacun des seize
   effets d'insert, réglé à 60 % de chaque paramètre, rend à l'échantillon
   près le même signal à 128, 256, 1024 et 2048 échantillons par bloc qu'à
   512 (`chaque_effet_d_insert_rend_pareil_quelle_que_soit_la_taille_de_bloc`).
   Hypothèse écrite avant la mesure, confirmée sans écart ; les clips étirés
   l'étaient déjà depuis D12.5.
3. **Rendu temps réel et rendu hors ligne restent identiques**, à l'échantillon
   près, sur tout ce qui s'ajoute. ~~Le test existe pour CLAP ; il s'étend.~~
   **Il s'est étendu le 31/08/2026, a trouvé une exception, et l'exception a
   été corrigée le jour même — identité stricte à toutes les tailles de bloc,
   frontière comprise (note ci-dessous).**
4. **Le MOTEUR se compile et s'utilise sans Python, sans réseau, sans CLAP.**
   L'application, elle, exige JUCE, récupéré depuis GitHub au premier
   configure : hors ligne, lui désigner une copie locale
   (`-DFETCHCONTENT_SOURCE_DIR_JUCE=...`). Mesuré le 31/08/2026 avec
   `FETCHCONTENT_FULLY_DISCONNECTED=ON` — détail au § 8 de
   [`ROADMAP-fusion.md`](ROADMAP-fusion.md). Chaque phase le revérifie : c'est
   le genre de garantie qu'on perd sans s'en apercevoir.
5. **Rien ne se perd et rien ne ment.** Toute fonction ajoutée est sauvegardée
   dans le projet, présente à l'export, et sans commande morte. C'est l'acquis
   de D0, et le reperdre serait pire que ne l'avoir jamais eu.

> **L'INVARIANT N° 3, ÉTENDU LE 31/08/2026 : L'EXCEPTION TROUVÉE PUIS CORRIGÉE
> LE JOUR MÊME.** Rendu à neuf tailles de bloc, le graphe divergeait quand une
> fin de note tombait PILE sur une frontière (~−76 dB dans la queue de
> relâchement). Cause double, dans la distribution des événements aux blocs :
> l'appartenance se décidait en SECONDES accumulées — l'erreur d'accumulation
> faisait entrer l'événement de frontière dans le bloc de trop — puis un clamp
> le rabattait sur le dernier échantillon, relâchement un échantillon trop tôt.
> Correction : l'appartenance se décide en ÉCHANTILLONS ABSOLUS ARRONDIS
> (`llround(t x sr)`), un quart d'échantillon de marge sur la borne de
> recherche, l'offset tranche, AUCUN clamp — un clamp déplace, et un événement
> déplacé est un événement faux. La borne basse du test-mémoire a échoué au
> premier build corrigé, exactement comme son en-tête le promettait ; les tests
> (`test_process_graph_determinism.cpp`) exigent désormais l'IDENTITÉ STRICTE
> partout, deux canaux, et les empreintes audio sont inchangées (801 verts).
>
> **UNE QUESTION OUVERTE EN EST SORTIE, ÉCRITE PLUTÔT QUE TUE.** Le test « un
> motif bouclé ne respire pas » passait PAR ACCIDENT : l'état de synthèse
> traverse le rebouclage, et la répétabilité bit-à-bit d'un tour n'est PAS une
> propriété du moteur — sur le moteur d'AVANT la correction, une note de
> 241 ticks divergeait déjà entre tours, et vingt tours ne se répètent jamais
> (seize voix, pas de période simple). Le test affirme désormais la garantie
> réelle : deux exécutions complètes du même rebouclage sont identiques au bit
> près.
>
> **LA QUESTION EST FERMÉE LE LENDEMAIN, PAR LA MESURE (01/09/2026).** L'état
> qui traverse : **la phase de l'oscillateur et la mémoire du filtre des voix
> réutilisées.** `Voice::noteOn` ne remet ni l'une ni l'autre à zéro ; une voix
> inactive n'est plus traitée (`if (v.isActive())`), son état GÈLE à la
> désactivation et repart tel quel à la réutilisation — et l'allocateur reprend
> le premier emplacement libre, donc la même voix, tour après tour. Prouvé sans
> boucle : deux notes IDENTIQUES espacées dans un rendu linéaire, voix libérée
> entre les deux, diffèrent jusqu'à **0,124** d'amplitude. Mesuré aussi :
> désactivation à l'échantillon 21 600 exactement (relâchement de 9 600 pile) —
> et l'arithmétique de phase seule prédit alors l'INVERSE des répétitions
> observées (198,000 cycles/tour au moteur corrigé, 197,991 à l'ancien) : la
> parité fine des tours de l'ancien moteur reste inexpliquée, et elle est dite
> telle quelle plutôt qu'habillée.
>
> **DÉCISION : LE COMPORTEMENT EST CONSERVÉ.** La réutilisation sans remise à
> zéro est le motif commun des voix du parc — c'est elle qui donne aux machines
> vintage leurs notes jamais deux fois identiques, et c'est voulu. La remise à
> zéro ne rendrait au rebouclage une répétabilité bit-à-bit qu'au prix d'un
> changement d'empreinte audio (invariant n° 1) pour un gain que l'oreille ne
> demande pas. Ce que le moteur garantit — le déterminisme entre exécutions —
> est testé ; ce qu'il ne garantit pas — la répétition bit-à-bit d'un tour de
> boucle — est écrit ici, avec sa cause.
>
> **ET IL NE L'ÉTAIT QU'AU TIERS (31/08/2026).** L'invariant interdit TROIS
> choses — allocation, verrou, I/O — et le test ne comptait que la première,
> tout en citant la phrase entière. Les verrous bloquants et les
> entrées-sorties se comptent désormais par la même interposition de symbole
> que `operator new`, et le verdict est **zéro** dans les quatre scénarios,
> diffusion disque comprise — POUR LES PRIMITIVES COUVERTES : le contrat exact
> (ce que les compteurs voient, et la liste de ce qu'ils NE voient PAS —
> `rwlock`, sémaphores, `open`, `mmap`, lectures `FILE*`…) est écrit en tête de
> `test_no_allocation_in_process.cpp`, à l'endroit qu'il faudra élargir si le
> moteur adopte une primitive non couverte. Un garde-fou du garde-fou vérifie
> que les compteurs voient un verrou et une lecture réels quand il y en a.
>
> **L'INVARIANT N° 2 EST MESURÉ DEPUIS LE 30/08/2026, ET IL NE L'ÉTAIT PAS.**
> D2.2 en faisait déjà un critère — « un test compte les allocations » — et ce
> test n'existait pas : la règle était tenue par la relecture, c'est-à-dire par
> l'attention de celui qui écrivait. C'est exactement le genre de garantie que
> le § 6 dit qu'on perd sans s'en apercevoir, et D8.2 venait de refaire tout ce
> chemin-là. `audio/tests/test_no_allocation_in_process.cpp` remplace
> `operator new` pour le binaire de tests et compte, sur quatre configurations :
> huit machines, une piste audio résidente, **une piste audio diffusée depuis le
> disque** — le cas que l'invariant nomme — et le rebouclage avec automation,
> qui sont les deux chemins qui découpent le bloc.
>
> **LE COMPTEUR EST PROPRE À CHAQUE THREAD**, et ce n'est pas un détail : le
> thread de diffusion disque a parfaitement le droit d'allouer — c'est même
> pour cela qu'il existe. Un compteur global le prendrait pour une faute du
> thread audio et ferait échouer le test au hasard, selon le moment où le disque
> a répondu.
>
> **LE GARDE-FOU DU GARDE-FOU A SERVI DÈS LE PREMIER LANCEMENT.** Le test qui
> vérifie que le compteur compte quelque chose a échoué : écrit avec un
> `std::vector` local, l'allocation était **éliminée par le compilateur**
> (l'élision d'allocation est expressément permise depuis C++14). Les trois
> autres tests passaient alors pour la pire des raisons — ils mesuraient un
> compteur qui ne comptait rien. Vérifié dans l'autre sens aussi : une
> allocation ajoutée exprès en tête de `processBlock` les fait tous échouer.
>
> **ET LA MESURE EST COURTE POUR UNE RAISON QU'IL FAUT ÉCRIRE** : deux cents
> blocs se rendent ici en une milliseconde, alors qu'ils durent deux secondes à
> l'écoute. Le thread de diffusion, qui se réveille toutes les dix
> millisecondes, ne peut pas suivre — et il a raison de ne pas suivre, puisque
> personne ne joue mille fois plus vite que le temps réel. On mesure donc dans
> la fenêtre déjà lue, qui est le régime permanent de la lecture.

### Phase D19 — Le huitième audit : ce qui manque une fois D18 posée (05/09/2026, 08:12 — les heures de cette phase sont celles des commits ; celles écrites d'abord, de 10:10 à 12:05, venaient d'une horloge estimée et non lue)

**Pourquoi.** Même méthode que D11 à D18, et le même ordre du § 3 : le geste de
tous les jours d'abord, l'outil de travail ensuite, le modèle en dernier.

**LA RÈGLE DE D18 A ÉTÉ APPLIQUÉE, ET ELLE A SERVI DEUX FOIS.** L'audit
précédent avait écrit un manque qui n'en était pas un, et en avait tiré la
règle : *on cherche le CONCEPT, pas l'identifiant, et on cherche AUSSI dans la
feuille de route.* Deux candidats de cet audit-ci sont tombés à cette
vérification, et il faut le dire plutôt que de faire comme s'ils n'avaient
jamais été écrits :

- **« Les changements de signature rythmique »** — absent de tous les
  documents, donc plausible. Faux : `Project::timeSignatureMap` porte une
  CARTE de signatures depuis longtemps (`core/include/vsm/sequencer/
  TimeSignatureMap.h`). Le mot n'était nulle part, la chose y était.
- **« Nommer les pièces d'une batterie plutôt que leurs hauteurs »** — le mot
  « drum map » n'apparaît ni dans `docs/` ni dans le code, et une piste de
  batterie reconstruite est bien une rangée de 36/38/42/46. Faux également :
  `PianoRollComponent` appelle déjà `drumVoiceName(instrumentId, note)` et
  affiche « charleston fermé » là où l'on attendrait « F#2 », en tirant le nom
  de la machine assignée ou, à défaut, de la convention General MIDI.

**ET UN TROISIÈME EST PASSÉ, QUE LA RÈGLE N'A PAS SUFFI À ARRÊTER.** « D19.1 —
transformer les vélocités » a d'abord été écrit comme un manque entier. Il ne
l'était pas : `NoteEdit` expose `setVelocity`, `scaleVelocity`, `rampVelocity`
et `randomizeVelocity` depuis longtemps, et les quatre sont câblées au piano
roll. La recherche avait porté sur `scaleVelocities|compresserVelocites|
velocityScale` — c'est-à-dire sur des noms que j'avais inventés moi-même, au
pluriel et dans une autre casse que ceux du dépôt. C'est exactement le travers
que la règle de D18 dénonçait, commis une ligne après l'avoir recopiée.

**La règle se précise donc, et c'est la vraie leçon de cet audit : on ne
cherche pas un manque par `grep`, on LIT LA SURFACE du module qui porterait la
fonction.** Vingt lignes d'en-tête de `NoteEdit.h` auraient répondu tout de
suite, là où trois motifs devinés ont répondu « absent » avec assurance.
L'étape D19.1 a été RÉDUITE à ce qui manque vraiment plutôt que retirée : il
reste deux transformations, et elles servent la réparation d'une transcription.

Les trois manques restants ont été revérifiés de cette façon : la surface des
opérations de piste de `Project.h` en compte quatre (`removeTrack`,
`moveTrack`, `duplicateTrack`, `publishInstrumentOutputs`) et aucune n'éclate
une piste ; `Track::Kind` vaut `{Midi, Audio, Group}` et rien d'autre.

Le relevé a également écarté, comme existant : l'export d'une plage (D6.1), le
gel, le report d'une piste et d'une sélection (D5.5, D18.1), les prises et leur
assemblage (D3.5, D18.2), les repères et les sections (D16.4, D18.4), les
groupes de mixage (D4.2), les groupes d'édition (D18.3), la chaîne latérale,
l'automation par courbes et ses modes W/R (D5.4, D16.8), le suivi de tempo et
l'étirement (D12), les fondus et leurs formes (D17.1), les notes du projet
(D18.6), la publication des sorties d'instrument (D18.7), le varispeed (D18.5),
l'humanisation et le swing, les gammes et les accords, l'arpège.

Quatre manques ont survécu à la vérification.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D19.1 | **Comprimer et limiter les vélocités** (ÉTAPE RÉDUITE : voir la note ci-dessus — l'essentiel existait déjà). `NoteEdit` sait déjà poser (`setVelocity`), mettre à l'échelle (`scaleVelocity`), dégrader (`rampVelocity`) et humaniser (`randomizeVelocity`), et les quatre sont câblées au piano roll. Manquent les deux transformations qui servent une TRANSCRIPTION plutôt qu'une intention musicale : resserrer des nuances bruitées vers leur moyenne, et les contenir dans un intervalle. Cubase : Logical Editor / Velocity | `compressVelocity` et `limitVelocity` dans `NoteEdit`, fonctions pures sur une sélection, aucune note déplacée ; test `core/` : comprimer à 0 rend toutes les vélocités égales à la moyenne arrondie, comprimer à 1 ne change RIEN au bit près, et limiter est idempotent |
| D19.2 | **Retrouver une piste.** La parité pousse le nombre de pistes vers le haut — D18.7b vient d'en ajouter cinq pour une seule boîte à rythmes — et la liste n'a ni filtre ni recherche : on fait défiler. Cubase : Track Visibility / filtre ; Live : le repli des groupes | un champ de filtre au-dessus de la liste des pistes ; il masque les pistes dont le nom ne correspond pas, SANS toucher à `Track::hidden` (qui est un état du morceau, D17.4) ni au son ; vidé, tout revient ; vérifié à l'écran |
| D19.3 | **Éclater une piste par hauteur.** Une piste de batterie reconstruite porte la grosse caisse, la caisse claire et le charley sur une seule ligne de temps. La chaîne d'analyse les a SÉPARÉS ; le DAW ne sait pas refaire ce geste à la main, ni le défaire. C'est le pendant manuel de l'objectif de parité, et le compagnon de D18.7b — qui a donné une piste à chaque SORTIE, quand celle-ci en donne une à chaque HAUTEUR. Cubase : Dissolve Part | « Piste ▸ Éclater par hauteur » : une piste neuve par hauteur présente, nommée par la pièce (`drumVoiceName`) quand la machine la nomme, insérées après l'originale, l'instrument recopié ; annulable ; les index de routage suivent comme en D18.7b ; test `core/` : éclater trois hauteurs rend trois pistes dont la réunion des notes est exactement le matériau d'origine |
| D19.4 | **Les pistes dossier.** `Track::folded` replie UNE piste ; rien ne replie un GROUPE de pistes. Un groupe de mixage (D4.2) est un bus, pas un rangement : router huit micros de batterie dans un bus ne les fait pas disparaître de la vue quand on travaille sur les cordes. Cubase : Folder Tracks | une piste de type dossier qui CONTIENT des pistes, se replie et les masque toutes, et dont le repli est écrit dans `project.json` ; elle ne touche à aucun signal — un dossier n'est pas un bus, et un projet qui n'en a pas garde son fichier octet pour octet ; test `interchange/` : aller-retour, et absence totale du fichier quand il n'y a aucun dossier |

> **D19.4 EST FAITE (05/09/2026, 08:40), ET LA PHASE D19 EST CLOSE.**
> `Track::Kind::Folder` et `Track::folderDepth`, tous deux absents du fichier
> quand ils valent le défaut — un projet sans dossier garde son fichier octet
> pour octet, et un test l'exige.
>
> **LE CONTENU SE LIT PAR CONTIGUÏTÉ, PAS PAR UN INDEX DE PARENT**, et le choix
> est motivé par ce que cette session a payé ailleurs. Un dossier de
> profondeur `d` contient les pistes qui le suivent tant qu'elles sont plus
> profondes que lui — le modèle de Cubase. L'alternative, un index de parent,
> serait plus directe à lire mais devrait être RÉPARÉE dans `moveTrack`,
> `duplicateTrack` et `removeTrack` : c'est exactement ce qu'il a fallu faire
> pour `outputSourceTrack` en D18.7b, et c'est là que ce genre de référence
> pourrit en silence. Une profondeur ne référence personne — déplacer une piste
> ne peut pas la faire pointer sur le mauvais dossier, au pire elle change de
> tiroir, ce qui est ce qu'on voulait en la déplaçant.
>
> **L'INVARIANT EST RÉTABLI PLUTÔT QUE SUPPOSÉ.** `normalizeFolderDepths` fait
> qu'une piste ne peut être plus profonde que « la précédente + 1 », et ne peut
> descendre d'un cran que si la précédente est un DOSSIER — sinon elle
> prétendrait être rangée dans une piste ordinaire, et le contenu lu serait
> celui que personne n'a voulu. Elle est appelée après chaque changement de
> profondeur, elle est idempotente, et un arbre légitime la traverse sans une
> correction.
>
> **UN DOSSIER NE TOUCHE À AUCUN SIGNAL, et c'est là que ce dépôt s'écarte de
> Cubase en le disant.** Cubase donne à ses dossiers un muet et un solo qui
> agissent sur leur contenu ; ce serait ici un bus déguisé, et le rangement
> cesserait d'être gratuit — on ne pourrait plus replier huit micros de
> batterie sans se demander si l'on vient de changer le mélange. La rangée d'un
> dossier n'a donc ni fader, ni panoramique, ni muet, ni solo, ni armement, ni
> sortie : elle porte un chevron, un nom, et la mention « dossier (ne joue
> rien) ».
>
> **CE QUE L'ÉCRAN A CORRIGÉ, une deuxième fois dans cette session.** La
> première capture montrait un dossier avec un fader — un réglage sans effet,
> la pire espèce — et sans sa mention, parce que `resized()` ne donnait de
> bornes à l'étiquette que pour trois genres de piste sur quatre. Les deux se
> voient d'un coup d'œil et ne se déduisent d'aucun test.
>
> Trois gestes qui se composent, plutôt qu'une grande commande qui devinerait
> ce qu'on veut ranger : « Ranger cette piste dans un dossier neuf », « Entrer
> dans le dossier du dessus », « Sortir du dossier ». Sortir un DOSSIER emmène
> ce qu'il contenait — sans quoi ses pistes se retrouveraient rangées dans le
> voisin d'à côté.
>
> Vérifié à l'écran, replié et déplié côte à côte : le chevron passe de ▾ à ▸,
> les deux pistes rangées disparaissent, la piste qui suit le dossier ne bouge
> pas, et les pistes contenues restent en retrait quand il est ouvert.
>
> Tests : 1 830 C++ (258 / 1 243 / 274 / 25 / 11 / 19) et 150 Python, tous
> verts.


> **D19.2 EST FAITE (05/09/2026, 08:29).** Un champ « Filtrer les pistes... »
> sur sa propre ligne sous la barre d'outils — pas serré entre deux boutons :
> entre « ça tient dans la case » et « ça se lit », c'est la lisibilité qui
> prime. La comparaison ignore la casse : on tape « caisse » pour trouver
> « Caisse claire », et personne ne devrait avoir à deviner la majuscule.
>
> **DEUX RAISONS DE NE PAS PARAÎTRE, ET ELLES NE SE MÉLANGENT PAS.**
> `Track::hidden` (D17.4) appartient au MORCEAU et se sauvegarde ; le filtre
> appartient à la SÉANCE et ne s'écrit nulle part. Rouvrir un projet ne cache
> donc jamais une piste, et le filtre ne touche ni à `hidden` ni au son — la
> capture le montre : la liste ne garde que ce qui correspond pendant que le
> mélangeur garde toutes ses tranches.
>
> **PANNE MUETTE INTERDITE, JUSQUE DANS UNE LISTE VIDE.** Un filtre qui ne
> trouve rien laissait un panneau vierge, et un panneau vierge ressemble à des
> pistes supprimées. Il dit maintenant « Aucune des 4 pistes ne porte ce nom.
> Elles jouent toujours — videz le filtre. » Le NOMBRE compte : il apprend du
> même coup que les pistes sont là.
>
> **UNE VARIABLE D'ENVIRONNEMENT DE PLUS, ET POUR LA RAISON HABITUELLE.** Un
> champ de saisie ne se remplit qu'au clavier, et une capture d'un champ VIDE
> ne prouve pas qu'un filtre filtre. `VSM_FILTRE=texte` le pose avant la
> capture, comme `VSM_VUE` pilote le menu Affichage : le dépôt refuse de
> déclarer une interface invérifiable, et cela vaut aussi pour celle-ci.
>
> Vérifié à l'écran sur quatre valeurs. Le projet d'essai n'expose que quatre
> pistes — son fichier MIDI n'en porte que quatre, quoi qu'en dise son
> `project.json` — de sorte que « clap » et « voix » ne trouvent RIEN et que
> c'est la bonne réponse. C'est « caisse » qui fait la démonstration : deux
> pistes sur quatre restent, les deux autres s'effacent, le mélangeur ne
> bouge pas.


> **D19.1 EST FAITE (05/09/2026, 08:22), réduite à ce qui manquait vraiment.**
> `compressVelocity(notes, sélection, amount)` resserre vers la moyenne de la
> SÉLECTION — pas de tout le morceau, sans quoi deux compressions successives
> ne donneraient pas ce que la sélection réunie donne — et `limitVelocity`
> RAMÈNE dans un intervalle au lieu d'y remettre à l'échelle, ce qui la rend
> idempotente : c'est la propriété qu'on attend d'une limite.
>
> Deux détails valent d'être dits parce qu'ils ne se voient pas. La moyenne est
> ARRONDIE avant d'être distribuée : c'est ce qui fait qu'à `amount = 0` toutes
> les notes reçoivent le MÊME entier, et non des arrondis voisins d'un même
> réel. Et à `amount = 1` la vélocité n'est pas recalculée puis réécrite
> identique — la fonction sort avant d'y toucher. Faire reposer l'exactitude
> d'un cas neutre sur un arrondi qui tombe juste est la façon dont on découvre,
> six mois plus tard, qu'une note sur mille a bougé d'un cran.
>
> Bornes saisies à l'envers : lues dans le bon sens plutôt que refusées en
> silence — deux nombres inversés sont une faute de frappe, pas une demande
> d'ignorer le geste. Les deux commandes sont dans le menu contextuel du piano
> roll, sous les quatre qui existaient déjà, et libellées par ce qu'elles font
> (« Resserrer les nuances de moitié ») plutôt que par leur nom technique.

> **D19.3 EST FAITE (05/09/2026, 08:22).** « Piste ▸ Éclater par hauteur » pose
> une piste par hauteur présente, nommée par la pièce quand la machine sait la
> nommer.
>
> **RIEN N'A ÉTÉ RÉÉCRIT POUR LE NOMMAGE**, et c'est le faux manque n° 2 de cet
> audit qui l'a évité : `drumVoiceName(instrumentId, note)` existait déjà et
> sert au piano roll depuis longtemps. L'application la passe à `core/` en
> paramètre, pour la même raison que `ticksToSeconds` dans `spansFromTrack` —
> `core/` ne connaît pas les machines et n'a pas à les connaître.
>
> **LA PLUS GRAVE RESTE SUR LA PISTE D'ORIGINE**, comme la sortie n° 0 reste
> sur la piste qui porte la machine en D18.7b. Deux raisons : aucune piste vide
> n'est laissée derrière, et la piste d'origine garde son nom, ses inserts et
> son automation. **Les notes sont DÉPLACÉES et jamais copiées** : un test
> vérifie que la réunion des pistes obtenues est exactement le matériau de
> départ, identifiant par identifiant — copier ferait sonner chaque pièce deux
> fois.
>
> **LES CLIPS SUIVENT, avec des identifiants NEUFS.** Un clip est une FENÊTRE
> sur le matériau, pas un conteneur : la découpe de la piste vaut pour chacune
> de ses pièces, et l'oublier ferait sonner les pièces éclatées là où
> l'originale se taisait. Deux clips qui partageraient un identifiant feraient
> agir toute sélection sur les deux.
>
> Une piste à une seule hauteur rend **zéro** plutôt que de poser une piste
> vide : la commande n'a pas échoué, elle n'avait rien à faire, et l'entrée de
> menu le dit d'avance en affichant le nombre de hauteurs qu'elle trouverait.
>
> Tests : 254 core (dont 4 pour D19.3 et 6 pour D19.1), 1 243 audio, 273
> interchange — tous verts.


### Phase D20 — Le neuvième audit : ce qui manque une fois D19 posée (05/09/2026, 10:25)

**Pourquoi.** Même méthode que D11 à D19, même ordre du § 3 : le geste de
tous les jours d'abord, l'outil de travail ensuite, le modèle en dernier. Et
la règle de D19 appliquée à la lettre : **chaque manque a été cherché en
LISANT la surface du module qui le porterait** — `NoteEdit.h`, `ClipEdit.h`,
`Project.h`, `SilenceDetection.h`, les en-têtes de `PianoRollComponent` et
d'`ArrangementComponent`, et la construction des menus — puis dans ce
document, sur le concept et non sur un nom deviné.

Le relevé a écarté, comme existant : le legato et la suppression des
chevauchements (`applyLegato`, `removeOverlaps`), le renversement et le
miroir (`reverseNotesInTime`, `mirrorNotesPitch`), les gammes, les accords
et l'arpège, les notes fantômes, la ligne de vélocité, le groove (D17.8), les
fondus et leurs formes (D17.1), le fondu enchaîné sur un chevauchement
(D13.1), la normalisation, l'envers et le rognage au son d'un clip audio
(D13.6, D13.4, D17.6), le suivi de tempo et ses marqueurs (D12), la
transposition à la lecture (D17.5), la sauvegarde automatique, la hauteur
réglable des pistes de l'arrangement, l'export d'une plage et des stems
(D6.1, D6.2), la reconstruction d'un fichier entier depuis l'application
(D9).

**Et il a écarté quatre candidats en le disant.** Le *modèle de piste*
(Cubase : Track Presets) — le modèle de projet (D11.6) couvre l'usage réel de
démarrer vite, et les pistes d'un projet reconstruit sont fabriquées par la
chaîne, pas reprises d'un projet à l'autre ; à rouvrir le jour où l'on ajoute
souvent la même piste à des projets différents. La *suppression des
doublons* (Cubase : Delete Doubles) — la transcription n'écrit jamais deux
notes identiques au même tick, et `removeOverlaps` puis `joinNotes` couvrent
ce qu'elle produit réellement. Le *découpage aux silences* en plusieurs clips
(Cubase : Detect Silence) — D17.6 rogne, et le découpage aux transitoires
ci-dessous en est le complément naturel ; il viendra avec lui s'il manque
encore. La *transposition d'un clip audio* — l'effet d'insert de hauteur
(D13.8) existe et s'applique à la piste : le concept est là.

Cinq manques ont survécu.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D20.1 | **Répéter un clip en copies indépendantes.** (ÉTAPE RÉDUITE EN L'ÉCRIVANT : la première version disait « poser un motif d'une mesure sur seize demande seize gestes » — c'est FAUX, un clip plus long que sa fenêtre la REJOUE en boucle depuis D1, `test_clips.cpp` le tient. Ce qui manque est plus étroit : des copies INDÉPENDANTES, qu'on retouche une à une — rendre muette la troisième, couper la quatrième —, là où la fenêtre étirée est une seule chose.) `ClipEdit` sait dupliquer UNE fois, à un décalage ; « jusqu'à la fin de la boucle » n'existe pas. Cubase : Repeat… ; Live : Ctrl+D répété | `repeatClips(piste, sélection, nombre)` dans `core/`, les copies à la suite l'une de l'autre, identifiants neufs ; le menu du clip propose « Répéter N fois… » et « Répéter jusqu'à la fin de la boucle » (grisé sans boucle, avec sa raison) ; annulable ; test `core/` : trois répétitions rendent trois clips contigus dont les fenêtres sont celles de l'original, et la piste verrouillée n'en rend aucune |
| D20.2 | **Replier le piano roll sur les hauteurs jouées.** Une piste de batterie tient sur cinq hauteurs entre 36 et 46 ; le piano roll les montre parmi cent vingt-huit, et `cadrerSurLesNotes` ne fait que centrer. Live : Fold | un bouton « Replier » dans la barre du piano roll : seules les hauteurs présentes sur la piste font une rangée, le clavier les nomme, les gestes (dessiner, déplacer, écouter) tombent sur la bonne hauteur ; rien dans le modèle, rien dans le fichier ; une piste sans note ne se replie pas et le bouton le dit ; vérifié à l'écran, replié et déplié côte à côte |
| D20.3 | **Découper un clip audio aux transitoires.** `io::detectSound` trouve les bornes du son, rien ne trouve les ATTAQUES : découper une boucle de batterie en huit coups se fait à l'œil. Cubase : hitpoints ; Live : Slice | `io::detectOnsets` dans `audio/` — un flux d'énergie sur des trames courtes, une attaque là où l'énergie bondit au-dessus de ce qui précède, un écart minimal entre deux ; le menu du clip « Découper aux transitoires (N coupes) », qui passe par `splitClips` et dit d'avance combien de coupes il ferait ; test `audio/` : huit impulsions dans du bruit sont trouvées à ±2 ms, une sinusoïde tenue n'en donne aucune, deux impulsions à 10 ms n'en donnent qu'une |
| D20.4 | **Transcrire un clip audio en MIDI.** D9 reconstruit un FICHIER entier par la chaîne ; rien ne transcrit le clip qu'on a sous la souris. C'est le geste de Live (Convert to MIDI), et c'est la spécialité de ce projet : `analyzer/note_extraction.py` sait déjà le faire, personne ne l'appelle depuis l'application | `analyse/transcrire_clip.py` (nouveau, n'importe rien de la chaîne en cours) écrit les notes d'une plage d'un fichier en JSON, confiance comprise ; le menu du clip « Transcrire en MIDI » lance le script par l'interpréteur que D9 a trouvé, pose une piste MIDI neuve après la piste audio, sans instrument, avec un clip sur la plage et les notes douteuses marquées ; sans Python, l'entrée est grisée AVEC sa raison ; test Python : sur la basse du morceau minuscule, les notes rendues sont celles de la vérité à ±1 demi-ton |
| D20.5 | **Exporter en FLAC et en OGG.** L'export ne propose que `*.wav` ; Cubase et Live exportent FLAC et Ogg Vorbis, et un mixage de neuf minutes en 24 bits pèse 150 Mo. MP3 est écarté : l'encodeur n'est pas dans JUCE, et la règle n° 2 du § 0 interdit une dépendance à télécharger | le rendu passe par le MÊME code (`renderBundleToWav`), puis le WAV est transcodé par JUCE (FLAC à la profondeur choisie, Ogg Vorbis en qualité haute) ; le sélecteur dit les trois formats ; `VSM_EXPORT=fichier.flac` exporte le projet ouvert sans fenêtre, pour qu'on puisse le vérifier ; vérifié en relisant le fichier écrit (durée, canaux, fréquence) |

**Un outil de vérification de plus, et pour la raison habituelle.** Trois de
ces cinq gestes ne vivent que dans le menu contextuel d'un clip, qui ne
s'atteint qu'à la souris. `VSM_MENU=libellé[;libellé…]` exécute des entrées
de menu par leur LIBELLÉ avant la capture (« Tout sélectionner ; Répéter
jusqu'à la fin de la boucle »), et « Tout sélectionner » entre dans le menu
Édition pour que la sélection se fasse sans souris aussi. Le dépôt refuse de
déclarer une interface invérifiable, et cela vaut pour celle-ci.

> **D20.1 EST FAITE (05/09/2026, 10:36).** `repeatClips(piste, sélection,
> nombre, bloc)` pose les copies à la suite -- la répétition k décalée de
> k × bloc, le bloc étant celui que « dupliquer » emploie déjà (la longueur de
> la sélection arrondie à la mesure ou à la grille), calculé au même endroit
> (`selectionSpan`) pour que les deux gestes tombent au même tick. Les copies
> ont des identifiants neufs et deviennent la sélection : le geste suivant
> porte sur ce qu'on vient de poser. La piste verrouillée n'en rend aucune.
>
> **DES NOMBRES FIXES PLUTÔT QU'UNE BOÎTE DE DIALOGUE** (2, 3, 4, 8, 16 fois) :
> le geste est « encore, encore », pas « combien ? ». Et « jusqu'à la fin de
> la boucle » dit d'avance combien de fois y tiennent -- ou pourquoi zéro
> (« rien n'y tient, ou pas de boucle ») -- parce qu'une commande grisée sans
> raison est une commande qu'on croit cassée ; `repeatsThatFit` ne déborde
> jamais de la boucle, sinon la seizième mesure sonnerait après le rebouclage.
> Le sous-menu vit dans le menu contextuel du clip ET dans Édition, le second
> pour qu'il s'atteigne sans souris.
>
> **CE QUE VSM_MENU A APPRIS À SON PREMIER USAGE.** « Tout sélectionner »
> existe deux fois dans le menu Édition -- les notes du piano roll d'abord, les
> clips de l'arrangement ensuite --, et le premier préfixe venu a choisi les
> notes : la capture d'après était identique à celle d'avant, sans un mot. Le
> libellé EXACT est donc cherché avant le préfixe, et les entrées « N fois »
> sont grisées sans sélection pour que VSM_MENU le DISE au lieu de ne rien
> faire. Vérifié à l'écran sur le projet d'exemple : « Tout sélectionner dans
> l'arrangement ; 4 fois » pose quatre copies contiguës de chaque clip, à la
> mesure.
>
> Tests : 261 core (+3), tous verts.

> **D20.2 EST FAITE (05/09/2026, 10:41).** Tout passe par des RANGÉES :
> dépliée, la rangée d'une hauteur est « 127 moins la hauteur » et rien n'a
> changé au pixel ; repliée, c'est le nombre de hauteurs jouées plus aiguës
> qu'elle. `noteToY`, `yToNote`, le clavier, la grille et la barre de
> défilement lisent tous cette correspondance, si bien que dessiner, déplacer
> et écouter tombent sur la bonne hauteur sans qu'un seul geste ait été
> réécrit. Les rangées se relisent à chaque dessin : une note ajoutée fait
> apparaître sa rangée. Rien dans le modèle, rien dans le fichier.
>
> Refusé sur une piste sans note, en le disant dans la ligne d'état, et le
> bouton revient de lui-même ; l'entrée de menu annonce combien de hauteurs
> elle montrerait. Repliée, chaque rangée est nommée : elles ne se suivent
> pas, et « les do seulement » laisserait la caisse claire sans nom.
>
> Vérifié à l'écran sur la batterie du projet d'exemple, côte à côte : cent
> vingt-huit rangées avant, deux après (« charleston fermé », « grosse
> caisse »), la ligne de vélocité inchangée, la ligne d'état qui le dit.
> `VSM_MENU=Replier sur les hauteurs jouées` a piloté la seconde capture.
>
> **UN PIÈGE DE C++20 AU PASSAGE** : un littéral `u8"…"` est un `char8_t[]`,
> et `juce::String + u8"…"` est ambigu — la compilation avait échoué en
> silence derrière un `grep`, et la première capture « repliée » était celle
> de l'ancien binaire. Le code de sortie d'un tube est celui de son dernier
> maillon ; on lit désormais `PIPESTATUS`.

> **D20.3 EST FAITE (05/09/2026, 11:01), ET LE DÉTECTEUR A ÉTÉ RÉÉCRIT DEUX
> FOIS PAR LA MESURE.** La première version -- un flux d'énergie TOTALE,
> trames de 5 ms -- passait ses tests sur des impulsions dans du bruit et ne
> trouvait RIEN sur le stem de TR-909 du morceau minuscule : une grosse
> caisse y traîne à -18 dB pendant toute la mesure, et chaque frappe de
> charleston ne fait monter le tout que de 0,8 à 2,4 dB (mesuré aux huit
> instants vrais ; seule la seconde grosse caisse atteignait 7,4 dB, sous
> les 8 dB du seuil). L'en-tête disait « un détecteur spectral ferait mieux
> sur une note tenue ; ce n'est pas le cas d'usage » -- c'était exactement le
> cas d'usage. Le flux se calcule donc PAR BANDE (le tout, le grave sous
> 200 Hz, le médium, l'aigu au-dessus de 2 kHz, trois biquads), chaque bande
> avec sa propre moyenne de ce qui précède.
>
> **Puis vingt attaques sur quatre notes de basse.** À 5 ms, une dent de scie
> de TB-303 à 92 Hz est un clic toutes les 10,8 ms dans la bande haute : la
> trame qui contient le clic bondit de dix décibels au-dessus de celle qui ne
> le contient pas. La fenêtre est passée à 20 ms (deux périodes), le pas à
> 5 ms ; la précision de l'instant ne vient pas de la trame mais d'un
> affinage à l'échantillon -- la montée la plus raide, énergie de la
> milliseconde qui suit contre celle des cinq qui précèdent, DANS LA BANDE
> QUI A BONDI : sur la queue de la grosse caisse, l'énergie totale par
> milliseconde est plate à -18 dB et la caisse claire y est invisible, seule
> la bande haute la voit (mesuré : huit millisecondes de retard avant, trois
> millisecondes -- la marge -- après).
>
> Résultat sur les deux stems du morceau minuscule, posés en clips audio :
> la batterie donne ses sept coupes (0,243 / 0,489 / 0,735 / 0,981 / 1,227 /
> 1,472 / 1,718 s pour des frappes vraies à 0,246 / 0,492 / 0,738 / 0,984 /
> 1,230 / 1,475 / 1,721 -- la marge de 3 ms, exactement), la basse ses trois
> (0,489 / 0,981 / 1,472 pour 0,492 / 0,984 / 1,475). La première frappe, à
> zéro, ne coupe rien : une coupe à moins d'un écart minimal du début ferait
> un clip de dix millisecondes.
>
> Le nombre de coupes se dit APRÈS, avec les instants de chaque clip, sur
> stderr aussi (une boîte de message n'entre pas dans un autoportrait) --
> et non dans l'entrée de menu : le compter d'avance lirait le fichier
> entier à chaque clic droit. Ce qui n'est pas découpé est dit : piste MIDI,
> piste verrouillée, clip à l'envers, clip sans attaque. Le projet d'essai
> se fabrique depuis les stems vrais commis (`vocal_audio_track` sur
> `01-basse.wav` et `02-batterie.wav`, `write_project_bundle`) ; la capture
> l'a piloté par `VSM_VUE=tout-choisir` et `VSM_MENU=Découper la sélection
> aux transitoires`.
>
> Tests : 1 247 audio (+4, dont un motif de boîte à rythmes synthétique --
> grosse caisse à hauteur descendante et charleston --, qui est le cas réel
> et non les impulsions), tous verts.

> **D20.4 EST FAITE (05/09/2026, 11:06).** `analyse/transcrire_clip.py` --
> nouveau, il n'importe rien que la chaîne en cours n'ait déjà en mémoire, et
> il RÉEMPLOIE `extraire_notes` de `reconstruire.py` plutôt que de recopier
> ses vélocités tirées de l'énergie du son -- écrit les notes d'une plage d'un
> fichier en JSON, instants DANS LE FICHIER, confiance comprise. Trois tests
> Python : sur la basse du morceau minuscule, les notes rendues sont celles de
> la vérité à ±1 demi-ton et ±50 ms ; une plage coupée entre deux notes rend
> les siennes aux instants du fichier ; une plage vide est une erreur nommée.
>
> `ClipTranscriber` lance le script par l'interpréteur que D9 a trouvé, dans
> un processus enfant (Basic Pitch met dix secondes à charger, l'interface ne
> les attend pas), garde le journal entier pour le montrer si ça échoue, et
> tient qu'un code de sortie nul SANS fichier écrit est un échec. L'application
> pose alors une piste MIDI neuve après la piste audio -- par `moveTrack`, qui
> répare les index de routage, et non par une insertion à la main --, un clip
> sur la plage du clip audio, les notes replacées sur la ligne de temps par la
> fenêtre du clip (suivi de tempo compris, la même règle qu'une coupe), les
> douteuses comptées et dites, la piste SANS instrument : on en choisit un
> dans le rack, comme pour un projet importé d'un autre DAW. Un clip à la fois,
> et c'est dit quand plusieurs sont choisis. Sans Python, l'entrée est grisée
> avec la raison de D9.
>
> Vérifié à l'écran sur le projet d'essai : « Basse (audio) (transcrit) »
> après la piste audio, quatre notes, « (Aucun) » dans le rack. Deux
> commandes de plus pour y arriver sans souris : `premier-clip:N` dans
> VSM_VUE, et `VSM_DELAI=ms` pour que l'autoportrait attende la transcription.

> **D20.5 EST FAITE (05/09/2026, 11:12), ET LA PHASE D20 EST CLOSE.** Le rendu
> reste celui de `vsm-render` (`renderBundleToWav`) ; FLAC et Ogg Vorbis ne
> font que TRANSCODER ce rendu-là par les encodeurs de JUCE, jamais en rendre
> un autre -- deux chemins de rendu seraient deux vérités. FLAC à la
> profondeur choisie, 16 ou 24 bits ; un rendu 32 bits flottants s'y écrit en
> 24 et le compte rendu le dit, FLAC ne portant pas de flottants. Ogg Vorbis à
> la qualité la plus haute que l'encodeur propose, et le compte rendu dit
> « compression avec perte » plutôt qu'une profondeur qui n'existe pas. MP3
> n'y est pas : l'encodeur n'est pas dans JUCE, et la règle n° 2 du § 0
> interdit une dépendance à télécharger. Le sélecteur dit les trois formats.
>
> Vérifié en relisant les fichiers écrits depuis le projet d'exemple par
> `VSM_EXPORT=fichier.{wav,flac,ogg}` : 3,827 s, 44,1 kHz, deux canaux dans
> les trois cas -- WAV 1 013 Ko en PCM 24, FLAC 257 Ko en PCM 24, OGG 115 Ko
> en Vorbis.
>
> **Ce que l'audit laisse écrit.** Cinq manques trouvés en lisant les
> surfaces, cinq faits ; un réduit en l'écrivant (D20.1 : la boucle par
> fenêtre existait) ; un détecteur réécrit deux fois par la mesure (D20.3 :
> l'en-tête qui écartait le cas spectral décrivait le cas d'usage) ; et un
> outil de vérification de plus, `VSM_MENU`, qui a raté sa première capture
> sans le dire et a appris à préférer le libellé exact. Le moteur n'a pas été
> recompilé de toute la phase : une campagne tournait.
>
> Tests : 261 core, 1 247 audio, 274 interchange, et 168 Python — tous verts.

### Phase D21 — Le dixième audit : ce qui manque une fois D20 posée (05/09/2026, 11:35)

**Pourquoi.** Même méthode, même ordre du § 3, et la règle de D19 : chaque
manque cherché en LISANT la surface du module qui le porterait —
`NoteEdit.h` (la sélection), `MixerComponent` (le solo), `ClipEdit.h` et le
découpage de l'arrangement, `TimeSignatureMap.h` et les vues qui pourraient
l'éditer, la boîte d'export — puis dans ce document.

Le relevé a écarté, comme existant : la porte de bruit (`gate` est dans
`EffectFactory`, avec quinze autres effets), le pitch shift par insert
(D13.8), la sélection des notes douteuses, de même hauteur et d'une plage
(`selectDoubtfulNotes`, `selectNotesWithSamePitch`, `selectNotesInTimeRange`),
la carte des signatures dans le MODÈLE (`TimeSignatureMap::addChange`), les
fondus enchaînés (D13.1), l'export d'une plage et des stems (D6), la
normalisation d'un CLIP (D13.6).

**Et il en a reporté un en le disant.** *Transposer un clip audio* (Live : le
bouton Transpose d'un clip ; Cubase : la ligne d'information) demande un
rendu différent dans le moteur — un rééchantillonnage suivi d'un étirement
WSOLA pour garder la durée —, donc dans `vsm-render` aussi, que l'invariant
n° 3 veut identique au temps réel et qu'on ne recompile pas pendant qu'une
campagne tourne. Il attend la fin de la campagne R1.

Cinq manques ont survécu.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D21.1 | **Choisir les notes par vélocité ou par durée.** Une transcription laisse des notes fantômes — faibles, ou d'un soixante-quatrième — et on les chasse une à une ; `NoteEdit` sait choisir par hauteur, par plage et par doute, pas par vélocité ni par durée. Cubase : Logical Editor ; Live : rien | `selectNotesBelowVelocity` et `selectNotesShorterThan` dans `core/`, pures ; le sous-menu Sélection du piano roll propose « plus faibles que 32 / que 16 » et « plus courtes que la grille / que sa moitié » — des seuils fixes plutôt qu'une boîte de dialogue, pour que le geste enchaîne avec Supprimer ; tests `core/` : les bornes sont strictes et la sélection rendue ne contient que les notes visées |
| D21.2 | **Le solo exclusif.** Le bouton Solo d'une tranche s'ajoute aux autres ; écouter UNE piste sur douze demande d'éteindre onze solos. Cubase et Live : Ctrl+clic | Ctrl+clic sur Solo (et « Piste ▸ Solo exclusif ») met la piste seule en solo et éteint les autres ; un second Ctrl+clic rend tout ; le mélangeur et la liste suivent ; vérifié à l'écran |
| D21.3 | **Couper au passage par zéro.** Une coupe audio tombe où la tête ou le transitoire la pose, et une coupe sur un ventre de forme d'onde fait un clic ; D20.3 en pose sept d'un coup. Cubase : Snap to Zero Crossing ; Live : coupe et fondu automatiques | chaque coupe d'un clip audio — Ctrl+E, le découpage aux transitoires — se déplace au passage par zéro le plus proche dans ±2 ms, lu dans la source audio DÉJÀ chargée par le moteur ; `io::nearestZeroCrossing` dans `audio/`, testé ; l'aimant se dit dans le compte rendu ; une coupe sur une piste MIDI ne bouge pas |
| D21.4 | **Changer de mesure dans l'interface.** `Project::timeSignatureMap` porte une carte depuis longtemps (D19 l'a rappelé), la règle et l'arrangement la lisent, un MIDI importé la remplit, `project.json` l'écrit et la relit déjà (`transport.timeSignatures`, vérifié en lisant `ProjectDocument.cpp` avant d'écrire cette ligne) — et aucune vue ne l'ÉDITE, et la barre de transport dit la signature du tick ZÉRO, pas celle sous la tête : un morceau qui passe en 3/4 à la mesure 17 ne se compose pas ici. Cubase : piste de signature | « Édition ▸ Signature à la tête de lecture » propose 2/4, 3/4, 4/4, 5/4, 6/8, 7/8 et « Retirer le changement à la tête » ; `TimeSignatureMap::removeChangeAt` dans `core/`, testé ; la règle dessine les nouvelles mesures et la barre de transport dit la signature sous la tête ; vérifié à l'écran |
| D21.5 | **Normaliser à l'export.** Le rendu sort au niveau du mixage, et un mixage qui crête à −9 dB s'envoie tel quel ; Live propose de normaliser, et les plateformes demandent une sonie. Le moteur mesure déjà la sonie (`LufsMeter`, D4) | un choix de niveau dans la boîte d'export : tel quel, crête à −1 dBFS, −14 LUFS, −23 LUFS ; la mesure se fait sur le rendu écrit, le gain s'applique en réécrivant par JUCE — WAV, FLAC ou OGG — et le compte rendu dit le gain en dB et la sonie obtenue ; `VSM_EXPORT_NIVEAU` pour le vérifier sans fenêtre, en relisant la crête du fichier |

> **D21.1 EST FAITE (05/09/2026, 11:55).** `selectNotesBelowVelocity` et
> `selectNotesShorterThan`, pures, bornes strictes (« plus faibles que 32 » ne
> prend pas 32), testées. Le sous-menu Sélection du piano roll propose cinq
> entrées à seuils FIXES — plus faibles que 64, 32, 16 ; plus courtes que la
> grille, que sa moitié —, et chaque entrée ANNONCE combien de notes elle
> prendrait, pour que « rien à choisir » se lise avant de cliquer. La ligne
> d'état dit le compte après. Vérifié à l'écran par `VSM_MENU` sur la basse
> du morceau minuscule et sur celle du projet d'exemple : « 0 note(s) plus
> faible(s) que 64 », « 0 note(s) plus courte(s) que 120 ticks » — ces deux
> basses n'ont ni note faible ni note brève, et c'est la bonne réponse ; la
> sélection elle-même est tenue par le test `core/`.
>
> **D21.2 EST FAITE (05/09/2026, 11:55).** Ctrl+clic (ou Cmd) sur le Solo
> d'une tranche laisse le bouton à l'application, qui met la piste seule en
> solo, éteint les autres, et resynchronise toutes les tranches
> (`refreshMuteSolo`) ; si elle était déjà seule, tout s'éteint — un second
> Ctrl+clic rend le mélange entier. « Piste ▸ Solo exclusif de la piste
> choisie » fait de même au clavier, et son libellé dit lequel des deux gestes
> il fera. Vérifié à l'écran : Drums en solo, Acid Bass non.
>
> **D21.3 EST FAITE (05/09/2026, 11:55).** `io::nearestZeroCrossing` cherche,
> en s'éloignant de l'instant demandé des deux côtés à la fois, le premier
> changement de signe (ou zéro) dans ±2 ms, et rend celui des deux
> échantillons dont la valeur est la plus petite ; sans passage — du continu,
> un grave sous 250 Hz — l'instant reste. Testé sur une sinusoïde, du continu
> et le bord du matériau. L'application lit les échantillons dans la source
> que le MOTEUR joue (`ProcessGraph::trackAudio`, déjà rééchantillonnée à la
> session) : rien à relire. Ctrl+E s'aimante piste par piste ; le découpage
> aux transitoires aussi, et son compte rendu le dit : « 5 coupe(s)
> aimantée(s) au passage par zéro (au plus 1,11 ms) » sur les dix coupes des
> deux stems d'essai. Un clip étiré ou à l'envers n'est pas aimanté : la
> correspondance tick → échantillon n'y est plus une droite.
>
> **D21.4 EST FAITE (05/09/2026, 11:55).** « Édition ▸ Signature à la tête de
> lecture » : 2/4, 3/4, 4/4, 5/4, 6/8, 7/8, posés au DÉBUT de la mesure qui
> contient la tête — une signature qui changerait au milieu d'une mesure
> ferait une mesure de longueur impossible —, et « Retirer le changement de
> cette mesure », qui dit s'il y a quelque chose à retirer.
> `TimeSignatureMap::removeChangeAt` ne retire jamais celui du tick zéro : un
> morceau a toujours une signature, on la change. La barre de transport dit
> désormais la signature SOUS LA TÊTE, mise à jour seulement quand elle
> change. `project.json` l'écrivait et la relisait déjà (vérifié avant
> d'écrire l'étape, et dit dans son intitulé). Vérifié à l'écran : « 3/4 »
> dans la barre, et onze mesures dans la règle là où il y en avait neuf.
>
> **D21.5 EST FAITE (05/09/2026, 11:55), ET LA PHASE D21 EST CLOSE.** Un
> choix de niveau dans la boîte d'export — tel quel (le défaut : normaliser
> est un choix, jamais un accident), crête à −1 dBFS, −14 LUFS, −23 LUFS.
> La crête vient du rendu lui-même, la sonie du mesureur du moteur
> (`dsp::LufsMeter`) relisant le fichier ; le gain s'applique bloc par bloc
> en réécrivant par JUCE — WAV, FLAC ou OGG —, jamais en rendant autrement.
> Le compte rendu dit le gain et la sonie mesurée. Vérifié en relisant les
> fichiers du projet d'exemple par `VSM_EXPORT_NIVEAU` : crête à −1,00 dBFS
> exactement (+4,0 dB), et « −14 LUFS (mesurée −18,2, +4,2 dB) » avec une
> crête à −0,8 dBFS.
>
> **Ce que l'audit laisse écrit.** Cinq manques trouvés en lisant les
> surfaces, cinq faits, un reporté en le disant (transposer un clip audio,
> après la campagne) ; le détecteur d'attaques a rendu son biquad au moteur
> (`dsp::Biquad`) plutôt que d'en garder une copie ; et une leçon d'outil :
> un `grep` en zsh mange ses propres options quand elles ressemblent à des
> motifs (`--include=*.cpp`), et une compilation à `-j 6` pendant qu'une
> séparation demucs tourne a été tuée par le manque de mémoire — deux
> travaux seulement, désormais, tant qu'une campagne court.
>
> Tests : 263 core, 1 249 audio, 274 interchange, 168 Python — tous verts.

### Phase D22 — Le onzième audit : ce qui manque une fois D21 posée (06/09/2026, 13:05)

**Pourquoi.** Même méthode, même ordre du § 3, et la règle de D19 : chaque
manque cherché en LISANT la surface du module qui le porterait — le menu
contextuel du clip (`ArrangementComponent`), `ClipEdit.h`, `NoteEdit.h`,
`TimeSignatureMap.h`, `TransportBarComponent` et `AudioEngine`, le menu
Piste — puis dans ce document. Le relevé a été fait par sondage du code,
vingt-cinq candidats d'un coup, et chaque « absent » revérifié à la main.

Le relevé a écarté, comme existant : les notes fantômes des autres pistes
(bouton « Fantômes » du piano roll), le tap tempo (bouton « Tap »), le
copier-coller des clips (Ctrl+C / Ctrl+V dans l'arrangement), la crête
tenue et l'écrêtage au mélangeur, le suivi de la tête, le repère suivant
et précédent (Maj+N / Maj+B), la hauteur réglable des pistes, le swing de
la quantification, la chaîne latérale, le compteur de charge.

**Et l'élément reporté de D21 reste reporté, pour la même raison** : la
campagne R1, morte avec un redémarrage de la machine pendant le neuvième
morceau du lot prod (journal arrêté à 13:23 le 05/09, machine relancée le
06/09 à 12:52), a été RELANCÉE à 12:54 — reprise automatique, les huit
morceaux mesurés sont sautés — et `vsm-render` ne se recompile pas tant
qu'elle court. *Transposer un clip audio* attend donc son verdict.

Cinq manques ont survécu.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D22.1 | **Le gain et la phase d'un clip audio, à la main.** `Clip::gain` et `Clip::invertPhase` sont dans le modèle, le fichier et le moteur depuis D13 ; `setClipGain` et `toggleClipPhase` existent dans `core/`, testés — et aucune vue ne les appelle : l'application NORMALISE (gain = 1/crête) et rien d'autre, et le liséré de phase inversée se dessine pour un état que personne ne peut poser. Live : Clip Gain ; Cubase : le volume de la ligne d'information et le bouton Ø | le menu du clip audio propose « Gain du clip ▸ −6, −3, −1, +1, +3, +6 dB, 0 dB » (relatif au gain courant, borné à ±24 dB) et « Phase inversée » cochée ; sur toute la sélection ; le gain se lit sur le clip (« +3,0 dB ») et la phase par son liséré ; annulable ; vérifié à l'écran |
| D22.2 | **Aller à une mesure.** La position se lit (D11.3) et ne se saisit pas : rejoindre la mesure 57 se fait à la souris, en zoomant. Cubase : Maj+P ; Live : le champ de position se tape | `TimeSignatureMap::tickAtBarBeat` (inverse de `barBeatAt`, changements de signature compris) et `parseBarBeat` (« 17 », « 17.3 », « 17:3 ») dans `core/`, testés en aller-retour à travers un passage en 3/4 ; « Édition ▸ Aller à la mesure… » (Maj+P, dans la table des raccourcis) et le double-clic sur la position de la barre de transport ouvrent la saisie ; `VSM_POSITION=17.3` pour le vérifier sans souris ; la barre de transport dit la nouvelle position ; vérifié à l'écran |
| D22.3 | **Deux fois plus vite, deux fois plus lent.** « Durée ×2 » allonge chaque note sans bouger son départ : une phrase de croches devient une phrase de noires QUI SE CHEVAUCHENT, pas la même phrase jouée à moitié de vitesse. Live : les boutons ×2 et :2 du clip MIDI ; Cubase : « Double/Half tempo » du Logical Editor | `scaleNoteTimes` dans `core/`, pur : départs ET durées mis à l'échelle depuis le PREMIER départ de la sélection, durée d'au moins un tick, les notes non choisies immobiles ; « Temps et durée ▸ Deux fois plus lent (×2) » et « Deux fois plus vite (÷2) » dans le piano roll ; test : quatre noires ×2 donnent quatre blanches à 0, 960, 1920, 2880, et ÷2 deux fois de suite ne perd pas de note |
| D22.4 | **Le voyant d'activité MIDI.** Un clavier branché qui ne joue rien laisse deux causes : le câble ou la piste. Rien ne dit si une note ARRIVE ; rien ne dit si une note PART vers une machine. Live : les deux voyants en haut à droite ; Cubase : l'activité MIDI de la barre de transport | deux voyants « IN » et « OUT » dans la barre de transport, à côté du témoin d'entrée audio : IN s'allume à chaque message reçu du clavier (MIDI ou d'ordinateur), OUT à chaque note envoyée à une machine (planning ou écoute) ; compteurs atomiques dans `AudioEngine` et `ProcessGraph`, lus par la minuterie de l'application, tenue de 250 ms ; `VSM_LECTURE=1` lance la lecture avant la capture pour que OUT se photographie ; vérifié à l'écran |
| D22.5 | **Les presets de piste.** Un preset d'effet (D15.4) sauve UN insert, un preset de synthé sauve UNE machine ; la piste — sa machine et son état, ses inserts, ses départs, son volume, son panoramique, sa couleur, sa transposition, son décalage — se refait à la main à chaque projet. Cubase : Track Presets ; Live : enregistrer le rack de la piste par défaut | `TrackPreset` dans `interchange/` (format `vsm-track-preset` v1, extension `.track.json`), qui emboîte un `SynthPreset` pour la machine et les descriptions d'effets ; aller-retour JSON testé, un preset d'autre format refusé par son nom ; « Piste ▸ Enregistrer la piste comme preset… » (nom demandé) et « Piste ▸ Appliquer un preset de piste ▸ liste des fichiers du dossier `pistes` de la bibliothèque » ; appliquer garde les notes et les clips de la piste, remplace le reste, annulable ; vérifié à l'écran |

> **D22.1 EST FAITE (06/09/2026, 13:34).** Le menu du clip audio propose
> « Gain du clip (+0,0 dB) ▸ −6, −3, −1, +1, +3, +6 dB, 0 dB (remettre) » —
> le titre du sous-menu DIT le gain d'où l'on part — et « Phase inversée »,
> cochée quand elle l'est. Les pas sont RELATIFS et bornés à ±24 dB : « +3 dB »
> sur six clips inégaux les monte tous de 3 dB, il ne les aligne pas. Le gain
> se lit sur le clip, en haut à droite, sur un cartouche sombre — la couleur
> du clip est celle de l'utilisateur, et un texte ambre sur un clip ambre ne
> se lisait pas (première capture). Vérifié à l'écran sur un projet à piste
> audio, par deux jetons neufs de `VSM_VUE` qui passent par la MÊME fonction
> que le menu contextuel (`gain-clip:+3`, `phase-clip`) : « +3.0 dB » sur
> « Voix prise 1 », et le liséré rouge en tirets de la phase. `setClipGain`
> et `toggleClipPhase` étaient testés depuis D13 ; ils n'étaient appelés par
> personne.
>
> **D22.2 EST FAITE (06/09/2026, 13:34).** `TimeSignatureMap::tickAtBarBeat`
> est l'inverse de `barBeatAt`, changements de signature compris — testé en
> aller-retour sur soixante mesures, tous les temps, à travers un passage en
> 3/4 ; un temps au-delà de la mesure déborde sur la suivante plutôt que
> d'être refusé, c'est ce que la saisie attend. `parseBarBeat` lit « 17 »,
> « 17.3 », « 17:3 », « 17 3 » en numérotation humaine et REFUSE le reste
> (« 0 », « 17.0 », « 17. », « mesure 17 ») : une faute de frappe qui serait
> lue de travers enverrait la tête n'importe où sans le dire. « Édition ▸
> Aller à la mesure… » (Maj+P, dans la table des raccourcis, à côté des
> marqueurs) et le double-clic sur la position de la barre de transport
> ouvrent la saisie, préremplie de la position courante. Vérifié à l'écran
> par `VSM_POSITION=3.2` : « 00:04,154 | mes. 3 · 2 », qui est bien neuf
> temps à 130 BPM.
>
> **D22.3 EST FAITE (06/09/2026, 13:34).** `scaleNoteTimes` met à l'échelle
> les DÉPARTS et les durées depuis le premier départ de la sélection, qui ne
> bouge pas ; durée d'au moins un tick ; les notes non choisies immobiles.
> Testé : quatre noires ×2 donnent quatre blanches à 0, 960, 1920, 2880 ;
> ÷2 douze fois de suite ne perd pas de note ; l'ancre est celle de la
> sélection, pas du morceau. « Temps et durée ▸ Deux fois plus lent (départs
> et durées ×2) » et « Deux fois plus vite (÷2) » dans le piano roll, sous
> « Durée x2 » pour que la différence se lise là où l'on choisit. Vérifié à
> l'écran par `VSM_MENU=Tout sélectionner;Deux fois plus lent` sur la basse
> du projet d'exemple : la phrase qui finissait à la mesure 1,9 finit à la
> 2,8, les barres de vélocité deux fois plus espacées, la sélection gardée.
>
> **D22.4 EST FAITE (06/09/2026, 13:34).** Deux voyants « IN » et « OUT »
> dans la barre de transport, après le témoin d'entrée audio. IN compte les
> messages reçus par `AudioEngine::handleIncomingMidiMessage` — AVANT tout
> tri, un message qu'on ignore est quand même arrivé —, donc du clavier
> branché comme du clavier d'ordinateur, qui passe par le même chemin ; OUT
> compte les NoteOn au moment où ils entrent dans le tableau que la machine
> lit (`ProcessGraph`, écoute et planning). La minuterie de l'application
> compare deux lectures, la barre tient chaque voyant 250 ms et ne se
> redessine que quand l'état visible change. Vérifié à l'écran par
> `VSM_LECTURE=1` et `VSM_VUE=note:48` (une note par le chemin du clavier
> d'ordinateur, rejouée pendant trois secondes) : IN et OUT allumés, Play
> enfoncé, position 00:01,939.
>
> **Et une panne muette trouvée en posant les voyants.** Le témoin d'entrée
> audio (D3.4) ne se dessinait plus : la zone de transport faisait 460 px
> pour 466 px de boutons depuis le sélecteur de vitesse (D18.5), et le
> témoin, servi en dernier, recevait un rectangle vide — que `paint` saute
> sans rien dire. La zone est élargie pour lui et pour les voyants (540 px,
> 494 en fenêtre serrée), la place reprise sur le bouton d'écoute A/B ; à
> 1 280 px logiques, « Ouvrir MIDI… » et « Exporter MIDI… » tiennent encore.
>
> **D22.5 EST FAITE (06/09/2026, 13:34), ET LA PHASE D22 EST CLOSE.**
> `TrackPreset` dans `interchange/` (format `vsm-track-preset` v1,
> `.track.json`) emboîte un `SynthPreset` pour la machine et un
> `EffectPreset` par insert, avec son contournement ; il sauve la machine,
> les inserts, les départs, le volume, le panoramique, la couleur, le canal,
> la transposition, le décalage, le groupe de sortie — et PAS les notes, les
> clips, les prises, l'automation, ni le solo, le muet, l'armement, le gel,
> le verrou : un preset est un réglage qu'on pose sur un contenu, pas un
> contenu, et pas un état de session. Aller-retour JSON testé champ par
> champ ; un preset d'autre format ou d'autre version refusé par son nom ;
> un preset audio posé sur une piste MIDI lui laisse sa machine (ses notes
> n'en auraient plus). « Piste ▸ Enregistrer la piste comme preset… » (nom
> demandé, l'état de la machine pris sur la machine VIVANTE) et « Appliquer
> un preset de piste ▸ » qui LISTE le dossier `pistes` de la bibliothèque —
> sinon du projet, sinon des données de l'application, comme les grooves —
> et dit lequel quand il est vide. Appliquer garde les notes et les clips,
> remplace le reste, refabrique les inserts par le modèle, pose l'état de la
> machine après, annulable. Vérifié à l'écran : « Basse acide » écrit depuis
> la piste 1 (`VSM_PRESET_PISTE`), appliqué à « Drums » par
> `VSM_MENU=Basse acide` — la piste porte désormais la TB-303 et le volume
> −0,9 dB de la basse, ses notes de batterie sont restées.
>
> **Ce que l'audit laisse écrit.** Cinq manques trouvés en lisant les
> surfaces, cinq faits, l'élément reporté de D21 toujours reporté (la
> campagne court). La campagne R1 est morte une seconde fois avec un
> redémarrage de la machine, à 12:58 dans le dixième morceau du lot prod ;
> relancée à 13:18 par `setsid nohup`, les neuf morceaux mesurés sautés.
> Tout ce qui touche l'application a été compilé à `-j 2` pendant qu'elle
> court, et `vsm-render` n'a pas été recompilé.
>
> Tests : 268 core, 1 249 audio, 278 interchange — tous verts ; Python
> inchangé (168 à D21).


### Phase D23 — Le douzième audit : ce qui manque une fois D22 posée (06/09/2026, 13:40)

**Pourquoi.** Même méthode, même ordre du § 3, et la règle de D19 : chaque
manque cherché en LISANT la surface du module qui le porterait — la tranche
du mélangeur (`MixerComponent`), la tranche master et `MasterBus`, le menu
Enregistrement et `AudioEngine`, le menu Fichier et `Project::toParsedFile`,
le menu Affichage et `ArrangementComponent` — puis dans ce document. Le
relevé a été fait par sondage du code, cinquante candidats d'un coup
(un script de `grep` par candidat), et chaque « absent » revérifié à la main.

Le relevé a écarté, comme existant : l'arpégiateur et la contrainte à la
gamme du piano roll, les grilles en triolets et pointées, le zoom sur la
sélection, la compensation de latence des inserts (D4.5), le copier-coller
des notes, le dither à l'export (D14.4), les groupes, les dossiers, le gel,
les prises et leur assemblage, les repères et leur renommage, « contourner
tous les inserts », le clic pendant l'enregistrement seul et le niveau du
métronome (D16.6), l'automation qui suit les clips, la chasse des
contrôleurs, le solo exclusif, le déplacement fin, le tempo en rampes.

**Et l'élément reporté de D21 reste reporté, pour la même raison** : la
campagne R1 court (lot forcé `r1f-sec` depuis 13:31), et `vsm-render` ne
se recompile pas tant qu'elle court.

Cinq manques ont survécu.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D23.1 | **La polarité d'une piste.** Le clip audio a sa phase (D22.1) ; la PISTE n'en a pas — deux micros sur une caisse claire, un bus de groupe en opposition, rien ne s'inverse au mélangeur. Cubase : le bouton Ø de chaque tranche ; Live : le module Utility | `Track::invertPhase` dans le modèle et le fichier (`mix.invertPhase`, écrit seulement quand il l'est), appliqué par `ProcessGraph` au fader — signe sur le volume, donc sur la sortie ET les départs, pré comme post ; test audio : une piste inversée annule sa jumelle (crête < 10⁻⁴) ; bouton « Ø » sur la tranche, à côté du W, annulable ; dans le preset de piste (D22.5) ; vérifié à l'écran |
| D23.2 | **Le monitoring automatique.** « Écouter l'entrée en direct » est un interrupteur global : on l'oublie allumé et la lecture repasse l'entrée, on l'oublie éteint et la prise se fait sans s'entendre. Cubase : Auto Monitoring (Tape Machine Style) ; Live : Monitor Auto par piste | « Enregistrement ▸ Écoute de l'entrée ▸ Manuelle / Automatique (une piste audio armée, transport arrêté ou en enregistrement) / Armée (une piste audio armée, toujours) », réglage d'application retenu ; la minuterie applique ; le témoin d'entrée de la barre de transport prend un liséré quand l'écoute est active, quel que soit le mode ; `VSM_VUE=armer:N` pour le vérifier ; vérifié à l'écran |
| D23.3 | **Exporter la piste choisie en MIDI.** « Exporter MIDI… » écrit tout le morceau ; donner une partie à un autre musicien, ou la rejouer ailleurs, oblige à supprimer les autres pistes avant. Live : Export MIDI Clip ; Cubase : l'export des pistes choisies | `Project::extractTrack(i)` dans `core/`, pur : la carte de tempo et les signatures gardées, la piste seule, ses routages vers d'autres pistes défaits ; testé — un fichier d'une piste se relit avec ses notes et son tempo ; « Fichier ▸ Exporter la piste choisie en MIDI… » ; `VSM_EXPORT_MIDI_PISTE=fichier.mid` pour le vérifier sans fenêtre ; le fichier relu compte UNE piste |
| D23.4 | **Toutes les pistes à la fenêtre.** La hauteur de chaque piste se tire à la main (D5.3) ; à quinze pistes, les ramener toutes dans la fenêtre se fait quinze fois. Live : H (optimiser les hauteurs) ; Cubase : Zoom Full / Zoom Tracks | « Affichage ▸ Toutes les pistes à la fenêtre » : les pistes visibles et dépliées se partagent la hauteur disponible, entre 24 et 400 px, les pliées gardent la leur ; « Hauteur des pistes ▸ Petite, Normale, Grande » ; annulable (la hauteur est dans le projet) ; `VSM_VUE=pistes-a-la-fenetre`, `hauteur-pistes:N` ; vérifié à l'écran |
| D23.5 | **L'écoute en mono.** Le master mesure la corrélation de phase (D4.7) et la DIT ; on ne peut pas ENTENDRE ce qu'il reste en mono sans poser un effet. Cubase : Control Room, Mono ; Live : Utility sur le master | `MasterBus::setMonoListen` : repli L+R après le limiteur, avant les mesures — donc la corrélation lue passe à 1, ce qui est ce qu'on entend —, actif même quand la tranche master est contournée ; jamais dans un export (le rendu hors ligne monte son propre bus) ; test : un signal tout à gauche ressort égal des deux côtés ; bouton « MONO » sur la tranche master et « Mixage ▸ Écoute en mono » coché ; vérifié à l'écran |

> **D23.1 EST FAITE (06/09/2026, 13:50).** `Track::invertPhase`, écrit dans
> `mix.invertPhase` seulement quand il l'est (un projet sans piste inversée
> garde son fichier octet pour octet), relu, dans le preset de piste. Le
> moteur l'applique par le SIGNE du volume : la sortie, les départs
> post-fader ET pré-fader s'inversent — deux micros en opposition le sont
> dans la réverbération aussi —, pour une piste comme pour un groupe. Test
> audio : une piste inversée annule sa jumelle (crête < 10⁻⁴ sur les deux
> canaux, le témoin en phase à plus de 0,05). Bouton « Ø » sur la tranche, à
> côté du W — deux libellés d'un caractère tiennent là où trois se
> tronquaient (D16.8) —, annulable comme un geste de fader. Vérifié à l'écran
> par `VSM_VUE=polarite:2` : le Ø de « Voix » allumé. Le rendu hors ligne
> passe par le même graphe : `vsm-render` l'appliquera à sa prochaine
> recompilation, après la campagne.
>
> **D23.2 EST FAITE (06/09/2026, 13:50).** « Enregistrement ▸ Écoute de
> l'entrée ▸ Écoute manuelle / Écoute automatique (une piste audio armée,
> transport arrêté ou en enregistrement) / Écoute quand une piste est armée »,
> réglage d'application retenu ; en mode manuel, l'interrupteur d'avant reste
> le maître, et il se grise sinon. La minuterie applique à chaque tour ; le
> témoin d'entrée prend un liséré turquoise dès que l'écoute est active, quel
> que soit le mode — un mode qui s'allume tout seul doit se VOIR. Vérifié à
> l'écran par `VSM_VUE=armer:2` et `VSM_MENU=Écoute automatique` : le liséré,
> transport arrêté. Et une leçon d'outil, payée une capture : `VSM_MENU`
> prend le PREMIER libellé exact tous menus confondus, et « Automatique »
> existait déjà dans Fichier (les threads de rendu) — un libellé neuf doit
> être unique dans toute la barre, pas seulement dans son menu.
>
> **D23.3 EST FAITE (06/09/2026, 13:50).** `Project::extractTrack(i)`, pur :
> la piste seule, la carte de tempo et les signatures gardées, `outputGroup`
> et `outputSourceTrack` défaits (elles désignaient des pistes qui n'y sont
> plus), un index hors bornes rend un projet sans piste. Testé : le fichier
> d'une piste se relit avec ses deux notes et son tempo. « Fichier ▸ Exporter
> la piste choisie en MIDI (« nom »)… », grisée avec sa raison sur une piste
> qui n'est pas MIDI. Vérifié par `VSM_EXPORT_MIDI_PISTE` sur « Drums » du
> projet d'exemple, relu par un lecteur SMF indépendant : format 1, UNE
> piste, nommée « Drums », 8 NoteOn, tempo 461 538 µs (130 BPM).
>
> **D23.4 EST FAITE (06/09/2026, 13:50).** « Affichage ▸ Toutes les pistes à
> la fenêtre » : les pistes visibles et dépliées se partagent la hauteur
> disponible sous la règle, entre 24 et 400 px, les pliées et masquées
> gardent la leur ; « Hauteur des pistes ▸ Petite (24), Normale (56), Grande
> (112) ». Annulable — la hauteur est dans le projet (D5.3) —, et rien n'est
> écrit dans l'historique si aucune hauteur ne change. Vérifié à l'écran par
> `VSM_VUE=pistes-a-la-fenetre` : trois pistes qui remplissent l'arrangement.
>
> **D23.5 EST FAITE (06/09/2026, 13:50), ET LA PHASE D23 EST CLOSE.**
> `MasterBus::setMonoListen` : le repli L+R après le limiteur, avant les
> mesures — la corrélation lue devient ce qu'on entend, 1 —, actif même
> quand la tranche est contournée (vérifier un mixage en mono ne demande pas
> de master). Un `std::atomic<bool>` et non un paramètre : ni dans le fichier,
> ni dans un export — le rendu hors ligne monte son propre bus. Test : un
> signal tout à gauche ressort égal des deux côtés à la demi-somme, tranche
> contournée ; deux canaux en opposition mesurent une corrélation de 1,00
> tranche active ; éteinte, elle redevient transparente. Bouton « MONO » à
> côté de « MASTER » (ambre, comme un solo : un état d'écoute qu'on doit
> remarquer), et « Mixage ▸ Écoute en mono (jamais dans un export) » coché.
> Vérifié à l'écran par `VSM_MENU=Écoute en mono` : le bouton allumé.
>
> **Ce que l'audit laisse écrit.** Cinquante candidats sondés par script,
> cinq manques, cinq faits, l'élément reporté de D21 toujours reporté (la
> campagne court : lot forcé `r1f-sec`, deuxième morceau à 13:44). Tout
> compilé à `-j 2`, `vsm-render` intact.
>
> Tests : 269 core, 1 251 audio, 278 interchange — tous verts ; Python
> inchangé (168 à D21).

### Phase D24 — Le treizième audit : ce qui manque une fois D23 posée (06/09/2026, 13:55)

**Pourquoi.** Même méthode, même ordre du § 3 : vingt candidats sondés par
script, chaque « absent » revérifié en lisant la surface qui le porterait —
`AudioEngine::handleIncomingMidiMessage`, `MidiRecorder`, la table des
raccourcis, `ProcessGraph`, le menu Fichier et le lâcher de fichiers.

Le relevé a écarté, comme existant : la couleur des notes par vélocité,
l'insertion d'accords, le lâcher de fichiers sur la fenêtre, le retour au
départ à l'arrêt (préférence), le renommage d'une piste (double-clic),
Ctrl+D, Suppr, Ctrl+Q, le clic de la règle pour se placer.

**Et il a trouvé un trou plus grand qu'un manque de confort.** Un clavier
MIDI branché ne fait entendre que ses NOTES : la molette de hauteur, la
molette de modulation, la pression et la pédale de sustain sont jetées par
`handleIncomingMidiMessage` (« `if (!message.isController()) return;` »,
et les contrôleurs ne servent qu'au MIDI Learn) — alors que les machines
savent les recevoir (`handleControlEvent`, joué depuis le planning) et que
le fichier les écrit. Et la prise n'en garde rien : `RecordedNoteEvent`
n'a que des notes. Une nappe jouée avec la molette se réécoute plate.

**Et l'élément reporté de D21 reste reporté** : la campagne court.

Cinq manques ont survécu.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D24.1 | **Les contrôleurs en direct.** Molette de hauteur, modulation (CC 1), sustain (CC 64), pression : jetés à l'entrée. Cubase et Live : évidemment transmis à l'instrument de la piste | `ProcessGraph::sendLiveControl` (une file par source, comme les notes d'écoute), livré en tête de bloc par `handleControlEvent` aux pistes armées ou à la piste choisie ; un CC lié par MIDI Learn continue d'aller au paramètre lié, un CC libre va à la machine ; test : un pitch bend envoyé en direct est reçu par la machine (compteur `ignoredControlEvents` inchangé, `liveControlsDelivered` incrémenté) |
| D24.2 | **Les contrôleurs dans la prise.** Une prise ne garde que les notes ; la molette jouée pendant l'enregistrement disparaît | `RecordedControlEvent` et `MidiRecorder::pushControl` / `finishControls` dans `core/`, purs, testés : un CC et un pitch bend datés en secondes ressortent en ticks, ce qui précède le point d'entrée est écarté ; `applyRecordedControls` — remplacer efface la plage, superposer ajoute, trié ; la prise ordinaire les écrit sur chaque piste armée (les passes de boucle empilées : notes seulement, dit ici) |
| D24.3 | **Les raccourcis du transport.** Ni enregistrer, ni boucle, ni métronome, ni « aller à la fin » n'ont de touche. Cubase : *, /, C, Fin ; Live : F9 | `TransportRecord` (F9), `TransportLoop` (/), `TransportMetronome` (C), `NavGoToEnd` (Fin) dans la table, par les MÊMES boutons que la barre de transport (le bouton Rec grisé reste grisé, et le raccourci le dit sur stderr) ; testés par la table (identifiants uniques, touches uniques) |
| D24.4 | **Couper toutes les notes (panic).** Une note bloquée par un câble débranché ne se coupe qu'en arrêtant l'application. Cubase : Reset ; Live : rien de tel, et c'est un manque connu | `ProcessGraph::requestPanic()` : au bloc suivant, un NoteOff pour chaque note qui sonne sur chaque machine, sustain relâché (CC 64 = 0), le compte des notes qui sonnent remis à zéro ; « Enregistrement ▸ Couper toutes les notes (panic) » ; test : une note tenue puis coupée s'éteint (crête du dernier bloc < ¼ de la crête tenue, une seconde après) |
| D24.5 | **Importer un fichier audio sur une piste neuve, au menu.** Un fichier lâché sur la fenêtre propose la RECONSTRUCTION ; le poser sur une piste demande de créer la piste puis de lâcher le fichier sur elle. Cubase : Import Audio File ; Live : le navigateur | « Fichier ▸ Importer un fichier audio sur une piste neuve… » : la piste, puis `placeSampleOnTrack` à la mesure 1 — la même fonction que le lâcher sur une piste ; `VSM_IMPORT_AUDIO=fichier.wav` ; vérifié à l'écran |

> **D24.1 EST FAITE (06/09/2026, 14:08).** `ProcessGraph::sendLiveControl`,
> une file par source comme les notes d'écoute, vidée en tête de bloc et
> livrée par `handleControlEvent` au même endroit que les valeurs chassées
> (D16.2), avant les notes du bloc ; comptée (`liveControlsDelivered`). Dans
> `AudioEngine::handleIncomingMidiMessage`, la molette de hauteur (14 bits
> signés vers ± 2 demi-tons, la même conversion que le planning), la
> pression de canal, la pression polyphonique et tout CC LIBRE partent vers
> les pistes qui écoutent — les mêmes que les notes : armées, sinon choisie.
> Un CC lié par MIDI Learn reste au paramètre lié : c'est ce que l'utilisateur
> a demandé en le liant. Test : un pitch bend envoyé en direct est livré à la
> machine, transport à l'arrêt (le bloc est rendu pour cela), aucun
> contrôleur ignoré, une piste hors bornes refusée sans bloquer.
>
> **D24.2 EST FAITE (06/09/2026, 14:08).** `RecordedControlEvent` (datée en
> secondes, avec sa passe, valeur MIDI brute) dans sa PROPRE file de capture,
> quatre fois plus large que celle des notes — une molette produit des
> dizaines de messages par seconde et ne doit pas pousser une note hors de
> la file. `MidiRecorder::pushControl` (même point d'entrée que les notes),
> `finishControls` (ticks, triés, le point de sortie respecté),
> `applyRecordedControls` (remplacer efface la plage, superposer ajoute,
> trié). Testé : un CC et un pitch bend ressortent aux bons ticks, ce qui
> précède le point d'entrée est écarté, remplacer puis superposer donne
> trois puis cinq points en ordre. La prise ordinaire les écrit sur chaque
> piste armée ; les passes de boucle EMPILÉES gardent leurs notes seulement,
> et c'est dit ici : une passe est une prise conservée, et leur donner des
> contrôleurs demanderait de les ranger par passe dans les prises — un
> travail à part, non fait.
>
> **D24.3 EST FAITE (06/09/2026, 14:08).** `TransportRecord` (F9, comme
> Live), `TransportLoop` (/), `TransportMetronome` (C), `NavGoToEnd` (Fin)
> dans la table ; les touches passent par les BOUTONS de la barre
> (`toggleRecord`, `toggleLoop`, `toggleMetronome`), pour que l'état affiché
> et l'état réel ne divergent jamais ; Rec grisé reste grisé, et F9 le dit
> sur stderr. La table est tenue par ses tests (identifiants et touches
> uniques) : `/` et `C` n'étaient pris par rien.
>
> **D24.4 EST FAITE (06/09/2026, 14:08).** `ProcessGraph::requestPanic()` :
> lu une fois par bloc ; sur chaque piste, la pédale relâchée d'abord (CC 64
> à zéro — un NoteOff sous pédale ne coupe rien), puis un NoteOff par note
> qui sonne, le compte remis à zéro ; le bloc est rendu même transport
> arrêté. « Enregistrement ▸ Couper toutes les notes (panic) », qui relâche
> aussi les touches d'ordinateur enfoncées. Test : une note tenue quatre
> secondes, coupée à 0,5 s, a une crête inférieure au quart de la crête tenue
> une seconde après ; le témoin sans panic sonne encore au même instant.
>
> **D24.5 EST FAITE (06/09/2026, 14:08), ET LA PHASE D24 EST CLOSE.**
> « Fichier ▸ Importer un fichier audio sur une piste neuve… » : la piste,
> nommée d'après le fichier, puis `placeSampleOnTrack` à la mesure 1 — la
> même fonction que le lâcher sur une piste, donc la même copie dans
> `audio/` (D6.4) et les mêmes refus ; sans dossier de projet, refusé et dit
> AVANT de créer la piste ; si la pose échoue, la piste est retirée.
> Vérifié à l'écran par `VSM_IMPORT_AUDIO` : la piste « voix », son clip
> d'une mesure et demie, `audio/voix.wav` copié. Et une ligne de la liste des
> pistes qui gardait « Audio 4 » : créée avant le nom, rafraîchie depuis.
>
> **Ce que l'audit laisse écrit.** Le trou des contrôleurs était plus grand
> qu'un manque de confort : un clavier MIDI ne faisait entendre que ses
> notes, et personne ne l'avait écrit, parce que les tests du moteur jouent
> depuis le planning — où les contrôleurs passaient — et jamais depuis
> l'entrée MIDI. Les deux tests neufs entrent par `sendLiveControl` et
> `pushControl`, là où le trou était. La campagne court (lot forcé,
> deuxième morceau).
>
> Tests : 270 core, 1 253 audio, 278 interchange — tous verts ; Python
> inchangé (168 à D21).

### Phase D25 — Le quatorzième audit : ce qui manque une fois D24 posée (06/09/2026, 14:15)

**Pourquoi.** Même méthode. Le relevé a écarté, comme existant : le choix
des entrées MIDI (réglages audio), le clavier d'ordinateur et ses octaves,
l'insertion de silence entre les locateurs, l'automation sur les clips.

**Et il a mesuré l'étendue du trou de D24.** `handleControlEvent` est
implémenté par AUCUNE des 57 machines (le défaut de `ISynthPlugin` rend
faux) : pédale, molette de hauteur, modulation et pression — du planning
comme de l'entrée MIDI — sont comptés « ignorés » et n'ont jamais eu
d'effet. Chaque machine convertit sa note en fréquence elle-même (57
dossiers, aucun assistant commun, aucun paramètre d'accord global) : la
molette de hauteur demande de passer dans chacune, et c'est une phase à
part, pas une ligne d'audit. Elle est REPORTÉE en le disant (D26). La
pédale, elle, se règle SANS les machines : retenir les NoteOff tant qu'elle
est enfoncée est un geste du graphe.

Quatre manques ont survécu, et un cinquième est reporté.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D25.1 | **La pédale de sustain, pour toutes les machines.** CC 64 enfoncé, les notes relâchées doivent tenir ; aucune machine ne le fait | dans `ProcessGraph`, par piste : pédale enfoncée (CC 64 ≥ 64, du planning, de la chasse ou du direct), les NoteOff sont RETENUS ; relâchée, ils partent d'un coup ; une note rejouée sous pédale oublie son NoteOff retenu ; le rebouclage, l'arrêt et le panic vident la retenue ; la pédale n'est pas transmise à la machine (elle ne saurait qu'en faire) ni comptée ignorée ; test : une croche sous pédale sonne encore une seconde après son NoteOff, et se tait après le relâchement — contre un témoin sans pédale |
| D25.2 | **Muet, solo et piste voisine au clavier.** Le muet et le solo de la piste choisie se cliquent ; la piste suivante se clique. Cubase : M, S, ↑/↓ ; Live : rien de tel pour muet/solo, ↑/↓ pour les pistes | `TrackMuteSelected` (Maj+M), `TrackSoloSelected` (Maj+S), `NavNextTrack` (Alt+↓), `NavPreviousTrack` (Alt+↑) dans la table — M et S nus sont pris par l'arrangement (aimant) et la saisie ; par les MÊMES fonctions que les boutons ; annulables ; `VSM_VUE=muet-piste:N`, `solo-piste:N` ; vérifié à l'écran |
| D25.3 | **Le double-clic remet un réglage à sa valeur d'usine.** Fader, panoramique, décalage, transposition : un fader poussé à −7,3 dB se ramène à 0 dB à la main, en visant. Cubase : Ctrl+clic ; Live : Suppr sur le réglage choisi | `setDoubleClickReturnValue` sur les quatre curseurs de la tranche (0 dB, centre, 0 ms, 0 demi-ton), annulable comme un glissé |
| D25.4 | **Les contrôleurs dans le tampon rétrospectif.** « Récupérer ce qui vient d'être joué » (D17.3) rend des notes ; la molette jouée avec est perdue | `RetrospectiveBuffer::pushControl` et `takeControls` dans `core/`, même capacité, même fenêtre de temps ; la récupération les pose avec les notes ; testé |
| D25.5 | **La molette de hauteur dans les machines** — REPORTÉE : phase D26, un assistant commun `noteToHz(note, bend)` et une passe sur les 57 machines, avec sa mesure (une note pliée d'un demi-ton doit sortir à sa fréquence) | — |

> **D25.1 EST FAITE (06/09/2026, 14:18).** La pédale est l'affaire du graphe :
> par piste, `sustainDown_` et `heldNoteOffs_` ; CC 64 intercepté qu'il
> vienne du planning, de la chasse (D16.2) ou du direct (D24.1), jamais
> transmis à la machine ni compté « ignoré » ; enfoncée, les NoteOff sont
> retenus (la note reste comptée comme sonnante, ce qu'elle est) ; relâchée,
> ils partent d'un coup en tête de bloc ; une note rejouée sous pédale
> oublie sa retenue ; le rebouclage et le panic vident tout. Test : une
> croche sous pédale sonne encore à 1 s (crête > 0,05), le témoin sans
> pédale s'est tu (< ¼), et après le relâchement à 2 s elle s'est tue
> aussi ; zéro contrôleur ignoré.
>
> **D25.2 EST FAITE (06/09/2026, 14:18).** Maj+M, Maj+S, Alt+↓, Alt+↑ dans
> la table — M et S nus étaient pris (l'aimant de l'arrangement, la
> saisie). Les bascules passent par la MÊME republication que les boutons
> (`refreshMuteSolo`, la liste, `onMixChanged`), annulables (« Mixage ») ;
> la piste voisine saute les pistes masquées — on ne choisit pas ce qu'on
> ne voit pas. Vérifié à l'écran par `VSM_VUE=muet-piste:1,solo-piste:2` :
> M rouge sur « Drums », S ambre sur « Voix ».
>
> **D25.3 EST FAITE (06/09/2026, 14:18).** `setDoubleClickReturnValue` sur
> les quatre curseurs de la tranche : 0 dB, centre, 0 ms, 0 demi-ton. Le
> geste passe par le même `onDragStart`/`onDragEnd` que le glissé, donc par
> la même entrée d'historique et la même passe d'automation.
>
> **D25.4 EST FAITE (06/09/2026, 14:18), ET LA PHASE D25 EST CLOSE.**
> `RetrospectiveBuffer::pushControl` / `takeControls` : un anneau à part de
> même capacité — une molette ne chasse pas les notes du tampon —, et la
> même fenêtre de temps ; la conversion et le tri réemploient
> `finishControls`, comme les notes réemploient `finish`. Hors
> enregistrement, la file des contrôleurs va au tampon avec les notes ;
> « Récupérer ce qui vient d'être joué » pose les deux, superposés ; un
> tampon qui n'a que des contrôleurs n'est plus « vide ». Testé : trois
> contrôleurs gardés sur cinq dans un anneau de trois, la note intacte, la
> fenêtre bornée, le tampon vidé.
>
> **D25.5 RESTE REPORTÉE**, et devient la phase D26 : la molette de hauteur
> dans les 57 machines, par un assistant commun et une mesure par machine.
>
> **Ce que l'audit laisse écrit.** Le comptage des machines qui répondent
> aux contrôleurs — zéro sur cinquante-sept — est le chiffre de cette
> phase, et il n'avait jamais été écrit parce que rien ne le mesurait :
> `ignoredControlEvents` comptait, personne ne lisait. Il est désormais
> tenu par un test (la pédale ne le fait plus grimper).
>
> Tests : 271 core, 1 254 audio, 278 interchange — tous verts ; Python
> inchangé (168 à D21).

> **RECTIFICATIF (06/09/2026, 14:45) — le chiffre de D25 était faux.** Le
> relevé « `handleControlEvent` implémenté par aucune des 57 machines »
> venait d'un `grep` sur `audio/src` et `audio/include` — où les machines
> ne sont PAS : elles vivent dans `audio/plugins/`. Recompté au bon endroit :
> **53 dossiers sur 65 implémentent `handleControlEvent`, 42 répondent à la
> molette de hauteur, 21 à la pression de canal, 18 à des CC, 8 ont leur
> propre étouffoir (CC 64)**. Le trou de D24 n'était donc pas dans les
> machines mais à l'ENTRÉE (les messages jetés) et dans la PRISE (jamais
> gardés) — ce que D24 a réparé. Ce que ce rectificatif change :
> la pédale du graphe (D25.1) reste juste pour les 45 machines sans
> étouffoir, et elle est désormais TRANSMISE aussi aux huit qui en ont un,
> pour ne pas leur retirer le leur ; la molette (« D25.5 reportée ») n'est
> pas une passe sur 57 machines mais sur celles qui ne la font pas encore,
> et D26 est recadrée ci-dessous. La leçon est dans CLAUDE.md : un « zéro »
> sorti d'un `grep` se revérifie en LISTANT ce qu'on a cherché et où, avant
> de l'écrire. Le message du commit b503321 porte le chiffre faux ; il n'est
> pas réécrit (l'historique ne se retouche pas), ce rectificatif le corrige.

### Phase D26 — La molette de hauteur là où elle manque (recadrée le 06/09/2026, 14:50)

**Ce que le recomptage donne.** Vingt-trois dossiers de `audio/plugins/`
ne répondent pas à la molette : douze n'ont pas de `handleControlEvent`
(divider, drums, epiano, flute, fmdrums, perc, piano, sampler, testtone,
tonewheel, tr808, tr909) et onze en ont un sans la molette (bagpipe,
carillon, clavinet, harpsichord, hurdygurdy, jewsharp, kalimba, mandolin,
musicbox, pipeorgan, vibraphone). Parmi eux, **sept seulement sont des
instruments où un musicien plie la hauteur** : la flûte, le piano
électrique, le clavinet, la guimbarde, la vielle, la mandoline, le kalimba.
Les seize autres sont des percussions, des orgues, des cloches, des boîtes
à musique, un clavecin — des instruments qui ne se plient pas, et où une
molette serait une invention.

**Ce que chacun des sept demande.** Ces machines sont des modèles physiques
qui fixent leur fréquence AU NOTE-ON (la longueur du guide d'ondes, la
tine, la corde) : `EPianoSynth.cpp:14`, `FluteSynth.h:138`,
`ClavinetSynth.h:94` — une fréquence calculée une fois. Plier la hauteur en
cours de note demande, pour chacune, de rendre cette longueur variable par
bloc dans le modèle même, sans casser sa justesse (mesurée par leurs
tests) : un travail par machine, sur son modèle, une heure ou deux chacune.

**Décision, écrite ici plutôt que faite.** Aucune mesure ne réclame cette
phase : la chaîne d'analyse n'émet pas de pitch bend (`grep pitch_bend
analyse/analyzer/` : rien), donc aucune reconstruction n'en dépend, et les
42 machines qui plient couvrent les synthétiseurs — ceux qu'un musicien
plie. Elle se fera **machine par machine, quand un morceau le réclamera**, avec
pour critère : une note pliée d'un demi-ton sort à la fréquence de la note
du dessus, à 1 % près, mesurée sur le rendu.

> **RECTIFICATIF (10/09/2026) : SIX MACHINES, PAS SEPT — LA FLÛTE N'EST PAS
> DANS LE PARC.** Ce paragraphe disait « en commençant par la flûte, le seul
> des sept où le pli est courant ». Vérification faite au moteur :
> `vsm.flute` **n'est pas enregistrée**. Son source vit bien dans
> `audio/plugins/flute/`, avec sa macro `VSM_REGISTER_SYNTH_PLUGIN` et son
> test — mais aucun `CMakeLists.txt` ne les compile, et c'est **volontaire** :
> commit `0edd7cc`, « la flûte oscille enfin, et elle joue faux », un résultat
> négatif documenté (elle s'accroche à un mode élevé de sa boucle : +41
> demi-tons sur la note 45). Mesures dans `ARCHITECTURE.md` § 44.
>
> La phase porte donc sur **six** machines — e-piano, clavinet, guimbarde,
> vielle, mandoline, kalimba — toutes vérifiées présentes au registre. Et si la
> flûte revenait un jour, la molette serait le dernier de ses problèmes : une
> machine qui ne joue pas la note qu'on lui donne ne se plie pas non plus. La phase reste ouverte, sans
étape numérotée, et le prochain audit reprend l'ordre du § 3.

### Phase D27 — La sortie MIDI matérielle par piste (06/09/2026, 14:25)

**Pourquoi.** Le studio s'appelle « Vintage Synth » et ne sait parler à
aucun synthé réel : `juce::MidiOutput` n'apparaît nulle part dans
`app/Source/`. Cubase et Live envoient chaque piste MIDI vers un port
matériel ou virtuel, et c'est la première chose qu'on attend d'un
séquenceur devant un rack. Ici, une piste ne joue que sa machine interne.

**Ce que ça exige du moteur.** Le graphe ne peut ni allouer ni bloquer sur
le chemin audio ; il ne connaît pas JUCE. Il DÉPOSE donc ce qu'il livre à
une piste (notes, contrôleurs, retenues de pédale, NoteOff de rebouclage et
de panic) dans une file sans verrou, chaque événement daté sur l'horloge
monotone du système au début du bloc plus son décalage d'échantillon ; un
fil de l'application les envoie à leur heure. La gigue est celle du
sondage de ce fil (1 ms), l'avance est nulle : le port reçoit l'événement
au moment où la machine interne l'aurait rendu. Un port virtuel « VSM
Studio » est créé au démarrage, pour que la sortie se vérifie sans
matériel (`aseqdump`) et se branche sur n'importe quel logiciel.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D27.1 | **Le modèle et le fichier.** | `Track::midiOutputDevice` (nom du port, vide = machine interne seulement) ; `midiOutput` dans `project.json`, écrit seulement quand il est posé ; aller-retour testé ; pas dans le preset de piste (un port est une affaire de studio, pas de son) |
| D27.2 | **La file de sortie du graphe.** | `ProcessGraph::popMidiOut` ; une piste dont le port est posé passe par le rendu même sans machine ; tout ce qui est livré à la piste est déposé, daté ; ce qui déborde est compté, jamais tu ; test : une note planifiée donne un NoteOn puis un NoteOff, dans l'ordre, datés croissants, et rien sans port |
| D27.3 | **L'émetteur.** | un fil de `AudioEngine` sonde la file toutes les millisecondes et envoie à l'heure sur le port de la piste ; les ports s'ouvrent par nom depuis le fil d'interface ; le port virtuel « VSM Studio » existe toujours ; le voyant OUT (D22.4) compte aussi ces notes |
| D27.4 | **Le choix du port.** | « Piste ▸ Sortie MIDI matérielle ▸ (aucune) / ports disponibles » coché ; annulable ; l'en-tête de la piste dans l'arrangement dit « midi → port » ; `VSM_VUE=sortie-midi:N:port` |
| D27.5 | **La preuve.** | `aseqdump` branché sur « VSM Studio » pendant `VSM_LECTURE=1` sur le projet d'exemple : des Note on / Note off arrivent, comptés et écrits ici, avec la capture de l'en-tête « midi → VSM Studio » |

> **D27.1 EST FAITE (06/09/2026, 14:49).** `Track::midiOutputDevice`,
> `midiOutput` dans `project.json` (écrit seulement quand il est posé),
> aller-retour et application testés ; pas dans le preset de piste.
>
> **D27.2 EST FAITE (06/09/2026, 14:49).** `ProcessGraph::popMidiOut`, UNE
> file par piste — les fils de rendu (D8.1) rendent des pistes différentes
> en parallèle, et un anneau à un producteur ne supporte pas deux
> écrivains. Une piste à port passe par le rendu même sans machine ; tout ce
> qu'elle reçoit est déposé : notes du planning et de l'écoute, contrôleurs
> (le canal est celui de l'ÉVÉNEMENT, comme pour les notes), pédale, NoteOff
> de rebouclage et de panic. L'heure est ANCRÉE sur l'horloge
> d'échantillons — l'ancre plus les échantillons rendus, reposée seulement si
> elle dérive de plus de 20 ms — et non l'heure du rappel : deux rappels
> espacés de 9 puis 12 ms feraient sinon se chevaucher leurs événements
> (attrapé par le test, hors ligne). Tests : une note et un CC sans machine
> donnent « 92:60 B2:1 82:60 » ; huit notes à 48 kHz donnent huit NoteOn et
> huit NoteOff, pas un de plus ; rien sans port ; rien de perdu.
>
> **D27.3 EST FAITE (06/09/2026, 14:49).** Un fil « VSM MIDI out » sonde la
> file toutes les millisecondes, trie, envoie à l'heure (`sendMessageNow`)
> sur le port de la piste ; les ports s'ouvrent par NOM depuis le fil
> d'interface (un identifiant JUCE change d'une session à l'autre, un nom se
> lit dans le fichier), se ferment quand plus personne ne les emploie ; ce
> qui n'a pas de port est compté (`midiOutWithoutPort`). Le port virtuel
> « VSM Studio » est créé au démarrage. Le voyant OUT compte ces notes.
> `VSM_TRACE_MIDIOUT=1` écrit les quarante premiers envois avec leur retard —
> mesuré entre −0,4 et +0,3 ms.
>
> **D27.4 EST FAITE (06/09/2026, 14:49).** « Piste ▸ Sortie MIDI matérielle
> (→ port) ▸ (aucune) / ports », coché, annulable ; l'en-tête de la piste
> dit « midi → VSM Studio » (vérifié à l'écran) ; `VSM_VUE=sortie-midi:N:port`.
>
> **D27.5 EST FAITE (06/09/2026, 14:49), ET LA PHASE D27 EST CLOSE.**
> `aseqdump -p <client>:2` branché sur le port virtuel pendant une lecture
> du projet d'exemple (`VSM_LECTURE=4000`, pour que l'outil soit branché
> avant la première note) : **8 Note on, 8 Note off** — la basse du projet,
> exactement (le fichier compte 8 NoteOn) —, velocités 127 et 90 conformes,
> canal 1.
>
> **Ce que la preuve a attrapé, et qu'aucun test ne pouvait attraper.** La
> première mesure donnait **3 440 Note on pour 8 jouées**. Ni la file (une
> par piste depuis), ni la sélection du planning : le port virtuel de
> sortie se présente AUSSI comme une entrée MIDI (le côté lisible d'un port
> ALSA), et le moteur écoute toutes les entrées au démarrage — ce qui
> sortait revenait, rejouait la piste choisie, ressortait, une note par
> bloc. Le moteur n'écoute plus son propre port. Deux leçons dans
> CLAUDE.md : un port de sortie virtuel est aussi une entrée ; et une preuve
> par outil extérieur doit couvrir la FENÊTRE où le morceau joue — la
> deuxième preuve comptait zéro parce que le morceau de 1,85 s était fini
> avant qu'`aseqdump` ne soit branché.
>
> Tests : 271 core, 1 256 audio, 278 interchange — tous verts ; Python
> inchangé (168 à D21).

### Phase D28 — Le matériel, suite : l'horloge, le programme, le canal d'entrée (06/09/2026, 14:52)

**Pourquoi.** Une piste parle désormais à un port (D27) ; il lui manque ce
qu'un séquenceur dit à un synthé ou une boîte à rythmes réels au-delà des
notes. Cubase et Live envoient l'horloge MIDI (une boîte à rythmes se
synchronise dessus), le programme et la banque de la piste, et chaque
piste choisit le canal d'entrée qu'elle écoute (deux claviers, deux
pistes). Ici : pas d'horloge, pas de programme de piste, et tout ce qui
entre va à la piste armée quel que soit le canal. Le relevé a écarté comme
existant : le zoom de l'arrangement sur la sélection, le pré-roll (le
décompte lit déjà le morceau depuis N mesures avant le point d'entrée).

| Étape | Contenu | Terminé quand |
|---|---|---|
| D28.1 | **L'horloge MIDI et le transport sur les ports employés.** | 24 impulsions par noire (0xF8) suivant la carte de tempo, datées à l'échantillon ; Start (0xFA) quand la lecture part de zéro, Song Position (0xF2) puis Continue (0xFB) sinon, Stop (0xFC) à l'arrêt — détectés dans le bloc, jamais depuis le fil d'interface ; envoyés sur tous les ports que des pistes emploient ; test : une seconde à 120 BPM donne un Start puis 48 impulsions ± 1, puis un Stop ; rien sans port |
| D28.2 | **Le programme et la banque de la piste.** | `Track::midiProgram` (0–127, −1 = aucun) et `midiBank` (0–16383, −1 = aucune) dans le fichier ; envoyés sur le port de la piste — banque (CC 0, CC 32) puis programme — dès qu'ils changent et au départ de chaque lecture ; « Piste ▸ Programme MIDI… » ; l'en-tête dit « prog N » ; test : un programme posé donne CC 0, CC 32, 0xC0 une fois, pas une de plus |
| D28.3 | **Le canal d'entrée MIDI par piste.** | `Track::midiInputChannel` (0 = tous, 1–16) dans le fichier ; une piste armée ou choisie n'écoute que son canal — notes ET contrôleurs ; « Piste ▸ Canal d'entrée MIDI ▸ Tous / 1…16 » ; `VSM_VUE=canal-entree:N:C` ; vérifié à l'écran (l'en-tête dit « entrée ch. C ») |
| D28.4 | **Se placer sur la sélection.** | `NavToSelection` (L, comme Cubase) : la tête au début de la sélection de l'arrangement, sinon du piano roll ; dans la table ; rien s'il n'y a pas de sélection |
| D28.5 | **La preuve.** | `aseqdump` sur le port virtuel pendant une lecture : Start, des Clock au bon nombre pour la durée jouée, Program change et Control change de banque avant la première note, Stop à la fin — comptés et écrits ici |

> **D28.1 EST FAITE (06/09/2026, 15:01).** `emitMidiClockAndTransport`, EN
> TÊTE DE BLOC — avant le chemin « au repos » qui sort tôt, sans quoi le
> Stop d'un arrêt ne se voyait jamais (attrapé par le test) : Start (0xFA)
> quand la lecture part de zéro, Song Position puis Continue sinon, Stop à
> l'arrêt, 24 impulsions par noire aux ticks multiples de ppq/24 datées par
> la carte de tempo à l'échantillon ; dans une file SYSTÈME à part, vidée
> AVANT celles des pistes par l'émetteur — à heure égale, le tri stable
> garde l'ordre d'arrivée, et un Start doit précéder la première note
> (attrapé par la preuve : la première mesure avait « Note on, Start »).
> Envoyés une fois sur chaque port employé. Test : une seconde à 120 BPM
> donne un Start, 48 impulsions ± 1, un Stop ; rien sans port.
>
> **D28.2 EST FAITE (06/09/2026, 15:01).** `Track::midiProgram`, `midiBank`,
> dans le fichier quand ils sont posés ; envoyés — CC 0, CC 32, puis 0xC0 —
> dès qu'ils changent et au départ de chaque lecture (le dernier envoyé est
> oublié au Start) ; « Piste ▸ Programme MIDI (6, banque 130)… », boîte à
> deux champs, vide = rien ; l'en-tête dit « → VSM Studio · prog 6 » — le
> « midi » devant le port est tombé, il coupait le programme. Test : la
> suite exacte « B0:0:1 B0:32:2 C0:5:0 90:60:100 80:60:64 ».
>
> **D28.3 EST FAITE (06/09/2026, 15:01).** `Track::midiInputChannel`
> (0 = tous), dans le fichier quand il est posé, publié au moteur avec les
> ports ; sur le thread MIDI, une piste armée ou choisie n'écoute que son
> canal, notes ET contrôleurs ; « Piste ▸ Canal d'entrée MIDI (3) ▸ Tous /
> 1…16 » ; l'en-tête dit « midi · entrée ch. 3 » (vérifié à l'écran par
> `VSM_VUE=canal-entree:1:3`).
>
> **D28.4 EST FAITE (06/09/2026, 15:01).** `NavToSelection` (L) : la tête au
> début de la sélection de l'arrangement (`selectionStartTick`, par la même
> `selectionSpan` que la répétition), sinon à la première note choisie du
> piano roll ; rien sans sélection.
>
> **D28.5 EST FAITE (06/09/2026, 15:01), ET LA PHASE D28 EST CLOSE.**
> `aseqdump` sur le port virtuel, projet d'exemple, programme 6 banque 130
> posés sur la basse, lecture retardée de 4 s : **1 Start, puis CC 0 = 1,
> CC 32 = 2, Program change 5, puis les 8 Note on, 121 Clock, 1 Stop** —
> 121 impulsions font 5 noires à 130 BPM : le transport court jusqu'à la
> fin du morceau, dernière note relâchée comprise, et c'est là que le Stop
> part.
>
> Tests : 271 core, 1 257 audio, 278 interchange — tous verts ; Python
> inchangé (168 à D21).

### Phase D29 — Le quinzième audit : ce qui manque une fois D28 posée (06/09/2026, 15:15)

**Pourquoi.** Même méthode, cinq candidats sondés puis revérifiés en
lisant les surfaces : la table des raccourcis, `applyLearnedControls`, la
barre d'outils du piano roll, `gridTicks`.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D29.1 | **Les locateurs à la tête.** Cubase : I et O posent le début et la fin de la boucle à la tête ; ici, la région ne se pose qu'en tirant la règle ou sur une sélection (P) | `LoopStartAtPlayhead` (I) et `LoopEndAtPlayhead` (O) dans la table, par `setLoopRegionEverywhere` comme P ; poser un début après la fin repousse la fin d'une mesure, et l'inverse ; annulable ; `VSM_VUE=locateur-debut`, `locateur-fin` ; vérifié à l'écran |
| D29.2 | **Déplacer la tête d'un temps ou d'une mesure au clavier.** Cubase : + / − ; Live : rien ; ici, la tête se place à la souris ou à la mesure saisie (Maj+P) | `NavNextBeat` / `NavPreviousBeat` (Alt+→ / Alt+←), `NavNextBar` / `NavPreviousBar` (Maj+Alt+→ / ←), selon la signature sous la tête, jamais avant zéro ; `VSM_VUE=tete-temps:N`, `tete-mesure:N` ; vérifié par la barre de transport |
| D29.3 | **Le MIDI Learn écrit l'automation quand le W est armé.** Un potentiomètre lié au volume bouge le fader, mais la passe d'automation n'est ouverte que par la souris (D16.8) | `ChannelStrip::applyExternalControl` : la valeur reçue pose le curseur, la piste, et — W armé, transport en marche — ouvre ou nourrit la passe comme un glissé ; sans lâcher possible, la passe court jusqu'à l'arrêt (latch), quel que soit le mode, et c'est dit ; volume et panoramique ; les départs restent une valeur posée |
| D29.4 | **La ligne d'information des notes.** Cubase : la ligne d'info ; Live 12 : les champs de note ; ici, une note se règle à la souris seulement | dans la barre du piano roll : « Début » (mesure.temps, « +ticks » si hors temps), « Durée » (ticks), « Vélocité », relus 8 fois par seconde, ÉDITABLES : le début déplace toute la sélection de l'écart (par `nudgeSelection`), la durée et la vélocité se posent sur toutes les notes choisies ; annulables ; vérifié à l'écran |
| D29.5 | **La grille adaptative.** Live : la grille suit le zoom par défaut ; ici, 1/16 quel que soit le zoom | « Auto » dans le choix de grille : la subdivision la plus fine dont la case fait au moins 24 px, triolet et pointé respectés ; `gridTicks()` la calcule à chaque lecture ; vérifié à l'écran à deux zooms |

> **D29.1 EST FAITE (06/09/2026, 15:13).** I et O dans la table, par
> `setLoopRegionEverywhere` comme P ; un début posé après la fin repousse
> la fin d'une mesure, une fin posée avant le début ramène le début d'une
> mesure, une fin à zéro ne pose rien ; annulables. Vérifié à l'écran par
> `VSM_VUE=tete-mesure:1,locateur-debut,tete-mesure:1,locateur-fin` : le
> bouton Loop allumé, la région de la mesure 2 à la mesure 3 sur la règle du
> piano roll.
>
> **D29.2 EST FAITE (06/09/2026, 15:13).** Alt+→ / Alt+← d'un temps,
> Maj+Alt+→ / ← d'une mesure, selon la signature SOUS LA TÊTE, jamais avant
> zéro ; `tete-temps:N`, `tete-mesure:N`. Vérifié par la barre de transport
> : deux mesures puis deux temps donnent « mes. 3 · 3 », 4,615 s à 130 BPM.
>
> **D29.3 EST FAITE (06/09/2026, 15:13).** `ChannelStrip::applyExternalControl`
> : la valeur du MIDI Learn pose le curseur SANS le déclencher (c'est nous
> qui posons la piste et la passe, pas son rappel), et — W armé, transport
> en marche — ouvre la passe au premier message puis la nourrit comme un
> glissé ; un potentiomètre ne se lâche pas, la passe court jusqu'à l'arrêt
> comme en latch, quel que soit le mode, et c'est dit ici. Volume et
> panoramique ; les départs restent une valeur posée. Sans tranche (mélangeur
> pas encore construit), la valeur va au projet comme avant.
>
> **D29.4 EST FAITE (06/09/2026, 15:13).** Une TROISIÈME rangée dans la
> barre du piano roll : « Note : début · durée · vélocité », relus huit
> fois par seconde sauf pendant l'édition, éditables — le début (« 17.3 »,
> « 17.3+120 ») déplace toute la sélection de l'écart par `nudgeSelection`,
> la durée (ticks) et la vélocité se posent sur toutes les notes choisies
> (`setSelectionLength`, `setSelectionVelocity`), annulables. La première
> disposition prenait 300 px à droite de la rangée des réglages et cachait
> l'aimant, le pas à pas et le swing à la largeur du volet : la case a été
> agrandie, pas le texte réduit. Vérifié à l'écran : « 1.1 · 220 · 127 · 8 »
> pour les huit notes de la basse, la rangée des réglages intacte.
>
> **D29.5 EST FAITE (06/09/2026, 15:13), ET LA PHASE D29 EST CLOSE.**
> « Auto » dans le choix de grille : `gridTicks()` prend, de la plus fine à
> la ronde, la première subdivision dont la case fait 24 px, triolet et
> pointé respectés ; l'aimant, la quantification et la durée « = grille »
> la suivent d'office puisqu'ils lisent `gridTicks()`. Vérifié à l'écran
> par `grille-auto` à deux zooms (`zoom-piano:0.35` et `3`) : des lignes de
> mesure seules quand sept mesures tiennent dans le volet, des doubles-
> croches quand une seule y tient.
>
> Tests : 271 core, 1 257 audio, 278 interchange — tous verts ; Python
> inchangé (168 à D21).

### Phase D30 — Le seizième audit : ce qui manque une fois D29 posée (06/09/2026, 17:55)

**Pourquoi.** Même méthode : des candidats sondés, puis REVÉRIFIÉS en
nommant ce qu'on a cherché ET où. Le relevé a écarté, comme EXISTANT et
trouvé à la relecture — le tap tempo (`TransportBarComponent.cpp:66`),
l'aimantation au passage par zéro (D21.3), la sauvegarde automatique
(`AutosaveService`), l'export FLAC et Ogg (D20.5), le contournement de
toute la chaîne d'inserts (`setAllEffectsEnabled`), la courbure des
segments d'automation (`AutomationPoint::curve`, D17.7), le solo exclusif
(D21.2), les préréglages de piste (D22.5), le maintien de crête des
vu-mètres, le dithering à l'export (D14.4), la sélection des notes de même
hauteur, la saisie pas à pas (D13.5) et l'inversion temporelle des notes.

**Un piège du relevé, et il a servi deux fois.** Un premier sondage a
conclu « pas d'export FLAC ni Ogg » : le motif `mp3|flac|ogg` avait été
noyé par `ogg` dans `setClickingTogglesState` et `getToggleState`, et les
trois vraies lignes étaient au-delà du `head -3`. D20.5 avait fait ce
travail un jour plus tôt. C'est la leçon de CLAUDE.md — un « zéro » sorti
d'un grep se revérifie — appliquée à son symétrique : un « rien trouvé »
qui vient d'un motif trop court est aussi faux qu'un zéro mal cherché.

Cinq manques ont survécu à la revérification (cherchés dans `app/Source`,
`core`, `audio/include`, `audio/src`, `audio/plugins` et `interchange`) :

| Étape | Contenu | Terminé quand |
|---|---|---|
| D30.1 | **Le solo protégé.** Cubase : *Solo Defeat* ; Live : rien d'équivalent. Ici, un solo sur une piste coupe TOUT le reste — y compris le bus de réverbération, dont la sortie est justement ce qu'on veut entendre avec la piste soloée. Écouter une voix seule la donne sèche, ce qu'elle n'est pas dans le morceau | `Track::soloSafe` ; l'audibilité devient UNE fonction (`trackAudible`) au lieu des cinq copies de `anySolo ? track.solo : !track.muted` (`ProcessGraph` ×3, `PlaybackScheduler` ×2) ; protégée, la piste ignore le solo des AUTRES et garde son propre muet ; Alt+clic sur Solo ; écrit dans `project.json` seulement quand il est vrai ; mesuré : le solo d'une piste avec un départ de réverbération laisse la queue au mélange, le témoin sans protection ne l'a pas |
| D30.2 | **La piste désactivée.** Cubase : *Disable Track* ; ici, seul le muet existe, et une piste muette garde son instrument instancié et ses inserts en marche — un morceau reconstruit à quarante pistes paie les quarante machines même quand on n'en écoute qu'une | `Track::disabled` ; désactivée, la piste ne reçoit PAS d'instrument (`setTrackInstrument(i, "")`), pas de chaîne d'inserts, et le planificateur ne lui écrit aucun événement ; réactivée, tout revient (le matériau n'est jamais détruit) ; distinct du muet et du gel, et le dit ; écrit dans `project.json` ; mesuré : le nombre d'instruments tenus par le graphe tombe, et le rendu du reste ne bouge pas d'un bit |
| D30.3 | **Copier la chaîne d'effets d'une piste à l'autre.** Les préréglages existent par EFFET (D15.4) ; monter la même chaîne de quatre inserts sur six pistes de batterie demande vingt-quatre gestes | dans le menu **Piste** (et non dans le volet des effets, voir la décision ci-dessous) : « Copier la chaîne » / « Coller la chaîne » (remplace) et « Ajouter la chaîne » (à la suite) ; les paramètres ET l'état natif suivent, le contournement aussi ; annulable ; coller sur la piste d'origine ne se refuse pas mais ne duplique rien de vivant (une chaîne est une DESCRIPTION, pas des instances) ; vérifié à l'écran |
| D30.4 | **Le trim d'entrée.** Cubase : le gain avant le rack ; Live : un Utility en tête. Ici, le fader est APRÈS les inserts : pousser une piste dans son compresseur demande de rentrer dans le compresseur, ce qui déplace le réglage au lieu du niveau | `Track::inputTrimDb` (-24 à +24 dB, 0 par défaut), appliqué AVANT la chaîne d'inserts dans le graphe, sur la piste comme sur le groupe ; un curseur dans la tranche, remis à 0 dB au double-clic (D25.3) ; automatisable comme le volume (`mix.trim`) ; écrit seulement quand il n'est pas nul ; mesuré : à trim +6 dB et fader -6 dB, un insert NON linéaire (saturation) rend un signal DIFFÉRENT du même réglage sans trim — sans quoi le trim ne serait qu'un second fader |
| D30.5 | **Réduire les points d'une courbe d'automation.** Une passe en W (D16.8, et depuis D29.3 un potentiomètre MIDI) pose un point par tick touché : la courbe est illisible et impossible à retoucher à la main | `thinAutomation(curve, tolerance)` dans `core/` : une réduction par écart maximal (Ramer-Douglas-Peucker) qui GARDE les deux extrémités, les paliers (`step`) et les points dont la courbe s'écarterait de plus de `tolerance` ; « Réduire les points » dans le menu de la voie d'automation, tolérance à 1 % de l'amplitude du paramètre ; annulable ; mesuré et publié : le nombre de points AVANT et APRÈS sur une passe réelle, et l'écart maximal entre les deux courbes relues tick par tick, qui doit rester **sous la tolérance demandée** |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D30.1** — le mélange d'un solo sur une piste qui envoie à un bus
   protégé doit contenir la queue de réverbération ; le même projet sans
   protection ne doit PAS la contenir. Deux rendus, un seul réglage qui
   change : c'est l'A/B, et le témoin est le même code.
2. **D30.2** — désactiver une piste qui ne sonne pas dans la plage rendue
   doit laisser le rendu du reste **identique au bit près** (écart 0,0).
   Si l'écart n'est pas nul, ce n'est pas de l'arrondi : c'est que la
   désactivation a touché autre chose que la piste visée.
3. **D30.4** — j'attends que `trim +6 / fader -6` DIFFÈRE du réglage neutre
   dès qu'un insert non linéaire est en jeu, et qu'il soit **identique au
   bit près** quand la chaîne est vide ou purement linéaire. Le second est
   ce qui prouve que le trim est bien AVANT les inserts et nulle part
   ailleurs : sans insert, un gain +6 suivi d'un gain -6 rend le signal
   d'origine, à l'arrondi flottant près, que je mesure aussi.
4. **D30.5** — j'attends une réduction d'au moins **un ordre de grandeur**
   sur une passe réelle (des centaines de points vers des dizaines) pour un
   écart maximal sous la tolérance. Si la réduction est faible, l'algorithme
   ou la tolérance est en cause, et le chiffre le dira plutôt que de rester
   tu.

Aucune panne muette : ce qui est écarté, refusé ou remplacé est dit au
journal et à l'écran, comme partout ailleurs.

> **D30.1 EST FAITE (06/09/2026, 18:20).** `Track::soloSafe`, Alt+clic sur
> Solo et « Protéger cette piste du solo des autres » au menu Piste. **Le
> premier geste de l'étape n'a pas été d'ajouter un champ mais d'en finir
> avec cinq copies** : la règle d'audibilité était écrite cinq fois
> (`ProcessGraph` ×3 — le mélange d'une piste, celui d'un groupe, le rendu à
> l'arrêt — et `PlaybackScheduler` ×2 — le planning et la chasse). L'ajouter
> à quatre d'entre elles aurait donné une piste audible au mélange et muette
> au planning, c'est-à-dire une piste dont les notes ne partent pas mais dont
> la queue de réverbération sonne. Il n'y a plus qu'un `trackAudible(track,
> anySolo)`, dans `core/include/vsm/sequencer/Track.h`, et un test tient
> l'ancienne règle inchangée.
>
> **La mesure, un A/B dont la seule variable est la protection.** Une voix
> routée vers un bus de groupe, la voix soloée : crête du mélange **0,0 sans
> protection** (le bus n'est pas soloé, donc rien ne sort — le défaut qu'on
> répare) contre **une crête franche avec**. Et « ignore le solo des AUTRES »
> n'est pas « toujours audible » : une piste protégée ET muette reste muette,
> ce qu'un test vérifie dans les quatre combinaisons.
>
> Vu à l'écran (`VSM_VUE=mixer,solo-protege:1`) : le bouton Solo de « Drums »
> porte **« S+ » en turquoise**. Deux signes plutôt qu'un — le libellé et la
> couleur : un seul suffirait à l'œil qui sait ce qu'il cherche, il en faut
> deux à celui qui l'ignore.

> **D30.2 EST FAITE (06/09/2026, 18:25).** `Track::disabled` : la piste ne
> reçoit pas d'instrument (`setTrackInstrument(i, "")`), pas de chaîne
> d'inserts (`EffectChainComponent::rebuildFromProject`), et `renderTrackVoice`
> sort **avant** l'instrument, le fichier et les inserts — c'est là qu'est
> l'économie promise, là où le muet, lui, calcule puis jette.
>
> | | mesuré | attendu |
> |---|---|---|
> | désactiver une piste qui ne sonne pas dans la plage rendue | **écart 0,0 — au bit près** | 0,0 |
> | événements planifiés pour une piste désactivée | **0** | 0 |
>
> **CE QUE LA MESURE A OBLIGÉ À TRANCHER.** D18.7b avait posé que le muet
> d'une piste dont une AUTRE publie les sorties ne coupe pas la machine —
> couper la grosse caisse ne doit pas faire taire la caisse claire. Une piste
> désactivée n'a pas ce droit : sa machine n'est même pas instanciée, et lui
> écrire des événements les enverrait à personne. **Désactivée gagne donc sur
> « publiée »**, et c'est écrit dans `PlaybackScheduler` aux deux endroits.
>
> **CE QUE L'ÉCRAN A MONTRÉ, ET QUI A ÉTÉ CORRIGÉ.** Le voile gris de la
> ligne désactivée, posé dans `paint`, était **recouvert par le nom, le
> sélecteur de machine et les boutons** — les enfants se dessinent après leur
> parent. La ligne paraissait à peine plus sombre, c'est-à-dire pas
> désactivée du tout. Il est passé dans `paintOverChildren`, et la capture
> (`VSM_VUE=piste-eteinte:1`) montre « Drums » voilée avec **« désactivée »
> en rouge** — pendant que le rack, lui, annonce « (aucun instrument
> assigné) », ce qui est la vérité du moteur.

> **D30.3 EST FAITE (06/09/2026, 18:35).** « Copier la chaîne d'inserts » /
> « Coller la chaîne » / « Ajouter la chaîne à la suite », et les libellés
> **disent combien** : une commande grisée sans raison est une commande qu'on
> croit cassée. Le presse-papier tient des `TrackEffect`, c'est-à-dire des
> DESCRIPTIONS — identité, paramètres, état natif, contournement — et les
> instances sont refabriquées par le chemin habituel : une chaîne collée est
> montée exactement comme une chaîne saisie à la main.
>
> **LE CHOIX D'EMPLACEMENT, TRANCHÉ ICI PLUTÔT QUE DEMANDÉ.** Le tableau
> disait « dans le menu de la chaîne ». C'est le **menu Piste** qui l'a eu, et
> pour une raison qui n'était pas visible à l'écriture : le geste en demande
> TROIS à la suite — copier, changer de piste, coller — et le menu du volet
> des effets n'est atteignable qu'à la souris, donc invérifiable sans écran
> piloté. Trois commandes de vue (`copier-chaine`, `coller-chaine`,
> `ajouter-chaine`) l'accompagnent pour la même raison, `VSM_MENU` s'exécutant
> en bloc APRÈS `VSM_VUE` : on ne peut pas s'y intercaler un changement de
> piste. Vérifié à l'écran par
> `VSM_VUE=piste:0,copier-chaine,piste:1,coller-chaine,effets` : « Effets —
> Drums » liste Reverb, Delay, Chorus, et le journal dit « 3 insert(s) de
> "Acid Bass" » puis « 0 insert(s) avant, 3 après ».

> **D30.4 EST FAITE (06/09/2026, 18:30).** `Track::inputTrimDb`, -24 à +24 dB,
> appliqué dans `renderTrackVoice` **avant** la chaîne d'inserts et après tout
> le reste ; automatisable par `mix.trim` (`AutomationTarget::TrackTrim`, bit
> 10 du masque), **en décibels et non en gain** — le curseur est gradué en dB,
> et une courbe qui interpolerait le gain dessinerait une rampe et en ferait
> entendre une autre. Pas sur une piste gelée, pour la raison qui vaut pour
> les inserts : il est déjà dans le fichier gelé.
>
> | | mesuré | attendu |
> |---|---|---|
> | trim +6,0206 dB / fader ×0,5, **chaîne vide** | **écart < 1e-6** | identique au bit près |
> | le même réglage, **derrière un écrêteur** | **écart > 1e-3, et le RMS baisse** | différent |
> | trim à 0 dB | **écart 0,0** | rien ne change |
>
> Le premier chiffre est celui qui compte : sans insert, un gain +6 suivi d'un
> gain -6 rend le signal d'origine, ce qui prouve que le trim est bien AVANT
> les inserts **et nulle part ailleurs**. Le second prouve qu'il sert à
> quelque chose. Les deux ensemble sont ce qui distingue un trim d'un second
> fader. (Le +6,0206 dB n'est pas une coquetterie : 6 dB rond n'est pas une
> puissance de deux, et « au bit près » n'aurait pas eu de sens.)
>
> **CE QUE L'ÉCRAN A MONTRÉ, ET QUI A ÉTÉ CORRIGÉ DEUX FOIS.** La première
> capture donnait « -6,0 dB » nu en tête de tranche, au-dessus d'un fader qui
> n'écrit pas sa valeur : **rien ne distinguait le trim du volume**, et un
> réglage qu'on prend pour un autre est pire qu'un réglage caché. La case
> porte maintenant « Trim -6.0 dB », et la tranche a été **élargie de 76 à
> 88 px** pour que le mot tienne — entre « ça tient dans la case » et « ça se
> lit », c'est la lisibilité qui gagne. La seconde capture a montré le mot sur
> la piste réglée et un « 0.0 » nu sur l'autre : `setValue` d'une valeur DÉJÀ
> en place ne notifie rien, donc ne rappelle pas `textFromValueFunction` — le
> libellé manquait précisément là où rien n'avait bougé, c'est-à-dire sur
> presque toutes les tranches. Un `updateText()` le règle.

> **D30.5 EST FAITE (06/09/2026, 18:45), ET LA PHASE D30 EST CLOSE.**
> `thinAutomation(curve, tolerance)` dans `core/` : Ramer-Douglas-Peucker sur
> l'écart **vertical** au segment — le classique mesure une distance
> perpendiculaire, ce qui n'a pas de sens ici, l'axe horizontal étant du temps
> et l'axe vertical une valeur de paramètre ; leur « distance » dépendrait du
> zoom. Ce qu'on veut borner est l'écart entre ce qu'on entendait et ce qu'on
> entendra, donc l'écart de VALEUR à tick égal.
>
> **CE QUI A FAILLI FAIRE MENTIR LA TOLÉRANCE.** Les paliers (`step`) coupent
> la courbe en tronçons — évident. La COURBURE (`curve`, D17.7) aussi, et elle
> avait été oubliée : sur un segment fléchi, la droite que la simplification
> suppose n'existe pas, et l'écart qu'elle mesure est celui d'une courbe qu'on
> ne joue pas. Les deux sortes de point sont gardées, avec leur voisin de
> droite.
>
> | passe mesurée | avant | après | facteur | écart max | tolérance |
> |---|---|---|---|---|---|
> | rampe droite (2 mesures) | 961 | **2** | 480 | 0,000000 | 0,010 |
> | montée puis descente | 961 | **3** | 320 | 0,000000 | 0,010 |
> | passe au fader, avec le tremblement du geste | 1 921 | **16** | 120 | 0,008297 | 0,010 |
> | la même, tolérance deux fois plus fine | 1 921 | **26** | 74 | 0,004722 | 0,005 |
> | **la passe du projet d'essai, dans l'application** | **1 921** | **13** | **148** | **0,013957** | **0,015** |
>
> **L'attendu était « au moins un ordre de grandeur » : c'est deux.** Et
> l'écart tient sous la tolérance dans les cinq cas — mesuré sur l'UNION des
> ticks des deux courbes, donc AUX POINTS RETIRÉS, là où il est le plus grand.
> Mesuré sur les seuls ticks de la courbe réduite, il vaudrait zéro à tous les
> coups : un chiffre qui se contente de confirmer ce qu'on veut croire.
>
> Vu à l'écran (`VSM_VUE=arrangement,piste:0,courbes,reduire-automation:0`) :
> avant, un ruban orange épais et sans prise ; après, la même forme avec
> **treize poignées** qu'on peut saisir. La tolérance est 1 % de l'amplitude
> du paramètre, prise de `arrangement_.automationRange` — la MÊME source que
> le dessin, deux amplitudes finissant par donner deux tolérances. **Une
> courbe dont l'amplitude est inconnue est laissée ENTIÈRE et NOMMÉE au
> journal**, jamais réduite au jugé.

> **UNE PANNE MUETTE TROUVÉE ET BOUCHÉE EN CHEMIN, ET C'EST PEUT-ÊTRE LE PLUS
> UTILE DE LA PHASE.** La première vérification de D30.1 a été lancée avec
> `VSM_VUE=melangeur` (pour « mixer »). La capture est sortie, elle montrait
> l'écran d'accueil, et **rien** ne distinguait « la commande n'existe pas » de
> « le réglage n'a rien changé » : la chaîne de `else if` d'`applyViewCommand`
> n'avait pas de `else`. La panne muette vivait donc dans l'outil qui sert à
> prouver que les interfaces marchent — l'endroit où elle coûte le plus cher.
> Une commande inconnue est désormais dite sur stderr.
>
> Tests : 276 core, 1 266 audio, 280 interchange, 25 clap, 11 panels, 19 vst3
> — tous verts (16 tests neufs) ; Python inchangé (168).

### Phase D31 — Les effets MIDI de piste (06/09/2026, 19:00)

**Pourquoi, et pourquoi ce n'est pas un audit de plus.** Le seizième audit
a sondé six candidats de plus après D30 et en a écarté quatre comme
existants (« Enregistrer sous » — le premier motif cherché, `sauvegarder
sous`, était le mauvais français ; la compensation de latence D4.5 ;
l'aimantation ; la vélocité de relâchement). Deux manques ont survécu, et
l'un d'eux n'est pas un bouton absent mais **une catégorie entière** : les
effets MIDI. `grep -riE "insert MIDI|midiInsert|effet MIDI"` sur
`app/Source`, `core`, `audio/include`, `audio/src` et `interchange` ne rend
RIEN.

Ce sont les *MIDI inserts* de Cubase et le *MIDI Effects rack* de Live —
chez Live, une fonction d'affiche. Ici, `arpeggiateNotes` existe bien
(`core/src/sequencer/NoteEdit.cpp`), mais c'est une ÉDITION : elle écrit
dans les notes, et l'on ne revient pas en arrière autrement qu'en annulant.
Régler un arpège en écoutant, changer d'avis, garder le matériau intact :
impossible aujourd'hui.

**Où cela se branche, et pourquoi c'est le bon endroit.**
`PlaybackScheduler` porte depuis la Phase 1 une promesse écrite dans son
en-tête : « un seul et même calcul de timing pour la lecture live et
l'export, donc pas de divergence possible entre ce qu'on entend et ce qu'on
exporte ». La transposition de piste (D17.5) s'y applique déjà, à la
lecture, sans toucher au matériau. Les effets MIDI suivent le même chemin
et en héritent tout : la lecture, le rendu hors ligne, le gel, le report,
les ports matériels de D27 — tous passent par là.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D31.1 | **La chaîne dans le modèle et le fichier.** `Track::midiEffects`, sur le modèle de `TrackEffect` (identité, paramètres, contournement) | `struct MidiEffect { type, parameters, enabled }` et `std::vector<MidiEffect> midiEffects` dans `Track` ; écrite dans `project.json` SEULEMENT quand elle n'est pas vide — un projet d'avant D31 se relit et se réécrit octet pour octet ; aller-retour testé |
| D31.2 | **Les trois effets, en fonctions PURES dans `core/`.** « Gamme » est écartée : le piano roll contraint déjà à une gamme (`gridTicks`, D29.5, et le choix de gamme existant), et un second endroit qui décide de la même chose finirait par le décider autrement | `applyMidiEffects(effects, notes, ppq)` : **Transposition** (demi-tons ; hors 0..127 la note est ÉCARTÉE et COMPTÉE, jamais repliée à l'octave — comme D17.5) ; **Vélocité** (échelle en % puis décalage, bornée 1..127, jamais 0 — une vélocité nulle est un NoteOff déguisé) ; **Arpégiateur** (les notes qui SONNENT ENSEMBLE deviennent une suite, pas en ticks, modes montant / descendant / aller-retour, sur la durée de l'accord) ; testées séparément et enchaînées |
| D31.3 | **L'application à la lecture, non destructive.** | dans `PlaybackScheduler::build`, UNE FOIS par piste avant la boucle des passages -- et NON dans `chaseAt`, vérifié en la lisant : la chasse ne rattrape que des contrôleurs, des plis et des programmes, jamais des notes, et lui donner une chaîne qui transforme des notes n'aurait rien eu à transformer ; `track.notes` n'est jamais modifié ; mesuré : le rendu hors ligne d'une piste arpégée contient N attaques là où le matériau en a une, et le projet rechargé a toujours ses notes d'origine |
| D31.4 | **Le volet et l'écran.** | une section « Effets MIDI » au-dessus des inserts audio dans le volet des effets : ajouter, contourner (On/Off), monter/descendre, retirer, et les paramètres du choisi ; menu Piste pour le clavier et pour `VSM_MENU` ; vérifié à l'écran |
| D31.5 | **La divergence de l'export MIDI, DITE.** L'export `.mid` écrit le matériau (`toParsedFile`), pas ce qui est joué : un `.mid` exporté d'une piste arpégée ne contient pas l'arpège — et la transposition de piste de D17.5 est dans le même cas depuis un an, en silence | l'export nomme les pistes dont le `.mid` ne portera ni les effets MIDI ni la transposition, et combien ; et « Piste ▸ Reporter les effets MIDI dans les notes » les rend définitifs quand c'est ce qu'on veut (annulable) |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D31.2/D31.3** — un accord de trois notes tenu une ronde, arpégé au pas
   de la double-croche, doit donner **seize attaques** sur cette ronde
   (quatre noires × quatre doubles-croches), et non trois. Si le compte
   diffère, c'est le découpage de l'accord ou le pas qui est faux, et le
   chiffre le dira.
2. **Non-destructif** — après lecture, rendu et rechargement, `track.notes`
   doit être **identique note pour note** au matériau d'origine. C'est ce
   qui distingue un effet d'une édition, et c'est falsifiable.
3. **Rien ne change pour ce qui existe** — une piste sans effet MIDI doit
   donner un planning **identique événement pour événement** à celui
   d'avant la phase, et un rendu **identique au bit près**.
4. **D31.5** — j'attends que l'avertissement d'export nomme AUSSI les pistes
   seulement transposées : la divergence de D17.5 existait déjà et n'avait
   jamais été dite. Si l'avertissement ne les nomme pas, il ne couvre que le
   neuf et laisse l'ancien mentir.

> **D31.1 EST FAITE (06/09/2026, 19:20).** `Track::midiEffects`, une
> `std::vector<MidiEffect>` de même forme que `TrackEffect` — identité,
> paramètres, contournement — et sans `nativeState` : ces effets sont écrits
> ici, leur son EST leur table de paramètres, et rien n'est prévu pour
> héberger un effet MIDI tiers. Écrite dans `project.json` seulement quand
> elle n'est pas vide ; un projet d'avant la phase n'y gagne pas un octet,
> ce qu'un test vérifie en cherchant la clé dans le texte.

> **D31.2 EST FAITE (06/09/2026, 19:30).** `applyMidiEffects` dans
> `core/`, en fonctions pures. Le chiffre attendu était seize attaques, et
> c'est seize.
>
> | mesure | attendu | mesuré |
> |---|---|---|
> | accord de 3 notes, une ronde, pas de double-croche | 16 attaques | **16** |
> | l'arpège remplit la place de l'accord | début 0, fin 1920 | **0 → 1920** |
> | aller-retour sur 3 notes | do, mi, sol, mi | **do, mi, sol, mi** |
> | transposition de -24 sur une note n° 10 | écartée et comptée | **1 écartée** |
> | vélocité × 0 puis -100 | jamais 0 | **1, sur les deux notes** |
>
> **TROIS CHOSES QU'IL A FALLU TRANCHER EN ÉCRIVANT, et qui ne se voyaient
> pas depuis le tableau.**
>
> 1. **Ce qu'est un accord.** Les notes qui commencent AU MÊME TICK, et non
>    celles qui se recouvrent : deux notes dont l'une commence au milieu de
>    l'autre sont une tenue, et les arpéger déplacerait la seconde. Un test
>    le tient.
> 2. **L'aller-retour ne redouble pas ses extrêmes.** Do-mi-sol-mi et non
>    do-mi-sol-sol-mi : rejouer la note du haut deux fois de suite marque un
>    temps, ce qui n'est pas ce qu'on entend d'un arpégiateur.
> 3. **La vélocité est bornée à 1 et non à 0.** Une vélocité nulle est un
>    NoteOff déguisé dans le MIDI : une piste qu'on voulait seulement
>    adoucir se serait arrêtée de sonner sans qu'aucune note ait disparu.
>
> Et le pas de l'arpégiateur est **en fractions de noire**, pas en ticks :
> en ticks il dépendrait de `ticksPerQuarterNote`, et un projet relu à une
> autre résolution n'arpégerait plus pareil. Chaque attaque reçoit un
> IDENTIFIANT NEUF — seize attaques qui porteraient trois identifiants se
> confondraient trois par trois pour la sélection, l'automation liée et le
> tirage déterministe de l'humanisation.
>
> « Gamme » a été écartée en chemin, et c'est écrit dans le tableau : le
> piano roll contraint déjà à une gamme, et un second endroit qui décide de
> la même chose finirait par le décider autrement.

> **D31.3 EST FAITE (06/09/2026, 19:35).** Dans `PlaybackScheduler::build`,
> une fois par piste, avant la boucle des passages — avant et non dedans,
> pour le coût (arpéger une fois plutôt qu'une fois par clip) et pour le
> sens (un accord ne change pas selon le clip par lequel on le regarde).
> Le planificateur étant le seul calcul de timing du projet, la lecture, le
> rendu hors ligne, le gel, le report et les ports matériels de D27 en
> héritent tous sans une ligne de plus.
>
> | mesure | attendu | mesuré |
> |---|---|---|
> | planning de l'accord sans chaîne | 6 événements | **6** |
> | le même, arpégé | 32 événements | **32** |
> | `track.notes` après lecture | intact | **3 notes, fin 1920 — intact** |
> | piste sans chaîne | planning identique | **identique, transposition D17.5 comprise** |
>
> **UNE QUESTION QUE LE TABLEAU NE POSAIT PAS : les deux transpositions.**
> Celle de la PISTE (D17.5) et l'effet MIDI de transposition (D31.2)
> pourraient se remplacer ou s'ajouter. **Elles s'ajoutent**, et c'est
> testé : 60 + 5 (effet) + 2 (piste) = 67. Ce sont deux réglages distincts,
> posés à deux endroits, et faire disparaître l'un quand l'autre est posé
> serait un réglage qui s'éteint sans qu'on l'ait touché.

> **D31.4 EST FAITE (06/09/2026, 19:50).** Une section « Effets MIDI (sur
> les notes, avant la machine) » AU-DESSUS des inserts audio dans le volet
> des effets — et l'ordre est le sens : ce qui se lit de haut en bas doit
> être ce qui se traverse du premier au dernier, comme dans la tranche de
> console de D30.4. Rangées à part et non mêlées aux inserts : un effet MIDI
> n'a ni preset, ni latence, ni chaîne latérale, et lui donner les boutons
> des autres serait promettre des réglages sans effet. On ajoute par le
> sous-menu « Piste ▸ Effets MIDI » (qui dit combien la chaîne en porte), on
> contourne, on déplace, on retire ; le bandeau de paramètres est le MÊME que
> celui des inserts audio, `selectedIsMidi_` disant seulement laquelle des
> deux listes est visée. **Un effet qu'on vient d'ajouter est choisi** — ce
> qu'on ajoute est ce qu'on veut régler, la même règle que la duplication de
> clips qui rend la sélection des copies.
>
> **CE QUE L'ÉCRAN A MONTRÉ, ET QUI ÉTAIT UN DÉFAUT PLUS ANCIEN QUE LA
> PHASE.** Avec la chaîne MIDI ajoutée au-dessus, les libellés des knobs
> (« Division », « Mode ») tombaient sous le bas de la fenêtre. **Le volet
> des effets ne défilait pas** : six inserts audio et un effet choisi
> faisaient déjà déborder le dock, cela ne s'était simplement jamais vu. Tout
> le contenu vit maintenant dans un `Viewport`, et la capture, défilée de
> 80 px, montre les deux libellés en entier.
>
> **DEUX PIÈGES PAYÉS EN CHEMIN, ET C'EST DEUX FOIS LE MÊME.** `setSize`
> d'une taille INCHANGÉE ne déclenche pas `resized()` : après un
> `rebuildEffectList` qui n'avait pas changé la hauteur, les rangées neuves
> restaient sans bornes, donc invisibles. Et `scrollBy` appelé depuis une
> commande de vue ne faisait rien : les commandes s'exécutent avant que la
> disposition ne soit posée, le contenu n'était pas encore plus haut que le
> volet, et il n'y avait rien à faire défiler — la demande arrivait, ne
> bougeait rien, et la capture montrait le haut de la liste comme si le
> défilement n'existait pas. C'est exactement le `setValue` de D30.4, à un
> autre étage : **une API qui ne fait rien quand rien n'a changé ne
> rafraîchit pas non plus ce qui, lui, a changé.**

> **D31.5 EST FAITE (06/09/2026, 20:00), ET LA PHASE D31 EST CLOSE.**
> L'export `.mid` écrit le MATÉRIAU (`Project::toParsedFile`), pas ce que la
> lecture en fait. Il nomme désormais les pistes dont le fichier ne portera
> pas ce qu'on entend, et « Piste ▸ Effets MIDI ▸ Reporter les effets MIDI
> dans les notes » les rend définitifs quand c'est ce qu'on veut (annulable ;
> la chaîne est VIDÉE après le report, sans quoi elle s'appliquerait une
> seconde fois à ce qu'elle vient d'écrire — un arpège arpégé, le geste rendu
> deux fois pour un seul clic).
>
> **L'ATTENDU N° 4 EST TENU, ET C'EST LE PLUS INTÉRESSANT DES QUATRE.**
> L'avertissement devait nommer AUSSI les pistes seulement transposées : la
> divergence de D17.5 existait depuis un an et n'avait jamais été dite. Vérifié
> par `VSM_VUE=piste:0,fx-midi:arpeggio,diagnostic-export-midi` sur un projet
> dont la seconde piste est transposée de -5 :
>
> ```
> Export MIDI — ne seront pas portées : Acid Bass (effets MIDI) ; Drums (transposition -5)
> ```
>
> et, sur un projet où rien ne diverge, « le .mid portera tout ce qui est
> joué » — un avertissement qui parlerait pour ne rien dire cesserait d'être
> lu. `diagnostic-export-midi` existe pour cela : un avertissement qu'on ne
> peut déclencher qu'en ouvrant un sélecteur de fichier est un avertissement
> qu'on ne peut pas vérifier sans souris, donc qu'on ne peut pas déclarer
> vérifié.
>
> **CE QUI EST MESURÉ DANS L'APPLICATION, ET CE QUI NE L'EST PAS.** Le report
> a été exercé de bout en bout (`8 note(s) -> 8` sur une piste sans accord,
> chaîne vidée) : cela prouve le CÂBLAGE. La TRANSFORMATION, elle, est prouvée
> par les tests de `core/` (3 notes → 16 attaques), qui appellent la même
> fonction. Les deux ensemble couvrent le chemin ; ni l'un ni l'autre seul.
>
> Tests : 286 core, 1 266 audio, 282 interchange, 25 clap, 11 panels, 19 vst3
> — tous verts (12 tests neufs) ; Python inchangé (168).

### Phase D32 — Le dix-septième audit : ce qui manque une fois D31 posée (06/09/2026, 20:25)

**Pourquoi.** Même méthode, et les zéros revérifiés en nommant ce qu'on a
cherché ET où (`app/Source`, `core`, `audio/include`, `audio/src`,
`interchange`). Le relevé a écarté, comme EXISTANT et retrouvé à la
relecture : l'éditeur de contrôleurs (`MidiCcComponent`, avec ses paliers et
son historique), le rognage d'un clip à son son (`trimClipToSound`), le
report en place, les prises empilées, la piste de référence et son écoute
A/B, le filtre de la liste des pistes, les notes de projet (D18.6) et le
gel des pistes MIDI.

**Cinq manques ont survécu, et trois d'entre eux servent le second axe du
projet** — la fidélité de la reconstruction. Une reconstruction produit
quarante pistes nommées par la machine et des milliers d'événements : rien
aujourd'hui ne permet de les COMPTER, de les LIRE en nombres, ni de les
renommer autrement qu'une par une.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D32.1 | **La pré-écoute dans le navigateur.** Live : un clic sur un échantillon le joue. Ici, le navigateur indexe les échantillons (`BrowserItemKind::Sample`) et n'a qu'un `onApply` : pour entendre l'un de deux cents fichiers, il faut le poser sur une piste, écouter, annuler | un `AuditionPlayer` dans le graphe, sur le modèle de `ReferenceTrack` : tampon publié par échange atomique, mélangé APRÈS le master, JAMAIS dans l'export, coupé net au déclenchement suivant ; un bouton « ▶ » par ligne d'échantillon dans le navigateur ; mesuré : le rendu hors ligne d'un projet dont la pré-écoute tourne est identique AU BIT PRÈS à celui d'avant |
| D32.2 | **La liste des événements.** Cubase : l'éditeur de liste. Ici, les notes se voient au piano roll et les CC dans leur onglet ; les changements de programme, les plis de hauteur, la pression de canal et la pression polyphonique n'ont AUCUNE vue — le modèle les porte, le séquenceur les joue, l'import les garde, et personne ne peut les lire | un onglet « Liste » : tous les événements de la piste choisie, triés par tick, avec leur mesure·temps, leur nature, leurs nombres bruts ; filtrable par nature ; double-clic = la tête va là ; Suppr retire l'événement choisi (annulable) ; vérifié à l'écran sur une piste qui porte les cinq natures |
| D32.3 | **Le clavier à l'écran.** Cubase : le clavier virtuel. Ici, le clavier d'ordinateur joue (D22.4) mais RIEN ne montre ce qui sonne — ni ce qu'on joue, ni ce que le morceau joue | un `juce::MidiKeyboardComponent` sous le piano roll, qui ENVOIE les notes par le même chemin que le clavier d'ordinateur et qui MONTRE les notes que le transport joue sur la piste choisie ; `VSM_VUE=clavier` ; vérifié à l'écran, touche enfoncée visible |
| D32.4 | **Renommer les pistes en série.** Une reconstruction rend « Track 01 », « Track 02 »... ; les renommer se fait une par une | « Piste ▸ Renommer les pistes en série » : un motif avec `#` pour le numéro (« Batterie # » → « Batterie 1 »...), appliqué aux pistes VISIBLES, annulable ; dit combien il en a renommé ; `VSM_VUE=renommer-serie:motif` |
| D32.5 | **Les statistiques du projet.** Rien ne dit combien un projet porte de pistes, de notes, de clips, ni sa durée — c'est précisément ce qu'on veut lire d'une reconstruction pour la comparer à l'original | « Fichier ▸ Statistiques du projet » : pistes (par nature), notes, clips, courbes et points d'automation, événements MIDI par nature, machines employées, durée du matériau ; sur stderr aussi, pour qu'un rendu piloté le publie ; `VSM_VUE=statistiques` |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D32.1** — j'attends que le rendu hors ligne d'un projet soit **identique
   au bit près** (écart 0,0) qu'une pré-écoute soit chargée ou non. Si
   l'écart n'est pas nul, la pré-écoute a fui dans l'export, ce qui est
   exactement le piège que `ReferenceTrack` avait déjà eu à éviter.
2. **D32.2** — j'attends que la liste compte, sur une piste portant les cinq
   natures, **exactement autant de lignes que le modèle porte d'événements**.
   Un écart dirait qu'une nature est oubliée par la vue, ce qui est le défaut
   qu'on répare.
3. **D32.4** — j'attends que le renommage laisse **le nombre de pistes et
   leur ordre inchangés** : renommer n'est pas déplacer, et un motif mal formé
   ne doit rien renommer plutôt que renommer à moitié.
4. **D32.5** — le compte de notes des statistiques doit être **le même** que
   celui que la liste de D32.2 affiche pour la même piste. Deux comptages qui
   divergeraient diraient que l'un des deux ne regarde pas tout.

> **D32.1 EST FAITE (06/09/2026, 20:55).** `AuditionPlayer` dans le graphe,
> sur le modèle de `ReferenceTrack` : tampon publié par échange atomique,
> mélangé APRÈS le master, coupé par l'export et remis dans l'état où il
> l'avait trouvé. Un clic simple sur une ligne d'échantillon la joue —
> comme chez Live, sans bouton séparé : la ligne entière est la cible, ce
> qui est ce dont on a besoin quand on parcourt deux cents fichiers. Le
> double-clic continue d'APPLIQUER. Un « ▶ » apparaît sur la ligne survolée,
> et seulement sur un échantillon : sur un preset il promettrait un son
> qu'on ne sait pas rendre sans instrument.
>
> | mesure | attendu | mesuré |
> |---|---|---|
> | rendu avec / sans pré-écoute chargée | écart 0,0 | **0,0 — au bit près** |
> | pré-écoute transport ARRÊTÉ | s'entend | **crête > 0,1** |
> | arrivée au bout | s'arrête, ne boucle pas | **arrêtée après 2 blocs** |
>
> **DEUX DÉFAUTS QUE LES TESTS ONT ATTRAPÉS, ET LE SECOND ÉTAIT DANS LE
> TEST.**
>
> 1. **La pré-écoute ne s'entendait pas transport arrêté**, c'est-à-dire
>    presque jamais — on parcourt un dossier d'échantillons morceau à
>    l'arrêt. `processBlock` sort tôt par deux court-circuits quand rien n'a
>    à être rendu, et le mélange n'était écrit qu'en fin de bloc. Il passe
>    maintenant sur CHACUNE des sorties de ce chemin.
> 2. **Le premier test de fuite accusait la pré-écoute à tort** : il
>    comparait deux rendus enchaînés sur LE MÊME graphe, et mesurait donc la
>    mémoire des machines (phase d'oscillateur, charge de filtre) — écart
>    0,251, la leçon de D18.1 repayée. L'A/B se fait sur DEUX GRAPHES NEUFS
>    avec une seule variable, et un test-témoin est resté pour dire pourquoi.

> **D32.2 EST FAITE (06/09/2026, 21:10).** Un onglet « Liste » entre
> « MIDI CC » et « Tempo » : tous les événements de la piste, triés par tick,
> en NOMBRES BRUTS. `listTrackEvents` et `removeTrackEvent` sont des
> fonctions pures de `core/`, ce qui rend l'attendu vérifiable sans ouvrir de
> fenêtre.
>
> **L'attendu est tenu.** Sur une piste portant les six familles, la liste
> compte **8 lignes pour 8 événements** dans le test, et **13 à l'écran**
> pour une piste de 8 notes plus les cinq familles ajoutées — Programme 12,
> Pli **-4096**, CC 74 = 64, Pression poly 60/90, Pression canal 55. Aucune
> de ces cinq n'avait jamais eu de vue.
>
> La position se lit **mesure.temps ET tick brut** : la mesure pour se
> repérer dans le morceau, le tick parce que c'est ce que le fichier porte et
> ce que la chaîne d'analyse écrit ; donner l'un sans l'autre obligerait à
> convertir de tête. Supprimer une ligne périmée est REFUSÉ et dit, plutôt
> que de retirer le voisin — un test le tient.
>
> **UN PIÈGE POSÉ ET REFERMÉ DANS LA MÊME HEURE.** Insérer l'onglet avant
> « Tempo » a décalé ce dernier d'un rang, et `VSM_VUE=tempo` ouvrait la
> liste : les index d'onglets étaient écrits en dur. Ils se cherchent
> désormais par NOM (`getTabNames().indexOf`), et un onglet introuvable se
> dit. Un numéro en dur est un piège qui se referme au premier onglet ajouté.

> **D32.3 EST FAITE (06/09/2026, 21:35).** Un `MidiKeyboardComponent` sous
> la lane de vélocité, caché par défaut (`VSM_VUE=clavier`) : le piano roll a
> déjà son clavier vertical, et soixante-douze pixels pris à l'édition
> doivent se demander. Il ENVOIE par le même chemin que le clavier
> d'ordinateur (D11.7) — deux chemins pour une seule idée finiraient par ne
> plus jouer pareil — et il MONTRE ce que la piste choisie joue.
>
> **CE QUE MONTRER A COÛTÉ, ET POURQUOI CE N'ÉTAIT PAS GRATUIT.** Le graphe
> tenait déjà `soundingNotes_`, un `std::array<bool,128>` par piste — écrit
> par le thread audio. Le lire depuis l'interface aurait été une course, et
> un tableau de booléens lu pendant qu'on l'écrit n'a pas de valeur définie.
> Un masque de 128 bits en deux entiers atomiques l'accompagne désormais,
> tenu par `marquerSonnante`, **seul endroit qui écrit l'un et l'autre** :
> cinq sites d'écriture y passent, et un `soundingNotes_[t][n] = x` laissé
> ailleurs aurait fait mentir le voyant. Lecture relâchée : le voyant peut
> être en retard d'un bloc, il ne doit pas être une course.
>
> **TROIS CAPTURES POUR UNE TOUCHE, ET CHACUNE A ÉCARTÉ UNE CAUSE.** La
> première ne montrait rien : le voile de touche enfoncée par défaut de JUCE
> est presque invisible sur du blanc — le masque, lui, était juste
> (`0x1000000000`, la note 36), et seul un journal l'a établi. La deuxième
> non plus : la plage commençait à 36, et cette note tombait **sous le bouton
> de défilement** du composant ; on a descendu la plage plutôt que de rogner
> le bouton. La troisième non plus, et c'est la plus instructive : **le
> morceau était fini**. La capture a lieu deux secondes après l'ouverture et
> le matériau de démonstration dure 1,85 s ; il n'y avait rien à allumer.
> `VSM_LECTURE=1500` retarde le départ, et la touche do2 s'allume en ambre
> sous la tête de lecture. Une interface déclarée « cassée » l'était trois
> fois pour trois raisons dont aucune n'était le code qu'on soupçonnait.

> **D32.4 EST FAITE (06/09/2026, 21:45).** « Renommer les pistes en série » :
> un motif où `#` devient le numéro d'ordre, appliqué aux pistes VISIBLES —
> on renomme ce qu'on VOIT, et le numéro ne compte pas les masquées, un trou
> dans la numérotation ferait chercher la piste manquante. **Un motif sans
> `#` ne renomme RIEN** et le dit : quarante pistes portant toutes le même
> nom seraient pires qu'avant, et l'on aurait perdu les noms d'origine
> par-dessus le marché. L'attendu est tenu — le nombre de pistes et leur
> ordre ne bougent pas, rien n'est republié au moteur : renommer n'est pas
> déplacer.

> **D32.5 EST FAITE (06/09/2026, 21:50), ET LA PHASE D32 EST CLOSE.**
> « Fichier ▸ Statistiques du projet » : pistes par nature, notes, clips,
> courbes et points, les cinq familles d'événements MIDI, les machines
> employées avec leur nombre de pistes, la durée du matériau et le tempo de
> départ. Sur stderr aussi — un chiffre qu'on ne peut pas relire hors de
> l'écran ne sert pas à comparer deux reconstructions, et c'est justement à
> cela qu'il sert ici.
>
> **L'ATTENDU N° 4 EST TENU, ET C'ÉTAIT SA RAISON D'ÊTRE.** Les statistiques
> comptent **16 notes** sur le projet d'essai ; la liste de D32.2 en compte
> **8 pour « Acid Bass »** et **8 sur les 13 lignes de « Drums »**. Deux
> comptages écrits séparément qui tombent juste l'un sur l'autre : c'est ce
> qui dit qu'aucun des deux ne regarde à côté.
>
> Tests : 292 core, 1 270 audio, 282 interchange, 25 clap, 11 panels, 19 vst3
> — tous verts (10 tests neufs) ; Python inchangé (168).

### Phase D33 — Le dix-huitième audit : ce qui manque une fois D32 posée (06/09/2026, 23:50)

**Pourquoi.** Même méthode, zéros revérifiés en nommant ce qu'on a cherché
ET où. Le relevé a écarté, comme EXISTANT et retrouvé à la relecture : la
normalisation à l'export (D21.5, `VSM_EXPORT_NIVEAU`), le choix par
vélocité et par durée dans le piano roll (D21.1 — `selectNotesBelowVelocity`,
`selectShorterThan` : un « éditeur logique » complet en serait la
généralisation, pas le comblement d'un trou), l'aimantation des coupes au
passage par zéro (D21.3), le découpage aux transitoires, le rognage d'un
clip à son son, et le gel des pistes MIDI.

Cinq manques ont survécu, et **quatre d'entre eux concernent l'AUDIO** —
c'est le côté du logiciel que les dix-sept phases précédentes ont le moins
touché, et la reconstruction en produit pourtant des dizaines de fichiers.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D33.1 | **Importer plusieurs fichiers audio d'un coup.** `importAudioFilePrompt` n'ouvre qu'`canSelectFiles` et ne lit que `getResult()` : une reconstruction qui rend douze stems se réimporte en douze gestes | `canSelectMultipleItems`, une piste neuve PAR FICHIER, nommée d'après lui ; ce qui échoue est nommé fichier par fichier et n'arrête pas le reste ; le compte rendu dit combien sont entrés et combien ont été refusés |
| D33.2 | **Le fondu automatique aux bords des clips audio.** `fadeGain` ne s'applique QUE si l'utilisateur a posé un fondu (`fadeInFrames > 0`) : un clip dont le matériau ne commence pas à zéro claque à chaque bord. Cubase et Live posent tous deux un fondu de quelques millisecondes d'office | un fondu de sécurité de **2 ms** aux deux bords de CHAQUE span audio, appliqué seulement là où l'utilisateur n'a rien posé (son fondu à lui gagne, toujours) ; réglable et désactivable dans les préférences ; mesuré : le plus grand saut d'un échantillon au suivant au bord d'un clip qui coupe une sinusoïde en pleine amplitude doit tomber d'au moins un ordre de grandeur |
| D33.3 | **Le scrub.** Cubase : tirer la tête de lecture fait entendre ce qu'elle traverse. Ici la tête se pose et se tait — retrouver un point précis dans une prise se fait en lançant la lecture et en la rattrapant | tirer sur la règle avec **Ctrl** (et non Alt, déjà pris par la région de punch — voir la note de D33.3) joue le morceau à la vitesse du geste (`setPlaybackSpeed`, D18.5, déjà là) ; relâcher rend le silence et remet la vitesse à 1 ; borné à ±4× (au-delà, on n'entend plus rien d'utile) ; vérifié à l'écran |
| D33.4 | **Geler une piste AUDIO.** Le gel est réservé aux pistes MIDI (`gelable = kind == Midi`) alors que `renderTrackForFreeze` isole une piste QUELCONQUE : une piste audio portant une réverbération à convolution et un pitch-shift coûte à chaque bloc ce qu'un fichier coûterait une fois | le gel accepte une piste audio ; mesuré : le fichier gelé et le rendu de la piste vive sont identiques à **moins de -120 dBFS** près (la même barre que D18.7b, et pour la même raison — deux chemins de mixage, pas un arrondi près) ; **le GROUPE reste refusé, et le refus est écrit ici avec sa raison** |
| D33.5 | ~~**L'aimant relatif.**~~ **CE MANQUE N'EN ÉTAIT PAS UN, et c'est écrit plutôt qu'effacé** (voir la note ci-dessous). À sa place : **les couleurs de piste à l'import MIDI, et une seule palette pour tout le monde.** Il y en avait QUATRE — `addTrack`, `DawImport`, la chaîne Python, le défaut bleu de `Track` — et l'import d'un `.mid` n'en employait aucune : les quinze pistes d'un morceau ouvert par « Ouvrir MIDI... » arrivaient toutes du même bleu | `trackColourForIndex` dans `core/`, employée par les trois chemins C++ ; `ProjectImport` pose une couleur de rang quand la source n'en portait pas, et ne remplace JAMAIS une couleur déjà posée ; testé |

> **UN MANQUE DU RELEVÉ QUI N'EN ÉTAIT PAS UN (06/09/2026, 23:58).**
> « L'aimant relatif » a été inscrit au tableau parce que `grep -iE "aimant
> relatif|relativeSnap"` ne rendait rien. Le MOT était absent ; le
> COMPORTEMENT était là depuis toujours. `ArrangementComponent` ne colle pas
> le clip à la grille : il applique `delta = snapTick(curseur) -
> gesteDernier_`, et comme les deux termes sont des multiples du pas, le
> déplacement l'est aussi — un clip posé au tick 137 se retrouve au tick 617,
> son décalage intact. C'est exactement ce que Cubase appelle l'aimant
> relatif.
>
> **La leçon dépasse celle de CLAUDE.md, et il faut l'écrire.** « Un zéro
> sorti d'un grep se revérifie en listant ce qu'on a cherché ET où » suppose
> qu'on cherche la bonne chose. Ici le vocabulaire était le mauvais outil :
> un comportement peut être implémenté sans jamais être nommé, et aucune
> recherche de mots ne le trouvera. **Un manque supposé se vérifie en LISANT
> le code qui devrait le porter**, pas seulement en cherchant son nom. Sans
> cette relecture, un commit aurait promis une fonction déjà livrée.

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D33.2** — j'attends que le saut au bord d'un clip qui tranche une
   sinusoïde à pleine amplitude tombe **d'au moins un facteur dix**, et que
   le premier échantillon rendu soit **exactement zéro**. Et j'attends que
   le fondu posé à la MAIN par l'utilisateur soit inchangé au bit près : un
   fondu de sécurité qui s'ajouterait au sien le rendrait deux fois.
2. **D33.4** — j'attends **moins de -120 dBFS** entre le gel et le vif, et
   non zéro : le gel traverse son propre rendu isolé, le vif traverse le
   graphe complet, et deux chemins de mixage flottants ne donnent pas le
   dernier bit. Exiger zéro serait un critère qu'on contourne.
3. **D33.5** — j'attends que douze pistes importées d'un `.mid` portent
   **douze couleurs prises dans l'ordre de la palette**, et qu'une piste qui
   arrivait DÉJÀ colorée garde exactement sa couleur. Une palette qui
   écraserait ce que l'import a trouvé perdrait ce qu'il avait su lire.
4. **D33.1** — douze fichiers doivent donner **douze pistes**, et un
   treizième illisible doit laisser les douze autres entrer. Un import qui
   s'arrête au premier refus serait pire que pas d'import multiple du tout.

> **D33.1 EST FAITE (07/09/2026, 00:20).** `canSelectMultipleItems` et
> `getResults()` : une piste neuve par fichier, nommée d'après lui. **Ce qui
> échoue n'arrête pas le reste et est NOMMÉ** — un import qui s'arrêterait au
> premier fichier illisible serait pire que pas d'import multiple du tout :
> on aurait douze stems à poser, on s'arrêterait au troisième, et l'on ne
> saurait pas lesquels sont entrés. Le compte rendu dit combien sont entrés
> sur combien, et nomme les refusés.

> **D33.2 EST FAITE (07/09/2026, 00:05).** Un fondu de sécurité de 2 ms aux
> deux bords de chaque span audio, **là où l'utilisateur n'a rien posé** : le
> sien gagne toujours, sans quoi un fondu d'une seconde soigneusement dessiné
> se mettrait à commencer par une marche de deux millisecondes. Borné à la
> moitié du clip — sur un grain de trois millisecondes, deux fondus qui se
> chevaucheraient atténueraient le milieu.
>
> **L'attendu était « au moins un facteur dix ». C'est un facteur 96.**
>
> | fondu | plus grand saut au bord |
> |---|---|
> | 0 (le comportement d'avant) | **1,000000** |
> | 1 ms | 0,020789 |
> | **2 ms (le défaut)** | **0,010394** |
> | 5 ms | 0,004158 |
>
> Mesuré sur le pire cas possible : un clip qui commence là où la sinusoïde
> vaut exactement 1. Le premier échantillon rendu est **exactement zéro**, et
> un fondu posé à la main est inchangé **au bit près** — deux tests le
> tiennent. Le réglage se change en marche (`VSM_VUE=fondu-securite:5`), est
> conservé d'une exécution à l'autre, et **zéro rend exactement le chemin
> d'avant la phase**.

> **D33.3 EST FAITE (07/09/2026, 00:40).** Ctrl+glisser sur la règle fait
> entendre ce que la tête traverse, à la vitesse du geste (0,25× à 4×, les
> bornes que `setPlaybackSpeed` tient déjà depuis D18.5). Le transport est
> remis **dans l'état où on l'a trouvé** au relâchement : s'il jouait, il
> continue ; s'il était à l'arrêt, il s'arrête.
>
> **CTRL ET NON ALT, contrairement au tableau ci-dessus**, et c'est une
> correction qu'il valait mieux faire que taire : Alt dessine déjà la région
> de punch et Maj la boucle. Un troisième geste sur une touche prise aurait
> fait deux choses à la fois — exactement le genre de raccourci qu'on croit
> cassé.
>
> **LE SENS DU GESTE EST IGNORÉ, et c'est une décision écrite.** Le moteur ne
> sait pas lire à l'envers (`setPlaybackSpeed` borne à 0,25..4, tous
> positifs), et rendre un scrub arrière par des sauts en avant produirait un
> bruit qui n'apprend rien. Tirer vers la gauche déplace donc la tête sans
> son — ce qui est déjà ce qu'on veut : on cherche un point, on ne réécoute
> pas à l'envers.
>
> Vérifié par `VSM_VUE=scrub:960:2.5,scrub:1440:2.5,scrub:0:0`, qui emprunte
> le MÊME rappel que le geste : « lecture en marche, vitesse du moteur 2,50 »
> deux fois, puis « lecture arrêtée, vitesse du moteur 1,00 ». Le glissé de
> souris lui-même n'a pas été piloté — il ne se pilote pas sans souris —, et
> c'est dit plutôt que sous-entendu.

> **D33.4 EST FAITE (07/09/2026, 00:55).** Le gel accepte une piste AUDIO :
> `renderTrackForFreeze` isolait déjà une piste quelconque, la restriction ne
> vivait que dans un test de l'interface. « Reporter la piste en audio »
> s'ouvre du même coup et délibérément — c'est la même opération avec une
> autre permanence.
>
> **L'ATTENDU ÉTAIT FAUX, ET C'EST LA MESURE QUI L'A DIT.** J'avais écrit
> « moins de -120 dBFS entre le gel et le vif, et non zéro ». La première
> version du test comparait le gel au rendu du projet **tel quel** et trouvait
> un écart énorme. Ce n'était pas un défaut du gel : c'est la différence que
> le gel EST censé avoir. Il capture le signal d'AVANT le fader et le
> panoramique (D5.5), donc chaque canal inaltéré ; le rendu tel quel passe par
> la loi de panoramique, qui vaut 0,707 sur les deux canaux au centre. **Le
> critère avait été écrit contre la mauvaise référence.** Contre la bonne —
> celle de D5.5, la piste rendue à fond à gauche puis à fond à droite —
> l'écart mesuré est **exactement 0,0** : une piste audio ne traverse ni
> instrument ni insert, donc les deux côtés empruntent le même chemin. Le
> résultat est plus fort que ce que la feuille de route osait demander.
>
> **LE GEL D'UN GROUPE RESTE REFUSÉ, et voici pourquoi.** Geler un bus
> voudrait dire figer la SOMME de ses membres, donc décider ce qu'il advient
> d'eux : les taire ? les laisser muets au dégel ? Cubase ne gèle pas ses
> groupes non plus. Rien ne l'a demandé, et l'inventer coûterait un état de
> plus dans le modèle pour un besoin que personne n'a exprimé.

> **D33.5 EST FAITE (07/09/2026, 01:05), ET LA PHASE D33 EST CLOSE.**
> `trackColourForIndex` dans `core/`, et **quatre palettes deviennent une** :
> celle d'`addTrack`, celle de `DawImport`, le défaut bleu de `Track` et
> celle de la chaîne Python. Quatre palettes voulaient dire que la même piste
> changeait de couleur selon la porte par laquelle elle était entrée.
>
> **Et le trou était là où il se voyait le moins :** « Ouvrir MIDI... », le
> bouton de la barre de transport, passe par `Project::fromParsedFile`, qui
> ne posait aucune couleur. Douze pistes ouvertes d'un `.mid` arrivaient
> toutes du même bleu. Vérifié à l'écran (`VSM_VUE=ouvrir-midi:douze.mid`,
> commande ajoutée pour cela — un écran qu'on n'atteint qu'à la souris est un
> écran qu'on ne peut pas déclarer vérifié) : **douze pistes, douze
> couleurs**, dans l'arrangement, la liste des pistes ET les tranches du
> mélangeur, la palette reprenant au début à la onzième — dix teintes
> suffisent, au-delà des voisines se confondraient plus qu'elles ne
> distingueraient. Une piste qui arrivait DÉJÀ colorée garde exactement sa
> couleur, et un test le tient.
>
> Tests : 294 core, 1 274 audio, 283 interchange, 25 clap, 11 panels, 19 vst3
> — tous verts (10 tests neufs) ; Python inchangé (168).

---

### Phase D34 — Le dix-neuvième audit : ce qui manque une fois D33 posée (07/09/2026, 01:20)

**Pourquoi.** Même méthode, et la leçon de D33.5 appliquée d'emblée : **un
manque supposé se vérifie en LISANT le code qui devrait le porter**, pas en
cherchant son nom. Elle a servi deux fois dans l'heure.

**Écartés comme EXISTANTS, en nommant ce qu'on a cherché ET où** : le
défilement automatique pendant la lecture (`autoScroll` ne rend rien, mais
`ArrangementComponent::setPlayheadTick` tourne la page depuis D11.3 — le mot
était le mauvais outil, encore) ; le tempo frappé (`tapButton_`,
`TransportBarComponent`) ; le décalage de piste (`Track::delayMs`) ; l'écoute
en mono (D23.5) ; le rassemblement du projet et de ses fichiers
(`interchange/ProjectBundle.cpp`) ; les préréglages de piste
(`interchange/TrackPreset.h`) ; la duplication de piste
(`Project::duplicateTrack`) ; la recherche dans le navigateur
(`BrowserComponent`) ; l'entrée pas-à-pas (`PianoRollComponent::stepInput`) ;
la sauvegarde automatique (`AutosaveService`) ; la poursuite des contrôleurs à
la localisation (`PlaybackScheduler`, chase) ; les départs auxiliaires
(`Track::sendLevel`) ; les dossiers de pistes (`Track::isFolder`) ; les
groupes d'édition (D18.3) ; les mesures de niveau LUFS et de corrélation
(`MixerComponent`) ; la chaîne latérale (`ProcessGraph`) ; le décompte ; les
repères ; le gel ; l'étirement temporel ; l'inversion ; les formes de fondu.

Cinq manques ont survécu.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D34.1 | **Le crossfade aux recouvrements audio.** `spansFromTrack` fabrique chaque portée sans jamais regarder sa voisine, et `mixInto` les ADDITIONNE : deux prises du même passage qui se chevauchent de 200 ms jouent, dans le recouvrement, la somme des deux. Cubase et Live écrivent tous deux un fondu croisé dans la zone commune | les portées qui se recouvrent reçoivent un fondu croisé **à puissance constante** (`FadeShape::EqualPower`, déjà dans le modèle depuis D17.1) sur la durée exacte du recouvrement, **et seulement là où l'utilisateur n'a rien posé** — le sien gagne, comme pour le fondu de sécurité ; la forme est réglable ; mesuré sur deux matériaux décorrélés ET sur deux matériaux identiques |
| D34.2 | **Les copies liées : les DIRE, et pouvoir les rompre.** Un clip MIDI est une fenêtre sur le matériau de sa piste (`Track.h`), et `duplicateClips` recopie la fenêtre en gardant `sourceStart` : la copie est donc une **copie liée** — éditer une note dans l'une change l'autre. C'est une vraie fonction, celle que Cubase appelle « copie partagée » ; ici elle est **invisible et irréversible**. Rien ne dessine le lien, et rien ne permet d'en sortir | deux clips d'une piste qui partagent leur fenêtre source se dessinent comme liés (une marque dans le clip, et son nom en italique — la convention de Cubase) ; « Convertir en copie indépendante » recopie les notes de la fenêtre dans une région LIBRE du matériau et y repointe la fenêtre ; testé des DEUX côtés — qu'avant conversion l'édition se propage, qu'après elle ne se propage plus |
| D34.3 | **Un fichier audio lâché sur la fenêtre peut être POSÉ, pas seulement reconstruit.** `filesDropped` jette `x` et `y`, ne retient que le PREMIER fichier audio, et n'offre qu'un seul choix : une reconstruction de plusieurs minutes. Son propre commentaire nomme les trois choses qu'on peut vouloir — « l'écouter, le poser sur une piste, ou le reconstruire » — et part d'office sur la plus longue | la question posée offre **« Poser sur une piste »** à côté de « Reconstruire » ; poser crée une piste PAR fichier lâché, en empruntant le chemin de D33.1 plutôt qu'en le réécrivant ; ce qui échoue est nommé et n'arrête pas le reste ; le compte rendu dit combien sont entrés |
| D34.4 | **La règle en TEMPS.** `ArrangementComponent::paint` ne trace qu'une graduation par mesure, numérotée en mesures, toujours. Or ce logiciel compare une reconstruction à un enregistrement qui, lui, se mesure en secondes — et la barre de transport affiche DÉJÀ les deux depuis D11.3. La règle en montre une | *Affichage ▸ Règle ▸ Mesures / Minutes:secondes*, conservé d'une exécution à l'autre ; le pas des graduations est choisi pour qu'elles ne se rapprochent jamais à moins de 60 px, quel que soit le zoom ; vérifié à l'écran |
| D34.5 | **Dessiner une automation par une FORME.** `AutomationEdit.h` n'offre que `setAutomationPoint` (un point) et `writeAutomationRange` (une valeur constante sur une plage) ; la bande d'automation de l'arrangement s'édite point par point. Un balayage de filtre sur seize mesures se pose donc à la main, point après point. L'outil « ligne » de Cubase trace une droite, une parabole, un sinus, un triangle, un carré | `drawAutomationShape(courbe, deTick, àTick, forme, valeurDébut, valeurFin, périodes)` dans `core/`, formes **Ligne, Sinus, Triangle, Carré** ; appliquée sur la plage choisie de la bande d'automation ; mesuré : le sinus rendu par `automationValueAt` s'écarte du sinus idéal de moins de **1 %** de l'étendue, et le nombre de points posés reste **borné** — une forme qui poserait un point par tick serait illisible et impossible à retoucher |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D34.1** — sur deux matériaux **décorrélés** (deux bruits indépendants),
   j'attends que le niveau efficace dans le recouvrement passe de **+3,0 dB**
   (la somme de deux puissances) à **moins de 0,5 dB** de ce que chacun joue
   seul : c'est exactement ce que « puissance constante » veut dire, et c'est
   le cas ordinaire d'un fondu croisé, qui joint deux prises DIFFÉRENTES.
   Sur deux matériaux **identiques**, j'attends que la crête passe de
   **+6,0 dB** à **+3,0 dB** avec la puissance constante, et à **0 dB** avec
   la forme linéaire. **La puissance constante n'est donc pas parfaite
   partout, et c'est pourquoi la forme reste réglable** : un fondu croisé
   entre deux copies du même son est le cas où le linéaire gagne, et il est
   rare. Choisir un défaut ne doit pas revenir à cacher l'autre cas.
2. **D34.2** — j'attends qu'un test écrit AVANT la fonction confirme que
   l'édition se propage **aujourd'hui** entre deux copies. Si elle ne se
   propageait pas, l'analyse de `duplicateClips` serait fausse et l'étape
   entière serait à réécrire — c'est le genre de vérification qu'on doit
   faire avant d'annoncer une fonction, pas après.
3. **D34.5** — j'attends **moins de 1 %** d'écart sur le sinus et **au plus
   deux points par période** au-delà de ce qu'il faut pour tenir ce 1 % : le
   critère de tolérance et celui de parcimonie tirent en sens contraires, et
   ne mesurer que le premier laisserait passer une forme qui triche en
   posant mille points.

> **D34.1 EST FAITE (07/09/2026, 02:10), ET L'AUDIT AVAIT MAL POSÉ LE MANQUE.**
> Le tableau ci-dessus dit « `spansFromTrack` fabrique chaque portée sans
> jamais regarder sa voisine ». **C'est faux, et c'est écrit plutôt
> qu'effacé.** Le fondu croisé existe depuis **D13.1** : les portées sont
> triées et les recouvrements traités, à la fin de la même fonction — vingt
> lignes plus bas que là où j'avais arrêté ma lecture. `grep -i crossfade` ne
> rendait rien parce que le code dit « chevauchent » et « se fondent l'une
> dans l'autre ».
>
> **C'est la deuxième phase de suite où le vocabulaire est le mauvais outil**
> (D33.5 : l'aimant relatif ; ici : le fondu croisé), et cette fois la leçon
> était DÉJÀ écrite au-dessus de ma table. La relire ne suffit donc pas : ce
> qui manquait est la discipline de lire la fonction **jusqu'au bout**. Un
> `sed -n '266,346p'` n'est pas une lecture, c'est un autre grep.
>
> **LE VRAI MANQUE EST PLUS FIN, ET D17.1 L'AVAIT LUI-MÊME MESURÉ SANS EN
> TIRER LA CONSÉQUENCE.** D17.1 a mesuré que deux droites qui se croisent
> creusent 3 dB sur du matériau décorrélé, a donné une forme au clip — et a
> laissé `Linear` par défaut pour ne rien changer aux projets existants. Le
> fondu croisé **automatique**, celui que personne ne demande et que tout le
> monde reçoit, est donc resté sur la courbe que la même étape avait démontrée
> fausse. Et les tests de D17.1 posaient leurs fondus **à la main, sur deux
> pistes séparées** : ils ne traversaient pas une seule fois le chemin
> automatique.
>
> **Ce qui a été fait.** Le fondu croisé a désormais son propre champ
> (`crossfadeInFrames`, `crossfadeOutFrames`, `crossfadeShape`), distinct des
> fondus dessinés. Il peut donc porter SA forme sans toucher à la leur : un
> fondu d'entrée reste `Linear` par défaut, au bit près, et la jonction entre
> deux prises passe à la puissance constante. La préséance, sur chaque bord,
> va **du plus intentionnel au plus machinal** — le fondu de l'utilisateur
> (quand il est le plus long, règle de D13.1 conservée), puis le fondu croisé,
> puis le fondu de sécurité de D33.2.
>
> **L'attendu était juste des deux côtés, y compris là où il annonçait un
> revers.**
>
> | matériau | linéaire (avant) | puissance constante (après) |
> |---|---|---|
> | **décorrélé** (deux prises) | **−3,07 dB** | **−0,07 dB** |
> | **identique** (deux copies) | +0,00 dB | **+3,01 dB** |
>
> Le nouveau défaut est meilleur là où l'on croise vraiment deux prises, et
> **moins bon** là où l'on croise un son avec lui-même. Ce second chiffre est
> mesuré et publié plutôt que tu : c'est pourquoi **la forme est une donnée du
> PROJET** (`Project::crossfadeShape`, écrite dans `project.json` seulement si
> elle diffère du défaut) et non une préférence — elle change ce que le morceau
> SONNE, et un projet doit s'exporter comme il se joue. C'est mot pour mot la
> raison qui avait fait entrer les réglages du master dans le fichier.
>
> **UNE PANNE MUETTE TROUVÉE EN CHEMIN, ET RÉPARÉE.** Le **fondu de sécurité
> de D33.2 manquait au rendu hors ligne** : il avait été posé dans
> `loadAudioTracks`, du côté de l'application seule. Un projet EXPORTÉ claquait
> donc aux bords de ses clips là où sa LECTURE ne claquait pas. C'est le
> troisième exemplaire de la même erreur que `OfflineReconstruction.cpp`
> collectionne — le calage des portées étirées (D12.5), les inserts, ceci — et
> la parade a été prise cette fois-ci : **le fondu croisé est posé DANS
> `spansFromTrack`**, la fonction que les deux chemins appellent, plutôt que
> chez chacun d'eux. Ce qui doit valoir pour deux appelants se met dans ce
> qu'ils appellent.
>
> **Vérifié à l'écran**, sur un projet à deux clips audio qui se recouvrent
> d'une seconde : `VSM_VUE=fondu-croise:lineaire` puis `VSM_MENU="Puissance
> constante"` rendent « appliquée à **2 jonction(s)** de clips audio » et
> « *Puissance constante (deux prises différentes)* exécutée (menu Mixage) ».
> Les deux chemins empruntent la MÊME méthode (`setCrossfadeShape`) : ce qu'on
> photographie est ce que le geste fait.
>
> **ET `VSM_MENU` DIT ENFIN CE QU'IL A EXÉCUTÉ.** Il se taisait en cas de
> succès, si bien qu'« aucune erreur » ne distinguait pas « l'entrée a été
> jouée » de « la commande n'a pas tourné du tout ». Il écrit désormais le
> libellé RETENU en entier et le menu d'où il vient — précisément ce qui aurait
> évité la panne du 06/09, où « Automatique » a piloté les threads de rendu au
> lieu du mode d'écoute.
>
> Tests : **1 279 audio** (5 neufs), 294 core, **285 interchange** (2 neufs) —
> tous verts.

> **D34.2 EST FAITE (07/09/2026, 03:05), ET LE PREMIER TRAVAIL A ÉTÉ DE
> VÉRIFIER LA PRÉMISSE.** L'attendu écrit avant la fonction demandait « un test
> qui confirme que l'édition se propage AUJOURD'HUI entre deux copies ». Elle
> se propage — et le test existait déjà, depuis **D1.2** :
> `two_clips_on_the_same_material_share_it_by_construction`. La copie liée n'est
> donc pas un accident : c'est une décision de D1, écrite dans `Track.h`
> (« un clip est une RÉGION du matériau, il ne l'emporte pas ») et tenue par
> `passagesOf` (« aucune note n'est copiée : c'est le décalage qui est répété,
> pas le matériau »).
>
> **CE QUI MANQUAIT N'ÉTAIT PAS LA FONCTION : C'ÉTAIT DE POUVOIR EN SORTIR, ET
> DE LE SAVOIR.** Une fonction utile qu'on ne peut ni voir ni défaire devient
> un piège : on duplique un motif pour en faire une variante, on l'édite, et
> l'original change aussi. C'est ce que ce dépôt appelle ailleurs une panne
> muette, appliqué cette fois à un comportement **voulu**.
>
> **Ce qui a été fait.** `clipIsShared` et `makeClipIndependent` dans `core/` ;
> deux maillons de chaîne en haut à droite du clip et son nom **en italique**
> quand il est lié — la convention de Cubase, qui appelle cela une copie
> partagée ; et « Convertir en copie indépendante » au menu contextuel, **grisée
> plutôt qu'absente** quand le clip n'est lié à rien : une entrée qui apparaît
> et disparaît ne s'apprend jamais, tandis qu'une entrée grisée enseigne que la
> notion existe.
>
> **Trois décisions, écrites parce qu'elles se discutent.**
>
> 1. **« La même FENÊTRE » et non « le même début ».** Deux clips qui partent du
>    même tick source mais dont l'un a été rogné ne lisent pas les mêmes notes :
>    les dire liés promettrait une propagation qui n'aura pas lieu sur la partie
>    qu'ils ne partagent pas.
> 2. **Délier l'un ne délie pas les autres.** Sur trois copies, en délier une en
>    laisse deux liées : on demandait UNE variante, pas la dissolution du groupe.
> 3. **Aux pistes MIDI seulement.** Deux clips audio de la même fenêtre lisent
>    le même fichier, mais rien ne s'y « édite » — le montage ne change pas les
>    échantillons. Un marqueur de lien n'y avertirait de rien.
>
> Les notes recopiées vont **à la suite du matériau**, avec une mesure de marge,
> et non « quelque part » : le matériau d'une piste est une ligne de temps, et y
> insérer au milieu décalerait ce que TOUS les autres clips lisent. Elles sont
> **tronquées à la fenêtre**, comme la lecture les tronque déjà, et elles
> emportent **tous** leurs champs — vélocité de relâchement, silence, confiance
> — qu'`addNote` seul aurait laissés derrière, et dont la perte ne se serait
> entendue qu'après coup.
>
> **Vérifié à l'écran** : `VSM_VUE=copies-liees,choisir-clip:0,delier-clip,
> copies-liees` sur trois clips d'un même motif rend « **3 clip(s) MIDI
> partagent leur fenêtre** », puis « **3 avant, 2 après** », puis « **2** ». La
> capture montre le premier clip sans marqueur et le second avec — la commande
> emprunte la MÊME fonction que le menu contextuel
> (`runClipMenuActionForCapture`). Le nombre est ce qui prouve l'effet : un
> marqueur de six pixels ne se juge pas sur une capture d'écran.
>
> Tests : 1 279 audio, **301 core** (7 neufs), 285 interchange, 25 clap,
> 11 panels — tous verts.

> **D34.3 EST FAITE (07/09/2026, 04:20), ET ELLE A DÉTERRÉ DEUX DÉFAUTS QUI NE
> LUI APPARTENAIENT PAS.** Un fichier audio lâché sur la fenêtre peut
> désormais être **posé** : la boîte offre « Poser sur une piste » et
> « Reconstruire », `Poser` en premier parce que c'est le geste courant. Tous
> les fichiers du lâcher sont pris, et non le premier — une reconstruction rend
> des dizaines de stems. La reconstruction, elle, n'est proposée que pour **un
> seul** fichier : la chaîne analyse un morceau, pas un lot, et le dire dans la
> boîte vaut mieux qu'un refus après coup. Quand elle est hors service, la
> raison s'écrit dans la MÊME boîte au lieu de remplacer le choix par une
> alerte.
>
> **PREMIER DÉFAUT : UNE LECTURE APRÈS LIBÉRATION QUI FAISAIT TOMBER
> L'APPLICATION.** `updateSynthRackForSelection` donnait au rack un POINTEUR
> BRUT dans `project_.tracks` (`setTrack(&project_.tracks[idx])`) ; le moindre
> `push_back` sur ce vecteur le réalloue. Ajouter une piste pendant qu'une
> machine à grille de pas était choisie laissait donc le séquenceur relire les
> notes d'une piste **détruite** : `patternFromNotes` recevait un `std::vector`
> annoncé de capacité **2 345 625 308 412**, et le processus mourait sur une
> faute de segmentation.
>
> **Le chemin fautif n'a rien de neuf.** Il est dans `addTrack`, que l'import
> audio du menu — **D33.1, la phase précédente** — emprunte à chaque fichier.
> Le défaut ne se déclenche que si le vecteur réalloue à ce moment-là, ce qui
> dépend de sa capacité : cinq « Ajouter une piste MIDI » de suite ne l'ont pas
> réveillé, deux fichiers posés d'un coup, si. **C'est ce qui rend ce genre de
> panne intermittente, et c'est pourquoi la seule façon de la trouver était de
> LANCER l'application** — les 1 901 tests ne traversent pas une seule ligne
> d'interface. C'est exactement la leçon payée sur le point d'entrée de D7.5,
> et elle vient de resservir.
>
> Le pointeur est désormais lâché **en tête de `rebuildFromProject`**, et non à
> chacune des quinze mutations possibles : toute modification de la liste des
> pistes finit par appeler cette fonction, et un seul endroit vaut mieux que
> quinze dont le seizième oubliera. Vérifié en A/B, une seule variable : sans
> le correctif, faute de segmentation ; avec, « 1 piste audio avant, 3 après ».
>
> **SECOND DÉFAUT : DEUX VUES QUI NE DISAIENT PAS LA MÊME CHOSE.**
> `importAudioFileOnNewTrack` renommait la piste APRÈS `addTrack` et ne
> rafraîchissait que la liste des pistes : le mélangeur gardait « Audio 5 » sur
> une piste que la liste appelait « prise3 ». Là encore, le geste concerné est
> celui que D33.1 venait de rendre possible — importer douze stems laissait
> douze tranches mal nommées, et seule la dernière restait fausse assez
> longtemps pour se voir. `addTrack` prend maintenant le nom **à la
> naissance** : la piste est correcte avant que la moindre vue soit refaite, et
> il n'y a plus de second rafraîchissement à oublier.
>
> **Vérifié à l'écran** : `VSM_VUE=deposer-audio:prise2.wav;prise3.wav` rend
> « Import audio : 2 piste(s) créée(s) sur 2 fichier(s) » et « Pistes audio :
> 1 avant, 3 après le dépôt posé » ; la capture montre les deux pistes neuves,
> nommées d'après leurs fichiers, dans l'arrangement **et** dans le mélangeur.
>
> **CE QUI N'A PAS ÉTÉ PILOTÉ, ET C'EST DIT.** Le CLIC sur « Poser » ne l'a pas
> été : JUCE ne pose ici aucune `AlertWindow` qu'on puisse retrouver dans
> l'arbre des composants pour la presser, et l'écran de la machine peut être
> verrouillé quand la vérification tourne. La commande appelle donc
> **exactement** la méthode que le bouton appelle (`placeDroppedAudioOnTracks`,
> nommée pour cela) : ce qui est vérifié est tout le chemin sauf le clic — la
> même honnêteté que le scrub de D33.3, dont le glissé de souris n'avait pas
> été piloté non plus.
>
> **LE DÉPÔT NE TIENT TOUJOURS PAS COMPTE DE L'ENDROIT OÙ L'ON LÂCHE, et c'est
> une décision.** `filesDropped` reçoit `x` et `y` et les ignore. Le fichier
> arrive sur une piste NEUVE, comme par le menu, et non sur la piste survolée à
> l'instant du lâcher. La raison est celle que D14.3 avait déjà écrite pour le
> MIDI : « le geste le moins ambigu des deux, et le seul qui ne perd rien ».
> Poser sur la piste survolée écraserait ou décalerait ce qui s'y trouve, et
> douze stems lâchés ensemble n'ont de toute façon pas douze pistes sous le
> curseur. À rouvrir le jour où l'on lâchera un fichier unique en visant.
>
> Tests : 1 279 audio, 301 core, 285 interchange, 25 clap, 11 panels — tous
> verts.

> **D34.4 EST FAITE (07/09/2026, 05:10).** *Affichage ▸ Règle ▸ Mesures /
> Minutes:secondes*, conservé d'une exécution à l'autre. C'est une préférence
> d'ATELIER et non une donnée du morceau — elle ne change ni ce que le projet
> sonne ni ce qu'il contient —, donc elle va dans les propriétés de
> l'application et non dans `project.json`. La distinction avec le fondu croisé
> de D34.1, qui est allé dans le projet, tient à cette seule question : est-ce
> que le fichier exporté en dépend ?
>
> **LE PAS DES GRADUATIONS EST LE CŒUR DE L'ÉTAPE, et il se mesure.** Un pas
> fixe donne, au zoom large, une bouillie de traits, et au zoom serré une règle
> vide. Le pas est donc choisi dans une échelle de paliers qu'on lit sans
> calculer — un dixième, un quart, une demi-seconde, une seconde, deux, cinq,
> quinze, une minute, cinq, une demi-heure — de façon que deux graduations ne se
> rapprochent jamais à moins de **60 pixels**. Mesuré à sept zooms, en lisant
> l'écart minimal RÉEL entre deux traits :
>
> | pas retenu | graduations affichées | écart mini |
> |---|---|---|
> | 1 s | 0 · 1 · 2 · 3 · 4 | **62,4 px** |
> | 15 s | 0 · 15 · 30 · 45 · 1:00 | **62,4 px** |
> | 2 s | 0 · 2 · 4 · 6 · 8 | **66,6 px** |
> | 0,25 s | 0,00 · 0,25 · 0,50 · 0,75 · 1,00 | **66,6 px** |
> | 0,1 s | 0,0 · 0,1 | **208,0 px** |
>
> **DEUX DÉFAUTS TROUVÉS PAR CETTE MESURE MÊME, et c'est pourquoi elle valait
> mieux qu'un coup d'œil.**
>
> 1. **La règle mentait sur ce qu'elle graduait.** Une première version disait
>    « une décimale en dessous d'une demi-seconde ». Au pas d'un quart de
>    seconde, elle affichait « 0,0 · 0,2 · 0,5 · 0,8 · 1,0 » : **deux libellés
>    faux sur cinq**, pour des positions qui, elles, étaient justes. Le nombre
>    de décimales se CALCULE désormais — celui qu'il faut pour écrire le pas
>    exactement — au lieu de se deviner par un seuil. Une règle qui ment sur ce
>    qu'elle gradue est pire qu'une règle absente.
> 2. **Le séparateur décimal contredisait la barre de transport**, qui écrit
>    « 00:00,000 » depuis D11.3. Deux affichages du même temps dans la même
>    fenêtre, l'un à la virgule et l'autre au point.
>
> **Le dessin et la vérification lisent la MÊME liste de graduations**
> (`rulerTimeTicks`) : une vérification qui recalculerait les nombres de son
> côté prouverait que deux calculs sont d'accord, pas que la règle affiche ce
> qu'elle prétend. Vérifié par `VSM_VUE=regle:temps,zoom-arrangement:…` — une
> commande de zoom ajoutée pour cela, **parce qu'une règle correcte à un seul
> zoom ne prouve rien de la règle qui choisit le pas**.
>
> Tests : 1 279 audio, 301 core, 285 interchange — inchangés, l'étape est
> entièrement dans l'interface.

> **D34.5 EST FAITE (07/09/2026, 06:15), ET LA PHASE D34 EST CLOSE.**
> `drawAutomationShape` dans `core/`, quatre formes — **Ligne, Sinus, Triangle,
> Carré** —, au menu *Édition ▸ Dessiner l'automation sur la sélection*.
>
> **L'ATTENDU ÉTAIT DOUBLE, ET C'EST CE QUI L'A RENDU UTILE.** « Moins de 1 %
> d'écart au sinus idéal ET un nombre de points borné » : les deux moitiés
> tirent en sens contraires, et ne mesurer que la première laisserait passer
> une forme qui triche en posant mille points. Le compromis est donc publié
> plutôt que résumé à un seul chiffre :
>
> | tolérance demandée | écart mesuré au sinus | points posés (4 périodes) |
> |---|---|---|
> | 5 % | 4,45 % | **25** |
> | **1 % (le défaut)** | **0,94 %** | **62** |
> | 0,2 % | 0,19 % | **165** |
>
> Un sinus de quatre mesures tient donc en **62 points**, soit une quinzaine
> par période, là où l'échantillonnage en avait posé mille. La réduction est
> celle de **D30.5**, écrite pour les passes jouées : lui confier la parcimonie
> valait mieux que de la deviner forme par forme.
>
> **LA MESURE A CORRIGÉ LA FONCTION, ET DE JUSTESSE.** La première version
> rendait **1,01 %** pour 1 % promis. Un dépassement de rien du tout, qui
> restait un dépassement — et sa cause est instructive : la tolérance promise
> porte sur l'écart à la forme IDÉALE, or **deux** erreurs s'y ajoutent, celle
> de la réduction et celle de la polyligne échantillonnée elle-même, dont les
> cordes coupent les sommets. Dépenser tout le budget à la réduction seule,
> c'est promettre ce qu'on ne tient qu'au bord. L'échantillonnage est passé de
> 64 à 256 points par période et la réduction ne dépense plus que 95 % du
> budget.
>
> **Trois décisions, écrites parce qu'elles se discutent.**
>
> 1. **Un carré ne s'échantillonne pas.** Approché par des rampes très raides,
>    il devient une suite de fondus courts — et cela s'entend. Ses paliers se
>    posent en deux points par période, marqués `step`.
> 2. **Une oscillation par MESURE**, plutôt qu'un nombre demandé dans une
>    boîte. Un trémolo, un balayage, un panoramique qui va et vient se pensent
>    en mesures ; poser d'abord la question « combien de périodes ? » ferait
>    répondre « quatre » à quelqu'un qui voulait dire « une par mesure ». On
>    resserre ensuite à la main, sur une forme qu'on VOIT.
> 3. **La plage est la sélection de clips, et la boucle à défaut.** Ni l'une ni
>    l'autre, et rien n'est tracé : inventer une plage — tout le morceau ?
>    quatre mesures ? — serait un geste dont on ne pourrait pas prévoir
>    l'étendue. Les valeurs sont les BORNES du paramètre, la forme couvrant
>    toute sa course ; la resserrer se fait après, sur ce qu'on voit.
>
> Les bords sont raccordés comme dans `writeAutomationRange` et pour la même
> raison : tracer quatre mesures au milieu d'un fondu ferait autrement sauter
> le paramètre à l'entrée et à la sortie — on aurait dessiné quatre mesures en
> cassant les deux voisines.
>
> **Vérifié à l'écran** : `VSM_VUE=courbes,auto-forme:sinus` sur une boucle de
> quatre mesures rend « Automation tracée : **62 point(s) posé(s), 2
> remplacé(s), 4 période(s) sur 7 680 ticks** » — le même 62 que le banc —, et
> la capture montre les quatre oscillations dans la bande d'automation de la
> piste. Le tracé DIT ses nombres parce qu'une courbe de six pixels de haut ne
> se juge pas sur une capture, et que le critère de l'étape est justement un
> nombre de points.
>
> Tests : 1 279 audio, **308 core** (7 neufs), 285 interchange, 25 clap,
> 11 panels — tous verts.

---

**BILAN DE LA PHASE D34.** Cinq étapes, et **deux des cinq manques n'en étaient
pas** — le fondu croisé existait depuis D13.1, les copies liées depuis D1.2.
Après D33.5, cela fait **trois audits de suite** où le vocabulaire a été le
mauvais outil. La leçon en est donc affinée une dernière fois : **lire la
fonction jusqu'au bout**. Un `sed -n '266,346p'` n'est pas une lecture, c'est un
autre grep, et c'est précisément ce qui avait fait écrire « `spansFromTrack` ne
regarde jamais sa voisine » vingt lignes au-dessus du code qui la regarde.

Mais les deux « faux manques » ont donné les deux meilleures étapes de la
phase, et ce n'est pas un hasard : une fonction qui existe sans être dite est
plus dangereuse qu'une fonction absente. Le fondu croisé avait la courbe que
D17.1 avait elle-même démontrée fausse ; les copies liées piégeaient qui
dupliquait un motif pour en faire une variante. **Un audit qui ne cherche que
des absences ne trouve pas ces défauts-là.**

**Et ce qui a coûté le plus cher n'était dans aucune des cinq cases** : une
lecture après libération qui faisait tomber l'application, dans le chemin
d'import de D33.1, trouvée en LANÇANT le binaire. Les 1 908 tests ne traversent
pas une ligne d'interface, et c'est la seconde fois que ce dépôt paie ce prix
(la première fut le point d'entrée de D7.5). **Vérifier une interface, c'est
l'ouvrir.**

---

### Phase D35 — Le vingtième audit : deux fonctions de `core/` que rien n'appelle (07/09/2026, 06:40)

**Pourquoi, et comment cet audit a été mené autrement.** Les trois audits
précédents ont cherché des ABSENCES, et deux fois sur trois se sont trompés :
la fonction était là, sous un autre nom. D34 en a tiré la leçon inverse — **une
fonction qui existe sans être dite est plus dangereuse qu'une fonction
absente** — et cet audit l'a prise pour méthode. Au lieu de chercher ce qui
manque, il a cherché **ce qui existe et que personne n'appelle**, puis ce que
les gestes voisins font de ce qui existe.

Deux fonctions de `core/`, écrites avec soin et couvertes de tests, n'ont
**aucun appelant venant d'un geste de l'utilisateur** :

- **`moveTrack`** répare méticuleusement les index de routage (`outputGroup`,
  `outputSourceTrack`) au déplacement d'une piste — et n'est appelée qu'une
  fois, à l'intérieur de la transcription, pour ranger une piste neuve.
  **L'utilisateur ne peut pas réordonner ses pistes**, ni au menu, ni à la
  souris, ni au clavier.
- **`folderContents`** rend les pistes que contient un dossier. Elle n'est
  appelée **nulle part**, et cela explique tout le reste : les dossiers sont
  des étiquettes de profondeur que quatre gestes sur cinq ignorent.

**Le relevé a été mesuré, pas supposé.** Un petit programme a construit un
arbre — `Batterie/(0) Kick(1) Snare(1) Basse(0) Voix(0)` — et lui a appliqué
les gestes existants :

| geste | résultat mesuré |
|---|---|
| `moveTrack(dossier 0 → 3)` | `Kick(1) Snare(1) Basse(0) Batterie/(0) Voix(0)` puis, normalisé, `Kick(0) Snare(0) …` — **le dossier part seul et se vide** |
| `moveTrack(Voix 4 → 1)` | `Batterie/(0) Voix(0) Kick(1) Snare(1) …` — une piste posée entre le dossier et ses membres **les orpheline** |
| `removeTrack(dossier 0)` | `Kick(1) Snare(1) …` puis, normalisé, tout à plat — **supprimer l'étiquette dissout le tiroir en silence** |
| `duplicateTrack(dossier 0)` | `Batterie/ Batterie (copie)/ Kick(1) Snare(1) …` — **la copie VOLE le contenu de l'original** |

Le principe était pourtant connu et écrit : `changeSelectedTrackFolderDepth`
porte le commentaire « EN SORTANT, ON EMMÈNE CE QU'ON CONTENAIT ». Il a été
appliqué **une fois sur cinq**.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D35.1 | **Réordonner les pistes.** `moveTrack` existe, est testée, et aucun geste ne l'appelle : l'ordre des pistes est celui du fichier MIDI, définitivement. C'est le geste le plus banal d'un DAW | *Piste ▸ Monter / Descendre* (et les raccourcis), plus le glisser dans la liste des pistes ; les routages survivent — c'est ce que `moveTrack` sait déjà faire, et c'est pourquoi le geste passe par elle et ne réécrit rien |
| D35.2 | **Un dossier qui bouge emporte ce qu'il contient**, et une piste posée entre un dossier et ses membres est **adoptée** plutôt que de les orpheliner | `moveTrackWithFolder` dans `core/` ; mesuré sur l'arbre du relevé : après chaque déplacement, `folderContents` rend exactement les mêmes pistes qu'avant, et `normalizeFolderDepths` n'a **rien** à corriger — un arbre qui a besoin d'être normalisé après un geste est un arbre que le geste a cassé |
| D35.3 | **Supprimer et dupliquer un dossier.** La suppression dissout le tiroir en silence ; la duplication fait pire, elle **transfère** le contenu à la copie | supprimer un dossier **emporte son contenu**, et le DIT (« 3 pistes supprimées avec le dossier ») ; dupliquer un dossier copie son contenu ; testé des deux côtés |
| D35.4 | **Le muet et le solo d'un dossier agissent sur ce qu'il contient.** `trackAudible` ne regarde que la piste elle-même : rendre muet un dossier de douze micros de batterie n'en tait aucun. C'est la raison d'être des dossiers au-delà du rangement | `trackAudible` prend l'arbre ; un dossier muet tait son contenu, un dossier en solo le fait entendre seul ; **une piste `soloSafe` reste audible sous un dossier muet**, comme elle l'est déjà sous un solo (D30.1) ; mesuré sur le rendu, pas sur un booléen |
| D35.5 | **Une tranche de mélangeur pour un dossier, dont le fader ne fait rien.** `MixerComponent` fabrique une tranche par piste, dossiers compris ; or un dossier n'est pas un bus (`Kind::Group` l'est), aucun signal n'y passe, et tirer son fader ne change rien | ou bien le dossier n'a pas de tranche, ou bien sa tranche ne montre que ce qui agit ; **la décision est écrite avec sa raison**, et vérifiée à l'écran |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D35.2** — j'attends que, sur l'arbre du relevé, `normalizeFolderDepths`
   rende **zéro correction** après chacun des quatre gestes. C'est le critère
   qui vaut, et il est plus fort que « le contenu a suivi » : la normalisation
   est le filet qui rattrapait les dégâts, et un geste correct ne doit rien lui
   laisser à rattraper. Aujourd'hui elle en corrige deux à quatre.
2. **D35.4** — j'attends que le rendu d'un projet dont le dossier est muet soit
   **identique au bit près** au rendu du même projet où l'on aurait rendu muet
   chaque piste du dossier à la main. Comparer deux booléens ne prouverait que
   l'accord de deux `if` ; comparer deux rendus prouve que le son se tait.
3. **D35.1** — j'attends qu'après avoir monté puis redescendu une piste, le
   projet soit **exactement** celui de départ, routages compris. Un aller-retour
   qui ne revient pas au point de départ est le symptôme d'un index réparé de
   travers, et c'est précisément le défaut que `moveTrack` a été écrite pour
   éviter.

> **LA PHASE D35 EST FAITE (07/09/2026, 08:05).** Les cinq étapes ensemble,
> parce qu'elles n'en font qu'une : les dossiers étaient une profondeur que
> quatre gestes sur cinq ignoraient.
>
> **D35.1 — Réordonner les pistes.** *Piste ▸ Monter / Descendre*, par
> `moveTrackWithFolder`. Une piste qui monte **entre dans le dossier qu'elle
> traverse**, et en ressort en franchissant sa frontière par le bas : le
> rangement se fait au clavier, sans jamais viser à la souris.
>
> **D35.2 — Un dossier emporte ce qu'il contient.** Le critère écrit avant la
> mesure était « `normalizeFolderDepths` rend **zéro** correction après chaque
> geste », et il est tenu sur les sept cas du banc — contre deux à quatre
> corrections avant l'étape. C'est un critère plus fort que « le contenu a
> suivi » : la normalisation était le filet qui rattrapait les dégâts, et **un
> arbre qui a besoin d'être normalisé après un geste est un arbre que le geste
> a cassé**.
>
> **LA RÈGLE DE PROFONDEUR A ÉTÉ CORRIGÉE PAR LA MESURE.** La première version
> adoptait toujours au PLAFOND. Vérifiée dans l'application, elle rendait
> `Prise1` là où l'on était parti de `Prise0` : « descendre » une piste jusqu'en
> bas la faisait entrer dans le dossier qu'elle venait de traverser. La règle
> est devenue **« on garde sa profondeur, sauf là où la place l'interdit »** —
> un plafond venu de la piste d'avant, un plancher venu de la piste d'après.
> Elle adopte là où aucun autre arbre ne serait valide, et seulement là.
>
> **ET L'ATTENDU DE D35.1 ÉTAIT TROP LARGE.** « Un aller-retour rend exactement
> le projet de départ » est vrai **tant que le chemin ne traverse pas un
> dossier**, et faux quand il en traverse un — par nécessité : entre deux
> membres d'un dossier, il n'existe aucun arbre valide où la piste serait à la
> racine. Deux tests écrivent les deux cas plutôt que d'en taire un. Quand un
> dossier va jusqu'au bout de la liste, sa dernière ligne est encore dedans : on
> en sort par *Piste ▸ Sortir du dossier*.
>
> **D35.3 — Supprimer et dupliquer.** Supprimer un dossier **emporte son
> contenu et le DIT** (« Dossier supprimé : 3 piste(s) retirée(s) avec lui »).
> L'autre choix — ne retirer que l'étiquette — avait pour lui la prudence, et
> contre lui d'être exactement ce que le code faisait **par accident**. La
> duplication était pire : la copie de l'en-tête s'insérait **entre** le dossier
> et ses membres, qui passaient sous la copie — dupliquer un dossier lui
> **volait** son contenu.
>
> **D35.4 — Le muet et le solo d'un dossier atteignent son contenu.** Mesuré sur
> le RENDU et non sur un booléen : le rendu d'un projet dont le dossier est muet
> est identique **à 0,000000000 près** au rendu du même projet où chaque membre
> serait muet à la main ; idem pour le solo ; et un dossier qui ne dit rien
> laisse le rendu identique au bit près à ce qu'il était avant la phase.
>
> **L'ATTENDU ÉTAIT FAUX SUR UN POINT, ET LE RAISONNEMENT SUFFISAIT À LE DIRE.**
> Il annonçait qu'une piste `soloSafe` resterait audible sous un dossier muet.
> `soloSafe` veut dire « le solo des **autres** ne me concerne pas », et non
> « je suis toujours audible » — la règle n° 2 de `trackAudible` le disait déjà,
> puisqu'une piste protégée obéit à son propre muet. Le muet d'un dossier est un
> muet posé sur elle, pas le solo d'un tiers.
>
> **ET LES CINQ BANCS « AUCUNE ALLOCATION DANS `process()` » ONT ATTRAPÉ LA
> PREMIÈRE VERSION.** Elle copiait la piste pour y écraser trois champs —
> jusqu'à **6 424 allocations** par bloc, sur le thread audio. Ces bancs n'ont
> pas seulement dit « c'est faux » : ils ont dit *où*, en une seconde.
>
> **D35.5 — Un dossier n'a plus de tranche au mélangeur.** Il n'est pas un bus
> (`Kind::Group` l'est) : aucun signal n'y passe, et son fader, son panoramique,
> son trim, ses départs, ses inserts et ses vumètres étaient six commandes
> mortes. **Une commande qui ne fait rien est pire qu'une commande absente,
> parce qu'elle promet.** Son muet et son solo, eux, agissent depuis D35.4 et
> restent là où le dossier vit : dans la liste des pistes.
>
> **CE QUE CELA COÛTAIT, ET COMMENT C'EST PAYÉ.** Un membre tu par son dossier
> n'aurait plus eu, dans le mélangeur, de tranche qui l'explique : une tranche
> silencieuse dont aucun bouton n'est enfoncé, c'est-à-dire une panne muette.
> **Son bouton M s'allume donc quand le silence lui vient d'un dossier**, avec
> l'infobulle qui le dit. Vérifié à l'écran : trois tranches pour quatre pistes,
> et les deux M des membres allumés en rouge.
>
> **ET RETIRER UNE TRANCHE A CASSÉ TROIS CHOSES QU'IL A FALLU RÉPARER**, parce
> que la n-ième tranche n'est plus la n-ième piste : les vumètres lisaient au
> rang (on aurait vu, sous un dossier, le niveau de la voisine — et un vumètre
> qui bouge a l'air juste), la largeur des tranches masquées de D17.4 aussi, et
> le MIDI Learn de D29.3 aurait piloté la piste d'à côté. Les trois passent
> désormais par `ChannelStrip::trackIndex()`.
>
> Tests : **1 283 audio** (4 neufs), **316 core** (8 neufs), 285 interchange,
> 25 clap, 11 panels — tous verts.

### Phase D36 — Le vingt et unième audit : les gestes qui échappent à l'annulation et à la sauvegarde (07/09/2026, 09:10)

**Pourquoi cet audit a changé de lunette.** D35 a cherché « ce qui existe et
que personne n'appelle », et la même lunette, repassée sur `core/`, ne rend
plus que des primitives enveloppées ailleurs : `snapNoteToScale` sert à
`constrainNotesToScale`, `setNotesMuted` à `toggleNotesMuted`. La veine est
épuisée, et s'obstiner y aurait produit exactement la faute que D34 s'était
promis d'éviter : annoncer absent ce qui est là sous un autre nom.

La lunette de cet audit est autre, et elle vient d'une phrase écrite dans le
code lui-même. `MainComponent::beginProjectEdit` porte ce commentaire :

> *TOUTES LES MODIFICATIONS ANNULABLES PASSENT PAR ICI (D10.4) : c'est
> l'endroit qui ne peut pas être oublié, parce qu'oublier de l'appeler
> casserait déjà l'annulation, ce qui se voit tout de suite.*

**Cette phrase est fausse sur ses deux moitiés**, et la seconde explique la
première : l'oubli ne « se voit pas tout de suite », parce que l'annulation
n'est pas absente — elle est DÉCALÉE. `SnapshotHistory` restaure un instantané
du projet ENTIER. Un geste qui n'empile rien n'ôte donc pas le Ctrl+Z : il
laisse le Ctrl+Z suivant remonter à l'instantané d'AVANT, c'est-à-dire annuler
le geste précédent **et** celui qu'on vient de faire, en une fois et sans le
dire. Rien ne clignote, rien n'échoue ; on croit avoir annulé une chose, on en
a perdu deux.

**Et `beginProjectEdit` fait une SECONDE chose que son nom ne dit pas** : il
appelle `markProjectDirty()`. Le drapeau `projectDirty_` commande la
sauvegarde automatique (`autosaveIfNeeded`). Un geste qui ne passe pas par là
n'est donc pas seulement inannulable : **il n'est jamais photographié**. Une
séance passée à renommer des pistes et à régler des faders dans la liste laisse
la copie de secours dans l'état où elle était avant — et une coupure de courant
la rend telle quelle, sans un mot.

**Le relevé, mesuré et non supposé.** Trois endroits de l'interface écrivent
dans le matériau du projet sans passer par l'historique :

| endroit | ce qu'il écrit | annulable | photographié |
|---|---|---|---|
| `TrackListComponent` (la ligne de piste) | `name`, `channel`, `instrumentId`, `folded`, `muted`, `solo`, `outputGroup`, `volume`, `pan` — **neuf champs** (plus `armed`, qui est de session et n'en demande pas) | non | non |
| `VelocityLaneComponent` | `note.velocity`, au clic et au trait (`:111`, `:132`) | non | non |
| `StepSequencerComponent` | `writePatternToTrack` — **il RÉÉCRIT le vecteur de notes de la piste** | non | non |

**Le fait qui résume la phase : le même bouton, deux comportements.** Le muet,
le solo, le volume et le panoramique d'une piste existent à DEUX endroits — la
tranche du mélangeur et la ligne de la liste des pistes. Dans le mélangeur ils
passent par `onMixEditStarted`, donc s'annulent et se photographient ; dans la
liste ils ne passent par rien. **Le geste est le même, la valeur est la même,
et le résultat dépend du panneau où l'on a cliqué.** Rien à l'écran ne le
laisse deviner.

Le troisième est le plus coûteux : basculer un pas dans le séquenceur d'une
machine **remplace les notes de la piste**, et rien ne les rend.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D36.1 | **La liste des pistes rejoint l'historique.** Neuf champs écrits à la volée. La ligne ne doit pas connaître l'historique — elle ne connaît même pas le projet, et c'est une décision de son en-tête — : elle reçoit un `onEditStarted`, comme la tranche du mélangeur, et c'est `MainComponent` qui appelle `beginProjectEdit` | chacun des neuf gestes empile un pas nommé (« Renommer la piste », « Muet », « Volume »…) et marque le projet à photographier ; `armed` continue de n'en empiler aucun, et **c'est écrit**, parce qu'il n'est pas dans le fichier |
| D36.2 | **La lane de vélocité rejoint l'historique.** Un trait de vélocité change autant de notes qu'un coup de quantification, et s'annule moins bien qu'elle | un trait empile UN pas, pas un par note traversée — le glissé est un geste, pas une rafale de gestes |
| D36.3 | **Le séquenceur pas à pas rejoint l'historique**, lui qui réécrit tout le vecteur de notes | basculer un pas s'annule et rend la piste telle qu'elle était, notes non issues du motif comprises |
| D36.4 | **Le banc qui empêche le prochain oubli.** Trois oublis en vingt phases ne se réparent pas un par un : ce qui manque est la mesure qui les nomme | un banc parcourt les gestes d'édition et dit, pour chacun, s'il empile un pas et s'il marque le projet ; il ÉCHOUE sur un geste qui n'en fait ni l'un ni l'autre sans être inscrit dans la liste des exceptions dites |
| D36.5 | **`presetId` : un champ que personne ne remplit.** Cinq endroits l'effacent, aucun ne l'écrit, `project.json` l'ignore. C'est `monitoring` une seconde fois — le champ que D3.3 avait retiré pour la même raison | ou bien il disparaît, ou bien il est rempli et écrit ; **la décision est écrite avec sa raison** |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D36.1 à D36.3** — j'attends que le banc de D36.4, passé AVANT les
   corrections, compte **onze gestes** sans pas d'historique (neuf dans la
   liste des pistes, la vélocité, le motif), et **zéro** après. Le chiffre est
   écrit d'avance pour qu'on ne puisse pas, ensuite, appeler « tout » ce qu'on
   aura trouvé.
2. **D36.3** — j'attends que l'annulation d'un pas basculé rende le vecteur de
   notes **identique**, y compris les notes que le motif ne décrit pas. Un
   séquenceur qui rend « le motif d'avant » mais mange ce qui n'en faisait pas
   partie aurait l'air correct sur un projet de banc et perdrait du travail sur
   un vrai morceau.
3. **D36.2** — j'attends **un** pas d'historique pour un glissé, pas un par
   note. Ce critère est plus fort que « le trait s'annule » : un trait qui
   s'annule note à note s'annule, en effet, et demande quarante Ctrl+Z.
4. **La photographie.** J'attends qu'une séance qui ne fait QUE renommer une
   piste déclenche une sauvegarde automatique après les corrections, et
   **aucune** avant. C'est la moitié de la panne que l'annulation cachait, et
   celle qui coûte le plus cher.

> **LA PHASE D36 EST FAITE (07/09/2026, 11:40), ET L'AUDIT S'EST TROMPÉ SUR UN
> TIERS DE SON TABLEAU.** Le relevé nommait trois endroits ; le deuxième était
> faux, et le vrai défaut caché derrière lui était bien plus grave que les
> trois réunis.
>
> **CE QUE LE TABLEAU DISAIT DE FAUX.** « `VelocityLaneComponent` écrit
> `note.velocity` sans passer par l'historique » : **non**. Il appelle
> `pianoRoll_.beginExternalEdit(…)` à `mouseDown`, un pas par geste, exactement
> ce que D36.2 demandait. Je l'avais conclu de l'absence du mot `history_` dans
> le fichier — c'est-à-dire d'un `grep`, encore, et c'est la **troisième**
> phase de suite où le vocabulaire est le mauvais outil (D33.5 l'aimant
> relatif, D34.1 le fondu croisé). La leçon ne se retient pas en la réécrivant :
> ce qui a fini par marcher, ici, est d'avoir suivi l'appel jusqu'à ce qu'il
> fasse.
>
> **ET C'EST EN LE SUIVANT QU'EST APPARU LE VRAI DÉFAUT.**
> `PianoRollComponent::beginEdit` empile bien dans `history_` — mais
> `beginProjectEdit` faisait DEUX choses, et lui n'en fait qu'une : il n'appelle
> jamais `markProjectDirty()`. Le piano roll, la lane de vélocité, l'onglet MIDI
> CC et la piste de tempo prennent tous ce chemin-là. **Les trente-deux gestes
> d'édition de notes — le cœur du logiciel — s'annulaient parfaitement et
> n'étaient JAMAIS photographiés.** Une séance entière de travail sur les notes
> laissait la copie de secours dans l'état d'avant la séance, et une coupure de
> courant la rendait telle quelle, sans un mot.
>
> **LA CORRECTION RENVERSE LE SENS DE LA DÉCLARATION.** Le drapeau ne
> s'annonce plus, il se DÉDUIT : `autosaveIfNeeded` compare la profondeur de
> l'historique à celle de la dernière photo. Un pas d'historique EST la preuve
> qu'on a modifié le projet ; le déduire ne peut pas s'oublier, alors que le
> déclarer s'est oublié quatre fois. `clearHistory()` remet les deux ensemble,
> parce que les vider séparément ferait croire, au réveil suivant, qu'on vient
> d'annuler autant de pas que la pile en contenait.
>
> **D36.1 — la liste des pistes.** Neuf gestes écrivaient dans la piste sans
> rien empiler. Le chiffre était écrit d'avance ; le banc en a mesuré **neuf**
> avant, **zéro** après. Les curseurs signalent à `onDragStart` — un pas par
> glissé et non un par pixel — et aussi à `onValueChange` quand aucun glissé
> n'est en cours, sans quoi la molette et le clavier resteraient inannulables
> tout en ayant l'air couverts.
>
> **D36.3 — le séquenceur pas à pas**, le plus cher des trois : basculer un pas
> appelle `writePatternToTrack`, qui réécrit le vecteur de notes de la piste.
> Il n'avait aucun pas d'historique. Il en a un.
>
> **D36.4 — LE BANC, ET CE QU'IL A TROUVÉ TOUT SEUL.** `vsm-edit-audit`
> n'énumère pas des gestes connus : il PARCOURT les widgets d'une ligne de
> piste et exige de chacun un signal. Un widget ajouté demain sans être câblé
> le fait échouer le jour où on l'ajoute. Et en mesurant D36.3 plutôt qu'en
> l'affirmant, il a montré ce que personne ne cherchait : **le clic sur un pas
> mangeait une note que la grille ne montre pas.**
>
> **D36.6 (né du banc) — la grille n'efface plus que ses propres hauteurs.**
> `writePatternToTrack` bornait son effacement dans le TEMPS et pas dans la
> HAUTEUR, alors que `patternFromNotes` annonce, à la lecture, que « les notes
> hors grille sont ignorées : elles restent dans la piste ». **La lecture les
> ignorait, l'écriture les tuait** : basculer un pas de charleston effaçait une
> note de tom posée au piano roll dans la même mesure. Un motif percussif ne
> possède désormais que les hauteurs de ses lignes ; un motif mélodique garde
> sa fenêtre entière, parce qu'il peut poser n'importe quel pas sur n'importe
> quelle hauteur. Le drapeau `StepPattern::melodic` est POSÉ par
> `makeMonoPattern` et non deviné : un motif mélodique dont aucun pas n'a été
> déplacé ressemble trait pour trait à un motif percussif à une ligne.
>
> **D36.7 (né de la vérification à l'écran) — les deux panneaux se disent enfin
> la même chose.** La capture de contrôle et celle du geste ne différaient que
> par un bouton *Annuler* devenu actif : le M du mélangeur, lui, n'avait pas
> bougé. La piste ÉTAIT muette — le banc le mesure —, mais chaque panneau posait
> son bouton **une seule fois, à sa construction**. Rendre une piste muette dans
> la liste laissait le mélangeur montrer le contraire, et l'inverse aussi.
> `refreshMuteSolo` existait déjà du côté du mélangeur et n'était appelé par
> aucun geste de la liste ; il a son jumeau côté liste, et les deux sens sont
> branchés. **Sans la capture, ce défaut passait : les tests étaient verts et le
> banc aussi.**
>
> **D36.5 — `presetId` est parti.** Cinq endroits l'effaçaient, aucun ne
> l'écrivait, `project.json` l'ignorait. Le preset d'une piste vit dans
> `instruments/track_NN.synth.json`, que le format référence par son chemin ; une
> seconde façon de le désigner aurait fini par le désigner autrement. C'est
> `monitoring` une seconde fois, retiré par D3.3 pour la même raison.
>
> **CE QUI RESTE VRAI DE L'ATTENDU, ET CE QUI NE L'EST PAS.** Les neuf gestes de
> la liste : tenu, au chiffre près. Un pas par glissé : tenu. L'annulation qui
> rend le vecteur de notes entier : tenue par construction, puisque l'instantané
> est celui du projet. **« Onze gestes sans pas d'historique » était faux** : il
> y en avait dix (neuf plus le séquenceur), la vélocité en étant déjà pourvue —
> et il y avait, à la place du onzième, une panne d'un autre ordre que le
> chiffre ne pouvait pas compter.
>
> Vérifié à l'écran, deux exécutions du même binaire ne différant que par
> `VSM_GESTE_PISTE=muet` : *Annuler* passe de grisé à actif, et le M du
> mélangeur de sombre à rouge.
>
> Tests : **1 283 audio**, **319 core** (3 neufs), 285 interchange, 25 clap,
> 11 panels — tous verts, plus le banc `vsm-edit-audit` (11 gestes, 0 muet).

> **CORRECTION DE D36, ÉCRITE LE JOUR MÊME (07/09/2026, 12:05) : LA MOITIÉ
> « PHOTOGRAPHIE » DE CETTE PHASE ÉTAIT LARGEMENT FAUSSE.** Elle est corrigée
> ici plutôt qu'effacée, parce que la façon dont elle s'est trompée compte plus
> que le chiffre.
>
> **CE QUI A ÉTÉ AFFIRMÉ SANS ÊTRE MESURÉ.** J'ai lu que `beginProjectEdit`
> appelait `markProjectDirty()`, constaté que le piano roll ne passait pas par
> `beginProjectEdit`, et conclu que ses trente-deux gestes n'étaient jamais
> photographiés. **Je n'ai pas suivi l'autre chemin.** `refreshTransportSchedule`
> appelle `markProjectDirty()` **depuis D10.4**, la phase même qui a créé la
> sauvegarde automatique, et son commentaire l'annonce en toutes lettres : « la
> sauvegarde automatique doit le savoir, **même quand le changement n'est pas
> passé par l'historique** ». Le piano roll y arrive par `onNotesEdited`. Ses
> éditions étaient photographiées. C'est exactement la faute que le paragraphe
> d'à côté reprochait à l'audit — conclure d'un chemin qu'on a lu qu'aucun
> autre n'existe —, commise dans le même mouvement que sa dénonciation.
>
> **LE RELEVÉ EXACT, CETTE FOIS TRACÉ APPEL PAR APPEL.** Des neuf gestes de la
> ligne de piste, **six** atteignaient `markProjectDirty` : le canal, le repli,
> le muet, le solo, le volume et le panoramique appellent tous `onChanged`, donc
> `onTracksChanged`, donc `refreshTransportSchedule`. **Trois** ne
> l'atteignaient pas, et ceux-là étaient bien perdus à la coupure :
>
> | geste | chemin d'avant | photographié |
> |---|---|---|
> | **renommer** | aucun rappel du tout (`TrackListComponent.cpp:34`) | non |
> | **machine de la piste** | `onInstrumentChanged` → le moteur, rien d'autre | non |
> | **sortie de la piste** | `onOutputChanged` → `mixDirty_`, qui republie au moteur sans jamais marquer le projet | non |
> | canal, repli, muet, solo, volume, panoramique | `onChanged` → `refreshTransportSchedule` | **oui** |
>
> **CE QUI RESTE ENTIÈREMENT VRAI.** La moitié « annulation » de la phase :
> **neuf** gestes n'empilaient aucun pas, mesuré à neuf avant et à zéro après
> par un banc qui actionne les widgets. Le séquenceur pas à pas n'en empilait
> aucun non plus. D36.6 (la grille effaçait des hauteurs qu'elle ne montre pas)
> et D36.7 (deux panneaux qui se contredisent) sont mesurés et vérifiés à
> l'écran, et ne dépendent pas de cette erreur.
>
> **CE QUE DEVIENT LA CORRECTION DE D36.2.** Déduire le drapeau de la
> profondeur de l'historique se garde, mais pour ce qu'elle vaut réellement :
> non pas « elle sauve les trente-deux gestes du piano roll », qui l'étaient
> déjà, mais **elle rend la photographie indépendante du fait que quelqu'un
> pense à passer par `refreshTransportSchedule`**. Les trois gestes du tableau
> ci-dessus, qui n'y passaient pas, sont désormais couverts deux fois : par le
> pas d'historique que D36.1 leur donne, et par la déduction. Le gain est réel
> et il est petit ; l'annoncer comme grand était une faute de mesure, pas
> d'intention.
>
> **LA LEÇON, ET ELLE VISE UNE HABITUDE PRÉCISE.** Un chemin d'appel n'est pas
> mesuré tant qu'on n'a pas cherché **les autres**. J'ai suivi
> `beginProjectEdit` jusqu'au bout et je me suis arrêté là, alors que la
> question posée — « qui marque le projet ? » — se répondait en cherchant les
> appelants de `markProjectDirty`, ce qui prend une commande et rend neuf
> lignes. Chercher où une chose est FAITE, et non où l'on croyait qu'elle
> l'était.

### Phase D37 — Le vingt-deuxième audit : une valeur, plusieurs panneaux, un seul qui la relit (07/09/2026, 12:20)

**D'où vient cet audit : d'un défaut que seule la capture a montré.** D36.7 n'a
pas été trouvé en lisant le code ni en passant un banc. Les deux captures du
contrôle et du geste ne différaient que par un bouton *Annuler* devenu actif, et
c'est **ce qui n'avait PAS changé** qui a parlé : le M du mélangeur restait
éteint alors que la piste était muette. Chaque panneau posait son bouton une
seule fois, à sa construction, et ne le relisait jamais.

Ce défaut n'a aucune raison d'être seul. Il tient à une forme d'écriture — un
widget qui reçoit `track_.quelquechose` dans un constructeur — et cette forme est
partout. **La question de cet audit est donc : quelles valeurs s'affichent à
plus d'un endroit, et lesquelles sont relues ?**

**Le relevé des recoupements, mesuré sur les champs que chaque panneau lit.**

| valeur | panneaux qui l'affichent |
|---|---|
| `name` | liste des pistes, mélangeur, arrangement, liste d'événements, automation, chaîne d'effets — **six** |
| `volume`, `pan` | liste des pistes, mélangeur |
| `colorRgba` | liste des pistes, mélangeur, arrangement |
| `muted`, `solo` | liste des pistes, mélangeur — **réglé par D36.7** |

Le nom est le cas le plus lourd : **six panneaux**, dont trois le mettent dans
un widget à la construction (le libellé d'une tranche, une liste déroulante de
pistes, une liste d'événements) et trois le lisent au dessin. Les premiers
mentent jusqu'à leur reconstruction ; les seconds non. Rien ne distingue les uns
des autres à l'écran.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D37.1 | **Renommer une piste, et voir combien de panneaux le savent.** Mesuré à l'écran par deux exécutions du même binaire ne différant que par le geste, comme D36.7 | le relevé dit, panneau par panneau, lequel affiche le nom neuf et lequel l'ancien ; puis tous affichent le neuf |
| D37.2 | **Le volume et le panoramique**, qui existent au fader du mélangeur ET au curseur de la ligne | bouger l'un déplace l'autre, dans les deux sens, sans que le geste ne se rejoue |
| D37.3 | **La couleur d'une piste**, affichée à trois endroits | les trois suivent |
| D37.4 | **Le banc, étendu à la classe entière.** D36.4 a montré qu'un banc qui parcourt vaut mieux qu'une liste qu'on tient à jour : celui-ci compare, pour chaque valeur partagée, ce que chaque panneau affiche à ce que la piste dit | il ÉCHOUE dès qu'un panneau montre autre chose que la piste ; un panneau ajouté demain y entre sans qu'on ait à s'en souvenir |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D37.1** — j'attends que l'**arrangement suive** le renommage sans rien
   faire (il lit `track.name` au dessin) et que la **tranche du mélangeur ne le
   suive pas** (son libellé est posé au constructeur). C'est une prédiction qui
   peut se tromper des deux côtés, et c'est pour cela qu'elle est écrite.
2. **D37.2** — j'attends que **ni** le fader **ni** le curseur ne suivent
   l'autre : tous deux reçoivent leur valeur au constructeur, exactement comme
   les boutons de D36.7.
3. **La boucle.** J'attends que brancher les deux sens ne rejoue pas le geste :
   un rafraîchissement qui notifie renverrait la balle à l'autre panneau
   indéfiniment. C'est le risque propre à cette correction, et le seul —
   `dontSendNotification` est la réponse, encore faut-il le vérifier plutôt que
   de l'affirmer, un curseur n'ayant pas les mêmes règles de notification qu'un
   bouton.

> **LA PHASE D37 EST FAITE (07/09/2026, 13:30), ET LES TROIS PRÉDICTIONS
> ÉTAIENT JUSTES — CE QUI N'A PAS EMPÊCHÉ LA MESURE DE TROUVER PLUS.**
>
> **D37.1 — le nom d'une piste s'affiche à SEPT endroits, pas six.** Le relevé
> écrit d'avance en comptait six ; la capture en a montré un septième que
> `grep` ne pouvait pas trouver, parce qu'il ne lit pas `track.name` : le
> **rack de machines**, dont le grand titre reçoit le nom en paramètre
> (`setSynth(synth, trackName, …)`). Mesuré à l'écran, deux exécutions du même
> binaire ne différant que par `VSM_GESTE_PISTE=renommer:ZZTOP` :
>
> | panneau | avant la correction |
> |---|---|
> | ligne de piste | ZZTOP |
> | arrangement | ZZTOP — il lit le nom au dessin |
> | mélangeur | **Bass** |
> | rack de machines | **Bass** |
> | automation (liste « Piste ») | **Bass** |
> | liste d'événements | **Bass** |
> | chaîne d'effets | **Bass** |
>
> **Deux sur sept.** La prédiction — l'arrangement suit, le mélangeur non —
> était juste des deux côtés, et c'est la seule raison pour laquelle elle
> valait d'être écrite. Les sept suivent désormais, par un seul
> `refreshTrackNamesEverywhere()` : un huitième panneau n'aura qu'une ligne à
> ajouter là.
>
> **ET RAFRAÎCHIR UN TITRE A FAILLI CASSER CE QU'ON FAISAIT.** Le titre de la
> chaîne d'effets était calculé DANS `setActiveTrack`, qui remet aussi
> `selectedEffect_ = -1`. Le rafraîchir par ce chemin aurait effacé l'effet
> choisi au milieu d'un réglage : **une correction d'affichage qui casse ce
> qu'on était en train de faire est pire que l'affichage faux qu'elle
> corrige.** Le calcul du titre a donc son propre `refreshTrackName()`.
>
> **D37.2 — LE BANC A ATTRAPÉ UN DÉFAUT QUE J'AI ÉCRIT EN CORRIGEANT.** La
> prédiction (ni le fader ni le curseur ne suivent l'autre) était juste. Mais
> `refreshFromTrack` posait `track_.volume` — un gain linéaire — dans le fader
> de la tranche, **qui est en décibels** (-60 à +6). Un gain de 0,25 s'y
> affichait « 0,3 dB » : la valeur tombait dans la plage sans y avoir de sens,
> donc sans que rien ne proteste. Les deux conversions vivaient dans un espace
> anonyme du `.cpp`, invisibles depuis l'en-tête où j'écrivais ; elles sont
> remontées dans l'en-tête, à un seul endroit.
>
> **CE QUI A RENDU CE DÉFAUT VISIBLE EST LA FORME DE LA MESURE.** Le banc
> compare ce que chaque panneau **AFFICHE** (`nomAffiche()`, `volumeAffiche()`),
> et non ce que la piste contient. Lire la piste des deux côtés aurait donné
> deux fois la même valeur juste et n'aurait rien montré : **c'est le désaccord
> entre deux affichages qui est le défaut, pas la valeur.**
>
> **ET LA TOLÉRANCE A DÛ ÊTRE PRISE DANS L'INSTRUMENT.** Comparer les gains
> échouait de 0,001 sur 0,25 — non parce que les panneaux se contredisent, mais
> parce qu'un fader qui avance par pas de 0,1 dB **n'a pas de position** pour
> ce gain-là. La comparaison se fait en décibels, à un demi-pas près. Une
> tolérance prise ailleurs que dans la résolution de l'instrument est un
> chiffre qu'on ajuste jusqu'à ce que le banc passe.
>
> **D37.3 — la couleur suivait déjà, et c'est le MOYEN qui était mauvais.**
> L'audit attendait un défaut ; il n'y en avait pas. `ColourApplier` appelait
> `trackList_.loadProject()` et `mixer_.setProject()`, c'est-à-dire
> **détruisait et refabriquait toutes les lignes et toutes les tranches** — des
> dizaines de fois pendant un seul glissé, le sélecteur de couleur émettant un
> changement par pixel. Or les trois panneaux lisent `colorRgba` **au dessin** :
> trois `repaint()` suffisent. Refabriquer une ligne pendant qu'on s'en sert
> détruit le widget qui a le focus ; **la bonne mesure d'un rafraîchissement est
> ce que le panneau LIT, pas ce qu'il contient.** Vérifié à l'écran : le bandeau
> de couleur devient vert dans les trois panneaux, sans une reconstruction.
>
> **La boucle attendue n'a pas eu lieu**, et c'était le seul risque de cette
> phase : `dontSendNotification` des deux côtés, et l'application traverse les
> deux sens sans se renvoyer la balle.
>
> Tests : 1 283 audio, 319 core, 285 interchange, 25 clap, 11 panels — tous
> verts ; banc `vsm-edit-audit` : 11 gestes, 0 muet, 0 désaccord.

### Phase D38 — Une seule piste à la fois : la sélection multiple manque entièrement (07/09/2026, 14:00)

**Pourquoi celle-ci, et pas un vingt-troisième audit.** Les deux phases
précédentes ont cherché des défauts dans ce qui existe. Ici le manque est
franc, il est nommé par le § 2 (« digne de Cubase, Live, FL Studio ») et il se
constate en une ligne : `TrackListComponent::selectedIndex_` est un `size_t`.
**Il n'y a pas de sélection de pistes ; il y a une piste courante.** Les clips,
eux, ont une vraie sélection (`ClipSelection`, un ensemble d'identifiants)
depuis D5 — le modèle sait donc faire, c'est la liste des pistes qui ne sait
pas.

`TrackRowComponent::mouseDown` le dit d'un trait : il appelle `onSelected` et
**ne regarde pas les modificateurs**. Ni Ctrl+clic, ni Maj+clic.

**Ce que cela coûte, en gestes.** Une reconstruction en parité aligne quinze
pistes, dont huit micros de batterie. Les taire toutes demande **huit clics et
huit pas d'annulation** — huit Ctrl+Z pour revenir. Les dossiers de D35
couvrent le cas *si* les pistes sont rangées dans un dossier ; une
reconstruction les rend à plat. Changer la couleur de six pistes, en supprimer
trois, en masquer quatre : autant de fois le même geste.

**LA DÉCISION QUI REND CETTE PHASE PETITE, et elle est écrite ici parce que
c'est elle qui décide de tout le reste.** `selectedTrackIndex()` est appelé
**soixante-deux fois** dans `MainComponent`. Le remplacer partout par un
ensemble serait un chantier de plusieurs jours et casserait tout ce qui ne
s'applique QU'À UNE piste (le piano roll, le rack, la chaîne d'effets, l'onglet
MIDI CC : ils éditent une piste, pas six). La sélection multiple s'ajoute donc
**à côté** de la piste courante, sans la remplacer :

- `selectedTrackIndex()` garde exactement son sens — **la piste ACTIVE**, celle
  qu'on vient de désigner, celle qu'éditent le piano roll et le rack. Les
  soixante-deux appels restent justes, mot pour mot.
- `selectedTracks()` est neuf : l'ensemble, qui contient toujours la piste
  active. Seuls les gestes qui ont un sens sur plusieurs pistes le consultent.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D38.1 | **La sélection existe et se voit.** Ctrl+clic ajoute ou retire, Maj+clic étend depuis la piste active, un clic simple ramène à une seule. Elle survit à une reconstruction de la liste, et une piste supprimée en sort | les lignes choisies se dessinent comme telles ; `selectedTracks()` ne contient jamais d'index hors bornes |
| D38.2 | **Les gestes qui se multiplient**, et UN SEUL pas d'annulation pour le lot : muet, solo, couleur, masquer, supprimer | taire six pistes choisies demande un clic et rend un seul Ctrl+Z ; mesuré, pas supposé |
| D38.3 | **Les gestes qui NE se multiplient PAS, et pourquoi.** Renommer (six pistes du même nom ne sont plus distinguables), le canal MIDI, la machine, l'armement (D3.3 : une seule piste audio armée à la fois) | la décision est écrite avec sa raison, dans le code et ici |
| D38.4 | **Agir sur une piste hors de la sélection la REMPLACE.** Six pistes choisies, on clique le M d'une septième : Cubase tait la septième seule, et la choisit. L'autre règle — ajouter la septième au lot — ferait agir sur des pistes qu'on ne regarde pas | un geste sur une ligne non choisie ramène la sélection à elle seule, avant d'agir |
| D38.5 | **Le banc suit.** `vsm-edit-audit` mesure aujourd'hui qu'un geste empile UN pas ; il doit mesurer qu'un geste sur six pistes en empile UN aussi, et en touche six | le banc échoue si un geste multiplié empile un pas par piste |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D38.2** — j'attends **un** pas d'historique pour six pistes tues, et
   **six** pistes muettes. Les deux moitiés comptent : un seul pas qui ne
   tairait qu'une piste serait aussi faux que six pas qui en taisent six. C'est
   le même critère qu'en D36.2 pour le glissé, et pour la même raison — un lot
   qui s'annule pièce par pièce s'annule, en effet, et demande six Ctrl+Z.
2. **D38.4** — j'attends que la règle « une piste hors sélection remplace la
   sélection » se vérifie **à l'écran** et pas seulement dans le banc : c'est
   une règle d'usage, et son défaut serait de faire quelque chose d'invisible.
3. **La suppression.** J'attends que supprimer plusieurs pistes à la fois ne
   décale pas les index en cours de route — supprimer les pistes 2, 5 et 7 en
   remontant depuis la fin, jamais en descendant. C'est la faute classique de
   ce geste, et elle est silencieuse : on supprime la 5 puis la 7, qui n'est
   plus la même.

> **LA PHASE D38 EST FAITE (07/09/2026, 15:40).** Les cinq étapes, et deux
> défauts que seule la mesure a montrés.
>
> **D38.1 — la sélection existe.** Ctrl+clic ajoute ou retire, Maj+clic étend
> **depuis l'ancre** — le dernier clic simple — et non depuis la piste active :
> étendre depuis le résultat de l'extension précédente ferait grandir la
> sélection à chaque Maj+clic au lieu de la redessiner. On ne peut jamais en
> retirer la dernière : une liste sans piste active n'a rien à montrer au piano
> roll ni au rack.
>
> **LA DÉCISION QUI A RENDU LA PHASE PETITE A TENU.** `selectedTracks()`
> s'ajoute à côté de `selectedTrackIndex()` sans le remplacer : les
> **soixante-deux** appels de `MainComponent` sont restés justes mot pour mot,
> et pas une ligne du piano roll, du rack ou de la chaîne d'effets n'a bougé.
>
> **D38.2 — un geste, un pas.** Mesuré : six pistes choisies, un clic sur le M
> de l'une d'elles, **six pistes tues et UN pas d'historique**. Les deux
> moitiés étaient écrites d'avance et comptent autant : un pas unique qui n'en
> tairait qu'une ne ferait pas le geste ; six pas qui en taisent six ne
> s'annulent pas d'un coup. Le lot pose **le même état sur toutes** — celui de
> la piste cliquée, renversé — plutôt que de renverser chacune : sur six pistes
> dont deux muettes, se renverser chacune en laisserait quatre muettes et deux
> non, ce qui ne ressemble à aucune intention.
>
> **LA SUPPRESSION DESCEND, ET L'ATTENDU N° 3 VISAIT JUSTE.** Supprimer les
> pistes 2, 5 et 7 en montant supprime la 2, ce qui fait glisser tout ce qui
> suit : la « 5 » suivante est l'ancienne 6, la « 7 » l'ancienne 9. On efface
> trois pistes dont deux qu'on n'avait pas désignées, sans un mot. Le lot est
> donc trié **décroissant** avant d'être appliqué.
>
> **D38.3 — quatre gestes ne se multiplient pas, et la règle est écrite.**
> Renommer (six pistes du même nom ne se distinguent plus, et le nom est
> justement ce qui les distingue), le canal MIDI (mettre six pistes sur le même
> canal les fait jouer l'une par-dessus l'autre : le geste a l'air d'un réglage
> et fait une fusion), la machine, l'armement (D3.3 l'interdisait déjà).
> **La règle générale : un geste se multiplie quand il pose la MÊME valeur sur
> toutes les pistes sans les rendre indistinctes.**
>
> **D38.4 — agir hors de la sélection la remplace**, et c'est la règle de
> Cubase. L'autre choix — ajouter la piste cliquée au lot — ferait porter le
> geste sur six pistes qu'on ne regarde pas, c'est-à-dire sur ce qu'on a oublié
> d'avoir sélectionné : exactement la surprise qu'une sélection est censée
> éviter.
>
> **ET LA SÉLECTION ÉTAIT INVISIBLE — TROUVÉ PAR LA CAPTURE, PAS PAR LA
> LECTURE.** Les deux exécutions, avec et sans `choisir:0,1,2`, rendaient des
> images dont la différence était **exactement nulle**. Une ligne choisie ne
> tenait qu'à l'écart entre `panel` (#1f1f24) et `panelRaised` (#26262c) : sept
> unités de gris par canal. Or cette sélection commande désormais la
> **suppression**. Une sélection qu'on ne voit pas est précisément le « faire
> quelque chose d'invisible » contre quoi D38.4 venait d'être écrite — le
> défaut était donc dans la phase elle-même, deux étapes après sa mise en
> garde. Les lignes choisies portent maintenant un contour ambre.
>
> **LA CAPTURE A AUSSI CORRIGÉ L'OUTIL DE MESURE.** `VSM_GESTE_PISTE` était lu
> **avant** `VSM_MENU` : le `choisir:0,1,2` s'appliquait à une liste d'une seule
> piste, puis les pistes ajoutées par le menu reconstruisaient la liste et
> emportaient la sélection. La variable est passée après, et accepte désormais
> plusieurs gestes séparés par « ; » — montrer un lot demande d'abord de le
> choisir, puis d'agir dessus.
>
> **LE BANC A DÛ CHANGER D'ASSEMBLAGE, ET C'EST UNE LEÇON.** Depuis que le muet
> passe par la LISTE — seule à connaître la sélection —, une rangée construite
> à part ne signale plus rien : le banc de D36.4 aurait déclaré une régression
> là où il n'y a qu'un déplacement de responsabilité. Il conduit désormais une
> vraie `TrackListComponent` et descend son arbre pour trouver ses rangées.
> **Un banc qui monte son propre assemblage mesure son assemblage** ; celui-ci
> mesure celui de l'application.
>
> Vérifié à l'écran, un binaire et une variable : `choisir:0,1,2` dessine trois
> contours ambre, `choisir:0,1,2;muet` allume trois M rouges au mélangeur sur
> quatre tranches.
>
> Tests : 1 283 audio, 319 core, 285 interchange, 25 clap, 11 panels — verts ;
> banc : 11 gestes, 0 muet, 0 désaccord.

### Phase D39 — La sélection s'arrête à la liste : le clavier et les deux autres panneaux l'ignorent (07/09/2026, 16:10)

**Cette phase commence par un défaut que D38 a CRÉÉ, et que ni son banc ni sa
capture n'ont vu.** La table des raccourcis porte depuis longtemps
`TrackMuteSelected` et `TrackSoloSelected`. Ils appellent
`toggleMuteSelectedTrack`, qui écrit `project_.tracks[piste].muted` **en
direct**, sur la seule piste active :

| chemin | ce que fait « rendre muet » avec six pistes choisies |
|---|---|
| le bouton **M** d'une ligne | tait les **six**, un pas d'annulation (D38.2) |
| le **raccourci clavier** | tait **une** piste, un pas d'annulation |

**Le même geste, deux résultats, selon qu'on a pris la souris ou le clavier.**
Avant D38 les deux faisaient la même chose — une piste — et se valaient ; c'est
la phase précédente qui les a désaccordés, en n'en corrigeant qu'un.

**POURQUOI LA MESURE DE D38 NE L'A PAS VU, ET C'EST LA VRAIE LEÇON.** Le banc
appelle `TrackListComponent::basculerMuet`, et la capture clique le bouton :
**les deux mesuraient le même chemin.** Un troisième existait, et deux
instruments braqués au même endroit ne valent pas mieux qu'un seul. D36 avait
déjà nommé ce défaut sous sa forme « deux panneaux » ; il revient ici sous sa
forme « deux entrées », et la parade est la même — chercher **tous** les
chemins, pas le premier.

**Le reste de l'audit : la sélection s'arrête à la liste.** Trois panneaux
dessinent des pistes ; un seul sait laquelle est choisie.

| panneau | sait qui est choisi | permet de choisir |
|---|---|---|
| liste des pistes | oui (D38) | oui |
| arrangement | **non** — il dessine les en-têtes (`ArrangementComponent.cpp:1686`) sans jamais lire la sélection | **non** — aucun clic d'en-tête |
| mélangeur | **non** — aucune notion de tranche choisie | **non** |

Dans Cubase comme dans Live, choisir une piste dans l'un des trois la choisit
dans les trois : c'est ainsi qu'on navigue dans un morceau à quinze pistes.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D39.1 | **Les raccourcis rejoignent le lot.** Muet et solo au clavier passent par le chemin de la liste — celui qui consulte la sélection et n'ouvre qu'un pas | six pistes choisies, le raccourci en tait six ; mesuré par un chemin QUI N'EST PAS celui du bouton |
| D39.2 | **Étendre au clavier.** Maj+↑ et Maj+↓ agrandissent la sélection au lieu de la déplacer ; « tout choisir » existe pour les pistes | la navigation simple continue de ramener à une piste, et l'extension part de l'ancre, comme le Maj+clic de D38.1 |
| D39.3 | **L'arrangement montre et pose la sélection.** Cliquer un en-tête de piste la choisit ; les pistes choisies s'y voient | le contour ambre de D38.1 y a son équivalent, et Ctrl+clic y fait ce qu'il fait dans la liste |
| D39.4 | **Le mélangeur aussi** | une tranche choisie se voit ; cliquer son nom choisit la piste |
| D39.5 | **Le banc apprend à ne pas mesurer deux fois le même chemin.** C'est ce qui a laissé passer D39.1 | pour chaque geste multipliable, le banc éprouve **toutes** ses entrées — le bouton ET le raccourci — et échoue si elles ne s'accordent pas |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D39.1** — j'attends que le raccourci, mesuré sur six pistes choisies, en
   taise **six** et n'ouvre **qu'un** pas, exactement comme le bouton. Le
   critère qui compte n'est pas « le raccourci marche » mais « les deux entrées
   rendent la même chose » : c'est leur désaccord qui est le défaut.
2. **D39.3 et D39.4** — j'attends de devoir trancher une question que le § 2 ne
   règle pas : le mélangeur montre **les tranches**, et une tranche n'existe pas
   pour un dossier (D35.5). Choisir un dossier dans la liste n'a donc rien à
   éclairer dans le mélangeur. J'attends que ce cas apparaisse à la mesure et
   qu'il soit écrit plutôt que corrigé en silence.
3. **Ce qui n'est PAS sauvegardé.** J'attends de confirmer que la sélection
   reste un état de SESSION, comme l'armement (D3.3) et l'ordre de jeu (D18.4) :
   rouvrir un projet ne doit pas ressusciter une sélection qu'on ne se rappelle
   pas avoir posée, et l'invariant n° 5 du § 6 (« rien ne se perd ») ne
   s'applique pas à ce qui ne décrit pas le morceau.

> **LA PHASE D39 EST FAITE (07/09/2026, 17:20), ET SON PROPRE RELEVÉ S'EST
> TROMPÉ D'UNE CASE.**
>
> **D39.1 — le raccourci rejoint le lot, et le défaut était bien de D38.**
> `toggleMuteSelectedTrack` écrivait `project_.tracks[piste].muted` en direct.
> Il appelle désormais `trackList_.basculerMuet`, celui du bouton. Mesuré par un
> chemin qui n'est PAS celui du bouton — `VSM_TOUCHE="shift + M"`, qui traverse
> `keyPressed` et la table des raccourcis : sur trois pistes choisies, trois M
> rouges dans les trois contours ambre, la quatrième tranche intacte.
>
> **LA LEÇON QUI VAUT AU-DELÀ DE CE DÉFAUT.** D38 a mesuré son muet deux fois —
> par le banc et par la capture — et les deux passaient par
> `TrackListComponent::basculerMuet`. **Deux instruments braqués au même endroit
> ne valent pas mieux qu'un seul.** Ce n'est pas le nombre de mesures qui fait
> la preuve, c'est le nombre de CHEMINS qu'elles traversent.
>
> **LE RELEVÉ DISAIT « AUCUN CLIC D'EN-TÊTE » DANS L'ARRANGEMENT, ET C'EST
> FAUX.** Le clic existe depuis longtemps, appelle `onTrackSelected` et change
> la piste du piano roll. Ce qui manquait est l'autre moitié : l'arrangement
> **connaissait** la piste courante (`pisteCourante_`) et ne l'a **jamais
> dessinée**. C'est la troisième fois de la journée qu'un relevé de ma main
> annonce absent ce qui est présent sans être montré (D36.2 la lane de
> vélocité, D37.3 la couleur, ici l'en-tête). Le point commun des trois : j'ai
> cherché si la fonction EXISTE, alors que la question était si elle SE VOIT.
>
> **D39.2 — étendre au clavier.** Maj+Alt+↑/↓ agrandissent depuis l'ancre,
> Ctrl+Maj+A choisit tout. `Ctrl+A` n'était pas disponible : il veut déjà dire
> « tout sélectionner » **dans la vue qui a le focus** — les clips de
> l'arrangement, les notes du piano roll. Lui donner un troisième sens selon
> l'endroit où l'on a cliqué en dernier rendrait la touche imprévisible, et une
> touche imprévisible ne s'apprend jamais. Les pistes masquées sont sautées,
> comme pour la navigation simple (D17.4).
>
> **D39.3 et D39.4 — les trois panneaux portent la MÊME marque**, le contour
> ambre : trois codes différents pour une seule idée obligeraient à en
> apprendre trois. La liste reste seule à TENIR la sélection ; l'arrangement et
> le mélangeur la reçoivent et la dessinent. Deux endroits qui la calculeraient
> finiraient par ne pas être d'accord.
>
> **LA QUESTION ANNONCÉE AVANT LA MESURE A BIEN EU LIEU, ET ELLE SE TRANCHE
> PAR « ON NE CORRIGE PAS ».** Un dossier n'a plus de tranche depuis D35.5 : le
> choisir dans la liste n'éclaire rien au mélangeur. Lui rendre une tranche pour
> qu'elle puisse être choisie remettrait les six commandes mortes que D35.5 a
> retirées. Le mélangeur montre les chemins du signal, et un dossier n'en est
> pas un ; le dossier choisi se voit là où il vit.
>
> **D39.5 — ce que le banc peut garder, et ce qu'il ne peut pas.** Le raccourci
> vit dans `MainComponent`, qui exige le moteur audio et une fenêtre : il ne se
> monte pas dans un banc console, et **cela est écrit plutôt que contourné** —
> son chemin se vérifie à l'écran. Ce que le banc garde est ce qui a laissé
> passer D39.1 : l'apparition d'un chemin d'écriture de plus. Il relit les
> sources et compte les endroits qui posent `muted` ou `solo`, chacun nommé avec
> sa raison (le chemin unique, les boutons d'une tranche, le MIDI Learn lié à
> UNE piste nommée, le solo exclusif, le muet d'un CLIP).
>
> **ET LE BANC S'EST MIS EN DÉFAUT TOUT SEUL À SA PREMIÈRE EXÉCUTION**, ce qui
> est exactement ce qu'on lui demande : il a signalé l'arrangement, où
> `c.muted = muet` porte sur un CLIP et non sur une piste. Son repérage est
> TEXTUEL et ne distingue pas les deux. **La limite est écrite dans le banc** :
> il attrape l'apparition d'un chemin dans un fichier NEUF, pas l'ajout d'un
> chemin dans un fichier déjà nommé. Il vaut ce qu'il vaut, à condition de
> savoir laquelle des deux choses il fait.
>
> **L'ORDRE DES VARIABLES D'UN BANC FAIT PARTIE DU BANC — payé deux fois le
> même jour.** `VSM_TOUCHE` était lu avant `VSM_GESTE_PISTE` : la touche
> agissait sur la piste active d'alors, et la capture montrait une quatrième
> tranche muette **hors de la sélection** — c'est-à-dire quelque chose qui
> ressemblait trait pour trait au défaut cherché. La même faute avait été payée
> à D38 avec `VSM_GESTE_PISTE` avant `VSM_MENU`. Un banc qui se trompe d'ordre
> ne rend pas « rien » : il rend un résultat vraisemblable.
>
> **LA SÉLECTION N'EST PAS SAUVEGARDÉE**, et l'attendu n° 3 est confirmé : c'est
> un état de SESSION, comme l'armement (D3.3) et l'ordre de jeu (D18.4).
> Rouvrir un projet ne doit pas ressusciter une sélection qu'on ne se rappelle
> pas avoir posée, et l'invariant n° 5 du § 6 ne s'applique pas à ce qui ne
> décrit pas le morceau.
>
> Tests : 1 283 audio, 319 core, 285 interchange, 25 clap, 11 panels — verts ;
> banc : 11 gestes, 0 muet, 8 écritures déclarées, 0 non déclarée.

### Phase D40 — Le DAW à 64 pistes : la parité engage aussi l'application (07/09/2026, 17:45)

**Pourquoi celle-ci, et d'où elle vient.** Ce n'est pas un audit de plus : c'est
une exigence déjà écrite, et jamais vérifiée. L'objectif de parité — « si un
original comporte 15 postes, la reconstruction doit comporter 15 pistes ; s'il
en comporte 64, elle doit en comporter 64 » — porte une seconde moitié qu'on a
lue sans la mesurer : **la parité vaut à toute échelle, ce qui engage aussi le
DAW, qui doit rester utilisable et vérifié à 64 pistes, pas seulement à huit.**

Or **le plus gros projet du dépôt fait six pistes** (`reconstruction/
children-dream-v7/`), et toutes les phases D0 à D39 ont été mesurées et
photographiées sur un à quatre. Une reconstruction à parité rendra un projet
que rien n'a jamais ouvert.

**Ce que cette phase mesure, et ce qu'elle ne mesure pas.** Elle ne cherche pas
à rendre le DAW rapide : elle cherche à savoir **ce qui casse, et à partir de
combien**. Un chiffre qu'on n'a pas est plus dangereux qu'un chiffre mauvais.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D40.1 | **Un banc qui construit N pistes et chronomètre** la liste des pistes, le mélangeur, l'arrangement et la publication au moteur, à 8, 16, 32 et 64 | les quatre temps sont écrits pour chaque taille, et le banc dit lequel croît plus vite que N |
| D40.2 | **Ce qui croît en N² est nommé** — et corrigé seulement si le chiffre le demande. Un N² de 4 096 opérations triviales n'est pas un défaut, c'en est un s'il porte une allocation ou un balayage | chaque croissance super-linéaire est mesurée et tranchée par son coût réel, pas par sa forme |
| D40.3 | **Ce qui devient illisible ou inatteignable à 64 pistes.** Le mélangeur aligne les tranches : 64 × ~85 px font 5 440 px, soit six écrans. La liste défile-t-elle ? Le sélecteur de sortie propose-t-il 64 entrées ? | photographié à 64 pistes ; ce qui est inatteignable est nommé, et corrigé ou écrit |
| D40.4 | **Le moteur** : 64 pistes qui jouent ensemble, sans allocation dans `process()` et sans dépassement du budget de bloc | l'invariant n° 2 du § 6 vérifié à 64 pistes, et le temps de calcul d'un bloc mesuré |
| D40.5 | **Ce qui est décidé plutôt que corrigé.** Certaines limites sont légitimes (un écran ne montre pas 64 tranches) ; elles s'écrivent avec leur raison plutôt que de rester des surprises | la décision est écrite dans le document et dans le code |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **La construction de la liste des pistes croît plus vite que N**, parce que
   chaque ligne remplit un sélecteur d'instrument avec **tout le registre** —
   65 machines aujourd'hui. À 64 pistes cela fait 4 160 entrées de liste
   déroulante, plus une dizaine de widgets par ligne. J'attends que cela reste
   sous **une demi-seconde**, c'est-à-dire perceptible mais supportable, et je
   me trompe si c'est au-delà de deux secondes.
2. **Le mélangeur sera le premier à devenir inutilisable**, non par lenteur mais
   par largeur : je m'attends à ce que les tranches au-delà de la douzième
   soient hors de l'écran, et je ne sais pas si un défilement existe. C'est la
   prédiction que je tiens le moins fermement, et c'est pourquoi elle est
   écrite.
3. **`refreshMuteSolo` est en N²** : il parcourt les tranches et appelle
   `trackAudible` pour chacune, qui parcourt les pistes (D35.4, le muet hérité
   d'un dossier). À 64 pistes cela fait ~4 096 tours de boucle sans allocation :
   j'attends que ce soit **négligeable**, et donc à ne PAS corriger. Une forme
   en N² n'est pas un défaut ; c'est son coût qui en fait un.
4. **Le moteur tiendra**, parce que `ProcessGraph` a été mesuré à huit pistes
   et que rien dans sa structure ne dépend du nombre — mais 64 machines
   instanciées, c'est 64 fois la mémoire d'une machine, et je n'ai aucune idée
   de ce que cela pèse. C'est le chiffre que je veux le plus.

> **LA PHASE D40 EST FAITE (07/09/2026, 18:30), ET TROIS ATTENTES SUR QUATRE
> ÉTAIENT FAUSSES — DANS LE BON SENS.**
>
> **LES CHIFFRES QUI MANQUAIENT.** Projet synthétique, une machine par piste
> (`vsm.minimoog`), 32 notes par piste :
>
> | pistes | liste (ms) | mélangeur (ms) | un bloc de 512 (ms) | du budget |
> |---|---|---|---|---|
> | 8 | 0,5 | 0,4 | 0,280 | 2,6 % |
> | 16 | 0,7 | 0,8 | 0,555 | 5,2 % |
> | 32 | 1,2 | 1,6 | 1,123 | 10,5 % |
> | 64 | **2,5** | **3,2** | **2,229** | **20,9 %** |
>
> Tout est **linéaire** (×2,03 et ×2,02 au dernier doublement). **Le DAW tient
> 64 pistes, et il les tient largement.**
>
> **L'ATTENTE N° 1 ÉTAIT FAUSSE D'UN FACTEUR DEUX CENTS.** J'annonçais une
> construction super-linéaire « sous une demi-seconde » parce que chaque ligne
> remplit un sélecteur avec les 64 machines du registre — 4 096 entrées à
> 64 pistes. Mesuré : **2,5 ms**, et linéaire. Mon modèle de ce qui coûte cher
> était faux de deux ordres de grandeur, et aucune lecture de code ne me
> l'aurait dit : seule la mesure fixe l'échelle.
>
> **L'ATTENTE N° 3 ÉTAIT JUSTE, ET SA CONCLUSION AUSSI.** `refreshMuteSolo` est
> bien en N² (chaque tranche appelle `trackAudible`, qui parcourt les pistes) :
> **0,037 ms** à 64 pistes, soit un quatre-centième de ce qu'un glissé de fader
> peut dépenser entre deux images. **Une forme en N² n'est pas un défaut ; c'est
> son coût qui en fait un.** Elle n'est pas corrigée, et c'est écrit.
>
> **L'ATTENTE N° 2 ÉTAIT À MOITIÉ FAUSSE, ET C'EST CELLE QUE JE TENAIS LE MOINS
> FERMEMENT.** Le mélangeur défile bien — il montre treize tranches sur 64. Mais
> le vrai défaut était ailleurs, et l'échelle l'a fait apparaître : **choisir la
> piste 41 dessinait son contour ambre sur une tranche hors de l'écran.** La
> marque de sélection que D39.4 venait d'ajouter existait et ne se voyait pas —
> le défaut même que D38.1 et D39.3 avaient corrigé ailleurs, revenu par le
> nombre. La liste des pistes avait `faireVoirLaPiste` depuis longtemps, et son
> commentaire annonçait déjà le cas (« un projet en parité en a onze ») ; le
> mélangeur n'avait pas son jumeau. Il l'a, par `trackIndex()` et non par le
> rang de la tranche — un dossier n'en a plus depuis D35.5.
>
> **LE BANC A RENDU UN CHIFFRE IMPOSSIBLE, ET C'EST SON IMPOSSIBILITÉ QUI L'A
> DÉNONCÉ.** Première exécution : **0,002 ms par bloc à 64 pistes**, soit deux
> microsecondes pour 512 échantillons de soixante-quatre Minimoog. Le moteur
> n'était pas en lecture : il ne déclenchait aucune note, et le banc mesurait
> soixante-quatre machines au repos. **Un chiffre trop beau se vérifie avant de
> se publier** — et la parade est écrite dans le banc, qui relève désormais la
> CRÊTE de sortie et dit « SILENCE : LE BANC NE MESURE RIEN » si elle est nulle.
> Une mesure de performance sans preuve que le calcul a eu lieu ne mesure que
> l'absence de calcul.
>
> **UNE OBSERVATION, AVEC SA RÉSERVE.** À 64 pistes la crête de sortie atteint
> **4,05** — le mélange écrête largement. **Ce n'est pas un défaut du DAW mais
> une propriété du banc** : mes 64 pistes jouent le même motif dense à la même
> vélocité sur la même machine, ce qu'aucun morceau réel ne fait. Ce que le
> chiffre dit tout de même : **rien dans la chaîne n'empêche la somme
> d'écrêter**, et une reconstruction à parité en aura soixante-quatre
> contributeurs au lieu de quatre. À vérifier sur un vrai morceau à parité —
> pas ici, où le témoin ne ressemble à rien.
>
> **CE QUI N'EST PAS MESURÉ, ET C'EST DIT.** Le banc ne pèse pas la MÉMOIRE des
> 64 machines instanciées — c'était l'autre moitié de l'attente n° 4, et un
> compteur d'allocations demanderait d'instrumenter le moteur. Le montage des
> 64 machines prend 0,7 ms, ce qui exclut au moins qu'il fasse quelque chose de
> lourd. Le chiffre de mémoire reste à prendre. **— PRIS PAR D41** : 120
> machines vivantes tiennent dans 33 Mo, 13 Mo au-dessus du processus vide.
> (Renvoi ajouté le 09/09/2026 : sans lui, cette ligne fait rechercher un
> chiffre déjà publié deux phases plus loin.)
>
> Vérifié à l'écran à 64 pistes (63 « Ajouter une piste MIDI » par `VSM_MENU`) :
> la liste défile, l'arrangement défile, le mélangeur défile — et, après
> correction, la tranche de la piste choisie vient se montrer.
>
> Tests : 1 283 audio, 319 core, 285 interchange, 25 clap, 11 panels — verts ;
> banc d'édition : 11 gestes, 0 muet, 0 désaccord.

### Phase D41 — « Il les tient largement » était vrai d'UNE machine sur soixante-quatre (07/09/2026, 19:00)

**CETTE PHASE COMMENCE PAR CORRIGER CELLE D'AVANT.** La conclusion de D40 dit :
« Le DAW tient 64 pistes, et il les tient largement », sur la foi de 2,229 ms
par bloc, soit 20,9 % du budget. **Ce chiffre est celui du Minimoog**, et le
banc ne mesurait que lui. Rejoué sur d'autres machines, une option de ligne de
commande plutôt qu'une constante éditée :

| machine | un bloc de 512 à 64 pistes | du budget temps réel |
|---|---|---|
| `vsm.minimoog` | 2,27 ms | 21,3 % |
| `vsm.granular` | 3,66 ms | 34,3 % |
| `vsm.pipeorgan` | 7,72 ms | 72,3 % |
| **`vsm.cs80`** | **10,79 ms** | **101,2 %** |

**À 64 pistes de CS-80, le moteur ne tient plus.** Le facteur entre la machine
la plus légère et la plus lourde est de **cinq**, et D40 a généralisé depuis la
plus légère. C'est exactement la faute que D39 venait de nommer — mesurer un
seul chemin et conclure — commise à la phase suivante, sur un autre objet.

**LA MÉMOIRE, ELLE, N'EST PAS UN PROBLÈME, et c'est le chiffre que D40 avait
annoncé ne pas prendre** : **120 machines vivantes tiennent dans 33 Mo**, soit
13 Mo au-dessus du processus vide, et le coût marginal par machine DÉCROÎT
(0,40 puis 0,06 Mo) — ce qui dit qu'on mesure la granularité du tas plutôt
qu'un poids par machine. Identique pour les quatre machines essayées.

**ET LE TÉMOIN QUI DIRAIT TOUT CELA À L'UTILISATEUR EST LE PREMIER QU'ON
EFFACE.** `TransportBarComponent` affiche une charge CPU — et la pose **en
dernier, et seulement si elle tient** : « à droite, par ordre d'importance :
exporter, ouvrir, puis les deux étiquettes seulement si elles tiennent ». À la
largeur de fenêtre de tous les autoportraits de ce document, elle est
**invisible**. Le seul indicateur qui dise si le morceau va jouer disparaît
avant deux boutons qui, eux, ont un menu.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D41.1 | **Le coût de CHAQUE machine, mesuré et publié.** Le parc en compte 64 ; on n'en connaît le prix d'aucune. C'est une donnée pour le DAW *et* pour la reconstruction, qui choisit les machines qu'elle assigne | une table des 64, à polyphonie et bloc fixés, du moins cher au plus cher |
| D41.2 | **La charge se voit quand elle compte.** Elle ne doit pas être la première rognée ; et un nombre gris ne dit pas le danger | la charge reste affichée, et **change d'aspect** au-delà d'un seuil ; vérifié à l'écran, à la largeur où elle disparaissait |
| D41.3 | **Un décrochage est un fait, pas une impression.** La moyenne de JUCE lisse ce qui compte : un bloc en retard s'entend et ne se voit pas | ou bien l'application compte les blocs en retard et le DIT, ou bien il est écrit pourquoi elle ne le peut pas |
| D41.4 | **Le banc garde la table.** Une machine ajoutée demain doit entrer dans la mesure sans qu'on y pense — c'est la discipline que `regression_every_registered_machine_has_a_reference` applique déjà aux empreintes | le banc parcourt le REGISTRE, pas une liste écrite à la main |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **D41.1** — j'attends un rapport d'au moins **dix** entre la machine la
   moins chère et la plus chère, et j'attends que les plus chères soient celles
   qui empilent des voix complètes : le CS-80 (deux couches par voix, écrit dans
   son CDC), l'orgue à tuyaux (une soufflerie commune et des tuyaux qui
   parlent), les modèles physiques (`vsm.string`, `vsm.modal`, `vsm.scanned`).
   Je me trompe si l'écart est inférieur à cinq, ou si le haut du classement est
   occupé par des soustractifs ordinaires.
2. **D41.2** — j'attends de devoir choisir quoi rogner à sa place, et le
   candidat est la fréquence d'échantillonnage : elle ne change jamais en cours
   de séance, alors que la charge change à chaque note. Une étiquette qui ne
   varie pas n'a pas besoin d'être sous les yeux.
3. **D41.3** — je ne sais pas si JUCE expose un compteur de décrochages, et je
   m'attends à devoir écrire que non plutôt qu'à en trouver un. Si c'est le
   cas, l'honnêteté est de le dire et de mesurer ce qui est mesurable — la
   charge crête plutôt que moyenne — au lieu d'inventer un chiffre qui aurait
   l'air d'un compte.

> **LA PHASE D41 EST FAITE (07/09/2026, 19:55), ET ELLE CORRIGE D40 SUR LE
> POINT QUI COMPTAIT.**
>
> **D41.1 — les 64 machines du parc, mesurées.** Rapport de **42** entre la
> moins chère et la plus chère, à 16 pistes et bloc de 512 :
>
> | | machine | un bloc | du budget |
> |---|---|---|---|
> | la moins chère | `vsm.testtone` | 0,12 ms | 1,1 % |
> | | `vsm.tb303` | 0,40 ms | 3,7 % |
> | | `vsm.cs80` | 1,44 ms | 13,5 % |
> | | `vsm.dx7` | 1,70 ms | 15,9 % |
> | | `vsm.divider` | 2,56 ms | 24,0 % |
> | | `vsm.plate` | 2,66 ms | 25,0 % |
> | **la plus chère** | **`vsm.additive`** | **3,23 ms** | **30,3 %** |
>
> **MON PRONOSTIC SUR *LESQUELLES* ÉTAIT FAUX.** J'attendais en tête les
> machines qui empilent des voix complètes — le CS-80 et ses deux couches,
> l'orgue à tuyaux. Mesurés : **13,5 %** et **12,5 %**, en milieu de tableau. Le
> haut est occupé par ce qui somme beaucoup d'oscillateurs ou fait tourner un
> modèle physique : l'additif, la plaque, le diviseur, le modal. L'écart
> annoncé (« au moins dix ») était juste ; la raison que je lui donnais ne
> l'était pas.
>
> **TROIS MACHINES NE JOUENT PAS SANS ÉCHANTILLONS** — `vsm.sampler`,
> `vsm.perc`, `vsm.multisample` — et le banc l'ÉCRIT (« SILENCE : ne joue pas,
> prix non mesuré ») au lieu de publier leur prix de repos comme s'il était
> leur prix.
>
> **ET L'EXTRAPOLATION ÉTAIT FAUSSE, DANS LE SENS RASSURANT.** Multiplier par
> quatre le prix de seize pistes donnait 100 % du budget pour `vsm.plate` ; la
> mesure directe à 64 pistes en rend **258 %**. Le coût ne suit pas le nombre de
> pistes, parce que le nombre de VOIX simultanées ne le suit pas non plus. Le
> banc remesure donc la plus chère à 64 pistes au lieu de multiplier :
> `vsm.additive` y coûte **13,3 ms, soit 125 % du budget — le moteur ne tient
> plus**.
>
> **CE QUE DEVIENT LA CONCLUSION DE D40.** « Le DAW tient 64 pistes, et il les
> tient largement » est vrai du Minimoog (21 %) et **faux** du CS-80 (108 %), de
> l'additif (125 %) et de la plaque (258 %). La phrase juste est : **le DAW tient
> 64 pistes des machines légères, et le choix de la machine pèse plus lourd que
> le nombre de pistes.** C'est la faute que D39 venait de nommer — mesurer un
> seul chemin et conclure — refaite à la phase suivante sur un autre objet.
>
> **LA MÉMOIRE N'EST PAS UN PROBLÈME**, et c'était le chiffre annoncé manquant :
> **120 machines vivantes tiennent dans 33 Mo**, 13 Mo au-dessus du processus
> vide, identique pour les quatre machines essayées. Le coût marginal DÉCROÎT
> (0,40 puis 0,06 Mo), ce qui dit qu'on mesure la granularité du tas plutôt
> qu'un poids par machine.
>
> **D41.2 — la charge se voit, et elle change d'aspect.** Elle était posée en
> dernier, « seulement si elle tient », et n'apparaissait sur AUCUN des
> autoportraits de ce document. Elle passe maintenant avant les deux boutons —
> qui ont chacun leur entrée de menu — et avant la fréquence d'échantillonnage,
> **qui est la bonne chose à rogner : elle ne change jamais en cours de séance,
> alors que la charge change à chaque note.** Trois états plutôt que deux : gris
> sous 70 %, ambre entre 70 et 90, rouge au-delà — parce qu'un témoin qui
> n'alerte qu'une fois le mal fait arrive trop tard. **Le prix est dit** : à
> 1 264 px, « Ouvrir MIDI… » cède la place ; il reste au menu Fichier.
>
> **D41.3 — MON ATTENTE ÉTAIT FAUSSE, ET C'EST TANT MIEUX.** J'annonçais devoir
> écrire que JUCE n'expose pas de compteur de décrochages. **Il en expose un** :
> `AudioDeviceManager::getXRunCount()`, qui interroge le pilote quand celui-ci
> sait répondre. La barre affiche « 3 craquements » **et seulement s'il y en
> a** — un compteur à zéro en permanence devient un meuble qu'on ne lit plus, et
> c'est le jour où il change qu'il faut le voir. Il passe avant la charge : **la
> charge dit un risque, le compte dit un dégât déjà fait.** Le cas « le pilote
> ne sait pas le dire » (-1) se distingue de « aucun » (0) et n'affiche rien
> plutôt qu'un zéro rassurant.
>
> **D41.4 — le banc parcourt le REGISTRE**, pas une liste écrite à la main :
> une machine ajoutée demain entre dans la table sans qu'on y pense, comme
> `regression_every_registered_machine_has_a_reference` l'impose déjà aux
> empreintes.
>
> Tests : 1 283 audio, 319 core, 285 interchange, 25 clap, 11 panels — verts ;
> banc d'édition : 11 gestes, 0 muet, 0 désaccord.

> **CORRECTION DE D41, ÉCRITE LE JOUR MÊME (07/09/2026, 20:30) : L'ALARME
> ÉTAIT UN ARTEFACT DU BANC.** Elle est corrigée ici plutôt qu'effacée, parce
> que c'est la **troisième fois de la journée** que la même faute est commise,
> et que sa répétition est plus instructive que le chiffre.
>
> **CE QUE LE BANC NE FAISAIT PAS.** Il ne réglait pas les fils de rendu. La
> réserve (`RenderThreadPool`) restait à **zéro travailleur**, et le moteur
> calculait tout sur un cœur — alors que l'application met ce réglage sur
> « automatique » depuis D8, c'est-à-dire jusqu'à **huit** fils, un plafond
> lui-même choisi par une mesure (×3,70 sur le p99 à huit, contre ×1,80 à
> douze). **Le banc mesurait une configuration que le logiciel n'emploie
> jamais.**
>
> **LES VRAIS CHIFFRES, À 64 PISTES, TÉMOIN ET MESURE SORTIS DU MÊME BINAIRE**
> (les fils sont devenus une option de ligne de commande, pas une constante) :
>
> | machine | 1 cœur (ce que D41 a publié) | 8 fils (ce que le logiciel fait) |
> |---|---|---|
> | `vsm.minimoog` | 2,24 ms — 21,0 % | **0,58 ms — 5,5 %** |
> | `vsm.cs80` | 10,70 ms — 100,3 % | **2,79 ms — 26,2 %** |
> | `vsm.additive` | 15,55 ms — 145,8 % | **3,05 ms — 28,6 %** |
> | `vsm.plate` | 27,37 ms — 256,6 % | **5,71 ms — 53,5 %** |
>
> **Le DAW tient 64 pistes de N'IMPORTE LAQUELLE des machines mesurées**, et la
> plus chère laisse encore la moitié du budget. Le rendu parallèle s'engage bien
> en lecture — 201 portées parallèles sur 200 blocs —, ce que le compteur
> `parallelSpansRendered()` dit désormais dans le rapport du banc.
>
> **CE QUI RESTE VRAI DE D41.** Le rapport de **42** entre la machine la moins
> chère et la plus chère ne dépend pas du nombre de fils, et il reste la donnée
> utile : elle vaut pour le DAW comme pour la reconstruction, qui choisit les
> machines qu'elle assigne. Le témoin de charge rendu visible, ses trois états
> et le compte de craquements valent indépendamment de ces chiffres — un bloc
> arrive en retard au-delà de 90 %, quelle que soit la machine qui a consommé le
> temps. Ce que la correction change est l'idée qu'on s'en fait : **on n'atteint
> ces seuils qu'avec des inserts, des effets et un projet chargé, pas avec
> 64 pistes nues.**
>
> **LA MÊME FAUTE, TROIS FOIS, ET SA FORME EST CHAQUE FOIS LA MÊME.** D39 :
> deux instruments braqués sur le même chemin de code. D41 : une seule machine
> mesurée, conclusion pour toutes. Ici : une seule configuration du moteur,
> conclusion pour le logiciel. **À chaque fois j'ai mesuré quelque chose de
> réel, et j'en ai tiré une phrase plus large que ce que la mesure couvrait.**
> Ce n'est pas un défaut d'instrument, c'est un défaut de portée.
>
> **LA RÈGLE QUI EN SORT, ET ELLE EST PLUS ÉTROITE QU'UNE BONNE RÉSOLUTION.**
> *Un banc qui mesure le moteur doit le CONFIGURER comme l'application le
> configure, ou dire dans son rapport quelle configuration il mesure.* Le banc
> imprime désormais `fils=8 portées//=201` à chaque ligne : le lecteur voit du
> même coup ce qui a été mesuré et si le chemin parallèle a servi. Un rapport
> qui tait sa configuration laisse croire qu'il n'y en avait qu'une.

### Phase D42 — La charge PAR PISTE : à 64 pistes, le total ne dit pas quoi geler (07/09/2026, 20:45)

**Ce que D41 a laissé à moitié fait.** La barre affiche désormais « CPU 92 % »
et le nombre de craquements. C'est un progrès sur rien — mais devant un projet
en parité, **le total est inutilisable** : il dit qu'il faut alléger, pas QUOI
alléger. L'infobulle conseille « Piste ▸ Geler la piste » sans dire laquelle, et
D41 a mesuré un rapport de **42** entre la machine la moins chère et la plus
chère : sur 64 pistes, une seule peut coûter ce que quarante autres coûtent
ensemble.

C'est le geste de tous les jours dans Cubase (*Performance Meter* par piste) et
dans Live : on regarde la colonne, on gèle la piste qui dépasse, on continue.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D42.1 | **Le moteur mesure le temps passé par piste**, publié comme les vumètres — banque de taille fixe, atomiques, aucune allocation | l'invariant n° 2 du § 6 tient, vérifié par le banc « aucune allocation dans `process()` » |
| D42.2 | **Les deux chemins de rendu sont couverts**, le séquentiel et le parallèle : c'est le second qui porte les 64 pistes, et l'oublier mesurerait le cas qui n'arrive pas | le banc dit, pour chaque piste, un temps non nul dans les deux configurations |
| D42.3 | **Le chiffre s'affiche là où l'on choisit quoi geler** : la tranche du mélangeur | photographié à 64 pistes, avec une piste chère et des pistes légères |
| D42.4 | **Ce que ce chiffre NE dit pas est écrit** | voir l'attendu n° 3, qui est le piège de cette phase |

**Ce qui est attendu, écrit AVANT la mesure.**

1. **La somme des temps par piste sera INFÉRIEURE au temps du bloc** en rendu
   séquentiel, parce que le bloc contient aussi le mixage, le bus master et ce
   qui n'appartient à aucune piste. J'attends un écart visible — dix à trente
   pour cent. **Si la somme par piste égale le temps du bloc, c'est que je
   mesure autre chose que ce que je crois.**
2. **Le coût de la mesure elle-même** : deux lectures d'horloge par piste et par
   bloc, soit 128 à 64 pistes. J'attends **moins de 1 %** du budget. Au-delà de
   cinq, l'instrument coûte plus qu'il n'informe et doit devenir facultatif ou
   disparaître — un mètre qui fait craquer ce qu'il mesure est pire qu'aucun
   mètre.
3. **LE PIÈGE, ÉCRIT MAINTENANT POUR NE PAS Y TOMBER.** En rendu parallèle, huit
   pistes sont calculées **en même temps** : chacune rapporte sa propre durée, et
   leur somme peut donc dépasser la durée du bloc. Ce n'est pas une erreur,
   c'est ce que « huit fils » veut dire. **Un affichage en « pourcentage du
   bloc » montrerait donc 300 % et mentirait.** Le chiffre par piste est un
   temps, et il se compare aux AUTRES PISTES — pas au budget. C'est exactement
   la faute de portée commise trois fois aujourd'hui, prévue d'avance cette
   fois-ci.

> **LA PHASE D42 EST FAITE (07/09/2026, 21:30). UNE ATTENTE JUSTE, UNE FAUSSE,
> ET UNE VÉRIFIÉE PAR UNE AUTRE MÉTHODE QUE LA PRÉVUE.**
>
> **D42.1 et D42.2 — le chronomètre est dans `renderTrackVoice`, et nulle part
> ailleurs.** Les deux chemins de rendu — le séquentiel et le parallèle qui
> porte les 64 pistes — passent tous deux par cette fonction : les couvrir tous
> les deux est une conséquence de l'endroit choisi, pas une chose dont il faut
> se souvenir. Un **garde RAII** plutôt qu'un appel à chaque `return` : la
> fonction en compte une dizaine, et le onzième qu'on ajoutera oublierait le
> chronomètre sans que rien ne le dise — la piste afficherait alors un coût
> figé, c'est-à-dire un chiffre plausible et faux. Les **neuf** tests « aucune
> allocation dans `process()` » passent, balayage du parc entier compris :
> l'invariant n° 2 du § 6 tient.
>
> **L'ATTENTE N° 3 ÉTAIT JUSTE, ET C'ÉTAIT LE PIÈGE.** En rendu parallèle, la
> somme des temps par piste vaut **301 %** de la durée du bloc — huit pistes
> calculées ensemble rapportent chacune sa propre durée. Écrit avant la mesure,
> donc l'affichage ne présente jamais ce temps comme une part du budget : il se
> compare aux **autres pistes**, et c'est dit dans l'en-tête de `MeterBank`, seul
> endroit que lira qui voudra s'en servir ailleurs.
>
> **L'ATTENTE N° 1 ÉTAIT FAUSSE, ET SON GARDE-FOU L'ÉTAIT AUSSI.** J'annonçais
> une somme par piste inférieure de 10 à 30 % au temps du bloc, et j'avais
> écrit : « si la somme égale le temps du bloc, c'est que je mesure autre chose
> que ce que je crois ». Mesuré en séquentiel : **98 %**. Le garde-fou aurait
> donc dû condamner l'instrument — **et il avait tort**, parce qu'il supposait
> un travail hors pistes important. Avec un bus master vide et aucun départ, le
> hors-piste vaut **0,019 ms sur 0,803**, soit 2,4 % : un mélange de seize
> pistes est une addition pondérée, et un master sans effet est un passage.
> **Ce qui départage les deux hypothèses n'est pas la somme mais la
> DISPERSION** : les pistes diffèrent entre elles d'un facteur 7,5 à 12,6, ce
> qu'un chronomètre posé par erreur sur quelque chose de partagé ne pourrait pas
> produire — il rendrait seize valeurs identiques, et une somme de seize fois le
> bloc, pas de 0,98 fois.
>
> **L'ATTENTE N° 2 EST CONFIRMÉE, MAIS PAS PAR LA MÉTHODE PRÉVUE, ET C'EST LA
> LEÇON DE CETTE PHASE.** Comparer le temps du bloc avant et après
> l'instrumentation donnait **+10,8 %** — au-dessus des 5 % où j'avais écrit que
> l'instrument devrait disparaître. Cinq exécutions du même banc rendent 0,545 ;
> 0,673 ; 0,644 ; 0,674 ; 0,741 ms : **le bruit est de ±18 %, et la mesure
> d'avant tombe dedans.** Une différence de 1 % ne se mesure pas par la
> différence de deux nombres bruités ; elle se mesure **directement** : 128
> lectures d'horloge (deux par piste, 64 pistes) prennent **0,0020 ms, soit
> 0,02 % du budget**. J'ai failli condamner un instrument correct sur une
> comparaison que le bruit rendait vide de sens.
>
> **D42.3 — le nom en ambre, le chiffre dans l'infobulle.** Une console est
> étroite ; y glisser un nombre de plus aurait demandé une police plus petite,
> exactement ce que l'échelle à 150 % existe pour éviter. **Le seuil est
> relatif : trois fois la MÉDIANE**, et non un seuil absolu — un projet de
> quatre flûtes n'a pas de piste chère, et un projet de soixante-quatre additifs
> en aurait soixante-quatre. La médiane et non la moyenne : une seule piste très
> chère tire la moyenne au point de se cacher derrière son propre seuil.
>
> **CE QUI N'A PAS PU ÊTRE PHOTOGRAPHIÉ, ET POURQUOI — DIT PLUTÔT QUE
> CONTOURNÉ.** Le marquage dépend de blocs réellement rendus. Sur cette machine
> le périphérique audio est **pris par un autre processus** (`aplay -l` :
> « Sous-périphériques : 0/1 »), le moteur ne rend aucun bloc pendant une
> capture, et toutes les pistes y coûtent zéro — trois captures successives l'ont
> montré avant que j'en cherche la cause ailleurs. La **règle** est donc mesurée
> au banc, qui relit la décision (`pistesCheres()`) au lieu de la décrire : sur
> 38, 41, 500, 39, 42 µs elle désigne la seule piste à 500 ; sur des coûts
> voisins elle n'en désigne **aucune**. Ce qui reste non photographié est le
> pixel ambre, pas la décision.
>
> **ET LE PREMIER BANC DE D42.3 NE MESURAIT RIEN** : il imprimait les coûts
> d'entrée et annonçait ce qui devait arriver, sans relire ce qui était arrivé.
> Un banc qui décrit sa propre attente la confirme toujours.
>
> Tests : 1 283 audio, 319 core, 285 interchange, 25 clap, 11 panels — verts ;
> banc d'édition : 11 gestes, 0 muet, 0 désaccord.

### Phase D43 — Le moteur note pourquoi il n'y a pas de son, et personne ne le lit (07/09/2026, 21:50)

**Trouvé en cherchant autre chose.** D42 n'a pas pu photographier son marquage
parce qu'aucun bloc n'était rendu ; la cause s'est révélée être le périphérique
audio, **pris par un autre processus** (`aplay -l` : « Sous-périphériques :
0/1 »). En remontant ce fil, on tombe sur ceci :

```cpp
    if (error.isNotEmpty()) {
        lastError_ = error;
        return; // pas de device : l'app reste utilisable, juste sans son
    }
```

`AudioEngine::lastError()` existe, est publique, et **n'est appelée nulle
part**. Le moteur écrit soigneusement la raison pour laquelle il n'y aura pas
de son, et personne ne la lit. Ce que l'utilisateur obtient est une application
qui s'ouvre normalement, dont tous les boutons répondent, et qui **ne fait
aucun bruit** — sans un mot.

**C'EST L'INTERDIT LE PLUS EXPLICITE DU PROJET**, appliqué à son cas le plus
grave. La règle dit : « ce qui est écarté, ignoré ou remplacé est DIT ». Ici ce
qui est écarté est le son lui-même. Et l'on ne parle pas d'un cas de
laboratoire : il se produit **sur cette machine, en ce moment**.

**CE QUI EST DÉJÀ BIEN, ET QU'IL NE FAUT PAS CASSER.** Le repli est juste : une
application qui refuserait de démarrer sans carte son serait inutilisable pour
éditer, mixer et exporter — ce que `vsm-render` fait très bien sans périphérique.
Le défaut n'est pas de continuer, il est de continuer **en silence**.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D43.1 | **La raison se lit.** `lastError()` cesse d'être une fonction que personne n'appelle : ce qu'elle contient s'affiche | l'application sans carte son dit qu'elle est sans son, **et pourquoi**, avec le texte du pilote |
| D43.2 | **Là où l'on peut agir**, pas dans une boîte à fermer : le réglage existe (*Fichier ▸ Réglages audio…*) et le message doit y mener | le témoin est permanent tant que le son manque, et nomme le geste |
| D43.3 | **Le repli reste entier** : rien de ce qui marche sans son ne doit se mettre à exiger une carte | l'édition, le mixage et l'export continuent de fonctionner sans périphérique |
| D43.4 | **Et l'inverse : quand le son revient, le témoin part.** Une carte peut apparaître en cours de séance (`refreshArmedTracks` le sait déjà) | branché/débranché en cours de route, le témoin suit |

**Ce qui est attendu, écrit AVANT la mesure.**

1. J'attends que `isDeviceOpen()` soit **faux** sur cette machine et que
   `lastError()` porte un texte venu d'ALSA. **Si `lastError()` est VIDE alors
   que le périphérique est fermé**, il y a un second défaut : le repli
   `initialise(0, 2, …)` écrase l'erreur du premier essai par la sienne, ou par
   rien — et l'application n'aurait alors même pas de raison à donner.
2. **L'inversion de cette phase, et elle est plaisante à écrire** : d'ordinaire
   le cas sain se photographie et le cas de panne se raisonne. Ici c'est
   l'inverse — la panne est l'état de cette machine, et c'est **le bon
   fonctionnement** qui sera le plus difficile à montrer. Je m'attends donc à
   photographier le témoin allumé sans effort, et à devoir libérer la carte pour
   vérifier qu'il s'éteint.

> **CORRECTION DE D42, ÉCRITE AVANT D'ALLER PLUS LOIN (07/09/2026, 22:10) :
> LA CAUSE PUBLIÉE ÉTAIT FAUSSE.** D42 explique que son marquage n'a pas pu être
> photographié parce que « le périphérique audio est PRIS par un autre
> processus », en citant `aplay -l` (« Sous-périphériques : 0/1 »).
>
> **C'est faux, et une mesure d'une ligne le dit** : `isDeviceOpen=1`. Le
> périphérique s'ouvre parfaitement sur cette machine ; la carte `default`
> passe par dmix, que l'occupation de `hw:0,0` ne bloque pas. J'ai lu une
> sortie d'`aplay`, j'y ai trouvé une explication plausible à ce que je voyais,
> et je l'ai publiée sans la vérifier — alors que la vérifier tenait en un
> `fprintf`.
>
> **LA VRAIE RAISON, MESURÉE.** Les coûts par piste sont bel et bien publiés :
> `0:65,9 µs 1:59,8 µs 2:48,2 µs 3:9,6 µs`. Mais le transport ne JOUAIT pas
> (`lecture=0`), les quatre pistes ne portaient donc que le coût d'un
> instrument au repos, et elles se valaient à quelques microsecondes près.
> **Aucune ne dépassait trois fois la médiane, donc aucune n'était désignée —
> ce qui est exactement ce que la règle doit faire.** Le marquage n'était pas en
> panne : il n'avait rien à désigner. Ce que je prenais pour l'absence d'une
> fonction était la fonction en train de répondre « rien à signaler ».
>
> **CE QUI RESTE VRAI DE D42** : la mesure par piste, le garde RAII, les neuf
> tests d'allocation, les trois attentes et leur bilan, la règle relue au banc.
> Seule la phrase sur le périphérique était fausse.

> **LA PHASE D43 EST FAITE (07/09/2026, 22:20), ET SA PRÉMISSE A ÉTÉ CORRIGÉE
> EN COURS DE ROUTE.**
>
> Le tableau d'ouverture annonçait que le défaut « se produit sur cette machine,
> en ce moment ». **C'était faux**, hérité de l'erreur de D42 corrigée
> ci-dessus. Le défaut, lui, est bien réel — et il se trouve par la LECTURE,
> pas par l'observation : `AudioEngine::lastError()` est publique, documentée,
> et **n'a aucun appelant** dans tout l'arbre. Le moteur écrit soigneusement
> pourquoi il n'y aura pas de son, et rien ne le lit.
>
> **CE QUE CELA VAUT, MÊME SANS L'AVOIR VU.** Une application qui s'ouvre, dont
> tous les boutons répondent, et qui ne fait aucun bruit sans un mot, est le cas
> le plus grave de l'interdit le plus explicite du projet : « ce qui est écarté,
> ignoré ou remplacé est DIT ». Ce qui est écarté ici est le son. Le repli est
> juste — éditer, mixer et exporter n'ont pas besoin de carte —, et le défaut
> n'était pas de continuer mais de continuer **en silence**.
>
> **CE QUI EST FAIT.** Un témoin « SANS SON » en rouge, permanent tant que le
> son manque, **avant les craquements et avant la charge** — une charge et un
> compte de craquements n'ont aucun sens quand rien ne sort. Son infobulle
> donne **le texte du pilote tel quel** : « ALSA : device or resource busy » se
> cherche dans un moteur de recherche, « le son n'est pas disponible » ne se
> cherche pas. Et elle nomme le geste : *Fichier ▸ Réglages audio…*.
>
> **CE QUE LA CAPTURE A PU MONTRER, ET CE QU'ELLE N'A PAS PU.** Le cas sain est
> photographié : le périphérique s'ouvre, aucun témoin — ce qui prouve l'absence
> de fausse alerte, et rien de plus. **Deux tentatives pour provoquer la panne
> ont échoué** : occuper `hw:0,0` (dmix le contourne) et blanchir
> `ALSA_CONFIG_PATH` (JUCE le surmonte). La branche que la capture n'atteint pas
> est donc mesurée au banc, qui construit la barre et relit la visibilité du
> témoin : caché au départ, **visible** quand on lui donne une raison, **caché à
> nouveau** quand le son revient (D43.4). Trois états, trois lectures.
>
> **L'ATTENTE N° 2 ÉTAIT FAUSSE, ET DE FAÇON INSTRUCTIVE.** J'annonçais que la
> panne serait facile à photographier et le bon fonctionnement difficile,
> puisque la machine était censée être en panne. C'est l'inverse qui s'est
> produit — parce que la prémisse elle-même était fausse. **Une attente écrite
> avant la mesure ne protège pas d'une prémisse fausse ; elle la rend
> seulement visible plus tôt.** Ici elle l'a rendue visible au premier
> `fprintf`, avant que quoi que ce soit ne soit bâti dessus.
>
> Tests : 1 283 audio, 319 core, 285 interchange, 25 clap, 11 panels — verts ;
> banc d'édition : 11 gestes, 0 muet, 0 désaccord.

> **D42.3, AFFINÉ PAR CE QUE LA CHASSE À LA CAPTURE A TROUVÉ (07/09/2026,
> 22:50).** En cherchant à photographier le marquage ambre, j'ai construit le
> cas qui devait l'allumer : la piste de démo dupliquée trois fois, la
> quatrième passée en `vsm.additive` — la machine la plus chère du parc,
> **5,8 fois** le Minimoog en jeu (3,197 ms contre 0,555 à seize pistes). Le
> marquage ne s'est pas allumé. Trois causes plausibles se présentaient ; au
> lieu d'en choisir une, une trace les a départagées :
>
> ```
> [couts] lecture=0 0=74,8 1=82,9 2=83,9 3=16,3 us
> ```
>
> **AU REPOS, L'ADDITIVE EST LA MOINS CHÈRE DES QUATRE** — 16,3 µs contre 75 à
> 84. Une machine sans voix active ne somme rien, tandis qu'un soustractif fait
> tourner ses oscillateurs et son filtre quoi qu'il arrive. **Le classement à
> l'arrêt est non seulement inutile : il est INVERSÉ.**
>
> **CONSÉQUENCE, ET C'EST UNE CORRECTION DU CODE, PAS UNE NOTE.** Recalculer la
> désignation à l'arrêt revenait à désigner une piste au hasard dès qu'on appuie
> sur Stop — et à désigner la MOINS chère aussi souvent qu'une autre.
> `publishRenderCosts` ne recalcule donc plus qu'en LECTURE : ce qu'on lit à
> l'arrêt est la dernière désignation qui voulait dire quelque chose.
>
> **ET LE PIXEL AMBRE RESTE NON PHOTOGRAPHIÉ**, cette fois pour une raison
> précise et non pour une raison inventée : la capture doit tomber **pendant**
> la lecture, et le morceau de démo dure **1,85 s** — le même chiffre qui avait
> déjà piégé D27.5 avec `aseqdump`. Trois réglages de délai (7 000, 1 200 et
> 900 ms) n'ont pas attrapé la fenêtre. La règle, elle, reste mesurée au banc
> dans les deux sens.
>
> **CE QUE CETTE POURSUITE A COÛTÉ ET RAPPORTÉ.** Elle a coûté beaucoup de
> tentatives pour une image que je n'ai pas. Elle a rapporté un défaut réel que
> ni le banc ni la lecture du code n'auraient montré — **parce qu'il fallait
> comparer des machines en jeu et à l'arrêt pour le voir**. La leçon n'est pas
> « il faut insister » : c'est qu'une mesure qui refuse de donner le résultat
> attendu a quelque chose à dire, et que la trace qui départage trois
> hypothèses coûte moins cher que la quatrième tentative de deviner laquelle.

### Phase D44 — Geler une piste : le conseil que l'application donne, mesuré (07/09/2026, 23:15)

**UNE ENTORSE À LA MÉTHODE, DITE PLUTÔT QUE MAQUILLÉE.** Toutes les phases de
ce document écrivent leur attendu AVANT la mesure. Celle-ci ne l'a pas fait :
la mesure est partie la première, et écrire après coup une « prédiction » dont
je connais déjà la réponse serait exactement la malhonnêteté que cette règle
existe pour empêcher. C'est donc une phase de VÉRIFICATION, pas d'hypothèse, et
elle vaut ce que vaut ce genre de phase : elle confirme ou dément, elle ne
tranche rien.

**CE QU'IL Y AVAIT À VÉRIFIER.** Depuis D41.2, l'infobulle de la charge dit à
l'utilisateur : « *Piste ▸ Geler la piste* libère son instrument ». C'est un
conseil que le logiciel donne, et rien ne l'avait jamais éprouvé. **Un conseil
faux est pire qu'aucun conseil** : il fait perdre le temps de l'essayer, et il
détourne de la vraie cause.

**LA MESURE**, 16 pistes, huit fils de rendu, le fichier gelé publié au moteur
comme l'application le publie (`setTrackAudio`) :

| machine | en jeu | gelées | rapport | ce qu'il RESTE |
|---|---|---|---|---|
| `vsm.additive` | 1,098 ms | **0,030 ms** | **÷ 36,2** | 2,8 % |
| `vsm.minimoog` | 0,222 ms | **0,031 ms** | ÷ 7,1 | 14,0 % |

**Le conseil est juste.** Geler saute l'instrument ET la chaîne d'inserts
(`renderTrackVoice`), et ne garde que la lecture du fichier.

**LE CHIFFRE QUI COMPTE N'EST PAS LE RAPPORT, C'EST LA COLONNE « GELÉES ».**
Elle vaut **0,030 et 0,031 ms** — la même, à 3 % près, pour la machine la plus
chère du parc et pour un soustractif ordinaire. C'est attendu une fois qu'on le
voit : une piste gelée ne fait plus que lire un fichier stéréo, et lire un
fichier coûte ce qu'il coûte, quelle que soit la machine qui l'a produit.
**Le gain du gel est donc proportionnel à ce que la piste coûtait** — il paie
d'autant plus qu'on gèle une piste chère. C'est exactement la propriété qu'il
faut pour que le conseil de D41.2 soit bon là où il est donné : la barre passe
au rouge quand quelque chose coûte cher, et c'est précisément là que geler rend
le plus.

**ET CELA ACHÈVE DE JUSTIFIER D42.3.** Le conseil « gelez une piste » ne sert
que si l'on sait LAQUELLE : geler la moins chère de seize pistes rendrait
0,03 ms sur 0,22, et l'on conclurait que le gel ne sert à rien.

**CE QUE CETTE MESURE NE DIT PAS, ET C'EST DIT.** Le fichier gelé du banc est
un signal constant en mémoire, pas un vrai rendu sur disque : le COÛT est
représentatif (lire et rééchantillonner deux canaux), le SON ne l'est pas — les
crêtes diffèrent (1,131 contre 0,991) pour cette seule raison. Et le banc ne
mesure pas la lecture depuis le DISQUE, que l'application fait par flux : elle a
son propre test (`process_block_allocates_nothing_while_streaming_from_disk`),
mais son coût n'est pas chiffré ici.

Tests : 1 283 audio, 319 core, 285 interchange, 25 clap, 11 panels — verts.

> **D42.3, RÉGLÉ PAR UNE VRAIE RECONSTRUCTION — ET LE PIXEL AMBRE EST ENFIN
> PHOTOGRAPHIÉ (07/09/2026, 23:50).** Toutes les captures de cette journée
> utilisaient le projet de démonstration : **une** piste, 1,85 s. En ouvrant
> `reconstruction/children-dream-v7` — six pistes, six machines différentes, de
> vraies notes —, deux défauts du marquage sont apparus, que ni le banc ni le
> projet de démo ne pouvaient montrer.
>
> **PREMIER DÉFAUT : LE SEUIL NE SE DÉCLENCHAIT JAMAIS.** Trois fois la médiane
> venait du PARC, où D41 a mesuré un rapport de 45 entre les extrêmes. Mais un
> projet réel n'est pas le parc : coûts mesurés en jeu **86, 58, 136, 62, 152 et
> 34 µs**, médiane 86, maximum 152 — **1,8 fois**. La fonction n'aurait servi à
> personne. Ce qu'il faut désigner n'est pas une aberration mais **la plus
> chère** : la question « laquelle je gèle ? » a une réponse même quand l'écart
> est modeste, et geler la piste à 152 µs rend cinq fois ce que rend celle à 34.
>
> **SECOND DÉFAUT : LA DÉSIGNATION CLIGNOTAIT.** Deux relevés à une seconde
> d'intervalle sur le même morceau : `86 58 136 62 152 34` puis
> `80 63 106 17 49 17`. Les coûts d'un bloc varient du simple au triple selon
> les notes qui tombent ; recalculée à chaque image, la désignation sautait
> d'une tranche à l'autre. **Une étiquette qui clignote ne se lit pas** — et
> celle-ci sert à décider. On ne change donc d'avis que si la prétendante
> dépasse la désignée en titre d'un **quart** : c'est ce qui distingue « une
> autre piste vient de jouer une note » de « c'est l'autre qui coûte ».
>
> **CE QUE CELA DIT DE LA MÉTHODE.** Le banc validait la règle dans ses deux
> sens et ne pouvait pas voir ces deux défauts : il donnait des coûts FIXES,
> choisis par moi, et un écart de douze. **Un banc mesure ce qu'on lui donne à
> mesurer.** Les deux défauts tenaient à la forme des données réelles — un écart
> modeste, et du bruit — c'est-à-dire à ce que personne n'invente en écrivant un
> banc. Ouvrir un vrai projet aurait dû être le premier geste, pas le dixième.
>
> Vérifié à l'écran : sur `children-dream-v7` en lecture, la tranche **`other`**
> porte son nom en ambre, et elle seule — c'est bien la piste que les deux
> relevés donnaient la plus chère (136 puis 106 µs).

### Vérification D45 — Ouvrir puis enregistrer une VRAIE reconstruction ne perd rien (07/09/2026, 23:58)

**Pourquoi cette vérification n'avait jamais été faite.** L'invariant n° 5 du
§ 6 dit « rien ne se perd et rien ne ment », et chaque champ a son test
d'aller-retour dans `test_project_document.cpp`. Mais **aucun n'avait jamais
traversé un vrai projet** : ils construisent tous leur propre exemple. Or c'est
un vrai projet que l'utilisateur ouvre.

**Le protocole**, sur une COPIE — jamais sur les données de l'utilisateur :
ouvrir `reconstruction/children-dream-v7` (six pistes, six machines, 5 584
notes), presser Ctrl+S par le chemin du clavier (`VSM_TOUCHE`), et comparer.

**LES NOTES SONT IDENTIQUES, PISTE PAR PISTE** : 876, 580, 2192, 421, 416,
1099 — lues avec le lecteur MIDI du projet, non avec un comptage d'octets (le
premier essai, à l'octet, rendait 4 166 puis 6 145 et n'aurait rien prouvé).
**Rien n'est perdu, rien n'est inventé.**

**LE FICHIER MIDI DOUBLE POURTANT DE TAILLE** — 45 868 → 84 758 octets —, et
c'est la seule chose qui demandait une explication. Elle tient en cinq
méta-événements : le bloc privé `0x7F` de D6.3, qui fait voyager le `muted` et
la CONFIANCE de chaque note. Le fichier d'origine, écrit par la chaîne, ne les
portait pas ; le DAW les ajoute. **Il grossit parce qu'il conserve davantage,
pas parce qu'il réécrit mal.**

**CE QUI CHANGE DANS `project.json`, ET POURQUOI CHACUN EST LÉGITIME :**
version 1 → 2 (conversion vide, documentée) ; les paramètres du master écrits
avec leurs valeurs par défaut ; les clips matérialisés (« une piste avec du
matériau et sans clip joue mais ne se voit pas ») ; la couleur `#06D6A0FF`
rendue opaque — une réparation **délibérée et commentée** des projets écrits
avant que la chaîne ne corrige son ordre d'octets.

**LE SEUL VRAI ÉCART EST LA PRÉCISION DES VOLUMES**, et il est chiffré :
6,731937604802863 devient 6,731937 — le modèle les tient en `float`, le fichier
en double. Écart maximal **2,05 × 10⁻⁷**, soit **0,000000 dB**. C'est le prix
d'un `float`, et il est payé sous le seuil de l'audible par six ordres de
grandeur.

**ET LA LEÇON DE LA JOURNÉE SE CONFIRME ICI ENCORE.** Ce contrôle a été fait
parce qu'une vraie reconstruction venait de montrer deux défauts que le banc ne
pouvait pas voir (D42.3). Cette fois elle n'en montre aucun — **et c'est un
résultat, pas une absence de résultat** : « rien ne se perd » cesse d'être une
règle écrite pour devenir un chiffre mesuré sur les données que le logiciel
rencontre vraiment.

### Vérification D46 — L'export du DAW et `vsm-render` rendent le MÊME son, mesuré sur un vrai morceau (08/09/2026, 00:20)

**L'invariant n° 3 du § 6** exige que le rendu temps réel et le rendu hors ligne
soient identiques à l'échantillon près. Il a un test pour CLAP, un pour les
tailles de bloc, un pour le déterminisme — **tous sur des projets construits
pour l'occasion**. Jamais sur un vrai morceau.

**Le protocole.** `reconstruction/children-dream-v7` : six pistes, six machines,
5 584 notes, **232,53 secondes**. Exporté par l'application (`VSM_EXPORT`), puis
rendu par `vsm-render` à la même fréquence.

| comparaison | corrélation | écart |
|---|---|---|
| **export du DAW vs `vsm-render`, aujourd'hui** | **1,000000** | **−129,96 dB** |
| `vsm-render` vs `reconstruit.wav` (22 août) | 0,911369 | −7,12 dB |

**LES DEUX CHEMINS RENDENT LE MÊME SON.** −130 dB est le plancher de
quantification : l'application écrit en 24 bits, `vsm-render` en 32 bits
flottants. Il n'y a rien d'autre entre eux. L'invariant tient, et il tient
désormais sur les données que le logiciel rencontre vraiment.

**ET J'AI FAILLI PUBLIER UN DÉFAUT QUI N'EXISTE PAS.** La première mesure
comparait l'export du DAW à `reconstruit.wav`, le fichier rendu par la chaîne et
rangé dans le dossier : **−7,13 dB, corrélation 0,911**. Un tel écart n'est pas
du bruit — c'est un autre son. J'ai d'abord cherché un décalage temporel (il n'y
en avait pas : zéro échantillon), puis regardé la DATE du fichier : **22 août**,
c'est-à-dire avant l'essentiel de D0 à D45. **Comparer l'export d'aujourd'hui à
un rendu d'août compare deux moteurs**, et l'écart mesure ce que le moteur a
appris depuis, pas une faute.

La mesure qui tranche est celle où les deux termes sortent du même code — c'est
la règle des A/B du projet, appliquée ici à une comparaison que je n'avais pas
vue comme un A/B. **Un fichier rangé dans un dossier n'est pas un témoin ; c'est
un souvenir.**

**ET LE FICHIER PÉRIMÉ NE TROMPE PERSONNE**, vérifié plutôt que supposé :
l'écoute A/B ne lit jamais `reconstruit.wav`. Elle prend l'ORIGINAL — la source
nommée dans la provenance de `rapport.json`, ou à défaut le canal gauche de
`comparaison.wav` — et met en face le moteur VIVANT. La comparaison que
l'utilisateur entend est donc toujours celle d'aujourd'hui.

### Phase D47 — L'export en 32 bits flottants écrêtait, et les stems avec lui (08/09/2026, 01:10)

**Trouvé en vérifiant une phrase que l'outil affiche lui-même.** L'aide de
`vsm-render` promet : « **la somme des stems redonne le mixage** avant la
tranche master ». Personne ne l'avait mesurée. Sur `children-dream-v7` — six
pistes, 232,53 s —, la somme des six stems donnait **−51 dB** d'écart avec le
mixage, avec un maximum de **0,405** sur un échantillon. Ce n'est pas de la
précision : c'est un autre signal.

**LA CAUSE ÉTAIT DANS LE GRAVEUR DE FICHIERS, ET ELLE TENAIT EN UN APPEL.** La
branche `Float32` de `WavFileWriter` appelait `clampSample`, comme les branches
entières :

```cpp
    case SampleFormat::Float32: {
        float lc = clampSample(left[i]);   // <- le flottant n'a pas à borner
```

**Un WAV 32 bits flottants porte parfaitement au-delà de ±1 — c'est sa raison
d'être.** On exporte en flottant précisément pour garder la marge et la
rattraper au mastering. Les ramener à ±1 détruit ce que le format sait tenir,
et le détruit **en silence**.

**LES CHIFFRES.** Le mixage de ce morceau a un pic de **1,40517** ; le fichier
flottant en rendait **1,0000**, et **602 échantillons sur 20,5 millions**
étaient rabotés. Après correction, le fichier porte 1,40517 et la somme des
stems retrouve le mixage à **−148,7 dB** — la précision du flottant, c'est-à-dire
l'égalité. La promesse de l'aide est donc vraie ; c'était le graveur qui
mentait.

**ET C'ÉTAIT PIRE QUE LE MIXAGE : LES STEMS AUSSI.** Le premier relevé donnait
`01 - bass.wav crête 1,0000` — un pic à exactement 1,0000 sur un stem est la
signature d'un écrêtage, pas un hasard. C'est ce qui explique que l'écart
subsistât (−98 dB) **là même où le mixage n'écrêtait pas** : c'était la basse
qui était rabotée dans son propre fichier. **Un jeu de stems livré à un
mixeur arrivait donc amputé**, sans que rien ne le dise.

**CE QUI CONTINUE DE BORNER, ET C'EST JUSTE.** Les formats 16 et 24 bits ne
savent pas représenter au-delà de l'échelle : y écrire un dépassement replierait
le signal, c'est-à-dire produirait un son FAUX plutôt qu'un son fort. Ils
bornent toujours, et un test neuf l'épingle — car le seul test d'écrêtage qui
existait portait sur `Int16`, **le cas où borner est juste**. Rien ne surveillait
l'autre.

**LE DÉPASSEMENT SE DIT MAINTENANT, DES DEUX CÔTÉS.** `vsm-render` ajoute à son
résumé « AU-DESSUS DE 0 dBFS : conservé en 32 bits flottants, borné en 16 ou
24 bits » ; l'application, dont le message affichait « crête 1,405 » en laissant
l'utilisateur en tirer la conséquence, dit désormais que le format a borné **et
nomme les deux remèdes qui existent déjà dans le même menu** — « crête à
-1 dBFS », ou l'export en flottant.

Tests : **1 285 audio** (2 neufs), 319 core, 285 interchange, 25 clap,
11 panels — tous verts, empreintes comprises.

> **CE QUE `vsm-render` PROMET, VÉRIFIÉ (08/09/2026, 01:30).** D47 est né du
> contrôle d'une phrase de l'aide de l'outil. Les autres ont été passées au même
> crible, sur `children-dream-v7` :
>
> | ce que l'aide promet | mesuré |
> |---|---|
> | « le rendu part toujours de zéro et la plage est découpée ensuite » | `--start 60 --duration 10` rend **exactement** le même passage que le rendu complet découpé : écart **nul**, au bit près |
> | « la somme des stems redonne le mixage » | **−148,7 dB** après D47 (c'était −51 dB avant, et c'est ce qui a mené à D47) |
> | « [le temps réel est] inutile aux machines de ce projet, qui sont déterministes » | `--temps-reel` rend le fichier **identique au bit près** — et prend bien 8,0 s pour 8 s d'audio |
> | « [le dither est] actif par défaut, sans effet sur le flottant » | `--sans-dither` change le rendu **int24** et ne change **rien** au **float32** |
>
> **ET LE RENDU EST DÉTERMINISTE JUSQUE DANS LES FORMATS ENTIERS** : deux
> exécutions du même rendu donnent le même fichier au **hachage près**, en
> float32 comme en int24. Le bruit du dither est donc semé de façon
> reproductible — ce qui n'allait pas de soi, un dither étant par définition du
> hasard, et ce dont dépend toute la chaîne de mesure du projet.
>
> **CE QUE CE CONTRÔLE APPREND SUR D47.** Quatre promesses sur cinq étaient
> vraies. Celle qui ne l'était pas ne l'était pas à cause de l'outil : elle
> l'était à cause du **graveur de fichiers**, deux couches plus bas, que
> personne ne soupçonnait parce que sa faute ressemblait à un mixage fort. Une
> promesse fausse ne dénonce pas toujours celui qui la fait.

> **L'AVERTISSEMENT DE D47, VÉRIFIÉ SUR LE VRAI CHEMIN D'EXPORT (08/09/2026,
> 01:50).** Exporté depuis l'application, `children-dream-v7` en WAV 24 bits —
> le cas où borner est inévitable — rend exactement :
>
> ```
> 232.5 s, 44.1 kHz, 24 bits, crête 1.405.
>
> ATTENTION : la crête dépasse 0 dBFS (3.0 dBFS). Ce format ne peut pas la
> porter et l'a bornée. « Niveau : crête à -1 dBFS » à l'export l'évite, ou un
> export en 32 bits flottants la conserve.
> ```
>
> Le message donne le dépassement **en dBFS** (3,0) et non en nombre nu : c'est
> l'unité dans laquelle on décide de baisser un fader. Et il nomme les deux
> remèdes, qui existaient déjà dans le même menu — la phase n'a rien ajouté à
> faire, elle a ajouté de le DIRE.
>
> **LES TROIS FORMATS SONT ÉCRITS ET BIEN FORMÉS** : WAV (`RIFF`, 61,5 Mo),
> FLAC (`fLaC`, 44,0 Mo) et Ogg (`OggS`, 12,6 Mo) sur le même morceau de
> 232,5 s.
>
> **ET LE LECTEUR EST SYMÉTRIQUE DE LA CORRECTION**, vérifié plutôt que
> supposé : `WavDecoding.h` rend un `float32` **tel quel**, sans le borner. Ce
> qu'on écrit au-delà de ±1 se relit au-delà de ±1 ; le défaut était bien du
> seul côté du graveur.

> **D47 INVALIDE-T-IL LES MESURES DÉJÀ FAITES ? NON, ET C'EST VÉRIFIÉ
> (08/09/2026, 02:05).** La question se posait d'elle-même : si la chaîne
> d'analyse comparait des rendus écrits par `WavFileWriter`, alors **toute
> distance mesurée sur un morceau fort l'aurait été sur du son raboté**, et les
> campagnes seraient à refaire.
>
> **Elle ne l'était pas.** La boucle de recherche parle au moteur par
> `vsm-render --serve`, qui lui rend l'audio en **base64 flottant** dans sa
> réponse JSON (`PatchRenderService`, `returnAudio: base64-f32-mono`) : une
> somme mono des tampons du moteur, sans passer par aucun fichier et **sans
> écrêtage**. Vérifié des deux côtés : `vsm_engine.py` ne demande jamais autre
> chose que `base64-f32-mono`, et le service ne borne cette somme nulle part.
>
> **CE QUI ÉTAIT BIEN ATTEINT, EN REVANCHE** : le `reconstruit.wav` final, le
> `comparaison.wav`, et tout export d'utilisateur — c'est-à-dire **ce qu'on
> écoute et ce qu'on livre**, jamais ce qu'on mesure. D47 améliore donc le
> produit sans toucher au verdict d'aucune campagne passée.
>
> **CE QUE CETTE VÉRIFICATION AURAIT COÛTÉ SI ON NE L'AVAIT PAS FAITE.** Rien
> d'immédiat — et c'est bien le problème. Une correction dont on ignore la
> portée laisse planer un doute sur tout ce qui l'a précédée, et ce doute finit
> par être tranché de mémoire, dans le sens qui arrange, six mois plus tard.

> **D47 A AUSSI RÉPARÉ LE GEL, SANS QUE JE LE SACHE (08/09/2026, 02:15).**
> `toggleFreezeSelectedTrack` écrit son rendu en `SampleFormat::Float32` — donc
> par la branche que D47 vient de corriger. **Geler une piste dont le pic
> dépasse 1 changeait son son**, alors que le gel est censé être transparent :
> c'est le remède que D41 conseille et que D44 a chiffré, et il abîmait ce qu'il
> soulageait.
>
> **Mesuré sur le même morceau**, une fois le graveur corrigé : la piste `bass`
> a un pic réel de **1,04219**, et **13 échantillons** au-dessus de 1. Geler
> cette piste avant D47 les rabotait ; les cinq autres pistes, toutes sous 0,96,
> n'auraient rien perdu.
>
> **CE QUE CELA DIT DU DÉFAUT.** Il n'était pas dans l'export, ni dans les
> stems, ni dans le gel : il était dans le **graveur**, et il atteignait donc
> tout ce qui écrit un fichier — un endroit, trois symptômes, dont deux que je
> n'avais pas cherchés. C'est l'inverse de la faute de portée commise quatre
> fois aujourd'hui : ici la mesure d'un seul cas valait pour tous, non parce que
> je l'ai décidé, mais parce que le code n'a qu'un chemin.

### Phase D48 — Le mètre du master était mort par défaut, et rien ne disait que la sortie saturait (08/09/2026, 02:40)

**Suite directe de D47, mais du côté vivant.** D47 a fait dire à l'export qu'il
écrête. Reste la lecture : `children-dream-v7` sort à **1,405** — jouer ce
morceau sature la carte son, et rien à l'écran ne le disait.

**PREMIER DÉFAUT, ET IL EST PLUS GRAVE QUE LE SECOND : LE MÈTRE NE MESURAIT
RIEN.** `MasterBus::process` sortait au premier `if (!isEnabled())`, emportant
avec lui le pic, la valeur efficace, la corrélation **et** la sonie. Or un bus
master désactivé LAISSE PASSER le son. Ce que l'utilisateur voyait alors était
une aiguille morte, « phase 1.00 » et « **-inf LUFS** » pendant que le morceau
jouait.

**ET CE N'EST PAS UN CAS RARE : tout projet écrit par la chaîne de
reconstruction arrive avec `"Master Enabled": 0`.** Le seul mètre de SORTIE du
logiciel était donc éteint **par défaut, sur les projets qui font l'objet de ce
dépôt**. Mesuré à l'écran, même morceau, même instant : « -inf LUFS » avant,
« **-16,3 LUFS**, phase 0,98 » après.

Le contrat « no-op complet si désactivé » reste tenu pour ce qui compte : le
**signal** n'est pas touché. **Mesurer n'est pas traiter.**

**SECOND DÉFAUT : L'AIGUILLE NE DIT PAS LE DÉPASSEMENT.** Au-delà de 1 la barre
est en butée, et l'on ne distingue pas 1,0 de 1,4. Un témoin **SAT** rouge
s'allume, retient le PIRE dépassement en dB — « ça a saturé » et « ça a saturé
de 3 dB » n'appellent pas le même geste — et **garde sa mémoire** : une crête
dure quelques échantillons, un voyant qui s'éteindrait aussitôt ne serait jamais
vu. On l'efface d'un clic, comme sur une console.

**LA PLACE A DÛ ÊTRE PRISE À QUELQU'UN, ET C'EST ÉCRIT.** La tranche master est
déjà trop courte pour ce qu'elle porte : huit potentiomètres sur quatre rangées
de 46 pixels dépassent la hauteur disponible, si bien que réserver une ligne de
plus ne suffisait pas — le témoin s'écrivait **par-dessus** les libellés RATIO
et SAT. Il occupe donc la ligne de la PHASE tant que dure l'écrêtage : la
corrélation est un diagnostic qu'on va consulter, la saturation est un fait
qu'il faut voir maintenant. La phase reste lisible dans l'infobulle du témoin.

**ET UN DÉFAUT QUE SEULE LA CAPTURE POUVAIT MONTRER.** Le témoin, écrit
`setVisible(false)` **avant** `addAndMakeVisible`, était en fait VISIBLE :
`addAndMakeVisible` rend visible, et annulait la ligne précédente. Une étiquette
vide occupait donc la ligne en permanence et en chassait la phase. **Un
composant invisible qui reste visible ne se voit pas — il se voit à ce qu'il
cache**, et c'est la disparition de « phase 0.98 » qui l'a dénoncé, pas la
lecture du code. `addChildComponent` ajoute sans montrer, et c'est le geste
juste.

Vérifié à l'écran, deux positions du même morceau : mesure 5 (rien ne sature) →
« phase 0.98, -16,3 LUFS » ; mesure 67 (le pic) → « **SAT +1.6** » en rouge à la
place de la phase.

Tests : 1 285 audio, 319 core, 285 interchange, 25 clap, 11 panels — verts.

> **AUCUNE COMMANDE MORTE, VÉRIFIÉ PAR BALAYAGE (08/09/2026, 02:55).**
> L'invariant de D0.8 — « aucun contrôle affiché n'est sans effet » — n'avait
> jamais été revérifié depuis. Les **64** boutons, curseurs et listes déclarés
> dans `app/Source` ont été confrontés à leur gestionnaire (`onClick`,
> `onValueChange`, `onChange`, `addListener`).
>
> **Trois seulement n'en avaient pas, et les trois sont légitimes**, vérifiés un
> par un plutôt que comptés : `prise_` et `sectionAAjouter_` sont des SÉLECTEURS
> dont un bouton voisin lit la valeur au moment d'agir (`getSelectedId()`), et
> `selectTool_` reçoit son `onClick` par une lambda d'assistance qui câble les
> six outils du piano roll d'un coup. **Un contrôle sans gestionnaire n'est pas
> mort s'il est LU** — la question n'est pas « qui écoute ce bouton » mais « ce
> bouton change-t-il quelque chose ».
>
> L'invariant tient donc, et il tient sur un chiffre plutôt que sur un souvenir.

### Phase D49 — Le remède contre l'écrêtage produisait un fichier écrêté (08/09/2026, 03:20)

**Trouvé en vérifiant un conseil que je venais d'écrire.** D47 et D48 disent à
l'utilisateur, quand la sortie dépasse 0 dBFS : « *Niveau : crête à -1 dBFS* à
l'export l'évite », et « activer le limiteur ». Comme en D44, un conseil se
mesure avant d'être donné.

**LE LIMITEUR TIENT PAROLE**, et exactement : master activé, plafond à
−0,3 dBFS, le pic de `children-dream-v7` passe de **1,40517 à 0,966051** —
c'est-à-dire **−0,30 dBFS** au centième près.

**L'AUTRE REMÈDE, LUI, NE TENAIT PAS.** Export en « crête à -1 dBFS » : le
message annonçait « crête ramenée à -1 dBFS (-4.0 dB) », et le fichier sortait
à **−3,95 dBFS**. Trois décibels d'écart, sur l'option dont c'est toute la
raison d'être.

**LA CAUSE, ET C'EST D47 UNE TROISIÈME FOIS.** Quand l'export doit réécrire
(normalisation, FLAC, OGG), il rend d'abord un fichier intermédiaire — **dans le
format cible**, donc en 24 bits le plus souvent. Le rendu à 1,405 y était borné
à 1,0 **avant** que le gain de 0,634 ne s'applique : 1,0 × 0,634 = **0,634**,
soit les −3,95 dBFS mesurés. Le remède contre l'écrêtage produisait un fichier
**écrêté puis baissé**, c'est-à-dire le pire des deux.

**ET LE MESSAGE DISAIT VRAI SUR CE QU'IL AVAIT CALCULÉ, PAS SUR CE QU'IL AVAIT
ÉCRIT.** « crête 0,891 » est `peakLevel × gain` — la valeur PRÉVUE. Le fichier,
lui, en portait 0,634. **Un compte rendu qui répète l'intention ne vérifie
rien** : il aurait fallu relire le fichier, comme ce banc l'a fait.

**LA CORRECTION TIENT EN DEUX LIGNES** : l'intermédiaire est écrit en 32 bits
flottants, toujours. Il n'a aucune raison d'être dans le format cible — il est
effacé juste après —, et le flottant le porte sans rien perdre, ce pour quoi il
existe. Mesuré après : **0,89125, soit −1,00 dBFS**, exactement ce qui était
demandé.

**CE QUE CES TROIS PHASES DESSINENT ENSEMBLE.** Un même geste — borner à ±1 là
où le format n'y oblige pas — a produit **quatre** symptômes : le mixage
exporté, les stems, le gel, et la normalisation. Aucun ne ressemblait aux
autres, et chacun avait l'air d'un problème de niveau plutôt que d'un problème
d'écriture.

Tests : 1 285 audio, 319 core, 285 interchange, 25 clap, 11 panels — verts.

### Phase D50 — Le mixage disait son écrêtage, les stems sortaient en silence (08/09/2026, 15:45)

**La quatrième fois que le même geste se paie, et la première où il ne
s'agissait plus d'écrire un fichier mais d'en parler.** D47 avait borné à ±1
là où le format n'y obligeait pas ; D48 avait donné au mixage exporté un
avertissement quand sa crête dépasse 0 dBFS ; D49 avait mesuré que le remède
annoncé tenait parole. **L'export PAR STEMS n'avait rien reçu de tout cela** :
`renderStemsToFolder` écrivait chaque fichier dans le format demandé sans
jamais regarder sa crête, `Stem` ne portait aucun pic, et les deux comptes
rendus — « 6 stems écrits dans … » dans l'application, un nombre de fichiers
dans `vsm-render` — ne disaient pas un mot du niveau.

**HYPOTHÈSE, ÉCRITE AVANT LA MESURE.** Sur `children-dream-v7` (mixage à
1,405), au moins un stem dépasse 1,0 ; exporté en 24 bits — **le défaut de
l'interface** —, il est borné sans un mot ; et la somme des stems s'écarte
alors du mixage bien plus que les −148,7 dB annoncés en D47, qui avaient été
mesurés en 32 bits flottants.

**LES TROIS SONT VRAIES, ET LE CHIFFRE DU MILIEU EST LE PLUS PARLANT.**

| | 32 bits flottants | 24 bits entiers |
|---|---|---|
| crête du stem `01 - bass` | **1,04341** (+0,37 dBFS) | 1,00000 (borné) |
| échantillons rabotés | 0 | **14** sur 22 322 454 |
| écart somme-mixage, RMS | −163,0 dB | −93,6 dB |
| écart somme-mixage, **crête** | **−137,8 dB** | **−27,2 dB** |

**−27,2 dB, c'est-à-dire 0,0437 d'amplitude** — et 0,0437 est exactement
1,04341 − 1,0. **La promesse « la somme des stems redonne le mixage » ne tombe
donc pas un peu : elle tombe de cent dix décibels, à cause de quatorze
échantillons sur vingt-deux millions**, et rien dans le dossier écrit ne
permettait de s'en apercevoir. Cinq stems sur six étaient parfaits.

**CE QUI EST AJOUTÉ.** `Stem` porte sa crête — prise du rendu, qui la mesure
déjà — et, seulement quand elle dépasse 1, le nombre d'échantillons concernés
(le parcours ne se paie que là où il apprend quelque chose). L'écriture
prévient, par stem, en nommant le fichier, le dépassement et le remède. Les
deux comptes rendus listent désormais la crête de chaque stem.

**LE REMÈDE NOMMÉ EST LE 32 BITS FLOTTANTS, ET C'EST LE SEUL HONNÊTE.** Le
mixage, lui, a le choix : « crête à -1 dBFS » le baisse et le fichier reste
juste. Un stem non : le baisser seul ferait mentir la somme, et les baisser
tous donnerait un jeu de stems qui ne redonne plus CE mixage-là. Le tableau
ci-dessus est la mesure de ce conseil, faite **avant** de le donner — la règle
de D44 et D49.

**ET LA LEÇON DE D49 S'APPLIQUAIT À CE QUE J'ÉCRIVAIS À L'INSTANT.** La
première version de la liste affichait « `01 - bass.wav` — crête +0,36 dBFS »
en face d'un fichier 24 bits qui, relu, en portait 1,00000. C'était exactement
la faute que D49 venait de nommer : *un compte rendu qui répète l'intention ne
vérifie rien*. La ligne dit maintenant « crête +0,36 dBFS, **bornée à 0 dBFS
par ce format** », et elle est vraie du fichier qu'elle nomme. Les six fichiers
écrits par l'application ont été relus, dans les deux formats : chaque nombre
annoncé est celui du fichier, au centième de décibel. (Les chiffres de
l'application diffèrent de ceux de `vsm-render` — +0,36 dBFS et 13 échantillons
sur 20 508 756 — parce qu'elle rend à la fréquence de la session, 44,1 kHz, et
non à 48 kHz ; c'est le même stem, pas un désaccord.)

**L'EXPORT PAR STEMS ÉTAIT INVÉRIFIABLE, ET C'EST CE QUI L'AVAIT LAISSÉ
DERRIÈRE.** Il vit derrière **deux** modales — une fenêtre d'options, puis un
sélecteur de dossier — que nulle capture ne traverse : son compte rendu ne
pouvait donc être relu par personne, et c'est très exactement ce que le § de
conduite sur l'interface interdit de laisser. `VSM_EXPORT_STEMS=dossier`,
`VSM_EXPORT_STEMS_FORMAT` et `VSM_EXPORT_STEMS_PAR` le rendent exécutable sans
souris, **par le même code que le menu** : le texte du menu et celui du
terminal sont le même texte, il n'y a donc qu'une chose à vérifier. Le mode
d'emploi les liste.

**CE QUE CETTE PHASE AJOUTE À LA SÉRIE D47–D49.** Le même geste initial a
produit quatre symptômes (mixage, stems, gel, normalisation) ; D50 en montre un
cinquième d'une autre nature — non plus un fichier faux, mais un fichier juste
qu'aucun compte rendu ne décrivait. **Réparer l'écriture ne suffit pas si
l'écriture reste muette** : le § « Mesure » dit qu'une panne muette est
interdite, et un stem raboté en silence en était une.

Test : `a_stem_above_full_scale_says_so_and_names_the_format_that_clips_it`
monte un stem à 1,05 par le seul fader (une variable, le témoin étant le même
projet rendu en flottant), vérifie sa prémisse avant tout le reste, exige un
avertissement et **un seul**, nommant le stem et le remède, et **relit les deux
fichiers écrits** pour confronter l'annonce au contenu.

Tests : 1 285 audio, 319 core, 286 interchange, 25 clap, 11 panels — verts.

### Phase D51 — Le chargeur mesure le rééchantillonnage depuis D2, et l'application ne l'a jamais écrit (08/09/2026, 16:30)

**Trouvé en appliquant la leçon de D50 à l'autre bout de la chaîne.** D50 dit :
*réparer l'écriture ne suffit pas si l'écriture reste muette*. La même question
posée à la LECTURE donne ceci — `AudioTrackLoadResult` porte `resampled`,
`fileSampleRate`, `sessionSampleRate`, `streamed` et `residentBytes`, et son
en-tête écrit, en toutes lettres :

> *« le rééchantillonnage a eu lieu, et le rapport le dit »* … *« Le rapport le
> dit, comme il dit le rééchantillonnage : c'est une propriété de ce qui a été
> chargé, et **l'interface doit pouvoir l'écrire** »*

**UN GREP SUR TOUT LE DÉPÔT NE TROUVE QU'UN SEUL LECTEUR DE `resampled`** : le
rendu hors ligne, qui en fait un avertissement. `streamed` et `residentBytes`
n'en ont **aucun**. L'application les remplissait et n'en lisait pas un.

**MESURÉ, PAS DÉDUIT.** Un projet portant un fichier à 96 kHz, ouvert dans une
session à 44,1 kHz :

| | ce qui est dit |
|---|---|
| `vsm-render` | `avertissement : Piste 0 (Sinus 96k) : audio rééchantillonné de 96000 à 48000 Hz` |
| l'application | **rien** — pas une boîte, pas une ligne, pas un mot sur la ligne de piste (capture et sortie d'erreur vides) |

**Deux vérités pour un même fait**, c'est-à-dire celle qu'on lit et celle qu'on
n'a pas. Et le fait n'est pas mince : un fichier rééchantillonné n'est plus le
fichier qu'on a posé — c'est exactement ce que D2 a payé en remplaçant
l'interpolation linéaire par un noyau sinc.

**LA MENTION VA SUR LA LIGNE, PAS DANS UNE BOÎTE.** C'est la règle de D43 (« un
témoin permanent tant que le son manque, et non une boîte à fermer ») appliquée
telle quelle : la fréquence d'un fichier ne change pas, on la relit chaque fois
qu'on se demande ce que joue cette piste, et une boîte fermée se ferme.
La ligne de piste porte donc `res0.wav · 96 → 44.1 kHz`, et `· disque` quand le
matériau est diffusé (D8.2) plutôt que résident. L'infobulle porte la phrase
entière, y compris les mégaoctets résidents. Vérifié à l'écran sur trois bancs :
un fichier court rééchantillonné, un fichier de trente secondes (donc diffusé),
et six pistes à la fois.

**ET SUR LE TERMINAL AUSSI**, pour la même raison que les avertissements de
`vsm-render` : une capture montre la ligne, un banc automatique a besoin d'une
phrase à lire (`VSM_AUDIO : rééchantillonné — …`). L'interface cesse d'être
invérifiable sur ce point.

#### D51.2 — La trace neuve écrivait tout DEUX fois, et ce n'était pas l'affichage

**La première chose que la nouvelle ligne a apprise porte sur autre chose
qu'elle-même.** Elle sortait en double à chaque ouverture. Ce n'était pas un
doublon d'affichage : `applyAudioConfig()` recharge toutes les pistes audio
quand la fréquence change, et `appliedSampleRate_` **part de zéro** — le premier
passage du minuteur rechargeait donc TOUT, y compris ce que l'ouverture du
projet venait de charger à la MÊME fréquence une milliseconde plus tôt.

**Mesuré sur six pistes de dix-neuf secondes à 96 kHz** (résidentes, donc
décodées et rééchantillonnées en entier) : **douze chargements pour six
pistes**, comptés sur la sortie d'erreur, en quatre ouvertures — 48 lignes pour
24 attendues. Après correction : **6 par ouverture**, exactement.

La correction est un compteur : `audioTracksLoadedAtRate_` retient la fréquence
à laquelle les pistes sont chargées, et `applyAudioConfig()` ne recharge que si
elle a bougé. Les autres appelants de `loadAudioTracks()` (import, gel, prise)
ne sont pas touchés : eux ont une raison de recharger.

**LE TEMPS, DIT COMME IL A ÉTÉ MESURÉ.** Ouverture complète du même projet :
1 323–1 358 ms avant (4 mesures), 901–1 204 ms après (12 mesures). **Ce n'est
pas un A/B propre** au sens du § « Mesure » — le témoin n'est pas derrière une
option, c'est le code d'avant —, et la distribution d'après est bimodale
(~905 ms et ~1 200 ms en alternance) sans que je sache pourquoi. **Le chiffre
qui tranche est donc le compte de chargements, 12 → 6, qui est exact et ne
dépend d'aucune horloge** ; le temps n'est qu'une indication, et il est écrit
comme telle plutôt que lissé en un gain.

Tests : 1 285 audio, 319 core, 286 interchange, 25 clap, 11 panels — verts.

### Phase D52 — « Rien n'est jamais appliqué en douce » : quatre appelants jetaient le rapport qui le garantissait (08/09/2026, 17:20)

**Trouvé en cherchant à la machine, et le grep a d'abord menti.** D50 et D51
sont deux trouvailles de la même forme : un fait mesuré que personne ne lit.
Plutôt qu'une troisième trouvée à la main, j'ai listé les 181 champs des
38 structures « résultat » du dépôt et compté leurs lectures. **Le résultat brut
était inexploitable** — l'expression régulière prenait des variables locales
pour des champs de structure, et douze des seize « champs jamais lus » étaient
des faux (`LatencyProbe::Resultat` n'a ni `somme` ni `produit`). C'est
exactement l'avertissement du § « Pièges payés » : *un « zéro » sorti d'un grep
se revérifie avant de l'écrire*. Deux candidats ont survécu à la vérification à
la main, et l'un a ouvert cette phase.

**CE QUE `PresetApplyReport` PROMET.** Son en-tête dit : *« Ce qui s'est
réellement passé à l'application d'un preset — jamais silencieux : chaque
paramètre non appliqué est nommé, avec sa raison. »* Et celui de `applyPreset` :
*« Rien n'est jamais appliqué en douce. »*

**QUATRE APPELANTS LE JETAIENT.**

| Où | Ce qui disparaissait |
|---|---|
| `MainComponent.cpp:5769` | le rapport des ÉCHANTILLONS, **à deux lignes** de celui des paramètres qui, lui, ouvrait une boîte |
| `MainComponent.cpp:9546` | le rapport des paramètres, chemin « Appliquer un preset de piste » |
| `MainComponent.cpp:9547` | le rapport des échantillons, même chemin |
| `clap/adapter/VsmClapAdapter.cpp:271` | l'état relu par l'hôte CLAP, à la réouverture d'une session |

**MESURÉ SUR UN BANC, AVANT ET APRÈS.** Un preset de piste posé sur un Minimoog,
portant quatre paramètres qu'il connaît, un hors bornes et deux venus d'une
autre architecture (`oscillator.sub.level`, `filter.2.cutoff`) :

| | ce que l'application dit |
|---|---|
| avant | `VSM_MENU : « bancD52 » exécutée (menu Piste)` — **rien d'autre** |
| après | `VSM_PRESET : réserves — 4 paramètre(s) appliqué(s), 1 borné(s), 2 non pris en charge : filter.2.cutoff, oscillator.sub.level` |

Un premier banc, plus brutal, avait donné **0 appliqué sur 6** — un preset
entièrement perdu, sans un mot, et la façade de la machine à l'écran ne laissait
rien deviner.

**LA BOÎTE NE SE PHOTOGRAPHIE PAS, ET C'EST DIT PLUTÔT QUE CONTOURNÉ.**
`VSM_CAPTURE` fait l'autoportrait de la FENÊTRE PRINCIPALE ; une `AlertWindow`
est une fenêtre à part et n'y figure pas. La capture de ce banc montre donc la
machine, pas le message — d'où la ligne `VSM_PRESET : réserves — …` sur la
sortie d'erreur, qui est la seule trace vérifiable sans souris. Écrire « boîte
vérifiée » sur la foi d'une capture qui ne la contient pas aurait été la faute
de D49 sous un autre déguisement.

**LE CAS CLAP EST TRANCHÉ ICI, ET LA RAISON EST ÉCRITE.** Un plugin n'ouvre pas
de boîte, et rendre `false` serait pire : l'hôte jetterait l'état ENTIER parce
qu'un seul paramètre est inconnu. La réserve va donc sur la sortie d'erreur —
le journal de l'hôte —, et seulement quand il y a quelque chose à dire. Le cas
n'est pas théorique : c'est celui d'une session écrite par une version où la
machine avait d'autres paramètres.

Test : `the_apply_report_tells_the_truth_about_what_the_machine_took` exige les
trois comptes (4 appliqués, 1 borné, 2 non pris en charge), exige que les deux
inconnus soient NOMMÉS dans le résumé — un compte sans les noms ne se vérifie
pas —, et **relit chaque paramètre dans la machine** pour confronter
`appliedValue` à ce qu'elle porte vraiment. C'est la leçon de D49 appliquée à un
rapport plutôt qu'à un fichier.

Tests : 1 285 audio, 319 core, 287 interchange, 25 clap, 11 panels — verts.

### Phase D53 — Une distance publiée sans sa métrique, et cinq distances par piste que personne ne voyait (08/09/2026, 18:05)

**Le second candidat survivant de l'audit de D52.** `StemReport::distance` était
lu par `ReconstructionReport.cpp` et par personne d'autre. Ce n'est pas un champ
anodin : c'est le seul chiffre qui dise **où** travailler.

**CE QUE L'ÉCRAN MONTRAIT.** « 6 piste(s) reconstruite(s) · distance globale
0.2325 (0 = identique, 1 = silence) », puis `bass → vsm.piano`,
`guitar → vsm.minimoog`… — les machines, sans un chiffre.

**CE QUE LE FICHIER PORTAIT AU MÊME MOMENT** (`children-dream-v7/rapport.json`,
relu directement) :

| stem | machine | distance |
|---|---|---|
| guitar | `vsm.minimoog` | **0,1755** |
| bass | `vsm.piano` | 0,1896 |
| other | `vsm.string` | 0,2196 |
| vocals | `vsm.obx` | 0,2217 |
| piano | `vsm.wind` | **0,2224** |

**La pire piste est 26,7 % plus loin que la meilleure**, et l'écran ne le disait
pas. Une seule distance globale de 0,2325 les résume toutes et n'en désigne
aucune : le musicien qui veut gagner du terrain ne sait pas par où commencer.
La ligne du piano porte désormais « — la plus loin de l'original », en ambre,
comme le fourre-tout et le porteur d'énergie.

**ET LE NOMBRE ÉTAIT PUBLIÉ SANS CE QUI LE REND COMPARABLE.** L'ordre de marche
du projet dit : « *deux distances ne se comparent que si métrique, budget, gate
et stems sont identiques* », et l'en-tête de `ReconstructionReport` répète que
« les distances v1 et v2 ne se comparent pas ». L'écran affichait pourtant
`0.2325` tout nu, alors que `metric` (« v2 ») et `iterations` (20) étaient dans
le même fichier, à deux champs de là. **Un nombre sans sa métrique invite
exactement la comparaison que le projet interdit, et il l'invite d'autant plus
qu'il a l'air simple.** La ligne « Ne se compare qu'à une distance de métrique
v2 et de budget 20 itération(s) » suit désormais le résumé.

**LE `gate` AUSSI, MAIS SEULEMENT QUAND IL N'EST PAS À 1.** Il conditionne la
distance au même titre que la métrique — le faire passer de 0,95 à sa vraie
valeur 0,24 sur un violoncelle à l'archet change la distance d'un facteur 1,6 et
**inverse le classement des machines**, sans toucher une ligne de DSP
(ARCHITECTURE.md § 32). Mais « gate 1.00 » sur chaque ligne deviendrait un
meuble : il ne s'écrit que lorsqu'il dit quelque chose. Vérifié à l'écran sur un
rapport truqué à `gate` 0,24 et 0,71 — les deux lignes le portent, les trois
autres restent nettes.

**UNE DÉCISION PRISE ET ÉCRITE PLUTÔT QUE FAITE EN DOUCE.** J'ai d'abord ajouté
la distance à la liste montrée À L'OUVERTURE du projet, puis je l'ai retirée :
cette liste alimente la boîte « **Projet ouvert, avec des reserves** ». Une
distance n'est pas une réserve, et l'y mettre ferait s'ouvrir une boîte
d'avertissement sur **chaque** reconstruction, y compris les meilleures — un
avertissement qui s'allume toujours devient un meuble qu'on ne lit plus, le même
raisonnement que le compteur de décrochages de D41.3. La distance par stem vit
donc dans « Voir le rapport de reconstruction », où on la cherche quand on la
cherche. La raison est écrite dans le code, à l'endroit où la tentation
reviendra.

**CE QUE CETTE PHASE DOIT À D52.** Elle ne vient pas d'une intuition : elle
vient du second des deux candidats que le comptage de lectures avait laissés
debout après vérification à la main. La méthode a donc produit deux phases, et
la moitié du travail a consisté à jeter ce que le grep avait cru trouver.

Vérifié à l'écran : trois captures (l'écran d'avant, l'écran d'après, et le cas
`gate`), chaque nombre affiché confronté au JSON relu séparément.

Tests : 1 285 audio, 319 core, 287 interchange, 25 clap, 11 panels — verts.

### Phase D54 — Transposer un clip audio : l'élément que D21 avait reporté, et que trois phases après elle ont reporté avec (08/09/2026, 19:40)

**Un élément reporté quatre fois pour une raison qui a cessé d'exister.** D21 :
« *Transposer un clip audio demande un rendu différent dans le moteur, donc dans
`vsm-render` aussi, qu'on ne recompile pas pendant qu'une campagne tourne. Il
attend la fin de la campagne R1.* » D22, D23 et D24 l'ont reporté à leur tour,
chaque fois en le disant, chaque fois pour la même raison. **La campagne R1 est
finie ; aucune ne tourne** (`pgrep` avant d'y croire, comme le § « Pièges
payés » l'exige). L'élément se prend donc, et il se prend en premier parce
qu'un report dont le motif a expiré n'est plus un report, c'est un oubli.

**LE PRINCIPE TIENT EN UNE LIGNE.** Lire un matériau `r` fois plus vite monte
sa hauteur d'un facteur `r` et raccourcit sa durée d'autant — le vinyle qu'on
accélère, que le mode `Repitch` fait depuis D12. Étirer le résultat par le même
`r` rend la durée sans retoucher la hauteur — ce que le vocodeur de phase fait
depuis D12.8. **Aucune des deux moitiés n'est neuve ; c'est leur composition qui
l'est**, et le code ajouté au DSP est un décorateur de vingt lignes,
`PitchedSampleStore`, glissé SOUS l'étireur — la même couture que
`MirroredSampleStore` avait utilisée pour le clip à l'envers.

**LA PRÉDICTION DU CDC ÉTAIT BONNE SUR L'ESSENTIEL ET FAUSSE SUR DEUX POINTS**,
notés au § 9 de `CDC-etirement-temporel.md` plutôt qu'effacés : l'ordre est
l'inverse de celui annoncé (rééchantillonner d'abord, sans quoi il faudrait un
tampon intermédiaire et reconquérir l'indépendance à la taille de bloc), et
l'étireur est le vocodeur et non le WSOLA — le § 8 avait été écrit avant que
D12.8 ne tranche.

**CE QUE LE BANC MESURE**, écrit avant la première mesure :

| demandé | attendu | mesuré | écart |
|---|---|---|---|
| +12 demi-tons | 880 Hz | **880,00 Hz** | 0,00 cent |
| −12 demi-tons | 220 Hz | **220,00 Hz** | −0,00 cent |
| +7 demi-tons | 659,26 Hz | **659,30 Hz** | 0,12 cent |

La durée ne bouge dans aucun des trois — c'est là que `Repitch` échoue par
construction, et c'est toute la raison d'être de cette phase. Le banc vérifie
aussi que **zéro demi-ton laisse le chemin d'avant au bit près** (ni étireur ni
magasin enveloppant : un réglage neutre qui change le son est un réglage cassé)
et que le rendu est **identique à 128, 512 et 2 048 échantillons par bloc** —
l'invariant n° 3, sans lequel la lecture et `vsm-render` ne produiraient pas le
même fichier.

**ET LE BANC A ÉTÉ CASSÉ EXPRÈS POUR VOIR S'IL MORD.** `preparePitchedSpans`
désactivée, les trois mesures tombent à 702, 348 et 550 Hz — c'est-à-dire au
bord de la fenêtre de recherche — et les trois tests échouent. Un banc qui
passe du premier coup mérite qu'on vérifie qu'il mesure quelque chose. Le même
traitement a été appliqué à l'aller-retour de sérialisation : la lecture du
champ neutralisée, le test tombe.

**DE BOUT EN BOUT, PAR L'APPLICATION.** Le geste (`hauteur-clip:+12`), le
projet enregistré, l'export relu au spectre : **440,02 / 880,02 / 329,65 Hz**, à
un dixième de cent, sur trois fichiers de 232,5 s tous de la même longueur. Ce
n'est pas le rendu qui est mesuré, c'est le fichier — la leçon de D49.

**DEUX BORNES, DÉCIDÉES ET ÉCRITES PLUTÔT QUE DEMANDÉES.**

- **±24 demi-tons** : au-delà, monter de trois octaves lit le matériau huit fois
  plus vite et l'étire d'autant, ce qui n'est plus une transposition mais un
  effet.
- **Le mode `Repitch` la refuse, et le menu grise le geste EN DISANT POURQUOI**
  (« Hauteur du clip — suit la durée (mode Rééchantillonné) »). Dans ce mode la
  hauteur est une conséquence du tempo, pas un réglage ; lui ajouter une hauteur
  indépendante demanderait une seconde étape d'étirement, c'est-à-dire ce que ce
  mode existe pour éviter. Un geste qui ne ferait rien sans le dire serait pire
  que pas de geste.

**DEUX DÉFAUTS TROUVÉS EN VÉRIFIANT, ET CORRIGÉS DANS LA FOULÉE.**

1. **Le geste posait la hauteur sur les clips MIDI aussi.** Le sous-menu ne
   s'offre que sur un clip audio, mais la sélection, elle, peut en couvrir
   d'autres — et « tout choisir » les couvre toutes. Vu sur la première trace :
   « la440 +12,00, guitar +12,00, other +12,00… ». Un champ écrit dans le projet
   pour ne rien faire est un mensonge à retardement ; le geste ne touche plus
   que les pistes audio.
2. **LE CARTOUCHE DE GAIN DE D22.1 ÉTAIT INVISIBLE SUR TOUT CLIP UN PEU LONG.**
   Il était posé au bord DROIT du rectangle du clip : sur une prise plus large
   que la vue — c'est-à-dire sur toute prise de plus de quelques mesures — ce
   bord est hors de l'écran, et le gain ne s'affichait **nulle part**. Trouvé en
   cherchant mon propre cartouche de hauteur sur une capture où il n'y était
   pas. Borné à la fenêtre, il reste sur le clip et se voit toujours : la
   capture montre maintenant « −3.0 dB » sur les six clips et « +12 st » sur le
   seul clip audio.

Tests : 1 291 audio, 319 core, 287 interchange, 25 clap, 11 panels — verts
(six bancs neufs pour la hauteur d'un clip, un aller-retour de sérialisation
avec ses cents).

### Phase D55 — Le vingt-troisième audit : ce que « enregistrer puis rouvrir » perd, champ par champ (09/09/2026, 16:25)

**POURQUOI CETTE LUNETTE-LÀ.** Les audits précédents ont cherché des gestes
absents (D32, D33), puis du code que personne n'appelle (D35), puis des valeurs
qu'un seul panneau relisait (D37). Il reste une famille qu'aucun n'a couverte,
et c'est celle qui coûte le plus cher à l'utilisateur : **un réglage qu'on pose,
qu'on enregistre, et qui n'est plus là quand on rouvre**. Elle ne se voit pas en
lisant le code — le champ EXISTE, le geste EXISTE, la sauvegarde RÉUSSIT — et
elle ne se découvre qu'à l'usage, longtemps après, quand plus personne ne sait
ce qui l'a mangé.

**LA MÉTHODE : LE MODÈLE EST LA LISTE DE CONTRÔLE.** Plutôt que de deviner quel
champ pourrait manquer, on les prend TOUS. Un projet dont chaque champ de
`Project`, de `Track`, de `Clip`, de `Take`, de `Marker`, de `SendBusDescription`,
de `TrackEffect`, de `MidiEffect`, d'`AutomationPoint` et de `WarpMarker` porte
une valeur DISTINCTIVE (jamais son défaut, sans quoi « conservé » ne prouverait
rien), écrit par le chemin réel de l'application (`saveProjectBundle`), relu par
le chemin réel (`loadProjectBundle`), puis comparé champ par champ.

**CE QUE L'AUDIT A RENDU : 122 champs éprouvés, 120 conservés, 2 non — et LES
DEUX SONT DES DÉCISIONS DÉJÀ PRISES**, pas des pannes.

| champ | verdict |
|---|---|
| `Track::armed` | **volontairement absent.** D22.5 l'a déjà écrit : l'armement est « un état de séance », au même titre que le varispeed de D18.5 qui n'est « écrit NULLE PART dans le projet ». Rouvrir un projet dont une piste s'arme toute seule est un piège, pas un service |
| `Clip::id` | **régénéré, et c'est équivalent.** L'identifiant d'un clip est une poignée de séance (sélection, `clipById`) ; rien dans le fichier ne le référence, et `assignClipIds()` en pose un neuf à l'ouverture. Le LIEN entre deux clips, lui, ne passe pas par l'identifiant mais par la fenêtre (`clipIsShared` compare `sourceStart`/`sourceLength`) : il survit donc à l'aller-retour, ce que l'audit vérifie aussi |

**L'AUDIT A ÉTÉ CASSÉ EXPRÈS POUR VOIR S'IL MORD**, comme celui de D54. Deux
sabotages dans le sérialiseur — `Clip::gain` non écrit, `midiInputChannel`
forcé à zéro — font tomber **trois** assertions (le gain est éprouvé sur le clip
d'une piste ET sur celui d'une prise), et le compte passe de 2 à 5 perdus. Un
banc qui passe du premier coup mérite qu'on vérifie qu'il mesure quelque chose.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D55.1 | **L'aller-retour champ par champ devient un test permanent.** Le banc ci-dessus a trouvé zéro défaut aujourd'hui ; sa valeur n'est pas là. Elle est dans le PROCHAIN champ ajouté au modèle et oublié dans le sérialiseur — le défaut de D51 et de D53, deux fois le même — qu'il fera tomber le jour où il est écrit | un test d'`interchange/` qui construit le projet distinctif, l'écrit, le relit et compare les 122 champs ; les deux non-conservés y sont écrits comme des décisions, avec leur raison, et non passés sous silence |
| D55.2 | **La recette de l'assemblage des prises est jetée après usage.** `TakeCompComponent` détient la liste des tronçons — « de la mesure 1 à 4, la prise 2 » — et son propre en-tête promet qu'on peut « corriger une frontière sans avoir à tout refaire ». C'est faux : `setTake` vide la liste À CHAQUE ouverture du panneau, même sur la même piste, et rien ne l'écrit dans le projet. On compose, on écoute, on rouvre pour déplacer une frontière — et l'on retape tout | les tronçons vivent dans `Track`, sont écrits dans `project.json` quand il y en a (et seulement alors : un projet sans assemblage garde son fichier octet pour octet), et le panneau les RELIT au lieu de les vider |

**CE QUI EST ATTENDU DE D55.2, ÉCRIT AVANT LA MESURE.**

1. Deux tronçons posés, « Composer » cliqué, le panneau fermé puis rouvert :
   j'attends **2** tronçons affichés, là où la mesure d'aujourd'hui en donne
   **0**. C'est le geste exact que l'en-tête du panneau promet.
2. Le même après enregistrement et réouverture du projet : **2** encore.
3. Un projet sans assemblage garde son `project.json` **octet pour octet** —
   vérifié par comparaison des deux fichiers, pas par lecture.
4. Un tronçon qui désigne une prise disparue est **écarté en le disant** au
   rapport d'import, jamais gardé pointant à côté.

> **LA PHASE D55 EST FAITE (09/09/2026, 16:45), et les quatre attendus de
> D55.2 sont tenus.**
>
> **D55.1 — LE BANC EST DEVENU UN TEST** (`interchange/tests/test_project_roundtrip.cpp`).
> Six pistes, une par forme que le modèle sait prendre — MIDI chargée, audio
> avec ses clips, publiée, dossier, membre de dossier, groupe — et 122 champs
> comparés après un aller-retour par le disque. Les deux qui ne survivent pas
> y sont AFFIRMÉS plutôt que tus : le jour où l'armement se mettrait à
> survivre, le test tombe et l'on décide, au lieu de le découvrir en
> s'étonnant qu'une piste s'arme toute seule.
>
> **UN DÉTAIL QUE LA MESURE A OBLIGÉ À ÉCRIRE, ET QUI ÉTAIT UN FAUX DÉFAUT.**
> La confiance d'une note revient à **0,500008** pour 0,5 demandé. Ce n'est pas
> une perte mais la traversée d'un entier 16 bits dans le bloc privé du SMF
> (1/65535). Le test le dit dans sa tolérance, plutôt que de prétendre à
> l'exactitude d'un flottant qui ne traverse pas le fichier — et un premier
> jet, avec sa tolérance à 1e-6, l'avait compté comme un champ perdu.
>
> **DEUX AUTRES FAUSSES ALERTES, DITES PARCE QU'ELLES INSTRUISENT.**
> `Track::instrumentId` est d'abord ressorti « PERDU » : le banc demandait la
> machine « minimoog » quand le registre la nomme `vsm.minimoog`, et le
> chargeur avait raison de refuser de deviner (il le SIGNALE, c'est la règle de
> D18.7). Et `Clip::id` a été suspecté de casser les copies liées, jusqu'à ce
> que la lecture de `clipIsShared` montre que le lien passe par la FENÊTRE et
> non par l'identifiant — le test le vérifie maintenant sur deux clips
> réellement liés.
>
> **D55.2 — LA RECETTE DE L'ASSEMBLAGE VIT DANS LA PISTE.**
> `Track::compSegments`, écrite dans `project.json` sous la clé `comp` **et
> seulement quand il y en a une**. Le panneau la RELIT au lieu de la vider.
>
> | | avant | après |
> |---|---|---|
> | panneau rouvert dans la même séance (fermer, rouvrir) | **0 tronçon** | **2** |
> | projet enregistré puis rouvert | **0** | **2** |
> | `comp` dans le fichier d'un projet sans assemblage | absent | **absent** |
> | version du fichier | 2 | **2** |
>
> Le témoin est le MÊME BINAIRE sur un projet écrit avant l'étape : il ouvre
> à `0 au panneau, 0 sur la piste`, et rien n'est inventé. C'est aussi
> exactement ce que faisait le code d'avant, dont la piste ne pouvait rien
> porter.
>
> **LA VERSION DU FICHIER NE MONTE PAS, ET C'EST UNE DÉCISION.** Le suivi de
> tempo (D12), l'inversion (D13.4) et la transposition d'un clip (D54) l'ont
> fait monter parce qu'ils changent CE QU'ON ENTEND : un lecteur ancien qui les
> ignore joue autre chose sans le dire. Une recette d'assemblage ne change rien
> à ce qu'on entend — le matériau composé est déjà dans les notes. Un lecteur
> qui l'ignore joue le même morceau ; il perd seulement le moyen de recomposer
> autrement. Faire monter la version pour cela rendrait illisibles, chez les
> autres, des projets qui sonnent pareil.
>
> **ET LA RECETTE EST POSÉE PAR « COMPOSER », PAR LUI SEUL.** Les tronçons
> qu'on ajoute et retire avant de composer restent dans le panneau : écrire
> dans le projet une recette qui ne décrit pas le matériau présent donnerait un
> fichier qui se contredit. Ce que la piste porte décrit donc toujours ce qu'on
> entend — et c'est posé dans la MÊME édition annulable que le matériau, sans
> quoi annuler l'assemblage laisserait une recette orpheline.
>
> **UN TRONÇON QUI DÉSIGNE UNE PRISE ABSENTE EST ÉCARTÉ ET NOMMÉ** au rapport
> d'import (« 2 tronçon(s) d'assemblage écarté(s) »), bornes vides comprises.
> Le commentaire du panneau annonçait déjà cette règle (« des tronçons qui
> désignent des prises disparues ne désignent rien ») ; elle est passée du
> commentaire au code, et à l'endroit qui peut la DIRE.
>
> **CE QUE L'ÉCRAN A OBLIGÉ À AJOUTER.** L'autoportrait ne prend que la fenêtre
> socle : un panneau FLOTTANT n'y figure pas. Et la capture d'écran du système
> rend, sous XWayland, une fenêtre au cadre correct et au **contenu blanc** —
> le contenu JUCE n'est pas dans le pixmap que le compositeur donne (mesuré :
> `spectacle -b -n -f` rend une image de moyenne 0, `-a` rend le cadre
> « Assembler les prises » vide). Un panneau flottant aurait donc été
> « invérifiable faute d'écran », ce que ce projet s'interdit de dire depuis
> D7.4. `VSM_CAPTURE_PANNEAUX=1` photographie désormais **chaque fenêtre
> flottante visible**, par le rendu hors écran de l'application elle-même, une
> image par panneau. La capture montre, sur un projet rouvert : « mesures 1 à 3
> → Passe 2 », « mesures 3 à 5 → Passe 3 ».
>
> Tests : 1 291 audio, 319 core, **292 interchange** (5 neufs), 25 clap,
> 11 panels — tous verts.

### Phase D56 — « Le .mid portera tout ce qui est joué » : l'application le dit, et c'est faux dès qu'une piste est découpée (09/09/2026, 16:45)

**LA MÊME LUNETTE QUE D55, D'UN CRAN PLUS LOIN.** D55 a demandé ce qu'un
aller-retour par le disque perd. Cette phase demande ce qu'un aller-retour par
un AUTRE LOGICIEL perd : ce que le `.mid` exporté ne porte pas de ce qu'on
entend. La question n'est pas cosmétique — un export MIDI sert précisément à
donner son travail à quelqu'un d'autre, et un fichier qui ne joue pas ce que
l'auteur entendait est un fichier faux.

**D31.5 AVAIT DÉJÀ POSÉ LA RÈGLE — et l'a appliquée à deux causes sur six.**
L'export prévient quand une piste porte une chaîne d'effets MIDI ou une
transposition ; sinon il affiche « **le .mid portera tout ce qui est joué** ».
Cette phrase est FAUSSE dès qu'une piste est découpée en clips, et c'est le cas
de tout arrangement.

**MESURÉ, sur une piste de huit notes (une par mesure) dont un seul clip montre
les mesures 3 et 4, posé à la mesure 1 et long de deux fenêtres — donc bouclé
une fois :**

| | notes |
|---|---|
| matériau de la piste | 8 |
| ce que la LECTURE joue (`PlaybackScheduler`) | **4** |
| ce que l'EXPORT écrit (`toParsedFile`) | **8** |

Et l'application, ce projet n'ayant ni effet MIDI ni transposition, annonce que
le fichier portera tout ce qui est joué. Il porte quatre notes que personne
n'entend, et perd la reprise de la boucle.

| Étape | Contenu | Terminé quand |
|---|---|---|
| D56.1 | **L'export écrit l'ARRANGEMENT, clips compris** : la fenêtre de chaque clip, ses répétitions, sa fin qui coupe les notes qui pendent, et les clips muets exclus. Par la MÊME fonction que la lecture, déplacée dans `core/` — deux calculs de passage finiraient par diverger, et c'est justement de cette divergence que la phase parle | `clipPassages` dans `ClipEdit`, appelée par `PlaybackScheduler` ET par un export « tel qu'arrangé » ; sur la mesure ci-dessus, le `.mid` porte **4** notes aux positions jouées |
| D56.2 | **L'avertissement devient complet, et sa phrase rassurante conditionnelle.** Restent non portées par le format : les effets MIDI, la transposition de piste, le muet/solo/désactivé, le muet venu d'un dossier, le décalage de piste. Chacune est NOMMÉE ; « le .mid portera tout ce qui est joué » ne s'affiche que lorsque la liste est vide | l'avertissement nomme les cinq causes, piste par piste ; la phrase rassurante n'apparaît plus que quand elle est vraie |

**CE QUI EST BAKÉ ET CE QUI EST DIT — LA DÉCISION, ÉCRITE AVEC SA RAISON.**
Le clip est l'ARRANGEMENT : il dit où le matériau se trouve dans le temps, et
c'est exactement ce qu'un fichier MIDI sait porter. Le reste — une chaîne
d'effets, une transposition de piste, un muet, un décalage — est un PROCESSUS
DE LECTURE ou un ÉTAT DE MIXAGE : le cuire dans le fichier ferait dépendre
l'export du bouton sur lequel on a appuyé une minute plus tôt, et D31.5 a déjà
tranché en offrant un geste explicite (« Reporter les effets MIDI dans les
notes ») plutôt qu'une cuisson silencieuse. C'est aussi la coupure que font
Cubase et Live : leur export MIDI écrit les parties telles qu'arrangées, et
laisse les paramètres de piste au projet.

**CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE.**

1. Sur la mesure ci-dessus, le `.mid` exporté porte **4** notes, aux ticks 0,
   1920, 3840 et 5760 — les deux passages du clip —, et non 8.
2. **Un projet SANS clip exporte le fichier d'avant, octet pour octet.** Une
   piste sans clip donne un passage identité, ce qui est déjà la règle de la
   lecture : il n'y a pas un chemin historique à côté du chemin des clips.
3. La note dont la fin dépasse la fin du clip est **coupée** à cette fin, comme
   à la lecture — sans quoi elle resterait tenue pour toujours chez celui qui
   ouvre le fichier.

> **LA PHASE D56 EST FAITE (09/09/2026, 16:55), et les trois attendus sont
> tenus.**
>
> **D56.1 — L'EXPORT ÉCRIT L'ARRANGEMENT.** `Project::toParsedFileArranged()`,
> à côté de `toParsedFile()` qui reste le matériau — c'est lui qu'il faut pour
> `midi/arrangement.mid` dans un dossier de projet, où les clips l'accompagnent
> dans `project.json`. Les deux partagent leur corps : un seul `buildParsedFile`
> avec un drapeau, parce que deux écritures du même fichier finiraient par
> diverger.
>
> **ET LES PASSAGES ONT DÉMÉNAGÉ DANS `ClipEdit`.** Ils vivaient privés dans
> `PlaybackScheduler.cpp` ; l'export en avait besoin. Les recopier aurait donné
> deux calculs de passage — c'est-à-dire, à la première correction de l'un,
> exactement la divergence entre ce qu'on entend et ce qu'on exporte que cette
> phase corrige.
>
> | | attendu | mesuré |
> |---|---|---|
> | notes du matériau | — | 8 |
> | notes jouées (`PlaybackScheduler`) | — | 4 |
> | notes exportées AVANT | — | **8** |
> | notes exportées APRÈS | 4 | **4**, aux ticks 0, 1920, 3840, 5760 |
> | projet sans clip, fichier écrit | identique | **identique, octet pour octet** |
> | note qui dépasse la fin du clip | coupée à 1920 | **1920** |
> | clip muet | rien | **0 note** |
>
> **LE FICHIER EST RELU, PAS LE RENDU** — la leçon de D49. `VSM_EXPORT_MIDI=f.mid`
> écrit le projet entier par la même fonction que « Fichier ▸ Exporter MIDI… » ;
> l'export complet ne s'atteignait qu'à la souris, si bien que ce qu'il ÉCRIT
> n'avait jamais été relu par une vérification — seulement ce qu'il ANNONCE, et
> c'est justement l'écart entre les deux dont parle cette phase. Le `.mid` relu
> donne, piste par piste : **Découpée 4** (0, 1920, 3840, 5760), Muette 8,
> Décalée 8, Transposée 8, Ordinaire 8.
>
> **LE BLOC PRIVÉ DE D6.3 N'EST PAS ÉCRIT SUR UNE PISTE RÉARRANGÉE**, et c'est
> une décision : il retrouve ses notes muettes et ses confiances par leur tick
> de MATÉRIAU, et ces ticks n'existent plus dans un fichier où les clips ont été
> appliqués — une note bouclée y figure même deux fois. Un bloc qui pointe à
> côté serait pire que pas de bloc. Une piste sans clip garde le sien, et c'est
> ce qui laisse le témoin sans clip identique octet pour octet ; le test le
> vérifie sur un projet qui porte justement une note muette et une confiance.
>
> **D56.2 — L'AVERTISSEMENT COUVRAIT DEUX CAUSES SUR SIX.** Il nomme désormais
> les effets MIDI, la transposition, le muet — en disant D'OÙ il vient : la
> piste, le solo d'une autre, ou son dossier — la piste désactivée et le
> décalage. Mesuré sur un projet à cinq pistes : « **Muette (piste muette) ;
> Décalée (décalage -25.0 ms) ; Transposée (transposition +5)** ». La piste
> découpée n'y est plus, puisque l'export la porte maintenant ; et la phrase
> « le .mid portera tout ce qui est joué » ne s'affiche que lorsqu'elle est
> vraie.
>
> **LE BANC A ÉTÉ CASSÉ EXPRÈS.** `toParsedFileArranged` renvoyée au matériau,
> **trois** tests sur quatre tombent (8 notes au lieu de 4, fin à 3840 au lieu
> de 1920, 8 notes pour un clip muet) — et le quatrième, celui du témoin sans
> clip, passe encore : c'est exactement ce qu'il doit faire, puisqu'il affirme
> que les deux chemins coïncident quand il n'y a pas de découpe.
>
> Tests : 1 291 audio, **323 core** (4 neufs), 292 interchange, 25 clap,
> 11 panels — tous verts.

### Phase D57 — Le tiroir des prises s'empilait depuis D3.5 et rien ne le dépilait (09/09/2026, 17:05)

**LE GESTE MANQUANT, TROUVÉ EN CHERCHANT AUTRE CHOSE.** D55.2 vient de poser la
recette de l'assemblage sur la piste. En vérifiant qu'un tronçon désignant une
prise absente est bien écarté, une question s'est imposée : **comment une prise
peut-elle devenir absente ?** `pushTake` empile depuis D3.5 ; `selectTake`
choisit ; **rien ne retire**. `grep -rn "removeTake\|takes.erase"` rend zéro
occurrence dans tout le dépôt. Le tiroir d'une piste qu'on retravaille grossit
donc à chaque passe, et les passes ratées y restent pour toujours. C'est un
geste banal de tout DAW — les lanes de Cubase, les takes de Live se
suppriment.

**ET LE POINT DÉLICAT N'EST PAS LA SUPPRESSION, CE SONT LES INDEX.**
`Track::compSegments` désigne les prises par leur RANG. Retirer la prise n° 1
sans toucher aux tronçons ferait recomposer avec la n° 2 sous le nom de la
n° 1 : un mensonge silencieux, et exactement la faute que D35 a payée sur les
dossiers (« `moveTrack` répare méticuleusement les index… et n'est appelée par
aucun geste »). Ici l'index existe depuis une heure, et le geste qui le casse
arrive en même temps que lui.

**LA RÈGLE, ÉCRITE AVEC SA RAISON.** Les tronçons qui désignaient la prise
retirée sont **écartés** ; ceux qui désignaient une prise située après elle
**reculent d'un rang** ; les deux nombres sont rendus à l'appelant, qui les
DIT. Et **la prise active est détachée, pas détruite** : quand on retire celle
qu'on entend, le matériau RESTE sur la piste et `activeTake` passe à -1 — il
n'appartient plus à aucune passe, comme après un assemblage. Ce qu'on retire,
c'est l'entrée du tiroir. Faire disparaître ce qu'on écoute sans prévenir
serait la pire des surprises.

**MESURÉ DE BOUT EN BOUT, PAR L'APPLICATION**, sur le projet à trois passes de
D55.2 : deux tronçons posés (mesures 1 à 3 → Passe 2, mesures 3 à 5 → Passe 3),
composés, puis « Passe 2 » retirée.

> Prise « Passe 2 » retirée du tiroir. **1 tronçon(s) d'assemblage la
> désignaient : retirés. 1 tronçon(s) ont reculé d'un rang.**

Le projet enregistré puis rouvert montre, dans le panneau : **« mesures 3 à 5 →
Passe 3 »** — le tronçon qui pointait le rang 2 pointe maintenant le rang 1, et
nomme toujours la MÊME passe. C'est la capture qui le prouve, pas le compteur.

**UN DÉFAUT TROUVÉ PAR LA VÉRIFICATION ELLE-MÊME, ET CORRIGÉ.** La première
mesure a rendu « **2 au panneau, 1 sur la piste** ». Le panneau d'assemblage ne
relisait la piste qu'à son OUVERTURE : retirer une prise, en choisir une autre
— il marque « celle qu'on entend » — ou composer le laissaient sur ce qu'il
avait lu. Une valeur, deux endroits, un seul qui la relit : la faute que D37 a
nommée. `refreshTakeCompPanel()` est appelée par les trois gestes, et la mesure
donne maintenant « 1 au panneau, 1 sur la piste ».

**ET UN SECOND, DANS L'OUTIL DE VÉRIFICATION DE D55.2.**
`VSM_CAPTURE_PANNEAUX` nommait ses images d'après le TITRE de la fenêtre — or
une boîte d'alerte reprend celui du panneau qui l'a ouverte, si bien que la
seconde écrasait la première sans un mot. J'ai cherché la capture du panneau et
trouvé celle de l'alerte. Le rang est désormais dans le nom.

**LE BANC A ÉTÉ CASSÉ EXPRÈS.** Le recul des index supprimé, le test des
tronçons tombe (`shiftedSegments` 0 au lieu de 1) ; les trois autres passent,
ce qui est juste — ils portent sur le matériau, la prise active et les bornes.

Tests : 1 291 audio, **327 core** (4 neufs), 292 interchange, 25 clap,
11 panels — tous verts.

> **DEUX LUNETTES REPASSÉES LE 09/09/2026, SANS RIEN TROUVER — et l'écrire
> évite de les repasser une troisième fois.**
>
> 1. **Celle de D35, « ce qui existe et que personne n'appelle »**, rejouée sur
>    les 285 fonctions déclarées dans les en-têtes publics de `core/` et
>    `audio/` : **zéro orpheline**. Les huit candidates d'un premier passage
>    étaient toutes des faux positifs — six appels depuis d'autres EN-TÊTES que
>    la recherche n'incluait pas, et deux variables locales (`gPow`, `tiny`)
>    que l'expression régulière avait prises pour des déclarations. Un « zéro »
>    sorti d'un grep se revérifie en listant ce qu'on a cherché ET où : la règle
>    du § « Pièges payés » a servi ici contre son auteur.
> 2. **« Ce qu'on peut créer et jamais retirer »**, la lunette qui a donné D57 :
>    les prises étaient le seul cas. Repères, bus de départ, inserts, effets
>    MIDI, marqueurs de warp, points d'automation, associations MIDI Learn,
>    pistes et clips ont tous leur geste de retrait.

### Phase D58 — `VSM_TAILLE` ne faisait rien depuis le 31/08, et l'image obtenue ressemblait assez à celle qu'on attendait pour que personne ne le voie (09/09/2026, 18:00)

**COMMENT CELA S'EST TROUVÉ.** En ouvrant la plus grosse reconstruction réelle
du dépôt — `usandthem-parite-v3`, **onze pistes** : sept MIDI, deux audio, deux
groupes — pour la regarder à 1600x1000. La capture est sortie à **1264x742**.
Quatre valeurs différentes de `VSM_TAILLE`, mesurées :

| demandé | image obtenue |
|---|---|
| (absent) | 1264x742 |
| 800x600 | **1264x742** |
| 1280x800 | **1264x742** |
| 1600x1000 | **1264x742** |

**LE RÉGLAGE NE FAISAIT RIEN DU TOUT.** `showFloatingPanels()` recouvre la
fenêtre socle par l'écran de travail entier en mode « fenêtre unique »
(`socle->setBounds(screenArea.reduced(8))`) — le défaut depuis le commit du
31/08 « La fenêtre unique devient le défaut » —, et cette ligne s'exécute
**juste après** que `Main.cpp` a posé la taille demandée. 1264, c'est
`(1920 − 16) / 1,5` : la largeur de l'écran moins la marge, divisée par
l'échelle d'interface. Tout autoportrait de ce document pris depuis le 31/08 a
la taille de l'écran, quoi qu'ait demandé son auteur.

**CE QUE CELA A COÛTÉ, ET IL FAUT LE DIRE.** D22.4 écrit « à 1 280 px logiques,
« Ouvrir MIDI… » et « Exporter MIDI… » tiennent encore ». La vérification a en
réalité eu lieu à **1 264** px. L'affirmation reste vraie — à seize pixels près,
et par hasard : si l'écran de développement avait été plus large, elle aurait
été fausse et personne ne l'aurait su. **Un réglage de vérification qui promet
et ne fait rien est pire qu'un réglage absent**, parce qu'il fait écrire des
chiffres qu'on n'a pas mesurés.

**CE QUI EST FAIT.**

1. **`VSM_TAILLE` gagne** : la disposition en fenêtre unique ne recouvre plus la
   taille imposée. Mesuré après correction : 900x660 demandé → **900x660**,
   1280x800 → **1280x742**, 1600x1000 → **1280x742**.
2. **Elle DIT ce qu'elle a demandé ET ce qu'elle a obtenu**, ce dernier lu sur
   **l'image écrite** et non sur le composant — c'est la leçon de D49, et elle
   a servi tout de suite : interrogé juste après le redimensionnement, le
   composant rendait encore 1600x1000 alors que l'image faisait 1280x742. Seule
   l'image dit la vérité. Le plafond est l'écran **divisé par l'échelle
   d'interface** : à 150 % sur un écran de 1920, il vaut 1280.
3. **Un PLANCHER, mesuré et non choisi** : `setResizeLimits(900, 660, …)`.
   Rien n'en fixait, et l'on pouvait réduire la fenêtre jusqu'à ce que la
   façade de machine perde **toutes** ses légendes — à 800x600 logiques,
   « OSCILL… » et des rangées de points à la place des noms de potentiomètres.
   À 900x660 elles se lisent encore, abrégées. La lisibilité prime sur « ça
   tient dans la case ». Une demande sous le plancher est **remontée en le
   disant** ; un écran plus petit se règle par *Affichage ▸ Taille de
   l'interface*, pas en rendant l'application illisible.

**CE QUE LA PREMIÈRE VRAIE PETITE FENÊTRE A MONTRÉ, ET QUI RESTE.** Au plancher,
le bandeau de la tranche master écrit « phase 1.00 » et « -inf LUFS »
**par-dessus** sa dernière rangée de potentiomètres. Ce n'est pas une
régression : c'est une disposition que personne n'avait jamais vue, faute d'un
réglage qui marche. Elle est **nommée ici plutôt que corrigée en silence dans
la même phase**, avec son seuil : à 1024x742 le recouvrement disparaît.

**ET LA RECONSTRUCTION À ONZE PISTES S'OUVRE.** C'est la première fois qu'un
projet réel de cette taille est ouvert et regardé — D40 avait mesuré à 64
pistes, mais sur un projet SYNTHÉTIQUE. Onze tranches au mélangeur, onze lignes
dans l'arrangement, chaque piste avec sa machine (`vsm.vocal` sur la basse,
`vsm.divider`, `vsm.tb303`, deux `vsm.multisample`, `vsm.drums`) et les deux
groupes en fin de liste. `diagnostic-export-midi` rend « le .mid portera tout
ce qui est joué », ce qui est vrai depuis D56 : aucune de ces pistes n'est
muette, décalée ni transposée.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts
(la phase ne touche qu'`app/Source/`, dont aucune suite ne traverse le point
d'entrée : c'est la mesure à l'écran qui la tranche, et elle est ci-dessus).

### Phase D59 — La septième commande du master n'était visible à AUCUNE taille de fenêtre, et c'est celle que D49 a rendue honnête (09/09/2026, 18:10)

**D58 A NOMMÉ CE DÉFAUT ET NE L'A PAS CORRIGÉ, EXPRÈS.** Il valait sa phase :
en le regardant, il s'est révélé bien pire que « le bandeau du master déborde
en petite fenêtre ».

**LA TRANCHE MASTER PORTE SEPT POTENTIOMÈTRES** — LOW, MID, HIGH, COMP, RATIO,
SAT et **CEIL**, le plafond du limiteur. Sur la capture pleine taille d'un
projet réel, on en voit six. Le septième est dessiné **à moitié hors de la
tranche, sans son libellé**, et « phase 1.00 » et « -inf LUFS » s'écrivent
par-dessus. Ce n'est pas un défaut de petite fenêtre : la tranche reçoit
**221 pixels quelle que soit la taille de la fenêtre**, le dock du bas ayant
sa propre hauteur.

**CE QUE CELA VEUT DIRE.** Le plafond du limiteur est le réglage que D47 à D50
ont passé quatre phases à rendre honnête — l'export qui écrêtait, le mètre mort
par défaut, le remède qui produisait un fichier écrêté, les stems silencieux.
**Et sa commande était inatteignable à la souris depuis toujours.** Une
commande qu'on ne peut pas atteindre est pire qu'une commande absente, pour la
raison exacte de D35.5 : elle promet.

**POURQUOI PERSONNE NE L'AVAIT VU.** Le commentaire de `MasterStrip::resized()`
constatait déjà le mécanisme — « la grille de knobs a une hauteur fixe et
déborde par le bas quoi qu'on réserve » — et la conclusion tirée à l'époque
avait été de faire partager une LIGNE à la phase et à la saturation. Le
débordement, lui, est resté. Et il n'a jamais été vu parce que `VSM_TAILLE` ne
faisait rien (D58) : tous les autoportraits étaient pris à la taille de
l'écran, où l'on ne cherche pas ce qui manque en bas d'une tranche de deux
cents pixels.

**MESURÉ, avec le témoin issu du même binaire** (une hauteur de rangée en
option dans le code, mesurée puis retirée) :

| hauteur de rangée | place disponible | libellés hors de la tranche | pire dépassement |
|---|---|---|---|
| **46 px (avant)** | 135 px | **1** | **13 px** |
| **34 px (après)** | 135 px | **0** | **0** |

La rangée n'est plus une constante : elle vaut `place / rangées`, bornée à
[34, 46]. **Le plancher de 34 est une question de lisibilité**, pas de place :
en dessous, l'étiquette de 12 points et son potentiomètre ne cohabitent plus.
C'est la règle de la mémoire de lisibilité — agrandir la case, jamais
rétrécir le texte —, et ici la case ne peut pas grandir, alors le nombre de
rangées visibles ne diminue pas non plus : elles se resserrent jusqu'au
plancher, et le dock est empêché de descendre plus bas.

**ET LE DOCK DU BAS EST BORNÉ PAR CE QUE LA CONSOLE RÉCLAME.** Il pouvait
descendre à 120 px : `MixerComponent::hauteurMinimale()` rend ce que la tranche
master demande (`MasterStrip::hauteurUtile()`), et `MainComponent` en fait le
plancher de la poignée. Sans cela, tirer la poignée vers le bas aurait fait
réapparaître le défaut, et la correction n'aurait tenu qu'à la hauteur par
défaut. Le surcoût du dock — barre d'onglets et marges — est **mesuré et non
estimé** : le mélangeur reçoit 221 px quand le dock en fait 282.

**VU À L'ÉCRAN, aux deux bouts** : à 900x660 comme en plein écran, les sept
potentiomètres sont là, chacun avec son libellé, et les deux témoins du bas
sont sous eux et non dessus.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts.

### Phase D60 — Le premier balayage à la taille plancher : deux aides coupées au milieu de la clause qui compte (09/09/2026, 18:20)

**LA PREMIÈRE FOIS QUE LES ONGLETS DU BAS SONT REGARDÉS AILLEURS QU'EN PLEIN
ÉCRAN.** `VSM_TAILLE` marchant enfin (D58), les cinq onglets ont été
photographiés à 900x660, la taille plancher, sur la reconstruction réelle à
onze pistes. Trois tiennent — Mixer, Effets, Automation, Liste. **Deux coupent
leur bandeau d'aide au milieu d'un mot** :

| onglet | ce qu'on lisait à 900 px |
|---|---|
| Tempo | « … rampe jusqu'au suivant - **le tempo de départ (tick 0) ne bouge qu'en v…** » |
| MIDI CC | « … Clic droit : supprimer - **un CC vaut jus…** » |

**ET CE SONT LES CLAUSES QUI COMPTENT.** Les trois premières de chaque bandeau
énoncent l'évident — clic, glisser, clic droit. La dernière est la seule qu'on
vient lire : elle explique un comportement qui SURPREND (le tempo du tick 0 se
déplace en valeur mais pas en temps ; un CC vaut jusqu'au suivant et
n'interpole pas). `juce::Label` coupe par la fin ; la coupure emporte donc
exactement le renseignement.

**LA RÈGLE DE CETTE APPLICATION EST D'AGRANDIR LA CASE, JAMAIS DE RÉTRÉCIR LE
TEXTE.** Le bandeau prend désormais **deux lignes quand une ne suffit pas** (42
px au lieu de 26), et le graphe commence d'autant plus bas. La hauteur d'en-tête
était écrite DEUX fois — 26 dans `resized()`, 30 en dur dans `editorArea()` —
et les deux auraient divergé au premier changement : il n'y a plus qu'un
`hauteurEnTete_`, que les deux consultent.

**MESURÉ AUX DEUX BOUTS, sur les mêmes onglets et le même projet** : à 900 px
les deux aides se plient et se lisent en entier ; à 1264 px elles tiennent sur
une ligne et le bandeau reprend ses 26 pixels — aucune régression sur la taille
où tous les autoportraits antérieurs ont été pris.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts.

### Phase D61 — Le balayage au plancher, suite : la barre du piano roll perdait ONZE commandes, à toute taille de fenêtre (09/09/2026, 23:40)

**CE QUE D60 N'AVAIT PAS REGARDÉ.** Le premier balayage à 900x660 s'était
arrêté aux cinq onglets du dock. Le HAUT de la fenêtre — arrangement, piano
roll, rack de machines — n'avait jamais été photographié ailleurs qu'en plein
écran. Il l'a été, sur la même reconstruction réelle à onze pistes.

#### 1. La barre du piano roll posait à ZÉRO PIXEL ce qui ne tenait pas

`PianoRollToolbar::resized()` posait chacune de ses trois bandes sur UNE
rangée, à coups de `removeFromLeft(largeur)` avec des largeurs constantes.
Quand la rangée est épuisée, `removeFromLeft` rend un rectangle vide : tout ce
qui suit reçoit une largeur de **zéro** — invisible, incliquable, sans un mot.
Et le commentaire promettait l'inverse : « *La barre reste utilisable sur une
fenêtre étroite -- rien n'est jamais coupé, les éléments se serrent.* » Rien ne
se serrait : les largeurs sont des constantes.

**MESURÉ, avec un témoin issu du même binaire** (l'ancien calcul rejoué à côté
du nouveau, puis retiré) sur les 36 éléments de la barre — commandes et
intitulés :

| fenêtre | rack | largeur de la barre | AVANT : éléments à zéro pixel | APRÈS : rangées |
|---|---|---|---|---|
| 900x660 | ouvert | 248 px | **27 / 36** | 15 |
| 900x660 | fermé | 705 px | **12 / 36** | 5 |
| 1280x742 | ouvert | 438 px | **19 / 36** | 8 |
| 1280x742 | fermé | 1093 px | **3 / 36** | 4 |

**LA DERNIÈRE LIGNE EST CELLE QUI COMPTE** : 1 093 px est le plus large que cet
écran puisse donner à la barre, et **trois commandes y étaient déjà à zéro
pixel** — « Replier », « Suivre » et la ligne d'information de D29.4. Ce n'est
donc pas un défaut de petite fenêtre : la barre réclame ~950 px pour sa seule
bande du haut et ~1 100 px pour celle du milieu. Le compte de ce qu'on voyait à
l'écran le confirme : à 900x660 avec le rack, les outils s'arrêtaient sur
« C… » (pour « Coll. ») et l'on ne pouvait ni couper le son d'une note, ni
annuler, ni quantifier, ni zoomer.

**CE QUI EST FAIT : LES BANDES SE REPLIENT.** Ce qui ne tient pas passe à la
rangée suivante, jamais à zéro. La case grandit, le texte ne rétrécit pas —
c'est la règle de D60, à qui le bandeau d'aide devait déjà sa seconde ligne. Un
GROUPE ne se coupe jamais : un intitulé reste avec ce qu'il nomme (« Swing »
avec son curseur), et une paire indissociable reste entière (annuler/rétablir,
les trois zooms).

**ET LA BARRE NE DÉVORE PAS L'ÉDITEUR.** Quinze rangées à 248 px, c'est plus que
le panneau n'a de hauteur. La barre vit donc dans un `juce::Viewport` : elle
DEMANDE sa hauteur (`hauteurUtile(largeur)`, un seul parcours sert à mesurer et
à poser, sans quoi les deux divergeraient), le panneau lui en accorde **au plus
deux cinquièmes** de sa hauteur — plancher à 92 px, la valeur de D29.4, pour que
rien ne change là où tout tenait déjà — et le reste défile. Aucune commande
n'est plus hors d'atteinte ; certaines demandent un coup de molette.

#### 2. Les sérigraphies des façades s'écrivaient « … » — deux pixels pour une lettre

Sur la façade du Vocal à 900x660, les quatre curseurs d'enveloppe portent
**« … » à la place de « A D S R »**. Le mécanisme, mesuré : `juce::Label`
réserve **cinq pixels de marge de chaque côté** par défaut, dix en tout, pris
sur la largeur du texte.

| | case | reste pour le texte | la lettre veut |
|---|---|---|---|
| 900x660 | 12 px | **2 px** | 6 à 7 px |
| 1280x742 | 22 px | 12 px | 6 à 7 px |

Deux pixels pour une lettre : `drawFittedText` met des points. **Des points à
la place d'un nom ne disent rien du tout**, et la marge n'apporte rien ici — la
sérigraphie est centrée sous son bouton, sans fond ni cadre. Elle passe à zéro :
c'est la règle de lisibilité prise par l'autre bout, on agrandit la case au lieu
de rétrécir le texte.

**MESURÉ sur les 64 sérigraphies que le rack pose réellement** (le témoin
compare, pour chacune, la largeur voulue par le texte à la place laissée) :

| fenêtre | écrasées pour tenir | réduites à des points |
|---|---|---|
| 900x660 avant | 48 | **30** |
| 900x660 après | 15 | **11** |
| 1280x742 avant | 14 | 7 |
| 1280x742 après | 9 | 4 |

#### 3. CE QUE LE BALAYAGE A VU ET QUI RESTE — les potentiomètres des sections basses

Nommé ici plutôt que corrigé en silence dans la même phase, avec son chiffre.
La section **RÉGLAGES** du TB-303 porte deux potentiomètres, « ACCENT
THRESHOLD » et « ANALOG DRIFT » : ils font **5 pixels de diamètre à 1280x742**
— le plus large que cet écran donne — et leur sérigraphie se dessine par-dessus.
Ce n'est pas un défaut de petite fenêtre : `captionHeight` vaut
`jlimit(12, 26, hauteur × 0,26)`, et son PLANCHER de 12 px s'applique même
quand la cellule en fait 18 : il en reste six pour le bouton. Un bouton de cinq
pixels n'est pas une commande — c'est la promesse de D35.5 à nouveau. À
regarder dans sa propre phase.

**VU À L'ÉCRAN, AUX DEUX BOUTS.** À 900x660 : les outils du piano roll se lisent
en entier sur quatre rangées (« Coll. » et « Muet » ne sont plus « C… » et
rien), et l'enveloppe du Vocal écrit « A D S R ». À 1280x742 sans le rack : la
barre tient sur **quatre** rangées et montre TOUT, « Replier », « Suivre » et la
ligne d'information comprises — trois éléments qui n'avaient jamais été
visibles.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts
(la phase ne touche qu'`app/Source/`, dont aucune suite ne traverse le point
d'entrée : c'est la mesure à l'écran qui la tranche, et elle est ci-dessus).

### Phase D62 — Un potentiomètre de SIX pixels à la plus grande fenêtre de l'écran : la sérigraphie prenait la cellule entière (10/09/2026, 00:20)

**CE QUE D61 AVAIT NOMMÉ SANS LE CORRIGER**, et qui valait sa phase. La hauteur
de la sérigraphie vaut `jlimit(12, 26, hauteur × 0,26)` : son **plancher de
douze pixels s'applique même à une cellule qui en fait dix-huit**, et il n'en
reste que six pour le bouton. Mesuré à 1280x742 — la plus grande fenêtre que
cet écran donne — sur la section « RÉGLAGES » du TB-303 : cellule **66x18**,
sérigraphie 12, **bouton 6 pixels**, et le nom écrit par-dessus. Ce n'est pas un
défaut de petite fenêtre.

**LA CORRECTION : LA SÉRIGRAPHIE PASSE À CÔTÉ QUAND LA CELLULE EST BASSE.** Le
bouton prend alors toute la hauteur, le nom la largeur qui reste. Mais
l'arrangement n'est retenu que si les DEUX conditions tiennent, et la seconde a
été ajoutée après une mesure qui a réfuté la première version :

1. **il donne un bouton plus grand** — sans quoi une cellule étroite et haute
   (21x28, bouton déjà à 16) y perdrait ;
2. **le nom n'y perd pas de place** — on compare les deux surfaces offertes au
   texte, largeur × nombre de lignes que `drawFittedText` peut écrire. Sans
   cette condition, la batterie acoustique y perdait : ses cellules sont
   étroites, le bouton y gagnait trois pixels et « DECAY » devenait « DEC… »,
   « CL LVL » devenait « … ». Vu à l'écran, puis corrigé.

**SAUF QUAND L'EMPILEMENT NE LAISSE RIEN.** Un bouton de zéro pixel est une
commande qu'on ne peut pas atteindre — la promesse de D35.5, encore — et cela ne
se compare pas à quelques pixels de nom en moins. La cellule de **43x11** du
TB-303 n'avait AUCUN bouton : la sérigraphie y prenait ses onze pixels et un de
plus. Elle passe à côté quoi qu'il en coûte au nom.

**MESURÉ, avec un témoin issu du même binaire, sur les 49 cellules que le rack
pose pour cette machine** (le témoin imprime cellule, diamètre et arrangement,
puis a été retiré) :

| | fenêtre | plus petit bouton | à ZÉRO pixel | sous 12 px | sous 18 px | passés à côté |
|---|---|---|---|---|---|---|
| avant | 1280x742 | **6 px** | 0 | 2 | 2 | — |
| après | 1280x742 | **18 px** | 0 | **0** | **0** | 7 |
| avant | 900x660 | 0 px | 4 | 16 | 26 | — |
| après | 900x660 | 0 px | **2** | **6** | **21** | 16 |

**LES DEUX QUI RESTENT À ZÉRO À 900x660 SONT D'UNE AUTRE NATURE**, et il faut le
dire : leur cellule fait **43x0** — la grille de la façade ne leur donne aucune
hauteur du tout. Aucun arrangement ne peut rien pour une cellule sans hauteur ;
c'est la description de la machine qu'il faudrait revoir. Nommé ici, pas corrigé.

**VU À L'ÉCRAN, AUX DEUX BOUTS.** À 1280x742, les deux commandes de « RÉGLAGES »
sont deux vrais boutons avec leur repère et leur nom à côté, là où c'étaient
deux points. À 900x660, la rangée « SYNTHESIZER » du TB-303 — sept points
alignés jusqu'ici — montre sept boutons nommés (WAVEFORM, CUT OFF FREQ sur deux
lignes, RESONA…, ENV MOD, DECAY, ACCENT, SLIDE TIME). Et la batterie
acoustique est **inchangée** : LEVEL, TUNE, DECAY, BEATER, SNARES, CL LVL,
CL DEC, OP LVL, OP DEC, RIDE, RD DEC, CRASH, CR DEC, LEVEL, SIZE, VOLUME s'y
lisent comme avant — c'est la seconde condition qui l'a préservée.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts.

### Phase D63 — La façade GÉNÉRIQUE défilait, la façade DESSINÉE était écrasée : l'asymétrie était à l'envers (10/09/2026, 01:10)

**LES DEUX DÉFAUTS QUE D62 AVAIT NOMMÉS**, et qui n'en font qu'un. Le rack pose
la façade d'une machine ainsi :

```cpp
if (usingMachinePanel_) { machinePanel_.setBounds(area); return; }
viewport_.setBounds(area);                       // la façade générique, elle, défile
```

La façade **générique** — celle des machines qui n'ont pas de dessin, une simple
liste de potentiomètres — vit dans un `juce::Viewport` depuis toujours et reçoit
`max(rangées × hauteur, hauteur du volet)`. La façade **dessinée** — celle qui a
une grille, des blocs et des titres, celle pour laquelle `panels/` existe —
recevait la hauteur du rack, quelle qu'elle soit. **L'asymétrie était à l'envers
du bon sens** : c'est la seconde qui ne supporte pas d'être écrasée.

**CE QUE CELA DONNAIT.** À 900x660, le bloc « RÉGLAGES » du TB-303 recevait une
cellule de **42x19** : la sérigraphie en prend 12 au plancher, il en restait
**sept** pour le bouton. Deux commandes de la façade étaient à **zéro pixel**.

**CE QUI EST FAIT.** `MachinePanelComponent::hauteurUtile()` calcule la hauteur
que la grille réclame, et le rack la lui donne — dans son propre volet, qui
défile en dessous. Le calcul part du plancher mesuré en D62 : un bouton de 18 px
et sa sérigraphie de 12, soit une cellule de 30, plus ce que la rangée perd en
chemin. **Un bloc d'une seule rangée paie ses frais sur cette seule rangée** ; un
bloc de deux les amortit. On prend donc la rangée la plus exigeante, et non une
moyenne — sans quoi le bloc le plus serré reste sans place, ce qui est
exactement le cas de « RÉGLAGES ».

**LE CHIFFRE DES FRAIS A DÛ ÊTRE MESURÉ, ET DEUX FOIS.** Écrit de tête depuis le
code voisin, il valait d'abord 6, puis 28 — et la façade sortait à 340 px avec
une cellule de 19. Un témoin qui imprime la cellule **réellement posée** a rendu
le compte juste : `58,8 − 40 = 18,8`, et 40 est la somme de quatre réductions
dont un `reduced(6)` que la lecture avait sauté. Une addition de constantes lues
dans le code se vérifie à l'écran comme le reste ; c'est la leçon de D49 sous une
forme de plus.

**MESURÉ, sur les 49 cellules que le rack pose pour cette machine** (témoin issu
du même binaire, retiré après) :

| | fenêtre | plus petit bouton | à ZÉRO pixel | sous 18 px |
|---|---|---|---|---|
| après D62 | 900x660 | 0 px | **2** | **21** |
| après D63 | 900x660 | **11 px** | **0** | **4** |
| après D62 | 1280x742 | 18 px | 0 | 0 |
| après D63 | 1280x742 | **23 px** | 0 | 0 |

La façade du TB-303 réclame **400 px** et les obtient aux deux tailles ; elle en
recevait 340 en fenêtre pleine, donc **le plafond aussi était trop bas** — le
défaut n'était pas propre à la petite fenêtre.

**CE QUI RESTE, ET C'EST UNE AUTRE VARIABLE.** Les quatre boutons encore sous
18 px sont ceux du Minimoog, dans des cellules de **11x24** : c'est la LARGEUR
qui les borne, pas la hauteur, et le rack est une colonne étroite par
construction. Une largeur minimale se déclarerait de la même façon, mais elle
ferait défiler le rack horizontalement — une seconde variable, à mesurer dans sa
propre phase plutôt qu'à glisser dans celle-ci.

**VU À L'ÉCRAN, AUX DEUX BOUTS.** À 900x660, les sept commandes de
« SYNTHESIZER » sont des potentiomètres pleins avec leur sérigraphie entière, et
« RÉGLAGES » porte deux vrais boutons sous eux, atteignables en défilant. À
1280x742, la façade gagne les soixante pixels qui lui manquaient.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts.

### Phase D64 — Le mélangeur du Minimoog est une BANDE VERTICALE, et le dessiner en carré coûtait 11 pixels de large (10/09/2026, 02:30)

**LES QUATRE BOUTONS QUE D63 AVAIT LAISSÉS.** Cellules de **11x24** : c'est la
largeur qui les borne, pas la hauteur. Ils appartiennent tous au bloc « MIXER »
du Minimoog, à qui la description donne **deux colonnes sur seize** pour une
grille interne de **2x2**. Deux colonnes de grille valent 50 px à un rack de
426 ; moins les frais du bloc, il reste 34 px pour deux colonnes internes, donc
17 par cellule, donc 11 pour le bouton.

**LA CORRECTION EST DANS LA DESCRIPTION, ET ELLE REND LA FAÇADE PLUS JUSTE.**
Sur la machine d'origine, le mélangeur n'est pas un carré : c'est une **bande
verticale** où les trois volumes d'oscillateur et le bruit s'empilent. Le 2x2
était une licence de dessin, pas une fidélité. En colonne — quatre rangées,
une seule colonne interne — chaque cellule reçoit les deux colonnes de grille
entières, et le bouton **triple** de taille.

| | | plus petit bouton | boutons sous 18 px |
|---|---|---|---|
| après D62 | 900x660 | 0 px | 23 |
| après D63 | 900x660 | 11 px | 4 |
| **après D64** | 900x660 | **19 px** | **0** |
| après D62 | 1280x742 | 18 px | 0 |
| après D63 | 1280x742 | 23 px | 0 |
| **après D64** | 1280x742 | **29 px** | 0 |

Sur les 49 cellules que le rack pose pour ces machines, **plus aucun bouton
n'est sous le plancher de 18 px**, aux deux tailles de fenêtre. La série
D62-D63-D64 est close.

#### Deux remèdes écrits, mesurés, et RETIRÉS — c'est la moitié du travail

**1. Prendre la largeur de la façade comme plancher de la poignée du rack**,
comme le dock du bas prend celle de la tranche master depuis D59. Écrit, puis
retiré par la mesure : les **63** façades décrites réclament de **156 à
1 372 px**, médiane **420**, et **31 d'entre elles** demandent plus que les
426 px que le rack reçoit. Le plancher aurait donc élargi le rack de force sur la moitié du
parc, en écrasant une largeur que l'utilisateur choisit et que l'application
retient (`dock.droite`). **Une disposition réglable ne se reprend pas à son
propriétaire.**

**2. Faire défiler la façade HORIZONTALEMENT** quand elle ne tient pas, comme
D63 l'a fait en hauteur. Écrit, compilé, REGARDÉ : sur le Divider, on ne voyait
plus que **trois blocs sur six**, « ENVELO… » coupé au bord droit, et il fallait
défiler dans les deux sens pour trouver un réglage. Retiré le jour même. La
différence avec la hauteur n'est pas une question de goût : **une façade
tronquée en bas reste lisible en haut, une façade tronquée à droite perd des
blocs entiers.**

**ET LE CALCUL DE CETTE LARGEUR AVAIT UN DÉFAUT, TROUVÉ EN LE RELISANT.** Sa
première version posait un plancher de 40 px par colonne de grille
*indépendamment des blocs* — comme si chaque colonne devait porter un bloc à
elle seule. Elle annonçait 428 px pour la façade la moins exigeante là où le
compte juste en donne 156, et gonflait toutes les façades d'un coup. Corrigée,
la table ci-dessus est celle qui a servi à décider. Le calcul lui-même a été
retiré avec le témoin : il a fait son travail, qui était de produire ce chiffre.

**CE QUE LA TABLE LAISSE OUVERT.** **Trente et une** façades sur soixante-trois
demandent plus que le rack par défaut ; les plus gourmandes sont `vsm.generic`
(1 372 px, grille de 24 colonnes), `vsm.obx`, `vsm.wavetable` et `vsm.pcmhybrid`
(1 148, grille de 20).
Ce sont des grilles trop fines pour une colonne latérale, et le remède est du
côté des DESCRIPTIONS — comme celui du Minimoog vient de l'être, et en y
gagnant en fidélité. À faire machine par machine, quand une mesure ou une
écoute le réclame, pas en bloc.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts.

> **RECTIFICATIF DE D64, LE JOUR MÊME : LA TABLE DISAIT « 54 FAÇADES », IL Y EN
> A 63.** Le témoin écrivait sur la sortie d'erreur et la mesure passait par un
> tube (`… | grep …`) : **neuf lignes s'y sont perdues**, et ce sont les neuf
> PREMIÈRES de la liste — TB-303, TR-808, TR-909, SH-101, Juno-106, Jupiter-8,
> MS-20, DX7, CS-80. Un début de flot avalé avant que le filtre ne lise.
>
> Refaite avec un témoin qui écrit DIRECTEMENT dans un fichier, la mesure donne
> **63 façades** et **31** au-dessus de 426 px, au lieu de 54 et 26. Le minimum
> (156), la médiane (420) et le maximum (1 372) ne bougent pas, et la conclusion
> non plus : les deux remèdes ont été retirés pour des raisons qui ne dépendent
> pas du compte. Les chiffres de D64 sont corrigés en place.
>
> **C'est la règle du dépôt prise en défaut par son auteur** : *un nombre sorti
> d'un grep se revérifie en listant ce qu'on a cherché ET où, avant de
> l'écrire*. Le dénominateur était vérifiable en une commande —
> `grep -c "^MachinePanel make" panels/src/MachinePanels.cpp` en rend 63 — et
> il ne l'a pas été. La leçon d'exploitation qui s'ajoute aux précédentes :
> **un témoin de mesure écrit dans un FICHIER, jamais dans un tube.**

### Phase D65 — Un synoptique dont aucun poste ne se voit : les deux façades les plus larges, empilées (10/09/2026, 04:00)

**LA TABLE DE D64 DÉSIGNAIT DES COUPABLES ; ENCORE FALLAIT-IL LES REGARDER.**
Elle donnait une largeur *réclamée par la grille* — un calcul. Ce qui juge une
façade, c'est le diamètre RENDU de ses boutons. Les cinq plus exigeantes ont
donc été mises dans un projet d'essai et ouvertes, et le témoin a relevé chaque
cellule posée :

| façade | boutons relevés | plus petit | sous 18 px | cellule la plus étroite |
|---|---|---|---|---|
| `vsm.generic` | 25 | **0 px** | **25 / 25** | **3x12** — « RATE » |
| `vsm.obx` | 8 | **4 px** | **8 / 8** | 5x16 — « WAVE » |

**AUCUN bouton du synthé générique n'atteignait le plancher de 18 px**, et le
plus petit n'avait aucune surface du tout. C'est la machine que
`CDC-machines-manquantes.md` a conçue POUR LA RECHERCHE, celle qui gagne le stem
de basse : sa façade n'existait pas.

**LA CAUSE EST LA MÊME POUR LES DEUX, ET ELLE EST DANS LA DESCRIPTION.** Les
sept blocs du générique tenaient sur **une seule rangée de vingt-quatre
colonnes** ; les sept de l'OB-X sur une rangée de vingt. Une lecture de gauche à
droite, superbe sur un écran large — et le rack est une colonne de 426 px.

**CE QUI CHANGE : LES BLOCS S'EMPILENT.** Huit colonnes, dix rangées, deux blocs
côte à côte quand ils sont étroits (les deux oscillateurs, les deux enveloppes).
**Le sens de lecture est conservé** : sources, filtre, enveloppes, modulation,
sortie — de haut en bas au lieu de gauche à droite, ce qu'un synoptique supporte
sans rien perdre.

| | plus petit bouton | sous 18 px | cellule la plus étroite |
|---|---|---|---|
| `vsm.generic` avant | 0 px | 25 | 3x12 |
| `vsm.generic` après | **35 px** | **0** | **55x47** |
| `vsm.obx` avant | 4 px | 8 | 5x16 |
| `vsm.obx` après | **35 px** | **0** | **40x47** |

**ET C'EST D63 QUI REND CELA POSSIBLE.** Empiler rend la façade plus HAUTE — le
générique réclame maintenant plus que le rack n'a. Avant D63, cela l'aurait
écrasée ; depuis, elle défile. **La hauteur est devenue bon marché, la largeur
ne l'est pas** : c'est la conclusion de D64 employée, et non plus seulement
écrite.

**CE QUI N'EST PAS FAIT, ET POURQUOI.** `vsm.wavetable`, `vsm.pcmhybrid` et
`vsm.supersaw` ont exactement la même forme — tous leurs blocs sur la rangée 0,
sur dix-huit à vingt colonnes — et la table de D64 les place juste derrière.
Mais **ils n'ont pas été mesurés à l'écran** : dans les courses du témoin, ils
n'ont pas été posés. Corriger une façade sur la foi d'un calcul quand le
diamètre rendu est mesurable serait exactement ce que ce document interdit
ailleurs. Ils attendent leur mesure, et le compte d'A5 reste à trente et une.

**VU À L'ÉCRAN.** Le générique montre ses douze sources nommées (SHAPE 1, LVL 1,
PW 1, DETUNE, SUB, NOISE / SHAPE 2, LVL 2, PW 2, OCTAVE, SUB SH, COLOUR) et son
filtre dessous ; l'OB-X montre CONTROL avec ses trois curseurs et ses deux
boutons, puis OSCILLATOR 1 et OSCILLATOR 2 côte à côte.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts.

### Phase D66 — Les huit façades qui restaient : soixante-sept boutons sous le plancher, zéro après (10/09/2026, 05:30)

**D65 AVAIT LAISSÉ TROIS FAÇADES FAUTE DE MESURE**, et la raison en était un
projet d'essai mal formé : huit pistes déclarées dans `project.json`, **deux**
seulement dans le fichier MIDI. Le chargeur apparie les deux, et les six
dernières n'existaient pas. Refait avec l'exporteur de la chaîne
(`write_project_bundle`), qui écrit les deux du même mouvement, les huit
machines s'ouvrent.

**CE QUE LE TÉMOIN A RELEVÉ**, sur les huit façades que la table de D64 place en
tête, à 900x660 :

| machine | boutons | avant : plus petit / sous 18 px | après : plus petit / sous 18 px |
|---|---|---|---|
| `vsm.chebyshev` | 3 | 1 px / 1 | **47 px** / **0** |
| `vsm.wavetable` | 14 | 4 px / 10 | **40 px** / **0** |
| `vsm.pcmhybrid` | 15 | 4 px / 9 | **35 px** / **0** |
| `vsm.vector` | 16 | 8 px / 6 | **38 px** / **0** |
| `vsm.cs80` | 20 | 9 px / 10 | **35 px** / **0** |
| `vsm.prophet` | 29 | 10 px / 15 | **40 px** / **0** |
| `vsm.supersaw` | 13 | 11 px / 7 | **38 px** / **0** |
| `vsm.arpodyssey` | 9 | 13 px / 9 | **35 px** / **0** |
| `vsm.minimoog` (D64) | 22 | 20 px / 0 | 20 px / 0 |

**Soixante-sept boutons sous le plancher de 18 px, zéro après.** Un bouton d'UN
pixel sur le Chebyshev — la commande de volume.

**LE REMÈDE EST CELUI DE D65, APPLIQUÉ PAR UN OUTIL.** Toutes ces façades ont la
même forme : tous leurs blocs sur la rangée 0, sur seize à vingt colonnes. Un
script relit chaque bloc — ses colonnes et ses rangées INTERNES, déduites de ses
commandes — et réécrit ses seules coordonnées de grille : huit colonnes, blocs
empilés, deux côte à côte quand ils sont étroits. **L'ordre des blocs, leurs
commandes, leurs couleurs et leurs sérigraphies ne sont pas touchés** : le sens
de lecture de chaque machine est conservé, seul son pliage change.

#### Deux défauts de l'outil, et qui les a trouvés

**LE PREMIER A ÉTÉ ATTRAPÉ PAR UN TEST, SANS ÉCRAN.** Quand un bloc de demi-
largeur attendait un partenaire et que le suivant prenait toute la largeur,
l'outil n'avançait que d'UNE rangée au lieu de la hauteur du bloc en attente :
les deux se chevauchaient. `sections_never_overlap` l'a nommé —
« façade vsm.prophet : les blocs "POLY-MOD" et "OSCILLATOR A / B" se
chevauchent ». C'est exactement ce pour quoi `panels/` est une bibliothèque de
DONNÉES testables plutôt que du code de dessin : une erreur de disposition s'y
attrape sans serveur graphique, avant toute capture.

**LE SECOND A ÉTÉ ATTRAPÉ PAR L'ŒIL, que la mesure ne remplace pas.** Les
chiffres étaient déjà bons — 35 px partout, zéro sous le plancher — et le
POLY-MOD du Prophet occupait la moitié gauche de la façade en laissant un grand
rectangle vide à droite : lisible, et l'air inachevé. Un bloc resté seul sur sa
rangée prend désormais toute la largeur, et ses cinq commandes s'y étalent avec
leur sérigraphie à côté.

**VU À L'ÉCRAN.** Le POLY-MOD du Prophet montre ses cinq sources et destinations
nommées en entier (SOURCE : FILT ENV, SOURCE : OSC B, TO FREQ A, TO PW A, TO
FILTER), OSCILLATOR A / B dessous.

**CE QUE CELA LAISSE.** Onze façades sur soixante-trois réclament encore, PAR LE
CALCUL de D64, plus que le rack ne donne. Aucune n'a été mesurée à l'écran : la
règle de D65 tient, on ne corrige pas une façade sur la foi d'un calcul quand le
diamètre rendu est mesurable. Elles attendent leur projet d'essai.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts.

### Phase D67 — Les soixante-trois façades passées au crible, et le calcul avait désigné les mauvaises (10/09/2026, 07:15)

**D66 LAISSAIT « ONZE FAÇADES QUE LE CALCUL SIGNALE ». LE CALCUL SE TROMPAIT
DE MACHINES.** Un projet d'essai portant les **soixante-trois** façades décrites
a été écrit par l'exporteur de la chaîne, et chacune ouverte à 900x660 — une
course par piste, le rack ne posant que la façade de la piste choisie. Le témoin
a relevé chaque cellule réellement posée.

| | façades avec un bouton sous 18 px | boutons concernés |
|---|---|---|
| avant D67 | **13 / 63** | **89** |
| après D67 | **4 / 63** | **37** |

**LA LISTE MESURÉE N'EST PAS LA LISTE CALCULÉE.** Le TR-909, la percussion, les
FM drums, le Juno-106, le SH-101 et le sampler y figurent alors que la table de
largeur de D64 ne les signalait pas ; d'autres qu'elle signalait s'y révèlent
propres. La raison est simple et vaut d'être écrite : la largeur réclamée par la
grille est un PROXY, le diamètre rendu est le fait. **On ne corrige pas une
façade sur la foi d'un calcul quand le rendu est mesurable** — la règle posée en
D65 a servi ici à ne pas toucher les bonnes machines pour de mauvaises raisons.

**NEUF FAÇADES CORRIGÉES.** Huit par l'outil d'empilement de D66 — Jupiter-8,
orgue à roues phoniques, vent, cône, Juno-106, SH-101, batterie acoustique,
`vsm.scanned` — et une à la main : le **DX7**, dont les six opérateurs étaient
alignés sur une rangée de seize colonnes et donnaient des cellules de 11 px.
Sa matrice passe de **6x1 à 3x2**. Comparer un opérateur à l'autre est tout
l'intérêt de cette façade, et cela se fait aussi bien en deux rangées de trois
qu'en une de six.

#### L'outil a refusé cinq façades, et il a eu raison de refuser

Son lecteur reconnaît les blocs déclarés un par un ; le DX7, le TR-909, la
percussion, les FM drums et le sampler construisent les leurs **dans une
boucle** — un bloc par opérateur, un par voix. L'outil n'en voyait qu'un seul et
aurait empilé celui-là en laissant les autres à leurs anciennes coordonnées.

**IL A FALLU DEUX TESTS ROUGES POUR L'APPRENDRE, ET C'EST LA BONNE FAÇON DE
L'APPRENDRE.** La première version n'avait pas ce garde-fou : les suites de
`panels/` sont passées de onze vertes à neuf, et le message nommait la façade et
les deux blocs qui se chevauchaient. Le garde-fou compare désormais le nombre de
blocs LUS au nombre de blocs DÉCLARÉS, et refuse quand ils diffèrent — une
façade qu'on ne comprend pas ne se réécrit pas.

**CE QUI RESTE, ET POURQUOI ON NE LE TOUCHE PAS À LA LÉGÈRE.**

| machine | boutons | plus petit | sous 18 px |
|---|---|---|---|
| `vsm.sampler` | 4 | 11 px | 4 |
| `vsm.tr909` | 17 | 15 px | 6 |
| `vsm.perc` | 14 | 15 px | 11 |
| `vsm.fmdrums` | 21 | 15 px | 16 |

Quatre boîtes à rythmes, et leur disposition **est** leur identité : le README
en fait l'éloge sous le nom de « colonne par pièce des TR-808/909 et du
sampler-boîte à rythmes ». Empiler leurs voix les rendrait méconnaissables pour
gagner quatre pixels — le compromis n'est pas le même que sur un synthé dont les
blocs suivent un signal. Le remède, s'il en faut un, est une décision par
machine : deux rangées de voix plutôt qu'une, ou un rack plus large pour elles.
Nommé, chiffré, non fait.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts.

### Phase D68 — La barre de transport coupait sa moitié gauche, et « Ouvrir… » ne s'est jamais montré (10/09/2026, 09:00)

**LA MOITIÉ DROITE SAVAIT S'EFFACER, LA GAUCHE COUPAIT.** D41.2 avait donné à la
barre un ordre d'importance — l'absence de son, les craquements, la charge, puis
les deux boutons, la fréquence en dernier — et un `poser()` qui CACHE ce qui ne
tient pas. La moitié gauche, elle, est restée sur des `removeFromLeft` à
largeurs constantes : ce qui dépasse reçoit zéro pixel, comme la barre du piano
roll avant D61.

**MESURÉ**, témoin dans un fichier, aux trois largeurs que cet écran permet :

| élément | 900 | 1024 | 1280 |
|---|---|---|---|
| tempo | **64 px** (au lieu de 100) | 100 px | 100 px |
| signature rythmique | **ZÉRO px** | 60 px | 60 px |
| charge | caché | caché | 76 px |
| Exporter MIDI… | caché | caché | 120 px |
| **Ouvrir MIDI…** | **caché** | **caché** | **caché** |
| fréquence | caché | caché | caché |

**TROIS FAITS, ET LE TROISIÈME EST LE PIRE.**

1. La signature rythmique est à **zéro pixel** au plancher — coupée, pas cachée.
2. La moitié gauche réclame 986 px pour 884 disponibles : **rien** de la moitié
   droite ne peut plus se poser. La charge disparaît donc au plancher — or
   D41.2 écrit noir sur blanc qu'elle est « le seul indicateur qui dise si le
   morceau va JOUER », et qu'elle doit passer AVANT les deux boutons.
3. **« Ouvrir MIDI… » ne s'affiche à aucune des largeurs que cet écran permet.**
   Il lui en faut environ 1 400 ; le plafond est 1 280 px logiques (D58). Le
   bouton est posé, compté, et jamais vu. C'est la promesse de D35.5 pour la
   troisième fois dans ce document, après la septième commande du master (D59)
   et les onze commandes du piano roll (D61).

**CE QUI EST FAIT : LA BARRE SE REPLIE**, comme le bandeau d'aide de D60 et la
barre du piano roll de D61. Elle demande sa hauteur (`hauteurUtile(largeur)`, un
seul parcours pour mesurer et pour poser) et `MainComponent` la lui accorde, au
lieu des 56 px en dur. **Le bloc de transport ne se coupe jamais** : ses dix
commandes forment un geste unique, et les deux voyants MIDI n'ont de sens que
collés au reste. L'ordre de D41.2 est conservé tel quel — il décide désormais de
l'ordre de repli au lieu de l'ordre de disparition.

| élément | avant → après, à 900 px |
|---|---|
| tempo | 64 px → **100 px** |
| signature | ZÉRO → **60 px** |
| charge | caché → **76 px** |
| Exporter MIDI… | caché → **120 px** |
| Ouvrir MIDI… | caché → **120 px** |
| fréquence | caché → **90 px** |

**ET CE QUE CELA COÛTE, PARCE QUE CELA SE PAIE.** Il faut ~1 400 px pour une
seule rangée ; sur cet écran la barre en fait donc **deux à toutes les tailles**,
et passe de 56 à 100 px. Quarante-quatre pixels pris à l'arrangement, en
permanence. Le compromis est assumé dans ce sens-là : six éléments qui
n'existaient pas à l'écran, dont un bouton qui ne s'était jamais montré et
l'indicateur qui dit si le son va tenir, valent mieux qu'une rangée fine où l'on
ne voit ni l'un ni l'autre. Sur un écran plus large, la barre reprend une seule
rangée sans rien changer d'autre.

**VU À L'ÉCRAN, à 900x660** : rangée du haut — Play, Stop, Rec, Loop, Clic, Tap,
x1, les deux voyants, « Écoute : reconstruction », le temps ; rangée du bas —
120.0 BPM, 4/4, CPU 0.1 %, « Exporter MIDI… », « Ouvrir MIDI… », 44.1 kHz.

Tests : 1 291 audio, 327 core, 292 interchange, 25 clap, 11 panels — verts.

### Vérification D69 — Les deux dernières zones du plancher ne trouvent rien, et la série se referme (10/09/2026, 11:20)

**CE QUI RESTAIT À REGARDER.** La série ouverte par D58 — `VSM_TAILLE` qui ne
faisait rien — a balayé la fenêtre zone par zone à 900x660, et chaque passage a
trouvé quelque chose :

| zone | phase | ce qui a été trouvé |
|---|---|---|
| tranche master | D59 | la 7ᵉ commande invisible à toute taille |
| onglets du dock | D60 | deux aides coupées au milieu de la clause utile |
| barre du piano roll | D61 | 27 éléments sur 36 à zéro pixel |
| façades de machine | D62 → D67 | 89 boutons sous 18 px, dont un à zéro |
| barre de transport | D68 | la signature à zéro, « Ouvrir… » jamais visible |
| **liste de pistes** | **D69** | **rien** |
| **mélangeur** | **D69** | **rien** |

**LA LISTE DE PISTES TIENT.** Les noms se lisent en entier (« other · voix 1 »),
le champ de filtre garde son intitulé, « + Ajouter une piste » se plie sur deux
lignes plutôt que de se couper, et les sélecteurs de machine abrègent par la fin
— « Vocal (conduit voc… » — en gardant le nom de la machine, qui est la partie
qui renseigne. Aucun élément à zéro pixel, aucune sérigraphie réduite à des
points.

**LE MÉLANGEUR TIENT AUSSI**, et pour une raison écrite ailleurs : il a son
viewport depuis le § 4.6 du CDC détection-multipiste, où 64 pistes ont été
regardées. Neuf tranches se voient au plancher, le MASTER reste épinglé à
droite, le reste défile. La tranche coupée au bord droit est le comportement
d'un volet qui défile, pas une troncature.

**UN RÉSULTAT NÉGATIF SE PUBLIE COMME LES AUTRES.** Il dit deux choses. La
première : les deux zones qui n'avaient jamais été photographiées au plancher
n'avaient rien à cacher, et la série s'arrête là plutôt que de continuer à
chercher. La seconde, plus utile : **ce qui tient tient pour une raison**, et
c'est la même dans les deux cas — un volet qui défile (le mélangeur) ou un
texte qui se plie (le bouton d'ajout). Les six défauts trouvés par la série
avaient tous la forme inverse : une largeur constante posée par
`removeFromLeft`, et ce qui dépasse à zéro pixel.

Aucun code n'a changé. `./verifier.sh` : tout vert.

### Phase D70 — « Une colonne par pièce » descend d'un cran : ce qui serre les quatre boîtes à rythmes est la grille INTERNE du bloc (10/09/2026, 12:10)

**CE QUE D67 A LAISSÉ, ET POURQUOI IL FAUT LE REPRENDRE.** Quatre façades sur
soixante-trois gardent des boutons sous le plancher de 18 px mesuré en D62 —
`vsm.sampler`, `vsm.tr909`, `vsm.perc`, `vsm.fmdrums`, soit **37 boutons**. D67
a nommé deux remèdes et n'en a pris aucun : « deux rangées de voix plutôt
qu'une, ou un rack plus large pour elles ». **Les deux sont refusés, et chacun
par une décision déjà écrite ailleurs :**

- **Empiler les voix** est ce que fait l'outil de D66, et D67 a eu raison de ne
  pas le lâcher sur ces quatre-là : la rangée de pièces côte à côte **est** leur
  identité, celle que le README loue sous le nom de « colonne par pièce des
  TR-808/909 ».
- **Élargir le rack** est refusé par D64, et deux fois : le plancher du rack ne
  se prend pas sur ce que la façade réclame (« une disposition réglable ne se
  reprend pas à son propriétaire »), et le défilement horizontal a été écrit,
  regardé, puis RETIRÉ — sur le Divider, il ne montrait plus que trois blocs sur
  six.

**IL EXISTE UN TROISIÈME REMÈDE, QUE NI D64 NI D67 N'ONT NOMMÉ, ET C'EST LUI
QU'ON PREND.** Ce qui serre ces façades n'est pas la rangée de blocs : c'est la
grille **à l'intérieur** du bloc. Le bloc « COWBELL / WOOD » de la percussion
tient trois colonnes de grille et y range **trois colonnes de commandes** ; le
bloc « TOM / BELL » des FM drums en tient cinq pour **cinq colonnes**. À 410 px
de rack, une colonne de grille vaut 27 px : trois colonnes de commandes dans
trois colonnes de grille laissent 16 px au bouton, et c'est tout le défaut.

**LA DÉCISION, ET SA RAISON.** « Une colonne par pièce » cesse d'être la
disposition des **blocs** pour devenir celle des **commandes dans le bloc** :
une pièce est une **colonne verticale de ses réglages**. Le charleston fermé
garde LEVEL au-dessus de DECAY ; la cloche et le bois deviennent deux colonnes
de trois au lieu de deux rangées de trois. **Aucun bloc ne change de rangée ni
d'ordre** — les voix restent côte à côte, dans le sens de lecture de la machine
d'origine —, le rack garde la largeur que l'utilisateur lui a donnée, et rien ne
défile horizontalement. C'est la conclusion de D65 (« la hauteur est devenue bon
marché, la largeur ne l'est pas ») appliquée un cran plus bas que D65 et D66 ne
l'avaient appliquée : eux empilaient des blocs, celle-ci empile des commandes.

Et ce n'est pas un pis-aller : le commentaire du sampler décrit déjà la machine
comme « seize pièces, **une colonne de réglages par pièce** » alors que ses
emplacements portent un carré de deux sur deux. La façade se met à ressembler à
ce que son propre code annonce.

**UN DÉFAUT DE CALCUL QUE CE CHANGEMENT MET AU JOUR, ET QUI EST RÉPARÉ
D'ABORD.** `MachinePanelComponent::hauteurUtile()` réserve la hauteur d'un bloc
en divisant par `section.rowSpan` — le nombre de rangées de **grille** — alors
que `resized()` découpe le bloc par son nombre de rangées de **commandes**. Tant
que les deux coïncident, le compte tombe juste ; une sonde sur les soixante-trois
façades le confirme : **aucun bloc n'a aujourd'hui plus de rangées de commandes
que de rangées de grille**. Empiler les commandes crée précisément ce cas. Le
calcul prend donc `max(rowSpan, rangées de commandes)` — une forme choisie pour
qu'elle ne puisse pas changer une façade existante, et non pour qu'elle tombe
juste sur les quatre nouvelles.

**ET UNE MESURE QUI RESTE, AU LIEU D'ÊTRE REFAITE À CHAQUE PHASE.** D63, D66 et
D67 ont chacune ajouté un témoin qui imprime la cellule réellement posée, puis
l'ont retiré. Trois fois le même outil, et c'est lui qui a démenti le calcul en
D67. Il devient permanent et gratuit : **`VSM_MESURE_FACADE=fichier.tsv`** écrit,
pour la façade posée, une ligne par commande — machine, bloc, sérigraphie,
cellule, diamètre rendu. Ce qui se mesure à chaque phase se lit désormais sans
rien recompiler.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 12:10).**
>
> 1. **Les quatre façades passent le plancher à 900x660** : zéro bouton sous
>    18 px, contre 37 aujourd'hui. C'est le seul chiffre qui juge la phase ; s'il
>    reste un bouton dessous, la disposition choisie est insuffisante et il
>    faudra le dire plutôt que baisser le plancher.
> 2. **Aucune voix ne bouge.** Pour chacune des quatre, le `row` de tous les
>    blocs reste celui d'aujourd'hui, et leur ordre en `column` est inchangé :
>    ce qui change est la grille interne, et sur `vsm.tr909` la répartition des
>    colonnes de grille entre deux blocs voisins (à somme constante, 15).
> 3. **Les cinquante-neuf autres façades ne bougent pas d'un pixel.** Mesuré par
>    le même témoin sur les soixante-trois, avant et après, et non supposé de la
>    forme de la formule.
> 4. **Les façades s'allongent, et c'est le prix assumé.** Une pièce en colonne
>    est plus haute qu'en carré ; la façade réclame donc plus de hauteur et
>    défile sous le rack (D63). J'attends que la plus haute des quatre reste
>    sous **deux fois** la hauteur du rack à 900x660 — au-delà, chercher un
>    réglage demanderait trop de défilement, et il faudrait alors rouvrir la
>    question de la largeur.

> **D70 EST FAITE (10/09/2026, 13:40). TROIS ATTENDUS SUR QUATRE SONT TENUS, ET
> LE QUATRIÈME ÉTAIT MAL POSÉ — PAR MOI, AVANT DE SAVOIR CE QUE MESURAIT SA
> BARRE.**
>
> | | avant | après | attendu |
> |---|---|---|---|
> | boutons sous 18 px, parc entier | **98** | **0** | 0 |
> | façades concernées | 4 / 63 | **0 / 63** | 0 |
> | plus petit bouton, `vsm.sampler` | 11 px | **26 px** | ≥ 18 |
> | plus petit bouton, `vsm.tr909` | 15 px | **24 px** | ≥ 18 |
> | plus petit bouton, `vsm.perc` | 15 px | **26 px** | ≥ 18 |
> | plus petit bouton, `vsm.fmdrums` | 15 px | **21 px** | ≥ 18 |
> | les 59 autres façades (1 029 cellules) | — | **identiques au pixel** | inchangées |
>
> **LE COMPTE DE D67 ÉTAIT FAUX, ET C'EST LE PREMIER RÉSULTAT DE LA PHASE.** Son
> tableau annonçait « 37 boutons sous 18 px », dont « 4 » pour le sampler. Le
> sampler en a **soixante-quatre**, et les soixante-quatre étaient sous le
> plancher, à 11 px — seize emplacements de quatre réglages chacun. Le total
> n'était pas 37 mais **98**. Le témoin de D67 n'a relevé qu'un emplacement sur
> seize, et son chiffre a été recopié dans `INDEX.md` puis dans un commit. C'est
> exactement pourquoi le témoin devient permanent : un outil qu'on rebâtit à
> chaque phase se retrompe à chaque phase, et rien ne le contredit.
>
> **CE QUI A ÉTÉ FAIT, MACHINE PAR MACHINE**, sans qu'aucun bloc quitte sa
> rangée ni change d'ordre :
>
> | machine | ce qui change | colonnes de grille |
> |---|---|---|
> | `vsm.sampler` | les 4 réglages d'un emplacement passent du carré 2x2 à **une colonne de quatre** | inchangées |
> | `vsm.tr909` | TOM / CLAP passe à **une colonne par pièce** et rend une colonne de grille à CYMBAL / HAT | 4+4+4+3 → 4+4+**3**+**4** |
> | `vsm.perc` | COWBELL / WOOD et SHAKEN passent à **une colonne par pièce** | inchangées |
> | `vsm.fmdrums` | les quatre blocs redistribuent leurs colonnes, aucune ne mêle deux pièces | 4+4+5+3 → **3**+4+5+**4** |
>
> **LA RÈGLE EXACTE N'EST PAS « UNE COLONNE PAR PIÈCE » MAIS « UNE COLONNE NE
> PORTE JAMAIS DEUX PIÈCES ».** La formulation d'avant la mesure était trop
> serrée : le tom des FM drums a cinq réglages et en prend deux colonnes, la
> caisse claire aussi. Ce qui est interdit, et qui était le vrai défaut, c'est
> qu'une colonne descende du tom à la cloche — ce que le commentaire de cette
> façade reprochait déjà à une version antérieure d'elle-même (« une façade qui
> mente sur ce qu'elle groupe est pire qu'une façade serrée »).
>
> **UN SECOND DÉFAUT, TROUVÉ EN CHEMIN ET RÉPARÉ D'ABORD.** `hauteurUtile()` ne
> se trompait pas seulement de rangées (attendu écrit plus haut) : il comptait la
> marge de cellule — 3 px en haut et en bas — **une fois par bloc** alors que
> `resized()` la retire de **chaque rangée**. Un bloc de quatre rangées en payait
> une et en devait quatre. Les deux erreurs étaient invisibles pour la même
> raison : le plancher d'une rangée (70 px) couvre tout bloc de deux rangées ou
> moins, et aucune façade n'en avait davantage. La colonne du sampler en a
> quatre, et la cellule serait tombée à 20 px avec un bouton de 8. La formule
> écrite — `(rangées de commandes x 36 + 34) / rangées de grille` — rend
> exactement l'ancienne valeur sur les soixante-trois façades d'aujourd'hui, ce
> que la colonne « identiques au pixel » vérifie plutôt que de le supposer.
>
> **L'ATTENDU N° 4 N'EST PAS TENU, ET SA BARRE ÉTAIT FAUSSE.** J'attendais que la
> plus haute des quatre reste « sous deux fois la hauteur du rack ». Le sampler
> passe de 540 à **680 px** pour un volet de rack de **330 px** à 900x660, soit
> **2,06 fois** — au-dessus de la barre. Mais cette barre a été écrite sans que je
> sache ce que valait le volet : je l'imaginais vers 470. Mesurée, elle condamne
> **dix façades que la série D65–D66 a délibérément créées** et dont personne ne
> s'est plaint — `vsm.wavetable` tient 1 030 px, soit 3,1 volets. Le sampler
> devient la onzième, et la plus basse des onze. La barre ne mesurait donc pas ce
> que je croyais : elle disait « cette façade est-elle plus haute que la
> moyenne », pas « chercher un réglage y coûte-t-il trop de défilement ». Je la
> retire au lieu de la contourner, et je publie le chiffre qu'elle visait :
> **11 façades sur 63 dépassent deux volets, contre 10 avant la phase.**
>
> **UNE QUATRIÈME MESURE, QUI DIT NON.** Avant de conclure, j'ai cherché un
> garde-fou sans pixels : « un bloc ne range jamais autant de colonnes de
> commandes qu'il a de colonnes de grille ». Il est vrai des quatre façades
> corrigées — et **vingt-six blocs du parc le violent en mesurant tous au-dessus
> de 18 px** (`vsm.chebyshev`, les six opérateurs du DX7, sept enveloppes…). La
> largeur réclamée par la grille reste un proxy, et le diamètre rendu reste le
> fait : c'est la leçon de D67, vérifiée une fois de plus dans la phase qui
> corrige D67. **Il n'y a donc pas de test de données qui tienne ce plancher**, et
> ce n'est pas un manque à combler : le garde-fou est le témoin, et il est
> désormais permanent.
>
> **VU À L'ÉCRAN**, à 900x660, les quatre façades. Le sampler montre huit
> colonnes colorées — « 1 KICK », « 2 SN… », « 3 HH… » jusqu'à « 8 RIDE » —,
> chacune LVL, TUNE, DEC, PAN de haut en bas : c'est un panneau de boîte à
> rythmes, ce que le carré de deux sur deux ne donnait pas. Le TR-909 montre
> BASS DRUM et SNARE DRUM en gros potentiomètres, TOM / CLAP en deux colonnes et
> CYMBAL / HAT en trois. Les FM drums montrent leurs quatre blocs, TOM / BELL en
> quatre colonnes. **Ce qui reste visible et n'est pas nouveau** : les titres des
> emplacements du sampler s'abrègent (« 2 SN… ») — deux colonnes de grille font
> 49 px, et c'était déjà le cas avant la phase.
>
> Tests : 327 core, 1 291 audio, 292 interchange, 25 clap, 11 panels, 172 Python,
> ruff et mypy — tout vert.

### Phase D71 — Le vingt-quatrième audit : D52 a corrigé UN rapport jeté, et n'a pas demandé si ses frères l'étaient aussi (10/09/2026, 14:20)

**LA LUNETTE, ET POURQUOI ELLE EST NEUVE.** D52 a trouvé quatre appelants qui
jetaient `PresetApplyReport` — le rapport dont l'en-tête promet que « rien n'est
jamais appliqué en douce ». Elle les a réparés, et elle s'est arrêtée là. Or ce
rapport n'est pas seul : le dépôt en compte **neuf de la même famille**, tous
bâtis sur la même idée — une fonction qui applique quelque chose et rend la liste
de ce qu'elle n'a pas pu appliquer. **Personne n'a demandé si les huit autres
étaient lus.** C'est la faute de D52 sous une forme de plus : réparer l'exemplaire
qu'on a trouvé plutôt que la classe à laquelle il appartient.

**ET UNE SECONDE LUNETTE, QUI DONNE À LA PREMIÈRE SON TÉMOIN.** Un projet a
**deux lecteurs** : l'application, et le rendu hors ligne de `vsm-render`. D46 a
vérifié qu'ils rendent le même SON, au bit près. Personne n'a vérifié qu'ils
disent les mêmes ANOMALIES. Or c'est le même dossier, les mêmes structures et la
même règle — « panne muette interdite » —, et si les deux divergent, l'un des
deux ment.

**CE QUE L'AUDIT A RENDU.** Sur le même `project.json`, quatre anomalies que le
rendu hors ligne NOMME et que l'application AVALE :

| anomalie dans le projet | `vsm-render` | l'application |
|---|---|---|
| insert d'un type inconnu | « Piste 3 : effet « xyz » inconnu, non appliqué » | **rien** |
| insert portant un réglage inconnu | « …, réglage inconnu « q » » | **rien** |
| bus de départ d'un type inconnu | « Bus de départ « Rev » : effet « xyz » inconnu » | **rien** |
| bus de départ portant un réglage inconnu | « …, réglage inconnu « q » » | **rien** |

Les trois appelants fautifs sont `EffectChainComponent.cpp:273`, `:761` et
`MainComponent.cpp:7234` : tous trois appellent `applyEffectDescription` et
**jettent l'`EffectApplyReport` qu'elle rend**. Deux sites de plus sautent un
effet dont le type est inconnu sans un mot (`EffectChainComponent.cpp:266`,
`MainComponent.cpp:7224`) — et le commentaire du premier explique soigneusement
pourquoi on ne le REMPLACE pas, sans se demander s'il faut le DIRE.

**CE QUE CELA COÛTE À L'UTILISATEUR.** Il ouvre un projet écrit par une version
où l'effet s'appelait autrement, ou par la chaîne d'analyse avec un insert que
cette version ne connaît pas. Le projet s'ouvre, la piste joue, la description
de l'effet reste dans le fichier et se réécrit intacte — et le son n'a pas
l'effet. Rien à l'écran ne le dit ; le mélangeur montre un insert qui existe
dans le projet et pas dans le graphe. Puis il exporte, et **`vsm-render` le lui
dit** : deux lecteurs, deux vérités, et celle qui parle est celle qu'on consulte
le moins.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA CORRECTION (10/09/2026, 14:20).**
>
> 1. **Les quatre lignes du tableau passent de « rien » à une phrase**, et la
>    phrase NOMME l'effet et le réglage — un compte sans les noms ne se vérifie
>    pas (la leçon du test de D52).
> 2. **La phrase se lit sans souris.** L'écran de rapport d'ouverture est une
>    fenêtre que `VSM_CAPTURE` photographie ; la ligne part AUSSI sur la sortie
>    d'erreur (`VSM_EFFET : …`), comme `VSM_PRESET` en D52, pour qu'un banc sans
>    écran la relise. La capture DOIT montrer l'écran de rapport, faute de quoi
>    ce sont deux chemins déclarés et un seul vérifié.
> 3. **Un projet sain ne dit rien.** Aucune ligne ajoutée quand tous les effets
>    sont connus et tous leurs réglages compris : un rapport qui parle toujours
>    ne se lit plus.
> 4. **Les sept autres rapports de la famille sont comptés**, appelant par
>    appelant, et le compte est publié ici — y compris s'il est nul. C'est la
>    partie de D52 qui n'a pas été faite, et la refaire à moitié serait la
>    refaire pour rien.

> **D71 EST FAITE (10/09/2026, 16:05). LES QUATRE ATTENDUS SONT TENUS, ET LA
> VÉRIFICATION DU SECOND A TROUVÉ UN DÉFAUT DU BANC LUI-MÊME.**
>
> **Mesuré sur un projet abîmé exprès** — un insert de type inconnu, un insert
> portant deux réglages inconnus, un bus de départ de type inconnu, un bus de
> départ portant deux réglages inconnus — ouvert par l'application, et rendu par
> le `vsm-render` du dépôt (binaire d'avant cette phase, donc témoin intact) :
>
> | | avant | après |
> |---|---|---|
> | lignes dites par l'application | **0** | **6** |
> | lignes dites par `vsm-render` | 6 | 6 |
> | le mot `VSM_EFFET` dans le binaire | **absent** | présent |
> | projet sain (63 façades) | 0 | **0** |
> | vraie reconstruction (`sky-v4`) | 0 | **0** |
>
> Le « 0 » d'avant n'est pas une lecture du diff : le binaire d'avant a été
> reconstruit (`git stash`), lancé sur le même dossier, et il n'a rien dit.
>
> **CE QUE L'APPLICATION ÉCRIT MAINTENANT**, mot pour mot :
>
> ```
> VSM_EFFET : bus de départ « Rev » : effet « reverb-a-plaque-de-1974 » inconnu, non appliqué
> VSM_EFFET : bus de départ « Delai » : réglage inconnu « delay.retro.inverse »
> VSM_EFFET : Piste 1 : effet « chorus-de-1987 » inconnu, non appliqué
> VSM_EFFET : Piste 2 : effet « reverb » : réglage inconnu « reverb.densite.stochastique »
> ```
>
> **LA RÉPONSE À L'ATTENDU N° 4 : DEUX FAMILLES SUR NEUF, ET NON UNE.** Les neuf
> rapports ont été suivis appelant par appelant. Sept sont lus partout. Deux ne
> l'étaient pas :
>
> | famille | sites qui la jetaient | où |
> |---|---|---|
> | `EffectApplyReport` | **3** | `EffectChainComponent.cpp:273` et `:761`, `MainComponent.cpp:7234` — tous dans `app/` |
> | `MultisampleProfileApplyReport::ignored` | **1** | `SynthPreset.cpp:318`, c'est-à-dire le chemin qu'emprunte un PROJET qui s'ouvre ; `PatchRenderService` le publiait déjà |
>
> Deux sites de plus sautaient un effet de type inconnu sans un mot. La seconde
> famille a demandé une troisième catégorie dans `SampleLoadReport` — les
> **réserves**, qui ne sont ni un chargement ni un échec — et un test tient
> désormais son contrat, y compris qu'un preset dont tout se charge ne dise
> RIEN : un rapport qui parle toujours ne se lit plus.
>
> **LE BANC A UN DÉFAUT, ET C'EST LA VÉRIFICATION DE L'ATTENDU N° 2 QUI L'A
> TROUVÉ.** L'écran de rapport a bien été photographié, avec ses six lignes
> (`VSM_CAPTURE_PANNEAUX`, D55.2) — **une fois sur sept tentatives**, à des
> délais de 1 800 à 3 000 ms, sans que le délai explique quoi que ce soit : ce
> n'est pas une attente trop courte, c'est une course. La boîte est ouverte par
> `showMessageBoxAsync`, et le photographe passe sans la voir la plupart du
> temps. **Cela corrige aussi D52**, qui écrivait « une `AlertWindow` est une
> fenêtre à part et n'y figure pas » : elle y figure, par intermittence — ce qui
> est pire, parce qu'un banc qui rate sa cible six fois sur sept et la trouve la
> septième laisse croire, le jour où il la trouve, qu'il la trouve toujours.
> **Nommé, chiffré, non fait** : la ligne `VSM_EFFET` sur la sortie d'erreur,
> elle, est déterministe, et c'est elle qui porte la preuve ici.
>
> **DEUX DIVERGENCES DE PLUS ENTRE LES DEUX LECTEURS, NOMMÉES ET NON FAITES.**
>
> 1. **La numérotation des pistes.** L'application dit « Piste 1 », `vsm-render`
>    dit « Piste 0 » — pour la même piste et la même anomalie. Chacun est
>    cohérent avec lui-même (l'application compte à partir de 1 partout dans son
>    écran de rapport) ; mis côte à côte, ils se contredisent. Non corrigé
>    **parce que le corriger demanderait de recompiler `vsm-render`, et qu'une
>    campagne tourne** — remplacer ce binaire tue la course (règle du dépôt).
> 2. **`vsm-render` dit « Piste 2 () : aucun instrument, elle restera
>    silencieuse » ; l'application ne le dit pas.** C'est une divergence sur les
>    INSTRUMENTS, pas sur les effets ; elle sort de la lunette de cette phase et
>    y entre par la porte que cette phase vient d'ouvrir — la comparaison des
>    deux lecteurs sur le même dossier. Elle attend sa mesure.
>
> **CORRIGÉ EN CHEMIN, PARCE QUE LA CAPTURE LE MONTRAIT** : le titre de la boîte
> lisait « Projet ouvert, avec des reserves », sans son accent. Il passe par
> `juce::String::fromUTF8`, la seule façon d'écrire un accent ici sans le
> transformer en « rÃ©serves ».
>
> Tests : 327 core, 1 291 audio, **293** interchange (un de plus), 25 clap,
> 11 panels, 172 Python, ruff et mypy — tout vert.

### Phase D72 — A7 : le rapport d'ouverture était une boîte modale que le banc ratait six fois sur sept (10/09/2026, 16:40)

**CE QUE D71 A LAISSÉ.** L'écran de rapport d'ouverture — celui qui dit ce qu'un
projet a perdu en s'ouvrant — passe par `juce::AlertWindow::showMessageBoxAsync`.
D71 l'a photographié **une fois sur sept**, à des délais de 1 800 à 3 000 ms, et
a montré que ce n'est pas un délai trop court mais une course. Un banc qui rate
sa cible six fois sur sept est pire qu'un banc qui la rate toujours : le jour où
il la trouve, on croit qu'il la trouve toujours.

**ET L'APPLICATION A DÉJÀ L'ÉCRAN QU'IL FAUT.** `ImportReportComponent` est un
volet DANS la fenêtre principale — donc dans l'autoportrait —, avec son
défilement, son repli, son bouton Copier, ses quatre tons et son `reportText()`
« que le test peut lire sans avoir à déchiffrer une image ». Il sert déjà
l'import DAW et le rapport de reconstruction. **Le rapport d'ouverture est son
troisième client naturel, et il était le seul à ouvrir une boîte.**

**CE QUE LA BOÎTE COÛTAIT, AU-DELÀ DU BANC.** Elle est modale : elle arrête le
travail pour une liste qu'on ne peut ni faire défiler, ni copier, ni rouvrir.
Un projet reconstruit qui perd huit presets affichait huit lignes dans une
alerte, et une fois « OK » cliqué, plus rien ne les redonnait — alors que
« Voir le dernier rapport » existe au menu et ne les a jamais vues.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 16:40).**
>
> 1. **Le rapport se photographie à tous les coups** : dix captures sur dix,
>    dans l'autoportrait de la fenêtre principale, sans `VSM_CAPTURE_PANNEAUX`
>    — contre 1 sur 7 aujourd'hui, et seulement avec lui.
> 2. **Les lignes sont les mêmes, au mot près.** Ce n'est pas une phase qui
>    change ce qui est dit ; elle change où c'est dit. Comparé par
>    `reportText()`, pas à l'œil.
> 3. **Un projet sain n'ouvre rien.** La règle de D71 tient : aucun écran quand
>    il n'y a rien à dire.
> 4. **Le rapport se ROUVRE.** « Voir le dernier rapport » le retrouve après
>    fermeture, ce que la boîte ne permettait pas.

> **D72 EST FAITE (10/09/2026, 17:10), ET LES QUATRE ATTENDUS SONT TENUS.**
>
> | | avant (D71) | après |
> |---|---|---|
> | captures montrant le rapport | **1 / 7**, et seulement avec `VSM_CAPTURE_PANNEAUX` | **10 / 10**, dans l'autoportrait ordinaire |
> | lignes affichées | 7 | **7, au mot près** |
> | projet sain | rien | **rien** |
> | rouvrable après fermeture | non | **oui** (« Voir le dernier rapport d'import ») |
>
> **LA MESURE NE CROIT PAS LE NOM DU FICHIER.** « Dix captures écrites » ne dit
> rien : un PNG s'écrit même si le volet n'est pas là. Le témoin lit **le pixel
> (450, 200)**, au cœur du bandeau du rapport — `srgba(31,31,36)` sur les dix, la
> couleur du volet, contre `srgba(7,7,9)` sur le projet sain, qui est la fenêtre
> nue. C'est la leçon de D49 : on lit l'IMAGE, pas la trace qui dit qu'on l'a
> écrite.
>
> **UN DÉTAIL QUE SEULE LA CAPTURE POUVAIT MONTRER, ET QUI ÉTAIT À L'ENVERS.**
> La première version peignait les effets non appliqués en ambre discret et
> l'avertissement le plus anodin — « le projet décrit 2 pistes, le MIDI en
> contient 3 » — en rouge vif. Le volet a en effet deux tons dont les noms
> trompent qui ne lit pas leur table : **`attention` est ROUGE** (« regarde
> maintenant ») et **`perte` est AMBRE** (« ceci a été perdu »). Les deux autres
> clients du volet suivent déjà cette convention — l'écran de reconstruction
> range ses stems perdus en ambre et ses parts anormales en rouge. Le rapport
> d'ouverture la suit maintenant : ses six pertes en ambre, sa note de comptage
> en gris.
>
> **CE QUE LA BOÎTE EMPORTAIT AVEC ELLE**, et qui revient sans qu'on l'ait
> demandé : le défilement (une reconstruction qui perd huit presets tenait mal
> dans une alerte), le bouton **Copier**, la fermeture par Échap, et
> `reportText()` — c'est-à-dire un rapport qu'un test peut lire « sans avoir à
> déchiffrer une image », ce que l'en-tête du volet promettait déjà à ses deux
> autres clients.
>
> **A7 EST CLOSE.** Reste A8 — les deux lecteurs qui ne numérotent pas les
> pistes pareil —, qui attend la fin de la campagne : recompiler `vsm-render`
> pendant qu'elle tourne tue la course.
>
> Tests : 327 core, 1 291 audio, 293 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D73 — L'anglais comme seconde langue de l'interface (10/09/2026, 18:00)

**LA DEMANDE, ET LE MOT QUI LA BORNE.** « Ajoute **aussi** l'anglais comme
langue. » L'anglais s'ajoute au français, il ne le remplace pas : le français
reste la langue par défaut, et l'anglais est un choix.

**CE QUE CELA OUVRE, ET CE QUE CELA N'OUVRE PAS.** L'application compte
**33 181 lignes** et **2 581 chaînes littérales**, dont environ mille
ressemblent à du texte affiché — le reste étant des identifiants, des clés de
préférences et des traces de banc. Traduire les mille d'un coup, à la main,
serait long et surtout invérifiable : on ne saurait pas ce qui a été oublié.
La phase pose donc **le mécanisme et les surfaces que l'utilisateur lit en
premier**, et elle **publie sa couverture** plutôt que de laisser croire à une
traduction complète.

**TROIS DÉCISIONS, ÉCRITES ICI PLUTÔT QUE DEVINÉES.**

1. **La table est indexée par le FRANÇAIS.** C'est le mécanisme de
   `juce::LocalisedStrings` : la chaîne d'origine est la clé. Une chaîne sans
   traduction ressort donc **en français**, jamais vide et jamais en clé brute
   — la dégradation est lisible, ce qui est la condition pour livrer une
   couverture partielle sans mentir.
2. **Le passage par `tr()` corrige un piège au passage.** `juce::String(const
   char*)` lit les octets en **Latin-1** — c'est le défaut qui rendait
   « s'éclaircit » en « s'Â©claircit » dans le rack, et qui a obligé D71 à
   écrire un en-tête de colonne sans accent. `tr()` a deux surcharges, `const
   char*` et `const char8_t*` (les littéraux `u8"…"` du C++20), et **passe
   toujours par `fromUTF8`**. Traduire et bien lire les accents deviennent le
   même geste.
3. **Le changement de langue est IMMÉDIAT, pas au prochain démarrage.** Cubase
   et Live demandent un redémarrage, et c'était la solution facile. Elle est
   refusée : la barre de menus se reconstruit à chaque ouverture, et les
   libellés fixes qu'on traduit ici tiennent dans une seule fonction de
   re-traduction. Une interface qui demande de relancer le logiciel pour lire
   son propre menu dans sa langue est une interface qui n'a pas fini le travail.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 18:00).**
>
> 1. **La barre de menus est traduite ENTIÈREMENT** — les sept menus et toutes
>    leurs entrées. C'est la surface qu'on lit en premier et la seule qu'on ne
>    peut pas contourner ; une barre à moitié anglaise serait pire que rien.
>    Mesuré en comptant les chaînes qui passent par `tr()` et celles qui ont une
>    traduction, pas à l'œil.
> 2. **Rien ne sort vide ni en français au milieu de l'anglais**, sur les
>    surfaces déclarées traduites. Une capture en anglais et une en français,
>    côte à côte, et la liste des chaînes sans traduction publiée — même si
>    elle est vide.
> 3. **Le français ne bouge pas d'un pixel.** La table est un aiguillage : en
>    français, `tr()` rend son argument. Vérifié par capture, avant et après.
> 4. **La langue se choisit sans souris et se conserve.** `VSM_LANGUE=en|fr`
>    pour le banc, le menu *Affichage ▸ Langue* pour l'utilisateur, et le choix
>    survit au redémarrage comme l'échelle d'interface.
> 5. **Le libellé de menu neuf est UNIQUE dans toute la barre.** Piège payé le
>    06/09 : `VSM_MENU` prend le premier libellé exact tous menus confondus, et
>    « Automatique » avait piloté les threads de rendu au lieu du mode d'écoute.

> **RECTIFICATIF DE D72 (10/09/2026, 19:30), TROUVÉ EN VÉRIFIANT D73.** Le
> « 10 / 10 » de D72 est juste ; **la façon dont je l'ai vérifié ne l'était
> pas**, et il faut le dire parce que la méthode est ce que les phases suivantes
> réemploient.
>
> D72 lisait **un pixel**, (450, 200), et le comparait à un « projet sain ». Deux
> fautes dans une seule mesure : la couleur `srgba(31,31,36)` n'est pas propre au
> volet de rapport — c'est aussi le fond du piano roll et des façades —, et le
> « projet sain » n'était pas sain : le projet des 63 façades décrit 63 pistes
> pour 64 tronçons MIDI, et il ouvre donc un rapport lui aussi. Ce rapport-là ne
> tenant qu'UNE ligne, il est plus court, et le point (450, 200) tombait
> au-dessus de lui. Un témoin qui répond « rien » parce qu'il regarde à côté est
> le pire des témoins : il confirme.
>
> **REFAIT AVEC UNE SONDE SPÉCIFIQUE**, l'empreinte de la bande du titre du
> rapport (280x24 px à partir de (36, 188)), et un projet **vraiment** sain —
> deux pistes décrites, deux tronçons MIDI, aucun avertissement :
>
> | | résultat |
> |---|---|
> | projet abîmé, dix captures | **10 / 10** portent le bandeau, empreinte identique |
> | projet vraiment sain | **aucun rapport** — la fenêtre nue |
>
> La conclusion de D72 tient donc, et sa preuve est maintenant falsifiable. La
> leçon est celle de D49 encore une fois, un cran plus loin : **il ne suffit pas
> de lire l'image, il faut lire un endroit de l'image qui ne peut pas dire oui
> par hasard** — et le contre-exemple doit être un vrai contre-exemple.

> **D73 EST FAITE (10/09/2026, 19:45), ET LES CINQ ATTENDUS SONT TENUS.**
>
> | attendu | mesuré |
> |---|---|
> | 1. barre de menus traduite entièrement | **227 / 227** chaînes, **0** sans traduction |
> | 2. rien de vide ni de français sur les surfaces déclarées | captures FR et EN côte à côte |
> | 3. le français ne bouge pas | **0 pixel** de différence sur la bande des menus |
> | 4. langue sans souris et conservée | `VSM_LANGUE`, menu, et relance sans variable |
> | 5. libellés de menu neufs uniques | « Langue », « Français », « English » |
>
> **LA TABLE COMPTE 270 PAIRES**, écrites à la main. La traduction automatique se
> trompe précisément là où cela compte : « piste » est *track*, « prise » est
> *take* et non *plug*, « écoute » est *monitoring* et non *listening*, « départ »
> est *send*. Ces mots ont un sens fixé par le métier, et c'est celui-là qu'un
> musicien cherche dans un menu.
>
> **LE FRANÇAIS EST INTACT, ET C'EST MESURÉ PLUTÔT QU'AFFIRMÉ.** La bande de la
> barre de menus, comparée entre une capture d'avant la phase et une d'après :
> **0 pixel** de différence. La même sonde entre le français et l'anglais :
> **2 876 pixels** — la mesure n'est donc pas aveugle, ce qu'un « 0 » tout seul
> ne prouverait pas (la leçon du rectificatif ci-dessus, appliquée le jour même).
> Les deux ou trois pixels qui bougent dans la bande du transport sont le
> **compteur de charge**, qui bat en continu.
>
> **LA LANGUE SE CONSERVE**, vérifié en la posant par le menu puis en relançant
> **sans** `VSM_LANGUE` : la barre revient en anglais. Et `VSM_LANGUE` ne
> l'écrase pas — un banc choisit sa langue pour une course sans changer le
> réglage de l'utilisateur, sans quoi une vérification en anglais laisserait le
> DAW en anglais.
>
> **CE QUI N'EST PAS TRADUIT, NOMMÉ ET CHIFFRÉ.** La phase couvre la barre de
> menus (227), les onglets du dock, la barre de transport, la liste de pistes et
> l'écran de rapport — 270 chaînes. Restent en français, et c'est écrit ici
> plutôt que découvert à l'usage :
>
> | surface | pourquoi |
> |---|---|
> | les phrases des rapports (« effet « … » inconnu, non appliqué ») | elles sont fabriquées dans `interchange/`, qui ne dépend pas de JUCE et n'a donc pas de table ; les traduire demande son propre mécanisme |
> | les panneaux du dock (mixeur, automation, effets, liste, tempo) | chacun a ses libellés, et ce sont autant de `retraduire()` à écrire |
> | les fenêtres flottantes (préférences, raccourcis, historique…) | même raison |
> | les sérigraphies des façades (« CUTOFF », « BASS DRUM ») | **ne se traduisent dans aucun sens** : elles imitent des machines réelles |
>
> Une chaîne sans traduction ressort **en français**, jamais vide et jamais en
> clé technique : c'est ce qui rend cette couverture partielle honnête, et ce qui
> permet de l'étendre une ligne à la fois.
>
> **UN PIÈGE RÉPARÉ EN PASSANT.** `tr()` a deux surcharges, `const char*` et
> `const char8_t*` (les `u8"…"` du C++20), et passe **toujours** par `fromUTF8`.
> Traduire une chaîne et lire ses accents correctement sont devenus le même
> geste — là où `juce::String(const char*)` lit ses octets en Latin-1 et rendait
> « s'éclaircit » en « s'Â©claircit ».
>
> Tests : 327 core, 1 291 audio, 293 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D74 — La barre du piano roll, le mélangeur, et deux conventions de noms de notes à trois centimètres l'une de l'autre (10/09/2026, 21:15)

**CE QUE D73 A LAISSÉ (A9).** La barre de menus est traduite entièrement ; les
panneaux du dock et la barre du piano roll ne le sont pas. Cette phase prend
**les surfaces qui sont à l'écran en permanence** — la barre du piano roll et le
mélangeur — et laisse les fenêtres flottantes et les menus contextuels de
l'arrangement à une passe suivante, en les comptant.

**ET ELLE TROUVE AUTRE CHOSE, QUI N'EST PAS UNE AFFAIRE DE LANGUE.**
L'application écrit les noms de notes de **deux façons différentes, dans la même
fenêtre** :

| où | ce qui s'affiche |
|---|---|
| clavier du piano roll | `C#2` |
| liste des événements | `C#2 (37)` |
| séquenceur des boîtes à rythmes | `C#2` |
| **sélecteur de tonique de la barre du piano roll** | **`Do#`** |

Le commentaire de `EventListComponent.cpp:145` avait vu la moitié du problème —
« le piano roll, à trois centimètres de là, écrivait C#2 sur son clavier » — et
l'a corrigé pour la liste, sans regarder la barre juste au-dessus.

**LA DÉCISION, ET SA RAISON.** Les noms de notes sont des **lettres**, dans les
deux langues : `C`, `C#`, `D`… Ce n'est donc PAS une entrée de la table de
traduction, et c'est délibéré. Trois raisons, dans l'ordre où elles pèsent :

1. **C'est ce que fait l'étalon nommé par ce document.** Cubase et Ableton Live
   en français affichent `C3`, pas `Do3`. Le § 2 dit que le DAW se juge à leur
   aune ; sur ce point précis, l'usage du métier a tranché avant nous.
2. **L'application le fait déjà partout ailleurs.** Trois surfaces sur quatre
   écrivent des lettres ; c'est le sélecteur de tonique qui est l'exception, pas
   la règle.
3. **`noteNumberToName` vit dans `core/`, et doit y rester en lettres.** Il ne
   sert pas qu'à l'affichage : `Project.cpp:753` s'en sert pour NOMMER les
   pistes qu'« Éclater par hauteur » fabrique, et ce nom part dans le fichier de
   projet. Un nom de piste qui changerait selon la langue de l'interface
   changerait le contenu enregistré — exactement ce que le § 6 interdit.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 21:15).**
>
> 1. **Une seule convention de noms de notes dans toute l'application**, et
>    c'est celle des lettres. Vérifié en cherchant les deux tables : il ne doit
>    en rester qu'une.
> 2. **La barre du piano roll et le mélangeur sont traduits**, et la couverture
>    est publiée comme en D73 : combien de chaînes, combien traduites.
> 3. **Les infobulles longues restent en français, et c'est écrit plutôt que
>    subi.** Elles sont fabriquées en collant trois ou quatre morceaux de phrase
>    répartis sur autant de lignes ; en traduire la moitié donnerait une phrase
>    mi-française mi-anglaise, ce qui est pire qu'une phrase non traduite. Elles
>    demandent d'être recollées d'abord, ce qui est un travail à part.
> 4. **Le français ne bouge pas**, sauf sur le sélecteur de tonique, qui est le
>    défaut corrigé — et cette exception est mesurée, pas affirmée.

> **D74 EST FAITE (10/09/2026, 22:05), ET LES QUATRE ATTENDUS SONT TENUS.**
>
> | attendu | mesuré |
> |---|---|
> | 1. une seule convention de noms de notes | **une** — la recherche de « Do », « Ré », « Sol », « Si » dans `app/` et `core/` ne rend plus rien |
> | 2. barre du piano roll traduite | **27 / 27** chaînes, 0 sans traduction ; table à **289** paires |
> | 3. infobulles longues laissées en français | écrit, avec la raison |
> | 4. le français ne bouge pas, sauf le sélecteur de tonique | vérifié par capture |
>
> **CE QUE LA SECONDE LANGUE A RÉVÉLÉ, ET QUE LA PREMIÈRE CACHAIT DEPUIS
> TOUJOURS.** Le sélecteur de subdivision est large de 78 px, ce qui suffit à
> « Droit » et **pas** à « Straight » : la capture en anglais rendait
> « Strai… ». La case est passée à **96 px**. C'est la règle de lisibilité du
> projet appliquée telle quelle — entre « ça tient dans la case » et « ça se
> lit », c'est la lisibilité qui prime, et l'on agrandit la case plutôt que de
> rétrécir le texte. **Une largeur taillée sur une seule langue est un défaut
> que seule la seconde langue révèle** : c'est le premier bénéfice de la
> traduction qui n'a rien à voir avec la traduction.
>
> **LES ABRÉGÉS NE SE TRADUISENT PAS COMME DES ABRÉGÉS.** « Coup. » devient
> « Cut » et non « Cu. », « Dess. » devient « Draw » : on traduit le MOT ENTIER
> puis on regarde s'il faut l'abréger, et en anglais il ne le faut presque
> jamais — les mots y sont plus courts. Traduire l'abrégé aurait fabriqué des
> moignons qui ne veulent rien dire.
>
> **CE QUI RESTE, COMPTÉ.** Les infobulles longues du mélangeur et de la barre
> — celles qui expliquent le Trim, la polarité inversée, l'écriture
> d'automation — sont **fabriquées en collant trois ou quatre morceaux de phrase
> répartis sur autant de lignes de code**. En traduire la moitié donnerait une
> phrase mi-française mi-anglaise, ce qui est pire qu'une phrase non traduite ;
> elles demandent d'être recollées d'abord. Restent aussi les menus contextuels
> de l'arrangement (109 chaînes brutes), les fenêtres flottantes et les autres
> panneaux du dock. A9 reste ouvert, avec ce compte.
>
> Tests : 327 core, 1 291 audio, 293 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D75 — A8 : les deux lecteurs comptaient les pistes différemment, et l'application comptait déjà des deux façons à elle seule (10/09/2026, 16:30)

**CE QUE D71 A LAISSÉ (A8).** Sur le même dossier, l'application écrit
« Piste 1 » et `vsm-render` « Piste 0 » pour la même piste et la même anomalie ;
et `vsm-render` dit d'une piste MIDI sans machine qu'« elle restera
silencieuse », quand l'application se tait. D71 l'a nommé et ne l'a pas corrigé
parce qu'une campagne tournait. **Aucune ne tourne aujourd'hui** — `pgrep` ne
rend ni `vsm-render`, ni chaîne d'analyse, ni séparation, vérifié avant
d'écrire ces lignes —, et recompiler `vsm-render` ne tue plus rien.

**L'INVENTAIRE CHANGE LA NATURE DU DÉFAUT.** Ce ne sont pas « deux lecteurs qui
comptent différemment » : **l'application affiche déjà les deux
numérotations**, parce qu'elle recopie telles quelles des phrases fabriquées
dans `interchange/`.

| où, dans l'application | ce qui s'affiche pour la première piste |
|---|---|
| liste de pistes, mélangeur, menus contextuels | « Piste 1 » |
| rapport d'ouverture, lignes écrites par l'application (preset, échantillons, effets) | « Piste 1 » |
| rapport d'ouverture, ligne venue de `loadProjectBundle` (preset introuvable) | « piste **0** » |
| message d'export, lignes venues du rendu hors ligne | « Piste **0** » |

Un rapport d'ouverture peut donc écrire « Piste 1 : … » et « Preset introuvable
pour la piste 0 » **de la même piste, l'une sous l'autre**. Le compte : **14**
phrases de `interchange/` numérotent à partir de 0 (13 dans le rendu hors ligne,
une au chargement), et aucune de l'application.

**LA DÉCISION : À PARTIR DE 1, PARTOUT OÙ UN HUMAIN LIT.** Trois raisons :

1. **C'est ce que montre l'écran** où l'on va chercher la piste — la liste, le
   mélangeur, les menus. Une phrase qui nomme une piste doit la nommer comme
   l'endroit où on la cherche.
2. **C'est ce que font Cubase et Live**, l'étalon du § 2.
3. **Les index à partir de 0 restent là où ce sont des index** : les noms de
   fichiers (`track_00.synth.json`), le JSON, les champs des structures. Ce ne
   sont pas des phrases, et les changer casserait des projets écrits.

**LA PISTE SANS MACHINE, ET LA PISTE À MACHINE INCONNUE.** **Les deux lecteurs
fabriqueront désormais ces phrases par la même fonction**, dans
`interchange/` : corriger le texte des deux côtés à la main les ferait
rediverger à la prochaine retouche, et c'est exactement ainsi que D71 les a
trouvés divergents.

> **RECTIFICATIF, ÉCRIT APRÈS LA MESURE « AVANT » ET AVANT LA CORRECTION
> (10/09/2026, 16:50).** La première version de ce paragraphe affirmait qu'une
> piste désignant une machine absente de ce build, et sans preset, « s'ouvre
> muette, sans un mot » dans l'application. **C'est faux, et le banc l'a
> montré** : le chargement (`applyDocumentToProject`) VIDE l'identifiant d'une
> machine absente et écrit « Instrument manquant : com.autre… », ligne que les
> deux lecteurs recopient. L'inventaire avait lu le code de l'application seul ;
> la ligne vient d'en dessous. Ce que la mesure montre à la place est un autre
> défaut : cette ligne **ne dit pas de quelle piste elle parle** — deux pistes
> à machine absente donnent deux lignes identiques —, et le rendu redit la même
> piste « aucun instrument, elle restera silencieuse » juste en dessous. Deux
> lignes pour un seul manque, dont une fausse : la piste n'est pas sans
> instrument, elle en demande un qu'on n'a pas. La ligne du chargement prend
> donc le numéro de sa piste et la phrase commune (« Piste 3 : instrument « … »
> indisponible »), et « aucun instrument » ne se dit plus d'une piste dont la
> machine a été demandée.

**UNE FAUSSE PISTE ÉCARTÉE, ÉCRITE POUR QU'ON NE LA REPRENNE PAS.** Le message
d'export recopie les avertissements du rendu par `juce::String(warning)`, et
D73 a montré que `juce::String(const char*)` lit ses octets en Latin-1. Le
soupçon était donc un « rÃ©Ã©chantillonnÃ© » dans le message d'export. **Il est
faux** : le constructeur depuis `std::string` passe par `CharPointer_UTF8`
(`juce_String.cpp:386`, `createFromFixedLength`). Seul le `const char*` nu
lit du Latin-1. Vérifié dans la source de JUCE avant de l'écrire, et pas
corrigé, puisqu'il n'y a rien à corriger.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA CORRECTION (10/09/2026, 16:30).**
>
> 1. **Chaque anomalie que les deux lecteurs voient porte le MÊME numéro des
>    deux côtés**, et c'est celui de la liste de pistes. Mesuré sur un projet
>    abîmé exprès, en comparant les lignes de l'application à celles de
>    `vsm-render` — un diff de texte, pas une lecture d'image.
> 2. **La piste MIDI sans machine et la piste à machine inconnue sont dites par
>    l'application**, avec la phrase de `vsm-render` au mot près — ce qui n'est
>    tenable que si c'est la même fonction qui l'écrit.
> 3. **Ni un bus de groupe, ni une piste audio, ni une piste désactivée ne sont
>    dits « sans instrument »**, par aucun des deux. Une piste désactivée est
>    sortie du morceau par un geste de l'utilisateur (D30.2) : son silence est
>    voulu, et le dire noierait les vrais avertissements — la leçon du bus de
>    batterie de sky-parite. **Ajouté à 16:40, avant toute mesure**, quand
>    l'énumération `Track::Kind` a montré un quatrième genre : **ni un
>    dossier** (`Kind::Folder`, « ne joue rien »), que la règle du rendu —
>    « tout sauf l'audio et le groupe » — faisait dire silencieux.
> 4. **Un projet sain ne dit toujours rien** (règle de D71), et un vrai projet
>    reconstruit ne gagne aucune ligne qui ne soit vraie : compté avant et après.
> 5. **Le rapport d'ouverture part AUSSI sur la sortie d'erreur**, ligne par
>    ligne (`VSM_OUVERTURE : …`), comme `VSM_EFFET` en D71 : c'est ce qui fait
>    du n° 1 un diff et non une impression.

> **D75 EST FAITE (10/09/2026, 16:55), ET LES CINQ ATTENDUS SONT TENUS.**
>
> **Le banc** : un projet abîmé exprès, huit pistes, une anomalie par piste —
> preset déclaré et absent, piste MIDI sans machine, machine inconnue, dossier,
> deux pistes désactivées (l'une sans machine, l'autre à machine inconnue),
> insert de type inconnu, bus de groupe. Ouvert par l'application, rendu par
> `vsm-render`. **Les binaires d'avant ont été copiés AVANT la compilation** et
> ont servi de témoins : la colonne « avant » est une exécution, pas une lecture
> du diff.
>
> | | `vsm-render` avant | application avant | **les deux après** |
> |---|---|---|---|
> | lignes dites | 9 | 4 | **5, les mêmes au mot près** |
> | sans numéro de piste (« Instrument manquant : … ») | 2 | 2 | **0** |
> | numérotées à partir de 0 | **7** | 1 | **0** |
> | fausses (le dossier, les deux pistes désactivées, la machine demandée dite « aucun instrument ») | **4** | 0 | **0** |
> | la piste MIDI sans machine | dite | **tue** | dite |
> | la septième piste s'appelle | « Piste 6 » | « Piste 7 » | « Piste 7 » |
>
> **Le n° 1 est un diff, et il est vide** : les lignes `VSM_OUVERTURE` de
> l'application et les avertissements de `vsm-render`, triées, sont identiques.
> Triées, et c'est dit : l'application range sa boucle des pistes avant les
> lignes du chargement, le rendu après — l'ordre diffère, pas le contenu. Le
> « avant » de l'application est lu sur l'autoportrait (le binaire témoin
> n'avait pas `VSM_OUVERTURE`, c'est cette phase qui l'ajoute) ; le volet y
> montre les quatre lignes, dont « Preset introuvable pour la piste 0 » posée
> juste au-dessus de « Piste 7 ». L'autoportrait d'après montre les cinq lignes,
> toutes en ambre : « indisponible » est entré dans la liste des mots de perte,
> où il manquait.
>
> **Les projets sains** (n° 4) : la démo `demo-project`, **0** ligne des deux
> côtés, avant comme après. La vraie reconstruction `sky-v4` : **0** ligne de
> `vsm-render` avant et après, et **une** ligne de l'application avant et après
> — la même, sur les notes douteuses, que seule l'application peut dire
> puisqu'elle vient du `rapport.json` de la chaîne et non du projet.
>
> **CE QUE LA PHASE A CHANGÉ, EN UNE LISTE.** Quatre fonctions dans
> `ProjectBundle.h` — `libellePiste`, `pisteAttendUneMachine`,
> `pisteADireSansMachine` et les deux phrases — appelées par les deux lecteurs ;
> **14** phrases qui comptaient depuis 0 ; la règle « attend une machine »
> écrite en liste positive (MIDI et active) au lieu de « tout sauf l'audio et le
> groupe » ; le chargement qui garde l'index de piste de chaque machine absente
> (`ImportReport::missingInstrumentTracks`) ; une piste désactivée qui ne
> reçoit plus de machine dans le rendu, comme dans l'application ; et la fausse
> réserve « machine indisponible » d'une piste désactivée à preset, qui devient
> ce qui est vrai : « désactivée, preset non appliqué ».
>
> **CE QUE LA MESURE A MONTRÉ EN PASSANT, ET QUI OUVRE D76 (A10).** Lire le
> chargement pour comprendre les deux lignes sans numéro a fait voir deux
> choses qui ne sont PAS des affaires de phrases, et qui sont plus graves :
>
> 1. Le chargement **vide** l'identifiant d'une machine absente, alors que son
>    en-tête promet que « l'identifiant demandé est conservé : l'utilisateur
>    peut installer la machine et rouvrir ». Il l'est dans le document lu ; mais
>    l'application **réécrit le document à partir du projet** en enregistrant
>    (`documentFromProject`), et le projet ne l'a plus.
> 2. Une piste désactivée n'a pas de machine vivante, et l'enregistrement ne
>    capture un preset que depuis une machine vivante (`writeProjectTo`) ; là où
>    il n'en trouve pas, `saveProjectBundle` capture **l'état par défaut** d'une
>    machine neuve et l'écrit à la place.
>
> Si les deux lectures sont justes, **enregistrer un projet détruit ce qu'on
> n'a pas pu ouvrir** : la machine qu'on n'a pas installée, le réglage de la
> piste qu'on a mise de côté. Ce sont des soupçons lus dans le code, pas des
> mesures, et ils sont écrits ici comme tels. D76 les tranche.
>
> Tests : 327 core, 1 291 audio, **295** interchange (deux de plus), 25 clap,
> 11 panels, 172 Python, ruff et mypy — tout vert.

### Phase D76 — A10 : enregistrer détruit-il ce qu'on n'a pas pu ouvrir ? (10/09/2026, 17:00)

**LE SOUPÇON, TEL QUE D75 L'A LU DANS LE CODE.** Trois chemins, et une seule
forme de défaut : l'enregistrement recapture l'état depuis ce qui VIT dans la
session, et ce qui n'y vit pas est remplacé par un défaut, ou par rien.

| cas | ce que le code fait | ce qu'on perdrait |
|---|---|---|
| une piste demande une machine absente de ce build | le chargement vide `instrumentId` (`ProjectDocument.cpp:567`) ; l'enregistrement réécrit `preferredPlugin = instrumentId` (`:272`) ; `saveProjectBundle` efface le chemin du preset, faute de pouvoir créer la machine | l'identifiant demandé et la référence au preset — contre la promesse écrite dans `ProjectDocument.h` : « l'utilisateur peut installer la machine et rouvrir » |
| une piste désactivée à l'ouverture | aucune machine n'est instanciée (D30.2) ; `writeProjectTo` ne capture que les machines vivantes ; `saveProjectBundle` comble le trou avec l'état par défaut d'une machine NEUVE | le réglage de la piste, remplacé par le réglage d'usine |
| une piste désactivée puis réactivée dans la session | `setTrackInstrument("")` détruit l'instance ; la réactivation en crée une neuve, et rien ne lui repose son réglage | le réglage — alors que le compte rendu dit « ses notes et ses réglages restent », et le menu « sa machine revient » |

> **HYPOTHÈSE, ÉCRITE AVANT LA MESURE (10/09/2026, 17:00).** Les trois pertes
> sont réelles :
>
> 1. ouvrir puis enregistrer (Ctrl+S) un projet dont une piste demande une
>    machine absente fait disparaître `preferredPlugin` et `preset` de cette
>    piste dans `project.json` ;
> 2. ouvrir puis enregistrer un projet dont une piste désactivée porte un
>    preset réglé réécrit son `track_NN.synth.json` avec les valeurs d'usine ;
> 3. désactiver puis réactiver une piste, puis enregistrer, fait de même.
>
> Mesuré en comparant les fichiers ÉCRITS aux fichiers d'origine, paramètre
> par paramètre — pas en relisant le code une seconde fois. **Ce qui la
> réfuterait** : un fichier intact dans l'un des trois cas. Le cas sort alors
> de la phase, et c'est écrit.

> **MESURÉ (10/09/2026) : LES TROIS PERTES SONT RÉELLES, ET LE TÉMOIN
> PROUVE QUE LA MESURE N'EST PAS AVEUGLE.** Quatre copies de
> `docs/examples/demo-project` (Basse = TB-303 réglée, Drums = TR-909), ouvertes
> par l'application et enregistrées par `VSM_MENU=…;Enregistrer`, avec le
> binaire d'avant la phase gardé comme témoin. Chaque `project.json` a bien été
> réécrit (horodatage comparé) : aucune colonne ne mesure un fichier resté tel
> quel.
>
> | cas | la machine demandée par la piste 2 | le réglage de la Basse |
> |---|---|---|
> | **témoin** (rien d'anormal) | gardée | **0 écart sur 9** |
> | machine absente de ce build | **effacée** — ni `preferredPlugin` ni `preset` ; le fichier du preset reste sur le disque, orphelin | 0 / 9 |
> | Basse désactivée dans le fichier | gardée | **4 / 9 remis à l'usine** : coupure 480 → 800 Hz, résonance 0,9 → 0,6, enveloppe 0,85 → 0,5, accent 0,8 → 0,6 |
> | Basse désactivée puis réactivée dans la session | gardée | **les mêmes 4 / 9** — pendant que l'application écrit « ses notes et ses réglages restent », puis « sa machine et ses inserts sont revenus » |
>
> Le troisième cas est le plus grave des trois parce qu'il est le plus
> ordinaire : désactiver une piste pour soulager le processeur est exactement
> l'usage que D30.2 annonçait, et le geste coûtait le réglage de la machine
> sans un mot — en affirmant le contraire.

> **UN QUATRIÈME CAS, ÉCRIT AVANT SA MESURE (10/09/2026).** Chercher où
> corriger le troisième a fait lire `rebuildFromProject` en entier, et le
> défaut n'y est peut-être pas propre à la désactivation : la fonction appelle
> `setTrackInstrument` pour **chaque** piste, et `setTrackInstrument` crée une
> instance neuve même quand l'identifiant n'a pas changé — sans rien capturer
> avant, ni rien reposer après. Or tout geste qui touche la liste des pistes
> finit par elle (son propre commentaire le dit : « toute modification de la
> liste des pistes finit par appeler cette fonction »).
>
> **Hypothèse** : ajouter une piste, puis enregistrer, remet à l'usine le
> réglage de TOUTES les machines du projet, et pas seulement celui d'une piste
> désactivée. Mesuré avec le même banc : la Basse réglée, « Ajouter une piste »,
> « Enregistrer », et son preset comparé à l'original. **Ce qui la réfuterait** :
> 0 écart sur 9 ; le défaut serait alors propre à la désactivation, et la
> correction resterait à sa taille.
>
> **MESURÉ : CONFIRMÉ.** « Ajouter une piste MIDI », puis
> « Enregistrer » : la Basse perd **les mêmes 4 réglages sur 9** que dans les
> cas désactivés, sans qu'on l'ait touchée. Le défaut n'est donc pas celui de la
> désactivation : c'est celui de `rebuildFromProject`, et la désactivation n'en
> est qu'un chemin. Ajouter une piste, en supprimer une, annuler, déplacer —
> tout ce qui touche la liste des pistes remet **toutes** les machines à
> l'usine, et on l'ENTEND : l'instance neuve joue dès le bloc suivant avec les
> valeurs d'usine.
>
> **ET UN CINQUIÈME CHEMIN, LU EN CHERCHANT LE QUATRIÈME** : l'annulation
> rappelle `onProjectRestored`, qui appelle `rebuildFromProject(false)`
> (`MainComponent.cpp:393`). **Annuler n'importe quoi — une note déplacée,
> un volume — recrée toutes les machines.** Lu, pas encore mesuré : le banc
> neuf le mesurera (cas « annulation » ci-dessous).
>
> **UN SIXIÈME CAS, ÉCRIT AVANT SA MESURE (10/09/2026).** En vérifiant
> que la capture pouvait servir de remède (décision n° 3), une recherche de
> qui remplit `SynthPreset::samples` n'a trouvé que l'analyseur de fichier et
> le service de rendu de patch : **aucune capture depuis une machine vivante**.
> Or l'enregistrement ne connaît les presets que par capture (`writeProjectTo`).
> **Hypothèse** : ouvrir un projet dont un preset déclare des échantillons,
> puis l'enregistrer tel quel, efface la liste des échantillons de ce preset —
> et le projet rouvert rend un sampler muet. C'est aussi une contradiction
> possible avec D45 (« ouvrir puis réenregistrer une vraie reconstruction ne
> perd rien »), qui n'a peut-être pas eu de sampler à perdre. **Ce qui la
> réfuterait** : la liste intacte dans le preset réécrit.
>
> **MESURÉ : CONFIRMÉ, SUR LA VRAIE RECONSTRUCTION.** Une copie de
> `sky-v4` ouverte puis enregistrée telle quelle, sans un geste : le preset de
> son sampler (`track_03`, la voix) déclarait **1** échantillon
> (`samples/voix.wav`) ; réécrit, il en déclare **0**. Le projet rouvert rendrait
> une voix muette, et le premier Ctrl+S après une reconstruction suffit à le
> provoquer. L'original n'a pas été touché : la mesure porte sur une copie.
>
> **UN SEPTIÈME CAS, ÉCRIT AVANT SA MESURE (10/09/2026), ET C'EST LE
> PLUS LOURD POUR LA RECONSTRUCTION.** L'export du DAW ne relit pas le dossier :
> il fabrique ses presets par la MÊME capture (`bundleFromSession`), qui ne
> prend pas les échantillons. **Hypothèse** : exporter `sky-v4` depuis
> l'application rend la voix — la piste du sampler — **muette**, alors que
> `vsm-render`, qui relit le fichier, la rend. Ce serait une contradiction
> directe avec D46 (« l'export du DAW et `vsm-render` rendent le même son,
> mesuré sur un vrai morceau ») : ou D46 a mesuré un morceau sans sampler, ou
> l'hypothèse est fausse. Mesuré par l'export en stems (`VSM_EXPORT_STEMS`),
> qui dit la crête de chaque stem ou « silencieux » (D50). **Ce qui la
> réfuterait** : un stem de la voix non silencieux.
>
> **MESURÉ : CONFIRMÉ, AVEC SON TÉMOIN.** Sur la même copie de
> `sky-v4`, non modifiée :
>
> | stem | export du DAW (binaire d'avant) | `vsm-render --stems` (témoin) |
> |---|---|---|
> | 01 - bass | −7,94 dBFS | −7,93 dBFS |
> | 02 - other | −6,33 dBFS | −6,58 dBFS |
> | 03 - Batterie | +1,15 dBFS | +0,84 dBFS |
> | **04 - Voix** | **silencieux** | **−6,41 dBFS** |
>
> L'application FAIT ENTENDRE la voix — l'ouverture applique les échantillons
> du fichier à la machine vivante — et l'EXPORTE muette, parce que l'export
> recapture le preset sans eux. Ce qu'on écoute et ce qu'on livre divergent sur
> la piste la plus exposée d'une reconstruction. D46 n'a pas pu mesurer cela
> sur ce morceau : ou il en a mesuré un autre, ou il a comparé autre chose que
> les stems ; la phrase de D46 reste vraie de ce qu'elle a mesuré, et fausse
> de `sky-v4`. (Les écarts de crête des trois autres stems — quelques
> dixièmes de dB — viennent du dither et du format entier de l'export du DAW
> contre le flottant de `vsm-render` ; ils ne sont pas l'objet de cette phase.)

**LA DÉCISION, ET SES RAISONS.**

1. **Une machine dont la piste n'a pas bougé n'est pas recréée.**
   `rebuildFromProject` compare, emplacement par emplacement, la piste qui
   l'occupait et la machine qu'elle y avait avec celles qu'il faut
   maintenant ; identiques, il n'y touche pas. C'est de loin le cas le plus
   fréquent — toute annulation qui ne déplace pas de piste, tout ajout en fin
   de liste —, et c'est le seul qui ne peut rien perdre, puisque rien n'est
   détruit. Cubase et Live ne réinstancient pas un instrument parce qu'on a
   ajouté une piste à côté.
2. **Une piste reçoit une identité** (`Track::uid`), de session, jamais
   écrite dans le fichier. Sans elle, « la piste qui occupait l'emplacement 3 »
   ne se distingue pas de « celle qui l'occupe maintenant » après une
   suppression. Elle vient d'un compteur **de processus**, jamais réutilisé :
   l'annulation remet d'anciennes pistes en place, et un compteur propre au
   projet, restauré avec lui, redistribuerait des identités déjà prises.
3. **Quand une piste change d'emplacement, son réglage est capturé avant et
   reposé après — l'instance n'est pas déplacée.** La déplacer serait plus
   fidèle (voix en cours, état complet), mais le graphe n'offre aucun moyen de
   savoir que le fil audio a fini le bloc où il la tient encore par l'ancien
   emplacement : la même instance serait jouée par deux fils de rendu à la
   fois. La capture est ce que l'enregistrement fait déjà, et le témoin montre
   qu'elle est fidèle (0 écart sur 9) — **à une lacune près, que le sixième cas
   a mesurée** : elle ne prenait pas les échantillons, qui vivent à part des
   paramètres (`SynthPreset::samples`). Ils sont désormais **capturés à la
   source** : la machine sait ce qu'elle tient (`ISampleLoader::samplePath`),
   et `capturePreset` le lui demande, en rendant le chemin relatif au dossier
   du projet comme le format l'exige. *Première version de ce point, écrite avec
   les attendus : « repris du dernier preset connu de la piste ». Abandonnée avant
   d'être codée, parce qu'elle aurait réparé la capture pour ce seul chemin et
   laissé l'enregistrement, l'export et le gel écrire des samplers muets.*
4. **Un réglage qu'aucune machine vivante ne porte est GARDÉ, jamais
   recapturé depuis une machine neuve.** Piste désactivée : capturé au moment
   où la machine est libérée, reposé à la réactivation, écrit tel quel à
   l'enregistrement. Machine absente de ce build : le preset lu à l'ouverture
   est gardé et réécrit, et l'identifiant demandé survit dans la piste
   (`Track::requestedInstrumentId`), écrit comme `preferredPlugin` tant que la
   piste n'a pas reçu d'autre machine — la promesse de `ProjectDocument.h`,
   tenue jusqu'à l'enregistrement.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA CORRECTION (10/09/2026).**
>
> 1. **0 écart sur 9 dans tous les cas du banc**, y compris deux neufs : la
>    suppression d'une piste placée AVANT la Basse (chemin « recréée avec son
>    réglage ») et l'ajout suivi d'une annulation. La machine absente garde
>    `preferredPlugin` et `preset`.
> 2. **Le témoin reste à 0, et le banc de D75 redonne ses cinq lignes au mot
>    près** : cette phase ne doit rien changer à ce que disent les lecteurs.
> 3. **Le chemin pris se dit** : une ligne `VSM_MACHINES : N gardée(s),
>    M recréée(s) avec leur réglage, K neuve(s)` par reconstruction, sur la
>    sortie d'erreur. L'ajout en fin de liste doit dire **0 recréée** ; la
>    suppression d'une piste de tête doit en dire au moins une. Sans cette
>    ligne, on vérifierait un résultat en croyant vérifier un chemin.
> 4. **Aucun test existant ne change de verdict** ; des tests neufs tiennent
>    l'identité des pistes (`core/`) et l'aller-retour de la machine absente
>    (`interchange/`).
> 5. **Ce qui n'est pas couvert est dit, pas découvert** : une machine
>    RECRÉÉE (piste déplacée) perd ses voix en cours — une note tenue pendant
>    qu'on supprime une piste au-dessus s'arrête ; et l'état natif d'un plugin
>    tiers passe par `saveNativeState`, qu'aucun plugin tiers installé ici ne
>    permet de mesurer.

> **D76 EST FAITE (10/09/2026, 18:16). QUATRE ATTENDUS SUR CINQ SONT TENUS ;
> LE PREMIER L'EST POUR SIX CAS SUR SEPT, ET LE SEPTIÈME A TROUVÉ UN AUTRE
> DÉFAUT.**
>
> Le même banc, rejoué avec le binaire d'avant (témoin, copié avant la
> compilation) puis avec celui d'après ; chaque `project.json` vérifié réécrit,
> le preset de la Basse retrouvé PAR SON NOM — son numéro de fichier change
> quand une piste disparaît au-dessus d'elle.
>
> | cas | avant | après | chemin dit par `VSM_MACHINES` |
> |---|---|---|---|
> | témoin | 0 / 9 | 0 / 9 | — |
> | machine absente de ce build | demande et preset **effacés** | **gardés** | — |
> | Basse désactivée dans le fichier | 4 / 9 perdus | **0 / 9** | — |
> | désactiver puis réactiver | 4 / 9 perdus | **0 / 9** | « 1 gardée, 1 recréée avec leur réglage » à la réactivation |
> | ajouter une piste MIDI | 4 / 9 perdus | **0 / 9** | « 2 gardée(s), 0 recréée(s), 0 neuve(s) » |
> | supprimer une piste placée avant la Basse | 4 / 9 perdus | **0 / 9** | « 0 gardée, 2 recréée(s) avec leur réglage » |
> | sampler de `sky-v4`, ouvert puis enregistré | 1 échantillon → **0** | **1 → 1**, identique | — |
> | stem « Voix » de `sky-v4` exporté par le DAW | **silencieux** | **−6,44 dBFS** (`vsm-render` : −6,41) | — |
>
> **Le septième cas du premier attendu — ajouter puis annuler — n'a PAS été
> mesuré, et il faut le dire parce que le tableau le laisserait croire.** Le
> projet écrit compte **3 pistes**, avant comme après : Ctrl+Z n'a rien annulé,
> et le cas a mesuré un second ajout. `VSM_TOUCHE` a répondu « touche inconnue
> ou sans commande ». Ce n'est pas le banc : **l'application ne sait annuler
> que depuis le piano roll** — `MainComponent::keyPressed` ne connaît pas
> `EditUndo`, et le menu Édition n'a ni « Annuler » ni « Rétablir ». C'est
> A11. Le cinquième chemin de perte (« annuler recrée toutes les machines »)
> reste donc une LECTURE du code, que la correction couvre par construction
> (`onProjectRestored` passe par la même reconstruction) sans que rien ne l'ait
> joué.
>
> **Les attendus 2 à 5.** Le témoin reste à 0, et le banc de D75 redonne ses
> lignes au mot près sur les trois projets (5 et 5, 0 et 0, 0 et 1). La
> ligne `VSM_MACHINES` distingue les chemins comme demandé : **0 recréée** à
> l'ajout, **2 recréées** à la suppression. Aucun test existant ne change de
> verdict ; cinq tests neufs tiennent l'identité des pistes (`core/`, trois),
> la capture des échantillons et l'aller-retour de la machine absente
> (`interchange/`, deux). Et ce qui n'est pas couvert est écrit : les voix en
> cours d'une machine recréée, l'état natif d'un plugin tiers — et un preset de
> PISTE (bibliothèque) d'un sampler, qui capture désormais ses échantillons en
> chemin absolu, faute de dossier de projet auquel les rapporter : la relecture
> les refusera EN LE DISANT, là où ils disparaissaient sans un mot.
>
> **CE QUE CETTE PHASE CORRIGE DE D46.** « L'export du DAW et `vsm-render`
> rendent le même son, mesuré sur un vrai morceau » restait vrai du morceau
> mesuré ; sur `sky-v4`, l'export du DAW perdait la voix. La phrase de D46 n'est
> pas réécrite — un relevé daté dit ce qu'il a mesuré — ; c'est ici que la
> réserve est écrite.
>
> **CE QUE L'HORLOGE A CORRIGÉ.** Plusieurs paragraphes de cette phase avaient
> été datés à l'estime, et certains portaient des heures qui n'étaient pas
> encore arrivées quand ils ont été écrits. Ils portent désormais la date seule :
> l'ordre hypothèse-puis-mesure est celui du texte, et c'est lui qui compte ;
> une minute inventée ne l'aurait pas prouvé mieux.
>
> Tests : **330** core (+3), 1 291 audio, **297** interchange (+2), 25 clap,
> 11 panels, 172 Python, ruff et mypy — tout vert.

### Phase D77 — A9, suite : le panneau des effets traduit, et changer de langue ne l'atteignait pas (10/09/2026, 18:25)

**CE QUI ÉTAIT EN COURS, ET CE QUE LA RELECTURE Y A TROUVÉ.** La session
précédente a laissé un travail sans commit : sept paires de traduction (le
titre et deux libellés du panneau des effets, « Prêt » dans la barre d'état du
piano roll, « Vél. » sur la ligne de vélocité, les boutons « Ouvrir MIDI… » et
« Exporter MIDI… » repris par la re-traduction du transport). Relu avant d'être
repris, et non commité tel quel : il traduisait le panneau des effets **à sa
construction et nulle part ailleurs**. Un changement de langue en cours de
séance — la règle n° 3 de D73, « immédiat, pas au prochain démarrage » — ne
l'atteignait donc pas. Et le panneau gardait **dix** autres chaînes affichées en
français, dont une, « - parametres », privée de son accent pour échapper au
piège Latin-1 que `tr()` résout.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 18:25).**
>
> 1. **Le panneau des effets est traduit entièrement**, sauf les noms d'effets
>    et de paramètres : ils viennent des effets eux-mêmes, comme les
>    sérigraphies viennent des machines. Couverture publiée : combien de
>    chaînes affichées, combien par `tr()`, combien de paires dans la table.
> 2. **Changer de langue l'atteint SANS redémarrer** : lancée en français,
>    basculée par le menu (« English »), l'application est photographiée, et
>    le panneau doit être en anglais sur la même image que la barre de menus.
> 3. **Le français ne bouge pas**, sauf l'accent rendu à « paramètres », qui
>    est le défaut corrigé — mesuré par capture, avant et après.
> 4. **« Prêt » suit aussi la bascule** tant qu'aucun message ne l'a remplacé.

> **D77 EST FAITE (10/09/2026, 18:36), ET LES QUATRE ATTENDUS SONT TENUS.**
> Mesuré par capture sur une copie de la démo portant une réverbération
> (onglet Effets, `VSM_VUE=effets`), sous un `HOME` isolé — voir plus bas
> pourquoi.
>
> | attendu | mesuré |
> |---|---|
> | 1. panneau des effets traduit | **31** chaînes par `tr()`, **0** chaîne affichée hors de `tr()` ; table à **306** paires (296 avec le travail repris, dix de plus) |
> | 2. la bascule l'atteint sans redémarrer | lancée en français, basculée par « English » : « Effects — Acid Bass », « Add: », « choose an effect », « Bypass all » sur la même image que la barre de menus anglaise |
> | 3. le français ne bouge pas | **0** pixel sur 937 888 entre le binaire d'avant et celui d'après ; le contre-exemple français/anglais en donne **17 433** — le zéro n'est pas aveugle |
> | 4. « Prêt » suit la bascule | « Ready » dans la barre d'état du piano roll après la bascule |
>
> L'accent rendu à « paramètres » n'est pas sur ces images : l'en-tête des
> paramètres ne s'affiche que quand un effet est choisi, ce qu'aucune commande
> sans souris ne fait. Les chaînes françaises passées à `tr()` ont été
> vérifiées une par une contre la version commitée : **29 sur 29** existaient
> telles quelles, et « — paramètres » est celle de l'en-tête des effets MIDI,
> deux lignes plus haut.
>
> **CE QUE LA MESURE A TROUVÉ, ET QUE D74 NE POUVAIT PAS VOIR.** D74 mesurait
> la barre du piano roll traduite à 27/27 **au démarrage**. Une capture lancée
> directement en anglais et une capture basculée en direct diffèrent de
> **4 056 pixels**, en deux endroits exactement : le bouton d'écoute A/B
> (« A/B monitoring: no original » au démarrage, « Écoute A/B : pas
> d'original » après la bascule) et le sélecteur de subdivision (« Straight »
> contre « Droit »). La règle n° 3 de D73 — la bascule est immédiate — y est
> donc violée. Et présents sur les deux images, donc traduits nulle part :
> « Gamme », les noms de gammes (« Chromatique ») et « mes. » dans la position.
> Nommés et comptés ici, pour la phase suivante.
>
> **UN PIÈGE PAYÉ PENDANT LA MESURE, ÉCRIT DANS `CLAUDE.md`.** Un premier
> essai de photographier les panneaux en mode flottant (`VSM_VUE=flottant`) a
> basculé la disposition **de l'utilisateur**, et elle s'est conservée : les
> captures suivantes montraient une fenêtre vide sous la barre de transport,
> et deux images vides ont donné « 0 pixel de différence » — un zéro qui ne
> mesurait rien, attrapé parce que l'image a été regardée. Les bancs de
> D75 et D76 avaient aussi inscrit leurs dossiers de brouillon dans les
> projets récents de l'utilisateur. Le fichier de préférences a été rétabli
> (fenêtre unique, projets récents réduits aux deux chemins réels), et les
> bancs tournent désormais sous `HOME=<brouillon>` : `cmp` a vérifié, après
> chaque série, que le fichier de l'utilisateur n'avait pas bougé d'un octet.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D78 — La bascule de langue rattrapée : ce que D77 a mesuré et que D74 ne voyait pas (10/09/2026, 18:45)

**CE QUE D77 A LAISSÉ, COMPTÉ.** Deux libellés que la bascule en direct
oublie alors que le démarrage les traduit — le bouton d'écoute A/B et le
sélecteur de subdivision (4 056 pixels entre « lancé en anglais » et « basculé
en anglais ») —, et des mots traduits nulle part : l'étiquette « Gamme », les
treize noms de gammes, « mes. » dans la position. Lu à côté : « Decompte », le
compte à rebours du transport, écrit sans son accent.

**TROIS DÉCISIONS, ÉCRITES AVANT D'ÊTRE CODÉES.**

1. **Les noms de gammes restent en français dans `core/`.** `scaleTypeName` n'a
   qu'un appelant, la barre du piano roll : c'est à l'affichage qu'on traduit,
   comme D74 l'a décidé pour les noms de notes — un nom fabriqué dans `core/`
   peut finir dans un fichier, et il ne doit pas dépendre de la langue de
   l'interface. Au passage, l'affichage passe par `fromUTF8` : l'appel
   actuel convertit un `const char*` en `juce::String`, donc lit « Mineure
   mélodique » en Latin-1. *Lu dans le code, pas photographié : la liste
   déroulante ne s'ouvre pas sans souris.*
2. **La bascule repose ce que le démarrage pose, par le même chemin.** Le
   bouton d'écoute est reposé par la fonction qui le pose au démarrage — pas
   par une copie de ses quatre textes, qui divergerait à la première retouche ;
   les entrées des sélecteurs le sont par `changeItemText`, qui ne touche pas à
   la sélection de l'utilisateur.
3. **« Décompte » reprend son accent.** C'est la seule modification du
   français que la phase s'autorise, et c'est la correction d'un défaut.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 18:45).**
>
> 1. **« Lancée en anglais » et « basculée en anglais » donnent la même image
>    dans toutes les zones de texte** : D77 y a trouvé 4 056 pixels, attendu
>    **0**. Ce qui reste est situé et nommé — un état de survol, un compteur
>    qui bat — plutôt que noyé dans un total.
> 2. **« Gamme », les noms de gammes et « mes. » sont en anglais** sur les
>    deux images.
> 3. **Le français ne bouge pas** : 0 pixel entre le binaire d'avant et celui
>    d'après, avec l'image anglaise pour contre-exemple.
> 4. **La couverture est publiée** : paires ajoutées, taille de la table.

> **D78 EST FAITE (10/09/2026, 18:51), ET LES QUATRE ATTENDUS SONT TENUS —
> AU SECOND ESSAI, ET LE PREMIER A APPRIS QUELQUE CHOSE SUR JUCE.** Même
> démo, même onglet, `HOME` isolé ; le fichier de préférences de l'utilisateur
> vérifié par `cmp` après chaque série : intact.
>
> | | D77 | première version | D78 |
> |---|---|---|---|
> | « lancée en anglais » / « basculée en anglais » | 4 056 px | 527 px | **0 px** sur 937 888 |
> | français, binaire d'avant / d'après | — | 0 px | **0 px** |
> | français / anglais (contre-exemple) | 17 433 px | 16 509 px | 17 036 px |
>
> Sur l'image basculée : « bar 1 · 1 », « Scale », « Straight », « Chromatic »,
> « A/B monitoring: no original », « Ready ». Table à **321** paires (quinze de
> plus : « mes. », « Décompte » et les treize noms de gammes).
>
> **CE QUE LA PREMIÈRE VERSION A RATÉ, ET POURQUOI.** Elle renommait l'entrée
> choisie (`changeItemText`) puis demandait si elle était choisie, pour
> reposer la sélection et rafraîchir le texte affiché. Or
> `ComboBox::getSelectedId` ne rend l'identifiant **que si le texte affiché est
> encore celui de l'entrée** (`juce_ComboBox.cpp:262`) : une fois l'entrée
> renommée, il rendait 0, la condition était fausse, et l'image gardait
> « Droit » et « Chromatique ». Les 527 pixels l'ont montré, les deux zones
> agrandies côte à côte l'ont expliqué, et la source de JUCE l'a confirmé avant
> que la correction — lire la sélection AVANT de renommer — soit écrite. La
> leçon, pour tout sélecteur qu'on re-traduira : **renommer une entrée choisie
> la « dé-choisit » aux yeux de JUCE jusqu'à ce qu'on la repose.**
>
> **CE QUI RESTE, VU SUR LA MÊME IMAGE.** « RÉGLAGES », en tête d'une section
> de la façade de la TB-303, à côté de « SYNTHESIZER » en anglais sur la même
> façade. Les sérigraphies ne se traduisent pas (D73), mais une façade qui
> mêle les deux langues n'imite aucune machine : c'est un titre ajouté par
> l'application, pas une sérigraphie, et il relève d'A9. Nommé ici, non fait.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D79 — A11 : Ctrl+Z n'agissait que dans le piano roll — et le menu Annuler existait, contrairement à ce que D76 a écrit (10/09/2026, 18:55)

> **RECTIFICATIF DE D76, ET DE LA LIGNE A11 DE L'INDEX (10/09/2026, 19:00).**
> D76 a écrit : « le menu Édition n'a ni « Annuler » ni « Rétablir » ». **C'est
> faux.** Le menu Édition EST le menu contextuel du piano roll
> (`MainComponent.cpp` : `menu = pianoRoll_.buildContextMenu();`), et ce
> menu commence par « Annuler : … » et « Rétablir : … » (`kCtxUndo`,
> `kCtxRedo`), transmis par `performContextMenuAction`. La recherche qui a
> fondé la phrase ne portait que sur `MainComponent.cpp` ; l'entrée vit dans
> `PianoRollComponent.cpp`. C'est la faute que `CLAUDE.md` décrit pour les
> machines (« un zéro sorti d'un grep se revérifie en listant ce qu'on a
> cherché ET où »), commise une fois de plus — et trouvée en ouvrant la
> fonction qui construit le menu avant d'y ajouter les entrées qu'on croyait
> manquantes. La moitié vraie de A11 tient : **Ctrl+Z n'agit que si le piano
> roll a le focus**, parce que `MainComponent::keyPressed` ne connaît pas
> `EditUndo` (son `default` rend `false`) — c'est ce que le banc de D76 a
> buté, et c'est ce que cette phase corrige.

**CE QUI MANQUE VRAIMENT.** Le raccourci. Dans la liste des pistes,
l'arrangement ou le mélangeur, Ctrl+Z ne fait rien, sans un mot ; Cubase et
Live annulent quelle que soit la zone qui a le focus. **Et le menu qui
existait permet enfin de jouer le cinquième chemin de perte de D76**
(« annuler recrée toutes les machines »), resté une lecture du code faute de
savoir annuler depuis le banc.

**LA DÉCISION.** **Un seul chemin d'annulation** : la touche appelle
`pianoRoll_.undo()` et `redo()`, que le menu, le bouton de la barre et la
fenêtre d'historique appellent déjà. Une annulation à deux chemins finirait
par ne pas annuler la même chose.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 19:00).**
>
> 1. **Le cinquième chemin de D76, joué par le menu** : ajouter une piste,
>    « Annuler », enregistrer. Avec le binaire d'AVANT D76, la Basse perd ses
>    4 réglages sur 9 (hypothèse : l'annulation reconstruit tout, comme
>    l'ajout) ; avec le binaire d'après, **0 / 9**, et la ligne
>    `VSM_MACHINES` de l'annulation dit « gardée(s) ». Le projet écrit doit
>    compter 2 pistes : sinon l'annulation n'a pas eu lieu et le cas ne mesure
>    rien (la leçon du cas raté de D76).
> 2. **Ctrl+Z et Ctrl+Maj+Z (ou Ctrl+Y) agissent hors du piano roll** : le
>    cas « annulation » par `VSM_TOUCHE`, qui passe par
>    `MainComponent::keyPressed`, doit écrire 2 pistes, et non 3.
> 3. **Ce que la relecture du menu a montré est compté** : le menu Édition
>    étant celui du piano roll, ses entrées écrites sans `tr()` restent en
>    français dans l'interface anglaise — contrairement au « 227 / 227 » de
>    D73, qui ne les comptait pas. Le compte est publié ; la traduction relève
>    d'A9.

> **D79 EST FAITE (10/09/2026, 18:58), ET LES TROIS ATTENDUS SONT TENUS.** Le
> banc de D76, rejoué sous un `HOME` isolé ; le fichier de préférences de
> l'utilisateur vérifié intact par `cmp`.
>
> **1. Le cinquième chemin de perte de D76, enfin joué** (ajouter une piste,
> « Annuler » par le menu, enregistrer) :
>
> | binaire | pistes écrites | réglages de la Basse | reconstruction de l'annulation |
> |---|---|---|---|
> | d'avant D76 | 2 — l'annulation a eu lieu | **4 / 9 perdus** | — |
> | d'après D76 | 2 | **0 / 9** | « 2 gardée(s), 0 recréée(s) avec leur réglage » |
>
> L'hypothèse lue par D76 est confirmée : **annuler, avant D76, remettait
> toutes les machines à l'usine** — le geste censé tout rendre tel quel. Les
> quatre chiffres de A10 deviennent donc sept cas mesurés sur sept.
>
> **2. Le raccourci, hors du piano roll** (`VSM_TOUCHE`, qui passe par
> `MainComponent::keyPressed`) :
>
> | cas | avant | après |
> |---|---|---|
> | ajouter, Ctrl+Z, Ctrl+S | 3 pistes, « touche inconnue ou sans commande » | **2 pistes**, 0 / 9, aucun message |
> | ajouter, Ctrl+Z, Ctrl+Maj+Z, Ctrl+S | — | **3 pistes** (refait), 0 / 9 |
>
> **3. Le menu Édition, compté** : c'est le menu contextuel du piano roll,
> **64 entrées, 0 par `tr()`**. En anglais, le menu Édition est entièrement en
> français — et le « barre de menus traduite entièrement, 227 / 227 » de D73
> ne valait donc que pour les six autres menus. Écrit dans la ligne A9 de
> l'INDEX, pour la phase qui le traduira.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D80 — A9 : le menu Édition, et une barre de menus comptée sur ce qu'elle affiche (10/09/2026, 19:05)

**CE QUE D79 A COMPTÉ.** Le menu Édition est le menu contextuel du piano roll :
**64 entrées, aucune par `tr()`**. En anglais, il est entièrement en français.
Le « 227 / 227 » de D73 était un compte STATIQUE — les chaînes qui passent par
`tr()` dans `MainComponent.cpp` — et il ne voyait pas un menu construit
ailleurs. Des libellés y sont en plus FABRIQUÉS : « Annuler : » suivi du nom
du geste, des comptes entre parenthèses, et les noms d'accords, qui sortent
de `core/` et passent par `juce::String(const char*)` — donc en Latin-1.

**LA DÉCISION SUR LA MESURE, AVANT LA TRADUCTION.** Un menu déroulant ne se
photographie pas, et un compte statique a déjà menti une fois. L'application
**dira donc son menu elle-même** : `VSM_MENU_LISTE=1` écrit chaque entrée de
la barre, sous-menus compris, sur la sortie d'erreur, telle qu'elle
s'affiche. Une entrée IDENTIQUE en français et en anglais est soit non
traduite, soit neutre (« Legato », « Crescendo », « Octave + ») : la liste de
ces entrées se publie et se lit, plutôt qu'un pourcentage. Les noms d'accords
restent français dans `core/` et se traduisent à l'affichage, comme les
gammes en D78.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 19:05).**
>
> 1. **La barre entière est comptée sur ce qu'elle affiche**, en français et
>    en anglais, et le chiffre de D73 est confronté à ce compte-là.
> 2. **Le menu Édition est traduit** : après la phase, les seules entrées
>    identiques dans les deux langues sont neutres, et elles sont nommées.
> 3. **Le français ne change pas** : la liste française d'après est celle
>    qu'affichait le binaire d'avant, entrée pour entrée.
> 4. **Aucun libellé anglais neuf n'en double un autre** dans la barre
>    (piège `VSM_MENU` du 06/09), vérifié sur la liste.

> **LE RELEVÉ D'AVANT (10/09/2026, 19:10), ET CE QU'IL FAIT DE D73.** Le binaire
> d'avant la traduction — le même code, commande de liste comprise —, la démo
> à réverbération ouverte, `HOME` isolé. La barre affiche **315 entrées**, et
> **150 sont identiques en français et en anglais** :
>
> | menu | entrées | identiques FR / EN |
> |---|---|---|
> | Fichier | 65 | 16 |
> | Édition | 108 | **89** |
> | Piste | 74 | 14 |
> | Enregistrement | 26 | 3 |
> | Mixage | 9 | 5 |
> | Affichage | 32 | 23 |
> | Aide | 1 | 0 |
>
> **RECTIFICATIF DE D73.** « La barre de menus est traduite ENTIÈREMENT —
> 227 / 227 chaînes, 0 sans traduction » était un compte fait dans le code :
> les chaînes de `MainComponent.cpp` passées à `tr()` et présentes dans la
> table. Il ne voyait pas le menu Édition, construit par le piano roll, et il
> ne voyait pas que les libellés FABRIQUÉS — « Copier la chaîne d'inserts
> (1) », « Réduire les points d'automation (0 points) », « Ordre de jeu…
> (aucune section) » — n'ont pas la clé de la table : le texte affiché n'est
> pas la chaîne qu'on a traduite. Une partie des 150 est neutre par nature
> (les chemins des projets récents, les ports MIDI, « 150 % », « Français » et
> « English », « Legato », « Crescendo ») ; le reste est du français que D73
> déclarait traduit. **La leçon est celle de D72, un cran plus loin : on
> mesure ce que l'application AFFICHE, pas ce que le code contient.**
>
> **ET UN DÉFAUT DU FRANÇAIS, VU CETTE FOIS ET NON PLUS LU** : « DiminuÃ© sur
> C4 » et « AugmentÃ© sur C4 » dans le sous-menu des accords — le nom sort de
> `core/` en UTF-8 et `juce::String(const char*)` le lisait en Latin-1. Le
> même piège que D78 soupçonnait pour les gammes sans pouvoir l'ouvrir.

> **D80 EST FAITE (10/09/2026, 19:18), ET LES QUATRE ATTENDUS SONT TENUS.**
> Les listes d'après, prises par le binaire d'après dans les mêmes conditions
> (démo à réverbération, `HOME` isolé, préférences de l'utilisateur vérifiées
> intactes par `cmp`).
>
> | menu | entrées | identiques FR / EN avant | après |
> |---|---|---|---|
> | Fichier | 65 | 16 | 13 |
> | **Édition** | 108 | **89** | **11** |
> | Piste | 74 | 14 | 14 |
> | Enregistrement | 26 | 3 | 3 |
> | Mixage | 9 | 5 | 5 |
> | Affichage | 32 | 23 | 23 |
> | Aide | 1 | 0 | 0 |
> | **barre entière** | 315 | **150** | **69** |
>
> **Les onze du menu Édition sont neutres**, et les voici : « Octave + »,
> « Octave - », « Legato », « Crescendo », « Decrescendo », « 2/4 », « 3/4 »,
> « 4/4 », « 5/4 », « 6/8 », « 7/8 ». Le modèle du groove, qu'aucune liste ne
> montrait sans groove en mémoire, a été affiché en extrayant d'abord celui de
> la Basse : « Appliquer le groove « Acid Bass » » en français,
> « Apply the groove “Acid Bass” » en anglais.
>
> **Le français ne change pas** : alignées par leur contenu (`difflib`), les
> listes d'avant et d'après ont **308 entrées égales** et trois blocs
> différents. Deux tiennent à l'ENVIRONNEMENT et non au code : le binaire
> témoin, lancé depuis le brouillon, ne trouve pas `analyse/` « à côté de
> l'application » et le dit dans le menu Fichier et dans l'entrée de
> transcription. Le troisième est la correction voulue : « Diminué » et
> « Augmenté » au lieu de « DiminuÃ© » et « AugmentÃ© ». *Une première lecture,
> ligne par ligne, avait donné « 278 entrées différentes » : le décalage de
> trois lignes du menu Fichier faisait tout glisser. Le chiffre n'a pas été
> écrit tel quel ; il a été expliqué, puis refait par contenu.*
>
> **Aucun libellé anglais neuf n'en double un autre** : les deux doublons de
> la liste anglaise (« None », « Quantise ») recouvrent exactement deux
> doublons que le français avait déjà (« Aucun », « Quantifier (100 %) /
> (50 %) »).
>
> **CE QUE LA PHASE A TROUVÉ EN PLUS, ET QUI EXPLIQUE UNE PARTIE DU CHIFFRE DE
> D73.** Sept paires de la table existaient DÉJÀ pour des entrées du menu
> Édition — les quatre formes d'automation, « Triangle », « Carré »,
> « Appliquer le groove (aucun en mémoire) » — mais le code les écrivait par
> `juce::String::fromUTF8`, sans passer par `tr()`. Le compte statique les
> voyait « traduites » ; l'écran ne l'était pas. La table compte désormais
> **391** paires.
>
> **CE QUI RESTE, COMPTÉ SUR L'ÉCRAN** : 69 entrées identiques dans les deux
> langues, dont une part neutre (chemins, ports, pourcentages, « Français » et
> « English », noms de vues) ; le reste — des libellés fabriqués avec un compte
> entre parenthèses surtout, dans Piste, Affichage, Mixage et Fichier — est
> la suite d'A9, avec désormais une commande qui le mesure.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D81 — A9 : les six autres menus, jusqu'aux seules entrées neutres (10/09/2026, 19:25)

**CE QUE D80 A LAISSÉ, TRIÉ.** Des 69 entrées encore identiques en français et
en anglais, **34 sont neutres** et le resteront : dix chemins de projets
récents, deux ports MIDI, les onze du menu Édition (« Octave + », « Legato »,
les signatures), les quatre noms de vues que l'interface française emploie
déjà en anglais (« Piano Roll », « Synth Rack », « Mixer », « Arrangement »),
les cinq tailles « 100 % » à « 200 % », et « Français » / « English ». **Les 35
autres sont du français** : 3 dans Fichier, 12 dans Piste, 3 dans
Enregistrement, 5 dans Mixage, 12 dans Affichage — surtout des libellés
FABRIQUÉS avec un compte entre parenthèses (« Copier la chaîne d'inserts
(1) »), dont le texte affiché n'est la clé d'aucune paire.

**LA RÈGLE POUR LES LIBELLÉS FABRIQUÉS, TRANCHÉE ICI.** Un libellé qui porte un
nombre ou un nom se traduit comme UN modèle entier (`… (%1)`), jamais par
morceaux : c'est la décision prise pour le groove en D80, et c'est la seule qui
laisse l'anglais écrire l'ordre des mots qu'il veut.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 19:25).**
>
> 1. **Les entrées identiques dans les deux langues se réduisent aux 35
>    neutres**, et la liste d'après est publiée : ni plus, ni une de moins
>    sans explication. *Corrigé avant la mesure : le tri ci-dessus comptait 34
>    neutres ; « Active » (la région de punch) passe déjà par `tr()` et reste
>    « Active » en anglais parce que c'est le même mot — elle est neutre, et
>    les entrées françaises sont 34.*
> 2. **Le français ne change pas** : les listes d'avant et d'après, alignées
>    par contenu, sont égales — le témoin est lancé depuis le dossier du
>    build, pour que l'environnement ne fabrique plus de différence (leçon de
>    D80).
> 3. **Aucun doublon anglais neuf** dans la barre.

> **D81 EST FAITE (10/09/2026, 19:32), ET LES TROIS ATTENDUS SONT TENUS AU
> CHIFFRE PRÈS.** Les listes d'avant sont celles d'après D80 (le même binaire,
> lancé du dossier du build) ; celles d'après, prises par le binaire de D81 au
> même endroit, `HOME` isolé, préférences de l'utilisateur intactes.
>
> | | avant (D80) | après |
> |---|---|---|
> | entrées identiques en français et en anglais | 69 | **35** |
> | dont neutres | 35 | **35** |
> | dont françaises | 34 | **0** |
> | français, aligné par contenu | — | **312 / 312** entrées égales, 0 bloc différent |
> | doublons anglais neufs | — | **0** |
>
> **La barre de menus est traduite entièrement, et cette fois c'est mesuré sur
> ce qu'elle affiche** — ce que D73 affirmait et que D80 a réfuté.
>
> **CE QUI EXPLIQUE LE CHIFFRE DE D73, ENTIÈREMENT.** Des 40 traductions dont
> ces 34 entrées avaient besoin, **26 existaient déjà dans la table** : D73 les
> avait écrites, et le code affichait ces entrées par
> `juce::String::fromUTF8(u8"…")`, sans jamais appeler `tr()`. Le compte
> statique voyait une paire pour chaque chaîne et concluait « traduite » ; il
> ne regardait pas si l'affichage passait par la table. Les 14 autres étaient
> des libellés FABRIQUÉS : ils deviennent douze modèles entiers
> (« Copier la chaîne d'inserts (%1) »), la règle tranchée en tête de phase.
> Il ne reste **aucun** `fromUTF8(u8"…")` dans `getMenuForIndex`, et la table
> compte **405** paires.
>
> **CE QUI RESTE D'A9, NOMMÉ.** Les fenêtres flottantes, les menus contextuels
> de l'arrangement, les phrases fabriquées dans `interchange/`, les
> infobulles longues de D74, « RÉGLAGES » sur la façade de la TB-303, et le
> nom du geste dans « Undo: … », qui vient de l'historique et reste en
> français. La commande `VSM_MENU_LISTE` ne couvre que la barre ; les
> surfaces restantes demanderont leur propre mesure.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D82 — A9 : les noms des gestes, dans « Undo: … » et dans l'historique (10/09/2026, 19:45)

**CE QUE L'INTERFACE ANGLAISE MONTRE ENCORE EN FRANÇAIS, À CHAQUE GESTE.** La
première entrée du menu Édition dit ce qu'Annuler défera : « Undo: Ajouter une
piste ». La fenêtre d'historique liste les mêmes noms. Ils viennent de
`beginProjectEdit`, qui les reçoit en français, et l'inventaire des appels les
compte : **143 libellés littéraux, dont 12 ont une paire** ; **un seul est
fabriqué** (« Signature 3/4 ») ; et deux sont écrits sans leur accent
(« Deplacer un effet », « Reglage d'effet »). La fenêtre d'historique, elle,
affiche les libellés sans passer par `tr()`, et son texte d'explication est
posé une fois, à sa construction.

**TROIS DÉCISIONS.**

1. **Le libellé reste français dans l'historique, et se traduit à
   l'AFFICHAGE.** L'historique garde ce qu'on lui a donné ; le traduire au
   moment du geste figerait sa langue, et une bascule en cours de séance
   laisserait les pas déjà faits dans l'ancienne — contre la règle n° 3 de
   D73.
2. **Le libellé fabriqué se reconnaît par son modèle** (« Signature %1 ») à
   l'affichage : c'est le seul, et il ne justifie pas de changer ce que
   l'historique stocke.
3. **Les deux accents manquants sont rendus** — seule modification du
   français, et c'est la correction d'un défaut.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 19:45).**
>
> 1. **Les 143 libellés ont une paire**, et c'est vérifié à l'écran, pas
>    seulement dans la table (leçon de D80) : après une suite de gestes, le
>    menu Édition anglais dit « Undo: … » en anglais (`VSM_MENU_LISTE`), et la
>    fenêtre d'historique photographiée en anglais aussi.
> 2. **La bascule l'atteint** : lancée en français, basculée par « English »,
>    la fenêtre d'historique et son explication sont en anglais.
> 3. **Le français ne change pas**, sauf les deux accents — liste du menu et
>    capture de la fenêtre, avant et après.

> **D82 EST FAITE (10/09/2026, 19:59), ET LES TROIS ATTENDUS SONT TENUS.**
> Même suite de gestes avec le binaire d'avant et celui d'après — « Ajouter une
> piste MIDI », « Désactiver la piste », « Réactiver la piste », puis la bascule
> « English » —, la fenêtre d'historique ouverte (`VSM_VUE=historique`) et
> photographiée (`VSM_CAPTURE_PANNEAUX`), `HOME` isolé, préférences de
> l'utilisateur intactes.
>
> | | avant | après |
> |---|---|---|
> | libellés littéraux d'historique ayant une paire | 12 / 143 | **143 / 143** |
> | première entrée du menu Édition, basculé en anglais | « Undo: Réactiver une piste » | **« Undo: Re-enable a track »** |
> | fenêtre d'historique, basculée en anglais | tout en français — les pas, « état courant », l'explication | **tout en anglais** |
> | fenêtre d'historique en français, avant / après | — | **0 pixel** (contre-exemple anglais : 7 055) |
> | menu en français, aligné par contenu | — | **310 entrées égales** |
>
> Les deux blocs qui diffèrent dans le menu français sont ceux de D80, et pour
> la même raison : le binaire témoin a été lancé depuis le brouillon, où il ne
> trouve pas `analyse/`. *D81 avait appris à le lancer du dossier du build ;
> cette phase l'a oublié. L'écart est d'environnement, il est identifié, et
> il n'a pas été refait parce qu'il ne touche aucune entrée de cette phase —
> mais c'est écrit, pour que le prochain banc ne l'oublie pas une troisième
> fois.*
>
> Les deux accents rendus (« Déplacer un effet », « Réglage d'effet ») ne sont
> sur aucune image : la suite de gestes ne déplace ni ne règle d'effet. Ils
> sont vérifiés par la table — les 143 libellés, accents corrigés compris, ont
> leur paire. La table compte **538** paires (133 de plus).
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D83 — A9 : les menus de l'arrangement, sortis de `mouseDown` pour être mesurés (10/09/2026, 20:10)

**CE QUI RESTE, ET POURQUOI IL ÉCHAPPAIT À LA MESURE.** Les menus du clic
droit dans l'arrangement — celui de la règle (repères) et celui du clip, avec
ses sous-menus (répéter, suivi de tempo, formes des fondus, gain, hauteur) —
comptent **40 appels `addItem`/`addSubMenu`, aucun par `tr()`**. Ils sont
construits EN LIGNE dans `mouseDown`, au moment du clic : ni une capture ni
`VSM_MENU_LISTE` ne les atteignent. S'y ajoutent trois listes déroulantes
toujours à l'écran : « (Aucun) » et « -> Master » dans la liste des pistes,
« Tout » dans la liste d'événements.

**LA DÉCISION.** **D'abord rendre mesurable, ensuite traduire, et mesurer
entre les deux** : la construction des deux menus sort de `mouseDown` dans
deux fonctions, `mouseDown` les appelle, et `VSM_MENU_LISTE` les liste —
sur un clip MIDI et sur un clip audio importé (`VSM_IMPORT_AUDIO`), puisque
la moitié du menu n'existe que pour l'audio. La liste « avant » est prise
avec ce code-là, sans traduction ; c'est la règle du témoin de même code.
Les libellés à nombre deviennent des modèles entiers (règle de D81).

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 20:10).**
>
> 1. **Les menus de l'arrangement sont listés**, règle et clip, MIDI et
>    audio ; après traduction, leurs seules entrées identiques en français et
>    en anglais sont neutres (« +3 dB », « 16 fois » ne l'est pas), et la
>    liste est publiée.
> 2. **Le français ne change pas** : liste d'avant et liste d'après égales,
>    alignées par contenu, les deux binaires lancés du dossier du build
>    (l'oubli de D82 ne se répète pas).
> 3. **Les listes déroulantes de la liste des pistes** sont en anglais sur une
>    capture en anglais.

> **D83 EST FAITE (10/09/2026, 20:17), ET LES TROIS ATTENDUS SONT TENUS.** Le
> relevé « avant » a été pris par le code refactorisé et encore non traduit —
> le témoin de même code —, les deux binaires lancés du dossier du build, sur
> une copie de la démo où un WAV est importé (`VSM_IMPORT_AUDIO`) pour que le
> menu d'un clip audio existe. `HOME` isolé, préférences de l'utilisateur
> intactes.
>
> | | avant | après |
> |---|---|---|
> | entrées des menus de l'arrangement (règle, clip MIDI, clip audio) | 70 | 70 |
> | identiques en français et en anglais | **70** | **6** — « -6 dB » … « +6 dB », neutres |
> | liste entière en français, alignée par contenu | 382 | **381 égales**, et la 382ᵉ est une entrée des projets récents du `HOME` isolé, déplacée par une capture du banc lui-même |
> | liste des pistes lancée en anglais | « (Aucun) » | **« (None) »**, et « Track N » pour une piste sans nom |
>
> La refactorisation ne change pas le menu : `mouseDown` appelle les deux
> fonctions avec ce qu'il calculait avant (la position du clic, le marqueur
> survolé), et le relevé d'avant a été pris par ce code-là. Les libellés à
> nombre sont des modèles entiers ; « demi-ton » et « demi-tons » en sont deux,
> choisis par la règle française, parce que l'anglais n'accorde pas au même
> endroit. La liste d'événements traduit à l'affichage les types qui sortent
> de `core/` (« Pli » → « Bend »). Table à **583** paires (45 de plus).
>
> **CE QUE LA MESURE A TROUVÉ EN PLUS — UN OUBLI DE LA BASCULE, NOMMÉ ET NON
> FAIT.** La liste des pistes a désormais sa re-traduction ligne par ligne, et
> la capture basculée montre « (None) ». Mais entre la capture lancée en
> anglais et la capture basculée, **3 482 pixels** diffèrent encore, et ils
> sont dans le **volet du rapport d'ouverture** : son titre (« Projet ouvert,
> avec des réserves »), son résumé (« 5 réserves à l'ouverture ») et ses
> boutons « Copier » et « Fermer » restent dans la langue de l'ouverture. Le
> titre et le résumé sont fabriqués par l'appelant au moment où le rapport
> s'ouvre ; les re-traduire demande que le volet garde ses clés plutôt que ses
> textes — une phase à part.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D84 — Le volet de rapport gardait la langue de son ouverture (10/09/2026, 20:25)

**CE QUE D83 A MESURÉ.** Lancée en anglais, l'application montre le rapport
d'ouverture avec « Project opened, with reservations », « 5 reservations on
opening », « Copy », « Close ». Lancée en français puis basculée en anglais,
elle garde « Projet ouvert, avec des réserves », « 5 réserves à l'ouverture »,
« Copier », « Fermer » : **3 482 pixels** entre les deux images, tous dans ce
volet. Le volet sert trois clients — le rapport d'ouverture, l'import d'un
autre DAW, le rapport de reconstruction — et il stocke des TEXTES déjà
traduits, fabriqués par l'appelant au moment de l'ouverture.

**LA DÉCISION.** Le volet ne re-traduit pas un texte qu'on lui a donné tout
fait ; c'est le **client** qui sait fabriquer son rapport, et c'est lui qui le
refait à la bascule — la même fonction qui l'a fabriqué la première fois, avec
les mêmes données. Les boutons, eux, sont au volet, et il les repose lui-même.
Les LIGNES qui viennent de `interchange/` (« aucun instrument, elle restera
silencieuse ») restent françaises : `interchange/` n'a pas de table, c'est
écrit dans A9 depuis D73, et cette phase ne le prétend pas autrement.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 20:25).**
>
> 1. **« Lancée en anglais » et « basculée en anglais » donnent la même image**
>    du volet d'ouverture : 3 482 pixels à D83, attendu **0**, sur le projet
>    abîmé de D75.
> 2. **Le français ne bouge pas** : 0 pixel entre le binaire d'avant et celui
>    d'après, avec un contre-exemple anglais.
> 3. **Rouvrir le rapport après la bascule** (« Voir le dernier rapport »)
>    le montre dans la nouvelle langue, et non dans l'ancienne.

> **D84 EST FAITE (10/09/2026, 20:30), ET LES TROIS ATTENDUS SONT TENUS AU PIXEL
> PRÈS.** Le projet abîmé de D75, `HOME` isolé, préférences de l'utilisateur
> intactes ; le binaire de D83 gardé comme témoin.
>
> | | D83 | D84 |
> |---|---|---|
> | volet d'ouverture, « lancée en anglais » / « basculée en anglais » | 3 482 px | **0 px** |
> | français, binaire d'avant / d'après | — | **0 px** (contre-exemple anglais : 18 878) |
> | volet fermé, bascule, puis « Show the last import report » / lancée en anglais | — | **0 px** |
>
> **Ce qui a été fait, en une ligne par pièce.** Le volet repose ses deux
> boutons (`ImportReportComponent::retraduire`) ; le rapport d'ouverture garde
> ses lignes brutes et son dossier, et sa fabrication — titre, résumé, tons —
> est sortie dans `afficherRapportDOuverture`, que la bascule rappelle volet
> ouvert OU fermé (`showLines(…, montrerLeVolet)`) ; `clientDuRapport_` retient
> qui a rempli le volet en dernier, pour que refaire le rapport d'ouverture
> n'efface pas un rapport d'import ou de reconstruction venu après lui.
>
> **Ce qui reste, et que la phase ne prétend pas avoir fait** : les LIGNES du
> rapport qui viennent de `interchange/` (« aucun instrument, elle restera
> silencieuse ») sont en français dans les deux langues — le 0 pixel les
> compare à elles-mêmes. Les deux autres clients du volet (rapport d'import
> d'un autre DAW, rapport de reconstruction) fabriquent un texte français sans
> `tr()` : ils ne suivent pas la bascule parce qu'ils ne sont pas traduits du
> tout. C'est A9, nommé.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D85 — A9 : la fenêtre des Préférences (10/09/2026, 20:40)

**CE QUE L'INVENTAIRE DES FENÊTRES FLOTTANTES A COMPTÉ.** Sept fenêtres,
une cinquantaine de chaînes affichées hors de `tr()` : les Préférences (24,
dont 3 traduites), l'assemblage des prises (11), l'ordre de jeu (7), les
raccourcis (6), la reconstruction (6), les associations MIDI (5), le
navigateur (2). Aucune n'a de re-traduction : même traduites, elles
resteraient dans la langue du démarrage à la bascule. La phase prend **les
Préférences**, la plus visitée ; les six autres suivent, une par une ou par
paires, chacune avec sa capture.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 20:40).**
>
> 1. **Lancée en anglais, la fenêtre des Préférences est en anglais**, titres,
>    libellés, listes et textes d'état compris — photographiée.
> 2. **Basculée en anglais, elle est la même image** que lancée en anglais.
> 3. **Le français ne bouge pas** : 0 pixel entre le binaire d'avant et celui
>    d'après, avec un contre-exemple anglais.

> **D85 EST FAITE (10/09/2026, 20:42), ET LES TROIS ATTENDUS SONT TENUS AU PIXEL
> PRÈS.** Captures de la fenêtre (`VSM_VUE=preferences`,
> `VSM_CAPTURE_PANNEAUX=1`), `HOME` isolé, préférences de l'utilisateur
> intactes. **Le témoin a été lancé depuis le dossier du build**, à côté du
> vrai binaire : l'état de la chaîne d'analyse, que la fenêtre affiche, dépend
> de l'emplacement de l'exécutable, et deux phases (D80, D82) ont payé cet
> écart.
>
> | | avant | après |
> |---|---|---|
> | français / anglais, lancées chacune dans sa langue | **0 px** — rien n'était traduit | **23 384 px** |
> | « lancée en anglais » / « basculée en anglais » | — | **0 px** |
> | français, binaire d'avant / d'après | — | **0 px** |
>
> Le « 0 px » d'avant est le chiffre qui compte : **la fenêtre des
> Préférences était la même image dans les deux langues**, et le décompte
> statique lui créditait trois chaînes traduites. Après, elle est entièrement
> en anglais, rien n'est tronqué — vérifié sur l'image, pas seulement compté.
>
> **Ce qui a été fait.** Le constructeur ne pose plus que les polices et
> appelle `retraduire()`, qui pose tous les textes fixes — y compris ceux des
> cases et des boutons, que l'en-tête initialisait en français ; les entrées
> « Mono-cœur » / « N thread(s) » sont renommées en lisant la sélection avant
> (piège de D78). Les textes d'état sont des modèles entiers dans `refresh()`
> (« Automatique (%1 ici) », « Prête — trouvée dans %1 »), et la bascule
> rappelle `refreshPreferences()` — le client refait ce qu'il a fabriqué, la
> règle de D84. Table à **607** paires (24 de plus).
>
> **Ce qui reste** : la raison et le remède affichés quand la chaîne est
> introuvable viennent de `interchange/` et restent français (A9) ; les six
> autres fenêtres flottantes — prises, ordre de jeu, raccourcis,
> reconstruction, associations MIDI, navigateur — suivent.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D86 — A9 : l'assemblage des prises et l'ordre de jeu (10/09/2026, 20:55)

**CE QUI RESTE DES FENÊTRES FLOTTANTES, LES DEUX QUI ÉCRIVENT LE MATÉRIAU.**
L'assemblage des prises (11 chaînes) et l'ordre de jeu (7) sont les deux
fenêtres dont un bouton RÉÉCRIT le morceau — « Composer », « Aplatir » : ce
sont les deux où une phrase mal comprise coûte le plus. Aucune chaîne n'y
passe par `tr()`, leurs boutons sont initialisés en français dans l'en-tête,
et aucune n'a de re-traduction. Même traitement que les Préférences (D85) :
`retraduire()` pose les textes fixes, le rafraîchissement fabrique les textes
d'état en modèles entiers, la bascule rappelle les deux.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 20:55).**
>
> 1. **Lancées en anglais, les deux fenêtres sont en anglais** — photographiées
>    dans un état qui montre leurs textes d'état et, pour l'assemblage, une
>    ligne de tronçon (le modèle « mesures %1 à %2 → %3 »).
> 2. **Basculées en anglais, elles sont la même image** que lancées en anglais.
> 3. **Le français ne bouge pas** : 0 pixel, témoin lancé du dossier du build.
> 4. **Rien n'est tronqué en anglais** — regardé sur l'image, pas compté.

> **D86 EST FAITE (10/09/2026, 21:24). LES QUATRE ATTENDUS SONT TENUS — LE
> QUATRIÈME AU SECOND ESSAI, ET CE SECOND ESSAI A CORRIGÉ LE FRANÇAIS AUSSI.**
> Un projet fabriqué pour l'occasion (trois prises, trois repères, un tronçon
> posé par `troncon:0:1:3`), le témoin lancé du dossier du build, `HOME`
> isolé.
>
> | fenêtre | FR / EN avant | FR / EN après | lancée / basculée en anglais | français avant / après |
> |---|---|---|---|---|
> | assemblage des prises | **0 px** — rien n'était traduit | 9 698 px | **0 px** | 1 980 px, situés (voir plus bas) |
> | ordre de jeu | **0 px** | 4 819 px | **0 px** | **0 px** |
>
> **Ce que la première image anglaise a montré, et qu'aucun compte n'aurait
> vu.** L'étiquette entre les deux champs de mesure s'affichait « … » : sa
> case faisait 16 px, taillés pour « à », et « to » n'y tient pas. Elle se
> taille désormais sur son texte (au moins ses 16 px d'origine), et la
> re-traduction la replace. La même image disait « The song is 1 bars long » :
> le défaut était **français d'abord** — « Le morceau fait 1 mesures » —, et le
> singulier a maintenant son propre modèle.
>
> **Les 1 980 pixels du français, situés** : la ligne d'aide (y 20-59), où
> « 1 mesures » devient « 1 mesure » et décale la fin de la phrase ; et la
> rangée des champs (y 80-119), où la case de « à », taillée sur son texte,
> s'élargit de quelques pixels et décale ce qui la suit (x 163-283). Rien ailleurs. Ce sont les deux corrections, et
> seulement elles — c'est pourquoi le chiffre est publié au lieu d'un « 0 »
> qu'il aurait fallu obtenir en laissant les défauts.
>
> **Et une leçon d'exploitation, écrite dans `CLAUDE.md`.** Pendant la série de
> captures, le fichier de préférences de l'utilisateur a changé : le contrôle
> `cmp` a échoué. Lu clé par clé, le changement venait de **l'utilisateur
> lui-même** — des projets récents que le banc n'a jamais ouverts, une fenêtre
> déplacée —, qui se servait de l'application pendant ce temps. Le fichier n'a
> pas été « rétabli », ce qui aurait effacé ce qu'il venait de faire ; le
> contrôle compare désormais à une copie prise juste avant chaque série.
>
> Table à **626** paires (19 de plus). Tests : 330 core, 1 291 audio,
> 297 interchange, 25 clap, 11 panels, 172 Python, ruff et mypy — tout vert.

### Phase D87 — A9 : les raccourcis, les associations MIDI, le navigateur et la fenêtre de reconstruction (10/09/2026, 21:35)

**LES QUATRE DERNIÈRES FENÊTRES FLOTTANTES.** Les raccourcis affichent 56
commandes groupées en six familles, dont libellés et familles viennent de la
table de `interchange/` (« Édition », « Lecture / arrêt ») : ils se traduisent
à l'AFFICHAGE, comme les gammes (D78) et les accords (D80) — la table des
raccourcis s'écrit aussi dans un fichier, et ses noms ne doivent pas dépendre
de la langue. Les associations MIDI, le navigateur et la fenêtre de
reconstruction ont leurs textes et leurs boutons en français, initialisés dans
l'en-tête pour trois d'entre eux, et aucune re-traduction.

**UNE LIMITE DE LA MESURE, DITE D'AVANCE.** La fenêtre de reconstruction ne
s'ouvre qu'après le choix d'un fichier audio dans une boîte du système, que
rien ne pilote sans souris : elle est traduite, mais **pas photographiée** ; son
français est vérifié par ses littéraux, comme en D77.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 21:35).**
>
> 1. **Lancées en anglais, les trois fenêtres photographiables sont en
>    anglais** — raccourcis, associations MIDI, navigateur — et rien n'y est
>    tronqué.
> 2. **Basculées en anglais, elles sont la même image** que lancées en anglais.
> 3. **Le français ne bouge pas** : 0 pixel, témoin lancé du dossier du build.

> **D87 EST FAITE (10/09/2026, 21:39). LES TROIS ATTENDUS SONT TENUS, AVEC UNE
> RÉSERVE ÉCRITE SUR LE NAVIGATEUR.** Le témoin lancé du dossier du build, les
> fenêtres ouvertes par leurs entrées de menu (`VSM_MENU`), photographiées par
> `VSM_CAPTURE_PANNEAUX`, `HOME` isolé, préférences de l'utilisateur intactes
> (comparées à une copie prise juste avant la série).
>
> | fenêtre | FR / EN avant | FR / EN après | lancée / basculée en anglais | français avant / après |
> |---|---|---|---|---|
> | raccourcis clavier | **0 px** | 9 494 px | **0 px** | **0 px** |
> | associations MIDI | **0 px** | 6 506 px | **0 px** | **0 px** |
> | navigateur | **0 px** | 1 841 px | **0 px** | **0 px** |
>
> Trois fenêtres de plus qui étaient **la même image dans les deux langues**.
> Les 56 commandes, les 6 familles et les 4 raccourcis fixes de la table des
> raccourcis ont leur paire, traduits à l'affichage ; les boutons initialisés
> dans les en-têtes sont reposés par `retraduire()` ; la bascule rappelle les
> clients (`refreshShortcutList`, `refreshMidiLearnList`). La fenêtre de
> reconstruction est traduite et **non photographiée**, comme annoncé. Table à
> **688** paires (62 de plus).
>
> **LA RÉSERVE, VUE SUR L'IMAGE DU NAVIGATEUR.** Le champ de recherche est en
> anglais ; les lignes ne le sont qu'à moitié. Les **descriptions des machines**
> (« Clavinet (la corde qui sonne entière au relâchement) ») viennent des
> machines elles-mêmes (`displayName`), et la source « Parc VSM » de
> `interchange/` : ce sont des chaînes du moteur, pas de l'application, et
> elles relèvent du reste d'A9. La même image montre la colonne de la source
> **coupée par le bord de la fenêtre** (« Parc VSI… ») — dans les deux
> langues, donc un défaut de disposition antérieur, pas un effet de la
> traduction. Nommé, non fait.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D88 — La colonne de la source du navigateur passait sous la barre de défilement (10/09/2026, 21:50)

**CE QUE D87 A VU.** Sur l'image du navigateur, la source de chaque ligne se
lisait « Parc VSI… » : coupée par le bord, dans les deux langues. La lecture
du code l'explique : la liste reçoit la largeur ENTIÈRE de sa zone de
défilement, alors que la barre verticale en occupe le bord droit ; la colonne
de la source, alignée à droite, passe dessous. Un défaut de lisibilité, que la
règle du projet place avant tout goût : ce qui s'affiche doit se lire.

**LA CORRECTION.** La liste reçoit la largeur VISIBLE
(`Viewport::getMaximumVisibleWidth()`), à la mise en page et après chaque
filtrage — la manière dont JUCE l'entend.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 21:50).**
>
> 1. **« Parc VSM » se lit en entier**, en français et en anglais, et le texte
>    le plus à droite de chaque ligne s'arrête AVANT la barre de défilement —
>    mesuré sur l'image, colonne de pixels par colonne.
> 2. **Rien d'autre ne bouge qu'à cause de ce changement** : les pixels qui
>    diffèrent entre l'avant (les captures de D87) et l'après sont situés, et
>    ils sont dans les lignes de la liste, pas dans le champ de recherche ni
>    dans le compteur.

> **D88 EST FAITE (10/09/2026, 22:06), ET LES DEUX ATTENDUS SONT TENUS — APRÈS
> QU'UN TROISIÈME CHIFFRE A DÉNONCÉ UN AUTRE DÉFAUT.** Avant : les captures de
> D87 (le même binaire). Après : le binaire de D88, mêmes conditions.
>
> | | avant (D87) | après |
> |---|---|---|
> | texte de la source, première ligne | x 562-603, **42 px** — « Parc VSI » | x 554-602, **49 px** — « Parc VSM », arrêté avant le curseur de la barre (x 605) |
> | pixels changés, français | — | 7 006, y 51-551, x 387-603 : les lignes de la liste |
> | pixels changés, anglais | — | 7 006, même zone |
>
> Les 7 006 pixels sont la correction et sa conséquence : la colonne des noms,
> taillée en proportion d'une largeur devenue plus étroite, coupe ses points de
> suspension un peu plus tôt. Le champ de recherche et le compteur n'ont pas
> bougé.
>
> **LE TROISIÈME CHIFFRE.** La première série d'après donnait, en anglais,
> **301 562 pixels** changés sur toute la liste — trop pour cette correction.
> L'image l'a montré : tout le fond de la liste était éclairci. La cause était
> dans le dessin du survol, `if (i == survol_) g.fillAll(…)` : `fillAll`
> remplit toute la zone à repeindre, pas la ligne. Le pointeur de la souris
> passait au-dessus de la liste pendant cette capture, et c'est la liste
> entière qui s'éclaircissait. Corrigé : la ligne survolée, et elle seule
> (`fillRect`). **Ce qui est vérifié, et ce qui ne l'est pas** : la série
> refaite donne les mêmes 7 006 pixels dans les deux langues, et aucune ligne
> éclaircie (0 sur 19) — mais le pointeur n'était au-dessus d'aucune ligne
> cette fois. La correction du survol est donc établie par le code et par
> l'explication du chiffre, pas par une image qui montrerait une seule ligne
> éclaircie : la position de la souris ne se pilote pas. Les deux `fillAll`
> de la liste d'événements ont été relus : ils sont dans `paintRowBackground`,
> où JUCE limite le dessin à la ligne, et ils sont justes.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D89 — A9 : les phrases du moteur, dans le rapport d'ouverture (10/09/2026, 22:15)

**CE QUI RESTE LE PLUS VISIBLE D'A9.** Le volet du rapport d'ouverture a un
titre anglais et des lignes françaises : « Piste 2 (Oubliee) : aucun
instrument, elle restera silencieuse ». Ces phrases sont fabriquées dans
`interchange/` — 117 appels dans 14 fichiers — et par l'application pour les
réserves d'effet. Elles doivent RESTER françaises là où elles naissent :
`vsm-render` les écrit sur sa sortie d'erreur, la chaîne d'analyse les lit, et
D75 a fait des deux lecteurs d'un dossier deux voix qui disent la même chose au
mot près.

**LA DÉCISION : TRADUIRE À L'AFFICHAGE, PAR MODÈLES.** Un traducteur de phrases
(`trPhrase`) essaie d'abord la table, puis une liste de modèles
(« Piste %1 (%2) : aucun instrument, elle restera silencieuse » ↔ « Track %1
(%2): no instrument, it will stay silent »). Les arguments sont des DONNÉES —
noms de pistes, identifiants d'effets, chemins — et passent intacts : une
piste nommée « Batterie » ne doit pas devenir « Drums ». Les résumés composés
(« 9 paramètre(s) appliqué(s), 1 non pris en charge : … ») se traduisent
segment par segment, et un segment qu'aucun modèle ne reconnaît passe tel quel
plutôt que d'être deviné. Le ton de chaque ligne (perte, information) reste lu
sur le français, qui est la donnée.

**LA MESURE, AVANT LE TRADUCTEUR.** `VSM_RAPPORT_LISTE` écrit ce que le volet
AFFICHE, ligne par ligne ; elle est compilée seule d'abord, et le relevé
« avant » est pris avec ce binaire-là. Trois projets : l'abîmé de D75 (cinq
lignes), une copie de `sky-v4` (les notes douteuses), et un projet neuf qui
déclenche les autres familles (réglage d'effet inconnu, bus de départ inconnu,
paramètre de preset non pris en charge, échantillon manquant, piste décrite
sans MIDI).

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026, 22:15).**
>
> 1. **En anglais, les lignes du rapport sont en anglais** : sur les trois
>    projets, le compte des lignes identiques en français et en anglais
>    tombe, et chaque ligne qui reste identique est nommée, avec sa raison.
> 2. **Le français ne change pas d'un caractère** : le texte affiché en
>    français est identique avant et après, ligne pour ligne.
> 3. **Les données passent intactes** : noms de pistes, identifiants et
>    chemins se retrouvent tels quels dans la ligne anglaise.
> 4. **`vsm-render` et `VSM_OUVERTURE` ne changent pas** : le texte brut reste
>    celui de D75.

> **D89 EST FAITE (10/09/2026, 22:27), ET LES QUATRE ATTENDUS SONT TENUS — AU
> TROISIÈME ESSAI.** Le relevé « avant » a été pris par le binaire qui ne
> portait que la commande de liste ; celui d'après, par le binaire de D89, dans
> les mêmes conditions (`HOME` isolé, préférences de l'utilisateur intactes).
>
> | projet | lignes du volet identiques FR / EN, avant | après |
> |---|---|---|
> | l'abîmé de D75 | 8 / 10 | **3 / 10** |
> | copie de `sky-v4` | 4 / 6 | **3 / 6** |
> | le projet neuf (cinq familles de phrases) | 10 / 12 | **3 / 12** |
>
> Les trois lignes qui restent identiques sont, dans chaque projet, **le nom du
> dossier et deux lignes vides** : rien à traduire. Une recherche des mots
> français (« piste », « échantillon », « réglage », « inconnu »…) dans les
> lignes anglaises ne rend **rien**. Le texte affiché en français est
> **identique** avant et après, sur les trois projets ; les lignes brutes
> (`VSM_OUVERTURE`) aussi, et `vsm-render` n'a pas été touché — `interchange/`
> ne l'a pas été. Les données passent intactes : « Oubliee »,
> `com.autre.editeur.synth-absent`, `filter.9.cutoff`, les chemins.
>
> **LES DEUX ESSAIS QUI N'ONT PAS COMPTÉ, ET POURQUOI ILS SONT ÉCRITS.**
> 1. **La première compilation a échoué**, et le relevé a tourné quand même,
>    sur l'ancien binaire : ses chiffres ne mesuraient rien de D89. La table
>    des modèles déclarait des `const char*` initialisés par des `u8"…"` —
>    le piège C++20 que `CLAUDE.md` décrit pour `juce::String`, sous une autre
>    forme. La commande de mesure s'arrête désormais sur un échec de
>    compilation, au lieu de l'afficher et de continuer.
> 2. **Le deuxième essai a produit des lignes à moitié traduites** —
>    « Track 1: 9 paramètre(s) appliqué(s), 1 unsupported: … » — que le compte
>    des lignes identiques ne pouvait pas voir, puisqu'elles n'étaient plus
>    identiques. Un segment comme « %1 non pris en charge : %2 » reconnaissait
>    la ligne ENTIÈRE, son « %1 » avalant le préfixe français, recopié comme
>    une donnée. Les nombres ont désormais leur marqueur (`%#N`, capturé par
>    `[0-9]+`) : 21 arguments en portent un. **La leçon : un compte de lignes
>    identiques ne suffit pas à prouver une traduction — il faut aussi
>    chercher le français qui reste DANS les lignes qui ont changé.**
>
> **CE QUE LE MÉCANISME COUVRE, ET CE QU'IL NE COUVRE PAS ENCORE.** 29 modèles :
> les phrases du chargement (preset introuvable ou illisible, prises, pistes
> décrites contre MIDI, tronçons écartés), les phrases d'instrument de D75, les
> réserves d'effet et de bus, les résumés de preset et d'échantillons segment
> par segment, les notes douteuses. Il ne sert encore que le **rapport
> d'ouverture** : les messages d'export (avertissements du rendu), les réserves
> d'un preset appliqué à la main, le rapport d'import d'un autre DAW et celui
> de reconstruction affichent toujours le français du moteur. Le traducteur est
> là ; les brancher, et compléter ses modèles, est la suite d'A9.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D90 — A9 : l'export audio et l'export par stems, en anglais (10/09/2026)

**CE QUE LE TÉMOIN MONTRE.** En anglais, le compte rendu d'un export est
entièrement français. Sur l'abîmé de D75, ce que la boîte « Export audio
terminé » affiche (`VSM_EXPORT`, qui écrit le même message) : « Rendu écrit : »,
« 3.6 s, 44.1 kHz, 24 bits, crête 0.415. », « Piste 2 (Oubliee) : aucun
instrument, elle restera silencieuse », « Piste 7 : effet « chorus-de-1987 »
inconnu, non appliqué » ; et pour les stems (`VSM_EXPORT_STEMS`) : « 7 stems
écrits dans : », « 02 - Oubliee.wav — silencieux », « la tranche master n'est
pas dans les stems (c'est voulu) : … ». Les deux fenêtres d'options — « Exporter
en audio », « Exporter les stems » — n'appellent pas `tr()` du tout, et leur
français est écrit SANS ACCENTS (« Crete a -1 dBFS », « La selection »,
« Frequence », « Decoupage », « au bit pres », « a la vitesse »), et cela dès
leur écriture : `git log -S` les trouve sans accents, et déjà en `u8"…"`, dans
les commits qui les ont introduits (D6.1, D6.2, D21.5) — dont les titres
évitaient eux-mêmes les accents. Aucune panne n'est à l'origine ; c'était une
précaution, et `tr()` la rend inutile.

**LA DÉCISION.** Les phrases de l'APPLICATION (l'en-tête, la ligne de format, le
niveau, l'avertissement d'écrêtage, les lignes de stems) passent par `tr()`, en
modèles `%1` remplis après traduction ; celles du MOTEUR (avertissements et
erreurs du rendu, venus d'`interchange/`) passent par `trPhrase`, avec les
modèles qui leur manquent. `interchange/` n'est pas touché : `vsm-render` et la
chaîne d'analyse lisent toujours le français (D89). Les accents reviennent dans
les deux fenêtres, puisque `tr()` lit l'UTF-8 : c'est le SEUL changement voulu
du français.

**LA MESURE.** Le témoin est le binaire de D89 (`temoin-d90`, dans le dossier
de construction, `HOME` isolé), mesuré avec les mêmes commandes : `VSM_EXPORT` et
`VSM_EXPORT_STEMS` sur l'abîmé de D75 et sur le projet de démonstration de D77 ;
les deux fenêtres ouvertes par `VSM_MENU` et photographiées par
`VSM_CAPTURE_PANNEAUX`, qui prend aussi les boîtes d'alerte.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026).**
>
> 1. **En anglais, les comptes rendus d'export sont en anglais** : les lignes
>    identiques en français et en anglais tombent à celles qui n'ont rien à
>    traduire (chemins, lignes vides), chacune nommée ; et aucun mot français
>    ne reste DANS les lignes anglaises qui ont changé (la leçon de D89).
> 2. **Le compte rendu français ne change pas d'un caractère**, sur les deux
>    projets, mixage et stems.
> 3. **Les fichiers rendus sont identiques octet pour octet** — témoin, après
>    en français, après en anglais : la langue ne touche que les mots.
> 4. **Les deux fenêtres, photographiées** : en anglais, plus un mot français ;
>    en français, les seules différences avec le témoin sont les accents rendus
>    aux six libellés nommés plus haut.
> 5. **`interchange/` et `vsm-render` ne changent pas.**
>
> **COMPLÉTÉ APRÈS LES PHOTOS DU TÉMOIN, AVANT TOUTE MESURE D'APRÈS.** La photo
> française du témoin montre deux choses que l'attendu 4 n'avait pas vues : un
> **septième** libellé sans accent (« Au pas du temps reel »), et le texte de la
> fenêtre des stems, coupé à la main par des retours à la ligne, qui laisse
> « qu'il » **seul sur sa ligne** à 150 %. Les retours forcés sont retirés : la
> fenêtre coupe elle-même, à sa largeur. L'attendu 4 devient donc : en français,
> les seules différences avec le témoin sont les accents des **sept** libellés,
> et la coupe des lignes du texte des stems — les mots, eux, inchangés.

> **D90 EST FAITE (10/09/2026), ET LES CINQ ATTENDUS SONT TENUS — APRÈS UNE
> PHOTO QUI A MONTRÉ CE QU'AUCUN COMPTE NE VOYAIT.** Témoin et après dans les
> mêmes conditions (`HOME` isolé, préférences de l'utilisateur intactes à
> chaque série).
>
> | compte rendu (mixage et stems) | lignes identiques FR / EN, témoin | après |
> |---|---|---|
> | l'abîmé de D75 | 18 / 18 | **5 / 18** |
> | le projet de démonstration de D77 | 11 / 11 | **5 / 11** |
>
> Les cinq lignes qui restent identiques sont, dans les deux projets, **les deux
> chemins** (le fichier, le dossier des stems) **et trois lignes vides**. La
> recherche des mots français dans les lignes anglaises qui ont changé rend une
> seule ligne, « 03 - Inconnue.wav — silent » : « Inconnue » est le nom d'une
> piste, une donnée. Le compte rendu français est **identique au témoin**,
> caractère pour caractère, sur les deux projets. Les **11 fichiers rendus**
> (2 mixages, 9 stems) sont **identiques octet pour octet** entre le témoin,
> l'après en français et l'après en anglais. `interchange/` et `vsm-render` ne
> sont pas touchés.
>
> **LES FENÊTRES.** En français, « Exporter en audio » diffère du témoin de
> **18 pixels**, dans les accents des trois libellés visibles sur la photo
> (« Fréquence », « à la vitesse », « près ») ; les quatre autres (« La
> sélection », « Crête à », « temps réel ») sont dans des listes fermées, et
> leur texte se vérifie dans le code, pas à l'image. « Exporter les stems »
> passe de 496 × 348 à **419 × 332** : la fenêtre coupe elle-même son texte en
> trois lignes, et « qu'il » n'est plus seul. En anglais, les deux fenêtres ne
> portent plus un mot français. Vu sans être traité : dans « Exporter en
> audio », un vide sépare le texte du rendu en temps réel de la liste qui le
> suit, dans les deux langues, et déjà sur le témoin.
>
> **CE QUE LA PHOTO A MONTRÉ, ET PAS LE COMPTE.** La première série d'après
> donnait, en anglais, un bouton **« Undo »** là où il fallait « Cancel ». La
> table traduit « Annuler » par « Undo », ce qui est juste dans le menu
> Édition et faux sur le bouton d'une boîte : le français met deux sens sous un
> seul mot, et la table, indexée par le français, n'en porte qu'un. Le défaut
> n'était pas né avec D90 : **quatre boîtes traduites plus tôt (plugin CLAP,
> catalogue d'instruments, effets tiers) et la fenêtre de reconstruction (D87)
> disaient déjà « Undo »** pour « Cancel » — établi par le code et la table,
> ces fenêtres n'ayant pas été photographiées en anglais. Remède :
> `trSelon(contexte, texte)` cherche d'abord « Annuler@bouton », et rend la
> traduction ordinaire à défaut ; en français, le texte seul. Les **20**
> boutons d'annulation des boîtes passent par lui : les 7 qui disaient « Undo »
> (dont les 2 de cette phase), et 13 qui restaient en français faute de
> `tr()`. Le bouton du piano roll et son menu gardent « Undo », qui est leur
> sens. Vérifié à l'image sur trois boîtes : les deux d'export, et « Renommer
> les pistes en série », dont le bouton dit « Cancel » en anglais et
> « Annuler » en français — **et dont tout le reste est encore français** : ces
> treize boîtes-là n'ont que leur bouton de traduit.
>
> **CE QUI RESTE D'A9.** Les treize boîtes dont seul le bouton est traduit
> (renommer un clip, des pistes en série, un marqueur ; aller à une mesure ;
> programme MIDI ; preset de piste ; aplatir l'assemblage ; geler ;
> reconstruire un morceau déposé ; une boîte de la chaîne d'effets) ; les
> réserves d'un preset appliqué à la main ; les rapports d'import d'un autre
> DAW et de reconstruction ; le message d'import audio ; les descriptions des
> machines et la source « Parc VSM » du navigateur ; « RÉGLAGES », titre de
> section des façades. Table à **746** paires, **44** modèles de
> phrases — 30 de D89 et 14 de D90 : le « 29 modèles » écrit par D89 était
> faux d'un, compté à la main ; celui-ci est compté sur le fichier.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D91 — A9 : les boîtes dont seul le bouton était traduit (10/09/2026)

**CE QUE D90 A LAISSÉ.** Treize boutons d'annulation disent « Cancel » en
anglais, dans des boîtes dont tout le reste est français : renommer un clip ;
« le clip fait N mesures » ; un fichier déposé (deux boîtes : poser, ou poser
et reconstruire) ; aplatir l'ordre de jeu ; reporter la piste en audio ;
renommer les pistes en série ; aller à la mesure ; programme MIDI ; enregistrer
la piste comme preset ; poser et renommer un repère ; enregistrer un preset
d'effet. S'y ajoute la boîte qui dit qu'un clip créé a été raccourci par son
voisin.

**LA DÉCISION.** Tout par `tr()`, en modèles `%1` remplis après traduction. Un
mot, deux verbes : « Poser » un fichier sur une piste se dit « Place », « Poser »
un repère se dit « Add » — `trSelon("repere", …)`, le mécanisme de D90. Les
raisons pour lesquelles la reconstruction est indisponible, fabriquées par
`interchange/` (`ReconstructionChain`), restent hors de cette phase : ce sont
des phrases du moteur, à traiter avec les autres.

**CE QU'IL FAUT D'ABORD DONNER À LA MESURE.** Six de ces boîtes s'ouvrent par la
barre de menus (`VSM_MENU`) ; les autres par un clic droit dans l'arrangement,
un dépôt de fichier ou le menu d'un effet, qu'aucune commande du banc
n'atteint. Deux commandes sont donc ajoutées AVANT la traduction, et compilées
seules pour faire le témoin : `VSM_MENU_CONTEXTE=quel:libellé` (`regle`,
`clip-midi`, `clip-audio`, `effets`), qui exécute une entrée d'un menu
contextuel par la MÊME méthode que le clic ; et `VSM_DEPOSER=fichier`, qui
appelle `filesDropped` comme le fait un dépôt. Chaque boîte est photographiée
par `VSM_CAPTURE_PANNEAUX`, en français et en anglais, avec le témoin puis avec
D91.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026).**
>
> 1. **Les deux commandes ouvrent, avec le témoin, les boîtes qu'elles
>    visent** : chaque photo porte le titre de la sienne.
> 2. **En anglais, plus un mot français dans les boîtes photographiées**, hors
>    les données (noms de pistes, de clips, chemins).
> 3. **En français, chaque boîte est la même image que celle du témoin, à 0
>    pixel près** — et la paire français / anglais d'une même boîte, elle,
>    diffère : c'est ce qui prouve que la photo voit le texte.
> 4. **Les boîtes non photographiées sont nommées**, avec la raison.

> **D91 EST FAITE (10/09/2026) ; LES ATTENDUS 1 À 3 SONT TENUS, ET LE 4 NOMME
> CE QUE LE BANC N'ATTEINT PAS.** Avec le témoin — les deux commandes seules,
> compilées avant la traduction —, **les 11 cas** ouvrent leur boîte, dans
> les deux langues, chaque photo portant son titre : aller à la mesure,
> enregistrer la piste comme preset, renommer les pistes en série, reporter la
> piste en audio, programme MIDI (barre de menus) ; poser un repère, renommer un repère,
> renommer un clip, « le clip fait N mesures » après un import audio,
> enregistrer un preset d'effet (`VSM_MENU_CONTEXTE`) ; « Que faire de ce
> fichier ? » (`VSM_DEPOSER`).
>
> | les 11 boîtes photographiées | témoin → D91 | |
> |---|---|---|
> | en français | **0 pixel pour 10** ; 2 188 pour l'onzième | |
> | en anglais | toutes changent (taille ou texte) | |
> | français / anglais, avec D91 | toutes diffèrent | la photo voit le texte |
>
> Les 2 188 pixels de « Enregistrer la piste comme preset » sont le **chemin**
> que la boîte affiche : le dossier de presets passe par la copie du projet du
> banc, `…/d91/avant/…` pour le témoin et `…/d91/apres/…` pour D91 — une
> donnée, lue sur les deux photos côte à côte. En anglais, les onze boîtes ne
> portent plus un mot français ; restent les données : « Acid Bass »,
> « Refrain », « abime.wav », le type d'effet `reverb`, le chemin. « Poser »
> y devient « Place » sur un fichier déposé et « Add » sur un repère.
>
> **CE QUE LE BANC N'ATTEINT PAS.** « Aplatir l'ordre de jeu » s'ouvre depuis
> la fenêtre de l'ordre de jeu, sur des sections ; la boîte « Créer un clip »
> ne paraît que si le clip créé est raccourci par son voisin. Aucune commande
> du banc ne les ouvre : leur texte se vérifie dans le code et la table.
>
> **« PROGRAMME MIDI », OU LA COURSE DE D72 REVENUE.** Sur le projet du banc,
> l'entrée était grisée : elle demande une piste MIDI qui a un port de sortie,
> et le projet n'en donne aucun. Une copie où la piste 0 reçoit un port
> l'active — et la boîte n'a pas été photographiée, trois fois de suite.
> `gdb` montrait pourtant `promptMidiProgram` atteinte par le menu. L'A/B
> suivant a raté AUSSI « Aller à la mesure », photographiée dans la série
> juste avant : ce n'était pas le port, c'était la course que D72 a mesurée
> sur les boîtes modales (une photo sur sept au pire), et non un délai.
> Relancée, la boîte est venue du premier coup dans les quatre cas (témoin et
> D91, français et anglais) : **0 pixel** en français, l'anglais change. Le
> diagnostic « le port ferme la boîte » a failli s'écrire sur trois ratés.
>
> **VU SANS ÊTRE TRAITÉ.** Deux textes gardent des retours à la ligne forcés
> qui coupent mal à 150 % : « pitch. » seul sur sa ligne dans « Clip is N
> bars », et « Drums # » coupé entre deux lignes dans le renommage en série —
> comme « Batterie # » en français, déjà sur la photo de D90. Les retirer
> changerait l'image française, que cette phase devait laisser intacte : c'est
> un geste à part, comme celui de D90 pour les stems.
>
> Table : **786** paires, **44** modèles de phrases.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy — tout vert.

### Phase D92 — A9 : le rapport de reconstruction, en anglais (10/09/2026)

**CE QUI RESTE LE PLUS VISIBLE D'A9 APRÈS D91.** Le volet qui montre le rapport
de reconstruction (`showReconstructionReport`, 270 lignes) fabrique ses lignes
en français à partir de `rapport.json` : 65 fragments cousus autour des
chiffres — « 6 piste(s) reconstruite(s) · distance globale 0.2325 (0 =
identique, 1 = silence) », « … % de l'énergie du morceau », « Batterie → … »,
« Verdict du mélange », « Réverbération au mélange : RETENUE… ». En anglais,
tout le volet est français, sauf son cadre. C'est pourtant l'écran du second
axe du projet : celui où l'on juge une reconstruction.

**LA DÉCISION : UNE LIGNE, UN MODÈLE.** Chaque ligne devient un modèle `tr()`
rempli après traduction — la PHRASE entière, et non ses fragments : l'ordre
des mots change d'une langue à l'autre, et « % de l'énergie du morceau »
traduit seul ne donne pas une phrase anglaise. Les compléments facultatifs
(gate, profil, polyphonie, « la plus loin de l'original ») restent des
segments ajoutés, chacun son modèle. Les données — noms de stems, machines,
familles de pièces, chiffres — passent intactes. Les avertissements de la
batterie viennent de la chaîne d'analyse (Python) : `trPhrase` s'y essaie, et
ce qu'il ne reconnaît pas passe tel quel — compté à part.

**LA MESURE, ET CE QU'IL A FALLU LUI RENDRE.** `VSM_RAPPORT_LISTE` (D89)
listait le volet AVANT que `VSM_RAPPORT` n'y pose le rapport de
reconstruction : elle aurait listé le rapport d'ouverture. Elle est déplacée
après, seule, et ce binaire est le témoin.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (10/09/2026).**
>
> 1. **En anglais, les lignes du volet sont anglaises** : les lignes identiques
>    en français et en anglais tombent à celles qui n'ont rien à traduire,
>    chacune nommée ; aucun mot français ne reste dans les lignes changées,
>    hors données et hors avertissements de la chaîne non reconnus — comptés à
>    part.
> 2. **Le français ne change pas d'un caractère.**
> 3. **Les chiffres sont les mêmes dans les deux langues** : chaque ligne
>    anglaise porte la même suite de nombres que la ligne française qui lui
>    répond.
> 4. **D89 est intacte** : sans `VSM_RAPPORT`, la liste du rapport d'ouverture
>    est la même qu'avant le déplacement de la commande.

> **D92 EST FAITE (10/09/2026), ET LES QUATRE ATTENDUS SONT TENUS.** Témoin et
> D92 dans les mêmes conditions (`HOME` isolé, préférences de l'utilisateur
> intactes), sur quatre rapports réels copiés hors de `reconstruction/` :
>
> | rapport | lignes identiques FR / EN, témoin | D92 | ce qui reste identique |
> |---|---|---|---|
> | `sky-v4` (batterie, verdict) | 26 / 26 | **7 / 26** | le dossier, 5 lignes vides, « bass → vsm.wavetable · distance 0.2190 » |
> | `sky-parite` (partage, batterie éclatée) | 28 / 28 | **7 / 28** | le dossier, 6 lignes vides |
> | `parite-reverb` (réverbération, verdict) | 31 / 31 | **8 / 31** | le dossier, 7 lignes vides |
> | `children-dream-v7` (cinq stems) | 12 / 12 | **8 / 12** | le dossier, 3 lignes vides, 4 lignes « stem → machine · distance » |
>
> « distance » s'écrit de même dans les deux langues : une ligne faite de noms,
> d'une flèche et de ce mot n'a rien à traduire. Le français est **identique
> au témoin** sur les quatre rapports ; les **97 lignes sur 97** portent la
> même suite de nombres en français et en anglais. La recherche de mots
> français dans les lignes anglaises changées en trouve deux, qui sont des
> données : `--batterie-par-piece`, le nom d'une option de la chaîne, et
> « Batterie · hihat », le nom d'une piste. **D89 est intacte** : la liste du
> rapport d'ouverture du témoin est celle de D89, au caractère près, en
> français et en anglais, et D92 ne la change pas.
>
> **LES PHRASES DE LA CHAÎNE D'ANALYSE.** Les avertissements de la batterie
> viennent de `vsm_drumkit.py` : sept modèles pour `trPhrase` (famille écartée,
> frappe non isolée, voix absente et repli par le spectre, par la table, ou
> aucun). Les libellés du verdict du mélange — « machine suivante (…) »,
> « arbitrage » — sont dans les rapports du banc, écrits par une version
> antérieure de la chaîne : le source actuel ne les produit plus en toutes
> lettres, et seul « réglage » y reste. Tous sont traduits à l'AFFICHAGE ;
> `rapport.json`, que la chaîne relit, reste en français.
>
> **CE QUI RESTE.** La bascule de langue en direct ne redessine pas CE rapport
> (elle redessine le rapport d'ouverture, D84) : `VSM_MENU`, qui bascule,
> passe avant `VSM_RAPPORT` dans le banc, qui ne sait donc pas le tester. Le
> rapport rouvert paraît dans la nouvelle langue. Table à **811** paires,
> **52** modèles de phrases.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels,
> 172 Python, ruff et mypy (106 fichiers) — tout vert.

### Phase D93 — A9 : le rapport d'import d'un autre DAW, en anglais, et les deux rapports suivent la bascule (11/09/2026)

**LES DEUX RESTES D'A9 QUI SE TIENNENT PAR LA MAIN.** Le volet de rapport a
quatre clients : le rapport d'ouverture (D84, D89), le rapport de
reconstruction (D92), le rapport d'import d'un autre DAW et l'échec d'un
import. Après D92, deux restent en défaut :

- **le rapport d'import est entièrement français en anglais** — son titre, ses
  quatre lignes de résumé fabriquées par le volet, et les lignes du lecteur
  (`interchange/src/DawImport.cpp`), écrites en français avec leurs données
  cousues dedans ; l'échec (« Import impossible », le message du `.cpr` qui
  nomme les deux chemins praticables) aussi ;
- **la bascule de langue ne refait ni lui ni le rapport de reconstruction** :
  `retraduire()` ne refait que le rapport d'ouverture, les trois autres clients
  étant rangés sous `ClientDuRapport::autre`, qui veut dire « ne rien faire ».

Les deux se tranchent ensemble parce que le banc ne sait mesurer le premier
que par le second : `VSM_IMPORT` passe avant `VSM_MENU` au démarrage, donc un
rapport d'import photographié en anglais est un rapport refait par la bascule.

**LES DÉCISIONS.**

1. **Traduit à l'affichage, comme D89 et D92.** Le lecteur continue d'écrire
   en français : c'est la donnée que les tests d'`interchange/` relisent (un
   test cherche « Piste AUDIO » dans le rapport) et ce que le terminal imprime
   après un `VSM_IMPORT`. Le volet fait passer chaque ligne par `trPhrase`, et
   la table reçoit un modèle par forme de phrase du lecteur : les noms de
   pistes, les formats et les versions passent intacts, les nombres ne se
   reconnaissent que comme nombres (`%#N`).
2. **Chaque client a son nom, et c'est lui qui refait son rapport.**
   `ClientDuRapport::autre` disparaît au profit de `import`, `echecImport` et
   `reconstruction`. L'application garde le rapport d'import (la structure, pas
   le texte) et le message d'échec en français ; le rapport de reconstruction
   se relit sur `rapport.json`. La bascule refait le dernier client, volet
   ouvert ou fermé — la règle de D84 : le volet ne reçoit que des textes déjà
   traduits, un texte assemblé ne se re-traduit pas.
3. **Les messages de `Inflate` sont traduits aussi** : un `.als` corrompu les
   montre tels quels dans l'échec, et ce sont seize phrases fixes.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (11/09/2026).** Banc : les trois
> projets d'épreuve des tests d'`interchange/` écrits sur disque (le `.als`
> gzippé, le `.flp` octet pour octet, l'archive Cubase), plus un `.cpr`,
> ouverts par `VSM_IMPORT` sous `HOME` isolé, chacun en français et basculé en
> anglais ; témoin : le binaire de D92.
>
> 1. **En anglais, le rapport d'import est anglais** : les lignes identiques
>    FR / EN tombent à celles qui n'ont rien à traduire (lignes vides, un nom
>    propre de format ou de version), chacune nommée ; aucun mot français ne
>    reste dans les lignes changées, hors données (noms de pistes, de canaux).
> 2. **Le français ne change pas d'un caractère** contre le témoin, sur les
>    quatre fichiers.
> 3. **Les nombres sont les mêmes** dans les deux langues, ligne pour ligne.
> 4. **La bascule refait le rapport de reconstruction** : lancé en français,
>    basculé par « English » APRÈS `VSM_RAPPORT`, le volet liste le même texte
>    que D92 lancé en anglais — au caractère près, sur deux projets.
> 5. **Le rapport d'ouverture de D89 ne bouge pas** (même liste que le témoin).

> **D93 EST FAITE (11/09/2026).** Témoin : le binaire de D92, recompilé depuis
> le même arbre, les changements de D93 mis de côté (`git stash`) le temps de
> la compilation — le témoin est du même code que ce qu'il témoigne. `HOME`
> isolé ; le fichier de préférences de l'utilisateur comparé par `cmp` à une
> copie prise juste avant la série : inchangé.
>
> | fichier | lignes | identiques FR / EN, témoin | D93 | ce qui reste identique |
> |---|---|---|---|---|
> | `projet.als` (Ableton Live 11.3.4) | 14 | 14 / 14 | **4 / 14** | le nom et la version (« Ableton Live 11.3.4 »), 3 lignes vides |
> | `projet.flp` (FL Studio 20.8) | 13 | 13 / 13 | **4 / 13** | « FL Studio — 20.8.3.2304 », 3 lignes vides |
> | `archive.xml` (Cubase) | 12 | 12 / 12 | **3 / 12** | 3 lignes vides |
> | `projet.cpr` (refusé) | 4 | 4 / 4 | **2 / 4** | 2 lignes vides |
>
> **Les cinq attendus :**
>
> 1. **Tenu.** En anglais, 43 lignes sur 43 → **13**, toutes des noms propres
>    de format ou de version, ou des lignes vides. La recherche de mots
>    français dans les lignes anglaises changées ne trouve rien ; restent les
>    DONNÉES, entre guillemets anglais : « Voix enregistree », « Basse »,
>    « Lead Été », les noms de pistes et de canaux du projet d'origine.
> 2. **Tenu.** Le français est identique au témoin, au caractère près, sur les
>    quatre fichiers.
> 3. **Tenu.** 43 lignes sur 43 portent la même suite de nombres en français et
>    en anglais.
> 4. **Tenu.** Lancé en français, le rapport ouvert par le menu (« Voir le rapport de
>    reconstruction »), puis basculé par « English » : le volet liste le même
>    texte que D92 lancé en anglais, **au caractère près** — 12 lignes sur
>    `children-dream-v7`, 9 sur `children-dream-v12`. Le rapport de
>    reconstruction en français est identique à D92 sur les deux.
> 5. **Tenu.** Le rapport d'ouverture de D89 est identique au témoin, au
>    caractère près, sur les deux projets et dans les deux langues —
>    4 lancements sur 4, 6 lignes chacun (« Project opened, with
>    reservations » en anglais).
>
> **L'IMAGE.** L'autoportrait du `.als` basculé en anglais montre le volet
> entier en anglais, les pertes en ambre comme en français, les lignes longues
> repliées avec leur retrait. Il montre aussi, derrière le voile, un reste
> qu'aucune liste ne nommait : le rack affiche « (aucun instrument assigné) »
> (`SynthRackComponent.cpp:69`, écrit sans `tr()`).
>
> **CE QUI RESTE D'A9.** Le rack sans instrument (trouvé par cette image) ; les
> réserves d'un preset appliqué à la main ; le message d'import audio ; les
> raisons pour lesquelles la reconstruction est indisponible
> (`ReconstructionChain`) ; les descriptions des machines et la source « Parc
> VSM » du navigateur ; « RÉGLAGES », titre de section des façades ; deux
> textes aux retours à la ligne forcés (D91). Le terminal d'un `VSM_IMPORT`
> reste en français, et c'est voulu : c'est la donnée, celle que relisent les
> tests d'`interchange/`.
>
> Table à **866** paires, **72** modèles de phrases.
>
> Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels, relancés
> après D93 — tout vert. Python : 172 tests, ruff et mypy verts ce matin sur
> le même code Python ; D93 ne touche que `app/Source/` et la documentation. La
> suite Python relancée à côté du corpus A6 a été ARRÊTÉE faute de mémoire —
> le corpus a survécu (piège ajouté à CLAUDE.md).

### Phase D94 — A9 compté pour de bon : 396 chaînes à l'écran sans tr(), et les panneaux toujours visibles traduits (11/09/2026)

**CE QUE D93 A LAISSÉ CROIRE.** La ligne A9 d'`INDEX.md` nommait une dizaine de
restes (le rack vide, « RÉGLAGES », les descriptions des machines…). Un
inventaire du code en trouve **396** : des littéraux français qui atteignent
l'écran sans passer par `tr()` — infobulles de la barre de transport, de la
liste des pistes, du mixeur, de la barre du piano roll, messages d'état,
boîtes de `MainComponent` (257 à lui seul). La liste des restes était une
liste de ce qu'on avait VU, pas de ce qui reste.

**DEUX MESURES, PARCE QU'AUCUNE NE VOIT TOUT.**

- **`tools/inventaire_langue.py`** lit le CODE. Il classe chaque littéral
  français : ÉCRAN (le chiffre d'A9), SANS_PAIRE (passé à `tr()` sans clé dans
  la table — il reste français en anglais aussi sûrement qu'un littéral nu :
  **2**, « Écoute A/B : … »), TERMINAL (94, voulu), TABLE (177, traduits par
  une variable), COMMANDE (25 jetons de banc). Les règles sont écrites en tête
  du fichier, avant de compter. **Son angle mort** : un français sans accent ni
  mot courant (« Annuler (Ctrl+Z) ») lui échappe.
- **`VSM_TEXTES_LISTE=1`** lit ce qui S'AFFICHE : chaque libellé, bouton,
  liste et infobulle des composants visibles, dans la langue courante — une
  infobulle ne se photographie pas. **Son angle mort** : une boîte fermée, un
  panneau caché, ce que `paint()` dessine sans composant.

**LE TÉMOIN (même binaire que la mesure, D94 sans ses traductions).** Sur
`children-dream-v7`, **296 textes** affichés ; **146** textes distincts
identiques entre un démarrage français et un démarrage anglais, dont **43**
en français à coup sûr : 39 infobulles et 3 noms de machines dans les listes
(« Piano (cordes frappées) »…), plus « Écoute A/B ». La bascule en anglais rend
la même liste que le démarrage anglais — parce que ce qui n'est pas traduit
reste français des deux côtés.

**LE LOT DE D94 : LES PANNEAUX TOUJOURS VISIBLES.** Barre de transport, liste
des pistes, barre et corps du piano roll, mixeur, voies de CC, de tempo, de
vélocité et d'automation, arrangement, liste d'événements, chaîne d'effets,
rack, spectre, séquenceur pas à pas, façade — seize fichiers, **environ 120**
des 396. `MainComponent` (257), les messages des couches audio et de
reconstruction, et les noms des machines (qui viennent du registre du moteur)
viennent ensuite, chacun sa phase. **La décision sur les infobulles posées
dans un constructeur** : elles passent dans le `retraduire()` de leur
composant — sinon la bascule en direct les laisserait dans la langue du
démarrage, ce que D78 interdit.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (11/09/2026).**
>
> 1. **L'inventaire** : ÉCRAN et SANS_PAIRE à **zéro** dans les seize fichiers
>    du lot, ou chaque reste nommé avec sa raison ; le total baisse d'autant.
> 2. **L'affichage** : parmi les textes identiques en français et en anglais,
>    plus aucune infobulle ni aucun libellé français des panneaux du lot ; ce
>    qui reste identique est nommé (données, noms de machines, mots neutres).
> 3. **Le français ne change pas d'un caractère** : la liste française est
>    identique à celle du témoin, et la capture française l'est au pixel près.
> 4. **La bascule rend le démarrage** : la liste basculée en anglais est
>    identique à celle d'un démarrage en anglais.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Banc : `children-dream-v7`,
HOME isolé, préférences de l'utilisateur intactes (`cmp`, copie prise juste
avant chaque série) ; liste comparée au témoin de listage, image comparée à
**D93 recompilé depuis le même arbre, D94 mis de côté par `git stash`**, pris
dans les mêmes conditions (voir plus bas pourquoi il a fallu le refaire).

1. **L'inventaire — tenu.** ÉCRAN **396 → 273** (−123), SANS_PAIRE **2 → 0**,
   TABLE 177 → 202. Dans les seize fichiers du lot, **un** reste, nommé :
   « Arrangement / règle » (`ArrangementComponent::menusPourCapture`) est le
   nom que `VSM_MENU_LISTE` écrit sur la sortie d'erreur — un libellé de banc,
   rendu par une fonction et écrit par une autre, que l'inventaire ne peut pas
   relier à son `fputs`. Les réserves de la chaîne d'effets s'écrivent
   désormais par leur modèle (`kModeles`, trois de plus) et comptent en TABLE.
   Les 273 : `MainComponent` 257, les couches audio (7), les plugins (4), la
   reconstruction (4) — chacun sa phase.
2. **L'affichage — tenu.** Textes distincts identiques en français et en
   anglais : **146 → 103**. Les 43 traduits : **42 infobulles** (barre de
   transport, barre du piano roll, liste des pistes, mixeur) et l'unité de
   transposition, « 0 dt » devenu « 0 st ». Aucun texte n'est devenu
   identique. Les 103 qui restent : les mots du magnétophone (Play, Stop, Rec,
   Tap, M / S / R / W), les sérigraphies des façades (BASS, DAMPER… — la
   décision de D73), les noms des pistes, les nombres et leurs unités — et
   **quatre noms de machines en français** dans les listes (« Piano (cordes
   frappées) », « String (corde pincée / frottée) », « Wind (anche et
   lèvres) », « Drums (batterie acoustique) »), qui viennent du registre du
   moteur : la phase suivante.
3. **Le français — tenu.** La liste française est **identique au témoin**,
   296 textes sur 296. L'image française est identique à D93 **à 42 pixels
   près, tous dans la charge du processeur** (« CPU 0.2% » contre
   « CPU 0.1% ») : une mesure qui bouge d'un lancement à l'autre, pas un
   texte. L'image anglaise diffère de D93 de 924 pixels, tous dans « 0 dt » →
   « 0 st » sur les six tranches.
4. **La bascule — tenue.** La liste basculée en anglais est identique à celle
   du démarrage en anglais, 296 sur 296, et l'image à **0 pixel**.

La table passe à **1 019** paires (+153) et **75** modèles de phrases (+3) ;
`trGeste` reconnaît « Durée x%1 » et « Vélocité x%1 », les deux gestes du
piano roll dont le nom porte un nombre.

**CE QUE LA MESURE A TROUVÉ EN CHEMIN.**

- **Une régression, attrapée par la liste.** La première passe listait deux
  textes de trop en français : « SANS SON » et son infobulle, « Raison donnée
  par le système : ? », sur une barre qui AVAIT du son. `poserTexteSansSon()`
  se fiait à `isVisible()` — or la boucle du constructeur de la barre de
  transport rend visibles le témoin sans son et le compte de craquements
  juste après les avoir cachés. La fonction se règle désormais sur l'état
  (`derniereRaisonSon_`, « ? » = jamais posée). L'image ne le montrait pas ;
  la liste, si — c'est pour cela qu'il faut les deux.
- **Un défaut plus ancien — et une piste qui NE TIENT PAS (rectifié le
  11/09).** La première version de ce paragraphe, poussée avec D94, disait
  ces deux témoins PLACÉS par `disposer()`, 216 px pris à la barre d'A6 —
  lu dans le code, pas mesuré. **L'image le dément.** Calculée à cette
  largeur (1 248 px utiles), la première rangée AVEC les deux témoins finit
  « 4/4 » à 1 001, pose « SANS SON » de 1 009 à 1 099 et les craquements
  jusqu'à 1 217, et renvoie « CPU » à la ligne ; SANS eux, « CPU » va de
  1 009 à 1 085, « Exporter MIDI… » tient sur la rangée et seul « Ouvrir
  MIDI… » passe à la suivante. Les quatre captures de D94 (D93 et D94,
  français et anglais) montrent la seconde : « CPU » à 8 px de « 4/4 ».
  Visibles pour la liste, les deux témoins n'étaient pas dans la dernière
  disposition. La piste d'A6 est retirée ; reste un défaut de code — un
  `setVisible(false)` défait trois lignes plus bas — sans effet mesuré à
  l'écran.
- **Un écran verrouillé fait lister zéro texte.** La série de ce matin a
  tourné pendant que la session était verrouillée (`LockedHint=yes`) : la
  liste a rendu **0 texte** sur trois lancements, pendant que l'autoportrait
  dessinait la fenêtre entière. `isShowing()` exige que la fenêtre ne soit
  pas minimisée, et un écran verrouillé la fait passer pour telle. Le listage
  lit maintenant `isVisible()` en descendant depuis la racine — la même liste
  quand la fenêtre est à l'écran — et dit son compte (`VSM_TEXTES : N`, avec
  « fenêtre non affichée » quand c'est le cas). La fenêtre, elle, y fait
  1264×784 au lieu de 1264×742 : l'image ne se compare qu'à un témoin pris
  dans les mêmes conditions, d'où D93 recompilé.
- **L'angle mort de l'inventaire, mesuré.** Il ne lit que les `.cpp`.
  `--entetes` applique les mêmes règles aux en-têtes : **20** chaînes avant
  ce lot, **19** après — toutes des noms de pièces de batterie
  (`DrumVoiceNames.h`, « charleston fermé »…) ; la vingtième, l'infobulle du
  coût de calcul d'une tranche (`MixerComponent.h`), est traduite ici. Le
  chiffre d'A9 reste celui des `.cpp`, pour que les phases se comparent.
- **Deux pièges de construction** (écrits dans `CLAUDE.md`) : un `using
  vsm::app::ui::tr` local à UNE fonction ne sert pas aux autres (deux
  fichiers ne compilaient pas) ; et `cmake --build … > log ; grep error log`
  rend le code du grep, 0 quand il A TROUVÉ des erreurs. Trois compilations
  lancées en arrière-plan ont été arrêtées faute de mémoire — une compilation
  Gradle tournait à côté — ; détachées par `setsid`, elles sont passées.

Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels — verts ;
`vsm-ui-preview`, qui compile `Langue.cpp` et le spectre, construit. Aucun
code Python de la chaîne n'a changé ; `tools/inventaire_langue.py` passe ruff
et mypy.

### Phase D95 — A9 : les boîtes du montage (11/09/2026)

**LE LOT.** Des 257 chaînes de `MainComponent` que D94 laisse, celles des
boîtes qu'on rencontre EN MONTANT : les refus du montage, posés par le
constructeur (piste verrouillée, dans l'arrangement et dans le piano roll ;
jonction refusée ; changement de piste refusé ; transposition qui pousse des
notes hors de 0..127 ; tempo d'une boucle à adopter), et les actions d'un
clip audio (découper aux transitoires, rogner au son, transcrire en MIDI),
avec la création d'un clip. **47** chaînes à l'inventaire. Les autres thèmes
— l'enregistrement, les plugins, les fichiers du projet, les opérations de
piste, le navigateur — viennent ensuite, chacun sa phase.

**CE QU'IL FAUT D'ABORD DONNER À LA MESURE : LIRE UNE BOÎTE.** D91 les
photographiait ; une photo ne se compare pas mot à mot, et D94 a montré
qu'une liste attrape ce qu'une image ne montre pas. Or une boîte n'est pas
un enfant de la fenêtre : c'est une AUTRE fenêtre, ouverte après le geste
qui la demande — `VSM_TEXTES_LISTE`, qui lit la fenêtre au démarrage, ne la
voit pas. JUCE ne rend pas le message d'une `AlertWindow` (`text` est
privé), mais il le pose dans un libellé ACCESSIBLE, enfant visible et
transparent de la boîte : « titre. message » (`juce_AlertWindow.cpp`,
`setMessage`). La liste lit donc désormais, au moment de la photo, les
textes de chaque AUTRE fenêtre visible — boîtes et panneaux flottants — avec
la même descente que la fenêtre principale. Le témoin est ce binaire-là,
sans les traductions.

**ET LA COURSE DE D72, REVENUE EN ENTIER.** Premier témoin : **0 boîte** sur
les six lancements — et 0 aussi pour « Aller à la mesure… », que D91 avait
photographiée. La liste, élargie aux fenêtres cachées et au compte des
composants modaux, a dit pourquoi : deux secondes après le geste, il y a
**six** fenêtres de premier niveau (la principale et cinq panneaux cachés) et
**aucun** composant modal — la boîte n'est pas cachée, elle n'est plus là.
C'est la course que D72 a mesurée (une photo sur sept au mieux), sous un
écran verrouillé cette fois : zéro sur sept. D72 l'a réglée pour son rapport
en le sortant de la boîte ; ici, **la boîte se lit au moment où elle est
demandée** : les appels du lot passent par `montrerBoite()`, qui écrit
`VSM_BOITE : titre : message` sur la sortie d'erreur, dans la langue
affichée, puis ouvre la boîte — le même principe que les réserves d'effet
(D71) : un banc sans souris doit pouvoir relire la phrase. La boîte du tempo
d'une boucle, construite à la main et que le banc n'ouvre pas, reste hors de
ce chemin : elle se vérifie dans le code et la table. Le témoin est refait
avec `montrerBoite()`, toujours sans les traductions.

**LE BANC.** Une copie fraîche de `docs/examples/demo-project` par
lancement (l'import COPIE le fichier dans le dossier du projet, et
`reconstruction/` ne se touche pas), `VSM_IMPORT_AUDIO` d'un échantillon de
`children-dream-v10`, puis `VSM_MENU_CONTEXTE=clip-audio:…` pour chacune des
trois actions d'un clip audio ; français, puis anglais ; témoin, puis D95.
HOME isolé, préférences de l'utilisateur vérifiées par `cmp`.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (11/09/2026).**
>
> 1. **L'inventaire** : ÉCRAN et SANS_PAIRE à **zéro** dans les rappels du
>    montage et les quatre fonctions du lot, ou chaque reste nommé avec sa
>    raison ; le total baisse d'autant.
> 2. **Les trois boîtes du banc s'ouvrent avec le témoin et se LISENT** :
>    titre et message, dans les deux langues.
> 3. **En anglais, plus un mot français dans ces trois boîtes**, hors les
>    données (noms de pistes et de fichiers, chemins, nombres).
> 4. **Le français ne change pas** : le texte des trois boîtes et la liste de
>    la fenêtre principale sont identiques à ceux du témoin.
> 5. **Les boîtes que le banc n'ouvre pas sont nommées**, avec la raison ;
>    leur texte se vérifie dans le code et dans la table.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Banc : copie de
`demo-project`, `percussion.wav` importé, les trois actions du clip, français
puis anglais, témoin (D95 sans ses traductions, `montrerBoite()` en place)
puis D95 ; HOME isolé, préférences de l'utilisateur intactes (`cmp`).

1. **L'inventaire — tenu.** ÉCRAN **273 → 226**, soit **−47, exactement le
   lot** ; dans les rappels du montage et les quatre fonctions, **0** ÉCRAN et
   **0** SANS_PAIRE. TABLE 202 → 213 ; TERMINAL 94 → 98, dont les lignes de
   compte des outils de banc (`VSM_TEXTES`, `VSM_FENETRES`). Les comptes
   rendus qui partent AUSSI au terminal — découper, transcrire — restent
   français, écrits par leurs modèles (`kModeles`, +9), et la boîte les
   traduit à l'affichage par `trPhrase` : la sortie d'erreur et les bancs qui
   la relisent ne changent pas.
2. **Les boîtes se lisent — tenu.** Chaque lancement demande sa boîte (1 sur
   1, six fois), lue par `VSM_BOITE` dans les deux langues : « Choisissez
   d'abord un clip audio dans l'arrangement. » (le banc ne choisit pas de
   clip), « Ce clip commence et finit déjà sur le son : rien à rogner. »,
   « Choisissez d'abord un clip d'une piste AUDIO : une piste MIDI porte déjà
   ses notes. »
3. **L'anglais — tenu.** Aucun mot français dans les trois boîtes :
   « First choose an audio clip in the arrangement. », « This clip already
   starts and ends on sound: nothing to trim. », « First choose a clip on an
   AUDIO track: a MIDI track already carries its notes. »
4. **Le français — tenu.** Les trois boîtes sont identiques au témoin, mot
   pour mot, et la liste de la fenêtre principale aussi : 182 textes sur 182.
5. **Les boîtes que le banc n'ouvre pas**, et pourquoi : la transposition hors
   plage (un réglage du mixeur qui pousse des notes au-delà de 0..127), la
   piste verrouillée et le changement de piste refusé (des glissers), la
   jonction refusée (deux clips choisis), le tempo d'une boucle (une boucle
   détectée à l'import, et une boîte construite à la main, hors de
   `montrerBoite()`), les autres branches des trois actions (un clip CHOISI,
   que le banc ne sait pas poser ; la chaîne d'analyse en marche), et
   « Créer un clip » (un groupe, ou un clip déjà là). Leur texte se lit dans
   le code et dans la table : 0 ÉCRAN, 0 SANS_PAIRE. **La raison** que donne
   `ReconstructionChain` quand la chaîne est indisponible reste française
   dans « The analysis chain is not available: … » : elle vient de la couche
   de reconstruction, et c'est sa phase.

**CE QUE LA MESURE A MONTRÉ EN CHEMIN : L'ÉCRAN, ENCORE.** Le témoin a
tourné sous un écran verrouillé : **0 boîte sur 7**. La série de D95 a tourné
écran déverrouillé — l'utilisateur était revenu : les six boîtes sont des
fenêtres AFFICHÉES, lues par la liste des fenêtres **mot pour mot comme
`VSM_BOITE`** (« titre. message », puis « OK ») et photographiées par
`VSM_CAPTURE_PANNEAUX` — l'anglais et le français de « Transcrire en MIDI »
regardés côte à côte, sans rien de coupé. Les deux lectures concordent ;
`VSM_BOITE` est la seule qui ne dépend pas de l'écran.

La table passe à **1 056** paires (+37) et **84** modèles de phrases (+9).
Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels — verts ;
`vsm-ui-preview`, qui compile `Langue.cpp`, construit. Aucun code Python n'a
changé.

### Phase D96 — A9 : les opérations de piste (11/09/2026)

**L'ORDRE, TRANCHÉ ICI.** D95 laissait l'ordre des thèmes ouvert, et
l'enregistrement venait en tête. Il passe après, et c'est la mesure qui en
décide : le lancement d'une prise n'est pas dans la barre de menus,
« Récupérer ce qui vient d'être joué » y est grisé tant que rien n'a été
joué, et la mesure de latence ENVOIE un balayage dans les haut-parleurs —
deux ou trois de ses quinze boîtes s'ouvriraient au banc, et l'une ferait du
bruit chez l'utilisateur, qui travaille à côté. Les opérations de piste se
commandent par le menu Piste et répondent par des boîtes déterministes :
elles passent d'abord. L'enregistrement aura son banc à lui.

**LE LOT, ET CE QU'IL N'EST PAS.** L'inventaire en compte 35 ; la lecture du
code en retranche deux espèces. **Cinq opérations n'écrivent QUE sur la
sortie d'erreur** — désactiver une piste, copier et coller une chaîne,
réduire l'automation, reporter les effets MIDI : leur message se construit
dans une variable, passée à `fputs` une instruction plus loin, et
l'inventaire, qui ne lit que l'instruction où la chaîne est écrite, les
range à l'ÉCRAN. Elles restent françaises, comme tout le terminal. Et
**quatre chaînes sont des données** : les chemins `gel/piste-`,
`audio/report-piste-`, `audio/report-selection-piste-`, et le nom par défaut
« Piste N » d'une piste ajoutée, écrit dans le projet. Restent les boîtes :
geler, reporter la piste, éclater par hauteur, publier les sorties,
reporter la sélection — **neuf appels**, qui passent par `montrerBoite()`
(D95).

**UNE COMMANDE, DEUX NOMS ANGLAIS.** Le menu dit « Split by pitch
(4 tracks) » ; la boîte qui répond, et le nom du geste dans l'historique,
disaient « Explode by pitch ». **La décision : le mot du MENU, « Split by
pitch », partout** — c'est celui qu'on vient de cliquer, et une boîte qui
répond sous un autre nom fait douter qu'elle réponde à ce geste-là.

**LE BANC.** Trois gestes, par la barre de menus : éclater par hauteur, sur
une copie de `demo-project` ; geler, et reporter, une piste MIDI ajoutée à un
projet NEUF — jamais enregistré, c'est ce qui ouvre leur boîte sans rendre
un son. Français puis anglais, libellés pris dans la langue affichée ;
témoin (D96 sans ses traductions, `montrerBoite()` en place), puis D96. HOME
isolé, préférences de l'utilisateur vérifiées par `cmp`.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (11/09/2026).**
>
> 1. **L'inventaire** : dans les cinq fonctions à boîtes, ÉCRAN et SANS_PAIRE
>    à **zéro** ; les messages du terminal et les chemins nommés comme tels.
> 2. **Les trois boîtes du banc s'ouvrent avec le témoin et se lisent**
>    (`VSM_BOITE`) dans les deux langues.
> 3. **En anglais, plus un mot français dans ces trois boîtes**, hors les
>    données ; « Split by pitch » là où « Explode by pitch » était.
> 4. **Le français ne change pas** : les trois boîtes et la liste de la
>    fenêtre principale sont identiques à celles du témoin.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Banc : trois gestes par la
barre de menus, français puis anglais ; témoin (D96 sans ses traductions,
`montrerBoite()` en place) puis D96 ; HOME isolé, préférences de
l'utilisateur intactes (`cmp`).

1. **L'inventaire — tenu, et un chiffre qu'il faut corriger.** ÉCRAN
   **226 → 206**. Dans les cinq fonctions à boîtes : **0** SANS_PAIRE et un
   seul ÉCRAN, `audio/report-selection-piste-`, un chemin. Le thème garde 18
   chaînes, nommées : 4 données (le nom « Piste N », trois chemins) et 14
   messages du terminal. **Mais −20 n'est pas 20 chaînes traduites : c'en est
   17.** « Projet jamais enregistré », devenu une clé de la table, est compté
   TABLE aux TROIS endroits hors du lot qui l'écrivent sans `tr()` — poser un
   échantillon, lancer une prise, importer un fichier audio (celui-là en
   `é`) : ils restent français en anglais. La règle TABLE de
   l'inventaire (« une clé de la table est traduite ailleurs, par une
   variable ») est trop large pour un titre de boîte ; ces trois-là sont
   nommés ici et viendront avec leur phase.
2. **Les boîtes se lisent — tenu.** Chaque geste demande sa boîte (6 sur 6) :
   « 3 piste(s) créée(s) — la hauteur la plus grave reste sur la piste
   d'origine. », « Projet jamais enregistré : Un gel est un FICHIER… »,
   « Projet jamais enregistré : Un report est un FICHIER… ».
3. **L'anglais — tenu.** Aucun mot français : « Split by pitch : 3 track(s)
   created — the lowest pitch stays on the original track. », « Project never
   saved : A freeze is a FILE… », « … A bounce is a FILE… ». **« Split by
   pitch » partout où « Explode by pitch » était** — le titre de la boîte et
   le nom du geste dans l'historique disent désormais le mot du menu.
4. **Le français — tenu.** Les trois boîtes identiques au témoin, mot pour
   mot, et la liste de la fenêtre principale aussi : 253 textes (éclater) et
   160 (geler, reporter), identiques.

**Les boîtes que le banc n'ouvre pas** : publier les sorties (grisé — la
machine du projet n'en a qu'une), reporter la sélection (grisé sans
sélection), l'échec d'un gel (un rendu qui échoue), le compte rendu d'un
report réussi (il rend du son) ; leur texte se lit dans le code et la table.
Écran déverrouillé : les six boîtes sont aussi photographiées.

La table passe à **1 070** paires (+14) ; « Éclater par hauteur » y change
d'anglais.
Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels — verts ;
`vsm-ui-preview` construit. La boîte anglaise « Split by pitch » regardée :
rien de coupé. Aucun code Python n'a changé.

### Phase D97 — A9 : les boîtes du projet (11/09/2026)

**LE LOT.** Ce que le menu Fichier dit du PROJET : ses statistiques, le
modèle de projet, un dossier qui ne s'ouvre pas, un enregistrement
impossible ou incomplet, l'import et l'export MIDI (ce que le `.mid` ne
porte pas, les rampes de tempo rendues en paliers), l'audio qui ne se charge
pas, les locateurs absents, et les titres des sélecteurs de fichier. Le
groove (extraire, enregistrer, charger, appliquer) aura sa phase ;
l'enregistrement aussi, avec un banc qui ne fasse pas de bruit (D96).

**LES STATISTIQUES SE TRADUISENT À LA SOURCE — ET CE N'EST PAS LA RÈGLE DE
D95.** D95 garde en français les comptes rendus qui partent AUSSI au
terminal, parce que des bancs les relisent. Le texte des statistiques part
aussi au terminal, mais personne ne le relit : `projectStatisticsText()`
n'a qu'un appel, la boîte, et aucun banc ni test ne le lit. Il se traduit
donc là où il s'écrit, par des modèles (« %1 courbe(s), %2 point(s) »), et
le terminal reçoit le texte de la boîte. La règle de D95 tient à la raison
qu'elle donne, pas à la sortie d'erreur elle-même.

**CE QUE L'INVENTAIRE COMPTE À TORT.** Les deux lignes du rapport
d'ouverture que ce lot touche — « Preset pour une piste inexistante (N) :
ignoré », « N note(s) signalée(s) comme douteuses… » — sont traduites à
l'affichage par leurs modèles depuis D89 ; l'inventaire les range à l'écran
parce que le code les écrit en morceaux. Elles s'écriront par leur modèle,
comme les comptes rendus de D95 : même français, et l'inventaire les voit.

**LE BANC.** Trois boîtes, sur une copie de `demo-project` : les
statistiques et le modèle de projet, par le menu Fichier ; « Locateurs »,
par son raccourci (`ctrl + shift + I`, via `VSM_TOUCHE`) — l'entrée du menu
est grisée sans boucle, mais le clavier passe par la table des raccourcis et
atteint la commande. **Corrigé avant la mesure :** la première version de ce
paragraphe visait « Projet illisible », en ouvrant un dossier qui n'est pas
un projet ; or `VSM_PROJET` vérifie le dossier AVANT
`loadProjectBundleFromFolder()`, seule à ouvrir cette boîte, et se plaint au
terminal. Elle rejoint les boîtes que le banc n'ouvre pas, et la raison d'un
projet illisible (attendu 3) se lit dans le code et les modèles. Le reste du
lot passe par un sélecteur de fichier ou une entrée grisée sur ce projet :
il se lit dans le code et la table. Français puis anglais ; témoin (D97 sans ses traductions,
`montrerBoite()` en place), puis D97 ; HOME isolé, préférences de
l'utilisateur vérifiées par `cmp`.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (11/09/2026).**
>
> 1. **L'inventaire** : dans les fonctions du lot, ÉCRAN et SANS_PAIRE à
>    **zéro**, ou chaque reste nommé avec sa raison.
> 2. **Les trois boîtes du banc s'ouvrent avec le témoin et se lisent**
>    (`VSM_BOITE`) dans les deux langues.
> 3. **En anglais, plus un mot français dans ces trois boîtes**, hors les
>    données ; la raison d'un projet illisible, qui vient d'`interchange/`,
>    est traduite par un modèle, ou nommée.
> 4. **Le français ne change pas** : les trois boîtes et la liste de la
>    fenêtre principale sont identiques à celles du témoin.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Banc : trois boîtes sur
une copie de `demo-project`, français puis anglais ; témoin (D97 sans ses
traductions, `montrerBoite()` en place) puis D97 ; HOME isolé, préférences
de l'utilisateur intactes (`cmp`).

1. **L'inventaire — tenu, et cette fois le chiffre est juste.** ÉCRAN
   **206 → 164** (−42) ; dans les quatorze fonctions du lot, **0** ÉCRAN et
   **0** SANS_PAIRE. La leçon de D96 vérifiée avant d'écrire : aucune des 46
   clés neuves n'est un littéral écrit ailleurs sans `tr()` — pas de faux
   TABLE, les 42 sont 42 chaînes traduites. Les deux lignes du rapport
   d'ouverture s'écrivent par leur modèle de D89 : même français, et
   l'inventaire les voit.
2. **Les boîtes se lisent — tenu.** Chaque geste demande sa boîte (6 sur 6),
   « Locateurs » compris : le raccourci l'atteint alors que le menu est
   grisé.
3. **L'anglais — tenu.** Aucun mot français : « Project statistics : Tracks :
   2  (2 MIDI, 0 audio, 0 group(s), 0 folder(s)) / … », « The current project
   has become the template: File ▸ New from template will open it, without a
   path, every time. », « Locators : Set the locators first: the loop region
   is the range to insert or delete. » Les menus cités le sont sous leur nom
   anglais (« File ▸ New from template », « Track ▸ Track MIDI effects »).
4. **Le français — tenu.** Les trois boîtes identiques au témoin, mot pour
   mot, et la liste de la fenêtre principale : 175 textes sur 175.

**Les boîtes que le banc n'ouvre pas** : « Projet illisible » (voir le banc),
l'enregistrement impossible ou incomplet, l'import MIDI et son erreur,
l'export MIDI — ce que le `.mid` ne porte pas, les rampes en paliers, son
erreur — et l'audio qui ne se charge pas, derrière un sélecteur de fichier
ou un fichier manquant ; les titres des quatre sélecteurs et celui de la
fenêtre d'un projet neuf depuis le modèle. Leur texte se lit dans le code et
la table. **Restent des données** : la raison d'un enregistrement ou d'une
ouverture qui échoue vient d'`interchange/` et passe par `trPhrase` (ses
modèles la traduisent quand ils la connaissent), le message d'une exception
d'import ou d'export MIDI, les noms de fichiers et de pistes.

La table passe à **1 116** paires (+46).
Tests : 330 core, 1 291 audio, 297 interchange, 25 clap, 11 panels — verts ;
`vsm-ui-preview` construit. La boîte anglaise des statistiques regardée :
quinze lignes, rien de coupé. Aucun code Python n'a changé.

### Phase D98 — A9 : le groove et les presets de piste (11/09/2026)

**LE LOT.** Les boîtes du groove — l'extraire d'une piste, l'appliquer à
une sélection, l'enregistrer, le charger — et celles des presets de piste —
l'appliquer (machine absente, réserves, fichier illisible), l'écrire. Une
quinzaine de chaînes. **Le navigateur attend sa phase**, et c'est la mesure
qui le décide : ses boîtes (piste déjà occupée, échantillon copié dans le
projet, preset d'effet indisponible…) ne s'ouvrent qu'à un GLISSER depuis
le panneau, et aucune commande du banc n'en fait un. Il lui faudra la
sienne, comme D91 a donné `VSM_MENU_CONTEXTE` aux menus du clic droit.

**LE BANC.** Trois cas : extraire le groove d'une piste de `demo-project` ;
l'extraire puis l'ENREGISTRER — l'écriture ne passe pas par un sélecteur,
elle va dans le dossier `grooves/` du projet, ici la copie du banc ; et
l'extraire d'une piste MIDI neuve, sans note. Appliquer un groove demande
une sélection de notes dans le piano roll, que le banc ne sait pas poser ;
les presets de piste n'ouvrent de boîte qu'en cas d'échec ou de réserve :
ces boîtes-là se lisent dans le code et la table. Français puis anglais ;
témoin (D98 sans ses traductions, `montrerBoite()` en place), puis D98 ;
HOME isolé, préférences de l'utilisateur vérifiées par `cmp`.

> **CE QUI EST ATTENDU, ÉCRIT AVANT LA MESURE (11/09/2026).**
>
> 1. **L'inventaire** : dans les fonctions du lot, ÉCRAN et SANS_PAIRE à
>    **zéro**, ou chaque reste nommé ; et, leçon de D96, aucune clé neuve
>    qui ferait passer pour traduit un littéral écrit ailleurs sans `tr()`.
> 2. **Les boîtes du banc s'ouvrent avec le témoin et se lisent**
>    (`VSM_BOITE`) dans les deux langues.
> 3. **En anglais, plus un mot français dans ces boîtes**, hors les données
>    (le nom du groove, qui est celui de la piste ; les chemins).
> 4. **Le français ne change pas** : les boîtes et la liste de la fenêtre
>    principale sont identiques à celles du témoin.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Banc : trois cas par la
barre de menus, français puis anglais ; témoin (D98 sans ses traductions,
`montrerBoite()` en place) puis D98 ; HOME isolé, préférences de
l'utilisateur intactes (`cmp`).

1. **L'inventaire — tenu.** ÉCRAN **164 → 147** (−17) ; dans les sept
   fonctions du lot, **0** ÉCRAN et **0** SANS_PAIRE ; aucune des 12 clés
   neuves n'est un littéral écrit ailleurs sans `tr()` — les 17 sont 17
   chaînes traduites. TABLE baisse de 4 (218 → 214), et c'est attendu :
   trois titres du lot étaient déjà des clés, écrits quatre fois sans passer
   par `tr()` (« Appliquer le groove », « Appliquer un preset de piste »,
   « Enregistrer la piste comme preset » deux fois) ; ils y passent, et
   l'inventaire ne les compte plus. TERMINAL (98) et COMMANDE (25) ne
   bougent pas.
2. **Les boîtes se lisent — tenu.** Chaque cas demande ses boîtes, dans les
   deux langues : 1, puis **2** pour « extraire puis enregistrer »
   (l'extraction, puis « Écrit dans …/grooves/Acid Bass.groove.json »),
   puis 1 pour la piste sans note.
3. **L'anglais — tenu.** « Groove “Acid Bass”: 8 of 16 steps filled in. The
   steps where the track played nothing will leave the notes alone. »,
   « Written to …/grooves/Acid Bass.groove.json », « This track has no
   notes: there is no timing to extract from it. » Le seul « projet » que
   l'analyse relève est dans le CHEMIN de la copie du banc
   (`…/projet-…/grooves/`) : une donnée.
4. **Le français — tenu.** Les boîtes identiques au témoin, mot pour mot —
   l'étiquette du banc ramenée à une seule forme dans le chemin écrit —, et
   la liste de la fenêtre principale : 175 textes (160 sur le projet neuf).

**Les boîtes que le banc n'ouvre pas** : appliquer un groove (il faut une
sélection de notes), le charger (sélecteur de fichier) et son erreur, les
trois boîtes des presets de piste (échec, machine absente, réserves) et la
confirmation d'un preset écrit par la boîte de nom. Leur texte se lit dans
le code et la table. Les réserves d'un preset et l'erreur d'un groove
illisible viennent d'`interchange/` et passent par `trPhrase`.

La table passe à **1 128** paires (+12). `VintageSynthMidiStudio` et
`vsm-ui-preview` compilent ; suites C++ vertes (330 cœur, 1 291 audio,
297 interchange, 25 CLAP, 11 panneaux).

### Phase D99 — A9 : le navigateur (11/09/2026)

**LE LOT.** Après le groove, le groupe suivant de `MainComponent` : le
navigateur — `refreshBrowser` (les origines des entrées), `applyBrowserItem`
(le double-clic : huit boîtes) et `placeSampleOnTrack` (le dépôt d'un
échantillon : cinq boîtes). Aucune de ces boîtes ne s'ouvrait depuis le banc :
elles demandent un double-clic ou un glisser dans une fenêtre flottante.

**LA COMMANDE DE BANC.** `VSM_NAVIGATEUR=geste:référence[;…]`, geste
`double-clic` ou `depot`. Elle refait la liste du navigateur comme son
ouverture la refait (`refreshBrowser`), y cherche l'entrée dont la référence
est celle donnée — le chemin d'un fichier, l'identifiant d'une machine —, et
appelle la MÊME fonction que le geste : `applyBrowserItem` (le double-clic,
`onApply`) ou `applyBrowserDropAt` au tick 0 (le dépôt sur l'arrangement),
sur la piste choisie. Une référence que la liste ne porte pas est dite sur la
sortie d'erreur, jamais fabriquée : le banc ne doit pas pouvoir faire ce que
l'utilisateur ne peut pas. Elle passe après `VSM_MENU`, qui ajoute la piste
visée.

**LES DÉCISIONS.**

1. **Les origines se traduisent à la source**, dans `refreshBrowser`, pas au
   dessin : le filtre de recherche lit l'origine, et il doit trouver les mots
   qu'on voit. Au changement de langue, `retraduire()` refait la liste si la
   fenêtre du navigateur existe.
2. **Le titre de la fenêtre « Navigateur » reste hors du lot.** `PanelWindow`
   retient sa position sous son titre (`fenetre.<titre>` dans les
   préférences) : traduire le titre ferait perdre à l'utilisateur la position
   qu'il a réglée, au premier changement de langue. Les neuf autres fenêtres
   flottantes sont dans le même cas, et aucune n'a de titre traduit. D100
   sépare la clé du titre, pour toutes à la fois.
3. **« Preset appliqué, avec des reserves » prend son accent** : c'est la
   seule différence française voulue, déclarée ici avant la mesure.
4. **« Projet jamais enregistré » passe par `tr()`** dans
   `placeSampleOnTrack` — l'un des trois faux TABLE nommés par D96. Les deux
   autres (`startRecording`, `importAudioFileOnNewTrack`) restent à leur lot.

**L'ANGLE MORT DE L'INVENTAIRE, vu dans ce lot.** L'inventaire en relève 18
lignes ÉCRAN. Il en manque huit affichages : « Navigateur » (deux titres),
« Preset d'effet illisible », « L'effet « … » n'est pas disponible. »,
« Preset illisible », « Copie impossible », « Parc VSM », « Plugin tiers ».
Aucun ne porte d'accent ni un mot de sa liste. Ils sont traduits avec le lot
et comptés à la main ; l'angle mort sur tout `app/Source` se mesure à part
(D101), avec son hypothèse, pour que la règle de comptage ne change pas au
milieu d'une comparaison.

**ATTENDU, écrit avant la mesure.** Banc : quatre lancements, français puis
anglais, témoin (D99 sans ses traductions : `montrerBoite()` et la commande
de banc en place) puis D99 ; HOME isolé dont la bibliothèque est un dossier du
brouillon (deux échantillons, un WAV cassé, un preset illisible, un preset à
paramètre inconnu, un preset d'effet de type inconnu, un preset d'effet
illisible, un profil).

| lancement | projet | gestes | boîtes |
|---|---|---|---|
| sans-piste | neuf | double-clic `vsm.tb303` | 1 : « Choisissez d'abord une piste. » |
| jamais-enregistre | neuf + piste MIDI | dépôt `bip.wav` | 1 : « Projet jamais enregistré » |
| presets | copie de demo-project | double-clic sur les cinq fichiers de réglage, puis dépôt de `bip.wav` sur la piste 1 (qui a des notes) | 6 : illisible, réserves, effet absent, effet illisible, profil, piste occupée |
| echantillons | copie + piste MIDI | double-clic `bip.wav`, double-clic `bop.wav`, dépôt `casse.wav` | 3 : posé, piste déjà pourvue, échantillon illisible |

1. **L'inventaire** : ÉCRAN **147 → 129** (−18) ; 0 ÉCRAN et 0 SANS_PAIRE
   dans les trois fonctions ; TABLE **214 → 213** (« Projet jamais
   enregistré »). Les libellés d'annulation du lot restent TABLE : ils sont
   traduits à l'affichage par `trGeste`.
2. **Les boîtes se lisent** : 1, 1, 6, 3, dans les deux langues. Si la
   première ne vient pas, c'est qu'un projet neuf a déjà une piste : je le
   dirai, sans changer de cas.
3. **L'anglais** : aucun mot français dans les boîtes, hors des données
   (noms de fichiers, chemins, nom d'un preset). Les messages venus
   d'`interchange/` (erreur de lecture, réserves d'un preset) passent par
   `trPhrase` ; celui qui resterait français faute de modèle est nommé.
4. **Le français** : les boîtes identiques au témoin, sauf l'accent de la
   décision 3 ; la liste de la fenêtre principale identique.
5. **Les origines** : la fenêtre du navigateur, photographiée
   (`VSM_MENU=Navigateur` / `Browser`, `VSM_CAPTURE_PANNEAUX`), dit
   « Bibliothèque / presets » en français et « Library / presets » en
   anglais.

**VU DANS LE TÉMOIN, écrit avant la mesure d'après.** Le témoin a tourné
(8 lancements, préférences intactes) ; deux choses s'y lisent, qui changent
la lecture de l'attendu :

- **`sans-piste` : 0 boîte.** Le cas prévu dans l'attendu 2 : un projet neuf
  a déjà une piste (la 0), et le double-clic sur `vsm.tb303` y a changé la
  machine — sans boîte, ce qui est le bon comportement. « Choisissez d'abord
  une piste. » ne s'atteint pas depuis un projet neuf ; son texte se lit dans
  le code et la table. Le cas reste au banc, attendu à 0 boîte dans les deux
  langues.
- **Le titre « Preset appliquÃ©, avec des reserves ».** Écrit en littéral
  sans `u8`, il passe par `juce::String(const char*)`, qui ne lit pas l'UTF-8 :
  la boîte française montrait des octets. `tr(u8"…")` le répare. C'est donc
  une SECONDE différence française, voulue elle aussi : « appliquÃ© » →
  « appliqué » (avec l'accent de la décision 3). « Preset non appliqué »
  a le même défaut, mais le banc n'ouvre pas sa boîte.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026). Banc : quatre lancements**
par langue, témoin puis D99, écran verrouillé (session `LockedHint=yes` : les
boîtes se lisent par `VSM_BOITE`, la photo ne sert qu'aux panneaux) ; HOME
isolé, préférences de l'utilisateur intactes (`cmp`, deux séries).

1. **L'inventaire — tenu.** ÉCRAN **147 → 129** (−18), 0 ÉCRAN et 0
   SANS_PAIRE dans les trois fonctions ; TABLE **214 → 213** (« Projet jamais
   enregistré »). Restent TABLE dans le lot les trois libellés d'annulation,
   traduits à l'affichage par `trGeste`. TERMINAL **98 → 100** : les deux
   lignes que la commande de banc écrit sur la sortie d'erreur (« sur la
   piste », « absent du navigateur (… entrées) ») — non prévues, et à leur
   place. Les huit affichages que l'inventaire ne voit pas passent tous par
   `tr()`, comptés à la main.
2. **Les boîtes — tenu.** 0, 1, 6 et 3, dans les deux langues, témoin et
   D99 : chaque geste a été joué (`VSM_NAVIGATEUR : … sur la piste N`), aucune
   référence absente de la liste. `sans-piste` à 0, comme le témoin l'avait
   annoncé.
3. **L'anglais — tenu, avec un reste nommé.** Toutes les boîtes sont en
   anglais, jusqu'aux messages d'`interchange/` et d'`audio/` : « invalid
   JSON: '"' expected (position 2) », « JSON root: object expected »,
   « 9 parameter(s) applied, 1 unsupported: parametre.inexistant »,
   « unreadable audio (…/casse.wav): RIFF/WAVE header missing ». Il a fallu
   pour cela 8 clés (les messages de l'analyseur JSON, la racine d'un preset,
   l'en-tête WAV) et 5 modèles, et « audio illisible (%1) : %2 » est passé à
   `%P2` : sa cause est une phrase, pas une donnée — une cause sans modèle
   revient telle quelle. Deux mots français restent, et ce sont des données :
   `projet` dans le chemin de la copie du banc, et **« Piste 3 »** — le nom
   que la piste a reçu à sa création, ÉCRIT dans le projet en français alors
   que l'interface est anglaise (la liste des pistes, elle, ne traduit
   « Piste %1 » que pour un nom vide). Piste pour plus tard : le nom par
   défaut d'une piste neuve est une donnée fabriquée par l'application, pas
   par l'utilisateur.
4. **Le français — tenu.** Les boîtes identiques au témoin sauf une, et
   c'est la différence déclarée : « Preset appliquÃ©, avec des reserves » →
   « Preset appliqué, avec des réserves ». Les listes de la fenêtre
   principale identiques dans les deux langues (151, 160, 175, 182 textes).
   Les octets abîmés ne sont pas un défaut répandu à l'écran : 0 texte abîmé
   dans les listes des séries D98 et D99, 2 boîtes (ce titre, en français et
   en anglais) ; la classe — un littéral accentué sans `u8` qui passe par
   `juce::String(const char*)` — reste à compter sur tout `app/Source` avec
   une règle propre, que le premier balayage (759 candidats, table et outils
   compris) n'avait pas.
5. **Les origines — tenu pour les machines.** La fenêtre du navigateur,
   photographiée, dit « VSM machines » en anglais et « Parc VSM » en français
   sur ses vingt premières lignes. Les entrées de la bibliothèque sont sous le
   pli : « Library » se lit dans la table et le code, pas sur l'image. La même
   image montre ce que D99 ne traduit pas : les descriptions des machines
   (« Multisample (acoustique échantillonné) »), qui viennent du registre —
   leur lot est déjà nommé.

**Les boîtes que le banc n'ouvre pas** : « Choisissez d'abord une piste. »
(un projet neuf a une piste), « Copie impossible » (il faudrait un dossier
sans droit d'écriture), « Preset non appliqué » (une machine absente du
parc). Leur texte se lit dans le code et la table.

La table passe à **1 159** paires (+31 : 23 pour le lot, 8 pour les messages
d'`interchange/` et d'`audio/`), **89** modèles (+5). `VintageSynthMidiStudio`
et `vsm-ui-preview` compilent ; suites C++ vertes (330 cœur, 1 291 audio,
297 interchange, 25 CLAP, 11 panneaux). `MainComponent` : 113 chaînes à
l'inventaire.

### Phase D100 — A9 : les titres des fenêtres flottantes (11/09/2026)

**LE DÉFAUT.** Quinze fenêtres flottantes (`PanelWindow`) : cinq panneaux
(Pistes, Piano Roll, Synth Rack, Mixer, Arrangement) et dix fenêtres ouvertes
à la demande (Navigateur, Historique des modifications, Analyseur de spectre,
Raccourcis clavier, Associations MIDI, Reconstruction, Assembler les prises,
Ordre de jeu, Notes du projet, Préférences). Aucune n'a de titre traduit : en
anglais, la barre de titre dit « Historique des modifications ». Et `tr()` à
l'appel n'était pas la réponse : le titre EST la clé sous laquelle la fenêtre
retient sa position (`fenetre.<titre>`), et les préférences de l'utilisateur en
portent dix (`fenetre.Pistes`, `fenetre.Préférences`…). Traduire le titre
aurait perdu ces positions au premier lancement en anglais, et en aurait écrit
une seconde série sous les titres anglais.

**LA DÉCISION.** `PanelWindow` sépare les deux. La clé est le titre français
donné à la construction, gardé tel quel ; le titre affiché est sa traduction,
reposée par `PanelWindow::retraduire()` au changement de langue — appelé par
`MainComponent::retraduire()` pour les quinze. Les appels ne changent pas : ils
passent le français, et c'est `PanelWindow` qui traduit. Six clés neuves (les
titres qui n'avaient pas de paire).

**LE BANC.** L'outil d'abord, au témoin comme à l'après : la ligne
`VSM_FENETRE` dit les limites de la fenêtre (`-- limites x y l h`). HOME isolé
dont les préférences retiennent deux positions (`fenetre.Historique des
modifications` = 140 160 520 430, `fenetre.Analyseur de spectre` = 700 180
640 380). Trois lancements sur un projet neuf, qui ouvrent sept fenêtres par la
barre de menus (Navigateur, Raccourcis clavier, Historique, Analyseur, Notes
du projet, Associations MIDI, Préférences) : `fr`, `en`, et `bascule` (en
français, puis « English » dans le menu, fenêtres ouvertes). Les préférences
du HOME sont copiées à la fin de chaque lancement.

**ATTENDU, écrit avant la mesure.**

1. **Les titres** : en anglais, les sept fenêtres ouvertes et les cinq
   panneaux cachés portent leur titre anglais (« Edit history », « Tracks »…) ;
   au témoin, le français.
2. **Le changement de langue en direct** : `bascule` finit avec les titres
   anglais ; le témoin garde les français.
3. **Les positions** : l'historique et l'analyseur s'ouvrent aux limites
   retenues, dans les deux langues — ou, si le gestionnaire de fenêtres en
   décide autrement, aux MÊMES limites en français et en anglais : c'est
   l'invariant.
4. **Les clés** : après chaque lancement, les préférences du HOME ne portent
   que des clés `fenetre.` françaises — aucune `fenetre.Edit history`.
5. **Le français** : les titres, et les listes de la fenêtre principale et
   des fenêtres ouvertes, identiques au témoin.
6. **L'inventaire** : ÉCRAN **129 → 126** et TABLE **213 → 216** —
   « Historique des modifications », « Notes du projet » et « Préférences »
   deviennent des clés. Les sept autres titres échappaient déjà à l'inventaire
   (sans accent ni mot de sa liste) ou étaient déjà des clés.

**VU DANS LE TÉMOIN (D100), écrit avant la mesure d'après.** Trois
lancements, sept menus exécutés (huit avec « English »), sept fenêtres lues et
cinq panneaux cachés à chaque fois, préférences de l'utilisateur intactes.
Titres français partout, anglais compris — le défaut, tel qu'annoncé. Deux
choses précisent l'attendu :

- **L'historique s'ouvre exactement à sa position retenue** (140 160 520 430).
  **L'analyseur, retenu à x = 700, s'ouvre à x = 640** : `setDefaultSize`
  ramène la fenêtre dans la zone utile de l'écran (sa bordure droite tombe à
  x = 1 280, soit 640 + 640) — la règle de D15.3, pas la langue. L'attendu 3
  se juge donc sur l'égalité français / anglais, et sur la position exacte de
  l'historique.
- **Chaque fenêtre ouverte écrit sa clé** (`memoriser()` à l'affichage) :
  sept clés `fenetre.` à la fin du lancement, pas seulement les deux
  retenues. L'attendu 4 porte sur ces sept.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Témoin puis D100, trois
lancements chacun, écran verrouillé ; préférences de l'utilisateur intactes
(`cmp`, deux séries).

1. **Les titres — tenu.** En anglais : « Browser », « Edit history »,
   « Keyboard shortcuts », « MIDI mappings », « Preferences », « Project
   notes », « Spectrum analyser » pour les sept fenêtres ouvertes ; « Tracks »,
   « Piano Roll », « Synth Rack », « Mixer », « Arrangement » pour les cinq
   panneaux cachés. Le témoin : les mêmes, en français.
2. **Le changement de langue en direct — tenu.** `bascule` (ouvertes en
   français, puis « English ») finit avec les douze titres anglais ; le témoin
   les gardait français.
3. **Les positions — tenu.** Les sept fenêtres ont les MÊMES limites en
   français, en anglais et après la bascule, au pixel ; l'historique à sa
   position retenue (140 160 520 430), l'analyseur ramené dans l'écran
   (640 180 640 380) comme au témoin.
4. **Les clés — tenu.** Après chacun des trois lancements, les préférences
   du HOME portent les sept mêmes clés, françaises (`fenetre.Historique des
   modifications`… `fenetre.Préférences`), aux mêmes valeurs qu'au témoin ;
   aucune clé anglaise.
5. **Le français — tenu.** Titres et limites identiques au témoin ; listes
   identiques (180 textes de la fenêtre principale, 163 dans les fenêtres
   ouvertes).
6. **L'inventaire — tenu.** ÉCRAN **129 → 126**, TABLE **213 → 216**. Il ne
   voit que cinq des quinze titres : six titres français lui échappent
   (« Navigateur », « Analyseur de spectre », « Raccourcis clavier »,
   « Associations MIDI », « Reconstruction », « Ordre de jeu »), et quatre
   sont des mots anglais. Pièce de plus pour D101.

**Ce que le banc n'ouvre pas** : « Reconstruction », « Assembler les
prises » et « Ordre de jeu » (il faut un rapport, des prises, des sections).
Elles passent par le même `PanelWindow`, et leur titre est une clé de la table.

La table passe à **1 165** paires (+6). `VintageSynthMidiStudio` et
`vsm-ui-preview` compilent ; suites C++ vertes (330 cœur, 1 291 audio,
297 interchange, 25 CLAP, 11 panneaux). `MainComponent` : 110 chaînes à
l'inventaire. **La leçon** : un titre qui sert aussi de clé se traduit là où
il s'affiche, pas là où il est donné — sinon changer de langue fait oublier
où l'utilisateur a mis ses fenêtres.

### Phase D101 — A9 : l'angle mort de l'inventaire (11/09/2026)

**LE CONSTAT.** L'inventaire (`tools/inventaire_langue.py`) ne compte une
chaîne que si elle a l'air française : un accent, ou un mot d'une courte liste
(le, la, piste, projet…). Deux lots de suite ont montré ce qu'il laisse
passer : huit affichages du navigateur (D99), six titres de fenêtres sur
quinze (D100) — des libellés courts, sans accent ni mot de la liste :
« Navigateur », « Preset illisible », « Raccourcis clavier ». Le chiffre
d'A9 (ÉCRAN 126) est donc un minimum, et l'on ne sait pas de combien.

**DEUX RÈGLES DE PLUS, écrites avant de compter.** Mesurées séparément puis
ensemble, sur le MÊME code, par une option de l'outil
(`--regle=stricte|mots|position|large`) ; la règle d'aujourd'hui (`stricte`)
reste le défaut tant que la mesure n'a pas tranché.

- `mots` : des mots-outils français que l'anglais n'emploie pas (de, du, et,
  est, pas, ne, au, aux, ce, cette, ces, qui, que, son, sa, ses, leur, leurs,
  puis, rien, tout, tous, toute, toutes, vers, votre, vos) et les élisions
  (l', d', n', s', c', j', qu'). **Aucun mot de contenu choisi d'après les
  ratés connus** : une règle taillée pour retrouver ce qu'on sait déjà ne
  mesurerait rien.
- `position` : une chaîne qui a des lettres et n'a pas l'allure d'un
  identifiant (sans espace, et un point, un tiret bas, une barre, deux-points
  ou une minuscule en tête), passée dans une instruction qui affiche :
  `montrerBoite`, `showMessageBoxAsync`, `setButtonText`, `setText`,
  `setTooltip`, `addItem`, `addSectionHeader`, `addTab`, `drawText`,
  `drawFittedText`, `setTitle`, `addTextEditor`, `PanelWindow`.
- `large` : l'une ou l'autre.

**ATTENDU.**

1. **Le rappel, sur une vérité connue.** Le code de D98 (`17991aa`) porte
   encore les quatorze ratés de D99 et D100 sans traduction. Dérivé des
   règles, pas mesuré : `mots` en attrape 4 (« Preset d'effet illisible »,
   « L'effet « … » n'est pas disponible. », « Analyseur de spectre »,
   « Ordre de jeu ») ; `position` attrape ceux qui passent par une fonction
   de la liste — les deux titres « Navigateur » des boîtes, « Preset
   illisible », « Preset d'effet illisible », « Copie impossible », et les
   titres des fenêtres créées par `std::make_unique<PanelWindow>(` —, soit
   au moins 10 ; il manque « Parc VSM » et « Plugin tiers », écrits dans
   une affectation, et « L'effet » passé en message. `large` : au moins 12
   sur 14.
2. **La taille, sur le code de D100.** `mots` ajoute entre 10 et 60 chaînes
   ÉCRAN, `position` entre 30 et 120, `large` entre 40 et 150.
3. **La lecture.** Chaque ajout de `large` se lit et se range : français
   affiché (le vrai angle mort), texte identique dans les deux langues
   (« Piano Roll », une unité), donnée ou format. J'attends au moins 70 % de
   français.
4. **La décision, écrite d'avance.** Si `large` tient les 70 %, elle devient
   la règle d'A9 : le chiffre est redit sous elle, les deux donnés (126 et le
   nouveau), et les textes identiques dans les deux langues vont dans une
   liste nommée de l'outil, lue et non devinée. Sinon, elle reste une option,
   et l'angle mort est donné comme un intervalle.

**LE RÉSULTAT (11/09/2026).** L'outil seul : aucun code de l'application
touché. La règle `stricte` redonne à la ligne près les comptes de l'outil de
HEAD (ÉCRAN 126, SANS_PAIRE 0, TERMINAL 100, TABLE 216, COMMANDE 25) : le
témoin est bien le même instrument.

1. **Le rappel — tenu, au chiffre près.** Sur le code de D98 : `stricte`
   0 / 14, `mots` 4 / 14 (les quatre prévus), `position` 12 / 14, `large`
   12 / 14 ; les deux manqués sont ceux prévus (« Parc VSM », « Plugin
   tiers », écrits dans une affectation). Des douze, cinq tombent en TABLE :
   des clés affichées sans `tr()` — le faux TABLE de D96, qu'aucune de ces
   règles ne distingue d'une clé traduite par une variable.
2. **La taille — tenue.** Sur le code de D100, ÉCRAN : `mots` 142 (+16),
   `position` 166 (+40), `large` 179 (+53).
3. **La lecture — NON TENUE.** Des 53 ajouts de `large`, 33 sont du texte
   français et 20 se lisent pareil dans les deux langues : le nom de
   l'application, les unités (« kHz », « BPM », « dB », « -inf LUFS »,
   « ticks »), « SYNTH RACK », « PATTERN », « Ch », « CPU », « Track »,
   « Signature », « -> Master ». 62 %, sous les 70 % écrits. Règle par
   règle, c'est net : `mots` 16 sur 16 français, `position` 20 sur 40 — les
   faux sont tous à elle. `position` trouve aussi 6 SANS_PAIRE, tous des mots
   identiques en anglais (« MIDI CC », « Transposition », « Octave + »,
   « Octave - », « Crescendo », « Decrescendo ») : `tr()` les rend tels
   quels, sans défaut à l'écran.
4. **La décision, telle qu'écrite.** `large` reste une option, le défaut
   reste `stricte`, et le chiffre d'A9 se donne en intervalle : ÉCRAN **126**
   à la règle stricte, **au moins 159** en comptant les 33 chaînes françaises
   lues — et le rappel dit qu'il en reste d'autres (2 ratés sur 14 connus).
   Je n'adopte pas `mots` comme défaut, bien que ses 16 ajouts soient tous
   français : ce serait une décision prise après la mesure, sur seize
   chaînes. Elle se prendra avec son hypothèse, si un lot la demande.

**LES 33, NOMMÉES** — ce que la mesure rapporte de plus utile, pour les lots
d'A9 à venir :

- **`MainComponent`, 27.** Les plugins (« Plugin CLAP illisible », « Plugin
  charge », « Balayage termine », « Balayage lance », « Pas d'interface
  native », « Effet illisible », « Plugin VST3 illisible », « Instrument
  charge ») ; l'enregistrement et la référence (« Charger l'enregistrement
  d'origine ( », « Enregistrement illisible », « (canal gauche de
  comparaison.wav) », « Enregistrement audio impossible », « Report
  impossible » deux fois, « Mesure impossible », « Rien n'est revenu »,
  « Aller-retour : ») ; les réglages relus (« Rapport de reconstruction
  illisible : », « Reconstruction indisponible », « Associations MIDI
  illisibles », « Raccourcis illisibles », « Ignorer et effacer ») ; la
  restauration d'un preset de piste (« ». Sa machine et ses inserts sont
  revenus. », « insert(s) de « ») ; « Couleur du clip », « Import audio »,
  et la boîte « À propos » (« Sequenceur MIDI + rack de synthetiseurs… »).
- **`audio/`, 4** : les erreurs de `DiskRecorder` (2) et de
  `ReferenceAudioLoader` (2), rendues à `MainComponent` — suivies jusqu'à
  leur appelant, pas jusqu'à une image.
- **`PianoRollComponent`, 2** : « note(s) plus faible(s) que », « note(s)
  plus courte(s) que ».

**Huit sont du français SANS SES ACCENTS** (« Plugin charge », « Balayage
termine », « Balayage lance », « Instrument charge », « Sequenceur … de
synthetiseurs », « Frequence ou nombre de canaux invalide. », « Impossible
d'ecrire », « frequence d'echantillonnage… ») — écrits en ASCII, sans doute
pour éviter le piège des `u8`. Ils sont faux en français aussi : leur lot
leur rendra leurs accents, différence française déclarée d'avance.

Outil : `ruff` et `mypy` propres ; aucune suite C++ concernée.

### Phase D102 — A9 : les plugins (11/09/2026)

**LE LOT.** Huit fonctions de `MainComponent` : `showAboutDialog`,
`loadClapPluginOnSelectedTrack`, `scanInstalledPlugins`,
`chooseInstrumentFromCatalogue`, `openPluginEditorForSelectedTrack`,
`chooseThirdPartyEffect`, `browseForThirdPartyEffect`,
`loadVst3PluginOnSelectedTrack`. À l'inventaire : 21 ÉCRAN à la règle stricte,
31 à la règle large (D101). Une vingtaine de ces chaînes sont du français SANS
SES ACCENTS (« Plugin charge », « Balayage termine », « facade »,
« reglages »…), comme deux libellés du menu Piste qui les ouvrent.

**LA COMMANDE DE BANC.** Trois de ces chemins passent par un sélecteur de
fichier, qu'aucun banc ne pilote. `VSM_PLUGIN=chemin` pose le fichier que le
PROCHAIN sélecteur de plugin rendra. Le geste qui l'ouvre reste celui de
l'utilisateur — le menu Piste (`VSM_MENU`), ou la liste « ajouter un effet »
de la chaîne d'effets (`VSM_MENU_CONTEXTE=ajout-effet:libellé`, nouvelle, qui
choisit l'entrée comme la souris) ; seul le sélecteur est sauté, et c'est dit
(`VSM_PLUGIN : sélecteur sauté, fichier …`). La suite du sélecteur devient une
fonction locale, que le sélecteur et le banc appellent ; rien d'autre ne
bouge.

**LES DÉCISIONS.**

1. **Les accents rendus.** Les chaînes du lot écrites sans accents les
   retrouvent en français, avec les deux libellés du menu Piste
   (« Rechercher les plugins installés... », « Instrument parmi les plugins
   trouvés... ») et leurs clés. C'est la différence française voulue, déclarée
   ici, et la seule.
2. **La boîte « À propos » est traduite telle qu'elle est**, version et
   phases comprises : la mettre à jour serait un autre travail.
3. **Le titre de la fenêtre pendant le balayage** (« … -- balayage N/M :
   fichier ») passe par `tr()` lui aussi.
4. **« Pas d'interface native » ne se banc pas** : son entrée de menu est
   grisée pour une machine du parc, et `VSM_MENU` n'exécute pas une entrée
   grisée — c'est voulu (D7.4), et forcer le passage vérifierait un chemin
   que personne n'emprunte.

**ATTENDU, écrit avant la mesure.** HOME isolé : le catalogue des plugins vit
sous `~/.config`, celui de l'utilisateur n'est ni lu ni écrit. Projet neuf
(une piste MIDI). Par langue, témoin (D102 sans ses traductions : la commande
de banc et `montrerBoite()` en place) puis D102 :

| cas | geste | fichier | boîtes |
|---|---|---|---|
| apropos | Aide ▸ À propos | — | 1 |
| clap-illisible | Charger un plugin CLAP | un `.clap` cassé | 1 : Plugin CLAP illisible |
| clap-instrument | Charger un plugin CLAP | `vsm-test-gui.clap` (un instrument) | 1 : Plugin chargé |
| clap-plusieurs | Charger un plugin CLAP | `vsm-instruments.clap` (les machines du parc) | 0 ; la fenêtre « Plusieurs plugins dans ce fichier », lue par `VSM_FENETRE` |
| vst3-illisible | Charger un instrument VST3 | un `.vst3` cassé | 1 : Plugin VST3 illisible |
| effet-illisible | ajouter un effet ▸ Un plugin | `vsm-instruments.clap` (que des instruments) | 1 : Effet illisible |
| effet-clap | ajouter un effet ▸ Un plugin | `vsm-test-effect.clap` | 0 ; « Annuler : Ajouter un effet » au menu Édition |
| balayage | Rechercher les plugins installés | — | 1 ou 2 : Balayage lancé, puis terminé s'il finit avant la photo |

1. **L'inventaire** : 0 ÉCRAN et 0 SANS_PAIRE dans les huit fonctions ;
   ÉCRAN **126 → 105** à la règle stricte, **179 → 148** à la règle large.
2. **Les boîtes** : le compte du tableau, dans les deux langues, témoin et
   D102 ; chaque cas dit `VSM_PLUGIN : sélecteur sauté`.
3. **L'anglais** : aucun mot français dans les boîtes et la fenêtre « Several
   plugins », hors des données — chemins, et les noms des machines que
   `vsm-instruments.clap` publie (ils viennent du registre : leur lot est
   nommé). Les erreurs des hôtes CLAP et VST3 passent par `trPhrase` ; celle
   qui resterait française faute de modèle en reçoit un, ou est nommée.
4. **Le français** : identique au témoin, sauf les accents de la décision 1 ;
   la liste de la fenêtre principale identique.

**VU DANS LE TÉMOIN (D102), écrit avant la mesure d'après.** Seize lancements,
préférences intactes ; le tableau tenu cas par cas (1, 1, 1, 0, 1, 1, 0, et 2
pour le balayage, fini avant la photo), un sélecteur sauté dans chaque cas à
fichier, et « Annuler : Ajouter un effet » au menu Édition après `effet-clap`.
Deux choses à écrire avant d'aller plus loin :

- **La fenêtre « Plusieurs plugins dans ce fichier » n'a pas été lue** :
  « 0 autre(s) fenêtre(s) lue(s) ; composants modaux : 0 ». L'écran est
  verrouillé, et sous un écran verrouillé JUCE ne montre AUCUN composant
  modal — le piège de D95. Les boîtes y échappent parce que `montrerBoite()`
  les écrit quand elles sont demandées ; les quatre fenêtres de choix du lot
  (plusieurs plugins, plusieurs instruments, le catalogue, les effets
  trouvés) ne passent pas par elle. **Décision** : une fonction
  `annoncerFenetre()` écrit leur titre et leurs textes (`VSM_CHOIX`) au moment
  où elles sont demandées. Elle n'entre qu'au build d'après : le témoin, lui,
  n'a rien lu de cette fenêtre, et ne peut donc pas lui servir de témoin. Son
  français se compare au code du témoin, pas à une lecture.
- **Les erreurs des hôtes** : « chargement impossible : …/casse.clap: en-tête
  ELF invalide » (CLAP) et « aucun plugin VST3 dans « … » » (VST3). Deux
  modèles de phrase de plus. La fin du premier (« en-tête ELF invalide ») est
  le message du SYSTÈME (`dlerror()`, dans la langue de la session) : une
  donnée, que l'application ne traduit pas.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Témoin puis D102, seize
lancements chacun, écran verrouillé ; préférences de l'utilisateur intactes
(`cmp`, deux séries).

1. **L'inventaire — tenu à la règle stricte, à une chaîne près à la large.**
   0 ÉCRAN et 0 SANS_PAIRE dans les huit fonctions. Stricte : ÉCRAN
   **126 → 105**, comme écrit. Large : **179 → 149**, pas 148 : le titre de
   la boîte « À propos », « Vintage Synth MIDI Studio », reste — c'est le nom
   de l'application, que D101 rangeait déjà parmi les textes identiques dans
   les deux langues, et je l'avais compté parmi les partants. TABLE
   **216 → 218** : « ce fichier ne contient que des instruments… », écrit deux
   fois par `browseForThirdPartyEffect`, est désormais une clé, lue à l'écran
   par `trPhrase`. Des 33 chaînes de l'angle mort (D101), neuf étaient ici
   (l'écart large − stricte passe de 53 à 44) ; il en reste 24, dont trois
   écrites sans leurs accents.
2. **Les boîtes — tenu.** Le tableau, cas par cas, dans les deux langues, au
   témoin comme après : 1, 1, 1, 0, 1, 1, 0, 2 ; un sélecteur sauté dans
   chaque cas à fichier ; « Undo: Add an effect » après `effet-clap` — le
   chemin d'insertion d'un effet tiers, réécrit autour de `suite`, insère
   toujours.
3. **L'anglais — tenu.** « Unreadable CLAP plugin : This file yielded no
   plugin. / cannot load: …/casse.clap: en-tête ELF invalide », « Plugin
   loaded : VSM Test GUI (CLAP) now plays on track 2. », « no VST3 plugin in
   “…/casse.vst3” », « this file only contains instruments: put it on a
   track, not as an insert », « Scan started », « Scan finished : 0
   instrument(s), 0 effect(s) found. », la boîte « About ». Restent en
   français deux données, annoncées : le message du système (`dlerror()`,
   « en-tête ELF invalide ») et les noms des machines que
   `vsm-instruments.clap` publie (« Additive (le spectre rang par rang) »).
4. **Le français — tenu.** Identique au témoin aux accents déclarés près
   (« Plugin chargé », « Balayage lancé », « Balayage terminé », la boîte
   « À propos »), et nulle part ailleurs ; listes de la fenêtre principale
   identiques (180 et 160 textes).
5. **La fenêtre de choix — lue après seulement, comme décidé.** `VSM_CHOIX` :
   « Plusieurs plugins dans ce fichier. Lequel charger ? », Charger, Annuler ;
   « Several plugins in this file. Which one to load? », Load, Cancel. Le
   témoin n'en a rien lu (écran verrouillé) : son français se compare au code
   du témoin — « Plusieurs plugins dans ce fichier », « Lequel charger ? »,
   « Charger », sans accent à rendre — et il est le même.

**Ce que le banc n'ouvre pas** : « Pas d'interface native » (entrée grisée,
décision 4), la fenêtre « Plusieurs instruments dans ce fichier » (il faudrait
un VST3 à plusieurs instruments), le choix dans le catalogue et parmi les
effets trouvés (il faut un catalogue non vide : le balayage du banc n'en
trouve aucun — dossiers système vides, HOME isolé), « Pas d'instrument dans ce
fichier » (un VST3 d'effets). Leurs textes sont des clés de la table, et les
trois fenêtres de choix passent par `annoncerFenetre()`.

La table passe à **1 199** paires (+34), et deux clés renommées avec leurs
accents ; **91** modèles (+2). `VintageSynthMidiStudio` et `vsm-ui-preview`
compilent ; suites C++
vertes (330 cœur, 1 291 audio, 297 interchange, 25 CLAP, 11 panneaux).
`MainComponent` : 89 chaînes à l'inventaire strict.

### Phase D103 — A9 : les noms des machines (11/09/2026)

**LE DÉFAUT.** Le parc compte 65 machines ; 48 de leurs noms portent une
description française entre parenthèses (« Multisample (acoustique
échantillonné) », « Terrain (le chemin fait le timbre) »). En anglais, on les
lit en français dans la liste des instruments de chaque piste, en tête du
Synth Rack et dans le navigateur — là où l'on choisit une machine. Aucune règle
de l'inventaire ne les voit : ils viennent du moteur (`audio/plugins/`), pas
d'`app/Source`, et une description comme « Drums (batterie acoustique) » n'a
ni accent ni mot-outil. Deux sources de noms, d'ailleurs : le nom
ENREGISTRÉ (`VSM_REGISTER_SYNTH_PLUGIN`), que montrent la liste des pistes et le
navigateur, et `machineName()`, que montre le rack. Elles concordent pour 63
machines sur 65.

**LES DÉCISIONS.**

1. **La traduction se fait à l'affichage**, par des clés : le nom complet en
   français est la clé, le nom complet en anglais la valeur. Le nom avant la
   parenthèse est déjà celui de la machine dans les deux langues ; seule la
   description se traduit. Les deux « Test Tone (reference…) » sont déjà
   anglais : pas de clé. Donc **47 clés**.
2. **Trois points d'affichage** : la liste des instruments de la ligne de
   piste, l'étiquette du rack, les noms du navigateur (traduits à la source,
   comme ses origines en D99 : le filtre lit ce qu'on voit). La liste de la
   ligne de piste et l'étiquette du rack suivent la bascule de langue ; elles
   ne la suivaient pas (seule l'entrée « (Aucun) » était reposée).
3. **« Sampler (8 emplacements) » est faux** : le sampler a 16 emplacements
   (`kSlotCount = 16`), et son `machineName()` le dit. Le nom enregistré est
   aligné sur la machine : le navigateur et la liste des pistes disaient 8, le
   rack 16. Différence française voulue, déclarée ici.
4. **Un compte de plus à l'inventaire**, écrit avant de compter :
   `--machines` lit les noms enregistrés et les `machineName()` des machines,
   et compte ceux qui portent une parenthèse sans avoir de clé — hors une
   liste nommée de noms identiques dans les deux langues (les deux « Test
   Tone »). Sans lui, une machine ajoutée demain s'afficherait en français
   sans que rien ne le dise.

**ATTENDU, écrit avant la mesure.** Le témoin est le binaire de HEAD (D102),
déjà construit : il n'y a pas d'outil de banc à ajouter. HOME isolé, copie de
demo-project. Trois cas, français puis anglais, témoin puis D103 :
`modal` (la piste 1 reçoit `vsm.modal` par `VSM_GESTE_PISTE`, navigateur
ouvert et photographié), `sampler` (la piste 1 reçoit `vsm.sampler`),
`bascule` (en français, `vsm.modal`, puis « English » dans le menu).

1. **L'inventaire des machines** : 47 noms sans clé → 0. Les comptes
   d'`app/Source` ne bougent pas (règle stricte : ÉCRAN 105).
2. **L'anglais** : la liste de la piste et l'étiquette du rack disent
   « Modal (the struck object) », « Sampler (16 slots) » ; au témoin, le
   français.
3. **La bascule** : `bascule` finit avec les noms anglais — au témoin, la
   liste et le rack gardaient le français.
4. **Le français** : identique au témoin, sauf « Sampler (8 emplacements) » →
   « Sampler (16 emplacements) » dans la liste de la piste (décision 3).
5. **Le navigateur** photographié en anglais : les noms des machines visibles
   en anglais.

**VU DANS LE TÉMOIN (D103), écrit avant la mesure d'après.** Cinq lancements,
préférences intactes. Le défaut, tel qu'annoncé : en anglais, la liste de la
piste et l'étiquette du rack disent « Modal (l'objet frappé) » ; pour le
sampler, la liste dit « Sampler (8 emplacements) » et le rack « Sampler (16
emplacements) », dans les deux langues. Deux corrections à l'attendu :

- **`--machines` compte 48 noms sans clé au témoin, pas 47** : le quarante-
  huitième est le nom faux du sampler, « (8 emplacements) », compté à côté du
  juste. Après D103, attendu : 47 décrits, 0 sans clé.
- **Un test garde « Sampler (8 emplacements) »** (`interchange/tests/
  test_preset_samples.cpp`) : c'est le nom de machine NOTÉ dans un preset de
  test (`preset.machineName`), une donnée, pas le nom enregistré. Il reste tel
  quel ; le garde-fou du correctif, qui refusait d'écrire, ne regarde plus que
  le code hors des tests.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Témoin (le binaire de
HEAD) puis D103, cinq lancements chacun, écran verrouillé ; préférences de
l'utilisateur intactes (`cmp`, deux séries).

1. **L'inventaire des machines — tenu, compté juste.** `--machines` : 48 noms
   décrits sans clé au témoin (les 47, plus le nom faux du sampler), **0**
   après, sur 47 décrits — 66 noms en tout, le nom faux parti. Les comptes
   d'`app/Source` ne bougent pas (ÉCRAN 105 à la règle stricte, 149 à la
   large).
2. **L'anglais — tenu.** Liste de la piste et étiquette du rack : « Modal (the
   struck object) », « Sampler (16 slots) » ; au témoin, le français.
3. **La bascule — tenu.** `bascule` finit sur « Modal (the struck object) »
   dans la liste de la piste et dans le rack ; le témoin gardait le français
   aux deux endroits.
4. **Le français — tenu.** Identique au témoin, sauf la ligne déclarée : la
   liste de la piste dit « Sampler (16 emplacements) », comme le rack ; les
   autres textes identiques (175 et 298).
5. **Le navigateur — tenu.** En anglais, les vingt lignes visibles :
   « Multisample (sampled acoustic) », « Terrain (the path makes the
   timbre) », « Sampler (16 slots) », « Reed (the free reed) », « Additive
   (the spectrum, partial by partial) »…, origine « VSM machines ».

La table passe à **1 246** paires (+47). `VintageSynthMidiStudio` et
`vsm-ui-preview` compilent ; les cibles de test RECONSTRUITES (le nom
enregistré du sampler est dans `vsm_audio`) passent : 330 cœur, 1 291 audio,
297 interchange, 25 CLAP, 11 panneaux. **La leçon** : un nom qui n'est pas dans
`app/Source` échappe à tout inventaire de l'interface — il fallait un compte à
lui, et c'est en le faisant qu'est apparu le sampler qui disait 8 à un endroit
et 16 à l'autre.

### Phase D104 — A9 : la reprise après une session interrompue (11/09/2026)

**LE LOT.** `offerCrashRecovery` : la boîte « Session interrompue », ouverte au
démarrage quand une session s'est arrêtée sans se fermer. Huit chaînes à
l'inventaire (règle large). C'est la boîte qu'on lit au pire moment : le
logiciel vient de tomber, et l'utilisateur doit décider de récupérer ou
d'effacer son travail.

**LE BANC.** Une session interrompue se fabrique : un dossier sous
`recuperation/` (à côté des préférences, donc dans le HOME isolé), un
`project.json` et une fiche `recuperation.json` (`vsm.recuperation.v1`) dont
personne ne tient le verrou. La boîte est une QUESTION (`showOkCancelBox`) :
sous un écran verrouillé, JUCE ne la montre pas (D95). Une fonction
`demanderOuiNon()` l'écrit donc quand elle est demandée — titre, message, et
ses deux réponses — au témoin comme après. Trois cas, français puis anglais :
`jamais` (projet jamais enregistré, sauvé il y a cinq minutes, une seconde
session en attente), `recent` (projet enregistré, sauvé à l'instant),
`sans-titre` (titre vide).

**LES DÉCISIONS.** Les deux formes de « il y a N minute(s) » restent deux clés
(singulier, pluriel) : le français les écrivait ainsi, et il ne doit pas
changer. Le tiret « — » est un littéral sans `u8` : s'il sort abîmé au
témoin, c'est la classe de D99, réparée ici et déclarée.

**ATTENDU, écrit avant la mesure.**

1. **L'inventaire** : 0 ÉCRAN dans `offerCrashRecovery` ; ÉCRAN **105 → 98**
   à la règle stricte (sept de ses chaînes y comptent), **149 → 141** à la
   large.
2. **La boîte** : une par cas, dans les deux langues, témoin et D104, avec
   ses deux boutons. En anglais : « Interrupted session », « Recover »,
   « Ignore and delete », « saved automatically 5 minutes ago », « less than
   a minute ago », « (untitled project) », et le paragraphe du projet jamais
   enregistré.
3. **Le français** : identique au témoin (hors le tiret, s'il était abîmé).

**VU DANS LE TÉMOIN (D104), écrit avant la mesure d'après.** Six lancements,
une boîte chacun, lue par `demanderOuiNon()` avec ses deux boutons ;
préférences intactes. Les minutes justes (330 s → « il y a 5 minutes »,
150 s → « il y a 2 minutes », 5 s → « il y a moins d'une minute »), le
paragraphe du projet jamais enregistré et celui de la seconde session là où
ils doivent être. **Le tiret « — » n'est PAS abîmé** : l'exception déclarée
ne servira pas, et le français doit sortir identique au témoin, sans réserve.
En anglais, la boîte est entièrement française — le défaut, tel qu'annoncé.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Témoin puis D104, six
lancements chacun, écran verrouillé ; préférences de l'utilisateur intactes
(`cmp`, deux séries).

1. **L'inventaire — tenu.** 0 ÉCRAN dans `offerCrashRecovery` ; ÉCRAN
   **105 → 98** à la règle stricte, **149 → 141** à la large, comme écrit.
   « Ignorer et effacer » était l'une des 33 chaînes de l'angle mort (D101) :
   il en reste 23.
2. **La boîte — tenu.** Une par cas, dans les deux langues, témoin et D104,
   avec ses deux boutons. En anglais : « Interrupted session : Morceau du banc
   — 3 track(s), 42 note(s), saved automatically 5 minutes ago. / This
   project had NEVER been saved: without this copy, it would be lost. / (1
   other interrupted session(s) will be kept and offered at the next launch.)
   : [Recover | Ignore and delete] » ; « … saved automatically less than a
   minute ago. » ; « (untitled project) — … 2 minutes ago. ». Reste en
   français le titre du projet, « Morceau du banc » : une donnée. (L'analyse
   relevait aussi « session » et « minute(s) » : ce sont des mots anglais.)
3. **Le français — tenu, sans réserve.** Identique au témoin dans les trois
   cas, boîte et listes de la fenêtre principale (180 textes).

La table passe à **1 256** paires (+10). `VintageSynthMidiStudio` et
`vsm-ui-preview` compilent ;
suites C++ vertes (330 cœur, 1 291 audio, 297 interchange, 25 CLAP, 11
panneaux). `MainComponent` : 82 chaînes à l'inventaire strict.

### Phase D105 — A9 : les boîtes des opérations de piste restantes (11/09/2026)

**LE LOT, APRÈS TRI.** Onze fonctions de `MainComponent` totalisaient 39 chaînes
ÉCRAN (règle large). Lues une à une, elles ne vont pas toutes à l'écran :

- **16 ne vont qu'au terminal** : désactiver une piste, copier et coller une
  chaîne d'inserts, réduire les points d'automation, reporter les effets MIDI
  — leur message est assemblé dans une variable, puis écrit par `fputs` dans
  une AUTRE instruction. L'inventaire ne regarde que l'instruction de la
  chaîne : il les compte ÉCRAN. Faux positif, mesuré à part (D106).
- **4 sont déjà traduites à l'affichage** : les réserves des bus de départ
  (`applySendBuses`) vont au rapport d'ouverture, dont `kModeles` porte les
  deux phrases.
- **1 est une donnée** : le préfixe de fichier `audio/report-piste-`.
- **18 vont à l'écran**, dans huit boîtes de cinq fonctions : la signature
  (deux), le report en audio (deux), la prise retirée, l'import audio refusé,
  ce qui vient d'être joué (deux). Ce sont elles, le lot.

**LE BANC, ET CE QU'IL NE PEUT PAS.** La plupart de ces boîtes sont des gardes
que l'interface ne laisse pas atteindre : les deux boîtes de la signature
suivent une entrée de menu GRISÉE justement quand elles s'ouvriraient ; le
report n'échoue que si le rendu ou l'écriture échoue ; l'import refusé passe
par la boîte « Que faire de ce fichier ? » ; « ce qui vient d'être joué »
demande qu'on ait joué. **Une seule se banc** : la prise retirée. Aucun projet
d'exemple n'a de tiroir de prises ; le banc en fabrique un sur une copie de
demo-project — trois prises, la deuxième entendue, un tronçon d'assemblage sur
elle et un sur la troisième — et retire la prise entendue par le menu
Enregistrement. Le texte dit alors ses quatre phrases : la prise retirée,
« c'était celle qu'on entend », un tronçon retiré, un tronçon reculé d'un
rang. Témoin : les huit boîtes par `montrerBoite()`, sans traduction.

**LA DÉCISION.** Le texte d'une prise retirée et celui d'un import audio vont
à la boîte ET à la sortie d'erreur ; ils sont traduits à la source, comme les
statistiques de D97 — personne ne relit ces lignes au terminal.

**ATTENDU, écrit avant la mesure.**

1. **L'inventaire** : 0 ÉCRAN dans les cinq fonctions ; ÉCRAN **98 → 85** à
   la règle stricte, **141 → 123** à la large (13 et 18 chaînes du lot, la
   donnée restant ÉCRAN : c'est un chemin, pas un texte). *Corrigé avant la
   mesure* : j'avais écrit 98 → 81, en comptant 17 chaînes à la règle stricte
   sans lancer l'inventaire ; lancé sur le code du témoin, il en voit 13 (et
   la donnée), les autres n'ayant ni accent ni mot de sa liste.
2. **La boîte de la prise retirée** : une, dans les deux langues, témoin et
   D105, avec ses quatre phrases. En anglais : « Remove a take : Take “Passe B
   du banc” removed from the drawer. It was the one you hear: … 1 comp
   segment(s) pointed to it: removed. 1 comp segment(s) moved back one
   rank. »
3. **Le français** : identique au témoin, et la liste de la fenêtre
   principale aussi.

**VU DANS LE TÉMOIN (D105), écrit avant la mesure d'après.** Deux lancements,
préférences intactes. Le tiroir fabriqué s'ouvre, et « retirer la prise
entendue » passe par le menu Enregistrement dans les deux langues (le libellé
complet, suffixe compris, désigne bien l'entrée de retrait et non celle de
choix). Une boîte, et ses quatre phrases : « Prise « Passe B du banc »
retirée du tiroir. C'était celle qu'on entend : son matériau RESTE sur la
piste, il n'appartient plus à aucune passe. 1 tronçon(s) d'assemblage la
désignaient : retirés. 1 tronçon(s) ont reculé d'un rang. » Après le retrait,
le menu liste A et C deux fois chacune (choisir, retirer) : quatre entrées,
B partie. En anglais, la boîte est française — le défaut, tel qu'annoncé.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Témoin puis D105, deux
lancements chacun, écran verrouillé ; préférences de l'utilisateur intactes
(`cmp`, deux séries).

1. **L'inventaire — tenu, à la prévision corrigée.** ÉCRAN **98 → 85** à la
   règle stricte, **141 → 123** à la large ; dans les cinq fonctions ne reste
   que la donnée `audio/report-piste-`. TABLE **218 → 215** : trois titres de
   boîte étaient déjà des clés, affichés sans `tr()` (« Retirer une prise »,
   « Récupérer ce qui vient d'être joué » deux fois) — en anglais, ils
   sortaient en français. Des chaînes que seule la règle large voyait, cinq
   étaient ici : « Report impossible » deux fois et « Import audio » (trois
   des 33 de l'angle mort : il en reste 20), et « Signature » deux fois, que
   D101 rangeait parmi les textes identiques et qui dit ici « Time
   signature ». Les 16 chaînes qui ne vont qu'au terminal restent comptées
   ÉCRAN : c'est D106.
2. **La boîte de la prise retirée — tenu.** Une, dans les deux langues,
   témoin et D105. En anglais : « Remove a take : Take “Passe B du banc”
   removed from the drawer. It was the one you hear: its material STAYS on
   the track, it no longer belongs to any pass. 1 comp segment(s) pointed to
   it: removed. 1 comp segment(s) moved back one rank. » Seul reste le nom de
   la prise : une donnée.
3. **Le français — tenu.** Identique au témoin, boîte et liste de la fenêtre
   principale (177 textes).

**Ce que le banc n'ouvre pas** : les deux boîtes de la signature (leur entrée
de menu est grisée quand elles s'ouvriraient), les deux « Report impossible »
(un rendu ou une écriture qui échoue), l'import refusé (derrière la boîte
« Que faire de ce fichier ? »), les deux boîtes de « ce qui vient d'être
joué » (il faut avoir joué). Leurs textes sont des clés de la table.

La table passe à **1 270** paires (+14). `VintageSynthMidiStudio` et
`vsm-ui-preview` compilent ;
suites C++ vertes (330 cœur, 1 291 audio, 297 interchange, 25 CLAP, 11
panneaux). `MainComponent` : 69 chaînes à l'inventaire strict, dont 16 ne
vont qu'au terminal.

### Phase D106 — A9 : ce que l'inventaire comptait à l'écran, et qui ne va qu'au terminal (11/09/2026)

**LE FAUX POSITIF**, vu en D105 : une chaîne assemblée dans une variable
locale, puis écrite par `fputs` dans une AUTRE instruction, est comptée ÉCRAN
— l'inventaire ne regarde que l'instruction de la chaîne. D105 en a lu seize,
dans cinq fonctions (désactiver une piste, copier et coller une chaîne
d'inserts, réduire les points d'automation, reporter les effets MIDI).

**LA RÈGLE, écrite avant de compter.** `--suivi` suit la variable dans sa
fonction. La chaîne est TERMINAL si l'instruction qui la contient affecte ou
complète une variable LOCALE (jamais un membre, `nom_` : l'interface peut le
montrer ailleurs), et si CHAQUE usage suivant de cette variable est un nouvel
ajout (`v +=`, `v =`) ou une instruction qui écrit sur la sortie (la même
expression que la règle TERMINAL). Un seul autre usage — une boîte, un
retour, un libellé, un argument — et elle reste ÉCRAN. Option d'abord : le
témoin est l'outil d'aujourd'hui, sans l'option.

**ATTENDU.**

1. **Les seize connues sortent de l'ÉCRAN** : 14 à la règle stricte (ce que
   l'outil compte aujourd'hui dans les cinq fonctions), 16 à la large.
2. **Ailleurs, peu d'autres** — moins de dix, des comptes rendus de même
   forme. Chacune se vérifie en lisant sa fonction : une seule qui irait à
   l'écran, et c'est la règle qui est fausse, pas le code.
3. **Rien d'autre ne bouge** : SANS_PAIRE, TABLE, COMMANDE identiques ;
   TERMINAL monte d'autant que l'ÉCRAN descend.
4. **La décision, écrite d'avance** : si toutes les nouvelles TERMINAL sont
   justes, `--suivi` devient le défaut, et le chiffre d'A9 est redit sous
   elle, les deux donnés. Sinon, la règle est corrigée — et remesurée — ou
   reste une option.

**MESURE 1 (D106), écrite avant la correction.** `--suivi` passe TERMINAL
13 chaînes à la règle stricte (ÉCRAN 85 → 72), 14 à la large (123 → 109) ;
SANS_PAIRE, TABLE et COMMANDE ne bougent pas. Justesse : toutes les 14 sont
bonnes — 13 des seize connues, et une inattendue, « [grisée] », que
`listMenusForCapture` écrit sur la sortie d'erreur (la liste de menus du
banc). **Mais l'attendu 1 n'est pas tenu** : trois des seize restent ÉCRAN
(lignes 8899, 9075, 9078). La cause est la même pour les trois : un « ; »
écrit DANS une chaîne (« libérés ; ses notes », « ; ») est pris pour une fin
d'instruction, et la règle, qui cherche le début de l'instruction en
remontant jusqu'au dernier « ; », tombe au milieu d'un littéral et ne voit
plus l'affectation. **Correction** : les bornes des instructions et les
usages de la variable se cherchent dans le texte dont les littéraux sont
vidés (même longueur) — un « ; » ou un nom écrits dans une chaîne ne sont
pas du code. Le défaut de la règle TERMINAL elle-même (même recherche du « ; »)
n'est pas touché ici : le témoin doit rester l'outil d'aujourd'hui.

**MESURE 2 ET RÉSULTAT (D106, 11/09/2026).** La règle corrigée, sur le même
code :

1. **Les seize connues — tenu.** 14 passent TERMINAL à la règle stricte, 16 à
   la large ; aucune ne reste ÉCRAN.
2. **Ailleurs — tenu.** Une seule autre, « [grisée] » : `listMenusForCapture`
   la met dans une ligne que l'instruction suivante écrit sur la sortie
   d'erreur (lu dans le code).
3. **Rien d'autre ne bouge — tenu.** SANS_PAIRE, TABLE et COMMANDE
   identiques ; TERMINAL monte d'autant que l'ÉCRAN descend — stricte :
   ÉCRAN 85 → 70, TERMINAL 100 → 115 ; large : ÉCRAN 123 → 106, TERMINAL
   105 → 122.
4. **La décision, telle qu'écrite.** Toutes les nouvelles TERMINAL sont
   justes : la règle devient le DÉFAUT de l'outil, `--sans-suivi` rend
   l'ancienne. Le chiffre d'A9, les deux donnés : ÉCRAN **85** à l'ancienne
   règle, **70** à celle d'aujourd'hui (règle stricte) ; 123 → 106 à la
   large.

**Et une correction de D101.** Deux des 33 chaînes de l'angle mort — « ».
Sa machine et ses inserts sont revenus. » et « insert(s) de « » — ne vont
qu'au terminal : ce sont les comptes rendus de « désactiver une piste » et de
« copier la chaîne d'inserts », que D101 avait décrits, à tort, comme « la
restauration d'un preset de piste ». L'angle mort français compte donc 18
chaînes, et l'ÉCRAN réel au moins **88**.

Outil : `ruff` et `mypy` propres ; aucune suite C++ concernée, aucun code de
l'application touché. `MainComponent` : 54 chaînes à l'inventaire.
**La leçon** : une règle qui ne regarde que l'instruction de la chaîne ne voit
pas où va la phrase qu'on assemble ; et la première correction de la règle
avait le même défaut qu'elle — un « ; » écrit dans une chaîne pris pour du
code.

### Phase D107 — A9 : les boîtes du démarrage, les titres des sélecteurs, et le nom d'une piste neuve (11/09/2026)

**LE LOT** (règle large : 23 ÉCRAN et 4 TABLE ; stricte : 19 et 2) :

- trois boîtes du démarrage — raccourcis illisibles, associations MIDI
  illisibles, associations écartées (`loadShortcuts`, `loadMidiLearnMappings`) ;
- la boîte d'un import audio dans un projet jamais enregistré
  (`importAudioFileOnNewTrack`) — son titre, « Projet jamais enregistré », est
  l'un des deux faux TABLE de D96 qui restaient ;
- sept titres de sélecteurs de fichier (dossier de la bibliothèque, table des
  raccourcis, reconstruire un morceau, dossier de la chaîne, enregistrement
  d'origine, import audio, export MIDI d'une piste) et « Réglages audio » ;
- la boîte d'un conflit de raccourci, l'erreur d'export MIDI d'une piste ;
- le menu des cibles d'une association MIDI (Transport, Lecture / arrêt,
  Arrêt, Enregistrement, Boucle, « Piste N — nom », Volume, Panoramique,
  Muet, Solo, « Départ A ») — six de ses libellés n'ont ni accent ni mot de la
  liste : l'inventaire ne les voit pas ;
- le nom par défaut d'une piste neuve (« Piste N », « Groupe N », « Audio N »)
  et celui que le rack montre pour une piste sans nom ; « Couleur du clip ».

**LA DÉCISION — le nom d'une piste neuve.** C'est une donnée : écrite dans le
projet, l'utilisateur la change. Mais c'est l'application qui la fabrique, et
une piste créée en anglais qui s'appelle « Piste 3 » est une phrase française
dans un projet anglais (D99 l'avait relevé). Le nom par défaut se donne donc
dans la langue de l'interface AU MOMENT de la création — « Track 3 » en
anglais, « Piste 3 » en français comme aujourd'hui. Une fois écrit, il ne
suit plus la langue : un nom ne se retraduit pas.

**LE BANC.** HOME isolé ; quatre lancements par langue, témoin (D107 sans ses
traductions, les boîtes par `montrerBoite()`) puis D107 : `demarrage` (les
préférences portent des raccourcis et des associations MIDI illisibles),
`ecartees` (une association valide sauf son contrôleur, 200), `import` (projet
neuf, `VSM_IMPORT_AUDIO`), `nom-piste` (projet neuf, « Ajouter une piste
MIDI »). Le reste — sélecteurs, conflit de raccourci, erreur d'export, menu
des cibles — ne s'ouvre pas au banc : vérifié par le code et la table.

**ATTENDU, écrit avant la mesure.**

1. **L'inventaire** : 0 ÉCRAN dans le lot ; règle stricte ÉCRAN **70 → 51**,
   TABLE 215 → 213 ; règle large ÉCRAN **106 → 83**, TABLE 251 → 247.
2. **Les boîtes** : 2, 1, 1 et 0 par lancement, dans les deux langues,
   témoin et D107. En anglais : « Unreadable shortcuts », « Unreadable MIDI
   mappings » (et l'erreur JSON, par son modèle), « 1 saved mapping(s) … were
   discarded », « Project never saved ».
3. **Le nom** : en anglais, la piste ajoutée s'appelle « Track 2 » (le
   témoin : « Piste 2 ») ; la première piste d'un projet neuf aussi, si elle
   naît par la même fonction. En français, « Piste 2 » des deux côtés.
4. **Le français** : identique au témoin.

**VU DANS LE TÉMOIN (D107), écrit avant la mesure d'après.** Huit lancements,
préférences intactes. Les boîtes comme prévu — 2, 1, 1, 0 —, françaises dans
les deux langues : « Raccourcis illisibles », « Associations MIDI
illisibles : JSON invalide : nombre attendu », « 1 association(s)
enregistrée(s) n'ont pas été relues… écartées plutôt que devinées »,
« Projet jamais enregistré : Un fichier audio importé est COPIÉ… ». La piste
ajoutée s'appelle « Piste 2 » en français ET en anglais — le défaut de la
décision, tel qu'annoncé.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Témoin puis D107, huit
lancements chacun, écran verrouillé ; préférences de l'utilisateur intactes
(`cmp`, deux séries).

1. **L'inventaire — tenu pour l'ÉCRAN, à un près pour TABLE.** ÉCRAN
   **70 → 51** (stricte), **106 → 83** (large) ; aucune chaîne du lot ne
   reste. TABLE 215 → 214 (stricte, 213 attendu), 251 → 248 (large, 247
   attendu) : les partants attendus sont partis (« Lecture / arrêt »,
   « Projet jamais enregistré », et à la large « Transport », « Associations
   MIDI ») ; une clé est ARRIVÉE, « Piste %1 » — celle du nom d'une piste
   neuve, choisie DANS une alternative passée à `tr()` (`tr(groupe ? … :
   u8"Piste %1")`). L'inventaire ne reconnaît un appel à `tr()` que juste
   avant la chaîne : elle est traduite, et comptée TABLE. Des chaînes que
   seule la règle large voyait, quatre étaient ici, toutes françaises
   (« Couleur du clip », « Associations MIDI illisibles », « Raccourcis
   illisibles », « Charger l'enregistrement d'origine ( ») : l'angle mort
   passe à 14, l'ÉCRAN réel à au moins 65.
2. **Les boîtes — tenu.** 2, 1, 1, 0, dans les deux langues, témoin et D107.
   En anglais : « Unreadable MIDI mappings : invalid JSON: number expected »,
   « Unreadable shortcuts : The custom shortcuts could not be read back: the
   original ones are restored. », « MIDI mappings : 1 saved mapping(s) could
   not be read back: … discarded rather than guessed. », « Project never
   saved : An imported audio file is COPIED into the project folder. Save
   the project first (Ctrl+S). »
3. **Le nom — tenu pour la piste ajoutée.** « Track 2 » en anglais (le
   témoin : « Piste 2 »), « Piste 2 » en français des deux côtés. La première
   piste d'un projet neuf ne naît PAS de cette fonction : elle s'appelle
   « Bass » dans les deux langues — son nom vient du projet de départ, et il
   est le même partout.
4. **Le français — tenu.** Identique au témoin dans les quatre cas, boîtes et
   listes de la fenêtre principale.

**Ce que le banc n'ouvre pas** : les sept sélecteurs de fichier, le conflit de
raccourci, l'erreur d'export MIDI, le menu des cibles d'une association,
« Couleur du clip », le nom de repli du rack. Leurs textes sont des clés.

**Piste pour plus tard** : des infobulles françaises écrites SANS leurs
accents (« Armer la piste : elle recoit… », « Aucune piste armee »,
« Correlation de phase … disparait »). Leur anglais est juste, leur français
est faux — la classe des huit de D101.

La table passe à **1 293** paires (+23). `VintageSynthMidiStudio` et
`vsm-ui-preview` compilent ;
suites C++ vertes (330 cœur, 1 291 audio, 297 interchange, 25 CLAP, 11
panneaux). `MainComponent` : 35 chaînes à l'inventaire.

### Phase D108 — A9 : l'original de l'écoute A/B (11/09/2026)

**LE LOT.** Charger l'original qui sert de référence à l'écoute A/B : le
sélecteur (`loadReferenceAudio`), la boîte « Enregistrement illisible »
(`setReferenceAudioFile`), les erreurs du décodeur
(`audio/ReferenceAudioLoader.cpp`), et la ligne que le menu Fichier montre une
fois l'original chargé (« bip.wav  --  WAV natif, 48.0 kHz, mono, 0:00 »,
`publierReference`, et son suffixe « canal gauche de comparaison.wav » dans
`chargerOriginalDuProjet`). À l'inventaire : 6 ÉCRAN (stricte), 10 (large) ;
plusieurs lui échappent (« lecteur JUCE : », « formats acceptes », « WAV
natif »).

**LE BANC.** Le sélecteur passe par le crochet de D102 (`prendreLeFichierDeBanc`),
qu'une variable au nom juste désigne désormais : `VSM_FICHIER` (le même que
`VSM_PLUGIN`). Le geste reste le menu Fichier. Trois cas, français puis
anglais, témoin (D108 sans ses traductions : la boîte par `montrerBoite()`,
le crochet en place) puis D108 : `wav-casse` (un `.wav` qui n'en est pas un :
les deux lecteurs refusent, la boîte porte les deux refus), `flac-casse` (le
lecteur de JUCE seul), `bon` (`bip.wav` : la ligne du menu, lue par
`VSM_MENU_LISTE`).

**LES DÉCISIONS.**

1. **Les accents rendus** aux erreurs du décodeur (« formats acceptés »,
   « fichier sans échantillon », « trop long pour être chargé », « fréquence
   d'échantillonnage », « mémoire ») et à « stéréo » : différence française
   voulue, déclarée ici. Ces littéraux passent par `juce::String(u8"…")` — un
   littéral accentué lu comme `const char*` sort abîmé (D99).
2. **Les deux phrases composées deviennent des phrases entières** avec leur
   `%1` (« format non reconnu (formats acceptés : %1) », « … en mémoire (%1
   min) ») : leur modèle dans `kModeles` est alors le littéral lui-même. La
   phrase à deux lignes (« lecteur WAV du moteur : … / lecteur JUCE : … »)
   garde deux morceaux, chacun traduit par son modèle (`%P1`).
3. **La traduction se fait à l'affichage** (`trPhrase` dans la boîte, `tr()`
   pour le nom du décodeur, « stéréo » et le suffixe) : le décodeur reste
   français, les outils qui l'emploient aussi.

**ATTENDU, écrit avant la mesure.**

1. **L'inventaire** : ÉCRAN **51 → 45** (stricte), **83 → 74** (large) —
   une chaîne reste à la large, « lecteur WAV du moteur : %1 », traduite par
   son modèle `%P1` que l'inventaire ne sait pas rapprocher ; TABLE +7 aux
   deux règles (les phrases entières du décodeur deviennent des clés ou des
   modèles).
2. **Les boîtes** : 1, 1, 0 ; en anglais « Unreadable recording : engine WAV
   reader: RIFF/WAVE header missing / JUCE reader: unrecognised format
   (accepted formats: …) », puis « unrecognised format (…) » seul.
3. **La ligne du menu** (`bon`) : « bip.wav  --  native WAV, 48.0 kHz, mono,
   0:00 » en anglais.
4. **Le français** : identique au témoin, sauf « acceptes » → « acceptés »
   dans les deux boîtes (décision 1).

**VU DANS LE TÉMOIN (D108), écrit avant la mesure d'après.** Six lancements,
un sélecteur sauté chacun, préférences intactes. `wav-casse` : une boîte,
« Enregistrement illisible : lecteur WAV du moteur : en-tête RIFF/WAVE absent /
lecteur JUCE : format non reconnu (formats acceptes : WAV file, AIFF file,
FLAC file, Ogg-Vorbis file, MP3 file) » ; `flac-casse` : une boîte, le second
refus seul ; `bon` : aucune boîte, et le menu Fichier porte « bip.wav  --  WAV
natif, 48.0 kHz, mono, 0:00 ». En anglais, tout est français — le défaut, tel
qu'annoncé. Les noms de formats (« WAV file »…) viennent de JUCE : ils sont
anglais dans les deux langues, et le restent.

**ÉTAT AU COMMIT (D108, 11/09/2026) — phase NON terminée.** Le correctif est
appliqué et compile (`VintageSynthMidiStudio`, 0 erreur). L'inventaire est
mesuré et tient l'attendu 1 au chiffre près : ÉCRAN 51 → 45 (stricte),
83 → 74 (large), la seule chaîne restante étant celle prévue (« lecteur WAV
du moteur : %1 ») ; TABLE 214 → 221 et 248 → 255 ; la table à 1 301 paires,
95 modèles, sans doublon. **Restent à faire** : le banc d'après (attendus 2 à
4 — boîtes et ligne du menu en anglais, français identique au témoin), les
suites C++, `vsm-ui-preview`, et le résultat écrit ici.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Témoin puis D108, six
lancements chacun, écran verrouillé ; préférences de l'utilisateur intactes
(`cmp`, deux séries).

1. **L'inventaire — tenu au chiffre près** (écrit à l'état au commit) : ÉCRAN
   51 → 45 (stricte), 83 → 74 (large), la seule chaîne restante étant la
   prévue ; TABLE 214 → 221, 248 → 255. Des 14 chaînes de l'angle mort,
   quatre étaient ici (« Enregistrement illisible », « (canal gauche de
   comparaison.wav) », « frequence d'echantillonnage… », « lecteur WAV du
   moteur : »), toutes traduites — la dernière reste comptée à la règle
   large, traduite par son modèle. L'angle mort passe à 10, l'ÉCRAN réel à
   au moins 55.
2. **Les boîtes — tenu.** 1, 1, 0, dans les deux langues, témoin et D108. En
   anglais : « Unreadable recording : engine WAV reader: RIFF/WAVE header
   missing / JUCE reader: unrecognised format (accepted formats: WAV file,
   AIFF file, FLAC file, Ogg-Vorbis file, MP3 file) », puis « unrecognised
   format (…) » seul. (L'analyse relevait « format(s) » : des mots anglais.)
3. **La ligne du menu — tenu.** « bip.wav  --  native WAV, 48.0 kHz, mono,
   0:00 » en anglais.
4. **Le français — tenu.** Identique au témoin, à l'accent déclaré près
   (« acceptes » → « acceptés », dans les deux boîtes) ; listes de la fenêtre
   principale identiques (180 textes). « stéréo » n'est pas passé au banc :
   `bip.wav` est mono.

La table passe à **1 301** paires (+8), **95** modèles (+4).
`VintageSynthMidiStudio` et `vsm-ui-preview` compilent ; suites C++ vertes (330 cœur, 1 291 audio,
297 interchange, 25 CLAP, 11 panneaux). `MainComponent` : 34 chaînes à
l'inventaire.

### Phase D109 — A9 : les noms des pièces de batterie (11/09/2026)

**LE LOT.** `drumVoiceName` (`ui/DrumVoiceNames.h`) nomme la pièce qu'une note
déclenche sur une piste de batterie — 37 noms distincts, en français
(« grosse caisse », « charleston fermé », « conga aiguë étouffée »…), selon
la machine (TR-808, TR-909, Drums, FM Drums, Perc) ou la convention General
MIDI. Deux usages : le piano roll les dessine sur son clavier, et « Éclater
par hauteur » en fait les noms des pistes qu'il crée. À l'inventaire des
en-têtes : 19 ÉCRAN, les noms accentués ; les autres (« grosse caisse »,
« caisse claire », « tom grave »…) n'ont ni accent ni mot de sa liste, et
lui échappent. Aucun n'est une clé.

**LES DÉCISIONS.**

1. **La table reste française : c'est la clé.** La traduction se fait aux
   deux usages — `tr()` au piano roll (il redessine à chaque peinture, donc
   suit la bascule) ; `tr()` au nom d'une piste éclatée, donnée que
   l'application fabrique, donc dans la langue du moment, comme en D107.
2. **Douze noms sont les mêmes en anglais** (clap, cowbell, crash, crash 2,
   ride, ride 2, rim, splash, maracas, claves, china, tom) : pas de clé.
   **25 clés.**

**LE BANC.** Le témoin est le binaire de HEAD, déjà construit. Copie de
demo-project, piste « Drums » (`vsm.tr909`) choisie ; deux cas, français
puis anglais : `eclater` (le menu Piste, la liste des pistes lue par
`VSM_TEXTES_LISTE`, et la boîte de D96) ; `pianoroll` (`VSM_VUE=pianoroll`,
photographié).

**ATTENDU, écrit avant la mesure.**

1. **L'inventaire des en-têtes** : ÉCRAN **19 → 0**, TABLE 19 → 38 ; les
   `.cpp` ne bougent pas (ÉCRAN 45 stricte, 74 large).
2. **`eclater`** : en anglais, les pistes créées s'appellent « snare »,
   « closed hi-hat »… — les pièces que la piste joue ; au témoin, « caisse
   claire », « charleston fermé ». En français, identiques.
3. **`pianoroll`** : en anglais, le clavier de la piste de batterie dit
   « kick », « snare »…
4. **Le français** : identique au témoin.

**VU DANS LE TÉMOIN (D109), écrit avant la mesure d'après.** Quatre
lancements (le binaire de HEAD), préférences intactes. « Éclater par
hauteur » crée 3 pistes ; la liste des pistes montre « grosse caisse »,
« tom aigu », « tom grave », « clap » — en français dans les deux langues, le
défaut tel qu'annoncé, alors que la boîte de D96 dit déjà « Split by pitch :
3 track(s) created… ». « clap » est l'un des douze noms identiques.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026).** Témoin (le binaire de
HEAD) puis D109, quatre lancements chacun, écran verrouillé ; préférences de
l'utilisateur intactes (`cmp`, deux séries).

1. **L'inventaire des en-têtes — tenu.** ÉCRAN **19 → 0**, TABLE 19 → 38 ;
   les `.cpp` n'ont pas bougé (ÉCRAN 45 stricte, 74 large). Les noms sans
   accent (« grosse caisse », « caisse claire », « tom grave »…), que
   l'inventaire ne voyait pas, sont traduits de même : ils sont dans les
   25 clés.
2. **`eclater` — tenu.** En anglais, les pistes créées s'appellent « kick »,
   « high tom », « low tom », « clap » (au témoin : « grosse caisse », « tom
   aigu », « tom grave », « clap ») ; « clap » est l'un des douze noms
   identiques — l'analyse le relevait à tort. En français, identiques ; la
   boîte de D96 inchangée.
3. **`pianoroll` — tenu.** Le clavier de la piste de batterie dit « kick » en
   anglais, où le témoin disait « grosse caisse » — vu sur la photo.
4. **Le français — tenu.** Identique au témoin dans les deux cas, boîte et
   liste de la fenêtre principale.

La table passe à **1 326** paires (+25). `VintageSynthMidiStudio` et
`vsm-ui-preview` compilent ;
suites C++ vertes (330 cœur, 1 291 audio, 297 interchange, 25 CLAP, 11
panneaux). `MainComponent` : 34 chaînes à l'inventaire (inchangé).

### Phase D110 — A9 : les boîtes du départ d'une prise (11/09/2026)

**LE LOT.** `startRecording` et ses quatre boîtes — aucune piste armée /
aucune carte son ; plusieurs pistes audio armées ; projet jamais enregistré,
le dernier des faux TABLE que D96 avait nommés ; enregistrement audio
impossible —, avec les erreurs que la dernière montre : celle du moteur
(« Aucune entree audio ouverte… », `audio/AudioEngine.cpp`) et les trois du
rédacteur de prise (`audio/DiskRecorder.cpp`), du français écrit sans ses
accents. À l'inventaire : 9 ÉCRAN et 1 TABLE (stricte), 12 et 1 (large).

**LE BANC, SANS SON.** La mesure de latence et une prise véritable jouent et
enregistrent du son : pas de banc pour elles tant que l'utilisateur peut être
devant la machine. Mais deux des boîtes refusent AVANT tout décompte et toute
capture : deux pistes audio armées ; une piste audio armée dans un projet
jamais enregistré. Projet neuf, pistes audio ajoutées par le menu, armées par
un geste de banc neuf (`VSM_GESTE_PISTE=armer` : le bouton R de la ligne, par
son clic, ce que fait la souris), puis F9. L'écoute de l'entrée reste
manuelle — les préférences du banc ne portent pas `monitoringMode`, et en
mode manuel armer ne l'allume pas (`applyMonitoringMode`) : le micro ne va
pas aux haut-parleurs. Témoin : le geste et les boîtes par `montrerBoite()`,
sans traduction.

**LES DÉCISIONS.**

1. **Les accents rendus** au moteur et au rédacteur (« entrée »,
   « Réglages », « Fréquence », « écrire », « refusé », « à ») — différence
   française déclarée ; aucune de ces phrases ne passe au banc.
2. **Leurs phrases composées deviennent des phrases entières** à `%1`, `%2`,
   comme en D108 : le modèle est le littéral lui-même.
3. **« Aucune piste armée » et « Aucune carte son » sont des gardes** : le
   bouton Rec est grisé dans ces deux cas, et F9 ne fait que l'écrire au
   terminal. Traduites, vérifiées par le code.

**ATTENDU, écrit avant la mesure.**

1. **L'inventaire** : 0 ÉCRAN dans le lot ; stricte ÉCRAN **45 → 36**, TABLE
   221 → 224 ; large ÉCRAN **74 → 62**, TABLE 255 → 258 — « Projet jamais
   enregistré » passe par `tr()` et quitte TABLE ; les quatre phrases du
   moteur et du rédacteur y entrent, comme clés ou modèles.
2. **Les boîtes** : 1 et 1, dans les deux langues, témoin et D110. En
   anglais : « Several audio tracks armed : One input, one take: … »,
   « Project never saved : An audio take is a FILE, … ».
3. **Le français** : identique au témoin.

**PREMIER TÉMOIN (D110), écrit avant la mesure d'après : 0 boîte sur 4.**
Dans les quatre lancements, F9 a trouvé le bouton Rec gris (« Enregistrer
(F9) : le bouton Rec est grisé -- pas de carte son ouverte, ou aucune piste
armée »). La carte était ouverte (« 48.0 kHz »), les pistes créées
(« Bass », « Audio », « Audio 2 ») : c'est l'armement qui manquait. Le geste
de banc passait par `Button::triggerClick()`, qui POSTE le clic dans la file
des messages — il agissait après la touche F9 qui le suivait dans le même
tour du banc. **Correction de l'outil** (témoin et après) : basculer l'état
du bouton R puis appeler son `onClick`, tout de suite — ce que fait un clic,
sans la file. Le témoin est reconstruit et relancé.

**SECOND TÉMOIN (11/09, 16:50) : 4 boîtes sur 4.** Binaire du 11/09 14:57
(l'outil corrigé, les chaînes d'avant) : « Plusieurs pistes audio armées :
Une seule entrée, une seule prise… » pour les deux pistes armées, « Projet
jamais enregistré : Une prise audio est un FICHIER… » pour la piste seule —
en français dans les deux langues, ce qu'on attend d'un témoin sans
traduction. Plus aucun « bouton Rec grisé ». Préférences de l'utilisateur
intactes (`cmp` contre une copie prise juste avant la série).

*Précision de lieu, qui compte ce jour-là* : `AudioEngine.cpp` et
`DiskRecorder.cpp` sont ceux d'`app/Source/audio/`, pas du moteur `audio/`.
L'épreuve *Children* (CDC multipiste § 12) court pendant cette phase sur
`build/tools/vsm-render` ; `moteur_perime` ne regarde que `audio/`, `core/`
et `interchange/` — D110 ne périme donc pas le moteur qu'elle mesure, et la
compilation se fait à `-j 2`, la cible de l'application seule.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026, banc à 16:52).** Binaire du 11/09
16:52:28 (code 0), même banc, même outil que le second témoin.

1. **L'inventaire — TENU au chiffre** : stricte ÉCRAN **45 → 36**, TABLE
   221 → 224 ; large ÉCRAN **74 → 62**, TABLE 255 → 258. Plus aucune ligne
   de `startRecording`, de `DiskRecorder.cpp` ni de l'erreur du moteur à
   l'inventaire.
2. **Les boîtes — TENU** : 1 et 1 dans les deux langues. En anglais :
   « Several audio tracks armed : One input, one take: arm only one audio
   track at a time. Writing the same signal to two files would only double
   the space used. » et « Project never saved : An audio take is a FILE, and
   the format stores a project's files by path relative to its folder… Save
   the project first (Ctrl+S); the take will go into its audio/ subfolder. »
   Aucun mot français restant (filtre de `analyse-d110.py`).
3. **Le français — TENU** : les deux boîtes identiques au témoin, et les
   textes de la fenêtre aussi (188 et 160 lignes, dans les deux langues).
   Préférences de l'utilisateur intactes.

La photo (`jamais-en.png`) montre la fenêtre en anglais — « A/B monitoring:
no original », « (no file — arm and record) » — sans la boîte : c'est
attendu, une boîte se lit par `VSM_BOITE` au moment où elle est demandée,
et la photo ne prend que la fenêtre principale.

La table passe à **1 336** paires (+10) et **97** modèles (+2 : « Impossible
d'écrire %1 », « Format WAV refusé pour %1 canal/canaux à %2 Hz. »).
`VintageSynthMidiStudio` et suites C++ vertes, compilées à `-j 2` pendant
l'épreuve (330 cœur, 1 291 audio, 297 interchange, 25 CLAP, 11 panneaux) ;
`build/tools/vsm-render` n'a pas été touché. `MainComponent` : **34 → 27**
chaînes à l'inventaire. Le dernier des faux TABLE nommés par D96 (« Projet
jamais enregistré ») passe par `tr()`.

### Phase D111 — A9 : les phrases composées de `MainComponent` hors de l'enregistrement (11/09/2026)

**LE LOT.** Sept des 27 chaînes de `MainComponent` à l'inventaire (stricte),
en trois sites :

- **les réserves d'effet d'un bus de départ** (`loadSendEffects`, deux
  phrases en quatre morceaux) — « bus de départ « Rev » : effet « … »
  inconnu, non appliqué », « … : réglage inconnu « … » ». Elles S'AFFICHENT
  DÉJÀ traduites : D89 a posé leurs modèles, et le volet traduit la ligne à
  l'affichage. L'inventaire les compte parce que le source les COMPOSE par
  `+` : le littéral n'est pas la clé. C'est un faux ÉCRAN, et il ment sur ce
  qui reste à faire ;
- **l'erreur d'« Aller à la mesure »** (« « 17,x » n'est pas une position :
  attendu… ») — le titre est une clé depuis D91, le message non ;
- **les deux boîtes d'« Assembler les prises »** — tronçons sans note, et
  « N notes composées ».

**LES DÉCISIONS.**

1. **Les phrases composées deviennent des phrases entières à `%1`, `%2`**
   (comme D108, D110) : pour le bus de départ, le littéral EST le modèle de
   D89, et la donnée écrite (`VSM_OUVERTURE`, le ton ambre) ne change pas
   d'un octet — le ton se lit sur le français depuis D89.
2. **Les titres passent par `tr()`** avec leurs clés existantes ; les trois
   messages entrent dans la table (« %1 notes composées… » comme gabarit
   rempli APRÈS `tr()`, à la manière de « Impossible d'écrire %1 »).
3. **Pas de geste de banc neuf.** L'erreur d'« Aller à la mesure » ne naît
   que d'un texte tapé dans la boîte, et les deux boîtes d'assemblage d'un
   tracé de tronçons à la souris : vérifiées par le code, et dit.

**ATTENDU, écrit avant la mesure (11/09/2026, commit `0199830` à 16:58:11 — l'en-tête y portait « 17:10 », une heure à venir, rectifiée ici).**

1. **L'inventaire** : stricte ÉCRAN **36 → 29**, `MainComponent` **27 →
   20** ; les autres catégories se publient, sans chiffre promis.
2. **Le banc** (le projet abîmé exprès de D89 : un insert et un bus de
   départ de type inconnu, un insert et un bus portant des réglages
   inconnus), ouvert en français et en anglais, témoin (binaire de D110) et
   D111 : les lignes `VSM_OUVERTURE` (la donnée) et `VSM_RAPPORT` (ce que le
   volet affiche) **identiques au témoin, dans les deux langues** — en
   anglais, « send bus “Rev”: effect “…” unknown, not applied » avant comme
   après. Un écart dirait que le littéral réécrit n'est pas le modèle.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026, banc à 17:01 ; l'en-tête portait « 17:05 », une heure à venir).** Binaire du 11/09
17:01:06 (code 0) contre celui de D110 (16:52:28), même banc, `HOME` de
brouillon.

1. **L'inventaire — TENU** : stricte ÉCRAN **36 → 29**, `MainComponent`
   **27 → 20**. Publiés sans promesse : TABLE 224 → 223 (les deux titres
   entrent dans `tr()`), large ÉCRAN 62 → 55, TABLE 258 → 257.
2. **Le banc — TENU** : 6 lignes `VSM_OUVERTURE` et 12 lignes
   `VSM_RAPPORT` par langue, **identiques octet pour octet** au témoin, en
   français comme en anglais (`cmp`). La donnée : « bus de départ « Rev » :
   effet « reverb-a-plaque-de-1974 » inconnu, non appliqué » ; l'affiché en
   anglais : « send bus “Rev”: effect “reverb-a-plaque-de-1974” unknown, not
   applied ». Préférences intactes.

La table passe à **1 339** paires (+3). Suites C++ vertes à `-j 2` (330,
1 291, 297, 25, 11) ; `build/tools/vsm-render` non touché (15:00:36).

### Phase D112 — A9 : les boîtes de l'enregistrement qu'aucun banc n'atteignait (11/09/2026)

**LE LOT.** Les douze dernières chaînes de `MainComponent` qui touchent à
l'enregistrement, en cinq boîtes : la file de capture qui déborde (« Notes
perdues à l'enregistrement ») ; la mesure de latence — pas d'entrée (« Mesure
impossible »), rien de retrouvé (« Rien n'est revenu », avec sa netteté), le
résultat (« Latence mesurée », quatre nombres en trois paragraphes) ; le
disque qui n'a pas suivi (« La prise a perdu N bloc(s)… »).

**POURQUOI UN BANC NEUF, ET LEQUEL.** D110 a laissé deux gardes « vérifiées
par le code ». Ici ce serait les cinq boîtes : la latence ÉMET un balayage
dans les haut-parleurs, et le débordement d'une file ou d'un disque ne se
provoque pas. Mais ce que la boîte DIT ne dépend d'aucun son. Chaque boîte
devient une fonction (`boiteNotesPerdues`, `boiteMesureImpossible`,
`boiteRienNestRevenu`, `boiteLatenceMesuree`, et `signalerDisqueTropLent`
qui l'était déjà), appelée par le chemin réel ET par un geste de banc neuf,
`VSM_BOITE_ESSAI=perdues;impossible;rien;latence;disque`, avec des chiffres
fixes (netteté 3,2 ; 590 échantillons à 48 kHz, netteté 42,5 ; 3 blocs).
Le banc ne fait rien d'autre que montrer : ni son, ni latence retenue, ni
préférence écrite. Les boîtes passent par `montrerBoite()` (D95), qui les
écrit au terminal quand elles sont demandées.

**LES DÉCISIONS.**

1. **Le témoin est le même code sans la traduction** : les fonctions, le
   geste et `montrerBoite()`, les phrases d'avant.
2. **Les phrases composées deviennent des modèles à `%1`…`%4`** ; le
   résultat de la latence reste trois paragraphes, chacun sa clé (« Aller-
   retour : %1 ms (%2 échantillons à %3 kHz) », « Netteté du pic : %1 », le
   paragraphe sur les prises MIDI) — une clé qui porterait `\n\n` serait la
   seule de la table.
3. **Les nombres ne changent pas de forme** d'une langue à l'autre
   (`juce::String(double, n)`, point décimal) : l'anglais et le français
   doivent porter les mêmes chiffres.

**ATTENDU, écrit avant la mesure.**

1. **L'inventaire** : stricte ÉCRAN **29 → 17**, `MainComponent` **20 → 8**.
2. **Le banc** : 5 boîtes sur 5 dans chaque langue, témoin et D112 ; le
   témoin en français dans les deux langues ; D112 en anglais sans mot
   français, **mêmes nombres** qu'en français (3.2 ; 12.29 ms ; 590 ; 48.0 ;
   42.5 ; 3) ; le français de D112 identique au témoin, boîte pour boîte.
3. **Préférences intactes**, et `latenceAllerRetour` absent des préférences
   du banc après la série (le geste ne passe pas par la mesure).

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026, banc à 17:12).** Témoin :
binaire du 11/09 17:08:08 (fonctions, geste, `montrerBoite()`, phrases
d'avant) ; D112 : 17:10:07. Même banc, `HOME` de brouillon.

1. **L'inventaire — TENU** : stricte ÉCRAN **29 → 17**, `MainComponent`
   **20 → 8**. Publiés sans promesse : TABLE 223 (inchangé), large ÉCRAN
   55 → 39, TABLE 257 → 256, COMMANDE 25 → 26 (le jeton de refus du geste
   neuf).
2. **Le banc — TENU** : 5 boîtes sur 5 dans chaque langue, témoin et D112.
   Le témoin parle français dans les deux langues ; D112 en anglais, aucun
   mot français, **les mêmes nombres** que le français boîte pour boîte :
   « Nothing came back : The emitted sweep was not found in the input
   (sharpness 3.2)… », « Latency measured : Round trip: 12.29 ms (590
   samples at 48.0 kHz) / / Peak sharpness: 42.5 / / AUDIO takes are now
   moved earlier by that amount… », « The disk did not keep up : The take
   lost 3 block(s)… ». Le français de D112 est identique au témoin.
3. **TENU** : préférences de l'utilisateur intactes (`cmp`), et
   `latenceAllerRetour` absent des préférences du banc dans les quatre
   lancements.

La table passe à **1 351** paires (+12). Suites C++ vertes à `-j 2` (330,
1 291, 297, 25, 11) ; `build/tools/vsm-render` non touché (15:00:36).
Des 8 chaînes de `MainComponent` qui restent, **cinq ne s'affichent pas** :
le mot « libellé » du banc de D94, le mot « non appliqué » que le volet
cherche pour colorer une ligne (D89 : le ton se lit sur le français), et
trois chemins de fichiers (`gel/piste-`, `audio/report-…`). Restent le
volet de reconstruction (« Terminé — le projet est ouvert… », « — parité
des pistes ») et le texte d'attente des notes du projet.

### Phase D113 — un plugin illisible disparaissait du balayage sans un mot (11/09/2026)

**LE DÉFAUT, LU DANS LE CODE EN PRÉPARANT LA TRADUCTION DE SES PHRASES.**
Le balayage ouvre chaque fichier de plugin dans un processus enfant
(`--scan-plugin`), qui écrit une ligne par plugin trouvé. L'enfant appelle
`scanClapFile` / `scanVst3File`, qui rendent l'ERREUR de chargement
(« chargement impossible : … », « fabrique de plugins absente dans … ») —
et `scanOneFileInThisProcess` la jette : `juce::ignoreUnused(erreur)`.
L'enfant sort donc en 0 sans une ligne ; le parent y lit un succès à zéro
plugin. Le fichier n'entre ni au catalogue, ni dans la liste des fautifs
que la boîte « Balayage terminé » nomme. C'est exactement ce que le
commentaire de `PluginScanner::run` dit empêcher : « SIGNALÉ, PAS TU. Un
fichier qui disparaît du balayage sans un mot laisse l'utilisateur chercher
pourquoi son plugin n'apparaît pas. » Seuls les deux cas extrêmes — l'enfant
qui TOMBE, celui qui ne rend jamais la main — étaient dits.

**LE BANC.** Un `HOME` de brouillon (les dossiers système de plugins sont
vides sur ce poste), dont `~/.clap` porte quatre faux plugins écrits pour
l'occasion (`faux.c`, 30 lignes, la structure d'entrée recopiée de la
spécification CLAP) et un vrai :

| fichier | ce qu'il fait | ce que le code prédit |
|---|---|---|
| `faux-texte.clap` | un fichier texte | `dlopen` refuse → « chargement impossible : … » |
| `faux-vide.clap` | une bibliothèque, `clap_entry`, aucune fabrique | « fabrique de plugins absente dans … » |
| `faux-tombe.clap` | `init()` appelle `abort()` | l'enfant tombe |
| `faux-dort.clap` | `init()` dort 60 s | tué à 20 s |
| `vsm-instruments.clap` | le vrai, copié | 24 plugins, le témoin sain |

Balayage par le menu (« Rechercher les plugins installés... »), la boîte lue
par `VSM_BOITE`, le catalogue `plugins.json` du `HOME` de brouillon relu.
*Premier essai du témoin, sans valeur* : photo prise à 35 s, balayage pas
fini (seule la boîte « Balayage lancé » est venue) ; délai porté à 75 s.

**LES DÉCISIONS.**

1. **L'enfant dit pourquoi.** Quand un fichier ne rend aucun plugin, il écrit
   une ligne `VSM_ECHEC_BALAYAGE<tab>raison` — l'erreur de l'hôte, ou « aucun
   plugin dans ce fichier » s'il n'y en a pas. Elle ne peut pas passer pour
   un plugin : `decodeScanLine` exige son préfixe et six champs. Rien ne
   change dans `interchange/` (le moteur de l'épreuve *Children* le lit).
2. **Le parent la range dans les fautifs** : sortie en 0 et zéro plugin =
   fautif, avec la raison de l'enfant.
3. **Les phrases du balayage retrouvent leurs accents** (« répondu »,
   « tombé ») et deviennent des modèles, avec celles de l'hôte CLAP
   (« chargement impossible : %1 », « fabrique de plugins absente dans
   %1 »…) : la raison passe déjà par `trPhrase` à l'affichage. Les anciennes
   formes sans accents restent des modèles aussi : un catalogue écrit avant
   D113 garde ses fautifs d'un balayage à l'autre, et ils se relisent.

**ATTENDU, écrit avant la mesure.**

1. **Le témoin** (binaire de D112) : 24 plugins ; **2 fautifs** —
   `faux-tombe` (« le processus de balayage est tombe (code … ») et
   `faux-dort` (« le plugin n'a pas repondu en 20 secondes ») ;
   **`faux-texte` et `faux-vide` absents des deux listes** : le défaut,
   mesuré.
2. **D113** : 24 plugins ; **4 fautifs**, chacun avec sa raison — les deux
   du témoin, accentués, plus « chargement impossible : … » pour
   `faux-texte` et « fabrique de plugins absente dans … » pour `faux-vide`.
   La boîte « Balayage terminé » dit « 4 fichier(s) n'ont pas pu être
   lus » dans les deux langues, et en anglais les quatre raisons sont
   traduites.
3. **Rien d'autre ne bouge** : le vrai plugin donne les mêmes 24 entrées,
   dans le même ordre, témoin et D113.

**LE TÉMOIN RÉFUTE L'ATTENDU N° 1, ET DANS LE MAUVAIS SENS : 0 FAUTIF SUR 4.**
Binaire de D112, deux langues : 24 plugins, « Balayage terminé : 24
instrument(s), 0 effet(s) trouvés. », et le catalogue porte `faulty: []`.
Même le plugin qui TOMBE et celui qui DORT passent pour des succès à zéro
plugin — et le balayage a duré plus de 60 s, le temps du sommeil entier,
quand le délai par fichier est de 20 s. Deux causes de plus que celle que
j'avais lue, toutes deux dans JUCE (`build/_deps/juce-src`, la copie que ce
build compile), et toutes deux vérifiées dans sa source avant d'être
écrites :

- **un enfant tué par un signal rend « 0 »** : `getExitCode()`
  (`juce_SharedCode_posix.h:1237`) ne lit le code que si `WIFEXITED` ; sinon
  il rend 0. `abort()` dans un plugin — le cas pour lequel « toute cette
  machinerie existe », dit le commentaire — se lit donc comme une sortie
  propre ;
- **le délai de 20 s ne s'applique jamais** : `scanInChildProcess` appelle
  `readAllProcessOutput()` AVANT `waitForProcessToFinish(20000)`, et la
  lecture boucle sur un `fread` bloquant jusqu'à la fin du flux
  (`juce_ChildProcess.cpp:77`), c'est-à-dire jusqu'à la mort de l'enfant. Un
  plugin qui attend un serveur de licence injoignable bloque le balayage
  aussi longtemps qu'il attend — pour toujours s'il le faut.

**DÉCISIONS AJOUTÉES, écrites avant la correction.**

4. **L'enfant écrit dans un FICHIER, plus sur un tube** :
   `--scan-plugin <plugin> <sortie>`. Le parent attend AVEC son délai, tue
   s'il le faut, puis lit le fichier. Plus de lecture bloquante, et plus de
   tube qui se remplit si un plugin bavarde (sa sortie standard n'est plus
   lue par personne).
5. **Une sentinelle de fin, `VSM_FIN_BALAYAGE`**, écrite par l'enfant en
   dernier. Absente, l'enfant est mort en route : « tombé », avec son code
   s'il en a rendu un, « sans code de sortie » sinon — ce qui est, sur ce
   système, la signature d'un signal. On ne demande plus à `getExitCode()`
   ce qu'il ne sait pas dire.

**ATTENDU AJOUTÉ** : le balayage des cinq fichiers finit en **moins de
35 s** après le lancement (le dormeur tué à 20 s), contre plus de 60 s au
témoin — mesuré par l'heure du dernier enregistrement du catalogue, que le
balayage réécrit après chaque fichier. Les attendus n° 2 et 3 sont
inchangés : 4 fautifs, chacun sa raison, 24 plugins.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026, banc à 17:34).** Binaire du
11/09 17:32:57 (code 0) contre celui de D112 (17:10:07), même banc, deux
langues, `HOME` de brouillon.

1. **Le témoin** — réfuté plus haut : 0 fautif sur 4, dernier enregistrement
   du catalogue à **+61 et +62 s**.
2. **D113 — TENU** : 24 plugins et **4 fautifs**, chacun sa raison :

   | fichier | raison écrite au catalogue | ce que la boîte dit en anglais |
   |---|---|---|
   | `faux-texte` | « chargement impossible : … » (le message de `dlopen`) | « cannot load: … » |
   | `faux-vide` | « fabrique de plugins absente dans … » | « no plugin factory in … » |
   | `faux-tombe` | « le processus de balayage est tombé sans code de sortie (un signal, le plus souvent un plantage) » | « the scanning process crashed without an exit code… » |
   | `faux-dort` | « le plugin n'a pas répondu en 20 secondes » | « the plugin did not respond within 20 seconds » |

   « 4 fichier(s) n'ont pas pu être lus » / « 4 file(s) could not be read »,
   et aucun mot français dans la boîte anglaise, chemins mis à part.
3. **TENU** : les 24 plugins du vrai fichier, mêmes identifiants, même
   ordre, témoin et D113.
4. **L'attendu ajouté — TENU** : dernier enregistrement du catalogue à
   **+22 s** dans les deux langues, contre +61 et +62 s : le dormeur est tué
   à 20 s.

Ce que le banc ne couvre pas, dit : l'échec au LANCEMENT de l'enfant
(« impossible de lancer le processus de balayage ») et un plugin VST3 —
aucun faux `.vst3` n'a été écrit ; « aucun plugin VST3 dans « … » » a son
modèle depuis D102. Table : **1 354** paires (+3), **106** modèles (+9 ;
« chargement impossible : %1 » et le message VST3 existaient déjà, D102).
Inventaire : stricte ÉCRAN **17 → 14**, TABLE 223 → 228 ; large 39 → 36,
261. Il reste au balayage « Balayage des plugins », le NOM du fil
d'exécution, qu'aucun écran ne montre. Suites C++ vertes à `-j 2` (330,
1 291, 297, 25, 11) ; `build/tools/vsm-render` non touché ; préférences
intactes.

### Phase D114 — A9 : la reconstruction lancée depuis l'application, en anglais (11/09/2026)

**LE LOT.** Ce que l'application montre autour d'une reconstruction :

- **« Reconstruction indisponible »** — la boîte, et les RAISONS qu'elle
  montre, écrites par `interchange/ReconstructionChain.cpp` (« le dossier de
  la chaîne d'analyse indiqué dans les préférences ne contient pas
  reconstruire.py (…) », « l'environnement Python de la chaîne n'a pas été
  créé (aucun .venv dans …) »…) avec leurs remèdes. Le titre n'est même pas
  à l'inventaire strict : sans accent ni mot de la liste, il est dans
  l'angle mort de D101 ;
- **le volet de reconstruction** — « Terminé — le projet est ouvert,
  l'original en regard », le suffixe « — parité des pistes » (collé au nom
  du fichier, il finissait dans « Reconstruction of X — parité des
  pistes »), et la raison d'un échec : `setFinished` affiche le texte brut ;
- **le lanceur et le transcripteur** — « la chaîne d'analyse n'a pas pu être
  lancée », « la chaîne s'est arrêtée (code N) », « le transcripteur n'a pas
  pu être lancé », « [le script est sorti en 0 sans écrire …] » ;
- **le texte d'attente des notes du projet** (« Ce que la chaîne ne dit
  pas : … »).

**RIEN DANS `interchange/`** : l'épreuve *Children* court sur le moteur, et
`moteur_perime` regarde ce dossier. Les raisons de `ReconstructionChain`
restent françaises à la source — ce sont des données — et se traduisent À
L'AFFICHAGE par `trPhrase`, comme les phrases du moteur depuis D89.

**LE BANC.** Aucun des deux chemins réels vers la boîte ne se passe au
banc : le menu ouvre un sélecteur de fichier, et le dépôt une boîte à trois
boutons. Même montage que D112 : la boîte devient une fonction,
`boiteReconstructionIndisponible()`, appelée par `startReconstruction` ET par
le geste `VSM_BOITE_ESSAI=indisponible` (le dispatcher de D112, renommé
`showBoxForCapture` puisqu'il ne sert plus seulement l'enregistrement). La
raison est la VRAIE : celle que `locate()` rend d'après les préférences du
`HOME` de brouillon — deux cas, un dossier désigné sans `reconstruire.py`,
et un dossier qui en a un mais pas de `.venv`. Le texte d'attente des notes
se photographie par `VSM_VUE=notes` et `VSM_CAPTURE_PANNEAUX`. Le volet de
reconstruction, le lanceur et le transcripteur ne se passent pas au banc
pendant l'épreuve (il faudrait lancer la chaîne) : vérifiés par le code, et
dit.

**ATTENDU, écrit avant la mesure.**

1. **L'inventaire** : stricte ÉCRAN **14 → 7**, `MainComponent` **8 → 5** —
   les sept qui restent ne s'affichent pas (un jeton de banc, un mot cherché
   pour colorer, trois chemins, le nom d'un fil, le libellé d'un banc).
2. **La boîte, deux cas, deux langues** : le témoin (même code, phrases
   d'avant) la montre en français dans les deux langues ; D114 en anglais —
   « Reconstruction unavailable : the analysis chain folder set in the
   preferences does not contain reconstruire.py (…) / / fix the path, or
   clear it to let the application search », et « …the chain's Python
   environment has not been created (no .venv in …) » —, sans mot français
   hors chemins et commande ; le français de D114 identique au témoin.
3. **Le texte d'attente des notes** : français au témoin dans les deux
   langues, anglais avec D114 — lu sur la photo du panneau.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026, banc à 17:44).** Témoin :
binaire du 11/09 17:42:07 (la fonction, le jeton, `montrerBoite()`, les
phrases d'avant) ; D114 : 17:43:58. Même banc, `HOME` de brouillon, deux
dossiers de chaîne fabriqués pour l'occasion.

1. **L'inventaire — TENU** : stricte ÉCRAN **14 → 7**, `MainComponent`
   **8 → 5**. Les sept qui restent ne s'affichent pas : « libellé » (le
   banc de D94), « non appliqué » (le mot que le volet cherche pour
   colorer), `gel/piste-`, `audio/report-…` ×2 (des chemins), « Balayage des
   plugins » (le nom d'un fil), « Arrangement / règle » (le libellé d'un
   banc). TABLE 228 → 233 ; large ÉCRAN 36 → 28, TABLE 266.
2. **La boîte — TENU**, 1 sur 1 par cas et par langue. Le témoin la montre
   en français dans les deux langues ; D114 en anglais : « Reconstruction
   unavailable : the analysis chain folder set in the preferences does not
   contain reconstruire.py (…) / / fix the path, or clear it to let the
   application search » et « … the chain's Python environment has not been
   created (no .venv in …) / / python3 -m venv .venv && … » ; aucun mot
   français hors chemins et commande ; le français de D114 identique au
   témoin dans les deux cas. La raison est bien celle de `locate()` : elle
   change avec la préférence, sans rien d'écrit pour le banc.
3. **Le texte d'attente — TENU**, lu sur la photo du panneau (`VSM_CAPTURE_PANNEAUX`) :
   « Ce que la chaîne ne dit pas : pourquoi cette piste vient de ce stem, ce
   qui est une … » au témoin en anglais, « What the chain does not say: why
   this track comes from that stem, what is a hypo… » avec D114. Vu en
   passant, et laissé : ce texte est TRONQUÉ dans les deux langues — le
   texte d'attente d'un `TextEditor` JUCE tient sur une ligne, et le
   panneau de 520 px n'en montre que les deux tiers.

Non passés au banc, dits : le volet de reconstruction (phrase de fin,
suffixe de parité, raison d'un échec désormais traduite dans `setFinished`),
le lanceur et le transcripteur — il faudrait lancer la chaîne, et
l'épreuve *Children* l'occupe. Table : **1 363** paires (+9), **110**
modèles (+4). Suites C++ vertes à `-j 2` (330, 1 291, 297, 25, 11) ;
`build/tools/vsm-render` non touché ; préférences intactes.

### Phase D115 — A9 : ce qui s'affiche encore en français et qu'aucun compte strict ne voit (11/09/2026)

**LE LOT.** Après D114, l'inventaire strict ne compte plus que 7 chaînes, et
aucune ne s'affiche. La règle large de D101 en montre 28, dont une vingtaine
de faux déjà nommés (unités, sigles, mots anglais). Parmi le reste, trois
textes s'affichent vraiment en français dans l'interface anglaise :

- **« RÉGLAGES »**, titre d'une section de la façade du TB-303 — la seule
  des **163** sérigraphies de section qui ne soit pas en anglais (les autres
  recopient la machine d'origine : ENVELOPE, FILTER, OUTPUT…). Elle vit dans
  `panels/`, hors du champ de l'inventaire, qui ne lit qu'`app/Source` :
  aucun compte ne la voyait, l'INDEX la nommait à la main ;
- **deux phrases d'état du piano roll** — « N note(s) plus faible(s) que V »
  et « N note(s) plus courte(s) que T ticks », après *Sélectionner ▸ Notes
  plus faibles que… / plus courtes que la grille* du menu du clic droit ;
  sans accent ni mot de la liste, elles sont dans l'angle mort.

Et un faux de la règle large, réécrit en passant : « Rapport de
reconstruction illisible : … » s'affiche déjà traduit (son modèle date de
D89), mais le source le compose par `+`.

**LES DÉCISIONS.**

1. **Le titre de section passe par `tr()` à l'affichage**
   (`MachinePanelComponent`). `tr()` rend la clé quand elle n'a pas de
   paire : les 162 sérigraphies anglaises restent telles quelles, et
   « RÉGLAGES » devient « SETTINGS ». Rien dans `panels/`.
2. **Les deux phrases d'état deviennent des modèles à `%1`, `%2`.**
3. **Un geste de banc** : `VSM_MENU_CONTEXTE` gagne `pianoroll:libellé` — le
   menu du clic droit du piano roll, par `buildContextMenu()` et
   `performContextMenuAction()`, la paire même que la souris emprunte. La
   recherche d'une entrée par son libellé (`entreeParLibelle`, D83) sort
   d'`ArrangementComponent.cpp` dans un en-tête commun : deux copies
   finiraient par ne plus lire la même chose.

**LE BANC.** (a) Projet neuf, `VSM_GESTE_PISTE=machine:vsm.tb303`,
`VSM_TEXTES_LISTE` : les titres de la façade. (b) `children-dream-v12` (la
reconstruction d'août, pistes MIDI), la basse choisie, puis les deux entrées
du menu du clic droit, par leur libellé DANS LA LANGUE du lancement ;
`VSM_TEXTES_LISTE` lit la ligne d'état. Témoin : le même code — l'en-tête
commun et le geste —, sans traduction.

**ATTENDU, écrit avant la mesure.**

1. **L'inventaire** : stricte **inchangé à 7** — ces textes sont dans
   l'angle mort, c'est la raison de la phase ; large ÉCRAN **28 → 25** (les
   deux phrases d'état, le rapport illisible).
2. **La façade** : « RÉGLAGES » au témoin dans les deux langues ; avec D115,
   « RÉGLAGES » en français et « SETTINGS » en anglais ; les autres titres
   de la façade identiques d'une série à l'autre.
3. **Le piano roll** : au témoin, la ligne d'état en français dans les deux
   langues ; avec D115, « N note(s) softer than 64 » et « N note(s) shorter
   than T ticks » en anglais (« softer », le mot de l'entrée de menu
   anglaise « Notes softer than 64 »), **avec les mêmes nombres** qu'en français ; le
   français identique au témoin.

**LE RÉSULTAT, ATTENDU PAR ATTENDU (11/09/2026, banc à 17:54).** Témoin :
binaire du 11/09 17:52:37 (l'en-tête commun, le geste, les phrases d'avant) ;
D115 : 17:54:28. Six lancements par série, `HOME` de brouillon.

1. **L'inventaire — TENU** : stricte **inchangé à 7** ; large ÉCRAN
   **28 → 25**, TABLE 266 → 267.
2. **La façade — TENU** : 35 libellés en capitales sur la façade du TB-303
   dans chaque série ; en anglais, le SEUL changement est « RÉGLAGES » →
   « SETTINGS » ; le français identique au témoin.
3. **Le piano roll — TENU** : le geste trouve et exécute l'entrée par son
   libellé dans chaque langue (« Notes plus faibles que 64 », « Notes softer
   than 64 »…). Au témoin, « 12 note(s) plus faible(s) que 64 » et « 0 note(s)
   plus courte(s) que 120 ticks » dans les deux langues ; avec D115, « 12
   note(s) softer than 64 » et « 0 note(s) shorter than 120 ticks » en
   anglais — mêmes nombres (12, 64 ; 0, 120) — et le français identique.

Table : **1 366** paires (+3). Suites C++ vertes à `-j 2` (330, 1 291, 297,
25, 11) ; `build/tools/vsm-render` non touché ; préférences intactes.

### Phase D116 — A9 : les réserves d'un preset de piste appliqué à la main — un reste de l'INDEX, vérifié plutôt que supposé (11/09/2026)

**CE QUE L'INDEX DIT, ET CE QUE LE CODE DIT.** Depuis D89, l'INDEX range
« les réserves d'un preset appliqué à la main » dans ce qui reste d'A9.
Relu aujourd'hui, `applyTrackPresetFile` montre ces réserves par
`montrerBoite(…, tr(u8"Preset de piste appliqué, avec des réserves"),
trPhrase(…))`, et `trPhrase` traduit LIGNE PAR LIGNE puis segment par
segment ; les segments de `PresetApplyReport::summary()` (« N paramètre(s)
appliqué(s), M borné(s), K non pris en charge : … ») ont leurs modèles
depuis D89-D90. Lu dans le code, ce reste serait déjà traduit — mais une
lecture de code n'est pas une mesure, et D101 a montré ce qu'elle laisse
passer.

**LE BANC.** Un preset de piste ÉCRIT PAR L'APPLICATION (`VSM_PRESET_PISTE`,
Minimoog, 22 paramètres), puis abîmé à la main : deux paramètres que la
machine ne connaît pas (`filter.1.fantome`, `oscillator.9.level`) et deux
valeurs hors bornes (`filter.1.cutoff` à 999 999, `envelope.1.attack` à
−5). Posé dans `~/.config/VSM/pistes/` d'un `HOME` de brouillon, appliqué
par le menu (« Banc-D116-abime », l'entrée qu'il y prend), en français et
en anglais ; la boîte lue par `VSM_BOITE`, la ligne `VSM_PRESET` du
terminal à côté.

**ATTENDU, écrit avant la mesure** (binaire de D115, aucun code touché).

1. **Une boîte « avec des réserves »** dans chaque langue, qui NOMME les deux
   paramètres inconnus et compte les deux bornés.
2. **En anglais, rien de français** hors des identifiants de paramètres :
   titre et segments traduits (« … parameter(s) applied, 2 clamped, 2
   unsupported: filter.1.fantome, oscillator.9.level »).
3. **Ce que chaque issue décide** : tenu, le reste de l'INDEX est CLOS par
   la mesure, sans code ; un segment français en anglais, et le lot est le
   modèle qui manque — écrit dans cette même phase.

**LE RÉSULTAT (11/09/2026, banc à 18:01) — LES TROIS ATTENDUS TENUS, AUCUN CODE.**
Binaire de D115 (17:54:28), `HOME` de brouillon, deux lancements.

1. **Une boîte par langue**, qui nomme les deux inconnus et compte les deux
   bornés : « Preset de piste appliqué, avec des réserves : 22 paramètre(s)
   appliqué(s), 2 borné(s), 2 non pris en charge : filter.1.fantome,
   oscillator.9.level ».
2. **L'anglais, entièrement traduit** : « Track preset applied, with
   reservations : 22 parameter(s) applied, 2 clamped, 2 unsupported:
   filter.1.fantome, oscillator.9.level ». La ligne `VSM_PRESET` du
   terminal reste française dans les deux langues — c'est une donnée, comme
   `VSM_OUVERTURE`, et c'est voulu.
3. **Le reste est CLOS par la mesure.** Il l'était dans le code depuis D89-D90
   ; l'INDEX le portait encore parce que personne ne l'avait passé au banc.
   Retiré de la liste, avec ce banc pour preuve.

Au passage, et c'est la même leçon que D101 dans l'autre sens : un reste
qu'on a nommé une fois se recopie de document en document tant qu'aucune
mesure ne le relit. Les « infobulles françaises écrites sans leurs accents »
de la même liste n'ont plus de littéral nulle part (les onze infobulles hors
`tr()` sont toutes calculées, et aucune règle d'inventaire, stricte ou large,
n'y trouve de français) : je les laisse à l'INDEX avec cette réserve, faute
d'un banc qui les lise toutes à l'écran.

### Phase D117 — deux boîtes qui coupent mal à 150 % : un mot orphelin, un exemple coupé en deux (11/09/2026)

**CE QUE D91 A VU SANS LE TRAITER.** « Deux textes gardent des retours à la
ligne forcés qui coupent mal à 150 % : « pitch. » seul sur sa ligne dans
« Clip is N bars », et « Drums # » coupé entre deux lignes dans le renommage
en série — comme « Batterie # » en français. » Les retirer changeait l'image
française, que D91 devait laisser intacte : un geste à part. Le voici — et
c'est un geste de LISIBILITÉ, le besoin écrit en tête de ce projet (150 %
par défaut), pas une traduction.

**DEUX DÉFAUTS DIFFÉRENTS, DEUX GESTES.**

1. **« Le clip fait N mesures »** : le message force un `\n` entre deux
   phrases. `AlertWindow` compose avec `createLayoutWithBalancedLineLengths`,
   paragraphe par paragraphe : la première phrase, à peine plus longue que la
   boîte, laisse son dernier mot seul. **Décision** : un seul paragraphe, les
   deux phrases à la suite — le retour forcé n'apportait rien que la mise en
   page ne sache faire.
2. **« Renommer les pistes en série »** : l'exemple « Batterie # » est coupé
   ENTRE ses mots — le retour forcé n'y est pour rien, c'est l'espace
   ordinaire qui cède. **Décision** : des espaces INSÉCABLES (U+00A0) dans les
   exemples entre guillemets, en français à l'intérieur des « … » comme le
   veut la typographie, en anglais entre « Drums » et « # ». Le retour forcé
   avant « Seules les pistes VISIBLES… » reste : c'est une vraie seconde
   idée. Vérifié dans la source du JUCE de ce build avant de l'écrire :
   `CharacterFunctions::isWhitespace` s'en remet à `iswspace`, qui ne tient
   pas U+00A0 pour un blanc.

**LE BANC** — celui de D91, repris : le projet d'épreuve de D91, la boîte du
renommage par le menu, celle des mesures par l'import d'un WAV puis le menu
du clic droit du clip audio ; chaque boîte photographiée par
`VSM_CAPTURE_PANNEAUX`, en français et en anglais, témoin (binaire de D115)
puis D117. Une photo de boîte modale peut manquer (la course de D72) : le
banc relance jusqu'à quatre fois, et le dit.

**ATTENDU, écrit avant la mesure** — lu sur les photos, dit mot pour mot.

1. **Le témoin reproduit D91** : « pitch. » seul sur sa ligne en anglais ;
   « Drums # » ou « Batterie # » coupé en deux dans au moins une langue.
2. **D117** : aucune ligne d'un seul mot dans la boîte des mesures, dans
   aucune langue ; aucun exemple entre guillemets coupé dans la boîte du
   renommage, dans aucune langue.
3. **Rien d'autre ne bouge** : mêmes titres, mêmes champs, mêmes boutons ;
   les deux autres boîtes photographiées au passage (s'il en vient)
   identiques au pixel.

**LE TÉMOIN (binaire de D115, banc à 18:04) — l'attendu n° 1 TENU, et le
défaut est plus large que D91 ne le disait.** Quatre photos, chacune venue au
premier essai. En anglais, « pitch. » seul sur sa ligne ; EN FRANÇAIS AUSSI,
« hauteur. » seul sur la sienne. Et l'exemple coupé dans les DEUX langues :
« “Drums / #” », « « Batterie / # » ».

**LE PREMIER ESSAI (binaire du 11/09 18:06:16, banc à 18:06) — la moitié
tient, l'autre montre un défaut que l'attendu n'avait pas prévu.**

- **La boîte des mesures — TENU** dans les deux langues : « …without
  changing / pitch. The material's original tempo will be deduced and
  shown. », « …sans changer de / hauteur. Le tempo d'origine du matériau
  sera déduit et affiché. » Plus aucune ligne d'un seul mot.
- **Le renommage — tenu au sens écrit** (aucun exemple coupé), **mais la
  photo montre pire** : l'espace insécable s'affiche nettement PLUS LARGE
  qu'une espace ordinaire, à peu près deux — « “Drums  #” », « «  Batterie
  #  » ». Ce n'est pas un détail de typographie : c'est un MOTIF que
  l'utilisateur recopie, et « Drums  # » a l'air de porter deux espaces, ce
  qu'il ne porte pas. Une coupure de ligne gênait la lecture ; ceci induit en
  erreur. La cause n'est pas cherchée (une glyphe de repli, vraisemblablement)
  : le geste est abandonné, pas réparé.

**DÉCISION, écrite avant la seconde mesure.** L'exemple passe sur SA PROPRE
LIGNE : trois paragraphes — la règle, l'exemple, la restriction aux pistes
visibles —, séparés par des retours placés aux limites de phrase, et plus
aucun caractère spécial. Un paragraphe qui tient sur une ligne n'est pas
coupé par `createLayoutWithBalancedLineLengths` ; l'exemple le plus long
(« Exemple : « Batterie # » donne « Batterie 1 », « Batterie 2 »... », 63
signes) tient dans une ligne de cette boîte, qui en montrait environ 70 sur
la photo du témoin. La leçon des deux boîtes est la même, dans les deux
sens : un retour forcé AU MILIEU d'une idée coupe mal, un retour ENTRE deux
idées protège.

**ATTENDU DE LA SECONDE MESURE.** La boîte du renommage montre trois lignes
dans chaque langue, l'exemple entier sur la deuxième, avec des espaces
ordinaires ; la boîte des mesures identique au premier essai (sa chaîne ne
change pas).

