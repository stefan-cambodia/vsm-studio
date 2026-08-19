# VSM Studio — Cahier des charges : ajouter une nouvelle machine

**But de ce document.** Décrire, de façon complète et exécutable, comment
créer un nouvel instrument (synthé, boîte à rythmes, futur module FM/wavetable)
qui s'intègre au projet **sans jamais modifier le moteur, l'UI, le transport,
le mixer ni le piano roll**. C'est le contrat de la section 22 du cahier des
charges d'origine, rendu opérationnel.

Référence vivante, à lire **avec** `ARCHITECTURE.md` (architecture globale) et
`docs/ROADMAP-interop.md` (Phase 7 : CLAP / Python).

---

## 0. La règle d'or

Ajouter une machine touche **exactement** :

1. un nouveau dossier `audio/plugins/<machine>/` (un `.h` + un `.cpp`) ;
2. **2 lignes** dans `audio/CMakeLists.txt` (le `.cpp` du plugin + son test) ;
3. **1 bloc** dans `audio/src/plugin/BuiltInPlugins.cpp` (force-link) ;
4. un fichier de test `audio/tests/test_<machine>_synth.cpp`.

**Rien d'autre.** Ni `ProcessGraph`, ni `AudioEngine`, ni `Track`, ni
`SynthRackComponent`, ni `MixerComponent`, ni `TrackListComponent`. Le Synth
Rack construit automatiquement un knob par paramètre déclaré ; le combo
« instrument » du Track Editor liste automatiquement la machine dès qu'elle est
enregistrée. Si vous devez toucher un fichier du moteur ou de l'UI pour ajouter
une machine, **c'est un bug de conception** — signalez-le au lieu de contourner.

---

## 1. Le contrat : `ISynthPlugin`

Toute machine implémente `vsm::audio::plugin::ISynthPlugin`
(`audio/include/vsm/audio/plugin/ISynthPlugin.h`) :

```cpp
class ISynthPlugin {
public:
    virtual ~ISynthPlugin() = default;
    virtual void initialize(double sampleRate, int maxBlockSize) = 0;
    virtual void process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) = 0;
    virtual void setParameter(ParamId id, float value) = 0;
    virtual float getParameter(ParamId id) const = 0;
    virtual const ParameterList& parameterList() const = 0;
    virtual PresetState saveState() const = 0;
    virtual void loadState(const PresetState& state) = 0;
    virtual const char* machineName() const = 0;
    virtual int activeVoiceCount() const = 0;
};
```

Types associés (`ParameterTypes.h`) :

- `using ParamId = uint32_t;`
- `struct ParameterInfo { ParamId id; std::string name; float minValue, maxValue,
  defaultValue; std::string unit; };`
- `using ParameterList = std::vector<ParameterInfo>;`
- `struct PresetState { std::string pluginTypeId; std::unordered_map<ParamId,float> parameterValues; };`
- `struct MidiNoteEvent { enum class Kind { NoteOn, NoteOff }; Kind kind; int sampleOffset;
  uint8_t channel, note, velocity; };`

### Conventions obligatoires

- **Identifiant** : `"vsm.<machine>"` (ex. `"vsm.jupiter8"`), stable et unique.
  Sert d'`id` de registre ET de `PresetState::pluginTypeId`.
- **Namespace** : `vsm::plugins::<machine>` (ex. `vsm::plugins::jupiter8`).
- **Nom d'affichage** : « <Nom>-style … » (ex. `"Jupiter-8-style Polysynth"`).
  On n'affirme jamais être la machine originale (voir §10, authenticité).
- **Paramètres** : un `enum ParamIds : ParamId { kFoo = 0, …, kNumParams };`
  contigu à partir de 0. Stockage interne : `std::array<std::atomic<float>, kNumParams>`.

---

## 2. Règles temps réel (non négociables)

`process()` est appelé sur le **thread audio**. Interdits absolus :

- **aucune allocation / libération** (`new`, `malloc`, `std::vector::resize`,
  `push_back`, `std::string`, `std::function` construit…) ;
- **aucun lock** (`std::mutex`), aucune I/O (fichier, réseau, `printf`), aucun JSON ;
- **aucun `rand()` / `std::random_device`** — uniquement le RNG déterministe
  (voir §6).

