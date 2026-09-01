#include "TestFramework.h"
#include "../plugins/multisample/MultisampleSynth.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;
using vsm::plugins::multisample::LoadedProfile;
using vsm::plugins::multisample::LoadedZone;
using vsm::plugins::multisample::MultisampleSynth;
using vsm::plugins::multisample::ProfilePtr;

namespace {
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::shared_ptr<MultisampleSynth> makeMultisample(double sr = kSampleRate) {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.multisample");
    auto synth = std::dynamic_pointer_cast<MultisampleSynth>(plugin);
    if (synth) synth->initialize(sr, 512);
    return synth;
}

ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList()) if (info.name == name) return info.id;
    return 0;
}
void set(ISynthPlugin& plugin, const std::string& name, float value) {
    plugin.setParameter(byName(plugin, name), value);
}

MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity = 100) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}
MidiNoteEvent noteOff(int offset, uint8_t note) {
    return {MidiNoteEvent::Kind::NoteOff, offset, 0, note, 0};
}

/// Une sinusoïde pure, à la fréquence voulue, dans un tampon de la longueur
/// voulue. C'est l'échantillon de référence de tous les tests : sa hauteur est
/// connue exactement, donc toute erreur de repitch se mesure en cents.
vsm::audio::io::SampleBufferPtr makeSine(double frequency, size_t frames,
                                          double fileRate = kSampleRate, bool stereo = false) {
    auto buffer = std::make_shared<vsm::audio::io::SampleBuffer>();
    buffer->sampleRate = fileRate;
    buffer->left.resize(frames);
    for (size_t i = 0; i < frames; ++i)
        buffer->left[i] = static_cast<float>(std::sin(2.0 * kPi * frequency * static_cast<double>(i) / fileRate));
    if (stereo) buffer->right = buffer->left;
    return buffer;
}

std::vector<float> render(MultisampleSynth& synth, const std::vector<MidiNoteEvent>& events, int frames) {
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    constexpr int kBlock = 256;
    std::vector<MidiNoteEvent> blockEvents;
    for (int start = 0; start < frames; start += kBlock) {
        const int count = std::min(kBlock, frames - start);
        blockEvents.clear();
        for (const auto& event : events) {
            if (event.sampleOffset >= start && event.sampleOffset < start + count) {
                MidiNoteEvent local = event;
                local.sampleOffset = event.sampleOffset - start;
                blockEvents.push_back(local);
            }
        }
        synth.process(blockEvents.data(), static_cast<int>(blockEvents.size()),
                      left.data() + start, right.data() + start, count);
    }
    return left;
}

float peakAbs(const std::vector<float>& x, size_t from = 0, size_t count = SIZE_MAX) {
    float peak = 0.0f;
    for (size_t i = from; i < x.size() && i < from + count; ++i) peak = std::max(peak, std::abs(x[i]));
    return peak;
}
double rms(const std::vector<float>& x, size_t from, size_t count) {
    double sum = 0.0; size_t n = 0;
    for (size_t i = from; i < x.size() && i < from + count; ++i) { sum += double(x[i]) * x[i]; ++n; }
    return n ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

/// Fréquence estimée par passages par zéro montants, entre deux instants. La
/// sinusoïde de référence est pure : c'est assez précis pour juger un écart de
/// cinq cents (0,29 % en fréquence).
double estimateFrequency(const std::vector<float>& x, size_t from, size_t to) {
    double firstCrossing = -1.0, lastCrossing = -1.0;
    int crossings = 0;
    for (size_t i = from + 1; i < to && i < x.size(); ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            // Interpolation linéaire du passage par zéro : sans elle, la
            // quantification à l'échantillon plafonne la précision bien
            // au-dessus des cinq cents visés.
            const double t = static_cast<double>(i - 1)
                           + static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
            if (firstCrossing < 0.0) firstCrossing = t; else { lastCrossing = t; ++crossings; }
        }
    }
    if (crossings < 2) return 0.0;
    return static_cast<double>(crossings) * kSampleRate / (lastCrossing - firstCrossing);
}

double centsBetween(double a, double b) { return 1200.0 * std::log2(a / b); }

