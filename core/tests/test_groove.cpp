#include "TestFramework.h"
#include "vsm/sequencer/Groove.h"
#include <cmath>
#include <vector>

using namespace vsm::sequencer;
using vsm::midi::Tick;

// D17.8 de docs/ROADMAP-daw.md — LE GROOVE.
//
// La quantification connaît la grille et le swing : elle RAPPROCHE d'un idéal
// calculé. Un groove fait l'inverse — il prend le placement RÉEL d'une partie
// qu'on trouve bonne et l'impose à une autre. Sur ce projet, c'est le geste qui
// manquait le plus : la chaîne d'analyse reconstruit une batterie avec son
// placement d'origine, et rien ne permettait de le donner à une basse écrite
// droite.

namespace {

constexpr Tick kMesure = 1920;   // 480 ppq, 4/4
constexpr int kPas = 16;

/// Seize doubles croches, chacune décalée de `ecarts[i]` fraction de pas.
std::vector<Note> partie(const std::vector<double>& ecarts, uint8_t velocite = 100) {
    std::vector<Note> notes;
    const double pas = static_cast<double>(kMesure) / kPas;
    uint64_t id = 1;
    for (int i = 0; i < kPas; ++i) {
        const auto depart = static_cast<Tick>(std::llround((i + ecarts[static_cast<size_t>(i)]) * pas));
        notes.push_back(Note{depart, depart + 60, 0, 36, velocite, 64, id++});
    }
    return notes;
}

std::vector<double> swing(double force) {
    std::vector<double> e(kPas, 0.0);
    for (int i = 1; i < kPas; i += 2) e[static_cast<size_t>(i)] = force;   // les temps faibles en retard
    return e;
}

} // namespace

VSM_TEST(extracting_a_groove_then_applying_it_to_a_straight_copy_gives_the_original_back) {
    // LE CRITÈRE DE L'ÉTAPE, et le seul qui vaille : le groove doit être une
    // description SUFFISANTE du placement. S'il l'est, le tour est bouclé à un
    // tick près.
    const auto joue = partie(swing(0.28));
    const auto groove = extractGroove(joue, kMesure, kPas, "Balance");
    VSM_ASSERT_EQ(groove.steps.size(), size_t(kPas));

    auto droite = partie(std::vector<double>(kPas, 0.0));
    NoteSelection tout;
    for (const auto& n : droite) tout.insert(n.id);
    VSM_ASSERT_EQ(applyGroove(droite, tout, groove, kMesure, 1.0f), size_t(8));

    for (size_t i = 0; i < droite.size(); ++i) {
        const Tick attendu = joue[i].startTick;
        if (std::llabs(droite[i].startTick - attendu) > 1)
            std::printf("      [D17.8] pas %zu : attendu %lld, obtenu %lld\n", i,
                        static_cast<long long>(attendu),
                        static_cast<long long>(droite[i].startTick));
        VSM_ASSERT(std::llabs(droite[i].startTick - attendu) <= 1);
    }
}

VSM_TEST(a_strength_of_a_half_goes_halfway_and_a_strength_of_zero_moves_nothing) {
    const auto groove = extractGroove(partie(swing(0.4)), kMesure, kPas);
    const double pas = static_cast<double>(kMesure) / kPas;

    auto moitie = partie(std::vector<double>(kPas, 0.0));
    NoteSelection tout;
    for (const auto& n : moitie) tout.insert(n.id);
    applyGroove(moitie, tout, groove, kMesure, 0.5f);
    // Le deuxième pas devait partir de 0,4 pas de retard : à demi-force, 0,2.
    VSM_ASSERT_NEAR(static_cast<double>(moitie[1].startTick) - pas, 0.2 * pas, 1.5);

    auto immobile = partie(std::vector<double>(kPas, 0.0));
    const auto avant = immobile;
    VSM_ASSERT_EQ(applyGroove(immobile, tout, groove, kMesure, 0.0f), size_t(0));
    for (size_t i = 0; i < avant.size(); ++i)
        VSM_ASSERT_EQ(immobile[i].startTick, avant[i].startTick);
}

VSM_TEST(a_step_the_groove_says_nothing_about_leaves_its_notes_alone) {
    // Un pas muet ne décrit aucun placement, et prétendre le contraire ferait
    // tirer vers zéro les notes qui tombent dessus.
    std::vector<Note> source;
    uint64_t id = 1;
    const double pas = static_cast<double>(kMesure) / kPas;
    // Seulement les temps : quatre notes sur seize pas.
    for (int i = 0; i < kPas; i += 4) {
        const auto depart = static_cast<Tick>(std::llround((i + 0.25) * pas));
        source.push_back(Note{depart, depart + 60, 0, 36, 100, 64, id++});
    }
    const auto groove = extractGroove(source, kMesure, kPas);
    int presents = 0;
    for (const auto& s : groove.steps) if (s.present) ++presents;
    VSM_ASSERT_EQ(presents, 4);

    auto cible = partie(std::vector<double>(kPas, 0.0));
    NoteSelection tout;
    for (const auto& n : cible) tout.insert(n.id);
    const auto avant = cible;
    VSM_ASSERT_EQ(applyGroove(cible, tout, groove, kMesure, 1.0f), size_t(4));
    for (size_t i = 0; i < cible.size(); ++i)
        if (i % 4 != 0) VSM_ASSERT_EQ(cible[i].startTick, avant[i].startTick);
}

VSM_TEST(the_groove_carries_the_accents_only_when_asked) {
    // Un groove porte le placement ET l'accentuation, mais on veut souvent le
    // placement sans toucher aux nuances qu'on a écrites.
    std::vector<Note> source;
    uint64_t id = 1;
    const double pas = static_cast<double>(kMesure) / kPas;
    for (int i = 0; i < kPas; ++i) {
        const auto depart = static_cast<Tick>(std::llround(i * pas));
        source.push_back(Note{depart, depart + 60, 0, 36,
                               static_cast<uint8_t>(i % 4 == 0 ? 120 : 40), 64, id++});
    }
    const auto groove = extractGroove(source, kMesure, kPas);
    VSM_ASSERT(groove.steps[0].velocity > groove.steps[1].velocity);

    auto sansNuance = partie(std::vector<double>(kPas, 0.0), 90);
    NoteSelection tout;
    for (const auto& n : sansNuance) tout.insert(n.id);
    auto avecNuance = sansNuance;
    applyGroove(sansNuance, tout, groove, kMesure, 1.0f, false);
    for (const auto& n : sansNuance) VSM_ASSERT_EQ(int(n.velocity), 90);

    applyGroove(avecNuance, tout, groove, kMesure, 1.0f, true);
    VSM_ASSERT(avecNuance[0].velocity > 100);
    VSM_ASSERT(avecNuance[1].velocity < 60);
}

VSM_TEST(a_groove_extracted_from_nothing_is_empty_and_applies_nothing) {
    const auto vide = extractGroove({}, kMesure, kPas);
    VSM_ASSERT(vide.empty());
    auto notes = partie(std::vector<double>(kPas, 0.0));
    const auto avant = notes;
    NoteSelection tout;
    for (const auto& n : notes) tout.insert(n.id);
    VSM_ASSERT_EQ(applyGroove(notes, tout, vide, kMesure, 1.0f), size_t(0));
    for (size_t i = 0; i < avant.size(); ++i)
        VSM_ASSERT_EQ(notes[i].startTick, avant[i].startTick);
}
