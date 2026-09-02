#pragma once
#include <JuceHeader.h>
#include "vsm/interchange/ReconstructionChain.h"
#include <functional>
#include <mutex>
#include <vector>

namespace vsm::app {

/// LANCER LA CHAÎNE D'ANALYSE, ET LA REGARDER TRAVAILLER (D9.1 et D9.2).
///
/// **UN PROCESSUS ENFANT, ET NON UN INTERPRÉTEUR EMBARQUÉ.** Lier Python au
/// binaire violerait la règle n° 2 du § 0 de `ROADMAP-daw.md` -- le DAW se
/// compile et fonctionne sans Python. Un processus séparé la respecte
/// littéralement : sans Python, il n'y a rien à lancer, et rien à lier.
///
/// **ET IL PROTÈGE AUSSI DE CE QU'ON NE MAÎTRISE PAS.** La chaîne charge
/// `torch` et `demucs`, alloue plusieurs gigaoctets et peut s'effondrer sur un
/// modèle absent ou une carte graphique qui refuse. Dans le processus du DAW,
/// chacun de ces échecs emporterait le morceau ouvert. Ici, il produit un code
/// de sortie et une explication -- exactement la logique du balayage des
/// plugins (D7.5), pour la même raison.
///
/// **CE QUI EST AFFICHÉ VIENT DE LA CHAÎNE, PAS D'UNE ESTIMATION.** Elle
/// annonce ses étapes elle-même (`[2/5] Séparation en stems...`), et c'est cela
/// qu'on montre. Une barre de progression inventée -- « environ 40 % » --
/// mentirait sur une chaîne dont les étapes durent de trois secondes à dix
/// minutes selon le morceau et la machine.
class ReconstructionRunner : private juce::Thread {
public:
    struct Progress {
        int step = 0;             ///< étape en cours, 1..stepCount ; 0 = pas encore commencé
        int stepCount = 0;        ///< nombre d'étapes annoncé par la chaîne
        juce::String stepLabel;   ///< ce que la chaîne dit faire en ce moment
        juce::StringArray recentLines;  ///< les dernières lignes, pour le journal
    };

    ReconstructionRunner();
    ~ReconstructionRunner() override;

    /// Thread UI. `onProgress` et `onFinished` sont appelés sur le thread UI.
    /// `onFinished(true, dossier, "")` en cas de succès ; sinon
    /// `onFinished(false, {}, raison)`.
    /// `viserLaParite` : autant de pistes que le morceau a de parties
    /// (docs/CDC-detection-multipiste.md). Le réglage vient des préférences,
    /// pas d'un argument perdu ici : c'est un choix de travail, pas un
    /// paramètre d'appel.
    void start(const vsm::interchange::ReconstructionChain& chain,
                const juce::File& audioFile, const juce::File& outputFolder,
                bool viserLaParite = false);

    /// Thread UI. Demande l'arrêt : le processus enfant est tué, et le dossier
    /// de sortie INCOMPLET est laissé tel quel plutôt qu'effacé -- il contient
    /// ce que la chaîne a déjà produit, et l'effacer perdrait dix minutes de
    /// séparation pour un projet qu'on relancera peut-être avec d'autres
    /// options.
    void cancel();
    bool isRunning() const { return isThreadRunning(); }

    std::function<void(const Progress&)> onProgress;
    std::function<void(bool, juce::File, juce::String)> onFinished;

private:
    void run() override;
    void publish();

    void handleLine(const juce::String& ligne);

    vsm::interchange::ReconstructionChain chain_;
    juce::File audioFile_, outputFolder_;
    /// Viser la parité des pistes : lu au démarrage du thread, jamais après.
    bool viserLaParite_ = false;

    mutable std::mutex mutex_;
    Progress progress_;
    std::atomic<bool> cancelled_{false};
    /// Le processus enfant. Touché par le thread de travail ; `cancel()` le
    /// tue depuis le thread UI, d'où le verrou.
    std::unique_ptr<juce::ChildProcess> process_;
};

} // namespace vsm::app
