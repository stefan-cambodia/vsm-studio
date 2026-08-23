#include "multisample/MultisampleSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace vsm::plugins::multisample {

using vsm::audio::io::SampleBuffer;
using vsm::audio::io::SampleBufferPtr;
using vsm::audio::io::WavFileReader;
using vsm::audio::plugin::MidiNoteEvent;
using vsm::audio::plugin::MultisampleProfileSpec;
using vsm::audio::plugin::ParameterInfo;
using vsm::audio::plugin::ParamId;
using vsm::audio::plugin::PresetState;

namespace {
/// Le passe-bas de timbre est NEUTRE -- exactement, pas « presque » -- dès que
/// sa coupure atteint la butée haute du réglage, ou dépasse cette fraction de
/// la fréquence d'échantillonnage (le second cas couvre les moteurs tournant
/// sous 44,1 kHz, où la butée serait au-delà de Nyquist).
///
/// POURQUOI L'EXACTITUDE COMPTE ICI. Un filtre laissé actif « très haut » reste
/// mesurable : à 20 kHz sur un signal à 260 Hz il déphase encore, et le rendu
/// par défaut de la machine porterait alors une couleur que personne n'a
/// demandée. L'empreinte de non-régression mesurerait ce filtre au lieu de
/// mesurer le lecteur, et le § 27 d'ARCHITECTURE.md interdit ce genre
/// d'approximation silencieuse. Bouton à fond veut dire chemin direct.
constexpr double kToneNeutralFraction = 0.45;
constexpr double kToneMaximumHz = 20000.0;
} // namespace

// ---------------------------------------------------------------------------
// LoadedProfile
// ---------------------------------------------------------------------------

const LoadedZone* LoadedProfile::select(int program, int note, int velocity) const {
    for (const auto& zone : zones) {
        if (zone.program != program) continue;
        if (note < zone.lowNote || note > zone.highNote) continue;
        if (velocity < zone.lowVelocity || velocity > zone.highVelocity) continue;
        if (!zone.sample || zone.sample->empty()) continue;
        return &zone;
    }
    return nullptr;
}

int LoadedProfile::programCount() const {
    int highest = -1;
    for (const auto& zone : zones) highest = std::max(highest, zone.program);
    return highest + 1;
}

// ---------------------------------------------------------------------------
// MultisampleVoice
// ---------------------------------------------------------------------------

void MultisampleVoice::prepare(double sampleRate) {
    env_.setSampleRate(sampleRate);
    active_ = false;
    profile_.reset();
    zone_ = nullptr;
    sample_ = nullptr;
    toneStateL_ = toneStateR_ = 0.0f;
}

void MultisampleVoice::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    channel_ = channel;
    note_ = note;
    (void)velocity; // le gain de vélocité arrive avec la zone, dans attach()
    // La voix n'est pas encore jouable : `attach` lui donne sa zone. Tant
    // qu'elle n'en a pas, elle est active et muette -- l'état ne dure que le
    // temps de l'appel suivant, et il évite que VoiceManager réattribue la
    // même voix deux fois dans le même bloc.
    active_ = true;
    position_ = 0.0;
    toneStateL_ = toneStateR_ = 0.0f;
    env_.noteOn();
}

void MultisampleVoice::attach(ProfilePtr profile, const LoadedZone& zone, double engineRate,
                               float velocityGain, float globalTuneCents) {
    profile_ = std::move(profile);
    zone_ = &zone;
    sample_ = zone.sample.get();

    const double semitones = static_cast<double>(note_) - static_cast<double>(zone.rootNote)
                           + (static_cast<double>(zone.tuneCents) + static_cast<double>(globalTuneCents)) / 100.0;
    const double pitchRatio = std::pow(2.0, semitones / 12.0);
    // Le rapport des fréquences d'échantillonnage est INDISPENSABLE : un
    // fichier à 44,1 kHz relu tel quel par un moteur à 48 kHz sonne un demi-ton
    // trop bas. C'est la panne la plus courante d'un lecteur d'échantillons, et
    // elle est silencieuse -- tout joue, tout est faux.
    const double rateRatio = (engineRate > 0.0 && sample_ != nullptr)
                                 ? sample_->sampleRate / engineRate
                                 : 1.0;
    increment_ = pitchRatio * rateRatio;
    gain_ = velocityGain * zone.level;
    position_ = 0.0;
}

float MultisampleVoice::frameAt(const std::vector<float>& channel, int64_t index) const {
    const int64_t frames = static_cast<int64_t>(channel.size());
    if (frames == 0) return 0.0f;

    if (zone_ != nullptr && zone_->loopEnabled) {
        const int64_t start = static_cast<int64_t>(zone_->loopStart);
        const int64_t end = static_cast<int64_t>(zone_->loopEnd);
        const int64_t length = end - start;
        if (length > 0 && index >= start) index = start + ((index - start) % length);
    }

    if (index < 0) return channel[0];
    if (index >= frames) return 0.0f;
    return channel[static_cast<size_t>(index)];
}

