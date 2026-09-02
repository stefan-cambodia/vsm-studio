# VSM Studio — Addendum roadmap : Interopérabilité Python / CLAP / Interchange

**Statut :** addendum à `ARCHITECTURE.md` (référence vivante).
**Nature :** extension **orthogonale**. Elle se branche sur les interfaces déjà
présentes ; elle ne remplace ni ne réordonne la feuille de route existante.
**Langage / framework :** inchangés — C++20, JUCE pour app+UI, `vsm_core` + `vsm_audio`.

> Règle qui prime sur tout le reste de ce document :
> **Le moteur VSM reste la source de vérité du rendu. CLAP fournit
> l'interopérabilité plugin. Le format VSM fournit l'interopérabilité des
> paramètres et des projets. Python fournit l'analyse et le pilotage. Aucun de
> ces systèmes ne doit devenir une dépendance obligatoire des autres.**

---

## 0. Ce qui NE change pas (P0 — ne rien casser)

- `core/` (`vsm_core`) ne dépend jamais de l'UI, ni de Python, ni de CLAP, ni de JSON.
- `audio/` (`vsm_audio`) ne dépend jamais de l'UI, ni de Python, ni de JSON, ni du réseau.
- `AudioEngine` reste le seul point qui connaît `juce::AudioIODeviceCallback`.
- `TempoMap`, `PlaybackScheduler`, `ProcessGraph`, `OfflineRenderer` restent les
  références uniques du timing et du rendu.
- Ajouter une machine ne modifie ni `ProcessGraph`, ni `AudioEngine`, ni
  `TrackListComponent`, ni `SynthRackComponent`. Garantie **étendue** à l'ajout
  d'un adaptateur CLAP ou d'un descripteur de paramètres.
- Le chemin `process()` reste sans allocation, sans libération, sans I/O fichier,
  sans parsing JSON, sans réseau, sans log bloquant, sans mutex non justifié —
  **y compris dans le wrapper CLAP**.
- Les tests existants restent obligatoires et inchangés. Interchange et CLAP
  s'ajoutent **au-dessus**.

Interdits absolus dans le moteur : runtime Python, JSON dans le DSP, CLAP dans
`vsm_core`, réseau dans `AudioEngine`/`ProcessGraph`, dépendance à Python pour
jouer un projet, dépendance à CLAP pour les instruments natifs. **Le DAW doit
rester autonome.**

---

## 1. État actuel (mise à jour)

**Phases 1 à 6 terminées** (12 machines, 9 effets, UI complète, qualité :
profiling, SIMD, transports unifiés). La condition de démarrage de l'interop
est donc levée, et la **Phase 7 a commencé** :

| Étape | Contenu | Statut |
|---|---|---|
| **P2** | `ParameterDescriptor` + identités sémantiques | **Fait** — 308 paramètres annotés (12 machines + 9 effets), 3 tests de cohérence (complétude, unicité, sens transversal) |
| **P3** | `*.synth.json` | **Fait** — écriture/lecture versionnées, rapport d'application explicite, exemple dans `docs/examples/` |
| **P4** | `project.json` | **Fait** — transport, pistes, mixage, effets ; référence le MIDI sans recopier les notes ; chemins portables imposés ; instrument manquant signalé, jamais substitué |
| **P5** | adaptateur CLAP | **Fait** — `vsm-instruments.clap` expose les machines du registre (11 à l'époque ; l'énumération manuelle a depuis été remplacée par une interrogation du registre, après que huit machines ajoutées sont restées invisibles aux hôtes) ; enveloppe le DSP natif, rendu identique à l'échantillon près (testé) |
| **P6** | hôte CLAP | **Fait** — un `.clap` externe est chargé et présenté comme `ISynthPlugin` : ProcessGraph et l'UI inchangés |
| **P9** | API locale (Mode B) | **Fait, sous la forme la plus simple qui tienne** : `vsm-render --serve` lit des requêtes JSON ligne à ligne sur l'entrée standard. Pas de port, pas d'authentification, pas de serveur à laisser tourner -- les deux programmes sont sur la même machine. Pont Python dans `analyse/analyzer/vsm_engine.py` |
| **P7** | import d'un dossier de projet | **Fait** — `loadProjectBundle`/`saveProjectBundle` : project.json + MIDI + presets, dégradation honnête (preset ou instrument manquant signalé, jamais substitué) |
| **P8** | reconstruction hors ligne | **Fait** — `vsm-render <dossier> <sortie.wav>`, sans interface ni carte son ; rendu déterministe octet pour octet, via le même ProcessGraph que la lecture temps réel |
| **P10** | boucle d'optimisation audio → Python → DAW → rendu → Python | **Fait, et c'est `reconstruire.py`** : la chaîne d'analyse rend chaque candidate par `vsm-render` et mesure la distance au stem, plusieurs milliers de fois par morceau. Le mode `--serve` de P9 est ce qui rend ce volume tenable (machines chargées une fois). Éprouvé de bout en bout le 29/08/2026, voir le § 9 |

