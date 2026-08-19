# Vintage Synth MIDI Studio — Architecture

**Statut au 16/08/2026** : Phases 1, 2 (fondations audio + AudioEngine JUCE)
et **les TROIS instruments de la Phase 3 (Minimoog/TB-303/Juno-106) sont
faits**. La partie DSP de la Phase 2 est complète : **bus master** (EQ 3
bandes, compresseur, saturation, largeur stéréo, limiteur brickwall, mètre
LUFS ; branché sur le bus stéréo final du `ProcessGraph` en **bypass par
défaut**) et **suite d'effets d'insert** (delay/ping-pong, distorsion
oversamplée, bitcrusher, chorus BBD promu ; interface commune `IAudioEffect`
+ **oversampler 2x/4x/8x** anti-aliasing). La **Phase 4 est CLOSE** : **TR-808-style, TR-909-style** (boîtes à rythmes
entièrement synthétisées, section 12), **SH-101-style** (monosynthé VCO à
sub-oscillateur + glide) **et Prophet-style** (polysynthé 5 voix, 2 oscillateurs,
hard-sync, double enveloppe et Poly-Mod) sont tous faits. La **Phase 5 est
CLOSE** : **Jupiter-8-style** (poly 8 voix, 2 VCO cross-mod/sync, HPF+VCF,
double enveloppe, chorus BBD stéréo), **ARP-Odyssey-style** (DUOPHONIQUE :
les 2 VCO suivent les touches grave/aiguë, ring mod, sample & hold),
**MS-20-style** (double filtre HPF+LPF résonant auto-oscillant BORNÉ,
`MS20Filter` local au plugin) et **DX7-style** (synthèse FM 6 opérateurs,
système d'algorithmes, feedback, enveloppes par opérateur, sensibilité
vélocité) sont tous faits. La **Phase 6 est ENTAMÉE** : les **tests de
non-régression audio par machine** sont en place (section 9 bis) -- chacune des
12 machines a désormais une empreinte de rendu figée, qui détecte toute dérive
du son y compris causée par une brique DSP partagée modifiée pour une AUTRE
machine. La **Phase 6 est CLOSE** : banc de mesure CPU (§ 9 ter), profiling
intra-DSP (§ 9 quater), **SIMD entre voix** sur les trois machines
polyphoniques à filtre ladder (§ 9 sexies) et **unification des transports**
avec boucle échantillon-exacte (§ 6 bis). Bilan mesuré : graphe complet à 16
pistes **1,40x** plus rapide, Jupiter-8 **2,06x**, Juno-106 **1,88x**,
Distortion **3,5x** -- le tout à empreintes audio inchangées (écart maximal
0,001 %), ce que les tests de non-régression prouvent à chaque build. Le
**piano roll est désormais complet** (section 9 quinquies) : outils, historique
annuler/rétablir, ~30 opérations d'édition musicale, gammes, arpèges, accords,
écoute au clic, et toute la logique testée hors JUCE. Total : **381 tests moteur** (70 core + 311 audio,
tous verts, zéro warning, y compris sous les flags stricts type-JUCE
`-Wfloat-equal -Wsign-conversion -Wshadow`) + application complète compilée et
liée. Rendus réels vérifiables : `minimoog_demo.wav`,
`tb303_demo.wav`, `juno106_demo.wav`, `tr808_demo.wav`, `tr909_demo.wav`, `sh101_demo.wav`, `prophet_demo.wav`,
`jupiter8_demo.wav`, `arpodyssey_demo.wav`, `ms20_demo.wav`, `dx7_demo.wav`.

Volet UI de la Phase 2 -- **COMPLET** (tout compilé sans erreur/warning contre
JUCE, validation par unité de traduction) : **Mixer UI** (tranches fader/pan/
mute/solo/mètre + master EQ/comp/limiteur/LUFS), **sélecteur de device audio**,
**MIDI Learn** (entrée MIDI live, `MidiLearnMap` testé, apprentissage CC->param
depuis le synth rack), **éditeur de courbes d'automation** (publication RT-safe
des lanes via snapshot atomique) et **export audio WAV** (rendu offline ->
Int16/Int24/Float32, section 25).

**Côté MOTEUR, la Phase 2 est désormais COMPLÈTE** : chaîne d'inserts par piste
+ 2 bus de **sends** auxiliaires (sections 5 et 15, rendu stéréo par piste), la
palette d'effets de la **section 16** au complet (delay/ping-pong, chorus,
distorsion/overdrive, bitcrusher, **reverb** Freeverb, **flanger**, **phaser**,
**tape saturation**, **filtre** multimode, + compresseur/EQ sur le master), et
l'**automation sample-accurate** (découpage en sous-segments de 64 échantillons,
section 17). Tout testé (routage, sends, chaque effet, granularité sous-bloc,
déterminisme, non-régression).

**La Phase 2 est désormais COMPLÈTE, moteur ET interface.** Ajoutés côté UI :
`EffectFactory` (liste/instancie les 9 effets), `EffectChainComponent` (onglet
"Effets" : ajout/suppression/réordonnancement d'inserts par piste + réglage des
paramètres, un knob par entrée de `parameterList()`), et 2 **knobs de send** par
tranche du mixer routant vers 2 bus auxiliaires (Reverb sur A, Delay sur B par
défaut). Tous les fichiers UI compilés sans erreur/warning contre JUCE (flags
stricts inclus, validation par unité de traduction).

**Interopérabilité (Phase 7) : Mode A complet, CLAP fait.** La couche
`interchange/` (`vsm_interchange`, **45 tests**) couvre les identités
sémantiques des paramètres (P2), les presets `*.synth.json` (P3), les projets
`project.json` (P4), le chargement/écriture d'un dossier de projet (P7) et la
reconstruction hors ligne `projet -> WAV` via l'outil `vsm-render` (P8). La
boucle Python -> DAW -> audio tourne donc de bout en bout, sans réseau. Voir
section 28 et [`docs/ROADMAP-interop.md`]. **P5-P6 (CLAP)** sont faits eux
aussi : les machines VSM s'exposent en plugin CLAP, et un `.clap` tiers se
charge dans le moteur sans que `ProcessGraph` ni l'UI changent d'une ligne. Ne
reste que P9 (API locale, Mode B).

**Ajouter une machine** (préalable à la Phase 5) est décrit pas à pas dans
[`docs/GUIDE-ajout-machine.md`] : contrat `ISynthPlugin`, règles temps réel,
architectures de voix, catalogue des briques DSP, enregistrement + force-link,
CMake, batterie de tests, et un **squelette copiable vérifié** (compile sans
warning, sonne, save/load OK). La règle d'or : une nouvelle machine ne touche
que son dossier `plugins/<machine>/` + 2 lignes de CMake + 1 bloc force-link. Hors périmètre
2/3/4/7 : UIs "hardware" par machine.
Tous les paramètres (synthés, bus master, effets) réutilisent `ParameterList`
-> l'ensemble est "ParameterDescriptor-ready" pour la Phase 7 (addon interop),
sans en dépendre.

Ce document sert de référence vivante : il doit être mis à jour à chaque
changement d'architecture significatif, pas seulement relu au lancement du
projet.

**Les phases 1 à 7 sont terminées.** La suite est pilotée par
[`docs/ROADMAP-fusion.md`], établie après la fusion avec le projet d'analyse
(`analyse/`) : l'objectif commun est désormais de reconstruire un fichier WAV
en MIDI + patchs rejouables. Deux cahiers des charges l'accompagnent :
[`docs/CDC-nouvelle-machine.md`] (ce qu'une machine doit satisfaire pour être
déclarée finie, façade et identités sémantiques comprises) et
[`docs/CDC-machines-manquantes.md`] (ce qui manque au parc pour couvrir des
enregistrements réels : un lecteur d'échantillons et un synthé neutre, plutôt
que d'autres machines de caractère).

---

## 1. Philosophie générale

Quatre décisions structurent tout le reste :

1. **Le moteur ne dépend jamais de l'UI.** `core/` et `audio/` ne
   connaissent rien de JUCE. `app/Source/audio/AudioEngine` est le SEUL
   endroit qui connaît `juce::AudioIODeviceCallback`.
2. **Une seule source de vérité pour le timing ET pour le rendu.**
   `TempoMap` + `PlaybackScheduler` (MIDI) et `ProcessGraph` (audio) sont
   déterministes -- vérifié par test, illustré par les deux WAV de démo.
3. **Ajouter une machine ne doit jamais toucher le moteur NI l'UI.** Le
   TB-303-style a été ajouté sans modifier `ProcessGraph`, `AudioEngine`,
   `TrackListComponent` ni `SynthRackComponent` -- seulement de nouveaux
   fichiers dans `audio/plugins/tb303/` + une ligne dans
   `BuiltInPlugins.cpp`. Il apparaît automatiquement dans l'UI (combo
   instrument, Synth Rack) car les deux lisent `PluginRegistry` à
   l'exécution.
4. **Le chemin temps réel ne ment jamais sur ses garanties**, et
   **l'authenticité non plus** (section 27) : chaque simplification ou
   approximation est documentée explicitement dans le code.