Tout buffer doit être dimensionné dans `initialize()`. `setParameter` /
`getParameter` peuvent être appelés depuis l'UI **et** l'audio : rendez-les
thread-safe via `std::atomic<float>` (jamais un mutex).

Sortie **stéréo** : écrivez `outputL[i]` **et** `outputR[i]` (identiques si la
machine est mono ; le `ProcessGraph` gère ensuite pan/volume/inserts/sends).
Protégez le DSP récursif (filtres, delays) avec
`vsm::audio::dsp::ScopedNoDenormals` (`DenormalGuard.h`).

### Squelette de boucle `process()` (déclenchement sample-accurate)

```cpp
int ev = 0;
for (int i = 0; i < numSamples; ++i) {
    while (ev < numEvents && events[ev].sampleOffset == i) {
        const auto& e = events[ev];
        if (e.kind == MidiNoteEvent::Kind::NoteOn && e.velocity > 0)
            /* déclencher la note e.note / e.velocity */;
        else
            /* note off e.note */;
        ++ev;
    }
    float s = /* synthèse d'un échantillon */;
    outputL[i] = s;
    outputR[i] = s;
}
```

---

## 3. Choisir son architecture de voix

Trois patrons éprouvés, tous déjà en place :

- **Monophonique** (Minimoog, TB-303, SH-101) →
  `vsm::audio::engine::MonoVoiceAllocator` : priorité dernière note + glide.
  API : `noteOn(note,vel) → {shouldPlay,note,velocity,retrigger}`,
  `noteOff(note) → {…}`, `hasHeldNotes()`, `reset()`. Le glide se fait via
  `ParameterSmoother` sur le numéro de note.

- **Polyphonique** (Juno-106, Prophet) →
  `vsm::audio::engine::VoiceManager<Voice, N>`. Votre classe `Voice` doit
  exposer : `bool isActive() const`, `uint8_t note() const`,
  `uint8_t channel() const`, `void noteOn(ch,note,vel)`, `void noteOff(vel)`.
  Vol de voix « oldest note » automatique. Parcours pour le rendu :
  `vm.forEachVoice([&](Voice& v){ sum += v.render(...); });`.

- **Percussion / boîte à rythmes** (TR-808, TR-909) → une **voix synthétisée
  par pièce** (kick, snare, hats…), déclenchée par numéro de note MIDI
  (mapping GM : 36 kick, 38 snare, 42/46 hats…). Enveloppes `DecayEnvelope`.
  Aucune allocation dynamique de voix ; les pièces sont des membres fixes.
  **Section 12 : synthétiser, ne pas lire d'échantillons** (documenter les
  approximations pour les cymbales/charlestons si le hardware d'origine
  utilisait du PCM).

---

## 4. Briques DSP disponibles (réutiliser avant de réécrire)

Dans `audio/include/vsm/audio/dsp/` — toutes sans dépendance JUCE, testées :

| Brique | Rôle | API clé |
|---|---|---|
| `BandLimitedOscillator` (`Oscillator.h`) | Osc anti-aliasé (PolyBLEP) saw/square/tri/sine + PWM | `setSampleRate/setWaveform/setFrequency/setPulseWidth/reset/nextSample` |
| `AdsrEnvelope` (`Envelope.h`) | Enveloppe ADSR | `setSampleRate/setSettings(AdsrSettings)/noteOn/noteOff/nextSample/isActive` |
| `DecayEnvelope` (`DecayEnvelope.h`) | Enveloppe percussive one-shot | `setSampleRate/setDecaySeconds/trigger/choke/next/isActive` |
| `LadderFilterZDF` (`LadderFilterZDF.h`) | Ladder Moog ZDF, **nb de pôles réglable** (3=303, 4=Moog), auto-oscillant | `setCutoffHz/setResonance/setPoleCount/setDrive/process` |
| `StateVariableFilter` (`Filter.h`) | SVF LP/HP/BP/Notch | `setMode/setCutoffHz/setResonance/process` |
| `Biquad` (`Biquad.h`) | EQ/shelf/peak | (voir en-tête) |
| `AnalogDrift` (`AnalogDrift.h`) | Dérive analogique **seedée** | `setSeed/setAmount/setRateHz/nextValue` |
| `ParameterSmoother` (`ParameterSmoother.h`) | Lissage (glide, anti-zipper) | `setSmoothingTimeMs/reset/setTarget/nextValue` |
| `Chorus` (`Chorus.h`) | Chorus BBD stéréo | `setRateHz/setDepthMs/setMix/process(in,outL,outR)` |
| `Oversampler` (`Oversampler.h`) | Suréchantillonnage 2×/4×/8× (non-linéarités) | `prepare(factor,maxBlock)/…` |
| `Dynamics.h`, `LufsMeter.h` | Compresseur/limiteur, mesure LUFS | (utilisés par le bus master) |
| `Constants.h` | `kPi`, `kTwoPi` | — |
| `DenormalGuard.h` | `ScopedNoDenormals`, `flushDenormalToZero` | à utiliser dans tout DSP récursif |

