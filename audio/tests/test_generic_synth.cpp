#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

constexpr double kSampleRate = 48000.0;

SynthPluginPtr makeGeneric() {
    registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create("vsm.generic");
    if (plugin) plugin->initialize(kSampleRate, 512);
    return plugin;
}

ParamId byName(const ISynthPlugin& plugin, const std::string& name) {
    for (const auto& info : plugin.parameterList())
        if (info.name == name) return info.id;
    return 0;
}

std::vector<float> renderNote(const SynthPluginPtr& synth, uint8_t note,
                               int frames = 16000, uint8_t velocity = 100) {
    const MidiNoteEvent event{MidiNoteEvent::Kind::NoteOn, 0, 0, note, velocity};
    std::vector<float> left(static_cast<size_t>(frames), 0.0f), right(static_cast<size_t>(frames), 0.0f);
    synth->process(&event, 1, left.data(), right.data(), frames);
    return left;
}

float peakAbs(const std::vector<float>& b) {
    float peak = 0.0f;
    for (float sample : b) peak = std::max(peak, std::abs(sample));
    return peak;
}

double rms(const std::vector<float>& b, size_t from = 0) {
    double sum = 0.0;
    for (size_t i = from; i < b.size(); ++i) sum += static_cast<double>(b[i]) * b[i];
    return b.size() > from ? std::sqrt(sum / static_cast<double>(b.size() - from)) : 0.0;
}

/// Énergie haute fréquence, par les différences premières. On ne mesure PAS
/// l'énergie totale : la résonance en ajoute, ce qui fausserait la lecture.
double brightness(const std::vector<float>& b) {
    double sum = 0.0;
    for (size_t i = 1; i < b.size(); ++i) {
        const double d = static_cast<double>(b[i]) - b[i - 1];
        sum += d * d;
    }
    return sum;
}

/// Puissance à une fréquence donnée.
double powerAt(const std::vector<float>& b, double hz) {
    double real = 0.0, imaginary = 0.0;
    for (size_t i = 0; i < b.size(); ++i) {
        const double angle = 2.0 * 3.14159265358979323846 * hz * static_cast<double>(i) / kSampleRate;
        real += static_cast<double>(b[i]) * std::cos(angle);
        imaginary += static_cast<double>(b[i]) * std::sin(angle);
    }
    const double n = static_cast<double>(b.size());
    return (real * real + imaginary * imaginary) / (n * n);
}

/// Règle la machine dans son état NEUTRE : filtre grand ouvert, enveloppes
/// plates, aucune modulation, aucun drive. C'est l'état où elle doit être un
/// oscillateur nu et rien d'autre.
void makeNeutral(const SynthPluginPtr& synth) {
    for (const auto& info : synth->parameterList()) synth->setParameter(info.id, 0.0f);
    synth->setParameter(byName(*synth, "Osc1 Level"), 1.0f);
    synth->setParameter(byName(*synth, "Osc1 Pulse Width"), 0.5f);
    synth->setParameter(byName(*synth, "Filter Cutoff"), 18000.0f);
    synth->setParameter(byName(*synth, "Amp Attack"), 0.001f);
    synth->setParameter(byName(*synth, "Amp Sustain"), 1.0f);
    synth->setParameter(byName(*synth, "Amp Decay"), 8.0f);
    synth->setParameter(byName(*synth, "Amp Release"), 0.2f);
    synth->setParameter(byName(*synth, "Filter Sustain"), 1.0f);
    synth->setParameter(byName(*synth, "Output Level"), 1.0f);
}

} // namespace

VSM_TEST(generic_is_registered) {
    auto synth = makeGeneric();
    VSM_ASSERT(synth != nullptr);
    VSM_ASSERT_EQ(std::string(synth->machineName()), std::string("Generic Synth"));
    VSM_ASSERT(synth->parameterList().size() >= 35);
}

VSM_TEST(generic_produces_sound) {
    auto synth = makeGeneric();
    VSM_ASSERT(peakAbs(renderNote(synth, 60)) > 0.05f);
}

// --- EXIGENCE 1 : CONTINUITÉ -------------------------------------------------