/// Profil d'essai : un programme, deux zones de notes contiguës, deux couches
/// de vélocité, échantillons engendrés -- AUCUN fichier, donc aucun test qui
/// dépende d'une banque installée.
ProfilePtr makeTestProfile() {
    auto profile = std::make_shared<LoadedProfile>();
    profile->name = "Essai";
    profile->attribution = "engendré par les tests";
    profile->programNames = {"Essai"};

    // Zone A : notes 48..59, deux couches ; zone B : notes 60..71.
    struct Decl { int lo, hi, root, loVel, hiVel; double frequency; };
    const Decl declarations[] = {
        {48, 59, 52, 1,   63,  164.81}, // mi2 doux
        {48, 59, 52, 64, 127,  164.81}, // mi2 fort
        {60, 71, 64, 1,   63,  329.63}, // mi3 doux
        {60, 71, 64, 64, 127,  329.63}, // mi3 fort
    };
    for (const auto& declaration : declarations) {
        LoadedZone zone;
        zone.program = 0;
        zone.lowNote = declaration.lo;
        zone.highNote = declaration.hi;
        zone.lowVelocity = declaration.loVel;
        zone.highVelocity = declaration.hiVel;
        zone.rootNote = declaration.root;
        // Les couches fortes sont deux fois plus fortes : c'est ce qui rend le
        // franchissement du seuil MESURABLE, et pas seulement plausible.
        zone.level = declaration.loVel > 1 ? 1.0f : 0.5f;
        zone.sample = makeSine(declaration.frequency, 24000);
        zone.relativePath = "essai.wav";
        profile->zones.push_back(std::move(zone));
    }
    return profile;
}
} // namespace

// --- Batterie obligatoire (GUIDE § 9) --------------------------------------

VSM_TEST(multisample_registered) {
    registerBuiltInPlugins();
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.multisample"));
}

VSM_TEST(multisample_silent_with_no_events) {
    auto synth = makeMultisample();
    VSM_ASSERT(synth != nullptr);
    synth->setProfile(makeTestProfile());
    const auto out = render(*synth, {}, 4800);
    VSM_ASSERT_NEAR(peakAbs(out), 0.0f, 1e-9f);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(multisample_note_produces_sound) {
    auto synth = makeMultisample();
    synth->setProfile(makeTestProfile());
    const auto out = render(*synth, {noteOn(0, 60, 100)}, 9600);
    VSM_ASSERT(peakAbs(out) > 0.05f);
    for (float value : out) VSM_ASSERT(std::isfinite(value));
}

VSM_TEST(multisample_is_deterministic) {
    const std::vector<MidiNoteEvent> events{noteOn(0, 60, 100), noteOn(1200, 64, 40), noteOff(4800, 60)};
    auto first = makeMultisample();
    first->setProfile(makeTestProfile());
    const auto a = render(*first, events, 14400);
    auto second = makeMultisample();
    second->setProfile(makeTestProfile());
    const auto b = render(*second, events, 14400);
    VSM_ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT(a[i] == b[i]); // au bit près
}

VSM_TEST(multisample_save_load_roundtrip) {
    auto synth = makeMultisample();
    set(*synth, "Tune", 37.0f);
    set(*synth, "Attack", 0.25f);
    set(*synth, "Release", 1.75f);
    set(*synth, "Tone Cutoff", 3200.0f);
    set(*synth, "Velocity Amount", 0.4f);
    const auto state = synth->saveState();
    VSM_ASSERT(state.pluginTypeId == "vsm.multisample");

    auto other = makeMultisample();
    other->loadState(state);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Tune")), 37.0f, 1e-4f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Attack")), 0.25f, 1e-4f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Release")), 1.75f, 1e-4f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Tone Cutoff")), 3200.0f, 1e-2f);
    VSM_ASSERT_NEAR(other->getParameter(byName(*other, "Velocity Amount")), 0.4f, 1e-4f);
}

VSM_TEST(multisample_parameter_list_size) {
    auto synth = makeMultisample();
    VSM_ASSERT_EQ(synth->parameterList().size(), size_t(7));
}

// --- Tests propres à la machine (CDC-multisample § 8) ----------------------

