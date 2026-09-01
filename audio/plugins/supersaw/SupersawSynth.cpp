#include "SupersawSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::supersaw {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x535550455253ULL; // "SUPERS"

/// Écarts de désaccord des sept scies, en fraction du désaccord courant.
///
/// La répartition n'est PAS régulière, et c'est tout l'intérêt : trois voix
/// restent serrées contre la fondamentale (±0.02, ±0.06) tandis que deux
/// s'en écartent cinq fois plus (±0.11). L'oreille garde ainsi une hauteur
/// nette -- donnée par le paquet central -- tout en entendant la masse.
/// Répartir les sept régulièrement donne un son de chœur désaccordé, pas un
/// supersaw : le test `supersaw_detune_spacing_is_uneven` fige ce déséquilibre.
constexpr float kDetuneOffsets[SupersawVoice::kSawCount] = {
    -0.11002313f, -0.06288439f, -0.01952356f, 0.0f, 0.01991221f, 0.06216538f, 0.10745242f
};

/// Courbe de désaccord : le réglage de façade (0..1) ne commande PAS
/// directement un nombre de cents. La machine d'origine y applique une courbe
/// très plate au début puis très raide, ce qui donne au réglage sa
/// progressivité : le premier quart sert à épaissir, le dernier à détruire la
/// hauteur. Polynôme de degré 11 relevé par Adam Szabo (KTH, 2010) sur
/// l'instrument réel -- repris tel quel, non revérifié ici.
float detuneCurve(float x) {
    const float x2 = x * x, x3 = x2 * x, x4 = x3 * x, x5 = x4 * x, x6 = x5 * x;
    const float x7 = x6 * x, x8 = x7 * x, x9 = x8 * x, x10 = x9 * x, x11 = x10 * x;
    return 10028.7312891634f * x11 - 50818.8652045924f * x10 + 111363.4808729368f * x9
         - 138150.6761080548f * x8 + 106649.6679158292f * x7 - 53046.9642751875f * x6
         + 17019.9518580080f * x5 - 3425.0836591318f * x4 + 404.2703938388f * x3
         - 24.1878824391f * x2 + 0.6717417634f * x + 0.0030115596f;
}

/// Niveau des six scies LATÉRALES en fonction du réglage de mélange : il
/// MONTE quand on ouvre.
float sideGain(float mix) { return -0.73764f * mix * mix + 1.2841f * mix + 0.044372f; }

/// Niveau de la scie CENTRALE : il DESCEND quand on ouvre. Les deux courbes
/// vont en sens inverse, et c'est ce croisement qui fait passer d'une scie
/// franche à un nuage sans jamais perdre le niveau global.
float centreGain(float mix) { return -0.55366f * mix + 0.99785f; }
} // namespace

void SupersawVoice::prepare(double sampleRate, uint64_t seed) {
    sampleRate_ = sampleRate;
    rng_ = vsm::util::DeterministicRng{seed};
    for (auto& saw : saws_) { saw.setSampleRate(sampleRate); saw.setWaveform(Waveform::Saw); }
    sub_.setSampleRate(sampleRate);
    sub_.setWaveform(Waveform::Square);
    for (auto* filter : {&filterL_, &filterR_}) {
        filter->setSampleRate(sampleRate);
        filter->setMode(StateVariableFilter::Mode::LowPass);
    }
    for (auto* filter : {&hpfL_, &hpfR_}) {
        filter->setSampleRate(sampleRate);
        filter->setMode(StateVariableFilter::Mode::HighPass);
    }
    ampEnv_.setSampleRate(sampleRate);
    filterEnv_.setSampleRate(sampleRate);
    drift_.setSampleRate(sampleRate); drift_.setSeed(seed ^ 0x51DEULL); drift_.setRateHz(0.13f);
}

