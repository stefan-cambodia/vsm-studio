#include "vsm/panels/MachinePanel.h"
#include <algorithm>

namespace vsm::panels {

namespace {

using S = ControlStyle;

PanelControl control(std::string parameter, std::string caption, S style, int column, int row,
                      int columnSpan = 1, int rowSpan = 1) {
    PanelControl c;
    c.parameterName = std::move(parameter);
    c.caption = std::move(caption);
    c.style = style;
    c.column = column;
    c.row = row;
    c.columnSpan = columnSpan;
    c.rowSpan = rowSpan;
    return c;
}

// ---------------------------------------------------------------------------
// Minimoog-style
//
// La façade d'origine se lit de gauche à droite comme le signal : banc
// d'oscillateurs, mélangeur, modifieurs (filtre puis enveloppes), sortie.
// C'est cette lecture qui fait qu'on sait s'en servir sans mode d'emploi, et
// c'est elle qu'on reproduit -- pas une image du panneau.
//
// Châssis bois, panneau anthracite, sérigraphie claire : les trois traits
// visuels qui font reconnaître la machine au premier coup d'œil.
// ---------------------------------------------------------------------------
MachinePanel makeMinimoog() {
    MachinePanel panel;
    panel.pluginId = "vsm.minimoog";
    panel.displayName = "Minimoog-style";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#2A2724";
    panel.sectionColour = "#1E1C1A";
    panel.textColour = "#E6E1D6";
    panel.knobColour = "#1C1A18"; // boutons noirs sur panneau anthracite
    panel.gridColumns = 16;
    panel.gridRows = 6;

    PanelSection oscillators;
    oscillators.title = "OSCILLATOR BANK";
    oscillators.accentColour = "#C8A24D";
    oscillators.column = 0; oscillators.row = 0; oscillators.columnSpan = 5; oscillators.rowSpan = 4;
    oscillators.controls = {
        control("Osc1 Waveform", "OSC 1 WAVE", S::Selector, 0, 0),
        control("Osc2 Waveform", "OSC 2 WAVE", S::Selector, 0, 1),
        control("Osc3 Waveform", "OSC 3 WAVE", S::Selector, 0, 2),
        // Le désaccord ne concerne que les oscillateurs 2 et 3 : sur la
        // machine d'origine, l'oscillateur 1 sert de référence d'accord.
        control("Osc2 Detune", "OSC 2 FREQ", S::Knob, 1, 1),
        control("Osc3 Detune", "OSC 3 FREQ", S::Knob, 1, 2),
    };

    PanelSection mixer;
    mixer.title = "MIXER";
    mixer.accentColour = "#C8A24D";
    mixer.column = 5; mixer.row = 0; mixer.columnSpan = 2; mixer.rowSpan = 4;
    mixer.controls = {
        control("Osc1 Level", "OSC 1", S::Knob, 0, 0),
        control("Osc2 Level", "OSC 2", S::Knob, 1, 0),
        control("Osc3 Level", "OSC 3", S::Knob, 0, 1),
        control("Noise Level", "NOISE", S::Knob, 1, 1),
    };

    PanelSection modifiers;
    modifiers.title = "MODIFIERS";
    modifiers.accentColour = "#C8A24D";
    modifiers.column = 7; modifiers.row = 0; modifiers.columnSpan = 9; modifiers.rowSpan = 4;
    modifiers.controls = {
        // Le gros potentiomètre de coupure est LE geste de cette machine :
        // il est plus grand que les autres, comme sur l'original.
        control("Filter Cutoff", "CUTOFF FREQUENCY", S::LargeKnob, 0, 0, 2, 2),
        control("Filter Resonance", "EMPHASIS", S::Knob, 2, 0),
        control("Filter Env Amount", "AMOUNT OF CONTOUR", S::Knob, 3, 0),
        control("Filter Key Track", "KEY TRACK", S::Knob, 4, 0),
        control("Filter Drive", "DRIVE", S::Knob, 5, 0),
        control("Filter Attack", "FILTER ATTACK", S::Knob, 3, 1),
        control("Filter Decay", "FILTER DECAY", S::Knob, 4, 1),
        control("Filter Sustain", "FILTER SUSTAIN", S::Knob, 5, 1),
        control("Amp Attack", "LOUDNESS ATTACK", S::Knob, 6, 0),
        control("Amp Decay", "LOUDNESS DECAY", S::Knob, 7, 0),
        control("Amp Sustain", "LOUDNESS SUSTAIN", S::Knob, 6, 1),
    };

    PanelSection controllers;
    controllers.title = "CONTROLLERS";
    controllers.accentColour = "#8A8892";
    controllers.column = 0; controllers.row = 4; controllers.columnSpan = 16; controllers.rowSpan = 2;
    controllers.contentColumns = 8; // deux commandes à gauche, comme les molettes de l'original
    controllers.controls = {
        control("Glide Time", "GLIDE", S::Knob, 0, 0),
        control("Analog Character", "ANALOG DRIFT", S::Knob, 1, 0),
    };

    panel.sections = {oscillators, mixer, modifiers, controllers};
    return panel;
}

// ---------------------------------------------------------------------------
// TB-303-style
//
// Une seule rangée de potentiomètres sur un boîtier plat argenté : la machine
// tient dans un geste, et c'est justement ce qui a fait son usage. On garde
// donc la rangée unique plutôt que de « mieux » ranger les commandes.
// ---------------------------------------------------------------------------
MachinePanel makeTb303() {
    MachinePanel panel;
    panel.pluginId = "vsm.tb303";
    panel.displayName = "TB-303-style";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#9A9A97";
    panel.sectionColour = "#8C8C89";
    panel.textColour = "#1A1A1A";
    panel.knobColour = "#2A2A2C"; // boutons noirs sur boîtier argenté
    panel.gridColumns = 8;
    panel.gridRows = 3;

    PanelSection main;
    main.title = "SYNTHESIZER";
    main.accentColour = "#C4462F"; // le liseré rouge-orangé caractéristique
    main.column = 0; main.row = 0; main.columnSpan = 8; main.rowSpan = 2;
    main.controls = {
        control("Waveform", "WAVEFORM", S::Toggle, 0, 0),
        control("Cutoff", "CUT OFF FREQ", S::Knob, 1, 0),
        control("Resonance", "RESONANCE", S::Knob, 2, 0),
        control("Env Mod", "ENV MOD", S::Knob, 3, 0),
        control("Decay", "DECAY", S::Knob, 4, 0),
        control("Accent", "ACCENT", S::Knob, 5, 0),
        control("Glide Time", "SLIDE TIME", S::Knob, 6, 0),
    };

    PanelSection setup;
    setup.title = "RÉGLAGES";
    setup.accentColour = "#5A5A57";
    setup.column = 0; setup.row = 2; setup.columnSpan = 8; setup.rowSpan = 1;
    setup.contentColumns = 8;
    setup.controls = {
        control("Accent Threshold", "ACCENT THRESHOLD", S::Knob, 0, 0),
        control("Analog Character", "ANALOG DRIFT", S::Knob, 1, 0),
    };

    // L'éditeur de motif : sur la machine d'origine, c'est par lui qu'on
    // écrit la musique -- les potentiomètres ne font que la colorer.
    panel.sequencer.kind = SequencerKind::MonoPattern;
    panel.sequencer.title = "PATTERN";
    panel.sequencer.stepCount = 16;
    panel.sequencer.defaultNote = 36;
    panel.sequencer.rowSpan = 2;
    panel.gridRows += panel.sequencer.rowSpan;

    panel.sections = {main, setup};
    return panel;
}

// ---------------------------------------------------------------------------
// TR-808-style
//
// Ici la disposition EST l'instrument : une colonne de commandes par pièce de
// batterie, alignées sous leur nom, plus l'accent à part. Regrouper « tous les
// niveaux » puis « tous les decays » serait plus compact et rendrait la
// machine inutilisable -- on règle une pièce, pas une catégorie.
// ---------------------------------------------------------------------------
MachinePanel makeTr808() {
    MachinePanel panel;
    panel.pluginId = "vsm.tr808";
    panel.displayName = "TR-808-style";
    panel.chassis = Chassis::Plastic;
    panel.panelColour = "#3A3A3C";
    panel.sectionColour = "#2C2C2E";
    panel.textColour = "#EDEDE8";
    panel.knobColour = "#DCD6C8"; // boutons crème, comme les commandes de la machine d'origine
    panel.gridColumns = 12;
    panel.gridRows = 5;

    auto voiceSection = [](std::string title, std::string accent, int column, int span,
                            std::vector<PanelControl> controls) {
        PanelSection section;
        section.title = std::move(title);
        section.accentColour = std::move(accent);
        section.column = column;
        section.row = 0;
        section.columnSpan = span;
        section.rowSpan = 3;
        section.controls = std::move(controls);
        return section;
    };

    // Le nuancier de la machine : orange, jaune, ocre, blanc cassé -- couleurs
    // qui séparaient les familles de pièces sur le panneau d'origine.
    const std::string orange = "#D96C2C";
    const std::string yellow = "#D9B23A";
    const std::string cream = "#D8D2C4";

    panel.sections = {
        voiceSection("BASS DRUM", orange, 0, 3, {
            control("Kick Level", "LEVEL", S::Knob, 0, 0),
            control("Kick Tune", "TONE", S::Knob, 1, 0),
            control("Kick Decay", "DECAY", S::Knob, 2, 0),
        }),
        voiceSection("SNARE DRUM", orange, 3, 3, {
            control("Snare Level", "LEVEL", S::Knob, 0, 0),
            control("Snare Tune", "TONE", S::Knob, 1, 0),
            control("Snare Decay", "DECAY", S::Knob, 2, 0),
            control("Snare Snappy", "SNAPPY", S::Knob, 1, 1),
        }),
        voiceSection("CYMBAL / HAT", yellow, 6, 3, {
            control("Closed Hat Level", "CH LEVEL", S::Knob, 0, 0),
            control("Closed Hat Decay", "CH DECAY", S::Knob, 1, 0),
            control("Open Hat Level", "OH LEVEL", S::Knob, 0, 1),
            control("Open Hat Decay", "OH DECAY", S::Knob, 1, 1),
        }),
        voiceSection("CLAP / COWBELL", cream, 9, 3, {
            control("Clap Level", "CP LEVEL", S::Knob, 0, 0),
            control("Clap Decay", "CP DECAY", S::Knob, 1, 0),
            control("Cowbell Level", "CB LEVEL", S::Knob, 0, 1),
            control("Cowbell Tune", "CB TUNE", S::Knob, 1, 1),
        }),
    };

    PanelSection accent;
    accent.title = "ACCENT";
    accent.accentColour = "#C4462F";
    accent.column = 0; accent.row = 3; accent.columnSpan = 3; accent.rowSpan = 2;
    accent.contentColumns = 2; // un seul bouton, à gauche -- pas étalé sur toute la largeur
    accent.controls = { control("Accent", "ACCENT", S::Knob, 0, 0) };
    panel.sections.push_back(accent);

    // Les seize pas, une ligne par pièce : c'est l'instrument lui-même.
    panel.sequencer.kind = SequencerKind::DrumGrid;
    panel.sequencer.title = "RHYTHM PATTERN";
    panel.sequencer.stepCount = 16;
    panel.sequencer.rowSpan = 3;
    panel.sequencer.lanes = {
        {"BASS DRUM", 36}, {"SNARE", 38}, {"CLAP", 39},
        {"CLOSED HAT", 42}, {"OPEN HAT", 46}, {"COWBELL", 56},
    };
    panel.gridRows += panel.sequencer.rowSpan;
    return panel;
}


// ---------------------------------------------------------------------------
// TR-909-style
//
// Même principe que le TR-808 -- une colonne de commandes par pièce -- mais la
// machine est plus fournie (attaque de grosse caisse, toms, crash) et son
// panneau est plus clair, avec le liseré orange/rouge qui sépare les groupes.
// La grille de pas partage les mêmes seize boutons colorés par quatre.
// ---------------------------------------------------------------------------
MachinePanel makeTr909() {
    MachinePanel panel;
    panel.pluginId = "vsm.tr909";
    panel.displayName = "TR-909-style";
    panel.chassis = Chassis::Plastic;
    panel.panelColour = "#43444A";
    panel.sectionColour = "#33343A";
    panel.textColour = "#EFEFEA";
    panel.knobColour = "#D9D4C8";
    panel.gridColumns = 15;
    panel.gridRows = 5;

    auto voice = [](std::string title, std::string accent, int column, int span,
                     std::vector<PanelControl> controls) {
        PanelSection section;
        section.title = std::move(title);
        section.accentColour = std::move(accent);
        section.column = column;
        section.row = 0;
        section.columnSpan = span;
        section.rowSpan = 3;
        section.controls = std::move(controls);
        return section;
    };

    const std::string red = "#C4462F";
    const std::string orange = "#E08A2E";
    const std::string steel = "#9FB0C0";

    panel.sections = {
        voice("BASS DRUM", red, 0, 4, {
            control("Kick Level", "LEVEL", S::Knob, 0, 0),
            control("Kick Tune", "TUNE", S::Knob, 1, 0),
            control("Kick Attack", "ATTACK", S::Knob, 0, 1),
            control("Kick Decay", "DECAY", S::Knob, 1, 1),
        }),
        voice("SNARE DRUM", red, 4, 4, {
            control("Snare Level", "LEVEL", S::Knob, 0, 0),
            control("Snare Tune", "TUNE", S::Knob, 1, 0),
            control("Snare Snappy", "SNAPPY", S::Knob, 0, 1),
            control("Snare Decay", "DECAY", S::Knob, 1, 1),
        }),
        voice("TOM / CLAP", orange, 8, 4, {
            control("Tom Level", "TOM LEVEL", S::Knob, 0, 0),
            control("Tom Tune", "TOM TUNE", S::Knob, 1, 0),
            control("Tom Decay", "TOM DECAY", S::Knob, 0, 1),
            control("Clap Level", "CLAP LEVEL", S::Knob, 1, 1),
            control("Clap Decay", "CLAP DECAY", S::Knob, 2, 1),
        }),
        voice("CYMBAL / HAT", steel, 12, 3, {
            control("Closed Hat Level", "CH LEVEL", S::Knob, 0, 0),
            control("Open Hat Level", "OH LEVEL", S::Knob, 1, 0),
            control("Closed Hat Decay", "CH DECAY", S::Knob, 0, 1),
            control("Open Hat Decay", "OH DECAY", S::Knob, 1, 1),
            control("Crash Level", "CRASH", S::Knob, 2, 0),
            control("Crash Decay", "CR DECAY", S::Knob, 2, 1),
        }),
    };

    PanelSection accent;
    accent.title = "ACCENT";
    accent.accentColour = red;
    accent.column = 0; accent.row = 3; accent.columnSpan = 3; accent.rowSpan = 2;
    accent.contentColumns = 2;
    accent.controls = { control("Accent", "ACCENT", S::Knob, 0, 0) };
    panel.sections.push_back(accent);

    panel.sequencer.kind = SequencerKind::DrumGrid;
    panel.sequencer.title = "RHYTHM PATTERN";
    panel.sequencer.stepCount = 16;
    panel.sequencer.rowSpan = 3;
    panel.sequencer.lanes = {
        {"BASS DRUM", 36}, {"SNARE", 38}, {"CLAP", 39}, {"LOW TOM", 45},
        {"CLOSED HAT", 42}, {"OPEN HAT", 46}, {"CRASH", 49},
    };
    panel.gridRows += panel.sequencer.rowSpan;
    return panel;
}

// ---------------------------------------------------------------------------
// SH-101-style
//
// Le premier panneau à CURSEURS du projet : sur cette machine, tout se règle
// par de petits faders alignés, et c'est ce qui la rend lisible d'un regard --
// on voit la forme du son avant de lire les intitulés. Boîtier coloré (gris,
// bleu ou rouge selon les séries), sérigraphie claire.
// ---------------------------------------------------------------------------
MachinePanel makeSh101() {
    MachinePanel panel;
    panel.pluginId = "vsm.sh101";
    panel.displayName = "SH-101-style";
    panel.chassis = Chassis::Plastic;
    panel.panelColour = "#4A5560";
    panel.sectionColour = "#3B444E";
    panel.textColour = "#EDEFF2";
    panel.knobColour = "#20242A";
    panel.gridColumns = 16;
    panel.gridRows = 5;

    PanelSection source;
    source.title = "SOURCE MIXER";
    source.accentColour = "#E0A33C";
    source.column = 0; source.row = 0; source.columnSpan = 5; source.rowSpan = 5;
    source.controls = {
        control("Saw Level", "SAW", S::VerticalSlider, 0, 0),
        control("Pulse Level", "PULSE", S::VerticalSlider, 1, 0),
        control("Sub Level", "SUB", S::VerticalSlider, 2, 0),
        control("Noise Level", "NOISE", S::VerticalSlider, 3, 0),
        control("Sub Type", "SUB TYPE", S::Selector, 4, 0),
        control("Analog Character", "ANALOG DRIFT", S::Knob, 4, 1),
    };

    PanelSection modulation;
    modulation.title = "LFO / PWM";
    modulation.accentColour = "#79B4D0";
    modulation.column = 5; modulation.row = 0; modulation.columnSpan = 4; modulation.rowSpan = 5;
    modulation.controls = {
        control("LFO Rate", "RATE", S::VerticalSlider, 0, 0),
        control("LFO Pitch Amount", "TO PITCH", S::VerticalSlider, 1, 0),
        control("LFO Filter Amount", "TO VCF", S::VerticalSlider, 2, 0),
        control("PWM LFO Amount", "TO PWM", S::VerticalSlider, 3, 0),
        control("LFO Waveform", "LFO WAVE", S::Selector, 0, 1),
        control("Pulse Width", "PULSE WIDTH", S::Knob, 1, 1),
    };

    PanelSection filter;
    filter.title = "VCF";
    filter.accentColour = "#E0A33C";
    filter.column = 9; filter.row = 0; filter.columnSpan = 4; filter.rowSpan = 5;
    filter.controls = {
        control("Filter Cutoff", "CUTOFF", S::VerticalSlider, 0, 0),
        control("Filter Resonance", "RESONANCE", S::VerticalSlider, 1, 0),
        control("Filter Env Amount", "ENV", S::VerticalSlider, 2, 0),
        control("Filter Key Track", "KYBD", S::VerticalSlider, 3, 0),
    };

    PanelSection envelope;
    envelope.title = "ENV / VCA";
    envelope.accentColour = "#79B4D0";
    envelope.column = 13; envelope.row = 0; envelope.columnSpan = 3; envelope.rowSpan = 5;
    envelope.controls = {
        control("Env Attack", "A", S::VerticalSlider, 0, 0),
        control("Env Decay", "D", S::VerticalSlider, 1, 0),
        control("Env Sustain", "S", S::VerticalSlider, 2, 0),
        control("Env Release", "R", S::VerticalSlider, 0, 1),
        control("VCA Mode", "VCA", S::Selector, 1, 1),
        control("Glide Time", "PORTAMENTO", S::Knob, 2, 1),
    };

    panel.sections = {source, modulation, filter, envelope};
    return panel;
}


// ---------------------------------------------------------------------------
// Juno-106-style
//
// Machine à CURSEURS : DCO, HPF, VCF, VCA et enveloppe s'alignent de gauche à
// droite, chacun sa colonne de faders. Panneau noir, sérigraphie et liserés
// bleutés, potentiomètres uniquement là où l'original en a un (le chorus est
// un sélecteur, pas un curseur).
// ---------------------------------------------------------------------------
MachinePanel makeJuno106() {
    MachinePanel panel;
    panel.pluginId = "vsm.juno106";
    panel.displayName = "Juno-106-style";
    panel.chassis = Chassis::Plastic;
    panel.panelColour = "#25262B";
    panel.sectionColour = "#1B1C20";
    panel.textColour = "#E9EBEF";
    panel.knobColour = "#3A3C42";
    panel.gridColumns = 18;
    panel.gridRows = 6;

    PanelSection lfo;
    lfo.title = "LFO";
    lfo.accentColour = "#5FA8D3";
    lfo.column = 0; lfo.row = 0; lfo.columnSpan = 3; lfo.rowSpan = 6;
    lfo.controls = {
        control("LFO Rate", "RATE", S::VerticalSlider, 0, 0),
        control("LFO Delay", "DELAY", S::VerticalSlider, 1, 0),
        control("LFO Pitch Amount", "TO DCO", S::VerticalSlider, 2, 0),
    };

    PanelSection dco;
    dco.title = "DCO";
    dco.accentColour = "#E2B04A";
    dco.column = 3; dco.row = 0; dco.columnSpan = 5; dco.rowSpan = 6;
    dco.controls = {
        control("DCO Saw Level", "SAW", S::VerticalSlider, 0, 0),
        control("DCO Pulse Level", "PULSE", S::VerticalSlider, 1, 0),
        control("DCO Sub Level", "SUB", S::VerticalSlider, 2, 0),
        control("Noise Level", "NOISE", S::VerticalSlider, 3, 0),
        control("Pulse Width", "PWM", S::VerticalSlider, 4, 0),
        control("PWM LFO Amount", "PWM LFO", S::Knob, 4, 1),
    };

    PanelSection filter;
    filter.title = "HPF / VCF";
    filter.accentColour = "#E2B04A";
    filter.column = 8; filter.row = 0; filter.columnSpan = 6; filter.rowSpan = 6;
    filter.controls = {
        control("HPF Cutoff", "HPF", S::VerticalSlider, 0, 0),
        control("VCF Cutoff", "FREQ", S::VerticalSlider, 1, 0),
        control("VCF Resonance", "RES", S::VerticalSlider, 2, 0),
        control("VCF Env Amount", "ENV", S::VerticalSlider, 3, 0),
        control("VCF LFO Amount", "LFO", S::VerticalSlider, 4, 0),
        control("VCF Key Track", "KYBD", S::VerticalSlider, 5, 0),
    };

    PanelSection envelope;
    envelope.title = "ENV / CHORUS";
    envelope.accentColour = "#5FA8D3";
    envelope.column = 14; envelope.row = 0; envelope.columnSpan = 4; envelope.rowSpan = 6;
    envelope.controls = {
        control("Env Attack", "A", S::VerticalSlider, 0, 0),
        control("Env Decay", "D", S::VerticalSlider, 1, 0),
        control("Env Sustain", "S", S::VerticalSlider, 2, 0),
        control("Env Release", "R", S::VerticalSlider, 3, 0),
        control("Chorus Mode", "CHORUS", S::Selector, 0, 1),
        control("Analog Character", "ANALOG DRIFT", S::Knob, 1, 1),
    };

    panel.sections = {lfo, dco, filter, envelope};
    return panel;
}

// ---------------------------------------------------------------------------
// Jupiter-8-style
//
// Le grand frère : deux VCO complets, cross-mod et sync, deux enveloppes.
// Panneau sombre, curseurs à capuchons colorés -- bleu pour la modulation,
// orange pour le son -- qui séparent les fonctions à l'œil nu.
// ---------------------------------------------------------------------------
MachinePanel makeJupiter8() {
    MachinePanel panel;
    panel.pluginId = "vsm.jupiter8";
    panel.displayName = "Jupiter-8-style";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#26272C";
    panel.sectionColour = "#1C1D21";
    panel.textColour = "#EAECF0";
    panel.knobColour = "#33353B";
    panel.gridColumns = 20;
    panel.gridRows = 7;

    PanelSection lfo;
    lfo.title = "LFO / MOD";
    lfo.accentColour = "#5FA8D3";
    lfo.column = 0; lfo.row = 0; lfo.columnSpan = 4; lfo.rowSpan = 7;
    lfo.controls = {
        control("LFO Rate", "RATE", S::VerticalSlider, 0, 0),
        control("LFO to Pitch", "TO VCO", S::VerticalSlider, 1, 0),
        control("LFO to Filter", "TO VCF", S::VerticalSlider, 2, 0),
        control("LFO to PWM", "TO PWM", S::VerticalSlider, 3, 0),
    };

    PanelSection oscillators;
    oscillators.title = "VCO 1 / VCO 2";
    oscillators.accentColour = "#E2803C";
    oscillators.column = 4; oscillators.row = 0; oscillators.columnSpan = 6; oscillators.rowSpan = 7;
    oscillators.controls = {
        control("VCO-1 Level", "VCO 1", S::VerticalSlider, 0, 0),
        control("VCO-1 Shape", "SHAPE 1", S::Selector, 1, 0),
        control("VCO-1 Pulse Width", "PW 1", S::Knob, 2, 0),
        control("VCO-2 Level", "VCO 2", S::VerticalSlider, 3, 0),
        control("VCO-2 Shape", "SHAPE 2", S::Selector, 4, 0),
        control("VCO-2 Pulse Width", "PW 2", S::Knob, 5, 0),
        control("VCO-2 Detune", "DETUNE", S::Knob, 3, 1),
        control("Cross Mod", "CROSS MOD", S::Knob, 4, 1),
        control("Sync", "SYNC", S::Toggle, 5, 1),
    };

    PanelSection filter;
    filter.title = "HPF / VCF";
    filter.accentColour = "#E2803C";
    filter.column = 10; filter.row = 0; filter.columnSpan = 5; filter.rowSpan = 7;
    filter.controls = {
        control("HPF Cutoff", "HPF", S::VerticalSlider, 0, 0),
        control("Filter Cutoff", "FREQ", S::VerticalSlider, 1, 0),
        control("Filter Resonance", "RES", S::VerticalSlider, 2, 0),
        control("Filter Env Amount", "ENV", S::VerticalSlider, 3, 0),
        control("Filter Key Track", "KYBD", S::VerticalSlider, 4, 0),
    };

    PanelSection envelopes;
    envelopes.title = "ENV 1 / ENV 2";
    envelopes.accentColour = "#5FA8D3";
    envelopes.column = 15; envelopes.row = 0; envelopes.columnSpan = 5; envelopes.rowSpan = 7;
    envelopes.controls = {
        control("Env 1 Attack", "A1", S::VerticalSlider, 0, 0),
        control("Env 1 Decay", "D1", S::VerticalSlider, 1, 0),
        control("Env 1 Sustain", "S1", S::VerticalSlider, 2, 0),
        control("Env 1 Release", "R1", S::VerticalSlider, 3, 0),
        control("Env 2 Attack", "A2", S::VerticalSlider, 0, 1),
        control("Env 2 Decay", "D2", S::VerticalSlider, 1, 1),
        control("Env 2 Sustain", "S2", S::VerticalSlider, 2, 1),
        control("Env 2 Release", "R2", S::VerticalSlider, 3, 1),
        control("Chorus Mode", "CHORUS", S::Selector, 4, 0),
        control("Analog Character", "DRIFT", S::Knob, 4, 1),
    };

    panel.sections = {lfo, oscillators, filter, envelopes};
    return panel;
}

// ---------------------------------------------------------------------------
// Prophet-style
//
// Flancs de bois, panneau noir, tout au potentiomètre : oscillateur A,
// oscillateur B, mélangeur, filtre, deux enveloppes, et surtout le bloc
// POLY-MOD, la particularité de la machine -- deux sources (enveloppe de
// filtre, oscillateur B) routées vers trois destinations.
// ---------------------------------------------------------------------------
MachinePanel makeProphet() {
    MachinePanel panel;
    panel.pluginId = "vsm.prophet";
    panel.displayName = "Prophet-style";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#232326";
    panel.sectionColour = "#1A1A1D";
    panel.textColour = "#E8E6E0";
    panel.knobColour = "#1A1A1C";
    panel.gridColumns = 20;
    panel.gridRows = 4;

    PanelSection polyMod;
    polyMod.title = "POLY-MOD";
    polyMod.accentColour = "#C86A4A";
    polyMod.column = 0; polyMod.row = 0; polyMod.columnSpan = 5; polyMod.rowSpan = 4;
    polyMod.controls = {
        control("PolyMod Filt Env", "SOURCE : FILT ENV", S::Knob, 0, 0),
        control("PolyMod Osc B", "SOURCE : OSC B", S::Knob, 1, 0),
        control("PolyMod to Freq A", "TO FREQ A", S::Knob, 0, 1),
        control("PolyMod to PW A", "TO PW A", S::Knob, 1, 1),
        control("PolyMod to Filter", "TO FILTER", S::Knob, 2, 1),
    };

    PanelSection oscillators;
    oscillators.title = "OSCILLATOR A / B";
    oscillators.accentColour = "#D8B25A";
    oscillators.column = 5; oscillators.row = 0; oscillators.columnSpan = 6; oscillators.rowSpan = 4;
    oscillators.controls = {
        control("Osc A Level", "A LEVEL", S::Knob, 0, 0),
        control("Osc A Shape", "A SHAPE", S::Selector, 1, 0),
        control("Osc A Pulse Width", "A PW", S::Knob, 2, 0),
        control("Sync", "SYNC", S::Toggle, 3, 0),
        control("Osc B Level", "B LEVEL", S::Knob, 0, 1),
        control("Osc B Shape", "B SHAPE", S::Selector, 1, 1),
        control("Osc B Pulse Width", "B PW", S::Knob, 2, 1),
        control("Osc B Detune", "B DETUNE", S::Knob, 3, 1),
    };

    PanelSection filter;
    filter.title = "FILTER";
    filter.accentColour = "#D8B25A";
    filter.column = 11; filter.row = 0; filter.columnSpan = 5; filter.rowSpan = 4;
    filter.controls = {
        control("Filter Cutoff", "CUTOFF", S::LargeKnob, 0, 0, 2, 2),
        control("Filter Resonance", "RESONANCE", S::Knob, 2, 0),
        control("Filter Env Amount", "ENV AMOUNT", S::Knob, 3, 0),
        control("Filter Key Track", "KEYBOARD", S::Knob, 2, 1),
        control("LFO Rate", "LFO RATE", S::Knob, 3, 1),
        control("LFO to Pitch", "LFO TO OSC", S::Knob, 0, 2),
        control("LFO to Filter", "LFO TO VCF", S::Knob, 1, 2),
        control("Analog Character", "DRIFT", S::Knob, 2, 2),
    };

    PanelSection envelopes;
    envelopes.title = "FILTER ENV / AMP ENV";
    envelopes.accentColour = "#8FA9C0";
    envelopes.column = 16; envelopes.row = 0; envelopes.columnSpan = 4; envelopes.rowSpan = 4;
    envelopes.controls = {
        control("Filter Attack", "F ATTACK", S::Knob, 0, 0),
        control("Filter Decay", "F DECAY", S::Knob, 1, 0),
        control("Filter Sustain", "F SUSTAIN", S::Knob, 2, 0),
        control("Filter Release", "F RELEASE", S::Knob, 3, 0),
        control("Amp Attack", "A ATTACK", S::Knob, 0, 1),
        control("Amp Decay", "A DECAY", S::Knob, 1, 1),
        control("Amp Sustain", "A SUSTAIN", S::Knob, 2, 1),
        control("Amp Release", "A RELEASE", S::Knob, 3, 1),
    };

    panel.sections = {polyMod, oscillators, filter, envelopes};
    return panel;
}

// ---------------------------------------------------------------------------
// MS-20-style
//
// Panneau sombre à sérigraphie blanche, et sa signature : DEUX filtres
// séparés (passe-haut et passe-bas), chacun avec sa propre résonance,
// présentés côte à côte comme sur la machine -- c'est ce qui permet le
// balayage en bande passante qui la caractérise.
// ---------------------------------------------------------------------------
MachinePanel makeMs20() {
    MachinePanel panel;
    panel.pluginId = "vsm.ms20";
    panel.displayName = "MS-20-style";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#2E2F33";
    panel.sectionColour = "#232428";
    panel.textColour = "#F0F0EC";
    panel.knobColour = "#1C1D20";
    panel.gridColumns = 16;
    panel.gridRows = 4;

    PanelSection oscillators;
    oscillators.title = "VCO 1 / VCO 2";
    oscillators.accentColour = "#D7DADF";
    oscillators.column = 0; oscillators.row = 0; oscillators.columnSpan = 5; oscillators.rowSpan = 4;
    oscillators.controls = {
        control("VCO-1 Level", "VCO 1 LEVEL", S::Knob, 0, 0),
        control("VCO-1 Shape", "VCO 1 WAVE", S::Selector, 1, 0),
        control("VCO-1 Pulse Width", "VCO 1 PW", S::Knob, 2, 0),
        control("VCO-2 Level", "VCO 2 LEVEL", S::Knob, 0, 1),
        control("VCO-2 Shape", "VCO 2 WAVE", S::Selector, 1, 1),
        control("VCO-2 Pitch", "VCO 2 PITCH", S::Knob, 2, 1),
        control("Noise Level", "NOISE", S::Knob, 0, 2),
        control("Glide Time", "PORTAMENTO", S::Knob, 1, 2),
        control("Analog Character", "DRIFT", S::Knob, 2, 2),
    };

    PanelSection filters;
    filters.title = "HIGH PASS / LOW PASS";
    filters.accentColour = "#E0C25A";
    filters.column = 5; filters.row = 0; filters.columnSpan = 6; filters.rowSpan = 4;
    filters.controls = {
        control("HPF Cutoff", "HPF CUTOFF", S::Knob, 0, 0),
        control("HPF Resonance", "HPF PEAK", S::Knob, 1, 0),
        control("LPF Cutoff", "LPF CUTOFF", S::LargeKnob, 2, 0, 2, 2),
        control("LPF Resonance", "LPF PEAK", S::Knob, 0, 1),
        control("Filter Drive", "DRIVE", S::Knob, 1, 1),
        control("EG to LPF", "EG TO LPF", S::Knob, 0, 2),
    };

    PanelSection modulation;
    modulation.title = "MG / EG";
    modulation.accentColour = "#7FB2C8";
    modulation.column = 11; modulation.row = 0; modulation.columnSpan = 5; modulation.rowSpan = 4;
    modulation.controls = {
        control("MG Rate", "MG FREQUENCY", S::Knob, 0, 0),
        control("MG Waveform", "MG WAVEFORM", S::Selector, 1, 0),
        control("MG to Pitch", "MG TO VCO", S::Knob, 2, 0),
        control("MG to LPF", "MG TO VCF", S::Knob, 3, 0),
        control("Amp Attack", "ATTACK", S::Knob, 0, 1),
        control("Amp Decay", "DECAY", S::Knob, 1, 1),
        control("Amp Sustain", "SUSTAIN", S::Knob, 2, 1),
        control("Amp Release", "RELEASE", S::Knob, 3, 1),
    };

    panel.sections = {oscillators, filters, modulation};
    return panel;
}

// ---------------------------------------------------------------------------
// ARP-Odyssey-style
//
// Panneau noir à curseurs plats et sérigraphie dorée. Duophonique : ses deux
// oscillateurs suivent les touches grave et aiguë, d'où le ring mod placé
// juste à côté d'eux -- il n'a de sens qu'avec les deux.
// ---------------------------------------------------------------------------
MachinePanel makeArpOdyssey() {
    MachinePanel panel;
    panel.pluginId = "vsm.arpodyssey";
    panel.displayName = "ARP-Odyssey-style";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#1F1F22";
    panel.sectionColour = "#17171A";
    panel.textColour = "#E7D9A8";
    panel.knobColour = "#2C2C30";
    panel.gridColumns = 18;
    panel.gridRows = 6;

    PanelSection oscillators;
    oscillators.title = "VCO 1 / VCO 2 / RING";
    oscillators.accentColour = "#D9B75A";
    oscillators.column = 0; oscillators.row = 0; oscillators.columnSpan = 7; oscillators.rowSpan = 6;
    oscillators.controls = {
        control("VCO-1 Level", "VCO 1", S::VerticalSlider, 0, 0),
        control("VCO-2 Level", "VCO 2", S::VerticalSlider, 1, 0),
        control("Ring Mod Level", "RING MOD", S::VerticalSlider, 2, 0),
        control("Noise Level", "NOISE", S::VerticalSlider, 3, 0),
        control("VCO-1 Shape", "WAVE 1", S::Selector, 4, 0),
        control("VCO-2 Shape", "WAVE 2", S::Selector, 5, 0),
        control("VCO-1 Pulse Width", "PW 1", S::Knob, 4, 1),
        control("VCO-2 Pulse Width", "PW 2", S::Knob, 5, 1),
        control("VCO-2 Detune", "DETUNE", S::Knob, 6, 0),
        control("Sync", "SYNC", S::Toggle, 6, 1),
    };

    PanelSection filter;
    filter.title = "FILTER";
    filter.accentColour = "#D9B75A";
    filter.column = 7; filter.row = 0; filter.columnSpan = 5; filter.rowSpan = 6;
    filter.controls = {
        control("HPF Cutoff", "HPF", S::VerticalSlider, 0, 0),
        control("Filter Cutoff", "CUTOFF", S::VerticalSlider, 1, 0),
        control("Filter Resonance", "RESONANCE", S::VerticalSlider, 2, 0),
        control("Filter Env Amount", "ENV", S::VerticalSlider, 3, 0),
        control("Filter Key Track", "KYBD", S::VerticalSlider, 4, 0),
    };

    PanelSection modulation;
    modulation.title = "LFO / ADSR";
    modulation.accentColour = "#8FB4C4";
    modulation.column = 12; modulation.row = 0; modulation.columnSpan = 6; modulation.rowSpan = 6;
    modulation.controls = {
        control("LFO Rate", "LFO RATE", S::VerticalSlider, 0, 0),
        control("LFO to Pitch", "TO VCO", S::VerticalSlider, 1, 0),
        control("LFO to Filter", "TO VCF", S::VerticalSlider, 2, 0),
        control("Amp Attack", "A", S::VerticalSlider, 3, 0),
        control("Amp Decay", "D", S::VerticalSlider, 4, 0),
        control("Amp Sustain", "S", S::VerticalSlider, 5, 0),
        control("Amp Release", "R", S::VerticalSlider, 0, 1),
        control("LFO Waveform", "LFO WAVE", S::Selector, 1, 1),
        control("Glide Time", "PORTAMENTO", S::Knob, 2, 1),
        control("Analog Character", "DRIFT", S::Knob, 3, 1),
    };

    panel.sections = {oscillators, filter, modulation};
    return panel;
}

// ---------------------------------------------------------------------------
// DX7-style
//
// Cas à part, et il faut le dire franchement : la machine d'origine n'a
// quasiment aucune commande visible -- un clavier à membrane, un afficheur de
// deux lignes, un curseur de données, et TOUT passe par des menus. Reproduire
// ça littéralement donnerait une façade fidèle et inutilisable, où régler un
// opérateur demanderait dix pressions.
//
// La façade retenue montre donc ce que la machine CACHE : la matrice des six
// opérateurs, un par colonne, avec ses paramètres alignés en rangées. C'est la
// convention de tous les éditeurs FM depuis quarante ans, et c'est la seule
// disposition qui rende la synthèse FM lisible -- on compare deux opérateurs
// d'un coup d'œil au lieu de les visiter l'un après l'autre.
// ---------------------------------------------------------------------------
MachinePanel makeDx7() {
    MachinePanel panel;
    panel.pluginId = "vsm.dx7";
    panel.displayName = "DX7-style";
    panel.chassis = Chassis::Plastic;
    panel.panelColour = "#3B3A38";
    panel.sectionColour = "#2C2B29";
    panel.textColour = "#E9E4D8";
    panel.knobColour = "#22211F";
    panel.gridColumns = 16;
    panel.gridRows = 8;

    PanelSection global;
    global.title = "ALGORITHM / GLOBAL";
    global.accentColour = "#7FA8B8";
    global.column = 0; global.row = 0; global.columnSpan = 4; global.rowSpan = 8;
    global.controls = {
        control("Algorithm", "ALGORITHM", S::LargeKnob, 0, 0, 2, 2),
        control("Feedback", "FEEDBACK", S::Knob, 2, 0),
        control("LFO Rate", "LFO RATE", S::Knob, 2, 1),
        control("LFO to Pitch", "LFO TO PITCH", S::Knob, 0, 2),
        control("Velocity Sens", "VELOCITY", S::Knob, 1, 2),
        control("Key Level Scaling", "KEY SCALING", S::Knob, 2, 2),
        control("Pitch Env Amount", "PITCH ENV", S::Knob, 0, 3),
        control("Pitch Env Time", "PITCH TIME", S::Knob, 1, 3),
        control("Analog Character", "DRIFT", S::Knob, 2, 3),
    };
    panel.sections.push_back(global);

    // Un bloc par opérateur : six colonnes identiques, ce qui permet de
    // comparer les opérateurs entre eux -- c'est tout l'intérêt de la matrice.
    const char* operatorAccents[6] = {"#D98C3C", "#D9A83C", "#C8C05A", "#8FBF7A", "#7FA8B8", "#9B8FC0"};
    for (int op = 1; op <= 6; ++op) {
        PanelSection section;
        section.title = "OP " + std::to_string(op);
        section.accentColour = operatorAccents[op - 1];
        section.column = 4 + (op - 1) * 2;
        section.row = 0;
        section.columnSpan = 2;
        section.rowSpan = 8;
        const std::string prefix = "Op" + std::to_string(op) + " ";
        section.controls = {
            control(prefix + "Ratio", "RATIO", S::Knob, 0, 0),
            control(prefix + "Level", "LEVEL", S::Knob, 1, 0),
            control(prefix + "Attack", "A", S::Knob, 0, 1),
            control(prefix + "Decay", "D", S::Knob, 1, 1),
            control(prefix + "Sustain", "S", S::Knob, 0, 2),
            control(prefix + "Release", "R", S::Knob, 1, 2),
            control(prefix + "Fixed", "FIXED", S::Toggle, 0, 3),
        };
        panel.sections.push_back(section);
    }
    return panel;
}

// ---------------------------------------------------------------------------
// Sampler — 16 emplacements
//
// Façade de boîte à rythmes plutôt que de sampler à écran : une colonne par
// emplacement, ses réglages dessous, et la grille de pas en bas. C'est la
// disposition qui correspond à l'usage visé (rejouer des coups découpés d'un
// enregistrement), et celle où l'on règle une pièce plutôt qu'une catégorie.
//
// POURQUOI SEIZE TIENNENT ALORS QUE HUIT SEMBLAIENT DÉJÀ BEAUCOUP. La version
// précédente s'arrêtait à huit parce que seize emplacements à sept paramètres
// font cent douze commandes -- illisibles alignées. Le nombre de commandes par
// emplacement est donc réduit à QUATRE : celles qu'on joue. Seize fois quatre
// font soixante-quatre, ce qu'une boîte à rythmes affiche couramment.
//
// Les trois autres (note de déclenchement, point de départ, groupe de coupure)
// sont des réglages de CONFIGURATION, posés une fois par l'analyse ou par le
// panneau générique, pas des gestes de jeu. Ils sont déclarés omis, avec leur
// raison, comme l'exige le cahier des charges.
// ---------------------------------------------------------------------------
MachinePanel makeSampler() {
    MachinePanel panel;
    panel.pluginId = "vsm.sampler";
    panel.displayName = "Sampler";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#33363C";
    panel.sectionColour = "#282B30";
    panel.textColour = "#EDEEF0";
    panel.knobColour = "#1E2024";
    panel.gridColumns = 16;
    panel.gridRows = 4;

    // Deux rangées de huit. Les couleurs suivent les FAMILLES de pièces, comme
    // les capuchons colorés d'une boîte à rythmes : on repère la grosse caisse
    // ou les charlestons sans lire les étiquettes.
    //
    // CHAQUE EMPLACEMENT PORTE LE NOM DE SA PIÈCE, et pas seulement son
    // numéro. C'est ce qui manquait pour que cette machine SOIT la boîte à
    // rythmes générique du cahier des charges (« vsm.drumkit », §5 de
    // docs/CDC-machines-manquantes.md) plutôt qu'une machine de plus à écrire
    // à côté : tout le reste y était déjà -- seize pièces, une colonne de
    // réglages par pièce, grille de seize pas, groupes de coupure.
    //
    // Le NUMÉRO RESTE EN TÊTE du titre, et ce n'est pas décoratif : les
    // paramètres s'appellent « Slot 3 Level », l'analyse écrit dans
    // l'emplacement 3, et un titre qui ne dirait que « HH CL » couperait le
    // lien entre ce qu'on voit et ce qu'on règle. Le nom dit ce que la
    // convention General MIDI met là PAR DÉFAUT ; changer la note de
    // déclenchement d'un emplacement le rend inexact, ce qui est la limite
    // assumée d'un kit dont les pièces sont réassignables.
    const char* pieces[16] = {
        "KICK", "SNARE", "HH CL", "HH OP", "CLAP", "LO TOM", "CRASH", "RIDE",
        "FLOOR", "HI FLR", "MID TOM", "HI TOM", "RIM", "SNARE 2", "BELL", "COWBELL",
    };
    const char* accents[16] = {
        "#D96C2C", "#D96C2C",                       // 1-2  : peaux graves
        "#D9B23A", "#D9B23A",                       // 3-4  : charlestons
        "#8FBF7A", "#8FBF7A",                       // 5-6  : claps et toms
        "#7FA8B8", "#7FA8B8",                       // 7-8  : cymbales
        "#C4788F", "#C4788F", "#C4788F", "#C4788F", // 9-12 : toms
        "#9B8FD9", "#9B8FD9", "#9B8FD9", "#9B8FD9", // 13-16: percussions
    };
    for (int slot = 0; slot < 16; ++slot) {
        PanelSection section;
        section.title = std::to_string(slot + 1) + " " + pieces[slot];
        section.accentColour = accents[slot];
        section.column = (slot % 8) * 2;
        section.row = (slot / 8) * 2;
        section.columnSpan = 2;
        section.rowSpan = 2;
        const std::string prefix = "Slot " + std::to_string(slot + 1) + " ";
        // Deux par deux plutôt qu'en ligne : à deux colonnes de large, quatre
        // commandes alignées seraient minuscules.
        section.controls = {
            control(prefix + "Level", "LVL", S::Knob, 0, 0),
            control(prefix + "Tune", "TUNE", S::Knob, 1, 0),
            control(prefix + "Decay", "DEC", S::Knob, 0, 1),
            control(prefix + "Pan", "PAN", S::Knob, 1, 1),
        };
        panel.sections.push_back(section);
    }

    panel.omittedParameters = {
        {"Master Level", "réglé au mixer, comme le volume de toute autre piste"},
    };
    for (int slot = 1; slot <= 16; ++slot) {
        const std::string prefix = "Slot " + std::to_string(slot) + " ";
        panel.omittedParameters.emplace_back(
            prefix + "Note", "mapping : posé une fois par l'analyse ou le panneau générique");
        panel.omittedParameters.emplace_back(
            prefix + "Start", "réglage de configuration, pas un geste de jeu");
        panel.omittedParameters.emplace_back(
            prefix + "Choke", "groupe de coupure : configuration du kit, réglée une fois");
    }

    // La grille de pas ne montre que les HUIT PREMIERS emplacements, et c'est
    // délibéré : seize lignes de seize pas rendraient la grille illisible, et
    // les huit premiers portent la convention General MIDI -- grosse caisse,
    // caisse claire, charlestons -- c'est-à-dire ce qu'on programme au pas.
    // Les emplacements 9 à 16 (toms, percussions) se jouent depuis le piano
    // roll. C'est la seule concession faite au « 8 à 16 pièces » du cahier des
    // charges, et elle est du côté de la LISIBILITÉ, pas de la capacité : les
    // seize pièces se déclenchent, huit seulement se programment au pas.
    panel.sequencer.kind = SequencerKind::DrumGrid;
    panel.sequencer.title = "PATTERN";
    panel.sequencer.stepCount = 16;
    panel.sequencer.rowSpan = 3;
    panel.sequencer.lanes = {
        {"KICK", 36}, {"SNARE", 38}, {"HH CL", 42}, {"HH OP", 46},
        {"CLAP", 39}, {"LO TOM", 45}, {"CRASH", 49}, {"RIDE", 51},
    };
    panel.gridRows += panel.sequencer.rowSpan;
    return panel;
}

// ---------------------------------------------------------------------------
// Piano électrique à lames
//
// La façade d'origine n'est pas celle d'un synthétiseur : sur l'instrument
// réel, TOUT le réglage tient en cinq commandes sur le préampli de la valise
// (grave, aigu, intensité et vitesse du vibrato, volume) -- le reste du son
// vient de la mécanique, réglée au tournevis par le technicien.
//
// C'est cette hiérarchie qu'on reproduit, et non une rangée uniforme : le bloc
// PREAMP tient la place et la disposition du panneau réel, et les réglages de
// mécanique (marteaux, lames, micros) sont regroupés à part sous le nom que
// leur donne le manuel d'entretien -- « VOICING ». Un pianiste retrouve ses
// cinq gestes ; qui veut changer l'instrument passe par le bloc d'à côté.
//
// Châssis bois, tolex noir, sérigraphie et logo rouge orangé.
// ---------------------------------------------------------------------------
MachinePanel makeEPiano() {
    MachinePanel panel;
    panel.pluginId = "vsm.epiano";
    panel.displayName = "Electric Piano (lames)";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#1B1A19";
    panel.sectionColour = "#131211";
    panel.textColour = "#E6E1D8";
    panel.knobColour = "#C9C3B6";
    panel.gridColumns = 16;
    panel.gridRows = 4;

    // Mécanique : ce que le technicien règle sous le couvercle.
    PanelSection voicing;
    voicing.title = "VOICING";
    voicing.accentColour = "#C4392B";
    voicing.column = 0; voicing.row = 0; voicing.columnSpan = 6; voicing.rowSpan = 4;
    voicing.controls = {
        control("Hammer Hardness", "HAMMER", S::Knob, 0, 0),
        control("Hammer Noise", "KNOCK", S::Knob, 1, 0),
        control("Bell Level", "BELL", S::Knob, 2, 0),
        control("Tine Decay", "TINE DECAY", S::Knob, 0, 1),
        control("Release", "DAMPER", S::Knob, 1, 1),
        control("Character", "AGE", S::Knob, 2, 1),
    };

    // Micros : distance et niveau d'attaque, les deux gestes qui font passer
    // du son doux au son mordant.
    PanelSection pickup;
    pickup.title = "PICKUPS";
    pickup.accentColour = "#C4392B";
    pickup.column = 6; pickup.row = 0; pickup.columnSpan = 3; pickup.rowSpan = 4;
    pickup.controls = {
        control("Pickup Drive", "DRIVE", S::Knob, 0, 0),
        control("Velocity Sensitivity", "TOUCH", S::Knob, 0, 1),
    };

    // Préampli de la valise : la façade réelle, avec ses proportions.
    PanelSection preamp;
    preamp.title = "PREAMP";
    preamp.accentColour = "#D8B45A";
    preamp.column = 9; preamp.row = 0; preamp.columnSpan = 7; preamp.rowSpan = 4;
    preamp.controls = {
        // rowSpan 2 : sur le préampli réel les curseurs occupent toute la
        // hauteur du panneau. Les laisser sur une demi-hauteur creusait un
        // vide sous eux et les faisait passer pour des commandes secondaires.
        control("Tone Bass", "BASS", S::VerticalSlider, 0, 0, 1, 2),
        control("Tone Treble", "TREBLE", S::VerticalSlider, 1, 0, 1, 2),
        control("Tremolo Depth", "INTENSITY", S::VerticalSlider, 2, 0, 1, 2),
        control("Tremolo Rate", "SPEED", S::VerticalSlider, 3, 0, 1, 2),
        control("Output Level", "VOLUME", S::VerticalSlider, 4, 0, 1, 2),
        control("Tremolo Stereo", "STEREO", S::Knob, 5, 0),
        control("Analog Character", "AGE DRIFT", S::Knob, 5, 1),
    };

    panel.sections = {voicing, pickup, preamp};
    return panel;
}

// ---------------------------------------------------------------------------
// Polyphonique américain à filtre 2 pôles
//
// Panneau noir, capuchons de curseurs crème, sérigraphie orangée : la machine
// se reconnaît à ce contraste avant même qu'on lise un nom de bloc.
//
// La disposition suit celle de l'original, qui est aussi celle du signal :
// CONTROL (la modulation, tout à gauche, parce qu'elle arrose tout le reste),
// puis les deux oscillateurs, le filtre, ses deux enveloppes, et enfin la
// section de jeu. Le pupitre réel met la pente du filtre et l'unisson sur des
// interrupteurs, pas des potentiomètres : ce sont des choix, pas des dosages.
// ---------------------------------------------------------------------------
MachinePanel makeObx() {
    MachinePanel panel;
    panel.pluginId = "vsm.obx";
    panel.displayName = "OB-X-style";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#161617";
    panel.sectionColour = "#0F0F10";
    panel.textColour = "#EDE7DA";
    panel.knobColour = "#DFD8C6";
    panel.gridColumns = 20;
    panel.gridRows = 6;

    // Note de disposition : les curseurs portent rowSpan 2 pour occuper toute
    // la hauteur de leur bloc, et les commandes à positions discrètes
    // (formes d'onde, interrupteurs) sont posées dans une colonne à part.
    // C'est ce que fait le pupitre d'origine, et ça donne à l'œil le même
    // repère : ce qui est haut se dose, ce qui est petit se choisit.
    PanelSection control_;
    control_.title = "CONTROL";
    control_.accentColour = "#D9722E";
    control_.column = 0; control_.row = 0; control_.columnSpan = 4; control_.rowSpan = 6;
    control_.controls = {
        control("LFO Rate", "RATE", S::VerticalSlider, 0, 0, 1, 2),
        control("LFO to Pitch", "FREQ", S::VerticalSlider, 1, 0, 1, 2),
        control("LFO to PWM", "PWM", S::VerticalSlider, 2, 0, 1, 2),
        control("LFO Waveform", "WAVE", S::Selector, 3, 0),
        control("LFO to Filter", "FILTER", S::Knob, 3, 1),
    };

    PanelSection osc1;
    osc1.title = "OSCILLATOR 1";
    osc1.accentColour = "#E8C24A";
    osc1.column = 4; osc1.row = 0; osc1.columnSpan = 3; osc1.rowSpan = 6;
    osc1.controls = {
        control("Osc1 Level", "LEVEL", S::VerticalSlider, 0, 0, 1, 2),
        control("Osc1 Pulse Width", "PW", S::VerticalSlider, 1, 0, 1, 2),
        control("Osc1 Shape", "WAVE", S::Selector, 2, 0),
    };

    PanelSection osc2;
    osc2.title = "OSCILLATOR 2";
    osc2.accentColour = "#E8C24A";
    osc2.column = 7; osc2.row = 0; osc2.columnSpan = 3; osc2.rowSpan = 6;
    osc2.controls = {
        control("Osc2 Level", "LEVEL", S::VerticalSlider, 0, 0, 1, 2),
        control("Osc2 Pulse Width", "PW", S::VerticalSlider, 1, 0, 1, 2),
        control("Osc2 Shape", "WAVE", S::Selector, 2, 0),
        control("Osc2 Detune", "DETUNE", S::Knob, 2, 1),
        control("Sync", "SYNC", S::Toggle, 3, 0),
    };

    PanelSection filter;
    filter.title = "FILTER";
    filter.accentColour = "#D9722E";
    filter.column = 10; filter.row = 0; filter.columnSpan = 4; filter.rowSpan = 6;
    filter.controls = {
        control("Filter Cutoff", "FREQUENCY", S::VerticalSlider, 0, 0, 1, 2),
        control("Filter Resonance", "RESONANCE", S::VerticalSlider, 1, 0, 1, 2),
        control("Filter Env Amount", "ENV AMT", S::VerticalSlider, 2, 0, 1, 2),
        control("Filter Key Track", "TRACK", S::VerticalSlider, 3, 0, 1, 2),
        control("Filter Slope", "4-POLE", S::Toggle, 4, 0),
        control("Velocity to Filter", "VELOCITY", S::Knob, 4, 1),
    };

    // Les deux enveloppes gardent leurs quatre curseurs alignés sur une seule
    // rangée, comme sur l'original : A-D-S-R se lit de gauche à droite, et
    // c'est ce que la main cherche.
    PanelSection filterEnv;
    filterEnv.title = "FILTER ENVELOPE";
    filterEnv.accentColour = "#8FA9C9";
    filterEnv.column = 14; filterEnv.row = 0; filterEnv.columnSpan = 2; filterEnv.rowSpan = 6;
    filterEnv.controls = {
        control("Filter Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Filter Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Filter Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Filter Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection ampEnv;
    ampEnv.title = "AMP ENVELOPE";
    ampEnv.accentColour = "#8FA9C9";
    ampEnv.column = 16; ampEnv.row = 0; ampEnv.columnSpan = 2; ampEnv.rowSpan = 6;
    ampEnv.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection performance;
    performance.title = "MANUAL";
    performance.accentColour = "#D9722E";
    performance.column = 18; performance.row = 0; performance.columnSpan = 2; performance.rowSpan = 6;
    performance.controls = {
        control("Unison", "UNISON", S::Toggle, 0, 0),
        control("Unison Detune", "DETUNE", S::Knob, 1, 0),
        control("Analog Character", "ANALOG", S::Knob, 0, 1, 2, 1),
    };

    panel.sections = {control_, osc1, osc2, filter, filterEnv, ampEnv, performance};
    return panel;
}

// ---------------------------------------------------------------------------
// Lead supersaw
//
// La machine d'origine est un synthé de scène des années 1990 : plastique
// bleu-gris, sérigraphie blanche, écran et pavé de sélection. Rien à voir
// avec les façades de bois et de tôle du reste du parc, et c'est voulu -- on
// doit voir au premier regard qu'on a changé d'époque.
//
// La disposition met le bloc SUPER SAW EN PREMIER et lui donne la place la
// plus grande, alors que le filtre passe après. C'est l'inverse de toutes les
// autres façades du projet, et c'est justement ce qui est fidèle : sur cette
// machine le timbre se règle au désaccord, pas à la coupure.
// ---------------------------------------------------------------------------
MachinePanel makeSupersaw() {
    MachinePanel panel;
    panel.pluginId = "vsm.supersaw";
    panel.displayName = "Supersaw Lead";
    panel.chassis = Chassis::Plastic;
    panel.panelColour = "#2E3540";
    panel.sectionColour = "#242A33";
    panel.textColour = "#F0F3F7";
    panel.knobColour = "#C8CDD4";
    panel.gridColumns = 18;
    panel.gridRows = 6;

    // Le bloc qui fait le son : deux grands potentiomètres, comme sur
    // l'original où ils sont les seuls de cette taille.
    PanelSection superSaw;
    superSaw.title = "SUPER SAW";
    superSaw.accentColour = "#4FC3E8";
    superSaw.column = 0; superSaw.row = 0; superSaw.columnSpan = 5; superSaw.rowSpan = 6;
    superSaw.controls = {
        control("Detune", "DETUNE", S::LargeKnob, 0, 0),
        control("Mix", "MIX", S::LargeKnob, 1, 0),
        control("Stereo Spread", "SPREAD", S::Knob, 0, 1),
        control("Pitch HPF", "HPF", S::Knob, 1, 1),
    };

    PanelSection oscillator;
    oscillator.title = "OSC";
    oscillator.accentColour = "#4FC3E8";
    oscillator.column = 5; oscillator.row = 0; oscillator.columnSpan = 2; oscillator.rowSpan = 6;
    oscillator.controls = {
        control("Sub Level", "SUB", S::Knob, 0, 0),
        control("Noise Level", "NOISE", S::Knob, 0, 1),
    };

    PanelSection filter;
    filter.title = "FILTER";
    filter.accentColour = "#E8A33D";
    filter.column = 7; filter.row = 0; filter.columnSpan = 4; filter.rowSpan = 6;
    filter.controls = {
        control("Filter Cutoff", "CUTOFF", S::VerticalSlider, 0, 0, 1, 2),
        control("Filter Resonance", "RESO", S::VerticalSlider, 1, 0, 1, 2),
        control("Filter Env Amount", "ENV", S::VerticalSlider, 2, 0, 1, 2),
        control("Filter Key Track", "KEY", S::Knob, 3, 0),
        control("Velocity to Filter", "VELO", S::Knob, 3, 1),
    };

    PanelSection filterEnv;
    filterEnv.title = "FILTER ENV";
    filterEnv.accentColour = "#8FA9C9";
    filterEnv.column = 11; filterEnv.row = 0; filterEnv.columnSpan = 2; filterEnv.rowSpan = 6;
    filterEnv.controls = {
        control("Filter Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Filter Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Filter Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Filter Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection ampEnv;
    ampEnv.title = "AMP ENV";
    ampEnv.accentColour = "#8FA9C9";
    ampEnv.column = 13; ampEnv.row = 0; ampEnv.columnSpan = 2; ampEnv.rowSpan = 6;
    ampEnv.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection performance;
    performance.title = "LFO / PLAY";
    performance.accentColour = "#4FC3E8";
    performance.column = 15; performance.row = 0; performance.columnSpan = 3; performance.rowSpan = 6;
    performance.controls = {
        control("LFO Rate", "RATE", S::Knob, 0, 0),
        control("LFO to Pitch", "PITCH", S::Knob, 1, 0),
        control("LFO to Filter", "FILTER", S::Knob, 2, 0),
        control("Glide", "PORTAMENTO", S::Knob, 0, 1),
        control("Analog Character", "DRIFT", S::Knob, 1, 1),
    };

    panel.sections = {superSaw, oscillator, filter, filterEnv, ampEnv, performance};
    return panel;
}

// ---------------------------------------------------------------------------
// Synthé à table d'ondes
//
// Les machines de cette famille sont des instruments de laboratoire devenus
// instruments de scène : façade sombre, sérigraphie fine, et un bloc WAVE mis
// en avant là où les autres mettent le filtre. La disposition le dit
// franchement -- la table est la source du son, le filtre ne vient qu'après,
// et ce n'est pas lui qu'on règle en premier.
//
// Le bloc WAVE reçoit SA PROPRE enveloppe, à côté de lui et non rangée avec
// les deux autres : c'est la commande de mouvement de l'instrument, et la
// façade doit le montrer.
// ---------------------------------------------------------------------------
MachinePanel makeWavetable() {
    MachinePanel panel;
    panel.pluginId = "vsm.wavetable";
    panel.displayName = "Wavetable Synth";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#1E1F24";
    panel.sectionColour = "#16171B";
    panel.textColour = "#DFE3EA";
    panel.knobColour = "#2A2C33";
    panel.gridColumns = 20;
    panel.gridRows = 6;

    PanelSection wave;
    wave.title = "WAVE";
    wave.accentColour = "#7BD389";
    wave.column = 0; wave.row = 0; wave.columnSpan = 5; wave.rowSpan = 6;
    wave.controls = {
        control("Wavetable", "TABLE", S::Selector, 0, 0),
        control("Position", "POSITION", S::LargeKnob, 1, 0),
        control("Wave Env Amount", "ENV AMT", S::Knob, 0, 1),
        control("LFO to Position", "LFO AMT", S::Knob, 1, 1),
    };

    // L'enveloppe de la table, à côté de la table : c'est elle qui fait le
    // mouvement, elle ne se range pas avec les enveloppes de service.
    PanelSection waveEnv;
    waveEnv.title = "WAVE ENVELOPE";
    waveEnv.accentColour = "#7BD389";
    waveEnv.column = 5; waveEnv.row = 0; waveEnv.columnSpan = 3; waveEnv.rowSpan = 6;
    waveEnv.controls = {
        control("Wave Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Wave Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Wave Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Wave Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection oscB;
    oscB.title = "OSC B";
    oscB.accentColour = "#7BD389";
    oscB.column = 8; oscB.row = 0; oscB.columnSpan = 2; oscB.rowSpan = 6;
    oscB.controls = {
        control("Osc B Level", "LEVEL", S::Knob, 0, 0),
        control("Osc B Detune", "DETUNE", S::Knob, 1, 0),
        control("Osc B Position", "POS OFF", S::Knob, 0, 1),
        control("Noise Level", "NOISE", S::Knob, 1, 1),
    };

    PanelSection filter;
    filter.title = "FILTER";
    filter.accentColour = "#E0A458";
    filter.column = 10; filter.row = 0; filter.columnSpan = 3; filter.rowSpan = 6;
    filter.controls = {
        control("Filter Cutoff", "CUTOFF", S::VerticalSlider, 0, 0, 1, 2),
        control("Filter Resonance", "RESO", S::VerticalSlider, 1, 0, 1, 2),
        control("Filter Env Amount", "ENV", S::VerticalSlider, 2, 0, 1, 2),
        control("Filter Key Track", "KEY", S::Knob, 3, 0),
        control("Velocity to Filter", "VELO", S::Knob, 3, 1),
    };

    PanelSection filterEnv;
    filterEnv.title = "FILTER ENV";
    filterEnv.accentColour = "#8FA9C9";
    filterEnv.column = 13; filterEnv.row = 0; filterEnv.columnSpan = 2; filterEnv.rowSpan = 6;
    filterEnv.controls = {
        control("Filter Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Filter Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Filter Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Filter Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection ampEnv;
    ampEnv.title = "AMP ENV";
    ampEnv.accentColour = "#8FA9C9";
    ampEnv.column = 15; ampEnv.row = 0; ampEnv.columnSpan = 2; ampEnv.rowSpan = 6;
    ampEnv.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection modulation;
    modulation.title = "LFO";
    modulation.accentColour = "#7BD389";
    modulation.column = 17; modulation.row = 0; modulation.columnSpan = 3; modulation.rowSpan = 6;
    modulation.controls = {
        control("LFO Rate", "RATE", S::Knob, 0, 0),
        control("LFO to Filter", "FILTER", S::Knob, 1, 0),
        control("LFO to Pitch", "PITCH", S::Knob, 0, 1),
        control("Analog Character", "DRIFT", S::Knob, 1, 1),
    };

    panel.sections = {wave, waveEnv, oscB, filter, filterEnv, ampEnv, modulation};
    return panel;
}

// ---------------------------------------------------------------------------
// Hybride PCM + synthé
//
// Les machines de cette génération abandonnent les potentiomètres pour un
// écran et quelques boutons : gris clair, sérigraphie discrète, esthétique de
// matériel professionnel plutôt que d'instrument. On garde des commandes
// visibles -- une façade à un seul écran serait injouable -- mais on reprend
// la palette et la sobriété.
//
// La disposition suit les DEUX COUCHES, séparées à l'œil : le bloc PARTIAL A
// (l'attaque échantillonnée) puis PARTIAL B (le corps synthétique), avec la
// STRUCTURE qui les relie posée entre les deux. C'est ainsi que le manuel de
// ces machines présente le son, et c'est ce qui rend la façade lisible.
// ---------------------------------------------------------------------------
MachinePanel makePcmHybrid() {
    MachinePanel panel;
    panel.pluginId = "vsm.pcmhybrid";
    panel.displayName = "PCM + Synth Hybrid";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#3C3E42";
    panel.sectionColour = "#313337";
    panel.textColour = "#E8E9EB";
    panel.knobColour = "#202225";
    panel.gridColumns = 20;
    panel.gridRows = 6;

    PanelSection attack;
    attack.title = "PARTIAL A - PCM ATTACK";
    attack.accentColour = "#C8553D";
    attack.column = 0; attack.row = 0; attack.columnSpan = 6; attack.rowSpan = 6;
    attack.controls = {
        control("Attack Sample", "SAMPLE", S::Selector, 0, 0),
        control("Attack Level", "LEVEL", S::Knob, 1, 0),
        control("Attack Decay", "DECAY", S::Knob, 2, 0),
        control("Attack Tune", "TUNE", S::Knob, 0, 1),
        control("Attack Tone", "TONE", S::Knob, 1, 1),
        control("Velocity to Attack", "VELOCITY", S::Knob, 2, 1),
    };

    // La structure, entre les deux couches : c'est elle qui dit comment elles
    // se combinent, et elle change la machine du tout au tout.
    PanelSection structure;
    structure.title = "STRUCTURE";
    structure.accentColour = "#E8B84B";
    structure.column = 6; structure.row = 0; structure.columnSpan = 2; structure.rowSpan = 6;
    structure.controls = {
        control("Structure", "RING MOD", S::Toggle, 0, 0),
    };

    PanelSection tone;
    tone.title = "PARTIAL B - SYNTH";
    tone.accentColour = "#4E8098";
    tone.column = 8; tone.row = 0; tone.columnSpan = 3; tone.rowSpan = 6;
    tone.controls = {
        control("Tone Shape", "WAVE", S::Selector, 0, 0),
        control("Tone Level", "LEVEL", S::Knob, 1, 0),
        control("Tone Detune", "DETUNE", S::Knob, 0, 1),
    };

    PanelSection filter;
    filter.title = "TVF";
    filter.accentColour = "#E8B84B";
    filter.column = 11; filter.row = 0; filter.columnSpan = 3; filter.rowSpan = 6;
    filter.controls = {
        control("Filter Cutoff", "CUTOFF", S::VerticalSlider, 0, 0, 1, 2),
        control("Filter Resonance", "RESO", S::VerticalSlider, 1, 0, 1, 2),
        control("Filter Env Amount", "ENV", S::VerticalSlider, 2, 0, 1, 2),
        control("Filter Key Track", "KEY", S::Knob, 3, 0),
        control("Velocity to Filter", "VELO", S::Knob, 3, 1),
    };

    PanelSection filterEnv;
    filterEnv.title = "TVF ENV";
    filterEnv.accentColour = "#8FA9C9";
    filterEnv.column = 14; filterEnv.row = 0; filterEnv.columnSpan = 2; filterEnv.rowSpan = 6;
    filterEnv.controls = {
        control("Filter Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Filter Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Filter Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Filter Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection ampEnv;
    ampEnv.title = "TVA ENV";
    ampEnv.accentColour = "#8FA9C9";
    ampEnv.column = 16; ampEnv.row = 0; ampEnv.columnSpan = 2; ampEnv.rowSpan = 6;
    ampEnv.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection modulation;
    modulation.title = "LFO";
    modulation.accentColour = "#4E8098";
    modulation.column = 18; modulation.row = 0; modulation.columnSpan = 2; modulation.rowSpan = 6;
    modulation.controls = {
        control("LFO Rate", "RATE", S::Knob, 0, 0),
        control("LFO to Pitch", "PITCH", S::Knob, 1, 0),
        control("LFO to Filter", "FILTER", S::Knob, 0, 1),
        control("Analog Character", "DRIFT", S::Knob, 1, 1),
    };

    panel.sections = {attack, structure, tone, filter, filterEnv, ampEnv, modulation};
    return panel;
}

// ---------------------------------------------------------------------------
// Orgue à roues phoniques
//
// La façade de cet instrument n'a AUCUN potentiomètre : elle a des TIRETTES,
// qu'on tire vers soi, et leur position se lit d'un coup d'œil comme un
// graphique du spectre. C'est le geste central de l'instrument, et un
// panneau de boutons ronds serait faux même si tous les paramètres y étaient.
//
// Les couleurs des tirettes sont codées sur la machine d'origine, et elles
// ne sont pas décoratives : BLANC pour les rangs de la série harmonique
// (16', 8', 4', 2', 1'), NOIR pour les rangs « en quinte » (5⅓', 2⅔', 1⅗',
// 1⅓') qui sonnent faux sur un accord, et BRUN pour les deux graves. Un
// organiste lit ses réglages avec ce code ; on le reproduit avec les liserés
// des blocs, faute de pouvoir colorer les commandes une à une.
//
// Bois foncé, panneau brun-noir : l'instrument est un meuble avant d'être une
// machine.
// ---------------------------------------------------------------------------
MachinePanel makeTonewheel() {
    MachinePanel panel;
    panel.pluginId = "vsm.tonewheel";
    panel.displayName = "Tonewheel Organ";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#2A211B";
    panel.sectionColour = "#1E1712";
    panel.textColour = "#EDE4D3";
    panel.knobColour = "#D8CFBC";
    panel.gridColumns = 18;
    panel.gridRows = 6;

    // Les deux tirettes graves : brunes sur la machine d'origine.
    PanelSection sub;
    sub.title = "SUB";
    sub.accentColour = "#8A5A34";
    sub.column = 0; sub.row = 0; sub.columnSpan = 3; sub.rowSpan = 6;
    sub.controls = {
        control("Drawbar 16", "16'", S::VerticalSlider, 0, 0, 1, 2),
        control("Drawbar 5 1/3", "5 1/3'", S::VerticalSlider, 1, 0, 1, 2),
    };

    // Les sept tirettes de la série : blanches et noires mêlées, mais toutes
    // du même bloc sur la machine -- on ne les sépare donc pas.
    PanelSection drawbars;
    drawbars.title = "DRAWBARS";
    drawbars.accentColour = "#D8CFBC";
    drawbars.column = 3; drawbars.row = 0; drawbars.columnSpan = 7; drawbars.rowSpan = 6;
    drawbars.controls = {
        control("Drawbar 8", "8'", S::VerticalSlider, 0, 0, 1, 2),
        control("Drawbar 4", "4'", S::VerticalSlider, 1, 0, 1, 2),
        control("Drawbar 2 2/3", "2 2/3'", S::VerticalSlider, 2, 0, 1, 2),
        control("Drawbar 2", "2'", S::VerticalSlider, 3, 0, 1, 2),
        control("Drawbar 1 3/5", "1 3/5'", S::VerticalSlider, 4, 0, 1, 2),
        control("Drawbar 1 1/3", "1 1/3'", S::VerticalSlider, 5, 0, 1, 2),
        control("Drawbar 1", "1'", S::VerticalSlider, 6, 0, 1, 2),
    };

    PanelSection percussion;
    percussion.title = "PERCUSSION";
    percussion.accentColour = "#C4462F";
    percussion.column = 10; percussion.row = 0; percussion.columnSpan = 3; percussion.rowSpan = 6;
    percussion.controls = {
        control("Percussion Level", "ON / SOFT", S::Knob, 0, 0),
        control("Percussion Harmonic", "2ND / 3RD", S::Toggle, 1, 0),
        control("Percussion Decay", "DECAY", S::Knob, 0, 1),
        control("Key Click", "KEY CLICK", S::Knob, 1, 1),
    };

    PanelSection vibrato;
    vibrato.title = "VIBRATO";
    vibrato.accentColour = "#5E7F9B";
    vibrato.column = 13; vibrato.row = 0; vibrato.columnSpan = 2; vibrato.rowSpan = 6;
    vibrato.controls = {
        control("Vibrato Depth", "DEPTH", S::Knob, 0, 0),
        control("Vibrato Rate", "RATE", S::Knob, 0, 1),
    };

    // Le rotatif n'est pas un effet ajouté : sans lui cet instrument ne
    // ressemble à rien de ce qu'on connaît. Il a donc son bloc sur la façade,
    // et pas une place dans une chaîne d'effets.
    PanelSection rotary;
    rotary.title = "ROTARY";
    rotary.accentColour = "#C4462F";
    rotary.column = 15; rotary.row = 0; rotary.columnSpan = 3; rotary.rowSpan = 6;
    rotary.controls = {
        control("Rotary Fast", "SLOW / FAST", S::Toggle, 0, 0),
        control("Rotary Depth", "DEPTH", S::Knob, 1, 0),
        control("Rotary Balance", "HORN / DRUM", S::Knob, 0, 1),
        control("Overdrive", "DRIVE", S::Knob, 1, 1),
        control("Output Level", "VOLUME", S::Knob, 2, 0),
    };

    panel.sections = {sub, drawbars, percussion, vibrato, rotary};
    return panel;
}

// ---------------------------------------------------------------------------
// Synthé neutre
//
// Cette façade ne ressemble à aucune machine, et c'est la seule du parc dans
// ce cas. Les autres reproduisent un instrument ; celle-ci reproduit un
// SCHÉMA -- gris neutre, sérigraphie sobre, aucune couleur de caractère. On
// doit voir au premier regard qu'on est devant un outil, pas devant un
// instrument qui a une histoire.
//
// La disposition suit le signal de gauche à droite, comme un synoptique :
// sources, filtre, enveloppes, modulation, sortie. C'est la lecture la plus
// neutre possible, celle d'un manuel.
// ---------------------------------------------------------------------------
MachinePanel makeGeneric() {
    MachinePanel panel;
    panel.pluginId = "vsm.generic";
    panel.displayName = "Generic Synth";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#2C2E31";
    panel.sectionColour = "#232528";
    panel.textColour = "#DDE0E4";
    panel.knobColour = "#8C9096";
    panel.gridColumns = 24;
    panel.gridRows = 6;

    PanelSection sources;
    sources.title = "SOURCES";
    sources.accentColour = "#7E8A96";
    sources.column = 0; sources.row = 0; sources.columnSpan = 7; sources.rowSpan = 6;
    sources.controls = {
        // La forme est un potentiomètre CONTINU et non un sélecteur : c'est
        // l'exigence centrale de cette machine, et la façade doit le dire.
        control("Osc1 Shape", "SHAPE 1", S::LargeKnob, 0, 0),
        control("Osc1 Level", "LVL 1", S::Knob, 1, 0),
        control("Osc1 Pulse Width", "PW 1", S::Knob, 2, 0),
        control("Osc2 Shape", "SHAPE 2", S::LargeKnob, 0, 1),
        control("Osc2 Level", "LVL 2", S::Knob, 1, 1),
        control("Osc2 Pulse Width", "PW 2", S::Knob, 2, 1),
        control("Osc2 Detune", "DETUNE", S::Knob, 3, 0),
        control("Osc2 Octave", "OCTAVE", S::Knob, 3, 1),
        control("Sub Level", "SUB", S::Knob, 4, 0),
        control("Sub Shape", "SUB SH", S::Knob, 4, 1),
        control("Noise Level", "NOISE", S::Knob, 5, 0),
        control("Noise Colour", "COLOUR", S::Knob, 5, 1),
    };

    PanelSection filter;
    filter.title = "FILTER";
    filter.accentColour = "#7E8A96";
    filter.column = 7; filter.row = 0; filter.columnSpan = 5; filter.rowSpan = 6;
    filter.controls = {
        // Le type aussi est continu : LP -> BP -> HP sans palier.
        control("Filter Type", "TYPE", S::LargeKnob, 0, 0),
        control("Filter Cutoff", "CUTOFF", S::VerticalSlider, 1, 0, 1, 2),
        control("Filter Resonance", "RESO", S::VerticalSlider, 2, 0, 1, 2),
        control("Filter Env Amount", "ENV", S::VerticalSlider, 3, 0, 1, 2),
        control("Filter Key Track", "KEY", S::Knob, 0, 1),
        control("Filter Slope", "4-POLE", S::Toggle, 4, 0),
        control("Velocity to Filter", "VELO", S::Knob, 4, 1),
    };

    PanelSection ampEnv;
    ampEnv.title = "AMP ENV";
    ampEnv.accentColour = "#8FA9C9";
    ampEnv.column = 12; ampEnv.row = 0; ampEnv.columnSpan = 2; ampEnv.rowSpan = 6;
    ampEnv.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection filterEnv;
    filterEnv.title = "FILTER ENV";
    filterEnv.accentColour = "#8FA9C9";
    filterEnv.column = 14; filterEnv.row = 0; filterEnv.columnSpan = 2; filterEnv.rowSpan = 6;
    filterEnv.controls = {
        control("Filter Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Filter Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Filter Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Filter Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection lfo1;
    lfo1.title = "LFO 1";
    lfo1.accentColour = "#9FA6AD";
    lfo1.column = 16; lfo1.row = 0; lfo1.columnSpan = 3; lfo1.rowSpan = 6;
    lfo1.controls = {
        control("LFO1 Rate", "RATE", S::Knob, 0, 0),
        control("LFO1 Shape", "SHAPE", S::Knob, 1, 0),
        control("LFO1 to Pitch", "PITCH", S::Knob, 2, 0),
        control("LFO1 to Filter", "FILTER", S::Knob, 0, 1),
        control("LFO1 to Amp", "AMP", S::Knob, 1, 1),
        control("LFO1 to PWM", "PWM", S::Knob, 2, 1),
    };

    PanelSection lfo2;
    lfo2.title = "LFO 2";
    lfo2.accentColour = "#9FA6AD";
    lfo2.column = 19; lfo2.row = 0; lfo2.columnSpan = 2; lfo2.rowSpan = 6;
    lfo2.controls = {
        control("LFO2 Rate", "RATE", S::Knob, 0, 0),
        control("LFO2 Shape", "SHAPE", S::Knob, 1, 0),
        control("LFO2 to Pitch", "PITCH", S::Knob, 0, 1),
        control("LFO2 to Filter", "FILTER", S::Knob, 1, 1),
    };

    PanelSection output;
    output.title = "OUTPUT";
    output.accentColour = "#7E8A96";
    output.column = 21; output.row = 0; output.columnSpan = 3; output.rowSpan = 6;
    output.controls = {
        control("Drive", "DRIVE", S::Knob, 0, 0),
        control("Output Level", "LEVEL", S::Knob, 1, 0),
        control("Velocity to Amp", "VELO AMP", S::Knob, 0, 1),
    };

    panel.sections = {sources, filter, ampEnv, filterEnv, lfo1, lfo2, output};
    return panel;
}

// ---------------------------------------------------------------------------
// Corde pincée / frottée
//
// Il n'y a pas de machine d'origine à copier : la modélisation physique n'a
// jamais eu de façade canonique, et prétendre en imiter une serait inventer
// un souvenir. La règle du § 5 du cahier des charges prévoit ce cas et donne
// la solution : à défaut d'un original, la disposition suit LE TRAJET DU
// SIGNAL — ici le trajet PHYSIQUE, qui est aussi celui que le musicien
// parcourt en pensée : on excite quelque part, la corde répond, la caisse
// rayonne, l'ampli sort.
//
//     EXCITATION  ->  ARCHET  ->  CORDE  ->  CAISSE  ->  SORTIE
//
// Le bois du châssis et le laiton des commandes disent de quelle famille
// d'instruments il s'agit, sans emprunter à personne.
// ---------------------------------------------------------------------------
MachinePanel makeString() {
    MachinePanel panel;
    panel.pluginId = "vsm.string";
    panel.displayName = "String (corde pincée / frottée)";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#241C15";
    panel.sectionColour = "#1A140F";
    panel.textColour = "#EFE3D0";
    panel.knobColour = "#C9A567";
    panel.gridColumns = 18;
    panel.gridRows = 4;

    // Le geste : où l'on touche la corde, et avec quoi. « BLEND » est
    // volontairement un potentiomètre et non un interrupteur : le passage du
    // médiator à l'archet est CONTINU dans cette machine, et une façade qui
    // montrerait deux positions mentirait sur ce qu'elle commande.
    PanelSection excitation;
    excitation.title = "EXCITATION";
    excitation.accentColour = "#C9A567";
    excitation.column = 0; excitation.row = 0; excitation.columnSpan = 5; excitation.rowSpan = 4;
    excitation.controls = {
        control("Excitation", "PLUCK / BOW", S::LargeKnob, 0, 0),
        control("Pick Position", "POSITION", S::Knob, 1, 0),
        control("Pick Hardness", "HARDNESS", S::Knob, 2, 0),
        control("Velocity Sensitivity", "TOUCH", S::Knob, 0, 1),
    };

    // L'archet a ses deux gestes propres : appuyer et tirer. Ils ne servent
    // qu'en position « bow », et rester groupés le dit mieux qu'une note.
    PanelSection bow;
    bow.title = "BOW";
    bow.accentColour = "#B08A4E";
    bow.column = 5; bow.row = 0; bow.columnSpan = 3; bow.rowSpan = 4;
    bow.controls = {
        control("Bow Pressure", "PRESSURE", S::Knob, 0, 0),
        control("Bow Speed", "SPEED", S::Knob, 0, 1),
    };

    // La corde elle-même : ce qui se perd à chaque aller-retour, et sa
    // raideur. Curseurs, parce que ce sont des grandeurs qu'on règle en
    // regardant leur position relative — comme sur une table d'harmonie.
    PanelSection stringSection;
    stringSection.title = "STRING";
    stringSection.accentColour = "#C9A567";
    stringSection.column = 8; stringSection.row = 0; stringSection.columnSpan = 4; stringSection.rowSpan = 4;
    stringSection.controls = {
        control("String Decay", "DECAY", S::VerticalSlider, 0, 0, 1, 2),
        control("String Damping", "DAMPING", S::VerticalSlider, 1, 0, 1, 2),
        control("Stiffness", "STIFFNESS", S::VerticalSlider, 2, 0, 1, 2),
        control("Release", "DAMPER", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection body;
    body.title = "BODY";
    body.accentColour = "#8E6B3A";
    body.column = 12; body.row = 0; body.columnSpan = 3; body.rowSpan = 4;
    body.controls = {
        control("Body Level", "LEVEL", S::Knob, 0, 0),
        control("Body Size", "SIZE", S::Knob, 0, 1),
    };

    PanelSection output;
    output.title = "OUTPUT";
    output.accentColour = "#C9A567";
    output.column = 15; output.row = 0; output.columnSpan = 3; output.rowSpan = 4;
    output.controls = {
        control("Drive", "DRIVE", S::Knob, 0, 0),
        control("Analog Character", "AGE", S::Knob, 1, 0),
        control("Output Level", "VOLUME", S::Knob, 0, 1),
    };

    panel.sections = {excitation, bow, stringSection, body, output};
    return panel;
}

// ---------------------------------------------------------------------------
// Piano acoustique
//
// Pas de façade d'origine à copier : un piano n'a pas de panneau, il a un
// couvercle. La disposition suit donc, comme pour la corde, le trajet
// PHYSIQUE — le marteau, les cordes, la table, la salle — qui est aussi
// l'ordre dans lequel un technicien intervient. Bois sombre, laiton : la
// matière dit l'instrument sans emprunter de marque.
// ---------------------------------------------------------------------------
MachinePanel makePiano() {
    MachinePanel panel;
    panel.pluginId = "vsm.piano";
    panel.displayName = "Piano (cordes frappées)";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#1A1512";
    panel.sectionColour = "#120E0C";
    panel.textColour = "#F0E7D8";
    panel.knobColour = "#C7A05A";
    panel.gridColumns = 18;
    panel.gridRows = 4;

    PanelSection hammer;
    hammer.title = "HAMMER";
    hammer.accentColour = "#C7A05A";
    hammer.column = 0; hammer.row = 0; hammer.columnSpan = 4; hammer.rowSpan = 4;
    hammer.controls = {
        control("Hammer Hardness", "FELT", S::LargeKnob, 0, 0),
        control("Hammer Position", "STRIKE", S::Knob, 1, 0),
        control("Velocity Sensitivity", "TOUCH", S::Knob, 0, 1),
    };

    PanelSection strings;
    strings.title = "STRINGS";
    strings.accentColour = "#C7A05A";
    strings.column = 4; strings.row = 0; strings.columnSpan = 5; strings.rowSpan = 4;
    strings.controls = {
        control("String Decay", "DECAY", S::VerticalSlider, 0, 0, 1, 2),
        control("String Damping", "DAMPING", S::VerticalSlider, 1, 0, 1, 2),
        control("Inharmonicity", "STIFFNESS", S::VerticalSlider, 2, 0, 1, 2),
        control("Unison Detune", "UNISON", S::VerticalSlider, 3, 0, 1, 2),
    };

    // L'étouffoir et la pédale vont ensemble : ce sont les deux faces du même
    // mécanisme, et les séparer rendrait la pédale incompréhensible.
    PanelSection damper;
    damper.title = "DAMPER";
    damper.accentColour = "#8E6B3A";
    damper.column = 9; damper.row = 0; damper.columnSpan = 3; damper.rowSpan = 4;
    damper.controls = {
        control("Release", "DAMPER", S::Knob, 0, 0),
        control("Sustain Pedal", "PEDAL", S::Toggle, 0, 1),
    };

    PanelSection board;
    board.title = "SOUNDBOARD";
    board.accentColour = "#8E6B3A";
    board.column = 12; board.row = 0; board.columnSpan = 3; board.rowSpan = 4;
    board.controls = {
        control("Soundboard Level", "LEVEL", S::Knob, 0, 0),
        control("Soundboard Size", "SIZE", S::Knob, 0, 1),
    };

    PanelSection output;
    output.title = "OUTPUT";
    output.accentColour = "#C7A05A";
    output.column = 15; output.row = 0; output.columnSpan = 3; output.rowSpan = 4;
    output.controls = {
        control("Tone Bass", "BASS", S::Knob, 0, 0),
        control("Tone Treble", "TREBLE", S::Knob, 1, 0),
        control("Stereo Spread", "SPREAD", S::Knob, 0, 1),
        control("Output Level", "VOLUME", S::Knob, 1, 1),
    };
    panel.omittedParameters = {
        {"Analog Character", "dérive d'accord très lente, commune à tout le parc : "
                             "elle se règle une fois et ne se joue pas"},
    };

    panel.sections = {hammer, strings, damper, board, output};
    return panel;
}

// ---------------------------------------------------------------------------
// Batterie acoustique
//
// Ici il y a bien une disposition d'origine à suivre, et c'est celle des
// boîtes à rythmes du parc : UNE COLONNE PAR PIÈCE, code couleur par famille.
// Regrouper « tous les niveaux » puis « tous les decays » serait plus compact
// et rendrait l'instrument inutilisable -- on règle une caisse claire, pas une
// rangée de potentiomètres.
// ---------------------------------------------------------------------------
MachinePanel makeDrums() {
    MachinePanel panel;
    panel.pluginId = "vsm.drums";
    panel.displayName = "Drums (batterie acoustique)";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#232021";
    panel.sectionColour = "#1A1718";
    panel.textColour = "#EDE6DC";
    panel.knobColour = "#B9B2A6";
    panel.gridColumns = 20;
    panel.gridRows = 4;

    PanelSection kick;
    kick.title = "KICK";
    kick.accentColour = "#C4562B";
    kick.column = 0; kick.row = 0; kick.columnSpan = 4; kick.rowSpan = 4;
    kick.controls = {
        control("Kick Level", "LEVEL", S::Knob, 0, 0),
        control("Kick Tune", "TUNE", S::Knob, 1, 0),
        control("Kick Decay", "DECAY", S::Knob, 0, 1),
        control("Kick Beater", "BEATER", S::Knob, 1, 1),
    };

    PanelSection snare;
    snare.title = "SNARE";
    snare.accentColour = "#D8A13F";
    snare.column = 4; snare.row = 0; snare.columnSpan = 4; snare.rowSpan = 4;
    snare.controls = {
        control("Snare Level", "LEVEL", S::Knob, 0, 0),
        control("Snare Tune", "TUNE", S::Knob, 1, 0),
        control("Snare Decay", "DECAY", S::Knob, 0, 1),
        control("Snare Wires", "SNARES", S::Knob, 1, 1),
    };

    PanelSection toms;
    toms.title = "TOMS";
    toms.accentColour = "#C4562B";
    toms.column = 8; toms.row = 0; toms.columnSpan = 3; toms.rowSpan = 4;
    toms.controls = {
        control("Tom Level", "LEVEL", S::Knob, 0, 0),
        control("Tom Tune", "TUNE", S::Knob, 1, 0),
        control("Tom Decay", "DECAY", S::Knob, 0, 1),
    };

    PanelSection hats;
    hats.title = "HI-HAT";
    hats.accentColour = "#7FA8C4";
    hats.column = 11; hats.row = 0; hats.columnSpan = 3; hats.rowSpan = 4;
    hats.controls = {
        control("Closed Hat Level", "CL LVL", S::Knob, 0, 0),
        control("Closed Hat Decay", "CL DEC", S::Knob, 1, 0),
        control("Open Hat Level", "OP LVL", S::Knob, 0, 1),
        control("Open Hat Decay", "OP DEC", S::Knob, 1, 1),
    };

    PanelSection cymbals;
    cymbals.title = "CYMBALS";
    cymbals.accentColour = "#7FA8C4";
    cymbals.column = 14; cymbals.row = 0; cymbals.columnSpan = 3; cymbals.rowSpan = 4;
    cymbals.controls = {
        control("Ride Level", "RIDE", S::Knob, 0, 0),
        control("Ride Decay", "RD DEC", S::Knob, 1, 0),
        control("Crash Level", "CRASH", S::Knob, 0, 1),
        control("Crash Decay", "CR DEC", S::Knob, 1, 1),
    };

    // La pièce n'est pas un effet ajouté : sans elle un kit modélisé sonne
    // électronique. Elle a donc sa place sur la façade, pas dans un rack.
    PanelSection room;
    room.title = "ROOM";
    room.accentColour = "#8E9B7A";
    room.column = 17; room.row = 0; room.columnSpan = 3; room.rowSpan = 4;
    room.controls = {
        control("Room Level", "LEVEL", S::Knob, 0, 0),
        control("Room Size", "SIZE", S::Knob, 1, 0),
        control("Output Level", "VOLUME", S::Knob, 0, 1),
    };

    panel.sequencer.kind = SequencerKind::DrumGrid;
    panel.sequencer.title = "PATTERN";
    panel.sequencer.stepCount = 16;
    panel.sequencer.rowSpan = 4;
    panel.sequencer.lanes = {
        {"KICK", 36}, {"SNARE", 38}, {"HH CL", 42}, {"HH OP", 46},
        {"TOM L", 41}, {"TOM M", 45}, {"TOM H", 48}, {"RIDE", 51},
    };
    // La grille s'AJOUTE sous les commandes, elle ne se glisse pas dedans :
    // sans cette ligne, elle se dessinait par-dessus la seconde rangée de
    // potentiomètres. Le rendu l'a montré, aucun test ne pouvait le voir.
    panel.gridRows += panel.sequencer.rowSpan;

    panel.sections = {kick, snare, toms, hats, cymbals, room};
    return panel;
}

// ---------------------------------------------------------------------------
// Vents
//
// Là encore, aucune façade d'origine : une clarinette n'a pas de boutons. La
// lecture suit le souffle -- l'embouchure, le tuyau, le pavillon, puis ce que
// le musicien ajoute par-dessus (le vibrato). Laiton sur noir.
// ---------------------------------------------------------------------------
MachinePanel makeWind() {
    MachinePanel panel;
    panel.pluginId = "vsm.wind";
    panel.displayName = "Wind (anche et lèvres)";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#1C1B19";
    panel.sectionColour = "#141311";
    panel.textColour = "#F2E9D6";
    panel.knobColour = "#D0AC63";
    panel.gridColumns = 16;
    panel.gridRows = 4;

    PanelSection mouth;
    mouth.title = "MOUTHPIECE";
    mouth.accentColour = "#D0AC63";
    mouth.column = 0; mouth.row = 0; mouth.columnSpan = 5; mouth.rowSpan = 4;
    mouth.controls = {
        control("Breath Pressure", "BREATH", S::LargeKnob, 0, 0),
        control("Reed Stiffness", "REED", S::Knob, 1, 0),
        control("Breath Noise", "AIR", S::Knob, 2, 0),
        control("Velocity Sensitivity", "TOUCH", S::Knob, 1, 1),
    };

    PanelSection bore;
    bore.title = "BORE";
    bore.accentColour = "#B08A4E";
    bore.column = 5; bore.row = 0; bore.columnSpan = 3; bore.rowSpan = 4;
    bore.controls = {
        control("Bell Damping", "BELL", S::Knob, 0, 0),
        control("Brassiness", "BRASS", S::Knob, 0, 1),
    };

    PanelSection envelope;
    envelope.title = "ARTICULATION";
    envelope.accentColour = "#8FA9C9";
    envelope.column = 8; envelope.row = 0; envelope.columnSpan = 3; envelope.rowSpan = 4;
    envelope.controls = {
        control("Attack", "TONGUE", S::VerticalSlider, 0, 0, 1, 2),
        control("Release", "RELEASE", S::VerticalSlider, 1, 0, 1, 2),
    };

    PanelSection vibrato;
    vibrato.title = "VIBRATO";
    vibrato.accentColour = "#8FA9C9";
    vibrato.column = 11; vibrato.row = 0; vibrato.columnSpan = 3; vibrato.rowSpan = 4;
    vibrato.controls = {
        control("Vibrato Depth", "DEPTH", S::Knob, 0, 0),
        control("Vibrato Rate", "RATE", S::Knob, 1, 0),
        control("Vibrato Delay", "DELAY", S::Knob, 0, 1),
    };

    PanelSection output;
    output.title = "OUTPUT";
    output.accentColour = "#D0AC63";
    output.column = 14; output.row = 0; output.columnSpan = 2; output.rowSpan = 4;
    output.controls = {
        control("Tone Bass", "BASS", S::Knob, 0, 0),
        control("Tone Treble", "TREBLE", S::Knob, 0, 1),
        control("Output Level", "VOLUME", S::Knob, 1, 0),
    };
    panel.omittedParameters = {
        {"Analog Character", "instabilité d'intonation très lente, commune au parc : "
                             "elle se règle une fois et ne se joue pas"},
    };

    panel.sections = {mouth, bore, envelope, vibrato, output};
    return panel;
}

/// `vsm.multisample` -- une façade DÉLIBÉRÉMENT PAUVRE, et c'est le sujet.
///
/// Les autres machines du parc portent leur timbre dans leurs commandes : on
/// tourne la coupure et le son change de nature. Ici, le timbre est dans les
/// échantillons, et la façade n'a rien à en dire. Lui inventer une rangée de
/// potentiomètres pour « faire riche » serait exactement le mensonge que le
/// § 29 d'ARCHITECTURE.md reproche aux façades décoratives : des commandes qui
/// ne font presque rien, sur une machine dont l'essentiel se joue ailleurs.
///
/// Ce que la façade montre est donc CE QUI SE JOUE : le programme choisi, la
/// réponse au toucher, l'articulation, et la sortie. Le reste -- les zones,
/// les couches, les boucles -- appartient au PROFIL, c'est-à-dire à un
/// fichier, pas à un bouton. Le bandeau de la machine affiche le nom du profil
/// installé et son attribution ; à défaut, il dit qu'aucun profil n'est
/// installé, plutôt que de laisser croire à une panne.
MachinePanel makeMultisample() {
    MachinePanel panel;
    panel.pluginId = "vsm.multisample";
    panel.displayName = "Multisample (acoustique échantillonné)";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#221A14";
    panel.sectionColour = "#191310";
    panel.textColour = "#F0E4D2";
    panel.knobColour = "#C9A227";
    panel.gridColumns = 12;
    panel.gridRows = 4;

    PanelSection instrument;
    instrument.title = "INSTRUMENT";
    instrument.accentColour = "#C9A227";
    instrument.column = 0; instrument.row = 0; instrument.columnSpan = 4; instrument.rowSpan = 4;
    instrument.contentColumns = 2;
    instrument.controls = {
        control("Program", "PROGRAM", S::Selector, 0, 0, 2, 1),
        control("Tune", "TUNE", S::Knob, 0, 1),
        control("Velocity Amount", "TOUCH", S::Knob, 1, 1),
    };

    PanelSection articulation;
    articulation.title = "ARTICULATION";
    articulation.accentColour = "#9FB4C7";
    articulation.column = 4; articulation.row = 0; articulation.columnSpan = 4; articulation.rowSpan = 4;
    articulation.contentColumns = 2;
    articulation.controls = {
        control("Attack", "ATTACK", S::VerticalSlider, 0, 0, 1, 2),
        control("Release", "RELEASE", S::VerticalSlider, 1, 0, 1, 2),
    };

    PanelSection output;
    output.title = "OUTPUT";
    output.accentColour = "#C9A227";
    output.column = 8; output.row = 0; output.columnSpan = 4; output.rowSpan = 4;
    output.contentColumns = 2;
    output.controls = {
        control("Tone Cutoff", "TONE", S::LargeKnob, 0, 0),
        control("Output Level", "VOLUME", S::Knob, 1, 0),
    };

    panel.sections = {instrument, articulation, output};
    return panel;
}

// ---------------------------------------------------------------------------
// Percussions à peaux et barres
//
// AUCUNE FAÇADE D'ORIGINE, et c'est le premier choix à faire : un conguero n'a
// pas de potentiomètres. La disposition suit donc ce qu'un percussionniste a
// devant lui -- les peaux à gauche, du plus grave au plus aigu, les métaux et
// les bois à droite, les grains secoués au bout. C'est l'ordre dans lequel on
// les frappe, pas l'ordre alphabétique.
//
// Bois clair et peau tendue plutôt que tôle pliée : cette machine n'est pas une
// boîte à rythmes électronique, et sa façade doit le dire avant qu'on ait lu
// une seule sérigraphie. La grille de pas est là quand même -- on programme
// une clave comme on programme un charleston -- avec les treize pièces en
// numérotation General MIDI.
// ---------------------------------------------------------------------------
MachinePanel makePerc() {
    MachinePanel panel;
    panel.pluginId = "vsm.perc";
    panel.displayName = "Percussion (peaux et barres)";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#3A2E24";
    panel.sectionColour = "#2B211A";
    panel.textColour = "#F0E4D2";
    panel.knobColour = "#D9C7A8";
    panel.gridColumns = 15;
    panel.gridRows = 5;

    auto piece = [](std::string title, std::string accent, int column, int span,
                    std::vector<PanelControl> controls) {
        PanelSection section;
        section.title = std::move(title);
        section.accentColour = std::move(accent);
        section.column = column;
        section.row = 0;
        section.columnSpan = span;
        section.rowSpan = 3;
        section.controls = std::move(controls);
        return section;
    };

    // Les peaux en terre cuite, les métaux en laiton, les bois en miel : trois
    // familles, trois couleurs, reconnaissables sans lire.
    const std::string peau = "#C1663A";
    const std::string bois = "#D9A441";
    const std::string metal = "#B9C2CC";

    panel.sections = {
        piece("CONGA", peau, 0, 3, {
            control("Conga Level", "LEVEL", S::Knob, 0, 0),
            control("Conga Tune", "TUNE", S::LargeKnob, 1, 0),
            control("Conga Decay", "DECAY", S::Knob, 0, 1),
        }),
        piece("BONGO", peau, 3, 3, {
            control("Bongo Level", "LEVEL", S::Knob, 0, 0),
            control("Bongo Tune", "TUNE", S::Knob, 1, 0),
            control("Bongo Decay", "DECAY", S::Knob, 0, 1),
        }),
        piece("TIMBALE", peau, 6, 3, {
            control("Timbale Level", "LEVEL", S::Knob, 0, 0),
            control("Timbale Tune", "TUNE", S::Knob, 1, 0),
            control("Timbale Decay", "DECAY", S::Knob, 0, 1),
        }),
        piece("COWBELL / WOOD", bois, 9, 3, {
            control("Cowbell Level", "BELL", S::Knob, 0, 0),
            control("Cowbell Tune", "BELL TUNE", S::Knob, 1, 0),
            control("Cowbell Decay", "BELL DEC", S::Knob, 2, 0),
            control("Wood Level", "WOOD", S::Knob, 0, 1),
            control("Wood Tune", "WOOD TUNE", S::Knob, 1, 1),
            control("Wood Decay", "WOOD DEC", S::Knob, 2, 1),
        }),
        piece("SHAKEN", metal, 12, 3, {
            control("Shaker Level", "SHAKER", S::Knob, 0, 0),
            control("Shaker Tone", "TONE", S::Knob, 1, 0),
            control("Shaker Decay", "DECAY", S::Knob, 0, 1),
            control("Tambourine Level", "TAMB", S::Knob, 1, 1),
            control("Tambourine Decay", "TAMB DEC", S::Knob, 2, 1),
        }),
    };

    PanelSection accent;
    accent.title = "ACCENT";
    accent.accentColour = peau;
    accent.column = 0; accent.row = 3; accent.columnSpan = 3; accent.rowSpan = 2;
    accent.contentColumns = 2;
    accent.controls = { control("Accent", "ACCENT", S::Knob, 0, 0) };
    panel.sections.push_back(accent);

    panel.sequencer.kind = SequencerKind::DrumGrid;
    panel.sequencer.title = "PERCUSSION PATTERN";
    panel.sequencer.stepCount = 16;
    panel.sequencer.rowSpan = 3;
    // Les treize pièces, en numérotation General MIDI. L'ordre des lignes est
    // celui de la façade -- peaux du grave à l'aigu, puis bois, puis métaux --
    // pour qu'on retrouve d'un regard, dans la grille, ce qu'on vient de régler.
    panel.sequencer.lanes = {
        {"LOW CONGA", 64}, {"HI CONGA", 63}, {"MUTE CONGA", 62},
        {"LOW BONGO", 61}, {"HI BONGO", 60},
        {"LOW TIMBALE", 66}, {"HI TIMBALE", 65},
        {"COWBELL", 56}, {"CLAVES", 75}, {"LOW WOOD", 77}, {"HI WOOD", 76},
        {"MARACAS", 70}, {"TAMBOURINE", 54},
    };
    // Les groupes de quatre reprennent les tons de la façade plutôt que le
    // rouge/orange des boîtes Roland : la machine n'est pas de cette famille.
    panel.sequencer.stepGroupColours = {"#C1663A", "#D9A441", "#B9C2CC", "#EADFCB"};
    panel.gridRows += panel.sequencer.rowSpan;
    return panel;
}

// ---------------------------------------------------------------------------
// Additif
//
// Pas de façade d'origine non plus, et surtout pas de coupure : c'est ce qui
// oriente toute la disposition. Sur un soustractif, l'œil va d'abord au gros
// potentiomètre de filtre ; ici il n'y en a pas, et le geste central est la
// PENTE du spectre -- elle joue exactement le rôle que la coupure joue
// ailleurs. Elle est donc seule, en grand, à gauche.
//
// Vient ensuite ce qu'aucune autre machine ne sait faire : la balance
// impairs/pairs, qui creuse un spectre à trous, et la raideur, qui étire les
// rangs. Ces deux commandes SONT la machine, et la façade les met côte à côte
// dans leur propre bloc plutôt que noyées parmi les réglages d'enveloppe.
//
// Anthracite et cuivre : un instrument de laboratoire, pas un objet de scène.
// ---------------------------------------------------------------------------
MachinePanel makeAdditive() {
    MachinePanel panel;
    panel.pluginId = "vsm.additive";
    panel.displayName = "Additive (le spectre rang par rang)";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#22252A";
    panel.sectionColour = "#191C20";
    panel.textColour = "#E4EAF0";
    panel.knobColour = "#C89B5A";
    panel.gridColumns = 15;
    panel.gridRows = 4;

    PanelSection spectre;
    spectre.title = "SPECTRUM";
    spectre.accentColour = "#C89B5A";
    spectre.column = 0; spectre.row = 0; spectre.columnSpan = 5; spectre.rowSpan = 4;
    spectre.controls = {
        control("Spectral Tilt", "TILT", S::LargeKnob, 0, 0),
        // « COUNT » et non « PARTIALS » : le bloc voisin s'appelle PARTIALS, et
        // deux intitulés identiques à deux endroits différents se lisent comme
        // une erreur avant de se lire comme une commande.
        control("Partials", "COUNT", S::Knob, 1, 0),
        control("Decay Tilt", "DAMPING", S::Knob, 0, 1),
    };

    // LE BLOC QUI JUSTIFIE LA MACHINE. Deux commandes, et aucune autre façade
    // du parc n'en porte d'équivalent : c'est le seul endroit où l'on pose un
    // spectre que nul filtre ne peut tailler.
    PanelSection partiels;
    partiels.title = "PARTIALS";
    partiels.accentColour = "#7FB4C4";
    partiels.column = 5; partiels.row = 0; partiels.columnSpan = 4; partiels.rowSpan = 4;
    partiels.contentColumns = 2;
    partiels.controls = {
        control("Odd/Even Balance", "ODD / EVEN", S::LargeKnob, 0, 0),
        control("Inharmonicity", "STRETCH", S::Knob, 1, 0),
        control("Attack Spread", "SPREAD", S::Knob, 1, 1),
    };

    PanelSection enveloppe;
    enveloppe.title = "ENVELOPE";
    enveloppe.accentColour = "#8FA9C9";
    enveloppe.column = 9; enveloppe.row = 0; enveloppe.columnSpan = 4; enveloppe.rowSpan = 4;
    enveloppe.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection sortie;
    sortie.title = "OUTPUT";
    sortie.accentColour = "#C89B5A";
    sortie.column = 13; sortie.row = 0; sortie.columnSpan = 2; sortie.rowSpan = 4;
    sortie.controls = {
        control("Velocity to Tilt", "TOUCH", S::Knob, 0, 0),
        control("Output Level", "VOLUME", S::Knob, 0, 1),
    };

    panel.omittedParameters = {
        {"Analog Character", "instabilité d'intonation très lente, commune au parc : "
                             "elle se règle une fois et ne se joue pas"},
    };

    panel.sections = {spectre, partiels, enveloppe, sortie};
    return panel;
}

// ---------------------------------------------------------------------------
// West Coast
//
// Une façade « côte ouest » ne ressemble à aucune façade Roland ou Moog, et ce
// n'est pas une affaire de goût : ces machines sont des MODULES, et leur
// panneau se lit de gauche à droite comme le signal circule -- l'oscillateur,
// puis le plieur, puis la porte. La disposition suit ce chemin, et le PLIEUR
// est au milieu, en grand, parce que c'est lui qui fait le son.
//
// Bois clair et sérigraphie sobre, comme les panneaux d'origine : ces
// instruments ont l'air d'appareils de mesure, et c'est délibéré chez eux
// aussi.
// ---------------------------------------------------------------------------
MachinePanel makeWestCoast() {
    MachinePanel panel;
    panel.pluginId = "vsm.westcoast";
    // Nom COURT : l'aperçu a montré que « West Coast (pliage et porte) » se
    // faisait couper au bord droit du bandeau. Une façade dont le nom déborde
    // dit d'emblée que personne ne l'a regardée.
    panel.displayName = "West Coast";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#E4DCCB";
    panel.sectionColour = "#D6CCB6";
    panel.textColour = "#2A2620";
    // Boutons SOMBRES sur façade claire : c'est la règle inverse du reste du
    // parc, et c'est ce qui fait reconnaître ces machines de loin.
    panel.knobColour = "#3A342B";
    // 14 x 5 et non 14 x 4 : à quatre rangées, la façade est si plate (rapport
    // 3,5) que le bandeau du nom se faisait couper au bord droit de l'aperçu.
    // Une rangée de plus donne aussi des cases moins écrasées, et c'est visible.
    panel.gridColumns = 14;
    panel.gridRows = 5;

    PanelSection osc;
    osc.title = "COMPLEX OSCILLATOR";
    osc.accentColour = "#7C6A4A";
    osc.column = 0; osc.row = 0; osc.columnSpan = 3; osc.rowSpan = 5;
    // UNE SEULE COLONNE, deux rangées. L'aperçu a montré pourquoi : deux
    // commandes côte à côte dans un bloc large recevaient chacune une case
    // énorme, et l'oscillateur -- qui n'est PAS le sujet de cette machine --
    // écrasait visuellement le plieur, qui l'est.
    osc.contentColumns = 1;
    osc.controls = {
        control("Mod Ratio", "RATIO", S::Knob, 0, 0),
        control("Mod Depth", "MOD DEPTH", S::Knob, 0, 1),
    };

    // LE BLOC QUI FAIT LA MACHINE : c'est ici qu'on FABRIQUE des harmoniques,
    // là où tout le reste du parc en enlève. Il est au centre et en grand.
    PanelSection folder;
    folder.title = "TIMBRE / FOLDER";
    folder.accentColour = "#B4562F";
    folder.column = 3; folder.row = 0; folder.columnSpan = 5; folder.rowSpan = 5;
    folder.contentColumns = 2;
    folder.controls = {
        // FOLD occupe DEUX rangées : c'est la seule façon de le rendre plus
        // gros que ses voisins, et il doit l'être -- sur une machine côte
        // ouest, c'est lui qui décide combien d'harmoniques existent.
        control("Fold", "FOLD", S::LargeKnob, 0, 0, 1, 2),
        control("Fold Symmetry", "SYMMETRY", S::Knob, 1, 0),
        control("Velocity to Fold", "TOUCH", S::Knob, 1, 1),
    };

    PanelSection gate;
    gate.title = "LOW PASS GATE";
    gate.accentColour = "#4A6B57";
    gate.column = 8; gate.row = 0; gate.columnSpan = 3; gate.rowSpan = 5;
    gate.controls = {
        control("Gate Cutoff", "CUTOFF", S::Knob, 0, 0),
        control("Gate Lag", "VACTROL", S::Knob, 1, 0),
        control("Output Level", "LEVEL", S::Knob, 0, 1),
    };

    PanelSection env;
    env.title = "ENVELOPE";
    env.accentColour = "#7C6A4A";
    env.column = 11; env.row = 0; env.columnSpan = 3; env.rowSpan = 5;
    env.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    panel.omittedParameters = {
        {"Analog Character", "instabilité d'intonation très lente, commune au parc : "
                             "elle se règle une fois et ne se joue pas"},
    };
    panel.sections = {osc, folder, gate, env};
    return panel;
}

// ---------------------------------------------------------------------------
// FM Drums
//
// Une colonne par pièce, comme les boîtes du parc -- mais chaque colonne porte
// DEUX commandes que les autres n'ont pas : le rapport et l'indice. Elles sont
// placées EN DESSOUS du couple hauteur/durée, parce que c'est l'ordre dans
// lequel on règle une percussion FM : d'abord la note et sa longueur, ensuite
// combien de métal on met dedans.
//
// Façade sombre et sérigraphie froide : ces boîtes étaient numériques, et
// leurs panneaux le disaient.
// ---------------------------------------------------------------------------
MachinePanel makeFmDrums() {
    MachinePanel panel;
    panel.pluginId = "vsm.fmdrums";
    panel.displayName = "FM Drums (percussions métalliques)";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#232A31";
    panel.sectionColour = "#1A2027";
    panel.textColour = "#DCE6EF";
    panel.knobColour = "#8FA6BC";
    panel.gridColumns = 16;
    panel.gridRows = 5;

    auto piece = [](std::string title, std::string accent, int column, int span,
                    std::vector<PanelControl> controls) {
        PanelSection section;
        section.title = std::move(title);
        section.accentColour = std::move(accent);
        section.column = column;
        section.row = 0;
        section.columnSpan = span;
        section.rowSpan = 3;
        section.controls = std::move(controls);
        return section;
    };

    const std::string cyan = "#4FB0C6";
    const std::string ambre = "#D2A24C";
    const std::string acier = "#9BA9B8";

    panel.sections = {
        piece("BASS DRUM", cyan, 0, 4, {
            control("Kick Level", "LEVEL", S::Knob, 0, 0),
            control("Kick Tune", "TUNE", S::Knob, 1, 0),
            control("Kick Decay", "DECAY", S::Knob, 2, 0),
            control("Kick Ratio", "RATIO", S::Knob, 0, 1),
            control("Kick Clang", "CLANG", S::LargeKnob, 1, 1),
        }),
        piece("SNARE / CLAP", cyan, 4, 4, {
            control("Snare Level", "LEVEL", S::Knob, 0, 0),
            control("Snare Tune", "TUNE", S::Knob, 1, 0),
            control("Snare Decay", "DECAY", S::Knob, 2, 0),
            control("Snare Ratio", "RATIO", S::Knob, 0, 1),
            control("Snare Clang", "CLANG", S::Knob, 1, 1),
            control("Clap Level", "CLAP", S::Knob, 2, 1),
            control("Clap Decay", "CLAP DEC", S::Knob, 3, 1),
        }),
        // LES DIX COMMANDES DE LA CLOCHE ET DU TOM TIENNENT ICI, et pas
        // ailleurs : l'aperçu a montré « BELL RATIO » et « BELL CLANG » posées
        // dans le bloc HAT faute de place, ce qui range deux réglages de cloche
        // sous un intitulé de charleston. Une façade qui ment sur ce qu'elle
        // groupe est pire qu'une façade serrée.
        piece("TOM / BELL", ambre, 8, 5, {
            control("Tom Level", "TOM", S::Knob, 0, 0),
            control("Tom Tune", "TOM TUNE", S::Knob, 1, 0),
            control("Tom Decay", "TOM DEC", S::Knob, 2, 0),
            control("Tom Ratio", "TOM RATIO", S::Knob, 3, 0),
            control("Tom Clang", "TOM CLANG", S::Knob, 4, 0),
            control("Bell Level", "BELL", S::Knob, 0, 1),
            control("Bell Tune", "BELL TUNE", S::Knob, 1, 1),
            control("Bell Decay", "BELL DEC", S::Knob, 2, 1),
            control("Bell Ratio", "BELL RATIO", S::Knob, 3, 1),
            control("Bell Clang", "BELL CLANG", S::Knob, 4, 1),
        }),
        piece("HAT", acier, 13, 3, {
            control("Hat Level", "LEVEL", S::Knob, 0, 0),
            control("Hat Tone", "TONE", S::Knob, 1, 0),
            control("Closed Hat Decay", "CH DECAY", S::Knob, 0, 1),
            control("Open Hat Decay", "OH DECAY", S::Knob, 1, 1),
        }),
    };

    PanelSection accent;
    accent.title = "ACCENT";
    accent.accentColour = cyan;
    accent.column = 0; accent.row = 3; accent.columnSpan = 3; accent.rowSpan = 2;
    accent.contentColumns = 2;
    accent.controls = { control("Accent", "ACCENT", S::Knob, 0, 0) };
    panel.sections.push_back(accent);

    panel.sequencer.kind = SequencerKind::DrumGrid;
    panel.sequencer.title = "RHYTHM PATTERN";
    panel.sequencer.stepCount = 16;
    panel.sequencer.rowSpan = 3;
    panel.sequencer.lanes = {
        {"BASS DRUM", 36}, {"SNARE", 38}, {"CLAP", 39}, {"TOM", 45},
        {"CLOSED HAT", 42}, {"OPEN HAT", 46}, {"BELL", 49},
    };
    panel.sequencer.stepGroupColours = {"#4FB0C6", "#D2A24C", "#9BA9B8", "#E6EDF3"};
    panel.gridRows += panel.sequencer.rowSpan;
    return panel;
}

// ---------------------------------------------------------------------------
// Vocal
//
// Aucune façade d'origine : personne n'a jamais fabriqué de synthétiseur vocal
// à potentiomètres, et les rares tentatives étaient des ordinateurs. La
// disposition suit donc le MODÈLE, qui est celui de la phonétique : à gauche la
// SOURCE (la glotte -- tension, souffle), au centre le CONDUIT (la voyelle et
// sa taille), à droite ce que le chanteur ajoute (le vibrato) puis la sortie.
// C'est l'ordre dans lequel l'air traverse, et c'est le seul ordre défendable
// quand il n'y a pas d'objet à copier.
//
// La commande VOYELLE est en grand et seule dans son bloc : sur cette machine,
// elle joue le rôle que la coupure joue ailleurs, et tout le reste la colore.
// Couleurs chair et bois sombre, loin des façades d'instruments électroniques.
// ---------------------------------------------------------------------------
MachinePanel makeVocal() {
    MachinePanel panel;
    panel.pluginId = "vsm.vocal";
    panel.displayName = "Vocal";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#2E2622";
    panel.sectionColour = "#241D1A";
    panel.textColour = "#F0DFD2";
    panel.knobColour = "#C99B7E";
    panel.gridColumns = 14;
    panel.gridRows = 5;

    PanelSection glotte;
    glotte.title = "GLOTTIS";
    glotte.accentColour = "#C4665A";
    glotte.column = 0; glotte.row = 0; glotte.columnSpan = 3; glotte.rowSpan = 5;
    glotte.contentColumns = 1;
    glotte.controls = {
        control("Tension", "TENSION", S::Knob, 0, 0),
        control("Breath", "BREATH", S::Knob, 0, 1),
    };

    // LE BLOC QUI FAIT LA MACHINE : c'est ici qu'on choisit une voyelle et la
    // taille de la gorge qui la prononce, et personne d'autre dans le parc ne
    // sait faire ni l'un ni l'autre.
    PanelSection conduit;
    conduit.title = "VOCAL TRACT";
    conduit.accentColour = "#D9A066";
    conduit.column = 3; conduit.row = 0; conduit.columnSpan = 5; conduit.rowSpan = 5;
    conduit.contentColumns = 2;
    conduit.controls = {
        control("Vowel", "A  E  I  O  U", S::LargeKnob, 0, 0, 1, 2),
        control("Formant Shift", "TRACT SIZE", S::Knob, 1, 0),
        control("Velocity to Breath", "TOUCH", S::Knob, 1, 1),
    };

    PanelSection vibrato;
    vibrato.title = "VIBRATO";
    vibrato.accentColour = "#8FA9C9";
    vibrato.column = 8; vibrato.row = 0; vibrato.columnSpan = 3; vibrato.rowSpan = 5;
    vibrato.controls = {
        control("Vibrato Depth", "DEPTH", S::Knob, 0, 0),
        control("Vibrato Rate", "RATE", S::Knob, 1, 0),
        control("Vibrato Delay", "DELAY", S::Knob, 0, 1),
        control("Output Level", "LEVEL", S::Knob, 1, 1),
    };

    PanelSection env;
    env.title = "ENVELOPE";
    env.accentColour = "#C4665A";
    env.column = 11; env.row = 0; env.columnSpan = 3; env.rowSpan = 5;
    env.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    panel.omittedParameters = {
        {"Analog Character", "instabilité d'intonation très lente, commune au parc : "
                             "elle se règle une fois et ne se joue pas"},
    };
    panel.sections = {glotte, conduit, vibrato, env};
    return panel;
}

// ---------------------------------------------------------------------------
// Phase Distortion
//
// Les machines d'origine étaient des claviers de grande série au panneau
// minuscule : quelques boutons membrane et un afficheur. Copier ça donnerait
// une façade illisible et fausse -- ce qui se jouait sur ces instruments se
// réglait par menus, pas sous les doigts.
//
// La disposition suit donc ce que la machine FAIT : la déformation en grand à
// gauche, avec ce qui la pilote dans le temps ; la résonance à part, parce
// qu'elle ne sert que sur une partie du répertoire et qu'elle a sa propre
// logique -- un RANG entier, pas une fréquence. Puis les deux enveloppes, celle
// du timbre et celle du niveau, côte à côte : c'est le geste de ces machines,
// et les voir ensemble est ce qui permet de le régler.
//
// Gris clair et bleu d'écran : ces instruments étaient numériques et s'en
// vantaient.
// ---------------------------------------------------------------------------
MachinePanel makePhaseDist() {
    MachinePanel panel;
    panel.pluginId = "vsm.phasedist";
    panel.displayName = "Phase Distortion";
    panel.chassis = Chassis::Plastic;
    panel.panelColour = "#D5D7DA";
    panel.sectionColour = "#C4C7CC";
    panel.textColour = "#20242A";
    panel.knobColour = "#3A4048";
    panel.gridColumns = 15;
    panel.gridRows = 5;

    PanelSection dist;
    dist.title = "PHASE DISTORTION";
    dist.accentColour = "#2C6FB5";
    dist.column = 0; dist.row = 0; dist.columnSpan = 4; dist.rowSpan = 5;
    dist.contentColumns = 2;
    dist.controls = {
        control("Distortion", "AMOUNT", S::LargeKnob, 0, 0, 1, 2),
        control("Env to Distortion", "ENV", S::Knob, 1, 0),
        control("Velocity to Distortion", "TOUCH", S::Knob, 1, 1),
    };

    PanelSection reso;
    reso.title = "RESONANCE";
    reso.accentColour = "#B5502C";
    reso.column = 4; reso.row = 0; reso.columnSpan = 3; reso.rowSpan = 5;
    reso.contentColumns = 1;
    reso.controls = {
        control("Resonance", "DEPTH", S::Knob, 0, 0),
        // SÉLECTEUR et non potentiomètre : ce réglage saute d'un rang entier à
        // l'autre, il ne glisse pas. Le montrer comme un bouton continu ferait
        // croire à une coupure de filtre, c'est-à-dire exactement ce que cette
        // machine n'a pas.
        control("Resonance Harmonic", "HARMONIC", S::Selector, 0, 1),
    };

    PanelSection timbre;
    timbre.title = "TIMBRE ENVELOPE";
    timbre.accentColour = "#2C6FB5";
    timbre.column = 7; timbre.row = 0; timbre.columnSpan = 4; timbre.rowSpan = 5;
    timbre.controls = {
        control("Mod Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Mod Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Mod Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Mod Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection ampli;
    ampli.title = "AMP ENVELOPE";
    ampli.accentColour = "#4A5560";
    ampli.column = 11; ampli.row = 0; ampli.columnSpan = 4; ampli.rowSpan = 5;
    ampli.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    panel.omittedParameters = {
        {"Analog Character", "instabilité d'intonation très lente, commune au parc : "
                             "elle se règle une fois et ne se joue pas"},
        {"Output Level", "le niveau se règle à la tranche du mixeur, et cette façade "
                         "n'a pas de place pour une commande que le mixeur porte déjà"},
    };
    panel.sections = {dist, reso, timbre, ampli};
    return panel;
}

// ---------------------------------------------------------------------------
// Divider
//
// Ces machines-là ONT une façade d'origine, et elle est célèbre : un long
// bandeau plat, quelques bascules et deux ou trois potentiomètres, vinyle noir
// et sérigraphie blanche. Rien à régler ou presque -- ce qui EST le sujet. Un
// instrument à huit commandes se lit d'un coup d'oeil, et lui inventer des
// blocs pour « faire riche » serait le mensonge que le § 29 reproche aux
// façades décoratives.
//
// L'ENSEMBLE est en grand et à part, parce que c'est lui qu'on vient chercher :
// une corde électronique sans son chorus n'est qu'un orgue pauvre.
// ---------------------------------------------------------------------------
MachinePanel makeDivider() {
    MachinePanel panel;
    panel.pluginId = "vsm.divider";
    panel.displayName = "Divider";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#1A1A1C";
    panel.sectionColour = "#131315";
    panel.textColour = "#F2F2EE";
    panel.knobColour = "#E8E4D8";
    panel.gridColumns = 12;
    panel.gridRows = 4;

    PanelSection registres;
    registres.title = "REGISTRATION";
    registres.accentColour = "#E8E4D8";
    registres.column = 0; registres.row = 0; registres.columnSpan = 4; registres.rowSpan = 4;
    registres.contentColumns = 2;
    registres.controls = {
        control("16' Level", "16'", S::VerticalSlider, 0, 0, 1, 2),
        control("8' Level", "8'", S::VerticalSlider, 1, 0, 1, 2),
    };

    PanelSection ensemble;
    ensemble.title = "ENSEMBLE";
    ensemble.accentColour = "#4FA3C6";
    ensemble.column = 4; ensemble.row = 0; ensemble.columnSpan = 3; ensemble.rowSpan = 4;
    ensemble.contentColumns = 1;
    ensemble.controls = {
        control("Ensemble", "DEPTH", S::LargeKnob, 0, 0),
    };

    PanelSection couleur;
    couleur.title = "TONE";
    couleur.accentColour = "#E8E4D8";
    couleur.column = 7; couleur.row = 0; couleur.columnSpan = 2; couleur.rowSpan = 4;
    couleur.contentColumns = 1;
    couleur.controls = {
        control("Tone", "TONE", S::Knob, 0, 0),
        control("Output Level", "VOLUME", S::Knob, 0, 1),
    };

    PanelSection enveloppe;
    enveloppe.title = "ENVELOPE";
    enveloppe.accentColour = "#8A8892";
    enveloppe.column = 9; enveloppe.row = 0; enveloppe.columnSpan = 3; enveloppe.rowSpan = 4;
    enveloppe.contentColumns = 2;
    // DEUX COMMANDES, PAS QUATRE, et c'est une fidélité : ces machines n'ont ni
    // déclin ni maintien réglables. Leur clavier ouvre et ferme des portes,
    // rien de plus.
    enveloppe.controls = {
        control("Attack", "ATTACK", S::VerticalSlider, 0, 0, 1, 2),
        control("Release", "RELEASE", S::VerticalSlider, 1, 0, 1, 2),
    };

    panel.omittedParameters = {
        {"Analog Character", "la dérive des douze maîtres : elle est le caractère de "
                             "cette machine et se règle une fois, elle ne se joue pas"},
    };
    panel.sections = {registres, ensemble, couleur, enveloppe};
    return panel;
}

// ---------------------------------------------------------------------------
// PSG
//
// Une puce n'a pas de façade : elle a des REGISTRES, qu'un programme écrit. La
// disposition ne copie donc rien -- elle montre ce qu'on écrivait dans ces
// registres, dans l'ordre où l'on y pensait : d'abord l'horloge et les bits,
// qui décident de la GRILLE dans laquelle tout le reste tombe, puis les voix
// carrées, puis le bruit, puis l'enveloppe.
//
// L'HORLOGE est en grand, et c'est le choix qui porte tout le reste : sur cette
// machine, elle joue le rôle que la coupure joue sur un soustractif, sauf
// qu'elle agit sur la JUSTESSE au lieu du timbre. La montrer petite, à côté
// d'un rapport cyclique, laisserait croire à un réglage d'accordage fin.
//
// Vert phosphore sur gris de boîtier : ces puces vivaient derrière un écran.
// ---------------------------------------------------------------------------
MachinePanel makePsg() {
    MachinePanel panel;
    panel.pluginId = "vsm.psg";
    panel.displayName = "PSG";
    panel.chassis = Chassis::Plastic;
    panel.panelColour = "#2A2E2B";
    panel.sectionColour = "#1F2320";
    panel.textColour = "#D8E8D0";
    panel.knobColour = "#8FCB7A";
    panel.gridColumns = 14;
    panel.gridRows = 4;

    PanelSection puce;
    puce.title = "CHIP";
    puce.accentColour = "#8FCB7A";
    puce.column = 0; puce.row = 0; puce.columnSpan = 4; puce.rowSpan = 4;
    puce.contentColumns = 2;
    puce.controls = {
        control("Clock", "CLOCK", S::LargeKnob, 0, 0, 1, 2),
        // SÉLECTEUR : une profondeur de volume est un nombre de BITS, donc un
        // entier. Un potentiomètre continu ferait croire à un réglage de
        // niveau, c'est-à-dire à autre chose.
        control("Volume Bits", "BITS", S::Selector, 1, 0),
        control("Output Level", "VOLUME", S::Knob, 1, 1),
    };

    PanelSection carre;
    carre.title = "SQUARE";
    carre.accentColour = "#7AB8CB";
    carre.column = 4; carre.row = 0; carre.columnSpan = 4; carre.rowSpan = 4;
    carre.contentColumns = 2;
    carre.controls = {
        control("Pulse Width", "WIDTH", S::Knob, 0, 0),
        control("Square Voices", "VOICES", S::Selector, 1, 0),
        control("Detune", "DETUNE", S::Knob, 0, 1),
    };

    PanelSection bruit;
    bruit.title = "NOISE";
    bruit.accentColour = "#CB9A7A";
    bruit.column = 8; bruit.row = 0; bruit.columnSpan = 3; bruit.rowSpan = 4;
    bruit.contentColumns = 1;
    bruit.controls = {
        control("Noise Level", "LEVEL", S::Knob, 0, 0),
        control("Noise Period", "PERIOD", S::Knob, 0, 1),
    };

    PanelSection env;
    env.title = "ENVELOPE";
    env.accentColour = "#8FCB7A";
    env.column = 11; env.row = 0; env.columnSpan = 3; env.rowSpan = 4;
    env.controls = {
        control("Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    panel.sections = {puce, carre, bruit, env};
    return panel;
}

// ---------------------------------------------------------------------------
// Stochastic
//
// Aucune façade d'origine : cette famille est née dans un article et a vécu
// dans du code, jamais dans un boîtier. La disposition suit donc le MODÈLE, qui
// tient en deux idées : une FORME décrite par des points, et une DIVAGATION qui
// la déplace. Les deux sont côte à côte, en grand, parce qu'il n'y a rien
// d'autre à comprendre sur cette machine.
//
// Le VERROU DE HAUTEUR est à part, et c'est délibéré : il n'appartient ni à la
// forme ni à la divagation, il décide si l'instrument est jouable dans un
// morceau ou s'il part en promenade. Papier millimétré et encre : un instrument
// qui vient d'un article.
// ---------------------------------------------------------------------------
MachinePanel makeStochastic() {
    MachinePanel panel;
    panel.pluginId = "vsm.stochastic";
    panel.displayName = "Stochastic";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#E8E6DC";
    panel.sectionColour = "#DAD7CB";
    panel.textColour = "#232019";
    panel.knobColour = "#2E2A22";
    panel.gridColumns = 14;
    panel.gridRows = 4;

    PanelSection forme;
    forme.title = "WAVEFORM";
    forme.accentColour = "#2E2A22";
    forme.column = 0; forme.row = 0; forme.columnSpan = 4; forme.rowSpan = 4;
    forme.contentColumns = 1;
    forme.controls = {
        // SÉLECTEUR : un nombre de points est un entier. Un potentiomètre
        // continu laisserait croire à un morphage, ce que cette machine ne fait
        // pas -- elle ajoute ou retire des angles.
        control("Breakpoints", "POINTS", S::Selector, 0, 0),
        control("Tone", "TONE", S::Knob, 0, 1),
    };

    PanelSection divagation;
    divagation.title = "WANDER";
    divagation.accentColour = "#A63D2F";
    divagation.column = 4; divagation.row = 0; divagation.columnSpan = 4; divagation.rowSpan = 4;
    divagation.contentColumns = 2;
    divagation.controls = {
        control("Shape Wander", "SHAPE", S::LargeKnob, 0, 0, 1, 2),
        control("Time Wander", "TIME", S::Knob, 1, 0),
        control("Velocity to Wander", "TOUCH", S::Knob, 1, 1),
    };

    PanelSection verrou;
    verrou.title = "PITCH";
    verrou.accentColour = "#2F6EA6";
    verrou.column = 8; verrou.row = 0; verrou.columnSpan = 2; verrou.rowSpan = 4;
    verrou.contentColumns = 1;
    verrou.controls = {
        control("Pitch Lock", "LOCK", S::Knob, 0, 0),
        control("Output Level", "LEVEL", S::Knob, 0, 1),
    };

    PanelSection env;
    env.title = "ENVELOPE";
    env.accentColour = "#2E2A22";
    env.column = 10; env.row = 0; env.columnSpan = 4; env.rowSpan = 4;
    env.controls = {
        control("Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    panel.sections = {forme, divagation, verrou, env};
    return panel;
}

/// `vsm.cone` -- l'anche sur perce conique : saxophone, hautbois, basson.
///
/// LA FAÇADE EST CELLE DE `vsm.wind`, COMMANDE POUR COMMANDE, et c'est une
/// décision d'en-tête de la machine : ce qui sépare le cône du cylindre est
/// la PERCE, pas le geste. Souffle, anche, pavillon, articulation, vibrato --
/// le musicien retrouve les mêmes mains. Seule la livrée change : le laiton
/// verni d'un saxophone, pour que l'oeil distingue d'emblée les deux vents.
MachinePanel makeCone() {
    MachinePanel panel;
    panel.pluginId = "vsm.cone";
    panel.displayName = "Cone (anche sur perce conique)";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#241A0F";
    panel.sectionColour = "#191106";
    panel.textColour = "#F5E8C8";
    panel.knobColour = "#E0B45C";
    panel.gridColumns = 16;
    panel.gridRows = 4;

    PanelSection mouth;
    mouth.title = "MOUTHPIECE";
    mouth.accentColour = "#E0B45C";
    mouth.column = 0; mouth.row = 0; mouth.columnSpan = 5; mouth.rowSpan = 4;
    mouth.controls = {
        control("Breath Pressure", "BREATH", S::LargeKnob, 0, 0),
        control("Reed Stiffness", "REED", S::Knob, 1, 0),
        control("Breath Noise", "AIR", S::Knob, 2, 0),
        control("Velocity Sensitivity", "TOUCH", S::Knob, 1, 1),
    };

    PanelSection bore;
    bore.title = "CONICAL BORE";
    bore.accentColour = "#C29140";
    bore.column = 5; bore.row = 0; bore.columnSpan = 3; bore.rowSpan = 4;
    bore.controls = {
        control("Bell Damping", "BELL", S::Knob, 0, 0),
        control("Brassiness", "EMBOUCHURE", S::Knob, 0, 1),
    };

    PanelSection envelope;
    envelope.title = "ARTICULATION";
    envelope.accentColour = "#9CB0C9";
    envelope.column = 8; envelope.row = 0; envelope.columnSpan = 3; envelope.rowSpan = 4;
    envelope.controls = {
        control("Attack", "TONGUE", S::VerticalSlider, 0, 0, 1, 2),
        control("Release", "RELEASE", S::VerticalSlider, 1, 0, 1, 2),
    };

    PanelSection vibrato;
    vibrato.title = "VIBRATO";
    vibrato.accentColour = "#9CB0C9";
    vibrato.column = 11; vibrato.row = 0; vibrato.columnSpan = 3; vibrato.rowSpan = 4;
    vibrato.controls = {
        control("Vibrato Depth", "DEPTH", S::Knob, 0, 0),
        control("Vibrato Rate", "RATE", S::Knob, 1, 0),
        control("Vibrato Delay", "DELAY", S::Knob, 0, 1),
    };

    PanelSection output;
    output.title = "OUTPUT";
    output.accentColour = "#E0B45C";
    output.column = 14; output.row = 0; output.columnSpan = 2; output.rowSpan = 4;
    output.controls = {
        control("Tone Bass", "BASS", S::Knob, 0, 0),
        control("Tone Treble", "TREBLE", S::Knob, 0, 1),
        control("Output Level", "VOLUME", S::Knob, 1, 0),
    };
    panel.omittedParameters = {
        {"Analog Character", "instabilité d'intonation très lente, commune au parc : "
                             "elle se règle une fois et ne se joue pas"},
    };

    panel.sections = {mouth, bore, envelope, vibrato, output};
    return panel;
}

/// `vsm.vector` -- la synthèse vectorielle : quatre coins, un trajet.
///
/// La disposition suit le GESTE de l'original (Prophet VS) : le vecteur au
/// centre de la façade -- c'est lui l'instrument --, les quatre coins autour
/// de lui comme sur la sérigraphie du joystick (A en haut à gauche, D en bas
/// à droite), le filtre et les enveloppes à droite. Livrée sombre à accents
/// violets, la teinte des machines numériques de la fin des années 80.
MachinePanel makeVector() {
    MachinePanel panel;
    panel.pluginId = "vsm.vector";
    panel.displayName = "Vector (quatre coins, un trajet)";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#17141F";
    panel.sectionColour = "#0F0D16";
    panel.textColour = "#E9E4F5";
    panel.knobColour = "#8E7CC3";
    panel.gridColumns = 18;
    panel.gridRows = 4;

    PanelSection cornerA;
    cornerA.title = "A";
    cornerA.accentColour = "#8E7CC3";
    cornerA.column = 0; cornerA.row = 0; cornerA.columnSpan = 2; cornerA.rowSpan = 2;
    cornerA.controls = {
        control("A Shape", "SHAPE", S::Knob, 0, 0),
        control("A Detune", "DETUNE", S::Knob, 1, 0),
    };
    PanelSection cornerC;
    cornerC.title = "C";
    cornerC.accentColour = "#8E7CC3";
    cornerC.column = 0; cornerC.row = 2; cornerC.columnSpan = 2; cornerC.rowSpan = 2;
    cornerC.controls = {
        control("C Shape", "SHAPE", S::Knob, 0, 0),
        control("C Detune", "DETUNE", S::Knob, 1, 0),
    };

    PanelSection vecteur;
    vecteur.title = "VECTOR";
    vecteur.accentColour = "#C3B1F0";
    vecteur.column = 2; vecteur.row = 0; vecteur.columnSpan = 5; vecteur.rowSpan = 4;
    vecteur.controls = {
        control("Vector X", "X", S::LargeKnob, 0, 0),
        control("Vector Y", "Y", S::LargeKnob, 1, 0),
        control("Orbit Rate", "ORBIT", S::Knob, 0, 1),
        control("Orbit Depth", "DEPTH", S::Knob, 1, 1),
    };

    PanelSection cornerB;
    cornerB.title = "B";
    cornerB.accentColour = "#8E7CC3";
    cornerB.column = 7; cornerB.row = 0; cornerB.columnSpan = 2; cornerB.rowSpan = 2;
    cornerB.controls = {
        control("B Shape", "SHAPE", S::Knob, 0, 0),
        control("B Detune", "DETUNE", S::Knob, 1, 0),
    };
    PanelSection cornerD;
    cornerD.title = "D";
    cornerD.accentColour = "#8E7CC3";
    cornerD.column = 7; cornerD.row = 2; cornerD.columnSpan = 2; cornerD.rowSpan = 2;
    cornerD.controls = {
        control("D Shape", "SHAPE", S::Knob, 0, 0),
        control("D Detune", "DETUNE", S::Knob, 1, 0),
    };

    PanelSection filtre;
    filtre.title = "FILTER";
    filtre.accentColour = "#7CA6C3";
    filtre.column = 9; filtre.row = 0; filtre.columnSpan = 3; filtre.rowSpan = 4;
    filtre.controls = {
        control("Filter Cutoff", "CUTOFF", S::LargeKnob, 0, 0),
        control("Filter Resonance", "RES", S::Knob, 1, 0),
        control("Filter Env Amount", "ENV", S::Knob, 0, 1),
        control("Filter Key Track", "TRACK", S::Knob, 1, 1),
    };

    PanelSection envs;
    envs.title = "ENVELOPES";
    envs.accentColour = "#7CA6C3";
    envs.column = 12; envs.row = 0; envs.columnSpan = 4; envs.rowSpan = 4;
    envs.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
        control("Filter Attack", "FA", S::Knob, 0, 2),
        control("Filter Decay", "FD", S::Knob, 1, 2),
        control("Filter Sustain", "FS", S::Knob, 2, 2),
        control("Filter Release", "FR", S::Knob, 3, 2),
    };

    PanelSection output;
    output.title = "OUTPUT";
    output.accentColour = "#C3B1F0";
    output.column = 16; output.row = 0; output.columnSpan = 2; output.rowSpan = 4;
    output.controls = {
        control("Velocity Sensitivity", "TOUCH", S::Knob, 0, 0),
        control("Output Level", "VOLUME", S::Knob, 0, 1),
    };
    panel.omittedParameters = {
        {"Analog Character", "instabilité d'intonation très lente, commune au parc : "
                             "elle se règle une fois et ne se joue pas"},
    };

    panel.sections = {cornerA, cornerC, vecteur, cornerB, cornerD, filtre, envs, output};
    return panel;
}

/// `vsm.granular` -- le nuage de grains : la matière au centre, la dispersion
/// à sa droite -- c'est elle l'instrument --, le filtre et l'enveloppe
/// ensuite. Livrée gris perle piquée de cyan : la couleur d'un banc de
/// laboratoire, pour la seule machine du parc née d'un papier plutôt que
/// d'un panneau.
MachinePanel makeGranular() {
    MachinePanel panel;
    panel.pluginId = "vsm.granular";
    panel.displayName = "Granular (le nuage de grains)";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#1A1D1E";
    panel.sectionColour = "#121415";
    panel.textColour = "#E8F0F2";
    panel.knobColour = "#6FC2CE";
    panel.gridColumns = 16;
    panel.gridRows = 4;

    PanelSection matiere;
    matiere.title = "GRAIN";
    matiere.accentColour = "#6FC2CE";
    matiere.column = 0; matiere.row = 0; matiere.columnSpan = 4; matiere.rowSpan = 4;
    matiere.controls = {
        control("Grain Size", "SIZE", S::LargeKnob, 0, 0),
        control("Density", "DENSITY", S::LargeKnob, 1, 0),
        control("Grain Shape", "SOURCE", S::Knob, 0, 1),
        control("Stereo Spread", "SPREAD", S::Knob, 1, 1),
    };

    PanelSection nuage;
    nuage.title = "CLOUD";
    nuage.accentColour = "#9FDDE6";
    nuage.column = 4; nuage.row = 0; nuage.columnSpan = 4; nuage.rowSpan = 4;
    nuage.controls = {
        control("Pitch Spray", "PITCH", S::LargeKnob, 0, 0),
        control("Time Spray", "TIME", S::Knob, 1, 0),
        control("Shimmer", "SHIMMER", S::Knob, 1, 1),
    };

    PanelSection filtre;
    filtre.title = "FILTER";
    filtre.accentColour = "#7CA6C3";
    filtre.column = 8; filtre.row = 0; filtre.columnSpan = 2; filtre.rowSpan = 4;
    filtre.controls = {
        control("Filter Cutoff", "CUTOFF", S::Knob, 0, 0),
        control("Filter Resonance", "RES", S::Knob, 0, 1),
    };

    PanelSection env;
    env.title = "ENVELOPE";
    env.accentColour = "#7CA6C3";
    env.column = 10; env.row = 0; env.columnSpan = 4; env.rowSpan = 4;
    env.controls = {
        control("Amp Attack", "A", S::VerticalSlider, 0, 0, 1, 2),
        control("Amp Decay", "D", S::VerticalSlider, 1, 0, 1, 2),
        control("Amp Sustain", "S", S::VerticalSlider, 2, 0, 1, 2),
        control("Amp Release", "R", S::VerticalSlider, 3, 0, 1, 2),
    };

    PanelSection output;
    output.title = "OUTPUT";
    output.accentColour = "#6FC2CE";
    output.column = 14; output.row = 0; output.columnSpan = 2; output.rowSpan = 4;
    output.controls = {
        control("Velocity Sensitivity", "TOUCH", S::Knob, 0, 0),
        control("Output Level", "VOLUME", S::Knob, 0, 1),
    };

    panel.sections = {matiere, nuage, filtre, env, output};
    return panel;
}

/// `vsm.cs80` -- deux couches, et une pression par note.
///
/// La disposition suit l'original : les deux couches EN MIROIR de part et
/// d'autre du mélangeur, chacune avec sa rangée complète (oscillateur, coupe-
/// bas, filtre) -- c'est ainsi que le panneau du CS-80 est organisé, et c'est
/// ce qui rend lisible qu'une touche allume deux synthétiseurs. La PRESSION a
/// sa propre section : elle est le geste de la machine, pas un réglage
/// d'appoint. Livrée bois et crème, comme les grandes consoles japonaises de
/// la fin des années 70.
MachinePanel makeCs80() {
    MachinePanel panel;
    panel.pluginId = "vsm.cs80";
    panel.displayName = "CS-80-style (deux couches, pression par note)";
    panel.chassis = Chassis::Wood;
    panel.panelColour = "#241C14";
    panel.sectionColour = "#191309";
    panel.textColour = "#F2E7D2";
    panel.knobColour = "#C9A961";
    panel.gridColumns = 20;
    panel.gridRows = 4;

    PanelSection coucheI;
    coucheI.title = "CHANNEL I";
    coucheI.accentColour = "#C9A961";
    coucheI.column = 0; coucheI.row = 0; coucheI.columnSpan = 6; coucheI.rowSpan = 4;
    coucheI.controls = {
        control("I Shape", "WAVE", S::Knob, 0, 0),
        control("I Pulse Width", "PW", S::Knob, 1, 0),
        control("I Detune", "TUNE", S::Knob, 2, 0),
        control("I High Pass", "HPF", S::Knob, 3, 0),
        control("I Cutoff", "LPF", S::LargeKnob, 0, 1),
        control("I Resonance", "RES", S::Knob, 1, 1),
        control("I Env Amount", "ENV", S::Knob, 2, 1),
        control("I Level", "LEVEL", S::Knob, 3, 1),
    };

    PanelSection melange;
    melange.title = "MIX";
    melange.accentColour = "#E5C87F";
    melange.column = 6; melange.row = 0; melange.columnSpan = 2; melange.rowSpan = 4;
    melange.controls = {
        control("Layer Mix", "I / II", S::LargeKnob, 0, 0),
        control("Output Level", "VOLUME", S::Knob, 0, 1),
    };

    PanelSection coucheII;
    coucheII.title = "CHANNEL II";
    coucheII.accentColour = "#C9A961";
    coucheII.column = 8; coucheII.row = 0; coucheII.columnSpan = 6; coucheII.rowSpan = 4;
    coucheII.controls = {
        control("II Shape", "WAVE", S::Knob, 0, 0),
        control("II Pulse Width", "PW", S::Knob, 1, 0),
        control("II Detune", "TUNE", S::Knob, 2, 0),
        control("II High Pass", "HPF", S::Knob, 3, 0),
        control("II Cutoff", "LPF", S::LargeKnob, 0, 1),
        control("II Resonance", "RES", S::Knob, 1, 1),
        control("II Env Amount", "ENV", S::Knob, 2, 1),
        control("II Level", "LEVEL", S::Knob, 3, 1),
    };

    PanelSection toucher;
    toucher.title = "TOUCH";
    toucher.accentColour = "#D98F5A";
    toucher.column = 14; toucher.row = 0; toucher.columnSpan = 3; toucher.rowSpan = 4;
    toucher.controls = {
        control("Pressure to Cutoff", "AFT>LPF", S::LargeKnob, 0, 0),
        control("Pressure to Level", "AFT>VOL", S::Knob, 1, 0),
        control("Velocity to Cutoff", "VEL>LPF", S::Knob, 0, 1),
        control("Velocity to Level", "VEL>VOL", S::Knob, 1, 1),
    };

    PanelSection envs;
    envs.title = "ENVELOPES";
    envs.accentColour = "#8FA9C9";
    envs.column = 17; envs.row = 0; envs.columnSpan = 3; envs.rowSpan = 4;
    envs.controls = {
        control("I Amp Attack", "IA", S::Knob, 0, 0),
        control("I Amp Release", "IR", S::Knob, 1, 0),
        control("II Amp Attack", "IIA", S::Knob, 2, 0),
        control("II Amp Release", "IIR", S::Knob, 0, 1),
        control("Filter Attack", "FA", S::Knob, 1, 1),
        control("Filter Release", "FR", S::Knob, 2, 1),
    };
    panel.omittedParameters = {
        {"I Amp Decay", "les temps intermédiaires des deux couches : réglés une fois "
                        "pour la nappe, ils ne se jouent pas"},
        {"I Amp Sustain", "voir I Amp Decay"},
        {"II Amp Decay", "voir I Amp Decay"},
        {"II Amp Sustain", "voir I Amp Decay"},
        {"Filter Decay", "voir I Amp Decay"},
        {"Filter Sustain", "voir I Amp Decay"},
        {"Analog Character", "instabilité d'intonation très lente, commune au parc : "
                             "elle se règle une fois et ne se joue pas"},
    };

    panel.sections = {coucheI, melange, coucheII, toucher, envs};
    return panel;
}

/// `vsm.modal` -- l'objet frappé. La façade suit l'OBJET, pas le spectre :
/// à gauche ce qu'il EST (matériau, modes, étirement), au centre comment il
/// s'éteint, à droite comment on le frappe. Livrée bronze sombre : celle
/// d'un jeu de lames.
MachinePanel makeModal() {
    MachinePanel panel;
    panel.pluginId = "vsm.modal";
    panel.displayName = "Modal (l'objet frappé)";
    panel.chassis = Chassis::Metal;
    panel.panelColour = "#1E1B16";
    panel.sectionColour = "#15120D";
    panel.textColour = "#EFE6D2";
    panel.knobColour = "#B98F4E";
    panel.gridColumns = 12;
    panel.gridRows = 4;

    PanelSection objet;
    objet.title = "OBJECT";
    objet.accentColour = "#B98F4E";
    objet.column = 0; objet.row = 0; objet.columnSpan = 4; objet.rowSpan = 4;
    objet.controls = {
        control("Material", "MATERIAL", S::LargeKnob, 0, 0),
        control("Spread", "SPREAD", S::Knob, 1, 0),
        control("Modes", "MODES", S::Knob, 0, 1),
    };

    PanelSection extinction;
    extinction.title = "DECAY";
    extinction.accentColour = "#D9B36A";
    extinction.column = 4; extinction.row = 0; extinction.columnSpan = 4; extinction.rowSpan = 4;
    extinction.controls = {
        control("Decay", "TIME", S::LargeKnob, 0, 0),
        control("Decay Tilt", "TILT", S::Knob, 1, 0),
    };

    PanelSection maillet;
    maillet.title = "MALLET";
    maillet.accentColour = "#C97F5A";
    maillet.column = 8; maillet.row = 0; maillet.columnSpan = 4; maillet.rowSpan = 4;
    maillet.controls = {
        control("Mallet Hardness", "HARDNESS", S::Knob, 0, 0),
        control("Strike Position", "POSITION", S::Knob, 1, 0),
        control("Velocity to Hardness", "VEL", S::Knob, 0, 1),
        control("Output Level", "VOLUME", S::Knob, 1, 1),
    };

    panel.sections = {objet, extinction, maillet};
    return panel;
}

const std::vector<MachinePanel>& panels() {
    static const std::vector<MachinePanel> all = {
        makeMinimoog(), makeTb303(), makeTr808(), makeTr909(), makeSh101(),
        makeJuno106(), makeJupiter8(), makeProphet(), makeMs20(), makeArpOdyssey(), makeDx7(), makeSampler(),
        makeEPiano(), makeObx(), makeSupersaw(), makeWavetable(), makePcmHybrid(), makeTonewheel(), makeGeneric(), makeString(),
        makePiano(), makeDrums(), makeWind(), makeMultisample(),
        makePerc(), makeAdditive(), makeWestCoast(), makeFmDrums(), makeVocal(), makePhaseDist(), makeDivider(), makePsg(), makeStochastic(),
        makeCone(), makeVector(), makeGranular(), makeCs80(), makeModal()
    };
    return all;
}

} // namespace

const MachinePanel* findMachinePanel(const std::string& pluginId) {
    for (const auto& panel : panels())
        if (panel.pluginId == pluginId) return &panel;
    return nullptr;
}

std::vector<std::string> machinePanelIds() {
    std::vector<std::string> ids;
    ids.reserve(panels().size());
    for (const auto& panel : panels()) ids.push_back(panel.pluginId);
    return ids;
}

std::vector<PanelControl> allControls(const MachinePanel& panel) {
    std::vector<PanelControl> controls;
    for (const auto& section : panel.sections)
        controls.insert(controls.end(), section.controls.begin(), section.controls.end());
    return controls;
}

} // namespace vsm::panels