/// § 8.1 — la note à la frontière de deux zones tire la BONNE zone, des deux
/// côtés. La zone basse et la zone haute portent des échantillons d'une octave
/// d'écart avec la même racine relative : si la sélection se trompe de zone, la
/// hauteur rendue saute d'une octave, ce qui ne se confond avec rien.
VSM_TEST(multisample_zone_boundary_picks_the_right_zone) {
    auto synth = makeMultisample();
    synth->setProfile(makeTestProfile());
    set(*synth, "Attack", 0.001f);
    set(*synth, "Velocity Amount", 0.0f);

    // 59 est la dernière note de la zone basse (racine 52, échantillon mi2) ;
    // 60 la première de la zone haute (racine 64, échantillon mi3).
    const auto below = render(*synth, {noteOn(0, 59, 100)}, 24000);
    auto other = makeMultisample();
    other->setProfile(makeTestProfile());
    set(*other, "Attack", 0.001f);
    set(*other, "Velocity Amount", 0.0f);
    const auto above = render(*other, {noteOn(0, 60, 100)}, 24000);

    const double frequencyBelow = estimateFrequency(below, 2400, 20000);
    const double frequencyAbove = estimateFrequency(above, 2400, 20000);
    // note 59 sur la zone basse : 164,81 Hz transposé de +7 demi-tons
    const double expectedBelow = 164.81 * std::pow(2.0, 7.0 / 12.0);
    // note 60 sur la zone haute : 329,63 Hz transposé de -4 demi-tons
    const double expectedAbove = 329.63 * std::pow(2.0, -4.0 / 12.0);
    VSM_ASSERT(std::abs(centsBetween(frequencyBelow, expectedBelow)) < 5.0);
    VSM_ASSERT(std::abs(centsBetween(frequencyAbove, expectedAbove)) < 5.0);
}

/// § 8.2 — deux vélocités de part et d'autre du seuil de couche tirent deux
/// échantillons DIFFÉRENTS. Mesuré sur le niveau, avec `Velocity Amount` à
/// zéro : si la couche ne changeait pas, la vélocité n'aurait plus aucun effet
/// et les deux rendus seraient identiques.
VSM_TEST(multisample_velocity_layers_are_distinct) {
    auto soft = makeMultisample();
    soft->setProfile(makeTestProfile());
    set(*soft, "Velocity Amount", 0.0f);
    const auto quiet = render(*soft, {noteOn(0, 60, 63)}, 12000);

    auto loud = makeMultisample();
    loud->setProfile(makeTestProfile());
    set(*loud, "Velocity Amount", 0.0f);
    const auto strong = render(*loud, {noteOn(0, 60, 64)}, 12000);

    const double quietRms = rms(quiet, 2400, 7200);
    const double strongRms = rms(strong, 2400, 7200);
    VSM_ASSERT(quietRms > 0.0);
    VSM_ASSERT(strongRms > 1.8 * quietRms); // la couche forte est déclarée deux fois plus forte
}

/// § 8.3 — la boucle de tenue ne claque pas. L'échantillon fait une seconde ;
/// la boucle est calée sur un nombre ENTIER de périodes, et la note tient
/// quatre secondes, donc la boucle est franchie plusieurs fois. Le critère est
/// la continuité d'énergie : aucune fenêtre courte ne doit s'écarter du niveau
/// moyen, ce qu'un clic ferait immédiatement.
VSM_TEST(multisample_sustain_loop_does_not_click) {
    auto profile = std::make_shared<LoadedProfile>();
    profile->name = "Boucle";
    profile->attribution = "engendré par les tests";
    LoadedZone zone;
    zone.rootNote = 60;
    zone.lowNote = 0; zone.highNote = 127;
    const double frequency = 200.0; // 240 trames par période à 48 kHz
    zone.sample = makeSine(frequency, 48000);
    // Boucle de la trame 12000 à la trame 24000 : cinquante périodes exactes.
    zone.loopEnabled = true;
    zone.loopStart = 12000;
    zone.loopEnd = 24000;
    zone.relativePath = "boucle.wav";
    profile->zones.push_back(std::move(zone));
    profile->programNames = {"Boucle"};

    auto synth = makeMultisample();
    synth->setProfile(profile);
    set(*synth, "Attack", 0.001f);
    set(*synth, "Velocity Amount", 0.0f);

    const int frames = 48000 * 4;
    const auto out = render(*synth, {noteOn(0, 60, 100)}, frames);

    // La note doit encore sonner bien après la fin du fichier : c'est la
    // preuve que la boucle tourne, et pas seulement qu'elle ne claque pas.
    VSM_ASSERT(rms(out, 100000, 4800) > 0.1);

    // Continuité : le RMS de fenêtres de 240 trames (une période) reste stable
    // autour du bouclage. Un clic ferait bondir une fenêtre.
    double lowest = 1e9, highest = 0.0;
    for (size_t start = 24000; start + 240 < static_cast<size_t>(frames); start += 240) {
        const double window = rms(out, start, 240);
        lowest = std::min(lowest, window);
        highest = std::max(highest, window);
    }
    VSM_ASSERT(lowest > 0.5 * highest);

    // Et le pas d'échantillon reste borné : un raccord discontinu produirait un
    // saut bien supérieur à ce que la sinusoïde permet.
    const double maximumStep = 2.0 * kPi * frequency / kSampleRate * 1.5;
    for (size_t i = 24001; i < static_cast<size_t>(frames); ++i)
        VSM_ASSERT(std::abs(out[i] - out[i - 1]) < maximumStep);
}

