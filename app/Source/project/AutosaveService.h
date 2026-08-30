#pragma once
#include <JuceHeader.h>
#include "vsm/interchange/ProjectBundle.h"
#include "vsm/interchange/RecoverySession.h"
#include "vsm/interchange/SynthPreset.h"
#include "vsm/sequencer/Project.h"
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace vsm::app {

/// LA SAUVEGARDE AUTOMATIQUE, ET LA RÉCUPÉRATION APRÈS PLANTAGE (D10.4).
///
/// **CE QU'ELLE N'ÉCRIT PAS EST AUSSI IMPORTANT QUE CE QU'ELLE ÉCRIT.** Un
/// projet complet (`exportStandaloneProject`) COPIE tous les médias : recopier
/// une prise de deux cents mégaoctets toutes les trente secondes ferait de la
/// sauvegarde automatique la panne dont elle devait protéger. Elle écrit donc
/// `project.json`, le MIDI et les presets — et retient le dossier D'ORIGINE,
/// auquel les chemins de médias restent relatifs.
///
/// **ELLE N'ÉCRIT PAS SUR LE THREAD DE L'INTERFACE** non plus. Le projet est
/// COPIÉ sur le thread de l'interface — c'est un type valeur, la copie est
/// rapide et cohérente — puis écrit par un thread à part. Un studio qui hoquette
/// toutes les trente secondes est un studio dont on désactive la sauvegarde
/// automatique, et la protection disparaît avec elle.
///
/// **COMMENT ON SAIT QU'UNE SESSION S'EST INTERROMPUE.** Chaque exécution
/// possède son dossier et tient un verrou inter-processus dessus, et l'efface
/// en se terminant NORMALEMENT. Un dossier qui subsiste et dont le verrou
/// s'acquiert est donc celui d'une session morte sans se fermer. Le verrou est
/// ce qui distingue « l'application a planté » de « une autre fenêtre est
/// ouverte en ce moment » — sans lui, lancer une deuxième instance proposerait
/// de récupérer la première, qui est en train de travailler.
class AutosaveService : private juce::Thread {
public:
    /// Ce qu'on a trouvé d'une session interrompue.
    struct Recoverable {
        juce::File folder;                        ///< le dossier de récupération
        vsm::interchange::RecoveryRecord record;
    };

    /// `root` est le dossier qui contient toutes les sessions (un sous-dossier
    /// par session).
    explicit AutosaveService(const juce::File& root);
    ~AutosaveService() override;

    /// LES SESSIONS INTERROMPUES, À APPELER AVANT `begin()`. Après, notre
    /// propre dossier existerait et il faudrait l'exclure — une condition de
    /// plus, donc une occasion de plus de se tromper.
    std::vector<Recoverable> findInterruptedSessions();

    /// Ouvre la session de CETTE exécution : un dossier, un verrou.
    void begin();

    /// Thread UI. Prend une photo du projet et la fait écrire en fond. Sans
    /// effet si une écriture est déjà en attente : à trente secondes
    /// d'intervalle, cela ne peut arriver que si le disque est très lent, et
    /// dans ce cas empiler les photos ne ferait qu'aggraver.
    void requestSave(const vsm::sequencer::Project& project,
                      const std::map<size_t, vsm::interchange::SynthPreset>& presets,
                      const juce::File& originalFolder);

    /// Efface le dossier de cette session. Appelée à la fermeture NORMALE :
    /// c'est son absence qui, au prochain lancement, signalera un plantage.
    void endCleanly();

    /// Efface un dossier de récupération dont on n'a plus besoin (l'utilisateur
    /// a refusé, ou la récupération est faite).
    static void discard(const juce::File& folder);

    juce::File sessionFolder() const { return sessionFolder_; }

private:
    void run() override;
    static juce::String lockNameFor(const juce::File& folder);

    struct Photo {
        vsm::sequencer::Project project;
        std::map<size_t, vsm::interchange::SynthPreset> presets;
        vsm::interchange::RecoveryRecord record;
        bool valide = false;
    };

    juce::File root_;
    juce::File sessionFolder_;
    std::unique_ptr<juce::InterProcessLock> lock_;

    std::mutex mutex_;
    Photo enAttente_;
};

} // namespace vsm::app
