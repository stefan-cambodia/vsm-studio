#include "TestFramework.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "../plugins/multisample/MultisampleSynth.h"
#include "../plugins/sampler/SamplerSynth.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Tests de NON-RÉGRESSION AUDIO par machine (Phase 6, ARCHITECTURE.md § 10).
//
// Les tests par machine existants (test_minimoog_synth.cpp, test_dx7_synth.cpp,
// etc.) vérifient des PROPRIÉTÉS ("l'accent ouvre le filtre", "le HPF retire
// les graves"). Ils ne détectent pas une dérive globale du rendu : un jour où
// une brique DSP partagée (LadderFilterZDF, Envelope, AnalogDrift...) change
// légèrement, toutes ces propriétés restent vraies alors que TOUTES les
// machines sonnent différemment.
//
// Ce fichier comble ce trou : chaque machine rejoue une phrase FIXE et son
// rendu est réduit à une empreinte (pic, RMS global, RMS de 6 fenêtres, taux
// de passages par zéro, proportion d'échantillons L/R différents et profil
// spectral en 16 bandes + centroïde), comparée à
// une référence figée ci-dessous. Toute modification du son d'une machine --
// voulue ou accidentelle -- fait échouer son test et DOIT donc être un geste
// conscient : on régénère la table (voir plus bas) et on documente pourquoi.
//
// TOLÉRANCE, ET POURQUOI PAS DU BIT-À-BIT : le rendu est déterministe sur une
// machine donnée (tout l'aléatoire passe par vsm::util::DeterministicRng seedé)
// -- c'est déjà prouvé par les tests de déterminisme. Mais std::sin/std::exp
// n'ont pas de résultat identique garanti d'une libm ou d'une architecture à
// l'autre, et les filtres résonants comme la synthèse FM amplifient l'écart.
// Une empreinte bit-exacte échouerait donc à la première compilation sur une
// autre plateforme, pour de mauvaises raisons. La tolérance relative retenue
// (kRelTol) absorbe ce bruit numérique tout en restant très inférieure à ce
// que produit le moindre vrai changement de DSP (typiquement > 10 %).
//
// RÉGÉNÉRER LA TABLE (après un changement de son ASSUMÉ, ou pour une nouvelle
// machine) :
//     VSM_REGEN_AUDIO_FINGERPRINTS=1 ./build/audio/vsm_audio_tests
// puis recopier les lignes imprimées dans kReferences ci-dessous. Dans ce
// mode, les tests n'assertent rien : ils impriment.
// ---------------------------------------------------------------------------

using namespace vsm::audio::plugin;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;
constexpr int kTotalSamples = 72192; // ~1,5 s -- assez pour attaque, tenue et queue de release
constexpr int kNumWindows = 6;
static_assert(kTotalSamples % kBlockSize == 0, "le rendu se fait en blocs entiers");
static_assert(kTotalSamples % kNumWindows == 0, "les fenêtres d'analyse doivent partitionner exactement le rendu");

// Tolérance relative sur les grandeurs d'énergie (pic, RMS). kAbsFloor évite
// de comparer relativement deux quasi-zéros (fenêtre de silence en fin de
// queue) : sous ce plancher, deux valeurs sont considérées identiques.
constexpr float kRelTol = 0.03f;
constexpr float kAbsFloor = 1e-4f;
// Le taux de passages par zéro est un chiffre petit (quelques millièmes) mais
// très discriminant : deux machines différentes n'ont pas le même. Il se
// compare donc en relatif, avec un plancher plus bas que kAbsFloor.
constexpr float kZcrRelTol = 0.05f;
constexpr float kZcrFloor = 0.002f;
// Bandes spectrales : fractions dont la somme vaut 1. Le plancher évite de
// comparer relativement des bandes quasi vides (ex. 12-20 kHz sur un synthé
// filtré), qui ne portent aucune information.
constexpr float kBandRelTol = 0.05f;
constexpr float kBandFloor = 0.0005f;
// Centroïde spectral : mesure continue, qui bouge avec la moindre modification
// de filtre là où une bande (large) ne bouge qu'au-delà d'un certain
// déplacement d'énergie.
//
// CALIBRATION (mesurée en injectant temporairement de vraies dérives dans le
// code, puis en les retirant -- pas des valeurs choisies au jugé) :
//   - coupure du ladder +1 %  -> aucune machine en échec (marge de bruit voulue)
//   - coupure du ladder +10 % -> Minimoog, TB-303, SH-101, Prophet, Jupiter-8
//   - decay des enveloppes -5 % -> TB-303, Juno-106, Prophet, DX7
// Aucune de ces deux dérives ne faisait échouer le moindre test préexistant.
// Limite connue et assumée : le Juno-106 et l'ARP Odyssey ne réagissent pas à
// la dérive de coupure ci-dessus (chorus stéréo qui étale le spectre pour le
// premier, filtre peu déterminant dans le patch par défaut pour le second) --
// leur empreinte reste sensible aux changements d'enveloppe et de niveau.
constexpr float kCentroidRelTol = 0.02f;
constexpr float kCentroidFloor = 1.0f;
// Ratio stéréo : 0 pour une machine mono, ~0,9 pour une machine à chorus
// stéréo -- un plancher large suffit à détecter le passage de l'un à l'autre.
constexpr float kStereoTol = 0.05f;
constexpr float kStereoFloor = 0.01f;