Les livrables se trouvent dans `interchange/` (`vsm_interchange`, 45 tests) et
`tools/vsm-render`, plus `clap/` (adaptateur et hôte, 8 tests, option
`-DVSM_BUILD_CLAP=ON`). Un dossier de projet complet et jouable sert
d'exemple : `docs/examples/demo-project/`.
La règle P0 est vérifiée : ni `core/` ni `audio/` n'incluent quoi que ce soit de
cette couche, et aucune dépendance externe n'a été ajoutée (le lecteur/écrivain
JSON est écrit dans le projet, pour que le build reste possible hors ligne).

---

## 2. Où vivront les nouvelles briques (Phase 7)

```
interchange/     ← ParameterDescriptor, SynthPreset, Project, sérialiseurs (peut connaître JSON)
clap/            ← host/ + adapter/ + common/ (SDK CLAP officiel)
app/integration/ ← ProjectImporter, PresetImporter, PythonProjectImporter
```

- `interchange/` est la **seule** couche autorisée à connaître JSON. Jamais le DSP.
- `clap/adapter/` **enveloppe** les processeurs natifs ; il ne les réécrit pas :
  `MinimoogProcessor → ProcessGraph / OfflineRenderer → (CLAP Adapter)`.
- SDK CLAP utilisé comme couche dédiée (pas greffé dans le FormatManager JUCE).

Dépendances nouvelles limitées à ces couches : **CLAP SDK** + **parseur JSON C++**.
Python, HTTP, WebSocket, gRPC restent optionnels et jamais requis pour le DAW.

---

## 3. Brique centrale : `ParameterDescriptor` et trois niveaux d'ID (P2)

À définir **avant** de figer le JSON. Un paramètre porte au minimum :
`semanticId, displayName, module, type, minimum, maximum, default, unit, flags`.

```
Semantic ID          filter.cutoff
     ↓
VSM Parameter ID     Minimoog.Filter.Cutoff
     ↓
CLAP Parameter ID    2001   (stable, unique)
```

**Python ne travaille QUE sur le `semanticId`.** Le DAW fait le mapping
`semanticId → ParameterDescriptor → paramètre VSM → CLAP`.

Vocabulaire sémantique initial (extensible) :

```
oscillator.N.{waveform,pitch,detune,level,sync}
filter.N.{type,cutoff,resonance,drive}
envelope.N.{attack,decay,sustain,release}
lfo.N.{waveform,rate,amount}
voice.{polyphony,glide,glideTime,mode}
effect.N.{type,mix,rate,depth}
```

Paramètres spécifiques machine autorisés (ex. TB-303 : `accent.amount`,
`accent.threshold`, `slide.time`). Chaque synthé/effet déclare ses paramètres,
avec un statut explicite `SUPPORTED | UNSUPPORTED | APPROXIMATED`. Une
approximation n'est jamais silencieuse.

---

## 4. Formats (P3–P4) — versionnés dès la V1

- `*.synth.json` — preset **sémantique** (`format: vsm-synth-preset`, `version: 1`).
- `project.json` — projet importable (`format: vsm-project`, `version: 1`) :
  transport, référence MIDI, pistes avec `preferredPlugin` + preset + état.
- `*.clapstate` — état natif CLAP, restauration exacte d'un plugin.
- Conservés : `*.mid`, `*.wav`. `*.clap` = plugin, **pas** projet.

