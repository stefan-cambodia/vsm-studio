#include "TestFramework.h"
#include "vsm/interchange/BrowserIndex.h"
#include <filesystem>
#include <fstream>

using namespace vsm::interchange;
namespace fs = std::filesystem;

// D10.1 de docs/ROADMAP-daw.md — LE NAVIGATEUR.
//
// « Trouver un preset ne demande plus d'ouvrir un dossier. » Ce que
// l'application savait faire, c'était CHARGER un preset — à condition de savoir
// déjà où il était. Trente-quatre machines, autant de presets par projet, des
// profils et des dossiers d'échantillons : la matière existait, et le seul
// moyen d'y accéder était de s'en souvenir.

namespace {

fs::path fabriquerBibliotheque() {
    const fs::path racine = fs::temp_directory_path() / "vsm-d101";
    fs::remove_all(racine);
    fs::create_directories(racine / "basses");
    fs::create_directories(racine / "nappes");
    fs::create_directories(racine / "batterie" / "caisses");

    auto ecrire = [](const fs::path& p) { std::ofstream(p) << "{}\n"; };
    ecrire(racine / "basses" / "TB-303 Acid Lead.synth.json");
    ecrire(racine / "basses" / "sub-ronde.synth.json");
    ecrire(racine / "nappes" / "choeur-lointain.synth.json");
    ecrire(racine / "batterie" / "kit-live.profile.json");
    ecrire(racine / "batterie" / "caisses" / "caisse-claire.wav");
    ecrire(racine / "notes.txt");            // ni preset, ni profil, ni échantillon
    ecrire(racine / "project.json");         // un projet n'est pas un preset
    return racine;
}

} // namespace

VSM_TEST(the_index_recognises_what_it_is_looking_at) {
    const fs::path racine = fabriquerBibliotheque();
    std::vector<BrowserItem> entrees;
    indexFolder(racine.string(), "Bibliothèque", entrees);

    int presets = 0, profils = 0, echantillons = 0;
    for (const auto& e : entrees) {
        if (e.kind == BrowserItemKind::Preset) ++presets;
        if (e.kind == BrowserItemKind::Profile) ++profils;
        if (e.kind == BrowserItemKind::Sample) ++echantillons;
    }
    VSM_ASSERT_EQ(presets, 3);
    VSM_ASSERT_EQ(profils, 1);
    VSM_ASSERT_EQ(echantillons, 1);
    // ET RIEN D'AUTRE : `notes.txt` et `project.json` ne sont pas des presets.
    VSM_ASSERT_EQ(entrees.size(), size_t{5});
    fs::remove_all(racine);
}

VSM_TEST(a_preset_keeps_a_readable_name) {
    // « basse-acide.synth » se lit mal : la double extension du format part en
    // entier.
    const fs::path racine = fabriquerBibliotheque();
    std::vector<BrowserItem> entrees;
    indexFolder(racine.string(), "Bibliothèque", entrees);

    bool trouve = false;
    for (const auto& e : entrees)
        if (e.name == "TB-303 Acid Lead") { trouve = true; VSM_ASSERT(e.kind == BrowserItemKind::Preset); }
    VSM_ASSERT(trouve);
    fs::remove_all(racine);
}

VSM_TEST(the_origin_says_which_folder_not_just_which_library) {
    // Deux « basse » dans deux sous-dossiers doivent se distinguer sans qu'on
    // ait à les essayer.
    const fs::path racine = fabriquerBibliotheque();
    std::vector<BrowserItem> entrees;
    indexFolder(racine.string(), "Bibliothèque", entrees);

    for (const auto& e : entrees)
        if (e.name == "sub-ronde")
            VSM_ASSERT(e.origin.find("basses") != std::string::npos);
    fs::remove_all(racine);
}

VSM_TEST(all_the_words_in_any_order) {
    std::vector<BrowserItem> entrees = {
        {BrowserItemKind::Preset, "TB-303 Acid Lead", "/a/b.synth.json", "Projet"},
        {BrowserItemKind::Preset, "acid lead (tb303)", "/a/c.synth.json", "Bibliothèque"},
        {BrowserItemKind::Preset, "nappe douce", "/a/d.synth.json", "Bibliothèque"},
        {BrowserItemKind::Machine, "Juno-106", "vsm.juno106", "Parc"},
    };
    // « 303 acid » trouve les deux, dans n'importe quel ordre et sans casse.
    VSM_ASSERT_EQ(filterBrowserItems(entrees, "303 acid").size(), size_t{2});
    VSM_ASSERT_EQ(filterBrowserItems(entrees, "ACID 303").size(), size_t{2});
    // L'origine compte aussi : chercher « projet » ne rend que ce qui vient du
    // projet ouvert.
    VSM_ASSERT_EQ(filterBrowserItems(entrees, "projet").size(), size_t{1});
    // Une requête vide rend tout : le navigateur s'ouvre plein, pas vide.
    VSM_ASSERT_EQ(filterBrowserItems(entrees, "").size(), entrees.size());
    VSM_ASSERT_EQ(filterBrowserItems(entrees, "introuvable").size(), size_t{0});
}

VSM_TEST(the_index_refuses_to_dig_forever) {
    // Un dossier d'échantillons peut être n'importe quoi -- y compris la racine
    // d'un disque, désignée par mégarde. Une exploration sans fond
    // transformerait une erreur de clic en gel de plusieurs minutes.
    const fs::path racine = fs::temp_directory_path() / "vsm-d101-profond";
    fs::remove_all(racine);
    fs::path courant = racine;
    for (int i = 0; i < 8; ++i) {
        courant /= ("niveau" + std::to_string(i));
        fs::create_directories(courant);
        std::ofstream(courant / ("son" + std::to_string(i) + ".wav")) << "x";
    }
    std::vector<BrowserItem> entrees;
    indexFolder(racine.string(), "Profond", entrees, /*maxDepth=*/3);
    VSM_ASSERT(!entrees.empty());
    VSM_ASSERT(entrees.size() <= 4);   // les niveaux 0..3, pas les huit
    fs::remove_all(racine);
}

VSM_TEST(a_folder_that_is_not_there_is_not_an_error) {
    // Un projet jamais enregistré n'a pas de dossier, une bibliothèque peut
    // avoir été déplacée : le navigateur s'ouvre quand même, avec ce qu'il a.
    std::vector<BrowserItem> entrees;
    indexFolder("", "Rien", entrees);
    indexFolder("/chemin/qui/n/existe/pas", "Rien", entrees);
    VSM_ASSERT_EQ(entrees.size(), size_t{0});
}
