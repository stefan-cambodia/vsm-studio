#include "TestFramework.h"
#include "vsm/interchange/ReconstructionChain.h"
#include <filesystem>
#include <fstream>
#include <string>

using namespace vsm::interchange;
namespace fs = std::filesystem;

// D9.1 de docs/ROADMAP-daw.md — LANCER LA CHAÎNE DEPUIS L'APPLICATION.
//
// Le critère de l'étape ne porte pas sur le cas où tout marche : il porte sur
// celui où Python n'est pas là. « Fonction grisée AVEC SA RAISON, jamais une
// erreur ». Ces tests éprouvent donc surtout les absences, parce que c'est là
// que le logiciel a le choix entre expliquer et laisser devant un mur.

namespace {

/// Un faux dépôt jetable : `racine/analyse/reconstruire.py`, et éventuellement
/// l'environnement virtuel.
fs::path fabriquerDepot(const std::string& nom, bool avecScript, bool avecVenv) {
    const fs::path racine = fs::temp_directory_path() / ("vsm-d91-" + nom);
    fs::remove_all(racine);
    fs::create_directories(racine / "analyse");
    if (avecScript) {
        std::ofstream(racine / "analyse" / "reconstruire.py") << "# faux script\n";
    }
    if (avecVenv) {
        fs::create_directories(racine / "analyse" / ".venv" / "bin");
        std::ofstream(racine / "analyse" / ".venv" / "bin" / "python") << "#!/bin/sh\n";
    }
    // Un sous-dossier de compilation, pour éprouver la remontée : le binaire
    // vit dans `build/app/`, la chaîne à la racine.
    fs::create_directories(racine / "build" / "app");
    return racine;
}

} // namespace

VSM_TEST(the_chain_is_found_by_walking_up_from_the_binary) {
    // LA RECHERCHE DOIT REMONTER. Un exécutable compilé vit dans `build/app/`
    // et la chaîne à la racine du dépôt : ne regarder qu'à côté du binaire ne
    // trouverait jamais rien pendant le développement, c'est-à-dire au moment
    // même où la chaîne est là.
    const fs::path racine = fabriquerDepot("remontee", true, true);
    const auto chaine = ReconstructionChain::locate((racine / "build" / "app").string());

    VSM_ASSERT(chaine.available);
    VSM_ASSERT(chaine.reason.empty());
    VSM_ASSERT_EQ(fs::path(chaine.chainFolder).filename().string(), std::string("analyse"));
    VSM_ASSERT(!chaine.interpreterPath.empty());
    fs::remove_all(racine);
}

VSM_TEST(a_missing_chain_folder_says_so_and_says_what_to_do) {
    const fs::path vide = fs::temp_directory_path() / "vsm-d91-vide" / "a" / "b" / "c";
    fs::remove_all(fs::temp_directory_path() / "vsm-d91-vide");
    fs::create_directories(vide);

    const auto chaine = ReconstructionChain::locate(vide.string());
    VSM_ASSERT(!chaine.available);
    // UNE RAISON EST UNE PHRASE, PAS UN CODE : elle est destinée à être lue.
    VSM_ASSERT(!chaine.reason.empty());
    VSM_ASSERT(chaine.reason.find("analyse") != std::string::npos);
    VSM_ASSERT(!chaine.remedy.empty());
    // Et surtout : pas de commande à lancer, donc rien qui puisse échouer.
    VSM_ASSERT(chaine.commandLine("morceau.mp3", "sortie").empty());
    fs::remove_all(fs::temp_directory_path() / "vsm-d91-vide");
}