```
VSMProject/
├── project.json
├── midi/arrangement.mid
├── instruments/track_NN.synth.json
└── states/track_NN.clapstate
```

Multiplateforme : pas de chemins absolus, pas de séparateurs codés en dur, pas
d'ID dépendant de l'OS/endianness. Chaque élément est versionné ; migrations explicites.

---

## 5. Authenticité & confiance

Statut par paramètre : `measured | derived | estimated | approximated | unknown`.
Une estimation IA n'est jamais présentée comme une mesure. Plugin absent à
l'import → message explicite `Missing instrument: com.vsm.xxx`, en conservant
MIDI + preset + plugin ID + version. Jamais de reconstruction silencieuse fausse.

---

## 6. Déterminisme (condition des tests Python ↔ DAW)

MIDI + preset + sample rate + état initial (+ seed) identiques ⇒ rendu
déterministe. `AnalogDrift` est déjà seedé (prolonge `DeterministicRng`/SplitMix64).
Tests ajoutés : validation `project.json`/`synth.json`, conformité **Builtin vs
CLAP** par instrument (amplitude, F0, enveloppe, filtre, timing, silences,
stabilité, tolérance documentée), import des fixtures Python sans intervention.

---

## 7. Roadmap consolidée (ordre strict)

```
P0  Ne rien casser        ← permanent
P1  Phases 1-4            ← FAIT (moteur + UI Phase 2 ; Phases 3 et 4 complètes)
        ▼
    Phase 5 — synthèses avancées (DX7, MS-20, Jupiter, ARP)
        ▼
    Phase 6 — qualité (profiling, SIMD, oversampling, anti-aliasing, tests audio/machine)
        ▼
    Phase 7 — INTEROP (cet addendum) :
P2  ParameterDescriptor + IDs sémantiques
P3  synth.json
P4  project.json
P5  CLAP adapters      → Minimoog, TB-303, puis Juno-106
P6  CLAP Host          → chargement de plugins CLAP externes
P7  Import Python      → MIDI + synth.json + project.json      (Mode A : pilotage par fichiers)
P8  Offline reconstruction → project → render.wav (via OfflineRenderer)
P9  API locale Python ↔ DAW (HTTP/WS/gRPC)                     (Mode B : pilotage à distance)
P10 Boucle d'optimisation audio→Python→DAW→render→Python
```

**Pilotage du DAW par le projet Python** = deux modes, dans cet ordre :
- **Mode A (P7-P8, prioritaire)** : Python produit MIDI + presets + `project.json`,
  le DAW les importe et rend via `OfflineRenderer`. Simple, déterministe, testable,
  sans réseau.
- **Mode B (P9)** : API locale (HTTP/WS/gRPC) exposée par le DAW pour un contrôle
  **à distance** en temps réel (charger un projet, régler un `semanticId`, lancer
  un rendu, récupérer des features). Abordé **seulement après** stabilisation du
  format, du rendu et de l'import fichier.

---

## 8. Lien avec le projet d'analyse Python

Le projet Python (analyse / séparation / transcription / estimation de paramètres)
cible **exclusivement la couche sémantique** : il produit `arrangement.mid`,
`*.synth.json`, `project.json` et travaille sur les `semanticId`, jamais sur les
`clap_id` ni les ID internes VSM. Le pont vers le vrai moteur passe par
`OfflineRenderer` (rendu headless `project → WAV`), avec seed déterministe pour
une optimisation reproductible.

---

## 9. Critère de réussite final

```
original.wav → Python → (arrangement.mid + *.synth.json + project.json)
             → VSM DAW → chargement auto (MIDI + instruments + presets)
             → ProcessGraph / CLAP → OfflineRenderer → reconstructed.wav
```

Simultanément : (1) le DAW fonctionne sans Python ; (2) sans CLAP pour ses
instruments natifs ; (3) instruments natifs inchangés ; (4) `core/` ignore Python
et CLAP ; (5) `audio/` ignore Python et l'UI ; (6) un instrument s'ajoute sans
toucher l'UI ; (7) paramètres identifiables sémantiquement ; (8) presets Python
importables ; (9) leur MIDI lu par le moteur existant ; (10) projet importé rendu
par `OfflineRenderer` ; (11) tests existants verts ; (12) approximations documentées.

