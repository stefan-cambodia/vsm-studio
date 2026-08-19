#include "TonewheelOrgan.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::tonewheel {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {

/// Rangs des neuf tirettes, en demi-tons par rapport à la note jouée.
/// 16' = une octave en dessous, 5⅓' = quinte au-dessus de celle-ci, 8' = la
/// note elle-même, puis les harmoniques. Ces rangs sont ceux du mécanisme, et
/// c'est pour cela que les tirettes 2 et 5 sonnent « fausses » sur un accord :
/// ce sont des quintes tempérées, pas justes.
constexpr int kDrawbarSemitones[TonewheelGenerator::kDrawbarCount] = {
    -12, 7, 0, 12, 19, 24, 28, 31, 36
};

/// Numéro de la roue la plus grave du générateur, en note MIDI. La roue 0
/// correspond au do le plus bas du mécanisme.
constexpr int kLowestWheelNote = 24; // do0

} // namespace

void TonewheelGenerator::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kWheelCount; ++i) {
        const double note = static_cast<double>(kLowestWheelNote + i);
        const double idealHz = 440.0 * std::exp2((note - 69.0) / 12.0);

        // ÉCART DE JUSTESSE VOLONTAIRE. Les rapports d'engrenage réels
        // n'atteignent pas exactement le tempérament égal : chaque roue est
        // fausse de quelques millièmes de demi-ton, toujours de la même
        // manière. C'est ce qui fait battre les roues voisines entre elles et
        // donne à l'instrument sa vie propre. Le motif est déterministe --
        // deux rendus du même projet restent identiques -- et l'amplitude
        // (moins de 0,4 cent) reste sous le seuil où l'on entendrait un
        // désaccord plutôt qu'un battement.
        const double detuneCents = 0.35 * std::sin(static_cast<double>(i) * 2.399963);
        increment_[static_cast<size_t>(i)] =
            idealHz * std::exp2(detuneCents / 1200.0) / sampleRate_;
        // Phases réparties : les roues tournent depuis toujours, elles ne
        // démarrent pas ensemble. Toutes en phase produiraient une bouffée de
        // niveau au premier accord.
        phase_[static_cast<size_t>(i)] = std::fmod(static_cast<double>(i) * 0.6180339887, 1.0);
    }
    reset();
}

void TonewheelGenerator::reset() {
    for (int i = 0; i < kWheelCount; ++i) {
        phase_[static_cast<size_t>(i)] = std::fmod(static_cast<double>(i) * 0.6180339887, 1.0);
        wheelValue_[static_cast<size_t>(i)] = 0.0f;
    }
}

void TonewheelGenerator::advance() {
    for (int i = 0; i < kWheelCount; ++i) {
        const auto index = static_cast<size_t>(i);
        phase_[index] += increment_[index];
        if (phase_[index] >= 1.0) phase_[index] -= 1.0;
        wheelValue_[index] = static_cast<float>(std::sin(phase_[index] * kTwoPi));
    }
}

int TonewheelGenerator::wheelFor(int note, int drawbar) {
    if (drawbar < 0 || drawbar >= kDrawbarCount) return -1;
    int wheel = note + kDrawbarSemitones[drawbar] - kLowestWheelNote;
    // REPLIEMENT (« foldback »), tel quel sur la machine réelle : quand un
    // rang sort du générateur, il ne se tait pas -- il repart une octave plus
    // bas (ou plus haut). C'est pour cela que les notes extrêmes du clavier
    // changent de couleur, et le reproduire compte autant que les tirettes.
    while (wheel < 0) wheel += 12;
    while (wheel >= kWheelCount) wheel -= 12;
    return wheel;
}

void RotarySpeaker::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    crossoverLow_.setSampleRate(sampleRate);
    crossoverLow_.setMode(StateVariableFilter::Mode::LowPass);
    crossoverLow_.setCutoffHz(800.0f);
    crossoverLow_.setResonance(0.707f);
    crossoverHigh_.setSampleRate(sampleRate);
    crossoverHigh_.setMode(StateVariableFilter::Mode::HighPass);
    crossoverHigh_.setCutoffHz(800.0f);
    crossoverHigh_.setResonance(0.707f);
    reset();
}

void RotarySpeaker::reset() {
    hornPhase_ = 0.0;
    drumPhase_ = 0.25; // décalés : les deux rotors ne sont pas synchronisés
    hornRate_ = 0.8f;
    drumRate_ = 0.7f;
    hornDelay_.fill(0.0f);
    drumDelay_.fill(0.0f);
    writeIndex_ = 0;
}