---

## 2. Arborescence

```
vsm-studio/
├── ARCHITECTURE.md
├── README.md
├── CMakeLists.txt
│
├── core/                       "vsm_core" — moteur MIDI/séquenceur (32 tests)
│
├── audio/                      "vsm_audio" — moteur audio temps réel (115 tests)
│   ├── include/vsm/audio/dsp/
│   │   ├── LadderFilterZDF.h     GÉNÉRALISÉ à N pôles (2-4) -- voir section 7
│   │   └── ...                   Oscillator, Envelope, Filter(SVF), AnalogDrift, ParameterSmoother
│   ├── include/vsm/audio/engine/  VoiceManager, MonoVoiceAllocator, AutomationLane, Mixer,
│   │                              ProcessGraph, OfflineRenderer
│   └── plugins/
│       ├── testtone/              synthé de référence générique
│       ├── minimoog/              Minimoog-style (4 pôles, mono, glide, drift)
│       └── tb303/                 TB-303-style (3 pôles, slide, accent) -- NOUVEAU
│
├── app/                         application JUCE — compilée et liée avec succès
│   └── Source/
│       ├── audio/AudioEngine.h/.cpp
│       └── ui/  TransportBarComponent, TrackListComponent (instruments depuis PluginRegistry),
│                PianoRollComponent+VelocityLaneComponent (via PianoRollPanel),
│                SynthRackComponent (générique), PanelWindow (fenêtres flottantes)
│
└── interchange/                 "vsm_interchange" — couche d'interopérabilité (Phase 7)
    ├── ParameterDescriptor.h      identités sémantiques des paramètres
    ├── SemanticMap.cpp            table nom de paramètre -> semanticId, par machine
    ├── Json.h/.cpp                lecteur/écrivain JSON minimal, sans dépendance
    └── SynthPreset.h/.cpp         presets *.synth.json (format sémantique, versionné)
```

---

## 3. Choix technologiques

*(inchangé -- voir C++20, JUCE, CMake+FetchContent avec le piège
`LANGUAGES CXX C` déjà documenté, framework de tests maison)*

---

## 4-6. Moteur MIDI, moteur audio, câblage AudioEngine, fenêtres flottantes

*(inchangé depuis les versions précédentes, À UNE EXCEPTION PRÈS : l'horloge,
voir ci-dessous)*

### 6 bis. Unification des transports (Phase 6, fait)

Deux transports coexistaient : `RealtimeTransport` (Phase 1, thread dédié,
horloge `steady_clock`) donnait la position affichée, pendant que
`ProcessGraph` produisait réellement le son sur le thread audio. Ils étaient
resynchronisés à chaque Play/Stop -- ce qui ne suffit pas : entre deux
synchronisations, deux horloges indépendantes DÉRIVENT. Le curseur finissait
par mentir de plusieurs dizaines de millisecondes sur un morceau long, et une
boucle bouclait au mauvais endroit selon qu'on écoutait ou qu'on regardait.

**Désormais, une seule horloge fait référence : celle du moteur audio.** Elle
compte les échantillons réellement produits par la carte son -- il n'existe pas
de mesure plus exacte de « où en est la lecture », puisque c'est littéralement
ce qu'on entend. L'interface lit `ProcessGraph::currentSeconds()` et le
convertit en ticks.

`RealtimeTransport` n'est pas supprimé pour autant, et ce n'est pas de la
timidité : il reste (1) la source de repli quand **aucune carte son n'est
ouverte** -- l'application doit rester utilisable pour éditer, faire défiler et
exporter sur une machine sans audio -- et (2) le pilote de la sortie MIDI
(`IMidiEventSink`), qui n'a rien à voir avec le rendu audio. Le choix se fait
sur `AudioEngine::isDeviceOpen()`, en un seul endroit (`MainComponent::timerCallback`).

**La boucle appartient maintenant au moteur audio** (`ProcessGraph::setLoopRegion`),
avec deux propriétés que le transport MIDI ne pouvait pas offrir :

- **rebouclage échantillon-exact** : le bloc est découpé à la frontière de
  boucle, pas arrondi à la taille de bloc. À 48 kHz et 512 échantillons, une
  boucle d'une demi-seconde tombe au milieu d'un bloc ; arrondir accumulerait
  une erreur audible en quelques tours. Testé en comparant la position après
  500 blocs à la valeur théorique, à un échantillon près ;
- **aucune note bloquée** : une note dont le NoteOff se trouve APRÈS la fin de
  boucle ne le recevrait jamais et sonnerait indéfiniment. Le graphe suit donc
  les notes tenues par piste et les relâche explicitement au saut. Testé avec
  le cas qui produit le bug : une note qui commence avant la boucle et finit
  après.

---

## 7. Filtre ladder généralisé à N pôles (nouveau, pour le TB-303-style)

Le vrai TB-303 utilise un filtre à **3 pôles (18 dB/oct)**, pas 4 pôles
(24 dB/oct) comme le Moog/Minimoog -- une différence de fidélité trop
importante pour être ignorée. Plutôt que dupliquer `LadderFilterZDF` ou
créer une classe séparée, la dérivation ZDF existante (déjà validée par
test pour 4 pôles) s'est généralisée directement à N pôles : la boucle
d'accumulation de l'état S et l'exposant final changent, la logique reste
identique (voir le commentaire de classe dans `LadderFilterZDF.h`).