// Profil spectral en 16 bandes (demi-octaves, 30 Hz -> 19,2 kHz). C'est la
// partie DISCRIMINANTE de l'empreinte : le pic et le RMS ne bougent
// quasiment pas quand un filtre change de fréquence de coupure (mesuré :
// +10 % de cutoff sur LadderFilterZDF ne déplace le RMS du Minimoog que de
// 0,3 %), alors que la répartition de l'énergie par bande, elle, bouge tout
// de suite. Sans ce profil, ces tests laisseraient passer précisément le
// genre de dérive qu'ils sont censés attraper.
// 16 bandes d'environ une demi-octave chacune, de 30 Hz à 19,2 kHz. La
// finesse compte : avec des bandes d'une octave entière, un déplacement de
// fréquence de coupure de 10 % restait invisible sur la plupart des machines
// (mesuré), l'énergie ne changeant pas assez de bande. À une demi-octave, le
// flanc du filtre traverse une frontière de bande dès qu'il bouge un peu.
constexpr int kNumBands = 16;
constexpr double kBandMinHz = 30.0;
constexpr double kBandMaxHz = 19200.0;
constexpr int kProbesPerBand = 5;

double bandEdgeHz(int b) {
    return kBandMinHz * std::pow(kBandMaxHz / kBandMinHz, static_cast<double>(b) / kNumBands);
}

struct Fingerprint {
    float peak = 0.0f;
    float rms = 0.0f;
    float windowRms[kNumWindows] = {};
    float zeroCrossingRate = 0.0f;
    float stereoDiffRatio = 0.0f;
    float bandEnergy[kNumBands] = {};  // fractions de l'énergie totale, somme = 1
    float spectralCentroidHz = 0.0f;   // centroïde GÉOMÉTRIQUE (moyenne pondérée en log-fréquence)
};

struct Reference {
    const char* pluginId;
    Fingerprint fp;
};

MidiNoteEvent noteOn(int offset, uint8_t note, uint8_t velocity) {
    return {MidiNoteEvent::Kind::NoteOn, offset, 0, note, velocity};
}
MidiNoteEvent noteOff(int offset, uint8_t note) {
    return {MidiNoteEvent::Kind::NoteOff, offset, 0, note, 0};
}

/// Phrase mélodique commune à toutes les machines à hauteur : une note grave,
/// puis une note accentuée (vélocité 127 -- fait réagir le TB-303), puis une
/// note QUI CHEVAUCHE la précédente (fait réagir le slide du TB-303, la
/// duophonie de l'ARP, la polyphonie du Juno/Prophet/Jupiter/DX7), enfin un
/// silence assez long pour capturer la queue de release.
std::vector<MidiNoteEvent> melodicPhrase() {
    return {
        noteOn(0, 48, 100),
        noteOff(12000, 48), noteOn(12000, 55, 127),
        noteOn(24000, 60, 90),
        noteOff(36000, 55),
        noteOff(48000, 60),
    };
}

