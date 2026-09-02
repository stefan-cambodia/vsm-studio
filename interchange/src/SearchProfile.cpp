#include "vsm/interchange/SearchProfile.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vsm::interchange {

namespace {

/// Règle appliquée à une FAMILLE de paramètres, reconnue à la FORME
/// CANONIQUE de son identité sémantique : les segments purement numériques y
/// sont remplacés par « # ». `filter.1.cutoff` et `filter.2.cutoff` partagent
/// donc la forme `filter.#.cutoff` et la même règle, quelle que soit la
/// machine -- c'est tout l'intérêt d'avoir des identités sémantiques.
///
/// Une première version filtrait sur le dernier segment de l'identifiant.
/// Elle ratait les familles dont le numéro est EN DERNIER : `organ.drawbar.1`
/// se terminait par « 1 », si bien que les neuf tirettes de l'orgue -- tout
/// son timbre -- n'entraient pas dans l'espace de recherche.
struct Rule {
    const char* pattern;     ///< forme canonique exacte, ex. « filter.#.cutoff »
    float low, high;         ///< bornes utiles ; 0/0 = reprendre celles du paramètre
    SearchScale scale;
    float importance;
};

/// Numéro d'INSTANCE d'un identifiant : le premier segment numérique.
/// `envelope.2.attack` -> 2, `filter.1.cutoff` -> 1, `voice.unison` -> 1.
int instanceOf(const std::string& semanticId) {
    size_t begin = 0;
    while (begin <= semanticId.size()) {
        size_t end = semanticId.find('.', begin);
        if (end == std::string::npos) end = semanticId.size();
        bool numeric = end > begin;
        for (size_t i = begin; i < end; ++i)
            if (semanticId[i] < '0' || semanticId[i] > '9') { numeric = false; break; }
        if (numeric) return std::atoi(semanticId.substr(begin, end - begin).c_str());
        if (end == semanticId.size()) break;
        begin = end + 1;
    }
    return 1;
}

/// Forme canonique d'un identifiant sémantique : segments numériques -> « # ».
std::string canonicalise(const std::string& semanticId) {
    std::string out;
    out.reserve(semanticId.size());
    size_t begin = 0;
    while (begin <= semanticId.size()) {
        size_t end = semanticId.find('.', begin);
        if (end == std::string::npos) end = semanticId.size();
        const std::string segment = semanticId.substr(begin, end - begin);
        bool numeric = !segment.empty();
        for (char c : segment)
            if (c < '0' || c > '9') { numeric = false; break; }
        if (!out.empty()) out.push_back('.');
        out += numeric ? "#" : segment;
        if (end == semanticId.size()) break;
        begin = end + 1;
    }
    return out;
}

/// TABLE DES RÈGLES.
///
/// Les bornes sont des bornes UTILES : elles disent où il vaut la peine de
/// chercher, pas ce que la machine accepte. Elles sont ensuite rognées aux
/// bornes réelles du paramètre, si bien qu'une machine dont la coupure
/// s'arrête à 8 kHz ne se verra jamais proposer 12 kHz.
///
/// `0, 0` signifie « reprendre les bornes réelles du paramètre ».
///
/// RÈGLE POUR CHOISIR : une fenêtre utile fixe n'est légitime que pour une
/// grandeur à UNITÉ ABSOLUE, partagée par toutes les machines -- des hertz,
/// des secondes. 900 Hz veut dire la même chose partout, et chercher au-delà
/// de 12 kHz ne sert nulle part.
///
/// Pour tout le reste, c'est la plage DÉCLARÉE qui fait foi. La résonance en
/// est l'exemple, et il a coûté une régression mesurée : elle va de 0 à 4,2
/// sur le Minimoog et de 0 à 1 sur la TB-303. Une fenêtre fixe à 0..0,8
/// rendait donc INATTEIGNABLE la valeur 2,2 d'une cible Minimoog ; la
/// recherche compensait en faussant la coupure (1190 Hz pour 900 visés) et
/// l'enveloppe, et la distance finale passait de 0,51 à 1,30. Une mauvaise
/// borne est pire que pas de dimension du tout.
///
/// Les importances suivent une hiérarchie défendable : ce qui détermine le
/// TIMBRE d'abord, puis l'ENVELOPPE (ce qui sépare un pincement d'une nappe),
/// puis les modulations, enfin le détail. Elles servent à choisir les N
/// dimensions à chercher quand le budget est court -- ce ne sont PAS des
/// poids dans la distance, qui se mesure sur le son et sur rien d'autre.
constexpr Rule kRules[] = {
    // --- Ce qui fait le timbre, par ordre d'effet ----------------------------
    {"filter.#.cutoff",                   80.0f, 12000.0f, SearchScale::Logarithmic, 1.00f},
    {"organ.drawbar.#",                    0.0f,     0.0f, SearchScale::Linear,      0.95f},
    {"oscillator.#.wavePosition",          0.0f,     1.0f, SearchScale::Linear,      0.95f},
    {"oscillator.supersaw.detune",         0.0f,     1.0f, SearchScale::Linear,      0.92f},
    {"fm.algorithm",                       0.0f,     0.0f, SearchScale::Linear,      0.90f},
    {"fm.operator.#.ratio",                0.0f,     0.0f, SearchScale::Linear,      0.88f},
    {"fm.operator.#.level",                0.0f,     1.0f, SearchScale::Linear,      0.82f},
    {"sample.#.select",                    0.0f,     0.0f, SearchScale::Linear,      0.85f},
    {"filter.#.resonance",                 0.0f,     0.0f, SearchScale::Linear,      0.80f}, // plage déclarée : 0..1 ici, 0..4,2 là
    {"filter.#.envAmount",                -1.0f,     1.0f, SearchScale::Linear,      0.78f},
    {"oscillator.supersaw.mix",            0.0f,     1.0f, SearchScale::Linear,      0.76f},
    {"epiano.bellLevel",                   0.0f,     1.0f, SearchScale::Linear,      0.76f},
    {"epiano.tineDecay",                   0.0f,     0.0f, SearchScale::Linear,      0.76f},
    {"drum.kick.decay",                    0.0f,     0.0f, SearchScale::Linear,      0.75f},
    {"drum.snare.decay",                   0.0f,     0.0f, SearchScale::Linear,      0.75f},
    {"drum.tom.decay",                     0.0f,     0.0f, SearchScale::Linear,      0.75f},
    {"drum.clap.decay",                    0.0f,     0.0f, SearchScale::Linear,      0.72f},
    {"drum.closedHat.decay",               0.0f,     0.0f, SearchScale::Linear,      0.72f},
    {"drum.openHat.decay",                 0.0f,     0.0f, SearchScale::Linear,      0.72f},
    {"drum.crash.decay",                   0.0f,     0.0f, SearchScale::Linear,      0.72f},
    {"oscillator.#.wavetable",             0.0f,     0.0f, SearchScale::Linear,      0.72f},
    // Famille de la CORDE (vsm.string). L'ordre n'est pas celui d'un
    // soustractif : sur une corde, ce qui fait le timbre est d'abord ce qui
    // se PERD à chaque aller-retour (l'amortissement, puis la durée), ensuite
    // seulement la manière dont on l'excite. L'amortissement y joue le rôle
    // que la coupure joue ailleurs — c'est le seul réglage qui change la
    // couleur du son tenu — d'où une importance du même ordre.
    {"string.damping",                     0.0f,     1.0f, SearchScale::Linear,      0.98f},
    {"string.decay",                       0.15f,   12.0f, SearchScale::Logarithmic, 0.90f},
    {"string.excitation",                  0.0f,     1.0f, SearchScale::Linear,      0.88f},
    {"string.pickPosition",                0.0f,     0.0f, SearchScale::Linear,      0.82f},
    {"string.pickHardness",                0.0f,     1.0f, SearchScale::Linear,      0.74f},
    {"string.bodyLevel",                   0.0f,     1.0f, SearchScale::Linear,      0.70f},
    {"string.stiffness",                   0.0f,     1.0f, SearchScale::Linear,      0.62f},
    {"string.bodySize",                    0.0f,     1.0f, SearchScale::Linear,      0.58f},
    {"string.bowPressure",                 0.0f,     1.0f, SearchScale::Linear,      0.55f},
    {"string.bowSpeed",                    0.0f,     1.0f, SearchScale::Linear,      0.45f},
    // WAVESHAPING (vsm.chebyshev). L'index EST le timbre de cette famille :
    // il décide à lui seul de la brillance, sans qu'aucun filtre n'existe
    // sur la machine. Les poids de rang viennent après, et leur importance
    // décroît comme leur audibilité.
    {"waveshaper.index",                   0.0f,     1.0f, SearchScale::Linear,      0.95f},
    {"waveshaper.velocityToIndex",         0.0f,     1.0f, SearchScale::Linear,      0.50f},
    // OBJET FRAPPÉ (vsm.modal). Ce qui fait reconnaître un objet est d'abord
    // la POSITION de ses partiels -- une barre ne sonne pas comme une corde,
    // et aucun autre réglage ne rattrape cela ; vient ensuite la façon dont
    // ses modes hauts s'éteignent (le bois contre le métal), puis la durée,
    // puis le maillet. Le nombre de modes ferme la marche : au-delà d'une
    // douzaine, l'oreille n'entend plus la différence, et une recherche qui
    // le balaierait en priorité perdrait son budget.
    {"modal.material",                     0.0f,     1.0f, SearchScale::Linear,      0.97f},
    {"modal.decayTilt",                    0.0f,     3.0f, SearchScale::Linear,      0.88f},
    {"modal.decay",                        0.05f,   12.0f, SearchScale::Logarithmic, 0.84f},
    {"modal.spread",                       0.5f,     2.0f, SearchScale::Linear,      0.62f},
    {"modal.modeCount",                    1.0f,    24.0f, SearchScale::Linear,      0.40f},
    // SYNTHÈSE BALAYÉE (vsm.scanned). L'AMORTISSEMENT commande d'abord, et
    // de très loin : c'est lui qui décide si le timbre voyage ou se fige, et
    // une chaîne figée rend cette machine indiscernable d'une table d'ondes
    // ordinaire -- toute la famille tient là. La tension vient ensuite,
    // parce qu'elle règle la VITESSE du voyage ; le rappel ferme la marche
    // des trois réglages de la chaîne, son effet étant surtout d'écourter le
    // mouvement. La borne haute de l'amortissement est tenue à 0,3 : au-delà,
    // la chaîne passe sous le seuil d'entretien et s'éteint pour de bon, et
    // une recherche qui balaierait cette zone y perdrait ses candidats.
    {"scanned.damping",                    0.0f,     0.3f, SearchScale::Linear,      0.96f},
    {"scanned.tension",                    0.02f,    1.0f, SearchScale::Logarithmic, 0.88f},
    {"scanned.centering",                  0.0f,     0.5f, SearchScale::Linear,      0.64f},
    // LECTURE DE BANDE (vsm.mellotron). La LONGUEUR DE BANDE commande, et
    // d'une façon qu'aucun autre réglage du parc ne partage : elle ne change
    // pas le timbre, elle décide COMBIEN DE TEMPS une note existe. Une nappe
    // tenue de six secondes et une bande de trois, et la moitié de la phrase
    // disparaît -- aucun ajustement de filtre ne rattrape cela, donc c'est le
    // premier axe à balayer. Le pleurage vient ensuite : c'est lui qui donne
    // le grain, et il s'entend tout de suite. Le rembobinage ferme la marche,
    // son effet ne se voyant que sur les reprises rapprochées.
    {"tape.length",                        1.0f,    20.0f, SearchScale::Logarithmic, 0.97f},
    {"tape.wowDepth",                      0.0f,    45.0f, SearchScale::Linear,      0.80f},
    {"tape.hiss",                          0.0f,     0.6f, SearchScale::Linear,      0.58f},
    {"tape.wowRate",                       0.1f,     3.0f, SearchScale::Logarithmic, 0.50f},
    {"tape.rewindTime",                    0.1f,     4.0f, SearchScale::Logarithmic, 0.34f},
    // CORDES SYMPATHIQUES (vsm.sitar). Le NIVEAU des sympathiques commande :
    // c'est lui qui décide si l'instrument traîne une queue de résonance ou
    // sonne comme une corde ordinaire, et l'écart entre les deux est le plus
    // audible de la machine. Vient ensuite le JAWARI, qui fait tout le grain
    // du timbre (×30 sur la bande 5–12 kHz d'un extrême à l'autre). La
    // TONIQUE compte moins qu'on ne croirait à l'oreille mais beaucoup pour
    // une recherche, puisque c'est elle qui décide quelles notes du morceau
    // trouveront une corde : elle est cherchée en LINÉAIRE sur les demi-tons,
    // et non en logarithmique, parce que c'est un numéro de note.
    {"sympathetic.level",                  0.0f,     1.0f, SearchScale::Linear,      0.94f},
    {"string.bridgeBuzz",                  0.0f,     1.0f, SearchScale::Linear,      0.86f},
    {"sympathetic.root",                  28.0f,    64.0f, SearchScale::Linear,      0.78f},
    {"sympathetic.decay",                  1.0f,    20.0f, SearchScale::Logarithmic, 0.60f},
    {"sympathetic.count",                  1.0f,    13.0f, SearchScale::Linear,      0.36f},
    // PEAU TENDUE (vsm.membrane). La CHARGE commande, et de loin : c'est elle
    // qui décide si l'objet est inharmonique (une timbale) ou accordé (un
    // tabla), et rien d'autre ne rattrape ce choix — deux instruments
    // différents vivent aux deux bouts de sa course. Le RAYON de frappe vient
    // juste après, parce qu'il fait disparaître des familles entières de modes
    // d'un coup : frappée au centre, la peau perd TOUS ses modes diamétraux.
    {"membrane.loading",                   0.0f,     1.0f, SearchScale::Linear,      0.96f},
    {"membrane.strikeRadius",              0.0f,    0.95f, SearchScale::Linear,      0.90f},
    // Famille du VENT (vsm.wind). Comme sur la corde, ce qui fait le timbre
    // est d'abord ce qui se perd (le rayonnement au pavillon) et la raideur de
    // ce qui entretient (l'anche ou les lèvres) ; la pression de souffle vient
    // juste après, parce qu'elle change le spectre autant que le niveau.
    {"wind.bellDamping",                   0.0f,     1.0f, SearchScale::Linear,      0.96f},
    {"wind.reedStiffness",                 0.0f,     1.0f, SearchScale::Linear,      0.92f},
    {"wind.breathPressure",                0.1f,     1.0f, SearchScale::Linear,      0.86f},
    {"wind.brassiness",                    0.0f,     1.0f, SearchScale::Linear,      0.72f},
    {"wind.breathNoise",                   0.0f,     1.0f, SearchScale::Linear,      0.52f},
    // ANCHE LIBRE (vsm.reed). La charge d'air est le seul réglage du parc qui
    // déplace la HAUTEUR d'une note tenue : une recherche qui la balaie change
    // l'accord, ce qui pèse lourd sur une distance. Elle vient donc juste
    // après la pression et la raideur, que la machine partage avec le vent.
    {"reed.airLoading",                    0.0f,     1.0f, SearchScale::Linear,      0.74f},
    // PLAQUE (vsm.plate). Le COUPLAGE commande sans partage : à zéro, la
    // machine est une banque de modes ordinaire qui s'assombrit ; au-delà,
    // elle s'éclaircit en durant, ce qu'aucune autre ne fait. L'écart entre
    // les deux bouts de sa course est le plus grand du parc (brillance ×130
    // mesurée à frappe forte), donc c'est le premier axe à balayer.
    {"plate.coupling",                     0.0f,     1.0f, SearchScale::Linear,      0.97f},
    // Percussions modélisées : la pièce compte AUTANT que les pièces, parce
    // que c'est elle qui sépare le plus sûrement un kit acoustique d'un kit
    // électronique.
    {"drum.room.level",                    0.0f,     1.0f, SearchScale::Linear,      0.72f},
    {"drum.room.size",                     0.0f,     1.0f, SearchScale::Linear,      0.60f},
    {"drum.ride.decay",                    0.0f,     0.0f, SearchScale::Linear,      0.72f},
    {"drum.ride.level",                    0.0f,     1.0f, SearchScale::Linear,      0.64f},
    // PERCUSSIONS À PEAUX ET BARRES (vsm.perc). L'ordre suit celui des autres
    // percussions, avec une exception assumée : sur un conga ou un bongo, la
    // TENSION de la peau pèse plus que sa durée. Un tambour trop grave ne se
    // rattrape pas en le raccourcissant, alors qu'un kick de boîte à rythmes,
    // dont la hauteur est balayée à l'attaque, se reconnaît d'abord à sa
    // queue. La barre (bois, claves) suit la même règle : sa hauteur EST son
    // identité, sa durée n'est qu'une nuance de frappe.
    // SYNTHÈSE STOCHASTIQUE (vsm.stochastic). Le nombre de POINTS vient en
    // tête : c'est lui qui décide combien d'angles la forme porte, donc la
    // richesse du spectre -- le rôle que la coupure tient ailleurs. La
    // divagation de forme suit, parce qu'elle fait la TEXTURE, qui est la
    // raison d'être de cette machine. Le verrou de hauteur vient loin derrière
    // dans la recherche : sur un stem accordé, il vaut toujours un.
    {"stochastic.breakpoints",             2.0f,    16.0f, SearchScale::Linear,      0.92f},
    {"stochastic.shapeWander",             0.0f,     0.5f, SearchScale::Linear,      0.86f},
    {"stochastic.timeWander",              0.0f,     0.5f, SearchScale::Linear,      0.62f},
    {"stochastic.pitchLock",               0.0f,     1.0f, SearchScale::Linear,      0.40f},
    // PUCE 8 BITS (vsm.psg). L'HORLOGE en tête, et ce n'est pas un réglage
    // d'accordage : elle décide de la GRILLE des fréquences atteignables, donc
    // du désaccord caractéristique. Les bits de volume suivent -- ils font la
    // granularité de la dynamique, ce qui s'entend autant que le timbre. Le
    // rapport cyclique vient ensuite, puis le bruit, qui n'est là que sur une
    // partie du répertoire.
    {"psg.clock",                          0.0f,     0.0f, SearchScale::Logarithmic, 0.92f},
    {"psg.volumeBits",                     1.0f,     8.0f, SearchScale::Linear,      0.80f},
    {"psg.squareVoices",                   1.0f,     3.0f, SearchScale::Linear,      0.66f},
    {"psg.noisePeriod",                    0.0f,     0.0f, SearchScale::Logarithmic, 0.58f},
    // CORDES ÉLECTRONIQUES (vsm.divider). L'ENSEMBLE vient en tête, et c'est
    // propre à cette machine : sans son chorus, elle n'est qu'un orgue pauvre,
    // et c'est lui qu'on reconnaît avant tout le reste. Les deux registres
    // suivent -- ils font l'équilibre grave/aigu, ce qu'une coupure fait
    // ailleurs. Le filtre global vient après : il n'a qu'un pôle de caractère,
    // pas de résonance, et ne change pas la nature du son.
    {"effect.chorus.depth",                0.0f,     1.0f, SearchScale::Linear,      0.94f},
    {"organ.drawbar.1",                    0.0f,     1.0f, SearchScale::Linear,      0.82f},
    {"organ.drawbar.2",                    0.0f,     1.0f, SearchScale::Linear,      0.82f},
    // DISTORSION DE PHASE (vsm.phasedist). La DÉFORMATION est à cette machine
    // ce que la coupure est à un soustractif : elle décide combien
    // d'harmoniques existent, et elle vient donc en tête, suivie de ce qui la
    // pilote dans le temps. La résonance vient après parce qu'elle ne sert que
    // sur une partie du répertoire de la machine ; son RANG, lui, est discret
    // par nature -- il saute d'un entier à l'autre -- donc moins rentable à
    // chercher finement qu'un réglage continu.
    {"phasedist.amount",                   0.0f,     1.0f, SearchScale::Linear,      0.96f},
    {"phasedist.envAmount",                0.0f,     1.0f, SearchScale::Linear,      0.86f},
    {"phasedist.resonance",                0.0f,     1.0f, SearchScale::Linear,      0.72f},
    {"phasedist.resonanceHarmonic",        1.0f,    16.0f, SearchScale::Linear,      0.64f},
    // VOIX (vsm.vocal). La VOYELLE d'abord, et de loin : c'est elle qui décide
    // de quel son il s'agit, là où les autres machines ont une coupure. Le
    // décalage du conduit suit -- il change la TAILLE du chanteur, ce qui
    // s'entend immédiatement. La tension et le souffle colorent la source sans
    // toucher aux formants : ils viennent après, et c'est la hiérarchie du
    // modèle source-filtre lui-même.
    {"vocal.vowel",                        0.0f,     4.0f, SearchScale::Linear,      0.98f},
    {"vocal.formantShift",               -12.0f,    12.0f, SearchScale::Linear,      0.88f},
    {"vocal.tension",                      0.0f,     1.0f, SearchScale::Linear,      0.66f},
    {"vocal.breath",                       0.0f,     1.0f, SearchScale::Linear,      0.60f},
    // PERCUSSIONS FM (vsm.fmdrums). Le RAPPORT vient en tête, et devant la
    // durée : c'est lui qui décide si la pièce est harmonique ou métallique,
    // c'est-à-dire de quelle FAMILLE de son il s'agit. L'indice suit -- il dose
    // ce que le rapport a rendu possible. Sur les autres boîtes, l'ordre est
    // l'inverse (la durée d'abord), parce qu'aucune n'a de rapport à régler.
    {"drum.kick.fmRatio",                  0.0f,     0.0f, SearchScale::Linear,      0.88f},
    {"drum.snare.fmRatio",                 0.0f,     0.0f, SearchScale::Linear,      0.84f},
    {"drum.tom.fmRatio",                   0.0f,     0.0f, SearchScale::Linear,      0.80f},
    {"drum.cowbell.fmRatio",               0.0f,     0.0f, SearchScale::Linear,      0.76f},
    {"drum.kick.fmIndex",                  0.0f,     0.0f, SearchScale::Linear,      0.78f},
    {"drum.snare.fmIndex",                 0.0f,     0.0f, SearchScale::Linear,      0.74f},
    {"drum.tom.fmIndex",                   0.0f,     0.0f, SearchScale::Linear,      0.72f},
    {"drum.cowbell.fmIndex",               0.0f,     0.0f, SearchScale::Linear,      0.70f},
    {"drum.closedHat.tone",                0.0f,     0.0f, SearchScale::Logarithmic, 0.68f},
    // SYNTHÈSE « CÔTE OUEST » (vsm.westcoast). Le pliage est à cette machine ce
    // que la coupure est à un soustractif : c'est LUI qui décide combien
    // d'harmoniques existent, et il vient donc en tête. La symétrie suit de
    // près -- elle fait apparaître les rangs pairs, c'est-à-dire qu'elle change
    // la NATURE du spectre et pas seulement sa richesse. La lenteur de la porte
    // vient loin derrière : elle colore l'extinction, elle ne fait pas le son.
    {"westcoast.fold",                     0.0f,     1.0f, SearchScale::Linear,      0.98f},
    {"westcoast.foldSymmetry",             0.0f,     1.0f, SearchScale::Linear,      0.84f},
    {"westcoast.gateLag",                  0.0f,     0.0f, SearchScale::Logarithmic, 0.56f},
    // SYNTHÈSE ADDITIVE (vsm.additive). L'ordre n'est pas celui d'un
    // soustractif, et pour une raison de fond : il n'y a pas de coupure ici.
    // Ce qui fait le timbre est d'abord la PENTE du spectre -- elle joue le
    // rôle que la coupure joue ailleurs, et sur toute la course --, puis la
    // balance impairs/pairs, qui bascule d'un son creux à un son plein sans
    // rien changer d'autre. La raideur vient après : elle colore, elle ne
    // décide pas. Le nombre de rangs est le plus grossier des quatre, mais il
    // est DISCRET, donc peu rentable à chercher finement.
    {"additive.spectralTilt",            -18.0f,     0.0f, SearchScale::Linear,      0.96f},
    {"additive.oddEvenBalance",            0.0f,     1.0f, SearchScale::Linear,      0.90f},
    {"additive.decayTilt",                 0.0f,     1.0f, SearchScale::Linear,      0.78f},
    {"additive.inharmonicity",             0.0f,     1.0f, SearchScale::Linear,      0.74f},
    {"additive.partialCount",              1.0f,    32.0f, SearchScale::Linear,      0.68f},
    {"additive.attackSpread",              0.0f,     1.0f, SearchScale::Linear,      0.52f},
    {"drum.conga.tune",                    0.0f,     0.0f, SearchScale::Linear,      0.76f},
    {"drum.bongo.tune",                    0.0f,     0.0f, SearchScale::Linear,      0.74f},
    {"drum.timbale.tune",                  0.0f,     0.0f, SearchScale::Linear,      0.74f},
    {"drum.woodblock.tune",                0.0f,     0.0f, SearchScale::Linear,      0.72f},
    {"drum.conga.decay",                   0.0f,     0.0f, SearchScale::Linear,      0.70f},
    {"drum.bongo.decay",                   0.0f,     0.0f, SearchScale::Linear,      0.68f},
    {"drum.timbale.decay",                 0.0f,     0.0f, SearchScale::Linear,      0.68f},
    {"drum.woodblock.decay",               0.0f,     0.0f, SearchScale::Linear,      0.66f},
    // Le shaker n'a pas de hauteur : ce qui le décrit est la BANDE de son
    // bruit, et elle joue ici le rôle qu'une coupure joue ailleurs.
    {"drum.shaker.tone",                   0.0f,     0.0f, SearchScale::Linear,      0.70f},
    {"drum.shaker.decay",                  0.0f,     0.0f, SearchScale::Linear,      0.64f},
    {"drum.tambourine.decay",              0.0f,     0.0f, SearchScale::Linear,      0.62f},
    {"drum.conga.level",                   0.0f,     1.0f, SearchScale::Linear,      0.66f},
    {"drum.bongo.level",                   0.0f,     1.0f, SearchScale::Linear,      0.64f},
    {"drum.timbale.level",                 0.0f,     1.0f, SearchScale::Linear,      0.64f},
    {"drum.woodblock.level",               0.0f,     1.0f, SearchScale::Linear,      0.62f},
    {"drum.shaker.level",                  0.0f,     1.0f, SearchScale::Linear,      0.60f},
    {"drum.tambourine.level",              0.0f,     1.0f, SearchScale::Linear,      0.58f},
    // Familles propres à la machine NEUTRE. Elles sont CONTINUES par
    // construction, ce qui les rend particulièrement rentables à chercher :
    // une forme d'onde morphable explore tout le passage sinus-carré sans le
    // moindre palier, là où un sélecteur discret bloquerait la descente.
    {"oscillator.#.shape",                 0.0f,     3.0f, SearchScale::Linear,      0.92f},
    {"filter.#.type",                      0.0f,     2.0f, SearchScale::Linear,      0.86f},
    {"output.drive",                       0.0f,     1.0f, SearchScale::Linear,      0.62f},
    {"oscillator.noise.colour",            0.0f,     1.0f, SearchScale::Linear,      0.45f},
    {"oscillator.sub.shape",               0.0f,     1.0f, SearchScale::Linear,      0.42f},
    {"oscillator.#.octave",                0.0f,     0.0f, SearchScale::Linear,      0.46f},
    {"lfo.#.toAmp",                        0.0f,     1.0f, SearchScale::Linear,      0.22f},
    {"oscillator.#.waveform",              0.0f,     0.0f, SearchScale::Linear,      0.70f},
    {"voice.structure",                    0.0f,     0.0f, SearchScale::Linear,      0.70f},
    {"epiano.hammerHardness",              0.0f,     1.0f, SearchScale::Linear,      0.70f},
    {"sample.#.tone",                      0.0f,     1.0f, SearchScale::Linear,      0.70f},
    {"drum.kick.tune",                     0.0f,     0.0f, SearchScale::Linear,      0.70f},
    {"drum.snare.tune",                    0.0f,     0.0f, SearchScale::Linear,      0.70f},
    {"drum.tom.tune",                      0.0f,     0.0f, SearchScale::Linear,      0.70f},
    {"drum.cowbell.tune",                  0.0f,     0.0f, SearchScale::Linear,      0.66f},
    {"drum.kick.level",                    0.0f,     1.0f, SearchScale::Linear,      0.68f},
    {"drum.snare.level",                   0.0f,     1.0f, SearchScale::Linear,      0.68f},
    {"drum.tom.level",                     0.0f,     1.0f, SearchScale::Linear,      0.66f},
    {"drum.clap.level",                    0.0f,     1.0f, SearchScale::Linear,      0.66f},
    {"drum.closedHat.level",               0.0f,     1.0f, SearchScale::Linear,      0.66f},
    {"drum.openHat.level",                 0.0f,     1.0f, SearchScale::Linear,      0.66f},
    {"drum.crash.level",                   0.0f,     1.0f, SearchScale::Linear,      0.64f},
    {"drum.cowbell.level",                 0.0f,     1.0f, SearchScale::Linear,      0.64f},
    {"drum.snare.snappy",                  0.0f,     1.0f, SearchScale::Linear,      0.62f},
    {"drum.kick.attack",                   0.0f,     1.0f, SearchScale::Linear,      0.60f},
    {"oscillator.#.level",                 0.0f,     1.0f, SearchScale::Linear,      0.65f},
    {"oscillator.#.sawLevel",              0.0f,     1.0f, SearchScale::Linear,      0.65f},
    {"oscillator.#.pulseLevel",            0.0f,     1.0f, SearchScale::Linear,      0.65f},
    {"oscillator.#.subLevel",              0.0f,     1.0f, SearchScale::Linear,      0.60f},
    {"epiano.pickupDrive",                 0.0f,     1.0f, SearchScale::Linear,      0.65f},
    {"sample.#.level",                     0.0f,     1.0f, SearchScale::Linear,      0.64f},
    {"sample.#.decay",                     0.0f,     0.0f, SearchScale::Logarithmic, 0.60f},
    {"sample.#.tune",                      0.0f,     0.0f, SearchScale::Linear,      0.55f},
    // Les emplacements du sampler SE CHERCHENT, contrairement à leur mapping :
    // une fois le coup découpé et posé, son accord et sa décroissance sont
    // exactement ce qui reste à ajuster pour coller à la cible.
    {"sampler.slot.#.decay",               0.0f,     0.0f, SearchScale::Linear,      0.70f},
    {"sampler.slot.#.tune",                0.0f,     0.0f, SearchScale::Linear,      0.66f},
    {"sampler.slot.#.level",               0.0f,     1.0f, SearchScale::Linear,      0.60f},
    {"epiano.hammerNoise",                 0.0f,     1.0f, SearchScale::Linear,      0.58f},
    {"oscillator.sub.level",               0.0f,     1.0f, SearchScale::Linear,      0.58f},
    {"oscillator.sub.type",                0.0f,     0.0f, SearchScale::Linear,      0.45f},
    {"oscillator.noise.level",             0.0f,     1.0f, SearchScale::Linear,      0.55f},
    {"oscillator.#.pulseWidth",            0.1f,     0.9f, SearchScale::Linear,      0.55f},
    {"tone.bass",                          0.0f,     0.0f, SearchScale::Linear,      0.55f},
    {"tone.treble",                        0.0f,     0.0f, SearchScale::Linear,      0.55f},
    // Coupe-bas CORRECTEUR (Juno-106, Jupiter-8, ARP, supersaw). Longtemps
    // écrit « filter.2.cutoff », il héritait de l'importance du filtre
    // principal (1,00 moins la décote d'instance, soit 0,88) et occupait le
    // rang 3 de l'espace cherché du Juno-106 -- pour une commande qui, sur la
    // machine d'origine, est un simple sélecteur de graves. Le TYPE dans
    // l'identifiant permet enfin de le classer pour ce qu'il est. Le HPF
    // résonant du MS-20 reste « filter.2.cutoff » et garde son rang de vrai
    // filtre.
    {"filter.hp.cutoff",                   0.0f,     0.0f, SearchScale::Logarithmic, 0.52f},
    {"filter.#.drive",                     0.0f,     0.0f, SearchScale::Linear,      0.52f},
    {"filter.#.slope",                     0.0f,     0.0f, SearchScale::Linear,      0.50f},
    {"oscillator.crossMod",                0.0f,     0.0f, SearchScale::Linear,      0.50f},
    {"oscillator.ringMod",                 0.0f,     0.0f, SearchScale::Linear,      0.50f},
    {"oscillator.#.sync",                  0.0f,     0.0f, SearchScale::Linear,      0.50f},
    {"oscillator.#.detune",                0.0f,     0.0f, SearchScale::Linear,      0.50f}, // demi-tons : ±1 sur l'un, ±12 sur l'autre
    {"oscillator.#.pitch",                 0.0f,     0.0f, SearchScale::Linear,      0.48f},
    {"voice.unison",                       0.0f,     0.0f, SearchScale::Linear,      0.48f},
    {"voice.unisonDetune",                 0.0f,     1.0f, SearchScale::Linear,      0.48f},
    {"epiano.character",                   0.0f,     1.0f, SearchScale::Linear,      0.45f},
    {"organ.percussion.level",             0.0f,     1.0f, SearchScale::Linear,      0.50f},
    {"organ.percussion.decay",             0.0f,     0.0f, SearchScale::Logarithmic, 0.48f},
    {"organ.percussion.harmonic",          0.0f,     0.0f, SearchScale::Linear,      0.44f},
    {"organ.keyClick",                     0.0f,     1.0f, SearchScale::Linear,      0.42f},
    {"accent.amount",                      0.0f,     1.0f, SearchScale::Linear,      0.42f},
    {"accent.threshold",                   0.0f,     0.0f, SearchScale::Linear,      0.35f},

    // --- Enveloppes : ce qui sépare un pincement d'une nappe ------------------
    {"envelope.#.attack",                  0.001f,   0.6f, SearchScale::Logarithmic, 0.90f},
    {"envelope.#.decay",                   0.02f,    2.0f, SearchScale::Logarithmic, 0.85f},
    {"envelope.#.sustain",                 0.0f,     1.0f, SearchScale::Linear,      0.85f},
    {"envelope.#.release",                 0.02f,    2.5f, SearchScale::Logarithmic, 0.60f},
    {"envelope.#.amount",                 -1.0f,     1.0f, SearchScale::Linear,      0.60f},
    {"envelope.#.time",                    0.001f,   2.0f, SearchScale::Logarithmic, 0.60f},
    {"envelope.#.mode",                    0.0f,     0.0f, SearchScale::Linear,      0.40f},
    {"oscillator.#.wavePositionEnvAmount",-1.0f,     1.0f, SearchScale::Linear,      0.80f},
    {"fm.operator.#.attack",               0.001f,   0.6f, SearchScale::Logarithmic, 0.62f},
    {"fm.operator.#.decay",                0.02f,    2.0f, SearchScale::Logarithmic, 0.60f},
    {"fm.operator.#.sustain",              0.0f,     1.0f, SearchScale::Linear,      0.60f},
    {"fm.operator.#.release",              0.02f,    2.5f, SearchScale::Logarithmic, 0.45f},
    {"fm.operator.#.fixedFrequency",       0.0f,     0.0f, SearchScale::Linear,      0.35f},
    {"fm.feedback",                        0.0f,     1.0f, SearchScale::Linear,      0.55f},
    {"fm.keyLevelScaling",                 0.0f,     1.0f, SearchScale::Linear,      0.30f},

    // --- Modulation : utile, rarement décisive pour approcher une cible -------
    {"filter.#.keyTrack",                  0.0f,     1.0f, SearchScale::Linear,      0.40f},
    {"filter.#.velocityAmount",            0.0f,     1.0f, SearchScale::Linear,      0.35f},
    {"sample.#.velocityAmount",            0.0f,     1.0f, SearchScale::Linear,      0.35f},
    {"voice.velocitySensitivity",          0.0f,     1.0f, SearchScale::Linear,      0.35f},
    {"lfo.#.toWavePosition",               0.0f,     1.0f, SearchScale::Linear,      0.32f},
    {"lfo.#.rate",                         0.1f,    12.0f, SearchScale::Logarithmic, 0.30f},
    {"lfo.#.toFilter",                     0.0f,     1.0f, SearchScale::Linear,      0.25f},
    {"lfo.#.toPulseWidth",                 0.0f,     1.0f, SearchScale::Linear,      0.22f},
    {"lfo.#.toPitch",                      0.0f,     0.5f, SearchScale::Linear,      0.20f},
    {"lfo.#.waveform",                     0.0f,     0.0f, SearchScale::Linear,      0.20f},
    {"lfo.#.delay",                        0.0f,     0.0f, SearchScale::Linear,      0.15f},
    {"polyMod.toFilterCutoff",             0.0f,     1.0f, SearchScale::Linear,      0.34f},
    {"polyMod.toOscAFrequency",            0.0f,     1.0f, SearchScale::Linear,      0.32f},
    {"polyMod.toOscAPulseWidth",           0.0f,     1.0f, SearchScale::Linear,      0.30f},
    {"polyMod.sourceFilterEnv",            0.0f,     0.0f, SearchScale::Linear,      0.28f},
    {"polyMod.sourceOscB",                 0.0f,     0.0f, SearchScale::Linear,      0.28f},

    // --- Effets intégrés à une machine ---------------------------------------
    // Ceux d'un piano électrique ou d'un orgue ne sont PAS accessoires : le
    // trémolo et le rotatif font partie de l'instrument. Ceux d'une chaîne
    // d'effets séparée n'apparaissent jamais ici, puisqu'on construit le
    // profil d'une machine.
    {"effect.tremolo.depth",               0.0f,     1.0f, SearchScale::Linear,      0.45f},
    {"effect.tremolo.rate",                0.5f,    12.0f, SearchScale::Logarithmic, 0.42f},
    {"effect.tremolo.stereo",              0.0f,     1.0f, SearchScale::Linear,      0.25f},
    {"effect.rotary.depth",                0.0f,     1.0f, SearchScale::Linear,      0.35f},
    {"effect.rotary.balance",              0.0f,     1.0f, SearchScale::Linear,      0.35f},
    {"effect.vibrato.depth",               0.0f,     1.0f, SearchScale::Linear,      0.30f},
    {"effect.vibrato.rate",                3.0f,     9.0f, SearchScale::Logarithmic, 0.25f},
    {"effect.chorus.mode",                 0.0f,     0.0f, SearchScale::Linear,      0.38f},
    {"effect.drive.amount",                0.0f,     1.0f, SearchScale::Linear,      0.35f},
    // Accord global, en cents. Cherchable et UTILE : une banque enregistrée
    // instrument par instrument n'est jamais parfaitement d'accord avec
    // elle-même, et quelques cents suffisent à creuser la distance sur une
    // cible tenue. Bornes du paramètre (0/0), échelle linéaire -- le cent EST
    // déjà une échelle logarithmique de fréquence.
    {"output.tune",                        0.0f,     0.0f, SearchScale::Linear,      0.55f},
};

/// Paramètres EXCLUS de la recherche, avec la raison. Les exclure n'est pas
/// une commodité : les laisser dégraderait activement le résultat.
struct Exclusion { const char* pattern; const char* reason; };

constexpr Exclusion kExclusions[] = {
    {"voice.analogCharacter",
     "ajoute du bruit aléatoire : l'optimiseur le pousserait à zéro pour "
     "réduire la distance, ce qui n'est pas un réglage plus juste mais une "
     "machine plus morte"},
    {"output.level",
     "un volume ne rapproche d'aucune cible : la distance est mesurée sur des "
     "grandeurs normalisées, et l'optimiseur y gaspillerait une dimension"},
    {"output.stereoWidth",
     "même raison : une note isolée se compare en mono"},
    {"voice.glide",
     "s'entend ENTRE deux notes ; la recherche rend une note isolée"},
    {"voice.glideTime",
     "même raison que le glissando lui-même"},
    {"effect.rotary.fast",
     "commande de jeu (lent/rapide), pas un réglage de timbre"},
    {"piano.sustainPedal",
     "commande de JEU, et binaire : la chercher reviendrait à demander à "
     "l'optimiseur de choisir entre deux falaises, alors que la pédale se joue "
     "au pied pendant le morceau et n'appartient pas au patch"},
    {"sampler.slot.#.note",
     "mapping : quelle touche déclenche l'emplacement. Le chercher ferait "
     "taire le son au lieu de l'ajuster"},
    {"sampler.slot.#.start",
     "point de départ dans le fichier : réglage de découpe, posé par "
     "l'analyse qui a fait la découpe"},
    {"sampler.slot.#.chokeGroup",
     "groupe de coupure : configuration du kit, sans effet sur un coup isolé"},
    {"sampler.slot.#.pan",
     "placement stéréo d'un kit ; une note isolée se compare en mono"},
    {"sample.program",
     "choix d'INSTRUMENT dans le profil, et il est discret : entre le programme "
     "40 et le 41 il n'y a pas de continuum mais deux sons sans rapport. "
     "L'évolution différentielle interpole ses candidats ; sur une falaise "
     "pareille elle ne descend pas, elle tire au sort. C'est le classifieur ou "
     "l'utilisateur qui choisit l'instrument, la recherche cale l'habillage"},
};

bool isExcluded(const std::string& canonicalId) {
    for (const auto& exclusion : kExclusions)
        if (canonicalId == exclusion.pattern) return true;
    return false;
}

const Rule* findRule(const std::string& canonicalId) {
    for (const auto& rule : kRules)
        if (canonicalId == rule.pattern) return &rule;
    return nullptr;
}

} // namespace