/// § 8.4 — le repitch est juste à ±5 cents, mesuré sur la sinusoïde de
/// référence, y compris quand le fichier n'est PAS à la fréquence du moteur.
VSM_TEST(multisample_repitch_is_accurate) {
    struct Case { uint8_t note; double fileRate; };
    const Case cases[] = {{60, kSampleRate}, {67, kSampleRate}, {48, kSampleRate}, {60, 44100.0}};
    for (const auto& testCase : cases) {
        auto profile = std::make_shared<LoadedProfile>();
        profile->attribution = "engendré par les tests";
        LoadedZone zone;
        zone.rootNote = 60;
        zone.lowNote = 0; zone.highNote = 127;
        zone.sample = makeSine(220.0, static_cast<size_t>(testCase.fileRate), testCase.fileRate);
        zone.loopEnabled = true;
        zone.loopStart = 0;
        zone.loopEnd = static_cast<uint64_t>(testCase.fileRate);
        profile->zones.push_back(std::move(zone));

        auto synth = makeMultisample();
        synth->setProfile(profile);
        set(*synth, "Attack", 0.001f);
        set(*synth, "Velocity Amount", 0.0f);
        const auto out = render(*synth, {noteOn(0, testCase.note, 100)}, 24000);

        const double expected = 220.0 * std::pow(2.0, (double(testCase.note) - 60.0) / 12.0);
        const double measured = estimateFrequency(out, 2400, 22000);
        VSM_ASSERT(measured > 0.0);
        VSM_ASSERT(std::abs(centsBetween(measured, expected)) < 5.0);
    }
}

/// § 8.5 — profil absent, échantillon absent, attribution absente : refusés EN
/// LE DISANT. Le chemin absolu, lui, est refusé par la couche d'échange qui
/// possède la règle de format (test dans interchange/).
VSM_TEST(multisample_refuses_broken_profiles_out_loud) {
    auto synth = makeMultisample();

    // Sans profil : silencieuse, et elle le dit par son état, pas par un son
    // de repli.
    VSM_ASSERT_EQ(synth->zoneCount(), 0);
    VSM_ASSERT(synth->profileName().empty());
    const auto silence = render(*synth, {noteOn(0, 60, 100)}, 4800);
    VSM_ASSERT_NEAR(peakAbs(silence), 0.0f, 1e-9f);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);

    std::string error;
    MultisampleProfileSpec empty;
    empty.attribution = "essai";
    VSM_ASSERT(!synth->loadProfile(empty, error, nullptr));
    VSM_ASSERT(!error.empty());

    MultisampleProfileSpec noAttribution;
    noAttribution.zones.push_back({});
    error.clear();
    VSM_ASSERT(!synth->loadProfile(noAttribution, error, nullptr));
    VSM_ASSERT(error.find("attribution") != std::string::npos);

    MultisampleProfileSpec missingFile;
    missingFile.attribution = "essai";
    MultisampleZoneSpec zone;
    zone.samplePath = (std::filesystem::temp_directory_path() / "vsm-inexistant-xyz.wav").string();
    zone.relativePath = "vsm-inexistant-xyz.wav";
    missingFile.zones.push_back(zone);
    error.clear();
    VSM_ASSERT(!synth->loadProfile(missingFile, error, nullptr));
    VSM_ASSERT(error.find("vsm-inexistant-xyz.wav") != std::string::npos);

    // Un échec ne détruit pas le profil en place : la machine garde ce qu'elle
    // avait, plutôt que de devenir muette à cause d'un profil fautif.
    synth->setProfile(makeTestProfile());
    VSM_ASSERT_EQ(synth->zoneCount(), 4);
    error.clear();
    VSM_ASSERT(!synth->loadProfile(missingFile, error, nullptr));
    VSM_ASSERT_EQ(synth->zoneCount(), 4);
}