void SupersawVoice::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    channel_ = channel; note_ = note; velocity_ = velocity;
    ampEnv_.noteOn(); filterEnv_.noteOn();
    baseHz_ = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
    targetHz_ = baseHz_;
    if (currentHz_ <= 0.0f || glideSeconds_ <= 0.0f) currentHz_ = baseHz_;

    // PHASES TIRÉES AU SORT. Sept scies démarrant ensemble additionnent leurs
    // fronts : la note commence par un claquement de niveau et le timbre sonne
    // creux tant que le désaccord ne les a pas écartées. Le tirage est
    // déterministe (générateur à graine), donc deux rendus du même projet
    // restent identiques échantillon pour échantillon.
    for (auto& saw : saws_) saw.reset(static_cast<double>(rng_.nextUnipolar()));
    sub_.reset(static_cast<double>(rng_.nextUnipolar()));
}

void SupersawVoice::render(const Params& p, float lfo, float& outL, float& outR) {
    if (!ampEnv_.isActive()) { outL = 0.0f; outR = 0.0f; return; }

    const float filterEnvLevel = filterEnv_.nextSample();
    const float ampLevel = ampEnv_.nextSample();

    // Portamento : glissement EXPONENTIEL, c'est-à-dire linéaire en demi-tons.
    // Un glissement linéaire en hertz passerait beaucoup trop vite dans les
    // graves et traînerait dans les aigus.
    if (glideSeconds_ > 0.0001f && std::abs(currentHz_ - targetHz_) > 0.01f) {
        const float coefficient = 1.0f - std::exp(-1.0f / (glideSeconds_ * static_cast<float>(sampleRate_)));
        currentHz_ += (targetHz_ - currentHz_) * coefficient;
    } else {
        currentHz_ = targetHz_;
    }

    const float drift = drift_.nextValue() * 0.05f;
    // Terme de molette ADDITIF : l'expression d'origine garde son ordre
    // d'association flottant, l'empreinte ne bouge pas à molette nulle.
    const float vibrato = lfo * p.lfoToPitch * 0.5f + lfo * p.wheelVibratoSemis;
    const float rootHz = currentHz_ * std::exp2f((drift + vibrato + p.bendSemitones) / 12.0f);

    // Le réglage de façade passe d'abord par la courbe, puis sert d'échelle
    // aux sept écarts. `detuneCurve` rend une fraction de fréquence, pas des
    // demi-tons : on l'applique donc en multiplicateur.
    const float detune = detuneCurve(std::clamp(p.detune, 0.0f, 1.0f));
    const float side = sideGain(std::clamp(p.mix, 0.0f, 1.0f));
    const float centre = centreGain(std::clamp(p.mix, 0.0f, 1.0f));

    float left = 0.0f, right = 0.0f;
    for (int i = 0; i < kSawCount; ++i) {
        saws_[static_cast<size_t>(i)].setFrequency(rootHz * (1.0f + kDetuneOffsets[i] * detune));
        const float sample = saws_[static_cast<size_t>(i)].nextSample();
        const float gain = (i == kSawCount / 2) ? centre : side;

        // Placement stéréo : les scies les plus désaccordées partent le plus
        // loin sur les côtés, la centrale reste au milieu. C'est ce qui donne
        // au supersaw sa largeur -- une somme mono dupliquée sonnerait plate.
        const float position = kDetuneOffsets[i] / 0.11002313f; // -1..+1
        const float pan = position * std::clamp(p.spread, 0.0f, 1.0f);
        left += sample * gain * (0.5f - pan * 0.5f);
        right += sample * gain * (0.5f + pan * 0.5f);
    }

    if (p.subLevel > 0.0001f) {
        sub_.setFrequency(rootHz * 0.5f);
        const float sub = sub_.nextSample() * p.subLevel;
        left += sub; right += sub;
    }
    if (p.noiseLevel > 0.0001f) {
        const float noise = rng_.nextBipolar() * p.noiseLevel;
        left += noise; right += noise;
    }

    const float velocity = static_cast<float>(velocity_) / 127.0f;
    const float envOctaves = p.envAmount * filterEnvLevel * 5.0f;
    const float lfoOctaves = lfo * p.lfoToFilter * 3.0f;
    const float trackOctaves = p.keyTrack * (static_cast<float>(note_) - 60.0f) / 12.0f;
    const float velocityOctaves = p.velocityToFilter * (velocity - 0.5f) * 2.0f;
    const float cutoff = std::clamp(
        p.cutoff * std::exp2f(envOctaves + lfoOctaves + trackOctaves + velocityOctaves), 20.0f, 18000.0f);
    const float q = 0.707f + p.resonance * 7.0f;

    filterL_.setCutoffHz(cutoff); filterL_.setResonance(q);
    filterR_.setCutoffHz(cutoff); filterR_.setResonance(q);
    left = filterL_.process(left);
    right = filterR_.process(right);

    // Coupe-bas SUIVANT LA NOTE. Sur l'instrument d'origine il est fixe et
    // câblé ; ici il suit la fondamentale, ce qui rend le même service dans
    // tout le registre : nettoyer le bas que sept scies désaccordées
    // accumulent, sans creuser un lead grave. Réglage à 0 = coupe-bas éteint,
    // pour qui veut la basse brute.
    if (p.pitchHpf > 0.0001f) {
        const float hpfHz = std::clamp(rootHz * p.pitchHpf, 20.0f, 8000.0f);
        hpfL_.setCutoffHz(hpfHz); hpfL_.setResonance(0.707f);
        hpfR_.setCutoffHz(hpfHz); hpfR_.setResonance(0.707f);
        left = hpfL_.process(left);
        right = hpfR_.process(right);
    }

    outL = left * ampLevel;
    outR = right * ampLevel;
}

