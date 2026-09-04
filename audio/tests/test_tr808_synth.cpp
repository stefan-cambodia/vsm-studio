#include "TestFramework.h"
#include "vsm/audio/plugin/BuiltInPlugins.h"
#include "vsm/audio/plugin/PluginRegistry.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vsm::audio::plugin;

namespace {

SynthPluginPtr makeTr808(double sr = 48000.0) {
    auto p = PluginRegistry::instance().create("vsm.tr808");
    p->initialize(sr, 512);
    return p;
}

float peakAbs(const std::vector<float>& b) {
    float p = 0.0f;
    for (float s : b) p = std::max(p, std::abs(s));
    return p;
}

MidiNoteEvent hit(int offset, uint8_t note, uint8_t vel = 110) {
    return MidiNoteEvent{MidiNoteEvent::Kind::NoteOn, offset, 9, note, vel};
}

ParamId paramByName(const ISynthPlugin& p, const std::string& n) {
    for (const auto& info : p.parameterList())
        if (info.name == n) return info.id;
    return 0;
}

} // namespace

VSM_TEST(tr808_registered) {
    VSM_ASSERT(PluginRegistry::instance().isRegistered("vsm.tr808"));
}

VSM_TEST(tr808_silent_with_no_events) {
    auto d = makeTr808();
    std::vector<float> l(512, 0.0f), r(512, 0.0f);
    d->process(nullptr, 0, l.data(), r.data(), 512);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
    VSM_ASSERT_EQ(d->activeVoiceCount(), 0);
}

VSM_TEST(tr808_kick_produces_low_frequency_hit) {
    auto d = makeTr808();
    MidiNoteEvent k = hit(0, 36); // kick
    std::vector<float> l(8000, 0.0f), r(8000, 0.0f);
    d->process(&k, 1, l.data(), r.data(), 8000);
    VSM_ASSERT(peakAbs(l) > 0.05f);
    for (float s : l) VSM_ASSERT(std::isfinite(s));
}

VSM_TEST(tr808_each_piece_triggers_independently) {
    // Chaque numéro de note déclenche sa pièce et produit du son.
    const uint8_t notes[] = {36, 38, 42, 46, 39, 56};
    for (uint8_t note : notes) {
        auto d = makeTr808();
        MidiNoteEvent e = hit(0, note);
        std::vector<float> l(6000, 0.0f), r(6000, 0.0f);
        d->process(&e, 1, l.data(), r.data(), 6000);
        VSM_ASSERT(peakAbs(l) > 0.02f);
    }
}

VSM_TEST(tr808_unmapped_note_is_silent) {
    auto d = makeTr808();
    MidiNoteEvent e = hit(0, 70); // non mappée
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
    d->process(&e, 1, l.data(), r.data(), 2000);
    VSM_ASSERT_NEAR(peakAbs(l), 0.0, 1e-6);
}

VSM_TEST(tr808_closed_hat_chokes_open_hat) {
    auto d = makeTr808(48000.0);
    // Ouvre le charleston, laisse-le sonner, puis frappe le fermé : l'ouvert
    // doit être coupé -> l'énergie après le choke doit chuter fortement.
    MidiNoteEvent openEv = hit(0, 46);
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f);
    d->process(&openEv, 1, l.data(), r.data(), 2000);
    double energyOpenAlone = 0.0;
    for (float s : l) energyOpenAlone += static_cast<double>(s) * s;

    auto d2 = makeTr808(48000.0);
    d2->process(&openEv, 1, l.data(), r.data(), 500); // ouvert sonne un peu
    MidiNoteEvent closedEv = hit(0, 42);
    std::vector<float> l2(2000, 0.0f), r2(2000, 0.0f);
    d2->process(&closedEv, 1, l2.data(), r2.data(), 2000);
    // Après le fermé, on regarde la traîne tardive (le fermé est court).
    double lateEnergy = 0.0;
    for (size_t i = 1000; i < l2.size(); ++i) lateEnergy += static_cast<double>(l2[i]) * l2[i];

    VSM_ASSERT(energyOpenAlone > 0.0);
    VSM_ASSERT(lateEnergy < energyOpenAlone * 0.25); // ouvert bien étouffé
}

