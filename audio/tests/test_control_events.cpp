#include "TestFramework.h"
#include "vsm/audio/engine/OfflineRenderer.h"
#include "vsm/audio/engine/ProcessGraph.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include "vsm/sequencer/Project.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace vsm::sequencer;
using namespace vsm::audio::engine;
using namespace vsm::audio::plugin;

// D0.5 de docs/ROADMAP-daw.md — LE MIDI QUI N'EST PAS UNE NOTE.
//
// Le modèle d'édition connaît quatorze types d'événements, le planificateur les
// ordonne, l'écriture SMF les exporte. Le moteur, lui, n'en reconnaissait que
// deux : `MidiNoteEvent` n'a que NoteOn et NoteOff, et tout le reste retournait
// `false` dans un `std::visit` avant d'être jeté. Un pitch bend écrit dans un
// projet était donc lu, stocké, sauvegardé, exporté -- et n'atteignait jamais
// un instrument. Le projet contenait des données qui ne sonnaient pas.

namespace {

/// Hauteur du FONDAMENTAL, par autocorrélation (lags de 50 Hz à 2 kHz).
///
/// La première version comptait les passages par zéro, et elle mentait sur le
/// Juno-106 : son VCF coupe à 1 200 Hz, monter la note de deux demi-tons
/// pousse des harmoniques HORS de la bande, et le compte de passages BAISSE
/// alors que la hauteur monte (mesuré : 766 -> 760 « Hz » pour un bend de
/// +2 demi-tons qui, sondé à l'octave, fonctionne parfaitement). Un compte
/// d'harmoniques n'est pas une hauteur ; l'autocorrélation, si.
double hauteurApprochee(const std::vector<float>& signal, double sampleRate) {
    // L'attaque (enveloppe, transitoire) est écartée : elle brouille les lags.
    const size_t debut = std::min(signal.size() / 4, static_cast<size_t>(sampleRate * 0.1));
    const size_t n = signal.size() - debut;
    const float* s = signal.data() + debut;
    const auto lagMin = static_cast<size_t>(sampleRate / 2000.0);
    const auto lagMax = std::min(n / 2, static_cast<size_t>(sampleRate / 50.0));
    if (lagMax <= lagMin) return 0.0;
    double energie = 0.0;
    for (size_t i = 0; i < n; ++i) energie += static_cast<double>(s[i]) * s[i];
    if (energie <= 0.0) return 0.0;
    double meilleur = -1.0;
    size_t meilleurLag = lagMin;
    for (size_t lag = lagMin; lag <= lagMax; ++lag) {
        double somme = 0.0;
        for (size_t i = 0; i + lag < n; ++i)
            somme += static_cast<double>(s[i]) * s[i + lag];
        if (somme > meilleur) { meilleur = somme; meilleurLag = lag; }
    }
    return sampleRate / static_cast<double>(meilleurLag);
}

/// Fait sonner une machine une demi-seconde, avec ou sans molette de hauteur.
std::vector<float> rendreUneNote(const std::string& machineId, float bendSemitones) {
    vsm::audio::plugin::registerBuiltInPlugins();
    auto plugin = PluginRegistry::instance().create(machineId);
    VSM_ASSERT(plugin != nullptr);

    constexpr double kSampleRate = 48000.0;
    constexpr int kBlock = 512;
    plugin->initialize(kSampleRate, kBlock);

    if (bendSemitones != 0.0f) {
        MidiControlEvent bend;
        bend.kind = MidiControlEvent::Kind::PitchBend;
        bend.value = bendSemitones;
        VSM_ASSERT(plugin->handleControlEvent(bend));
    }

    MidiNoteEvent note;
    note.kind = MidiNoteEvent::Kind::NoteOn;
    note.sampleOffset = 0;
    note.note = 69;          // La 440
    note.velocity = 100;

    std::vector<float> gauche(static_cast<size_t>(kSampleRate * 0.5), 0.0f);
    std::vector<float> droite(gauche.size(), 0.0f);
    for (size_t i = 0; i + kBlock <= gauche.size(); i += kBlock)
        plugin->process(i == 0 ? &note : nullptr, i == 0 ? 1 : 0,
                        gauche.data() + i, droite.data() + i, kBlock);
    return gauche;
}

Project projetAvecPitchBend(const std::string& machineId) {
    Project project;
    project.ticksPerQuarterNote = 480;
    Track track;
    track.name = "Essai";
    track.instrumentId = machineId;
    uint64_t ids = 1;
    track.addNote(0, 960, 69, 100, 0, ids);
    // Une molette poussée à fond, au deuxième temps.
    track.pitchBends.push_back({240, 0, 8191});
    project.tracks.push_back(track);
    return project;
}

} // namespace

