#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "testtone/TestToneSynth.h"
#include "minimoog/MinimoogSynth.h"
#include "tb303/TB303Synth.h"
#include "juno106/Juno106Synth.h"
#include "tr808/TR808Synth.h"
#include "tr909/TR909Synth.h"
#include "sh101/SH101Synth.h"
#include "prophet/ProphetSynth.h"
#include "jupiter8/Jupiter8Synth.h"
#include "arpodyssey/ArpOdysseySynth.h"
#include "ms20/MS20Synth.h"
#include "dx7/DX7Synth.h"
#include "sampler/SamplerSynth.h"
#include "epiano/EPianoSynth.h"
#include "obx/ObxSynth.h"
#include "supersaw/SupersawSynth.h"
#include "wavetable/WavetableSynth.h"
#include "pcmhybrid/PcmHybridSynth.h"
#include "tonewheel/TonewheelOrgan.h"
#include "generic/GenericSynth.h"
#include "string/StringSynth.h"
#include "piano/PianoSynth.h"
#include "drums/DrumsSynth.h"
#include "wind/WindSynth.h"
#include "multisample/MultisampleSynth.h"

namespace vsm::audio::plugin {

void registerBuiltInPlugins() {
    // Construit puis détruit immédiatement une instance de chaque plugin
    // intégré. Chaque constructeur fait un travail réel (remplissage de
    // parameterList_, initialisation des std::atomic) : le compilateur ne
    // peut pas prouver qu'il est sans effet de bord et donc l'éliminer, ce
    // qui force l'éditeur de liens à inclure sa traduction unit -- et donc
    // son registrar VSM_REGISTER_SYNTH_PLUGIN -- depuis la bibliothèque
    // statique vsm_audio. Les instances elles-mêmes ne sont jamais
    // utilisées : les instances réelles sont créées via PluginRegistry.
    {
        vsm::plugins::testtone::TestToneSynth forceLinkTestTone;
        (void)forceLinkTestTone;
    }
    {
        vsm::plugins::minimoog::MinimoogSynth forceLinkMinimoog;
        (void)forceLinkMinimoog;
    }
    {
        vsm::plugins::tb303::TB303Synth forceLinkTb303;
        (void)forceLinkTb303;
    }
    {
        vsm::plugins::juno106::Juno106Synth forceLinkJuno106;
        (void)forceLinkJuno106;
    }
    {
        vsm::plugins::tr808::TR808Synth forceLinkTr808;
        (void)forceLinkTr808;
    }
    {
        vsm::plugins::tr909::TR909Synth forceLinkTr909;
        (void)forceLinkTr909;
    }
    {
        vsm::plugins::sh101::SH101Synth forceLinkSh101;
        (void)forceLinkSh101;
    }
    {
        vsm::plugins::prophet::ProphetSynth forceLinkProphet;
        (void)forceLinkProphet;
    }
    {
        vsm::plugins::jupiter8::Jupiter8Synth forceLinkJupiter8;
        (void)forceLinkJupiter8;
    }
    {
        vsm::plugins::arpodyssey::ArpOdysseySynth forceLinkArpOdyssey;
        (void)forceLinkArpOdyssey;
    }
    {
        vsm::plugins::ms20::MS20Synth forceLinkMs20;
        (void)forceLinkMs20;
    }
    {
        vsm::plugins::dx7::DX7Synth forceLinkDx7;
        (void)forceLinkDx7;
    }
    {
        vsm::plugins::sampler::SamplerSynth forceLinkSampler;
        (void)forceLinkSampler;
    }
    {
        vsm::plugins::epiano::EPianoSynth forceLinkEPiano;
        (void)forceLinkEPiano;
    }
    {
        vsm::plugins::obx::ObxSynth forceLinkObx;
        (void)forceLinkObx;
    }
    {
        vsm::plugins::supersaw::SupersawSynth forceLinkSupersaw;
        (void)forceLinkSupersaw;
    }
    {
        vsm::plugins::wavetable::WavetableSynth forceLinkWavetable;
        (void)forceLinkWavetable;
    }
    {
        vsm::plugins::pcmhybrid::PcmHybridSynth forceLinkPcmHybrid;
        (void)forceLinkPcmHybrid;
    }
    {
        vsm::plugins::tonewheel::TonewheelOrgan forceLinkTonewheel;
        (void)forceLinkTonewheel;
    }
    {
        vsm::plugins::generic::GenericSynth forceLinkGeneric;
        (void)forceLinkGeneric;
    }
    {
        vsm::plugins::string_machine::StringSynth forceLinkString;
        (void)forceLinkString;
    }
    {
        vsm::plugins::piano::PianoSynth forceLinkPiano;
        (void)forceLinkPiano;
    }
    {
        vsm::plugins::drums::DrumsSynth forceLinkDrums;
        (void)forceLinkDrums;
    }
    {
        vsm::plugins::wind::WindSynth forceLinkWind;
        (void)forceLinkWind;
    }
    {
        vsm::plugins::multisample::MultisampleSynth forceLinkMultisample;
        (void)forceLinkMultisample;
    }
}

} // namespace vsm::audio::plugin