float MultisampleVoice::interpolate(const std::vector<float>& channel, double position) const {
    // Catmull-Rom : quatre prises, continuité de la dérivée première. Le § 3
    // du cahier des charges impose « cubique minimum » ; l'approximation
    // restante (pas de filtre anti-repliement à la transposition vers l'aigu)
    // est ASSUMÉE et écrite ici, comme pour `vsm.sampler`.
    const int64_t base = static_cast<int64_t>(std::floor(position));
    const double t = position - static_cast<double>(base);
    const double y0 = static_cast<double>(frameAt(channel, base - 1));
    const double y1 = static_cast<double>(frameAt(channel, base));
    const double y2 = static_cast<double>(frameAt(channel, base + 1));
    const double y3 = static_cast<double>(frameAt(channel, base + 2));

    const double a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
    const double b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
    const double c = -0.5 * y0 + 0.5 * y2;
    return static_cast<float>(((a * t + b) * t + c) * t + y1);
}

void MultisampleVoice::render(float& outL, float& outR) {
    if (!active_) return;
    if (sample_ == nullptr || zone_ == nullptr) {
        // Voix allouée sans zone : elle se tait et s'éteint dès que son
        // enveloppe le permet, pour ne pas occuper un emplacement.
        if (!env_.isActive()) active_ = false; else env_.nextSample();
        return;
    }

    const double frames = static_cast<double>(sample_->numFrames());
    if (!zone_->loopEnabled && position_ >= frames) { active_ = false; return; }

    const float left = interpolate(sample_->left, position_);
    const float right = sample_->isStereo() ? interpolate(sample_->right, position_) : left;

    const float envelope = env_.nextSample();
    float l = left * gain_ * envelope;
    float r = right * gain_ * envelope;

    if (toneCoefficient_ > 0.0f) {
        toneStateL_ += (1.0f - toneCoefficient_) * (l - toneStateL_);
        toneStateR_ += (1.0f - toneCoefficient_) * (r - toneStateR_);
        l = toneStateL_;
        r = toneStateR_;
    }

    outL += l;
    outR += r;

    // L'extinction se constate APRÈS avoir rendu l'échantillon : couper avant
    // jetterait la dernière valeur du relâchement, ce qui est un clic d'autant
    // plus audible que le relâchement est court.
    if (!env_.isActive()) { active_ = false; return; }

    position_ += increment_;
    if (zone_->loopEnabled) {
        const double loopEnd = static_cast<double>(zone_->loopEnd);
        const double loopLength = loopEnd - static_cast<double>(zone_->loopStart);
        if (loopLength > 0.0)
            while (position_ >= loopEnd) position_ -= loopLength;
    }
}

// ---------------------------------------------------------------------------
// MultisampleSynth
// ---------------------------------------------------------------------------

