#include "PercSynth.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>

namespace vsm::plugins::perc {

using namespace vsm::audio::plugin;

PercSynth::PercSynth() {
    // TOUT EN UNITÉS PHYSIQUES (§ 1 du CDC) : des hertz et des secondes, jamais
    // du 0..1 normalisé. C'est ce qui permet au projet d'analyse d'écrire
    // « 210 Hz » sans rien connaître de cette machine.
    //
    // Les plages viennent des instruments : un conga sonne entre 130 et 260 Hz,
    // un bongo une octave plus haut, une timbale au-dessus encore. Les défauts
    // sont au milieu de la plage utile, pas au minimum.
    parameterList_ = {
        {kCongaLevel, "Conga Level", 0.0f, 1.0f, 0.85f, ""},
        {kCongaTune, "Conga Tune", 90.0f, 260.0f, 150.0f, "Hz"},
        {kCongaDecay, "Conga Decay", 0.10f, 1.20f, 0.45f, "s"},
        {kBongoLevel, "Bongo Level", 0.0f, 1.0f, 0.8f, ""},
        {kBongoTune, "Bongo Tune", 180.0f, 520.0f, 300.0f, "Hz"},
        {kBongoDecay, "Bongo Decay", 0.05f, 0.60f, 0.22f, "s"},
        {kTimbaleLevel, "Timbale Level", 0.0f, 1.0f, 0.8f, ""},
        {kTimbaleTune, "Timbale Tune", 200.0f, 620.0f, 330.0f, "Hz"},
        {kTimbaleDecay, "Timbale Decay", 0.08f, 0.80f, 0.30f, "s"},
        {kCowbellLevel, "Cowbell Level", 0.0f, 1.0f, 0.7f, ""},
        {kCowbellTune, "Cowbell Tune", 400.0f, 1200.0f, 587.0f, "Hz"},
        {kCowbellDecay, "Cowbell Decay", 0.10f, 1.00f, 0.35f, "s"},
        {kWoodLevel, "Wood Level", 0.0f, 1.0f, 0.75f, ""},
        {kWoodTune, "Wood Tune", 500.0f, 2500.0f, 1100.0f, "Hz"},
        {kWoodDecay, "Wood Decay", 0.02f, 0.30f, 0.08f, "s"},
        {kShakerLevel, "Shaker Level", 0.0f, 1.0f, 0.65f, ""},
        {kShakerTone, "Shaker Tone", 2000.0f, 12000.0f, 6000.0f, "Hz"},
        {kShakerDecay, "Shaker Decay", 0.02f, 0.30f, 0.07f, "s"},
        {kTambourineLevel, "Tambourine Level", 0.0f, 1.0f, 0.7f, ""},
        {kTambourineDecay, "Tambourine Decay", 0.05f, 0.60f, 0.20f, "s"},
        {kAccent, "Accent", 0.0f, 1.0f, 0.5f, ""},
    };
    for (const auto& info : parameterList_)
        params_[info.id].store(info.defaultValue, std::memory_order_relaxed);
}

void PercSynth::initialize(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    lowConga_.setSampleRate(sampleRate);
    hiConga_.setSampleRate(sampleRate);
    muteConga_.setSampleRate(sampleRate);
    lowBongo_.setSampleRate(sampleRate);
    hiBongo_.setSampleRate(sampleRate);
    lowTimbale_.setSampleRate(sampleRate);
    hiTimbale_.setSampleRate(sampleRate);
    cowbell_.setSampleRate(sampleRate);
    hiWood_.setSampleRate(sampleRate);
    lowWood_.setSampleRate(sampleRate);
    claves_.setSampleRate(sampleRate);
    maracas_.setSampleRate(sampleRate);
    tambourine_.setSampleRate(sampleRate);
    applyConfig();
}

void PercSynth::applyConfig() {
    const float congaTune = params_[kCongaTune].load(std::memory_order_relaxed);
    const float congaDecay = params_[kCongaDecay].load(std::memory_order_relaxed);
    const float congaLevel = params_[kCongaLevel].load(std::memory_order_relaxed);
    // LES ÉCARTS DE LA PAIRE SONT FIXES, et c'est voulu : un conguero accorde
    // ses fûts à une quarte environ, et c'est cet intervalle qu'on reconnaît.
    // Un réglage de plus ne dirait rien de neuf ; le réglage `Tune` déplace la
    // paire entière.
    lowConga_.configure(congaTune * 0.75f, congaDecay, congaLevel);
    hiConga_.configure(congaTune, congaDecay * 0.85f, congaLevel);
    // La conga ÉTOUFFÉE est la même peau, la main posée dessus : mêmes modes,
    // durée divisée par cinq. C'est une nuance de jeu, pas un autre fût.
    muteConga_.configure(congaTune, congaDecay * 0.20f, congaLevel);

    const float bongoTune = params_[kBongoTune].load(std::memory_order_relaxed);
    const float bongoDecay = params_[kBongoDecay].load(std::memory_order_relaxed);
    const float bongoLevel = params_[kBongoLevel].load(std::memory_order_relaxed);
    lowBongo_.configure(bongoTune * 0.75f, bongoDecay, bongoLevel);
    hiBongo_.configure(bongoTune, bongoDecay * 0.85f, bongoLevel);

    const float timbaleTune = params_[kTimbaleTune].load(std::memory_order_relaxed);
    const float timbaleDecay = params_[kTimbaleDecay].load(std::memory_order_relaxed);
    const float timbaleLevel = params_[kTimbaleLevel].load(std::memory_order_relaxed);
    lowTimbale_.configure(timbaleTune * 0.75f, timbaleDecay, timbaleLevel);
    hiTimbale_.configure(timbaleTune, timbaleDecay * 0.85f, timbaleLevel);

    cowbell_.configure(params_[kCowbellTune].load(std::memory_order_relaxed),
                       params_[kCowbellDecay].load(std::memory_order_relaxed),
                       params_[kCowbellLevel].load(std::memory_order_relaxed));

    const float woodTune = params_[kWoodTune].load(std::memory_order_relaxed);
    const float woodDecay = params_[kWoodDecay].load(std::memory_order_relaxed);
    const float woodLevel = params_[kWoodLevel].load(std::memory_order_relaxed);
    hiWood_.configure(woodTune, woodDecay, woodLevel);
    lowWood_.configure(woodTune * 0.7f, woodDecay * 1.15f, woodLevel);
    // Les claves sont plus aiguës et plus sèches qu'un bloc de bois : même
    // barre, deux fois plus courte et deux fois moins de queue.
    claves_.configure(woodTune * 1.6f, woodDecay * 0.6f, woodLevel);

    maracas_.configure(params_[kShakerTone].load(std::memory_order_relaxed),
                       params_[kShakerDecay].load(std::memory_order_relaxed),
                       params_[kShakerLevel].load(std::memory_order_relaxed));

    tambourine_.configure(params_[kTambourineDecay].load(std::memory_order_relaxed),
                          params_[kTambourineLevel].load(std::memory_order_relaxed));
}

void PercSynth::triggerNote(uint8_t note, uint8_t velocity) {
    const float accent = params_[kAccent].load(std::memory_order_relaxed);
    const float velNorm = static_cast<float>(velocity) / 127.0f;
    const float velGain = (0.35f + 0.65f * velNorm) * (1.0f + accent * 0.5f * velNorm);

    switch (note) {
        case kNoteTambourine: tambourine_.trigger(velGain); break;
        case kNoteCowbell: cowbell_.trigger(velGain); break;
        case kNoteHiBongo: hiBongo_.trigger(velGain); break;
        case kNoteLowBongo: lowBongo_.trigger(velGain); break;
        case kNoteMuteHiConga: muteConga_.trigger(velGain); break;
        case kNoteOpenHiConga: hiConga_.trigger(velGain); break;
        case kNoteLowConga: lowConga_.trigger(velGain); break;
        case kNoteHiTimbale: hiTimbale_.trigger(velGain); break;
        case kNoteLowTimbale: lowTimbale_.trigger(velGain); break;
        case kNoteMaracas: maracas_.trigger(velGain); break;
        case kNoteClaves: claves_.trigger(velGain); break;
        case kNoteHiWoodBlock: hiWood_.trigger(velGain); break;
        case kNoteLowWoodBlock: lowWood_.trigger(velGain); break;
        default: break;
    }
}

void PercSynth::process(const MidiNoteEvent* events, int numEvents,
                        float* outputL, float* outputR, int numSamples) {
    applyConfig();

    int eventIndex = 0;
    for (int i = 0; i < numSamples; ++i) {
        while (eventIndex < numEvents && events[eventIndex].sampleOffset == i) {
            const auto& ev = events[eventIndex];
            if (ev.kind == MidiNoteEvent::Kind::NoteOn && ev.velocity > 0)
                triggerNote(ev.note, ev.velocity);
            ++eventIndex;
        }

        float sum = lowConga_.render() + hiConga_.render() + muteConga_.render()
                  + lowBongo_.render() + hiBongo_.render()
                  + lowTimbale_.render() + hiTimbale_.render()
                  + cowbell_.render()
                  + hiWood_.render() + lowWood_.render() + claves_.render()
                  + maracas_.render() + tambourine_.render();
        // NIVEAU, PUIS SATURATION DOUCE — et le second n'est pas un pansement.
        //
        // Le facteur 0,45 est celui de la TR-909, pour que deux boîtes mises en
        // concurrence sur un même stem ne se départagent pas au VOLUME : une
        // frappe seule sort ici à 0,44 de crête, contre 0,47 à la 808 et 0,69 à
        // la 909. C'est la bonne échelle.
        //
        // Mais cette machine a TREIZE pièces, contre neuf à la 909, et treize
        // frappes simultanées s'additionnent en phase : mesuré, la crête monte
        // à 3,79. Baisser le facteur d'autant rendrait la machine quatre fois
        // plus faible que les autres sur une frappe isolée, c'est-à-dire sur le
        // cas normal, pour protéger un cas qui ne se produit pas en musique.
        //
        // La sortie passe donc par une TANGENTE HYPERBOLIQUE, qui est ce que
        // fait le bus d'une boîte analogique : linéaire à 1 % près en dessous de
        // 0,2, elle ne peut mathématiquement pas dépasser 1. Une frappe seule y
        // perd 6 % (0,439 -> 0,413) ; le pupitre entier y est comprimé au lieu
        // d'écrêter. C'est une approximation ASSUMÉE (§ 8 du CDC) : aucune
        // mesure sur un bus réel ne l'a réglée, c'est la forme la moins
        // arbitraire qui borne sans casser la dynamique utile.
        outputL[i] = std::tanh(sum * 0.45f);
        outputR[i] = outputL[i];
    }
}

int PercSynth::activeVoiceCount() const {
    return (lowConga_.isActive() ? 1 : 0) + (hiConga_.isActive() ? 1 : 0)
         + (muteConga_.isActive() ? 1 : 0)
         + (lowBongo_.isActive() ? 1 : 0) + (hiBongo_.isActive() ? 1 : 0)
         + (lowTimbale_.isActive() ? 1 : 0) + (hiTimbale_.isActive() ? 1 : 0)
         + (cowbell_.isActive() ? 1 : 0)
         + (hiWood_.isActive() ? 1 : 0) + (lowWood_.isActive() ? 1 : 0)
         + (claves_.isActive() ? 1 : 0)
         + (maracas_.isActive() ? 1 : 0) + (tambourine_.isActive() ? 1 : 0);
}

void PercSynth::setParameter(ParamId id, float value) {
    if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

float PercSynth::getParameter(ParamId id) const {
    return id < kNumParams ? params_[id].load(std::memory_order_relaxed) : 0.0f;
}

const ParameterList& PercSynth::parameterList() const { return parameterList_; }

PresetState PercSynth::saveState() const {
    PresetState state;
    state.pluginTypeId = "vsm.perc";
    for (const auto& info : parameterList_)
        state.parameterValues[info.id] = params_[info.id].load(std::memory_order_relaxed);
    return state;
}

void PercSynth::loadState(const PresetState& state) {
    for (const auto& [id, value] : state.parameterValues)
        if (id < kNumParams) params_[id].store(value, std::memory_order_relaxed);
}

VSM_REGISTER_SYNTH_PLUGIN("vsm.perc", "Percussion (peaux et barres, modal)", PercSynth);

} // namespace vsm::plugins::perc