const char* searchScaleName(SearchScale scale) {
    return scale == SearchScale::Logarithmic ? "log" : "linear";
}

std::vector<SearchDimension> SearchProfile::topDimensions(size_t count) const {
    // Le profil est déjà trié par importance décroissante à la construction.
    const size_t take = std::min(count, dimensions_.size());
    return std::vector<SearchDimension>(dimensions_.begin(),
                                         dimensions_.begin() + static_cast<long>(take));
}

const SearchDimension* SearchProfile::find(const std::string& semanticId) const {
    for (const auto& dimension : dimensions_)
        if (dimension.semanticId == semanticId) return &dimension;
    return nullptr;
}

JsonValue SearchProfile::toJson() const {
    JsonValue root = JsonValue::makeObject();
    root.set("machine", JsonValue::makeString(pluginId_));
    JsonValue list = JsonValue::makeArray();
    for (const auto& dimension : dimensions_) {
        JsonValue entry = JsonValue::makeObject();
        entry.set("id", JsonValue::makeString(dimension.semanticId));
        entry.set("low", JsonValue::makeFloat(dimension.low));
        entry.set("high", JsonValue::makeFloat(dimension.high));
        entry.set("scale", JsonValue::makeString(searchScaleName(dimension.scale)));
        entry.set("importance", JsonValue::makeFloat(dimension.importance));
        if (!dimension.unit.empty()) entry.set("unit", JsonValue::makeString(dimension.unit));
        list.append(std::move(entry));
    }
    root.set("dimensions", std::move(list));
    return root;
}