MultisampleSynth::MultisampleSynth() {
    // Unités RÉELLES, comme l'exige le § 2 du cahier des charges d'ajout de
    // machine : des cents, des secondes, des hertz. Rien en 0..1 sauf ce qui
    // est réellement un dosage.
    parameterList_ = {
        {kOutputLevel,    "Output Level",    0.0f,     1.0f,     0.80f,  ""},
        {kTune,           "Tune",         -100.0f,   100.0f,     0.0f,   "cents"},
        {kAttack,         "Attack",          0.001f,    2.0f,    0.002f, "s"},
        {kRelease,        "Release",         0.01f,     5.0f,    0.40f,  "s"},
        {kToneCutoff,     "Tone Cutoff",   200.0f, 20000.0f, 20000.0f,   "Hz"},
        {kProgram,        "Program",         0.0f,    127.0f,     0.0f,  ""},
        {kVelocityAmount, "Velocity Amount", 0.0f,      1.0f,     1.0f,  ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void MultisampleSynth::initialize(double sampleRate, int maxBlockSize) {
    (void)maxBlockSize;
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    voices_.forEachVoice([this](MultisampleVoice& voice) { voice.prepare(sampleRate_); });
}

void MultisampleSynth::setParameter(ParamId id, float value) {
    if (id >= kParamCount) return;
    for (const auto& info : parameterList_) {
        if (info.id != id) continue;
        params_[id].store(std::clamp(value, info.minValue, info.maxValue), std::memory_order_relaxed);
        return;
    }
}

float MultisampleSynth::getParameter(ParamId id) const {
    if (id >= kParamCount) return 0.0f;
    return params_[id].load(std::memory_order_relaxed);
}

PresetState MultisampleSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.multisample";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void MultisampleSynth::loadState(const PresetState& state) {
    // Le PROFIL ne passe pas par ici, et c'est une limite ASSUMÉE : `PresetState`
    // ne transporte que des flottants. Le chemin du profil est réenregistré par
    // la couche d'échange (`SynthPreset::profile`), au même titre que les
    // chemins d'échantillons du sampler, et pour la même raison.
    for (const auto& [id, value] : state.parameterValues) setParameter(id, value);
}

void MultisampleSynth::process(const MidiNoteEvent* events, int numEvents,
                                float* outputL, float* outputR, int numSamples) {
    vsm::audio::dsp::ScopedNoDenormals noDenormals;
    std::fill(outputL, outputL + numSamples, 0.0f);
    std::fill(outputR, outputR + numSamples, 0.0f);

    // UNE seule copie du shared_ptr par bloc : c'est un incrément de compteur
    // atomique, pas une allocation, donc compatible avec le temps réel.
    const ProfilePtr profile = profile_.load(std::memory_order_acquire);

    const float master = param(kOutputLevel);
    const float globalTune = param(kTune);
    const float velocityAmount = param(kVelocityAmount);
    const int program = static_cast<int>(std::lround(param(kProgram)));

    vsm::audio::dsp::AdsrSettings envelope;
    envelope.attackSeconds = param(kAttack);
    // Pas de segment de DÉCROISSANCE : sur un instrument échantillonné, la
    // décroissance est DANS le fichier. En imposer une par-dessus reviendrait à
    // amortir deux fois, et c'est exactement ce qui fait sonner « synthétique »
    // un lecteur d'échantillons mal réglé.
    envelope.decaySeconds = 0.001f;
    envelope.sustainLevel = 1.0f;
    envelope.releaseSeconds = param(kRelease);

    const double cutoff = static_cast<double>(param(kToneCutoff));
    const bool toneIsNeutral = cutoff >= kToneMaximumHz || cutoff >= kToneNeutralFraction * sampleRate_;
    const float toneCoefficient =
        toneIsNeutral ? 0.0f
                      : static_cast<float>(std::exp(-2.0 * std::acos(-1.0) * cutoff / sampleRate_));

    voices_.forEachVoice([&](MultisampleVoice& voice) {
        voice.setEnvelope(envelope);
        voice.setToneCoefficient(toneCoefficient);
    });

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset <= i) {
            const MidiNoteEvent& event = events[eventIndex];
            if (event.kind == MidiNoteEvent::Kind::NoteOn && event.velocity > 0) {
                // La zone se cherche AVANT d'allouer la voix : si le profil ne
                // couvre pas cette note, aucune voix n'est consommée et rien ne
                // sonne. Le trou du profil reste donc audible comme un trou.
                const LoadedZone* zone =
                    profile ? profile->select(program, static_cast<int>(event.note),
                                              static_cast<int>(event.velocity))
                            : nullptr;
                if (zone != nullptr) {
                    auto* voice = voices_.noteOn(event.channel, event.note, event.velocity);
                    if (voice != nullptr) {
                        const float normalized = static_cast<float>(event.velocity) / 127.0f;
                        const float velocityGain = (1.0f - velocityAmount) + velocityAmount * normalized;
                        voice->setEnvelope(envelope);
                        voice->setToneCoefficient(toneCoefficient);
                        voice->attach(profile, *zone, sampleRate_, velocityGain, globalTune);
                    }
                }
            } else if (event.kind == MidiNoteEvent::Kind::NoteOff) {
                voices_.noteOff(event.channel, event.note, event.velocity);
            }
            ++eventIndex;
        }

        float left = 0.0f, right = 0.0f;
        voices_.forEachVoice([&](MultisampleVoice& voice) { voice.render(left, right); });

        outputL[i] = left * master;
        outputR[i] = right * master;
    }
}

// --- IMultisampleBank ------------------------------------------------------