VSM_TEST(a_pitch_bend_actually_changes_what_a_machine_plays) {
    // Les cinq machines à voix unique ou double du parc — celles dont la
    // molette fait le style de jeu — puis les polyphoniques, équipées machine
    // par machine (la case du § 10 de CDC-nouvelle-machine.md).
    for (const char* machine : {"vsm.minimoog", "vsm.tb303", "vsm.sh101", "vsm.ms20", "vsm.arpodyssey",
                                "vsm.juno106", "vsm.jupiter8", "vsm.prophet", "vsm.obx", "vsm.supersaw",
                                "vsm.dx7", "vsm.wavetable", "vsm.pcmhybrid", "vsm.generic",
                                "vsm.phasedist", "vsm.additive", "vsm.westcoast", "vsm.vocal",
                                "vsm.string", "vsm.wind", "vsm.psg", "vsm.stochastic", "vsm.cone"}) {
        const auto nu = rendreUneNote(machine, 0.0f);
        const auto bende = rendreUneNote(machine, 2.0f);   // deux demi-tons vers le haut

        const double hauteurNue = hauteurApprochee(nu, 48000.0);
        const double hauteurBendee = hauteurApprochee(bende, 48000.0);
        // La machine fautive est NOMMÉE : un échec anonyme dans une boucle de
        // vingt machines ne dit rien de ce qu'il faut réparer.
        if (!(hauteurNue > 50.0) || !(hauteurBendee > hauteurNue * 1.05))
            std::printf("      [%s] hauteur nue %.1f Hz, bendée %.1f Hz\n",
                        machine, hauteurNue, hauteurBendee);
        VSM_ASSERT(hauteurNue > 50.0);                     // sinon le test ne prouve rien
        VSM_ASSERT(hauteurBendee > hauteurNue * 1.05);     // deux demi-tons = +12,2 %
    }
}

VSM_TEST(a_machine_that_ignores_a_control_event_says_so_instead_of_pretending) {
    vsm::audio::plugin::registerBuiltInPlugins();
    // Une boîte à rythmes n'a que faire d'une molette de hauteur, et c'est un
    // choix légitime -- mais elle doit le DIRE, pour que le moteur puisse
    // expliquer pourquoi la modulation ne s'entend pas.
    auto boite = PluginRegistry::instance().create("vsm.tr808");
    VSM_ASSERT(boite != nullptr);
    MidiControlEvent bend;
    bend.kind = MidiControlEvent::Kind::PitchBend;
    bend.value = 2.0f;
    VSM_ASSERT(!boite->handleControlEvent(bend));
}

VSM_TEST(the_graph_delivers_control_events_and_counts_those_nobody_wanted) {
    // Machine qui écoute : rien n'est compté comme ignoré.
    {
        ProcessGraph graph;
        graph.prepare(48000.0, 512);
        graph.setTrackInstrument(0, "vsm.minimoog");
        graph.setProject(projetAvecPitchBend("vsm.minimoog"));
        OfflineRenderer::render(graph, 48000.0, 512, 1.0);
        VSM_ASSERT_EQ(graph.ignoredControlEvents(), uint64_t(0));
        VSM_ASSERT_EQ(graph.droppedNoteEvents(), uint64_t(0));
    }
    // Machine qui n'écoute pas : le moteur le sait et le compte. C'est la
    // différence entre une machine qui ignore -- son droit -- et un moteur qui
    // jette, ce qu'il faisait.
    {
        ProcessGraph graph;
        graph.prepare(48000.0, 512);
        graph.setTrackInstrument(0, "vsm.tr808");
        graph.setProject(projetAvecPitchBend("vsm.tr808"));
        OfflineRenderer::render(graph, 48000.0, 512, 1.0);
        VSM_ASSERT(graph.ignoredControlEvents() > 0);
    }
}

