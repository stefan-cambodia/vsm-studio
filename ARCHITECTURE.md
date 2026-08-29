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
23 machines a désormais une empreinte de rendu figée, qui détecte toute dérive
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
écoute au clic, et toute la logique testée hors JUCE. Total : **830 tests moteur** (84 core + 613 audio
+ 111 interchange + 11 CLAP + 11 façades,
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

### Bilan actuel : 830 tests moteur + 18 tests d'analyse, tous verts

- **84 tests `vsm_core`** (dont l'édition du piano roll : opérations de
  notes, gammes, accords, arpèges, historique annuler/rétablir, parcours des
  notes douteuses de la transcription),
  **613 tests `vsm_audio`** (dont le SIMD : équivalence avec le filtre
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

**563 paramètres** (23 machines + 9 effets, 308 à l'époque des 12 machines)
ont reçu une identité, dont
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

### L'automation voyage dans le projet (suite de P4)

Le format `project.json` transporte désormais des COURBES D'AUTOMATION par
piste : un paramètre visé par son identité sémantique, des points
`(tick, valeur)` en unités réelles, des segments linéaires ou en palier. Champ
facultatif — un projet sans automation garde exactement le fichier qu'il a
toujours eu. Le rendu hors ligne résout l'identité vers le paramètre de la
machine et pousse les courbes dans le `ProcessGraph`, qui les applique en
sous-blocs de 64 échantillons ; une courbe visant un paramètre que la machine
n'a pas est rapportée, jamais ignorée. Un test rend une rampe de coupure
écrite dans le JSON et vérifie que la seconde moitié du son est réellement
plus brillante — la preuve audible, pas seulement structurelle.

**Ce que la chaîne d'analyse en fait.** Un patch figé ne dit qu'une moyenne,
et le caractère d'un morceau vit souvent dans le mouvement — la coupure qui
balaye est l'âme d'une ligne acide. `analyse/analyzer/vsm_automation.py`
extrait la TENDANCE du centroïde spectral du stem (lissage médian d'une
seconde : le mouvement interne des notes appartient aux enveloppes de la
machine, l'écrire en automation l'appliquerait deux fois — mesuré : la courbe
par trame AGGRAVAIT la distance), la traduit en coupure par une relation
APPRISE SUR LA MACHINE ELLE-MÊME (deux rendus aux bornes, interpolation
log-log — l'hypothèse « centroïde = coupure » était trois fois trop plate),
puis la met à l'épreuve : deux mini-projets rendus par `vsm-render`, avec et
sans la courbe, et la courbe n'est GARDÉE que si la distance mesurée baisse.
La calibration se fait AUTOUR DU POINT DE FONCTIONNEMENT (base ÷4 à ×4) et
non aux bornes : à 20 Hz un filtre est muet et le centroïde mesuré est celui
du bruit numérique. Elle accepte les pentes INVERSÉES, parce que la mesure en
a trouvé une : sur un patch au type de filtre continu réglé vers le
passe-bande, monter la coupure assombrit la note (424 Hz → centroïde 2672 ;
6776 Hz → 1242). Et quand la coupure ne pilote pas le timbre sous le patch
trouvé — le cas du stem de basse de House Of God, mesuré à la note médiane —
l'automation est déclinée EN LE DISANT, motif imprimé. Contrôles : cible
balayée connue, courbe gardée (5,25 → 3,85) ; cible statique, rejetée par la
mesure (elle aurait coûté 6,59). L'heuristique propose, le chiffre dispose.

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

**P5, l'adaptateur** (`clap/adapter/`) expose les machines VSM comme un
plugin CLAP (`vsm-instruments.clap`) -- TOUTES les machines du registre, hors
générateur de tonalité d'essai, interrogé au chargement. La première version
énumérait onze machines à la main, et le défaut est resté invisible pendant
huit ajouts : le sampler, l'e-piano, l'OB-X, le supersaw, la table d'ondes,
l'hybride PCM, l'orgue et le synthé neutre existaient dans le DAW mais pas
dans les hôtes CLAP -- sans erreur nulle part, parce que le test de couverture
se contentait de « au moins 11 ». Le test compare désormais l'ENSEMBLE exposé
à l'ensemble enregistré, ce qui rend l'oubli du prochain ajout impossible.
L'adaptateur **enveloppe** `ISynthPlugin`, il ne le
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
**vérifié** sur tous les paramètres du parc (563 aujourd'hui), pas supposé,
et trois valeurs sont **gelées
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
ses 644 tests hors ligne. Activer CLAP impose aussi le code indépendant de la
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

**Seize emplacements, et le blocage était l'AFFICHAGE.** La première version
s'arrêtait à huit, non par manque de place en mémoire mais parce que seize
emplacements à sept réglages font cent douze commandes, illisibles alignées. Le
blocage a donc été levé là où il était : la façade montre les seize pièces en
deux rangées de huit, réduites aux quatre réglages qui SE JOUENT (niveau,
accord, décroissance, panoramique). Les trois autres — note de déclenchement,
point de départ, groupe de coupure — sont des réglages de configuration, posés
une fois par l'analyse ; ils restent accessibles par le panneau générique et
sont déclarés omis, avec leur raison.

**C'est aussi ce qui rend `vsm.drumkit` inutile à écrire.** Le §5 de
`docs/CDC-machines-manquantes.md` prévoyait une boîte à rythmes générique — 8 à
16 pièces, une colonne de réglages par pièce, grille de 16 pas, la façade des TR
sans leur synthèse — en laissant ouvert le choix entre un profil du sampler et
une machine distincte. En reprenant la liste point par point, le sampler la
remplissait déjà en entier : seize pièces, seize sections de quatre réglages,
une grille de seize pas, la note qui SÉLECTIONNE la pièce au lieu de la
transposer, les notes par défaut de la convention General MIDI, et les groupes
de coupure pour la charleston. Il manquait le NOM DES PIÈCES, la façade disant
« SLOT 3 » là où une boîte à rythmes dit « HH CL ».

Ce nom a été ajouté, et rien d'autre. Une seconde machine aurait partagé le
moteur, les paramètres, les identités sémantiques et la façade de la première,
pour coûter une empreinte, une table sémantique et une batterie de tests
supplémentaires — un changement d'étiquettes payé au prix d'une machine. Le
numéro d'emplacement reste en tête du titre (« 3 HH CL ») parce que les
paramètres s'appellent `Slot 3 Level` et que l'analyse écrit dans
l'emplacement 3 : le nom dit ce que General MIDI met là par défaut, le numéro
dit ce qu'on règle. Un emplacement réassigné à une autre note porte donc une
étiquette inexacte — limite assumée d'un kit dont les pièces sont
réassignables. Et la grille ne programme que les huit premières : les seize se
déclenchent, les toms et percussions se jouent au piano roll.

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

## 31. `vsm.generic` — une machine sans caractère, faite pour être ajustée

**Le problème qu'elle résout.** Les dix-huit autres machines du parc sont des
personnages : le drive du ladder Moog, la dérive analogique, le chorus BBD du
Juno sont présents *quoi qu'on règle*. C'est leur raison d'être quand on joue,
et c'est un défaut quand on cherche : si le son cible ne les a pas, aucun jeu
de paramètres ne l'atteindra. Mesuré sur une cible pourtant rendue par le
Minimoog lui-même, la recherche de patch converge vers 1125 Hz de coupure pour
une cible à 900 Hz, et 1,18 de résonance pour 2,2. Le pipeline fonctionne :
c'est l'espace qui est difficile. `vsm.generic` est la machine qu'on ajoute
pour que cet espace cesse de l'être — quarante paramètres, huit voix, aucune
signature sonore propre.

**Quatre exigences, chacune contre un obstacle mesuré de l'optimisation.**
Elles n'ont de sens que pour cette machine, et chacune a ses tests.

1. **Continuité.** Tout ce qui peut être continu l'est, *forme d'onde et type
   de filtre compris*. Un sélecteur discret creuse une falaise dans la fonction
   de coût, où une recherche par descente se bloque. La forme d'onde est donc
   un nombre de 0 à 3 (sinus → triangle → dent de scie → carré) et le type de
   filtre un nombre de 0 à 2 (passe-bas → passe-bande → passe-haut). Les tests
   balaient finement les deux et refusent tout écart entre voisins nettement
   plus grand que l'écart médian.
2. **Neutralité au repos.** Ce qui est réglé à zéro disparaît vraiment : à
   drive nul la saturation est *rigoureusement* l'identité, sources muettes
   n'ajoutent *rien*, et à réglages neutres sur forme sinusoïdale le spectre ne
   porte pas de partiel parasite. Trois tests le vérifient à 1e-12 près.
3. **Monotonie.** Ouvrir la coupure augmente le contenu aigu, monter la
   résonance accentue la bande, monter le drive ajoute des harmoniques —
   toujours, sans retournement. Un paramètre non monotone piège toute descente,
   qui s'arrête au premier retournement en croyant tenir un optimum.
4. **Découplage.** Un paramètre agit sur une dimension et une seule. Là où le
   couplage est physique, il est **compensé**, et la compensation est mesurée
   plutôt que déduite (voir plus bas).

**Un oscillateur morphable, une seule phase.** Les quatre formes sont calculées
à la *même* phase puis fondues deux à deux. Quatre oscillateurs indépendants
qu'on mélangerait auraient des phases distinctes : à fréquence égale, deux
formes déphasées se peignent au lieu de se fondre, et le fondu ferait entendre
un creux là où il ne devrait rien faire. La dent de scie et le carré gardent la
correction polyBLEP du reste du parc — sans elle la machine deviendrait
d'autant plus fausse qu'on monte dans l'aigu, exactement le contraire de ce
qu'on attend d'un instrument de mesure.

**Une brique partagée a dû s'ouvrir : `StateVariableFilter::processMulti()`.**
Une topologie à variable d'état calcule le passe-bas, le passe-bande et le
passe-haut *dans le même pas d'état* ; `process()` en rendait un et jetait les
deux autres. Fondre continûment d'un type à l'autre exige les trois — et les
obtenir par trois appels ferait avancer l'état trois fois par échantillon,
c'est-à-dire tourner le filtre à triple fréquence. C'est le même piège que
celui rencontré sur la cascade 24 dB/oct de l'OB-X ; il est désormais rendu
impossible, et un test compare les trois sorties simultanées à celles de trois
filtres réglés séparément, échantillon par échantillon.

**La compensation résonance → niveau, et pourquoi son exposant est mesuré.**
Un filtre à variable d'état amplifie autour de sa coupure quand le Q monte ;
sans correction, tourner la résonance monterait aussi le volume et l'optimiseur
confondrait les deux effets. Une première version divisait par la racine du Q —
la correction théorique du pic — et faisait **chuter** le niveau d'un facteur
2,8 : la compensation était pire que le couplage. La hausse du niveau *global*
est en effet bien plus faible que celle du pic, puisque seule une bande étroite
est accentuée. Mesuré sur une dent de scie filtrée à 2 kHz, le niveau efficace
passe de 0,170 à 0,206 quand la résonance va de 0 à 1, soit +21 % : l'exposant
retenu (0,08) vient de cette mesure, et deux tests vérifient que ni la
résonance ni le drive ne déplacent le niveau d'ensemble.

**Le profil de recherche est déclaré, pas deviné.** Les familles propres à
cette machine (`oscillator.#.shape`, `filter.#.type`, `output.drive`,
`oscillator.noise.colour`…) reçoivent leurs bornes utiles, leur échelle et leur
importance dans `interchange/src/SearchProfile.cpp`. Les deux premières sont
déclarées très rentables à chercher (0,92 et 0,86) : étant continues par
construction, elles explorent tout le passage sinus → carré ou passe-bas →
passe-haut sans le moindre palier, là où un sélecteur discret bloquerait la
descente. Le projet
d'analyse n'a donc rien à coder en dur pour s'en servir.

**Ses identités sémantiques sont volontairement les plus canoniques du
vocabulaire** — `oscillator.1.shape`, `filter.1.cutoff`, `envelope.2.attack` —
et aucune ne lui est propre. C'est ce qui permet à un patch trouvé sur elle
d'être transposé vers une machine de caractère, et réciproquement.

**Sa façade est la seule du parc à ne ressembler à aucune machine**, parce
qu'elle n'en imite aucune : gris neutre, sérigraphie sobre, aucune couleur de
caractère. Elle reproduit un **schéma** et non un instrument, et la lecture va
de gauche à droite le long du signal — sources, filtre, enveloppes, modulation,
sortie — comme un synoptique de manuel. On doit voir au premier regard qu'on
est devant un outil.

**Ce qui reste ouvert et assumé.** La courbe d'enveloppe (linéaire ↔
exponentielle) figurait au cahier des charges et n'est pas exposée : les deux
ADSR emploient la courbe partagée du parc. C'est une dimension de moins pour la
recherche, écrite ici plutôt que découverte plus tard.

### La promesse, mesurée : avant / après, chiffres publiés

Le cahier des charges exige de mesurer la distance AVANT et APRÈS l'ajout de
la machine, et de publier les chiffres « flatteurs ou non ». Les voici — ils ne
sont pas flatteurs partout, et c'est précisément ce qui les rend utilisables.

**Protocole.** Trois cibles-vérité rendues par des machines du parc, patchs
connus du seul auteur du test : une basse SH-101 (coupure 900 Hz, résonance
2,2), une nappe Juno-106 (chorus actif), une cloche DX7. Recherche sur les
seize machines mélodiques, présélection DÉSACTIVÉE (chaque machine reçoit le
même budget complet, sans quoi comparer leurs distances ne voudrait rien
dire), 20 itérations, métrique v2.

**1. Le choix de machine, avant et après.** « Avant » = la meilleure machine
hors `vsm.generic` ; « après » = avec elle dans les candidates.

| Cible | Vraie machine | Avant | Après | `vsm.generic` |
|---|---|---|---|---|
| basse SH-101 | `vsm.sh101` | sh101, 0,055 | sh101, 0,055 | 0,190 (9e/16) |
| nappe Juno-106 | `vsm.juno106` | juno106, 0,109 | juno106, 0,109 | 0,212 (10e/16) |
| cloche DX7 | `vsm.dx7` | dx7, 0,114 | dx7, 0,114 | 0,223 (3e/16) |

Deux lectures. **Le risque est écarté** : la machine neutre ne vole jamais
l'identification à la vraie machine — c'était le danger principal, une machine
qui gagne en distance ce qu'elle fait perdre en « quel synthé est-ce ? ».
**Le gain est nul sur ces cibles** : quand la cible a été produite par une
machine du parc, cette machine gagne, et c'est le résultat attendu. En
retirant la vraie machine (le vrai cas de reconstruction : la cible n'a pas de
machine exacte), le generic finit 8e, 9e et **2e** sur quinze — la seule
percée est la cloche FM, hors de la famille soustractive.

**2. Le budget de recherche est une partie de l'explication — pas toute.**
Hypothèse testée : la recherche n'explore que les six axes les plus
importants ; or le generic en a quarante, et tout ce qui fait sa largeur
(deuxième oscillateur, sub, bruit, LFO, drive) n'est jamais touché. Mesuré
contre le meilleur remplaçant de chaque cible, à budget croissant :

| Cible / budget | 6 axes, 20 itér. | 12 axes, 40 itér. | 20 axes, 60 itér. |
|---|---|---|---|
| basse — generic | 0,190 | 0,170 | **0,132** |
| basse — ms20 | **0,098** | **0,072** | 0,144 |
| nappe — generic | 0,212 | 0,174 | 0,182 |
| nappe — pcmhybrid | **0,133** | **0,120** | **0,117** |
| cloche — generic | 0,223 | 0,223 | **0,117** |
| cloche — ms20 | **0,183** | **0,140** | **0,103** |

Trois faits en sortent. Le generic est la seule machine qui PROFITE d'un
espace élargi sur la basse (−31 %, jusqu'à passer devant le ms20 à 20 axes) ;
les machines étroites PEUVENT au contraire se diluer quand on leur ouvre trop
d'axes (ms20 sur la basse : 0,072 → 0,144 — soixante itérations dans vingt
dimensions cherchent moins bien que quarante dans douze ; mais le même ms20
profite du même élargissement sur la cloche, 0,140 → 0,103 : la dilution
dépend de la cible, elle n'est pas une loi) ; et à leurs meilleurs budgets
respectifs, les machines de caractère restent devant sur les trois cibles
(0,072 contre 0,132 ; 0,117 contre 0,174 ; 0,103 contre 0,117). Le plafond de
six dimensions pénalise bien le generic structurellement — il est la seule
machine dont la distance ne descend QUE si on l'élève — mais le lever ne
suffit pas à le faire gagner sur de l'audio propre : sur ces cibles-là, la
signature de la vraie famille l'emporte, et le terrain du generic reste celui
que la chaîne complète a montré — l'audio séparé, sans machine d'origine dans
le parc.

**3. La chaîne complète, avant et après.** Même vérité terrain jouée en
morceau (basse + nappe mixées), puis `reconstruire.py` de bout en bout :
séparation, transcription, recherche. Sur le stem de basse SÉPARÉ, le generic
**gagne** : 0,139 contre 0,149 au meilleur sans lui — et la vraie machine
(sh101) n'est même pas sur le podium. Sur les stems PROPRES (sans séparation),
à l'inverse, le generic ne monte sur aucun podium. Sur la basse, le résultat
avec ou sans lui est identique au chiffre près (arpodyssey 0,053 et distance
globale 4,5567 dans les deux cas — l'égalité exacte est aussi la preuve que la
chaîne, une fois la séparation seedée, est redevenue déterministe). Sur la
nappe, la mesure attrape un effet plus fin : sans generic, le pcmhybrid gagne
à 0,128 ; avec, le generic l'ÉVINCE de la présélection — les finalistes sont
en nombre fixe — et le meilleur restant fait 0,131. Ajouter une candidate ne
vole donc jamais l'identification, mais peut coûter une place de finaliste à
la machine qui aurait gagné : c'est le prix, petit et désormais chiffré
(+0,003 ici), de toute présélection à budget constant — `shortlist=0` le
supprime quand l'exactitude prime sur le temps.

La lecture d'ensemble est cohérente : sur de l'audio propre sorti d'une
machine du parc, les machines de caractère gagnent — rien d'étonnant, la cible
EST leur signature. C'est sur l'audio réellement à reconstruire — séparé,
teinté d'artefacts, sans machine d'origine dans le parc — que la neutralité
paie, parce qu'aucune signature ne colle et que la machine ajustable s'approche
le plus. C'est exactement le périmètre que le cahier des charges lui donnait.

**Suite donnée (règle à deux étages, mesurée).** Le balayage complémentaire —
quatre machines de largeurs différentes (21 à 50 dimensions déclarées), trois
cibles, deux graines, itérations FIGÉES au défaut de la chaîne — a tranché ce
que l'expérience de budget laissait ouvert : à itérations fixes, ouvrir 10
axes gagne dans 8 cellules sur 12, et les gains majeurs (DX7 sur sa cloche :
0,112 → 0,075 ; MS-20 sur la basse : 0,098 → 0,074) vont aux machines
cherchées DANS leur famille, c'est-à-dire aux futures gagnantes ; les trois
régressions touchent des machines hors famille qui ne gagnaient jamais, si
bien qu'aucun verdict ne s'inverse. Quatorze axes n'apportent plus rien. Le
generic, lui, ne profite presque pas des axes seuls : ses gains d'hier
venaient du couple axes × itérations — l'hypothèse « le plafond de six le
bride » était à moitié vraie, et la mesure a corrigé l'autre moitié. D'où la
règle adoptée dans `choose_machine` : 6 axes pour DÉGROSSIR (la passe payée
par toutes les candidates), 10 pour RÉGLER les finalistes (+60 % sur cette
passe seule). Vérifié de bout en bout : la cloche DX7 passe de 0,114 à 0,075,
marge doublée sur le second. Tableau complet et lecture détaillée dans
`vsm_patch_optimizer.py`, sous `FINALIST_MAX_DIMENSIONS`.

**Ce que la mesure a rapporté d'autre — et qui vaut plus que les tableaux.**
Trois défauts de la chaîne, invisibles sans elle : l'étape finale de
`reconstruire.py` cherchait `vsm-render` dans le PATH et échouait après des
minutes de calcul (corrigé : résolution par `find_vsm_render`) ; la séparation
demucs gardait son défaut `shifts=1`, un décalage aléatoire NON SEEDÉ qui
rendait deux exécutions incomparables — 101 notes transcrites contre 156 sur
le même fichier (corrigé : `shifts=0`, documenté) ; et l'adaptateur CLAP
exposait une liste de machines écrite en dur, en retard de huit machines sur
le registre (corrigé, avec un test ensembliste). Une promesse qu'on mesure
rend la chaîne honnête bien au-delà du chiffre qu'on cherchait.

---

## 32. `vsm.string` — la corde, et la dernière case de couverture

**Le problème qu'elle résout.** C'était la dernière limite encore écrite au
§ 1 de [`docs/ROADMAP-fusion.md`](docs/ROADMAP-fusion.md) : « basse, guitare et
cordes réelles passent toujours par le sampler faute de modèle dédié ». Le § 1
de [`docs/CDC-machines-manquantes.md`](docs/CDC-machines-manquantes.md) marque
trois sources NON COUVERTES — basse électrique, guitare, cordes — et c'est la
seule case restée vide après les six machines du § 9. Ce n'est donc pas une
machine de caractère de plus, contre lesquelles le § 7 du même document met en
garde : elle ouvre une **famille de synthèse absente du parc**, la modélisation
physique par guide d'ondes, qui est la seule à pouvoir répondre honnêtement à
un stem de corde jouée. Quinze paramètres, huit voix.

**Le modèle.** Une seule ligne à retard porte l'aller-retour de l'onde. La
boucle contient quatre organes, chacun répondant à un geste que le musicien
connaît :

```
excitation ──> [ point de contact : 1 - z^-pD ]
                     │
                     v
     ┌──> ligne à retard (N) ──> retard fractionnaire ──┐
     │                                                  │
     └── gain de boucle <── dispersion <── amortissement ┘
                                                        │
                                     corps (résonances) <┘
```

1. **Amortissement** — passe-bas d'ordre un `(1-b)x[n] + b·x[n-1]`. Son gain
   vaut *exactement* 1 en continu, ce qui est la condition pour que
   « String Decay » soit vraiment le T60 du fondamental et non une
   approximation.
2. **Raideur** — trois passe-tout d'ordre un. Une corde raide transmet les
   aigus plus vite que les graves : ses partiels montent.
3. **Retard fractionnaire** — un passe-tout accordé pour que la boucle fasse
   exactement SR/f0. Sans lui la hauteur se quantifierait à l'échantillon
   près : à 4 kHz, un échantillon vaut plus d'un demi-ton. Mesuré : erreur
   inférieure à 0,2 cent sur cinq octaves, et un test la vérifie de la note 28
   à la note 76.
4. **Gain de boucle** — la décroissance, calculée depuis le T60 demandé.

Deux excitations partagent cette boucle, et le paramètre `Excitation` fond de
l'une à l'autre **sans palier** : le pincement (salve de bruit injectée en un
point) et l'archet (force de frottement continue, table de friction en
stick-slip). La continuité est délibérée — cette machine est faite pour être
CHERCHÉE, et un sélecteur discret creuse une falaise dans la fonction de coût
(§ 3 de `CDC-machines-manquantes.md`).

### Trois corrections que seule la mesure pouvait imposer

Aucune n'était visible en lisant le code ; chacune vient d'un chiffre.

**1. Le coefficient de dispersion doit dépendre de la note.** À coefficient
fixe, l'inharmonicité suit la fréquence ABSOLUE et non le rang du partiel : à
−0,55, le 16e partiel d'un la 440 monte de 39 cents, celui d'un mi grave à
82 Hz de **0,5 cent** — c'est-à-dire rien, précisément sur les cordes graves où
la raideur s'entend le plus. Pire pour la recherche : en rendant le coefficient
proportionnel au réglage, les trois premiers quarts de la course du bouton ne
produisaient **aucun effet audible** sur une basse (mesuré : +0,0, +0,0, +0,4
cent pour les réglages 0,25, 0,50 et 0,75) — la falaise même que la machine
devait éviter. Le coefficient est donc résolu pour que l'inharmonicité visée au
16e partiel soit LINÉAIRE dans le réglage, de 0 à 25 cents. La loi
`|a| = exp(−k·ω16)` avec `k = 3,26·cents^−0,368` vient de l'inversion numérique
de la réponse du peigne ; le facteur k s'est révélé indépendant de la note à
1 % près sur cinq octaves, ce qui est ce qui rend la formule utilisable.

**2. Le pincement doit avoir la pente d'un triangle, pas celle d'un bruit.**
Le point d'injection produit à lui seul le peigne `1 − z^−pD`, c'est-à-dire le
facteur `sin(nπp)` de la corde idéale. Il manquait l'autre moitié : le
déplacement initial d'une corde pincée est un TRIANGLE, dont le contenu
harmonique décroît en 1/n², soit −12 dB par octave. Sans cette pente, le second
harmonique sortait plus fort que le fondamental — profil harmonique mesuré
0,77 / 1,00 / 0,75 là où un violoncelle réel donne 1,00 / 0,29 / 0,19. Aucun
instrument à cordes ne fait cela. La salve passe désormais par **deux**
passe-bas d'ordre un, dont la coupure est ce que règle « Pick Hardness ».

**3. La salve dure une PÉRIODE, pas 5 ms.** Une salve courte à durée fixe
paraissait plus « médiator ». Elle est en réalité incapable d'exciter une corde
grave : une fenêtre de 5 ms n'a presque pas d'énergie sous 200 Hz. Sur un
violoncelle à 73 Hz, le fondamental de la machine ne sortait même pas dans les
huit plus fortes raies de son spectre. Le pincement met en mouvement TOUTE la
longueur de la corde : la salve dure un aller-retour. Après correction, le
profil harmonique passe à 1,00 / 0,57 / 0,07 sur un réglage doux — enfin celui
d'une corde.

### La promesse, mesurée : avant / après, chiffres publiés

**Protocole.** Cible **réelle** : `cello.wav`, un violoncelle à l'archet
(ré 2, 73,4 Hz) — montée en 200 ms, archet levé vers 0,24 s, extinction libre
jusqu'à 1 s ; le centroïde part à 3,7 kHz (le crin qui mord) et retombe à
500 Hz. C'est une corde frottée d'instrument acoustique, l'une des trois
sources que le § 1 marque « non couvert ». Recherche sur les **17 machines
mélodiques**, présélection DÉSACTIVÉE (chaque machine reçoit le même budget
complet, sans quoi comparer leurs distances ne voudrait rien dire), 10 axes,
métrique v2, `gate` réglé sur celui de la cible. « Avant » = la meilleure
machine hors `vsm.string` ; « après » = avec elle dans les candidates. Les deux
colonnes sortent de la MÊME série de recherches, ce qui rend la comparaison
exacte. Trois graines, deux budgets — parce qu'un seul chiffre ne prouve rien.

**1. Au budget par défaut de la chaîne (20 itérations), la machine gagne.**
Classement complet sur 17 machines, graine 1234 :

| rang | machine | distance |
|---|---|---|
| **1** | **`vsm.string`** | **0,1225** |
| 2 | jupiter8 | 0,1310 |
| 3 | sh101 | 0,1373 |
| 4 | prophet | 0,1454 |
| 5 | wavetable | 0,1475 |
| 6 | generic | 0,1476 |

puis pcmhybrid 0,1491 · obx 0,1514 · minimoog 0,1559 · juno106 0,1598 ·
arpodyssey 0,1644 · ms20 0,1890 · supersaw 0,1979 · dx7 0,2007 · tb303 0,2398 ·
epiano 0,2448 · tonewheel 0,4279.

**2. À budget triplé, elle perd — et c'est le résultat le plus instructif des
deux.** Six recherches, trois graines, deux budgets :

| budget | graine | `vsm.string` | meilleur sans elle | verdict |
|---|---|---|---|---|
| 20 itér. | 1234 | **0,1225** | jupiter8 0,1310 | **gagne** |
| 20 itér. | 7 | **0,1287** | jupiter8 0,1375 | **gagne** |
| 20 itér. | 99 | **0,1274** | prophet 0,1343 | **gagne** |
| 60 itér. | 1234 | 0,1190 | sh101 **0,0865** | perd |
| 60 itér. | 7 | 0,1151 | sh101 **0,0968** | perd |
| 60 itér. | 99 | 0,1124 | jupiter8 **0,1071** | perd |

**Ce que le budget révèle : la machine sature, les soustractifs non.** En
passant de 20 à 60 itérations, la médiane de `vsm.string` gagne **−10 %**
(0,1274 → 0,1151) quand celle du SH-101 gagne **−36 %** (0,1496 → 0,0968) et
celle du Jupiter-8 **−27 %**. Ce n'est pas un défaut de convergence : c'est un
PLAFOND PHYSIQUE. Les dix axes de la corde sont peu nombreux et fortement
contraints — une corde ne peut pas produire n'importe quel spectre, c'est
précisément ce qui en fait un modèle — donc la recherche les épuise vite et
bute sur ce que la physique autorise. Un soustractif, lui, peut plier son
enveloppe spectrale indéfiniment vers la cible pour peu qu'on lui paie les
évaluations, sans jamais se soucier de ce qu'une corde ferait.

C'est le symétrique exact de ce que le § 31 avait mesuré sur `vsm.generic` —
« la seule machine qui PROFITE d'un espace élargi ». Ici c'est la machine qui
en profite le MOINS, et pour la même raison retournée : la neutralité s'achète
avec du budget, la fidélité physique se paie d'avance.

**Trois conséquences, à ne pas confondre.**

- **La couverture est acquise, la victoire est conditionnelle.** Au budget que
  `reconstruire.py` emploie réellement, un stem de corde retient enfin une
  machine qui le modélise, et le chiffre baisse. Au budget élevé du 6e passage
  de House Of God, un soustractif bien réglé passe devant : dire l'inverse
  serait choisir la mesure qui arrange.
- **Elle est la plus STABLE du lot**, et cela ne se voit que sur trois graines :
  sur les six recherches, `vsm.string` varie de 0,1124 à 0,1287 (±7 %) quand le
  SH-101 va de 0,0865 à 0,1778 (×2,05). Une machine dont le verdict ne dépend
  pas de la graine vaut mieux, pour un classement de machines, qu'une machine
  qui gagne parfois très bien et parfois pas du tout.
- **Le modèle a identifié la bonne physique, seul et six fois sur six.** Sur
  une cible à l'archet, les six recherches indépendantes ont toutes retenu
  `string.excitation` entre **0,956 et 0,999** — c'est-à-dire l'archet, jamais
  le pincement — depuis six populations initiales différentes. Aucune distance
  ne dit à l'optimiseur ce qu'est un archet ; il l'a trouvé parce que le modèle
  en contient un. C'est le seul résultat de cette page qu'un ajustement de
  spectre ne pouvait pas produire par hasard.

**Portée de la mesure, et ce qu'elle n'établit pas.** Une cible, un instrument,
deux budgets, trois graines. Elle établit que la case de couverture est
réellement comblée et que le modèle est physiquement pertinent ; elle
n'établit pas un classement général, et surtout pas que la modélisation
physique batte la synthèse soustractive sur les cordes en général. La chaîne
complète (`reconstruire.py` de bout en bout sur un morceau, séparation et
transcription comprises) n'a PAS pu être rejouée ici, faute de la pile
d'analyse lourde (demucs, basic-pitch) sur cette machine : c'est la moitié
manquante de la preuve, et elle est écrite comme telle plutôt que passée sous
silence.

### Le défaut de méthode : un `gate` qui mentait, et ce qu'il a failli faire conclure

Cette mesure a d'abord donné le résultat INVERSE. Les chiffres, avec leurs
conditions — puisque c'est tout le sujet de cette section :

| conditions | `vsm.string` | meilleur du parc | rang |
|---|---|---|---|
| 4 itér., `gate` 0,95, DSP avant corrections 2 et 3 | 0,2456 | minimoog 0,1266 | 14/17 |
| 4 itér., `gate` 0,95, DSP corrigé | 0,2199 | minimoog 0,1266 | 12/17 |
| 20 itér., `gate` 0,95, DSP corrigé | 0,1912 | wavetable 0,0836 | — |
| **20 itér., `gate` 0,24, DSP corrigé** | **0,1225** | jupiter8 0,1310 | **1/17** |

Le soupçon portait naturellement sur la machine — un modèle physique qui perd
contre un Jupiter-8 sur un violoncelle est un modèle suspect. Deux des trois
corrections DSP ci-dessus sont sorties de cette enquête et étaient bien réelles
(elles valent 0,2456 → 0,2199 à conditions égales). Mais le gros de l'écart
venait d'ailleurs :

`gate` est la proportion de l'extrait pendant laquelle la note est TENUE, et il
doit valoir celui de la cible. Il était réglé à 0,95 alors que le violoncelle
se tait à 0,24 s sur 1,0 s. Chaque candidate jouait donc pendant 0,95 s face à
une cible éteinte depuis 0,24 s — ce qui pénalise spécifiquement tout modèle
**entretenu** (l'archet, l'orgue), incapable de s'arrêter tant que la touche est
tenue, là où un soustractif referme simplement son enveloppe. Le classement
n'était pas celui des machines, c'était celui de leur capacité à survivre à une
erreur de protocole. Corrigé à 0,24, la même machine, le même budget et la même
graine donnent 0,1225 et la première place — soit un facteur 1,6 sur la
distance, obtenu sans toucher une ligne de DSP.

C'est la même leçon qu'au § 10.3 et à la septième passe de House Of God, sous
une troisième forme : **une distance n'est un chiffre que si l'on sait à quelles
conditions elle a été obtenue.** La métrique était inscrite dans le rapport, le
budget aussi depuis la septième passe ; le `gate`, lui, ne l'était nulle part —
et il vaut ici plus que les trois corrections de DSP réunies.

### Ce qui est assumé, et écrit plutôt que découvert plus tard

- **Une seule ligne à retard**, là où la physique en demande deux (onde montante
  et onde descendante). L'archet agit donc sur l'onde résultante et non sur une
  jonction entre deux ondes : le cycle d'adhérence-décrochement est conservé,
  sa forme d'onde exacte non.
- **Corps en résonances série** et non en modes couplés : trois cloches de
  Biquad qui colorent, pas une caisse qui rayonne. À `Body Level = 0` la machine
  est exactement transparente — ce dont une basse électrique a besoin.
- **Raideur rognée dans l'aigu** : le retard qu'exigent les passe-tout de
  dispersion ne peut pas dépasser 40 % de la boucle, faute de quoi une note très
  aiguë n'aurait plus de ligne à retard. Au-dessus d'environ 1 kHz la raideur
  demandée est donc réduite ; la note reste juste, elle est seulement moins
  inharmonique que réglée.
- **Chaque note repart d'une corde au repos** : on ne modélise pas le
  repincement d'une corde qui vibre encore.
- **L'archet n'a pas de temps de montée réglable.** Sur la cible mesurée, dont
  l'attaque dure 200 ms, c'est un handicap face aux machines qui ont un
  `envelope.1.attack` dans leur espace de recherche — et c'est un candidat
  sérieux pour expliquer le plafond constaté à budget élevé. Un seizième
  paramètre coûterait une dimension à la recherche ; le compromis est celui-là,
  il est chiffré, et il devra se rouvrir si une seconde cible confirme le
  plafond.
- **Aucune mesure n'a été faite sur un instrument réel** : le statut honnête est
  « dérivé », jamais « mesuré », comme pour toutes les machines du parc.

**Sa façade ne copie aucune machine d'origine**, parce que la modélisation
physique n'en a jamais eu de canonique : prétendre en imiter une serait inventer
un souvenir. Le § 5 de `CDC-nouvelle-machine.md` prévoit ce cas — à défaut
d'original, la disposition suit le trajet du signal. Ici c'est le trajet
PHYSIQUE, qui est aussi celui que le musicien parcourt en pensée :
EXCITATION → ARCHET → CORDE → CAISSE → SORTIE.

**Un test manquait au parc, et cette machine l'a révélé.** `panels/` vérifiait
qu'aucune façade ne vise une machine inexistante, mais pas l'inverse : une
machine SANS façade passait en silence, et l'instrument aurait été jouable sans
la moindre commande. `every_registered_machine_has_a_panel` comble le trou
(vérifié en le retirant : le test échoue bien).

---

## 33. La version finale du parc : trois machines, et le sampler rendu à la voix

**La décision qui commande tout le reste.** Le sampler servait de repli
universel : batterie découpée, et tout ce qu'aucune machine ne savait faire. Il
est désormais **réservé à la voix**. Ce n'est pas un détail d'organisation,
c'est ce qui a ouvert trois trous d'un coup — la batterie acoustique perdait sa
seule réponse, le piano acoustique aussi (il était déclaré « hors de portée
sans bibliothèque d'échantillons »), et les cuivres et bois n'en avaient jamais
eu. Trois machines les comblent.

| Machine | Famille ouverte | Ce qu'elle rend jouable |
|---|---|---|
| `vsm.piano` | cordes FRAPPÉES | le stem `piano_or_keys` acoustique |
| `vsm.drums` | membranes et métal | le stem `drums` sans échantillon |
| `vsm.wind` | anche et lèvres | clarinette et cuivres |

### La brique s'est ouverte avant la deuxième machine

`vsm.piano` a besoin EXACTEMENT de la boucle de `vsm.string` : une ligne à
retard, un amortissement, une dispersion, un retard fractionnaire, un gain. La
recopier aurait donné deux exemplaires d'une même physique, qui divergent
toujours à la longue — le § 8.4 du cahier des charges met en garde contre
précisément cela. La boucle est donc sortie dans
`dsp/StringWaveguide.h`, et les deux machines la partagent.

**La preuve que l'extraction était fidèle n'est pas une relecture, c'est
l'empreinte** : `vsm.string` a gardé la sienne au bit près à travers le
refactoring. C'est exactement le service que ces empreintes rendent, et c'est
la deuxième fois qu'il paie.

`vsm.wind`, lui, ne la partage PAS, et le dire est aussi important : sa
réflexion est INVERSANTE (une corde ne l'est jamais), il n'a pas de dispersion
(un tuyau d'air n'est pas raide), et sa perte n'est pas un amortissement
interne mais un rayonnement au pavillon. Partager aurait demandé d'ajouter à la
corde trois options dont elle n'a que faire.

### `vsm.piano` — le marteau porte la loi expressive

Ce qui sépare un piano d'une corde pincée tient en trois points, et aucun n'est
un réglage de timbre :

1. **La durée de contact du marteau décroît quand on frappe fort.** Or elle
   fixe la coupure du spectre injecté : frapper fort OUVRE LE TIMBRE, pas
   seulement le volume. Cette loi sort de la physique du modèle ; aucune
   enveloppe de filtre n'a à la simuler. C'est le test
   `piano_striking_harder_opens_the_timbre_not_only_the_level` qui la verrouille.
2. **Deux cordes par note, légèrement désaccordées.** Ce n'est pas un chorus :
   c'est ce qui donne la **décroissance en deux temps** — chute rapide, puis
   longue traîne faible. Une corde seule donne une exponentielle unique, qui
   s'entend aussitôt comme « pas un piano ». La traîne est obtenue en donnant à
   la seconde corde un T60 plus long (×1,35), ce qui est l'effet audible du
   couplage au chevalet sans le mécanisme.
3. **Le marteau frappe au huitième de la corde**, ce qui pose un noeud sur le
   8e harmonique et le supprime — la raison pour laquelle un piano ne sonne pas
   dur. Le peigne du guide d'ondes le produit seul dès qu'on lui donne la
   position.

**Ses identités sémantiques sont presque toutes celles de `vsm.string`**, et
c'est voulu : le point de contact et la dureté de l'excitation sont la même
notion qu'on frappe ou qu'on pince, la raideur et la décroissance sont celles
de la corde, la table d'harmonie est le corps. Un seul identifiant lui est
propre — `piano.sustainPedal` — parce que rien d'autre au parc n'a d'étouffoir.
Il est **exclu de la recherche** : c'est une commande de jeu, binaire, et la
chercher reviendrait à demander à l'optimiseur de choisir entre deux falaises.

### `vsm.drums` — une peau n'est pas une sinusoïde

Le parc avait déjà deux boîtes à rythmes ; elles synthétisent des percussions
ÉLECTRONIQUES, et c'est ce qu'on leur demande. Une batterie acoustique est
autre chose, et la différence tient en une phrase : **les modes d'une membrane
sont inharmoniques**. Une corde vibre sur des multiples entiers ; une peau
circulaire vibre sur les zéros de Bessel — 1,000 / 1,593 / 2,135 / 2,295 — et
ce sont ces rapports irrationnels qu'on entend comme « peau » plutôt que comme
« note ». Un banc modal les produit directement.

Trois conséquences que le modèle donne gratuitement : les modes hauts meurent
avant le fondamental (un coup est riche trente millisecondes puis devient un
bourdonnement) ; frapper fort réveille les modes hauts, donc la vélocité change
le TIMBRE ; et le métal, lui, reçoit des rapports volontairement sans commune
mesure, pour qu'aucune hauteur ne s'installe.

**La pièce fait partie de l'instrument.** C'est ce qui trahit le plus vite un
kit modélisé : sans ambiance il sonne « électronique », quelle que soit la
qualité des peaux. Deux peignes et un passe-tout suffisent à poser un lieu — ce
n'est pas une réverbération et ne prétend pas l'être ; la vraie réverbération
du projet reste un effet d'insert. À `Room Level = 0` la machine est
exactement sèche et mono, et un test le vérifie à 1e-9.

**Un défaut trouvé par la mesure, pas par relecture** : le groupe de coupure
n'étouffait que le BRUIT de la charleston ouverte, pas son cluster métallique,
qui continuait de sonner par-dessus la fermée. Mesuré : 43 % d'énergie
restante là où on en attendait 5 %. Deux positions d'un même instrument ne
peuvent pas sonner ensemble ; le banc modal a reçu son `choke()`.

### `vsm.wind` — ce qui marche, et ce qui ne marche pas

Une valve non linéaire — anche de bois ou lèvres de cuivre — entretient un
tuyau. Ce n'est pas une enveloppe qui module un oscillateur : le souffle ouvre
la valve, l'onde part, revient, et c'est la pression de retour qui la referme.
L'oscillation naît de ce dialogue, et l'attaque, l'accroche, le petit retard
d'établissement sont dans la physique.

**Ce qu'elle couvre : les perces cylindriques — la clarinette, et en première
approximation les cuivres.** Un tuyau cylindrique fermé côté valve ne résonne
que sur les harmoniques impaires ; c'est le creux caractéristique de la
clarinette et son saut à la douzième.

**Ce qu'elle ne couvre pas : les perces coniques (saxophone, hautbois) et les
flûtes.** Ce n'est pas faute d'avoir essayé, et le tableau qui suit est le
résultat de l'expérience — quatre topologies de boucle, la même anche :

| topologie | résultat |
|---|---|
| cylindre, réflexion inversante, D/2 | **oscille**, harmoniques impaires |
| non inversante, D | ne s'amorce pas |
| non inversante, D, + dérivateur | diverge |
| inversante, D/2, + dérivateur | diverge |

La raison est structurelle et mérite d'être écrite pour qu'on n'y revienne pas :
une boucle à réflexion inversante et demi-longueur impose `x(t + T/2) = −x(t)`.
Cette **symétrie demi-onde interdit mathématiquement les harmoniques paires**,
quel que soit le filtre placé dans la boucle. Ce qui la casserait est la
réflexion à l'apex d'un cône, qui n'est pas un changement de signe mais un
filtre du premier ordre ; les deux topologies essayées dans cette direction
divergent, faute d'un gain de boucle borné.

> **REPRIS DEPUIS, ET LE VERDICT « HORS DE PORTÉE » NE TIENT PLUS.** Le tableau
> ci-dessus reste exact, mais sa conclusion était trop large. Trois mesures
> l'ont corrigée, et elles sont conservées ici parce qu'elles coûtent cher à
> refaire (le prototype vit dans `audio/plugins/cone/`, hors build) :
>
> 1. **Un banc modal ne peut pas s'amorcer, et c'est structurel.** Des
>    résonateurs à `n·f0` portent bien la série complète et sont bornés par
>    construction — les deux qualités qui manquaient. Mais la phase d'un
>    deux-pôles va de 0° au continu à −90° à sa résonance : la condition de
>    Barkhausen n'est satisfaite QU'AU CONTINU, où le gain vaut 0,43. Mesuré :
>    salve à 4,19 puis décroissance jusqu'à 0,0013. **Un instrument à anche
>    oscille parce que sa perce est un RETARD** ; sans retard, pas
>    d'auto-oscillation, quelle que soit la finesse du banc.
> 2. **Le « ne s'amorce pas » de la topologie non inversante était un défaut de
>    gain, pas de topologie.** Une première version injectait un DÉBIT
>    (`flow = anche(Δp)·souffle`), dont la contribution au gain de boucle ne
>    vaut que 0,22. La formulation par DIFFUSION de `vsm.wind`
>    (`p = souffle + (retour − souffle)·anche`) en apporte ~0,7, tendant vers 1
>    à la butée de la valve.
> 3. **Le filtre d'apex doit être un passe-haut À UN PÔLE, pas un dérivateur.**
>    C'est là qu'était la divergence : un dérivateur a un gain qui croît sans
>    limite, un passe-haut a le même zéro au continu et plafonne à 1. Sans ce
>    zéro, la boucle non inversante a un gain positif au continu et s'y
>    installe — la valve retombe dans sa zone linéaire et la note s'éteint
>    (rms 0,059 → 0,00003 en trois fenêtres). Avec lui, **la boucle tient** :
>    rms 0,160 stable sur douze fenêtres, sans dérive continue, et la série
>    harmonique est COMPLÈTE (h1 0,55 · h2 1,00 · h3 0,48 · h4 0,36) — les
>    rangs pairs que le cylindre ne peut pas produire.
>
> 4. **La régulation est trouvée, le TIMBRE ne vient pas, et c'est structurel.**
>    Un gain de régénération au-dessus du seuil avec la saturation de l'anche
>    pour borner l'amplitude règle entièrement le premier défaut : **135
>    configurations sur 135 s'entretiennent**, aucune muette, aucune emballée,
>    niveau efficace tenu entre 0,167 et 0,210, hauteur juste sur 42 des 45
>    réglages mesurés. Mais le spectre est presque vide — h2 entre 0,00 et 0,13
>    dans tout le grave et le médium. Une sinusoïde, là où l'on voulait un
>    saxophone.
>
>    La cause n'est pas un réglage à trouver, c'est une TENSION entre deux
>    exigences : la hauteur juste exige un filtrage de boucle raide, parce qu'un
>    cône résonne à CHAQUE `n·f0` — les rangs sont deux fois plus serrés que
>    dans un cylindre, et tout gain assez fort pour faire battre l'anche hisse
>    aussi le rang 2 au-dessus du seuil (mesuré sur douze combinaisons de gain
>    et de coupure, avec un pôle comme avec deux : de 5 à 25 réglages sur 45
>    jouaient l'octave ou la douzième). Le timbre, lui, exige l'inverse. Le
>    filtrage raide vide l'onde de ses harmoniques et l'anche retombe dans sa
>    zone linéaire.
>
>    **`vsm.wind` échappe à ce piège par sa géométrie** : ses résonances
>    impaires (f0, 3·f0, 5·f0) sont deux fois plus espacées, un filtre doux
>    suffit à choisir le fondamental, et l'anche reste libre de claquer. La
>    symétrie demi-onde qui lui interdit les rangs pairs est aussi ce qui lui
>    donne son timbre : les deux tiennent au même trait.
>
> **Ce qui reste, et pourquoi la machine n'est pas livrée.** Elle sonne juste,
> tient son niveau et ne s'emballe pas — mais elle sort une sinusoïde, et une
> machine qui sort une sinusoïde n'apporte rien au parc : n'importe quel
> soustractif en fait une meilleure, et les rangs pairs qui justifiaient son
> existence ne sont pas au rendez-vous. Le § 10 de `CDC-nouvelle-machine.md` est
> clair : « une case non cochée n'est pas un détail à finir plus tard ». Elle
> n'est donc ni enregistrée ni compilée (les 527 tests restent verts), et la
> case « saxophone, hautbois, flûte » du tableau de couverture reste vide.
>
> **Où reprendre.** Le retard est trouvé, la régulation est trouvée ; ce qui
> manque est la SÉLECTIVITÉ. Il faudrait un résonateur qui choisisse `f0` sans
> raboter ses harmoniques — un peigne accordé plutôt qu'un passe-bas dans la
> boucle. C'est un travail de conception, pas de réglage, et c'est là qu'il
> faudra le reprendre.

**Un réglage a été RETIRÉ à cause de cette mesure.** `Bore Shape` prétendait
fondre du cylindre au cône par un évasement (passe-tout à coefficient positif,
qui allonge le chemin des aigus). Mesuré sur toute sa course : la deuxième
harmonique reste à 0,0001 contre 0,041 pour la troisième. **Le réglage ne
faisait rien.** Un réglage qui ne fait rien est pire qu'un réglage absent : il
ment à qui le tourne, et il coûte une dimension à la recherche. Il a été
supprimé, et la machine est passée de seize à quinze paramètres.

### La chaîne suit : la batterie sort du sampler, la voix y entre

`reconstruire.py` routait `drums` vers le sampler et cherchait un patch de
synthé sur `vocals`. Les deux sont inversés.

**La voix ne se reconstruit pas, elle se REPORTE.** Le § 6 de la feuille de
route le dit depuis le début — « hors de portée d'une synthèse par machine ; la
séparation la rend déjà disponible en audio, c'est le mieux qu'on puisse en
faire honnêtement ». La chaîne, elle, cherchait quand même un patch dessus et
publiait un chiffre : sur Children, `vsm.obx` à 0,196. Ce chiffre était pire
qu'inutile, il était trompeur — ce n'est pas parce qu'un OB-X approche le
spectre moyen d'une voix qu'il chante. Le stem vocal devient donc **un
échantillon, joué tel quel**, et le journal le dit dans ces termes : « la voix
n'est pas reconstruite, elle est reportée ».

**La batterie passe à `vsm.drums`.** La détection de frappes et le classement
par familles ne changent pas d'une ligne : c'est le même travail, mais son
résultat pilote des NOTES au lieu de charger des échantillons. On y gagne un
kit réglable — accorder la caisse claire, ouvrir la pièce, allonger la
charleston — là où un coup découpé était figé ; on y perd la fidélité littérale
au coup enregistré. L'ancien comportement reste accessible par
`--batterie-echantillonnee`, parce qu'il est plus fidèle et moins réglable, et
que le choix dépend de ce qu'on veut faire du projet.

### Ce que la première exécution complète a trouvé, et qui n'était pas prévu

Trois défauts, dont deux qu'aucun test unitaire ne pouvait voir.

**1. Le `gate` pouvait dépasser 1 — révélé par le champ ajouté la veille.** La
leçon du § 32 (« une distance n'est un chiffre que si l'on sait à quelles
conditions elle a été obtenue ») avait fait inscrire le `gate` dans
`rapport.json`. Première exécution, premier stem : `gate = 1,9978`. La cause
est simple et le champ l'a rendue visible en une seconde — l'extrait est
plafonné à 1,5 s, la durée de la note choisie ne l'est pas, donc une note de
trois secondes donnait « tenue pendant 200 % de l'extrait ». Borné à 1. C'est
la meilleure justification possible pour ce champ : il a servi le lendemain.

**2. La batterie modélisée était trois fois trop faible.** Le calage
automatique des volumes demandait un facteur 4,3 pour la rattraper et BUTAIT
sur sa borne de 2,5 : la piste restait deux fois trop faible dans le mélange.
La cause était un critère de calibration trop sévère que je m'étais donné —
« les neuf pièces frappées ensemble à vélocité 127 restent sous 1,0 ». Mesuré
sur un motif ordinaire, et comparé au parc :

| | pic sur un motif | rms | pic, neuf pièces ensemble |
|---|---|---|---|
| `vsm.drums` (gain 0,19) | 0,376 | 0,079 | 0,916 |
| `vsm.tr909` | 0,894 | 0,234 | **1,761** |
| `vsm.tr808` | 0,746 | 0,271 | **1,451** |

Les deux boîtes du parc dépassent elles aussi 1,0 sur ce cas artificiel, qui
n'arrive dans aucun motif. Le garantir coûtait un facteur trois de niveau
utile. Le gain est passé à 0,42 — motif à 0,832 de crête, niveau efficace dans
la fourchette du parc — et **le test a changé de critère en même temps que le
code** : il verrouille désormais un motif réel, avec un plancher autant qu'un
plafond, parce qu'une batterie trop faible est un défaut aussi réel qu'une
batterie qui écrête.

**3. `vsm.piano` n'a pas survécu à la présélection.** Avec dix-neuf candidates,
le dégrossissage n'en garde que dix, sur 0,4 s d'extrait et un budget réduit —
et le piano n'était pas dedans. Le § 31 avait déjà chiffré ce risque
(« ajouter une candidate ne vole jamais l'identification, mais peut coûter une
place de finaliste ») ; ici la victime est la candidate elle-même. C'est
pourquoi `reconstruire.py` reçoit `--finalistes`, qui expose le réglage jusque
sur la ligne de commande : **`--finalistes 0` est le seul régime sous lequel
les distances de toutes les machines se comparent**, et c'est celui qu'il faut
employer pour mesurer, jamais celui du travail courant.

### L'épreuve : *Children* (Robert Miles, 1996), avant et après

Un morceau du commerce, quatre stems, la chaîne complète — séparation,
transcription, recherche, rendu, mesure. Le morceau a été choisi pour ce qu'il
met en jeu : **un piano au premier plan**, une batterie de dance, une basse et
des nappes. C'est-à-dire, précisément, ce que les trois machines visent.

**Ce que la chaîne retient, avant et après :**

| stem | avant | après |
|---|---|---|
| basse | `vsm.generic` d=0,175 | `vsm.generic` d=0,175 — **identique** |
| nappes/piano | `vsm.string` d=0,203 | `vsm.string` d=0,203 — **identique** |
| batterie | sampler, 5 pièces, 1092 frappes | **`vsm.drums`**, mêmes 5 pièces, mêmes 1092 frappes |
| voix | `vsm.obx` d=0,196 | **report intégral** par le sampler |

Que la basse et les nappes soient identiques au millième n'est pas un détail :
c'est la preuve qu'ajouter trois machines au parc n'a rien déplacé de ce qui
marchait, et que la chaîne est restée déterministe.

**La distance globale, et ce qu'elle coûte :**

| projet | distance (v2) |
|---|---|
| AVANT — batterie échantillonnée, voix cherchée | **0,1982** |
| HYBRIDE — tout d'après, batterie d'avant | 0,2033 |
| APRÈS — batterie modélisée, voix reportée | **0,2169** |

L'hybride n'existe que pour attribuer : il ne diffère d'APRÈS que par la piste
de batterie, et d'AVANT que par la voix. L'écart total (+0,0187, soit +9,4 %)
se répartit donc sans ambiguïté :

- **la batterie modélisée coûte +0,0136**, les trois quarts ;
- **le report de la voix coûte +0,0051**, le quart restant.

Confirmé sur la piste seule, contre son propre stem et à niveau efficace égalisé :

| batterie | distance au stem `drums` |
|---|---|
| échantillonnée (les coups du disque) | **0,1722** |
| modélisée (`vsm.drums`) | 0,2029 |
| silence | 0,9360 |

**Ce résultat était inévitable, et il faut le dire dans ce sens-là.** Rejouer
les coups découpés DANS l'enregistrement ne peut pas être battu par un modèle :
c'est la copie contre la ressemblance. Demander « le sampler seulement pour la
voix » revient donc à échanger, sciemment, un peu de distance contre une
batterie entièrement MODÉLISÉE — accordable, réglable, indépendante du fichier
source, et qui n'a plus besoin qu'on découpe le disque pour sonner. Le prix est
de 18 % sur la piste, 6,7 % sur le morceau. Il est écrit ici plutôt que caché,
et `--batterie-echantillonnee` le rend réversible en une option.

Quant à la voix, son report est bien plus FIDÈLE que le patch qu'on cherchait
avant (0,065 contre 0,196, mesuré sur la piste seule) ; s'il coûte tout de même
0,005 sur le mélange, c'est que la voix de ce morceau est très en retrait
(niveau efficace 0,0033 contre 0,156 pour la batterie) et qu'aucune des deux
versions ne pèse lourd dans la mesure globale. Le gain est réel, il est
simplement invisible à cette échelle.

### Un chiffre qui mentait sur ses conditions, trouvé en chemin

En mesurant l'attribution ci-dessus, l'écart AVANT/APRÈS ne concordait pas :
la chaîne annonçait 5,01 puis 8,09, quand la mesure directe donnait 0,198 puis
0,217. Les deux sont justes — ils ne sont pas dans la même métrique.

`reconstruction_distance` appelait `audio_distance`, c'est-à-dire la **v1**,
quoi qu'on ait demandé, pendant que le résumé imprimait « métrique v2 » et que
`rapport.json` l'inscrivait. **Toutes les distances GLOBALES publiées jusqu'ici
sont des v1 mal étiquetées** ; les distances par stem, elles, ont toujours été
celles qu'elles annonçaient. Le § 10.3 avait pourtant établi que les deux ne
sont pas du même ordre et ne se comparent pas.

C'est la troisième fois que ce projet se fait prendre par la même chose — la
métrique au § 10.3, le budget à la septième passe de House Of God, le `gate` au
§ 32 — mais c'est la première fois que le défaut est dans le code qui PUBLIE le
chiffre, et non dans la façon de le lire. Corrigé : le paramètre est transmis.
Conséquence à retenir pour la lecture de l'historique : les distances globales
des sections antérieures (0,670 sur la vérité terrain, 2,974 sur House Of God
v7, 5,01 ici) sont des v1, et elles ne se comparent qu'entre elles.

### `--sans-sampler` : interdire l'échantillon sur tout un morceau

La règle de la version finale — « le sampler n'est QUE pour la voix » — reste
le défaut, et elle est bonne : la voix ne se synthétise pas. Mais elle rend le
projet **dépendant du disque d'origine**, puisqu'une de ses pistes contient un
morceau de l'enregistrement. Qui veut un projet entièrement rejouable, réglable
et détachable de sa source doit pouvoir dire « que des synthés », et le payer en
connaissance de cause. C'est ce que fait `--sans-sampler` :

- le stem `vocals` repasse par la recherche de patch, comme n'importe quel
  autre stem ;
- `build_drum_kit(..., write_samples=False)` fait la même détection, le même
  classement, les mêmes instants et les mêmes vélocités, mais **n'écrit pas les
  WAV découpés** : un projet qui ne charge aucun échantillon ne doit pas
  traîner un dossier `samples/` qui laisse croire le contraire ;
- `--sans-sampler` avec `--batterie-echantillonnee` est refusé avec sa raison,
  plutôt que silencieusement arbitré : la batterie échantillonnée EST le
  sampler.

**Ce que l'interdiction coûte, sur *Children (Dream Version)*.** Le morceau
entier, quatre stems, tempo mesuré à 138 BPM, métrique v2, budget 20 :

| stem | machine retenue | distance | podium |
|---|---|---|---|
| bass | `vsm.generic` | 0,175 | generic 0,18 / string 0,21 / obx 0,24 |
| other | `vsm.string` | 0,203 | string 0,20 / ms20 0,21 / dx7 0,23 |
| vocals | `vsm.wind` | 0,164 | wind 0,16 / obx 0,20 / generic 0,21 |
| drums | `vsm.drums` | — | 5 pièces, 1092 frappes, aucun échantillon écrit |

**Distance globale : 0,2672.** Et l'attribution, faite par ablation sur ce
rendu-là plutôt que supposée :

| variante | distance (v2) |
|---|---|
| livré — quatre pistes, que des synthés | **0,2672** |
| piste de voix coupée | 0,2663 |
| voix REPORTÉE telle quelle (mesure seule, pas livrée) | 0,2682 |
| silence | 0,9674 |

**Sur ce morceau, l'interdiction ne coûte rien de mesurable.** Le stem `vocals`
de *Children* est du résidu de séparation — 0,0033 de niveau efficace contre
0,156 pour la batterie — et le rendu ne bouge pas de plus d'un millième selon
qu'on y met un `vsm.wind`, l'enregistrement lui-même, ou rien. La piste
synthétisée est même très légèrement défavorable (+0,0009) ; elle est conservée
parce que la contrainte demande un projet à quatre pistes de synthèse, et le
chiffre est écrit plutôt que tu. **Ce résultat ne se généralise pas** : il tient
à ce que ce morceau-ci n'a pratiquement pas de chant, et un morceau chanté
paierait l'interdiction au prix fort.

**Un défaut soupçonné, et démenti par la mesure.** Le calage automatique des
volumes demande un facteur 7,7 pour la basse et **butte sur sa borne de 2,5** :
le patch `vsm.generic` retenu sort à 0,0113 de niveau efficace quand son stem
est à 0,0867. La borne a donc été soupçonnée de peser sur la distance globale,
par analogie avec la batterie trois fois trop faible du § précédent. Rendu avec
le facteur réellement demandé, **c'est pire** : 0,2792 contre 0,2672. La borne
protège ici au lieu de nuire — une piste calée au niveau efficace de son stem
n'est pas une piste calée au bon niveau dans le mélange, dès lors que son timbre
ne recouvre pas celui de l'original. Le soupçon est écrit avec son démenti,
parce que l'intuition était raisonnable et qu'elle était fausse.

**Ce qui n'est pas comparable.** Les distances PAR STEM ci-dessus sont
identiques au millième à celles du tableau AVANT/APRÈS plus haut : c'est le même
fichier, et la chaîne est restée déterministe. La distance GLOBALE, elle, ne se
compare pas à 0,2169 en l'état — cette mesure-là n'a pas laissé d'artefact sur
disque, et la seule façon honnête de l'opposer à 0,2672 serait de refaire tourner
la variante AVEC sampler de bout en bout sur ce même fichier. Ce n'est pas fait,
et c'est dit plutôt que comblé par une hypothèse.

---

## 34. Ce qu'on juge n'était pas ce qu'on écoute : la piste, puis le mélange

Point de départ : *Children (Dream Version)* reconstruit à **0,2672**, et
l'utilisateur qui dit « on est encore loin, et ce morceau ne devrait pas être
compliqué ». Il avait raison sur les deux points, et chercher où l'écart se
logeait a mis au jour un défaut de MÉTHODE, pas un défaut de machine.

### D'abord mesurer où ça casse, et non ce qu'on suppose

Chaque piste rendue seule, contre son propre stem :

| piste | machine | distance à son stem | (silence) |
|---|---|---|---|
| **bass** | `vsm.generic` | **0,4614** | 0,9272 |
| vocals | `vsm.wind` | 0,3972 | 0,8373 |
| other | `vsm.string` | 0,2701 | 0,9368 |
| Batterie | `vsm.drums` | 0,2165 | 0,9360 |

La basse est à mi-chemin du silence. Premier soupçon, raisonnable : la
transcription, qui ajoute des octaves fantômes (le stem tourne sur 29 / 34 / 37,
le MIDI ajoute 41 / 46 / 49). **Démenti par la mesure**, à patch constant :

| notes | nombre | distance |
|---|---|---|
| transcription par défaut | 833 | **0,4614** |
| doublures d'octave retirées | 624 | 0,4714 |
| réduite à une voix (la plus grave) | 568 | 0,4736 |
| plafond de fréquence à 300 Hz | 832 | 0,4616 |

Toutes les « corrections » sont pires. Ce n'était pas les notes.

### Le défaut : la machine était choisie sur UNE note

La piste de basse rendue par les dix-neuf machines, **patch d'usine, sans
aucune recherche** :

| machine, patch d'usine | distance |
|---|---|
| **`vsm.piano`** | **0,2820** |
| `vsm.dx7` | 0,3874 |
| `vsm.tonewheel` | 0,4110 |
| … | |
| `vsm.generic` (la machine retenue) | 1,5374 |
| *`vsm.generic` avec son patch cherché* | *0,4614* |

Une machine que la recherche n'avait pas retenue, avec le patch qu'elle a en
sortant de boîte, fait **39 % mieux** que la gagnante réglée sur mesure. Ce
n'est pas un défaut de l'optimiseur : la recherche choisit sur la note la plus
longue du stem, parce que chercher sur chacune coûterait des heures.
L'hypothèse « les notes d'un même instrument partagent leur timbre » tient pour
RÉGLER un patch ; elle ne tient pas pour CHOISIR une machine. Les deux critères
ne classent pas dans le même ordre, et c'est le second qu'on écoute. À noter :
`--finalistes 0` n'y aurait rien changé — le défaut est dans le critère, pas
dans la présélection.

### Trois étapes ajoutées, et ce que chacune a rapporté

**1. L'arbitrage sur la piste** (`analyzer/vsm_track_arbitration.py`). Après la
recherche, la piste ENTIÈRE est rendue avec le patch trouvé de chaque machine
*et* avec le patch d'usine de chaque machine ; on garde la meilleure. Le patch
d'usine est dans la liste précisément parce que le cas ci-dessus s'est produit.

| piste | recherche (une note) | arbitrage (la piste) |
|---|---|---|
| bass | `vsm.generic` 0,461 | **`vsm.piano`** d'usine 0,282 |
| other | `vsm.string` 0,270 | **`vsm.ms20`** d'usine 0,250 |
| vocals | `vsm.wind` cherché 0,395 | `vsm.wind` **d'usine** 0,346 |

Le cas `vocals` est le plus parlant : même machine, et c'est le patch réglé sur
une note qui perd contre le patch sorti de boîte.

**2. Le plafond de volume, relevé de 2,5 à 10.** L'arbitrage n'a d'abord rendu
que 0,2672 → 0,2608 sur le morceau, alors que les pistes gagnaient 39 % et 7 %.
Le calage butait sur sa borne : la basse demande un facteur 6,7 et n'en recevait
que 2,5.

| volume de la basse | distance du morceau |
|---|---|
| 2,5 (l'ancien plafond) | 0,2608 |
| 4 | 0,2443 |
| 5 | 0,2355 |
| 6,7 (le facteur demandé) | **0,2246** |

Le plafond coûtait 14 %. **Et son envers a été mesuré aussi** : avec la basse
précédente (`vsm.generic`, timbre faux), ce même facteur DÉGRADAIT le résultat
(0,2672 → 0,2792). Le plafond compensait un mauvais patch au lieu de le
corriger. Une piste juste veut son niveau ; une piste fausse veut être refaite.

**3. Le réglage du patch sur la piste** (`analyzer/vsm_track_refine.py`) : une
descente par coordonnées, axe par axe dans l'ordre d'importance que la machine
déclare, deux passes (large puis resserrée), une valeur gardée seulement si elle
rapproche le rendu complet. Elle améliore franchement chaque piste — et **elle
éloigne le morceau**.

### Le résultat qui a demandé une quatrième étape

| | pistes contre leurs stems | le MORCEAU |
|---|---|---|
| arbitrage seul | basse 0,282 · other 0,250 · voix 0,346 | **0,2246** |
| + réglage libre | 0,206 · 0,246 · 0,168 | 0,2519 |
| + réglage contraint en niveau | 0,216 · 0,246 · 0,168 | 0,2380 |

Première cause, trouvée et corrigée : **la distance est insensible au niveau**,
donc la descente n'a aucune raison de préserver le volume de sortie. Sur la
basse elle a trouvé un patch meilleur de 27 % en timbre et deux fois plus faible
(0,0117 → 0,0060) ; le calage réclamait ×13, butait, et l'écrêtage passait de
840 à 8 519 échantillons. D'où la contrainte, aux deux étapes : un candidat dont
le niveau ne pourra plus être rattrapé (`0,9 × rms_stem / rms_rendu > 10`, la
formule exacte du calage avec sa borne) est refusé là où l'on sait encore
pourquoi.

Mais la contrainte ne récupère que la moitié de l'écart. **Le reste n'est pas
une affaire de volume** : les stems d'une séparation ne se rendorment pas
exactement dans l'original -- ils se recouvrent et fuient l'un dans l'autre --
et rien ne garantit qu'une piste plus proche de SON stem donne un mélange plus
proche du MORCEAU. C'est la leçon centrale de cette section, et elle vaut pour
toute la chaîne : **une piste jugée seule et une piste dans un mélange ne sont
pas le même objectif.**

**4. Le verdict du mélange** (`analyzer/vsm_mix_verdict.py`). Pour chaque piste
ayant deux propositions, le projet complet est rendu avec l'une puis avec
l'autre — volume recalé à chaque fois, sans quoi on comparerait deux patchs au
mauvais niveau — et l'on garde celle qui rapproche du morceau, en disant ce
qu'on écarte. C'est mot pour mot la règle de l'automation de coupure (« gardée
seulement si elle RAPPROCHE le rendu »), remontée d'un cran. Le réglage ne peut
donc plus dégrader le résultat : au pire il est refusé piste par piste.

### Ce que ça coûte, et deux limites assumées

| opération | coût mesuré (morceau de 4 min) |
|---|---|
| un rendu de piste | 1,4 s |
| une distance v2 sur la piste entière | **3,7 s** |
| une distance v2 sur 30 s | 0,35 s |
| un rendu de projet complet | ~5 s |

C'est la DISTANCE qui domine, pas le rendu. L'arbitrage coûte donc ~3 min par
stem (38 candidates), le réglage ~3 min (40 évaluations, budget exposé par
`--budget-piste`), le verdict ~10 s par proposition.

Deux limites, écrites plutôt que découvertes plus tard : le réglage ne traite
que la machine GAGNANTE (régler les trois premières et les redépartager
coûterait trois fois plus pour un gain non mesuré), et le verdict est GLOUTON
piste par piste (les huit combinaisons de trois pistes coûteraient huit rendus
au lieu de six, gain non mesuré non plus).

### Un piège d'exécution qui a tué la première tentative

L'arbitrage gardait ses trente-huit rendus. Un rendu de quatre minutes en stéréo
flottante pèse 82 Mo : 3 Go par stem, dans un `/tmp` qui est un disque **en
mémoire** de 7,7 Go sur cette machine. `No space left on device`, après que
l'arbitrage de la basse eut abouti. Chaque candidate est désormais rendue dans
le même dossier et son WAV effacé sitôt lu : l'empreinte est celle d'un seul
rendu, quel que soit le nombre de candidates. La leçon n'est pas « nettoyer ses
fichiers » mais : **une étape qui multiplie les rendus multiplie aussi leur
poids, et un disque en mémoire ne pardonne pas.**

### Deux options de plus sur `reconstruire.py`

- `--stems <dossier>` reprend des stems déjà séparés. Refaire trois minutes de
  séparation pour comparer deux réglages de la SUITE de la chaîne ne mesure rien
  de plus, et rend les deux passes moins comparables si le modèle change.
- `--sans-arbitrage` et `--sans-reglage-piste` rendent les comportements
  d'avant, parce que c'est ainsi qu'on attribue un écart à une étape et non à
  un ensemble.

### La cible ne change pas : 64 % du coût d'une évaluation, rendus

Les trois nouvelles étapes recalculaient les descripteurs de LA CIBLE à chaque
évaluation, alors que le stem ne bouge jamais d'une candidate à l'autre : sur
38 candidates d'arbitrage puis 40 évaluations de réglage, c'était 77 fois le
même travail. La leçon existait déjà dans le dépôt -- `vsm_distance_cache` l'a
tirée pour des cibles d'une SECONDE -- et elle n'avait pas été appliquée à des
cibles de quatre MINUTES, là où elle vaut cent fois plus.

| | valeur rendue | temps |
|---|---|---|
| calcul direct | 1,931979 | 5,55 s |
| cible en cache | 1,931979 | **2,02 s** |

**L'écart des valeurs est nul au dernier chiffre**, et c'est la condition qui
compte : une optimisation qui déplacerait la distance d'un millième rendrait
incomparables les passes d'avant et d'après, c'est-à-dire tous les tableaux de
cette section. Mesuré à l'usage : réglage d'un stem en 111 s au lieu de 230.

### Un réglage figé sans être mesuré : le nombre d'AXES

La descente n'explorait que les **8 premiers axes** déclarés par la machine.
C'est un chiffre que j'avais choisi, pas mesuré. `vsm.drums` en déclare **21**
-- un accord, une extinction et un niveau par pièce, plus la salle -- donc
treize restaient à leur valeur d'usine sans que personne les ait jugés, sur la
piste qui pèse le plus lourd du mélange. Exposé en `--axes-piste`, et la mesure
est sans appel :

| batterie, réglage sur la piste | distance à son stem |
|---|---|
| 8 axes, 40 évaluations | 0,219 → **0,219** (rien) |
| 16 axes, 126 évaluations | 0,219 → **0,189** |

Et les axes qui l'ont fait bouger sont les **`level`** pièce par pièce, classés
au-delà du huitième rang d'importance : invisibles à l'ancienne descente par
construction. Même effet sur la basse : 0,216 à budget 40, **0,199** à budget
150.

### Séparer en six sources : mesuré, et refusé

`htdemucs_6s` sort un stem de piano et un de guitare en plus des quatre
habituels. Sur un morceau dont le piano est la signature, c'était le levier le
plus prometteur de la file. Il coûte :

| séparation | distance du morceau |
|---|---|
| `htdemucs`, 4 sources | **0,2190** |
| `htdemucs_6s`, 6 sources | 0,2325 |

La cause se lit dans les stems, pas dans la chaîne : **le modèle à six sources
sépare moins bien la batterie** (son stem part de 0,279 au lieu de 0,219 à
niveau efficace identique), et la batterie est la source dominante (0,156
contre 0,011 pour le prétendu piano). On dégrade la source qui pèse pour gagner
deux pistes maigres. Le verdict du mélange l'a d'ailleurs dit à sa façon : il a
refusé **cinq réglages sur six** dans cette passe.

Deux observations qui valent d'être gardées :

- **ce que la séparation appelle « piano » n'est pas un piano.** Sur ce morceau
  le stem porte 0,0113 de niveau efficace, et l'arbitrage le fait jouer par
  `vsm.wind` -- une machine à anche -- devant `vsm.piano`, qui n'est même pas au
  podium. Pendant ce temps la BASSE, elle, est jouée par `vsm.piano`. Les
  étiquettes de la séparation ne disent rien de ce qu'il faut pour rejouer le
  contenu ;
- **c'est dans cette passe que des patchs CHERCHÉS ont gagné l'arbitrage** pour
  la première fois (guitare, `other`, voix), là où les patchs d'usine
  l'emportaient partout ailleurs. La recherche sur une note n'est donc pas
  inutile : elle est insuffisante à décider seule, ce qui est une conclusion
  plus juste que « elle se trompe ».

### Le stem « piano » repris seul : mesuré aussi, et refusé aussi

Le recul de la passe à six sources venait de la BATTERIE, pas du piano. On peut
donc ne prendre du modèle à six sources que ce qu'on lui demandait : un jeu de
stems **hybride** — `drums`, `bass`, `vocals` de `htdemucs`, le `piano` de
`htdemucs_6s`, et `other` de `htdemucs` MOINS ce piano, pour que rien ne soit
compté deux fois. Le contrôle qui rend l'opération honnête : la somme des cinq
stems hybrides moins la somme des quatre d'origine vaut **0,00e+00** — on n'a ni
ajouté ni perdu de matière, seulement sorti le piano de son fourre-tout.

| stems | distance du morceau |
|---|---|
| 4 sources | **0,2190** |
| hybride, piano extrait | 0,2210 |
| 6 sources entières | 0,2325 |

L'hybride récupère bien les trois quarts du recul (0,2325 → 0,2210), ce qui
confirme le diagnostic. Mais l'extraction elle-même coûte **+0,0020** : elle
retire 0,005 à `other`, qui devient plus difficile, et rend une piste à 0,174
qui ne pèse que 0,0113 de niveau efficace. Le levier est refusé sur son chiffre,
pas sur un principe.

### Bilan de la file : ce qui a marché, ce qui n'a pas marché

| passe | ce qui change | distance |
|---|---|---|
| v1 | recherche sur une note (état d'avant) | 0,2672 |
| v2 | + arbitrage sur la piste | 0,2608 |
| v3 | + réglage du patch, libre | 0,2519 |
| v4 | + réglage contraint en niveau | 0,2380 |
| v5 | + verdict du mélange (plafond de volume levé) | 0,2204 |
| v6 | + réglage de la batterie | 0,2190 |
| v7 | séparation à six sources | 0,2325 |
| v9 | stems hybrides, piano extrait | 0,2210 |
| **v8** | **+ budget élargi (150 évaluations, 16 axes)** | **0,2101** |

**De 0,2672 à 0,2101, soit −21 %**, sans ajouter une seule machine ni changer
une ligne de DSP : uniquement en changeant CE QU'ON JUGE et QUAND.

Trois leviers ont payé (arbitrage sur la piste, verdict du mélange, budget
élargi), deux ont été refusés sur leur chiffre (six sources, piano extrait), et
un troisième — le réglage des patchs mélodiques — n'a jamais été gardé par le
verdict du mélange sur AUCUNE des trois dernières passes, alors qu'il améliore
toujours la piste contre son stem.

**Ce dernier point est la règle à retenir de toute cette section, et elle se
répète assez pour ne plus être une anecdote : régler un patch mélodique contre
son stem n'aide pas le mélange ; régler la batterie l'aide.** L'explication
tient à la séparation elle-même — les stems mélodiques se recouvrent et fuient
les uns dans les autres, tandis que la batterie occupe un domaine
temps-fréquence que rien d'autre n'occupe. Son stem est presque le vrai signal ;
les autres n'en sont qu'une estimation. À la passe 8, le réglage de la batterie
rapporte à lui seul 0,0146 sur le morceau, et les trois réglages mélodiques sont
écartés.

### Un preset ne doit dépendre de rien

Conséquence inattendue de l'arbitrage : quand un patch d'USINE gagne, le
dictionnaire de paramètres est vide, et le preset écrit ne dit rien. Le son du
projet dépendrait alors des valeurs par défaut de la machine **au moment où on
l'ouvre** — le jour où un défaut change, le morceau change sans que rien ne le
signale. C'est exactement la divergence silencieuse que ce dépôt refuse
partout ailleurs, et elle était entrée par la porte de derrière.

Les presets écrivent désormais les valeurs RÉSOLUES : les défauts de la machine,
surchargés par le patch retenu. Vérifié sur le projet livré — 16 à 23 paramètres
inscrits par piste au lieu de 0 à 8, et le rendu **identique octet pour octet**.

---

## 34 bis. `vsm.tonewheel` — quatre-vingt-onze sinus par échantillon, dont neuf servaient

Trouvé en engendrant le corpus d'apprentissage (phase A0), qui mesure le débit
machine par machine : l'orgue tombait à **25 exemples par seconde contre 98 en
moyenne**. Vérifié au rendu isolé, l'écart était pire encore.

| | rendu de 0,6 s |
|---|---|
| `vsm.tonewheel`, avant | **25,9 ms** |
| `vsm.minimoog`, pour situer | 2,6 ms |
| `vsm.tonewheel`, après | **4,9 ms** |
| `vsm.tonewheel`, après, accord de six notes | 7,5 ms |

**La cause.** `TonewheelGenerator::advance()` calculait le sinus des
quatre-vingt-onze roues à CHAQUE échantillon. C'est fidèle au mécanisme — les
roues d'un Hammond tournent en permanence, qu'on les écoute ou non — mais une
roue que personne ne lit ne contribue à rien, et une note n'en lit que neuf, une
par tirette.

**Le correctif, et sa condition.** Le sinus est calculé À LA DEMANDE, avec un
cache d'un échantillon qu'un compteur invalide d'un coup. Les phases, elles,
continuent d'avancer TOUTES — une addition par roue, pas un sinus — si bien
qu'une roue qu'on se met à lire au milieu d'un morceau a exactement la phase
qu'elle aurait eue. Le rendu est donc **identique au bit près**, et c'est
l'empreinte de non-régression de la machine qui le prouve, pas une relecture.

**Ce que ça change au-delà du corpus.** La recherche de patch payait ce facteur
dix à chaque évaluation : l'orgue coûtait dix fois un Minimoog pour être comparé
à lui. Un banc de mesure qui n'existait pas — la génération d'un corpus, machine
par machine, avec son débit publié — a suffi à rendre visible un défaut que des
mois d'usage n'avaient pas montré. C'est l'argument le plus solide en faveur de
ces bancs : ils trouvent ce qu'on ne cherchait pas.

---

## 35. `vsm.multisample` — l'acoustique par report, et ce que la mesure exigeait

**Ce qui a rendu cette machine nécessaire n'est pas un raisonnement, c'est une
photo-finish.** *Clair de Lune* (piano seul, 311 s, 2219 notes), reconstruit le
23/08/2026 par la chaîne complète (`--sans-separation`, métrique v2) :

| machine | distance |
|---|---|
| `vsm.supersaw` | **0,2590** |
| `vsm.generic` | 0,2649 |
| `vsm.minimoog` | 0,2703 |
| `vsm.obx` | 0,2707 |
| `vsm.pcmhybrid` | 0,2727 |
| `vsm.wavetable` | 0,2897 |
| `vsm.jupiter8` | 0,2970 |
| `vsm.tonewheel` | 0,3217 |

Six pour cent séparent la première de la huitième. Ce n'est pas un classement,
c'est une absence d'information : **aucune machine du parc ne ressemble à un
piano, et toutes s'en écartent autant.** La distance globale du morceau valait
1,639, dont l'essentiel récupéré non par le patch mais par l'automation de
coupure — 8,34 → 1,64 en six cent six points. Le parc COMPENSAIT au lieu de
REPRODUIRE, et l'écoute le confirme : les notes et le geste y sont, le timbre
non.

**La réponse est UNE machine, pas une par instrument.** Un lecteur
multi-échantillons couvre le piano aujourd'hui et, par l'import SoundFont,
l'orchestre General MIDI entier demain — sans une ligne de DSP nouvelle. C'est
le choix inverse de `vsm.string`, `vsm.piano` et `vsm.wind`, et les deux se
justifient : la modélisation physique donne l'EXPRESSIVITÉ (un archet qui
appuie, une anche qui claque), le report d'échantillon donne la COUVERTURE. Le
§ 27 autorise explicitement le second à condition de le dire, et c'est ce que
fait cette section.

### Ce qu'elle n'est pas : `vsm.sampler` élargi

Le sampler est **percussif par construction** : seize emplacements, déclenchés
par note fixe, sans transposition ni couche de vélocité — parce que transposer
un coup de caisse claire selon la touche produirait n'importe quoi. Sa façade
et son § 30 sont pensés batterie. L'étendre le dénaturerait. `vsm.multisample`
prend l'hypothèse inverse : la note **sélectionne une zone ET la transpose**, la
vélocité **choisit une couche**. Ce qu'elle lui emprunte, en revanche, elle
l'emprunte tel quel : chargement hors thread audio, publication par échange
atomique de `shared_ptr`, échantillon manquant SIGNALÉ et jamais substitué.

### La chaîne, et les trois pièges qu'elle contient

```
note MIDI + vélocité
      │   première zone du profil qui les contient  (l'ORDRE fait foi)
      v
  lecture repitchée, interpolation cubique Catmull-Rom
      │   boucle de tenue repliée AU NIVEAU DES PRISES, pas seulement du curseur
      v
  enveloppe (attaque, tenue à 1, relâchement) — PAS de décroissance imposée
      v
  niveau · vélocité · passe-bas doux, neutre EXACTEMENT à fond
```

1. **Le rapport des fréquences d'échantillonnage.** Un fichier à 44,1 kHz relu
   tel quel par un moteur à 48 kHz sonne un demi-ton trop bas. C'est la panne la
   plus courante d'un lecteur d'échantillons et elle est **silencieuse** : tout
   joue, tout est faux. Un test la couvre en mesurant la hauteur rendue à
   ±5 cents, fichier à 44,1 kHz compris.
2. **La boucle et l'interpolation.** Replier seulement le curseur ne suffit
   pas : les quatre prises de Catmull-Rom déborderaient de la fin du fichier et
   liraient des zéros, ce qui produit exactement le clic que la boucle existe
   pour éviter. C'est `frameAt()` qui replie, prise par prise. Vérifié par
   continuité d'énergie sur quatre secondes de tenue et par une borne sur le pas
   d'échantillon.
3. **Pas de décroissance dans l'enveloppe.** Sur un instrument échantillonné,
   la décroissance est DANS le fichier. En imposer une par-dessus revient à
   amortir deux fois — c'est précisément ce qui fait sonner « synthétique » un
   lecteur d'échantillons mal réglé. L'enveloppe se réduit donc à attaque,
   tenue à 1, relâchement.

### Deux refus, qui sont le cœur de son honnêteté

**Une note hors zone ne sonne pas, et ne consomme pas de voix.** Emprunter la
zone voisine ferait jouer un son faux que personne ne rattacherait au trou dans
le profil. Le trou reste donc audible comme un trou.

**Sans profil, la machine est muette — et le service de rendu REFUSE de la
rendre.** C'est le point le moins évident et le plus important : une machine
muette ne perd pas la comparaison, elle la **fausse**, en gagnant sur toutes les
cibles douces. Une distance mesurée contre du silence est un chiffre, et un
chiffre faux coûte plus cher qu'une erreur franche. La chaîne d'analyse écarte
donc la machine de ses candidates tant qu'aucun profil n'est installé, **en le
disant**, et le pont refuse la requête plutôt que de rendre zéro.

### Le réglage de timbre est neutre EXACTEMENT, pas presque

Un passe-bas laissé actif « très haut » reste mesurable : à 20 kHz sur un signal
à 260 Hz il déphase encore. Le rendu par défaut porterait une couleur que
personne n'a demandée, et l'empreinte de non-régression mesurerait ce filtre au
lieu de mesurer le lecteur. Bouton à fond veut donc dire **chemin direct**. Le
test le vérifie de la façon la plus stricte possible : joué à sa note racine, à
la fréquence du moteur, sans vélocité ni accord, le lecteur rend le FICHIER,
échantillon pour échantillon, à 10⁻⁶ près.

### Un cache d'échantillons, sans lequel la machine était incherchable

Le service de rendu crée une instance NEUVE par requête — c'est ce qui garantit
que deux rendus identiques donnent le même son (§ 28), et il ne faut pas y
toucher. Mais installer un profil de piano, c'est décoder cent vingt fichiers et
deux cent trente mégaoctets :

| | coût d'une évaluation |
|---|---|
| `vsm.minimoog` | 4,1 ms |
| `vsm.multisample`, profil rechargé à chaque rendu | **123,9 ms** |
| `vsm.multisample`, échantillons décodés mis en cache | **2,0 ms** |

Une machine trente fois plus chère que les autres ne se cherche pas : la
présélection l'écarte au premier tour, et son absence se lit comme un mauvais
résultat. `MultisampleSampleCache` partage donc les échantillons DÉCODÉS entre
les instances successives, et **rien d'autre** — jamais de l'état de machine.
C'est la condition à laquelle il est acceptable, et un test la vérifie : le
rendu est identique **au bit près** avec cache et sans.

### Le format de profil vit dans `interchange/`, jamais dans `audio/`

L'invariant est net : le moteur ne connaît aucun format d'échange. La machine
reçoit donc des ZONES — structures nues décrites par `IMultisampleBank` — et n'a
jamais entendu parler de JSON. Ce n'est pas une élégance : c'est ce qui permettra
d'ajouter les round-robin, les échantillons de relâchement et un jour le SFZ
**sans recompiler ni reregresser une seule machine**.

Le format `*.profile.json` vérifie à la lecture, et refuse plutôt qu'il
n'interprète : format et version contrôlés, chemins d'échantillons
obligatoirement relatifs (un chemin absolu rend un projet non transportable),
étendues non vides, points de boucle cohérents avec la longueur du fichier,
budget mémoire de 256 Mo. **L'attribution est obligatoire** : une banque dont on
ne sait pas sous quelle licence elle circule n'est pas chargée « en attendant »
(§ 28). Et tout champ inconnu est **nommé** dans le rapport de chargement —
règle mise en place ici, sur le format natif où elle est facile à vérifier,
avant que l'import SoundFont n'en ait besoin pour ses générateurs exotiques.

### Approximations assumées de la v1

| Omis | Pourquoi, et ce qu'il faudrait pour le lever |
|---|---|
| **Pédale forte (CC64)** | `ISynthPlugin` transporte NoteOn/NoteOff, pas les CC. Moins grave qu'il n'y paraît **pour la reconstruction** : la transcription inscrit déjà la pédale dans la DURÉE des notes (les 2219 notes de Clair de Lune la portent). Pour le jeu au clavier, la limite est réelle. Étendre le contrat aux CC datés au sample est une décision de MOTEUR, à prendre séparément — pas en contrebande d'une machine. |
| Échantillons de relâchement | une couche de plus par zone ; le budget mémoire décide |
| Round-robin | demande un état par note, donc un rendu non déterministe à graine égale — à peser contre l'invariant de déterminisme |
| Streaming disque | le préchargement tient dans 256 Mo pour un piano ; un orchestre entier ne tiendra pas, et c'est là qu'il faudra le faire |
| Résonance sympathique | c'est de la modélisation, pas de la lecture — elle appartient à `vsm.piano` |

### Le profil piano, et ce que la banque réelle a répondu

Installé par `tools/installer-profil-piano.py` depuis **Salamander Grand Piano
V3** (Alexander Holm, CC-BY 3.0), archive 44,1 kHz / 16 bits (412 Mo, empreinte
SHA-256 épinglée dans l'outil). La banque contient 480 échantillons utiles :
**trente notes racines** — un échantillon tous les trois demi-tons, de A0 à C8 —
et **seize couches de vélocité**. Le profil en retient quatre couches et toutes
les racines, tronquées à six secondes : **120 zones, 229 Mo en mémoire** pour un
budget de 256.

Elle contient aussi des échantillons de RELÂCHEMENT (`rel79.wav`) et
d'HARMONIQUES (`harmSA4.wav`). Ils sont ignorés, et le motif de nommage de
l'outil est ancré pour qu'ils le soient explicitement : c'est l'omission
déclarée de la v1, pas un oubli.

**Justesse, mesurée sur les soixante et une notes de 36 à 96, jouées isolément**
(pic spectral cherché dans une fenêtre de ±6 % autour du fondamental attendu) :

| | écart au tempérament égal |
|---|---|
| médiane | **5,8 cents** |
| maximum | 20,5 cents (notes 95-96, extrême aigu) |
| extrême grave (37-39) | −16 à −18 cents |

Ces écarts ne sont pas une erreur de repitch : c'est **l'accord étiré** d'un
piano réel, dont les partiels graves sont bas et l'aigu haut par inharmonicité
des cordes. Un lecteur d'échantillons juste au cent près rendrait un piano moins
fidèle, pas plus. La vérification du repitch au sens strict — ±5 cents — se fait
donc sur une sinusoïde de référence, dans les tests, où la question a un sens.

**Coutures aux frontières de zones**, mesurées par le rapport
centroïde spectral / fondamental sur la gamme chromatique 48-84 :

| | écart relatif d'une note à la suivante |
|---|---|
| à l'intérieur d'une zone | **0,5 %** |
| à une frontière de zone | **13,1 %** |
| gradient NATUREL du piano entre deux racines (3 demi-tons) | **13,2 %** |

Le chiffre qui compte est le troisième. La couture n'AJOUTE rien : elle
concentre en un pas le gradient que l'instrument parcourt naturellement en
trois demi-tons, parce que la banque n'a pas d'échantillon intermédiaire à
offrir. Descendre à un échantillon par demi-ton lisserait la marche — et
triplerait la mémoire, sans que rien n'ait encore montré que la marche
s'entende. C'est mesuré, c'est écrit, et c'est là qu'il faudra revenir si
l'écoute dit le contraire.

### L'import SoundFont : cent vingt-huit instruments d'un coup

Un lecteur multi-échantillons sans banque est un moteur sans carburant. Le SF2
est le seul format d'instrument échantillonné dont il existe des banques libres
COMPLÈTES et attribuables — FluidR3, GeneralUser GS —, et le lire ouvre
l'orchestre General MIDI entier sans une ligne de DSP nouvelle.

**Ce qui est lu, et ce qui ne l'est pas.** Le format déclare une soixantaine de
« générateurs » ; une petite dizaine décrit un instrument échantillonné (zones
de notes et de vélocités, note racine, accord, boucle, atténuation, enveloppe de
volume), le reste décrit une synthèse soustractive complète — filtre, LFO,
enveloppe de modulation — que cette machine ne possède pas. Les générateurs non
appliqués sont **imprimés à la conversion, un par un, avec leur nombre
d'occurrences**. Une banque dont le caractère tient à son filtre se convertirait
sinon en silence, et personne ne saurait pourquoi elle sonne plat.

**Où vit ce code.** L'outil est `tools/vsm-sf2` ; la LECTURE est dans
`interchange/`, parce que dans ce projet tout format a des tests et qu'il
n'existe pas d'infrastructure de test Python. Un analyseur de format sans tests
est précisément la pièce qui se met à mal interpréter un champ sans que personne
ne le voie — l'atténuation SF2 est en **centibels**, et la confondre avec des
décibels donne des couches douces dix fois trop faibles. C'est un test qui l'a
attrapée. `core/` et `audio/` ignorent tout du SF2 ; le DAW ne charge jamais
qu'un profil.

**Ce que le format de profil ne sait pas dire.** L'enveloppe de volume du SF2
n'a pas sa place dans le profil, qui n'a pas d'enveloppe par zone. L'outil écrit
donc à côté un preset `*.synth.json` — un format qui existe déjà — portant
`envelope.1.{attack,release}` et désignant le profil par son nom. Rien n'est
perdu en silence : ce qui n'entre pas dans le profil entre dans le preset, et ce
qui n'entre nulle part est imprimé.

**Le SF2 d'essai est ENGENDRÉ, pas commis.** Un binaire commis est opaque :
quand un test échoue, on ne sait pas si la faute est au lecteur ou au fichier.
Ici le contenu attendu est du code relisible, et le test compare la lecture à ce
que l'écriture a voulu dire — zone globale d'instrument comprise, qui est la
construction qu'emploie toute banque réelle.

### Le banc de clôture : la machine n'apporte rien sur *Clair de Lune*

Il faut l'écrire ici, à côté de la description de la machine, parce que c'est ce
qu'un lecteur doit savoir avant de s'en servir. Mesuré à conditions strictement
identiques — même morceau, même code, mêmes options, seul le dossier de profils
changeant :

| | distance globale | machine retenue |
|---|---|---|
| sans `vsm.multisample` | **0,2159** | `vsm.piano` |
| avec `vsm.multisample` | **0,2159** | `vsm.piano` |

Elle finit **7e sur 30** au tableau d'arbitrage (0,3571, patch d'usine) et ne
survit pas à la présélection de la recherche par note. Trois causes mécaniques
ont été éprouvées et écartées — troncature (−49 dB de discontinuité), double
comptage de la dynamique (un millième d'écart sur toute la course du réglage),
absence de production (la réverbération dégrade les deux rendus) — et la
métrique désigne bien la machine quand la cible est son propre rendu (0,0000
contre 0,1648 au second). Le détail et les trois lectures encore ouvertes sont
dans [`docs/CDC-multisample.md`](docs/CDC-multisample.md) § 11 ; la leçon
générale est au § 5 sexies de la feuille de route.

Ce que la machine apporte, à ce stade, est donc la **couverture** — un lecteur
d'échantillons honnête, et l'orchestre General MIDI par l'import SoundFont — pas
une victoire mesurée sur le piano.

### Ce que la façade montre, et pourquoi elle est pauvre

Les autres machines portent leur timbre dans leurs commandes. Ici le timbre est
dans les échantillons, et la façade n'a rien à en dire. Lui inventer une rangée
de potentiomètres « pour faire riche » serait le mensonge que le § 29 reproche
aux façades décoratives. Elle montre donc **ce qui se joue** : programme,
toucher, accord, articulation, sortie — sept commandes, aucune omise. Le reste
— zones, couches, boucles — appartient au PROFIL, c'est-à-dire à un fichier, pas
à un bouton.

---

## 36. `vsm.perc` — les peaux et les barres, et pourquoi leurs modes sont irrationnels

**LE TROU ÉTAIT MESURÉ AVANT QU'UNE LIGNE NE SOIT ÉCRITE.** Le § 7 de
`docs/CDC-machines-manquantes.md` demande de juger une machine sur la COUVERTURE
qu'elle ajoute, pas sur le catalogue, et interdit d'en construire une sur la foi
d'un raisonnement. Celle-ci a deux chiffres derrière elle, tous deux tirés de
morceaux réels :

- sur *Sky and Sand*, la chaîne a imprimé « clap : famille sans voix déclarée
  dans `vsm.drums`, jouée sur la note 39 (**559 frappes**) ». Le détecteur nomme
  six familles ; `vsm.drums` n'en joue que cinq, la TR-808 non plus ;
- au-delà de ces six, les percussions au sens large — congas, bongos, timbales,
  cloche, claves, blocs de bois, shaker, tambourin — ne sont jouables par
  **aucune** machine du parc, ni nommables par le détecteur (A2.3 de
  `ROADMAP-apprentissage.md` : « le modèle ne nomme que ce qu'il a entendu »).
  Une machine qui les joue est la condition pour que le corpus de frappes
  puisse un jour les apprendre.

**LE TRAIT DISTINCTIF, ET IL EST PHYSIQUE.** Les trois boîtes du parc
fabriquent leurs peaux avec un sinus et une enveloppe de hauteur : leur spectre
est harmonique, ou presque pur. Une membrane circulaire tendue ne l'est pas —
ses modes sont les zéros de la fonction de Bessel J0, dans les rapports
**1 ; 1,594 ; 2,136 ; 2,296**. Ces rapports IRRATIONNELS sont exactement ce qui
fait qu'un tambour rend un *son* et non une *note*. Une barre libre aux deux
bouts sonne, elle, à **1 ; 2,756 ; 5,404** : c'est le « toc » d'un bloc de bois.

Mesuré sur un conga accordé à 200 Hz, amplitudes rapportées au fondamental :

| fréquence | amplitude |
|---|---|
| f0 | 1,000 |
| **1,593·f0** (2e mode de Bessel) | **0,301** |
| **2·f0** (l'octave, qu'un oscillateur donnerait) | **0,000** |
| 2,136·f0 (3e mode de Bessel) | 0,114 |

L'octave est **absente**, et c'est tout le sujet : c'est elle qui trahirait un
sinus déguisé en tambour. Deux tests la verrouillent
(`perc_membrane_modes_are_inharmonic`, `perc_bar_modes_are_inharmonic`), et ils
vérifient les deux moitiés de l'affirmation — le mode inharmonique est présent,
l'harmonique entière ne l'est pas.

**LE MODÈLE.** Chaque pièce est une somme de MODES, un mode étant un sinus qui
décroît : c'est la définition de la réponse d'un objet linéaire percuté. La peau
y ajoute un choc de main très court (6 ms de bruit filtré autour des modes) —
sans lui, une membrane modale sonne comme une cloche douce, alors que ce qu'on
reconnaît d'un tambour est d'abord son attaque. Le shaker et le tambourin ne
sont pas modaux du tout : ce sont des dizaines de chocs minuscules, donc du
bruit filtré, et prétendre le contraire serait de la décoration ; le tambourin
s'en distingue par deux modes de cymbalette, et par rien d'autre.

**TREIZE PIÈCES, EN NUMÉROTATION GENERAL MIDI SANS ÉCART** — tambourin 54,
cloche 56, bongos 60-61, congas 62-64, timbales 65-66, maracas 70, claves 75,
blocs 76-77. Un fichier MIDI écrit pour un module GM joue donc juste ici sans
traduction.

**UNE SATURATION EN SORTIE, ET ELLE EST ARGUMENTÉE.** Treize pièces frappées
ensemble s'additionnent en phase : mesuré, la crête monte à **3,79**. Baisser le
niveau d'autant rendrait la machine quatre fois plus faible que les autres sur
une frappe isolée — c'est-à-dire sur le cas normal — pour protéger un cas qui ne
se produit pas en musique. La sortie passe donc par une tangente hyperbolique,
ce que fait le bus d'une boîte analogique : linéaire à 1 % près sous 0,2, elle
ne peut mathématiquement pas dépasser 1. Une frappe seule y perd 6 % (0,439 →
0,413, contre 0,472 à la TR-808 et 0,688 à la TR-909 : la machine ne gagne ni ne
perd au volume) ; le pupitre entier y est comprimé au lieu d'écrêter.

**CE QUE CETTE MACHINE N'EST PAS.** Aucune mesure sur un instrument réel n'a
servi à la régler : statut « dérivé » au sens du § 8 de
`CDC-nouvelle-machine.md`. Les rapports de modes viennent de la physique, les
amplitudes et les durées sont choisies à l'oreille du modèle, pas relevées sur
un conga. Et l'excitation est un instant, pas une main : un conga frappé du plat
ou du bout des doigts n'excite pas les mêmes modes, mais le MIDI ne transporte
pas ce geste — la vélocité n'en change que le niveau.

---

## 37. `vsm.additive` — le spectre rang par rang, et la machine la plus inversible du parc

**LA FAMILLE ÉTAIT ABSENTE, ET C'EST LE SEUL CRITÈRE QUI COMPTE.** Le § 7 de
`CDC-machines-manquantes.md` déconseille explicitement « un septième
soustractif » — un nom sur une liste, pas une famille. Le parc avait dix
soustractifs, une FM, une table d'ondes, un hybride PCM, trois modélisations
physiques, deux lecteurs d'échantillons, un orgue à roues phoniques. **Personne
ne pouvait poser un spectre arbitraire** : l'orgue empile bien des sinus, mais
ses neuf tirettes sont à des rapports FIXES et sans enveloppe propre.

**CE QUE ÇA CHANGE POUR LA RECONSTRUCTION, et c'est l'argument qui décide.**
Toutes les autres machines fabriquent un spectre par un chemin INDIRECT : on
règle une coupure, une résonance, un indice de modulation, et le spectre en
découle ; chercher un patch, c'est inverser cette application. L'additif est le
seul dont les réglages DÉCRIVENT le spectre. Mesuré au pont Python, rangs 1 à 8
normalisés au maximum, sensibilité de vélocité neutralisée :

| réglage demandé | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | attendu |
|---|---|---|---|---|---|---|---|---|---|
| pente 0 dB/oct | 0,99 | 0,96 | 0,90 | 0,87 | 0,93 | 0,98 | 1,00 | 1,00 | tous égaux |
| pente −6 dB/oct | 1,00 | 0,48 | 0,31 | 0,22 | 0,19 | 0,17 | 0,15 | 0,13 | 1/n |
| pente −12 dB/oct | 1,00 | 0,24 | 0,10 | 0,06 | 0,04 | 0,03 | 0,02 | 0,02 | 1/n² |
| **impairs seuls** | 1,00 | **0,00** | 0,53 | **0,00** | 0,42 | **0,00** | 0,38 | **0,00** | pairs à zéro |
| **pairs seuls** | **0,00** | 1,00 | **0,00** | 0,64 | **0,00** | 0,59 | **0,00** | 0,52 | impairs à zéro |

Ce qu'on demande est ce qu'on obtient, à la deuxième décimale.

**LE TRAIT DISTINCTIF, TESTÉ DEUX FOIS.** *Le spectre à trous* : un filtre est
une fonction de transfert CONTINUE — il ne peut pas éteindre le rang 2 en
laissant intacts les rangs 1 et 3, quelle que soit sa résonance. Les deux
dernières lignes du tableau sont hors de portée de tout le reste du parc, et le
test vérifie les deux moitiés de l'affirmation (les rangs voulus présents, les
autres absents), sur les deux bouts de la course du réglage. *Les partiels
étirés* : une corde raide a ses rangs à `n·f0·sqrt(1 + B·n²)` et non aux
multiples entiers — c'est ce qui fait qu'un piano s'accorde faux exprès. Le test
vérifie que le rang 8 QUITTE 8·f0 et se retrouve là où la physique le met
(+2,5 % au maximum du réglage).

**SIX RÉGLAGES DE FORME, PAS SOIXANTE-QUATRE CURSEURS.** Un curseur par rang
serait fidèle à un Synclavier et inutilisable par la recherche de patch, qui a
besoin d'un espace de petite dimension (§ 6 de `CDC-machines-manquantes.md`).
Les trente-deux rangs sont donc pilotés par : nombre de rangs, pente,
balance impairs/pairs, raideur, décroissance différentielle et étalement
d'attaque. Le profil de recherche les classe dans cet ordre-là, et la raison est
écrite : **il n'y a pas de coupure ici**, c'est la PENTE qui joue son rôle.

**UNE NORMALISATION QUI BORNE, ET UN DÉFAUT TROUVÉ EN L'ÉCRIVANT.** La première
version divisait par la racine du nombre de rangs — l'usage, qui suppose des
phases indépendantes. Or les phases partent alignées (c'est ce qui rend le rendu
déterministe, donc l'empreinte de non-régression possible), et un accord de huit
notes à pente nulle crêtait alors **au-dessus de 1**. La sortie est désormais
divisée par la SOMME des amplitudes, qui est le majorant exact de la crête.
Le prix est assumé et il a un sens physique : trente-deux rangs se PARTAGENT
l'énergie au lieu de l'additionner. Mesurée, une note seule sort à 0,184 de
crête, contre 0,227 au Juno-106 et 0,217 au Prophet — la machine ne se départage
ni au volume ni contre elle-même.

**APPROXIMATIONS ASSUMÉES** (§ 8 de `CDC-nouvelle-machine.md`), statut
« dérivé » : trente-deux rangs au plus, ceux qui dépassent Nyquist ÉTEINTS et
non repliés (un test le vérifie sur une note aiguë : rien sous le fondamental) ;
une seule enveloppe d'amplitude par voix, plus une décroissance qui dépend du
rang ; pas de rapports inharmoniques libres — l'étirement est celui d'une corde
raide, à un paramètre, et une cloche demanderait autant de réglages que de rangs.

---

## 38. `vsm.westcoast` — ajouter des harmoniques au lieu d'en retirer

**LA MOITIÉ DU MONDE MANQUAIT.** Le parc compte dix soustractifs, plus la table
d'ondes et l'hybride PCM : douze machines qui partent d'une onde RICHE et lui
enlèvent ce qu'on ne veut pas, au filtre. C'est l'école de la côte est. L'autre
école fait l'inverse — partir d'un SINUS, l'onde la plus pauvre qui soit, et lui
FABRIQUER des harmoniques par pliage — et le § 7 de
`CDC-machines-manquantes.md` demande précisément d'ajouter des familles, pas des
noms.

**LE TRAIT DISTINCTIF EST UNE IMPOSSIBILITÉ POUR TOUT LE RESTE DU PARC.** Un
filtre est une opération LINÉAIRE : il ne crée jamais une fréquence qui n'était
pas là. Mesuré, à réglages égaux par ailleurs :

| | rang 1 | rang 3 | rang 5 |
|---|---|---|---|
| pliage nul | 1,00 | **< 0,01** | **< 0,01** |
| pliage maximal | 1,00 | **> 0,10** | > 0,02 |

À gauche, un sinus ; à droite, un spectre. Aucun filtre ne fait ce passage, et
c'est toute la définition de la synthèse par pliage. Un second réglage, la
SYMÉTRIE, décale le signal avant les replis : un plieur symétrique a une
fonction de transfert impaire et ne peut donner que des rangs impairs ; décalé,
les rangs pairs apparaissent. Deux tests verrouillent les deux, chacun sur les
deux bouts de la course.

**LA PORTE PASSE-BAS, ET C'EST LE SECOND TRAIT.** Sur ces machines il n'y a pas
un ampli d'un côté et un filtre de l'autre : un seul organe — une
photorésistance chauffée par une lampe, le « vactrol » — baisse À LA FOIS le
volume et la brillance, avec la lenteur d'un composant thermique. C'est pour
cela qu'une note s'y éteint en devenant SOURDE. Une seule enveloppe commande
donc les deux, lissée par une constante de temps réglable, et le test vérifie
que le centroïde spectral descend pendant l'extinction — ce qu'un ampli seul ne
produirait pas.

**LE NIVEAU, CALIBRÉ SUR UNE NORME RELEVÉE.** Un accord de huit notes crête, sur
les autres polyphoniques : Juno-106 0,944, Prophet 0,766, additif 0,736,
Jupiter-8 0,567. Cette machine montait à **1,664** — elle aurait écrêté là où
les autres ont de la marge. Une correction essayée d'abord n'a PAS suffi et
c'est écrit dans le code : donner à chaque voix une phase de départ différente
(ce qui est juste — huit oscillateurs libres ne sont jamais en phase) n'a fait
passer l'accord que de 1,746 à 1,664, cinq pour cent. Sur une seconde, huit
fréquences finissent par se croiser quel que soit leur point de départ. Le
facteur de voix est donc calibré à part, ce qui met une note seule à 0,154 —
exactement le Jupiter-8.

---

## 39. `vsm.fmdrums` — des percussions dont les partiels sont MOBILES

**CE QUE LES QUATRE AUTRES ROUTES NE FONT PAS.** Le parc sait faire une batterie
de quatre façons : analogique (TR-808, TR-909 : un sinus, une enveloppe de
hauteur, du bruit — spectre harmonique ou presque pur), acoustique modélisée
(`vsm.drums`), modale (`vsm.perc` : des partiels inharmoniques, mais FIXES,
ceux d'une membrane), et le report d'échantillon. Manque ce qui a fait le son
des boîtes numériques des années 80 : des percussions dont les partiels sont
inharmoniques **et mobiles**.

**POURQUOI LA FM, ET PAS AUTRE CHOSE.** Ses composantes tombent à
`|porteuse ± n·modulante|` : un rapport non entier suffit pour que rien ne
tombe sur la série harmonique, et l'indice étant sous enveloppe, **le spectre
change pendant la frappe**. Les rapports par défaut sont 1,414 (racine de deux)
et 1,618 (nombre d'or) — des irrationnels, qui ne peuvent être le rapport
d'aucun couple d'harmoniques.

Deux tests, pour les deux moitiés de l'affirmation. Le premier mesure la part
d'énergie qui tombe SUR la série harmonique : elle passe de plus de 0,75 à
rapport entier (2,0) à nettement moins à rapport irrationnel — c'est le rapport
qui décide, pas le hasard d'un filtre. Le second vérifie que le métal S'EN VA
avant la note : l'indice descend trois fois plus vite que l'amplitude, si bien
que la frappe est métallique à l'attaque et se referme sur son fondamental. Un
banc de modes fixes ne peut pas faire cela, et c'est ce qui sépare cette machine
de `vsm.perc`.

**UN CHOIX DE JUSTESSE, ÉCRIT PLUTÔT QUE CACHÉ** : les charlestons ne sont PAS
en FM mais en bruit filtré. Une charleston FM sonne comme une cloche courte, ce
qui n'est pas ce qu'on attend de la pièce qui marque le temps.

**ET UN DÉFAUT DU BANC D'EMPREINTES, TROUVÉ PAR CETTE MACHINE.** Le banc joue
une phrase MÉLODIQUE ou une phrase de BATTERIE selon une liste tenue à la main,
`isDrumMachine`, qui avait dérivé : elle ne contenait que la TR-808, la TR-909
et le sampler. `vsm.drums` et `vsm.perc` recevaient donc une gamme et n'ont
sonné que par chance — leurs pièces tombent sur des notes que la gamme traverse.
`vsm.fmdrums`, dont les pièces sont entre 36 et 49, a rendu du **silence** : son
empreinte de référence était une suite de zéros, c'est-à-dire une empreinte
qu'aucune régression ne peut faire échouer, et elle passait tous les tests
puisque deux silences sont toujours égaux.

Trois correctifs, et le troisième est le seul qui empêche que ça se reproduise :
la liste couvre désormais les cinq boîtes de kit ; une **phrase de percussions**
a été ajoutée pour `vsm.perc`, dont les congas et les claves ne sont ni des
notes de gamme ni des notes de kit (le garde-fou l'a révélée muette elle aussi,
juste après) ; et **une empreinte muette est maintenant une ERREUR**, refusée à
l'écriture comme à la comparaison, avec un message qui dit où chercher. Les
empreintes de `vsm.drums`, `vsm.perc` et `vsm.fmdrums` ont été régénérées : ce
qu'elles mesurent a changé, et c'est un changement assumé.

---

## 40. `vsm.vocal` — la dernière case nommée du tableau de couverture

**LE § 1 L'AVAIT ÉCRIT, ET C'ÉTAIT LA DERNIÈRE.** « Chaque source a une machine
qui la MODÉLISE, **sauf la voix**, qui est reportée telle quelle et présentée
comme telle. » C'était la dernière case atteignable du tableau — celle des bois
coniques ayant été mesurée hors de portée quatre fois (§ 33 et `ConeSynth.h`).

Reporter la voix reste le bon choix pour reconstruire un disque : une voix
humaine n'est pas synthétisable à l'identique, et la chaîne le dit. Mais « pas à
l'identique » n'est pas « pas du tout » : un chœur, une nappe vocale, un pad qui
prononce une voyelle sont d'un usage courant, et le parc n'en produisait aucun.

**LE TRAIT DISTINCTIF EST LA DÉFINITION MÊME D'UNE VOIX.** Les résonances du
conduit vocal — les FORMANTS — ne suivent pas la note chantée : un même « a » à
110 Hz et à 220 Hz a son premier formant au même endroit, vers 730 Hz. C'est ce
qui fait qu'on reconnaît la voyelle indépendamment de la hauteur. Aucune machine
du parc ne sait faire cela : un filtre soustractif n'a qu'UNE résonance, qui
suit le clavier ou ne le suit pas, et ne peut pas tenir trois pics à des
fréquences absolues pendant que le fondamental se déplace. Le test joue la même
voyelle à une octave d'écart et vérifie qu'aux deux hauteurs l'énergie est dans
la bande 600–900 Hz, et non dans la bande 1250–1700 où une transposition
l'aurait mise.

**LE MODÈLE : SOURCE-FILTRE**, celui de la phonétique depuis 1960. Une source
glottique — une impulsion dont la largeur se règle, plus du souffle — traverse
trois résonateurs accordés sur les formants de la voyelle. Cinq voyelles, et le
réglage passe de l'une à l'autre EN CONTINU, en suivant le trapèze vocalique.
Un second test le vérifie dans les deux sens : le « a » concentre son énergie
entre 600 et 1200 Hz là où le « i » la met à 270 et 2290.

**DEUX ERREURS DE MESURE DE MA PART, ÉCRITES DANS LE TEST PARCE QU'ELLES SE
REFERAIENT.** La première version cherchait « la fréquence du maximum » dans une
bande. Or le spectre d'une voix est un PEIGNE d'harmoniques espacées de f0 : un
formant n'y apparaît pas comme un maximum à sa propre fréquence, mais comme un
renforcement de l'harmonique la plus proche — et cette harmonique change quand
la note change. Le test trouvait donc un écart de plus de cent hertz entre deux
octaves alors que le formant n'avait pas bougé d'un seul. **L'énergie d'une
bande, elle, est insensible à la position des dents du peigne.** La seconde
erreur était de comparer une bande haute à elle-même pour séparer « a » et
« i » : le troisième formant du « a » (2440 Hz) et le second du « i » (2290 Hz)
y tombent tous les deux, et cette bande ne sépare rien. Ce qui sépare est le
RAPPORT entre une bande basse et une bande haute.

Une troisième assertion a été écrite puis RETIRÉE, et c'est écrit dans le test :
comparer le rapport de deux bandes d'une hauteur à l'autre revenait à mesurer la
chance qu'une harmonique tombe dans une fenêtre de 450 Hz. Elle aurait fait
échouer le test pour une raison sans rapport avec ce qu'il prétend vérifier.

**CE QUE CETTE MACHINE N'EST PAS**, et c'est écrit dans son en-tête : elle chante
des voyelles, elle ne PARLE pas. Les consonnes demandent des transitoires et des
occlusions, c'est-à-dire un modèle de geste et non de conduit. Trois formants au
lieu des cinq utiles ; pas de nasalité, qui exigerait un anti-formant. Statut
« dérivé » : les fréquences viennent des tables de phonétique publiées, aucune
n'a été relevée sur un chanteur.

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

**Un défaut du COMPOSANT trouvé par une nouvelle façade, et il touchait huit
anciennes.** Sur un châssis de bois, les joues latérales sont peintes par-dessus
la façade ; le nom de la machine, lui, était aligné sur le bord droit du
composant — il passait donc DESSOUS et se faisait couper. Visible d'un coup sur
`vsm.westcoast`, où « West Coast » se lisait « West Coa » ; discret mais présent
sur les huit autres façades de bois, dont le nom mordait sur la joue. La marge
tient désormais compte de la joue, et l'ellipse est autorisée : un nom trop long
doit se terminer par des points de suspension, pas disparaître dans le décor.
Les huit aperçus concernés ont été régénérés.

**Et il faut vraiment REGARDER, la liste de contrôle le dit.** Sur la façade de
`vsm.additive`, l'aperçu a montré ce qu'aucun test ne pouvait voir : un
potentiomètre sérigraphié « PARTIALS » posé juste à côté d'un bloc intitulé
« PARTIALS », qui se lit comme une erreur avant de se lire comme une commande.
Le bouton s'appelle « COUNT » depuis. Au passage, la régénération a révélé que
l'aperçu de `vsm.multisample` n'avait jamais été commité : les vingt-six
machines à façade ont désormais leur image, et aucune des vingt-quatre
existantes n'a bougé d'un pixel — ce qui confirme que le rendu est déterministe.

### Les deux façades sans original : `vsm.perc` et `vsm.additive`

Les machines du parc qui copient un objet réel ont une contrainte claire : les
gestes doivent être là où l'original les met. Ces deux-là n'ont pas d'original,
et la question devient « qu'est-ce que le musicien cherche en premier ? ».

**`vsm.perc`** suit ce qu'un percussionniste a devant lui : les peaux à gauche,
du grave à l'aigu (conga, bongo, timbale), les bois et les métaux à droite,
les grains secoués au bout — l'ordre dans lequel on les frappe, pas l'ordre
alphabétique. Trois familles, trois couleurs (terre cuite, miel, acier), et un
châssis de BOIS plutôt que la tôle pliée des boîtes Roland : cette machine
n'est pas de cette famille, et sa façade doit le dire avant qu'on ait lu une
sérigraphie. Elle garde la grille de pas — on programme une clave comme on
programme un charleston — avec ses treize lignes en numérotation General MIDI,
rangées dans l'ordre de la façade.

**`vsm.additive`** est disposée autour d'une absence : **il n'y a pas de
coupure**. Sur un soustractif, l'œil va d'abord au gros potentiomètre de
filtre ; ici, le geste central est la PENTE du spectre, qui joue exactement son
rôle, et elle est donc seule et en grand, à gauche. Le second bloc porte les
deux commandes qu'aucune autre machine du parc ne peut offrir — la balance
impairs/pairs, qui creuse un spectre à trous, et la raideur, qui étire les
rangs — et il est marqué d'un liseré différent : ces deux-là SONT la machine,
les noyer parmi des réglages d'enveloppe l'aurait déguisée en soustractif de
plus.

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

### État : les vingt-trois machines ont leur façade

| Machine | Forme de la façade |
|---|---|
| String | trajet PHYSIQUE du signal (EXCITATION -> ARCHET -> CORDE -> CAISSE -> SORTIE) ; aucune machine d'origine à copier, voir §32 |
| Piano | même principe : MARTEAU -> CORDES -> ÉTOUFFOIR -> TABLE -> SORTIE, voir §33 |
| Drums | une colonne par PIÈCE et grille de 16 pas, comme les TR du parc -- c'est bien la disposition d'origine ici |
| Wind | le trajet du souffle : EMBOUCHURE -> PERCE -> ARTICULATION -> VIBRATO -> SORTIE |
| Sampler | une colonne par PIÈCE NOMMÉE (« 1 KICK », « 3 HH CL »...), grille de 16 pas ; les réglages de mapping sont déclarés omis — c'est la boîte à rythmes générique du parc, voir §30 |
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
| E-Piano | trois blocs d'un piano de scène : VOICING (marteau, cloche, lames), PICKUPS (drive, touch), préampli à curseurs avec trémolo |
| OB-X | curseurs sur noir aux liserés crème et ambre, trajet CONTROL -> OSC -> FILTER, bloc MANUAL (unisson, détune), commutateur 2/4 pôles |
| Supersaw | le désaccord et le mélange en gros boutons -- les deux commandes qui font le son |
| Wavetable | table et position en gros boutons, enveloppe d'onde dédiée à côté du filtre |
| PCM Hybrid | attaque PCM à gauche, bloc STRUCTURE (ring mod) au centre, partiel synthétique et TVF/TVA à droite -- la lecture du D-50 |
| Tonewheel | neuf tirettes VERTICALES numérotées par longueur de tuyau, percussion, vibrato, cabine rotative |
| Generic Synth | synoptique gris neutre, signal de gauche à droite -- la seule façade du parc qui ne reproduit AUCUN instrument, voir §31 |

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