bool MultisampleSynth::loadProfile(const MultisampleProfileSpec& spec, std::string& outError,
                                    vsm::audio::plugin::MultisampleSampleCache* cache) {
    if (spec.zones.empty()) { outError = "profil sans aucune zone"; return false; }
    if (spec.attribution.empty()) {
        // § 28 d'ARCHITECTURE.md : licence inconnue, banque refusée. Charger
        // « en attendant » reviendrait à distribuer un son dont on ne sait pas
        // s'il peut l'être.
        outError = "profil sans attribution : licence inconnue, banque refusée";
        return false;
    }

    // Un même fichier sert souvent plusieurs zones (deux couches de vélocité
    // qui partagent un échantillon, une zone étendue découpée en programmes).
    // Le charger une seule fois n'est pas une optimisation de confort : c'est
    // ce qui fait tenir un profil dans son budget.
    std::map<std::string, SampleBufferPtr> loaded;
    size_t bytes = 0;
    auto profile = std::make_shared<LoadedProfile>();
    profile->name = spec.name;
    profile->attribution = spec.attribution;
    profile->sourcePath = spec.sourcePath;
    profile->programNames = spec.programNames;
    profile->zones.reserve(spec.zones.size());

    for (const auto& zoneSpec : spec.zones) {
        auto found = loaded.find(zoneSpec.samplePath);
        if (found == loaded.end()) {
            SampleBufferPtr buffer;
            std::string error;
            if (cache != nullptr) {
                buffer = cache->get(zoneSpec.samplePath, error);
            } else {
                auto result = WavFileReader::readFile(zoneSpec.samplePath);
                if (result.success && result.buffer.empty()) error = "fichier vide";
                else if (!result.success) error = result.error;
                else buffer = std::make_shared<const SampleBuffer>(std::move(result.buffer));
            }
            if (!buffer) {
                outError = "échantillon « " + zoneSpec.relativePath + " » : " + error;
                return false;
            }
            const size_t frames = buffer->numFrames();
            const size_t channels = buffer->isStereo() ? 2u : 1u;
            bytes += frames * channels * sizeof(float);
            if (bytes > vsm::audio::plugin::kMultisampleMemoryBudgetBytes) {
                outError = "profil au-delà du budget mémoire de "
                         + std::to_string(vsm::audio::plugin::kMultisampleMemoryBudgetBytes / (1024u * 1024u))
                         + " Mo : réduire le nombre de zones ou de couches";
                return false;
            }
            found = loaded.emplace(zoneSpec.samplePath, std::move(buffer)).first;
        }

        LoadedZone zone;
        zone.program = zoneSpec.program;
        zone.lowNote = zoneSpec.lowNote;
        zone.highNote = zoneSpec.highNote;
        zone.lowVelocity = zoneSpec.lowVelocity;
        zone.highVelocity = zoneSpec.highVelocity;
        zone.rootNote = zoneSpec.rootNote;
        zone.tuneCents = zoneSpec.tuneCents;
        zone.level = zoneSpec.level;
        zone.sample = found->second;
        zone.relativePath = zoneSpec.relativePath;

        const uint64_t frames = static_cast<uint64_t>(zone.sample->numFrames());
        if (zoneSpec.loopEnabled) {
            // Des points de boucle incohérents ne se rattrapent pas en douce :
            // une boucle qui déborde du fichier lirait du silence en rond, ce
            // qui s'entend comme une note qui s'éteint sans raison.
            if (zoneSpec.loopEnd > frames || zoneSpec.loopStart >= zoneSpec.loopEnd) {
                outError = "zone « " + zoneSpec.relativePath + " » : points de boucle invalides ("
                         + std::to_string(zoneSpec.loopStart) + ".." + std::to_string(zoneSpec.loopEnd)
                         + " pour " + std::to_string(frames) + " trames)";
                return false;
            }
            zone.loopEnabled = true;
            zone.loopStart = zoneSpec.loopStart;
            zone.loopEnd = zoneSpec.loopEnd;
        }

        profile->zones.push_back(std::move(zone));
    }

    profile->memoryBytes = bytes;
    setProfile(std::move(profile));
    return true;
}

void MultisampleSynth::setProfile(ProfilePtr profile) {
    profile_.store(std::move(profile), std::memory_order_release);
}

void MultisampleSynth::clearProfile() {
    profile_.store(nullptr, std::memory_order_release);
}

std::string MultisampleSynth::profileName() const {
    auto profile = profile_.load(std::memory_order_acquire);
    return profile ? profile->name : std::string{};
}

std::string MultisampleSynth::profileSourcePath() const {
    auto profile = profile_.load(std::memory_order_acquire);
    return profile ? profile->sourcePath : std::string{};
}

std::string MultisampleSynth::profileAttribution() const {
    auto profile = profile_.load(std::memory_order_acquire);
    return profile ? profile->attribution : std::string{};
}

int MultisampleSynth::zoneCount() const {
    auto profile = profile_.load(std::memory_order_acquire);
    return profile ? static_cast<int>(profile->zones.size()) : 0;
}

int MultisampleSynth::programCount() const {
    auto profile = profile_.load(std::memory_order_acquire);
    return profile ? profile->programCount() : 0;
}

size_t MultisampleSynth::profileMemoryBytes() const {
    auto profile = profile_.load(std::memory_order_acquire);
    return profile ? profile->memoryBytes : 0u;
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.multisample", "Multisample (acoustique échantillonné)", MultisampleSynth);

} // namespace vsm::plugins::multisample
