#include "vsm/interchange/SoundFont.h"
#include "vsm/interchange/MultisampleProfile.h"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

// `vsm-sf2` — convertit un preset SoundFont en profil pour `vsm.multisample`
// (docs/CDC-multisample.md § 5, phase M2).
//
// POURQUOI UN OUTIL SÉPARÉ, ET PAS UNE FONCTION DU DAW : le § 5 du cahier des
// charges l'exige, et la raison tient en une phrase — le DAW ne charge QUE le
// format de profil de la machine. Une banque SF2 se convertit une fois, à
// l'installation ; la convertir à chaque ouverture de projet ferait dépendre le
// rendu d'un analyseur de format tiers, et le rendu est la source de vérité.
//
// L'outil est bavard À DESSEIN : il imprime les générateurs qu'il n'applique
// pas, un par un, avec leur nombre d'occurrences. Une banque dont le caractère
// tient à son filtre se convertirait sinon en silence, et personne ne saurait
// pourquoi elle sonne plat.

namespace {

int usage() {
    std::cout <<
        "vsm-sf2 — convertit un preset SoundFont en profil vsm.multisample\n\n"
        "  vsm-sf2 --lister BANQUE.sf2\n"
        "      liste les presets du fichier, avec banque et programme\n\n"
        "  vsm-sf2 --convertir BANQUE.sf2 --programme N [options]\n"
        "      --banque N          numéro de banque MIDI (défaut 0 ; 128 = percussions)\n"
        "      --nom NOM           nom du profil écrit (défaut : celui du preset)\n"
        "      --attribution TXT   licence et crédit — OBLIGATOIRE\n"
        "      --sortie DOSSIER    où écrire (défaut : le dossier des profils installés)\n"
        "      --duree-max S       tronquer les échantillons à S secondes (0 = non)\n\n"
        "  vsm-sf2 --sf2-minimal FICHIER.sf2\n"
        "      écrit le SoundFont minimal qui sert aux tests\n";
    return 1;
}

std::string argumentAfter(int argc, char** argv, const char* name, const std::string& fallback = {}) {
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return fallback;
}

bool hasFlag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

} // namespace

int main(int argc, char** argv) {
    using namespace vsm::interchange;
    if (argc < 2) return usage();

    if (hasFlag(argc, argv, "--sf2-minimal")) {
        const std::string path = argumentAfter(argc, argv, "--sf2-minimal");
        if (path.empty()) return usage();
        std::string error;
        if (!writeMinimalSoundFont(path, error)) { std::cerr << "vsm-sf2 : " << error << "\n"; return 2; }
        std::cout << "SoundFont minimal écrit : " << path << "\n";
        return 0;
    }

    if (hasFlag(argc, argv, "--lister")) {
        const std::string path = argumentAfter(argc, argv, "--lister");
        if (path.empty()) return usage();
        const auto index = readSoundFontIndex(path);
        if (!index.success) { std::cerr << "vsm-sf2 : " << index.error << "\n"; return 2; }
        std::cout << index.bankName << "  (SoundFont " << index.majorVersion << "."
                  << index.minorVersion << ", " << index.presets.size() << " presets)\n";
        if (!index.copyright.empty()) std::cout << "  licence déclarée : " << index.copyright << "\n";
        if (!index.engineers.empty()) std::cout << "  auteurs : " << index.engineers << "\n";
        std::cout << "\n  banque  prog  nom\n";
        for (const auto& preset : index.presets)
            std::cout << "  " << (preset.bank < 10 ? "   " : (preset.bank < 100 ? "  " : " "))
                      << preset.bank << "   " << (preset.program < 10 ? "  " : (preset.program < 100 ? " " : ""))
                      << preset.program << "  " << preset.name << "\n";
        return 0;
    }

    const std::string path = argumentAfter(argc, argv, "--convertir");
    if (path.empty()) return usage();

    const int bank = std::stoi(argumentAfter(argc, argv, "--banque", "0"));
    const std::string programText = argumentAfter(argc, argv, "--programme");
    if (programText.empty()) { std::cerr << "vsm-sf2 : --programme est obligatoire\n"; return usage(); }
    const int program = std::stoi(programText);
    const double maxSeconds = std::stod(argumentAfter(argc, argv, "--duree-max", "0"));

    const auto index = readSoundFontIndex(path);
    if (!index.success) { std::cerr << "vsm-sf2 : " << index.error << "\n"; return 2; }

    std::string name = argumentAfter(argc, argv, "--nom");
    if (name.empty())
        for (const auto& preset : index.presets)
            if (preset.bank == bank && preset.program == program) name = preset.name;
    if (name.empty()) name = "programme-" + std::to_string(program);
    // Le nom devient un nom de fichier ET un nom de dossier : on n'y laisse
    // passer que ce qui traverse tous les systèmes de fichiers sans surprise.
    for (char& c : name)
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|' || c == ' ') c = '-';

    std::string attribution = argumentAfter(argc, argv, "--attribution");
    if (attribution.empty() && !index.copyright.empty()) {
        attribution = index.bankName + " — " + index.copyright;
        if (!index.engineers.empty()) attribution += " (" + index.engineers + ")";
        std::cout << "attribution reprise du fichier : " << attribution << "\n";
    }
    if (attribution.empty()) {
        // § 28 d'ARCHITECTURE.md, appliqué à la lettre : licence inconnue,
        // banque refusée. Le fichier ne la déclare pas et l'utilisateur ne l'a
        // pas donnée — il n'y a rien à supposer.
        std::cerr << "vsm-sf2 : ce fichier ne déclare aucune licence (champ ICOP vide) et\n"
                     "          --attribution n'a pas été donné. Conversion REFUSÉE : on ne\n"
                     "          distribue pas un profil dont on ignore sous quelle licence il\n"
                     "          circule (§ 28 d'ARCHITECTURE.md).\n";
        return 3;
    }

    std::string folder = argumentAfter(argc, argv, "--sortie");
    if (folder.empty()) folder = multisampleProfileFolder();
    std::error_code ignored;
    std::filesystem::create_directories(folder, ignored);

    std::cout << "conversion de « " << name << " » (banque " << bank << ", programme "
              << program << ")…\n";
    const auto conversion = convertSoundFontPreset(path, bank, program, maxSeconds);
    if (!conversion.success) { std::cerr << "vsm-sf2 : " << conversion.error << "\n"; return 2; }

    const auto written = writeSoundFontProfile(conversion, folder, name, attribution);
    if (!written.success) { std::cerr << "vsm-sf2 : " << written.error << "\n"; return 2; }

    std::cout << "  " << conversion.zones.size() << " zones, "
              << (written.memoryBytes / (1024u * 1024u)) << " Mo en mémoire\n";
    std::cout << "  enveloppe reprise : attaque " << conversion.attackSeconds
              << " s, relâchement " << conversion.releaseSeconds << " s\n";

    if (!conversion.ignoredGenerators.empty()) {
        std::cout << "\ngénérateurs NON appliqués (ce que cette machine ne sait pas faire) :\n";
        for (const auto& [generator, count] : conversion.ignoredGenerators)
            std::cout << "  " << generator << " ×" << count << "\n";
    }
    for (const auto& note : conversion.notes) std::cout << "  note : " << note << "\n";

    std::cout << "\nprofil : " << written.profilePath << "\npreset : " << written.presetPath << "\n";
    if (written.memoryBytes > vsm::audio::plugin::kMultisampleMemoryBudgetBytes)
        std::cout << "\nATTENTION : au-delà du budget mémoire, la machine refusera ce profil.\n"
                     "            Réessayez avec --duree-max.\n";
    return 0;
}