/// Phrase de batterie (les boîtes à rythmes ignorent les notes non mappées et
/// les note-off) : kick / charleston / caisse claire, dont un charleston
/// ouvert juste après un fermé pour exercer le choke.
std::vector<MidiNoteEvent> drumPhrase() {
    return {
        noteOn(0, 36, 110),
        noteOn(6000, 42, 90),
        noteOn(12000, 38, 100),
        noteOn(18000, 42, 90),
        noteOn(24000, 36, 110),
        noteOn(30000, 46, 100),
        noteOn(36000, 42, 90),
        noteOn(42000, 38, 120),
    };
}

/// Le sampler joue lui aussi la phrase rythmique : ses emplacements sont
/// mappés par défaut sur les notes de batterie General MIDI, que la phrase
/// mélodique n'atteindrait jamais.
bool isDrumMachine(const std::string& id) {
    return id == "vsm.tr808" || id == "vsm.tr909" || id == "vsm.sampler";
}

float rmsOf(const std::vector<float>& b, size_t from, size_t count) {
    double acc = 0.0;
    for (size_t i = from; i < from + count; ++i) acc += static_cast<double>(b[i]) * b[i];
    return static_cast<float>(std::sqrt(acc / static_cast<double>(count)));
}

/// Énergie du signal à une fréquence donnée, par corrélation directe avec un
/// phaseur complexe (DFT à une seule fréquence). Écrit ici plutôt que
/// réutilisé depuis vsm::audio::dsp : un outil d'analyse qui partagerait ses
/// briques avec le code analysé ne verrait pas une régression DANS ces
/// briques. Le phaseur est renormalisé périodiquement pour que son module ne
/// dérive pas sur 1,5 s d'échantillons.
double probeEnergy(const std::vector<float>& x, double frequencyHz) {
    const double pi = std::acos(-1.0);
    const double w = 2.0 * pi * frequencyHz / kSampleRate;
    const double c = std::cos(w), s = std::sin(w);
    double phasorRe = 1.0, phasorIm = 0.0, accRe = 0.0, accIm = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        accRe += static_cast<double>(x[i]) * phasorRe;
        accIm += static_cast<double>(x[i]) * phasorIm;
        const double nextRe = phasorRe * c - phasorIm * s;
        const double nextIm = phasorRe * s + phasorIm * c;
        phasorRe = nextRe;
        phasorIm = nextIm;
        if ((i & 4095u) == 4095u) {
            const double m = std::sqrt(phasorRe * phasorRe + phasorIm * phasorIm);
            phasorRe /= m;
            phasorIm /= m;
        }
    }
    const double n = static_cast<double>(x.size());
    return (accRe * accRe + accIm * accIm) / (n * n);
}

/// Répartition de l'énergie en 8 bandes + centroïde géométrique. Chaque bande
/// est sondée à kProbesPerBand fréquences réparties géométriquement à
/// l'intérieur d'elle (une sonde unique serait trop sensible au fait de tomber
/// ou non exactement sur une harmonique). Le centroïde réutilise ces mêmes
/// sondes, pondérées en log-fréquence -- l'échelle qui correspond à la
/// perception, et celle sur laquelle un déplacement de coupure se lit
/// proportionnellement.
void computeSpectrum(const std::vector<float>& x, float (&bandsOut)[kNumBands], float& centroidOut) {
    double bands[kNumBands] = {};
    double total = 0.0, logWeighted = 0.0;
    for (int b = 0; b < kNumBands; ++b) {
        const double lo = bandEdgeHz(b), hi = bandEdgeHz(b + 1);
        double acc = 0.0;
        for (int k = 0; k < kProbesPerBand; ++k) {
            const double t = (static_cast<double>(k) + 0.5) / kProbesPerBand;
            const double f = lo * std::pow(hi / lo, t);
            const double e = probeEnergy(x, f);
            acc += e;
            logWeighted += e * std::log(f);
        }
        bands[b] = acc / kProbesPerBand;
        total += bands[b];
    }
    const double energySum = total * kProbesPerBand; // somme brute des sondes
    for (int b = 0; b < kNumBands; ++b)
        bandsOut[b] = total > 0.0 ? static_cast<float>(bands[b] / total) : 0.0f;
    centroidOut = energySum > 0.0 ? static_cast<float>(std::exp(logWeighted / energySum)) : 0.0f;
}