### Le critère est éprouvé sur un morceau réel (29/08/2026)

Il ne l'avait jamais été sur un projet produit par la chaîne elle-même — les
mesures portaient sur `docs/examples/demo-project`, écrit à la main. *Sky and
Sand* (Fritz Kalkbrenner, 8 min 52) est passé par la flèche entière : audio →
`reconstruire.py` → `arrangement.mid` + quatre `*.synth.json` + `project.json`
→ `vsm-render` → WAV.

| ce qui est vérifié | résultat |
|---|---|
| le dossier de projet se charge et se rend | **4/4 pistes sonorisées**, 534,167 s, code de sortie 0 |
| déterminisme (deux rendus du même dossier) | **identiques octet pour octet** |
| le rendu hors ligne EST celui de la chaîne | corrélation 1,000000, **écart maximal 0,00** |

La troisième ligne est la seule qui n'allait pas de soi : elle dit que le WAV
qu'un tiers obtiendra en rendant le dossier est **le même échantillon pour
échantillon** que celui sur lequel la chaîne a calculé sa distance publiée. Le
chiffre du `rapport.json` porte donc sur un objet reproductible par quelqu'un
d'autre, et c'est tout l'intérêt d'écrire un dossier de projet plutôt qu'un WAV.

**Deux pièges rencontrés en le vérifiant, et ils valent d'être écrits.**

*La fréquence d'échantillonnage fait partie des conditions d'une mesure.*
`vsm-render` rend à **48 000 Hz par défaut**, la chaîne d'analyse travaille à
44 100. Comparés tels quels, les deux rendus donnent une corrélation de
**0,0002** — c'est-à-dire aucun rapport — alors qu'ils sont identiques à taux
égal. Rien n'est faux dans les deux programmes ; c'est la comparaison qui l'est.
C'est la règle du § 10.3 de `ROADMAP-fusion.md` sous une forme de plus, après la
métrique, le budget et le `gate` : **une distance n'est un chiffre que si l'on
sait à quelles conditions elle a été obtenue**, et le taux d'échantillonnage en
fait partie. Passer `--sample-rate 44100` pour rejouer une mesure de la chaîne.

*Un binaire périmé ne se signale pas comme périmé.* Un `vsm-render` d'août
traînait à la racine du dépôt (ignoré par git, reliquat local). Il charge le
projet, avertit « Instrument manquant : vsm.multisample », rend une piste
SILENCIEUSE — la piste mélodique principale du morceau — et sort avec le code
**0**. L'avertissement est correct et le comportement conforme au § 5 (« jamais
de substitution silencieuse ») ; il reste qu'un rendu réussi peut être un rendu
amputé. Le binaire à employer est `build/tools/vsm-render`, celui que
`find_vsm_render` retient, et c'est celui que le README nomme.

**LA MÊME LEÇON, UNE SECONDE FOIS ET PLUS CHÈRE (02/09/2026), parce qu'elle
était restée une consigne.** Le paragraphe ci-dessus disait quoi faire — prendre
`build/tools/vsm-render` — et c'est précisément ce que faisaient les courses
v13 et v14. Le bon binaire, donc ; mais compilé à 08:48, alors que sept machines
ont été écrites entre 09:13 et 10:17. Les deux courses ont annoncé un commit
dont le vivier compte quarante-sept machines mélodiques et n'en ont vu quarante
et une, sans que rien ne le dise. **Un binaire périmé ne se signale toujours pas
comme périmé, et il ne le fera jamais tant que la parade est une chose à se
rappeler.** Une consigne qu'on doit garder en tête est une consigne qu'on oublie
sous charge — d'autant plus qu'ici il n'y a pas d'erreur à commettre : il suffit
d'écrire une machine et de lancer une mesure, deux gestes justes, pour que le
résultat soit faux.

Elle devient donc une **mesure imprimée** : `reconstruire.py` inscrit un bloc
`moteur` dans la provenance de `rapport.json` — chemin, date de compilation,
taille, nombre de machines déclarées — et se plaint au démarrage, en nommant le
fichier fautif, dès que le binaire est plus vieux qu'une source de `audio/`,
`core/` ou `interchange/`. Voir `analyse/tests/test_provenance_moteur.py`.
