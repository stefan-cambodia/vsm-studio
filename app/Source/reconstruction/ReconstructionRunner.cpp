#include "ReconstructionRunner.h"

namespace vsm::app {

namespace {
/// Combien de lignes de journal on garde à l'écran. Assez pour voir ce qui
/// vient de se passer, pas assez pour que la fenêtre devienne un fichier de
/// trace : le journal COMPLET est celui du dossier de sortie.
constexpr int kLignesGardees = 200;
} // namespace

ReconstructionRunner::ReconstructionRunner() : juce::Thread("reconstruction") {}

ReconstructionRunner::~ReconstructionRunner() {
    cancel();
    stopThread(4000);
}

void ReconstructionRunner::start(const vsm::interchange::ReconstructionChain& chain,
                                  const juce::File& audioFile, const juce::File& outputFolder,
                                  bool viserLaParite) {
    if (isThreadRunning()) return;
    chain_ = chain;
    audioFile_ = audioFile;
    outputFolder_ = outputFolder;
    viserLaParite_ = viserLaParite;
    cancelled_.store(false);
    {
        std::lock_guard<std::mutex> verrou(mutex_);
        progress_ = Progress{};
    }
    startThread();
}

void ReconstructionRunner::cancel() {
    cancelled_.store(true);
    std::lock_guard<std::mutex> verrou(mutex_);
    if (process_) process_->kill();
}

void ReconstructionRunner::handleLine(const juce::String& ligne) {
    std::lock_guard<std::mutex> verrou(mutex_);
    progress_.recentLines.add(ligne);
    while (progress_.recentLines.size() > kLignesGardees) progress_.recentLines.remove(0);

    // LES ÉTAPES SONT CELLES QUE LA CHAÎNE ANNONCE : « [2/5] Séparation en
    // stems (htdemucs) ». On lit le compte qu'elle donne au lieu de le
    // supposer -- le jour où elle passera à six étapes, la fenêtre suivra sans
    // qu'on y touche.
    const juce::String coupee = ligne.trim();
    if (!coupee.startsWith("[")) return;
    const int barre = coupee.indexOfChar('/');
    const int fermante = coupee.indexOfChar(']');
    if (barre <= 0 || fermante <= barre) return;
    const int numero = coupee.substring(1, barre).getIntValue();
    const int total = coupee.substring(barre + 1, fermante).getIntValue();
    if (numero <= 0 || total <= 0) return;
    progress_.step = numero;
    progress_.stepCount = total;
    progress_.stepLabel = coupee.substring(fermante + 1).trim();
}

void ReconstructionRunner::publish() {
    Progress copie;
    {
        std::lock_guard<std::mutex> verrou(mutex_);
        copie = progress_;
    }
    juce::MessageManager::callAsync([this, copie] { if (onProgress) onProgress(copie); });
}

void ReconstructionRunner::run() {
    const auto arguments = chain_.commandLine(audioFile_.getFullPathName().toStdString(),
                                               outputFolder_.getFullPathName().toStdString(),
                                               viserLaParite_);
    juce::StringArray commande;
    for (const auto& argument : arguments) commande.add(juce::String::fromUTF8(argument.c_str()));

    auto enfant = std::make_unique<juce::ChildProcess>();
    // ON NE CHANGE PAS LE DOSSIER COURANT, et c'est un choix qu'il vaut mieux
    // écrire : `setAsCurrentWorkingDirectory` agit sur le PROCESSUS ENTIER, et
    // depuis un thread de fond, pendant que l'utilisateur ouvre un sélecteur de
    // fichiers dans l'autre. Un effet de bord global pour servir un processus
    // enfant serait payé par tout le reste de l'application.
    //
    // Rien ne l'exige : `reconstruire.py` ajoute lui-même son dossier au chemin
    // d'import (`sys.path.insert(0, Path(__file__).parent)`) et le pont trouve
    // `vsm-render` en remontant depuis `__file__`, pas depuis le dossier
    // courant. Tous les chemins qu'on lui passe sont absolus. Vérifié en
    // lançant la chaîne depuis un autre dossier.
    const bool demarre = enfant->start(commande, juce::ChildProcess::wantStdOut
                                                  | juce::ChildProcess::wantStdErr);
    if (!demarre) {
        juce::MessageManager::callAsync([this] {
            if (onFinished)
                onFinished(false, juce::File(),
                            juce::String::fromUTF8(u8"la chaîne d'analyse n'a pas pu être lancée"));
        });
        return;
    }
    {
        std::lock_guard<std::mutex> verrou(mutex_);
        process_ = std::move(enfant);
    }

    // LECTURE AU FIL DE L'EAU, et non à la fin. Attendre la fin pour tout
    // afficher d'un coup reviendrait à ne rien afficher : la chaîne dure
    // plusieurs minutes, et c'est exactement pendant ces minutes-là qu'on a
    // besoin de savoir qu'elle avance.
    juce::String reste;
    char tampon[4096];
    while (!threadShouldExit()) {
        juce::ChildProcess* processus = nullptr;
        {
            std::lock_guard<std::mutex> verrou(mutex_);
            processus = process_.get();
        }
        if (processus == nullptr) break;
        const int lus = processus->readProcessOutput(tampon, sizeof(tampon));
        if (lus > 0) {
            reste += juce::String::fromUTF8(tampon, lus);
            int fin;
            while ((fin = reste.indexOfChar('\n')) >= 0) {
                handleLine(reste.substring(0, fin));
                reste = reste.substring(fin + 1);
            }
            publish();
        } else if (!processus->isRunning()) {
            break;
        } else {
            wait(50);
        }
    }
    if (!reste.isEmpty()) { handleLine(reste); publish(); }

    int code = -1;
    {
        std::lock_guard<std::mutex> verrou(mutex_);
        if (process_) {
            process_->waitForProcessToFinish(5000);
            code = process_->getExitCode();
        }
        process_.reset();
    }

    const bool annule = cancelled_.load();
    juce::StringArray dernieres;
    {
        std::lock_guard<std::mutex> verrou(mutex_);
        dernieres = progress_.recentLines;
    }
    const juce::File sortie = outputFolder_;
    juce::MessageManager::callAsync([this, annule, code, sortie, dernieres] {
        if (!onFinished) return;
        if (annule) {
            onFinished(false, juce::File(),
                        juce::String::fromUTF8(u8"reconstruction interrompue"));
            return;
        }
        if (code != 0) {
            // CE QUE LA CHAÎNE A DIT EN DERNIER, et non un code de sortie nu :
            // « [ERREUR] pas de stem vocal » se comprend, « code 2 » non.
            juce::String raison = juce::String::fromUTF8(u8"la chaîne s'est arrêtée (code ")
                                  + juce::String(code) + ")";
            for (int i = dernieres.size() - 1; i >= 0 && i >= dernieres.size() - 5; --i)
                if (dernieres[i].contains("[ERREUR]")) { raison = dernieres[i]; break; }
            onFinished(false, juce::File(), raison);
            return;
        }
        onFinished(true, sortie, {});
    });
}

} // namespace vsm::app
