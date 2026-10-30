#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/effect/BitCrusher.h"
#include "vsm/audio/effect/ChannelStrip.h"
#include "vsm/audio/effect/ChorusEffect.h"
#include "vsm/audio/effect/Delay.h"
#include "vsm/audio/effect/Distortion.h"
#include "vsm/audio/effect/FilterEffect.h"
#include "vsm/audio/effect/Flanger.h"
#include "vsm/audio/effect/Phaser.h"
#include "vsm/audio/effect/Reverb.h"
#include "vsm/audio/effect/TapeSaturation.h"
#include "vsm/audio/effect/TransientShaperEffect.h"
#include "vsm/audio/effect/TremoloEffect.h"
#include "vsm/audio/effect/TunerEffect.h"
#include "vsm/audio/effect/PitchShiftEffect.h"
#include "vsm/audio/effect/ExpanderDeEsser.h"

namespace vsm::audio::effect {

const std::vector<EffectInfo>& EffectFactory::available() {
    // Ordre = ordre d'affichage dans le menu "ajouter un effet".
    static const std::vector<EffectInfo> kEffects = {
        // LES QUATRE DE LA TRANCHE D'ABORD (D4.1) : ce sont ceux qu'on met sur
        // une piste avant d'avoir envie d'un chorus, et un menu se lit du haut.
        {"eq", "Equaliser"},
        {"compressor", "Compressor"},
        {"gate", "Gate"},
        {"limiter", "Limiter"},
        {"filter", "Filter"},
        {"distortion", "Distortion"},
        {"bitcrusher", "Bit Crusher"},
        {"chorus", "Chorus"},
        {"flanger", "Flanger"},
        {"phaser", "Phaser"},
        {"delay", "Delay"},
        {"reverb", "Reverb"},
        {"tape", "Tape Saturation"},
        // TROIS DE PLUS (D13.8) : ce qu'une tranche de Cubase ou de Live a et
        // que le parc n'avait pas -- la forme (transient shaper), le mouvement
        // (trémolo / auto-pan) et la hauteur en temps réel (pitch shift).
        {"transientshaper", "Transient Shaper"},
        {"tremolo", "Tremolo / Auto-pan"},
        {"pitchshift", "Pitch Shift"},
        // DEUX DE PLUS (D596) : les dynamiques de Cubase que le parc n'avait pas — à la FIN, pour que les seize d'avant
        // gardent leur rang dans le menu.
        {"expander", "Expander"},
        {"deesser", "De-esser"},
        // UN DE PLUS (D605) : l'accordeur — il mesure et ne touche pas au son. À la fin, pour que les dix-huit d'avant
        // gardent leur rang.
        {"tuner", "Tuner"},
    };
    return kEffects;
}

std::unique_ptr<IAudioEffect> EffectFactory::create(const std::string& id) {
    if (id == "eq") return std::make_unique<EqualiserEffect>();
    if (id == "compressor") return std::make_unique<CompressorEffect>();
    if (id == "gate") return std::make_unique<GateEffect>();
    if (id == "limiter") return std::make_unique<LimiterEffect>();
    if (id == "filter") return std::make_unique<FilterEffect>();
    if (id == "distortion") return std::make_unique<Distortion>();
    if (id == "bitcrusher") return std::make_unique<BitCrusher>();
    if (id == "chorus") return std::make_unique<ChorusEffect>();
    if (id == "flanger") return std::make_unique<Flanger>();
    if (id == "phaser") return std::make_unique<Phaser>();
    if (id == "delay") return std::make_unique<Delay>();
    if (id == "reverb") return std::make_unique<Reverb>();
    if (id == "tape") return std::make_unique<TapeSaturation>();
    if (id == "transientshaper") return std::make_unique<TransientShaperEffect>();
    if (id == "tremolo") return std::make_unique<TremoloEffect>();
    if (id == "pitchshift") return std::make_unique<PitchShiftEffect>();
    if (id == "expander") return std::make_unique<ExpanderEffect>();   // D596
    if (id == "deesser") return std::make_unique<DeEsserEffect>();
    if (id == "tuner") return std::make_unique<TunerEffect>();   // D605
    // PAS UN EFFET INTERNE : on demande aux couches d'hébergement, s'il y en a
    // une de posée. Sans elles, on rend nullptr comme avant, et l'appelant
    // signale un effet inconnu -- jamais ne le remplace par un autre.
    if (const auto& externe = externalResolver()) return externe(id);
    return nullptr;
}

namespace {
/// LA VARIABLE EST LOCALE À UNE FONCTION, pas statique de fichier : l'ordre
/// d'initialisation des statiques entre unités de traduction n'est pas défini,
/// et une couche d'hébergement qui se poserait avant `main` écrirait dans un
/// objet pas encore construit.
AudioEffectFactoryById& resolverSlot() {
    static AudioEffectFactoryById resolver;
    return resolver;
}
} // namespace

void EffectFactory::setExternalResolver(AudioEffectFactoryById resolver) {
    resolverSlot() = std::move(resolver);
}

const AudioEffectFactoryById& EffectFactory::externalResolver() { return resolverSlot(); }

} // namespace vsm::audio::effect