VSM_TEST(a_chain_without_its_virtualenv_is_a_different_answer) {
    // LA DISTINCTION VAUT LE CODE QU'ELLE COÛTE. Le dossier est là, sous les
    // yeux de l'utilisateur ; c'est l'environnement Python qui n'a jamais été
    // créé. Répondre « chaîne introuvable » l'enverrait chercher un dossier
    // qu'il vient de regarder, et le vrai remède -- une commande de trois mots
    // -- ne serait dit nulle part.
    const fs::path racine = fabriquerDepot("sans-venv", true, false);
    const auto chaine = ReconstructionChain::locate((racine / "build" / "app").string());

    VSM_ASSERT(!chaine.available);
    VSM_ASSERT(chaine.reason.find(".venv") != std::string::npos);
    VSM_ASSERT(chaine.remedy.find("venv") != std::string::npos);
    // Le dossier, lui, a bien été trouvé : la raison ne ment pas sur ce point.
    VSM_ASSERT(!chaine.chainFolder.empty());
    fs::remove_all(racine);
}

VSM_TEST(an_explicit_folder_wins_and_a_wrong_one_is_not_silently_replaced) {
    // Un dossier VALIDE existe par remontée, et l'utilisateur en a désigné un
    // AUTRE, qui est faux. Lui trouver quand même le bon lui ferait croire que
    // son réglage est correct -- et le jour où il le déplacera, il ne
    // comprendra pas pourquoi plus rien ne marche.
    const fs::path racine = fabriquerDepot("explicite", true, true);
    const fs::path faux = fs::temp_directory_path() / "vsm-d91-explicite-faux";
    fs::remove_all(faux);
    fs::create_directories(faux);

    const auto chaine = ReconstructionChain::locate((racine / "build" / "app").string(), faux.string());
    VSM_ASSERT(!chaine.available);
    VSM_ASSERT(chaine.reason.find("préférences") != std::string::npos);

    // Et un chemin explicite CORRECT est employé tel quel.
    const auto bonne = ReconstructionChain::locate((racine / "build" / "app").string(),
                                                    (racine / "analyse").string());
    VSM_ASSERT(bonne.available);
    fs::remove_all(racine);
    fs::remove_all(faux);
}

VSM_TEST(the_command_line_is_a_list_of_arguments_not_a_shell_string) {
    // UN MORCEAU S'APPELLE « Sweet Child O' Mine.mp3 ». Concaténer ce nom dans
    // une ligne de commande est la façon la plus sûre de transformer un titre
    // en instruction, et l'apostrophe suffit à le faire.
    const fs::path racine = fabriquerDepot("commande", true, true);
    const auto chaine = ReconstructionChain::locate((racine / "build" / "app").string());
    VSM_ASSERT(chaine.available);

    const auto commande = chaine.commandLine("/musique/Sweet Child O' Mine.mp3", "/tmp/sortie");
    VSM_ASSERT_EQ(commande.size(), size_t{5});
    VSM_ASSERT_EQ(commande[0], chaine.interpreterPath);
    VSM_ASSERT_EQ(commande[1], chaine.scriptPath);
    // Le nom de fichier passe ENTIER et INTACT, apostrophe et espaces compris.
    VSM_ASSERT_EQ(commande[2], std::string("/musique/Sweet Child O' Mine.mp3"));
    VSM_ASSERT_EQ(commande[3], std::string("--sortie"));
    VSM_ASSERT_EQ(commande[4], std::string("/tmp/sortie"));
    fs::remove_all(racine);
}

VSM_TEST(only_audio_is_offered_for_reconstruction) {
    // Un `.mid` glissé sur la fenêtre est un projet à importer, pas un morceau
    // à reconstruire : les confondre lancerait une analyse de dix minutes sur
    // un fichier qui n'attendait qu'à être lu.
    VSM_ASSERT(isReconstructableAudio("morceau.mp3"));
    VSM_ASSERT(isReconstructableAudio("/a/b/PRISE.WAV"));
    VSM_ASSERT(isReconstructableAudio("x.flac"));
    VSM_ASSERT(!isReconstructableAudio("morceau.mid"));
    VSM_ASSERT(!isReconstructableAudio("projet.json"));
    VSM_ASSERT(!isReconstructableAudio("sans-extension"));
}