VSM_TEST(a_pitch_bend_survives_the_whole_chain_from_the_project_to_the_sound) {
    // Le trajet complet : une courbe écrite dans le projet, rendue par le
    // graphe, et qui S'ENTEND. C'est le critère de la phase, et il tombe si
    // n'importe quel maillon relâche l'événement.
    ProcessGraph avec;
    avec.prepare(48000.0, 512);
    avec.setTrackInstrument(0, "vsm.minimoog");
    avec.setProject(projetAvecPitchBend("vsm.minimoog"));
    const auto rendu = OfflineRenderer::render(avec, 48000.0, 512, 1.0);

    Project sansBend = projetAvecPitchBend("vsm.minimoog");
    sansBend.tracks[0].pitchBends.clear();
    ProcessGraph sans;
    sans.prepare(48000.0, 512);
    sans.setTrackInstrument(0, "vsm.minimoog");
    sans.setProject(sansBend);
    const auto temoin = OfflineRenderer::render(sans, 48000.0, 512, 1.0);

    // La seconde moitié du rendu est celle qui suit la molette.
    const size_t moitie = rendu.left.size() / 2;
    std::vector<float> finAvec(rendu.left.begin() + static_cast<long>(moitie), rendu.left.end());
    std::vector<float> finSans(temoin.left.begin() + static_cast<long>(moitie), temoin.left.end());
    VSM_ASSERT(hauteurApprochee(finAvec, 48000.0) > hauteurApprochee(finSans, 48000.0) * 1.05);
}

VSM_TEST(the_mod_wheel_adds_an_audible_vibrato_where_a_lfo_path_exists) {
    // CC 1 (molette de modulation) : elle DOSE un vibrato au LFO de la
    // machine, une demi-note à fond. Le critère est celui qui s'entend : le
    // rendu avec molette diffère du rendu sans, sur les machines qui ont un
    // chemin LFO -> hauteur ; les autres refusent l'événement en le disant.
    vsm::audio::plugin::registerBuiltInPlugins();
    for (const char* machine : {"vsm.sh101", "vsm.ms20", "vsm.arpodyssey",
                                "vsm.juno106", "vsm.jupiter8", "vsm.prophet", "vsm.obx",
                                "vsm.supersaw", "vsm.wavetable", "vsm.pcmhybrid",
                                "vsm.generic", "vsm.dx7", "vsm.wind", "vsm.vocal"}) {
        auto rendre = [&](float wheel) {
            auto plugin = PluginRegistry::instance().create(machine);
            VSM_ASSERT(plugin != nullptr);
            plugin->initialize(48000.0, 512);
            if (wheel > 0.0f) {
                MidiControlEvent cc;
                cc.kind = MidiControlEvent::Kind::ControlChange;
                cc.index = 1;
                cc.value = wheel;
                if (!plugin->handleControlEvent(cc))
                    std::printf("      [%s] refuse le CC 1\n", machine);
                VSM_ASSERT(plugin->handleControlEvent(cc));
            }
            MidiNoteEvent note;
            note.kind = MidiNoteEvent::Kind::NoteOn;
            note.sampleOffset = 0;
            note.note = 69;
            note.velocity = 100;
            std::vector<float> gauche(24000, 0.0f), droite(24000, 0.0f);
            for (size_t i = 0; i + 512 <= gauche.size(); i += 512)
                plugin->process(i == 0 ? &note : nullptr, i == 0 ? 1 : 0,
                                gauche.data() + i, droite.data() + i, 512);
            return gauche;
        };
        const auto sans = rendre(0.0f);
        const auto avec = rendre(1.0f);
        float ecart = 0.0f;
        for (size_t i = 0; i < sans.size(); ++i)
            ecart = std::max(ecart, std::abs(sans[i] - avec[i]));
        if (!(ecart > 1.0e-4f))
            std::printf("      [%s] molette inaudible (écart max %.6f)\n", machine, ecart);
        VSM_ASSERT(ecart > 1.0e-4f);
    }
    // Et une machine sans chemin LFO -> hauteur refuse le CC 1 en le disant.
    auto moog = PluginRegistry::instance().create("vsm.minimoog");
    MidiControlEvent cc;
    cc.kind = MidiControlEvent::Kind::ControlChange;
    cc.index = 1;
    cc.value = 1.0f;
    VSM_ASSERT(!moog->handleControlEvent(cc));
}