SearchProfile buildSearchProfile(const std::string& pluginId) {
    const SemanticProfile semantic = buildSemanticProfile(pluginId);
    if (semantic.empty()) return {};

    std::vector<SearchDimension> dimensions;
    for (const auto& parameter : semantic.parameters()) {
        if (parameter.semanticId.empty()) continue;
        const std::string canonicalId = canonicalise(parameter.semanticId);
        if (isExcluded(canonicalId)) continue;
        const Rule* rule = findRule(canonicalId);
        // Aucune règle : le paramètre n'est PAS cherché. C'est un choix, pas
        // un oubli -- une famille qu'on n'a pas su classer serait cherchée à
        // l'aveugle, ce qui coûte du budget sans rapprocher de la cible.
        if (rule == nullptr) continue;

        SearchDimension dimension;
        dimension.semanticId = parameter.semanticId;
        dimension.scale = rule->scale;
        dimension.unit = parameter.unit;

        // DÉCOTE PAR NUMÉRO D'INSTANCE. La règle porte sur la forme canonique,
        // où le numéro a disparu : `envelope.1.attack` et `envelope.2.attack`
        // reçoivent donc la même importance de base. Or, par convention du
        // projet, l'instance 1 est la PRINCIPALE -- enveloppe d'amplitude,
        // filtre principal, oscillateur principal -- et les suivantes sont des
        // modulations ou des étages secondaires.
        //
        // Mesuré, sur une cible produite par le Minimoog : sans cette décote,
        // les deux enveloppes d'attaque occupaient les rangs 2 et 3 et
        // EXPULSAIENT la résonance du filtre de l'espace cherché. Le résultat
        // trouvait la coupure à 907 Hz pour 900 visés, mais laissait la
        // résonance à sa valeur par défaut, loin de la cible.
        const int instance = instanceOf(parameter.semanticId);
        const float decay = 1.0f - 0.12f * static_cast<float>(std::max(0, instance - 1));
        dimension.importance = rule->importance * std::max(0.25f, decay);

        // Une règle à bornes nulles signifie « reprendre les bornes réelles du
        // paramètre ». C'est le cas des grandeurs DISCRÈTES (choix de forme
        // d'onde, de table, de tirette) et de celles dont l'étendue utile
        // dépend entièrement de la machine : la borner à l'aveugle en
        // interdirait des valeurs légitimes.
        const bool useDeclaredRange = (rule->low == 0.0f && rule->high == 0.0f);
        if (useDeclaredRange) {
            dimension.low = parameter.minimum;
            dimension.high = parameter.maximum;
        } else {
            // ROGNAGE aux bornes réelles : une borne utile qui sortirait de ce
            // que la machine accepte produirait des valeurs refusées ou
            // écrêtées, et l'optimiseur explorerait un plateau.
            dimension.low = std::max(rule->low, parameter.minimum);
            dimension.high = std::min(rule->high, parameter.maximum);
        }

        // Une dimension sans étendue n'est pas une dimension : la chercher
        // reviendrait à dépenser du budget pour une constante.
        if (!(dimension.high > dimension.low)) continue;

        // Une échelle logarithmique exige une borne basse strictement
        // positive. Plutôt que de refuser la dimension, on relève la borne :
        // c'est ce que fait déjà toute recherche de fréquence sérieuse.
        if (dimension.scale == SearchScale::Logarithmic && dimension.low <= 0.0f)
            dimension.low = std::max(1e-4f, dimension.high * 1e-4f);

        dimensions.push_back(std::move(dimension));
    }

    // Tri par importance décroissante, puis par identifiant pour que l'ordre
    // soit STABLE : deux exécutions doivent proposer les mêmes dimensions dans
    // le même ordre, sans quoi la recherche ne serait pas reproductible.
    std::sort(dimensions.begin(), dimensions.end(),
              [](const SearchDimension& a, const SearchDimension& b) {
                  if (a.importance != b.importance) return a.importance > b.importance;
                  return a.semanticId < b.semanticId;
              });

    return SearchProfile(pluginId, std::move(dimensions));
}

} // namespace vsm::interchange