VSM_TEST(generic_waveform_morph_has_no_step) {
    // L'exigence centrale de cette machine. Un sélecteur discret créerait une
    // falaise dans la fonction de coût, et une recherche par descente s'y
    // bloquerait. On balaie donc la forme finement et on vérifie qu'AUCUN pas
    // ne produit de saut plus grand que ses voisins.
    std::vector<double> niveaux;
    for (int pas = 0; pas <= 60; ++pas) {
        auto synth = makeGeneric();
        makeNeutral(synth);
        synth->setParameter(byName(*synth, "Osc1 Shape"), 3.0f * static_cast<float>(pas) / 60.0f);
        niveaux.push_back(brightness(renderNote(synth, 57, 8000)));
    }
    // Écart entre deux formes voisines, rapporté à l'écart médian : un palier
    // se verrait comme un écart des dizaines de fois plus grand.
    std::vector<double> ecarts;
    for (size_t i = 1; i < niveaux.size(); ++i) ecarts.push_back(std::abs(niveaux[i] - niveaux[i - 1]));
    std::vector<double> tries = ecarts;
    std::sort(tries.begin(), tries.end());
    const double median = tries[tries.size() / 2];
    const double maximum = tries.back();
    VSM_ASSERT(maximum < median * 12.0);
}

VSM_TEST(generic_filter_type_morph_has_no_step) {
    // Même exigence pour le type de filtre : passe-bas -> passe-bande ->
    // passe-haut doit être un fondu, pas trois cases.
    std::vector<double> graves;
    for (int pas = 0; pas <= 40; ++pas) {
        auto synth = makeGeneric();
        makeNeutral(synth);
        synth->setParameter(byName(*synth, "Osc1 Shape"), 2.0f); // dent de scie, riche
        synth->setParameter(byName(*synth, "Filter Cutoff"), 800.0f);
        synth->setParameter(byName(*synth, "Filter Type"), 2.0f * static_cast<float>(pas) / 40.0f);
        const auto rendu = renderNote(synth, 45, 8000);
        graves.push_back(powerAt(rendu, 110.0)); // la fondamentale du La2
    }
    // Écarts mesurés en ÉCHELLE LOGARITHMIQUE. La puissance décroît ici de
    // trois ordres de grandeur entre passe-bas et passe-haut ; comparés en
    // linéaire, les écarts du début écrasent ceux de la fin et un fondu
    // parfaitement lisse paraît accidenté. Une première version du test s'y
    // est laissé prendre.
    std::vector<double> ecarts;
    for (size_t i = 1; i < graves.size(); ++i)
        ecarts.push_back(std::abs(std::log(std::max(graves[i], 1e-18))
                                   - std::log(std::max(graves[i - 1], 1e-18))));
    std::vector<double> tries = ecarts;
    std::sort(tries.begin(), tries.end());
    VSM_ASSERT(tries.back() < tries[tries.size() / 2] * 12.0);

    // ...et le fondu va bien dans le bon sens : le passe-haut laisse passer
    // beaucoup moins de fondamentale que le passe-bas.
    VSM_ASSERT(graves.front() > graves.back() * 5.0);
}

// --- EXIGENCE 2 : NEUTRALITÉ AU REPOS ---------------------------------------

VSM_TEST(generic_at_neutral_settings_is_a_clean_oscillator) {
    // « Ce qui est réglé à zéro doit vraiment disparaître. » À réglages
    // neutres et forme sinusoïdale, la sortie doit être une sinusoïde PROPRE :
    // pas de dérive, pas de saturation résiduelle, pas de bruit ajouté.
    auto synth = makeGeneric();
    makeNeutral(synth);
    synth->setParameter(byName(*synth, "Osc1 Shape"), 0.0f); // sinus pur

    // La note 45 est le La2, à 110 Hz. Une première version employait la
    // note 57 -- qui est le La3, à 220 Hz -- et cherchait donc la
    // fondamentale une octave trop bas : le test voyait « 20 000 fois plus
    // d'harmonique 2 que de fondamentale », ce qui était la fondamentale
    // elle-même. L'analyse démarre après l'attaque, dont le transitoire est
    // large bande par nature.
    const auto rendu = renderNote(synth, 45, 16000);
    const std::vector<float> etabli(rendu.begin() + 2000, rendu.end());
    const double fondamentale = powerAt(etabli, 110.0);
    VSM_ASSERT(fondamentale > 0.0);

    // Aucun partiel parasite : les harmoniques 2 à 6 doivent être plus de
    // mille fois plus faibles que la fondamentale.
    for (int harmonique = 2; harmonique <= 6; ++harmonique) {
        const double partiel = powerAt(etabli, 110.0 * harmonique);
        VSM_ASSERT(partiel < fondamentale * 1e-3);
    }
}

