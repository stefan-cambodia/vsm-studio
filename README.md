# Vintage Synth MIDI Studio

Séquenceur MIDI + rack de synthétiseurs vintage virtuels. Voir
[`ARCHITECTURE.md`](ARCHITECTURE.md) pour la conception complète et l'état
d'avancement détaillé par phase.

**État actuel** : le moteur MIDI (`core/`, 81 tests) et le moteur audio
temps réel (`audio/`, 527 tests, dont un test de concurrence réel vérifié
sous ThreadSanitizer) sont implémentés et **entièrement testés** — 725
tests, tous verts, zéro warning. Les 23 machines (Minimoog, TB-303, Juno-106,
TR-808, TR-909, SH-101, Prophet, Jupiter-8, ARP Odyssey, MS-20, DX7, sampler
16 emplacements, e-piano, OB-X, supersaw, table d'ondes, hybride PCM, orgue à
roues phoniques, Generic Synth, String — corde pincée et frottée par guide
d'ondes —, Piano — cordes frappées —, Drums — batterie acoustique modélisée —,
Wind — anche et lèvres — + le synthé de test) ont chacune une
**empreinte de non-régression audio** qui fige leur rendu. **Toutes les phases
des feuilles de route sont terminées** (1 à 6 : moteur, machines, optimisation
SIMD ; 7 : interopérabilité sémantique, CLAP ; 8 à 11 : reconstruction
WAV → MIDI + patchs, voir [`docs/ROADMAP-fusion.md`](docs/ROADMAP-fusion.md)).
La couche `interchange/` donne à chaque paramètre de chaque machine une
identité sémantique stable (`filter.1.cutoff`...), et lit/écrit des presets
`*.synth.json` ainsi que des projets `project.json` qu'un outil extérieur — le
projet d'analyse `analyse/`, par exemple — peut produire sans rien connaître
du code du DAW. Exemples de fichiers dans [`docs/examples/`](docs/examples).

## Façades « façon hardware »

**Les vingt-trois machines** ont leur propre façade, avec la disposition de
l'original : trajet du signal du Minimoog, rangée unique du TB-303, colonne
par pièce des TR-808/909 et du sampler-boîte à rythmes, curseurs du Juno-106,
du Jupiter-8, du SH-101 et de l'ARP Odyssey, double filtre du MS-20, bloc
Poly-Mod du Prophet, matrice des six opérateurs du DX7, tirettes de l'orgue à
roues phoniques, colonne par pièce de la batterie acoustique -- et, pour les
machines de modélisation physique qui n'ont jamais eu de façade canonique
(String, Piano, Wind), le trajet du signal lui-même, faute d'original à
imiter. Aperçus :
[`docs/images/panels/`](docs/images/panels).

Le **séquenceur à pas** des machines qui en ont un (TR-808, TB-303) est
intégré à la façade : clic pour allumer un pas, Maj+clic pour l'accentuer,
Alt+clic pour un slide, molette pour la hauteur sur un motif mélodique. La
grille édite directement les notes de la piste — c'est la même musique que
celle du piano roll, vue autrement. Seul le synthé de test garde le panneau
générique (un potentiomètre par paramètre), qui reste aussi le filet de
sécurité pour les plugins CLAP tiers.

Pour regarder une façade sans lancer l'application :

```bash
cmake --build build --target vsm-panel-preview
./build/app/vsm-panel-preview_artefacts/RelWithDebInfo/vsm-panel-preview /tmp/apercus 1200
```

## Utiliser les machines dans un autre logiciel (CLAP)

Les machines VSM s'exportent en plugin CLAP, et le moteur sait à l'inverse
charger un plugin CLAP tiers. Cette couche est **désactivée par défaut** : elle
est la seule à nécessiter un téléchargement (le SDK CLAP), alors que tout le
reste du dépôt se compile hors ligne.

```bash
cmake -B build -DVSM_BUILD_CLAP=ON
cmake --build build --target vsm_clap_adapter
# -> build/clap/vsm-instruments.clap, à copier dans ~/.clap
```

