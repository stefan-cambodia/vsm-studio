#include "PcmHybridSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::pcmhybrid {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;
using vsm::audio::io::SampleBuffer;
using vsm::audio::io::SampleBufferPtr;

namespace {
constexpr uint64_t kBaseSeed = 0x50434D485942ULL; // "PCMHYB"

/// Enveloppe exponentielle simple, en secondes, pour la fabrication des
/// transitoires. Volontairement locale : ce n'est pas une brique du moteur,
/// c'est un outil de génération appelé une seule fois.
float decayAt(double seconds, double timeConstant) {
    return static_cast<float>(std::exp(-seconds / timeConstant));
}
} // namespace

AttackBank::AttackBank() {
    // Durée : 400 ms. Au-delà, ce ne serait plus une attaque mais un son -- et
    // c'est le rôle de la couche synthétique.
    const size_t frames = static_cast<size_t>(kReferenceRate * 0.4);
    vsm::util::DeterministicRng rng{0xA77ACC0ULL};

    for (auto& attack : attacks_) attack.assign(frames, 0.0f);

    // 1. MALLET -- un maillet sur une lame. Bruit très court filtré haut,
    //    plus deux partiels inharmoniques qui s'éteignent vite. C'est le
    //    « toc » qui fait entendre le bois ou le métal frappé.
    {
        auto& out = attacks_[0];
        names_[0] = "MALLET";
        float lowpassState = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / kReferenceRate;
            const float noise = rng.nextBipolar();
            lowpassState += (noise - lowpassState) * 0.55f; // passe-bas doux
            const float knock = (noise - lowpassState) * decayAt(t, 0.004);
            const float body = 0.5f * std::sin(static_cast<float>(t * kTwoPi * 2400.0)) * decayAt(t, 0.020)
                             + 0.3f * std::sin(static_cast<float>(t * kTwoPi * 5700.0)) * decayAt(t, 0.010);
            out[i] = knock + body;
        }
    }

    // 2. PLUCK -- une corde pincée. Front riche puis extinction rapide des
    //    harmoniques hautes ; le grave reste plus longtemps.
    {
        auto& out = attacks_[1];
        names_[1] = "PLUCK";
        for (size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / kReferenceRate;
            float sum = 0.0f;
            for (int harmonic = 1; harmonic <= 24; ++harmonic) {
                // Les harmoniques hautes s'éteignent plus vite : c'est ce qui
                // distingue un pincement d'un simple clic filtré.
                const double timeConstant = 0.09 / static_cast<double>(harmonic);
                sum += static_cast<float>(
                    std::sin(t * kTwoPi * 220.0 * harmonic) * decayAt(t, timeConstant) / harmonic);
            }
            out[i] = sum * 0.8f;
        }
    }

    // 3. BREATH -- un souffle d'anche ou de flûte. Bruit passé en bande,
    //    montée en quelques millisecondes puis extinction lente.
    {
        auto& out = attacks_[2];
        names_[2] = "BREATH";
        float bandState1 = 0.0f, bandState2 = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / kReferenceRate;
            const float noise = rng.nextBipolar();
            bandState1 += (noise - bandState1) * 0.25f;
            bandState2 += (bandState1 - bandState2) * 0.25f;
            const float band = bandState1 - bandState2; // passe-bande grossier
            const float rise = static_cast<float>(1.0 - std::exp(-t / 0.006));
            out[i] = band * rise * decayAt(t, 0.11) * 3.0f;
        }
    }

    // 4. SCRAPE -- un archet qui mord. Bruit peigné : le retard court crée
    //    une résonance qui donne l'impression de frottement sur une corde.
    {
        auto& out = attacks_[3];
        names_[3] = "SCRAPE";
        const size_t delay = 137; // ~350 Hz de résonance de peigne
        std::vector<float> history(frames + delay, 0.0f);
        for (size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / kReferenceRate;
            const float noise = rng.nextBipolar() * decayAt(t, 0.05);
            const float delayed = i >= delay ? history[i - delay] : 0.0f;
            history[i] = noise + delayed * 0.82f;
            out[i] = history[i] * 0.5f;
        }
    }

    // 5. BELL -- une frappe métallique. Partiels franchement inharmoniques :
    //    aucune hauteur nette, et c'est exactement ce qu'on veut devant un
    //    corps de son qui, lui, en a une.
    {
        auto& out = attacks_[4];
        names_[4] = "BELL";
        const double partials[6] = {520.0, 1180.0, 1970.0, 3310.0, 4930.0, 7100.0};
        for (size_t i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / kReferenceRate;
            float sum = 0.0f;
            for (int k = 0; k < 6; ++k)
                sum += static_cast<float>(std::sin(t * kTwoPi * partials[k])
                                          * decayAt(t, 0.14 / (1.0 + k * 0.55)) / (1.0 + k));
            out[i] = sum * 0.7f;
        }
    }

    // Normalisation en crête, transitoire par transitoire : sans elle,
    // changer d'attaque changerait aussi le volume, et l'utilisateur
    // prendrait un choix de timbre pour un réglage de niveau.
    for (auto& attack : attacks_) {
        float peak = 0.0f;
        for (float sample : attack) peak = std::max(peak, std::abs(sample));
        if (peak > 1e-6f) {
            const float scale = 0.9f / peak;
            for (float& sample : attack) sample *= scale;
        }
    }
}

