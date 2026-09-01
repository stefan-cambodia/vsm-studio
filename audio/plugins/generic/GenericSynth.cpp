#include "GenericSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::generic {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {
constexpr uint64_t kBaseSeed = 0x47454E45524943ULL; // "GENERIC"
} // namespace

// --- oscillateur morphable ---------------------------------------------------

float MorphOscillator::polyBlep(double t, double dt) {
    if (t < dt) {
        const double x = t / dt;
        return static_cast<float>(x + x - x * x - 1.0);
    }
    if (t > 1.0 - dt) {
        const double x = (t - 1.0) / dt;
        return static_cast<float>(x * x + x + x + 1.0);
    }
    return 0.0f;
}

float MorphOscillator::nextSample(float shape, float pulseWidth) {
    const double dt = static_cast<double>(frequencyHz_) / sampleRate_;
    const double t = phase_;

    // Les quatre formes, toutes calculées à LA MÊME phase. C'est ce qui permet
    // de les fondre : quatre oscillateurs indépendants se peigneraient.
    const auto sinus = static_cast<float>(std::sin(t * kTwoPi));
    const auto triangle = static_cast<float>(4.0 * std::abs(t - 0.5) - 1.0);

    float dent = static_cast<float>(2.0 * t - 1.0);
    dent -= polyBlep(t, dt);

    const double largeur = std::clamp(static_cast<double>(pulseWidth), 0.05, 0.95);
    float carre = (t < largeur) ? 1.0f : -1.0f;
    carre += polyBlep(t, dt);
    carre -= polyBlep(std::fmod(t + (1.0 - largeur), 1.0), dt);

    phase_ += dt;
    if (phase_ >= 1.0) phase_ -= 1.0;

    // FONDU ENTRE LES DEUX FORMES VOISINES. La sortie est continue en `shape`,
    // sans le moindre palier : c'est l'exigence centrale de cette machine, car
    // un saut dans la fonction de coût bloque une recherche par descente.
    const float position = std::clamp(shape, 0.0f, 3.0f);
    const auto index = static_cast<int>(position);
    const float fraction = position - static_cast<float>(index);
    const float formes[4] = {sinus, triangle, dent, carre};
    const float a = formes[std::min(index, 3)];
    const float b = formes[std::min(index + 1, 3)];
    return a + (b - a) * fraction;
}

// --- voix --------------------------------------------------------------------

void GenericVoice::prepare(double sampleRate, uint64_t seed) {
    sampleRate_ = sampleRate;
    rng_ = vsm::util::DeterministicRng{seed};
    osc1_.setSampleRate(sampleRate);
    osc2_.setSampleRate(sampleRate);
    sub_.setSampleRate(sampleRate);
    filter_.setSampleRate(sampleRate);
    filter2_.setSampleRate(sampleRate);
    ampEnv_.setSampleRate(sampleRate);
    filterEnv_.setSampleRate(sampleRate);
}

void GenericVoice::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    channel_ = channel; note_ = note; velocity_ = velocity;
    ampEnv_.noteOn(); filterEnv_.noteOn();
    baseHz_ = 440.0f * std::exp2f((static_cast<float>(note_) - 69.0f) / 12.0f);
    // PHASE REMISE À ZÉRO, et non tirée au sort. Deux rendus du même patch
    // doivent être identiques échantillon pour échantillon : c'est une machine
    // de mesure, la moindre variation ferait du bruit dans la fonction de coût
    // que l'optimiseur prendrait pour un effet de ses réglages.
    osc1_.reset(0.0);
    osc2_.reset(0.0);
    sub_.reset(0.0);
    pinkState_ = 0.0f;
}