/// Préparation propre à certaines machines, avant le rendu de l'empreinte.
///
/// Le sampler ne produit RIEN sans échantillon : son empreinte ne mesurerait
/// que du silence, et ne protégerait donc pas sa lecture (interpolation,
/// conversion de fréquence, enveloppe). On lui charge un échantillon
/// synthétique DÉTERMINISTE -- généré ici, jamais lu depuis un fichier, pour
/// que le test ne dépende d'aucune donnée externe.
void prepareMachineForFingerprint(const std::string& pluginId, vsm::audio::plugin::ISynthPlugin& plugin) {
    if (pluginId == "vsm.multisample") {
        // Même raison que le sampler, et une exigence de plus : l'empreinte doit
        // exercer ce qui distingue CETTE machine -- le choix de zone selon la
        // note, le choix de couche selon la vélocité, le repitch et la boucle de
        // tenue. Le profil est donc engendré ici, avec deux zones de notes, deux
        // couches de vélocité de part et d'autre de 100 (la phrase mélodique joue
        // 100, 127 et 90, donc les deux couches servent), et une boucle calée sur
        // un nombre ENTIER de périodes.
        //
        // ENGENDRÉ, JAMAIS LU : le cahier des charges de la machine interdit que
        // l'empreinte dépende d'une banque téléchargée. Une empreinte qui exige
        // une installation n'est pas une empreinte, c'est une panne de CI.
        auto& machine = dynamic_cast<vsm::plugins::multisample::MultisampleSynth&>(plugin);
        auto profile = std::make_shared<vsm::plugins::multisample::LoadedProfile>();
        profile->name = "Empreinte";
        profile->attribution = "engendré par les tests";
        profile->programNames = {"Empreinte"};

        struct Decl { int lo, hi, root, loVel, hiVel; double frequency, damping, level; bool loop; };
        const Decl declarations[] = {
            {40, 59, 50,   1,  99, 150.0, 1.5, 0.30, true},
            {40, 59, 50, 100, 127, 150.0, 0.8, 0.62, true},
            {60, 79, 66,   1,  99, 375.0, 4.0, 0.30, false},
            {60, 79, 66, 100, 127, 375.0, 2.5, 0.62, false},
        };
        for (const auto& declaration : declarations) {
            vsm::plugins::multisample::LoadedZone zone;
            zone.lowNote = declaration.lo;
            zone.highNote = declaration.hi;
            zone.lowVelocity = declaration.loVel;
            zone.highVelocity = declaration.hiVel;
            zone.rootNote = declaration.root;
            zone.level = static_cast<float>(declaration.level);

            auto buffer = std::make_shared<vsm::audio::io::SampleBuffer>();
            buffer->sampleRate = kSampleRate;
            buffer->left.resize(9600);
            buffer->right.resize(9600);
            for (size_t i = 0; i < buffer->left.size(); ++i) {
                const double t = static_cast<double>(i) / kSampleRate;
                const double phase = 2.0 * std::acos(-1.0) * declaration.frequency * t;
                // Une harmonique trois discrète : de quoi rendre l'empreinte
                // spectrale sensible au filtre de timbre et à l'interpolation.
                const double value = (std::sin(phase) + 0.25 * std::sin(3.0 * phase))
                                     * std::exp(-declaration.damping * t);
                buffer->left[i] = static_cast<float>(value);
                // Canal droit très légèrement décalé : la largeur stéréo fait
                // partie de l'empreinte, et un lecteur qui perdrait le canal
                // droit passerait sinon inaperçu.
                buffer->right[i] = static_cast<float>(value * 0.92);
            }
            // NORMALISÉ à un pic de 1, comme l'est toute banque publiée. Sans
            // cela, la somme de deux notes tenues dépasserait la pleine échelle
            // et l'empreinte figerait un écrêtage plutôt qu'un rendu.
            float peak = 0.0f;
            for (float value : buffer->left) peak = std::max(peak, std::abs(value));
            if (peak > 0.0f) {
                const float normalisation = 1.0f / peak;
                for (auto& value : buffer->left) value *= normalisation;
                for (auto& value : buffer->right) value *= normalisation;
            }

            zone.sample = buffer;
            zone.relativePath = "empreinte.wav";
            if (declaration.loop) {
                // 150 Hz à 48 kHz : trois cent vingt trames par période. La
                // boucle couvre dix périodes exactes.
                zone.loopEnabled = true;
                zone.loopStart = 3200;
                zone.loopEnd = 6400;
            }
            profile->zones.push_back(std::move(zone));
        }
        machine.setProfile(std::move(profile));
        return;
    }

    if (pluginId != "vsm.sampler") return;

    auto& sampler = dynamic_cast<vsm::plugins::sampler::SamplerSynth&>(plugin);
    for (int slot = 0; slot < 4; ++slot) {
        auto buffer = std::make_shared<vsm::audio::io::SampleBuffer>();
        buffer->sampleRate = kSampleRate;
        buffer->left.resize(4000);
        // Une sinusoïde amortie par emplacement, de hauteur différente : de
        // quoi entendre une transposition ou une interpolation qui change.
        const double frequency = 110.0 * std::pow(2.0, static_cast<double>(slot) * 0.5);
        for (size_t i = 0; i < buffer->left.size(); ++i) {
            const double t = static_cast<double>(i) / kSampleRate;
            buffer->left[i] = static_cast<float>(std::sin(2.0 * std::acos(-1.0) * frequency * t) *
                                                  std::exp(-12.0 * t));
        }
        sampler.setSample(slot, buffer);
    }
}

