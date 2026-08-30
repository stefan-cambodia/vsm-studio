#include "AutosaveService.h"
#include <chrono>

namespace vsm::app {

AutosaveService::AutosaveService(const juce::File& root)
    : juce::Thread("autosave"), root_(root) {}

AutosaveService::~AutosaveService() {
    signalThreadShouldExit();
    notify();
    stopThread(4000);
    // PAS DE `endCleanly()` ICI. Un destructeur s'exécute aussi bien quand
    // l'application se ferme normalement que quand elle est démontée après une
    // erreur ; effacer le dossier des deux côtés effacerait justement ce qu'on
    // voulait garder. C'est à la fermeture explicite de le dire.
}

juce::String AutosaveService::lockNameFor(const juce::File& folder) {
    return "vsm-recuperation-" + folder.getFileName();
}

std::vector<AutosaveService::Recoverable> AutosaveService::findInterruptedSessions() {
    std::vector<Recoverable> trouvees;
    if (!root_.isDirectory()) return trouvees;

    for (const auto& entree : juce::RangedDirectoryIterator(root_, false, "*",
                                                              juce::File::findDirectories)) {
        const juce::File dossier = entree.getFile();
        if (dossier == sessionFolder_) continue;

        // LE VERROU DIT SI LE PROPRIÉTAIRE EST ENCORE LÀ. S'il s'acquiert, la
        // session qui tenait ce dossier n'existe plus : elle est morte sans se
        // fermer. Sinon, une autre fenêtre travaille dedans en ce moment, et
        // proposer de la récupérer serait absurde.
        auto sonde = std::make_unique<juce::InterProcessLock>(lockNameFor(dossier));
        if (!sonde->enter(0)) continue;
        sonde->exit();

        juce::File fiche = dossier.getChildFile(vsm::interchange::kRecoveryRecordFileName);
        vsm::interchange::RecoveryRecord record;
        if (!fiche.existsAsFile()
            || !vsm::interchange::recoveryRecordFromJson(fiche.loadFileAsString().toStdString(),
                                                          record)) {
            // Illisible ou incomplet : on l'efface au lieu de le proposer.
            // Proposer de récupérer puis échouer serait la deuxième mauvaise
            // nouvelle d'affilée.
            dossier.deleteRecursively();
            continue;
        }
        if (!dossier.getChildFile("project.json").existsAsFile()) {
            dossier.deleteRecursively();
            continue;
        }
        trouvees.push_back({dossier, record});
    }
    // La plus récente d'abord : c'est celle qu'on veut voir en premier.
    std::sort(trouvees.begin(), trouvees.end(), [](const Recoverable& a, const Recoverable& b) {
        return a.record.savedAtEpochSeconds > b.record.savedAtEpochSeconds;
    });
    return trouvees;
}

void AutosaveService::begin() {
    root_.createDirectory();
    // Un identifiant par exécution, et non un nom fixe : deux fenêtres ouvertes
    // en même temps ne doivent pas écrire dans le même dossier.
    sessionFolder_ = root_.getChildFile(juce::Uuid().toDashedString());
    sessionFolder_.createDirectory();
    lock_ = std::make_unique<juce::InterProcessLock>(lockNameFor(sessionFolder_));
    lock_->enter(0);
    startThread();
}

void AutosaveService::endCleanly() {
    signalThreadShouldExit();
    notify();
    stopThread(4000);
    if (lock_) { lock_->exit(); lock_.reset(); }
    if (sessionFolder_ != juce::File()) sessionFolder_.deleteRecursively();
}

void AutosaveService::discard(const juce::File& folder) {
    if (folder != juce::File()) folder.deleteRecursively();
}

void AutosaveService::requestSave(const vsm::sequencer::Project& project,
                                   const std::map<size_t, vsm::interchange::SynthPreset>& presets,
                                   const juce::File& originalFolder) {
    if (sessionFolder_ == juce::File()) return;

    Photo photo;
    photo.project = project;                 // copie de valeur : cohérente et rapide
    photo.presets = presets;
    photo.record.originalFolder = originalFolder == juce::File()
                                       ? std::string()
                                       : originalFolder.getFullPathName().toStdString();
    photo.record.title = project.title;
    photo.record.savedAtEpochSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    photo.record.trackCount = static_cast<int>(project.tracks.size());
    int notes = 0;
    for (const auto& piste : project.tracks) notes += static_cast<int>(piste.notes.size());
    photo.record.noteCount = notes;
    photo.valide = true;

    {
        std::lock_guard<std::mutex> verrou(mutex_);
        // ON REMPLACE, ON N'EMPILE PAS : la photo la plus récente est la seule
        // qui vaille, et empiler ferait courir le disque après l'utilisateur.
        enAttente_ = std::move(photo);
    }
    notify();
}

void AutosaveService::run() {
    while (!threadShouldExit()) {
        Photo photo;
        {
            std::lock_guard<std::mutex> verrou(mutex_);
            if (enAttente_.valide) { photo = std::move(enAttente_); enAttente_ = Photo{}; }
        }
        if (!photo.valide) { wait(500); continue; }

        // ÉCRITURE DANS UN DOSSIER PROVISOIRE PUIS BASCULE. Une sauvegarde
        // automatique interrompue EN COURS D'ÉCRITURE laisserait sinon un
        // `project.json` tronqué — c'est-à-dire qu'un plantage pendant la
        // sauvegarde détruirait la sauvegarde, ce qui est exactement le
        // scénario qu'on cherche à couvrir.
        const juce::File provisoire = sessionFolder_.getSiblingFile(
            sessionFolder_.getFileName() + "-en-cours");
        provisoire.deleteRecursively();
        provisoire.createDirectory();

        const auto ecrit = vsm::interchange::saveProjectBundle(
            photo.project, provisoire.getFullPathName().toStdString(), photo.presets);
        if (ecrit.success) {
            juce::File fiche = provisoire.getChildFile(vsm::interchange::kRecoveryRecordFileName);
            fiche.replaceWithText(juce::String::fromUTF8(
                vsm::interchange::recoveryRecordToJson(photo.record).c_str()));

            // La bascule : on vide l'ancien contenu et on déplace le nouveau.
            // `moveFileTo` d'un dossier entier n'étant pas garanti partout, on
            // recopie fichier par fichier — le volume est celui d'un projet
            // sans médias, quelques centaines de kilo-octets.
            for (const auto& vieux : juce::RangedDirectoryIterator(
                     sessionFolder_, true, "*", juce::File::findFilesAndDirectories))
                if (vieux.getFile().existsAsFile()) vieux.getFile().deleteFile();
            for (const auto& neuf : juce::RangedDirectoryIterator(
                     provisoire, true, "*", juce::File::findFiles)) {
                const juce::String relatif =
                    neuf.getFile().getRelativePathFrom(provisoire);
                const juce::File cible = sessionFolder_.getChildFile(relatif);
                cible.getParentDirectory().createDirectory();
                neuf.getFile().copyFileTo(cible);
            }
        }
        provisoire.deleteRecursively();
    }
}

} // namespace vsm::app
