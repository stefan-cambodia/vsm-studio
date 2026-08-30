#include "PluginScanner.h"

#if VSM_WITH_CLAP
#include "ClapPluginHost.h"
#endif
#if VSM_WITH_VST3
#include "Vst3PluginHost.h"
#endif

#include <juce_core/juce_core.h>

namespace vsm::app::plugins {
namespace {

using vsm::interchange::CataloguedPlugin;

/// LE TEMPS LAISSÉ À UN ENFANT POUR OUVRIR UN FICHIER. Généreux : certains
/// plugins lisent des gigaoctets d'échantillons à l'ouverture, et les couper
/// les déclarerait fautifs à tort. Mais BORNÉ : un plugin qui attend une clé de
/// licence sur un serveur injoignable ne rendra jamais la main, et le balayage
/// resterait bloqué sur lui pour toujours.
constexpr int kDelaiParFichierMs = 20000;

} // namespace

std::vector<juce::File> defaultPluginFolders() {
    std::vector<juce::File> dossiers;
    auto ajoute = [&dossiers](const juce::File& dossier) {
        if (dossier.isDirectory()) dossiers.push_back(dossier);
    };

    const juce::File maison = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
#if JUCE_LINUX || JUCE_BSD
    ajoute(juce::File("/usr/lib/vst3"));
    ajoute(juce::File("/usr/local/lib/vst3"));
    ajoute(maison.getChildFile(".vst3"));
    ajoute(juce::File("/usr/lib/clap"));
    ajoute(juce::File("/usr/local/lib/clap"));
    ajoute(maison.getChildFile(".clap"));
#elif JUCE_MAC
    ajoute(juce::File("/Library/Audio/Plug-Ins/VST3"));
    ajoute(maison.getChildFile("Library/Audio/Plug-Ins/VST3"));
    ajoute(juce::File("/Library/Audio/Plug-Ins/CLAP"));
    ajoute(maison.getChildFile("Library/Audio/Plug-Ins/CLAP"));
#elif JUCE_WINDOWS
    ajoute(juce::File("C:\\Program Files\\Common Files\\VST3"));
    ajoute(juce::File("C:\\Program Files\\Common Files\\CLAP"));
#endif
    return dossiers;
}

std::vector<juce::File> findPluginFiles(const std::vector<juce::File>& folders) {
    std::vector<juce::File> fichiers;
    for (const auto& dossier : folders) {
        // UN `.vst3` EST UN DOSSIER sur Linux et sur macOS, un fichier sur
        // Windows. On cherche donc les deux, et on ne descend pas DEDANS : le
        // bundle entier est l'unité qu'un hôte ouvre.
        for (const auto& trouve : juce::RangedDirectoryIterator(
                 dossier, true, "*.vst3;*.clap",
                 juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles)) {
            const juce::File fichier = trouve.getFile();
            if (!fichier.getFileName().endsWithIgnoreCase(".vst3")
                && !fichier.getFileName().endsWithIgnoreCase(".clap")) continue;
            // Un bundle contient parfois d'autres bundles ; on garde le plus
            // extérieur, qui est celui que l'hôte sait ouvrir.
            bool dedansUnAutre = false;
            for (const auto& deja : fichiers)
                if (fichier.isAChildOf(deja)) { dedansUnAutre = true; break; }
            if (!dedansUnAutre) fichiers.push_back(fichier);
        }
    }
    return fichiers;
}

std::vector<CataloguedPlugin> scanOneFileInThisProcess(const juce::File& file) {
    std::vector<CataloguedPlugin> trouves;
    const std::string chemin = file.getFullPathName().toStdString();
    std::string erreur;

#if VSM_WITH_VST3
    if (file.getFileName().endsWithIgnoreCase(".vst3")) {
        for (const auto& info : vsm::vst3::scanVst3File(chemin, erreur)) {
            CataloguedPlugin plugin;
            plugin.format = "vst3";
            plugin.path = chemin;
            plugin.id = info.id;
            plugin.name = info.name;
            plugin.vendor = info.vendor;
            plugin.isInstrument = info.isInstrument;
            trouves.push_back(std::move(plugin));
        }
    }
#endif
#if VSM_WITH_CLAP
    if (file.getFileName().endsWithIgnoreCase(".clap")) {
        for (const auto& info : vsm::clap::scanClapFile(chemin, erreur)) {
            CataloguedPlugin plugin;
            plugin.format = "clap";
            plugin.path = chemin;
            plugin.id = info.id;
            plugin.name = info.name;
            plugin.vendor = info.vendor;
            plugin.isInstrument = info.isInstrument;
            trouves.push_back(std::move(plugin));
        }
    }
#endif
    juce::ignoreUnused(erreur);
    return trouves;
}

juce::File catalogueFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("VSM Studio")
        .getChildFile("plugins.json");
}