float GenericVoice::render(const Params& p, float lfo1, float lfo2) {
    if (!ampEnv_.isActive()) return 0.0f;

    const float filterEnvLevel = filterEnv_.nextSample();
    const float ampLevel = ampEnv_.nextSample();
    const float velocity = static_cast<float>(velocity_) / 127.0f;

    // Terme de molette ADDITIF : l'expression d'origine garde son ordre
    // d'association flottant, l'empreinte ne bouge pas à molette nulle.
    const float vibrato = (lfo1 * p.lfo1ToPitch + lfo2 * p.lfo2ToPitch) * 0.5f
                        + lfo1 * p.wheelVibratoSemis
                        + p.bendSemitones;
    const float pwm = lfo1 * p.lfo1ToPulseWidth * 0.4f;

    // --- sources ------------------------------------------------------------
    const float hz1 = baseHz_ * std::exp2f(vibrato / 12.0f);
    osc1_.setFrequency(hz1);
    float mixe = osc1_.nextSample(p.osc1Shape,
                                   std::clamp(p.osc1PulseWidth + pwm, 0.05f, 0.95f)) * p.osc1Level;

    if (p.osc2Level > 0.0f) {
        const float hz2 = baseHz_ * std::exp2f((vibrato + p.osc2Detune
                                                 + static_cast<float>(p.osc2Octave) * 12.0f) / 12.0f);
        osc2_.setFrequency(hz2);
        mixe += osc2_.nextSample(p.osc2Shape,
                                  std::clamp(p.osc2PulseWidth + pwm, 0.05f, 0.95f)) * p.osc2Level;
    }
    if (p.subLevel > 0.0f) {
        sub_.setFrequency(hz1 * 0.5f);
        // Le sous-oscillateur morphe entre sinus et carré : ce sont les deux
        // seules formes qu'on veut une octave en dessous.
        mixe += sub_.nextSample(p.subShape * 3.0f, 0.5f) * p.subLevel;
    }
    if (p.noiseLevel > 0.0f) {
        const float blanc = rng_.nextBipolar();
        // Bruit ROSE par filtrage à un pôle, fondu continûment avec le blanc :
        // la couleur est un réglage, pas un choix entre deux cases.
        pinkState_ += (blanc - pinkState_) * 0.05f;
        const float rose = pinkState_ * 3.5f;
        mixe += (blanc + (rose - blanc) * std::clamp(p.noiseColour, 0.0f, 1.0f)) * p.noiseLevel;
    }

    // --- filtre -------------------------------------------------------------
    const float envOctaves = p.envAmount * filterEnvLevel * 5.0f;
    const float lfoOctaves = (lfo1 * p.lfo1ToFilter + lfo2 * p.lfo2ToFilter) * 3.0f;
    const float trackOctaves = p.keyTrack * (static_cast<float>(note_) - 60.0f) / 12.0f;
    const float velOctaves = p.velocityToFilter * (velocity - 0.5f) * 2.0f;
    const float cutoff = std::clamp(
        p.cutoff * std::exp2f(envOctaves + lfoOctaves + trackOctaves + velOctaves), 20.0f, 18000.0f);

    // Résonance : de 0,707 (plat) à 8. La borne haute reste sous
    // l'auto-oscillation, qui serait une signature sonore -- exactement ce que
    // cette machine doit éviter.
    const float q = 0.707f + std::clamp(p.resonance, 0.0f, 1.0f) * 7.3f;
    filter_.setCutoffHz(cutoff);
    filter_.setResonance(q);

    const auto trois = filter_.processMulti(mixe);
    // FONDU CONTINU passe-bas -> passe-bande -> passe-haut. Les trois sorties
    // viennent d'un SEUL pas d'état (voir `processMulti`) : les obtenir par
    // trois appels ferait tourner le filtre à triple fréquence.
    const float type = std::clamp(p.filterType, 0.0f, 2.0f);
    const float sorties[3] = {trois.lowPass, trois.bandPass, trois.highPass};
    const auto indexType = static_cast<int>(type);
    const float fractionType = type - static_cast<float>(indexType);
    float filtre = sorties[std::min(indexType, 2)]
                 + (sorties[std::min(indexType + 1, 2)] - sorties[std::min(indexType, 2)]) * fractionType;

    if (p.fourPole) {
        filter2_.setCutoffHz(cutoff);
        filter2_.setResonance(0.707f);
        const auto second = filter2_.processMulti(filtre);
        const float secondes[3] = {second.lowPass, second.bandPass, second.highPass};
        filtre = secondes[std::min(indexType, 2)]
               + (secondes[std::min(indexType + 1, 2)] - secondes[std::min(indexType, 2)]) * fractionType;
    }

    // COMPENSATION DU COUPLAGE résonance -> niveau, et son EXPOSANT EST
    // MESURÉ, pas déduit.
    //
    // Un filtre à variable d'état amplifie autour de sa coupure quand le Q
    // monte ; sans correction, tourner la résonance monterait aussi le volume
    // et l'optimiseur confondrait les deux effets. Mais la hausse du niveau
    // GLOBAL est bien plus faible que celle du pic, puisque seule une bande
    // étroite est accentuée : mesuré sur une dent de scie filtrée à 2 kHz, le
    // niveau efficace passe de 0,170 à 0,206 quand la résonance va de 0 à 1,
    // soit +21 % -- et non le facteur 3,4 qu'une division par la racine du Q
    // aurait retiré. Une première version divisait ainsi et faisait CHUTER le
    // niveau d'un facteur 2,8 : la compensation était pire que le couplage.
    filtre /= std::pow(q / 0.707f, 0.08f);

    // --- sortie -------------------------------------------------------------
    float sortie = filtre * ampLevel;
    const float velocityGain = 1.0f - p.velocityToAmp * (1.0f - velocity);
    sortie *= velocityGain;

    if (p.drive > 0.0f) {
        // Saturation douce, ET COMPENSÉE : à drive nul la fonction doit être
        // rigoureusement l'identité (exigence de neutralité), et monter le
        // drive doit ajouter des harmoniques sans monter le volume, sinon la
        // recherche confondrait « plus saturé » et « plus fort ».
        const float amount = std::clamp(p.drive, 0.0f, 1.0f);
        const float gain = 1.0f + amount * 9.0f;
        const float sature = std::tanh(sortie * gain) / std::tanh(gain);
        sortie = sortie + (sature - sortie) * amount;
    }
    return sortie;
}