/// Une note hors de toute zone ne consomme PAS de voix et ne sonne pas. C'est
/// le trait qui distingue « le profil ne couvre pas cette note » de « la note
/// est jouée par la zone voisine », et le second serait un mensonge.
VSM_TEST(multisample_uncovered_note_is_silent_and_costs_no_voice) {
    auto synth = makeMultisample();
    synth->setProfile(makeTestProfile()); // couvre 48..71
    const auto out = render(*synth, {noteOn(0, 90, 100)}, 4800);
    VSM_ASSERT_NEAR(peakAbs(out), 0.0f, 1e-9f);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

/// Polyphonie : trente-deux notes tenues sonnent toutes, la trente-troisième
/// vole la plus ancienne. Sans ce test, une régression du gestionnaire de voix
/// se manifesterait par des notes manquantes au milieu d'un accord de pédale.
VSM_TEST(multisample_polyphony_holds_thirty_two_notes) {
    // Profil COUVRANT TOUT LE CLAVIER : le profil d'essai ordinaire ne couvre
    // que 48..71, soit vingt-quatre notes -- de quoi faire échouer le test pour
    // une raison qui n'a rien à voir avec la polyphonie.
    auto wide = std::make_shared<LoadedProfile>();
    wide->attribution = "engendré par les tests";
    LoadedZone whole;
    whole.rootNote = 60;
    whole.lowNote = 0; whole.highNote = 127;
    whole.sample = makeSine(220.0, 48000);
    whole.loopEnabled = true; whole.loopStart = 0; whole.loopEnd = 48000;
    wide->zones.push_back(std::move(whole));

    auto synth = makeMultisample();
    synth->setProfile(wide);
    set(*synth, "Release", 4.0f);

    std::vector<MidiNoteEvent> events;
    for (int i = 0; i < 32; ++i) events.push_back(noteOn(i * 16, static_cast<uint8_t>(48 + i), 100));
    const auto out = render(*synth, events, 8192);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 32);
    VSM_ASSERT(peakAbs(out) > 0.0f);

    std::vector<MidiNoteEvent> oneMore{noteOn(0, 71, 100)};
    render(*synth, oneMore, 256);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 32); // vol, pas dépassement
}

/// Le réglage de timbre est NEUTRE à fond -- exactement. Joué à sa note racine,
/// à la fréquence du moteur, sans vélocité ni accord, le lecteur doit rendre le
/// FICHIER, échantillon pour échantillon. C'est la vérification la plus stricte
/// possible de la chaîne : tout filtre résiduel, toute erreur d'interpolation à
/// pas unité, tout gain parasite la ferait échouer.
VSM_TEST(multisample_at_root_note_reproduces_the_file_exactly) {
    auto profile = std::make_shared<LoadedProfile>();
    profile->attribution = "engendré par les tests";
    LoadedZone zone;
    zone.rootNote = 60;
    zone.lowNote = 0; zone.highNote = 127;
    zone.sample = makeSine(220.0, 48000);
    zone.loopEnabled = true; zone.loopStart = 0; zone.loopEnd = 48000;
    profile->zones.push_back(zone);

    auto synth = makeMultisample();
    synth->setProfile(profile);
    set(*synth, "Output Level", 1.0f);
    set(*synth, "Velocity Amount", 0.0f);
    set(*synth, "Attack", 0.001f);
    set(*synth, "Tone Cutoff", 20000.0f); // butée haute = chemin direct
    const auto out = render(*synth, {noteOn(0, 60, 100)}, 24000);

    const auto& file = zone.sample->left;
    for (size_t i = 4800; i < 20000; ++i)
        VSM_ASSERT_NEAR(out[i], file[i], 1e-6f);
}

