# Vintage Synth MIDI Studio

Séquenceur MIDI + rack de synthétiseurs vintage virtuels. Voir
[`ARCHITECTURE.md`](ARCHITECTURE.md) pour la conception complète et l'état
d'avancement détaillé par phase.

**État actuel** : le moteur MIDI (`core/`, 158 tests) et le moteur audio
temps réel (`audio/`, 860 tests, dont un test de concurrence réel vérifié
sous ThreadSanitizer) sont implémentés et **entièrement testés** — **1 279
tests moteur**, tous verts, zéro warning. Les 39 machines (Minimoog, TB-303, Juno-106,
TR-808, TR-909, SH-101, Prophet, Jupiter-8, ARP Odyssey, MS-20, DX7, sampler
16 emplacements, e-piano, OB-X, supersaw, table d'ondes, hybride PCM, orgue à
roues phoniques, Generic Synth, String — corde pincée et frottée par guide
d'ondes —, Piano — cordes frappées —, Drums — batterie acoustique modélisée —,
Wind — anche et lèvres —, Multisample — l'acoustique reportée par
échantillons, profils installables —, Vocal — conduit vocal et voyelles —,
Additive — le spectre rang par rang —, West Coast — pliage et porte passe-bas —,
Phase Distortion — le temps déformé —, Divider — cordes électroniques —,
PSG — puce 8 bits —, Stochastic — la forme qui divague —, Percussion — peaux et
barres, modal — et FM Drums — percussions métalliques — + le synthé de test) ont
chacune une **empreinte de non-régression audio** qui fige leur rendu. **Toutes les phases
des feuilles de route sont terminées** (1 à 6 : moteur, machines, optimisation
SIMD ; 7 : interopérabilité sémantique, CLAP ; 8 à 11 : reconstruction
WAV → MIDI + patchs, voir [`docs/ROADMAP-fusion.md`](docs/ROADMAP-fusion.md)).
**L'axe « logiciel » est terminé lui aussi.** Ce paragraphe a longtemps dit que
« l'application qui les accueille est restée un démonstrateur — pas de piste
audio, pas d'enregistrement, pas de clip, et aucune sauvegarde de projet » :
plus rien de cela n'est vrai. Les phases D0 à D10 de
[`docs/ROADMAP-daw.md`](docs/ROADMAP-daw.md) sont closes — projet enregistré et
récupéré après plantage, clips, pistes audio, enregistrement MIDI et audio,
console avec bus et chaîne latérale, vue d'arrangement, exports, hôte CLAP et
VST3, navigateur, MIDI learn persistant, raccourcis configurables.

La couche `interchange/` donne à chaque paramètre de chaque machine une
identité sémantique stable (`filter.1.cutoff`...), et lit/écrit des presets
`*.synth.json` ainsi que des projets `project.json` qu'un outil extérieur — le
projet d'analyse `analyse/`, par exemple — peut produire sans rien connaître
du code du DAW. Exemples de fichiers dans [`docs/examples/`](docs/examples).

## Façades « façon hardware »

**Les vingt-quatre machines** ont leur propre façade, avec la disposition de
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

### Ce que la chaîne juge, et dans quel ordre

La recherche de patch travaille sur **une note** — la plus longue du stem —
parce que chercher sur chacune coûterait des heures. Mesuré, ce critère ne
suffit pas à CHOISIR une machine : sur la basse de *Children*, un `vsm.piano`
avec son patch d'usine bat de 39 % le `vsm.generic` réglé sur mesure que la
recherche retenait. La chaîne enchaîne donc trois verdicts, du plus étroit au
plus large, et chacun peut défaire le précédent :

1. **la note** choisit un patch, par machine (`--iterations`) ;
2. **la piste entière** choisit la machine, puis règle son patch — patchs
   cherchés et patchs d'usine remis en concurrence (`--budget-piste`) ;
3. **le mélange** tranche en dernier : une amélioration de piste n'est gardée
   que si elle rapproche le morceau, parce que les stems d'une séparation ne se
   rendorment pas exactement dans l'original.

Chaque étape se désactive (`--sans-arbitrage`, `--sans-reglage-piste`) : c'est
ainsi qu'on attribue un écart à une étape et non à un ensemble. `--stems`
reprend des stems déjà séparés pour ne pas repayer la séparation à chaque
mesure. Le détail, les chiffres et les impasses : `ARCHITECTURE.md` § 34.