/// Rejoue la phrase bloc par bloc (comme le ferait le thread audio) et réduit
/// le résultat à une empreinte. Vérifie au passage qu'aucun échantillon n'est
/// NaN/inf et qu'aucune machine ne dépasse une amplitude aberrante.
Fingerprint renderFingerprint(const std::string& pluginId) {
    auto synth = PluginRegistry::instance().create(pluginId);
    VSM_ASSERT(synth != nullptr);
    synth->initialize(kSampleRate, kBlockSize);
    prepareMachineForFingerprint(pluginId, *synth);

    const auto events = isDrumMachine(pluginId) ? drumPhrase() : melodicPhrase();

    std::vector<float> left(static_cast<size_t>(kTotalSamples), 0.0f);
    std::vector<float> right(static_cast<size_t>(kTotalSamples), 0.0f);
    std::vector<MidiNoteEvent> blockEvents;
    blockEvents.reserve(events.size());

    for (int start = 0; start < kTotalSamples; start += kBlockSize) {
        blockEvents.clear();
        for (const auto& ev : events) {
            if (ev.sampleOffset >= start && ev.sampleOffset < start + kBlockSize) {
                MidiNoteEvent local = ev;
                local.sampleOffset = ev.sampleOffset - start;
                blockEvents.push_back(local);
            }
        }
        synth->process(blockEvents.empty() ? nullptr : blockEvents.data(),
                       static_cast<int>(blockEvents.size()),
                       left.data() + start, right.data() + start, kBlockSize);
    }

    Fingerprint fp;
    size_t stereoDiffs = 0, crossings = 0;
    for (size_t i = 0; i < left.size(); ++i) {
        VSM_ASSERT(std::isfinite(left[i]) && std::isfinite(right[i]));
        fp.peak = std::max(fp.peak, std::max(std::abs(left[i]), std::abs(right[i])));
        if (std::abs(left[i] - right[i]) > 1e-6f) ++stereoDiffs;
        if (i > 0 && ((left[i - 1] < 0.0f) != (left[i] < 0.0f))) ++crossings;
    }
    VSM_ASSERT(fp.peak < 8.0f); // garde-fou : une machine qui explose se voit tout de suite

    fp.rms = rmsOf(left, 0, left.size());
    const size_t windowSize = left.size() / kNumWindows;
    for (int w = 0; w < kNumWindows; ++w)
        fp.windowRms[w] = rmsOf(left, static_cast<size_t>(w) * windowSize, windowSize);
    fp.zeroCrossingRate = static_cast<float>(crossings) / static_cast<float>(left.size());
    fp.stereoDiffRatio = static_cast<float>(stereoDiffs) / static_cast<float>(left.size());
    computeSpectrum(left, fp.bandEnergy, fp.spectralCentroidHz);
    return fp;
}