VSM_TEST(generic_is_deterministic_to_the_sample) {
    // Machine de MESURE : deux rendus du même patch doivent être identiques
    // échantillon pour échantillon. La moindre variation ferait du bruit dans
    // la fonction de coût, que l'optimiseur prendrait pour un effet de ses
    // réglages.
    auto a = makeGeneric(), b = makeGeneric();
    makeNeutral(a); makeNeutral(b);
    const auto ra = renderNote(a, 62), rb = renderNote(b, 62);
    for (size_t i = 0; i < ra.size(); ++i) VSM_ASSERT_NEAR(ra[i], rb[i], 1e-12);
}

VSM_TEST(generic_zero_drive_is_exactly_transparent) {
    // À drive nul, la saturation doit être l'IDENTITÉ -- pas « presque ».
    auto sans = makeGeneric(), avec = makeGeneric();
    makeNeutral(sans); makeNeutral(avec);
    avec->setParameter(byName(*avec, "Drive"), 0.0f);
    const auto a = renderNote(sans, 60), b = renderNote(avec, 60);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-12);
}

VSM_TEST(generic_silent_sources_add_nothing) {
    // Sous-oscillateur et bruit à zéro : la sortie doit être RIGOUREUSEMENT
    // celle de l'oscillateur seul. Un résidu « pour le grain » rendrait le
    // paramètre non monotone près de zéro.
    auto nu = makeGeneric(), garni = makeGeneric();
    makeNeutral(nu); makeNeutral(garni);
    garni->setParameter(byName(*garni, "Sub Level"), 0.0f);
    garni->setParameter(byName(*garni, "Noise Level"), 0.0f);
    garni->setParameter(byName(*garni, "Osc2 Level"), 0.0f);
    const auto a = renderNote(nu, 60), b = renderNote(garni, 60);
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-12);
}

// --- EXIGENCE 3 : MONOTONIE --------------------------------------------------

VSM_TEST(generic_cutoff_is_monotone_in_brightness) {
    // Un paramètre non monotone piège toute recherche par descente : elle
    // s'arrête au premier retournement en croyant tenir un optimum.
    double precedent = -1.0;
    for (float coupure : {200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f, 12000.0f}) {
        auto synth = makeGeneric();
        makeNeutral(synth);
        synth->setParameter(byName(*synth, "Osc1 Shape"), 2.0f);
        synth->setParameter(byName(*synth, "Filter Cutoff"), coupure);
        const double clarte = brightness(renderNote(synth, 45, 8000));
        VSM_ASSERT(clarte > precedent);
        precedent = clarte;
    }
}

VSM_TEST(generic_resonance_is_monotone_in_band_emphasis) {
    // Monter la résonance doit accentuer la bande autour de la coupure, et
    // toujours dans le même sens.
    double precedent = -1.0;
    for (float resonance : {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f}) {
        auto synth = makeGeneric();
        makeNeutral(synth);
        synth->setParameter(byName(*synth, "Osc1 Shape"), 2.0f);
        synth->setParameter(byName(*synth, "Filter Cutoff"), 660.0f); // 6e harmonique du La2
        synth->setParameter(byName(*synth, "Filter Resonance"), resonance);
        const auto rendu = renderNote(synth, 45, 12000);
        // Énergie DANS la bande, rapportée à celle de la fondamentale :
        // c'est l'accentuation qu'on règle, pas le niveau général.
        const double accentuation = powerAt(rendu, 660.0) / std::max(1e-18, powerAt(rendu, 110.0));
        VSM_ASSERT(accentuation > precedent);
        precedent = accentuation;
    }
}

VSM_TEST(generic_drive_is_monotone_in_harmonics) {
    double precedent = -1.0;
    for (float drive : {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f}) {
        auto synth = makeGeneric();
        makeNeutral(synth);
        synth->setParameter(byName(*synth, "Osc1 Shape"), 0.0f); // sinus : tout harmonique vient du drive
        synth->setParameter(byName(*synth, "Drive"), drive);
        const auto rendu = renderNote(synth, 45, 12000);
        // Harmoniques 3 et 5, rapportées à la fondamentale : une saturation
        // symétrique n'engendre que les rangs impairs.
        const double impairs = (powerAt(rendu, 330.0) + powerAt(rendu, 550.0))
                             / std::max(1e-18, powerAt(rendu, 110.0));
        VSM_ASSERT(impairs > precedent);
        precedent = impairs;
    }
}

// --- EXIGENCE 4 : DÉCOUPLAGE -------------------------------------------------