const AttackBank& AttackBank::shared() {
    static const AttackBank bank;
    return bank;
}

const char* AttackBank::attackName(size_t index) const {
    return index < kAttackCount ? names_[index] : "";
}

const std::vector<float>& AttackBank::attack(size_t index) const {
    return attacks_[std::min(index, kAttackCount - 1)];
}

void PcmHybridVoice::prepare(double sampleRate, uint64_t seed) {
    sampleRate_ = sampleRate;
    rng_ = vsm::util::DeterministicRng{seed};
    tone_.setSampleRate(sampleRate);
    filter_.setSampleRate(sampleRate);
    filter_.setMode(StateVariableFilter::Mode::LowPass);
    attackTone_.setSampleRate(sampleRate);
    attackTone_.setMode(StateVariableFilter::Mode::LowPass);
    ampEnv_.setSampleRate(sampleRate);
    filterEnv_.setSampleRate(sampleRate);
    drift_.setSampleRate(sampleRate); drift_.setSeed(seed); drift_.setRateHz(0.12f);
}

void PcmHybridVoice::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    channel_ = channel; note_ = note; velocity_ = velocity;
    ampEnv_.noteOn(); filterEnv_.noteOn();
    baseHz_ = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
    // L'attaque REPART DE ZÉRO à chaque note : c'est un événement percussif,
    // pas un oscillateur libre. Ne pas la relancer donnerait des notes sans
    // attaque dès la deuxième, ce qui est le défaut le plus audible qu'une
    // machine de ce type puisse avoir.
    attackPosition_ = 0.0;
    attackEnvelope_ = 1.0f;
    attackPlaying_ = true;
    tone_.reset(static_cast<double>(rng_.nextUnipolar()));
}