**La batterie concourt aussi.** Le stem de batterie est découpé en kit, puis
rejoué par `vsm.drums` (modélisée) ET par les boîtes à rythmes du parc (TR-909,
TR-808), la piste entière tranche, toutes celles à portée sont réglées, la
meilleure réglée prend la piste et les autres réglées restent en jeu au verdict
du mélange — avec leurs propres notes, puisque chaque boîte a les siennes. Sur
un morceau de techno de 1993, faire entrer les boîtes dans la course a fait
passer la distance globale de 0,4088 à 0,2490 — et il a fallu une oreille pour
remarquer qu'elles n'y étaient pas (`--sans-arbitrage-batterie` rend l'ancien
comportement).

**La métrique se choisit, et s'inscrit.** `--metrique v2` (défaut) ou `v3`, qui
ajoute un terme de HAUTEUR pour les sons graves — sans lui, un kick réel à 59 Hz
était jugé plus proche d'un 808 à 30 Hz qu'à 60. Deux métriques ne se comparent
pas ; chaque `rapport.json` porte la sienne, son budget, sa `provenance`
(commit, options, modèles consultés), la batterie (`drums` : pièces, arbitrage,
réglages) et le verdict du mélange (`mixVerdict` : gardé, écarté, chiffres).

### La séparation tourne sur l'iGPU (2,8x), et rend les mêmes stems

La séparation (demucs) est la seule étape de la chaîne qui sache utiliser un
accélérateur. `analyse/analyzer/separation.py` choisit `xpu`, puis `cuda`, puis
`cpu`, et **imprime au journal celui qu'il a retenu** — la ligne `device :` est
le seul moyen de savoir, en relisant une exécution, sur quoi elle a tourné.

Mesuré sur les 5 min de *Clair de Lune*, même modèle, mêmes options `shifts=0`,
même environnement, sur un iGPU **Intel Arc** (Meteor Lake) :

| dorsal | séparation | stems |
|---|---|---|
| CPU (16 fils) | 89,6 s | référence |
| **XPU (Intel Arc)** | **31,7 s** | corrélation 1,000000, écart maximal 4,2e-07 |

Le gain n'est pas payé par un autre résultat : l'écart est l'arrondi du
`float32`. L'identité des stems a été vérifiée AVANT la durée, parce qu'une
séparation deux fois plus rapide qui rendrait d'autres stems ne serait pas une
accélération, ce serait une autre chaîne.

**Ce que le GPU n'accélère pas, et c'est l'essentiel du temps.** La recherche de
patch rend l'audio par `vsm-render`, le moteur C++ du DAW, qui ne va pas sur GPU.
Sur *Sky and Sand* (8 min 52, quatre stems), la séparation pesait ~6 minutes sur
49. Attendez-vous à quelques minutes gagnées par morceau, pas à une
reconstruction trois fois plus rapide.

**L'installation est entièrement en espace utilisateur** — aucun paquet système,
aucun `sudo` : les paquets pip d'Intel (`intel-opencl-rt`, `intel-sycl-rt`,
`dpcpp-cpp-rt`) embarquent le runtime Level Zero.

```bash
analyse/.venv/bin/python -m pip install --index-url https://download.pytorch.org/whl/xpu \
    "torch==2.13.0+xpu" "torchaudio==2.11.0+xpu"
analyse/.venv/bin/python -c "import torch; print(torch.xpu.is_available())"   # True attendu
```

**Le suffixe `+xpu` est obligatoire.** Sans lui, pip voit la même version
`2.13.0` que la variante CUDA déjà installée, ne fait rien, et la séparation
retombe sur le CPU sans le dire autrement que par sa ligne `device :`. C'est
aussi pourquoi `analyse/requirements.txt` ne règle pas la question tout seul :
il installe la variante par défaut de PyPI, qui est celle de CUDA. **Refaire le
venv depuis `requirements.txt` défait donc cette bascule**, silencieusement.

**Sur une machine sans matériel Intel**, rien à faire : `choisir_device()`
retombe sur `cuda` s'il y a une carte NVIDIA, sinon sur `cpu`, et la chaîne rend
les mêmes résultats — plus lentement.

### Où est l'erreur ? Le budget, piste par piste (`budget_erreur.py`)

Une distance globale dit qu'on est à 0,2854 de l'original. Elle ne dit pas OÙ,
et sans ça on règle au hasard.

```bash
analyse/.venv/bin/python analyse/budget_erreur.py DOSSIER-PROJET --original MORCEAU.wav
```

La commande remplace chaque piste reconstruite par le **stem réel**
correspondant et mesure ce qu'une reconstruction PARFAITE de cette piste-là
rapporterait. Sur *Sky and Sand* : batterie **−63,5 %**, basse −0,0 %, voix
−0,0 %, `other` +2,5 % (pire). Presque toute l'erreur est dans une seule piste,
et deux des quatre sont hors de cause — ce qu'aucun chiffre global ne dit.