bool regenMode() { return std::getenv("VSM_REGEN_AUDIO_FINGERPRINTS") != nullptr; }

void printReferenceLine(const std::string& pluginId, const Fingerprint& fp) {
    std::printf("    {\"%s\", {%.6ff, %.6ff, {", pluginId.c_str(), fp.peak, fp.rms);
    for (int w = 0; w < kNumWindows; ++w)
        std::printf("%.6ff%s", fp.windowRms[w], w + 1 < kNumWindows ? ", " : "");
    std::printf("}, %.6ff, %.6ff, {", fp.zeroCrossingRate, fp.stereoDiffRatio);
    for (int b = 0; b < kNumBands; ++b)
        std::printf("%.6ff%s", fp.bandEnergy[b], b + 1 < kNumBands ? ", " : "");
    std::printf("}, %.3ff}},\n", fp.spectralCentroidHz);
}

// --- Table de référence (régénérable, voir en-tête du fichier) --------------
const std::vector<Reference>& references() {
    static const std::vector<Reference> table = {
#include "audio_fingerprints.inc"
    };
    return table;
}

const Fingerprint* referenceFor(const std::string& pluginId) {
    for (const auto& r : references())
        if (pluginId == r.pluginId) return &r.fp;
    return nullptr;
}

void expectClose(const char* what, float actual, float expected, float relTol, float absFloor) {
    const float diff = std::abs(actual - expected);
    if (diff <= absFloor) return;
    const float allowed = relTol * std::max(std::abs(expected), absFloor);
    if (diff > allowed) {
        std::ostringstream oss;
        oss << "régression audio sur " << what << " : attendu " << expected << ", obtenu " << actual
            << " (écart " << (100.0f * diff / std::max(std::abs(expected), absFloor))
            << " %, toléré " << (100.0f * relTol) << " %)";
        throw vsm::test::AssertionFailure(oss.str());
    }
}

/// Coeur partagé par les 12 tests par machine.
void checkMachine(const char* pluginId) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered(pluginId));
    const Fingerprint fp = renderFingerprint(pluginId);

    if (regenMode()) {
        printReferenceLine(pluginId, fp);
        return;
    }

    const Fingerprint* ref = referenceFor(pluginId);
    if (ref == nullptr)
        throw vsm::test::AssertionFailure(std::string("aucune empreinte de référence pour ") + pluginId
                                          + " -- régénérer avec VSM_REGEN_AUDIO_FINGERPRINTS=1");

    expectClose("le pic", fp.peak, ref->peak, kRelTol, kAbsFloor);
    expectClose("le RMS global", fp.rms, ref->rms, kRelTol, kAbsFloor);
    for (int w = 0; w < kNumWindows; ++w) {
        const std::string label = "le RMS de la fenêtre " + std::to_string(w);
        expectClose(label.c_str(), fp.windowRms[w], ref->windowRms[w], kRelTol, kAbsFloor);
    }
    expectClose("le taux de passages par zéro", fp.zeroCrossingRate, ref->zeroCrossingRate, kZcrRelTol, kZcrFloor);
    expectClose("le ratio stéréo", fp.stereoDiffRatio, ref->stereoDiffRatio, kStereoTol, kStereoFloor);
    for (int b = 0; b < kNumBands; ++b) {
        const std::string label = "l'énergie de la bande " + std::to_string(static_cast<int>(bandEdgeHz(b)))
                                + "-" + std::to_string(static_cast<int>(bandEdgeHz(b + 1))) + " Hz";
        expectClose(label.c_str(), fp.bandEnergy[b], ref->bandEnergy[b], kBandRelTol, kBandFloor);
    }
    expectClose("le centroïde spectral", fp.spectralCentroidHz, ref->spectralCentroidHz, kCentroidRelTol, kCentroidFloor);
}

} // namespace

