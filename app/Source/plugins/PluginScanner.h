#pragma once
#include "vsm/interchange/PluginCatalogue.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// BALAYAGE DES PLUGINS INSTALLÉS (D7.5).
//
// BALAYER UN PLUGIN, C'EST L'EXÉCUTER. On ne peut pas savoir ce qu'un fichier
// `.vst3` contient sans ouvrir sa bibliothèque et l'interroger, c'est-à-dire
// sans faire tourner du code qu'on n'a pas écrit. Un seul plugin mal écrit --
// et il y en a -- fait alors tomber le processus. Un balayage naïf transforme
// « l'utilisateur a installé un plugin douteux » en « le DAW ne démarre plus »,
// sans le moindre message.
//
// D'OÙ UN PROCESSUS ENFANT PAR FICHIER. L'application se relance elle-même avec
// `--scan-plugin <fichier>` ; l'enfant charge, énumère, écrit une ligne par
// plugin trouvé, et sort. S'il tombe, il tombe SEUL : le parent constate un
// code de sortie anormal, note le fichier comme fautif, et passe au suivant.
// C'est le sens exact de « plugin fautif isolé et signalé, jamais fatal », et
// c'est irréalisable dans un seul processus.
//
// SE RELANCER SOI-MÊME plutôt que de livrer un exécutable de balayage à part :
// l'enfant doit charger EXACTEMENT le même code d'hôte que le parent, sinon le
// balayage validerait un chemin et la lecture en emprunterait un autre. Un
// second binaire aurait aussi à être trouvé, installé et tenu à jour.

namespace vsm::app::plugins {

/// Les dossiers où les plugins s'installent sur cette plateforme.
std::vector<juce::File> defaultPluginFolders();

/// Tous les fichiers `.clap` et `.vst3` de ces dossiers.
std::vector<juce::File> findPluginFiles(const std::vector<juce::File>& folders);

/// LE TRAVAIL DE L'ENFANT : ouvrir un fichier et dire ce qu'il contient. Ne
/// s'appelle que dans le processus enfant -- l'appeler dans le parent
/// reviendrait à supprimer l'isolement qui est toute la raison d'être de cette
/// étape.
std::vector<vsm::interchange::CataloguedPlugin> scanOneFileInThisProcess(const juce::File& file);

/// Où le catalogue est rangé, à côté des autres réglages de l'application.
juce::File catalogueFile();

vsm::interchange::PluginCatalogue loadCatalogue();
void saveCatalogue(const vsm::interchange::PluginCatalogue& catalogue);

/// Le balayage lui-même, sur un fil de fond : l'interface reste vivante et
/// l'utilisateur peut continuer à travailler pendant que deux cents fichiers
/// s'ouvrent un par un.
class PluginScanner final : public juce::Thread {
public:
    PluginScanner();
    ~PluginScanner() override;

    /// Démarre un balayage. `full` rouvre TOUT, y compris ce qui est déjà connu
    /// et ce qui a été noté fautif -- ce qu'on veut après avoir mis à jour un
    /// plugin, et seulement alors : rouvrir un fichier qui a déjà fait tomber
    /// un processus fait payer la même chute pour rien.
    void start(bool full);

    /// Avancement, appelé sur le fil de FOND. L'appelant doit repasser sur le
    /// fil de l'interface avant de toucher à quoi que ce soit d'affiché.
    std::function<void(int done, int total, const juce::String& current)> onProgress;
    /// Balayage terminé, appelé sur le fil de fond, avec le catalogue complet.
    std::function<void(vsm::interchange::PluginCatalogue)> onFinished;

    void run() override;

private:
    /// Lance l'enfant sur un fichier et lit ce qu'il en dit. Rend faux si
    /// l'enfant est tombé, a dépassé le temps imparti, ou n'a rien écrit
    /// d'exploitable -- et remplit alors `outReason`.
    bool scanInChildProcess(const juce::File& file,
                             std::vector<vsm::interchange::CataloguedPlugin>& out,
                             juce::String& outReason);

    bool full_ = false;
};

} // namespace vsm::app::plugins
