#include "vsm/interchange/ParameterDescriptor.h"
#include "vsm/audio/effect/EffectFactory.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <map>
#include <mutex>

namespace vsm::interchange {

// ---------------------------------------------------------------------------
// Table de correspondance « nom de paramètre d'une machine -> semanticId ».
//
// POURQUOI ICI ET PAS DANS CHAQUE PLUGIN : le vocabulaire sémantique est une
// affaire d'INTEROPÉRABILITÉ, pas de synthèse. L'inscrire dans les plugins
// obligerait à toucher (et donc à re-tester) douze machines à chaque
// évolution du vocabulaire, et ferait entrer une préoccupation d'échange de
// données dans du code qui n'a qu'un travail : produire du son. La table est
// donc externe -- au prix d'un couplage par le NOM du paramètre, qui est
// justement ce que le projet garde stable (les noms sont déjà verrouillés par
// les tests `..._parameter_list_size` de chaque machine).
//
// Un test de complétude vérifie qu'AUCUN paramètre d'AUCUNE machine
// enregistrée ne manque à cette table, et qu'aucun semanticId n'y est employé
// deux fois pour la même machine (deux paramètres partageant une identité
// s'écraseraient mutuellement à l'import d'un preset).
// ---------------------------------------------------------------------------

namespace {

using NameToSemantic = std::vector<std::pair<std::string, std::string>>;

/// Les paramètres du sampler suivent un schéma régulier (« Slot N Level »...).
/// Les générer garantit qu'aucun emplacement n'est oublié -- écrire 57 entrées
/// à la main invite l'erreur de copie, et le test de complétude ne dirait que
/// « il en manque une », pas laquelle.
NameToSemantic samplerEntries() {
    NameToSemantic entries = {{"Master Level", "output.level"}};
    static const std::pair<const char*, const char*> perSlot[] = {
        {"Note", "note"}, {"Tune", "tune"}, {"Level", "level"},
        {"Pan", "pan"}, {"Decay", "decay"}, {"Start", "start"}, {"Choke", "chokeGroup"},
    };
    for (int slot = 1; slot <= 16; ++slot) {
        for (const auto& [suffix, semantic] : perSlot) {
            entries.emplace_back("Slot " + std::to_string(slot) + " " + suffix,
                                  "sampler.slot." + std::to_string(slot) + "." + semantic);
        }
    }
    return entries;
}

const std::vector<std::pair<std::string, NameToSemantic>>& semanticTable() {
    static const std::vector<std::pair<std::string, NameToSemantic>> table = {
    {"vsm.sampler", samplerEntries()},
    // --- vsm.epiano ---
    {"vsm.epiano", {
        {"Bell Level", "epiano.bellLevel"},
        {"Tine Decay", "epiano.tineDecay"},
        {"Release", "envelope.1.release"},
        {"Hammer Hardness", "epiano.hammerHardness"},
        {"Hammer Noise", "epiano.hammerNoise"},
        {"Pickup Drive", "epiano.pickupDrive"},
        {"Character", "epiano.character"},
        {"Velocity Sensitivity", "voice.velocitySensitivity"},
        {"Tone Bass", "tone.bass"},
        {"Tone Treble", "tone.treble"},
        {"Tremolo Rate", "effect.tremolo.rate"},
        {"Tremolo Depth", "effect.tremolo.depth"},
        {"Tremolo Stereo", "effect.tremolo.stereo"},
        {"Analog Character", "voice.analogCharacter"},
        {"Output Level", "output.level"},
    }},
    // --- vsm.string ---
    // Machine de MODÉLISATION PHYSIQUE : ses identités décrivent une corde,
    // pas une chaîne soustractive. Rien ici ne se traduit en `oscillator.*`
    // ou `filter.*` sans mentir -- une corde n'a ni oscillateur ni filtre,
    // elle a une longueur, une raideur et un point de contact. Les seules
    // identités réutilisées sont celles qui gardent leur sens partout :
    // le relâchement, la sensibilité à la vélocité, la sortie.
    {"vsm.string", {
        {"Pick Position", "string.pickPosition"},
        {"Pick Hardness", "string.pickHardness"},
        {"Excitation", "string.excitation"},
        {"Bow Pressure", "string.bowPressure"},
        {"Bow Speed", "string.bowSpeed"},
        {"String Decay", "string.decay"},
        {"String Damping", "string.damping"},
        {"Stiffness", "string.stiffness"},
        {"Release", "envelope.1.release"},
        {"Body Level", "string.bodyLevel"},
        {"Body Size", "string.bodySize"},
        {"Velocity Sensitivity", "voice.velocitySensitivity"},
        {"Drive", "output.drive"},
        {"Analog Character", "voice.analogCharacter"},
        {"Output Level", "output.level"},
    }},
    // --- vsm.piano ---
    // Ses identités sont presque toutes CELLES DE `vsm.string`, et c'est
    // voulu : les deux machines partagent la même boucle physique
    // (`dsp/StringWaveguide.h`), et un patch de corde doit pouvoir voyager
    // vers le piano. Le point de contact et la dureté de l'excitation sont la
    // même notion, qu'on frappe ou qu'on pince ; la raideur, la décroissance
    // et l'amortissement sont ceux de la corde ; la table d'harmonie est le
    // corps. Un seul identifiant lui est propre, parce que rien d'autre au
    // parc n'a d'étouffoir.
    {"vsm.piano", {
        {"Hammer Hardness", "string.pickHardness"},
        {"Hammer Position", "string.pickPosition"},
        {"Velocity Sensitivity", "voice.velocitySensitivity"},
        {"Unison Detune", "voice.unisonDetune"},
        {"Inharmonicity", "string.stiffness"},
        {"String Decay", "string.decay"},
        {"String Damping", "string.damping"},
        {"Release", "envelope.1.release"},
        {"Sustain Pedal", "piano.sustainPedal"},
        {"Soundboard Level", "string.bodyLevel"},
        {"Soundboard Size", "string.bodySize"},
        {"Tone Bass", "tone.bass"},
        {"Tone Treble", "tone.treble"},
        {"Stereo Spread", "output.stereoWidth"},
        {"Analog Character", "voice.analogCharacter"},
        {"Output Level", "output.level"},
    }},
    // --- vsm.drums ---
    // Le vocabulaire de percussion du parc (`drum.kick.*`, `drum.snare.*`...)
    // est repris tel quel : une grosse caisse est une grosse caisse, qu'elle
    // soit synthétisée par une 808 ou modélisée par une peau. C'est ce qui
    // permet à un motif écrit pour une TR de s'appliquer ici sans traduction.
    {"vsm.drums", {
        {"Kick Level", "drum.kick.level"},
        {"Kick Tune", "drum.kick.tune"},
        {"Kick Decay", "drum.kick.decay"},
        {"Kick Beater", "drum.kick.attack"},
        {"Snare Level", "drum.snare.level"},
        {"Snare Tune", "drum.snare.tune"},
        {"Snare Decay", "drum.snare.decay"},
        {"Snare Wires", "drum.snare.snappy"},
        {"Tom Level", "drum.tom.level"},
        {"Tom Tune", "drum.tom.tune"},
        {"Tom Decay", "drum.tom.decay"},
        {"Closed Hat Level", "drum.closedHat.level"},
        {"Closed Hat Decay", "drum.closedHat.decay"},
        {"Open Hat Level", "drum.openHat.level"},
        {"Open Hat Decay", "drum.openHat.decay"},
        {"Ride Level", "drum.ride.level"},
        {"Ride Decay", "drum.ride.decay"},
        {"Crash Level", "drum.crash.level"},
        {"Crash Decay", "drum.crash.decay"},
        {"Room Level", "drum.room.level"},
        {"Room Size", "drum.room.size"},
        {"Output Level", "output.level"},
    }},
    // --- vsm.wind ---
    {"vsm.wind", {
        {"Breath Pressure", "wind.breathPressure"},
        {"Reed Stiffness", "wind.reedStiffness"},
        {"Brassiness", "wind.brassiness"},
        {"Breath Noise", "wind.breathNoise"},
        {"Bell Damping", "wind.bellDamping"},
        {"Attack", "envelope.1.attack"},
        {"Release", "envelope.1.release"},
        {"Vibrato Rate", "effect.vibrato.rate"},
        {"Vibrato Depth", "effect.vibrato.depth"},
        {"Vibrato Delay", "lfo.1.delay"},
        {"Tone Bass", "tone.bass"},
        {"Tone Treble", "tone.treble"},
        {"Velocity Sensitivity", "voice.velocitySensitivity"},
        {"Analog Character", "voice.analogCharacter"},
        {"Output Level", "output.level"},
    }},
    // --- vsm.multisample ---
    // Peu de paramètres, et TOUS canoniques : le timbre de cette machine vient
    // des échantillons, pas des réglages. `voice.velocitySensitivity` est
    // réemployée telle quelle plutôt qu'une identité neuve : elle dit déjà
    // « la vélocité agit-elle sur la dynamique », et en inventer une seconde
    // pour la même chose est exactement la faute que le § 8.4 de la feuille de
    // route reproche au reste du projet.
    {"vsm.multisample", {
        {"Program", "sample.program"},
        {"Tune", "output.tune"},
        {"Attack", "envelope.1.attack"},
        {"Release", "envelope.1.release"},
        {"Tone Cutoff", "filter.1.cutoff"},
        {"Velocity Amount", "voice.velocitySensitivity"},
        {"Output Level", "output.level"},
    }},
    // --- vsm.generic ---
    // Machine NEUTRE : ses identités sont volontairement les plus canoniques
    // du vocabulaire, sans aucun identifiant qui lui soit propre. C'est ce qui
    // permet à un patch trouvé sur elle d'être transposé vers n'importe quelle
    // machine de caractère, et réciproquement.
    {"vsm.generic", {
        {"Osc1 Shape", "oscillator.1.shape"},
        {"Osc1 Level", "oscillator.1.level"},
        {"Osc1 Pulse Width", "oscillator.1.pulseWidth"},
        {"Osc2 Shape", "oscillator.2.shape"},
        {"Osc2 Level", "oscillator.2.level"},
        {"Osc2 Pulse Width", "oscillator.2.pulseWidth"},
        {"Osc2 Detune", "oscillator.2.detune"},
        {"Osc2 Octave", "oscillator.2.octave"},
        {"Sub Level", "oscillator.sub.level"},
        {"Sub Shape", "oscillator.sub.shape"},
        {"Noise Level", "oscillator.noise.level"},
        {"Noise Colour", "oscillator.noise.colour"},
        {"Filter Type", "filter.1.type"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Filter Resonance", "filter.1.resonance"},
        {"Filter Slope", "filter.1.slope"},
        {"Filter Env Amount", "filter.1.envAmount"},
        {"Filter Key Track", "filter.1.keyTrack"},
        {"Amp Attack", "envelope.1.attack"},
        {"Amp Decay", "envelope.1.decay"},
        {"Amp Sustain", "envelope.1.sustain"},
        {"Amp Release", "envelope.1.release"},
        {"Filter Attack", "envelope.2.attack"},
        {"Filter Decay", "envelope.2.decay"},
        {"Filter Sustain", "envelope.2.sustain"},
        {"Filter Release", "envelope.2.release"},
        {"LFO1 Rate", "lfo.1.rate"},
        {"LFO1 Shape", "lfo.1.waveform"},
        {"LFO1 to Pitch", "lfo.1.toPitch"},
        {"LFO1 to Filter", "lfo.1.toFilter"},
        {"LFO1 to Amp", "lfo.1.toAmp"},
        {"LFO1 to PWM", "lfo.1.toPulseWidth"},
        {"LFO2 Rate", "lfo.2.rate"},
        {"LFO2 Shape", "lfo.2.waveform"},
        {"LFO2 to Pitch", "lfo.2.toPitch"},
        {"LFO2 to Filter", "lfo.2.toFilter"},
        {"Velocity to Filter", "filter.1.velocityAmount"},
        {"Velocity to Amp", "voice.velocitySensitivity"},
        {"Drive", "output.drive"},
        {"Output Level", "output.level"},
    }},
    // --- vsm.tonewheel ---
    // Les tirettes sont numérotées par RANG HARMONIQUE et non par leur
    // longueur de tuyau : « drawbar.16 » se lirait comme un numéro d'ordre,
    // alors que le rang dit ce que la tirette prélève réellement.
    {"vsm.tonewheel", {
        {"Drawbar 16", "organ.drawbar.1"},
        {"Drawbar 5 1/3", "organ.drawbar.2"},
        {"Drawbar 8", "organ.drawbar.3"},
        {"Drawbar 4", "organ.drawbar.4"},
        {"Drawbar 2 2/3", "organ.drawbar.5"},
        {"Drawbar 2", "organ.drawbar.6"},
        {"Drawbar 1 3/5", "organ.drawbar.7"},
        {"Drawbar 1 1/3", "organ.drawbar.8"},
        {"Drawbar 1", "organ.drawbar.9"},
        {"Percussion Level", "organ.percussion.level"},
        {"Percussion Decay", "organ.percussion.decay"},
        {"Percussion Harmonic", "organ.percussion.harmonic"},
        {"Key Click", "organ.keyClick"},
        {"Vibrato Depth", "effect.vibrato.depth"},
        {"Vibrato Rate", "effect.vibrato.rate"},
        {"Rotary Fast", "effect.rotary.fast"},
        {"Rotary Depth", "effect.rotary.depth"},
        {"Rotary Balance", "effect.rotary.balance"},
        {"Overdrive", "effect.drive.amount"},
        {"Output Level", "output.level"},
    }},
    // --- vsm.pcmhybrid ---
    {"vsm.pcmhybrid", {
        {"Attack Sample", "sample.1.select"},
        {"Attack Level", "sample.1.level"},
        {"Attack Decay", "sample.1.decay"},
        {"Attack Tune", "sample.1.tune"},
        {"Attack Tone", "sample.1.tone"},
        {"Velocity to Attack", "sample.1.velocityAmount"},
        {"Tone Shape", "oscillator.1.waveform"},
        {"Tone Level", "oscillator.1.level"},
        {"Tone Detune", "oscillator.1.detune"},
        {"Structure", "voice.structure"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Filter Resonance", "filter.1.resonance"},
        {"Filter Env Amount", "filter.1.envAmount"},
        {"Filter Key Track", "filter.1.keyTrack"},
        {"Filter Attack", "envelope.2.attack"},
        {"Filter Decay", "envelope.2.decay"},
        {"Filter Sustain", "envelope.2.sustain"},
        {"Filter Release", "envelope.2.release"},
        {"Amp Attack", "envelope.1.attack"},
        {"Amp Decay", "envelope.1.decay"},
        {"Amp Sustain", "envelope.1.sustain"},
        {"Amp Release", "envelope.1.release"},
        {"LFO Rate", "lfo.1.rate"},
        {"LFO to Pitch", "lfo.1.toPitch"},
        {"LFO to Filter", "lfo.1.toFilter"},
        {"Velocity to Filter", "filter.1.velocityAmount"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.wavetable ---
    {"vsm.wavetable", {
        {"Wavetable", "oscillator.1.wavetable"},
        {"Position", "oscillator.1.wavePosition"},
        {"Wave Env Amount", "oscillator.1.wavePositionEnvAmount"},
        {"LFO to Position", "lfo.1.toWavePosition"},
        {"Osc B Level", "oscillator.2.level"},
        {"Osc B Detune", "oscillator.2.detune"},
        {"Osc B Position", "oscillator.2.wavePosition"},
        {"Noise Level", "oscillator.noise.level"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Filter Resonance", "filter.1.resonance"},
        {"Filter Env Amount", "filter.1.envAmount"},
        {"Filter Key Track", "filter.1.keyTrack"},
        {"Filter Attack", "envelope.2.attack"},
        {"Filter Decay", "envelope.2.decay"},
        {"Filter Sustain", "envelope.2.sustain"},
        {"Filter Release", "envelope.2.release"},
        {"Amp Attack", "envelope.1.attack"},
        {"Amp Decay", "envelope.1.decay"},
        {"Amp Sustain", "envelope.1.sustain"},
        {"Amp Release", "envelope.1.release"},
        {"Wave Attack", "envelope.3.attack"},
        {"Wave Decay", "envelope.3.decay"},
        {"Wave Sustain", "envelope.3.sustain"},
        {"Wave Release", "envelope.3.release"},
        {"LFO Rate", "lfo.1.rate"},
        {"LFO to Filter", "lfo.1.toFilter"},
        {"LFO to Pitch", "lfo.1.toPitch"},
        {"Velocity to Filter", "filter.1.velocityAmount"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.supersaw ---
    {"vsm.supersaw", {
        {"Detune", "oscillator.supersaw.detune"},
        {"Mix", "oscillator.supersaw.mix"},
        {"Stereo Spread", "output.stereoWidth"},
        {"Sub Level", "oscillator.sub.level"},
        {"Noise Level", "oscillator.noise.level"},
        // « filter.hp.cutoff » et non « filter.2.cutoff » : ce coupe-bas est
        // un CORRECTEUR DE TIMBRE, pas un second filtre. L'écrire comme une
        // deuxième instance lui donnait presque l'importance du filtre
        // principal dans l'espace de recherche (rang 3 sur le Juno-106,
        // mesuré), alors qu'il n'est qu'une commande mineure. Le TYPE dans
        // l'identifiant dit ce que le numéro d'instance ne sait pas dire.
        // Seul le MS-20 garde « filter.2.* » : son HPF est résonant, c'est un
        // vrai second filtre, la moitié de l'identité de la machine.
        {"Pitch HPF", "filter.hp.cutoff"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Filter Resonance", "filter.1.resonance"},
        {"Filter Env Amount", "filter.1.envAmount"},
        {"Filter Key Track", "filter.1.keyTrack"},
        {"Filter Attack", "envelope.2.attack"},
        {"Filter Decay", "envelope.2.decay"},
        {"Filter Sustain", "envelope.2.sustain"},
        {"Filter Release", "envelope.2.release"},
        {"Amp Attack", "envelope.1.attack"},
        {"Amp Decay", "envelope.1.decay"},
        {"Amp Sustain", "envelope.1.sustain"},
        {"Amp Release", "envelope.1.release"},
        {"LFO Rate", "lfo.1.rate"},
        {"LFO to Pitch", "lfo.1.toPitch"},
        {"LFO to Filter", "lfo.1.toFilter"},
        {"Glide", "voice.glide"},
        {"Velocity to Filter", "filter.1.velocityAmount"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.obx ---
    {"vsm.obx", {
        {"Osc1 Level", "oscillator.1.level"},
        {"Osc1 Shape", "oscillator.1.waveform"},
        {"Osc1 Pulse Width", "oscillator.1.pulseWidth"},
        {"Osc2 Level", "oscillator.2.level"},
        {"Osc2 Shape", "oscillator.2.waveform"},
        {"Osc2 Pulse Width", "oscillator.2.pulseWidth"},
        {"Osc2 Detune", "oscillator.2.detune"},
        {"Sync", "oscillator.2.sync"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Filter Resonance", "filter.1.resonance"},
        {"Filter Env Amount", "filter.1.envAmount"},
        {"Filter Key Track", "filter.1.keyTrack"},
        {"Filter Slope", "filter.1.slope"},
        {"Filter Attack", "envelope.2.attack"},
        {"Filter Decay", "envelope.2.decay"},
        {"Filter Sustain", "envelope.2.sustain"},
        {"Filter Release", "envelope.2.release"},
        {"Amp Attack", "envelope.1.attack"},
        {"Amp Decay", "envelope.1.decay"},
        {"Amp Sustain", "envelope.1.sustain"},
        {"Amp Release", "envelope.1.release"},
        {"LFO Rate", "lfo.1.rate"},
        {"LFO Waveform", "lfo.1.waveform"},
        {"LFO to Pitch", "lfo.1.toPitch"},
        {"LFO to PWM", "lfo.1.toPulseWidth"},
        {"LFO to Filter", "lfo.1.toFilter"},
        {"Unison", "voice.unison"},
        {"Unison Detune", "voice.unisonDetune"},
        {"Velocity to Filter", "filter.1.velocityAmount"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.ms20 ---
    {"vsm.ms20", {
        {"VCO-1 Level", "oscillator.1.level"},
        {"VCO-1 Shape", "oscillator.1.waveform"},
        {"VCO-1 Pulse Width", "oscillator.1.pulseWidth"},
        {"VCO-2 Level", "oscillator.2.level"},
        {"VCO-2 Shape", "oscillator.2.waveform"},
        {"VCO-2 Pitch", "oscillator.2.pitch"},
        {"Noise Level", "oscillator.noise.level"},
        // Le HPF du MS-20 reste « filter.2.* » là où les autres machines
        // disent « filter.hp.* » : ici il est RÉSONANT et sculpte le timbre à
        // égalité avec le passe-bas -- c'est un second filtre à part entière,
        // pas un correcteur (voir le commentaire du supersaw).
        {"HPF Cutoff", "filter.2.cutoff"},
        {"HPF Resonance", "filter.2.resonance"},
        {"LPF Cutoff", "filter.1.cutoff"},
        {"LPF Resonance", "filter.1.resonance"},
        {"Filter Drive", "filter.1.drive"},
        {"EG to LPF", "filter.1.envAmount"},
        {"MG Rate", "lfo.1.rate"},
        {"MG Waveform", "lfo.1.waveform"},
        {"MG to Pitch", "lfo.1.toPitch"},
        {"MG to LPF", "lfo.1.toFilter"},
        {"Amp Attack", "envelope.1.attack"},
        {"Amp Decay", "envelope.1.decay"},
        {"Amp Sustain", "envelope.1.sustain"},
        {"Amp Release", "envelope.1.release"},
        {"Glide Time", "voice.glideTime"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.arpodyssey ---
    {"vsm.arpodyssey", {
        {"VCO-1 Level", "oscillator.1.level"},
        {"VCO-1 Shape", "oscillator.1.waveform"},
        {"VCO-1 Pulse Width", "oscillator.1.pulseWidth"},
        {"VCO-2 Level", "oscillator.2.level"},
        {"VCO-2 Shape", "oscillator.2.waveform"},
        {"VCO-2 Pulse Width", "oscillator.2.pulseWidth"},
        {"VCO-2 Detune", "oscillator.2.detune"},
        {"Ring Mod Level", "oscillator.ringMod"},
        {"Noise Level", "oscillator.noise.level"},
        {"Sync", "oscillator.2.sync"},
        {"HPF Cutoff", "filter.hp.cutoff"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Filter Resonance", "filter.1.resonance"},
        {"Filter Env Amount", "filter.1.envAmount"},
        {"Filter Key Track", "filter.1.keyTrack"},
        {"LFO Rate", "lfo.1.rate"},
        {"LFO Waveform", "lfo.1.waveform"},
        {"LFO to Pitch", "lfo.1.toPitch"},
        {"LFO to Filter", "lfo.1.toFilter"},
        {"Amp Attack", "envelope.1.attack"},
        {"Amp Decay", "envelope.1.decay"},
        {"Amp Sustain", "envelope.1.sustain"},
        {"Amp Release", "envelope.1.release"},
        {"Glide Time", "voice.glideTime"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.prophet ---
    {"vsm.prophet", {
        {"Osc A Level", "oscillator.1.level"},
        {"Osc A Shape", "oscillator.1.waveform"},
        {"Osc A Pulse Width", "oscillator.1.pulseWidth"},
        {"Osc B Level", "oscillator.2.level"},
        {"Osc B Shape", "oscillator.2.waveform"},
        {"Osc B Pulse Width", "oscillator.2.pulseWidth"},
        {"Osc B Detune", "oscillator.2.detune"},
        {"Sync", "oscillator.2.sync"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Filter Resonance", "filter.1.resonance"},
        {"Filter Env Amount", "filter.1.envAmount"},
        {"Filter Key Track", "filter.1.keyTrack"},
        {"Filter Attack", "envelope.2.attack"},
        {"Filter Decay", "envelope.2.decay"},
        {"Filter Sustain", "envelope.2.sustain"},
        {"Filter Release", "envelope.2.release"},
        {"Amp Attack", "envelope.1.attack"},
        {"Amp Decay", "envelope.1.decay"},
        {"Amp Sustain", "envelope.1.sustain"},
        {"Amp Release", "envelope.1.release"},
        {"LFO Rate", "lfo.1.rate"},
        {"LFO to Pitch", "lfo.1.toPitch"},
        {"LFO to Filter", "lfo.1.toFilter"},
        {"PolyMod Filt Env", "polyMod.sourceFilterEnv"},
        {"PolyMod Osc B", "polyMod.sourceOscB"},
        {"PolyMod to Freq A", "polyMod.toOscAFrequency"},
        {"PolyMod to PW A", "polyMod.toOscAPulseWidth"},
        {"PolyMod to Filter", "polyMod.toFilterCutoff"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.tr909 ---
    {"vsm.tr909", {
        {"Kick Level", "drum.kick.level"},
        {"Kick Tune", "drum.kick.tune"},
        {"Kick Decay", "drum.kick.decay"},
        {"Kick Attack", "drum.kick.attack"},
        {"Snare Level", "drum.snare.level"},
        {"Snare Tune", "drum.snare.tune"},
        {"Snare Decay", "drum.snare.decay"},
        {"Snare Snappy", "drum.snare.snappy"},
        {"Closed Hat Level", "drum.closedHat.level"},
        {"Closed Hat Decay", "drum.closedHat.decay"},
        {"Open Hat Level", "drum.openHat.level"},
        {"Open Hat Decay", "drum.openHat.decay"},
        {"Clap Level", "drum.clap.level"},
        {"Clap Decay", "drum.clap.decay"},
        {"Crash Level", "drum.crash.level"},
        {"Crash Decay", "drum.crash.decay"},
        {"Tom Level", "drum.tom.level"},
        {"Tom Tune", "drum.tom.tune"},
        {"Tom Decay", "drum.tom.decay"},
        {"Accent", "accent.amount"},
    }},
    // --- vsm.perc ---
    // MÊME GRAMMAIRE QUE LES BOÎTES À RYTHMES DU PARC : `drum.<pièce>.<réglage>`.
    // Les pièces sont nouvelles -- aucune autre machine ne déclare de conga ni
    // de claves -- mais les réglages portent les noms canoniques (`level`,
    // `tune`, `decay`), pour qu'un outil qui sait accorder un tom sache
    // accorder un conga sans rien apprendre.
    {"vsm.perc", {
        {"Conga Level", "drum.conga.level"},
        {"Conga Tune", "drum.conga.tune"},
        {"Conga Decay", "drum.conga.decay"},
        {"Bongo Level", "drum.bongo.level"},
        {"Bongo Tune", "drum.bongo.tune"},
        {"Bongo Decay", "drum.bongo.decay"},
        {"Timbale Level", "drum.timbale.level"},
        {"Timbale Tune", "drum.timbale.tune"},
        {"Timbale Decay", "drum.timbale.decay"},
        {"Cowbell Level", "drum.cowbell.level"},
        {"Cowbell Tune", "drum.cowbell.tune"},
        {"Cowbell Decay", "drum.cowbell.decay"},
        {"Wood Level", "drum.woodblock.level"},
        {"Wood Tune", "drum.woodblock.tune"},
        {"Wood Decay", "drum.woodblock.decay"},
        {"Shaker Level", "drum.shaker.level"},
        {"Shaker Tone", "drum.shaker.tone"},
        {"Shaker Decay", "drum.shaker.decay"},
        {"Tambourine Level", "drum.tambourine.level"},
        {"Tambourine Decay", "drum.tambourine.decay"},
        {"Accent", "accent.amount"},
    }},
    // --- vsm.dx7 ---
    {"vsm.dx7", {
        {"Algorithm", "fm.algorithm"},
        {"Feedback", "fm.feedback"},
        {"LFO Rate", "lfo.1.rate"},
        {"LFO to Pitch", "lfo.1.toPitch"},
        {"Velocity Sens", "voice.velocitySensitivity"},
        {"Pitch Env Amount", "envelope.3.amount"},
        {"Pitch Env Time", "envelope.3.time"},
        {"Key Level Scaling", "fm.keyLevelScaling"},
        {"Analog Character", "voice.analogCharacter"},
        {"Op1 Ratio", "fm.operator.1.ratio"},
        {"Op1 Level", "fm.operator.1.level"},
        {"Op1 Attack", "fm.operator.1.attack"},
        {"Op1 Decay", "fm.operator.1.decay"},
        {"Op1 Sustain", "fm.operator.1.sustain"},
        {"Op1 Release", "fm.operator.1.release"},
        {"Op1 Fixed", "fm.operator.1.fixedFrequency"},
        {"Op2 Ratio", "fm.operator.2.ratio"},
        {"Op2 Level", "fm.operator.2.level"},
        {"Op2 Attack", "fm.operator.2.attack"},
        {"Op2 Decay", "fm.operator.2.decay"},
        {"Op2 Sustain", "fm.operator.2.sustain"},
        {"Op2 Release", "fm.operator.2.release"},
        {"Op2 Fixed", "fm.operator.2.fixedFrequency"},
        {"Op3 Ratio", "fm.operator.3.ratio"},
        {"Op3 Level", "fm.operator.3.level"},
        {"Op3 Attack", "fm.operator.3.attack"},
        {"Op3 Decay", "fm.operator.3.decay"},
        {"Op3 Sustain", "fm.operator.3.sustain"},
        {"Op3 Release", "fm.operator.3.release"},
        {"Op3 Fixed", "fm.operator.3.fixedFrequency"},
        {"Op4 Ratio", "fm.operator.4.ratio"},
        {"Op4 Level", "fm.operator.4.level"},
        {"Op4 Attack", "fm.operator.4.attack"},
        {"Op4 Decay", "fm.operator.4.decay"},
        {"Op4 Sustain", "fm.operator.4.sustain"},
        {"Op4 Release", "fm.operator.4.release"},
        {"Op4 Fixed", "fm.operator.4.fixedFrequency"},
        {"Op5 Ratio", "fm.operator.5.ratio"},
        {"Op5 Level", "fm.operator.5.level"},
        {"Op5 Attack", "fm.operator.5.attack"},
        {"Op5 Decay", "fm.operator.5.decay"},
        {"Op5 Sustain", "fm.operator.5.sustain"},
        {"Op5 Release", "fm.operator.5.release"},
        {"Op5 Fixed", "fm.operator.5.fixedFrequency"},
        {"Op6 Ratio", "fm.operator.6.ratio"},
        {"Op6 Level", "fm.operator.6.level"},
        {"Op6 Attack", "fm.operator.6.attack"},
        {"Op6 Decay", "fm.operator.6.decay"},
        {"Op6 Sustain", "fm.operator.6.sustain"},
        {"Op6 Release", "fm.operator.6.release"},
        {"Op6 Fixed", "fm.operator.6.fixedFrequency"},
    }},
    // --- vsm.tr808 ---
    {"vsm.tr808", {
        {"Kick Level", "drum.kick.level"},
        {"Kick Tune", "drum.kick.tune"},
        {"Kick Decay", "drum.kick.decay"},
        {"Snare Level", "drum.snare.level"},
        {"Snare Tune", "drum.snare.tune"},
        {"Snare Decay", "drum.snare.decay"},
        {"Snare Snappy", "drum.snare.snappy"},
        {"Closed Hat Level", "drum.closedHat.level"},
        {"Closed Hat Decay", "drum.closedHat.decay"},
        {"Open Hat Level", "drum.openHat.level"},
        {"Open Hat Decay", "drum.openHat.decay"},
        {"Clap Level", "drum.clap.level"},
        {"Clap Decay", "drum.clap.decay"},
        {"Cowbell Level", "drum.cowbell.level"},
        {"Cowbell Tune", "drum.cowbell.tune"},
        {"Accent", "accent.amount"},
    }},
    // --- vsm.jupiter8 ---
    {"vsm.jupiter8", {
        {"VCO-1 Level", "oscillator.1.level"},
        {"VCO-1 Shape", "oscillator.1.waveform"},
        {"VCO-1 Pulse Width", "oscillator.1.pulseWidth"},
        {"VCO-2 Level", "oscillator.2.level"},
        {"VCO-2 Shape", "oscillator.2.waveform"},
        {"VCO-2 Pulse Width", "oscillator.2.pulseWidth"},
        {"VCO-2 Detune", "oscillator.2.detune"},
        {"Cross Mod", "oscillator.crossMod"},
        {"Sync", "oscillator.2.sync"},
        {"HPF Cutoff", "filter.hp.cutoff"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Filter Resonance", "filter.1.resonance"},
        {"Filter Env Amount", "filter.1.envAmount"},
        {"Filter Key Track", "filter.1.keyTrack"},
        {"Env 1 Attack", "envelope.1.attack"},
        {"Env 1 Decay", "envelope.1.decay"},
        {"Env 1 Sustain", "envelope.1.sustain"},
        {"Env 1 Release", "envelope.1.release"},
        {"Env 2 Attack", "envelope.2.attack"},
        {"Env 2 Decay", "envelope.2.decay"},
        {"Env 2 Sustain", "envelope.2.sustain"},
        {"Env 2 Release", "envelope.2.release"},
        {"LFO Rate", "lfo.1.rate"},
        {"LFO to Pitch", "lfo.1.toPitch"},
        {"LFO to Filter", "lfo.1.toFilter"},
        {"LFO to PWM", "lfo.1.toPulseWidth"},
        {"Chorus Mode", "effect.chorus.mode"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.tb303 ---
    {"vsm.tb303", {
        {"Waveform", "oscillator.1.waveform"},
        {"Cutoff", "filter.1.cutoff"},
        {"Resonance", "filter.1.resonance"},
        {"Env Mod", "filter.1.envAmount"},
        {"Decay", "envelope.1.decay"},
        {"Accent", "accent.amount"},
        {"Accent Threshold", "accent.threshold"},
        {"Glide Time", "voice.glideTime"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.juno106 ---
    {"vsm.juno106", {
        {"DCO Saw Level", "oscillator.1.sawLevel"},
        {"DCO Pulse Level", "oscillator.1.pulseLevel"},
        {"DCO Sub Level", "oscillator.1.subLevel"},
        {"Noise Level", "oscillator.noise.level"},
        {"Pulse Width", "oscillator.1.pulseWidth"},
        {"PWM LFO Amount", "lfo.1.toPulseWidth"},
        {"LFO Rate", "lfo.1.rate"},
        {"LFO Delay", "lfo.1.delay"},
        {"LFO Pitch Amount", "lfo.1.toPitch"},
        {"HPF Cutoff", "filter.hp.cutoff"},
        {"VCF Cutoff", "filter.1.cutoff"},
        {"VCF Resonance", "filter.1.resonance"},
        {"VCF Env Amount", "filter.1.envAmount"},
        {"VCF LFO Amount", "lfo.1.toFilter"},
        {"VCF Key Track", "filter.1.keyTrack"},
        {"Env Attack", "envelope.1.attack"},
        {"Env Decay", "envelope.1.decay"},
        {"Env Sustain", "envelope.1.sustain"},
        {"Env Release", "envelope.1.release"},
        {"Chorus Mode", "effect.chorus.mode"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.minimoog ---
    {"vsm.minimoog", {
        {"Osc1 Waveform", "oscillator.1.waveform"},
        {"Osc2 Waveform", "oscillator.2.waveform"},
        {"Osc3 Waveform", "oscillator.3.waveform"},
        {"Osc1 Level", "oscillator.1.level"},
        {"Osc2 Level", "oscillator.2.level"},
        {"Osc3 Level", "oscillator.3.level"},
        {"Noise Level", "oscillator.noise.level"},
        {"Osc2 Detune", "oscillator.2.detune"},
        {"Osc3 Detune", "oscillator.3.detune"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Filter Resonance", "filter.1.resonance"},
        {"Filter Drive", "filter.1.drive"},
        {"Filter Env Amount", "filter.1.envAmount"},
        {"Filter Key Track", "filter.1.keyTrack"},
        {"Filter Attack", "envelope.2.attack"},
        {"Filter Decay", "envelope.2.decay"},
        {"Filter Sustain", "envelope.2.sustain"},
        {"Amp Attack", "envelope.1.attack"},
        {"Amp Decay", "envelope.1.decay"},
        {"Amp Sustain", "envelope.1.sustain"},
        {"Glide Time", "voice.glideTime"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.sh101 ---
    {"vsm.sh101", {
        {"Saw Level", "oscillator.1.sawLevel"},
        {"Pulse Level", "oscillator.1.pulseLevel"},
        {"Sub Level", "oscillator.1.subLevel"},
        {"Noise Level", "oscillator.noise.level"},
        {"Pulse Width", "oscillator.1.pulseWidth"},
        {"PWM LFO Amount", "lfo.1.toPulseWidth"},
        {"Sub Type", "oscillator.sub.type"},
        {"LFO Rate", "lfo.1.rate"},
        {"LFO Waveform", "lfo.1.waveform"},
        {"LFO Pitch Amount", "lfo.1.toPitch"},
        {"LFO Filter Amount", "lfo.1.toFilter"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Filter Resonance", "filter.1.resonance"},
        {"Filter Env Amount", "filter.1.envAmount"},
        {"Filter Key Track", "filter.1.keyTrack"},
        {"Env Attack", "envelope.1.attack"},
        {"Env Decay", "envelope.1.decay"},
        {"Env Sustain", "envelope.1.sustain"},
        {"Env Release", "envelope.1.release"},
        {"VCA Mode", "envelope.1.mode"},
        {"Glide Time", "voice.glideTime"},
        {"Analog Character", "voice.analogCharacter"},
    }},
    // --- vsm.testtone ---
    {"vsm.testtone", {
        {"Waveform", "oscillator.1.waveform"},
        {"Filter Cutoff", "filter.1.cutoff"},
        {"Resonance", "filter.1.resonance"},
        {"Attack", "envelope.1.attack"},
        {"Decay", "envelope.1.decay"},
        {"Sustain", "envelope.1.sustain"},
        {"Release", "envelope.1.release"},
    }},
    // --- fx.filter ---
    {"fx.filter", {
        {"Cutoff", "effect.filter.cutoff"},
        {"Resonance", "effect.filter.resonance"},
        {"Mode", "effect.filter.type"},
        {"Mix", "effect.filter.mix"},
    }},
    // --- fx.distortion ---
    {"fx.distortion", {
        {"Drive", "effect.distortion.drive"},
        {"Mode", "effect.distortion.type"},
        {"Tone", "effect.distortion.tone"},
        {"Mix", "effect.distortion.mix"},
        {"Output", "effect.distortion.output"},
    }},
    // --- fx.bitcrusher ---
    {"fx.bitcrusher", {
        {"Bits", "effect.bitcrusher.bits"},
        {"Downsample", "effect.bitcrusher.downsample"},
        {"Mix", "effect.bitcrusher.mix"},
    }},
    // --- fx.chorus ---
    {"fx.chorus", {
        {"Rate", "effect.chorus.rate"},
        {"Depth", "effect.chorus.depth"},
        {"Mix", "effect.chorus.mix"},
    }},
    // --- fx.flanger ---
    {"fx.flanger", {
        {"Rate", "effect.flanger.rate"},
        {"Depth", "effect.flanger.depth"},
        {"Feedback", "effect.flanger.feedback"},
        {"Mix", "effect.flanger.mix"},
    }},
    // --- fx.phaser ---
    {"fx.phaser", {
        {"Rate", "effect.phaser.rate"},
        {"Depth", "effect.phaser.depth"},
        {"Feedback", "effect.phaser.feedback"},
        {"Mix", "effect.phaser.mix"},
    }},
    // --- fx.delay ---
    {"fx.delay", {
        {"Time", "effect.delay.time"},
        {"Feedback", "effect.delay.feedback"},
        {"Mix", "effect.delay.mix"},
        {"Ping-Pong", "effect.delay.pingPong"},
        {"Tone", "effect.delay.tone"},
    }},
    // --- fx.reverb ---
    {"fx.reverb", {
        {"Size", "effect.reverb.size"},
        {"Damping", "effect.reverb.damping"},
        {"Width", "effect.reverb.width"},
        {"Mix", "effect.reverb.mix"},
    }},
    // --- fx.tape ---
    {"fx.tape", {
        {"Drive", "effect.tape.drive"},
        {"Tone", "effect.tape.tone"},
        {"Mix", "effect.tape.mix"},
    }},
    };
    return table;
}

const NameToSemantic* tableFor(const std::string& pluginId) {
    for (const auto& [id, entries] : semanticTable())
        if (id == pluginId) return &entries;
    return nullptr;
}

} // namespace

const char* fidelityName(Fidelity fidelity) {
    switch (fidelity) {
        case Fidelity::Measured:     return "measured";
        case Fidelity::Derived:      return "derived";
        case Fidelity::Estimated:    return "estimated";
        case Fidelity::Approximated: return "approximated";
        case Fidelity::Unknown:      return "unknown";
    }
    return "unknown";
}

const char* supportStatusName(SupportStatus status) {
    switch (status) {
        case SupportStatus::Supported:    return "supported";
        case SupportStatus::Approximated: return "approximated";
        case SupportStatus::Unsupported:  return "unsupported";
    }
    return "unsupported";
}

const ParameterDescriptor* SemanticProfile::findBySemanticId(const std::string& semanticId) const {
    for (const auto& descriptor : parameters_)
        if (descriptor.semanticId == semanticId) return &descriptor;
    return nullptr;
}

const ParameterDescriptor* SemanticProfile::findByParamId(vsm::audio::plugin::ParamId id) const {
    for (const auto& descriptor : parameters_)
        if (descriptor.paramId == id) return &descriptor;
    return nullptr;
}

std::string lookupSemanticId(const std::string& pluginId, const std::string& parameterName) {
    const NameToSemantic* entries = tableFor(pluginId);
    if (!entries) return {};
    for (const auto& [name, semantic] : *entries)
        if (name == parameterName) return semantic;
    return {};
}

std::vector<std::string> knownSemanticPluginIds() {
    std::vector<std::string> ids;
    ids.reserve(semanticTable().size());
    for (const auto& [id, entries] : semanticTable()) ids.push_back(id);
    return ids;
}

namespace {

/// Premier segment du semanticId : "filter.1.cutoff" -> "filter". Sert à
/// regrouper les paramètres par module dans les presets et les interfaces.
std::string moduleOf(const std::string& semanticId) {
    const size_t dot = semanticId.find('.');
    return dot == std::string::npos ? semanticId : semanticId.substr(0, dot);
}

/// Récupère la ParameterList réelle, machine ou effet. C'est la SOURCE de
/// vérité : la table sémantique ne fait qu'y attacher des identités, elle
/// n'invente jamais un paramètre qui n'existe pas.
std::optional<vsm::audio::plugin::ParameterList> parameterListFor(const std::string& pluginId) {
    if (pluginId.rfind("fx.", 0) == 0) {
        auto effect = vsm::audio::effect::EffectFactory::create(pluginId.substr(3));
        if (!effect) return std::nullopt;
        effect->prepare(48000.0, 512);
        return effect->parameterList();
    }
    // registerBuiltInPlugins() est idempotent, mais l'appeler depuis plusieurs
    // threads ne l'est pas : once_flag plutôt que de compter sur l'appelant.
    static std::once_flag registration;
    std::call_once(registration, [] { vsm::audio::plugin::registerBuiltInPlugins(); });

    auto plugin = vsm::audio::plugin::PluginRegistry::instance().create(pluginId);
    if (!plugin) return std::nullopt;
    plugin->initialize(48000.0, 512);
    return plugin->parameterList();
}

} // namespace

SemanticProfile buildSemanticProfile(const std::string& pluginId) {
    auto parameters = parameterListFor(pluginId);
    if (!parameters) return {};

    std::vector<ParameterDescriptor> descriptors;
    descriptors.reserve(parameters->size());
    for (const auto& info : *parameters) {
        ParameterDescriptor descriptor;
        descriptor.semanticId = lookupSemanticId(pluginId, info.name);
        descriptor.displayName = info.name;
        descriptor.module = moduleOf(descriptor.semanticId);
        descriptor.paramId = info.id;
        descriptor.minimum = info.minValue;
        descriptor.maximum = info.maxValue;
        descriptor.defaultValue = info.defaultValue;
        descriptor.unit = info.unit;
        // Aucune de ces machines n'a été comparée à du matériel réel (§ 27 de
        // ARCHITECTURE.md le dit sans détour) : le statut honnête est donc
        // "derived" -- dérivé d'une architecture documentée -- et surtout pas
        // "measured".
        descriptor.fidelity = Fidelity::Derived;
        descriptors.push_back(std::move(descriptor));
    }
    return SemanticProfile(pluginId, std::move(descriptors));
}

} // namespace vsm::interchange