VSM_TEST(tr808_simultaneous_voices) {
    auto d = makeTr808();
    std::vector<MidiNoteEvent> evs = {hit(0, 36), hit(0, 42), hit(0, 56)}; // kick+hat+cowbell
    std::vector<float> l(3000, 0.0f), r(3000, 0.0f);
    d->process(evs.data(), static_cast<int>(evs.size()), l.data(), r.data(), 3000);
    VSM_ASSERT(d->activeVoiceCount() >= 2);
}

VSM_TEST(tr808_is_deterministic) {
    auto render = [] {
        auto d = makeTr808();
        std::vector<MidiNoteEvent> evs = {hit(0, 36), hit(1200, 38), hit(2400, 42)};
        std::vector<float> l(6000, 0.0f), r(6000, 0.0f);
        d->process(evs.data(), static_cast<int>(evs.size()), l.data(), r.data(), 6000);
        return l;
    };
    auto a = render();
    auto b = render();
    for (size_t i = 0; i < a.size(); ++i) VSM_ASSERT_NEAR(a[i], b[i], 1e-9);
}

VSM_TEST(tr808_save_load_roundtrip) {
    auto a = makeTr808();
    ParamId kickTune = paramByName(*a, "Kick Tune");
    ParamId snappy = paramByName(*a, "Snare Snappy");
    a->setParameter(kickTune, 41.0f);
    a->setParameter(snappy, 0.9f);
    PresetState st = a->saveState();
    VSM_ASSERT_EQ(st.pluginTypeId, std::string("vsm.tr808"));

    auto b = makeTr808();
    b->loadState(st);
    VSM_ASSERT_NEAR(b->getParameter(kickTune), 41.0, 1e-2);
    VSM_ASSERT_NEAR(b->getParameter(snappy), 0.9, 1e-3);
}

VSM_TEST(tr808_parameter_list_size) {
    auto d = makeTr808();
    VSM_ASSERT_EQ(d->parameterList().size(), static_cast<size_t>(16));
}

// --------------------------------------------------------------------------
// D18.7 — LES SORTIES SÉPARÉES D'UNE BOÎTE À RYTHMES.
//
// Les huit voix arrivaient MIXÉES sur deux canaux : une reconstruction qui a
// séparé la grosse caisse de la caisse claire les recollait en les jouant.
// L'invariant qui rend l'addition sans risque : la SOMME des sorties séparées
// est ce que `process` rend, AU BIT PRÈS.
// --------------------------------------------------------------------------