VSM_TEST(generic_resonance_does_not_change_the_overall_level) {
    // Couplage inévitable, donc COMPENSÉ : sans correction, tourner la
    // résonance monterait aussi le volume, et l'optimiseur confondrait les
    // deux effets.
    double minimum = 1e9, maximum = 0.0;
    for (float resonance : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        auto synth = makeGeneric();
        makeNeutral(synth);
        synth->setParameter(byName(*synth, "Osc1 Shape"), 2.0f);
        synth->setParameter(byName(*synth, "Filter Cutoff"), 2000.0f);
        synth->setParameter(byName(*synth, "Filter Resonance"), resonance);
        const double niveau = rms(renderNote(synth, 45, 12000), 2000);
        minimum = std::min(minimum, niveau);
        maximum = std::max(maximum, niveau);
    }
    VSM_ASSERT(maximum < minimum * 2.0); // le niveau reste du même ordre
}

VSM_TEST(generic_drive_does_not_change_the_overall_level) {
    double minimum = 1e9, maximum = 0.0;
    for (float drive : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        auto synth = makeGeneric();
        makeNeutral(synth);
        synth->setParameter(byName(*synth, "Osc1 Shape"), 2.0f);
        synth->setParameter(byName(*synth, "Drive"), drive);
        const double niveau = rms(renderNote(synth, 45, 12000), 2000);
        minimum = std::min(minimum, niveau);
        maximum = std::max(maximum, niveau);
    }
    VSM_ASSERT(maximum < minimum * 2.0);
}

// --- couverture ordinaire ----------------------------------------------------

VSM_TEST(generic_stays_within_headroom_on_a_full_chord) {
    auto synth = makeGeneric();
    MidiNoteEvent events[8]{};
    const uint8_t notes[8] = {48, 52, 55, 59, 60, 64, 67, 71};
    for (int i = 0; i < 8; ++i)
        events[i] = MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, 0, 0, notes[i], 110};
    std::vector<float> left(24000, 0.0f), right(24000, 0.0f);
    synth->process(events, 8, left.data(), right.data(), 24000);
    VSM_ASSERT(peakAbs(left) < 1.0f);
    VSM_ASSERT(peakAbs(left) > 0.1f);
}

VSM_TEST(generic_sub_oscillator_adds_an_octave_below) {
    auto sans = makeGeneric(), avec = makeGeneric();
    makeNeutral(sans); makeNeutral(avec);
    avec->setParameter(byName(*avec, "Sub Level"), 0.8f);
    // Note 57 = La3, à 220 Hz : le sous-oscillateur sonne donc à 110 Hz, et
    // non à 55 -- l'octave EN DESSOUS de la note, pas deux octaves.
    const double sansSub = powerAt(renderNote(sans, 57, 12000), 110.0);
    const double avecSub = powerAt(renderNote(avec, 57, 12000), 110.0);
    VSM_ASSERT(avecSub > sansSub * 20.0);
}

VSM_TEST(generic_noise_colour_moves_energy_from_treble_to_bass) {
    auto blanc = makeGeneric(), rose = makeGeneric();
    for (auto* synth : {&blanc, &rose}) {
        makeNeutral(*synth);
        (*synth)->setParameter(byName(**synth, "Osc1 Level"), 0.0f);
        (*synth)->setParameter(byName(**synth, "Noise Level"), 1.0f);
    }
    blanc->setParameter(byName(*blanc, "Noise Colour"), 0.0f);
    rose->setParameter(byName(*rose, "Noise Colour"), 1.0f);
    VSM_ASSERT(brightness(renderNote(blanc, 60)) > brightness(renderNote(rose, 60)) * 2.0);
}

VSM_TEST(generic_state_round_trips) {
    auto synth = makeGeneric();
    synth->setParameter(byName(*synth, "Osc1 Shape"), 1.37f);
    synth->setParameter(byName(*synth, "Filter Type"), 1.62f);
    const auto state = synth->saveState();
    VSM_ASSERT_EQ(state.pluginTypeId, std::string("vsm.generic"));
    auto restored = makeGeneric();
    restored->loadState(state);
    VSM_ASSERT_NEAR(restored->getParameter(byName(*restored, "Osc1 Shape")), 1.37f, 1e-6);
    VSM_ASSERT_NEAR(restored->getParameter(byName(*restored, "Filter Type")), 1.62f, 1e-6);
}