L'adaptateur enveloppe le moteur natif au lieu de le réimplémenter : un même
patch joué dans un hôte CLAP et dans l'application produit des échantillons
identiques (c'est vérifié par test). Côté hôte, un plugin chargé est présenté
au moteur comme une machine native — ni le graphe audio ni l'interface n'ont
de code spécifique à CLAP.

## Piloter le moteur depuis Python (`analyse/`)

Le projet d'analyse (`analyse/`) reconstruit un son : il sépare, transcrit,
puis cherche les réglages qui reproduisent l'audio d'origine. Il rend
désormais ses candidats avec le **moteur réel** plutôt qu'avec une
approximation, si bien que le patch trouvé est directement jouable dans le DAW
et dans un hôte CLAP.

```python
from analyzer.vsm_engine import VsmEngine

with VsmEngine(sample_rate=44100) as engine:
    audio = engine.render_note("vsm.tb303",
                               {"filter.1.cutoff": 700.0, "filter.1.resonance": 0.8},
                               midi_note=45, duration=1.0)
```

Un rendu coûte ~10 ms, avec un audio identique au bit près pour deux requêtes
identiques. Détails et mesures : [`analyse/PONT-VSM.md`](analyse/PONT-VSM.md).

## Rendre un projet sans interface (`vsm-render`)

Un dossier de projet (`project.json` + `midi/` + `instruments/`) se rend en
WAV en une commande, sans carte son ni fenêtre — c'est ainsi qu'un outil
extérieur pilote le moteur :

```bash
cmake --build build --target vsm-render
./build/tools/vsm-render docs/examples/demo-project sortie.wav --tail 1.5
# rendu 3.32692 s (159692 échantillons), 2/2 piste(s) sonorisée(s), pic 0.576647
```

Le rendu emprunte exactement le même chemin de calcul que la lecture dans
l'application, et il est déterministe : deux appels sur les mêmes fichiers
produisent deux WAV identiques octet pour octet. Un instrument ou un preset
manquant n'interrompt pas le rendu mais apparaît en avertissement — jamais de
substitution silencieuse.

Boucle typique côté Python : écrire/modifier `instruments/track_00.synth.json`
(les paramètres y portent des noms stables comme `filter.1.cutoff`), appeler
`vsm-render`, analyser le WAV, recommencer. L'interface (`app/`, JUCE) compile, tourne et
est câblée sur le moteur audio (AudioEngine JUCE, Synth Rack, Mixer,
éditeur d'effets, automation, export WAV).

## Compiler le moteur (`vsm_core` + `vsm_audio`) — testé, ne nécessite que CMake + un compilateur C++20

```bash
cmake -B build -DVSM_BUILD_TESTS=ON
cmake --build build -j
./build/core/vsm_core_tests
./build/audio/vsm_audio_tests
./build/interchange/vsm_interchange_tests
```

Aucune dépendance externe : ça doit fonctionner tel quel sur Linux/macOS/
Windows avec n'importe quel compilateur récent (GCC 11+, Clang 14+, MSVC
2022+).

## Mesurer les performances (banc CPU, Phase 6)

Cible optionnelle (désactivée par défaut) qui mesure le coût par bloc de
chaque machine, de chaque effet et du graphe complet, à 48 kHz / 512
échantillons :

```bash
cmake -B build-bench -DVSM_BUILD_BENCH=ON -DVSM_BUILD_TESTS=OFF -DVSM_BUILD_APP=OFF
cmake --build build-bench -j
taskset -c 2 ./build-bench/audio/vsm_audio_bench   # épingler le cœur : voir plus bas
```

Sur un CPU **hybride** (Intel Core Ultra, Apple Silicon, ARM big.LITTLE),
épinglez le processus sur un cœur : sans ça, le thread migre entre cœurs P
et E et les chiffres varient d'un facteur 2 sans qu'aucune ligne de code
n'ait changé. Comparez la colonne `min` entre deux versions du code.
Détails et relevé de référence dans `ARCHITECTURE.md`, section 9 ter.

## Compiler l'application desktop (`app/`, JUCE)

Nécessite une connexion réseau (le SDK JUCE est récupéré automatiquement
via `FetchContent` depuis GitHub) ainsi que les bibliothèques système
habituelles pour le développement audio/GUI :

- **Linux** : `libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libfreetype-dev libcurl4-openssl-dev`
- **macOS** : Xcode Command Line Tools
- **Windows** : Visual Studio 2022 (charge de travail "Développement Desktop en C++")

```bash
cmake -B build -DVSM_BUILD_APP=ON
cmake --build build --target VintageSynthMidiStudio -j
```

> **Important** : si vous avez déjà un dossier `build/` configuré (par
> exemple en ayant lancé la commande ci-dessus sans `-DVSM_BUILD_APP=ON`),
> faites `rm -rf build` avant de relancer avec `-DVSM_BUILD_APP=ON`. JUCE
> embarque des sources C (Sheenbidi), et CMake ne réévalue pas fiablement
> l'ajout du langage C sur un dossier de build déjà configuré en C++ seul —
> voir ARCHITECTURE.md section 3 pour le détail de ce piège.

Le premier build est long (compilation de JUCE). Les suivants sont
incrémentaux.

> Si vous utilisez **Claude Code** en local, il peut prendre le relais ici :
> câbler `ProcessGraph` (déjà testé dans `audio/`) dans un `AudioEngine`
> JUCE, et enchaîner sur la Phase 3 (premières émulations vintage) — ce
> dépôt est structuré pour ça.

## Le piano roll

L'éditeur de notes couvre ce qu'on attend d'un séquenceur moderne :

- **Outils** (touches 1-6) : sélection/déplacement, dessin, gomme (y compris
  en balayant), ciseaux, colle, muet.
- **Annuler/rétablir** illimité jusqu'à 128 pas, chaque geste (y compris un
  glissement continu) comptant pour une seule action.