**Quand créer une nouvelle brique ?** Seulement si elle est réutilisable par
d'autres machines (ex. le chorus BBD, extrait pour resservir en effet). Une
brique nouvelle va dans `dsp/`, avec ses propres tests, et **ne dépend pas d'un
plugin**. Sinon, gardez le DSP spécifique dans le dossier du plugin.

---

## 5. Qualité audio (section 14)

- Oscillateurs **band-limited** (utilisez `BandLimitedOscillator`, pas un
  `sin`/rampe naïf pour les formes riches).
- Traitements **non linéaires** (waveshaping, drive fort, hard-sync) → prévoir
  `Oversampler` si l'aliasing est audible (peut être différé et documenté).
- Pas de **zipper noise** : lissez les paramètres audibles (`ParameterSmoother`).
- Pas de **denormals** : `ScopedNoDenormals` en tête de `process()` dès qu'il y
  a récursion (filtres, réverbs, delays).
- Pas de **clics** : enveloppes avec attaque ≥ quelques échantillons.

---

## 6. Caractère analogique & déterminisme (sections 8 et 27)

- Toute variation « analogique » (drift de pitch/cutoff, tolérances, variation
  voix-à-voix) passe par `AnalogDrift` (ou `DeterministicRng` /
  `vsm::util::deriveSeed`), **jamais** `rand()`.
- **Chaque instance d'aléatoire est seedée** (graine fixe par machine, dérivée
  par voix via `deriveSeed(base, index)`). Conséquence testable et **exigée par
  l'interop Phase 7** : même MIDI + mêmes paramètres + même sample rate + même
  seed ⇒ **rendu bit-identique** (test de déterminisme obligatoire, §9).
- Exposez un paramètre `Analog Character` (0 = stable … 1 = très instable)
  quand c'est pertinent, câblé sur `AnalogDrift::setAmount`.

---

## 7. Enregistrement (les 2 points de couplage autorisés)

**a) Auto-enregistrement**, dernière ligne du `.cpp`, **à l'intérieur du
namespace du plugin**, avec le **nom de classe non qualifié** :

```cpp
VSM_REGISTER_SYNTH_PLUGIN("vsm.jupiter8", "Jupiter-8-style Polysynth", Jupiter8Synth);
```

**b) Force-link**, un bloc dans `audio/src/plugin/BuiltInPlugins.cpp` :

```cpp
#include "jupiter8/Jupiter8Synth.h"   // en tête
// … dans registerBuiltInPlugins() :
{ vsm::plugins::jupiter8::Jupiter8Synth forceLinkJupiter8; (void)forceLinkJupiter8; }
```

> **Pourquoi (piège C++ réel).** `vsm_audio` est une bibliothèque **statique** :
> si aucun symbole du `.cpp` du plugin n'est référencé ailleurs, l'éditeur de
> liens élimine toute l'unité de traduction — et le registrar avec elle. Le
> plugin « disparaît » silencieusement, sans erreur de compilation. Le bloc
> force-link construit une instance (dont le constructeur a des effets de bord
> réels) pour forcer l'inclusion. C'est documenté en détail dans
> `PluginRegistry.h`.

---

## 8. Intégration CMake

Dans `audio/CMakeLists.txt` :

```cmake
# dans add_library(vsm_audio STATIC …)
    plugins/jupiter8/Jupiter8Synth.cpp
# dans add_executable(vsm_audio_tests …)
        tests/test_jupiter8_synth.cpp
```

Aucune bibliothèque « plugins » séparée : le `.cpp` est compilé directement
comme source de `vsm_audio` (voir la note dans le CMake).

---

## 9. Tests (obligatoires, même style que l'existant)