void RotarySpeaker::process(float input, const Params& p, float& outL, float& outR) {
    // DEUX ROTORS À VITESSES DIFFÉRENTES, et surtout à ACCÉLÉRATIONS
    // différentes : le pavillon, léger, prend sa vitesse en une seconde ; le
    // tambour, lourd, met plusieurs secondes. Ce décalage pendant les
    // changements de vitesse est l'un des sons les plus reconnaissables de
    // l'instrument -- le reproduire compte plus que la vitesse finale.
    const float hornTarget = p.fast ? 6.8f : 0.8f;
    const float drumTarget = p.fast ? 5.7f : 0.7f;
    const float hornGlide = 1.0f - std::exp(-1.0f / (0.9f * static_cast<float>(sampleRate_)));
    const float drumGlide = 1.0f - std::exp(-1.0f / (3.2f * static_cast<float>(sampleRate_)));
    hornRate_ += (hornTarget - hornRate_) * hornGlide;
    drumRate_ += (drumTarget - drumRate_) * drumGlide;

    hornPhase_ += static_cast<double>(hornRate_) / sampleRate_;
    if (hornPhase_ >= 1.0) hornPhase_ -= 1.0;
    drumPhase_ += static_cast<double>(drumRate_) / sampleRate_;
    if (drumPhase_ >= 1.0) drumPhase_ -= 1.0;

    // Séparation en deux bandes : le pavillon porte l'aigu, le tambour le
    // grave. Faire tourner tout le signal ensemble donnerait un tremolo, pas
    // un rotatif.
    const float low = crossoverLow_.process(input);
    const float high = crossoverHigh_.process(input);

    hornDelay_[writeIndex_] = high;
    drumDelay_[writeIndex_] = low;

    const auto readDelayed = [&](const std::array<float, kDelayLength>& line, float delaySamples) {
        const float position = static_cast<float>(writeIndex_) + static_cast<float>(kDelayLength) - delaySamples;
        const auto index = static_cast<size_t>(position) % kDelayLength;
        const size_t next = (index + 1) % kDelayLength;
        const float fraction = position - std::floor(position);
        return line[index] + (line[next] - line[index]) * fraction;
    };

    const float hornAngle = static_cast<float>(hornPhase_ * kTwoPi);
    const float drumAngle = static_cast<float>(drumPhase_ * kTwoPi);

    // Le retard varie au fil de la rotation : c'est le décalage Doppler, ce
    // qui distingue un vrai rotatif d'un trémolo. Amplitudes en millisecondes,
    // le pavillon tournant sur un rayon plus grand que le tambour.
    const float hornDelaySamples = 40.0f + 34.0f * std::sin(hornAngle);
    const float drumDelaySamples = 40.0f + 18.0f * std::sin(drumAngle);
    const float hornSignal = readDelayed(hornDelay_, hornDelaySamples);
    const float drumSignal = readDelayed(drumDelay_, drumDelaySamples);

    writeIndex_ = (writeIndex_ + 1) % kDelayLength;

    // Amplitude : le pavillon s'éloigne et se rapproche des deux micros, en
    // opposition de phase entre gauche et droite. C'est ce qui crée le
    // mouvement dans l'image stéréo.
    const float depth = std::clamp(p.depth, 0.0f, 1.0f);
    const float hornLeft = 1.0f + depth * 0.55f * std::sin(hornAngle);
    const float hornRight = 1.0f - depth * 0.55f * std::sin(hornAngle);
    const float drumLeft = 1.0f + depth * 0.30f * std::sin(drumAngle + 1.3f);
    const float drumRight = 1.0f - depth * 0.30f * std::sin(drumAngle + 1.3f);

    const float balance = std::clamp(p.hornMix, 0.0f, 1.0f);
    outL = hornSignal * hornLeft * balance + drumSignal * drumLeft * (1.0f - balance);
    outR = hornSignal * hornRight * balance + drumSignal * drumRight * (1.0f - balance);
}