// --- machine -----------------------------------------------------------------

GenericSynth::GenericSynth() {
    parameterList_ = {
        {kOsc1Shape, "Osc1 Shape", 0.0f, 3.0f, 2.0f, ""},
        {kOsc1Level, "Osc1 Level", 0.0f, 1.0f, 0.8f, ""},
        {kOsc1PulseWidth, "Osc1 Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kOsc2Shape, "Osc2 Shape", 0.0f, 3.0f, 2.0f, ""},
        {kOsc2Level, "Osc2 Level", 0.0f, 1.0f, 0.0f, ""},
        {kOsc2PulseWidth, "Osc2 Pulse Width", 0.05f, 0.95f, 0.5f, ""},
        {kOsc2Detune, "Osc2 Detune", -12.0f, 12.0f, 0.0f, "st"},
        {kOsc2Octave, "Osc2 Octave", -2.0f, 2.0f, 0.0f, ""},
        {kSubLevel, "Sub Level", 0.0f, 1.0f, 0.0f, ""},
        {kSubShape, "Sub Shape", 0.0f, 1.0f, 0.0f, ""},
        {kNoiseLevel, "Noise Level", 0.0f, 1.0f, 0.0f, ""},
        {kNoiseColour, "Noise Colour", 0.0f, 1.0f, 0.0f, ""},
        {kFilterType, "Filter Type", 0.0f, 2.0f, 0.0f, ""},
        {kFilterCutoff, "Filter Cutoff", 20.0f, 18000.0f, 18000.0f, "Hz"},
        {kFilterResonance, "Filter Resonance", 0.0f, 1.0f, 0.0f, ""},
        {kFilterSlope, "Filter Slope", 0.0f, 1.0f, 0.0f, ""},
        {kFilterEnvAmount, "Filter Env Amount", -1.0f, 1.0f, 0.0f, ""},
        {kFilterKeyTrack, "Filter Key Track", 0.0f, 1.0f, 0.0f, ""},
        {kAmpAttack, "Amp Attack", 0.001f, 4.0f, 0.005f, "s"},
        {kAmpDecay, "Amp Decay", 0.001f, 8.0f, 0.5f, "s"},
        {kAmpSustain, "Amp Sustain", 0.0f, 1.0f, 1.0f, ""},
        {kAmpRelease, "Amp Release", 0.001f, 8.0f, 0.2f, "s"},
        {kFilterAttack, "Filter Attack", 0.001f, 4.0f, 0.005f, "s"},
        {kFilterDecay, "Filter Decay", 0.001f, 8.0f, 0.5f, "s"},
        {kFilterSustain, "Filter Sustain", 0.0f, 1.0f, 1.0f, ""},
        {kFilterRelease, "Filter Release", 0.001f, 8.0f, 0.2f, "s"},
        {kLfo1Rate, "LFO1 Rate", 0.05f, 20.0f, 5.0f, "Hz"},
        {kLfo1Shape, "LFO1 Shape", 0.0f, 2.0f, 0.0f, ""},
        {kLfo1ToPitch, "LFO1 to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kLfo1ToFilter, "LFO1 to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kLfo1ToAmp, "LFO1 to Amp", 0.0f, 1.0f, 0.0f, ""},
        {kLfo1ToPulseWidth, "LFO1 to PWM", 0.0f, 1.0f, 0.0f, ""},
        {kLfo2Rate, "LFO2 Rate", 0.05f, 20.0f, 0.5f, "Hz"},
        {kLfo2Shape, "LFO2 Shape", 0.0f, 2.0f, 0.0f, ""},
        {kLfo2ToPitch, "LFO2 to Pitch", 0.0f, 1.0f, 0.0f, ""},
        {kLfo2ToFilter, "LFO2 to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kVelocityToFilter, "Velocity to Filter", 0.0f, 1.0f, 0.0f, ""},
        {kVelocityToAmp, "Velocity to Amp", 0.0f, 1.0f, 0.0f, ""},
        {kDrive, "Drive", 0.0f, 1.0f, 0.0f, ""},
        {kOutputLevel, "Output Level", 0.0f, 1.0f, 0.8f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void GenericSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    uint64_t index = 0;
    voiceManager_.forEachVoice([&](GenericVoice& voice) {
        voice.prepare(sampleRate, vsm::util::deriveSeed(kBaseSeed, index));
        ++index;
    });
    lfo1Phase_ = 0.0;
    lfo2Phase_ = 0.0;
}

bool GenericSynth::handleControlEvent(const MidiControlEvent& event) {
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
    // L'AFTERTOUCH (pression de canal) dose le même vibrato que la molette :
    // c'est le geste qu'un clavier envoie quand on appuie dans la touche.
    if (event.kind == MidiControlEvent::Kind::ChannelPressure) {
        pressure_.store(event.value, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void GenericSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float GenericSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState GenericSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.generic";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void GenericSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

float GenericSynth::renderLfo(double phase, float shape) const {
    // Forme de LFO elle aussi CONTINUE : triangle -> sinus -> carré adouci.
    // Un sélecteur à trois positions rendrait ce paramètre inutilisable par la
    // recherche, alors qu'il porte un vrai effet.
    const auto triangle = static_cast<float>(4.0 * std::abs(phase - 0.5) - 1.0);
    const auto sinus = static_cast<float>(std::sin(phase * kTwoPi));
    const auto carre = static_cast<float>(std::tanh(std::sin(phase * kTwoPi) * 6.0));
    const float position = std::clamp(shape, 0.0f, 2.0f);
    const auto index = static_cast<int>(position);
    const float fraction = position - static_cast<float>(index);
    const float formes[3] = {triangle, sinus, carre};
    const float a = formes[std::min(index, 2)];
    const float b = formes[std::min(index + 1, 2)];
    return a + (b - a) * fraction;
}

void GenericSynth::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
        voiceManager_.noteOn(event.channel, event.note, event.velocity);
    else
        voiceManager_.noteOff(event.channel, event.note, event.velocity);
}

void GenericSynth::process(const MidiNoteEvent* events, int numEvents,
                            float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    GenericVoice::Params p;
    p.osc1Shape = params_[kOsc1Shape].load(std::memory_order_relaxed);
    p.osc1Level = params_[kOsc1Level].load(std::memory_order_relaxed);
    p.osc1PulseWidth = params_[kOsc1PulseWidth].load(std::memory_order_relaxed);
    p.osc2Shape = params_[kOsc2Shape].load(std::memory_order_relaxed);
    p.osc2Level = params_[kOsc2Level].load(std::memory_order_relaxed);
    p.osc2PulseWidth = params_[kOsc2PulseWidth].load(std::memory_order_relaxed);
    p.osc2Detune = params_[kOsc2Detune].load(std::memory_order_relaxed);
    p.osc2Octave = static_cast<int>(std::lround(params_[kOsc2Octave].load(std::memory_order_relaxed)));
    p.subLevel = params_[kSubLevel].load(std::memory_order_relaxed);
    p.subShape = params_[kSubShape].load(std::memory_order_relaxed);
    p.noiseLevel = params_[kNoiseLevel].load(std::memory_order_relaxed);
    p.noiseColour = params_[kNoiseColour].load(std::memory_order_relaxed);
    p.filterType = params_[kFilterType].load(std::memory_order_relaxed);
    p.cutoff = params_[kFilterCutoff].load(std::memory_order_relaxed);
    p.resonance = params_[kFilterResonance].load(std::memory_order_relaxed);
    p.fourPole = params_[kFilterSlope].load(std::memory_order_relaxed) >= 0.5f;
    p.envAmount = params_[kFilterEnvAmount].load(std::memory_order_relaxed);
    p.keyTrack = params_[kFilterKeyTrack].load(std::memory_order_relaxed);
    p.lfo1ToPitch = params_[kLfo1ToPitch].load(std::memory_order_relaxed);
    p.lfo1ToFilter = params_[kLfo1ToFilter].load(std::memory_order_relaxed);
    p.lfo1ToAmp = params_[kLfo1ToAmp].load(std::memory_order_relaxed);
    p.lfo1ToPulseWidth = params_[kLfo1ToPulseWidth].load(std::memory_order_relaxed);
    p.lfo2ToPitch = params_[kLfo2ToPitch].load(std::memory_order_relaxed);
    p.lfo2ToFilter = params_[kLfo2ToFilter].load(std::memory_order_relaxed);
    p.drive = params_[kDrive].load(std::memory_order_relaxed);
    p.velocityToFilter = params_[kVelocityToFilter].load(std::memory_order_relaxed);
    p.velocityToAmp = params_[kVelocityToAmp].load(std::memory_order_relaxed);
    p.bendSemitones = bendSemitones_.load(std::memory_order_relaxed);
    p.wheelVibratoSemis = std::min(1.0f, modWheel_.load(std::memory_order_relaxed)
                                     + pressure_.load(std::memory_order_relaxed)) * kWheelVibratoSemitones;

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
    voiceManager_.forEachVoice([&](GenericVoice& voice) { voice.setSettings(amp, filter); });

    const float lfo1Rate = params_[kLfo1Rate].load(std::memory_order_relaxed);
    const float lfo2Rate = params_[kLfo2Rate].load(std::memory_order_relaxed);
    const float lfo1Shape = params_[kLfo1Shape].load(std::memory_order_relaxed);
    const float lfo2Shape = params_[kLfo2Shape].load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    const double lfo1Increment = static_cast<double>(lfo1Rate) / sampleRate_;
    const double lfo2Increment = static_cast<double>(lfo2Rate) / sampleRate_;
    // Niveau calibré par mesure : à 0,30 un accord de huit notes à vélocité
    // 110 crêtait à 2,10, donc écrêtait franchement.
    constexpr float kVoiceGain = 0.13f;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        const float lfo1 = renderLfo(lfo1Phase_, lfo1Shape);
        const float lfo2 = renderLfo(lfo2Phase_, lfo2Shape);

        float sum = 0.0f;
        voiceManager_.forEachVoice([&](GenericVoice& voice) { sum += voice.render(p, lfo1, lfo2); });

        // Trémolo appliqué au bus : le LFO vers l'amplitude module le NIVEAU,
        // pas l'enveloppe -- deux effets distincts qu'il ne faut pas mêler.
        const float tremolo = 1.0f - params_[kLfo1ToAmp].load(std::memory_order_relaxed)
                                       * 0.5f * (1.0f - lfo1);
        sum *= kVoiceGain * outputLevel * tremolo;
        outputL[i] = sum;
        outputR[i] = sum;

        lfo1Phase_ += lfo1Increment;
        if (lfo1Phase_ >= 1.0) lfo1Phase_ -= 1.0;
        lfo2Phase_ += lfo2Increment;
        if (lfo2Phase_ >= 1.0) lfo2Phase_ -= 1.0;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.generic", "Generic Synth", GenericSynth);

} // namespace vsm::plugins::generic