vsm::interchange::PluginCatalogue loadCatalogue() {
    const juce::File fichier = catalogueFile();
    if (!fichier.existsAsFile()) return {};
    const auto lu = vsm::interchange::parsePluginCatalogue(fichier.loadFileAsString().toStdString());
    // UN CATALOGUE ILLISIBLE EST UN CATALOGUE VIDE, pas une erreur qui empêche
    // de démarrer : il se refait en un balayage, et rien de ce qu'il contient
    // n'est irremplaçable.
    return lu.success ? lu.catalogue : vsm::interchange::PluginCatalogue{};
}

void saveCatalogue(const vsm::interchange::PluginCatalogue& catalogue) {
    const juce::File fichier = catalogueFile();
    fichier.getParentDirectory().createDirectory();
    fichier.replaceWithText(
        juce::String(vsm::interchange::pluginCatalogueToJson(catalogue).toString()));
}

PluginScanner::PluginScanner() : juce::Thread("Balayage des plugins") {}

PluginScanner::~PluginScanner() {
    // ON ATTEND, MAIS PAS INDÉFINIMENT. Le fil peut être en train d'attendre un
    // enfant ; `signalThreadShouldExit` le fera sortir à la fin du fichier
    // courant. Deux fois le délai d'un fichier suffit largement.
    stopThread(kDelaiParFichierMs * 2);
}

void PluginScanner::start(bool full) {
    if (isThreadRunning()) return;
    full_ = full;
    startThread();
}

bool PluginScanner::scanInChildProcess(const juce::File& file,
                                        std::vector<CataloguedPlugin>& out,
                                        juce::String& outReason) {
    const juce::File moiMeme = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

    juce::ChildProcess enfant;
    juce::StringArray commande;
    commande.add(moiMeme.getFullPathName());
    commande.add("--scan-plugin");
    commande.add(file.getFullPathName());

    if (!enfant.start(commande, juce::ChildProcess::wantStdOut)) {
        outReason = "impossible de lancer le processus de balayage";
        return false;
    }

    const juce::String sortie = enfant.readAllProcessOutput();
    if (!enfant.waitForProcessToFinish(kDelaiParFichierMs)) {
        // IL N'A JAMAIS RENDU LA MAIN. Un plugin qui attend une clé de licence
        // sur un serveur injoignable en est le cas le plus courant. On le tue :
        // le laisser vivre bloquerait le balayage pour toujours.
        enfant.kill();
        outReason = "le plugin n'a pas repondu en "
                    + juce::String(kDelaiParFichierMs / 1000) + " secondes";
        return false;
    }

    const juce::uint32 code = enfant.getExitCode();
    if (code != 0) {
        // TOMBÉ. C'est le cas que toute cette machinerie existe pour survivre :
        // dans un seul processus, celui-ci aurait emporté l'application.
        outReason = "le processus de balayage est tombe (code " + juce::String(static_cast<int>(code)) + ")";
        return false;
    }

    juce::StringArray lignes;
    lignes.addLines(sortie);
    for (const auto& ligne : lignes) {
        CataloguedPlugin plugin;
        if (vsm::interchange::decodeScanLine(ligne.toStdString(), plugin))
            out.push_back(std::move(plugin));
    }
    return true;
}

void PluginScanner::run() {
    vsm::interchange::PluginCatalogue catalogue = full_ ? vsm::interchange::PluginCatalogue{}
                                                         : loadCatalogue();
    const auto fichiers = findPluginFiles(defaultPluginFolders());

    int faits = 0;
    for (const auto& fichier : fichiers) {
        if (threadShouldExit()) break;
        ++faits;
        const std::string chemin = fichier.getFullPathName().toStdString();
        if (!full_ && catalogue.alreadyKnown(chemin)) continue;
        if (onProgress) onProgress(faits, static_cast<int>(fichiers.size()), fichier.getFileName());

        std::vector<CataloguedPlugin> trouves;
        juce::String raison;
        if (scanInChildProcess(fichier, trouves, raison)) {
            for (auto& plugin : trouves) catalogue.plugins.push_back(std::move(plugin));
        } else {
            // SIGNALÉ, PAS TU. Un fichier qui disparaît du balayage sans un mot
            // laisse l'utilisateur chercher pourquoi son plugin n'apparaît pas.
            catalogue.faulty.push_back({chemin, raison.toStdString()});
        }
        // ÉCRIT AU FUR ET À MESURE : si l'APPLICATION est fermée pendant un
        // balayage de deux cents fichiers, le travail déjà fait est gardé.
        saveCatalogue(catalogue);
    }

    saveCatalogue(catalogue);
    if (onFinished) onFinished(catalogue);
}

} // namespace vsm::app::plugins
