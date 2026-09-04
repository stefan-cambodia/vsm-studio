#include "TestFramework.h"
#include "vsm/interchange/GroovePreset.h"
#include <cmath>

using namespace vsm::interchange;
using namespace vsm::sequencer;

// D17.8 de docs/ROADMAP-daw.md — LE GROOVE ENREGISTRÉ.
//
// Le fichier ne contient AUCUN tick : les écarts sont en fraction de pas, et le
// nombre de pas par mesure est dit. Le même fichier s'applique donc à un projet
// en 480 ppq comme en 960, à n'importe quel tempo.

VSM_TEST(a_groove_survives_the_round_trip_offsets_accents_and_silent_steps) {
    Groove groove;
    groove.name = "Balance du couplet";
    groove.stepsPerBar = 16;
    groove.steps.resize(16);
    groove.steps[0] = {0.0, 0.94f, true};
    groove.steps[1] = {0.31, 0.31f, true};
    groove.steps[2] = {-0.07, 0.5f, true};
    // Le troisième pas reste ABSENT : c'est ce que la partie d'origine disait.

    const auto relu = parseGroove(grooveToJson(groove).toString());
    VSM_ASSERT(relu.success);
    VSM_ASSERT_EQ(relu.groove.name, std::string("Balance du couplet"));
    VSM_ASSERT_EQ(relu.groove.stepsPerBar, 16);
    VSM_ASSERT_EQ(relu.groove.steps.size(), size_t(16));
    VSM_ASSERT_NEAR(relu.groove.steps[1].offset, 0.31, 1e-6);
    VSM_ASSERT_NEAR(relu.groove.steps[2].offset, -0.07, 1e-6);
    VSM_ASSERT_NEAR(relu.groove.steps[0].velocity, 0.94f, 1e-6f);
    VSM_ASSERT(relu.groove.steps[0].present);
    // UN PAS ABSENT RESTE ABSENT : lu comme « écart nul », il remettrait sur la
    // grille les notes que le groove devait laisser tranquilles.
    VSM_ASSERT(!relu.groove.steps[3].present);
    VSM_ASSERT(!relu.groove.steps[15].present);
}

VSM_TEST(a_file_that_is_not_a_groove_is_named_and_never_read_as_an_empty_one) {
    const auto autre = parseGroove(R"({"format":"vsm-synth-preset","version":1})");
    VSM_ASSERT(!autre.success);
    VSM_ASSERT(autre.error.find("vsm-synth-preset") != std::string::npos);

    const auto future = parseGroove(R"({"format":"vsm-groove","version":99})");
    VSM_ASSERT(!future.success);
    VSM_ASSERT(future.error.find("99") != std::string::npos);

    const auto vide = parseGroove(R"({"format":"vsm-groove","version":1,"stepsPerBar":16,"steps":[]})");
    VSM_ASSERT(!vide.success);

    const auto casse = parseGroove("{ pas du json");
    VSM_ASSERT(!casse.success);
    VSM_ASSERT(!casse.error.empty());
}