/// Et il assombrit réellement quand on le ferme. Mesuré sur un échantillon
/// RICHE (une dent de scie), parce qu'un passe-bas à un pôle ne dit presque
/// rien sur une sinusoïde située sous sa coupure.
VSM_TEST(multisample_tone_darkens_when_closed) {
    auto profile = std::make_shared<LoadedProfile>();
    profile->attribution = "engendré par les tests";
    LoadedZone zone;
    zone.rootNote = 60;
    zone.lowNote = 0; zone.highNote = 127;
    auto saw = std::make_shared<vsm::audio::io::SampleBuffer>();
    saw->sampleRate = kSampleRate;
    saw->left.resize(48000);
    for (size_t i = 0; i < saw->left.size(); ++i) {
        const double phase = std::fmod(220.0 * static_cast<double>(i) / kSampleRate, 1.0);
        saw->left[i] = static_cast<float>(2.0 * phase - 1.0);
    }
    zone.sample = saw;
    zone.loopEnabled = true; zone.loopStart = 0; zone.loopEnd = 48000;
    profile->zones.push_back(zone);

    auto bright = makeMultisample();
    bright->setProfile(profile);
    set(*bright, "Velocity Amount", 0.0f);
    set(*bright, "Tone Cutoff", 20000.0f);
    const auto open = render(*bright, {noteOn(0, 60, 100)}, 24000);

    auto muffled = makeMultisample();
    muffled->setProfile(profile);
    set(*muffled, "Velocity Amount", 0.0f);
    set(*muffled, "Tone Cutoff", 400.0f);
    const auto closed = render(*muffled, {noteOn(0, 60, 100)}, 24000);

    // Critère SPECTRAL, pas de niveau, et la nuance est mesurée : une dent de
    // scie garde l'essentiel de son énergie dans son fondamental, si bien que
    // fermer à 400 Hz ne lui retire que le quart de son RMS -- un seuil en
    // niveau y serait à la fois lâche et fragile. L'énergie de la DÉRIVÉE, elle,
    // pèse chaque harmonique par son rang : c'est exactement ce que le filtre
    // enlève, et l'écart devient franc.
    auto slopeEnergy = [](const std::vector<float>& x, size_t from, size_t to) {
        double sum = 0.0; size_t n = 0;
        for (size_t i = from + 1; i < to && i < x.size(); ++i) {
            const double step = static_cast<double>(x[i]) - static_cast<double>(x[i - 1]);
            sum += step * step; ++n;
        }
        return n ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
    };
    VSM_ASSERT(rms(closed, 4800, 14400) < 0.9 * rms(open, 4800, 14400));
    VSM_ASSERT(slopeEnergy(closed, 4800, 19200) < 0.2 * slopeEnergy(open, 4800, 19200));
}

VSM_TEST(multisample_honours_pitch_bend_like_a_hardware_sampler) {
    // Le refus « en attendant » du § 10 du CDC nouvelle machine est levé : la
    // molette de hauteur multiplie l'avance de lecture par 2^(demi-tons/12),
    // comme sur un sampler matériel. À molette nulle, le rapport vaut
    // exactement 1,0 : l'avance -- donc l'empreinte -- est inchangée au bit.
    // Le CC 1, lui, reste refusé en le disant : le vibrato d'un instrument
    // échantillonné est DANS ses échantillons, la machine n'a pas de LFO.
    auto pitchOf = [](const std::vector<float>& x, size_t from, size_t count) {
        // Hauteur par autocorrélation : suffisant pour « c'est plus aigu ».
        const float* p = x.data() + from;
        const size_t lagMin = static_cast<size_t>(kSampleRate / 2000.0);
        const size_t lagMax = std::min(count / 2, static_cast<size_t>(kSampleRate / 40.0));
        double best = -1.0; size_t bestLag = lagMin;
        for (size_t lag = lagMin; lag <= lagMax; ++lag) {
            double acc = 0.0;
            for (size_t i = 0; i + lag < count; ++i)
                acc += static_cast<double>(p[i]) * p[i + lag];
            if (acc > best) { best = acc; bestLag = lag; }
        }
        return kSampleRate / static_cast<double>(bestLag);
    };

    auto plie = makeMultisample();
    plie->setProfile(makeTestProfile());
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(plie->handleControlEvent(bend));
    const auto haut = render(*plie, {noteOn(0, 64, 100)}, 24000);

    auto temoin = makeMultisample();
    temoin->setProfile(makeTestProfile());
    const auto nu = render(*temoin, {noteOn(0, 64, 100)}, 24000);

    VSM_ASSERT(pitchOf(haut, 4800, 16000) > pitchOf(nu, 4800, 16000) * 1.05);

    MidiControlEvent cc;
    cc.kind = MidiControlEvent::Kind::ControlChange;
    cc.index = 1;
    cc.value = 1.0f;
    VSM_ASSERT(!temoin->handleControlEvent(cc));
}