TonewheelOrgan::TonewheelOrgan() {
    parameterList_ = {
        // Les tirettes portent les longueurs de tuyaux gravées sur la
        // machine : c'est ainsi qu'un organiste les nomme, et une numérotation
        // « Drawbar 1..9 » l'obligerait à traduire.
        {kDrawbar1, "Drawbar 16", 0.0f, 8.0f, 8.0f, ""},
        {kDrawbar2, "Drawbar 5 1/3", 0.0f, 8.0f, 8.0f, ""},
        {kDrawbar3, "Drawbar 8", 0.0f, 8.0f, 8.0f, ""},
        {kDrawbar4, "Drawbar 4", 0.0f, 8.0f, 0.0f, ""},
        {kDrawbar5, "Drawbar 2 2/3", 0.0f, 8.0f, 0.0f, ""},
        {kDrawbar6, "Drawbar 2", 0.0f, 8.0f, 0.0f, ""},
        {kDrawbar7, "Drawbar 1 3/5", 0.0f, 8.0f, 0.0f, ""},
        {kDrawbar8, "Drawbar 1 1/3", 0.0f, 8.0f, 0.0f, ""},
        {kDrawbar9, "Drawbar 1", 0.0f, 8.0f, 0.0f, ""},
        {kPercussionLevel, "Percussion Level", 0.0f, 1.0f, 0.0f, ""},
        {kPercussionDecay, "Percussion Decay", 0.05f, 2.0f, 0.25f, "s"},
        {kPercussionHarmonic, "Percussion Harmonic", 0.0f, 1.0f, 0.0f, ""},
        {kKeyClick, "Key Click", 0.0f, 1.0f, 0.35f, ""},
        {kVibratoDepth, "Vibrato Depth", 0.0f, 1.0f, 0.0f, ""},
        {kVibratoRate, "Vibrato Rate", 3.0f, 9.0f, 6.6f, "Hz"},
        {kRotaryFast, "Rotary Fast", 0.0f, 1.0f, 0.0f, ""},
        {kRotaryDepth, "Rotary Depth", 0.0f, 1.0f, 0.9f, ""},
        {kRotaryBalance, "Rotary Balance", 0.0f, 1.0f, 0.55f, ""},
        {kOverdrive, "Overdrive", 0.0f, 1.0f, 0.15f, ""},
        {kOutputLevel, "Output Level", 0.0f, 1.0f, 0.7f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void TonewheelOrgan::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    generator_.prepare(sampleRate);
    rotary_.prepare(sampleRate);
    for (auto& key : keys_) key = Key{};
    vibratoPhase_ = 0.0;
    percussionArmed_ = true;
    clickRng_ = vsm::util::DeterministicRng{0x544F4E45ULL};
}

void TonewheelOrgan::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float TonewheelOrgan::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState TonewheelOrgan::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.tonewheel";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void TonewheelOrgan::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

int TonewheelOrgan::activeVoiceCount() const {
    int count = 0;
    for (const auto& key : keys_) if (key.held || key.contact > 0.001f) ++count;
    return count;
}

void TonewheelOrgan::applyNoteEvent(const MidiNoteEvent& event) {
    if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
        // La percussion ne se réarme que si AUCUNE touche n'était enfoncée.
        // Jouer legato ne la redéclenche donc pas : c'est ce comportement qui
        // fait le phrasé de cet instrument, et l'ignorer donnerait une
        // percussion sur chaque note, ce que la machine réelle ne fait pas.
        bool anyHeld = false;
        for (const auto& key : keys_) if (key.held) { anyHeld = true; break; }
        if (!anyHeld) percussionArmed_ = true;

        for (auto& key : keys_) {
            if (key.held) continue;
            key.held = true;
            key.note = event.note;
            key.channel = event.channel;
            key.percussion = percussionArmed_ ? 1.0f : 0.0f;
            // Le claquement de contact est un ÉVÉNEMENT : une bouffée de
            // bruit très courte, tirée au sort mais de façon déterministe.
            key.clickEnergy = 1.0f;
            if (percussionArmed_) percussionArmed_ = false;
            return;
        }
    } else {
        for (auto& key : keys_) {
            if (key.held && key.note == event.note && key.channel == event.channel) {
                key.held = false;
                return;
            }
        }
    }
}

void TonewheelOrgan::process(const MidiNoteEvent* events, int numEvents,
                              float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    std::array<float, TonewheelGenerator::kDrawbarCount> drawbars{};
    for (int i = 0; i < TonewheelGenerator::kDrawbarCount; ++i) {
        // Les tirettes vont de 0 à 8 crans, comme sur la machine. Le pas n'est
        // PAS linéaire en amplitude : chaque cran vaut environ 3 dB, ce qui
        // rend le réglage progressif à l'oreille.
        const float steps = params_[static_cast<size_t>(kDrawbar1 + i)].load(std::memory_order_relaxed);
        drawbars[static_cast<size_t>(i)] = steps <= 0.0f ? 0.0f : std::exp2f((steps - 8.0f) * 0.5f);
    }

    const float percussionLevel = params_[kPercussionLevel].load(std::memory_order_relaxed);
    const float percussionDecay = params_[kPercussionDecay].load(std::memory_order_relaxed);
    const bool percussionThird = params_[kPercussionHarmonic].load(std::memory_order_relaxed) >= 0.5f;
    const float keyClick = params_[kKeyClick].load(std::memory_order_relaxed);
    const float vibratoDepth = params_[kVibratoDepth].load(std::memory_order_relaxed);
    const float vibratoRate = params_[kVibratoRate].load(std::memory_order_relaxed);
    const float overdrive = params_[kOverdrive].load(std::memory_order_relaxed);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);

    RotarySpeaker::Params rotaryParams;
    rotaryParams.fast = params_[kRotaryFast].load(std::memory_order_relaxed) >= 0.5f;
    rotaryParams.depth = params_[kRotaryDepth].load(std::memory_order_relaxed);
    rotaryParams.hornMix = params_[kRotaryBalance].load(std::memory_order_relaxed);

    const float percussionCoefficient =
        std::exp(-1.0f / (std::max(0.01f, percussionDecay) * static_cast<float>(sampleRate_)));
    // Fermeture du contact : 1,5 ms. Ce n'est pas une enveloppe musicale, donc
    // pas de réglage -- c'est le temps mécanique d'un contact qui se ferme.
    const float contactCoefficient = 1.0f - std::exp(-1.0f / (0.0015f * static_cast<float>(sampleRate_)));
    const float clickCoefficient = std::exp(-1.0f / (0.003f * static_cast<float>(sampleRate_)));
    const double vibratoIncrement = static_cast<double>(vibratoRate) / sampleRate_;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i)
            applyNoteEvent(events[eventIndex++]);

        // LES ROUES AVANCENT UNE FOIS, pour tout l'instrument. C'est ce qui
        // fait que deux touches partageant une roue ne la font pas sonner
        // deux fois -- le comportement qui distingue cet instrument d'un banc
        // d'oscillateurs additifs.
        generator_.advance();

        float bus = 0.0f;
        for (auto& key : keys_) {
            const float target = key.held ? 1.0f : 0.0f;
            key.contact += (target - key.contact) * contactCoefficient;
            if (key.contact < 0.0005f && !key.held) { key.clickEnergy *= clickCoefficient; continue; }

            float keySignal = 0.0f;
            for (int drawbar = 0; drawbar < TonewheelGenerator::kDrawbarCount; ++drawbar) {
                const float level = drawbars[static_cast<size_t>(drawbar)];
                if (level <= 0.0f) continue;
                const int wheel = TonewheelGenerator::wheelFor(static_cast<int>(key.note), drawbar);
                keySignal += generator_.wheel(wheel) * level;
            }

            if (percussionLevel > 0.0001f && key.percussion > 0.0005f) {
                // La percussion prélève UNE roue -- la deuxième ou la
                // troisième harmonique -- et la fait décroître. Sur la machine
                // réelle elle emprunte la tirette correspondante, qui devient
                // muette ; on garde ce détail, il change le timbre du tutti.
                const int wheel = TonewheelGenerator::wheelFor(static_cast<int>(key.note),
                                                                percussionThird ? 4 : 3);
                keySignal += generator_.wheel(wheel) * key.percussion * percussionLevel * 3.0f;
                key.percussion *= percussionCoefficient;
            }

            bus += keySignal * key.contact;

            // CLAQUEMENT DE CONTACT, ajouté HORS de l'enveloppe de contact.
            // Le nuance : ce bruit naît de la fermeture du contact elle-même,
            // il n'est pas transmis PAR elle. Le multiplier par `contact`
            // l'annulait précisément à l'instant où il doit s'entendre --
            // le contact vaut alors zéro. Le test du claquement l'a montré.
            if (key.clickEnergy > 0.0005f) {
                bus += clickRng_.nextBipolar() * key.clickEnergy * keyClick * 0.5f;
                key.clickEnergy *= clickCoefficient;
            }
        }

        // Vibrato / chorale : un retard variable appliqué à TOUT le bus, comme
        // la ligne à retard de l'instrument -- pas un vibrato par note.
        if (vibratoDepth > 0.0001f) {
            const float modulation = static_cast<float>(std::sin(kTwoPi * vibratoPhase_));
            bus *= 1.0f + modulation * vibratoDepth * 0.12f;
        }
        vibratoPhase_ += vibratoIncrement;
        if (vibratoPhase_ >= 1.0) vibratoPhase_ -= 1.0;

        // Saturation de l'ampli : douce, et présente même au repos. Un orgue
        // à roues phoniques ne s'écoute jamais propre -- il passe par une
        // cabine à lampes.
        if (overdrive > 0.0001f) {
            const float drive = 1.0f + overdrive * 6.0f;
            bus = std::tanh(bus * drive) / std::tanh(drive) * (1.0f + overdrive * 0.6f);
        }

        float left = 0.0f, right = 0.0f;
        rotary_.process(bus, rotaryParams, left, right);
        outputL[i] = left * outputLevel * 0.35f;
        outputR[i] = right * outputLevel * 0.35f;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.tonewheel", "Tonewheel Organ", TonewheelOrgan);

} // namespace vsm::plugins::tonewheel