float PcmHybridVoice::render(const AttackBank& bank, const SampleBuffer* overrideSample,
                              const Params& p, float lfo) {
    if (!ampEnv_.isActive()) return 0.0f;

    const float filterEnvLevel = filterEnv_.nextSample();
    const float ampLevel = ampEnv_.nextSample();
    const float velocity = static_cast<float>(velocity_) / 127.0f;

    const float drift = drift_.nextValue() * 0.05f;
    const float vibrato = lfo * p.lfoToPitch * 0.5f;
    const float toneHz = baseHz_ * std::exp2f((p.toneDetune + drift + vibrato) / 12.0f);

    // --- Couche 1 : l'attaque -------------------------------------------------
    float attack = 0.0f;
    if (attackPlaying_) {
        const std::vector<float>* generated = nullptr;
        const SampleBuffer* loaded = (overrideSample != nullptr && !overrideSample->empty())
                                        ? overrideSample : nullptr;
        double sourceRate = AttackBank::kReferenceRate;
        size_t length = 0;
        if (loaded != nullptr) {
            sourceRate = loaded->sampleRate;
            length = loaded->numFrames();
        } else {
            generated = &bank.attack(static_cast<size_t>(std::max(0, p.attackIndex)));
            length = generated->size();
        }

        if (length >= 2) {
            const auto index = static_cast<size_t>(attackPosition_);
            if (index + 1 < length) {
                const float fraction = static_cast<float>(attackPosition_ - static_cast<double>(index));
                const float a = loaded ? loaded->left[index] : (*generated)[index];
                const float b = loaded ? loaded->left[index + 1] : (*generated)[index + 1];
                attack = (a + (b - a) * fraction) * attackEnvelope_;

                // L'attaque SUIT LA NOTE, contrairement au sampler du parc où
                // la touche sélectionne un emplacement. Ici la machine est
                // mélodique : une attaque de maillet doit monter avec le
                // clavier, sinon elle se décolle du corps du son dès qu'on
                // change d'octave.
                const double pitchRatio = static_cast<double>(baseHz_ / 261.6256f)
                                          * std::exp2(static_cast<double>(p.attackTune) / 12.0);
                attackPosition_ += pitchRatio * sourceRate / sampleRate_;
            } else {
                attackPlaying_ = false;
            }
        } else {
            attackPlaying_ = false;
        }

        // Extinction propre : sans elle, une attaque coupée net en pleine
        // amplitude claquerait.
        if (p.attackDecay > 0.001f) {
            const float coefficient = std::exp(-1.0f / (p.attackDecay * static_cast<float>(sampleRate_)));
            attackEnvelope_ *= coefficient;
            if (attackEnvelope_ < 0.0005f) attackPlaying_ = false;
        }

        // Filtrage de l'attaque, séparé de celui du son : c'est le réglage
        // qui fait passer d'un maillet dur à un maillet feutré.
        attackTone_.setCutoffHz(std::clamp(300.0f + p.attackTone * 15000.0f, 20.0f, 18000.0f));
        attackTone_.setResonance(0.707f);
        attack = attackTone_.process(attack);

        // La vélocité agit d'abord sur l'attaque, comme sur l'instrument
        // qu'on imite : jouer plus fort change surtout le bruit du contact.
        const float velocityGain = 1.0f - p.velocityToAttack * (1.0f - velocity);

        // FACTEUR 2.5 sur la couche d'attaque, et ce n'est pas arbitraire :
        // l'énergie d'un transitoire tient dans quelques millisecondes, celle
        // d'un son tenu s'étale sur toute la note. À crête égale, l'attaque
        // pèse donc bien moins à l'oreille -- mesuré : elle passait SOUS la
        // couche entretenue alors même que son réglage était au maximum et
        // celui du corps à un tiers. Sans cette compensation, le réglage
        // d'attaque ne servirait qu'à colorer légèrement le début de la note,
        // au lieu d'être l'événement qui donne son identité à l'instrument.
        attack *= p.attackLevel * velocityGain * 2.5f;
    }

    // --- Couche 2 : le corps --------------------------------------------------
    tone_.setFrequency(toneHz);
    switch (p.toneShape) {
        case 1: tone_.setWaveform(Waveform::Square); break;
        case 2: tone_.setWaveform(Waveform::Triangle); break;
        case 3: tone_.setWaveform(Waveform::Sine); break;
        default: tone_.setWaveform(Waveform::Saw); break;
    }
    const float tone = tone_.nextSample() * p.toneLevel;

    // --- Structure ------------------------------------------------------------
    // En parallèle, les deux couches s'additionnent. En modulation en anneau,
    // le corps est MULTIPLIÉ par l'attaque : le résultat n'a plus les
    // fréquences de l'un ni de l'autre mais leurs sommes et différences,
    // d'où des timbres métalliques qu'aucune addition ne produit.
    const float mixed = p.ringModulation ? (tone * attack * 2.0f + attack * 0.3f)
                                          : (tone + attack);

    const float envOctaves = p.envAmount * filterEnvLevel * 5.0f;
    const float lfoOctaves = lfo * p.lfoToFilter * 3.0f;
    const float trackOctaves = p.keyTrack * (static_cast<float>(note_) - 60.0f) / 12.0f;
    const float velocityOctaves = p.velocityToFilter * (velocity - 0.5f) * 2.0f;
    const float cutoff = std::clamp(
        p.cutoff * std::exp2f(envOctaves + lfoOctaves + trackOctaves + velocityOctaves), 20.0f, 18000.0f);

    filter_.setCutoffHz(cutoff);
    filter_.setResonance(0.707f + p.resonance * 7.0f);
    return filter_.process(mixed) * ampLevel;
}

