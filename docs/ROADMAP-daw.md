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

### Phase D17 — Le sixième audit : ce qui manque une fois D16 posée (04/09/2026, 23:55)

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