VSM_TEST(generic_silent_with_no_events) {
    // Sans la moindre note, la sortie doit être STRICTEMENT nulle -- pas
    // « presque » nulle. Une machine neutre qui laisserait passer un souffle
    // de fond ajouterait un plancher que la recherche de patch prendrait pour
    // une caractéristique de la cible.
    auto synth = makeGeneric();
    std::vector<float> left(2048, 1.0f), right(2048, 1.0f);
    synth->process(nullptr, 0, left.data(), right.data(), 2048);
    // Tolérance NULLE : `VSM_ASSERT_NEAR(..., 0.0)` exige l'égalité exacte
    // sans réveiller `-Wfloat-equal`, que la compilation stricte traite en
    // erreur.
    for (size_t i = 0; i < left.size(); ++i) {
        VSM_ASSERT_NEAR(left[i], 0.0, 0.0);
        VSM_ASSERT_NEAR(right[i], 0.0, 0.0);
    }
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
}

VSM_TEST(generic_parameter_list_size) {
    // Verrouille le nombre de paramètres : en ajouter ou en retirer un doit
    // rester un geste conscient, parce que chaque paramètre traîne derrière
    // lui une identité sémantique, une place sur la façade et une empreinte.
    auto synth = makeGeneric();
    VSM_ASSERT_EQ(synth->parameterList().size(), static_cast<size_t>(40));
}

VSM_TEST(generic_is_polyphonic_and_steals_the_oldest_voice) {
    auto synth = makeGeneric();
    std::vector<MidiNoteEvent> accord;
    for (int n = 0; n < 4; ++n)
        accord.push_back(MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, n * 8, 0,
                                        static_cast<uint8_t>(48 + n * 3), 100});
    std::vector<float> left(2000, 0.0f), right(2000, 0.0f);
    synth->process(accord.data(), static_cast<int>(accord.size()), left.data(), right.data(), 2000);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 4);

    // Neuf notes distinctes sur huit voix : la plus ancienne est volée, et le
    // total ne dépasse jamais la polyphonie déclarée.
    auto sature = makeGeneric();
    std::vector<MidiNoteEvent> neuf;
    for (int n = 0; n < 9; ++n)
        neuf.push_back(MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, n * 8, 0,
                                      static_cast<uint8_t>(40 + n * 2), 100});
    sature->process(neuf.data(), static_cast<int>(neuf.size()), left.data(), right.data(), 2000);
    VSM_ASSERT_EQ(sature->activeVoiceCount(), 8);
}

VSM_TEST(generic_note_off_eventually_silences) {
    auto synth = makeGeneric();
    makeNeutral(synth);
    synth->setParameter(byName(*synth, "Amp Release"), 0.01f);

    const MidiNoteEvent on{MidiNoteEvent::Kind::NoteOn, 0, 0, 57, 100};
    std::vector<float> left(2000, 0.0f), right(2000, 0.0f);
    synth->process(&on, 1, left.data(), right.data(), 2000);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 1);

    const MidiNoteEvent off{MidiNoteEvent::Kind::NoteOff, 0, 0, 57, 0};
    synth->process(&off, 1, left.data(), right.data(), 2000);
    std::vector<float> silence(4000, 0.0f), silenceR(4000, 0.0f);
    synth->process(nullptr, 0, silence.data(), silenceR.data(), 4000);
    VSM_ASSERT_EQ(synth->activeVoiceCount(), 0);
    VSM_ASSERT_NEAR(peakAbs(silence), 0.0, 0.0);
}

VSM_TEST(generic_stays_finite_under_extreme_settings) {
    // Conditions extrêmes exigées par le cahier des charges : chaque
    // paramètre poussé à sa borne, notes aux deux bouts du clavier,
    // vélocités 1 et 127, et des blocs d'UN échantillon -- un bloc court
    // révèle les états qu'on ne remet à jour qu'en début de bloc.
    for (int extreme = 0; extreme < 2; ++extreme) {
        for (uint8_t note : {uint8_t{0}, uint8_t{24}, uint8_t{108}, uint8_t{127}}) {
            for (uint8_t velocity : {uint8_t{1}, uint8_t{127}}) {
                auto synth = makeGeneric();
                for (const auto& info : synth->parameterList())
                    synth->setParameter(info.id, extreme == 0 ? info.minValue : info.maxValue);

                const MidiNoteEvent on{MidiNoteEvent::Kind::NoteOn, 0, 0, note, velocity};
                float left = 0.0f, right = 0.0f;
                synth->process(&on, 1, &left, &right, 1);
                for (int i = 0; i < 8000; ++i) {
                    synth->process(nullptr, 0, &left, &right, 1);
                    VSM_ASSERT(std::isfinite(left) && std::isfinite(right));
                    VSM_ASSERT(std::abs(left) < 8.0f);
                }
            }
        }
    }
}