Framework maison (`tests/TestFramework.h`) : `VSM_TEST(name){…}`,
`VSM_ASSERT(cond)`, `VSM_ASSERT_EQ(a,b)`, `VSM_ASSERT_NEAR(a,b,eps)`.

**Batterie minimale** attendue pour chaque machine :

1. `…_registered` — présente dans `PluginRegistry` après `registerBuiltInPlugins()`.
2. `…_silent_with_no_events` — sortie nulle, `activeVoiceCount()==0`.
3. `…_note_produces_sound` — une note produit un signal fini (`std::isfinite`)
   d'amplitude non nulle.
4. Comportement **spécifique à la machine** (le cœur de la fidélité) :
   ex. polyphonie effective + vol de voix ; accent/slide (303) ; poly-mod
   (Prophet) ; choke charleston (808/909) ; non-sensibilité à la vélocité
   (Juno/SH-101/Prophet) ; sub-oscillateur ; hard-sync ; etc.
5. `…_is_deterministic` — deux rendus identiques au bit près (avec
   `Analog Character` > 0 pour couvrir le drift seedé).
6. `…_save_load_roundtrip` — `saveState()` puis `loadState()` restaure les
   paramètres ; `pluginTypeId` correct.
7. `…_parameter_list_size` — verrouille le nombre de paramètres.

8. **Empreinte de non-régression audio** (Phase 6, obligatoire) — la machine
   doit apparaître dans `tests/audio_fingerprints.inc`. Un test dédié
   (`regression_every_registered_machine_has_a_reference`) échoue tant que ce
   n'est pas fait, donc ce point ne peut pas être oublié silencieusement.
   Marche à suivre : ajouter `VSM_TEST(regression_<machine>) { checkMachine("vsm.<machine>"); }`
   dans `tests/test_audio_regression.cpp`, puis

   ```
   VSM_REGEN_AUDIO_FINGERPRINTS=1 ./build/audio/vsm_audio_tests
   ```

   et recopier la ligne imprimée pour votre machine dans
   `tests/audio_fingerprints.inc`. Cette empreinte fige le rendu d'une phrase
   type (pic, RMS global et par fenêtre, passages par zéro, largeur stéréo,
   profil spectral en 16 bandes, centroïde) : à partir de là, toute dérive du
   son de la machine -- y compris causée par une brique DSP partagée modifiée
   pour une AUTRE machine -- fait échouer son test.

**Vérification de bout en bout** (au-delà des tests unitaires) : rendre un motif
représentatif via `ProcessGraph` + `OfflineRenderer` en WAV (comme les
`*_demo.wav`) et l'écouter/l'analyser.

---

## 10. Authenticité & approximations (section 27)

- Ne **jamais** prétendre une identité à 100 % avec le hardware sans mesure.
- **Documenter dans le code** chaque approximation assumée (topologie de filtre
  substituée, sync sans correction BLEP, cymbales synthétisées au lieu de PCM,
  facteurs de boost « au caractère » et non mesurés…).
- Statut de fidélité pensé pour l'interop : `measured | derived | estimated |
  approximated | unknown` (voir `docs/ROADMAP-interop.md`).

---

## 11. Prêt pour l'interop (Phase 7) — sans effort supplémentaire

En respectant le modèle `ParameterList` (id contigu, `name` lisible et stable,
bornes réelles en unités physiques), la machine est **déjà**
« ParameterDescriptor-ready » : l'export sémantique `synth.json`, l'adaptateur
CLAP et le pilotage Python se brancheront dessus **sans toucher au DSP**.
Recommandations qui facilitent la Phase 7 :

- noms de paramètres cohérents avec le vocabulaire sémantique (`Filter Cutoff`,
  `Osc 2 Detune`, `Amp Attack`…) ;
- valeurs stockées en **unités réelles** (Hz, dB, demi-tons), pas en normalisé ;
- pas de paramètre « caché » non déclaré dans `parameterList()`.

---

## 12. Checklist finale (ordre recommandé)

```
[ ] Concevoir : architecture osc/filtre/enveloppe/mod, mono vs poly, params.
[ ] Créer audio/plugins/<machine>/<Machine>Synth.h  (enum params, membres)
[ ] Créer audio/plugins/<machine>/<Machine>Synth.cpp (parameterList_, process, save/load)
[ ] VSM_REGISTER_SYNTH_PLUGIN(...) en dernière ligne, dans le namespace
[ ] Force-link dans BuiltInPlugins.cpp (+ include)
[ ] 2 lignes dans audio/CMakeLists.txt (plugin .cpp + test .cpp)
[ ] Écrire tests/test_<machine>_synth.cpp (batterie du §9)
[ ] Ajouter VSM_TEST(regression_<machine>) dans tests/test_audio_regression.cpp,
    puis générer son empreinte (VSM_REGEN_AUDIO_FINGERPRINTS=1) dans audio_fingerprints.inc
