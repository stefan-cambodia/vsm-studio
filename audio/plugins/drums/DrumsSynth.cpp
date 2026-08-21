#include "DrumsSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>

namespace vsm::plugins::drums {

using namespace vsm::audio::plugin;
using namespace vsm::audio::dsp;

namespace {

/// Zéros de la fonction de Bessel, normalisés au mode fondamental. Ce sont les
/// rapports des modes d'une membrane circulaire idéale, et ils sont
/// IRRATIONNELS : c'est cette inharmonicité qu'on entend comme « peau » plutôt
/// que comme « note ». Une boîte à rythmes analogique n'a pas ces rapports --
/// elle n'a qu'une sinusoïde -- et c'est ce qui la trahit sur un stem
/// acoustique.
constexpr float kMembraneRatios[6] = {1.000f, 1.593f, 2.135f, 2.295f, 2.917f, 3.500f};

/// Cluster métallique : rapports volontairement sans commune mesure, pour
/// qu'aucune périodicité ne s'installe. Une cymbale est un continuum de modes
/// si dense qu'on ne les distingue plus ; six suffisent à ne plus entendre de
/// hauteur.
constexpr float kMetalRatios[6] = {1.000f, 1.414f, 1.732f, 2.174f, 2.639f, 3.142f};

float velocityGain(uint8_t velocity) { return static_cast<float>(velocity) / 127.0f; }

} // namespace

DrumsSynth::DrumsSynth() {
    parameterList_ = {
        {kKickLevel, "Kick Level", 0.0f, 1.0f, 0.9f, ""},
        {kKickTune, "Kick Tune", 35.0f, 90.0f, 52.0f, "Hz"},
        {kKickDecay, "Kick Decay", 0.08f, 1.2f, 0.34f, "s"},
        {kKickBeater, "Kick Beater", 0.0f, 1.0f, 0.5f, ""},
        {kSnareLevel, "Snare Level", 0.0f, 1.0f, 0.8f, ""},
        {kSnareTune, "Snare Tune", 120.0f, 320.0f, 190.0f, "Hz"},
        {kSnareDecay, "Snare Decay", 0.05f, 0.8f, 0.18f, "s"},
        {kSnareWires, "Snare Wires", 0.0f, 1.0f, 0.6f, ""},
        {kTomLevel, "Tom Level", 0.0f, 1.0f, 0.75f, ""},
        {kTomTune, "Tom Tune", 70.0f, 260.0f, 120.0f, "Hz"},
        {kTomDecay, "Tom Decay", 0.1f, 1.5f, 0.45f, "s"},
        {kClosedHatLevel, "Closed Hat Level", 0.0f, 1.0f, 0.6f, ""},
        {kClosedHatDecay, "Closed Hat Decay", 0.01f, 0.2f, 0.055f, "s"},
        {kOpenHatLevel, "Open Hat Level", 0.0f, 1.0f, 0.6f, ""},
        {kOpenHatDecay, "Open Hat Decay", 0.1f, 1.5f, 0.42f, "s"},
        {kRideLevel, "Ride Level", 0.0f, 1.0f, 0.55f, ""},
        {kRideDecay, "Ride Decay", 0.2f, 4.0f, 1.6f, "s"},
        {kCrashLevel, "Crash Level", 0.0f, 1.0f, 0.55f, ""},
        {kCrashDecay, "Crash Decay", 0.3f, 6.0f, 2.4f, "s"},
        {kRoomLevel, "Room Level", 0.0f, 1.0f, 0.3f, ""},
        {kRoomSize, "Room Size", 0.0f, 1.0f, 0.45f, ""},
        {kOutputLevel, "Output Level", 0.0f, 2.0f, 1.0f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void DrumsSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;

    for (Membrane* piece : {&kick_, &snare_, &tom_}) {
        piece->modes.setSampleRate(sampleRate);
        piece->pitchDrop.setSampleRate(sampleRate);
        piece->transient.setSampleRate(sampleRate);
        piece->transientFilter.setSampleRate(sampleRate);
    }
    for (Metal* piece : {&closedHat_, &openHat_, &ride_, &crash_}) {
        piece->modes.setSampleRate(sampleRate);
        piece->noiseEnv.setSampleRate(sampleRate);
        piece->noiseFilter.setSampleRate(sampleRate);
        piece->noiseFilter.setMode(StateVariableFilter::Mode::HighPass);
        piece->noiseFilter.setResonance(0.7f);
    }
    // Graines distinctes : deux pièces qui tireraient le même bruit se
    // superposeraient de façon corrélée et sonneraient comme une seule.
    kick_.rng = vsm::util::DeterministicRng(vsm::util::deriveSeed(0x4452554DULL, 1));
    snare_.rng = vsm::util::DeterministicRng(vsm::util::deriveSeed(0x4452554DULL, 2));
    tom_.rng = vsm::util::DeterministicRng(vsm::util::deriveSeed(0x4452554DULL, 3));
    closedHat_.rng = vsm::util::DeterministicRng(vsm::util::deriveSeed(0x4D4554414CULL, 1));
    openHat_.rng = vsm::util::DeterministicRng(vsm::util::deriveSeed(0x4D4554414CULL, 2));
    ride_.rng = vsm::util::DeterministicRng(vsm::util::deriveSeed(0x4D4554414CULL, 3));
    crash_.rng = vsm::util::DeterministicRng(vsm::util::deriveSeed(0x4D4554414CULL, 4));
    wireRng_ = vsm::util::DeterministicRng(0x574952455AULL);

    snareWires_.setSampleRate(sampleRate);
    wireFilter_.setSampleRate(sampleRate);
    wireFilter_.setMode(StateVariableFilter::Mode::HighPass);
    wireFilter_.setResonance(0.6f);
    wireFilter_.setCutoffHz(1800.0f);

    // Pièce. Longueurs premières entre elles pour que les peignes ne se
    // renforcent pas sur une même période, et différentes à gauche et à droite
    // pour donner de la largeur SANS décorréler la frappe elle-même.
    combLenL_ = static_cast<size_t>(0.0231 * sampleRate);
    combLenR_ = static_cast<size_t>(0.0277 * sampleRate);
    allpassLen_ = static_cast<size_t>(0.0051 * sampleRate);
    combL_.assign(combLenL_ + 1, 0.0f);
    combR_.assign(combLenR_ + 1, 0.0f);
    allpass_.assign(allpassLen_ + 1, 0.0f);
    combIndexL_ = combIndexR_ = allpassIndex_ = 0;
    tomHz_ = 120.0f;
}

void DrumsSynth::setParameter(ParamId id, float value) {
    if (id < params_.size()) params_[id].store(value, std::memory_order_relaxed);
}

float DrumsSynth::getParameter(ParamId id) const {
    return id < params_.size() ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

PresetState DrumsSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.drums";
    for (const auto& info : parameterList_) state.parameterValues[info.id] = getParameter(info.id);
    return state;
}

void DrumsSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

int DrumsSynth::activeVoiceCount() const {
    int count = 0;
    for (const Membrane* piece : {&kick_, &snare_, &tom_})
        if (piece->modes.isActive() || piece->transient.isActive()) ++count;
    for (const Metal* piece : {&closedHat_, &openHat_, &ride_, &crash_})
        if (piece->modes.isActive() || piece->noiseEnv.isActive()) ++count;
    if (snareWires_.isActive()) ++count;
    return count;
}

void DrumsSynth::trigger(uint8_t note, uint8_t velocity) {
    const float gain = velocityGain(velocity);

    auto strikeMembrane = [&](Membrane& piece, float decay, float pitchDecay, float transientDecay) {
        // Les modes hauts s'éteignent avant le fondamental : c'est ce qui fait
        // qu'un coup est riche pendant trente millisecondes puis devient un
        // bourdonnement. Une enveloppe commune donnerait un son de cloche.
        float gains[6], decays[6];
        for (int i = 0; i < 6; ++i) {
            gains[i] = 1.0f / (1.0f + 1.6f * static_cast<float>(i));
            decays[i] = decay / (1.0f + 1.9f * static_cast<float>(i));
        }
        piece.modes.configure(6, kMembraneRatios, gains, decays);
        piece.modes.trigger();
        piece.pitchDrop.setDecaySeconds(pitchDecay);
        piece.pitchDrop.trigger();
        piece.transient.setDecaySeconds(transientDecay);
        piece.transient.trigger();
        piece.velocity = gain;
        piece.triggered = true;
    };

    auto strikeMetal = [&](Metal& piece, float decay) {
        float gains[6], decays[6];
        for (int i = 0; i < 6; ++i) {
            gains[i] = 0.7f / (1.0f + 0.45f * static_cast<float>(i));
            decays[i] = decay / (1.0f + 0.35f * static_cast<float>(i));
        }
        piece.modes.configure(6, kMetalRatios, gains, decays);
        piece.modes.trigger();
        piece.noiseEnv.setDecaySeconds(decay * 0.8f);
        piece.noiseEnv.trigger();
        piece.velocity = gain;
    };

    switch (note) {
        case kNoteKick:
            strikeMembrane(kick_, params_[kKickDecay].load(std::memory_order_relaxed), 0.045f, 0.004f);
            break;
        case kNoteSnare: {
            const float decay = params_[kSnareDecay].load(std::memory_order_relaxed);
            strikeMembrane(snare_, decay, 0.02f, 0.003f);
            // Le timbre est le TIMBRE : les frisés continuent de vibrer après
            // que la peau s'est tue, et c'est ce qui fait la caisse claire.
            snareWires_.setDecaySeconds(decay * 1.45f);
            snareWires_.trigger();
            break;
        }
        case kNoteLowTom:
        case kNoteMidTom:
        case kNoteHighTom: {
            // Une seule pièce pour les trois toms : la NOTE en fixe l'accord,
            // comme sur les boîtes à rythmes du parc. Trois bancs modaux
            // coûteraient trois fois plus pour un réglage que la note donne.
            const float base = params_[kTomTune].load(std::memory_order_relaxed);
            const float factor = note == kNoteLowTom ? 1.0f : (note == kNoteMidTom ? 1.32f : 1.74f);
            tomHz_ = base * factor;
            strikeMembrane(tom_, params_[kTomDecay].load(std::memory_order_relaxed), 0.06f, 0.003f);
            break;
        }
        case kNoteClosedHat:
        case kNotePedalHat:
            strikeMetal(closedHat_, params_[kClosedHatDecay].load(std::memory_order_relaxed));
            // La charleston fermée ÉTOUFFE l'ouverte : deux positions d'un même
            // instrument ne peuvent pas sonner ensemble. Le bruit ET les modes,
            // sans quoi le cluster métallique continuerait par-dessus.
            openHat_.noiseEnv.choke();
            openHat_.modes.choke();
            break;
        case kNoteOpenHat:
            strikeMetal(openHat_, params_[kOpenHatDecay].load(std::memory_order_relaxed));
            break;
        case kNoteRide:
            strikeMetal(ride_, params_[kRideDecay].load(std::memory_order_relaxed));
            break;
        case kNoteCrash:
            strikeMetal(crash_, params_[kCrashDecay].load(std::memory_order_relaxed));
            break;
        default:
            break;
    }
}

float DrumsSynth::renderMembrane(Membrane& piece, float baseHz, float pitchDepth,
                                 float transientLevel, float transientHz, float /*unused*/) {
    if (!piece.modes.isActive() && !piece.transient.isActive()) return 0.0f;
    // Chute de hauteur : une peau frappée se détend en rendant son énergie.
    // Beaucoup plus discrète que le balayage d'une 808, qui est un effet
    // voulu ; ici c'est un détail physique, et l'exagérer sonnerait
    // électronique.
    const float drop = 1.0f + pitchDepth * piece.pitchDrop.next();
    const float body = piece.modes.render(baseHz, drop, piece.velocity);
    // Transitoire : le choc de la mailloche ou de la baguette sur la peau,
    // large bande et très bref.
    piece.transientFilter.setCutoffHz(transientHz);
    piece.transientFilter.setMode(StateVariableFilter::Mode::HighPass);
    const float hit = piece.transientFilter.process(piece.rng.nextBipolar())
                    * piece.transient.next() * transientLevel;
    return (body + hit) * piece.velocity;
}

float DrumsSynth::renderMetal(Metal& piece, float baseHz, float noiseHz) {
    if (!piece.modes.isActive() && !piece.noiseEnv.isActive()) return 0.0f;
    const float cluster = piece.modes.render(baseHz, 1.0f, piece.velocity);
    piece.noiseFilter.setCutoffHz(noiseHz);
    const float hiss = piece.noiseFilter.process(piece.rng.nextBipolar()) * piece.noiseEnv.next();
    // Le bruit domine dans le métal : sans lui on entendrait un carillon.
    return (cluster * 0.35f + hiss * 0.9f) * piece.velocity;
}

void DrumsSynth::process(const MidiNoteEvent* events, int numEvents,
                         float* outputL, float* outputR, int numSamples) {
    ScopedNoDenormals noDenormals;

    const float kickHz = params_[kKickTune].load(std::memory_order_relaxed);
    const float kickLevel = params_[kKickLevel].load(std::memory_order_relaxed);
    const float beater = params_[kKickBeater].load(std::memory_order_relaxed);
    const float snareHz = params_[kSnareTune].load(std::memory_order_relaxed);
    const float snareLevel = params_[kSnareLevel].load(std::memory_order_relaxed);
    const float wires = params_[kSnareWires].load(std::memory_order_relaxed);
    const float tomLevel = params_[kTomLevel].load(std::memory_order_relaxed);
    const float closedLevel = params_[kClosedHatLevel].load(std::memory_order_relaxed);
    const float openLevel = params_[kOpenHatLevel].load(std::memory_order_relaxed);
    const float rideLevel = params_[kRideLevel].load(std::memory_order_relaxed);
    const float crashLevel = params_[kCrashLevel].load(std::memory_order_relaxed);
    const float roomLevel = params_[kRoomLevel].load(std::memory_order_relaxed);
    const float roomSize = std::clamp(params_[kRoomSize].load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float outputLevel = params_[kOutputLevel].load(std::memory_order_relaxed);
    const float feedback = 0.25f + 0.55f * roomSize;

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            const auto& event = events[eventIndex++];
            if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0)
                trigger(event.note, event.velocity);
            // Les percussions ne se relâchent pas : une frappe dure ce que dure
            // sa décroissance, et un NoteOff n'a rien à y faire.
        }

        float dry = 0.0f;
        dry += renderMembrane(kick_, kickHz, 0.45f, beater * 0.9f, 1400.0f, 0.0f) * kickLevel;
        dry += renderMembrane(snare_, snareHz, 0.18f, 0.35f, 2600.0f, 0.0f) * snareLevel;
        dry += renderMembrane(tom_, tomHz_, 0.30f, 0.30f, 2000.0f, 0.0f) * tomLevel;

        if (snareWires_.isActive())
            dry += wireFilter_.process(wireRng_.nextBipolar()) * snareWires_.next()
                 * wires * snareLevel * 0.85f;

        dry += renderMetal(closedHat_, 5200.0f, 7000.0f) * closedLevel;
        dry += renderMetal(openHat_, 4800.0f, 6400.0f) * openLevel;
        dry += renderMetal(ride_, 3600.0f, 5200.0f) * rideLevel;
        dry += renderMetal(crash_, 3100.0f, 4200.0f) * crashLevel;

        // NIVEAU CALIBRÉ SUR UN MOTIF RÉEL, ET SUR LE PARC.
        //
        // Une première version visait « les neuf pièces frappées ensemble à
        // vélocité 127 restent sous 1,0 » et donnait un gain de 0,19. Le
        // critère était trop sévère, et la chaîne complète l'a prouvé : sur
        // House Of God comme sur Children, le calage automatique des volumes
        // demandait un facteur 4,3 pour rattraper la batterie et BUTAIT sur sa
        // borne de 2,5 -- la piste restait deux fois trop faible dans le
        // mélange, et la distance globale s'en ressentait.
        //
        // Mesuré sur un motif ordinaire (grosse caisse, caisse claire et
        // charleston en croches), et comparé au parc :
        //
        //     vsm.drums (gain 0,19)   pic 0,376   rms 0,079
        //     vsm.tr909               pic 0,894   rms 0,234
        //     vsm.tr808               pic 0,746   rms 0,271
        //
        // Les deux boîtes du parc dépassent d'ailleurs 1,0 sur le cas
        // artificiel des neuf pièces simultanées (1,76 et 1,45) : ce cas
        // n'arrive dans aucun motif, et le garantir coûtait un facteur trois
        // de niveau utile. Le gain est donc réglé pour que le MOTIF culmine
        // vers 0,83, ce qui place le niveau efficace dans la fourchette du
        // parc, et c'est le motif que le test verrouille.
        dry *= 0.42f;

        // La pièce : deux peignes de longueurs différentes, puis un passe-tout
        // qui casse la périodicité qu'ils laisseraient. Ce n'est pas une
        // réverbération et ne prétend pas l'être -- c'est ce qui manque à un
        // kit modélisé pour cesser de sonner « électronique ».
        float roomL = 0.0f, roomR = 0.0f;
        if (roomLevel > 0.0f) {
            const float tapL = combL_[combIndexL_];
            const float tapR = combR_[combIndexR_];
            combL_[combIndexL_] = dry + tapL * feedback;
            combR_[combIndexR_] = dry + tapR * feedback;
            combIndexL_ = (combIndexL_ + 1) % combLenL_;
            combIndexR_ = (combIndexR_ + 1) % combLenR_;

            const float apIn = 0.5f * (tapL + tapR);
            const float apTap = allpass_[allpassIndex_];
            const float apOut = -0.5f * apIn + apTap;
            allpass_[allpassIndex_] = apIn + 0.5f * apTap;
            allpassIndex_ = (allpassIndex_ + 1) % allpassLen_;

            roomL = (tapL * 0.6f + apOut * 0.4f) * roomLevel;
            roomR = (tapR * 0.6f + apOut * 0.4f) * roomLevel;
        }

        outputL[i] = (dry + roomL) * outputLevel;
        outputR[i] = (dry + roomR) * outputLevel;
    }
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.drums", "Drums (batterie acoustique)", DrumsSynth);

} // namespace vsm::plugins::drums