Elle donne aussi le **plancher** : la somme des stems séparés est à 0,0551 de
l'original (le silence, lui, est à 0,9544), donc la séparation ne perd presque
rien et n'est pas ce qui limite la chaîne. Et elle imprime son propre
**contrôle** : la somme des pistes rendues séparément doit redonner la distance
du rapport, sinon la décomposition ne décrit pas le morceau qu'on croit.

Ce qu'elle ne dit pas : COMMENT réparer la piste qu'elle désigne. Sur ce
morceau, la route qui semblait évidente — rejouer les coups découpés dans
l'enregistrement (`--batterie-echantillonnee`) plutôt qu'une boîte modélisée —
s'est révélée pire de 41 %, parce qu'elle découpe un seul échantillon par
famille et le rejoue des centaines de fois.

### Apprendre les sonorités du parc (`docs/CDC-apprentissage.md`)

Le moteur fabrique un corpus étiqueté pour rien : des milliers de paires
(réglages → son), déterministes, sans droit tiers. Trois commandes, et chacune
publie son coût et sa mesure :

```bash
analyse/.venv/bin/python analyse/corpus.py --sortie corpus/          # A0 : ~27 min par machine à 10 000 patchs
analyse/.venv/bin/python analyse/classifieur.py --corpus corpus/ --sortie modeles/classifieur.joblib   # A1
analyse/.venv/bin/python analyse/classifieur_batterie.py --sortie modeles/frappes.joblib             # A2
analyse/.venv/bin/python analyse/banc_batterie.py [--classifieur-batterie modeles/frappes.joblib]  # le juge de la batterie, rejouable
analyse/.venv/bin/python analyse/corpus_separe.py --stems ... --sortie corpus/separe/separe.npz --sonde ...  # le corpus passé par demucs, et sa mesure
analyse/.venv/bin/python analyse/tests/run.py                        # les tests Python, sans dépendance
```

Les modèles sont des **conseillers** : ils ne produisent jamais une seconde
d'audio, et ils sont REFUSÉS au chargement si le son d'une machine a changé
depuis leur entraînement (empreintes rejouées, pas supposées — les deux
modèles, le classifieur de frappes compris). `corpus/` et `modeles/` sont
ignorés par git : ils se refont à l'identique.

- `--classifieur MODÈLE` : l'avis du classifieur de machine est consigné dans le
  rapport, à côté de la mesure — jamais à sa place. Mesuré : il reconnaît le
  moteur (99,9 % de bonne machine dans le top 3) et pas un disque (rang médian
  16/20 pour la gagnante réelle sur un piano) ; `--preselection-apprise N` le
  laisse dégrossir quand même, et l'aide dit que la mesure ne le recommande pas.
- `--classifieur-batterie MODÈLE` : le classifieur de frappes décide, attaque
  par attaque, quelles pièces frappent. Mesuré au banc (trois motifs) : caisse
  claire sous le kick 8/8 au lieu de 0/8, charleston seule 15/16 au lieu de
  8/16, claps 8/8 et toms 7/8 au lieu de 0, aucune frappe perdue.
- `--sans-apprentissage` : le témoin, aucun modèle consulté.

`corpus_separe.py` n'entraîne rien qui serve dans la chaîne : c'est l'épreuve
de la seule piste que deux mesures laissaient debout contre le fossé de domaine
— rendre le patch, le mélanger à de vrais stems, repasser le tout par demucs,
étiqueter avec le patch d'origine. Mesuré : un modèle entraîné sur ce corpus
lit mieux un STEM DE BASSE réel (rang médian 14 → 4) et pas mieux un disque
(trois sondes sur cinq reculent) ; il apprend les artefacts de la séparation,
qui sont dedans, et pas la distance entre un vrai instrument et le parc, qui
n'y est pas. Le tableau et ce qu'il ferme : la feuille de route.

Les résultats, les chiffres et ce qui a été rejeté :
[`docs/ROADMAP-apprentissage.md`](docs/ROADMAP-apprentissage.md).

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
substitution silencieuse. **Un rendu réussi peut donc être un rendu amputé** :
le code de sortie vaut 0 et la ligne « n/n piste(s) sonorisée(s) » est le seul
endroit qui le dise.