[ ] Compiler : cmake -B build -DVSM_BUILD_TESTS=ON -DVSM_BUILD_APP=OFF && cmake --build build
[ ] Tests verts + ZÉRO warning (y compris -Wsign-conversion, -Wfloat-equal, -Wshadow)
[ ] Non-régression : les machines existantes restent vertes
[ ] Rendu WAV de démo via ProcessGraph/OfflineRenderer, écoute
[ ] Documenter les approximations dans le code + mettre à jour ARCHITECTURE.md
```

Compilation stricte recommandée avant livraison (reproduit GCC 16 / flags JUCE) :

```
g++ -std=c++20 -I include -I ../core/include -I plugins \
    -Werror -Wall -Wextra -Wpedantic -Wshadow -Wfloat-equal -Wsign-conversion \
    -fsyntax-only <votre_header_test>.cpp
```

---

## 13. Squelette minimal (copiable, compile tel quel)

Monophonique, un oscillateur + filtre + enveloppe + drive doux. À copier dans
`audio/plugins/example/ExampleSynth.h` / `.cpp`, puis renommer.

### `ExampleSynth.h`

```cpp
#pragma once
#include "vsm/audio/dsp/Constants.h"
#include "vsm/audio/dsp/DenormalGuard.h"
#include "vsm/audio/dsp/Envelope.h"
#include "vsm/audio/dsp/LadderFilterZDF.h"
#include "vsm/audio/dsp/Oscillator.h"
#include "vsm/audio/dsp/ParameterSmoother.h"
#include "vsm/audio/engine/MonoVoiceAllocator.h"
#include "vsm/audio/plugin/ISynthPlugin.h"
#include <array>
#include <atomic>

namespace vsm::plugins::example {

enum ParamIds : vsm::audio::plugin::ParamId {
    kCutoff = 0, kResonance, kAttack, kDecay, kSustain, kRelease, kGlide, kNumParams
};

class ExampleSynth : public vsm::audio::plugin::ISynthPlugin {
public:
    ExampleSynth();
    void initialize(double sampleRate, int maxBlockSize) override;
    void process(const vsm::audio::plugin::MidiNoteEvent* events, int numEvents,
                 float* outL, float* outR, int numSamples) override;
    void setParameter(vsm::audio::plugin::ParamId id, float v) override;
    float getParameter(vsm::audio::plugin::ParamId id) const override;
    const vsm::audio::plugin::ParameterList& parameterList() const override;
    vsm::audio::plugin::PresetState saveState() const override;
    void loadState(const vsm::audio::plugin::PresetState& state) override;
    const char* machineName() const override { return "Example-style Monosynth"; }
    int activeVoiceCount() const override { return env_.isActive() ? 1 : 0; }

private:
    vsm::audio::dsp::BandLimitedOscillator osc_;
    vsm::audio::dsp::LadderFilterZDF filter_;
    vsm::audio::dsp::AdsrEnvelope env_;
    vsm::audio::dsp::ParameterSmoother glide_;
    vsm::audio::engine::MonoVoiceAllocator alloc_;
    std::array<std::atomic<float>, kNumParams> params_;
    vsm::audio::plugin::ParameterList parameterList_;
    double sampleRate_ = 48000.0;
};

} // namespace vsm::plugins::example
```

### `ExampleSynth.cpp`

```cpp
#include "ExampleSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <cmath>