VSM_TEST(the_sum_of_the_separate_outputs_is_bit_for_bit_what_process_renders) {
    vsm::audio::plugin::registerBuiltInPlugins();
    constexpr int kBloc = 512;
    constexpr int kBus = 6;

    auto machine = [] {
        auto p = PluginRegistry::instance().create("vsm.tr808");
        p->initialize(48000.0, kBloc);
        return p;
    };

    // Une mesure qui fait sonner PLUSIEURS pièces en même temps : c'est le cas
    // où la somme pourrait diverger, et donc le seul qui prouve quelque chose.
    std::vector<MidiNoteEvent> events;
    auto frappe = [&events](int note, int offset) {
        MidiNoteEvent e;
        e.kind = MidiNoteEvent::Kind::NoteOn;
        e.sampleOffset = offset;
        e.note = static_cast<uint8_t>(note);
        e.velocity = 100;
        events.push_back(e);
    };
    frappe(36, 0); frappe(42, 0); frappe(38, 0);      // grosse caisse + charley + caisse claire
    frappe(46, 64); frappe(39, 128); frappe(56, 200); // ouvert, clap, cloche

    auto ensemble = machine();
    std::vector<float> gauche(kBloc, 0.0f), droite(kBloc, 0.0f);
    ensemble->process(events.data(), static_cast<int>(events.size()),
                       gauche.data(), droite.data(), kBloc);

    auto separee = machine();
    VSM_ASSERT_EQ(separee->outputCount(), kBus);
    std::vector<std::vector<float>> busL(kBus, std::vector<float>(kBloc, 0.0f));
    std::vector<std::vector<float>> busR(kBus, std::vector<float>(kBloc, 0.0f));
    std::vector<float*> ptrL, ptrR;
    for (int b = 0; b < kBus; ++b) { ptrL.push_back(busL[b].data()); ptrR.push_back(busR[b].data()); }
    separee->processMultiOut(events.data(), static_cast<int>(events.size()),
                              ptrL.data(), ptrR.data(), kBus, kBloc);

    double pire = 0.0;
    float crete = 0.0f;
    for (int i = 0; i < kBloc; ++i) {
        float somme = 0.0f;
        for (int b = 0; b < kBus; ++b) somme += busL[b][i];
        pire = std::max(pire, std::abs(static_cast<double>(somme - gauche[i])));
        crete = std::max(crete, std::abs(gauche[i]));
    }
    std::printf("      [D18.7] somme des 6 sorties contre le mixage : ecart %.3e (crete %.4f)\n",
                pire, crete);
    VSM_ASSERT(crete > 0.01f);      // sinon le test ne prouve rien
    VSM_ASSERT(pire == 0.0);        // AU BIT PRÈS

    // ET CHAQUE SORTIE PORTE SA PIÈCE, pas le mixage : la grosse caisse sonne
    // sur la sienne, et la cloche n'y est pas.
    float creteKick = 0.0f, creteCloche = 0.0f;
    for (int i = 0; i < kBloc; ++i) {
        creteKick = std::max(creteKick, std::abs(busL[0][i]));
        creteCloche = std::max(creteCloche, std::abs(busL[5][i]));
    }
    VSM_ASSERT(creteKick > 0.001f);
    VSM_ASSERT(creteCloche > 0.001f);
    VSM_ASSERT(std::string(separee->outputName(0)) == "Grosse caisse");
}

VSM_TEST(a_machine_that_knows_only_one_output_renders_exactly_what_it_always_did) {
    // LE DÉFAUT DE L'INTERFACE : `processMultiOut` sur une machine qui ne
    // l'implémente pas doit être `process`, au bit près, et remplir de
    // silence les sorties qu'elle n'a pas.
    vsm::audio::plugin::registerBuiltInPlugins();
    constexpr int kBloc = 256;
    auto machine = [] {
        auto p = PluginRegistry::instance().create("vsm.minimoog");
        p->initialize(48000.0, kBloc);
        return p;
    };
    MidiNoteEvent note;
    note.kind = MidiNoteEvent::Kind::NoteOn;
    note.sampleOffset = 0;
    note.note = 48;
    note.velocity = 100;

    auto a = machine();
    VSM_ASSERT_EQ(a->outputCount(), 1);
    std::vector<float> gauche(kBloc, 0.0f), droite(kBloc, 0.0f);
    a->process(&note, 1, gauche.data(), droite.data(), kBloc);

    auto b = machine();
    std::vector<std::vector<float>> busL(3, std::vector<float>(kBloc, 1.0f));
    std::vector<std::vector<float>> busR(3, std::vector<float>(kBloc, 1.0f));
    std::vector<float*> ptrL{busL[0].data(), busL[1].data(), busL[2].data()};
    std::vector<float*> ptrR{busR[0].data(), busR[1].data(), busR[2].data()};
    b->processMultiOut(&note, 1, ptrL.data(), ptrR.data(), 3, kBloc);

    for (int i = 0; i < kBloc; ++i) {
        VSM_ASSERT(busL[0][i] == gauche[i]);
        VSM_ASSERT(busR[0][i] == droite[i]);
        VSM_ASSERT(busL[1][i] == 0.0f);   // les sorties qu'elle n'a pas sont MUETTES,
        VSM_ASSERT(busR[2][i] == 0.0f);   // et non laissées telles qu'on les a données
    }
}