- **Édition musicale** : quantifier (force réglable, swing, début et/ou fin),
  humaniser, legato, retirer les chevauchements, couper/fusionner, rétrograder,
  miroir des hauteurs, durées x2 / ÷2 / = grille, transposer au demi-ton ou à
  l'octave.
- **Vélocité** : lane dédiée où l'on peint les nuances à la souris, tracé en
  ligne droite (Maj) pour un crescendo régulier, valeurs fixes, mise à
  l'échelle, aléatoire reproductible.
- **Gammes et harmonie** : 14 gammes, surlignage des degrés hors gamme,
  contrainte d'une sélection à la gamme, insertion de 13 types d'accords,
  arpèges (montant, descendant, aller-retour, aléatoire reproductible).
- **Écoute** : cliquer une touche du clavier, ou dessiner une note, la fait
  sonner tout de suite sur l'instrument de la piste — même à l'arrêt. Un
  clavier MIDI branché joue la piste sélectionnée.
- **Navigation** : zoom souris/clavier, « tout voir », zoom sur la sélection,
  barres de défilement, suivi de la tête de lecture, règle cliquable
  (Maj + glissé = région de boucle), notes fantômes des autres pistes.
- **Notes muettes** : rendues silencieuses sans être supprimées (affichées
  hachurées) ; ni jouées ni exportées.

Toutes ces opérations vivent dans `core/` (testées sans interface graphique) et
sont accessibles au clavier, par la barre d'outils, par clic droit et par le
menu Édition — qui est littéralement le même menu.

### Taille de l'interface

*Affichage ▸ Taille de l'interface* propose 100, 125, 150, 175 et 200 %, et
démarre à **150 %** — les tailles d'origine (9 à 12 points) se sont révélées
trop petites à lire. Le
facteur agrandit le texte ET les cases dans le même rapport : la mise en page
ne bouge pas et aucune légende n'est tronquée — ce qui arriverait en grossissant
la police seule, puisque les façades de machines calculent la taille de leurs
légendes d'après la hauteur de leur case. Le réglage est conservé d'une
exécution à l'autre.

Pour juger sans lancer l'application, l'aperçu hors écran accepte la même
échelle :

```bash
./build/app/vsm-panel-preview_artefacts/RelWithDebInfo/vsm-panel-preview /tmp/apercu 1100 1.5
```

## Où va le projet

Les phases 1 à 7 sont terminées (moteur, machines, interface, interopérabilité).
La suite est décrite dans [`docs/ROADMAP-fusion.md`](docs/ROADMAP-fusion.md),
écrite après la fusion avec le projet d'analyse : reconstruire un fichier WAV en
MIDI + patchs rejouables, et mesurer l'écart.

- [`docs/CDC-nouvelle-machine.md`](docs/CDC-nouvelle-machine.md) — ce qu'une
  machine doit satisfaire pour être finie (DSP, tests, identités sémantiques,
  façade, empreinte audio).
- [`docs/CDC-machines-manquantes.md`](docs/CDC-machines-manquantes.md) — ce qui
  manque pour reconstruire des enregistrements réels, et pourquoi ce ne sont pas
  d'autres machines de caractère.

## Structure du projet

```
core/          moteur MIDI/séquenceur — testé, zéro dépendance
audio/         moteur audio temps réel — testé, zéro dépendance hormis core/
interchange/   formats d'échange (Phase 7) — seule couche qui connaît JSON
app/           interface JUCE — Piano Roll, Transport, Mixer, Synth Rack
```

Le sens des dépendances est strict et vérifié : `interchange/` lit `audio/`,
jamais l'inverse. Le DAW joue, édite et exporte sans qu'une ligne de la couche
d'interopérabilité soit chargée.

Voir [`ARCHITECTURE.md`](ARCHITECTURE.md) pour le détail complet de chaque
couche, les choix technologiques (et les pièges concrets rencontrés en les
mettant en œuvre), et le plan de développement par phases.