namespace vsm::plugins::example {
using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

ExampleSynth::ExampleSynth() {
    parameterList_ = {
        {kCutoff, "Filter Cutoff", 20.0f, 20000.0f, 1200.0f, "Hz"},
        {kResonance, "Filter Resonance", 0.0f, 4.2f, 0.3f, ""},
        {kAttack, "Amp Attack", 0.001f, 4.0f, 0.01f, "s"},
        {kDecay, "Amp Decay", 0.001f, 8.0f, 0.2f, "s"},
        {kSustain, "Amp Sustain", 0.0f, 1.0f, 0.7f, ""},
        {kRelease, "Amp Release", 0.001f, 8.0f, 0.3f, "s"},
        {kGlide, "Glide Time", 0.0f, 2.0f, 0.0f, "s"},
    };
    for (const auto& p : parameterList_)
        params_[p.id].store(p.defaultValue, std::memory_order_relaxed);
}

void ExampleSynth::initialize(double sr, int) {
    sampleRate_ = sr;
    osc_.setSampleRate(sr); osc_.setWaveform(Waveform::Saw);
    filter_.setSampleRate(sr); filter_.setPoleCount(4);
    env_.setSampleRate(sr);
    glide_.setSampleRate(sr); glide_.reset(60.0f);
    alloc_.reset();
}

void ExampleSynth::process(const MidiNoteEvent* events, int numEvents,
                           float* outL, float* outR, int numSamples) {
    ScopedNoDenormals noDenormals;
    env_.setSettings(AdsrSettings{
        params_[kAttack].load(std::memory_order_relaxed),
        params_[kDecay].load(std::memory_order_relaxed),
        params_[kSustain].load(std::memory_order_relaxed),
        params_[kRelease].load(std::memory_order_relaxed)});
    glide_.setSmoothingTimeMs(params_[kGlide].load(std::memory_order_relaxed) * 1000.0f);
    filter_.setResonance(params_[kResonance].load(std::memory_order_relaxed));
    const float cutoff = params_[kCutoff].load(std::memory_order_relaxed);

    int ev = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (ev < numEvents && events[ev].sampleOffset == i) {
            const auto& e = events[ev++];
            if (e.kind == MidiNoteEvent::Kind::NoteOn && e.velocity > 0) {
                bool wasIdle = !alloc_.hasHeldNotes();
                auto r = alloc_.noteOn(e.note, e.velocity);
                glide_.setTarget(static_cast<float>(r.note));
                if (wasIdle) glide_.reset(static_cast<float>(r.note));
                if (r.retrigger) env_.noteOn();
            } else {
                auto r = alloc_.noteOff(e.note);
                if (r.shouldPlay) glide_.setTarget(static_cast<float>(r.note));
                else env_.noteOff();
            }
        }
        const float note = glide_.nextValue();
        const float hz = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
        osc_.setFrequency(hz);
        filter_.setCutoffHz(cutoff);
        const float s = filter_.process(osc_.nextSample()) * env_.nextSample();
        outL[i] = s; outR[i] = s;
    }
}

void ExampleSynth::setParameter(ParamId id, float v) {
    if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
}
float ExampleSynth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}
const ParameterList& ExampleSynth::parameterList() const { return parameterList_; }

PresetState ExampleSynth::saveState() const {
    PresetState st; st.pluginTypeId = "vsm.example";
    for (const auto& p : parameterList_)
        st.parameterValues[p.id] = params_[p.id].load(std::memory_order_relaxed);
    return st;
}
void ExampleSynth::loadState(const PresetState& st) {
    for (const auto& [id, v] : st.parameterValues)
        if (id < kNumParams) params_[id].store(v, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.example", "Example-style Monosynth", ExampleSynth);

} // namespace vsm::plugins::example
```

---

## 14. Cas particulier : ajouter un EFFET (section 16)

Même philosophie, interface `vsm::audio::effect::IAudioEffect`
(`prepare/reset/process(L,R,n)/setParameter/getParameter/parameterList/effectName`).
Deux points de couplage :

1. Créer `audio/include/vsm/audio/effect/<Effet>.h` (header-only comme les autres).
2. L'enregistrer dans `audio/src/effect/EffectFactory.cpp`
   (`available()` + `create()`), et l'inclure là.

L'UI (`EffectChainComponent`) le proposera alors automatiquement dans le menu
« Ajouter », avec un knob par paramètre. Mêmes règles temps réel, mêmes tests
(passthrough à `mix=0`, effet audible + borné + fini, déterminisme).

---

*Ce document doit être mis à jour si le contrat `ISynthPlugin`, le modèle de
paramètres ou les conventions d'intégration évoluent.*
