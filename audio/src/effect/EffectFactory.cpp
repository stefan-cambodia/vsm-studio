#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/effect/BitCrusher.h"
#include "vsm/audio/effect/ChorusEffect.h"
#include "vsm/audio/effect/Delay.h"
#include "vsm/audio/effect/Distortion.h"
#include "vsm/audio/effect/FilterEffect.h"
#include "vsm/audio/effect/Flanger.h"
#include "vsm/audio/effect/Phaser.h"
#include "vsm/audio/effect/Reverb.h"
#include "vsm/audio/effect/TapeSaturation.h"

namespace vsm::audio::effect {

const std::vector<EffectInfo>& EffectFactory::available() {
    // Ordre = ordre d'affichage dans le menu "ajouter un effet".
    static const std::vector<EffectInfo> kEffects = {
        {"filter", "Filter"},
        {"distortion", "Distortion"},
        {"bitcrusher", "Bit Crusher"},
        {"chorus", "Chorus"},
        {"flanger", "Flanger"},
        {"phaser", "Phaser"},
        {"delay", "Delay"},
        {"reverb", "Reverb"},
        {"tape", "Tape Saturation"},
    };
    return kEffects;
}

std::unique_ptr<IAudioEffect> EffectFactory::create(const std::string& id) {
    if (id == "filter") return std::make_unique<FilterEffect>();
    if (id == "distortion") return std::make_unique<Distortion>();
    if (id == "bitcrusher") return std::make_unique<BitCrusher>();
    if (id == "chorus") return std::make_unique<ChorusEffect>();
    if (id == "flanger") return std::make_unique<Flanger>();
    if (id == "phaser") return std::make_unique<Phaser>();
    if (id == "delay") return std::make_unique<Delay>();
    if (id == "reverb") return std::make_unique<Reverb>();
    if (id == "tape") return std::make_unique<TapeSaturation>();
    return nullptr;
}

} // namespace vsm::audio::effect