**Discipline de non-régression** : `setPoleCount()` par défaut à 4 (aucun
changement pour le Minimoog-style, qui n'appelle jamais cette méthode) ;
tous les tests 4 pôles existants ont été rejoués APRÈS la généralisation et
sont restés verts, avant même d'écrire le premier test 3 pôles. Nouveaux
tests : pente 18 dB/oct (3 pôles) et 12 dB/oct (2 pôles, bonus), stabilité
et auto-oscillation bornée à 3 pôles.

---

## 8. Deuxième instrument de référence : TB-303-style Acid Synth

Architecture : oscillateur unique (saw/square uniquement, section 10) ->
`LadderFilterZDF` **en mode 3 pôles** -> enveloppe d'amplitude.
Monophonique, `MonoVoiceAllocator` réutilisé tel quel depuis le
Minimoog-style (aucune modification nécessaire).

- **SLIDE** : `MonoVoiceAllocator` est configuré en `legatoMode(true)` de
  façon PERMANENTE (contrairement au Minimoog où c'est optionnel) : deux
  notes MIDI qui se chevauchent sont donc TOUJOURS interprétées comme un
  slide (glissando de pitch, pas de retrigger d'enveloppe) -- la
  convention la plus répandue dans les émulations logicielles pilotées par
  MIDI externe, puisque le hardware original tire ce flag de son
  séquenceur interne (pas d'un signal MIDI). Testé comparativement :
  notes qui se chevauchent vs notes séparées produisent un transitoire
  mesurablement différent.
- **ACCENT** : converti intelligemment depuis la vélocité MIDI (section 10,
  exigé explicitement) -- intensité proportionnelle à quel point la
  vélocité dépasse un seuil réglable (`Accent Threshold`), pas un booléen
  tout-ou-rien. Un accent élevé ouvre davantage le filtre, augmente le
  niveau, ET raccourcit le decay -- testé en isolant l'effet du knob
  "Accent" à vélocité fixe (pour ne pas confondre avec le gain de vélocité
  brute), et en confirmant qu'une vélocité sous le seuil ne déclenche aucun
  bonus.
- Enveloppe de filtre : attaque quasi instantanée, DECAY seul réglable
  (pas de sustain -- comme le panneau réel, qui n'a qu'un potentiomètre
  "Decay"), raccourci par l'accent.
- `AnalogDrift` réutilisé (pitch + cutoff), même philosophie que le
  Minimoog-style.

**Limitations assumées** (section 27) : les facteurs de boost exacts
(accent -> ouverture filtre/niveau/decay) sont des choix raisonnés pour
capturer le CARACTÈRE de la machine, pas des valeurs mesurées sur un
hardware réel -- aucune comparaison mesurée n'a été faite. Le "Drive" du
filtre est une constante interne (pas de knob dédié, comme le hardware
réel) choisie pour donner du grain, pas calibrée.

**Vérification de bout en bout hors tests unitaires** : un pattern acid
classique (gammes de croches, deux notes accentuées, un slide) rendu via
`ProcessGraph`/`OfflineRenderer` (`tb303_demo.wav`) et analysé par
fenêtres de 100 ms : les DEUX sauts d'amplitude mesurés tombent
EXACTEMENT sur les deux notes programmées avec vélocité élevée (127 et
120) -- confirmation tangible que l'accent fonctionne comme prévu, pas
seulement que les tests unitaires passent.

### Troisième instrument de référence : Juno-106-style Polysynth (fait)

Dernier des trois instruments de référence, et le premier plugin **poly-
phonique** du projet : 6 voix via le `VoiceManager` générique (jusqu'ici
jamais utilisé par un plugin réel -- il est maintenant validé en conditions
réelles, y compris le vol de voix au-delà de 6 notes tenues).

Chaîne par voix : **DCO** (saw + pulse + sub carré une octave dessous) +
bruit -> **HPF** non résonant (`StateVariableFilter` en passe-haut) ->
**VCF** 24 dB/oct (`LadderFilterZDF` 4 pôles) -> **VCA** piloté par
l'enveloppe ADSR complète. La somme mono des 6 voix passe dans un **chorus
BBD stéréo global** -- c'est lui qui rend l'instrument stéréo.

Décisions fidèles à l'ORIGINAL (section 7), et documentées dans le code :

- **DCO, pas VCO** : accord très stable -> `AnalogDrift` appliqué avec une
  amplitude bien plus faible que sur le Minimoog (`kMaxDriftSemitones`
  petit). Phase des oscillateurs réinitialisée à chaque note (trait DCO :
  attaque cohérente).
- **Pas de sensibilité à la vélocité** : le clavier du Juno-106 n'en a pas
  -> le VCA ne dépend QUE de l'enveloppe (testé : même note à vélocité 30
  et 127 = rendu bit-identique). C'est un trait authentique, pas un oubli.
- **Enveloppe ADSR complète** (Attack/Decay/Sustain/Release), contrairement
  aux ADS-sans-release du Minimoog.
- **Chorus** : nouvelle brique DSP `dsp/Chorus.h` (une ligne à retard mono
  lue à deux positions modulées par deux LFO en quadrature de phase),
  pensée pour être **réutilisée comme effet d'insert générique** (section
  16). Modes I / II / I+II mappés sur des réglages rate/depth/mix.

Approximations assumées (section 27) : le VCF réel du Juno est un filtre
**OTA** (IR3109), modélisé ici par le ladder ZDF à 4 pôles -- même pente et
même comportement de résonance/auto-oscillation, topologie différente. Les
modes de chorus reproduisent le caractère, pas la topologie exacte à deux
LFO fixes de l'électronique d'origine. Aucune mesure comparative avec un
Juno matériel n'a été faite.

**Vérification hors tests unitaires** : `juno106_demo.wav` -- une nappe de
4 accords à 4 voix, analysée par fenêtres RMS de 0,25 s : attaque lente de
nappe, tenue polyphonique continue, extinction propre, aucun NaN, et 93,7 %
des échantillons présentant une différence L/R (preuve tangible que le
chorus produit bien une image stéréo).

---

## 9. Tests et qualité audio

### Bilan actuel : 381 tests moteur, tous verts

- **70 tests `vsm_core`** (dont 36 pour l'édition du piano roll : opérations de
  notes, gammes, accords, arpèges, historique annuler/rétablir),
  **311 tests `vsm_audio`** (dont le SIMD : équivalence avec le filtre
  scalaire, indépendance des lignes, bornes de l'approximation de tanh ; et la
  boucle : rebouclage échantillon-exact, notes relâchées au saut) : chorus BBD, Juno-106,
  bus master (biquad/compresseur/limiteur à plafond garanti/LUFS), oversampler,
  effets d'insert (delay, distorsion, bitcrusher, chorus), TR-808/TR-909
  (choke charleston, toms, crash), SH-101 (mono, glide, sub-osc,
  VCA env/gate), Prophet (5 voix, poly-mod osc B + env filtre, hard-sync,
  non-vélocité, déterminisme, save/load), et la **Phase 5** : Jupiter-8
  (poly 8 voix, cross-mod, sync, HPF, largeur stéréo par chorus, non-vélocité),
  ARP-Odyssey (duophonie grave/aigu, ring mod, déterminisme S&H), MS-20
  (double filtre, HPF retire les graves, **auto-oscillation bornée à résonance
  max**, ring mod) et DX7 (poly, l'algorithme change le routage, l'index de
  modulation et le feedback changent le timbre, sensibilité vélocité on/off,
  déterminisme, save/load ; §11 complet : enveloppe de pitch, keyboard level scaling, fréquence fixe par opérateur).
- Zéro warning, zéro régression : le bus master en bypass par défaut laisse
  le rendu historique strictement identique (prouvé par les tests
  `process_graph`/`OfflineRenderer` existants, tous encore verts).
- Sept rendus réels produits et analysés (`minimoog`, `tb303`, `juno106`,
  `tr808`, `tr909`, `sh101`, `prophet` -- `*_demo.wav`).

### 9 bis. Non-régression audio par machine (Phase 6)

Les tests par machine existants vérifient des PROPRIÉTÉS ("l'accent ouvre le
filtre", "le HPF retire les graves"). Ils ont un angle mort structurel : le jour
où une brique DSP partagée (`LadderFilterZDF`, `Envelope`, `AnalogDrift`...) est
modifiée pour une machine, toutes ces propriétés restent vraies alors que
TOUTES les machines qui partagent la brique sonnent différemment.

`audio/tests/test_audio_regression.cpp` comble ce trou. Chaque machine rejoue
une phrase FIXE (mélodique -- avec une note accentuée et un chevauchement pour
exercer slide/duophonie/polyphonie -- ou rythmique pour les boîtes à rythmes),
et le rendu est réduit à une empreinte comparée à une référence figée dans
`audio/tests/audio_fingerprints.inc` :

- pic, RMS global et RMS de 6 fenêtres successives (enveloppe temporelle) ;
- taux de passages par zéro et proportion d'échantillons où L != R (une machine
  mono qui devient stéréo, ou l'inverse, se voit immédiatement) ;
- **profil spectral en 16 bandes de demi-octave + centroïde géométrique**,
  calculés par corrélation directe avec un phaseur complexe -- code d'analyse
  volontairement INDÉPENDANT de `vsm::audio::dsp` : un outil de mesure qui
  partagerait ses briques avec le code mesuré ne verrait pas une régression
  DANS ces briques.

**Pas de comparaison bit-à-bit, et pourquoi** : le rendu est déterministe sur
une machine donnée (tout l'aléatoire passe par `DeterministicRng` seedé), mais
`std::sin`/`std::exp` ne donnent pas le même bit d'une libm ou d'une
architecture à l'autre, et les filtres résonants comme la FM amplifient l'écart.
Une empreinte bit-exacte échouerait à la première compilation ailleurs, pour de
mauvaises raisons. Les tolérances (3 % sur les énergies, 5 % sur les bandes,
2 % sur le centroïde) absorbent ce bruit.

**Sensibilité mesurée, pas supposée** (les tolérances ont été calibrées en
injectant de vraies dérives dans le code, puis retirées) :

| Dérive injectée | Détectée sur |
|---|---|
| Fréquence de coupure du ladder +1 % | aucune machine (marge de bruit voulue) |
| Fréquence de coupure du ladder +10 % | Minimoog, TB-303, SH-101, Prophet, Jupiter-8 |
| Temps de decay des enveloppes -5 % | TB-303, Juno-106, Prophet, DX7 |

Ces deux dérives ne faisaient échouer AUCUN des tests préexistants : c'est
exactement l'angle mort que ces tests ferment. Le premier passage des bandes
d'une octave à une demi-octave a été fait pour cette raison (à l'octave, +10 %
de coupure restait invisible sur la plupart des machines).

**Régénérer les références** après un changement de son ASSUMÉ :
`VSM_REGEN_AUDIO_FINGERPRINTS=1 ./build/audio/vsm_audio_tests`, puis recopier
les lignes imprimées dans `audio_fingerprints.inc`. Un test garde-fou
(`regression_every_registered_machine_has_a_reference`) échoue si une machine
enregistrée n'a pas d'empreinte : impossible d'ajouter une machine en oubliant
sa non-régression.

### 9 ter. Profiling CPU et première optimisation mesurée (Phase 6)

`audio/bench/bench_main.cpp` (cible `vsm_audio_bench`, **OFF par défaut** :
c'est un outil de mesure, il n'a pas sa place dans une CI où il n'ajouterait
que du temps et des chiffres dépendants de la machine) :

```bash
cmake -B build-bench -DVSM_BUILD_BENCH=ON -DVSM_BUILD_TESTS=OFF -DVSM_BUILD_APP=OFF
cmake --build build-bench -j
taskset -c 2 ./build-bench/audio/vsm_audio_bench
```

Il mesure le coût par bloc (48 kHz, blocs de 512 = **budget 10,667 ms**) de
chaque machine toutes voix actives, de chaque effet d'insert, et du
`ProcessGraph` complet à 1/4/8/16 pistes. Dans un moteur audio la moyenne ne
suffit pas : **un seul bloc en retard s'entend** (clic), donc le banc affiche
min / moyenne / p50 / p99 / max.

**Piège mesuré, à ne pas redécouvrir** : sur un CPU **hybride** (Intel Core
Ultra, Apple Silicon, ARM big.LITTLE), le thread de mesure migre entre cœurs P
et cœurs E, dont les performances diffèrent d'un facteur 2 et plus. Deux
exécutions successives du même binaire ont donné un rapport de 2,4x sur un
effet que la modification en cours ne touchait même pas. D'où la discipline
retenue : **épingler** le processus (`taskset -c 2`), comparer la colonne
`min` (coût du DSP sans interférence), et pour une comparaison avant/après,
**normaliser par un composant non modifié** mesuré dans la même exécution --
c'est ce qui a rendu le gain ci-dessous reproductible à 0,1 près alors que les
valeurs absolues variaient de 30 % d'un run à l'autre.

**Relevé de référence** (Intel Core Ultra 7 155H, cœur P épinglé,
RelWithDebInfo, colonne `min`) -- ce sont les chiffres AVANT la deuxième passe
d'optimisation décrite en section 9 quater, qui les améliore encore :

| Machine (voix) | ms/bloc | % d'un cœur | Effet | ms/bloc |
|---|---|---|---|---|
| Jupiter-8 (8) | 0,88 | 8,7 % | Distortion (4x oversampling) | 0,068 |
| Juno-106 (6) | 0,59 | 5,9 % | Reverb | 0,011 |
| DX7 (8) | 0,48 | 7,5 % | Phaser / Chorus | 0,008 |
| Prophet (5) | 0,46 | 4,4 % | Flanger | 0,006 |
| TR-909 (8) | 0,18 | 1,7 % | Tape Saturation | 0,005 |
| Minimoog / MS-20 / SH-101 / ARP / TB-303 (mono) | 0,06-0,09 | < 1 % | Delay / Filter / BitCrusher | 0,001-0,003 |

`ProcessGraph` complet : 1 piste 0,17 ms, 4 pistes 0,60 ms, 8 pistes 0,81 ms,
**16 pistes 1,63 ms (15 % d'un cœur)**. Le bus master activé coûte 0,01 ms sur
8 pistes -- négligeable. Conclusion : le moteur n'est pas près de saturer un
cœur, l'enjeu de la suite n'est pas de sauver des dropouts mais de gagner de
la marge (plus de pistes, latences plus courtes).

**Ce que la mesure a immédiatement révélé** : la Distortion coûtait **27 fois
plus cher que n'importe quel autre effet** (0,40 ms contre 0,015 ms). En cause,
`dsp/Oversampler` et deux gaspillages symétriques :

1. à la montée, f-1 échantillons sur f sont des zéros insérés, et le FIR
   calculait quand même `c[m] * 0` puis l'additionnait ;
2. à la descente, une convolution complète était calculée pour CHAQUE
   échantillon suréchantillonné, alors qu'un seul sur f est conservé.

Corrigé par une **décomposition polyphase** de l'étage de montée (chaque phase
k ne garde que les coefficients k, k+f, k+2f... appliqués aux vrais
échantillons d'entrée) et par un `FirLowpass::push()` qui alimente l'historique
sans calculer une sortie destinée à la poubelle.

**Ces deux économies sont EXACTES, pas des approximations** : les termes
supprimés valent zéro ou ne sont jamais lus, et le facteur de compensation de
gain (f, toujours une puissance de deux, donc exact en binaire) est absorbé
dans les coefficients. Ce n'est pas un raisonnement qu'on se contente de
croire : le test `oversampler_polyphase_matches_naive_reference` rejoue un
signal riche sur plusieurs blocs, aux facteurs 2/4/8, contre une
**réimplémentation naïve de référence écrite exprès dans les tests**
(zéro-stuffing explicite, convolution complète partout) et exige une identité
à 1e-6.

**Gain mesuré** : coût de la Distortion rapporté à celui de la Reverb (non
modifiée) dans la même exécution : **22,1 avant -> 6,3 après**, soit **3,5x plus
rapide**, reproduit sur 8 exécutions alternées. Le facteur théorique est 4
(= facteur de suréchantillonnage) ; l'écart correspond au `tanh` et au filtre
de tonalité, que l'optimisation ne touche pas.

### 9 quater. Profiling intra-DSP et deuxième passe d'optimisation

`perf` et `valgrind` n'étant pas disponibles ici, la granularité intra-DSP a
été obtenue autrement : le banc mesure aussi chaque **brique élémentaire** au
coût par échantillon (section "Briques DSP"), et on confronte la somme des
briques au coût réel d'une voix. Relevé (mêmes conditions que ci-dessus) :

| Brique | ns/échantillon | | Brique | ns/échantillon |
|---|---|---|---|---|
| `LadderFilterZDF::process()` | **70-75** | | `std::pow(2.0f, x)` | 10,8 |
| `StateVariableFilter::setCutoffHz()+process()` | 21,5 | | `std::exp2f(x)` | 4,4 |
| `StateVariableFilter::process()` | 6,8 | | `std::tan(x)` | 10,3 |
| `AnalogDrift::nextValue()` | 4,3 | | `std::sin(x)` | 5,8 |
| `BandLimitedOscillator::nextSample()` | 2,1 | | `std::tanh(x)` | 5,5 |
| `AdsrEnvelope::nextSample()` | 1,0 | | | |

Deux enseignements, aucun des deux devinable sans mesure :

1. **Le filtre ladder coûte 70-75 ns, soit 7 fois un `std::tan` et 35 fois un
   oscillateur** -- alors que son arithmétique (2 `tanh` + ~25 opérations)
   devrait valoir 17 ns, chiffre vérifié en simulant la même séquence
   d'opérations. L'écart vient de la nature du calcul : dans un filtre
   récursif, chaque échantillon dépend du précédent, donc rien ne se recouvre.
   Ce qui compte n'est pas le DÉBIT des opérations mais leur LATENCE, et une
   mesure isolée (où les itérations sont indépendantes et se recouvrent)
   sous-estime largement leur coût réel une fois insérées dans la chaîne.
2. **Les deux tiers du coût d'un filtre à coupure fixe étaient un recalcul de
   coefficients identiques** : les plugins réécrivent la même fréquence à
   chaque échantillon (HPF du Juno-106 et du Jupiter-8, filtre du synthé de
   test), et chaque écriture déclenchait un `std::tan`.

**Trois changements retenus, tous exacts** (aucune empreinte de non-régression
n'a bougé -- ils ne changent pas un bit du rendu) :

- **Sortie anticipée des setters de filtre** (`LadderFilterZDF`,
  `StateVariableFilter`) quand la valeur écrite est identique à l'actuelle.
  L'identité est testée avec `isSameValue()` (deux comparaisons d'ordre plutôt
  qu'un `==`, pour rester propre sous `-Wfloat-equal` : ici l'égalité bit à
  bit est l'intention).
- **`std::exp2f(x)` au lieu de `std::pow(2.0f, x)`** dans toutes les
  conversions demi-tons -> Hz et coupure -> Hz (26 sites) : 2,4x moins cher.
  Substitution vérifiée **bit à bit sur 1 832 062 valeurs** couvrant la plage
  utile : sur cette libm, les deux fonctions donnent exactement le même
  résultat. (Sur une autre libm elles pourraient différer d'un ulp ; les
  tolérances des empreintes couvrent cet écart.)
- **Hauteur de base du Jupiter-8 figée au note-on** : `440 * 2^((note-69)/12)`
  ne dépend que du numéro de note et était recalculé à chaque échantillon pour
  chaque voix. Le drift, le vibrato et le detune, eux, restent per-échantillon
  -- ce sont eux qui modulent.

**Un changement mesuré puis JETÉ** (il est aussi utile de documenter ce qui ne
marche pas) : une version du ladder précalculant les puissances de G et
l'inverse du dénominateur de feedback, pour remplacer une division par une
multiplication dans la boucle. Gain réel : indiscernable du bruit (70,9 ns
contre 73,6). Le code étant plus compliqué, il a été abandonné -- le vrai coût
est la latence des deux `tanh`, pas l'arithmétique autour.

**Résultats** (unités étalon, voir ci-dessous ; plus bas = mieux) :

| Machine / charge | avant | après | gain |
|---|---|---|---|
| Synthé de test (8 voix) | 292 k | 117 k | **2,5x** |
| Juno-106 (6 voix) | 583 k | 371 k | **1,57x** |
| Jupiter-8 (8 voix) | 865 k | 680 k | **1,27x** |
| Minimoog (1 voix) | 94 k | 78 k | **1,20x** |
| **Graphe complet, 16 pistes** | 3,94 M | 2,99 M | **1,32x** |
| TB-303 (1 voix) | 60,4 k | 61,2 k | 1 % *plus lent* |

Le TB-303 module sa coupure à chaque échantillon : la sortie anticipée ne s'y
déclenche jamais et ne fait qu'ajouter un test. C'est un arbitrage assumé et
documenté dans le code : 1 % sur la machine la moins chère du lot contre 32 %
sur la charge réaliste.

**Le banc a dû être amélioré pour que ces chiffres veuillent dire quelque
chose.** Sur ce portable, la fréquence du cœur varie de 30 % entre le début et
la fin d'une même exécution (turbo puis throttling) : une machine non modifiée
pouvait afficher 434 k puis 1 037 k d'une exécution à l'autre. Le banc mesure
donc désormais un **étalon** (le coût d'une enveloppe ADSR, brique triviale et
stable) **juste avant chaque ligne**, et publie une colonne "étalons" = coût du
bloc exprimé en nombre d'opérations étalon. Cette colonne est reproductible à
~2 % près d'une exécution à l'autre, là où les millisecondes varient de 30 % ;
c'est elle qu'il faut comparer entre deux versions du code. Contrôle de
validité : le DX7, qui ne touche à aucun des chemins modifiés, affiche la même
valeur avant et après (~1,03 M).

### 9 quinquies. Piano roll complet : où vit quoi

Le piano roll est le composant le plus riche de l'application. Sa conception
suit la règle n°1 du projet (« le moteur ne dépend jamais de l'UI »), poussée
ici jusqu'au bout : **le composant JUCE ne contient aucune logique musicale**.

| Couche | Fichier | Contenu |
|---|---|---|
| Logique musicale | `core/.../NoteEdit.h/.cpp` | ~30 opérations pures sur des notes : transposer, décaler, legato, retirer les chevauchements, couper, fusionner, rétrograder, miroir des hauteurs, rampes de vélocité, humanisation, gammes (14 types), accords (13 types), arpèges (4 modes), sélection, statistiques |
| Historique | `core/.../EditHistory.h` | annuler/rétablir par instantanés, profondeur bornée |
| Interface | `app/Source/ui/PianoRoll*` | pixels ↔ ticks, gestes de souris, dessin, barre d'outils, règle, lane de vélocité |

**Pourquoi ce découpage.** Un « legato » ou un « arpéger » sont de la musique,
pas du dessin. Placés dans le composant JUCE, ils exigeraient un serveur
graphique pour être testés -- autant dire jamais. Placés dans `core/`, ils sont
couverts par 36 tests qui tournent en quelques millisecondes, et ils seront
réutilisables tels quels par un futur éditeur de motifs ou par les scripts
Python de la Phase 7. La règle pratique : **si une opération a un sens sur une
liste de notes sans écran, elle ne va pas dans l'UI.**

**Historique par instantanés, et pourquoi pas des commandes inversibles.** Une
piste, ce sont quelques milliers de `Note` de 32 octets : mémoriser l'état
complet coûte quelques dizaines de kilo-octets par pas, négligeable pour 128
pas. L'alternative (une commande inversible par opération) demanderait d'écrire
ET de tester une inverse correcte pour chacune des trente opérations, y compris
composées. Le coût est en mémoire, le gain est qu'aucun « annuler » ne peut
être faux : on restaure un état, on ne rejoue pas un raisonnement.

**Notes muettes** (`Note::muted`) : rendre une note silencieuse sans la
supprimer est un concept d'ÉDITEUR. Le `PlaybackScheduler` l'ignore, et
l'export MIDI aussi -- le format SMF n'a aucun moyen de représenter « présente
mais silencieuse », et l'écrire produirait un fichier qui joue autre chose que
ce qu'on entend. Les deux comportements sont testés.

**Écoute (audition).** Cliquer une touche du clavier du piano roll, dessiner
une note ou jouer sur un clavier MIDI branché produit un son immédiatement,
**même transport à l'arrêt** -- c'est justement là qu'on écoute ce qu'on
écrit. Techniquement : `ProcessGraph::sendLiveNote()` pousse l'événement dans
une file lock-free lue en tête du bloc audio suivant (latence maximale d'un
bloc, 10,7 ms à 512 échantillons), et `processBlock()` rend désormais les
instruments à l'arrêt **quand il y a quelque chose à jouer** (note d'écoute en
attente, ou voix encore en release) -- avec court-circuit immédiat sinon, pour
ne pas brûler du CPU à rendre du silence.

**Une file par source**, et ce n'est pas un détail : `LockFreeRingBuffer` est
strictement un producteur / un consommateur. L'interface et le thread MIDI sont
deux threads distincts ; les faire écrire dans la même file serait un bug de
concurrence silencieux, ne se manifestant que rarement et sous charge. D'où
`LiveNoteSource::Ui` et `LiveNoteSource::MidiInput`, chacune avec la sienne.

**Le menu Édition EST le menu contextuel du piano roll** : une seule
définition (`buildContextMenu()`/`performContextMenuAction()`), donc aucune
possibilité qu'une opération existe à un endroit et pas à l'autre.

Raccourcis : outils 1-6, `Ctrl+Z/Y` annuler/rétablir, `Ctrl+A/I` sélection,
`Ctrl+C/X/V/D` presse-papiers et duplication, `Ctrl+Q` quantifier, `Ctrl+L`
legato, `Ctrl+J` fusionner, `Ctrl+E` couper, `Ctrl+M` muet, `Ctrl+0` tout voir,
flèches pour déplacer/transposer (avec Maj : mesure entière / octave), `G`
aimant, `+`/`-` zoom.

---

### 9 sexies. SIMD : vectoriser entre les VOIX, pas dans le temps

**Le diagnostic d'abord.** Le filtre ladder coûtait ~70 ns par échantillon
alors que son arithmétique seule en vaut ~17. En reconstruisant son
`process()` morceau par morceau, on obtient : 73,7 ns pour la version du
projet, 69,2 en remplaçant les deux `tanh` par un polynôme, 61,2 en déroulant
les boucles de pôles, **52,1 avec les deux**. Autrement dit : supprimer un
tiers du travail ne fait gagner qu'un tiers du temps, et on reste très loin des
17 ns théoriques. La raison est structurelle -- un filtre récursif est une
CHAÎNE DE DÉPENDANCES : chaque opération attend le résultat de la précédente,
et une trentaine d'opérations flottantes enchaînées à ~4 cycles de latence
chacune donnent ~120-160 cycles, soit précisément ce qu'on mesure. Le
processeur ne travaille pas trop, il attend.

**La conséquence est directe** : on ne peut pas vectoriser un filtre dans le
TEMPS (l'échantillon n+1 a besoin du n), mais on peut occuper ces attentes avec
d'autres voix. D'où `LadderFilterZDFx4` : quatre filtres indépendants, une voix
par ligne SIMD, chacun avec sa coupure, sa résonance et son état.

| Brique | ns par échantillon et par voix |
|---|---|
| `LadderFilterZDF` (scalaire) | 71,6 |
| `LadderFilterZDFx4`, 4 voix d'un coup | 61,9 pour les quatre, soit **15,5 par voix** |

Soit **4,6x** -- davantage que le facteur 4 des lignes SIMD, précisément parce
que la version scalaire perdait du temps à attendre.

**Ce que ça a coûté en fidélité, mesuré et non supposé.** `std::tanh` n'a pas
d'équivalent SIMD : la saturation passe par `fastTanh`, une approximation
rationnelle. Écart avec `std::tanh` : **2e-7 dans le régime musical courant**
(|x| <= 2), 9,6e-5 au pire vers |x| ~ 5, là où tanh vaut déjà 0,9999 et où le
signal est de toute façon écrêté. La forme est exactement préservée (impaire,
monotone, bornée), ce que les tests vérifient explicitement -- une saturation
qui perdrait l'une de ces propriétés s'entendrait, contrairement à une erreur
d'amplitude de -134 dBFS. La version scalaire utilise **la même**
approximation : il serait indéfendable qu'une machine sonne différemment selon
qu'elle a été vectorisée ou non.

**Adoption, machine par machine.** Seules les machines POLYPHONIQUES peuvent en
profiter (il faut quatre voix à filtrer en même temps) :

| Machine | Voix | Avant | Après | Gain |
|---|---|---|---|---|
| Jupiter-8 | 8 (2 groupes pleins) | 680 k | 420 k | **1,63x** |
| Juno-106 | 6 (2 groupes, 2 lignes vides) | 371 k | 310 k | **1,20x** |
| Prophet | 5 (2 groupes, 3 lignes vides) | 384 k | 293 k | **1,31x** |

Les lignes inutilisées (Juno, Prophet) sont du gâchis assumé : deux filtres
vectorisés restent bien moins chers que cinq ou six filtres scalaires. Les
machines monophoniques (Minimoog, TB-303, SH-101, MS-20) et duophoniques (ARP)
n'ont qu'une ou deux voix : **elles restent scalaires, et c'est le bon choix**
-- il n'y a rien à mettre dans les autres lignes.

**Les empreintes de non-régression audio ont validé chaque étape** : écart
maximal **0,0000 %** pour le Jupiter-8 et le Juno-106, 0,0009 % pour le
Prophet. C'est exactement ce pour quoi elles ont été écrites (§ 9 bis) -- une
réécriture aussi invasive que « le filtre ne vit plus dans la voix » aurait
sinon demandé une écoute comparative machine par machine, avec le risque
d'accepter une dérive parce qu'on ne l'entend pas sur le motif qu'on a choisi.

**Bilan de la Phase 6, en unités étalon** (voir § 9 ter pour la méthode) :

| Charge | Début de Phase 6 | Fin de Phase 6 | Gain |
|---|---|---|---|
| Synthé de test (8 voix) | 292 k | 84 k | **3,5x** |
| Jupiter-8 (8 voix) | 865 k | 420 k | **2,06x** |
| Juno-106 (6 voix) | 583 k | 310 k | **1,88x** |
| Minimoog (1 voix) | 94 k | 54 k | **1,74x** |
| Distortion (effet) | 22,1 (relatif) | 6,3 | **3,5x** |
| **Graphe complet, 16 pistes** | 3,94 M | 2,82 M | **1,40x** |

**Prochaine cible désignée par la mesure, pas par l'intuition** : le DX7 est
désormais la machine la plus chère (1,02 M, 10 % d'un cœur à 8 voix) et n'a
aucun filtre -- son coût, ce sont 48 `std::sin` par échantillon (6 opérateurs
x 8 voix). La chaîne FM étant série par construction, la piste n'est pas le
SIMD mais une table d'ondes ou une approximation polynomiale du sinus, avec la
même exigence de preuve : mesurer l'écart, le documenter, laisser les
empreintes trancher.

---

## 28. Interopérabilité : la couche sémantique (Phase 7, P2-P3)

`interchange/` est la **seule** couche du projet autorisée à connaître JSON.
Le sens de la dépendance est à sens unique et vérifié : elle utilise
`vsm_audio` pour lire les `ParameterList` réelles, alors que `core/` et
`audio/` ne l'incluent nulle part -- le DAW joue et exporte sans qu'une
seule ligne de cette couche soit chargée.

### Trois niveaux d'identifiants (P2)

Un projet Python qui veut « ouvrir le filtre » ne peut connaître ni les
identifiants internes de chaque machine, ni leurs libellés d'affichage : le
Minimoog dit « Filter Cutoff », le Juno « VCF Cutoff », le MS-20 « LPF
Cutoff ». D'où une identité stable et partagée :

```
semanticId       filter.1.cutoff          <- ce que Python manipule
     v
paramètre VSM    "VCF Cutoff" (ParamId)   <- interne à la machine
     v
clap_id          2001                     <- à venir (P5)
```

**La table de correspondance vit dans `interchange/`, pas dans les plugins.**
Le vocabulaire d'échange est une affaire d'interopérabilité, pas de synthèse :
l'inscrire dans les machines obligerait à toucher et re-tester douze plugins à
chaque évolution du vocabulaire, et ferait entrer une préoccupation d'échange
de données dans du code dont le seul métier est de produire du son. Le prix est
un couplage par le NOM du paramètre -- précisément ce que le projet garde
stable, déjà verrouillé par les tests `..._parameter_list_size` de chaque
machine.

**308 paramètres** (12 machines + 9 effets) ont reçu une identité, dont
`accent.amount` pour le TB-303 ou `fm.operator.3.ratio` pour le DX7 : le
vocabulaire commun couvre ce qui est commun, et le reste est déclaré tel quel
plutôt que forcé dans une case qui ne lui va pas.

Trois tests tiennent cette table honnête :

- **complétude** : aucun paramètre d'aucune machine enregistrée ne peut rester
  sans identité -- ajouter une machine sans l'annoter casse le build au bon
  endroit, au lieu de produire silencieusement des presets amputés ;
- **unicité** : deux paramètres d'une même machine ne peuvent pas partager une
  identité (ils s'écraseraient l'un l'autre à l'import) ;
- **cohérence transversale** : `filter.1.cutoff` désigne bien la coupure du
  filtre principal, en Hz, sur les sept machines à filtre -- c'est toute la
  raison d'être du vocabulaire.

### Presets sémantiques `*.synth.json` (P3)

Ce format ne remplace pas `PresetState` (table `ParamId -> valeur`, interne et
illisible hors de sa machine) : il décrit un son en termes que l'extérieur
comprend, en **unités physiques** (Hz, secondes, demi-tons).

```json
{
  "format": "vsm-synth-preset", "version": 1,
  "name": "Acid Lead", "pluginId": "vsm.tb303", "fidelity": "derived",
  "parameters": { "filter.1.cutoff": 620, "filter.1.resonance": 0.82, "accent.amount": 0.7 }
}
```

Exemple complet et généré par le moteur : [`docs/examples/tb303-acid.synth.json`].

Trois décisions structurantes :

- **Refus explicite plutôt que lecture optimiste** : un `format` ou une
  `version` inconnus font échouer le chargement avec un message clair. Lire
  « au mieux » un fichier dont les champs ont changé de sens produirait un son
  faux sans prévenir -- le pire échec possible pour un outil de reconstruction.
- **Aucune approximation silencieuse** : appliquer un preset renvoie un rapport
  qui nomme chaque paramètre non pris en charge par la machine cible et chaque
  valeur bornée. Un preset de Jupiter-8 chargé dans un TB-303 transpose la
  coupure et la résonance, et DIT que la cross-mod n'existe pas.
- **`fidelity` n'est jamais `measured`** : aucune machine du projet n'a été
  comparée à du matériel réel (§ 27), le statut honnête est `derived`. Un test
  interdit la valeur `measured`.

### Projet `project.json` (P4)

Le fichier décrit le **transport** (résolution, changements de tempo en BPM
lisibles, signatures rythmiques, boucle), la composition des **pistes** (nom,
canal, couleur, instrument souhaité, mixage, sends, effets) et **référence** le
MIDI :

```
VSMProject/
├── project.json                        <- transport + pistes
├── midi/arrangement.mid                <- les notes
└── instruments/track_00.synth.json     <- un preset par piste
```

Exemple généré par le moteur : [`docs/examples/project.json`].

**Il ne recopie jamais les notes**, et c'est la décision structurante du
format. Les notes ont déjà un format universel que tout le monde sait lire et
écrire ; les dupliquer en JSON créerait deux vérités qui divergeraient au
premier désaccord, sans qu'on sache laquelle croire. Un test vérifie
explicitement qu'aucune note ne se retrouve dans le JSON.

Trois autres règles, chacune testée :

- **Chemins relatifs uniquement**, séparateurs `/`. Un chemin absolu
  (`/home/moi/...`, `C:\Users\...`), un antislash ou une remontée `..` font
  ÉCHOUER la lecture. Accepter un projet non portable revient à laisser le
  problème apparaître chez quelqu'un d'autre -- et `..` laisserait un fichier
  de projet désigner des données hors de son propre dossier.
- **Instrument manquant : signalé, jamais remplacé.** Une machine que
  l'installation ne possède pas est rapportée par son identifiant, la piste
  gardant son nom, ses notes et l'intention d'origine. Substituer une autre
  machine donnerait un rendu faux que personne ne remarquerait.
- **Appariement des pistes par ordre**, et écart signalé. C'est la seule
  convention qui ne dépende pas de noms que l'export MIDI a pu transformer.

Le fichier parle en **BPM** (lisible) là où le moteur travaille en
microsecondes par noire ; un test vérifie que l'aller-retour ne décale pas le
tempo, sur sept tempos usuels.

### Dossier de projet et reconstruction hors ligne (P7-P8)

Le « Mode A » de la roadmap, et le premier bout de chaîne complet entre le
projet Python et le moteur :

```
original.wav -> Python -> VSMProject/ -> vsm-render -> reconstructed.wav -> Python
```

**Pourquoi des fichiers plutôt qu'une API**, alors qu'une API paraît plus
moderne : un dossier s'inspecte, se versionne, se rejoue des mois plus tard et
se compare octet par octet. Il n'y a ni serveur à lancer, ni session à
maintenir, ni ordre d'appels à respecter. C'est ce qui rend une boucle
d'optimisation reproductible et débogable -- l'API temps réel (P9) viendra
au-dessus d'une base déjà stable, pas à sa place.

`vsm-render` rend un dossier de projet en WAV **sans interface ni carte son** :

```bash
cmake --build build --target vsm-render
./build/tools/vsm-render docs/examples/demo-project sortie.wav --tail 1.5
# rendu 3.32692 s (159692 échantillons), 2/2 piste(s) sonorisée(s), pic 0.576647
```

Il suit les conventions qu'attend un script appelant : code de sortie 0/1/2,
diagnostics sur stderr, résumé exploitable sur stdout. Les avertissements
sortent **même en mode silencieux** : un instrument manquant change le rendu, et
le taire donnerait un WAV faux que l'appelant croirait bon.

**Le rendu passe par le MÊME `ProcessGraph::processBlock()` que la lecture
temps réel** (§ 5). Sans cette propriété, une boucle d'optimisation pilotée par
Python optimiserait un son que personne n'entend jamais.

Trois comportements tenus par des tests de bout en bout, qui écrivent un vrai
dossier, le relisent et rendent un vrai WAV :

- **Déterminisme** : deux rendus des mêmes fichiers donnent des WAV identiques
  **octet pour octet** -- la condition posée au § 6 de la roadmap, sans laquelle
  Python comparerait du bruit d'un tour à l'autre.
- **Les presets agissent vraiment** : deux projets ne différant que par
  `filter.1.cutoff` produisent des rendus différents, et le plus ouvert a plus
  d'énergie. C'est le test qui sépare une chaîne d'interopérabilité qui marche
  d'une qui se contente de ne pas planter. Vérifié aussi à la main, en éditant
  le preset en Python puis en relançant `vsm-render`.
- **Dégradation honnête** : instrument absent -> le WAV existe, cette piste est
  silencieuse, et le rapport nomme la machine ; preset absent -> le projet
  s'ouvre avec les réglages par défaut en le disant. Un projet incomplet doit
  s'ouvrir et se dire incomplet, pas refuser de s'ouvrir. En revanche un
  `project.json` ou un MIDI absent est une erreur franche : rendre du silence
  laisserait croire à un morceau vide.

Exemple complet et jouable : [`docs/examples/demo-project/`] (project.json,
MIDI, deux presets), rendu en une commande.

### CLAP : adaptateur et hôte (P5-P6)

Deux directions, et une propriété commune : **le DSP n'est écrit qu'une fois**.

**P5, l'adaptateur** (`clap/adapter/`) expose les 11 machines VSM comme un
plugin CLAP (`vsm-instruments.clap`). Il **enveloppe** `ISynthPlugin`, il ne le
réimplémente pas -- toute autre approche donnerait deux versions du même
instrument qui finiraient par sonner différemment, sans que l'utilisateur
sache laquelle est la bonne. Un test compare les deux chemins échantillon par
échantillon (`clap_and_native_paths_produce_the_same_audio`) : identiques à
1e-6 près.

**P6, l'hôte** (`clap/host/`) charge un `.clap` externe et le présente au reste
du moteur **comme un `ISynthPlugin`**. Conséquence directe : `ProcessGraph`,
`AudioEngine`, le Synth Rack et le piano roll n'ont pas une ligne de code
spécifique à CLAP. La garantie « ajouter une machine ne touche ni le moteur ni
l'UI » (§ 22) vaut donc aussi pour les machines qu'on n'a pas écrites. Le seul
ajout au moteur est `ProcessGraph::setTrackInstrumentInstance()`, qui accepte
une instance déjà construite au lieu d'un identifiant de registre.

**Les identifiants de paramètres sont des hachages, pas des numéros d'ordre.**
Un hôte mémorise les `clap_id` dans SES projets (automations, assignations de
contrôleurs). Numéroter dans l'ordre de déclaration signifierait qu'insérer un
paramètre au milieu d'une machine décale tous les suivants -- et le projet d'un
utilisateur se mettrait à automatiser la résonance à la place de la coupure,
sans erreur ni avertissement, des mois plus tard. Le `clap_id` est donc un
hachage FNV-1a de l'identifiant SÉMANTIQUE : tant que `filter.1.cutoff` désigne
la même chose, son identifiant ne bouge pas. Le prix (risque de collision) est
**vérifié** sur les 308 paramètres, pas supposé, et trois valeurs sont **gelées
par test** -- si ce test casse, le correctif est de restaurer le hachage, jamais
de mettre à jour les nombres.

**L'état est enregistré en `*.synth.json` sémantique** plutôt qu'en table
d'identifiants internes : un projet d'hôte sauvegardé aujourd'hui reste lisible
même si les identifiants internes d'une machine changent, et reste inspectable
à la main.

**L'adaptateur et l'hôte se valident mutuellement** : les tests chargent le
`.clap` que le dépôt vient de construire. Aucun plugin tiers n'est nécessaire
pour prouver que les deux moitiés marchent -- et le jour où un plugin tiers
posera problème, on saura que le défaut vient de lui, puisque ce circuit fermé
est vert.

**Compilation : `-DVSM_BUILD_CLAP=ON`, désactivée par défaut.** C'est la seule
partie du projet qui exige un téléchargement (le SDK CLAP, en-têtes seuls, via
FetchContent). Vérifié : le build par défaut ne crée aucun `_deps/` et passe
ses 433 tests hors ligne. Activer CLAP impose aussi le code indépendant de la
position (`-fPIC`) aux bibliothèques statiques -- un module partagé ne peut pas
lier autre chose.

### Écriture des nombres

Les valeurs de paramètres sont des `float`. Écrites naïvement en `double`,
elles donnent `0.69999998807907104` là où la valeur EST 0,7 à la précision d'un
float -- un format censé être relu et corrigé à la main perd sa raison d'être
s'il est illisible. `JsonValue::makeFloat` retient donc la plus courte écriture
qui se relit **au bit près**, testé sur les deux exigences à la fois.

### Pourquoi un parseur JSON maison

Le projet tient à ce qu'un `cmake && make` fonctionne hors ligne (c'est déjà la
raison d'être du parseur MIDI et du framework de tests maison). Importer
nlohmann/json via FetchContent imposerait une connexion réseau au premier build
de cette couche, pour un besoin de quelques centaines de lignes : les formats
sont écrits ET lus par nous, sans JSON exotique ni flux gigantesques. Le
parseur refuse les extensions non standard (commentaires, virgules finales) --
mieux vaut rejeter un fichier douteux que l'interpréter de travers.

---

## 30. `vsm.sampler` — rejouer ce qu'aucune synthèse ne reproduit

**Pourquoi cette machine avant toute autre** : sur un enregistrement réel, les
stems de batterie et de basse n'avaient jusqu'ici aucune machine cible. Aucune
synthèse soustractive ne fera une caisse claire enregistrée ; un échantillon,
si. Et le projet d'analyse dispose déjà du matériau -- il isole le stem de
batterie et découpe les frappes : **ces extraits sont les échantillons**.
Mesuré de bout en bout : un coup découpé, chargé dans un emplacement puis
rejoué, corrèle à **1,0000** avec l'original.

**Convention de jeu empruntée aux boîtes à rythmes** : la note MIDI
SÉLECTIONNE un emplacement, elle ne transpose pas. Un pad joue son son, à sa
hauteur ; l'accord se règle par `Tune`. Transposer un coup de caisse claire
selon la touche produirait n'importe quoi -- et les notes par défaut suivent la
convention General MIDI, pour qu'un MIDI de batterie transcrit tombe
directement sur les bons emplacements.

**Portée assumée : 8 emplacements**, là où le cahier des charges en prévoit 16.
Les huit de plus attendent que la façade sache sélectionner un emplacement :
seize colonnes de sept réglages afficheraient 112 commandes illisibles. C'est
une limite écrite, pas un oubli.

**Contraintes tenues**

- Le chargement des fichiers a lieu **hors du thread audio**, publication par
  échange atomique de `shared_ptr` — et la voix capture le pointeur au
  déclenchement, si bien que recharger un emplacement pendant qu'il joue ne
  détruit pas la donnée sous ses pieds.
- **Interpolation cubique** (Catmull-Rom) : l'interpolation linéaire suffirait
  à faire du son, mais ajouterait un repliement audible dès qu'on transpose --
  or cet ajout serait ensuite mesuré comme un écart avec l'original, dans un
  outil dont le métier est justement de mesurer cet écart.
- **Compensation de la fréquence du fichier** : un échantillon 44,1 kHz sur un
  moteur à 48 kHz garde sa hauteur (testé).
- **Un échantillon manquant est signalé, jamais substitué** : l'emplacement
  reste silencieux et le rapport le nomme.

**Nouveauté nécessaire au moteur** : `ISampleLoader`, interface séparée
d'`ISynthPlugin`. Cette dernière ne transporte que des flottants -- ce qui rend
l'automation, le MIDI Learn et CLAP uniformes -- et un chemin de fichier n'y
entre pas. Plutôt que d'imposer une indirection à toutes les machines pour en
servir une, les machines concernées implémentent l'interface EN PLUS ; les
appelants qui en ont besoin font un `dynamic_cast`, et le moteur continue de ne
voir qu'un `ISynthPlugin`.

Le service de rendu accepte désormais un champ `samples` : c'est ce qui rend le
sampler utilisable depuis le projet d'analyse, sans lequel la machine la plus
utile à la reconstruction serait restée hors de portée.

---

## 29. Façades « façon hardware », machine par machine (sections 6 et 21)

Le panneau générique (un potentiomètre par paramètre) reste le filet de
sécurité -- il couvre toute machine, y compris un plugin CLAP tiers dont on
ignore la disposition. Mais régler un Minimoog dans une liste alphabétique de
boutons n'a rien à voir avec le régler sur sa façade : **la disposition FAIT
partie de l'instrument**, parce qu'elle porte le trajet du signal et les
gestes qu'on a appris.

### Une description, pas douze composants

Chaque machine est une DONNÉE (`vsm_panels`, sans JUCE ni moteur audio) :
blocs, commandes, styles, couleurs, positions. Un unique composant JUCE sait
rendre n'importe laquelle. Écrire douze composants reviendrait à recopier
douze fois la même mécanique -- créer un potentiomètre, le relier, le placer,
le redessiner -- avec douze occasions de diverger.

Le vrai gain est ailleurs : **des données se testent**, sans serveur
graphique. Neuf tests tiennent les façades honnêtes, dont trois qui comptent :

- **aucune commande ne pointe vers un paramètre inexistant** : un renommage
  dans une machine ferait autrement un bouton mort, sans erreur ni trace ;
- **aucun paramètre n'est silencieusement inatteignable** : tout paramètre est
  soit posé sur la façade, soit déclaré omis AVEC SA RAISON ;
- **la disposition d'origine est verrouillée** : le TR-808 doit garder une
  colonne PAR PIÈCE (grosse caisse, caisse claire...), et surtout pas un bloc
  « tous les niveaux » puis un bloc « tous les decays » -- plus compact, et
  inutilisable, parce qu'on règle une pièce, pas une catégorie.

### Ce qui est reproduit, et ce qui ne l'est pas

Agencement, familles de commandes, code couleur, matière du châssis : ce qui
rend la machine reconnaissable et surtout utilisable par qui connaît
l'original. Ni logo, ni marque, ni sérigraphie littérale -- le projet dit
« -style » partout, et cette règle vaut aussi pour l'image.

### Regarder le résultat, pas seulement le tester

`vsm-panel-preview` rend chaque façade en PNG **sans écran** :

```bash
cmake --build build --target vsm-panel-preview
./build/app/vsm-panel-preview_artefacts/RelWithDebInfo/vsm-panel-preview docs/images/panels 1200
```

Les tests garantissent la cohérence, jamais la lisibilité. Cet outil a
immédiatement montré trois défauts qu'aucun test n'aurait attrapés : des
potentiomètres réduits à des points, des blocs quatre fois trop hauts pour
leur contenu, et des afficheurs « 0.4500000 s » sous chaque bouton -- alors
qu'une façade n'affiche aucun chiffre. D'où les choix actuels : une grille
INTERNE à chaque bloc (les commandes remplissent leur cadre), et un afficheur
unique en bas de façade, qui ne parle que lorsqu'on règle quelque chose.

Aperçus : [`docs/images/panels/`].

### Le séquenceur fait partie de la machine

Sur une boîte à rythmes ou un TB-303, le séquenceur n'est pas un accessoire :
**on ne joue pas un TR-808 au clavier, on allume des pas**, et un TB-303 sans
son éditeur de motif n'est qu'un filtre. Une façade sans sa grille serait donc
fausse, quelle que soit la fidélité des potentiomètres.

**La grille est une VUE sur les notes de la piste**, convertie dans les deux
sens (`core/.../StepPattern.h`, 11 tests). Allumer un pas écrit une note ;
dessiner cette note au piano roll rallume le pas. Stocker les motifs à côté du
morceau créerait deux vérités -- ce que montre la grille et ce que joue le
moteur -- qui divergeraient dès la première édition faite ailleurs.

Trois traductions, choisies pour coller à ce que ces machines font vraiment :

- **accent** : leur seule nuance (ces claviers ne sont pas sensibles à la
  vélocité), donc deux valeurs franches plutôt qu'un dégradé qui n'existe pas ;
- **slide** : un CHEVAUCHEMENT avec le pas suivant -- exactement ce que le
  TB-303-style interprète déjà en glissando (§ 8). Le motif n'invente aucune
  convention, il produit ce que le moteur sait lire ;
- **détaché** : un pas ne tient pas jusqu'au suivant, sinon la boîte à rythmes
  « bave ».

Deux garde-fous testés : une note posée hors grille au piano roll n'est **pas**
rapprochée du pas voisin (la grille afficherait un motif que le morceau ne joue
pas -- elle l'ignore et la laisse intacte), et écrire un motif ne remplace que
**sa propre fenêtre** de 16 pas, jamais le reste du morceau.

Les boutons de pas sont colorés **par groupes de quatre**, comme sur les
machines d'origine : c'est ce qui permet de compter les temps sans les compter.

### État : les onze machines ont leur façade

| Machine | Forme de la façade |
|---|---|
| Sampler | une colonne par emplacement, grille de 16 pas ; les réglages de mapping sont déclarés omis |
| Minimoog | banc à trajet de signal (oscillateurs -> mixage -> modifieurs), flancs bois, gros bouton de coupure |
| TB-303 | rangée unique sur boîtier argenté, liseré rouge, + éditeur de motif |
| TR-808 / TR-909 | une colonne de commandes PAR PIÈCE, code couleur par famille, + grille de 16 pas |
| SH-101 | curseurs alignés, boîtier bleu-gris |
| Juno-106 | curseurs par bloc (LFO, DCO, HPF/VCF, ENV), panneau noir et liserés bleus |
| Jupiter-8 | curseurs + potentiomètres, deux VCO, deux enveloppes |
| Prophet | tout au potentiomètre, flancs bois, bloc POLY-MOD séparé |
| MS-20 | ses DEUX filtres côte à côte, comme sur la machine |
| ARP Odyssey | curseurs plats, sérigraphie dorée, ring mod près des VCO |
| DX7 | matrice des six opérateurs, un par colonne |

Le **DX7 est un cas à part, et il faut le dire franchement** : la machine
d'origine n'a presque aucune commande visible -- clavier à membrane, afficheur
de deux lignes, curseur de données, et tout passe par des menus. La reproduire
littéralement donnerait une façade fidèle et inutilisable, où régler un
opérateur demanderait dix pressions. La façade montre donc ce que la machine
CACHE : la matrice des six opérateurs, convention de tous les éditeurs FM
depuis quarante ans, et seule disposition qui permette de comparer deux
opérateurs d'un coup d'œil.

Seul le synthé de test n'a pas de façade : il n'imite aucune machine, le
panneau générique lui va très bien.

### Deux bugs que seul le rendu a montrés

Aucun test ne les aurait attrapés, et ils sont typiques de ce que la mise en
page cache :

- **`removeAllChildren()` emportait les enfants permanents** (afficheur de
  valeur, séquenceur) ajoutés au constructeur. Panne parfaitement silencieuse :
  les composants existaient toujours, simplement détachés.
- **Le plafond de hauteur des potentiomètres décalait le PAS des rangées** :
  une rangée placée sous une rangée « plafonnée » remontait par-dessus les
  commandes voisines qui, elles, occupaient toute leur hauteur -- d'où des
  curseurs et des potentiomètres qui se chevauchaient sur le Jupiter-8. Le pas
  d'une grille ne doit jamais dépendre de la taille dessinée.

---

## 10. Plan de développement

| Phase | Contenu | Statut |
|---|---|---|
| **1 — Application** | ... | **Fait, testé, vérifié fonctionnel** |
| **2 — Fondations audio** | AudioEngine, plugin arch, automation, mixer | **CLOSE** : moteur (mixer, sends, bus master, 9 effets d'insert, automation sample-accurate), UI (Mixer, sélecteur de device, éditeur d'effets, éditeur d'automation, MIDI Learn) et export WAV, tous faits |
| **3 — Instruments de référence** | Minimoog-style, TB-303-style, Juno-style | **CLOSE. Les trois faits, testés, audibles** (`*_demo.wav`) |
| **4 — Extension** | TR-808/909-style, SH-101-style, Prophet-style | **CLOSE : les quatre faits** (TR-808, TR-909, SH-101, Prophet) |
| **5 — Synthèses avancées** | DX7-style FM, MS-20-style, Jupiter-style, ARP-style | **CLOSE : les quatre faits**, testés et audibles (`*_demo.wav`) |
| **6 — Qualité** | Profiling, SIMD, oversampling, unification transports, UI Mixer/device selector, tests de non-régression audio par machine | **CLOSE** : non-régression audio par machine (§ 9 bis), banc CPU + oversampling polyphase (§ 9 ter), profiling intra-DSP + optimisations exactes (§ 9 quater), SIMD entre voix (§ 9 sexies), unification des transports + boucle échantillon-exacte (§ 6 bis). Bilan : graphe 16 pistes **1,40x** plus rapide, Jupiter-8 **2,06x**, Distortion **3,5x**, à empreintes audio inchangées |
| **7 — Interopérabilité** | ParameterDescriptor sémantique, `*.synth.json`, `project.json`, CLAP, import Python, API locale | **P2-P8 FAITS** : identités sémantiques (308 paramètres), presets, projets, dossier de projet, rendu hors ligne `vsm-render`, adaptateur et hôte CLAP — 59 tests (§ 28). Ne reste que P9 (API locale, Mode B) |

**Reste de la Phase 2 avant la Phase 4.** Fait : le **bus master** (section
15) -- EQ 3 bandes, compresseur, saturation, largeur stéréo, limiteur
brickwall à plafond garanti, mètre LUFS ; branché sur le bus stéréo final du
`ProcessGraph`, bypass par défaut. Restent, dans l'ordre : suite d'effets
d'insert réutilisables (delay/ping-pong, reverb, flanger, phaser,
distorsion/overdrive, bitcrusher ; le chorus BBD est déjà prêt à promouvoir)
-> oversampling 2x/4x/8x pour les étages non linéaires -> MIDI Learn ->
composants JUCE (Mixer UI, automation UI, sélecteur de device). Le moteur de
calcul (Mixer, AutomationLane, MasterBus) est en place et testé ; il manque
surtout les briques DSP d'effets et les composants JUCE (ces derniers se
compilent sur une machine de dev, pas dans un conteneur headless sans X11).
La Phase 4 (nouvelles machines) peut avancer en parallèle sans toucher au
moteur, grâce au `PluginRegistry`.
