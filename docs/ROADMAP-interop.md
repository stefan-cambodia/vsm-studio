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
| **P5** | adaptateur CLAP | **Fait** — `vsm-instruments.clap` expose les 11 machines ; enveloppe le DSP natif, rendu identique à l'échantillon près (testé) |
| **P6** | hôte CLAP | **Fait** — un `.clap` externe est chargé et présenté comme `ISynthPlugin` : ProcessGraph et l'UI inchangés |
| **P9** | API locale (Mode B) | **Fait, sous la forme la plus simple qui tienne** : `vsm-render --serve` lit des requêtes JSON ligne à ligne sur l'entrée standard. Pas de port, pas d'authentification, pas de serveur à laisser tourner -- les deux programmes sont sur la même machine. Pont Python dans `analyse/analyzer/vsm_engine.py` |
| **P7** | import d'un dossier de projet | **Fait** — `loadProjectBundle`/`saveProjectBundle` : project.json + MIDI + presets, dégradation honnête (preset ou instrument manquant signalé, jamais substitué) |
| **P8** | reconstruction hors ligne | **Fait** — `vsm-render <dossier> <sortie.wav>`, sans interface ni carte son ; rendu déterministe octet pour octet, via le même ProcessGraph que la lecture temps réel |

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