**Le taux d'échantillonnage fait partie des conditions d'une mesure.** Par
défaut ce rendu est à 48 000 Hz, quand la chaîne d'analyse travaille à 44 100 :
comparés tels quels, deux rendus du MÊME projet donnent une corrélation de
0,0002, et à taux égal ils sont identiques à l'échantillon près. Pour rejouer
une mesure de `reconstruire.py`, passer `--sample-rate 44100`.

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
- **Annuler/rétablir** jusqu'à 128 pas, chaque geste (y compris un glissement
  continu) comptant pour une seule action. L'annulation porte sur le **projet
  entier** : elle défait aussi bien une note qu'un réglage de mixage, un effet
  inséré, une piste ajoutée ou supprimée, un repère posé — et elle survit au
  changement de piste, ce qui n'était pas le cas quand elle ne mémorisait que
  les notes de la piste affichée.
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
- **Notes douteuses** : un projet reconstruit porte la confiance de la
  transcription note par note, et celles qui passent sous le seuil sont
  marquées d'un liseré ambre. La touche **D** mène à la suivante, **Maj+D** à la
  précédente (la vue défile jusqu'à la note sans changer le zoom, et fait le
  tour du morceau arrivée au bout) ; *Sélection ▸ Toutes les notes douteuses*
  les prend d'un coup ; la barre d'état affiche combien il en reste.

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

## Écouter l'original en regard de la reconstruction

*Fichier ▸ Charger l'original (référence A/B)* ajoute l'enregistrement de départ
comme piste de référence, et le menu permet de passer à volonté de la
reconstruction seule aux deux ensemble, puis à l'original seul. La référence est
rééchantillonnée si besoin, elle passe après le bus master (le traitement de la
reconstruction ne doit pas colorer le modèle) et **elle ne part jamais dans
l'export**.

Pendant l'écoute, la barre de transport **dit ce qu'on entend** (*Écoute :
reconstruction / les deux / original*, en ambre dès que l'original est
audible) ; un clic sur ce bouton, ou la touche **R** depuis n'importe quelle
fenêtre, passe au mode suivant — on compare en regardant le piano roll, pas
le menu.

Formats acceptés : **WAV, AIFF, FLAC, Ogg Vorbis et MP3**. Le décodage a lieu
dans la couche interface, avec ce que JUCE apporte déjà : le moteur audio garde
ses zéro dépendance et ne connaît toujours que le WAV. Pour vérifier ce que
l'application fera d'un fichier, sans lancer de fenêtre :

```bash
cmake --build build --target vsm-audio-import-check
./build/app/vsm-audio-import-check_artefacts/RelWithDebInfo/vsm-audio-import-check morceau.mp3
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
- [`docs/CDC-apprentissage.md`](docs/CDC-apprentissage.md) — apprendre à
  reconnaître les sonorités du parc : corpus engendré par le moteur,
  classifieur de machine, gabarits de batterie, estimateur de paramètres. Les
  modèles CONSEILLENT ; ils ne produisent jamais une seconde d'audio.
- [`docs/ROADMAP-apprentissage.md`](docs/ROADMAP-apprentissage.md) — le
  découpage en phases A0 à A5, du gain le plus sûr au plus risqué.
- [`docs/ROADMAP-daw.md`](docs/ROADMAP-daw.md) — **l'axe ouvert le plus
  récemment** : ce qui manque au logiciel lui-même pour soutenir la comparaison
  avec Cubase, Ableton Live et FL Studio. Les feuilles de route précédentes
  disent ce que le programme entend et ce qu'il produit ; celle-ci dit ce qu'on
  peut y faire — et commence par les huit fonctions qui existent aujourd'hui en
  donnant un résultat faux sans le dire.
- [`docs/CDC-multisample.md`](docs/CDC-multisample.md) — `vsm.multisample` :
  l'acoustique mélodique par report d'échantillons, le format de profil, et
  l'installation d'une banque libre.

### Installer un profil de piano

Aucune banque d'échantillons n'est commise dans le dépôt : elles s'installent.

```bash
analyse/.venv/bin/python tools/installer-profil-piano.py --banque salamander
```

L'outil télécharge Salamander Grand Piano V3 (CC-BY 3.0, 412 Mo), **vérifie son
empreinte SHA-256**, en extrait un sous-ensemble tenant dans le budget mémoire,
écrit le profil et son fichier d'attribution. Sans profil installé,
`vsm.multisample` est silencieuse et la chaîne d'analyse l'écarte de ses
candidates en le disant — elle ne rend jamais un son de repli.

### Installer un instrument depuis une banque SoundFont

```bash
cmake --build build --target vsm-sf2
./build/tools/vsm-sf2 --lister BANQUE.sf2
./build/tools/vsm-sf2 --convertir BANQUE.sf2 --programme 40 --duree-max 6
```

Le premier appel liste les presets (banque, programme, nom) ; le second en
convertit un en profil. L'outil **imprime les générateurs SF2 qu'il n'applique
pas** — filtre, LFO, enveloppes de modulation : cette machine lit des
échantillons, elle ne synthétise pas — et **refuse de convertir une banque dont
la licence n'est écrite ni dans le fichier ni sur la ligne de commande**.

La conversion se fait une fois, à l'installation : le DAW ne charge jamais qu'un
profil, jamais un SF2.

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