VSM_TEST(regression_minimoog)   { checkMachine("vsm.minimoog"); }
VSM_TEST(regression_tb303)      { checkMachine("vsm.tb303"); }
VSM_TEST(regression_juno106)    { checkMachine("vsm.juno106"); }
VSM_TEST(regression_tr808)      { checkMachine("vsm.tr808"); }
VSM_TEST(regression_tr909)      { checkMachine("vsm.tr909"); }
VSM_TEST(regression_sh101)      { checkMachine("vsm.sh101"); }
VSM_TEST(regression_prophet)    { checkMachine("vsm.prophet"); }
VSM_TEST(regression_jupiter8)   { checkMachine("vsm.jupiter8"); }
VSM_TEST(regression_arpodyssey) { checkMachine("vsm.arpodyssey"); }
VSM_TEST(regression_ms20)       { checkMachine("vsm.ms20"); }
VSM_TEST(regression_dx7)        { checkMachine("vsm.dx7"); }
VSM_TEST(regression_testtone)   { checkMachine("vsm.testtone"); }
VSM_TEST(regression_sampler)    { checkMachine("vsm.sampler"); }
VSM_TEST(regression_epiano)     { checkMachine("vsm.epiano"); }
VSM_TEST(regression_obx)        { checkMachine("vsm.obx"); }
VSM_TEST(regression_supersaw)   { checkMachine("vsm.supersaw"); }
VSM_TEST(regression_wavetable)  { checkMachine("vsm.wavetable"); }
VSM_TEST(regression_pcmhybrid)  { checkMachine("vsm.pcmhybrid"); }
VSM_TEST(regression_multisample){ checkMachine("vsm.multisample"); }
VSM_TEST(regression_tonewheel)  { checkMachine("vsm.tonewheel"); }
VSM_TEST(regression_generic)    { checkMachine("vsm.generic"); }
VSM_TEST(regression_string)     { checkMachine("vsm.string"); }
VSM_TEST(regression_piano)      { checkMachine("vsm.piano"); }
VSM_TEST(regression_drums)      { checkMachine("vsm.drums"); }
VSM_TEST(regression_wind)       { checkMachine("vsm.wind"); }
VSM_TEST(regression_perc)       { checkMachine("vsm.perc"); }

/// Le rendu doit être reproductible À L'IDENTIQUE d'une instance à l'autre :
/// c'est la condition pour que l'empreinte ci-dessus ait un sens. Vérifié ici
/// sur une machine à RNG (SH-101 : bruit + LFO sample & hold) plutôt que sur
/// un cas facile.
VSM_TEST(regression_fingerprints_are_reproducible) {
    const Fingerprint a = renderFingerprint("vsm.sh101");
    const Fingerprint b = renderFingerprint("vsm.sh101");
    // Epsilon minuscule = égalité bit-à-bit en pratique, écrit comme les autres
    // tests de déterminisme du projet (VSM_ASSERT_NEAR plutôt que == sur des
    // flottants, pour rester compatible avec -Wfloat-equal).
    VSM_ASSERT_NEAR(a.peak, b.peak, 1e-9);
    VSM_ASSERT_NEAR(a.rms, b.rms, 1e-9);
    for (int w = 0; w < kNumWindows; ++w) VSM_ASSERT_NEAR(a.windowRms[w], b.windowRms[w], 1e-9);
    VSM_ASSERT_NEAR(a.zeroCrossingRate, b.zeroCrossingRate, 1e-9);
    for (int i = 0; i < kNumBands; ++i) VSM_ASSERT_NEAR(a.bandEnergy[i], b.bandEnergy[i], 1e-9);
    VSM_ASSERT_NEAR(a.spectralCentroidHz, b.spectralCentroidHz, 1e-6);
}

/// Garde-fou de DISCIPLINE : ajouter une machine sans lui donner d'empreinte
/// de référence fait échouer ce test (et non passer silencieusement). C'est
/// l'étape "batterie de tests" du GUIDE-ajout-machine.
VSM_TEST(regression_every_registered_machine_has_a_reference) {
    for (const auto& [id, displayName] : PluginRegistry::instance().listAvailable()) {
        if (id.rfind("vsm.", 0) != 0) continue; // plugins factices des tests (ex. "test.dummy")
        if (referenceFor(id) != nullptr) continue;
        if (regenMode()) { std::printf("// machine sans référence : %s (%s)\n", id.c_str(), displayName.c_str()); continue; }
        throw vsm::test::AssertionFailure("machine enregistrée sans empreinte de référence : " + id
                                          + " -- régénérer avec VSM_REGEN_AUDIO_FINGERPRINTS=1");
    }
}
