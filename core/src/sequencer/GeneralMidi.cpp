#include "vsm/sequencer/GeneralMidi.h"

namespace vsm::sequencer {

namespace {
// Les cent vingt-huit programmes, dans l'ordre de la norme, chacun avec la
// machine du parc qui lui ressemble le plus. La règle de choix : l'INSTRUMENT
// avant le timbre -- une corde pincée va à `vsm.string`, une anche à
// `vsm.cone` ou `vsm.reed`, un pad à un polysynthé ; quand le parc n'a pas
// l'instrument (guitare électrique, flûte hors build), la machine la plus
// proche par le geste.
const ProgrammeGM kTable[128] = {
    // Pianos
    {0, "Acoustic Grand Piano", "vsm.piano"},      {1, "Bright Acoustic Piano", "vsm.piano"},
    {2, "Electric Grand Piano", "vsm.piano"},      {3, "Honky-tonk Piano", "vsm.piano"},
    {4, "Electric Piano 1", "vsm.epiano"},         {5, "Electric Piano 2", "vsm.epiano"},
    {6, "Harpsichord", "vsm.harpsichord"},         {7, "Clavinet", "vsm.clavinet"},
    // Percussions chromatiques
    {8, "Celesta", "vsm.musicbox"},                {9, "Glockenspiel", "vsm.carillon"},
    {10, "Music Box", "vsm.musicbox"},             {11, "Vibraphone", "vsm.vibraphone"},
    {12, "Marimba", "vsm.modal"},                  {13, "Xylophone", "vsm.modal"},
    {14, "Tubular Bells", "vsm.carillon"},         {15, "Dulcimer", "vsm.string"},
    // Orgues
    {16, "Drawbar Organ", "vsm.tonewheel"},        {17, "Percussive Organ", "vsm.tonewheel"},
    {18, "Rock Organ", "vsm.tonewheel"},           {19, "Church Organ", "vsm.pipeorgan"},
    {20, "Reed Organ", "vsm.reed"},                {21, "Accordion", "vsm.reed"},
    {22, "Harmonica", "vsm.reed"},                 {23, "Tango Accordion", "vsm.reed"},
    // Guitares (le parc n'a pas de guitare électrique : la corde pincée)
    {24, "Acoustic Guitar (nylon)", "vsm.string"}, {25, "Acoustic Guitar (steel)", "vsm.string"},
    {26, "Electric Guitar (jazz)", "vsm.string"},  {27, "Electric Guitar (clean)", "vsm.string"},
    {28, "Electric Guitar (muted)", "vsm.string"}, {29, "Overdriven Guitar", "vsm.string"},
    {30, "Distortion Guitar", "vsm.string"},       {31, "Guitar Harmonics", "vsm.string"},
    // Basses
    {32, "Acoustic Bass", "vsm.string"},           {33, "Electric Bass (finger)", "vsm.minimoog"},
    {34, "Electric Bass (pick)", "vsm.minimoog"},  {35, "Fretless Bass", "vsm.minimoog"},
    {36, "Slap Bass 1", "vsm.minimoog"},           {37, "Slap Bass 2", "vsm.minimoog"},
    {38, "Synth Bass 1", "vsm.tb303"},             {39, "Synth Bass 2", "vsm.sh101"},
    // Cordes
    {40, "Violin", "vsm.string"},                  {41, "Viola", "vsm.string"},
    {42, "Cello", "vsm.string"},                   {43, "Contrabass", "vsm.string"},
    {44, "Tremolo Strings", "vsm.string"},         {45, "Pizzicato Strings", "vsm.string"},
    {46, "Orchestral Harp", "vsm.string"},         {47, "Timpani", "vsm.membrane"},
    // Ensembles
    {48, "String Ensemble 1", "vsm.divider"},      {49, "String Ensemble 2", "vsm.divider"},
    {50, "Synth Strings 1", "vsm.divider"},        {51, "Synth Strings 2", "vsm.juno106"},
    {52, "Choir Aahs", "vsm.vocal"},               {53, "Voice Oohs", "vsm.vocal"},
    {54, "Synth Voice", "vsm.vocal"},              {55, "Orchestra Hit", "vsm.obx"},
    // Cuivres
    {56, "Trumpet", "vsm.wind"},                   {57, "Trombone", "vsm.wind"},
    {58, "Tuba", "vsm.wind"},                      {59, "Muted Trumpet", "vsm.wind"},
    {60, "French Horn", "vsm.wind"},               {61, "Brass Section", "vsm.wind"},
    {62, "Synth Brass 1", "vsm.obx"},              {63, "Synth Brass 2", "vsm.jupiter8"},
    // Anches
    {64, "Soprano Sax", "vsm.cone"},               {65, "Alto Sax", "vsm.cone"},
    {66, "Tenor Sax", "vsm.cone"},                 {67, "Baritone Sax", "vsm.cone"},
    {68, "Oboe", "vsm.cone"},                      {69, "English Horn", "vsm.cone"},
    {70, "Bassoon", "vsm.cone"},                   {71, "Clarinet", "vsm.wind"},
    // Tuyaux (la flûte est hors build : le vent le plus proche)
    {72, "Piccolo", "vsm.wind"},                   {73, "Flute", "vsm.wind"},
    {74, "Recorder", "vsm.wind"},                  {75, "Pan Flute", "vsm.wind"},
    {76, "Blown Bottle", "vsm.wind"},              {77, "Shakuhachi", "vsm.wind"},
    {78, "Whistle", "vsm.wind"},                   {79, "Ocarina", "vsm.wind"},
    // Leads
    {80, "Lead 1 (square)", "vsm.sh101"},          {81, "Lead 2 (sawtooth)", "vsm.supersaw"},
    {82, "Lead 3 (calliope)", "vsm.juno106"},      {83, "Lead 4 (chiff)", "vsm.ms20"},
    {84, "Lead 5 (charang)", "vsm.ms20"},          {85, "Lead 6 (voice)", "vsm.vocal"},
    {86, "Lead 7 (fifths)", "vsm.arpodyssey"},     {87, "Lead 8 (bass + lead)", "vsm.minimoog"},
    // Pads
    {88, "Pad 1 (new age)", "vsm.juno106"},        {89, "Pad 2 (warm)", "vsm.jupiter8"},
    {90, "Pad 3 (polysynth)", "vsm.prophet"},      {91, "Pad 4 (choir)", "vsm.vocal"},
    {92, "Pad 5 (bowed)", "vsm.glass"},            {93, "Pad 6 (metallic)", "vsm.dx7"},
    {94, "Pad 7 (halo)", "vsm.cs80"},              {95, "Pad 8 (sweep)", "vsm.obx"},
    // Effets de synthèse
    {96, "FX 1 (rain)", "vsm.granular"},           {97, "FX 2 (soundtrack)", "vsm.wavetable"},
    {98, "FX 3 (crystal)", "vsm.dx7"},             {99, "FX 4 (atmosphere)", "vsm.granular"},
    {100, "FX 5 (brightness)", "vsm.additive"},    {101, "FX 6 (goblins)", "vsm.stochastic"},
    {102, "FX 7 (echoes)", "vsm.wavesequence"},    {103, "FX 8 (sci-fi)", "vsm.phasedist"},
    // Instruments du monde
    {104, "Sitar", "vsm.sitar"},                   {105, "Banjo", "vsm.banjo"},
    {106, "Shamisen", "vsm.string"},               {107, "Koto", "vsm.string"},
    {108, "Kalimba", "vsm.kalimba"},               {109, "Bag pipe", "vsm.bagpipe"},
    {110, "Fiddle", "vsm.string"},                 {111, "Shanai", "vsm.cone"},
    // Percussions
    {112, "Tinkle Bell", "vsm.carillon"},          {113, "Agogo", "vsm.perc"},
    {114, "Steel Drums", "vsm.modal"},             {115, "Woodblock", "vsm.perc"},
    {116, "Taiko Drum", "vsm.membrane"},           {117, "Melodic Tom", "vsm.membrane"},
    {118, "Synth Drum", "vsm.fmdrums"},            {119, "Reverse Cymbal", "vsm.plate"},
    // Bruitages
    {120, "Guitar Fret Noise", "vsm.string"},      {121, "Breath Noise", "vsm.wind"},
    {122, "Seashore", "vsm.granular"},             {123, "Bird Tweet", "vsm.theremin"},
    {124, "Telephone Ring", "vsm.psg"},            {125, "Helicopter", "vsm.stochastic"},
    {126, "Applause", "vsm.granular"},             {127, "Gunshot", "vsm.fmdrums"},
};
} // namespace

const ProgrammeGM& programmeGM(uint8_t numero) {
    return kTable[numero > 127 ? 127 : numero];
}

const char* machinePourKitGM(uint8_t programme) {
    if (programme == 25) return "vsm.tr808";
    if (programme >= 24 && programme <= 31) return "vsm.tr909";
    return "vsm.drums";
}

const char* nomDuKitGM(uint8_t programme) {
    if (programme == 25) return "TR-808 Kit";
    if (programme >= 24 && programme <= 31) return "Electronic Kit";
    if (programme >= 8 && programme <= 15) return "Room Kit";
    if (programme >= 16 && programme <= 23) return "Power Kit";
    if (programme >= 32 && programme <= 39) return "Jazz Kit";
    if (programme >= 40 && programme <= 47) return "Brush Kit";
    if (programme >= 48 && programme <= 55) return "Orchestra Kit";
    return "Standard Kit";
}

namespace {
// La liste CANONIQUE de tools/installer-banques-midi.py, recopiée : les deux
// doivent dire la même chose, et le test le garde pour les repères.
struct ProfilGM { uint8_t numero; const char* profil; };
const ProfilGM kProfils[] = {
    {0, "Grand-Piano"}, {4, "E-Piano-Tine"}, {5, "E-Piano-FM"}, {16, "Drawbar-Organ"},
    {18, "Rock-Organ"}, {19, "Church-Organ"}, {21, "Accordion"}, {24, "Nylon-Guitar"},
    {25, "Steel-Guitar"}, {26, "Jazz-Guitar"}, {27, "Clean-Guitar"}, {29, "Overdrive-Guitar"},
    {30, "Distortion-Guitar"}, {32, "Acoustic-Bass"}, {33, "Finger-Bass"}, {34, "Pick-Bass"},
    {35, "Fretless-Bass"}, {38, "Synth-Bass-1"}, {39, "Synth-Bass-2"}, {40, "Violin"},
    {42, "Cello"}, {46, "Harp"}, {48, "Strings"}, {49, "Slow-Strings"}, {50, "Synth-Strings-1"},
    {52, "Choir-Aahs"}, {53, "Voice-Oohs"}, {56, "Trumpet"}, {57, "Trombone"}, {61, "Brass-Section"},
    {62, "Synth-Brass-1"}, {64, "Soprano-Sax"}, {65, "Alto-Sax"}, {66, "Tenor-Sax"}, {68, "Oboe"},
    {71, "Clarinet"}, {73, "Flute"}, {80, "Square-Lead"}, {81, "Saw-Lead"}, {88, "New-Age-Pad"},
    {89, "Warm-Pad"}, {90, "Polysynth"}, {91, "Choir-Pad"}, {94, "Halo-Pad"}, {95, "Sweep-Pad"},
};
const BanqueGM kBanques[] = {
    {"FR3", "FluidR3"}, {"GU", "GeneralUser"}, {"MS", "MuseScore General"},
};
} // namespace

const char* profilCanoniqueGM(uint8_t programme) {
    for (const auto& p : kProfils)
        if (p.numero == programme) return p.profil;
    return nullptr;
}

const BanqueGM* banquesGM(std::size_t& compte) {
    compte = sizeof(kBanques) / sizeof(kBanques[0]);
    return kBanques;
}

} // namespace vsm::sequencer