PcmHybridSynth::PcmHybridSynth() {
    parameterList_ = {
        {kAttackSample, "Attack Sample", 0.0f, 4.0f, 0.0f, ""},
        {kAttackLevel, "Attack Level", 0.0f, 1.0f, 0.8f, ""},
        {kAttackDecay, "Attack Decay", 0.005f, 2.0f, 0.18f, "s"},
        {kAttackTune, "Attack Tune", -24.0f, 24.0f, 0.0f, "st"},
        {kAttackTone, "Attack Tone", 0.0f, 1.0f, 0.7f, ""},
        {kVelocityToAttack, "Velocity to Attack", 0.0f, 1.0f, 0.6f, ""},
        {kToneShape, "Tone Shape", 0.0f, 3.0f, 0.0f, ""},
        {kToneLevel, "Tone Level", 0.0f, 1.0f, 0.7f, ""},
        {kToneDetune, "Tone Detune", -12.0f, 12.0f, 0.0f, "st"},
        {kStructure, "Structure", 0.0f, 1.0f, 0.0f, ""},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 18000.0f, 4500.0f, "Hz"},
        {kFilterResonance, "Filter Resonance", 0.0f, 1.0f, 0.15f, ""},
        {kFilterEnvAmount, "Filter Env Amount", -1.0f, 1.0f, 0.3f, ""},
        {kFilterKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.4f, ""},
        {kFilterAttack, "Filter Attack", 0.001f, 4.0f, 0.005f, "s"},
        {kFilterDecay, "Filter Decay", 0.001f, 8.0f, 0.8f, "s"},
        {kFilterSustain, "Filter Sustain", 0.0f, 1.0f, 0.4f, ""},
        {kFilterRelease, "Filter Release", 0.001f, 8.0f, 0.5f, "s"},
        {kAmpAttack, "Amp Attack", 0.001f, 4.0f, 0.002f, "s"},
        {kAmpDecay, "Amp Decay", 0.001f, 8.0f, 1.2f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.6f, ""},
        {kAmpRelease, "Amp Release", 0.001f, 8.0f, 0.5f, "s"},
        {kLfoRate, "LFO Rate", 0.05f, 20.0f, 4.0f, "Hz"},
        {kLfoToPitch, "LFO to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kLfoToFilter, "LFO to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kVelocityToFilter, "Velocity to Filter", 0.0f, 1.0f, 0.3f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.15f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void PcmHybridSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    bank_ = &AttackBank::shared(); // engendré ici, jamais depuis le fil audio
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](PcmHybridVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
    lfoPhase_ = 0.0;
}

void PcmHybridSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float PcmHybridSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState PcmHybridSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.pcmhybrid";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void PcmHybridSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

bool PcmHybridSynth::loadSample(int slot, const std::string& path, std::string& outError) {
    if (slot != 0) { outError = "cette machine n'a qu'un emplacement (l'attaque)"; return false; }
    auto result = vsm::audio::io::WavFileReader::readFile(path);
    if (!result.success) {
        // Échec SIGNALÉ, attaque laissée telle quelle : substituer un autre
        // son donnerait un rendu faux que personne ne rattacherait au fichier
        // manquant. Même règle que dans le sampler.
        outError = result.error;
        return false;
    }
    if (result.buffer.empty()) { outError = "fichier sans échantillon : " + path; return false; }
    attackSample_.store(std::make_shared<const SampleBuffer>(std::move(result.buffer)),
                        std::memory_order_release);
    attackPath_ = path;
    return true;
}

void PcmHybridSynth::clearSample(int slot) {
    if (slot != 0) return;
    attackSample_.store(nullptr, std::memory_order_release);
    attackPath_.clear();
}

std::string PcmHybridSynth::samplePath(int slot) const {
    return slot == 0 ? attackPath_ : std::string();
}

void PcmHybridSynth::setAttackSample(SampleBufferPtr sample) {
    attackSample_.store(sample, std::memory_order_release);
    attackPath_ = sample ? sample->sourcePath : std::string();
}

void PcmHybridSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void PcmHybridSynth::process(const MidiNoteEvent* events, int numEvents,
                              float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    if (bank_ == nullptr) {
        std::fill(outputL, outputL + numSamples, 0.0f);
        std::fill(outputR, outputR + numSamples, 0.0f);
        return;
    }

    // Capture UNE FOIS par bloc : le pointeur est ensuite maintenu en vie
    // pour toute la durée du bloc, même si l'attaque est rechargée entre-temps.
    const SampleBufferPtr overrideSample = attackSample_.load(std::memory_order_acquire);

    PcmHybridVoice::Params p;
    p.attackIndex = static_cast<int>(std::lround(params_[kAttackSample].load(std::memory_order_relaxed)));
    p.attackLevel = params_[kAttackLevel].load(std::memory_order_relaxed);
    p.attackDecay = params_[kAttackDecay].load(std::memory_order_relaxed);
    p.attackTune = params_[kAttackTune].load(std::memory_order_relaxed);
    p.attackTone = params_[kAttackTone].load(std::memory_order_relaxed);
    p.velocityToAttack = params_[kVelocityToAttack].load(std::memory_order_relaxed);
    p.toneShape = static_cast<int>(std::lround(params_[kToneShape].load(std::memory_order_relaxed)));
    p.toneLevel = params_[kToneLevel].load(std::memory_order_relaxed);
    p.toneDetune = params_[kToneDetune].load(std::memory_order_relaxed);
    p.ringModulation = params_[kStructure].load(std::memory_order_relaxed) >= 0.5f;
    p.cutoff = params_[kFilterCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kFilterResonance].load(std::memory_order_relaxed);
    p.envAmount = params_[kFilterEnvAmount].load(std::memory_order_relaxed);
    p.keyTrack = params_[kFilterKeyTrack].load(std::memory_order_relaxed);
    p.lfoToPitch = params_[kLfoToPitch].load(std::memory_order_relaxed);
    p.lfoToFilter = params_[kLfoToFilter].load(std::memory_order_relaxed);
    p.velocityToFilter = params_[kVelocityToFilter].load(std::memory_order_relaxed);

    const AdsrSettings amp{
        params_[kAmpAttack].load(std::memory_order_relaxed),
        params_[kAmpDecay].load(std::memory_order_relaxed),
        params_[kAmpSustain].load(std::memory_order_relaxed),
        params_[kAmpRelease].load(std::memory_order_relaxed),
    };
    const AdsrSettings filter{
        params_[kFilterAttack].load(std::memory_order_relaxed),
        params_[kFilterDecay].load(std::memory_order_relaxed),
        params_[kFilterSustain].load(std::memory_order_relaxed),
        params_[kFilterRelease].load(std::memory_order_relaxed),
    };
    const float drift = params_[kAnalogCharacter].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](PcmHybridVoice& voice) {
        voice.setSettings(amp, filter);
        voice.setDriftAmount(drift);
    });

    const float lfoRate = params_[kLfoRate].load(std::memory_order_relaxed);
    const double lfoIncrement = static_cast<double>(lfoRate) / sampleRate_;
    // Mesuré : à 0.22, un accord de huit notes à vélocité 110 crêtait à 1.18
    // -- huit transitoires qui partent au même instant s'additionnent en
    // phase, contrairement à huit oscillateurs libres. 0.17 laisse la marge.
    constexpr float kVoiceGain = 0.17f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        const float lfo = static_cast<float>(std::sin(kTwoPi * lfoPhase_));

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](PcmHybridVoice& voice) {
            sum += voice.render(*bank_, overrideSample.get(), p, lfo);
        });
        sum *= kVoiceGain;
        outputL[i] = sum;
        outputR[i] = sum;

        lfoPhase_ += lfoIncrement;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.pcmhybrid", "PCM + Synth Hybrid", PcmHybridSynth);

} // namespace vsm::plugins::pcmhybrid