SupersawSynth::SupersawSynth() {
    parameterList_ = {
        {kOscDetune, "Detune", 0.0f, 1.0f, 0.35f, ""},
        {kOscMix, "Mix", 0.0f, 1.0f, 0.55f, ""},
        {kOscSpread, "Stereo Spread", 0.0f, 1.0f, 0.7f, ""},
        {kSubLevel, "Sub Level", 0.0f, 1.0f, 0.0f, ""},
        {kNoiseLevel, "Noise Level", 0.0f, 1.0f, 0.0f, ""},
        {kPitchHpf, "Pitch HPF", 0.0f, 2.0f, 0.75f, ""},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 18000.0f, 7000.0f, "Hz"},
        {kFilterResonance, "Filter Resonance", 0.0f, 1.0f, 0.12f, ""},
        {kFilterEnvAmount, "Filter Env Amount", -1.0f, 1.0f, 0.3f, ""},
        {kFilterKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.35f, ""},
        {kFilterAttack, "Filter Attack", 0.001f, 4.0f, 0.005f, "s"},
        {kFilterDecay, "Filter Decay", 0.001f, 8.0f, 0.6f, "s"},
        {kFilterSustain, "Filter Sustain", 0.0f, 1.0f, 0.5f, ""},
        {kFilterRelease, "Filter Release", 0.001f, 8.0f, 0.4f, "s"},
        {kAmpAttack, "Amp Attack", 0.001f, 4.0f, 0.008f, "s"},
        {kAmpDecay, "Amp Decay", 0.001f, 8.0f, 0.5f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 0.85f, ""},
        {kAmpRelease, "Amp Release", 0.001f, 8.0f, 0.3f, "s"},
        {kLfoRate, "LFO Rate", 0.05f, 20.0f, 5.0f, "Hz"},
        {kLfoToPitch, "LFO to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kLfoToFilter, "LFO to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kGlide, "Glide", 0.0f, 2.0f, 0.0f, "s"},
        {kVelocityToFilter, "Velocity to Filter", 0.0f, 1.0f, 0.25f, ""},
        {kAnalogCharacter, "Analog Character", 0.0f, 1.0f, 0.25f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void SupersawSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](SupersawVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
    lfoPhase_ = 0.0;
    lastHz_ = 0.0f;
}

bool SupersawSynth::handleControlEvent(const MidiControlEvent& event) {
    // Molette de hauteur et molette de modulation (CC 1) ; le reste est
    // refusé en le disant -- le moteur compte le refus.
    if (event.kind == MidiControlEvent::Kind::PitchBend) {
        bendSemitones_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    if (event.kind == MidiControlEvent::Kind::ControlChange && event.index == 1) {
        modWheel_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void SupersawSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float SupersawSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState SupersawSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.supersaw";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void SupersawSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

void SupersawSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        // Le portamento part de la DERNIÈRE hauteur jouée, pas de celle de la
        // voix réattribuée : c'est ce que fait un clavier, et c'est le seul
        // comportement qui donne un glissando cohérent en polyphonie.
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
        const float glide = params_[kGlide].load(std::memory_order_relaxed);
        if (glide > 0.0001f && lastHz_ > 0.0f) {
            voiceManager_.forEachVoice([&](SupersawVoice& voice) {
                if (voice.isActive() && voice.note() == event.note) voice.setGlideFrom(lastHz_);
            });
        }
        lastHz_ = 440.0f * std::exp2f((static_cast<float>(event.note) - 69.0f) / 12.0f);
    } else {
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
    }
}

void SupersawSynth::process(const MidiNoteEvent* events, int numEvents,
                             float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    SupersawVoice::Params p;
    p.detune = params_[kOscDetune].load(std::memory_order_relaxed);
    p.mix = params_[kOscMix].load(std::memory_order_relaxed);
    p.spread = params_[kOscSpread].load(std::memory_order_relaxed);
    p.subLevel = params_[kSubLevel].load(std::memory_order_relaxed);
    p.noiseLevel = params_[kNoiseLevel].load(std::memory_order_relaxed);
    p.pitchHpf = params_[kPitchHpf].load(std::memory_order_relaxed);
    p.cutoff = params_[kFilterCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kFilterResonance].load(std::memory_order_relaxed);
    p.envAmount = params_[kFilterEnvAmount].load(std::memory_order_relaxed);
    p.keyTrack = params_[kFilterKeyTrack].load(std::memory_order_relaxed);
    p.lfoToPitch = params_[kLfoToPitch].load(std::memory_order_relaxed);
    p.lfoToFilter = params_[kLfoToFilter].load(std::memory_order_relaxed);
    p.velocityToFilter = params_[kVelocityToFilter].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);
    p.wheelVibratoSemis = modWheel_.load(std::memory_order_relaxed) * kWheelVibratoSemitones;

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
    const float glide = params_[kGlide].load(std::memory_order_relaxed);
    voiceManager_.forEachVoice([&](SupersawVoice& voice) {
        voice.setSettings(amp, filter);
        voice.setDriftAmount(drift);
        voice.setGlideSeconds(glide);
    });

    const float lfoRate = params_[kLfoRate].load(std::memory_order_relaxed);
    const double lfoIncrement = static_cast<double>(lfoRate) / sampleRate_;
    // Sept scies par voix : sans compensation, une seule note serait déjà plus
    // forte qu'un accord sur n'importe quelle autre machine du parc.
    constexpr float kVoiceGain = 0.16f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        const float lfo = 4.0f * std::abs(static_cast<float>(lfoPhase_) - 0.5f) - 1.0f;

        float sumL = 0.0f, sumR = 0.0f;
        voiceManager_.forEachVoice([&](SupersawVoice& voice) {
            float l = 0.0f, r = 0.0f;
            voice.render(p, lfo, l, r);
            sumL += l; sumR += r;
        });
        outputL[i] = sumL * kVoiceGain;
        outputR[i] = sumR * kVoiceGain;

        lfoPhase_ += lfoIncrement;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.supersaw", "Supersaw Lead", SupersawSynth);

} // namespace vsm::plugins::supersaw
